// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Author: Libin Yang <libin.yang@intel.com>
 *         Luo Xionghu <xionghu.luo@intel.com>
 *         Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>

#include "sof-priv.h"
#include "virtio-be.h"
#include "virtio-miscdev.h"
#include <linux/vbs/vbs.h>

/*
 * This module registers a device node /dev/vbs_k_audio,
 * that handle the communication between Device Model and
 * the virtio backend service. The device model can
 * control the backend to : set the status, set the vq account
 * and etc. The config of the DM and VBS must be accordance.
 */

static struct virtio_miscdev *virtio_audio;

static struct virtio_miscdev *get_virtio_audio(void)
{
	return virtio_audio;
}

struct snd_sof_dev *sof_virtio_get_sof(void)
{
	struct virtio_miscdev *vaudio = get_virtio_audio();

	if (vaudio)
		return (struct snd_sof_dev *)vaudio->priv;

	return NULL;
}

static int sof_virtio_open(struct file *f, void *data)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *)data;
	struct sof_vbe *vbe;
	int ret;

	ret = sof_vbe_register(sdev, &vbe);
	if (ret)
		return ret;

	/*
	 * link to sdev->vbe_list
	 * Maybe virtio_miscdev managing the list is more reasonable.
	 * Let's use sdev to manage the FE audios now.
	 */
	list_add(&vbe->list, &sdev->vbe_list);
	f->private_data = vbe;

	return 0;
}

static long sof_virtio_ioctl(struct file *f, void *data, unsigned int ioctl,
			     unsigned long arg)
{
	struct sof_vbe *vbe = f->private_data;
	void __user *argp = (void __user *)arg;
	int ret;

	switch (ioctl) {
	case VBS_SET_DEV:
		ret = virtio_dev_ioctl(&vbe->dev_info, ioctl, argp);
		if (!ret)
			vbe->vm_id = vbe->dev_info._ctx.vmid;
		break;
	case VBS_SET_VQ:
		ret = virtio_vqs_ioctl(&vbe->dev_info, ioctl, argp);
		if (ret)
			return ret;

		/*
		 * Maybe we should move sof_register_vhm_client()
		 * in VBS_SET_DEV
		 */
		ret = sof_vbe_register_client(vbe);
		if (ret)
			return ret;
		/*
		 * TODO: load tplg and send to FE here
		 *
		 *  The better method is FE driver send FE-tplg id
		 *  and request FE-tplg.
		 *  Then BE loads the corresponding tplg based on
		 *  the FE-tplg id and send to FE driver.
		 */
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return ret;
}

static int sof_virtio_release(struct file *f, void *data)
{
	struct sof_vbe *vbe = f->private_data;

	list_del(&vbe->list);
	devm_kfree(vbe->sdev->dev, vbe);
	f->private_data = NULL;
	return 0;
}

static int vbs_audio_open(struct inode *inode, struct file *f)
{
	struct virtio_miscdev *vaudio = get_virtio_audio();

	if (!vaudio)
		return -ENODEV;	/* This should never happen */

	dev_dbg(vaudio->dev, "virtio audio open\n");
	if (vaudio->open)
		return vaudio->open(f, virtio_audio->priv);

	return 0;
}

static long vbs_audio_ioctl(struct file *f, unsigned int ioctl,
			    unsigned long arg)
{
	struct virtio_miscdev *vaudio = get_virtio_audio();

	if (!vaudio)
		return -ENODEV;	/* This should never happen */

	dev_dbg(vaudio->dev, "virtio audio ioctl\n");
	if (vaudio->ioctl)
		return vaudio->ioctl(f, vaudio->priv, ioctl, arg);
	else
		return -ENXIO;
}

static int vbs_audio_release(struct inode *inode, struct file *f)
{
	struct virtio_miscdev *vaudio = get_virtio_audio();

	if (!vaudio)
		return -ENODEV;	/* This should never happen */

	dev_dbg(vaudio->dev, "release virtio audio\n");

	if (vaudio->release)
		vaudio->release(f, vaudio->priv);

	return 0;
}

static const struct file_operations vbs_audio_fops = {
	.owner          = THIS_MODULE,
	.release        = vbs_audio_release,
	.unlocked_ioctl = vbs_audio_ioctl,
	.open           = vbs_audio_open,
	.llseek		= noop_llseek,
};

static struct miscdevice vbs_audio_k = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "vbs_k_audio",
	.fops = &vbs_audio_fops,
};

static int audio_virtio_miscdev_register(struct device *dev, void *data,
					 struct virtio_miscdev **va)
{
	struct virtio_miscdev *vaudio;
	int ret;

	ret = misc_register(&vbs_audio_k);
	if (ret) {
		dev_err(dev, "misc device register failed %d\n", ret);
		return ret;
	}

	vaudio = kzalloc(sizeof(*vaudio), GFP_KERNEL);
	if (!vaudio) {
		misc_deregister(&vbs_audio_k);
		return -ENOMEM;
	}

	vaudio->priv = data;
	vaudio->dev = dev;
	virtio_audio = vaudio;
	*va = vaudio;

	return 0;
}

static int audio_virtio_miscdev_unregister(void)
{
	if (virtio_audio) {
		misc_deregister(&vbs_audio_k);
		kfree(virtio_audio);
		virtio_audio = NULL;
	}

	return 0;
}

/**
 * sof_virtio_miscdev_register() - init the virtio be audio driver
 * @sdev: the snd_sof_dev of sof core
 *
 * This function registers the misc device, which will be used
 * by the user space to communicate with the audio driver.
 *
 * Return: 0 for success or negative value for err
 */
int sof_virtio_miscdev_register(struct snd_sof_dev *sdev)
{
	struct virtio_miscdev *vaudio;
	int ret;

	ret = audio_virtio_miscdev_register(sdev->dev, sdev, &vaudio);
	if (ret)
		return ret;

	vaudio->open = sof_virtio_open;
	vaudio->ioctl = sof_virtio_ioctl;
	vaudio->release = sof_virtio_release;

	return 0;
}
EXPORT_SYMBOL(sof_virtio_miscdev_register);

/**
 * sof_virtio_miscdev_unregister() - release the virtio be audio driver
 *
 * This function deregisters the misc device, and free virtio_miscdev
 *
 */
int sof_virtio_miscdev_unregister(void)
{
	return audio_virtio_miscdev_unregister();
}
EXPORT_SYMBOL(sof_virtio_miscdev_unregister);
