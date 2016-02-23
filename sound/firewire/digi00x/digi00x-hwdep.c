/*
 * digi00x-hwdep.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

/*
 * This codes give three functionality.
 *
 * 1.get firewire node information
 * 2.get notification about starting/stopping stream
 * 3.lock/unlock stream
 * 4.get asynchronous messaging
 */

#include "digi00x.h"

static long hwdep_read(struct snd_hwdep *hwdep, char __user *buf,  long count,
		       loff_t *offset)
{
	struct snd_dg00x *dg00x = hwdep->private_data;
	DEFINE_WAIT(wait);
	union snd_firewire_event event;

	spin_lock_irq(&dg00x->lock);

	while (!dg00x->dev_lock_changed && dg00x->msg == 0) {
		prepare_to_wait(&dg00x->hwdep_wait, &wait, TASK_INTERRUPTIBLE);
		spin_unlock_irq(&dg00x->lock);
		schedule();
		finish_wait(&dg00x->hwdep_wait, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		spin_lock_irq(&dg00x->lock);
	}

	memset(&event, 0, sizeof(event));
	if (dg00x->dev_lock_changed) {
		event.lock_status.type = SNDRV_FIREWIRE_EVENT_LOCK_STATUS;
		event.lock_status.status = (dg00x->dev_lock_count > 0);
		dg00x->dev_lock_changed = false;

		count = min_t(long, count, sizeof(event.lock_status));
	} else {
		event.digi00x_message.type =
					SNDRV_FIREWIRE_EVENT_DIGI00X_MESSAGE;
		event.digi00x_message.message = dg00x->msg;
		dg00x->msg = 0;

		count = min_t(long, count, sizeof(event.digi00x_message));
	}

	spin_unlock_irq(&dg00x->lock);

	if (copy_to_user(buf, &event, count))
		return -EFAULT;

	return count;
}

static unsigned int hwdep_poll(struct snd_hwdep *hwdep, struct file *file,
			       poll_table *wait)
{
	struct snd_dg00x *dg00x = hwdep->private_data;
	unsigned int events;

	poll_wait(file, &dg00x->hwdep_wait, wait);

	spin_lock_irq(&dg00x->lock);
	if (dg00x->dev_lock_changed || dg00x->msg)
		events = POLLIN | POLLRDNORM;
	else
		events = 0;
	spin_unlock_irq(&dg00x->lock);

	return events;
}

static int hwdep_get_info(struct snd_dg00x *dg00x, void __user *arg)
{
	struct fw_device *dev = fw_parent_device(dg00x->unit);
	struct snd_firewire_get_info info;

	memset(&info, 0, sizeof(info));
	info.type = SNDRV_FIREWIRE_TYPE_DIGI00X;
	info.card = dev->card->index;
	*(__be32 *)&info.guid[0] = cpu_to_be32(dev->config_rom[3]);
	*(__be32 *)&info.guid[4] = cpu_to_be32(dev->config_rom[4]);
	strlcpy(info.device_name, dev_name(&dev->device),
		sizeof(info.device_name));

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int hwdep_lock(struct snd_dg00x *dg00x)
{
	int err;

	spin_lock_irq(&dg00x->lock);

	if (dg00x->dev_lock_count == 0) {
		dg00x->dev_lock_count = -1;
		err = 0;
	} else {
		err = -EBUSY;
	}

	spin_unlock_irq(&dg00x->lock);

	return err;
}

static int hwdep_unlock(struct snd_dg00x *dg00x)
{
	int err;

	spin_lock_irq(&dg00x->lock);

	if (dg00x->dev_lock_count == -1) {
		dg00x->dev_lock_count = 0;
		err = 0;
	} else {
		err = -EBADFD;
	}

	spin_unlock_irq(&dg00x->lock);

	return err;
}

static int hwdep_release(struct snd_hwdep *hwdep, struct file *file)
{
	struct snd_dg00x *dg00x = hwdep->private_data;

	spin_lock_irq(&dg00x->lock);
	if (dg00x->dev_lock_count == -1)
		dg00x->dev_lock_count = 0;
	spin_unlock_irq(&dg00x->lock);

	return 0;
}

static int hwdep_ioctl(struct snd_hwdep *hwdep, struct file *file,
	    unsigned int cmd, unsigned long arg)
{
	struct snd_dg00x *dg00x = hwdep->private_data;

	switch (cmd) {
	case SNDRV_FIREWIRE_IOCTL_GET_INFO:
		return hwdep_get_info(dg00x, (void __user *)arg);
	case SNDRV_FIREWIRE_IOCTL_LOCK:
		return hwdep_lock(dg00x);
	case SNDRV_FIREWIRE_IOCTL_UNLOCK:
		return hwdep_unlock(dg00x);
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

static const struct snd_hwdep_ops hwdep_ops = {
	.read		= hwdep_read,
	.release	= hwdep_release,
	.poll		= hwdep_poll,
	.ioctl		= hwdep_ioctl,
	.ioctl_compat	= hwdep_compat_ioctl,
};

int snd_dg00x_create_hwdep_device(struct snd_dg00x *dg00x)
{
	struct snd_hwdep *hwdep;
	int err;

	err = snd_hwdep_new(dg00x->card, "Digi00x", 0, &hwdep);
	if (err < 0)
		return err;

	strcpy(hwdep->name, "Digi00x");
	hwdep->iface = SNDRV_HWDEP_IFACE_FW_DIGI00X;
	hwdep->ops = hwdep_ops;
	hwdep->private_data = dg00x;
	hwdep->exclusive = true;

	return err;
}
