/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#ifndef _RK628_H
#define _RK628_H

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>

#include <drm/drm_crtc_helper.h>

#define UPDATE(x, h, l)		(((x) << (l)) & GENMASK((h), (l)))
#define HIWORD_UPDATE(v, h, l)	(((v) << (l)) | (GENMASK((h), (l)) << 16))

#define GRF_SYSTEM_CON0			0x0000
#define SW_VSYNC_POL_MASK		BIT(26)
#define SW_VSYNC_POL(x)			UPDATE(x, 26, 26)
#define SW_HSYNC_POL_MASK		BIT(25)
#define SW_HSYNC_POL(x)			UPDATE(x, 25, 25)
#define SW_ADAPTER_I2CSLADR_MASK	GENMASK(24, 22)
#define SW_ADAPTER_I2CSLADR(x)		UPDATE(x, 24, 22)
#define SW_EDID_MODE_MASK		BIT(21)
#define SW_EDID_MODE(x)			UPDATE(x, 21, 21)
#define SW_I2S_DATA_OEN_MASK		BIT(10)
#define SW_I2S_DATA_OEN(x)		UPDATE(x, 10, 10)
#define SW_BT_DATA_OEN_MASK		BIT(9)
#define SW_BT_DATA_OEN			BIT(9)
#define SW_EFUSE_HDCP_EN_MASK		BIT(8)
#define SW_EFUSE_HDCP_EN(x)		UPDATE(x, 8, 8)
#define SW_OUTPUT_MODE_MASK		GENMASK(7, 3)
#define SW_OUTPUT_MODE(x)		UPDATE(x, 7, 3)
#define SW_INPUT_MODE_MASK		GENMASK(2, 0)
#define SW_INPUT_MODE(x)		UPDATE(x, 2, 0)
#define GRF_SYSTEM_CON1			0x0004
#define GRF_SYSTEM_CON2			0x0008
#define GRF_SYSTEM_CON3			0x000c
#define GRF_GPIO_RX_CEC_SEL_MASK	BIT(7)
#define GRF_GPIO_RX_CEC_SEL(x)		UPDATE(x, 7, 7)
#define GRF_GPIO_RXDDC_SDA_SEL_MASK	BIT(6)
#define GRF_GPIO_RXDDC_SDA_SEL(x)	UPDATE(x, 6, 6)
#define GRF_GPIO_RXDDC_SCL_SEL_MASK	BIT(5)
#define GRF_GPIO_RXDDC_SCL_SEL(x)	UPDATE(x, 5, 5)
#define GRF_SCALER_CON0			0x0010
#define SCL_VER_DOWN_MODE(x)		HIWORD_UPDATE(x, 8, 8)
#define SCL_HOR_DOWN_MODE(x)		HIWORD_UPDATE(x, 7, 7)
#define SCL_BIC_COE_SEL(x)		HIWORD_UPDATE(x, 6, 5)
#define SCL_VER_MODE(x)			HIWORD_UPDATE(x, 4, 3)
#define SCL_HOR_MODE(x)			HIWORD_UPDATE(x, 2, 1)
#define SCL_EN(x)			HIWORD_UPDATE(x, 0, 0)
#define GRF_SCALER_CON1			0x0014
#define SCL_V_FACTOR(x)			UPDATE(x, 31, 16)
#define SCL_H_FACTOR(x)			UPDATE(x, 15, 0)
#define GRF_SCALER_CON2			0x0018
#define DSP_FRAME_VST(x)		UPDATE(x, 28, 16)
#define DSP_FRAME_HST(x)		UPDATE(x, 12, 0)
#define GRF_SCALER_CON3			0x001c
#define DSP_HS_END(x)			UPDATE(x, 23, 16)
#define DSP_HTOTAL(x)			UPDATE(x, 12, 0)
#define GRF_SCALER_CON4			0x0020
#define DSP_HACT_ST(x)			UPDATE(x, 28, 16)
#define DSP_HACT_END(x)			UPDATE(x, 12, 0)
#define GRF_SCALER_CON5			0x0024
#define DSP_VS_END(x)			UPDATE(x, 23, 16)
#define DSP_VTOTAL(x)			UPDATE(x, 12, 0)
#define GRF_SCALER_CON6			0x0028
#define DSP_VACT_ST(x)			UPDATE(x, 28, 16)
#define DSP_VACT_END(x)			UPDATE(x, 12, 0)
#define GRF_SCALER_CON7			0x002c
#define DSP_HBOR_ST(x)			UPDATE(x, 28, 16)
#define DSP_HBOR_END(x)			UPDATE(x, 12, 0)
#define GRF_SCALER_CON8			0x0030
#define DSP_VBOR_ST(x)			UPDATE(x, 28, 16)
#define DSP_VBOR_END(x)			UPDATE(x, 12, 0)
#define GRF_POST_PROC_CON		0x0034
#define SW_DCLK_OUT_INV_EN		BIT(9)
#define SW_DCLK_IN_INV_EN		BIT(8)
#define SW_TXPHY_REFCLK_SEL_MASK	GENMASK(6, 5)
#define SW_TXPHY_REFCLK_SEL(x)		UPDATE(x, 6, 5)
#define SW_HDMITX_VCLK_PLLREF_SEL_MASK	BIT(4)
#define SW_HDMITX_VCLK_PLLREF_SEL(x)	UPDATE(x, 4, 4)
#define SW_HDMITX_DCLK_INV_EN		BIT(3)
#define SW_SPLIT_MODE(x)		UPDATE(x, 1, 1)
#define SW_SPLIT_EN			BIT(0)
#define GRF_CSC_CTRL_CON		0x0038
#define SW_YUV2VYU_SWP(x)		HIWORD_UPDATE(x, 8, 8)
#define SW_R2Y_EN(x)			HIWORD_UPDATE(x, 4, 4)
#define SW_Y2R_EN(x)			HIWORD_UPDATE(x, 0, 0)
#define GRF_LVDS_TX_CON			0x003c
#define SW_LVDS_CON_DUAL_SEL(x)		HIWORD_UPDATE(x, 12, 12)
#define SW_LVDS_CON_DEN_POLARITY(x)	HIWORD_UPDATE(x, 11, 11)
#define SW_LVDS_CON_HS_POLARITY(x)	HIWORD_UPDATE(x, 10, 10)
#define SW_LVDS_CON_CLKINV(x)		HIWORD_UPDATE(x, 9, 9)
#define SW_LVDS_STARTPHASE(x)		HIWORD_UPDATE(x, 8, 8)
#define SW_LVDS_CON_STARTSEL(x)		HIWORD_UPDATE(x, 7, 7)
#define SW_LVDS_CON_CHASEL(x)		HIWORD_UPDATE(x, 6, 6)
#define SW_LVDS_TIE_VSYNC_VALUE(x)	HIWORD_UPDATE(x, 5, 5)
#define SW_LVDS_TIE_HSYNC_VALUE(x)	HIWORD_UPDATE(x, 4, 4)
#define SW_LVDS_TIE_DEN_ONLY(x)		HIWORD_UPDATE(x, 3, 3)
#define SW_LVDS_CON_MSBSEL(x)		HIWORD_UPDATE(x, 2, 2)
#define SW_LVDS_CON_SELECT(x)		HIWORD_UPDATE(x, 1, 0)
#define GRF_RGB_DEC_CON0		0x0040
#define SW_HRES_MASK			GENMASK(28, 16)
#define SW_HRES(x)			UPDATE(x, 28, 16)
#define DUAL_DATA_SWAP			BIT(6)
#define DEC_DUALEDGE_EN			BIT(5)
#define SW_PROGRESS_EN			BIT(4)
#define SW_YC_SWAP			BIT(3)
#define SW_CAP_EN_ASYNC			BIT(1)
#define SW_CAP_EN_PSYNC			BIT(0)
#define GRF_RGB_DEC_CON1		0x0044
#define SW_SET_X_MASK			GENMASK(28, 16)
#define SW_SET_X(x)			HIWORD_UPDATE(x, 28, 16)
#define SW_SET_Y_MASK			GENMASK(28, 16)
#define SW_SET_Y(x)			HIWORD_UPDATE(x, 28, 16)
#define GRF_RGB_DEC_CON2		0x0048
#define GRF_RGB_ENC_CON			0x004c
#define BT1120_UV_SWAP(x)		HIWORD_UPDATE(x, 5, 5)
#define ENC_DUALEDGE_EN(x)		HIWORD_UPDATE(x, 3, 3)
#define GRF_MIPI_LANE_DELAY_CON0	0x0050
#define GRF_MIPI_LANE_DELAY_CON1	0x0054
#define GRF_BT1120_DCLK_DELAY_CON0	0x0058
#define GRF_BT1120_DCLK_DELAY_CON1	0x005c
#define GRF_MIPI_TX0_CON		0x0060
#define DPIUPDATECFG			BIT(26)
#define DPICOLORM			BIT(25)
#define DPISHUTDN			BIT(24)
#define CSI_PHYRSTZ			BIT(21)
#define CSI_PHYSHUTDOWNZ		BIT(20)
#define FORCETXSTOPMODE_MASK		GENMASK(19, 16)
#define FORCETXSTOPMODE(x)		UPDATE(x, 19, 16)
#define FORCERXMODE_MASK		GENMASK(15, 12)
#define FORCERXMODE(x)			UPDATE(x, 15, 12)
#define PHY_TESTCLR			BIT(10)
#define PHY_TESTCLK			BIT(9)
#define PHY_TESTEN			BIT(8)
#define PHY_TESTDIN_MASK		GENMASK(7, 0)
#define PHY_TESTDIN(x)			UPDATE(x, 7, 0)
#define GRF_DPHY0_STATUS		0x0064
#define DPHY_PHYLOCK			BIT(24)
#define PHY_TESTDOUT_SHIFT		8
#define GRF_MIPI_TX1_CON		0x0068
#define GRF_DPHY1_STATUS		0x006c
#define GRF_GPIO0AB_SEL_CON		0x0070
#define GRF_GPIO1AB_SEL_CON		0x0074
#define GRF_GPIO2AB_SEL_CON		0x0078
#define GRF_GPIO2C_SEL_CON		0x007c
#define GRF_GPIO3AB_SEL_CON		0x0080
#define GRF_GPIO2A_SMT			0x0090
#define GRF_GPIO2B_SMT			0x0094
#define GRF_GPIO2C_SMT			0x0098
#define GRF_GPIO3AB_SMT			0x009c
#define GRF_GPIO0A_P_CON		0x00a0
#define GRF_GPIO1A_P_CON		0x00a4
#define GRF_GPIO2A_P_CON		0x00a8
#define GRF_GPIO2B_P_CON		0x00ac
#define GRF_GPIO2C_P_CON		0x00b0
#define GRF_GPIO3A_P_CON		0x00b4
#define GRF_GPIO3B_P_CON		0x00b8
#define GRF_GPIO0B_D_CON		0x00c0
#define GRF_GPIO1B_D_CON		0x00c4
#define GRF_GPIO2A_D0_CON		0x00c8
#define GRF_GPIO2A_D1_CON		0x00cc
#define GRF_GPIO2B_D0_CON		0x00d0
#define GRF_GPIO2B_D1_CON		0x00d4
#define GRF_GPIO2C_D0_CON		0x00d8
#define GRF_GPIO2C_D1_CON		0x00dc
#define GRF_GPIO3A_D0_CON		0x00e0
#define GRF_GPIO3A_D1_CON		0x00e4
#define GRF_GPIO3B_D_CON		0x00e8
#define GRF_GPIO_SR_CON			0x00ec
#define GRF_INTR0_EN			0x0100
#define GRF_INTR0_CLR_EN		0x0104
#define GRF_INTR0_STATUS		0x0108
#define GRF_INTR0_RAW_STATUS		0x010c
#define GRF_INTR1_EN			0x0110
#define GRF_INTR1_CLR_EN		0x0114
#define GRF_INTR1_STATUS		0x0118
#define GRF_INTR1_RAW_STATUS		0x011c
#define GRF_SYSTEM_STATUS0		0x0120
/* 0: i2c mode and mcu mode; 1: i2c mode only */
#define I2C_ONLY_FLAG			BIT(6)
#define GRF_SYSTEM_STATUS3		0x012c
#define GRF_SYSTEM_STATUS4		0x0130
#define GRF_OS_REG0			0x0140
#define GRF_OS_REG1			0x0144
#define GRF_OS_REG2			0x0148
#define GRF_OS_REG3			0x014c
#define GRF_SOC_VERSION			0x0150
#define GRF_MAX_REGISTER		GRF_SOC_VERSION

