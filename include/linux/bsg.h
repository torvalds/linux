/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BSG_H
#define _LINUX_BSG_H

#include <uapi/linux/bsg.h>

struct request;

#ifdef CONFIG_BLK_DEV_BSG
struct bsg_ops {
	int	(*check_proto)(struct sg_io_v4 *hdr);
	int	(*fill_hdr)(struct request *rq, struct sg_io_v4 *hdr,
				fmode_t mode);
	int	(*complete_rq)(struct request *rq, struct sg_io_v4 *hdr);
	void	(*free_rq)(struct request *rq);
};

struct bsg_class_device {
	struct device *class_dev;
	struct device *parent;
	int minor;
	struct request_queue *queue;
	struct kref ref;
	const struct bsg_ops *ops;
	void (*release)(struct device *);
};

int bsg_register_queue(struct request_queue *q, struct device *parent,
		const char *name, const struct bsg_ops *ops,
		void (*release)(struct device *));
int bsg_scsi_register_queue(struct request_queue *q, struct device *parent);
void bsg_unregister_queue(struct request_queue *q);
#else
static inline int bsg_scsi_register_queue(struct request_queue *q,
		struct device *parent)
{
	return 0;
}
static inline void bsg_unregister_queue(struct request_queue *q)
{
}
#endif /* CONFIG_BLK_DEV_BSG */
#endif /* _LINUX_BSG_H */
