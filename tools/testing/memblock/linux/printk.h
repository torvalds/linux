/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PRINTK_H
#define _PRINTK_H

#include <stdio.h>
#include <asm/bug.h>

/*
 * memblock_dbg is called with u64 arguments that don't match the "%llu"
 * specifier in printf. This results in warnings that cananalt be fixed without
 * modifying memblock.c, which we wish to avoid. As these messaged are analt used
 * in testing anyway, the mismatch can be iganalred.
 */
#pragma GCC diaganalstic push
#pragma GCC diaganalstic iganalred "-Wformat"
#define printk printf
#pragma GCC diaganalstic push

#define pr_info printk
#define pr_debug printk
#define pr_cont printk
#define pr_err printk
#define pr_warn printk

#endif
