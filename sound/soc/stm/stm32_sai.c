// SPDX-License-Identifier: GPL-2.0-only
/*
 * STM32 ALSA SoC Digital Audio Interface (SAI) driver.
 *
 * Copyright (C) 2016, STMicroelectronics - All Rights Reserved
 * Author(s): Olivier Moysan <olivier.moysan@st.com> for STMicroelectronics.
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
	.version = STM_SAI_STM32F4,
	.fifo_size = 8,
	.has_spdif_pdm = false,
};

/*
 * Default settings for stm32 H7 socs and next.
 * These default settings will be overridden if the soc provides
 * support of hardware configuration registers.
 */
static const struct stm32_sai_conf stm32_sai_conf_h7 = {
	.version = STM_SAI_STM32H7,
	.fifo_size = 8,
	.has_spdif_pdm = true,
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
	const struct of_device_id *of_id;
	u32 val;
	int ret;

	sai = devm_kzalloc(&pdev->dev, sizeof(*sai), GFP_KERNEL);
	if (!sai)
		return -ENOMEM;

	sai->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sai->base))
		return PTR_ERR(sai->base);

	of_id = of_match_device(stm32_sai_ids, &pdev->dev);
	if (of_id)
		memcpy(&sai->conf, (const struct stm32_sai_conf *)of_id->data,
		       sizeof(struct stm32_sai_conf));
	else
		return -EINVAL;

	if (!STM_SAI_IS_F4(sai)) {
		sai->pclk = devm_clk_get(&pdev->dev, "pclk");
		if (IS_ERR(sai->pclk)) {
			if (PTR_ERR(sai->pclk) != -EPROBE_DEFER)
				dev_err(&pdev->dev, "missing bus clock pclk: %ld\n",
					PTR_ERR(sai->pclk));
			return PTR_ERR(sai->pclk);
		}
	}

	sai->clk_x8k = devm_clk_get(&pdev->dev, "x8k");
	if (IS_ERR(sai->clk_x8k)) {
		if (PTR_ERR(sai->clk_x8k) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "missing x8k parent clock: %ld\n",
				PTR_ERR(sai->clk_x8k));
		return PTR_ERR(sai->clk_x8k);
	}

	sai->clk_x11k = devm_clk_get(&pdev->dev, "x11k");
	if (IS_ERR(sai->clk_x11k)) {
		if (PTR_ERR(sai->clk_x11k) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "missing x11k parent clock: %ld\n",
				PTR_ERR(sai->clk_x11k));
		return PTR_ERR(sai->clk_x11k);
	}

	/* init irqs */
	sai->irq = platform_get_irq(pdev, 0);
	if (sai->irq < 0)
		return sai->irq;

	/* reset */
	rst = devm_reset_control_get_optional_exclusive(&pdev->dev, NULL);
	if (IS_ERR(rst)) {
		if (PTR_ERR(rst) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Reset controller error %ld\n",
				PTR_ERR(rst));
		return PTR_ERR(rst);
	}
	reset_control_assert(rst);
	udelay(2);
	reset_control_deassert(rst);

	/* Enable peripheral clock to allow register access */
	ret = clk_prepare_enable(sai->pclk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clock: %d\n", ret);
		return ret;
	}

	val = FIELD_GET(SAI_IDR_ID_MASK,
			readl_relaxed(sai->base + STM_SAI_IDR));
	if (val == SAI_IPIDR_NUMBER) {
		val = readl_relaxed(sai->base + STM_SAI_HWCFGR);
		sai->conf.fifo_size = FIELD_GET(SAI_HWCFGR_FIFO_SIZE, val);
		sai->conf.has_spdif_pdm = !!FIELD_GET(SAI_HWCFGR_SPDIF_PDM,
						      val);

		val = readl_relaxed(sai->base + STM_SAI_VERR);
		sai->conf.version = val;

		dev_dbg(&pdev->dev, "SAI version: %lu.%lu registered\n",
			FIELD_GET(SAI_VERR_MAJ_MASK, val),
			FIELD_GET(SAI_VERR_MIN_MASK, val));
	}
	clk_disable_unprepare(sai->pclk);

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
