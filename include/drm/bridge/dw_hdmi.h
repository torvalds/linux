/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
 */

#ifndef __DW_HDMI__
#define __DW_HDMI__

struct drm_connector;
struct drm_display_mode;
struct drm_encoder;
struct dw_hdmi;
struct platform_device;

/**
 * DOC: Supported input formats and encodings
 *
 * Depending on the Hardware configuration of the Controller IP, it supports
 * a subset of the following input formats and encodings on its internal
 * 48bit bus.
 *
 * +----------------------+----------------------------------+------------------------------+
 * | Format Name          | Format Code                      | Encodings                    |
 * +----------------------+----------------------------------+------------------------------+
 * | RGB 4:4:4 8bit       | ``MEDIA_BUS_FMT_RGB888_1X24``    | ``V4L2_YCBCR_ENC_DEFAULT``   |
 * +----------------------+----------------------------------+------------------------------+
 * | RGB 4:4:4 10bits     | ``MEDIA_BUS_FMT_RGB101010_1X30`` | ``V4L2_YCBCR_ENC_DEFAULT``   |
 * +----------------------+----------------------------------+------------------------------+
 * | RGB 4:4:4 12bits     | ``MEDIA_BUS_FMT_RGB121212_1X36`` | ``V4L2_YCBCR_ENC_DEFAULT``   |
 * +----------------------+----------------------------------+------------------------------+
 * | RGB 4:4:4 16bits     | ``MEDIA_BUS_FMT_RGB161616_1X48`` | ``V4L2_YCBCR_ENC_DEFAULT``   |
 * +----------------------+----------------------------------+------------------------------+
 * | YCbCr 4:4:4 8bit     | ``MEDIA_BUS_FMT_YUV8_1X24``      | ``V4L2_YCBCR_ENC_601``       |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_709``    |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_XV601``  |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_XV709``  |
 * +----------------------+----------------------------------+------------------------------+
 * | YCbCr 4:4:4 10bits   | ``MEDIA_BUS_FMT_YUV10_1X30``     | ``V4L2_YCBCR_ENC_601``       |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_709``    |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_XV601``  |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_XV709``  |
 * +----------------------+----------------------------------+------------------------------+
 * | YCbCr 4:4:4 12bits   | ``MEDIA_BUS_FMT_YUV12_1X36``     | ``V4L2_YCBCR_ENC_601``       |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_709``    |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_XV601``  |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_XV709``  |
 * +----------------------+----------------------------------+------------------------------+
 * | YCbCr 4:4:4 16bits   | ``MEDIA_BUS_FMT_YUV16_1X48``     | ``V4L2_YCBCR_ENC_601``       |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_709``    |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_XV601``  |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_XV709``  |
 * +----------------------+----------------------------------+------------------------------+
 * | YCbCr 4:2:2 8bit     | ``MEDIA_BUS_FMT_UYVY8_1X16``     | ``V4L2_YCBCR_ENC_601``       |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_709``    |
 * +----------------------+----------------------------------+------------------------------+
 * | YCbCr 4:2:2 10bits   | ``MEDIA_BUS_FMT_UYVY10_1X20``    | ``V4L2_YCBCR_ENC_601``       |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_709``    |
 * +----------------------+----------------------------------+------------------------------+
 * | YCbCr 4:2:2 12bits   | ``MEDIA_BUS_FMT_UYVY12_1X24``    | ``V4L2_YCBCR_ENC_601``       |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_709``    |
 * +----------------------+----------------------------------+------------------------------+
 * | YCbCr 4:2:0 8bit     | ``MEDIA_BUS_FMT_UYYVYY8_0_5X24`` | ``V4L2_YCBCR_ENC_601``       |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_709``    |
 * +----------------------+----------------------------------+------------------------------+
 * | YCbCr 4:2:0 10bits   | ``MEDIA_BUS_FMT_UYYVYY10_0_5X30``| ``V4L2_YCBCR_ENC_601``       |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_709``    |
 * +----------------------+----------------------------------+------------------------------+
 * | YCbCr 4:2:0 12bits   | ``MEDIA_BUS_FMT_UYYVYY12_0_5X36``| ``V4L2_YCBCR_ENC_601``       |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_709``    |
 * +----------------------+----------------------------------+------------------------------+
 * | YCbCr 4:2:0 16bits   | ``MEDIA_BUS_FMT_UYYVYY16_0_5X48``| ``V4L2_YCBCR_ENC_601``       |
 * |                      |                                  | or ``V4L2_YCBCR_ENC_709``    |
 * +----------------------+----------------------------------+------------------------------+
 */

enum {
	DW_HDMI_RES_8,
	DW_HDMI_RES_10,
	DW_HDMI_RES_12,
	DW_HDMI_RES_MAX,
};

