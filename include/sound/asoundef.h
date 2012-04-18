#ifndef __SOUND_ASOUNDEF_H
#define __SOUND_ASOUNDEF_H

/*
 *  Advanced Linux Sound Architecture - ALSA - Driver
 *  Copyright (c) 1994-2000 by Jaroslav Kysela <perex@perex.cz>
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

/****************************************************************************
 *                                                                          *
 *        Digital audio interface					    *
 *                                                                          *
 ****************************************************************************/

/* AES/IEC958 channel status bits */
#define IEC958_AES0_PROFESSIONAL	(1<<0)	/* 0 = consumer, 1 = professional */
#define IEC958_AES0_NONAUDIO		(1<<1)	/* 0 = audio, 1 = non-audio */
#define IEC958_AES0_PRO_EMPHASIS	(7<<2)	/* mask - emphasis */
#define IEC958_AES0_PRO_EMPHASIS_NOTID	(0<<2)	/* emphasis not indicated */
#define IEC958_AES0_PRO_EMPHASIS_NONE	(1<<2)	/* none emphasis */
#define IEC958_AES0_PRO_EMPHASIS_5015	(3<<2)	/* 50/15us emphasis */
#define IEC958_AES0_PRO_EMPHASIS_CCITT	(7<<2)	/* CCITT J.17 emphasis */
#define IEC958_AES0_PRO_FREQ_UNLOCKED	(1<<5)	/* source sample frequency: 0 = locked, 1 = unlocked */
#define IEC958_AES0_PRO_FS		(3<<6)	/* mask - sample frequency */
#define IEC958_AES0_PRO_FS_NOTID	(0<<6)	/* fs not indicated */
#define IEC958_AES0_PRO_FS_44100	(1<<6)	/* 44.1kHz */
#define IEC958_AES0_PRO_FS_48000	(2<<6)	/* 48kHz */
#define IEC958_AES0_PRO_FS_32000	(3<<6)	/* 32kHz */
#define IEC958_AES0_CON_NOT_COPYRIGHT	(1<<2)	/* 0 = copyright, 1 = not copyright */
#define IEC958_AES0_CON_EMPHASIS	(7<<3)	/* mask - emphasis */
#define IEC958_AES0_CON_EMPHASIS_NONE	(0<<3)	/* none emphasis */
#define IEC958_AES0_CON_EMPHASIS_5015	(1<<3)	/* 50/15us emphasis */
#define IEC958_AES0_CON_MODE		(3<<6)	/* mask - mode */
#define IEC958_AES1_PRO_MODE		(15<<0)	/* mask - channel mode */
#define IEC958_AES1_PRO_MODE_NOTID	(0<<0)	/* not indicated */
#define IEC958_AES1_PRO_MODE_STEREOPHONIC (2<<0) /* stereophonic - ch A is left */
#define IEC958_AES1_PRO_MODE_SINGLE	(4<<0)	/* single channel */
#define IEC958_AES1_PRO_MODE_TWO	(8<<0)	/* two channels */
#define IEC958_AES1_PRO_MODE_PRIMARY	(12<<0)	/* primary/secondary */
#define IEC958_AES1_PRO_MODE_BYTE3	(15<<0)	/* vector to byte 3 */
#define IEC958_AES1_PRO_USERBITS	(15<<4)	/* mask - user bits */
#define IEC958_AES1_PRO_USERBITS_NOTID	(0<<4)	/* not indicated */
#define IEC958_AES1_PRO_USERBITS_192	(8<<4)	/* 192-bit structure */
#define IEC958_AES1_PRO_USERBITS_UDEF	(12<<4)	/* user defined application */
#define IEC958_AES1_CON_CATEGORY	0x7f
#define IEC958_AES1_CON_GENERAL		0x00
#define IEC958_AES1_CON_LASEROPT_MASK	0x07
#define IEC958_AES1_CON_LASEROPT_ID	0x01
#define IEC958_AES1_CON_IEC908_CD	(IEC958_AES1_CON_LASEROPT_ID|0x00)
#define IEC958_AES1_CON_NON_IEC908_CD	(IEC958_AES1_CON_LASEROPT_ID|0x08)
#define IEC958_AES1_CON_MINI_DISC	(IEC958_AES1_CON_LASEROPT_ID|0x48)
#define IEC958_AES1_CON_DVD		(IEC958_AES1_CON_LASEROPT_ID|0x18)
#define IEC958_AES1_CON_LASTEROPT_OTHER	(IEC958_AES1_CON_LASEROPT_ID|0x78)
#define IEC958_AES1_CON_DIGDIGCONV_MASK 0x07
#define IEC958_AES1_CON_DIGDIGCONV_ID	0x02
#define IEC958_AES1_CON_PCM_CODER	(IEC958_AES1_CON_DIGDIGCONV_ID|0x00)
#define IEC958_AES1_CON_MIXER		(IEC958_AES1_CON_DIGDIGCONV_ID|0x10)
#define IEC958_AES1_CON_RATE_CONVERTER	(IEC958_AES1_CON_DIGDIGCONV_ID|0x18)
#define IEC958_AES1_CON_SAMPLER		(IEC958_AES1_CON_DIGDIGCONV_ID|0x20)
#define IEC958_AES1_CON_DSP		(IEC958_AES1_CON_DIGDIGCONV_ID|0x28)
#define IEC958_AES1_CON_DIGDIGCONV_OTHER (IEC958_AES1_CON_DIGDIGCONV_ID|0x78)
#define IEC958_AES1_CON_MAGNETIC_MASK	0x07
#define IEC958_AES1_CON_MAGNETIC_ID	0x03
#define IEC958_AES1_CON_DAT		(IEC958_AES1_CON_MAGNETIC_ID|0x00)
#define IEC958_AES1_CON_VCR		(IEC958_AES1_CON_MAGNETIC_ID|0x08)
#define IEC958_AES1_CON_DCC		(IEC958_AES1_CON_MAGNETIC_ID|0x40)
#define IEC958_AES1_CON_MAGNETIC_DISC	(IEC958_AES1_CON_MAGNETIC_ID|0x18)
#define IEC958_AES1_CON_MAGNETIC_OTHER	(IEC958_AES1_CON_MAGNETIC_ID|0x78)
#define IEC958_AES1_CON_BROADCAST1_MASK 0x07
#define IEC958_AES1_CON_BROADCAST1_ID	0x04
#define IEC958_AES1_CON_DAB_JAPAN	(IEC958_AES1_CON_BROADCAST1_ID|0x00)
#define IEC958_AES1_CON_DAB_EUROPE	(IEC958_AES1_CON_BROADCAST1_ID|0x08)
#define IEC958_AES1_CON_DAB_USA		(IEC958_AES1_CON_BROADCAST1_ID|0x60)
#define IEC958_AES1_CON_SOFTWARE	(IEC958_AES1_CON_BROADCAST1_ID|0x40)
#define IEC958_AES1_CON_IEC62105	(IEC958_AES1_CON_BROADCAST1_ID|0x20)
#define IEC958_AES1_CON_BROADCAST1_OTHER (IEC958_AES1_CON_BROADCAST1_ID|0x78)
#define IEC958_AES1_CON_BROADCAST2_MASK 0x0f
#define IEC958_AES1_CON_BROADCAST2_ID	0x0e
#define IEC958_AES1_CON_MUSICAL_MASK	0x07
#define IEC958_AES1_CON_MUSICAL_ID	0x05
#define IEC958_AES1_CON_SYNTHESIZER	(IEC958_AES1_CON_MUSICAL_ID|0x00)
#define IEC958_AES1_CON_MICROPHONE	(IEC958_AES1_CON_MUSICAL_ID|0x08)
#define IEC958_AES1_CON_MUSICAL_OTHER	(IEC958_AES1_CON_MUSICAL_ID|0x78)
#define IEC958_AES1_CON_ADC_MASK	0x1f
#define IEC958_AES1_CON_ADC_ID		0x06
#define IEC958_AES1_CON_ADC		(IEC958_AES1_CON_ADC_ID|0x00)
#define IEC958_AES1_CON_ADC_OTHER	(IEC958_AES1_CON_ADC_ID|0x60)
#define IEC958_AES1_CON_ADC_COPYRIGHT_MASK 0x1f
#define IEC958_AES1_CON_ADC_COPYRIGHT_ID 0x16
#define IEC958_AES1_CON_ADC_COPYRIGHT	(IEC958_AES1_CON_ADC_COPYRIGHT_ID|0x00)
#define IEC958_AES1_CON_ADC_COPYRIGHT_OTHER (IEC958_AES1_CON_ADC_COPYRIGHT_ID|0x60)
#define IEC958_AES1_CON_SOLIDMEM_MASK	0x0f
#define IEC958_AES1_CON_SOLIDMEM_ID	0x08
#define IEC958_AES1_CON_SOLIDMEM_DIGITAL_RECORDER_PLAYER (IEC958_AES1_CON_SOLIDMEM_ID|0x00)
#define IEC958_AES1_CON_SOLIDMEM_OTHER	(IEC958_AES1_CON_SOLIDMEM_ID|0x70)
#define IEC958_AES1_CON_EXPERIMENTAL	0x40
#define IEC958_AES1_CON_ORIGINAL	(1<<7)	/* this bits depends on the category code */
#define IEC958_AES2_PRO_SBITS		(7<<0)	/* mask - sample bits */
#define IEC958_AES2_PRO_SBITS_20	(2<<0)	/* 20-bit - coordination */
#define IEC958_AES2_PRO_SBITS_24	(4<<0)	/* 24-bit - main audio */
#define IEC958_AES2_PRO_SBITS_UDEF	(6<<0)	/* user defined application */
#define IEC958_AES2_PRO_WORDLEN		(7<<3)	/* mask - source word length */
#define IEC958_AES2_PRO_WORDLEN_NOTID	(0<<3)	/* not indicated */
#define IEC958_AES2_PRO_WORDLEN_22_18	(2<<3)	/* 22-bit or 18-bit */
#define IEC958_AES2_PRO_WORDLEN_23_19	(4<<3)	/* 23-bit or 19-bit */
#define IEC958_AES2_PRO_WORDLEN_24_20	(5<<3)	/* 24-bit or 20-bit */
#define IEC958_AES2_PRO_WORDLEN_20_16	(6<<3)	/* 20-bit or 16-bit */
#define IEC958_AES2_CON_SOURCE		(15<<0)	/* mask - source number */
#define IEC958_AES2_CON_SOURCE_UNSPEC	(0<<0)	/* unspecified */
#define IEC958_AES2_CON_CHANNEL		(15<<4)	/* mask - channel number */
#define IEC958_AES2_CON_CHANNEL_UNSPEC	(0<<4)	/* unspecified */
#define IEC958_AES3_CON_FS		(15<<0)	/* mask - sample frequency */
#define IEC958_AES3_CON_FS_44100	(0<<0)	/* 44.1kHz */
#define IEC958_AES3_CON_FS_NOTID	(1<<0)	/* non indicated */
#define IEC958_AES3_CON_FS_48000	(2<<0)	/* 48kHz */
#define IEC958_AES3_CON_FS_32000	(3<<0)	/* 32kHz */
#define IEC958_AES3_CON_FS_22050	(4<<0)	/* 22.05kHz */
#define IEC958_AES3_CON_FS_24000	(6<<0)	/* 24kHz */
#define IEC958_AES3_CON_FS_88200	(8<<0)	/* 88.2kHz */
#define IEC958_AES3_CON_FS_768000	(9<<0)	/* 768kHz */
#define IEC958_AES3_CON_FS_96000	(10<<0)	/* 96kHz */
#define IEC958_AES3_CON_FS_176400	(12<<0)	/* 176.4kHz */
#define IEC958_AES3_CON_FS_192000	(14<<0)	/* 192kHz */
#define IEC958_AES3_CON_CLOCK		(3<<4)	/* mask - clock accuracy */
#define IEC958_AES3_CON_CLOCK_1000PPM	(0<<4)	/* 1000 ppm */
#define IEC958_AES3_CON_CLOCK_50PPM	(1<<4)	/* 50 ppm */
#define IEC958_AES3_CON_CLOCK_VARIABLE	(2<<4)	/* variable pitch */
#define IEC958_AES4_CON_MAX_WORDLEN_24	(1<<0)	/* 0 = 20-bit, 1 = 24-bit */
#define IEC958_AES4_CON_WORDLEN		(7<<1)	/* mask - sample word length */
#define IEC958_AES4_CON_WORDLEN_NOTID	(0<<1)	/* not indicated */
#define IEC958_AES4_CON_WORDLEN_20_16	(1<<1)	/* 20-bit or 16-bit */
#define IEC958_AES4_CON_WORDLEN_22_18	(2<<1)	/* 22-bit or 18-bit */
#define IEC958_AES4_CON_WORDLEN_23_19	(4<<1)	/* 23-bit or 19-bit */
#define IEC958_AES4_CON_WORDLEN_24_20	(5<<1)	/* 24-bit or 20-bit */
#define IEC958_AES4_CON_WORDLEN_21_17	(6<<1)	/* 21-bit or 17-bit */
#define IEC958_AES4_CON_ORIGFS		(15<<4)	/* mask - original sample frequency */
#define IEC958_AES4_CON_ORIGFS_NOTID	(0<<4)	/* not indicated */
#define IEC958_AES4_CON_ORIGFS_192000	(1<<4)	/* 192kHz */
#define IEC958_AES4_CON_ORIGFS_12000	(2<<4)	/* 12kHz */
#define IEC958_AES4_CON_ORIGFS_176400	(3<<4)	/* 176.4kHz */
#define IEC958_AES4_CON_ORIGFS_96000	(5<<4)	/* 96kHz */
#define IEC958_AES4_CON_ORIGFS_8000	(6<<4)	/* 8kHz */
#define IEC958_AES4_CON_ORIGFS_88200	(7<<4)	/* 88.2kHz */
#define IEC958_AES4_CON_ORIGFS_16000	(8<<4)	/* 16kHz */
#define IEC958_AES4_CON_ORIGFS_24000	(9<<4)	/* 24kHz */
#define IEC958_AES4_CON_ORIGFS_11025	(10<<4)	/* 11.025kHz */
#define IEC958_AES4_CON_ORIGFS_22050	(11<<4)	/* 22.05kHz */
#define IEC958_AES4_CON_ORIGFS_32000	(12<<4)	/* 32kHz */
#define IEC958_AES4_CON_ORIGFS_48000	(13<<4)	/* 48kHz */
#define IEC958_AES4_CON_ORIGFS_44100	(15<<4)	/* 44.1kHz */
#define IEC958_AES5_CON_CGMSA		(3<<0)	/* mask - CGMS-A */
#define IEC958_AES5_CON_CGMSA_COPYFREELY (0<<0)	/* copying is permitted without restriction */
#define IEC958_AES5_CON_CGMSA_COPYONCE	(1<<0)	/* one generation of copies may be made */
#define IEC958_AES5_CON_CGMSA_COPYNOMORE (2<<0)	/* condition not be used */
#define IEC958_AES5_CON_CGMSA_COPYNEVER	(3<<0)	/* no copying is permitted */

