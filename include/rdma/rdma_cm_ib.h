/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2006 Intel Corporation.  All rights reserved.
 */

#ifndef RDMA_CM_IB_H
#define RDMA_CM_IB_H

#include <rdma/rdma_cm.h>

/**
 * rdma_set_ib_path - Manually sets the path record used to establish a
 *   connection.
 * @id: Connection identifier associated with the request.
 * @path_rec: Reference to the path record
 *
 * This call permits a user to specify routing information for rdma_cm_id's
 * bound to InfiniBand devices. It is called on the client side of a
 * connection and replaces the call to rdma_resolve_route.
 */
int rdma_set_ib_path(struct rdma_cm_id *id,
		     struct sa_path_rec *path_rec);

/* Global qkey for UDP QPs and multicast groups. */
#define RDMA_UDP_QKEY 0x01234567

#endif /* RDMA_CM_IB_H */
