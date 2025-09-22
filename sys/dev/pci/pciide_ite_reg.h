/*	$OpenBSD: pciide_ite_reg.h,v 1.1 2003/12/20 08:03:55 grange Exp $	*/
/*
 * Copyright (c) 2003 Alexander Yurchenko <grange@openbsd.org>
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

#ifndef _DEV_PCI_PCIIDE_ITE_REG_H_
#define _DEV_PCI_PCIIDE_ITE_REG_H_

/*
 * Registers definition for IT8212F
 */
#define IT_CFG			0x40	/* I/O configuration */
#define IT_CFG_MASK			0x0000ffff
#define IT_CFG_IORDY(chan)		(0x0001 << (chan))
#define IT_CFG_BLID(chan)		(0x0004 << (chan))
#define IT_CFG_CABLE(chan, drive)	(0x0010 << ((chan) * 2 + (drive)))
#define IT_CFG_DECODE(chan)		(0x8000 >> ((chan) * 2))

#define IT_MODE			0x50	/* mode control / RAID function */
#define IT_MODE_MASK			0x0000ffff
#define IT_MODE_CPU			0x0001
#define IT_MODE_50MHZ(chan)		(0x0002 << (chan))
#define IT_MODE_DMA(chan, drive)	(0x0008 << ((chan) * 2 + (drive)))
#define IT_MODE_RESET			0x0080
#define IT_MODE_RAID1			0x0100

#define IT_TIM(chan)		((chan) ? 0x58 : 0x54) /* timings */
#define IT_TIM_UDMA5(drive)		(0x00800000 << (drive) * 8)

#endif	/* !_DEV_PCI_PCIIDE_ITE_REG_H_ */
