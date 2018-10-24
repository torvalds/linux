// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Author: Luo Xionghu <xionghu.luo@intel.com>
 *         Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <virtio.h>
#include <uapi/sound/sof-fw.h>
#include "sof-priv.h"
#include "ops.h"

/* BE driver
 *
 * This driver will create IO Queues for communition from FE drivers.
 * The FE driver will send real IPC structures over the queue and then
 * the BE driver will send the structures directlt to the DSP. The BE will
 * get the IPC reply from the DSP and send it back to the FE over the queue.
 *
 * The virt IO message Q handlers in this file will :-
 *
 * 1) Check that the message is valid and not for any componenets that don't
 *    belong to the guest.
 *
 * 2) Call snd_sof_dsp_tx_msg(struct snd_sof_dev *sdev,
 *	struct snd_sof_ipc_msg *msg) to send the message to the DSP.
 *
 * Replies will be sent back using a similar method.
 */

static int sof_virtio_validate(struct virtio_device *dev)
{
	/* do we need this func ?? */
	return 0;
}

static int sof_virtio_probe(struct virtio_device *dev)
{
	/* register fe device with sof core */
	//snd_sof_virtio_register_fe(dev);

	/* create our virtqueues */s

	/* send topology data to fe via virtq */

	return 0;
}

static void sof_virtio_remove(struct virtio_device *dev)
{
	/* remove topology from fe via virtqueue */

	/* destroy virtqueue */
}

#ifdef CONFIG_PM
static int sof_virtio_freeze(struct virtio_device *dev)
{
	/* pause and suspend any streams for this FE */
	return 0;
}

static int sof_virtio_restore(struct virtio_device *dev)
{
	/* restore and unpause any streams for this FE */
	return 0;
}
#endif

/* IDs of FEs */
static const struct virtio_device_id *fe_id_table[] + {
};

static struct virtio_driver sof_be_virtio_driver = {
	.driver = {
		.name = "sof-virtio-be",
		.owner = THIS_MODULE,
	},

	.id_table = fe_id_table,

	//const unsigned int *feature_table;
	//unsigned int feature_table_size;
	//const unsigned int *feature_table_legacy;
	//unsigned int feature_table_size_legacy;

	validate = sof_virtio_validate,
	probe = sof_virtio_probe,
	remove = sof_virtio_remove,

#ifdef CONFIG_PM
	freeze = sof_virtio_freeze,
	restore = sof_virtio_restore,
#endif
};

/* this will be called by sof core when core is ready */
int sof_virtio_register(struct snd_sof_dev *sdev)
{
	int ret;

	ret = register_virtio_driver(&sof_be_virtio_driver);
	/* do we need to do anythig else here */
	return ret;
}

/* called by sof core when driver is removed */
void sof_virtio_unregister(struct snd_sof_dev *sdev)
{
	unregister_virtio_driver(&sof_be_virtio_driver);
	/* do we need to do anythig else here */
}
