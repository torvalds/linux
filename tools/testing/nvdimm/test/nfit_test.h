/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef __NFIT_TEST_H__
#define __NFIT_TEST_H__

struct nfit_test_resource {
	struct list_head list;
	struct resource *res;
	struct device *dev;
	void *buf;
};

typedef struct nfit_test_resource *(*nfit_test_lookup_fn)(resource_size_t);
void __iomem *__wrap_ioremap_nocache(resource_size_t offset,
		unsigned long size);
void __wrap_iounmap(volatile void __iomem *addr);
void nfit_test_setup(nfit_test_lookup_fn lookup);
void nfit_test_teardown(void);
#endif
