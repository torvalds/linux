// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/genalloc.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/sizes.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include "qrtr.h"

#define MAX_PKT_SZ		SZ_64K
#define LABEL_SIZE		32

#define FIFO_FULL_RESERVE	8
#define FIFO_SIZE		0x4000

#define HDR_KEY_VALUE		0xdead

#define MAGIC_KEY_VALUE		0x24495043 /* "$IPC" */
#define MAGIC_KEY		0x0
#define BUFFER_SIZE		0x4

#define FIFO_0_START_OFFSET	0x1000
#define FIFO_0_BASE		0x8
#define FIFO_0_SIZE		0xc
#define FIFO_0_TAIL		0x10
#define FIFO_0_HEAD		0x14
#define FIFO_0_NOTIFY		0x18

#define FIFO_1_START_OFFSET	(FIFO_0_START_OFFSET + FIFO_SIZE)
#define FIFO_1_BASE		0x1c
#define FIFO_1_SIZE		0x20
#define FIFO_1_TAIL		0x24
#define FIFO_1_HEAD		0x28
#define FIFO_1_NOTIFY		0x2c

#define LOCAL_STATE		0x30

#define IRQ_SETUP_IDX		0
#define IRQ_XFER_IDX		1

#define STATE_WAIT_TIMEOUT	msecs_to_jiffies(1000)

enum {
	LOCAL_STATE_DEFAULT,
	LOCAL_STATE_INIT,
	LOCAL_STATE_START,
	LOCAL_STATE_PREPARE_REBOOT,
	LOCAL_STATE_REBOOT,
};

struct qrtr_genpool_hdr {
	__le16 len;
	__le16 magic;
};

struct qrtr_genpool_ring {
	void *buf;
	size_t len;
	u32 offset;
};

struct qrtr_genpool_pipe {
	__le32 *tail;
	__le32 *head;
	__le32 *read_notify;

	void *fifo;
	size_t length;
};

/**
 * qrtr_genpool_dev - qrtr genpool fifo transport structure
 * @ep: qrtr endpoint specific info
 * @ep_registered: tracks the registration state of the qrtr endpoint
 * @dev: device from platform_device
 * @label: label of the edge on the other side
 * @ring: buf for reading from fifo
 * @rx_pipe: RX genpool fifo specific info
 * @tx_pipe: TX genpool fifo specific info
 * @tx_avail_notify: wait queue for available tx
 * @pool: handle to gen_pool framework
 * @dma_addr: dma_addr of shared fifo
 * @base: base of the shared fifo
 * @size: fifo size
 * @mbox_client: mailbox client signaling
 * @mbox_setup_chan: mailbox channel for setup
 * @mbox_xfer_chan: mailbox channel for transfers
 * @irq_setup: IRQ for signaling completion of fifo setup
 * @irq_setup_label: IRQ name for irq_setup
 * @setup_work: worker to maintain shared memory between edges
 * @irq_xfer: IRQ for incoming transfers
 * @irq_xfer_label: IRQ name for irq_xfer
 * @state: current state of the local side
 * @lock: lock for updating local state
 * @state_wait: wait queue for specific state
 * @reboot_handler: handle for getting reboot notifications
 */
struct qrtr_genpool_dev {
	struct qrtr_endpoint ep;
	bool ep_registered;
	struct device *dev;
	const char *label;
	struct qrtr_genpool_ring ring;
	struct qrtr_genpool_pipe rx_pipe;
	struct qrtr_genpool_pipe tx_pipe;
	wait_queue_head_t tx_avail_notify;

	struct gen_pool *pool;
	dma_addr_t dma_addr;
	void *base;
	size_t size;

	struct mbox_client mbox_client;
	struct mbox_chan *mbox_setup_chan;
	struct mbox_chan *mbox_xfer_chan;

	int irq_setup;
	char irq_setup_label[LABEL_SIZE];
	struct work_struct setup_work;

	int irq_xfer;
	char irq_xfer_label[LABEL_SIZE];

	u32 state;
	spinlock_t lock; /* lock for local state updates */
	wait_queue_head_t state_wait;

	struct notifier_block reboot_handler;
};

static void qrtr_genpool_set_state(struct qrtr_genpool_dev *qdev, u32 state)
{
	qdev->state = state;
	*(u32 *)(qdev->base + LOCAL_STATE) = cpu_to_le32(qdev->state);
}

