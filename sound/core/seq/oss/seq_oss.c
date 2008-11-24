/*
 * OSS compatible sequencer driver
 *
 * registration of device and proc
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/initval.h>
#include "seq_oss_device.h"
#include "seq_oss_synth.h"

/*
 * module option
 */
MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("OSS-compatible sequencer module");
MODULE_LICENSE("GPL");
/* Takashi says this is really only for sound-service-0-, but this is OK. */
MODULE_ALIAS_SNDRV_MINOR(SNDRV_MINOR_OSS_SEQUENCER);
MODULE_ALIAS_SNDRV_MINOR(SNDRV_MINOR_OSS_MUSIC);

#ifdef SNDRV_SEQ_OSS_DEBUG
module_param(seq_oss_debug, int, 0644);
MODULE_PARM_DESC(seq_oss_debug, "debug option");
int seq_oss_debug = 0;
#endif


/*
 * prototypes
 */
static int register_device(void);
static void unregister_device(void);
#ifdef CONFIG_PROC_FS
static int register_proc(void);
static void unregister_proc(void);
#else
static inline int register_proc(void) { return 0; }
static inline void unregister_proc(void) {}
#endif

static int odev_open(struct inode *inode, struct file *file);
static int odev_release(struct inode *inode, struct file *file);
static ssize_t odev_read(struct file *file, char __user *buf, size_t count, loff_t *offset);
static ssize_t odev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset);
static long odev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static unsigned int odev_poll(struct file *file, poll_table * wait);


/*
 * module interface
 */

static int __init alsa_seq_oss_init(void)
{
	int rc;
	static struct snd_seq_dev_ops ops = {
		snd_seq_oss_synth_register,
		snd_seq_oss_synth_unregister,
	};

	snd_seq_autoload_lock();
	if ((rc = register_device()) < 0)
		goto error;
	if ((rc = register_proc()) < 0) {
		unregister_device();
		goto error;
	}
	if ((rc = snd_seq_oss_create_client()) < 0) {
		unregister_proc();
		unregister_device();
		goto error;
	}

	if ((rc = snd_seq_device_register_driver(SNDRV_SEQ_DEV_ID_OSS, &ops,
						 sizeof(struct snd_seq_oss_reg))) < 0) {
		snd_seq_oss_delete_client();
		unregister_proc();
		unregister_device();
		goto error;
	}

	/* success */
	snd_seq_oss_synth_init();

 error:
	snd_seq_autoload_unlock();
	return rc;
}

static void __exit alsa_seq_oss_exit(void)
{
	snd_seq_device_unregister_driver(SNDRV_SEQ_DEV_ID_OSS);
	snd_seq_oss_delete_client();
	unregister_proc();
	unregister_device();
}

module_init(alsa_seq_oss_init)
module_exit(alsa_seq_oss_exit)

/*
 * ALSA minor device interface
 */

static DEFINE_MUTEX(register_mutex);

static int
odev_open(struct inode *inode, struct file *file)
{
	int level, rc;

	if (iminor(inode) == SNDRV_MINOR_OSS_MUSIC)
		level = SNDRV_SEQ_OSS_MODE_MUSIC;
	else
		level = SNDRV_SEQ_OSS_MODE_SYNTH;

	mutex_lock(&register_mutex);
	rc = snd_seq_oss_open(file, level);
	mutex_unlock(&register_mutex);

	return rc;
}

static int
odev_release(struct inode *inode, struct file *file)
{
	struct seq_oss_devinfo *dp;

	if ((dp = file->private_data) == NULL)
		return 0;

	snd_seq_oss_drain_write(dp);

	mutex_lock(&register_mutex);
	snd_seq_oss_release(dp);
	mutex_unlock(&register_mutex);

	return 0;
}

static ssize_t
odev_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	struct seq_oss_devinfo *dp;
	dp = file->private_data;
	if (snd_BUG_ON(!dp))
		return -ENXIO;
	return snd_seq_oss_read(dp, buf, count);
}


