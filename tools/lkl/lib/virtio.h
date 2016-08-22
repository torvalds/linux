#ifndef _LKL_LIB_VIRTIO_H
#define _LKL_LIB_VIRTIO_H

#include <stdint.h>
#include <lkl_host.h>

#define PAGE_SIZE		4096

/* The following are copied from skbuff.h */
#if (65536/PAGE_SIZE + 1) < 16
#define MAX_SKB_FRAGS 16UL
#else
#define MAX_SKB_FRAGS (65536/PAGE_SIZE + 1)
#endif

#define VIRTIO_REQ_MAX_BUFS	(MAX_SKB_FRAGS + 2)

/* We always have 2 queues on a netdev: one for tx, one for rx. */
#define RX_QUEUE_IDX 0
#define TX_QUEUE_IDX 1

struct virtio_req {
	struct virtio_dev *dev;
	struct virtio_queue *q;
	uint16_t idx;
	uint16_t buf_count;
	struct lkl_dev_buf buf[VIRTIO_REQ_MAX_BUFS];
	uint32_t mergeable_rx_len;
};

struct virtio_dev_ops {
	int (*check_features)(struct virtio_dev *dev);
	/*
	 * Return a negative value to stop the queue processing. In this case
	 * the current request is not consumed from the queue and the host
	 * device is resposible for restaring the queue processing by calling
	 * virtio_process_queue at a later time.
	 * A special case exists if a netdev is in mergeable RX buffer mode
	 * where more than one "avail" slots may be consumed. In this case
	 * it will return how many avail idx to advance.
	 */
	int (*enqueue)(struct virtio_dev *dev, struct virtio_req *req);
	/*
	 * Acquire/release a lock on the specified queue. Only implemented by
	 * netdevs, all other devices have NULL acquire/release function
	 * pointers.
	 */
	void (*acquire_queue)(struct virtio_dev *dev, int queue_idx);
	void (*release_queue)(struct virtio_dev *dev, int queue_idx);
};

struct virtio_queue {
	uint32_t num_max;
	uint32_t num;
	uint32_t ready;

	struct lkl_vring_desc *desc;
	struct lkl_vring_avail *avail;
	struct lkl_vring_used *used;
	uint16_t last_avail_idx;
	uint16_t last_used_idx_signaled;
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
	void *base;
};

int virtio_dev_setup(struct virtio_dev *dev, int queues, int num_max);
void virtio_dev_cleanup(struct virtio_dev *dev);
void virtio_req_complete(struct virtio_req *req, uint32_t len);
void virtio_process_queue(struct virtio_dev *dev, uint32_t qidx);

#define container_of(ptr, type, member) \
	(type *)((char *)(ptr) - __builtin_offsetof(type, member))


static inline int is_rx_queue(struct virtio_dev *dev,
			      struct virtio_queue *queue)
{
	return &dev->queue[RX_QUEUE_IDX] == queue;
}

static inline int is_tx_queue(struct virtio_dev *dev,
			      struct virtio_queue *queue)
{
	return &dev->queue[TX_QUEUE_IDX] == queue;
}

#endif /* _LKL_LIB_VIRTIO_H */
