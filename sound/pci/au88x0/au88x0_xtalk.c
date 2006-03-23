/***************************************************************************
 *            au88x0_cxtalk.c
 *
 *  Wed Nov 19 16:29:47 2003
 *  Copyright  2003  mjander
 *  mjander@users.sourceforge.org
 ****************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "au88x0_xtalk.h"

/* Data (a whole lot of data.... ) */

static short const sXtalkWideKLeftEq = 0x269C;
static short const sXtalkWideKRightEq = 0x269C;
static short const sXtalkWideKLeftXt = 0xF25E;
static short const sXtalkWideKRightXt = 0xF25E;
static short const sXtalkWideShiftLeftEq = 1;
static short const sXtalkWideShiftRightEq = 1;
static short const sXtalkWideShiftLeftXt = 0;
static short const sXtalkWideShiftRightXt = 0;
static unsigned short const wXtalkWideLeftDelay = 0xd;
static unsigned short const wXtalkWideRightDelay = 0xd;
static short const sXtalkNarrowKLeftEq = 0x468D;
static short const sXtalkNarrowKRightEq = 0x468D;
static short const sXtalkNarrowKLeftXt = 0xF82E;
static short const sXtalkNarrowKRightXt = 0xF82E;
static short const sXtalkNarrowShiftLeftEq = 0x3;
static short const sXtalkNarrowShiftRightEq = 0x3;
static short const sXtalkNarrowShiftLeftXt = 0;
static short const sXtalkNarrowShiftRightXt = 0;
static unsigned short const wXtalkNarrowLeftDelay = 0x7;
static unsigned short const wXtalkNarrowRightDelay = 0x7;

static xtalk_gains_t const asXtalkGainsDefault = {
	0x4000, 0x4000, 4000, 0x4000, 4000, 0x4000, 4000, 0x4000, 4000,
	0x4000
};

static xtalk_gains_t const asXtalkGainsTest = {
	0x8000, 0x7FFF, 0, 0xFFFF, 0x0001, 0xC000, 0x4000, 0xFFFE, 0x0002,
	0
};
static xtalk_gains_t const asXtalkGains1Chan = {
	0x7FFF, 0, 0, 0, 0x7FFF, 0, 0, 0, 0, 0
};

// Input gain for 4 A3D slices. One possible input pair is left zero.
static xtalk_gains_t const asXtalkGainsAllChan = {
	0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
	0
	    //0x7FFF,0x7FFF,0x7FFF,0x7FFF,0x7fff,0x7FFF,0x7FFF,0x7FFF,0x7FFF,0x7fff
};
static xtalk_gains_t const asXtalkGainsZeros = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static xtalk_dline_t const alXtalkDlineZeros = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0,
	0, 0, 0, 0, 0, 0, 0
};
static xtalk_dline_t const alXtalkDlineTest = {
	0xFC18, 0x03E8FFFF, 0x186A0, 0x7960FFFE, 1, 0xFFFFFFFF,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
};

static xtalk_instate_t const asXtalkInStateZeros = { 0, 0, 0, 0 };
static xtalk_instate_t const asXtalkInStateTest =
    { 0xFF80, 0x0080, 0xFFFF, 0x0001 };
static xtalk_state_t const asXtalkOutStateZeros = {
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0},
	{0, 0, 0, 0}
};
static short const sDiamondKLeftEq = 0x401d;
static short const sDiamondKRightEq = 0x401d;
static short const sDiamondKLeftXt = 0xF90E;
static short const sDiamondKRightXt = 0xF90E;
static short const sDiamondShiftLeftEq = 1;	/* 0xF90E Is this a bug ??? */
static short const sDiamondShiftRightEq = 1;
static short const sDiamondShiftLeftXt = 0;
static short const sDiamondShiftRightXt = 0;
static unsigned short const wDiamondLeftDelay = 0xb;
static unsigned short const wDiamondRightDelay = 0xb;

