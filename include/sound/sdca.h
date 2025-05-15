/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 *
 * Copyright(c) 2024 Intel Corporation
 */

#ifndef __SDCA_H__
#define __SDCA_H__

#include <linux/types.h>
#include <linux/kconfig.h>

struct sdw_slave;

#define SDCA_MAX_FUNCTION_COUNT 8

/**
 * struct sdca_function_desc - short descriptor for an SDCA Function
 * @node: firmware node for the Function.
 * @name: Human-readable string.
 * @type: Function topology type.
 * @adr: ACPI address (used for SDCA register access).
 */
struct sdca_function_desc {
	struct fwnode_handle *node;
	const char *name;
	u32 type;
	u8 adr;
};

/**
 * struct sdca_device_data - structure containing all SDCA related information
 * @interface_revision: Value read from _DSD property, mainly to check
 * for changes between silicon versions.
 * @num_functions: Total number of supported SDCA functions. Invalid/unsupported
 * functions will be skipped.
 * @function: Array of function descriptors.
 */
struct sdca_device_data {
	u32 interface_revision;
	int num_functions;
	struct sdca_function_desc function[SDCA_MAX_FUNCTION_COUNT];
};

enum sdca_quirk {
	SDCA_QUIRKS_RT712_VB,
};

#if IS_ENABLED(CONFIG_ACPI) && IS_ENABLED(CONFIG_SND_SOC_SDCA)

void sdca_lookup_functions(struct sdw_slave *slave);
void sdca_lookup_interface_revision(struct sdw_slave *slave);
bool sdca_device_quirk_match(struct sdw_slave *slave, enum sdca_quirk quirk);

#else

static inline void sdca_lookup_functions(struct sdw_slave *slave) {}
static inline void sdca_lookup_interface_revision(struct sdw_slave *slave) {}
static inline bool sdca_device_quirk_match(struct sdw_slave *slave, enum sdca_quirk quirk)
{
	return false;
}
#endif

#endif
