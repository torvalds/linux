// SPDX-License-Identifier: GPL-2.0-only
//
// es9356.c -- SoundWire codec driver
//
// Copyright(c) 2025 Everest Semiconductor Co., Ltd
//
//

#include <linux/device.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>
#include <sound/sdw.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <sound/sdca_function.h>
#include <sound/sdca_regmap.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <sound/jack.h>
#include <sound/sdca_asoc.h>
#include "es9356.h"

struct  es9356_sdw_priv {
	struct sdw_slave *slave;
	struct device *dev;
	struct regmap *regmap;
	struct snd_soc_component *component;
	struct snd_soc_jack *hs_jack;

	/* lock for irq*/
	struct mutex disable_irq_lock;

	/* lock for pde*/
	struct mutex pde_lock;

	bool hw_init;
	bool first_hw_init;
	int jack_type;
	bool disable_irq;

	struct delayed_work interrupt_handle_work;
	struct delayed_work button_detect_work;
	unsigned int sdca_status;
};

static int es9356_sdw_component_probe(struct snd_soc_component *component)
{
	struct es9356_sdw_priv *es9356 = snd_soc_component_get_drvdata(component);

	es9356->component = component;

	return 0;
}

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -9600, 12, 0);
static const DECLARE_TLV_DB_SCALE(amic_gain_tlv, 0, 3, 0);
static const DECLARE_TLV_DB_SCALE(dmic_gain_tlv, 0, 6, 0);

static const struct snd_kcontrol_new es9356_sdca_controls[] = {
	SDCA_SINGLE_Q78_TLV("FU41 Left Playback Volume",
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU41, ES9356_SDCA_CTL_FU_VOLUME, CH_L),
		ES9356_VOLUME_MIN, ES9356_VOLUME_MAX, ES9356_VOLUME_STEP, out_vol_tlv),
	SDCA_SINGLE_Q78_TLV("FU41 Right Playback Volume",
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU41, ES9356_SDCA_CTL_FU_VOLUME, CH_R),
		ES9356_VOLUME_MIN, ES9356_VOLUME_MAX, ES9356_VOLUME_STEP, out_vol_tlv),
	SDCA_SINGLE_Q78_TLV("FU36 Left Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU36, ES9356_SDCA_CTL_FU_VOLUME, CH_L),
		ES9356_VOLUME_MIN, ES9356_VOLUME_MAX, ES9356_VOLUME_STEP, out_vol_tlv),
	SDCA_SINGLE_Q78_TLV("FU36 Right Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU36, ES9356_SDCA_CTL_FU_VOLUME, CH_R),
		ES9356_VOLUME_MIN, ES9356_VOLUME_MAX, ES9356_VOLUME_STEP, out_vol_tlv),
	SDCA_SINGLE_Q78_TLV("FU33 Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU33, ES9356_SDCA_CTL_FU_CH_GAIN, 0),
		ES9356_GAIN_MIN, ES9356_AMIC_GAIN_MAX, ES9356_AMIC_GAIN_STEP, amic_gain_tlv),
	SDCA_SINGLE_Q78_TLV("FU21 Left Playback Volume",
		SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT_FU21, ES9356_SDCA_CTL_FU_VOLUME, CH_L),
		ES9356_VOLUME_MIN, ES9356_VOLUME_MAX, ES9356_VOLUME_STEP, out_vol_tlv),
	SDCA_SINGLE_Q78_TLV("FU21 Right Playback Volume",
		SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT_FU21, ES9356_SDCA_CTL_FU_VOLUME, CH_R),
		ES9356_VOLUME_MIN, ES9356_VOLUME_MAX, ES9356_VOLUME_STEP, out_vol_tlv),
	SDCA_SINGLE_Q78_TLV("FU113 Left Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_FU113, ES9356_SDCA_CTL_FU_VOLUME, CH_L),
		ES9356_VOLUME_MIN, ES9356_VOLUME_MAX, ES9356_VOLUME_STEP, out_vol_tlv),
	SDCA_SINGLE_Q78_TLV("FU113 Right Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_FU113, ES9356_SDCA_CTL_FU_VOLUME, CH_R),
		ES9356_VOLUME_MIN, ES9356_VOLUME_MAX, ES9356_VOLUME_STEP, out_vol_tlv),
	SDCA_SINGLE_Q78_TLV("FU11 Capture Volume",
		SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_FU11, ES9356_SDCA_CTL_FU_CH_GAIN, 0),
		ES9356_GAIN_MIN, ES9356_DMIC_GAIN_MAX, ES9356_DMIC_GAIN_STEP, dmic_gain_tlv),
};

static const char *const es9356_left_mux_txt[] = {
	"Left",
	"Right",
};

static const char *const es9356_right_mux_txt[] = {
	"Right",
	"Left",
};

static const struct soc_enum es9356_left_mux_enum =
	SOC_ENUM_SINGLE(ES9356_DAC_SWAP, 1,
			ARRAY_SIZE(es9356_left_mux_txt), es9356_left_mux_txt);
static const struct soc_enum es9356_right_mux_enum =
	SOC_ENUM_SINGLE(ES9356_DAC_SWAP, 0,
			ARRAY_SIZE(es9356_right_mux_txt), es9356_right_mux_txt);

static const struct snd_kcontrol_new es9356_left_mux_controls =
	SOC_DAPM_ENUM("Channel MUX", es9356_left_mux_enum);
