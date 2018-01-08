// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Author: Luo Xionghu <xionghu.luo@intel.com>
 *         Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

/*
 * virt IO FE driver
 *
 * The SOF driver thinks this driver is another audio DSP, however the calls
 * made by the SOF driver core do not directly go to HW, but over a virtIO
 * message Q to the virtIO BE driver.
 *
 * The virtIO message Q will use the *exact* same IPC structures as we currently
 * use in the mailbox.
 *
 * Guest OS SOF core -> SOF FE -> virtIO Q -> SOF BE ->
 * System OS SOF core -> DSP
 *
 * The mailbox IO and TX/RX msg functions below will do IO on the virt IO Q.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/device.h>
#include <linux/virtio.h>
#include <sound/sof.h>
#include <uapi/sound/sof-fw.h>

#include "sof-priv.h"
#include "ops.h"
#include "intel.h"

/*
 * IPC Firmware ready.
 */
static int virtio_fe_fw_ready(struct snd_sof_dev *sdev, u32 msg_id)
{
	/* not needed for FE ? */
	return 0;
}

/*
 * IPC Mailbox IO
 */

static void virtio_fe_mailbox_write(struct snd_sof_dev *sdev, u32 offset,
				    void *message, size_t bytes)
{
	/* write data to message Q buffer before sending message */
}

static void virtio_fe_mailbox_read(struct snd_sof_dev *sdev, u32 offset,
				   void *message, size_t bytes)
{
	/* read data from message Q buffer after receiving message */
}

static int virtio_fe_tx_busy(struct snd_sof_dev *sdev)
{
	/* return 1 if tx message Q is busy */
}

static int virtio_fe_tx_msg(struct snd_sof_dev *sdev,
			    struct snd_sof_ipc_msg *msg)
{
	/* write msg to the virtio queue message for BE */

	return 0;
}

static int virtio_fe_rx_msg(struct snd_sof_dev *sdev,
			    struct snd_sof_ipc_msg *msg)
{
	/* read the virtio queue message from BE and copy to msg */
	return 0;
}

/*
 * Probe and remove.
 */

static int virtio_fe_probe(struct snd_sof_dev *sdev)
{
	/* register virtio device */

	/* conenct virt queues to BE */
}

static int virtio_fe_remove(struct snd_sof_dev *sdev)
{
	/* free virtio resurces and unregister device */
}

/* baytrail ops */
struct snd_sof_dsp_ops snd_sof_virtio_fe_ops = {
	/* device init */
	.probe		= virtio_fe_probe,
	.remove		= virtio_fe_remove,

	/* mailbox */
	.mailbox_read	= virtio_fe_mailbox_read,
	.mailbox_write	= virtio_fe_mailbox_write,

	/* ipc */
	.tx_msg		= virtio_fe_tx_msg,
	.rx_msg		= virtio_fe_rx_msg,
	.fw_ready	= virtio_fe_fw_ready,
	.tx_busy	= virtio_fe_tx_busy,

	/* module loading */
//	.load_module	= snd_sof_parse_module_memcpy,

	/*Firmware loading */
	.load_firmware	= snd_sof_load_firmware_memcpy,
};
EXPORT_SYMBOL(snd_sof_virtio_fe_ops);

MODULE_LICENSE("Dual BSD/GPL");
