/*
 *   ALSA Driver for the PT2258 volume controller.
 *
 *	Copyright (c) 2006  Jochen Voss <voss@seehuhn.de>
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

#ifndef __SOUND_PT2258_H
#define __SOUND_PT2258_H

struct snd_pt2258 {
	struct snd_card *card;
	struct snd_i2c_bus *i2c_bus;
	struct snd_i2c_device *i2c_dev;

	unsigned char volume[6];
	int mute;
};

extern int snd_pt2258_reset(struct snd_pt2258 *pt);
extern int snd_pt2258_build_controls(struct snd_pt2258 *pt);

#endif /* __SOUND_PT2258_H */
