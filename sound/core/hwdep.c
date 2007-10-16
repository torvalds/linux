/*
 *  Hardware dependent layer
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
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

#include <sound/driver.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/minors.h>
#include <sound/hwdep.h>
#include <sound/info.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Hardware dependent layer");
MODULE_LICENSE("GPL");

static LIST_HEAD(snd_hwdep_devices);
static DEFINE_MUTEX(register_mutex);

static int snd_hwdep_free(struct snd_hwdep *hwdep);
static int snd_hwdep_dev_free(struct snd_device *device);
static int snd_hwdep_dev_register(struct snd_device *device);
static int snd_hwdep_dev_disconnect(struct snd_device *device);


static struct snd_hwdep *snd_hwdep_search(struct snd_card *card, int device)
{
	struct snd_hwdep *hwdep;

	list_for_each_entry(hwdep, &snd_hwdep_devices, list)
		if (hwdep->card == card && hwdep->device == device)
			return hwdep;
	return NULL;
}

static loff_t snd_hwdep_llseek(struct file * file, loff_t offset, int orig)
{
	struct snd_hwdep *hw = file->private_data;
	if (hw->ops.llseek)
		return hw->ops.llseek(hw, file, offset, orig);
	return -ENXIO;
}

static ssize_t snd_hwdep_read(struct file * file, char __user *buf,
			      size_t count, loff_t *offset)
{
	struct snd_hwdep *hw = file->private_data;
	if (hw->ops.read)
		return hw->ops.read(hw, buf, count, offset);
	return -ENXIO;	
}

static ssize_t snd_hwdep_write(struct file * file, const char __user *buf,
			       size_t count, loff_t *offset)
{
	struct snd_hwdep *hw = file->private_data;
	if (hw->ops.write)
		return hw->ops.write(hw, buf, count, offset);
	return -ENXIO;	
}

static int snd_hwdep_open(struct inode *inode, struct file * file)
{
	int major = imajor(inode);
	struct snd_hwdep *hw;
	int err;
	wait_queue_t wait;

	if (major == snd_major) {
		hw = snd_lookup_minor_data(iminor(inode),
					   SNDRV_DEVICE_TYPE_HWDEP);
#ifdef CONFIG_SND_OSSEMUL
	} else if (major == SOUND_MAJOR) {
		hw = snd_lookup_oss_minor_data(iminor(inode),
					       SNDRV_OSS_DEVICE_TYPE_DMFM);
#endif
	} else
		return -ENXIO;
	if (hw == NULL)
		return -ENODEV;

	if (!hw->ops.open)
		return -ENXIO;

	if (!try_module_get(hw->card->module))
		return -EFAULT;

	init_waitqueue_entry(&wait, current);
	add_wait_queue(&hw->open_wait, &wait);
	mutex_lock(&hw->open_mutex);
	while (1) {
		if (hw->exclusive && hw->used > 0) {
			err = -EBUSY;
			break;
		}
		err = hw->ops.open(hw, file);
		if (err >= 0)
			break;
		if (err == -EAGAIN) {
			if (file->f_flags & O_NONBLOCK) {
				err = -EBUSY;
				break;
			}
		} else
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&hw->open_mutex);
		schedule();
		mutex_lock(&hw->open_mutex);
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}
	}
	remove_wait_queue(&hw->open_wait, &wait);
	if (err >= 0) {
		err = snd_card_file_add(hw->card, file);
		if (err >= 0) {
			file->private_data = hw;
			hw->used++;
		} else {
			if (hw->ops.release)
				hw->ops.release(hw, file);
		}
	}
	mutex_unlock(&hw->open_mutex);
	if (err < 0)
		module_put(hw->card->module);
	return err;
}

static int snd_hwdep_release(struct inode *inode, struct file * file)
{
	int err = -ENXIO;
	struct snd_hwdep *hw = file->private_data;
	struct module *mod = hw->card->module;

	mutex_lock(&hw->open_mutex);
	if (hw->ops.release)
		err = hw->ops.release(hw, file);
	if (hw->used > 0)
		hw->used--;
	mutex_unlock(&hw->open_mutex);
	wake_up(&hw->open_wait);

	snd_card_file_remove(hw->card, file);
	module_put(mod);
	return err;
}

static unsigned int snd_hwdep_poll(struct file * file, poll_table * wait)
{
	struct snd_hwdep *hw = file->private_data;
	if (hw->ops.poll)
		return hw->ops.poll(hw, file, wait);
	return 0;
}

static int snd_hwdep_info(struct snd_hwdep *hw,
			  struct snd_hwdep_info __user *_info)
{
	struct snd_hwdep_info info;
	
	memset(&info, 0, sizeof(info));
	info.card = hw->card->number;
	strlcpy(info.id, hw->id, sizeof(info.id));	
	strlcpy(info.name, hw->name, sizeof(info.name));
	info.iface = hw->iface;
	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static int snd_hwdep_dsp_status(struct snd_hwdep *hw,
				struct snd_hwdep_dsp_status __user *_info)
{
	struct snd_hwdep_dsp_status info;
	int err;
	
	if (! hw->ops.dsp_status)
		return -ENXIO;
	memset(&info, 0, sizeof(info));
	info.dsp_loaded = hw->dsp_loaded;
	if ((err = hw->ops.dsp_status(hw, &info)) < 0)
		return err;
	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static int snd_hwdep_dsp_load(struct snd_hwdep *hw,
			      struct snd_hwdep_dsp_image __user *_info)
{
	struct snd_hwdep_dsp_image info;
	int err;
	
	if (! hw->ops.dsp_load)
		return -ENXIO;
	memset(&info, 0, sizeof(info));
	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;
	/* check whether the dsp was already loaded */
	if (hw->dsp_loaded & (1 << info.index))
		return -EBUSY;
	if (!access_ok(VERIFY_READ, info.image, info.length))
		return -EFAULT;
	err = hw->ops.dsp_load(hw, &info);
	if (err < 0)
		return err;
	hw->dsp_loaded |= (1 << info.index);
	return 0;
}

