// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OSS compatible sequencer driver
 *
 * registration of device and proc
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/compat.h>
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


/*
 * prototypes
 */
static int register_device(void);
static void unregister_device(void);
#ifdef CONFIG_SND_PROC_FS
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
static __poll_t odev_poll(struct file *file, poll_table * wait);


/*
 * module interface
 */

static struct snd_seq_driver seq_oss_synth_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.probe = snd_seq_oss_synth_probe,
		.remove = snd_seq_oss_synth_remove,
	},
	.id = SNDRV_SEQ_DEV_ID_OSS,
	.argsize = sizeof(struct snd_seq_oss_reg),
};

static int __init alsa_seq_oss_init(void)
{
	int rc;

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

	rc = snd_seq_driver_register(&seq_oss_synth_driver);
	if (rc < 0) {
		snd_seq_oss_delete_client();
		unregister_proc();
		unregister_device();
		goto error;
	}

	/* success */
	snd_seq_oss_synth_init();

 error:
	return rc;
}

static void __exit alsa_seq_oss_exit(void)
{
	snd_seq_driver_unregister(&seq_oss_synth_driver);
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
static long odev_ioctl_compat(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	return odev_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define odev_ioctl_compat	NULL
#endif

static __poll_t
odev_poll(struct file *file, poll_table * wait)
{
	struct seq_oss_devinfo *dp;
	dp = file->private_data;
	if (snd_BUG_ON(!dp))
		return EPOLLERR;
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
	.llseek =	noop_llseek,
};

static int __init
register_device(void)
{
	int rc;

	mutex_lock(&register_mutex);
	if ((rc = snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_SEQUENCER,
					  NULL, 0,
					  &seq_oss_f_ops, NULL)) < 0) {
		pr_err("ALSA: seq_oss: can't register device seq\n");
		mutex_unlock(&register_mutex);
		return rc;
	}
	if ((rc = snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_MUSIC,
					  NULL, 0,
					  &seq_oss_f_ops, NULL)) < 0) {
		pr_err("ALSA: seq_oss: can't register device music\n");
		snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_SEQUENCER, NULL, 0);
		mutex_unlock(&register_mutex);
		return rc;
	}
	mutex_unlock(&register_mutex);
	return 0;
}

static void
unregister_device(void)
{
	mutex_lock(&register_mutex);
	if (snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_MUSIC, NULL, 0) < 0)		
		pr_err("ALSA: seq_oss: error unregister device music\n");
	if (snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_SEQUENCER, NULL, 0) < 0)
		pr_err("ALSA: seq_oss: error unregister device seq\n");
	mutex_unlock(&register_mutex);
}

/*
 * /proc interface
 */

#ifdef CONFIG_SND_PROC_FS

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
#endif /* CONFIG_SND_PROC_FS */
