// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <drm/drm_print.h>

#include "gt/intel_gt_debugfs.h"
#include "gt/uc/intel_guc_ads.h"
#include "gt/uc/intel_guc_ct.h"
#include "gt/uc/intel_guc_slpc.h"
#include "gt/uc/intel_guc_submission.h"
#include "intel_guc.h"
#include "intel_guc_debugfs.h"
#include "intel_guc_log_debugfs.h"

#ifdef notyet

static int guc_info_show(struct seq_file *m, void *data)
{
	struct intel_guc *guc = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	if (!intel_guc_is_supported(guc))
		return -ENODEV;

	intel_guc_load_status(guc, &p);
	drm_puts(&p, "\n");
	intel_guc_log_info(&guc->log, &p);

	if (!intel_guc_submission_is_used(guc))
		return 0;

	intel_guc_ct_print_info(&guc->ct, &p);
	intel_guc_submission_print_info(guc, &p);
	intel_guc_ads_print_policy_info(guc, &p);

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(guc_info);

static int guc_registered_contexts_show(struct seq_file *m, void *data)
{
	struct intel_guc *guc = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	if (!intel_guc_submission_is_used(guc))
		return -ENODEV;

	intel_guc_submission_print_context_info(guc, &p);

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(guc_registered_contexts);

static int guc_slpc_info_show(struct seq_file *m, void *unused)
{
	struct intel_guc *guc = m->private;
	struct intel_guc_slpc *slpc = &guc->slpc;
	struct drm_printer p = drm_seq_file_printer(m);

	if (!intel_guc_slpc_is_used(guc))
		return -ENODEV;

	return intel_guc_slpc_print_info(slpc, &p);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(guc_slpc_info);

static bool intel_eval_slpc_support(void *data)
{
	struct intel_guc *guc = (struct intel_guc *)data;

	return intel_guc_slpc_is_used(guc);
}

static int guc_sched_disable_delay_ms_get(void *data, u64 *val)
{
	struct intel_guc *guc = data;

	if (!intel_guc_submission_is_used(guc))
		return -ENODEV;

	*val = (u64)guc->submission_state.sched_disable_delay_ms;

	return 0;
}

static int guc_sched_disable_delay_ms_set(void *data, u64 val)
{
	struct intel_guc *guc = data;

	if (!intel_guc_submission_is_used(guc))
		return -ENODEV;

	/* clamp to a practical limit, 1 minute is reasonable for a longest delay */
	guc->submission_state.sched_disable_delay_ms = min_t(u64, val, 60000);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(guc_sched_disable_delay_ms_fops,
			guc_sched_disable_delay_ms_get,
			guc_sched_disable_delay_ms_set, "%lld\n");

static int guc_sched_disable_gucid_threshold_get(void *data, u64 *val)
{
	struct intel_guc *guc = data;

	if (!intel_guc_submission_is_used(guc))
		return -ENODEV;

	*val = guc->submission_state.sched_disable_gucid_threshold;
	return 0;
}

static int guc_sched_disable_gucid_threshold_set(void *data, u64 val)
{
	struct intel_guc *guc = data;

	if (!intel_guc_submission_is_used(guc))
		return -ENODEV;

	if (val > intel_guc_sched_disable_gucid_threshold_max(guc))
		guc->submission_state.sched_disable_gucid_threshold =
			intel_guc_sched_disable_gucid_threshold_max(guc);
	else
		guc->submission_state.sched_disable_gucid_threshold = val;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(guc_sched_disable_gucid_threshold_fops,
			guc_sched_disable_gucid_threshold_get,
			guc_sched_disable_gucid_threshold_set, "%lld\n");

#endif

void intel_guc_debugfs_register(struct intel_guc *guc, struct dentry *root)
{
	STUB();
#ifdef notyet
	static const struct intel_gt_debugfs_file files[] = {
		{ "guc_info", &guc_info_fops, NULL },
		{ "guc_registered_contexts", &guc_registered_contexts_fops, NULL },
		{ "guc_slpc_info", &guc_slpc_info_fops, &intel_eval_slpc_support},
		{ "guc_sched_disable_delay_ms", &guc_sched_disable_delay_ms_fops, NULL },
		{ "guc_sched_disable_gucid_threshold", &guc_sched_disable_gucid_threshold_fops,
		   NULL },
	};

	if (!intel_guc_is_supported(guc))
		return;

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), guc);
	intel_guc_log_debugfs_register(&guc->log, root);
#endif
}