static long snd_hwdep_ioctl(struct file * file, unsigned int cmd,
			    unsigned long arg)
{
	struct snd_hwdep *hw = file->private_data;
	void __user *argp = (void __user *)arg;
	switch (cmd) {
	case SNDRV_HWDEP_IOCTL_PVERSION:
		return put_user(SNDRV_HWDEP_VERSION, (int __user *)argp);
	case SNDRV_HWDEP_IOCTL_INFO:
		return snd_hwdep_info(hw, argp);
	case SNDRV_HWDEP_IOCTL_DSP_STATUS:
		return snd_hwdep_dsp_status(hw, argp);
	case SNDRV_HWDEP_IOCTL_DSP_LOAD:
		return snd_hwdep_dsp_load(hw, argp);
	}
	if (hw->ops.ioctl)
		return hw->ops.ioctl(hw, file, cmd, arg);
	return -ENOTTY;
}

static int snd_hwdep_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct snd_hwdep *hw = file->private_data;
	if (hw->ops.mmap)
		return hw->ops.mmap(hw, file, vma);
	return -ENXIO;
}

static int snd_hwdep_control_ioctl(struct snd_card *card,
				   struct snd_ctl_file * control,
				   unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case SNDRV_CTL_IOCTL_HWDEP_NEXT_DEVICE:
		{
			int device;

			if (get_user(device, (int __user *)arg))
				return -EFAULT;
			mutex_lock(&register_mutex);
			device = device < 0 ? 0 : device + 1;
			while (device < SNDRV_MINOR_HWDEPS) {
				if (snd_hwdep_search(card, device))
					break;
				device++;
			}
			if (device >= SNDRV_MINOR_HWDEPS)
				device = -1;
			mutex_unlock(&register_mutex);
			if (put_user(device, (int __user *)arg))
				return -EFAULT;
			return 0;
		}
	case SNDRV_CTL_IOCTL_HWDEP_INFO:
		{
			struct snd_hwdep_info __user *info = (struct snd_hwdep_info __user *)arg;
			int device, err;
			struct snd_hwdep *hwdep;

			if (get_user(device, &info->device))
				return -EFAULT;
			mutex_lock(&register_mutex);
			hwdep = snd_hwdep_search(card, device);
			if (hwdep)
				err = snd_hwdep_info(hwdep, info);
			else
				err = -ENXIO;
			mutex_unlock(&register_mutex);
			return err;
		}
	}
	return -ENOIOCTLCMD;
}

