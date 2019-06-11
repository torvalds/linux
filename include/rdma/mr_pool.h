/*
 * Copyright (c) 2016 HGST, a Western Digital Company.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#ifndef _RDMA_MR_POOL_H
#define _RDMA_MR_POOL_H 1

#include <rdma/ib_verbs.h>

struct ib_mr *ib_mr_pool_get(struct ib_qp *qp, struct list_head *list);
void ib_mr_pool_put(struct ib_qp *qp, struct list_head *list, struct ib_mr *mr);

int ib_mr_pool_init(struct ib_qp *qp, struct list_head *list, int nr,
		enum ib_mr_type type, u32 max_num_sg, u32 max_num_meta_sg);
void ib_mr_pool_destroy(struct ib_qp *qp, struct list_head *list);

#endif /* _RDMA_MR_POOL_H */
