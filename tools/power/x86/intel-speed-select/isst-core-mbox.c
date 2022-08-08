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

static void mbox_get_uncore_p0_p1_info(struct isst_id *id, int config_index,
				struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;

	ctdp_level->uncore_pm = 0;
	ctdp_level->uncore_p0 = 0;
	ctdp_level->uncore_p1 = 0;

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_RATIO_INFO, 0,
				     (BIT(16) | config_index) , &resp);
	if (ret) {
		goto try_uncore_mbox;
	}

	ctdp_level->uncore_p0 = resp & GENMASK(7, 0);
	ctdp_level->uncore_p1 = (resp & GENMASK(15, 8)) >> 8;
	ctdp_level->uncore_pm = (resp & GENMASK(31, 24)) >> 24;

	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_RATIO_INFO resp:%x uncore p0:%d uncore p1:%d uncore pm:%d\n",
		id->cpu, config_index, resp, ctdp_level->uncore_p0, ctdp_level->uncore_p1,
		ctdp_level->uncore_pm);

	return;

try_uncore_mbox:
	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_UNCORE_P0_P1_INFO, 0,
				     config_index, &resp);
	if (ret) {
		ctdp_level->uncore_p0 = 0;
		ctdp_level->uncore_p1 = 0;
		return;
	}

	ctdp_level->uncore_p0 = resp & GENMASK(7, 0);
	ctdp_level->uncore_p1 = (resp & GENMASK(15, 8)) >> 8;
	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_UNCORE_P0_P1_INFO resp:%x uncore p0:%d uncore p1:%d\n",
		id->cpu, config_index, resp, ctdp_level->uncore_p0,
		ctdp_level->uncore_p1);
}

static void _get_p1_info(struct isst_id *id, int config_index,
		      struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;
	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP, CONFIG_TDP_GET_P1_INFO, 0,
				     config_index, &resp);
	if (ret) {
		ctdp_level->sse_p1 = 0;
		ctdp_level->avx2_p1 = 0;
		ctdp_level->avx512_p1 = 0;
		return;
	}

	ctdp_level->sse_p1 = resp & GENMASK(7, 0);
	ctdp_level->avx2_p1 = (resp & GENMASK(15, 8)) >> 8;
	ctdp_level->avx512_p1 = (resp & GENMASK(23, 16)) >> 16;
	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_P1_INFO resp:%x sse_p1:%d avx2_p1:%d avx512_p1:%d\n",
		id->cpu, config_index, resp, ctdp_level->sse_p1,
		ctdp_level->avx2_p1, ctdp_level->avx512_p1);
}

static void _get_uncore_mem_freq(struct isst_id *id, int config_index,
			      struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP, CONFIG_TDP_GET_MEM_FREQ,
				     0, config_index, &resp);
	if (ret) {
		ctdp_level->mem_freq = 0;
		return;
	}

	ctdp_level->mem_freq = resp & GENMASK(7, 0);
	if (is_spr_platform()) {
		ctdp_level->mem_freq *= 200;
	} else if (is_icx_platform()) {
		if (ctdp_level->mem_freq < 7) {
			ctdp_level->mem_freq = (12 - ctdp_level->mem_freq) * 133.33 * 2 * 10;
			ctdp_level->mem_freq /= 10;
			if (ctdp_level->mem_freq % 10 > 5)
				ctdp_level->mem_freq++;
		} else {
			ctdp_level->mem_freq = 0;
		}
	} else {
		ctdp_level->mem_freq = 0;
	}
	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_MEM_FREQ resp:%x uncore mem_freq:%d\n",
		id->cpu, config_index, resp, ctdp_level->mem_freq);
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

	mbox_get_uncore_p0_p1_info(id, config_index, ctdp_level);
	_get_p1_info(id, config_index, ctdp_level);
	_get_uncore_mem_freq(id, config_index, ctdp_level);

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