#ifdef CONFIG_COMPAT
#include "hwdep_compat.c"
#else
#define snd_hwdep_ioctl_compat	NULL
#endif

/*

 */

static const struct file_operations snd_hwdep_f_ops =
{
	.owner = 	THIS_MODULE,
	.llseek =	snd_hwdep_llseek,
	.read = 	snd_hwdep_read,
	.write =	snd_hwdep_write,
	.open =		snd_hwdep_open,
	.release =	snd_hwdep_release,
	.poll =		snd_hwdep_poll,
	.unlocked_ioctl =	snd_hwdep_ioctl,
	.compat_ioctl =	snd_hwdep_ioctl_compat,
	.mmap =		snd_hwdep_mmap,
};

/**
 * snd_hwdep_new - create a new hwdep instance
 * @card: the card instance
 * @id: the id string
 * @device: the device index (zero-based)
 * @rhwdep: the pointer to store the new hwdep instance
 *
 * Creates a new hwdep instance with the given index on the card.
 * The callbacks (hwdep->ops) must be set on the returned instance
 * after this call manually by the caller.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_hwdep_new(struct snd_card *card, char *id, int device,
		  struct snd_hwdep **rhwdep)
{
	struct snd_hwdep *hwdep;
	int err;
	static struct snd_device_ops ops = {
		.dev_free = snd_hwdep_dev_free,
		.dev_register = snd_hwdep_dev_register,
		.dev_disconnect = snd_hwdep_dev_disconnect,
	};

	snd_assert(rhwdep != NULL, return -EINVAL);
	*rhwdep = NULL;
	snd_assert(card != NULL, return -ENXIO);
	hwdep = kzalloc(sizeof(*hwdep), GFP_KERNEL);
	if (hwdep == NULL) {
		snd_printk(KERN_ERR "hwdep: cannot allocate\n");
		return -ENOMEM;
	}
	hwdep->card = card;
	hwdep->device = device;
	if (id)
		strlcpy(hwdep->id, id, sizeof(hwdep->id));
#ifdef CONFIG_SND_OSSEMUL
	hwdep->oss_type = -1;
#endif
	if ((err = snd_device_new(card, SNDRV_DEV_HWDEP, hwdep, &ops)) < 0) {
		snd_hwdep_free(hwdep);
		return err;
	}
	init_waitqueue_head(&hwdep->open_wait);
	mutex_init(&hwdep->open_mutex);
	*rhwdep = hwdep;
	return 0;
}

static int snd_hwdep_free(struct snd_hwdep *hwdep)
{
	snd_assert(hwdep != NULL, return -ENXIO);
	if (hwdep->private_free)
		hwdep->private_free(hwdep);
	kfree(hwdep);
	return 0;
}

static int snd_hwdep_dev_free(struct snd_device *device)
{
	struct snd_hwdep *hwdep = device->device_data;
	return snd_hwdep_free(hwdep);
}

static int snd_hwdep_dev_register(struct snd_device *device)
{
	struct snd_hwdep *hwdep = device->device_data;
	int err;
	char name[32];

	mutex_lock(&register_mutex);
	if (snd_hwdep_search(hwdep->card, hwdep->device)) {
		mutex_unlock(&register_mutex);
		return -EBUSY;
	}
	list_add_tail(&hwdep->list, &snd_hwdep_devices);
	sprintf(name, "hwC%iD%i", hwdep->card->number, hwdep->device);
	if ((err = snd_register_device(SNDRV_DEVICE_TYPE_HWDEP,
				       hwdep->card, hwdep->device,
				       &snd_hwdep_f_ops, hwdep, name)) < 0) {
		snd_printk(KERN_ERR "unable to register hardware dependent device %i:%i\n",
			   hwdep->card->number, hwdep->device);
		list_del(&hwdep->list);
		mutex_unlock(&register_mutex);
		return err;
	}
#ifdef CONFIG_SND_OSSEMUL
	hwdep->ossreg = 0;
	if (hwdep->oss_type >= 0) {
		if ((hwdep->oss_type == SNDRV_OSS_DEVICE_TYPE_DMFM) && (hwdep->device != 0)) {
			snd_printk (KERN_WARNING "only hwdep device 0 can be registered as OSS direct FM device!\n");
		} else {
			if (snd_register_oss_device(hwdep->oss_type,
						    hwdep->card, hwdep->device,
						    &snd_hwdep_f_ops, hwdep,
						    hwdep->oss_dev) < 0) {
				snd_printk(KERN_ERR "unable to register OSS compatibility device %i:%i\n",
					   hwdep->card->number, hwdep->device);
			} else
				hwdep->ossreg = 1;
		}
	}
#endif
	mutex_unlock(&register_mutex);
	return 0;
}

static int snd_hwdep_dev_disconnect(struct snd_device *device)
{
	struct snd_hwdep *hwdep = device->device_data;

	snd_assert(hwdep != NULL, return -ENXIO);
	mutex_lock(&register_mutex);
	if (snd_hwdep_search(hwdep->card, hwdep->device) != hwdep) {
		mutex_unlock(&register_mutex);
		return -EINVAL;
	}
#ifdef CONFIG_SND_OSSEMUL
	if (hwdep->ossreg)
		snd_unregister_oss_device(hwdep->oss_type, hwdep->card, hwdep->device);
#endif
	snd_unregister_device(SNDRV_DEVICE_TYPE_HWDEP, hwdep->card, hwdep->device);
	list_del_init(&hwdep->list);
	mutex_unlock(&register_mutex);
	return 0;
}

#ifdef CONFIG_PROC_FS
/*
 *  Info interface
 */