static xtalk_coefs_t const asXtalkWideCoefsLeftEq = {
	{0xEC4C, 0xDCE9, 0xFDC2, 0xFEEC, 0},
	{0x5F60, 0xCBCB, 0xFC26, 0x0305, 0},
	{0x340B, 0xf504, 0x6CE8, 0x0D23, 0x00E4},
	{0xD500, 0x8D76, 0xACC7, 0x5B05, 0x00FA},
	{0x7F04, 0xC0FA, 0x0263, 0xFDA2, 0}
};
static xtalk_coefs_t const asXtalkWideCoefsRightEq = {
	{0xEC4C, 0xDCE9, 0xFDC2, 0xFEEC, 0},
	{0x5F60, 0xCBCB, 0xFC26, 0x0305, 0},
	{0x340B, 0xF504, 0x6CE8, 0x0D23, 0x00E4},
	{0xD500, 0x8D76, 0xACC7, 0x5B05, 0x00FA},
	{0x7F04, 0xC0FA, 0x0263, 0xFDA2, 0}
};
static xtalk_coefs_t const asXtalkWideCoefsLeftXt = {
	{0x86C3, 0x7B55, 0x89C3, 0x005B, 0x0047},
	{0x6000, 0x206A, 0xC6CA, 0x40FF, 0},
	{0x1100, 0x1164, 0xA1D7, 0x90FC, 0x0001},
	{0xDC00, 0x9E77, 0xB8C7, 0x0AFF, 0},
	{0, 0, 0, 0, 0}
};
static xtalk_coefs_t const asXtalkWideCoefsRightXt = {
	{0x86C3, 0x7B55, 0x89C3, 0x005B, 0x0047},
	{0x6000, 0x206A, 0xC6CA, 0x40FF, 0},
	{0x1100, 0x1164, 0xA1D7, 0x90FC, 0x0001},
	{0xDC00, 0x9E77, 0xB8C7, 0x0AFF, 0},
	{0, 0, 0, 0, 0}
};
static xtalk_coefs_t const asXtalkNarrowCoefsLeftEq = {
	{0x50B5, 0xD07C, 0x026D, 0xFD21, 0},
	{0x460F, 0xE44F, 0xF75E, 0xEFA6, 0},
	{0x556D, 0xDCAB, 0x2098, 0xF0F2, 0},
	{0x7E03, 0xC1F0, 0x007D, 0xFF89, 0},
	{0x383E, 0xFD9D, 0xB278, 0x4547, 0}
};

static xtalk_coefs_t const asXtalkNarrowCoefsRightEq = {
	{0x50B5, 0xD07C, 0x026D, 0xFD21, 0},
	{0x460F, 0xE44F, 0xF75E, 0xEFA6, 0},
	{0x556D, 0xDCAB, 0x2098, 0xF0F2, 0},
	{0x7E03, 0xC1F0, 0x007D, 0xFF89, 0},
	{0x383E, 0xFD9D, 0xB278, 0x4547, 0}
};

static xtalk_coefs_t const asXtalkNarrowCoefsLeftXt = {
	{0x3CB2, 0xDF49, 0xF6EA, 0x095B, 0},
	{0x6777, 0xC915, 0xFEAF, 0x00B1, 0},
	{0x7762, 0xC7D9, 0x025B, 0xFDA6, 0},
	{0x6B7A, 0xD2AA, 0xF2FB, 0x0B64, 0},
	{0, 0, 0, 0, 0}
};

static xtalk_coefs_t const asXtalkNarrowCoefsRightXt = {
	{0x3CB2, 0xDF49, 0xF6EA, 0x095B, 0},
	{0x6777, 0xC915, 0xFEAF, 0x00B1, 0},
	{0x7762, 0xC7D9, 0x025B, 0xFDA6, 0},
	{0x6B7A, 0xD2AA, 0xF2FB, 0x0B64, 0},
	{0, 0, 0, 0, 0}
};

static xtalk_coefs_t const asXtalkCoefsZeros = {
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0}
};
static xtalk_coefs_t const asXtalkCoefsPipe = {
	{0, 0, 0x0FA0, 0, 0},
	{0, 0, 0x0FA0, 0, 0},
	{0, 0, 0x0FA0, 0, 0},
	{0, 0, 0x0FA0, 0, 0},
	{0, 0, 0x1180, 0, 0},
};
static xtalk_coefs_t const asXtalkCoefsNegPipe = {
	{0, 0, 0xF380, 0, 0},
	{0, 0, 0xF380, 0, 0},
	{0, 0, 0xF380, 0, 0},
	{0, 0, 0xF380, 0, 0},
	{0, 0, 0xF200, 0, 0}
};

static xtalk_coefs_t const asXtalkCoefsNumTest = {
	{0, 0, 0xF380, 0x8000, 0x6D60},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0}
};

static xtalk_coefs_t const asXtalkCoefsDenTest = {
	{0xC000, 0x2000, 0x4000, 0, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0}
};