/****************************************************************************
 *                                                                          *
 *        CEA-861 Audio InfoFrame. Used in HDMI and DisplayPort		    *
 *                                                                          *
 ****************************************************************************/
#define CEA861_AUDIO_INFOFRAME_DB1CC		(7<<0) /* mask - channel count */
#define CEA861_AUDIO_INFOFRAME_DB1CT		(0xf<<4) /* mask - coding type */
#define CEA861_AUDIO_INFOFRAME_DB1CT_FROM_STREAM (0<<4) /* refer to stream */
#define CEA861_AUDIO_INFOFRAME_DB1CT_IEC60958	(1<<4) /* IEC-60958 L-PCM */
#define CEA861_AUDIO_INFOFRAME_DB1CT_AC3	(2<<4) /* AC-3 */
#define CEA861_AUDIO_INFOFRAME_DB1CT_MPEG1	(3<<4) /* MPEG1 Layers 1 & 2 */
#define CEA861_AUDIO_INFOFRAME_DB1CT_MP3	(4<<4) /* MPEG1 Layer 3 */
#define CEA861_AUDIO_INFOFRAME_DB1CT_MPEG2_MULTICH (5<<4) /* MPEG2 Multichannel */
#define CEA861_AUDIO_INFOFRAME_DB1CT_AAC	(6<<4) /* AAC */
#define CEA861_AUDIO_INFOFRAME_DB1CT_DTS	(7<<4) /* DTS */
#define CEA861_AUDIO_INFOFRAME_DB1CT_ATRAC	(8<<4) /* ATRAC */
#define CEA861_AUDIO_INFOFRAME_DB1CT_ONEBIT	(9<<4) /* One Bit Audio */
#define CEA861_AUDIO_INFOFRAME_DB1CT_DOLBY_DIG_PLUS (10<<4) /* Dolby Digital + */
#define CEA861_AUDIO_INFOFRAME_DB1CT_DTS_HD	(11<<4) /* DTS-HD */
#define CEA861_AUDIO_INFOFRAME_DB1CT_MAT	(12<<4) /* MAT (MLP) */
#define CEA861_AUDIO_INFOFRAME_DB1CT_DST	(13<<4) /* DST */
#define CEA861_AUDIO_INFOFRAME_DB1CT_WMA_PRO	(14<<4) /* WMA Pro */
#define CEA861_AUDIO_INFOFRAME_DB2SF		(7<<2) /* mask - sample frequency */
#define CEA861_AUDIO_INFOFRAME_DB2SF_FROM_STREAM (0<<2) /* refer to stream */
#define CEA861_AUDIO_INFOFRAME_DB2SF_32000	(1<<2) /* 32kHz */
#define CEA861_AUDIO_INFOFRAME_DB2SF_44100	(2<<2) /* 44.1kHz */
#define CEA861_AUDIO_INFOFRAME_DB2SF_48000	(3<<2) /* 48kHz */
#define CEA861_AUDIO_INFOFRAME_DB2SF_88200	(4<<2) /* 88.2kHz */
#define CEA861_AUDIO_INFOFRAME_DB2SF_96000	(5<<2) /* 96kHz */
#define CEA861_AUDIO_INFOFRAME_DB2SF_176400	(6<<2) /* 176.4kHz */
#define CEA861_AUDIO_INFOFRAME_DB2SF_192000	(7<<2) /* 192kHz */
#define CEA861_AUDIO_INFOFRAME_DB2SS		(3<<0) /* mask - sample size */
#define CEA861_AUDIO_INFOFRAME_DB2SS_FROM_STREAM (0<<0) /* refer to stream */
#define CEA861_AUDIO_INFOFRAME_DB2SS_16BIT	(1<<0) /* 16 bits */
#define CEA861_AUDIO_INFOFRAME_DB2SS_20BIT	(2<<0) /* 20 bits */
#define CEA861_AUDIO_INFOFRAME_DB2SS_24BIT	(3<<0) /* 24 bits */
#define CEA861_AUDIO_INFOFRAME_DB5_DM_INH	(1<<7) /* mask - inhibit downmixing */
#define CEA861_AUDIO_INFOFRAME_DB5_DM_INH_PERMITTED (0<<7) /* stereo downmix permitted */
#define CEA861_AUDIO_INFOFRAME_DB5_DM_INH_PROHIBITED (1<<7) /* stereo downmis prohibited */
#define CEA861_AUDIO_INFOFRAME_DB5_LSV		(0xf<<3) /* mask - level-shift values */

