// SPDX-License-Identifier: GPL-2.0-only
/*
 *  compress_core.c - compress offload core
 *
 *  Copyright (C) 2011 Intel Corporation
 *  Authors:	Vinod Koul <vinod.koul@linux.intel.com>
 *		Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#define FORMAT(fmt) "%s: %d: " fmt, __func__, __LINE__
#define pr_fmt(fmt) KBUILD_MODNAME ": " FORMAT(fmt)

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/uio.h>
#include <linux/uaccess.h>
#include <linux/dma-buf.h>
#include <linux/module.h>
#include <linux/compat.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/info.h>
#include <sound/compress_params.h>
#include <sound/compress_offload.h>
#include <sound/compress_driver.h>

/* struct snd_compr_codec_caps overflows the ioctl bit size for some
 * architectures, so we need to disable the relevant ioctls.
 */
#if _IOC_SIZEBITS < 14
#define COMPR_CODEC_CAPS_OVERFLOW
#endif

/* TODO:
 * - add substream support for multiple devices in case of
 *	SND_DYNAMIC_MINORS is not used
 * - Multiple node representation
 *	driver should be able to register multiple nodes
 */

struct snd_compr_file {
	unsigned long caps;
	struct snd_compr_stream stream;
};

static void error_delayed_work(struct work_struct *work);

#if IS_ENABLED(CONFIG_SND_COMPRESS_ACCEL)
static void snd_compr_task_free_all(struct snd_compr_stream *stream);
#else
static inline void snd_compr_task_free_all(struct snd_compr_stream *stream) { }
#endif

/*
 * a note on stream states used:
 * we use following states in the compressed core
 * SNDRV_PCM_STATE_OPEN: When stream has been opened.
 * SNDRV_PCM_STATE_SETUP: When stream has been initialized. This is done by
 *	calling SNDRV_COMPRESS_SET_PARAMS. Running streams will come to this
 *	state at stop by calling SNDRV_COMPRESS_STOP, or at end of drain.
 * SNDRV_PCM_STATE_PREPARED: When a stream has been written to (for
 *	playback only). User after setting up stream writes the data buffer
 *	before starting the stream.
 * SNDRV_PCM_STATE_RUNNING: When stream has been started and is
 *	decoding/encoding and rendering/capturing data.
 * SNDRV_PCM_STATE_DRAINING: When stream is draining current data. This is done
 *	by calling SNDRV_COMPRESS_DRAIN.
 * SNDRV_PCM_STATE_PAUSED: When stream is paused. This is done by calling
 *	SNDRV_COMPRESS_PAUSE. It can be stopped or resumed by calling
 *	SNDRV_COMPRESS_STOP or SNDRV_COMPRESS_RESUME respectively.
 */
static int snd_compr_open(struct inode *inode, struct file *f)
{
	struct snd_compr *compr;
	struct snd_compr_file *data;
	struct snd_compr_runtime *runtime;
	enum snd_compr_direction dirn;
	int maj = imajor(inode);
	int ret;

	if ((f->f_flags & O_ACCMODE) == O_WRONLY)
		dirn = SND_COMPRESS_PLAYBACK;
	else if ((f->f_flags & O_ACCMODE) == O_RDONLY)
		dirn = SND_COMPRESS_CAPTURE;
	else if ((f->f_flags & O_ACCMODE) == O_RDWR)
		dirn = SND_COMPRESS_ACCEL;
	else
		return -EINVAL;

	if (maj == snd_major)
		compr = snd_lookup_minor_data(iminor(inode),
					SNDRV_DEVICE_TYPE_COMPRESS);
	else
		return -EBADFD;

	if (compr == NULL) {
		pr_err("no device data!!!\n");
		return -ENODEV;
	}

	if (dirn != compr->direction) {
		pr_err("this device doesn't support this direction\n");
		snd_card_unref(compr->card);
		return -EINVAL;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		snd_card_unref(compr->card);
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&data->stream.error_work, error_delayed_work);

	data->stream.ops = compr->ops;
	data->stream.direction = dirn;
	data->stream.private_data = compr->private_data;
	data->stream.device = compr;
	runtime = kzalloc(sizeof(*runtime), GFP_KERNEL);
	if (!runtime) {
		kfree(data);
		snd_card_unref(compr->card);
		return -ENOMEM;
	}
	runtime->state = SNDRV_PCM_STATE_OPEN;
	init_waitqueue_head(&runtime->sleep);
#if IS_ENABLED(CONFIG_SND_COMPRESS_ACCEL)
	INIT_LIST_HEAD(&runtime->tasks);
#endif
	data->stream.runtime = runtime;
	f->private_data = (void *)data;
	scoped_guard(mutex, &compr->lock)
		ret = compr->ops->open(&data->stream);
	if (ret) {
		kfree(runtime);
		kfree(data);
	}
	snd_card_unref(compr->card);
	return ret;
}

static int snd_compr_free(struct inode *inode, struct file *f)
{
	struct snd_compr_file *data = f->private_data;
	struct snd_compr_runtime *runtime = data->stream.runtime;

	cancel_delayed_work_sync(&data->stream.error_work);

	switch (runtime->state) {
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_DRAINING:
	case SNDRV_PCM_STATE_PAUSED:
		data->stream.ops->trigger(&data->stream, SNDRV_PCM_TRIGGER_STOP);
		break;
	default:
		break;
	}

	snd_compr_task_free_all(&data->stream);

	data->stream.ops->free(&data->stream);
	if (!data->stream.runtime->dma_buffer_p)
		kfree(data->stream.runtime->buffer);
	kfree(data->stream.runtime);
	kfree(data);
	return 0;
}

