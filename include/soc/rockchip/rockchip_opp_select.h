/*
 * Copyright (c) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#ifndef __SOC_ROCKCHIP_OPP_SELECT_H
#define __SOC_ROCKCHIP_OPP_SELECT_H

#define VOLT_RM_TABLE_END	~1

#define OPP_INTERMEDIATE_MASK	0x3f
#define OPP_INTERMEDIATE_RATE	BIT(0)
#define OPP_SCALING_UP_RATE	BIT(1)
#define OPP_SCALING_UP_INTER	(OPP_INTERMEDIATE_RATE | OPP_SCALING_UP_RATE)
#define OPP_SCALING_DOWN_INTER	OPP_INTERMEDIATE_RATE
#define OPP_LENGTH_LOW		BIT(2)

struct rockchip_opp_info;

struct volt_rm_table {
	int volt;
	int rm;
};

struct rockchip_opp_data {
	int (*get_soc_info)(struct device *dev, struct device_node *np,
			    int *bin, int *process);
	int (*set_soc_info)(struct device *dev, struct device_node *np,
			    int bin, int process, int volt_sel);
	int (*set_read_margin)(struct device *dev,
			       struct rockchip_opp_info *opp_info,
			       u32 rm);
};

struct pvtpll_opp_table {
	unsigned long rate;
	unsigned long u_volt;
	unsigned long u_volt_min;
	unsigned long u_volt_max;
};

struct rockchip_opp_info {
	struct device *dev;
	struct pvtpll_opp_table *opp_table;
	const struct rockchip_opp_data *data;
	struct volt_rm_table *volt_rm_tbl;
	struct regmap *grf;
	struct regmap *dsu_grf;
	struct clk_bulk_data *clks;
	struct clk *scmi_clk;
	/* The threshold frequency for set intermediate rate */
	unsigned long intermediate_threshold_freq;
	unsigned int pvtpll_avg_offset;
	unsigned int pvtpll_min_rate;
	unsigned int pvtpll_volt_step;
	int num_clks;
	/* The read margin for low voltage */
	u32 low_rm;
	u32 current_rm;
	u32 target_rm;
};

#if IS_ENABLED(CONFIG_ROCKCHIP_OPP)
int rockchip_of_get_leakage(struct device *dev, char *lkg_name, int *leakage);
void rockchip_of_get_lkg_sel(struct device *dev, struct device_node *np,
			     char *lkg_name, int process,
			     int *volt_sel, int *scale_sel);
void rockchip_pvtpll_calibrate_opp(struct rockchip_opp_info *info);
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
int rockchip_get_volt_rm_table(struct device *dev, struct device_node *np,
			       char *porp_name, struct volt_rm_table **table);
void rockchip_get_opp_data(const struct of_device_id *matches,
			   struct rockchip_opp_info *info);
void rockchip_get_scale_volt_sel(struct device *dev, char *lkg_name,
				 char *reg_name, int bin, int process,
				 int *scale, int *volt_sel);
struct opp_table *rockchip_set_opp_prop_name(struct device *dev, int process,
					     int volt_sel);
int rockchip_adjust_power_scale(struct device *dev, int scale);
int rockchip_get_read_margin(struct device *dev,
			     struct rockchip_opp_info *opp_info,
			     unsigned long volt, u32 *target_rm);
int rockchip_set_read_margin(struct device *dev,
			     struct rockchip_opp_info *opp_info, u32 rm,
			     bool is_set_rm);
int rockchip_set_intermediate_rate(struct device *dev,
				   struct rockchip_opp_info *opp_info,
				   struct clk *clk, unsigned long old_freq,
				   unsigned long new_freq, bool is_scaling_up,
				   bool is_set_clk);
int rockchip_init_opp_table(struct device *dev,
			    struct rockchip_opp_info *info,
			    char *lkg_name, char *reg_name);
#else
static inline int rockchip_of_get_leakage(struct device *dev, char *lkg_name,
					  int *leakage)
{
	return -EOPNOTSUPP;
}

static inline void rockchip_of_get_lkg_sel(struct device *dev,
					   struct device_node *np,
					   char *lkg_name, int process,
					   int *volt_sel, int *scale_sel)
{
}

static inline void rockchip_pvtpll_calibrate_opp(struct rockchip_opp_info *info)
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

static inline int rockchip_get_volt_rm_table(struct device *dev,
					     struct device_node *np,
					     char *porp_name,
					     struct volt_rm_table **table)
{
	return -EOPNOTSUPP;

}

static inline void rockchip_get_opp_data(const struct of_device_id *matches,
					 struct rockchip_opp_info *info)
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
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int rockchip_adjust_power_scale(struct device *dev, int scale)
{
	return -EOPNOTSUPP;
}

static inline int rockchip_get_read_margin(struct device *dev,
					   struct rockchip_opp_info *opp_info,
					   unsigned long volt, u32 *target_rm)
{
	return -EOPNOTSUPP;
}
static inline int rockchip_set_read_margin(struct device *dev,
					   struct rockchip_opp_info *opp_info,
					   u32 rm, bool is_set_rm)
{
	return -EOPNOTSUPP;
}

static inline int
rockchip_set_intermediate_rate(struct device *dev,
			       struct rockchip_opp_info *opp_info,
			       struct clk *clk, unsigned long old_freq,
			       unsigned long new_freq, bool is_scaling_up,
			       bool is_set_clk)
{
	return -EOPNOTSUPP;
}

static inline int rockchip_init_opp_table(struct device *dev,
					  struct rockchip_opp_info *info,
					  char *lkg_name, char *reg_name)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_ROCKCHIP_OPP */

#endif
