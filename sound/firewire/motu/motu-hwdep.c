/*
 * motu-hwdep.c - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

/*
 * This codes have five functionalities.
 *
 * 1.get information about firewire node
 * 2.get notification about starting/stopping stream
 * 3.lock/unlock streaming
 *
 */

#include "motu.h"

static long hwdep_read(struct snd_hwdep *hwdep, char __user *buf, long count,
		       loff_t *offset)
{
	struct snd_motu *motu = hwdep->private_data;
	DEFINE_WAIT(wait);
	union snd_firewire_event event;

	spin_lock_irq(&motu->lock);

	while (!motu->dev_lock_changed && motu->msg == 0) {
		prepare_to_wait(&motu->hwdep_wait, &wait, TASK_INTERRUPTIBLE);
		spin_unlock_irq(&motu->lock);
		schedule();
		finish_wait(&motu->hwdep_wait, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		spin_lock_irq(&motu->lock);
	}

	memset(&event, 0, sizeof(event));
	if (motu->dev_lock_changed) {
		event.lock_status.type = SNDRV_FIREWIRE_EVENT_LOCK_STATUS;
		event.lock_status.status = (motu->dev_lock_count > 0);
		motu->dev_lock_changed = false;

		count = min_t(long, count, sizeof(event.lock_status));
	} else {
		event.motu_notification.type = SNDRV_FIREWIRE_EVENT_MOTU_NOTIFICATION;
		event.motu_notification.message = motu->msg;
		motu->msg = 0;

		count = min_t(long, count, sizeof(event.motu_notification));
	}

	spin_unlock_irq(&motu->lock);

	if (copy_to_user(buf, &event, count))
		return -EFAULT;

	return count;
}

static __poll_t hwdep_poll(struct snd_hwdep *hwdep, struct file *file,
			       poll_table *wait)
{
	struct snd_motu *motu = hwdep->private_data;
	__poll_t events;

	poll_wait(file, &motu->hwdep_wait, wait);

	spin_lock_irq(&motu->lock);
	if (motu->dev_lock_changed || motu->msg)
		events = EPOLLIN | EPOLLRDNORM;
	else
		events = 0;
	spin_unlock_irq(&motu->lock);

	return events | EPOLLOUT;
}

static int hwdep_get_info(struct snd_motu *motu, void __user *arg)
{
	struct fw_device *dev = fw_parent_device(motu->unit);
	struct snd_firewire_get_info info;

	memset(&info, 0, sizeof(info));
	info.type = SNDRV_FIREWIRE_TYPE_MOTU;
	info.card = dev->card->index;
	*(__be32 *)&info.guid[0] = cpu_to_be32(dev->config_rom[3]);
	*(__be32 *)&info.guid[4] = cpu_to_be32(dev->config_rom[4]);
	strlcpy(info.device_name, dev_name(&dev->device),
		sizeof(info.device_name));

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int hwdep_lock(struct snd_motu *motu)
{
	int err;

	spin_lock_irq(&motu->lock);

	if (motu->dev_lock_count == 0) {
		motu->dev_lock_count = -1;
		err = 0;
	} else {
		err = -EBUSY;
	}

	spin_unlock_irq(&motu->lock);

	return err;
}

static int hwdep_unlock(struct snd_motu *motu)
{
	int err;

	spin_lock_irq(&motu->lock);

	if (motu->dev_lock_count == -1) {
		motu->dev_lock_count = 0;
		err = 0;
	} else {
		err = -EBADFD;
	}

	spin_unlock_irq(&motu->lock);

	return err;
}

static int hwdep_release(struct snd_hwdep *hwdep, struct file *file)
{
	struct snd_motu *motu = hwdep->private_data;

	spin_lock_irq(&motu->lock);
	if (motu->dev_lock_count == -1)
		motu->dev_lock_count = 0;
	spin_unlock_irq(&motu->lock);

	return 0;
}

static int hwdep_ioctl(struct snd_hwdep *hwdep, struct file *file,
	    unsigned int cmd, unsigned long arg)
{
	struct snd_motu *motu = hwdep->private_data;

	switch (cmd) {
	case SNDRV_FIREWIRE_IOCTL_GET_INFO:
		return hwdep_get_info(motu, (void __user *)arg);
	case SNDRV_FIREWIRE_IOCTL_LOCK:
		return hwdep_lock(motu);
	case SNDRV_FIREWIRE_IOCTL_UNLOCK:
		return hwdep_unlock(motu);
	default:
		return -ENOIOCTLCMD;
	}
}

#ifdef CONFIG_COMPAT
static int hwdep_compat_ioctl(struct snd_hwdep *hwdep, struct file *file,
			      unsigned int cmd, unsigned long arg)
{
	return hwdep_ioctl(hwdep, file, cmd,
			   (unsigned long)compat_ptr(arg));
}
#else
#define hwdep_compat_ioctl NULL
#endif

int snd_motu_create_hwdep_device(struct snd_motu *motu)
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

	err = snd_hwdep_new(motu->card, motu->card->driver, 0, &hwdep);
	if (err < 0)
		return err;

	strcpy(hwdep->name, "MOTU");
	hwdep->iface = SNDRV_HWDEP_IFACE_FW_MOTU;
	hwdep->ops = ops;
	hwdep->private_data = motu;
	hwdep->exclusive = true;

	return 0;
}