static const struct snd_kcontrol_new es9356_right_mux_controls =
	SOC_DAPM_ENUM("Channel MUX", es9356_right_mux_enum);

static const struct snd_soc_dapm_widget es9356_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_OUTPUT("SPK"),
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("PDM_DIN"),

	SND_SOC_DAPM_SUPPLY("DMIC Clock", ES9356_DMIC_GPIO, 1, 1, NULL, 0),

	SND_SOC_DAPM_AIF_IN("DP4RX", "DP4 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("DP3RX", "DP3 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP1TX", "DP1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP2TX", "DP2 Capture", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_PGA("IF DP3RXL", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF DP3RXR", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("Left Channel MUX", SND_SOC_NOPM, 0, 0, &es9356_left_mux_controls),
	SND_SOC_DAPM_MUX("Right Channel MUX", SND_SOC_NOPM, 0, 0, &es9356_right_mux_controls),

	SND_SOC_DAPM_DAC("FU 21 Left", NULL,
		SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT_FU21, ES9356_SDCA_CTL_FU_MUTE, CH_L), 0, 1),
	SND_SOC_DAPM_DAC("FU 21 Right", NULL,
		SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT_FU21, ES9356_SDCA_CTL_FU_MUTE, CH_R), 0, 1),
	SND_SOC_DAPM_DAC("FU 41 Left", NULL,
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU41, ES9356_SDCA_CTL_FU_MUTE, CH_L), 0, 1),
	SND_SOC_DAPM_DAC("FU 41 Right", NULL,
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU41, ES9356_SDCA_CTL_FU_MUTE, CH_R), 0, 1),
	SND_SOC_DAPM_DAC("FU 113 Left", NULL,
		SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_FU113, ES9356_SDCA_CTL_FU_MUTE, CH_L), 0, 1),
	SND_SOC_DAPM_DAC("FU 113 Right", NULL,
		SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_FU113, ES9356_SDCA_CTL_FU_MUTE, CH_R), 0, 1),
	SND_SOC_DAPM_DAC("FU 36 Left", NULL,
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU36, ES9356_SDCA_CTL_FU_MUTE, CH_L), 0, 1),
	SND_SOC_DAPM_DAC("FU 36 Right", NULL,
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU36, ES9356_SDCA_CTL_FU_MUTE, CH_R), 0, 1),
};

static const struct snd_soc_dapm_route es9356_audio_map[] = {
	{"FU 36 Left", NULL, "MIC1"},
	{"FU 36 Right", NULL, "MIC1"},
	{"DP2TX", NULL, "FU 36 Left"},
	{"DP2TX", NULL, "FU 36 Right"},

	{"PDM_DIN", NULL, "DMIC Clock"},
	{"FU 113 Left", NULL, "PDM_DIN"},
	{"FU 113 Right", NULL, "PDM_DIN"},
	{"DP1TX", NULL, "FU 113 Left"},
	{"DP1TX", NULL, "FU 113 Right"},

	{"FU 41 Left", NULL, "DP4RX"},
	{"FU 41 Right", NULL, "DP4RX"},

	{"IF DP3RXL", NULL, "DP3RX"},
	{"IF DP3RXR", NULL, "DP3RX"},

	{"Left Channel MUX", "Left", "IF DP3RXL"},
	{"Left Channel MUX", "Right", "IF DP3RXR"},
	{"Right Channel MUX", "Left", "IF DP3RXL"},
	{"Right Channel MUX", "Right", "IF DP3RXR"},

	{"FU 21 Left", NULL, "Left Channel MUX"},
	{"FU 21 Right", NULL, "Right Channel MUX"},

	{"SPK", NULL, "FU 21 Left"},
	{"SPK", NULL, "FU 21 Right"},

	{"HP", NULL, "FU 41 Left"},
	{"HP", NULL, "FU 41 Right"},
};

static int es9356_set_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *hs_jack, void *data)
{
	struct es9356_sdw_priv *es9356 = snd_soc_component_get_drvdata(component);
	int ret;

	es9356->hs_jack = hs_jack;

	/* we can only resume if the device was initialized at least once */
	if (!es9356->first_hw_init)
		return 0;

	ret = pm_runtime_resume_and_get(component->dev);
	if (ret < 0) {
		if (ret != -EACCES) {
			dev_err(component->dev, "%s: failed to resume %d\n", __func__, ret);
			return ret;
		}
		/* pm_runtime not enabled yet */
		dev_info(component->dev, "%s: skipping jack init for now\n", __func__);
		return 0;
	}

	if (es9356->hs_jack)
		sdw_write_no_pm(es9356->slave, SDW_SCP_SDCA_INTMASK1,
			(SDW_SCP_SDCA_INTMASK_SDCA_7 | SDW_SCP_SDCA_INTMASK_SDCA_5 | SDW_SCP_SDCA_INTMASK_SDCA_1));

	pm_runtime_mark_last_busy(component->dev);
	pm_runtime_put_autosuspend(component->dev);

	return 0;
}
static const struct snd_soc_component_driver snd_soc_es9356_sdw_component = {
	.probe = es9356_sdw_component_probe,
	.controls = es9356_sdca_controls,
	.num_controls = ARRAY_SIZE(es9356_sdca_controls),
	.dapm_widgets = es9356_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(es9356_dapm_widgets),
	.dapm_routes = es9356_audio_map,
	.num_dapm_routes = ARRAY_SIZE(es9356_audio_map),
	.set_jack = es9356_set_jack_detect,
	.endianness = 1,
};

