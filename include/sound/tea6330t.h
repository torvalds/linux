/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_TEA6330T_H
#define __SOUND_TEA6330T_H

/*
 *  Routines for control of TEA6330T circuit.
 *  Sound fader control circuit for car radios.
 */

#include <sound/i2c.h>		/* generic i2c support */

int snd_tea6330t_detect(struct snd_i2c_bus *bus, int equalizer);
int snd_tea6330t_update_mixer(struct snd_card *card, struct snd_i2c_bus *bus,
			      int equalizer, int fader);

#endif /* __SOUND_TEA6330T_H */
