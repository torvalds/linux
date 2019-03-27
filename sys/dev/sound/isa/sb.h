/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997,1998 Luigi Rizzo
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef SB_H
#define SB_H

struct sbc_softc;
void sbc_lock(struct sbc_softc *);
void sbc_lockassert(struct sbc_softc *);
void sbc_unlock(struct sbc_softc *);

/*
 * sound blaster registers
 */

#define SBDSP_RST	0x6
#define DSP_READ	0xA
#define DSP_WRITE	0xC
#define SBDSP_CMD	0xC
#define SBDSP_STATUS	0xC
#define DSP_DATA_AVAIL	0xE
#define DSP_DATA_AVL16	0xF

#define SB_MIX_ADDR	0x4
#define SB_MIX_DATA	0x5
#if 0
#define OPL3_LEFT	(io_base + 0x0)
#define OPL3_RIGHT	(io_base + 0x2)
#define OPL3_BOTH	(io_base + 0x8)
#endif

/*
 * DSP Commands. There are many, and in many cases they are used explicitly
 */

/* these are not used except for programmed I/O (not in this driver) */
#define	DSP_DAC8		0x10	/* direct DAC output */
#define	DSP_ADC8		0x20	/* direct ADC input */

/* these should be used in the SB 1.0 */
#define	DSP_CMD_DAC8		0x14	/* single cycle 8-bit dma out */
#define	DSP_CMD_ADC8		0x24	/* single cycle 8-bit dma in */

/* these should be used in the SB 2.0 and 2.01 */
#define	DSP_CMD_DAC8_AUTO	0x1c	/* auto 8-bit dma out */
#define	DSP_CMD_ADC8_AUTO	0x2c	/* auto 8-bit dma out */

#define	DSP_CMD_HSSIZE		0x48	/* high speed dma count */
#define	DSP_CMD_HSDAC_AUTO	0x90	/* high speed dac, auto */
#define DSP_CMD_HSADC_AUTO      0x98    /* high speed adc, auto */

/* SBPro commands. Some cards (JAZZ, SMW) also support 16 bits */

	/* prepare for dma input */
#define	DSP_CMD_DMAMODE(stereo, bit16) (0xA0 | (stereo ? 8:0) | (bit16 ? 4:0))

#define	DSP_CMD_DAC2		0x16	/* 2-bit adpcm dma out (cont) */
#define	DSP_CMD_DAC2S		0x17	/* 2-bit adpcm dma out (start) */

#define	DSP_CMD_DAC2S_AUTO	0x1f	/* auto 2-bit adpcm dma out (start) */


/* SB16 commands */
#define	DSP_CMD_O16		0xb0
#define	DSP_CMD_I16		0xb8
#define	DSP_CMD_O8		0xc0
#define	DSP_CMD_I8		0xc8

#define	DSP_MODE_U8MONO		0x00
#define	DSP_MODE_U8STEREO	0x20
#define	DSP_MODE_S16MONO	0x10
#define	DSP_MODE_S16STEREO	0x30

#define DSP_CMD_SPKON		0xD1
#define DSP_CMD_SPKOFF		0xD3
#define DSP_CMD_SPKR(on)	(0xD1 | (on ? 0:2))

#define	DSP_CMD_DMAPAUSE_8	0xD0
#define	DSP_CMD_DMAPAUSE_16	0xD5
#define	DSP_CMD_DMAEXIT_8	0xDA
#define	DSP_CMD_DMAEXIT_16	0xD9
#define	DSP_CMD_TCONST		0x40	/* set time constant */
#define	DSP_CMD_HSDAC		0x91	/* high speed dac */
#define DSP_CMD_HSADC           0x99    /* high speed adc */

#define	DSP_CMD_GETVER		0xE1
#define	DSP_CMD_GETID		0xE7	/* return id bytes */


#define	DSP_CMD_OUT16		0x41	/* send parms for dma out on sb16 */
#define	DSP_CMD_IN16		0x42	/* send parms for dma in on sb16 */
#if 0 /*** unknown ***/
#define	DSP_CMD_FA		0xFA	/* get version from prosonic*/
#define	DSP_CMD_FB		0xFB	/* set irq/dma for prosonic*/
#endif

/*
 * in fact, for the SB16, dma commands are as follows:
 *
 *  cmd, mode, len_low, len_high.
 *
 * cmd is a combination of DSP_DMA16 or DSP_DMA8 and
 */

#define	DSP_DMA16		0xb0
#define	DSP_DMA8		0xc0
#   define DSP_F16_DAC		0x00
#   define DSP_F16_ADC		0x08
#   define DSP_F16_AUTO		0x04
#   define DSP_F16_FIFO_ON	0x02

/*
 * mode is a combination of the following:
 */
#define DSP_F16_STEREO	0x20
#define DSP_F16_SIGNED	0x10

#define IMODE_NONE		0
#define IMODE_OUTPUT		PCM_ENABLE_OUTPUT
#define IMODE_INPUT		PCM_ENABLE_INPUT
#define IMODE_INIT		3
#define IMODE_MIDI		4

#define NORMAL_MIDI	0
#define UART_MIDI	1

/*
 * values used for bd_flags in SoundBlaster driver
 */
#define	BD_F_HISPEED	0x0001	/* doing high speed ... */

#if 0
#define	BD_F_JAZZ16	0x0002	/* jazz16 detected */
#define	BD_F_JAZZ16_2	0x0004	/* jazz16 type 2 */
#endif

#define	BD_F_DUP_MIDI	0x0008	/* duplex midi */

#define	BD_F_MIX_MASK	0x0070	/* up to 8 mixers (I know of 3) */
#define	BD_F_MIX_CT1335	0x0010	/* CT1335		*/
#define	BD_F_MIX_CT1345	0x0020	/* CT1345		*/
#define	BD_F_MIX_CT1745	0x0030	/* CT1745		*/

#define	BD_F_SB16	0x0100	/* this is a SB16 */
#define	BD_F_SB16X	0x0200	/* this is a vibra16X or clone */
#if 0
#define	BD_F_MIDIBUSY	0x0400	/* midi busy */
#endif
#define	BD_F_ESS	0x0800	/* this is an ESS chip */
#define BD_F_DMARUN	0x2000
#define BD_F_DMARUN2	0x4000

/*
 * Mixer registers of SB Pro
 */
#define VOC_VOL		0x04
#define MIC_VOL		0x0A
#define MIC_MIX		0x0A
#define RECORD_SRC	0x0C
#define IN_FILTER	0x0C
#define OUT_FILTER	0x0E
#define MASTER_VOL	0x22
#define FM_VOL		0x26
#define CD_VOL		0x28
#define LINE_VOL	0x2E
#define IRQ_NR		0x80
#define DMA_NR		0x81
#define IRQ_STAT	0x82

/*
 * Additional registers on the SG NX Pro
 */
#define COVOX_VOL	0x42
#define TREBLE_LVL	0x44
#define BASS_LVL	0x46

#define FREQ_HI         (1 << 3)/* Use High-frequency ANFI filters */
#define FREQ_LOW        0	/* Use Low-frequency ANFI filters */
#define FILT_ON         0	/* Yes, 0 to turn it on, 1 for off */
#define FILT_OFF        (1 << 5)

#define MONO_DAC	0x00
#define STEREO_DAC	0x02

/*
 * Mixer registers of SB16
 */
#define SB16_IMASK_L	0x3d
#define SB16_IMASK_R	0x3e
#define SB16_OMASK	0x3c
#endif
