// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <linux/irqreturn.h>
#include "core.h"
#include "messages.h"
#include "registers.h"

#define CATPT_IPC_TIMEOUT_MS	300

void catpt_ipc_init(struct catpt_ipc *ipc, struct device *dev)
{
	ipc->dev = dev;
	ipc->ready = false;
	ipc->default_timeout = CATPT_IPC_TIMEOUT_MS;
	init_completion(&ipc->done_completion);
	init_completion(&ipc->busy_completion);
	spin_lock_init(&ipc->lock);
	mutex_init(&ipc->mutex);
}

static int catpt_ipc_arm(struct catpt_ipc *ipc, struct catpt_fw_ready *config)
{
	/*
	 * Both tx and rx are put into and received from outbox. Inbox is
	 * only used for notifications where payload size is known upfront,
	 * thus no separate buffer is allocated for it.
	 */
	ipc->rx.data = devm_kzalloc(ipc->dev, config->outbox_size, GFP_KERNEL);
	if (!ipc->rx.data)
		return -ENOMEM;

	memcpy(&ipc->config, config, sizeof(*config));
	ipc->ready = true;

	return 0;
}

static void catpt_ipc_msg_init(struct catpt_ipc *ipc,
			       struct catpt_ipc_msg *reply)
{
	lockdep_assert_held(&ipc->lock);

	ipc->rx.header = 0;
	ipc->rx.size = reply ? reply->size : 0;
	reinit_completion(&ipc->done_completion);
	reinit_completion(&ipc->busy_completion);
}

static void catpt_dsp_send_tx(struct catpt_dev *cdev,
			      const struct catpt_ipc_msg *tx)
{
	u32 header = tx->header | CATPT_IPCC_BUSY;

	memcpy_toio(catpt_outbox_addr(cdev), tx->data, tx->size);
	catpt_writel_shim(cdev, IPCC, header);
}

static int catpt_wait_msg_completion(struct catpt_dev *cdev, int timeout)
{
	struct catpt_ipc *ipc = &cdev->ipc;
	int ret;

	ret = wait_for_completion_timeout(&ipc->done_completion,
					  msecs_to_jiffies(timeout));
	if (!ret)
		return -ETIMEDOUT;
	if (ipc->rx.rsp.status != CATPT_REPLY_PENDING)
		return 0;

	/* wait for delayed reply */
	ret = wait_for_completion_timeout(&ipc->busy_completion,
					  msecs_to_jiffies(timeout));
	return ret ? 0 : -ETIMEDOUT;
}

static int catpt_dsp_do_send_msg(struct catpt_dev *cdev,
				 struct catpt_ipc_msg request,
				 struct catpt_ipc_msg *reply, int timeout)
{
	struct catpt_ipc *ipc = &cdev->ipc;
	unsigned long flags;
	int ret;

	if (!ipc->ready)
		return -EPERM;
	if (request.size > ipc->config.outbox_size ||
	    (reply && reply->size > ipc->config.outbox_size))
		return -EINVAL;

	spin_lock_irqsave(&ipc->lock, flags);
	catpt_ipc_msg_init(ipc, reply);
	catpt_dsp_send_tx(cdev, &request);
	spin_unlock_irqrestore(&ipc->lock, flags);

	ret = catpt_wait_msg_completion(cdev, timeout);
	if (ret) {
		dev_crit(cdev->dev, "communication severed: %d, rebooting dsp..\n",
			 ret);
		ipc->ready = false;
		/* TODO: attempt recovery */
		return ret;
	}

	ret = ipc->rx.rsp.status;
	if (reply) {
		reply->header = ipc->rx.header;

		if (!ret && reply->data)
			memcpy(reply->data, ipc->rx.data, reply->size);
	}

	return ret;
}

int catpt_dsp_send_msg_timeout(struct catpt_dev *cdev,
			       struct catpt_ipc_msg request,
			       struct catpt_ipc_msg *reply, int timeout)
{
	struct catpt_ipc *ipc = &cdev->ipc;
	int ret;

	mutex_lock(&ipc->mutex);
	ret = catpt_dsp_do_send_msg(cdev, request, reply, timeout);
	mutex_unlock(&ipc->mutex);

	return ret;
}

int catpt_dsp_send_msg(struct catpt_dev *cdev, struct catpt_ipc_msg request,
		       struct catpt_ipc_msg *reply)
{
	return catpt_dsp_send_msg_timeout(cdev, request, reply,
					  cdev->ipc.default_timeout);
}

