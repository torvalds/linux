/*
 *  Routines for Trident 4DWave NX/DX soundcards - Synthesizer
 *  Copyright (c) by Scott McNab <jedi@tartarus.uwa.edu.au>
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
#include <asm/io.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/trident.h>
#include <sound/seq_device.h>

MODULE_AUTHOR("Scott McNab <jedi@tartarus.uwa.edu.au>");
MODULE_DESCRIPTION("Routines for Trident 4DWave NX/DX soundcards - Synthesizer");
MODULE_LICENSE("GPL");

/* linear to log pan conversion table (4.2 channel attenuation format) */
static unsigned int pan_table[63] = {
	7959, 7733, 7514, 7301, 7093, 6892, 6697, 6507, 
	6322, 6143, 5968, 5799, 5634, 5475, 5319, 5168, 
	5022, 4879, 4741, 4606, 4475, 4349, 4225, 4105, 
	3989, 3876, 3766, 3659, 3555, 3454, 3356, 3261, 
	3168, 3078, 2991, 2906, 2824, 2744, 2666, 2590, 
	2517, 2445, 2376, 2308, 2243, 2179, 2117, 2057, 
	1999, 1942, 1887, 1833, 1781, 1731, 1682, 1634, 
	1588, 1543, 1499, 1456, 1415, 1375, 1336
};

#define LOG_TABLE_SIZE 386

/* Linear half-attenuation to log conversion table in the format:
 *   {linear volume, logarithmic attenuation equivalent}, ...
 *
 * Provides conversion from a linear half-volume value in the range
 * [0,8192] to a logarithmic attenuation value in the range 0 to 6.02dB.
 * Halving the linear volume is equivalent to an additional 6dB of 
 * logarithmic attenuation. The algorithm used in log_from_linear()
 * therefore uses this table as follows:
 * 
 * - loop and for every time the volume is less than half the maximum 
 *   volume (16384), add another 6dB and halve the maximum value used
 *   for this comparison.
 * - when the volume is greater than half the maximum volume, take
 *   the difference of the volume to half volume (in the range [0,8192])
 *   and look up the log_table[] to find the nearest entry.
 * - take the logarithic component of this entry and add it to the 
 *   resulting attenuation.
 *
 * Thus this routine provides a linear->log conversion for a range of
 * [0,16384] using only 386 table entries
 *
 * Note: although this table stores log attenuation in 8.8 format, values
 * were only calculated for 6 bits fractional precision, since that is
 * the most precision offered by the trident hardware.
 */

