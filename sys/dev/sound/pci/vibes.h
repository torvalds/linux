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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* ------------------------------------------------------------------------- */
/* PCI Configuration Register Offsets */

#define SV_PCI_COMPAT	0x10
#define SV_PCI_ENHANCED 0x14
#define SV_PCI_FMSYNTH	0x18
#define SV_PCI_MIDI	0x1c
#define SV_PCI_GAMES	0x20
#define SV_PCI_DMAA	0x40
#define SV_PCI_DMAC	0x48

#define SV_PCI_DMAA_SIZE	0x10
#define SV_PCI_DMAA_ALIGN	0x10
#define SV_PCI_DMAC_SIZE	0x10
#define SV_PCI_DMAC_ALIGN	0x10

#define SV_PCI_ENHANCED_SIZE	0x08

#define SV_PCI_DMA_ENABLE	0x00000001
#define SV_PCI_DMA_EXTENDED	0x00000008

/* ------------------------------------------------------------------------- */
/* DMA Configuration Registers */

#define SV_DMA_ADDR	0x00
#define SV_DMA_COUNT	0x04

#define	SV_DMA_MODE	0x0B
#define 	SV_DMA_MODE_AUTO	0x10
#define		SV_DMA_MODE_RD		0x04
#define		SV_DMA_MODE_WR		0x08

/* ------------------------------------------------------------------------- */
/* Enhanced Mode Configuration Registers */

#define SV_CM_CONTROL	0x00
#define		SV_CM_CONTROL_ENHANCED	0x01
#define		SV_CM_CONTROL_TEST	0x02
#define		SV_CM_CONTROL_REVERB	0x04
#define		SV_CM_CONTROL_PWS	0x08
#define		SV_CM_CONTROL_INTA	0x20
#define		SV_CM_CONTROL_RESET	0x80

#define SV_CM_IMR	0x01
#define		SV_CM_IMR_AMSK		0x01
#define		SV_CM_IMR_CMSK		0x04
#define		SV_CM_IMR_SMSK		0x08
#define		SV_CM_IMR_UDM		0x40
#define		SV_CM_IMR_MIDM		0x80

#define SV_CM_STATUS	0x02
#define		SV_CM_STATUS_AINT	0x01
#define		SV_CM_STATUS_CINT	0x04
#define		SV_CM_STATUS_SINT	0x08
#define		SV_CM_STATUS_UDI	0x40
#define		SV_CM_STATUS_MI		0x80

#define SV_CM_INDEX	0x04
#define		SV_CM_INDEX_MASK	0x3f
#define		SV_CM_INDEX_MCE		0x40
#define		SV_CM_INDEX_TRD		0x80

#define SV_CM_DATA	0x05

/* ------------------------------------------------------------------------- */
/* Indexed Codec/Mixer Registers (left channels were applicable) */ 

#define SV_REG_ADC_INPUT	0x00
#define 	SV_INPUT_GAIN_MASK	0x0f
#define		SV_INPUT_MICGAIN	0x10
#define		SV_INPUT_CD		0x20
#define 	SV_INPUT_DAC		0x40
#define 	SV_INPUT_AUX2		0x60
#define 	SV_INPUT_LINE		0x80
#define 	SV_INPUT_AUX1		0xa0
#define 	SV_INPUT_MIC		0xc0
#define		SV_INPUT_MIXOUT		0xe0

#define	SV_REG_AUX1		0x02
#define	SV_REG_CD		0x04
#define	SV_REG_LINE		0x06
#define	SV_REG_MIC		0x08
#define	SV_REG_SYNTH		0x0a
#define	SV_REG_AUX2		0x0c
#define	SV_REG_MIX		0x0e
#define	SV_REG_PCM		0x10
#define		SV_DEFAULT_MAX		0x1f
#define		SV_ADC_MAX		0x0f
#define		SV_MIC_MAX		0x0f
#define 	SV_PCM_MAX		0x3f
#define 	SV_MUTE			0x80

