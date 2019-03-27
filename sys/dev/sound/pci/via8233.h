/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Orion Hodson <orion@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHERIN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THEPOSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_SOUND_PCI_VIA8233_H_
#define _SYS_SOUND_PCI_VIA8233_H_

/*
 * VIA Technologies VT8233 Southbridge Audio Driver
 *
 * Documentation sources:
 *
 * o V8233C specs. from VIA, gratefully received under NDA.
 * o AC97 R2.2 specs.
 * o ALSA driver (very useful comments)
 */

#define	VIA_PCI_SPDIF		0x49
#define		VIA_SPDIF_EN		0x08

#define VIA_DXS0_BASE		0x00
#define VIA_DXS1_BASE		0x10
#define VIA_DXS2_BASE		0x20
#define VIA_DXS3_BASE		0x30
#define VIA_DXS_BASE(n)		(0x10 * (n))
#define BASE_IS_VIA_DXS_REG(x)	((x) <= VIA_DXS3_BASE)

#define VIA8233_RP_DXS_LVOL	      0x02
#define VIA8233_RP_DXS_RVOL	      0x03
#define 	VIA8233_DXS_MUTE		0x3f
#define VIA8233_RP_DXS_RATEFMT	      0x08
#define		VIA8233_DXS_STOP_INDEX		0xff000000
#define 	VIA8233_DXS_RATEFMT_48K		0x000fffff
#define		VIA8233_DXS_RATEFMT_STEREO	0x00100000
#define		VIA8233_DXS_RATEFMT_16BIT	0x00200000

#define VIA_PCI_ACLINK_STAT	0x40
#	define VIA_PCI_ACLINK_C11_READY	0x20
#	define VIA_PCI_ACLINK_C10_READY	0x10
#	define VIA_PCI_ACLINK_C01_READY	0x04
#	define VIA_PCI_ACLINK_LOW_POWER	0x02
#	define VIA_PCI_ACLINK_C00_READY	0x01

#define VIA_PCI_ACLINK_CTRL	0x41
#	define VIA_PCI_ACLINK_EN	0x80
#	define VIA_PCI_ACLINK_NRST	0x40
#	define VIA_PCI_ACLINK_SYNC	0x20
#	define VIA_PCI_ACLINK_SERIAL	0x10
#	define VIA_PCI_ACLINK_VRATE	0x08
#	define VIA_PCI_ACLINK_SGD	0x04
#	define VIA_PCI_ACLINK_DESIRED 	(VIA_PCI_ACLINK_EN | 		      \
					 VIA_PCI_ACLINK_NRST |		      \
					 VIA_PCI_ACLINK_VRATE | 	      \
					 VIA_PCI_ACLINK_SGD)

#define VIA_MC_SGD_STATUS	0x40
#define VIA_WR0_SGD_STATUS	0x60
#define VIA_WR1_SGD_STATUS	0x70
#	define SGD_STATUS_ACTIVE	0x80
#	define SGD_STATUS_AT_STOP	0x40
#	define SGD_STATUS_TRIGGER_Q	0x08
#	define SGD_STATUS_STOP_I_S	0x04
#	define SGD_STATUS_EOL		0x02
#	define SGD_STATUS_FLAG		0x01
#	define SGD_STATUS_INTR		(SGD_STATUS_EOL | SGD_STATUS_FLAG)

#define VIA_WR_BASE(n)			(0x60 + (n) * 0x10)

#define VIA_MC_SGD_CONTROL	0x41
#define VIA_WR0_SGD_CONTROL	0x61
#define VIA_WR1_SGD_CONTROL	0x71
#	define SGD_CONTROL_START	0x80
#	define SGD_CONTROL_STOP		0x40
#	define SGD_CONTROL_AUTOSTART	0x20
#	define SGD_CONTROL_PAUSE	0x08
#	define SGD_CONTROL_I_STOP	0x04
#	define SGD_CONTROL_I_EOL	0x02
#	define SGD_CONTROL_I_FLAG	0x01

#define VIA_MC_SGD_FORMAT	0x42
#	define MC_SGD_16BIT		0x80
#	define MC_SGD_8BIT		0x00
#	define MC_SGD_CHANNELS(x)	(((x)& 0x07) << 4)

#define VIA_WR0_SGD_FORMAT	0x62
#define VIA_WR1_SGD_FORMAT	0x72
#define VIA_WR_RP_SGD_FORMAT		0x02
#	define WR_FIFO_ENABLE		0x40

#define VIA_WR0_SGD_INPUT	0x63
#define VIA_WR1_SGD_INPUT	0x73
#	define WR_LINE_IN		0x00
#	define WR_MIC_IN		0x04
#	define WR_PRIMARY_CODEC		0x00
#	define WR_SECONDARY_CODEC1	0x01
#	define WR_SECONDARY_CODEC2	0x02
#	define WR_SECONDARY_CODEC3	0x03

#define VIA_MC_TABLE_PTR_BASE	0x44
#define VIA_WR0_TABLE_PTR_BASE	0x64
#define VIA_WR1_TABLE_PTR_BASE	0x74

#define VIA_MC_SLOT_SELECT	0x48
#	define SLOT3(x)			(x)
#	define SLOT4(x)			((x) << 4)
#	define SLOT7(x)			((x) << 8)
#	define SLOT8(x)			((x) << 12)
#	define SLOT6(x)			((x) << 16)
#	define SLOT9(x)			((x) << 20)

#define VIA_MC_CURRENT_COUNT	0x4c

#define VIA_WR0_FORMAT		0x68
#define VIA_WR1_FORMAT		0x78
#	define WR_FORMAT_STOP_INDEX	0xff000000
#	define WR_FORMAT_STEREO		0x00100000
#	define WR_FORMAT_16BIT		0x00200000

/* Relative offsets */
#define VIA_RP_STATUS		0x00
#define VIA_RP_CONTROL		0x01
#define VIA_RP_TABLE_PTR	0x04
#define VIA_RP_CURRENT_COUNT	0x0c

#define VIA_AC97_CONTROL	0x80
#	define VIA_AC97_CODECID11	0xc0000000
#	define VIA_AC97_CODECID10	0x80000000
#	define VIA_AC97_CODECID01	0x40000000
#	define VIA_AC97_CODEC11_VALID	0x20000000
#	define VIA_AC97_CODEC10_VALID	0x10000000
#	define VIA_AC97_CODEC01_VALID	0x08000000
#	define VIA_AC97_CODEC00_VALID	0x02000000
#	define VIA_AC97_BUSY		0x01000000
#	define VIA_AC97_READ		0x00800000
#	define VIA_AC97_INDEX(x)	((x) << 16)
#	define VIA_AC97_DATA(x)		((x) & 0xffff)

#define         VIA_CODEC_BUSY                0x01000000
#define         VIA_CODEC_PRIVALID            0x02000000
#define         VIA_CODEC_INDEX(x)            ((x)<<16)

#endif /* SYS_SOUND_PCI_VIA8233_H_ */
