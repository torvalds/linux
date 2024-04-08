// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/mhi.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/sock.h>

#include "qrtr.h"

struct qrtr_mhi_dev {
	struct qrtr_endpoint ep;
	struct mhi_device *mhi_dev;
	struct device *dev;
};

/* From MHI to QRTR */
static void qcom_mhi_qrtr_dl_callback(struct mhi_device *mhi_dev,
				      struct mhi_result *mhi_res)
{
	struct qrtr_mhi_dev *qdev = dev_get_drvdata(&mhi_dev->dev);
	int rc;

	if (!qdev || mhi_res->transaction_status)
		return;

	rc = qrtr_endpoint_post(&qdev->ep, mhi_res->buf_addr,
				mhi_res->bytes_xferd);
	if (rc == -EINVAL)
		dev_err(qdev->dev, "invalid ipcrouter packet\n");
}

/* From QRTR to MHI */
static void qcom_mhi_qrtr_ul_callback(struct mhi_device *mhi_dev,
				      struct mhi_result *mhi_res)
{
	struct sk_buff *skb = mhi_res->buf_addr;

	if (skb->sk)
		sock_put(skb->sk);
	consume_skb(skb);
}

/* Send data over MHI */
static int qcom_mhi_qrtr_send(struct qrtr_endpoint *ep, struct sk_buff *skb)
{
	struct qrtr_mhi_dev *qdev = container_of(ep, struct qrtr_mhi_dev, ep);
	int rc;

	if (skb->sk)
		sock_hold(skb->sk);

	rc = skb_linearize(skb);
	if (rc)
		goto free_skb;

	rc = mhi_queue_skb(qdev->mhi_dev, DMA_TO_DEVICE, skb, skb->len,
			   MHI_EOT);
	if (rc)
		goto free_skb;

	return rc;

free_skb:
	if (skb->sk)
		sock_put(skb->sk);
	kfree_skb(skb);

	return rc;
}

static int qcom_mhi_qrtr_probe(struct mhi_device *mhi_dev,
			       const struct mhi_device_id *id)
{
	struct qrtr_mhi_dev *qdev;
	int rc;

	qdev = devm_kzalloc(&mhi_dev->dev, sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return -ENOMEM;

	qdev->mhi_dev = mhi_dev;
	qdev->dev = &mhi_dev->dev;
	qdev->ep.xmit = qcom_mhi_qrtr_send;

	dev_set_drvdata(&mhi_dev->dev, qdev);
	rc = qrtr_endpoint_register(&qdev->ep, QRTR_EP_NID_AUTO);
	if (rc)
		return rc;

	/* start channels */
	rc = mhi_prepare_for_transfer_autoqueue(mhi_dev);
	if (rc) {
		qrtr_endpoint_unregister(&qdev->ep);
		return rc;
	}

	dev_dbg(qdev->dev, "Qualcomm MHI QRTR driver probed\n");

	return 0;
}

static void qcom_mhi_qrtr_remove(struct mhi_device *mhi_dev)
{
	struct qrtr_mhi_dev *qdev = dev_get_drvdata(&mhi_dev->dev);

	qrtr_endpoint_unregister(&qdev->ep);
	mhi_unprepare_from_transfer(mhi_dev);
	dev_set_drvdata(&mhi_dev->dev, NULL);
}

static const struct mhi_device_id qcom_mhi_qrtr_id_table[] = {
	{ .chan = "IPCR" },
	{}
};
MODULE_DEVICE_TABLE(mhi, qcom_mhi_qrtr_id_table);

static int __maybe_unused qcom_mhi_qrtr_pm_suspend_late(struct device *dev)
{
	struct mhi_device *mhi_dev = container_of(dev, struct mhi_device, dev);
	enum mhi_state state;

	state = mhi_get_mhi_state(mhi_dev->mhi_cntrl);
	/*
	 * If the device is in suspend state, then no need for the
	 * client driver to unprepare the channels.
	 */
	if (state == MHI_STATE_M3)
		return 0;

	mhi_unprepare_from_transfer(mhi_dev);

	return 0;
}

static int __maybe_unused qcom_mhi_qrtr_pm_resume_early(struct device *dev)
{
	struct mhi_device *mhi_dev = container_of(dev, struct mhi_device, dev);
	enum mhi_state state;
	int rc;

	state = mhi_get_mhi_state(mhi_dev->mhi_cntrl);
	/*
	 * If the device is in suspend state, we won't unprepare channels
	 * in suspend callback, therefore no need to prepare channels when
	 * resume.
	 */
	if (state == MHI_STATE_M3)
		return 0;

	rc = mhi_prepare_for_transfer_autoqueue(mhi_dev);
	if (rc)
		dev_err(dev, "failed to prepare for autoqueue transfer %d\n", rc);

	return rc;
}

static const struct dev_pm_ops qcom_mhi_qrtr_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(qcom_mhi_qrtr_pm_suspend_late,
				     qcom_mhi_qrtr_pm_resume_early)
};

static struct mhi_driver qcom_mhi_qrtr_driver = {
	.probe = qcom_mhi_qrtr_probe,
	.remove = qcom_mhi_qrtr_remove,
	.dl_xfer_cb = qcom_mhi_qrtr_dl_callback,
	.ul_xfer_cb = qcom_mhi_qrtr_ul_callback,
	.id_table = qcom_mhi_qrtr_id_table,
	.driver = {
		.name = "qcom_mhi_qrtr",
		.pm = &qcom_mhi_qrtr_pm_ops,
	},
};

module_mhi_driver(qcom_mhi_qrtr_driver);

MODULE_AUTHOR("Chris Lew <clew@codeaurora.org>");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("Qualcomm IPC-Router MHI interface driver");
MODULE_LICENSE("GPL v2");
