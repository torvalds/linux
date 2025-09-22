/*	$OpenBSD: fmsreg.h,v 1.3 2008/06/26 05:42:17 ray Exp $	*/
/*	$NetBSD: fms.c,v 1.5.4.1 2000/06/30 16:27:50 simonb Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Witold J. Wnuk.
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


#ifndef _DEV_PCI_FMSREG_H_
#define _DEV_PCI_FMSREG_H_

#define FM_PCM_VOLUME		0x00
#define FM_FM_VOLUME		0x02
#define FM_I2S_VOLUME		0x04
#define FM_RECORD_SOURCE	0x06

#define FM_PLAY_CTL		0x08
#define  FM_PLAY_RATE_MASK		0x0f00
#define  FM_PLAY_BUF1_LAST		0x0001
#define  FM_PLAY_BUF2_LAST		0x0002
#define  FM_PLAY_START			0x0020
#define  FM_PLAY_PAUSE			0x0040
#define  FM_PLAY_STOPNOW		0x0080
#define  FM_PLAY_16BIT			0x4000
#define  FM_PLAY_STEREO			0x8000

#define FM_PLAY_DMALEN		0x0a
#define FM_PLAY_DMABUF1		0x0c
#define FM_PLAY_DMABUF2		0x10


#define FM_REC_CTL		0x14
#define  FM_REC_RATE_MASK		0x0f00
#define  FM_REC_BUF1_LAST		0x0001
#define  FM_REC_BUF2_LAST		0x0002
#define  FM_REC_START			0x0020
#define  FM_REC_PAUSE			0x0040
#define  FM_REC_STOPNOW			0x0080
#define  FM_REC_16BIT			0x4000
#define  FM_REC_STEREO			0x8000


#define FM_REC_DMALEN		0x16
#define FM_REC_DMABUF1		0x18
#define FM_REC_DMABUF2		0x1c

#define FM_CODEC_CTL		0x22
#define FM_VOLUME		0x26
#define  FM_VOLUME_MUTE			0x8000

#define FM_CODEC_CMD		0x2a
#define  FM_CODEC_CMD_READ		0x0080
#define  FM_CODEC_CMD_VALID		0x0100
#define  FM_CODEC_CMD_BUSY		0x0200

#define FM_CODEC_DATA		0x2c

#define FM_IO_CTL		0x52
#define  FM_IO_GPIO(x)			((x) << 12)
#define  FM_IO_GPIO_ALL			FM_IO_GPIO(0xf)
#define  FM_IO_GPIO_IN(x)		((x) << 8)
#define  FM_IO_PIN0			0x0001
#define  FM_IO_PIN1			0x0002
#define  FM_IO_PIN2			0x0004
#define  FM_IO_PIN3			0x0008

#define FM_CARD_CTL		0x54

#define FM_INTMASK		0x56
#define  FM_INTMASK_PLAY		0x0001
#define  FM_INTMASK_REC			0x0002
#define  FM_INTMASK_VOL			0x0040
#define  FM_INTMASK_MPU			0x0080

#define FM_INTSTATUS		0x5a
#define  FM_INTSTATUS_PLAY		0x0100
#define  FM_INTSTATUS_REC		0x0200
#define  FM_INTSTATUS_VOL		0x4000
#define  FM_INTSTATUS_MPU		0x8000

#endif /* _DEV_PCI_FMSREG_H_ */