static int snd_compr_update_tstamp(struct snd_compr_stream *stream,
		struct snd_compr_tstamp *tstamp)
{
	if (!stream->ops->pointer)
		return -ENOTSUPP;
	stream->ops->pointer(stream, tstamp);
	pr_debug("dsp consumed till %d total %d bytes\n",
		tstamp->byte_offset, tstamp->copied_total);
	if (stream->direction == SND_COMPRESS_PLAYBACK)
		stream->runtime->total_bytes_transferred = tstamp->copied_total;
	else
		stream->runtime->total_bytes_available = tstamp->copied_total;
	return 0;
}

static size_t snd_compr_calc_avail(struct snd_compr_stream *stream,
		struct snd_compr_avail *avail)
{
	memset(avail, 0, sizeof(*avail));
	snd_compr_update_tstamp(stream, &avail->tstamp);
	/* Still need to return avail even if tstamp can't be filled in */

	if (stream->runtime->total_bytes_available == 0 &&
			stream->runtime->state == SNDRV_PCM_STATE_SETUP &&
			stream->direction == SND_COMPRESS_PLAYBACK) {
		pr_debug("detected init and someone forgot to do a write\n");
		return stream->runtime->buffer_size;
	}
	pr_debug("app wrote %lld, DSP consumed %lld\n",
			stream->runtime->total_bytes_available,
			stream->runtime->total_bytes_transferred);
	if (stream->runtime->total_bytes_available ==
				stream->runtime->total_bytes_transferred) {
		if (stream->direction == SND_COMPRESS_PLAYBACK) {
			pr_debug("both pointers are same, returning full avail\n");
			return stream->runtime->buffer_size;
		} else {
			pr_debug("both pointers are same, returning no avail\n");
			return 0;
		}
	}

	avail->avail = stream->runtime->total_bytes_available -
			stream->runtime->total_bytes_transferred;
	if (stream->direction == SND_COMPRESS_PLAYBACK)
		avail->avail = stream->runtime->buffer_size - avail->avail;

	pr_debug("ret avail as %lld\n", avail->avail);
	return avail->avail;
}

static inline size_t snd_compr_get_avail(struct snd_compr_stream *stream)
{
	struct snd_compr_avail avail;

	return snd_compr_calc_avail(stream, &avail);
}

static int
snd_compr_ioctl_avail(struct snd_compr_stream *stream, unsigned long arg)
{
	struct snd_compr_avail ioctl_avail;
	size_t avail;

	if (stream->direction == SND_COMPRESS_ACCEL)
		return -EBADFD;

	avail = snd_compr_calc_avail(stream, &ioctl_avail);
	ioctl_avail.avail = avail;

	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_OPEN:
		return -EBADFD;
	case SNDRV_PCM_STATE_XRUN:
		return -EPIPE;
	default:
		break;
	}

	if (copy_to_user((__u64 __user *)arg,
				&ioctl_avail, sizeof(ioctl_avail)))
		return -EFAULT;
	return 0;
}

static int snd_compr_write_data(struct snd_compr_stream *stream,
	       const char __user *buf, size_t count)
{
	void *dstn;
	size_t copy;
	struct snd_compr_runtime *runtime = stream->runtime;
	/* 64-bit Modulus */
	u64 app_pointer = div64_u64(runtime->total_bytes_available,
				    runtime->buffer_size);
	app_pointer = runtime->total_bytes_available -
		      (app_pointer * runtime->buffer_size);

	dstn = runtime->buffer + app_pointer;
	pr_debug("copying %ld at %lld\n",
			(unsigned long)count, app_pointer);
	if (count < runtime->buffer_size - app_pointer) {
		if (copy_from_user(dstn, buf, count))
			return -EFAULT;
	} else {
		copy = runtime->buffer_size - app_pointer;
		if (copy_from_user(dstn, buf, copy))
			return -EFAULT;
		if (copy_from_user(runtime->buffer, buf + copy, count - copy))
			return -EFAULT;
	}
	/* if DSP cares, let it know data has been written */
	if (stream->ops->ack)
		stream->ops->ack(stream, count);
	return count;
}

static ssize_t snd_compr_write(struct file *f, const char __user *buf,
		size_t count, loff_t *offset)
{
	struct snd_compr_file *data = f->private_data;
	struct snd_compr_stream *stream;
	size_t avail;
	int retval;

	if (snd_BUG_ON(!data))
		return -EFAULT;

	stream = &data->stream;
	if (stream->direction == SND_COMPRESS_ACCEL)
		return -EBADFD;
	guard(mutex)(&stream->device->lock);
	/* write is allowed when stream is running or has been setup */
	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_SETUP:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_RUNNING:
		break;
	default:
		return -EBADFD;
	}

	avail = snd_compr_get_avail(stream);
	pr_debug("avail returned %ld\n", (unsigned long)avail);
	/* calculate how much we can write to buffer */
	if (avail > count)
		avail = count;

	if (stream->ops->copy) {
		char __user* cbuf = (char __user*)buf;
		retval = stream->ops->copy(stream, cbuf, avail);
	} else {
		retval = snd_compr_write_data(stream, buf, avail);
	}
	if (retval > 0)
		stream->runtime->total_bytes_available += retval;

	/* while initiating the stream, write should be called before START
	 * call, so in setup move state */
	if (stream->runtime->state == SNDRV_PCM_STATE_SETUP) {
		stream->runtime->state = SNDRV_PCM_STATE_PREPARED;
		pr_debug("stream prepared, Houston we are good to go\n");
	}

	return retval;
}


