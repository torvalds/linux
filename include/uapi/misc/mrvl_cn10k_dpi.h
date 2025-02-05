/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Marvell Octeon CN10K DPI driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#ifndef __MRVL_CN10K_DPI_H__
#define __MRVL_CN10K_DPI_H__

#include <linux/types.h>

#define DPI_MAX_ENGINES 6

struct dpi_mps_mrrs_cfg {
	__u16 max_read_req_sz; /* Max read request size */
	__u16 max_payload_sz;  /* Max payload size */
	__u16 port; /* Ebus port */
	__u16 reserved; /* Reserved */
};

struct dpi_engine_cfg {
	__u64 fifo_mask; /* FIFO size mask in KBytes */
	__u16 molr[DPI_MAX_ENGINES]; /* Max outstanding load requests */
	__u16 update_molr; /* '1' to update engine MOLR */
	__u16 reserved; /* Reserved */
};

/* DPI ioctl numbers */
#define DPI_MAGIC_NUM	0xB8

/* Set MPS & MRRS parameters */
#define DPI_MPS_MRRS_CFG _IOW(DPI_MAGIC_NUM, 1, struct dpi_mps_mrrs_cfg)

/* Set Engine FIFO configuration */
#define DPI_ENGINE_CFG   _IOW(DPI_MAGIC_NUM, 2, struct dpi_engine_cfg)

#endif /* __MRVL_CN10K_DPI_H__ */
