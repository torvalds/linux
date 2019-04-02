// SPDX-License-Identifier: GPL-2.0
#include <linux/.h>

void check(void)
{
	/*
	 * These kconfig symbols must be set to "m" for nfit_test to
	 * load and operate.
	 */
	BUILD__ON(!IS_MODULE(CONFIG_LIBNVDIMM));
	BUILD__ON(!IS_MODULE(CONFIG_BLK_DEV_PMEM));
	BUILD__ON(!IS_MODULE(CONFIG_ND_BTT));
	BUILD__ON(!IS_MODULE(CONFIG_ND_PFN));
	BUILD__ON(!IS_MODULE(CONFIG_ND_BLK));
	BUILD__ON(!IS_MODULE(CONFIG_ACPI_NFIT));
	BUILD__ON(!IS_MODULE(CONFIG_DEV_DAX));
	BUILD__ON(!IS_MODULE(CONFIG_DEV_DAX_PMEM));
}