static void snd_hwdep_proc_read(struct snd_info_entry *entry,
				struct snd_info_buffer *buffer)
{
	struct snd_hwdep *hwdep;

	mutex_lock(&register_mutex);
	list_for_each_entry(hwdep, &snd_hwdep_devices, list)
		snd_iprintf(buffer, "%02i-%02i: %s\n",
			    hwdep->card->number, hwdep->device, hwdep->name);
	mutex_unlock(&register_mutex);
}

static struct snd_info_entry *snd_hwdep_proc_entry;

static void __init snd_hwdep_proc_init(void)
{
	struct snd_info_entry *entry;

	if ((entry = snd_info_create_module_entry(THIS_MODULE, "hwdep", NULL)) != NULL) {
		entry->c.text.read = snd_hwdep_proc_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	snd_hwdep_proc_entry = entry;
}

static void __exit snd_hwdep_proc_done(void)
{
	snd_info_free_entry(snd_hwdep_proc_entry);
}
#else /* !CONFIG_PROC_FS */
#define snd_hwdep_proc_init()
#define snd_hwdep_proc_done()
#endif /* CONFIG_PROC_FS */


/*
 *  ENTRY functions
 */

static int __init alsa_hwdep_init(void)
{
	snd_hwdep_proc_init();
	snd_ctl_register_ioctl(snd_hwdep_control_ioctl);
	snd_ctl_register_ioctl_compat(snd_hwdep_control_ioctl);
	return 0;
}

static void __exit alsa_hwdep_exit(void)
{
	snd_ctl_unregister_ioctl(snd_hwdep_control_ioctl);
	snd_ctl_unregister_ioctl_compat(snd_hwdep_control_ioctl);
	snd_hwdep_proc_done();
}

module_init(alsa_hwdep_init)
module_exit(alsa_hwdep_exit)

EXPORT_SYMBOL(snd_hwdep_new);
