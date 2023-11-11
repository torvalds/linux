/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Renesas RPC-IF core driver
 *
 * Copyright (C) 2018~2019 Renesas Solutions Corp.
 * Copyright (C) 2019 Macronix International Co., Ltd.
 * Copyright (C) 2019-2020 Cogent Embedded, Inc.
 */

#ifndef __RENESAS_RPC_IF_H
#define __RENESAS_RPC_IF_H

#include <linux/pm_runtime.h>
#include <linux/types.h>

enum rpcif_data_dir {
	RPCIF_NO_DATA,
	RPCIF_DATA_IN,
	RPCIF_DATA_OUT,
};

struct rpcif_op {
	struct {
		u8 buswidth;
		u8 opcode;
		bool ddr;
	} cmd, ocmd;

	struct {
		u8 nbytes;
		u8 buswidth;
		bool ddr;
		u64 val;
	} addr;

	struct {
		u8 ncycles;
		u8 buswidth;
	} dummy;

	struct {
		u8 nbytes;
		u8 buswidth;
		bool ddr;
		u32 val;
	} option;

	struct {
		u8 buswidth;
		unsigned int nbytes;
		enum rpcif_data_dir dir;
		bool ddr;
		union {
			void *in;
			const void *out;
		} buf;
	} data;
};

enum rpcif_type {
	RPCIF_RCAR_GEN3,
	RPCIF_RZ_G2L,
};

struct rpcif {
	struct device *dev;
	void __iomem *dirmap;
	size_t size;
};

int rpcif_sw_init(struct rpcif *rpc, struct device *dev);
int rpcif_hw_init(struct rpcif *rpc, bool hyperflash);
void rpcif_prepare(struct rpcif *rpc, const struct rpcif_op *op, u64 *offs,
		   size_t *len);
int rpcif_manual_xfer(struct rpcif *rpc);
ssize_t rpcif_dirmap_read(struct rpcif *rpc, u64 offs, size_t len, void *buf);

static inline void rpcif_enable_rpm(struct rpcif *rpc)
{
	pm_runtime_enable(rpc->dev);
}

static inline void rpcif_disable_rpm(struct rpcif *rpc)
{
	pm_runtime_disable(rpc->dev);
}

#endif // __RENESAS_RPC_IF_H
