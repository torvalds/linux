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






