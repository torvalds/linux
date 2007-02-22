/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.2
 *  by Intel Corporation (http://developer.intel.com).
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

void snd_ac97_get_name(struct snd_ac97 *ac97, unsigned int id, char *name,
		       int modem);
int snd_ac97_update_bits_nolock(struct snd_ac97 *ac97, unsigned short reg,
				unsigned short mask, unsigned short value);

/* ac97_proc.c */
#ifdef CONFIG_PROC_FS
void snd_ac97_bus_proc_init(struct snd_ac97_bus * ac97);
void snd_ac97_bus_proc_done(struct snd_ac97_bus * ac97);
void snd_ac97_proc_init(struct snd_ac97 * ac97);
void snd_ac97_proc_done(struct snd_ac97 * ac97);
#else
#define snd_ac97_bus_proc_init(ac97_bus_t) do { } while (0)
#define snd_ac97_bus_proc_done(ac97_bus_t) do { } while (0)
#define snd_ac97_proc_init(ac97_t) do { } while (0)
#define snd_ac97_proc_done(ac97_t) do { } while (0)
#endif
