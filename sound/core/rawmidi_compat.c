// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   32bit -> 64bit ioctl wrapper for raw MIDI API
 *   Copyright (c) by Takashi Iwai <tiwai@suse.de>
 */

/* This file included from rawmidi.c */

#include <linux/compat.h>

struct snd_rawmidi_params32 {
	s32 stream;
	u32 buffer_size;
	u32 avail_min;
	unsigned int no_active_sensing; /* avoid bit-field */
	unsigned int mode;
	unsigned char reserved[12];
} __attribute__((packed));

static int snd_rawmidi_ioctl_params_compat(struct snd_rawmidi_file *rfile,
					   struct snd_rawmidi_params32 __user *src)
{
	struct snd_rawmidi_params params;
	unsigned int val;

	if (get_user(params.stream, &src->stream) ||
	    get_user(params.buffer_size, &src->buffer_size) ||
	    get_user(params.avail_min, &src->avail_min) ||
	    get_user(params.mode, &src->mode) ||
	    get_user(val, &src->no_active_sensing))
		return -EFAULT;
	params.no_active_sensing = val;
	switch (params.stream) {
	case SNDRV_RAWMIDI_STREAM_OUTPUT:
		if (!rfile->output)
			return -EINVAL;
		return snd_rawmidi_output_params(rfile->output, &params);
	case SNDRV_RAWMIDI_STREAM_INPUT:
		if (!rfile->input)
			return -EINVAL;
		return snd_rawmidi_input_params(rfile->input, &params);
	}
	return -EINVAL;
}

struct compat_snd_rawmidi_status64 {
	s32 stream;
	u8 rsvd[4]; /* alignment */
	s64 tstamp_sec;
	s64 tstamp_nsec;
	u32 avail;
	u32 xruns;
	unsigned char reserved[16];
} __attribute__((packed));

static int snd_rawmidi_ioctl_status_compat64(struct snd_rawmidi_file *rfile,
					     struct compat_snd_rawmidi_status64 __user *src)
{
	int err;
	struct snd_rawmidi_status64 status;
	struct compat_snd_rawmidi_status64 compat_status;

	if (get_user(status.stream, &src->stream))
		return -EFAULT;

	switch (status.stream) {
	case SNDRV_RAWMIDI_STREAM_OUTPUT:
		if (!rfile->output)
			return -EINVAL;
		err = snd_rawmidi_output_status(rfile->output, &status);
		break;
	case SNDRV_RAWMIDI_STREAM_INPUT:
		if (!rfile->input)
			return -EINVAL;
		err = snd_rawmidi_input_status(rfile->input, &status);
		break;
	default:
		return -EINVAL;
	}
	if (err < 0)
		return err;

	compat_status = (struct compat_snd_rawmidi_status64) {
		.stream = status.stream,
		.tstamp_sec = status.tstamp_sec,
		.tstamp_nsec = status.tstamp_nsec,
		.avail = status.avail,
		.xruns = status.xruns,
	};

	if (copy_to_user(src, &compat_status, sizeof(*src)))
		return -EFAULT;

	return 0;
}

enum {
	SNDRV_RAWMIDI_IOCTL_PARAMS32 = _IOWR('W', 0x10, struct snd_rawmidi_params32),
	SNDRV_RAWMIDI_IOCTL_STATUS_COMPAT32 = _IOWR('W', 0x20, struct snd_rawmidi_status32),
	SNDRV_RAWMIDI_IOCTL_STATUS_COMPAT64 = _IOWR('W', 0x20, struct compat_snd_rawmidi_status64),
};

static long snd_rawmidi_ioctl_compat(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct snd_rawmidi_file *rfile;
	void __user *argp = compat_ptr(arg);

	rfile = file->private_data;
	switch (cmd) {
	case SNDRV_RAWMIDI_IOCTL_PVERSION:
	case SNDRV_RAWMIDI_IOCTL_INFO:
	case SNDRV_RAWMIDI_IOCTL_DROP:
	case SNDRV_RAWMIDI_IOCTL_DRAIN:
		return snd_rawmidi_ioctl(file, cmd, (unsigned long)argp);
	case SNDRV_RAWMIDI_IOCTL_PARAMS32:
		return snd_rawmidi_ioctl_params_compat(rfile, argp);
	case SNDRV_RAWMIDI_IOCTL_STATUS_COMPAT32:
		return snd_rawmidi_ioctl_status32(rfile, argp);
	case SNDRV_RAWMIDI_IOCTL_STATUS_COMPAT64:
		return snd_rawmidi_ioctl_status_compat64(rfile, argp);
	}
	return -ENOIOCTLCMD;
}