static int mbox_get_get_trls(struct isst_id *id, int level, struct isst_pkg_ctdp_level_info *ctdp_level)
{
	int trl_max_levels = isst_get_trl_max_levels();
	int i, ret;

	for (i = 0; i < trl_max_levels; i++) {
		ret = mbox_get_get_trl(id, level, i, ctdp_level->trl_ratios[i]);
		if (ret)
			return ret;
	}
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

static int mbox_get_pbf_info(struct isst_id *id, int level, struct isst_pbf_info *pbf_info)
{
	int max_punit_core, max_mask_index;
	unsigned int req, resp;
	int i, ret;

	max_punit_core = get_max_punit_core_id(id);
	max_mask_index = max_punit_core > 32 ? 2 : 1;

	for (i = 0; i < max_mask_index; ++i) {
		unsigned long long mask;
		int count;

		ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
					     CONFIG_TDP_PBF_GET_CORE_MASK_INFO,
					     0, (i << 8) | level, &resp);
		if (ret)
			break;

		debug_printf(
			"cpu:%d CONFIG_TDP_PBF_GET_CORE_MASK_INFO resp:%x\n",
			id->cpu, resp);

		mask = (unsigned long long)resp << (32 * i);
		set_cpu_mask_from_punit_coremask(id, mask,
						 pbf_info->core_cpumask_size,
						 pbf_info->core_cpumask,
						 &count);
	}

	req = level;
	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
				     CONFIG_TDP_PBF_GET_P1HI_P1LO_INFO, 0, req,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_PBF_GET_P1HI_P1LO_INFO resp:%x\n", id->cpu,
		     resp);

	pbf_info->p1_low = resp & 0xff;
	pbf_info->p1_high = (resp & GENMASK(15, 8)) >> 8;

	req = level;
	ret = isst_send_mbox_command(
		id->cpu, CONFIG_TDP, CONFIG_TDP_PBF_GET_TDP_INFO, 0, req, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_PBF_GET_TDP_INFO resp:%x\n", id->cpu, resp);

	pbf_info->tdp = resp & 0xffff;

	req = level;
	ret = isst_send_mbox_command(
		id->cpu, CONFIG_TDP, CONFIG_TDP_PBF_GET_TJ_MAX_INFO, 0, req, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_PBF_GET_TJ_MAX_INFO resp:%x\n", id->cpu,
		     resp);
	pbf_info->t_control = (resp >> 8) & 0xff;
	pbf_info->t_prochot = resp & 0xff;

	return 0;
}

static int mbox_set_pbf_fact_status(struct isst_id *id, int pbf, int enable)
{
	struct isst_pkg_ctdp pkg_dev;
	struct isst_pkg_ctdp_level_info ctdp_level;
	int current_level;
	unsigned int req = 0, resp;
	int ret;

	ret = isst_get_ctdp_levels(id, &pkg_dev);
	if (ret)
		debug_printf("cpu:%d No support for dynamic ISST\n", id->cpu);

	current_level = pkg_dev.current_level;

	ret = isst_get_ctdp_control(id, current_level, &ctdp_level);
	if (ret)
		return ret;

	if (pbf) {
		if (ctdp_level.fact_enabled)
			req = BIT(16);

		if (enable)
			req |= BIT(17);
		else
			req &= ~BIT(17);
	} else {

		if (enable && !ctdp_level.sst_cp_enabled)
			isst_display_error_info_message(0, "Make sure to execute before: core-power enable", 0, 0);

		if (ctdp_level.pbf_enabled)
			req = BIT(17);

		if (enable)
			req |= BIT(16);
		else
			req &= ~BIT(16);
	}

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
				     CONFIG_TDP_SET_TDP_CONTROL, 0, req, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_SET_TDP_CONTROL pbf/fact:%d req:%x\n",
		     id->cpu, pbf, req);

	return 0;
}

static int _get_fact_bucket_info(struct isst_id *id, int level,
			      struct isst_fact_bucket_info *bucket_info)
{
	unsigned int resp;
	int i, k, ret;

	for (i = 0; i < 2; ++i) {
		int j;

		ret = isst_send_mbox_command(
			id->cpu, CONFIG_TDP,
			CONFIG_TDP_GET_FACT_HP_TURBO_LIMIT_NUMCORES, 0,
			(i << 8) | level, &resp);
		if (ret)
			return ret;

		debug_printf(
			"cpu:%d CONFIG_TDP_GET_FACT_HP_TURBO_LIMIT_NUMCORES index:%d level:%d resp:%x\n",
			id->cpu, i, level, resp);

		for (j = 0; j < 4; ++j) {
			bucket_info[j + (i * 4)].hp_cores =
				(resp >> (j * 8)) & 0xff;
		}
	}

	for (k = 0; k < 3; ++k) {
		for (i = 0; i < 2; ++i) {
			int j;

			ret = isst_send_mbox_command(
				id->cpu, CONFIG_TDP,
				CONFIG_TDP_GET_FACT_HP_TURBO_LIMIT_RATIOS, 0,
				(k << 16) | (i << 8) | level, &resp);
			if (ret)
				return ret;

			debug_printf(
				"cpu:%d CONFIG_TDP_GET_FACT_HP_TURBO_LIMIT_RATIOS index:%d level:%d avx:%d resp:%x\n",
				id->cpu, i, level, k, resp);

			for (j = 0; j < 4; ++j) {
				bucket_info[j + (i * 4)].hp_ratios[k] =
					(resp >> (j * 8)) & 0xff;
			}
		}
	}

	return 0;
}

