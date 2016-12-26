/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef __RDMA_COMMON__
#define __RDMA_COMMON__
/************************/
/* RDMA FW CONSTANTS */
/************************/

#define RDMA_RESERVED_LKEY                      (0)
#define RDMA_RING_PAGE_SIZE                     (0x1000)

#define RDMA_MAX_SGE_PER_SQ_WQE         (4)
#define RDMA_MAX_SGE_PER_RQ_WQE         (4)

#define RDMA_MAX_DATA_SIZE_IN_WQE       (0x7FFFFFFF)

#define RDMA_REQ_RD_ATOMIC_ELM_SIZE             (0x50)
#define RDMA_RESP_RD_ATOMIC_ELM_SIZE    (0x20)

#define RDMA_MAX_CQS                            (64 * 1024)
#define RDMA_MAX_TIDS                           (128 * 1024 - 1)
#define RDMA_MAX_PDS                            (64 * 1024)

#define RDMA_NUM_STATISTIC_COUNTERS                     MAX_NUM_VPORTS
#define RDMA_NUM_STATISTIC_COUNTERS_BB			MAX_NUM_VPORTS_BB

#define RDMA_TASK_TYPE (PROTOCOLID_ROCE)

struct rdma_srq_id {
	__le16 srq_idx;
	__le16 opaque_fid;
};

struct rdma_srq_producers {
	__le32 sge_prod;
	__le32 wqe_prod;
};

#endif /* __RDMA_COMMON__ */
