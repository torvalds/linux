/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
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

/* volume controls */
#define AC97_MUTE_MASK_MONO	0x8000
#define AC97_MUTE_MASK_STEREO	0x8080

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
#define AC97_BC_3D_TECH_ID_MASK	0x7c00	/* Per-vendor ID of 3D enhancement */

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

