/***************************************************************************
 *            au88x0_eq.c
 *  Aureal Vortex Hardware EQ control/access.
 *
 *  Sun Jun  8 18:19:19 2003
 *  2003  Manuel Jander (mjander@users.sourceforge.net)
 *  
 *  02 July 2003: First time something works :)
 *  November 2003: A3D Bypass code completed but untested.
 *
 *  TODO:
 *     - Debug (testing)
 *     - Test peak visualization support.
 *
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

/*
 The Aureal Hardware EQ is found on AU8810 and AU8830 chips only.
 it has 4 inputs (2 for general mix, 2 for A3D) and 2 outputs (supposed 
 to be routed to the codec).
*/

#include "au88x0.h"
#include "au88x0_eq.h"
#include "au88x0_eqdata.c"

#define VORTEX_EQ_BASE	 0x2b000
#define VORTEX_EQ_DEST   (VORTEX_EQ_BASE + 0x410)
#define VORTEX_EQ_SOURCE (VORTEX_EQ_BASE + 0x430)
#define VORTEX_EQ_CTRL   (VORTEX_EQ_BASE + 0x440)

#define VORTEX_BAND_COEFF_SIZE 0x30

/* CEqHw.s */
static void vortex_EqHw_SetTimeConsts(vortex_t * vortex, u16 gain, u16 level)
{
	hwwrite(vortex->mmio, 0x2b3c4, gain);
	hwwrite(vortex->mmio, 0x2b3c8, level);
}

static inline u16 sign_invert(u16 a)
{
	/* -(-32768) -> -32768 so we do -(-32768) -> 32767 to make the result positive */
	if (a == (u16)-32768)
		return 32767;
	else
		return -a;
}

