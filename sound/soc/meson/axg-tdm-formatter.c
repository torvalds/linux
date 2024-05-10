// SPDX-License-Identifier: (GPL-2.0 OR MIT)
//
// Copyright (c) 2018 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/soc.h>

#include "axg-tdm-formatter.h"

struct axg_tdm_formatter {
	struct list_head list;
	struct axg_tdm_stream *stream;
	const struct axg_tdm_formatter_driver *drv;
	struct clk *pclk;
	struct clk *sclk;
	struct clk *lrclk;
	struct clk *sclk_sel;
	struct clk *lrclk_sel;
	struct reset_control *reset;
	bool enabled;
	struct regmap *map;
};

int axg_tdm_formatter_set_channel_masks(struct regmap *map,
					struct axg_tdm_stream *ts,
					unsigned int offset)
{
	unsigned int ch = ts->channels;
	u32 val[AXG_TDM_NUM_LANES];
	int i, j, k;

	/*
	 * We need to mimick the slot distribution used by the HW to keep the
	 * channel placement consistent regardless of the number of channel
	 * in the stream. This is why the odd algorithm below is used.
	 */
	memset(val, 0, sizeof(*val) * AXG_TDM_NUM_LANES);

	/*
	 * Distribute the channels of the stream over the available slots
	 * of each TDM lane. We need to go over the 32 slots ...
	 */
	for (i = 0; (i < 32) && ch; i += 2) {
		/* ... of all the lanes ... */
		for (j = 0; j < AXG_TDM_NUM_LANES; j++) {
			/* ... then distribute the channels in pairs */
			for (k = 0; k < 2; k++) {
				if ((BIT(i + k) & ts->mask[j]) && ch) {
					val[j] |= BIT(i + k);
					ch -= 1;
				}
			}
		}
	}

	/*
	 * If we still have channel left at the end of the process, it means
	 * the stream has more channels than we can accommodate and we should
	 * have caught this earlier.
	 */
	if (WARN_ON(ch != 0)) {
		pr_err("channel mask error\n");
		return -EINVAL;
	}

	for (i = 0; i < AXG_TDM_NUM_LANES; i++) {
		regmap_write(map, offset, val[i]);
		offset += regmap_get_reg_stride(map);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(axg_tdm_formatter_set_channel_masks);

static int axg_tdm_formatter_enable(struct axg_tdm_formatter *formatter)
{
	struct axg_tdm_stream *ts = formatter->stream;
	bool invert;
	int ret;

	/* Do nothing if the formatter is already enabled */
	if (formatter->enabled)
		return 0;

	/*
	 * On the g12a (and possibly other SoCs), when a stream using
	 * multiple lanes is restarted, it will sometimes not start
	 * from the first lane, but randomly from another used one.
	 * The result is an unexpected and random channel shift.
	 *
	 * The hypothesis is that an HW counter is not properly reset
	 * and the formatter simply starts on the lane it stopped
	 * before. Unfortunately, there does not seems to be a way to
	 * reset this through the registers of the block.
	 *
	 * However, the g12a has indenpendent reset lines for each audio
	 * devices. Using this reset before each start solves the issue.
	 */
	ret = reset_control_reset(formatter->reset);
	if (ret)
		return ret;

	/*
	 * If sclk is inverted, it means the bit should latched on the
	 * rising edge which is what our HW expects. If not, we need to
	 * invert it before the formatter.
	 */
	invert = axg_tdm_sclk_invert(ts->iface->fmt);
	ret = clk_set_phase(formatter->sclk, invert ? 0 : 180);
	if (ret)
		return ret;

	/* Setup the stream parameter in the formatter */
	ret = formatter->drv->ops->prepare(formatter->map,
					   formatter->drv->quirks,
					   formatter->stream);
	if (ret)
		return ret;

	/* Enable the signal clocks feeding the formatter */
	ret = clk_prepare_enable(formatter->sclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(formatter->lrclk);
	if (ret) {
		clk_disable_unprepare(formatter->sclk);
		return ret;
	}

	/* Finally, actually enable the formatter */
	formatter->drv->ops->enable(formatter->map);
	formatter->enabled = true;

	return 0;
}

static void axg_tdm_formatter_disable(struct axg_tdm_formatter *formatter)
{
	/* Do nothing if the formatter is already disabled */
	if (!formatter->enabled)
		return;

	formatter->drv->ops->disable(formatter->map);
	clk_disable_unprepare(formatter->lrclk);
	clk_disable_unprepare(formatter->sclk);
	formatter->enabled = false;
}

static int axg_tdm_formatter_attach(struct axg_tdm_formatter *formatter)
{
	struct axg_tdm_stream *ts = formatter->stream;
	int ret = 0;

	mutex_lock(&ts->lock);

	/* Catch up if the stream is already running when we attach */
	if (ts->ready) {
		ret = axg_tdm_formatter_enable(formatter);
		if (ret) {
			pr_err("failed to enable formatter\n");
			goto out;
		}
	}

	list_add_tail(&formatter->list, &ts->formatter_list);
out:
	mutex_unlock(&ts->lock);
	return ret;
}

static void axg_tdm_formatter_dettach(struct axg_tdm_formatter *formatter)
{
	struct axg_tdm_stream *ts = formatter->stream;

	mutex_lock(&ts->lock);
	list_del(&formatter->list);
	mutex_unlock(&ts->lock);

	axg_tdm_formatter_disable(formatter);
}

static int axg_tdm_formatter_power_up(struct axg_tdm_formatter *formatter,
				      struct snd_soc_dapm_widget *w)
{
	struct axg_tdm_stream *ts = formatter->drv->ops->get_stream(w);
	int ret;

	/*
	 * If we don't get a stream at this stage, it would mean that the
	 * widget is powering up but is not attached to any backend DAI.
	 * It should not happen, ever !
	 */
	if (WARN_ON(!ts))
		return -ENODEV;

	/* Clock our device */
	ret = clk_prepare_enable(formatter->pclk);
	if (ret)
		return ret;

	/* Reparent the bit clock to the TDM interface */
	ret = clk_set_parent(formatter->sclk_sel, ts->iface->sclk);
	if (ret)
		goto disable_pclk;

	/* Reparent the sample clock to the TDM interface */
	ret = clk_set_parent(formatter->lrclk_sel, ts->iface->lrclk);
	if (ret)
		goto disable_pclk;

	formatter->stream = ts;
	ret = axg_tdm_formatter_attach(formatter);
	if (ret)
		goto disable_pclk;

	return 0;

disable_pclk:
	clk_disable_unprepare(formatter->pclk);
	return ret;
}

static void axg_tdm_formatter_power_down(struct axg_tdm_formatter *formatter)
{
	axg_tdm_formatter_dettach(formatter);
	clk_disable_unprepare(formatter->pclk);
	formatter->stream = NULL;
}

int axg_tdm_formatter_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *control,
			    int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	struct axg_tdm_formatter *formatter = snd_soc_component_get_drvdata(c);
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = axg_tdm_formatter_power_up(formatter, w);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		axg_tdm_formatter_power_down(formatter);
		break;

	default:
		dev_err(c->dev, "Unexpected event %d\n", event);
		return -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(axg_tdm_formatter_event);

int axg_tdm_formatter_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct axg_tdm_formatter_driver *drv;
	struct axg_tdm_formatter *formatter;
	void __iomem *regs;

	drv = of_device_get_match_data(dev);
	if (!drv) {
		dev_err(dev, "failed to match device\n");
		return -ENODEV;
	}

	formatter = devm_kzalloc(dev, sizeof(*formatter), GFP_KERNEL);
	if (!formatter)
		return -ENOMEM;
	platform_set_drvdata(pdev, formatter);
	formatter->drv = drv;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	formatter->map = devm_regmap_init_mmio(dev, regs, drv->regmap_cfg);
	if (IS_ERR(formatter->map)) {
		dev_err(dev, "failed to init regmap: %ld\n",
			PTR_ERR(formatter->map));
		return PTR_ERR(formatter->map);
	}

	/* Peripharal clock */
	formatter->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(formatter->pclk))
		return dev_err_probe(dev, PTR_ERR(formatter->pclk), "failed to get pclk\n");

	/* Formatter bit clock */
	formatter->sclk = devm_clk_get(dev, "sclk");
	if (IS_ERR(formatter->sclk))
		return dev_err_probe(dev, PTR_ERR(formatter->sclk), "failed to get sclk\n");

	/* Formatter sample clock */
	formatter->lrclk = devm_clk_get(dev, "lrclk");
	if (IS_ERR(formatter->lrclk))
		return dev_err_probe(dev, PTR_ERR(formatter->lrclk), "failed to get lrclk\n");

	/* Formatter bit clock input multiplexer */
	formatter->sclk_sel = devm_clk_get(dev, "sclk_sel");
	if (IS_ERR(formatter->sclk_sel))
		return dev_err_probe(dev, PTR_ERR(formatter->sclk_sel), "failed to get sclk_sel\n");

	/* Formatter sample clock input multiplexer */
	formatter->lrclk_sel = devm_clk_get(dev, "lrclk_sel");
	if (IS_ERR(formatter->lrclk_sel))
		return dev_err_probe(dev, PTR_ERR(formatter->lrclk_sel),
				     "failed to get lrclk_sel\n");

	/* Formatter dedicated reset line */
	formatter->reset = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(formatter->reset))
		return dev_err_probe(dev, PTR_ERR(formatter->reset), "failed to get reset\n");

	return devm_snd_soc_register_component(dev, drv->component_drv,
					       NULL, 0);
}
EXPORT_SYMBOL_GPL(axg_tdm_formatter_probe);

