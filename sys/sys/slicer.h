/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Semihalf.
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

#ifndef _FLASH_SLICER_H_
#define	_FLASH_SLICER_H_

#include <sys/types.h>

#define	FLASH_SLICES_MAX_NUM		8
#define	FLASH_SLICES_MAX_NAME_LEN	(32 + 1)

#define	FLASH_SLICES_FLAG_NONE		0
#define	FLASH_SLICES_FLAG_RO		1	/* Read only */

#define	FLASH_SLICES_FMT		"%ss.%s"

struct flash_slice {
	off_t		base;
	off_t		size;
	const char	*label;
	unsigned int	flags;
};

#ifdef _KERNEL

typedef int (*flash_slicer_t)(device_t dev, const char *provider,
    struct flash_slice *slices, int *slices_num);

#define	FLASH_SLICES_TYPE_NAND		0
#define	FLASH_SLICES_TYPE_CFI		1
#define	FLASH_SLICES_TYPE_SPI		2
#define	FLASH_SLICES_TYPE_MMC		3

/* Use NULL and set force to true for deregistering a slicer */
void flash_register_slicer(flash_slicer_t slicer, u_int type, bool force);

#endif /* _KERNEL */

#endif /* _FLASH_SLICER_H_ */
