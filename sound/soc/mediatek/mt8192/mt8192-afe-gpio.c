// SPDX-License-Identifier: GPL-2.0
//
// mt8192-afe-gpio.c  --  Mediatek 8192 afe gpio ctrl
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Shane Chien <shane.chien@mediatek.com>
//

#include <linux/pinctrl/consumer.h>

#include "mt8192-afe-common.h"
#include "mt8192-afe-gpio.h"

static struct pinctrl *aud_pinctrl;

enum mt8192_afe_gpio {
	MT8192_AFE_GPIO_DAT_MISO_OFF,
	MT8192_AFE_GPIO_DAT_MISO_ON,
	MT8192_AFE_GPIO_DAT_MOSI_OFF,
	MT8192_AFE_GPIO_DAT_MOSI_ON,
	MT8192_AFE_GPIO_DAT_MISO_CH34_OFF,
	MT8192_AFE_GPIO_DAT_MISO_CH34_ON,
	MT8192_AFE_GPIO_DAT_MOSI_CH34_OFF,
	MT8192_AFE_GPIO_DAT_MOSI_CH34_ON,
	MT8192_AFE_GPIO_I2S0_OFF,
	MT8192_AFE_GPIO_I2S0_ON,
	MT8192_AFE_GPIO_I2S1_OFF,
	MT8192_AFE_GPIO_I2S1_ON,
	MT8192_AFE_GPIO_I2S2_OFF,
	MT8192_AFE_GPIO_I2S2_ON,
	MT8192_AFE_GPIO_I2S3_OFF,
	MT8192_AFE_GPIO_I2S3_ON,
	MT8192_AFE_GPIO_I2S5_OFF,
	MT8192_AFE_GPIO_I2S5_ON,
	MT8192_AFE_GPIO_I2S6_OFF,
	MT8192_AFE_GPIO_I2S6_ON,
	MT8192_AFE_GPIO_I2S7_OFF,
	MT8192_AFE_GPIO_I2S7_ON,
	MT8192_AFE_GPIO_I2S8_OFF,
	MT8192_AFE_GPIO_I2S8_ON,
	MT8192_AFE_GPIO_I2S9_OFF,
	MT8192_AFE_GPIO_I2S9_ON,
	MT8192_AFE_GPIO_VOW_DAT_OFF,
	MT8192_AFE_GPIO_VOW_DAT_ON,
	MT8192_AFE_GPIO_VOW_CLK_OFF,
	MT8192_AFE_GPIO_VOW_CLK_ON,
	MT8192_AFE_GPIO_CLK_MOSI_OFF,
	MT8192_AFE_GPIO_CLK_MOSI_ON,
	MT8192_AFE_GPIO_TDM_OFF,
	MT8192_AFE_GPIO_TDM_ON,
	MT8192_AFE_GPIO_GPIO_NUM
};

struct audio_gpio_attr {
	const char *name;
	bool gpio_prepare;
	struct pinctrl_state *gpioctrl;
};

