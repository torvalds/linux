/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018-2020 Intel Corporation */

#ifndef __LINUX_MFD_INTEL_PECI_CLIENT_H
#define __LINUX_MFD_INTEL_PECI_CLIENT_H

#include <linux/peci.h>

#if IS_ENABLED(CONFIG_X86)
#include <asm/intel-family.h>
#else
/*
 * Architectures other than x86 cannot include the header file so define these
 * at here. These are needed for detecting type of client x86 CPUs behind a PECI
 * connection.
 */
#define INTEL_FAM6_HASWELL_X		0x3F
#define INTEL_FAM6_BROADWELL_X		0x4F
#define INTEL_FAM6_SKYLAKE_X		0x55
#define INTEL_FAM6_SKYLAKE_XD		0x56
#define INTEL_FAM6_ICELAKE_X		0x6A
#define INTEL_FAM6_ICELAKE_XD		0x6C
#endif

#define INTEL_FAM6             6 /* P6 (Pentium Pro and later) */

#define CORE_MASK_BITS_ON_HSX  18
#define CHAN_RANK_MAX_ON_HSX   8  /* Max number of channel ranks on Haswell */
#define DIMM_IDX_MAX_ON_HSX    3  /* Max DIMM index per channel on Haswell */

#define CORE_MASK_BITS_ON_BDX  24
#define CHAN_RANK_MAX_ON_BDX   4  /* Max number of channel ranks on Broadwell */
#define DIMM_IDX_MAX_ON_BDX    3  /* Max DIMM index per channel on Broadwell */

#define CORE_MASK_BITS_ON_SKX  28
#define CHAN_RANK_MAX_ON_SKX   6  /* Max number of channel ranks on Skylake */
#define DIMM_IDX_MAX_ON_SKX    2  /* Max DIMM index per channel on Skylake */

#define CORE_MASK_BITS_ON_SKXD 28
#define CHAN_RANK_MAX_ON_SKXD  2  /* Max number of channel ranks on Skylake D */
#define DIMM_IDX_MAX_ON_SKXD   2  /* Max DIMM index per channel on Skylake D */

#define CORE_MASK_BITS_ON_ICX  64
#define CHAN_RANK_MAX_ON_ICX   8  /* Max number of channel ranks on Icelake */
#define DIMM_IDX_MAX_ON_ICX    2  /* Max DIMM index per channel on Icelake */

#define CORE_MASK_BITS_ON_ICXD 64
#define CHAN_RANK_MAX_ON_ICXD  4  /* Max number of channel ranks on Icelake D */
#define DIMM_IDX_MAX_ON_ICXD   2  /* Max DIMM index per channel on Icelake D */

#define CORE_MASK_BITS_MAX     CORE_MASK_BITS_ON_ICX
#define CHAN_RANK_MAX          CHAN_RANK_MAX_ON_HSX
#define DIMM_IDX_MAX           DIMM_IDX_MAX_ON_HSX
#define DIMM_NUMS_MAX          (CHAN_RANK_MAX * DIMM_IDX_MAX)

/**
 * struct cpu_gen_info - CPU generation specific information
 * @family: CPU family ID
 * @model: CPU model
 * @core_mask_bits: number of resolved core bits
 * @chan_rank_max: max number of channel ranks
 * @dimm_idx_max: max number of DIMM indices
 *
 * CPU generation specific information to identify maximum number of cores and
 * DIMM slots.
 */
struct cpu_gen_info {
	u16  family;
	u8   model;
	uint core_mask_bits;
	uint chan_rank_max;
	uint dimm_idx_max;
};

/**
 * struct peci_client_manager - PECI client manager information
 * @client; pointer to the PECI client
 * @name: PECI client manager name
 * @gen_info: CPU generation info of the detected CPU
 *
 * PECI client manager information for managing PECI sideband functions on a CPU
 * client.
 */
struct peci_client_manager {
	struct peci_client *client;
	char name[PECI_NAME_SIZE];
	const struct cpu_gen_info *gen_info;
};

/**
 * peci_client_read_package_config - read from the Package Configuration Space
 * @priv: driver private data structure
 * @index: encoding index for the requested service
 * @param: parameter to specify the exact data being requested
 * @data: data buffer to store the result
 * Context: can sleep
 *
 * A generic PECI command that provides read access to the
 * "Package Configuration Space" that is maintained by the PCU, including
 * various power and thermal management functions. Typical PCS read services
 * supported by the processor may include access to temperature data, energy
 * status, run time information, DIMM temperatures and so on.
 *
 * Return: zero on success, else a negative error code.
 */
static inline int
peci_client_read_package_config(struct peci_client_manager *priv,
				u8 index, u16 param, u8 *data)
{
	struct peci_rd_pkg_cfg_msg msg;
	int ret;

	msg.addr = priv->client->addr;
	msg.index = index;
	msg.param = param;
	msg.rx_len = 4;

	ret = peci_command(priv->client->adapter, PECI_CMD_RD_PKG_CFG, &msg);
	if (msg.cc != PECI_DEV_CC_SUCCESS)
		ret = -EAGAIN;
	if (ret)
		return ret;

	memcpy(data, msg.pkg_config, 4);

	return 0;
}

/**
 * peci_client_write_package_config - write to the Package Configuration Space
 * @priv: driver private data structure
 * @index: encoding index for the requested service
 * @param: parameter to specify the exact data being requested
 * @data: data buffer with values to write
 * Context: can sleep
 *
 * Return: zero on success, else a negative error code.
 */
static inline int
peci_client_write_package_config(struct peci_client_manager *priv,
				 u8 index, u16 param, u8 *data)
{
	struct peci_rd_pkg_cfg_msg msg;
	int ret;

	msg.addr = priv->client->addr;
	msg.index = index;
	msg.param = param;
	msg.rx_len = 4u;
	memcpy(msg.pkg_config, data, msg.rx_len);

	ret = peci_command(priv->client->adapter, PECI_CMD_WR_PKG_CFG, &msg);
	if (!ret) {
		if (msg.cc != PECI_DEV_CC_SUCCESS)
			ret = -EAGAIN;
	}

	return ret;
}

#endif /* __LINUX_MFD_INTEL_PECI_CLIENT_H */
