/*
 *  include/asm-sparc/current.h
 *
 * Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright (C) 2002 Pete Zaitcev (zaitcev@yahoo.com)
 *
 *  Derived from "include/asm-s390/current.h" by
 *  Martin Schwidefsky (schwidefsky@de.ibm.com)
 *  Derived from "include/asm-i386/current.h"
 */
#ifndef _ASM_CURRENT_H
#define _ASM_CURRENT_H

/*
 * At the sparc64 DaveM keeps current_thread_info in %g4.
 * We might want to consider doing the same to shave a few cycles.
 */

#include <linux/thread_info.h>

struct task_struct;

/* Two stage process (inline + #define) for type-checking. */
/* We also obfuscate get_current() to check if anyone used that by mistake. */
static inline struct task_struct *__get_current(void)
{
	return current_thread_info()->task;
}
#define current __get_current()

#endif /* !(_ASM_CURRENT_H) */
