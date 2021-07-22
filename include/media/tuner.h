/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tuner.h - definition for different tuners
 *
 * Copyright (C) 1997 Markus Schroeder (schroedm@uni-duesseldorf.de)
 * minor modifications by Ralph Metzler (rjkm@thp.uni-koeln.de)
 */

#ifndef _TUNER_H
#define _TUNER_H
#ifdef __KERNEL__

#include <linux/videodev2.h>
#include <media/v4l2-mc.h>

#define ADDR_UNSET (255)

#define TUNER_TEMIC_PAL			0        /* 4002 FH5 (3X 7756, 9483) */
#define TUNER_PHILIPS_PAL_I		1
#define TUNER_PHILIPS_NTSC		2
#define TUNER_PHILIPS_SECAM		3	/* you must actively select B/G, L, L` */

#define TUNER_ABSENT			4
#define TUNER_PHILIPS_PAL		5
#define TUNER_TEMIC_NTSC		6	/* 4032 FY5 (3X 7004, 9498, 9789)  */
#define TUNER_TEMIC_PAL_I		7	/* 4062 FY5 (3X 8501, 9957) */

#define TUNER_TEMIC_4036FY5_NTSC	8	/* 4036 FY5 (3X 1223, 1981, 7686) */
#define TUNER_ALPS_TSBH1_NTSC		9
#define TUNER_ALPS_TSBE1_PAL		10
#define TUNER_ALPS_TSBB5_PAL_I		11

#define TUNER_ALPS_TSBE5_PAL		12
#define TUNER_ALPS_TSBC5_PAL		13
#define TUNER_TEMIC_4006FH5_PAL		14	/* 4006 FH5 (3X 9500, 9501, 7291) */
#define TUNER_ALPS_TSHC6_NTSC		15

#define TUNER_TEMIC_PAL_DK		16	/* 4016 FY5 (3X 1392, 1393) */
#define TUNER_PHILIPS_NTSC_M		17
#define TUNER_TEMIC_4066FY5_PAL_I	18	/* 4066 FY5 (3X 7032, 7035) */
#define TUNER_TEMIC_4006FN5_MULTI_PAL	19	/* B/G, I and D/K autodetected (3X 7595, 7606, 7657) */

#define TUNER_TEMIC_4009FR5_PAL		20	/* incl. FM radio (3X 7607, 7488, 7711) */
#define TUNER_TEMIC_4039FR5_NTSC	21	/* incl. FM radio (3X 7246, 7578, 7732) */
#define TUNER_TEMIC_4046FM5		22	/* you must actively select B/G, D/K, I, L, L` !  (3X 7804, 7806, 8103, 8104) */
#define TUNER_PHILIPS_PAL_DK		23

#define TUNER_PHILIPS_FQ1216ME		24	/* you must actively select B/G/D/K, I, L, L` */
#define TUNER_LG_PAL_I_FM		25
#define TUNER_LG_PAL_I			26
#define TUNER_LG_NTSC_FM		27

#define TUNER_LG_PAL_FM			28
#define TUNER_LG_PAL			29
#define TUNER_TEMIC_4009FN5_MULTI_PAL_FM 30	/* B/G, I and D/K autodetected (3X 8155, 8160, 8163) */
#define TUNER_SHARP_2U5JF5540_NTSC	31

#define TUNER_Samsung_PAL_TCPM9091PD27	32
#define TUNER_MT2032			33
#define TUNER_TEMIC_4106FH5		34	/* 4106 FH5 (3X 7808, 7865) */
#define TUNER_TEMIC_4012FY5		35	/* 4012 FY5 (3X 0971, 1099) */

#define TUNER_TEMIC_4136FY5		36	/* 4136 FY5 (3X 7708, 7746) */
#define TUNER_LG_PAL_NEW_TAPC		37
#define TUNER_PHILIPS_FM1216ME_MK3	38
#define TUNER_LG_NTSC_NEW_TAPC		39

#define TUNER_HITACHI_NTSC		40
#define TUNER_PHILIPS_PAL_MK		41
#define TUNER_PHILIPS_FCV1236D		42
#define TUNER_PHILIPS_FM1236_MK3	43