static struct audio_gpio_attr aud_gpios[MT8192_AFE_GPIO_GPIO_NUM] = {
	[MT8192_AFE_GPIO_DAT_MISO_OFF] = {"aud_dat_miso_off", false, NULL},
	[MT8192_AFE_GPIO_DAT_MISO_ON] = {"aud_dat_miso_on", false, NULL},
	[MT8192_AFE_GPIO_DAT_MOSI_OFF] = {"aud_dat_mosi_off", false, NULL},
	[MT8192_AFE_GPIO_DAT_MOSI_ON] = {"aud_dat_mosi_on", false, NULL},
	[MT8192_AFE_GPIO_I2S0_OFF] = {"aud_gpio_i2s0_off", false, NULL},
	[MT8192_AFE_GPIO_I2S0_ON] = {"aud_gpio_i2s0_on", false, NULL},
	[MT8192_AFE_GPIO_I2S1_OFF] = {"aud_gpio_i2s1_off", false, NULL},
	[MT8192_AFE_GPIO_I2S1_ON] = {"aud_gpio_i2s1_on", false, NULL},
	[MT8192_AFE_GPIO_I2S2_OFF] = {"aud_gpio_i2s2_off", false, NULL},
	[MT8192_AFE_GPIO_I2S2_ON] = {"aud_gpio_i2s2_on", false, NULL},
	[MT8192_AFE_GPIO_I2S3_OFF] = {"aud_gpio_i2s3_off", false, NULL},
	[MT8192_AFE_GPIO_I2S3_ON] = {"aud_gpio_i2s3_on", false, NULL},
	[MT8192_AFE_GPIO_I2S5_OFF] = {"aud_gpio_i2s5_off", false, NULL},
	[MT8192_AFE_GPIO_I2S5_ON] = {"aud_gpio_i2s5_on", false, NULL},
	[MT8192_AFE_GPIO_I2S6_OFF] = {"aud_gpio_i2s6_off", false, NULL},
	[MT8192_AFE_GPIO_I2S6_ON] = {"aud_gpio_i2s6_on", false, NULL},
	[MT8192_AFE_GPIO_I2S7_OFF] = {"aud_gpio_i2s7_off", false, NULL},
	[MT8192_AFE_GPIO_I2S7_ON] = {"aud_gpio_i2s7_on", false, NULL},
	[MT8192_AFE_GPIO_I2S8_OFF] = {"aud_gpio_i2s8_off", false, NULL},
	[MT8192_AFE_GPIO_I2S8_ON] = {"aud_gpio_i2s8_on", false, NULL},
	[MT8192_AFE_GPIO_I2S9_OFF] = {"aud_gpio_i2s9_off", false, NULL},
	[MT8192_AFE_GPIO_I2S9_ON] = {"aud_gpio_i2s9_on", false, NULL},
	[MT8192_AFE_GPIO_TDM_OFF] = {"aud_gpio_tdm_off", false, NULL},
	[MT8192_AFE_GPIO_TDM_ON] = {"aud_gpio_tdm_on", false, NULL},
	[MT8192_AFE_GPIO_VOW_DAT_OFF] = {"vow_dat_miso_off", false, NULL},
	[MT8192_AFE_GPIO_VOW_DAT_ON] = {"vow_dat_miso_on", false, NULL},
	[MT8192_AFE_GPIO_VOW_CLK_OFF] = {"vow_clk_miso_off", false, NULL},
	[MT8192_AFE_GPIO_VOW_CLK_ON] = {"vow_clk_miso_on", false, NULL},
	[MT8192_AFE_GPIO_DAT_MISO_CH34_OFF] = {"aud_dat_miso_ch34_off",
					       false, NULL},
	[MT8192_AFE_GPIO_DAT_MISO_CH34_ON] = {"aud_dat_miso_ch34_on",
					      false, NULL},
	[MT8192_AFE_GPIO_DAT_MOSI_CH34_OFF] = {"aud_dat_mosi_ch34_off",
					       false, NULL},
	[MT8192_AFE_GPIO_DAT_MOSI_CH34_ON] = {"aud_dat_mosi_ch34_on",
					      false, NULL},
	[MT8192_AFE_GPIO_CLK_MOSI_OFF] = {"aud_clk_mosi_off", false, NULL},
	[MT8192_AFE_GPIO_CLK_MOSI_ON] = {"aud_clk_mosi_on", false, NULL},
};

static DEFINE_MUTEX(gpio_request_mutex);

static int mt8192_afe_gpio_select(struct device *dev,
				  enum mt8192_afe_gpio type)
{
	int ret;

	if (type < 0 || type >= MT8192_AFE_GPIO_GPIO_NUM) {
		dev_err(dev, "%s(), error, invalid gpio type %d\n",
			__func__, type);
		return -EINVAL;
	}

	if (!aud_gpios[type].gpio_prepare) {
		dev_warn(dev, "%s(), error, gpio type %d not prepared\n",
			 __func__, type);
		return -EIO;
	}

	ret = pinctrl_select_state(aud_pinctrl,
				   aud_gpios[type].gpioctrl);
	if (ret) {
		dev_dbg(dev, "%s(), error, can not set gpio type %d\n",
			__func__, type);
	}

	return ret;
}

int mt8192_afe_gpio_init(struct device *dev)
{
	int i, ret;

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

	mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_CLK_MOSI_ON);

	/* gpio status init */
	mt8192_afe_gpio_request(dev, false, MT8192_DAI_ADDA, 0);
	mt8192_afe_gpio_request(dev, false, MT8192_DAI_ADDA, 1);

	return 0;
}
EXPORT_SYMBOL(mt8192_afe_gpio_init);

