// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Author: Libin Yang <libin.yang@intel.com>
 * Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __SOUND_SOF_VIRTIO_MISCDEV_H
#define __SOUND_SOF_VIRTIO_MISCDEV_H

struct virtio_miscdev {
	struct device *dev;
	int (*open)(struct file *f, void *data);
	long (*ioctl)(struct file *f, void *data, unsigned int ioctl,
		      unsigned long arg);
	int (*release)(struct file *f, void *data);
	void *priv;
};

#endif	/* __SOUND_SOF_VIRTIO_MISCDEV_H */
