// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <drm/drm_print.h>

#include "gt/intel_gt_debugfs.h"
#include "intel_huc.h"
#include "intel_huc_debugfs.h"

#ifdef notyet

static int huc_info_show(struct seq_file *m, void *data)
{
	struct intel_huc *huc = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	if (!intel_huc_is_supported(huc))
		return -ENODEV;

	intel_huc_load_status(huc, &p);

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(huc_info);

#endif

void intel_huc_debugfs_register(struct intel_huc *huc, struct dentry *root)
{
	STUB();
#ifdef notyet
	static const struct intel_gt_debugfs_file files[] = {
		{ "huc_info", &huc_info_fops, NULL },
	};

	if (!intel_huc_is_supported(huc))
		return;

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), huc);
#endif
}
