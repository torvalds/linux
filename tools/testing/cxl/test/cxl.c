// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2021 Intel Corporation. All rights reserved.

#include <linux/platform_device.h>
#include <linux/memory_hotplug.h>
#include <linux/genalloc.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <cxlmem.h>

#include "../watermark.h"
#include "mock.h"

static int interleave_arithmetic;
static bool extended_linear_cache;
static bool fail_autoassemble;

#define FAKE_QTG_ID	42

#define NR_CXL_HOST_BRIDGES 2
#define NR_CXL_SINGLE_HOST 1
#define NR_CXL_RCH 1
#define NR_CXL_ROOT_PORTS 2
#define NR_CXL_SWITCH_PORTS 2
#define NR_CXL_PORT_DECODERS 8
#define NR_BRIDGES (NR_CXL_HOST_BRIDGES + NR_CXL_SINGLE_HOST + NR_CXL_RCH)

#define MOCK_AUTO_REGION_SIZE_DEFAULT SZ_512M
static int mock_auto_region_size = MOCK_AUTO_REGION_SIZE_DEFAULT;

static struct platform_device *cxl_acpi;
static struct platform_device *cxl_host_bridge[NR_CXL_HOST_BRIDGES];
#define NR_MULTI_ROOT (NR_CXL_HOST_BRIDGES * NR_CXL_ROOT_PORTS)
static struct platform_device *cxl_root_port[NR_MULTI_ROOT];
static struct platform_device *cxl_switch_uport[NR_MULTI_ROOT];
#define NR_MEM_MULTI \
	(NR_CXL_HOST_BRIDGES * NR_CXL_ROOT_PORTS * NR_CXL_SWITCH_PORTS)
static struct platform_device *cxl_switch_dport[NR_MEM_MULTI];

static struct platform_device *cxl_hb_single[NR_CXL_SINGLE_HOST];
static struct platform_device *cxl_root_single[NR_CXL_SINGLE_HOST];
static struct platform_device *cxl_swu_single[NR_CXL_SINGLE_HOST];
#define NR_MEM_SINGLE (NR_CXL_SINGLE_HOST * NR_CXL_SWITCH_PORTS)
static struct platform_device *cxl_swd_single[NR_MEM_SINGLE];

struct platform_device *cxl_mem[NR_MEM_MULTI];
struct platform_device *cxl_mem_single[NR_MEM_SINGLE];

static struct platform_device *cxl_rch[NR_CXL_RCH];
static struct platform_device *cxl_rcd[NR_CXL_RCH];

/*
 * Decoder registry
 *
 * Record decoder programming so that the topology can be reconstructed
 * after cxl_acpi unbind/bind. This allows a user-created region config
 * to be replayed as if firmware had provided the region at enumeration
 * time.
 *
 * Entries are keyed by a stable port identity (port->uport_dev) combined
 * with the decoder id. Decoder state is saved at initialization and
 * updated on commit and reset.
 *
 * On re-enumeration mock_init_hdm_decoder() consults this registry to
 * restore enabled decoders. Disabled decoders are reinitialized to a
 * clean default state rather than replaying stale programming.
 */
static DEFINE_XARRAY(decoder_registry);

/*
 * When set, decoder reset will not update the registry. This allows
 * region destroy operations to reset live decoders without erasing
 * the saved programming needed for replay after re-enumeration.
 */
static bool decoder_reset_preserve_registry;

static inline bool is_multi_bridge(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cxl_host_bridge); i++)
		if (&cxl_host_bridge[i]->dev == dev)
			return true;
	return false;
}

static inline bool is_single_bridge(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cxl_hb_single); i++)
		if (&cxl_hb_single[i]->dev == dev)
			return true;
	return false;
}

static struct acpi_device acpi0017_mock;
static struct acpi_device host_bridge[NR_BRIDGES] = {
	[0] = {
		.handle = &host_bridge[0],
		.pnp.unique_id = "0",
	},
	[1] = {
		.handle = &host_bridge[1],
		.pnp.unique_id = "1",
	},
	[2] = {
		.handle = &host_bridge[2],
		.pnp.unique_id = "2",
	},
	[3] = {
		.handle = &host_bridge[3],
		.pnp.unique_id = "3",
	},
};

static bool is_mock_dev(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cxl_mem); i++)
		if (dev == &cxl_mem[i]->dev)
			return true;
	for (i = 0; i < ARRAY_SIZE(cxl_mem_single); i++)
		if (dev == &cxl_mem_single[i]->dev)
			return true;
	for (i = 0; i < ARRAY_SIZE(cxl_rcd); i++)
		if (dev == &cxl_rcd[i]->dev)
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
	struct acpi_cedt_chbs chbs[NR_BRIDGES];
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[1];
	} cfmws0;
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[2];
	} cfmws1;
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[1];
	} cfmws2;
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[2];
	} cfmws3;
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[1];
	} cfmws4;
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[1];
	} cfmws5;
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[1];
	} cfmws6;
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[2];
	} cfmws7;
	struct {
		struct acpi_cedt_cfmws cfmws;
		u32 target[3];
	} cfmws8;
	struct {
		struct acpi_cedt_cxims cxims;
		u64 xormap_list[2];
	} cxims0;
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
		.cxl_version = ACPI_CEDT_CHBS_VERSION_CXL11,
	},
	.cfmws0 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws0),
			},
			.interleave_ways = 0,
			.granularity = 4,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_HOSTONLYMEM |
					ACPI_CEDT_CFMWS_RESTRICT_VOLATILE,
			.qtg_id = FAKE_QTG_ID,
			.window_size = SZ_256M * 4UL,
		},
		.target = { 0 },
	},
	.cfmws1 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws1),
			},
			.interleave_ways = 1,
			.granularity = 4,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_HOSTONLYMEM |
					ACPI_CEDT_CFMWS_RESTRICT_VOLATILE,
			.qtg_id = FAKE_QTG_ID,
			.window_size = SZ_256M * 8UL,
		},
		.target = { 0, 1, },
	},
	.cfmws2 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws2),
			},
			.interleave_ways = 0,
			.granularity = 4,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_HOSTONLYMEM |
					ACPI_CEDT_CFMWS_RESTRICT_PMEM,
			.qtg_id = FAKE_QTG_ID,
			.window_size = SZ_256M * 4UL,
		},
		.target = { 0 },
	},
	.cfmws3 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws3),
			},
			.interleave_ways = 1,
			.granularity = 4,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_HOSTONLYMEM |
					ACPI_CEDT_CFMWS_RESTRICT_PMEM,
			.qtg_id = FAKE_QTG_ID,
			.window_size = SZ_256M * 8UL,
		},
		.target = { 0, 1, },
	},
	.cfmws4 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws4),
			},
			.interleave_ways = 0,
			.granularity = 4,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_HOSTONLYMEM |
					ACPI_CEDT_CFMWS_RESTRICT_PMEM,
			.qtg_id = FAKE_QTG_ID,
			.window_size = SZ_256M * 4UL,
		},
		.target = { 2 },
	},
	.cfmws5 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws5),
			},
			.interleave_ways = 0,
			.granularity = 4,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_HOSTONLYMEM |
					ACPI_CEDT_CFMWS_RESTRICT_VOLATILE,
			.qtg_id = FAKE_QTG_ID,
			.window_size = SZ_256M,
		},
		.target = { 3 },
	},
	/* .cfmws6,7,8 use ACPI_CEDT_CFMWS_ARITHMETIC_XOR */
	.cfmws6 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws6),
			},
			.interleave_arithmetic = ACPI_CEDT_CFMWS_ARITHMETIC_XOR,
			.interleave_ways = 0,
			.granularity = 4,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_HOSTONLYMEM |
					ACPI_CEDT_CFMWS_RESTRICT_PMEM,
			.qtg_id = FAKE_QTG_ID,
			.window_size = SZ_256M * 8UL,
		},
		.target = { 0, },
	},
	.cfmws7 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws7),
			},
			.interleave_arithmetic = ACPI_CEDT_CFMWS_ARITHMETIC_XOR,
			.interleave_ways = 1,
			.granularity = 0,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_HOSTONLYMEM |
					ACPI_CEDT_CFMWS_RESTRICT_PMEM,
			.qtg_id = FAKE_QTG_ID,
			.window_size = SZ_256M * 8UL,
		},
		.target = { 0, 1, },
	},
	.cfmws8 = {
		.cfmws = {
			.header = {
				.type = ACPI_CEDT_TYPE_CFMWS,
				.length = sizeof(mock_cedt.cfmws8),
			},
			.interleave_arithmetic = ACPI_CEDT_CFMWS_ARITHMETIC_XOR,
			.interleave_ways = 8,
			.granularity = 1,
			.restrictions = ACPI_CEDT_CFMWS_RESTRICT_HOSTONLYMEM |
					ACPI_CEDT_CFMWS_RESTRICT_PMEM,
			.qtg_id = FAKE_QTG_ID,
			.window_size = SZ_512M * 6UL,
		},
		.target = { 0, 1, 2, },
	},
	.cxims0 = {
		.cxims = {
			.header = {
				.type = ACPI_CEDT_TYPE_CXIMS,
				.length = sizeof(mock_cedt.cxims0),
			},
			.hbig = 0,
			.nr_xormaps = 2,
		},
		.xormap_list = { 0x404100, 0x808200, },
	},
};

