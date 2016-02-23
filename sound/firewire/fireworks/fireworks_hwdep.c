/*
 * fireworks_hwdep.c - a part of driver for Fireworks based devices
 *
 * Copyright (c) 2013-2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

/*
 * This codes have five functionalities.
 *
 * 1.get information about firewire node
 * 2.get notification about starting/stopping stream
 * 3.lock/unlock streaming
 * 4.transmit command of EFW transaction
 * 5.receive response of EFW transaction
 *
 */

#include "fireworks.h"

static long
hwdep_read_resp_buf(struct snd_efw *efw, char __user *buf, long remained,
		    loff_t *offset)
{
	unsigned int length, till_end, type;
	struct snd_efw_transaction *t;
	long count = 0;

	if (remained < sizeof(type) + sizeof(struct snd_efw_transaction))
		return -ENOSPC;

	/* data type is SNDRV_FIREWIRE_EVENT_EFW_RESPONSE */
	type = SNDRV_FIREWIRE_EVENT_EFW_RESPONSE;
	if (copy_to_user(buf, &type, sizeof(type)))
		return -EFAULT;
	remained -= sizeof(type);
	buf += sizeof(type);

	/* write into buffer as many responses as possible */
	while (efw->resp_queues > 0) {
		t = (struct snd_efw_transaction *)(efw->pull_ptr);
		length = be32_to_cpu(t->length) * sizeof(__be32);

		/* confirm enough space for this response */
		if (remained < length)
			break;

		/* copy from ring buffer to user buffer */
		while (length > 0) {
			till_end = snd_efw_resp_buf_size -
				(unsigned int)(efw->pull_ptr - efw->resp_buf);
			till_end = min_t(unsigned int, length, till_end);

			if (copy_to_user(buf, efw->pull_ptr, till_end))
				return -EFAULT;

			efw->pull_ptr += till_end;
			if (efw->pull_ptr >= efw->resp_buf +
					     snd_efw_resp_buf_size)
				efw->pull_ptr -= snd_efw_resp_buf_size;

			length -= till_end;
			buf += till_end;
			count += till_end;
			remained -= till_end;
		}

		efw->resp_queues--;
	}

	return count;
}

static long
hwdep_read_locked(struct snd_efw *efw, char __user *buf, long count,
		  loff_t *offset)
{
	union snd_firewire_event event;

	memset(&event, 0, sizeof(event));

	event.lock_status.type = SNDRV_FIREWIRE_EVENT_LOCK_STATUS;
	event.lock_status.status = (efw->dev_lock_count > 0);
	efw->dev_lock_changed = false;

	count = min_t(long, count, sizeof(event.lock_status));

	if (copy_to_user(buf, &event, count))
		return -EFAULT;

	return count;
}

