/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
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

#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/gus.h>
#define __GUS_TABLES_ALLOC__
#include "gus_tables.h"

EXPORT_SYMBOL(snd_gf1_atten_table); /* for snd-gus-synth module */

unsigned short snd_gf1_lvol_to_gvol_raw(unsigned int vol)
{
	unsigned short e, m, tmp;

	if (vol > 65535)
		vol = 65535;
	tmp = vol;
	e = 7;
	if (tmp < 128) {
		while (e > 0 && tmp < (1 << e))
			e--;
	} else {
		while (tmp > 255) {
			tmp >>= 1;
			e++;
		}
	}
	m = vol - (1 << e);
	if (m > 0) {
		if (e > 8)
			m >>= e - 8;
		else if (e < 8)
			m <<= 8 - e;
		m &= 255;
	}
	return (e << 8) | m;
}

#if 0

unsigned int snd_gf1_gvol_to_lvol_raw(unsigned short gf1_vol)
{
	unsigned int rvol;
	unsigned short e, m;

	if (!gf1_vol)
		return 0;
	e = gf1_vol >> 8;
	m = (unsigned char) gf1_vol;
	rvol = 1 << e;
	if (e > 8)
		return rvol | (m << (e - 8));
	return rvol | (m >> (8 - e));
}

unsigned int snd_gf1_calc_ramp_rate(struct snd_gus_card * gus,
				    unsigned short start,
				    unsigned short end,
				    unsigned int us)
{
	static unsigned char vol_rates[19] =
	{
		23, 24, 26, 28, 29, 31, 32, 34,
		36, 37, 39, 40, 42, 44, 45, 47,
		49, 50, 52
	};
	unsigned short range, increment, value, i;

	start >>= 4;
	end >>= 4;
	if (start < end)
		us /= end - start;
	else
		us /= start - end;
	range = 4;
	value = gus->gf1.enh_mode ?
	    vol_rates[0] :
	    vol_rates[gus->gf1.active_voices - 14];
	for (i = 0; i < 3; i++) {
		if (us < value) {
			range = i;
			break;
		} else
			value <<= 3;
	}
	if (range == 4) {
		range = 3;
		increment = 1;
	} else
		increment = (value + (value >> 1)) / us;
	return (range << 6) | (increment & 0x3f);
}

#endif  /*  0  */

unsigned short snd_gf1_translate_freq(struct snd_gus_card * gus, unsigned int freq16)
{
	freq16 >>= 3;
	if (freq16 < 50)
		freq16 = 50;
	if (freq16 & 0xf8000000) {
		freq16 = ~0xf8000000;
		snd_printk(KERN_ERR "snd_gf1_translate_freq: overflow - freq = 0x%x\n", freq16);
	}
	return ((freq16 << 9) + (gus->gf1.playback_freq >> 1)) / gus->gf1.playback_freq;
}

#if 0

short snd_gf1_compute_vibrato(short cents, unsigned short fc_register)
{
	static short vibrato_table[] =
	{
		0, 0, 32, 592, 61, 1175, 93, 1808,
		124, 2433, 152, 3007, 182, 3632, 213, 4290,
		241, 4834, 255, 5200
	};

	long depth;
	short *vi1, *vi2, pcents, v1;

	pcents = cents < 0 ? -cents : cents;
	for (vi1 = vibrato_table, vi2 = vi1 + 2; pcents > *vi2; vi1 = vi2, vi2 += 2);
	v1 = *(vi1 + 1);
	/* The FC table above is a list of pairs. The first number in the pair     */
	/* is the cents index from 0-255 cents, and the second number in the       */
	/* pair is the FC adjustment needed to change the pitch by the indexed     */
	/* number of cents. The table was created for an FC of 32768.              */
	/* The following expression does a linear interpolation against the        */
	/* approximated log curve in the table above, and then scales the number   */
	/* by the FC before the LFO. This calculation also adjusts the output      */
	/* value to produce the appropriate depth for the hardware. The depth      */
	/* is 2 * desired FC + 1.                                                  */
	depth = (((int) (*(vi2 + 1) - *vi1) * (pcents - *vi1) / (*vi2 - *vi1)) + v1) * fc_register >> 14;
	if (depth)
		depth++;
	if (depth > 255)
		depth = 255;
	return cents < 0 ? -(short) depth : (short) depth;
}

unsigned short snd_gf1_compute_pitchbend(unsigned short pitchbend, unsigned short sens)
{
	static long log_table[] = {1024, 1085, 1149, 1218, 1290, 1367, 1448, 1534, 1625, 1722, 1825, 1933};
	int wheel, sensitivity;
	unsigned int mantissa, f1, f2;
	unsigned short semitones, f1_index, f2_index, f1_power, f2_power;
	char bend_down = 0;
	int bend;

	if (!sens)
		return 1024;
	wheel = (int) pitchbend - 8192;
	sensitivity = ((int) sens * wheel) / 128;
	if (sensitivity < 0) {
		bend_down = 1;
		sensitivity = -sensitivity;
	}
	semitones = (unsigned int) (sensitivity >> 13);
	mantissa = sensitivity % 8192;
	f1_index = semitones % 12;
	f2_index = (semitones + 1) % 12;
	f1_power = semitones / 12;
	f2_power = (semitones + 1) / 12;
	f1 = log_table[f1_index] << f1_power;
	f2 = log_table[f2_index] << f2_power;
	bend = (int) ((((f2 - f1) * mantissa) >> 13) + f1);
	if (bend_down)
		bend = 1048576L / bend;
	return bend;
}

unsigned short snd_gf1_compute_freq(unsigned int freq,
				    unsigned int rate,
				    unsigned short mix_rate)
{
	unsigned int fc;
	int scale = 0;

	while (freq >= 4194304L) {
		scale++;
		freq >>= 1;
	}
	fc = (freq << 10) / rate;
	if (fc > 97391L) {
		fc = 97391;
		snd_printk(KERN_ERR "patch: (1) fc frequency overflow - %u\n", fc);
	}
	fc = (fc * 44100UL) / mix_rate;
	while (scale--)
		fc <<= 1;
	if (fc > 65535L) {
		fc = 65535;
		snd_printk(KERN_ERR "patch: (2) fc frequency overflow - %u\n", fc);
	}
	return (unsigned short) fc;
}

#endif  /*  0  */
