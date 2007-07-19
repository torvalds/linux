#ifndef __UM_A_OUT_H
#define __UM_A_OUT_H

#include "asm/arch/a.out.h"
#include "choose-mode.h"

#undef STACK_TOP
#undef STACK_TOP_MAX

extern unsigned long stacksizelim;

extern unsigned long host_task_size;

#define STACK_ROOM (stacksizelim)

extern int honeypot;
#define STACK_TOP \
	CHOOSE_MODE((honeypot ? host_task_size : task_size), task_size)

#define STACK_TOP_MAX	STACK_TOP

#endif
