// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019 NXP
 *  Author: Daniel Baluta <daniel.baluta@nxp.com>
 *
 * Implementation of the DSP IPC interface (host side)
 */

#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "dsp.h"

/* mailbox protocol refer to sof jh7110 mailbox */
#define JH7110_SOF_MAILBOX_HOST_REQUEST_MSG_DATA   0x02
#define JH7110_SOF_MAILBOX_HOST_REPLY_MSG_DATA     0x20
#define JH7110_SOF_MAILBOX_DSP_REQUEST_MSG_DATA    0x01
#define JH7110_SOF_MAILBOX_DSP_REPLY_MSG_DATA      0x10

/*
 * jh7110_dsp_ring_doorbell - triggers an interrupt on the other side (DSP)
 *
 * @dsp: DSP IPC handle
 * @chan_idx: index of the channel where to trigger the interrupt
 *
 * Returns non-negative value for success, negative value for error
 */
int jh7110_dsp_ring_doorbell(struct jh7110_dsp_ipc *ipc, unsigned int is_ack)
{
	int ret;
	struct jh7110_dsp_chan *dsp_chan;
	u32 msg = 0x02;

	dev_dbg(ipc->dev, "dsp ring doorbell for %s",
		(is_ack == 0) ? "request" : "ack reply");
	msg = (is_ack == 0) ? JH7110_SOF_MAILBOX_HOST_REQUEST_MSG_DATA : JH7110_SOF_MAILBOX_HOST_REPLY_MSG_DATA;
	dsp_chan = &ipc->chans[0];
	ret = mbox_send_message(dsp_chan->ch, (void *)&msg);
	mbox_chan_txdone(dsp_chan->ch, ret);

	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(jh7110_dsp_ring_doorbell);

static void jh7110_dsp_handle_rx_work_func(struct work_struct *work)
{
	struct jh7110_dsp_rx_work *dsp_rx_work;
	struct jh7110_dsp_ipc *ipc;

	dsp_rx_work = container_of(work, struct jh7110_dsp_rx_work, rx_work);
	if (unlikely(!dsp_rx_work))
		return;
	ipc = container_of(dsp_rx_work, struct jh7110_dsp_ipc, work);
	if (unlikely(!ipc))
		return;

	dev_dbg(ipc->dev, "[%s] msg_data: 0x%x\n", __func__, dsp_rx_work->data);

	if (dsp_rx_work->data & JH7110_SOF_MAILBOX_DSP_REPLY_MSG_DATA) {
		ipc->ops->handle_reply(ipc);
		dsp_rx_work->data &= ~JH7110_SOF_MAILBOX_DSP_REPLY_MSG_DATA;
	}
	if (dsp_rx_work->data & JH7110_SOF_MAILBOX_DSP_REQUEST_MSG_DATA) {
		ipc->ops->handle_request(ipc);
		jh7110_dsp_ring_doorbell(ipc, 1);
		dsp_rx_work->data &= ~JH7110_SOF_MAILBOX_DSP_REQUEST_MSG_DATA;
	}
}

/*
 * jh7110_dsp_handle_rx - rx callback used by jh7110 mailbox
 *
 * @c: mbox client
 * @msg: message received
 *
 * Users of DSP IPC will need to privde handle_reply and handle_request
 * callbacks.
 */
static void jh7110_dsp_handle_rx(struct mbox_client *c, void *msg)
{
	struct jh7110_dsp_chan *chan = container_of(c, struct jh7110_dsp_chan, cl);
	u32 msg_data = *((u32 *)msg);

	chan->ipc->work.data |= msg_data;

	queue_work(chan->ipc->dsp_ipc_wq, &chan->ipc->work.rx_work);
}

struct mbox_chan *jh7110_dsp_request_channel(struct jh7110_dsp_ipc *dsp_ipc, int idx)
{
	struct jh7110_dsp_chan *dsp_chan;

	if (idx >= DSP_MU_CHAN_NUM)
		return ERR_PTR(-EINVAL);

	dsp_chan = &dsp_ipc->chans[idx];
	dsp_chan->ch = mbox_request_channel_byname(&dsp_chan->cl, dsp_chan->name);
	return dsp_chan->ch;
}
EXPORT_SYMBOL(jh7110_dsp_request_channel);

void jh7110_dsp_free_channel(struct jh7110_dsp_ipc *dsp_ipc, int idx)
{
	struct jh7110_dsp_chan *dsp_chan;

	if (idx >= DSP_MU_CHAN_NUM)
		return;

	dsp_chan = &dsp_ipc->chans[idx];
	mbox_free_channel(dsp_chan->ch);
}
EXPORT_SYMBOL(jh7110_dsp_free_channel);

static int jh7110_dsp_setup_channels(struct jh7110_dsp_ipc *dsp_ipc)
{
	struct device *dev = dsp_ipc->dev;
	struct jh7110_dsp_chan *dsp_chan;
	struct mbox_client *cl;
	char *chan_name;
	int ret;
	int i, j;

	/* 0 for tx, 1 for rx */
	for (i = 0; i < DSP_MU_CHAN_NUM; i++) {
		if (i == 0)
			chan_name = "tx";
		else
			chan_name = "rx";

		if (!chan_name)
			return -ENOMEM;

		dsp_chan = &dsp_ipc->chans[i];
		dsp_chan->name = chan_name;
		cl = &dsp_chan->cl;
		cl->dev = dev;
		cl->tx_block = false;
		cl->knows_txdone = false;
		cl->rx_callback = jh7110_dsp_handle_rx;
		cl->tx_tout = 500;

		dsp_chan->ipc = dsp_ipc;
		dsp_chan->idx = i;
		dsp_chan->ch = mbox_request_channel_byname(cl, chan_name);
		if (IS_ERR(dsp_chan->ch)) {
			ret = PTR_ERR(dsp_chan->ch);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Failed to request mbox chan %s ret %d\n",
					chan_name, ret);
			goto out;
		}

		dev_dbg(dev, "request mbox chan %s\n", chan_name);
	}

	return 0;
out:
	for (j = 0; j < i; j++) {
		dsp_chan = &dsp_ipc->chans[j];
		mbox_free_channel(dsp_chan->ch);
		kfree(dsp_chan->name);
	}

	return ret;
}

