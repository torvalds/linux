// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OSS compatible sequencer driver
 *
 * synth device handlers
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
 */

#include "seq_oss_synth.h"
#include "seq_oss_midi.h"
#include "../seq_lock.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/nospec.h>

/*
 * constants
 */
#define SNDRV_SEQ_OSS_MAX_SYNTH_NAME	30
#define MAX_SYSEX_BUFLEN		128


/*
 * definition of synth info records
 */

/* synth info */
struct seq_oss_synth {
	int seq_device;

	/* for synth_info */
	int synth_type;
	int synth_subtype;
	int nr_voices;

	char name[SNDRV_SEQ_OSS_MAX_SYNTH_NAME];
	struct snd_seq_oss_callback oper;

	int opened;

	void *private_data;
	snd_use_lock_t use_lock;
};

DEFINE_FREE(seq_oss_synth, struct seq_oss_synth *, if (!IS_ERR_OR_NULL(_T)) snd_use_lock_free(&(_T)->use_lock))

/*
 * device table
 */
static int max_synth_devs;
static struct seq_oss_synth *synth_devs[SNDRV_SEQ_OSS_MAX_SYNTH_DEVS];
static struct seq_oss_synth midi_synth_dev = {
	.seq_device = -1,
	.synth_type = SYNTH_TYPE_MIDI,
	.synth_subtype = 0,
	.nr_voices = 16,
	.name = "MIDI",
};

static DEFINE_SPINLOCK(register_lock);

/*
 * prototypes
 */
static struct seq_oss_synth *get_synthdev(struct seq_oss_devinfo *dp, int dev);
static void reset_channels(struct seq_oss_synthinfo *info);

/*
 * global initialization
 */
void __init
snd_seq_oss_synth_init(void)
{
	snd_use_lock_init(&midi_synth_dev.use_lock);
}

/*
 * registration of the synth device
 */
int
snd_seq_oss_synth_probe(struct device *_dev)
{
	struct snd_seq_device *dev = to_seq_dev(_dev);
	int i;
	struct seq_oss_synth *rec;
	struct snd_seq_oss_reg *reg = SNDRV_SEQ_DEVICE_ARGPTR(dev);

	rec = kzalloc(sizeof(*rec), GFP_KERNEL);
	if (!rec)
		return -ENOMEM;
	rec->seq_device = -1;
	rec->synth_type = reg->type;
	rec->synth_subtype = reg->subtype;
	rec->nr_voices = reg->nvoices;
	rec->oper = reg->oper;
	rec->private_data = reg->private_data;
	rec->opened = 0;
	snd_use_lock_init(&rec->use_lock);

	/* copy and truncate the name of synth device */
	strscpy(rec->name, dev->name, sizeof(rec->name));

	/* registration */
	scoped_guard(spinlock_irqsave, &register_lock) {
		for (i = 0; i < max_synth_devs; i++) {
			if (synth_devs[i] == NULL)
				break;
		}
		if (i >= max_synth_devs) {
			if (max_synth_devs >= SNDRV_SEQ_OSS_MAX_SYNTH_DEVS) {
				pr_err("ALSA: seq_oss: no more synth slot\n");
				kfree(rec);
				return -ENOMEM;
			}
			max_synth_devs++;
		}
		rec->seq_device = i;
		synth_devs[i] = rec;
	}
	dev->driver_data = rec;
#ifdef SNDRV_OSS_INFO_DEV_SYNTH
	if (i < SNDRV_CARDS)
		snd_oss_info_register(SNDRV_OSS_INFO_DEV_SYNTH, i, rec->name);
#endif
	return 0;
}


int
snd_seq_oss_synth_remove(struct device *_dev)
{
	struct snd_seq_device *dev = to_seq_dev(_dev);
	int index;
	struct seq_oss_synth *rec = dev->driver_data;

	scoped_guard(spinlock_irqsave, &register_lock) {
		for (index = 0; index < max_synth_devs; index++) {
			if (synth_devs[index] == rec)
				break;
		}
		if (index >= max_synth_devs) {
			pr_err("ALSA: seq_oss: can't unregister synth\n");
			return -EINVAL;
		}
		synth_devs[index] = NULL;
		if (index == max_synth_devs - 1) {
			for (index--; index >= 0; index--) {
				if (synth_devs[index])
					break;
			}
			max_synth_devs = index + 1;
		}
	}
#ifdef SNDRV_OSS_INFO_DEV_SYNTH
	if (rec->seq_device < SNDRV_CARDS)
		snd_oss_info_unregister(SNDRV_OSS_INFO_DEV_SYNTH, rec->seq_device);
#endif

	snd_use_lock_sync(&rec->use_lock);
	kfree(rec);

	return 0;
}


