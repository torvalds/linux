/*
 * include/asm-xtensa/bug.h
 *
 * Macros to cause a 'bug' message.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_BUG_H
#define _XTENSA_BUG_H

#include <linux/stringify.h>

#define ILL	__asm__ __volatile__ (".byte 0,0,0\n")

#ifdef CONFIG_KALLSYMS
# define BUG() do {							\
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__);		\
	ILL;								\
} while (0)
#else
# define BUG() do {							\
	printk("kernel BUG!\n");					\
      	ILL;								\
} while (0)
#endif

#define BUG_ON(condition) do { if (unlikely((condition)!=0)) BUG(); } while(0)
#define PAGE_BUG(page) do {  BUG(); } while (0)
#define WARN_ON(condition) do {						   \
  if (unlikely((condition)!=0)) {					   \
    printk ("Warning in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
      dump_stack();							   \
  }									   \
} while (0)

#endif	/* _XTENSA_BUG_H */
