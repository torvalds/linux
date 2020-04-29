// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select -- Enumerate and control features
 * Copyright (c) 2019 Intel Corporation.
 */

#include "isst.h"

int isst_write_pm_config(int cpu, int cp_state)
{
	unsigned int req, resp;
	int ret;

	if (cp_state)
		req = BIT(16);
	else
		req = 0;

	ret = isst_send_mbox_command(cpu, WRITE_PM_CONFIG, PM_FEATURE, 0, req,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d WRITE_PM_CONFIG resp:%x\n", cpu, resp);

	return 0;
}

int isst_read_pm_config(int cpu, int *cp_state, int *cp_cap)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(cpu, READ_PM_CONFIG, PM_FEATURE, 0, 0,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d READ_PM_CONFIG resp:%x\n", cpu, resp);

	*cp_state = resp & BIT(16);
	*cp_cap = resp & BIT(0) ? 1 : 0;

	return 0;
}

int isst_get_ctdp_levels(int cpu, struct isst_pkg_ctdp *pkg_dev)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_LEVELS_INFO, 0, 0, &resp);
	if (ret) {
		pkg_dev->levels = 0;
		pkg_dev->locked = 1;
		pkg_dev->current_level = 0;
		pkg_dev->version = 0;
		pkg_dev->enabled = 0;
		return 0;
	}

	debug_printf("cpu:%d CONFIG_TDP_GET_LEVELS_INFO resp:%x\n", cpu, resp);

	pkg_dev->version = resp & 0xff;
	pkg_dev->levels = (resp >> 8) & 0xff;
	pkg_dev->current_level = (resp >> 16) & 0xff;
	pkg_dev->locked = !!(resp & BIT(24));
	pkg_dev->enabled = !!(resp & BIT(31));

	return 0;
}

int isst_get_ctdp_control(int cpu, int config_index,
			  struct isst_pkg_ctdp_level_info *ctdp_level)
{
	int cp_state, cp_cap;
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_TDP_CONTROL, 0,
				     config_index, &resp);
	if (ret)
		return ret;

	ctdp_level->fact_support = resp & BIT(0);
	ctdp_level->pbf_support = !!(resp & BIT(1));
	ctdp_level->fact_enabled = !!(resp & BIT(16));
	ctdp_level->pbf_enabled = !!(resp & BIT(17));

	ret = isst_read_pm_config(cpu, &cp_state, &cp_cap);
	if (ret) {
		debug_printf("cpu:%d pm_config is not supported \n", cpu);
	} else {
		debug_printf("cpu:%d pm_config SST-CP state:%d cap:%d \n", cpu, cp_state, cp_cap);
		ctdp_level->sst_cp_support = cp_cap;
		ctdp_level->sst_cp_enabled = cp_state;
	}

	debug_printf(
		"cpu:%d CONFIG_TDP_GET_TDP_CONTROL resp:%x fact_support:%d pbf_support: %d fact_enabled:%d pbf_enabled:%d\n",
		cpu, resp, ctdp_level->fact_support, ctdp_level->pbf_support,
		ctdp_level->fact_enabled, ctdp_level->pbf_enabled);

	return 0;
}

int isst_get_tdp_info(int cpu, int config_index,
		      struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(cpu, CONFIG_TDP, CONFIG_TDP_GET_TDP_INFO,
				     0, config_index, &resp);
	if (ret) {
		isst_display_error_info_message(1, "Invalid level, Can't get TDP information at level", 1, config_index);
		return ret;
	}

	ctdp_level->pkg_tdp = resp & GENMASK(14, 0);
	ctdp_level->tdp_ratio = (resp & GENMASK(23, 16)) >> 16;

	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_TDP_INFO resp:%x tdp_ratio:%d pkg_tdp:%d\n",
		cpu, config_index, resp, ctdp_level->tdp_ratio,
		ctdp_level->pkg_tdp);
	return 0;
}

int isst_get_pwr_info(int cpu, int config_index,
		      struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(cpu, CONFIG_TDP, CONFIG_TDP_GET_PWR_INFO,
				     0, config_index, &resp);
	if (ret)
		return ret;

	ctdp_level->pkg_max_power = resp & GENMASK(14, 0);
	ctdp_level->pkg_min_power = (resp & GENMASK(30, 16)) >> 16;

	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_PWR_INFO resp:%x pkg_max_power:%d pkg_min_power:%d\n",
		cpu, config_index, resp, ctdp_level->pkg_max_power,
		ctdp_level->pkg_min_power);

	return 0;
}

