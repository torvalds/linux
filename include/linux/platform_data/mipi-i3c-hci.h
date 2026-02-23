/* SPDX-License-Identifier: GPL-2.0 */
#ifndef INCLUDE_PLATFORM_DATA_MIPI_I3C_HCI_H
#define INCLUDE_PLATFORM_DATA_MIPI_I3C_HCI_H

#include <linux/compiler_types.h>

/**
 * struct mipi_i3c_hci_platform_data - Platform-dependent data for mipi_i3c_hci
 * @base_regs: Register set base address (to support multi-bus instances)
 */
struct mipi_i3c_hci_platform_data {
	void __iomem *base_regs;
};

#endif
