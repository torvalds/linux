/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SKC_LINUX_PRINTK_H
#define _SKC_LINUX_PRINTK_H

#include <stdio.h>

/* controllable printf */
extern int pr_output;
#define printk(fmt, ...)	\
	(pr_output ? printf(fmt, __VA_ARGS__) : 0)

#define pr_err printk
#define pr_warn	printk
#define pr_info	printk
#define pr_debug printk

#endif
