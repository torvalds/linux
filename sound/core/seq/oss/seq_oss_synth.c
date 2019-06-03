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

/* sysex buffer */
struct seq_oss_synth_sysex {
	int len;
	int skip;
	unsigned char buf[MAX_SYSEX_BUFLEN];
};

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
	unsigned long flags;

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
	strlcpy(rec->name, dev->name, sizeof(rec->name));

	/* registration */
	spin_lock_irqsave(&register_lock, flags);
	for (i = 0; i < max_synth_devs; i++) {
		if (synth_devs[i] == NULL)
			break;
	}
	if (i >= max_synth_devs) {
		if (max_synth_devs >= SNDRV_SEQ_OSS_MAX_SYNTH_DEVS) {
			spin_unlock_irqrestore(&register_lock, flags);
			pr_err("ALSA: seq_oss: no more synth slot\n");
			kfree(rec);
			return -ENOMEM;
		}
		max_synth_devs++;
	}
	rec->seq_device = i;
	synth_devs[i] = rec;
	spin_unlock_irqrestore(&register_lock, flags);
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
	unsigned long flags;

	spin_lock_irqsave(&register_lock, flags);
	for (index = 0; index < max_synth_devs; index++) {
		if (synth_devs[index] == rec)
			break;
	}
	if (index >= max_synth_devs) {
		spin_unlock_irqrestore(&register_lock, flags);
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
	spin_unlock_irqrestore(&register_lock, flags);
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
	unsigned long flags;

	spin_lock_irqsave(&register_lock, flags);
	rec = synth_devs[dev];
	if (rec)
		snd_use_lock_use(&rec->use_lock);
	spin_unlock_irqrestore(&register_lock, flags);
	return rec;
}


/*
 * set up synth tables
 */

void
snd_seq_oss_synth_setup(struct seq_oss_devinfo *dp)
{
	int i;
	struct seq_oss_synth *rec;
	struct seq_oss_synthinfo *info;

	dp->max_synthdev = max_synth_devs;
	dp->synth_opened = 0;
	memset(dp->synths, 0, sizeof(dp->synths));
	for (i = 0; i < dp->max_synthdev; i++) {
		rec = get_sdev(i);
		if (rec == NULL)
			continue;
		if (rec->oper.open == NULL || rec->oper.close == NULL) {
			snd_use_lock_free(&rec->use_lock);
			continue;
		}
		info = &dp->synths[i];
		info->arg.app_index = dp->port;
		info->arg.file_mode = dp->file_mode;
		info->arg.seq_mode = dp->seq_mode;
		if (dp->seq_mode == SNDRV_SEQ_OSS_MODE_SYNTH)
			info->arg.event_passing = SNDRV_SEQ_OSS_PROCESS_EVENTS;
		else
			info->arg.event_passing = SNDRV_SEQ_OSS_PASS_EVENTS;
		info->opened = 0;
		if (!try_module_get(rec->oper.owner)) {
			snd_use_lock_free(&rec->use_lock);
			continue;
		}
		if (rec->oper.open(&info->arg, rec->private_data) < 0) {
			module_put(rec->oper.owner);
			snd_use_lock_free(&rec->use_lock);
			continue;
		}
		info->nr_voices = rec->nr_voices;
		if (info->nr_voices > 0) {
			info->ch = kcalloc(info->nr_voices, sizeof(struct seq_oss_chinfo), GFP_KERNEL);
			if (!info->ch) {
				rec->oper.close(&info->arg);
				module_put(rec->oper.owner);
				snd_use_lock_free(&rec->use_lock);
				continue;
			}
			reset_channels(info);
		}
		info->opened++;
		rec->opened++;
		dp->synth_opened++;
		snd_use_lock_free(&rec->use_lock);
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
	struct seq_oss_synth *rec;
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
			rec = get_sdev(i);
			if (rec == NULL)
				continue;
			if (rec->opened > 0) {
				rec->oper.close(&info->arg);
				module_put(rec->oper.owner);
				rec->opened = 0;
			}
			snd_use_lock_free(&rec->use_lock);
		}
		kfree(info->sysex);
		info->sysex = NULL;
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
	struct seq_oss_synth *rec;
	struct seq_oss_synthinfo *info;

	info = get_synthinfo_nospec(dp, dev);
	if (!info || !info->opened)
		return;
	if (info->sysex)
		info->sysex->len = 0; /* reset sysex */
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
			kfree(info->sysex);
			info->sysex = NULL;
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
	snd_use_lock_free(&rec->use_lock);
}


/*
 * load a patch record:
 * call load_patch callback function
 */
