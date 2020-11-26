// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 */
#include <linux/memremap.h>
#include <linux/rculist.h>
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/pfn_t.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/mm.h>
#include "nfit_test.h"

static LIST_HEAD(iomap_head);

static struct iomap_ops {
	nfit_test_lookup_fn nfit_test_lookup;
	nfit_test_evaluate_dsm_fn evaluate_dsm;
	struct list_head list;
} iomap_ops = {
	.list = LIST_HEAD_INIT(iomap_ops.list),
};

void nfit_test_setup(nfit_test_lookup_fn lookup,
		nfit_test_evaluate_dsm_fn evaluate)
{
	iomap_ops.nfit_test_lookup = lookup;
	iomap_ops.evaluate_dsm = evaluate;
	list_add_rcu(&iomap_ops.list, &iomap_head);
}
EXPORT_SYMBOL(nfit_test_setup);

void nfit_test_teardown(void)
{
	list_del_rcu(&iomap_ops.list);
	synchronize_rcu();
}
EXPORT_SYMBOL(nfit_test_teardown);

static struct nfit_test_resource *__get_nfit_res(resource_size_t resource)
{
	struct iomap_ops *ops;

	ops = list_first_or_null_rcu(&iomap_head, typeof(*ops), list);
	if (ops)
		return ops->nfit_test_lookup(resource);
	return NULL;
}

struct nfit_test_resource *get_nfit_res(resource_size_t resource)
{
	struct nfit_test_resource *res;

	rcu_read_lock();
	res = __get_nfit_res(resource);
	rcu_read_unlock();

	return res;
}
EXPORT_SYMBOL(get_nfit_res);

void __iomem *__nfit_test_ioremap(resource_size_t offset, unsigned long size,
		void __iomem *(*fallback_fn)(resource_size_t, unsigned long))
{
	struct nfit_test_resource *nfit_res = get_nfit_res(offset);

	if (nfit_res)
		return (void __iomem *) nfit_res->buf + offset
			- nfit_res->res.start;
	return fallback_fn(offset, size);
}

void __iomem *__wrap_devm_ioremap(struct device *dev,
		resource_size_t offset, unsigned long size)
{
	struct nfit_test_resource *nfit_res = get_nfit_res(offset);

	if (nfit_res)
		return (void __iomem *) nfit_res->buf + offset
			- nfit_res->res.start;
	return devm_ioremap(dev, offset, size);
}
EXPORT_SYMBOL(__wrap_devm_ioremap);

void *__wrap_devm_memremap(struct device *dev, resource_size_t offset,
		size_t size, unsigned long flags)
{
	struct nfit_test_resource *nfit_res = get_nfit_res(offset);

	if (nfit_res)
		return nfit_res->buf + offset - nfit_res->res.start;
	return devm_memremap(dev, offset, size, flags);
}
EXPORT_SYMBOL(__wrap_devm_memremap);

static void nfit_test_kill(void *_pgmap)
{
	struct dev_pagemap *pgmap = _pgmap;

	WARN_ON(!pgmap || !pgmap->ref);

	if (pgmap->ops && pgmap->ops->kill)
		pgmap->ops->kill(pgmap);
	else
		percpu_ref_kill(pgmap->ref);

	if (pgmap->ops && pgmap->ops->cleanup) {
		pgmap->ops->cleanup(pgmap);
	} else {
		wait_for_completion(&pgmap->done);
		percpu_ref_exit(pgmap->ref);
	}
}

static void dev_pagemap_percpu_release(struct percpu_ref *ref)
{
	struct dev_pagemap *pgmap =
		container_of(ref, struct dev_pagemap, internal_ref);

	complete(&pgmap->done);
}

