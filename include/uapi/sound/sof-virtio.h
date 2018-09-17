/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 *  Contact Information:
 *  Author: Luo Xionghu <xionghu.luo@intel.com>
 *  Liam Girdwood <liam.r.girdwood@linux.intel.com>.
 */

#ifndef _SOF_VIRTIO_H
#define _SOF_VIRTIO_H

/* Currently we defined 4 vqs to do the IPC, CMD_TX is for send the msg
 * from FE to BE, and CMD_RX is to receive the reply. NOT_RX is to receive
 * the notification, and NOT_TX is to send empty buffer from FE to BE.
 * If we can handle the IPC with only 2 vqs, the config still need to
 * be changed in the device model(VM config), then only CMD_VQ and NOT_VQ
 * is needed.
 */

#define SOF_VIRTIO_IPC_CMD_TX_VQ	0
#define SOF_VIRTIO_IPC_CMD_RX_VQ	1
#define SOF_VIRTIO_IPC_NOT_TX_VQ	2
#define SOF_VIRTIO_IPC_NOT_RX_VQ	3
#define SOF_VIRTIO_NUM_OF_VQS		4

/* command messages from FE to BE, trigger/open/hw_params and so on */
#define SOF_VIRTIO_IPC_CMD_TX_VQ_NAME	"sof-ipc-cmd-tx"

/* get the reply of the command message */
#define SOF_VIRTIO_IPC_CMD_RX_VQ_NAME	"sof-ipc-cmd-rx"

/* first the FE need send empty buffer to BE to get the notification */
#define SOF_VIRTIO_IPC_NOT_TX_VQ_NAME	"sof-ipc-not-tx"

/* the vq to get the notification */
#define SOF_VIRTIO_IPC_NOT_RX_VQ_NAME	"sof-ipc-not-rx"

#endif