/*
 */
static struct seq_oss_synth *
get_sdev(int dev)
{
	struct seq_oss_synth *rec;

	guard(spinlock_irqsave)(&register_lock);
	rec = synth_devs[dev];
	if (rec)
		snd_use_lock_use(&rec->use_lock);
	return rec;
}


/*
 * set up synth tables
 */

void
snd_seq_oss_synth_setup(struct seq_oss_devinfo *dp)
{
	int i;
	struct seq_oss_synthinfo *info;

	dp->max_synthdev = max_synth_devs;
	dp->synth_opened = 0;
	memset(dp->synths, 0, sizeof(dp->synths));
	for (i = 0; i < dp->max_synthdev; i++) {
		struct seq_oss_synth *rec __free(seq_oss_synth) = get_sdev(i);

		if (rec == NULL)
			continue;
		if (rec->oper.open == NULL || rec->oper.close == NULL)
			continue;
		info = &dp->synths[i];
		info->arg.app_index = dp->port;
		info->arg.file_mode = dp->file_mode;
		info->arg.seq_mode = dp->seq_mode;
		if (dp->seq_mode == SNDRV_SEQ_OSS_MODE_SYNTH)
			info->arg.event_passing = SNDRV_SEQ_OSS_PROCESS_EVENTS;
		else
			info->arg.event_passing = SNDRV_SEQ_OSS_PASS_EVENTS;
		info->opened = 0;
		if (!try_module_get(rec->oper.owner))
			continue;
		if (rec->oper.open(&info->arg, rec->private_data) < 0) {
			module_put(rec->oper.owner);
			continue;
		}
		info->nr_voices = rec->nr_voices;
		if (info->nr_voices > 0) {
			info->ch = kcalloc(info->nr_voices, sizeof(struct seq_oss_chinfo), GFP_KERNEL);
			if (!info->ch) {
				rec->oper.close(&info->arg);
				module_put(rec->oper.owner);
				continue;
			}
			reset_channels(info);
		}
		info->opened++;
		rec->opened++;
		dp->synth_opened++;
	}
}


/*
 * set up synth tables for MIDI emulation - /dev/music mode only
 */

void
snd_seq_oss_synth_setup_midi(struct seq_oss_devinfo *dp)
{
	int i;

	if (dp->max_synthdev >= SNDRV_SEQ_OSS_MAX_SYNTH_DEVS)
		return;

	for (i = 0; i < dp->max_mididev; i++) {
		struct seq_oss_synthinfo *info;
		info = &dp->synths[dp->max_synthdev];
		if (snd_seq_oss_midi_open(dp, i, dp->file_mode) < 0)
			continue;
		info->arg.app_index = dp->port;
		info->arg.file_mode = dp->file_mode;
		info->arg.seq_mode = dp->seq_mode;
		info->arg.private_data = info;
		info->is_midi = 1;
		info->midi_mapped = i;
		info->arg.event_passing = SNDRV_SEQ_OSS_PASS_EVENTS;
		snd_seq_oss_midi_get_addr(dp, i, &info->arg.addr);
		info->opened = 1;
		midi_synth_dev.opened++;
		dp->max_synthdev++;
		if (dp->max_synthdev >= SNDRV_SEQ_OSS_MAX_SYNTH_DEVS)
			break;
	}
}


/*
 * clean up synth tables
 */

void
snd_seq_oss_synth_cleanup(struct seq_oss_devinfo *dp)
{
	int i;
	struct seq_oss_synthinfo *info;

	if (snd_BUG_ON(dp->max_synthdev > SNDRV_SEQ_OSS_MAX_SYNTH_DEVS))
		return;
	for (i = 0; i < dp->max_synthdev; i++) {
		info = &dp->synths[i];
		if (! info->opened)
			continue;
		if (info->is_midi) {
			if (midi_synth_dev.opened > 0) {
				snd_seq_oss_midi_close(dp, info->midi_mapped);
				midi_synth_dev.opened--;
			}
		} else {
			struct seq_oss_synth *rec __free(seq_oss_synth) =
				get_sdev(i);

			if (rec == NULL)
				continue;
			if (rec->opened > 0) {
				rec->oper.close(&info->arg);
				module_put(rec->oper.owner);
				rec->opened = 0;
			}
		}
		kfree(info->ch);
		info->ch = NULL;
	}
	dp->synth_opened = 0;
	dp->max_synthdev = 0;
}

