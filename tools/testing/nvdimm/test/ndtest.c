// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/genalloc.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <linux/list_sort.h>
#include <linux/libnvdimm.h>
#include <linux/ndctl.h>
#include <nd-core.h>
#include <linux/printk.h>
#include <linux/seq_buf.h>

#include "../watermark.h"
#include "nfit_test.h"
#include "ndtest.h"

enum {
	DIMM_SIZE = SZ_32M,
	LABEL_SIZE = SZ_128K,
	NUM_INSTANCES = 2,
	NUM_DCR = 4,
	NDTEST_MAX_MAPPING = 6,
};

#define NDTEST_SCM_DIMM_CMD_MASK	   \
	((1ul << ND_CMD_GET_CONFIG_SIZE) | \
	 (1ul << ND_CMD_GET_CONFIG_DATA) | \
	 (1ul << ND_CMD_SET_CONFIG_DATA) | \
	 (1ul << ND_CMD_CALL))

#define NFIT_DIMM_HANDLE(node, socket, imc, chan, dimm)			\
	(((node & 0xfff) << 16) | ((socket & 0xf) << 12)		\
	 | ((imc & 0xf) << 8) | ((chan & 0xf) << 4) | (dimm & 0xf))

static DEFINE_SPINLOCK(ndtest_lock);
static struct ndtest_priv *instances[NUM_INSTANCES];

static const struct class ndtest_dimm_class = {
	.name = "nfit_test_dimm",
};

static struct gen_pool *ndtest_pool;

static struct ndtest_dimm dimm_group1[] = {
	{
		.size = DIMM_SIZE,
		.handle = NFIT_DIMM_HANDLE(0, 0, 0, 0, 0),
		.uuid_str = "1e5c75d2-b618-11ea-9aa3-507b9ddc0f72",
		.physical_id = 0,
		.num_formats = 2,
	},
	{
		.size = DIMM_SIZE,
		.handle = NFIT_DIMM_HANDLE(0, 0, 0, 0, 1),
		.uuid_str = "1c4d43ac-b618-11ea-be80-507b9ddc0f72",
		.physical_id = 1,
		.num_formats = 2,
	},
	{
		.size = DIMM_SIZE,
		.handle = NFIT_DIMM_HANDLE(0, 0, 1, 0, 0),
		.uuid_str = "a9f17ffc-b618-11ea-b36d-507b9ddc0f72",
		.physical_id = 2,
		.num_formats = 2,
	},
	{
		.size = DIMM_SIZE,
		.handle = NFIT_DIMM_HANDLE(0, 0, 1, 0, 1),
		.uuid_str = "b6b83b22-b618-11ea-8aae-507b9ddc0f72",
		.physical_id = 3,
		.num_formats = 2,
	},
	{
		.size = DIMM_SIZE,
		.handle = NFIT_DIMM_HANDLE(0, 1, 0, 0, 0),
		.uuid_str = "bf9baaee-b618-11ea-b181-507b9ddc0f72",
		.physical_id = 4,
		.num_formats = 2,
	},
};

static struct ndtest_dimm dimm_group2[] = {
	{
		.size = DIMM_SIZE,
		.handle = NFIT_DIMM_HANDLE(1, 0, 0, 0, 0),
		.uuid_str = "ca0817e2-b618-11ea-9db3-507b9ddc0f72",
		.physical_id = 0,
		.num_formats = 1,
		.flags = PAPR_PMEM_UNARMED | PAPR_PMEM_EMPTY |
			 PAPR_PMEM_SAVE_FAILED | PAPR_PMEM_SHUTDOWN_DIRTY |
			 PAPR_PMEM_HEALTH_FATAL,
	},
};

static struct ndtest_mapping region0_mapping[] = {
	{
		.dimm = 0,
		.position = 0,
		.start = 0,
		.size = SZ_16M,
	},
	{
		.dimm = 1,
		.position = 1,
		.start = 0,
		.size = SZ_16M,
	}
};

