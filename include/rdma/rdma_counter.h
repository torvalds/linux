/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2019 Mellanox Technologies. All rights reserved.
 */

#ifndef _RDMA_COUNTER_H_
#define _RDMA_COUNTER_H_

#include <rdma/ib_verbs.h>
#include <rdma/restrack.h>

struct rdma_counter {
	struct rdma_restrack_entry	res;
	struct ib_device		*device;
	uint32_t			id;
	u8				port;
};
#endif /* _RDMA_COUNTER_H_ */
