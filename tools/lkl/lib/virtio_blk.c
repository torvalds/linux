#include <lkl_host.h>
#include "virtio.h"
#include "endian.h"

struct virtio_blk_dev {
	struct virtio_dev dev;
	struct lkl_virtio_blk_config config;
	struct lkl_dev_blk_ops *ops;
	union lkl_disk disk;
};

struct virtio_blk_req_trailer {
	uint8_t status;
};

static int blk_check_features(struct virtio_dev *dev)
{
	if (dev->driver_features == dev->device_features)
		return 0;

	return -LKL_EINVAL;
}

static int blk_enqueue(struct virtio_dev *dev, struct virtio_req *req)
{
	struct virtio_blk_dev *blk_dev;
	struct lkl_virtio_blk_outhdr *h;
	struct virtio_blk_req_trailer *t;
	struct lkl_blk_req lkl_req;

	if (req->buf_count < 3) {
		lkl_printf("virtio_blk: no status buf\n");
		goto out;
	}

	h = req->buf[0].addr;
	t = req->buf[req->buf_count - 1].addr;
	blk_dev = container_of(dev, struct virtio_blk_dev, dev);

	t->status = LKL_DEV_BLK_STATUS_IOERR;

	if (req->buf[0].len != sizeof(*h)) {
		lkl_printf("virtio_blk: bad header buf\n");
		goto out;
	}

	if (req->buf[req->buf_count - 1].len != sizeof(*t)) {
		lkl_printf("virtio_blk: bad status buf\n");
		goto out;
	}

	lkl_req.type = le32toh(h->type);
	lkl_req.prio = le32toh(h->ioprio);
	lkl_req.sector = le32toh(h->sector);
	lkl_req.buf = &req->buf[1];
	lkl_req.count = req->buf_count - 2;

	t->status = blk_dev->ops->request(blk_dev->disk, &lkl_req);

out:
	virtio_req_complete(req, 0);
	return 0;
}

static struct virtio_dev_ops blk_ops = {
	.check_features = blk_check_features,
	.enqueue = blk_enqueue,
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

	dev->dev.device_id = LKL_VIRTIO_ID_BLOCK;
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

	ret = virtio_dev_setup(&dev->dev, 1, 32);
	if (ret)
		goto out_free;

	return count++;

out_free:
	lkl_host_ops.mem_free(dev);

	return ret;
}
