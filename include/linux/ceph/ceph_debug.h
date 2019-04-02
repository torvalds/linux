/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_DE_H
#define _FS_CEPH_DE_H

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/string.h>

#ifdef CONFIG_CEPH_LIB_PRETTYDE

/*
 * wrap pr_de to include a filename:lineno prefix on each line.
 * this incurs some overhead (kernel size and execution time) due to
 * the extra function call at each call site.
 */

# if defined(DE) || defined(CONFIG_DYNAMIC_DE)
#  define dout(fmt, ...)						\
	pr_de("%.*s %12.12s:%-4d : " fmt,				\
		 8 - (int)sizeof(KBUILD_MODNAME), "    ",		\
		 kbasename(__FILE__), __LINE__, ##__VA_ARGS__)
# else
/* faux printk call just to see any compiler warnings. */
#  define dout(fmt, ...)	do {				\
		if (0)						\
			printk(KERN_DE fmt, ##__VA_ARGS__);	\
	} while (0)
# endif

#else

/*
 * or, just wrap pr_de
 */
# define dout(fmt, ...)	pr_de(" " fmt, ##__VA_ARGS__)

#endif

#endif
