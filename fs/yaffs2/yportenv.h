/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */


#ifndef __YPORTENV_H__
#define __YPORTENV_H__

/*
 * Define the MTD version in terms of Linux Kernel versions
 * This allows yaffs to be used independantly of the kernel
 * as well as with it.
 */

#define MTD_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))

#if defined CONFIG_YAFFS_WINCE

#include "ywinceenv.h"

#elif defined __KERNEL__

#include "moduleconfig.h"

/* Linux kernel */

#include <linux/version.h>
#define MTD_VERSION_CODE LINUX_VERSION_CODE

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19))
#include <linux/config.h>
#endif
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define YCHAR char
#define YUCHAR unsigned char
#define _Y(x)     x
#define yaffs_strcat(a, b)     strcat(a, b)
#define yaffs_strcpy(a, b)     strcpy(a, b)
#define yaffs_strncpy(a, b, c) strncpy(a, b, c)
#define yaffs_strncmp(a, b, c) strncmp(a, b, c)
#define yaffs_strnlen(s,m)	strnlen(s,m)
#define yaffs_sprintf	       sprintf
#define yaffs_toupper(a)       toupper(a)

#define Y_INLINE inline

#define YAFFS_LOSTNFOUND_NAME		"lost+found"
#define YAFFS_LOSTNFOUND_PREFIX		"obj"

/* #define YPRINTF(x) printk x */
#define YMALLOC(x) kmalloc(x, GFP_NOFS)
#define YFREE(x)   kfree(x)
#define YMALLOC_ALT(x) vmalloc(x)
#define YFREE_ALT(x)   vfree(x)
#define YMALLOC_DMA(x) YMALLOC(x)

#define YYIELD() schedule()
#define Y_DUMP_STACK() dump_stack()

#define YAFFS_ROOT_MODE			0755
#define YAFFS_LOSTNFOUND_MODE		0700

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 5, 0))
#define Y_CURRENT_TIME CURRENT_TIME.tv_sec
#define Y_TIME_CONVERT(x) (x).tv_sec
#else
#define Y_CURRENT_TIME CURRENT_TIME
#define Y_TIME_CONVERT(x) (x)
#endif

#define yaffs_SumCompare(x, y) ((x) == (y))
#define yaffs_strcmp(a, b) strcmp(a, b)

#define TENDSTR "\n"
#define TSTR(x) KERN_DEBUG x
#define TCONT(x) x
#define TOUT(p) printk p

#define compile_time_assertion(assertion) \
	({ int x = __builtin_choose_expr(assertion, 0, (void)0); (void) x; })

#elif defined CONFIG_YAFFS_DIRECT

#define MTD_VERSION_CODE MTD_VERSION(2, 6, 22)

/* Direct interface */
#include "ydirectenv.h"

#elif defined CONFIG_YAFFS_UTIL

/* Stuff for YAFFS utilities */

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#include "devextras.h"

#define YMALLOC(x) malloc(x)
#define YFREE(x)   free(x)
#define YMALLOC_ALT(x) malloc(x)
#define YFREE_ALT(x) free(x)

#define YCHAR char
#define YUCHAR unsigned char
#define _Y(x)     x
#define yaffs_strcat(a, b)     strcat(a, b)
#define yaffs_strcpy(a, b)     strcpy(a, b)
#define yaffs_strncpy(a, b, c) strncpy(a, b, c)
#define yaffs_strnlen(s,m)	       strnlen(s,m)
#define yaffs_sprintf	       sprintf
#define yaffs_toupper(a)       toupper(a)

#define Y_INLINE inline

/* #define YINFO(s) YPRINTF(( __FILE__ " %d %s\n",__LINE__,s)) */
/* #define YALERT(s) YINFO(s) */

#define TENDSTR "\n"
#define TSTR(x) x
#define TOUT(p) printf p

#define YAFFS_LOSTNFOUND_NAME		"lost+found"
#define YAFFS_LOSTNFOUND_PREFIX		"obj"
/* #define YPRINTF(x) printf x */

#define YAFFS_ROOT_MODE			0755
#define YAFFS_LOSTNFOUND_MODE		0700

#define yaffs_SumCompare(x, y) ((x) == (y))
#define yaffs_strcmp(a, b) strcmp(a, b)

#else
/* Should have specified a configuration type */
#error Unknown configuration

#endif

#ifndef Y_DUMP_STACK
#define Y_DUMP_STACK() do { } while (0)
#endif

#ifndef YBUG
#define YBUG() do {\
	T(YAFFS_TRACE_BUG,\
		(TSTR("==>> yaffs bug: " __FILE__ " %d" TENDSTR),\
		__LINE__));\
	Y_DUMP_STACK();\
} while (0)
#endif

#endif
