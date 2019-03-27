/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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

#ifndef _DEV_NAND_CDEV_H_
#define _DEV_NAND_CDEV_H_

#include <sys/ioccom.h>
#include <sys/param.h>

struct nand_raw_rw {
	off_t	off;
	off_t	len;
	uint8_t	*data;
};

struct nand_oob_rw {
	uint32_t	page;
	off_t		len;
	uint8_t		*data;
};

#define NAND_IOCTL_GROUP	'N'
#define NAND_IO_ERASE		_IOWR(NAND_IOCTL_GROUP, 0x0, off_t[2])

#define NAND_IO_OOB_READ	_IOWR(NAND_IOCTL_GROUP, 0x1, struct nand_oob_rw)

#define NAND_IO_OOB_PROG	_IOWR(NAND_IOCTL_GROUP, 0x2, struct nand_oob_rw)

#define NAND_IO_RAW_READ	_IOWR(NAND_IOCTL_GROUP, 0x3, struct nand_raw_rw)

#define NAND_IO_RAW_PROG	_IOWR(NAND_IOCTL_GROUP, 0x4, struct nand_raw_rw)

#define NAND_IO_GET_STATUS	_IOWR(NAND_IOCTL_GROUP, 0x5, uint8_t)

struct page_stat_io {
	uint32_t	page_num;
	uint32_t	page_read;
	uint32_t	page_written;
	uint32_t	page_raw_read;
	uint32_t	page_raw_written;
	uint32_t	ecc_succeded;
	uint32_t	ecc_corrected;
	uint32_t	ecc_failed;
};
#define NAND_IO_PAGE_STAT	_IOWR(NAND_IOCTL_GROUP, 0x6, \
    struct page_stat_io)

struct block_stat_io {
	uint32_t	block_num;
	uint32_t	block_erased;
};
#define NAND_IO_BLOCK_STAT	_IOWR(NAND_IOCTL_GROUP, 0x7, \
    struct block_stat_io)

struct chip_param_io {
	uint32_t	page_size;
	uint32_t	oob_size;

	uint32_t	blocks;
	uint32_t	pages_per_block;
};
#define NAND_IO_GET_CHIP_PARAM	_IOWR(NAND_IOCTL_GROUP, 0x8, \
    struct chip_param_io)

#endif /* _DEV_NAND_CDEV_H_ */
