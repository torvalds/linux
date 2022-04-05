// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//
// Generic IPC layer that can work over MMIO and SPI/I2C. PHY layer provided
// by platform driver code.
//

#include <linux/mutex.h>
#include <linux/types.h>

#include "sof-priv.h"
#include "sof-audio.h"
#include "ops.h"
#include "ipc3-ops.h"

/**
 * sof_ipc_send_msg - generic function to prepare and send one IPC message
 * @sdev:		pointer to SOF core device struct
 * @msg_data:		pointer to a message to send
 * @msg_bytes:		number of bytes in the message
 * @reply_bytes:	number of bytes available for the reply.
 *			The buffer for the reply data is not passed to this
 *			function, the available size is an information for the
 *			reply handling functions.
 *
 * On success the function returns 0, otherwise negative error number.
 *
 * Note: higher level sdev->ipc->tx_mutex must be held to make sure that
 *	 transfers are synchronized.
 */
int sof_ipc_send_msg(struct snd_sof_dev *sdev, void *msg_data, size_t msg_bytes,
		     size_t reply_bytes)
{
	struct snd_sof_ipc *ipc = sdev->ipc;
	struct snd_sof_ipc_msg *msg;
	int ret;

	if (ipc->disable_ipc_tx || sdev->fw_state != SOF_FW_BOOT_COMPLETE)
		return -ENODEV;

	/*
	 * The spin-lock is needed to protect message objects against other
	 * atomic contexts.
	 */
	spin_lock_irq(&sdev->ipc_lock);

	/* initialise the message */
	msg = &ipc->msg;

	/* attach message data */
	msg->msg_data = msg_data;
	msg->msg_size = msg_bytes;

	msg->reply_size = reply_bytes;
	msg->reply_error = 0;

	sdev->msg = msg;

	ret = snd_sof_dsp_send_msg(sdev, msg);
	/* Next reply that we receive will be related to this message */
	if (!ret)
		msg->ipc_complete = false;

	spin_unlock_irq(&sdev->ipc_lock);

	return ret;
}

/* send IPC message from host to DSP */
int sof_ipc_tx_message(struct snd_sof_ipc *ipc, void *msg_data, size_t msg_bytes,
		       void *reply_data, size_t reply_bytes)
{
	if (msg_bytes > ipc->max_payload_size ||
	    reply_bytes > ipc->max_payload_size)
		return -ENOBUFS;

	return ipc->ops->tx_msg(ipc->sdev, msg_data, msg_bytes, reply_data,
				reply_bytes, false);
}
EXPORT_SYMBOL(sof_ipc_tx_message);

/*
 * send IPC message from host to DSP without modifying the DSP state.
 * This will be used for IPC's that can be handled by the DSP
 * even in a low-power D0 substate.
 */
int sof_ipc_tx_message_no_pm(struct snd_sof_ipc *ipc, void *msg_data, size_t msg_bytes,
			     void *reply_data, size_t reply_bytes)
{
	if (msg_bytes > ipc->max_payload_size ||
	    reply_bytes > ipc->max_payload_size)
		return -ENOBUFS;

	return ipc->ops->tx_msg(ipc->sdev, msg_data, msg_bytes, reply_data,
				reply_bytes, true);
}
EXPORT_SYMBOL(sof_ipc_tx_message_no_pm);

/* Generic helper function to retrieve the reply */
void snd_sof_ipc_get_reply(struct snd_sof_dev *sdev)
{
	/*
	 * Sometimes, there is unexpected reply ipc arriving. The reply
	 * ipc belongs to none of the ipcs sent from driver.
	 * In this case, the driver must ignore the ipc.
	 */
	if (!sdev->msg) {
		dev_warn(sdev->dev, "unexpected ipc interrupt raised!\n");
		return;
	}

	sdev->msg->reply_error = sdev->ipc->ops->get_reply(sdev);
}
EXPORT_SYMBOL(snd_sof_ipc_get_reply);