static int es9356_sdw_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				     int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void es9356_sdw_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static int es9356_sdca_button(unsigned int *buffer)
{
	int cur_button = -1;

	if (*(buffer + 1) | *(buffer + 2))
		return -EINVAL;
	switch (*buffer) {
	case 0x00:
		cur_button = 0;
		break;
	case 0x20:
		cur_button = SND_JACK_BTN_4;
		break;
	case 0x10:
		cur_button = SND_JACK_BTN_2;
		break;
	case 0x08:
		cur_button = SND_JACK_BTN_1;
		break;
	case 0x02:
		cur_button = SND_JACK_BTN_3;
		break;
	case 0x01:
		cur_button = SND_JACK_BTN_0;
		break;
	default:
		break;
	}

	return cur_button;
}

static int es9356_sdca_button_detect(struct es9356_sdw_priv *es9356)
{
	unsigned int btn_type = 0, offset, idx, val, owner;
	unsigned int button[3];
	int ret;

	ret = regmap_read(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_HID, ES9356_SDCA_ENT_HID01, ES9356_SDCA_CTL_HIDTX_CURRENT_OWNER, 0), &owner);
	if (ret < 0 || owner == 0x01)
		return 0;

	ret = regmap_read(es9356->regmap, ES9356_BUF_ADDR_HID, &offset);
	if (ret < 0)
		goto button_det_end;

	for (idx = 0; idx < ARRAY_SIZE(button); idx++) {
		ret = regmap_read(es9356->regmap, ES9356_BUF_ADDR_HID + offset + idx, &val);
		if (ret < 0)
			goto button_det_end;
		button[idx] = val;
	}

	btn_type = es9356_sdca_button(&button[0]);

button_det_end:
	if (owner == 0x00)
		regmap_write(es9356->regmap,
			SDW_SDCA_CTL(FUNC_NUM_HID, ES9356_SDCA_ENT_HID01, ES9356_SDCA_CTL_HIDTX_CURRENT_OWNER, 0), 0x01);

	return btn_type;
}

static int es9356_sdca_headset_detect(struct es9356_sdw_priv *es9356)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(es9356->regmap,
			SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_GE35, ES9356_SDCA_CTL_DETECTED_MODE, 0), &reg);

	if (ret < 0)
		goto io_error;

	switch (reg) {
	case 0x00:
		es9356->jack_type = 0;
		break;
	case 0x03:
		es9356->jack_type = SND_JACK_HEADPHONE;
		break;
	case 0x04:
		es9356->jack_type = SND_JACK_HEADSET;
		break;
	default:
		es9356->jack_type = 0;
		return -1;
	}

	if (reg) {
		regmap_write(es9356->regmap,
			SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_GE35, ES9356_SDCA_CTL_SELECTED_MODE, 0), reg);
		regmap_write(es9356->regmap, ES9356_HP_DETECTTIME, 0x75);
	} else {
		regmap_write(es9356->regmap, ES9356_HP_DETECTTIME, 0xa4);
	}

	return 0;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
	return ret;
}

static void es9356_interrupt_handler(struct work_struct *work)
{
	struct es9356_sdw_priv *es9356 =
		container_of(work, struct es9356_sdw_priv, interrupt_handle_work.work);
	int ret, btn_type = 0;

	if (!es9356->hs_jack)
		return;

	if (!es9356->component->card || !es9356->component->card->instantiated)
		return;

	/* Handling different types of interrupts based on the mask bit */
	if (es9356->sdca_status & SDW_SCP_SDCA_INT_SDCA_7) {
		btn_type = es9356_sdca_button_detect(es9356);
		if (btn_type < 0)
			return;
	} else {
		ret = es9356_sdca_headset_detect(es9356);
		if (ret < 0)
			return;
	}

	if (es9356->jack_type != SND_JACK_HEADSET)
		btn_type = 0;

	snd_soc_jack_report(es9356->hs_jack, es9356->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3 |
			SND_JACK_BTN_4);

	if (btn_type) {
		snd_soc_jack_report(es9356->hs_jack, es9356->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3 |
			SND_JACK_BTN_4);
		mod_delayed_work(system_power_efficient_wq,
			&es9356->button_detect_work, msecs_to_jiffies(280));
	}
}

static void es9356_button_detect_handler(struct work_struct *work)
{
	struct es9356_sdw_priv *es9356 =
		container_of(work, struct es9356_sdw_priv, button_detect_work.work);
	int ret, idx, btn_type = 0;
	unsigned int reg, offset;
	unsigned int button[3];

	/* Check headset */
	ret = regmap_read(es9356->regmap,
			SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_GE35, ES9356_SDCA_CTL_DETECTED_MODE, 0), &reg);

	if (ret < 0)
		goto io_error;

	if (reg == 0x04) {
		ret = regmap_read(es9356->regmap, ES9356_BUF_ADDR_HID, &offset);
		if (ret < 0)
			goto io_error;
		for (idx = 0; idx < ARRAY_SIZE(button); idx++) {
			ret = regmap_read(es9356->regmap, ES9356_BUF_ADDR_HID + offset + idx, &reg);
			if (ret < 0)
				goto io_error;
			button[idx] = reg;
		}
		btn_type = es9356_sdca_button(&button[0]);
		if (btn_type < 0)
			return;
	}

	snd_soc_jack_report(es9356->hs_jack, es9356->jack_type | btn_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3 |
			SND_JACK_BTN_4);

	if (btn_type) {
		snd_soc_jack_report(es9356->hs_jack, es9356->jack_type,
			SND_JACK_HEADSET |
			SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			SND_JACK_BTN_2 | SND_JACK_BTN_3 |
			SND_JACK_BTN_4);
		mod_delayed_work(system_power_efficient_wq,
			&es9356->button_detect_work, msecs_to_jiffies(280));
	}

	return;
