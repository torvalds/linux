// SPDX-License-Identifier: GPL-2.0-only
//
// rt5682.c  --  RT5682 ALSA SoC audio component driver
//
// Copyright 2018 Realtek Semiconductor Corp.
// Author: Bard Liao <bardliao@realtek.com>
//

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/acpi.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/rt5682.h>

#include "rl6231.h"
#include "rt5682.h"

static const struct rt5682_platform_data i2s_default_platform_data = {
	.dmic1_data_pin = RT5682_DMIC1_DATA_GPIO2,
	.dmic1_clk_pin = RT5682_DMIC1_CLK_GPIO3,
	.jd_src = RT5682_JD1,
	.btndet_delay = 16,
	.dai_clk_names[RT5682_DAI_WCLK_IDX] = "rt5682-dai-wclk",
	.dai_clk_names[RT5682_DAI_BCLK_IDX] = "rt5682-dai-bclk",
};

static const struct regmap_config rt5682_regmap = {
	.reg_bits = 16,
	.val_bits = 16,
	.max_register = RT5682_I2C_MODE,
	.volatile_reg = rt5682_volatile_register,
	.readable_reg = rt5682_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5682_reg,
	.num_reg_defaults = RT5682_REG_NUM,
	.use_single_read = true,
	.use_single_write = true,
};

static void rt5682_jd_check_handler(struct work_struct *work)
{
	struct rt5682_priv *rt5682 = container_of(work, struct rt5682_priv,
		jd_check_work.work);

	if (snd_soc_component_read(rt5682->component, RT5682_AJD1_CTRL) & RT5682_JDH_RS_MASK)
		/* jack out */
		mod_delayed_work(system_power_efficient_wq,
				 &rt5682->jack_detect_work, 0);
	else
		schedule_delayed_work(&rt5682->jd_check_work, 500);
}

static irqreturn_t rt5682_irq(int irq, void *data)
{
	struct rt5682_priv *rt5682 = data;

	mod_delayed_work(system_power_efficient_wq,
		&rt5682->jack_detect_work, msecs_to_jiffies(rt5682->irq_work_delay_time));

	return IRQ_HANDLED;
}

static struct snd_soc_dai_driver rt5682_dai[] = {
	{
		.name = "rt5682-aif1",
		.id = RT5682_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5682_STEREO_RATES,
			.formats = RT5682_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5682_STEREO_RATES,
			.formats = RT5682_FORMATS,
		},
		.ops = &rt5682_aif1_dai_ops,
	},
	{
		.name = "rt5682-aif2",
		.id = RT5682_AIF2,
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5682_STEREO_RATES,
			.formats = RT5682_FORMATS,
		},
		.ops = &rt5682_aif2_dai_ops,
	},
};

static void rt5682_i2c_disable_regulators(void *data)
{
	struct rt5682_priv *rt5682 = data;

	regulator_bulk_disable(ARRAY_SIZE(rt5682->supplies), rt5682->supplies);
}

