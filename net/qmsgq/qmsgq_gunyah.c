// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/gunyah/gh_msgq.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/skbuff.h>
#include <linux/sizes.h>
#include <linux/types.h>
#include <linux/of.h>

#include "af_qmsgq.h"

#define QMSGQ_GH_PROTO_VER_1 1
#define MAX_PKT_SZ  SZ_64K

#define QMSGQ_SKB_WAKEUP_MS 500

enum qmsgq_gh_pkt_type {
	QMSGQ_GH_TYPE_DATA = 1,
};

/**
 * struct qmsgq_gh_hdr - qmsgq gunyah packet header
 * @version: protocol version
 * @type: packet type; one of qmsgq_gh_pkt_type
 * @flags: Reserved for future use
 * @optlen: length of optional header data
 * @size: length of packet, excluding this header and optlen
 * @src_node_id: source cid, reserved
 * @src_port_id: source port
 * @dst_node_id: destination cid, reserved
 * @dst_port_id: destination port
 */
struct qmsgq_gh_hdr {
	u8 version;
	u8 type;
	u8 flags;
	u8 optlen;
	__le32 size;
	__le32 src_rsvd;
	__le32 src_port_id;
	__le32 dst_rsvd;
	__le32 dst_port_id;
};

/* gh_transport_buf: gunyah transport buffer
 * @lock: lock for the buffer
 * @len: hdrlen + packet size
 * @copied: size of buffer copied
 * @hdr_received: true if the header is already saved, else false
 * @buf: buffer saved
 */
struct qmsgq_gh_recv_buf {
	/* @lock: lock for the buffer */
	struct mutex lock;
	size_t len;
	size_t copied;
	bool hdr_received;

	char buf[MAX_PKT_SZ];
};

/* qmsgq_gh_device: vm devices attached to this transport
 * @item: list item of all vm devices
 * @dev: device from platform_device.
 * @peer_cid: remote cid
 * @master: primary vm indicator
 * @msgq_label: msgq label
 * @msgq_hdl: msgq handle
 * @rm_nb: notifier block for vm status from rm
 * @tx_lock: tx lock to queue only one packet at a time
 * @rx_thread: rx thread to receive incoming packets
 * @ep: qmsq endpoint
 * @sock_ws: wakeup source
 */
struct qmsgq_gh_device {
	struct list_head item;
	struct device *dev;
	struct qmsgq_endpoint ep;

	unsigned int peer_cid;
	bool master;
	enum gh_msgq_label msgq_label;
	void *msgq_hdl;
	struct notifier_block rm_nb;

	struct wakeup_source *sock_ws;

	/* @tx_lock: tx lock to queue only one packet at a time */
	struct mutex tx_lock;
	struct task_struct *rx_thread;
	struct qmsgq_gh_recv_buf rx_buf;
};

static void reset_buf(struct qmsgq_gh_recv_buf *rx_buf)
{
	memset(rx_buf->buf, 0, MAX_PKT_SZ);
	rx_buf->hdr_received = false;
	rx_buf->copied = 0;
	rx_buf->len = 0;
}

static int qmsgq_gh_post(struct qmsgq_gh_device *qdev, struct qmsgq_gh_recv_buf *rx_buf)
{
	unsigned int cid, port, len;
	struct qmsgq_gh_hdr *hdr;
	struct sockaddr_vm src;
	struct sockaddr_vm dst;
	void *data;
	int rc;

	if (rx_buf->len < sizeof(*hdr)) {
		pr_err("%s: len: %d < hdr size\n", __func__, rx_buf->len);
		return -EINVAL;
	}
	hdr = (struct qmsgq_gh_hdr *)rx_buf->buf;

	if (hdr->type != QMSGQ_GH_TYPE_DATA)
		return -EINVAL;

	cid = le32_to_cpu(hdr->src_rsvd);
	port = le32_to_cpu(hdr->src_port_id);
	vsock_addr_init(&src, cid, port);

	cid = le32_to_cpu(hdr->dst_rsvd);
	port = le32_to_cpu(hdr->dst_port_id);
	vsock_addr_init(&dst, cid, port);

	data = rx_buf->buf + sizeof(*hdr);
	len = rx_buf->len - sizeof(*hdr);

	rc = qmsgq_post(&qdev->ep, &src, &dst, data, len);

	return rc;
}

