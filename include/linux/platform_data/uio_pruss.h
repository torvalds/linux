/*
 * include/linux/platform_data/uio_pruss.h
 *
 * Platform data for uio_pruss driver
 *
 * Copyright (C) 2010-11 Texas Instruments Incorporated - https://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _UIO_PRUSS_H_
#define _UIO_PRUSS_H_

/* To configure the PRUSS INTC base offset for UIO driver */
struct uio_pruss_pdata {
	u32		pintc_base;
	struct gen_pool *sram_pool;
};
#endif /* _UIO_PRUSS_H_ */
