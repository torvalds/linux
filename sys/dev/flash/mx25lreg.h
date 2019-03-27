/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#ifndef __MX25LREG_H__
#define __MX25LREG_H__

/*
 * Commands 
 */
#define CMD_WRITE_ENABLE	0x06
#define CMD_WRITE_DISABLE	0x04
#define CMD_READ_IDENT		0x9F
#define CMD_READ_STATUS		0x05
#define CMD_WRITE_STATUS	0x01
#define CMD_READ		0x03
#define CMD_FAST_READ		0x0B
#define CMD_PAGE_PROGRAM	0x02
#define CMD_SECTOR_ERASE	0xD8
#define CMD_BULK_ERASE		0xC7
#define	CMD_BLOCK_4K_ERASE	0x20
#define	CMD_BLOCK_32K_ERASE	0x52
#define	CMD_ENTER_4B_MODE	0xB7
#define	CMD_EXIT_4B_MODE	0xE9

/* Quad 4B-addressing operations. */
#define	CMD_QUAD_SECTOR_ERASE	0xDC
#define	CMD_QUAD_PAGE_PROGRAM	0x34
#define	CMD_READ_4B_QUAD_OUTPUT	0x6C

/*
 * Status register flags
 */
#define	STATUS_SRWD	(1 << 7)
#define	STATUS_BP2	(1 << 4)
#define	STATUS_BP1	(1 << 3)
#define	STATUS_BP0	(1 << 2)
#define	STATUS_WEL	(1 << 1)
#define	STATUS_WIP	(1 << 0)

#define	FLASH_PAGE_SIZE	256

#endif /* __MX25LREG_H__ */