#define SV_REG_FORMAT		0x12
#define		SV_AFMT_MONO	0x00
#define		SV_AFMT_STEREO	0x01
#define		SV_AFMT_S16	0x02
#define		SV_AFMT_U8	0x00
#define		SV_AFMT_DMAA(x)		(x)
#define		SV_AFMT_DMAA_MSK	0x03
#define		SV_AFMT_DMAC(x)		((x) << 4)
#define		SV_AFMT_DMAC_MSK	0x30

#define SV_REG_ENABLE		0x13
#define		SV_PLAY_ENABLE		0x01
#define		SV_RECORD_ENABLE	0x02
#define		SV_PLAYBACK_PAUSE	0x04

#define SV_REG_REVISION	0x15

#define SV_REG_LOOPBACK	0x16
#define		SV_LOOPBACK_ENABLE	0x01
#define		SV_LOOPBACK_MAX		0x3f
#define		SV_LOOPBACK_LEVEL(x)	((x) << 2)

#define	SV_REG_DMAA_COUNT_HI	0x18
#define	SV_REG_DMAA_COUNT_LO	0x19
#define	SV_REG_DMAC_COUNT_HI	0x1c
#define	SV_REG_DMAC_COUNT_LO	0x1d

#define SV_REG_PCM_SAMPLING_LO	0x1e
#define SV_REG_PCM_SAMPLING_HI	0x1f

#define SV_REG_SYN_SAMPLING_LO 	0x20
#define SV_REG_SYN_SAMPLING_HI 	0x21

#define SV_REG_CLOCK_SOURCE	0x22
#define		SV_CLOCK_ALTERNATE	0x10	
#define SV_REG_ALT_RATE	0x23

#define SV_REG_ADC_PLLM	0x24
#define SV_REG_ADC_PLLN	0x25
#define 	SV_ADC_PLLN(x)		((x) & 0x1f)
#define		SV_ADC_PLLR(x)		((x) << 5)

#define SV_REG_SYNTH_PLLM	0x26
#define SV_REG_SYNTH_PLLN	0x27
#define 	SV_SYNTH_PLLN(x)	((x) & 0x1f)
#define		SV_SYNTH_PLLR(x)	((x) << 5)

#define SV_REG_SRS_SPACE	0x2c
#define		SV_SRS_SPACE_100	0x00
#define		SV_SRS_SPACE_75		0x01
#define		SV_SRS_SPACE_50		0x02
#define		SV_SRS_SPACE_25		0x03
#define		SV_SRS_SPACE_0		0x04
#define		SV_SRS_DISABLED		0x80

#define	SV_REG_SRS_CENTER	0x2d
#define		SV_SRS_CENTER_100	0x00
#define		SV_SRS_CENTER_75	0x01
#define		SV_SRS_CENTER_50	0x02
#define		SV_SRS_CENTER_25	0x03
#define		SV_SRS_CENTER_0		0x04

#define SV_REG_ANALOG_PWR	0x30
#define		SV_ANALOG_OFF_DAC	0x01
#define		SV_ANALOG_OFF_ADC	0x08
#define		SV_ANALOG_OFF_MIX	0x10
#define		SV_ANALOG_OFF_SRS	0x20
#define		SV_ANALOG_OFF_SPLL	0x40
#define		SV_ANALOG_OFF_APLL	0x80
#define		SV_ANALOG_OFF		0xf9

#define	SV_REG_DIGITAL_PWR	0x31
#define		SV_DIGITAL_OFF_SYN	0x01
#define		SV_DIGITAL_OFF_MU	0x02
#define		SV_DIGITAL_OFF_GP	0x04	
#define		SV_DIGITAL_OFF_BI	0x08
#define		SV_DIGITAL_OFF		0x0f

/* ------------------------------------------------------------------------- */
/* ADC PLL constants */ 

#define		SV_F_SCALE		512
#define		SV_F_REF		24576000
