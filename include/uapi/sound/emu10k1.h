/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>,
 *		     Creative Labs, Inc.
 *  Definitions for EMU10K1 (SB Live!) chips
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
#ifndef _UAPI__SOUND_EMU10K1_H
#define _UAPI__SOUND_EMU10K1_H

#include <linux/types.h>
#include <sound/asound.h>

/*
 * ---- FX8010 ----
 */

#define EMU10K1_CARD_CREATIVE			0x00000000
#define EMU10K1_CARD_EMUAPS			0x00000001

#define EMU10K1_FX8010_PCM_COUNT		8

/* instruction set */
#define iMAC0	 0x00	/* R = A + (X * Y >> 31)   ; saturation */
#define iMAC1	 0x01	/* R = A + (-X * Y >> 31)  ; saturation */
#define iMAC2	 0x02	/* R = A + (X * Y >> 31)   ; wraparound */
#define iMAC3	 0x03	/* R = A + (-X * Y >> 31)  ; wraparound */
#define iMACINT0 0x04	/* R = A + X * Y	   ; saturation */
#define iMACINT1 0x05	/* R = A + X * Y	   ; wraparound (31-bit) */
#define iACC3	 0x06	/* R = A + X + Y	   ; saturation */
#define iMACMV   0x07	/* R = A, acc += X * Y >> 31 */
#define iANDXOR  0x08	/* R = (A & X) ^ Y */
#define iTSTNEG  0x09	/* R = (A >= Y) ? X : ~X */
#define iLIMITGE 0x0a	/* R = (A >= Y) ? X : Y */
#define iLIMITLT 0x0b	/* R = (A < Y) ? X : Y */
#define iLOG	 0x0c	/* R = linear_data, A (log_data), X (max_exp), Y (format_word) */
#define iEXP	 0x0d	/* R = log_data, A (linear_data), X (max_exp), Y (format_word) */
#define iINTERP  0x0e	/* R = A + (X * (Y - A) >> 31)  ; saturation */
#define iSKIP    0x0f	/* R = A (cc_reg), X (count), Y (cc_test) */

/* GPRs */
#define FXBUS(x)	(0x00 + (x))	/* x = 0x00 - 0x0f */
#define EXTIN(x)	(0x10 + (x))	/* x = 0x00 - 0x0f */
#define EXTOUT(x)	(0x20 + (x))	/* x = 0x00 - 0x0f physical outs -> FXWC low 16 bits */
#define FXBUS2(x)	(0x30 + (x))	/* x = 0x00 - 0x0f copies of fx buses for capture -> FXWC high 16 bits */
					/* NB: 0x31 and 0x32 are shared with Center/LFE on SB live 5.1 */

#define C_00000000	0x40
#define C_00000001	0x41
#define C_00000002	0x42
#define C_00000003	0x43
#define C_00000004	0x44
#define C_00000008	0x45
#define C_00000010	0x46
#define C_00000020	0x47
#define C_00000100	0x48
#define C_00010000	0x49
#define C_00080000	0x4a
#define C_10000000	0x4b
#define C_20000000	0x4c
#define C_40000000	0x4d
#define C_80000000	0x4e
#define C_7fffffff	0x4f
#define C_ffffffff	0x50
#define C_fffffffe	0x51
#define C_c0000000	0x52
#define C_4f1bbcdc	0x53
#define C_5a7ef9db	0x54
#define C_00100000	0x55		/* ?? */
#define GPR_ACCU	0x56		/* ACCUM, accumulator */
#define GPR_COND	0x57		/* CCR, condition register */
#define GPR_NOISE0	0x58		/* noise source */
#define GPR_NOISE1	0x59		/* noise source */
#define GPR_IRQ		0x5a		/* IRQ register */
#define GPR_DBAC	0x5b		/* TRAM Delay Base Address Counter */
#define GPR(x)		(FXGPREGBASE + (x)) /* free GPRs: x = 0x00 - 0xff */
#define ITRAM_DATA(x)	(TANKMEMDATAREGBASE + 0x00 + (x)) /* x = 0x00 - 0x7f */
#define ETRAM_DATA(x)	(TANKMEMDATAREGBASE + 0x80 + (x)) /* x = 0x00 - 0x1f */
#define ITRAM_ADDR(x)	(TANKMEMADDRREGBASE + 0x00 + (x)) /* x = 0x00 - 0x7f */
#define ETRAM_ADDR(x)	(TANKMEMADDRREGBASE + 0x80 + (x)) /* x = 0x00 - 0x1f */

