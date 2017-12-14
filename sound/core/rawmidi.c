/*
 *  Abstract layer for MIDI v1.0 stream
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

#include <sound/core.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <sound/rawmidi.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/minors.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Midlevel RawMidi code for ALSA.");
MODULE_LICENSE("GPL");

#ifdef CONFIG_SND_OSSEMUL
static int midi_map[SNDRV_CARDS];
static int amidi_map[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = 1};
module_param_array(midi_map, int, NULL, 0444);
MODULE_PARM_DESC(midi_map, "Raw MIDI device number assigned to 1st OSS device.");
module_param_array(amidi_map, int, NULL, 0444);
MODULE_PARM_DESC(amidi_map, "Raw MIDI device number assigned to 2nd OSS device.");
#endif /* CONFIG_SND_OSSEMUL */

static int snd_rawmidi_free(struct snd_rawmidi *rawmidi);
static int snd_rawmidi_dev_free(struct snd_device *device);
static int snd_rawmidi_dev_register(struct snd_device *device);
static int snd_rawmidi_dev_disconnect(struct snd_device *device);

static LIST_HEAD(snd_rawmidi_devices);
static DEFINE_MUTEX(register_mutex);

#define rmidi_err(rmidi, fmt, args...) \
	dev_err(&(rmidi)->dev, fmt, ##args)
#define rmidi_warn(rmidi, fmt, args...) \
	dev_warn(&(rmidi)->dev, fmt, ##args)
#define rmidi_dbg(rmidi, fmt, args...) \
	dev_dbg(&(rmidi)->dev, fmt, ##args)

static struct snd_rawmidi *snd_rawmidi_search(struct snd_card *card, int device)
{
	struct snd_rawmidi *rawmidi;

	list_for_each_entry(rawmidi, &snd_rawmidi_devices, list)
		if (rawmidi->card == card && rawmidi->device == device)
			return rawmidi;
	return NULL;
}

static inline unsigned short snd_rawmidi_file_flags(struct file *file)
{
	switch (file->f_mode & (FMODE_READ | FMODE_WRITE)) {
	case FMODE_WRITE:
		return SNDRV_RAWMIDI_LFLG_OUTPUT;
	case FMODE_READ:
		return SNDRV_RAWMIDI_LFLG_INPUT;
	default:
		return SNDRV_RAWMIDI_LFLG_OPEN;
	}
}

static inline int snd_rawmidi_ready(struct snd_rawmidi_substream *substream)
{
	struct snd_rawmidi_runtime *runtime = substream->runtime;
	return runtime->avail >= runtime->avail_min;
}

static inline int snd_rawmidi_ready_append(struct snd_rawmidi_substream *substream,
					   size_t count)
{
	struct snd_rawmidi_runtime *runtime = substream->runtime;
	return runtime->avail >= runtime->avail_min &&
	       (!substream->append || runtime->avail >= count);
}

static void snd_rawmidi_input_event_work(struct work_struct *work)
{
	struct snd_rawmidi_runtime *runtime =
		container_of(work, struct snd_rawmidi_runtime, event_work);
	if (runtime->event)
		runtime->event(runtime->substream);
}

static int snd_rawmidi_runtime_create(struct snd_rawmidi_substream *substream)
{
	struct snd_rawmidi_runtime *runtime;

	if ((runtime = kzalloc(sizeof(*runtime), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	runtime->substream = substream;
	spin_lock_init(&runtime->lock);
	init_waitqueue_head(&runtime->sleep);
	INIT_WORK(&runtime->event_work, snd_rawmidi_input_event_work);
	runtime->event = NULL;
	runtime->buffer_size = PAGE_SIZE;
	runtime->avail_min = 1;
	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT)
		runtime->avail = 0;
	else
		runtime->avail = runtime->buffer_size;
	if ((runtime->buffer = kmalloc(runtime->buffer_size, GFP_KERNEL)) == NULL) {
		kfree(runtime);
		return -ENOMEM;
	}
	runtime->appl_ptr = runtime->hw_ptr = 0;
	substream->runtime = runtime;
	return 0;
}

static int snd_rawmidi_runtime_free(struct snd_rawmidi_substream *substream)
{
	struct snd_rawmidi_runtime *runtime = substream->runtime;

	kfree(runtime->buffer);
	kfree(runtime);
	substream->runtime = NULL;
	return 0;
}

static inline void snd_rawmidi_output_trigger(struct snd_rawmidi_substream *substream,int up)
{
	if (!substream->opened)
		return;
	substream->ops->trigger(substream, up);
}

static void snd_rawmidi_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	if (!substream->opened)
		return;
	substream->ops->trigger(substream, up);
	if (!up)
		cancel_work_sync(&substream->runtime->event_work);
}

int snd_rawmidi_drop_output(struct snd_rawmidi_substream *substream)
{
	unsigned long flags;
	struct snd_rawmidi_runtime *runtime = substream->runtime;

	snd_rawmidi_output_trigger(substream, 0);
	runtime->drain = 0;
	spin_lock_irqsave(&runtime->lock, flags);
	runtime->appl_ptr = runtime->hw_ptr = 0;
	runtime->avail = runtime->buffer_size;
	spin_unlock_irqrestore(&runtime->lock, flags);
	return 0;
}
EXPORT_SYMBOL(snd_rawmidi_drop_output);

int snd_rawmidi_drain_output(struct snd_rawmidi_substream *substream)
{
	int err;
	long timeout;
	struct snd_rawmidi_runtime *runtime = substream->runtime;

	err = 0;
	runtime->drain = 1;
	timeout = wait_event_interruptible_timeout(runtime->sleep,
				(runtime->avail >= runtime->buffer_size),
				10*HZ);
	if (signal_pending(current))
		err = -ERESTARTSYS;
	if (runtime->avail < runtime->buffer_size && !timeout) {
		rmidi_warn(substream->rmidi,
			   "rawmidi drain error (avail = %li, buffer_size = %li)\n",
			   (long)runtime->avail, (long)runtime->buffer_size);
		err = -EIO;
	}
	runtime->drain = 0;
	if (err != -ERESTARTSYS) {
		/* we need wait a while to make sure that Tx FIFOs are empty */
		if (substream->ops->drain)
			substream->ops->drain(substream);
		else
			msleep(50);
		snd_rawmidi_drop_output(substream);
	}
	return err;
}
EXPORT_SYMBOL(snd_rawmidi_drain_output);

int snd_rawmidi_drain_input(struct snd_rawmidi_substream *substream)
{
	unsigned long flags;
	struct snd_rawmidi_runtime *runtime = substream->runtime;

	snd_rawmidi_input_trigger(substream, 0);
	runtime->drain = 0;
	spin_lock_irqsave(&runtime->lock, flags);
	runtime->appl_ptr = runtime->hw_ptr = 0;
	runtime->avail = 0;
	spin_unlock_irqrestore(&runtime->lock, flags);
	return 0;
}
EXPORT_SYMBOL(snd_rawmidi_drain_input);

/* look for an available substream for the given stream direction;
 * if a specific subdevice is given, try to assign it
 */
static int assign_substream(struct snd_rawmidi *rmidi, int subdevice,
			    int stream, int mode,
			    struct snd_rawmidi_substream **sub_ret)
{
	struct snd_rawmidi_substream *substream;
	struct snd_rawmidi_str *s = &rmidi->streams[stream];
	static unsigned int info_flags[2] = {
		[SNDRV_RAWMIDI_STREAM_OUTPUT] = SNDRV_RAWMIDI_INFO_OUTPUT,
		[SNDRV_RAWMIDI_STREAM_INPUT] = SNDRV_RAWMIDI_INFO_INPUT,
	};

	if (!(rmidi->info_flags & info_flags[stream]))
		return -ENXIO;
	if (subdevice >= 0 && subdevice >= s->substream_count)
		return -ENODEV;

	list_for_each_entry(substream, &s->substreams, list) {
		if (substream->opened) {
			if (stream == SNDRV_RAWMIDI_STREAM_INPUT ||
			    !(mode & SNDRV_RAWMIDI_LFLG_APPEND) ||
			    !substream->append)
				continue;
		}
		if (subdevice < 0 || subdevice == substream->number) {
			*sub_ret = substream;
			return 0;
		}
	}
	return -EAGAIN;
}

/* open and do ref-counting for the given substream */
static int open_substream(struct snd_rawmidi *rmidi,
			  struct snd_rawmidi_substream *substream,
			  int mode)
{
	int err;

	if (substream->use_count == 0) {
		err = snd_rawmidi_runtime_create(substream);
		if (err < 0)
			return err;
		err = substream->ops->open(substream);
		if (err < 0) {
			snd_rawmidi_runtime_free(substream);
			return err;
		}
		substream->opened = 1;
		substream->active_sensing = 0;
		if (mode & SNDRV_RAWMIDI_LFLG_APPEND)
			substream->append = 1;
		substream->pid = get_pid(task_pid(current));
		rmidi->streams[substream->stream].substream_opened++;
	}
	substream->use_count++;
	return 0;
}

static void close_substream(struct snd_rawmidi *rmidi,
			    struct snd_rawmidi_substream *substream,
			    int cleanup);

static int rawmidi_open_priv(struct snd_rawmidi *rmidi, int subdevice, int mode,
			     struct snd_rawmidi_file *rfile)
{
	struct snd_rawmidi_substream *sinput = NULL, *soutput = NULL;
	int err;

	rfile->input = rfile->output = NULL;
	if (mode & SNDRV_RAWMIDI_LFLG_INPUT) {
		err = assign_substream(rmidi, subdevice,
				       SNDRV_RAWMIDI_STREAM_INPUT,
				       mode, &sinput);
		if (err < 0)
			return err;
	}
	if (mode & SNDRV_RAWMIDI_LFLG_OUTPUT) {
		err = assign_substream(rmidi, subdevice,
				       SNDRV_RAWMIDI_STREAM_OUTPUT,
				       mode, &soutput);
		if (err < 0)
			return err;
	}

	if (sinput) {
		err = open_substream(rmidi, sinput, mode);
		if (err < 0)
			return err;
	}
	if (soutput) {
		err = open_substream(rmidi, soutput, mode);
		if (err < 0) {
			if (sinput)
				close_substream(rmidi, sinput, 0);
			return err;
		}
	}

	rfile->rmidi = rmidi;
	rfile->input = sinput;
	rfile->output = soutput;
	return 0;
}

/* called from sound/core/seq/seq_midi.c */
int snd_rawmidi_kernel_open(struct snd_card *card, int device, int subdevice,
			    int mode, struct snd_rawmidi_file * rfile)
{
	struct snd_rawmidi *rmidi;
	int err;

	if (snd_BUG_ON(!rfile))
		return -EINVAL;

	mutex_lock(&register_mutex);
	rmidi = snd_rawmidi_search(card, device);
	if (rmidi == NULL) {
		mutex_unlock(&register_mutex);
		return -ENODEV;
	}
	if (!try_module_get(rmidi->card->module)) {
		mutex_unlock(&register_mutex);
		return -ENXIO;
	}
	mutex_unlock(&register_mutex);

	mutex_lock(&rmidi->open_mutex);
	err = rawmidi_open_priv(rmidi, subdevice, mode, rfile);
	mutex_unlock(&rmidi->open_mutex);
	if (err < 0)
		module_put(rmidi->card->module);
	return err;
}
EXPORT_SYMBOL(snd_rawmidi_kernel_open);

static int snd_rawmidi_open(struct inode *inode, struct file *file)
{
	int maj = imajor(inode);
	struct snd_card *card;
	int subdevice;
	unsigned short fflags;
	int err;
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_file *rawmidi_file = NULL;
	wait_queue_t wait;

	if ((file->f_flags & O_APPEND) && !(file->f_flags & O_NONBLOCK)) 
		return -EINVAL;		/* invalid combination */

	err = nonseekable_open(inode, file);
	if (err < 0)
		return err;

	if (maj == snd_major) {
		rmidi = snd_lookup_minor_data(iminor(inode),
					      SNDRV_DEVICE_TYPE_RAWMIDI);
#ifdef CONFIG_SND_OSSEMUL
	} else if (maj == SOUND_MAJOR) {
		rmidi = snd_lookup_oss_minor_data(iminor(inode),
						  SNDRV_OSS_DEVICE_TYPE_MIDI);
#endif
	} else
		return -ENXIO;

	if (rmidi == NULL)
		return -ENODEV;

	if (!try_module_get(rmidi->card->module)) {
		snd_card_unref(rmidi->card);
		return -ENXIO;
	}

	mutex_lock(&rmidi->open_mutex);
	card = rmidi->card;
	err = snd_card_file_add(card, file);
	if (err < 0)
		goto __error_card;
	fflags = snd_rawmidi_file_flags(file);
	if ((file->f_flags & O_APPEND) || maj == SOUND_MAJOR) /* OSS emul? */
		fflags |= SNDRV_RAWMIDI_LFLG_APPEND;
	rawmidi_file = kmalloc(sizeof(*rawmidi_file), GFP_KERNEL);
	if (rawmidi_file == NULL) {
		err = -ENOMEM;
		goto __error;
	}
	init_waitqueue_entry(&wait, current);
	add_wait_queue(&rmidi->open_wait, &wait);
	while (1) {
		subdevice = snd_ctl_get_preferred_subdevice(card, SND_CTL_SUBDEV_RAWMIDI);
		err = rawmidi_open_priv(rmidi, subdevice, fflags, rawmidi_file);
		if (err >= 0)
			break;
		if (err == -EAGAIN) {
			if (file->f_flags & O_NONBLOCK) {
				err = -EBUSY;
				break;
			}
		} else
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&rmidi->open_mutex);
		schedule();
		mutex_lock(&rmidi->open_mutex);
		if (rmidi->card->shutdown) {
			err = -ENODEV;
			break;
		}
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}
	}
	remove_wait_queue(&rmidi->open_wait, &wait);
	if (err < 0) {
		kfree(rawmidi_file);
		goto __error;
	}
#ifdef CONFIG_SND_OSSEMUL
	if (rawmidi_file->input && rawmidi_file->input->runtime)
		rawmidi_file->input->runtime->oss = (maj == SOUND_MAJOR);
	if (rawmidi_file->output && rawmidi_file->output->runtime)
		rawmidi_file->output->runtime->oss = (maj == SOUND_MAJOR);
#endif
	file->private_data = rawmidi_file;
	mutex_unlock(&rmidi->open_mutex);
	snd_card_unref(rmidi->card);
	return 0;

 __error:
	snd_card_file_remove(card, file);
 __error_card:
	mutex_unlock(&rmidi->open_mutex);
	module_put(rmidi->card->module);
	snd_card_unref(rmidi->card);
	return err;
}

static void close_substream(struct snd_rawmidi *rmidi,
			    struct snd_rawmidi_substream *substream,
			    int cleanup)
{
	if (--substream->use_count)
		return;

	if (cleanup) {
		if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT)
			snd_rawmidi_input_trigger(substream, 0);
		else {
			if (substream->active_sensing) {
				unsigned char buf = 0xfe;
				/* sending single active sensing message
				 * to shut the device up
				 */
				snd_rawmidi_kernel_write(substream, &buf, 1);
			}
			if (snd_rawmidi_drain_output(substream) == -ERESTARTSYS)
				snd_rawmidi_output_trigger(substream, 0);
		}
	}
	substream->ops->close(substream);
	if (substream->runtime->private_free)
		substream->runtime->private_free(substream);
	snd_rawmidi_runtime_free(substream);
	substream->opened = 0;
	substream->append = 0;
	put_pid(substream->pid);
	substream->pid = NULL;
	rmidi->streams[substream->stream].substream_opened--;
}

static void rawmidi_release_priv(struct snd_rawmidi_file *rfile)
{
	struct snd_rawmidi *rmidi;

	rmidi = rfile->rmidi;
	mutex_lock(&rmidi->open_mutex);
	if (rfile->input) {
		close_substream(rmidi, rfile->input, 1);
		rfile->input = NULL;
	}
	if (rfile->output) {
		close_substream(rmidi, rfile->output, 1);
		rfile->output = NULL;
	}
	rfile->rmidi = NULL;
	mutex_unlock(&rmidi->open_mutex);
	wake_up(&rmidi->open_wait);
}

/* called from sound/core/seq/seq_midi.c */
int snd_rawmidi_kernel_release(struct snd_rawmidi_file *rfile)
{
	struct snd_rawmidi *rmidi;

	if (snd_BUG_ON(!rfile))
		return -ENXIO;
	
	rmidi = rfile->rmidi;
	rawmidi_release_priv(rfile);
	module_put(rmidi->card->module);
	return 0;
}
EXPORT_SYMBOL(snd_rawmidi_kernel_release);

static int snd_rawmidi_release(struct inode *inode, struct file *file)
{
	struct snd_rawmidi_file *rfile;
	struct snd_rawmidi *rmidi;
	struct module *module;

	rfile = file->private_data;
	rmidi = rfile->rmidi;
	rawmidi_release_priv(rfile);
	kfree(rfile);
	module = rmidi->card->module;
	snd_card_file_remove(rmidi->card, file);
	module_put(module);
	return 0;
}

static int snd_rawmidi_info(struct snd_rawmidi_substream *substream,
			    struct snd_rawmidi_info *info)
{
	struct snd_rawmidi *rmidi;
	
	if (substream == NULL)
		return -ENODEV;
	rmidi = substream->rmidi;
	memset(info, 0, sizeof(*info));
	info->card = rmidi->card->number;
	info->device = rmidi->device;
	info->subdevice = substream->number;
	info->stream = substream->stream;
	info->flags = rmidi->info_flags;
	strcpy(info->id, rmidi->id);
	strcpy(info->name, rmidi->name);
	strcpy(info->subname, substream->name);
	info->subdevices_count = substream->pstr->substream_count;
	info->subdevices_avail = (substream->pstr->substream_count -
				  substream->pstr->substream_opened);
	return 0;
}

static int snd_rawmidi_info_user(struct snd_rawmidi_substream *substream,
				 struct snd_rawmidi_info __user * _info)
{
	struct snd_rawmidi_info info;
	int err;
	if ((err = snd_rawmidi_info(substream, &info)) < 0)
		return err;
	if (copy_to_user(_info, &info, sizeof(struct snd_rawmidi_info)))
		return -EFAULT;
	return 0;
}

static int __snd_rawmidi_info_select(struct snd_card *card,
				     struct snd_rawmidi_info *info)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_str *pstr;
	struct snd_rawmidi_substream *substream;

	rmidi = snd_rawmidi_search(card, info->device);
	if (!rmidi)
		return -ENXIO;
	if (info->stream < 0 || info->stream > 1)
		return -EINVAL;
	pstr = &rmidi->streams[info->stream];
	if (pstr->substream_count == 0)
		return -ENOENT;
	if (info->subdevice >= pstr->substream_count)
		return -ENXIO;
	list_for_each_entry(substream, &pstr->substreams, list) {
		if ((unsigned int)substream->number == info->subdevice)
			return snd_rawmidi_info(substream, info);
	}
	return -ENXIO;
}