static struct ndtest_mapping region1_mapping[] = {
	{
		.dimm = 0,
		.position = 0,
		.start = SZ_16M,
		.size = SZ_16M,
	},
	{
		.dimm = 1,
		.position = 1,
		.start = SZ_16M,
		.size = SZ_16M,
	},
	{
		.dimm = 2,
		.position = 2,
		.start = SZ_16M,
		.size = SZ_16M,
	},
	{
		.dimm = 3,
		.position = 3,
		.start = SZ_16M,
		.size = SZ_16M,
	},
};

static struct ndtest_region bus0_regions[] = {
	{
		.type = ND_DEVICE_NAMESPACE_PMEM,
		.num_mappings = ARRAY_SIZE(region0_mapping),
		.mapping = region0_mapping,
		.size = DIMM_SIZE,
		.range_index = 1,
	},
	{
		.type = ND_DEVICE_NAMESPACE_PMEM,
		.num_mappings = ARRAY_SIZE(region1_mapping),
		.mapping = region1_mapping,
		.size = DIMM_SIZE * 2,
		.range_index = 2,
	},
};

static struct ndtest_mapping region6_mapping[] = {
	{
		.dimm = 0,
		.position = 0,
		.start = 0,
		.size = DIMM_SIZE,
	},
};

static struct ndtest_region bus1_regions[] = {
	{
		.type = ND_DEVICE_NAMESPACE_IO,
		.num_mappings = ARRAY_SIZE(region6_mapping),
		.mapping = region6_mapping,
		.size = DIMM_SIZE,
		.range_index = 1,
	},
};

static struct ndtest_config bus_configs[NUM_INSTANCES] = {
	/* bus 1 */
	{
		.dimm_start = 0,
		.dimm_count = ARRAY_SIZE(dimm_group1),
		.dimms = dimm_group1,
		.regions = bus0_regions,
		.num_regions = ARRAY_SIZE(bus0_regions),
	},
	/* bus 2 */
	{
		.dimm_start = ARRAY_SIZE(dimm_group1),
		.dimm_count = ARRAY_SIZE(dimm_group2),
		.dimms = dimm_group2,
		.regions = bus1_regions,
		.num_regions = ARRAY_SIZE(bus1_regions),
	},
};

static inline struct ndtest_priv *to_ndtest_priv(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	return container_of(pdev, struct ndtest_priv, pdev);
}

static int ndtest_config_get(struct ndtest_dimm *p, unsigned int buf_len,
			     struct nd_cmd_get_config_data_hdr *hdr)
{
	unsigned int len;

	if ((hdr->in_offset + hdr->in_length) > LABEL_SIZE)
		return -EINVAL;

	hdr->status = 0;
	len = min(hdr->in_length, LABEL_SIZE - hdr->in_offset);
	memcpy(hdr->out_buf, p->label_area + hdr->in_offset, len);

	return buf_len - len;
}

static int ndtest_config_set(struct ndtest_dimm *p, unsigned int buf_len,
			     struct nd_cmd_set_config_hdr *hdr)
{
	unsigned int len;

	if ((hdr->in_offset + hdr->in_length) > LABEL_SIZE)
		return -EINVAL;

	len = min(hdr->in_length, LABEL_SIZE - hdr->in_offset);
	memcpy(p->label_area + hdr->in_offset, hdr->in_buf, len);

	return buf_len - len;
}

static int ndtest_get_config_size(struct ndtest_dimm *dimm, unsigned int buf_len,
				  struct nd_cmd_get_config_size *size)
{
	size->status = 0;
	size->max_xfer = 8;
	size->config_size = dimm->config_size;

	return 0;
}