struct acpi_cedt_cfmws *mock_cfmws[] = {
	[0] = &mock_cedt.cfmws0.cfmws,
	[1] = &mock_cedt.cfmws1.cfmws,
	[2] = &mock_cedt.cfmws2.cfmws,
	[3] = &mock_cedt.cfmws3.cfmws,
	[4] = &mock_cedt.cfmws4.cfmws,
	[5] = &mock_cedt.cfmws5.cfmws,
	/* Modulo Math above, XOR Math below */
	[6] = &mock_cedt.cfmws6.cfmws,
	[7] = &mock_cedt.cfmws7.cfmws,
	[8] = &mock_cedt.cfmws8.cfmws,
};

static int cfmws_start;
static int cfmws_end;
#define CFMWS_MOD_ARRAY_START 0
#define CFMWS_MOD_ARRAY_END   5
#define CFMWS_XOR_ARRAY_START 6
#define CFMWS_XOR_ARRAY_END   8

struct acpi_cedt_cxims *mock_cxims[1] = {
	[0] = &mock_cedt.cxims0.cxims,
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

static struct cxl_mock_res *alloc_mock_res(resource_size_t size, int align)
{
	struct cxl_mock_res *res = kzalloc(sizeof(*res), GFP_KERNEL);
	struct genpool_data_align data = {
		.align = align,
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

/* Only update CFMWS0 as this is used by the auto region. */
static void cfmws_elc_update(struct acpi_cedt_cfmws *window, int index)
{
	if (!extended_linear_cache)
		return;

	if (index != 0)
		return;

	/*
	 * The window size should be 2x of the CXL region size where half is
	 * DRAM and half is CXL
	 */
	window->window_size = mock_auto_region_size * 2;
}

static int populate_cedt(void)
{
	struct cxl_mock_res *res;
	int i;

	for (i = 0; i < ARRAY_SIZE(mock_cedt.chbs); i++) {
		struct acpi_cedt_chbs *chbs = &mock_cedt.chbs[i];
		resource_size_t size;

		if (chbs->cxl_version == ACPI_CEDT_CHBS_VERSION_CXL20)
			size = ACPI_CEDT_CHBS_LENGTH_CXL20;
		else
			size = ACPI_CEDT_CHBS_LENGTH_CXL11;

		res = alloc_mock_res(size, size);
		if (!res)
			return -ENOMEM;
		chbs->base = res->range.start;
		chbs->length = size;
	}

	for (i = cfmws_start; i <= cfmws_end; i++) {
		struct acpi_cedt_cfmws *window = mock_cfmws[i];

		cfmws_elc_update(window, i);
		res = alloc_mock_res(window->window_size, SZ_256M);
		if (!res)
			return -ENOMEM;
		window->base_hpa = res->range.start;
	}

	return 0;
}

static bool is_mock_port(struct device *dev);

/*
 * WARNING, this hack assumes the format of 'struct cxl_cfmws_context'
 * and 'struct cxl_chbs_context' share the property that the first
 * struct member is a cxl_test device being probed by the cxl_acpi
 * driver.
 */
struct cxl_cedt_context {
	struct device *dev;
};

static int mock_acpi_table_parse_cedt(enum acpi_cedt_type id,
				      acpi_tbl_entry_handler_arg handler_arg,
				      void *arg)
{
	struct cxl_cedt_context *ctx = arg;
	struct device *dev = ctx->dev;
	union acpi_subtable_headers *h;
	unsigned long end;
	int i;

	if (!is_mock_port(dev) && !is_mock_dev(dev))
		return acpi_table_parse_cedt(id, handler_arg, arg);

	if (id == ACPI_CEDT_TYPE_CHBS)
		for (i = 0; i < ARRAY_SIZE(mock_cedt.chbs); i++) {
			h = (union acpi_subtable_headers *)&mock_cedt.chbs[i];
			end = (unsigned long)&mock_cedt.chbs[i + 1];
			handler_arg(h, arg, end);
		}

	if (id == ACPI_CEDT_TYPE_CFMWS)
		for (i = cfmws_start; i <= cfmws_end; i++) {
			h = (union acpi_subtable_headers *) mock_cfmws[i];
			end = (unsigned long) h + mock_cfmws[i]->header.length;
			handler_arg(h, arg, end);
		}

	if (id == ACPI_CEDT_TYPE_CXIMS)
		for (i = 0; i < ARRAY_SIZE(mock_cxims); i++) {
			h = (union acpi_subtable_headers *)mock_cxims[i];
			end = (unsigned long)h + mock_cxims[i]->header.length;
			handler_arg(h, arg, end);
		}

	return 0;
}

static bool is_mock_bridge(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cxl_host_bridge); i++)
		if (dev == &cxl_host_bridge[i]->dev)
			return true;
	for (i = 0; i < ARRAY_SIZE(cxl_hb_single); i++)
		if (dev == &cxl_hb_single[i]->dev)
			return true;
	for (i = 0; i < ARRAY_SIZE(cxl_rch); i++)
		if (dev == &cxl_rch[i]->dev)
			return true;

