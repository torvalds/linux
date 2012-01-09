/*
 *  Digital Audio (PCM) abstract layer
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

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/info.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>, Abramo Bagnara <abramo@alsa-project.org>");
MODULE_DESCRIPTION("Midlevel PCM code for ALSA.");
MODULE_LICENSE("GPL");

static LIST_HEAD(snd_pcm_devices);
static LIST_HEAD(snd_pcm_notify_list);
static DEFINE_MUTEX(register_mutex);

static int snd_pcm_free(struct snd_pcm *pcm);
static int snd_pcm_dev_free(struct snd_device *device);
static int snd_pcm_dev_register(struct snd_device *device);
static int snd_pcm_dev_disconnect(struct snd_device *device);

static struct snd_pcm *snd_pcm_get(struct snd_card *card, int device)
{
	struct snd_pcm *pcm;

	list_for_each_entry(pcm, &snd_pcm_devices, list) {
		if (pcm->card == card && pcm->device == device)
			return pcm;
	}
	return NULL;
}

static int snd_pcm_next(struct snd_card *card, int device)
{
	struct snd_pcm *pcm;

	list_for_each_entry(pcm, &snd_pcm_devices, list) {
		if (pcm->card == card && pcm->device > device)
			return pcm->device;
		else if (pcm->card->number > card->number)
			return -1;
	}
	return -1;
}

static int snd_pcm_add(struct snd_pcm *newpcm)
{
	struct snd_pcm *pcm;

	list_for_each_entry(pcm, &snd_pcm_devices, list) {
		if (pcm->card == newpcm->card && pcm->device == newpcm->device)
			return -EBUSY;
		if (pcm->card->number > newpcm->card->number ||
				(pcm->card == newpcm->card &&
				pcm->device > newpcm->device)) {
			list_add(&newpcm->list, pcm->list.prev);
			return 0;
		}
	}
	list_add_tail(&newpcm->list, &snd_pcm_devices);
	return 0;
}

static int snd_pcm_control_ioctl(struct snd_card *card,
				 struct snd_ctl_file *control,
				 unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE:
		{
			int device;

			if (get_user(device, (int __user *)arg))
				return -EFAULT;
			mutex_lock(&register_mutex);
			device = snd_pcm_next(card, device);
			mutex_unlock(&register_mutex);
			if (put_user(device, (int __user *)arg))
				return -EFAULT;
			return 0;
		}
	case SNDRV_CTL_IOCTL_PCM_INFO:
		{
			struct snd_pcm_info __user *info;
			unsigned int device, subdevice;
			int stream;
			struct snd_pcm *pcm;
			struct snd_pcm_str *pstr;
			struct snd_pcm_substream *substream;
			int err;

			info = (struct snd_pcm_info __user *)arg;
			if (get_user(device, &info->device))
				return -EFAULT;
			if (get_user(stream, &info->stream))
				return -EFAULT;
			if (stream < 0 || stream > 1)
				return -EINVAL;
			if (get_user(subdevice, &info->subdevice))
				return -EFAULT;
			mutex_lock(&register_mutex);
			pcm = snd_pcm_get(card, device);
			if (pcm == NULL) {
				err = -ENXIO;
				goto _error;
			}
			pstr = &pcm->streams[stream];
			if (pstr->substream_count == 0) {
				err = -ENOENT;
				goto _error;
			}
			if (subdevice >= pstr->substream_count) {
				err = -ENXIO;
				goto _error;
			}
			for (substream = pstr->substream; substream;
			     substream = substream->next)
				if (substream->number == (int)subdevice)
					break;
			if (substream == NULL) {
				err = -ENXIO;
				goto _error;
			}
			err = snd_pcm_info_user(substream, info);
		_error:
			mutex_unlock(&register_mutex);
			return err;
		}
	case SNDRV_CTL_IOCTL_PCM_PREFER_SUBDEVICE:
		{
			int val;
			
			if (get_user(val, (int __user *)arg))
				return -EFAULT;
			control->prefer_pcm_subdevice = val;
			return 0;
		}
	}
	return -ENOIOCTLCMD;
}

#define FORMAT(v) [SNDRV_PCM_FORMAT_##v] = #v

static char *snd_pcm_format_names[] = {
	FORMAT(S8),
	FORMAT(U8),
	FORMAT(S16_LE),
	FORMAT(S16_BE),
	FORMAT(U16_LE),
	FORMAT(U16_BE),
	FORMAT(S24_LE),
	FORMAT(S24_BE),
	FORMAT(U24_LE),
	FORMAT(U24_BE),
	FORMAT(S32_LE),
	FORMAT(S32_BE),
	FORMAT(U32_LE),
	FORMAT(U32_BE),
	FORMAT(FLOAT_LE),
	FORMAT(FLOAT_BE),
	FORMAT(FLOAT64_LE),
	FORMAT(FLOAT64_BE),
	FORMAT(IEC958_SUBFRAME_LE),
	FORMAT(IEC958_SUBFRAME_BE),
	FORMAT(MU_LAW),
	FORMAT(A_LAW),
	FORMAT(IMA_ADPCM),
	FORMAT(MPEG),
	FORMAT(GSM),
	FORMAT(SPECIAL),
	FORMAT(S24_3LE),
	FORMAT(S24_3BE),
	FORMAT(U24_3LE),
	FORMAT(U24_3BE),
	FORMAT(S20_3LE),
	FORMAT(S20_3BE),
	FORMAT(U20_3LE),
	FORMAT(U20_3BE),
	FORMAT(S18_3LE),
	FORMAT(S18_3BE),
	FORMAT(U18_3LE),
	FORMAT(U18_3BE),
	FORMAT(G723_24),
	FORMAT(G723_24_1B),
	FORMAT(G723_40),
	FORMAT(G723_40_1B),
};

const char *snd_pcm_format_name(snd_pcm_format_t format)
{
	if ((__force unsigned int)format >= ARRAY_SIZE(snd_pcm_format_names))
		return "Unknown";
	return snd_pcm_format_names[(__force unsigned int)format];
}
EXPORT_SYMBOL_GPL(snd_pcm_format_name);

#ifdef CONFIG_SND_VERBOSE_PROCFS

#define STATE(v) [SNDRV_PCM_STATE_##v] = #v
#define STREAM(v) [SNDRV_PCM_STREAM_##v] = #v
#define READY(v) [SNDRV_PCM_READY_##v] = #v
#define XRUN(v) [SNDRV_PCM_XRUN_##v] = #v
#define SILENCE(v) [SNDRV_PCM_SILENCE_##v] = #v
#define TSTAMP(v) [SNDRV_PCM_TSTAMP_##v] = #v
#define ACCESS(v) [SNDRV_PCM_ACCESS_##v] = #v
#define START(v) [SNDRV_PCM_START_##v] = #v
#define SUBFORMAT(v) [SNDRV_PCM_SUBFORMAT_##v] = #v 

static char *snd_pcm_stream_names[] = {
	STREAM(PLAYBACK),
	STREAM(CAPTURE),
};

static char *snd_pcm_state_names[] = {
	STATE(OPEN),
	STATE(SETUP),
	STATE(PREPARED),
	STATE(RUNNING),
	STATE(XRUN),
	STATE(DRAINING),
	STATE(PAUSED),
	STATE(SUSPENDED),
};

static char *snd_pcm_access_names[] = {
	ACCESS(MMAP_INTERLEAVED), 
	ACCESS(MMAP_NONINTERLEAVED),
	ACCESS(MMAP_COMPLEX),
	ACCESS(RW_INTERLEAVED),
	ACCESS(RW_NONINTERLEAVED),
};

static char *snd_pcm_subformat_names[] = {
	SUBFORMAT(STD), 
};

static char *snd_pcm_tstamp_mode_names[] = {
	TSTAMP(NONE),
	TSTAMP(ENABLE),
};

static const char *snd_pcm_stream_name(int stream)
{
	return snd_pcm_stream_names[stream];
}

static const char *snd_pcm_access_name(snd_pcm_access_t access)
{
	return snd_pcm_access_names[(__force int)access];
}

static const char *snd_pcm_subformat_name(snd_pcm_subformat_t subformat)
{
	return snd_pcm_subformat_names[(__force int)subformat];
}

static const char *snd_pcm_tstamp_mode_name(int mode)
{
	return snd_pcm_tstamp_mode_names[mode];
}

static const char *snd_pcm_state_name(snd_pcm_state_t state)
{
	return snd_pcm_state_names[(__force int)state];
}

#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
#include <linux/soundcard.h>

static const char *snd_pcm_oss_format_name(int format)
{
	switch (format) {
	case AFMT_MU_LAW:
		return "MU_LAW";
	case AFMT_A_LAW:
		return "A_LAW";
	case AFMT_IMA_ADPCM:
		return "IMA_ADPCM";
	case AFMT_U8:
		return "U8";
	case AFMT_S16_LE:
		return "S16_LE";
	case AFMT_S16_BE:
		return "S16_BE";
	case AFMT_S8:
		return "S8";
	case AFMT_U16_LE:
		return "U16_LE";
	case AFMT_U16_BE:
		return "U16_BE";
	case AFMT_MPEG:
		return "MPEG";
	default:
		return "unknown";
	}
}
#endif

static void snd_pcm_proc_info_read(struct snd_pcm_substream *substream,
				   struct snd_info_buffer *buffer)
{
	struct snd_pcm_info *info;
	int err;

	if (! substream)
		return;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (! info) {
		printk(KERN_DEBUG "snd_pcm_proc_info_read: cannot malloc\n");
		return;
	}

	err = snd_pcm_info(substream, info);
	if (err < 0) {
		snd_iprintf(buffer, "error %d\n", err);
		kfree(info);
		return;
	}
	snd_iprintf(buffer, "card: %d\n", info->card);
	snd_iprintf(buffer, "device: %d\n", info->device);
	snd_iprintf(buffer, "subdevice: %d\n", info->subdevice);
	snd_iprintf(buffer, "stream: %s\n", snd_pcm_stream_name(info->stream));
	snd_iprintf(buffer, "id: %s\n", info->id);
	snd_iprintf(buffer, "name: %s\n", info->name);
	snd_iprintf(buffer, "subname: %s\n", info->subname);
	snd_iprintf(buffer, "class: %d\n", info->dev_class);
	snd_iprintf(buffer, "subclass: %d\n", info->dev_subclass);
	snd_iprintf(buffer, "subdevices_count: %d\n", info->subdevices_count);
	snd_iprintf(buffer, "subdevices_avail: %d\n", info->subdevices_avail);
	kfree(info);
}

static void snd_pcm_stream_proc_info_read(struct snd_info_entry *entry,
					  struct snd_info_buffer *buffer)
{
	snd_pcm_proc_info_read(((struct snd_pcm_str *)entry->private_data)->substream,
			       buffer);
}

static void snd_pcm_substream_proc_info_read(struct snd_info_entry *entry,
					     struct snd_info_buffer *buffer)
{
	snd_pcm_proc_info_read(entry->private_data, buffer);
}

static void snd_pcm_substream_proc_hw_params_read(struct snd_info_entry *entry,
						  struct snd_info_buffer *buffer)
{
	struct snd_pcm_substream *substream = entry->private_data;
	struct snd_pcm_runtime *runtime;

	mutex_lock(&substream->pcm->open_mutex);
	runtime = substream->runtime;
	if (!runtime) {
		snd_iprintf(buffer, "closed\n");
		goto unlock;
	}
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		snd_iprintf(buffer, "no setup\n");
		goto unlock;
	}
	snd_iprintf(buffer, "access: %s\n", snd_pcm_access_name(runtime->access));
	snd_iprintf(buffer, "format: %s\n", snd_pcm_format_name(runtime->format));
	snd_iprintf(buffer, "subformat: %s\n", snd_pcm_subformat_name(runtime->subformat));
	snd_iprintf(buffer, "channels: %u\n", runtime->channels);	
	snd_iprintf(buffer, "rate: %u (%u/%u)\n", runtime->rate, runtime->rate_num, runtime->rate_den);	
	snd_iprintf(buffer, "period_size: %lu\n", runtime->period_size);	
	snd_iprintf(buffer, "buffer_size: %lu\n", runtime->buffer_size);	
#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
	if (substream->oss.oss) {
		snd_iprintf(buffer, "OSS format: %s\n", snd_pcm_oss_format_name(runtime->oss.format));
		snd_iprintf(buffer, "OSS channels: %u\n", runtime->oss.channels);	
		snd_iprintf(buffer, "OSS rate: %u\n", runtime->oss.rate);
		snd_iprintf(buffer, "OSS period bytes: %lu\n", (unsigned long)runtime->oss.period_bytes);
		snd_iprintf(buffer, "OSS periods: %u\n", runtime->oss.periods);
		snd_iprintf(buffer, "OSS period frames: %lu\n", (unsigned long)runtime->oss.period_frames);
	}
#endif
 unlock:
	mutex_unlock(&substream->pcm->open_mutex);
}

static void snd_pcm_substream_proc_sw_params_read(struct snd_info_entry *entry,
						  struct snd_info_buffer *buffer)
{
	struct snd_pcm_substream *substream = entry->private_data;
	struct snd_pcm_runtime *runtime;

	mutex_lock(&substream->pcm->open_mutex);
	runtime = substream->runtime;
	if (!runtime) {
		snd_iprintf(buffer, "closed\n");
		goto unlock;
	}
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		snd_iprintf(buffer, "no setup\n");
		goto unlock;
	}
	snd_iprintf(buffer, "tstamp_mode: %s\n", snd_pcm_tstamp_mode_name(runtime->tstamp_mode));
	snd_iprintf(buffer, "period_step: %u\n", runtime->period_step);
	snd_iprintf(buffer, "avail_min: %lu\n", runtime->control->avail_min);
	snd_iprintf(buffer, "start_threshold: %lu\n", runtime->start_threshold);
	snd_iprintf(buffer, "stop_threshold: %lu\n", runtime->stop_threshold);
	snd_iprintf(buffer, "silence_threshold: %lu\n", runtime->silence_threshold);
	snd_iprintf(buffer, "silence_size: %lu\n", runtime->silence_size);
	snd_iprintf(buffer, "boundary: %lu\n", runtime->boundary);
 unlock:
	mutex_unlock(&substream->pcm->open_mutex);
}

static void snd_pcm_substream_proc_status_read(struct snd_info_entry *entry,
					       struct snd_info_buffer *buffer)
{
	struct snd_pcm_substream *substream = entry->private_data;
	struct snd_pcm_runtime *runtime;
	struct snd_pcm_status status;
	int err;

	mutex_lock(&substream->pcm->open_mutex);
	runtime = substream->runtime;
	if (!runtime) {
		snd_iprintf(buffer, "closed\n");
		goto unlock;
	}
	memset(&status, 0, sizeof(status));
	err = snd_pcm_status(substream, &status);
	if (err < 0) {
		snd_iprintf(buffer, "error %d\n", err);
		goto unlock;
	}
	snd_iprintf(buffer, "state: %s\n", snd_pcm_state_name(status.state));
	snd_iprintf(buffer, "owner_pid   : %d\n", pid_vnr(substream->pid));
	snd_iprintf(buffer, "trigger_time: %ld.%09ld\n",
		status.trigger_tstamp.tv_sec, status.trigger_tstamp.tv_nsec);
	snd_iprintf(buffer, "tstamp      : %ld.%09ld\n",
		status.tstamp.tv_sec, status.tstamp.tv_nsec);
	snd_iprintf(buffer, "delay       : %ld\n", status.delay);
	snd_iprintf(buffer, "avail       : %ld\n", status.avail);
	snd_iprintf(buffer, "avail_max   : %ld\n", status.avail_max);
	snd_iprintf(buffer, "-----\n");
	snd_iprintf(buffer, "hw_ptr      : %ld\n", runtime->status->hw_ptr);
	snd_iprintf(buffer, "appl_ptr    : %ld\n", runtime->control->appl_ptr);
 unlock:
	mutex_unlock(&substream->pcm->open_mutex);
}

#ifdef CONFIG_SND_PCM_XRUN_DEBUG
static void snd_pcm_xrun_debug_read(struct snd_info_entry *entry,
				    struct snd_info_buffer *buffer)
{
	struct snd_pcm_str *pstr = entry->private_data;
	snd_iprintf(buffer, "%d\n", pstr->xrun_debug);
}

static void snd_pcm_xrun_debug_write(struct snd_info_entry *entry,
				     struct snd_info_buffer *buffer)
{
	struct snd_pcm_str *pstr = entry->private_data;
	char line[64];
	if (!snd_info_get_line(buffer, line, sizeof(line)))
		pstr->xrun_debug = simple_strtoul(line, NULL, 10);
}
#endif

static int snd_pcm_stream_proc_init(struct snd_pcm_str *pstr)
{
	struct snd_pcm *pcm = pstr->pcm;
	struct snd_info_entry *entry;
	char name[16];

	sprintf(name, "pcm%i%c", pcm->device, 
		pstr->stream == SNDRV_PCM_STREAM_PLAYBACK ? 'p' : 'c');
	if ((entry = snd_info_create_card_entry(pcm->card, name, pcm->card->proc_root)) == NULL)
		return -ENOMEM;
	entry->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return -ENOMEM;
	}
	pstr->proc_root = entry;

	if ((entry = snd_info_create_card_entry(pcm->card, "info", pstr->proc_root)) != NULL) {
		snd_info_set_text_ops(entry, pstr, snd_pcm_stream_proc_info_read);
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	pstr->proc_info_entry = entry;

#ifdef CONFIG_SND_PCM_XRUN_DEBUG
	if ((entry = snd_info_create_card_entry(pcm->card, "xrun_debug",
						pstr->proc_root)) != NULL) {
		entry->c.text.read = snd_pcm_xrun_debug_read;
		entry->c.text.write = snd_pcm_xrun_debug_write;
		entry->mode |= S_IWUSR;
		entry->private_data = pstr;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	pstr->proc_xrun_debug_entry = entry;
#endif
	return 0;
}

static int snd_pcm_stream_proc_done(struct snd_pcm_str *pstr)
{
#ifdef CONFIG_SND_PCM_XRUN_DEBUG
	snd_info_free_entry(pstr->proc_xrun_debug_entry);
	pstr->proc_xrun_debug_entry = NULL;
#endif
	snd_info_free_entry(pstr->proc_info_entry);
	pstr->proc_info_entry = NULL;
	snd_info_free_entry(pstr->proc_root);
	pstr->proc_root = NULL;
	return 0;
}

static int snd_pcm_substream_proc_init(struct snd_pcm_substream *substream)
{
	struct snd_info_entry *entry;
	struct snd_card *card;
	char name[16];

	card = substream->pcm->card;

	sprintf(name, "sub%i", substream->number);
	if ((entry = snd_info_create_card_entry(card, name, substream->pstr->proc_root)) == NULL)
		return -ENOMEM;
	entry->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return -ENOMEM;
	}
	substream->proc_root = entry;

	if ((entry = snd_info_create_card_entry(card, "info", substream->proc_root)) != NULL) {
		snd_info_set_text_ops(entry, substream,
				      snd_pcm_substream_proc_info_read);
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	substream->proc_info_entry = entry;

	if ((entry = snd_info_create_card_entry(card, "hw_params", substream->proc_root)) != NULL) {
		snd_info_set_text_ops(entry, substream,
				      snd_pcm_substream_proc_hw_params_read);
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	substream->proc_hw_params_entry = entry;

	if ((entry = snd_info_create_card_entry(card, "sw_params", substream->proc_root)) != NULL) {
		snd_info_set_text_ops(entry, substream,
				      snd_pcm_substream_proc_sw_params_read);
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	substream->proc_sw_params_entry = entry;

	if ((entry = snd_info_create_card_entry(card, "status", substream->proc_root)) != NULL) {
		snd_info_set_text_ops(entry, substream,
				      snd_pcm_substream_proc_status_read);
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	substream->proc_status_entry = entry;

	return 0;
}

static int snd_pcm_substream_proc_done(struct snd_pcm_substream *substream)
{
	snd_info_free_entry(substream->proc_info_entry);
	substream->proc_info_entry = NULL;
	snd_info_free_entry(substream->proc_hw_params_entry);
	substream->proc_hw_params_entry = NULL;
	snd_info_free_entry(substream->proc_sw_params_entry);
	substream->proc_sw_params_entry = NULL;
	snd_info_free_entry(substream->proc_status_entry);
	substream->proc_status_entry = NULL;
	snd_info_free_entry(substream->proc_root);
	substream->proc_root = NULL;
	return 0;
}
#else /* !CONFIG_SND_VERBOSE_PROCFS */
static inline int snd_pcm_stream_proc_init(struct snd_pcm_str *pstr) { return 0; }
static inline int snd_pcm_stream_proc_done(struct snd_pcm_str *pstr) { return 0; }
static inline int snd_pcm_substream_proc_init(struct snd_pcm_substream *substream) { return 0; }
static inline int snd_pcm_substream_proc_done(struct snd_pcm_substream *substream) { return 0; }
#endif /* CONFIG_SND_VERBOSE_PROCFS */