enum dw_hdmi_phy_type {
	DW_HDMI_PHY_DWC_HDMI_TX_PHY = 0x00,
	DW_HDMI_PHY_DWC_MHL_PHY_HEAC = 0xb2,
	DW_HDMI_PHY_DWC_MHL_PHY = 0xc2,
	DW_HDMI_PHY_DWC_HDMI_3D_TX_PHY_HEAC = 0xe2,
	DW_HDMI_PHY_DWC_HDMI_3D_TX_PHY = 0xf2,
	DW_HDMI_PHY_DWC_HDMI20_TX_PHY = 0xf3,
	DW_HDMI_PHY_VENDOR_PHY = 0xfe,
};

struct dw_hdmi_mpll_config {
	unsigned long mpixelclock;
	struct {
		u16 cpce;
		u16 gmp;
	} res[DW_HDMI_RES_MAX];
};

struct dw_hdmi_curr_ctrl {
	unsigned long mpixelclock;
	u16 curr[DW_HDMI_RES_MAX];
};

struct dw_hdmi_phy_config {
	unsigned long mpixelclock;
	u16 sym_ctr;    /*clock symbol and transmitter control*/
	u16 term;       /*transmission termination value*/
	u16 vlev_ctr;   /* voltage level control */
};

struct dw_hdmi_phy_ops {
	int (*init)(struct dw_hdmi *hdmi, void *data,
		    struct drm_display_mode *mode);
	void (*disable)(struct dw_hdmi *hdmi, void *data);
	enum drm_connector_status (*read_hpd)(struct dw_hdmi *hdmi, void *data);
	void (*update_hpd)(struct dw_hdmi *hdmi, void *data,
			   bool force, bool disabled, bool rxsense);
	void (*setup_hpd)(struct dw_hdmi *hdmi, void *data);
};

struct dw_hdmi_plat_data {
	struct regmap *regm;
	enum drm_mode_status (*mode_valid)(struct drm_connector *connector,
					   const struct drm_display_mode *mode);
	unsigned long input_bus_format;
	unsigned long input_bus_encoding;
	bool use_drm_infoframe;

	/* Vendor PHY support */
	const struct dw_hdmi_phy_ops *phy_ops;
	const char *phy_name;
	void *phy_data;
	unsigned int phy_force_vendor;

	/* Synopsys PHY support */
	const struct dw_hdmi_mpll_config *mpll_cfg;
	const struct dw_hdmi_curr_ctrl *cur_ctr;
	const struct dw_hdmi_phy_config *phy_config;
	int (*configure_phy)(struct dw_hdmi *hdmi,
			     const struct dw_hdmi_plat_data *pdata,
			     unsigned long mpixelclock);
};

struct dw_hdmi *dw_hdmi_probe(struct platform_device *pdev,
			      const struct dw_hdmi_plat_data *plat_data);
void dw_hdmi_remove(struct dw_hdmi *hdmi);
void dw_hdmi_unbind(struct dw_hdmi *hdmi);
struct dw_hdmi *dw_hdmi_bind(struct platform_device *pdev,
			     struct drm_encoder *encoder,
			     const struct dw_hdmi_plat_data *plat_data);

void dw_hdmi_resume(struct dw_hdmi *hdmi);

void dw_hdmi_setup_rx_sense(struct dw_hdmi *hdmi, bool hpd, bool rx_sense);

void dw_hdmi_set_sample_rate(struct dw_hdmi *hdmi, unsigned int rate);
void dw_hdmi_set_channel_count(struct dw_hdmi *hdmi, unsigned int cnt);
void dw_hdmi_set_channel_status(struct dw_hdmi *hdmi, u8 *channel_status);
void dw_hdmi_set_channel_allocation(struct dw_hdmi *hdmi, unsigned int ca);
void dw_hdmi_audio_enable(struct dw_hdmi *hdmi);
void dw_hdmi_audio_disable(struct dw_hdmi *hdmi);
void dw_hdmi_set_high_tmds_clock_ratio(struct dw_hdmi *hdmi);

/* PHY configuration */
void dw_hdmi_phy_i2c_set_addr(struct dw_hdmi *hdmi, u8 address);
void dw_hdmi_phy_i2c_write(struct dw_hdmi *hdmi, unsigned short data,
			   unsigned char addr);

void dw_hdmi_phy_gen2_pddq(struct dw_hdmi *hdmi, u8 enable);
void dw_hdmi_phy_gen2_txpwron(struct dw_hdmi *hdmi, u8 enable);
void dw_hdmi_phy_reset(struct dw_hdmi *hdmi);

enum drm_connector_status dw_hdmi_phy_read_hpd(struct dw_hdmi *hdmi,
					       void *data);
void dw_hdmi_phy_update_hpd(struct dw_hdmi *hdmi, void *data,
			    bool force, bool disabled, bool rxsense);
void dw_hdmi_phy_setup_hpd(struct dw_hdmi *hdmi, void *data);

#endif /* __IMX_HDMI_H__ */