	return false;
}

static bool is_mock_port(struct device *dev)
{
	int i;

	if (is_mock_bridge(dev))
		return true;

	for (i = 0; i < ARRAY_SIZE(cxl_root_port); i++)
		if (dev == &cxl_root_port[i]->dev)
			return true;

	for (i = 0; i < ARRAY_SIZE(cxl_switch_uport); i++)
		if (dev == &cxl_switch_uport[i]->dev)
			return true;

	for (i = 0; i < ARRAY_SIZE(cxl_switch_dport); i++)
		if (dev == &cxl_switch_dport[i]->dev)
			return true;

	for (i = 0; i < ARRAY_SIZE(cxl_root_single); i++)
		if (dev == &cxl_root_single[i]->dev)
			return true;

	for (i = 0; i < ARRAY_SIZE(cxl_swu_single); i++)
		if (dev == &cxl_swu_single[i]->dev)
			return true;

	for (i = 0; i < ARRAY_SIZE(cxl_swd_single); i++)
		if (dev == &cxl_swd_single[i]->dev)
			return true;

	if (is_cxl_memdev(dev))
		return is_mock_dev(dev->parent);

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

static int
mock_hmat_get_extended_linear_cache_size(struct resource *backing_res,
					 int nid, resource_size_t *cache_size)
{
	struct acpi_cedt_cfmws *window = mock_cfmws[0];
	struct resource cfmws0_res =
		DEFINE_RES_MEM(window->base_hpa, window->window_size);

	if (!extended_linear_cache ||
	    !resource_contains(&cfmws0_res, backing_res)) {
		return hmat_get_extended_linear_cache_size(backing_res,
							   nid, cache_size);
	}

	*cache_size = mock_auto_region_size;

	return 0;
}

static struct pci_bus mock_pci_bus[NR_BRIDGES];
static struct acpi_pci_root mock_pci_root[ARRAY_SIZE(mock_pci_bus)] = {
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

static struct cxl_hdm *mock_cxl_setup_hdm(struct cxl_port *port,
					  struct cxl_endpoint_dvsec_info *info)
{
	struct cxl_hdm *cxlhdm = devm_kzalloc(&port->dev, sizeof(*cxlhdm), GFP_KERNEL);
	struct device *dev = &port->dev;

	if (!cxlhdm)
		return ERR_PTR(-ENOMEM);

	cxlhdm->port = port;
	cxlhdm->interleave_mask = ~0U;
	cxlhdm->iw_cap_mask = ~0UL;
	dev_set_drvdata(dev, cxlhdm);
	return cxlhdm;
}

struct target_map_ctx {
	u32 *target_map;
	int index;
	int target_count;
};

static int map_targets(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct target_map_ctx *ctx = data;

	ctx->target_map[ctx->index++] = pdev->id;

	if (ctx->index > ctx->target_count) {
		dev_WARN_ONCE(dev, 1, "too many targets found?\n");
		return -ENXIO;
	}

	return 0;
}

/*
 * Build a stable registry key from the decoder's upstream port identity
 * and decoder id.
 *
 * Decoder objects and cxl_port objects are reallocated on each enumeration,
 * so their addresses cannot be used directly as replay keys. However,
 * port->uport_dev is stable for a given topology across cxl_acpi unbind/bind
 * in cxl_test, so use that as the port identity and pack the local decoder
 * id into the low bits.
 *
 * The key is formed as:
 *     ((unsigned long)port->uport_dev << 4) | cxld->id
 *
 * The low bits hold the decoder id (which must fit in 4 bits) while
 * the remaining bits identify the upstream port. This key is only used
 * within cxl_test to locate saved decoder state during replay.
 */
static unsigned long cxld_registry_index(struct cxl_decoder *cxld)
{
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);

	dev_WARN_ONCE(&port->dev, cxld->id >= 16,
		      "decoder id:%d out of range\n", cxld->id);
	return (((unsigned long)port->uport_dev) << 4) | cxld->id;
}

struct cxl_test_decoder {
	union {
		struct cxl_switch_decoder cxlsd;
		struct cxl_endpoint_decoder cxled;
	};
	struct range dpa_range;
};

static struct cxl_test_decoder *cxld_registry_find(struct cxl_decoder *cxld)
{
	return xa_load(&decoder_registry, cxld_registry_index(cxld));
}

#define dbg_cxld(port, msg, cxld)                                                       \
	do {                                                                            \
		struct cxl_decoder *___d = (cxld);                                      \
		dev_dbg((port)->uport_dev,                                              \
			"decoder%d: %s range: %#llx-%#llx iw: %d ig: %d flags: %#lx\n", \
			___d->id, msg, ___d->hpa_range.start,                           \
			___d->hpa_range.end + 1, ___d->interleave_ways,                 \
			___d->interleave_granularity, ___d->flags);                     \
	} while (0)

static int mock_decoder_commit(struct cxl_decoder *cxld);
static void mock_decoder_reset(struct cxl_decoder *cxld);
static void init_disabled_mock_decoder(struct cxl_decoder *cxld);

static void cxld_copy(struct cxl_decoder *a, struct cxl_decoder *b)
{
	a->id = b->id;
	a->hpa_range = b->hpa_range;
	a->interleave_ways = b->interleave_ways;
	a->interleave_granularity = b->interleave_granularity;
	a->target_type = b->target_type;
	a->flags = b->flags;
	a->commit = mock_decoder_commit;
	a->reset = mock_decoder_reset;
}

/*
 * Restore decoder programming saved in the registry.
 *
 * Only decoders that were saved enabled are restored. Disabled decoders
 * are left in their default inactive state so that stale programming is
 * not resurrected after topology replay.
 *
 * For endpoint decoders this also restores the DPA reservation needed
 * to reconstruct committed mappings.
 */
static int cxld_registry_restore(struct cxl_decoder *cxld,
				 struct cxl_test_decoder *td)
{
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);
	int rc;

	if (is_switch_decoder(&cxld->dev)) {
		struct cxl_switch_decoder *cxlsd =
			to_cxl_switch_decoder(&cxld->dev);

		if (!(td->cxlsd.cxld.flags & CXL_DECODER_F_ENABLE))
			return 0;

		dbg_cxld(port, "restore", &td->cxlsd.cxld);
		cxld_copy(cxld, &td->cxlsd.cxld);
		WARN_ON(cxlsd->nr_targets != td->cxlsd.nr_targets);

		/* Restore saved target intent; live dport binding happens later */
		for (int i = 0; i < cxlsd->nr_targets; i++) {
			cxlsd->target[i] = NULL;
			cxld->target_map[i] = td->cxlsd.cxld.target_map[i];
		}

		port->commit_end = cxld->id;

	} else {
		struct cxl_endpoint_decoder *cxled =
			to_cxl_endpoint_decoder(&cxld->dev);

		if (!(td->cxled.cxld.flags & CXL_DECODER_F_ENABLE))
			return 0;

		dbg_cxld(port, "restore", &td->cxled.cxld);
		cxld_copy(cxld, &td->cxled.cxld);
		cxled->state = td->cxled.state;
		cxled->skip = td->cxled.skip;
		if (range_len(&td->dpa_range)) {
			rc = devm_cxl_dpa_reserve(cxled, td->dpa_range.start,
						  range_len(&td->dpa_range),
						  td->cxled.skip);
			if (rc) {
				init_disabled_mock_decoder(cxld);
				return rc;
			}
		}
		port->commit_end = cxld->id;
	}

	return 0;
}

static void __cxld_registry_save(struct cxl_test_decoder *td,
				 struct cxl_decoder *cxld)
{
	if (is_switch_decoder(&cxld->dev)) {
		struct cxl_switch_decoder *cxlsd =
			to_cxl_switch_decoder(&cxld->dev);

		cxld_copy(&td->cxlsd.cxld, cxld);
		td->cxlsd.nr_targets = cxlsd->nr_targets;

		/* Save target port_id as a stable identify for the dport */
		for (int i = 0; i < cxlsd->nr_targets; i++) {
			struct cxl_dport *dport;

			if (!cxlsd->target[i])
				continue;

			dport = cxlsd->target[i];
			td->cxlsd.cxld.target_map[i] = dport->port_id;
		}
	} else {
		struct cxl_endpoint_decoder *cxled =
			to_cxl_endpoint_decoder(&cxld->dev);

		cxld_copy(&td->cxled.cxld, cxld);
		td->cxled.state = cxled->state;
		td->cxled.skip = cxled->skip;

		if (!(cxld->flags & CXL_DECODER_F_ENABLE)) {
			td->dpa_range.start = 0;
			td->dpa_range.end = -1;
		} else if (cxled->dpa_res) {
			td->dpa_range.start = cxled->dpa_res->start;
			td->dpa_range.end = cxled->dpa_res->end;
		} else {
			td->dpa_range.start = 0;
			td->dpa_range.end = -1;
		}
	}
}

static void cxld_registry_save(struct cxl_test_decoder *td,
			       struct cxl_decoder *cxld)
{
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);

