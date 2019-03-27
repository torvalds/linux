/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
 * All rights reserved.
 *
 * Derived from the public domain Linux driver
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

#ifndef _NM256_H_
#define _NM256_H_

/* The BIOS signature. */
#define NM_SIGNATURE 0x4e4d0000
/* Signature mask. */
#define NM_SIG_MASK 0xffff0000

/* Size of the second memory area. */
#define NM_PORT2_SIZE 4096

/* The base offset of the mixer in the second memory area. */
#define NM_MIXER_OFFSET 0x600

/* The base offset for the AC97 test */
#define NM_MIXER_PRESENCE 0xa06
#define NM_PRESENCE_MASK  0x050
#define NM_PRESENCE_VALUE 0x040

/* The maximum size of a coefficient entry. */
#define NM_MAX_COEFFICIENT 0x5000

/* The interrupt register. */
#define NM_INT_REG 0xa04
/* And its bits. */
#define NM_PLAYBACK_INT 0x40
#define NM_RECORD_INT 0x100
#define NM_MISC_INT_1 0x4000
#define NM_MISC_INT_2 0x1

/* The AV's "mixer ready" status bit and location. */
#define NM_MIXER_STATUS_OFFSET 0xa04
#define NM_MIXER_READY_MASK 0x0800

/*
 * For the ZX.  It uses the same interrupt register, but it holds 32
 * bits instead of 16.
 */
#define NM2_PLAYBACK_INT 0x10000
#define NM2_RECORD_INT 0x80000
#define NM2_MISC_INT_1 0x8
#define NM2_MISC_INT_2 0x2

/* The ZX's "mixer ready" status bit and location. */
#define NM2_MIXER_STATUS_OFFSET 0xa06
#define NM2_MIXER_READY_MASK 0x0800

/* The playback registers start from here. */
#define NM_PLAYBACK_REG_OFFSET 0x0
/* The record registers start from here. */
#define NM_RECORD_REG_OFFSET 0x200

/* The rate register is located 2 bytes from the start of the register area. */
#define NM_RATE_REG_OFFSET 2

/* Mono/stereo flag, number of bits on playback, and rate mask. */
#define NM_RATE_STEREO 1
#define NM_RATE_BITS_16 2
#define NM_RATE_MASK 0xf0

/* Playback enable register. */
#define NM_PLAYBACK_ENABLE_REG (NM_PLAYBACK_REG_OFFSET + 0x1)
#define NM_PLAYBACK_ENABLE_FLAG 1
#define NM_PLAYBACK_ONESHOT 2
#define NM_PLAYBACK_FREERUN 4

/* Mutes the audio output. */
#define NM_AUDIO_MUTE_REG (NM_PLAYBACK_REG_OFFSET + 0x18)
#define NM_AUDIO_MUTE_LEFT 0x8000
#define NM_AUDIO_MUTE_RIGHT 0x0080
#define NM_AUDIO_MUTE_BOTH 0x8080

/* Recording enable register. */
#define NM_RECORD_ENABLE_REG (NM_RECORD_REG_OFFSET + 0)
#define NM_RECORD_ENABLE_FLAG 1
#define NM_RECORD_FREERUN 2

#define NM_RBUFFER_START (NM_RECORD_REG_OFFSET + 0x4)
#define NM_RBUFFER_END   (NM_RECORD_REG_OFFSET + 0x10)
#define NM_RBUFFER_WMARK (NM_RECORD_REG_OFFSET + 0xc)
#define NM_RBUFFER_CURRP (NM_RECORD_REG_OFFSET + 0x8)

#define NM_PBUFFER_START (NM_PLAYBACK_REG_OFFSET + 0x4)
#define NM_PBUFFER_END   (NM_PLAYBACK_REG_OFFSET + 0x14)
#define NM_PBUFFER_WMARK (NM_PLAYBACK_REG_OFFSET + 0xc)
#define NM_PBUFFER_CURRP (NM_PLAYBACK_REG_OFFSET + 0x8)


#endif
