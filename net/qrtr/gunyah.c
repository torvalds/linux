// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/io.h>
#include <linux/sizes.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/gunyah/gh_vm.h>
#include <linux/gunyah/gh_dbl.h>
#include <soc/qcom/secure_buffer.h>
#include <linux/qcom_scm.h>
#include "qrtr.h"

#define GUNYAH_MAGIC_KEY	0x24495043 /* "$IPC" */
#define FIFO_SIZE	0x4000
#define FIFO_FULL_RESERVE 8
#define FIFO_0_START	0x1000
#define FIFO_1_START	(FIFO_0_START + FIFO_SIZE)
#define GUNYAH_MAGIC_IDX	0x0
#define TAIL_0_IDX	0x1
#define HEAD_0_IDX	0x2
#define TAIL_1_IDX	0x3
#define HEAD_1_IDX	0x4
#define NOTIFY_0_IDX	0x5
#define NOTIFY_1_IDX	0x6
#define QRTR_DBL_MASK	0x1

/* Add potential padding and header space to 64k */
#define MAX_PKT_SZ	(SZ_64K + SZ_32)

struct gunyah_ring {
	void *buf;
	size_t len;
	u32 offset;
};

struct gunyah_pipe {
	__le32 *tail;
	__le32 *head;
	__le32 *read_notify;

	void *fifo;
	size_t length;
};

/**
 * qrtr_gunyah_dev - qrtr gunyah transport structure
 * @ep: qrtr endpoint specific info.
 * @dev: device from platform_device.
 * @pkt: buf for reading from fifo.
 * @res: resource of reserved mem region
 * @memparcel: memparcel handle returned from sharing mem
 * @base: Base of the shared fifo.
 * @size: fifo size.
 * @master: primary vm indicator.
 * @peer_name: name of vm peer.
 * @vm_nb: notifier block for vm status from rm
 * @state_lock: lock to protect registered state
 * @registered: state of endpoint
 * @label: label for gunyah resources
 * @tx_dbl: doorbell for tx notifications.
 * @rx_dbl: doorbell for rx notifications.
 * @dbl_lock: lock to prevent read races.
 * @tx_pipe: TX gunyah specific info.
 * @rx_pipe: RX gunyah specific info.
 */
struct qrtr_gunyah_dev {
	struct qrtr_endpoint ep;
	struct device *dev;
	struct gunyah_ring ring;

	struct resource res;
	u32 memparcel;
	void *base;
	size_t size;
	bool master;
	u32 peer_name;
	struct notifier_block vm_nb;
	/* lock to protect registered */
	struct mutex state_lock;
	bool registered;

	u32 label;
	void *tx_dbl;
	void *rx_dbl;
	struct work_struct work;
	/* lock to protect dbl_running */
	spinlock_t dbl_lock;

	struct gunyah_pipe tx_pipe;
	struct gunyah_pipe rx_pipe;
	wait_queue_head_t tx_avail_notify;
};

static void qrtr_gunyah_read(struct qrtr_gunyah_dev *qdev);
static void qrtr_gunyah_fifo_init(struct qrtr_gunyah_dev *qdev);

static void qrtr_gunyah_kick(struct qrtr_gunyah_dev *qdev)
{
	gh_dbl_flags_t dbl_mask = QRTR_DBL_MASK;
	int ret;

	ret = gh_dbl_send(qdev->tx_dbl, &dbl_mask, GH_DBL_NONBLOCK);
	if (ret) {
		if (ret != EAGAIN)
			dev_err(qdev->dev, "failed to raise doorbell %d\n", ret);
		if (!qdev->master)
			schedule_work(&qdev->work);
	}
}

static void qrtr_gunyah_retry_work(struct work_struct *work)
{
	struct qrtr_gunyah_dev *qdev = container_of(work, struct qrtr_gunyah_dev,
						   work);
	gh_dbl_flags_t dbl_mask = QRTR_DBL_MASK;

	gh_dbl_send(qdev->tx_dbl, &dbl_mask, 0);
}

static void qrtr_gunyah_cb(int irq, void *data)
{
	qrtr_gunyah_read((struct qrtr_gunyah_dev *)data);
}

