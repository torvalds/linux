/*	$OpenBSD: ax88190reg.h,v 1.3 2008/06/26 05:42:15 ray Exp $	*/
/*	$NetBSD$	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Enami Tsugutomo.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_IC_AX88190REG_H_
#define	_DEV_IC_AX88190REG_H_

#define	AX88190_MEMR		0x04	/* MII/EEPROM/ Management Register */
#define	AX88190_MEMR_MDC	0x01	/* MII Clock */
#define	AX88190_MEMR_MDIR	0x02	/* MII STA MDIO signal direction
					   assert -> input */
#define	AX88190_MEMR_MDI	0x04	/* MII Data In */
#define	AX88190_MEMR_MDO	0x08	/* MII Data Out */
#define	AX88190_MEMR_EECS	0x10	/* EEPROM Chip Select */
#define	AX88190_MEMR_EEI	0x20	/* EEPROM Data In */
#define	AX88190_MEMR_EEO	0x40	/* EEPROM Data Out */
#define	AX88190_MEMR_EECLK	0x80	/* EEPROM Clock */

/*
 * Offset of LAN IOBASE0 and IOBASE1, and its size.
 */
#define	AX88190_LAN_IOBASE	0x3ca
#define	AX88190_LAN_IOSIZE	4
#define	AX88790_CSR		0x3c2
#define	AX88790_CSR_SIZE	2

/*
 * Offset of NODE ID in SRAM memory of ASIX AX88190.
 */
#define	AX88190_NODEID_OFFSET	0x400

/*
 * Start of SRAM buffer.
 */
#define	AX88190_BUFFER_START	0x800

#endif /* _DEV_IC_AX88190REG_H_ */
