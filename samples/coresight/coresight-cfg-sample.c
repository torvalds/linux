// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2020 Linaro Limited. All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include "coresight-config.h"
#include "coresight-syscfg.h"

/* create an alternate autofdo configuration */

/* we will provide 4 sets of preset parameter values */
#define AFDO2_NR_PRESETS	4
/* the total number of parameters in used features - strobing has 2 */
#define AFDO2_NR_PARAM_SUM	2

static const char *afdo2_ref_names[] = {
	"strobing",
};

/*
 * set of presets leaves strobing window constant while varying period to allow
 * experimentation with mark / space ratios for various workloads
 */
static u64 afdo2_presets[AFDO2_NR_PRESETS][AFDO2_NR_PARAM_SUM] = {
	{ 1000, 100 },
	{ 1000, 1000 },
	{ 1000, 5000 },
	{ 1000, 10000 },
};

struct cscfg_config_desc afdo2 = {
	.name = "autofdo2",
	.description = "Setup ETMs with strobing for autofdo\n"
	"Supplied presets allow experimentation with mark-space ratio for various loads\n",
	.nr_feat_refs = ARRAY_SIZE(afdo2_ref_names),
	.feat_ref_names = afdo2_ref_names,
	.nr_presets = AFDO2_NR_PRESETS,
	.nr_total_params = AFDO2_NR_PARAM_SUM,
	.presets = &afdo2_presets[0][0],
};

static struct cscfg_feature_desc *sample_feats[] = {
	NULL
};

static struct cscfg_config_desc *sample_cfgs[] = {
	&afdo2,
	NULL
};

static struct cscfg_load_owner_info mod_owner = {
	.type = CSCFG_OWNER_MODULE,
	.owner_handle = THIS_MODULE,
};

/* module init and exit - just load and unload configs */
static int __init cscfg_sample_init(void)
{
	return cscfg_load_config_sets(sample_cfgs, sample_feats, &mod_owner);
}

static void __exit cscfg_sample_exit(void)
{
	cscfg_unload_config_sets(&mod_owner);
}

module_init(cscfg_sample_init);
module_exit(cscfg_sample_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Mike Leach <mike.leach@linaro.org>");
MODULE_DESCRIPTION("CoreSight Syscfg Example");
