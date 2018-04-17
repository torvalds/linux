/*
 *  compress_core.c - compress offload core
 *
 *  Copyright (C) 2011 Intel Corporation
 *  Authors:	Vinod Koul <vinod.koul@linux.intel.com>
 *		Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
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

static DEFINE_MUTEX(device_mutex);

struct snd_compr_file {
	unsigned long caps;
	struct snd_compr_stream stream;
};

static void error_delayed_work(struct work_struct *work);

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
	data->stream.runtime = runtime;
	f->private_data = (void *)data;
	mutex_lock(&compr->lock);
	ret = compr->ops->open(&data->stream);
	mutex_unlock(&compr->lock);
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

	data->stream.ops->free(&data->stream);
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
	mutex_lock(&stream->device->lock);
	/* write is allowed when stream is running or has been steup */
	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_SETUP:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_RUNNING:
		break;
	default:
		mutex_unlock(&stream->device->lock);
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

	mutex_unlock(&stream->device->lock);
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
	mutex_lock(&stream->device->lock);

	/* read is allowed when stream is running, paused, draining and setup
	 * (yes setup is state which we transition to after stop, so if user
	 * wants to read data after stop we allow that)
	 */
	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_SUSPENDED:
	case SNDRV_PCM_STATE_DISCONNECTED:
		retval = -EBADFD;
		goto out;
	case SNDRV_PCM_STATE_XRUN:
		retval = -EPIPE;
		goto out;
	}

	avail = snd_compr_get_avail(stream);
	pr_debug("avail returned %ld\n", (unsigned long)avail);
	/* calculate how much we can read from buffer */
	if (avail > count)
		avail = count;

	if (stream->ops->copy) {
		retval = stream->ops->copy(stream, buf, avail);
	} else {
		retval = -ENXIO;
		goto out;
	}
	if (retval > 0)
		stream->runtime->total_bytes_transferred += retval;

out:
	mutex_unlock(&stream->device->lock);
	return retval;
}

static int snd_compr_mmap(struct file *f, struct vm_area_struct *vma)
{
	return -ENXIO;
}

static __poll_t snd_compr_get_poll(struct snd_compr_stream *stream)
{
	if (stream->direction == SND_COMPRESS_PLAYBACK)
		return POLLOUT | POLLWRNORM;
	else
		return POLLIN | POLLRDNORM;
}

static __poll_t snd_compr_poll(struct file *f, poll_table *wait)
{
	struct snd_compr_file *data = f->private_data;
	struct snd_compr_stream *stream;
	size_t avail;
	__poll_t retval = 0;

	if (snd_BUG_ON(!data))
		return POLLERR;

	stream = &data->stream;

	mutex_lock(&stream->device->lock);

	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_XRUN:
		retval = snd_compr_get_poll(stream) | POLLERR;
		goto out;
	default:
		break;
	}

	poll_wait(f, &stream->runtime->sleep, wait);

	avail = snd_compr_get_avail(stream);
	pr_debug("avail is %ld\n", (unsigned long)avail);
	/* check if we have at least one fragment to fill */
	switch (stream->runtime->state) {
	case SNDRV_PCM_STATE_DRAINING:
		/* stream has been woken up after drain is complete
		 * draining done so set stream state to stopped
		 */
		retval = snd_compr_get_poll(stream);
		stream->runtime->state = SNDRV_PCM_STATE_SETUP;
		break;
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
		if (avail >= stream->runtime->fragment_size)
			retval = snd_compr_get_poll(stream);
		break;
	default:
		retval = snd_compr_get_poll(stream) | POLLERR;
		break;
	}
out:
	mutex_unlock(&stream->device->lock);
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
	struct snd_compr_codec_caps *caps;

	if (!stream->ops->get_codec_caps)
		return -ENXIO;

	caps = kzalloc(sizeof(*caps), GFP_KERNEL);
	if (!caps)
		return -ENOMEM;

	retval = stream->ops->get_codec_caps(stream, caps);
	if (retval)
		goto out;
	if (copy_to_user((void __user *)arg, caps, sizeof(*caps)))
		retval = -EFAULT;

