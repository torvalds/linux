/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2005-2006 Intel Corporation.  All rights reserved.
 */

#ifndef IB_USER_MARSHALL_H
#define IB_USER_MARSHALL_H

#include <rdma/ib_verbs.h>
#include <rdma/ib_sa.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_user_sa.h>

void ib_copy_qp_attr_to_user(struct ib_device *device,
			     struct ib_uverbs_qp_attr *dst,
			     struct ib_qp_attr *src);

void ib_copy_ah_attr_to_user(struct ib_device *device,
			     struct ib_uverbs_ah_attr *dst,
			     struct rdma_ah_attr *src);

void ib_copy_path_rec_to_user(struct ib_user_path_rec *dst,
			      struct sa_path_rec *src);

#endif /* IB_USER_MARSHALL_H */
