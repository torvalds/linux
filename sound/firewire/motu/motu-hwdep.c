// SPDX-License-Identifier: GPL-2.0-only
/*
 * motu-hwdep.c - a part of driver for MOTU FireWire series
 *
 * Copyright (c) 2015-2017 Takashi Sakamoto <o-takashi@sakamocchi.jp>
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

static bool has_dsp_event(struct snd_motu *motu)
{
	if (motu->spec->flags & SND_MOTU_SPEC_REGISTER_DSP)
		return (snd_motu_register_dsp_message_parser_count_event(motu) > 0);
	else
		return false;
}

static long hwdep_read(struct snd_hwdep *hwdep, char __user *buf, long count,
		       loff_t *offset)
{
	struct snd_motu *motu = hwdep->private_data;
	DEFINE_WAIT(wait);
	union snd_firewire_event event;

	spin_lock_irq(&motu->lock);

	while (!motu->dev_lock_changed && motu->msg == 0 && !has_dsp_event(motu)) {
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
		spin_unlock_irq(&motu->lock);

		count = min_t(long, count, sizeof(event));
		if (copy_to_user(buf, &event, count))
			return -EFAULT;
	} else if (motu->msg > 0) {
		event.motu_notification.type = SNDRV_FIREWIRE_EVENT_MOTU_NOTIFICATION;
		event.motu_notification.message = motu->msg;
		motu->msg = 0;
		spin_unlock_irq(&motu->lock);

		count = min_t(long, count, sizeof(event));
		if (copy_to_user(buf, &event, count))
			return -EFAULT;
	} else if (has_dsp_event(motu)) {
		size_t consumed = 0;
		u32 __user *ptr;
		u32 ev;

		spin_unlock_irq(&motu->lock);

		// Header is filled later.
		consumed += sizeof(event.motu_register_dsp_change);

		while (consumed < count &&
		       snd_motu_register_dsp_message_parser_copy_event(motu, &ev)) {
			ptr = (u32 __user *)(buf + consumed);
			if (put_user(ev, ptr))
				return -EFAULT;
			consumed += sizeof(ev);
		}

		event.motu_register_dsp_change.type = SNDRV_FIREWIRE_EVENT_MOTU_REGISTER_DSP_CHANGE;
		event.motu_register_dsp_change.count =
			(consumed - sizeof(event.motu_register_dsp_change)) / 4;
		if (copy_to_user(buf, &event, sizeof(event.motu_register_dsp_change)))
			return -EFAULT;

		count = consumed;
	} else {
		spin_unlock_irq(&motu->lock);

		count = 0;
	}

	return count;
}

static __poll_t hwdep_poll(struct snd_hwdep *hwdep, struct file *file,
			       poll_table *wait)
{
	struct snd_motu *motu = hwdep->private_data;

	poll_wait(file, &motu->hwdep_wait, wait);

	guard(spinlock_irq)(&motu->lock);
	if (motu->dev_lock_changed || motu->msg || has_dsp_event(motu))
		return EPOLLIN | EPOLLRDNORM;
	else
		return 0;
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
	strscpy(info.device_name, dev_name(&dev->device),
		sizeof(info.device_name));

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int hwdep_lock(struct snd_motu *motu)
{
	guard(spinlock_irq)(&motu->lock);

	if (motu->dev_lock_count == 0) {
		motu->dev_lock_count = -1;
		return 0;
	} else {
		return -EBUSY;
	}
}

static int hwdep_unlock(struct snd_motu *motu)
{
	guard(spinlock_irq)(&motu->lock);

	if (motu->dev_lock_count == -1) {
		motu->dev_lock_count = 0;
		return 0;
	} else {
		return -EBADFD;
	}
}

static int hwdep_release(struct snd_hwdep *hwdep, struct file *file)
{
	struct snd_motu *motu = hwdep->private_data;

	guard(spinlock_irq)(&motu->lock);
	if (motu->dev_lock_count == -1)
		motu->dev_lock_count = 0;

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
	case SNDRV_FIREWIRE_IOCTL_MOTU_REGISTER_DSP_METER:
	{
		struct snd_firewire_motu_register_dsp_meter *meter;
		int err;

		if (!(motu->spec->flags & SND_MOTU_SPEC_REGISTER_DSP))
			return -ENXIO;

		meter = kzalloc(sizeof(*meter), GFP_KERNEL);
		if (!meter)
			return -ENOMEM;

		snd_motu_register_dsp_message_parser_copy_meter(motu, meter);

		err = copy_to_user((void __user *)arg, meter, sizeof(*meter));
		kfree(meter);

		if (err)
			return -EFAULT;

		return 0;
	}
	case SNDRV_FIREWIRE_IOCTL_MOTU_COMMAND_DSP_METER:
	{
		struct snd_firewire_motu_command_dsp_meter *meter;
		int err;

		if (!(motu->spec->flags & SND_MOTU_SPEC_COMMAND_DSP))
			return -ENXIO;

		meter = kzalloc(sizeof(*meter), GFP_KERNEL);
		if (!meter)
			return -ENOMEM;

		snd_motu_command_dsp_message_parser_copy_meter(motu, meter);

		err = copy_to_user((void __user *)arg, meter, sizeof(*meter));
		kfree(meter);

		if (err)
			return -EFAULT;

		return 0;
	}
	case SNDRV_FIREWIRE_IOCTL_MOTU_REGISTER_DSP_PARAMETER:
	{
		struct snd_firewire_motu_register_dsp_parameter *param;
		int err;

		if (!(motu->spec->flags & SND_MOTU_SPEC_REGISTER_DSP))
			return -ENXIO;

		param = kzalloc(sizeof(*param), GFP_KERNEL);
		if (!param)
			return -ENOMEM;

		snd_motu_register_dsp_message_parser_copy_parameter(motu, param);

		err = copy_to_user((void __user *)arg, param, sizeof(*param));
		kfree(param);
		if (err)
			return -EFAULT;

		return 0;
	}
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

	strscpy(hwdep->name, "MOTU");
	hwdep->iface = SNDRV_HWDEP_IFACE_FW_MOTU;
	hwdep->ops = ops;
	hwdep->private_data = motu;
	hwdep->exclusive = true;

	motu->hwdep = hwdep;

	return 0;
}