out:
	kfree(caps);
	return retval;
}
#endif /* !COMPR_CODEC_CAPS_OVERFLOW */

/* revisit this with snd_pcm_preallocate_xxx */
static int snd_compr_allocate_buffer(struct snd_compr_stream *stream,
		struct snd_compr_params *params)
{
	unsigned int buffer_size;
	void *buffer;

	buffer_size = params->buffer.fragment_size * params->buffer.fragments;
	if (stream->ops->copy) {
		buffer = NULL;
		/* if copy is defined the driver will be required to copy
		 * the data from core
		 */
	} else {
		buffer = kmalloc(buffer_size, GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;
	}
	stream->runtime->fragment_size = params->buffer.fragment_size;
	stream->runtime->fragments = params->buffer.fragments;
	stream->runtime->buffer = buffer;
	stream->runtime->buffer_size = buffer_size;
	return 0;
}

static int snd_compress_check_input(struct snd_compr_params *params)
{
	/* first let's check the buffer parameter's */
	if (params->buffer.fragment_size == 0 ||
	    params->buffer.fragments > INT_MAX / params->buffer.fragment_size)
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
	struct snd_compr_params *params;
	int retval;

	if (stream->runtime->state == SNDRV_PCM_STATE_OPEN) {
		/*
		 * we should allow parameter change only when stream has been
		 * opened not in other cases
		 */
		params = memdup_user((void __user *)arg, sizeof(*params));
		if (IS_ERR(params))
			return PTR_ERR(params);

		retval = snd_compress_check_input(params);
		if (retval)
			goto out;

		retval = snd_compr_allocate_buffer(stream, params);
		if (retval) {
			retval = -ENOMEM;
			goto out;
		}

		retval = stream->ops->set_params(stream, params);
		if (retval)
			goto out;

		stream->metadata_set = false;
		stream->next_track = false;

		if (stream->direction == SND_COMPRESS_PLAYBACK)
			stream->runtime->state = SNDRV_PCM_STATE_SETUP;
		else
			stream->runtime->state = SNDRV_PCM_STATE_PREPARED;
	} else {
		return -EPERM;
	}
out:
	kfree(params);
	return retval;
}

static int
snd_compr_get_params(struct snd_compr_stream *stream, unsigned long arg)
{
	struct snd_codec *params;
	int retval;

	if (!stream->ops->get_params)
		return -EBADFD;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;
	retval = stream->ops->get_params(stream, params);
	if (retval)
		goto out;
	if (copy_to_user((char __user *)arg, params, sizeof(*params)))
		retval = -EFAULT;

out:
	kfree(params);
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

	if (stream->runtime->state != SNDRV_PCM_STATE_RUNNING)
		return -EPERM;
	retval = stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_PAUSE_PUSH);
	if (!retval)
		stream->runtime->state = SNDRV_PCM_STATE_PAUSED;
	return retval;
}

static int snd_compr_resume(struct snd_compr_stream *stream)
{
	int retval;

	if (stream->runtime->state != SNDRV_PCM_STATE_PAUSED)
		return -EPERM;
	retval = stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_PAUSE_RELEASE);
	if (!retval)
		stream->runtime->state = SNDRV_PCM_STATE_RUNNING;
	return retval;
}

static int snd_compr_start(struct snd_compr_stream *stream)
{
	int retval;

	if (stream->runtime->state != SNDRV_PCM_STATE_PREPARED)
		return -EPERM;
	retval = stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_START);
	if (!retval)
		stream->runtime->state = SNDRV_PCM_STATE_RUNNING;
	return retval;
}

static int snd_compr_stop(struct snd_compr_stream *stream)
{
	int retval;

	if (stream->runtime->state == SNDRV_PCM_STATE_PREPARED ||
			stream->runtime->state == SNDRV_PCM_STATE_SETUP)
		return -EPERM;
	retval = stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_STOP);
	if (!retval) {
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

	mutex_lock(&stream->device->lock);

	stream->ops->trigger(stream, SNDRV_PCM_TRIGGER_STOP);
	wake_up(&stream->runtime->sleep);

	mutex_unlock(&stream->device->lock);
}

