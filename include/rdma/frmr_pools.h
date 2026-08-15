/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
 *
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef FRMR_POOLS_H
#define FRMR_POOLS_H

#include <linux/types.h>
#include <asm/page.h>

struct ib_device;
struct ib_mr;

struct ib_frmr_key {
	u64 vendor_key;
	/* A pool with non-zero kernel_vendor_key is a kernel-only pool. */
	u64 kernel_vendor_key;
	size_t num_dma_blocks;
	int access_flags;
	u8 ats:1;
};

struct ib_frmr_pool_ops {
	int (*create_frmrs)(struct ib_device *device, struct ib_frmr_key *key,
			    u32 *handles, u32 count);
	void (*destroy_frmrs)(struct ib_device *device, u32 *handles,
			      u32 count);
	int (*build_key)(struct ib_device *device, const struct ib_frmr_key *in,
			 struct ib_frmr_key *out);
};

int ib_frmr_pools_init(struct ib_device *device,
		       const struct ib_frmr_pool_ops *pool_ops);
void ib_frmr_pools_cleanup(struct ib_device *device);
int ib_frmr_pool_pop(struct ib_device *device, struct ib_mr *mr);
void ib_frmr_pool_push(struct ib_device *device, struct ib_mr *mr);
void ib_frmr_pool_drop(struct ib_mr *mr);

#endif /* FRMR_POOLS_H */
