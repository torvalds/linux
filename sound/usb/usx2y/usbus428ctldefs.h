/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * Copyright (c) 2003 by Karsten Wiese <annabellesgarden@yahoo.de>
 */

enum E_IN84 {
	E_FADER_0 = 0,
	E_FADER_1,
	E_FADER_2,
	E_FADER_3,
	E_FADER_4,
	E_FADER_5,
	E_FADER_6,
	E_FADER_7,
	E_FADER_M,
	E_TRANSPORT,
	E_MODIFIER = 10,
	E_FILTER_SELECT,
	E_SELECT,
	E_MUTE,

	E_SWITCH   = 15,
	E_WHEEL_GAIN,
	E_WHEEL_FREQ,
	E_WHEEL_Q,
	E_WHEEL_PAN,
	E_WHEEL    = 20
};

#define T_RECORD   1
#define T_PLAY     2
#define T_STOP     4
#define T_F_FWD    8
#define T_REW   0x10
#define T_SOLO  0x20
#define T_REC   0x40
#define T_NULL  0x80


struct us428_ctls {
	unsigned char   fader[9];
	unsigned char 	transport;
	unsigned char 	modifier;
	unsigned char 	filters_elect;
	unsigned char 	select;
	unsigned char   mute;
	unsigned char   unknown;
	unsigned char   wswitch;	     
	unsigned char   wheel[5];
};

struct us428_set_byte {
	unsigned char offset,
		value;
};

enum {
	ELT_VOLUME = 0,
	ELT_LIGHT
};

struct usx2y_volume {
	unsigned char channel,
		lh,
		ll,
		rh,
		rl;
};

struct us428_lights {
	struct us428_set_byte light[7];
};

struct us428_p4out {
	char type;
	union {
		struct usx2y_volume vol;
		struct us428_lights lights;
	} val;
};

#define N_US428_CTL_BUFS 16
#define N_US428_P4OUT_BUFS 16
struct us428ctls_sharedmem {
	struct us428_ctls	ctl_snapshot[N_US428_CTL_BUFS];
	int			ctl_snapshot_differs_at[N_US428_CTL_BUFS];
	int			ctl_snapshot_last, ctl_snapshot_red;
	struct us428_p4out	p4out[N_US428_P4OUT_BUFS];
	int			p4out_last, p4out_sent;
};
