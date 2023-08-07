// SPDX-License-Identifier: GPL-2.0
//
// ALSA SoC Texas Instruments TAS2781 Audio Smart Amplifier
//
// Copyright (C) 2022 - 2023 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS2781 driver implements a flexible and configurable
// algo coefficient setting for one, two, or even multiple
// TAS2781 chips.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>
// Author: Kevin Lu <kevin-lu@ti.com>
//

#include <linux/crc8.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tas2781.h>
#include <sound/tlv.h>
#include <sound/tas2781-tlv.h>

static const struct i2c_device_id tasdevice_id[] = {
	{ "tas2781", TAS2781 },
	{}
};
MODULE_DEVICE_TABLE(i2c, tasdevice_id);

#ifdef CONFIG_OF
static const struct of_device_id tasdevice_of_match[] = {
	{ .compatible = "ti,tas2781" },
	{},
};
MODULE_DEVICE_TABLE(of, tasdevice_of_match);
#endif

/**
 * tas2781_digital_getvol - get the volum control
 * @kcontrol: control pointer
 * @ucontrol: User data
 * Customer Kcontrol for tas2781 is primarily for regmap booking, paging
 * depends on internal regmap mechanism.
 * tas2781 contains book and page two-level register map, especially
 * book switching will set the register BXXP00R7F, after switching to the
 * correct book, then leverage the mechanism for paging to access the
 * register.
 */
static int tas2781_digital_getvol(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	return tasdevice_digital_getvol(tas_priv, ucontrol, mc);
}

static int tas2781_digital_putvol(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	return tasdevice_digital_putvol(tas_priv, ucontrol, mc);
}

static int tas2781_amp_getvol(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	return tasdevice_amp_getvol(tas_priv, ucontrol, mc);
}

static int tas2781_amp_putvol(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv =
		snd_soc_component_get_drvdata(codec);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	return tasdevice_amp_putvol(tas_priv, ucontrol, mc);
}

static int tas2781_force_fwload_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = (int)tas_priv->force_fwload_status;
	dev_dbg(tas_priv->dev, "%s : Force FWload %s\n", __func__,
			tas_priv->force_fwload_status ? "ON" : "OFF");

	return 0;
}

static int tas2781_force_fwload_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv =
		snd_soc_component_get_drvdata(component);
	bool change, val = (bool)ucontrol->value.integer.value[0];

	if (tas_priv->force_fwload_status == val)
		change = false;
	else {
		change = true;
		tas_priv->force_fwload_status = val;
	}
	dev_dbg(tas_priv->dev, "%s : Force FWload %s\n", __func__,
		tas_priv->force_fwload_status ? "ON" : "OFF");

	return change;
}

static const struct snd_kcontrol_new tas2781_snd_controls[] = {
	SOC_SINGLE_RANGE_EXT_TLV("Speaker Analog Gain", TAS2781_AMP_LEVEL,
		1, 0, 20, 0, tas2781_amp_getvol,
		tas2781_amp_putvol, amp_vol_tlv),
	SOC_SINGLE_RANGE_EXT_TLV("Speaker Digital Gain", TAS2781_DVC_LVL,
		0, 0, 200, 1, tas2781_digital_getvol,
		tas2781_digital_putvol, dvc_tlv),
	SOC_SINGLE_BOOL_EXT("Speaker Force Firmware Load", 0,
		tas2781_force_fwload_get, tas2781_force_fwload_put),
};

static int tasdevice_set_profile_id(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);
	int ret = 0;

	if (tas_priv->rcabin.profile_cfg_id !=
		ucontrol->value.integer.value[0]) {
		tas_priv->rcabin.profile_cfg_id =
			ucontrol->value.integer.value[0];
		ret = 1;
	}

	return ret;
}

static int tasdevice_info_programs(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);
	struct tasdevice_fw *tas_fw = tas_priv->fmw;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = (int)tas_fw->nr_programs;

	return 0;
}

static int tasdevice_info_configurations(
	struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *codec =
		snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);
	struct tasdevice_fw *tas_fw = tas_priv->fmw;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = (int)tas_fw->nr_configurations - 1;

	return 0;
}

static int tasdevice_info_profile(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = tas_priv->rcabin.ncfgs - 1;

	return 0;
}

