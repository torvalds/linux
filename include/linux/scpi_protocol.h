/*
 * SCPI Message Protocol driver header
 *
 * Copyright (C) 2014 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/types.h>
#include <linux/rockchip/common.h>

struct scpi_opp_entry {
	u32 freq_hz;
	u32 volt_mv;
} __packed;

struct scpi_opp {
	struct scpi_opp_entry *opp;
	u32 latency; /* in usecs */
	int count;
} __packed;

unsigned long scpi_clk_get_val(u16 clk_id);
int scpi_clk_set_val(u16 clk_id, unsigned long rate);
int scpi_dvfs_get_idx(u8 domain);
int scpi_dvfs_set_idx(u8 domain, u8 idx);
struct scpi_opp *scpi_dvfs_get_opps(u8 domain);
int scpi_get_sensor(char *name);
int scpi_get_sensor_value(u16 sensor, u32 *val);

int scpi_sys_set_mcu_state_suspend(void);
int scpi_sys_set_mcu_state_resume(void);

int scpi_ddr_init(u32 dram_speed_bin, u32 freq, u32 lcdc_type);
int scpi_ddr_set_clk_rate(u32 rate);
int scpi_ddr_round_rate(u32 m_hz);
int scpi_ddr_set_auto_self_refresh(u32 en);
int scpi_ddr_bandwidth_get(struct ddr_bw_info *ddr_bw_ch0,
			   struct ddr_bw_info *ddr_bw_ch1);
int scpi_ddr_get_clk_rate(void);
int scpi_thermal_get_temperature(void);

