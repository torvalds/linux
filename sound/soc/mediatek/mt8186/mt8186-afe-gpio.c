// SPDX-License-Identifier: GPL-2.0
//
// mt8186-afe-gpio.c  --  Mediatek 8186 afe gpio ctrl
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Jiaxin Yu <jiaxin.yu@mediatek.com>

#include <linux/gpio.h>
#include <linux/pinctrl/consumer.h>

#include "mt8186-afe-common.h"
#include "mt8186-afe-gpio.h"

static struct pinctrl *aud_pinctrl;

enum mt8186_afe_gpio {
	MT8186_AFE_GPIO_CLK_MOSI_OFF,
	MT8186_AFE_GPIO_CLK_MOSI_ON,
	MT8186_AFE_GPIO_CLK_MISO_OFF,
	MT8186_AFE_GPIO_CLK_MISO_ON,
	MT8186_AFE_GPIO_DAT_MISO_OFF,
	MT8186_AFE_GPIO_DAT_MISO_ON,
	MT8186_AFE_GPIO_DAT_MOSI_OFF,
	MT8186_AFE_GPIO_DAT_MOSI_ON,
	MT8186_AFE_GPIO_I2S0_OFF,
	MT8186_AFE_GPIO_I2S0_ON,
	MT8186_AFE_GPIO_I2S1_OFF,
	MT8186_AFE_GPIO_I2S1_ON,
	MT8186_AFE_GPIO_I2S2_OFF,
	MT8186_AFE_GPIO_I2S2_ON,
	MT8186_AFE_GPIO_I2S3_OFF,
	MT8186_AFE_GPIO_I2S3_ON,
	MT8186_AFE_GPIO_TDM_OFF,
	MT8186_AFE_GPIO_TDM_ON,
	MT8186_AFE_GPIO_PCM_OFF,
	MT8186_AFE_GPIO_PCM_ON,
	MT8186_AFE_GPIO_GPIO_NUM
};

struct audio_gpio_attr {
	const char *name;
	bool gpio_prepare;
	struct pinctrl_state *gpioctrl;
};

static struct audio_gpio_attr aud_gpios[MT8186_AFE_GPIO_GPIO_NUM] = {
	[MT8186_AFE_GPIO_CLK_MOSI_OFF] = {"aud_clk_mosi_off", false, NULL},
	[MT8186_AFE_GPIO_CLK_MOSI_ON] = {"aud_clk_mosi_on", false, NULL},
	[MT8186_AFE_GPIO_CLK_MISO_OFF] = {"aud_clk_miso_off", false, NULL},
	[MT8186_AFE_GPIO_CLK_MISO_ON] = {"aud_clk_miso_on", false, NULL},
	[MT8186_AFE_GPIO_DAT_MISO_OFF] = {"aud_dat_miso_off", false, NULL},
	[MT8186_AFE_GPIO_DAT_MISO_ON] = {"aud_dat_miso_on", false, NULL},
	[MT8186_AFE_GPIO_DAT_MOSI_OFF] = {"aud_dat_mosi_off", false, NULL},
	[MT8186_AFE_GPIO_DAT_MOSI_ON] = {"aud_dat_mosi_on", false, NULL},
	[MT8186_AFE_GPIO_I2S0_OFF] = {"aud_gpio_i2s0_off", false, NULL},
	[MT8186_AFE_GPIO_I2S0_ON] = {"aud_gpio_i2s0_on", false, NULL},
	[MT8186_AFE_GPIO_I2S1_OFF] = {"aud_gpio_i2s1_off", false, NULL},
	[MT8186_AFE_GPIO_I2S1_ON] = {"aud_gpio_i2s1_on", false, NULL},
	[MT8186_AFE_GPIO_I2S2_OFF] = {"aud_gpio_i2s2_off", false, NULL},
	[MT8186_AFE_GPIO_I2S2_ON] = {"aud_gpio_i2s2_on", false, NULL},
	[MT8186_AFE_GPIO_I2S3_OFF] = {"aud_gpio_i2s3_off", false, NULL},
	[MT8186_AFE_GPIO_I2S3_ON] = {"aud_gpio_i2s3_on", false, NULL},
	[MT8186_AFE_GPIO_TDM_OFF] = {"aud_gpio_tdm_off", false, NULL},
	[MT8186_AFE_GPIO_TDM_ON] = {"aud_gpio_tdm_on", false, NULL},
	[MT8186_AFE_GPIO_PCM_OFF] = {"aud_gpio_pcm_off", false, NULL},
	[MT8186_AFE_GPIO_PCM_ON] = {"aud_gpio_pcm_on", false, NULL},
};

static DEFINE_MUTEX(gpio_request_mutex);