static int ndtest_ctl(struct nvdimm_bus_descriptor *nd_desc,
		      struct nvdimm *nvdimm, unsigned int cmd, void *buf,
		      unsigned int buf_len, int *cmd_rc)
{
	struct ndtest_dimm *dimm;
	int _cmd_rc;

	if (!cmd_rc)
		cmd_rc = &_cmd_rc;

	*cmd_rc = 0;

	if (!nvdimm)
		return -EINVAL;

	dimm = nvdimm_provider_data(nvdimm);
	if (!dimm)
		return -EINVAL;

	switch (cmd) {
	case ND_CMD_GET_CONFIG_SIZE:
		*cmd_rc = ndtest_get_config_size(dimm, buf_len, buf);
		break;
	case ND_CMD_GET_CONFIG_DATA:
		*cmd_rc = ndtest_config_get(dimm, buf_len, buf);
		break;
	case ND_CMD_SET_CONFIG_DATA:
		*cmd_rc = ndtest_config_set(dimm, buf_len, buf);
		break;
	default:
		return -EINVAL;
	}

	/* Failures for a DIMM can be injected using fail_cmd and
	 * fail_cmd_code, see the device attributes below
	 */
	if ((1 << cmd) & dimm->fail_cmd)
		return dimm->fail_cmd_code ? dimm->fail_cmd_code : -EIO;

	return 0;
}

static struct nfit_test_resource *ndtest_resource_lookup(resource_size_t addr)
{
	int i;

	for (i = 0; i < NUM_INSTANCES; i++) {
		struct nfit_test_resource *n, *nfit_res = NULL;
		struct ndtest_priv *t = instances[i];

		if (!t)
			continue;
		spin_lock(&ndtest_lock);
		list_for_each_entry(n, &t->resources, list) {
			if (addr >= n->res.start && (addr < n->res.start
						+ resource_size(&n->res))) {
				nfit_res = n;
				break;
			} else if (addr >= (unsigned long) n->buf
					&& (addr < (unsigned long) n->buf
						+ resource_size(&n->res))) {
				nfit_res = n;
				break;
			}
		}
		spin_unlock(&ndtest_lock);
		if (nfit_res)
			return nfit_res;
	}

	pr_warn("Failed to get resource\n");

	return NULL;
}

static void ndtest_release_resource(void *data)
{
	struct nfit_test_resource *res  = data;

	spin_lock(&ndtest_lock);
	list_del(&res->list);
	spin_unlock(&ndtest_lock);

	if (resource_size(&res->res) >= DIMM_SIZE)
		gen_pool_free(ndtest_pool, res->res.start,
				resource_size(&res->res));
	vfree(res->buf);
	kfree(res);
}

static void *ndtest_alloc_resource(struct ndtest_priv *p, size_t size,
				   dma_addr_t *dma)
{
	dma_addr_t __dma;
	void *buf;
	struct nfit_test_resource *res;
	struct genpool_data_align data = {
		.align = SZ_128M,
	};

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		return NULL;

	buf = vmalloc(size);
	if (size >= DIMM_SIZE)
		__dma = gen_pool_alloc_algo(ndtest_pool, size,
					    gen_pool_first_fit_align, &data);
	else
		__dma = (unsigned long) buf;

	if (!__dma)
		goto buf_err;

	INIT_LIST_HEAD(&res->list);
	res->dev = &p->pdev.dev;
	res->buf = buf;
	res->res.start = __dma;
	res->res.end = __dma + size - 1;
	res->res.name = "NFIT";
	spin_lock_init(&res->lock);
	INIT_LIST_HEAD(&res->requests);
	spin_lock(&ndtest_lock);
	list_add(&res->list, &p->resources);
	spin_unlock(&ndtest_lock);

	if (dma)
		*dma = __dma;

	if (!devm_add_action(&p->pdev.dev, ndtest_release_resource, res))
		return res->buf;

buf_err:
	if (__dma && size >= DIMM_SIZE)
		gen_pool_free(ndtest_pool, __dma, size);
	if (buf)
		vfree(buf);
	kfree(res);

	return NULL;
}

static ssize_t range_index_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nd_region *nd_region = to_nd_region(dev);
	struct ndtest_region *region = nd_region_provider_data(nd_region);

	return sprintf(buf, "%d\n", region->range_index);
}
static DEVICE_ATTR_RO(range_index);

static struct attribute *ndtest_region_attributes[] = {
	&dev_attr_range_index.attr,
	NULL,
};