/* handle reply message from DSP */
void snd_sof_ipc_reply(struct snd_sof_dev *sdev, u32 msg_id)
{
	struct snd_sof_ipc_msg *msg = &sdev->ipc->msg;

	if (msg->ipc_complete) {
		dev_dbg(sdev->dev,
			"no reply expected, received 0x%x, will be ignored",
			msg_id);
		return;
	}

	/* wake up and return the error if we have waiters on this message ? */
	msg->ipc_complete = true;
	wake_up(&msg->waitq);
}
EXPORT_SYMBOL(snd_sof_ipc_reply);

/*
 * IPC get()/set() for kcontrols.
 */
int snd_sof_ipc_set_get_comp_data(struct snd_sof_control *scontrol, bool set)
{
	struct snd_soc_component *scomp = scontrol->scomp;
	struct sof_ipc_ctrl_data *cdata = scontrol->ipc_control_data;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	enum sof_ipc_ctrl_type ctrl_type;
	struct snd_sof_widget *swidget;
	bool widget_found = false;
	u32 ipc_cmd, msg_bytes;

	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (swidget->comp_id == scontrol->comp_id) {
			widget_found = true;
			break;
		}
	}

	if (!widget_found) {
		dev_err(sdev->dev, "%s: can't find widget with id %d\n", __func__,
			scontrol->comp_id);
		return -EINVAL;
	}

	/*
	 * Volatile controls should always be part of static pipelines and the widget use_count
	 * would always be > 0 in this case. For the others, just return the cached value if the
	 * widget is not set up.
	 */
	if (!swidget->use_count)
		return 0;

	/*
	 * Select the IPC cmd and the ctrl_type based on the ctrl_cmd and the
	 * direction
	 * Note: SOF_CTRL_TYPE_VALUE_COMP_* is not used and supported currently
	 *	 for ctrl_type
	 */
	if (cdata->cmd == SOF_CTRL_CMD_BINARY) {
		ipc_cmd = set ? SOF_IPC_COMP_SET_DATA : SOF_IPC_COMP_GET_DATA;
		ctrl_type = set ? SOF_CTRL_TYPE_DATA_SET : SOF_CTRL_TYPE_DATA_GET;
	} else {
		ipc_cmd = set ? SOF_IPC_COMP_SET_VALUE : SOF_IPC_COMP_GET_VALUE;
		ctrl_type = set ? SOF_CTRL_TYPE_VALUE_CHAN_SET : SOF_CTRL_TYPE_VALUE_CHAN_GET;
	}

	cdata->rhdr.hdr.cmd = SOF_IPC_GLB_COMP_MSG | ipc_cmd;
	cdata->type = ctrl_type;
	cdata->comp_id = scontrol->comp_id;
	cdata->msg_index = 0;

	/* calculate header and data size */
	switch (cdata->type) {
	case SOF_CTRL_TYPE_VALUE_CHAN_GET:
	case SOF_CTRL_TYPE_VALUE_CHAN_SET:
		cdata->num_elems = scontrol->num_channels;

		msg_bytes = scontrol->num_channels *
			    sizeof(struct sof_ipc_ctrl_value_chan);
		msg_bytes += sizeof(struct sof_ipc_ctrl_data);
		break;
	case SOF_CTRL_TYPE_DATA_GET:
	case SOF_CTRL_TYPE_DATA_SET:
		cdata->num_elems = cdata->data->size;

		msg_bytes = cdata->data->size;
		msg_bytes += sizeof(struct sof_ipc_ctrl_data) +
			     sizeof(struct sof_abi_hdr);
		break;
	default:
		return -EINVAL;
	}

	cdata->rhdr.hdr.size = msg_bytes;
	cdata->elems_remaining = 0;

	return iops->set_get_data(sdev, cdata, cdata->rhdr.hdr.size, set);
}
EXPORT_SYMBOL(snd_sof_ipc_set_get_comp_data);