static int tasdevice_get_profile_id(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = tas_priv->rcabin.profile_cfg_id;

	return 0;
}

static int tasdevice_create_control(struct tasdevice_priv *tas_priv)
{
	struct snd_kcontrol_new *prof_ctrls;
	int nr_controls = 1;
	int mix_index = 0;
	int ret;
	char *name;

	prof_ctrls = devm_kcalloc(tas_priv->dev, nr_controls,
		sizeof(prof_ctrls[0]), GFP_KERNEL);
	if (!prof_ctrls) {
		ret = -ENOMEM;
		goto out;
	}

	/* Create a mixer item for selecting the active profile */
	name = devm_kzalloc(tas_priv->dev, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		GFP_KERNEL);
	if (!name) {
		ret = -ENOMEM;
		goto out;
	}
	scnprintf(name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "Speaker Profile Id");
	prof_ctrls[mix_index].name = name;
	prof_ctrls[mix_index].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	prof_ctrls[mix_index].info = tasdevice_info_profile;
	prof_ctrls[mix_index].get = tasdevice_get_profile_id;
	prof_ctrls[mix_index].put = tasdevice_set_profile_id;
	mix_index++;

	ret = snd_soc_add_component_controls(tas_priv->codec,
		prof_ctrls, nr_controls < mix_index ? nr_controls : mix_index);

out:
	return ret;
}

static int tasdevice_program_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = tas_priv->cur_prog;

	return 0;
}

static int tasdevice_program_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);
	unsigned int nr_program = ucontrol->value.integer.value[0];
	int ret = 0;

	if (tas_priv->cur_prog != nr_program) {
		tas_priv->cur_prog = nr_program;
		ret = 1;
	}

	return ret;
}

static int tasdevice_configuration_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);

	ucontrol->value.integer.value[0] = tas_priv->cur_conf;

	return 0;
}

static int tasdevice_configuration_put(
	struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *codec = snd_soc_kcontrol_component(kcontrol);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);
	unsigned int nr_configuration = ucontrol->value.integer.value[0];
	int ret = 0;

	if (tas_priv->cur_conf != nr_configuration) {
		tas_priv->cur_conf = nr_configuration;
		ret = 1;
	}

	return ret;
}

static int tasdevice_dsp_create_ctrls(
	struct tasdevice_priv *tas_priv)
{
	struct snd_kcontrol_new *dsp_ctrls;
	char *prog_name, *conf_name;
	int nr_controls = 2;
	int mix_index = 0;
	int ret;

	/* Alloc kcontrol via devm_kzalloc, which don't manually
	 * free the kcontrol
	 */
	dsp_ctrls = devm_kcalloc(tas_priv->dev, nr_controls,
		sizeof(dsp_ctrls[0]), GFP_KERNEL);
	if (!dsp_ctrls) {
		ret = -ENOMEM;
		goto out;
	}

	/* Create a mixer item for selecting the active profile */
	prog_name = devm_kzalloc(tas_priv->dev,
		SNDRV_CTL_ELEM_ID_NAME_MAXLEN, GFP_KERNEL);
	conf_name = devm_kzalloc(tas_priv->dev, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		GFP_KERNEL);
	if (!prog_name || !conf_name) {
		ret = -ENOMEM;
		goto out;
	}

	scnprintf(prog_name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		"Speaker Program Id");
	dsp_ctrls[mix_index].name = prog_name;
	dsp_ctrls[mix_index].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	dsp_ctrls[mix_index].info = tasdevice_info_programs;
	dsp_ctrls[mix_index].get = tasdevice_program_get;
	dsp_ctrls[mix_index].put = tasdevice_program_put;
	mix_index++;

	scnprintf(conf_name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		"Speaker Config Id");
	dsp_ctrls[mix_index].name = conf_name;
	dsp_ctrls[mix_index].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	dsp_ctrls[mix_index].info = tasdevice_info_configurations;
	dsp_ctrls[mix_index].get = tasdevice_configuration_get;
	dsp_ctrls[mix_index].put = tasdevice_configuration_put;
	mix_index++;

	ret = snd_soc_add_component_controls(tas_priv->codec, dsp_ctrls,
		nr_controls < mix_index ? nr_controls : mix_index);

out:
	return ret;
}

