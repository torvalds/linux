/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* Copyright 2021 Marvell. All rights reserved. */

#ifndef __NVMETCP_COMMON__
#define __NVMETCP_COMMON__

#include "tcp_common.h"

/* NVMeTCP firmware function init parameters */
struct nvmetcp_spe_func_init {
	__le16 half_way_close_timeout;
	u8 num_sq_pages_in_ring;
	u8 num_r2tq_pages_in_ring;
	u8 num_uhq_pages_in_ring;
	u8 ll2_rx_queue_id;
	u8 flags;
#define NVMETCP_SPE_FUNC_INIT_COUNTERS_EN_MASK 0x1
#define NVMETCP_SPE_FUNC_INIT_COUNTERS_EN_SHIFT 0
#define NVMETCP_SPE_FUNC_INIT_NVMETCP_MODE_MASK 0x1
#define NVMETCP_SPE_FUNC_INIT_NVMETCP_MODE_SHIFT 1
#define NVMETCP_SPE_FUNC_INIT_RESERVED0_MASK 0x3F
#define NVMETCP_SPE_FUNC_INIT_RESERVED0_SHIFT 2
	u8 debug_flags;
	__le16 reserved1;
	u8 params;
#define NVMETCP_SPE_FUNC_INIT_MAX_SYN_RT_MASK	0xF
#define NVMETCP_SPE_FUNC_INIT_MAX_SYN_RT_SHIFT	0
#define NVMETCP_SPE_FUNC_INIT_RESERVED1_MASK	0xF
#define NVMETCP_SPE_FUNC_INIT_RESERVED1_SHIFT	4
	u8 reserved2[5];
	struct scsi_init_func_params func_params;
	struct scsi_init_func_queues q_params;
};

/* NVMeTCP init params passed by driver to FW in NVMeTCP init ramrod. */
struct nvmetcp_init_ramrod_params {
	struct nvmetcp_spe_func_init nvmetcp_init_spe;
	struct tcp_init_params tcp_init;
};

/* NVMeTCP Ramrod Command IDs */
enum nvmetcp_ramrod_cmd_id {
	NVMETCP_RAMROD_CMD_ID_UNUSED = 0,
	NVMETCP_RAMROD_CMD_ID_INIT_FUNC = 1,
	NVMETCP_RAMROD_CMD_ID_DESTROY_FUNC = 2,
	MAX_NVMETCP_RAMROD_CMD_ID
};

struct nvmetcp_glbl_queue_entry {
	struct regpair cq_pbl_addr;
	struct regpair reserved;
};

#endif /* __NVMETCP_COMMON__ */
