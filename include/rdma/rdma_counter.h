/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2019 Mellanox Technologies. All rights reserved.
 */

#ifndef _RDMA_COUNTER_H_
#define _RDMA_COUNTER_H_

#include <linux/mutex.h>

#include <rdma/ib_verbs.h>
#include <rdma/restrack.h>
#include <rdma/rdma_netlink.h>

struct auto_mode_param {
	int qp_type;
};

struct rdma_counter_mode {
	enum rdma_nl_counter_mode mode;
	enum rdma_nl_counter_mask mask;
	struct auto_mode_param param;
};

struct rdma_port_counter {
	struct rdma_counter_mode mode;
	struct mutex lock;
};

struct rdma_counter {
	struct rdma_restrack_entry	res;
	struct ib_device		*device;
	uint32_t			id;
	u8				port;
};

void rdma_counter_init(struct ib_device *dev);
void rdma_counter_release(struct ib_device *dev);
int rdma_counter_set_auto_mode(struct ib_device *dev, u8 port,
			       bool on, enum rdma_nl_counter_mask mask);

#endif /* _RDMA_COUNTER_H_ */
