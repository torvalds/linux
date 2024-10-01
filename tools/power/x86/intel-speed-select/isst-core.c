// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select -- Enumerate and control features
 * Copyright (c) 2019 Intel Corporation.
 */

#include "isst.h"

static struct isst_platform_ops		*isst_ops;

#define CHECK_CB(_name)	\
	do {	\
		if (!isst_ops || !isst_ops->_name) {	\
			fprintf(stderr, "Invalid ops\n");	\
			exit(0);	\
		}	\
	} while (0)

int isst_set_platform_ops(int api_version)
{
	switch (api_version) {
	case 1:
		isst_ops = mbox_get_platform_ops();
		break;
	case 2:
	case 3:
		isst_ops = tpmi_get_platform_ops();
		break;
	default:
		isst_ops = NULL;
		break;
	}

	if (!isst_ops)
		return -1;
	return 0;
}

void isst_update_platform_param(enum isst_platform_param param, int value)
{
	CHECK_CB(update_platform_param);

	isst_ops->update_platform_param(param, value);
}

int isst_get_disp_freq_multiplier(void)
{
	CHECK_CB(get_disp_freq_multiplier);
	return isst_ops->get_disp_freq_multiplier();
}

int isst_get_trl_max_levels(void)
{
	CHECK_CB(get_trl_max_levels);
	return isst_ops->get_trl_max_levels();
}

char *isst_get_trl_level_name(int level)
{
	CHECK_CB(get_trl_level_name);
	return isst_ops->get_trl_level_name(level);
}

int isst_is_punit_valid(struct isst_id *id)
{
	CHECK_CB(is_punit_valid);
	return isst_ops->is_punit_valid(id);
}

int isst_send_msr_command(unsigned int cpu, unsigned int msr, int write,
			  unsigned long long *req_resp)
{
	struct isst_if_msr_cmds msr_cmds;
	const char *pathname = "/dev/isst_interface";
	FILE *outf = get_output_file();
	int fd;

	fd = open(pathname, O_RDWR);
	if (fd < 0)
		err(-1, "%s open failed", pathname);

	msr_cmds.cmd_count = 1;
	msr_cmds.msr_cmd[0].logical_cpu = cpu;
	msr_cmds.msr_cmd[0].msr = msr;
	msr_cmds.msr_cmd[0].read_write = write;
	if (write)
		msr_cmds.msr_cmd[0].data = *req_resp;

	if (ioctl(fd, ISST_IF_MSR_COMMAND, &msr_cmds) == -1) {
		perror("ISST_IF_MSR_COMMAND");
		fprintf(outf, "Error: msr_cmd cpu:%d msr:%x read_write:%d\n",
			cpu, msr, write);
	} else {
		if (!write)
			*req_resp = msr_cmds.msr_cmd[0].data;

		debug_printf(
			"msr_cmd response: cpu:%d msr:%x rd_write:%x resp:%llx %llx\n",
			cpu, msr, write, *req_resp, msr_cmds.msr_cmd[0].data);
	}

	close(fd);

	return 0;
}

int isst_read_pm_config(struct isst_id *id, int *cp_state, int *cp_cap)
{
	CHECK_CB(read_pm_config);
	return isst_ops->read_pm_config(id, cp_state, cp_cap);
}

int isst_get_ctdp_levels(struct isst_id *id, struct isst_pkg_ctdp *pkg_dev)
{
	CHECK_CB(get_config_levels);
	return isst_ops->get_config_levels(id, pkg_dev);
}

int isst_get_ctdp_control(struct isst_id *id, int config_index,
			  struct isst_pkg_ctdp_level_info *ctdp_level)
{
	CHECK_CB(get_ctdp_control);
	return isst_ops->get_ctdp_control(id, config_index, ctdp_level);
}

int isst_get_tdp_info(struct isst_id *id, int config_index,
		      struct isst_pkg_ctdp_level_info *ctdp_level)
{
	CHECK_CB(get_tdp_info);
	return isst_ops->get_tdp_info(id, config_index, ctdp_level);
}

int isst_get_pwr_info(struct isst_id *id, int config_index,
		      struct isst_pkg_ctdp_level_info *ctdp_level)
{
	CHECK_CB(get_pwr_info);
	return isst_ops->get_pwr_info(id, config_index, ctdp_level);
}

int isst_get_coremask_info(struct isst_id *id, int config_index,
			   struct isst_pkg_ctdp_level_info *ctdp_level)
{
	CHECK_CB(get_coremask_info);
	return isst_ops->get_coremask_info(id, config_index, ctdp_level);
}