static int rt5682_i2c_probe(struct i2c_client *i2c)
{
	struct rt5682_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt5682_priv *rt5682;
	int i, ret;
	unsigned int val;

	rt5682 = devm_kzalloc(&i2c->dev, sizeof(struct rt5682_priv),
		GFP_KERNEL);
	if (!rt5682)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5682);

	rt5682->i2c_dev = &i2c->dev;

	rt5682->pdata = i2s_default_platform_data;

	if (pdata)
		rt5682->pdata = *pdata;
	else
		rt5682_parse_dt(rt5682, &i2c->dev);

	rt5682->regmap = devm_regmap_init_i2c(i2c, &rt5682_regmap);
	if (IS_ERR(rt5682->regmap)) {
		ret = PTR_ERR(rt5682->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(rt5682->supplies); i++)
		rt5682->supplies[i].supply = rt5682_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev, ARRAY_SIZE(rt5682->supplies),
				      rt5682->supplies);
	if (ret) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(&i2c->dev, rt5682_i2c_disable_regulators,
				       rt5682);
	if (ret)
		return ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(rt5682->supplies),
				    rt5682->supplies);
	if (ret) {
		dev_err(&i2c->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	if (gpio_is_valid(rt5682->pdata.ldo1_en)) {
		if (devm_gpio_request_one(&i2c->dev, rt5682->pdata.ldo1_en,
					  GPIOF_OUT_INIT_HIGH, "rt5682"))
			dev_err(&i2c->dev, "Fail gpio_request gpio_ldo\n");
	}

	/* Sleep for 300 ms miniumum */
	usleep_range(300000, 350000);

	regmap_write(rt5682->regmap, RT5682_I2C_MODE, 0x1);
	usleep_range(10000, 15000);

	regmap_read(rt5682->regmap, RT5682_DEVICE_ID, &val);
	if (val != DEVICE_ID) {
		dev_err(&i2c->dev,
			"Device with ID register %x is not rt5682\n", val);
		return -ENODEV;
	}

	mutex_init(&rt5682->calibrate_mutex);
	rt5682_calibrate(rt5682);

	rt5682_apply_patch_list(rt5682, &i2c->dev);

	regmap_write(rt5682->regmap, RT5682_DEPOP_1, 0x0000);

	/* DMIC pin*/
	if (rt5682->pdata.dmic1_data_pin != RT5682_DMIC1_NULL) {
		switch (rt5682->pdata.dmic1_data_pin) {
		case RT5682_DMIC1_DATA_GPIO2: /* share with LRCK2 */
			regmap_update_bits(rt5682->regmap, RT5682_DMIC_CTRL_1,
				RT5682_DMIC_1_DP_MASK, RT5682_DMIC_1_DP_GPIO2);
			regmap_update_bits(rt5682->regmap, RT5682_GPIO_CTRL_1,
				RT5682_GP2_PIN_MASK, RT5682_GP2_PIN_DMIC_SDA);
			break;

		case RT5682_DMIC1_DATA_GPIO5: /* share with DACDAT1 */
			regmap_update_bits(rt5682->regmap, RT5682_DMIC_CTRL_1,
				RT5682_DMIC_1_DP_MASK, RT5682_DMIC_1_DP_GPIO5);
			regmap_update_bits(rt5682->regmap, RT5682_GPIO_CTRL_1,
				RT5682_GP5_PIN_MASK, RT5682_GP5_PIN_DMIC_SDA);
			break;

		default:
			dev_warn(&i2c->dev, "invalid DMIC_DAT pin\n");
			break;
		}

		switch (rt5682->pdata.dmic1_clk_pin) {
		case RT5682_DMIC1_CLK_GPIO1: /* share with IRQ */
			regmap_update_bits(rt5682->regmap, RT5682_GPIO_CTRL_1,
				RT5682_GP1_PIN_MASK, RT5682_GP1_PIN_DMIC_CLK);
			break;

		case RT5682_DMIC1_CLK_GPIO3: /* share with BCLK2 */
			regmap_update_bits(rt5682->regmap, RT5682_GPIO_CTRL_1,
				RT5682_GP3_PIN_MASK, RT5682_GP3_PIN_DMIC_CLK);
			if (rt5682->pdata.dmic_clk_driving_high)
				regmap_update_bits(rt5682->regmap,
					RT5682_PAD_DRIVING_CTRL,
					RT5682_PAD_DRV_GP3_MASK,
					2 << RT5682_PAD_DRV_GP3_SFT);
			break;

		default:
			dev_warn(&i2c->dev, "invalid DMIC_CLK pin\n");
			break;
		}
	}

	regmap_update_bits(rt5682->regmap, RT5682_PWR_ANLG_1,
		RT5682_LDO1_DVO_MASK | RT5682_HP_DRIVER_MASK,
		RT5682_LDO1_DVO_12 | RT5682_HP_DRIVER_5X);
	regmap_write(rt5682->regmap, RT5682_MICBIAS_2, 0x0080);
	regmap_update_bits(rt5682->regmap, RT5682_GPIO_CTRL_1,
		RT5682_GP4_PIN_MASK | RT5682_GP5_PIN_MASK,
		RT5682_GP4_PIN_ADCDAT1 | RT5682_GP5_PIN_DACDAT1);
	regmap_write(rt5682->regmap, RT5682_TEST_MODE_CTRL_1, 0x0000);
	regmap_update_bits(rt5682->regmap, RT5682_BIAS_CUR_CTRL_8,
		RT5682_HPA_CP_BIAS_CTRL_MASK, RT5682_HPA_CP_BIAS_3UA);
	regmap_update_bits(rt5682->regmap, RT5682_CHARGE_PUMP_1,
		RT5682_CP_CLK_HP_MASK, RT5682_CP_CLK_HP_300KHZ);
	regmap_update_bits(rt5682->regmap, RT5682_HP_CHARGE_PUMP_1,
		RT5682_PM_HP_MASK, RT5682_PM_HP_HV);
	regmap_update_bits(rt5682->regmap, RT5682_DMIC_CTRL_1,
		RT5682_FIFO_CLK_DIV_MASK, RT5682_FIFO_CLK_DIV_2);

	INIT_DELAYED_WORK(&rt5682->jack_detect_work,
		rt5682_jack_detect_handler);
	INIT_DELAYED_WORK(&rt5682->jd_check_work,
		rt5682_jd_check_handler);

	if (i2c->irq) {
		ret = devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL,
			rt5682_irq, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_ONESHOT, "rt5682", rt5682);
		if (!ret)
			rt5682->irq = i2c->irq;
		else
			dev_err(&i2c->dev, "Failed to reguest IRQ: %d\n", ret);
	}

#ifdef CONFIG_COMMON_CLK
	/* Check if MCLK provided */
	rt5682->mclk = devm_clk_get_optional(&i2c->dev, "mclk");
	if (IS_ERR(rt5682->mclk))
		return PTR_ERR(rt5682->mclk);

	/* Register CCF DAI clock control */
	ret = rt5682_register_dai_clks(rt5682);
	if (ret)
		return ret;

	/* Initial setup for CCF */
	rt5682->lrck[RT5682_AIF1] = 48000;
#endif

	return devm_snd_soc_register_component(&i2c->dev,
					       &rt5682_soc_component_dev,
					       rt5682_dai, ARRAY_SIZE(rt5682_dai));
}

