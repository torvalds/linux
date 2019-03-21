/*
 * STM32 ALSA SoC Digital Audio Interface (SAI) driver.
 *
 * Copyright (C) 2016, STMicroelectronics - All Rights Reserved
 * Author(s): Olivier Moysan <olivier.moysan@st.com> for STMicroelectronics.
 *
 * License terms: GPL V2.0.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/reset.h>

#include <sound/dmaengine_pcm.h>
#include <sound/core.h>

#include "stm32_sai.h"

static const struct stm32_sai_conf stm32_sai_conf_f4 = {
	.version = SAI_STM32F4,
	.has_spdif = false,
};

static const struct stm32_sai_conf stm32_sai_conf_h7 = {
	.version = SAI_STM32H7,
	.has_spdif = true,
};

static const struct of_device_id stm32_sai_ids[] = {
	{ .compatible = "st,stm32f4-sai", .data = (void *)&stm32_sai_conf_f4 },
	{ .compatible = "st,stm32h7-sai", .data = (void *)&stm32_sai_conf_h7 },
	{}
};

static int stm32_sai_pclk_disable(struct device *dev)
{
	struct stm32_sai_data *sai = dev_get_drvdata(dev);

	clk_disable_unprepare(sai->pclk);

	return 0;
}

static int stm32_sai_pclk_enable(struct device *dev)
{
	struct stm32_sai_data *sai = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(sai->pclk);
	if (ret) {
		dev_err(&sai->pdev->dev, "failed to enable clock: %d\n", ret);
		return ret;
	}

	return 0;
}

static int stm32_sai_sync_conf_client(struct stm32_sai_data *sai, int synci)
{
	int ret;

	/* Enable peripheral clock to allow GCR register access */
	ret = stm32_sai_pclk_enable(&sai->pdev->dev);
	if (ret)
		return ret;

	writel_relaxed(FIELD_PREP(SAI_GCR_SYNCIN_MASK, (synci - 1)), sai->base);

	stm32_sai_pclk_disable(&sai->pdev->dev);

	return 0;
}

static int stm32_sai_sync_conf_provider(struct stm32_sai_data *sai, int synco)
{
	u32 prev_synco;
	int ret;

	/* Enable peripheral clock to allow GCR register access */
	ret = stm32_sai_pclk_enable(&sai->pdev->dev);
	if (ret)
		return ret;

	dev_dbg(&sai->pdev->dev, "Set %pOFn%s as synchro provider\n",
		sai->pdev->dev.of_node,
		synco == STM_SAI_SYNC_OUT_A ? "A" : "B");

	prev_synco = FIELD_GET(SAI_GCR_SYNCOUT_MASK, readl_relaxed(sai->base));
	if (prev_synco != STM_SAI_SYNC_OUT_NONE && synco != prev_synco) {
		dev_err(&sai->pdev->dev, "%pOFn%s already set as sync provider\n",
			sai->pdev->dev.of_node,
			prev_synco == STM_SAI_SYNC_OUT_A ? "A" : "B");
			stm32_sai_pclk_disable(&sai->pdev->dev);
		return -EINVAL;
	}

	writel_relaxed(FIELD_PREP(SAI_GCR_SYNCOUT_MASK, synco), sai->base);

	stm32_sai_pclk_disable(&sai->pdev->dev);

	return 0;
}

static int stm32_sai_set_sync(struct stm32_sai_data *sai_client,
			      struct device_node *np_provider,
			      int synco, int synci)
{
	struct platform_device *pdev = of_find_device_by_node(np_provider);
	struct stm32_sai_data *sai_provider;
	int ret;

	if (!pdev) {
		dev_err(&sai_client->pdev->dev,
			"Device not found for node %pOFn\n", np_provider);
		of_node_put(np_provider);
		return -ENODEV;
	}

	sai_provider = platform_get_drvdata(pdev);
	if (!sai_provider) {
		dev_err(&sai_client->pdev->dev,
			"SAI sync provider data not found\n");
		ret = -EINVAL;
		goto error;
	}

	/* Configure sync client */
	ret = stm32_sai_sync_conf_client(sai_client, synci);
	if (ret < 0)
		goto error;

	/* Configure sync provider */
	ret = stm32_sai_sync_conf_provider(sai_provider, synco);

error:
	put_device(&pdev->dev);
	of_node_put(np_provider);
	return ret;
}