/*****************************************************************************
 *                                                                           *
 *                            MIDI v1.0 interface                            *
 *                                                                           *
 *****************************************************************************/

#define MIDI_CHANNELS			16
#define MIDI_GM_DRUM_CHANNEL		(10-1)

/*
 *  MIDI commands
 */

#define MIDI_CMD_NOTE_OFF		0x80
#define MIDI_CMD_NOTE_ON		0x90
#define MIDI_CMD_NOTE_PRESSURE		0xa0
#define MIDI_CMD_CONTROL		0xb0
#define MIDI_CMD_PGM_CHANGE		0xc0
#define MIDI_CMD_CHANNEL_PRESSURE	0xd0
#define MIDI_CMD_BENDER			0xe0

#define MIDI_CMD_COMMON_SYSEX		0xf0
#define MIDI_CMD_COMMON_MTC_QUARTER	0xf1
#define MIDI_CMD_COMMON_SONG_POS	0xf2
#define MIDI_CMD_COMMON_SONG_SELECT	0xf3
#define MIDI_CMD_COMMON_TUNE_REQUEST	0xf6
#define MIDI_CMD_COMMON_SYSEX_END	0xf7
#define MIDI_CMD_COMMON_CLOCK		0xf8
#define MIDI_CMD_COMMON_START		0xfa
#define MIDI_CMD_COMMON_CONTINUE	0xfb
#define MIDI_CMD_COMMON_STOP		0xfc
#define MIDI_CMD_COMMON_SENSING		0xfe
#define MIDI_CMD_COMMON_RESET		0xff