static ssize_t snd_compr_read(struct file *f, char __user *buf,
		size_t count, loff_t *offset)
{
	struct snd_compr_file *data = f->private_data;
	struct snd_compr_stream *stream;
	size_t avail;
	int retval;

	if (snd_BUG_ON(!data))
		return -EFAULT;

	stream = &data->stream;
	if (stream->direction == SND_COMPRESS_ACCEL)
		return -EBADFD;
	guard(mutex)(&stream->device->lock);

	/* read is allowed when stream is running, paused, draining and setup
	 * (yes setup is state which we transition to after stop, so if user
	 * wants to read data after stop we allow that)
	 */
	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_SUSPENDED:
	case SNDRV_PCM_STATE_DISCONNECTED:
		return -EBADFD;
	case SNDRV_PCM_STATE_XRUN:
		return -EPIPE;
	}

	avail = snd_compr_get_avail(stream);
	pr_debug("avail returned %ld\n", (unsigned long)avail);
	/* calculate how much we can read from buffer */
	if (avail > count)
		avail = count;

	if (stream->ops->copy)
		retval = stream->ops->copy(stream, buf, avail);
	else
		return -ENXIO;
	if (retval > 0)
		stream->runtime->total_bytes_transferred += retval;

	return retval;
}

static int snd_compr_mmap(struct file *f, struct vm_area_struct *vma)
{
	return -ENXIO;
}

static __poll_t snd_compr_get_poll(struct snd_compr_stream *stream)
{
	if (stream->direction == SND_COMPRESS_PLAYBACK)
		return EPOLLOUT | EPOLLWRNORM;
	else
		return EPOLLIN | EPOLLRDNORM;
}

static __poll_t snd_compr_poll(struct file *f, poll_table *wait)
{
	struct snd_compr_file *data = f->private_data;
	struct snd_compr_stream *stream;
	struct snd_compr_runtime *runtime;
	size_t avail;
	__poll_t retval = 0;

	if (snd_BUG_ON(!data))
		return EPOLLERR;

	stream = &data->stream;
	runtime = stream->runtime;

	guard(mutex)(&stream->device->lock);

	switch (runtime->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_XRUN:
		return snd_compr_get_poll(stream) | EPOLLERR;
	default:
		break;
	}

	poll_wait(f, &runtime->sleep, wait);

#if IS_ENABLED(CONFIG_SND_COMPRESS_ACCEL)
	if (stream->direction == SND_COMPRESS_ACCEL) {
		struct snd_compr_task_runtime *task;
		if (runtime->fragments > runtime->active_tasks)
			retval |= EPOLLOUT | EPOLLWRNORM;
		task = list_first_entry_or_null(&runtime->tasks,
						struct snd_compr_task_runtime,
						list);
		if (task && task->state == SND_COMPRESS_TASK_STATE_FINISHED)
			retval |= EPOLLIN | EPOLLRDNORM;
		return retval;
	}
#endif

	avail = snd_compr_get_avail(stream);
	pr_debug("avail is %ld\n", (unsigned long)avail);
	/* check if we have at least one fragment to fill */
	switch (runtime->state) {
	case SNDRV_PCM_STATE_DRAINING:
		/* stream has been woken up after drain is complete
		 * draining done so set stream state to stopped
		 */
		retval = snd_compr_get_poll(stream);
		runtime->state = SNDRV_PCM_STATE_SETUP;
		break;
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
		if (avail >= runtime->fragment_size)
			retval = snd_compr_get_poll(stream);
		break;
	default:
		return snd_compr_get_poll(stream) | EPOLLERR;
	}

	return retval;
}

static int
snd_compr_get_caps(struct snd_compr_stream *stream, unsigned long arg)
{
	int retval;
	struct snd_compr_caps caps;

	if (!stream->ops->get_caps)
		return -ENXIO;

	memset(&caps, 0, sizeof(caps));
	retval = stream->ops->get_caps(stream, &caps);
	if (retval)
		goto out;
	if (copy_to_user((void __user *)arg, &caps, sizeof(caps)))
		retval = -EFAULT;
out:
	return retval;
}

#ifndef COMPR_CODEC_CAPS_OVERFLOW
static int
snd_compr_get_codec_caps(struct snd_compr_stream *stream, unsigned long arg)
{
	int retval;
	struct snd_compr_codec_caps *caps __free(kfree) = NULL;

	if (!stream->ops->get_codec_caps)
		return -ENXIO;

	caps = kzalloc(sizeof(*caps), GFP_KERNEL);
	if (!caps)
		return -ENOMEM;

	retval = stream->ops->get_codec_caps(stream, caps);
	if (retval)
		return retval;
	if (copy_to_user((void __user *)arg, caps, sizeof(*caps)))
		return -EFAULT;
	return retval;
}
#endif /* !COMPR_CODEC_CAPS_OVERFLOW */

int snd_compr_malloc_pages(struct snd_compr_stream *stream, size_t size)
{
	struct snd_dma_buffer *dmab;
	int ret;

	if (snd_BUG_ON(!(stream) || !(stream)->runtime))
		return -EINVAL;
	dmab = kzalloc(sizeof(*dmab), GFP_KERNEL);
	if (!dmab)
		return -ENOMEM;
	dmab->dev = stream->dma_buffer.dev;
	ret = snd_dma_alloc_pages(dmab->dev.type, dmab->dev.dev, size, dmab);
	if (ret < 0) {
		kfree(dmab);
		return ret;
	}

	snd_compr_set_runtime_buffer(stream, dmab);
	stream->runtime->dma_bytes = size;
	return 1;
}
EXPORT_SYMBOL(snd_compr_malloc_pages);

int snd_compr_free_pages(struct snd_compr_stream *stream)
{
	struct snd_compr_runtime *runtime;

	if (snd_BUG_ON(!(stream) || !(stream)->runtime))
		return -EINVAL;
	runtime = stream->runtime;
	if (runtime->dma_area == NULL)
		return 0;
	if (runtime->dma_buffer_p != &stream->dma_buffer) {
		/* It's a newly allocated buffer. Release it now. */
		snd_dma_free_pages(runtime->dma_buffer_p);
		kfree(runtime->dma_buffer_p);
	}

	snd_compr_set_runtime_buffer(stream, NULL);
	return 0;
}
EXPORT_SYMBOL(snd_compr_free_pages);

