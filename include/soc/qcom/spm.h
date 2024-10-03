/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014,2015, Linaro Ltd.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SPM_H__
#define __SPM_H__

#include <linux/cpuidle.h>

#define MAX_PMIC_DATA		2
#define MAX_SEQ_DATA		64

enum pm_sleep_mode {
	PM_SLEEP_MODE_STBY,
	PM_SLEEP_MODE_RET,
	PM_SLEEP_MODE_SPC,
	PM_SLEEP_MODE_PC,
	PM_SLEEP_MODE_NR,
};

struct spm_reg_data {
	const u16 *reg_offset;
	u32 spm_cfg;
	u32 spm_dly;
	u32 pmic_dly;
	u32 pmic_data[MAX_PMIC_DATA];
	u32 avs_ctl;
	u32 avs_limit;
	u8 seq[MAX_SEQ_DATA];
	u8 start_index[PM_SLEEP_MODE_NR];
};

struct spm_driver_data {
	void __iomem *reg_base;
	const struct spm_reg_data *reg_data;
};

enum {
	MSM_SPM_MODE_DISABLED,
	MSM_SPM_MODE_CLOCK_GATING,
	MSM_SPM_MODE_RETENTION,
	MSM_SPM_MODE_GDHS,
	MSM_SPM_MODE_POWER_COLLAPSE,
	MSM_SPM_MODE_STANDALONE_POWER_COLLAPSE,
	MSM_SPM_MODE_FASTPC,
	MSM_SPM_MODE_NR
};

enum msm_spm_avs_irq {
	MSM_SPM_AVS_IRQ_MIN,
	MSM_SPM_AVS_IRQ_MAX,
};

struct msm_spm_device;
struct device_node;

void spm_set_low_power_mode(struct spm_driver_data *drv,
			enum pm_sleep_mode mode);

#if defined(CONFIG_MSM_SPM)

int msm_spm_set_low_power_mode(unsigned int mode, bool notify_rpm);
void msm_spm_set_rpm_hs(bool allow_rpm_hs);
int msm_spm_probe_done(void);
int msm_spm_set_vdd(unsigned int cpu, unsigned int vlevel);
int msm_spm_get_vdd(unsigned int cpu);
int msm_spm_turn_on_cpu_rail(struct device_node *l2ccc_node,
		unsigned int val, int cpu, int vctl_offset);
struct msm_spm_device *msm_spm_get_device_by_name(const char *name);
int msm_spm_config_low_power_mode(struct msm_spm_device *dev,
		unsigned int mode, bool notify_rpm);
int msm_spm_config_low_power_mode_addr(struct msm_spm_device *dev,
		unsigned int mode, bool notify_rpm);
int msm_spm_device_init(void);
bool msm_spm_is_mode_avail(unsigned int mode);
void msm_spm_dump_regs(unsigned int cpu);
int msm_spm_is_avs_enabled(unsigned int cpu);
int msm_spm_avs_enable(unsigned int cpu);
int msm_spm_avs_disable(unsigned int cpu);
int msm_spm_avs_set_limit(unsigned int cpu, uint32_t min_lvl,
		uint32_t max_lvl);
int msm_spm_avs_enable_irq(unsigned int cpu, enum msm_spm_avs_irq irq);
int msm_spm_avs_disable_irq(unsigned int cpu, enum msm_spm_avs_irq irq);
int msm_spm_avs_clear_irq(unsigned int cpu, enum msm_spm_avs_irq irq);

#if defined(CONFIG_MSM_L2_SPM)

/* Public functions */

int msm_spm_apcs_set_phase(int cpu, unsigned int phase_cnt);
int msm_spm_enable_fts_lpm(int cpu, uint32_t mode);

#else

static inline int msm_spm_apcs_set_phase(int cpu, unsigned int phase_cnt)
{
	return -ENODEV;
}

static inline int msm_spm_enable_fts_lpm(int cpu, uint32_t mode)
{
	return -ENODEV;
}
#endif /* defined(CONFIG_MSM_L2_SPM) */
#else /* defined(CONFIG_MSM_SPM) */
static inline int msm_spm_set_low_power_mode(unsigned int mode, bool notify_rpm)
{
	return -ENODEV;
}

static inline void msm_spm_set_rpm_hs(bool allow_rpm_hs) {}

static inline int msm_spm_probe_done(void)
{
	return -ENODEV;
}

static inline int msm_spm_set_vdd(unsigned int cpu, unsigned int vlevel)
{
	return -ENODEV;
}

static inline int msm_spm_get_vdd(unsigned int cpu)
{
	return 0;
}

static inline int msm_spm_turn_on_cpu_rail(struct device_node *l2ccc_node,
		unsigned int val, int cpu, int vctl_offset)
{
	return -ENODEV;
}

static inline int msm_spm_device_init(void)
{
	return -ENODEV;
}

static inline void msm_spm_dump_regs(unsigned int cpu)
{ }

static inline int msm_spm_config_low_power_mode(struct msm_spm_device *dev,
		unsigned int mode, bool notify_rpm)
{
	return -ENODEV;
}

static inline int msm_spm_config_low_power_mode_addr(
	struct msm_spm_device *dev, unsigned int mode, bool notify_rpm)
{
	return -ENODEV;
}

static inline struct msm_spm_device *msm_spm_get_device_by_name(
				const char *name)
{
	return NULL;
}

static inline bool msm_spm_is_mode_avail(unsigned int mode)
{
	return false;
}

static inline int msm_spm_is_avs_enabled(unsigned int cpu)
{
	return -ENODEV;
}

static inline int msm_spm_avs_enable(unsigned int cpu)
{
	return -ENODEV;
}

static inline int msm_spm_avs_disable(unsigned int cpu)
{
	return -ENODEV;
}

static inline int msm_spm_avs_set_limit(unsigned int cpu, uint32_t min_lvl,
		uint32_t max_lvl)
{
	return -ENODEV;
}

static inline int msm_spm_avs_enable_irq(unsigned int cpu,
		enum msm_spm_avs_irq irq)
{
	return -ENODEV;
}

static inline int msm_spm_avs_disable_irq(unsigned int cpu,
		enum msm_spm_avs_irq irq)
{
	return -ENODEV;
}

static inline int msm_spm_avs_clear_irq(unsigned int cpu,
		enum msm_spm_avs_irq irq)
{
	return -ENODEV;
}

#endif  /* defined (CONFIG_MSM_SPM) */
#endif /* __SPM_H__ */
