/*	$OpenBSD: landiskreg.h,v 1.4 2007/04/29 17:53:37 miod Exp $	*/
/*	$NetBSD: landiskreg.h,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

/*-
 * Copyright (c) 2005 NONAKA Kimihiro
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	LANDISKREG_H__
#define	LANDISKREG_H__

#define	LANDISK_LEDCTRL		0xb0000001
#define	LANDISK_BTNSTAT		0xb0000002
#define	LANDISK_PWRMNG		0xb0000003	/* write-only */
#define	LANDISK_INTEN		0xb0000005
#define	LANDISK_PWRSW_INTCLR	0xb0000006

#define	LED_POWER_VALUE		0x01
#define	LED_ACCESS_VALUE	0x02
#define	LED_POWER_CHANGE	0x04
#define	LED_ACCESS_CHANGE	0x08

#define	BTN_SELECT_BIT		(1 << 0)
#define	BTN_COPY_BIT		(1 << 1)
#define	BTN_REMOVE_BIT		(1 << 2)
#define	BTN_POWER_BIT		(1 << 4)
#define	BTN_RESET_BIT		(1 << 5)
#define	BTN_ALL_BIT		(BTN_SELECT_BIT \
				 | BTN_COPY_BIT \
				 | BTN_REMOVE_BIT \
				 | BTN_RESET_BIT)

#define	PWRMNG_POWEROFF		0x01
#define	PWRMNG_RTC_CE		0x02

#define	INTEN_ALL_MASK		0x00
#define	INTEN_PCI0		0x01	/* re/ehci */
#define	INTEN_PCI1		0x02	/* pciide */
#define	INTEN_PCI2		0x04	/* ohci0 */
#define	INTEN_PCI3		0x08	/* ohci1 */
#define	INTEN_ICONNECT		0x10	/* wdc1 at obio */
#define	INTEN_CFIDE		0x20	/* wdc0 at obio */
#define	INTEN_PWRSW		0x40	/* power at obio */
#define	INTEN_BTN		0x80	/* btn at obio */

#define	LANDISK_INTR_PCI0	5	/* re/ehci */
#define	LANDISK_INTR_PCI1	6	/* pciide (LANDISK) */
#define	LANDISK_INTR_PCI2	7	/* ohci0 */
#define	LANDISK_INTR_PCI3	8	/* ohci1 */
#define	LANDISK_INTR_ICONNECT	9	/* wdc1 (LAN-iCN2 iConnect) */
#define	LANDISK_INTR_CFIDE	10	/* wdc0 (LAN-iCN2/USL-5P CF) */
#define	LANDISK_INTR_PWRSW	11	/* pwrsw */
#define	LANDISK_INTR_BTN	12	/* btn (USL-5P) */

#endif	/* LANDISKREG_H__ */