/* revisit this with snd_pcm_preallocate_xxx */
static int snd_compr_allocate_buffer(struct snd_compr_stream *stream,
		struct snd_compr_params *params)
{
	unsigned int buffer_size;
	void *buffer = NULL;

	if (stream->direction == SND_COMPRESS_ACCEL)
		goto params;

	buffer_size = params->buffer.fragment_size * params->buffer.fragments;
	if (stream->ops->copy) {
		buffer = NULL;
		/* if copy is defined the driver will be required to copy
		 * the data from core
		 */
	} else {
		if (stream->runtime->dma_buffer_p) {

			if (buffer_size > stream->runtime->dma_buffer_p->bytes)
				dev_err(stream->device->dev,
						"Not enough DMA buffer");
			else
				buffer = stream->runtime->dma_buffer_p->area;

		} else {
			buffer = kmalloc(buffer_size, GFP_KERNEL);
		}

		if (!buffer)
			return -ENOMEM;
	}

	stream->runtime->buffer = buffer;
	stream->runtime->buffer_size = buffer_size;
params:
	stream->runtime->fragment_size = params->buffer.fragment_size;
	stream->runtime->fragments = params->buffer.fragments;
	return 0;
}

static int
snd_compress_check_input(struct snd_compr_stream *stream, struct snd_compr_params *params)
{
	u32 max_fragments;

	/* first let's check the buffer parameter's */
	if (params->buffer.fragment_size == 0)
		return -EINVAL;

	if (stream->direction == SND_COMPRESS_ACCEL)
		max_fragments = 64;			/* safe value */
	else
		max_fragments = U32_MAX / params->buffer.fragment_size;

	if (params->buffer.fragments > max_fragments ||
	    params->buffer.fragments == 0)
		return -EINVAL;

	/* now codec parameters */
	if (params->codec.id == 0 || params->codec.id > SND_AUDIOCODEC_MAX)
		return -EINVAL;

	if (params->codec.ch_in == 0 || params->codec.ch_out == 0)
		return -EINVAL;

	return 0;
}

static int
snd_compr_set_params(struct snd_compr_stream *stream, unsigned long arg)
{
	struct snd_compr_params *params __free(kfree) = NULL;
	int retval;

	if (stream->runtime->state == SNDRV_PCM_STATE_OPEN || stream->next_track) {
		/*
		 * we should allow parameter change only when stream has been
		 * opened not in other cases
		 */
		params = memdup_user((void __user *)arg, sizeof(*params));
		if (IS_ERR(params))
			return PTR_ERR(params);

		retval = snd_compress_check_input(stream, params);
		if (retval)
			return retval;

		retval = snd_compr_allocate_buffer(stream, params);
		if (retval)
			return -ENOMEM;

		retval = stream->ops->set_params(stream, params);
		if (retval)
			return retval;

		if (stream->next_track)
			return retval;

		stream->metadata_set = false;
		stream->next_track = false;

		stream->runtime->state = SNDRV_PCM_STATE_SETUP;
	} else {
		return -EPERM;
	}
	return retval;
}

static int
snd_compr_get_params(struct snd_compr_stream *stream, unsigned long arg)
{
	struct snd_codec *params __free(kfree) = NULL;
	int retval;

	if (!stream->ops->get_params)
		return -EBADFD;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;
	retval = stream->ops->get_params(stream, params);
	if (retval)
		return retval;
	if (copy_to_user((char __user *)arg, params, sizeof(*params)))
		return -EFAULT;
	return retval;
}

static int
snd_compr_get_metadata(struct snd_compr_stream *stream, unsigned long arg)
{
	struct snd_compr_metadata metadata;
	int retval;

	if (!stream->ops->get_metadata)
		return -ENXIO;

	if (copy_from_user(&metadata, (void __user *)arg, sizeof(metadata)))
		return -EFAULT;

	retval = stream->ops->get_metadata(stream, &metadata);
	if (retval != 0)
		return retval;

	if (copy_to_user((void __user *)arg, &metadata, sizeof(metadata)))
		return -EFAULT;

	return 0;
}

static int
snd_compr_set_metadata(struct snd_compr_stream *stream, unsigned long arg)
{
	struct snd_compr_metadata metadata;
	int retval;

	if (!stream->ops->set_metadata)
		return -ENXIO;
	/*
	* we should allow parameter change only when stream has been
	* opened not in other cases
	*/
	if (copy_from_user(&metadata, (void __user *)arg, sizeof(metadata)))
		return -EFAULT;

	retval = stream->ops->set_metadata(stream, &metadata);
	stream->metadata_set = true;

	return retval;
}

static inline int
snd_compr_tstamp(struct snd_compr_stream *stream, unsigned long arg)
{
	struct snd_compr_tstamp tstamp = {0};
	int ret;

	ret = snd_compr_update_tstamp(stream, &tstamp);
	if (ret == 0)
		ret = copy_to_user((struct snd_compr_tstamp __user *)arg,
			&tstamp, sizeof(tstamp)) ? -EFAULT : 0;
	return ret;
}

static int snd_compr_pause(struct snd_compr_stream *stream)
{
	int retval;

	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_RUNNING:
		retval = stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_PAUSE_PUSH);
		if (!retval)
			stream->runtime->state = SNDRV_PCM_STATE_PAUSED;
		break;
	case SNDRV_PCM_STATE_DRAINING:
		if (!stream->device->use_pause_in_draining)
			return -EPERM;
		retval = stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_PAUSE_PUSH);
		if (!retval)
			stream->pause_in_draining = true;
		break;
	default:
		return -EPERM;
	}
	return retval;
}