int
snd_seq_oss_synth_load_patch(struct seq_oss_devinfo *dp, int dev, int fmt,
			    const char __user *buf, int p, int c)
{
	struct seq_oss_synth *rec;
	struct seq_oss_synthinfo *info;
	int rc;

	info = get_synthinfo_nospec(dp, dev);
	if (!info)
		return -ENXIO;

	if (info->is_midi)
		return 0;
	if ((rec = get_synthdev(dp, dev)) == NULL)
		return -ENXIO;

	if (rec->oper.load_patch == NULL)
		rc = -ENXIO;
	else
		rc = rec->oper.load_patch(&info->arg, fmt, buf, p, c);
	snd_use_lock_free(&rec->use_lock);
	return rc;
}

/*
 * check if the device is valid synth device and return the synth info
 */
struct seq_oss_synthinfo *
snd_seq_oss_synth_info(struct seq_oss_devinfo *dp, int dev)
{
	struct seq_oss_synth *rec;

	rec = get_synthdev(dp, dev);
	if (rec) {
		snd_use_lock_free(&rec->use_lock);
		return get_synthinfo_nospec(dp, dev);
	}
	return NULL;
}


/*
 * receive OSS 6 byte sysex packet:
 * the full sysex message will be sent if it reaches to the end of data
 * (0xff).
 */
int
snd_seq_oss_synth_sysex(struct seq_oss_devinfo *dp, int dev, unsigned char *buf, struct snd_seq_event *ev)
{
	int i, send;
	unsigned char *dest;
	struct seq_oss_synth_sysex *sysex;
	struct seq_oss_synthinfo *info;

	info = snd_seq_oss_synth_info(dp, dev);
	if (!info)
		return -ENXIO;

	sysex = info->sysex;
	if (sysex == NULL) {
		sysex = kzalloc(sizeof(*sysex), GFP_KERNEL);
		if (sysex == NULL)
			return -ENOMEM;
		info->sysex = sysex;
	}

	send = 0;
	dest = sysex->buf + sysex->len;
	/* copy 6 byte packet to the buffer */
	for (i = 0; i < 6; i++) {
		if (buf[i] == 0xff) {
			send = 1;
			break;
		}
		dest[i] = buf[i];
		sysex->len++;
		if (sysex->len >= MAX_SYSEX_BUFLEN) {
			sysex->len = 0;
			sysex->skip = 1;
			break;
		}
	}

	if (sysex->len && send) {
		if (sysex->skip) {
			sysex->skip = 0;
			sysex->len = 0;
			return -EINVAL; /* skip */
		}
		/* copy the data to event record and send it */
		ev->flags = SNDRV_SEQ_EVENT_LENGTH_VARIABLE;
		if (snd_seq_oss_synth_addr(dp, dev, ev))
			return -EINVAL;
		ev->data.ext.len = sysex->len;
		ev->data.ext.ptr = sysex->buf;
		sysex->len = 0;
		return 0;
	}

	return -EINVAL; /* skip */
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
	struct seq_oss_synth *rec;
	struct seq_oss_synthinfo *info;
	int rc;

	info = get_synthinfo_nospec(dp, dev);
	if (!info || info->is_midi)
		return -ENXIO;
	if ((rec = get_synthdev(dp, dev)) == NULL)
		return -ENXIO;
	if (rec->oper.ioctl == NULL)
		rc = -ENXIO;
	else
		rc = rec->oper.ioctl(&info->arg, cmd, addr);
	snd_use_lock_free(&rec->use_lock);
	return rc;
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
	struct seq_oss_synth *rec;
	struct seq_oss_synthinfo *info = get_synthinfo_nospec(dp, dev);

	if (!info)
		return -ENXIO;

	if (info->is_midi) {
		struct midi_info minf;
		snd_seq_oss_midi_make_info(dp, info->midi_mapped, &minf);
		inf->synth_type = SYNTH_TYPE_MIDI;
		inf->synth_subtype = 0;
		inf->nr_voices = 16;
		inf->device = dev;
		strlcpy(inf->name, minf.name, sizeof(inf->name));
	} else {
		if ((rec = get_synthdev(dp, dev)) == NULL)
			return -ENXIO;
		inf->synth_type = rec->synth_type;
		inf->synth_subtype = rec->synth_subtype;
		inf->nr_voices = rec->nr_voices;
		inf->device = dev;
		strlcpy(inf->name, rec->name, sizeof(inf->name));
		snd_use_lock_free(&rec->use_lock);
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
	struct seq_oss_synth *rec;

	snd_iprintf(buf, "\nNumber of synth devices: %d\n", max_synth_devs);
	for (i = 0; i < max_synth_devs; i++) {
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
			    enabled_str((long)rec->oper.ioctl),
			    enabled_str((long)rec->oper.load_patch));
		snd_use_lock_free(&rec->use_lock);
	}
}
#endif /* CONFIG_SND_PROC_FS */
