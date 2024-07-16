// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022, The Linux Foundation. All rights reserved.

#include <linux/export.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include "lpass-macro-common.h"

struct lpass_macro *lpass_macro_pds_init(struct device *dev)
{
	struct lpass_macro *l_pds;
	int ret;

	if (!of_find_property(dev->of_node, "power-domains", NULL))
		return NULL;

	l_pds = devm_kzalloc(dev, sizeof(*l_pds), GFP_KERNEL);
	if (!l_pds)
		return ERR_PTR(-ENOMEM);

	l_pds->macro_pd = dev_pm_domain_attach_by_name(dev, "macro");
	if (IS_ERR_OR_NULL(l_pds->macro_pd)) {
		ret = l_pds->macro_pd ? PTR_ERR(l_pds->macro_pd) : -ENODATA;
		goto macro_err;
	}

	ret = pm_runtime_resume_and_get(l_pds->macro_pd);
	if (ret < 0)
		goto macro_sync_err;

	l_pds->dcodec_pd = dev_pm_domain_attach_by_name(dev, "dcodec");
	if (IS_ERR_OR_NULL(l_pds->dcodec_pd)) {
		ret = l_pds->dcodec_pd ? PTR_ERR(l_pds->dcodec_pd) : -ENODATA;
		goto dcodec_err;
	}

	ret = pm_runtime_resume_and_get(l_pds->dcodec_pd);
	if (ret < 0)
		goto dcodec_sync_err;
	return l_pds;

dcodec_sync_err:
	dev_pm_domain_detach(l_pds->dcodec_pd, false);
dcodec_err:
	pm_runtime_put(l_pds->macro_pd);
macro_sync_err:
	dev_pm_domain_detach(l_pds->macro_pd, false);
macro_err:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(lpass_macro_pds_init);

void lpass_macro_pds_exit(struct lpass_macro *pds)
{
	if (pds) {
		pm_runtime_put(pds->macro_pd);
		dev_pm_domain_detach(pds->macro_pd, false);
		pm_runtime_put(pds->dcodec_pd);
		dev_pm_domain_detach(pds->dcodec_pd, false);
	}
}
EXPORT_SYMBOL_GPL(lpass_macro_pds_exit);

MODULE_DESCRIPTION("Common macro driver");
MODULE_LICENSE("GPL");
