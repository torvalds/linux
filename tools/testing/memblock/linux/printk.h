/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PRINTK_H
#define _PRINTK_H

#include <stdio.h>
#include <asm/bug.h>

/*
 * memblock_dbg is called with u64 arguments that don't match the "%llu"
 * specifier in printf. This results in warnings that cannot be fixed without
 * modifying memblock.c, which we wish to avoid. As these messaged are not used
 * in testing anyway, the mismatch can be ignored.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
#define printk printf
#pragma GCC diagnostic push

#define pr_info printk
#define pr_debug printk
#define pr_cont printk
#define pr_err printk
#define pr_warn printk

#endif
