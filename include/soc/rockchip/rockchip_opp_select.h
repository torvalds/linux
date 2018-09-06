/*
 * Copyright (c) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#ifndef __SOC_ROCKCHIP_OPP_SELECT_H
#define __SOC_ROCKCHIP_OPP_SELECT_H

struct thermal_opp_info;

enum thermal_opp_type {
	THERMAL_OPP_TPYE_CPU = 0,
	THERMAL_OPP_TPYE_DEV,
};

struct thermal_opp_device_data {
	enum thermal_opp_type type;
	void *data;
	int (*low_temp_adjust)(struct thermal_opp_info *info, bool is_low);
	int (*high_temp_adjust)(struct thermal_opp_info *info, bool is_low);
};

struct thermal_opp_table {
	unsigned long rate;
	unsigned long volt;
	unsigned long low_temp_volt;
};

struct thermal_opp_info {
	struct device *dev;
	struct thermal_zone_device *tz;
	struct notifier_block thermal_nb;
	struct sel_table *low_temp_table;
	struct thermal_opp_table *opp_table;
	struct thermal_opp_device_data *dev_data;
	unsigned int num_opps;
	unsigned long low_limit;
	unsigned long high_limit;
	int low_temp;
	int high_temp;
	int temp_hysteresis;
	int max_volt;
	int low_temp_min_volt;
	int high_temp_max_volt;
	bool is_low_temp;
	bool is_high_temp;
};

#ifdef CONFIG_ROCKCHIP_OPP
void rockchip_of_get_lkg_sel(struct device *dev, struct device_node *np,
			     char *lkg_name, int process,
			     int *volt_sel, int *scale_sel);
void rockchip_of_get_pvtm_sel(struct device *dev, struct device_node *np,
			      char *reg_name, int process,
			      int *volt_sel, int *scale_sel);
void rockchip_of_get_bin_sel(struct device *dev, struct device_node *np,
			     int bin, int *scale_sel);
int rockchip_get_efuse_value(struct device_node *np, char *porp_name,
			     int *value);
void rockchip_get_soc_info(struct device *dev,
			   const struct of_device_id *matches,
			   int *bin, int *process);
void rockchip_get_scale_volt_sel(struct device *dev, char *lkg_name,
				 char *reg_name, int bin, int process,
				 int *scale, int *volt_sel);
int rockchip_set_opp_info(struct device *dev, int process, int volt_sel);
int rockchip_adjust_power_scale(struct device *dev, int scale);
int rockchip_init_opp_table(struct device *dev,
			    const struct of_device_id *matches,
			    char *lkg_name, char *reg_name);
struct thermal_opp_info *
rockchip_register_thermal_notifier(struct device *dev,
				   struct thermal_opp_device_data *data);
void rockchip_unregister_thermal_notifier(struct thermal_opp_info *info);
int rockchip_cpu_low_temp_adjust(struct thermal_opp_info *info,
				 bool is_low);
int rockchip_cpu_high_temp_adjust(struct thermal_opp_info *info,
				  bool is_high);
int rockchip_dev_low_temp_adjust(struct thermal_opp_info *info,
				 bool is_low);
int rockchip_dev_high_temp_adjust(struct thermal_opp_info *info,
				  bool is_high);
#else
static inline void rockchip_of_get_lkg_sel(struct device *dev,
					   struct device_node *np,
					   char *lkg_name, int process,
					   int *volt_sel, int *scale_sel)
{
}

static inline void rockchip_of_get_pvtm_sel(struct device *dev,
					    struct device_node *np,
					    char *reg_name, int process,
					    int *volt_sel, int *scale_sel)
{
}

static inline void rockchip_of_get_bin_sel(struct device *dev,
					   struct device_node *np, int bin,
					   int *scale_sel)
{
}

static inline int rockchip_get_efuse_value(struct device_node *np,
					   char *porp_name, int *value)
{
	return -ENOTSUPP;
}

static inline void rockchip_get_soc_info(struct device *dev,
					 const struct of_device_id *matches,
					 int *bin, int *process)
{
}

static inline void rockchip_get_scale_volt_sel(struct device *dev,
					       char *lkg_name, char *reg_name,
					       int bin, int process, int *scale,
					       int *volt_sel)
{
}

static inline int rockchip_set_opp_info(struct device *dev, int process,
					int volt_sel)
{
	return -ENOTSUPP;
}

static inline int rockchip_adjust_power_scale(struct device *dev, int scale)
{
	return -ENOTSUPP;
}

static inline int rockchip_init_opp_table(struct device *dev,
					  const struct of_device_id *matches,
					  char *lkg_name, char *reg_name)
{
	return -ENOTSUPP;
}

static inline struct thermal_opp_info *
rockchip_register_thermal_notifier(struct device *dev,
				   struct thermal_opp_device_data *data)
{
	return ERR_PTR(-ENOTSUPP);
}

static inline void
rockchip_unregister_thermal_notifier(struct thermal_opp_info *info)
{
}

static inline int rockchip_cpu_low_temp_adjust(struct thermal_opp_info *info,
					       bool is_low)
{
	return -ENOTSUPP;
}

static inline int rockchip_cpu_high_temp_adjust(struct thermal_opp_info *info,
						bool is_high)
{
	return -ENOTSUPP;
}

static inline int rockchip_dev_low_temp_adjust(struct thermal_opp_info *info,
					       bool is_low)
{
	return -ENOTSUPP;
}

static inline int rockchip_dev_high_temp_adjust(struct thermal_opp_info *info,
						bool is_high)
{
	return -ENOTSUPP;
}
#endif /* CONFIG_ROCKCHIP_OPP */

#endif
