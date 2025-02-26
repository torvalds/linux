/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic OPP Interface
 *
 * Copyright (C) 2009-2010 Texas Instruments Incorporated.
 *	Nishanth Menon
 *	Romit Dasgupta
 *	Kevin Hilman
 */

#ifndef __LINUX_OPP_H__
#define __LINUX_OPP_H__

#include <linux/energy_model.h>
#include <linux/err.h>
#include <linux/notifier.h>

struct clk;
struct cpufreq_frequency_table;
struct regulator;
struct dev_pm_opp;
struct device;
struct opp_table;

enum dev_pm_opp_event {
	OPP_EVENT_ADD, OPP_EVENT_REMOVE, OPP_EVENT_ENABLE, OPP_EVENT_DISABLE,
	OPP_EVENT_ADJUST_VOLTAGE,
};

/**
 * struct dev_pm_opp_supply - Power supply voltage/current values
 * @u_volt:	Target voltage in microvolts corresponding to this OPP
 * @u_volt_min:	Minimum voltage in microvolts corresponding to this OPP
 * @u_volt_max:	Maximum voltage in microvolts corresponding to this OPP
 * @u_amp:	Maximum current drawn by the device in microamperes
 * @u_watt:	Power used by the device in microwatts
 *
 * This structure stores the voltage/current/power values for a single power
 * supply.
 */
struct dev_pm_opp_supply {
	unsigned long u_volt;
	unsigned long u_volt_min;
	unsigned long u_volt_max;
	unsigned long u_amp;
	unsigned long u_watt;
};

typedef int (*config_regulators_t)(struct device *dev,
			struct dev_pm_opp *old_opp, struct dev_pm_opp *new_opp,
			struct regulator **regulators, unsigned int count);

typedef int (*config_clks_t)(struct device *dev, struct opp_table *opp_table,
			struct dev_pm_opp *opp, void *data, bool scaling_down);

/**
 * struct dev_pm_opp_config - Device OPP configuration values
 * @clk_names: Clk names, NULL terminated array.
 * @config_clks: Custom set clk helper.
 * @prop_name: Name to postfix to properties.
 * @config_regulators: Custom set regulator helper.
 * @supported_hw: Array of hierarchy of versions to match.
 * @supported_hw_count: Number of elements in the array.
 * @regulator_names: Array of pointers to the names of the regulator, NULL terminated.
 * @required_dev: The required OPP device.
 * @required_dev_index: The index of the required OPP for the @required_dev.
 *
 * This structure contains platform specific OPP configurations for the device.
 */
struct dev_pm_opp_config {
	/* NULL terminated */
	const char * const *clk_names;
	config_clks_t config_clks;
	const char *prop_name;
	config_regulators_t config_regulators;
	const unsigned int *supported_hw;
	unsigned int supported_hw_count;
	const char * const *regulator_names;
	struct device *required_dev;
	unsigned int required_dev_index;
};

#define OPP_LEVEL_UNSET			U32_MAX

/**
 * struct dev_pm_opp_data - The data to use to initialize an OPP.
 * @turbo: Flag to indicate whether the OPP is to be marked turbo or not.
 * @level: The performance level for the OPP. Set level to OPP_LEVEL_UNSET if
 * level field isn't used.
 * @freq: The clock rate in Hz for the OPP.
 * @u_volt: The voltage in uV for the OPP.
 */
struct dev_pm_opp_data {
	bool turbo;
	unsigned int level;
	unsigned long freq;
	unsigned long u_volt;
};

#if defined(CONFIG_PM_OPP)

struct opp_table *dev_pm_opp_get_opp_table(struct device *dev);
void dev_pm_opp_get_opp_table_ref(struct opp_table *opp_table);
void dev_pm_opp_put_opp_table(struct opp_table *opp_table);

unsigned long dev_pm_opp_get_bw(struct dev_pm_opp *opp, bool peak, int index);

unsigned long dev_pm_opp_get_voltage(struct dev_pm_opp *opp);

int dev_pm_opp_get_supplies(struct dev_pm_opp *opp, struct dev_pm_opp_supply *supplies);

unsigned long dev_pm_opp_get_power(struct dev_pm_opp *opp);

unsigned long dev_pm_opp_get_freq_indexed(struct dev_pm_opp *opp, u32 index);

unsigned int dev_pm_opp_get_level(struct dev_pm_opp *opp);