static void tasdevice_fw_ready(const struct firmware *fmw,
	void *context)
{
	struct tasdevice_priv *tas_priv = context;
	int ret = 0;
	int i;

	mutex_lock(&tas_priv->codec_lock);

	ret = tasdevice_rca_parser(tas_priv, fmw);
	if (ret)
		goto out;
	tasdevice_create_control(tas_priv);

	tasdevice_dsp_remove(tas_priv);
	tasdevice_calbin_remove(tas_priv);
	tas_priv->fw_state = TASDEVICE_DSP_FW_PENDING;
	scnprintf(tas_priv->coef_binaryname, 64, "%s_coef.bin",
		tas_priv->dev_name);
	ret = tasdevice_dsp_parser(tas_priv);
	if (ret) {
		dev_err(tas_priv->dev, "dspfw load %s error\n",
			tas_priv->coef_binaryname);
		tas_priv->fw_state = TASDEVICE_DSP_FW_FAIL;
		goto out;
	}
	tasdevice_dsp_create_ctrls(tas_priv);

	tas_priv->fw_state = TASDEVICE_DSP_FW_ALL_OK;

	/* If calibrated data occurs error, dsp will still works with default
	 * calibrated data inside algo.
	 */
	for (i = 0; i < tas_priv->ndev; i++) {
		scnprintf(tas_priv->cal_binaryname[i], 64, "%s_cal_0x%02x.bin",
			tas_priv->dev_name, tas_priv->tasdevice[i].dev_addr);
		ret = tas2781_load_calibration(tas_priv,
			tas_priv->cal_binaryname[i], i);
		if (ret != 0)
			dev_err(tas_priv->dev,
				"%s: load %s error, default will effect\n",
				__func__, tas_priv->cal_binaryname[i]);
	}

	tasdevice_prmg_calibdata_load(tas_priv, 0);
	tas_priv->cur_prog = 0;
out:
	if (tas_priv->fw_state == TASDEVICE_DSP_FW_FAIL) {
		/*If DSP FW fail, kcontrol won't be created */
		tasdevice_config_info_remove(tas_priv);
		tasdevice_dsp_remove(tas_priv);
	}
	mutex_unlock(&tas_priv->codec_lock);
	if (fmw)
		release_firmware(fmw);
}

static int tasdevice_dapm_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *codec = snd_soc_dapm_to_component(w->dapm);
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);
	int state = 0;

	/* Codec Lock Hold */
	mutex_lock(&tas_priv->codec_lock);
	if (event == SND_SOC_DAPM_PRE_PMD)
		state = 1;
	tasdevice_tuning_switch(tas_priv, state);
	/* Codec Lock Release*/
	mutex_unlock(&tas_priv->codec_lock);

	return 0;
}

static const struct snd_soc_dapm_widget tasdevice_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI", "ASI Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT_E("ASI OUT", "ASI Capture", 0, SND_SOC_NOPM,
		0, 0, tasdevice_dapm_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SPK("SPK", tasdevice_dapm_event),
	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_INPUT("DMIC")
};

static const struct snd_soc_dapm_route tasdevice_audio_map[] = {
	{"SPK", NULL, "ASI"},
	{"OUT", NULL, "SPK"},
	{"ASI OUT", NULL, "DMIC"}
};

static int tasdevice_startup(struct snd_pcm_substream *substream,
						struct snd_soc_dai *dai)
{
	struct snd_soc_component *codec = dai->component;
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);
	int ret = 0;

	if (tas_priv->fw_state != TASDEVICE_DSP_FW_ALL_OK) {
		dev_err(tas_priv->dev, "DSP bin file not loaded\n");
		ret = -EINVAL;
	}

	return ret;
}

static int tasdevice_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct tasdevice_priv *tas_priv = snd_soc_dai_get_drvdata(dai);
	unsigned int slot_width;
	unsigned int fsrate;
	int bclk_rate;
	int rc = 0;

	fsrate = params_rate(params);
	switch (fsrate) {
	case 48000:
	case 44100:
		break;
	default:
		dev_err(tas_priv->dev, "%s: incorrect sample rate = %u\n",
			__func__, fsrate);
		rc = -EINVAL;
		goto out;
	}

	slot_width = params_width(params);
	switch (slot_width) {
	case 16:
	case 20:
	case 24:
	case 32:
		break;
	default:
		dev_err(tas_priv->dev, "%s: incorrect slot width = %u\n",
			__func__, slot_width);
		rc = -EINVAL;
		goto out;
	}

	bclk_rate = snd_soc_params_to_bclk(params);
	if (bclk_rate < 0) {
		dev_err(tas_priv->dev, "%s: incorrect bclk rate = %d\n",
			__func__, bclk_rate);
		rc = bclk_rate;
		goto out;
	}

