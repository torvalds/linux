// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/lib/zlib_dfltcc/dfltcc_syms.c
 *
 * Exported symbols for the s390 zlib dfltcc support.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/zlib.h>
#include "dfltcc.h"

EXPORT_SYMBOL(dfltcc_can_deflate);
EXPORT_SYMBOL(dfltcc_deflate);
EXPORT_SYMBOL(dfltcc_reset);
MODULE_LICENSE("GPL");
