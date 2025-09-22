/*	$OpenBSD: auxioreg.h,v 1.5 2019/12/05 12:46:54 mpi Exp $	*/
/*	$NetBSD: auxioreg.h,v 1.3 2000/04/15 03:08:13 mrg Exp $	*/

/*
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
 * and power (POWER) devices on the Ebus2 have their AUXIO registers mapped
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
/* XXX: these may be useless on Ebus2 auxio! find out! */
#define	AUXIO_LED_FHD		0x20	/* floppy: high density (unreliable?)*/
#define	AUXIO_LED_LTE		0x08	/* link-test enable */
#define	AUXIO_LED_MMUX		0x04	/* Monitor/Mouse MUX; what is it? */
#define	AUXIO_LED_FTC		0x02	/* floppy: drives Terminal Count pin */
#define	AUXIO_LED_LED		0x01	/* front panel LED */
#define	AUXIO_LED_FLOPPY_MASK		(AUXIO_LED_FTC)

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
