/*
 *  Abstract layer for MIDI v1.0 stream
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
#include <sound/core.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <sound/rawmidi.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/minors.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Midlevel RawMidi code for ALSA.");
MODULE_LICENSE("GPL");

#ifdef CONFIG_SND_OSSEMUL
static int midi_map[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = 0};
static int amidi_map[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = 1};
module_param_array(midi_map, int, NULL, 0444);
MODULE_PARM_DESC(midi_map, "Raw MIDI device number assigned to 1st OSS device.");
module_param_array(amidi_map, int, NULL, 0444);
MODULE_PARM_DESC(amidi_map, "Raw MIDI device number assigned to 2nd OSS device.");
#endif /* CONFIG_SND_OSSEMUL */

static int snd_rawmidi_free(snd_rawmidi_t *rawmidi);
static int snd_rawmidi_dev_free(snd_device_t *device);
static int snd_rawmidi_dev_register(snd_device_t *device);
static int snd_rawmidi_dev_disconnect(snd_device_t *device);
static int snd_rawmidi_dev_unregister(snd_device_t *device);

static snd_rawmidi_t *snd_rawmidi_devices[SNDRV_CARDS * SNDRV_RAWMIDI_DEVICES];

static DECLARE_MUTEX(register_mutex);

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

static inline int snd_rawmidi_ready(snd_rawmidi_substream_t * substream)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;
	return runtime->avail >= runtime->avail_min;
}

static inline int snd_rawmidi_ready_append(snd_rawmidi_substream_t * substream, size_t count)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;
	return runtime->avail >= runtime->avail_min &&
	       (!substream->append || runtime->avail >= count);
}

static void snd_rawmidi_input_event_tasklet(unsigned long data)
{
	snd_rawmidi_substream_t *substream = (snd_rawmidi_substream_t *)data;
	substream->runtime->event(substream);
}

static void snd_rawmidi_output_trigger_tasklet(unsigned long data)
{
	snd_rawmidi_substream_t *substream = (snd_rawmidi_substream_t *)data;
	substream->ops->trigger(substream, 1);
}

