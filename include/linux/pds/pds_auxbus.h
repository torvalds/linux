/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#ifndef _PDSC_AUXBUS_H_
#define _PDSC_AUXBUS_H_

#include <linux/auxiliary_bus.h>

struct pds_auxiliary_dev {
	struct auxiliary_device aux_dev;
	struct pci_dev *vf_pdev;
	u16 client_id;
};
#endif /* _PDSC_AUXBUS_H_ */