/**
 * snd_pcm_new_stream - create a new PCM stream
 * @pcm: the pcm instance
 * @stream: the stream direction, SNDRV_PCM_STREAM_XXX
 * @substream_count: the number of substreams
 *
 * Creates a new stream for the pcm.
 * The corresponding stream on the pcm must have been empty before
 * calling this, i.e. zero must be given to the argument of
 * snd_pcm_new().
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_pcm_new_stream(struct snd_pcm *pcm, int stream, int substream_count)
{
	int idx, err;
	struct snd_pcm_str *pstr = &pcm->streams[stream];
	struct snd_pcm_substream *substream, *prev;

#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
	mutex_init(&pstr->oss.setup_mutex);
#endif
	pstr->stream = stream;
	pstr->pcm = pcm;
	pstr->substream_count = substream_count;
	if (substream_count > 0) {
		err = snd_pcm_stream_proc_init(pstr);
		if (err < 0) {
			snd_printk(KERN_ERR "Error in snd_pcm_stream_proc_init\n");
			return err;
		}
	}
	prev = NULL;
	for (idx = 0, prev = NULL; idx < substream_count; idx++) {
		substream = kzalloc(sizeof(*substream), GFP_KERNEL);
		if (substream == NULL) {
			snd_printk(KERN_ERR "Cannot allocate PCM substream\n");
			return -ENOMEM;
		}
		substream->pcm = pcm;
		substream->pstr = pstr;
		substream->number = idx;
		substream->stream = stream;
		sprintf(substream->name, "subdevice #%i", idx);
		substream->buffer_bytes_max = UINT_MAX;
		if (prev == NULL)
			pstr->substream = substream;
		else
			prev->next = substream;
		err = snd_pcm_substream_proc_init(substream);
		if (err < 0) {
			snd_printk(KERN_ERR "Error in snd_pcm_stream_proc_init\n");
			if (prev == NULL)
				pstr->substream = NULL;
			else
				prev->next = NULL;
			kfree(substream);
			return err;
		}
		substream->group = &substream->self_group;
		spin_lock_init(&substream->self_group.lock);
		INIT_LIST_HEAD(&substream->self_group.substreams);
		list_add_tail(&substream->link_list, &substream->self_group.substreams);
		atomic_set(&substream->mmap_count, 0);
		prev = substream;
	}
	return 0;
}				

EXPORT_SYMBOL(snd_pcm_new_stream);

/**
 * snd_pcm_new - create a new PCM instance
 * @card: the card instance
 * @id: the id string
 * @device: the device index (zero based)
 * @playback_count: the number of substreams for playback
 * @capture_count: the number of substreams for capture
 * @rpcm: the pointer to store the new pcm instance
 *
 * Creates a new PCM instance.
 *
 * The pcm operators have to be set afterwards to the new instance
 * via snd_pcm_set_ops().
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_pcm_new(struct snd_card *card, const char *id, int device,
		int playback_count, int capture_count,
	        struct snd_pcm ** rpcm)
{
	struct snd_pcm *pcm;
	int err;
	static struct snd_device_ops ops = {
		.dev_free = snd_pcm_dev_free,
		.dev_register =	snd_pcm_dev_register,
		.dev_disconnect = snd_pcm_dev_disconnect,
	};

	if (snd_BUG_ON(!card))
		return -ENXIO;
	if (rpcm)
		*rpcm = NULL;
	pcm = kzalloc(sizeof(*pcm), GFP_KERNEL);
	if (pcm == NULL) {
		snd_printk(KERN_ERR "Cannot allocate PCM\n");
		return -ENOMEM;
	}
	pcm->card = card;
	pcm->device = device;
	if (id)
		strlcpy(pcm->id, id, sizeof(pcm->id));
	if ((err = snd_pcm_new_stream(pcm, SNDRV_PCM_STREAM_PLAYBACK, playback_count)) < 0) {
		snd_pcm_free(pcm);
		return err;
	}
	if ((err = snd_pcm_new_stream(pcm, SNDRV_PCM_STREAM_CAPTURE, capture_count)) < 0) {
		snd_pcm_free(pcm);
		return err;
	}
	mutex_init(&pcm->open_mutex);
	init_waitqueue_head(&pcm->open_wait);
	if ((err = snd_device_new(card, SNDRV_DEV_PCM, pcm, &ops)) < 0) {
		snd_pcm_free(pcm);
		return err;
	}
	if (rpcm)
		*rpcm = pcm;
	return 0;
}

EXPORT_SYMBOL(snd_pcm_new);

static void snd_pcm_free_stream(struct snd_pcm_str * pstr)
{
	struct snd_pcm_substream *substream, *substream_next;
#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
	struct snd_pcm_oss_setup *setup, *setupn;
#endif
	substream = pstr->substream;
	while (substream) {
		substream_next = substream->next;
		snd_pcm_timer_done(substream);
		snd_pcm_substream_proc_done(substream);
		kfree(substream);
		substream = substream_next;
	}
	snd_pcm_stream_proc_done(pstr);
#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
	for (setup = pstr->oss.setup_list; setup; setup = setupn) {
		setupn = setup->next;
		kfree(setup->task_name);
		kfree(setup);
	}
#endif
}

static int snd_pcm_free(struct snd_pcm *pcm)
{
	struct snd_pcm_notify *notify;

	if (!pcm)
		return 0;
	list_for_each_entry(notify, &snd_pcm_notify_list, list) {
		notify->n_unregister(pcm);
	}
	if (pcm->private_free)
		pcm->private_free(pcm);
	snd_pcm_lib_preallocate_free_for_all(pcm);
	snd_pcm_free_stream(&pcm->streams[SNDRV_PCM_STREAM_PLAYBACK]);
	snd_pcm_free_stream(&pcm->streams[SNDRV_PCM_STREAM_CAPTURE]);
	kfree(pcm);
	return 0;
}

static int snd_pcm_dev_free(struct snd_device *device)
{
	struct snd_pcm *pcm = device->device_data;
	return snd_pcm_free(pcm);
}

int snd_pcm_attach_substream(struct snd_pcm *pcm, int stream,
			     struct file *file,
			     struct snd_pcm_substream **rsubstream)
{
	struct snd_pcm_str * pstr;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	struct snd_ctl_file *kctl;
	struct snd_card *card;
	int prefer_subdevice = -1;
	size_t size;

	if (snd_BUG_ON(!pcm || !rsubstream))
		return -ENXIO;
	*rsubstream = NULL;
	pstr = &pcm->streams[stream];
	if (pstr->substream == NULL || pstr->substream_count == 0)
		return -ENODEV;

	card = pcm->card;
	read_lock(&card->ctl_files_rwlock);
	list_for_each_entry(kctl, &card->ctl_files, list) {
		if (kctl->pid == task_pid(current)) {
			prefer_subdevice = kctl->prefer_pcm_subdevice;
			if (prefer_subdevice != -1)
				break;
		}
	}
	read_unlock(&card->ctl_files_rwlock);

	switch (stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		if (pcm->info_flags & SNDRV_PCM_INFO_HALF_DUPLEX) {
			for (substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream; substream; substream = substream->next) {
				if (SUBSTREAM_BUSY(substream))
					return -EAGAIN;
			}
		}
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		if (pcm->info_flags & SNDRV_PCM_INFO_HALF_DUPLEX) {
			for (substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; substream; substream = substream->next) {
				if (SUBSTREAM_BUSY(substream))
					return -EAGAIN;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	if (file->f_flags & O_APPEND) {
		if (prefer_subdevice < 0) {
			if (pstr->substream_count > 1)
				return -EINVAL; /* must be unique */
			substream = pstr->substream;
		} else {
			for (substream = pstr->substream; substream;
			     substream = substream->next)
				if (substream->number == prefer_subdevice)
					break;
		}
		if (! substream)
			return -ENODEV;
		if (! SUBSTREAM_BUSY(substream))
			return -EBADFD;
		substream->ref_count++;
		*rsubstream = substream;
		return 0;
	}

	if (prefer_subdevice >= 0) {
		for (substream = pstr->substream; substream; substream = substream->next)
			if (!SUBSTREAM_BUSY(substream) && substream->number == prefer_subdevice)
				goto __ok;
	}
	for (substream = pstr->substream; substream; substream = substream->next)
		if (!SUBSTREAM_BUSY(substream))
			break;
      __ok:
	if (substream == NULL)
		return -EAGAIN;

	runtime = kzalloc(sizeof(*runtime), GFP_KERNEL);
	if (runtime == NULL)
		return -ENOMEM;

	size = PAGE_ALIGN(sizeof(struct snd_pcm_mmap_status));
	runtime->status = snd_malloc_pages(size, GFP_KERNEL);
	if (runtime->status == NULL) {
		kfree(runtime);
		return -ENOMEM;
	}
	memset((void*)runtime->status, 0, size);

	size = PAGE_ALIGN(sizeof(struct snd_pcm_mmap_control));
	runtime->control = snd_malloc_pages(size, GFP_KERNEL);
	if (runtime->control == NULL) {
		snd_free_pages((void*)runtime->status,
			       PAGE_ALIGN(sizeof(struct snd_pcm_mmap_status)));
		kfree(runtime);
		return -ENOMEM;
	}
	memset((void*)runtime->control, 0, size);

	init_waitqueue_head(&runtime->sleep);
	init_waitqueue_head(&runtime->tsleep);

	runtime->status->state = SNDRV_PCM_STATE_OPEN;

	substream->runtime = runtime;
	substream->private_data = pcm->private_data;
	substream->ref_count = 1;
	substream->f_flags = file->f_flags;
	substream->pid = get_pid(task_pid(current));
	pstr->substream_opened++;
	*rsubstream = substream;
	return 0;
}

