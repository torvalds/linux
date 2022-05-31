// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2021 Mediatek Corporation. All rights reserved.
//
// Author: YC Hung <yc.hung@mediatek.com>
//
// Hardware interface for mt8195 DSP clock

#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include "mt8195.h"
#include "mt8195-clk.h"
#include "../adsp_helper.h"
#include "../../sof-audio.h"

static const char *adsp_clks[ADSP_CLK_MAX] = {
	[CLK_TOP_ADSP] = "adsp_sel",
	[CLK_TOP_CLK26M] = "clk26m_ck",
	[CLK_TOP_AUDIO_LOCAL_BUS] = "audio_local_bus",
	[CLK_TOP_MAINPLL_D7_D2] = "mainpll_d7_d2",
	[CLK_SCP_ADSP_AUDIODSP] = "scp_adsp_audiodsp",
	[CLK_TOP_AUDIO_H] = "audio_h",
};

int mt8195_adsp_init_clock(struct snd_sof_dev *sdev)
{
	struct device *dev = sdev->dev;
	struct adsp_priv *priv = sdev->pdata->hw_pdata;
	int i;

	priv->clk = devm_kcalloc(dev, ADSP_CLK_MAX, sizeof(*priv->clk), GFP_KERNEL);

	if (!priv->clk)
		return -ENOMEM;

	for (i = 0; i < ADSP_CLK_MAX; i++) {
		priv->clk[i] = devm_clk_get(dev, adsp_clks[i]);
		if (IS_ERR(priv->clk[i]))
			return PTR_ERR(priv->clk[i]);
	}

	return 0;
}

static int adsp_enable_all_clock(struct snd_sof_dev *sdev)
{
	struct device *dev = sdev->dev;
	struct adsp_priv *priv = sdev->pdata->hw_pdata;
	int ret;

	ret = clk_prepare_enable(priv->clk[CLK_TOP_MAINPLL_D7_D2]);
	if (ret) {
		dev_err(dev, "%s clk_prepare_enable(mainpll_d7_d2) fail %d\n",
			__func__, ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->clk[CLK_TOP_ADSP]);
	if (ret) {
		dev_err(dev, "%s clk_prepare_enable(adsp_sel) fail %d\n",
			__func__, ret);
		goto disable_mainpll_d7_d2_clk;
	}

	ret = clk_prepare_enable(priv->clk[CLK_TOP_AUDIO_LOCAL_BUS]);
	if (ret) {
		dev_err(dev, "%s clk_prepare_enable(audio_local_bus) fail %d\n",
			__func__, ret);
		goto disable_dsp_sel_clk;
	}

	ret = clk_prepare_enable(priv->clk[CLK_SCP_ADSP_AUDIODSP]);
	if (ret) {
		dev_err(dev, "%s clk_prepare_enable(scp_adsp_audiodsp) fail %d\n",
			__func__, ret);
		goto disable_audio_local_bus_clk;
	}

	ret = clk_prepare_enable(priv->clk[CLK_TOP_AUDIO_H]);
	if (ret) {
		dev_err(dev, "%s clk_prepare_enable(audio_h) fail %d\n",
			__func__, ret);
		goto disable_scp_adsp_audiodsp_clk;
	}

	return 0;

disable_scp_adsp_audiodsp_clk:
	clk_disable_unprepare(priv->clk[CLK_SCP_ADSP_AUDIODSP]);
disable_audio_local_bus_clk:
	clk_disable_unprepare(priv->clk[CLK_TOP_AUDIO_LOCAL_BUS]);
disable_dsp_sel_clk:
	clk_disable_unprepare(priv->clk[CLK_TOP_ADSP]);
disable_mainpll_d7_d2_clk:
	clk_disable_unprepare(priv->clk[CLK_TOP_MAINPLL_D7_D2]);

	return ret;
}

static void adsp_disable_all_clock(struct snd_sof_dev *sdev)
{
	struct adsp_priv *priv = sdev->pdata->hw_pdata;

	clk_disable_unprepare(priv->clk[CLK_TOP_AUDIO_H]);
	clk_disable_unprepare(priv->clk[CLK_SCP_ADSP_AUDIODSP]);
	clk_disable_unprepare(priv->clk[CLK_TOP_AUDIO_LOCAL_BUS]);
	clk_disable_unprepare(priv->clk[CLK_TOP_ADSP]);
	clk_disable_unprepare(priv->clk[CLK_TOP_MAINPLL_D7_D2]);
}

static int adsp_default_clk_init(struct snd_sof_dev *sdev, bool enable)
{
	struct device *dev = sdev->dev;
	struct adsp_priv *priv = sdev->pdata->hw_pdata;
	int ret;

	dev_dbg(dev, "%s: %s\n", __func__, enable ? "on" : "off");

	if (enable) {
		ret = clk_set_parent(priv->clk[CLK_TOP_ADSP],
				     priv->clk[CLK_TOP_CLK26M]);
		if (ret) {
			dev_err(dev, "failed to set dsp_sel to clk26m: %d\n", ret);
			return ret;
		}

		ret = clk_set_parent(priv->clk[CLK_TOP_AUDIO_LOCAL_BUS],
				     priv->clk[CLK_TOP_MAINPLL_D7_D2]);
		if (ret) {
			dev_err(dev, "set audio_local_bus failed %d\n", ret);
			return ret;
		}

		ret = adsp_enable_all_clock(sdev);
		if (ret) {
			dev_err(dev, "failed to adsp_enable_clock: %d\n", ret);
			return ret;
		}
	} else {
		adsp_disable_all_clock(sdev);
	}

	return 0;
}

int adsp_clock_on(struct snd_sof_dev *sdev)
{
	/* Open ADSP clock */
	return adsp_default_clk_init(sdev, 1);
}

int adsp_clock_off(struct snd_sof_dev *sdev)
{
	/* Close ADSP clock */
	return adsp_default_clk_init(sdev, 0);
}

