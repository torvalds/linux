#include <string.h>
#include <stdio.h>
#include <lkl_host.h>
#include "iomem.h"
#include "virtio.h"

#define VIRTIO_DEV_MAGIC		0x74726976
#define VIRTIO_DEV_VERSION		2

#define VIRTIO_MMIO_MAGIC_VALUE		0x000
#define VIRTIO_MMIO_VERSION		0x004
#define VIRTIO_MMIO_DEVICE_ID		0x008
#define VIRTIO_MMIO_VENDOR_ID		0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES	0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES	0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL		0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034
#define VIRTIO_MMIO_QUEUE_NUM		0x038
#define VIRTIO_MMIO_QUEUE_READY		0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064
#define VIRTIO_MMIO_STATUS		0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW	0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH	0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW	0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH	0x0a4
#define VIRTIO_MMIO_CONFIG_GENERATION	0x0fc
#define VIRTIO_MMIO_CONFIG		0x100
#define VIRTIO_MMIO_INT_VRING		0x01
#define VIRTIO_MMIO_INT_CONFIG		0x02

#define VIRTIO_DEV_STATUS_ACK		0x01
#define VIRTIO_DEV_STATUS_DRV		0x02
#define VIRTIO_DEV_STATUS_FEATURES_OK	0x08
#define VIRTIO_DEV_STATUS_DRV_OK	0x04
#define VIRTIO_DEV_STATUS_FAILED	0x80

#define VIRTIO_F_VERSION_1		(1ULL << 32)
#define VIRTIO_RING_F_EVENT_IDX		(1ULL << 29)
#define VIRTIO_DESC_F_NEXT		1

static inline uint16_t virtio_get_used_event(struct virtio_queue *q)
{
	return q->avail->ring[q->num];
}

static inline void virtio_set_avail_event(struct virtio_queue *q, uint16_t val)
{
	*((uint16_t *)&q->used->ring[q->num]) = val;
}

#define ring_entry(q, r, idx) \
	q->r->ring[idx & (q->num - 1)]

static inline void virtio_deliver_irq(struct virtio_dev *dev)
{
	dev->int_status |= VIRTIO_MMIO_INT_VRING;
	lkl_trigger_irq(dev->irq, NULL);
}

void virtio_dev_complete(struct virtio_dev_req *req, uint32_t len)
{
	struct virtio_queue *q = req->q;
	struct virtio_dev *dev = req->dev;
	uint16_t idx = le16toh(q->used->idx) & (q->num - 1);
	int send_irq = 0;

	q->used->ring[idx].id = htole16(req->desc_idx);
	q->used->ring[idx].len = htole16(len);
	if (virtio_get_used_event(q) == q->used->idx)
		send_irq = 1;
	q->used->idx = htole16(le16toh(q->used->idx) + 1);

	if (send_irq)
		virtio_deliver_irq(dev);

	lkl_host_ops.mem_free(req);
}

static void virtio_process_avail_one(struct virtio_dev *dev,
				     struct virtio_queue *q,
				     int avail_idx)
{
	int j;
	uint16_t desc_idx;
	struct virtio_desc *i;
	struct virtio_dev_req *req = NULL;

	avail_idx = avail_idx & (q->num - 1);
	desc_idx = le16toh(q->avail->ring[avail_idx]) & (q->num - 1);

	i = &q->desc[desc_idx];
	j = 1;
	while (le16toh(i->flags) & VIRTIO_DESC_F_NEXT) {
		desc_idx = le16toh(i->next) & (q->num - 1);
		i = &q->desc[desc_idx];
		j++;
	}

	req = lkl_host_ops.mem_alloc((uintptr_t)&req->buf[j]);
	if (!req)
		return;

	req->dev = dev;
	req->q = q;
	req->desc_idx = q->avail->ring[avail_idx];
	req->buf_count = j;

	desc_idx = le16toh(q->avail->ring[avail_idx]) & (q->num - 1);
	i = &q->desc[desc_idx];
	j = 0;
	req->buf[j].addr = (void *)(uintptr_t)le64toh(i->addr);
	req->buf[j].len = le32toh(i->len);
	while (le16toh(i->flags) & VIRTIO_DESC_F_NEXT) {
		desc_idx = le16toh(i->next) & (q->num - 1);
		i = &q->desc[desc_idx];
		j++;
		req->buf[j].addr = (void *)(uintptr_t)le64toh(i->addr);
		req->buf[j].len = le32toh(i->len);
	}

	dev->ops->queue(dev, req);
}

