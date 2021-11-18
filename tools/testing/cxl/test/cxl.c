// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2021 Intel Corporation. All rights reserved.

#include <linux/platform_device.h>
#include <linux/genalloc.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include "mock.h"

#define NR_CXL_HOST_BRIDGES 4
#define NR_CXL_ROOT_PORTS 2

static struct platform_device *cxl_acpi;
static struct platform_device *cxl_host_bridge[NR_CXL_HOST_BRIDGES];
static struct platform_device
	*cxl_root_port[NR_CXL_HOST_BRIDGES * NR_CXL_ROOT_PORTS];
struct platform_device *cxl_mem[NR_CXL_HOST_BRIDGES * NR_CXL_ROOT_PORTS];

static struct acpi_device acpi0017_mock;
static struct acpi_device host_bridge[NR_CXL_HOST_BRIDGES] = {
	[0] = {
		.handle = &host_bridge[0],
	},
	[1] = {
		.handle = &host_bridge[1],
	},
	[2] = {
		.handle = &host_bridge[2],
	},
	[3] = {
		.handle = &host_bridge[3],
	},
};

static bool is_mock_dev(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cxl_mem); i++)
		if (dev == &cxl_mem[i]->dev)
			return true;
	if (dev == &cxl_acpi->dev)
		return true;
	return false;
}

static bool is_mock_adev(struct acpi_device *adev)
{
	int i;

	if (adev == &acpi0017_mock)
		return true;

	for (i = 0; i < ARRAY_SIZE(host_bridge); i++)
		if (adev == &host_bridge[i])
			return true;

	return false;
}

static struct {
	struct acpi_table_cedt cedt;
	struct acpi_cedt_chbs chbs[NR_CXL_HOST_BRIDGES];
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[1];
	} cfmws0;
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[4];
	} cfmws1;
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[1];
	} cfmws2;
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[4];
	} cfmws3;
} __packed mock_cedt = {
	.cedt = {
		.header = {
			.signature = "CEDT",
			.length = sizeof(mock_cedt),
			.revision = 1,
		},
	},
	.chbs[0] = {
		.header = {
			.type = ACPI_CEDT_TYPE_CHBS,
			.length = sizeof(mock_cedt.chbs[0]),
		},
		.uid = 0,
		.cxl_version = ACPI_CEDT_CHBS_VERSION_CXL20,
	},
	.chbs[1] = {
		.header = {
			.type = ACPI_CEDT_TYPE_CHBS,
			.length = sizeof(mock_cedt.chbs[0]),
		},
		.uid = 1,
		.cxl_version = ACPI_CEDT_CHBS_VERSION_CXL20,
	},
	.chbs[2] = {
		.header = {
			.type = ACPI_CEDT_TYPE_CHBS,
			.length = sizeof(mock_cedt.chbs[0]),
		},
		.uid = 2,
		.cxl_version = ACPI_CEDT_CHBS_VERSION_CXL20,
	},
	.chbs[3] = {
		.header = {
			.type = ACPI_CEDT_TYPE_CHBS,
			.length = sizeof(mock_cedt.chbs[0]),
		},
		.uid = 3,
		.cxl_version = ACPI_CEDT_CHBS_VERSION_CXL20,
	},
	.cfmws0 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws0),
			},
			.interleave_ways = 0,
			.granularity = 4,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_TYPE3 |
					ACPI_CEDT_CFMWS_RESTRICT_VOLATILE,
			.qtg_id = 0,
			.window_size = SZ_256M,
		},
		.target = { 0 },
	},
	.cfmws1 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws1),
			},
			.interleave_ways = 2,
			.granularity = 4,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_TYPE3 |
					ACPI_CEDT_CFMWS_RESTRICT_VOLATILE,
			.qtg_id = 1,
			.window_size = SZ_256M * 4,
		},
		.target = { 0, 1, 2, 3 },
	},
	.cfmws2 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws2),
			},
			.interleave_ways = 0,
			.granularity = 4,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_TYPE3 |
					ACPI_CEDT_CFMWS_RESTRICT_PMEM,
			.qtg_id = 2,
			.window_size = SZ_256M,
		},
		.target = { 0 },
	},
	.cfmws3 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws3),
			},
			.interleave_ways = 2,
			.granularity = 4,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_TYPE3 |
					ACPI_CEDT_CFMWS_RESTRICT_PMEM,
			.qtg_id = 3,
			.window_size = SZ_256M * 4,
		},
		.target = { 0, 1, 2, 3 },
	},
};