int snd_rawmidi_info_select(struct snd_card *card, struct snd_rawmidi_info *info)
{
	int ret;

	mutex_lock(&register_mutex);
	ret = __snd_rawmidi_info_select(card, info);
	mutex_unlock(&register_mutex);
	return ret;
}
EXPORT_SYMBOL(snd_rawmidi_info_select);

static int snd_rawmidi_info_select_user(struct snd_card *card,
					struct snd_rawmidi_info __user *_info)
{
	int err;
	struct snd_rawmidi_info info;
	if (get_user(info.device, &_info->device))
		return -EFAULT;
	if (get_user(info.stream, &_info->stream))
		return -EFAULT;
	if (get_user(info.subdevice, &_info->subdevice))
		return -EFAULT;
	if ((err = snd_rawmidi_info_select(card, &info)) < 0)
		return err;
	if (copy_to_user(_info, &info, sizeof(struct snd_rawmidi_info)))
		return -EFAULT;
	return 0;
}

int snd_rawmidi_output_params(struct snd_rawmidi_substream *substream,
			      struct snd_rawmidi_params * params)
{
	char *newbuf;
	struct snd_rawmidi_runtime *runtime = substream->runtime;
	
	if (substream->append && substream->use_count > 1)
		return -EBUSY;
	snd_rawmidi_drain_output(substream);
	if (params->buffer_size < 32 || params->buffer_size > 1024L * 1024L) {
		return -EINVAL;
	}
	if (params->avail_min < 1 || params->avail_min > params->buffer_size) {
		return -EINVAL;
	}
	if (params->buffer_size != runtime->buffer_size) {
		newbuf = krealloc(runtime->buffer, params->buffer_size,
				  GFP_KERNEL);
		if (!newbuf)
			return -ENOMEM;
		runtime->buffer = newbuf;
		runtime->buffer_size = params->buffer_size;
		runtime->avail = runtime->buffer_size;
	}
	runtime->avail_min = params->avail_min;
	substream->active_sensing = !params->no_active_sensing;
	return 0;
}
EXPORT_SYMBOL(snd_rawmidi_output_params);

