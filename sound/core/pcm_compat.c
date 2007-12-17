/*
 *   32bit -> 64bit ioctl wrapper for PCM API
 *   Copyright (c) by Takashi Iwai <tiwai@suse.de>
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

/* This file included from pcm_native.c */

#include <linux/compat.h>

static int snd_pcm_ioctl_delay_compat(struct snd_pcm_substream *substream,
				      s32 __user *src)
{
	snd_pcm_sframes_t delay;
	mm_segment_t fs;
	int err;

	fs = snd_enter_user();
	err = snd_pcm_delay(substream, &delay);
	snd_leave_user(fs);
	if (err < 0)
		return err;
	if (put_user(delay, src))
		return -EFAULT;
	return err;
}

static int snd_pcm_ioctl_rewind_compat(struct snd_pcm_substream *substream,
				       u32 __user *src)
{
	snd_pcm_uframes_t frames;
	int err;

	if (get_user(frames, src))
		return -EFAULT;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		err = snd_pcm_playback_rewind(substream, frames);
	else
		err = snd_pcm_capture_rewind(substream, frames);
	if (put_user(err, src))
		return -EFAULT;
	return err < 0 ? err : 0;
}

static int snd_pcm_ioctl_forward_compat(struct snd_pcm_substream *substream,
				       u32 __user *src)
{
	snd_pcm_uframes_t frames;
	int err;

	if (get_user(frames, src))
		return -EFAULT;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		err = snd_pcm_playback_forward(substream, frames);
	else
		err = snd_pcm_capture_forward(substream, frames);
	if (put_user(err, src))
		return -EFAULT;
	return err < 0 ? err : 0;
}

struct snd_pcm_hw_params32 {
	u32 flags;
	struct snd_mask masks[SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK + 1]; /* this must be identical */
	struct snd_mask mres[5];	/* reserved masks */
	struct snd_interval intervals[SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1];
	struct snd_interval ires[9];	/* reserved intervals */
	u32 rmask;
	u32 cmask;
	u32 info;
	u32 msbits;
	u32 rate_num;
	u32 rate_den;
	u32 fifo_size;
	unsigned char reserved[64];
};

struct snd_pcm_sw_params32 {
	s32 tstamp_mode;
	u32 period_step;
	u32 sleep_min;
	u32 avail_min;
	u32 xfer_align;
	u32 start_threshold;
	u32 stop_threshold;
	u32 silence_threshold;
	u32 silence_size;
	u32 boundary;
	unsigned char reserved[64];
};

/* recalcuate the boundary within 32bit */
static snd_pcm_uframes_t recalculate_boundary(struct snd_pcm_runtime *runtime)
{
	snd_pcm_uframes_t boundary;

	if (! runtime->buffer_size)
		return 0;
	boundary = runtime->buffer_size;
	while (boundary * 2 <= 0x7fffffffUL - runtime->buffer_size)
		boundary *= 2;
	return boundary;
}

static int snd_pcm_ioctl_sw_params_compat(struct snd_pcm_substream *substream,
					  struct snd_pcm_sw_params32 __user *src)
{
	struct snd_pcm_sw_params params;
	snd_pcm_uframes_t boundary;
	int err;

	memset(&params, 0, sizeof(params));
	if (get_user(params.tstamp_mode, &src->tstamp_mode) ||
	    get_user(params.period_step, &src->period_step) ||
	    get_user(params.sleep_min, &src->sleep_min) ||
	    get_user(params.avail_min, &src->avail_min) ||
	    get_user(params.xfer_align, &src->xfer_align) ||
	    get_user(params.start_threshold, &src->start_threshold) ||
	    get_user(params.stop_threshold, &src->stop_threshold) ||
	    get_user(params.silence_threshold, &src->silence_threshold) ||
	    get_user(params.silence_size, &src->silence_size))
		return -EFAULT;
	/*
	 * Check silent_size parameter.  Since we have 64bit boundary,
	 * silence_size must be compared with the 32bit boundary.
	 */
	boundary = recalculate_boundary(substream->runtime);
	if (boundary && params.silence_size >= boundary)
		params.silence_size = substream->runtime->boundary;
	err = snd_pcm_sw_params(substream, &params);
	if (err < 0)
		return err;
	if (boundary && put_user(boundary, &src->boundary))
		return -EFAULT;
	return err;
}