void snd_pcm_detach_substream(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;

	if (PCM_RUNTIME_CHECK(substream))
		return;
	runtime = substream->runtime;
	if (runtime->private_free != NULL)
		runtime->private_free(runtime);
	snd_free_pages((void*)runtime->status,
		       PAGE_ALIGN(sizeof(struct snd_pcm_mmap_status)));
	snd_free_pages((void*)runtime->control,
		       PAGE_ALIGN(sizeof(struct snd_pcm_mmap_control)));
	kfree(runtime->hw_constraints.rules);
#ifdef CONFIG_SND_PCM_XRUN_DEBUG
	if (runtime->hwptr_log)
		kfree(runtime->hwptr_log);
#endif
	kfree(runtime);
	substream->runtime = NULL;
	put_pid(substream->pid);
	substream->pid = NULL;
	substream->pstr->substream_opened--;
}

static ssize_t show_pcm_class(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct snd_pcm *pcm;
	const char *str;
	static const char *strs[SNDRV_PCM_CLASS_LAST + 1] = {
		[SNDRV_PCM_CLASS_GENERIC] = "generic",
		[SNDRV_PCM_CLASS_MULTI] = "multi",
		[SNDRV_PCM_CLASS_MODEM] = "modem",
		[SNDRV_PCM_CLASS_DIGITIZER] = "digitizer",
	};

	if (! (pcm = dev_get_drvdata(dev)) ||
	    pcm->dev_class > SNDRV_PCM_CLASS_LAST)
		str = "none";
	else
		str = strs[pcm->dev_class];
        return snprintf(buf, PAGE_SIZE, "%s\n", str);
}

