// SPDX-License-Identifier: GPL-2.0-only
/*
 * bebob_hwdep.c - a part of driver for BeBoB based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 */

/*
 * This codes give three functionality.
 *
 * 1.get firewire node infomation
 * 2.get notification about starting/stopping stream
 * 3.lock/unlock stream
 */

#include "bebob.h"

static long
hwdep_read(struct snd_hwdep *hwdep, char __user *buf,  long count,
	   loff_t *offset)
{
	struct snd_bebob *bebob = hwdep->private_data;
	DEFINE_WAIT(wait);
	union snd_firewire_event event;

	spin_lock_irq(&bebob->lock);

	while (!bebob->dev_lock_changed) {
		prepare_to_wait(&bebob->hwdep_wait, &wait, TASK_INTERRUPTIBLE);
		spin_unlock_irq(&bebob->lock);
		schedule();
		finish_wait(&bebob->hwdep_wait, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		spin_lock_irq(&bebob->lock);
	}

	memset(&event, 0, sizeof(event));
	count = min_t(long, count, sizeof(event.lock_status));
	event.lock_status.type = SNDRV_FIREWIRE_EVENT_LOCK_STATUS;
	event.lock_status.status = (bebob->dev_lock_count > 0);
	bebob->dev_lock_changed = false;

	spin_unlock_irq(&bebob->lock);

	if (copy_to_user(buf, &event, count))
		return -EFAULT;

	return count;
}

static __poll_t
hwdep_poll(struct snd_hwdep *hwdep, struct file *file, poll_table *wait)
{
	struct snd_bebob *bebob = hwdep->private_data;

	poll_wait(file, &bebob->hwdep_wait, wait);

	guard(spinlock_irq)(&bebob->lock);
	if (bebob->dev_lock_changed)
		return EPOLLIN | EPOLLRDNORM;
	else
		return 0;
}

static int
hwdep_get_info(struct snd_bebob *bebob, void __user *arg)
{
	struct fw_device *dev = fw_parent_device(bebob->unit);
	struct snd_firewire_get_info info;

	memset(&info, 0, sizeof(info));
	info.type = SNDRV_FIREWIRE_TYPE_BEBOB;
	info.card = dev->card->index;
	*(__be32 *)&info.guid[0] = cpu_to_be32(dev->config_rom[3]);
	*(__be32 *)&info.guid[4] = cpu_to_be32(dev->config_rom[4]);
	strscpy(info.device_name, dev_name(&dev->device),
		sizeof(info.device_name));

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int
hwdep_lock(struct snd_bebob *bebob)
{
	guard(spinlock_irq)(&bebob->lock);

	if (bebob->dev_lock_count == 0) {
		bebob->dev_lock_count = -1;
		return 0;
	} else {
		return -EBUSY;
	}
}

static int
hwdep_unlock(struct snd_bebob *bebob)
{
	guard(spinlock_irq)(&bebob->lock);

	if (bebob->dev_lock_count == -1) {
		bebob->dev_lock_count = 0;
		return 0;
	} else {
		return -EBADFD;
	}
}

static int
hwdep_release(struct snd_hwdep *hwdep, struct file *file)
{
	struct snd_bebob *bebob = hwdep->private_data;

	guard(spinlock_irq)(&bebob->lock);
	if (bebob->dev_lock_count == -1)
		bebob->dev_lock_count = 0;

	return 0;
}

static int
hwdep_ioctl(struct snd_hwdep *hwdep, struct file *file,
	    unsigned int cmd, unsigned long arg)
{
	struct snd_bebob *bebob = hwdep->private_data;

	switch (cmd) {
	case SNDRV_FIREWIRE_IOCTL_GET_INFO:
		return hwdep_get_info(bebob, (void __user *)arg);
	case SNDRV_FIREWIRE_IOCTL_LOCK:
		return hwdep_lock(bebob);
	case SNDRV_FIREWIRE_IOCTL_UNLOCK:
		return hwdep_unlock(bebob);
	default:
		return -ENOIOCTLCMD;
	}
}

#ifdef CONFIG_COMPAT
static int
hwdep_compat_ioctl(struct snd_hwdep *hwdep, struct file *file,
		   unsigned int cmd, unsigned long arg)
{
	return hwdep_ioctl(hwdep, file, cmd,
			   (unsigned long)compat_ptr(arg));
}
#else
#define hwdep_compat_ioctl NULL
#endif

int snd_bebob_create_hwdep_device(struct snd_bebob *bebob)
{
	static const struct snd_hwdep_ops ops = {
		.read		= hwdep_read,
		.release	= hwdep_release,
		.poll		= hwdep_poll,
		.ioctl		= hwdep_ioctl,
		.ioctl_compat	= hwdep_compat_ioctl,
	};
	struct snd_hwdep *hwdep;
	int err;

	err = snd_hwdep_new(bebob->card, "BeBoB", 0, &hwdep);
	if (err < 0)
		goto end;
	strscpy(hwdep->name, "BeBoB");
	hwdep->iface = SNDRV_HWDEP_IFACE_FW_BEBOB;
	hwdep->ops = ops;
	hwdep->private_data = bebob;
	hwdep->exclusive = true;
end:
	return err;
}

