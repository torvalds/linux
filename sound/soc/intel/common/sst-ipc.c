/*
 * Intel SST generic IPC Support
 *
 * Copyright (C) 2015, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <sound/asound.h>

#include "sst-dsp.h"
#include "sst-dsp-priv.h"
#include "sst-ipc.h"

/* IPC message timeout (msecs) */
#define IPC_TIMEOUT_MSECS	300

#define IPC_EMPTY_LIST_SIZE	8

/* locks held by caller */
static struct ipc_message *msg_get_empty(struct sst_generic_ipc *ipc)
{
	struct ipc_message *msg = NULL;

	if (!list_empty(&ipc->empty_list)) {
		msg = list_first_entry(&ipc->empty_list, struct ipc_message,
			list);
		list_del(&msg->list);
	}

	return msg;
}

static int tx_wait_done(struct sst_generic_ipc *ipc,
	struct ipc_message *msg, void *rx_data)
{
	unsigned long flags;
	int ret;

	/* wait for DSP completion (in all cases atm inc pending) */
	ret = wait_event_timeout(msg->waitq, msg->complete,
		msecs_to_jiffies(IPC_TIMEOUT_MSECS));

	spin_lock_irqsave(&ipc->dsp->spinlock, flags);
	if (ret == 0) {
		if (ipc->ops.shim_dbg != NULL)
			ipc->ops.shim_dbg(ipc, "message timeout");

		list_del(&msg->list);
		ret = -ETIMEDOUT;
	} else {

		/* copy the data returned from DSP */
		if (rx_data)
			memcpy(rx_data, msg->rx_data, msg->rx_size);
		ret = msg->errno;
	}

	list_add_tail(&msg->list, &ipc->empty_list);
	spin_unlock_irqrestore(&ipc->dsp->spinlock, flags);
	return ret;
}

static int ipc_tx_message(struct sst_generic_ipc *ipc, u64 header,
	void *tx_data, size_t tx_bytes, void *rx_data,
	size_t rx_bytes, int wait)
{
	struct ipc_message *msg;
	unsigned long flags;

	spin_lock_irqsave(&ipc->dsp->spinlock, flags);

	msg = msg_get_empty(ipc);
	if (msg == NULL) {
		spin_unlock_irqrestore(&ipc->dsp->spinlock, flags);
		return -EBUSY;
	}

	msg->header = header;
	msg->tx_size = tx_bytes;
	msg->rx_size = rx_bytes;
	msg->wait = wait;
	msg->errno = 0;
	msg->pending = false;
	msg->complete = false;

	if ((tx_bytes) && (ipc->ops.tx_data_copy != NULL))
		ipc->ops.tx_data_copy(msg, tx_data, tx_bytes);

	list_add_tail(&msg->list, &ipc->tx_list);
	schedule_work(&ipc->kwork);
	spin_unlock_irqrestore(&ipc->dsp->spinlock, flags);

	if (wait)
		return tx_wait_done(ipc, msg, rx_data);
	else
		return 0;
}

static int msg_empty_list_init(struct sst_generic_ipc *ipc)
{
	int i;

	ipc->msg = kcalloc(IPC_EMPTY_LIST_SIZE, sizeof(struct ipc_message),
			   GFP_KERNEL);
	if (ipc->msg == NULL)
		return -ENOMEM;

	for (i = 0; i < IPC_EMPTY_LIST_SIZE; i++) {
		ipc->msg[i].tx_data = kzalloc(ipc->tx_data_max_size, GFP_KERNEL);
		if (ipc->msg[i].tx_data == NULL)
			goto free_mem;

		ipc->msg[i].rx_data = kzalloc(ipc->rx_data_max_size, GFP_KERNEL);
		if (ipc->msg[i].rx_data == NULL) {
			kfree(ipc->msg[i].tx_data);
			goto free_mem;
		}

		init_waitqueue_head(&ipc->msg[i].waitq);
		list_add(&ipc->msg[i].list, &ipc->empty_list);
	}

	return 0;

free_mem:
	while (i > 0) {
		kfree(ipc->msg[i-1].tx_data);
		kfree(ipc->msg[i-1].rx_data);
		--i;
	}
	kfree(ipc->msg);

	return -ENOMEM;
}

static void ipc_tx_msgs(struct work_struct *work)
{
	struct sst_generic_ipc *ipc =
		container_of(work, struct sst_generic_ipc, kwork);
	struct ipc_message *msg;

	spin_lock_irq(&ipc->dsp->spinlock);

	while (!list_empty(&ipc->tx_list) && !ipc->pending) {
		/* if the DSP is busy, we will TX messages after IRQ.
		 * also postpone if we are in the middle of processing
		 * completion irq
		 */
		if (ipc->ops.is_dsp_busy && ipc->ops.is_dsp_busy(ipc->dsp)) {
			dev_dbg(ipc->dev, "ipc_tx_msgs dsp busy\n");
			break;
		}

		msg = list_first_entry(&ipc->tx_list, struct ipc_message, list);
		list_move(&msg->list, &ipc->rx_list);

		if (ipc->ops.tx_msg != NULL)
			ipc->ops.tx_msg(ipc, msg);
	}

	spin_unlock_irq(&ipc->dsp->spinlock);
}

