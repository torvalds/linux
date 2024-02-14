// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Advanced Linux Sound Architecture
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/init.h>
#include <sound/core.h>

static int __init alsa_sound_last_init(void)
{
	struct snd_card *card;
	int idx, ok = 0;
	
	printk(KERN_INFO "ALSA device list:\n");
	for (idx = 0; idx < SNDRV_CARDS; idx++) {
		card = snd_card_ref(idx);
		if (card) {
			printk(KERN_INFO "  #%i: %s\n", idx, card->longname);
			snd_card_unref(card);
			ok++;
		}
	}
	if (ok == 0)
		printk(KERN_INFO "  No soundcards found.\n");
	return 0;
}

late_initcall_sync(alsa_sound_last_init);
