/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (C) 2019, Fuzhou Rockchip Electronics Co., Ltd
 */

#ifndef __SOC_ROCKCHIP_SYSTEM_MONITOR_H
#define __SOC_ROCKCHIP_SYSTEM_MONITOR_H

#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/regulator/consumer.h>

enum monitor_dev_type {
	MONITOR_TPYE_CPU = 0,	/* CPU */
	MONITOR_TPYE_DEV,	/* GPU, NPU, DMC, and so on */
};

enum system_monitor_event_type {
	SYSTEM_MONITOR_CHANGE_TEMP = 0,
};

struct system_monitor_event_data {
	int temp;
};

struct volt_adjust_table {
	unsigned int min;	/* Minimum frequency in MHz */
	unsigned int max;	/* Maximum frequency in MHz */
	int volt;		/* Voltage in microvolt */
};

struct temp_freq_table {
	int temp;		/* millicelsius */
	unsigned int freq;	/* KHz */
};

/**
 * struct temp_opp_table - System monitor device OPP description structure
 * @rate:		Frequency in hertz
 * @volt:		Target voltage in microvolt
 * @mem_volt:		Target voltage for memory in microvolt
 * @low_temp_volt:	Target voltage when low temperature, in microvolt
 * @low_temp_mem_volt:	Target voltage for memory when low temperature,
 *			in microvolt
 * @max_volt:		Maximum voltage in microvolt
 * @max_mem_volt:	Maximum voltage for memory in microvolt
 */
struct temp_opp_table {
	unsigned long rate;
	unsigned long volt;
	unsigned long mem_volt;
	unsigned long low_temp_volt;
	unsigned long low_temp_mem_volt;
	unsigned long max_volt;
	unsigned long max_mem_volt;
};

/**
 * struct monitor_dev_info - structure for a system monitor device
 * @dev:		Device registered by system monitor
 * @low_temp_adjust_table:	Voltage margin for different OPPs when lowe
 *				temperature
 * @opp_table:		Frequency and voltage information of device
 * @devp:		Device-specific system monitor profile
 * @node:		Node in monitor_dev_list
 * @high_limit_table:	Limit maximum frequency at different temperature,
 *			but the frequency is also changed by thermal framework.
 * @volt_adjust_mutex:	A mutex to protect changing voltage.
 * @max_temp_freq_req:	CPU maximum frequency constraint changed according
 *			to temperature.
 * @min_sta_freq_req:   CPU minimum frequency constraint changed according
 *			to system status.
 * @max_sta_freq_req:   CPU maximum frequency constraint changed according
 *			to system status.
 * @dev_max_freq_req:	Devices maximum frequency constraint changed according
 *			to temperature.
 * @low_limit:		Limit maximum frequency when low temperature, in Hz
 * @high_limit:		Limit maximum frequency when high temperature, in Hz
 * @max_volt:		Maximum voltage in microvolt
 * @low_temp_min_volt:	Minimum voltage of OPPs when low temperature, in
 *			microvolt
 * @high_temp_max_volt:	Maximum voltage when high temperature, in microvolt
 * @wide_temp_limit:	Target maximum frequency when low or high temperature,
 *			in Hz
 * @video_4k_freq:	Maximum frequency when paly 4k video, in KHz
 * @reboot_freq:	Limit maximum and minimum frequency when reboot, in KHz
 * @status_min_limit:	Minimum frequency of some status frequency, in KHz
 * @status_max_limit:	Minimum frequency of all status frequency, in KHz
 * @low_temp:		Low temperature trip point, in millicelsius
 * @high_temp:		High temperature trip point, in millicelsius
 * @temp_hysteresis:	A low hysteresis value on low_temp, in millicelsius
 * @is_low_temp:	True if current temperature less than low_temp
 * @is_high_temp:	True if current temperature greater than high_temp
 * @is_low_temp_enabled:	True if device node contains low temperature
 *				configuration
 * @is_status_freq_fixed:	True if enter into some status
 */
