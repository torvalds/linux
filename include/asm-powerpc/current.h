#ifndef _ASM_POWERPC_CURRENT_H
#define _ASM_POWERPC_CURRENT_H
#ifdef __KERNEL__

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

struct task_struct;

#ifdef __powerpc64__
#include <asm/paca.h>

#define current		(get_paca()->__current)

#else

/*
 * We keep `current' in r2 for speed.
 */
register struct task_struct *current asm ("r2");

#endif

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_CURRENT_H */
