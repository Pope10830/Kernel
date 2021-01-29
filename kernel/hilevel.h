/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of 
 * which can be found via http://creativecommons.org (and should be included as 
 * LICENSE.txt within the associated archive or repository).
 */

#ifndef __HILEVEL_H
#define __HILEVEL_H

// Include functionality relating to newlib (the standard C library).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include <string.h>

// Include functionality relating to the platform.

#include   "GIC.h"
#include "PL011.h"
#include "SP804.h"

// Include functionality relating to the   kernel.

#include "lolevel.h"
#include     "int.h"

#define MAX_PROCS 3

#define SYS_YIELD     ( 0x00 )
#define SYS_WRITE     ( 0x01 )
#define SYS_READ      ( 0x02 )
#define SYS_FORK      ( 0x03 )
#define SYS_EXIT      ( 0x04 )
#define SYS_EXEC      ( 0x05 )
#define SYS_KILL      ( 0x06 )
#define SYS_NICE      ( 0x07 )

typedef int pid_t;

typedef enum {
	STATUS_INVALID,

	STATUS_CREATED,
	STATUS_TERMINATED,

	STATUS_READY,
	STATUS_EXECUTING,
	STATUS_WAITING
} status_t;

typedef struct {
	uint32_t cpsr, pc, gpr[13], sp, lr;
} ctx_t;

typedef struct {
	pid_t    pid; // Process IDentifier (PID)
	status_t status; // current status
	uint32_t    tos; // address of Top of Stack (ToS)
	ctx_t    ctx; // execution context
	uint32_t base_priority;
	uint32_t age;
	uint32_t parent_pid;
} pcb_t;

#endif
