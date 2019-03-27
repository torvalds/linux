/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Ilya Bakulin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _LINUX_SDIO_COMPAT_H_
#define _LINUX_SDIO_COMPAT_H_

#include <sys/types.h>
#include "linux_compat.h"

/* Linux SDIO stack functions and definitions */
#define SDIO_CCCR_ABORT SD_IO_CCCR_CTL
#define SDIO_CCCR_IENx  SD_IO_CCCR_INT_ENABLE

struct sdio_func {
	struct cam_device *dev;
	uint8_t num;
};

u8 sdio_readb(struct sdio_func *func, unsigned int addr, int *err_ret);
unsigned char sdio_f0_readb(struct sdio_func *func,
			    unsigned int addr, int *err_ret);
u16 sdio_readw(struct sdio_func *func, unsigned int addr, int *err_ret);
u32 sdio_readl(struct sdio_func *func, unsigned int addr, int *err_ret);

void sdio_writeb(struct sdio_func *func, u8 b,
	unsigned int addr, int *err_ret);
void sdio_f0_writeb(struct sdio_func *func, unsigned char b,
		    unsigned int addr, int *err_ret);
void sdio_writew(struct sdio_func *func, u16 b,
	unsigned int addr, int *err_ret);
void sdio_writel(struct sdio_func *func, u32 b,
	unsigned int addr, int *err_ret);


#endif