void isst_get_uncore_p0_p1_info(int cpu, int config_index,
				struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;
	ret = isst_send_mbox_command(cpu, CONFIG_TDP,
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
		cpu, config_index, resp, ctdp_level->uncore_p0,
		ctdp_level->uncore_p1);
}

void isst_get_p1_info(int cpu, int config_index,
		      struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;
	ret = isst_send_mbox_command(cpu, CONFIG_TDP, CONFIG_TDP_GET_P1_INFO, 0,
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
		cpu, config_index, resp, ctdp_level->sse_p1,
		ctdp_level->avx2_p1, ctdp_level->avx512_p1);
}

void isst_get_uncore_mem_freq(int cpu, int config_index,
			      struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;
	ret = isst_send_mbox_command(cpu, CONFIG_TDP, CONFIG_TDP_GET_MEM_FREQ,
				     0, config_index, &resp);
	if (ret) {
		ctdp_level->mem_freq = 0;
		return;
	}

	ctdp_level->mem_freq = resp & GENMASK(7, 0);
	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_MEM_FREQ resp:%x uncore mem_freq:%d\n",
		cpu, config_index, resp, ctdp_level->mem_freq);
}

int isst_get_tjmax_info(int cpu, int config_index,
			struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(cpu, CONFIG_TDP, CONFIG_TDP_GET_TJMAX_INFO,
				     0, config_index, &resp);
	if (ret)
		return ret;

	ctdp_level->t_proc_hot = resp & GENMASK(7, 0);

	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_TJMAX_INFO resp:%x t_proc_hot:%d\n",
		cpu, config_index, resp, ctdp_level->t_proc_hot);

	return 0;
}

int isst_get_coremask_info(int cpu, int config_index,
			   struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int i, ret;

	ctdp_level->cpu_count = 0;
	for (i = 0; i < 2; ++i) {
		unsigned long long mask;
		int cpu_count = 0;

		ret = isst_send_mbox_command(cpu, CONFIG_TDP,
					     CONFIG_TDP_GET_CORE_MASK, 0,
					     (i << 8) | config_index, &resp);
		if (ret)
			return ret;

		debug_printf(
			"cpu:%d ctdp:%d mask:%d CONFIG_TDP_GET_CORE_MASK resp:%x\n",
			cpu, config_index, i, resp);

		mask = (unsigned long long)resp << (32 * i);
		set_cpu_mask_from_punit_coremask(cpu, mask,
						 ctdp_level->core_cpumask_size,
						 ctdp_level->core_cpumask,
						 &cpu_count);
		ctdp_level->cpu_count += cpu_count;
		debug_printf("cpu:%d ctdp:%d mask:%d cpu count:%d\n", cpu,
			     config_index, i, ctdp_level->cpu_count);
	}

	return 0;
}

int isst_get_get_trl_from_msr(int cpu, int *trl)
{
	unsigned long long msr_trl;
	int ret;

	ret = isst_send_msr_command(cpu, 0x1AD, 0, &msr_trl);
	if (ret)
		return ret;

	trl[0] = msr_trl & GENMASK(7, 0);
	trl[1] = (msr_trl & GENMASK(15, 8)) >> 8;
	trl[2] = (msr_trl & GENMASK(23, 16)) >> 16;
	trl[3] = (msr_trl & GENMASK(31, 24)) >> 24;
	trl[4] = (msr_trl & GENMASK(39, 32)) >> 32;
	trl[5] = (msr_trl & GENMASK(47, 40)) >> 40;
	trl[6] = (msr_trl & GENMASK(55, 48)) >> 48;
	trl[7] = (msr_trl & GENMASK(63, 56)) >> 56;

	return 0;
}

