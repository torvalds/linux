// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Information interface for ALSA driver
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/slab.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/export.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <linux/utsname.h>
#include <linux/mutex.h>

/*
 *  OSS compatible part
 */

static DEFINE_MUTEX(strings);
static char *snd_sndstat_strings[SNDRV_CARDS][SNDRV_OSS_INFO_DEV_COUNT];

int snd_oss_info_register(int dev, int num, char *string)
{
	char *x;

	if (snd_BUG_ON(dev < 0 || dev >= SNDRV_OSS_INFO_DEV_COUNT))
		return -ENXIO;
	if (snd_BUG_ON(num < 0 || num >= SNDRV_CARDS))
		return -ENXIO;
	mutex_lock(&strings);
	if (string == NULL) {
		x = snd_sndstat_strings[num][dev];
		kfree(x);
		x = NULL;
	} else {
		x = kstrdup(string, GFP_KERNEL);
		if (x == NULL) {
			mutex_unlock(&strings);
			return -ENOMEM;
		}
	}
	snd_sndstat_strings[num][dev] = x;
	mutex_unlock(&strings);
	return 0;
}
EXPORT_SYMBOL(snd_oss_info_register);

static int snd_sndstat_show_strings(struct snd_info_buffer *buf, char *id, int dev)
{
	int idx, ok = -1;
	char *str;

	snd_iprintf(buf, "\n%s:", id);
	mutex_lock(&strings);
	for (idx = 0; idx < SNDRV_CARDS; idx++) {
		str = snd_sndstat_strings[idx][dev];
		if (str) {
			if (ok < 0) {
				snd_iprintf(buf, "\n");
				ok++;
			}
			snd_iprintf(buf, "%i: %s\n", idx, str);
		}
	}
	mutex_unlock(&strings);
	if (ok < 0)
		snd_iprintf(buf, " NOT ENABLED IN CONFIG\n");
	return ok;
}

static void snd_sndstat_proc_read(struct snd_info_entry *entry,
				  struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "Sound Driver:3.8.1a-980706 (ALSA emulation code)\n");
	snd_iprintf(buffer, "Kernel: %s %s %s %s %s\n",
		    init_utsname()->sysname,
		    init_utsname()->nodename,
		    init_utsname()->release,
		    init_utsname()->version,
		    init_utsname()->machine);
	snd_iprintf(buffer, "Config options: 0\n");
	snd_iprintf(buffer, "\nInstalled drivers: \n");
	snd_iprintf(buffer, "Type 10: ALSA emulation\n");
	snd_iprintf(buffer, "\nCard config: \n");
	snd_card_info_read_oss(buffer);
	snd_sndstat_show_strings(buffer, "Audio devices", SNDRV_OSS_INFO_DEV_AUDIO);
	snd_sndstat_show_strings(buffer, "Synth devices", SNDRV_OSS_INFO_DEV_SYNTH);
	snd_sndstat_show_strings(buffer, "Midi devices", SNDRV_OSS_INFO_DEV_MIDI);
	snd_sndstat_show_strings(buffer, "Timers", SNDRV_OSS_INFO_DEV_TIMERS);
	snd_sndstat_show_strings(buffer, "Mixers", SNDRV_OSS_INFO_DEV_MIXERS);
}

int __init snd_info_minor_register(void)
{
	struct snd_info_entry *entry;

	memset(snd_sndstat_strings, 0, sizeof(snd_sndstat_strings));
	entry = snd_info_create_module_entry(THIS_MODULE, "sndstat",
					     snd_oss_root);
	if (!entry)
		return -ENOMEM;
	entry->c.text.read = snd_sndstat_proc_read;
	return snd_info_register(entry); /* freed in error path */
}