struct cxl_mock_res {
	struct list_head list;
	struct range range;
};

static LIST_HEAD(mock_res);
static DEFINE_MUTEX(mock_res_lock);
static struct gen_pool *cxl_mock_pool;

static void depopulate_all_mock_resources(void)
{
	struct cxl_mock_res *res, *_res;

	mutex_lock(&mock_res_lock);
	list_for_each_entry_safe(res, _res, &mock_res, list) {
		gen_pool_free(cxl_mock_pool, res->range.start,
			      range_len(&res->range));
		list_del(&res->list);
		kfree(res);
	}
	mutex_unlock(&mock_res_lock);
}

static struct cxl_mock_res *alloc_mock_res(resource_size_t size)
{
	struct cxl_mock_res *res = kzalloc(sizeof(*res), GFP_KERNEL);
	struct genpool_data_align data = {
		.align = SZ_256M,
	};
	unsigned long phys;

	INIT_LIST_HEAD(&res->list);
	phys = gen_pool_alloc_algo(cxl_mock_pool, size,
				   gen_pool_first_fit_align, &data);
	if (!phys)
		return NULL;

	res->range = (struct range) {
		.start = phys,
		.end = phys + size - 1,
	};
	mutex_lock(&mock_res_lock);
	list_add(&res->list, &mock_res);
	mutex_unlock(&mock_res_lock);

	return res;
}

static int populate_cedt(void)
{
	struct acpi_cedt_cfmws *cfmws[4] = {
		[0] = &mock_cedt.cfmws0.cfmws,
		[1] = &mock_cedt.cfmws1.cfmws,
		[2] = &mock_cedt.cfmws2.cfmws,
		[3] = &mock_cedt.cfmws3.cfmws,
	};
	struct cxl_mock_res *res;
	int i;

	for (i = 0; i < ARRAY_SIZE(mock_cedt.chbs); i++) {
		struct acpi_cedt_chbs *chbs = &mock_cedt.chbs[i];
		resource_size_t size;

		if (chbs->cxl_version == ACPI_CEDT_CHBS_VERSION_CXL20)
			size = ACPI_CEDT_CHBS_LENGTH_CXL20;
		else
			size = ACPI_CEDT_CHBS_LENGTH_CXL11;

		res = alloc_mock_res(size);
		if (!res)
			return -ENOMEM;
		chbs->base = res->range.start;
		chbs->length = size;
	}

	for (i = 0; i < ARRAY_SIZE(cfmws); i++) {
		struct acpi_cedt_cfmws *window = cfmws[i];

		res = alloc_mock_res(window->window_size);
		if (!res)
			return -ENOMEM;
		window->base_hpa = res->range.start;
	}

	return 0;
}

static acpi_status mock_acpi_get_table(char *signature, u32 instance,
				       struct acpi_table_header **out_table)
{
	if (instance < U32_MAX || strcmp(signature, ACPI_SIG_CEDT) != 0)
		return acpi_get_table(signature, instance, out_table);

	*out_table = (struct acpi_table_header *) &mock_cedt;
	return AE_OK;
}

static void mock_acpi_put_table(struct acpi_table_header *table)
{
	if (table == (struct acpi_table_header *) &mock_cedt)
		return;
	acpi_put_table(table);
}

static bool is_mock_bridge(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cxl_host_bridge); i++)
		if (dev == &cxl_host_bridge[i]->dev)
			return true;

	return false;
}

