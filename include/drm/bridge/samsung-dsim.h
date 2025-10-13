/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2022 Amarula Solutions(India)
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#ifndef __SAMSUNG_DSIM__
#define __SAMSUNG_DSIM__

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>

struct platform_device;
struct samsung_dsim;

#define DSIM_STATE_ENABLED		BIT(0)
#define DSIM_STATE_INITIALIZED		BIT(1)
#define DSIM_STATE_CMD_LPM		BIT(2)
#define DSIM_STATE_VIDOUT_AVAILABLE	BIT(3)

enum samsung_dsim_type {
	DSIM_TYPE_EXYNOS3250,
	DSIM_TYPE_EXYNOS4210,
	DSIM_TYPE_EXYNOS5410,
	DSIM_TYPE_EXYNOS5422,
	DSIM_TYPE_EXYNOS5433,
	DSIM_TYPE_EXYNOS7870,
	DSIM_TYPE_IMX8MM,
	DSIM_TYPE_IMX8MP,
	DSIM_TYPE_COUNT,
};

#define samsung_dsim_hw_is_exynos(hw) \
	((hw) >= DSIM_TYPE_EXYNOS3250 && (hw) <= DSIM_TYPE_EXYNOS5433)

struct samsung_dsim_transfer {
	struct list_head list;
	struct completion completed;
	int result;
	struct mipi_dsi_packet packet;
	u16 flags;
	u16 tx_done;

	u8 *rx_payload;
	u16 rx_len;
	u16 rx_done;
};

struct samsung_dsim_driver_data {
	const unsigned int *reg_ofs;
	unsigned int plltmr_reg;
	unsigned int has_legacy_status_reg:1;
	unsigned int has_freqband:1;
	unsigned int has_clklane_stop:1;
	unsigned int has_broken_fifoctrl_emptyhdr:1;
	unsigned int has_sfrctrl:1;
	struct clk_bulk_data *clk_data;
	unsigned int num_clks;
	unsigned int min_freq;
	unsigned int max_freq;
	unsigned int wait_for_hdr_fifo;
	unsigned int wait_for_reset;
	unsigned int num_bits_resol;
	unsigned int video_mode_bit;
	unsigned int pll_stable_bit;
	unsigned int esc_clken_bit;
	unsigned int byte_clken_bit;
	unsigned int tx_req_hsclk_bit;
	unsigned int lane_esc_clk_bit;
	unsigned int lane_esc_data_offset;
	unsigned int pll_p_offset;
	unsigned int pll_m_offset;
	unsigned int pll_s_offset;
	unsigned int main_vsa_offset;
	const unsigned int *reg_values;
	unsigned int pll_fin_min;
	unsigned int pll_fin_max;
	u16 m_min;
	u16 m_max;
};

struct samsung_dsim_host_ops {
	int (*register_host)(struct samsung_dsim *dsim);
	void (*unregister_host)(struct samsung_dsim *dsim);
	int (*attach)(struct samsung_dsim *dsim, struct mipi_dsi_device *device);
	void (*detach)(struct samsung_dsim *dsim, struct mipi_dsi_device *device);
	irqreturn_t (*te_irq_handler)(struct samsung_dsim *dsim);
};

struct samsung_dsim_plat_data {
	enum samsung_dsim_type hw_type;
	const struct samsung_dsim_host_ops *host_ops;
};

struct samsung_dsim {
	struct mipi_dsi_host dsi_host;
	struct drm_bridge bridge;
	struct drm_bridge *out_bridge;
	struct device *dev;
	struct drm_display_mode mode;

	void __iomem *reg_base;
	struct phy *phy;
	struct clk *pll_clk;
	struct regulator_bulk_data supplies[2];
	int irq;
	struct gpio_desc *te_gpio;

	u32 pll_clk_rate;
	u32 burst_clk_rate;
	u32 hs_clock;
	u32 esc_clk_rate;
	u32 lanes;
	u32 mode_flags;
	u32 format;

	bool swap_dn_dp_clk;
	bool swap_dn_dp_data;
	int state;
	struct drm_property *brightness;
	struct completion completed;

	spinlock_t transfer_lock; /* protects transfer_list */
	struct list_head transfer_list;

	const struct samsung_dsim_driver_data *driver_data;
	const struct samsung_dsim_plat_data *plat_data;

	void *priv;
};

extern int samsung_dsim_probe(struct platform_device *pdev);
extern void samsung_dsim_remove(struct platform_device *pdev);
extern const struct dev_pm_ops samsung_dsim_pm_ops;

#endif /* __SAMSUNG_DSIM__ */