io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
}

static int es9356_pde_transition_delay(struct es9356_sdw_priv *es9356, unsigned char func,
	unsigned char entity, unsigned char ps)
{
	unsigned int retries = 10, val;

	/* waiting for Actual PDE becomes to PS0/PS3 */
	while (retries) {
		regmap_read(es9356->regmap,
			SDW_SDCA_CTL(func, entity, ES9356_SDCA_CTL_ACTUAL_POWER_STATE, 0), &val);
		if (val == ps)
			return 1;

		usleep_range(1000, 1500);
		retries--;
	}
	if (!retries) {
		dev_dbg(&es9356->slave->dev, "%s PDE is NOT %s", __func__, ps?"PS3":"PS0");
	}
	return 0;
}

static int es9356_power_state(struct snd_soc_dai *dai, unsigned char ps, unsigned int *rate)
{
	struct snd_soc_component *component = dai->component;
	struct es9356_sdw_priv *es9356 = snd_soc_component_get_drvdata(component);
	unsigned char ps0 = 0x0, ps3 = 0x3;
	unsigned char func, cs_entity, pde_entity;
	int ret;

	switch (dai->id) {
	case ES9356_DMIC:
		func = FUNC_NUM_MIC;
		cs_entity = ES9356_SDCA_ENT_CS113;
		pde_entity = ES9356_SDCA_ENT_PDE11;
		break;
	case ES9356_AMP:
		func = FUNC_NUM_AMP;
		cs_entity = ES9356_SDCA_ENT_CS21;
		pde_entity = ES9356_SDCA_ENT_PDE23;
		break;
	case ES9356_JACK_IN:
		func = FUNC_NUM_UAJ;
		cs_entity = ES9356_SDCA_ENT_CS36;
		pde_entity = ES9356_SDCA_ENT_PDE34;
		break;
	case ES9356_JACK_OUT:
		func = FUNC_NUM_UAJ;
		cs_entity = ES9356_SDCA_ENT_CS41;
		pde_entity = ES9356_SDCA_ENT_PDE47;
		break;
	default:
		return -EINVAL;
	}

	/* power state changes are not independent across functions */
	mutex_lock(&es9356->pde_lock);
	ret = es9356_pde_transition_delay(es9356, func, pde_entity, ps?ps0:ps3);
	if (ret) {
		regmap_write(es9356->regmap,
			SDW_SDCA_CTL(func, pde_entity, ES9356_SDCA_CTL_REQ_POWER_STATE, 0), ps?ps3:ps0);
		es9356_pde_transition_delay(es9356, func, pde_entity, ps?ps3:ps0);
	} else
		dev_dbg(component->dev, "%s PDE is already %d\n", __func__, ps?ps0:ps3);

	mutex_unlock(&es9356->pde_lock);

	if (rate)
		regmap_write(es9356->regmap,
			SDW_SDCA_CTL(func, cs_entity, ES9356_SDCA_CTL_SAMPLE_FREQ_INDEX, 0), *rate);

	return 0;
}

static int es9356_sdw_pcm_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es9356_sdw_priv *es9356 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config = {0};
	struct sdw_port_config port_config = {0};
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	unsigned char ps0 = 0x0;
	unsigned int rate;
	int ret;

	if (!sdw_stream)
		return -EINVAL;

	if (!es9356->slave)
		return -EINVAL;

	/* SoundWire specific configuration */
	snd_sdw_params_to_config(substream, params, &stream_config, &port_config);

	port_config.num = dai->id;

	ret = sdw_stream_add_slave(es9356->slave, &stream_config,
				   &port_config, 1, sdw_stream);
	if (ret) {
		dev_err(dai->dev, "Unable to configure port\n");
		return -EINVAL;
	}

	switch (params_rate(params)) {
	case 16000:
		rate = ES9356_SDCA_RATE_16000HZ;
		break;
	case 44100:
		rate = ES9356_SDCA_RATE_44100HZ;
		break;
	case 48000:
		rate = ES9356_SDCA_RATE_48000HZ;
		break;
	case 96000:
		rate = ES9356_SDCA_RATE_96000HZ;
		break;
	default:
		dev_err(component->dev, "%s: Rate %d is not supported\n",
			__func__, params_rate(params));
		return -EINVAL;
	}

	ret = es9356_power_state(dai, ps0, &rate);
	if (ret) {
		dev_err(component->dev, "%s: Invalid dai id: %d\n",
			__func__, dai->id);
		return -EINVAL;
	}

	return 0;
}

