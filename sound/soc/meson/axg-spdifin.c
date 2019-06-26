// SPDX-License-Identifier: (GPL-2.0 OR MIT)
//
// Copyright (c) 2018 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/pcm_params.h>

#define SPDIFIN_CTRL0			0x00
#define  SPDIFIN_CTRL0_EN		BIT(31)
#define  SPDIFIN_CTRL0_RST_OUT		BIT(29)
#define  SPDIFIN_CTRL0_RST_IN		BIT(28)
#define  SPDIFIN_CTRL0_WIDTH_SEL	BIT(24)
#define  SPDIFIN_CTRL0_STATUS_CH_SHIFT	11
#define  SPDIFIN_CTRL0_STATUS_SEL	GENMASK(10, 8)
#define  SPDIFIN_CTRL0_SRC_SEL		GENMASK(5, 4)
#define  SPDIFIN_CTRL0_CHK_VALID	BIT(3)
#define SPDIFIN_CTRL1			0x04
#define  SPDIFIN_CTRL1_BASE_TIMER	GENMASK(19, 0)
#define  SPDIFIN_CTRL1_IRQ_MASK		GENMASK(27, 20)
#define SPDIFIN_CTRL2			0x08
#define  SPDIFIN_THRES_PER_REG		3
#define  SPDIFIN_THRES_WIDTH		10
#define SPDIFIN_CTRL3			0x0c
#define SPDIFIN_CTRL4			0x10
#define  SPDIFIN_TIMER_PER_REG		4
#define  SPDIFIN_TIMER_WIDTH		8
#define SPDIFIN_CTRL5			0x14
#define SPDIFIN_CTRL6			0x18
#define SPDIFIN_STAT0			0x1c
#define  SPDIFIN_STAT0_MODE		GENMASK(30, 28)
#define  SPDIFIN_STAT0_MAXW		GENMASK(17, 8)
#define  SPDIFIN_STAT0_IRQ		GENMASK(7, 0)
#define  SPDIFIN_IRQ_MODE_CHANGED	BIT(2)
#define SPDIFIN_STAT1			0x20
#define SPDIFIN_STAT2			0x24
#define SPDIFIN_MUTE_VAL		0x28

#define SPDIFIN_MODE_NUM		7

struct axg_spdifin_cfg {
	const unsigned int *mode_rates;
	unsigned int ref_rate;
};

struct axg_spdifin {
	const struct axg_spdifin_cfg *conf;
	struct regmap *map;
	struct clk *refclk;
	struct clk *pclk;
};

/*
 * TODO:
 * It would have been nice to check the actual rate against the sample rate
 * requested in hw_params(). Unfortunately, I was not able to make the mode
 * detection and IRQ work reliably:
 *
 * 1. IRQs are generated on mode change only, so there is no notification
 *    on transition between no signal and mode 0 (32kHz).
 * 2. Mode detection very often has glitches, and may detects the
 *    lowest or the highest mode before zeroing in on the actual mode.
 *
 * This makes calling snd_pcm_stop() difficult to get right. Even notifying
 * the kcontrol would be very unreliable at this point.
 * Let's keep things simple until the magic spell that makes this work is
 * found.
 */

static unsigned int axg_spdifin_get_rate(struct axg_spdifin *priv)
{
	unsigned int stat, mode, rate = 0;

	regmap_read(priv->map, SPDIFIN_STAT0, &stat);
	mode = FIELD_GET(SPDIFIN_STAT0_MODE, stat);

	/*
	 * If max width is zero, we are not capturing anything.
	 * Also Sometimes, when the capture is on but there is no data,
	 * mode is SPDIFIN_MODE_NUM, but not always ...
	 */
	if (FIELD_GET(SPDIFIN_STAT0_MAXW, stat) &&
	    mode < SPDIFIN_MODE_NUM)
		rate = priv->conf->mode_rates[mode];

	return rate;
}

