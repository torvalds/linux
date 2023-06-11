/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_GENERIC_EXPORT_H
#define __ASM_GENERIC_EXPORT_H

/*
 * <asm/export.h> and <asm-generic/export.h> are deprecated.
 * Please include <linux/export.h> directly.
 */
#include <linux/export.h>

#define EXPORT_DATA_SYMBOL(name)	EXPORT_SYMBOL(name)
#define EXPORT_DATA_SYMBOL_GPL(name)	EXPORT_SYMBOL_GPL(name)

#endif