static const struct attribute_group ndtest_region_attribute_group = {
	.name = "papr",
	.attrs = ndtest_region_attributes,
};

static const struct attribute_group *ndtest_region_attribute_groups[] = {
	&ndtest_region_attribute_group,
	NULL,
};

static int ndtest_create_region(struct ndtest_priv *p,
				struct ndtest_region *region)
{
	struct nd_mapping_desc mappings[NDTEST_MAX_MAPPING];
	struct nd_region_desc *ndr_desc, _ndr_desc;
	struct nd_interleave_set *nd_set;
	struct resource res;
	int i, ndimm = region->mapping[0].dimm;
	u64 uuid[2];

	memset(&res, 0, sizeof(res));
	memset(&mappings, 0, sizeof(mappings));
	memset(&_ndr_desc, 0, sizeof(_ndr_desc));
	ndr_desc = &_ndr_desc;

	if (!ndtest_alloc_resource(p, region->size, &res.start))
		return -ENOMEM;

	res.end = res.start + region->size - 1;
	ndr_desc->mapping = mappings;
	ndr_desc->res = &res;
	ndr_desc->provider_data = region;
	ndr_desc->attr_groups = ndtest_region_attribute_groups;

	if (uuid_parse(p->config->dimms[ndimm].uuid_str, (uuid_t *)uuid)) {
		pr_err("failed to parse UUID\n");
		return -ENXIO;
	}

	nd_set = devm_kzalloc(&p->pdev.dev, sizeof(*nd_set), GFP_KERNEL);
	if (!nd_set)
		return -ENOMEM;

	nd_set->cookie1 = cpu_to_le64(uuid[0]);
	nd_set->cookie2 = cpu_to_le64(uuid[1]);
	nd_set->altcookie = nd_set->cookie1;
	ndr_desc->nd_set = nd_set;

	for (i = 0; i < region->num_mappings; i++) {
		ndimm = region->mapping[i].dimm;
		mappings[i].start = region->mapping[i].start;
		mappings[i].size = region->mapping[i].size;
		mappings[i].position = region->mapping[i].position;
		mappings[i].nvdimm = p->config->dimms[ndimm].nvdimm;
	}

	ndr_desc->num_mappings = region->num_mappings;
	region->region = nvdimm_pmem_region_create(p->bus, ndr_desc);

	if (!region->region) {
		dev_err(&p->pdev.dev, "Error registering region %pR\n",
			ndr_desc->res);
		return -ENXIO;
	}

	return 0;
}

static int ndtest_init_regions(struct ndtest_priv *p)
{
	int i, ret = 0;

	for (i = 0; i < p->config->num_regions; i++) {
		ret = ndtest_create_region(p, &p->config->regions[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static void put_dimms(void *data)
{
	struct ndtest_priv *p = data;
	int i;

	for (i = 0; i < p->config->dimm_count; i++)
		if (p->config->dimms[i].dev) {
			device_unregister(p->config->dimms[i].dev);
			p->config->dimms[i].dev = NULL;
		}
}

static ssize_t handle_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct ndtest_dimm *dimm = dev_get_drvdata(dev);

	return sprintf(buf, "%#x\n", dimm->handle);
}
static DEVICE_ATTR_RO(handle);

static ssize_t fail_cmd_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct ndtest_dimm *dimm = dev_get_drvdata(dev);

	return sprintf(buf, "%#x\n", dimm->fail_cmd);
}

static ssize_t fail_cmd_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct ndtest_dimm *dimm = dev_get_drvdata(dev);
	unsigned long val;
	ssize_t rc;

	rc = kstrtol(buf, 0, &val);
	if (rc)
		return rc;

	dimm->fail_cmd = val;

	return size;
}
static DEVICE_ATTR_RW(fail_cmd);

static ssize_t fail_cmd_code_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct ndtest_dimm *dimm = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", dimm->fail_cmd_code);
}