static int es9356_sdw_pcm_hw_free(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es9356_sdw_priv *es9356 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	unsigned char ps3 = 0x3;
	int ret;

	if (!es9356->slave)
		return -EINVAL;

	sdw_stream_remove_slave(es9356->slave, sdw_stream);

	ret = es9356_power_state(dai, ps3, NULL);
	if (ret) {
		dev_err(component->dev, "%s: Invalid dai id: %d\n",
			__func__, dai->id);
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops es9356_sdw_ops = {
	.hw_params	= es9356_sdw_pcm_hw_params,
	.hw_free	= es9356_sdw_pcm_hw_free,
	.set_stream	= es9356_sdw_set_sdw_stream,
	.shutdown	= es9356_sdw_shutdown,
};

static struct snd_soc_dai_driver es9356_sdw_dai[] = {
	{
		.name = "es9356-sdp-aif4",
		.id = ES9356_DMIC,
		.capture = {
			.stream_name = "DP1 Capture",
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &es9356_sdw_ops,
	},
	{
		.name = "es9356-sdp-aif2",
		.id = ES9356_JACK_IN,
		.capture = {
			.stream_name = "DP2 Capture",
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &es9356_sdw_ops,
	},
	{
		.name = "es9356-sdp-aif3",
		.id = ES9356_AMP,
		.playback = {
			.stream_name = "DP3 Playback",
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &es9356_sdw_ops,
	},
	{
		.name = "es9356-sdp-aif1",
		.id = ES9356_JACK_OUT,
		.playback = {
			.stream_name = "DP4 Playback",
			.channels_min = 1,
			.channels_max = 2,
		},
		.ops = &es9356_sdw_ops,
	},
};

static int es9356_sdca_mbq_size(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_FU113, ES9356_SDCA_CTL_FU_VOLUME, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_FU113, ES9356_SDCA_CTL_FU_VOLUME, CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT_FU21, ES9356_SDCA_CTL_FU_VOLUME, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT_FU21, ES9356_SDCA_CTL_FU_VOLUME, CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU41, ES9356_SDCA_CTL_FU_VOLUME, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU41, ES9356_SDCA_CTL_FU_VOLUME, CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU36, ES9356_SDCA_CTL_FU_VOLUME, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU36, ES9356_SDCA_CTL_FU_VOLUME, CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU33, ES9356_SDCA_CTL_FU_CH_GAIN, 0):
	case SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_FU11, ES9356_SDCA_CTL_FU_CH_GAIN, 0):
		return 2;
	default:
		return 1;
	}
}

static struct regmap_sdw_mbq_cfg es9356_mbq_config = {
	.mbq_size = es9356_sdca_mbq_size,
};

static bool es9356_sdca_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ES9356_BUF_ADDR_HID:
	case ES9356_HID_BYTE2:
	case ES9356_HID_BYTE3:
	case ES9356_HID_BYTE4:
	case SDW_SDCA_CTL(FUNC_NUM_HID, ES9356_SDCA_ENT_HID01, ES9356_SDCA_CTL_HIDTX_CURRENT_OWNER, 0):
	case SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_GE35, ES9356_SDCA_CTL_DETECTED_MODE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_GE35, ES9356_SDCA_CTL_SELECTED_MODE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT_PDE23, ES9356_SDCA_CTL_REQ_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT_PDE23, ES9356_SDCA_CTL_ACTUAL_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_PDE11, ES9356_SDCA_CTL_REQ_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_PDE11, ES9356_SDCA_CTL_ACTUAL_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_PDE47, ES9356_SDCA_CTL_REQ_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_PDE47, ES9356_SDCA_CTL_ACTUAL_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_PDE34, ES9356_SDCA_CTL_REQ_POWER_STATE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_PDE34, ES9356_SDCA_CTL_ACTUAL_POWER_STATE, 0):
	case ES9356_FLAGS_HP:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config es9356_sdca_regmap = {
	.reg_bits = 32,
	.val_bits = 16,
	.volatile_reg = es9356_sdca_volatile_register,
	.max_register = 0x45ffffff,
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static void es9356_register_init(struct es9356_sdw_priv *es9356)
{
	regmap_write(es9356->regmap, ES9356_STATE, 0x02);
	regmap_write(es9356->regmap, ES9356_ENDPOINT_MODE, 0x24);
	regmap_write(es9356->regmap, ES9356_PRE_DIV_CTL, 0x00);
	regmap_write(es9356->regmap, ES9356_ADC_OSR, 0x18);
	regmap_write(es9356->regmap, ES9356_ADC_OSRGAIN, 0x13);
	regmap_write(es9356->regmap, ES9356_DAC_OSR, 0x16);
	regmap_write(es9356->regmap, ES9356_CLK_CTL, 0x0f);
	regmap_write(es9356->regmap, ES9356_CSM_RESET, 0x01);
	regmap_write(es9356->regmap, ES9356_CLK_SEL, 0x30);

	regmap_write(es9356->regmap, ES9356_DETCLK_CTL, 0x51);
	regmap_write(es9356->regmap, ES9356_HP_TYPE, 0x10);
	regmap_write(es9356->regmap, ES9356_MICBIAS_CTL, 0x10);
	regmap_write(es9356->regmap, ES9356_HPDETECT_CTL, 0x07);
	regmap_write(es9356->regmap, ES9356_ADC_ANA, 0x30);
	regmap_write(es9356->regmap, ES9356_PGA_CTL, 0xa8);
	regmap_write(es9356->regmap, ES9356_ADC_INT, 0xaa);
	regmap_write(es9356->regmap, ES9356_ADC_LP, 0x19);
	regmap_write(es9356->regmap, ES9356_VMID1SEL, 0xbc);
	regmap_write(es9356->regmap, ES9356_VMID_TIME, 0x0b);
	regmap_write(es9356->regmap, ES9356_STATE_TIME, 0xbb);
	regmap_write(es9356->regmap, ES9356_HP_SPK_TIME, 0x77);
	regmap_write(es9356->regmap, ES9356_HP_DETECTTIME, 0xa4);
	regmap_write(es9356->regmap, ES9356_MICBIAS_SEL, 0x15);
	regmap_write(es9356->regmap, ES9356_KEY_PRESS_TIME, 0xff);
	regmap_write(es9356->regmap, ES9356_KEY_RELEASE_TIME, 0xff);
	regmap_write(es9356->regmap, ES9356_KEY_HOLD_TIME, 0x0f);
	regmap_write(es9356->regmap, ES9356_BTSEL_REF, 0x00);
	regmap_write(es9356->regmap, ES9356_KEYD_DETECT, 0x18);
	regmap_write(es9356->regmap, ES9356_MICBIAS_RES, 0x03);
	regmap_write(es9356->regmap, ES9356_BUTTON_CHARGE, 0x00);
	regmap_write(es9356->regmap, ES9356_CALIBRATION_TIME, 0x13);
	regmap_write(es9356->regmap, ES9356_CALIBRATION_SETTING, 0xf4);

	regmap_write(es9356->regmap, ES9356_SPK_VOLUME, 0x33);
	regmap_write(es9356->regmap, ES9356_DAC_VROI, 0x01);
	regmap_write(es9356->regmap, ES9356_DAC_LP, 0x00);
	regmap_write(es9356->regmap, ES9356_HP_IBIAS, 0x04);
	regmap_write(es9356->regmap, ES9356_HP_LP, 0x03);
	regmap_write(es9356->regmap, ES9356_SPKLDO_CTL, 0x65);
	regmap_write(es9356->regmap, ES9356_SPKBIAS_COMP, 0x09);
	regmap_write(es9356->regmap, ES9356_VMID1STL, 0x00);
	regmap_write(es9356->regmap, ES9356_VMID2STL, 0x00);
	regmap_write(es9356->regmap, ES9356_VSEL, 0xfc);

	regmap_write(es9356->regmap, ES9356_IBIASGEN, 0x10);
	regmap_write(es9356->regmap, ES9356_ADC_AMIC_CTL, 0x0d);
	regmap_write(es9356->regmap, ES9356_STATE, 0x0e);
	regmap_write(es9356->regmap, ES9356_CSM_RESET, 0x00);
	regmap_write(es9356->regmap, ES9356_HP_TYPE, 0x08);

	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_FU113, ES9356_SDCA_CTL_FU_VOLUME, CH_L), ES9356_DEFAULT_VOLUME);
	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_FU113, ES9356_SDCA_CTL_FU_VOLUME, CH_R), ES9356_DEFAULT_VOLUME);

	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT_FU21, ES9356_SDCA_CTL_FU_VOLUME, CH_L), ES9356_DEFAULT_VOLUME);
	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT_FU21, ES9356_SDCA_CTL_FU_VOLUME, CH_R), ES9356_DEFAULT_VOLUME);

	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU41, ES9356_SDCA_CTL_FU_VOLUME, CH_L), ES9356_DEFAULT_VOLUME);
	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU41, ES9356_SDCA_CTL_FU_VOLUME, CH_R), ES9356_DEFAULT_VOLUME);

	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU36, ES9356_SDCA_CTL_FU_VOLUME, CH_L), ES9356_DEFAULT_VOLUME);
	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_FU36, ES9356_SDCA_CTL_FU_VOLUME, CH_R), ES9356_DEFAULT_VOLUME);
}