void *__wrap_devm_memremap_pages(struct device *dev, struct dev_pagemap *pgmap)
{
	int error;
	resource_size_t offset = pgmap->range.start;
	struct nfit_test_resource *nfit_res = get_nfit_res(offset);

	if (!nfit_res)
		return devm_memremap_pages(dev, pgmap);

	if (!pgmap->ref) {
		if (pgmap->ops && (pgmap->ops->kill || pgmap->ops->cleanup))
			return ERR_PTR(-EINVAL);

		init_completion(&pgmap->done);
		error = percpu_ref_init(&pgmap->internal_ref,
				dev_pagemap_percpu_release, 0, GFP_KERNEL);
		if (error)
			return ERR_PTR(error);
		pgmap->ref = &pgmap->internal_ref;
	} else {
		if (!pgmap->ops || !pgmap->ops->kill || !pgmap->ops->cleanup) {
			WARN(1, "Missing reference count teardown definition\n");
			return ERR_PTR(-EINVAL);
		}
	}

	error = devm_add_action_or_reset(dev, nfit_test_kill, pgmap);
	if (error)
		return ERR_PTR(error);
	return nfit_res->buf + offset - nfit_res->res.start;
}
EXPORT_SYMBOL_GPL(__wrap_devm_memremap_pages);

pfn_t __wrap_phys_to_pfn_t(phys_addr_t addr, unsigned long flags)
{
	struct nfit_test_resource *nfit_res = get_nfit_res(addr);

	if (nfit_res)
		flags &= ~PFN_MAP;
        return phys_to_pfn_t(addr, flags);
}
EXPORT_SYMBOL(__wrap_phys_to_pfn_t);

void *__wrap_memremap(resource_size_t offset, size_t size,
		unsigned long flags)
{
	struct nfit_test_resource *nfit_res = get_nfit_res(offset);

	if (nfit_res)
		return nfit_res->buf + offset - nfit_res->res.start;
	return memremap(offset, size, flags);
}
EXPORT_SYMBOL(__wrap_memremap);

void __wrap_devm_memunmap(struct device *dev, void *addr)
{
	struct nfit_test_resource *nfit_res = get_nfit_res((long) addr);

	if (nfit_res)
		return;
	return devm_memunmap(dev, addr);
}
EXPORT_SYMBOL(__wrap_devm_memunmap);

void __iomem *__wrap_ioremap(resource_size_t offset, unsigned long size)
{
	return __nfit_test_ioremap(offset, size, ioremap);
}
EXPORT_SYMBOL(__wrap_ioremap);

void __iomem *__wrap_ioremap_wc(resource_size_t offset, unsigned long size)
{
	return __nfit_test_ioremap(offset, size, ioremap_wc);
}
EXPORT_SYMBOL(__wrap_ioremap_wc);

void __wrap_iounmap(volatile void __iomem *addr)
{
	struct nfit_test_resource *nfit_res = get_nfit_res((long) addr);
	if (nfit_res)
		return;
	return iounmap(addr);
}
EXPORT_SYMBOL(__wrap_iounmap);

void __wrap_memunmap(void *addr)
{
	struct nfit_test_resource *nfit_res = get_nfit_res((long) addr);

	if (nfit_res)
		return;
	return memunmap(addr);
}
EXPORT_SYMBOL(__wrap_memunmap);

static bool nfit_test_release_region(struct device *dev,
		struct resource *parent, resource_size_t start,
		resource_size_t n);

static void nfit_devres_release(struct device *dev, void *data)
{
	struct resource *res = *((struct resource **) data);

	WARN_ON(!nfit_test_release_region(NULL, &iomem_resource, res->start,
			resource_size(res)));
}

static int match(struct device *dev, void *__res, void *match_data)
{
	struct resource *res = *((struct resource **) __res);
	resource_size_t start = *((resource_size_t *) match_data);

	return res->start == start;
}

static bool nfit_test_release_region(struct device *dev,
		struct resource *parent, resource_size_t start,
		resource_size_t n)
{
	if (parent == &iomem_resource) {
		struct nfit_test_resource *nfit_res = get_nfit_res(start);

		if (nfit_res) {
			struct nfit_test_request *req;
			struct resource *res = NULL;

			if (dev) {
				devres_release(dev, nfit_devres_release, match,
						&start);
				return true;
			}

			spin_lock(&nfit_res->lock);
			list_for_each_entry(req, &nfit_res->requests, list)
				if (req->res.start == start) {
					res = &req->res;
					list_del(&req->list);
					break;
				}
			spin_unlock(&nfit_res->lock);

			WARN(!res || resource_size(res) != n,
					"%s: start: %llx n: %llx mismatch: %pr\n",
						__func__, start, n, res);
			if (res)
				kfree(req);
			return true;
		}
	}
	return false;
}

