/* SPDX-License-Identifier: GPL-2.0
 *
 * Driver for AMD network controllers and boards
 *
 * Copyright (C) 2021, Xilinx, Inc.
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#ifndef MC_CDX_PCOL_H
#define MC_CDX_PCOL_H
#include  <linux/cdx/mcdi.h>

#define MC_CMD_EDAC_GET_DDR_CONFIG_OUT_WORD_LENGTH_LEN		4
/* Number of registers for the DDR controller */
#define MC_CMD_GET_DDR_CONFIG_OFST	4
#define MC_CMD_GET_DDR_CONFIG_LEN	4

/***********************************/
/* MC_CMD_EDAC_GET_DDR_CONFIG
 * Provides detailed configuration for the DDR controller of the given index.
 */
#define MC_CMD_EDAC_GET_DDR_CONFIG 0x3

/* MC_CMD_EDAC_GET_DDR_CONFIG_IN msgrequest */
#define MC_CMD_EDAC_GET_DDR_CONFIG_IN_CONTROLLER_INDEX_OFST		0
#define MC_CMD_EDAC_GET_DDR_CONFIG_IN_CONTROLLER_INDEX_LEN		4

#endif /* MC_CDX_PCOL_H */