out:
	return rc;
}

static int tasdevice_set_dai_sysclk(struct snd_soc_dai *codec_dai,
	int clk_id, unsigned int freq, int dir)
{
	struct tasdevice_priv *tas_priv = snd_soc_dai_get_drvdata(codec_dai);

	tas_priv->sysclk = freq;

	return 0;
}

static const struct snd_soc_dai_ops tasdevice_dai_ops = {
	.startup = tasdevice_startup,
	.hw_params = tasdevice_hw_params,
	.set_sysclk = tasdevice_set_dai_sysclk,
};

static struct snd_soc_dai_driver tasdevice_dai_driver[] = {
	{
		.name = "tas2781_codec",
		.id = 0,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates	 = TASDEVICE_RATES,
			.formats	= TASDEVICE_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates	 = TASDEVICE_RATES,
			.formats	= TASDEVICE_FORMATS,
		},
		.ops = &tasdevice_dai_ops,
		.symmetric_rate = 1,
	},
};

static int tasdevice_codec_probe(struct snd_soc_component *codec)
{
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);

	return tascodec_init(tas_priv, codec, tasdevice_fw_ready);
}

static void tasdevice_deinit(void *context)
{
	struct tasdevice_priv *tas_priv = (struct tasdevice_priv *) context;

	tasdevice_config_info_remove(tas_priv);
	tasdevice_dsp_remove(tas_priv);
	tasdevice_calbin_remove(tas_priv);
	tas_priv->fw_state = TASDEVICE_DSP_FW_PENDING;
}

static void tasdevice_codec_remove(
	struct snd_soc_component *codec)
{
	struct tasdevice_priv *tas_priv = snd_soc_component_get_drvdata(codec);

	tasdevice_deinit(tas_priv);
}

static const struct snd_soc_component_driver
	soc_codec_driver_tasdevice = {
	.probe			= tasdevice_codec_probe,
	.remove			= tasdevice_codec_remove,
	.controls		= tas2781_snd_controls,
	.num_controls		= ARRAY_SIZE(tas2781_snd_controls),
	.dapm_widgets		= tasdevice_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tasdevice_dapm_widgets),
	.dapm_routes		= tasdevice_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(tasdevice_audio_map),
	.idle_bias_on		= 1,
	.endianness		= 1,
};