#define TUNER_PHILIPS_4IN1		44	/* ATI TV Wonder Pro - Conexant */
	/*
	 * Microtune merged with Temic 12/31/1999 partially financed by Alps.
	 * these may be similar to Temic
	 */
#define TUNER_MICROTUNE_4049FM5		45
#define TUNER_PANASONIC_VP27		46
#define TUNER_LG_NTSC_TAPE		47

#define TUNER_TNF_8831BGFF		48
#define TUNER_MICROTUNE_4042FI5		49	/* DViCO FusionHDTV 3 Gold-Q - 4042 FI5 (3X 8147) */
#define TUNER_TCL_2002N			50
#define TUNER_PHILIPS_FM1256_IH3	51

#define TUNER_THOMSON_DTT7610		52
#define TUNER_PHILIPS_FQ1286		53
#define TUNER_PHILIPS_TDA8290		54
#define TUNER_TCL_2002MB		55	/* Hauppauge PVR-150 PAL */

#define TUNER_PHILIPS_FQ1216AME_MK4	56	/* Hauppauge PVR-150 PAL */
#define TUNER_PHILIPS_FQ1236A_MK4	57	/* Hauppauge PVR-500MCE NTSC */
#define TUNER_YMEC_TVF_8531MF		58
#define TUNER_YMEC_TVF_5533MF		59	/* Pixelview Pro Ultra NTSC */

#define TUNER_THOMSON_DTT761X		60	/* DTT 7611 7611A 7612 7613 7613A 7614 7615 7615A */
#define TUNER_TENA_9533_DI		61
#define TUNER_TEA5767			62	/* Only FM Radio Tuner */
#define TUNER_PHILIPS_FMD1216ME_MK3	63

#define TUNER_LG_TDVS_H06XF		64	/* TDVS H061F, H062F, H064F */
#define TUNER_YMEC_TVF66T5_B_DFF	65	/* Acorp Y878F */
#define TUNER_LG_TALN			66
#define TUNER_PHILIPS_TD1316		67

#define TUNER_PHILIPS_TUV1236D		68	/* ATI HDTV Wonder */
#define TUNER_TNF_5335MF                69	/* Sabrent Bt848   */
#define TUNER_SAMSUNG_TCPN_2121P30A     70	/* Hauppauge PVR-500MCE NTSC */
#define TUNER_XC2028			71

#define TUNER_THOMSON_FE6600		72	/* DViCO FusionHDTV DVB-T Hybrid */
#define TUNER_SAMSUNG_TCPG_6121P30A     73	/* Hauppauge PVR-500 PAL */
#define TUNER_TDA9887                   74      /* This tuner should be used only internally */
#define TUNER_TEA5761			75	/* Only FM Radio Tuner */
#define TUNER_XC5000			76	/* Xceive Silicon Tuner */
#define TUNER_TCL_MF02GIP_5N		77	/* TCL MF02GIP_5N */
#define TUNER_PHILIPS_FMD1216MEX_MK3	78
#define TUNER_PHILIPS_FM1216MK5		79
#define TUNER_PHILIPS_FQ1216LME_MK3	80	/* Active loopthrough, no FM */

#define TUNER_PARTSNIC_PTI_5NF05	81
#define TUNER_PHILIPS_CU1216L           82
#define TUNER_NXP_TDA18271		83
#define TUNER_SONY_BTF_PXN01Z		84
#define TUNER_PHILIPS_FQ1236_MK5	85	/* NTSC, TDA9885, no FM radio */
#define TUNER_TENA_TNF_5337		86

#define TUNER_XC4000			87	/* Xceive Silicon Tuner */
#define TUNER_XC5000C			88	/* Xceive Silicon Tuner */

#define TUNER_SONY_BTF_PG472Z		89	/* PAL+SECAM */
#define TUNER_SONY_BTF_PK467Z		90	/* NTSC_JP */
#define TUNER_SONY_BTF_PB463Z		91	/* NTSC */

