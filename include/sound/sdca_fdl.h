/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 *
 * Copyright (C) 2025 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef __SDCA_FDL_H__
#define __SDCA_FDL_H__

struct device;
struct regmap;
struct sdca_fdl_set;
struct sdca_function_data;
struct sdca_interrupt;

/**
 * struct fdl_state - FDL state structure to keep data between interrupts
 * @set: Pointer to the FDL set currently being downloaded.
 * @file_index: Index of the current file being processed.
 */
struct fdl_state {
	struct sdca_fdl_set *set;
	int file_index;
};

#define SDCA_CTL_XU_FDLH_COMPLETE	0
#define SDCA_CTL_XU_FDLH_MORE_FILES	SDCA_CTL_XU_FDLH_SET_IN_PROGRESS
#define SDCA_CTL_XU_FDLH_FILE_AVAILABLE	(SDCA_CTL_XU_FDLH_TRANSFERRED_FILE | \
					 SDCA_CTL_XU_FDLH_SET_IN_PROGRESS)
#define SDCA_CTL_XU_FDLH_MASK		(SDCA_CTL_XU_FDLH_TRANSFERRED_CHUNK | \
					 SDCA_CTL_XU_FDLH_TRANSFERRED_FILE | \
					 SDCA_CTL_XU_FDLH_SET_IN_PROGRESS | \
					 SDCA_CTL_XU_FDLH_RESET_ACK | \
					 SDCA_CTL_XU_FDLH_REQ_ABORT)

#define SDCA_CTL_XU_FDLD_COMPLETE	0
#define SDCA_CTL_XU_FDLD_FILE_OK	(SDCA_CTL_XU_FDLH_TRANSFERRED_FILE | \
					 SDCA_CTL_XU_FDLH_SET_IN_PROGRESS | \
					 SDCA_CTL_XU_FDLD_ACK_TRANSFER | \
					 SDCA_CTL_XU_FDLD_NEEDS_SET)
#define SDCA_CTL_XU_FDLD_MORE_FILES_OK	(SDCA_CTL_XU_FDLH_SET_IN_PROGRESS | \
					 SDCA_CTL_XU_FDLD_ACK_TRANSFER | \
					 SDCA_CTL_XU_FDLD_NEEDS_SET)
#define SDCA_CTL_XU_FDLD_MASK		(SDCA_CTL_XU_FDLD_REQ_RESET | \
					 SDCA_CTL_XU_FDLD_REQ_ABORT | \
					 SDCA_CTL_XU_FDLD_ACK_TRANSFER | \
					 SDCA_CTL_XU_FDLD_NEEDS_SET)

int sdca_fdl_alloc_state(struct sdca_interrupt *interrupt);
int sdca_fdl_process(struct sdca_interrupt *interrupt);

int sdca_reset_function(struct device *dev, struct sdca_function_data *function,
			struct regmap *regmap);

#endif // __SDCA_FDL_H__