static int host_bridge_index(struct acpi_device *adev)
{
	return adev - host_bridge;
}

static struct acpi_device *find_host_bridge(acpi_handle handle)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(host_bridge); i++)
		if (handle == host_bridge[i].handle)
			return &host_bridge[i];
	return NULL;
}

static acpi_status
mock_acpi_evaluate_integer(acpi_handle handle, acpi_string pathname,
			   struct acpi_object_list *arguments,
			   unsigned long long *data)
{
	struct acpi_device *adev = find_host_bridge(handle);

	if (!adev || strcmp(pathname, METHOD_NAME__UID) != 0)
		return acpi_evaluate_integer(handle, pathname, arguments, data);

	*data = host_bridge_index(adev);
	return AE_OK;
}

static struct pci_bus mock_pci_bus[NR_CXL_HOST_BRIDGES];
static struct acpi_pci_root mock_pci_root[NR_CXL_HOST_BRIDGES] = {
	[0] = {
		.bus = &mock_pci_bus[0],
	},
	[1] = {
		.bus = &mock_pci_bus[1],
	},
	[2] = {
		.bus = &mock_pci_bus[2],
	},
	[3] = {
		.bus = &mock_pci_bus[3],
	},
};

static struct platform_device *mock_cxl_root_port(struct pci_bus *bus, int index)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mock_pci_bus); i++)
		if (bus == &mock_pci_bus[i])
			return cxl_root_port[index + i * NR_CXL_ROOT_PORTS];
	return NULL;
}

static bool is_mock_port(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cxl_root_port); i++)
		if (pdev == cxl_root_port[i])
			return true;
	return false;
}

static bool is_mock_bus(struct pci_bus *bus)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mock_pci_bus); i++)
		if (bus == &mock_pci_bus[i])
			return true;
	return false;
}

static struct acpi_pci_root *mock_acpi_pci_find_root(acpi_handle handle)
{
	struct acpi_device *adev = find_host_bridge(handle);

	if (!adev)
		return acpi_pci_find_root(handle);
	return &mock_pci_root[host_bridge_index(adev)];
}

static struct cxl_mock_ops cxl_mock_ops = {
	.is_mock_adev = is_mock_adev,
	.is_mock_bridge = is_mock_bridge,
	.is_mock_bus = is_mock_bus,
	.is_mock_port = is_mock_port,
	.is_mock_dev = is_mock_dev,
	.mock_port = mock_cxl_root_port,
	.acpi_get_table = mock_acpi_get_table,
	.acpi_put_table = mock_acpi_put_table,
	.acpi_evaluate_integer = mock_acpi_evaluate_integer,
	.acpi_pci_find_root = mock_acpi_pci_find_root,
	.list = LIST_HEAD_INIT(cxl_mock_ops.list),
};

static void mock_companion(struct acpi_device *adev, struct device *dev)
{
	device_initialize(&adev->dev);
	fwnode_init(&adev->fwnode, NULL);
	dev->fwnode = &adev->fwnode;
	adev->fwnode.dev = dev;
}

#ifndef SZ_64G
#define SZ_64G (SZ_32G * 2)
#endif

#ifndef SZ_512G
#define SZ_512G (SZ_64G * 8)
#endif

static struct platform_device *alloc_memdev(int id)
{
	struct resource res[] = {
		[0] = {
			.flags = IORESOURCE_MEM,
		},
		[1] = {
			.flags = IORESOURCE_MEM,
			.desc = IORES_DESC_PERSISTENT_MEMORY,
		},
	};
	struct platform_device *pdev;
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(res); i++) {
		struct cxl_mock_res *r = alloc_mock_res(SZ_256M);

		if (!r)
			return NULL;
		res[i].start = r->range.start;
		res[i].end = r->range.end;
	}

	pdev = platform_device_alloc("cxl_mem", id);
	if (!pdev)
		return NULL;

	rc = platform_device_add_resources(pdev, res, ARRAY_SIZE(res));
	if (rc)
		goto err;

	return pdev;

