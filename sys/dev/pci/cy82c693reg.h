/*	$OpenBSD: cy82c693reg.h,v 1.2 2008/06/26 05:42:17 ray Exp $	*/
/* $NetBSD: cy82c693reg.h,v 1.1 2000/06/06 03:07:39 thorpej Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
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

#ifndef _DEV_PCI_CY82C693REG_H_
#define	_DEV_PCI_CY82C693REG_H_

/*
 * Register definitions for the Cypress 82c693 hyperCache(tm) Stand-Alone
 * PCI Peripheral Controller with USB.
 */

#define	CYHC_CONFIG_ADDR	0x22	/* Chipset Configuration Address */
#define	CYHC_CONFIG_DATA	0x23	/* Chipset Configuration Data */

#define	CONFIG_PERIPH1		0x01	/* Peripheral Control #1 */

#define	CONFIG_PERIPH2		0x02	/* Peripheral Control #2 */

#define	CONFIG_ELCR1		0x03	/* Edge/Level Control #1 */

#define	CONFIG_ELCR2		0x04	/* Edge/Level Control #2 */

#define	CONFIG_RTC		0x05	/* RTC Configuration */

#endif /* _DEV_PCI_CY82C693REG_H_ */