static int es9356_sdca_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct es9356_sdw_priv *es9356 = dev_get_drvdata(&slave->dev);

	if (es9356->hw_init)
		return 0;

	es9356->disable_irq = false;

	regcache_cache_only(es9356->regmap, false);

	if (es9356->first_hw_init) {
		regcache_cache_bypass(es9356->regmap, true);
	} else {
		/* update count of parent 'active' children */
		pm_runtime_set_active(&slave->dev);

		es9356_register_init(es9356);
	}
	pm_runtime_get_noresume(&slave->dev);

	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT_XU12, ES9356_SDCA_CTL_SELECTED_MODE, 0), 0x01);
	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_MIC, ES9356_SDCA_ENT0, ES9356_SDCA_CTL_FUNC_STATUS, 0), 0x40);
	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT_XU22, ES9356_SDCA_CTL_SELECTED_MODE, 0), 0x01);
	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_AMP, ES9356_SDCA_ENT0, ES9356_SDCA_CTL_FUNC_STATUS, 0), 0x40);
	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_XU42, ES9356_SDCA_CTL_SELECTED_MODE, 0), 0x01);
	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT_XU36, ES9356_SDCA_CTL_SELECTED_MODE, 0), 0x01);
	regmap_write(es9356->regmap,
		SDW_SDCA_CTL(FUNC_NUM_UAJ, ES9356_SDCA_ENT0, ES9356_SDCA_CTL_FUNC_STATUS, 0), 0x40);

	if (es9356->first_hw_init) {
		regcache_cache_bypass(es9356->regmap, false);
		regcache_mark_dirty(es9356->regmap);
	} else
		es9356->first_hw_init = true;

	es9356->hw_init = true;

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	return 0;
}

