#ifndef __SOUND_AC97_CODEC_H
#define __SOUND_AC97_CODEC_H

/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.1
 *  by Intel Corporation (http://developer.intel.com).
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include "pcm.h"
#include "control.h"
#include "info.h"

/*
 *  AC'97 codec registers
 */

#define AC97_RESET		0x00	/* Reset */
#define AC97_MASTER		0x02	/* Master Volume */
#define AC97_HEADPHONE		0x04	/* Headphone Volume (optional) */
#define AC97_MASTER_MONO	0x06	/* Master Volume Mono (optional) */
#define AC97_MASTER_TONE	0x08	/* Master Tone (Bass & Treble) (optional) */
#define AC97_PC_BEEP		0x0a	/* PC Beep Volume (optinal) */
#define AC97_PHONE		0x0c	/* Phone Volume (optional) */
#define AC97_MIC		0x0e	/* MIC Volume */
#define AC97_LINE		0x10	/* Line In Volume */
#define AC97_CD			0x12	/* CD Volume */
#define AC97_VIDEO		0x14	/* Video Volume (optional) */
#define AC97_AUX		0x16	/* AUX Volume (optional) */
#define AC97_PCM		0x18	/* PCM Volume */
#define AC97_REC_SEL		0x1a	/* Record Select */
#define AC97_REC_GAIN		0x1c	/* Record Gain */
#define AC97_REC_GAIN_MIC	0x1e	/* Record Gain MIC (optional) */
#define AC97_GENERAL_PURPOSE	0x20	/* General Purpose (optional) */
#define AC97_3D_CONTROL		0x22	/* 3D Control (optional) */
#define AC97_INT_PAGING		0x24	/* Audio Interrupt & Paging (AC'97 2.3) */
#define AC97_POWERDOWN		0x26	/* Powerdown control / status */
/* range 0x28-0x3a - AUDIO AC'97 2.0 extensions */
#define AC97_EXTENDED_ID	0x28	/* Extended Audio ID */
#define AC97_EXTENDED_STATUS	0x2a	/* Extended Audio Status and Control */
#define AC97_PCM_FRONT_DAC_RATE 0x2c	/* PCM Front DAC Rate */
#define AC97_PCM_SURR_DAC_RATE	0x2e	/* PCM Surround DAC Rate */
#define AC97_PCM_LFE_DAC_RATE	0x30	/* PCM LFE DAC Rate */
#define AC97_PCM_LR_ADC_RATE	0x32	/* PCM LR ADC Rate */
#define AC97_PCM_MIC_ADC_RATE	0x34	/* PCM MIC ADC Rate */
#define AC97_CENTER_LFE_MASTER	0x36	/* Center + LFE Master Volume */
#define AC97_SURROUND_MASTER	0x38	/* Surround (Rear) Master Volume */
#define AC97_SPDIF		0x3a	/* S/PDIF control */
/* range 0x3c-0x58 - MODEM */
#define AC97_EXTENDED_MID	0x3c	/* Extended Modem ID */
#define AC97_EXTENDED_MSTATUS	0x3e	/* Extended Modem Status and Control */
#define AC97_LINE1_RATE		0x40	/* Line1 DAC/ADC Rate */
#define AC97_LINE2_RATE		0x42	/* Line2 DAC/ADC Rate */
#define AC97_HANDSET_RATE	0x44	/* Handset DAC/ADC Rate */
#define AC97_LINE1_LEVEL	0x46	/* Line1 DAC/ADC Level */
#define AC97_LINE2_LEVEL	0x48	/* Line2 DAC/ADC Level */
#define AC97_HANDSET_LEVEL	0x4a	/* Handset DAC/ADC Level */
#define AC97_GPIO_CFG		0x4c	/* GPIO Configuration */
#define AC97_GPIO_POLARITY	0x4e	/* GPIO Pin Polarity/Type, 0=low, 1=high active */
#define AC97_GPIO_STICKY	0x50	/* GPIO Pin Sticky, 0=not, 1=sticky */
#define AC97_GPIO_WAKEUP	0x52	/* GPIO Pin Wakeup, 0=no int, 1=yes int */
#define AC97_GPIO_STATUS	0x54	/* GPIO Pin Status, slot 12 */
#define AC97_MISC_AFE		0x56	/* Miscellaneous Modem AFE Status and Control */
/* range 0x5a-0x7b - Vendor Specific */
#define AC97_VENDOR_ID1		0x7c	/* Vendor ID1 */
#define AC97_VENDOR_ID2		0x7e	/* Vendor ID2 / revision */
/* range 0x60-0x6f (page 1) - extended codec registers */
#define AC97_CODEC_CLASS_REV	0x60	/* Codec Class/Revision */
#define AC97_PCI_SVID		0x62	/* PCI Subsystem Vendor ID */
#define AC97_PCI_SID		0x64	/* PCI Subsystem ID */
#define AC97_FUNC_SELECT	0x66	/* Function Select */
#define AC97_FUNC_INFO		0x68	/* Function Information */
#define AC97_SENSE_INFO		0x6a	/* Sense Details */

/* slot allocation */
#define AC97_SLOT_TAG		0
#define AC97_SLOT_CMD_ADDR	1
#define AC97_SLOT_CMD_DATA	2
#define AC97_SLOT_PCM_LEFT	3
#define AC97_SLOT_PCM_RIGHT	4
#define AC97_SLOT_MODEM_LINE1	5
#define AC97_SLOT_PCM_CENTER	6
#define AC97_SLOT_MIC		6	/* input */
#define AC97_SLOT_SPDIF_LEFT1	6
#define AC97_SLOT_PCM_SLEFT	7	/* surround left */
#define AC97_SLOT_PCM_LEFT_0	7	/* double rate operation */
#define AC97_SLOT_SPDIF_LEFT	7
#define AC97_SLOT_PCM_SRIGHT	8	/* surround right */
#define AC97_SLOT_PCM_RIGHT_0	8	/* double rate operation */
#define AC97_SLOT_SPDIF_RIGHT	8
#define AC97_SLOT_LFE		9
#define AC97_SLOT_SPDIF_RIGHT1	9
#define AC97_SLOT_MODEM_LINE2	10
#define AC97_SLOT_PCM_LEFT_1	10	/* double rate operation */
#define AC97_SLOT_SPDIF_LEFT2	10
#define AC97_SLOT_HANDSET	11	/* output */
#define AC97_SLOT_PCM_RIGHT_1	11	/* double rate operation */
#define AC97_SLOT_SPDIF_RIGHT2	11
#define AC97_SLOT_MODEM_GPIO	12	/* modem GPIO */
#define AC97_SLOT_PCM_CENTER_1	12	/* double rate operation */

/* basic capabilities (reset register) */
#define AC97_BC_DEDICATED_MIC	0x0001	/* Dedicated Mic PCM In Channel */
#define AC97_BC_RESERVED1	0x0002	/* Reserved (was Modem Line Codec support) */
#define AC97_BC_BASS_TREBLE	0x0004	/* Bass & Treble Control */
#define AC97_BC_SIM_STEREO	0x0008	/* Simulated stereo */
#define AC97_BC_HEADPHONE	0x0010	/* Headphone Out Support */
#define AC97_BC_LOUDNESS	0x0020	/* Loudness (bass boost) Support */
#define AC97_BC_16BIT_DAC	0x0000	/* 16-bit DAC resolution */
#define AC97_BC_18BIT_DAC	0x0040	/* 18-bit DAC resolution */
#define AC97_BC_20BIT_DAC	0x0080	/* 20-bit DAC resolution */
#define AC97_BC_DAC_MASK	0x00c0
#define AC97_BC_16BIT_ADC	0x0000	/* 16-bit ADC resolution */
#define AC97_BC_18BIT_ADC	0x0100	/* 18-bit ADC resolution */
#define AC97_BC_20BIT_ADC	0x0200	/* 20-bit ADC resolution */
#define AC97_BC_ADC_MASK	0x0300

/* general purpose */
#define AC97_GP_DRSS_MASK	0x0c00	/* double rate slot select */
#define AC97_GP_DRSS_1011	0x0000	/* LR(C) 10+11(+12) */
#define AC97_GP_DRSS_78		0x0400	/* LR 7+8 */

/* powerdown bits */
#define AC97_PD_ADC_STATUS	0x0001	/* ADC status (RO) */
#define AC97_PD_DAC_STATUS	0x0002	/* DAC status (RO) */
#define AC97_PD_MIXER_STATUS	0x0004	/* Analog mixer status (RO) */
#define AC97_PD_VREF_STATUS	0x0008	/* Vref status (RO) */
#define AC97_PD_PR0		0x0100	/* Power down PCM ADCs and input MUX */
#define AC97_PD_PR1		0x0200	/* Power down PCM front DAC */
#define AC97_PD_PR2		0x0400	/* Power down Mixer (Vref still on) */
#define AC97_PD_PR3		0x0800	/* Power down Mixer (Vref off) */
#define AC97_PD_PR4		0x1000	/* Power down AC-Link */
#define AC97_PD_PR5		0x2000	/* Disable internal clock usage */
#define AC97_PD_PR6		0x4000	/* Headphone amplifier */
#define AC97_PD_EAPD		0x8000	/* External Amplifer Power Down (EAPD) */

/* extended audio ID bit defines */
#define AC97_EI_VRA		0x0001	/* Variable bit rate supported */
#define AC97_EI_DRA		0x0002	/* Double rate supported */
#define AC97_EI_SPDIF		0x0004	/* S/PDIF out supported */
#define AC97_EI_VRM		0x0008	/* Variable bit rate supported for MIC */
#define AC97_EI_DACS_SLOT_MASK	0x0030	/* DACs slot assignment */
#define AC97_EI_DACS_SLOT_SHIFT	4
#define AC97_EI_CDAC		0x0040	/* PCM Center DAC available */
#define AC97_EI_SDAC		0x0080	/* PCM Surround DACs available */
#define AC97_EI_LDAC		0x0100	/* PCM LFE DAC available */
#define AC97_EI_AMAP		0x0200	/* indicates optional slot/DAC mapping based on codec ID */
#define AC97_EI_REV_MASK	0x0c00	/* AC'97 revision mask */
#define AC97_EI_REV_22		0x0400	/* AC'97 revision 2.2 */
#define AC97_EI_REV_23		0x0800	/* AC'97 revision 2.3 */
#define AC97_EI_REV_SHIFT	10
#define AC97_EI_ADDR_MASK	0xc000	/* physical codec ID (address) */
#define AC97_EI_ADDR_SHIFT	14

/* extended audio status and control bit defines */
#define AC97_EA_VRA		0x0001	/* Variable bit rate enable bit */
#define AC97_EA_DRA		0x0002	/* Double-rate audio enable bit */
#define AC97_EA_SPDIF		0x0004	/* S/PDIF out enable bit */
#define AC97_EA_VRM		0x0008	/* Variable bit rate for MIC enable bit */
#define AC97_EA_SPSA_SLOT_MASK	0x0030	/* Mask for slot assignment bits */
#define AC97_EA_SPSA_SLOT_SHIFT 4
#define AC97_EA_SPSA_3_4	0x0000	/* Slot assigned to 3 & 4 */
#define AC97_EA_SPSA_7_8	0x0010	/* Slot assigned to 7 & 8 */
#define AC97_EA_SPSA_6_9	0x0020	/* Slot assigned to 6 & 9 */
#define AC97_EA_SPSA_10_11	0x0030	/* Slot assigned to 10 & 11 */
#define AC97_EA_CDAC		0x0040	/* PCM Center DAC is ready (Read only) */
#define AC97_EA_SDAC		0x0080	/* PCM Surround DACs are ready (Read only) */
#define AC97_EA_LDAC		0x0100	/* PCM LFE DAC is ready (Read only) */
#define AC97_EA_MDAC		0x0200	/* MIC ADC is ready (Read only) */
#define AC97_EA_SPCV		0x0400	/* S/PDIF configuration valid (Read only) */
#define AC97_EA_PRI		0x0800	/* Turns the PCM Center DAC off */
#define AC97_EA_PRJ		0x1000	/* Turns the PCM Surround DACs off */
#define AC97_EA_PRK		0x2000	/* Turns the PCM LFE DAC off */
#define AC97_EA_PRL		0x4000	/* Turns the MIC ADC off */

/* S/PDIF control bit defines */
#define AC97_SC_PRO		0x0001	/* Professional status */
#define AC97_SC_NAUDIO		0x0002	/* Non audio stream */
#define AC97_SC_COPY		0x0004	/* Copyright status */
#define AC97_SC_PRE		0x0008	/* Preemphasis status */
#define AC97_SC_CC_MASK		0x07f0	/* Category Code mask */
#define AC97_SC_CC_SHIFT	4
#define AC97_SC_L		0x0800	/* Generation Level status */
#define AC97_SC_SPSR_MASK	0x3000	/* S/PDIF Sample Rate bits */
#define AC97_SC_SPSR_SHIFT	12
#define AC97_SC_SPSR_44K	0x0000	/* Use 44.1kHz Sample rate */
#define AC97_SC_SPSR_48K	0x2000	/* Use 48kHz Sample rate */
#define AC97_SC_SPSR_32K	0x3000	/* Use 32kHz Sample rate */
#define AC97_SC_DRS		0x4000	/* Double Rate S/PDIF */
#define AC97_SC_V		0x8000	/* Validity status */

/* Interrupt and Paging bit defines (AC'97 2.3) */
#define AC97_PAGE_MASK		0x000f	/* Page Selector */
#define AC97_PAGE_VENDOR	0	/* Vendor-specific registers */
#define AC97_PAGE_1		1	/* Extended Codec Registers page 1 */
#define AC97_INT_ENABLE		0x0800	/* Interrupt Enable */
#define AC97_INT_SENSE		0x1000	/* Sense Cycle */
#define AC97_INT_CAUSE_SENSE	0x2000	/* Sense Cycle Completed (RO) */
#define AC97_INT_CAUSE_GPIO	0x4000	/* GPIO bits changed (RO) */
#define AC97_INT_STATUS		0x8000	/* Interrupt Status */

/* extended modem ID bit defines */
#define AC97_MEI_LINE1		0x0001	/* Line1 present */
#define AC97_MEI_LINE2		0x0002	/* Line2 present */
#define AC97_MEI_HANDSET	0x0004	/* Handset present */
#define AC97_MEI_CID1		0x0008	/* caller ID decode for Line1 is supported */
#define AC97_MEI_CID2		0x0010	/* caller ID decode for Line2 is supported */
#define AC97_MEI_ADDR_MASK	0xc000	/* physical codec ID (address) */
#define AC97_MEI_ADDR_SHIFT	14

/* extended modem status and control bit defines */
#define AC97_MEA_GPIO		0x0001	/* GPIO is ready (ro) */
#define AC97_MEA_MREF		0x0002	/* Vref is up to nominal level (ro) */
#define AC97_MEA_ADC1		0x0004	/* ADC1 operational (ro) */
#define AC97_MEA_DAC1		0x0008	/* DAC1 operational (ro) */
#define AC97_MEA_ADC2		0x0010	/* ADC2 operational (ro) */
#define AC97_MEA_DAC2		0x0020	/* DAC2 operational (ro) */
#define AC97_MEA_HADC		0x0040	/* HADC operational (ro) */
#define AC97_MEA_HDAC		0x0080	/* HDAC operational (ro) */
#define AC97_MEA_PRA		0x0100	/* GPIO power down (high) */
#define AC97_MEA_PRB		0x0200	/* reserved */
#define AC97_MEA_PRC		0x0400	/* ADC1 power down (high) */
#define AC97_MEA_PRD		0x0800	/* DAC1 power down (high) */
#define AC97_MEA_PRE		0x1000	/* ADC2 power down (high) */
#define AC97_MEA_PRF		0x2000	/* DAC2 power down (high) */
#define AC97_MEA_PRG		0x4000	/* HADC power down (high) */
#define AC97_MEA_PRH		0x8000	/* HDAC power down (high) */

/* modem gpio status defines */
#define AC97_GPIO_LINE1_OH      0x0001  /* Off Hook Line1 */
#define AC97_GPIO_LINE1_RI      0x0002  /* Ring Detect Line1 */
#define AC97_GPIO_LINE1_CID     0x0004  /* Caller ID path enable Line1 */
#define AC97_GPIO_LINE1_LCS     0x0008  /* Loop Current Sense Line1 */
#define AC97_GPIO_LINE1_PULSE   0x0010  /* Opt./ Pulse Dial Line1 (out) */
#define AC97_GPIO_LINE1_HL1R    0x0020  /* Opt./ Handset to Line1 relay control (out) */
#define AC97_GPIO_LINE1_HOHD    0x0040  /* Opt./ Handset off hook detect Line1 (in) */
#define AC97_GPIO_LINE12_AC     0x0080  /* Opt./ Int.bit 1 / Line1/2 AC (out) */
#define AC97_GPIO_LINE12_DC     0x0100  /* Opt./ Int.bit 2 / Line1/2 DC (out) */
#define AC97_GPIO_LINE12_RS     0x0200  /* Opt./ Int.bit 3 / Line1/2 RS (out) */
#define AC97_GPIO_LINE2_OH      0x0400  /* Off Hook Line2 */
#define AC97_GPIO_LINE2_RI      0x0800  /* Ring Detect Line2 */
#define AC97_GPIO_LINE2_CID     0x1000  /* Caller ID path enable Line2 */
#define AC97_GPIO_LINE2_LCS     0x2000  /* Loop Current Sense Line2 */
#define AC97_GPIO_LINE2_PULSE   0x4000  /* Opt./ Pulse Dial Line2 (out) */
#define AC97_GPIO_LINE2_HL1R    0x8000  /* Opt./ Handset to Line2 relay control (out) */

/* specific - SigmaTel */
#define AC97_SIGMATEL_OUTSEL	0x64	/* Output Select, STAC9758 */
#define AC97_SIGMATEL_INSEL	0x66	/* Input Select, STAC9758 */
#define AC97_SIGMATEL_IOMISC	0x68	/* STAC9758 */
#define AC97_SIGMATEL_ANALOG	0x6c	/* Analog Special */
#define AC97_SIGMATEL_DAC2INVERT 0x6e
#define AC97_SIGMATEL_BIAS1	0x70
#define AC97_SIGMATEL_BIAS2	0x72
#define AC97_SIGMATEL_VARIOUS	0x72	/* STAC9758 */
#define AC97_SIGMATEL_MULTICHN	0x74	/* Multi-Channel programming */
#define AC97_SIGMATEL_CIC1	0x76
#define AC97_SIGMATEL_CIC2	0x78

/* specific - Analog Devices */
#define AC97_AD_TEST		0x5a	/* test register */
#define AC97_AD_TEST2		0x5c	/* undocumented test register 2 */
#define AC97_AD_CODEC_CFG	0x70	/* codec configuration */
#define AC97_AD_JACK_SPDIF	0x72	/* Jack Sense & S/PDIF */
#define AC97_AD_SERIAL_CFG	0x74	/* Serial Configuration */
#define AC97_AD_MISC		0x76	/* Misc Control Bits */

/* specific - Cirrus Logic */
#define AC97_CSR_ACMODE		0x5e	/* AC Mode Register */
#define AC97_CSR_MISC_CRYSTAL	0x60	/* Misc Crystal Control */
#define AC97_CSR_SPDIF		0x68	/* S/PDIF Register */
#define AC97_CSR_SERIAL		0x6a	/* Serial Port Control */
#define AC97_CSR_SPECF_ADDR	0x6c	/* Special Feature Address */
#define AC97_CSR_SPECF_DATA	0x6e	/* Special Feature Data */
#define AC97_CSR_BDI_STATUS	0x7a	/* BDI Status */

/* specific - Conexant */
#define AC97_CXR_AUDIO_MISC	0x5c
#define AC97_CXR_SPDIFEN	(1<<3)
#define AC97_CXR_COPYRGT	(1<<2)
#define AC97_CXR_SPDIF_MASK	(3<<0)
#define AC97_CXR_SPDIF_PCM	0x0
#define AC97_CXR_SPDIF_AC3	0x2

/* specific - ALC */
#define AC97_ALC650_SPDIF_INPUT_STATUS1	0x60
/* S/PDIF input status 1 bit defines */
#define AC97_ALC650_PRO             0x0001  /* Professional status */
#define AC97_ALC650_NAUDIO          0x0002  /* Non audio stream */
#define AC97_ALC650_COPY            0x0004  /* Copyright status */
#define AC97_ALC650_PRE             0x0038  /* Preemphasis status */
#define AC97_ALC650_PRE_SHIFT       3
#define AC97_ALC650_MODE            0x00C0  /* Preemphasis status */
#define AC97_ALC650_MODE_SHIFT      6
#define AC97_ALC650_CC_MASK         0x7f00  /* Category Code mask */
#define AC97_ALC650_CC_SHIFT        8
#define AC97_ALC650_L               0x8000  /* Generation Level status */

#define AC97_ALC650_SPDIF_INPUT_STATUS2	0x62
/* S/PDIF input status 2 bit defines */
#define AC97_ALC650_SOUCE_MASK      0x000f  /* Source number */
#define AC97_ALC650_CHANNEL_MASK    0x00f0  /* Channel number */
#define AC97_ALC650_CHANNEL_SHIFT   4 
#define AC97_ALC650_SPSR_MASK       0x0f00  /* S/PDIF Sample Rate bits */
#define AC97_ALC650_SPSR_SHIFT      8
#define AC97_ALC650_SPSR_44K        0x0000  /* Use 44.1kHz Sample rate */
#define AC97_ALC650_SPSR_48K        0x0200  /* Use 48kHz Sample rate */
#define AC97_ALC650_SPSR_32K        0x0300  /* Use 32kHz Sample rate */
#define AC97_ALC650_CLOCK_ACCURACY  0x3000  /* Clock accuracy */
#define AC97_ALC650_CLOCK_SHIFT     12
#define AC97_ALC650_CLOCK_LOCK      0x4000  /* Clock locked status */
#define AC97_ALC650_V               0x8000  /* Validity status */

#define AC97_ALC650_SURR_DAC_VOL	0x64
#define AC97_ALC650_LFE_DAC_VOL		0x66
#define AC97_ALC650_UNKNOWN1		0x68
#define AC97_ALC650_MULTICH		0x6a
#define AC97_ALC650_UNKNOWN2		0x6c
#define AC97_ALC650_REVISION		0x6e
#define AC97_ALC650_UNKNOWN3		0x70
#define AC97_ALC650_UNKNOWN4		0x72
#define AC97_ALC650_MISC		0x74
#define AC97_ALC650_GPIO_SETUP		0x76
#define AC97_ALC650_GPIO_STATUS		0x78
#define AC97_ALC650_CLOCK		0x7a

/* specific - Yamaha YMF753 */
#define AC97_YMF753_DIT_CTRL2	0x66	/* DIT Control 2 */
#define AC97_YMF753_3D_MODE_SEL	0x68	/* 3D Mode Select */

/* specific - C-Media */
#define AC97_CM9738_VENDOR_CTRL	0x5a
#define AC97_CM9739_MULTI_CHAN	0x64
#define AC97_CM9739_SPDIF_IN_STATUS	0x68 /* 32bit */
#define AC97_CM9739_SPDIF_CTRL	0x6c

/* specific - wolfson */
#define AC97_WM97XX_FMIXER_VOL  0x72
#define AC97_WM9704_RMIXER_VOL  0x74
#define AC97_WM9704_TEST        0x5a
#define AC97_WM9704_RPCM_VOL    0x70
#define AC97_WM9711_OUT3VOL     0x16


/* ac97->scaps */
#define AC97_SCAP_AUDIO		(1<<0)	/* audio codec 97 */
#define AC97_SCAP_MODEM		(1<<1)	/* modem codec 97 */
#define AC97_SCAP_SURROUND_DAC	(1<<2)	/* surround L&R DACs are present */
#define AC97_SCAP_CENTER_LFE_DAC (1<<3)	/* center and LFE DACs are present */
#define AC97_SCAP_SKIP_AUDIO	(1<<4)	/* skip audio part of codec */
#define AC97_SCAP_SKIP_MODEM	(1<<5)	/* skip modem part of codec */
#define AC97_SCAP_INDEP_SDIN	(1<<6)	/* independent SDIN */
#define AC97_SCAP_INV_EAPD	(1<<7)	/* inverted EAPD */
#define AC97_SCAP_DETECT_BY_VENDOR (1<<8) /* use vendor registers for read tests */
#define AC97_SCAP_NO_SPDIF	(1<<9)	/* don't build SPDIF controls */
#define AC97_SCAP_EAPD_LED	(1<<10)	/* EAPD as mute LED */

/* ac97->flags */
#define AC97_HAS_PC_BEEP	(1<<0)	/* force PC Speaker usage */
#define AC97_AD_MULTI		(1<<1)	/* Analog Devices - multi codecs */
#define AC97_CS_SPDIF		(1<<2)	/* Cirrus Logic uses funky SPDIF */
#define AC97_CX_SPDIF		(1<<3)	/* Conexant's spdif interface */
#define AC97_STEREO_MUTES	(1<<4)	/* has stereo mute bits */
#define AC97_DOUBLE_RATE	(1<<5)	/* supports double rate playback */
#define AC97_HAS_NO_MASTER_VOL	(1<<6)	/* no Master volume */
#define AC97_HAS_NO_PCM_VOL	(1<<7)	/* no PCM volume */
#define AC97_DEFAULT_POWER_OFF	(1<<8)	/* no RESET write */
#define AC97_MODEM_PATCH	(1<<9)	/* modem patch */
#define AC97_HAS_NO_REC_GAIN	(1<<10) /* no Record gain */
#define AC97_HAS_NO_PHONE	(1<<11) /* no PHONE volume */
#define AC97_HAS_NO_PC_BEEP	(1<<12) /* no PC Beep volume */
#define AC97_HAS_NO_VIDEO	(1<<13) /* no Video volume */
#define AC97_HAS_NO_CD		(1<<14) /* no CD volume */
#define AC97_HAS_NO_MIC	(1<<15) /* no MIC volume */
#define AC97_HAS_NO_TONE	(1<<16) /* no Tone volume */
#define AC97_HAS_NO_STD_PCM	(1<<17)	/* no standard AC97 PCM volume and mute */
#define AC97_HAS_NO_AUX		(1<<18) /* no standard AC97 AUX volume and mute */

/* rates indexes */
#define AC97_RATES_FRONT_DAC	0
#define AC97_RATES_SURR_DAC	1
#define AC97_RATES_LFE_DAC	2
#define AC97_RATES_ADC		3
#define AC97_RATES_MIC_ADC	4
#define AC97_RATES_SPDIF	5

/*
 *
 */

struct snd_ac97;

struct snd_ac97_build_ops {
	int (*build_3d) (struct snd_ac97 *ac97);
	int (*build_specific) (struct snd_ac97 *ac97);
	int (*build_spdif) (struct snd_ac97 *ac97);
	int (*build_post_spdif) (struct snd_ac97 *ac97);
#ifdef CONFIG_PM
	void (*suspend) (struct snd_ac97 *ac97);
	void (*resume) (struct snd_ac97 *ac97);
#endif
	void (*update_jacks) (struct snd_ac97 *ac97);	/* for jack-sharing */
};

struct snd_ac97_bus_ops {
	void (*reset) (struct snd_ac97 *ac97);
	void (*write) (struct snd_ac97 *ac97, unsigned short reg, unsigned short val);
	unsigned short (*read) (struct snd_ac97 *ac97, unsigned short reg);
	void (*wait) (struct snd_ac97 *ac97);
	void (*init) (struct snd_ac97 *ac97);
};

struct snd_ac97_bus {
	/* -- lowlevel (hardware) driver specific -- */
	struct snd_ac97_bus_ops *ops;
	void *private_data;
	void (*private_free) (struct snd_ac97_bus *bus);
	/* --- */
	struct snd_card *card;
	unsigned short num;	/* bus number */
	unsigned short no_vra: 1, /* bridge doesn't support VRA */
		       dra: 1,	/* bridge supports double rate */
		       isdin: 1;/* independent SDIN */
	unsigned int clock;	/* AC'97 base clock (usually 48000Hz) */
	spinlock_t bus_lock;	/* used mainly for slot allocation */
	unsigned short used_slots[2][4]; /* actually used PCM slots */
	unsigned short pcms_count; /* count of PCMs */
	struct ac97_pcm *pcms;
	struct snd_ac97 *codec[4];
	struct snd_info_entry *proc;
};

/* static resolution table */
struct snd_ac97_res_table {
	unsigned short reg;	/* register */
	unsigned short bits;	/* resolution bitmask */
};

struct snd_ac97_template {
	void *private_data;
	void (*private_free) (struct snd_ac97 *ac97);
	struct pci_dev *pci;	/* assigned PCI device - used for quirks */
	unsigned short num;	/* number of codec: 0 = primary, 1 = secondary */
	unsigned short addr;	/* physical address of codec [0-3] */
	unsigned int scaps;	/* driver capabilities */
	const struct snd_ac97_res_table *res_table;	/* static resolution */
};

struct snd_ac97 {
	/* -- lowlevel (hardware) driver specific -- */
	struct snd_ac97_build_ops * build_ops;
	void *private_data;
	void (*private_free) (struct snd_ac97 *ac97);
	/* --- */
	struct snd_ac97_bus *bus;
	struct pci_dev *pci;	/* assigned PCI device - used for quirks */
	struct snd_info_entry *proc;
	struct snd_info_entry *proc_regs;
	unsigned short subsystem_vendor;
	unsigned short subsystem_device;
	struct mutex reg_mutex;
	struct mutex page_mutex;	/* mutex for AD18xx multi-codecs and paging (2.3) */
	unsigned short num;	/* number of codec: 0 = primary, 1 = secondary */
	unsigned short addr;	/* physical address of codec [0-3] */
	unsigned int id;	/* identification of codec */
	unsigned short caps;	/* capabilities (register 0) */
	unsigned short ext_id;	/* extended feature identification (register 28) */
	unsigned short ext_mid;	/* extended modem ID (register 3C) */
	const struct snd_ac97_res_table *res_table;	/* static resolution */
	unsigned int scaps;	/* driver capabilities */
	unsigned int flags;	/* specific code */
	unsigned int rates[6];	/* see AC97_RATES_* defines */
	unsigned int spdif_status;
	unsigned short regs[0x80]; /* register cache */
	DECLARE_BITMAP(reg_accessed, 0x80); /* bit flags */
	union {			/* vendor specific code */
		struct {
			unsigned short unchained[3];	// 0 = C34, 1 = C79, 2 = C69
			unsigned short chained[3];	// 0 = C34, 1 = C79, 2 = C69
			unsigned short id[3];		// codec IDs (lower 16-bit word)
			unsigned short pcmreg[3];	// PCM registers
			unsigned short codec_cfg[3];	// CODEC_CFG bits
		} ad18xx;
		unsigned int dev_flags;		/* device specific */
	} spec;
	/* jack-sharing info */
	unsigned char indep_surround;
	unsigned char channel_mode;

#ifdef CONFIG_SND_AC97_POWER_SAVE
	unsigned int power_up;	/* power states */
	struct workqueue_struct *power_workq;
	struct delayed_work power_work;
#endif
	struct device dev;
};

#define to_ac97_t(d) container_of(d, struct snd_ac97, dev)

/* conditions */
static inline int ac97_is_audio(struct snd_ac97 * ac97)
{
	return (ac97->scaps & AC97_SCAP_AUDIO);
}
static inline int ac97_is_modem(struct snd_ac97 * ac97)
{
	return (ac97->scaps & AC97_SCAP_MODEM);
}
static inline int ac97_is_rev22(struct snd_ac97 * ac97)
{
	return (ac97->ext_id & AC97_EI_REV_MASK) >= AC97_EI_REV_22;
}
static inline int ac97_can_amap(struct snd_ac97 * ac97)
{
	return (ac97->ext_id & AC97_EI_AMAP) != 0;
}
static inline int ac97_can_spdif(struct snd_ac97 * ac97)
{
	return (ac97->ext_id & AC97_EI_SPDIF) != 0;
}

/* functions */
/* create new AC97 bus */
int snd_ac97_bus(struct snd_card *card, int num, struct snd_ac97_bus_ops *ops,
		 void *private_data, struct snd_ac97_bus **rbus);
/* create mixer controls */
int snd_ac97_mixer(struct snd_ac97_bus *bus, struct snd_ac97_template *template,
		   struct snd_ac97 **rac97);
const char *snd_ac97_get_short_name(struct snd_ac97 *ac97);

void snd_ac97_write(struct snd_ac97 *ac97, unsigned short reg, unsigned short value);
unsigned short snd_ac97_read(struct snd_ac97 *ac97, unsigned short reg);
void snd_ac97_write_cache(struct snd_ac97 *ac97, unsigned short reg, unsigned short value);
int snd_ac97_update(struct snd_ac97 *ac97, unsigned short reg, unsigned short value);
int snd_ac97_update_bits(struct snd_ac97 *ac97, unsigned short reg, unsigned short mask, unsigned short value);
#ifdef CONFIG_SND_AC97_POWER_SAVE
int snd_ac97_update_power(struct snd_ac97 *ac97, int reg, int powerup);
#else
static inline int snd_ac97_update_power(struct snd_ac97 *ac97, int reg,
					int powerup)
{
	return 0;
}
#endif
#ifdef CONFIG_PM
void snd_ac97_suspend(struct snd_ac97 *ac97);
void snd_ac97_resume(struct snd_ac97 *ac97);
#endif

/* quirk types */
enum {
	AC97_TUNE_DEFAULT = -1,	/* use default from quirk list (not valid in list) */
	AC97_TUNE_NONE = 0,	/* nothing extra to do */
	AC97_TUNE_HP_ONLY,	/* headphone (true line-out) control as master only */
	AC97_TUNE_SWAP_HP,	/* swap headphone and master controls */
	AC97_TUNE_SWAP_SURROUND, /* swap master and surround controls */
	AC97_TUNE_AD_SHARING,	/* for AD1985, turn on OMS bit and use headphone */
	AC97_TUNE_ALC_JACK,	/* for Realtek, enable JACK detection */
	AC97_TUNE_INV_EAPD,	/* inverted EAPD implementation */
	AC97_TUNE_MUTE_LED,	/* EAPD bit works as mute LED */
	AC97_TUNE_HP_MUTE_LED,  /* EAPD bit works as mute LED, use headphone control as master */
};

struct ac97_quirk {
	unsigned short subvendor; /* PCI subsystem vendor id */
	unsigned short subdevice; /* PCI sybsystem device id */
	unsigned short mask;	/* device id bit mask, 0 = accept all */
	unsigned int codec_id;	/* codec id (if any), 0 = accept all */
	const char *name;	/* name shown as info */
	int type;		/* quirk type above */
};

int snd_ac97_tune_hardware(struct snd_ac97 *ac97, struct ac97_quirk *quirk, const char *override);
int snd_ac97_set_rate(struct snd_ac97 *ac97, int reg, unsigned int rate);

/*
 * PCM allocation
 */

enum ac97_pcm_cfg {
	AC97_PCM_CFG_FRONT = 2,
	AC97_PCM_CFG_REAR = 10,		/* alias surround */
	AC97_PCM_CFG_LFE = 11,		/* center + lfe */
	AC97_PCM_CFG_40 = 4,		/* front + rear */
	AC97_PCM_CFG_51 = 6,		/* front + rear + center/lfe */
	AC97_PCM_CFG_SPDIF = 20
};

struct ac97_pcm {
	struct snd_ac97_bus *bus;
	unsigned int stream: 1,	   	   /* stream type: 1 = capture */
		     exclusive: 1,	   /* exclusive mode, don't override with other pcms */
		     copy_flag: 1,	   /* lowlevel driver must fill all entries */
		     spdif: 1;		   /* spdif pcm */
	unsigned short aslots;		   /* active slots */
	unsigned short cur_dbl;		   /* current double-rate state */
	unsigned int rates;		   /* available rates */
	struct {
		unsigned short slots;	   /* driver input: requested AC97 slot numbers */
		unsigned short rslots[4];  /* allocated slots per codecs */
		unsigned char rate_table[4];
		struct snd_ac97 *codec[4];	   /* allocated codecs */
	} r[2];				   /* 0 = standard rates, 1 = double rates */
	unsigned long private_value;	   /* used by the hardware driver */
};

int snd_ac97_pcm_assign(struct snd_ac97_bus *ac97,
			unsigned short pcms_count,
			const struct ac97_pcm *pcms);
int snd_ac97_pcm_open(struct ac97_pcm *pcm, unsigned int rate,
		      enum ac97_pcm_cfg cfg, unsigned short slots);
int snd_ac97_pcm_close(struct ac97_pcm *pcm);
int snd_ac97_pcm_double_rate_rules(struct snd_pcm_runtime *runtime);

/* ad hoc AC97 device driver access */
extern struct bus_type ac97_bus_type;

#endif /* __SOUND_AC97_CODEC_H */
