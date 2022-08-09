// SPDX-License-Identifier: GPL-2.0
#include <linux/bug.h>

void check(void)
{
	/*
	 * These kconfig symbols must be set to "m" for cxl_test to load
	 * and operate.
	 */
	BUILD_BUG_ON(!IS_MODULE(CONFIG_CXL_BUS));
	BUILD_BUG_ON(!IS_MODULE(CONFIG_CXL_ACPI));
	BUILD_BUG_ON(!IS_MODULE(CONFIG_CXL_PMEM));
}
