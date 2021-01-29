/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#include "hilevel.h"

pcb_t procTab[MAX_PROCS]; pcb_t* executing = NULL;
int next_i = 0;
int next_pid = 1;

void dispatch(ctx_t* ctx, pcb_t* prev, pcb_t* next) {
	char prev_pid = '?', next_pid = '?';

	if (NULL != prev) {
		memcpy(&prev->ctx, ctx, sizeof(ctx_t)); // preserve execution context of P_{prev}
		prev_pid = '0' + prev->pid;
	}
	if (NULL != next) {
		memcpy(ctx, &next->ctx, sizeof(ctx_t)); // restore  execution context of P_{next}
		next_pid = '0' + next->pid;
	}

	PL011_putc(UART0, '[', true);
	PL011_putc(UART0, prev_pid, true);
	PL011_putc(UART0, '-', true);
	PL011_putc(UART0, '>', true);
	PL011_putc(UART0, next_pid, true);
	PL011_putc(UART0, ']', true);

	executing = next;                           // update   executing process to P_{next}

	return;
}

int get_number_of_ready_processes() {
    int number_of_ready_processes = 0;

    for (int i = 0; i < MAX_PROCS; i++) {
    	if (procTab[i].status == STATUS_READY) {
    	    number_of_ready_processes += 1;
    	}
    }
}

int get_current_process_index() {
    int current_process_index = -1;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (executing->pid == procTab[i].pid) {
            current_process_index = i;
        }
    }

    return current_process_index;
}

int get_highest_priority_index() {
    int highest_priority = 0;
    int highest_priority_index = 0;

    for (int i = 0; i < MAX_PROCS; i++) {
        if (procTab[i].status == STATUS_READY) {
            if (procTab[i].base_priority + procTab[i].age > highest_priority) {
                highest_priority_index = i;
                highest_priority = procTab[i].base_priority + procTab[i].age;
            }
        }
    }

    return highest_priority_index;
}

void adjust_priorities(int current_process_index) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (i != current_process_index) {
            procTab[i].age += 1;
        } else {
            procTab[i].age = 0;
        }
    }
}

// Returns the first empty index in procTab, if its full return -1
int get_next_free_index() {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procTab[i].status == STATUS_INVALID) {
            return i;
        }
    }

    return -1;
}

// Returns the index of a given pid, if it doesn't exist return -1
int get_index_from_pid(int pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procTab[i].pid == pid) {
            return i;
        }
    }

    return -1;
}

void schedule(ctx_t* ctx) {
    int number_of_ready_processes = get_number_of_ready_processes();
    int current_process_index = get_current_process_index();
    adjust_priorities(current_process_index);

    int next_index = get_highest_priority_index();

    if (current_process_index != -1) {
        dispatch(ctx, &procTab[current_process_index], &procTab[next_index]);

        procTab[current_process_index].status = STATUS_READY;
    } else {
        dispatch(ctx, NULL, &procTab[next_index]);
    }
    procTab[next_index].status = STATUS_EXECUTING;

    return;
}

extern void     main_console();
extern uint32_t tos_console;
extern uint32_t tos_P3;

void hilevel_handler_rst(ctx_t* ctx) {
  /* Configure the mechanism for interrupt handling by
   *
   * - configuring timer st. it raises a (periodic) interrupt for each
   *   timer tick,
   * - configuring GIC st. the selected interrupts are forwarded to the
   *   processor via the IRQ interrupt signal, then
   * - enabling IRQ interrupts.
   */

	for (int i = 0; i < MAX_PROCS; i++) {
		procTab[i].status = STATUS_INVALID;
	}

	memset(&procTab[0], 0, sizeof(pcb_t)); // initialise 0-th PCB = console
    procTab[0].pid = 1;
    procTab[0].status = STATUS_READY;
    procTab[0].tos = (uint32_t)(&tos_console);
    procTab[0].ctx.cpsr = 0x50;
    procTab[0].ctx.pc = (uint32_t)(&main_console);
    procTab[0].ctx.sp = procTab[0].tos;
    procTab[0].base_priority = 1;
    procTab[0].age = 0;
    procTab[0].parent_pid = 0;

    next_pid = 2;

  TIMER0->Timer1Load  = 0x00100000; // select period = 2^20 ticks ~= 1 sec
  TIMER0->Timer1Ctrl  = 0x00000002; // select 32-bit   timer
  TIMER0->Timer1Ctrl |= 0x00000040; // select periodic timer
  TIMER0->Timer1Ctrl |= 0x00000020; // enable          timer interrupt
  TIMER0->Timer1Ctrl |= 0x00000080; // enable          timer

  GICC0->PMR          = 0x000000F0; // unmask all            interrupts
  GICD0->ISENABLER1  |= 0x00000010; // enable timer          interrupt
  GICC0->CTLR         = 0x00000001; // enable GIC interface
  GICD0->CTLR         = 0x00000001; // enable GIC distributor

  int_enable_irq();

  schedule(ctx);

  return;
}

