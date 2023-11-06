/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BSG_H
#define _LINUX_BSG_H

#include <uapi/linux/bsg.h>

struct bsg_device;
struct device;
struct request_queue;

typedef int (bsg_sg_io_fn)(struct request_queue *, struct sg_io_v4 *hdr,
		bool open_for_write, unsigned int timeout);

struct bsg_device *bsg_register_queue(struct request_queue *q,
		struct device *parent, const char *name,
		bsg_sg_io_fn *sg_io_fn);
void bsg_unregister_queue(struct bsg_device *bcd);

#endif /* _LINUX_BSG_H */