/*
 * snd_compr_stop_error: Report a fatal error on a stream
 * @stream: pointer to stream
 * @state: state to transition the stream to
 *
 * Stop the stream and set its state.
 *
 * Should be called with compressed device lock held.
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

	if (stream->runtime->state == SNDRV_PCM_STATE_PREPARED ||
			stream->runtime->state == SNDRV_PCM_STATE_SETUP)
		return -EPERM;

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
	if (stream->runtime->state == SNDRV_PCM_STATE_PREPARED ||
			stream->runtime->state == SNDRV_PCM_STATE_SETUP)
		return -EPERM;
	/* stream can be drained only when next track has been signalled */
	if (stream->next_track == false)
		return -EPERM;

	retval = stream->ops->trigger(stream, SND_COMPR_TRIGGER_PARTIAL_DRAIN);
	if (retval) {
		pr_debug("Partial drain returned failure\n");
		wake_up(&stream->runtime->sleep);
		return retval;
	}

	stream->next_track = false;
	return snd_compress_wait_for_drain(stream);
}

static long snd_compr_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct snd_compr_file *data = f->private_data;
	struct snd_compr_stream *stream;
	int retval = -ENOTTY;

	if (snd_BUG_ON(!data))
		return -EFAULT;

	stream = &data->stream;

	mutex_lock(&stream->device->lock);
	switch (_IOC_NR(cmd)) {
	case _IOC_NR(SNDRV_COMPRESS_IOCTL_VERSION):
		retval = put_user(SNDRV_COMPRESS_VERSION,
				(int __user *)arg) ? -EFAULT : 0;
		break;
	case _IOC_NR(SNDRV_COMPRESS_GET_CAPS):
		retval = snd_compr_get_caps(stream, arg);
		break;
#ifndef COMPR_CODEC_CAPS_OVERFLOW
	case _IOC_NR(SNDRV_COMPRESS_GET_CODEC_CAPS):
		retval = snd_compr_get_codec_caps(stream, arg);
		break;
#endif
	case _IOC_NR(SNDRV_COMPRESS_SET_PARAMS):
		retval = snd_compr_set_params(stream, arg);
		break;
	case _IOC_NR(SNDRV_COMPRESS_GET_PARAMS):
		retval = snd_compr_get_params(stream, arg);
		break;
	case _IOC_NR(SNDRV_COMPRESS_SET_METADATA):
		retval = snd_compr_set_metadata(stream, arg);
		break;
	case _IOC_NR(SNDRV_COMPRESS_GET_METADATA):
		retval = snd_compr_get_metadata(stream, arg);
		break;
	case _IOC_NR(SNDRV_COMPRESS_TSTAMP):
		retval = snd_compr_tstamp(stream, arg);
		break;
	case _IOC_NR(SNDRV_COMPRESS_AVAIL):
		retval = snd_compr_ioctl_avail(stream, arg);
		break;
	case _IOC_NR(SNDRV_COMPRESS_PAUSE):
		retval = snd_compr_pause(stream);
		break;
	case _IOC_NR(SNDRV_COMPRESS_RESUME):
		retval = snd_compr_resume(stream);
		break;
	case _IOC_NR(SNDRV_COMPRESS_START):
		retval = snd_compr_start(stream);
		break;
	case _IOC_NR(SNDRV_COMPRESS_STOP):
		retval = snd_compr_stop(stream);
		break;
	case _IOC_NR(SNDRV_COMPRESS_DRAIN):
		retval = snd_compr_drain(stream);
		break;
	case _IOC_NR(SNDRV_COMPRESS_PARTIAL_DRAIN):
		retval = snd_compr_partial_drain(stream);
		break;
	case _IOC_NR(SNDRV_COMPRESS_NEXT_TRACK):
		retval = snd_compr_next_track(stream);
		break;

	}
	mutex_unlock(&stream->device->lock);
	return retval;
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
	int ret = -EINVAL;
	struct snd_compr *compr;

	if (snd_BUG_ON(!device || !device->device_data))
		return -EBADFD;
	compr = device->device_data;

	pr_debug("reg device %s, direction %d\n", compr->name,
			compr->direction);
	/* register compressed device */
	ret = snd_register_device(SNDRV_DEVICE_TYPE_COMPRESS,
				  compr->card, compr->device,
				  &snd_compr_file_ops, compr, &compr->dev);
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
	snd_unregister_device(&compr->dev);
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
	entry->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return -ENOMEM;
	}
	compr->proc_root = entry;

	entry = snd_info_create_card_entry(compr->card, "info",
					   compr->proc_root);
	if (entry) {
		snd_info_set_text_ops(entry, compr,
				      snd_compress_proc_info_read);
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
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
	strlcpy(compr->id, id, sizeof(compr->id));
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
	put_device(&compr->dev);
	return 0;
}

