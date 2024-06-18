/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __WCD_MBHC_V2_H__
#define __WCD_MBHC_V2_H__

#include <sound/jack.h>

#define WCD_MBHC_FIELD(id, rreg, rmask) \
	[id] = { .reg = rreg, .mask = rmask }

enum wcd_mbhc_field_function {
	WCD_MBHC_L_DET_EN,
	WCD_MBHC_GND_DET_EN,
	WCD_MBHC_MECH_DETECTION_TYPE,
	WCD_MBHC_MIC_CLAMP_CTL,
	WCD_MBHC_ELECT_DETECTION_TYPE,
	WCD_MBHC_HS_L_DET_PULL_UP_CTRL,
	WCD_MBHC_HS_L_DET_PULL_UP_COMP_CTRL,
	WCD_MBHC_HPHL_PLUG_TYPE,
	WCD_MBHC_GND_PLUG_TYPE,
	WCD_MBHC_SW_HPH_LP_100K_TO_GND,
	WCD_MBHC_ELECT_SCHMT_ISRC,
	WCD_MBHC_FSM_EN,
	WCD_MBHC_INSREM_DBNC,
	WCD_MBHC_BTN_DBNC,
	WCD_MBHC_HS_VREF,
	WCD_MBHC_HS_COMP_RESULT,
	WCD_MBHC_IN2P_CLAMP_STATE,
	WCD_MBHC_MIC_SCHMT_RESULT,
	WCD_MBHC_HPHL_SCHMT_RESULT,
	WCD_MBHC_HPHR_SCHMT_RESULT,
	WCD_MBHC_OCP_FSM_EN,
	WCD_MBHC_BTN_RESULT,
	WCD_MBHC_BTN_ISRC_CTL,
	WCD_MBHC_ELECT_RESULT,
	WCD_MBHC_MICB_CTRL,    /* Pull-up and micb control */
	WCD_MBHC_HPH_CNP_WG_TIME,
	WCD_MBHC_HPHR_PA_EN,
	WCD_MBHC_HPHL_PA_EN,
	WCD_MBHC_HPH_PA_EN,
	WCD_MBHC_SWCH_LEVEL_REMOVE,
	WCD_MBHC_PULLDOWN_CTRL,
	WCD_MBHC_ANC_DET_EN,
	WCD_MBHC_FSM_STATUS,
	WCD_MBHC_MUX_CTL,
	WCD_MBHC_MOISTURE_STATUS,
	WCD_MBHC_HPHR_GND,
	WCD_MBHC_HPHL_GND,
	WCD_MBHC_HPHL_OCP_DET_EN,
	WCD_MBHC_HPHR_OCP_DET_EN,
	WCD_MBHC_HPHL_OCP_STATUS,
	WCD_MBHC_HPHR_OCP_STATUS,
	WCD_MBHC_ADC_EN,
	WCD_MBHC_ADC_COMPLETE,
	WCD_MBHC_ADC_TIMEOUT,
	WCD_MBHC_ADC_RESULT,
	WCD_MBHC_MICB2_VOUT,
	WCD_MBHC_ADC_MODE,
	WCD_MBHC_DETECTION_DONE,
	WCD_MBHC_ELECT_ISRC_EN,
	WCD_MBHC_REG_FUNC_MAX,
};

#define WCD_MBHC_DEF_BUTTONS 8
#define WCD_MBHC_KEYCODE_NUM 8
#define WCD_MBHC_USLEEP_RANGE_MARGIN_US 100
#define WCD_MBHC_THR_HS_MICB_MV  2700
#define WCD_MONO_HS_MIN_THR	2

enum wcd_mbhc_detect_logic {
	WCD_DETECTION_LEGACY,
	WCD_DETECTION_ADC,
};

enum wcd_mbhc_cs_mb_en_flag {
	WCD_MBHC_EN_CS = 0,
	WCD_MBHC_EN_MB,
	WCD_MBHC_EN_PULLUP,
	WCD_MBHC_EN_NONE,
};

enum {
	WCD_MBHC_ELEC_HS_INS,
	WCD_MBHC_ELEC_HS_REM,
};

enum wcd_mbhc_plug_type {
	MBHC_PLUG_TYPE_INVALID = -1,
	MBHC_PLUG_TYPE_NONE,
	MBHC_PLUG_TYPE_HEADSET,
	MBHC_PLUG_TYPE_HEADPHONE,
	MBHC_PLUG_TYPE_HIGH_HPH,
	MBHC_PLUG_TYPE_GND_MIC_SWAP,
};

enum pa_dac_ack_flags {
	WCD_MBHC_HPHL_PA_OFF_ACK = 0,
	WCD_MBHC_HPHR_PA_OFF_ACK,
};

enum wcd_mbhc_btn_det_mem {
	WCD_MBHC_BTN_DET_V_BTN_LOW,
	WCD_MBHC_BTN_DET_V_BTN_HIGH
};

