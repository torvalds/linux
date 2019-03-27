/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tyler C. Sarna.
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
 *
 * $FreeBSD$
 */

#ifndef _VIA_H
#define _VIA_H

/*
 * VIA Technologies VT82C686A Southbridge Audio Driver
 *
 * Documentation links:
 *
 * ftp://ftp.alsa-project.org/pub/manuals/via/686a.pdf
 * ftp://ftp.alsa-project.org/pub/manuals/general/ac97r21.pdf
 * ftp://ftp.alsa-project.org/pub/manuals/ad/AD1881_0.pdf (example AC'97 codec)
 */

#define VIA_AC97STATUS		0x40
#define		VIA_AC97STATUS_RDY	0x01
#define		VIA_AC97STATUS_LOWPWR	0x02
#define		VIA_AC97STATUS_2RDY	0x04

#define VIA_ACLINKCTRL		0x41
#define         VIA_ACLINK_EN		0x80     /* ac link enab */
#define         VIA_ACLINK_NRST		0x40     /* ~(ac reset) */
#define         VIA_ACLINK_SYNC		0x20     /* ac sync */
#define         VIA_ACLINK_VSR		0x08     /* var. samp. rate */
#define         VIA_ACLINK_SGD		0x04     /* SGD enab */
#define         VIA_ACLINK_FM		0x02     /* FM enab */
#define         VIA_ACLINK_SB		0x01     /* SB enab */
#define		VIA_ACLINK_DESIRED	(VIA_ACLINK_EN|VIA_ACLINK_NRST|VIA_ACLINK_VSR|VIA_ACLINK_SGD)
#define VIA_PCICONF_FUNC_EN	0x42

#define VIA_PLAY_STAT                 0x00
#define VIA_RECORD_STAT               0x10
#define         VIA_RPSTAT_INTR               0x03
#define VIA_PLAY_CONTROL              0x01
#define VIA_RECORD_CONTROL            0x11
#define         VIA_RPCTRL_START              0x80
#define         VIA_RPCTRL_TERMINATE          0x40
#define VIA_PLAY_MODE                 0x02
#define VIA_RECORD_MODE               0x12
#define         VIA_RPMODE_INTR_FLAG          0x01
#define         VIA_RPMODE_INTR_EOL           0x02
#define         VIA_RPMODE_STEREO             0x10
#define         VIA_RPMODE_16BIT              0x20
#define         VIA_RPMODE_AUTOSTART          0x80
#define VIA_PLAY_DMAOPS_BASE          0x04
#define VIA_RECORD_DMAOPS_BASE        0x14
#define VIA_PLAY_DMAOPS_COUNT         0x0C
#define VIA_RECORD_DMAOPS_COUNT       0x1C

#define VIA_CODEC_CTL                 0x80
#define         VIA_CODEC_READ                0x00800000
#define         VIA_CODEC_BUSY                0x01000000
#define         VIA_CODEC_PRIVALID            0x02000000
#define         VIA_CODEC_INDEX(x)            ((x)<<16)

#endif /* _VIA_H */