static ssize_t
odev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	struct seq_oss_devinfo *dp;
	dp = file->private_data;
	if (snd_BUG_ON(!dp))
		return -ENXIO;
	return snd_seq_oss_write(dp, buf, count, file);
}

static long
odev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct seq_oss_devinfo *dp;
	dp = file->private_data;
	if (snd_BUG_ON(!dp))
		return -ENXIO;
	return snd_seq_oss_ioctl(dp, cmd, arg);
}

#ifdef CONFIG_COMPAT
#define odev_ioctl_compat	odev_ioctl
#else
#define odev_ioctl_compat	NULL
#endif

static unsigned int
odev_poll(struct file *file, poll_table * wait)
{
	struct seq_oss_devinfo *dp;
	dp = file->private_data;
	if (snd_BUG_ON(!dp))
		return -ENXIO;
	return snd_seq_oss_poll(dp, file, wait);
}

/*
 * registration of sequencer minor device
 */

static const struct file_operations seq_oss_f_ops =
{
	.owner =	THIS_MODULE,
	.read =		odev_read,
	.write =	odev_write,
	.open =		odev_open,
	.release =	odev_release,
	.poll =		odev_poll,
	.unlocked_ioctl =	odev_ioctl,
	.compat_ioctl =	odev_ioctl_compat,
};

static int __init
register_device(void)
{
	int rc;

	mutex_lock(&register_mutex);
	if ((rc = snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_SEQUENCER,
					  NULL, 0,
					  &seq_oss_f_ops, NULL,
					  SNDRV_SEQ_OSS_DEVNAME)) < 0) {
		snd_printk(KERN_ERR "can't register device seq\n");
		mutex_unlock(&register_mutex);
		return rc;
	}
	if ((rc = snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_MUSIC,
					  NULL, 0,
					  &seq_oss_f_ops, NULL,
					  SNDRV_SEQ_OSS_DEVNAME)) < 0) {
		snd_printk(KERN_ERR "can't register device music\n");
		snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_SEQUENCER, NULL, 0);
		mutex_unlock(&register_mutex);
		return rc;
	}
	debug_printk(("device registered\n"));
	mutex_unlock(&register_mutex);
	return 0;
}

static void
unregister_device(void)
{
	mutex_lock(&register_mutex);
	debug_printk(("device unregistered\n"));
	if (snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_MUSIC, NULL, 0) < 0)		
		snd_printk(KERN_ERR "error unregister device music\n");
	if (snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_SEQUENCER, NULL, 0) < 0)
		snd_printk(KERN_ERR "error unregister device seq\n");
	mutex_unlock(&register_mutex);
}

/*
 * /proc interface
 */

#ifdef CONFIG_PROC_FS

static struct snd_info_entry *info_entry;

static void
info_read(struct snd_info_entry *entry, struct snd_info_buffer *buf)
{
	mutex_lock(&register_mutex);
	snd_iprintf(buf, "OSS sequencer emulation version %s\n", SNDRV_SEQ_OSS_VERSION_STR);
	snd_seq_oss_system_info_read(buf);
	snd_seq_oss_synth_info_read(buf);
	snd_seq_oss_midi_info_read(buf);
	mutex_unlock(&register_mutex);
}


static int __init
register_proc(void)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, SNDRV_SEQ_OSS_PROCNAME, snd_seq_root);
	if (entry == NULL)
		return -ENOMEM;

	entry->content = SNDRV_INFO_CONTENT_TEXT;
	entry->private_data = NULL;
	entry->c.text.read = info_read;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return -ENOMEM;
	}
	info_entry = entry;
	return 0;
}

static void
unregister_proc(void)
{
	snd_info_free_entry(info_entry);
	info_entry = NULL;
}
#endif /* CONFIG_PROC_FS */