static size_t gunyah_rx_avail(struct gunyah_pipe *pipe)
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

static void gunyah_rx_peak(struct gunyah_pipe *pipe, void *data,
			   unsigned int offset, size_t count)
{
	size_t len;
	u32 tail;

	tail = le32_to_cpu(*pipe->tail);
	tail += offset;
	if (tail >= pipe->length)
		tail -= pipe->length;

	if (WARN_ON_ONCE(tail > pipe->length))
		return;

	len = min_t(size_t, count, pipe->length - tail);
	if (len)
		memcpy_fromio(data, pipe->fifo + tail, len);

	if (len != count)
		memcpy_fromio(data + len, pipe->fifo, (count - len));
}

static void gunyah_rx_advance(struct gunyah_pipe *pipe, size_t count)
{
	u32 tail;

	tail = le32_to_cpu(*pipe->tail);

	tail += count;
	if (tail >= pipe->length)
		tail %= pipe->length;

	*pipe->tail = cpu_to_le32(tail);
}

static size_t gunyah_tx_avail(struct gunyah_pipe *pipe)
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

	if (WARN_ON_ONCE(head > pipe->length))
		avail = 0;

	return avail;
}

static void gunyah_tx_write(struct gunyah_pipe *pipe, const void *data,
			    size_t count)
{
	size_t len;
	u32 head;

	head = le32_to_cpu(*pipe->head);
	if (WARN_ON_ONCE(head > pipe->length))
		return;

	len = min_t(size_t, count, pipe->length - head);
	if (len)
		memcpy_toio(pipe->fifo + head, data, len);

	if (len != count)
		memcpy_toio(pipe->fifo, data + len, count - len);

	head += count;
	if (head >= pipe->length)
		head -= pipe->length;

	/* Ensure ordering of fifo and head update */
	smp_wmb();

	*pipe->head = cpu_to_le32(head);
}

static size_t gunyah_sg_copy_toio(struct scatterlist *sg, unsigned int nents,
				  void *buf, size_t buflen, off_t skip)
{
	unsigned int sg_flags = SG_MITER_ATOMIC | SG_MITER_FROM_SG;
	struct sg_mapping_iter miter;
	unsigned int offset = 0;

	sg_miter_start(&miter, sg, nents, sg_flags);

	if (!sg_miter_skip(&miter, skip))
		return 0;

	while ((offset < buflen) && sg_miter_next(&miter)) {
		unsigned int len;

		len = min(miter.length, buflen - offset);
		memcpy_toio(buf + offset, miter.addr, len);
		offset += len;
	}

	sg_miter_stop(&miter);

	return offset;
}

static void gunyah_sg_write(struct gunyah_pipe *pipe, struct scatterlist *sg,
			    int offset, size_t count)
{
	size_t len;
	u32 head;
	int rc = 0;

	head = le32_to_cpu(*pipe->head);
	if (WARN_ON_ONCE(head > pipe->length))
		return;

	len = min_t(size_t, count, pipe->length - head);
	if (len) {
		rc = gunyah_sg_copy_toio(sg, sg_nents(sg), pipe->fifo + head,
					 len, offset);
		offset += rc;
	}

	if (len != count)
		rc = gunyah_sg_copy_toio(sg, sg_nents(sg), pipe->fifo,
					 count - len, offset);

	head += count;
	if (head >= pipe->length)
		head -= pipe->length;

	smp_wmb();

	*pipe->head = cpu_to_le32(head);
}

static void gunyah_set_tx_notify(struct qrtr_gunyah_dev *qdev)
{
	*qdev->tx_pipe.read_notify = cpu_to_le32(1);
}

static void gunyah_clr_tx_notify(struct qrtr_gunyah_dev *qdev)
{
	*qdev->tx_pipe.read_notify = 0;
}

static bool gunyah_get_read_notify(struct qrtr_gunyah_dev *qdev)
{
	return le32_to_cpu(*qdev->rx_pipe.read_notify);
}