static void qrtr_genpool_signal(struct qrtr_genpool_dev *qdev,
				struct mbox_chan *mbox_chan)
{
	mbox_send_message(mbox_chan, NULL);
	mbox_client_txdone(mbox_chan, 0);
}

static void qrtr_genpool_signal_setup(struct qrtr_genpool_dev *qdev)
{
	qrtr_genpool_signal(qdev, qdev->mbox_setup_chan);
}

static void qrtr_genpool_signal_xfer(struct qrtr_genpool_dev *qdev)
{
	qrtr_genpool_signal(qdev, qdev->mbox_xfer_chan);
}

static void qrtr_genpool_tx_write(struct qrtr_genpool_pipe *pipe, const void *data,
				  size_t count)
{
	size_t len;
	u32 head;

	head = le32_to_cpu(*pipe->head);

	len = min_t(size_t, count, pipe->length - head);
	if (len)
		memcpy_toio(pipe->fifo + head, data, len);

	if (len != count)
		memcpy_toio(pipe->fifo, data + len, count - len);

	head += count;
	if (head >= pipe->length)
		head %= pipe->length;

	/* Ensure ordering of fifo and head update */
	smp_wmb();

	*pipe->head = cpu_to_le32(head);
}

static void qrtr_genpool_clr_tx_notify(struct qrtr_genpool_dev *qdev)
{
	*qdev->tx_pipe.read_notify = 0;
}

static void qrtr_genpool_set_tx_notify(struct qrtr_genpool_dev *qdev)
{
	*qdev->tx_pipe.read_notify = cpu_to_le32(1);
}

static size_t qrtr_genpool_tx_avail(struct qrtr_genpool_pipe *pipe)
{
	u32 avail;
	u32 head;
	u32 tail;

	head = le32_to_cpu(*pipe->head);
	tail = le32_to_cpu(*pipe->tail);

	if (tail <= head)
		avail = pipe->length - head + tail;
	else
		avail = tail - head;

	if (avail < FIFO_FULL_RESERVE)
		avail = 0;
	else
		avail -= FIFO_FULL_RESERVE;

	return avail;
}

static void qrtr_genpool_wait_for_tx_avail(struct qrtr_genpool_dev *qdev)
{
	qrtr_genpool_set_tx_notify(qdev);
	wait_event_timeout(qdev->tx_avail_notify,
			   qrtr_genpool_tx_avail(&qdev->tx_pipe), 10 * HZ);
}

static void qrtr_genpool_generate_hdr(struct qrtr_genpool_dev *qdev,
				      struct qrtr_genpool_hdr *hdr)
{
	size_t hdr_len = sizeof(*hdr);

	while (qrtr_genpool_tx_avail(&qdev->tx_pipe) < hdr_len)
		qrtr_genpool_wait_for_tx_avail(qdev);

	qrtr_genpool_tx_write(&qdev->tx_pipe, hdr, hdr_len);
};

/* from qrtr to genpool fifo */
static int qrtr_genpool_send(struct qrtr_endpoint *ep, struct sk_buff *skb)
{
	struct qrtr_genpool_dev *qdev;
	struct qrtr_genpool_hdr hdr;
	size_t tx_avail;
	int chunk_size;
	int left_size;
	int offset;
	int rc;

	qdev = container_of(ep, struct qrtr_genpool_dev, ep);

	rc = skb_linearize(skb);
	if (rc) {
		kfree_skb(skb);
		return rc;
	}

	hdr.len = cpu_to_le16(skb->len);
	hdr.magic = cpu_to_le16(HDR_KEY_VALUE);
	qrtr_genpool_generate_hdr(qdev, &hdr);

	left_size = skb->len;
	offset = 0;
	while (left_size > 0) {
		tx_avail = qrtr_genpool_tx_avail(&qdev->tx_pipe);
		if (!tx_avail) {
			qrtr_genpool_wait_for_tx_avail(qdev);
			continue;
		}

		if (tx_avail < left_size)
			chunk_size = tx_avail;
		else
			chunk_size = left_size;

		qrtr_genpool_tx_write(&qdev->tx_pipe, skb->data + offset,
				      chunk_size);
		offset += chunk_size;
		left_size -= chunk_size;

		qrtr_genpool_signal_xfer(qdev);
	}
	qrtr_genpool_clr_tx_notify(qdev);
	kfree_skb(skb);

	return 0;
}