static void rt5682_i2c_shutdown(struct i2c_client *client)
{
	struct rt5682_priv *rt5682 = i2c_get_clientdata(client);

	disable_irq(client->irq);
	cancel_delayed_work_sync(&rt5682->jack_detect_work);
	cancel_delayed_work_sync(&rt5682->jd_check_work);

	rt5682_reset(rt5682);
}

static void rt5682_i2c_remove(struct i2c_client *client)
{
	rt5682_i2c_shutdown(client);
}

static const struct of_device_id rt5682_of_match[] = {
	{.compatible = "realtek,rt5682i"},
	{},
};
MODULE_DEVICE_TABLE(of, rt5682_of_match);

static const struct acpi_device_id rt5682_acpi_match[] = {
	{"10EC5682", 0,},
	{},
};
MODULE_DEVICE_TABLE(acpi, rt5682_acpi_match);

static const struct i2c_device_id rt5682_i2c_id[] = {
	{"rt5682", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, rt5682_i2c_id);

static struct i2c_driver rt5682_i2c_driver = {
	.driver = {
		.name = "rt5682",
		.of_match_table = rt5682_of_match,
		.acpi_match_table = rt5682_acpi_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe_new = rt5682_i2c_probe,
	.remove = rt5682_i2c_remove,
	.shutdown = rt5682_i2c_shutdown,
	.id_table = rt5682_i2c_id,
};
module_i2c_driver(rt5682_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5682 driver");
MODULE_AUTHOR("Bard Liao <bardliao@realtek.com>");
MODULE_LICENSE("GPL v2");