static unsigned short log_table[LOG_TABLE_SIZE*2] =
{
	4, 0x0604, 19, 0x0600, 34, 0x05fc, 
	49, 0x05f8, 63, 0x05f4, 78, 0x05f0, 93, 0x05ec, 108, 0x05e8, 
	123, 0x05e4, 138, 0x05e0, 153, 0x05dc, 168, 0x05d8, 183, 0x05d4, 
	198, 0x05d0, 213, 0x05cc, 228, 0x05c8, 244, 0x05c4, 259, 0x05c0, 
	274, 0x05bc, 289, 0x05b8, 304, 0x05b4, 320, 0x05b0, 335, 0x05ac, 
	350, 0x05a8, 366, 0x05a4, 381, 0x05a0, 397, 0x059c, 412, 0x0598, 
	428, 0x0594, 443, 0x0590, 459, 0x058c, 474, 0x0588, 490, 0x0584, 
	506, 0x0580, 521, 0x057c, 537, 0x0578, 553, 0x0574, 568, 0x0570, 
	584, 0x056c, 600, 0x0568, 616, 0x0564, 632, 0x0560, 647, 0x055c, 
	663, 0x0558, 679, 0x0554, 695, 0x0550, 711, 0x054c, 727, 0x0548, 
	743, 0x0544, 759, 0x0540, 776, 0x053c, 792, 0x0538, 808, 0x0534, 
	824, 0x0530, 840, 0x052c, 857, 0x0528, 873, 0x0524, 889, 0x0520, 
	906, 0x051c, 922, 0x0518, 938, 0x0514, 955, 0x0510, 971, 0x050c, 
	988, 0x0508, 1004, 0x0504, 1021, 0x0500, 1037, 0x04fc, 1054, 0x04f8, 
	1071, 0x04f4, 1087, 0x04f0, 1104, 0x04ec, 1121, 0x04e8, 1138, 0x04e4, 
	1154, 0x04e0, 1171, 0x04dc, 1188, 0x04d8, 1205, 0x04d4, 1222, 0x04d0, 
	1239, 0x04cc, 1256, 0x04c8, 1273, 0x04c4, 1290, 0x04c0, 1307, 0x04bc, 
	1324, 0x04b8, 1341, 0x04b4, 1358, 0x04b0, 1376, 0x04ac, 1393, 0x04a8, 
	1410, 0x04a4, 1427, 0x04a0, 1445, 0x049c, 1462, 0x0498, 1479, 0x0494, 
	1497, 0x0490, 1514, 0x048c, 1532, 0x0488, 1549, 0x0484, 1567, 0x0480, 
	1584, 0x047c, 1602, 0x0478, 1620, 0x0474, 1637, 0x0470, 1655, 0x046c, 
	1673, 0x0468, 1690, 0x0464, 1708, 0x0460, 1726, 0x045c, 1744, 0x0458, 
	1762, 0x0454, 1780, 0x0450, 1798, 0x044c, 1816, 0x0448, 1834, 0x0444, 
	1852, 0x0440, 1870, 0x043c, 1888, 0x0438, 1906, 0x0434, 1924, 0x0430, 
	1943, 0x042c, 1961, 0x0428, 1979, 0x0424, 1997, 0x0420, 2016, 0x041c, 
	2034, 0x0418, 2053, 0x0414, 2071, 0x0410, 2089, 0x040c, 2108, 0x0408, 
	2127, 0x0404, 2145, 0x0400, 2164, 0x03fc, 2182, 0x03f8, 2201, 0x03f4, 
	2220, 0x03f0, 2239, 0x03ec, 2257, 0x03e8, 2276, 0x03e4, 2295, 0x03e0, 
	2314, 0x03dc, 2333, 0x03d8, 2352, 0x03d4, 2371, 0x03d0, 2390, 0x03cc, 
	2409, 0x03c8, 2428, 0x03c4, 2447, 0x03c0, 2466, 0x03bc, 2485, 0x03b8, 
	2505, 0x03b4, 2524, 0x03b0, 2543, 0x03ac, 2562, 0x03a8, 2582, 0x03a4, 
	2601, 0x03a0, 2621, 0x039c, 2640, 0x0398, 2660, 0x0394, 2679, 0x0390, 
	2699, 0x038c, 2718, 0x0388, 2738, 0x0384, 2758, 0x0380, 2777, 0x037c, 
	2797, 0x0378, 2817, 0x0374, 2837, 0x0370, 2857, 0x036c, 2876, 0x0368, 
	2896, 0x0364, 2916, 0x0360, 2936, 0x035c, 2956, 0x0358, 2976, 0x0354, 
	2997, 0x0350, 3017, 0x034c, 3037, 0x0348, 3057, 0x0344, 3077, 0x0340, 
	3098, 0x033c, 3118, 0x0338, 3138, 0x0334, 3159, 0x0330, 3179, 0x032c, 
	3200, 0x0328, 3220, 0x0324, 3241, 0x0320, 3261, 0x031c, 3282, 0x0318, 
	3303, 0x0314, 3323, 0x0310, 3344, 0x030c, 3365, 0x0308, 3386, 0x0304, 
	3406, 0x0300, 3427, 0x02fc, 3448, 0x02f8, 3469, 0x02f4, 3490, 0x02f0, 
	3511, 0x02ec, 3532, 0x02e8, 3553, 0x02e4, 3575, 0x02e0, 3596, 0x02dc, 
	3617, 0x02d8, 3638, 0x02d4, 3660, 0x02d0, 3681, 0x02cc, 3702, 0x02c8, 
	3724, 0x02c4, 3745, 0x02c0, 3767, 0x02bc, 3788, 0x02b8, 3810, 0x02b4, 
	3831, 0x02b0, 3853, 0x02ac, 3875, 0x02a8, 3896, 0x02a4, 3918, 0x02a0, 
	3940, 0x029c, 3962, 0x0298, 3984, 0x0294, 4006, 0x0290, 4028, 0x028c, 
	4050, 0x0288, 4072, 0x0284, 4094, 0x0280, 4116, 0x027c, 4138, 0x0278, 
	4160, 0x0274, 4182, 0x0270, 4205, 0x026c, 4227, 0x0268, 4249, 0x0264, 
	4272, 0x0260, 4294, 0x025c, 4317, 0x0258, 4339, 0x0254, 4362, 0x0250, 
	4384, 0x024c, 4407, 0x0248, 4430, 0x0244, 4453, 0x0240, 4475, 0x023c, 
	4498, 0x0238, 4521, 0x0234, 4544, 0x0230, 4567, 0x022c, 4590, 0x0228, 
	4613, 0x0224, 4636, 0x0220, 4659, 0x021c, 4682, 0x0218, 4705, 0x0214, 
	4728, 0x0210, 4752, 0x020c, 4775, 0x0208, 4798, 0x0204, 4822, 0x0200, 
	4845, 0x01fc, 4869, 0x01f8, 4892, 0x01f4, 4916, 0x01f0, 4939, 0x01ec, 
	4963, 0x01e8, 4987, 0x01e4, 5010, 0x01e0, 5034, 0x01dc, 5058, 0x01d8, 
	5082, 0x01d4, 5106, 0x01d0, 5130, 0x01cc, 5154, 0x01c8, 5178, 0x01c4, 
	5202, 0x01c0, 5226, 0x01bc, 5250, 0x01b8, 5274, 0x01b4, 5299, 0x01b0, 
	5323, 0x01ac, 5347, 0x01a8, 5372, 0x01a4, 5396, 0x01a0, 5420, 0x019c, 
	5445, 0x0198, 5469, 0x0194, 5494, 0x0190, 5519, 0x018c, 5543, 0x0188, 
	5568, 0x0184, 5593, 0x0180, 5618, 0x017c, 5643, 0x0178, 5668, 0x0174, 
	5692, 0x0170, 5717, 0x016c, 5743, 0x0168, 5768, 0x0164, 5793, 0x0160, 
	5818, 0x015c, 5843, 0x0158, 5868, 0x0154, 5894, 0x0150, 5919, 0x014c, 
	5945, 0x0148, 5970, 0x0144, 5995, 0x0140, 6021, 0x013c, 6047, 0x0138, 
	6072, 0x0134, 6098, 0x0130, 6124, 0x012c, 6149, 0x0128, 6175, 0x0124, 
	6201, 0x0120, 6227, 0x011c, 6253, 0x0118, 6279, 0x0114, 6305, 0x0110, 
	6331, 0x010c, 6357, 0x0108, 6384, 0x0104, 6410, 0x0100, 6436, 0x00fc, 
	6462, 0x00f8, 6489, 0x00f4, 6515, 0x00f0, 6542, 0x00ec, 6568, 0x00e8, 
	6595, 0x00e4, 6621, 0x00e0, 6648, 0x00dc, 6675, 0x00d8, 6702, 0x00d4, 
	6728, 0x00d0, 6755, 0x00cc, 6782, 0x00c8, 6809, 0x00c4, 6836, 0x00c0, 
	6863, 0x00bc, 6890, 0x00b8, 6917, 0x00b4, 6945, 0x00b0, 6972, 0x00ac, 
	6999, 0x00a8, 7027, 0x00a4, 7054, 0x00a0, 7081, 0x009c, 7109, 0x0098, 
	7136, 0x0094, 7164, 0x0090, 7192, 0x008c, 7219, 0x0088, 7247, 0x0084, 
	7275, 0x0080, 7303, 0x007c, 7331, 0x0078, 7359, 0x0074, 7387, 0x0070, 
	7415, 0x006c, 7443, 0x0068, 7471, 0x0064, 7499, 0x0060, 7527, 0x005c, 
	7556, 0x0058, 7584, 0x0054, 7613, 0x0050, 7641, 0x004c, 7669, 0x0048, 
	7698, 0x0044, 7727, 0x0040, 7755, 0x003c, 7784, 0x0038, 7813, 0x0034, 
	7842, 0x0030, 7870, 0x002c, 7899, 0x0028, 7928, 0x0024, 7957, 0x0020, 
	7986, 0x001c, 8016, 0x0018, 8045, 0x0014, 8074, 0x0010, 8103, 0x000c, 
	8133, 0x0008, 8162, 0x0004, 8192, 0x0000
};