static xtalk_state_t const asXtalkOutStateTest = {
	{0x7FFF, 0x0004, 0xFFFC, 0},
	{0xFE00, 0x0008, 0xFFF8, 0x4000},
	{0x200, 0x0010, 0xFFF0, 0xC000},
	{0x8000, 0x0020, 0xFFE0, 0},
	{0, 0, 0, 0}
};

static xtalk_coefs_t const asDiamondCoefsLeftEq = {
	{0x0F1E, 0x2D05, 0xF8E3, 0x07C8, 0},
	{0x45E2, 0xCA51, 0x0448, 0xFCE7, 0},
	{0xA93E, 0xDBD5, 0x022C, 0x028A, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0}
};

static xtalk_coefs_t const asDiamondCoefsRightEq = {
	{0x0F1E, 0x2D05, 0xF8E3, 0x07C8, 0},
	{0x45E2, 0xCA51, 0x0448, 0xFCE7, 0},
	{0xA93E, 0xDBD5, 0x022C, 0x028A, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0}
};

static xtalk_coefs_t const asDiamondCoefsLeftXt = {
	{0x3B50, 0xFE08, 0xF959, 0x0060, 0},
	{0x9FCB, 0xD8F1, 0x00A2, 0x003A, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0}
};

static xtalk_coefs_t const asDiamondCoefsRightXt = {
	{0x3B50, 0xFE08, 0xF959, 0x0060, 0},
	{0x9FCB, 0xD8F1, 0x00A2, 0x003A, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0}
};

 /**/
/* XTalk EQ and XT */
static void
vortex_XtalkHw_SetLeftEQ(vortex_t * vortex, short arg_0, short arg_4,
			 xtalk_coefs_t const coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		hwwrite(vortex->mmio, 0x24200 + i * 0x24, coefs[i][0]);
		hwwrite(vortex->mmio, 0x24204 + i * 0x24, coefs[i][1]);
		hwwrite(vortex->mmio, 0x24208 + i * 0x24, coefs[i][2]);
		hwwrite(vortex->mmio, 0x2420c + i * 0x24, coefs[i][3]);
		hwwrite(vortex->mmio, 0x24210 + i * 0x24, coefs[i][4]);
	}
	hwwrite(vortex->mmio, 0x24538, arg_0 & 0xffff);
	hwwrite(vortex->mmio, 0x2453C, arg_4 & 0xffff);
}

static void
vortex_XtalkHw_SetRightEQ(vortex_t * vortex, short arg_0, short arg_4,
			  xtalk_coefs_t const coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		hwwrite(vortex->mmio, 0x242b4 + i * 0x24, coefs[i][0]);
		hwwrite(vortex->mmio, 0x242b8 + i * 0x24, coefs[i][1]);
		hwwrite(vortex->mmio, 0x242bc + i * 0x24, coefs[i][2]);
		hwwrite(vortex->mmio, 0x242c0 + i * 0x24, coefs[i][3]);
		hwwrite(vortex->mmio, 0x242c4 + i * 0x24, coefs[i][4]);
	}
	hwwrite(vortex->mmio, 0x24540, arg_0 & 0xffff);
	hwwrite(vortex->mmio, 0x24544, arg_4 & 0xffff);
}

static void
vortex_XtalkHw_SetLeftXT(vortex_t * vortex, short arg_0, short arg_4,
			 xtalk_coefs_t const coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		hwwrite(vortex->mmio, 0x24368 + i * 0x24, coefs[i][0]);
		hwwrite(vortex->mmio, 0x2436c + i * 0x24, coefs[i][1]);
		hwwrite(vortex->mmio, 0x24370 + i * 0x24, coefs[i][2]);
		hwwrite(vortex->mmio, 0x24374 + i * 0x24, coefs[i][3]);
		hwwrite(vortex->mmio, 0x24378 + i * 0x24, coefs[i][4]);
	}
	hwwrite(vortex->mmio, 0x24548, arg_0 & 0xffff);
	hwwrite(vortex->mmio, 0x2454C, arg_4 & 0xffff);
}