/*
 * snd_compress_new: create new compress device
 * @card: sound card pointer
 * @device: device number
 * @dirn: device direction, should be of type enum snd_compr_direction
 * @compr: compress device pointer
 */
int snd_compress_new(struct snd_card *card, int device,
			int dirn, const char *id, struct snd_compr *compr)
{
	static struct snd_device_ops ops = {
		.dev_free = snd_compress_dev_free,
		.dev_register = snd_compress_dev_register,
		.dev_disconnect = snd_compress_dev_disconnect,
	};
	int ret;

	compr->card = card;
	compr->device = device;
	compr->direction = dirn;

	snd_compress_set_id(compr, id);

	snd_device_initialize(&compr->dev, card);
	dev_set_name(&compr->dev, "comprC%iD%i", card->number, device);

	ret = snd_device_new(card, SNDRV_DEV_COMPRESS, compr, &ops);
	if (ret == 0)
		snd_compress_proc_init(compr);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_compress_new);

static int snd_compress_add_device(struct snd_compr *device)
{
	int ret;

	if (!device->card)
		return -EINVAL;

	/* register the card */
	ret = snd_card_register(device->card);
	if (ret)
		goto out;
	return 0;

out:
	pr_err("failed with %d\n", ret);
	return ret;

}

static int snd_compress_remove_device(struct snd_compr *device)
{
	return snd_card_free(device->card);
}

/**
 * snd_compress_register - register compressed device
 *
 * @device: compressed device to register
 */
int snd_compress_register(struct snd_compr *device)
{
	int retval;

	if (device->name == NULL || device->ops == NULL)
		return -EINVAL;

	pr_debug("Registering compressed device %s\n", device->name);
	if (snd_BUG_ON(!device->ops->open))
		return -EINVAL;
	if (snd_BUG_ON(!device->ops->free))
		return -EINVAL;
	if (snd_BUG_ON(!device->ops->set_params))
		return -EINVAL;
	if (snd_BUG_ON(!device->ops->trigger))
		return -EINVAL;

	mutex_init(&device->lock);

	/* register a compressed card */
	mutex_lock(&device_mutex);
	retval = snd_compress_add_device(device);
	mutex_unlock(&device_mutex);
	return retval;
}
EXPORT_SYMBOL_GPL(snd_compress_register);

int snd_compress_deregister(struct snd_compr *device)
{
	pr_debug("Removing compressed device %s\n", device->name);
	mutex_lock(&device_mutex);
	snd_compress_remove_device(device);
	mutex_unlock(&device_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_compress_deregister);

static int __init snd_compress_init(void)
{
	return 0;
}

static void __exit snd_compress_exit(void)
{
}

module_init(snd_compress_init);
module_exit(snd_compress_exit);

MODULE_DESCRIPTION("ALSA Compressed offload framework");
MODULE_AUTHOR("Vinod Koul <vinod.koul@linux.intel.com>");
MODULE_LICENSE("GPL v2");
