/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2018 Intel Corporation.
 *
 */

#ifndef TID_RDMA_DEFS_H
#define TID_RDMA_DEFS_H

#include <rdma/ib_pack.h>

struct tid_rdma_read_req {
	__le32 kdeth0;
	__le32 kdeth1;
	struct ib_reth reth;
	__be32 tid_flow_psn;
	__be32 tid_flow_qp;
	__be32 verbs_qp;
};

struct tid_rdma_read_resp {
	__le32 kdeth0;
	__le32 kdeth1;
	__be32 aeth;
	__be32 reserved[4];
	__be32 verbs_psn;
	__be32 verbs_qp;
};

/*
 * TID RDMA Opcodes
 */
#define IB_OPCODE_TID_RDMA 0xe0
enum {
	IB_OPCODE_READ_REQ        = 0x4,
	IB_OPCODE_READ_RESP       = 0x5,

	IB_OPCODE(TID_RDMA, READ_REQ),
	IB_OPCODE(TID_RDMA, READ_RESP),
};

#define TID_OP(x) IB_OPCODE_TID_RDMA_##x

/*
 * Define TID RDMA specific WR opcodes. The ib_wr_opcode
 * enum already provides some reserved values for use by
 * low level drivers. Two of those are used but renamed
 * to be more descriptive.
 */
#define IB_WR_TID_RDMA_READ  IB_WR_RESERVED2

#endif /* TID_RDMA_DEFS_H */
