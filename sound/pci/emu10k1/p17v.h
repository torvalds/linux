/*
 *  Copyright (c) by James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver p17v chips
 *  Version: 0.01
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

/******************************************************************************/
/* Audigy2Value Tina (P17V) pointer-offset register set,
 * accessed through the PTR20 and DATA24 registers  */
/******************************************************************************/

/* 00 - 07: Not used */
#define P17V_PLAYBACK_FIFO_PTR	0x08	/* Current playback fifo pointer
					 * and number of sound samples in cache.
					 */  
/* 09 - 12: Not used */
#define P17V_CAPTURE_FIFO_PTR	0x13	/* Current capture fifo pointer
					 * and number of sound samples in cache.
					 */  
/* 14 - 17: Not used */
#define P17V_PB_CHN_SEL		0x18	/* P17v playback channel select */
#define P17V_SE_SLOT_SEL_L	0x19	/* Sound Engine slot select low */
#define P17V_SE_SLOT_SEL_H	0x1a	/* Sound Engine slot select high */
/* 1b - 1f: Not used */
/* 20 - 2f: Not used */
/* 30 - 3b: Not used */
#define P17V_SPI		0x3c	/* SPI interface register */
#define P17V_I2C_ADDR		0x3d	/* I2C Address */
#define P17V_I2C_0		0x3e	/* I2C Data */
#define P17V_I2C_1		0x3f	/* I2C Data */
/* I2C values */
#define I2C_A_ADC_ADD_MASK	0x000000fe	/*The address is a 7 bit address */
#define I2C_A_ADC_RW_MASK	0x00000001	/*bit mask for R/W */
#define I2C_A_ADC_TRANS_MASK	0x00000010  	/*Bit mask for I2c address DAC value  */
#define I2C_A_ADC_ABORT_MASK	0x00000020	/*Bit mask for I2C transaction abort flag */
#define I2C_A_ADC_LAST_MASK	0x00000040	/*Bit mask for Last word transaction */
#define I2C_A_ADC_BYTE_MASK	0x00000080	/*Bit mask for Byte Mode */

#define I2C_A_ADC_ADD		0x00000034	/*This is the Device address for ADC  */
#define I2C_A_ADC_READ		0x00000001	/*To perform a read operation */
#define I2C_A_ADC_START		0x00000100	/*Start I2C transaction */
#define I2C_A_ADC_ABORT		0x00000200	/*I2C transaction abort */
#define I2C_A_ADC_LAST		0x00000400	/*I2C last transaction */
#define I2C_A_ADC_BYTE		0x00000800	/*I2C one byte mode */

#define I2C_D_ADC_REG_MASK	0xfe000000  	/*ADC address register */ 
#define I2C_D_ADC_DAT_MASK	0x01ff0000  	/*ADC data register */

#define ADC_TIMEOUT		0x00000007	/*ADC Timeout Clock Disable */
#define ADC_IFC_CTRL		0x0000000b	/*ADC Interface Control */
#define ADC_MASTER		0x0000000c	/*ADC Master Mode Control */
#define ADC_POWER		0x0000000d	/*ADC PowerDown Control */
#define ADC_ATTEN_ADCL		0x0000000e	/*ADC Attenuation ADCL */
#define ADC_ATTEN_ADCR		0x0000000f	/*ADC Attenuation ADCR */
#define ADC_ALC_CTRL1		0x00000010	/*ADC ALC Control 1 */
#define ADC_ALC_CTRL2		0x00000011	/*ADC ALC Control 2 */
#define ADC_ALC_CTRL3		0x00000012	/*ADC ALC Control 3 */
#define ADC_NOISE_CTRL		0x00000013	/*ADC Noise Gate Control */
#define ADC_LIMIT_CTRL		0x00000014	/*ADC Limiter Control */
#define ADC_MUX			0x00000015  	/*ADC Mux offset */
#if 0
/* FIXME: Not tested yet. */
#define ADC_GAIN_MASK		0x000000ff	//Mask for ADC Gain
#define ADC_ZERODB		0x000000cf	//Value to set ADC to 0dB
#define ADC_MUTE_MASK		0x000000c0	//Mask for ADC mute
#define ADC_MUTE		0x000000c0	//Value to mute ADC
#define ADC_OSR			0x00000008	//Mask for ADC oversample rate select
#define ADC_TIMEOUT_DISABLE	0x00000008	//Value and mask to disable Timeout clock
#define ADC_HPF_DISABLE		0x00000100	//Value and mask to disable High pass filter
#define ADC_TRANWIN_MASK	0x00000070	//Mask for Length of Transient Window
#endif

#define ADC_MUX_MASK		0x0000000f	//Mask for ADC Mux
#define ADC_MUX_0		0x00000001	//Value to select Unknown at ADC Mux (Not used)
#define ADC_MUX_1		0x00000002	//Value to select Unknown at ADC Mux (Not used)
#define ADC_MUX_2		0x00000004	//Value to select Mic at ADC Mux
#define ADC_MUX_3		0x00000008	//Value to select Line-In at ADC Mux