static int snd_compr_resume(struct snd_compr_stream *stream)
{
	int retval;

	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_PAUSED:
		retval = stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_PAUSE_RELEASE);
		if (!retval)
			stream->runtime->state = SNDRV_PCM_STATE_RUNNING;
		break;
	case SNDRV_PCM_STATE_DRAINING:
		if (!stream->pause_in_draining)
			return -EPERM;
		retval = stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_PAUSE_RELEASE);
		if (!retval)
			stream->pause_in_draining = false;
		break;
	default:
		return -EPERM;
	}
	return retval;
}

static int snd_compr_start(struct snd_compr_stream *stream)
{
	int retval;

	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_SETUP:
		if (stream->direction != SND_COMPRESS_CAPTURE)
			return -EPERM;
		break;
	case SNDRV_PCM_STATE_PREPARED:
		break;
	default:
		return -EPERM;
	}

	retval = stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_START);
	if (!retval)
		stream->runtime->state = SNDRV_PCM_STATE_RUNNING;
	return retval;
}

static int snd_compr_stop(struct snd_compr_stream *stream)
{
	int retval;

	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_SETUP:
	case SNDRV_PCM_STATE_PREPARED:
		return -EPERM;
	default:
		break;
	}

	retval = stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_STOP);
	if (!retval) {
		/* clear flags and stop any drain wait */
		stream->partial_drain = false;
		stream->metadata_set = false;
		stream->pause_in_draining = false;
		snd_compr_drain_notify(stream);
		stream->runtime->total_bytes_available = 0;
		stream->runtime->total_bytes_transferred = 0;
	}
	return retval;
}

static void error_delayed_work(struct work_struct *work)
{
	struct snd_compr_stream *stream;

	stream = container_of(work, struct snd_compr_stream, error_work.work);

	guard(mutex)(&stream->device->lock);

	stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_STOP);
	wake_up(&stream->runtime->sleep);
}

/**
 * snd_compr_stop_error: Report a fatal error on a stream
 * @stream: pointer to stream
 * @state: state to transition the stream to
 *
 * Stop the stream and set its state.
 *
 * Should be called with compressed device lock held.
 *
 * Return: zero if successful, or a negative error code
 */
int snd_compr_stop_error(struct snd_compr_stream *stream,
			 snd_pcm_state_t state)
{
	if (stream->runtime->state == state)
		return 0;

	stream->runtime->state = state;

	pr_debug("Changing state to: %d\n", state);

	queue_delayed_work(system_power_efficient_wq, &stream->error_work, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_compr_stop_error);

static int snd_compress_wait_for_drain(struct snd_compr_stream *stream)
{
	int ret;

	/*
	 * We are called with lock held. So drop the lock while we wait for
	 * drain complete notification from the driver
	 *
	 * It is expected that driver will notify the drain completion and then
	 * stream will be moved to SETUP state, even if draining resulted in an
	 * error. We can trigger next track after this.
	 */
	stream->runtime->state = SNDRV_PCM_STATE_DRAINING;
	mutex_unlock(&stream->device->lock);

	/* we wait for drain to complete here, drain can return when
	 * interruption occurred, wait returned error or success.
	 * For the first two cases we don't do anything different here and
	 * return after waking up
	 */

	ret = wait_event_interruptible(stream->runtime->sleep,
			(stream->runtime->state != SNDRV_PCM_STATE_DRAINING));
	if (ret == -ERESTARTSYS)
		pr_debug("wait aborted by a signal\n");
	else if (ret)
		pr_debug("wait for drain failed with %d\n", ret);


	wake_up(&stream->runtime->sleep);
	mutex_lock(&stream->device->lock);

	return ret;
}

static int snd_compr_drain(struct snd_compr_stream *stream)
{
	int retval;

	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_SETUP:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
		return -EPERM;
	case SNDRV_PCM_STATE_XRUN:
		return -EPIPE;
	default:
		break;
	}

	retval = stream->ops->trigger(stream, SND_COMPR_TRIGGER_DRAIN);
	if (retval) {
		pr_debug("SND_COMPR_TRIGGER_DRAIN failed %d\n", retval);
		wake_up(&stream->runtime->sleep);
		return retval;
	}

	return snd_compress_wait_for_drain(stream);
}

static int snd_compr_next_track(struct snd_compr_stream *stream)
{
	int retval;

	/* only a running stream can transition to next track */
	if (stream->runtime->state != SNDRV_PCM_STATE_RUNNING)
		return -EPERM;

	/* next track doesn't have any meaning for capture streams */
	if (stream->direction == SND_COMPRESS_CAPTURE)
		return -EPERM;

	/* you can signal next track if this is intended to be a gapless stream
	 * and current track metadata is set
	 */
	if (stream->metadata_set == false)
		return -EPERM;

	retval = stream->ops->trigger(stream, SND_COMPR_TRIGGER_NEXT_TRACK);
	if (retval != 0)
		return retval;
	stream->metadata_set = false;
	stream->next_track = true;
	return 0;
}

static int snd_compr_partial_drain(struct snd_compr_stream *stream)
{
	int retval;

	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_SETUP:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
		return -EPERM;
	case SNDRV_PCM_STATE_XRUN:
		return -EPIPE;
	default:
		break;
	}

	/* partial drain doesn't have any meaning for capture streams */
	if (stream->direction == SND_COMPRESS_CAPTURE)
		return -EPERM;

	/* stream can be drained only when next track has been signalled */
	if (stream->next_track == false)
		return -EPERM;

	stream->partial_drain = true;
	retval = stream->ops->trigger(stream, SND_COMPR_TRIGGER_PARTIAL_DRAIN);
	if (retval) {
		pr_debug("Partial drain returned failure\n");
		wake_up(&stream->runtime->sleep);
		return retval;
	}

	stream->next_track = false;
	return snd_compress_wait_for_drain(stream);
}

#if IS_ENABLED(CONFIG_SND_COMPRESS_ACCEL)

