// SPDX-License-Identifier: GPL-2.0
//
// TSE-850 audio - ASoC driver for the Axentia TSE-850 with a PCM5142 codec
//
// Copyright (C) 2016 Axentia Technologies AB
//
// Author: Peter Rosin <peda@axentia.se>
//
//               loop1 relays
//   IN1 +---o  +------------+  o---+ OUT1
//            \                /
//             +              +
//             |   /          |
//             +--o  +--.     |
//             |  add   |     |
//             |        V     |
//             |      .---.   |
//   DAC +----------->|Sum|---+
//             |      '---'   |
//             |              |
//             +              +
//
//   IN2 +---o--+------------+--o---+ OUT2
//               loop2 relays
//
// The 'loop1' gpio pin controls two relays, which are either in loop
// position, meaning that input and output are directly connected, or
// they are in mixer position, meaning that the signal is passed through
// the 'Sum' mixer. Similarly for 'loop2'.
//
// In the above, the 'loop1' relays are inactive, thus feeding IN1 to the
// mixer (if 'add' is active) and feeding the mixer output to OUT1. The
// 'loop2' relays are active, short-cutting the TSE-850 from channel 2.
// IN1, IN2, OUT1 and OUT2 are TSE-850 connectors and DAC is the PCB name
// of the (filtered) output from the PCM5142 codec.

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

struct tse850_priv {
	struct gpio_desc *add;
	struct gpio_desc *loop1;
	struct gpio_desc *loop2;

	struct regulator *ana;

	int add_cache;
	int loop1_cache;
	int loop2_cache;
};

static int tse850_get_mux1(struct snd_kcontrol *kctrl,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kctrl);
	struct snd_soc_card *card = dapm->card;
	struct tse850_priv *tse850 = snd_soc_card_get_drvdata(card);

	ucontrol->value.enumerated.item[0] = tse850->loop1_cache;

	return 0;
}

static int tse850_put_mux1(struct snd_kcontrol *kctrl,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kctrl);
	struct snd_soc_card *card = dapm->card;
	struct tse850_priv *tse850 = snd_soc_card_get_drvdata(card);
	struct soc_enum *e = (struct soc_enum *)kctrl->private_value;
	unsigned int val = ucontrol->value.enumerated.item[0];

	if (val >= e->items)
		return -EINVAL;

	gpiod_set_value_cansleep(tse850->loop1, val);
	tse850->loop1_cache = val;

	return snd_soc_dapm_put_enum_double(kctrl, ucontrol);
}

static int tse850_get_mux2(struct snd_kcontrol *kctrl,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kctrl);
	struct snd_soc_card *card = dapm->card;
	struct tse850_priv *tse850 = snd_soc_card_get_drvdata(card);

	ucontrol->value.enumerated.item[0] = tse850->loop2_cache;

	return 0;
}

static int tse850_put_mux2(struct snd_kcontrol *kctrl,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kctrl);
	struct snd_soc_card *card = dapm->card;
	struct tse850_priv *tse850 = snd_soc_card_get_drvdata(card);
	struct soc_enum *e = (struct soc_enum *)kctrl->private_value;
	unsigned int val = ucontrol->value.enumerated.item[0];

	if (val >= e->items)
		return -EINVAL;

	gpiod_set_value_cansleep(tse850->loop2, val);
	tse850->loop2_cache = val;

	return snd_soc_dapm_put_enum_double(kctrl, ucontrol);
}

static int tse850_get_mix(struct snd_kcontrol *kctrl,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kctrl);
	struct snd_soc_card *card = dapm->card;
	struct tse850_priv *tse850 = snd_soc_card_get_drvdata(card);

	ucontrol->value.enumerated.item[0] = tse850->add_cache;

	return 0;
}

static int tse850_put_mix(struct snd_kcontrol *kctrl,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kctrl);
	struct snd_soc_card *card = dapm->card;
	struct tse850_priv *tse850 = snd_soc_card_get_drvdata(card);
	int connect = !!ucontrol->value.integer.value[0];

	if (tse850->add_cache == connect)
		return 0;

	/*
	 * Hmmm, this gpiod_set_value_cansleep call should probably happen
	 * inside snd_soc_dapm_mixer_update_power in the loop.
	 */
	gpiod_set_value_cansleep(tse850->add, connect);
	tse850->add_cache = connect;

	snd_soc_dapm_mixer_update_power(dapm, kctrl, connect, NULL);
	return 1;
}

