/*	$OpenBSD: i82802reg.h,v 1.5 2022/01/09 05:42:38 jsg Exp $	*/

/*
 * Copyright (c) 2000 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Intel 82802AB/82802AC Firmware Hub
 *
 * see:	ftp://download.intel.com/design/chipsets/datashts/29065804.pdf	 
 *     and http://www.intel.com/Assets/PDF/manual/298029.pdf
 */

/*
 * unfortunately FWH does not show up in the pci device scan, 10x intel.
 * so all we do is probe for it in the pchb driver at the following address.
 */
#define	I82802_IOBASE	0xffb00000
#define	I82802_IOSIZE	0x00100000

/*
 * FWH registers
 * (table 4-4)
 */
#define	I82802_BLOCK_LK		0xf0002
#define	I82802_MINUS01_LK	0xe0002
#define	I82802_MINUS02_LK	0xd0002
#define	I82802_MINUS03_LK	0xc0002
#define	I82802_MINUS04_LK	0xb0002
#define	I82802_MINUS05_LK	0xa0002
#define	I82802_MINUS06_LK	0x90002
#define	I82802_MINUS07_LK	0x80002
#define	I82802_MINUS08_LK	0x70002
#define	I82802_MINUS09_LK	0x60002
#define	I82802_MINUS10_LK	0x50002
#define	I82802_MINUS11_LK	0x40002
#define	I82802_MINUS12_LK	0x30002
#define	I82802_MINUS13_LK	0x20002
#define	I82802_MINUS14_LK	0x10002
#define	I82802_MINUS15_LK	0x00002
#define	I82802_FGPI_REG		0xc0100

/*
 * T_BLOCK_LK and T_MINUS_* (block locking registers)
 * (table 4-5)
 */
#define	I82802_BLR_RD		0x04
#define	I82802_BLR_LD		0x02
#define	I82802_BLR_WL		0x01

/*
 * Register Based Locking Value Definitions
 * (table 4-6)
 */
#define	I82802_LV_FULL		0x00
#define	I82802_LV_WRITE		0x01
#define	I82802_LV_DOWN		0x02
#define	I82802_LV_READ		0x04

/*
 * General Purpose Inputs Register
 * (table 4-7)
 */
#define	I82802_FGPI_PIN4	0x10	/* PLCC-30/T SOP-7  */
#define	I82802_FGPI_PIN3	0x08	/* PLCC-30/T SOP-15 */
#define	I82802_FGPI_PIN2	0x04	/* PLCC-30/T SOP-16 */
#define	I82802_FGPI_PIN1	0x02	/* PLCC-30/T SOP-17 */
#define	I82802_FGPI_PIN0	0x01	/* PLCC-30/T SOP-18 */

/*
 * RNG registers
 */
#define	I82802_RNG_HWST		0xc015f
#define	I82802_RNG_HWST_PRESENT	0x40
#define	I82802_RNG_HWST_ENABLE	0x01
#define	I82802_RNG_RNGST	0xc0160
#define	I82802_RNG_RNGST_DATAV	0x01
#define	I82802_RNG_DATA		0xc0161