static int es9356_sdw_update_status(struct sdw_slave *slave,
				    enum sdw_slave_status status)
{
	struct es9356_sdw_priv *es9356 = dev_get_drvdata(&slave->dev);

	if (status == SDW_SLAVE_UNATTACHED) {
		es9356->hw_init = false;
		cancel_delayed_work_sync(&es9356->interrupt_handle_work);
		cancel_delayed_work_sync(&es9356->button_detect_work);
		regcache_cache_only(es9356->regmap, true);
	}

	if (status == SDW_SLAVE_ATTACHED) {
		if (es9356->hs_jack)
			sdw_write_no_pm(es9356->slave, SDW_SCP_SDCA_INTMASK1,
			(SDW_SCP_SDCA_INTMASK_SDCA_7 | SDW_SCP_SDCA_INTMASK_SDCA_5 | SDW_SCP_SDCA_INTMASK_SDCA_1));
	}

	if (es9356->hw_init || status != SDW_SLAVE_ATTACHED)
		return 0;

	return es9356_sdca_io_init(&slave->dev, slave);
}

static int es9356_sdw_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval;
	int i, j;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->paging_support = true;

	/*
	 * first we need to allocate memory for set bits in port lists
	 * the port allocation is completely arbitrary:
	 * DP0 is not supported
	 * DP3 and DP4 is sink
	 * DP1 and DP2 is source
	 */
	prop->source_ports = BIT(1) | BIT(2);
	prop->sink_ports = BIT(3) | BIT(4);

	nval = hweight32(prop->source_ports);
	prop->src_dpn_prop = devm_kcalloc(&slave->dev, nval,
					  sizeof(*prop->src_dpn_prop),
					  GFP_KERNEL);
	if (!prop->src_dpn_prop)
		return -ENOMEM;

	i = 0;
	dpn = prop->src_dpn_prop;
	addr = prop->source_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[i].num = bit;
		dpn[i].type = SDW_DPN_FULL;
		dpn[i].simple_ch_prep_sm = true;
		i++;
	}

	/* do this again for sink now */
	nval = hweight32(prop->sink_ports);
	prop->sink_dpn_prop = devm_kcalloc(&slave->dev, nval,
					   sizeof(*prop->sink_dpn_prop),
					   GFP_KERNEL);
	if (!prop->sink_dpn_prop)
		return -ENOMEM;

	j = 0;
	dpn = prop->sink_dpn_prop;
	addr = prop->sink_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[j].num = bit;
		dpn[j].type = SDW_DPN_FULL;
		dpn[j].simple_ch_prep_sm = true;
		j++;
	}

	/* wake-up event */
	prop->wake_capable = 1;

	return 0;
}

static int es9356_sdw_interrupt_callback(struct sdw_slave *slave,
					 struct sdw_slave_intr_status *status)
{
	struct es9356_sdw_priv *es9356 = dev_get_drvdata(&slave->dev);
	unsigned int sdca_cascade, scp_sdca_stat1 = 0;
	int count = 0, retry = 3;
	int ret, stat, reg;

	mutex_lock(&es9356->disable_irq_lock);

	ret = sdw_read_no_pm(es9356->slave, SDW_SCP_SDCA_INT1);
	if (ret < 0)
		goto io_error;
	es9356->sdca_status = ret;

	do {
		reg = sdw_read_no_pm(es9356->slave, SDW_SCP_SDCA_INT1);
		if (reg < 0)
			goto io_error;
		if (reg & SDW_SCP_SDCA_INTMASK_SDCA_1) {
			ret = sdw_update_no_pm(es9356->slave, SDW_SCP_SDCA_INT1,
				SDW_SCP_SDCA_INT_SDCA_1, SDW_SCP_SDCA_INT_SDCA_1);
			if (ret < 0)
				goto io_error;
		}

		if (reg & SDW_SCP_SDCA_INTMASK_SDCA_5) {
			ret = sdw_update_no_pm(es9356->slave, SDW_SCP_SDCA_INT1,
				SDW_SCP_SDCA_INT_SDCA_5, SDW_SCP_SDCA_INT_SDCA_5);
			if (ret < 0)
				goto io_error;
		}

		if (reg & SDW_SCP_SDCA_INTMASK_SDCA_7) {
			ret = sdw_update_no_pm(es9356->slave, SDW_SCP_SDCA_INT1,
				SDW_SCP_SDCA_INT_SDCA_7, SDW_SCP_SDCA_INT_SDCA_7);
			if (ret < 0)
				goto io_error;
		}

		ret = sdw_read_no_pm(es9356->slave, SDW_DP0_INT);
		if (ret < 0)
			goto io_error;
		sdca_cascade = ret & SDW_DP0_SDCA_CASCADE;

		ret = sdw_read_no_pm(es9356->slave, SDW_SCP_SDCA_INT1);
		if (ret < 0)
			goto io_error;
		scp_sdca_stat1 = ret &
			(SDW_SCP_SDCA_INTMASK_SDCA_1 | SDW_SCP_SDCA_INTMASK_SDCA_5 | SDW_SCP_SDCA_INTMASK_SDCA_7);

		stat = scp_sdca_stat1 || sdca_cascade;

		count++;
	} while (stat != 0 && count < retry);

