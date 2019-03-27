/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Orion Hodson <O.Hodson@cs.ucl.ac.uk>
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

#define ALS_PCI_ID0 		0x40004005
#define ALS_PCI_POWERREG	0xe0

#define ALS_CONFIG_SPACE_BYTES	128

#define ALS_GCR_DATA		0x08
#define ALS_GCR_INDEX		0x0c
#	define ALS_GCR_MISC		0x8c
#	define ALS_GCR_TEST		0x90
#	define ALS_GCR_DMA0_START	0x91
#	define ALS_GCR_DMA0_MODE	0x92
#	define ALS_GCR_DMA1_START	0x93
#	define ALS_GCR_DMA1_MODE	0x94
#	define ALS_GCR_DMA3_START	0x95
#	define ALS_GCR_DMA3_MODE	0x96
#	define ALS_GCR_DMA_EMULATION	0x99
#	define ALS_GCR_FIFO0_CURRENT	0xa0
#	define ALS_GCR_FIFO0_STATUS	0xa1
#	define ALS_GCR_FIFO1_START	0xa2
#	define ALS_GCR_FIFO1_COUNT	0xa3
#	define ALS_GCR_FIFO1_CURRENT	0xa4
#	define ALS_GCR_FIFO1_STATUS	0xa5
#	define ALS_GCR_POWER		0xa6
#	define ALS_GCR_PIC_ACCESS	0xa7

#define ALS_SB_MPU_IRQ		0x0e

#define ALS_MIXER_DATA		0x15
#define ALS_MIXER_INDEX		0x14
#	define ALS_SB16_RESET		0x00
#	define ALS_SB16_DMA_SETUP	0x81
#	define ALS_CONTROL		0xc0
#	define ALS_SB16_CONFIG		ALS_CONTROL + 0x00
#	define ALS_MISC_CONTROL		ALS_CONTROL + 0x02
#	define ALS_FIFO1_LENGTH_LO	ALS_CONTROL + 0x1c
#	define ALS_FIFO1_LENGTH_HI	ALS_CONTROL + 0x1d
#	define ALS_FIFO1_CONTROL	ALS_CONTROL + 0x1e
#		define ALS_FIFO1_STOP		0x00
#		define ALS_FIFO1_RUN		0x80
#		define ALS_FIFO1_PAUSE		0x40
#		define ALS_FIFO1_STEREO		0x20
#		define ALS_FIFO1_SIGNED		0x10
#		define ALS_FIFO1_8BIT		0x04

#define ALS_ESP_RST		0x16
#define ALS_CR1E_ACK_PORT	0x16

#define ALS_ESP_RD_DATA		0x1a
#define ALS_ESP_WR_DATA		0x1c
#define ALS_ESP_WR_STATUS	0x1c
#define ALS_ESP_RD_STATUS8	0x1e
#define ALS_ESP_RD_STATUS16	0x1f
#	define ALS_ESP_SAMPLE_RATE	0x41

#define ALS_MIDI_DATA		0x30
#define ALS_MIDI_STATUS		0x31

/* Interrupt masks */
#define	ALS_IRQ_STATUS8		0x01
#define	ALS_IRQ_STATUS16	0x02
#define ALS_IRQ_MPUIN		0x04
#define ALS_IRQ_CR1E		0x20

/* Sample Rate Locks */
#define ALS_RATE_LOCK_PLAYBACK	0x01
#define ALS_RATE_LOCK_CAPTURE	0x02
#define ALS_RATE_LOCK		0x03