int snd_rawmidi_input_params(struct snd_rawmidi_substream *substream,
			     struct snd_rawmidi_params * params)
{
	char *newbuf;
	struct snd_rawmidi_runtime *runtime = substream->runtime;

	snd_rawmidi_drain_input(substream);
	if (params->buffer_size < 32 || params->buffer_size > 1024L * 1024L) {
		return -EINVAL;
	}
	if (params->avail_min < 1 || params->avail_min > params->buffer_size) {
		return -EINVAL;
	}
	if (params->buffer_size != runtime->buffer_size) {
		newbuf = krealloc(runtime->buffer, params->buffer_size,
				  GFP_KERNEL);
		if (!newbuf)
			return -ENOMEM;
		runtime->buffer = newbuf;
		runtime->buffer_size = params->buffer_size;
	}
	runtime->avail_min = params->avail_min;
	return 0;
}
EXPORT_SYMBOL(snd_rawmidi_input_params);

static int snd_rawmidi_output_status(struct snd_rawmidi_substream *substream,
				     struct snd_rawmidi_status * status)
{
	struct snd_rawmidi_runtime *runtime = substream->runtime;

	memset(status, 0, sizeof(*status));
	status->stream = SNDRV_RAWMIDI_STREAM_OUTPUT;
	spin_lock_irq(&runtime->lock);
	status->avail = runtime->avail;
	spin_unlock_irq(&runtime->lock);
	return 0;
}

