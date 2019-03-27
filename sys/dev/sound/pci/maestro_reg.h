/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2000 Taku YAMAMOTO <taku@cent.saitama-u.ac.jp>
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
 *	maestro_reg.h,v 1.13 2001/11/11 18:29:46 taku Exp
 * $FreeBSD$
 */

#ifndef	MAESTRO_REG_H_INCLUDED
#define	MAESTRO_REG_H_INCLUDED

/* -----------------------------
 * PCI config registers
 */

/* Legacy emulation */
#define CONF_LEGACY	0x40

#define LEGACY_DISABLED	0x8000

/* Chip configurations */
#define CONF_MAESTRO	0x50
#define MAESTRO_PMC		0x08000000
#define MAESTRO_SPDIF		0x01000000
#define MAESTRO_HWVOL		0x00800000
#define MAESTRO_CHIBUS		0x00100000
#define MAESTRO_POSTEDWRITE	0x00000080
#define MAESTRO_DMA_PCITIMING	0x00000040
#define MAESTRO_SWAP_LR		0x00000020

/* ACPI configurations */
#define CONF_ACPI_STOPCLOCK	0x54
#define ACPI_PART_2ndC_CLOCK	15
#define ACPI_PART_CODEC_CLOCK	14
#define ACPI_PART_978		13 /* Docking station or something */
#define ACPI_PART_SPDIF		12
#define ACPI_PART_GLUE		11 /* What? */
#define ACPI_PART_DAA		10
#define ACPI_PART_PCI_IF	9
#define ACPI_PART_HW_VOL	8
#define ACPI_PART_GPIO		7
#define ACPI_PART_ASSP		6
#define ACPI_PART_SB		5
#define ACPI_PART_FM		4
#define ACPI_PART_RINGBUS	3
#define ACPI_PART_MIDI		2
#define ACPI_PART_GAME_PORT	1
#define ACPI_PART_WP		0

/* Power management */
#define	CONF_PM_PTR	0x34	/* BYTE R */
#define	PM_CID		0	/* BYTE R */
#define	PPMI_CID	1
#define	PM_CTRL		4	/* BYTE RW */
#define	PPMI_D0		0	/* Full power */
#define	PPMI_D1		1	/* Medium power */
#define	PPMI_D2		2	/* Low power */
#define	PPMI_D3		3	/* Turned off */


/* -----------------------------
 * I/O ports
 */

/* Direct Sound Processor (aka WP) */
#define PORT_DSP_DATA	0x00	/* WORD RW */
#define PORT_DSP_INDEX	0x02	/* WORD RW */
#define PORT_INT_STAT	0x04	/* WORD RW */
#define PORT_SAMPLE_CNT	0x06	/* WORD RO */

/* WaveCache */
#define PORT_WAVCACHE_INDEX	0x10	/* WORD RW */
#define PORT_WAVCACHE_DATA	0x12	/* WORD RW */
#define WAVCACHE_PCMBAR		0x1fc
#define WAVCACHE_WTBAR		0x1f0
#define WAVCACHE_BASEADDR_SHIFT	12

#define WAVCACHE_CHCTL_ADDRTAG_MASK	0xfff8
#define WAVCACHE_CHCTL_U8		0x0004
#define WAVCACHE_CHCTL_STEREO		0x0002
#define WAVCACHE_CHCTL_DECREMENTAL	0x0001

#define PORT_WAVCACHE_CTRL	0x14	/* WORD RW */
#define WAVCACHE_EXTRA_CH_ENABLED	0x0200
#define WAVCACHE_ENABLED		0x0100
#define WAVCACHE_CH_60_ENABLED		0x0080
#define WAVCACHE_WTSIZE_MASK	0x0060
#define WAVCACHE_WTSIZE_1MB	0x0000
#define WAVCACHE_WTSIZE_2MB	0x0020
#define WAVCACHE_WTSIZE_4MB	0x0040
#define WAVCACHE_WTSIZE_8MB	0x0060
#define WAVCACHE_SGC_MASK		0x000c
#define WAVCACHE_SGC_DISABLED		0x0000
#define WAVCACHE_SGC_40_47		0x0004
#define WAVCACHE_SGC_32_47		0x0008
#define WAVCACHE_TESTMODE		0x0001