	dbg_cxld(port, "save", cxld);
	__cxld_registry_save(td, cxld);
}

static void cxld_registry_update(struct cxl_decoder *cxld)
{
	struct cxl_test_decoder *td = cxld_registry_find(cxld);
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);

	if (WARN_ON_ONCE(!td))
		return;

	dbg_cxld(port, "update", cxld);
	__cxld_registry_save(td, cxld);
}

static int mock_decoder_commit(struct cxl_decoder *cxld)
{
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);
	int id = cxld->id;

	if (cxld->flags & CXL_DECODER_F_ENABLE)
		return 0;

	dev_dbg(&port->dev, "%s commit\n", dev_name(&cxld->dev));
	if (cxl_num_decoders_committed(port) != id) {
		dev_dbg(&port->dev,
			"%s: out of order commit, expected decoder%d.%d\n",
			dev_name(&cxld->dev), port->id,
			cxl_num_decoders_committed(port));
		return -EBUSY;
	}

	port->commit_end++;
	cxld->flags |= CXL_DECODER_F_ENABLE;
	if (is_endpoint_decoder(&cxld->dev)) {
		struct cxl_endpoint_decoder *cxled =
			to_cxl_endpoint_decoder(&cxld->dev);

		cxled->state = CXL_DECODER_STATE_AUTO;
	}
	cxld_registry_update(cxld);

	return 0;
}

static void mock_decoder_reset(struct cxl_decoder *cxld)
{
	struct cxl_port *port = to_cxl_port(cxld->dev.parent);
	int id = cxld->id;

	if ((cxld->flags & CXL_DECODER_F_ENABLE) == 0)
		return;

	dev_dbg(&port->dev, "%s reset\n", dev_name(&cxld->dev));
	if (port->commit_end == id)
		cxl_port_commit_reap(cxld);
	else
		dev_dbg(&port->dev,
			"%s: out of order reset, expected decoder%d.%d\n",
			dev_name(&cxld->dev), port->id, port->commit_end);
	cxld->flags &= ~CXL_DECODER_F_ENABLE;

	if (is_endpoint_decoder(&cxld->dev)) {
		struct cxl_endpoint_decoder *cxled =
			to_cxl_endpoint_decoder(&cxld->dev);

		cxled->state = CXL_DECODER_STATE_MANUAL;
		cxled->skip = 0;
	}
	if (decoder_reset_preserve_registry)
		dev_dbg(port->uport_dev, "decoder%d: skip registry update\n",
			cxld->id);
	else
		cxld_registry_update(cxld);
}

static struct cxl_test_decoder *cxld_registry_new(struct cxl_decoder *cxld)
{
	struct cxl_test_decoder *td __free(kfree) =
		kzalloc(sizeof(*td), GFP_KERNEL);
	unsigned long key = cxld_registry_index(cxld);

	if (!td)
		return NULL;

	if (xa_insert(&decoder_registry, key, td, GFP_KERNEL)) {
		WARN_ON(1);
		return NULL;
	}

	cxld_registry_save(td, cxld);
	return no_free_ptr(td);
}

static void init_disabled_mock_decoder(struct cxl_decoder *cxld)
{
	cxld->hpa_range.start = 0;
	cxld->hpa_range.end = -1;
	cxld->interleave_ways = 1;
	cxld->interleave_granularity = 0;
	cxld->target_type = CXL_DECODER_HOSTONLYMEM;
	cxld->flags = 0;
	cxld->commit = mock_decoder_commit;
	cxld->reset = mock_decoder_reset;

	if (is_switch_decoder(&cxld->dev)) {
		struct cxl_switch_decoder *cxlsd =
			to_cxl_switch_decoder(&cxld->dev);

		for (int i = 0; i < cxlsd->nr_targets; i++) {
			cxlsd->target[i] = NULL;
			cxld->target_map[i] = 0;
		}
	} else {
		struct cxl_endpoint_decoder *cxled =
			to_cxl_endpoint_decoder(&cxld->dev);

		cxled->state = CXL_DECODER_STATE_MANUAL;
		cxled->skip = 0;
	}
}

static void default_mock_decoder(struct cxl_decoder *cxld)
{
	cxld->hpa_range = (struct range){
		.start = 0,
		.end = -1,
	};

	cxld->interleave_ways = 1;
	cxld->interleave_granularity = 256;
	cxld->target_type = CXL_DECODER_HOSTONLYMEM;
	cxld->commit = mock_decoder_commit;
	cxld->reset = mock_decoder_reset;

	WARN_ON_ONCE(!cxld_registry_new(cxld));
}

static int first_decoder(struct device *dev, const void *data)
{
	struct cxl_decoder *cxld;

	if (!is_switch_decoder(dev))
		return 0;
	cxld = to_cxl_decoder(dev);
	if (cxld->id == 0)
		return 1;
	return 0;
}

/*
 * Initialize a decoder during HDM enumeration.
 *
 * If a saved registry entry exists:
 *   - enabled decoders are restored from the saved programming
 *   - disabled decoders are initialized in a clean disabled state
 *
 * If no registry entry exists the decoder follows the normal mock
 * initialization path, including the special auto-region setup for
 * the first endpoints under host-bridge0.
 *
 * Returns true if decoder state was restored from the registry. In
 * that case the saved decode configuration (including target mapping)
 * has already been applied and the map_targets() is skipped.
 */
