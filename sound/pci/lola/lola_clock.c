/*
 *  Support for Digigram Lola PCI-e boards
 *
 *  Copyright (c) 2011 Takashi Iwai <tiwai@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "lola.h"

unsigned int lola_sample_rate_convert(unsigned int coded)
{
	unsigned int freq;

	/* base frequency */
	switch (coded & 0x3) {
	case 0:     freq = 48000; break;
	case 1:     freq = 44100; break;
	case 2:     freq = 32000; break;
	default:    return 0;   /* error */
	}

	/* multiplier / devisor */
	switch (coded & 0x1c) {
	case (0 << 2):    break;
	case (4 << 2):    break;
	case (1 << 2):    freq *= 2; break;
	case (2 << 2):    freq *= 4; break;
	case (5 << 2):    freq /= 2; break;
	case (6 << 2):    freq /= 4; break;
	default:        return 0;   /* error */
	}

	/* ajustement */
	switch (coded & 0x60) {
	case (0 << 5):    break;
	case (1 << 5):    freq = (freq * 999) / 1000; break;
	case (2 << 5):    freq = (freq * 1001) / 1000; break;
	default:        return 0;   /* error */
	}
	return freq;
}

/*
 * Granualrity
 */

#define LOLA_MAXFREQ_AT_GRANULARITY_MIN         48000
#define LOLA_MAXFREQ_AT_GRANULARITY_BELOW_MAX   96000

static bool check_gran_clock_compatibility(struct lola *chip,
					   unsigned int val,
					   unsigned int freq)
{
	if (!chip->granularity)
		return true;

	if (val < LOLA_GRANULARITY_MIN || val > LOLA_GRANULARITY_MAX ||
	    (val % LOLA_GRANULARITY_STEP) != 0)
		return false;

	if (val == LOLA_GRANULARITY_MIN) {
		if (freq > LOLA_MAXFREQ_AT_GRANULARITY_MIN)
			return false;
	} else if (val < LOLA_GRANULARITY_MAX) {
		if (freq > LOLA_MAXFREQ_AT_GRANULARITY_BELOW_MAX)
			return false;
	}
	return true;
}

int lola_set_granularity(struct lola *chip, unsigned int val, bool force)
{
	int err;

	if (!force) {
		if (val == chip->granularity)
			return 0;
#if 0
		/* change Gran only if there are no streams allocated ! */
		if (chip->audio_in_alloc_mask || chip->audio_out_alloc_mask)
			return -EBUSY;
#endif
		if (!check_gran_clock_compatibility(chip, val,
						    chip->clock.cur_freq))
			return -EINVAL;
	}

	chip->granularity = val;
	val /= LOLA_GRANULARITY_STEP;

	/* audio function group */
	err = lola_codec_write(chip, 1, LOLA_VERB_SET_GRANULARITY_STEPS,
			       val, 0);
	if (err < 0)
		return err;
	/* this can be a very slow function !!! */
	usleep_range(400 * val, 20000);
	return lola_codec_flush(chip);
}

/*
 * Clock widget handling
 */

int lola_init_clock_widget(struct lola *chip, int nid)
{
	unsigned int val;
	int i, j, nitems, nb_verbs, idx, idx_list;
	int err;

	err = lola_read_param(chip, nid, LOLA_PAR_AUDIO_WIDGET_CAP, &val);
	if (err < 0) {
		printk(KERN_ERR SFX "Can't read wcaps for 0x%x\n", nid);
		return err;
	}

	if ((val & 0xfff00000) != 0x01f00000) { /* test SubType and Type */
		snd_printdd("No valid clock widget\n");
		return 0;
	}

	chip->clock.nid = nid;
	chip->clock.items = val & 0xff;
	snd_printdd("clock_list nid=%x, entries=%d\n", nid,
		    chip->clock.items);
	if (chip->clock.items > MAX_SAMPLE_CLOCK_COUNT) {
		printk(KERN_ERR SFX "CLOCK_LIST too big: %d\n",
		       chip->clock.items);
		return -EINVAL;
	}

	nitems = chip->clock.items;
	nb_verbs = (nitems + 3) / 4;
	idx = 0;
	idx_list = 0;
	for (i = 0; i < nb_verbs; i++) {
		unsigned int res_ex;
		unsigned short items[4];

		err = lola_codec_read(chip, nid, LOLA_VERB_GET_CLOCK_LIST,
				      idx, 0, &val, &res_ex);
		if (err < 0) {
			printk(KERN_ERR SFX "Can't read CLOCK_LIST\n");
			return -EINVAL;
		}

		items[0] = val & 0xfff;
		items[1] = (val >> 16) & 0xfff;
		items[2] = res_ex & 0xfff;
		items[3] = (res_ex >> 16) & 0xfff;

		for (j = 0; j < 4; j++) {
			unsigned char type = items[j] >> 8;
			unsigned int freq = items[j] & 0xff;
			int format = LOLA_CLOCK_FORMAT_NONE;
			bool add_clock = true;
			if (type == LOLA_CLOCK_TYPE_INTERNAL) {
				freq = lola_sample_rate_convert(freq);
				if (freq < chip->sample_rate_min)
					add_clock = false;
				else if (freq == 48000) {
					chip->clock.cur_index = idx_list;
					chip->clock.cur_freq = 48000;
					chip->clock.cur_valid = true;
				}
			} else if (type == LOLA_CLOCK_TYPE_VIDEO) {
				freq = lola_sample_rate_convert(freq);
				if (freq < chip->sample_rate_min)
					add_clock = false;
				/* video clock has a format (0:NTSC, 1:PAL)*/
				if (items[j] & 0x80)
					format = LOLA_CLOCK_FORMAT_NTSC;
				else
					format = LOLA_CLOCK_FORMAT_PAL;
			}
			if (add_clock) {
				struct lola_sample_clock *sc;
				sc = &chip->clock.sample_clock[idx_list];
				sc->type = type;
				sc->format = format;
				sc->freq = freq;
				/* keep the index used with the board */
				chip->clock.idx_lookup[idx_list] = idx;
				idx_list++;
			} else {
				chip->clock.items--;
			}
			if (++idx >= nitems)
				break;
		}
	}
	return 0;
}

