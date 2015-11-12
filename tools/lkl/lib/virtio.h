#ifndef _LKL_LIB_VIRTIO_H
#define _LKL_LIB_VIRTIO_H

#include <stdint.h>
#include <lkl_host.h>

struct virtio_desc {
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
};

struct virtio_avail {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[];
};

struct virtio_used_elem {
	uint32_t id;
	uint32_t len;
};

struct virtio_used {
	uint16_t flags;
	uint16_t idx;
	struct virtio_used_elem ring[];
};

struct virtio_queue {
	uint32_t num_max;
	uint32_t num;
	uint32_t ready;

	struct virtio_desc *desc;
	struct virtio_avail *avail;
	struct virtio_used *used;
	uint16_t last_avail_idx;
	void *config_data;
	int config_len;
};

struct virtio_dev_req {
	struct virtio_dev *dev;
	struct virtio_queue *q;
	uint16_t desc_idx;
	uint16_t buf_count;
	struct lkl_dev_buf buf[];
};

struct virtio_dev_ops {
	int (*check_features)(uint32_t features);
	void (*queue)(struct virtio_dev *dev, struct virtio_dev_req *req);
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
void virtio_dev_complete(struct virtio_dev_req *req, uint32_t len);

#define container_of(ptr, type, member) \
	(type *)((char *)(ptr) - __builtin_offsetof(type, member))

#ifndef __MINGW32__
#ifdef __FreeBSD__
#include <sys/endian.h>
#else
#include <endian.h>
#endif  /* __FreeBSD__ */
#else  /* !__MINGW32__ */
#define le32toh(x) (x)
#define le16toh(x) (x)
#define htole32(x) (x)
#define htole16(x) (x)
#define le64toh(x) (x)
#endif  /* __MINGW32__ */

#endif /* _LKL_LIB_VIRTIO_H */