static bool mock_init_hdm_decoder(struct cxl_decoder *cxld)
{
	struct acpi_cedt_cfmws *window = mock_cfmws[0];
	struct platform_device *pdev = NULL;
	struct cxl_endpoint_decoder *cxled;
	struct cxl_switch_decoder *cxlsd;
	struct cxl_port *port, *iter;
	struct cxl_test_decoder *td;
	struct cxl_memdev *cxlmd;
	struct cxl_dport *dport;
	struct device *dev;
	bool hb0 = false;
	u64 base;
	int i;

	if (is_endpoint_decoder(&cxld->dev)) {
		cxled = to_cxl_endpoint_decoder(&cxld->dev);
		cxlmd = cxled_to_memdev(cxled);
		WARN_ON(!dev_is_platform(cxlmd->dev.parent));
		pdev = to_platform_device(cxlmd->dev.parent);

		/* check is endpoint is attach to host-bridge0 */
		port = cxled_to_port(cxled);
		do {
			if (port->uport_dev == &cxl_host_bridge[0]->dev) {
				hb0 = true;
				break;
			}
			if (is_cxl_port(port->dev.parent))
				port = to_cxl_port(port->dev.parent);
			else
				port = NULL;
		} while (port);
		port = cxled_to_port(cxled);
	} else {
		port = to_cxl_port(cxld->dev.parent);
	}

	td = cxld_registry_find(cxld);
	if (td) {
		bool enabled;

		if (is_switch_decoder(&cxld->dev))
			enabled = td->cxlsd.cxld.flags & CXL_DECODER_F_ENABLE;
		else
			enabled = td->cxled.cxld.flags & CXL_DECODER_F_ENABLE;

		if (enabled)
			return !cxld_registry_restore(cxld, td);

		init_disabled_mock_decoder(cxld);
		return false;
	}

	/*
	 * The first decoder on the first 2 devices on the first switch
	 * attached to host-bridge0 mock a fake / static RAM region. All
	 * other decoders are default disabled. Given the round robin
	 * assignment those devices are named cxl_mem.0, and cxl_mem.4.
	 *
	 * See 'cxl list -BMPu -m cxl_mem.0,cxl_mem.4'
	 */
	if (!is_endpoint_decoder(&cxld->dev) || !hb0 || pdev->id % 4 ||
	    pdev->id > 4 || cxld->id > 0) {
		default_mock_decoder(cxld);
		return false;
	}

	/* Simulate missing cxl_mem.4 configuration */
	if (hb0 && pdev->id == 4 && cxld->id == 0 && fail_autoassemble) {
		default_mock_decoder(cxld);
		return false;
	}

	base = window->base_hpa;
	if (extended_linear_cache)
		base += mock_auto_region_size;
	cxld->hpa_range = (struct range) {
		.start = base,
		.end = base + mock_auto_region_size - 1,
	};

	cxld->interleave_ways = 2;
	eig_to_granularity(window->granularity, &cxld->interleave_granularity);
	cxld->target_type = CXL_DECODER_HOSTONLYMEM;
	cxld->flags = CXL_DECODER_F_ENABLE;
	cxled->state = CXL_DECODER_STATE_AUTO;
	port->commit_end = cxld->id;
	devm_cxl_dpa_reserve(cxled, 0,
			     mock_auto_region_size / cxld->interleave_ways, 0);
	cxld->commit = mock_decoder_commit;
	cxld->reset = mock_decoder_reset;

	WARN_ON_ONCE(!cxld_registry_new(cxld));
	/*
	 * Now that endpoint decoder is set up, walk up the hierarchy
	 * and setup the switch and root port decoders targeting @cxlmd.
	 */
	iter = port;
	for (i = 0; i < 2; i++) {
		dport = iter->parent_dport;
		iter = dport->port;
		dev = device_find_child(&iter->dev, NULL, first_decoder);
		/*
		 * Ancestor ports are guaranteed to be enumerated before
		 * @port, and all ports have at least one decoder.
		 */
		if (WARN_ON(!dev))
			continue;

		cxlsd = to_cxl_switch_decoder(dev);
		if (i == 0) {
			/* put cxl_mem.4 second in the decode order */
			if (pdev->id == 4) {
				cxlsd->target[1] = dport;
				cxlsd->cxld.target_map[1] = dport->port_id;
			} else {
				cxlsd->target[0] = dport;
				cxlsd->cxld.target_map[0] = dport->port_id;
			}
		} else {
			cxlsd->target[0] = dport;
			cxlsd->cxld.target_map[0] = dport->port_id;
		}
		cxld = &cxlsd->cxld;
		cxld->target_type = CXL_DECODER_HOSTONLYMEM;
		cxld->flags = CXL_DECODER_F_ENABLE;
		iter->commit_end = 0;
		/*
		 * Switch targets 2 endpoints, while host bridge targets
		 * one root port
		 */
		if (i == 0)
			cxld->interleave_ways = 2;
		else
			cxld->interleave_ways = 1;
		cxld->interleave_granularity = 4096;
		cxld->hpa_range = (struct range) {
			.start = base,
			.end = base + mock_auto_region_size - 1,
		};
		cxld->commit = mock_decoder_commit;
		cxld->reset = mock_decoder_reset;

		cxld_registry_update(cxld);
		put_device(dev);
	}

	return false;
}

static int mock_cxl_enumerate_decoders(struct cxl_hdm *cxlhdm,
				       struct cxl_endpoint_dvsec_info *info)
{
	struct cxl_port *port = cxlhdm->port;
	struct cxl_port *parent_port = to_cxl_port(port->dev.parent);
	int target_count, i;
	bool restored;

	if (is_cxl_endpoint(port))
		target_count = 0;
	else if (is_cxl_root(parent_port))
		target_count = NR_CXL_ROOT_PORTS;
	else
		target_count = NR_CXL_SWITCH_PORTS;

	for (i = 0; i < NR_CXL_PORT_DECODERS; i++) {
		struct target_map_ctx ctx = {
			.target_count = target_count,
		};
		struct cxl_decoder *cxld;
		int rc;

		if (target_count) {
			struct cxl_switch_decoder *cxlsd;

			cxlsd = cxl_switch_decoder_alloc(port, target_count);
			if (IS_ERR(cxlsd)) {
				dev_warn(&port->dev,
					 "Failed to allocate the decoder\n");
				return PTR_ERR(cxlsd);
			}
			cxld = &cxlsd->cxld;
		} else {
			struct cxl_endpoint_decoder *cxled;

			cxled = cxl_endpoint_decoder_alloc(port);

			if (IS_ERR(cxled)) {
				dev_warn(&port->dev,
					 "Failed to allocate the decoder\n");
				return PTR_ERR(cxled);
			}
			cxld = &cxled->cxld;
		}

		ctx.target_map = cxld->target_map;
		restored = mock_init_hdm_decoder(cxld);
		if (target_count && !restored) {
			rc = device_for_each_child(port->uport_dev, &ctx,
						   map_targets);
			if (rc) {
				put_device(&cxld->dev);
				return rc;
			}
		}

		rc = cxl_decoder_add_locked(cxld);
		if (rc) {
			put_device(&cxld->dev);
			dev_err(&port->dev, "Failed to add decoder\n");
			return rc;
		}

		rc = cxl_decoder_autoremove(&port->dev, cxld);
		if (rc)
			return rc;
		dev_dbg(&cxld->dev, "Added to port %s\n", dev_name(&port->dev));
	}

	return 0;
}

static int __mock_cxl_decoders_setup(struct cxl_port *port)
{
	struct cxl_hdm *cxlhdm;

	cxlhdm = mock_cxl_setup_hdm(port, NULL);
	if (IS_ERR(cxlhdm)) {
		if (PTR_ERR(cxlhdm) != -ENODEV)
			dev_err(&port->dev, "Failed to map HDM decoder capability\n");
		return PTR_ERR(cxlhdm);
	}

	return mock_cxl_enumerate_decoders(cxlhdm, NULL);
}

static int mock_cxl_switch_port_decoders_setup(struct cxl_port *port)
{
	if (is_cxl_root(port) || is_cxl_endpoint(port))
		return -EOPNOTSUPP;

	return __mock_cxl_decoders_setup(port);
}