static struct snd_compr_task_runtime *
snd_compr_find_task(struct snd_compr_stream *stream, __u64 seqno)
{
	struct snd_compr_task_runtime *task;

	list_for_each_entry(task, &stream->runtime->tasks, list) {
		if (task->seqno == seqno)
			return task;
	}
	return NULL;
}

static void snd_compr_task_free(struct snd_compr_task_runtime *task)
{
	if (task->output)
		dma_buf_put(task->output);
	if (task->input)
		dma_buf_put(task->input);
	kfree(task);
}

static u64 snd_compr_seqno_next(struct snd_compr_stream *stream)
{
	u64 seqno = ++stream->runtime->task_seqno;
	if (seqno == 0)
		seqno = ++stream->runtime->task_seqno;
	return seqno;
}

static int snd_compr_task_new(struct snd_compr_stream *stream, struct snd_compr_task *utask)
{
	struct snd_compr_task_runtime *task;
	int retval, fd_i, fd_o;

	if (stream->runtime->total_tasks >= stream->runtime->fragments)
		return -EBUSY;
	if (utask->origin_seqno != 0 || utask->input_size != 0)
		return -EINVAL;
	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (task == NULL)
		return -ENOMEM;
	task->seqno = utask->seqno = snd_compr_seqno_next(stream);
	task->input_size = utask->input_size;
	retval = stream->ops->task_create(stream, task);
	if (retval < 0)
		goto cleanup;
	/* similar functionality as in dma_buf_fd(), but ensure that both
	   file descriptors are allocated before fd_install() */
	if (!task->input || !task->input->file || !task->output || !task->output->file) {
		retval = -EINVAL;
		goto cleanup;
	}
	fd_i = get_unused_fd_flags(O_WRONLY|O_CLOEXEC);
	if (fd_i < 0)
		goto cleanup;
	fd_o = get_unused_fd_flags(O_RDONLY|O_CLOEXEC);
	if (fd_o < 0) {
		put_unused_fd(fd_i);
		goto cleanup;
	}
	/* keep dmabuf reference until freed with task free ioctl */
	get_dma_buf(task->input);
	get_dma_buf(task->output);
	fd_install(fd_i, task->input->file);
	fd_install(fd_o, task->output->file);
	utask->input_fd = fd_i;
	utask->output_fd = fd_o;
	list_add_tail(&task->list, &stream->runtime->tasks);
	stream->runtime->total_tasks++;
	return 0;
cleanup:
	snd_compr_task_free(task);
	return retval;
}

static int snd_compr_task_create(struct snd_compr_stream *stream, unsigned long arg)
{
	struct snd_compr_task *task __free(kfree) = NULL;
	int retval;

	if (stream->runtime->state != SNDRV_PCM_STATE_SETUP)
		return -EPERM;
	task = memdup_user((void __user *)arg, sizeof(*task));
	if (IS_ERR(task))
		return PTR_ERR(task);
	retval = snd_compr_task_new(stream, task);
	if (retval >= 0)
		if (copy_to_user((void __user *)arg, task, sizeof(*task)))
			retval = -EFAULT;
	return retval;
}

static int snd_compr_task_start_prepare(struct snd_compr_task_runtime *task,
					struct snd_compr_task *utask)
{
	if (task == NULL)
		return -EINVAL;
	if (task->state >= SND_COMPRESS_TASK_STATE_FINISHED)
		return -EBUSY;
	if (utask->input_size > task->input->size)
		return -EINVAL;
	task->flags = utask->flags;
	task->input_size = utask->input_size;
	task->state = SND_COMPRESS_TASK_STATE_IDLE;
	return 0;
}

static int snd_compr_task_start(struct snd_compr_stream *stream, struct snd_compr_task *utask)
{
	struct snd_compr_task_runtime *task;
	int retval;

	if (utask->origin_seqno > 0) {
		task = snd_compr_find_task(stream, utask->origin_seqno);
		retval = snd_compr_task_start_prepare(task, utask);
		if (retval < 0)
			return retval;
		task->seqno = utask->seqno = snd_compr_seqno_next(stream);
		utask->origin_seqno = 0;
		list_move_tail(&task->list, &stream->runtime->tasks);
	} else {
		task = snd_compr_find_task(stream, utask->seqno);
		if (task && task->state != SND_COMPRESS_TASK_STATE_IDLE)
			return -EBUSY;
		retval = snd_compr_task_start_prepare(task, utask);
		if (retval < 0)
			return retval;
	}
	retval = stream->ops->task_start(stream, task);
	if (retval >= 0) {
		task->state = SND_COMPRESS_TASK_STATE_ACTIVE;
		stream->runtime->active_tasks++;
	}
	return retval;
}

static int snd_compr_task_start_ioctl(struct snd_compr_stream *stream, unsigned long arg)
{
	struct snd_compr_task *task __free(kfree) = NULL;
	int retval;

	if (stream->runtime->state != SNDRV_PCM_STATE_SETUP)
		return -EPERM;
	task = memdup_user((void __user *)arg, sizeof(*task));
	if (IS_ERR(task))
		return PTR_ERR(task);
	retval = snd_compr_task_start(stream, task);
	if (retval >= 0)
		if (copy_to_user((void __user *)arg, task, sizeof(*task)))
			retval = -EFAULT;
	return retval;
}

static void snd_compr_task_stop_one(struct snd_compr_stream *stream,
					struct snd_compr_task_runtime *task)
{
	if (task->state != SND_COMPRESS_TASK_STATE_ACTIVE)
		return;
	stream->ops->task_stop(stream, task);
	if (!snd_BUG_ON(stream->runtime->active_tasks == 0))
		stream->runtime->active_tasks--;
	list_move_tail(&task->list, &stream->runtime->tasks);
	task->state = SND_COMPRESS_TASK_STATE_IDLE;
}

