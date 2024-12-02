/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *	Atish Patra <atish.patra@wdc.com>
 */

#ifndef __TIMER_RISCV_H
#define __TIMER_RISCV_H

#include <linux/types.h>

extern void riscv_cs_get_mult_shift(u32 *mult, u32 *shift);

#endif
