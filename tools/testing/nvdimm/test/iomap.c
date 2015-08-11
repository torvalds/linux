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
#include <linux/rculist.h>
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/io.h>
#include "nfit_test.h"

static LIST_HEAD(iomap_head);

static struct iomap_ops {
	nfit_test_lookup_fn nfit_test_lookup;
	struct list_head list;
} iomap_ops = {
	.list = LIST_HEAD_INIT(iomap_ops.list),
};

void nfit_test_setup(nfit_test_lookup_fn lookup)
{
	iomap_ops.nfit_test_lookup = lookup;
	list_add_rcu(&iomap_ops.list, &iomap_head);
}
EXPORT_SYMBOL(nfit_test_setup);

void nfit_test_teardown(void)
{
	list_del_rcu(&iomap_ops.list);
	synchronize_rcu();
}
EXPORT_SYMBOL(nfit_test_teardown);

static struct nfit_test_resource *get_nfit_res(resource_size_t resource)
{
	struct iomap_ops *ops;

	ops = list_first_or_null_rcu(&iomap_head, typeof(*ops), list);
	if (ops)
		return ops->nfit_test_lookup(resource);
	return NULL;
}

void __iomem *__nfit_test_ioremap(resource_size_t offset, unsigned long size,
		void __iomem *(*fallback_fn)(resource_size_t, unsigned long))
{
	struct nfit_test_resource *nfit_res;

	rcu_read_lock();
	nfit_res = get_nfit_res(offset);
	rcu_read_unlock();
	if (nfit_res)
		return (void __iomem *) nfit_res->buf + offset
			- nfit_res->res->start;
	return fallback_fn(offset, size);
}

void __iomem *__wrap_devm_ioremap_nocache(struct device *dev,
		resource_size_t offset, unsigned long size)
{
	struct nfit_test_resource *nfit_res;

	rcu_read_lock();
	nfit_res = get_nfit_res(offset);
	rcu_read_unlock();
	if (nfit_res)
		return (void __iomem *) nfit_res->buf + offset
			- nfit_res->res->start;
	return devm_ioremap_nocache(dev, offset, size);
}
EXPORT_SYMBOL(__wrap_devm_ioremap_nocache);

void *__wrap_devm_memremap(struct device *dev, resource_size_t offset,
		size_t size, unsigned long flags)
{
	struct nfit_test_resource *nfit_res;

	rcu_read_lock();
	nfit_res = get_nfit_res(offset);
	rcu_read_unlock();
	if (nfit_res)
		return (void __iomem *) nfit_res->buf + offset
			- nfit_res->res->start;
	return devm_memremap(dev, offset, size, flags);
}
EXPORT_SYMBOL(__wrap_devm_memremap);

void __iomem *__wrap_ioremap_nocache(resource_size_t offset, unsigned long size)
{
	return __nfit_test_ioremap(offset, size, ioremap_nocache);
}
EXPORT_SYMBOL(__wrap_ioremap_nocache);

void __iomem *__wrap_ioremap_wc(resource_size_t offset, unsigned long size)
{
	return __nfit_test_ioremap(offset, size, ioremap_wc);
}
EXPORT_SYMBOL(__wrap_ioremap_wc);

void __wrap_iounmap(volatile void __iomem *addr)
{
	struct nfit_test_resource *nfit_res;

	rcu_read_lock();
	nfit_res = get_nfit_res((unsigned long) addr);
	rcu_read_unlock();
	if (nfit_res)
		return;
	return iounmap(addr);
}
EXPORT_SYMBOL(__wrap_iounmap);

static struct resource *nfit_test_request_region(struct device *dev,
		struct resource *parent, resource_size_t start,
		resource_size_t n, const char *name, int flags)
{
	struct nfit_test_resource *nfit_res;

	if (parent == &iomem_resource) {
		rcu_read_lock();
		nfit_res = get_nfit_res(start);
		rcu_read_unlock();
		if (nfit_res) {
			struct resource *res = nfit_res->res + 1;

			if (start + n > nfit_res->res->start
					+ resource_size(nfit_res->res)) {
				pr_debug("%s: start: %llx n: %llx overflow: %pr\n",
						__func__, start, n,
						nfit_res->res);
				return NULL;
			}

			res->start = start;
			res->end = start + n - 1;
			res->name = name;
			res->flags = resource_type(parent);
			res->flags |= IORESOURCE_BUSY | flags;
			pr_debug("%s: %pr\n", __func__, res);
			return res;
		}
	}
	if (dev)
		return __devm_request_region(dev, parent, start, n, name);
	return __request_region(parent, start, n, name, flags);
}

struct resource *__wrap___request_region(struct resource *parent,
		resource_size_t start, resource_size_t n, const char *name,
		int flags)
{
	return nfit_test_request_region(NULL, parent, start, n, name, flags);
}
EXPORT_SYMBOL(__wrap___request_region);

struct resource *__wrap___devm_request_region(struct device *dev,
		struct resource *parent, resource_size_t start,
		resource_size_t n, const char *name)
{
	if (!dev)
		return NULL;
	return nfit_test_request_region(dev, parent, start, n, name, 0);
}
EXPORT_SYMBOL(__wrap___devm_request_region);

void __wrap___release_region(struct resource *parent, resource_size_t start,
				resource_size_t n)
{
	struct nfit_test_resource *nfit_res;

	if (parent == &iomem_resource) {
		rcu_read_lock();
		nfit_res = get_nfit_res(start);
		rcu_read_unlock();
		if (nfit_res) {
			struct resource *res = nfit_res->res + 1;

			if (start != res->start || resource_size(res) != n)
				pr_info("%s: start: %llx n: %llx mismatch: %pr\n",
						__func__, start, n, res);
			else
				memset(res, 0, sizeof(*res));
			return;
		}
	}
	__release_region(parent, start, n);
}
EXPORT_SYMBOL(__wrap___release_region);

MODULE_LICENSE("GPL v2");