static void
vortex_XtalkHw_SetRightXT(vortex_t * vortex, short arg_0, short arg_4,
			  xtalk_coefs_t const coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		hwwrite(vortex->mmio, 0x2441C + i * 0x24, coefs[i][0]);
		hwwrite(vortex->mmio, 0x24420 + i * 0x24, coefs[i][1]);
		hwwrite(vortex->mmio, 0x24424 + i * 0x24, coefs[i][2]);
		hwwrite(vortex->mmio, 0x24428 + i * 0x24, coefs[i][3]);
		hwwrite(vortex->mmio, 0x2442C + i * 0x24, coefs[i][4]);
	}
	hwwrite(vortex->mmio, 0x24550, arg_0 & 0xffff);
	hwwrite(vortex->mmio, 0x24554, arg_4 & 0xffff);
}

static void
vortex_XtalkHw_SetLeftEQStates(vortex_t * vortex,
			       xtalk_instate_t const arg_0,
			       xtalk_state_t const coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		hwwrite(vortex->mmio, 0x24214 + i * 0x24, coefs[i][0]);
		hwwrite(vortex->mmio, 0x24218 + i * 0x24, coefs[i][1]);
		hwwrite(vortex->mmio, 0x2421C + i * 0x24, coefs[i][2]);
		hwwrite(vortex->mmio, 0x24220 + i * 0x24, coefs[i][3]);
	}
	hwwrite(vortex->mmio, 0x244F8 + i * 0x24, arg_0[0]);
	hwwrite(vortex->mmio, 0x244FC + i * 0x24, arg_0[1]);
	hwwrite(vortex->mmio, 0x24500 + i * 0x24, arg_0[2]);
	hwwrite(vortex->mmio, 0x24504 + i * 0x24, arg_0[3]);
}

static void
vortex_XtalkHw_SetRightEQStates(vortex_t * vortex,
				xtalk_instate_t const arg_0,
				xtalk_state_t const coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		hwwrite(vortex->mmio, 0x242C8 + i * 0x24, coefs[i][0]);
		hwwrite(vortex->mmio, 0x242CC + i * 0x24, coefs[i][1]);
		hwwrite(vortex->mmio, 0x242D0 + i * 0x24, coefs[i][2]);
		hwwrite(vortex->mmio, 0x244D4 + i * 0x24, coefs[i][3]);
	}
	hwwrite(vortex->mmio, 0x24508 + i * 0x24, arg_0[0]);
	hwwrite(vortex->mmio, 0x2450C + i * 0x24, arg_0[1]);
	hwwrite(vortex->mmio, 0x24510 + i * 0x24, arg_0[2]);
	hwwrite(vortex->mmio, 0x24514 + i * 0x24, arg_0[3]);
}

static void
vortex_XtalkHw_SetLeftXTStates(vortex_t * vortex,
			       xtalk_instate_t const arg_0,
			       xtalk_state_t const coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		hwwrite(vortex->mmio, 0x2437C + i * 0x24, coefs[i][0]);
		hwwrite(vortex->mmio, 0x24380 + i * 0x24, coefs[i][1]);
		hwwrite(vortex->mmio, 0x24384 + i * 0x24, coefs[i][2]);
		hwwrite(vortex->mmio, 0x24388 + i * 0x24, coefs[i][3]);
	}
	hwwrite(vortex->mmio, 0x24518 + i * 0x24, arg_0[0]);
	hwwrite(vortex->mmio, 0x2451C + i * 0x24, arg_0[1]);
	hwwrite(vortex->mmio, 0x24520 + i * 0x24, arg_0[2]);
	hwwrite(vortex->mmio, 0x24524 + i * 0x24, arg_0[3]);
}

static void
vortex_XtalkHw_SetRightXTStates(vortex_t * vortex,
				xtalk_instate_t const arg_0,
				xtalk_state_t const coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		hwwrite(vortex->mmio, 0x24430 + i * 0x24, coefs[i][0]);
		hwwrite(vortex->mmio, 0x24434 + i * 0x24, coefs[i][1]);
		hwwrite(vortex->mmio, 0x24438 + i * 0x24, coefs[i][2]);
		hwwrite(vortex->mmio, 0x2443C + i * 0x24, coefs[i][3]);
	}
	hwwrite(vortex->mmio, 0x24528 + i * 0x24, arg_0[0]);
	hwwrite(vortex->mmio, 0x2452C + i * 0x24, arg_0[1]);
	hwwrite(vortex->mmio, 0x24530 + i * 0x24, arg_0[2]);
	hwwrite(vortex->mmio, 0x24534 + i * 0x24, arg_0[3]);
}