int axg_tdm_stream_start(struct axg_tdm_stream *ts)
{
	struct axg_tdm_formatter *formatter;
	int ret = 0;

	mutex_lock(&ts->lock);
	ts->ready = true;

	/* Start all the formatters attached to the stream */
	list_for_each_entry(formatter, &ts->formatter_list, list) {
		ret = axg_tdm_formatter_enable(formatter);
		if (ret) {
			pr_err("failed to start tdm stream\n");
			goto out;
		}
	}

out:
	mutex_unlock(&ts->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(axg_tdm_stream_start);

void axg_tdm_stream_stop(struct axg_tdm_stream *ts)
{
	struct axg_tdm_formatter *formatter;

	mutex_lock(&ts->lock);
	ts->ready = false;

	/* Stop all the formatters attached to the stream */
	list_for_each_entry(formatter, &ts->formatter_list, list) {
		axg_tdm_formatter_disable(formatter);
	}

	mutex_unlock(&ts->lock);
}
EXPORT_SYMBOL_GPL(axg_tdm_stream_stop);

struct axg_tdm_stream *axg_tdm_stream_alloc(struct axg_tdm_iface *iface)
{
	struct axg_tdm_stream *ts;

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts) {
		INIT_LIST_HEAD(&ts->formatter_list);
		mutex_init(&ts->lock);
		ts->iface = iface;
	}

	return ts;
}
EXPORT_SYMBOL_GPL(axg_tdm_stream_alloc);

void axg_tdm_stream_free(struct axg_tdm_stream *ts)
{
	/*
	 * If the list is not empty, it would mean that one of the formatter
	 * widget is still powered and attached to the interface while we
	 * are removing the TDM DAI. It should not be possible
	 */
	WARN_ON(!list_empty(&ts->formatter_list));
	mutex_destroy(&ts->lock);
	kfree(ts);
}
EXPORT_SYMBOL_GPL(axg_tdm_stream_free);

int axg_tdm_stream_set_cont_clocks(struct axg_tdm_stream *ts,
				   unsigned int fmt)
{
	int ret = 0;

	if (fmt & SND_SOC_DAIFMT_CONT) {
		/* Clock are already enabled - skipping */
		if (ts->clk_enabled)
			return 0;

		ret = clk_prepare_enable(ts->iface->mclk);
		if (ret)
			return ret;

		ret = clk_prepare_enable(ts->iface->sclk);
		if (ret)
			goto err_sclk;

		ret = clk_prepare_enable(ts->iface->lrclk);
		if (ret)
			goto err_lrclk;

		ts->clk_enabled = true;
		return 0;
	}

	/* Clocks are already disabled - skipping */
	if (!ts->clk_enabled)
		return 0;

	clk_disable_unprepare(ts->iface->lrclk);
err_lrclk:
	clk_disable_unprepare(ts->iface->sclk);
err_sclk:
	clk_disable_unprepare(ts->iface->mclk);
	ts->clk_enabled = false;
	return ret;
}
EXPORT_SYMBOL_GPL(axg_tdm_stream_set_cont_clocks);

MODULE_DESCRIPTION("Amlogic AXG TDM formatter driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