int isst_get_get_trl(int cpu, int level, int avx_level, int *trl)
{
	unsigned int req, resp;
	int ret;

	req = level | (avx_level << 16);
	ret = isst_send_mbox_command(cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_TURBO_LIMIT_RATIOS, 0, req,
				     &resp);
	if (ret)
		return ret;

	debug_printf(
		"cpu:%d CONFIG_TDP_GET_TURBO_LIMIT_RATIOS req:%x resp:%x\n",
		cpu, req, resp);

	trl[0] = resp & GENMASK(7, 0);
	trl[1] = (resp & GENMASK(15, 8)) >> 8;
	trl[2] = (resp & GENMASK(23, 16)) >> 16;
	trl[3] = (resp & GENMASK(31, 24)) >> 24;

	req = level | BIT(8) | (avx_level << 16);
	ret = isst_send_mbox_command(cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_TURBO_LIMIT_RATIOS, 0, req,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_GET_TURBO_LIMIT req:%x resp:%x\n", cpu,
		     req, resp);

	trl[4] = resp & GENMASK(7, 0);
	trl[5] = (resp & GENMASK(15, 8)) >> 8;
	trl[6] = (resp & GENMASK(23, 16)) >> 16;
	trl[7] = (resp & GENMASK(31, 24)) >> 24;

	return 0;
}

int isst_get_trl_bucket_info(int cpu, unsigned long long *buckets_info)
{
	int ret;

	debug_printf("cpu:%d bucket info via MSR\n", cpu);

	*buckets_info = 0;

	ret = isst_send_msr_command(cpu, 0x1ae, 0, buckets_info);
	if (ret)
		return ret;

	debug_printf("cpu:%d bucket info via MSR successful 0x%llx\n", cpu,
		     *buckets_info);

	return 0;
}

int isst_set_tdp_level_msr(int cpu, int tdp_level)
{
	unsigned long long level = tdp_level;
	int ret;

	debug_printf("cpu: tdp_level via MSR %d\n", cpu, tdp_level);

	if (isst_get_config_tdp_lock_status(cpu)) {
		isst_display_error_info_message(1, "tdp_locked", 0, 0);
		return -1;
	}

	if (tdp_level > 2)
		return -1; /* invalid value */

	ret = isst_send_msr_command(cpu, 0x64b, 1, &level);
	if (ret)
		return ret;

	debug_printf("cpu: tdp_level via MSR successful %d\n", cpu, tdp_level);

	return 0;
}

int isst_set_tdp_level(int cpu, int tdp_level)
{
	unsigned int resp;
	int ret;


	if (isst_get_config_tdp_lock_status(cpu)) {
		isst_display_error_info_message(1, "TDP is locked", 0, 0);
		return -1;

	}

	ret = isst_send_mbox_command(cpu, CONFIG_TDP, CONFIG_TDP_SET_LEVEL, 0,
				     tdp_level, &resp);
	if (ret) {
		isst_display_error_info_message(1, "Set TDP level failed for level", 1, tdp_level);
		return ret;
	}

	return 0;
}