static void snd_compr_task_free_one(struct snd_compr_stream *stream,
					struct snd_compr_task_runtime *task)
{
	snd_compr_task_stop_one(stream, task);
	stream->ops->task_free(stream, task);
	list_del(&task->list);
	snd_compr_task_free(task);
	stream->runtime->total_tasks--;
}

static void snd_compr_task_free_all(struct snd_compr_stream *stream)
{
	struct snd_compr_task_runtime *task, *temp;

	list_for_each_entry_safe_reverse(task, temp, &stream->runtime->tasks, list)
		snd_compr_task_free_one(stream, task);
}

typedef void (*snd_compr_seq_func_t)(struct snd_compr_stream *stream,
					struct snd_compr_task_runtime *task);

static int snd_compr_task_seq(struct snd_compr_stream *stream, unsigned long arg,
					snd_compr_seq_func_t fcn)
{
	struct snd_compr_task_runtime *task, *temp;
	__u64 seqno;
	int retval;

	if (stream->runtime->state != SNDRV_PCM_STATE_SETUP)
		return -EPERM;
	retval = copy_from_user(&seqno, (__u64 __user *)arg, sizeof(seqno));
	if (retval)
		return -EFAULT;
	retval = 0;
	if (seqno == 0) {
		list_for_each_entry_safe_reverse(task, temp, &stream->runtime->tasks, list)
			fcn(stream, task);
	} else {
		task = snd_compr_find_task(stream, seqno);
		if (task == NULL) {
			retval = -EINVAL;
		} else {
			fcn(stream, task);
		}
	}
	return retval;
}

static int snd_compr_task_status(struct snd_compr_stream *stream,
					struct snd_compr_task_status *status)
{
	struct snd_compr_task_runtime *task;

	task = snd_compr_find_task(stream, status->seqno);
	if (task == NULL)
		return -EINVAL;
	status->input_size = task->input_size;
	status->output_size = task->output_size;
	status->state = task->state;
	return 0;
}

static int snd_compr_task_status_ioctl(struct snd_compr_stream *stream, unsigned long arg)
{
	struct snd_compr_task_status *status __free(kfree) = NULL;
	int retval;

	if (stream->runtime->state != SNDRV_PCM_STATE_SETUP)
		return -EPERM;
	status = memdup_user((void __user *)arg, sizeof(*status));
	if (IS_ERR(status))
		return PTR_ERR(status);
	retval = snd_compr_task_status(stream, status);
	if (retval >= 0)
		if (copy_to_user((void __user *)arg, status, sizeof(*status)))
			retval = -EFAULT;
	return retval;
}

/**
 * snd_compr_task_finished: Notify that the task was finished
 * @stream: pointer to stream
 * @task: runtime task structure
 *
 * Set the finished task state and notify waiters.
 */
void snd_compr_task_finished(struct snd_compr_stream *stream,
			    struct snd_compr_task_runtime *task)
{
	guard(mutex)(&stream->device->lock);
	if (!snd_BUG_ON(stream->runtime->active_tasks == 0))
		stream->runtime->active_tasks--;
	task->state = SND_COMPRESS_TASK_STATE_FINISHED;
	wake_up(&stream->runtime->sleep);
}
EXPORT_SYMBOL_GPL(snd_compr_task_finished);

MODULE_IMPORT_NS("DMA_BUF");
#endif /* CONFIG_SND_COMPRESS_ACCEL */

static long snd_compr_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct snd_compr_file *data = f->private_data;
	struct snd_compr_stream *stream;

	if (snd_BUG_ON(!data))
		return -EFAULT;

	stream = &data->stream;

	guard(mutex)(&stream->device->lock);
	switch (cmd) {
	case SNDRV_COMPRESS_IOCTL_VERSION:
		return put_user(SNDRV_COMPRESS_VERSION,
				(int __user *)arg) ? -EFAULT : 0;
	case SNDRV_COMPRESS_GET_CAPS:
		return snd_compr_get_caps(stream, arg);
#ifndef COMPR_CODEC_CAPS_OVERFLOW
	case SNDRV_COMPRESS_GET_CODEC_CAPS:
		return snd_compr_get_codec_caps(stream, arg);
#endif
	case SNDRV_COMPRESS_SET_PARAMS:
		return snd_compr_set_params(stream, arg);
	case SNDRV_COMPRESS_GET_PARAMS:
		return snd_compr_get_params(stream, arg);
	case SNDRV_COMPRESS_SET_METADATA:
		return snd_compr_set_metadata(stream, arg);
	case SNDRV_COMPRESS_GET_METADATA:
		return snd_compr_get_metadata(stream, arg);
	}

	if (stream->direction == SND_COMPRESS_ACCEL) {
#if IS_ENABLED(CONFIG_SND_COMPRESS_ACCEL)
		switch (cmd) {
		case SNDRV_COMPRESS_TASK_CREATE:
			return snd_compr_task_create(stream, arg);
		case SNDRV_COMPRESS_TASK_FREE:
			return snd_compr_task_seq(stream, arg, snd_compr_task_free_one);
		case SNDRV_COMPRESS_TASK_START:
			return snd_compr_task_start_ioctl(stream, arg);
		case SNDRV_COMPRESS_TASK_STOP:
			return snd_compr_task_seq(stream, arg, snd_compr_task_stop_one);
		case SNDRV_COMPRESS_TASK_STATUS:
			return snd_compr_task_status_ioctl(stream, arg);
		}
#endif
		return -ENOTTY;
	}

	switch (cmd) {
	case SNDRV_COMPRESS_TSTAMP:
		return snd_compr_tstamp(stream, arg);
	case SNDRV_COMPRESS_AVAIL:
		return snd_compr_ioctl_avail(stream, arg);
	case SNDRV_COMPRESS_PAUSE:
		return snd_compr_pause(stream);
	case SNDRV_COMPRESS_RESUME:
		return snd_compr_resume(stream);
	case SNDRV_COMPRESS_START:
		return snd_compr_start(stream);
	case SNDRV_COMPRESS_STOP:
		return snd_compr_stop(stream);
	case SNDRV_COMPRESS_DRAIN:
		return snd_compr_drain(stream);
	case SNDRV_COMPRESS_PARTIAL_DRAIN:
		return snd_compr_partial_drain(stream);
	case SNDRV_COMPRESS_NEXT_TRACK:
		return snd_compr_next_track(stream);
	}

	return -ENOTTY;
}