	/* The 280 ms figure was determined through testing */
	if (status->sdca_cascade && !es9356->disable_irq)
		mod_delayed_work(system_power_efficient_wq,
			&es9356->interrupt_handle_work, msecs_to_jiffies(280));

	mutex_unlock(&es9356->disable_irq_lock);
	return 0;

io_error:
	mutex_unlock(&es9356->disable_irq_lock);
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
	return ret;
}

static const struct sdw_slave_ops es9356_sdw_slave_ops = {
	.read_prop = es9356_sdw_read_prop,
	.interrupt_callback = es9356_sdw_interrupt_callback,
	.update_status = es9356_sdw_update_status,
};

static int es9356_sdca_init(struct device *dev, struct regmap *regmap, struct sdw_slave *slave)
{
	struct es9356_sdw_priv *es9356;
	int ret;

	es9356 = devm_kzalloc(dev, sizeof(*es9356), GFP_KERNEL);
	if (!es9356)
		return -ENOMEM;

	dev_set_drvdata(dev, es9356);

	es9356->slave = slave;
	es9356->regmap = regmap;
	mutex_init(&es9356->disable_irq_lock);
	mutex_init(&es9356->pde_lock);

	regcache_cache_only(es9356->regmap, true);

	es9356->hw_init = false;
	es9356->first_hw_init = false;

	INIT_DELAYED_WORK(&es9356->interrupt_handle_work,
			  es9356_interrupt_handler);
	INIT_DELAYED_WORK(&es9356->button_detect_work,
			  es9356_button_detect_handler);

	ret = devm_snd_soc_register_component(dev,
					       &snd_soc_es9356_sdw_component,
					       es9356_sdw_dai,
					       ARRAY_SIZE(es9356_sdw_dai));
	if (ret) {
		dev_err_probe(dev, ret, "Failed to register component\n");
		return ret;
	}
	/* set autosuspend parameters */
	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);
	/* make sure the device does not suspend immediately */
	pm_runtime_mark_last_busy(dev);
	pm_runtime_enable(dev);

	return 0;
}

static int es9356_sdw_probe(struct sdw_slave *slave,
				const struct sdw_device_id *id)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_sdw_mbq_cfg(&slave->dev, slave, &es9356_sdca_regmap, &es9356_mbq_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return es9356_sdca_init(&slave->dev, regmap, slave);
}

static void es9356_sdw_remove(struct sdw_slave *slave)
{
	struct es9356_sdw_priv *es9356 = dev_get_drvdata(&slave->dev);

	if (es9356->hw_init) {
		cancel_delayed_work_sync(&es9356->interrupt_handle_work);
		cancel_delayed_work_sync(&es9356->button_detect_work);
	}

	if (es9356->first_hw_init)
		pm_runtime_disable(&slave->dev);

	mutex_destroy(&es9356->disable_irq_lock);
	mutex_destroy(&es9356->pde_lock);
}

static const struct sdw_device_id es9356_sdw_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x04b3, 0x9356, 0x02, 0, 0),
	SDW_SLAVE_ENTRY_EXT(0x04b3, 0x9356, 0x03, 0, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, es9356_sdw_id);

static int es9356_sdca_dev_suspend(struct device *dev)
{
	struct es9356_sdw_priv *es9356 = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&es9356->interrupt_handle_work);
	cancel_delayed_work_sync(&es9356->button_detect_work);

	regcache_cache_only(es9356->regmap, true);

	return 0;
}

static int es9356_sdca_dev_system_suspend(struct device *dev)
{
	struct es9356_sdw_priv *es9356 = dev_get_drvdata(dev);

	mutex_lock(&es9356->disable_irq_lock);
	es9356->disable_irq = true;
	mutex_unlock(&es9356->disable_irq_lock);

	return es9356_sdca_dev_suspend(dev);
}

#define es9356_PROBE_TIMEOUT 2000

static int es9356_sdca_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct es9356_sdw_priv *es9356 = dev_get_drvdata(dev);
	int ret;

	if (!slave->unattach_request)
		es9356->disable_irq = false;

	ret = sdw_slave_wait_for_init(slave, es9356_PROBE_TIMEOUT);
	if (ret) {
		sdw_show_ping_status(slave->bus, true);
		return ret;
	}

	regcache_cache_only(es9356->regmap, false);
	regcache_sync(es9356->regmap);
	return 0;
}

static const struct dev_pm_ops es9356_sdca_pm = {
	SYSTEM_SLEEP_PM_OPS(es9356_sdca_dev_system_suspend, es9356_sdca_dev_resume)
	RUNTIME_PM_OPS(es9356_sdca_dev_suspend, es9356_sdca_dev_resume, NULL)
};

static struct sdw_driver es9356_sdw_driver = {
	.driver = {
		.name = "es9356",
		.pm = pm_ptr(&es9356_sdca_pm),
	},
	.probe = es9356_sdw_probe,
	.remove = es9356_sdw_remove,
	.ops = &es9356_sdw_slave_ops,
	.id_table = es9356_sdw_id,
};
module_sdw_driver(es9356_sdw_driver);

MODULE_IMPORT_NS("SND_SOC_SDCA");
MODULE_DESCRIPTION("ASoC ES9356 SDCA SDW codec driver");
MODULE_AUTHOR("Michael Zhang <zhangyi@everest-semi.com>");
MODULE_LICENSE("GPL");
