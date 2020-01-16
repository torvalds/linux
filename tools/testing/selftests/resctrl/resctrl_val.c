// SPDX-License-Identifier: GPL-2.0
/*
 * Memory bandwidth monitoring and allocation library
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Authors:
 *    Sai Praneeth Prakhya <sai.praneeth.prakhya@intel.com>,
 *    Fenghua Yu <fenghua.yu@intel.com>
 */
#include "resctrl.h"

#define UNCORE_IMC		"uncore_imc"
#define READ_FILE_NAME		"events/cas_count_read"
#define WRITE_FILE_NAME		"events/cas_count_write"
#define DYN_PMU_PATH		"/sys/bus/event_source/devices"
#define SCALE			0.00006103515625
#define MAX_IMCS		20
#define MAX_TOKENS		5
#define READ			0
#define WRITE			1
#define CON_MON_MBM_LOCAL_BYTES_PATH				\
	"%s/%s/mon_groups/%s/mon_data/mon_L3_%02d/mbm_local_bytes"

#define CON_MBM_LOCAL_BYTES_PATH		\
	"%s/%s/mon_data/mon_L3_%02d/mbm_local_bytes"

#define MON_MBM_LOCAL_BYTES_PATH		\
	"%s/mon_groups/%s/mon_data/mon_L3_%02d/mbm_local_bytes"

#define MBM_LOCAL_BYTES_PATH			\
	"%s/mon_data/mon_L3_%02d/mbm_local_bytes"

struct membw_read_format {
	__u64 value;         /* The value of the event */
	__u64 time_enabled;  /* if PERF_FORMAT_TOTAL_TIME_ENABLED */
	__u64 time_running;  /* if PERF_FORMAT_TOTAL_TIME_RUNNING */
	__u64 id;            /* if PERF_FORMAT_ID */
};

struct imc_counter_config {
	__u32 type;
	__u64 event;
	__u64 umask;
	struct perf_event_attr pe;
	struct membw_read_format return_value;
	int fd;
};

static struct imc_counter_config imc_counters_config[MAX_IMCS][2];

void membw_initialize_perf_event_attr(int i, int j)
{
	memset(&imc_counters_config[i][j].pe, 0,
	       sizeof(struct perf_event_attr));
	imc_counters_config[i][j].pe.type = imc_counters_config[i][j].type;
	imc_counters_config[i][j].pe.size = sizeof(struct perf_event_attr);
	imc_counters_config[i][j].pe.disabled = 1;
	imc_counters_config[i][j].pe.inherit = 1;
	imc_counters_config[i][j].pe.exclude_guest = 0;
	imc_counters_config[i][j].pe.config =
		imc_counters_config[i][j].umask << 8 |
		imc_counters_config[i][j].event;
	imc_counters_config[i][j].pe.sample_type = PERF_SAMPLE_IDENTIFIER;
	imc_counters_config[i][j].pe.read_format =
		PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
}

void membw_ioctl_perf_event_ioc_reset_enable(int i, int j)
{
	ioctl(imc_counters_config[i][j].fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(imc_counters_config[i][j].fd, PERF_EVENT_IOC_ENABLE, 0);
}

void membw_ioctl_perf_event_ioc_disable(int i, int j)
{
	ioctl(imc_counters_config[i][j].fd, PERF_EVENT_IOC_DISABLE, 0);
}

/*
 * get_event_and_umask:	Parse config into event and umask
 * @cas_count_cfg:	Config
 * @count:		iMC number
 * @op:			Operation (read/write)
 */
void get_event_and_umask(char *cas_count_cfg, int count, bool op)
{
	char *token[MAX_TOKENS];
	int i = 0;

	strcat(cas_count_cfg, ",");
	token[0] = strtok(cas_count_cfg, "=,");

	for (i = 1; i < MAX_TOKENS; i++)
		token[i] = strtok(NULL, "=,");

	for (i = 0; i < MAX_TOKENS; i++) {
		if (!token[i])
			break;
		if (strcmp(token[i], "event") == 0) {
			if (op == READ)
				imc_counters_config[count][READ].event =
				strtol(token[i + 1], NULL, 16);
			else
				imc_counters_config[count][WRITE].event =
				strtol(token[i + 1], NULL, 16);
		}
		if (strcmp(token[i], "umask") == 0) {
			if (op == READ)
				imc_counters_config[count][READ].umask =
				strtol(token[i + 1], NULL, 16);
			else
				imc_counters_config[count][WRITE].umask =
				strtol(token[i + 1], NULL, 16);
		}
	}
}