static void
catpt_dsp_notify_stream(struct catpt_dev *cdev, union catpt_notify_msg msg)
{
	struct catpt_stream_runtime *stream;
	struct catpt_notify_position pos;
	struct catpt_notify_glitch glitch;

	stream = catpt_stream_find(cdev, msg.stream_hw_id);
	if (!stream) {
		dev_warn(cdev->dev, "notify %d for non-existent stream %d\n",
			 msg.notify_reason, msg.stream_hw_id);
		return;
	}

	switch (msg.notify_reason) {
	case CATPT_NOTIFY_POSITION_CHANGED:
		memcpy_fromio(&pos, catpt_inbox_addr(cdev), sizeof(pos));

		catpt_stream_update_position(cdev, stream, &pos);
		break;

	case CATPT_NOTIFY_GLITCH_OCCURRED:
		memcpy_fromio(&glitch, catpt_inbox_addr(cdev), sizeof(glitch));

		dev_warn(cdev->dev, "glitch %d at pos: 0x%08llx, wp: 0x%08x\n",
			 glitch.type, glitch.presentation_pos,
			 glitch.write_pos);
		break;

	default:
		dev_warn(cdev->dev, "unknown notification: %d received\n",
			 msg.notify_reason);
		break;
	}
}

static void catpt_dsp_copy_rx(struct catpt_dev *cdev, u32 header)
{
	struct catpt_ipc *ipc = &cdev->ipc;

	ipc->rx.header = header;
	if (ipc->rx.rsp.status != CATPT_REPLY_SUCCESS)
		return;

	memcpy_fromio(ipc->rx.data, catpt_outbox_addr(cdev), ipc->rx.size);
}

static void catpt_dsp_process_response(struct catpt_dev *cdev, u32 header)
{
	union catpt_notify_msg msg = CATPT_MSG(header);
	struct catpt_ipc *ipc = &cdev->ipc;

	if (msg.fw_ready) {
		struct catpt_fw_ready config;
		/* to fit 32b header original address is shifted right by 3 */
		u32 off = msg.mailbox_address << 3;

		memcpy_fromio(&config, cdev->lpe_ba + off, sizeof(config));

		catpt_ipc_arm(ipc, &config);
		complete(&cdev->fw_ready);
		return;
	}

	switch (msg.global_msg_type) {
	case CATPT_GLB_REQUEST_CORE_DUMP:
		dev_err(cdev->dev, "ADSP device coredump received\n");
		ipc->ready = false;
		catpt_coredump(cdev);
		/* TODO: attempt recovery */
		break;

	case CATPT_GLB_STREAM_MESSAGE:
		switch (msg.stream_msg_type) {
		case CATPT_STRM_NOTIFICATION:
			catpt_dsp_notify_stream(cdev, msg);
			break;
		default:
			catpt_dsp_copy_rx(cdev, header);
			/* signal completion of delayed reply */
			complete(&ipc->busy_completion);
			break;
		}
		break;

	default:
		dev_warn(cdev->dev, "unknown response: %d received\n",
			 msg.global_msg_type);
		break;
	}
}

irqreturn_t catpt_dsp_irq_thread(int irq, void *dev_id)
{
	struct catpt_dev *cdev = dev_id;
	u32 ipcd;

	ipcd = catpt_readl_shim(cdev, IPCD);

	/* ensure there is delayed reply or notification to process */
	if (!(ipcd & CATPT_IPCD_BUSY))
		return IRQ_NONE;

	catpt_dsp_process_response(cdev, ipcd);

	/* tell DSP processing is completed */
	catpt_updatel_shim(cdev, IPCD, CATPT_IPCD_BUSY | CATPT_IPCD_DONE,
			   CATPT_IPCD_DONE);
	/* unmask dsp BUSY interrupt */
	catpt_updatel_shim(cdev, IMC, CATPT_IMC_IPCDB, 0);

	return IRQ_HANDLED;
}

irqreturn_t catpt_dsp_irq_handler(int irq, void *dev_id)
{
	struct catpt_dev *cdev = dev_id;
	irqreturn_t ret = IRQ_NONE;
	u32 isc, ipcc;

	isc = catpt_readl_shim(cdev, ISC);

	/* immediate reply */
	if (isc & CATPT_ISC_IPCCD) {
		/* mask host DONE interrupt */
		catpt_updatel_shim(cdev, IMC, CATPT_IMC_IPCCD, CATPT_IMC_IPCCD);

		ipcc = catpt_readl_shim(cdev, IPCC);
		catpt_dsp_copy_rx(cdev, ipcc);
		complete(&cdev->ipc.done_completion);

		/* tell DSP processing is completed */
		catpt_updatel_shim(cdev, IPCC, CATPT_IPCC_DONE, 0);
		/* unmask host DONE interrupt */
		catpt_updatel_shim(cdev, IMC, CATPT_IMC_IPCCD, 0);
		ret = IRQ_HANDLED;
	}

	/* delayed reply or notification */
	if (isc & CATPT_ISC_IPCDB) {
		/* mask dsp BUSY interrupt */
		catpt_updatel_shim(cdev, IMC, CATPT_IMC_IPCDB, CATPT_IMC_IPCDB);
		ret = IRQ_WAKE_THREAD;
	}

	return ret;
}
