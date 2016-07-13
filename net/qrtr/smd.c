/*
 * Copyright (c) 2015, Sony Mobile Communications Inc.
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/soc/qcom/smd.h>

#include "qrtr.h"

struct qrtr_smd_dev {
	struct qrtr_endpoint ep;
	struct qcom_smd_channel *channel;
	struct device *dev;
};

/* from smd to qrtr */
static int qcom_smd_qrtr_callback(struct qcom_smd_channel *channel,
				  const void *data, size_t len)
{
	struct qrtr_smd_dev *qdev = qcom_smd_get_drvdata(channel);
	int rc;

	if (!qdev)
		return -EAGAIN;

	rc = qrtr_endpoint_post(&qdev->ep, data, len);
	if (rc == -EINVAL) {
		dev_err(qdev->dev, "invalid ipcrouter packet\n");
		/* return 0 to let smd drop the packet */
		rc = 0;
	}

	return rc;
}

/* from qrtr to smd */
static int qcom_smd_qrtr_send(struct qrtr_endpoint *ep, struct sk_buff *skb)
{
	struct qrtr_smd_dev *qdev = container_of(ep, struct qrtr_smd_dev, ep);
	int rc;

	rc = skb_linearize(skb);
	if (rc)
		goto out;

	rc = qcom_smd_send(qdev->channel, skb->data, skb->len);

out:
	if (rc)
		kfree_skb(skb);
	else
		consume_skb(skb);
	return rc;
}

static int qcom_smd_qrtr_probe(struct qcom_smd_device *sdev)
{
	struct qrtr_smd_dev *qdev;
	int rc;

	qdev = devm_kzalloc(&sdev->dev, sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return -ENOMEM;

	qdev->channel = sdev->channel;
	qdev->dev = &sdev->dev;
	qdev->ep.xmit = qcom_smd_qrtr_send;

	rc = qrtr_endpoint_register(&qdev->ep, QRTR_EP_NID_AUTO);
	if (rc)
		return rc;

	qcom_smd_set_drvdata(sdev->channel, qdev);
	dev_set_drvdata(&sdev->dev, qdev);

	dev_dbg(&sdev->dev, "Qualcomm SMD QRTR driver probed\n");

	return 0;
}

static void qcom_smd_qrtr_remove(struct qcom_smd_device *sdev)
{
	struct qrtr_smd_dev *qdev = dev_get_drvdata(&sdev->dev);

	qrtr_endpoint_unregister(&qdev->ep);

	dev_set_drvdata(&sdev->dev, NULL);
}

static const struct qcom_smd_id qcom_smd_qrtr_smd_match[] = {
	{ "IPCRTR" },
	{}
};

static struct qcom_smd_driver qcom_smd_qrtr_driver = {
	.probe = qcom_smd_qrtr_probe,
	.remove = qcom_smd_qrtr_remove,
	.callback = qcom_smd_qrtr_callback,
	.smd_match_table = qcom_smd_qrtr_smd_match,
	.driver = {
		.name = "qcom_smd_qrtr",
		.owner = THIS_MODULE,
	},
};

module_qcom_smd_driver(qcom_smd_qrtr_driver);

MODULE_DESCRIPTION("Qualcomm IPC-Router SMD interface driver");
MODULE_LICENSE("GPL v2");
