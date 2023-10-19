// SPDX-License-Identifier: GPL-2.0-only
/*
 * dice_hwdep.c - a part of driver for DICE based devices
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * Copyright (c) 2014 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 */

#include "dice.h"

static long hwdep_read(struct snd_hwdep *hwdep, char __user *buf,
			    long count, loff_t *offset)
{
	struct snd_dice *dice = hwdep->private_data;
	DEFINE_WAIT(wait);
	union snd_firewire_event event;

	spin_lock_irq(&dice->lock);

	while (!dice->dev_lock_changed && dice->notification_bits == 0) {
		prepare_to_wait(&dice->hwdep_wait, &wait, TASK_INTERRUPTIBLE);
		spin_unlock_irq(&dice->lock);
		schedule();
		finish_wait(&dice->hwdep_wait, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		spin_lock_irq(&dice->lock);
	}

	memset(&event, 0, sizeof(event));
	if (dice->dev_lock_changed) {
		event.lock_status.type = SNDRV_FIREWIRE_EVENT_LOCK_STATUS;
		event.lock_status.status = dice->dev_lock_count > 0;
		dice->dev_lock_changed = false;

		count = min_t(long, count, sizeof(event.lock_status));
	} else {
		event.dice_notification.type =
					SNDRV_FIREWIRE_EVENT_DICE_NOTIFICATION;
		event.dice_notification.notification = dice->notification_bits;
		dice->notification_bits = 0;

		count = min_t(long, count, sizeof(event.dice_notification));
	}

	spin_unlock_irq(&dice->lock);

	if (copy_to_user(buf, &event, count))
		return -EFAULT;

	return count;
}

static __poll_t hwdep_poll(struct snd_hwdep *hwdep, struct file *file,
			       poll_table *wait)
{
	struct snd_dice *dice = hwdep->private_data;
	__poll_t events;

	poll_wait(file, &dice->hwdep_wait, wait);

	spin_lock_irq(&dice->lock);
	if (dice->dev_lock_changed || dice->notification_bits != 0)
		events = EPOLLIN | EPOLLRDNORM;
	else
		events = 0;
	spin_unlock_irq(&dice->lock);

	return events;
}

static int hwdep_get_info(struct snd_dice *dice, void __user *arg)
{
	struct fw_device *dev = fw_parent_device(dice->unit);
	struct snd_firewire_get_info info;

	memset(&info, 0, sizeof(info));
	info.type = SNDRV_FIREWIRE_TYPE_DICE;
	info.card = dev->card->index;
	*(__be32 *)&info.guid[0] = cpu_to_be32(dev->config_rom[3]);
	*(__be32 *)&info.guid[4] = cpu_to_be32(dev->config_rom[4]);
	strscpy(info.device_name, dev_name(&dev->device),
		sizeof(info.device_name));

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int hwdep_lock(struct snd_dice *dice)
{
	int err;

	spin_lock_irq(&dice->lock);

	if (dice->dev_lock_count == 0) {
		dice->dev_lock_count = -1;
		err = 0;
	} else {
		err = -EBUSY;
	}

	spin_unlock_irq(&dice->lock);

	return err;
}

static int hwdep_unlock(struct snd_dice *dice)
{
	int err;

	spin_lock_irq(&dice->lock);

	if (dice->dev_lock_count == -1) {
		dice->dev_lock_count = 0;
		err = 0;
	} else {
		err = -EBADFD;
	}

	spin_unlock_irq(&dice->lock);

	return err;
}

static int hwdep_release(struct snd_hwdep *hwdep, struct file *file)
{
	struct snd_dice *dice = hwdep->private_data;

	spin_lock_irq(&dice->lock);
	if (dice->dev_lock_count == -1)
		dice->dev_lock_count = 0;
	spin_unlock_irq(&dice->lock);

	return 0;
}

static int hwdep_ioctl(struct snd_hwdep *hwdep, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	struct snd_dice *dice = hwdep->private_data;

	switch (cmd) {
	case SNDRV_FIREWIRE_IOCTL_GET_INFO:
		return hwdep_get_info(dice, (void __user *)arg);
	case SNDRV_FIREWIRE_IOCTL_LOCK:
		return hwdep_lock(dice);
	case SNDRV_FIREWIRE_IOCTL_UNLOCK:
		return hwdep_unlock(dice);
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

int snd_dice_create_hwdep(struct snd_dice *dice)
{
	static const struct snd_hwdep_ops ops = {
		.read         = hwdep_read,
		.release      = hwdep_release,
		.poll         = hwdep_poll,
		.ioctl        = hwdep_ioctl,
		.ioctl_compat = hwdep_compat_ioctl,
	};
	struct snd_hwdep *hwdep;
	int err;

	err = snd_hwdep_new(dice->card, "DICE", 0, &hwdep);
	if (err < 0)
		return err;
	strcpy(hwdep->name, "DICE");
	hwdep->iface = SNDRV_HWDEP_IFACE_FW_DICE;
	hwdep->ops = ops;
	hwdep->private_data = dice;
	hwdep->exclusive = true;

	return 0;
}
