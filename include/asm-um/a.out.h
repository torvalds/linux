/*
 * Copyright (C) 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __UM_A_OUT_H
#define __UM_A_OUT_H

#include "asm/arch/a.out.h"

#undef STACK_TOP
#undef STACK_TOP_MAX

extern unsigned long stacksizelim;

extern unsigned long host_task_size;

#define STACK_ROOM (stacksizelim)

#define STACK_TOP task_size

#define STACK_TOP_MAX STACK_TOP

#endif