static int tse850_get_ana(struct snd_kcontrol *kctrl,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kctrl);
	struct snd_soc_card *card = dapm->card;
	struct tse850_priv *tse850 = snd_soc_card_get_drvdata(card);
	int ret;

	ret = regulator_get_voltage(tse850->ana);
	if (ret < 0)
		return ret;

	/*
	 * Map regulator output values like so:
	 *      -11.5V to "Low" (enum 0)
	 * 11.5V-12.5V to "12V" (enum 1)
	 * 12.5V-13.5V to "13V" (enum 2)
	 *     ...
	 * 18.5V-19.5V to "19V" (enum 8)
	 * 19.5V-      to "20V" (enum 9)
	 */
	if (ret < 11000000)
		ret = 11000000;
	else if (ret > 20000000)
		ret = 20000000;
	ret -= 11000000;
	ret = (ret + 500000) / 1000000;

	ucontrol->value.enumerated.item[0] = ret;

	return 0;
}

static int tse850_put_ana(struct snd_kcontrol *kctrl,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kctrl);
	struct snd_soc_card *card = dapm->card;
	struct tse850_priv *tse850 = snd_soc_card_get_drvdata(card);
	struct soc_enum *e = (struct soc_enum *)kctrl->private_value;
	unsigned int uV = ucontrol->value.enumerated.item[0];
	int ret;

	if (uV >= e->items)
		return -EINVAL;

	/*
	 * Map enum zero (Low) to 2 volts on the regulator, do this since
	 * the ana regulator is supplied by the system 12V voltage and
	 * requesting anything below the system voltage causes the system
	 * voltage to be passed through the regulator. Also, the ana
	 * regulator induces noise when requesting voltages near the
	 * system voltage. So, by mapping Low to 2V, that noise is
	 * eliminated when all that is needed is 12V (the system voltage).
	 */
	if (uV)
		uV = 11000000 + (1000000 * uV);
	else
		uV = 2000000;

	ret = regulator_set_voltage(tse850->ana, uV, uV);
	if (ret < 0)
		return ret;

	return snd_soc_dapm_put_enum_double(kctrl, ucontrol);
}

static const char * const mux_text[] = { "Mixer", "Loop" };

static const struct soc_enum mux_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(mux_text), mux_text);

static const struct snd_kcontrol_new mux1 =
	SOC_DAPM_ENUM_EXT("MUX1", mux_enum, tse850_get_mux1, tse850_put_mux1);

static const struct snd_kcontrol_new mux2 =
	SOC_DAPM_ENUM_EXT("MUX2", mux_enum, tse850_get_mux2, tse850_put_mux2);

#define TSE850_DAPM_SINGLE_EXT(xname, reg, shift, max, invert, xget, xput) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = snd_soc_info_volsw, \
	.get = xget, \
	.put = xput, \
	.private_value = SOC_SINGLE_VALUE(reg, shift, max, invert, 0) }

static const struct snd_kcontrol_new mix[] = {
	TSE850_DAPM_SINGLE_EXT("IN Switch", SND_SOC_NOPM, 0, 1, 0,
			       tse850_get_mix, tse850_put_mix),
};

static const char * const ana_text[] = {
	"Low", "12V", "13V", "14V", "15V", "16V", "17V", "18V", "19V", "20V"
};

static const struct soc_enum ana_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(ana_text), ana_text);

static const struct snd_kcontrol_new out =
	SOC_DAPM_ENUM_EXT("ANA", ana_enum, tse850_get_ana, tse850_put_ana);

static const struct snd_soc_dapm_widget tse850_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("OUT1", NULL),
	SND_SOC_DAPM_LINE("OUT2", NULL),
	SND_SOC_DAPM_LINE("IN1", NULL),
	SND_SOC_DAPM_LINE("IN2", NULL),
	SND_SOC_DAPM_INPUT("DAC"),
	SND_SOC_DAPM_AIF_IN("AIFINL", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIFINR", "Playback", 1, SND_SOC_NOPM, 0, 0),
	SOC_MIXER_ARRAY("MIX", SND_SOC_NOPM, 0, 0, mix),
	SND_SOC_DAPM_MUX("MUX1", SND_SOC_NOPM, 0, 0, &mux1),
	SND_SOC_DAPM_MUX("MUX2", SND_SOC_NOPM, 0, 0, &mux2),
	SND_SOC_DAPM_OUT_DRV("OUT", SND_SOC_NOPM, 0, 0, &out, 1),
};

/*
 * These connections are not entirely correct, since both IN1 and IN2
 * are always fed to MIX (if the "IN switch" is set so), i.e. without
 * regard to the loop1 and loop2 relays that according to this only
 * control MUX1 and MUX2 but in fact also control how the input signals
 * are routed.
 * But, 1) I don't know how to do it right, and 2) it doesn't seem to
 * matter in practice since nothing is powered in those sections anyway.
 */
static const struct snd_soc_dapm_route tse850_intercon[] = {
	{ "OUT1", NULL, "MUX1" },
	{ "OUT2", NULL, "MUX2" },

	{ "MUX1", "Loop",  "IN1" },
	{ "MUX1", "Mixer", "OUT" },

	{ "MUX2", "Loop",  "IN2" },
	{ "MUX2", "Mixer", "OUT" },

	{ "OUT", NULL, "MIX" },

	{ "MIX", NULL, "DAC" },
	{ "MIX", "IN Switch", "IN1" },
	{ "MIX", "IN Switch", "IN2" },

	/* connect board input to the codec left channel output pin */
	{ "DAC", NULL, "OUTL" },
};