static int snd_rawmidi_runtime_create(snd_rawmidi_substream_t * substream)
{
	snd_rawmidi_runtime_t *runtime;

	if ((runtime = kzalloc(sizeof(*runtime), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	spin_lock_init(&runtime->lock);
	init_waitqueue_head(&runtime->sleep);
	if (substream->stream == SNDRV_RAWMIDI_STREAM_INPUT)
		tasklet_init(&runtime->tasklet,
			     snd_rawmidi_input_event_tasklet,
			     (unsigned long)substream);
	else
		tasklet_init(&runtime->tasklet,
			     snd_rawmidi_output_trigger_tasklet,
			     (unsigned long)substream);
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

static int snd_rawmidi_runtime_free(snd_rawmidi_substream_t * substream)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	kfree(runtime->buffer);
	kfree(runtime);
	substream->runtime = NULL;
	return 0;
}

static inline void snd_rawmidi_output_trigger(snd_rawmidi_substream_t * substream, int up)
{
	if (up) {
		tasklet_hi_schedule(&substream->runtime->tasklet);
	} else {
		tasklet_kill(&substream->runtime->tasklet);
		substream->ops->trigger(substream, 0);
	}
}

static void snd_rawmidi_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	substream->ops->trigger(substream, up);
	if (!up && substream->runtime->event)
		tasklet_kill(&substream->runtime->tasklet);
}

int snd_rawmidi_drop_output(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	snd_rawmidi_output_trigger(substream, 0);
	runtime->drain = 0;
	spin_lock_irqsave(&runtime->lock, flags);
	runtime->appl_ptr = runtime->hw_ptr = 0;
	runtime->avail = runtime->buffer_size;
	spin_unlock_irqrestore(&runtime->lock, flags);
	return 0;
}

int snd_rawmidi_drain_output(snd_rawmidi_substream_t * substream)
{
	int err;
	long timeout;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	err = 0;
	runtime->drain = 1;
	timeout = wait_event_interruptible_timeout(runtime->sleep,
				(runtime->avail >= runtime->buffer_size),
				10*HZ);
	if (signal_pending(current))
		err = -ERESTARTSYS;
	if (runtime->avail < runtime->buffer_size && !timeout) {
		snd_printk(KERN_WARNING "rawmidi drain error (avail = %li, buffer_size = %li)\n", (long)runtime->avail, (long)runtime->buffer_size);
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

int snd_rawmidi_drain_input(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	snd_rawmidi_input_trigger(substream, 0);
	runtime->drain = 0;
	spin_lock_irqsave(&runtime->lock, flags);
	runtime->appl_ptr = runtime->hw_ptr = 0;
	runtime->avail = 0;
	spin_unlock_irqrestore(&runtime->lock, flags);
	return 0;
}

int snd_rawmidi_kernel_open(int cardnum, int device, int subdevice,
			    int mode, snd_rawmidi_file_t * rfile)
{
	snd_rawmidi_t *rmidi;
	struct list_head *list1, *list2;
	snd_rawmidi_substream_t *sinput = NULL, *soutput = NULL;
	snd_rawmidi_runtime_t *input = NULL, *output = NULL;
	int err;

	if (rfile)
		rfile->input = rfile->output = NULL;
	rmidi = snd_rawmidi_devices[(cardnum * SNDRV_RAWMIDI_DEVICES) + device];
	if (rmidi == NULL) {
		err = -ENODEV;
		goto __error1;
	}
	if (!try_module_get(rmidi->card->module)) {
		err = -EFAULT;
		goto __error1;
	}
	if (!(mode & SNDRV_RAWMIDI_LFLG_NOOPENLOCK))
		down(&rmidi->open_mutex);
	if (mode & SNDRV_RAWMIDI_LFLG_INPUT) {
		if (!(rmidi->info_flags & SNDRV_RAWMIDI_INFO_INPUT)) {
			err = -ENXIO;
			goto __error;
		}
		if (subdevice >= 0 && (unsigned int)subdevice >= rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substream_count) {
			err = -ENODEV;
			goto __error;
		}
		if (rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substream_opened >=
		    rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substream_count) {
			err = -EAGAIN;
			goto __error;
		}
	}
	if (mode & SNDRV_RAWMIDI_LFLG_OUTPUT) {
		if (!(rmidi->info_flags & SNDRV_RAWMIDI_INFO_OUTPUT)) {
			err = -ENXIO;
			goto __error;
		}
		if (subdevice >= 0 && (unsigned int)subdevice >= rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substream_count) {
			err = -ENODEV;
			goto __error;
		}
		if (rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substream_opened >=
		    rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substream_count) {
			err = -EAGAIN;
			goto __error;
		}
	}
	list1 = rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substreams.next;
	while (1) {
		if (list1 == &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substreams) {
			sinput = NULL;
			if (mode & SNDRV_RAWMIDI_LFLG_INPUT) {
				err = -EAGAIN;
				goto __error;
			}
			break;
		}
		sinput = list_entry(list1, snd_rawmidi_substream_t, list);
		if ((mode & SNDRV_RAWMIDI_LFLG_INPUT) && sinput->opened)
			goto __nexti;
		if (subdevice < 0 || (subdevice >= 0 && subdevice == sinput->number))
			break;
	      __nexti:
		list1 = list1->next;
	}
	list2 = rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams.next;
	while (1) {
		if (list2 == &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams) {
			soutput = NULL;
			if (mode & SNDRV_RAWMIDI_LFLG_OUTPUT) {
				err = -EAGAIN;
				goto __error;
			}
			break;
		}
		soutput = list_entry(list2, snd_rawmidi_substream_t, list);
		if (mode & SNDRV_RAWMIDI_LFLG_OUTPUT) {
			if (mode & SNDRV_RAWMIDI_LFLG_APPEND) {
				if (soutput->opened && !soutput->append)
					goto __nexto;
			} else {
				if (soutput->opened)
					goto __nexto;
			}
		}
		if (subdevice < 0 || (subdevice >= 0 && subdevice == soutput->number))
			break;
	      __nexto:
		list2 = list2->next;
	}
	if (mode & SNDRV_RAWMIDI_LFLG_INPUT) {
		if ((err = snd_rawmidi_runtime_create(sinput)) < 0)
			goto __error;
		input = sinput->runtime;
		if ((err = sinput->ops->open(sinput)) < 0)
			goto __error;
		sinput->opened = 1;
		rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substream_opened++;
	} else {
		sinput = NULL;
	}
	if (mode & SNDRV_RAWMIDI_LFLG_OUTPUT) {
		if (soutput->opened)
			goto __skip_output;
		if ((err = snd_rawmidi_runtime_create(soutput)) < 0) {
			if (mode & SNDRV_RAWMIDI_LFLG_INPUT)
				sinput->ops->close(sinput);
			goto __error;
		}
		output = soutput->runtime;
		if ((err = soutput->ops->open(soutput)) < 0) {
			if (mode & SNDRV_RAWMIDI_LFLG_INPUT)
				sinput->ops->close(sinput);
			goto __error;
		}
	      __skip_output:
		soutput->opened = 1;
		if (mode & SNDRV_RAWMIDI_LFLG_APPEND)
			soutput->append = 1;
	      	if (soutput->use_count++ == 0)
			soutput->active_sensing = 1;
		rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substream_opened++;
	} else {
		soutput = NULL;
	}
	if (!(mode & SNDRV_RAWMIDI_LFLG_NOOPENLOCK))
		up(&rmidi->open_mutex);
	if (rfile) {
		rfile->rmidi = rmidi;
		rfile->input = sinput;
		rfile->output = soutput;
	}
	return 0;

      __error:
	if (input != NULL)
		snd_rawmidi_runtime_free(sinput);
	if (output != NULL)
		snd_rawmidi_runtime_free(soutput);
	module_put(rmidi->card->module);
	if (!(mode & SNDRV_RAWMIDI_LFLG_NOOPENLOCK))
		up(&rmidi->open_mutex);
      __error1:
	return err;
}

static int snd_rawmidi_open(struct inode *inode, struct file *file)
{
	int maj = imajor(inode);
	int cardnum;
	snd_card_t *card;
	int device, subdevice;
	unsigned short fflags;
	int err;
	snd_rawmidi_t *rmidi;
	snd_rawmidi_file_t *rawmidi_file;
	wait_queue_t wait;
	struct list_head *list;
	snd_ctl_file_t *kctl;

	if (maj == snd_major) {
		cardnum = SNDRV_MINOR_CARD(iminor(inode));
		cardnum %= SNDRV_CARDS;
		device = SNDRV_MINOR_DEVICE(iminor(inode)) - SNDRV_MINOR_RAWMIDI;
		device %= SNDRV_MINOR_RAWMIDIS;
#ifdef CONFIG_SND_OSSEMUL
	} else if (maj == SOUND_MAJOR) {
		cardnum = SNDRV_MINOR_OSS_CARD(iminor(inode));
		cardnum %= SNDRV_CARDS;
		device = SNDRV_MINOR_OSS_DEVICE(iminor(inode)) == SNDRV_MINOR_OSS_MIDI ?
			midi_map[cardnum] : amidi_map[cardnum];
#endif
	} else
		return -ENXIO;

	rmidi = snd_rawmidi_devices[(cardnum * SNDRV_RAWMIDI_DEVICES) + device];
	if (rmidi == NULL)
		return -ENODEV;
#ifdef CONFIG_SND_OSSEMUL
	if (maj == SOUND_MAJOR && !rmidi->ossreg)
		return -ENXIO;
#endif
	if ((file->f_flags & O_APPEND) && !(file->f_flags & O_NONBLOCK)) 
		return -EINVAL;		/* invalid combination */
	card = rmidi->card;
	err = snd_card_file_add(card, file);
	if (err < 0)
		return -ENODEV;
	fflags = snd_rawmidi_file_flags(file);
	if ((file->f_flags & O_APPEND) || maj == SOUND_MAJOR) /* OSS emul? */
		fflags |= SNDRV_RAWMIDI_LFLG_APPEND;
	fflags |= SNDRV_RAWMIDI_LFLG_NOOPENLOCK;
	rawmidi_file = kmalloc(sizeof(*rawmidi_file), GFP_KERNEL);
	if (rawmidi_file == NULL) {
		snd_card_file_remove(card, file);
		return -ENOMEM;
	}
	init_waitqueue_entry(&wait, current);
	add_wait_queue(&rmidi->open_wait, &wait);
	down(&rmidi->open_mutex);
	while (1) {
		subdevice = -1;
		down_read(&card->controls_rwsem);
		list_for_each(list, &card->ctl_files) {
			kctl = snd_ctl_file(list);
			if (kctl->pid == current->pid) {
				subdevice = kctl->prefer_rawmidi_subdevice;
				break;
			}
		}
		up_read(&card->controls_rwsem);
		err = snd_rawmidi_kernel_open(cardnum, device, subdevice, fflags, rawmidi_file);
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
		up(&rmidi->open_mutex);
		schedule();
		down(&rmidi->open_mutex);
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}
	}
#ifdef CONFIG_SND_OSSEMUL
	if (rawmidi_file->input && rawmidi_file->input->runtime)
		rawmidi_file->input->runtime->oss = (maj == SOUND_MAJOR);
	if (rawmidi_file->output && rawmidi_file->output->runtime)
		rawmidi_file->output->runtime->oss = (maj == SOUND_MAJOR);
#endif
	remove_wait_queue(&rmidi->open_wait, &wait);
	if (err >= 0) {
		file->private_data = rawmidi_file;
	} else {
		snd_card_file_remove(card, file);
		kfree(rawmidi_file);
	}
	up(&rmidi->open_mutex);
	return err;
}

int snd_rawmidi_kernel_release(snd_rawmidi_file_t * rfile)
{
	snd_rawmidi_t *rmidi;
	snd_rawmidi_substream_t *substream;
	snd_rawmidi_runtime_t *runtime;

	snd_assert(rfile != NULL, return -ENXIO);
	snd_assert(rfile->input != NULL || rfile->output != NULL, return -ENXIO);
	rmidi = rfile->rmidi;
	down(&rmidi->open_mutex);
	if (rfile->input != NULL) {
		substream = rfile->input;
		rfile->input = NULL;
		runtime = substream->runtime;
		snd_rawmidi_input_trigger(substream, 0);
		substream->ops->close(substream);
		if (runtime->private_free != NULL)
			runtime->private_free(substream);
		snd_rawmidi_runtime_free(substream);
		substream->opened = 0;
		rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substream_opened--;
	}
	if (rfile->output != NULL) {
		substream = rfile->output;
		rfile->output = NULL;
		if (--substream->use_count == 0) {
			runtime = substream->runtime;
			if (substream->active_sensing) {
				unsigned char buf = 0xfe;
				/* sending single active sensing message to shut the device up */
				snd_rawmidi_kernel_write(substream, &buf, 1);
			}
			if (snd_rawmidi_drain_output(substream) == -ERESTARTSYS)
				snd_rawmidi_output_trigger(substream, 0);
			substream->ops->close(substream);
			if (runtime->private_free != NULL)
				runtime->private_free(substream);
			snd_rawmidi_runtime_free(substream);
			substream->opened = 0;
			substream->append = 0;
		}
		rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substream_opened--;
	}
	up(&rmidi->open_mutex);
	module_put(rmidi->card->module);
	return 0;
}

static int snd_rawmidi_release(struct inode *inode, struct file *file)
{
	snd_rawmidi_file_t *rfile;
	snd_rawmidi_t *rmidi;
	int err;

	rfile = file->private_data;
	err = snd_rawmidi_kernel_release(rfile);
	rmidi = rfile->rmidi;
	wake_up(&rmidi->open_wait);
	kfree(rfile);
	snd_card_file_remove(rmidi->card, file);
	return err;
}

int snd_rawmidi_info(snd_rawmidi_substream_t *substream, snd_rawmidi_info_t *info)
{
	snd_rawmidi_t *rmidi;
	
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

static int snd_rawmidi_info_user(snd_rawmidi_substream_t *substream, snd_rawmidi_info_t __user * _info)
{
	snd_rawmidi_info_t info;
	int err;
	if ((err = snd_rawmidi_info(substream, &info)) < 0)
		return err;
	if (copy_to_user(_info, &info, sizeof(snd_rawmidi_info_t)))
		return -EFAULT;
	return 0;
}

int snd_rawmidi_info_select(snd_card_t *card, snd_rawmidi_info_t *info)
{
	snd_rawmidi_t *rmidi;
	snd_rawmidi_str_t *pstr;
	snd_rawmidi_substream_t *substream;
	struct list_head *list;
	if (info->device >= SNDRV_RAWMIDI_DEVICES)
		return -ENXIO;
	rmidi = snd_rawmidi_devices[card->number * SNDRV_RAWMIDI_DEVICES + info->device];
	if (info->stream < 0 || info->stream > 1)
		return -EINVAL;
	pstr = &rmidi->streams[info->stream];
	if (pstr->substream_count == 0)
		return -ENOENT;
	if (info->subdevice >= pstr->substream_count)
		return -ENXIO;
	list_for_each(list, &pstr->substreams) {
		substream = list_entry(list, snd_rawmidi_substream_t, list);
		if ((unsigned int)substream->number == info->subdevice)
			return snd_rawmidi_info(substream, info);
	}
	return -ENXIO;
}

static int snd_rawmidi_info_select_user(snd_card_t *card,
					snd_rawmidi_info_t __user *_info)
{
	int err;
	snd_rawmidi_info_t info;
	if (get_user(info.device, &_info->device))
		return -EFAULT;
	if (get_user(info.stream, &_info->stream))
		return -EFAULT;
	if (get_user(info.subdevice, &_info->subdevice))
		return -EFAULT;
	if ((err = snd_rawmidi_info_select(card, &info)) < 0)
		return err;
	if (copy_to_user(_info, &info, sizeof(snd_rawmidi_info_t)))
		return -EFAULT;
	return 0;
}

int snd_rawmidi_output_params(snd_rawmidi_substream_t * substream,
			      snd_rawmidi_params_t * params)
{
	char *newbuf;
	snd_rawmidi_runtime_t *runtime = substream->runtime;
	
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
		if ((newbuf = (char *) kmalloc(params->buffer_size, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		kfree(runtime->buffer);
		runtime->buffer = newbuf;
		runtime->buffer_size = params->buffer_size;
	}
	runtime->avail_min = params->avail_min;
	substream->active_sensing = !params->no_active_sensing;
	return 0;
}

int snd_rawmidi_input_params(snd_rawmidi_substream_t * substream,
			     snd_rawmidi_params_t * params)
{
	char *newbuf;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	snd_rawmidi_drain_input(substream);
	if (params->buffer_size < 32 || params->buffer_size > 1024L * 1024L) {
		return -EINVAL;
	}
	if (params->avail_min < 1 || params->avail_min > params->buffer_size) {
		return -EINVAL;
	}
	if (params->buffer_size != runtime->buffer_size) {
		if ((newbuf = (char *) kmalloc(params->buffer_size, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		kfree(runtime->buffer);
		runtime->buffer = newbuf;
		runtime->buffer_size = params->buffer_size;
	}
	runtime->avail_min = params->avail_min;
	return 0;
}

static int snd_rawmidi_output_status(snd_rawmidi_substream_t * substream,
				     snd_rawmidi_status_t * status)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	memset(status, 0, sizeof(*status));
	status->stream = SNDRV_RAWMIDI_STREAM_OUTPUT;
	spin_lock_irq(&runtime->lock);
	status->avail = runtime->avail;
	spin_unlock_irq(&runtime->lock);
	return 0;
}

static int snd_rawmidi_input_status(snd_rawmidi_substream_t * substream,
				    snd_rawmidi_status_t * status)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;

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
	snd_rawmidi_file_t *rfile;
	void __user *argp = (void __user *)arg;

	rfile = file->private_data;
	if (((cmd >> 8) & 0xff) != 'W')
		return -ENOTTY;
	switch (cmd) {
	case SNDRV_RAWMIDI_IOCTL_PVERSION:
		return put_user(SNDRV_RAWMIDI_VERSION, (int __user *)argp) ? -EFAULT : 0;
	case SNDRV_RAWMIDI_IOCTL_INFO:
	{
		snd_rawmidi_stream_t stream;
		snd_rawmidi_info_t __user *info = argp;
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
		snd_rawmidi_params_t params;
		if (copy_from_user(&params, argp, sizeof(snd_rawmidi_params_t)))
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
		snd_rawmidi_status_t status;
		if (copy_from_user(&status, argp, sizeof(snd_rawmidi_status_t)))
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
		if (copy_to_user(argp, &status, sizeof(snd_rawmidi_status_t)))
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
#ifdef CONFIG_SND_DEBUG
	default:
		snd_printk(KERN_WARNING "rawmidi: unknown command = 0x%x\n", cmd);
#endif
	}
	return -ENOTTY;
}

static int snd_rawmidi_control_ioctl(snd_card_t * card,
				     snd_ctl_file_t * control,
				     unsigned int cmd,
				     unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	unsigned int tmp;

	tmp = card->number * SNDRV_RAWMIDI_DEVICES;
	switch (cmd) {
	case SNDRV_CTL_IOCTL_RAWMIDI_NEXT_DEVICE:
	{
		int device;
		
		if (get_user(device, (int __user *)argp))
			return -EFAULT;
		device = device < 0 ? 0 : device + 1;
		while (device < SNDRV_RAWMIDI_DEVICES) {
			if (snd_rawmidi_devices[tmp + device])
				break;
			device++;
		}
		if (device == SNDRV_RAWMIDI_DEVICES)
			device = -1;
		if (put_user(device, (int __user *)argp))
			return -EFAULT;
		return 0;
	}
	case SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE:
	{
		int val;
		
		if (get_user(val, (int __user *)argp))
			return -EFAULT;
		control->prefer_rawmidi_subdevice = val;
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
 * Returns the size of read data, or a negative error code on failure.
 */
int snd_rawmidi_receive(snd_rawmidi_substream_t * substream, const unsigned char *buffer, int count)
{
	unsigned long flags;
	int result = 0, count1;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	if (runtime->buffer == NULL) {
		snd_printd("snd_rawmidi_receive: input is not active!!!\n");
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
			tasklet_hi_schedule(&runtime->tasklet);
		else if (snd_rawmidi_ready(substream))
			wake_up(&runtime->sleep);
	}
	spin_unlock_irqrestore(&runtime->lock, flags);
	return result;
}

static long snd_rawmidi_kernel_read1(snd_rawmidi_substream_t *substream,
				     unsigned char *buf, long count, int kernel)
{
	unsigned long flags;
	long result = 0, count1;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	while (count > 0 && runtime->avail) {
		count1 = runtime->buffer_size - runtime->appl_ptr;
		if (count1 > count)
			count1 = count;
		spin_lock_irqsave(&runtime->lock, flags);
		if (count1 > (int)runtime->avail)
			count1 = runtime->avail;
		if (kernel) {
			memcpy(buf + result, runtime->buffer + runtime->appl_ptr, count1);
		} else {
			spin_unlock_irqrestore(&runtime->lock, flags);
			if (copy_to_user((char __user *)buf + result,
					 runtime->buffer + runtime->appl_ptr, count1)) {
				return result > 0 ? result : -EFAULT;
			}
			spin_lock_irqsave(&runtime->lock, flags);
		}
		runtime->appl_ptr += count1;
		runtime->appl_ptr %= runtime->buffer_size;
		runtime->avail -= count1;
		spin_unlock_irqrestore(&runtime->lock, flags);
		result += count1;
		count -= count1;
	}
	return result;
}

long snd_rawmidi_kernel_read(snd_rawmidi_substream_t *substream, unsigned char *buf, long count)
{
	snd_rawmidi_input_trigger(substream, 1);
	return snd_rawmidi_kernel_read1(substream, buf, count, 1);
}

static ssize_t snd_rawmidi_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	long result;
	int count1;
	snd_rawmidi_file_t *rfile;
	snd_rawmidi_substream_t *substream;
	snd_rawmidi_runtime_t *runtime;

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
			if (signal_pending(current))
				return result > 0 ? result : -ERESTARTSYS;
			if (!runtime->avail)
				return result > 0 ? result : -EIO;
			spin_lock_irq(&runtime->lock);
		}
		spin_unlock_irq(&runtime->lock);
		count1 = snd_rawmidi_kernel_read1(substream,
						  (unsigned char __force *)buf,
						  count, 0);
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
 * Returns 1 if the internal output buffer is empty, 0 if not.
 */
int snd_rawmidi_transmit_empty(snd_rawmidi_substream_t * substream)
{
	snd_rawmidi_runtime_t *runtime = substream->runtime;
	int result;
	unsigned long flags;

	if (runtime->buffer == NULL) {
		snd_printd("snd_rawmidi_transmit_empty: output is not active!!!\n");
		return 1;
	}
	spin_lock_irqsave(&runtime->lock, flags);
	result = runtime->avail >= runtime->buffer_size;
	spin_unlock_irqrestore(&runtime->lock, flags);
	return result;		
}

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
 * Returns the size of copied data, or a negative error code on failure.
 */
int snd_rawmidi_transmit_peek(snd_rawmidi_substream_t * substream, unsigned char *buffer, int count)
{
	unsigned long flags;
	int result, count1;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	if (runtime->buffer == NULL) {
		snd_printd("snd_rawmidi_transmit_peek: output is not active!!!\n");
		return -EINVAL;
	}
	result = 0;
	spin_lock_irqsave(&runtime->lock, flags);
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
	spin_unlock_irqrestore(&runtime->lock, flags);
	return result;
}

/**
 * snd_rawmidi_transmit_ack - acknowledge the transmission
 * @substream: the rawmidi substream
 * @count: the tranferred count
 *
 * Advances the hardware pointer for the internal output buffer with
 * the given size and updates the condition.
 * Call after the transmission is finished.
 *
 * Returns the advanced size if successful, or a negative error code on failure.
 */
int snd_rawmidi_transmit_ack(snd_rawmidi_substream_t * substream, int count)
{
	unsigned long flags;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	if (runtime->buffer == NULL) {
		snd_printd("snd_rawmidi_transmit_ack: output is not active!!!\n");
		return -EINVAL;
	}
	spin_lock_irqsave(&runtime->lock, flags);
	snd_assert(runtime->avail + count <= runtime->buffer_size, );
	runtime->hw_ptr += count;
	runtime->hw_ptr %= runtime->buffer_size;
	runtime->avail += count;
	substream->bytes += count;
	if (count > 0) {
		if (runtime->drain || snd_rawmidi_ready(substream))
			wake_up(&runtime->sleep);
	}
	spin_unlock_irqrestore(&runtime->lock, flags);
	return count;
}

/**
 * snd_rawmidi_transmit - copy from the buffer to the device
 * @substream: the rawmidi substream
 * @buffer: the buffer pointer
 * @count: the data size to transfer
 * 
 * Copies data from the buffer to the device and advances the pointer.
 *
 * Returns the copied size if successful, or a negative error code on failure.
 */
int snd_rawmidi_transmit(snd_rawmidi_substream_t * substream, unsigned char *buffer, int count)
{
	count = snd_rawmidi_transmit_peek(substream, buffer, count);
	if (count < 0)
		return count;
	return snd_rawmidi_transmit_ack(substream, count);
}

static long snd_rawmidi_kernel_write1(snd_rawmidi_substream_t * substream, const unsigned char *buf, long count, int kernel)
{
	unsigned long flags;
	long count1, result;
	snd_rawmidi_runtime_t *runtime = substream->runtime;

	snd_assert(buf != NULL, return -EINVAL);
	snd_assert(runtime->buffer != NULL, return -EINVAL);

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
		if (kernel) {
			memcpy(runtime->buffer + runtime->appl_ptr, buf, count1);
		} else {
			spin_unlock_irqrestore(&runtime->lock, flags);
			if (copy_from_user(runtime->buffer + runtime->appl_ptr,
					   (char __user *)buf, count1)) {
				spin_lock_irqsave(&runtime->lock, flags);
				result = result > 0 ? result : -EFAULT;
				goto __end;
			}
			spin_lock_irqsave(&runtime->lock, flags);
		}
		runtime->appl_ptr += count1;
		runtime->appl_ptr %= runtime->buffer_size;
		runtime->avail -= count1;
		result += count1;
		buf += count1;
		count -= count1;
	}
      __end:
	count1 = runtime->avail < runtime->buffer_size;
	spin_unlock_irqrestore(&runtime->lock, flags);
	if (count1)
		snd_rawmidi_output_trigger(substream, 1);
	return result;
}

long snd_rawmidi_kernel_write(snd_rawmidi_substream_t * substream, const unsigned char *buf, long count)
{
	return snd_rawmidi_kernel_write1(substream, buf, count, 1);
}

static ssize_t snd_rawmidi_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	long result, timeout;
	int count1;
	snd_rawmidi_file_t *rfile;
	snd_rawmidi_runtime_t *runtime;
	snd_rawmidi_substream_t *substream;

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
			if (signal_pending(current))
				return result > 0 ? result : -ERESTARTSYS;
			if (!runtime->avail && !timeout)
				return result > 0 ? result : -EIO;
			spin_lock_irq(&runtime->lock);
		}
		spin_unlock_irq(&runtime->lock);
		count1 = snd_rawmidi_kernel_write1(substream,
						   (unsigned char __force *)buf,
						   count, 0);
		if (count1 < 0)
			return result > 0 ? result : count1;
		result += count1;
		buf += count1;
		if ((size_t)count1 < count && (file->f_flags & O_NONBLOCK))
			break;
		count -= count1;
	}
	if (file->f_flags & O_SYNC) {
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
	snd_rawmidi_file_t *rfile;
	snd_rawmidi_runtime_t *runtime;
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

static void snd_rawmidi_proc_info_read(snd_info_entry_t *entry,
				       snd_info_buffer_t * buffer)
{
	snd_rawmidi_t *rmidi;
	snd_rawmidi_substream_t *substream;
	snd_rawmidi_runtime_t *runtime;
	struct list_head *list;

	rmidi = entry->private_data;
	snd_iprintf(buffer, "%s\n\n", rmidi->name);
	down(&rmidi->open_mutex);
	if (rmidi->info_flags & SNDRV_RAWMIDI_INFO_OUTPUT) {
		list_for_each(list, &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams) {
			substream = list_entry(list, snd_rawmidi_substream_t, list);
			snd_iprintf(buffer,
				    "Output %d\n"
				    "  Tx bytes     : %lu\n",
				    substream->number,
				    (unsigned long) substream->bytes);
			if (substream->opened) {
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
		list_for_each(list, &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substreams) {
			substream = list_entry(list, snd_rawmidi_substream_t, list);
			snd_iprintf(buffer,
				    "Input %d\n"
				    "  Rx bytes     : %lu\n",
				    substream->number,
				    (unsigned long) substream->bytes);
			if (substream->opened) {
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
	up(&rmidi->open_mutex);
}

/*
 *  Register functions
 */

static struct file_operations snd_rawmidi_f_ops =
{
	.owner =	THIS_MODULE,
	.read =		snd_rawmidi_read,
	.write =	snd_rawmidi_write,
	.open =		snd_rawmidi_open,
	.release =	snd_rawmidi_release,
	.poll =		snd_rawmidi_poll,
	.unlocked_ioctl =	snd_rawmidi_ioctl,
	.compat_ioctl =	snd_rawmidi_ioctl_compat,
};

static snd_minor_t snd_rawmidi_reg =
{
	.comment =	"raw midi",
	.f_ops =	&snd_rawmidi_f_ops,
};

static int snd_rawmidi_alloc_substreams(snd_rawmidi_t *rmidi,
					snd_rawmidi_str_t *stream,
					int direction,
					int count)
{
	snd_rawmidi_substream_t *substream;
	int idx;

	INIT_LIST_HEAD(&stream->substreams);
	for (idx = 0; idx < count; idx++) {
		substream = kzalloc(sizeof(*substream), GFP_KERNEL);
		if (substream == NULL)
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
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_rawmidi_new(snd_card_t * card, char *id, int device,
		    int output_count, int input_count,
		    snd_rawmidi_t ** rrawmidi)
{
	snd_rawmidi_t *rmidi;
	int err;
	static snd_device_ops_t ops = {
		.dev_free = snd_rawmidi_dev_free,
		.dev_register = snd_rawmidi_dev_register,
		.dev_disconnect = snd_rawmidi_dev_disconnect,
		.dev_unregister = snd_rawmidi_dev_unregister
	};

	snd_assert(rrawmidi != NULL, return -EINVAL);
	*rrawmidi = NULL;
	snd_assert(card != NULL, return -ENXIO);
	rmidi = kzalloc(sizeof(*rmidi), GFP_KERNEL);
	if (rmidi == NULL)
		return -ENOMEM;
	rmidi->card = card;
	rmidi->device = device;
	init_MUTEX(&rmidi->open_mutex);
	init_waitqueue_head(&rmidi->open_wait);
	if (id != NULL)
		strlcpy(rmidi->id, id, sizeof(rmidi->id));
	if ((err = snd_rawmidi_alloc_substreams(rmidi, &rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT], SNDRV_RAWMIDI_STREAM_INPUT, input_count)) < 0) {
		snd_rawmidi_free(rmidi);
		return err;
	}
	if ((err = snd_rawmidi_alloc_substreams(rmidi, &rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT], SNDRV_RAWMIDI_STREAM_OUTPUT, output_count)) < 0) {
		snd_rawmidi_free(rmidi);
		return err;
	}
	if ((err = snd_device_new(card, SNDRV_DEV_RAWMIDI, rmidi, &ops)) < 0) {
		snd_rawmidi_free(rmidi);
		return err;
	}
	*rrawmidi = rmidi;
	return 0;
}

static void snd_rawmidi_free_substreams(snd_rawmidi_str_t *stream)
{
	snd_rawmidi_substream_t *substream;

	while (!list_empty(&stream->substreams)) {
		substream = list_entry(stream->substreams.next, snd_rawmidi_substream_t, list);
		list_del(&substream->list);
		kfree(substream);
	}
}

static int snd_rawmidi_free(snd_rawmidi_t *rmidi)
{
	snd_assert(rmidi != NULL, return -ENXIO);	
	snd_rawmidi_free_substreams(&rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT]);
	snd_rawmidi_free_substreams(&rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT]);
	if (rmidi->private_free)
		rmidi->private_free(rmidi);
	kfree(rmidi);
	return 0;
}

static int snd_rawmidi_dev_free(snd_device_t *device)
{
	snd_rawmidi_t *rmidi = device->device_data;
	return snd_rawmidi_free(rmidi);
}

#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
static void snd_rawmidi_dev_seq_free(snd_seq_device_t *device)
{
	snd_rawmidi_t *rmidi = device->private_data;
	rmidi->seq_dev = NULL;
}
#endif

static int snd_rawmidi_dev_register(snd_device_t *device)
{
	int idx, err;
	snd_info_entry_t *entry;
	char name[16];
	snd_rawmidi_t *rmidi = device->device_data;

	if (rmidi->device >= SNDRV_RAWMIDI_DEVICES)
		return -ENOMEM;
	down(&register_mutex);
	idx = (rmidi->card->number * SNDRV_RAWMIDI_DEVICES) + rmidi->device;
	if (snd_rawmidi_devices[idx] != NULL) {
		up(&register_mutex);
		return -EBUSY;
	}
	snd_rawmidi_devices[idx] = rmidi;
	sprintf(name, "midiC%iD%i", rmidi->card->number, rmidi->device);
	if ((err = snd_register_device(SNDRV_DEVICE_TYPE_RAWMIDI,
				       rmidi->card, rmidi->device,
				       &snd_rawmidi_reg, name)) < 0) {
		snd_printk(KERN_ERR "unable to register rawmidi device %i:%i\n", rmidi->card->number, rmidi->device);
		snd_rawmidi_devices[idx] = NULL;
		up(&register_mutex);
		return err;
	}
	if (rmidi->ops && rmidi->ops->dev_register &&
	    (err = rmidi->ops->dev_register(rmidi)) < 0) {
		snd_unregister_device(SNDRV_DEVICE_TYPE_RAWMIDI, rmidi->card, rmidi->device);
		snd_rawmidi_devices[idx] = NULL;
		up(&register_mutex);
		return err;
	}
#ifdef CONFIG_SND_OSSEMUL
	rmidi->ossreg = 0;
	if ((int)rmidi->device == midi_map[rmidi->card->number]) {
		if (snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_MIDI,
					    rmidi->card, 0, &snd_rawmidi_reg, name) < 0) {
			snd_printk(KERN_ERR "unable to register OSS rawmidi device %i:%i\n", rmidi->card->number, 0);
		} else {
			rmidi->ossreg++;
#ifdef SNDRV_OSS_INFO_DEV_MIDI
			snd_oss_info_register(SNDRV_OSS_INFO_DEV_MIDI, rmidi->card->number, rmidi->name);
#endif
		}
	}
	if ((int)rmidi->device == amidi_map[rmidi->card->number]) {
		if (snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_MIDI,
					    rmidi->card, 1, &snd_rawmidi_reg, name) < 0) {
			snd_printk(KERN_ERR "unable to register OSS rawmidi device %i:%i\n", rmidi->card->number, 1);
		} else {
			rmidi->ossreg++;
		}
	}
#endif /* CONFIG_SND_OSSEMUL */
	up(&register_mutex);
	sprintf(name, "midi%d", rmidi->device);
	entry = snd_info_create_card_entry(rmidi->card, name, rmidi->card->proc_root);
	if (entry) {
		entry->private_data = rmidi;
		entry->c.text.read_size = 1024;
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

static int snd_rawmidi_dev_disconnect(snd_device_t *device)
{
	snd_rawmidi_t *rmidi = device->device_data;
	int idx;

	down(&register_mutex);
	idx = (rmidi->card->number * SNDRV_RAWMIDI_DEVICES) + rmidi->device;
	snd_rawmidi_devices[idx] = NULL;
	up(&register_mutex);
	return 0;
}

static int snd_rawmidi_dev_unregister(snd_device_t *device)
{
	int idx;
	snd_rawmidi_t *rmidi = device->device_data;

	snd_assert(rmidi != NULL, return -ENXIO);
	down(&register_mutex);
	idx = (rmidi->card->number * SNDRV_RAWMIDI_DEVICES) + rmidi->device;
	snd_rawmidi_devices[idx] = NULL;
	if (rmidi->proc_entry) {
		snd_info_unregister(rmidi->proc_entry);
		rmidi->proc_entry = NULL;
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
	if (rmidi->ops && rmidi->ops->dev_unregister)
		rmidi->ops->dev_unregister(rmidi);
	snd_unregister_device(SNDRV_DEVICE_TYPE_RAWMIDI, rmidi->card, rmidi->device);
	up(&register_mutex);
#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
	if (rmidi->seq_dev) {
		snd_device_free(rmidi->card, rmidi->seq_dev);
		rmidi->seq_dev = NULL;
	}
#endif
	return snd_rawmidi_free(rmidi);
}

/**
 * snd_rawmidi_set_ops - set the rawmidi operators
 * @rmidi: the rawmidi instance
 * @stream: the stream direction, SNDRV_RAWMIDI_STREAM_XXX
 * @ops: the operator table
 *
 * Sets the rawmidi operators for the given stream direction.
 */
void snd_rawmidi_set_ops(snd_rawmidi_t *rmidi, int stream, snd_rawmidi_ops_t *ops)
{
	struct list_head *list;
	snd_rawmidi_substream_t *substream;
	
	list_for_each(list, &rmidi->streams[stream].substreams) {
		substream = list_entry(list, snd_rawmidi_substream_t, list);
		substream->ops = ops;
	}
}

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
			snd_printk(KERN_ERR "invalid midi_map[%d] = %d\n", i, midi_map[i]);
			midi_map[i] = 0;
		}
		if (amidi_map[i] < 0 || amidi_map[i] >= SNDRV_RAWMIDI_DEVICES) {
			snd_printk(KERN_ERR "invalid amidi_map[%d] = %d\n", i, amidi_map[i]);
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

EXPORT_SYMBOL(snd_rawmidi_output_params);
EXPORT_SYMBOL(snd_rawmidi_input_params);
EXPORT_SYMBOL(snd_rawmidi_drop_output);
EXPORT_SYMBOL(snd_rawmidi_drain_output);
EXPORT_SYMBOL(snd_rawmidi_drain_input);
EXPORT_SYMBOL(snd_rawmidi_receive);
EXPORT_SYMBOL(snd_rawmidi_transmit_empty);
EXPORT_SYMBOL(snd_rawmidi_transmit_peek);
EXPORT_SYMBOL(snd_rawmidi_transmit_ack);
EXPORT_SYMBOL(snd_rawmidi_transmit);
EXPORT_SYMBOL(snd_rawmidi_new);
EXPORT_SYMBOL(snd_rawmidi_set_ops);
EXPORT_SYMBOL(snd_rawmidi_info);
EXPORT_SYMBOL(snd_rawmidi_info_select);
EXPORT_SYMBOL(snd_rawmidi_kernel_open);
EXPORT_SYMBOL(snd_rawmidi_kernel_release);
EXPORT_SYMBOL(snd_rawmidi_kernel_read);
EXPORT_SYMBOL(snd_rawmidi_kernel_write);
