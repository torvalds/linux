/* bug.h: FRV bug trapping
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_BUG_H
#define _ASM_BUG_H

#include <linux/config.h>

#ifdef CONFIG_BUG
/*
 * Tell the user there is some problem.
 */
extern asmlinkage void __debug_bug_trap(int signr);

#ifdef CONFIG_NO_KERNEL_MSG
#define	_debug_bug_printk()
#else
extern void __debug_bug_printk(const char *file, unsigned line);
#define	_debug_bug_printk() __debug_bug_printk(__FILE__, __LINE__)
#endif

#define _debug_bug_trap(signr)			\
do {						\
	__debug_bug_trap(signr);		\
	asm volatile("nop");			\
} while(0)

#define HAVE_ARCH_BUG
#define BUG()					\
do {						\
	_debug_bug_printk();			\
	_debug_bug_trap(6 /*SIGABRT*/);		\
} while (0)

#ifdef CONFIG_GDBSTUB
#define HAVE_ARCH_KGDB_RAISE
#define kgdb_raise(signr) do { _debug_bug_trap(signr); } while(0)

#define HAVE_ARCH_KGDB_BAD_PAGE
#define kgdb_bad_page(page) do { kgdb_raise(SIGABRT); } while(0)
#endif
#endif

#include <asm-generic/bug.h>

#endif
