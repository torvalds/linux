/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _QCOM_PMU_H
#define _QCOM_PMU_H

#include <linux/kernel.h>
#include <linux/scmi_protocol.h>
#include <linux/scmi_pmu.h>

/* (1) ccntr + (6) evcntr + (1) llcc */
#define QCOM_PMU_MAX_EVS	8
#define INVALID_PMU_HW_IDX	0xFF

struct cpucp_hlos_map {
	bool			shared;
	unsigned long		cpus;
};

struct qcom_pmu_data {
	u32			event_ids[QCOM_PMU_MAX_EVS];
	u64			ev_data[QCOM_PMU_MAX_EVS];
	u32			num_evs;
};

typedef void (*idle_fn_t)(struct qcom_pmu_data *data, int cpu, int state);
struct qcom_pmu_notif_node {
	idle_fn_t		idle_cb;
	struct list_head	node;
};

enum amu_counters {
	SYS_AMU_CONST_CYC,
	SYS_AMU_CORE_CYC,
	SYS_AMU_INST_RET,
	SYS_AMU_STALL_MEM,
	SYS_AMU_MAX,
};

#if IS_ENABLED(CONFIG_QCOM_PMU_LIB)
int qcom_pmu_event_supported(u32 event_id, int cpu);
int qcom_get_cpucp_id(u32 event_id, int cpu);
int qcom_pmu_read(int cpu, u32 event_id, u64 *pmu_data);
int qcom_pmu_read_local(u32 event_id, u64 *pmu_data);
int qcom_pmu_read_all(int cpu, struct qcom_pmu_data *data);
int qcom_pmu_read_all_local(struct qcom_pmu_data *data);
int qcom_pmu_idle_register(struct qcom_pmu_notif_node *idle_node);
int qcom_pmu_idle_unregister(struct qcom_pmu_notif_node *idle_node);
int rimps_pmu_init(struct scmi_device *sdev);
#else
static inline int qcom_pmu_event_supported(u32 event_id, int cpu)
{
	return -ENODEV;
}
static inline int qcom_get_cpucp_id(u32 event_id, int cpu)
{
	return -ENODEV;
}
static inline int qcom_pmu_read(int cpu, u32 event_id, u64 *pmu_data)
{
	return -ENODEV;
}
static inline int qcom_pmu_read_local(u32 event_id, u64 *pmu_data)
{
	return -ENODEV;
}
static inline int qcom_pmu_read_all(int cpu, struct qcom_pmu_data *data)
{
	return -ENODEV;
}
static inline int qcom_pmu_read_all_local(struct qcom_pmu_data *data)
{
	return -ENODEV;
}
static inline int qcom_pmu_idle_register(struct qcom_pmu_notif_node *idle_node)
{
	return -ENODEV;
}
static inline int qcom_pmu_idle_unregister(
					struct qcom_pmu_notif_node *idle_node)
{
	return -ENODEV;
}
static inline int rimps_pmu_init(struct scmi_device *sdev)
{
	return -ENODEV;
}
#endif

#endif /* _QCOM_PMU_H */
