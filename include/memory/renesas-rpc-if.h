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

struct rpcif {
	struct device *dev;
	void __iomem *dirmap;
	struct regmap *regmap;
	struct reset_control *rstc;
	size_t size;
	enum rpcif_data_dir dir;
	u8 bus_size;
	void *buffer;
	u32 xferlen;
	u32 smcr;
	u32 smadr;
	u32 command;		/* DRCMR or SMCMR */
	u32 option;		/* DROPR or SMOPR */
	u32 enable;		/* DRENR or SMENR */
	u32 dummy;		/* DRDMCR or SMDMCR */
	u32 ddr;		/* DRDRENR or SMDRENR */
};

int rpcif_sw_init(struct rpcif *rpc, struct device *dev);
void rpcif_hw_init(struct rpcif *rpc, bool hyperflash);
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
