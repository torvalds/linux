// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select -- Enumerate and control features for Mailbox Interface
 * Copyright (c) 2023 Intel Corporation.
 */
#include "isst.h"

static int mbox_get_disp_freq_multiplier(void)
{
        return DISP_FREQ_MULTIPLIER;
}

static int mbox_get_trl_max_levels(void)
{
        return 3;
}

static char *mbox_get_trl_level_name(int level)
{
        switch (level) {
        case 0:
                return "sse";
        case 1:
                return "avx2";
        case 2:
                return "avx512";
        default:
                return NULL;
        }
}

static int mbox_is_punit_valid(struct isst_id *id)
{
	if (id->cpu < 0)
		return 0;

	if (id->pkg < 0 || id->die < 0 || id->punit)
		return 0;

	return 1;
}

static int mbox_get_config_levels(struct isst_id *id, struct isst_pkg_ctdp *pkg_dev)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_LEVELS_INFO, 0, 0, &resp);
	if (ret) {
		pkg_dev->levels = 0;
		pkg_dev->locked = 1;
		pkg_dev->current_level = 0;
		pkg_dev->version = 0;
		pkg_dev->enabled = 0;
		return 0;
	}

	debug_printf("cpu:%d CONFIG_TDP_GET_LEVELS_INFO resp:%x\n", id->cpu, resp);

	pkg_dev->version = resp & 0xff;
	pkg_dev->levels = (resp >> 8) & 0xff;
	pkg_dev->current_level = (resp >> 16) & 0xff;
	pkg_dev->locked = !!(resp & BIT(24));
	pkg_dev->enabled = !!(resp & BIT(31));

	return 0;
}

static int mbox_get_ctdp_control(struct isst_id *id, int config_index,
			  struct isst_pkg_ctdp_level_info *ctdp_level)
{
	int cp_state, cp_cap;
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_TDP_CONTROL, 0,
				     config_index, &resp);
	if (ret)
		return ret;

	ctdp_level->fact_support = resp & BIT(0);
	ctdp_level->pbf_support = !!(resp & BIT(1));
	ctdp_level->fact_enabled = !!(resp & BIT(16));
	ctdp_level->pbf_enabled = !!(resp & BIT(17));

	ret = isst_read_pm_config(id, &cp_state, &cp_cap);
	if (ret) {
		debug_printf("cpu:%d pm_config is not supported\n", id->cpu);
	} else {
		debug_printf("cpu:%d pm_config SST-CP state:%d cap:%d\n", id->cpu, cp_state, cp_cap);
		ctdp_level->sst_cp_support = cp_cap;
		ctdp_level->sst_cp_enabled = cp_state;
	}

	debug_printf(
		"cpu:%d CONFIG_TDP_GET_TDP_CONTROL resp:%x fact_support:%d pbf_support: %d fact_enabled:%d pbf_enabled:%d\n",
		id->cpu, resp, ctdp_level->fact_support, ctdp_level->pbf_support,
		ctdp_level->fact_enabled, ctdp_level->pbf_enabled);

	return 0;
}

static int mbox_get_tdp_info(struct isst_id *id, int config_index,
		      struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP, CONFIG_TDP_GET_TDP_INFO,
				     0, config_index, &resp);
	if (ret) {
		isst_display_error_info_message(1, "Invalid level, Can't get TDP information at level", 1, config_index);
		return ret;
	}

	ctdp_level->pkg_tdp = resp & GENMASK(14, 0);
	ctdp_level->tdp_ratio = (resp & GENMASK(23, 16)) >> 16;

	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_TDP_INFO resp:%x tdp_ratio:%d pkg_tdp:%d\n",
		id->cpu, config_index, resp, ctdp_level->tdp_ratio,
		ctdp_level->pkg_tdp);

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP, CONFIG_TDP_GET_TJMAX_INFO,
				     0, config_index, &resp);
	if (ret)
		return ret;

	ctdp_level->t_proc_hot = resp & GENMASK(7, 0);

	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_TJMAX_INFO resp:%x t_proc_hot:%d\n",
		id->cpu, config_index, resp, ctdp_level->t_proc_hot);

	return 0;
}

static int mbox_get_pwr_info(struct isst_id *id, int config_index,
		      struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP, CONFIG_TDP_GET_PWR_INFO,
				     0, config_index, &resp);
	if (ret)
		return ret;

	ctdp_level->pkg_max_power = resp & GENMASK(14, 0);
	ctdp_level->pkg_min_power = (resp & GENMASK(30, 16)) >> 16;

	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_PWR_INFO resp:%x pkg_max_power:%d pkg_min_power:%d\n",
		id->cpu, config_index, resp, ctdp_level->pkg_max_power,
		ctdp_level->pkg_min_power);

	return 0;
}

