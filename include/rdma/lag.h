/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2020 Mellanox Technologies. All rights reserved.
 */

#ifndef _RDMA_LAG_H_
#define _RDMA_LAG_H_

#include <net/lag.h>

struct ib_device;
struct rdma_ah_attr;

enum rdma_lag_flags {
	RDMA_LAG_FLAGS_HASH_ALL_SLAVES = 1 << 0
};

void rdma_lag_put_ah_roce_slave(struct net_device *xmit_slave);
struct net_device *rdma_lag_get_ah_roce_slave(struct ib_device *device,
					      struct rdma_ah_attr *ah_attr,
					      gfp_t flags);

#endif /* _RDMA_LAG_H_ */