static size_t qrtr_genpool_rx_avail(struct qrtr_genpool_pipe *pipe)
{
	size_t len;
	u32 head;
	u32 tail;

	head = le32_to_cpu(*pipe->head);
	tail = le32_to_cpu(*pipe->tail);

	if (head < tail)
		len = pipe->length - tail + head;
	else
		len = head - tail;

	if (WARN_ON_ONCE(len > pipe->length))
		len = 0;

	return len;
}

static void qrtr_genpool_rx_advance(struct qrtr_genpool_pipe *pipe, size_t count)
{
	u32 tail;

	tail = le32_to_cpu(*pipe->tail);
	tail += count;
	if (tail >= pipe->length)
		tail %= pipe->length;

	*pipe->tail = cpu_to_le32(tail);
}

static void qrtr_genpool_rx_peak(struct qrtr_genpool_pipe *pipe, void *data,
				 unsigned int offset, size_t count)
{
	size_t len;
	u32 tail;

	tail = le32_to_cpu(*pipe->tail);
	tail += offset;
	if (tail >= pipe->length)
		tail %= pipe->length;

	len = min_t(size_t, count, pipe->length - tail);
	if (len)
		memcpy_fromio(data, pipe->fifo + tail, len);

	if (len != count)
		memcpy_fromio(data + len, pipe->fifo, count - len);
}

static bool qrtr_genpool_get_read_notify(struct qrtr_genpool_dev *qdev)
{
	return le32_to_cpu(*qdev->rx_pipe.read_notify);
}

static void qrtr_genpool_read_new(struct qrtr_genpool_dev *qdev)
{
	struct qrtr_genpool_ring *ring = &qdev->ring;
	struct qrtr_genpool_hdr hdr = {0, 0};
	size_t rx_avail;
	size_t pkt_len;
	size_t hdr_len;
	int rc;

	/* copy hdr from rx_pipe and check hdr for pkt size */
	hdr_len = sizeof(hdr);
	qrtr_genpool_rx_peak(&qdev->rx_pipe, &hdr, 0, hdr_len);
	pkt_len = le16_to_cpu(hdr.len);
	if (pkt_len > MAX_PKT_SZ) {
		dev_err(qdev->dev, "invalid pkt_len %zu\n", pkt_len);
		return;
	}
	qrtr_genpool_rx_advance(&qdev->rx_pipe, hdr_len);

	rx_avail = qrtr_genpool_rx_avail(&qdev->rx_pipe);
	if (rx_avail > pkt_len)
		rx_avail = pkt_len;

	qrtr_genpool_rx_peak(&qdev->rx_pipe, ring->buf, 0, rx_avail);
	qrtr_genpool_rx_advance(&qdev->rx_pipe, rx_avail);

	if (rx_avail == pkt_len) {
		rc = qrtr_endpoint_post(&qdev->ep, ring->buf, pkt_len);
		if (rc == -EINVAL)
			dev_err(qdev->dev, "invalid ipcrouter packet\n");
	} else {
		ring->len = pkt_len;
		ring->offset = rx_avail;
	}
}

static void qrtr_genpool_read_frag(struct qrtr_genpool_dev *qdev)
{
	struct qrtr_genpool_ring *ring = &qdev->ring;
	size_t rx_avail;
	int rc;

	rx_avail = qrtr_genpool_rx_avail(&qdev->rx_pipe);
	if (rx_avail + ring->offset > ring->len)
		rx_avail = ring->len - ring->offset;

	qrtr_genpool_rx_peak(&qdev->rx_pipe, ring->buf + ring->offset, 0, rx_avail);
	qrtr_genpool_rx_advance(&qdev->rx_pipe, rx_avail);

	if (rx_avail + ring->offset == ring->len) {
		rc = qrtr_endpoint_post(&qdev->ep, ring->buf, ring->len);
		if (rc == -EINVAL)
			dev_err(qdev->dev, "invalid ipcrouter packet\n");
		ring->offset = 0;
		ring->len = 0;
	} else {
		ring->offset += rx_avail;
	}
}

