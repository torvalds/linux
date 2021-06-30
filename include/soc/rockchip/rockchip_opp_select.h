/*
 * Copyright (c) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#ifndef __SOC_ROCKCHIP_OPP_SELECT_H
#define __SOC_ROCKCHIP_OPP_SELECT_H

#if IS_ENABLED(CONFIG_ROCKCHIP_OPP)
int rockchip_of_get_leakage(struct device *dev, char *lkg_name, int *leakage);
void rockchip_of_get_lkg_sel(struct device *dev, struct device_node *np,
			     char *lkg_name, int process,
			     int *volt_sel, int *scale_sel);
void rockchip_of_get_pvtm_sel(struct device *dev, struct device_node *np,
			      char *reg_name, int process,
			      int *volt_sel, int *scale_sel);
void rockchip_of_get_bin_sel(struct device *dev, struct device_node *np,
			     int bin, int *scale_sel);
void rockchip_of_get_bin_volt_sel(struct device *dev, struct device_node *np,
				  int bin, int *bin_volt_sel);
int rockchip_nvmem_cell_read_u8(struct device_node *np, const char *cell_id,
				u8 *val);
int rockchip_nvmem_cell_read_u16(struct device_node *np, const char *cell_id,
				 u16 *val);
void rockchip_get_soc_info(struct device *dev,
			   const struct of_device_id *matches,
			   int *bin, int *process);
void rockchip_get_scale_volt_sel(struct device *dev, char *lkg_name,
				 char *reg_name, int bin, int process,
				 int *scale, int *volt_sel);
struct opp_table *rockchip_set_opp_prop_name(struct device *dev, int process,
					     int volt_sel);
int rockchip_adjust_power_scale(struct device *dev, int scale);
int rockchip_init_opp_table(struct device *dev,
			    const struct of_device_id *matches,
			    char *lkg_name, char *reg_name);
#else
static inline int rockchip_of_get_leakage(struct device *dev, char *lkg_name,
					  int *leakage)
{
	return -ENOTSUPP;
}

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

static inline void rockchip_of_get_bin_volt_sel(struct device *dev,
						struct device_node *np,
						int bin, int *bin_volt_sel)
{
}

static inline int rockchip_nvmem_cell_read_u8(struct device_node *np,
					      const char *cell_id, u8 *val)
{
	return -EOPNOTSUPP;
}

static inline int rockchip_nvmem_cell_read_u16(struct device_node *np,
					       const char *cell_id, u16 *val)
{
	return -EOPNOTSUPP;
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

static inline struct opp_table *rockchip_set_opp_prop_name(struct device *dev,
							   int process,
							   int volt_sel)
{
	return ERR_PTR(-ENOTSUPP);
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

#endif /* CONFIG_ROCKCHIP_OPP */

#endif