#define P17V_START_AUDIO	0x40	/* Start Audio bit */
/* 41 - 47: Reserved */
#define P17V_START_CAPTURE	0x48	/* Start Capture bit */
#define P17V_CAPTURE_FIFO_BASE	0x49	/* Record FIFO base address */
#define P17V_CAPTURE_FIFO_SIZE	0x4a	/* Record FIFO buffer size */
#define P17V_CAPTURE_FIFO_INDEX	0x4b	/* Record FIFO capture index */
#define P17V_CAPTURE_VOL_H	0x4c	/* P17v capture volume control */
#define P17V_CAPTURE_VOL_L	0x4d	/* P17v capture volume control */
/* 4e - 4f: Not used */
/* 50 - 5f: Not used */
#define P17V_SRCSel		0x60	/* SRC48 and SRCMulti sample rate select
					 * and output select
					 */
#define P17V_MIXER_AC97_10K1_VOL_L	0x61	/* 10K to Mixer_AC97 input volume control */
#define P17V_MIXER_AC97_10K1_VOL_H	0x62	/* 10K to Mixer_AC97 input volume control */
#define P17V_MIXER_AC97_P17V_VOL_L	0x63	/* P17V to Mixer_AC97 input volume control */
#define P17V_MIXER_AC97_P17V_VOL_H	0x64	/* P17V to Mixer_AC97 input volume control */
#define P17V_MIXER_AC97_SRP_REC_VOL_L	0x65	/* SRP Record to Mixer_AC97 input volume control */
#define P17V_MIXER_AC97_SRP_REC_VOL_H	0x66	/* SRP Record to Mixer_AC97 input volume control */
/* 67 - 68: Reserved */
#define P17V_MIXER_Spdif_10K1_VOL_L	0x69	/* 10K to Mixer_Spdif input volume control */
#define P17V_MIXER_Spdif_10K1_VOL_H	0x6A	/* 10K to Mixer_Spdif input volume control */
#define P17V_MIXER_Spdif_P17V_VOL_L	0x6B	/* P17V to Mixer_Spdif input volume control */
#define P17V_MIXER_Spdif_P17V_VOL_H	0x6C	/* P17V to Mixer_Spdif input volume control */
#define P17V_MIXER_Spdif_SRP_REC_VOL_L	0x6D	/* SRP Record to Mixer_Spdif input volume control */
#define P17V_MIXER_Spdif_SRP_REC_VOL_H	0x6E	/* SRP Record to Mixer_Spdif input volume control */
/* 6f - 70: Reserved */
#define P17V_MIXER_I2S_10K1_VOL_L	0x71	/* 10K to Mixer_I2S input volume control */
#define P17V_MIXER_I2S_10K1_VOL_H	0x72	/* 10K to Mixer_I2S input volume control */
#define P17V_MIXER_I2S_P17V_VOL_L	0x73	/* P17V to Mixer_I2S input volume control */
#define P17V_MIXER_I2S_P17V_VOL_H	0x74	/* P17V to Mixer_I2S input volume control */
#define P17V_MIXER_I2S_SRP_REC_VOL_L	0x75	/* SRP Record to Mixer_I2S input volume control */
#define P17V_MIXER_I2S_SRP_REC_VOL_H	0x76	/* SRP Record to Mixer_I2S input volume control */
/* 77 - 78: Reserved */
#define P17V_MIXER_AC97_ENABLE		0x79	/* Mixer AC97 input audio enable */
#define P17V_MIXER_SPDIF_ENABLE		0x7A	/* Mixer SPDIF input audio enable */
#define P17V_MIXER_I2S_ENABLE		0x7B	/* Mixer I2S input audio enable */
#define P17V_AUDIO_OUT_ENABLE		0x7C	/* Audio out enable */
#define P17V_MIXER_ATT			0x7D	/* SRP Mixer Attenuation Select */
#define P17V_SRP_RECORD_SRR		0x7E	/* SRP Record channel source Select */
#define P17V_SOFT_RESET_SRP_MIXER	0x7F	/* SRP and mixer soft reset */

#define P17V_AC97_OUT_MASTER_VOL_L	0x80	/* AC97 Output master volume control */
#define P17V_AC97_OUT_MASTER_VOL_H	0x81	/* AC97 Output master volume control */
#define P17V_SPDIF_OUT_MASTER_VOL_L	0x82	/* SPDIF Output master volume control */
#define P17V_SPDIF_OUT_MASTER_VOL_H	0x83	/* SPDIF Output master volume control */
#define P17V_I2S_OUT_MASTER_VOL_L	0x84	/* I2S Output master volume control */
#define P17V_I2S_OUT_MASTER_VOL_H	0x85	/* I2S Output master volume control */
/* 86 - 87: Not used */
#define P17V_I2S_CHANNEL_SWAP_PHASE_INVERSE	0x88	/* I2S out mono channel swap
							 * and phase inverse */
#define P17V_SPDIF_CHANNEL_SWAP_PHASE_INVERSE	0x89	/* SPDIF out mono channel swap
							 * and phase inverse */
/* 8A: Not used */
#define P17V_SRP_P17V_ESR		0x8B	/* SRP_P17V estimated sample rate and rate lock */
#define P17V_SRP_REC_ESR		0x8C	/* SRP_REC estimated sample rate and rate lock */
#define P17V_SRP_BYPASS			0x8D	/* srps channel bypass and srps bypass */
/* 8E - 92: Not used */
#define P17V_I2S_SRC_SEL		0x93	/* I2SIN mode sel */