static int mock_cxl_endpoint_decoders_setup(struct cxl_port *port)
{
	if (!is_cxl_endpoint(port))
		return -EOPNOTSUPP;

	return __mock_cxl_decoders_setup(port);
}

static int get_port_array(struct cxl_port *port,
			  struct platform_device ***port_array,
			  int *port_array_size)
{
	struct platform_device **array;
	int array_size;

	if (port->depth == 1) {
		if (is_multi_bridge(port->uport_dev)) {
			array_size = ARRAY_SIZE(cxl_root_port);
			array = cxl_root_port;
		} else if (is_single_bridge(port->uport_dev)) {
			array_size = ARRAY_SIZE(cxl_root_single);
			array = cxl_root_single;
		} else {
			dev_dbg(&port->dev, "%s: unknown bridge type\n",
				dev_name(port->uport_dev));
			return -ENXIO;
		}
	} else if (port->depth == 2) {
		struct cxl_port *parent = to_cxl_port(port->dev.parent);

		if (is_multi_bridge(parent->uport_dev)) {
			array_size = ARRAY_SIZE(cxl_switch_dport);
			array = cxl_switch_dport;
		} else if (is_single_bridge(parent->uport_dev)) {
			array_size = ARRAY_SIZE(cxl_swd_single);
			array = cxl_swd_single;
		} else {
			dev_dbg(&port->dev, "%s: unknown bridge type\n",
				dev_name(port->uport_dev));
			return -ENXIO;
		}
	} else {
		dev_WARN_ONCE(&port->dev, 1, "unexpected depth %d\n",
			      port->depth);
		return -ENXIO;
	}

	*port_array = array;
	*port_array_size = array_size;

	return 0;
}

static struct cxl_dport *mock_cxl_add_dport_by_dev(struct cxl_port *port,
						   struct device *dport_dev)
{
	struct platform_device **array;
	int rc, i, array_size;

	rc = get_port_array(port, &array, &array_size);
	if (rc)
		return ERR_PTR(rc);

	for (i = 0; i < array_size; i++) {
		struct platform_device *pdev = array[i];

		if (pdev->dev.parent != port->uport_dev) {
			dev_dbg(&port->dev, "%s: mismatch parent %s\n",
				dev_name(port->uport_dev),
				dev_name(pdev->dev.parent));
			continue;
		}

		if (&pdev->dev != dport_dev)
			continue;

		return devm_cxl_add_dport(port, &pdev->dev, pdev->id,
					  CXL_RESOURCE_NONE);
	}

	return ERR_PTR(-ENODEV);
}

/*
 * Faking the cxl_dpa_perf for the memdev when appropriate.
 */
static void dpa_perf_setup(struct cxl_port *endpoint, struct range *range,
			   struct cxl_dpa_perf *dpa_perf)
{
	dpa_perf->qos_class = FAKE_QTG_ID;
	dpa_perf->dpa_range = *range;
	for (int i = 0; i < ACCESS_COORDINATE_MAX; i++) {
		dpa_perf->coord[i].read_latency = 500;
		dpa_perf->coord[i].write_latency = 500;
		dpa_perf->coord[i].read_bandwidth = 1000;
		dpa_perf->coord[i].write_bandwidth = 1000;
	}
}

static void mock_cxl_endpoint_parse_cdat(struct cxl_port *port)
{
	struct cxl_root *cxl_root __free(put_cxl_root) =
		find_cxl_root(port);
	struct cxl_memdev *cxlmd = to_cxl_memdev(port->uport_dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct access_coordinate ep_c[ACCESS_COORDINATE_MAX];

	if (!cxl_root)
		return;

	for (int i = 0; i < cxlds->nr_partitions; i++) {
		struct resource *res = &cxlds->part[i].res;
		struct cxl_dpa_perf *perf = &cxlds->part[i].perf;
		struct range range = {
			.start = res->start,
			.end = res->end,
		};

		dpa_perf_setup(port, &range, perf);
	}

	cxl_memdev_update_perf(cxlmd);

	/*
	 * This function is here to only test the topology iterator. It serves
	 * no other purpose.
	 */
	cxl_endpoint_get_perf_coordinates(port, ep_c);
}

/*
 * Simulate that the first half of mock CXL Window 0 is "Soft Reserve" capacity
 */
static int mock_walk_hmem_resources(struct device *host, walk_hmem_fn fn)
{
	struct acpi_cedt_cfmws *cfmws = mock_cfmws[0];
	struct resource window =
		DEFINE_RES_MEM(cfmws->base_hpa, cfmws->window_size / 2);

	dev_dbg(host, "walk cxl_test resource: %pr\n", &window);
	return fn(host, 0, &window);
}

/*
 * This should only be called by the dax_hmem case, treat mismatches (negative
 * result) as "fallback to base region_intersects()". Simulate that the first
 * half of mock CXL Window 0 is IORES_DESC_CXL capacity.
 */
static int mock_region_intersects(resource_size_t start, size_t size,
				  unsigned long flags, unsigned long desc)
{
	struct resource res = DEFINE_RES_MEM(start, size);
	struct acpi_cedt_cfmws *cfmws = mock_cfmws[0];
	struct resource window =
		DEFINE_RES_MEM(cfmws->base_hpa, cfmws->window_size / 2);

	if (resource_overlaps(&res, &window))
		return REGION_INTERSECTS;
	pr_debug("warning: no cxl_test CXL intersection for %pr\n", &res);
	return -1;
}


static int
mock_region_intersects_soft_reserve(resource_size_t start, size_t size)
{
	struct resource res = DEFINE_RES_MEM(start, size);
	struct acpi_cedt_cfmws *cfmws = mock_cfmws[0];
	struct resource window =
		DEFINE_RES_MEM(cfmws->base_hpa, cfmws->window_size / 2);

	if (resource_overlaps(&res, &window))
		return REGION_INTERSECTS;
	pr_debug("warning: no cxl_test soft reserve intersection for %pr\n", &res);
	return -1;
}

static struct cxl_mock_ops cxl_mock_ops = {
	.is_mock_adev = is_mock_adev,
	.is_mock_bridge = is_mock_bridge,
	.is_mock_bus = is_mock_bus,
	.is_mock_port = is_mock_port,
	.is_mock_dev = is_mock_dev,
	.acpi_table_parse_cedt = mock_acpi_table_parse_cedt,
	.acpi_evaluate_integer = mock_acpi_evaluate_integer,
	.acpi_pci_find_root = mock_acpi_pci_find_root,
	.devm_cxl_switch_port_decoders_setup = mock_cxl_switch_port_decoders_setup,
	.devm_cxl_endpoint_decoders_setup = mock_cxl_endpoint_decoders_setup,
	.cxl_endpoint_parse_cdat = mock_cxl_endpoint_parse_cdat,
	.devm_cxl_add_dport_by_dev = mock_cxl_add_dport_by_dev,
	.hmat_get_extended_linear_cache_size =
		mock_hmat_get_extended_linear_cache_size,
	.walk_hmem_resources = mock_walk_hmem_resources,
	.region_intersects = mock_region_intersects,
	.region_intersects_soft_reserve = mock_region_intersects_soft_reserve,
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

static __init int cxl_rch_topo_init(void)
{
	int rc, i;

	for (i = 0; i < ARRAY_SIZE(cxl_rch); i++) {
		int idx = NR_CXL_HOST_BRIDGES + NR_CXL_SINGLE_HOST + i;
		struct acpi_device *adev = &host_bridge[idx];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_host_bridge", idx);
		if (!pdev)
			goto err_bridge;

		mock_companion(adev, &pdev->dev);
		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_bridge;
		}

		cxl_rch[i] = pdev;
		mock_pci_bus[idx].bridge = &pdev->dev;
		rc = sysfs_create_link(&pdev->dev.kobj, &pdev->dev.kobj,
				       "firmware_node");
		if (rc)
			goto err_bridge;
	}

	return 0;

err_bridge:
	for (i = ARRAY_SIZE(cxl_rch) - 1; i >= 0; i--) {
		struct platform_device *pdev = cxl_rch[i];

		if (!pdev)
			continue;
		sysfs_remove_link(&pdev->dev.kobj, "firmware_node");
		platform_device_unregister(cxl_rch[i]);
	}

	return rc;
}

static void cxl_rch_topo_exit(void)
{
	int i;

	for (i = ARRAY_SIZE(cxl_rch) - 1; i >= 0; i--) {
		struct platform_device *pdev = cxl_rch[i];

		if (!pdev)
			continue;
		sysfs_remove_link(&pdev->dev.kobj, "firmware_node");
		platform_device_unregister(cxl_rch[i]);
	}
}

static __init int cxl_single_topo_init(void)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(cxl_hb_single); i++) {
		struct acpi_device *adev =
			&host_bridge[NR_CXL_HOST_BRIDGES + i];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_host_bridge",
					     NR_CXL_HOST_BRIDGES + i);
		if (!pdev)
			goto err_bridge;

		mock_companion(adev, &pdev->dev);
		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_bridge;
		}

		cxl_hb_single[i] = pdev;
		mock_pci_bus[i + NR_CXL_HOST_BRIDGES].bridge = &pdev->dev;
		rc = sysfs_create_link(&pdev->dev.kobj, &pdev->dev.kobj,
				       "physical_node");
		if (rc)
			goto err_bridge;
	}

	for (i = 0; i < ARRAY_SIZE(cxl_root_single); i++) {
		struct platform_device *bridge =
			cxl_hb_single[i % ARRAY_SIZE(cxl_hb_single)];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_root_port",
					     NR_MULTI_ROOT + i);
		if (!pdev)
			goto err_port;
		pdev->dev.parent = &bridge->dev;

		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_port;
		}
		cxl_root_single[i] = pdev;
	}

	for (i = 0; i < ARRAY_SIZE(cxl_swu_single); i++) {
		struct platform_device *root_port = cxl_root_single[i];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_switch_uport",
					     NR_MULTI_ROOT + i);
		if (!pdev)
			goto err_uport;
		pdev->dev.parent = &root_port->dev;

		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_uport;
		}
		cxl_swu_single[i] = pdev;
	}

	for (i = 0; i < ARRAY_SIZE(cxl_swd_single); i++) {
		struct platform_device *uport =
			cxl_swu_single[i % ARRAY_SIZE(cxl_swu_single)];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_switch_dport",
					     i + NR_MEM_MULTI);
		if (!pdev)
			goto err_dport;
		pdev->dev.parent = &uport->dev;

		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_dport;
		}
		cxl_swd_single[i] = pdev;
	}

	return 0;