/* Host Interruption */
#define PORT_HOSTINT_CTRL	0x18	/* WORD RW */
#define HOSTINT_CTRL_SOFT_RESET		0x8000
#define HOSTINT_CTRL_DSOUND_RESET	0x4000
#define HOSTINT_CTRL_HW_VOL_TO_PME	0x0400
#define HOSTINT_CTRL_CLKRUN_ENABLED	0x0100
#define HOSTINT_CTRL_HWVOL_ENABLED	0x0040
#define HOSTINT_CTRL_ASSP_INT_ENABLED	0x0010
#define HOSTINT_CTRL_ISDN_INT_ENABLED	0x0008
#define HOSTINT_CTRL_DSOUND_INT_ENABLED	0x0004
#define HOSTINT_CTRL_MPU401_INT_ENABLED	0x0002
#define HOSTINT_CTRL_SB_INT_ENABLED	0x0001

#define PORT_HOSTINT_STAT	0x1a	/* BYTE RW */
#define HOSTINT_STAT_HWVOL	0x40
#define HOSTINT_STAT_ASSP	0x10
#define HOSTINT_STAT_ISDN	0x08
#define HOSTINT_STAT_DSOUND	0x04
#define HOSTINT_STAT_MPU401	0x02
#define HOSTINT_STAT_SB		0x01

/* Hardware volume */
#define PORT_HWVOL_CTRL		0x1b	/* BYTE RW */
#define HWVOL_CTRL_SPLIT_SHADOW	0x01

#define PORT_HWVOL_VOICE_SHADOW	0x1c	/* BYTE RW */
#define PORT_HWVOL_VOICE	0x1d	/* BYTE RW */
#define PORT_HWVOL_MASTER_SHADOW 0x1e	/* BYTE RW */
#define PORT_HWVOL_MASTER	0x1f	/* BYTE RW */
#define HWVOL_NOP		0x88
#define HWVOL_MUTE		0x11
#define HWVOL_UP		0xaa
#define HWVOL_DOWN		0x66

/* CODEC */
#define	PORT_CODEC_CMD	0x30	/* BYTE W */
#define CODEC_CMD_READ	0x80
#define	CODEC_CMD_WRITE	0x00
#define	CODEC_CMD_ADDR_MASK	0x7f

#define PORT_CODEC_STAT	0x30	/* BYTE R */
#define CODEC_STAT_MASK	0x01
#define CODEC_STAT_RW_DONE	0x00
#define CODEC_STAT_PROGLESS	0x01

#define PORT_CODEC_REG	0x32	/* WORD RW */

/* Ring bus control */
#define PORT_RINGBUS_CTRL	0x34	/* DWORD RW */
#define RINGBUS_CTRL_I2S_ENABLED	0x80000000
#define RINGBUS_CTRL_RINGBUS_ENABLED	0x20000000
#define RINGBUS_CTRL_ACLINK_ENABLED	0x10000000
#define RINGBUS_CTRL_AC97_SWRESET	0x08000000

#define RINGBUS_SRC_MIC		20
#define RINGBUS_SRC_I2S		16
#define RINGBUS_SRC_ADC		12
#define RINGBUS_SRC_MODEM	8
#define RINGBUS_SRC_DSOUND	4
#define RINGBUS_SRC_ASSP	0

#define RINGBUS_DEST_MONORAL	000
#define RINGBUS_DEST_STEREO	010
#define RINGBUS_DEST_NONE	0
#define RINGBUS_DEST_DAC	1
#define RINGBUS_DEST_MODEM_IN	2
#define RINGBUS_DEST_RESERVED3	3
#define RINGBUS_DEST_DSOUND_IN	4
#define RINGBUS_DEST_ASSP_IN	5

