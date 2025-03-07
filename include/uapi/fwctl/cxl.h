/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2024-2025 Intel Corporation
 *
 * These are definitions for the mailbox command interface of CXL subsystem.
 */
#ifndef _UAPI_FWCTL_CXL_H_
#define _UAPI_FWCTL_CXL_H_

#include <linux/types.h>
#include <linux/stddef.h>
#include <cxl/features.h>

/**
 * struct fwctl_rpc_cxl - ioctl(FWCTL_RPC) input for CXL
 * @opcode: CXL mailbox command opcode
 * @flags: Flags for the command (input).
 * @op_size: Size of input payload.
 * @reserved1: Reserved. Must be 0s.
 * @get_sup_feats_in: Get Supported Features input
 */
struct fwctl_rpc_cxl {
	__struct_group(fwctl_rpc_cxl_hdr, hdr, /* no attrs */,
		__u32 opcode;
		__u32 flags;
		__u32 op_size;
		__u32 reserved1;
	);
	struct cxl_mbox_get_sup_feats_in get_sup_feats_in;
};

/**
 * struct fwctl_rpc_cxl_out - ioctl(FWCTL_RPC) output for CXL
 * @size: Size of the output payload
 * @retval: Return value from device
 * @get_sup_feats_out: Get Supported Features output
 */
struct fwctl_rpc_cxl_out {
	__struct_group(fwctl_rpc_cxl_out_hdr, hdr, /* no attrs */,
		__u32 size;
		__u32 retval;
	);
	struct cxl_mbox_get_sup_feats_out get_sup_feats_out;
};

#endif
