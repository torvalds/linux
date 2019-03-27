/*-
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#define	CODEC_RGADW	0x00	/* Address, data in and write command */
#define	 RGADW_ICRST	(1 << 31) /* Reset internal CODEC */
#define	 RGADW_RGWR	(1 << 16) /* Issue a write command to CODEC */
#define	 RGADW_RGADDR_S	8	/* CODEC register's address. */
#define	 RGADW_RGADDR_M	(0x7f << RGADW_RGADDR_S)
#define	 RGADW_RGDIN_S	0	/* CODEC register data to write */
#define	 RGADW_RGDIN_M	(0xff << RGADW_RGDIN_S)
#define	CODEC_RGDATA	0x04	/* The data read out */

#define	SR		0x00	/* Status Register */
#define	SR2		0x01	/* Status Register 2 */
#define	MR		0x07	/* Mode status register */
#define	AICR_DAC	0x08	/* DAC Audio Interface Control Register */
#define	 DAC_ADWL_S	6	/* Audio Data Word Length for DAC path. */
#define	 DAC_ADWL_M	(0x3 << DAC_ADWL_S)
#define	 DAC_ADWL_16	(0 << DAC_ADWL_S)
#define	 AICR_DAC_SB	(1 << 4)	/* DAC audio interface in power-down mode */
#define	 AUDIOIF_S	0
#define	 AUDIOIF_M	(0x3 << AUDIOIF_S)
#define	 AUDIOIF_I2S	0x3	/* I2S interface */
#define	 AUDIOIF_DSP	0x2	/* DSP interface */
#define	 AUDIOIF_LJ	0x1	/* Left-justified interface */
#define	 AUDIOIF_P	0x0	/* Parallel interface */
#define	AICR_ADC	0x09	/* ADC Audio Interface Control Register */
#define	CR_LO		0x0B	/* Differential line-out Control Register */
#define	CR_HP		0x0D	/* HeadPhone Control Register */
#define	 HP_MUTE	(1 << 7)	/* no signal on headphone outputs */
#define	 HP_SB		(1 << 4)	/* power-down */
#define	CR_DMIC		0x10	/* Digital Microphone register */
#define	CR_MIC1		0x11	/* Microphone1 Control register */
#define	CR_MIC2		0x12	/* Microphone2 Control register */
#define	CR_LI1		0x13	/* Control Register for line1 inputs */
#define	CR_LI2		0x14	/* Control Register for line2 inputs */
#define	CR_DAC		0x17	/* DAC Control Register */
#define	 DAC_MUTE	(1 << 7)	/* puts the DAC in soft mute mode */
#define	 DAC_SB		(1 << 4)	/* power-down */
#define	CR_ADC		0x18	/* ADC Control Register */
#define	CR_MIX		0x19	/* Digital Mixer Control Register */
#define	DR_MIX		0x1A	/* Digital Mixer Data Register */
#define	CR_VIC		0x1B	/* Control Register for the ViC */
#define	 VIC_SB_SLEEP	(1 << 1)	/* sleep mode */
#define	 VIC_SB		(1 << 0)	/* complete power-down */
#define	CR_CK		0x1C	/* Clock Control Register */
#define	FCR_DAC		0x1D	/* DAC Frequency Control Register */
#define	 FCR_DAC_48	8	/* 48 kHz. */
#define	 FCR_DAC_96	10	/* 96 kHz. */
#define	FCR_ADC		0x20	/* ADC Frequency Control Register */
#define	CR_TIMER_MSB	0x21	/* MSB of programmable counter */
#define	CR_TIMER_LSB	0x22	/* LSB of programmable counter */
#define	ICR		0x23	/* Interrupt Control Register */
#define	IMR		0x24	/* Interrupt Mask Register */
#define	IFR		0x25	/* Interrupt Flag Register */
#define	IMR2		0x26	/* Interrupt Mask Register 2 */
#define	IFR2		0x27	/* Interrupt Flag Register 2 */
#define	GCR_HPL		0x28	/* Left channel headphone Control Gain Register */
#define	GCR_HPR		0x29	/* Right channel headphone Control Gain Register */
#define	GCR_LIBYL	0x2A	/* Left channel bypass line Control Gain Register */
#define	GCR_LIBYR	0x2B	/* Right channel bypass line Control Gain Register */
#define	GCR_DACL	0x2C	/* Left channel DAC Gain Control Register */
#define	GCR_DACR	0x2D	/* Right channel DAC Gain Control Register */
#define	GCR_MIC1	0x2E	/* Microphone 1 Gain Control Register */
#define	GCR_MIC2	0x2F	/* Microphone 2 Gain Control Register */
#define	GCR_ADCL	0x30	/* Left ADC Gain Control Register */
#define	GCR_ADCR	0x31	/* Right ADC Gain Control Register */
#define	GCR_MIXDACL	0x34	/* DAC Digital Mixer Control Register */
#define	GCR_MIXDACR	0x35	/* DAC Digital Mixer Control Register */
#define	GCR_MIXADCL	0x36	/* ADC Digital Mixer Control Register */
#define	GCR_MIXADCR	0x37	/* ADC Digital Mixer Control Register */
#define	CR_ADC_AGC	0x3A	/* Automatic Gain Control Register */
#define	DR_ADC_AGC	0x3B	/* Automatic Gain Control Data Register */
