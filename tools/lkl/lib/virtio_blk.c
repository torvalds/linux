#include <lkl_host.h>
#include "virtio.h"
#include "endian.h"

struct virtio_blk_dev {
	struct virtio_dev dev;
	struct {
		uint64_t capacity;
	} config;
	struct lkl_dev_blk_ops *ops;
	union lkl_disk disk;
};

struct virtio_blk_req_header {
	uint32_t type;
	uint32_t prio;
	uint64_t sector;
};

struct virtio_blk_req_trailer {
	uint8_t status;
};

static int blk_check_features(uint32_t features)
{
	if (!features)
		return 0;

	return -LKL_EINVAL;
}

void lkl_dev_blk_complete(struct lkl_dev_buf *bufs, unsigned char status,
			  int len)
{
	struct virtio_dev_req *req;
	struct virtio_blk_req_trailer *f;

	req = container_of(bufs - 1, struct virtio_dev_req, buf);

	if (req->buf_count < 2) {
		lkl_printf("virtio_blk: no status buf\n");
		return;
	}

	if (req->buf[req->buf_count - 1].len != sizeof(*f)) {
		lkl_printf("virtio_blk: bad status buf\n");
	} else {
		f = req->buf[req->buf_count - 1].addr;
		f->status = status;
	}

	virtio_dev_complete(req, len);
}

static void blk_queue(struct virtio_dev *dev, struct virtio_dev_req *req)
{
	struct virtio_blk_req_header *h;
	struct virtio_blk_dev *blk_dev;

	if (req->buf[0].len != sizeof(struct virtio_blk_req_header)) {
		lkl_printf("virtio_blk: bad header buf\n");
		lkl_dev_blk_complete(&req->buf[1], LKL_DEV_BLK_STATUS_UNSUP, 0);
		return;
	}

	h = req->buf[0].addr;
	blk_dev = container_of(dev, struct virtio_blk_dev, dev);

	blk_dev->ops->request(blk_dev->disk, le32toh(h->type),
			      le32toh(h->prio), le32toh(h->sector),
			      &req->buf[1], req->buf_count - 2);
}

static struct virtio_dev_ops blk_ops = {
	.check_features = blk_check_features,
	.queue = blk_queue,
};

int lkl_disk_add(union lkl_disk disk)
{
	struct virtio_blk_dev *dev;
	unsigned long long capacity;
	int ret;
	static int count;

	dev = lkl_host_ops.mem_alloc(sizeof(*dev));
	if (!dev)
		return -LKL_ENOMEM;

	dev->dev.device_id = 2;
	dev->dev.vendor_id = 0;
	dev->dev.device_features = 0;
	dev->dev.config_gen = 0;
	dev->dev.config_data = &dev->config;
	dev->dev.config_len = sizeof(dev->config);
	dev->dev.ops = &blk_ops;
	dev->ops = &lkl_dev_blk_ops;
	dev->disk = disk;

	ret = dev->ops->get_capacity(disk, &capacity);
	if (ret) {
		ret = -LKL_ENOMEM;
		goto out_free;
	}
	dev->config.capacity = capacity;

	ret = virtio_dev_setup(&dev->dev, 1, 65536);
	if (ret)
		goto out_free;

	return count++;

out_free:
	lkl_host_ops.mem_free(dev);

	return ret;
}