#if 0
static void
vortex_XtalkHw_GetLeftEQ(vortex_t * vortex, short *arg_0, short *arg_4,
			 xtalk_coefs_t coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		coefs[i][0] = hwread(vortex->mmio, 0x24200 + i * 0x24);
		coefs[i][1] = hwread(vortex->mmio, 0x24204 + i * 0x24);
		coefs[i][2] = hwread(vortex->mmio, 0x24208 + i * 0x24);
		coefs[i][3] = hwread(vortex->mmio, 0x2420c + i * 0x24);
		coefs[i][4] = hwread(vortex->mmio, 0x24210 + i * 0x24);
	}
	*arg_0 = hwread(vortex->mmio, 0x24538) & 0xffff;
	*arg_4 = hwread(vortex->mmio, 0x2453c) & 0xffff;
}

static void
vortex_XtalkHw_GetRightEQ(vortex_t * vortex, short *arg_0, short *arg_4,
			  xtalk_coefs_t coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		coefs[i][0] = hwread(vortex->mmio, 0x242b4 + i * 0x24);
		coefs[i][1] = hwread(vortex->mmio, 0x242b8 + i * 0x24);
		coefs[i][2] = hwread(vortex->mmio, 0x242bc + i * 0x24);
		coefs[i][3] = hwread(vortex->mmio, 0x242c0 + i * 0x24);
		coefs[i][4] = hwread(vortex->mmio, 0x242c4 + i * 0x24);
	}
	*arg_0 = hwread(vortex->mmio, 0x24540) & 0xffff;
	*arg_4 = hwread(vortex->mmio, 0x24544) & 0xffff;
}

static void
vortex_XtalkHw_GetLeftXT(vortex_t * vortex, short *arg_0, short *arg_4,
			 xtalk_coefs_t coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		coefs[i][0] = hwread(vortex->mmio, 0x24368 + i * 0x24);
		coefs[i][1] = hwread(vortex->mmio, 0x2436C + i * 0x24);
		coefs[i][2] = hwread(vortex->mmio, 0x24370 + i * 0x24);
		coefs[i][3] = hwread(vortex->mmio, 0x24374 + i * 0x24);
		coefs[i][4] = hwread(vortex->mmio, 0x24378 + i * 0x24);
	}
	*arg_0 = hwread(vortex->mmio, 0x24548) & 0xffff;
	*arg_4 = hwread(vortex->mmio, 0x2454C) & 0xffff;
}

static void
vortex_XtalkHw_GetRightXT(vortex_t * vortex, short *arg_0, short *arg_4,
			  xtalk_coefs_t coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		coefs[i][0] = hwread(vortex->mmio, 0x2441C + i * 0x24);
		coefs[i][1] = hwread(vortex->mmio, 0x24420 + i * 0x24);
		coefs[i][2] = hwread(vortex->mmio, 0x24424 + i * 0x24);
		coefs[i][3] = hwread(vortex->mmio, 0x24428 + i * 0x24);
		coefs[i][4] = hwread(vortex->mmio, 0x2442C + i * 0x24);
	}
	*arg_0 = hwread(vortex->mmio, 0x24550) & 0xffff;
	*arg_4 = hwread(vortex->mmio, 0x24554) & 0xffff;
}

static void
vortex_XtalkHw_GetLeftEQStates(vortex_t * vortex, xtalk_instate_t arg_0,
			       xtalk_state_t coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		coefs[i][0] = hwread(vortex->mmio, 0x24214 + i * 0x24);
		coefs[i][1] = hwread(vortex->mmio, 0x24218 + i * 0x24);
		coefs[i][2] = hwread(vortex->mmio, 0x2421C + i * 0x24);
		coefs[i][3] = hwread(vortex->mmio, 0x24220 + i * 0x24);
	}
	arg_0[0] = hwread(vortex->mmio, 0x244F8 + i * 0x24);
	arg_0[1] = hwread(vortex->mmio, 0x244FC + i * 0x24);
	arg_0[2] = hwread(vortex->mmio, 0x24500 + i * 0x24);
	arg_0[3] = hwread(vortex->mmio, 0x24504 + i * 0x24);
}