/* Ring bus control B */
#define PORT_RINGBUS_CTRL_B	0x38	/* BYTE RW */
#define RINGBUS_CTRL_SSPE		0x40
#define RINGBUS_CTRL_2ndCODEC		0x20
#define RINGBUS_CTRL_SPDIF		0x10
#define RINGBUS_CTRL_ITB_DISABLE	0x08
#define RINGBUS_CTRL_CODEC_ID_MASK	0x03
#define RINGBUS_CTRL_CODEC_ID_AC98	2

/* General Purpose I/O */
#define PORT_GPIO_DATA	0x60	/* WORD RW */
#define PORT_GPIO_MASK	0x64	/* WORD RW */
#define PORT_GPIO_DIR	0x68	/* WORD RW */

/* Application Specific Signal Processor */
#define PORT_ASSP_MEM_INDEX	0x80	/* DWORD RW */
#define PORT_ASSP_MEM_DATA	0x84	/* WORD RW */
#define PORT_ASSP_CTRL_A	0xa2	/* BYTE RW */
#define PORT_ASSP_CTRL_B	0xa4	/* BYTE RW */
#define PORT_ASSP_CTRL_C	0xa6	/* BYTE RW */
#define PORT_ASSP_HOST_WR_INDEX	0xa8	/* BYTE W */
#define PORT_ASSP_HOST_WR_DATA	0xaa	/* BYTE RW */
#define PORT_ASSP_INT_STAT	0xac	/* BYTE RW */


/* -----------------------------
 * Wave Processor Indexed Data Registers.
 */

#define WPREG_DATA_PORT		0
#define WPREG_CRAM_PTR		1
#define WPREG_CRAM_DATA		2
#define WPREG_WAVE_DATA		3
#define WPREG_WAVE_PTR_LOW	4
#define WPREG_WAVE_PTR_HIGH	5

#define WPREG_TIMER_FREQ	6
#define WP_TIMER_FREQ_PRESCALE_MASK	0x00e0	/* actual - 9 */
#define WP_TIMER_FREQ_PRESCALE_SHIFT	5
#define WP_TIMER_FREQ_DIVIDE_MASK	0x001f
#define WP_TIMER_FREQ_DIVIDE_SHIFT	0

#define WPREG_WAVE_ROMRAM	7
#define WP_WAVE_VIRTUAL_ENABLED	0x0400
#define WP_WAVE_8BITRAM_ENABLED	0x0200
#define WP_WAVE_DRAM_ENABLED	0x0100
#define WP_WAVE_RAMSPLIT_MASK	0x00ff
#define WP_WAVE_RAMSPLIT_SHIFT	0

#define WPREG_BASE		12
#define WP_PARAOUT_BASE_MASK	0xf000
#define WP_PARAOUT_BASE_SHIFT	12
#define WP_PARAIN_BASE_MASK	0x0f00
#define WP_PARAIN_BASE_SHIFT	8
#define WP_SERIAL0_BASE_MASK	0x00f0
#define WP_SERIAL0_BASE_SHIFT	4
#define WP_SERIAL1_BASE_MASK	0x000f
#define WP_SERIAL1_BASE_SHIFT	0

#define WPREG_TIMER_ENABLE	17
#define WPREG_TIMER_START	23


/* -----------------------------
 * Audio Processing Unit.
 */
#define APUREG_APUTYPE	0
#define APU_DMA_ENABLED	0x4000
#define APU_INT_ON_LOOP	0x2000
#define APU_ENDCURVE	0x1000
#define APU_APUTYPE_MASK	0x00f0
#define APU_FILTERTYPE_MASK	0x000c
#define APU_FILTERQ_MASK	0x0003

/* APU types */
#define APU_APUTYPE_SHIFT	4

#define APUTYPE_INACTIVE	0
#define APUTYPE_16BITLINEAR	1
#define APUTYPE_16BITSTEREO	2
#define APUTYPE_8BITLINEAR	3
#define APUTYPE_8BITSTEREO	4
#define APUTYPE_8BITDIFF	5
#define APUTYPE_DIGITALDELAY	6
#define APUTYPE_DUALTAP_READER	7
#define APUTYPE_CORRELATOR	8
#define APUTYPE_INPUTMIXER	9
#define APUTYPE_WAVETABLE	10
#define APUTYPE_RATECONV	11
#define APUTYPE_16BITPINGPONG	12
/* APU type 13 through 15 are reserved. */

