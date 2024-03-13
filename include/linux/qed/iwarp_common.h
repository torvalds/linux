/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef __IWARP_COMMON__
#define __IWARP_COMMON__

#include <linux/qed/rdma_common.h>

/************************/
/* IWARP FW CONSTANTS	*/
/************************/

#define IWARP_ACTIVE_MODE 0
#define IWARP_PASSIVE_MODE 1

#define IWARP_SHARED_QUEUE_PAGE_SIZE		(0x8000)
#define IWARP_SHARED_QUEUE_PAGE_RQ_PBL_OFFSET	(0x4000)
#define IWARP_SHARED_QUEUE_PAGE_RQ_PBL_MAX_SIZE	(0x1000)
#define IWARP_SHARED_QUEUE_PAGE_SQ_PBL_OFFSET	(0x5000)
#define IWARP_SHARED_QUEUE_PAGE_SQ_PBL_MAX_SIZE	(0x3000)

#define IWARP_REQ_MAX_INLINE_DATA_SIZE		(128)
#define IWARP_REQ_MAX_SINGLE_SQ_WQE_SIZE	(176)

#define IWARP_MAX_QPS				(64 * 1024)

#endif /* __IWARP_COMMON__ */
