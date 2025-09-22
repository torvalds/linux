/*	$OpenBSD: dl10019reg.h,v 1.2 2008/06/26 05:42:15 ray Exp $	*/
/*	$NetBSD$	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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

/*
 * Registers on D-Link DL10019 and DL10022 NE2000-compatible Ethernet
 * chips.
 */

#ifndef _DEV_IC_DL10019_REG_H_
#define	_DEV_IC_DL10019_REG_H_

/*
 * Page 0 register offsets.
 */
#define	NEDL_DL0_GPIO		0x1c	/* general purpose I/O */

#define	DL0_GPIO_MII_CLK	0x80	/* MII clock */
#define	DL0_GPIO_MII_DATAOUT	0x40	/* MII data MAC->PHY */
#define	DL0_22_GPIO_MII_DIROUT	0x20	/* MII direction MAC->PHY */
#define	DL0_19_GPIO_MII_DIROUT	0x10	/* MII direction MAC->PHY */
#define	DL0_GPIO_MII_DATAIN	0x10	/* MII data PHY->MAC */
#define	DL0_GPIO_PRESERVE	0x0f	/* must preserve these bits! */

#define	NEDL_DL0_DIAG		0x1d	/* diagnostics register */

#define	DL0_DIAG_NOCOLLDETECT	0x04	/* disable collision detection */

#endif /* _DEV_IC_DL10019_REG_H_ */
