/*
 * Copyright (c) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#ifndef __SOC_ROCKCHIP_OPP_SELECT_H
#define __SOC_ROCKCHIP_OPP_SELECT_H

int rockchip_of_get_lkg_scale_sel(struct device *dev, char *name);
int rockchip_of_get_lkg_volt_sel(struct device *dev, char *name);
int rockchip_of_get_pvtm_volt_sel(struct device *dev,
				  char *clk_name,
				  char *reg_name);
int rockchip_adjust_opp_by_irdrop(struct device *dev);

#endif
