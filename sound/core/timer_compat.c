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

struct sndrv_timer_info32 {
	u32 flags;
	s32 card;
	unsigned char id[64];
	unsigned char name[80];
	u32 reserved0;
	u32 resolution;
	unsigned char reserved[64];
};

static int snd_timer_user_info_compat(struct file *file,
				      struct sndrv_timer_info32 __user *_info)
{
	snd_timer_user_t *tu;
	struct sndrv_timer_info32 info;
	snd_timer_t *t;

	tu = file->private_data;
	snd_assert(tu->timeri != NULL, return -ENXIO);
	t = tu->timeri->timer;
	snd_assert(t != NULL, return -ENXIO);
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

struct sndrv_timer_status32 {
	struct compat_timespec tstamp;
	u32 resolution;
	u32 lost;
	u32 overrun;
	u32 queue;
	unsigned char reserved[64];
};

static int snd_timer_user_status_compat(struct file *file,
					struct sndrv_timer_status32 __user *_status)
{
	snd_timer_user_t *tu;
	snd_timer_status_t status;
	
	tu = file->private_data;
	snd_assert(tu->timeri != NULL, return -ENXIO);
	memset(&status, 0, sizeof(status));
	status.tstamp = tu->tstamp;
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

/*
 */

enum {
	SNDRV_TIMER_IOCTL_INFO32 = _IOR('T', 0x11, struct sndrv_timer_info32),
	SNDRV_TIMER_IOCTL_STATUS32 = _IOW('T', 0x14, struct sndrv_timer_status32),
};

static long snd_timer_user_ioctl_compat(struct file *file, unsigned int cmd, unsigned long arg)
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
		return snd_timer_user_ioctl(file, cmd, (unsigned long)argp);
	case SNDRV_TIMER_IOCTL_INFO32:
		return snd_timer_user_info_compat(file, argp);
	case SNDRV_TIMER_IOCTL_STATUS32:
		return snd_timer_user_status_compat(file, argp);
	}
	return -ENOIOCTLCMD;
}
