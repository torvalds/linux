// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   32bit -> 64bit ioctl wrapper for timer API
 *   Copyright (c) by Takashi Iwai <tiwai@suse.de>
 */

/* This file included from timer.c */

#include <linux/compat.h>

/*
 * ILP32/LP64 has different size for 'long' type. Additionally, the size
 * of storage alignment differs depending on architectures. Here, '__packed'
 * qualifier is used so that the size of this structure is multiple of 4 and
 * it fits to any architectures with 32 bit storage alignment.
 */
struct snd_timer_gparams32 {
	struct snd_timer_id tid;
	u32 period_num;
	u32 period_den;
	unsigned char reserved[32];
} __packed;

struct snd_timer_info32 {
	u32 flags;
	s32 card;
	unsigned char id[64];
	unsigned char name[80];
	u32 reserved0;
	u32 resolution;
	unsigned char reserved[64];
};

static int snd_timer_user_gparams_compat(struct file *file,
					struct snd_timer_gparams32 __user *user)
{
	struct snd_timer_gparams gparams;

	if (copy_from_user(&gparams.tid, &user->tid, sizeof(gparams.tid)) ||
	    get_user(gparams.period_num, &user->period_num) ||
	    get_user(gparams.period_den, &user->period_den))
		return -EFAULT;

	return timer_set_gparams(&gparams);
}

static int snd_timer_user_info_compat(struct file *file,
				      struct snd_timer_info32 __user *_info)
{
	struct snd_timer_user *tu;
	struct snd_timer_info32 info;
	struct snd_timer *t;

	tu = file->private_data;
	if (!tu->timeri)
		return -EBADFD;
	t = tu->timeri->timer;
	if (!t)
		return -EBADFD;
	memset(&info, 0, sizeof(info));
	info.card = t->card ? t->card->number : -1;
	if (t->hw.flags & SNDRV_TIMER_HW_SLAVE)
		info.flags |= SNDRV_TIMER_FLG_SLAVE;
	strscpy(info.id, t->id, sizeof(info.id));
	strscpy(info.name, t->name, sizeof(info.name));
	info.resolution = t->hw.resolution;
	if (copy_to_user(_info, &info, sizeof(*_info)))
		return -EFAULT;
	return 0;
}

enum {
	SNDRV_TIMER_IOCTL_GPARAMS32 = _IOW('T', 0x04, struct snd_timer_gparams32),
	SNDRV_TIMER_IOCTL_INFO32 = _IOR('T', 0x11, struct snd_timer_info32),
	SNDRV_TIMER_IOCTL_STATUS_COMPAT32 = _IOW('T', 0x14, struct snd_timer_status32),
	SNDRV_TIMER_IOCTL_STATUS_COMPAT64 = _IOW('T', 0x14, struct snd_timer_status64),
};

static long __snd_timer_user_ioctl_compat(struct file *file, unsigned int cmd,
					  unsigned long arg)
{
	void __user *argp = compat_ptr(arg);

	switch (cmd) {
	case SNDRV_TIMER_IOCTL_PVERSION:
	case SNDRV_TIMER_IOCTL_TREAD_OLD:
	case SNDRV_TIMER_IOCTL_TREAD64:
	case SNDRV_TIMER_IOCTL_GINFO:
	case SNDRV_TIMER_IOCTL_GSTATUS:
	case SNDRV_TIMER_IOCTL_SELECT:
	case SNDRV_TIMER_IOCTL_PARAMS:
	case SNDRV_TIMER_IOCTL_START:
	case SNDRV_TIMER_IOCTL_START_OLD:
	case SNDRV_TIMER_IOCTL_STOP:
	case SNDRV_TIMER_IOCTL_STOP_OLD:
	case SNDRV_TIMER_IOCTL_CONTINUE:
	case SNDRV_TIMER_IOCTL_CONTINUE_OLD:
	case SNDRV_TIMER_IOCTL_PAUSE:
	case SNDRV_TIMER_IOCTL_PAUSE_OLD:
	case SNDRV_TIMER_IOCTL_NEXT_DEVICE:
		return __snd_timer_user_ioctl(file, cmd, (unsigned long)argp, true);
	case SNDRV_TIMER_IOCTL_GPARAMS32:
		return snd_timer_user_gparams_compat(file, argp);
	case SNDRV_TIMER_IOCTL_INFO32:
		return snd_timer_user_info_compat(file, argp);
	case SNDRV_TIMER_IOCTL_STATUS_COMPAT32:
		return snd_timer_user_status32(file, argp);
	case SNDRV_TIMER_IOCTL_STATUS_COMPAT64:
		return snd_timer_user_status64(file, argp);
	}
	return -ENOIOCTLCMD;
}

static long snd_timer_user_ioctl_compat(struct file *file, unsigned int cmd,
					unsigned long arg)
{
	struct snd_timer_user *tu = file->private_data;
	long ret;

	mutex_lock(&tu->ioctl_lock);
	ret = __snd_timer_user_ioctl_compat(file, cmd, arg);
	mutex_unlock(&tu->ioctl_lock);
	return ret;
}