static void qmsgq_process_recv(struct qmsgq_gh_device *qdev, void *buf, size_t len)
{
	struct qmsgq_gh_recv_buf *rx_buf = &qdev->rx_buf;
	struct qmsgq_gh_hdr *hdr;
	size_t n;

	mutex_lock(&rx_buf->lock);

	/* Copy message into the local buffer */
	n = (rx_buf->copied + len < MAX_PKT_SZ) ? len : MAX_PKT_SZ - rx_buf->copied;
	memcpy(rx_buf->buf + rx_buf->copied, buf, n);
	rx_buf->copied += n;

	if (!rx_buf->hdr_received) {
		hdr = (struct qmsgq_gh_hdr *)rx_buf->buf;

		if (hdr->version != QMSGQ_GH_PROTO_VER_1) {
			pr_err("%s: Incorrect version:%d\n", __func__, hdr->version);
			goto err;
		}
		if (hdr->type != QMSGQ_GH_TYPE_DATA) {
			pr_err("%s: Incorrect type:%d\n", __func__, hdr->type);
			goto err;
		}
		if (hdr->size > MAX_PKT_SZ - sizeof(*hdr)) {
			pr_err("%s: Packet size too big:%d\n", __func__, hdr->size);
			goto err;
		}

		rx_buf->len = sizeof(*hdr) + hdr->size;
		rx_buf->hdr_received = true;
	}

	/* Check len size, can not be smaller than amount copied*/
	if (rx_buf->len < rx_buf->copied) {
		pr_err("%s: Size mismatch len:%d, copied:%d\n", __func__,
		       rx_buf->len, rx_buf->copied);
		goto err;
	}

	if (rx_buf->len == rx_buf->copied) {
		qmsgq_gh_post(qdev, rx_buf);
		reset_buf(rx_buf);
	}

	mutex_unlock(&rx_buf->lock);
	return;

err:
	reset_buf(rx_buf);
	mutex_unlock(&rx_buf->lock);
}