static struct device_attribute pcm_attrs =
	__ATTR(pcm_class, S_IRUGO, show_pcm_class, NULL);

static int snd_pcm_dev_register(struct snd_device *device)
{
	int cidx, err;
	struct snd_pcm_substream *substream;
	struct snd_pcm_notify *notify;
	char str[16];
	struct snd_pcm *pcm;
	struct device *dev;

	if (snd_BUG_ON(!device || !device->device_data))
		return -ENXIO;
	pcm = device->device_data;
	mutex_lock(&register_mutex);
	err = snd_pcm_add(pcm);
	if (err) {
		mutex_unlock(&register_mutex);
		return err;
	}
	for (cidx = 0; cidx < 2; cidx++) {
		int devtype = -1;
		if (pcm->streams[cidx].substream == NULL)
			continue;
		switch (cidx) {
		case SNDRV_PCM_STREAM_PLAYBACK:
			sprintf(str, "pcmC%iD%ip", pcm->card->number, pcm->device);
			devtype = SNDRV_DEVICE_TYPE_PCM_PLAYBACK;
			break;
		case SNDRV_PCM_STREAM_CAPTURE:
			sprintf(str, "pcmC%iD%ic", pcm->card->number, pcm->device);
			devtype = SNDRV_DEVICE_TYPE_PCM_CAPTURE;
			break;
		}
		/* device pointer to use, pcm->dev takes precedence if
		 * it is assigned, otherwise fall back to card's device
		 * if possible */
		dev = pcm->dev;
		if (!dev)
			dev = snd_card_get_device_link(pcm->card);
		/* register pcm */
		err = snd_register_device_for_dev(devtype, pcm->card,
						  pcm->device,
						  &snd_pcm_f_ops[cidx],
						  pcm, str, dev);
		if (err < 0) {
			list_del(&pcm->list);
			mutex_unlock(&register_mutex);
			return err;
		}
		snd_add_device_sysfs_file(devtype, pcm->card, pcm->device,
					  &pcm_attrs);
		for (substream = pcm->streams[cidx].substream; substream; substream = substream->next)
			snd_pcm_timer_init(substream);
	}

	list_for_each_entry(notify, &snd_pcm_notify_list, list)
		notify->n_register(pcm);

	mutex_unlock(&register_mutex);
	return 0;
}