enum {
	MIC_BIAS_1 = 1,
	MIC_BIAS_2,
	MIC_BIAS_3,
	MIC_BIAS_4
};

enum {
	MICB_PULLUP_ENABLE,
	MICB_PULLUP_DISABLE,
	MICB_ENABLE,
	MICB_DISABLE,
};

enum wcd_notify_event {
	WCD_EVENT_INVALID,
	/* events for micbias ON and OFF */
	WCD_EVENT_PRE_MICBIAS_2_OFF,
	WCD_EVENT_POST_MICBIAS_2_OFF,
	WCD_EVENT_PRE_MICBIAS_2_ON,
	WCD_EVENT_POST_MICBIAS_2_ON,
	WCD_EVENT_PRE_DAPM_MICBIAS_2_OFF,
	WCD_EVENT_POST_DAPM_MICBIAS_2_OFF,
	WCD_EVENT_PRE_DAPM_MICBIAS_2_ON,
	WCD_EVENT_POST_DAPM_MICBIAS_2_ON,
	/* events for PA ON and OFF */
	WCD_EVENT_PRE_HPHL_PA_ON,
	WCD_EVENT_POST_HPHL_PA_OFF,
	WCD_EVENT_PRE_HPHR_PA_ON,
	WCD_EVENT_POST_HPHR_PA_OFF,
	WCD_EVENT_PRE_HPHL_PA_OFF,
	WCD_EVENT_PRE_HPHR_PA_OFF,
	WCD_EVENT_OCP_OFF,
	WCD_EVENT_OCP_ON,
	WCD_EVENT_LAST,
};

enum wcd_mbhc_event_state {
	WCD_MBHC_EVENT_PA_HPHL,
	WCD_MBHC_EVENT_PA_HPHR,
};

enum wcd_mbhc_hph_type {
	WCD_MBHC_HPH_NONE = 0,
	WCD_MBHC_HPH_MONO,
	WCD_MBHC_HPH_STEREO,
};

/*
 * These enum definitions are directly mapped to the register
 * definitions
 */

enum mbhc_hs_pullup_iref {
	I_DEFAULT = -1,
	I_OFF = 0,
	I_1P0_UA,
	I_2P0_UA,
	I_3P0_UA,
};

enum mbhc_hs_pullup_iref_v2 {
	HS_PULLUP_I_DEFAULT = -1,
	HS_PULLUP_I_3P0_UA = 0,
	HS_PULLUP_I_2P25_UA,
	HS_PULLUP_I_1P5_UA,
	HS_PULLUP_I_0P75_UA,
	HS_PULLUP_I_1P125_UA = 0x05,
	HS_PULLUP_I_0P375_UA = 0x07,
	HS_PULLUP_I_2P0_UA,
	HS_PULLUP_I_1P0_UA = 0x0A,
	HS_PULLUP_I_0P5_UA,
	HS_PULLUP_I_0P25_UA = 0x0F,
	HS_PULLUP_I_0P125_UA = 0x17,
	HS_PULLUP_I_OFF,
};

enum mbhc_moisture_rref {
	R_OFF,
	R_24_KOHM,
	R_84_KOHM,
	R_184_KOHM,
};

struct wcd_mbhc_config {
	int btn_high[WCD_MBHC_DEF_BUTTONS];
	int btn_low[WCD_MBHC_DEF_BUTTONS];
	int v_hs_max;
	int num_btn;
	bool mono_stero_detection;
	bool typec_analog_mux;
	bool (*swap_gnd_mic)(struct snd_soc_component *component, bool active);
	bool hs_ext_micbias;
	bool gnd_det_en;
	uint32_t linein_th;
	bool moisture_en;
	int mbhc_micbias;
	int anc_micbias;
	bool moisture_duty_cycle_en;
	bool hphl_swh; /*track HPHL switch NC / NO */
	bool gnd_swh; /*track GND switch NC / NO */
	u32 hs_thr;
	u32 hph_thr;
	u32 micb_mv;
	u32 moist_vref;
	u32 moist_iref;
	u32 moist_rref;
};

struct wcd_mbhc_intr {
	int mbhc_sw_intr;
	int mbhc_btn_press_intr;
	int mbhc_btn_release_intr;
	int mbhc_hs_ins_intr;
	int mbhc_hs_rem_intr;
	int hph_left_ocp;
	int hph_right_ocp;
};

struct wcd_mbhc_field {
	u16 reg;
	u8 mask;
};

struct wcd_mbhc;

