// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2018 Intel Corporation. All rights reserved.
#ifndef _TEST_NVDIMM_WATERMARK_H_
#define _TEST_NVDIMM_WATERMARK_H_
int pmem_test(void);
int libnvdimm_test(void);
int acpi_nfit_test(void);
int device_dax_test(void);

/*
 * dummy routine for nfit_test to validate it is linking to the properly
 * mocked module and not the standard one from the base tree.
 */
#define nfit_test_watermark(x)				\
int x##_test(void)					\
{							\
	pr_debug("%s for nfit_test\n", KBUILD_MODNAME);	\
	return 0;					\
}							\
EXPORT_SYMBOL(x##_test)
#endif /* _TEST_NVDIMM_WATERMARK_H_ */
