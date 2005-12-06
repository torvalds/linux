/*
    tuner.h - definition for different tuners

    Copyright (C) 1997 Markus Schroeder (schroedm@uni-duesseldorf.de)
    minor modifications by Ralph Metzler (rjkm@thp.uni-koeln.de)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _TUNER_H
#define _TUNER_H

#include <linux/videodev2.h>

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
#define TUNER_PHILIPS_ATSC		42
#define TUNER_PHILIPS_FM1236_MK3	43

#define TUNER_PHILIPS_4IN1		44	/* ATI TV Wonder Pro - Conexant */
/* Microtune mergeged with Temic 12/31/1999 partially financed by Alps - these may be similar to Temic */
#define TUNER_MICROTUNE_4049FM5 	45
#define TUNER_MICROTUNE_4042_FI5	46
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

#define TUNER_THOMSON_DTT7611		60	/* DViCO FusionHDTV 3 Gold-T */
#define TUNER_TENA_9533_DI		61
#define TUNER_TEA5767			62	/* Only FM Radio Tuner */
#define TUNER_PHILIPS_FMD1216ME_MK3	63

#define TUNER_LG_TDVS_H062F		64	/* DViCO FusionHDTV 5 */
#define TUNER_YMEC_TVF66T5_B_DFF	65	/* Acorp Y878F */
#define TUNER_LG_NTSC_TALN_MINI		66
#define TUNER_PHILIPS_TD1316		67

#define TUNER_PHILIPS_TUV1236D		68	/* ATI HDTV Wonder */
#define TUNER_TNF_5335MF                69	/* Sabrent Bt848   */

#define NOTUNER 0
#define PAL     1	/* PAL_BG */
#define PAL_I   2
#define NTSC    3
#define SECAM   4
#define ATSC    5
#define RADIO   6

#define NoTuner 0
#define Philips 1
#define TEMIC   2
#define Sony    3
#define Alps    4
#define LGINNOTEK 5
#define SHARP   6
#define Samsung 7
#define Microtune 8
#define HITACHI 9
#define Panasonic 10
#define TCL     11
#define THOMSON 12

#define TUNER_SET_TYPE_ADDR          _IOW('T',3,int)
#define TUNER_SET_STANDBY            _IOW('T',4,int)
#define TDA9887_SET_CONFIG           _IOW('t',5,int)

/* tv card specific */
# define TDA9887_PRESENT             (1<<0)
# define TDA9887_PORT1_INACTIVE      (1<<1)
# define TDA9887_PORT2_INACTIVE      (1<<2)
# define TDA9887_QSS                 (1<<3)
# define TDA9887_INTERCARRIER        (1<<4)
# define TDA9887_PORT1_ACTIVE        (1<<5)
# define TDA9887_PORT2_ACTIVE        (1<<6)
# define TDA9887_INTERCARRIER_NTSC   (1<<7)
/* config options */
# define TDA9887_DEEMPHASIS_MASK     (3<<16)
# define TDA9887_DEEMPHASIS_NONE     (1<<16)
# define TDA9887_DEEMPHASIS_50       (2<<16)
# define TDA9887_DEEMPHASIS_75       (3<<16)
# define TDA9887_AUTOMUTE            (1<<18)

#ifdef __KERNEL__

enum tuner_mode {
	T_UNINITIALIZED = 0,
	T_RADIO		= 1 << V4L2_TUNER_RADIO,
	T_ANALOG_TV     = 1 << V4L2_TUNER_ANALOG_TV,
	T_DIGITAL_TV    = 1 << V4L2_TUNER_DIGITAL_TV,
	T_STANDBY	= 1 << 31
};

struct tuner_setup {
	unsigned short	addr;
	unsigned int	type;
	unsigned int	mode_mask;
};

struct tuner {
	/* device */
	struct i2c_client i2c;

	unsigned int type;	/* chip type */

	unsigned int mode;
	unsigned int mode_mask;	/* Combination of allowable modes */

	unsigned int freq;	/* keep track of the current settings */
	unsigned int audmode;
	v4l2_std_id  std;

	int          using_v4l2;

	/* used by MT2032 */
	unsigned int xogc;
	unsigned int radio_if2;

	/* used by tda8290 */
	unsigned char tda8290_easy_mode;
	unsigned char tda827x_lpsel;
	unsigned char tda827x_addr;
	unsigned char tda827x_ver;
	unsigned int sgIF;

	/* function ptrs */
	void (*tv_freq)(struct i2c_client *c, unsigned int freq);
	void (*radio_freq)(struct i2c_client *c, unsigned int freq);
	int  (*has_signal)(struct i2c_client *c);
	int  (*is_stereo)(struct i2c_client *c);
	void (*standby)(struct i2c_client *c);
};

extern unsigned int tuner_debug;
extern unsigned const int tuner_count;

extern int microtune_init(struct i2c_client *c);
extern int tda8290_init(struct i2c_client *c);
extern int tda8290_probe(struct i2c_client *c);
extern int tea5767_tuner_init(struct i2c_client *c);
extern int default_tuner_init(struct i2c_client *c);
extern int tea5767_autodetection(struct i2c_client *c);

#define tuner_warn(fmt, arg...) do {\
	printk(KERN_WARNING "%s %d-%04x: " fmt, t->i2c.driver->name, \
			t->i2c.adapter->nr, t->i2c.addr , ##arg); } while (0)
#define tuner_info(fmt, arg...) do {\
	printk(KERN_INFO "%s %d-%04x: " fmt, t->i2c.driver->name, \
			t->i2c.adapter->nr, t->i2c.addr , ##arg); } while (0)
#define tuner_dbg(fmt, arg...) do {\
	if (tuner_debug) \
		printk(KERN_DEBUG "%s %d-%04x: " fmt, t->i2c.driver->name, \
			t->i2c.adapter->nr, t->i2c.addr , ##arg); } while (0)

#endif /* __KERNEL__ */

#endif /* _TUNER_H */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
