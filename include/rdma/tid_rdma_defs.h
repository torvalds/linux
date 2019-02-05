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

struct tid_rdma_write_req {
	__le32 kdeth0;
	__le32 kdeth1;
	struct ib_reth reth;
	__be32 reserved[2];
	__be32 verbs_qp;
};

struct tid_rdma_write_resp {
	__le32 kdeth0;
	__le32 kdeth1;
	__be32 aeth;
	__be32 reserved[3];
	__be32 tid_flow_psn;
	__be32 tid_flow_qp;
	__be32 verbs_qp;
};

struct tid_rdma_write_data {
	__le32 kdeth0;
	__le32 kdeth1;
	__be32 reserved[6];
	__be32 verbs_qp;
};

struct tid_rdma_resync {
	__le32 kdeth0;
	__le32 kdeth1;
	__be32 reserved[6];
	__be32 verbs_qp;
};

struct tid_rdma_ack {
	__le32 kdeth0;
	__le32 kdeth1;
	__be32 aeth;
	__be32 reserved[2];
	__be32 tid_flow_psn;
	__be32 verbs_psn;
	__be32 tid_flow_qp;
	__be32 verbs_qp;
};

/*
 * TID RDMA Opcodes
 */
#define IB_OPCODE_TID_RDMA 0xe0
enum {
	IB_OPCODE_WRITE_REQ       = 0x0,
	IB_OPCODE_WRITE_RESP      = 0x1,
	IB_OPCODE_WRITE_DATA      = 0x2,
	IB_OPCODE_WRITE_DATA_LAST = 0x3,
	IB_OPCODE_READ_REQ        = 0x4,
	IB_OPCODE_READ_RESP       = 0x5,
	IB_OPCODE_RESYNC          = 0x6,
	IB_OPCODE_ACK             = 0x7,

	IB_OPCODE(TID_RDMA, WRITE_REQ),
	IB_OPCODE(TID_RDMA, WRITE_RESP),
	IB_OPCODE(TID_RDMA, WRITE_DATA),
	IB_OPCODE(TID_RDMA, WRITE_DATA_LAST),
	IB_OPCODE(TID_RDMA, READ_REQ),
	IB_OPCODE(TID_RDMA, READ_RESP),
	IB_OPCODE(TID_RDMA, RESYNC),
	IB_OPCODE(TID_RDMA, ACK),
};

#define TID_OP(x) IB_OPCODE_TID_RDMA_##x

/*
 * Define TID RDMA specific WR opcodes. The ib_wr_opcode
 * enum already provides some reserved values for use by
 * low level drivers. Two of those are used but renamed
 * to be more descriptive.
 */
#define IB_WR_TID_RDMA_WRITE IB_WR_RESERVED1
#define IB_WR_TID_RDMA_READ  IB_WR_RESERVED2

#endif /* TID_RDMA_DEFS_H */