err_dport:
	for (i = ARRAY_SIZE(cxl_swd_single) - 1; i >= 0; i--)
		platform_device_unregister(cxl_swd_single[i]);
err_uport:
	for (i = ARRAY_SIZE(cxl_swu_single) - 1; i >= 0; i--)
		platform_device_unregister(cxl_swu_single[i]);
err_port:
	for (i = ARRAY_SIZE(cxl_root_single) - 1; i >= 0; i--)
		platform_device_unregister(cxl_root_single[i]);
err_bridge:
	for (i = ARRAY_SIZE(cxl_hb_single) - 1; i >= 0; i--) {
		struct platform_device *pdev = cxl_hb_single[i];

		if (!pdev)
			continue;
		sysfs_remove_link(&pdev->dev.kobj, "physical_node");
		platform_device_unregister(cxl_hb_single[i]);
	}

	return rc;
}

static void cxl_single_topo_exit(void)
{
	int i;

	for (i = ARRAY_SIZE(cxl_swd_single) - 1; i >= 0; i--)
		platform_device_unregister(cxl_swd_single[i]);
	for (i = ARRAY_SIZE(cxl_swu_single) - 1; i >= 0; i--)
		platform_device_unregister(cxl_swu_single[i]);
	for (i = ARRAY_SIZE(cxl_root_single) - 1; i >= 0; i--)
		platform_device_unregister(cxl_root_single[i]);
	for (i = ARRAY_SIZE(cxl_hb_single) - 1; i >= 0; i--) {
		struct platform_device *pdev = cxl_hb_single[i];

		if (!pdev)
			continue;
		sysfs_remove_link(&pdev->dev.kobj, "physical_node");
		platform_device_unregister(cxl_hb_single[i]);
	}
}

static void cxl_mem_exit(void)
{
	int i;

	for (i = ARRAY_SIZE(cxl_rcd) - 1; i >= 0; i--)
		platform_device_unregister(cxl_rcd[i]);
	for (i = ARRAY_SIZE(cxl_mem_single) - 1; i >= 0; i--)
		platform_device_unregister(cxl_mem_single[i]);
	for (i = ARRAY_SIZE(cxl_mem) - 1; i >= 0; i--)
		platform_device_unregister(cxl_mem[i]);
}

static int cxl_mem_init(void)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(cxl_mem); i++) {
		struct platform_device *dport = cxl_switch_dport[i];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_mem", i);
		if (!pdev)
			goto err_mem;
		pdev->dev.parent = &dport->dev;
		set_dev_node(&pdev->dev, i % 2);

		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_mem;
		}
		cxl_mem[i] = pdev;
	}

	for (i = 0; i < ARRAY_SIZE(cxl_mem_single); i++) {
		struct platform_device *dport = cxl_swd_single[i];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_mem", NR_MEM_MULTI + i);
		if (!pdev)
			goto err_single;
		pdev->dev.parent = &dport->dev;
		set_dev_node(&pdev->dev, i % 2);

		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_single;
		}
		cxl_mem_single[i] = pdev;
	}

	for (i = 0; i < ARRAY_SIZE(cxl_rcd); i++) {
		int idx = NR_MEM_MULTI + NR_MEM_SINGLE + i;
		struct platform_device *rch = cxl_rch[i];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_rcd", idx);
		if (!pdev)
			goto err_rcd;
		pdev->dev.parent = &rch->dev;
		set_dev_node(&pdev->dev, i % 2);

		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_rcd;
		}
		cxl_rcd[i] = pdev;
	}

	return 0;

err_rcd:
	for (i = ARRAY_SIZE(cxl_rcd) - 1; i >= 0; i--)
		platform_device_unregister(cxl_rcd[i]);
err_single:
	for (i = ARRAY_SIZE(cxl_mem_single) - 1; i >= 0; i--)
		platform_device_unregister(cxl_mem_single[i]);
err_mem:
	for (i = ARRAY_SIZE(cxl_mem) - 1; i >= 0; i--)
		platform_device_unregister(cxl_mem[i]);
	return rc;
}