struct snd_pcm_channel_info32 {
	u32 channel;
	u32 offset;
	u32 first;
	u32 step;
};

static int snd_pcm_ioctl_channel_info_compat(struct snd_pcm_substream *substream,
					     struct snd_pcm_channel_info32 __user *src)
{
	struct snd_pcm_channel_info info;
	int err;

	if (get_user(info.channel, &src->channel) ||
	    get_user(info.offset, &src->offset) ||
	    get_user(info.first, &src->first) ||
	    get_user(info.step, &src->step))
		return -EFAULT;
	err = snd_pcm_channel_info(substream, &info);
	if (err < 0)
		return err;
	if (put_user(info.channel, &src->channel) ||
	    put_user(info.offset, &src->offset) ||
	    put_user(info.first, &src->first) ||
	    put_user(info.step, &src->step))
		return -EFAULT;
	return err;
}

struct snd_pcm_status32 {
	s32 state;
	struct compat_timespec trigger_tstamp;
	struct compat_timespec tstamp;
	u32 appl_ptr;
	u32 hw_ptr;
	s32 delay;
	u32 avail;
	u32 avail_max;
	u32 overrange;
	s32 suspended_state;
	unsigned char reserved[60];
} __attribute__((packed));


static int snd_pcm_status_user_compat(struct snd_pcm_substream *substream,
				      struct snd_pcm_status32 __user *src)
{
	struct snd_pcm_status status;
	int err;

	err = snd_pcm_status(substream, &status);
	if (err < 0)
		return err;

	if (put_user(status.state, &src->state) ||
	    put_user(status.trigger_tstamp.tv_sec, &src->trigger_tstamp.tv_sec) ||
	    put_user(status.trigger_tstamp.tv_nsec, &src->trigger_tstamp.tv_nsec) ||
	    put_user(status.tstamp.tv_sec, &src->tstamp.tv_sec) ||
	    put_user(status.tstamp.tv_nsec, &src->tstamp.tv_nsec) ||
	    put_user(status.appl_ptr, &src->appl_ptr) ||
	    put_user(status.hw_ptr, &src->hw_ptr) ||
	    put_user(status.delay, &src->delay) ||
	    put_user(status.avail, &src->avail) ||
	    put_user(status.avail_max, &src->avail_max) ||
	    put_user(status.overrange, &src->overrange) ||
	    put_user(status.suspended_state, &src->suspended_state))
		return -EFAULT;

	return err;
}

/* both for HW_PARAMS and HW_REFINE */
static int snd_pcm_ioctl_hw_params_compat(struct snd_pcm_substream *substream,
					  int refine, 
					  struct snd_pcm_hw_params32 __user *data32)
{
	struct snd_pcm_hw_params *data;
	struct snd_pcm_runtime *runtime;
	int err;

	if (! (runtime = substream->runtime))
		return -ENOTTY;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	/* only fifo_size is different, so just copy all */
	if (copy_from_user(data, data32, sizeof(*data32))) {
		err = -EFAULT;
		goto error;
	}
	if (refine)
		err = snd_pcm_hw_refine(substream, data);
	else
		err = snd_pcm_hw_params(substream, data);
	if (err < 0)
		goto error;
	if (copy_to_user(data32, data, sizeof(*data32)) ||
	    put_user(data->fifo_size, &data32->fifo_size)) {
		err = -EFAULT;
		goto error;
	}