static void
vortex_XtalkHw_GetRightEQStates(vortex_t * vortex, xtalk_instate_t arg_0,
				xtalk_state_t coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		coefs[i][0] = hwread(vortex->mmio, 0x242C8 + i * 0x24);
		coefs[i][1] = hwread(vortex->mmio, 0x242CC + i * 0x24);
		coefs[i][2] = hwread(vortex->mmio, 0x242D0 + i * 0x24);
		coefs[i][3] = hwread(vortex->mmio, 0x242D4 + i * 0x24);
	}
	arg_0[0] = hwread(vortex->mmio, 0x24508 + i * 0x24);
	arg_0[1] = hwread(vortex->mmio, 0x2450C + i * 0x24);
	arg_0[2] = hwread(vortex->mmio, 0x24510 + i * 0x24);
	arg_0[3] = hwread(vortex->mmio, 0x24514 + i * 0x24);
}

static void
vortex_XtalkHw_GetLeftXTStates(vortex_t * vortex, xtalk_instate_t arg_0,
			       xtalk_state_t coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		coefs[i][0] = hwread(vortex->mmio, 0x2437C + i * 0x24);
		coefs[i][1] = hwread(vortex->mmio, 0x24380 + i * 0x24);
		coefs[i][2] = hwread(vortex->mmio, 0x24384 + i * 0x24);
		coefs[i][3] = hwread(vortex->mmio, 0x24388 + i * 0x24);
	}
	arg_0[0] = hwread(vortex->mmio, 0x24518 + i * 0x24);
	arg_0[1] = hwread(vortex->mmio, 0x2451C + i * 0x24);
	arg_0[2] = hwread(vortex->mmio, 0x24520 + i * 0x24);
	arg_0[3] = hwread(vortex->mmio, 0x24524 + i * 0x24);
}

static void
vortex_XtalkHw_GetRightXTStates(vortex_t * vortex, xtalk_instate_t arg_0,
				xtalk_state_t coefs)
{
	int i;

	for (i = 0; i < 5; i++) {
		coefs[i][0] = hwread(vortex->mmio, 0x24430 + i * 0x24);
		coefs[i][1] = hwread(vortex->mmio, 0x24434 + i * 0x24);
		coefs[i][2] = hwread(vortex->mmio, 0x24438 + i * 0x24);
		coefs[i][3] = hwread(vortex->mmio, 0x2443C + i * 0x24);
	}
	arg_0[0] = hwread(vortex->mmio, 0x24528 + i * 0x24);
	arg_0[1] = hwread(vortex->mmio, 0x2452C + i * 0x24);
	arg_0[2] = hwread(vortex->mmio, 0x24530 + i * 0x24);
	arg_0[3] = hwread(vortex->mmio, 0x24534 + i * 0x24);
}

#endif
/* Gains */

static void
vortex_XtalkHw_SetGains(vortex_t * vortex, xtalk_gains_t const gains)
{
	int i;

	for (i = 0; i < XTGAINS_SZ; i++) {
		hwwrite(vortex->mmio, 0x244D0 + (i * 4), gains[i]);
	}
}

static void
vortex_XtalkHw_SetGainsAllChan(vortex_t * vortex)
{
	vortex_XtalkHw_SetGains(vortex, asXtalkGainsAllChan);
}

#if 0
static void vortex_XtalkHw_GetGains(vortex_t * vortex, xtalk_gains_t gains)
{
	int i;

	for (i = 0; i < XTGAINS_SZ; i++)
		gains[i] = hwread(vortex->mmio, 0x244D0 + i * 4);
}

#endif
/* Delay parameters */

static void
vortex_XtalkHw_SetDelay(vortex_t * vortex, unsigned short right,
			unsigned short left)
{
	u32 esp0 = 0;

	esp0 &= 0x1FFFFFFF;
	esp0 |= 0xA0000000;
	esp0 = (esp0 & 0xffffE0ff) | ((right & 0x1F) << 8);
	esp0 = (esp0 & 0xfffc1fff) | ((left & 0x1F) << 0xd);

	hwwrite(vortex->mmio, 0x24660, esp0);
}

static void
vortex_XtalkHw_SetLeftDline(vortex_t * vortex, xtalk_dline_t const dline)
{
	int i;

	for (i = 0; i < 0x20; i++) {
		hwwrite(vortex->mmio, 0x24000 + (i << 2), dline[i] & 0xffff);
		hwwrite(vortex->mmio, 0x24080 + (i << 2), dline[i] >> 0x10);
	}
}

static void
vortex_XtalkHw_SetRightDline(vortex_t * vortex, xtalk_dline_t const dline)
{
	int i;

	for (i = 0; i < 0x20; i++) {
		hwwrite(vortex->mmio, 0x24100 + (i << 2), dline[i] & 0xffff);
		hwwrite(vortex->mmio, 0x24180 + (i << 2), dline[i] >> 0x10);
	}
}

