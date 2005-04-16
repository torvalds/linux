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

#include "control.h"
#include "i2c.h"		/* generic i2c support */

typedef struct {
	snd_i2c_device_t *device;
	snd_i2c_bus_t *bus;
	int equalizer;
	int fader;
	unsigned char regs[8];
	unsigned char mleft, mright;
	unsigned char bass, treble;
	unsigned char max_bass, max_treble;
} tea6330t_t;

extern int snd_tea6330t_detect(snd_i2c_bus_t *bus, int equalizer);
extern int snd_tea6330t_update_mixer(snd_card_t * card, snd_i2c_bus_t * bus, int equalizer, int fader);

#endif /* __SOUND_TEA6330T_H */