int sst_ipc_tx_message_wait(struct sst_generic_ipc *ipc, u64 header,
	void *tx_data, size_t tx_bytes, void *rx_data, size_t rx_bytes)
{
	int ret;

	/*
	 * DSP maybe in lower power active state, so
	 * check if the DSP supports DSP lp On method
	 * if so invoke that before sending IPC
	 */
	if (ipc->ops.check_dsp_lp_on)
		if (ipc->ops.check_dsp_lp_on(ipc->dsp, true))
			return -EIO;

	ret = ipc_tx_message(ipc, header, tx_data, tx_bytes,
		rx_data, rx_bytes, 1);

	if (ipc->ops.check_dsp_lp_on)
		if (ipc->ops.check_dsp_lp_on(ipc->dsp, false))
			return -EIO;

	return ret;
}
EXPORT_SYMBOL_GPL(sst_ipc_tx_message_wait);

int sst_ipc_tx_message_nowait(struct sst_generic_ipc *ipc, u64 header,
	void *tx_data, size_t tx_bytes)
{
	return ipc_tx_message(ipc, header, tx_data, tx_bytes,
		NULL, 0, 0);
}
EXPORT_SYMBOL_GPL(sst_ipc_tx_message_nowait);

int sst_ipc_tx_message_nopm(struct sst_generic_ipc *ipc, u64 header,
	void *tx_data, size_t tx_bytes, void *rx_data, size_t rx_bytes)
{
	return ipc_tx_message(ipc, header, tx_data, tx_bytes,
		rx_data, rx_bytes, 1);
}
EXPORT_SYMBOL_GPL(sst_ipc_tx_message_nopm);

struct ipc_message *sst_ipc_reply_find_msg(struct sst_generic_ipc *ipc,
	u64 header)
{
	struct ipc_message *msg;
	u64 mask;

	if (ipc->ops.reply_msg_match != NULL)
		header = ipc->ops.reply_msg_match(header, &mask);

	if (list_empty(&ipc->rx_list)) {
		dev_err(ipc->dev, "error: rx list empty but received 0x%llx\n",
			header);
		return NULL;
	}

	list_for_each_entry(msg, &ipc->rx_list, list) {
		if ((msg->header & mask) == header)
			return msg;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(sst_ipc_reply_find_msg);

/* locks held by caller */
void sst_ipc_tx_msg_reply_complete(struct sst_generic_ipc *ipc,
	struct ipc_message *msg)
{
	msg->complete = true;

	if (!msg->wait)
		list_add_tail(&msg->list, &ipc->empty_list);
	else
		wake_up(&msg->waitq);
}
EXPORT_SYMBOL_GPL(sst_ipc_tx_msg_reply_complete);

void sst_ipc_drop_all(struct sst_generic_ipc *ipc)
{
	struct ipc_message *msg, *tmp;
	unsigned long flags;
	int tx_drop_cnt = 0, rx_drop_cnt = 0;

	/* drop all TX and Rx messages before we stall + reset DSP */
	spin_lock_irqsave(&ipc->dsp->spinlock, flags);

	list_for_each_entry_safe(msg, tmp, &ipc->tx_list, list) {
		list_move(&msg->list, &ipc->empty_list);
		tx_drop_cnt++;
	}

	list_for_each_entry_safe(msg, tmp, &ipc->rx_list, list) {
		list_move(&msg->list, &ipc->empty_list);
		rx_drop_cnt++;
	}

	spin_unlock_irqrestore(&ipc->dsp->spinlock, flags);

	if (tx_drop_cnt || rx_drop_cnt)
		dev_err(ipc->dev, "dropped IPC msg RX=%d, TX=%d\n",
			tx_drop_cnt, rx_drop_cnt);
}
EXPORT_SYMBOL_GPL(sst_ipc_drop_all);

int sst_ipc_init(struct sst_generic_ipc *ipc)
{
	int ret;

	INIT_LIST_HEAD(&ipc->tx_list);
	INIT_LIST_HEAD(&ipc->rx_list);
	INIT_LIST_HEAD(&ipc->empty_list);
	init_waitqueue_head(&ipc->wait_txq);

	ret = msg_empty_list_init(ipc);
	if (ret < 0)
		return -ENOMEM;

	INIT_WORK(&ipc->kwork, ipc_tx_msgs);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_ipc_init);

void sst_ipc_fini(struct sst_generic_ipc *ipc)
{
	int i;

	cancel_work_sync(&ipc->kwork);

	if (ipc->msg) {
		for (i = 0; i < IPC_EMPTY_LIST_SIZE; i++) {
			kfree(ipc->msg[i].tx_data);
			kfree(ipc->msg[i].rx_data);
		}
		kfree(ipc->msg);
	}
}
EXPORT_SYMBOL_GPL(sst_ipc_fini);

/* Module information */
MODULE_AUTHOR("Jin Yao");
MODULE_DESCRIPTION("Intel SST IPC generic");
MODULE_LICENSE("GPL v2");