static struct seq_oss_synthinfo *
get_synthinfo_nospec(struct seq_oss_devinfo *dp, int dev)
{
	if (dev < 0 || dev >= dp->max_synthdev)
		return NULL;
	dev = array_index_nospec(dev, SNDRV_SEQ_OSS_MAX_SYNTH_DEVS);
	return &dp->synths[dev];
}

/*
 * return synth device information pointer
 */
static struct seq_oss_synth *
get_synthdev(struct seq_oss_devinfo *dp, int dev)
{
	struct seq_oss_synth *rec;
	struct seq_oss_synthinfo *info = get_synthinfo_nospec(dp, dev);

	if (!info)
		return NULL;
	if (!info->opened)
		return NULL;
	if (info->is_midi) {
		rec = &midi_synth_dev;
		snd_use_lock_use(&rec->use_lock);
	} else {
		rec = get_sdev(dev);
		if (!rec)
			return NULL;
	}
	if (! rec->opened) {
		snd_use_lock_free(&rec->use_lock);
		return NULL;
	}
	return rec;
}


/*
 * reset note and velocity on each channel.
 */
static void
reset_channels(struct seq_oss_synthinfo *info)
{
	int i;
	if (info->ch == NULL || ! info->nr_voices)
		return;
	for (i = 0; i < info->nr_voices; i++) {
		info->ch[i].note = -1;
		info->ch[i].vel = 0;
	}
}


/*
 * reset synth device:
 * call reset callback.  if no callback is defined, send a heartbeat
 * event to the corresponding port.
 */
void
snd_seq_oss_synth_reset(struct seq_oss_devinfo *dp, int dev)
{
	struct seq_oss_synth *rec __free(seq_oss_synth) = NULL;
	struct seq_oss_synthinfo *info;

	info = get_synthinfo_nospec(dp, dev);
	if (!info || !info->opened)
		return;
	reset_channels(info);
	if (info->is_midi) {
		if (midi_synth_dev.opened <= 0)
			return;
		snd_seq_oss_midi_reset(dp, info->midi_mapped);
		/* reopen the device */
		snd_seq_oss_midi_close(dp, dev);
		if (snd_seq_oss_midi_open(dp, info->midi_mapped,
					  dp->file_mode) < 0) {
			midi_synth_dev.opened--;
			info->opened = 0;
			kfree(info->ch);
			info->ch = NULL;
		}
		return;
	}

	rec = get_sdev(dev);
	if (rec == NULL)
		return;
	if (rec->oper.reset) {
		rec->oper.reset(&info->arg);
	} else {
		struct snd_seq_event ev;
		memset(&ev, 0, sizeof(ev));
		snd_seq_oss_fill_addr(dp, &ev, info->arg.addr.client,
				      info->arg.addr.port);
		ev.type = SNDRV_SEQ_EVENT_RESET;
		snd_seq_oss_dispatch(dp, &ev, 0, 0);
	}
}


/*
 * load a patch record:
 * call load_patch callback function
 */
int
snd_seq_oss_synth_load_patch(struct seq_oss_devinfo *dp, int dev, int fmt,
			    const char __user *buf, int p, int c)
{
	struct seq_oss_synth *rec __free(seq_oss_synth) = NULL;
	struct seq_oss_synthinfo *info;

	info = get_synthinfo_nospec(dp, dev);
	if (!info)
		return -ENXIO;

	if (info->is_midi)
		return 0;
	rec = get_synthdev(dp, dev);
	if (!rec)
		return -ENXIO;

	if (rec->oper.load_patch == NULL)
		return -ENXIO;
	else
		return rec->oper.load_patch(&info->arg, fmt, buf, p, c);
}

/*
 * check if the device is valid synth device and return the synth info
 */
struct seq_oss_synthinfo *
snd_seq_oss_synth_info(struct seq_oss_devinfo *dp, int dev)
{
	struct seq_oss_synth *rec __free(seq_oss_synth) = NULL;

	rec = get_synthdev(dp, dev);
	if (rec)
		return get_synthinfo_nospec(dp, dev);
	return NULL;
}


/*
 * receive OSS 6 byte sysex packet:
 * the event is filled and prepared for sending immediately
 * (i.e. sysex messages are fragmented)
 */