unsigned int dev_pm_opp_get_required_pstate(struct dev_pm_opp *opp,
					    unsigned int index);

bool dev_pm_opp_is_turbo(struct dev_pm_opp *opp);

int dev_pm_opp_get_opp_count(struct device *dev);
unsigned long dev_pm_opp_get_max_clock_latency(struct device *dev);
unsigned long dev_pm_opp_get_max_volt_latency(struct device *dev);
unsigned long dev_pm_opp_get_max_transition_latency(struct device *dev);
unsigned long dev_pm_opp_get_suspend_opp_freq(struct device *dev);

struct dev_pm_opp *dev_pm_opp_find_freq_exact(struct device *dev,
					      unsigned long freq,
					      bool available);

struct dev_pm_opp *
dev_pm_opp_find_freq_exact_indexed(struct device *dev, unsigned long freq,
				   u32 index, bool available);

struct dev_pm_opp *dev_pm_opp_find_freq_floor(struct device *dev,
					      unsigned long *freq);

struct dev_pm_opp *dev_pm_opp_find_freq_floor_indexed(struct device *dev,
						      unsigned long *freq, u32 index);

struct dev_pm_opp *dev_pm_opp_find_freq_ceil(struct device *dev,
					     unsigned long *freq);

struct dev_pm_opp *dev_pm_opp_find_freq_ceil_indexed(struct device *dev,
						     unsigned long *freq, u32 index);

struct dev_pm_opp *dev_pm_opp_find_level_exact(struct device *dev,
					       unsigned int level);

struct dev_pm_opp *dev_pm_opp_find_level_ceil(struct device *dev,
					      unsigned int *level);

struct dev_pm_opp *dev_pm_opp_find_level_floor(struct device *dev,
					       unsigned int *level);

struct dev_pm_opp *dev_pm_opp_find_bw_ceil(struct device *dev,
					   unsigned int *bw, int index);

struct dev_pm_opp *dev_pm_opp_find_bw_floor(struct device *dev,
					   unsigned int *bw, int index);

void dev_pm_opp_get(struct dev_pm_opp *opp);
void dev_pm_opp_put(struct dev_pm_opp *opp);

int dev_pm_opp_add_dynamic(struct device *dev, struct dev_pm_opp_data *opp);

void dev_pm_opp_remove(struct device *dev, unsigned long freq);
void dev_pm_opp_remove_all_dynamic(struct device *dev);

int dev_pm_opp_adjust_voltage(struct device *dev, unsigned long freq,
			      unsigned long u_volt, unsigned long u_volt_min,
			      unsigned long u_volt_max);

int dev_pm_opp_enable(struct device *dev, unsigned long freq);

int dev_pm_opp_disable(struct device *dev, unsigned long freq);

int dev_pm_opp_register_notifier(struct device *dev, struct notifier_block *nb);
int dev_pm_opp_unregister_notifier(struct device *dev, struct notifier_block *nb);

int dev_pm_opp_set_config(struct device *dev, struct dev_pm_opp_config *config);
int devm_pm_opp_set_config(struct device *dev, struct dev_pm_opp_config *config);
void dev_pm_opp_clear_config(int token);
int dev_pm_opp_config_clks_simple(struct device *dev,
		struct opp_table *opp_table, struct dev_pm_opp *opp, void *data,
		bool scaling_down);

struct dev_pm_opp *dev_pm_opp_xlate_required_opp(struct opp_table *src_table, struct opp_table *dst_table, struct dev_pm_opp *src_opp);
int dev_pm_opp_xlate_performance_state(struct opp_table *src_table, struct opp_table *dst_table, unsigned int pstate);
int dev_pm_opp_set_rate(struct device *dev, unsigned long target_freq);
int dev_pm_opp_set_opp(struct device *dev, struct dev_pm_opp *opp);
int dev_pm_opp_set_sharing_cpus(struct device *cpu_dev, const struct cpumask *cpumask);
int dev_pm_opp_get_sharing_cpus(struct device *cpu_dev, struct cpumask *cpumask);
void dev_pm_opp_remove_table(struct device *dev);
void dev_pm_opp_cpumask_remove_table(const struct cpumask *cpumask);
int dev_pm_opp_sync_regulators(struct device *dev);
#else
static inline struct opp_table *dev_pm_opp_get_opp_table(struct device *dev)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct opp_table *dev_pm_opp_get_opp_table_indexed(struct device *dev, int index)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void dev_pm_opp_get_opp_table_ref(struct opp_table *opp_table) {}