static int snd_pcm_dev_disconnect(struct snd_device *device)
{
	struct snd_pcm *pcm = device->device_data;
	struct snd_pcm_notify *notify;
	struct snd_pcm_substream *substream;
	int cidx, devtype;

	mutex_lock(&register_mutex);
	if (list_empty(&pcm->list))
		goto unlock;

	list_del_init(&pcm->list);
	for (cidx = 0; cidx < 2; cidx++)
		for (substream = pcm->streams[cidx].substream; substream; substream = substream->next)
			if (substream->runtime)
				substream->runtime->status->state = SNDRV_PCM_STATE_DISCONNECTED;
	list_for_each_entry(notify, &snd_pcm_notify_list, list) {
		notify->n_disconnect(pcm);
	}
	for (cidx = 0; cidx < 2; cidx++) {
		devtype = -1;
		switch (cidx) {
		case SNDRV_PCM_STREAM_PLAYBACK:
			devtype = SNDRV_DEVICE_TYPE_PCM_PLAYBACK;
			break;
		case SNDRV_PCM_STREAM_CAPTURE:
			devtype = SNDRV_DEVICE_TYPE_PCM_CAPTURE;
			break;
		}
		snd_unregister_device(devtype, pcm->card, pcm->device);
	}
 unlock:
	mutex_unlock(&register_mutex);
	return 0;
}