#if 0
static void
vortex_XtalkHw_GetDelay(vortex_t * vortex, unsigned short *right,
			unsigned short *left)
{
	int esp0;

	esp0 = hwread(vortex->mmio, 0x24660);
	*right = (esp0 >> 8) & 0x1f;
	*left = (esp0 >> 0xd) & 0x1f;
}

static void vortex_XtalkHw_GetLeftDline(vortex_t * vortex, xtalk_dline_t dline)
{
	int i;

	for (i = 0; i < 0x20; i++) {
		dline[i] =
		    (hwread(vortex->mmio, 0x24000 + (i << 2)) & 0xffff) |
		    (hwread(vortex->mmio, 0x24080 + (i << 2)) << 0x10);
	}
}

static void vortex_XtalkHw_GetRightDline(vortex_t * vortex, xtalk_dline_t dline)
{
	int i;

	for (i = 0; i < 0x20; i++) {
		dline[i] =
		    (hwread(vortex->mmio, 0x24100 + (i << 2)) & 0xffff) |
		    (hwread(vortex->mmio, 0x24180 + (i << 2)) << 0x10);
	}
}

#endif
/* Control/Global stuff */

#if 0
static void vortex_XtalkHw_SetControlReg(vortex_t * vortex, u32 ctrl)
{
	hwwrite(vortex->mmio, 0x24660, ctrl);
}
static void vortex_XtalkHw_GetControlReg(vortex_t * vortex, u32 *ctrl)
{
	*ctrl = hwread(vortex->mmio, 0x24660);
}
#endif
static void vortex_XtalkHw_SetSampleRate(vortex_t * vortex, u32 sr)
{
	u32 temp;

	temp = (hwread(vortex->mmio, 0x24660) & 0x1FFFFFFF) | 0xC0000000;
	temp = (temp & 0xffffff07) | ((sr & 0x1f) << 3);
	hwwrite(vortex->mmio, 0x24660, temp);
}

#if 0
static void vortex_XtalkHw_GetSampleRate(vortex_t * vortex, u32 *sr)
{
	*sr = (hwread(vortex->mmio, 0x24660) >> 3) & 0x1f;
}

#endif
static void vortex_XtalkHw_Enable(vortex_t * vortex)
{
	u32 temp;

	temp = (hwread(vortex->mmio, 0x24660) & 0x1FFFFFFF) | 0xC0000000;
	temp |= 1;
	hwwrite(vortex->mmio, 0x24660, temp);

}

static void vortex_XtalkHw_Disable(vortex_t * vortex)
{
	u32 temp;

	temp = (hwread(vortex->mmio, 0x24660) & 0x1FFFFFFF) | 0xC0000000;
	temp &= 0xfffffffe;
	hwwrite(vortex->mmio, 0x24660, temp);

}

static void vortex_XtalkHw_ZeroIO(vortex_t * vortex)
{
	int i;

	for (i = 0; i < 20; i++)
		hwwrite(vortex->mmio, 0x24600 + (i << 2), 0);
	for (i = 0; i < 4; i++)
		hwwrite(vortex->mmio, 0x24650 + (i << 2), 0);
}

static void vortex_XtalkHw_ZeroState(vortex_t * vortex)
{
	vortex_XtalkHw_ZeroIO(vortex);	// inlined

	vortex_XtalkHw_SetLeftEQ(vortex, 0, 0, asXtalkCoefsZeros);
	vortex_XtalkHw_SetRightEQ(vortex, 0, 0, asXtalkCoefsZeros);

	vortex_XtalkHw_SetLeftXT(vortex, 0, 0, asXtalkCoefsZeros);
	vortex_XtalkHw_SetRightXT(vortex, 0, 0, asXtalkCoefsZeros);

	vortex_XtalkHw_SetGains(vortex, asXtalkGainsZeros);	// inlined

	vortex_XtalkHw_SetDelay(vortex, 0, 0);	// inlined

	vortex_XtalkHw_SetLeftDline(vortex, alXtalkDlineZeros);	// inlined
	vortex_XtalkHw_SetRightDline(vortex, alXtalkDlineZeros);	// inlined
	vortex_XtalkHw_SetLeftDline(vortex, alXtalkDlineZeros);	// inlined
	vortex_XtalkHw_SetRightDline(vortex, alXtalkDlineZeros);	// inlined

	vortex_XtalkHw_SetLeftEQStates(vortex, asXtalkInStateZeros,
				       asXtalkOutStateZeros);
	vortex_XtalkHw_SetRightEQStates(vortex, asXtalkInStateZeros,
					asXtalkOutStateZeros);
	vortex_XtalkHw_SetLeftXTStates(vortex, asXtalkInStateZeros,
				       asXtalkOutStateZeros);
	vortex_XtalkHw_SetRightXTStates(vortex, asXtalkInStateZeros,
					asXtalkOutStateZeros);
}

