/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.2
 *  by Intel Corporation (http://developer.intel.com).
 */

void snd_ac97_get_name(struct snd_ac97 *ac97, unsigned int id, char *name,
		       size_t maxlen, int modem);
int snd_ac97_update_bits_nolock(struct snd_ac97 *ac97, unsigned short reg,
				unsigned short mask, unsigned short value);

/* ac97_proc.c */
#ifdef CONFIG_SND_PROC_FS
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