static ssize_t fail_cmd_code_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct ndtest_dimm *dimm = dev_get_drvdata(dev);
	unsigned long val;
	ssize_t rc;

	rc = kstrtol(buf, 0, &val);
	if (rc)
		return rc;

	dimm->fail_cmd_code = val;
	return size;
}
static DEVICE_ATTR_RW(fail_cmd_code);

static struct attribute *dimm_attributes[] = {
	&dev_attr_handle.attr,
	&dev_attr_fail_cmd.attr,
	&dev_attr_fail_cmd_code.attr,
	NULL,
};

static struct attribute_group dimm_attribute_group = {
	.attrs = dimm_attributes,
};

static const struct attribute_group *dimm_attribute_groups[] = {
	&dimm_attribute_group,
	NULL,
};

static ssize_t phys_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct ndtest_dimm *dimm = nvdimm_provider_data(nvdimm);

	return sprintf(buf, "%#x\n", dimm->physical_id);
}
static DEVICE_ATTR_RO(phys_id);

static ssize_t vendor_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0x1234567\n");
}
static DEVICE_ATTR_RO(vendor);

static ssize_t id_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct ndtest_dimm *dimm = nvdimm_provider_data(nvdimm);

	return sprintf(buf, "%04x-%02x-%04x-%08x", 0xabcd,
		       0xa, 2016, ~(dimm->handle));
}
static DEVICE_ATTR_RO(id);

static ssize_t nvdimm_handle_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct ndtest_dimm *dimm = nvdimm_provider_data(nvdimm);

	return sprintf(buf, "%#x\n", dimm->handle);
}

static struct device_attribute dev_attr_nvdimm_show_handle =  {
	.attr	= { .name = "handle", .mode = 0444 },
	.show	= nvdimm_handle_show,
};

static ssize_t subsystem_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%04x\n", 0);
}
static DEVICE_ATTR_RO(subsystem_vendor);

static ssize_t dirty_shutdown_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", 42);
}
static DEVICE_ATTR_RO(dirty_shutdown);

static ssize_t formats_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct ndtest_dimm *dimm = nvdimm_provider_data(nvdimm);

	return sprintf(buf, "%d\n", dimm->num_formats);
}
static DEVICE_ATTR_RO(formats);

static ssize_t format_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct ndtest_dimm *dimm = nvdimm_provider_data(nvdimm);

	if (dimm->num_formats > 1)
		return sprintf(buf, "0x201\n");

	return sprintf(buf, "0x101\n");
}
static DEVICE_ATTR_RO(format);

static ssize_t format1_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "0x301\n");
}
static DEVICE_ATTR_RO(format1);

static umode_t ndtest_nvdimm_attr_visible(struct kobject *kobj,
					struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct ndtest_dimm *dimm = nvdimm_provider_data(nvdimm);

	if (a == &dev_attr_format1.attr && dimm->num_formats <= 1)
		return 0;

	return a->mode;
}

static ssize_t flags_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct nvdimm *nvdimm = to_nvdimm(dev);
	struct ndtest_dimm *dimm = nvdimm_provider_data(nvdimm);
	struct seq_buf s;
	u64 flags;

	flags = dimm->flags;

	seq_buf_init(&s, buf, PAGE_SIZE);
	if (flags & PAPR_PMEM_UNARMED_MASK)
		seq_buf_printf(&s, "not_armed ");

	if (flags & PAPR_PMEM_BAD_SHUTDOWN_MASK)
		seq_buf_printf(&s, "flush_fail ");

	if (flags & PAPR_PMEM_BAD_RESTORE_MASK)
		seq_buf_printf(&s, "restore_fail ");

	if (flags & PAPR_PMEM_SAVE_MASK)
		seq_buf_printf(&s, "save_fail ");

	if (flags & PAPR_PMEM_SMART_EVENT_MASK)
		seq_buf_printf(&s, "smart_notify ");


	if (seq_buf_used(&s))
		seq_buf_printf(&s, "\n");

	return seq_buf_used(&s);
}
static DEVICE_ATTR_RO(flags);