int snd_pcm_notify(struct snd_pcm_notify *notify, int nfree)
{
	struct snd_pcm *pcm;

	if (snd_BUG_ON(!notify ||
		       !notify->n_register ||
		       !notify->n_unregister ||
		       !notify->n_disconnect))
		return -EINVAL;
	mutex_lock(&register_mutex);
	if (nfree) {
		list_del(&notify->list);
		list_for_each_entry(pcm, &snd_pcm_devices, list)
			notify->n_unregister(pcm);
	} else {
		list_add_tail(&notify->list, &snd_pcm_notify_list);
		list_for_each_entry(pcm, &snd_pcm_devices, list)
			notify->n_register(pcm);
	}
	mutex_unlock(&register_mutex);
	return 0;
}

EXPORT_SYMBOL(snd_pcm_notify);

#ifdef CONFIG_PROC_FS
/*
 *  Info interface
 */

static void snd_pcm_proc_read(struct snd_info_entry *entry,
			      struct snd_info_buffer *buffer)
{
	struct snd_pcm *pcm;

	mutex_lock(&register_mutex);
	list_for_each_entry(pcm, &snd_pcm_devices, list) {
		snd_iprintf(buffer, "%02i-%02i: %s : %s",
			    pcm->card->number, pcm->device, pcm->id, pcm->name);
		if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream)
			snd_iprintf(buffer, " : playback %i",
				    pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream_count);
		if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream)
			snd_iprintf(buffer, " : capture %i",
				    pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream_count);
		snd_iprintf(buffer, "\n");
	}
	mutex_unlock(&register_mutex);
}