/* enable unsolicited events of the clock widget */
int lola_enable_clock_events(struct lola *chip)
{
	unsigned int res;
	int err;

	err = lola_codec_read(chip, chip->clock.nid,
			      LOLA_VERB_SET_UNSOLICITED_ENABLE,
			      LOLA_UNSOLICITED_ENABLE | LOLA_UNSOLICITED_TAG,
			      0, &res, NULL);
	if (err < 0)
		return err;
	if (res) {
		printk(KERN_WARNING SFX "error in enable_clock_events %d\n",
		       res);
		return -EINVAL;
	}
	return 0;
}

int lola_set_clock_index(struct lola *chip, unsigned int idx)
{
	unsigned int res;
	int err;

	err = lola_codec_read(chip, chip->clock.nid,
			      LOLA_VERB_SET_CLOCK_SELECT,
			      chip->clock.idx_lookup[idx],
			      0, &res, NULL);
	if (err < 0)
		return err;
	if (res) {
		printk(KERN_WARNING SFX "error in set_clock %d\n", res);
		return -EINVAL;
	}
	return 0;
}

bool lola_update_ext_clock_freq(struct lola *chip, unsigned int val)
{
	unsigned int tag;

	/* the current EXTERNAL clock information gets updated by interrupt
	 * with an unsolicited response
	 */
	if (!val)
		return false;
	tag = (val >> LOLA_UNSOL_RESP_TAG_OFFSET) & LOLA_UNSOLICITED_TAG_MASK;
	if (tag != LOLA_UNSOLICITED_TAG)
		return false;

	/* only for current = external clocks */
	if (chip->clock.sample_clock[chip->clock.cur_index].type !=
	    LOLA_CLOCK_TYPE_INTERNAL) {
		chip->clock.cur_freq = lola_sample_rate_convert(val & 0x7f);
		chip->clock.cur_valid = (val & 0x100) != 0;
	}
	return true;
}

int lola_set_clock(struct lola *chip, int idx)
{
	int freq = 0;
	bool valid = false;

	if (idx == chip->clock.cur_index) {
		/* current clock is allowed */
		freq = chip->clock.cur_freq;
		valid = chip->clock.cur_valid;
	} else if (chip->clock.sample_clock[idx].type ==
		   LOLA_CLOCK_TYPE_INTERNAL) {
		/* internal clocks allowed */
		freq = chip->clock.sample_clock[idx].freq;
		valid = true;
	}

	if (!freq || !valid)
		return -EINVAL;

	if (!check_gran_clock_compatibility(chip, chip->granularity, freq))
		return -EINVAL;

	if (idx != chip->clock.cur_index) {
		int err = lola_set_clock_index(chip, idx);
		if (err < 0)
			return err;
		/* update new settings */
		chip->clock.cur_index = idx;
		chip->clock.cur_freq = freq;
		chip->clock.cur_valid = true;
	}
	return 0;
}

int lola_set_sample_rate(struct lola *chip, int rate)
{
	int i;

	if (chip->clock.cur_freq == rate && chip->clock.cur_valid)
		return 0;
	/* search for new dwClockIndex */
	for (i = 0; i < chip->clock.items; i++) {
		if (chip->clock.sample_clock[i].type == LOLA_CLOCK_TYPE_INTERNAL &&
		    chip->clock.sample_clock[i].freq == rate)
			break;
	}
	if (i >= chip->clock.items)
		return -EINVAL;
	return lola_set_clock(chip, i);
}

