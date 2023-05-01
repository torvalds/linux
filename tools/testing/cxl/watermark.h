// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#ifndef _TEST_CXL_WATERMARK_H_
#define _TEST_CXL_WATERMARK_H_
#include <linux/module.h>
#include <linux/printk.h>

int cxl_acpi_test(void);
int cxl_core_test(void);
int cxl_mem_test(void);
int cxl_pmem_test(void);
int cxl_port_test(void);

/*
 * dummy routine for cxl_test to validate it is linking to the properly
 * mocked module and not the standard one from the base tree.
 */
#define cxl_test_watermark(x)				\
int x##_test(void)					\
{							\
	pr_debug("%s for cxl_test\n", KBUILD_MODNAME);	\
	return 0;					\
}							\
EXPORT_SYMBOL(x##_test)
#endif /* _TEST_CXL_WATERMARK_H_ */
