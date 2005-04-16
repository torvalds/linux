/*
 *
 * Copyright (c) 2003 by Karsten Wiese <annabellesgarden@yahoo.de>
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
 */

enum E_In84{
	eFader0 = 0,
	eFader1,
	eFader2,
	eFader3,
	eFader4,
	eFader5,
	eFader6,
	eFader7,
	eFaderM,
	eTransport,
	eModifier = 10,
	eFilterSelect,
	eSelect,
	eMute,

	eSwitch   = 15,
	eWheelGain,
	eWheelFreq,
	eWheelQ,
	eWheelPan,
	eWheel    = 20
};

#define T_RECORD   1
#define T_PLAY     2
#define T_STOP     4
#define T_F_FWD    8
#define T_REW   0x10
#define T_SOLO  0x20
#define T_REC   0x40
#define T_NULL  0x80


struct us428_ctls{
	unsigned char   Fader[9];
	unsigned char 	Transport;
	unsigned char 	Modifier;
	unsigned char 	FilterSelect;
	unsigned char 	Select;
	unsigned char   Mute;
	unsigned char   UNKNOWN;
	unsigned char   Switch;	     
	unsigned char   Wheel[5];
};

typedef struct us428_ctls us428_ctls_t;

typedef struct us428_setByte{
	unsigned char Offset,
		Value;
}us428_setByte_t;

enum {
	eLT_Volume = 0,
	eLT_Light
};

typedef struct usX2Y_volume {
	unsigned char Channel,
		LH,
		LL,
		RH,
		RL;
} usX2Y_volume_t;

struct us428_lights{
	us428_setByte_t Light[7];
};
typedef struct us428_lights us428_lights_t;

typedef struct {
	char type;
	union {
		usX2Y_volume_t	vol;
		us428_lights_t  lights;
	} val;
} us428_p4out_t;

#define N_us428_ctl_BUFS 16
#define N_us428_p4out_BUFS 16
struct us428ctls_sharedmem{
	us428_ctls_t	CtlSnapShot[N_us428_ctl_BUFS];
	int		CtlSnapShotDiffersAt[N_us428_ctl_BUFS];
	int		CtlSnapShotLast, CtlSnapShotRed;
	us428_p4out_t	p4out[N_us428_p4out_BUFS];
	int		p4outLast, p4outSent;
};
typedef struct us428ctls_sharedmem us428ctls_sharedmem_t;
