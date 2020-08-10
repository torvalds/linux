/* SPDX-License-Identifier: GPL-2.0 */
/*
 * * Copyright (c) 2020, Oracle and/or its affiliates.
 */

#ifndef RPC_RDMA_CID_H
#define RPC_RDMA_CID_H

/*
 * The rpc_rdma_cid struct records completion ID information. A
 * completion ID matches an incoming Send or Receive completion
 * to a Completion Queue and to a previous ib_post_*(). The ID
 * can then be displayed in an error message or recorded in a
 * trace record.
 *
 * This struct is shared between the server and client RPC/RDMA
 * transport implementations.
 */
struct rpc_rdma_cid {
	u32			ci_queue_id;
	int			ci_completion_id;
};

#endif	/* RPC_RDMA_CID_H */
