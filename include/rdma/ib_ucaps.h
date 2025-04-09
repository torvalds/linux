/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#ifndef _IB_UCAPS_H_
#define _IB_UCAPS_H_

#define UCAP_ENABLED(ucaps, type) (!!((ucaps) & (1U << (type))))

enum rdma_user_cap {
	RDMA_UCAP_MLX5_CTRL_LOCAL,
	RDMA_UCAP_MLX5_CTRL_OTHER_VHCA,
	RDMA_UCAP_MAX
};

void ib_cleanup_ucaps(void);
int ib_get_ucaps(int *fds, int fd_count, uint64_t *idx_mask);
#if IS_ENABLED(CONFIG_INFINIBAND_USER_ACCESS)
int ib_create_ucap(enum rdma_user_cap type);
void ib_remove_ucap(enum rdma_user_cap type);
#else
static inline int ib_create_ucap(enum rdma_user_cap type)
{
	return -EOPNOTSUPP;
}
static inline void ib_remove_ucap(enum rdma_user_cap type) {}
#endif /* CONFIG_INFINIBAND_USER_ACCESS */

#endif /* _IB_UCAPS_H_ */