int
snd_seq_oss_synth_sysex(struct seq_oss_devinfo *dp, int dev, unsigned char *buf, struct snd_seq_event *ev)
{
	unsigned char *p;
	int len = 6;

	p = memchr(buf, 0xff, 6);
	if (p)
		len = p - buf + 1;

	/* copy the data to event record and send it */
	if (snd_seq_oss_synth_addr(dp, dev, ev))
		return -EINVAL;
	ev->flags = SNDRV_SEQ_EVENT_LENGTH_VARIABLE;
	ev->data.ext.len = len;
	ev->data.ext.ptr = buf;
	return 0;
}

/*
 * fill the event source/destination addresses
 */
int
snd_seq_oss_synth_addr(struct seq_oss_devinfo *dp, int dev, struct snd_seq_event *ev)
{
	struct seq_oss_synthinfo *info = snd_seq_oss_synth_info(dp, dev);

	if (!info)
		return -EINVAL;
	snd_seq_oss_fill_addr(dp, ev, info->arg.addr.client,
			      info->arg.addr.port);
	return 0;
}


/*
 * OSS compatible ioctl
 */
int
snd_seq_oss_synth_ioctl(struct seq_oss_devinfo *dp, int dev, unsigned int cmd, unsigned long addr)
{
	struct seq_oss_synth *rec __free(seq_oss_synth) = NULL;
	struct seq_oss_synthinfo *info;

	info = get_synthinfo_nospec(dp, dev);
	if (!info || info->is_midi)
		return -ENXIO;
	rec = get_synthdev(dp, dev);
	if (!rec)
		return -ENXIO;
	if (rec->oper.ioctl == NULL)
		return -ENXIO;
	else
		return rec->oper.ioctl(&info->arg, cmd, addr);
}


/*
 * send OSS raw events - SEQ_PRIVATE and SEQ_VOLUME
 */
int
snd_seq_oss_synth_raw_event(struct seq_oss_devinfo *dp, int dev, unsigned char *data, struct snd_seq_event *ev)
{
	struct seq_oss_synthinfo *info;

	info = snd_seq_oss_synth_info(dp, dev);
	if (!info || info->is_midi)
		return -ENXIO;
	ev->type = SNDRV_SEQ_EVENT_OSS;
	memcpy(ev->data.raw8.d, data, 8);
	return snd_seq_oss_synth_addr(dp, dev, ev);
}


/*
 * create OSS compatible synth_info record
 */
int
snd_seq_oss_synth_make_info(struct seq_oss_devinfo *dp, int dev, struct synth_info *inf)
{
	struct seq_oss_synthinfo *info = get_synthinfo_nospec(dp, dev);

	if (!info)
		return -ENXIO;

	if (info->is_midi) {
		struct midi_info minf;
		if (snd_seq_oss_midi_make_info(dp, info->midi_mapped, &minf))
			return -ENXIO;
		inf->synth_type = SYNTH_TYPE_MIDI;
		inf->synth_subtype = 0;
		inf->nr_voices = 16;
		inf->device = dev;
		strscpy(inf->name, minf.name, sizeof(inf->name));
	} else {
		struct seq_oss_synth *rec __free(seq_oss_synth) =
			get_synthdev(dp, dev);

		if (!rec)
			return -ENXIO;
		inf->synth_type = rec->synth_type;
		inf->synth_subtype = rec->synth_subtype;
		inf->nr_voices = rec->nr_voices;
		inf->device = dev;
		strscpy(inf->name, rec->name, sizeof(inf->name));
	}
	return 0;
}


#ifdef CONFIG_SND_PROC_FS
/*
 * proc interface
 */
void
snd_seq_oss_synth_info_read(struct snd_info_buffer *buf)
{
	int i;

	snd_iprintf(buf, "\nNumber of synth devices: %d\n", max_synth_devs);
	for (i = 0; i < max_synth_devs; i++) {
		struct seq_oss_synth *rec __free(seq_oss_synth) = NULL;

		snd_iprintf(buf, "\nsynth %d: ", i);
		rec = get_sdev(i);
		if (rec == NULL) {
			snd_iprintf(buf, "*empty*\n");
			continue;
		}
		snd_iprintf(buf, "[%s]\n", rec->name);
		snd_iprintf(buf, "  type 0x%x : subtype 0x%x : voices %d\n",
			    rec->synth_type, rec->synth_subtype,
			    rec->nr_voices);
		snd_iprintf(buf, "  capabilities : ioctl %s / load_patch %s\n",
			    str_enabled_disabled((long)rec->oper.ioctl),
			    str_enabled_disabled((long)rec->oper.load_patch));
	}
}
#endif /* CONFIG_SND_PROC_FS */