static int snd_rawmidi_input_status(struct snd_rawmidi_substream *substream,
				    struct snd_rawmidi_status * status)
{
	struct snd_rawmidi_runtime *runtime = substream->runtime;

	memset(status, 0, sizeof(*status));
	status->stream = SNDRV_RAWMIDI_STREAM_INPUT;
	spin_lock_irq(&runtime->lock);
	status->avail = runtime->avail;
	status->xruns = runtime->xruns;
	runtime->xruns = 0;
	spin_unlock_irq(&runtime->lock);
	return 0;
}

static long snd_rawmidi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct snd_rawmidi_file *rfile;
	void __user *argp = (void __user *)arg;

	rfile = file->private_data;
	if (((cmd >> 8) & 0xff) != 'W')
		return -ENOTTY;
	switch (cmd) {
	case SNDRV_RAWMIDI_IOCTL_PVERSION:
		return put_user(SNDRV_RAWMIDI_VERSION, (int __user *)argp) ? -EFAULT : 0;
	case SNDRV_RAWMIDI_IOCTL_INFO:
	{
		int stream;
		struct snd_rawmidi_info __user *info = argp;
		if (get_user(stream, &info->stream))
			return -EFAULT;
		switch (stream) {
		case SNDRV_RAWMIDI_STREAM_INPUT:
			return snd_rawmidi_info_user(rfile->input, info);
		case SNDRV_RAWMIDI_STREAM_OUTPUT:
			return snd_rawmidi_info_user(rfile->output, info);
		default:
			return -EINVAL;
		}
	}
	case SNDRV_RAWMIDI_IOCTL_PARAMS:
	{
		struct snd_rawmidi_params params;
		if (copy_from_user(&params, argp, sizeof(struct snd_rawmidi_params)))
			return -EFAULT;
		switch (params.stream) {
		case SNDRV_RAWMIDI_STREAM_OUTPUT:
			if (rfile->output == NULL)
				return -EINVAL;
			return snd_rawmidi_output_params(rfile->output, &params);
		case SNDRV_RAWMIDI_STREAM_INPUT:
			if (rfile->input == NULL)
				return -EINVAL;
			return snd_rawmidi_input_params(rfile->input, &params);
		default:
			return -EINVAL;
		}
	}
	case SNDRV_RAWMIDI_IOCTL_STATUS:
	{
		int err = 0;
		struct snd_rawmidi_status status;
		if (copy_from_user(&status, argp, sizeof(struct snd_rawmidi_status)))
			return -EFAULT;
		switch (status.stream) {
		case SNDRV_RAWMIDI_STREAM_OUTPUT:
			if (rfile->output == NULL)
				return -EINVAL;
			err = snd_rawmidi_output_status(rfile->output, &status);
			break;
		case SNDRV_RAWMIDI_STREAM_INPUT:
			if (rfile->input == NULL)
				return -EINVAL;
			err = snd_rawmidi_input_status(rfile->input, &status);
			break;
		default:
			return -EINVAL;
		}
		if (err < 0)
			return err;
		if (copy_to_user(argp, &status, sizeof(struct snd_rawmidi_status)))
			return -EFAULT;
		return 0;
	}
	case SNDRV_RAWMIDI_IOCTL_DROP:
	{
		int val;
		if (get_user(val, (int __user *) argp))
			return -EFAULT;
		switch (val) {
		case SNDRV_RAWMIDI_STREAM_OUTPUT:
			if (rfile->output == NULL)
				return -EINVAL;
			return snd_rawmidi_drop_output(rfile->output);
		default:
			return -EINVAL;
		}
	}
	case SNDRV_RAWMIDI_IOCTL_DRAIN:
	{
		int val;
		if (get_user(val, (int __user *) argp))
			return -EFAULT;
		switch (val) {
		case SNDRV_RAWMIDI_STREAM_OUTPUT:
			if (rfile->output == NULL)
				return -EINVAL;
			return snd_rawmidi_drain_output(rfile->output);
		case SNDRV_RAWMIDI_STREAM_INPUT:
			if (rfile->input == NULL)
				return -EINVAL;
			return snd_rawmidi_drain_input(rfile->input);
		default:
			return -EINVAL;
		}
	}
	default:
		rmidi_dbg(rfile->rmidi,
			  "rawmidi: unknown command = 0x%x\n", cmd);
	}
	return -ENOTTY;
}

static int snd_rawmidi_control_ioctl(struct snd_card *card,
				     struct snd_ctl_file *control,
				     unsigned int cmd,
				     unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case SNDRV_CTL_IOCTL_RAWMIDI_NEXT_DEVICE:
	{
		int device;
		
		if (get_user(device, (int __user *)argp))
			return -EFAULT;
		if (device >= SNDRV_RAWMIDI_DEVICES) /* next device is -1 */
			device = SNDRV_RAWMIDI_DEVICES - 1;
		mutex_lock(&register_mutex);
		device = device < 0 ? 0 : device + 1;
		while (device < SNDRV_RAWMIDI_DEVICES) {
			if (snd_rawmidi_search(card, device))
				break;
			device++;
		}
		if (device == SNDRV_RAWMIDI_DEVICES)
			device = -1;
		mutex_unlock(&register_mutex);
		if (put_user(device, (int __user *)argp))
			return -EFAULT;
		return 0;
	}
	case SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE:
	{
		int val;
		
		if (get_user(val, (int __user *)argp))
			return -EFAULT;
		control->preferred_subdevice[SND_CTL_SUBDEV_RAWMIDI] = val;
		return 0;
	}
	case SNDRV_CTL_IOCTL_RAWMIDI_INFO:
		return snd_rawmidi_info_select_user(card, argp);
	}
	return -ENOIOCTLCMD;
}

/**
 * snd_rawmidi_receive - receive the input data from the device
 * @substream: the rawmidi substream
 * @buffer: the buffer pointer
 * @count: the data size to read
 *
 * Reads the data from the internal buffer.
 *
 * Return: The size of read data, or a negative error code on failure.
 */
int snd_rawmidi_receive(struct snd_rawmidi_substream *substream,
			const unsigned char *buffer, int count)
{
	unsigned long flags;
	int result = 0, count1;
	struct snd_rawmidi_runtime *runtime = substream->runtime;

	if (!substream->opened)
		return -EBADFD;
	if (runtime->buffer == NULL) {
		rmidi_dbg(substream->rmidi,
			  "snd_rawmidi_receive: input is not active!!!\n");
		return -EINVAL;
	}
	spin_lock_irqsave(&runtime->lock, flags);
	if (count == 1) {	/* special case, faster code */
		substream->bytes++;
		if (runtime->avail < runtime->buffer_size) {
			runtime->buffer[runtime->hw_ptr++] = buffer[0];
			runtime->hw_ptr %= runtime->buffer_size;
			runtime->avail++;
			result++;
		} else {
			runtime->xruns++;
		}
	} else {
		substream->bytes += count;
		count1 = runtime->buffer_size - runtime->hw_ptr;
		if (count1 > count)
			count1 = count;
		if (count1 > (int)(runtime->buffer_size - runtime->avail))
			count1 = runtime->buffer_size - runtime->avail;
		memcpy(runtime->buffer + runtime->hw_ptr, buffer, count1);
		runtime->hw_ptr += count1;
		runtime->hw_ptr %= runtime->buffer_size;
		runtime->avail += count1;
		count -= count1;
		result += count1;
		if (count > 0) {
			buffer += count1;
			count1 = count;
			if (count1 > (int)(runtime->buffer_size - runtime->avail)) {
				count1 = runtime->buffer_size - runtime->avail;
				runtime->xruns += count - count1;
			}
			if (count1 > 0) {
				memcpy(runtime->buffer, buffer, count1);
				runtime->hw_ptr = count1;
				runtime->avail += count1;
				result += count1;
			}
		}
	}
	if (result > 0) {
		if (runtime->event)
			schedule_work(&runtime->event_work);
		else if (snd_rawmidi_ready(substream))
			wake_up(&runtime->sleep);
	}
	spin_unlock_irqrestore(&runtime->lock, flags);
	return result;
}
EXPORT_SYMBOL(snd_rawmidi_receive);