static struct snd_info_entry *snd_pcm_proc_entry;

static void snd_pcm_proc_init(void)
{
	struct snd_info_entry *entry;

	if ((entry = snd_info_create_module_entry(THIS_MODULE, "pcm", NULL)) != NULL) {
		snd_info_set_text_ops(entry, NULL, snd_pcm_proc_read);
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	snd_pcm_proc_entry = entry;
}

static void snd_pcm_proc_done(void)
{
	snd_info_free_entry(snd_pcm_proc_entry);
}

#else /* !CONFIG_PROC_FS */
#define snd_pcm_proc_init()
#define snd_pcm_proc_done()
#endif /* CONFIG_PROC_FS */


/*
 *  ENTRY functions
 */

static int __init alsa_pcm_init(void)
{
	snd_ctl_register_ioctl(snd_pcm_control_ioctl);
	snd_ctl_register_ioctl_compat(snd_pcm_control_ioctl);
	snd_pcm_proc_init();
	return 0;
}

static void __exit alsa_pcm_exit(void)
{
	snd_ctl_unregister_ioctl(snd_pcm_control_ioctl);
	snd_ctl_unregister_ioctl_compat(snd_pcm_control_ioctl);
	snd_pcm_proc_done();
}

module_init(alsa_pcm_init)
module_exit(alsa_pcm_exit)
