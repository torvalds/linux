/*
 * CPPC (Collaborative Processor Performance Control) methods used
 * by CPUfreq drivers.
 *
 * (C) Copyright 2014, 2015 Linaro Ltd.
 * Author: Ashwin Chaugule <ashwin.chaugule@linaro.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef _CPPC_ACPI_H
#define _CPPC_ACPI_H

#include <linux/acpi.h>
#include <linux/types.h>

#include <acpi/pcc.h>
#include <acpi/processor.h>

/* Support CPPCv2 and CPPCv3  */
#define CPPC_V2_REV	2
#define CPPC_V3_REV	3
#define CPPC_V2_NUM_ENT	21
#define CPPC_V3_NUM_ENT	23

#define PCC_CMD_COMPLETE_MASK	(1 << 0)
#define PCC_ERROR_MASK		(1 << 2)

#define MAX_CPC_REG_ENT 21

/* CPPC specific PCC commands. */
#define	CMD_READ 0
#define	CMD_WRITE 1

/* Each register has the folowing format. */
struct cpc_reg {
	u8 descriptor;
	u16 length;
	u8 space_id;
	u8 bit_width;
	u8 bit_offset;
	u8 access_width;
	u64 __iomem address;
} __packed;

/*
 * Each entry in the CPC table is either
 * of type ACPI_TYPE_BUFFER or
 * ACPI_TYPE_INTEGER.
 */
struct cpc_register_resource {
	acpi_object_type type;
	u64 __iomem *sys_mem_vaddr;
	union {
		struct cpc_reg reg;
		u64 int_value;
	} cpc_entry;
};

/* Container to hold the CPC details for each CPU */
struct cpc_desc {
	int num_entries;
	int version;
	int cpu_id;
	int write_cmd_status;
	int write_cmd_id;
	struct cpc_register_resource cpc_regs[MAX_CPC_REG_ENT];
	struct acpi_psd_package domain_info;
	struct kobject kobj;
};

/* These are indexes into the per-cpu cpc_regs[]. Order is important. */
enum cppc_regs {
	HIGHEST_PERF,
	NOMINAL_PERF,
	LOW_NON_LINEAR_PERF,
	LOWEST_PERF,
	GUARANTEED_PERF,
	DESIRED_PERF,
	MIN_PERF,
	MAX_PERF,
	PERF_REDUC_TOLERANCE,
	TIME_WINDOW,
	CTR_WRAP_TIME,
	REFERENCE_CTR,
	DELIVERED_CTR,
	PERF_LIMITED,
	ENABLE,
	AUTO_SEL_ENABLE,
	AUTO_ACT_WINDOW,
	ENERGY_PERF,
	REFERENCE_PERF,
	LOWEST_FREQ,
	NOMINAL_FREQ,
};

/*
 * Categorization of registers as described
 * in the ACPI v.5.1 spec.
 * XXX: Only filling up ones which are used by governors
 * today.
 */
struct cppc_perf_caps {
	u32 highest_perf;
	u32 nominal_perf;
	u32 lowest_perf;
	u32 lowest_nonlinear_perf;
	u32 lowest_freq;
	u32 nominal_freq;
};

struct cppc_perf_ctrls {
	u32 max_perf;
	u32 min_perf;
	u32 desired_perf;
};

struct cppc_perf_fb_ctrs {
	u64 reference;
	u64 delivered;
	u64 reference_perf;
	u64 wraparound_time;
};

/* Per CPU container for runtime CPPC management. */
struct cppc_cpudata {
	int cpu;
	struct cppc_perf_caps perf_caps;
	struct cppc_perf_ctrls perf_ctrls;
	struct cppc_perf_fb_ctrs perf_fb_ctrs;
	struct cpufreq_policy *cur_policy;
	unsigned int shared_type;
	cpumask_var_t shared_cpu_map;
};

extern int cppc_get_perf_ctrs(int cpu, struct cppc_perf_fb_ctrs *perf_fb_ctrs);
extern int cppc_set_perf(int cpu, struct cppc_perf_ctrls *perf_ctrls);
extern int cppc_get_perf_caps(int cpu, struct cppc_perf_caps *caps);
extern int acpi_get_psd_map(struct cppc_cpudata **);
extern unsigned int cppc_get_transition_latency(int cpu);

#endif /* _CPPC_ACPI_H*/