static void tasdevice_parse_dt(struct tasdevice_priv *tas_priv)
{
	struct i2c_client *client = (struct i2c_client *)tas_priv->client;
	unsigned int dev_addrs[TASDEVICE_MAX_CHANNELS];
	int rc, i, ndev = 0;

	if (tas_priv->isacpi) {
		ndev = device_property_read_u32_array(&client->dev,
			"ti,audio-slots", NULL, 0);
		if (ndev <= 0) {
			ndev = 1;
			dev_addrs[0] = client->addr;
		} else {
			ndev = (ndev < ARRAY_SIZE(dev_addrs))
				? ndev : ARRAY_SIZE(dev_addrs);
			ndev = device_property_read_u32_array(&client->dev,
				"ti,audio-slots", dev_addrs, ndev);
		}

		tas_priv->irq_info.irq_gpio =
			acpi_dev_gpio_irq_get(ACPI_COMPANION(&client->dev), 0);
	} else {
		struct device_node *np = tas_priv->dev->of_node;
#ifdef CONFIG_OF
		const __be32 *reg, *reg_end;
		int len, sw, aw;

		aw = of_n_addr_cells(np);
		sw = of_n_size_cells(np);
		if (sw == 0) {
			reg = (const __be32 *)of_get_property(np,
				"reg", &len);
			reg_end = reg + len/sizeof(*reg);
			ndev = 0;
			do {
				dev_addrs[ndev] = of_read_number(reg, aw);
				reg += aw;
				ndev++;
			} while (reg < reg_end);
		} else {
			ndev = 1;
			dev_addrs[0] = client->addr;
		}
#else
		ndev = 1;
		dev_addrs[0] = client->addr;
#endif
		tas_priv->irq_info.irq_gpio = of_irq_get(np, 0);
	}
	tas_priv->ndev = ndev;
	for (i = 0; i < ndev; i++)
		tas_priv->tasdevice[i].dev_addr = dev_addrs[i];

	tas_priv->reset = devm_gpiod_get_optional(&client->dev,
			"reset-gpios", GPIOD_OUT_HIGH);
	if (IS_ERR(tas_priv->reset))
		dev_err(tas_priv->dev, "%s Can't get reset GPIO\n",
			__func__);

	strcpy(tas_priv->dev_name, tasdevice_id[tas_priv->chip_id].name);

	if (gpio_is_valid(tas_priv->irq_info.irq_gpio)) {
		rc = gpio_request(tas_priv->irq_info.irq_gpio,
				"AUDEV-IRQ");
		if (!rc) {
			gpio_direction_input(
				tas_priv->irq_info.irq_gpio);

			tas_priv->irq_info.irq =
				gpio_to_irq(tas_priv->irq_info.irq_gpio);
		} else
			dev_err(tas_priv->dev, "%s: GPIO %d request error\n",
				__func__, tas_priv->irq_info.irq_gpio);
	} else
		dev_err(tas_priv->dev,
			"Looking up irq-gpio property failed %d\n",
			tas_priv->irq_info.irq_gpio);
}

static int tasdevice_i2c_probe(struct i2c_client *i2c)
{
	const struct i2c_device_id *id = i2c_match_id(tasdevice_id, i2c);
	const struct acpi_device_id *acpi_id;
	struct tasdevice_priv *tas_priv;
	int ret;

	tas_priv = tasdevice_kzalloc(i2c);
	if (!tas_priv)
		return -ENOMEM;

	if (ACPI_HANDLE(&i2c->dev)) {
		acpi_id = acpi_match_device(i2c->dev.driver->acpi_match_table,
				&i2c->dev);
		if (!acpi_id) {
			dev_err(&i2c->dev, "No driver data\n");
			ret = -EINVAL;
			goto err;
		}
		tas_priv->chip_id = acpi_id->driver_data;
		tas_priv->isacpi = true;
	} else {
		tas_priv->chip_id = id ? id->driver_data : 0;
		tas_priv->isacpi = false;
	}

	tasdevice_parse_dt(tas_priv);

	ret = tasdevice_init(tas_priv);
	if (ret)
		goto err;

	ret = devm_snd_soc_register_component(tas_priv->dev,
		&soc_codec_driver_tasdevice,
		tasdevice_dai_driver, ARRAY_SIZE(tasdevice_dai_driver));
	if (ret) {
		dev_err(tas_priv->dev, "%s: codec register error:0x%08x\n",
			__func__, ret);
		goto err;
	}
err:
	if (ret < 0)
		tasdevice_remove(tas_priv);
	return ret;
}

static void tasdevice_i2c_remove(struct i2c_client *client)
{
	struct tasdevice_priv *tas_priv = i2c_get_clientdata(client);

	tasdevice_remove(tas_priv);
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id tasdevice_acpi_match[] = {
	{ "TAS2781", TAS2781 },
	{},
};

MODULE_DEVICE_TABLE(acpi, tasdevice_acpi_match);
#endif

static struct i2c_driver tasdevice_i2c_driver = {
	.driver = {
		.name = "tas2781-codec",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tasdevice_of_match),
#ifdef CONFIG_ACPI
		.acpi_match_table = ACPI_PTR(tasdevice_acpi_match),
#endif
	},
	.probe	= tasdevice_i2c_probe,
	.remove = tasdevice_i2c_remove,
	.id_table = tasdevice_id,
};

module_i2c_driver(tasdevice_i2c_driver);

MODULE_AUTHOR("Shenghao Ding <shenghao-ding@ti.com>");
MODULE_AUTHOR("Kevin Lu <kevin-lu@ti.com>");
MODULE_DESCRIPTION("ASoC TAS2781 Driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(SND_SOC_TAS2781_FMWLIB);