static int gunyah_wait_for_tx_avail(struct qrtr_gunyah_dev *qdev)
{
	int ret;

	gunyah_set_tx_notify(qdev);
	qrtr_gunyah_kick(qdev);
	ret = wait_event_timeout(qdev->tx_avail_notify, gunyah_tx_avail(&qdev->tx_pipe), 10 * HZ);

	return ret;
}

/* from qrtr to gunyah */
static int qrtr_gunyah_send(struct qrtr_endpoint *ep, struct sk_buff *skb)
{
	struct qrtr_gunyah_dev *qdev;
	size_t tx_avail;
	int chunk_size;
	int left_size;
	int offset;
	int rc = 0;

	qdev = container_of(ep, struct qrtr_gunyah_dev, ep);

	left_size = skb->len;
	offset = 0;
	while (left_size > 0) {
		tx_avail = gunyah_tx_avail(&qdev->tx_pipe);
		if (!tx_avail) {
			if (!gunyah_wait_for_tx_avail(qdev)) {
				dev_err(qdev->dev, "transport stalled\n");
				rc = -ETIMEDOUT;
				break;
			}
			continue;
		}
		if (tx_avail < left_size)
			chunk_size = tx_avail;
		else
			chunk_size = left_size;

		if (skb_is_nonlinear(skb)) {
			struct scatterlist sg[MAX_SKB_FRAGS + 1];

			sg_init_table(sg, skb_shinfo(skb)->nr_frags + 1);
			rc = skb_to_sgvec(skb, sg, 0, skb->len);
			if (rc < 0) {
				pr_err("failed skb_to_sgvec rc:%d\n", rc);
				break;
			}
			gunyah_sg_write(&qdev->tx_pipe, sg, offset,
					chunk_size);
		} else {
			gunyah_tx_write(&qdev->tx_pipe, skb->data + offset,
					chunk_size);
		}

		offset += chunk_size;
		left_size -= chunk_size;

		qrtr_gunyah_kick(qdev);
	}
	gunyah_clr_tx_notify(qdev);
	kfree_skb(skb);

	return (rc < 0) ? rc : 0;
}

static void qrtr_gunyah_read_new(struct qrtr_gunyah_dev *qdev)
{
	struct gunyah_ring *ring = &qdev->ring;
	size_t rx_avail;
	size_t pkt_len;
	u32 hdr[8];
	int rc;
	size_t hdr_len = sizeof(hdr);

	gunyah_rx_peak(&qdev->rx_pipe, &hdr, 0, hdr_len);
	pkt_len = qrtr_peek_pkt_size((void *)&hdr);
	if ((int)pkt_len < 0 || pkt_len > MAX_PKT_SZ) {
		/* Corrupted packet, reset the pipe and discard existing data */
		rx_avail = gunyah_rx_avail(&qdev->rx_pipe);
		dev_err(qdev->dev, "invalid pkt_len:%zu dropping:%zu bytes\n",
			pkt_len, rx_avail);
		gunyah_rx_advance(&qdev->rx_pipe, rx_avail);
		return;
	}

	rx_avail = gunyah_rx_avail(&qdev->rx_pipe);
	if (rx_avail > pkt_len)
		rx_avail = pkt_len;

	gunyah_rx_peak(&qdev->rx_pipe, ring->buf, 0, rx_avail);
	gunyah_rx_advance(&qdev->rx_pipe, rx_avail);

	if (rx_avail == pkt_len) {
		rc = qrtr_endpoint_post(&qdev->ep, ring->buf, pkt_len);
		if (rc == -EINVAL)
			dev_err(qdev->dev, "invalid ipcrouter packet\n");
	} else {
		ring->len = pkt_len;
		ring->offset = rx_avail;
	}
}

