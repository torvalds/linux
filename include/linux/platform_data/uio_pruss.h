/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/linux/platform_data/uio_pruss.h
 *
 * Platform data for uio_pruss driver
 *
 * Copyright (C) 2010-11 Texas Instruments Incorporated - https://www.ti.com/
 */

#ifndef _UIO_PRUSS_H_
#define _UIO_PRUSS_H_

/* To configure the PRUSS INTC base offset for UIO driver */
struct uio_pruss_pdata {
	u32		pintc_base;
	struct gen_pool *sram_pool;
};
#endif /* _UIO_PRUSS_H_ */