/* support of 32bit userspace on 64bit platforms */
#ifdef CONFIG_COMPAT
static long snd_compr_ioctl_compat(struct file *file, unsigned int cmd,
						unsigned long arg)
{
	return snd_compr_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations snd_compr_file_ops = {
		.owner =	THIS_MODULE,
		.open =		snd_compr_open,
		.release =	snd_compr_free,
		.write =	snd_compr_write,
		.read =		snd_compr_read,
		.unlocked_ioctl = snd_compr_ioctl,
#ifdef CONFIG_COMPAT
		.compat_ioctl = snd_compr_ioctl_compat,
#endif
		.mmap =		snd_compr_mmap,
		.poll =		snd_compr_poll,
};

static int snd_compress_dev_register(struct snd_device *device)
{
	int ret;
	struct snd_compr *compr;

	if (snd_BUG_ON(!device || !device->device_data))
		return -EBADFD;
	compr = device->device_data;

	pr_debug("reg device %s, direction %d\n", compr->name,
			compr->direction);
	/* register compressed device */
	ret = snd_register_device(SNDRV_DEVICE_TYPE_COMPRESS,
				  compr->card, compr->device,
				  &snd_compr_file_ops, compr, compr->dev);
	if (ret < 0) {
		pr_err("snd_register_device failed %d\n", ret);
		return ret;
	}
	return ret;

}

static int snd_compress_dev_disconnect(struct snd_device *device)
{
	struct snd_compr *compr;

	compr = device->device_data;
	snd_unregister_device(compr->dev);
	return 0;
}

#ifdef CONFIG_SND_VERBOSE_PROCFS
static void snd_compress_proc_info_read(struct snd_info_entry *entry,
					struct snd_info_buffer *buffer)
{
	struct snd_compr *compr = (struct snd_compr *)entry->private_data;

	snd_iprintf(buffer, "card: %d\n", compr->card->number);
	snd_iprintf(buffer, "device: %d\n", compr->device);
	snd_iprintf(buffer, "stream: %s\n",
			compr->direction == SND_COMPRESS_PLAYBACK
				? "PLAYBACK" : "CAPTURE");
	snd_iprintf(buffer, "id: %s\n", compr->id);
}

static int snd_compress_proc_init(struct snd_compr *compr)
{
	struct snd_info_entry *entry;
	char name[16];

	sprintf(name, "compr%i", compr->device);
	entry = snd_info_create_card_entry(compr->card, name,
					   compr->card->proc_root);
	if (!entry)
		return -ENOMEM;
	entry->mode = S_IFDIR | 0555;
	compr->proc_root = entry;

	entry = snd_info_create_card_entry(compr->card, "info",
					   compr->proc_root);
	if (entry)
		snd_info_set_text_ops(entry, compr,
				      snd_compress_proc_info_read);
	compr->proc_info_entry = entry;

	return 0;
}

static void snd_compress_proc_done(struct snd_compr *compr)
{
	snd_info_free_entry(compr->proc_info_entry);
	compr->proc_info_entry = NULL;
	snd_info_free_entry(compr->proc_root);
	compr->proc_root = NULL;
}

static inline void snd_compress_set_id(struct snd_compr *compr, const char *id)
{
	strscpy(compr->id, id, sizeof(compr->id));
}
#else
static inline int snd_compress_proc_init(struct snd_compr *compr)
{
	return 0;
}

static inline void snd_compress_proc_done(struct snd_compr *compr)
{
}

static inline void snd_compress_set_id(struct snd_compr *compr, const char *id)
{
}
#endif

static int snd_compress_dev_free(struct snd_device *device)
{
	struct snd_compr *compr;

	compr = device->device_data;
	snd_compress_proc_done(compr);
	put_device(compr->dev);
	return 0;
}

/**
 * snd_compress_new: create new compress device
 * @card: sound card pointer
 * @device: device number
 * @dirn: device direction, should be of type enum snd_compr_direction
 * @id: ID string
 * @compr: compress device pointer
 *
 * Return: zero if successful, or a negative error code
 */
int snd_compress_new(struct snd_card *card, int device,
			int dirn, const char *id, struct snd_compr *compr)
{
	static const struct snd_device_ops ops = {
		.dev_free = snd_compress_dev_free,
		.dev_register = snd_compress_dev_register,
		.dev_disconnect = snd_compress_dev_disconnect,
	};
	int ret;

#if !IS_ENABLED(CONFIG_SND_COMPRESS_ACCEL)
	if (snd_BUG_ON(dirn == SND_COMPRESS_ACCEL))
		return -EINVAL;
#endif

	compr->card = card;
	compr->device = device;
	compr->direction = dirn;
	mutex_init(&compr->lock);

	snd_compress_set_id(compr, id);

	ret = snd_device_alloc(&compr->dev, card);
	if (ret)
		return ret;
	dev_set_name(compr->dev, "comprC%iD%i", card->number, device);

	ret = snd_device_new(card, SNDRV_DEV_COMPRESS, compr, &ops);
	if (ret == 0)
		snd_compress_proc_init(compr);
	else
		put_device(compr->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_compress_new);

MODULE_DESCRIPTION("ALSA Compressed offload framework");
MODULE_AUTHOR("Vinod Koul <vinod.koul@linux.intel.com>");
MODULE_LICENSE("GPL v2");