static void vortex_XtalkHw_ProgramPipe(vortex_t * vortex)
{

	vortex_XtalkHw_SetLeftEQ(vortex, 0, 1, asXtalkCoefsPipe);
	vortex_XtalkHw_SetRightEQ(vortex, 0, 1, asXtalkCoefsPipe);
	vortex_XtalkHw_SetLeftXT(vortex, 0, 0, asXtalkCoefsZeros);
	vortex_XtalkHw_SetRightXT(vortex, 0, 0, asXtalkCoefsZeros);

	vortex_XtalkHw_SetDelay(vortex, 0, 0);	// inlined
}

static void vortex_XtalkHw_ProgramXtalkWide(vortex_t * vortex)
{

	vortex_XtalkHw_SetLeftEQ(vortex, sXtalkWideKLeftEq,
				 sXtalkWideShiftLeftEq, asXtalkWideCoefsLeftEq);
	vortex_XtalkHw_SetRightEQ(vortex, sXtalkWideKRightEq,
				  sXtalkWideShiftRightEq,
				  asXtalkWideCoefsRightEq);
	vortex_XtalkHw_SetLeftXT(vortex, sXtalkWideKLeftXt,
				 sXtalkWideShiftLeftXt, asXtalkWideCoefsLeftXt);
	vortex_XtalkHw_SetRightXT(vortex, sXtalkWideKLeftXt,
				  sXtalkWideShiftLeftXt,
				  asXtalkWideCoefsLeftXt);

	vortex_XtalkHw_SetDelay(vortex, wXtalkWideRightDelay, wXtalkWideLeftDelay);	// inlined
}

static void vortex_XtalkHw_ProgramXtalkNarrow(vortex_t * vortex)
{

	vortex_XtalkHw_SetLeftEQ(vortex, sXtalkNarrowKLeftEq,
				 sXtalkNarrowShiftLeftEq,
				 asXtalkNarrowCoefsLeftEq);
	vortex_XtalkHw_SetRightEQ(vortex, sXtalkNarrowKRightEq,
				  sXtalkNarrowShiftRightEq,
				  asXtalkNarrowCoefsRightEq);
	vortex_XtalkHw_SetLeftXT(vortex, sXtalkNarrowKLeftXt,
				 sXtalkNarrowShiftLeftXt,
				 asXtalkNarrowCoefsLeftXt);
	vortex_XtalkHw_SetRightXT(vortex, sXtalkNarrowKLeftXt,
				  sXtalkNarrowShiftLeftXt,
				  asXtalkNarrowCoefsLeftXt);

	vortex_XtalkHw_SetDelay(vortex, wXtalkNarrowRightDelay, wXtalkNarrowLeftDelay);	// inlined
}

static void vortex_XtalkHw_ProgramDiamondXtalk(vortex_t * vortex)
{

	//sDiamondKLeftEq,sDiamondKRightXt,asDiamondCoefsLeftEq
	vortex_XtalkHw_SetLeftEQ(vortex, sDiamondKLeftEq,
				 sDiamondShiftLeftEq, asDiamondCoefsLeftEq);
	vortex_XtalkHw_SetRightEQ(vortex, sDiamondKRightEq,
				  sDiamondShiftRightEq, asDiamondCoefsRightEq);
	vortex_XtalkHw_SetLeftXT(vortex, sDiamondKLeftXt,
				 sDiamondShiftLeftXt, asDiamondCoefsLeftXt);
	vortex_XtalkHw_SetRightXT(vortex, sDiamondKLeftXt,
				  sDiamondShiftLeftXt, asDiamondCoefsLeftXt);

	vortex_XtalkHw_SetDelay(vortex, wDiamondRightDelay, wDiamondLeftDelay);	// inlined
}

static void vortex_XtalkHw_init(vortex_t * vortex)
{
	vortex_XtalkHw_ZeroState(vortex);
}

/* End of file */
