/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2019 Mellanox Technologies. All rights reserved.
 */

#ifndef _RDMA_COUNTER_H_
#define _RDMA_COUNTER_H_

#include <linux/mutex.h>
#include <linux/pid_namespace.h>

#include <rdma/ib_verbs.h>
#include <rdma/restrack.h>
#include <rdma/rdma_netlink.h>

struct ib_qp;

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
	struct rdma_hw_stats *hstats;
	unsigned int num_counters;
	struct mutex lock;
};

struct rdma_counter {
	struct rdma_restrack_entry	res;
	struct ib_device		*device;
	uint32_t			id;
	struct kref			kref;
	struct rdma_counter_mode	mode;
	struct mutex			lock;
	struct rdma_hw_stats		*stats;
	u8				port;
};

void rdma_counter_init(struct ib_device *dev);
void rdma_counter_release(struct ib_device *dev);
int rdma_counter_set_auto_mode(struct ib_device *dev, u8 port,
			       bool on, enum rdma_nl_counter_mask mask);
int rdma_counter_bind_qp_auto(struct ib_qp *qp, u8 port);
int rdma_counter_unbind_qp(struct ib_qp *qp, bool force);

int rdma_counter_query_stats(struct rdma_counter *counter);
u64 rdma_counter_get_hwstat_value(struct ib_device *dev, u8 port, u32 index);
int rdma_counter_bind_qpn(struct ib_device *dev, u8 port,
			  u32 qp_num, u32 counter_id);
int rdma_counter_bind_qpn_alloc(struct ib_device *dev, u8 port,
				u32 qp_num, u32 *counter_id);
int rdma_counter_unbind_qpn(struct ib_device *dev, u8 port,
			    u32 qp_num, u32 counter_id);

#endif /* _RDMA_COUNTER_H_ */
