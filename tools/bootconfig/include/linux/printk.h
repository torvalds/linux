/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SKC_LINUX_PRINTK_H
#define _SKC_LINUX_PRINTK_H

#include <stdio.h>

#define printk(fmt, ...) printf(fmt, ##__VA_ARGS__)

#define pr_err printk
#define pr_warn	printk
#define pr_info	printk
#define pr_debug printk

#endif