#define A_ITRAM_DATA(x)	(TANKMEMDATAREGBASE + 0x00 + (x)) /* x = 0x00 - 0xbf */
#define A_ETRAM_DATA(x)	(TANKMEMDATAREGBASE + 0xc0 + (x)) /* x = 0x00 - 0x3f */
#define A_ITRAM_ADDR(x)	(TANKMEMADDRREGBASE + 0x00 + (x)) /* x = 0x00 - 0xbf */
#define A_ETRAM_ADDR(x)	(TANKMEMADDRREGBASE + 0xc0 + (x)) /* x = 0x00 - 0x3f */
#define A_ITRAM_CTL(x)	(A_TANKMEMCTLREGBASE + 0x00 + (x)) /* x = 0x00 - 0xbf */
#define A_ETRAM_CTL(x)	(A_TANKMEMCTLREGBASE + 0xc0 + (x)) /* x = 0x00 - 0x3f */

#define A_FXBUS(x)	(0x00 + (x))	/* x = 0x00 - 0x3f FX buses */
#define A_EXTIN(x)	(0x40 + (x))	/* x = 0x00 - 0x0f physical ins */
#define A_P16VIN(x)	(0x50 + (x))	/* x = 0x00 - 0x0f p16v ins (A2 only) "EMU32 inputs" */
#define A_EXTOUT(x)	(0x60 + (x))	/* x = 0x00 - 0x1f physical outs -> A_FXWC1 0x79-7f unknown   */
#define A_FXBUS2(x)	(0x80 + (x))	/* x = 0x00 - 0x1f extra outs used for EFX capture -> A_FXWC2 */
#define A_EMU32OUTH(x)	(0xa0 + (x))	/* x = 0x00 - 0x0f "EMU32_OUT_10 - _1F" - ??? */
#define A_EMU32OUTL(x)	(0xb0 + (x))	/* x = 0x00 - 0x0f "EMU32_OUT_1 - _F" - ??? */
#define A3_EMU32IN(x)	(0x160 + (x))	/* x = 0x00 - 0x3f "EMU32_IN_00 - _3F" - Only when .device = 0x0008 */
#define A3_EMU32OUT(x)	(0x1E0 + (x))	/* x = 0x00 - 0x0f "EMU32_OUT_00 - _3F" - Only when .device = 0x0008 */
#define A_GPR(x)	(A_FXGPREGBASE + (x))

/* cc_reg constants */
#define CC_REG_NORMALIZED C_00000001
#define CC_REG_BORROW	C_00000002
#define CC_REG_MINUS	C_00000004
#define CC_REG_ZERO	C_00000008
#define CC_REG_SATURATE	C_00000010
#define CC_REG_NONZERO	C_00000100

/* FX buses */
#define FXBUS_PCM_LEFT		0x00
#define FXBUS_PCM_RIGHT		0x01
#define FXBUS_PCM_LEFT_REAR	0x02
#define FXBUS_PCM_RIGHT_REAR	0x03
#define FXBUS_MIDI_LEFT		0x04
#define FXBUS_MIDI_RIGHT	0x05
#define FXBUS_PCM_CENTER	0x06
#define FXBUS_PCM_LFE		0x07
#define FXBUS_PCM_LEFT_FRONT	0x08
#define FXBUS_PCM_RIGHT_FRONT	0x09
#define FXBUS_MIDI_REVERB	0x0c
#define FXBUS_MIDI_CHORUS	0x0d
#define FXBUS_PCM_LEFT_SIDE	0x0e
#define FXBUS_PCM_RIGHT_SIDE	0x0f
#define FXBUS_PT_LEFT		0x14
#define FXBUS_PT_RIGHT		0x15

