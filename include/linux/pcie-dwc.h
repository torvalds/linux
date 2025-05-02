/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021-2023 Alibaba Inc.
 * Copyright (C) 2025 Linaro Ltd.
 *
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#ifndef LINUX_PCIE_DWC_H
#define LINUX_PCIE_DWC_H

#include <linux/pci_ids.h>

struct dwc_pcie_vsec_id {
	u16 vendor_id;
	u16 vsec_id;
	u8 vsec_rev;
};

/*
 * VSEC IDs are allocated by the vendor, so a given ID may mean different
 * things to different vendors.  See PCIe r6.0, sec 7.9.5.2.
 */
static const struct dwc_pcie_vsec_id dwc_pcie_rasdes_vsec_ids[] = {
	{ .vendor_id = PCI_VENDOR_ID_ALIBABA,
	  .vsec_id = 0x02, .vsec_rev = 0x4 },
	{ .vendor_id = PCI_VENDOR_ID_AMPERE,
	  .vsec_id = 0x02, .vsec_rev = 0x4 },
	{ .vendor_id = PCI_VENDOR_ID_QCOM,
	  .vsec_id = 0x02, .vsec_rev = 0x4 },
	{ .vendor_id = PCI_VENDOR_ID_ROCKCHIP,
	  .vsec_id = 0x02, .vsec_rev = 0x4 },
	{ .vendor_id = PCI_VENDOR_ID_SAMSUNG,
	  .vsec_id = 0x02, .vsec_rev = 0x4 },
	{}
};

#endif /* LINUX_PCIE_DWC_H */
