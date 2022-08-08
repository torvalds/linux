// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select -- Enumerate and control features
 * Copyright (c) 2019 Intel Corporation.
 */

#include "isst.h"

static int mbox_delay;
static int mbox_retries = 3;

static struct isst_platform_ops		*isst_ops;

#define CHECK_CB(_name)	\
	do {	\
		if (!isst_ops || !isst_ops->_name) {	\
			fprintf(stderr, "Invalid ops\n");	\
			exit(0);	\
		}	\
	} while (0)

int isst_set_platform_ops(void)
{
	isst_ops = mbox_get_platform_ops();

	if (!isst_ops)
		return -1;
	return 0;
}

void isst_update_platform_param(enum isst_platform_param param, int value)
{
	switch (param) {
	case ISST_PARAM_MBOX_DELAY:
		mbox_delay = value;
		break;
	case ISST_PARAM_MBOX_RETRIES:
		mbox_retries = value;
		break;
	default:
		break;
	}
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

static int isst_send_mmio_command(unsigned int cpu, unsigned int reg, int write,
				  unsigned int *value)
{
	struct isst_if_io_regs io_regs;
	const char *pathname = "/dev/isst_interface";
	int cmd;
	FILE *outf = get_output_file();
	int fd;

	debug_printf("mmio_cmd cpu:%d reg:%d write:%d\n", cpu, reg, write);

	fd = open(pathname, O_RDWR);
	if (fd < 0)
		err(-1, "%s open failed", pathname);

	io_regs.req_count = 1;
	io_regs.io_reg[0].logical_cpu = cpu;
	io_regs.io_reg[0].reg = reg;
	cmd = ISST_IF_IO_CMD;
	if (write) {
		io_regs.io_reg[0].read_write = 1;
		io_regs.io_reg[0].value = *value;
	} else {
		io_regs.io_reg[0].read_write = 0;
	}

	if (ioctl(fd, cmd, &io_regs) == -1) {
		if (errno == ENOTTY) {
			perror("ISST_IF_IO_COMMAND\n");
			fprintf(stderr, "Check presence of kernel modules: isst_if_mmio\n");
			exit(0);
		}
		fprintf(outf, "Error: mmio_cmd cpu:%d reg:%x read_write:%x\n",
			cpu, reg, write);
	} else {
		if (!write)
			*value = io_regs.io_reg[0].value;

		debug_printf(
			"mmio_cmd response: cpu:%d reg:%x rd_write:%x resp:%x\n",
			cpu, reg, write, *value);
	}

	close(fd);

	return 0;
}

int isst_send_mbox_command(unsigned int cpu, unsigned char command,
			   unsigned char sub_command, unsigned int parameter,
			   unsigned int req_data, unsigned int *resp)
{
	const char *pathname = "/dev/isst_interface";
	int fd, retry;
	struct isst_if_mbox_cmds mbox_cmds = { 0 };

	debug_printf(
		"mbox_send: cpu:%d command:%x sub_command:%x parameter:%x req_data:%x\n",
		cpu, command, sub_command, parameter, req_data);

	if (!is_skx_based_platform() && command == CONFIG_CLOS &&
	    sub_command != CLOS_PM_QOS_CONFIG) {
		unsigned int value;
		int write = 0;
		int clos_id, core_id, ret = 0;

		debug_printf("CPU %d\n", cpu);

		if (parameter & BIT(MBOX_CMD_WRITE_BIT)) {
			value = req_data;
			write = 1;
		}

		switch (sub_command) {
		case CLOS_PQR_ASSOC:
			core_id = parameter & 0xff;
			ret = isst_send_mmio_command(
				cpu, PQR_ASSOC_OFFSET + core_id * 4, write,
				&value);
			if (!ret && !write)
				*resp = value;
			break;
		case CLOS_PM_CLOS:
			clos_id = parameter & 0x03;
			ret = isst_send_mmio_command(
				cpu, PM_CLOS_OFFSET + clos_id * 4, write,
				&value);
			if (!ret && !write)
				*resp = value;
			break;
		case CLOS_STATUS:
			break;
		default:
			break;
		}
		return ret;
	}

	mbox_cmds.cmd_count = 1;
	mbox_cmds.mbox_cmd[0].logical_cpu = cpu;
	mbox_cmds.mbox_cmd[0].command = command;
	mbox_cmds.mbox_cmd[0].sub_command = sub_command;
	mbox_cmds.mbox_cmd[0].parameter = parameter;
	mbox_cmds.mbox_cmd[0].req_data = req_data;

	if (mbox_delay)
		usleep(mbox_delay * 1000);

	fd = open(pathname, O_RDWR);
	if (fd < 0)
		err(-1, "%s open failed", pathname);

	retry = mbox_retries;

	do {
		if (ioctl(fd, ISST_IF_MBOX_COMMAND, &mbox_cmds) == -1) {
			if (errno == ENOTTY) {
				perror("ISST_IF_MBOX_COMMAND\n");
				fprintf(stderr, "Check presence of kernel modules: isst_if_mbox_pci or isst_if_mbox_msr\n");
				exit(0);
			}
			debug_printf(
				"Error: mbox_cmd cpu:%d command:%x sub_command:%x parameter:%x req_data:%x errorno:%d\n",
				cpu, command, sub_command, parameter, req_data, errno);
			--retry;
		} else {
			*resp = mbox_cmds.mbox_cmd[0].resp_data;
			debug_printf(
				"mbox_cmd response: cpu:%d command:%x sub_command:%x parameter:%x req_data:%x resp:%x\n",
				cpu, command, sub_command, parameter, req_data, *resp);
			break;
		}
	} while (retry);

	close(fd);

	if (!retry) {
		debug_printf("Failed mbox command even after retries\n");
		return -1;

	}
	return 0;
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

int isst_write_pm_config(struct isst_id *id, int cp_state)
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

int isst_read_pm_config(struct isst_id *id, int *cp_state, int *cp_cap)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(id->cpu, READ_PM_CONFIG, PM_FEATURE, 0, 0,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d READ_PM_CONFIG resp:%x\n", id->cpu, resp);

	*cp_state = resp & BIT(16);
	*cp_cap = resp & BIT(0) ? 1 : 0;

	return 0;
}

int isst_get_ctdp_levels(struct isst_id *id, struct isst_pkg_ctdp *pkg_dev)
{
	CHECK_CB(get_config_levels);
	return isst_ops->get_config_levels(id, pkg_dev);
}

int isst_get_ctdp_control(struct isst_id *id, int config_index,
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

int isst_get_tdp_info(struct isst_id *id, int config_index,
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
	return 0;
}

int isst_get_pwr_info(struct isst_id *id, int config_index,
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

void isst_get_uncore_p0_p1_info(struct isst_id *id, int config_index,
				struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;

	ctdp_level->uncore_pm = 0;
	ctdp_level->uncore_p0 = 0;
	ctdp_level->uncore_p1 = 0;

	ret = isst_send_mbox_command(id->cpu, CONFIG_TDP,
				     CONFIG_TDP_GET_RATIO_INFO, 0,
				     (BIT(16) | config_index), &resp);
	if (ret)
		goto try_uncore_mbox;

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

void isst_get_p1_info(struct isst_id *id, int config_index,
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

void isst_get_uncore_mem_freq(struct isst_id *id, int config_index,
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

int isst_get_tjmax_info(struct isst_id *id, int config_index,
			struct isst_pkg_ctdp_level_info *ctdp_level)
{
	unsigned int resp;
	int ret;

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

int isst_get_coremask_info(struct isst_id *id, int config_index,
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

int isst_get_trl_bucket_info(struct isst_id *id, unsigned long long *buckets_info)
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

int isst_set_tdp_level(struct isst_id *id, int tdp_level)
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

int isst_get_pbf_info(struct isst_id *id, int level, struct isst_pbf_info *pbf_info)
{
	struct isst_pkg_ctdp_level_info ctdp_level;
	struct isst_pkg_ctdp pkg_dev;
	int i, ret, max_punit_core, max_mask_index;
	unsigned int req, resp;

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

void isst_get_pbf_info_complete(struct isst_pbf_info *pbf_info)
{
	free_cpu_set(pbf_info->core_cpumask);
}

int isst_set_pbf_fact_status(struct isst_id *id, int pbf, int enable)
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

int isst_get_fact_bucket_info(struct isst_id *id, int level,
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

int isst_get_fact_info(struct isst_id *id, int level, int fact_bucket, struct isst_fact_info *fact_info)
{
	struct isst_pkg_ctdp_level_info ctdp_level;
	struct isst_pkg_ctdp pkg_dev;
	unsigned int resp;
	int j, ret, print;

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

	ret = isst_get_fact_bucket_info(id, level, fact_info->bucket_info);
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

int isst_set_trl_from_current_tdp(struct isst_id *id, unsigned long long trl)
{
	unsigned long long msr_trl;
	int ret;

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
		int trl_max_levels = isst_get_trl_max_levels();
		int j;

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
			isst_get_trl_bucket_info(id, &ctdp_level->trl_cores);
			continue;
		}

		ret = isst_get_tdp_info(id, i, ctdp_level);
		if (ret)
			return ret;

		ret = isst_get_pwr_info(id, i, ctdp_level);
		if (ret)
			return ret;

		ret = isst_get_tjmax_info(id, i, ctdp_level);
		if (ret)
			return ret;

		ctdp_level->core_cpumask_size =
			alloc_cpu_set(&ctdp_level->core_cpumask);
		ret = isst_get_coremask_info(id, i, ctdp_level);
		if (ret)
			return ret;

		ret = isst_get_trl_bucket_info(id, &ctdp_level->trl_cores);
		if (ret)
			return ret;

		for (j = 0; j < trl_max_levels; j++) {
			ret = isst_get_get_trl(id, i, j,
				       ctdp_level->trl_ratios[j]);
			if (ret)
				return ret;
		}

		isst_get_uncore_p0_p1_info(id, i, ctdp_level);
		isst_get_p1_info(id, i, ctdp_level);
		isst_get_uncore_mem_freq(id, i, ctdp_level);
	}

	if (!valid)
		isst_display_error_info_message(0, "Invalid level, Can't get TDP control information at specified levels on cpu", 1, id->cpu);

	return 0;
}

int isst_clos_get_clos_information(struct isst_id *id, int *enable, int *type)
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

int isst_pm_qos_config(struct isst_id *id, int enable_clos, int priority_type)
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
		ret = isst_write_pm_config(id, 0);
		if (ret)
			isst_display_error_info_message(0, "WRITE_PM_CONFIG command failed, ignoring error", 0, 0);
	} else {
		ret = isst_write_pm_config(id, 1);
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

int isst_pm_get_clos(struct isst_id *id, int clos, struct isst_clos_config *clos_config)
{
	unsigned int resp;
	int ret;

	ret = isst_send_mbox_command(id->cpu, CONFIG_CLOS, CLOS_PM_CLOS, clos, 0,
				     &resp);
	if (ret)
		return ret;

	clos_config->epp = resp & 0x0f;
	clos_config->clos_prop_prio = (resp >> 4) & 0x0f;
	clos_config->clos_min = (resp >> 8) & 0xff;
	clos_config->clos_max = (resp >> 16) & 0xff;
	clos_config->clos_desired = (resp >> 24) & 0xff;

	return 0;
}

int isst_set_clos(struct isst_id *id, int clos, struct isst_clos_config *clos_config)
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

	ret = isst_send_mbox_command(id->cpu, CONFIG_CLOS, CLOS_PM_CLOS, param, req,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CLOS_PM_CLOS param:%x req:%x\n", id->cpu, param, req);

	return 0;
}

int isst_clos_get_assoc_status(struct isst_id *id, int *clos_id)
{
	unsigned int resp;
	unsigned int param;
	int core_id, ret;

	core_id = find_phy_core_num(id->cpu);
	param = core_id;

	ret = isst_send_mbox_command(id->cpu, CONFIG_CLOS, CLOS_PQR_ASSOC, param, 0,
				     &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CLOS_PQR_ASSOC param:%x resp:%x\n", id->cpu, param,
		     resp);
	*clos_id = (resp >> 16) & 0x03;

	return 0;
}

int isst_clos_associate(struct isst_id *id, int clos_id)
{
	unsigned int req, resp;
	unsigned int param;
	int core_id, ret;

	req = (clos_id & 0x03) << 16;
	core_id = find_phy_core_num(id->cpu);
	param = BIT(MBOX_CMD_WRITE_BIT) | core_id;

	ret = isst_send_mbox_command(id->cpu, CONFIG_CLOS, CLOS_PQR_ASSOC, param,
				     req, &resp);
	if (ret)
		return ret;

	debug_printf("cpu:%d CLOS_PQR_ASSOC param:%x req:%x\n", id->cpu, param,
		     req);

	return 0;
}