static struct attribute *ndtest_nvdimm_attributes[] = {
	&dev_attr_nvdimm_show_handle.attr,
	&dev_attr_vendor.attr,
	&dev_attr_id.attr,
	&dev_attr_phys_id.attr,
	&dev_attr_subsystem_vendor.attr,
	&dev_attr_dirty_shutdown.attr,
	&dev_attr_formats.attr,
	&dev_attr_format.attr,
	&dev_attr_format1.attr,
	&dev_attr_flags.attr,
	NULL,
};

static const struct attribute_group ndtest_nvdimm_attribute_group = {
	.name = "papr",
	.attrs = ndtest_nvdimm_attributes,
	.is_visible = ndtest_nvdimm_attr_visible,
};

static const struct attribute_group *ndtest_nvdimm_attribute_groups[] = {
	&ndtest_nvdimm_attribute_group,
	NULL,
};

static int ndtest_dimm_register(struct ndtest_priv *priv,
				struct ndtest_dimm *dimm, int id)
{
	struct device *dev = &priv->pdev.dev;
	unsigned long dimm_flags = dimm->flags;

	if (dimm->num_formats > 1)
		set_bit(NDD_LABELING, &dimm_flags);

	if (dimm->flags & PAPR_PMEM_UNARMED_MASK)
		set_bit(NDD_UNARMED, &dimm_flags);

	dimm->nvdimm = nvdimm_create(priv->bus, dimm,
				    ndtest_nvdimm_attribute_groups, dimm_flags,
				    NDTEST_SCM_DIMM_CMD_MASK, 0, NULL);
	if (!dimm->nvdimm) {
		dev_err(dev, "Error creating DIMM object for %pOF\n", priv->dn);
		return -ENXIO;
	}

	dimm->dev = device_create_with_groups(&ndtest_dimm_class,
					     &priv->pdev.dev,
					     0, dimm, dimm_attribute_groups,
					     "test_dimm%d", id);
	if (!dimm->dev) {
		pr_err("Could not create dimm device attributes\n");
		return -ENOMEM;
	}

	return 0;
}

static int ndtest_nvdimm_init(struct ndtest_priv *p)
{
	struct ndtest_dimm *d;
	void *res;
	int i, id;

	for (i = 0; i < p->config->dimm_count; i++) {
		d = &p->config->dimms[i];
		d->id = id = p->config->dimm_start + i;
		res = ndtest_alloc_resource(p, LABEL_SIZE, NULL);
		if (!res)
			return -ENOMEM;

		d->label_area = res;
		sprintf(d->label_area, "label%d", id);
		d->config_size = LABEL_SIZE;

		if (!ndtest_alloc_resource(p, d->size,
					   &p->dimm_dma[id]))
			return -ENOMEM;

		if (!ndtest_alloc_resource(p, LABEL_SIZE,
					   &p->label_dma[id]))
			return -ENOMEM;

		if (!ndtest_alloc_resource(p, LABEL_SIZE,
					   &p->dcr_dma[id]))
			return -ENOMEM;

		d->address = p->dimm_dma[id];

		ndtest_dimm_register(p, d, id);
	}

	return 0;
}

static ssize_t compatible_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "nvdimm_test");
}
static DEVICE_ATTR_RO(compatible);

static struct attribute *of_node_attributes[] = {
	&dev_attr_compatible.attr,
	NULL
};

static const struct attribute_group of_node_attribute_group = {
	.name = "of_node",
	.attrs = of_node_attributes,
};

static const struct attribute_group *ndtest_attribute_groups[] = {
	&of_node_attribute_group,
	NULL,
};

static int ndtest_bus_register(struct ndtest_priv *p)
{
	p->config = &bus_configs[p->pdev.id];

	p->bus_desc.ndctl = ndtest_ctl;
	p->bus_desc.module = THIS_MODULE;
	p->bus_desc.provider_name = NULL;
	p->bus_desc.attr_groups = ndtest_attribute_groups;

	p->bus = nvdimm_bus_register(&p->pdev.dev, &p->bus_desc);
	if (!p->bus) {
		dev_err(&p->pdev.dev, "Error creating nvdimm bus %pOF\n", p->dn);
		return -ENOMEM;
	}

	return 0;
}