SND_SOC_DAILINK_DEFS(pcm,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "pcm512x-hifi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link tse850_dailink = {
	.name = "TSE-850",
	.stream_name = "TSE-850-PCM",
	.dai_fmt = SND_SOC_DAIFMT_I2S
		 | SND_SOC_DAIFMT_NB_NF
		 | SND_SOC_DAIFMT_CBP_CFC,
	SND_SOC_DAILINK_REG(pcm),
};

static struct snd_soc_card tse850_card = {
	.name = "TSE-850-ASoC",
	.owner = THIS_MODULE,
	.dai_link = &tse850_dailink,
	.num_links = 1,
	.dapm_widgets = tse850_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tse850_dapm_widgets),
	.dapm_routes = tse850_intercon,
	.num_dapm_routes = ARRAY_SIZE(tse850_intercon),
	.fully_routed = true,
};

static int tse850_dt_init(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *codec_np, *cpu_np;
	struct snd_soc_dai_link *dailink = &tse850_dailink;

	if (!np) {
		dev_err(&pdev->dev, "only device tree supported\n");
		return -EINVAL;
	}

	cpu_np = of_parse_phandle(np, "axentia,cpu-dai", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "failed to get cpu dai\n");
		return -EINVAL;
	}
	dailink->cpus->of_node = cpu_np;
	dailink->platforms->of_node = cpu_np;
	of_node_put(cpu_np);

	codec_np = of_parse_phandle(np, "axentia,audio-codec", 0);
	if (!codec_np) {
		dev_err(&pdev->dev, "failed to get codec info\n");
		return -EINVAL;
	}
	dailink->codecs->of_node = codec_np;
	of_node_put(codec_np);

	return 0;
}

static int tse850_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &tse850_card;
	struct device *dev = card->dev = &pdev->dev;
	struct tse850_priv *tse850;
	int ret;

	tse850 = devm_kzalloc(dev, sizeof(*tse850), GFP_KERNEL);
	if (!tse850)
		return -ENOMEM;

	snd_soc_card_set_drvdata(card, tse850);

	ret = tse850_dt_init(pdev);
	if (ret) {
		dev_err(dev, "failed to init dt info\n");
		return ret;
	}

	tse850->add = devm_gpiod_get(dev, "axentia,add", GPIOD_OUT_HIGH);
	if (IS_ERR(tse850->add))
		return dev_err_probe(dev, PTR_ERR(tse850->add),
				     "failed to get 'add' gpio\n");
	tse850->add_cache = 1;

	tse850->loop1 = devm_gpiod_get(dev, "axentia,loop1", GPIOD_OUT_HIGH);
	if (IS_ERR(tse850->loop1))
		return dev_err_probe(dev, PTR_ERR(tse850->loop1),
				     "failed to get 'loop1' gpio\n");
	tse850->loop1_cache = 1;

	tse850->loop2 = devm_gpiod_get(dev, "axentia,loop2", GPIOD_OUT_HIGH);
	if (IS_ERR(tse850->loop2))
		return dev_err_probe(dev, PTR_ERR(tse850->loop2),
				     "failed to get 'loop2' gpio\n");
	tse850->loop2_cache = 1;

	tse850->ana = devm_regulator_get(dev, "axentia,ana");
	if (IS_ERR(tse850->ana))
		return dev_err_probe(dev, PTR_ERR(tse850->ana),
				     "failed to get 'ana' regulator\n");

	ret = regulator_enable(tse850->ana);
	if (ret < 0) {
		dev_err(dev, "failed to enable the 'ana' regulator\n");
		return ret;
	}

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(dev, "snd_soc_register_card failed\n");
		goto err_disable_ana;
	}

	return 0;

err_disable_ana:
	regulator_disable(tse850->ana);
	return ret;
}

static void tse850_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct tse850_priv *tse850 = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);
	regulator_disable(tse850->ana);
}

static const struct of_device_id tse850_dt_ids[] = {
	{ .compatible = "axentia,tse850-pcm5142", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tse850_dt_ids);

static struct platform_driver tse850_driver = {
	.driver = {
		.name = "axentia-tse850-pcm5142",
		.of_match_table = tse850_dt_ids,
	},
	.probe = tse850_probe,
	.remove_new = tse850_remove,
};

module_platform_driver(tse850_driver);

/* Module information */
MODULE_AUTHOR("Peter Rosin <peda@axentia.se>");
MODULE_DESCRIPTION("ALSA SoC driver for TSE-850 with PCM5142 codec");
MODULE_LICENSE("GPL v2");