int snd_sof_ipc_valid(struct snd_sof_dev *sdev)
{
	struct sof_ipc_fw_ready *ready = &sdev->fw_ready;
	struct sof_ipc_fw_version *v = &ready->version;

	dev_info(sdev->dev,
		 "Firmware info: version %d:%d:%d-%s\n",  v->major, v->minor,
		 v->micro, v->tag);
	dev_info(sdev->dev,
		 "Firmware: ABI %d:%d:%d Kernel ABI %d:%d:%d\n",
		 SOF_ABI_VERSION_MAJOR(v->abi_version),
		 SOF_ABI_VERSION_MINOR(v->abi_version),
		 SOF_ABI_VERSION_PATCH(v->abi_version),
		 SOF_ABI_MAJOR, SOF_ABI_MINOR, SOF_ABI_PATCH);

	if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION, v->abi_version)) {
		dev_err(sdev->dev, "error: incompatible FW ABI version\n");
		return -EINVAL;
	}

	if (SOF_ABI_VERSION_MINOR(v->abi_version) > SOF_ABI_MINOR) {
		if (!IS_ENABLED(CONFIG_SND_SOC_SOF_STRICT_ABI_CHECKS)) {
			dev_warn(sdev->dev, "warn: FW ABI is more recent than kernel\n");
		} else {
			dev_err(sdev->dev, "error: FW ABI is more recent than kernel\n");
			return -EINVAL;
		}
	}

	if (ready->flags & SOF_IPC_INFO_BUILD) {
		dev_info(sdev->dev,
			 "Firmware debug build %d on %s-%s - options:\n"
			 " GDB: %s\n"
			 " lock debug: %s\n"
			 " lock vdebug: %s\n",
			 v->build, v->date, v->time,
			 (ready->flags & SOF_IPC_INFO_GDB) ?
				"enabled" : "disabled",
			 (ready->flags & SOF_IPC_INFO_LOCKS) ?
				"enabled" : "disabled",
			 (ready->flags & SOF_IPC_INFO_LOCKSV) ?
				"enabled" : "disabled");
	}

	/* copy the fw_version into debugfs at first boot */
	memcpy(&sdev->fw_version, v, sizeof(*v));

	return 0;
}
EXPORT_SYMBOL(snd_sof_ipc_valid);

int sof_ipc_init_msg_memory(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc_msg *msg;

	msg = &sdev->ipc->msg;

	msg->reply_data = devm_kzalloc(sdev->dev, SOF_IPC_MSG_MAX_SIZE, GFP_KERNEL);
	if (!msg->reply_data)
		return -ENOMEM;

	sdev->ipc->max_payload_size = SOF_IPC_MSG_MAX_SIZE;

	return 0;
}

struct snd_sof_ipc *snd_sof_ipc_init(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc *ipc;
	struct snd_sof_ipc_msg *msg;
	const struct sof_ipc_ops *ops;

	ipc = devm_kzalloc(sdev->dev, sizeof(*ipc), GFP_KERNEL);
	if (!ipc)
		return NULL;

	mutex_init(&ipc->tx_mutex);
	ipc->sdev = sdev;
	msg = &ipc->msg;

	/* indicate that we aren't sending a message ATM */
	msg->ipc_complete = true;

	init_waitqueue_head(&msg->waitq);

	/*
	 * Use IPC3 ops as it is the only available version now. With the addition of new IPC
	 * versions, this will need to be modified to use the selected version at runtime.
	 */
	ipc->ops = &ipc3_ops;
	ops = ipc->ops;

	/* check for mandatory ops */
	if (!ops->tx_msg || !ops->rx_msg || !ops->set_get_data || !ops->get_reply) {
		dev_err(sdev->dev, "Missing IPC message handling ops\n");
		return NULL;
	}

	if (!ops->pcm) {
		dev_err(sdev->dev, "Missing IPC PCM ops\n");
		return NULL;
	}

	if (!ops->tplg || !ops->tplg->widget || !ops->tplg->control) {
		dev_err(sdev->dev, "Missing IPC topology ops\n");
		return NULL;
	}

	return ipc;
}
EXPORT_SYMBOL(snd_sof_ipc_init);

void snd_sof_ipc_free(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc *ipc = sdev->ipc;

	if (!ipc)
		return;

	/* disable sending of ipc's */
	mutex_lock(&ipc->tx_mutex);
	ipc->disable_ipc_tx = true;
	mutex_unlock(&ipc->tx_mutex);
}
EXPORT_SYMBOL(snd_sof_ipc_free);
