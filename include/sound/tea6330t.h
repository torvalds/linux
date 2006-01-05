#ifndef __SOUND_TEA6330T_H
#define __SOUND_TEA6330T_H

/*
 *  Routines for control of TEA6330T circuit.
 *  Sound fader control circuit for car radios.
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
 *
 */

#include "i2c.h"		/* generic i2c support */

int snd_tea6330t_detect(struct snd_i2c_bus *bus, int equalizer);
int snd_tea6330t_update_mixer(struct snd_card *card, struct snd_i2c_bus *bus,
			      int equalizer, int fader);

#endif /* __SOUND_TEA6330T_H */