static int stm32_sai_probe(struct platform_device *pdev)
{
	struct stm32_sai_data *sai;
	struct reset_control *rst;
	struct resource *res;
	const struct of_device_id *of_id;

	sai = devm_kzalloc(&pdev->dev, sizeof(*sai), GFP_KERNEL);
	if (!sai)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sai->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sai->base))
		return PTR_ERR(sai->base);

	of_id = of_match_device(stm32_sai_ids, &pdev->dev);
	if (of_id)
		sai->conf = (struct stm32_sai_conf *)of_id->data;
	else
		return -EINVAL;

	if (!STM_SAI_IS_F4(sai)) {
		sai->pclk = devm_clk_get(&pdev->dev, "pclk");
		if (IS_ERR(sai->pclk)) {
			dev_err(&pdev->dev, "missing bus clock pclk\n");
			return PTR_ERR(sai->pclk);
		}
	}

	sai->clk_x8k = devm_clk_get(&pdev->dev, "x8k");
	if (IS_ERR(sai->clk_x8k)) {
		dev_err(&pdev->dev, "missing x8k parent clock\n");
		return PTR_ERR(sai->clk_x8k);
	}

	sai->clk_x11k = devm_clk_get(&pdev->dev, "x11k");
	if (IS_ERR(sai->clk_x11k)) {
		dev_err(&pdev->dev, "missing x11k parent clock\n");
		return PTR_ERR(sai->clk_x11k);
	}

	/* init irqs */
	sai->irq = platform_get_irq(pdev, 0);
	if (sai->irq < 0) {
		dev_err(&pdev->dev, "no irq for node %s\n", pdev->name);
		return sai->irq;
	}

	/* reset */
	rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (!IS_ERR(rst)) {
		reset_control_assert(rst);
		udelay(2);
		reset_control_deassert(rst);
	}

	sai->pdev = pdev;
	sai->set_sync = &stm32_sai_set_sync;
	platform_set_drvdata(pdev, sai);

	return devm_of_platform_populate(&pdev->dev);
}

#ifdef CONFIG_PM_SLEEP
/*
 * When pins are shared by two sai sub instances, pins have to be defined
 * in sai parent node. In this case, pins state is not managed by alsa fw.
 * These pins are managed in suspend/resume callbacks.
 */
static int stm32_sai_suspend(struct device *dev)
{
	struct stm32_sai_data *sai = dev_get_drvdata(dev);
	int ret;

	ret = stm32_sai_pclk_enable(dev);
	if (ret)
		return ret;

	sai->gcr = readl_relaxed(sai->base);
	stm32_sai_pclk_disable(dev);

	return pinctrl_pm_select_sleep_state(dev);
}

static int stm32_sai_resume(struct device *dev)
{
	struct stm32_sai_data *sai = dev_get_drvdata(dev);
	int ret;

	ret = stm32_sai_pclk_enable(dev);
	if (ret)
		return ret;

	writel_relaxed(sai->gcr, sai->base);
	stm32_sai_pclk_disable(dev);

	return pinctrl_pm_select_default_state(dev);
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops stm32_sai_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(stm32_sai_suspend, stm32_sai_resume)
};

MODULE_DEVICE_TABLE(of, stm32_sai_ids);

static struct platform_driver stm32_sai_driver = {
	.driver = {
		.name = "st,stm32-sai",
		.of_match_table = stm32_sai_ids,
		.pm = &stm32_sai_pm_ops,
	},
	.probe = stm32_sai_probe,
};

module_platform_driver(stm32_sai_driver);

MODULE_DESCRIPTION("STM32 Soc SAI Interface");
MODULE_AUTHOR("Olivier Moysan <olivier.moysan@st.com>");
MODULE_ALIAS("platform:st,stm32-sai");
MODULE_LICENSE("GPL v2");