err:
	platform_device_put(pdev);
	return NULL;
}

static __init int cxl_test_init(void)
{
	int rc, i;

	register_cxl_mock_ops(&cxl_mock_ops);

	cxl_mock_pool = gen_pool_create(ilog2(SZ_2M), NUMA_NO_NODE);
	if (!cxl_mock_pool) {
		rc = -ENOMEM;
		goto err_gen_pool_create;
	}

	rc = gen_pool_add(cxl_mock_pool, SZ_512G, SZ_64G, NUMA_NO_NODE);
	if (rc)
		goto err_gen_pool_add;

	rc = populate_cedt();
	if (rc)
		goto err_populate;

	for (i = 0; i < ARRAY_SIZE(cxl_host_bridge); i++) {
		struct acpi_device *adev = &host_bridge[i];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_host_bridge", i);
		if (!pdev)
			goto err_bridge;

		mock_companion(adev, &pdev->dev);
		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_bridge;
		}
		cxl_host_bridge[i] = pdev;
	}

	for (i = 0; i < ARRAY_SIZE(cxl_root_port); i++) {
		struct platform_device *bridge =
			cxl_host_bridge[i / NR_CXL_ROOT_PORTS];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_root_port", i);
		if (!pdev)
			goto err_port;
		pdev->dev.parent = &bridge->dev;

		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_port;
		}
		cxl_root_port[i] = pdev;
	}

	BUILD_BUG_ON(ARRAY_SIZE(cxl_mem) != ARRAY_SIZE(cxl_root_port));
	for (i = 0; i < ARRAY_SIZE(cxl_mem); i++) {
		struct platform_device *port = cxl_root_port[i];
		struct platform_device *pdev;

		pdev = alloc_memdev(i);
		if (!pdev)
			goto err_mem;
		pdev->dev.parent = &port->dev;

		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_mem;
		}
		cxl_mem[i] = pdev;
	}

	cxl_acpi = platform_device_alloc("cxl_acpi", 0);
	if (!cxl_acpi)
		goto err_mem;

	mock_companion(&acpi0017_mock, &cxl_acpi->dev);
	acpi0017_mock.dev.bus = &platform_bus_type;

	rc = platform_device_add(cxl_acpi);
	if (rc)
		goto err_add;

	return 0;

err_add:
	platform_device_put(cxl_acpi);
err_mem:
	for (i = ARRAY_SIZE(cxl_mem) - 1; i >= 0; i--)
		platform_device_unregister(cxl_mem[i]);
err_port:
	for (i = ARRAY_SIZE(cxl_root_port) - 1; i >= 0; i--)
		platform_device_unregister(cxl_root_port[i]);
err_bridge:
	for (i = ARRAY_SIZE(cxl_host_bridge) - 1; i >= 0; i--)
		platform_device_unregister(cxl_host_bridge[i]);
err_populate:
	depopulate_all_mock_resources();
err_gen_pool_add:
	gen_pool_destroy(cxl_mock_pool);
err_gen_pool_create:
	unregister_cxl_mock_ops(&cxl_mock_ops);
	return rc;
}

static __exit void cxl_test_exit(void)
{
	int i;

	platform_device_unregister(cxl_acpi);
	for (i = ARRAY_SIZE(cxl_mem) - 1; i >= 0; i--)
		platform_device_unregister(cxl_mem[i]);
	for (i = ARRAY_SIZE(cxl_root_port) - 1; i >= 0; i--)
		platform_device_unregister(cxl_root_port[i]);
	for (i = ARRAY_SIZE(cxl_host_bridge) - 1; i >= 0; i--)
		platform_device_unregister(cxl_host_bridge[i]);
	depopulate_all_mock_resources();
	gen_pool_destroy(cxl_mock_pool);
	unregister_cxl_mock_ops(&cxl_mock_ops);
}

module_init(cxl_test_init);
module_exit(cxl_test_exit);
MODULE_LICENSE("GPL v2");
