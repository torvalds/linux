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

#include <linux/completion.h>
#include <linux/workqueue.h>

struct device;
struct regmap;
struct sdca_fdl_set;
struct sdca_function_data;
struct sdca_interrupt;
struct sdca_interrupt_info;

/**
 * struct fdl_state - FDL state structure to keep data between interrupts
 * @begin: Completion indicating the start of an FDL download cycle.
 * @done: Completion indicating the end of an FDL download cycle.
 * @timeout: Delayed work used for timing out UMP transactions.
 * @lock: Mutex to protect between the timeout work and IRQ handlers.
 * @interrupt: Pointer to the interrupt struct to which this FDL is attached.
 * @set: Pointer to the FDL set currently being downloaded.
 * @file_index: Index of the current file being processed.
 */
struct fdl_state {
	struct completion begin;
	struct completion done;
	struct delayed_work timeout;
	struct mutex lock;

	struct sdca_interrupt *interrupt;
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

#if IS_ENABLED(CONFIG_SND_SOC_SDCA_FDL)

int sdca_fdl_alloc_state(struct sdca_interrupt *interrupt);
int sdca_fdl_process(struct sdca_interrupt *interrupt);
int sdca_fdl_sync(struct device *dev, struct sdca_function_data *function,
		  struct sdca_interrupt_info *info);

int sdca_reset_function(struct device *dev, struct sdca_function_data *function,
			struct regmap *regmap);

#else

static inline int sdca_fdl_alloc_state(struct sdca_interrupt *interrupt)
{
	return 0;
}

static inline int sdca_fdl_process(struct sdca_interrupt *interrupt)
{
	return 0;
}

static inline int sdca_fdl_sync(struct device *dev,
				struct sdca_function_data *function,
				struct sdca_interrupt_info *info)
{
	return 0;
}

static inline int sdca_reset_function(struct device *dev,
				      struct sdca_function_data *function,
				      struct regmap *regmap)
{
	return 0;
}

#endif // CONFIG_SND_SOC_SDCA_FDL

#endif // __SDCA_FDL_H__