enum {
	COMBTXPHY_MODULEA_EN = BIT(0),
	COMBTXPHY_MODULEB_EN = BIT(1),
};

enum {
	OUTPUT_MODE_GVI = 1,
	OUTPUT_MODE_LVDS,
	OUTPUT_MODE_HDMI,
	OUTPUT_MODE_CSI,
	OUTPUT_MODE_DSI,
	OUTPUT_MODE_BT1120 = 8,
	OUTPUT_MODE_RGB = 16,
	OUTPUT_MODE_YUV = 24,
};

enum {
	INPUT_MODE_HDMI,
	INPUT_MODE_BT1120 = 2,
	INPUT_MODE_RGB,
	INPUT_MODE_YUV,
};

struct rk628_irq_chip_data {
	const char *name;
	unsigned int status_base;
	unsigned int mask_base;
	unsigned int ack_base;
	int num_regs;
	const struct regmap_irq *irqs;
	int num_irqs;
	struct mutex lock;
	struct irq_chip irq_chip;
	struct regmap *map;
	struct irq_domain *domain;
	int irq;
	unsigned int *status_buf;
	unsigned int *mask_buf;
	unsigned int *mask_buf_def;
	unsigned int irq_reg_stride;
	unsigned int reg_stride;
};

struct rk628 {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *grf;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct rk628_irq_chip_data *irq_data;
	struct drm_display_mode src_mode;
	struct drm_display_mode dst_mode;
	bool dst_mode_valid;
};

/**
 * rk628_scaler_add_src_mode - add source mode for scaler
 * @rk628: parent device
 * @connector: DRM connector
 * If need scale, call the function at last of get_modes.
 */
int rk628_scaler_add_src_mode(struct rk628 *rk628,
			      struct drm_connector *connector);

/**
 * rk628_mode_copy - rk628 mode copy
 * @rk628: parent device
 * @dst: dst mode
 * @src: src mode
 * Call the function at mode_set, replace drm_mode_copy.
 */
void rk628_mode_copy(struct rk628 *rk628, struct drm_display_mode *dst,
		     const struct drm_display_mode *src);

#endif
