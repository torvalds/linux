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
};

static struct ndtest_priv *instances[NUM_INSTANCES];
static struct class *ndtest_dimm_class;

static inline struct ndtest_priv *to_ndtest_priv(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	return container_of(pdev, struct ndtest_priv, pdev);
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
	case ND_CMD_GET_CONFIG_DATA:
	case ND_CMD_SET_CONFIG_DATA:
	default:
		return -EINVAL;
	}

	return 0;
}

static int ndtest_bus_register(struct ndtest_priv *p)
{
	p->bus_desc.ndctl = ndtest_ctl;
	p->bus_desc.module = THIS_MODULE;
	p->bus_desc.provider_name = NULL;

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

	p = to_ndtest_priv(&pdev->dev);
	if (ndtest_bus_register(p))
		return -ENOMEM;

	platform_set_drvdata(pdev, p);

	return 0;
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

	if (ndtest_dimm_class)
		class_destroy(ndtest_dimm_class);
}

static __init int ndtest_init(void)
{
	int rc, i;

	pmem_test();
	libnvdimm_test();
	device_dax_test();
	dax_pmem_test();
	dax_pmem_core_test();
#ifdef CONFIG_DEV_DAX_PMEM_COMPAT
	dax_pmem_compat_test();
#endif

	ndtest_dimm_class = class_create(THIS_MODULE, "nfit_test_dimm");
	if (IS_ERR(ndtest_dimm_class)) {
		rc = PTR_ERR(ndtest_dimm_class);
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
