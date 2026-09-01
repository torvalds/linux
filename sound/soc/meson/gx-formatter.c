// SPDX-License-Identifier: (GPL-2.0 OR MIT)
//
// Copyright (c) 2026 BayLibre, SAS.
// Author: Valerio Setti <vsetti@baylibre.com>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "gx-formatter.h"

struct gx_formatter {
	struct list_head list;
	struct gx_stream *stream;
	const struct gx_formatter_driver *drv;
	bool enabled;
	struct regmap *map;
};

static int gx_formatter_enable(struct gx_formatter *formatter)
{
	int ret;

	/* Do nothing if the formatter is already enabled */
	if (formatter->enabled)
		return 0;

	/* Setup the stream parameter in the formatter */
	if (formatter->drv->ops->prepare) {
		ret = formatter->drv->ops->prepare(formatter->map,
					   formatter->drv->quirks,
					   formatter->stream);
		if (ret)
			return ret;
	}

	/* Finally, actually enable the formatter */
	if (formatter->drv->ops->enable)
		formatter->drv->ops->enable(formatter->map);

	formatter->enabled = true;

	return 0;
}

static void gx_formatter_disable(struct gx_formatter *formatter)
{
	/* Do nothing if the formatter is already disabled */
	if (!formatter->enabled)
		return;

	if (formatter->drv->ops->disable)
		formatter->drv->ops->disable(formatter->map);

	formatter->enabled = false;
}

static int gx_formatter_attach(struct gx_formatter *formatter)
{
	struct gx_stream *ts = formatter->stream;
	int ret = 0;

	mutex_lock(&ts->lock);

	/* Catch up if the stream is already running when we attach */
	if (ts->ready) {
		ret = gx_formatter_enable(formatter);
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

static void gx_formatter_detach(struct gx_formatter *formatter)
{
	struct gx_stream *ts = formatter->stream;

	if (!ts)
		return;

	mutex_lock(&ts->lock);
	list_del(&formatter->list);
	mutex_unlock(&ts->lock);

	gx_formatter_disable(formatter);
}

static int gx_formatter_power_up(struct gx_formatter *formatter,
				      struct snd_soc_dapm_widget *w)
{
	struct gx_stream *ts = formatter->drv->ops->get_stream(w);
	int ret;

	/*
	 * If we don't get a stream at this stage, it would mean that the
	 * widget is powering up but is not attached to any backend DAI.
	 * It should not happen, ever !
	 */
	if (WARN_ON(!ts))
		return -ENODEV;

	formatter->stream = ts;
	INIT_LIST_HEAD(&formatter->list);
	ret = gx_formatter_attach(formatter);
	if (ret)
		return ret;

	return 0;
}

static void gx_formatter_power_down(struct gx_formatter *formatter)
{
	gx_formatter_detach(formatter);
	formatter->stream = NULL;
}

int gx_formatter_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *control,
		       int event)
{
	struct snd_soc_component *c;
	struct gx_formatter *formatter;
	int ret = 0;

	c = snd_soc_dapm_to_component(w->dapm);

	if (w->priv)
		formatter = w->priv;
	else
		formatter = snd_soc_component_get_drvdata(c);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = gx_formatter_power_up(formatter, w);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		gx_formatter_power_down(formatter);
		break;

	default:
		dev_err(c->dev, "Unexpected event %d\n", event);
		return -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(gx_formatter_event);

int gx_formatter_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct gx_formatter_driver *drv;
	struct gx_formatter *formatter;
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

	return devm_snd_soc_register_component(dev, drv->component_drv,
					       NULL, 0);
}
EXPORT_SYMBOL_GPL(gx_formatter_probe);

int gx_formatter_create(struct device *dev,
			struct snd_soc_dapm_widget *w,
			const struct gx_formatter_driver *drv,
			struct regmap *regmap)
{
	struct gx_formatter *formatter;

	formatter = devm_kzalloc(dev, sizeof(*formatter), GFP_KERNEL);
	if (!formatter)
		return -ENOMEM;

	formatter->drv = drv;
	formatter->map = regmap;

	w->priv = formatter;

	return 0;
}
EXPORT_SYMBOL_GPL(gx_formatter_create);

int gx_stream_start(struct gx_stream *ts)
{
	struct gx_formatter *formatter;
	int ret = 0;

	mutex_lock(&ts->lock);

	/* Start all the formatters attached to the stream */
	list_for_each_entry(formatter, &ts->formatter_list, list) {
		ret = gx_formatter_enable(formatter);
		if (ret) {
			pr_err("failed to enable formatter\n");
			goto out;
		}
	}

	ts->ready = true;

out:
	mutex_unlock(&ts->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(gx_stream_start);

void gx_stream_stop(struct gx_stream *ts)
{
	struct gx_formatter *formatter;

	mutex_lock(&ts->lock);
	ts->ready = false;

	/* Stop all the formatters attached to the stream */
	list_for_each_entry(formatter, &ts->formatter_list, list) {
		gx_formatter_disable(formatter);
	}

	mutex_unlock(&ts->lock);
}
EXPORT_SYMBOL_GPL(gx_stream_stop);

struct gx_stream *gx_stream_alloc(struct gx_iface *iface)
{
	struct gx_stream *ts;

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts) {
		INIT_LIST_HEAD(&ts->formatter_list);
		mutex_init(&ts->lock);
		ts->iface = iface;
	}

	return ts;
}
EXPORT_SYMBOL_GPL(gx_stream_alloc);

void gx_stream_free(struct gx_stream *ts)
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
EXPORT_SYMBOL_GPL(gx_stream_free);

MODULE_DESCRIPTION("Amlogic GX formatter driver");
MODULE_AUTHOR("Valerio Setti <vsetti@baylibre.com>");
MODULE_LICENSE("GPL");
