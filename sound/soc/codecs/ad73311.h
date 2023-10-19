/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * File:         sound/soc/codec/ad73311.h
 * Based on:
 * Author:       Cliff Cai <cliff.cai@analog.com>
 *
 * Created:      Thur Sep 25, 2008
 * Description:  definitions for AD73311 registers
 *
 * Modified:
 *               Copyright 2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 */

#ifndef __AD73311_H__
#define __AD73311_H__

#define AD_CONTROL	0x8000
#define AD_DATA		0x0000
#define AD_READ		0x4000
#define AD_WRITE	0x0000

/* Control register A */
#define CTRL_REG_A	(0 << 8)

#define REGA_MODE_PRO	0x00
#define REGA_MODE_DATA	0x01
#define REGA_MODE_MIXED	0x03
#define REGA_DLB		0x04
#define REGA_SLB		0x08
#define REGA_DEVC(x)		((x & 0x7) << 4)
#define REGA_RESET		0x80

/* Control register B */
#define CTRL_REG_B	(1 << 8)

#define REGB_DIRATE(x)	(x & 0x3)
#define REGB_SCDIV(x)	((x & 0x3) << 2)
#define REGB_MCDIV(x)	((x & 0x7) << 4)
#define REGB_CEE		(1 << 7)

/* Control register C */
#define CTRL_REG_C	(2 << 8)

#define REGC_PUDEV		(1 << 0)
#define REGC_PUADC		(1 << 3)
#define REGC_PUDAC		(1 << 4)
#define REGC_PUREF		(1 << 5)
#define REGC_REFUSE		(1 << 6)

/* Control register D */
#define CTRL_REG_D	(3 << 8)

#define REGD_IGS(x)		(x & 0x7)
#define REGD_RMOD		(1 << 3)
#define REGD_OGS(x)		((x & 0x7) << 4)
#define REGD_MUTE		(1 << 7)

/* Control register E */
#define CTRL_REG_E	(4 << 8)

#define REGE_DA(x)		(x & 0x1f)
#define REGE_IBYP		(1 << 5)

/* Control register F */
#define CTRL_REG_F	(5 << 8)

#define REGF_SEEN		(1 << 5)
#define REGF_INV		(1 << 6)
#define REGF_ALB		(1 << 7)

#endif