static inline void dev_pm_opp_put_opp_table(struct opp_table *opp_table) {}

static inline unsigned long dev_pm_opp_get_bw(struct dev_pm_opp *opp, bool peak, int index)
{
	return 0;
}

static inline unsigned long dev_pm_opp_get_voltage(struct dev_pm_opp *opp)
{
	return 0;
}

static inline int dev_pm_opp_get_supplies(struct dev_pm_opp *opp, struct dev_pm_opp_supply *supplies)
{
	return -EOPNOTSUPP;
}

static inline unsigned long dev_pm_opp_get_power(struct dev_pm_opp *opp)
{
	return 0;
}

static inline unsigned long dev_pm_opp_get_freq_indexed(struct dev_pm_opp *opp, u32 index)
{
	return 0;
}

static inline unsigned int dev_pm_opp_get_level(struct dev_pm_opp *opp)
{
	return 0;
}

static inline
unsigned int dev_pm_opp_get_required_pstate(struct dev_pm_opp *opp,
					    unsigned int index)
{
	return 0;
}

static inline bool dev_pm_opp_is_turbo(struct dev_pm_opp *opp)
{
	return false;
}

static inline int dev_pm_opp_get_opp_count(struct device *dev)
{
	return 0;
}

static inline unsigned long dev_pm_opp_get_max_clock_latency(struct device *dev)
{
	return 0;
}

static inline unsigned long dev_pm_opp_get_max_volt_latency(struct device *dev)
{
	return 0;
}

static inline unsigned long dev_pm_opp_get_max_transition_latency(struct device *dev)
{
	return 0;
}

static inline unsigned long dev_pm_opp_get_suspend_opp_freq(struct device *dev)
{
	return 0;
}

static inline struct dev_pm_opp *dev_pm_opp_find_freq_exact(struct device *dev,
					unsigned long freq, bool available)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct dev_pm_opp *
dev_pm_opp_find_freq_exact_indexed(struct device *dev, unsigned long freq,
				   u32 index, bool available)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct dev_pm_opp *dev_pm_opp_find_freq_floor(struct device *dev,
					unsigned long *freq)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct dev_pm_opp *
dev_pm_opp_find_freq_floor_indexed(struct device *dev, unsigned long *freq, u32 index)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct dev_pm_opp *dev_pm_opp_find_freq_ceil(struct device *dev,
					unsigned long *freq)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct dev_pm_opp *
dev_pm_opp_find_freq_ceil_indexed(struct device *dev, unsigned long *freq, u32 index)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct dev_pm_opp *dev_pm_opp_find_level_exact(struct device *dev,
					unsigned int level)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct dev_pm_opp *dev_pm_opp_find_level_ceil(struct device *dev,
					unsigned int *level)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct dev_pm_opp *dev_pm_opp_find_level_floor(struct device *dev,
							     unsigned int *level)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct dev_pm_opp *dev_pm_opp_find_bw_ceil(struct device *dev,
					unsigned int *bw, int index)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct dev_pm_opp *dev_pm_opp_find_bw_floor(struct device *dev,
					unsigned int *bw, int index)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void dev_pm_opp_get(struct dev_pm_opp *opp) {}

static inline void dev_pm_opp_put(struct dev_pm_opp *opp) {}

static inline int
dev_pm_opp_add_dynamic(struct device *dev, struct dev_pm_opp_data *opp)
{
	return -EOPNOTSUPP;
}

static inline void dev_pm_opp_remove(struct device *dev, unsigned long freq)
{
}

static inline void dev_pm_opp_remove_all_dynamic(struct device *dev)
{
}

static inline int
dev_pm_opp_adjust_voltage(struct device *dev, unsigned long freq,
			  unsigned long u_volt, unsigned long u_volt_min,
			  unsigned long u_volt_max)
{
	return 0;
}

static inline int dev_pm_opp_enable(struct device *dev, unsigned long freq)
{
	return 0;
}

static inline int dev_pm_opp_disable(struct device *dev, unsigned long freq)
{
	return 0;
}