	if (! refine) {
		unsigned int new_boundary = recalculate_boundary(runtime);
		if (new_boundary)
			runtime->boundary = new_boundary;
	}
 error:
	kfree(data);
	return err;
}


/*
 */
struct snd_xferi32 {
	s32 result;
	u32 buf;
	u32 frames;
};

static int snd_pcm_ioctl_xferi_compat(struct snd_pcm_substream *substream,
				      int dir, struct snd_xferi32 __user *data32)
{
	compat_caddr_t buf;
	u32 frames;
	int err;

	if (! substream->runtime)
		return -ENOTTY;
	if (substream->stream != dir)
		return -EINVAL;
	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;

	if (get_user(buf, &data32->buf) ||
	    get_user(frames, &data32->frames))
		return -EFAULT;

	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		err = snd_pcm_lib_write(substream, compat_ptr(buf), frames);
	else
		err = snd_pcm_lib_read(substream, compat_ptr(buf), frames);
	if (err < 0)
		return err;
	/* copy the result */
	if (put_user(err, &data32->result))
		return -EFAULT;
	return 0;
}


/* snd_xfern needs remapping of bufs */
struct snd_xfern32 {
	s32 result;
	u32 bufs;  /* this is void **; */
	u32 frames;
};

/*
 * xfern ioctl nees to copy (up to) 128 pointers on stack.
 * although we may pass the copied pointers through f_op->ioctl, but the ioctl
 * handler there expands again the same 128 pointers on stack, so it is better
 * to handle the function (calling pcm_readv/writev) directly in this handler.
 */
static int snd_pcm_ioctl_xfern_compat(struct snd_pcm_substream *substream,
				      int dir, struct snd_xfern32 __user *data32)
{
	compat_caddr_t buf;
	compat_caddr_t __user *bufptr;
	u32 frames;
	void __user **bufs;
	int err, ch, i;

	if (! substream->runtime)
		return -ENOTTY;
	if (substream->stream != dir)
		return -EINVAL;

	if ((ch = substream->runtime->channels) > 128)
		return -EINVAL;
	if (get_user(buf, &data32->bufs) ||
	    get_user(frames, &data32->frames))
		return -EFAULT;
	bufptr = compat_ptr(buf);
	bufs = kmalloc(sizeof(void __user *) * ch, GFP_KERNEL);
	if (bufs == NULL)
		return -ENOMEM;
	for (i = 0; i < ch; i++) {
		u32 ptr;
		if (get_user(ptr, bufptr)) {
			kfree(bufs);
			return -EFAULT;
		}
		bufs[ch] = compat_ptr(ptr);
		bufptr++;
	}
	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		err = snd_pcm_lib_writev(substream, bufs, frames);
	else
		err = snd_pcm_lib_readv(substream, bufs, frames);
	if (err >= 0) {
		if (put_user(err, &data32->result))
			err = -EFAULT;
	}
	kfree(bufs);
	return err;
}


struct snd_pcm_mmap_status32 {
	s32 state;
	s32 pad1;
	u32 hw_ptr;
	struct compat_timespec tstamp;
	s32 suspended_state;
} __attribute__((packed));

struct snd_pcm_mmap_control32 {
	u32 appl_ptr;
	u32 avail_min;
};

struct snd_pcm_sync_ptr32 {
	u32 flags;
	union {
		struct snd_pcm_mmap_status32 status;
		unsigned char reserved[64];
	} s;
	union {
		struct snd_pcm_mmap_control32 control;
		unsigned char reserved[64];
	} c;
} __attribute__((packed));

