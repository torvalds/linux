// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2018 Intel Corporation. All rights reserved.

#include <linux/module.h>
#include <linux/printk.h>
#include "watermark.h"
#include <nfit.h>

nfit_test_watermark(acpi_nfit);

/* strong / override definition of nfit_intel_shutdown_status */
void nfit_intel_shutdown_status(struct nfit_mem *nfit_mem)
{
	set_bit(NFIT_MEM_DIRTY_COUNT, &nfit_mem->flags);
	nfit_mem->dirty_shutdown = 42;
}
