/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SOC_QCOM_CRM_H__
#define __SOC_QCOM_CRM_H__

#include <linux/platform_device.h>

/**
 * enum crm_hw_drv_state:   Progressive higher power states for the HW DRV request
 *
 * @CRM_PWR_STATE0:         Power State0
 * @CRM_PWR_STATE1:         Power State1
 * @CRM_PWR_STATE2:         Power State2
 * @CRM_PWR_STATE3:         Power State3
 * @CRM_PWR_STATE4:         Power State4
 */
enum crm_hw_drv_state {
	CRM_PWR_STATE0,
	CRM_PWR_STATE1,
	CRM_PWR_STATE2,
	CRM_PWR_STATE3,
	CRM_PWR_STATE4,
};

/**
 * enum crm_sw_drv_state:  Power states for the SW DRV request
 *
 * @CRM_ACTIVE_STATE:       Active or AMC mode requests. Resource state
 *                          is aggregated immediately.
 * @CRM_SLEEP_STATE:        State of the resource when the subsystem is
 *                          powered down. There is no client using the
 *                          resource actively.
 * @CRM_WAKE_STATE:         Resume resource state to the value previously
 *                          requested before the subsystem was powered down.
 */
enum crm_sw_drv_state {
	CRM_ACTIVE_STATE,
	CRM_SLEEP_STATE,
	CRM_WAKE_STATE,
};

union power_state {
	enum crm_hw_drv_state hw;
	enum crm_sw_drv_state sw;
};

/**
 * enum crm_drv_type:       CRM DRV type
 *
 * @CRM_HW_DRV:             DRV is HW (HW Client)
 * @CRM_SW_DRV:             DRV is SW (SW Client)
 */
enum crm_drv_type {
	CRM_HW_DRV,
	CRM_SW_DRV,
};

/**
 * struct crm_cmd: The message to be sent to CRM
 *
 * @pwr_state:     The pwr_state for HW/SW DRV
 * @resource_idx:  The index of the VCD OR ND to apply data
 * @data:          The Clock Plan index for the VCDs voted by PERF_OL.
 *                 BW vote for the VCDs voted by BW.
 * @wait:          Set by the client want if want to wait for the vote completion IRQ.
 *                 Applicable for only SW DRV client.
 *                 Don't care for HW DRV client.
 */
struct crm_cmd {
	union power_state pwr_state;
	u32 resource_idx;
	u32 data;
	bool wait;
};

#if IS_ENABLED(CONFIG_QCOM_CRM)
int crm_write_perf_ol(const struct device *dev, enum crm_drv_type drv,
		      u32 drv_id, const struct crm_cmd *cmd);
int crm_write_bw_vote(const struct device *dev, enum crm_drv_type drv,
		      u32 drv_id, const struct crm_cmd *cmd);
int crm_write_pwr_states(const struct device *dev, u32 drv_id);
int crm_dump_drv_regs(const char *name, u32 drv_id);
int crm_dump_regs(const char *name);
int crm_read_curr_perf_ol(const char *name, int vcd_idx, u32 *data);

const struct device *crm_get_device(const char *name);
#else

static inline int crm_write_perf_ol(const struct device *dev,
				    enum crm_drv_type drv,
				    u32 drv_id,
				    const struct crm_cmd *cmd)
{ return -ENODEV; }

static inline int crm_write_bw_vote(const struct device *dev,
				    enum crm_drv_type drv,
				    u32 drv_id,
				    const struct crm_cmd *cmd)
{ return -ENODEV; }

static inline int crm_write_pwr_states(const struct device *dev, u32 drv_id)
{ return -ENODEV; }

static inline int crm_dump_drv_regs(const char *name, u32 drv_id)
{ return -ENODEV; }

static inline int crm_dump_regs(const char *name)
{ return -ENODEV; }

static inline int crm_read_curr_perf_ol(const char *name, int vcd_idx, u32 *data)
{ return -ENODEV; }

static inline const struct device *crm_get_device(const char *name)
{ return ERR_PTR(-ENODEV); }
#endif /* CONFIG_QCOM_CRM */

#endif /* __SOC_QCOM_CRM_H__ */
