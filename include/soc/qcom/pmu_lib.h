/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_PMU_H
#define _QCOM_PMU_H

#include <linux/kernel.h>
#include <linux/scmi_protocol.h>
#if !IS_ENABLED(CONFIG_QTI_SCMI_VENDOR_PROTOCOL)
#include <linux/scmi_pmu.h>
#endif

/* (1) ccntr + (6) evcntr + (1) llcc */
#define QCOM_PMU_MAX_EVS	8
#define INVALID_PMU_HW_IDX	0xFF

enum cpucp_ev_idx {
	CPU_CYC_EVT = 0,
	CNT_CYC_EVT,
	INST_RETIRED_EVT,
	STALL_BACKEND_EVT,
	L2D_CACHE_REFILL_EVT,
	L2D_WB_EVT,
	L3_CACHE_REFILL_EVT,
	L3_ACCESS_EVT,
	LLCC_CACHE_REFILL_EVT,
	MAX_CPUCP_EVT,
};

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
int cpucp_pmu_init(struct scmi_device *sdev);
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
