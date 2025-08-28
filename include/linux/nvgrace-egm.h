/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#ifndef NVGRACE_EGM_H
#define NVGRACE_EGM_H

#include <linux/auxiliary_bus.h>

#define NVGRACE_EGM_DEV_NAME "egm"

struct gpu_node {
	struct list_head list;
	struct pci_dev *pdev;
};

struct nvgrace_egm_dev {
	struct auxiliary_device aux_dev;
	u64 egmpxm;
	struct list_head gpus;
};

struct nvgrace_egm_dev_entry {
	struct list_head list;
	struct nvgrace_egm_dev *egm_dev;
};

#endif /* NVGRACE_EGM_H */
