// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <cxl.h>
#include "test/mock.h"
#include <core/core.h>

int match_nvdimm_bridge(struct device *dev, const void *data)
{
	int index, rc = 0;
	struct cxl_mock_ops *ops = get_cxl_mock_ops(&index);
	const struct cxl_nvdimm *cxl_nvd = data;

	if (ops) {
		if (dev->type == &cxl_nvdimm_bridge_type &&
		    (ops->is_mock_dev(dev->parent->parent) ==
		     ops->is_mock_dev(cxl_nvd->dev.parent->parent)))
			rc = 1;
	} else
		rc = dev->type == &cxl_nvdimm_bridge_type;

	put_cxl_mock_ops(index);

	return rc;
}
