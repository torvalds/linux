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

void isst_get_uncore_p0_p1_info(struct isst_id *id, int config_index,
				struct isst_pkg_ctdp_level_info *ctdp_level)
{
	CHECK_CB(get_uncore_p0_p1_info);
	return isst_ops->get_uncore_p0_p1_info(id, config_index, ctdp_level);
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

		for (j = 0; j < trl_max_levels; j++) {
			ret = isst_get_get_trl(id, i, j,
				       ctdp_level->trl_ratios[j]);
			if (ret)
				return ret;
		}
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
