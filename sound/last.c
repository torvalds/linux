/*
 *  Advanced Linux Sound Architecture
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

#define SNDRV_MAIN_OBJECT_FILE
#include <sound/driver.h>
#include <linux/init.h>
#include <sound/core.h>

static int __init alsa_sound_last_init(void)
{
	int idx, ok = 0;
	
	printk(KERN_INFO "ALSA device list:\n");
	for (idx = 0; idx < SNDRV_CARDS; idx++)
		if (snd_cards[idx] != NULL) {
			printk(KERN_INFO "  #%i: %s\n", idx, snd_cards[idx]->longname);
			ok++;
		}
	if (ok == 0)
		printk(KERN_INFO "  No soundcards found.\n");
	return 0;
}

__initcall(alsa_sound_last_init);