static int axg_spdifin_prepare(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct axg_spdifin *priv = snd_soc_dai_get_drvdata(dai);

	/* Apply both reset */
	regmap_update_bits(priv->map, SPDIFIN_CTRL0,
			   SPDIFIN_CTRL0_RST_OUT |
			   SPDIFIN_CTRL0_RST_IN,
			   0);

	/* Clear out reset before in reset */
	regmap_update_bits(priv->map, SPDIFIN_CTRL0,
			   SPDIFIN_CTRL0_RST_OUT, SPDIFIN_CTRL0_RST_OUT);
	regmap_update_bits(priv->map, SPDIFIN_CTRL0,
			   SPDIFIN_CTRL0_RST_IN,  SPDIFIN_CTRL0_RST_IN);

	return 0;
}

static int axg_spdifin_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct axg_spdifin *priv = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = clk_prepare_enable(priv->refclk);
	if (ret) {
		dev_err(dai->dev,
			"failed to enable spdifin reference clock\n");
		return ret;
	}

	regmap_update_bits(priv->map, SPDIFIN_CTRL0, SPDIFIN_CTRL0_EN,
			   SPDIFIN_CTRL0_EN);

	return 0;
}

static void axg_spdifin_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct axg_spdifin *priv = snd_soc_dai_get_drvdata(dai);

	regmap_update_bits(priv->map, SPDIFIN_CTRL0, SPDIFIN_CTRL0_EN, 0);
	clk_disable_unprepare(priv->refclk);
}

static void axg_spdifin_write_mode_param(struct regmap *map, int mode,
					 unsigned int val,
					 unsigned int num_per_reg,
					 unsigned int base_reg,
					 unsigned int width)
{
	uint64_t offset = mode;
	unsigned int reg, shift, rem;

	rem = do_div(offset, num_per_reg);

	reg = offset * regmap_get_reg_stride(map) + base_reg;
	shift = width * (num_per_reg - 1 - rem);

	regmap_update_bits(map, reg, GENMASK(width - 1, 0) << shift,
			   val << shift);
}

static void axg_spdifin_write_timer(struct regmap *map, int mode,
				    unsigned int val)
{
	axg_spdifin_write_mode_param(map, mode, val, SPDIFIN_TIMER_PER_REG,
				     SPDIFIN_CTRL4, SPDIFIN_TIMER_WIDTH);
}

static void axg_spdifin_write_threshold(struct regmap *map, int mode,
					unsigned int val)
{
	axg_spdifin_write_mode_param(map, mode, val, SPDIFIN_THRES_PER_REG,
				     SPDIFIN_CTRL2, SPDIFIN_THRES_WIDTH);
}

static unsigned int axg_spdifin_mode_timer(struct axg_spdifin *priv,
					   int mode,
					   unsigned int rate)
{
	/*
	 * Number of period of the reference clock during a period of the
	 * input signal reference clock
	 */
	return rate / (128 * priv->conf->mode_rates[mode]);
}

static int axg_spdifin_sample_mode_config(struct snd_soc_dai *dai,
					  struct axg_spdifin *priv)
{
	unsigned int rate, t_next;
	int ret, i = SPDIFIN_MODE_NUM - 1;

	/* Set spdif input reference clock */
	ret = clk_set_rate(priv->refclk, priv->conf->ref_rate);
	if (ret) {
		dev_err(dai->dev, "reference clock rate set failed\n");
		return ret;
	}

	/*
	 * The rate actually set might be slightly different, get
	 * the actual rate for the following mode calculation
	 */
	rate = clk_get_rate(priv->refclk);

	/* HW will update mode every 1ms */
	regmap_update_bits(priv->map, SPDIFIN_CTRL1,
			   SPDIFIN_CTRL1_BASE_TIMER,
			   FIELD_PREP(SPDIFIN_CTRL1_BASE_TIMER, rate / 1000));

	/* Threshold based on the minimum width between two edges */
	regmap_update_bits(priv->map, SPDIFIN_CTRL0,
			   SPDIFIN_CTRL0_WIDTH_SEL, SPDIFIN_CTRL0_WIDTH_SEL);

	/* Calculate the last timer which has no threshold */
	t_next = axg_spdifin_mode_timer(priv, i, rate);
	axg_spdifin_write_timer(priv->map, i, t_next);

