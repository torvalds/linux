#ifndef __SOUND_OPL4_H
#define __SOUND_OPL4_H

/*
 * Global definitions for the OPL4 driver
 * Copyright (c) 2003 by Clemens Ladisch <clemens@ladisch.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <sound/opl3.h>

struct snd_opl4;

extern int snd_opl4_create(struct snd_card *card,
			   unsigned long fm_port, unsigned long pcm_port,
			   int seq_device,
			   struct snd_opl3 **opl3, struct snd_opl4 **opl4);

#endif /* __SOUND_OPL4_H */
