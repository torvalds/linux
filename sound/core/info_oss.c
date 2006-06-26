/*
 *  Information interface for ALSA driver
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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

#include <sound/driver.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <sound/version.h>
#include <linux/utsname.h>
#include <linux/mutex.h>

#if defined(CONFIG_SND_OSSEMUL) && defined(CONFIG_PROC_FS)

/*
 *  OSS compatible part
 */

static DEFINE_MUTEX(strings);
static char *snd_sndstat_strings[SNDRV_CARDS][SNDRV_OSS_INFO_DEV_COUNT];
static struct snd_info_entry *snd_sndstat_proc_entry;

int snd_oss_info_register(int dev, int num, char *string)
{
	char *x;

	snd_assert(dev >= 0 && dev < SNDRV_OSS_INFO_DEV_COUNT, return -ENXIO);
	snd_assert(num >= 0 && num < SNDRV_CARDS, return -ENXIO);
	mutex_lock(&strings);
	if (string == NULL) {
		if ((x = snd_sndstat_strings[num][dev]) != NULL) {
			kfree(x);
			x = NULL;
		}
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

extern void snd_card_info_read_oss(struct snd_info_buffer *buffer);

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
	snd_iprintf(buffer, "Sound Driver:3.8.1a-980706 (ALSA v" CONFIG_SND_VERSION " emulation code)\n");
	snd_iprintf(buffer, "Kernel: %s %s %s %s %s\n",
		    system_utsname.sysname,
		    system_utsname.nodename,
		    system_utsname.release,
		    system_utsname.version,
		    system_utsname.machine);
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

int snd_info_minor_register(void)
{
	struct snd_info_entry *entry;

	memset(snd_sndstat_strings, 0, sizeof(snd_sndstat_strings));
	if ((entry = snd_info_create_module_entry(THIS_MODULE, "sndstat", snd_oss_root)) != NULL) {
		entry->c.text.read = snd_sndstat_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	snd_sndstat_proc_entry = entry;
	return 0;
}

int snd_info_minor_unregister(void)
{
	if (snd_sndstat_proc_entry) {
		snd_info_unregister(snd_sndstat_proc_entry);
		snd_sndstat_proc_entry = NULL;
	}
	return 0;
}

#endif /* CONFIG_SND_OSSEMUL */