/*
 *  MIDI controllers
 */

#define MIDI_CTL_MSB_BANK		0x00
#define MIDI_CTL_MSB_MODWHEEL         	0x01
#define MIDI_CTL_MSB_BREATH           	0x02
#define MIDI_CTL_MSB_FOOT             	0x04
#define MIDI_CTL_MSB_PORTAMENTO_TIME 	0x05
#define MIDI_CTL_MSB_DATA_ENTRY		0x06
#define MIDI_CTL_MSB_MAIN_VOLUME      	0x07
#define MIDI_CTL_MSB_BALANCE          	0x08
#define MIDI_CTL_MSB_PAN              	0x0a
#define MIDI_CTL_MSB_EXPRESSION       	0x0b
#define MIDI_CTL_MSB_EFFECT1		0x0c
#define MIDI_CTL_MSB_EFFECT2		0x0d
#define MIDI_CTL_MSB_GENERAL_PURPOSE1 	0x10
#define MIDI_CTL_MSB_GENERAL_PURPOSE2 	0x11
#define MIDI_CTL_MSB_GENERAL_PURPOSE3 	0x12
#define MIDI_CTL_MSB_GENERAL_PURPOSE4 	0x13
#define MIDI_CTL_LSB_BANK		0x20
#define MIDI_CTL_LSB_MODWHEEL        	0x21
#define MIDI_CTL_LSB_BREATH           	0x22
#define MIDI_CTL_LSB_FOOT             	0x24
#define MIDI_CTL_LSB_PORTAMENTO_TIME 	0x25
#define MIDI_CTL_LSB_DATA_ENTRY		0x26
#define MIDI_CTL_LSB_MAIN_VOLUME      	0x27
#define MIDI_CTL_LSB_BALANCE          	0x28
#define MIDI_CTL_LSB_PAN              	0x2a
#define MIDI_CTL_LSB_EXPRESSION       	0x2b
#define MIDI_CTL_LSB_EFFECT1		0x2c
#define MIDI_CTL_LSB_EFFECT2		0x2d
#define MIDI_CTL_LSB_GENERAL_PURPOSE1 	0x30
#define MIDI_CTL_LSB_GENERAL_PURPOSE2 	0x31
#define MIDI_CTL_LSB_GENERAL_PURPOSE3 	0x32
#define MIDI_CTL_LSB_GENERAL_PURPOSE4 	0x33
#define MIDI_CTL_SUSTAIN              	0x40
#define MIDI_CTL_PORTAMENTO           	0x41
#define MIDI_CTL_SOSTENUTO            	0x42
#define MIDI_CTL_SOFT_PEDAL           	0x43
#define MIDI_CTL_LEGATO_FOOTSWITCH	0x44
#define MIDI_CTL_HOLD2                	0x45
#define MIDI_CTL_SC1_SOUND_VARIATION	0x46
#define MIDI_CTL_SC2_TIMBRE		0x47
#define MIDI_CTL_SC3_RELEASE_TIME	0x48
#define MIDI_CTL_SC4_ATTACK_TIME	0x49
#define MIDI_CTL_SC5_BRIGHTNESS		0x4a
#define MIDI_CTL_SC6			0x4b
#define MIDI_CTL_SC7			0x4c
#define MIDI_CTL_SC8			0x4d
#define MIDI_CTL_SC9			0x4e
#define MIDI_CTL_SC10			0x4f
#define MIDI_CTL_GENERAL_PURPOSE5     	0x50
#define MIDI_CTL_GENERAL_PURPOSE6     	0x51
#define MIDI_CTL_GENERAL_PURPOSE7     	0x52
#define MIDI_CTL_GENERAL_PURPOSE8     	0x53
#define MIDI_CTL_PORTAMENTO_CONTROL	0x54
#define MIDI_CTL_E1_REVERB_DEPTH	0x5b
#define MIDI_CTL_E2_TREMOLO_DEPTH	0x5c
#define MIDI_CTL_E3_CHORUS_DEPTH	0x5d
#define MIDI_CTL_E4_DETUNE_DEPTH	0x5e
#define MIDI_CTL_E5_PHASER_DEPTH	0x5f
#define MIDI_CTL_DATA_INCREMENT       	0x60
#define MIDI_CTL_DATA_DECREMENT       	0x61
#define MIDI_CTL_NONREG_PARM_NUM_LSB  	0x62
#define MIDI_CTL_NONREG_PARM_NUM_MSB  	0x63
#define MIDI_CTL_REGIST_PARM_NUM_LSB  	0x64
#define MIDI_CTL_REGIST_PARM_NUM_MSB	0x65
#define MIDI_CTL_ALL_SOUNDS_OFF		0x78
#define MIDI_CTL_RESET_CONTROLLERS	0x79
#define MIDI_CTL_LOCAL_CONTROL_SWITCH	0x7a
#define MIDI_CTL_ALL_NOTES_OFF		0x7b
#define MIDI_CTL_OMNI_OFF		0x7c
#define MIDI_CTL_OMNI_ON		0x7d
#define MIDI_CTL_MONO1			0x7e
#define MIDI_CTL_MONO2			0x7f

#endif /* __SOUND_ASOUNDEF_H */
