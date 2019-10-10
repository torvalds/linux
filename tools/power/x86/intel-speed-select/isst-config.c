// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select -- Enumerate and control features
 * Copyright (c) 2019 Intel Corporation.
 */

#include <linux/isst_if.h>

#include "isst.h"

struct process_cmd_struct {
	char *feature;
	char *command;
	void (*process_fn)(void);
};

static const char *version_str = "v1.0";
static const int supported_api_ver = 1;
static struct isst_if_platform_info isst_platform_info;
static char *progname;
static int debug_flag;
static FILE *outf;

static int cpu_model;

#define MAX_CPUS_IN_ONE_REQ 64
static short max_target_cpus;
static unsigned short target_cpus[MAX_CPUS_IN_ONE_REQ];

static int topo_max_cpus;
static size_t present_cpumask_size;
static cpu_set_t *present_cpumask;
static size_t target_cpumask_size;
static cpu_set_t *target_cpumask;
static int tdp_level = 0xFF;
static int fact_bucket = 0xFF;
static int fact_avx = 0xFF;
static unsigned long long fact_trl;
static int out_format_json;
static int cmd_help;
static int force_online_offline;

/* clos related */
static int current_clos = -1;
static int clos_epp = -1;
static int clos_prop_prio = -1;
static int clos_min = -1;
static int clos_max = -1;
static int clos_desired = -1;
static int clos_priority_type;

struct _cpu_map {
	unsigned short core_id;
	unsigned short pkg_id;
	unsigned short die_id;
	unsigned short punit_cpu;
	unsigned short punit_cpu_core;
};
struct _cpu_map *cpu_map;

void debug_printf(const char *format, ...)
{
	va_list args;

	va_start(args, format);

	if (debug_flag)
		vprintf(format, args);

	va_end(args);
}

static void update_cpu_model(void)
{
	unsigned int ebx, ecx, edx;
	unsigned int fms, family;

	__cpuid(1, fms, ebx, ecx, edx);
	family = (fms >> 8) & 0xf;
	cpu_model = (fms >> 4) & 0xf;
	if (family == 6 || family == 0xf)
		cpu_model += ((fms >> 16) & 0xf) << 4;
}

/* Open a file, and exit on failure */
static FILE *fopen_or_exit(const char *path, const char *mode)
{
	FILE *filep = fopen(path, mode);

	if (!filep)
		err(1, "%s: open failed", path);

	return filep;
}

/* Parse a file containing a single int */
static int parse_int_file(int fatal, const char *fmt, ...)
{
	va_list args;
	char path[PATH_MAX];
	FILE *filep;
	int value;

	va_start(args, fmt);
	vsnprintf(path, sizeof(path), fmt, args);
	va_end(args);
	if (fatal) {
		filep = fopen_or_exit(path, "r");
	} else {
		filep = fopen(path, "r");
		if (!filep)
			return -1;
	}
	if (fscanf(filep, "%d", &value) != 1)
		err(1, "%s: failed to parse number from file", path);
	fclose(filep);

	return value;
}

int cpufreq_sysfs_present(void)
{
	DIR *dir;

	dir = opendir("/sys/devices/system/cpu/cpu0/cpufreq");
	if (dir) {
		closedir(dir);
		return 1;
	}

	return 0;
}

int out_format_is_json(void)
{
	return out_format_json;
}

int get_physical_package_id(int cpu)
{
	return parse_int_file(
		0, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id",
		cpu);
}

