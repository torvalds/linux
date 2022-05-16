// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   32bit -> 64bit ioctl wrapper for PCM API
 *   Copyright (c) by Takashi Iwai <tiwai@suse.de>
 */

/* This file included from pcm_native.c */

#include <linux/compat.h>
#include <linux/slab.h>

static int snd_pcm_ioctl_delay_compat(struct snd_pcm_substream *substream,
				      s32 __user *src)
{
	snd_pcm_sframes_t delay;
	int err;

	err = snd_pcm_delay(substream, &delay);
	if (err)
		return err;
	if (put_user(delay, src))
		return -EFAULT;
	return 0;
}

static int snd_pcm_ioctl_rewind_compat(struct snd_pcm_substream *substream,
				       u32 __user *src)
{
	snd_pcm_uframes_t frames;
	int err;

	if (get_user(frames, src))
		return -EFAULT;
	err = snd_pcm_rewind(substream, frames);
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
	err = snd_pcm_forward(substream, frames);
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
	u32 proto;
	u32 tstamp_type;
	unsigned char reserved[56];
};

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
	    get_user(params.silence_size, &src->silence_size) ||
	    get_user(params.tstamp_type, &src->tstamp_type) ||
	    get_user(params.proto, &src->proto))
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

#ifdef CONFIG_X86_X32
/* X32 ABI has the same struct as x86-64 for snd_pcm_channel_info */
static int snd_pcm_channel_info_user(struct snd_pcm_substream *substream,
				     struct snd_pcm_channel_info __user *src);
#define snd_pcm_ioctl_channel_info_x32(s, p)	\
	snd_pcm_channel_info_user(s, p)
#endif /* CONFIG_X86_X32 */

struct compat_snd_pcm_status64 {
	snd_pcm_state_t state;
	u8 rsvd[4]; /* alignment */
	s64 trigger_tstamp_sec;
	s64 trigger_tstamp_nsec;
	s64 tstamp_sec;
	s64 tstamp_nsec;
	u32 appl_ptr;
	u32 hw_ptr;
	s32 delay;
	u32 avail;
	u32 avail_max;
	u32 overrange;
	snd_pcm_state_t suspended_state;
	u32 audio_tstamp_data;
	s64 audio_tstamp_sec;
	s64 audio_tstamp_nsec;
	s64 driver_tstamp_sec;
	s64 driver_tstamp_nsec;
	u32 audio_tstamp_accuracy;
	unsigned char reserved[52-4*sizeof(s64)];
} __packed;

