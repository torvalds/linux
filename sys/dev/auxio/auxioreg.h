/*	$FreeBSD$ */
/*	$NetBSD: auxioreg.h,v 1.4 2001/10/22 07:31:41 mrg Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2000 Matthew R. Green
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * The AUXIO registers; their offset in the Ebus2 address space, plus the
 * bits for each register.  Note that the fdthree (FD), SUNW,CS4231 (AUDIO)
 * and power (POWER) devices on the Ebus2 have their AUXIO regsiters mapped
 * into their own "reg" properties, not the "auxio" device's "reg" properties.
 */
#define	AUXIO_FD			0x00720000
#define	AUXIO_FD_DENSENSE_INPUT		0x0
#define	AUXIO_FD_DENSENSE_OUTPUT	0x1

#define	AUXIO_AUDIO			0x00722000
#define	AUXIO_AUDIO_POWERDOWN		0x0

#define	AUXIO_POWER			0x00724000
#define	AUXIO_POWER_SYSTEM_OFF		0x0
#define	AUXIO_POWER_COURTESY_OFF	0x1

#define	AUXIO_LED			0x00726000
#define	AUXIO_LED_LED			1

#define	AUXIO_PCI			0x00728000
#define	AUXIO_PCI_SLOT0			0x0	/* two bits each */
#define	AUXIO_PCI_SLOT1			0x2
#define	AUXIO_PCI_SLOT2			0x4
#define	AUXIO_PCI_SLOT3			0x6
#define	AUXIO_PCI_MODE			0x8

#define	AUXIO_FREQ			0x0072a000
#define	AUXIO_FREQ_FREQ0		0x0
#define	AUXIO_FREQ_FREQ1		0x1
#define	AUXIO_FREQ_FREQ2		0x2

#define	AUXIO_SCSI			0x0072c000
#define	AUXIO_SCSI_INT_OSC_EN		0x0
#define	AUXIO_SCSI_EXT_OSC_EN		0x1

#define	AUXIO_TEMP			0x0072f000
#define	AUXIO_TEMP_SELECT		0x0
#define	AUXIO_TEMP_CLOCK		0x1
#define	AUXIO_TEMP_ENABLE		0x2
#define	AUXIO_TEMP_DATAOUT		0x3
#define	AUXIO_TEMP_DATAINT		0x4