static long snd_rawmidi_kernel_read1(struct snd_rawmidi_substream *substream,
				     unsigned char __user *userbuf,
				     unsigned char *kernelbuf, long count)
{
	unsigned long flags;
	long result = 0, count1;
	struct snd_rawmidi_runtime *runtime = substream->runtime;
	unsigned long appl_ptr;

	spin_lock_irqsave(&runtime->lock, flags);
	while (count > 0 && runtime->avail) {
		count1 = runtime->buffer_size - runtime->appl_ptr;
		if (count1 > count)
			count1 = count;
		if (count1 > (int)runtime->avail)
			count1 = runtime->avail;

		/* update runtime->appl_ptr before unlocking for userbuf */
		appl_ptr = runtime->appl_ptr;
		runtime->appl_ptr += count1;
		runtime->appl_ptr %= runtime->buffer_size;
		runtime->avail -= count1;

		if (kernelbuf)
			memcpy(kernelbuf + result, runtime->buffer + appl_ptr, count1);
		if (userbuf) {
			spin_unlock_irqrestore(&runtime->lock, flags);
			if (copy_to_user(userbuf + result,
					 runtime->buffer + appl_ptr, count1)) {
				return result > 0 ? result : -EFAULT;
			}
			spin_lock_irqsave(&runtime->lock, flags);
		}
		result += count1;
		count -= count1;
	}
	spin_unlock_irqrestore(&runtime->lock, flags);
	return result;
}

long snd_rawmidi_kernel_read(struct snd_rawmidi_substream *substream,
			     unsigned char *buf, long count)
{
	snd_rawmidi_input_trigger(substream, 1);
	return snd_rawmidi_kernel_read1(substream, NULL/*userbuf*/, buf, count);
}
EXPORT_SYMBOL(snd_rawmidi_kernel_read);