int isst_get_get_trl_from_msr(struct isst_id *id, int *trl)
{
	unsigned long long msr_trl;
	int ret;

	ret = isst_send_msr_command(id->cpu, 0x1AD, 0, &msr_trl);
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

int isst_get_get_trl(struct isst_id *id, int level, int avx_level, int *trl)
{
	CHECK_CB(get_get_trl);
	return isst_ops->get_get_trl(id, level, avx_level, trl);
}

int isst_get_get_trls(struct isst_id *id, int level, struct isst_pkg_ctdp_level_info *ctdp_level)
{
	CHECK_CB(get_get_trls);
	return isst_ops->get_get_trls(id, level, ctdp_level);
}

int isst_get_trl_bucket_info(struct isst_id *id, int level, unsigned long long *buckets_info)
{
	CHECK_CB(get_trl_bucket_info);
	return isst_ops->get_trl_bucket_info(id, level, buckets_info);
}

int isst_set_tdp_level(struct isst_id *id, int tdp_level)
{
	CHECK_CB(set_tdp_level);
	return isst_ops->set_tdp_level(id, tdp_level);
}

int isst_get_pbf_info(struct isst_id *id, int level, struct isst_pbf_info *pbf_info)
{
	struct isst_pkg_ctdp_level_info ctdp_level;
	struct isst_pkg_ctdp pkg_dev;
	int ret;

	ret = isst_get_ctdp_levels(id, &pkg_dev);
	if (ret) {
		isst_display_error_info_message(1, "Failed to get number of levels", 0, 0);
		return ret;
	}

	if (level > pkg_dev.levels) {
		isst_display_error_info_message(1, "Invalid level", 1, level);
		return -1;
	}

	ret = isst_get_ctdp_control(id, level, &ctdp_level);
	if (ret)
		return ret;

	if (!ctdp_level.pbf_support) {
		isst_display_error_info_message(1, "base-freq feature is not present at this level", 1, level);
		return -1;
	}

	pbf_info->core_cpumask_size = alloc_cpu_set(&pbf_info->core_cpumask);

	CHECK_CB(get_pbf_info);
	return isst_ops->get_pbf_info(id, level, pbf_info);
}

int isst_set_pbf_fact_status(struct isst_id *id, int pbf, int enable)
{
	CHECK_CB(set_pbf_fact_status);
	return isst_ops->set_pbf_fact_status(id, pbf, enable);
}



int isst_get_fact_info(struct isst_id *id, int level, int fact_bucket, struct isst_fact_info *fact_info)
{
	struct isst_pkg_ctdp_level_info ctdp_level;
	struct isst_pkg_ctdp pkg_dev;
	int ret;

	ret = isst_get_ctdp_levels(id, &pkg_dev);
	if (ret) {
		isst_display_error_info_message(1, "Failed to get number of levels", 0, 0);
		return ret;
	}

	if (level > pkg_dev.levels) {
		isst_display_error_info_message(1, "Invalid level", 1, level);
		return -1;
	}

	ret = isst_get_ctdp_control(id, level, &ctdp_level);
	if (ret)
		return ret;

	if (!ctdp_level.fact_support) {
		isst_display_error_info_message(1, "turbo-freq feature is not present at this level", 1, level);
		return -1;
	}
	CHECK_CB(get_fact_info);
	return isst_ops->get_fact_info(id, level, fact_bucket, fact_info);
}

int isst_get_trl(struct isst_id *id, unsigned long long *trl)
{
	int ret;

	ret = isst_send_msr_command(id->cpu, 0x1AD, 0, trl);
	if (ret)
		return ret;

	return 0;
}

int isst_set_trl(struct isst_id *id, unsigned long long trl)
{
	int ret;

	if (!trl)
		trl = 0xFFFFFFFFFFFFFFFFULL;

	ret = isst_send_msr_command(id->cpu, 0x1AD, 1, &trl);
	if (ret)
		return ret;

	return 0;
}

#define MSR_TRL_FREQ_MULTIPLIER		100

int isst_set_trl_from_current_tdp(struct isst_id *id, unsigned long long trl)
{
	unsigned long long msr_trl;
	int ret;

	if (id->cpu < 0)
		return 0;

	if (trl) {
		msr_trl = trl;
	} else {
		struct isst_pkg_ctdp pkg_dev;
		int trl[8];
		int i;

		ret = isst_get_ctdp_levels(id, &pkg_dev);
		if (ret)
			return ret;

		ret = isst_get_get_trl(id, pkg_dev.current_level, 0, trl);
		if (ret)
			return ret;

		msr_trl = 0;
		for (i = 0; i < 8; ++i) {
			unsigned long long _trl = trl[i];

			/* MSR is always in 100 MHz unit */
			if (isst_get_disp_freq_multiplier() == 1)
				_trl /= MSR_TRL_FREQ_MULTIPLIER;

			msr_trl |= (_trl << (i * 8));
		}
	}
	ret = isst_send_msr_command(id->cpu, 0x1AD, 1, &msr_trl);
	if (ret)
		return ret;

	return 0;
}

/* Return 1 if locked */
int isst_get_config_tdp_lock_status(struct isst_id *id)
{
	unsigned long long tdp_control = 0;
	int ret;

	ret = isst_send_msr_command(id->cpu, 0x64b, 0, &tdp_control);
	if (ret)
		return ret;

	ret = !!(tdp_control & BIT(31));

	return ret;
}

void isst_get_process_ctdp_complete(struct isst_id *id, struct isst_pkg_ctdp *pkg_dev)
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

void isst_adjust_uncore_freq(struct isst_id *id, int config_index,
				struct isst_pkg_ctdp_level_info *ctdp_level)
{
	CHECK_CB(adjust_uncore_freq);
	return isst_ops->adjust_uncore_freq(id, config_index, ctdp_level);
}

int isst_get_process_ctdp(struct isst_id *id, int tdp_level, struct isst_pkg_ctdp *pkg_dev)
{
	int i, ret, valid = 0;

	if (pkg_dev->processed)
		return 0;

	ret = isst_get_ctdp_levels(id, pkg_dev);
	if (ret)
		return ret;

	debug_printf("cpu: %d ctdp enable:%d current level: %d levels:%d\n",
		     id->cpu, pkg_dev->enabled, pkg_dev->current_level,
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

		debug_printf("cpu:%d Get Information for TDP level:%d\n", id->cpu,
			     i);
		ctdp_level = &pkg_dev->ctdp_level[i];

		ctdp_level->level = i;
		ctdp_level->control_cpu = id->cpu;
		ctdp_level->pkg_id = id->pkg;
		ctdp_level->die_id = id->die;

		ret = isst_get_ctdp_control(id, i, ctdp_level);
		if (ret)
			continue;

		valid = 1;
		pkg_dev->processed = 1;
		ctdp_level->processed = 1;

		if (ctdp_level->pbf_support) {
			ret = isst_get_pbf_info(id, i, &ctdp_level->pbf_info);
			if (!ret)
				ctdp_level->pbf_found = 1;
		}

		if (ctdp_level->fact_support) {
			ret = isst_get_fact_info(id, i, 0xff,
						 &ctdp_level->fact_info);
			if (ret)
				return ret;
		}

		if (!pkg_dev->enabled && is_skx_based_platform()) {
			int freq;

			freq = get_cpufreq_base_freq(id->cpu);
			if (freq > 0) {
				ctdp_level->sse_p1 = freq / 100000;
				ctdp_level->tdp_ratio = ctdp_level->sse_p1;
			}

			isst_get_get_trl_from_msr(id, ctdp_level->trl_ratios[0]);
			isst_get_trl_bucket_info(id, i, &ctdp_level->trl_cores);
			continue;
		}

		ret = isst_get_tdp_info(id, i, ctdp_level);
		if (ret)
			return ret;

		ret = isst_get_pwr_info(id, i, ctdp_level);
		if (ret)
			return ret;

		ctdp_level->core_cpumask_size =
			alloc_cpu_set(&ctdp_level->core_cpumask);
		ret = isst_get_coremask_info(id, i, ctdp_level);
		if (ret)
			return ret;

		ret = isst_get_trl_bucket_info(id, i, &ctdp_level->trl_cores);
		if (ret)
			return ret;

		ret = isst_get_get_trls(id, i, ctdp_level);
		if (ret)
			return ret;
	}

	if (!valid)
		isst_display_error_info_message(0, "Invalid level, Can't get TDP control information at specified levels on cpu", 1, id->cpu);

	return 0;
}

int isst_clos_get_clos_information(struct isst_id *id, int *enable, int *type)
{
	CHECK_CB(get_clos_information);
	return isst_ops->get_clos_information(id, enable, type);
}

int isst_pm_qos_config(struct isst_id *id, int enable_clos, int priority_type)
{
	CHECK_CB(pm_qos_config);
	return isst_ops->pm_qos_config(id, enable_clos, priority_type);
}

int isst_pm_get_clos(struct isst_id *id, int clos, struct isst_clos_config *clos_config)
{
	CHECK_CB(pm_get_clos);
	return isst_ops->pm_get_clos(id, clos, clos_config);
}

int isst_set_clos(struct isst_id *id, int clos, struct isst_clos_config *clos_config)
{
	CHECK_CB(set_clos);
	return isst_ops->set_clos(id, clos, clos_config);
}

int isst_clos_get_assoc_status(struct isst_id *id, int *clos_id)
{
	CHECK_CB(clos_get_assoc_status);
	return isst_ops->clos_get_assoc_status(id, clos_id);
}

int isst_clos_associate(struct isst_id *id, int clos_id)
{
	CHECK_CB(clos_associate);
	return isst_ops->clos_associate(id, clos_id);

}
