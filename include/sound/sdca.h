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
 * sdca_device_desc - short descriptor for an SDCA Function
 * @adr: ACPI address (used for SDCA register access)
 * @type: Function topology type
 * @name: human-readable string
 */
struct sdca_function_desc {
	const char *name;
	u32 type;
	u8 adr;
};

/**
 * sdca_device_data - structure containing all SDCA related information
 * @sdca_interface_revision: value read from _DSD property, mainly to check
 * for changes between silicon versions
 * @num_functions: total number of supported SDCA functions. Invalid/unsupported
 * functions will be skipped.
 * @sdca_func: array of function descriptors
 */
struct sdca_device_data {
	u32 interface_revision;
	int num_functions;
	struct sdca_function_desc sdca_func[SDCA_MAX_FUNCTION_COUNT];
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