int get_physical_core_id(int cpu)
{
	return parse_int_file(
		0, "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
}

int get_physical_die_id(int cpu)
{
	int ret;

	ret = parse_int_file(0, "/sys/devices/system/cpu/cpu%d/topology/die_id",
			     cpu);
	if (ret < 0)
		ret = 0;

	return ret;
}

int get_topo_max_cpus(void)
{
	return topo_max_cpus;
}

static void set_cpu_online_offline(int cpu, int state)
{
	char buffer[128];
	int fd, ret;

	snprintf(buffer, sizeof(buffer),
		 "/sys/devices/system/cpu/cpu%d/online", cpu);

	fd = open(buffer, O_WRONLY);
	if (fd < 0)
		err(-1, "%s open failed", buffer);

	if (state)
		ret = write(fd, "1\n", 2);
	else
		ret = write(fd, "0\n", 2);

	if (ret == -1)
		perror("Online/Offline: Operation failed\n");

	close(fd);
}

#define MAX_PACKAGE_COUNT 8
#define MAX_DIE_PER_PACKAGE 2
static void for_each_online_package_in_set(void (*callback)(int, void *, void *,
							    void *, void *),
					   void *arg1, void *arg2, void *arg3,
					   void *arg4)
{
	int max_packages[MAX_PACKAGE_COUNT * MAX_PACKAGE_COUNT];
	int pkg_index = 0, i;

	memset(max_packages, 0xff, sizeof(max_packages));
	for (i = 0; i < topo_max_cpus; ++i) {
		int j, online, pkg_id, die_id = 0, skip = 0;

		if (!CPU_ISSET_S(i, present_cpumask_size, present_cpumask))
			continue;
		if (i)
			online = parse_int_file(
				1, "/sys/devices/system/cpu/cpu%d/online", i);
		else
			online =
				1; /* online entry for CPU 0 needs some special configs */

		die_id = get_physical_die_id(i);
		if (die_id < 0)
			die_id = 0;
		pkg_id = get_physical_package_id(i);
		/* Create an unique id for package, die combination to store */
		pkg_id = (MAX_PACKAGE_COUNT * pkg_id + die_id);

		for (j = 0; j < pkg_index; ++j) {
			if (max_packages[j] == pkg_id) {
				skip = 1;
				break;
			}
		}

		if (!skip && online && callback) {
			callback(i, arg1, arg2, arg3, arg4);
			max_packages[pkg_index++] = pkg_id;
		}
	}
}

static void for_each_online_target_cpu_in_set(
	void (*callback)(int, void *, void *, void *, void *), void *arg1,
	void *arg2, void *arg3, void *arg4)
{
	int i;

	for (i = 0; i < topo_max_cpus; ++i) {
		int online;

		if (!CPU_ISSET_S(i, target_cpumask_size, target_cpumask))
			continue;
		if (i)
			online = parse_int_file(
				1, "/sys/devices/system/cpu/cpu%d/online", i);
		else
			online =
				1; /* online entry for CPU 0 needs some special configs */

		if (online && callback)
			callback(i, arg1, arg2, arg3, arg4);
	}
}

#define BITMASK_SIZE 32
static void set_max_cpu_num(void)
{
	FILE *filep;
	unsigned long dummy;

	topo_max_cpus = 0;
	filep = fopen_or_exit(
		"/sys/devices/system/cpu/cpu0/topology/thread_siblings", "r");
	while (fscanf(filep, "%lx,", &dummy) == 1)
		topo_max_cpus += BITMASK_SIZE;
	fclose(filep);
	topo_max_cpus--; /* 0 based */

	debug_printf("max cpus %d\n", topo_max_cpus);
}

size_t alloc_cpu_set(cpu_set_t **cpu_set)
{
	cpu_set_t *_cpu_set;
	size_t size;

	_cpu_set = CPU_ALLOC((topo_max_cpus + 1));
	if (_cpu_set == NULL)
		err(3, "CPU_ALLOC");
	size = CPU_ALLOC_SIZE((topo_max_cpus + 1));
	CPU_ZERO_S(size, _cpu_set);

	*cpu_set = _cpu_set;
	return size;
}

void free_cpu_set(cpu_set_t *cpu_set)
{
	CPU_FREE(cpu_set);
}

static int cpu_cnt[MAX_PACKAGE_COUNT][MAX_DIE_PER_PACKAGE];
static void set_cpu_present_cpu_mask(void)
{
	size_t size;
	DIR *dir;
	int i;

	size = alloc_cpu_set(&present_cpumask);
	present_cpumask_size = size;
	for (i = 0; i < topo_max_cpus; ++i) {
		char buffer[256];

		snprintf(buffer, sizeof(buffer),
			 "/sys/devices/system/cpu/cpu%d", i);
		dir = opendir(buffer);
		if (dir) {
			int pkg_id, die_id;

			CPU_SET_S(i, size, present_cpumask);
			die_id = get_physical_die_id(i);
			if (die_id < 0)
				die_id = 0;

			pkg_id = get_physical_package_id(i);
			if (pkg_id < MAX_PACKAGE_COUNT &&
			    die_id < MAX_DIE_PER_PACKAGE)
				cpu_cnt[pkg_id][die_id]++;
		}
		closedir(dir);
	}
}

int get_cpu_count(int pkg_id, int die_id)
{
	if (pkg_id < MAX_PACKAGE_COUNT && die_id < MAX_DIE_PER_PACKAGE)
		return cpu_cnt[pkg_id][die_id];

	return 0;
}

static void set_cpu_target_cpu_mask(void)
{
	size_t size;
	int i;

	size = alloc_cpu_set(&target_cpumask);
	target_cpumask_size = size;
	for (i = 0; i < max_target_cpus; ++i) {
		if (!CPU_ISSET_S(target_cpus[i], present_cpumask_size,
				 present_cpumask))
			continue;

		CPU_SET_S(target_cpus[i], size, target_cpumask);
	}
}

static void create_cpu_map(void)
{
	const char *pathname = "/dev/isst_interface";
	int i, fd = 0;
	struct isst_if_cpu_maps map;

	cpu_map = malloc(sizeof(*cpu_map) * topo_max_cpus);
	if (!cpu_map)
		err(3, "cpumap");

	fd = open(pathname, O_RDWR);
	if (fd < 0)
		err(-1, "%s open failed", pathname);

	for (i = 0; i < topo_max_cpus; ++i) {
		if (!CPU_ISSET_S(i, present_cpumask_size, present_cpumask))
			continue;

		map.cmd_count = 1;
		map.cpu_map[0].logical_cpu = i;

		debug_printf(" map logical_cpu:%d\n",
			     map.cpu_map[0].logical_cpu);
		if (ioctl(fd, ISST_IF_GET_PHY_ID, &map) == -1) {
			perror("ISST_IF_GET_PHY_ID");
			fprintf(outf, "Error: map logical_cpu:%d\n",
				map.cpu_map[0].logical_cpu);
			continue;
		}
		cpu_map[i].core_id = get_physical_core_id(i);
		cpu_map[i].pkg_id = get_physical_package_id(i);
		cpu_map[i].die_id = get_physical_die_id(i);
		cpu_map[i].punit_cpu = map.cpu_map[0].physical_cpu;
		cpu_map[i].punit_cpu_core = (map.cpu_map[0].physical_cpu >>
					     1); // shift to get core id

		debug_printf(
			"map logical_cpu:%d core: %d die:%d pkg:%d punit_cpu:%d punit_core:%d\n",
			i, cpu_map[i].core_id, cpu_map[i].die_id,
			cpu_map[i].pkg_id, cpu_map[i].punit_cpu,
			cpu_map[i].punit_cpu_core);
	}

	if (fd)
		close(fd);
}

int find_logical_cpu(int pkg_id, int die_id, int punit_core_id)
{
	int i;

	for (i = 0; i < topo_max_cpus; ++i) {
		if (cpu_map[i].pkg_id == pkg_id &&
		    cpu_map[i].die_id == die_id &&
		    cpu_map[i].punit_cpu_core == punit_core_id)
			return i;
	}

	return -EINVAL;
}

void set_cpu_mask_from_punit_coremask(int cpu, unsigned long long core_mask,
				      size_t core_cpumask_size,
				      cpu_set_t *core_cpumask, int *cpu_cnt)
{
	int i, cnt = 0;
	int die_id, pkg_id;

	*cpu_cnt = 0;
	die_id = get_physical_die_id(cpu);
	pkg_id = get_physical_package_id(cpu);

	for (i = 0; i < 64; ++i) {
		if (core_mask & BIT(i)) {
			int j;

			for (j = 0; j < topo_max_cpus; ++j) {
				if (!CPU_ISSET_S(j, present_cpumask_size, present_cpumask))
					continue;

				if (cpu_map[j].pkg_id == pkg_id &&
				    cpu_map[j].die_id == die_id &&
				    cpu_map[j].punit_cpu_core == i) {
					CPU_SET_S(j, core_cpumask_size,
						  core_cpumask);
					++cnt;
				}
			}
		}
	}

	*cpu_cnt = cnt;
}

int find_phy_core_num(int logical_cpu)
{
	if (logical_cpu < topo_max_cpus)
		return cpu_map[logical_cpu].punit_cpu_core;

	return -EINVAL;
}

static int isst_send_mmio_command(unsigned int cpu, unsigned int reg, int write,
				  unsigned int *value)
{
	struct isst_if_io_regs io_regs;
	const char *pathname = "/dev/isst_interface";
	int cmd;
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
		perror("ISST_IF_IO_CMD");
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
	int fd;
	struct isst_if_mbox_cmds mbox_cmds = { 0 };

	debug_printf(
		"mbox_send: cpu:%d command:%x sub_command:%x parameter:%x req_data:%x\n",
		cpu, command, sub_command, parameter, req_data);

	if (isst_platform_info.mmio_supported && command == CONFIG_CLOS) {
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
		case CLOS_PM_QOS_CONFIG:
			ret = isst_send_mmio_command(cpu, PM_QOS_CONFIG_OFFSET,
						     write, &value);
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

	fd = open(pathname, O_RDWR);
	if (fd < 0)
		err(-1, "%s open failed", pathname);

	if (ioctl(fd, ISST_IF_MBOX_COMMAND, &mbox_cmds) == -1) {
		perror("ISST_IF_MBOX_COMMAND");
		fprintf(outf,
			"Error: mbox_cmd cpu:%d command:%x sub_command:%x parameter:%x req_data:%x\n",
			cpu, command, sub_command, parameter, req_data);
	} else {
		*resp = mbox_cmds.mbox_cmd[0].resp_data;
		debug_printf(
			"mbox_cmd response: cpu:%d command:%x sub_command:%x parameter:%x req_data:%x resp:%x\n",
			cpu, command, sub_command, parameter, req_data, *resp);
	}

	close(fd);

	return 0;
}

int isst_send_msr_command(unsigned int cpu, unsigned int msr, int write,
			  unsigned long long *req_resp)
{
	struct isst_if_msr_cmds msr_cmds;
	const char *pathname = "/dev/isst_interface";
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
		perror("ISST_IF_MSR_COMMAD");
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

static int isst_fill_platform_info(void)
{
	const char *pathname = "/dev/isst_interface";
	int fd;

	fd = open(pathname, O_RDWR);
	if (fd < 0)
		err(-1, "%s open failed", pathname);

	if (ioctl(fd, ISST_IF_GET_PLATFORM_INFO, &isst_platform_info) == -1) {
		perror("ISST_IF_GET_PLATFORM_INFO");
		close(fd);
		return -1;
	}

	close(fd);

	if (isst_platform_info.api_version > supported_api_ver) {
		printf("Incompatible API versions; Upgrade of tool is required\n");
		return -1;
	}
	return 0;
}

static void isst_print_platform_information(void)
{
	struct isst_if_platform_info platform_info;
	const char *pathname = "/dev/isst_interface";
	int fd;

	fd = open(pathname, O_RDWR);
	if (fd < 0)
		err(-1, "%s open failed", pathname);

	if (ioctl(fd, ISST_IF_GET_PLATFORM_INFO, &platform_info) == -1) {
		perror("ISST_IF_GET_PLATFORM_INFO");
	} else {
		fprintf(outf, "Platform: API version : %d\n",
			platform_info.api_version);
		fprintf(outf, "Platform: Driver version : %d\n",
			platform_info.driver_version);
		fprintf(outf, "Platform: mbox supported : %d\n",
			platform_info.mbox_supported);
		fprintf(outf, "Platform: mmio supported : %d\n",
			platform_info.mmio_supported);
	}

	close(fd);

	exit(0);
}

static void exec_on_get_ctdp_cpu(int cpu, void *arg1, void *arg2, void *arg3,
				 void *arg4)
{
	int (*fn_ptr)(int cpu, void *arg);
	int ret;

	fn_ptr = arg1;
	ret = fn_ptr(cpu, arg2);
	if (ret)
		perror("get_tdp_*");
	else
		isst_ctdp_display_core_info(cpu, outf, arg3,
					    *(unsigned int *)arg4);
}

#define _get_tdp_level(desc, suffix, object, help)                                \
	static void get_tdp_##object(void)                                        \
	{                                                                         \
		struct isst_pkg_ctdp ctdp;                                        \
\
		if (cmd_help) {                                                   \
			fprintf(stderr,                                           \
				"Print %s [No command arguments are required]\n", \
				help);                                            \
			exit(0);                                                  \
		}                                                                 \
		isst_ctdp_display_information_start(outf);                        \
		if (max_target_cpus)                                              \
			for_each_online_target_cpu_in_set(                        \
				exec_on_get_ctdp_cpu, isst_get_ctdp_##suffix,     \
				&ctdp, desc, &ctdp.object);                       \
		else                                                              \
			for_each_online_package_in_set(exec_on_get_ctdp_cpu,      \
						       isst_get_ctdp_##suffix,    \
						       &ctdp, desc,               \
						       &ctdp.object);             \
		isst_ctdp_display_information_end(outf);                          \
	}

_get_tdp_level("get-config-levels", levels, levels, "TDP levels");
_get_tdp_level("get-config-version", levels, version, "TDP version");
_get_tdp_level("get-config-enabled", levels, enabled, "TDP enable status");
_get_tdp_level("get-config-current_level", levels, current_level,
	       "Current TDP Level");
_get_tdp_level("get-lock-status", levels, locked, "TDP lock status");

static void dump_isst_config_for_cpu(int cpu, void *arg1, void *arg2,
				     void *arg3, void *arg4)
{
	struct isst_pkg_ctdp pkg_dev;
	int ret;

	memset(&pkg_dev, 0, sizeof(pkg_dev));
	ret = isst_get_process_ctdp(cpu, tdp_level, &pkg_dev);
	if (ret) {
		perror("isst_get_process_ctdp");
	} else {
		isst_ctdp_display_information(cpu, outf, tdp_level, &pkg_dev);
		isst_get_process_ctdp_complete(cpu, &pkg_dev);
	}
}

static void dump_isst_config(void)
{
	if (cmd_help) {
		fprintf(stderr,
			"Print Intel(R) Speed Select Technology Performance profile configuration\n");
		fprintf(stderr,
			"including base frequency and turbo frequency configurations\n");
		fprintf(stderr, "Optional: -l|--level : Specify tdp level\n");
		fprintf(stderr,
			"\tIf no arguments, dump information for all TDP levels\n");
		exit(0);
	}

	isst_ctdp_display_information_start(outf);

	if (max_target_cpus)
		for_each_online_target_cpu_in_set(dump_isst_config_for_cpu,
						  NULL, NULL, NULL, NULL);
	else
		for_each_online_package_in_set(dump_isst_config_for_cpu, NULL,
					       NULL, NULL, NULL);

	isst_ctdp_display_information_end(outf);
}

static void set_tdp_level_for_cpu(int cpu, void *arg1, void *arg2, void *arg3,
				  void *arg4)
{
	int ret;

	ret = isst_set_tdp_level(cpu, tdp_level);
	if (ret)
		perror("set_tdp_level_for_cpu");
	else {
		isst_display_result(cpu, outf, "perf-profile", "set_tdp_level",
				    ret);
		if (force_online_offline) {
			struct isst_pkg_ctdp_level_info ctdp_level;
			int pkg_id = get_physical_package_id(cpu);
			int die_id = get_physical_die_id(cpu);

			fprintf(stderr, "Option is set to online/offline\n");
			ctdp_level.core_cpumask_size =
				alloc_cpu_set(&ctdp_level.core_cpumask);
			isst_get_coremask_info(cpu, tdp_level, &ctdp_level);
			if (ctdp_level.cpu_count) {
				int i, max_cpus = get_topo_max_cpus();
				for (i = 0; i < max_cpus; ++i) {
					if (pkg_id != get_physical_package_id(i) || die_id != get_physical_die_id(i))
						continue;
					if (CPU_ISSET_S(i, ctdp_level.core_cpumask_size, ctdp_level.core_cpumask)) {
						fprintf(stderr, "online cpu %d\n", i);
						set_cpu_online_offline(i, 1);
					} else {
						fprintf(stderr, "offline cpu %d\n", i);
						set_cpu_online_offline(i, 0);
					}
				}
			}
		}
	}
}

static void set_tdp_level(void)
{
	if (cmd_help) {
		fprintf(stderr, "Set Config TDP level\n");
		fprintf(stderr,
			"\t Arguments: -l|--level : Specify tdp level\n");
		fprintf(stderr,
			"\t Optional Arguments: -o | online : online/offline for the tdp level\n");
		exit(0);
	}

	if (tdp_level == 0xff) {
		fprintf(outf, "Invalid command: specify tdp_level\n");
		exit(1);
	}
	isst_ctdp_display_information_start(outf);
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(set_tdp_level_for_cpu, NULL,
						  NULL, NULL, NULL);
	else
		for_each_online_package_in_set(set_tdp_level_for_cpu, NULL,
					       NULL, NULL, NULL);
	isst_ctdp_display_information_end(outf);
}

static void dump_pbf_config_for_cpu(int cpu, void *arg1, void *arg2, void *arg3,
				    void *arg4)
{
	struct isst_pbf_info pbf_info;
	int ret;

	ret = isst_get_pbf_info(cpu, tdp_level, &pbf_info);
	if (ret) {
		perror("isst_get_pbf_info");
	} else {
		isst_pbf_display_information(cpu, outf, tdp_level, &pbf_info);
		isst_get_pbf_info_complete(&pbf_info);
	}
}

static void dump_pbf_config(void)
{
	if (cmd_help) {
		fprintf(stderr,
			"Print Intel(R) Speed Select Technology base frequency configuration for a TDP level\n");
		fprintf(stderr,
			"\tArguments: -l|--level : Specify tdp level\n");
		exit(0);
	}

	if (tdp_level == 0xff) {
		fprintf(outf, "Invalid command: specify tdp_level\n");
		exit(1);
	}

	isst_ctdp_display_information_start(outf);
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(dump_pbf_config_for_cpu, NULL,
						  NULL, NULL, NULL);
	else
		for_each_online_package_in_set(dump_pbf_config_for_cpu, NULL,
					       NULL, NULL, NULL);
	isst_ctdp_display_information_end(outf);
}

static void set_pbf_for_cpu(int cpu, void *arg1, void *arg2, void *arg3,
			    void *arg4)
{
	int ret;
	int status = *(int *)arg4;

	ret = isst_set_pbf_fact_status(cpu, 1, status);
	if (ret) {
		perror("isst_set_pbf");
	} else {
		if (status)
			isst_display_result(cpu, outf, "base-freq", "enable",
					    ret);
		else
			isst_display_result(cpu, outf, "base-freq", "disable",
					    ret);
	}
}

static void set_pbf_enable(void)
{
	int status = 1;

	if (cmd_help) {
		fprintf(stderr,
			"Enable Intel Speed Select Technology base frequency feature [No command arguments are required]\n");
		exit(0);
	}

	isst_ctdp_display_information_start(outf);
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(set_pbf_for_cpu, NULL, NULL,
						  NULL, &status);
	else
		for_each_online_package_in_set(set_pbf_for_cpu, NULL, NULL,
					       NULL, &status);
	isst_ctdp_display_information_end(outf);
}

static void set_pbf_disable(void)
{
	int status = 0;

	if (cmd_help) {
		fprintf(stderr,
			"Disable Intel Speed Select Technology base frequency feature [No command arguments are required]\n");
		exit(0);
	}

	isst_ctdp_display_information_start(outf);
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(set_pbf_for_cpu, NULL, NULL,
						  NULL, &status);
	else
		for_each_online_package_in_set(set_pbf_for_cpu, NULL, NULL,
					       NULL, &status);
	isst_ctdp_display_information_end(outf);
}

static void dump_fact_config_for_cpu(int cpu, void *arg1, void *arg2,
				     void *arg3, void *arg4)
{
	struct isst_fact_info fact_info;
	int ret;

	ret = isst_get_fact_info(cpu, tdp_level, &fact_info);
	if (ret)
		perror("isst_get_fact_bucket_info");
	else
		isst_fact_display_information(cpu, outf, tdp_level, fact_bucket,
					      fact_avx, &fact_info);
}

static void dump_fact_config(void)
{
	if (cmd_help) {
		fprintf(stderr,
			"Print complete Intel Speed Select Technology turbo frequency configuration for a TDP level. Other arguments are optional.\n");
		fprintf(stderr,
			"\tArguments: -l|--level : Specify tdp level\n");
		fprintf(stderr,
			"\tArguments: -b|--bucket : Bucket index to dump\n");
		fprintf(stderr,
			"\tArguments: -r|--trl-type : Specify trl type: sse|avx2|avx512\n");
		exit(0);
	}

	if (tdp_level == 0xff) {
		fprintf(outf, "Invalid command: specify tdp_level\n");
		exit(1);
	}

	isst_ctdp_display_information_start(outf);
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(dump_fact_config_for_cpu,
						  NULL, NULL, NULL, NULL);
	else
		for_each_online_package_in_set(dump_fact_config_for_cpu, NULL,
					       NULL, NULL, NULL);
	isst_ctdp_display_information_end(outf);
}

static void set_fact_for_cpu(int cpu, void *arg1, void *arg2, void *arg3,
			     void *arg4)
{
	int ret;
	int status = *(int *)arg4;

	ret = isst_set_pbf_fact_status(cpu, 0, status);
	if (ret)
		perror("isst_set_fact");
	else {
		if (status) {
			struct isst_pkg_ctdp pkg_dev;

			ret = isst_get_ctdp_levels(cpu, &pkg_dev);
			if (ret) {
				isst_display_result(cpu, outf, "turbo-freq",
						    "enable", ret);
				return;
			}
			ret = isst_set_trl(cpu, fact_trl);
			isst_display_result(cpu, outf, "turbo-freq", "enable",
					    ret);
		} else {
			/* Since we modified TRL during Fact enable, restore it */
			isst_set_trl_from_current_tdp(cpu, fact_trl);
			isst_display_result(cpu, outf, "turbo-freq", "disable",
					    ret);
		}
	}
}

static void set_fact_enable(void)
{
	int status = 1;

	if (cmd_help) {
		fprintf(stderr,
			"Enable Intel Speed Select Technology Turbo frequency feature\n");
		fprintf(stderr,
			"Optional: -t|--trl : Specify turbo ratio limit\n");
		exit(0);
	}

	isst_ctdp_display_information_start(outf);
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(set_fact_for_cpu, NULL, NULL,
						  NULL, &status);
	else
		for_each_online_package_in_set(set_fact_for_cpu, NULL, NULL,
					       NULL, &status);
	isst_ctdp_display_information_end(outf);
}

static void set_fact_disable(void)
{
	int status = 0;

	if (cmd_help) {
		fprintf(stderr,
			"Disable Intel Speed Select Technology turbo frequency feature\n");
		fprintf(stderr,
			"Optional: -t|--trl : Specify turbo ratio limit\n");
		exit(0);
	}

	isst_ctdp_display_information_start(outf);
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(set_fact_for_cpu, NULL, NULL,
						  NULL, &status);
	else
		for_each_online_package_in_set(set_fact_for_cpu, NULL, NULL,
					       NULL, &status);
	isst_ctdp_display_information_end(outf);
}

static void enable_clos_qos_config(int cpu, void *arg1, void *arg2, void *arg3,
				   void *arg4)
{
	int ret;
	int status = *(int *)arg4;

	ret = isst_pm_qos_config(cpu, status, clos_priority_type);
	if (ret) {
		perror("isst_pm_qos_config");
	} else {
		if (status)
			isst_display_result(cpu, outf, "core-power", "enable",
					    ret);
		else
			isst_display_result(cpu, outf, "core-power", "disable",
					    ret);
	}
}

static void set_clos_enable(void)
{
	int status = 1;

	if (cmd_help) {
		fprintf(stderr, "Enable core-power for a package/die\n");
		fprintf(stderr,
			"\tClos Enable: Specify priority type with [--priority|-p]\n");
		fprintf(stderr, "\t\t 0: Proportional, 1: Ordered\n");
		exit(0);
	}

	if (cpufreq_sysfs_present()) {
		fprintf(stderr,
			"cpufreq subsystem and core-power enable will interfere with each other!\n");
	}

	isst_ctdp_display_information_start(outf);
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(enable_clos_qos_config, NULL,
						  NULL, NULL, &status);
	else
		for_each_online_package_in_set(enable_clos_qos_config, NULL,
					       NULL, NULL, &status);
	isst_ctdp_display_information_end(outf);
}

static void set_clos_disable(void)
{
	int status = 0;

	if (cmd_help) {
		fprintf(stderr,
			"Disable core-power: [No command arguments are required]\n");
		exit(0);
	}

	isst_ctdp_display_information_start(outf);
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(enable_clos_qos_config, NULL,
						  NULL, NULL, &status);
	else
		for_each_online_package_in_set(enable_clos_qos_config, NULL,
					       NULL, NULL, &status);
	isst_ctdp_display_information_end(outf);
}

static void dump_clos_config_for_cpu(int cpu, void *arg1, void *arg2,
				     void *arg3, void *arg4)
{
	struct isst_clos_config clos_config;
	int ret;

	ret = isst_pm_get_clos(cpu, current_clos, &clos_config);
	if (ret)
		perror("isst_pm_get_clos");
	else
		isst_clos_display_information(cpu, outf, current_clos,
					      &clos_config);
}

static void dump_clos_config(void)
{
	if (cmd_help) {
		fprintf(stderr,
			"Print Intel Speed Select Technology core power configuration\n");
		fprintf(stderr,
			"\tArguments: [-c | --clos]: Specify clos id\n");
		exit(0);
	}
	if (current_clos < 0 || current_clos > 3) {
		fprintf(stderr, "Invalid clos id\n");
		exit(0);
	}

	isst_ctdp_display_information_start(outf);
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(dump_clos_config_for_cpu,
						  NULL, NULL, NULL, NULL);
	else
		for_each_online_package_in_set(dump_clos_config_for_cpu, NULL,
					       NULL, NULL, NULL);
	isst_ctdp_display_information_end(outf);
}

static void get_clos_info_for_cpu(int cpu, void *arg1, void *arg2, void *arg3,
				  void *arg4)
{
	int enable, ret, prio_type;

	ret = isst_clos_get_clos_information(cpu, &enable, &prio_type);
	if (ret)
		perror("isst_clos_get_info");
	else
		isst_clos_display_clos_information(cpu, outf, enable, prio_type);
}

static void dump_clos_info(void)
{
	if (cmd_help) {
		fprintf(stderr,
			"Print Intel Speed Select Technology core power information\n");
		fprintf(stderr, "\tSpecify targeted cpu id with [--cpu|-c]\n");
		exit(0);
	}

	if (!max_target_cpus) {
		fprintf(stderr,
			"Invalid target cpu. Specify with [-c|--cpu]\n");
		exit(0);
	}

	isst_ctdp_display_information_start(outf);
	for_each_online_target_cpu_in_set(get_clos_info_for_cpu, NULL,
					  NULL, NULL, NULL);
	isst_ctdp_display_information_end(outf);

}

static void set_clos_config_for_cpu(int cpu, void *arg1, void *arg2, void *arg3,
				    void *arg4)
{
	struct isst_clos_config clos_config;
	int ret;

	clos_config.pkg_id = get_physical_package_id(cpu);
	clos_config.die_id = get_physical_die_id(cpu);

	clos_config.epp = clos_epp;
	clos_config.clos_prop_prio = clos_prop_prio;
	clos_config.clos_min = clos_min;
	clos_config.clos_max = clos_max;
	clos_config.clos_desired = clos_desired;
	ret = isst_set_clos(cpu, current_clos, &clos_config);
	if (ret)
		perror("isst_set_clos");
	else
		isst_display_result(cpu, outf, "core-power", "config", ret);
}

static void set_clos_config(void)
{
	if (cmd_help) {
		fprintf(stderr,
			"Set core-power configuration for one of the four clos ids\n");
		fprintf(stderr,
			"\tSpecify targeted clos id with [--clos|-c]\n");
		fprintf(stderr, "\tSpecify clos EPP with [--epp|-e]\n");
		fprintf(stderr,
			"\tSpecify clos Proportional Priority [--weight|-w]\n");
		fprintf(stderr, "\tSpecify clos min with [--min|-n]\n");
		fprintf(stderr, "\tSpecify clos max with [--max|-m]\n");
		fprintf(stderr, "\tSpecify clos desired with [--desired|-d]\n");
		exit(0);
	}

	if (current_clos < 0 || current_clos > 3) {
		fprintf(stderr, "Invalid clos id\n");
		exit(0);
	}
	if (clos_epp < 0 || clos_epp > 0x0F) {
		fprintf(stderr, "clos epp is not specified, default: 0\n");
		clos_epp = 0;
	}
	if (clos_prop_prio < 0 || clos_prop_prio > 0x0F) {
		fprintf(stderr,
			"clos frequency weight is not specified, default: 0\n");
		clos_prop_prio = 0;
	}
	if (clos_min < 0) {
		fprintf(stderr, "clos min is not specified, default: 0\n");
		clos_min = 0;
	}
	if (clos_max < 0) {
		fprintf(stderr, "clos max is not specified, default: 0xff\n");
		clos_max = 0xff;
	}
	if (clos_desired < 0) {
		fprintf(stderr, "clos desired is not specified, default: 0\n");
		clos_desired = 0x00;
	}

	isst_ctdp_display_information_start(outf);
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(set_clos_config_for_cpu, NULL,
						  NULL, NULL, NULL);
	else
		for_each_online_package_in_set(set_clos_config_for_cpu, NULL,
					       NULL, NULL, NULL);
	isst_ctdp_display_information_end(outf);
}

static void set_clos_assoc_for_cpu(int cpu, void *arg1, void *arg2, void *arg3,
				   void *arg4)
{
	int ret;

	ret = isst_clos_associate(cpu, current_clos);
	if (ret)
		perror("isst_clos_associate");
	else
		isst_display_result(cpu, outf, "core-power", "assoc", ret);
}

static void set_clos_assoc(void)
{
	if (cmd_help) {
		fprintf(stderr, "Associate a clos id to a CPU\n");
		fprintf(stderr,
			"\tSpecify targeted clos id with [--clos|-c]\n");
		exit(0);
	}

	if (current_clos < 0 || current_clos > 3) {
		fprintf(stderr, "Invalid clos id\n");
		exit(0);
	}
	if (max_target_cpus)
		for_each_online_target_cpu_in_set(set_clos_assoc_for_cpu, NULL,
						  NULL, NULL, NULL);
	else {
		fprintf(stderr,
			"Invalid target cpu. Specify with [-c|--cpu]\n");
	}
}

static void get_clos_assoc_for_cpu(int cpu, void *arg1, void *arg2, void *arg3,
				   void *arg4)
{
	int clos, ret;

	ret = isst_clos_get_assoc_status(cpu, &clos);
	if (ret)
		perror("isst_clos_get_assoc_status");
	else
		isst_clos_display_assoc_information(cpu, outf, clos);
}

static void get_clos_assoc(void)
{
	if (cmd_help) {
		fprintf(stderr, "Get associate clos id to a CPU\n");
		fprintf(stderr, "\tSpecify targeted cpu id with [--cpu|-c]\n");
		exit(0);
	}

	if (!max_target_cpus) {
		fprintf(stderr,
			"Invalid target cpu. Specify with [-c|--cpu]\n");
		exit(0);
	}

	isst_ctdp_display_information_start(outf);
	for_each_online_target_cpu_in_set(get_clos_assoc_for_cpu, NULL,
					  NULL, NULL, NULL);
	isst_ctdp_display_information_end(outf);
}

static struct process_cmd_struct isst_cmds[] = {
	{ "perf-profile", "get-lock-status", get_tdp_locked },
	{ "perf-profile", "get-config-levels", get_tdp_levels },
	{ "perf-profile", "get-config-version", get_tdp_version },
	{ "perf-profile", "get-config-enabled", get_tdp_enabled },
	{ "perf-profile", "get-config-current-level", get_tdp_current_level },
	{ "perf-profile", "set-config-level", set_tdp_level },
	{ "perf-profile", "info", dump_isst_config },
	{ "base-freq", "info", dump_pbf_config },
	{ "base-freq", "enable", set_pbf_enable },
	{ "base-freq", "disable", set_pbf_disable },
	{ "turbo-freq", "info", dump_fact_config },
	{ "turbo-freq", "enable", set_fact_enable },
	{ "turbo-freq", "disable", set_fact_disable },
	{ "core-power", "info", dump_clos_info },
	{ "core-power", "enable", set_clos_enable },
	{ "core-power", "disable", set_clos_disable },
	{ "core-power", "config", set_clos_config },
	{ "core-power", "get-config", dump_clos_config },
	{ "core-power", "assoc", set_clos_assoc },
	{ "core-power", "get-assoc", get_clos_assoc },
	{ NULL, NULL, NULL }
};

/*
 * parse cpuset with following syntax
 * 1,2,4..6,8-10 and set bits in cpu_subset
 */
void parse_cpu_command(char *optarg)
{
	unsigned int start, end;
	char *next;

	next = optarg;

	while (next && *next) {
		if (*next == '-') /* no negative cpu numbers */
			goto error;

		start = strtoul(next, &next, 10);

		if (max_target_cpus < MAX_CPUS_IN_ONE_REQ)
			target_cpus[max_target_cpus++] = start;

		if (*next == '\0')
			break;

		if (*next == ',') {
			next += 1;
			continue;
		}

		if (*next == '-') {
			next += 1; /* start range */
		} else if (*next == '.') {
			next += 1;
			if (*next == '.')
				next += 1; /* start range */
			else
				goto error;
		}

		end = strtoul(next, &next, 10);
		if (end <= start)
			goto error;

		while (++start <= end) {
			if (max_target_cpus < MAX_CPUS_IN_ONE_REQ)
				target_cpus[max_target_cpus++] = start;
		}

		if (*next == ',')
			next += 1;
		else if (*next != '\0')
			goto error;
	}

#ifdef DEBUG
	{
		int i;

		for (i = 0; i < max_target_cpus; ++i)
			printf("cpu [%d] in arg\n", target_cpus[i]);
	}
#endif
	return;

error:
	fprintf(stderr, "\"--cpu %s\" malformed\n", optarg);
	exit(-1);
}

static void parse_cmd_args(int argc, int start, char **argv)
{
	int opt;
	int option_index;

	static struct option long_options[] = {
		{ "bucket", required_argument, 0, 'b' },
		{ "level", required_argument, 0, 'l' },
		{ "online", required_argument, 0, 'o' },
		{ "trl-type", required_argument, 0, 'r' },
		{ "trl", required_argument, 0, 't' },
		{ "help", no_argument, 0, 'h' },
		{ "clos", required_argument, 0, 'c' },
		{ "desired", required_argument, 0, 'd' },
		{ "epp", required_argument, 0, 'e' },
		{ "min", required_argument, 0, 'n' },
		{ "max", required_argument, 0, 'm' },
		{ "priority", required_argument, 0, 'p' },
		{ "weight", required_argument, 0, 'w' },
		{ 0, 0, 0, 0 }
	};

	option_index = start;

	optind = start + 1;
	while ((opt = getopt_long(argc, argv, "b:l:t:c:d:e:n:m:p:w:ho",
				  long_options, &option_index)) != -1) {
		switch (opt) {
		case 'b':
			fact_bucket = atoi(optarg);
			break;
		case 'h':
			cmd_help = 1;
			break;
		case 'l':
			tdp_level = atoi(optarg);
			break;
		case 'o':
			force_online_offline = 1;
			break;
		case 't':
			sscanf(optarg, "0x%llx", &fact_trl);
			break;
		case 'r':
			if (!strncmp(optarg, "sse", 3)) {
				fact_avx = 0x01;
			} else if (!strncmp(optarg, "avx2", 4)) {
				fact_avx = 0x02;
			} else if (!strncmp(optarg, "avx512", 4)) {
				fact_avx = 0x04;
			} else {
				fprintf(outf, "Invalid sse,avx options\n");
				exit(1);
			}
			break;
		/* CLOS related */
		case 'c':
			current_clos = atoi(optarg);
			break;
		case 'd':
			clos_desired = atoi(optarg);
			break;
		case 'e':
			clos_epp = atoi(optarg);
			break;
		case 'n':
			clos_min = atoi(optarg);
			break;
		case 'm':
			clos_max = atoi(optarg);
			break;
		case 'p':
			clos_priority_type = atoi(optarg);
			break;
		case 'w':
			clos_prop_prio = atoi(optarg);
			break;
		default:
			printf("no match\n");
		}
	}
}

static void isst_help(void)
{
	printf("perf-profile:\tAn architectural mechanism that allows multiple optimized \n\
		performance profiles per system via static and/or dynamic\n\
		adjustment of core count, workload, Tjmax, and\n\
		TDP, etc.\n");
	printf("\nCommands : For feature=perf-profile\n");
	printf("\tinfo\n");
	printf("\tget-lock-status\n");
	printf("\tget-config-levels\n");
	printf("\tget-config-version\n");
	printf("\tget-config-enabled\n");
	printf("\tget-config-current-level\n");
	printf("\tset-config-level\n");
}

static void pbf_help(void)
{
	printf("base-freq:\tEnables users to increase guaranteed base frequency\n\
		on certain cores (high priority cores) in exchange for lower\n\
		base frequency on remaining cores (low priority cores).\n");
	printf("\tcommand : info\n");
	printf("\tcommand : enable\n");
	printf("\tcommand : disable\n");
}

static void fact_help(void)
{
	printf("turbo-freq:\tEnables the ability to set different turbo ratio\n\
		limits to cores based on priority.\n");
	printf("\nCommand: For feature=turbo-freq\n");
	printf("\tcommand : info\n");
	printf("\tcommand : enable\n");
	printf("\tcommand : disable\n");
}

static void core_power_help(void)
{
	printf("core-power:\tInterface that allows user to define per core/tile\n\
		priority.\n");
	printf("\nCommands : For feature=core-power\n");
	printf("\tinfo\n");
	printf("\tenable\n");
	printf("\tdisable\n");
	printf("\tconfig\n");
	printf("\tget-config\n");
	printf("\tassoc\n");
	printf("\tget-assoc\n");
}

struct process_cmd_help_struct {
	char *feature;
	void (*process_fn)(void);
};

static struct process_cmd_help_struct isst_help_cmds[] = {
	{ "perf-profile", isst_help },
	{ "base-freq", pbf_help },
	{ "turbo-freq", fact_help },
	{ "core-power", core_power_help },
	{ NULL, NULL }
};

void process_command(int argc, char **argv)
{
	int i = 0, matched = 0;
	char *feature = argv[optind];
	char *cmd = argv[optind + 1];

	if (!feature || !cmd)
		return;

	debug_printf("feature name [%s] command [%s]\n", feature, cmd);
	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
		while (isst_help_cmds[i].feature) {
			if (!strcmp(isst_help_cmds[i].feature, feature)) {
				isst_help_cmds[i].process_fn();
				exit(0);
			}
			++i;
		}
	}

	create_cpu_map();

	i = 0;
	while (isst_cmds[i].feature) {
		if (!strcmp(isst_cmds[i].feature, feature) &&
		    !strcmp(isst_cmds[i].command, cmd)) {
			parse_cmd_args(argc, optind + 1, argv);
			isst_cmds[i].process_fn();
			matched = 1;
			break;
		}
		++i;
	}

	if (!matched)
		fprintf(stderr, "Invalid command\n");
}

static void usage(void)
{
	printf("Intel(R) Speed Select Technology\n");
	printf("\nUsage:\n");
	printf("intel-speed-select [OPTIONS] FEATURE COMMAND COMMAND_ARGUMENTS\n");
	printf("\nUse this tool to enumerate and control the Intel Speed Select Technology features,\n");
	printf("\nFEATURE : [perf-profile|base-freq|turbo-freq|core-power]\n");
	printf("\nFor help on each feature, use -h|--help\n");
	printf("\tFor example:  intel-speed-select perf-profile -h\n");

	printf("\nFor additional help on each command for a feature, use --h|--help\n");
	printf("\tFor example:  intel-speed-select perf-profile get-lock-status -h\n");
	printf("\t\t This will print help for the command \"get-lock-status\" for the feature \"perf-profile\"\n");

	printf("\nOPTIONS\n");
	printf("\t[-c|--cpu] : logical cpu number\n");
	printf("\t\tDefault: Die scoped for all dies in the system with multiple dies/package\n");
	printf("\t\t\t Or Package scoped for all Packages when each package contains one die\n");
	printf("\t[-d|--debug] : Debug mode\n");
	printf("\t[-h|--help] : Print help\n");
	printf("\t[-i|--info] : Print platform information\n");
	printf("\t[-o|--out] : Output file\n");
	printf("\t\t\tDefault : stderr\n");
	printf("\t[-f|--format] : output format [json|text]. Default: text\n");
	printf("\t[-v|--version] : Print version\n");

	printf("\nResult format\n");
	printf("\tResult display uses a common format for each command:\n");
	printf("\tResults are formatted in text/JSON with\n");
	printf("\t\tPackage, Die, CPU, and command specific results.\n");
	exit(1);
}

static void print_version(void)
{
	fprintf(outf, "Version %s\n", version_str);
	fprintf(outf, "Build date %s time %s\n", __DATE__, __TIME__);
	exit(0);
}

static void cmdline(int argc, char **argv)
{
	int opt;
	int option_index = 0;
	int ret;

	static struct option long_options[] = {
		{ "cpu", required_argument, 0, 'c' },
		{ "debug", no_argument, 0, 'd' },
		{ "format", required_argument, 0, 'f' },
		{ "help", no_argument, 0, 'h' },
		{ "info", no_argument, 0, 'i' },
		{ "out", required_argument, 0, 'o' },
		{ "version", no_argument, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	progname = argv[0];
	while ((opt = getopt_long_only(argc, argv, "+c:df:hio:v", long_options,
				       &option_index)) != -1) {
		switch (opt) {
		case 'c':
			parse_cpu_command(optarg);
			break;
		case 'd':
			debug_flag = 1;
			printf("Debug Mode ON\n");
			break;
		case 'f':
			if (!strncmp(optarg, "json", 4))
				out_format_json = 1;
			break;
		case 'h':
			usage();
			break;
		case 'i':
			isst_print_platform_information();
			break;
		case 'o':
			if (outf)
				fclose(outf);
			outf = fopen_or_exit(optarg, "w");
			break;
		case 'v':
			print_version();
			break;
		default:
			usage();
		}
	}

	if (geteuid() != 0) {
		fprintf(stderr, "Must run as root\n");
		exit(0);
	}

	if (optind > (argc - 2)) {
		fprintf(stderr, "Feature name and|or command not specified\n");
		exit(0);
	}
	update_cpu_model();
	printf("Intel(R) Speed Select Technology\n");
	printf("Executing on CPU model:%d[0x%x]\n", cpu_model, cpu_model);
	set_max_cpu_num();
	set_cpu_present_cpu_mask();
	set_cpu_target_cpu_mask();
	ret = isst_fill_platform_info();
	if (ret)
		goto out;

	process_command(argc, argv);
out:
	free_cpu_set(present_cpumask);
	free_cpu_set(target_cpumask);
}

int main(int argc, char **argv)
{
	outf = stderr;
	cmdline(argc, argv);
	return 0;
}
