/* SPDX-License-Identifier: GPL-2.0 */
#ifndef INCLUDE_PLATFORM_DATA_MIPI_I3C_HCI_H
#define INCLUDE_PLATFORM_DATA_MIPI_I3C_HCI_H

#include <linux/compiler_types.h>
#include <linux/types.h>

/**
 * struct mipi_i3c_hci_platform_data - Platform-dependent data for mipi_i3c_hci
 * @base_regs: Register set base address (to support multi-bus instances)
 * @instance: Zero-based instance number of the Bus Controller as defined by the
 *            DisCo specification I3C Target Address (_ADR) Encoding
 */
struct mipi_i3c_hci_platform_data {
	void __iomem *base_regs;
	u8 instance;
};

#endif