static int qmsgq_gh_msgq_recv(void *data)
{
	struct qmsgq_gh_device *qdev = data;
	size_t size;
	void *buf;
	int rc;

	buf = kzalloc(GH_MSGQ_MAX_MSG_SIZE_BYTES, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	while (!kthread_should_stop()) {
		rc = gh_msgq_recv(qdev->msgq_hdl, buf, GH_MSGQ_MAX_MSG_SIZE_BYTES, &size,
				  GH_MSGQ_TX_PUSH);
		if (rc)
			continue;

		if (size <= 0)
			continue;

		qmsgq_process_recv(qdev, buf, size);
		pm_wakeup_ws_event(qdev->sock_ws, QMSGQ_SKB_WAKEUP_MS, true);
	}
	kfree(buf);

	return 0;
}

static int qmsgq_gh_send(struct qmsgq_gh_device *qdev, void *buf, size_t len)
{
	size_t left, chunk, offset;
	int rc = 0;

	left = len;
	chunk = 0;
	offset = 0;

	mutex_lock(&qdev->tx_lock);
	while (left > 0) {
		chunk = (left > GH_MSGQ_MAX_MSG_SIZE_BYTES) ? GH_MSGQ_MAX_MSG_SIZE_BYTES : left;
		rc = gh_msgq_send(qdev->msgq_hdl, buf + offset, chunk, GH_MSGQ_TX_PUSH);
		if (rc) {
			pr_err("%s: gh_msgq_send failed: %d\n", __func__, rc);
			mutex_unlock(&qdev->tx_lock);
			goto err;
		}
		left -= chunk;
		offset += chunk;
	}
	mutex_unlock(&qdev->tx_lock);
	return 0;

err:
	return rc;
}

static int qmsgq_gh_dgram_enqueue(struct qmsgq_sock *qsk, struct sockaddr_vm *remote,
				  struct msghdr *msg, size_t len)
{
	struct sockaddr_vm *local_addr = &qsk->local_addr;
	const struct qmsgq_endpoint *ep;
	struct qmsgq_gh_device *qdev;
	struct qmsgq_gh_hdr *hdr;
	char *buf;
	int rc;

	ep = qsk->ep;
	if (!ep)
		return -ENXIO;
	qdev = container_of(ep, struct qmsgq_gh_device, ep);

	if (!qdev->msgq_hdl) {
		pr_err("%s: Transport not ready\n", __func__);
		return -ENODEV;
	}

	if (len > MAX_PKT_SZ - sizeof(*hdr)) {
		pr_err("%s: Invalid pk size: len: %lu\n", __func__, len);
		return -EMSGSIZE;
	}

	/* Allocate a buffer for the user's message and our packet header. */
	buf = kmalloc(len + sizeof(*hdr), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Populate Header */
	hdr = (struct qmsgq_gh_hdr *)buf;
	hdr->version = QMSGQ_GH_PROTO_VER_1;
	hdr->type = QMSGQ_GH_TYPE_DATA;
	hdr->flags = 0;
	hdr->optlen = 0;
	hdr->size = len;
	hdr->src_rsvd = 0;
	hdr->src_port_id = local_addr->svm_port;
	hdr->dst_rsvd = 0;
	hdr->dst_port_id = remote->svm_port;
	rc = memcpy_from_msg((void *)buf + sizeof(*hdr), msg, len);
	if (rc) {
		pr_err("%s failed: memcpy_from_msg rc: %d\n", __func__, rc);
		goto send_err;
	}

	pr_debug("TX DATA: Len:0x%x src[0x%x] dst[0x%x]\n", len, hdr->src_port_id,
		 hdr->dst_port_id);

	rc = qmsgq_gh_send(qdev, buf, len + sizeof(*hdr));
	if (rc < 0) {
		pr_err("%s: failed to send msg rc: %d\n", __func__, rc);
		goto send_err;
	}
	kfree(buf);

	return 0;

send_err:
	kfree(buf);
	return rc;
}

static int qmsgq_gh_socket_init(struct qmsgq_sock *qsk, struct qmsgq_sock *psk)
{
	return 0;
}

static void qmsgq_gh_destruct(struct qmsgq_sock *qsk)
{
}

static void qmsgq_gh_release(struct qmsgq_sock *qsk)
{
}

static bool qmsgq_gh_allow_rsvd_cid(u32 cid)
{
	/* Allowing for cid 0 as of now as af_qmsgq sends 0 if no cid is
	 * passed by the client.
	 */
	if (cid == 0)
		return true;

	return false;
}

static bool qmsgq_gh_dgram_allow(u32 cid, u32 port)
{
	if (qmsgq_gh_allow_rsvd_cid(cid) || cid == VMADDR_CID_ANY || cid == VMADDR_CID_HOST)
		return true;

	pr_err("%s: dgram not allowed for cid 0x%x\n", __func__, cid);

	return false;
}

static int qmsgq_gh_shutdown(struct qmsgq_sock *qsk, int mode)
{
	return 0;
}

static u32 qmsgq_gh_get_local_cid(void)
{
	return VMADDR_CID_HOST;
}

static int qmsgq_gh_msgq_start(struct qmsgq_gh_device *qdev)
{
	struct device *dev = qdev->dev;
	int rc;

	if (qdev->msgq_hdl) {
		dev_err(qdev->dev, "Already have msgq handle!\n");
		return NOTIFY_DONE;
	}

	qdev->msgq_hdl = gh_msgq_register(qdev->msgq_label);
	if (IS_ERR_OR_NULL(qdev->msgq_hdl)) {
		rc = PTR_ERR(qdev->msgq_hdl);
		dev_err(dev, "msgq register failed rc:%d\n", rc);
		return rc;
	}

	qdev->rx_thread = kthread_run(qmsgq_gh_msgq_recv, qdev, "qmsgq_gh_rx");
	if (IS_ERR_OR_NULL(qdev->rx_thread)) {
		rc = PTR_ERR(qdev->rx_thread);
		dev_err(dev, "Failed to create rx thread rc:%d\n", rc);
		return rc;
	}

	return 0;
}

static int qmsgq_gh_rm_cb(struct notifier_block *nb, unsigned long cmd, void *data)
{
	struct qmsgq_gh_device *qdev = container_of(nb, struct qmsgq_gh_device, rm_nb);
	struct gh_rm_notif_vm_status_payload *vm_status_payload = data;
	u8 vm_status = vm_status_payload->vm_status;
	int rc;

	if (cmd != GH_RM_NOTIF_VM_STATUS)
		return NOTIFY_DONE;

	/* TODO - check for peer */
	switch (vm_status) {
	case GH_RM_VM_STATUS_READY:
		rc = qmsgq_gh_msgq_start(qdev);
		break;
	default:
		pr_debug("Unknown notification for vmid = %d vm_status = %d\n",
			 vm_status_payload->vmid, vm_status);
	}

	return NOTIFY_DONE;
}

static int qmsgq_gh_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct qmsgq_gh_device *qdev;
	int rc;

	qdev = devm_kzalloc(dev, sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return -ENOMEM;
	qdev->dev = dev;
	dev_set_drvdata(&pdev->dev, qdev);

	mutex_init(&qdev->tx_lock);
	mutex_init(&qdev->rx_buf.lock);
	qdev->rx_buf.len = 0;
	qdev->rx_buf.copied = 0;
	qdev->rx_buf.hdr_received = false;

	qdev->ep.module = THIS_MODULE;
	qdev->ep.init = qmsgq_gh_socket_init;
	qdev->ep.destruct = qmsgq_gh_destruct;
	qdev->ep.release = qmsgq_gh_release;
	qdev->ep.dgram_enqueue = qmsgq_gh_dgram_enqueue;
	qdev->ep.dgram_allow = qmsgq_gh_dgram_allow;
	qdev->ep.shutdown = qmsgq_gh_shutdown;
	qdev->ep.get_local_cid = qmsgq_gh_get_local_cid;

	//TODO properly set this
	qdev->peer_cid = 0;

	qdev->sock_ws = wakeup_source_register(NULL, "qmsgq_sock_ws");

	rc = of_property_read_u32(np, "msgq-label", &qdev->msgq_label);
	if (rc) {
		dev_err(dev, "failed to read msgq-label info %d\n", rc);
		return rc;
	}

	qdev->master = of_property_read_bool(np, "qcom,master");
	if (qdev->master) {
		qdev->rm_nb.notifier_call = qmsgq_gh_rm_cb;
		gh_rm_register_notifier(&qdev->rm_nb);
	} else {
		rc = qmsgq_gh_msgq_start(qdev);
	}
	qmsgq_endpoint_register(&qdev->ep);

	return rc;
}

static int qmsgq_gh_remove(struct platform_device *pdev)
{
	struct qmsgq_gh_device *qdev = dev_get_drvdata(&pdev->dev);

	if (qdev->master)
		gh_rm_unregister_notifier(&qdev->rm_nb);

	if (qdev->rx_thread)
		kthread_stop(qdev->rx_thread);

	qmsgq_endpoint_unregister(&qdev->ep);

	return 0;
}

static const struct of_device_id qmsgq_gh_of_match[] = {
	{ .compatible = "qcom,qmsgq-gh" },
	{}
};
MODULE_DEVICE_TABLE(of, qmsgq_gh_of_match);

static struct platform_driver qmsgq_gh_driver = {
	.probe	= qmsgq_gh_probe,
	.remove	= qmsgq_gh_remove,
	.driver = {
		.name	= "qmsgq-gh",
		.of_match_table = qmsgq_gh_of_match,
	}
};
module_platform_driver(qmsgq_gh_driver);

MODULE_ALIAS("gunyah:QMSGQ");
MODULE_DESCRIPTION("Gunyah QMSGQ Transport driver");
MODULE_LICENSE("GPL");