static int mbox_get_fact_info(struct isst_id *id, int level, int fact_bucket, struct isst_fact_info *fact_info)
{
	unsigned int resp;
	int j, ret, print;

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_FACT_LP_CLIPPING_RATIO, 0,
				     level, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_GET_FACT_LP_CLIPPING_RATIO resp:%x\n",
		     id->cpu, resp);

	fact_info->lp_ratios[0] = resp & 0xff;
	fact_info->lp_ratios[1] = (resp >> 8) & 0xff;
	fact_info->lp_ratios[2] = (resp >> 16) & 0xff;

	ret = _get_fact_bucket_info(id, level, fact_info->bucket_info);
	if (ret)
		return ret;

	print = 0;
	for (j = 0; j < ISST_FACT_MAX_BUCKETS; ++j) {
		if (fact_bucket != 0xff && fact_bucket != j)
			continue;

		if (!fact_info->bucket_info[j].hp_cores)
			break;

		print = 1;
	}
	if (!print) {
		isst_display_error_info_message(1, "Invalid bucket", 0, 0);
		return -1;
	}

	return 0;
}

static int mbox_get_clos_information(struct isst_id *id, int *enable, int *type)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(id->cpu, CONFIG_CLOS, CLOS_PM_QOS_CONFIG, 0, 0,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CLOS_PM_QOS_CONFIG resp:%x\n", id->cpu, resp);

	if (resp & BIT(1))
		*enable = 1;
	else
		*enable = 0;

	if (resp & BIT(2))
		*type = 1;
	else
		*type = 0;

	return 0;
}

static int _write_pm_config(struct isst_id *id, int cp_state)
{
	unsigned int req, resp;
	int ret;

	if (cp_state)
		req = BIT(16);
	else
		req = 0;

	ret = isst_send_mbox_command(id->cpu, WRITE_PM_CONFIG, PM_FEATURE, 0, req,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d WRITE_PM_CONFIG resp:%x\n", id->cpu, resp);

	return 0;
}

static int mbox_pm_qos_config(struct isst_id *id, int enable_clos, int priority_type)
{
	unsigned int req, resp;
	int ret;

	if (!enable_clos) {
		struct isst_pkg_ctdp pkg_dev;
		struct isst_pkg_ctdp_level_info ctdp_level;

		ret = isst_get_ctdp_levels(id, &pkg_dev);
		if (ret) {
			debug_printf("isst_get_ctdp_levels\n");
			return ret;
		}

		ret = isst_get_ctdp_control(id, pkg_dev.current_level,
					    &ctdp_level);
		if (ret)
			return ret;

		if (ctdp_level.fact_enabled) {
			isst_display_error_info_message(1, "Ignoring request, turbo-freq feature is still enabled", 0, 0);
			return -EINVAL;
		}
		ret = _write_pm_config(id, 0);
		if (ret)
			isst_display_error_info_message(0, "WRITE_PM_CONFIG command failed, ignoring error", 0, 0);
	} else {
		ret = _write_pm_config(id, 1);
		if (ret)
			isst_display_error_info_message(0, "WRITE_PM_CONFIG command failed, ignoring error", 0, 0);
	}

	ret = isst_send_mbox_command(id->cpu, CONFIG_CLOS, CLOS_PM_QOS_CONFIG, 0, 0,
				     &resp);
	if (ret) {
		isst_display_error_info_message(1, "CLOS_PM_QOS_CONFIG command failed", 0, 0);
		return ret;
	}

	debug_printf("cpu:%d CLOS_PM_QOS_CONFIG resp:%x\n", id->cpu, resp);

	req = resp;

	if (enable_clos)
		req = req | BIT(1);
	else
		req = req & ~BIT(1);

	if (priority_type > 1)
		isst_display_error_info_message(1, "Invalid priority type: Changing type to ordered", 0, 0);

	if (priority_type)
		req = req | BIT(2);
	else
		req = req & ~BIT(2);

	ret = isst_send_mbox_command(id->cpu, CONFIG_CLOS, CLOS_PM_QOS_CONFIG,
				     BIT(MBOX_CMD_WRITE_BIT), req, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CLOS_PM_QOS_CONFIG priority type:%d req:%x\n", id->cpu,
		     priority_type, req);

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
	.get_get_trls = mbox_get_get_trls,
	.get_trl_bucket_info = mbox_get_trl_bucket_info,
	.set_tdp_level = mbox_set_tdp_level,
	.get_pbf_info = mbox_get_pbf_info,
	.set_pbf_fact_status = mbox_set_pbf_fact_status,
	.get_fact_info = mbox_get_fact_info,
	.get_uncore_p0_p1_info = mbox_get_uncore_p0_p1_info,
	.get_clos_information = mbox_get_clos_information,
	.pm_qos_config = mbox_pm_qos_config,
};

struct isst_platform_ops *mbox_get_platform_ops(void)
{
	return &mbox_ops;
}