static int snd_pcm_ioctl_sync_ptr_compat(struct snd_pcm_substream *substream,
					 struct snd_pcm_sync_ptr32 __user *src)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	volatile struct snd_pcm_mmap_status *status;
	volatile struct snd_pcm_mmap_control *control;
	u32 sflags;
	struct snd_pcm_mmap_control scontrol;
	struct snd_pcm_mmap_status sstatus;
	snd_pcm_uframes_t boundary;
	int err;

	snd_assert(runtime, return -EINVAL);

	if (get_user(sflags, &src->flags) ||
	    get_user(scontrol.appl_ptr, &src->c.control.appl_ptr) ||
	    get_user(scontrol.avail_min, &src->c.control.avail_min))
		return -EFAULT;
	if (sflags & SNDRV_PCM_SYNC_PTR_HWSYNC) {
		err = snd_pcm_hwsync(substream);
		if (err < 0)
			return err;
	}
	status = runtime->status;
	control = runtime->control;
	boundary = recalculate_boundary(runtime);
	if (! boundary)
		boundary = 0x7fffffff;
	snd_pcm_stream_lock_irq(substream);
	/* FIXME: we should consider the boundary for the sync from app */
	if (!(sflags & SNDRV_PCM_SYNC_PTR_APPL))
		control->appl_ptr = scontrol.appl_ptr;
	else
		scontrol.appl_ptr = control->appl_ptr % boundary;
	if (!(sflags & SNDRV_PCM_SYNC_PTR_AVAIL_MIN))
		control->avail_min = scontrol.avail_min;
	else
		scontrol.avail_min = control->avail_min;
	sstatus.state = status->state;
	sstatus.hw_ptr = status->hw_ptr % boundary;
	sstatus.tstamp = status->tstamp;
	sstatus.suspended_state = status->suspended_state;
	snd_pcm_stream_unlock_irq(substream);
	if (put_user(sstatus.state, &src->s.status.state) ||
	    put_user(sstatus.hw_ptr, &src->s.status.hw_ptr) ||
	    put_user(sstatus.tstamp.tv_sec, &src->s.status.tstamp.tv_sec) ||
	    put_user(sstatus.tstamp.tv_nsec, &src->s.status.tstamp.tv_nsec) ||
	    put_user(sstatus.suspended_state, &src->s.status.suspended_state) ||
	    put_user(scontrol.appl_ptr, &src->c.control.appl_ptr) ||
	    put_user(scontrol.avail_min, &src->c.control.avail_min))
		return -EFAULT;

	return 0;
}


/*
 */
enum {
	SNDRV_PCM_IOCTL_HW_REFINE32 = _IOWR('A', 0x10, struct snd_pcm_hw_params32),
	SNDRV_PCM_IOCTL_HW_PARAMS32 = _IOWR('A', 0x11, struct snd_pcm_hw_params32),
	SNDRV_PCM_IOCTL_SW_PARAMS32 = _IOWR('A', 0x13, struct snd_pcm_sw_params32),
	SNDRV_PCM_IOCTL_STATUS32 = _IOR('A', 0x20, struct snd_pcm_status32),
	SNDRV_PCM_IOCTL_DELAY32 = _IOR('A', 0x21, s32),
	SNDRV_PCM_IOCTL_CHANNEL_INFO32 = _IOR('A', 0x32, struct snd_pcm_channel_info32),
	SNDRV_PCM_IOCTL_REWIND32 = _IOW('A', 0x46, u32),
	SNDRV_PCM_IOCTL_FORWARD32 = _IOW('A', 0x49, u32),
	SNDRV_PCM_IOCTL_WRITEI_FRAMES32 = _IOW('A', 0x50, struct snd_xferi32),
	SNDRV_PCM_IOCTL_READI_FRAMES32 = _IOR('A', 0x51, struct snd_xferi32),
	SNDRV_PCM_IOCTL_WRITEN_FRAMES32 = _IOW('A', 0x52, struct snd_xfern32),
	SNDRV_PCM_IOCTL_READN_FRAMES32 = _IOR('A', 0x53, struct snd_xfern32),
	SNDRV_PCM_IOCTL_SYNC_PTR32 = _IOWR('A', 0x23, struct snd_pcm_sync_ptr32),

};