static unsigned short lookup_volume_table( unsigned short value )
{
	/* This code is an optimised version of:
	 *   int i = 0;
	 *   while( volume_table[i*2] < value )
	 *       i++;
	 *   return volume_table[i*2+1];
	 */
	unsigned short *ptr = log_table;
	while( *ptr < value )
		ptr += 2;
	return *(ptr+1);
}

/* this function calculates a 8.8 fixed point logarithmic attenuation
 * value from a linear volume value in the range 0 to 16384 */
static unsigned short log_from_linear( unsigned short value )
{
	if (value >= 16384)
		return 0x0000;
	if (value) {
		unsigned short result = 0;
		int v, c;
		for( c = 0, v = 8192; c < 14; c++, v >>= 1 ) {
			if( value >= v ) {
				result += lookup_volume_table( (value - v) << c );
				return result;
			}
			result += 0x0605;	/* 6.0205 (result of -20*log10(0.5)) */
		}
	}
	return 0xffff;
}

/*
 * Sample handling operations
 */

static void sample_start(trident_t * trident, snd_trident_voice_t * voice, snd_seq_position_t position);
static void sample_stop(trident_t * trident, snd_trident_voice_t * voice, snd_seq_stop_mode_t mode);
static void sample_freq(trident_t * trident, snd_trident_voice_t * voice, snd_seq_frequency_t freq);
static void sample_volume(trident_t * trident, snd_trident_voice_t * voice, snd_seq_ev_volume_t * volume);
static void sample_loop(trident_t * trident, snd_trident_voice_t * voice, snd_seq_ev_loop_t * loop);
static void sample_pos(trident_t * trident, snd_trident_voice_t * voice, snd_seq_position_t position);
static void sample_private1(trident_t * trident, snd_trident_voice_t * voice, unsigned char *data);

static snd_trident_sample_ops_t sample_ops =
{
	sample_start,
	sample_stop,
	sample_freq,
	sample_volume,
	sample_loop,
	sample_pos,
	sample_private1
};

static void snd_trident_simple_init(snd_trident_voice_t * voice)
{
	//voice->handler_wave = interrupt_wave;
	//voice->handler_volume = interrupt_volume;
	//voice->handler_effect = interrupt_effect;
	//voice->volume_change = NULL;
	voice->sample_ops = &sample_ops;
}

static void sample_start(trident_t * trident, snd_trident_voice_t * voice, snd_seq_position_t position)
{
	simple_instrument_t *simple;
	snd_seq_kinstr_t *instr;
	unsigned long flags;
	unsigned int loop_start, loop_end, sample_start, sample_end, start_offset;
	unsigned int value;
	unsigned int shift = 0;

	instr = snd_seq_instr_find(trident->synth.ilist, &voice->instr, 0, 1);
	if (instr == NULL)
		return;
	voice->instr = instr->instr;	/* copy ID to speedup aliases */
	simple = KINSTR_DATA(instr);

	spin_lock_irqsave(&trident->reg_lock, flags);

	if (trident->device == TRIDENT_DEVICE_ID_SI7018)
		voice->GVSel = 1;	/* route to Wave volume */

	voice->CTRL = 0;
	voice->Alpha = 0;
	voice->FMS = 0;

	loop_start = simple->loop_start >> 4;
	loop_end = simple->loop_end >> 4;
	sample_start = (simple->start + position) >> 4;
	if( sample_start >= simple->size )
		sample_start = simple->start >> 4;
	sample_end = simple->size;
	start_offset = position >> 4;

	if (simple->format & SIMPLE_WAVE_16BIT) {
		voice->CTRL |= 8;
		shift++;
	}
	if (simple->format & SIMPLE_WAVE_STEREO) {
		voice->CTRL |= 4;
		shift++;
	}
	if (!(simple->format & SIMPLE_WAVE_UNSIGNED))
		voice->CTRL |= 2;

	voice->LBA = simple->address.memory;

	if (simple->format & SIMPLE_WAVE_LOOP) {
		voice->CTRL |= 1;
		voice->LBA += loop_start << shift;
		if( start_offset >= loop_start ) {
			voice->CSO = start_offset - loop_start;
			voice->negCSO = 0;
		} else {
			voice->CSO = loop_start - start_offset;
			voice->negCSO = 1;
		}
		voice->ESO = loop_end - loop_start - 1;
	} else {
		voice->LBA += start_offset << shift;
		voice->CSO = sample_start;
		voice->ESO = sample_end - 1;
		voice->negCSO = 0;
	}

	if (voice->flags & SNDRV_TRIDENT_VFLG_RUNNING) {
		snd_trident_stop_voice(trident, voice->number);
		voice->flags &= ~SNDRV_TRIDENT_VFLG_RUNNING;
	}

	/* set CSO sign */
	value = inl(TRID_REG(trident, T4D_SIGN_CSO_A));
	if( voice->negCSO ) {
		value |= 1 << (voice->number&31);
	} else {
		value &= ~(1 << (voice->number&31));
	}
	outl(value,TRID_REG(trident, T4D_SIGN_CSO_A));

	voice->Attribute = 0;	
	snd_trident_write_voice_regs(trident, voice);
	snd_trident_start_voice(trident, voice->number);
	voice->flags |= SNDRV_TRIDENT_VFLG_RUNNING;
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	snd_seq_instr_free_use(trident->synth.ilist, instr);
}