static int ndtest_remove(struct platform_device *pdev)
{
	struct ndtest_priv *p = to_ndtest_priv(&pdev->dev);

	nvdimm_bus_unregister(p->bus);
	return 0;
}

static int ndtest_probe(struct platform_device *pdev)
{
	struct ndtest_priv *p;
	int rc;

	p = to_ndtest_priv(&pdev->dev);
	if (ndtest_bus_register(p))
		return -ENOMEM;

	p->dcr_dma = devm_kcalloc(&p->pdev.dev, NUM_DCR,
				 sizeof(dma_addr_t), GFP_KERNEL);
	p->label_dma = devm_kcalloc(&p->pdev.dev, NUM_DCR,
				   sizeof(dma_addr_t), GFP_KERNEL);
	p->dimm_dma = devm_kcalloc(&p->pdev.dev, NUM_DCR,
				  sizeof(dma_addr_t), GFP_KERNEL);

	rc = ndtest_nvdimm_init(p);
	if (rc)
		goto err;

	rc = ndtest_init_regions(p);
	if (rc)
		goto err;

	rc = devm_add_action_or_reset(&pdev->dev, put_dimms, p);
	if (rc)
		goto err;

	platform_set_drvdata(pdev, p);

	return 0;

err:
	pr_err("%s:%d Failed nvdimm init\n", __func__, __LINE__);
	return rc;
}

static const struct platform_device_id ndtest_id[] = {
	{ KBUILD_MODNAME },
	{ },
};

static struct platform_driver ndtest_driver = {
	.probe = ndtest_probe,
	.remove = ndtest_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	},
	.id_table = ndtest_id,
};

static void ndtest_release(struct device *dev)
{
	struct ndtest_priv *p = to_ndtest_priv(dev);

	kfree(p);
}

static void cleanup_devices(void)
{
	int i;

	for (i = 0; i < NUM_INSTANCES; i++)
		if (instances[i])
			platform_device_unregister(&instances[i]->pdev);

	nfit_test_teardown();

	if (ndtest_pool)
		gen_pool_destroy(ndtest_pool);


	class_unregister(&ndtest_dimm_class);
}

static __init int ndtest_init(void)
{
	int rc, i;

	pmem_test();
	libnvdimm_test();
	device_dax_test();
	dax_pmem_test();

	nfit_test_setup(ndtest_resource_lookup, NULL);

	rc = class_register(&ndtest_dimm_class);
	if (rc)
		goto err_register;

	ndtest_pool = gen_pool_create(ilog2(SZ_4M), NUMA_NO_NODE);
	if (!ndtest_pool) {
		rc = -ENOMEM;
		goto err_register;
	}

	if (gen_pool_add(ndtest_pool, SZ_4G, SZ_4G, NUMA_NO_NODE)) {
		rc = -ENOMEM;
		goto err_register;
	}

	/* Each instance can be taken as a bus, which can have multiple dimms */
	for (i = 0; i < NUM_INSTANCES; i++) {
		struct ndtest_priv *priv;
		struct platform_device *pdev;

		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (!priv) {
			rc = -ENOMEM;
			goto err_register;
		}

		INIT_LIST_HEAD(&priv->resources);
		pdev = &priv->pdev;
		pdev->name = KBUILD_MODNAME;
		pdev->id = i;
		pdev->dev.release = ndtest_release;
		rc = platform_device_register(pdev);
		if (rc) {
			put_device(&pdev->dev);
			goto err_register;
		}
		get_device(&pdev->dev);

		instances[i] = priv;
	}

	rc = platform_driver_register(&ndtest_driver);
	if (rc)
		goto err_register;

	return 0;

err_register:
	pr_err("Error registering platform device\n");
	cleanup_devices();

	return rc;
}

static __exit void ndtest_exit(void)
{
	cleanup_devices();
	platform_driver_unregister(&ndtest_driver);
}

module_init(ndtest_init);
module_exit(ndtest_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