static ssize_t
decoder_reset_preserve_registry_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", decoder_reset_preserve_registry);
}

static ssize_t
decoder_reset_preserve_registry_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int rc;

	rc = kstrtobool(buf, &decoder_reset_preserve_registry);
	if (rc)
		return rc;
	return count;
}

static DEVICE_ATTR_RW(decoder_reset_preserve_registry);

static struct attribute *cxl_acpi_attrs[] = {
	&dev_attr_decoder_reset_preserve_registry.attr, NULL
};
ATTRIBUTE_GROUPS(cxl_acpi);

static __init int cxl_test_init(void)
{
	int rc, i;
	struct range mappable;

	cxl_acpi_test();
	cxl_core_test();
	cxl_mem_test();
	cxl_pmem_test();
	cxl_port_test();

	register_cxl_mock_ops(&cxl_mock_ops);

	cxl_mock_pool = gen_pool_create(ilog2(SZ_2M), NUMA_NO_NODE);
	if (!cxl_mock_pool) {
		rc = -ENOMEM;
		goto err_gen_pool_create;
	}
	mappable = mhp_get_pluggable_range(true);

	rc = gen_pool_add(cxl_mock_pool,
			  min(iomem_resource.end + 1 - SZ_64G,
			      mappable.end + 1 - SZ_64G),
			  SZ_64G, NUMA_NO_NODE);
	if (rc)
		goto err_gen_pool_add;

	if (interleave_arithmetic == 1) {
		cfmws_start = CFMWS_XOR_ARRAY_START;
		cfmws_end = CFMWS_XOR_ARRAY_END;
	} else {
		cfmws_start = CFMWS_MOD_ARRAY_START;
		cfmws_end = CFMWS_MOD_ARRAY_END;
	}

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
		mock_pci_bus[i].bridge = &pdev->dev;
		rc = sysfs_create_link(&pdev->dev.kobj, &pdev->dev.kobj,
				       "physical_node");
		if (rc)
			goto err_bridge;
	}

	for (i = 0; i < ARRAY_SIZE(cxl_root_port); i++) {
		struct platform_device *bridge =
			cxl_host_bridge[i % ARRAY_SIZE(cxl_host_bridge)];
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

	BUILD_BUG_ON(ARRAY_SIZE(cxl_switch_uport) != ARRAY_SIZE(cxl_root_port));
	for (i = 0; i < ARRAY_SIZE(cxl_switch_uport); i++) {
		struct platform_device *root_port = cxl_root_port[i];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_switch_uport", i);
		if (!pdev)
			goto err_uport;
		pdev->dev.parent = &root_port->dev;

		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_uport;
		}
		cxl_switch_uport[i] = pdev;
	}

	for (i = 0; i < ARRAY_SIZE(cxl_switch_dport); i++) {
		struct platform_device *uport =
			cxl_switch_uport[i % ARRAY_SIZE(cxl_switch_uport)];
		struct platform_device *pdev;

		pdev = platform_device_alloc("cxl_switch_dport", i);
		if (!pdev)
			goto err_dport;
		pdev->dev.parent = &uport->dev;

		rc = platform_device_add(pdev);
		if (rc) {
			platform_device_put(pdev);
			goto err_dport;
		}
		cxl_switch_dport[i] = pdev;
	}

	rc = cxl_single_topo_init();
	if (rc)
		goto err_dport;

	rc = cxl_rch_topo_init();
	if (rc)
		goto err_single;

	cxl_acpi = platform_device_alloc("cxl_acpi", 0);
	if (!cxl_acpi)
		goto err_rch;

	mock_companion(&acpi0017_mock, &cxl_acpi->dev);
	acpi0017_mock.dev.bus = &platform_bus_type;
	cxl_acpi->dev.groups = cxl_acpi_groups;

	rc = platform_device_add(cxl_acpi);
	if (rc)
		goto err_root;

	rc = cxl_mem_init();
	if (rc)
		goto err_root;

	rc = hmem_test_init();
	if (rc)
		goto err_mem;

	return 0;

err_mem:
	cxl_mem_exit();
err_root:
	platform_device_put(cxl_acpi);
err_rch:
	cxl_rch_topo_exit();
err_single:
	cxl_single_topo_exit();
err_dport:
	for (i = ARRAY_SIZE(cxl_switch_dport) - 1; i >= 0; i--)
		platform_device_unregister(cxl_switch_dport[i]);
err_uport:
	for (i = ARRAY_SIZE(cxl_switch_uport) - 1; i >= 0; i--)
		platform_device_unregister(cxl_switch_uport[i]);
err_port:
	for (i = ARRAY_SIZE(cxl_root_port) - 1; i >= 0; i--)
		platform_device_unregister(cxl_root_port[i]);
err_bridge:
	for (i = ARRAY_SIZE(cxl_host_bridge) - 1; i >= 0; i--) {
		struct platform_device *pdev = cxl_host_bridge[i];

		if (!pdev)
			continue;
		sysfs_remove_link(&pdev->dev.kobj, "physical_node");
		platform_device_unregister(cxl_host_bridge[i]);
	}
err_populate:
	depopulate_all_mock_resources();
err_gen_pool_add:
	gen_pool_destroy(cxl_mock_pool);
err_gen_pool_create:
	unregister_cxl_mock_ops(&cxl_mock_ops);
	return rc;
}

static void free_decoder_registry(void)
{
	unsigned long index;
	void *entry;

	xa_for_each(&decoder_registry, index, entry) {
		xa_erase(&decoder_registry, index);
		kfree(entry);
	}
}

static __exit void cxl_test_exit(void)
{
	int i;

	hmem_test_exit();
	cxl_mem_exit();
	platform_device_unregister(cxl_acpi);
	cxl_rch_topo_exit();
	cxl_single_topo_exit();
	for (i = ARRAY_SIZE(cxl_switch_dport) - 1; i >= 0; i--)
		platform_device_unregister(cxl_switch_dport[i]);
	for (i = ARRAY_SIZE(cxl_switch_uport) - 1; i >= 0; i--)
		platform_device_unregister(cxl_switch_uport[i]);
	for (i = ARRAY_SIZE(cxl_root_port) - 1; i >= 0; i--)
		platform_device_unregister(cxl_root_port[i]);
	for (i = ARRAY_SIZE(cxl_host_bridge) - 1; i >= 0; i--) {
		struct platform_device *pdev = cxl_host_bridge[i];

		if (!pdev)
			continue;
		sysfs_remove_link(&pdev->dev.kobj, "physical_node");
		platform_device_unregister(cxl_host_bridge[i]);
	}
	depopulate_all_mock_resources();
	gen_pool_destroy(cxl_mock_pool);
	unregister_cxl_mock_ops(&cxl_mock_ops);
	free_decoder_registry();
	xa_destroy(&decoder_registry);
}

module_param(interleave_arithmetic, int, 0444);
MODULE_PARM_DESC(interleave_arithmetic, "Modulo:0, XOR:1");
module_param(extended_linear_cache, bool, 0444);
MODULE_PARM_DESC(extended_linear_cache, "Enable extended linear cache support");
module_param(fail_autoassemble, bool, 0444);
MODULE_PARM_DESC(fail_autoassemble, "Simulate missing member of an auto-region");
module_init(cxl_test_init);
module_exit(cxl_test_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("cxl_test: setup module");
MODULE_IMPORT_NS("ACPI");
MODULE_IMPORT_NS("CXL");