static void sample_stop(trident_t * trident, snd_trident_voice_t * voice, snd_seq_stop_mode_t mode)
{
	unsigned long flags;

	if (!(voice->flags & SNDRV_TRIDENT_VFLG_RUNNING))
		return;

	switch (mode) {
	default:
		spin_lock_irqsave(&trident->reg_lock, flags);
		snd_trident_stop_voice(trident, voice->number);
		voice->flags &= ~SNDRV_TRIDENT_VFLG_RUNNING;
		spin_unlock_irqrestore(&trident->reg_lock, flags);
		break;
	case SAMPLE_STOP_LOOP:	/* disable loop only */
		voice->CTRL &= ~1;
		spin_lock_irqsave(&trident->reg_lock, flags);
		outb((unsigned char) voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
		outw((((voice->CTRL << 12) | (voice->EC & 0x0fff)) & 0xffff), CH_GVSEL_PAN_VOL_CTRL_EC);
		spin_unlock_irqrestore(&trident->reg_lock, flags);
		break;
	}
}

static void sample_freq(trident_t * trident, snd_trident_voice_t * voice, snd_seq_frequency_t freq)
{
	unsigned long flags;
	freq >>= 4;

	spin_lock_irqsave(&trident->reg_lock, flags);
	if (freq == 44100)
		voice->Delta = 0xeb3;
	else if (freq == 8000)
		voice->Delta = 0x2ab;
	else if (freq == 48000)
		voice->Delta = 0x1000;
	else
		voice->Delta = (((freq << 12) + freq) / 48000) & 0x0000ffff;

	outb((unsigned char) voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	if (trident->device == TRIDENT_DEVICE_ID_NX) {
		outb((unsigned char) voice->Delta, TRID_REG(trident, CH_NX_DELTA_CSO + 3));
		outb((unsigned char) (voice->Delta >> 8), TRID_REG(trident, CH_NX_DELTA_ESO + 3));
	} else {
		outw((unsigned short) voice->Delta, TRID_REG(trident, CH_DX_ESO_DELTA));
	}

	spin_unlock_irqrestore(&trident->reg_lock, flags);
}

static void sample_volume(trident_t * trident, snd_trident_voice_t * voice, snd_seq_ev_volume_t * volume)
{
	unsigned long flags;
	unsigned short value;

	spin_lock_irqsave(&trident->reg_lock, flags);
	voice->GVSel = 0;	/* use global music volume */
	voice->FMC = 0x03;	/* fixme: can we do something useful with FMC? */
	if (volume->volume >= 0) {
		volume->volume &= 0x3fff;
		/* linear volume -> logarithmic attenuation conversion
		 * uses EC register for greater resolution (6.6 bits) than Vol register (5.3 bits)
		 * Vol register used when additional attenuation is required */
		voice->RVol = 0;
		voice->CVol = 0;
		value = log_from_linear( volume->volume );
		voice->Vol = 0;
		voice->EC = (value & 0x3fff) >> 2;
		if (value > 0x3fff) {
			voice->EC |= 0xfc0;
			if (value < 0x5f00 )
				voice->Vol = ((value >> 8) - 0x3f) << 5;
			else {
				voice->Vol = 0x3ff;
				voice->EC = 0xfff;
			}
		}
	}
	if (volume->lr >= 0) {
		volume->lr &= 0x3fff;
		/* approximate linear pan by attenuating channels */
		if (volume->lr >= 0x2000) {	/* attenuate left (pan right) */
			value = 0x3fff - volume->lr;
			for (voice->Pan = 0; voice->Pan < 63; voice->Pan++ ) 
				if (value >= pan_table[voice->Pan] )
					break;
		} else {			/* attenuate right (pan left) */
			for (voice->Pan = 0; voice->Pan < 63; voice->Pan++ ) 
				if ((unsigned int)volume->lr >= pan_table[voice->Pan] )
					break;
			voice->Pan |= 0x40;
		}
	}
	outb((unsigned char) voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	outl((voice->GVSel << 31) | ((voice->Pan & 0x0000007f) << 24) |
		 ((voice->Vol & 0x000000ff) << 16) | ((voice->CTRL & 0x0000000f) << 12) |
		 (voice->EC & 0x00000fff), TRID_REG(trident, CH_GVSEL_PAN_VOL_CTRL_EC));
	value = ((voice->FMC & 0x03) << 14) | ((voice->RVol & 0x7f) << 7) | (voice->CVol & 0x7f);
	outw(value, TRID_REG(trident, CH_DX_FMC_RVOL_CVOL));
	spin_unlock_irqrestore(&trident->reg_lock, flags);
}

static void sample_loop(trident_t * trident, snd_trident_voice_t * voice, snd_seq_ev_loop_t * loop)
{
	unsigned long flags;
	simple_instrument_t *simple;
	snd_seq_kinstr_t *instr;
	unsigned int loop_start, loop_end;

	instr = snd_seq_instr_find(trident->synth.ilist, &voice->instr, 0, 1);
	if (instr == NULL)
		return;
	voice->instr = instr->instr;	/* copy ID to speedup aliases */
	simple = KINSTR_DATA(instr);

	loop_start = loop->start >> 4;
	loop_end = loop->end >> 4;

	spin_lock_irqsave(&trident->reg_lock, flags);

	voice->LBA = simple->address.memory + loop_start;
	voice->CSO = 0;
	voice->ESO = loop_end - loop_start - 1;

	outb((unsigned char) voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	outb((voice->LBA >> 16), TRID_REG(trident, CH_LBA + 2));
	outw((voice->LBA & 0xffff), TRID_REG(trident, CH_LBA));
	if (trident->device == TRIDENT_DEVICE_ID_NX) {
		outb((voice->ESO >> 16), TRID_REG(trident, CH_NX_DELTA_ESO + 2));
		outw((voice->ESO & 0xffff), TRID_REG(trident, CH_NX_DELTA_ESO));
		outb((voice->CSO >> 16), TRID_REG(trident, CH_NX_DELTA_CSO + 2));
		outw((voice->CSO & 0xffff), TRID_REG(trident, CH_NX_DELTA_CSO));
	} else {
		outw((voice->ESO & 0xffff), TRID_REG(trident, CH_DX_ESO_DELTA + 2));
		outw((voice->CSO & 0xffff), TRID_REG(trident, CH_DX_CSO_ALPHA_FMS + 2));
	}

	spin_unlock_irqrestore(&trident->reg_lock, flags);
	snd_seq_instr_free_use(trident->synth.ilist, instr);
}

static void sample_pos(trident_t * trident, snd_trident_voice_t * voice, snd_seq_position_t position)
{
	unsigned long flags;
	simple_instrument_t *simple;
	snd_seq_kinstr_t *instr;
	unsigned int value;

	instr = snd_seq_instr_find(trident->synth.ilist, &voice->instr, 0, 1);
	if (instr == NULL)
		return;
	voice->instr = instr->instr;	/* copy ID to speedup aliases */
	simple = KINSTR_DATA(instr);

	spin_lock_irqsave(&trident->reg_lock, flags);

	if (simple->format & SIMPLE_WAVE_LOOP) {
		if( position >= simple->loop_start ) {
			voice->CSO = (position - simple->loop_start) >> 4;
			voice->negCSO = 0;
		} else {
			voice->CSO = (simple->loop_start - position) >> 4;
			voice->negCSO = 1;
		}
	} else {
		voice->CSO = position >> 4;
		voice->negCSO = 0;
	}

	/* set CSO sign */
	value = inl(TRID_REG(trident, T4D_SIGN_CSO_A));
	if( voice->negCSO ) {
		value |= 1 << (voice->number&31);
	} else {
		value &= ~(1 << (voice->number&31));
	}
	outl(value,TRID_REG(trident, T4D_SIGN_CSO_A));
	

	outb((unsigned char) voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	if (trident->device == TRIDENT_DEVICE_ID_NX) {
		outw((voice->CSO & 0xffff), TRID_REG(trident, CH_NX_DELTA_CSO));
		outb((voice->CSO >> 16), TRID_REG(trident, CH_NX_DELTA_CSO + 2));
	} else {
		outw((voice->CSO & 0xffff), TRID_REG(trident, CH_DX_CSO_ALPHA_FMS) + 2);
	}

	spin_unlock_irqrestore(&trident->reg_lock, flags);
	snd_seq_instr_free_use(trident->synth.ilist, instr);
}

static void sample_private1(trident_t * trident, snd_trident_voice_t * voice, unsigned char *data)
{
}

/*
 * Memory management / sample loading
 */

static int snd_trident_simple_put_sample(void *private_data, simple_instrument_t * instr,
					 char __user *data, long len, int atomic)
{
	trident_t *trident = private_data;
	int size = instr->size;
	int shift = 0;

	if (instr->format & SIMPLE_WAVE_BACKWARD ||
	    instr->format & SIMPLE_WAVE_BIDIR ||
	    instr->format & SIMPLE_WAVE_ULAW) 
		return -EINVAL;	/* not supported */

	if (instr->format & SIMPLE_WAVE_16BIT)
		shift++;
	if (instr->format & SIMPLE_WAVE_STEREO)
		shift++;
	size <<= shift;

	if (trident->synth.current_size + size > trident->synth.max_size)
		return -ENOMEM;

	if (!access_ok(VERIFY_READ, data, size))
		return -EFAULT;

	if (trident->tlb.entries) {
		snd_util_memblk_t *memblk;
		memblk = snd_trident_synth_alloc(trident, size); 
		if (memblk == NULL)
			return -ENOMEM;
		if (snd_trident_synth_copy_from_user(trident, memblk, 0, data, size) ) {
			snd_trident_synth_free(trident, memblk);
			return -EFAULT;
		}
		instr->address.ptr = (unsigned char*)memblk;
		instr->address.memory = memblk->offset;
	} else {
		struct snd_dma_buffer dmab;
		if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(trident->pci),
					size, &dmab) < 0)
			return -ENOMEM;

		if (copy_from_user(dmab.area, data, size)) {
			snd_dma_free_pages(&dmab);
			return -EFAULT;
		}
		instr->address.ptr = dmab.area;
		instr->address.memory = dmab.addr;
	}

	trident->synth.current_size += size;
	return 0;
}

static int snd_trident_simple_get_sample(void *private_data, simple_instrument_t * instr,
					 char __user *data, long len, int atomic)
{
	//trident_t *trident = private_data;
	int size = instr->size;
	int shift = 0;

	if (instr->format & SIMPLE_WAVE_16BIT)
		shift++;
	if (instr->format & SIMPLE_WAVE_STEREO)
		shift++;
	size <<= shift;

	if (!access_ok(VERIFY_WRITE, data, size))
		return -EFAULT;

	/* FIXME: not implemented yet */

	return -EBUSY;
}

static int snd_trident_simple_remove_sample(void *private_data, simple_instrument_t * instr,
					    int atomic)
{
	trident_t *trident = private_data;
	int size = instr->size;

	if (instr->format & SIMPLE_WAVE_16BIT)
		size <<= 1;
	if (instr->format & SIMPLE_WAVE_STEREO)
		size <<= 1;

	if (trident->tlb.entries) {
		snd_util_memblk_t *memblk = (snd_util_memblk_t*)instr->address.ptr;
		if (memblk)
			snd_trident_synth_free(trident, memblk);
		else
			return -EFAULT;
	} else {
		struct snd_dma_buffer dmab;
		dmab.dev.type = SNDRV_DMA_TYPE_DEV;
		dmab.dev.dev = snd_dma_pci_data(trident->pci);
		dmab.area = instr->address.ptr;
		dmab.addr = instr->address.memory;
		dmab.bytes = size;
		snd_dma_free_pages(&dmab);
	}

	trident->synth.current_size -= size;
	if (trident->synth.current_size < 0)	/* shouldn't need this check... */
		trident->synth.current_size = 0;

	return 0;
}

static void select_instrument(trident_t * trident, snd_trident_voice_t * v)
{
	snd_seq_kinstr_t *instr;
	instr = snd_seq_instr_find(trident->synth.ilist, &v->instr, 0, 1);
	if (instr != NULL) {
		if (instr->ops) {
			if (!strcmp(instr->ops->instr_type, SNDRV_SEQ_INSTR_ID_SIMPLE))
				snd_trident_simple_init(v);
		}
		snd_seq_instr_free_use(trident->synth.ilist, instr);
	}
}

/*

 */

static void event_sample(snd_seq_event_t * ev, snd_trident_port_t * p, snd_trident_voice_t * v)
{
	if (v->sample_ops && v->sample_ops->sample_stop)
		v->sample_ops->sample_stop(p->trident, v, SAMPLE_STOP_IMMEDIATELY);
	v->instr.std = ev->data.sample.param.sample.std;
	if (v->instr.std & 0xff000000) {	/* private instrument */
		v->instr.std &= 0x00ffffff;
		v->instr.std |= (unsigned int)ev->source.client << 24;
	}
	v->instr.bank = ev->data.sample.param.sample.bank;
	v->instr.prg = ev->data.sample.param.sample.prg;
	select_instrument(p->trident, v);
}

static void event_cluster(snd_seq_event_t * ev, snd_trident_port_t * p, snd_trident_voice_t * v)
{
	if (v->sample_ops && v->sample_ops->sample_stop)
		v->sample_ops->sample_stop(p->trident, v, SAMPLE_STOP_IMMEDIATELY);
	v->instr.cluster = ev->data.sample.param.cluster.cluster;
	select_instrument(p->trident, v);
}

static void event_start(snd_seq_event_t * ev, snd_trident_port_t * p, snd_trident_voice_t * v)
{
	if (v->sample_ops && v->sample_ops->sample_start)
		v->sample_ops->sample_start(p->trident, v, ev->data.sample.param.position);
}

static void event_stop(snd_seq_event_t * ev, snd_trident_port_t * p, snd_trident_voice_t * v)
{
	if (v->sample_ops && v->sample_ops->sample_stop)
		v->sample_ops->sample_stop(p->trident, v, ev->data.sample.param.stop_mode);
}

static void event_freq(snd_seq_event_t * ev, snd_trident_port_t * p, snd_trident_voice_t * v)
{
	if (v->sample_ops && v->sample_ops->sample_freq)
		v->sample_ops->sample_freq(p->trident, v, ev->data.sample.param.frequency);
}

static void event_volume(snd_seq_event_t * ev, snd_trident_port_t * p, snd_trident_voice_t * v)
{
	if (v->sample_ops && v->sample_ops->sample_volume)
		v->sample_ops->sample_volume(p->trident, v, &ev->data.sample.param.volume);
}

static void event_loop(snd_seq_event_t * ev, snd_trident_port_t * p, snd_trident_voice_t * v)
{
	if (v->sample_ops && v->sample_ops->sample_loop)
		v->sample_ops->sample_loop(p->trident, v, &ev->data.sample.param.loop);
}

static void event_position(snd_seq_event_t * ev, snd_trident_port_t * p, snd_trident_voice_t * v)
{
	if (v->sample_ops && v->sample_ops->sample_pos)
		v->sample_ops->sample_pos(p->trident, v, ev->data.sample.param.position);
}

static void event_private1(snd_seq_event_t * ev, snd_trident_port_t * p, snd_trident_voice_t * v)
{
	if (v->sample_ops && v->sample_ops->sample_private1)
		v->sample_ops->sample_private1(p->trident, v, (unsigned char *) &ev->data.sample.param.raw8);
}

typedef void (trident_sample_event_handler_t) (snd_seq_event_t * ev, snd_trident_port_t * p, snd_trident_voice_t * v);

static trident_sample_event_handler_t *trident_sample_event_handlers[9] =
{
	event_sample,
	event_cluster,
	event_start,
	event_stop,
	event_freq,
	event_volume,
	event_loop,
	event_position,
	event_private1
};

static void snd_trident_sample_event(snd_seq_event_t * ev, snd_trident_port_t * p)
{
	int idx, voice;
	trident_t *trident = p->trident;
	snd_trident_voice_t *v;
	unsigned long flags;

	idx = ev->type - SNDRV_SEQ_EVENT_SAMPLE;
	if (idx < 0 || idx > 8)
		return;
	for (voice = 0; voice < 64; voice++) {
		v = &trident->synth.voices[voice];
		if (v->use && v->client == ev->source.client &&
		    v->port == ev->source.port &&
		    v->index == ev->data.sample.channel) {
			spin_lock_irqsave(&trident->event_lock, flags);
			trident_sample_event_handlers[idx] (ev, p, v);
			spin_unlock_irqrestore(&trident->event_lock, flags);
			return;
		}
	}
}

/*

 */

static void snd_trident_synth_free_voices(trident_t * trident, int client, int port)
{
	int idx;
	snd_trident_voice_t *voice;

	for (idx = 0; idx < 32; idx++) {
		voice = &trident->synth.voices[idx];
		if (voice->use && voice->client == client && voice->port == port)
			snd_trident_free_voice(trident, voice);
	}
}

static int snd_trident_synth_use(void *private_data, snd_seq_port_subscribe_t * info)
{
	snd_trident_port_t *port = (snd_trident_port_t *) private_data;
	trident_t *trident = port->trident;
	snd_trident_voice_t *voice;
	unsigned int idx;
	unsigned long flags;

	if (info->voices > 32)
		return -EINVAL;
	spin_lock_irqsave(&trident->reg_lock, flags);
	for (idx = 0; idx < info->voices; idx++) {
		voice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_SYNTH, info->sender.client, info->sender.port);
		if (voice == NULL) {
			snd_trident_synth_free_voices(trident, info->sender.client, info->sender.port);
			spin_unlock_irqrestore(&trident->reg_lock, flags);
			return -EBUSY;
		}
		voice->index = idx;
		voice->Vol = 0x3ff;
		voice->EC = 0x0fff;
	}
#if 0
	for (idx = 0; idx < info->midi_voices; idx++) {
		port->midi_has_voices = 1;
		voice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_MIDI, info->sender.client, info->sender.port);
		if (voice == NULL) {
			snd_trident_synth_free_voices(trident, info->sender.client, info->sender.port);
			spin_unlock_irqrestore(&trident->reg_lock, flags);
			return -EBUSY;
		}
		voice->Vol = 0x3ff;
		voice->EC = 0x0fff;
	}
#endif
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return 0;
}

static int snd_trident_synth_unuse(void *private_data, snd_seq_port_subscribe_t * info)
{
	snd_trident_port_t *port = (snd_trident_port_t *) private_data;
	trident_t *trident = port->trident;
	unsigned long flags;

	spin_lock_irqsave(&trident->reg_lock, flags);
	snd_trident_synth_free_voices(trident, info->sender.client, info->sender.port);
	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return 0;
}

/*

 */

static void snd_trident_synth_free_private_instruments(snd_trident_port_t * p, int client)
{
	snd_seq_instr_header_t ifree;

	memset(&ifree, 0, sizeof(ifree));
	ifree.cmd = SNDRV_SEQ_INSTR_FREE_CMD_PRIVATE;
	snd_seq_instr_list_free_cond(p->trident->synth.ilist, &ifree, client, 0);
}

static int snd_trident_synth_event_input(snd_seq_event_t * ev, int direct, void *private_data, int atomic, int hop)
{
	snd_trident_port_t *p = (snd_trident_port_t *) private_data;

	if (p == NULL)
		return -EINVAL;
	if (ev->type >= SNDRV_SEQ_EVENT_SAMPLE &&
	    ev->type <= SNDRV_SEQ_EVENT_SAMPLE_PRIVATE1) {
		snd_trident_sample_event(ev, p);
		return 0;
	}
	if (ev->source.client == SNDRV_SEQ_CLIENT_SYSTEM &&
	    ev->source.port == SNDRV_SEQ_PORT_SYSTEM_ANNOUNCE) {
		if (ev->type == SNDRV_SEQ_EVENT_CLIENT_EXIT) {
			snd_trident_synth_free_private_instruments(p, ev->data.addr.client);
			return 0;
		}
	}
	if (direct) {
		if (ev->type >= SNDRV_SEQ_EVENT_INSTR_BEGIN) {
			snd_seq_instr_event(&p->trident->synth.simple_ops.kops,
					    p->trident->synth.ilist, ev,
					    p->trident->synth.seq_client, atomic, hop);
			return 0;
		}
	}
	return 0;
}

static void snd_trident_synth_instr_notify(void *private_data,
					   snd_seq_kinstr_t * instr,
					   int what)
{
	int idx;
	trident_t *trident = private_data;
	snd_trident_voice_t *pvoice;
	unsigned long flags;

	spin_lock_irqsave(&trident->event_lock, flags);
	for (idx = 0; idx < 64; idx++) {
		pvoice = &trident->synth.voices[idx];
		if (pvoice->use && !memcmp(&pvoice->instr, &instr->instr, sizeof(pvoice->instr))) {
			if (pvoice->sample_ops && pvoice->sample_ops->sample_stop) {
				pvoice->sample_ops->sample_stop(trident, pvoice, SAMPLE_STOP_IMMEDIATELY);
			} else {
				snd_trident_stop_voice(trident, pvoice->number);
				pvoice->flags &= ~SNDRV_TRIDENT_VFLG_RUNNING;
			}
		}
	}
	spin_unlock_irqrestore(&trident->event_lock, flags);
}

/*

 */

static void snd_trident_synth_free_port(void *private_data)
{
	snd_trident_port_t *p = (snd_trident_port_t *) private_data;

	if (p)
		snd_midi_channel_free_set(p->chset);
}

static int snd_trident_synth_create_port(trident_t * trident, int idx)
{
	snd_trident_port_t *p;
	snd_seq_port_callback_t callbacks;
	char name[32];
	char *str;
	int result;

	p = &trident->synth.seq_ports[idx];
	p->chset = snd_midi_channel_alloc_set(16);
	if (p->chset == NULL)
		return -ENOMEM;
	p->chset->private_data = p;
	p->trident = trident;
	p->client = trident->synth.seq_client;

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.owner = THIS_MODULE;
	callbacks.use = snd_trident_synth_use;
	callbacks.unuse = snd_trident_synth_unuse;
	callbacks.event_input = snd_trident_synth_event_input;
	callbacks.private_free = snd_trident_synth_free_port;
	callbacks.private_data = p;

	str = "???";
	switch (trident->device) {
	case TRIDENT_DEVICE_ID_DX:	str = "Trident 4DWave-DX"; break;
	case TRIDENT_DEVICE_ID_NX:	str = "Trident 4DWave-NX"; break;
	case TRIDENT_DEVICE_ID_SI7018:	str = "SiS 7018"; break;
	}
	sprintf(name, "%s port %i", str, idx);
	p->chset->port = snd_seq_event_port_attach(trident->synth.seq_client,
						   &callbacks,
						   SNDRV_SEQ_PORT_CAP_WRITE | SNDRV_SEQ_PORT_CAP_SUBS_WRITE,
						   SNDRV_SEQ_PORT_TYPE_DIRECT_SAMPLE |
						   SNDRV_SEQ_PORT_TYPE_SYNTH,
						   16, 0,
						   name);
	if (p->chset->port < 0) {
		result = p->chset->port;
		snd_trident_synth_free_port(p);
		return result;
	}
	p->port = p->chset->port;
	return 0;
}

/*

 */

static int snd_trident_synth_new_device(snd_seq_device_t *dev)
{
	trident_t *trident;
	int client, i;
	snd_seq_client_callback_t callbacks;
	snd_seq_client_info_t cinfo;
	snd_seq_port_subscribe_t sub;
	snd_simple_ops_t *simpleops;
	char *str;

	trident = *(trident_t **)SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (trident == NULL)
		return -EINVAL;

	trident->synth.seq_client = -1;

	/* allocate new client */
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.private_data = trident;
	callbacks.allow_output = callbacks.allow_input = 1;
	client = trident->synth.seq_client =
	    snd_seq_create_kernel_client(trident->card, 1, &callbacks);
	if (client < 0)
		return client;

	/* change name of client */
	memset(&cinfo, 0, sizeof(cinfo));
	cinfo.client = client;
	cinfo.type = KERNEL_CLIENT;
	str = "???";
	switch (trident->device) {
	case TRIDENT_DEVICE_ID_DX:	str = "Trident 4DWave-DX"; break;
	case TRIDENT_DEVICE_ID_NX:	str = "Trident 4DWave-NX"; break;
	case TRIDENT_DEVICE_ID_SI7018:	str = "SiS 7018"; break;
	}
	sprintf(cinfo.name, str);
	snd_seq_kernel_client_ctl(client, SNDRV_SEQ_IOCTL_SET_CLIENT_INFO, &cinfo);

	for (i = 0; i < 4; i++)
		snd_trident_synth_create_port(trident, i);

	trident->synth.ilist = snd_seq_instr_list_new();
	if (trident->synth.ilist == NULL) {
		snd_seq_delete_kernel_client(client);
		trident->synth.seq_client = -1;
		return -ENOMEM;
	}
	trident->synth.ilist->flags = SNDRV_SEQ_INSTR_FLG_DIRECT;

	simpleops = &trident->synth.simple_ops;
	snd_seq_simple_init(simpleops, trident, NULL);
	simpleops->put_sample = snd_trident_simple_put_sample;
	simpleops->get_sample = snd_trident_simple_get_sample;
	simpleops->remove_sample = snd_trident_simple_remove_sample;
	simpleops->notify = snd_trident_synth_instr_notify;

	memset(&sub, 0, sizeof(sub));
	sub.sender.client = SNDRV_SEQ_CLIENT_SYSTEM;
	sub.sender.port = SNDRV_SEQ_PORT_SYSTEM_ANNOUNCE;
	sub.dest.client = client;
	sub.dest.port = 0;
	snd_seq_kernel_client_ctl(client, SNDRV_SEQ_IOCTL_SUBSCRIBE_PORT, &sub);

	return 0;
}

static int snd_trident_synth_delete_device(snd_seq_device_t *dev)
{
	trident_t *trident;

	trident = *(trident_t **)SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (trident == NULL)
		return -EINVAL;

	if (trident->synth.seq_client >= 0) {
		snd_seq_delete_kernel_client(trident->synth.seq_client);
		trident->synth.seq_client = -1;
	}
	if (trident->synth.ilist)
		snd_seq_instr_list_free(&trident->synth.ilist);
	return 0;
}

static int __init alsa_trident_synth_init(void)
{
	static snd_seq_dev_ops_t ops =
	{
		snd_trident_synth_new_device,
		snd_trident_synth_delete_device
	};

	return snd_seq_device_register_driver(SNDRV_SEQ_DEV_ID_TRIDENT, &ops,
					      sizeof(trident_t*));
}

static void __exit alsa_trident_synth_exit(void)
{
	snd_seq_device_unregister_driver(SNDRV_SEQ_DEV_ID_TRIDENT);
}

module_init(alsa_trident_synth_init)
module_exit(alsa_trident_synth_exit)