/* Inputs */
#define EXTIN_AC97_L	   0x00	/* AC'97 capture channel - left */
#define EXTIN_AC97_R	   0x01	/* AC'97 capture channel - right */
#define EXTIN_SPDIF_CD_L   0x02	/* internal S/PDIF CD - onboard - left */
#define EXTIN_SPDIF_CD_R   0x03	/* internal S/PDIF CD - onboard - right */
#define EXTIN_ZOOM_L	   0x04	/* Zoom Video I2S - left */
#define EXTIN_ZOOM_R	   0x05	/* Zoom Video I2S - right */
#define EXTIN_TOSLINK_L	   0x06	/* LiveDrive - TOSLink Optical - left */
#define EXTIN_TOSLINK_R    0x07	/* LiveDrive - TOSLink Optical - right */
#define EXTIN_LINE1_L	   0x08	/* LiveDrive - Line/Mic 1 - left */
#define EXTIN_LINE1_R	   0x09	/* LiveDrive - Line/Mic 1 - right */
#define EXTIN_COAX_SPDIF_L 0x0a	/* LiveDrive - Coaxial S/PDIF - left */
#define EXTIN_COAX_SPDIF_R 0x0b /* LiveDrive - Coaxial S/PDIF - right */
#define EXTIN_LINE2_L	   0x0c	/* LiveDrive - Line/Mic 2 - left */
#define EXTIN_LINE2_R	   0x0d	/* LiveDrive - Line/Mic 2 - right */

/* Outputs */
#define EXTOUT_AC97_L	   0x00	/* AC'97 playback channel - left */
#define EXTOUT_AC97_R	   0x01	/* AC'97 playback channel - right */
#define EXTOUT_TOSLINK_L   0x02	/* LiveDrive - TOSLink Optical - left */
#define EXTOUT_TOSLINK_R   0x03	/* LiveDrive - TOSLink Optical - right */
#define EXTOUT_AC97_CENTER 0x04	/* SB Live 5.1 - center */
#define EXTOUT_AC97_LFE	   0x05 /* SB Live 5.1 - LFE */
#define EXTOUT_HEADPHONE_L 0x06	/* LiveDrive - Headphone - left */
#define EXTOUT_HEADPHONE_R 0x07	/* LiveDrive - Headphone - right */
#define EXTOUT_REAR_L	   0x08	/* Rear channel - left */
#define EXTOUT_REAR_R	   0x09	/* Rear channel - right */
#define EXTOUT_ADC_CAP_L   0x0a	/* ADC Capture buffer - left */
#define EXTOUT_ADC_CAP_R   0x0b	/* ADC Capture buffer - right */
#define EXTOUT_MIC_CAP	   0x0c	/* MIC Capture buffer */
#define EXTOUT_AC97_REAR_L 0x0d	/* SB Live 5.1 (c) 2003 - Rear Left */
#define EXTOUT_AC97_REAR_R 0x0e	/* SB Live 5.1 (c) 2003 - Rear Right */
#define EXTOUT_ACENTER	   0x11 /* Analog Center */
#define EXTOUT_ALFE	   0x12 /* Analog LFE */