/* tv card specific */
#define TDA9887_PRESENT			(1<<0)
#define TDA9887_PORT1_INACTIVE		(1<<1)
#define TDA9887_PORT2_INACTIVE		(1<<2)
#define TDA9887_QSS			(1<<3)
#define TDA9887_INTERCARRIER		(1<<4)
#define TDA9887_PORT1_ACTIVE		(1<<5)
#define TDA9887_PORT2_ACTIVE		(1<<6)
#define TDA9887_INTERCARRIER_NTSC	(1<<7)
/* Tuner takeover point adjustment, in dB, -16 <= top <= 15 */
#define TDA9887_TOP_MASK		(0x3f << 8)
#define TDA9887_TOP_SET			(1 << 13)
#define TDA9887_TOP(top)		(TDA9887_TOP_SET | \
					 (((16 + (top)) & 0x1f) << 8))

/* config options */
#define TDA9887_DEEMPHASIS_MASK		(3<<16)
#define TDA9887_DEEMPHASIS_NONE		(1<<16)
#define TDA9887_DEEMPHASIS_50		(2<<16)
#define TDA9887_DEEMPHASIS_75		(3<<16)
#define TDA9887_AUTOMUTE		(1<<18)
#define TDA9887_GATING_18		(1<<19)
#define TDA9887_GAIN_NORMAL		(1<<20)
#define TDA9887_RIF_41_3		(1<<21)  /* radio IF1 41.3 vs 33.3 */

/**
 * enum tuner_mode      - Mode of the tuner
 *
 * @T_RADIO:        Tuner core will work in radio mode
 * @T_ANALOG_TV:    Tuner core will work in analog TV mode
 *
 * Older boards only had a single tuner device, but some devices have a
 * separate tuner for radio. In any case, the tuner-core needs to know if
 * the tuner chip(s) will be used in radio mode or analog TV mode, as, on
 * radio mode, frequencies are specified on a different range than on TV
 * mode. This enum is used by the tuner core in order to work with the
 * proper tuner range and eventually use a different tuner chip while in
 * radio mode.
 */
enum tuner_mode {
	T_RADIO		= 1 << V4L2_TUNER_RADIO,
	T_ANALOG_TV     = 1 << V4L2_TUNER_ANALOG_TV,
	/* Don't map V4L2_TUNER_DIGITAL_TV, as tuner-core won't use it */
};

/**
 * struct tuner_setup   - setup the tuner chipsets
 *
 * @addr:		I2C address used to control the tuner device/chipset
 * @type:		Type of the tuner, as defined at the TUNER_* macros.
 *			Each different tuner model should have an unique
 *			identifier.
 * @mode_mask:		Mask with the allowed tuner modes: V4L2_TUNER_RADIO,
 *			V4L2_TUNER_ANALOG_TV and/or V4L2_TUNER_DIGITAL_TV,
 *			describing if the tuner should be used to support
 *			Radio, analog TV and/or digital TV.
 * @config:		Used to send tuner-specific configuration for complex
 *			tuners that require extra parameters to be set.
 *			Only a very few tuners require it and its usage on
 *			newer tuners should be avoided.
 * @tuner_callback:	Some tuners require to call back the bridge driver,
 *			in order to do some tasks like rising a GPIO at the
 *			bridge chipset, in order to do things like resetting
 *			the device.
 *
 * Older boards only had a single tuner device. Nowadays multiple tuner
 * devices may be present on a single board. Using TUNER_SET_TYPE_ADDR
 * to pass the tuner_setup structure it is possible to setup each tuner
 * device in turn.
 *
 * Since multiple devices may be present it is no longer sufficient to
 * send a command to a single i2c device. Instead you should broadcast
 * the command to all i2c devices.
 *
 * By setting the mode_mask correctly you can select which commands are
 * accepted by a specific tuner device. For example, set mode_mask to
 * T_RADIO if the device is a radio-only tuner. That specific tuner will
 * only accept commands when the tuner is in radio mode and ignore them
 * when the tuner is set to TV mode.
 */

struct tuner_setup {
	unsigned short	addr;
	unsigned int	type;
	unsigned int	mode_mask;
	void		*config;
	int (*tuner_callback)(void *dev, int component, int cmd, int arg);
};

#endif /* __KERNEL__ */

#endif /* _TUNER_H */