static int jh7110_dsp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct jh7110_dsp_ipc *dsp_ipc;
	int ret;

	device_set_of_node_from_dev(&pdev->dev, pdev->dev.parent);

	dsp_ipc = devm_kzalloc(dev, sizeof(*dsp_ipc), GFP_KERNEL);
	if (!dsp_ipc)
		return -ENOMEM;

	dsp_ipc->dev = dev;
	dev_set_drvdata(dev, dsp_ipc);

	dsp_ipc->dsp_ipc_wq = create_workqueue("dsp ipc wq");
	dsp_ipc->work.data = 0;
	INIT_WORK(&dsp_ipc->work.rx_work, jh7110_dsp_handle_rx_work_func);

	ret = jh7110_dsp_setup_channels(dsp_ipc);
	if (ret)
		return ret;

	dev_dbg(dev, "STARFIVE DSP IPC initialized\n");

	return 0;
}

static int jh7110_dsp_remove(struct platform_device *pdev)
{
	struct jh7110_dsp_chan *dsp_chan;
	struct jh7110_dsp_ipc *dsp_ipc;
	int i;

	dsp_ipc = dev_get_drvdata(&pdev->dev);

	for (i = 0; i < DSP_MU_CHAN_NUM; i++) {
		dsp_chan = &dsp_ipc->chans[i];
		mbox_free_channel(dsp_chan->ch);
		kfree(dsp_chan->name);
	}

	destroy_workqueue(dsp_ipc->dsp_ipc_wq);

	return 0;
}

static struct platform_driver jh7110_dsp_driver = {
	.driver = {
		.name = "jh7110-dsp",
	},
	.probe = jh7110_dsp_probe,
	.remove = jh7110_dsp_remove,
};
builtin_platform_driver(jh7110_dsp_driver);

MODULE_AUTHOR("Carter Li <carter.li@starfivetech.com>");
MODULE_DESCRIPTION("STARFIVE DSP IPC protocol driver");
MODULE_LICENSE("GPL v2");