/* Filter types */
#define APU_FILTERTYPE_SHIFT	2

#define FILTERTYPE_2POLE_LOPASS		0
#define FILTERTYPE_2POLE_BANDPASS	1
#define FILTERTYPE_2POLE_HIPASS		2
#define FILTERTYPE_1POLE_LOPASS		3
#define FILTERTYPE_1POLE_HIPASS		4
#define FILTERTYPE_PASSTHROUGH		5

/* Filter Q */
#define APU_FILTERQ_SHIFT	0

#define FILTERQ_LESSQ	0
#define FILTERQ_MOREQ	3

/* APU register 2 */
#define APUREG_FREQ_LOBYTE	2
#define APU_FREQ_LOBYTE_MASK	0xff00
#define APU_plus6dB		0x0010

/* APU register 3 */
#define APUREG_FREQ_HIWORD	3
#define APU_FREQ_HIWORD_MASK	0x0fff

/* Frequency */
#define APU_FREQ_LOBYTE_SHIFT	8
#define APU_FREQ_HIWORD_SHIFT	0
#define FREQ_Hz2DIV(freq)	(((u_int64_t)(freq) << 16) / 48000)

/* APU register 4 */
#define APUREG_WAVESPACE	4
#define APU_64KPAGE_MASK	0xff00

/* 64KW (==128KB) Page */
#define APU_64KPAGE_SHIFT	8

/* Wave Processor Wavespace Address */
#define WPWA_MAX		((1 << 22) - 1)
#define WPWA_STEREO		(1 << 23)
#define WPWA_USE_SYSMEM		(1 << 22)

#define WPWA_WTBAR_SHIFT(wtsz)	WPWA_WTBAR_SHIFT_##wtsz
#define WPWA_WTBAR_SHIFT_1	15
#define WPWA_WTBAR_SHIFT_2	16
#define WPWA_WTBAR_SHIFT_4	17
#define WPWA_WTBAR_SHIFT_8	18

#define WPWA_PCMBAR_SHIFT	20

/* APU register 5 - 7 */
#define APUREG_CURPTR	5
#define APUREG_ENDPTR	6
#define APUREG_LOOPLEN	7

/* APU register 8 */
#define APUREG_EFFECT_GAIN	8

/* Effect gain? */
#define APUREG_EFFECT_GAIN_MASK	0x00ff

/* APU register 9 */
#define APUREG_AMPLITUDE	9
#define APU_AMPLITUDE_NOW_MASK	0xff00
#define APU_AMPLITUDE_DEST_MASK	0x00ff

/* Amplitude now? */
#define APU_AMPLITUDE_NOW_SHIFT	8

/* APU register 10 */
#define APUREG_POSITION	10
#define APU_RADIUS_MASK	0x00c0
#define APU_PAN_MASK	0x003f

/* Radius control. */
#define APU_RADIUS_SHIFT	6
#define RADIUS_CENTERCIRCLE	0
#define RADIUS_MIDDLE		1
#define RADIUS_OUTSIDE		2

/* Polar pan. */
#define APU_PAN_SHIFT	0
#define PAN_RIGHT	0x00
#define PAN_FRONT	0x08
#define PAN_LEFT	0x10

/* Source routing. */
#define APUREG_ROUTING	11
#define APU_INVERT_POLARITY_B	0x8000
#define APU_DATASRC_B_MASK	0x7f00
#define APU_INVERT_POLARITY_A	0x0080
#define APU_DATASRC_A_MASK	0x007f

#define APU_DATASRC_A_SHIFT	0
#define APU_DATASRC_B_SHIFT	8


/* -----------------------------
 * Limits.
 */
#define WPWA_MAXADDR	((1 << 23) - 1)
#define MAESTRO_MAXADDR	((1 << 28) - 1)

#endif	/* MAESTRO_REG_H_INCLUDED */