int isst_get_pbf_info(int cpu, int level, struct isst_pbf_info *pbf_info)
{
	struct isst_pkg_ctdp_level_info ctdp_level;
	struct isst_pkg_ctdp pkg_dev;
	int i, ret, core_cnt, max;
	unsigned int req, resp;

	ret = isst_get_ctdp_levels(cpu, &pkg_dev);
	if (ret) {
		isst_display_error_info_message(1, "Failed to get number of levels", 0, 0);
		return ret;
	}

	if (level > pkg_dev.levels) {
		isst_display_error_info_message(1, "Invalid level", 1, level);
		return -1;
	}

	ret = isst_get_ctdp_control(cpu, level, &ctdp_level);
	if (ret)
		return ret;

	if (!ctdp_level.pbf_support) {
		isst_display_error_info_message(1, "base-freq feature is not present at this level", 1, level);
		return -1;
	}

	pbf_info->core_cpumask_size = alloc_cpu_set(&pbf_info->core_cpumask);

	core_cnt = get_core_count(get_physical_package_id(cpu), get_physical_die_id(cpu));
	max = core_cnt > 32 ? 2 : 1;

	for (i = 0; i < max; ++i) {
		unsigned long long mask;
		int count;

		ret = isst_send_mbox_command(cpu, CONFIG_TDP,
					     CONFIG_TDP_PBF_GET_CORE_MASK_INFO,
					     0, (i << 8) | level, &resp);
		if (ret)
			break;

		debug_printf(
			"cpu:%d CONFIG_TDP_PBF_GET_CORE_MASK_INFO resp:%x\n",
			cpu, resp);

		mask = (unsigned long long)resp << (32 * i);
		set_cpu_mask_from_punit_coremask(cpu, mask,
						 pbf_info->core_cpumask_size,
						 pbf_info->core_cpumask,
						 &count);
	}

	req = level;
	ret = isst_send_mbox_command(cpu, CONFIG_TDP,
				     CONFIG_TDP_PBF_GET_P1HI_P1LO_INFO, 0, req,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_PBF_GET_P1HI_P1LO_INFO resp:%x\n", cpu,
		     resp);

	pbf_info->p1_low = resp & 0xff;
	pbf_info->p1_high = (resp & GENMASK(15, 8)) >> 8;

	req = level;
	ret = isst_send_mbox_command(
		cpu, CONFIG_TDP, CONFIG_TDP_PBF_GET_TDP_INFO, 0, req, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_PBF_GET_TDP_INFO resp:%x\n", cpu, resp);

	pbf_info->tdp = resp & 0xffff;

	req = level;
	ret = isst_send_mbox_command(
		cpu, CONFIG_TDP, CONFIG_TDP_PBF_GET_TJ_MAX_INFO, 0, req, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_PBF_GET_TJ_MAX_INFO resp:%x\n", cpu,
		     resp);
	pbf_info->t_control = (resp >> 8) & 0xff;
	pbf_info->t_prochot = resp & 0xff;

	return 0;
}

void isst_get_pbf_info_complete(struct isst_pbf_info *pbf_info)
{
	free_cpu_set(pbf_info->core_cpumask);
}

int isst_set_pbf_fact_status(int cpu, int pbf, int enable)
{
	struct isst_pkg_ctdp pkg_dev;
	struct isst_pkg_ctdp_level_info ctdp_level;
	int current_level;
	unsigned int req = 0, resp;
	int ret;

	ret = isst_get_ctdp_levels(cpu, &pkg_dev);
	if (ret)
		debug_printf("cpu:%d No support for dynamic ISST\n", cpu);

	current_level = pkg_dev.current_level;

	ret = isst_get_ctdp_control(cpu, current_level, &ctdp_level);
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

	ret = isst_send_mbox_command(cpu, CONFIG_TDP,
				     CONFIG_TDP_SET_TDP_CONTROL, 0, req, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_SET_TDP_CONTROL pbf/fact:%d req:%x\n",
		     cpu, pbf, req);

	return 0;
}

int isst_get_fact_bucket_info(int cpu, int level,
			      struct isst_fact_bucket_info *bucket_info)
{
	unsigned int resp;
	int i, k, ret;

	for (i = 0; i < 2; ++i) {
		int j;

		ret = isst_send_mbox_command(
			cpu, CONFIG_TDP,
			CONFIG_TDP_GET_FACT_HP_TURBO_LIMIT_NUMCORES, 0,
			(i << 8) | level, &resp);
		if (ret)
			return ret;

		debug_printf(
			"cpu:%d CONFIG_TDP_GET_FACT_HP_TURBO_LIMIT_NUMCORES index:%d level:%d resp:%x\n",
			cpu, i, level, resp);

		for (j = 0; j < 4; ++j) {
			bucket_info[j + (i * 4)].high_priority_cores_count =
				(resp >> (j * 8)) & 0xff;
		}
	}

	for (k = 0; k < 3; ++k) {
		for (i = 0; i < 2; ++i) {
			int j;

			ret = isst_send_mbox_command(
				cpu, CONFIG_TDP,
				CONFIG_TDP_GET_FACT_HP_TURBO_LIMIT_RATIOS, 0,
				(k << 16) | (i << 8) | level, &resp);
			if (ret)
				return ret;

			debug_printf(
				"cpu:%d CONFIG_TDP_GET_FACT_HP_TURBO_LIMIT_RATIOS index:%d level:%d avx:%d resp:%x\n",
				cpu, i, level, k, resp);

			for (j = 0; j < 4; ++j) {
				switch (k) {
				case 0:
					bucket_info[j + (i * 4)].sse_trl =
						(resp >> (j * 8)) & 0xff;
					break;
				case 1:
					bucket_info[j + (i * 4)].avx_trl =
						(resp >> (j * 8)) & 0xff;
					break;
				case 2:
					bucket_info[j + (i * 4)].avx512_trl =
						(resp >> (j * 8)) & 0xff;
					break;
				default:
					break;
				}
			}
		}
	}

	return 0;
}

int isst_get_fact_info(int cpu, int level, int fact_bucket, struct isst_fact_info *fact_info)
{
	struct isst_pkg_ctdp_level_info ctdp_level;
	struct isst_pkg_ctdp pkg_dev;
	unsigned int resp;
	int j, ret, print;

	ret = isst_get_ctdp_levels(cpu, &pkg_dev);
	if (ret) {
		isst_display_error_info_message(1, "Failed to get number of levels", 0, 0);
		return ret;
	}

	if (level > pkg_dev.levels) {
		isst_display_error_info_message(1, "Invalid level", 1, level);
		return -1;
	}

	ret = isst_get_ctdp_control(cpu, level, &ctdp_level);
	if (ret)
		return ret;

	if (!ctdp_level.fact_support) {
		isst_display_error_info_message(1, "turbo-freq feature is not present at this level", 1, level);
		return -1;
	}

	ret = isst_send_mbox_command(cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_FACT_LP_CLIPPING_RATIO, 0,
				     level, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CONFIG_TDP_GET_FACT_LP_CLIPPING_RATIO resp:%x\n",
		     cpu, resp);

	fact_info->lp_clipping_ratio_license_sse = resp & 0xff;
	fact_info->lp_clipping_ratio_license_avx2 = (resp >> 8) & 0xff;
	fact_info->lp_clipping_ratio_license_avx512 = (resp >> 16) & 0xff;

	ret = isst_get_fact_bucket_info(cpu, level, fact_info->bucket_info);
	if (ret)
		return ret;

	print = 0;
	for (j = 0; j < ISST_FACT_MAX_BUCKETS; ++j) {
		if (fact_bucket != 0xff && fact_bucket != j)
			continue;

		if (!fact_info->bucket_info[j].high_priority_cores_count)
			break;

		print = 1;
	}
	if (!print) {
		isst_display_error_info_message(1, "Invalid bucket", 0, 0);
		return -1;
	}

	return 0;
}

int isst_set_trl(int cpu, unsigned long long trl)
{
	int ret;

	if (!trl)
		trl = 0xFFFFFFFFFFFFFFFFULL;

	ret = isst_send_msr_command(cpu, 0x1AD, 1, &trl);
	if (ret)
		return ret;

	return 0;
}

int isst_set_trl_from_current_tdp(int cpu, unsigned long long trl)
{
	unsigned long long msr_trl;
	int ret;

	if (trl) {
		msr_trl = trl;
	} else {
		struct isst_pkg_ctdp pkg_dev;
		int trl[8];
		int i;

		ret = isst_get_ctdp_levels(cpu, &pkg_dev);
		if (ret)
			return ret;

		ret = isst_get_get_trl(cpu, pkg_dev.current_level, 0, trl);
		if (ret)
			return ret;

		msr_trl = 0;
		for (i = 0; i < 8; ++i) {
			unsigned long long _trl = trl[i];

			msr_trl |= (_trl << (i * 8));
		}
	}
	ret = isst_send_msr_command(cpu, 0x1AD, 1, &msr_trl);
	if (ret)
		return ret;

	return 0;
}

/* Return 1 if locked */
int isst_get_config_tdp_lock_status(int cpu)
{
	unsigned long long tdp_control = 0;
	int ret;

	ret = isst_send_msr_command(cpu, 0x64b, 0, &tdp_control);
	if (ret)
		return ret;

	ret = !!(tdp_control & BIT(31));

	return ret;
}

void isst_get_process_ctdp_complete(int cpu, struct isst_pkg_ctdp *pkg_dev)
{
	int i;

	if (!pkg_dev->processed)
		return;

	for (i = 0; i < pkg_dev->levels; ++i) {
		struct isst_pkg_ctdp_level_info *ctdp_level;

		ctdp_level = &pkg_dev->ctdp_level[i];
		if (ctdp_level->pbf_support)
			free_cpu_set(ctdp_level->pbf_info.core_cpumask);
		free_cpu_set(ctdp_level->core_cpumask);
	}
}

int isst_get_process_ctdp(int cpu, int tdp_level, struct isst_pkg_ctdp *pkg_dev)
{
	int i, ret, valid = 0;

	if (pkg_dev->processed)
		return 0;

	ret = isst_get_ctdp_levels(cpu, pkg_dev);
	if (ret)
		return ret;

	debug_printf("cpu: %d ctdp enable:%d current level: %d levels:%d\n",
		     cpu, pkg_dev->enabled, pkg_dev->current_level,
		     pkg_dev->levels);

	if (tdp_level != 0xff && tdp_level > pkg_dev->levels) {
		isst_display_error_info_message(1, "Invalid level", 0, 0);
		return -1;
	}

	if (!pkg_dev->enabled)
		isst_display_error_info_message(0, "perf-profile feature is not supported, just base-config level 0 is valid", 0, 0);

	for (i = 0; i <= pkg_dev->levels; ++i) {
		struct isst_pkg_ctdp_level_info *ctdp_level;

		if (tdp_level != 0xff && i != tdp_level)
			continue;

		debug_printf("cpu:%d Get Information for TDP level:%d\n", cpu,
			     i);
		ctdp_level = &pkg_dev->ctdp_level[i];

		ctdp_level->level = i;
		ctdp_level->control_cpu = cpu;
		ctdp_level->pkg_id = get_physical_package_id(cpu);
		ctdp_level->die_id = get_physical_die_id(cpu);

		ret = isst_get_ctdp_control(cpu, i, ctdp_level);
		if (ret)
			continue;

		valid = 1;
		pkg_dev->processed = 1;
		ctdp_level->processed = 1;

		if (ctdp_level->pbf_support) {
			ret = isst_get_pbf_info(cpu, i, &ctdp_level->pbf_info);
			if (!ret)
				ctdp_level->pbf_found = 1;
		}

		if (ctdp_level->fact_support) {
			ret = isst_get_fact_info(cpu, i, 0xff,
						 &ctdp_level->fact_info);
			if (ret)
				return ret;
		}

		if (!pkg_dev->enabled) {
			int freq;

			freq = get_cpufreq_base_freq(cpu);
			if (freq > 0) {
				ctdp_level->sse_p1 = freq / 100000;
				ctdp_level->tdp_ratio = ctdp_level->sse_p1;
			}

			isst_get_get_trl_from_msr(cpu, ctdp_level->trl_sse_active_cores);
			isst_get_trl_bucket_info(cpu, &ctdp_level->buckets_info);
			continue;
		}

		ret = isst_get_tdp_info(cpu, i, ctdp_level);
		if (ret)
			return ret;

		ret = isst_get_pwr_info(cpu, i, ctdp_level);
		if (ret)
			return ret;

		ret = isst_get_tjmax_info(cpu, i, ctdp_level);
		if (ret)
			return ret;

		ctdp_level->core_cpumask_size =
			alloc_cpu_set(&ctdp_level->core_cpumask);
		ret = isst_get_coremask_info(cpu, i, ctdp_level);
		if (ret)
			return ret;

		ret = isst_get_trl_bucket_info(cpu, &ctdp_level->buckets_info);
		if (ret)
			return ret;

		ret = isst_get_get_trl(cpu, i, 0,
				       ctdp_level->trl_sse_active_cores);
		if (ret)
			return ret;

		ret = isst_get_get_trl(cpu, i, 1,
				       ctdp_level->trl_avx_active_cores);
		if (ret)
			return ret;

		ret = isst_get_get_trl(cpu, i, 2,
				       ctdp_level->trl_avx_512_active_cores);
		if (ret)
			return ret;

		isst_get_uncore_p0_p1_info(cpu, i, ctdp_level);
		isst_get_p1_info(cpu, i, ctdp_level);
		isst_get_uncore_mem_freq(cpu, i, ctdp_level);
	}

	if (!valid)
		isst_display_error_info_message(0, "Invalid level, Can't get TDP control information at specified levels on cpu", 1, cpu);

	return 0;
}

int isst_clos_get_clos_information(int cpu, int *enable, int *type)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(cpu, CONFIG_CLOS, CLOS_PM_QOS_CONFIG, 0, 0,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CLOS_PM_QOS_CONFIG resp:%x\n", cpu, resp);

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

int isst_pm_qos_config(int cpu, int enable_clos, int priority_type)
{
	unsigned int req, resp;
	int ret;

	if (!enable_clos) {
		struct isst_pkg_ctdp pkg_dev;
		struct isst_pkg_ctdp_level_info ctdp_level;

		ret = isst_get_ctdp_levels(cpu, &pkg_dev);
		if (ret) {
			debug_printf("isst_get_ctdp_levels\n");
			return ret;
		}

		ret = isst_get_ctdp_control(cpu, pkg_dev.current_level,
					    &ctdp_level);
		if (ret)
			return ret;

		if (ctdp_level.fact_enabled) {
			debug_printf("Turbo-freq feature must be disabled first\n");
			return -EINVAL;
		}
		ret = isst_write_pm_config(cpu, 0);
		if (ret)
			isst_display_error_info_message(0, "WRITE_PM_CONFIG command failed, ignoring error\n", 0, 0);
	} else {
		ret = isst_write_pm_config(cpu, 1);
		if (ret)
			isst_display_error_info_message(0, "WRITE_PM_CONFIG command failed, ignoring error\n", 0, 0);
	}

	ret = isst_send_mbox_command(cpu, CONFIG_CLOS, CLOS_PM_QOS_CONFIG, 0, 0,
				     &resp);
	if (ret) {
		isst_display_error_info_message(1, "CLOS_PM_QOS_CONFIG command failed", 0, 0);
		return ret;
	}

	debug_printf("cpu:%d CLOS_PM_QOS_CONFIG resp:%x\n", cpu, resp);

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

	ret = isst_send_mbox_command(cpu, CONFIG_CLOS, CLOS_PM_QOS_CONFIG,
				     BIT(MBOX_CMD_WRITE_BIT), req, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CLOS_PM_QOS_CONFIG priority type:%d req:%x\n", cpu,
		     priority_type, req);

	return 0;
}

int isst_pm_get_clos(int cpu, int clos, struct isst_clos_config *clos_config)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(cpu, CONFIG_CLOS, CLOS_PM_CLOS, clos, 0,
				     &resp);
	if (ret)
		return ret;

	clos_config->pkg_id = get_physical_package_id(cpu);
	clos_config->die_id = get_physical_die_id(cpu);

	clos_config->epp = resp & 0x0f;
	clos_config->clos_prop_prio = (resp >> 4) & 0x0f;
	clos_config->clos_min = (resp >> 8) & 0xff;
	clos_config->clos_max = (resp >> 16) & 0xff;
	clos_config->clos_desired = (resp >> 24) & 0xff;

	return 0;
}

int isst_set_clos(int cpu, int clos, struct isst_clos_config *clos_config)
{
	unsigned int req, resp;
	unsigned int param;
	int ret;

	req = clos_config->epp & 0x0f;
	req |= (clos_config->clos_prop_prio & 0x0f) << 4;
	req |= (clos_config->clos_min & 0xff) << 8;
	req |= (clos_config->clos_max & 0xff) << 16;
	req |= (clos_config->clos_desired & 0xff) << 24;

	param = BIT(MBOX_CMD_WRITE_BIT) | clos;

	ret = isst_send_mbox_command(cpu, CONFIG_CLOS, CLOS_PM_CLOS, param, req,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CLOS_PM_CLOS param:%x req:%x\n", cpu, param, req);

	return 0;
}

int isst_clos_get_assoc_status(int cpu, int *clos_id)
{
	unsigned int resp;
	unsigned int param;
	int core_id, ret;

	core_id = find_phy_core_num(cpu);
	param = core_id;

	ret = isst_send_mbox_command(cpu, CONFIG_CLOS, CLOS_PQR_ASSOC, param, 0,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CLOS_PQR_ASSOC param:%x resp:%x\n", cpu, param,
		     resp);
	*clos_id = (resp >> 16) & 0x03;

	return 0;
}

int isst_clos_associate(int cpu, int clos_id)
{
	unsigned int req, resp;
	unsigned int param;
	int core_id, ret;

	req = (clos_id & 0x03) << 16;
	core_id = find_phy_core_num(cpu);
	param = BIT(MBOX_CMD_WRITE_BIT) | core_id;

	ret = isst_send_mbox_command(cpu, CONFIG_CLOS, CLOS_PQR_ASSOC, param,
				     req, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CLOS_PQR_ASSOC param:%x req:%x\n", cpu, param,
		     req);

	return 0;
}