static ssize_t snd_rawmidi_read(struct file *file, char __user *buf, size_t count,
				loff_t *offset)
{
	long result;
	int count1;
	struct snd_rawmidi_file *rfile;
	struct snd_rawmidi_substream *substream;
	struct snd_rawmidi_runtime *runtime;

	rfile = file->private_data;
	substream = rfile->input;
	if (substream == NULL)
		return -EIO;
	runtime = substream->runtime;
	snd_rawmidi_input_trigger(substream, 1);
	result = 0;
	while (count > 0) {
		spin_lock_irq(&runtime->lock);
		while (!snd_rawmidi_ready(substream)) {
			wait_queue_t wait;
			if ((file->f_flags & O_NONBLOCK) != 0 || result > 0) {
				spin_unlock_irq(&runtime->lock);
				return result > 0 ? result : -EAGAIN;
			}
			init_waitqueue_entry(&wait, current);
			add_wait_queue(&runtime->sleep, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(&runtime->lock);
			schedule();
			remove_wait_queue(&runtime->sleep, &wait);
			if (rfile->rmidi->card->shutdown)
				return -ENODEV;
			if (signal_pending(current))
				return result > 0 ? result : -ERESTARTSYS;
			if (!runtime->avail)
				return result > 0 ? result : -EIO;
			spin_lock_irq(&runtime->lock);
		}
		spin_unlock_irq(&runtime->lock);
		count1 = snd_rawmidi_kernel_read1(substream,
						  (unsigned char __user *)buf,
						  NULL/*kernelbuf*/,
						  count);
		if (count1 < 0)
			return result > 0 ? result : count1;
		result += count1;
		buf += count1;
		count -= count1;
	}
	return result;
}

/**
 * snd_rawmidi_transmit_empty - check whether the output buffer is empty
 * @substream: the rawmidi substream
 *
 * Return: 1 if the internal output buffer is empty, 0 if not.
 */
int snd_rawmidi_transmit_empty(struct snd_rawmidi_substream *substream)
{
	struct snd_rawmidi_runtime *runtime = substream->runtime;
	int result;
	unsigned long flags;

	if (runtime->buffer == NULL) {
		rmidi_dbg(substream->rmidi,
			  "snd_rawmidi_transmit_empty: output is not active!!!\n");
		return 1;
	}
	spin_lock_irqsave(&runtime->lock, flags);
	result = runtime->avail >= runtime->buffer_size;
	spin_unlock_irqrestore(&runtime->lock, flags);
	return result;		
}
EXPORT_SYMBOL(snd_rawmidi_transmit_empty);

/**
 * __snd_rawmidi_transmit_peek - copy data from the internal buffer
 * @substream: the rawmidi substream
 * @buffer: the buffer pointer
 * @count: data size to transfer
 *
 * This is a variant of snd_rawmidi_transmit_peek() without spinlock.
 */
int __snd_rawmidi_transmit_peek(struct snd_rawmidi_substream *substream,
			      unsigned char *buffer, int count)
{
	int result, count1;
	struct snd_rawmidi_runtime *runtime = substream->runtime;

	if (runtime->buffer == NULL) {
		rmidi_dbg(substream->rmidi,
			  "snd_rawmidi_transmit_peek: output is not active!!!\n");
		return -EINVAL;
	}
	result = 0;
	if (runtime->avail >= runtime->buffer_size) {
		/* warning: lowlevel layer MUST trigger down the hardware */
		goto __skip;
	}
	if (count == 1) {	/* special case, faster code */
		*buffer = runtime->buffer[runtime->hw_ptr];
		result++;
	} else {
		count1 = runtime->buffer_size - runtime->hw_ptr;
		if (count1 > count)
			count1 = count;
		if (count1 > (int)(runtime->buffer_size - runtime->avail))
			count1 = runtime->buffer_size - runtime->avail;
		memcpy(buffer, runtime->buffer + runtime->hw_ptr, count1);
		count -= count1;
		result += count1;
		if (count > 0) {
			if (count > (int)(runtime->buffer_size - runtime->avail - count1))
				count = runtime->buffer_size - runtime->avail - count1;
			memcpy(buffer + count1, runtime->buffer, count);
			result += count;
		}
	}
      __skip:
	return result;
}
EXPORT_SYMBOL(__snd_rawmidi_transmit_peek);

/**
 * snd_rawmidi_transmit_peek - copy data from the internal buffer
 * @substream: the rawmidi substream
 * @buffer: the buffer pointer
 * @count: data size to transfer
 *
 * Copies data from the internal output buffer to the given buffer.
 *
 * Call this in the interrupt handler when the midi output is ready,
 * and call snd_rawmidi_transmit_ack() after the transmission is
 * finished.
 *
 * Return: The size of copied data, or a negative error code on failure.
 */
int snd_rawmidi_transmit_peek(struct snd_rawmidi_substream *substream,
			      unsigned char *buffer, int count)
{
	struct snd_rawmidi_runtime *runtime = substream->runtime;
	int result;
	unsigned long flags;

	spin_lock_irqsave(&runtime->lock, flags);
	result = __snd_rawmidi_transmit_peek(substream, buffer, count);
	spin_unlock_irqrestore(&runtime->lock, flags);
	return result;
}
EXPORT_SYMBOL(snd_rawmidi_transmit_peek);

/**
 * __snd_rawmidi_transmit_ack - acknowledge the transmission
 * @substream: the rawmidi substream
 * @count: the transferred count
 *
 * This is a variant of __snd_rawmidi_transmit_ack() without spinlock.
 */
int __snd_rawmidi_transmit_ack(struct snd_rawmidi_substream *substream, int count)
{
	struct snd_rawmidi_runtime *runtime = substream->runtime;

	if (runtime->buffer == NULL) {
		rmidi_dbg(substream->rmidi,
			  "snd_rawmidi_transmit_ack: output is not active!!!\n");
		return -EINVAL;
	}
	snd_BUG_ON(runtime->avail + count > runtime->buffer_size);
	runtime->hw_ptr += count;
	runtime->hw_ptr %= runtime->buffer_size;
	runtime->avail += count;
	substream->bytes += count;
	if (count > 0) {
		if (runtime->drain || snd_rawmidi_ready(substream))
			wake_up(&runtime->sleep);
	}
	return count;
}
EXPORT_SYMBOL(__snd_rawmidi_transmit_ack);

/**
 * snd_rawmidi_transmit_ack - acknowledge the transmission
 * @substream: the rawmidi substream
 * @count: the transferred count
 *
 * Advances the hardware pointer for the internal output buffer with
 * the given size and updates the condition.
 * Call after the transmission is finished.
 *
 * Return: The advanced size if successful, or a negative error code on failure.
 */
int snd_rawmidi_transmit_ack(struct snd_rawmidi_substream *substream, int count)
{
	struct snd_rawmidi_runtime *runtime = substream->runtime;
	int result;
	unsigned long flags;

	spin_lock_irqsave(&runtime->lock, flags);
	result = __snd_rawmidi_transmit_ack(substream, count);
	spin_unlock_irqrestore(&runtime->lock, flags);
	return result;
}
EXPORT_SYMBOL(snd_rawmidi_transmit_ack);

/**
 * snd_rawmidi_transmit - copy from the buffer to the device
 * @substream: the rawmidi substream
 * @buffer: the buffer pointer
 * @count: the data size to transfer
 * 
 * Copies data from the buffer to the device and advances the pointer.
 *
 * Return: The copied size if successful, or a negative error code on failure.
 */
int snd_rawmidi_transmit(struct snd_rawmidi_substream *substream,
			 unsigned char *buffer, int count)
{
	struct snd_rawmidi_runtime *runtime = substream->runtime;
	int result;
	unsigned long flags;

	spin_lock_irqsave(&runtime->lock, flags);
	if (!substream->opened)
		result = -EBADFD;
	else {
		count = __snd_rawmidi_transmit_peek(substream, buffer, count);
		if (count <= 0)
			result = count;
		else
			result = __snd_rawmidi_transmit_ack(substream, count);
	}
	spin_unlock_irqrestore(&runtime->lock, flags);
	return result;
}
EXPORT_SYMBOL(snd_rawmidi_transmit);

static long snd_rawmidi_kernel_write1(struct snd_rawmidi_substream *substream,
				      const unsigned char __user *userbuf,
				      const unsigned char *kernelbuf,
				      long count)
{
	unsigned long flags;
	long count1, result;
	struct snd_rawmidi_runtime *runtime = substream->runtime;
	unsigned long appl_ptr;

	if (!kernelbuf && !userbuf)
		return -EINVAL;
	if (snd_BUG_ON(!runtime->buffer))
		return -EINVAL;

	result = 0;
	spin_lock_irqsave(&runtime->lock, flags);
	if (substream->append) {
		if ((long)runtime->avail < count) {
			spin_unlock_irqrestore(&runtime->lock, flags);
			return -EAGAIN;
		}
	}
	while (count > 0 && runtime->avail > 0) {
		count1 = runtime->buffer_size - runtime->appl_ptr;
		if (count1 > count)
			count1 = count;
		if (count1 > (long)runtime->avail)
			count1 = runtime->avail;

		/* update runtime->appl_ptr before unlocking for userbuf */
		appl_ptr = runtime->appl_ptr;
		runtime->appl_ptr += count1;
		runtime->appl_ptr %= runtime->buffer_size;
		runtime->avail -= count1;

		if (kernelbuf)
			memcpy(runtime->buffer + appl_ptr,
			       kernelbuf + result, count1);
		else if (userbuf) {
			spin_unlock_irqrestore(&runtime->lock, flags);
			if (copy_from_user(runtime->buffer + appl_ptr,
					   userbuf + result, count1)) {
				spin_lock_irqsave(&runtime->lock, flags);
				result = result > 0 ? result : -EFAULT;
				goto __end;
			}
			spin_lock_irqsave(&runtime->lock, flags);
		}
		result += count1;
		count -= count1;
	}
      __end:
	count1 = runtime->avail < runtime->buffer_size;
	spin_unlock_irqrestore(&runtime->lock, flags);
	if (count1)
		snd_rawmidi_output_trigger(substream, 1);
	return result;
}

long snd_rawmidi_kernel_write(struct snd_rawmidi_substream *substream,
			      const unsigned char *buf, long count)
{
	return snd_rawmidi_kernel_write1(substream, NULL, buf, count);
}
EXPORT_SYMBOL(snd_rawmidi_kernel_write);

static ssize_t snd_rawmidi_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *offset)
{
	long result, timeout;
	int count1;
	struct snd_rawmidi_file *rfile;
	struct snd_rawmidi_runtime *runtime;
	struct snd_rawmidi_substream *substream;

	rfile = file->private_data;
	substream = rfile->output;
	runtime = substream->runtime;
	/* we cannot put an atomic message to our buffer */
	if (substream->append && count > runtime->buffer_size)
		return -EIO;
	result = 0;
	while (count > 0) {
		spin_lock_irq(&runtime->lock);
		while (!snd_rawmidi_ready_append(substream, count)) {
			wait_queue_t wait;
			if (file->f_flags & O_NONBLOCK) {
				spin_unlock_irq(&runtime->lock);
				return result > 0 ? result : -EAGAIN;
			}
			init_waitqueue_entry(&wait, current);
			add_wait_queue(&runtime->sleep, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(&runtime->lock);
			timeout = schedule_timeout(30 * HZ);
			remove_wait_queue(&runtime->sleep, &wait);
			if (rfile->rmidi->card->shutdown)
				return -ENODEV;
			if (signal_pending(current))
				return result > 0 ? result : -ERESTARTSYS;
			if (!runtime->avail && !timeout)
				return result > 0 ? result : -EIO;
			spin_lock_irq(&runtime->lock);
		}
		spin_unlock_irq(&runtime->lock);
		count1 = snd_rawmidi_kernel_write1(substream, buf, NULL, count);
		if (count1 < 0)
			return result > 0 ? result : count1;
		result += count1;
		buf += count1;
		if ((size_t)count1 < count && (file->f_flags & O_NONBLOCK))
			break;
		count -= count1;
	}
	if (file->f_flags & O_DSYNC) {
		spin_lock_irq(&runtime->lock);
		while (runtime->avail != runtime->buffer_size) {
			wait_queue_t wait;
			unsigned int last_avail = runtime->avail;
			init_waitqueue_entry(&wait, current);
			add_wait_queue(&runtime->sleep, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(&runtime->lock);
			timeout = schedule_timeout(30 * HZ);
			remove_wait_queue(&runtime->sleep, &wait);
			if (signal_pending(current))
				return result > 0 ? result : -ERESTARTSYS;
			if (runtime->avail == last_avail && !timeout)
				return result > 0 ? result : -EIO;
			spin_lock_irq(&runtime->lock);
		}
		spin_unlock_irq(&runtime->lock);
	}
	return result;
}

static unsigned int snd_rawmidi_poll(struct file *file, poll_table * wait)
{
	struct snd_rawmidi_file *rfile;
	struct snd_rawmidi_runtime *runtime;
	unsigned int mask;

	rfile = file->private_data;
	if (rfile->input != NULL) {
		runtime = rfile->input->runtime;
		snd_rawmidi_input_trigger(rfile->input, 1);
		poll_wait(file, &runtime->sleep, wait);
	}
	if (rfile->output != NULL) {
		runtime = rfile->output->runtime;
		poll_wait(file, &runtime->sleep, wait);
	}
	mask = 0;
	if (rfile->input != NULL) {
		if (snd_rawmidi_ready(rfile->input))
			mask |= POLLIN | POLLRDNORM;
	}
	if (rfile->output != NULL) {
		if (snd_rawmidi_ready(rfile->output))
			mask |= POLLOUT | POLLWRNORM;
	}
	return mask;
}

/*
 */
#ifdef CONFIG_COMPAT
#include "rawmidi_compat.c"
#else
#define snd_rawmidi_ioctl_compat	NULL
#endif

/*

 */

static void snd_rawmidi_proc_info_read(struct snd_info_entry *entry,
				       struct snd_info_buffer *buffer)
{
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_substream *substream;
	struct snd_rawmidi_runtime *runtime;

	rmidi = entry->private_data;
	snd_iprintf(buffer, "%s\n\n", rmidi->name);
	mutex_lock(&rmidi->open_mutex);
	if (rmidi->info_flags & SNDRV_RAWMIDI_INFO_OUTPUT) {
		list_for_each_entry(substream,
				    &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams,
				    list) {
			snd_iprintf(buffer,
				    "Output %d\n"
				    "  Tx bytes     : %lu\n",
				    substream->number,
				    (unsigned long) substream->bytes);
			if (substream->opened) {
				snd_iprintf(buffer,
				    "  Owner PID    : %d\n",
				    pid_vnr(substream->pid));
				runtime = substream->runtime;
				snd_iprintf(buffer,
				    "  Mode         : %s\n"
				    "  Buffer size  : %lu\n"
				    "  Avail        : %lu\n",
				    runtime->oss ? "OSS compatible" : "native",
				    (unsigned long) runtime->buffer_size,
				    (unsigned long) runtime->avail);
			}
		}
	}
	if (rmidi->info_flags & SNDRV_RAWMIDI_INFO_INPUT) {
		list_for_each_entry(substream,
				    &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substreams,
				    list) {
			snd_iprintf(buffer,
				    "Input %d\n"
				    "  Rx bytes     : %lu\n",
				    substream->number,
				    (unsigned long) substream->bytes);
			if (substream->opened) {
				snd_iprintf(buffer,
					    "  Owner PID    : %d\n",
					    pid_vnr(substream->pid));
				runtime = substream->runtime;
				snd_iprintf(buffer,
					    "  Buffer size  : %lu\n"
					    "  Avail        : %lu\n"
					    "  Overruns     : %lu\n",
					    (unsigned long) runtime->buffer_size,
					    (unsigned long) runtime->avail,
					    (unsigned long) runtime->xruns);
			}
		}
	}
	mutex_unlock(&rmidi->open_mutex);
}

/*
 *  Register functions
 */

static const struct file_operations snd_rawmidi_f_ops =
{
	.owner =	THIS_MODULE,
	.read =		snd_rawmidi_read,
	.write =	snd_rawmidi_write,
	.open =		snd_rawmidi_open,
	.release =	snd_rawmidi_release,
	.llseek =	no_llseek,
	.poll =		snd_rawmidi_poll,
	.unlocked_ioctl =	snd_rawmidi_ioctl,
	.compat_ioctl =	snd_rawmidi_ioctl_compat,
};

static int snd_rawmidi_alloc_substreams(struct snd_rawmidi *rmidi,
					struct snd_rawmidi_str *stream,
					int direction,
					int count)
{
	struct snd_rawmidi_substream *substream;
	int idx;

	for (idx = 0; idx < count; idx++) {
		substream = kzalloc(sizeof(*substream), GFP_KERNEL);
		if (!substream)
			return -ENOMEM;
		substream->stream = direction;
		substream->number = idx;
		substream->rmidi = rmidi;
		substream->pstr = stream;
		list_add_tail(&substream->list, &stream->substreams);
		stream->substream_count++;
	}
	return 0;
}

static void release_rawmidi_device(struct device *dev)
{
	kfree(container_of(dev, struct snd_rawmidi, dev));
}

/**
 * snd_rawmidi_new - create a rawmidi instance
 * @card: the card instance
 * @id: the id string
 * @device: the device index
 * @output_count: the number of output streams
 * @input_count: the number of input streams
 * @rrawmidi: the pointer to store the new rawmidi instance
 *
 * Creates a new rawmidi instance.
 * Use snd_rawmidi_set_ops() to set the operators to the new instance.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_rawmidi_new(struct snd_card *card, char *id, int device,
		    int output_count, int input_count,
		    struct snd_rawmidi ** rrawmidi)
{
	struct snd_rawmidi *rmidi;
	int err;
	static struct snd_device_ops ops = {
		.dev_free = snd_rawmidi_dev_free,
		.dev_register = snd_rawmidi_dev_register,
		.dev_disconnect = snd_rawmidi_dev_disconnect,
	};

	if (snd_BUG_ON(!card))
		return -ENXIO;
	if (rrawmidi)
		*rrawmidi = NULL;
	rmidi = kzalloc(sizeof(*rmidi), GFP_KERNEL);
	if (!rmidi)
		return -ENOMEM;
	rmidi->card = card;
	rmidi->device = device;
	mutex_init(&rmidi->open_mutex);
	init_waitqueue_head(&rmidi->open_wait);
	INIT_LIST_HEAD(&rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substreams);
	INIT_LIST_HEAD(&rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams);

	if (id != NULL)
		strlcpy(rmidi->id, id, sizeof(rmidi->id));

	snd_device_initialize(&rmidi->dev, card);
	rmidi->dev.release = release_rawmidi_device;
	dev_set_name(&rmidi->dev, "midiC%iD%i", card->number, device);

	if ((err = snd_rawmidi_alloc_substreams(rmidi,
						&rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT],
						SNDRV_RAWMIDI_STREAM_INPUT,
						input_count)) < 0) {
		snd_rawmidi_free(rmidi);
		return err;
	}
	if ((err = snd_rawmidi_alloc_substreams(rmidi,
						&rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT],
						SNDRV_RAWMIDI_STREAM_OUTPUT,
						output_count)) < 0) {
		snd_rawmidi_free(rmidi);
		return err;
	}
	if ((err = snd_device_new(card, SNDRV_DEV_RAWMIDI, rmidi, &ops)) < 0) {
		snd_rawmidi_free(rmidi);
		return err;
	}
	if (rrawmidi)
		*rrawmidi = rmidi;
	return 0;
}
EXPORT_SYMBOL(snd_rawmidi_new);

static void snd_rawmidi_free_substreams(struct snd_rawmidi_str *stream)
{
	struct snd_rawmidi_substream *substream;

	while (!list_empty(&stream->substreams)) {
		substream = list_entry(stream->substreams.next, struct snd_rawmidi_substream, list);
		list_del(&substream->list);
		kfree(substream);
	}
}

static int snd_rawmidi_free(struct snd_rawmidi *rmidi)
{
	if (!rmidi)
		return 0;

	snd_info_free_entry(rmidi->proc_entry);
	rmidi->proc_entry = NULL;
	mutex_lock(&register_mutex);
	if (rmidi->ops && rmidi->ops->dev_unregister)
		rmidi->ops->dev_unregister(rmidi);
	mutex_unlock(&register_mutex);

	snd_rawmidi_free_substreams(&rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT]);
	snd_rawmidi_free_substreams(&rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT]);
	if (rmidi->private_free)
		rmidi->private_free(rmidi);
	put_device(&rmidi->dev);
	return 0;
}

static int snd_rawmidi_dev_free(struct snd_device *device)
{
	struct snd_rawmidi *rmidi = device->device_data;
	return snd_rawmidi_free(rmidi);
}

#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
static void snd_rawmidi_dev_seq_free(struct snd_seq_device *device)
{
	struct snd_rawmidi *rmidi = device->private_data;
	rmidi->seq_dev = NULL;
}
#endif

static int snd_rawmidi_dev_register(struct snd_device *device)
{
	int err;
	struct snd_info_entry *entry;
	char name[16];
	struct snd_rawmidi *rmidi = device->device_data;

	if (rmidi->device >= SNDRV_RAWMIDI_DEVICES)
		return -ENOMEM;
	mutex_lock(&register_mutex);
	if (snd_rawmidi_search(rmidi->card, rmidi->device)) {
		mutex_unlock(&register_mutex);
		return -EBUSY;
	}
	list_add_tail(&rmidi->list, &snd_rawmidi_devices);
	mutex_unlock(&register_mutex);
	err = snd_register_device(SNDRV_DEVICE_TYPE_RAWMIDI,
				  rmidi->card, rmidi->device,
				  &snd_rawmidi_f_ops, rmidi, &rmidi->dev);
	if (err < 0) {
		rmidi_err(rmidi, "unable to register\n");
		mutex_lock(&register_mutex);
		list_del(&rmidi->list);
		mutex_unlock(&register_mutex);
		return err;
	}
	if (rmidi->ops && rmidi->ops->dev_register &&
	    (err = rmidi->ops->dev_register(rmidi)) < 0) {
		snd_unregister_device(&rmidi->dev);
		mutex_lock(&register_mutex);
		list_del(&rmidi->list);
		mutex_unlock(&register_mutex);
		return err;
	}
#ifdef CONFIG_SND_OSSEMUL
	rmidi->ossreg = 0;
	if ((int)rmidi->device == midi_map[rmidi->card->number]) {
		if (snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_MIDI,
					    rmidi->card, 0, &snd_rawmidi_f_ops,
					    rmidi) < 0) {
			rmidi_err(rmidi,
				  "unable to register OSS rawmidi device %i:%i\n",
				  rmidi->card->number, 0);
		} else {
			rmidi->ossreg++;
#ifdef SNDRV_OSS_INFO_DEV_MIDI
			snd_oss_info_register(SNDRV_OSS_INFO_DEV_MIDI, rmidi->card->number, rmidi->name);
#endif
		}
	}
	if ((int)rmidi->device == amidi_map[rmidi->card->number]) {
		if (snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_MIDI,
					    rmidi->card, 1, &snd_rawmidi_f_ops,
					    rmidi) < 0) {
			rmidi_err(rmidi,
				  "unable to register OSS rawmidi device %i:%i\n",
				  rmidi->card->number, 1);
		} else {
			rmidi->ossreg++;
		}
	}
#endif /* CONFIG_SND_OSSEMUL */
	sprintf(name, "midi%d", rmidi->device);
	entry = snd_info_create_card_entry(rmidi->card, name, rmidi->card->proc_root);
	if (entry) {
		entry->private_data = rmidi;
		entry->c.text.read = snd_rawmidi_proc_info_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	rmidi->proc_entry = entry;
#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
	if (!rmidi->ops || !rmidi->ops->dev_register) { /* own registration mechanism */
		if (snd_seq_device_new(rmidi->card, rmidi->device, SNDRV_SEQ_DEV_ID_MIDISYNTH, 0, &rmidi->seq_dev) >= 0) {
			rmidi->seq_dev->private_data = rmidi;
			rmidi->seq_dev->private_free = snd_rawmidi_dev_seq_free;
			sprintf(rmidi->seq_dev->name, "MIDI %d-%d", rmidi->card->number, rmidi->device);
			snd_device_register(rmidi->card, rmidi->seq_dev);
		}
	}
#endif
	return 0;
}

static int snd_rawmidi_dev_disconnect(struct snd_device *device)
{
	struct snd_rawmidi *rmidi = device->device_data;
	int dir;

	mutex_lock(&register_mutex);
	mutex_lock(&rmidi->open_mutex);
	wake_up(&rmidi->open_wait);
	list_del_init(&rmidi->list);
	for (dir = 0; dir < 2; dir++) {
		struct snd_rawmidi_substream *s;
		list_for_each_entry(s, &rmidi->streams[dir].substreams, list) {
			if (s->runtime)
				wake_up(&s->runtime->sleep);
		}
	}

#ifdef CONFIG_SND_OSSEMUL
	if (rmidi->ossreg) {
		if ((int)rmidi->device == midi_map[rmidi->card->number]) {
			snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_MIDI, rmidi->card, 0);
#ifdef SNDRV_OSS_INFO_DEV_MIDI
			snd_oss_info_unregister(SNDRV_OSS_INFO_DEV_MIDI, rmidi->card->number);
#endif
		}
		if ((int)rmidi->device == amidi_map[rmidi->card->number])
			snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_MIDI, rmidi->card, 1);
		rmidi->ossreg = 0;
	}
#endif /* CONFIG_SND_OSSEMUL */
	snd_unregister_device(&rmidi->dev);
	mutex_unlock(&rmidi->open_mutex);
	mutex_unlock(&register_mutex);
	return 0;
}