static void virtio_process_avail(struct virtio_dev *dev, uint32_t qidx)
{
	struct virtio_queue *q = &dev->queue[qidx];

	virtio_set_avail_event(q, q->avail->idx);

	while (q->last_avail_idx != le16toh(q->avail->idx)) {
		virtio_process_avail_one(dev, q, q->last_avail_idx);
		q->last_avail_idx++;
	}
}

static inline uint32_t virtio_read_device_features(struct virtio_dev *dev)
{
	if (dev->device_features_sel)
		return (uint32_t)(dev->device_features >> 32);

	return (uint32_t)dev->device_features;
}

static inline void virtio_write_driver_features(struct virtio_dev *dev,
						uint32_t val)
{
	uint64_t tmp;

	if (dev->driver_features_sel) {
		tmp = dev->driver_features & 0xFFFFFFFF;
		dev->driver_features = tmp | (uint64_t)val << 32;
	} else {
		tmp = dev->driver_features & 0xFFFFFFFF00000000;
		dev->driver_features = tmp | val;
	}
}

static int virtio_read(void *data, int offset, void *res, int size)
{
	uint32_t val;
	struct virtio_dev *dev = (struct virtio_dev *)data;

	if (offset >= VIRTIO_MMIO_CONFIG) {
		offset -= VIRTIO_MMIO_CONFIG;
		if (offset + size > dev->config_len)
			return -LKL_EINVAL;
		memcpy(res, dev->config_data + offset, size);
		return 0;
	}

	if (size != sizeof(uint32_t))
		return -LKL_EINVAL;

	switch (offset) {
	case VIRTIO_MMIO_MAGIC_VALUE:
		val = VIRTIO_DEV_MAGIC;
		break;
	case VIRTIO_MMIO_VERSION:
		val = VIRTIO_DEV_VERSION;
		break;
	case VIRTIO_MMIO_DEVICE_ID:
		val = dev->device_id;
		break;
	case VIRTIO_MMIO_VENDOR_ID:
		val = dev->vendor_id;
		break;
	case VIRTIO_MMIO_DEVICE_FEATURES:
		val = virtio_read_device_features(dev);
		break;
	case VIRTIO_MMIO_QUEUE_NUM_MAX:
		val = dev->queue[dev->queue_sel].num_max;
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		val = dev->queue[dev->queue_sel].ready;
		break;
	case VIRTIO_MMIO_INTERRUPT_STATUS:
		val = dev->int_status;
		break;
	case VIRTIO_MMIO_STATUS:
		val = dev->status;
		break;
	case VIRTIO_MMIO_CONFIG_GENERATION:
		val = dev->config_gen;
		break;
	default:
		return -1;
	}

	*(uint32_t *)res = htole32(val);

	return 0;
}

static inline void set_ptr_low(void **ptr, uint32_t val)
{
	uint64_t tmp = (uintptr_t)*ptr;

	tmp = (tmp & 0xFFFFFFFF00000000) | val;
	*ptr = (void *)(long)tmp;
}

static inline void set_ptr_high(void **ptr, uint32_t val)
{
	uint64_t tmp = (uintptr_t)*ptr;

	tmp = (tmp & 0x00000000FFFFFFFF) | ((uint64_t)val << 32);
	*ptr = (void *)(long)tmp;
}