int mt8186_afe_gpio_init(struct device *dev)
{
	int i, j, ret;

	aud_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(aud_pinctrl)) {
		ret = PTR_ERR(aud_pinctrl);
		dev_err(dev, "%s(), ret %d, cannot get aud_pinctrl!\n",
			__func__, ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(aud_gpios); i++) {
		aud_gpios[i].gpioctrl = pinctrl_lookup_state(aud_pinctrl,
							     aud_gpios[i].name);
		if (IS_ERR(aud_gpios[i].gpioctrl)) {
			ret = PTR_ERR(aud_gpios[i].gpioctrl);
			dev_dbg(dev, "%s(), pinctrl_lookup_state %s fail, ret %d\n",
				 __func__, aud_gpios[i].name, ret);
		} else {
			aud_gpios[i].gpio_prepare = true;
		}
	}

	/* gpio status init */
	for (i = MT8186_DAI_ADDA; i <= MT8186_DAI_TDM_IN; i++) {
		for (j = 0; j <= 1; j++)
			mt8186_afe_gpio_request(dev, false, i, j);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt8186_afe_gpio_init);

static int mt8186_afe_gpio_select(struct device *dev,
				  enum mt8186_afe_gpio type)
{
	int ret = 0;

	if (type < 0 || type >= MT8186_AFE_GPIO_GPIO_NUM) {
		dev_dbg(dev, "%s(), error, invalid gpio type %d\n",
			__func__, type);
		return -EINVAL;
	}

	if (!aud_gpios[type].gpio_prepare) {
		dev_dbg(dev, "%s(), error, gpio type %d not prepared\n",
			__func__, type);
		return -EIO;
	}

	ret = pinctrl_select_state(aud_pinctrl,
				   aud_gpios[type].gpioctrl);
	if (ret) {
		dev_dbg(dev, "%s(), error, can not set gpio type %d\n",
			__func__, type);
		return ret;
	}

	return 0;
}

static int mt8186_afe_gpio_adda_dl(struct device *dev, bool enable)
{
	int ret;

	if (enable) {
		ret = mt8186_afe_gpio_select(dev, MT8186_AFE_GPIO_CLK_MOSI_ON);
		if (ret) {
			dev_dbg(dev, "%s(), MOSI CLK ON select fail!\n", __func__);
			return ret;
		}

		ret = mt8186_afe_gpio_select(dev, MT8186_AFE_GPIO_DAT_MOSI_ON);
		if (ret) {
			dev_dbg(dev, "%s(), MOSI DAT ON select fail!\n", __func__);
			return ret;
		}
	} else {
		ret = mt8186_afe_gpio_select(dev, MT8186_AFE_GPIO_DAT_MOSI_OFF);
		if (ret) {
			dev_dbg(dev, "%s(), MOSI DAT OFF select fail!\n", __func__);
			return ret;
		}

		ret = mt8186_afe_gpio_select(dev, MT8186_AFE_GPIO_CLK_MOSI_OFF);
		if (ret) {
			dev_dbg(dev, "%s(), MOSI CLK ON select fail!\n", __func__);
			return ret;
		}
	}

	return 0;
}

static int mt8186_afe_gpio_adda_ul(struct device *dev, bool enable)
{
	int ret;

	if (enable) {
		ret = mt8186_afe_gpio_select(dev, MT8186_AFE_GPIO_CLK_MISO_ON);
		if (ret) {
			dev_dbg(dev, "%s(), MISO CLK ON select fail!\n", __func__);
			return ret;
		}

		ret = mt8186_afe_gpio_select(dev, MT8186_AFE_GPIO_DAT_MISO_ON);
		if (ret) {
			dev_dbg(dev, "%s(), MISO DAT ON select fail!\n", __func__);
			return ret;
		}
	} else {
		ret = mt8186_afe_gpio_select(dev, MT8186_AFE_GPIO_DAT_MISO_OFF);
		if (ret) {
			dev_dbg(dev, "%s(), MISO DAT OFF select fail!\n", __func__);
			return ret;
		}

		ret = mt8186_afe_gpio_select(dev, MT8186_AFE_GPIO_CLK_MISO_OFF);
		if (ret) {
			dev_dbg(dev, "%s(), MISO CLK OFF select fail!\n", __func__);
			return ret;
		}
	}

	return 0;
}

int mt8186_afe_gpio_request(struct device *dev, bool enable,
			    int dai, int uplink)
{
	enum mt8186_afe_gpio sel;
	int ret = -EINVAL;

	mutex_lock(&gpio_request_mutex);

	switch (dai) {
	case MT8186_DAI_ADDA:
		if (uplink)
			ret = mt8186_afe_gpio_adda_ul(dev, enable);
		else
			ret = mt8186_afe_gpio_adda_dl(dev, enable);
		goto unlock;
	case MT8186_DAI_I2S_0:
		sel = enable ? MT8186_AFE_GPIO_I2S0_ON : MT8186_AFE_GPIO_I2S0_OFF;
		break;
	case MT8186_DAI_I2S_1:
		sel = enable ? MT8186_AFE_GPIO_I2S1_ON : MT8186_AFE_GPIO_I2S1_OFF;
		break;
	case MT8186_DAI_I2S_2:
		sel = enable ? MT8186_AFE_GPIO_I2S2_ON : MT8186_AFE_GPIO_I2S2_OFF;
		break;
	case MT8186_DAI_I2S_3:
		sel = enable ? MT8186_AFE_GPIO_I2S3_ON : MT8186_AFE_GPIO_I2S3_OFF;
		break;
	case MT8186_DAI_TDM_IN:
		sel = enable ? MT8186_AFE_GPIO_TDM_ON : MT8186_AFE_GPIO_TDM_OFF;
		break;
	case MT8186_DAI_PCM:
		sel = enable ? MT8186_AFE_GPIO_PCM_ON : MT8186_AFE_GPIO_PCM_OFF;
		break;
	default:
		dev_dbg(dev, "%s(), invalid dai %d\n", __func__, dai);
		goto unlock;
	}

	ret = mt8186_afe_gpio_select(dev, sel);

unlock:
	mutex_unlock(&gpio_request_mutex);

	return ret;
}