static int mt8192_afe_gpio_adda_dl(struct device *dev, bool enable)
{
	if (enable) {
		return mt8192_afe_gpio_select(dev,
					      MT8192_AFE_GPIO_DAT_MOSI_ON);
	} else {
		return mt8192_afe_gpio_select(dev,
					      MT8192_AFE_GPIO_DAT_MOSI_OFF);
	}
}

static int mt8192_afe_gpio_adda_ul(struct device *dev, bool enable)
{
	if (enable) {
		return mt8192_afe_gpio_select(dev,
					      MT8192_AFE_GPIO_DAT_MISO_ON);
	} else {
		return mt8192_afe_gpio_select(dev,
					      MT8192_AFE_GPIO_DAT_MISO_OFF);
	}
}

static int mt8192_afe_gpio_adda_ch34_dl(struct device *dev, bool enable)
{
	if (enable) {
		return mt8192_afe_gpio_select(dev,
			MT8192_AFE_GPIO_DAT_MOSI_CH34_ON);
	} else {
		return mt8192_afe_gpio_select(dev,
			MT8192_AFE_GPIO_DAT_MOSI_CH34_OFF);
	}
}

static int mt8192_afe_gpio_adda_ch34_ul(struct device *dev, bool enable)
{
	if (enable) {
		return mt8192_afe_gpio_select(dev,
			MT8192_AFE_GPIO_DAT_MISO_CH34_ON);
	} else {
		return mt8192_afe_gpio_select(dev,
			MT8192_AFE_GPIO_DAT_MISO_CH34_OFF);
	}
}

int mt8192_afe_gpio_request(struct device *dev, bool enable,
			    int dai, int uplink)
{
	mutex_lock(&gpio_request_mutex);
	switch (dai) {
	case MT8192_DAI_ADDA:
		if (uplink)
			mt8192_afe_gpio_adda_ul(dev, enable);
		else
			mt8192_afe_gpio_adda_dl(dev, enable);
		break;
	case MT8192_DAI_ADDA_CH34:
		if (uplink)
			mt8192_afe_gpio_adda_ch34_ul(dev, enable);
		else
			mt8192_afe_gpio_adda_ch34_dl(dev, enable);
		break;
	case MT8192_DAI_I2S_0:
		if (enable)
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S0_ON);
		else
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S0_OFF);
		break;
	case MT8192_DAI_I2S_1:
		if (enable)
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S1_ON);
		else
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S1_OFF);
		break;
	case MT8192_DAI_I2S_2:
		if (enable)
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S2_ON);
		else
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S2_OFF);
		break;
	case MT8192_DAI_I2S_3:
		if (enable)
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S3_ON);
		else
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S3_OFF);
		break;
	case MT8192_DAI_I2S_5:
		if (enable)
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S5_ON);
		else
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S5_OFF);
		break;
	case MT8192_DAI_I2S_6:
		if (enable)
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S6_ON);
		else
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S6_OFF);
		break;
	case MT8192_DAI_I2S_7:
		if (enable)
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S7_ON);
		else
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S7_OFF);
		break;
	case MT8192_DAI_I2S_8:
		if (enable)
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S8_ON);
		else
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S8_OFF);
		break;
	case MT8192_DAI_I2S_9:
		if (enable)
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S9_ON);
		else
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_I2S9_OFF);
		break;
	case MT8192_DAI_TDM:
		if (enable)
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_TDM_ON);
		else
			mt8192_afe_gpio_select(dev, MT8192_AFE_GPIO_TDM_OFF);
		break;
	case MT8192_DAI_VOW:
		if (enable) {
			mt8192_afe_gpio_select(dev,
					       MT8192_AFE_GPIO_VOW_CLK_ON);
			mt8192_afe_gpio_select(dev,
					       MT8192_AFE_GPIO_VOW_DAT_ON);
		} else {
			mt8192_afe_gpio_select(dev,
					       MT8192_AFE_GPIO_VOW_CLK_OFF);
			mt8192_afe_gpio_select(dev,
					       MT8192_AFE_GPIO_VOW_DAT_OFF);
		}
		break;
	default:
		mutex_unlock(&gpio_request_mutex);
		dev_warn(dev, "%s(), invalid dai %d\n", __func__, dai);
		return -EINVAL;
	}
	mutex_unlock(&gpio_request_mutex);

	return 0;
}
EXPORT_SYMBOL(mt8192_afe_gpio_request);
