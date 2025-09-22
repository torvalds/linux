/*	$OpenBSD: eisareg.h,v 1.4 1996/05/05 12:42:24 deraadt Exp $	*/
/*	$NetBSD: eisareg.h,v 1.3 1996/04/09 22:46:13 cgd Exp $	*/

/*
 * Copyright (c) 1995, 1996 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *      for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DEV_EISA_EISAREG_H__
#define	__DEV_EISA_EISAREG_H__

/*
 * Register (etc.) descriptions for the EISA bus.

 * Mostly culled from EISA chipset descriptions in:
 *	Intel Peripheral Components Databook (1992)
 */

/*
 * Slot I/O space size, and I/O address of a given slot.
 */
#define	EISA_SLOT_SIZE		0x1000
#define	EISA_SLOT_ADDR(s)	((s) * EISA_SLOT_SIZE)

/*
 * Slot offsets for important/standard registers.
 */
#define	EISA_SLOTOFF_VID	0xc80		/* offset of vendor id regs */
#define	EISA_NVIDREGS		2
#define	EISA_SLOTOFF_PID	0xc82		/* offset of product id regs */
#define	EISA_NPIDREGS		2

#ifdef AHA284X_HACK
/*
 * AHA-284x (VL bus) requires priming a register with the following values.
 */
#define	EISA_SLOTOFF_PRIMING	EISA_SLOTOFF_VID	/* offset */
#define	EISA_PRIMING_VID(index)	(0x80 + (index))	/* value for vendor */
#define	EISA_PRIMING_PID(index)	(0x82 + (index))	/* value for product */
#endif

/*
 * EISA ID functions, used to manipulate and decode EISA ID registers.
 * ``Somebody was let out without adult supervision.''
 */

#define	EISA_IDSTRINGLEN	8	/* length of ID string, incl. NUL */

/*
 * Vendor ID: three characters, encoded in 16 bits.
 *
 * EISA_VENDID_NODEV returns true if there's no device in the slot.
 * EISA_VENDID_IDDELAY returns true if there's a device in the slot,
 *	but that device hasn't been configured by system firmware.
 * EISA_VENDID_n returns the "n"th character of the vendor ID.
 */
#define	EISA_VENDID_NODEV(vid)						\
	    (((vid)[0] & 0x80) != 0)
#define	EISA_VENDID_IDDELAY(vid)					\
	    (((vid)[0] & 0xf0) == 0x70)
#define	EISA_VENDID_0(vid)						\
	    ((((vid)[0] & 0x7c) >> 2) + '@')
#define	EISA_VENDID_1(vid)						\
	    (((((vid)[0] & 0x03) << 3) | (((vid)[1] & 0xe0) >> 5)) + '@')
#define	EISA_VENDID_2(vid)						\
	    (((vid)[1] & 0x1f) + '@')

/*
 * Product ID: four hex digits, encoded in 16 bits (normal, sort of).
 *
 * EISA_PRIDID_n returns the "n"th hex digit of the product ID.
 */
#define	__EISA_HEX_MAP	"0123456789ABCDEF"
#define	EISA_PRODID_0(pid)						\
	    (__EISA_HEX_MAP[(((pid)[0] >> 4) & 0xf)])
#define	EISA_PRODID_1(pid)						\
	    (__EISA_HEX_MAP[(((pid)[0] >> 0) & 0xf)])
#define	EISA_PRODID_2(pid)						\
	    (__EISA_HEX_MAP[(((pid)[1] >> 4) & 0xf)])
#define	EISA_PRODID_3(pid)						\
	    (__EISA_HEX_MAP[(((pid)[1] >> 0) & 0xf)])

#endif /* !__DEV_EISA_EISAREG_H__ */