/* Audigy Inputs */
#define A_EXTIN_AC97_L		0x00	/* AC'97 capture channel - left */
#define A_EXTIN_AC97_R		0x01	/* AC'97 capture channel - right */
#define A_EXTIN_SPDIF_CD_L	0x02	/* digital CD left */
#define A_EXTIN_SPDIF_CD_R	0x03	/* digital CD left */
#define A_EXTIN_OPT_SPDIF_L     0x04    /* audigy drive Optical SPDIF - left */
#define A_EXTIN_OPT_SPDIF_R     0x05    /*                              right */ 
#define A_EXTIN_LINE2_L		0x08	/* audigy drive line2/mic2 - left */
#define A_EXTIN_LINE2_R		0x09	/*                           right */
#define A_EXTIN_ADC_L		0x0a    /* Philips ADC - left */
#define A_EXTIN_ADC_R		0x0b    /*               right */
#define A_EXTIN_AUX2_L		0x0c	/* audigy drive aux2 - left */
#define A_EXTIN_AUX2_R		0x0d	/*                   - right */

/* Audigiy Outputs */
#define A_EXTOUT_FRONT_L	0x00	/* digital front left */
#define A_EXTOUT_FRONT_R	0x01	/*               right */
#define A_EXTOUT_CENTER		0x02	/* digital front center */
#define A_EXTOUT_LFE		0x03	/* digital front lfe */
#define A_EXTOUT_HEADPHONE_L	0x04	/* headphone audigy drive left */
#define A_EXTOUT_HEADPHONE_R	0x05	/*                        right */
#define A_EXTOUT_REAR_L		0x06	/* digital rear left */
#define A_EXTOUT_REAR_R		0x07	/*              right */
#define A_EXTOUT_AFRONT_L	0x08	/* analog front left */
#define A_EXTOUT_AFRONT_R	0x09	/*              right */
#define A_EXTOUT_ACENTER	0x0a	/* analog center */
#define A_EXTOUT_ALFE		0x0b	/* analog LFE */
#define A_EXTOUT_ASIDE_L	0x0c	/* analog side left  - Audigy 2 ZS */
#define A_EXTOUT_ASIDE_R	0x0d	/*             right - Audigy 2 ZS */
#define A_EXTOUT_AREAR_L	0x0e	/* analog rear left */
#define A_EXTOUT_AREAR_R	0x0f	/*             right */
#define A_EXTOUT_AC97_L		0x10	/* AC97 left (front) */
#define A_EXTOUT_AC97_R		0x11	/*      right */
#define A_EXTOUT_ADC_CAP_L	0x16	/* ADC capture buffer left */
#define A_EXTOUT_ADC_CAP_R	0x17	/*                    right */
#define A_EXTOUT_MIC_CAP	0x18	/* Mic capture buffer */

/* Audigy constants */
#define A_C_00000000	0xc0
#define A_C_00000001	0xc1
#define A_C_00000002	0xc2
#define A_C_00000003	0xc3
#define A_C_00000004	0xc4
#define A_C_00000008	0xc5
#define A_C_00000010	0xc6
#define A_C_00000020	0xc7
#define A_C_00000100	0xc8
#define A_C_00010000	0xc9
#define A_C_00000800	0xca
#define A_C_10000000	0xcb
#define A_C_20000000	0xcc
#define A_C_40000000	0xcd
#define A_C_80000000	0xce
#define A_C_7fffffff	0xcf
#define A_C_ffffffff	0xd0
#define A_C_fffffffe	0xd1
#define A_C_c0000000	0xd2
#define A_C_4f1bbcdc	0xd3
#define A_C_5a7ef9db	0xd4
#define A_C_00100000	0xd5
#define A_GPR_ACCU	0xd6		/* ACCUM, accumulator */
#define A_GPR_COND	0xd7		/* CCR, condition register */
#define A_GPR_NOISE0	0xd8		/* noise source */
#define A_GPR_NOISE1	0xd9		/* noise source */
#define A_GPR_IRQ	0xda		/* IRQ register */
#define A_GPR_DBAC	0xdb		/* TRAM Delay Base Address Counter - internal */
#define A_GPR_DBACE	0xde		/* TRAM Delay Base Address Counter - external */

