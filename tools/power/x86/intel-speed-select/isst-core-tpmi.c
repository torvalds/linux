// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select -- Enumerate and control features for TPMI Interface
 * Copyright (c) 2022 Intel Corporation.
 */

#include <linux/isst_if.h>
#include "isst.h"

int tpmi_process_ioctl(int ioctl_no, void *info)
{
	const char *pathname = "/dev/isst_interface";
	int fd;

	if (is_debug_enabled()) {
		debug_printf("Issue IOCTL: ");
		switch (ioctl_no) {
		case ISST_IF_CORE_POWER_STATE:
			debug_printf("ISST_IF_CORE_POWER_STATE\n");
			break;
		case ISST_IF_CLOS_PARAM:
			debug_printf("ISST_IF_CLOS_PARAM\n");
			break;
		case ISST_IF_CLOS_ASSOC:
			debug_printf("ISST_IF_CLOS_ASSOC\n");
			break;
		case ISST_IF_PERF_LEVELS:
			debug_printf("ISST_IF_PERF_LEVELS\n");
			break;
		case ISST_IF_PERF_SET_LEVEL:
			debug_printf("ISST_IF_PERF_SET_LEVEL\n");
			break;
		case ISST_IF_PERF_SET_FEATURE:
			debug_printf("ISST_IF_PERF_SET_FEATURE\n");
			break;
		case ISST_IF_GET_PERF_LEVEL_INFO:
			debug_printf("ISST_IF_GET_PERF_LEVEL_INFO\n");
			break;
		case ISST_IF_GET_PERF_LEVEL_CPU_MASK:
			debug_printf("ISST_IF_GET_PERF_LEVEL_CPU_MASK\n");
			break;
		case ISST_IF_GET_BASE_FREQ_INFO:
			debug_printf("ISST_IF_GET_BASE_FREQ_INFO\n");
			break;
		case ISST_IF_GET_BASE_FREQ_CPU_MASK:
			debug_printf("ISST_IF_GET_BASE_FREQ_CPU_MASK\n");
			break;
		case ISST_IF_GET_TURBO_FREQ_INFO:
			debug_printf("ISST_IF_GET_TURBO_FREQ_INFO\n");
			break;
		case ISST_IF_COUNT_TPMI_INSTANCES:
			debug_printf("ISST_IF_COUNT_TPMI_INSTANCES\n");
			break;
		default:
			debug_printf("%d\n", ioctl_no);
			break;
		}
	}

	fd = open(pathname, O_RDWR);
	if (fd < 0)
		return -1;

	if (ioctl(fd, ioctl_no, info) == -1) {
		debug_printf("IOCTL %d Failed\n", ioctl_no);
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static int tpmi_get_disp_freq_multiplier(void)
{
	return 1;
}

static int tpmi_get_trl_max_levels(void)
{
	return TRL_MAX_LEVELS;
}

static char *tpmi_get_trl_level_name(int level)
{
	switch (level) {
	case 0:
		return "level-0";
	case 1:
		return "level-1";
	case 2:
		return "level-2";
	case 3:
		return "level-3";
	case 4:
		return "level-4";
	case 5:
		return "level-5";
	case 6:
		return "level-6";
	case 7:
		return "level-7";
	default:
		return NULL;
	}
}


static void tpmi_update_platform_param(enum isst_platform_param param, int value)
{
	/* No params need to be updated for now */
}

static int tpmi_is_punit_valid(struct isst_id *id)
{
	struct isst_tpmi_instance_count info;
	int ret;

	if (id->punit < 0)
		return 0;

	info.socket_id = id->pkg;
	ret = tpmi_process_ioctl(ISST_IF_COUNT_TPMI_INSTANCES, &info);
	if (ret == -1)
		return 0;

	if (info.valid_mask & BIT(id->punit))
		return 1;

	return 0;
}

static int tpmi_read_pm_config(struct isst_id *id, int *cp_state, int *cp_cap)
{
	struct isst_core_power info;
	int ret;

	info.get_set = 0;
	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	ret = tpmi_process_ioctl(ISST_IF_CORE_POWER_STATE, &info);
	if (ret == -1)
		return ret;

	*cp_state = info.enable;
	*cp_cap = info.supported;

	return 0;
}

int tpmi_get_config_levels(struct isst_id *id, struct isst_pkg_ctdp *pkg_dev)
{
	struct isst_perf_level_info info;
	int ret;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;

	ret = tpmi_process_ioctl(ISST_IF_PERF_LEVELS, &info);
	if (ret == -1)
		return ret;

	pkg_dev->version = info.feature_rev;
	pkg_dev->levels = info.max_level;
	pkg_dev->locked = info.locked;
	pkg_dev->current_level = info.current_level;
	pkg_dev->locked = info.locked;
	pkg_dev->enabled = info.enabled;

	return 0;
}

static int tpmi_get_ctdp_control(struct isst_id *id, int config_index,
				 struct isst_pkg_ctdp_level_info *ctdp_level)
{
	struct isst_core_power core_power_info;
	struct isst_perf_level_info info;
	int level_mask;
	int ret;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;

	ret = tpmi_process_ioctl(ISST_IF_PERF_LEVELS, &info);
	if (ret == -1)
		return -1;

	if (config_index != 0xff)
		level_mask = 1 << config_index;
	else
		level_mask = config_index;

	if (!(info.level_mask & level_mask))
		return -1;

	ctdp_level->fact_support = info.sst_tf_support;
	ctdp_level->pbf_support = info.sst_bf_support;
	ctdp_level->fact_enabled = !!(info.feature_state & BIT(1));
	ctdp_level->pbf_enabled = !!(info.feature_state & BIT(0));

	core_power_info.get_set = 0;
	core_power_info.socket_id = id->pkg;
	core_power_info.power_domain_id = id->punit;

	ret = tpmi_process_ioctl(ISST_IF_CORE_POWER_STATE, &core_power_info);
	if (ret == -1)
		return ret;

	ctdp_level->sst_cp_support = core_power_info.supported;
	ctdp_level->sst_cp_enabled = core_power_info.enable;

	debug_printf
	    ("cpu:%d CONFIG_TDP_GET_TDP_CONTROL fact_support:%d pbf_support: %d fact_enabled:%d pbf_enabled:%d\n",
	     id->cpu, ctdp_level->fact_support, ctdp_level->pbf_support,
	     ctdp_level->fact_enabled, ctdp_level->pbf_enabled);

	return 0;
}

static int tpmi_get_tdp_info(struct isst_id *id, int config_index,
			     struct isst_pkg_ctdp_level_info *ctdp_level)
{
	struct isst_perf_level_data_info info;
	int ret;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.level = config_index;

	ret = tpmi_process_ioctl(ISST_IF_GET_PERF_LEVEL_INFO, &info);
	if (ret == -1)
		return ret;

	ctdp_level->pkg_tdp = info.thermal_design_power_w;
	ctdp_level->tdp_ratio = info.tdp_ratio;
	ctdp_level->sse_p1 = info.base_freq_mhz;
	ctdp_level->avx2_p1 = info.base_freq_avx2_mhz;
	ctdp_level->avx512_p1 = info.base_freq_avx512_mhz;
	ctdp_level->amx_p1 = info.base_freq_amx_mhz;

	ctdp_level->t_proc_hot = info.tjunction_max_c;
	ctdp_level->mem_freq = info.max_memory_freq_mhz;
	ctdp_level->cooling_type = info.cooling_type;

	ctdp_level->uncore_p0 = info.p0_fabric_freq_mhz;
	ctdp_level->uncore_p1 = info.p1_fabric_freq_mhz;
	ctdp_level->uncore_pm = info.pm_fabric_freq_mhz;

	debug_printf
	    ("cpu:%d ctdp:%d CONFIG_TDP_GET_TDP_INFO tdp_ratio:%d pkg_tdp:%d ctdp_level->t_proc_hot:%d\n",
	     id->cpu, config_index, ctdp_level->tdp_ratio, ctdp_level->pkg_tdp,
	     ctdp_level->t_proc_hot);

	return 0;
}

static int tpmi_get_pwr_info(struct isst_id *id, int config_index,
			     struct isst_pkg_ctdp_level_info *ctdp_level)
{
	/* TBD */
	ctdp_level->pkg_max_power = 0;
	ctdp_level->pkg_min_power = 0;

	debug_printf
	    ("cpu:%d ctdp:%d CONFIG_TDP_GET_PWR_INFO pkg_max_power:%d pkg_min_power:%d\n",
	     id->cpu, config_index, ctdp_level->pkg_max_power,
	     ctdp_level->pkg_min_power);

	return 0;
}

int tpmi_get_coremask_info(struct isst_id *id, int config_index,
			   struct isst_pkg_ctdp_level_info *ctdp_level)
{
	struct isst_perf_level_cpu_mask info;
	int ret, cpu_count;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.level = config_index;
	info.punit_cpu_map = 1;

	ret = tpmi_process_ioctl(ISST_IF_GET_PERF_LEVEL_CPU_MASK, &info);
	if (ret == -1)
		return ret;

	set_cpu_mask_from_punit_coremask(id, info.mask,
					 ctdp_level->core_cpumask_size,
					 ctdp_level->core_cpumask, &cpu_count);
	ctdp_level->cpu_count = cpu_count;

	debug_printf("cpu:%d ctdp:%d core_mask ino cpu count:%d\n",
		     id->cpu, config_index, ctdp_level->cpu_count);

	return 0;
}

static int tpmi_get_get_trls(struct isst_id *id, int config_index,
			     struct isst_pkg_ctdp_level_info *ctdp_level)
{
	struct isst_perf_level_data_info info;
	int ret, i, j;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.level = config_index;

	ret = tpmi_process_ioctl(ISST_IF_GET_PERF_LEVEL_INFO, &info);
	if (ret == -1)
		return ret;

	if (info.max_buckets > TRL_MAX_BUCKETS)
		info.max_buckets = TRL_MAX_BUCKETS;

	if (info.max_trl_levels > TRL_MAX_LEVELS)
		info.max_trl_levels = TRL_MAX_LEVELS;

	for (i = 0; i < info.max_trl_levels; ++i)
		for (j = 0; j < info.max_buckets; ++j)
			ctdp_level->trl_ratios[i][j] = info.trl_freq_mhz[i][j];

	return 0;
}

static int tpmi_get_get_trl(struct isst_id *id, int level, int config_index,
			    int *trl)
{
	struct isst_pkg_ctdp_level_info ctdp_level;
	int ret, i;

	ret = tpmi_get_get_trls(id, config_index, &ctdp_level);
	if (ret)
		return ret;

	/* FIX ME: Just return for level 0 */
	for (i = 0; i < 8; ++i)
		trl[i] = ctdp_level.trl_ratios[0][i];

	return 0;
}

static int tpmi_get_trl_bucket_info(struct isst_id *id, int config_index,
				    unsigned long long *buckets_info)
{
	struct isst_perf_level_data_info info;
	unsigned char *mask = (unsigned char *)buckets_info;
	int ret, i;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.level = config_index;

	ret = tpmi_process_ioctl(ISST_IF_GET_PERF_LEVEL_INFO, &info);
	if (ret == -1)
		return ret;

	if (info.max_buckets > TRL_MAX_BUCKETS)
		info.max_buckets = TRL_MAX_BUCKETS;

	for (i = 0; i < info.max_buckets; ++i)
		mask[i] = info.bucket_core_counts[i];

	debug_printf("cpu:%d TRL bucket info: 0x%llx\n", id->cpu,
		     *buckets_info);

	return 0;
}

static int tpmi_set_tdp_level(struct isst_id *id, int tdp_level)
{
	struct isst_perf_level_control info;
	int ret;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.level = tdp_level;

	ret = tpmi_process_ioctl(ISST_IF_PERF_SET_LEVEL, &info);
	if (ret == -1)
		return ret;

	return 0;
}

static int _pbf_get_coremask_info(struct isst_id *id, int config_index,
				  struct isst_pbf_info *pbf_info)
{
	struct isst_perf_level_cpu_mask info;
	int ret, cpu_count;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.level = config_index;
	info.punit_cpu_map = 1;

	ret = tpmi_process_ioctl(ISST_IF_GET_BASE_FREQ_CPU_MASK, &info);
	if (ret == -1)
		return ret;

	set_cpu_mask_from_punit_coremask(id, info.mask,
					 pbf_info->core_cpumask_size,
					 pbf_info->core_cpumask, &cpu_count);

	debug_printf("cpu:%d ctdp:%d pbf core_mask info cpu count:%d\n",
		     id->cpu, config_index, cpu_count);

	return 0;
}

static int tpmi_get_pbf_info(struct isst_id *id, int level,
			     struct isst_pbf_info *pbf_info)
{
	struct isst_base_freq_info info;
	int ret;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.level = level;

	ret = tpmi_process_ioctl(ISST_IF_GET_BASE_FREQ_INFO, &info);
	if (ret == -1)
		return ret;

	pbf_info->p1_low = info.low_base_freq_mhz;
	pbf_info->p1_high = info.high_base_freq_mhz;
	pbf_info->tdp = info.thermal_design_power_w;
	pbf_info->t_prochot = info.tjunction_max_c;

	debug_printf("cpu:%d ctdp:%d pbf info:%d:%d:%d:%d\n",
		     id->cpu, level, pbf_info->p1_low, pbf_info->p1_high,
		     pbf_info->tdp, pbf_info->t_prochot);

	return _pbf_get_coremask_info(id, level, pbf_info);
}

static int tpmi_set_pbf_fact_status(struct isst_id *id, int pbf, int enable)
{
	struct isst_pkg_ctdp pkg_dev;
	struct isst_pkg_ctdp_level_info ctdp_level;
	int current_level;
	struct isst_perf_feature_control info;
	int ret;

	ret = isst_get_ctdp_levels(id, &pkg_dev);
	if (ret)
		debug_printf("cpu:%d No support for dynamic ISST\n", id->cpu);

	current_level = pkg_dev.current_level;

	ret = isst_get_ctdp_control(id, current_level, &ctdp_level);
	if (ret)
		return ret;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;

	info.feature = 0;

	if (pbf) {
		if (ctdp_level.fact_enabled)
			info.feature |= BIT(1);

		if (enable)
			info.feature |= BIT(0);
		else
			info.feature &= ~BIT(0);
	} else {

		if (enable && !ctdp_level.sst_cp_enabled)
			isst_display_error_info_message(0,
							"Make sure to execute before: core-power enable",
							0, 0);

		if (ctdp_level.pbf_enabled)
			info.feature |= BIT(0);

		if (enable)
			info.feature |= BIT(1);
		else
			info.feature &= ~BIT(1);
	}

	ret = tpmi_process_ioctl(ISST_IF_PERF_SET_FEATURE, &info);
	if (ret == -1)
		return ret;

	return 0;
}

static int tpmi_get_fact_info(struct isst_id *id, int level, int fact_bucket,
			      struct isst_fact_info *fact_info)
{
	struct isst_turbo_freq_info info;
	int i, j;
	int ret;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.level = level;

	ret = tpmi_process_ioctl(ISST_IF_GET_TURBO_FREQ_INFO, &info);
	if (ret == -1)
		return ret;

	for (i = 0; i < info.max_clip_freqs; ++i)
		fact_info->lp_ratios[i] = info.lp_clip_freq_mhz[i];

	if (info.max_buckets > TRL_MAX_BUCKETS)
		info.max_buckets = TRL_MAX_BUCKETS;

	if (info.max_trl_levels > TRL_MAX_LEVELS)
		info.max_trl_levels = TRL_MAX_LEVELS;

	for (i = 0; i < info.max_trl_levels; ++i) {
		for (j = 0; j < info.max_buckets; ++j)
			fact_info->bucket_info[j].hp_ratios[i] =
			    info.trl_freq_mhz[i][j];
	}

	for (i = 0; i < info.max_buckets; ++i)
		fact_info->bucket_info[i].hp_cores = info.bucket_core_counts[i];

	return 0;
}

static void _set_uncore_min_max(struct isst_id *id, int max, int freq)
{
	DIR *dir;
	FILE *filep;
	struct dirent *entry;
	char buffer[512];
	unsigned int tmp_id;
	int ret;

	dir = opendir("/sys/devices/system/cpu/intel_uncore_frequency/");
	if (!dir)
		return;

	while ((entry = readdir(dir)) != NULL ) {
		/* Check domain_id */
		snprintf(buffer, sizeof(buffer),
			 "/sys/devices/system/cpu/intel_uncore_frequency/%s/domain_id", entry->d_name);

		filep = fopen(buffer, "r");
		if (!filep)
			goto end;

		ret = fscanf(filep, "%u", &tmp_id);
		fclose(filep);
		if (ret != 1)
			goto end;

		if (tmp_id != id->punit)
			continue;

		/* Check package_id */
		snprintf(buffer, sizeof(buffer),
			 "/sys/devices/system/cpu/intel_uncore_frequency/%s/package_id", entry->d_name);

		filep = fopen(buffer, "r");
		if (!filep)
			goto end;

		ret = fscanf(filep, "%u", &tmp_id);
		fclose(filep);

		if (ret != 1)
			goto end;

		if (tmp_id != id->pkg)
			continue;

		/* Found the right sysfs path, adjust and quit */
		if (max)
			snprintf(buffer, sizeof(buffer),
				 "/sys/devices/system/cpu/intel_uncore_frequency/%s/max_freq_khz", entry->d_name);
		 else
			snprintf(buffer, sizeof(buffer),
				 "/sys/devices/system/cpu/intel_uncore_frequency/%s/min_freq_khz", entry->d_name);

		filep = fopen(buffer, "w");
		if (!filep)
			goto end;

		fprintf(filep, "%d\n", freq);
		fclose(filep);
		break;
	}

end:
	closedir(dir);
}

static void tpmi_adjust_uncore_freq(struct isst_id *id, int config_index,
				struct isst_pkg_ctdp_level_info *ctdp_level)
{
	struct isst_perf_level_data_info info;
	int ret;

	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.level = config_index;

	ret = tpmi_process_ioctl(ISST_IF_GET_PERF_LEVEL_INFO, &info);
	if (ret == -1)
		return;

	ctdp_level->uncore_p0 = info.p0_fabric_freq_mhz;
	ctdp_level->uncore_p1 = info.p1_fabric_freq_mhz;
	ctdp_level->uncore_pm = info.pm_fabric_freq_mhz;

	if (ctdp_level->uncore_pm)
		_set_uncore_min_max(id, 0, ctdp_level->uncore_pm * 100000);

	if (ctdp_level->uncore_p0)
		_set_uncore_min_max(id, 1, ctdp_level->uncore_p0 * 100000);

	return;
}

static int tpmi_get_clos_information(struct isst_id *id, int *enable, int *type)
{
	struct isst_core_power info;
	int ret;

	info.get_set = 0;
	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	ret = tpmi_process_ioctl(ISST_IF_CORE_POWER_STATE, &info);
	if (ret == -1)
		return ret;

	*enable = info.enable;
	*type = info.priority_type;

	return 0;
}

static int tpmi_pm_qos_config(struct isst_id *id, int enable_clos,
			      int priority_type)
{
	struct isst_core_power info;
	int ret;

	info.get_set = 1;
	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.enable = enable_clos;
	info.priority_type = priority_type;
	ret = tpmi_process_ioctl(ISST_IF_CORE_POWER_STATE, &info);
	if (ret == -1)
		return ret;

	return 0;
}

int tpmi_pm_get_clos(struct isst_id *id, int clos,
		     struct isst_clos_config *clos_config)
{
	struct isst_clos_param info;
	int ret;

	info.get_set = 0;
	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.clos = clos;

	ret = tpmi_process_ioctl(ISST_IF_CLOS_PARAM, &info);
	if (ret == -1)
		return ret;

	clos_config->epp = 0;
	clos_config->clos_prop_prio = info.prop_prio;
	clos_config->clos_min = info.min_freq_mhz;
	clos_config->clos_max = info.max_freq_mhz;
	clos_config->clos_desired = 0;

	debug_printf("cpu:%d clos:%d min:%d max:%d\n", id->cpu, clos,
		     clos_config->clos_min, clos_config->clos_max);

	return 0;
}

int tpmi_set_clos(struct isst_id *id, int clos,
		  struct isst_clos_config *clos_config)
{
	struct isst_clos_param info;
	int ret;

	info.get_set = 1;
	info.socket_id = id->pkg;
	info.power_domain_id = id->punit;
	info.clos = clos;
	info.prop_prio = clos_config->clos_prop_prio;

	info.min_freq_mhz = clos_config->clos_min;
	info.max_freq_mhz = clos_config->clos_max;

	if (info.min_freq_mhz <= 0xff)
		info.min_freq_mhz *= 100;
	if (info.max_freq_mhz <= 0xff)
		info.max_freq_mhz *= 100;

	ret = tpmi_process_ioctl(ISST_IF_CLOS_PARAM, &info);
	if (ret == -1)
		return ret;

	debug_printf("set cpu:%d clos:%d min:%d max:%d\n", id->cpu, clos,
		     clos_config->clos_min, clos_config->clos_max);

	return 0;
}

static int tpmi_clos_get_assoc_status(struct isst_id *id, int *clos_id)
{
	struct isst_if_clos_assoc_cmds assoc_cmds;
	int ret;

	assoc_cmds.cmd_count = 1;
	assoc_cmds.get_set = 0;
	assoc_cmds.punit_cpu_map = 1;
	assoc_cmds.assoc_info[0].logical_cpu = find_phy_core_num(id->cpu);
	assoc_cmds.assoc_info[0].socket_id = id->pkg;
	assoc_cmds.assoc_info[0].power_domain_id = id->punit;

	ret = tpmi_process_ioctl(ISST_IF_CLOS_ASSOC, &assoc_cmds);
	if (ret == -1)
		return ret;

	*clos_id = assoc_cmds.assoc_info[0].clos;

	return 0;
}

static int tpmi_clos_associate(struct isst_id *id, int clos_id)
{
	struct isst_if_clos_assoc_cmds assoc_cmds;
	int ret;

	assoc_cmds.cmd_count = 1;
	assoc_cmds.get_set = 1;
	assoc_cmds.punit_cpu_map = 1;
	assoc_cmds.assoc_info[0].logical_cpu = find_phy_core_num(id->cpu);
	assoc_cmds.assoc_info[0].clos = clos_id;
	assoc_cmds.assoc_info[0].socket_id = id->pkg;
	assoc_cmds.assoc_info[0].power_domain_id = id->punit;

	ret = tpmi_process_ioctl(ISST_IF_CLOS_ASSOC, &assoc_cmds);
	if (ret == -1)
		return ret;

	return 0;
}

static struct isst_platform_ops tpmi_ops = {
	.get_disp_freq_multiplier = tpmi_get_disp_freq_multiplier,
	.get_trl_max_levels = tpmi_get_trl_max_levels,
	.get_trl_level_name = tpmi_get_trl_level_name,
	.update_platform_param = tpmi_update_platform_param,
	.is_punit_valid = tpmi_is_punit_valid,
	.read_pm_config = tpmi_read_pm_config,
	.get_config_levels = tpmi_get_config_levels,
	.get_ctdp_control = tpmi_get_ctdp_control,
	.get_tdp_info = tpmi_get_tdp_info,
	.get_pwr_info = tpmi_get_pwr_info,
	.get_coremask_info = tpmi_get_coremask_info,
	.get_get_trl = tpmi_get_get_trl,
	.get_get_trls = tpmi_get_get_trls,
	.get_trl_bucket_info = tpmi_get_trl_bucket_info,
	.set_tdp_level = tpmi_set_tdp_level,
	.get_pbf_info = tpmi_get_pbf_info,
	.set_pbf_fact_status = tpmi_set_pbf_fact_status,
	.get_fact_info = tpmi_get_fact_info,
	.adjust_uncore_freq = tpmi_adjust_uncore_freq,
	.get_clos_information = tpmi_get_clos_information,
	.pm_qos_config = tpmi_pm_qos_config,
	.pm_get_clos = tpmi_pm_get_clos,
	.set_clos = tpmi_set_clos,
	.clos_get_assoc_status = tpmi_clos_get_assoc_status,
	.clos_associate = tpmi_clos_associate,
};

struct isst_platform_ops *tpmi_get_platform_ops(void)
{
	return &tpmi_ops;
}