void hilevel_handler_irq(ctx_t* ctx) {
  // Step 2: read  the interrupt identifier so we know the source.

  uint32_t id = GICC0->IAR;

  // Step 4: handle the interrupt, then clear (or reset) the source.

  if( id == GIC_SOURCE_TIMER0 ) {
    TIMER0->Timer1IntClr = 0x01;
	schedule(ctx);
  }

  // Step 5: write the interrupt identifier to signal we're done.

  GICC0->EOIR = id;

  return;
}

void hilevel_handler_svc( ctx_t* ctx, uint32_t id ) {
  /* Based on the identifier (i.e., the immediate operand) extracted from the
   * svc instruction,
   *
   * - read  the arguments from preserved usr mode registers,
   * - perform whatever is appropriate for this system call, then
   * - write any return value back to preserved usr mode registers.
   */

  switch( id ) {
    case SYS_YIELD : { // 0x00 => yield()
      schedule( ctx );
      break;
    }

    case SYS_WRITE : { // 0x01 => write( fd, x, n )
      int   fd = ( int   )( ctx->gpr[ 0 ] );
      char*  x = ( char* )( ctx->gpr[ 1 ] );
      int    n = ( int   )( ctx->gpr[ 2 ] );

      for( int i = 0; i < n; i++ ) {
        PL011_putc( UART0, *x++, true );
      }

      ctx->gpr[ 0 ] = n;

      break;
    }

    case SYS_FORK : {
        int next_free_index = get_next_free_index();

        // Checks if already at max processes
        if (next_free_index == -1) {
            return;
        }

        //todo allocate space for new stack
        uint32_t new_tos = tos_P3;

        memset(&procTab[next_free_index], 0, sizeof(pcb_t)); // initialise free PCB = child process
        procTab[next_free_index].pid = next_pid;

        next_pid += 1;

        procTab[next_free_index].status = STATUS_READY;
        procTab[next_free_index].tos = (uint32_t) new_tos;

        procTab[next_free_index].base_priority = executing->base_priority;
        procTab[next_free_index].age = 0;
        procTab[next_free_index].parent_pid = executing->pid;


        // Copy execution context
        procTab[next_free_index].ctx.cpsr = ctx->cpsr;
        procTab[next_free_index].ctx.pc = ctx->pc;

        for (int i = 0; i < 13; i++) {
            procTab[next_free_index].ctx.gpr[i] = ctx->gpr[i];
        }

        procTab[next_free_index].ctx.sp = ctx->sp;
        procTab[next_free_index].ctx.lr = ctx->lr;


        PL011_putc(UART0, 'F', true);

        // Parent returns child pid, child returns 0
        procTab[next_free_index].ctx.gpr[0] = 0;
        ctx->gpr[0] = procTab[next_free_index].pid;

        return;
    }

    case SYS_EXEC: {
        void*   addr = ( void*   )( ctx->gpr[ 0 ] );
        int index = get_current_process_index();

        procTab[index].ctx.cpsr = 0x50;
        procTab[index].ctx.pc = (uint32_t)(&addr);
        procTab[index].ctx.sp = procTab[index].tos;

        PL011_putc(UART0, 'E', true);
        break;
    }

    case SYS_KILL: {
        int   pid = (  int  )( ctx->gpr[ 0 ] );
        int   x   = (  int  )( ctx->gpr[ 1 ] );

        int index = get_index_from_pid(pid);

        procTab[index].status = STATUS_INVALID;
        PL011_putc(UART0, 'K', true);

        if (get_current_process_index() == index) {
            PL011_putc(UART0, 'R', true);
            int parent_index = get_index_from_pid(procTab[index].parent_pid);

            dispatch(ctx, &procTab[index], &procTab[parent_index]);
        }

        break;
    }

    default   : { // 0x?? => unknown/unsupported
      break;
    }
  }

  return;
}