/* definitions for debug register */
#define EMU10K1_DBG_ZC			0x80000000	/* zero tram counter */
#define EMU10K1_DBG_SATURATION_OCCURED	0x02000000	/* saturation control */
#define EMU10K1_DBG_SATURATION_ADDR	0x01ff0000	/* saturation address */
#define EMU10K1_DBG_SINGLE_STEP		0x00008000	/* single step mode */
#define EMU10K1_DBG_STEP		0x00004000	/* start single step */
#define EMU10K1_DBG_CONDITION_CODE	0x00003e00	/* condition code */
#define EMU10K1_DBG_SINGLE_STEP_ADDR	0x000001ff	/* single step address */

/* tank memory address line */
#ifndef __KERNEL__
#define TANKMEMADDRREG_ADDR_MASK 0x000fffff	/* 20 bit tank address field			*/
#define TANKMEMADDRREG_CLEAR	 0x00800000	/* Clear tank memory				*/
#define TANKMEMADDRREG_ALIGN	 0x00400000	/* Align read or write relative to tank access	*/
#define TANKMEMADDRREG_WRITE	 0x00200000	/* Write to tank memory				*/
#define TANKMEMADDRREG_READ	 0x00100000	/* Read from tank memory			*/
#endif

struct snd_emu10k1_fx8010_info {
	unsigned int internal_tram_size;	/* in samples */
	unsigned int external_tram_size;	/* in samples */
	char fxbus_names[16][32];		/* names of FXBUSes */
	char extin_names[16][32];		/* names of external inputs */
	char extout_names[32][32];		/* names of external outputs */
	unsigned int gpr_controls;		/* count of GPR controls */
};

#define EMU10K1_GPR_TRANSLATION_NONE		0
#define EMU10K1_GPR_TRANSLATION_TABLE100	1
#define EMU10K1_GPR_TRANSLATION_BASS		2
#define EMU10K1_GPR_TRANSLATION_TREBLE		3
#define EMU10K1_GPR_TRANSLATION_ONOFF		4

struct snd_emu10k1_fx8010_control_gpr {
	struct snd_ctl_elem_id id;		/* full control ID definition */
	unsigned int vcount;		/* visible count */
	unsigned int count;		/* count of GPR (1..16) */
	unsigned short gpr[32];		/* GPR number(s) */
	unsigned int value[32];		/* initial values */
	unsigned int min;		/* minimum range */
	unsigned int max;		/* maximum range */
	unsigned int translation;	/* translation type (EMU10K1_GPR_TRANSLATION*) */
	const unsigned int *tlv;
};

/* old ABI without TLV support */
struct snd_emu10k1_fx8010_control_old_gpr {
	struct snd_ctl_elem_id id;
	unsigned int vcount;
	unsigned int count;
	unsigned short gpr[32];
	unsigned int value[32];
	unsigned int min;
	unsigned int max;
	unsigned int translation;
};

struct snd_emu10k1_fx8010_code {
	char name[128];

	DECLARE_BITMAP(gpr_valid, 0x200); /* bitmask of valid initializers */
	__u32 __user *gpr_map;		/* initializers */

	unsigned int gpr_add_control_count; /* count of GPR controls to add/replace */
	struct snd_emu10k1_fx8010_control_gpr __user *gpr_add_controls; /* GPR controls to add/replace */

	unsigned int gpr_del_control_count; /* count of GPR controls to remove */
	struct snd_ctl_elem_id __user *gpr_del_controls; /* IDs of GPR controls to remove */

	unsigned int gpr_list_control_count; /* count of GPR controls to list */
	unsigned int gpr_list_control_total; /* total count of GPR controls */
	struct snd_emu10k1_fx8010_control_gpr __user *gpr_list_controls; /* listed GPR controls */

	DECLARE_BITMAP(tram_valid, 0x100); /* bitmask of valid initializers */
	__u32 __user *tram_data_map;	  /* data initializers */
	__u32 __user *tram_addr_map;	  /* map initializers */

	DECLARE_BITMAP(code_valid, 1024); /* bitmask of valid instructions */
	__u32 __user *code;		  /* one instruction - 64 bits */
};