struct monitor_dev_info {
	struct device *dev;
	struct volt_adjust_table *low_temp_adjust_table;
	struct temp_opp_table *opp_table;
	struct monitor_dev_profile *devp;
	struct list_head node;
	struct temp_freq_table *high_limit_table;
	struct mutex volt_adjust_mutex;
	struct freq_qos_request max_temp_freq_req;
	struct freq_qos_request min_sta_freq_req;
	struct freq_qos_request max_sta_freq_req;
	struct dev_pm_qos_request dev_max_freq_req;
	struct regulator *early_reg;
	struct regulator **regulators;
	struct dev_pm_set_opp_data *set_opp_data;
	struct clk *clk;
	unsigned long low_limit;
	unsigned long high_limit;
	unsigned long max_volt;
	unsigned long low_temp_min_volt;
	unsigned long high_temp_max_volt;
	unsigned int video_4k_freq;
	unsigned int reboot_freq;
	unsigned int init_freq;
	unsigned int status_min_limit;
	unsigned int status_max_limit;
	unsigned int early_min_volt;
	unsigned int regulator_count;
	int low_temp;
	int high_temp;
	int temp_hysteresis;
	bool is_low_temp;
	bool is_high_temp;
	bool is_low_temp_enabled;
};

struct monitor_dev_profile {
	enum monitor_dev_type type;
	void *data;
	bool is_checked;
	int (*low_temp_adjust)(struct monitor_dev_info *info, bool is_low);
	int (*high_temp_adjust)(struct monitor_dev_info *info, bool is_low);
	int (*update_volt)(struct monitor_dev_info *info);
	int (*set_opp)(struct dev_pm_set_opp_data *data);
	struct cpumask allowed_cpus;
	struct rockchip_opp_info *opp_info;
};

#if IS_REACHABLE(CONFIG_ROCKCHIP_SYSTEM_MONITOR)
struct monitor_dev_info *
rockchip_system_monitor_register(struct device *dev,
				 struct monitor_dev_profile *devp);
void rockchip_system_monitor_unregister(struct monitor_dev_info *info);
int rockchip_monitor_cpu_low_temp_adjust(struct monitor_dev_info *info,
					 bool is_low);
int rockchip_monitor_cpu_high_temp_adjust(struct monitor_dev_info *info,
					  bool is_high);
void rockchip_monitor_volt_adjust_lock(struct monitor_dev_info *info);
void rockchip_monitor_volt_adjust_unlock(struct monitor_dev_info *info);
int rockchip_monitor_check_rate_volt(struct monitor_dev_info *info);
int rockchip_monitor_dev_low_temp_adjust(struct monitor_dev_info *info,
					 bool is_low);
int rockchip_monitor_dev_high_temp_adjust(struct monitor_dev_info *info,
					  bool is_high);
int rockchip_monitor_suspend_low_temp_adjust(int cpu);
int rockchip_system_monitor_register_notifier(struct notifier_block *nb);
void rockchip_system_monitor_unregister_notifier(struct notifier_block *nb);
#else
static inline struct monitor_dev_info *
rockchip_system_monitor_register(struct device *dev,
				 struct monitor_dev_profile *devp)
{
	return ERR_PTR(-ENOTSUPP);
};

static inline void
rockchip_system_monitor_unregister(struct monitor_dev_info *info)
{
}

static inline int
rockchip_monitor_cpu_low_temp_adjust(struct monitor_dev_info *info, bool is_low)
{
	return 0;
};

static inline int
rockchip_monitor_cpu_high_temp_adjust(struct monitor_dev_info *info,
				      bool is_high)
{
	return 0;
};

static inline void
rockchip_monitor_volt_adjust_lock(struct monitor_dev_info *info)
{
}

static inline void
rockchip_monitor_volt_adjust_unlock(struct monitor_dev_info *info)
{
}

static inline int
rockchip_monitor_check_rate_volt(struct monitor_dev_info *info)
{
	return 0;
}

static inline int
rockchip_monitor_dev_low_temp_adjust(struct monitor_dev_info *info, bool is_low)
{
	return 0;
};

static inline int
rockchip_monitor_dev_high_temp_adjust(struct monitor_dev_info *info,
				      bool is_high)
{
	return 0;
};

static inline int rockchip_monitor_suspend_low_temp_adjust(int cpu)
{
	return 0;
};

static inline int
rockchip_system_monitor_register_notifier(struct notifier_block *nb)
{
	return 0;
};

static inline void
rockchip_system_monitor_unregister_notifier(struct notifier_block *nb)
{
};
#endif /* CONFIG_ROCKCHIP_SYSTEM_MONITOR */

#endif
