// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   32bit -> 64bit ioctl wrapper for hwdep API
 *   Copyright (c) by Takashi Iwai <tiwai@suse.de>
 */

/* This file is included from hwdep.c */

#include <linux/compat.h>

struct snd_hwdep_dsp_image32 {
	u32 index;
	unsigned char name[64];
	u32 image;	/* pointer */
	u32 length;
	u32 driver_data;
} /* don't set packed attribute here */;

static int snd_hwdep_dsp_load_compat(struct snd_hwdep *hw,
				     struct snd_hwdep_dsp_image32 __user *src)
{
	struct snd_hwdep_dsp_image info = {};
	compat_caddr_t ptr;

	if (copy_from_user(&info, src, 4 + 64) ||
	    get_user(ptr, &src->image) ||
	    get_user(info.length, &src->length) ||
	    get_user(info.driver_data, &src->driver_data))
		return -EFAULT;
	info.image = compat_ptr(ptr);

	return snd_hwdep_dsp_load(hw, &info);
}

enum {
	SNDRV_HWDEP_IOCTL_DSP_LOAD32   = _IOW('H', 0x03, struct snd_hwdep_dsp_image32)
};

static long snd_hwdep_ioctl_compat(struct file * file, unsigned int cmd,
				   unsigned long arg)
{
	struct snd_hwdep *hw = file->private_data;
	void __user *argp = compat_ptr(arg);
	switch (cmd) {
	case SNDRV_HWDEP_IOCTL_PVERSION:
	case SNDRV_HWDEP_IOCTL_INFO:
	case SNDRV_HWDEP_IOCTL_DSP_STATUS:
		return snd_hwdep_ioctl(file, cmd, (unsigned long)argp);
	case SNDRV_HWDEP_IOCTL_DSP_LOAD32:
		return snd_hwdep_dsp_load_compat(hw, argp);
	}
	if (hw->ops.ioctl_compat)
		return hw->ops.ioctl_compat(hw, file, cmd, arg);
	return -ENOIOCTLCMD;
}