struct wcd_mbhc_cb {
	void (*update_cross_conn_thr)(struct snd_soc_component *component);
	void (*get_micbias_val)(struct snd_soc_component *component, int *mb);
	void (*bcs_enable)(struct snd_soc_component *component, bool bcs_enable);
	void (*compute_impedance)(struct snd_soc_component *component,
				  uint32_t *zl, uint32_t *zr);
	void (*set_micbias_value)(struct snd_soc_component *component);
	void (*set_auto_zeroing)(struct snd_soc_component *component,
			bool enable);
	void (*clk_setup)(struct snd_soc_component *component, bool enable);
	bool (*micbias_enable_status)(struct snd_soc_component *component, int micb_num);
	void (*mbhc_bias)(struct snd_soc_component *component, bool enable);
	void (*set_btn_thr)(struct snd_soc_component *component,
			    int *btn_low, int *btn_high,
			    int num_btn, bool is_micbias);
	void (*hph_pull_up_control)(struct snd_soc_component *component,
				    enum mbhc_hs_pullup_iref);
	int (*mbhc_micbias_control)(struct snd_soc_component *component,
			int micb_num, int req);
	void (*mbhc_micb_ramp_control)(struct snd_soc_component *component,
			bool enable);
	bool (*extn_use_mb)(struct snd_soc_component *component);
	int (*mbhc_micb_ctrl_thr_mic)(struct snd_soc_component *component,
			int micb_num, bool req_en);
	void (*mbhc_gnd_det_ctrl)(struct snd_soc_component *component,
			bool enable);
	void (*hph_pull_down_ctrl)(struct snd_soc_component *component,
			bool enable);
	void (*mbhc_moisture_config)(struct snd_soc_component *component);
	void (*update_anc_state)(struct snd_soc_component *component,
			bool enable, int anc_num);
	void (*hph_pull_up_control_v2)(struct snd_soc_component *component,
			int pull_up_cur);
	bool (*mbhc_get_moisture_status)(struct snd_soc_component *component);
	void (*mbhc_moisture_polling_ctrl)(struct snd_soc_component *component, bool enable);
	void (*mbhc_moisture_detect_en)(struct snd_soc_component *component, bool enable);
};

#if IS_ENABLED(CONFIG_SND_SOC_WCD_MBHC)
int wcd_dt_parse_mbhc_data(struct device *dev, struct wcd_mbhc_config *cfg);
int wcd_mbhc_start(struct wcd_mbhc *mbhc, struct wcd_mbhc_config *mbhc_cfg,
		   struct snd_soc_jack *jack);
void wcd_mbhc_stop(struct wcd_mbhc *mbhc);
void wcd_mbhc_set_hph_type(struct wcd_mbhc *mbhc, int hph_type);
int wcd_mbhc_get_hph_type(struct wcd_mbhc *mbhc);
int wcd_mbhc_typec_report_plug(struct wcd_mbhc *mbhc);
int wcd_mbhc_typec_report_unplug(struct wcd_mbhc *mbhc);
struct wcd_mbhc *wcd_mbhc_init(struct snd_soc_component *component,
		      const struct wcd_mbhc_cb *mbhc_cb,
		      const struct wcd_mbhc_intr *mbhc_cdc_intr_ids,
		      struct wcd_mbhc_field *fields,
		      bool impedance_det_en);
int wcd_mbhc_get_impedance(struct wcd_mbhc *mbhc, uint32_t *zl,
			   uint32_t *zr);
void wcd_mbhc_deinit(struct wcd_mbhc *mbhc);
int wcd_mbhc_event_notify(struct wcd_mbhc *mbhc, unsigned long event);

#else
static inline int wcd_dt_parse_mbhc_data(struct device *dev,
					 struct wcd_mbhc_config *cfg)
{
	return -ENOTSUPP;
}

static inline void wcd_mbhc_stop(struct wcd_mbhc *mbhc)
{
}

static inline struct wcd_mbhc *wcd_mbhc_init(struct snd_soc_component *component,
		      const struct wcd_mbhc_cb *mbhc_cb,
		      const struct wcd_mbhc_intr *mbhc_cdc_intr_ids,
		      struct wcd_mbhc_field *fields,
		      bool impedance_det_en)
{
	return ERR_PTR(-ENOTSUPP);
}

static inline void wcd_mbhc_set_hph_type(struct wcd_mbhc *mbhc, int hph_type)
{
}

static inline int wcd_mbhc_get_hph_type(struct wcd_mbhc *mbhc)
{
	return -ENOTSUPP;
}

static inline int wcd_mbhc_event_notify(struct wcd_mbhc *mbhc, unsigned long event)
{
	return -ENOTSUPP;
}

static inline int wcd_mbhc_start(struct wcd_mbhc *mbhc,
				 struct wcd_mbhc_config *mbhc_cfg,
				 struct snd_soc_jack *jack)
{
	return 0;
}

static inline int wcd_mbhc_get_impedance(struct wcd_mbhc *mbhc,
					 uint32_t *zl,
					 uint32_t *zr)
{
	*zl = 0;
	*zr = 0;
	return -EINVAL;
}
static inline void wcd_mbhc_deinit(struct wcd_mbhc *mbhc)
{
}
#endif

#endif /* __WCD_MBHC_V2_H__ */
