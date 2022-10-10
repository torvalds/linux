/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
 */

#ifndef __DW_HDMI__
#define __DW_HDMI__

#include <drm/drm_property.h>
#include <drm/drm_crtc.h>
#include <sound/hdmi-codec.h>
#include <media/cec.h>

struct drm_display_info;
struct drm_display_mode;
struct drm_encoder;
struct dw_hdmi;
struct dw_hdmi_qp;
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

#define SUPPORT_HDMI_ALLM	BIT(1)

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

struct dw_hdmi_audio_frl_n {
	unsigned int r_bit;
	unsigned int n_32k;
	unsigned int n_44k1;
	unsigned int n_48k;
};

struct dw_hdmi_audio_tmds_n {
	unsigned long tmds;
	unsigned int n_32k;
	unsigned int n_44k1;
	unsigned int n_48k;
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

struct dw_hdmi_link_config {
	bool dsc_mode;
	bool frl_mode;
	int frl_lanes;
	int rate_per_lane;
	int hcactive;
	u8 add_func;
	u8 pps_payload[128];
};

struct dw_hdmi_phy_ops {
	int (*init)(struct dw_hdmi *hdmi, void *data,
		    const struct drm_display_info *display,
		    const struct drm_display_mode *mode);
	void (*disable)(struct dw_hdmi *hdmi, void *data);
	enum drm_connector_status (*read_hpd)(struct dw_hdmi *hdmi, void *data);
	void (*update_hpd)(struct dw_hdmi *hdmi, void *data,
			   bool force, bool disabled, bool rxsense);
	void (*setup_hpd)(struct dw_hdmi *hdmi, void *data);
};

struct dw_hdmi_qp_phy_ops {
	int (*init)(struct dw_hdmi_qp *hdmi, void *data,
		    struct drm_display_mode *mode);
	void (*disable)(struct dw_hdmi_qp *hdmi, void *data);
	enum drm_connector_status (*read_hpd)(struct dw_hdmi_qp *hdmi,
					      void *data);
	void (*update_hpd)(struct dw_hdmi_qp *hdmi, void *data,
			   bool force, bool disabled, bool rxsense);
	void (*setup_hpd)(struct dw_hdmi_qp *hdmi, void *data);
	void (*set_mode)(struct dw_hdmi_qp *dw_hdmi, void *data,
			 u32 mode_mask, bool enable);
};

struct dw_hdmi_property_ops {
	void (*attach_properties)(struct drm_connector *connector,
				  unsigned int color, int version,
				  void *data, bool allm_en);
	void (*destroy_properties)(struct drm_connector *connector,
				   void *data);
	int (*set_property)(struct drm_connector *connector,
			    struct drm_connector_state *state,
			    struct drm_property *property,
			    u64 val,
			    void *data);
	int (*get_property)(struct drm_connector *connector,
			    const struct drm_connector_state *state,
			    struct drm_property *property,
			    u64 *val,
			    void *data);
};

struct dw_hdmi_plat_data {
	struct regmap *regm;

	unsigned long input_bus_format;
	unsigned long input_bus_encoding;
	unsigned int max_tmdsclk;
	int id;
	bool use_drm_infoframe;
	bool ycbcr_420_allowed;
	bool unsupported_yuv_input;
	bool unsupported_deep_color;
	bool is_hdmi_qp;

	/*
	 * Private data passed to all the .mode_valid() and .configure_phy()
	 * callback functions.
	 */
	void *priv_data;

	/* Platform-specific mode validation (optional). */
	enum drm_mode_status (*mode_valid)(struct drm_connector *connector,
					   void *data,
					   const struct drm_display_info *info,
					   const struct drm_display_mode *mode);

	/* Vendor PHY support */
	const struct dw_hdmi_phy_ops *phy_ops;
	const struct dw_hdmi_qp_phy_ops *qp_phy_ops;
	const char *phy_name;
	void *phy_data;
	unsigned int phy_force_vendor;
	const struct dw_hdmi_audio_tmds_n *tmds_n_table;

	/* split mode */
	bool split_mode;
	bool first_screen;
	struct dw_hdmi_qp *left;
	struct dw_hdmi_qp *right;

	/* Synopsys PHY support */
	const struct dw_hdmi_mpll_config *mpll_cfg;
	const struct dw_hdmi_mpll_config *mpll_cfg_420;
	const struct dw_hdmi_curr_ctrl *cur_ctr;
	const struct dw_hdmi_phy_config *phy_config;
	int (*configure_phy)(struct dw_hdmi *hdmi, void *data,
			     unsigned long mpixelclock);