static int snd_pcm_status_user_compat64(struct snd_pcm_substream *substream,
					struct compat_snd_pcm_status64 __user *src,
					bool ext)
{
	struct snd_pcm_status64 status;
	struct compat_snd_pcm_status64 compat_status64;
	int err;

	memset(&status, 0, sizeof(status));
	memset(&compat_status64, 0, sizeof(compat_status64));
	/*
	 * with extension, parameters are read/write,
	 * get audio_tstamp_data from user,
	 * ignore rest of status structure
	 */
	if (ext && get_user(status.audio_tstamp_data,
				(u32 __user *)(&src->audio_tstamp_data)))
		return -EFAULT;
	err = snd_pcm_status64(substream, &status);
	if (err < 0)
		return err;

	if (clear_user(src, sizeof(*src)))
		return -EFAULT;

	compat_status64 = (struct compat_snd_pcm_status64) {
		.state = status.state,
		.trigger_tstamp_sec = status.trigger_tstamp_sec,
		.trigger_tstamp_nsec = status.trigger_tstamp_nsec,
		.tstamp_sec = status.tstamp_sec,
		.tstamp_nsec = status.tstamp_nsec,
		.appl_ptr = status.appl_ptr,
		.hw_ptr = status.hw_ptr,
		.delay = status.delay,
		.avail = status.avail,
		.avail_max = status.avail_max,
		.overrange = status.overrange,
		.suspended_state = status.suspended_state,
		.audio_tstamp_data = status.audio_tstamp_data,
		.audio_tstamp_sec = status.audio_tstamp_sec,
		.audio_tstamp_nsec = status.audio_tstamp_nsec,
		.driver_tstamp_sec = status.audio_tstamp_sec,
		.driver_tstamp_nsec = status.audio_tstamp_nsec,
		.audio_tstamp_accuracy = status.audio_tstamp_accuracy,
	};

	if (copy_to_user(src, &compat_status64, sizeof(compat_status64)))
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
	if (!data)
		return -ENOMEM;

	/* only fifo_size (RO from userspace) is different, so just copy all */
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
	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;

	if ((ch = substream->runtime->channels) > 128)
		return -EINVAL;
	if (get_user(buf, &data32->bufs) ||
	    get_user(frames, &data32->frames))
		return -EFAULT;
	bufptr = compat_ptr(buf);
	bufs = kmalloc_array(ch, sizeof(void __user *), GFP_KERNEL);
	if (bufs == NULL)
		return -ENOMEM;
	for (i = 0; i < ch; i++) {
		u32 ptr;
		if (get_user(ptr, bufptr)) {
			kfree(bufs);
			return -EFAULT;
		}
		bufs[i] = compat_ptr(ptr);
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

#ifdef CONFIG_X86_X32
/* X32 ABI has 64bit timespec and 64bit alignment */
struct snd_pcm_mmap_status_x32 {
	snd_pcm_state_t state;
	s32 pad1;
	u32 hw_ptr;
	u32 pad2; /* alignment */
	s64 tstamp_sec;
	s64 tstamp_nsec;
	snd_pcm_state_t suspended_state;
	s32 pad3;
	s64 audio_tstamp_sec;
	s64 audio_tstamp_nsec;
} __packed;

struct snd_pcm_mmap_control_x32 {
	u32 appl_ptr;
	u32 avail_min;
};

struct snd_pcm_sync_ptr_x32 {
	u32 flags;
	u32 rsvd; /* alignment */
	union {
		struct snd_pcm_mmap_status_x32 status;
		unsigned char reserved[64];
	} s;
	union {
		struct snd_pcm_mmap_control_x32 control;
		unsigned char reserved[64];
	} c;
} __packed;

static int snd_pcm_ioctl_sync_ptr_x32(struct snd_pcm_substream *substream,
				      struct snd_pcm_sync_ptr_x32 __user *src)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	volatile struct snd_pcm_mmap_status *status;
	volatile struct snd_pcm_mmap_control *control;
	u32 sflags;
	struct snd_pcm_mmap_control scontrol;
	struct snd_pcm_mmap_status sstatus;
	snd_pcm_uframes_t boundary;
	int err;

	if (snd_BUG_ON(!runtime))
		return -EINVAL;

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
	if (!boundary)
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
	sstatus.audio_tstamp = status->audio_tstamp;
	snd_pcm_stream_unlock_irq(substream);
	if (put_user(sstatus.state, &src->s.status.state) ||
	    put_user(sstatus.hw_ptr, &src->s.status.hw_ptr) ||
	    put_user(sstatus.tstamp.tv_sec, &src->s.status.tstamp_sec) ||
	    put_user(sstatus.tstamp.tv_nsec, &src->s.status.tstamp_nsec) ||
	    put_user(sstatus.suspended_state, &src->s.status.suspended_state) ||
	    put_user(sstatus.audio_tstamp.tv_sec, &src->s.status.audio_tstamp_sec) ||
	    put_user(sstatus.audio_tstamp.tv_nsec, &src->s.status.audio_tstamp_nsec) ||
	    put_user(scontrol.appl_ptr, &src->c.control.appl_ptr) ||
	    put_user(scontrol.avail_min, &src->c.control.avail_min))
		return -EFAULT;

	return 0;
}
#endif /* CONFIG_X86_X32 */

/*
 */
enum {
	SNDRV_PCM_IOCTL_HW_REFINE32 = _IOWR('A', 0x10, struct snd_pcm_hw_params32),
	SNDRV_PCM_IOCTL_HW_PARAMS32 = _IOWR('A', 0x11, struct snd_pcm_hw_params32),
	SNDRV_PCM_IOCTL_SW_PARAMS32 = _IOWR('A', 0x13, struct snd_pcm_sw_params32),
	SNDRV_PCM_IOCTL_STATUS_COMPAT32 = _IOR('A', 0x20, struct snd_pcm_status32),
	SNDRV_PCM_IOCTL_STATUS_EXT_COMPAT32 = _IOWR('A', 0x24, struct snd_pcm_status32),
	SNDRV_PCM_IOCTL_DELAY32 = _IOR('A', 0x21, s32),
	SNDRV_PCM_IOCTL_CHANNEL_INFO32 = _IOR('A', 0x32, struct snd_pcm_channel_info32),
	SNDRV_PCM_IOCTL_REWIND32 = _IOW('A', 0x46, u32),
	SNDRV_PCM_IOCTL_FORWARD32 = _IOW('A', 0x49, u32),
	SNDRV_PCM_IOCTL_WRITEI_FRAMES32 = _IOW('A', 0x50, struct snd_xferi32),
	SNDRV_PCM_IOCTL_READI_FRAMES32 = _IOR('A', 0x51, struct snd_xferi32),
	SNDRV_PCM_IOCTL_WRITEN_FRAMES32 = _IOW('A', 0x52, struct snd_xfern32),
	SNDRV_PCM_IOCTL_READN_FRAMES32 = _IOR('A', 0x53, struct snd_xfern32),
	SNDRV_PCM_IOCTL_STATUS_COMPAT64 = _IOR('A', 0x20, struct compat_snd_pcm_status64),
	SNDRV_PCM_IOCTL_STATUS_EXT_COMPAT64 = _IOWR('A', 0x24, struct compat_snd_pcm_status64),
#ifdef CONFIG_X86_X32
	SNDRV_PCM_IOCTL_CHANNEL_INFO_X32 = _IOR('A', 0x32, struct snd_pcm_channel_info),
	SNDRV_PCM_IOCTL_SYNC_PTR_X32 = _IOWR('A', 0x23, struct snd_pcm_sync_ptr_x32),
#endif /* CONFIG_X86_X32 */
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
	 * mmap of the old PCM status/control records because
	 * of the size incompatibility.
	 */
	pcm_file->no_compat_mmap = 1;

	switch (cmd) {
	case SNDRV_PCM_IOCTL_PVERSION:
	case SNDRV_PCM_IOCTL_INFO:
	case SNDRV_PCM_IOCTL_TSTAMP:
	case SNDRV_PCM_IOCTL_TTSTAMP:
	case SNDRV_PCM_IOCTL_USER_PVERSION:
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
	case __SNDRV_PCM_IOCTL_SYNC_PTR32:
		return snd_pcm_common_ioctl(file, substream, cmd, argp);
	case __SNDRV_PCM_IOCTL_SYNC_PTR64:
#ifdef CONFIG_X86_X32
		if (in_x32_syscall())
			return snd_pcm_ioctl_sync_ptr_x32(substream, argp);
#endif /* CONFIG_X86_X32 */
		return snd_pcm_common_ioctl(file, substream, cmd, argp);
	case SNDRV_PCM_IOCTL_HW_REFINE32:
		return snd_pcm_ioctl_hw_params_compat(substream, 1, argp);
	case SNDRV_PCM_IOCTL_HW_PARAMS32:
		return snd_pcm_ioctl_hw_params_compat(substream, 0, argp);
	case SNDRV_PCM_IOCTL_SW_PARAMS32:
		return snd_pcm_ioctl_sw_params_compat(substream, argp);
	case SNDRV_PCM_IOCTL_STATUS_COMPAT32:
		return snd_pcm_status_user32(substream, argp, false);
	case SNDRV_PCM_IOCTL_STATUS_EXT_COMPAT32:
		return snd_pcm_status_user32(substream, argp, true);
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
	case SNDRV_PCM_IOCTL_STATUS_COMPAT64:
		return snd_pcm_status_user_compat64(substream, argp, false);
	case SNDRV_PCM_IOCTL_STATUS_EXT_COMPAT64:
		return snd_pcm_status_user_compat64(substream, argp, true);
#ifdef CONFIG_X86_X32
	case SNDRV_PCM_IOCTL_CHANNEL_INFO_X32:
		return snd_pcm_ioctl_channel_info_x32(substream, argp);
#endif /* CONFIG_X86_X32 */
	}

	return -ENOIOCTLCMD;
}
