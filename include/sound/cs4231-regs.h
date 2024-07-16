/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_CS4231_REGS_H
#define __SOUND_CS4231_REGS_H

/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Definitions for CS4231 & InterWave chips & compatible chips registers
 */

/* IO ports */

#define CS4231P(x)		(c_d_c_CS4231##x)

#define c_d_c_CS4231REGSEL	0
#define c_d_c_CS4231REG		1
#define c_d_c_CS4231STATUS	2
#define c_d_c_CS4231PIO		3

/* codec registers */

#define CS4231_LEFT_INPUT	0x00	/* left input control */
#define CS4231_RIGHT_INPUT	0x01	/* right input control */
#define CS4231_AUX1_LEFT_INPUT	0x02	/* left AUX1 input control */
#define CS4231_AUX1_RIGHT_INPUT	0x03	/* right AUX1 input control */
#define CS4231_AUX2_LEFT_INPUT	0x04	/* left AUX2 input control */
#define CS4231_AUX2_RIGHT_INPUT	0x05	/* right AUX2 input control */
#define CS4231_LEFT_OUTPUT	0x06	/* left output control register */
#define CS4231_RIGHT_OUTPUT	0x07	/* right output control register */
#define CS4231_PLAYBK_FORMAT	0x08	/* clock and data format - playback - bits 7-0 MCE */
#define CS4231_IFACE_CTRL	0x09	/* interface control - bits 7-2 MCE */
#define CS4231_PIN_CTRL		0x0a	/* pin control */
#define CS4231_TEST_INIT	0x0b	/* test and initialization */
#define CS4231_MISC_INFO	0x0c	/* miscellaneous information */
#define CS4231_LOOPBACK		0x0d	/* loopback control */
#define CS4231_PLY_UPR_CNT	0x0e	/* playback upper base count */
#define CS4231_PLY_LWR_CNT	0x0f	/* playback lower base count */
#define CS4231_ALT_FEATURE_1	0x10	/* alternate #1 feature enable */
#define AD1845_AF1_MIC_LEFT	0x10	/* alternate #1 feature + MIC left */
#define CS4231_ALT_FEATURE_2	0x11	/* alternate #2 feature enable */
#define AD1845_AF2_MIC_RIGHT	0x11	/* alternate #2 feature + MIC right */
#define CS4231_LEFT_LINE_IN	0x12	/* left line input control */
#define CS4231_RIGHT_LINE_IN	0x13	/* right line input control */
#define CS4231_TIMER_LOW	0x14	/* timer low byte */
#define CS4231_TIMER_HIGH	0x15	/* timer high byte */
#define CS4231_LEFT_MIC_INPUT	0x16	/* left MIC input control register (InterWave only) */
#define AD1845_UPR_FREQ_SEL	0x16	/* upper byte of frequency select */
#define CS4231_RIGHT_MIC_INPUT	0x17	/* right MIC input control register (InterWave only) */
#define AD1845_LWR_FREQ_SEL	0x17	/* lower byte of frequency select */
#define CS4236_EXT_REG		0x17	/* extended register access */
#define CS4231_IRQ_STATUS	0x18	/* irq status register */
#define CS4231_LINE_LEFT_OUTPUT	0x19	/* left line output control register (InterWave only) */
#define CS4231_VERSION		0x19	/* CS4231(A) - version values */
#define CS4231_MONO_CTRL	0x1a	/* mono input/output control */
#define CS4231_LINE_RIGHT_OUTPUT 0x1b	/* right line output control register (InterWave only) */
#define AD1845_PWR_DOWN		0x1b	/* power down control */
#define CS4235_LEFT_MASTER	0x1b	/* left master output control */
#define CS4231_REC_FORMAT	0x1c	/* clock and data format - record - bits 7-0 MCE */
#define AD1845_CLOCK		0x1d	/* crystal clock select and total power down */
#define CS4235_RIGHT_MASTER	0x1d	/* right master output control */
#define CS4231_REC_UPR_CNT	0x1e	/* record upper count */
#define CS4231_REC_LWR_CNT	0x1f	/* record lower count */

/* definitions for codec register select port - CODECP( REGSEL ) */

#define CS4231_INIT		0x80	/* CODEC is initializing */
#define CS4231_MCE		0x40	/* mode change enable */
#define CS4231_TRD		0x20	/* transfer request disable */

/* definitions for codec status register - CODECP( STATUS ) */

#define CS4231_GLOBALIRQ	0x01	/* IRQ is active */

/* definitions for codec irq status */

#define CS4231_PLAYBACK_IRQ	0x10
#define CS4231_RECORD_IRQ	0x20
#define CS4231_TIMER_IRQ	0x40
#define CS4231_ALL_IRQS		0x70
#define CS4231_REC_UNDERRUN	0x08
#define CS4231_REC_OVERRUN	0x04
#define CS4231_PLY_OVERRUN	0x02
#define CS4231_PLY_UNDERRUN	0x01

/* definitions for CS4231_LEFT_INPUT and CS4231_RIGHT_INPUT registers */

#define CS4231_ENABLE_MIC_GAIN	0x20

