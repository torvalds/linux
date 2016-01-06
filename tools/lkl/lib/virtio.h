#ifndef _LKL_LIB_VIRTIO_H
#define _LKL_LIB_VIRTIO_H

#include <stdint.h>
#include <lkl_host.h>

#define VIRTIO_REQ_MAX_BUFS	4

struct virtio_req {
	struct virtio_dev *dev;
	struct virtio_queue *q;
	uint16_t idx;
	uint16_t buf_count;
	struct lkl_dev_buf buf[VIRTIO_REQ_MAX_BUFS];
};

struct virtio_dev_ops {
	int (*check_features)(struct virtio_dev *dev);
	/*
	 * Return a negative value to stop the queue processing. In this case
	 * the current request is not consumed from the queue and the host
	 * device is resposible for restaring the queue processing by calling
	 * virtio_process_queue at a later time.
	 */
	int (*enqueue)(struct virtio_dev *dev, struct virtio_req *req);
};

struct virtio_dev {
	uint32_t device_id;
	uint32_t vendor_id;
	uint64_t device_features;
	uint32_t device_features_sel;
	uint64_t driver_features;
	uint32_t driver_features_sel;
	uint32_t queue_sel;
	struct virtio_queue *queue;
	uint32_t queue_notify;
	uint32_t int_status;
	uint32_t status;
	uint32_t config_gen;

	struct virtio_dev_ops *ops;
	int irq;
	void *config_data;
	int config_len;
};

int virtio_dev_setup(struct virtio_dev *dev, int queues, int num_max);
void virtio_dev_cleanup(struct virtio_dev *dev);
void virtio_req_complete(struct virtio_req *req, uint32_t len);
void virtio_process_queue(struct virtio_dev *dev, uint32_t qidx);

#define container_of(ptr, type, member) \
	(type *)((char *)(ptr) - __builtin_offsetof(type, member))

#endif /* _LKL_LIB_VIRTIO_H */