	do {
		unsigned int t;

		i -= 1;

		/* Calculate the timer */
		t = axg_spdifin_mode_timer(priv, i, rate);

		/* Set the timer value */
		axg_spdifin_write_timer(priv->map, i, t);

		/* Set the threshold value */
		axg_spdifin_write_threshold(priv->map, i, t + t_next);

		/* Save the current timer for the next threshold calculation */
		t_next = t;

	} while (i > 0);

	return 0;
}

static int axg_spdifin_dai_probe(struct snd_soc_dai *dai)
{
	struct axg_spdifin *priv = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = clk_prepare_enable(priv->pclk);
	if (ret) {
		dev_err(dai->dev, "failed to enable pclk\n");
		return ret;
	}

	ret = axg_spdifin_sample_mode_config(dai, priv);
	if (ret) {
		dev_err(dai->dev, "mode configuration failed\n");
		clk_disable_unprepare(priv->pclk);
		return ret;
	}

	return 0;
}

static int axg_spdifin_dai_remove(struct snd_soc_dai *dai)
{
	struct axg_spdifin *priv = snd_soc_dai_get_drvdata(dai);

	clk_disable_unprepare(priv->pclk);
	return 0;
}

static const struct snd_soc_dai_ops axg_spdifin_ops = {
	.prepare	= axg_spdifin_prepare,
	.startup	= axg_spdifin_startup,
	.shutdown	= axg_spdifin_shutdown,
};

static int axg_spdifin_iec958_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int axg_spdifin_get_status_mask(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	int i;

	for (i = 0; i < 24; i++)
		ucontrol->value.iec958.status[i] = 0xff;

	return 0;
}

static int axg_spdifin_get_status(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct axg_spdifin *priv = snd_soc_component_get_drvdata(c);
	int i, j;

	for (i = 0; i < 6; i++) {
		unsigned int val;

		regmap_update_bits(priv->map, SPDIFIN_CTRL0,
				   SPDIFIN_CTRL0_STATUS_SEL,
				   FIELD_PREP(SPDIFIN_CTRL0_STATUS_SEL, i));

		regmap_read(priv->map, SPDIFIN_STAT1, &val);

		for (j = 0; j < 4; j++) {
			unsigned int offset = i * 4 + j;

			ucontrol->value.iec958.status[offset] =
				(val >> (j * 8)) & 0xff;
		}
	}

	return 0;
}

#define AXG_SPDIFIN_IEC958_MASK						\
	{								\
		.access = SNDRV_CTL_ELEM_ACCESS_READ,			\
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,			\
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, MASK),	\
		.info = axg_spdifin_iec958_info,			\
		.get = axg_spdifin_get_status_mask,			\
	}

#define AXG_SPDIFIN_IEC958_STATUS					\
	{								\
		.access = (SNDRV_CTL_ELEM_ACCESS_READ |			\
			   SNDRV_CTL_ELEM_ACCESS_VOLATILE),		\
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,			\
		.name =	SNDRV_CTL_NAME_IEC958("", CAPTURE, NONE),	\
		.info = axg_spdifin_iec958_info,			\
		.get = axg_spdifin_get_status,				\
	}

static const char * const spdifin_chsts_src_texts[] = {
	"A", "B",
};

static SOC_ENUM_SINGLE_DECL(axg_spdifin_chsts_src_enum, SPDIFIN_CTRL0,
			    SPDIFIN_CTRL0_STATUS_CH_SHIFT,
			    spdifin_chsts_src_texts);

static int axg_spdifin_rate_lock_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 192000;

	return 0;
}

static int axg_spdifin_rate_lock_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct axg_spdifin *priv = snd_soc_component_get_drvdata(c);

	ucontrol->value.integer.value[0] = axg_spdifin_get_rate(priv);

	return 0;
}

#define AXG_SPDIFIN_LOCK_RATE(xname)				\
	{							\
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,		\
		.access = (SNDRV_CTL_ELEM_ACCESS_READ |		\
			   SNDRV_CTL_ELEM_ACCESS_VOLATILE),	\
		.get = axg_spdifin_rate_lock_get,		\
		.info = axg_spdifin_rate_lock_info,		\
		.name = xname,					\
	}