static void qrtr_genpool_read(struct qrtr_genpool_dev *qdev)
{
	wake_up_all(&qdev->tx_avail_notify);

	while (qrtr_genpool_rx_avail(&qdev->rx_pipe)) {
		if (qdev->ring.offset)
			qrtr_genpool_read_frag(qdev);
		else
			qrtr_genpool_read_new(qdev);

		if (qrtr_genpool_get_read_notify(qdev))
			qrtr_genpool_signal_xfer(qdev);
	}
}

static void qrtr_genpool_memory_free(struct qrtr_genpool_dev *qdev)
{
	if (!qdev->base)
		return;

	gen_pool_free(qdev->pool, (unsigned long)qdev->base, qdev->size);
	qdev->base = NULL;
	qdev->dma_addr = 0;
	qdev->size = 0;
}

static int qrtr_genpool_memory_alloc(struct qrtr_genpool_dev *qdev)
{
	qdev->size = gen_pool_size(qdev->pool);
	qdev->base = gen_pool_dma_alloc(qdev->pool, qdev->size, &qdev->dma_addr);
	if (!qdev->base) {
		dev_err(qdev->dev, "failed to dma alloc\n");
		return -ENOMEM;
	}

	return 0;
}

static irqreturn_t qrtr_genpool_setup_intr(int irq, void *data)
{
	struct qrtr_genpool_dev *qdev = data;

	schedule_work(&qdev->setup_work);

	return IRQ_HANDLED;
}

static irqreturn_t qrtr_genpool_xfer_intr(int irq, void *data)
{
	struct qrtr_genpool_dev *qdev = data;
	unsigned long flags;

	spin_lock_irqsave(&qdev->lock, flags);
	if (qdev->state != LOCAL_STATE_START) {
		spin_unlock_irqrestore(&qdev->lock, flags);
		return IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&qdev->lock, flags);

	qrtr_genpool_read(qdev);

	return IRQ_HANDLED;
}

static int qrtr_genpool_irq_init(struct qrtr_genpool_dev *qdev)
{
	struct device *dev = qdev->dev;
	int irq, rc;

	irq = of_irq_get(dev->of_node, IRQ_XFER_IDX);
	if (irq < 0)
		return irq;

	qdev->irq_xfer = irq;
	snprintf(qdev->irq_xfer_label, LABEL_SIZE, "%s-xfer", qdev->label);
	rc = devm_request_irq(dev, qdev->irq_xfer, qrtr_genpool_xfer_intr, 0,
			      qdev->irq_xfer_label, qdev);
	if (rc) {
		dev_err(dev, "failed to request xfer IRQ: %d\n", rc);
		return rc;
	}
	enable_irq_wake(qdev->irq_xfer);

	irq = of_irq_get(dev->of_node, IRQ_SETUP_IDX);
	if (irq < 0)
		return irq;

	qdev->irq_setup = irq;
	snprintf(qdev->irq_setup_label, LABEL_SIZE, "%s-setup", qdev->label);
	rc = devm_request_irq(dev, qdev->irq_setup, qrtr_genpool_setup_intr, 0,
			      qdev->irq_setup_label, qdev);
	if (rc) {
		dev_err(dev, "failed to request setup IRQ: %d\n", rc);
		return rc;
	}
	enable_irq_wake(qdev->irq_setup);

	return 0;
}

static int qrtr_genpool_mbox_init(struct qrtr_genpool_dev *qdev)
{
	struct device *dev = qdev->dev;
	int rc;

	qdev->mbox_client.dev = dev;
	qdev->mbox_client.knows_txdone = true;
	qdev->mbox_setup_chan = mbox_request_channel(&qdev->mbox_client, IRQ_SETUP_IDX);
	if (IS_ERR(qdev->mbox_setup_chan)) {
		rc = PTR_ERR(qdev->mbox_setup_chan);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "failed to acquire IPC setup channel %d\n", rc);
		return rc;
	}

	qdev->mbox_xfer_chan = mbox_request_channel(&qdev->mbox_client, IRQ_XFER_IDX);
	if (IS_ERR(qdev->mbox_xfer_chan)) {
		rc = PTR_ERR(qdev->mbox_xfer_chan);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "failed to acquire IPC xfer channel %d\n", rc);
		return rc;
	}

	return 0;
}