static inline int dev_pm_opp_register_notifier(struct device *dev, struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_opp_unregister_notifier(struct device *dev, struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_opp_set_config(struct device *dev, struct dev_pm_opp_config *config)
{
	return -EOPNOTSUPP;
}

static inline int devm_pm_opp_set_config(struct device *dev, struct dev_pm_opp_config *config)
{
	return -EOPNOTSUPP;
}

static inline void dev_pm_opp_clear_config(int token) {}

static inline int dev_pm_opp_config_clks_simple(struct device *dev,
		struct opp_table *opp_table, struct dev_pm_opp *opp, void *data,
		bool scaling_down)
{
	return -EOPNOTSUPP;
}

static inline struct dev_pm_opp *dev_pm_opp_xlate_required_opp(struct opp_table *src_table,
				struct opp_table *dst_table, struct dev_pm_opp *src_opp)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int dev_pm_opp_xlate_performance_state(struct opp_table *src_table, struct opp_table *dst_table, unsigned int pstate)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_opp_set_rate(struct device *dev, unsigned long target_freq)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_opp_set_opp(struct device *dev, struct dev_pm_opp *opp)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_opp_set_sharing_cpus(struct device *cpu_dev, const struct cpumask *cpumask)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_opp_get_sharing_cpus(struct device *cpu_dev, struct cpumask *cpumask)
{
	return -EINVAL;
}

static inline void dev_pm_opp_remove_table(struct device *dev)
{
}

static inline void dev_pm_opp_cpumask_remove_table(const struct cpumask *cpumask)
{
}

static inline int dev_pm_opp_sync_regulators(struct device *dev)
{
	return -EOPNOTSUPP;
}

#endif		/* CONFIG_PM_OPP */

#if defined(CONFIG_CPU_FREQ) && defined(CONFIG_PM_OPP)
int dev_pm_opp_init_cpufreq_table(struct device *dev, struct cpufreq_frequency_table **table);
void dev_pm_opp_free_cpufreq_table(struct device *dev, struct cpufreq_frequency_table **table);
#else
static inline int dev_pm_opp_init_cpufreq_table(struct device *dev, struct cpufreq_frequency_table **table)
{
	return -EINVAL;
}

static inline void dev_pm_opp_free_cpufreq_table(struct device *dev, struct cpufreq_frequency_table **table)
{
}
#endif


#if defined(CONFIG_PM_OPP) && defined(CONFIG_OF)
int dev_pm_opp_of_add_table(struct device *dev);
int dev_pm_opp_of_add_table_indexed(struct device *dev, int index);
int devm_pm_opp_of_add_table_indexed(struct device *dev, int index);
void dev_pm_opp_of_remove_table(struct device *dev);
int devm_pm_opp_of_add_table(struct device *dev);
int dev_pm_opp_of_cpumask_add_table(const struct cpumask *cpumask);
void dev_pm_opp_of_cpumask_remove_table(const struct cpumask *cpumask);
int dev_pm_opp_of_get_sharing_cpus(struct device *cpu_dev, struct cpumask *cpumask);
struct device_node *dev_pm_opp_of_get_opp_desc_node(struct device *dev);
struct device_node *dev_pm_opp_get_of_node(struct dev_pm_opp *opp);
int of_get_required_opp_performance_state(struct device_node *np, int index);
bool dev_pm_opp_of_has_required_opp(struct device *dev);
int dev_pm_opp_of_find_icc_paths(struct device *dev, struct opp_table *opp_table);
int dev_pm_opp_of_register_em(struct device *dev, struct cpumask *cpus);
int dev_pm_opp_calc_power(struct device *dev, unsigned long *uW,
			  unsigned long *kHz);
static inline void dev_pm_opp_of_unregister_em(struct device *dev)
{
	em_dev_unregister_perf_domain(dev);
}
#else
static inline int dev_pm_opp_of_add_table(struct device *dev)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_opp_of_add_table_indexed(struct device *dev, int index)
{
	return -EOPNOTSUPP;
}

static inline int devm_pm_opp_of_add_table_indexed(struct device *dev, int index)
{
	return -EOPNOTSUPP;
}

static inline void dev_pm_opp_of_remove_table(struct device *dev)
{
}

static inline int devm_pm_opp_of_add_table(struct device *dev)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_opp_of_cpumask_add_table(const struct cpumask *cpumask)
{
	return -EOPNOTSUPP;
}

static inline void dev_pm_opp_of_cpumask_remove_table(const struct cpumask *cpumask)
{
}

static inline int dev_pm_opp_of_get_sharing_cpus(struct device *cpu_dev, struct cpumask *cpumask)
{
	return -EOPNOTSUPP;
}

static inline struct device_node *dev_pm_opp_of_get_opp_desc_node(struct device *dev)
{
	return NULL;
}

static inline struct device_node *dev_pm_opp_get_of_node(struct dev_pm_opp *opp)
{
	return NULL;
}

static inline int dev_pm_opp_of_register_em(struct device *dev,
					    struct cpumask *cpus)
{
	return -EOPNOTSUPP;
}

static inline void dev_pm_opp_of_unregister_em(struct device *dev)
{
}

static inline int dev_pm_opp_calc_power(struct device *dev, unsigned long *uW,
					unsigned long *kHz)
{
	return -EOPNOTSUPP;
}

static inline int of_get_required_opp_performance_state(struct device_node *np, int index)
{
	return -EOPNOTSUPP;
}

static inline bool dev_pm_opp_of_has_required_opp(struct device *dev)
{
	return false;
}

static inline int dev_pm_opp_of_find_icc_paths(struct device *dev, struct opp_table *opp_table)
{
	return -EOPNOTSUPP;
}
#endif

/* OPP Configuration helpers */

static inline int dev_pm_opp_add(struct device *dev, unsigned long freq,
				 unsigned long u_volt)
{
	struct dev_pm_opp_data data = {
		.freq = freq,
		.u_volt = u_volt,
	};

	return dev_pm_opp_add_dynamic(dev, &data);
}

/* Regulators helpers */
static inline int dev_pm_opp_set_regulators(struct device *dev,
					    const char * const names[])
{
	struct dev_pm_opp_config config = {
		.regulator_names = names,
	};

	return dev_pm_opp_set_config(dev, &config);
}

static inline void dev_pm_opp_put_regulators(int token)
{
	dev_pm_opp_clear_config(token);
}

static inline int devm_pm_opp_set_regulators(struct device *dev,
					     const char * const names[])
{
	struct dev_pm_opp_config config = {
		.regulator_names = names,
	};

	return devm_pm_opp_set_config(dev, &config);
}

/* Supported-hw helpers */
static inline int dev_pm_opp_set_supported_hw(struct device *dev,
					      const u32 *versions,
					      unsigned int count)
{
	struct dev_pm_opp_config config = {
		.supported_hw = versions,
		.supported_hw_count = count,
	};

	return dev_pm_opp_set_config(dev, &config);
}

static inline void dev_pm_opp_put_supported_hw(int token)
{
	dev_pm_opp_clear_config(token);
}

static inline int devm_pm_opp_set_supported_hw(struct device *dev,
					       const u32 *versions,
					       unsigned int count)
{
	struct dev_pm_opp_config config = {
		.supported_hw = versions,
		.supported_hw_count = count,
	};

	return devm_pm_opp_set_config(dev, &config);
}

/* clkname helpers */
static inline int dev_pm_opp_set_clkname(struct device *dev, const char *name)
{
	const char *names[] = { name, NULL };
	struct dev_pm_opp_config config = {
		.clk_names = names,
	};

	return dev_pm_opp_set_config(dev, &config);
}

static inline void dev_pm_opp_put_clkname(int token)
{
	dev_pm_opp_clear_config(token);
}

static inline int devm_pm_opp_set_clkname(struct device *dev, const char *name)
{
	const char *names[] = { name, NULL };
	struct dev_pm_opp_config config = {
		.clk_names = names,
	};

	return devm_pm_opp_set_config(dev, &config);
}

/* config-regulators helpers */
static inline int dev_pm_opp_set_config_regulators(struct device *dev,
						   config_regulators_t helper)
{
	struct dev_pm_opp_config config = {
		.config_regulators = helper,
	};

	return dev_pm_opp_set_config(dev, &config);
}

static inline void dev_pm_opp_put_config_regulators(int token)
{
	dev_pm_opp_clear_config(token);
}

/* prop-name helpers */
static inline int dev_pm_opp_set_prop_name(struct device *dev, const char *name)
{
	struct dev_pm_opp_config config = {
		.prop_name = name,
	};

	return dev_pm_opp_set_config(dev, &config);
}

static inline void dev_pm_opp_put_prop_name(int token)
{
	dev_pm_opp_clear_config(token);
}

static inline unsigned long dev_pm_opp_get_freq(struct dev_pm_opp *opp)
{
	return dev_pm_opp_get_freq_indexed(opp, 0);
}

#endif		/* __LINUX_OPP_H__ */