/**
 * snd_rawmidi_set_ops - set the rawmidi operators
 * @rmidi: the rawmidi instance
 * @stream: the stream direction, SNDRV_RAWMIDI_STREAM_XXX
 * @ops: the operator table
 *
 * Sets the rawmidi operators for the given stream direction.
 */
void snd_rawmidi_set_ops(struct snd_rawmidi *rmidi, int stream,
			 struct snd_rawmidi_ops *ops)
{
	struct snd_rawmidi_substream *substream;
	
	list_for_each_entry(substream, &rmidi->streams[stream].substreams, list)
		substream->ops = ops;
}
EXPORT_SYMBOL(snd_rawmidi_set_ops);

/*
 *  ENTRY functions
 */

static int __init alsa_rawmidi_init(void)
{

	snd_ctl_register_ioctl(snd_rawmidi_control_ioctl);
	snd_ctl_register_ioctl_compat(snd_rawmidi_control_ioctl);
#ifdef CONFIG_SND_OSSEMUL
	{ int i;
	/* check device map table */
	for (i = 0; i < SNDRV_CARDS; i++) {
		if (midi_map[i] < 0 || midi_map[i] >= SNDRV_RAWMIDI_DEVICES) {
			pr_err("ALSA: rawmidi: invalid midi_map[%d] = %d\n",
			       i, midi_map[i]);
			midi_map[i] = 0;
		}
		if (amidi_map[i] < 0 || amidi_map[i] >= SNDRV_RAWMIDI_DEVICES) {
			pr_err("ALSA: rawmidi: invalid amidi_map[%d] = %d\n",
			       i, amidi_map[i]);
			amidi_map[i] = 1;
		}
	}
	}
#endif /* CONFIG_SND_OSSEMUL */
	return 0;
}

static void __exit alsa_rawmidi_exit(void)
{
	snd_ctl_unregister_ioctl(snd_rawmidi_control_ioctl);
	snd_ctl_unregister_ioctl_compat(snd_rawmidi_control_ioctl);
}

module_init(alsa_rawmidi_init)
module_exit(alsa_rawmidi_exit)