/**
 * qrtr_genpool_fifo_init() - init genpool fifo configs
 *
 * @return: 0 on success, standard Linux error codes on error.
 *
 * This function is called to initialize the genpool fifo pointer with
 * the genpool fifo configurations.
 */
static void qrtr_genpool_fifo_init(struct qrtr_genpool_dev *qdev)
{
	u8 *descs;

	memset(qdev->base, 0, FIFO_0_START_OFFSET);
	descs = qdev->base;
	*(u32 *)(descs + MAGIC_KEY) = MAGIC_KEY_VALUE;
	*(u32 *)(descs + BUFFER_SIZE) = qdev->size;

	*(u32 *)(descs + FIFO_0_BASE) = FIFO_0_START_OFFSET;
	*(u32 *)(descs + FIFO_0_SIZE) = FIFO_SIZE;
	qdev->tx_pipe.fifo = (u32 *)(descs + FIFO_0_START_OFFSET);
	qdev->tx_pipe.tail = (u32 *)(descs + FIFO_0_TAIL);
	qdev->tx_pipe.head = (u32 *)(descs + FIFO_0_HEAD);
	qdev->tx_pipe.read_notify = (u32 *)(descs + FIFO_0_NOTIFY);
	qdev->tx_pipe.length = FIFO_SIZE;

	*(u32 *)(descs + FIFO_1_BASE) = FIFO_1_START_OFFSET;
	*(u32 *)(descs + FIFO_1_SIZE) = FIFO_SIZE;
	qdev->rx_pipe.fifo = (u32 *)(descs + FIFO_1_START_OFFSET);
	qdev->rx_pipe.tail = (u32 *)(descs + FIFO_1_TAIL);
	qdev->rx_pipe.head = (u32 *)(descs + FIFO_1_HEAD);
	qdev->rx_pipe.read_notify = (u32 *)(descs + FIFO_1_NOTIFY);
	qdev->rx_pipe.length = FIFO_SIZE;

	/* Reset respective index */
	*qdev->tx_pipe.head = 0;
	*qdev->rx_pipe.tail = 0;
}

static int qrtr_genpool_memory_init(struct qrtr_genpool_dev *qdev)
{
	struct device_node *np;

	np = of_parse_phandle(qdev->dev->of_node, "gen-pool", 0);
	if (!np) {
		dev_err(qdev->dev, "failed to parse gen-pool\n");
		return -ENODEV;
	}

	qdev->pool = of_gen_pool_get(np, "qrtr-gen-pool", 0);
	of_node_put(np);
	if (!qdev->pool)
		return -EPROBE_DEFER;

	/* check if pool has any entries */
	if (!gen_pool_avail(qdev->pool))
		return -EPROBE_DEFER;

	return 0;
}

static void qrtr_genpool_setup_work(struct work_struct *work)
{
	struct qrtr_genpool_dev *qdev = container_of(work, struct qrtr_genpool_dev, setup_work);
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&qdev->lock, flags);
	if (qdev->state == LOCAL_STATE_REBOOT) {
		spin_unlock_irqrestore(&qdev->lock, flags);
		return;
	}

	if (qdev->state == LOCAL_STATE_PREPARE_REBOOT) {
		qrtr_genpool_set_state(qdev, LOCAL_STATE_REBOOT);
		spin_unlock_irqrestore(&qdev->lock, flags);
		wake_up_all(&qdev->state_wait);
		return;
	}
	spin_unlock_irqrestore(&qdev->lock, flags);

	disable_irq(qdev->irq_xfer);

	if (qdev->ep_registered) {
		qrtr_endpoint_unregister(&qdev->ep);
		qdev->ep_registered = false;
	}

	qrtr_genpool_memory_free(qdev);

	rc = qrtr_genpool_memory_alloc(qdev);
	if (rc)
		return;

	qrtr_genpool_fifo_init(qdev);

	qdev->ep.xmit = qrtr_genpool_send;
	rc = qrtr_endpoint_register(&qdev->ep, QRTR_EP_NET_ID_AUTO, false, NULL);
	if (rc) {
		dev_err(qdev->dev, "failed to register qrtr endpoint rc%d\n", rc);
		return;
	}
	qdev->ep_registered = true;

	enable_irq(qdev->irq_xfer);

	spin_lock_irqsave(&qdev->lock, flags);
	qrtr_genpool_set_state(qdev, LOCAL_STATE_START);
	qrtr_genpool_signal_setup(qdev);
	spin_unlock_irqrestore(&qdev->lock, flags);
}