static void vortex_EqHw_SetLeftCoefs(vortex_t * vortex, u16 coefs[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int i = 0, n /*esp2c */;

	for (n = 0; n < eqhw->this04; n++) {
		hwwrite(vortex->mmio, 0x2b000 + n * 0x30, coefs[i + 0]);
		hwwrite(vortex->mmio, 0x2b004 + n * 0x30, coefs[i + 1]);

		if (eqhw->this08 == 0) {
			hwwrite(vortex->mmio, 0x2b008 + n * 0x30, coefs[i + 2]);
			hwwrite(vortex->mmio, 0x2b00c + n * 0x30, coefs[i + 3]);
			hwwrite(vortex->mmio, 0x2b010 + n * 0x30, coefs[i + 4]);
		} else {
			hwwrite(vortex->mmio, 0x2b008 + n * 0x30, sign_invert(coefs[2 + i]));
			hwwrite(vortex->mmio, 0x2b00c + n * 0x30, sign_invert(coefs[3 + i]));
		        hwwrite(vortex->mmio, 0x2b010 + n * 0x30, sign_invert(coefs[4 + i]));
		}
		i += 5;
	}
}

static void vortex_EqHw_SetRightCoefs(vortex_t * vortex, u16 coefs[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int i = 0, n /*esp2c */;

	for (n = 0; n < eqhw->this04; n++) {
		hwwrite(vortex->mmio, 0x2b1e0 + n * 0x30, coefs[0 + i]);
		hwwrite(vortex->mmio, 0x2b1e4 + n * 0x30, coefs[1 + i]);

		if (eqhw->this08 == 0) {
			hwwrite(vortex->mmio, 0x2b1e8 + n * 0x30, coefs[2 + i]);
			hwwrite(vortex->mmio, 0x2b1ec + n * 0x30, coefs[3 + i]);
			hwwrite(vortex->mmio, 0x2b1f0 + n * 0x30, coefs[4 + i]);
		} else {
			hwwrite(vortex->mmio, 0x2b1e8 + n * 0x30, sign_invert(coefs[2 + i]));
			hwwrite(vortex->mmio, 0x2b1ec + n * 0x30, sign_invert(coefs[3 + i]));
			hwwrite(vortex->mmio, 0x2b1f0 + n * 0x30, sign_invert(coefs[4 + i]));
		}
		i += 5;
	}

}

static void vortex_EqHw_SetLeftStates(vortex_t * vortex, u16 a[], u16 b[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int i = 0, ebx;

	hwwrite(vortex->mmio, 0x2b3fc, a[0]);
	hwwrite(vortex->mmio, 0x2b400, a[1]);

	for (ebx = 0; ebx < eqhw->this04; ebx++) {
		hwwrite(vortex->mmio, 0x2b014 + (i * 0xc), b[i]);
		hwwrite(vortex->mmio, 0x2b018 + (i * 0xc), b[1 + i]);
		hwwrite(vortex->mmio, 0x2b01c + (i * 0xc), b[2 + i]);
		hwwrite(vortex->mmio, 0x2b020 + (i * 0xc), b[3 + i]);
		i += 4;
	}
}

static void vortex_EqHw_SetRightStates(vortex_t * vortex, u16 a[], u16 b[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int i = 0, ebx;

	hwwrite(vortex->mmio, 0x2b404, a[0]);
	hwwrite(vortex->mmio, 0x2b408, a[1]);

	for (ebx = 0; ebx < eqhw->this04; ebx++) {
		hwwrite(vortex->mmio, 0x2b1f4 + (i * 0xc), b[i]);
		hwwrite(vortex->mmio, 0x2b1f8 + (i * 0xc), b[1 + i]);
		hwwrite(vortex->mmio, 0x2b1fc + (i * 0xc), b[2 + i]);
		hwwrite(vortex->mmio, 0x2b200 + (i * 0xc), b[3 + i]);
		i += 4;
	}
}

#if 0
static void vortex_EqHw_GetTimeConsts(vortex_t * vortex, u16 * a, u16 * b)
{
	*a = hwread(vortex->mmio, 0x2b3c4);
	*b = hwread(vortex->mmio, 0x2b3c8);
}

static void vortex_EqHw_GetLeftCoefs(vortex_t * vortex, u16 a[])
{

}

static void vortex_EqHw_GetRightCoefs(vortex_t * vortex, u16 a[])
{

}

static void vortex_EqHw_GetLeftStates(vortex_t * vortex, u16 * a, u16 b[])
{

}

static void vortex_EqHw_GetRightStates(vortex_t * vortex, u16 * a, u16 b[])
{

}

#endif
/* Mix Gains */
static void vortex_EqHw_SetBypassGain(vortex_t * vortex, u16 a, u16 b)
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	if (eqhw->this08 == 0) {
		hwwrite(vortex->mmio, 0x2b3d4, a);
		hwwrite(vortex->mmio, 0x2b3ec, b);
	} else {
		hwwrite(vortex->mmio, 0x2b3d4, sign_invert(a));
		hwwrite(vortex->mmio, 0x2b3ec, sign_invert(b));
	}
}

static void vortex_EqHw_SetA3DBypassGain(vortex_t * vortex, u16 a, u16 b)
{

	hwwrite(vortex->mmio, 0x2b3e0, a);
	hwwrite(vortex->mmio, 0x2b3f8, b);
}

#if 0
static void vortex_EqHw_SetCurrBypassGain(vortex_t * vortex, u16 a, u16 b)
{

	hwwrite(vortex->mmio, 0x2b3d0, a);
	hwwrite(vortex->mmio, 0x2b3e8, b);
}

static void vortex_EqHw_SetCurrA3DBypassGain(vortex_t * vortex, u16 a, u16 b)
{

	hwwrite(vortex->mmio, 0x2b3dc, a);
	hwwrite(vortex->mmio, 0x2b3f4, b);
}

#endif
static void
vortex_EqHw_SetLeftGainsSingleTarget(vortex_t * vortex, u16 index, u16 b)
{
	hwwrite(vortex->mmio, 0x2b02c + (index * 0x30), b);
}

static void
vortex_EqHw_SetRightGainsSingleTarget(vortex_t * vortex, u16 index, u16 b)
{
	hwwrite(vortex->mmio, 0x2b20c + (index * 0x30), b);
}

static void vortex_EqHw_SetLeftGainsTarget(vortex_t * vortex, u16 a[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int ebx;

	for (ebx = 0; ebx < eqhw->this04; ebx++) {
		hwwrite(vortex->mmio, 0x2b02c + ebx * 0x30, a[ebx]);
	}
}

static void vortex_EqHw_SetRightGainsTarget(vortex_t * vortex, u16 a[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int ebx;

	for (ebx = 0; ebx < eqhw->this04; ebx++) {
		hwwrite(vortex->mmio, 0x2b20c + ebx * 0x30, a[ebx]);
	}
}

static void vortex_EqHw_SetLeftGainsCurrent(vortex_t * vortex, u16 a[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int ebx;

	for (ebx = 0; ebx < eqhw->this04; ebx++) {
		hwwrite(vortex->mmio, 0x2b028 + ebx * 0x30, a[ebx]);
	}
}

static void vortex_EqHw_SetRightGainsCurrent(vortex_t * vortex, u16 a[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int ebx;

	for (ebx = 0; ebx < eqhw->this04; ebx++) {
		hwwrite(vortex->mmio, 0x2b208 + ebx * 0x30, a[ebx]);
	}
}

#if 0
static void vortex_EqHw_GetLeftGainsTarget(vortex_t * vortex, u16 a[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int ebx = 0;

	if (eqhw->this04 < 0)
		return;

	do {
		a[ebx] = hwread(vortex->mmio, 0x2b02c + ebx * 0x30);
		ebx++;
	}
	while (ebx < eqhw->this04);
}

static void vortex_EqHw_GetRightGainsTarget(vortex_t * vortex, u16 a[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int ebx = 0;

	if (eqhw->this04 < 0)
		return;

	do {
		a[ebx] = hwread(vortex->mmio, 0x2b20c + ebx * 0x30);
		ebx++;
	}
	while (ebx < eqhw->this04);
}

static void vortex_EqHw_GetLeftGainsCurrent(vortex_t * vortex, u16 a[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int ebx = 0;

	if (eqhw->this04 < 0)
		return;

	do {
		a[ebx] = hwread(vortex->mmio, 0x2b028 + ebx * 0x30);
		ebx++;
	}
	while (ebx < eqhw->this04);
}

static void vortex_EqHw_GetRightGainsCurrent(vortex_t * vortex, u16 a[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int ebx = 0;

	if (eqhw->this04 < 0)
		return;

	do {
		a[ebx] = hwread(vortex->mmio, 0x2b208 + ebx * 0x30);
		ebx++;
	}
	while (ebx < eqhw->this04);
}

#endif
/* EQ band levels settings */
static void vortex_EqHw_SetLevels(vortex_t * vortex, u16 peaks[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int i;

	/* set left peaks */
	for (i = 0; i < eqhw->this04; i++) {
		hwwrite(vortex->mmio, 0x2b024 + i * VORTEX_BAND_COEFF_SIZE, peaks[i]);
	}

	hwwrite(vortex->mmio, 0x2b3cc, peaks[eqhw->this04]);
	hwwrite(vortex->mmio, 0x2b3d8, peaks[eqhw->this04 + 1]);

	/* set right peaks */
	for (i = 0; i < eqhw->this04; i++) {
		hwwrite(vortex->mmio, 0x2b204 + i * VORTEX_BAND_COEFF_SIZE,
			peaks[i + (eqhw->this04 + 2)]);
	}

	hwwrite(vortex->mmio, 0x2b3e4, peaks[2 + (eqhw->this04 * 2)]);
	hwwrite(vortex->mmio, 0x2b3f0, peaks[3 + (eqhw->this04 * 2)]);
}

#if 0
static void vortex_EqHw_GetLevels(vortex_t * vortex, u16 a[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int ebx;

	if (eqhw->this04 < 0)
		return;

	ebx = 0;
	do {
		a[ebx] = hwread(vortex->mmio, 0x2b024 + ebx * 0x30);
		ebx++;
	}
	while (ebx < eqhw->this04);

	a[eqhw->this04] = hwread(vortex->mmio, 0x2b3cc);
	a[eqhw->this04 + 1] = hwread(vortex->mmio, 0x2b3d8);

	ebx = 0;
	do {
		a[ebx + (eqhw->this04 + 2)] =
		    hwread(vortex->mmio, 0x2b204 + ebx * 0x30);
		ebx++;
	}
	while (ebx < eqhw->this04);

	a[2 + (eqhw->this04 * 2)] = hwread(vortex->mmio, 0x2b3e4);
	a[3 + (eqhw->this04 * 2)] = hwread(vortex->mmio, 0x2b3f0);
}

#endif
/* Global Control */
static void vortex_EqHw_SetControlReg(vortex_t * vortex, u32 reg)
{
	hwwrite(vortex->mmio, 0x2b440, reg);
}

static void vortex_EqHw_SetSampleRate(vortex_t * vortex, u32 sr)
{
	hwwrite(vortex->mmio, 0x2b440, ((sr & 0x1f) << 3) | 0xb800);
}

#if 0
static void vortex_EqHw_GetControlReg(vortex_t * vortex, u32 *reg)
{
	*reg = hwread(vortex->mmio, 0x2b440);
}

static void vortex_EqHw_GetSampleRate(vortex_t * vortex, u32 *sr)
{
	*sr = (hwread(vortex->mmio, 0x2b440) >> 3) & 0x1f;
}

#endif
static void vortex_EqHw_Enable(vortex_t * vortex)
{
	hwwrite(vortex->mmio, VORTEX_EQ_CTRL, 0xf001);
}

static void vortex_EqHw_Disable(vortex_t * vortex)
{
	hwwrite(vortex->mmio, VORTEX_EQ_CTRL, 0xf000);
}

/* Reset (zero) buffers */
static void vortex_EqHw_ZeroIO(vortex_t * vortex)
{
	int i;
	for (i = 0; i < 0x8; i++)
		hwwrite(vortex->mmio, VORTEX_EQ_DEST + (i << 2), 0x0);
	for (i = 0; i < 0x4; i++)
		hwwrite(vortex->mmio, VORTEX_EQ_SOURCE + (i << 2), 0x0);
}

static void vortex_EqHw_ZeroA3DIO(vortex_t * vortex)
{
	int i;
	for (i = 0; i < 0x4; i++)
		hwwrite(vortex->mmio, VORTEX_EQ_DEST + (i << 2), 0x0);
}

static void vortex_EqHw_ZeroState(vortex_t * vortex)
{

	vortex_EqHw_SetControlReg(vortex, 0);
	vortex_EqHw_ZeroIO(vortex);
	hwwrite(vortex->mmio, 0x2b3c0, 0);

	vortex_EqHw_SetTimeConsts(vortex, 0, 0);

	vortex_EqHw_SetLeftCoefs(vortex, asEqCoefsZeros);
	vortex_EqHw_SetRightCoefs(vortex, asEqCoefsZeros);

	vortex_EqHw_SetLeftGainsCurrent(vortex, eq_gains_zero);
	vortex_EqHw_SetRightGainsCurrent(vortex, eq_gains_zero);
	vortex_EqHw_SetLeftGainsTarget(vortex, eq_gains_zero);
	vortex_EqHw_SetRightGainsTarget(vortex, eq_gains_zero);

	vortex_EqHw_SetBypassGain(vortex, 0, 0);
	//vortex_EqHw_SetCurrBypassGain(vortex, 0, 0);
	vortex_EqHw_SetA3DBypassGain(vortex, 0, 0);
	//vortex_EqHw_SetCurrA3DBypassGain(vortex, 0, 0);
	vortex_EqHw_SetLeftStates(vortex, eq_states_zero, asEqOutStateZeros);
	vortex_EqHw_SetRightStates(vortex, eq_states_zero, asEqOutStateZeros);
	vortex_EqHw_SetLevels(vortex, (u16 *) eq_levels);
}

/* Program coeficients as pass through */
static void vortex_EqHw_ProgramPipe(vortex_t * vortex)
{
	vortex_EqHw_SetTimeConsts(vortex, 0, 0);

	vortex_EqHw_SetLeftCoefs(vortex, asEqCoefsPipes);
	vortex_EqHw_SetRightCoefs(vortex, asEqCoefsPipes);

	vortex_EqHw_SetLeftGainsCurrent(vortex, eq_gains_current);
	vortex_EqHw_SetRightGainsCurrent(vortex, eq_gains_current);
	vortex_EqHw_SetLeftGainsTarget(vortex, eq_gains_current);
	vortex_EqHw_SetRightGainsTarget(vortex, eq_gains_current);
}

/* Program EQ block as 10 band Equalizer */
static void
vortex_EqHw_Program10Band(vortex_t * vortex, auxxEqCoeffSet_t * coefset)
{

	vortex_EqHw_SetTimeConsts(vortex, 0xc, 0x7fe0);

	vortex_EqHw_SetLeftCoefs(vortex, coefset->LeftCoefs);
	vortex_EqHw_SetRightCoefs(vortex, coefset->RightCoefs);

	vortex_EqHw_SetLeftGainsCurrent(vortex, coefset->LeftGains);

	vortex_EqHw_SetRightGainsTarget(vortex, coefset->RightGains);
	vortex_EqHw_SetLeftGainsTarget(vortex, coefset->LeftGains);

	vortex_EqHw_SetRightGainsCurrent(vortex, coefset->RightGains);
}

/* Read all EQ peaks. (think VU meter) */
static void vortex_EqHw_GetTenBandLevels(vortex_t * vortex, u16 peaks[])
{
	eqhw_t *eqhw = &(vortex->eq.this04);
	int i;

	if (eqhw->this04 <= 0)
		return;

	for (i = 0; i < eqhw->this04; i++)
		peaks[i] = hwread(vortex->mmio, 0x2B024 + i * 0x30);
	for (i = 0; i < eqhw->this04; i++)
		peaks[i + eqhw->this04] =
		    hwread(vortex->mmio, 0x2B204 + i * 0x30);
}

/* CEqlzr.s */

static int vortex_Eqlzr_GetLeftGain(vortex_t * vortex, u16 index, u16 * gain)
{
	eqlzr_t *eq = &(vortex->eq);

	if (eq->this28) {
		*gain = eq->this130[index];
		return 0;
	}
	return 1;
}

static void vortex_Eqlzr_SetLeftGain(vortex_t * vortex, u16 index, u16 gain)
{
	eqlzr_t *eq = &(vortex->eq);

	if (eq->this28 == 0)
		return;

	eq->this130[index] = gain;
	if (eq->this54)
		return;

	vortex_EqHw_SetLeftGainsSingleTarget(vortex, index, gain);
}

static int vortex_Eqlzr_GetRightGain(vortex_t * vortex, u16 index, u16 * gain)
{
	eqlzr_t *eq = &(vortex->eq);

	if (eq->this28) {
		*gain = eq->this130[index + eq->this10];
		return 0;
	}
	return 1;
}

static void vortex_Eqlzr_SetRightGain(vortex_t * vortex, u16 index, u16 gain)
{
	eqlzr_t *eq = &(vortex->eq);

	if (eq->this28 == 0)
		return;

	eq->this130[index + eq->this10] = gain;
	if (eq->this54)
		return;

	vortex_EqHw_SetRightGainsSingleTarget(vortex, index, gain);
}

#if 0
static int
vortex_Eqlzr_GetAllBands(vortex_t * vortex, u16 * gains, s32 *cnt)
{
	eqlzr_t *eq = &(vortex->eq);
	int si = 0;

	if (eq->this10 == 0)
		return 1;

	{
		if (vortex_Eqlzr_GetLeftGain(vortex, si, &gains[si]))
			return 1;
		if (vortex_Eqlzr_GetRightGain
		    (vortex, si, &gains[si + eq->this10]))
			return 1;
		si++;
	}
	while (eq->this10 > si) ;
	*cnt = si * 2;
	return 0;
}
#endif
static int vortex_Eqlzr_SetAllBandsFromActiveCoeffSet(vortex_t * vortex)
{
	eqlzr_t *eq = &(vortex->eq);

	vortex_EqHw_SetLeftGainsTarget(vortex, eq->this130);
	vortex_EqHw_SetRightGainsTarget(vortex, &(eq->this130[eq->this10]));

	return 0;
}

static int
vortex_Eqlzr_SetAllBands(vortex_t * vortex, u16 gains[], s32 count)
{
	eqlzr_t *eq = &(vortex->eq);
	int i;

	if (((eq->this10) * 2 != count) || (eq->this28 == 0))
		return 1;

	for (i = 0; i < count; i++) {
		eq->this130[i] = gains[i];
	}
	
	if (eq->this54)
		return 0;
	return vortex_Eqlzr_SetAllBandsFromActiveCoeffSet(vortex);
}

static void
vortex_Eqlzr_SetA3dBypassGain(vortex_t * vortex, u32 a, u32 b)
{
	eqlzr_t *eq = &(vortex->eq);
	u32 eax, ebx;

	eq->this58 = a;
	eq->this5c = b;
	if (eq->this54)
		eax = eq->this0e;
	else
		eax = eq->this0a;
	ebx = (eax * eq->this58) >> 0x10;
	eax = (eax * eq->this5c) >> 0x10;
	vortex_EqHw_SetA3DBypassGain(vortex, ebx, eax);
}

static void vortex_Eqlzr_ProgramA3dBypassGain(vortex_t * vortex)
{
	eqlzr_t *eq = &(vortex->eq);
	u32 eax, ebx;

	if (eq->this54)
		eax = eq->this0e;
	else
		eax = eq->this0a;
	ebx = (eax * eq->this58) >> 0x10;
	eax = (eax * eq->this5c) >> 0x10;
	vortex_EqHw_SetA3DBypassGain(vortex, ebx, eax);
}

static void vortex_Eqlzr_ShutDownA3d(vortex_t * vortex)
{
	if (vortex != NULL)
		vortex_EqHw_ZeroA3DIO(vortex);
}

static void vortex_Eqlzr_SetBypass(vortex_t * vortex, u32 bp)
{
	eqlzr_t *eq = &(vortex->eq);
	
	if ((eq->this28) && (bp == 0)) {
		/* EQ enabled */
		vortex_Eqlzr_SetAllBandsFromActiveCoeffSet(vortex);
		vortex_EqHw_SetBypassGain(vortex, eq->this08, eq->this08);
	} else {
		/* EQ disabled. */
		vortex_EqHw_SetLeftGainsTarget(vortex, eq->this14_array);
		vortex_EqHw_SetRightGainsTarget(vortex, eq->this14_array);
		vortex_EqHw_SetBypassGain(vortex, eq->this0c, eq->this0c);
	}
	vortex_Eqlzr_ProgramA3dBypassGain(vortex);
}

static void vortex_Eqlzr_ReadAndSetActiveCoefSet(vortex_t * vortex)
{
	eqlzr_t *eq = &(vortex->eq);

	/* Set EQ BiQuad filter coeficients */
	memcpy(&(eq->coefset), &asEqCoefsNormal, sizeof(auxxEqCoeffSet_t));
	/* Set EQ Band gain levels and dump into hardware registers. */
	vortex_Eqlzr_SetAllBands(vortex, eq_gains_normal, eq->this10 * 2);
}

static int vortex_Eqlzr_GetAllPeaks(vortex_t * vortex, u16 * peaks, int *count)
{
	eqlzr_t *eq = &(vortex->eq);

	if (eq->this10 == 0)
		return 1;
	*count = eq->this10 * 2;
	vortex_EqHw_GetTenBandLevels(vortex, peaks);
	return 0;
}

#if 0
static auxxEqCoeffSet_t *vortex_Eqlzr_GetActiveCoefSet(vortex_t * vortex)
{
	eqlzr_t *eq = &(vortex->eq);

	return (&(eq->coefset));
}
#endif
static void vortex_Eqlzr_init(vortex_t * vortex)
{
	eqlzr_t *eq = &(vortex->eq);

	/* Object constructor */
	//eq->this04 = 0;
	eq->this08 = 0;		/* Bypass gain with EQ in use. */
	eq->this0a = 0x5999;
	eq->this0c = 0x5999;	/* Bypass gain with EQ disabled. */
	eq->this0e = 0x5999;

	eq->this10 = 0xa;	/* 10 eq frequency bands. */
	eq->this04.this04 = eq->this10;
	eq->this28 = 0x1;	/* if 1 => Allow read access to this130 (gains) */
	eq->this54 = 0x0;	/* if 1 => Dont Allow access to hardware (gains) */
	eq->this58 = 0xffff;
	eq->this5c = 0xffff;

	/* Set gains. */
	memset(eq->this14_array, 0, sizeof(eq->this14_array));

	/* Actual init. */
	vortex_EqHw_ZeroState(vortex);
	vortex_EqHw_SetSampleRate(vortex, 0x11);
	vortex_Eqlzr_ReadAndSetActiveCoefSet(vortex);

	vortex_EqHw_Program10Band(vortex, &(eq->coefset));
	vortex_Eqlzr_SetBypass(vortex, eq->this54);
	vortex_Eqlzr_SetA3dBypassGain(vortex, 0, 0);
	vortex_EqHw_Enable(vortex);
}

static void vortex_Eqlzr_shutdown(vortex_t * vortex)
{
	vortex_Eqlzr_ShutDownA3d(vortex);
	vortex_EqHw_ProgramPipe(vortex);
	vortex_EqHw_Disable(vortex);
}

/* ALSA interface */

/* Control interface */
static int
snd_vortex_eqtoggle_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int
snd_vortex_eqtoggle_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	vortex_t *vortex = snd_kcontrol_chip(kcontrol);
	eqlzr_t *eq = &(vortex->eq);
	//int i = kcontrol->private_value;

	ucontrol->value.integer.value[0] = eq->this54 ? 0 : 1;

	return 0;
}

static int
snd_vortex_eqtoggle_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	vortex_t *vortex = snd_kcontrol_chip(kcontrol);
	eqlzr_t *eq = &(vortex->eq);
	//int i = kcontrol->private_value;

	eq->this54 = ucontrol->value.integer.value[0] ? 0 : 1;
	vortex_Eqlzr_SetBypass(vortex, eq->this54);

	return 1;		/* Allways changes */
}

static struct snd_kcontrol_new vortex_eqtoggle_kcontrol __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "EQ Enable",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.private_value = 0,
	.info = snd_vortex_eqtoggle_info,
	.get = snd_vortex_eqtoggle_get,
	.put = snd_vortex_eqtoggle_put
};

static int
snd_vortex_eq_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0x0000;
	uinfo->value.integer.max = 0x7fff;
	return 0;
}

static int
snd_vortex_eq_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	vortex_t *vortex = snd_kcontrol_chip(kcontrol);
	int i = kcontrol->private_value;
	u16 gainL = 0, gainR = 0;

	vortex_Eqlzr_GetLeftGain(vortex, i, &gainL);
	vortex_Eqlzr_GetRightGain(vortex, i, &gainR);
	ucontrol->value.integer.value[0] = gainL;
	ucontrol->value.integer.value[1] = gainR;
	return 0;
}

static int
snd_vortex_eq_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	vortex_t *vortex = snd_kcontrol_chip(kcontrol);
	int changed = 0, i = kcontrol->private_value;
	u16 gainL = 0, gainR = 0;

	vortex_Eqlzr_GetLeftGain(vortex, i, &gainL);
	vortex_Eqlzr_GetRightGain(vortex, i, &gainR);

	if (gainL != ucontrol->value.integer.value[0]) {
		vortex_Eqlzr_SetLeftGain(vortex, i,
					 ucontrol->value.integer.value[0]);
		changed = 1;
	}
	if (gainR != ucontrol->value.integer.value[1]) {
		vortex_Eqlzr_SetRightGain(vortex, i,
					  ucontrol->value.integer.value[1]);
		changed = 1;
	}
	return changed;
}

static struct snd_kcontrol_new vortex_eq_kcontrol __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "                        .",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.private_value = 0,
	.info = snd_vortex_eq_info,
	.get = snd_vortex_eq_get,
	.put = snd_vortex_eq_put
};

static int
snd_vortex_peaks_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 20;
	uinfo->value.integer.min = 0x0000;
	uinfo->value.integer.max = 0x7fff;
	return 0;
}

static int
snd_vortex_peaks_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	vortex_t *vortex = snd_kcontrol_chip(kcontrol);
	int i, count = 0;
	u16 peaks[20];

	vortex_Eqlzr_GetAllPeaks(vortex, peaks, &count);
	if (count != 20) {
		printk(KERN_ERR "vortex: peak count error 20 != %d \n", count);
		return -1;
	}
	for (i = 0; i < 20; i++)
		ucontrol->value.integer.value[i] = peaks[i];

	return 0;
}

static struct snd_kcontrol_new vortex_levels_kcontrol __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "EQ Peaks",
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info = snd_vortex_peaks_info,
	.get = snd_vortex_peaks_get,
};

/* EQ band gain labels. */
static char *EqBandLabels[10] __devinitdata = {
	"EQ0 31Hz\0",
	"EQ1 63Hz\0",
	"EQ2 125Hz\0",
	"EQ3 250Hz\0",
	"EQ4 500Hz\0",
	"EQ5 1KHz\0",
	"EQ6 2KHz\0",
	"EQ7 4KHz\0",
	"EQ8 8KHz\0",
	"EQ9 16KHz\0",
};

/* ALSA driver entry points. Init and exit. */
static int vortex_eq_init(vortex_t * vortex)
{
	struct snd_kcontrol *kcontrol;
	int err, i;

	vortex_Eqlzr_init(vortex);

	if ((kcontrol =
	     snd_ctl_new1(&vortex_eqtoggle_kcontrol, vortex)) == NULL)
		return -ENOMEM;
	kcontrol->private_value = 0;
	if ((err = snd_ctl_add(vortex->card, kcontrol)) < 0)
		return err;

	/* EQ gain controls */
	for (i = 0; i < 10; i++) {
		if ((kcontrol =
		     snd_ctl_new1(&vortex_eq_kcontrol, vortex)) == NULL)
			return -ENOMEM;
		strcpy(kcontrol->id.name, EqBandLabels[i]);
		kcontrol->private_value = i;
		if ((err = snd_ctl_add(vortex->card, kcontrol)) < 0)
			return err;
		//vortex->eqctrl[i] = kcontrol;
	}
	/* EQ band levels */
	if ((kcontrol = snd_ctl_new1(&vortex_levels_kcontrol, vortex)) == NULL)
		return -ENOMEM;
	if ((err = snd_ctl_add(vortex->card, kcontrol)) < 0)
		return err;

	return 0;
}

static int vortex_eq_free(vortex_t * vortex)
{
	/*
	   //FIXME: segfault because vortex->eqctrl[i] == 4
	   int i;
	   for (i=0; i<10; i++) {
	   if (vortex->eqctrl[i])
	   snd_ctl_remove(vortex->card, vortex->eqctrl[i]);
	   }
	 */
	vortex_Eqlzr_shutdown(vortex);
	return 0;
}

/* End */