static struct resource *nfit_test_request_region(struct device *dev,
		struct resource *parent, resource_size_t start,
		resource_size_t n, const char *name, int flags)
{
	struct nfit_test_resource *nfit_res;

	if (parent == &iomem_resource) {
		nfit_res = get_nfit_res(start);
		if (nfit_res) {
			struct nfit_test_request *req;
			struct resource *res = NULL;

			if (start + n > nfit_res->res.start
					+ resource_size(&nfit_res->res)) {
				pr_debug("%s: start: %llx n: %llx overflow: %pr\n",
						__func__, start, n,
						&nfit_res->res);
				return NULL;
			}

			spin_lock(&nfit_res->lock);
			list_for_each_entry(req, &nfit_res->requests, list)
				if (start == req->res.start) {
					res = &req->res;
					break;
				}
			spin_unlock(&nfit_res->lock);

			if (res) {
				WARN(1, "%pr already busy\n", res);
				return NULL;
			}

			req = kzalloc(sizeof(*req), GFP_KERNEL);
			if (!req)
				return NULL;
			INIT_LIST_HEAD(&req->list);
			res = &req->res;

			res->start = start;
			res->end = start + n - 1;
			res->name = name;
			res->flags = resource_type(parent);
			res->flags |= IORESOURCE_BUSY | flags;
			spin_lock(&nfit_res->lock);
			list_add(&req->list, &nfit_res->requests);
			spin_unlock(&nfit_res->lock);

			if (dev) {
				struct resource **d;

				d = devres_alloc(nfit_devres_release,
						sizeof(struct resource *),
						GFP_KERNEL);
				if (!d)
					return NULL;
				*d = res;
				devres_add(dev, d);
			}

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

int __wrap_insert_resource(struct resource *parent, struct resource *res)
{
	if (get_nfit_res(res->start))
		return 0;
	return insert_resource(parent, res);
}
EXPORT_SYMBOL(__wrap_insert_resource);

int __wrap_remove_resource(struct resource *res)
{
	if (get_nfit_res(res->start))
		return 0;
	return remove_resource(res);
}
EXPORT_SYMBOL(__wrap_remove_resource);

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
	if (!nfit_test_release_region(NULL, parent, start, n))
		__release_region(parent, start, n);
}
EXPORT_SYMBOL(__wrap___release_region);

void __wrap___devm_release_region(struct device *dev, struct resource *parent,
		resource_size_t start, resource_size_t n)
{
	if (!nfit_test_release_region(dev, parent, start, n))
		__devm_release_region(dev, parent, start, n);
}
EXPORT_SYMBOL(__wrap___devm_release_region);

acpi_status __wrap_acpi_evaluate_object(acpi_handle handle, acpi_string path,
		struct acpi_object_list *p, struct acpi_buffer *buf)
{
	struct nfit_test_resource *nfit_res = get_nfit_res((long) handle);
	union acpi_object **obj;

	if (!nfit_res || strcmp(path, "_FIT") || !buf)
		return acpi_evaluate_object(handle, path, p, buf);

	obj = nfit_res->buf;
	buf->length = sizeof(union acpi_object);
	buf->pointer = *obj;
	return AE_OK;
}
EXPORT_SYMBOL(__wrap_acpi_evaluate_object);

union acpi_object * __wrap_acpi_evaluate_dsm(acpi_handle handle, const guid_t *guid,
		u64 rev, u64 func, union acpi_object *argv4)
{
	union acpi_object *obj = ERR_PTR(-ENXIO);
	struct iomap_ops *ops;

	rcu_read_lock();
	ops = list_first_or_null_rcu(&iomap_head, typeof(*ops), list);
	if (ops)
		obj = ops->evaluate_dsm(handle, guid, rev, func, argv4);
	rcu_read_unlock();

	if (IS_ERR(obj))
		return acpi_evaluate_dsm(handle, guid, rev, func, argv4);
	return obj;
}
EXPORT_SYMBOL(__wrap_acpi_evaluate_dsm);

MODULE_LICENSE("GPL v2");
