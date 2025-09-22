/*	$OpenBSD: sdmmc_ioreg.h,v 1.11 2018/08/09 13:50:15 patrick Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SDMMC_IOREG_H
#define _SDMMC_IOREG_H

/* SDIO commands */				/* response type */
#define SD_IO_SEND_OP_COND		5	/* R4 */
#define SD_IO_RW_DIRECT			52	/* R5 */
#define SD_IO_RW_EXTENDED		53	/* R5? */

/* CMD52 arguments */
#define SD_ARG_CMD52_READ		(0<<31)
#define SD_ARG_CMD52_WRITE		(1<<31)
#define SD_ARG_CMD52_FUNC_SHIFT		28
#define SD_ARG_CMD52_FUNC_MASK		0x7
#define SD_ARG_CMD52_EXCHANGE		(1<<27)
#define SD_ARG_CMD52_REG_SHIFT		9
#define SD_ARG_CMD52_REG_MASK		0x1ffff
#define SD_ARG_CMD52_DATA_SHIFT		0
#define SD_ARG_CMD52_DATA_MASK		0xff
#define SD_R5_DATA(resp)		((resp)[0] & 0xff)

/* CMD53 arguments */
#define SD_ARG_CMD53_READ		(0<<31)
#define SD_ARG_CMD53_WRITE		(1<<31)
#define SD_ARG_CMD53_FUNC_SHIFT		28
#define SD_ARG_CMD53_FUNC_MASK		0x7
#define SD_ARG_CMD53_BLOCK_MODE		(1<<27)
#define SD_ARG_CMD53_INCREMENT		(1<<26)
#define SD_ARG_CMD53_REG_SHIFT		9
#define SD_ARG_CMD53_REG_MASK		0x1ffff
#define SD_ARG_CMD53_LENGTH_SHIFT	0
#define SD_ARG_CMD53_LENGTH_MASK	0x1ff
#define SD_ARG_CMD53_LENGTH_MAX		511

/* 48-bit response decoding (32 bits w/o CRC) */
#define MMC_R4(resp)			((resp)[0])
#define MMC_R5(resp)			((resp)[0])

/* SD R4 response (IO OCR) */
#define SD_IO_OCR_MEM_READY		(1<<31)
#define SD_IO_OCR_NUM_FUNCTIONS(ocr)	(((ocr) >> 28) & 0x7)
/* XXX big fat memory present "flag" because we don't know better */
#define SD_IO_OCR_MEM_PRESENT		(0xf<<24)
#define SD_IO_OCR_MASK			0x00fffff0

/* Card Common Control Registers (CCCR) */
#define SD_IO_CCCR_START		0x00000
#define SD_IO_CCCR_SIZE			0x100
#define SD_IO_CCCR_FN_ENABLE		0x02
#define SD_IO_CCCR_FN_READY		0x03
#define SD_IO_CCCR_INT_ENABLE		0x04
#define SD_IO_CCCR_CTL			0x06
#define  CCCR_CTL_RES			(1<<3)
#define SD_IO_CCCR_BUS_WIDTH		0x07
#define  CCCR_BUS_WIDTH_1		(0<<0)
#define  CCCR_BUS_WIDTH_4		(2<<0)
#define  CCCR_BUS_WIDTH_MASK		(3<<0)
#define SD_IO_CCCR_CISPTR		0x09 /* XXX 9-10, 10-11, or 9-12 */
#define SD_IO_CCCR_SPEED		0x13
#define  CCCR_SPEED_SHS			(1<<0)
#define  CCCR_SPEED_EHS			CCCR_SPEED_SDR25
#define  CCCR_SPEED_SDR12		(0<<1)
#define  CCCR_SPEED_SDR25		(1<<1)
#define  CCCR_SPEED_SDR50		(2<<1)
#define  CCCR_SPEED_SDR104		(3<<1)
#define  CCCR_SPEED_DDR50		(4<<1)
#define  CCCR_SPEED_MASK		(0x7<<1)

/* Function Basic Registers (FBR) */
#define SD_IO_FBR_BASE(f)		((f) * 0x100)
#define SD_IO_FBR_BLOCKLEN		0x10

/* Card Information Structure (CIS) */
#define SD_IO_CIS_START			0x01000
#define SD_IO_CIS_SIZE			0x17000

/* CIS tuple codes (based on PC Card 16) */
#define SD_IO_CISTPL_NULL		0x00
#define SD_IO_CISTPL_VERS_1		0x15
#define SD_IO_CISTPL_MANFID		0x20
#define SD_IO_CISTPL_FUNCID		0x21
#define SD_IO_CISTPL_FUNCE		0x22
#define SD_IO_CISTPL_END		0xff

/* CISTPL_FUNCID codes */
#define TPLFID_FUNCTION_SDIO		0x0c

#endif
