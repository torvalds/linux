/*
 * ff-hwdep.c - a part of driver for RME Fireface series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

/*
 * This codes give three functionality.
 *
 * 1.get firewire node information
 * 2.get notification about starting/stopping stream
 * 3.lock/unlock stream
 */

#include "ff.h"

static long hwdep_read(struct snd_hwdep *hwdep, char __user *buf,  long count,
		       loff_t *offset)
{
	struct snd_ff *ff = hwdep->private_data;
	DEFINE_WAIT(wait);
	union snd_firewire_event event;

	spin_lock_irq(&ff->lock);

	while (!ff->dev_lock_changed) {
		prepare_to_wait(&ff->hwdep_wait, &wait, TASK_INTERRUPTIBLE);
		spin_unlock_irq(&ff->lock);
		schedule();
		finish_wait(&ff->hwdep_wait, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		spin_lock_irq(&ff->lock);
	}

	memset(&event, 0, sizeof(event));
	if (ff->dev_lock_changed) {
		event.lock_status.type = SNDRV_FIREWIRE_EVENT_LOCK_STATUS;
		event.lock_status.status = (ff->dev_lock_count > 0);
		ff->dev_lock_changed = false;

		count = min_t(long, count, sizeof(event.lock_status));
	}

	spin_unlock_irq(&ff->lock);

	if (copy_to_user(buf, &event, count))
		return -EFAULT;

	return count;
}

static __poll_t hwdep_poll(struct snd_hwdep *hwdep, struct file *file,
			       poll_table *wait)
{
	struct snd_ff *ff = hwdep->private_data;
	__poll_t events;

	poll_wait(file, &ff->hwdep_wait, wait);

	spin_lock_irq(&ff->lock);
	if (ff->dev_lock_changed)
		events = EPOLLIN | EPOLLRDNORM;
	else
		events = 0;
	spin_unlock_irq(&ff->lock);

	return events;
}

static int hwdep_get_info(struct snd_ff *ff, void __user *arg)
{
	struct fw_device *dev = fw_parent_device(ff->unit);
	struct snd_firewire_get_info info;

	memset(&info, 0, sizeof(info));
	info.type = SNDRV_FIREWIRE_TYPE_FIREFACE;
	info.card = dev->card->index;
	*(__be32 *)&info.guid[0] = cpu_to_be32(dev->config_rom[3]);
	*(__be32 *)&info.guid[4] = cpu_to_be32(dev->config_rom[4]);
	strlcpy(info.device_name, dev_name(&dev->device),
		sizeof(info.device_name));

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int hwdep_lock(struct snd_ff *ff)
{
	int err;

	spin_lock_irq(&ff->lock);

	if (ff->dev_lock_count == 0) {
		ff->dev_lock_count = -1;
		err = 0;
	} else {
		err = -EBUSY;
	}

	spin_unlock_irq(&ff->lock);

	return err;
}

static int hwdep_unlock(struct snd_ff *ff)
{
	int err;

	spin_lock_irq(&ff->lock);

	if (ff->dev_lock_count == -1) {
		ff->dev_lock_count = 0;
		err = 0;
	} else {
		err = -EBADFD;
	}

	spin_unlock_irq(&ff->lock);

	return err;
}

static int hwdep_release(struct snd_hwdep *hwdep, struct file *file)
{
	struct snd_ff *ff = hwdep->private_data;

	spin_lock_irq(&ff->lock);
	if (ff->dev_lock_count == -1)
		ff->dev_lock_count = 0;
	spin_unlock_irq(&ff->lock);

	return 0;
}

static int hwdep_ioctl(struct snd_hwdep *hwdep, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	struct snd_ff *ff = hwdep->private_data;

	switch (cmd) {
	case SNDRV_FIREWIRE_IOCTL_GET_INFO:
		return hwdep_get_info(ff, (void __user *)arg);
	case SNDRV_FIREWIRE_IOCTL_LOCK:
		return hwdep_lock(ff);
	case SNDRV_FIREWIRE_IOCTL_UNLOCK:
		return hwdep_unlock(ff);
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

int snd_ff_create_hwdep_devices(struct snd_ff *ff)
{
	static const struct snd_hwdep_ops hwdep_ops = {
		.read		= hwdep_read,
		.release	= hwdep_release,
		.poll		= hwdep_poll,
		.ioctl		= hwdep_ioctl,
		.ioctl_compat	= hwdep_compat_ioctl,
	};
	struct snd_hwdep *hwdep;
	int err;

	err = snd_hwdep_new(ff->card, ff->card->driver, 0, &hwdep);
	if (err < 0)
		return err;

	strcpy(hwdep->name, ff->card->driver);
	hwdep->iface = SNDRV_HWDEP_IFACE_FW_FIREFACE;
	hwdep->ops = hwdep_ops;
	hwdep->private_data = ff;
	hwdep->exclusive = true;

	return 0;
}
