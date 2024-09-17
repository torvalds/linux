/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 */

#ifndef _LINUX_NVME_RDMA_H
#define _LINUX_NVME_RDMA_H

#define NVME_RDMA_MAX_QUEUE_SIZE	128

enum nvme_rdma_cm_fmt {
	NVME_RDMA_CM_FMT_1_0 = 0x0,
};

enum nvme_rdma_cm_status {
	NVME_RDMA_CM_INVALID_LEN	= 0x01,
	NVME_RDMA_CM_INVALID_RECFMT	= 0x02,
	NVME_RDMA_CM_INVALID_QID	= 0x03,
	NVME_RDMA_CM_INVALID_HSQSIZE	= 0x04,
	NVME_RDMA_CM_INVALID_HRQSIZE	= 0x05,
	NVME_RDMA_CM_NO_RSC		= 0x06,
	NVME_RDMA_CM_INVALID_IRD	= 0x07,
	NVME_RDMA_CM_INVALID_ORD	= 0x08,
};

static inline const char *nvme_rdma_cm_msg(enum nvme_rdma_cm_status status)
{
	switch (status) {
	case NVME_RDMA_CM_INVALID_LEN:
		return "invalid length";
	case NVME_RDMA_CM_INVALID_RECFMT:
		return "invalid record format";
	case NVME_RDMA_CM_INVALID_QID:
		return "invalid queue ID";
	case NVME_RDMA_CM_INVALID_HSQSIZE:
		return "invalid host SQ size";
	case NVME_RDMA_CM_INVALID_HRQSIZE:
		return "invalid host RQ size";
	case NVME_RDMA_CM_NO_RSC:
		return "resource not found";
	case NVME_RDMA_CM_INVALID_IRD:
		return "invalid IRD";
	case NVME_RDMA_CM_INVALID_ORD:
		return "Invalid ORD";
	default:
		return "unrecognized reason";
	}
}

/**
 * struct nvme_rdma_cm_req - rdma connect request
 *
 * @recfmt:        format of the RDMA Private Data
 * @qid:           queue Identifier for the Admin or I/O Queue
 * @hrqsize:       host receive queue size to be created
 * @hsqsize:       host send queue size to be created
 */
struct nvme_rdma_cm_req {
	__le16		recfmt;
	__le16		qid;
	__le16		hrqsize;
	__le16		hsqsize;
	u8		rsvd[24];
};

/**
 * struct nvme_rdma_cm_rep - rdma connect reply
 *
 * @recfmt:        format of the RDMA Private Data
 * @crqsize:       controller receive queue size
 */
struct nvme_rdma_cm_rep {
	__le16		recfmt;
	__le16		crqsize;
	u8		rsvd[28];
};

/**
 * struct nvme_rdma_cm_rej - rdma connect reject
 *
 * @recfmt:        format of the RDMA Private Data
 * @sts:           error status for the associated connect request
 */
struct nvme_rdma_cm_rej {
	__le16		recfmt;
	__le16		sts;
};

#endif /* _LINUX_NVME_RDMA_H */