	unsigned long (*get_input_bus_format)(void *data);
	unsigned long (*get_output_bus_format)(void *data);
	unsigned long (*get_enc_in_encoding)(void *data);
	unsigned long (*get_enc_out_encoding)(void *data);
	unsigned long (*get_quant_range)(void *data);
	struct drm_property *(*get_hdr_property)(void *data);
	struct drm_property_blob *(*get_hdr_blob)(void *data);
	bool (*get_color_changed)(void *data);
	int (*get_yuv422_format)(struct drm_connector *connector,
				 struct edid *edid);
	int (*get_edid_dsc_info)(void *data, struct edid *edid);
	int (*get_next_hdr_data)(void *data, struct edid *edid,
				 struct drm_connector *connector);
	struct dw_hdmi_link_config *(*get_link_cfg)(void *data);
	void (*set_grf_cfg)(void *data);
	u64 (*get_grf_color_fmt)(void *data);
	void (*convert_to_split_mode)(struct drm_display_mode *mode);
	void (*convert_to_origin_mode)(struct drm_display_mode *mode);
	int (*dclk_set)(void *data, bool enable, int vp_id);
	int (*link_clk_set)(void *data, bool enable);
	int (*get_vp_id)(struct drm_crtc_state *crtc_state);

	/* Vendor Property support */
	const struct dw_hdmi_property_ops *property_ops;
	struct drm_connector *connector;
};

struct dw_hdmi *dw_hdmi_probe(struct platform_device *pdev,
			      const struct dw_hdmi_plat_data *plat_data);
void dw_hdmi_remove(struct dw_hdmi *hdmi);
void dw_hdmi_unbind(struct dw_hdmi *hdmi);
struct dw_hdmi *dw_hdmi_bind(struct platform_device *pdev,
			     struct drm_encoder *encoder,
			     struct dw_hdmi_plat_data *plat_data);

void dw_hdmi_suspend(struct dw_hdmi *hdmi);
void dw_hdmi_resume(struct dw_hdmi *hdmi);

void dw_hdmi_setup_rx_sense(struct dw_hdmi *hdmi, bool hpd, bool rx_sense);

int dw_hdmi_set_plugged_cb(struct dw_hdmi *hdmi, hdmi_codec_plugged_cb fn,
			   struct device *codec_dev);
void dw_hdmi_set_sample_rate(struct dw_hdmi *hdmi, unsigned int rate);
void dw_hdmi_set_channel_count(struct dw_hdmi *hdmi, unsigned int cnt);
void dw_hdmi_set_channel_status(struct dw_hdmi *hdmi, u8 *channel_status);
void dw_hdmi_set_channel_allocation(struct dw_hdmi *hdmi, unsigned int ca);
void dw_hdmi_audio_enable(struct dw_hdmi *hdmi);
void dw_hdmi_audio_disable(struct dw_hdmi *hdmi);
void dw_hdmi_set_high_tmds_clock_ratio(struct dw_hdmi *hdmi,
				       const struct drm_display_info *display);

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
void dw_hdmi_set_quant_range(struct dw_hdmi *hdmi);
void dw_hdmi_set_output_type(struct dw_hdmi *hdmi, u64 val);
bool dw_hdmi_get_output_whether_hdmi(struct dw_hdmi *hdmi);
int dw_hdmi_get_output_type_cap(struct dw_hdmi *hdmi);
void dw_hdmi_set_cec_adap(struct dw_hdmi *hdmi, struct cec_adapter *adap);
void dw_hdmi_qp_set_allm_enable(struct dw_hdmi_qp *hdmi_qp, bool enable);

void dw_hdmi_qp_unbind(struct dw_hdmi_qp *hdmi);
struct dw_hdmi_qp *dw_hdmi_qp_bind(struct platform_device *pdev,
				struct drm_encoder *encoder,
				struct dw_hdmi_plat_data *plat_data);
void dw_hdmi_qp_suspend(struct device *dev, struct dw_hdmi_qp *hdmi);
void dw_hdmi_qp_resume(struct device *dev, struct dw_hdmi_qp *hdmi);
void dw_hdmi_qp_cec_set_hpd(struct dw_hdmi_qp *hdmi, bool plug_in, bool change);
void dw_hdmi_qp_set_cec_adap(struct dw_hdmi_qp *hdmi, struct cec_adapter *adap);
int dw_hdmi_qp_set_earc(struct dw_hdmi_qp *hdmi);
void dw_hdmi_qp_set_sample_rate(struct dw_hdmi_qp *hdmi, unsigned int rate);
void dw_hdmi_qp_set_channel_count(struct dw_hdmi_qp *hdmi, unsigned int cnt);
void dw_hdmi_qp_set_channel_status(struct dw_hdmi_qp *hdmi, u8 *channel_status,
				   bool ref2stream);
void dw_hdmi_qp_set_channel_allocation(struct dw_hdmi_qp *hdmi, unsigned int ca);
void dw_hdmi_qp_set_audio_infoframe(struct dw_hdmi_qp *hdmi,
				    struct hdmi_codec_params *hparms);
void dw_hdmi_qp_audio_enable(struct dw_hdmi_qp *hdmi);
void dw_hdmi_qp_audio_disable(struct dw_hdmi_qp *hdmi);
int dw_hdmi_qp_set_plugged_cb(struct dw_hdmi_qp *hdmi, hdmi_codec_plugged_cb fn,
			      struct device *codec_dev);
void dw_hdmi_qp_set_output_type(struct dw_hdmi_qp *hdmi, u64 val);
bool dw_hdmi_qp_get_output_whether_hdmi(struct dw_hdmi_qp *hdmi);
int dw_hdmi_qp_get_output_type_cap(struct dw_hdmi_qp *hdmi);

#endif /* __IMX_HDMI_H__ */