static void qrtr_gunyah_read_frag(struct qrtr_gunyah_dev *qdev)
{
	struct gunyah_ring *ring = &qdev->ring;
	size_t rx_avail;
	int rc;

	rx_avail = gunyah_rx_avail(&qdev->rx_pipe);
	if (rx_avail + ring->offset > ring->len)
		rx_avail = ring->len - ring->offset;

	gunyah_rx_peak(&qdev->rx_pipe, ring->buf + ring->offset, 0, rx_avail);
	gunyah_rx_advance(&qdev->rx_pipe, rx_avail);

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

static void qrtr_gunyah_read(struct qrtr_gunyah_dev *qdev)
{
	unsigned long flags;

	if (!qdev) {
		pr_err("%s: Invalid data.\n", __func__);
		return;
	}

	spin_lock_irqsave(&qdev->dbl_lock, flags);
	wake_up_all(&qdev->tx_avail_notify);

	while (gunyah_rx_avail(&qdev->rx_pipe)) {
		if (qdev->ring.offset)
			qrtr_gunyah_read_frag(qdev);
		else
			qrtr_gunyah_read_new(qdev);

		if (gunyah_get_read_notify(qdev))
			qrtr_gunyah_kick(qdev);
	}
	spin_unlock_irqrestore(&qdev->dbl_lock, flags);
}

static int qrtr_gunyah_share_mem(struct qrtr_gunyah_dev *qdev, gh_vmid_t self,
				 gh_vmid_t peer)
{
	struct qcom_scm_vmperm src_vmlist[] = {{self,
						PERM_READ | PERM_WRITE | PERM_EXEC}};
	struct qcom_scm_vmperm dst_vmlist[] = {{self, PERM_READ | PERM_WRITE},
					       {peer, PERM_READ | PERM_WRITE}};
	u64 srcvmids = BIT(src_vmlist[0].vmid);
	u64 dstvmids = BIT(dst_vmlist[0].vmid) | BIT(dst_vmlist[1].vmid);
	struct gh_acl_desc *acl;
	struct gh_sgl_desc *sgl;
	int ret;

	ret = qcom_scm_assign_mem(qdev->res.start, resource_size(&qdev->res),
				  &srcvmids, dst_vmlist, ARRAY_SIZE(dst_vmlist));
	if (ret) {
		pr_err("%s: qcom_scm_assign_mem failed addr=%x size=%u err=%d\n",
		       __func__, qdev->res.start, qdev->size, ret);
		return ret;
	}

	acl = kzalloc(offsetof(struct gh_acl_desc, acl_entries[2]), GFP_KERNEL);
	if (!acl)
		return -ENOMEM;
	sgl = kzalloc(offsetof(struct gh_sgl_desc, sgl_entries[1]), GFP_KERNEL);
	if (!sgl) {
		kfree(acl);
		return -ENOMEM;
	}
	acl->n_acl_entries = 2;
	acl->acl_entries[0].vmid = (u16)self;
	acl->acl_entries[0].perms = GH_RM_ACL_R | GH_RM_ACL_W;
	acl->acl_entries[1].vmid = (u16)peer;
	acl->acl_entries[1].perms = GH_RM_ACL_R | GH_RM_ACL_W;

	sgl->n_sgl_entries = 1;
	sgl->sgl_entries[0].ipa_base = qdev->res.start;
	sgl->sgl_entries[0].size = resource_size(&qdev->res);

	ret = ghd_rm_mem_share(GH_RM_MEM_TYPE_NORMAL, GH_RM_MEM_SHARE_SANITIZE, qdev->label,
			       acl, sgl, NULL, &qdev->memparcel);
	if (ret) {
		pr_err("%s: gh_rm_mem_share failed addr=%x size=%u err=%d\n",
		       __func__, qdev->res.start, qdev->size, ret);
		/* Attempt to give resource back to HLOS */
		if (qcom_scm_assign_mem(qdev->res.start, resource_size(&qdev->res),
					&dstvmids, src_vmlist, ARRAY_SIZE(src_vmlist)))
			pr_err("%s: qcom_scm_assign_mem failed addr=%x size=%u err=%d\n",
			       __func__, qdev->res.start, qdev->size, ret);
	}

	kfree(acl);
	kfree(sgl);

	return ret;
}

static void qrtr_gunyah_unshare_mem(struct qrtr_gunyah_dev *qdev,
				    gh_vmid_t self, gh_vmid_t peer)
{
	u64 src_vmlist = BIT(self) | BIT(peer);
	struct qcom_scm_vmperm dst_vmlist[1] = {{self, PERM_READ | PERM_WRITE | PERM_EXEC}};
	int ret;

	ret = ghd_rm_mem_reclaim(qdev->memparcel, 0);
	if (ret)
		pr_err("%s: Gunyah reclaim failed\n", __func__);

	ret = qcom_scm_assign_mem(qdev->res.start, resource_size(&qdev->res),
				  &src_vmlist, dst_vmlist, 1);
	if (ret)
		pr_err("%s: qcom_scm_assign_mem failed addr=%x size=%u err=%d\n",
		       __func__, qdev->res.start, resource_size(&qdev->res), ret);
}

static int qrtr_gunyah_vm_cb(struct notifier_block *nb, unsigned long cmd, void *data)
{
	struct qrtr_gunyah_dev *qdev = container_of(nb, struct qrtr_gunyah_dev, vm_nb);
	gh_vmid_t peer_vmid;
	gh_vmid_t self_vmid;
	gh_vmid_t vmid;

	if (!data)
		return NOTIFY_DONE;
	vmid = *((gh_vmid_t *)data);

	if (ghd_rm_get_vmid(qdev->peer_name, &peer_vmid))
		return NOTIFY_DONE;
	if (ghd_rm_get_vmid(GH_PRIMARY_VM, &self_vmid))
		return NOTIFY_DONE;
	if (peer_vmid != vmid)
		return NOTIFY_DONE;

	mutex_lock(&qdev->state_lock);
	switch (cmd) {
	case GH_VM_BEFORE_POWERUP:
		if (qdev->registered)
			break;
		qrtr_gunyah_fifo_init(qdev);
		if (qrtr_endpoint_register(&qdev->ep, QRTR_EP_NET_ID_AUTO, false, NULL)) {
			pr_err("%s: endpoint register failed\n", __func__);
			break;
		}
		if (qrtr_gunyah_share_mem(qdev, self_vmid, peer_vmid)) {
			pr_err("%s: failed to share memory\n", __func__);
			qrtr_endpoint_unregister(&qdev->ep);
			break;
		}
		qdev->registered = true;
		break;
	case GH_VM_POWERUP_FAIL:
		fallthrough;
	case GH_VM_EARLY_POWEROFF:
		if (qdev->registered) {
			qrtr_endpoint_unregister(&qdev->ep);
			qrtr_gunyah_unshare_mem(qdev, self_vmid, peer_vmid);
			qdev->registered = false;
		}
		break;
	}
	mutex_unlock(&qdev->state_lock);

	return NOTIFY_DONE;
}

/**
 * qrtr_gunyah_fifo_init() - init gunyah xprt configs
 *
 * @return: 0 on success, standard Linux error codes on error.
 *
 * This function is called to initialize the gunyah XPRT pointer with
 * the gunyah XPRT configurations either from device tree or static arrays.
 */
static void qrtr_gunyah_fifo_init(struct qrtr_gunyah_dev *qdev)
{
	__le32 *descs;

	descs = qdev->base;
	descs[GUNYAH_MAGIC_IDX] = GUNYAH_MAGIC_KEY;

	if (qdev->master) {
		qdev->tx_pipe.tail = &descs[TAIL_0_IDX];
		qdev->tx_pipe.head = &descs[HEAD_0_IDX];
		qdev->tx_pipe.fifo = qdev->base + FIFO_0_START;
		qdev->tx_pipe.length = FIFO_SIZE;
		qdev->tx_pipe.read_notify = &descs[NOTIFY_0_IDX];

		qdev->rx_pipe.tail = &descs[TAIL_1_IDX];
		qdev->rx_pipe.head = &descs[HEAD_1_IDX];
		qdev->rx_pipe.fifo = qdev->base + FIFO_1_START;
		qdev->rx_pipe.length = FIFO_SIZE;
		qdev->rx_pipe.read_notify = &descs[NOTIFY_1_IDX];
	} else {
		qdev->tx_pipe.tail = &descs[TAIL_1_IDX];
		qdev->tx_pipe.head = &descs[HEAD_1_IDX];
		qdev->tx_pipe.fifo = qdev->base + FIFO_1_START;
		qdev->tx_pipe.length = FIFO_SIZE;
		qdev->tx_pipe.read_notify = &descs[NOTIFY_1_IDX];

		qdev->rx_pipe.tail = &descs[TAIL_0_IDX];
		qdev->rx_pipe.head = &descs[HEAD_0_IDX];
		qdev->rx_pipe.fifo = qdev->base + FIFO_0_START;
		qdev->rx_pipe.length = FIFO_SIZE;
		qdev->rx_pipe.read_notify = &descs[NOTIFY_0_IDX];
	}

	/* Reset respective index */
	*qdev->tx_pipe.head = 0;
	*qdev->tx_pipe.read_notify = 0;
	*qdev->rx_pipe.tail = 0;
}

static struct device_node *qrtr_gunyah_svm_of_parse(struct qrtr_gunyah_dev *qdev)
{
	const char *compat = "qcom,qrtr-gunyah-gen";
	struct device_node *np = NULL;
	struct device_node *shm_np;
	u32 label;
	int ret;

	while ((np = of_find_compatible_node(np, NULL, compat))) {
		ret = of_property_read_u32(np, "qcom,label", &label);
		if (ret) {
			of_node_put(np);
			continue;
		}
		if (label == qdev->label)
			break;

		of_node_put(np);
	}
	if (!np)
		return NULL;

	shm_np = of_parse_phandle(np, "memory-region", 0);
	if (!shm_np)
		dev_err(qdev->dev, "can't parse svm shared mem node!\n");

	of_node_put(np);
	return shm_np;
}

static int qrtr_gunyah_alloc_fifo(struct qrtr_gunyah_dev *qdev)
{
	struct device *dev = qdev->dev;
	resource_size_t size;

	size = FIFO_1_START + FIFO_SIZE;

	qdev->base = dma_alloc_attrs(dev, size, &qdev->res.start, GFP_KERNEL,
				     DMA_ATTR_FORCE_CONTIGUOUS);
	if (!qdev->base)
		return -ENOMEM;

	qdev->res.end = qdev->res.start + size - 1;
	qdev->size = size;

	return 0;
}

static int qrtr_gunyah_map_memory(struct qrtr_gunyah_dev *qdev)
{
	struct device *dev = qdev->dev;
	struct device_node *np;
	resource_size_t size;
	int ret;

	if (qdev->master) {
		np = of_parse_phandle(dev->of_node, "shared-buffer", 0);
		if (!np)
			return qrtr_gunyah_alloc_fifo(qdev);
	} else {
		np = qrtr_gunyah_svm_of_parse(qdev);
		if (!np) {
			dev_err(dev, "can't parse shared mem node!\n");
			return -EINVAL;
		}
	}

	ret = of_address_to_resource(np, 0, &qdev->res);
	of_node_put(np);
	if (ret) {
		dev_err(dev, "of_address_to_resource failed!\n");
		return -EINVAL;
	}
	size = resource_size(&qdev->res);

	qdev->base = devm_ioremap_resource(dev, &qdev->res);
	if (IS_ERR(qdev->base)) {
		dev_err(dev, "ioremap failed!\n");
		return PTR_ERR(qdev->base);
	}
	qdev->size = size;

	return 0;
}

/**
 * qrtr_gunyah_probe() - Probe a gunyah xprt
 *
 * @pdev: Platform device corresponding to gunyah xprt.
 *
 * @return: 0 on success, standard Linux error codes on error.
 *
 * This function is called when the underlying device tree driver registers
 * a platform device, mapped to a gunyah transport.
 */
static int qrtr_gunyah_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct qrtr_gunyah_dev *qdev;
	enum gh_dbl_label dbl_label;
	int ret;

	qdev = devm_kzalloc(&pdev->dev, sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return -ENOMEM;
	qdev->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, qdev);

	qdev->ring.buf = devm_kzalloc(&pdev->dev, MAX_PKT_SZ, GFP_KERNEL);
	if (!qdev->ring.buf)
		return -ENOMEM;

	mutex_init(&qdev->state_lock);
	qdev->registered = false;
	spin_lock_init(&qdev->dbl_lock);

	ret = of_property_read_u32(node, "gunyah-label", &qdev->label);
	if (ret) {
		dev_err(qdev->dev, "failed to read label info %d\n", ret);
		return ret;
	}
	qdev->master = of_property_read_bool(node, "qcom,master");

	ret = qrtr_gunyah_map_memory(qdev);
	if (ret)
		return ret;

	if (!qdev->master)
		qrtr_gunyah_fifo_init(qdev);
	init_waitqueue_head(&qdev->tx_avail_notify);

	if (qdev->master) {
		ret = of_property_read_u32(node, "peer-name", &qdev->peer_name);
		if (ret)
			qdev->peer_name = GH_SELF_VM;

		qdev->vm_nb.notifier_call = qrtr_gunyah_vm_cb;
		qdev->vm_nb.priority = INT_MAX;
		gh_register_vm_notifier(&qdev->vm_nb);
	}

	dbl_label = qdev->label;
	qdev->tx_dbl = gh_dbl_tx_register(dbl_label);
	if (IS_ERR_OR_NULL(qdev->tx_dbl)) {
		ret = PTR_ERR(qdev->tx_dbl);
		dev_err(qdev->dev, "failed to get gunyah tx dbl %d\n", ret);
		return ret;
	}
	INIT_WORK(&qdev->work, qrtr_gunyah_retry_work);

	qdev->ep.xmit = qrtr_gunyah_send;
	if (!qdev->master) {
		ret = qrtr_endpoint_register(&qdev->ep, QRTR_EP_NET_ID_AUTO,
					     false, NULL);
		if (ret)
			goto register_fail;
	}

	qdev->rx_dbl = gh_dbl_rx_register(dbl_label, qrtr_gunyah_cb, qdev);
	if (IS_ERR_OR_NULL(qdev->rx_dbl)) {
		ret = PTR_ERR(qdev->rx_dbl);
		dev_err(qdev->dev, "failed to get gunyah rx dbl %d\n", ret);
		goto fail_rx_dbl;
	}

	if (!qdev->master && gunyah_rx_avail(&qdev->rx_pipe))
		qrtr_gunyah_read(qdev);

	return 0;

fail_rx_dbl:
	qrtr_endpoint_unregister(&qdev->ep);
register_fail:
	cancel_work_sync(&qdev->work);
	gh_dbl_tx_unregister(qdev->tx_dbl);

	return ret;
}

