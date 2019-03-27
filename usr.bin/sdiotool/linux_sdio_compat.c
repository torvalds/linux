/*-
 * Copyright (c) 2016-2017 Ilya Bakulin
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

#include <sys/ioctl.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/endian.h>
#include <sys/sbuf.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>
#include <fcntl.h>
#include <ctype.h>
#include <err.h>
#include <libutil.h>
#include <unistd.h>

#include <cam/cam.h>
#include <cam/cam_debug.h>
#include <cam/cam_ccb.h>
#include <cam/mmc/mmc_all.h>
#include <camlib.h>

#include "linux_compat.h"
#include "linux_sdio_compat.h"
#include "cam_sdio.h"

u8 sdio_readb(struct sdio_func *func, unsigned int addr, int *err_ret) {
	return sdio_read_1(func->dev, func->num, addr, err_ret);
}

unsigned char sdio_f0_readb(struct sdio_func *func, unsigned int addr, int *err_ret) {
	return sdio_readb(func, addr, err_ret);
}

u16 sdio_readw(struct sdio_func *func, unsigned int addr, int *err_ret) {
	return sdio_read_2(func->dev, func->num, addr, err_ret);
}

u32 sdio_readl(struct sdio_func *func, unsigned int addr, int *err_ret) {
	return sdio_read_4(func->dev, func->num, addr, err_ret);
}

void sdio_writeb(struct sdio_func *func, u8 b,
		 unsigned int addr, int *err_ret) {
	*err_ret = sdio_write_1(func->dev, func->num, addr, b);
}

/* Only writes to the vendor specific CCCR registers
 * (0xF0 - 0xFF) are permiited. */
void sdio_f0_writeb(struct sdio_func *func, unsigned char b,
		    unsigned int addr, int *err_ret)
{
	if (addr < 0xF0 || addr > 0xFF) {
		if (err_ret)
			*err_ret = -EINVAL;
		return;
	}
	sdio_writeb(func, b, addr, err_ret);
}

void sdio_writew(struct sdio_func *func, u16 b,
		 unsigned int addr, int *err_ret) {
	*err_ret = sdio_write_2(func->dev, func->num, addr, b);
}

void sdio_writel(struct sdio_func *func, u32 b,
		 unsigned int addr, int *err_ret) {
	*err_ret = sdio_write_4(func->dev, func->num, addr, b);
}