struct snd_emu10k1_fx8010_tram {
	unsigned int address;		/* 31.bit == 1 -> external TRAM */
	unsigned int size;		/* size in samples (4 bytes) */
	unsigned int *samples;		/* pointer to samples (20-bit) */
					/* NULL->clear memory */
};

struct snd_emu10k1_fx8010_pcm_rec {
	unsigned int substream;		/* substream number */
	unsigned int res1;		/* reserved */
	unsigned int channels;		/* 16-bit channels count, zero = remove this substream */
	unsigned int tram_start;	/* ring buffer position in TRAM (in samples) */
	unsigned int buffer_size;	/* count of buffered samples */
	unsigned short gpr_size;		/* GPR containing size of ringbuffer in samples (host) */
	unsigned short gpr_ptr;		/* GPR containing current pointer in the ring buffer (host = reset, FX8010) */
	unsigned short gpr_count;	/* GPR containing count of samples between two interrupts (host) */
	unsigned short gpr_tmpcount;	/* GPR containing current count of samples to interrupt (host = set, FX8010) */
	unsigned short gpr_trigger;	/* GPR containing trigger (activate) information (host) */
	unsigned short gpr_running;	/* GPR containing info if PCM is running (FX8010) */
	unsigned char pad;		/* reserved */
	unsigned char etram[32];	/* external TRAM address & data (one per channel) */
	unsigned int res2;		/* reserved */
};

#define SNDRV_EMU10K1_VERSION		SNDRV_PROTOCOL_VERSION(1, 0, 1)

#define SNDRV_EMU10K1_IOCTL_INFO	_IOR ('H', 0x10, struct snd_emu10k1_fx8010_info)
#define SNDRV_EMU10K1_IOCTL_CODE_POKE	_IOW ('H', 0x11, struct snd_emu10k1_fx8010_code)
#define SNDRV_EMU10K1_IOCTL_CODE_PEEK	_IOWR('H', 0x12, struct snd_emu10k1_fx8010_code)
#define SNDRV_EMU10K1_IOCTL_TRAM_SETUP	_IOW ('H', 0x20, int)
#define SNDRV_EMU10K1_IOCTL_TRAM_POKE	_IOW ('H', 0x21, struct snd_emu10k1_fx8010_tram)
#define SNDRV_EMU10K1_IOCTL_TRAM_PEEK	_IOWR('H', 0x22, struct snd_emu10k1_fx8010_tram)
#define SNDRV_EMU10K1_IOCTL_PCM_POKE	_IOW ('H', 0x30, struct snd_emu10k1_fx8010_pcm_rec)
#define SNDRV_EMU10K1_IOCTL_PCM_PEEK	_IOWR('H', 0x31, struct snd_emu10k1_fx8010_pcm_rec)
#define SNDRV_EMU10K1_IOCTL_PVERSION	_IOR ('H', 0x40, int)
#define SNDRV_EMU10K1_IOCTL_STOP	_IO  ('H', 0x80)
#define SNDRV_EMU10K1_IOCTL_CONTINUE	_IO  ('H', 0x81)
#define SNDRV_EMU10K1_IOCTL_ZERO_TRAM_COUNTER _IO ('H', 0x82)
#define SNDRV_EMU10K1_IOCTL_SINGLE_STEP	_IOW ('H', 0x83, int)
#define SNDRV_EMU10K1_IOCTL_DBG_READ	_IOR ('H', 0x84, int)

/* typedefs for compatibility to user-space */
typedef struct snd_emu10k1_fx8010_info emu10k1_fx8010_info_t;
typedef struct snd_emu10k1_fx8010_control_gpr emu10k1_fx8010_control_gpr_t;
typedef struct snd_emu10k1_fx8010_code emu10k1_fx8010_code_t;
typedef struct snd_emu10k1_fx8010_tram emu10k1_fx8010_tram_t;
typedef struct snd_emu10k1_fx8010_pcm_rec emu10k1_fx8010_pcm_t;

#endif /* _UAPI__SOUND_EMU10K1_H */