static const struct snd_kcontrol_new axg_spdifin_controls[] = {
	AXG_SPDIFIN_LOCK_RATE("Capture Rate Lock"),
	SOC_DOUBLE("Capture Switch", SPDIFIN_CTRL0, 7, 6, 1, 1),
	SOC_ENUM(SNDRV_CTL_NAME_IEC958("", CAPTURE, NONE) "Src",
		 axg_spdifin_chsts_src_enum),
	AXG_SPDIFIN_IEC958_MASK,
	AXG_SPDIFIN_IEC958_STATUS,
};

static const struct snd_soc_component_driver axg_spdifin_component_drv = {
	.controls		= axg_spdifin_controls,
	.num_controls		= ARRAY_SIZE(axg_spdifin_controls),
};

static const struct regmap_config axg_spdifin_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= SPDIFIN_MUTE_VAL,
};

static const unsigned int axg_spdifin_mode_rates[SPDIFIN_MODE_NUM] = {
	32000, 44100, 48000, 88200, 96000, 176400, 192000,
};

static const struct axg_spdifin_cfg axg_cfg = {
	.mode_rates = axg_spdifin_mode_rates,
	.ref_rate = 333333333,
};

static const struct of_device_id axg_spdifin_of_match[] = {
	{
		.compatible = "amlogic,axg-spdifin",
		.data = &axg_cfg,
	}, {}
};
MODULE_DEVICE_TABLE(of, axg_spdifin_of_match);

static struct snd_soc_dai_driver *
axg_spdifin_get_dai_drv(struct device *dev, struct axg_spdifin *priv)
{
	struct snd_soc_dai_driver *drv;
	int i;

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return ERR_PTR(-ENOMEM);

	drv->name = "SPDIF Input";
	drv->ops = &axg_spdifin_ops;
	drv->probe = axg_spdifin_dai_probe;
	drv->remove = axg_spdifin_dai_remove;
	drv->capture.stream_name = "Capture";
	drv->capture.channels_min = 1;
	drv->capture.channels_max = 2;
	drv->capture.formats = SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE;

	for (i = 0; i < SPDIFIN_MODE_NUM; i++) {
		unsigned int rb =
			snd_pcm_rate_to_rate_bit(priv->conf->mode_rates[i]);

		if (rb == SNDRV_PCM_RATE_KNOT)
			return ERR_PTR(-EINVAL);

		drv->capture.rates |= rb;
	}

	return drv;
}

static int axg_spdifin_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct axg_spdifin *priv;
	struct snd_soc_dai_driver *dai_drv;
	struct resource *res;
	void __iomem *regs;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	priv->conf = of_device_get_match_data(dev);
	if (!priv->conf) {
		dev_err(dev, "failed to match device\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	priv->map = devm_regmap_init_mmio(dev, regs, &axg_spdifin_regmap_cfg);
	if (IS_ERR(priv->map)) {
		dev_err(dev, "failed to init regmap: %ld\n",
			PTR_ERR(priv->map));
		return PTR_ERR(priv->map);
	}

	priv->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(priv->pclk)) {
		ret = PTR_ERR(priv->pclk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get pclk: %d\n", ret);
		return ret;
	}

	priv->refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(priv->refclk)) {
		ret = PTR_ERR(priv->refclk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get mclk: %d\n", ret);
		return ret;
	}

	dai_drv = axg_spdifin_get_dai_drv(dev, priv);
	if (IS_ERR(dai_drv)) {
		dev_err(dev, "failed to get dai driver: %ld\n",
			PTR_ERR(dai_drv));
		return PTR_ERR(dai_drv);
	}

	return devm_snd_soc_register_component(dev, &axg_spdifin_component_drv,
					       dai_drv, 1);
}

static struct platform_driver axg_spdifin_pdrv = {
	.probe = axg_spdifin_probe,
	.driver = {
		.name = "axg-spdifin",
		.of_match_table = axg_spdifin_of_match,
	},
};
module_platform_driver(axg_spdifin_pdrv);

MODULE_DESCRIPTION("Amlogic AXG SPDIF Input driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