#define CS4231_MIXS_LINE	0x00
#define CS4231_MIXS_AUX1	0x40
#define CS4231_MIXS_MIC		0x80
#define CS4231_MIXS_ALL		0xc0

/* definitions for clock and data format register - CS4231_PLAYBK_FORMAT */

#define CS4231_LINEAR_8		0x00	/* 8-bit unsigned data */
#define CS4231_ALAW_8		0x60	/* 8-bit A-law companded */
#define CS4231_ULAW_8		0x20	/* 8-bit U-law companded */
#define CS4231_LINEAR_16	0x40	/* 16-bit twos complement data - little endian */
#define CS4231_LINEAR_16_BIG	0xc0	/* 16-bit twos complement data - big endian */
#define CS4231_ADPCM_16		0xa0	/* 16-bit ADPCM */
#define CS4231_STEREO		0x10	/* stereo mode */
/* bits 3-1 define frequency divisor */
#define CS4231_XTAL1		0x00	/* 24.576 crystal */
#define CS4231_XTAL2		0x01	/* 16.9344 crystal */

/* definitions for interface control register - CS4231_IFACE_CTRL */

#define CS4231_RECORD_PIO	0x80	/* record PIO enable */
#define CS4231_PLAYBACK_PIO	0x40	/* playback PIO enable */
#define CS4231_CALIB_MODE	0x18	/* calibration mode bits */
#define CS4231_AUTOCALIB	0x08	/* auto calibrate */
#define CS4231_SINGLE_DMA	0x04	/* use single DMA channel */
#define CS4231_RECORD_ENABLE	0x02	/* record enable */
#define CS4231_PLAYBACK_ENABLE	0x01	/* playback enable */

/* definitions for pin control register - CS4231_PIN_CTRL */

#define CS4231_IRQ_ENABLE	0x02	/* enable IRQ */
#define CS4231_XCTL1		0x40	/* external control #1 */
#define CS4231_XCTL0		0x80	/* external control #0 */

/* definitions for test and init register - CS4231_TEST_INIT */

#define CS4231_CALIB_IN_PROGRESS 0x20	/* auto calibrate in progress */
#define CS4231_DMA_REQUEST	0x10	/* DMA request in progress */

/* definitions for misc control register - CS4231_MISC_INFO */

#define CS4231_MODE2		0x40	/* MODE 2 */
#define CS4231_IW_MODE3		0x6c	/* MODE 3 - InterWave enhanced mode */
#define CS4231_4236_MODE3	0xe0	/* MODE 3 - CS4236+ enhanced mode */

/* definitions for alternate feature 1 register - CS4231_ALT_FEATURE_1 */

#define	CS4231_DACZ		0x01	/* zero DAC when underrun */
#define CS4231_TIMER_ENABLE	0x40	/* codec timer enable */
#define CS4231_OLB		0x80	/* output level bit */

/* definitions for Extended Registers - CS4236+ */

#define CS4236_REG(i23val)	(((i23val << 2) & 0x10) | ((i23val >> 4) & 0x0f))
#define CS4236_I23VAL(reg)	((((reg)&0xf) << 4) | (((reg)&0x10) >> 2) | 0x8)

#define CS4236_LEFT_LINE	0x08	/* left LINE alternate volume */
#define CS4236_RIGHT_LINE	0x18	/* right LINE alternate volume */
#define CS4236_LEFT_MIC		0x28	/* left MIC volume */
#define CS4236_RIGHT_MIC	0x38	/* right MIC volume */
#define CS4236_LEFT_MIX_CTRL	0x48	/* synthesis and left input mixer control */
#define CS4236_RIGHT_MIX_CTRL	0x58	/* right input mixer control */
#define CS4236_LEFT_FM		0x68	/* left FM volume */
#define CS4236_RIGHT_FM		0x78	/* right FM volume */
#define CS4236_LEFT_DSP		0x88	/* left DSP serial port volume */
#define CS4236_RIGHT_DSP	0x98	/* right DSP serial port volume */
#define CS4236_RIGHT_LOOPBACK	0xa8	/* right loopback monitor volume */
#define CS4236_DAC_MUTE		0xb8	/* DAC mute and IFSE enable */
#define CS4236_ADC_RATE		0xc8	/* indenpendent ADC sample frequency */
#define CS4236_DAC_RATE		0xd8	/* indenpendent DAC sample frequency */
#define CS4236_LEFT_MASTER	0xe8	/* left master digital audio volume */
#define CS4236_RIGHT_MASTER	0xf8	/* right master digital audio volume */
#define CS4236_LEFT_WAVE	0x0c	/* left wavetable serial port volume */
#define CS4236_RIGHT_WAVE	0x1c	/* right wavetable serial port volume */
#define CS4236_VERSION		0x9c	/* chip version and ID */

/* definitions for extended registers - OPTI93X */
#define OPTi931_AUX_LEFT_INPUT	0x10
#define OPTi931_AUX_RIGHT_INPUT	0x11
#define OPTi93X_MIC_LEFT_INPUT	0x14
#define OPTi93X_MIC_RIGHT_INPUT	0x15
#define OPTi93X_OUT_LEFT	0x16
#define OPTi93X_OUT_RIGHT	0x17

#endif /* __SOUND_CS4231_REGS_H */