static int qrtr_gunyah_remove(struct platform_device *pdev)
{
	struct qrtr_gunyah_dev *qdev = dev_get_drvdata(&pdev->dev);
	struct device_node *np;
	gh_vmid_t peer_vmid;
	gh_vmid_t self_vmid;

	cancel_work_sync(&qdev->work);
	gh_dbl_tx_unregister(qdev->tx_dbl);
	gh_dbl_rx_unregister(qdev->rx_dbl);

	if (!qdev->master)
		return 0;
	gh_unregister_vm_notifier(&qdev->vm_nb);

	if (ghd_rm_get_vmid(qdev->peer_name, &peer_vmid))
		return 0;
	if (ghd_rm_get_vmid(GH_PRIMARY_VM, &self_vmid))
		return 0;
	qrtr_gunyah_unshare_mem(qdev, self_vmid, peer_vmid);

	np = of_parse_phandle(qdev->dev->of_node, "shared-buffer", 0);
	if (np) {
		of_node_put(np);
		return 0;
	}

	dma_free_attrs(qdev->dev, qdev->size, qdev->base, qdev->res.start,
		       DMA_ATTR_FORCE_CONTIGUOUS);

	return 0;
}

static const struct of_device_id qrtr_gunyah_match_table[] = {
	{ .compatible = "qcom,qrtr-gunyah" },
	{}
};

static struct platform_driver qrtr_gunyah_driver = {
	.driver = {
		.name = "qcom_gunyah_qrtr",
		.of_match_table = qrtr_gunyah_match_table,
	 },
	.probe = qrtr_gunyah_probe,
	.remove = qrtr_gunyah_remove,
};
module_platform_driver(qrtr_gunyah_driver);

MODULE_DESCRIPTION("QTI IPC-Router Gunyah interface driver");
MODULE_LICENSE("GPL");
