/*	$OpenBSD: ne2000reg.h,v 1.3 2006/10/20 18:27:25 brad Exp $	*/
/*	$NetBSD: ne2000reg.h,v 1.2 1997/10/14 22:54:11 thorpej Exp $	*/

/*
 * National Semiconductor DS8390 NIC register definitions.
 *
 * Copyright (C) 1993, David Greenman.  This software may be used, modified,
 * copied, distributed, and sold, in both source and binary form provided that
 * the above copyright and these terms are retained.  Under no circumstances is
 * the author responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its use.
 */

#ifndef _DEV_IC_NE2000REG_H_
#define	_DEV_IC_NE2000REG_H_

/*
 * Register group offsets from base.
 */
#define	NE2000_NIC_OFFSET	0x00
#define	NE2000_ASIC_OFFSET	0x10

#define	NE2000_NIC_NPORTS	0x10
#define	NE2000_ASIC_NPORTS	0x10
#define	NE2000_NPORTS		0x20

/*
 * NE2000 ASIC registers (given as offsets from ASIC base).
 */
#define	NE2000_ASIC_DATA	0x00	/* remote DMA/data register */
#define	NE2000_ASIC_RESET	0x0f	/* reset on read */

#endif /* _DEV_IC_NE2000REG_H_ */