static long snd_pcm_ioctl_compat(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct snd_pcm_file *pcm_file;
	struct snd_pcm_substream *substream;
	void __user *argp = compat_ptr(arg);

	pcm_file = file->private_data;
	if (! pcm_file)
		return -ENOTTY;
	substream = pcm_file->substream;
	if (! substream)
		return -ENOTTY;

	/*
	 * When PCM is used on 32bit mode, we need to disable
	 * mmap of PCM status/control records because of the size
	 * incompatibility.
	 */
	pcm_file->no_compat_mmap = 1;

	switch (cmd) {
	case SNDRV_PCM_IOCTL_PVERSION:
	case SNDRV_PCM_IOCTL_INFO:
	case SNDRV_PCM_IOCTL_TSTAMP:
	case SNDRV_PCM_IOCTL_TTSTAMP:
	case SNDRV_PCM_IOCTL_HWSYNC:
	case SNDRV_PCM_IOCTL_PREPARE:
	case SNDRV_PCM_IOCTL_RESET:
	case SNDRV_PCM_IOCTL_START:
	case SNDRV_PCM_IOCTL_DROP:
	case SNDRV_PCM_IOCTL_DRAIN:
	case SNDRV_PCM_IOCTL_PAUSE:
	case SNDRV_PCM_IOCTL_HW_FREE:
	case SNDRV_PCM_IOCTL_RESUME:
	case SNDRV_PCM_IOCTL_XRUN:
	case SNDRV_PCM_IOCTL_LINK:
	case SNDRV_PCM_IOCTL_UNLINK:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			return snd_pcm_playback_ioctl1(file, substream, cmd, argp);
		else
			return snd_pcm_capture_ioctl1(file, substream, cmd, argp);
	case SNDRV_PCM_IOCTL_HW_REFINE32:
		return snd_pcm_ioctl_hw_params_compat(substream, 1, argp);
	case SNDRV_PCM_IOCTL_HW_PARAMS32:
		return snd_pcm_ioctl_hw_params_compat(substream, 0, argp);
	case SNDRV_PCM_IOCTL_SW_PARAMS32:
		return snd_pcm_ioctl_sw_params_compat(substream, argp);
	case SNDRV_PCM_IOCTL_STATUS32:
		return snd_pcm_status_user_compat(substream, argp);
	case SNDRV_PCM_IOCTL_SYNC_PTR32:
		return snd_pcm_ioctl_sync_ptr_compat(substream, argp);
	case SNDRV_PCM_IOCTL_CHANNEL_INFO32:
		return snd_pcm_ioctl_channel_info_compat(substream, argp);
	case SNDRV_PCM_IOCTL_WRITEI_FRAMES32:
		return snd_pcm_ioctl_xferi_compat(substream, SNDRV_PCM_STREAM_PLAYBACK, argp);
	case SNDRV_PCM_IOCTL_READI_FRAMES32:
		return snd_pcm_ioctl_xferi_compat(substream, SNDRV_PCM_STREAM_CAPTURE, argp);
	case SNDRV_PCM_IOCTL_WRITEN_FRAMES32:
		return snd_pcm_ioctl_xfern_compat(substream, SNDRV_PCM_STREAM_PLAYBACK, argp);
	case SNDRV_PCM_IOCTL_READN_FRAMES32:
		return snd_pcm_ioctl_xfern_compat(substream, SNDRV_PCM_STREAM_CAPTURE, argp);
	case SNDRV_PCM_IOCTL_DELAY32:
		return snd_pcm_ioctl_delay_compat(substream, argp);
	case SNDRV_PCM_IOCTL_REWIND32:
		return snd_pcm_ioctl_rewind_compat(substream, argp);
	case SNDRV_PCM_IOCTL_FORWARD32:
		return snd_pcm_ioctl_forward_compat(substream, argp);
	}

	return -ENOIOCTLCMD;
}
