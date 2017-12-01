/*
 *   32bit -> 64bit ioctl wrapper for timer API
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

/* This file included from timer.c */

#include <linux/compat.h>

struct snd_timer_info32 {
	u32 flags;
	s32 card;
	unsigned char id[64];
	unsigned char name[80];
	u32 reserved0;
	u32 resolution;
	unsigned char reserved[64];
};

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
	strlcpy(info.id, t->id, sizeof(info.id));
	strlcpy(info.name, t->name, sizeof(info.name));
	info.resolution = t->hw.resolution;
	if (copy_to_user(_info, &info, sizeof(*_info)))
		return -EFAULT;
	return 0;
}

struct snd_timer_status32 {
	struct compat_timespec tstamp;
	u32 resolution;
	u32 lost;
	u32 overrun;
	u32 queue;
	unsigned char reserved[64];
};

static int snd_timer_user_status_compat(struct file *file,
					struct snd_timer_status32 __user *_status)
{
	struct snd_timer_user *tu;
	struct snd_timer_status32 status;
	
	tu = file->private_data;
	if (!tu->timeri)
		return -EBADFD;
	memset(&status, 0, sizeof(status));
	status.tstamp.tv_sec = tu->tstamp.tv_sec;
	status.tstamp.tv_nsec = tu->tstamp.tv_nsec;
	status.resolution = snd_timer_resolution(tu->timeri);
	status.lost = tu->timeri->lost;
	status.overrun = tu->overrun;
	spin_lock_irq(&tu->qlock);
	status.queue = tu->qused;
	spin_unlock_irq(&tu->qlock);
	if (copy_to_user(_status, &status, sizeof(status)))
		return -EFAULT;
	return 0;
}

#ifdef CONFIG_X86_X32
/* X32 ABI has the same struct as x86-64 */
#define snd_timer_user_status_x32(file, s) \
	snd_timer_user_status(file, s)
#endif /* CONFIG_X86_X32 */

/*
 */

enum {
	SNDRV_TIMER_IOCTL_INFO32 = _IOR('T', 0x11, struct snd_timer_info32),
	SNDRV_TIMER_IOCTL_STATUS32 = _IOW('T', 0x14, struct snd_timer_status32),
#ifdef CONFIG_X86_X32
	SNDRV_TIMER_IOCTL_STATUS_X32 = _IOW('T', 0x14, struct snd_timer_status),
#endif /* CONFIG_X86_X32 */
};

static long __snd_timer_user_ioctl_compat(struct file *file, unsigned int cmd,
					  unsigned long arg)
{
	void __user *argp = compat_ptr(arg);

	switch (cmd) {
	case SNDRV_TIMER_IOCTL_PVERSION:
	case SNDRV_TIMER_IOCTL_TREAD:
	case SNDRV_TIMER_IOCTL_GINFO:
	case SNDRV_TIMER_IOCTL_GPARAMS:
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
		return __snd_timer_user_ioctl(file, cmd, (unsigned long)argp);
	case SNDRV_TIMER_IOCTL_INFO32:
		return snd_timer_user_info_compat(file, argp);
	case SNDRV_TIMER_IOCTL_STATUS32:
		return snd_timer_user_status_compat(file, argp);
#ifdef CONFIG_X86_X32
	case SNDRV_TIMER_IOCTL_STATUS_X32:
		return snd_timer_user_status_x32(file, argp);
#endif /* CONFIG_X86_X32 */
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
