/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 HGST, a Western Digital Company.
 */
#ifndef _RDMA_MR_POOL_H
#define _RDMA_MR_POOL_H 1

#include <rdma/ib_verbs.h>

struct ib_mr *ib_mr_pool_get(struct ib_qp *qp, struct list_head *list);
void ib_mr_pool_put(struct ib_qp *qp, struct list_head *list, struct ib_mr *mr);

int ib_mr_pool_init(struct ib_qp *qp, struct list_head *list, int nr,
		enum ib_mr_type type, u32 max_num_sg);
void ib_mr_pool_destroy(struct ib_qp *qp, struct list_head *list);

#endif /* _RDMA_MR_POOL_H */