static long
hwdep_read(struct snd_hwdep *hwdep, char __user *buf, long count,
	   loff_t *offset)
{
	struct snd_efw *efw = hwdep->private_data;
	DEFINE_WAIT(wait);

	spin_lock_irq(&efw->lock);

	while ((!efw->dev_lock_changed) && (efw->resp_queues == 0)) {
		prepare_to_wait(&efw->hwdep_wait, &wait, TASK_INTERRUPTIBLE);
		spin_unlock_irq(&efw->lock);
		schedule();
		finish_wait(&efw->hwdep_wait, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		spin_lock_irq(&efw->lock);
	}

	if (efw->dev_lock_changed)
		count = hwdep_read_locked(efw, buf, count, offset);
	else if (efw->resp_queues > 0)
		count = hwdep_read_resp_buf(efw, buf, count, offset);

	spin_unlock_irq(&efw->lock);

	return count;
}

static long
hwdep_write(struct snd_hwdep *hwdep, const char __user *data, long count,
	    loff_t *offset)
{
	struct snd_efw *efw = hwdep->private_data;
	u32 seqnum;
	u8 *buf;

	if (count < sizeof(struct snd_efw_transaction) ||
	    SND_EFW_RESPONSE_MAXIMUM_BYTES < count)
		return -EINVAL;

	buf = memdup_user(data, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	/* check seqnum is not for kernel-land */
	seqnum = be32_to_cpu(((struct snd_efw_transaction *)buf)->seqnum);
	if (seqnum > SND_EFW_TRANSACTION_USER_SEQNUM_MAX) {
		count = -EINVAL;
		goto end;
	}

	if (snd_efw_transaction_cmd(efw->unit, buf, count) < 0)
		count = -EIO;
end:
	kfree(buf);
	return count;
}

static unsigned int
hwdep_poll(struct snd_hwdep *hwdep, struct file *file, poll_table *wait)
{
	struct snd_efw *efw = hwdep->private_data;
	unsigned int events;

	poll_wait(file, &efw->hwdep_wait, wait);

	spin_lock_irq(&efw->lock);
	if (efw->dev_lock_changed || (efw->resp_queues > 0))
		events = POLLIN | POLLRDNORM;
	else
		events = 0;
	spin_unlock_irq(&efw->lock);

	return events | POLLOUT;
}

static int
hwdep_get_info(struct snd_efw *efw, void __user *arg)
{
	struct fw_device *dev = fw_parent_device(efw->unit);
	struct snd_firewire_get_info info;

	memset(&info, 0, sizeof(info));
	info.type = SNDRV_FIREWIRE_TYPE_FIREWORKS;
	info.card = dev->card->index;
	*(__be32 *)&info.guid[0] = cpu_to_be32(dev->config_rom[3]);
	*(__be32 *)&info.guid[4] = cpu_to_be32(dev->config_rom[4]);
	strlcpy(info.device_name, dev_name(&dev->device),
		sizeof(info.device_name));

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int
hwdep_lock(struct snd_efw *efw)
{
	int err;

	spin_lock_irq(&efw->lock);

	if (efw->dev_lock_count == 0) {
		efw->dev_lock_count = -1;
		err = 0;
	} else {
		err = -EBUSY;
	}

	spin_unlock_irq(&efw->lock);

	return err;
}

static int
hwdep_unlock(struct snd_efw *efw)
{
	int err;

	spin_lock_irq(&efw->lock);

	if (efw->dev_lock_count == -1) {
		efw->dev_lock_count = 0;
		err = 0;
	} else {
		err = -EBADFD;
	}

	spin_unlock_irq(&efw->lock);

	return err;
}

static int
hwdep_release(struct snd_hwdep *hwdep, struct file *file)
{
	struct snd_efw *efw = hwdep->private_data;

	spin_lock_irq(&efw->lock);
	if (efw->dev_lock_count == -1)
		efw->dev_lock_count = 0;
	spin_unlock_irq(&efw->lock);

	return 0;
}

static int
hwdep_ioctl(struct snd_hwdep *hwdep, struct file *file,
	    unsigned int cmd, unsigned long arg)
{
	struct snd_efw *efw = hwdep->private_data;

	switch (cmd) {
	case SNDRV_FIREWIRE_IOCTL_GET_INFO:
		return hwdep_get_info(efw, (void __user *)arg);
	case SNDRV_FIREWIRE_IOCTL_LOCK:
		return hwdep_lock(efw);
	case SNDRV_FIREWIRE_IOCTL_UNLOCK:
		return hwdep_unlock(efw);
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

static const struct snd_hwdep_ops hwdep_ops = {
	.read		= hwdep_read,
	.write		= hwdep_write,
	.release	= hwdep_release,
	.poll		= hwdep_poll,
	.ioctl		= hwdep_ioctl,
	.ioctl_compat	= hwdep_compat_ioctl,
};

int snd_efw_create_hwdep_device(struct snd_efw *efw)
{
	struct snd_hwdep *hwdep;
	int err;

	err = snd_hwdep_new(efw->card, "Fireworks", 0, &hwdep);
	if (err < 0)
		goto end;
	strcpy(hwdep->name, "Fireworks");
	hwdep->iface = SNDRV_HWDEP_IFACE_FW_FIREWORKS;
	hwdep->ops = hwdep_ops;
	hwdep->private_data = efw;
	hwdep->exclusive = true;
end:
	return err;
}

