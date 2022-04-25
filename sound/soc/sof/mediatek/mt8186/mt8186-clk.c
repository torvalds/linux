// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2022 Mediatek Corporation. All rights reserved.
//
// Author: Allen-KH Cheng <allen-kh.cheng@mediatek.com>
//         Tinghan Shen <tinghan.shen@mediatek.com>
//
// Hardware interface for mt8186 DSP clock

#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>

#include "../../sof-audio.h"
#include "../../ops.h"
#include "../adsp_helper.h"
#include "mt8186.h"
#include "mt8186-clk.h"

static const char *adsp_clks[ADSP_CLK_MAX] = {
	[CLK_TOP_AUDIODSP] = "audiodsp_sel",
	[CLK_TOP_ADSP_BUS] = "adsp_bus_sel",
};

int mt8186_adsp_init_clock(struct snd_sof_dev *sdev)
{
	struct adsp_priv *priv = sdev->pdata->hw_pdata;
	struct device *dev = sdev->dev;
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
	struct adsp_priv *priv = sdev->pdata->hw_pdata;
	struct device *dev = sdev->dev;
	int ret;

	ret = clk_prepare_enable(priv->clk[CLK_TOP_AUDIODSP]);
	if (ret) {
		dev_err(dev, "%s clk_prepare_enable(audiodsp) fail %d\n",
			__func__, ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->clk[CLK_TOP_ADSP_BUS]);
	if (ret) {
		dev_err(dev, "%s clk_prepare_enable(adsp_bus) fail %d\n",
			__func__, ret);
		clk_disable_unprepare(priv->clk[CLK_TOP_AUDIODSP]);
		return ret;
	}

	return 0;
}

static void adsp_disable_all_clock(struct snd_sof_dev *sdev)
{
	struct adsp_priv *priv = sdev->pdata->hw_pdata;

	clk_disable_unprepare(priv->clk[CLK_TOP_ADSP_BUS]);
	clk_disable_unprepare(priv->clk[CLK_TOP_AUDIODSP]);
}

int adsp_clock_on(struct snd_sof_dev *sdev)
{
	struct device *dev = sdev->dev;
	int ret;

	ret = adsp_enable_all_clock(sdev);
	if (ret) {
		dev_err(dev, "failed to adsp_enable_clock: %d\n", ret);
		return ret;
	}
	snd_sof_dsp_write(sdev, DSP_REG_BAR, ADSP_CK_EN,
			  UART_EN | DMA_EN | TIMER_EN | COREDBG_EN | CORE_CLK_EN);
	snd_sof_dsp_write(sdev, DSP_REG_BAR, ADSP_UART_CTRL,
			  UART_BCLK_CG | UART_RSTN);

	return 0;
}

void adsp_clock_off(struct snd_sof_dev *sdev)
{
	snd_sof_dsp_write(sdev, DSP_REG_BAR, ADSP_CK_EN, 0);
	snd_sof_dsp_write(sdev, DSP_REG_BAR, ADSP_UART_CTRL, 0);
	adsp_disable_all_clock(sdev);
}

