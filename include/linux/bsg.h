/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BSG_H
#define _LINUX_BSG_H

#include <uapi/linux/bsg.h>

struct bsg_device;
struct device;
struct request;
struct request_queue;

struct bsg_ops {
	int	(*check_proto)(struct sg_io_v4 *hdr);
	int	(*fill_hdr)(struct request *rq, struct sg_io_v4 *hdr,
				fmode_t mode);
	int	(*complete_rq)(struct request *rq, struct sg_io_v4 *hdr);
	void	(*free_rq)(struct request *rq);
};

struct bsg_device *bsg_register_queue(struct request_queue *q,
		struct device *parent, const char *name,
		const struct bsg_ops *ops);
void bsg_unregister_queue(struct bsg_device *bcd);

#endif /* _LINUX_BSG_H */