static int qrtr_genpool_reboot_cb(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct qrtr_genpool_dev *qdev = container_of(nb, struct qrtr_genpool_dev, reboot_handler);
	unsigned long flags;
	int rc;

	cancel_work_sync(&qdev->setup_work);

	spin_lock_irqsave(&qdev->lock, flags);
	qrtr_genpool_set_state(qdev, LOCAL_STATE_PREPARE_REBOOT);
	qrtr_genpool_signal_setup(qdev);

	rc = wait_event_lock_irq_timeout(qdev->state_wait,
					 qdev->state == LOCAL_STATE_REBOOT,
					 qdev->lock,
					 STATE_WAIT_TIMEOUT);
	if (!rc)
		dev_dbg(qdev->dev, "timedout waiting for reboot state change\n");
	spin_unlock_irqrestore(&qdev->lock, flags);

	disable_irq(qdev->irq_xfer);

	if (qdev->ep_registered) {
		qrtr_endpoint_unregister(&qdev->ep);
		qdev->ep_registered = false;
	}

	qrtr_genpool_memory_free(qdev);

	vfree(qdev->ring.buf);
	qdev->ring.buf = NULL;

	return NOTIFY_DONE;
}

/**
 * qrtr_genpool_probe() - Probe a genpool fifo transport
 *
 * @pdev: Platform device corresponding to genpool fifo transport.
 *
 * @return: 0 on success, standard Linux error codes on error.
 *
 * This function is called when the underlying device tree driver registers
 * a platform device, mapped to a genpool fifo transport.
 */
static int qrtr_genpool_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qrtr_genpool_dev *qdev;
	int rc;

	qdev = devm_kzalloc(dev, sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return -ENOMEM;

	qdev->dev = dev;
	dev_set_drvdata(dev, qdev);

	rc = of_property_read_string(dev->of_node, "label", &qdev->label);
	if (rc < 0)
		qdev->label = dev->of_node->name;

	qdev->ring.buf = vzalloc(MAX_PKT_SZ);
	if (!qdev->ring.buf)
		return -ENOMEM;

	rc = qrtr_genpool_memory_init(qdev);
	if (rc)
		goto err;

	init_waitqueue_head(&qdev->tx_avail_notify);
	init_waitqueue_head(&qdev->state_wait);
	INIT_WORK(&qdev->setup_work, qrtr_genpool_setup_work);
	spin_lock_init(&qdev->lock);

	qdev->reboot_handler.notifier_call = qrtr_genpool_reboot_cb;
	rc = devm_register_reboot_notifier(qdev->dev, &qdev->reboot_handler);
	if (rc) {
		dev_err(qdev->dev, "failed to register reboot notifier rc%d\n", rc);
		goto err;
	}

	rc = qrtr_genpool_mbox_init(qdev);
	if (rc)
		goto err;

	rc = qrtr_genpool_irq_init(qdev);
	if (rc)
		goto err;

	return 0;

err:
	vfree(qdev->ring.buf);

	return rc;
}

static int qrtr_genpool_remove(struct platform_device *pdev)
{
	struct qrtr_genpool_dev *qdev = platform_get_drvdata(pdev);

	cancel_work_sync(&qdev->setup_work);

	if (qdev->ep_registered)
		qrtr_endpoint_unregister(&qdev->ep);

	vfree(qdev->ring.buf);

	return 0;
}

static const struct of_device_id qrtr_genpool_match_table[] = {
	{ .compatible = "qcom,qrtr-genpool" },
	{},
};

static struct platform_driver qrtr_genpool_driver = {
	.probe = qrtr_genpool_probe,
	.remove = qrtr_genpool_remove,
	.driver = {
		.name = "qcom_genpool_qrtr",
		.of_match_table = qrtr_genpool_match_table,
	 },
};
module_platform_driver(qrtr_genpool_driver);

MODULE_DESCRIPTION("QTI IPC-Router FIFO interface driver");
MODULE_LICENSE("GPL");