static int virtio_write(void *data, int offset, void *res, int size)
{
	struct virtio_dev *dev = (struct virtio_dev *)data;
	struct virtio_queue *q = &dev->queue[dev->queue_sel];
	uint32_t val;
	int ret = 0;

	if (offset >= VIRTIO_MMIO_CONFIG) {
		offset -= VIRTIO_MMIO_CONFIG;

		if (offset + size >= dev->config_len)
			return -LKL_EINVAL;
		memcpy(dev->config_data + offset, res, size);
		return 0;
	}

	if (size != sizeof(uint32_t))
		return -LKL_EINVAL;

	val = le32toh(*(uint32_t *)res);

	switch (offset) {
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
		if (val > 1)
			return -LKL_EINVAL;
		dev->device_features_sel = val;
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
		if (val > 1)
			return -LKL_EINVAL;
		dev->driver_features_sel = val;
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES:
		virtio_write_driver_features(dev, val);
		break;
	case VIRTIO_MMIO_QUEUE_SEL:
		dev->queue_sel = val;
		break;
	case VIRTIO_MMIO_QUEUE_NUM:
		dev->queue[dev->queue_sel].num = val;
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		dev->queue[dev->queue_sel].ready = val;
		break;
	case VIRTIO_MMIO_QUEUE_NOTIFY:
		virtio_process_avail(dev, val);
		break;
	case VIRTIO_MMIO_INTERRUPT_ACK:
		dev->int_status = 0;
		break;
	case VIRTIO_MMIO_STATUS:
		if (val & VIRTIO_DEV_STATUS_FEATURES_OK &&
		    (!(dev->driver_features & VIRTIO_F_VERSION_1) ||
		     !(dev->driver_features & VIRTIO_RING_F_EVENT_IDX) ||
		     dev->ops->check_features(dev->driver_features & 0xFFFFFF)))
			val &= ~VIRTIO_DEV_STATUS_FEATURES_OK;
		dev->status = val;
		break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
		set_ptr_low((void **)&q->desc, val);
		break;
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		set_ptr_high((void **)&q->desc, val);
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
		set_ptr_low((void **)&q->avail, val);
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
		set_ptr_high((void **)&q->avail, val);
		break;
	case VIRTIO_MMIO_QUEUE_USED_LOW:
		set_ptr_low((void **)&q->used, val);
		break;
	case VIRTIO_MMIO_QUEUE_USED_HIGH:
		set_ptr_high((void **)&q->used, val);
		break;
	default:
		ret = -1;
	}

	return ret;
}

static const struct lkl_iomem_ops virtio_ops = {
	.read = virtio_read,
	.write = virtio_write,
};

char lkl_virtio_devs[256];
static char *devs = lkl_virtio_devs;

int virtio_dev_setup(struct virtio_dev *dev, int queues, int num_max)
{
	int qsize = queues * sizeof(*dev->queue);
	int ret, avail, mmio_size;
	int i;

	dev->irq = lkl_get_free_irq("virtio");
	if (dev->irq < 0)
		return dev->irq;

	dev->device_features |= VIRTIO_F_VERSION_1 | VIRTIO_RING_F_EVENT_IDX;
	dev->queue = lkl_host_ops.mem_alloc(qsize);
	if (!dev->queue)
		return -LKL_ENOMEM;

	memset(dev->queue, 0, qsize);
	for (i = 0; i < queues; i++)
		dev->queue[i].num_max = num_max;

	mmio_size = VIRTIO_MMIO_CONFIG + dev->config_len;
	ret = register_iomem(dev, mmio_size, &virtio_ops);
	if (ret)
		lkl_host_ops.mem_free(dev->queue);

	avail = sizeof(lkl_virtio_devs) - (devs - lkl_virtio_devs);
	devs += snprintf(devs, avail, " virtio_mmio.device=%d@0x%lx:%d",
			 mmio_size, (uintptr_t)dev, dev->irq);

	return ret;
}

void virtio_dev_cleanup(struct virtio_dev *dev)
{
	lkl_put_irq(dev->irq, "virtio");
	unregister_iomem(dev);
	lkl_host_ops.mem_free(dev->queue);
}