static int mbox_get_coremask_info(struct isst_id *id, int config_index,
			   struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int i, ret;

	ctdp_level->cpu_count = 0;
	for (i = 0; i < 2; ++i) {
		unsigned long long mask;
		int cpu_count = 0;

		ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
					     CONFIG_TDP_GET_CORE_MASK, 0,
					     (i << 8) | config_index, &resp);
		if (ret)
			return ret;

		debug_printf(
			"cpu:%d ctdp:%d mask:%d CONFIG_TDP_GET_CORE_MASK resp:%x\n",
			id->cpu, config_index, i, resp);

		mask = (unsigned long long)resp << (32 * i);
		set_cpu_mask_from_punit_coremask(id, mask,
						 ctdp_level->core_cpumask_size,
						 ctdp_level->core_cpumask,
						 &cpu_count);
		ctdp_level->cpu_count += cpu_count;
		debug_printf("cpu:%d ctdp:%d mask:%d cpu count:%d\n", id->cpu,
			     config_index, i, ctdp_level->cpu_count);
	}

	return 0;
}

static int mbox_get_get_trl(struct isst_id *id, int level, int avx_level, int *trl)
{
	unsigned int req, resp;
	int ret;

	req = level | (avx_level << 16);
	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_TURBO_LIMIT_RATIOS, 0, req,
				     &resp);
	if (ret)
		return ret;

	debug_printf(
		"cpu:%d CONFIG_TDP_GET_TURBO_LIMIT_RATIOS req:%x resp:%x\n",
		id->cpu, req, resp);

	trl[0] = resp & GENMASK(7, 0);
	trl[1] = (resp & GENMASK(15, 8)) >> 8;
	trl[2] = (resp & GENMASK(23, 16)) >> 16;
	trl[3] = (resp & GENMASK(31, 24)) >> 24;

	req = level | BIT(8) | (avx_level << 16);
	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_TURBO_LIMIT_RATIOS, 0, req,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_GET_TURBO_LIMIT req:%x resp:%x\n", id->cpu,
		     req, resp);

	trl[4] = resp & GENMASK(7, 0);
	trl[5] = (resp & GENMASK(15, 8)) >> 8;
	trl[6] = (resp & GENMASK(23, 16)) >> 16;
	trl[7] = (resp & GENMASK(31, 24)) >> 24;

	return 0;
}

static int mbox_get_trl_bucket_info(struct isst_id *id, int level, unsigned long long *buckets_info)
{
	int ret;

	debug_printf("cpu:%d bucket info via MSR\n", id->cpu);

	*buckets_info = 0;

	ret = isst_send_msr_command(id->cpu, 0x1ae, 0, buckets_info);
	if (ret)
		return ret;

	debug_printf("cpu:%d bucket info via MSR successful 0x%llx\n", id->cpu,
		     *buckets_info);

	return 0;
}

static int mbox_set_tdp_level(struct isst_id *id, int tdp_level)
{
	unsigned int resp;
	int ret;


	if (isst_get_config_tdp_lock_status(id)) {
		isst_display_error_info_message(1, "TDP is locked", 0, 0);
		return -1;

	}

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP, CONFIG_TDP_SET_LEVEL, 0,
				     tdp_level, &resp);
	if (ret) {
		isst_display_error_info_message(1, "Set TDP level failed for level", 1, tdp_level);
		return ret;
	}

	return 0;
}

static struct isst_platform_ops mbox_ops = {
	.get_disp_freq_multiplier = mbox_get_disp_freq_multiplier,
	.get_trl_max_levels = mbox_get_trl_max_levels,
	.get_trl_level_name = mbox_get_trl_level_name,
	.is_punit_valid = mbox_is_punit_valid,
	.get_config_levels = mbox_get_config_levels,
	.get_ctdp_control = mbox_get_ctdp_control,
	.get_tdp_info = mbox_get_tdp_info,
	.get_pwr_info = mbox_get_pwr_info,
	.get_coremask_info = mbox_get_coremask_info,
	.get_get_trl = mbox_get_get_trl,
	.get_trl_bucket_info = mbox_get_trl_bucket_info,
	.set_tdp_level = mbox_set_tdp_level,
};

struct isst_platform_ops *mbox_get_platform_ops(void)
{
	return &mbox_ops;
}
