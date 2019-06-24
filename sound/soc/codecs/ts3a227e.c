// SPDX-License-Identifier: GPL-2.0-only
/*
 * TS3A227E Autonomous Audio Accessory Detection and Configuration Switch
 *
 * Copyright (C) 2014 Google, Inc.
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/acpi.h>

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/soc.h>

#include "ts3a227e.h"

struct ts3a227e {
	struct device *dev;
	struct regmap *regmap;
	struct snd_soc_jack *jack;
	bool plugged;
	bool mic_present;
	unsigned int buttons_held;
	int irq;
};

/* Button values to be reported on the jack */
static const int ts3a227e_buttons[] = {
	SND_JACK_BTN_0,
	SND_JACK_BTN_1,
	SND_JACK_BTN_2,
	SND_JACK_BTN_3,
};

#define TS3A227E_NUM_BUTTONS 4
#define TS3A227E_JACK_MASK (SND_JACK_HEADPHONE | \
			    SND_JACK_MICROPHONE | \
			    SND_JACK_BTN_0 | \
			    SND_JACK_BTN_1 | \
			    SND_JACK_BTN_2 | \
			    SND_JACK_BTN_3)

/* TS3A227E registers */
#define TS3A227E_REG_DEVICE_ID		0x00
#define TS3A227E_REG_INTERRUPT		0x01
#define TS3A227E_REG_KP_INTERRUPT	0x02
#define TS3A227E_REG_INTERRUPT_DISABLE	0x03
#define TS3A227E_REG_SETTING_1		0x04
#define TS3A227E_REG_SETTING_2		0x05
#define TS3A227E_REG_SETTING_3		0x06
#define TS3A227E_REG_SWITCH_CONTROL_1	0x07
#define TS3A227E_REG_SWITCH_CONTROL_2	0x08
#define TS3A227E_REG_SWITCH_STATUS_1	0x09
#define TS3A227E_REG_SWITCH_STATUS_2	0x0a
#define TS3A227E_REG_ACCESSORY_STATUS	0x0b
#define TS3A227E_REG_ADC_OUTPUT		0x0c
#define TS3A227E_REG_KP_THRESHOLD_1	0x0d
#define TS3A227E_REG_KP_THRESHOLD_2	0x0e
#define TS3A227E_REG_KP_THRESHOLD_3	0x0f

/* TS3A227E_REG_INTERRUPT 0x01 */
#define INS_REM_EVENT 0x01
#define DETECTION_COMPLETE_EVENT 0x02

/* TS3A227E_REG_KP_INTERRUPT 0x02 */
#define PRESS_MASK(idx) (0x01 << (2 * (idx)))
#define RELEASE_MASK(idx) (0x02 << (2 * (idx)))

/* TS3A227E_REG_INTERRUPT_DISABLE 0x03 */
#define INS_REM_INT_DISABLE 0x01
#define DETECTION_COMPLETE_INT_DISABLE 0x02
#define ADC_COMPLETE_INT_DISABLE 0x04
#define INTB_DISABLE 0x08

/* TS3A227E_REG_SETTING_2 0x05 */
#define KP_ENABLE 0x04

/* TS3A227E_REG_SETTING_3 0x06 */
#define MICBIAS_SETTING_SFT (3)
#define MICBIAS_SETTING_MASK (0x7 << MICBIAS_SETTING_SFT)

/* TS3A227E_REG_ACCESSORY_STATUS  0x0b */
#define TYPE_3_POLE 0x01
#define TYPE_4_POLE_OMTP 0x02
#define TYPE_4_POLE_STANDARD 0x04
#define JACK_INSERTED 0x08
#define EITHER_MIC_MASK (TYPE_4_POLE_OMTP | TYPE_4_POLE_STANDARD)

static const struct reg_default ts3a227e_reg_defaults[] = {
	{ TS3A227E_REG_DEVICE_ID, 0x10 },
	{ TS3A227E_REG_INTERRUPT, 0x00 },
	{ TS3A227E_REG_KP_INTERRUPT, 0x00 },
	{ TS3A227E_REG_INTERRUPT_DISABLE, 0x08 },
	{ TS3A227E_REG_SETTING_1, 0x23 },
	{ TS3A227E_REG_SETTING_2, 0x00 },
	{ TS3A227E_REG_SETTING_3, 0x0e },
	{ TS3A227E_REG_SWITCH_CONTROL_1, 0x00 },
	{ TS3A227E_REG_SWITCH_CONTROL_2, 0x00 },
	{ TS3A227E_REG_SWITCH_STATUS_1, 0x0c },
	{ TS3A227E_REG_SWITCH_STATUS_2, 0x00 },
	{ TS3A227E_REG_ACCESSORY_STATUS, 0x00 },
	{ TS3A227E_REG_ADC_OUTPUT, 0x00 },
	{ TS3A227E_REG_KP_THRESHOLD_1, 0x20 },
	{ TS3A227E_REG_KP_THRESHOLD_2, 0x40 },
	{ TS3A227E_REG_KP_THRESHOLD_3, 0x68 },
};

static bool ts3a227e_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TS3A227E_REG_DEVICE_ID ... TS3A227E_REG_KP_THRESHOLD_3:
		return true;
	default:
		return false;
	}
}

static bool ts3a227e_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TS3A227E_REG_INTERRUPT_DISABLE ... TS3A227E_REG_SWITCH_CONTROL_2:
	case TS3A227E_REG_KP_THRESHOLD_1 ... TS3A227E_REG_KP_THRESHOLD_3:
		return true;
	default:
		return false;
	}
}

static bool ts3a227e_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TS3A227E_REG_INTERRUPT ... TS3A227E_REG_INTERRUPT_DISABLE:
	case TS3A227E_REG_SETTING_2:
	case TS3A227E_REG_SWITCH_STATUS_1 ... TS3A227E_REG_ADC_OUTPUT:
		return true;
	default:
		return false;
	}
}

static void ts3a227e_jack_report(struct ts3a227e *ts3a227e)
{
	unsigned int i;
	int report = 0;

	if (!ts3a227e->jack)
		return;

	if (ts3a227e->plugged)
		report = SND_JACK_HEADPHONE;
	if (ts3a227e->mic_present)
		report |= SND_JACK_MICROPHONE;
	for (i = 0; i < TS3A227E_NUM_BUTTONS; i++) {
		if (ts3a227e->buttons_held & (1 << i))
			report |= ts3a227e_buttons[i];
	}
	snd_soc_jack_report(ts3a227e->jack, report, TS3A227E_JACK_MASK);
}

static void ts3a227e_new_jack_state(struct ts3a227e *ts3a227e, unsigned acc_reg)
{
	bool plugged, mic_present;

	plugged = !!(acc_reg & JACK_INSERTED);
	mic_present = plugged && !!(acc_reg & EITHER_MIC_MASK);

	ts3a227e->plugged = plugged;

	if (mic_present != ts3a227e->mic_present) {
		ts3a227e->mic_present = mic_present;
		ts3a227e->buttons_held = 0;
		if (mic_present) {
			/* Enable key press detection. */
			regmap_update_bits(ts3a227e->regmap,
					   TS3A227E_REG_SETTING_2,
					   KP_ENABLE, KP_ENABLE);
		}
	}
}

static irqreturn_t ts3a227e_interrupt(int irq, void *data)
{
	struct ts3a227e *ts3a227e = (struct ts3a227e *)data;
	struct regmap *regmap = ts3a227e->regmap;
	unsigned int int_reg, kp_int_reg, acc_reg, i;
	struct device *dev = ts3a227e->dev;
	int ret;

	/* Check for plug/unplug. */
	ret = regmap_read(regmap, TS3A227E_REG_INTERRUPT, &int_reg);
	if (ret) {
		dev_err(dev, "failed to clear interrupt ret=%d\n", ret);
		return IRQ_NONE;
	}

	if (int_reg & (DETECTION_COMPLETE_EVENT | INS_REM_EVENT)) {
		regmap_read(regmap, TS3A227E_REG_ACCESSORY_STATUS, &acc_reg);
		ts3a227e_new_jack_state(ts3a227e, acc_reg);
	}

	/* Report any key events. */
	ret = regmap_read(regmap, TS3A227E_REG_KP_INTERRUPT, &kp_int_reg);
	if (ret) {
		dev_err(dev, "failed to clear key interrupt ret=%d\n", ret);
		return IRQ_NONE;
	}

	for (i = 0; i < TS3A227E_NUM_BUTTONS; i++) {
		if (kp_int_reg & PRESS_MASK(i))
			ts3a227e->buttons_held |= (1 << i);
		if (kp_int_reg & RELEASE_MASK(i))
			ts3a227e->buttons_held &= ~(1 << i);
	}

	ts3a227e_jack_report(ts3a227e);

	return IRQ_HANDLED;
}

/**
 * ts3a227e_enable_jack_detect - Specify a jack for event reporting
 *
 * @component:  component to register the jack with
 * @jack: jack to use to report headset and button events on
 *
 * After this function has been called the headset insert/remove and button
 * events 0-3 will be routed to the given jack.  Jack can be null to stop
 * reporting.
 */
int ts3a227e_enable_jack_detect(struct snd_soc_component *component,
				struct snd_soc_jack *jack)
{
	struct ts3a227e *ts3a227e = snd_soc_component_get_drvdata(component);

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ts3a227e->jack = jack;
	ts3a227e_jack_report(ts3a227e);

	return 0;
}
EXPORT_SYMBOL_GPL(ts3a227e_enable_jack_detect);

static struct snd_soc_component_driver ts3a227e_soc_driver;

static const struct regmap_config ts3a227e_regmap_config = {
	.val_bits = 8,
	.reg_bits = 8,

	.max_register = TS3A227E_REG_KP_THRESHOLD_3,
	.readable_reg = ts3a227e_readable_reg,
	.writeable_reg = ts3a227e_writeable_reg,
	.volatile_reg = ts3a227e_volatile_reg,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = ts3a227e_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ts3a227e_reg_defaults),
};

static int ts3a227e_parse_device_property(struct ts3a227e *ts3a227e,
				struct device *dev)
{
	u32 micbias;
	int err;

	err = device_property_read_u32(dev, "ti,micbias", &micbias);
	if (!err) {
		regmap_update_bits(ts3a227e->regmap, TS3A227E_REG_SETTING_3,
			MICBIAS_SETTING_MASK,
			(micbias & 0x07) << MICBIAS_SETTING_SFT);
	}

	return 0;
}

static int ts3a227e_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct ts3a227e *ts3a227e;
	struct device *dev = &i2c->dev;
	int ret;
	unsigned int acc_reg;

	ts3a227e = devm_kzalloc(&i2c->dev, sizeof(*ts3a227e), GFP_KERNEL);
	if (ts3a227e == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, ts3a227e);
	ts3a227e->dev = dev;
	ts3a227e->irq = i2c->irq;

	ts3a227e->regmap = devm_regmap_init_i2c(i2c, &ts3a227e_regmap_config);
	if (IS_ERR(ts3a227e->regmap))
		return PTR_ERR(ts3a227e->regmap);

	ret = ts3a227e_parse_device_property(ts3a227e, dev);
	if (ret) {
		dev_err(dev, "Failed to parse device property: %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(dev, i2c->irq, NULL, ts3a227e_interrupt,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					"TS3A227E", ts3a227e);
	if (ret) {
		dev_err(dev, "Cannot request irq %d (%d)\n", i2c->irq, ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(&i2c->dev, &ts3a227e_soc_driver,
					      NULL, 0);
	if (ret)
		return ret;

	/* Enable interrupts except for ADC complete. */
	regmap_update_bits(ts3a227e->regmap, TS3A227E_REG_INTERRUPT_DISABLE,
			   INTB_DISABLE | ADC_COMPLETE_INT_DISABLE,
			   ADC_COMPLETE_INT_DISABLE);

	/* Read jack status because chip might not trigger interrupt at boot. */
	regmap_read(ts3a227e->regmap, TS3A227E_REG_ACCESSORY_STATUS, &acc_reg);
	ts3a227e_new_jack_state(ts3a227e, acc_reg);
	ts3a227e_jack_report(ts3a227e);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ts3a227e_suspend(struct device *dev)
{
	struct ts3a227e *ts3a227e = dev_get_drvdata(dev);

	dev_dbg(ts3a227e->dev, "suspend disable irq\n");
	disable_irq(ts3a227e->irq);

	return 0;
}

static int ts3a227e_resume(struct device *dev)
{
	struct ts3a227e *ts3a227e = dev_get_drvdata(dev);

	dev_dbg(ts3a227e->dev, "resume enable irq\n");
	enable_irq(ts3a227e->irq);

	return 0;
}
#endif

static const struct dev_pm_ops ts3a227e_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(ts3a227e_suspend, ts3a227e_resume)
};

static const struct i2c_device_id ts3a227e_i2c_ids[] = {
	{ "ts3a227e", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ts3a227e_i2c_ids);

static const struct of_device_id ts3a227e_of_match[] = {
	{ .compatible = "ti,ts3a227e", },
	{ }
};
MODULE_DEVICE_TABLE(of, ts3a227e_of_match);

#ifdef CONFIG_ACPI
static struct acpi_device_id ts3a227e_acpi_match[] = {
	{ "104C227E", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, ts3a227e_acpi_match);
#endif

static struct i2c_driver ts3a227e_driver = {
	.driver = {
		.name = "ts3a227e",
		.pm = &ts3a227e_pm,
		.of_match_table = of_match_ptr(ts3a227e_of_match),
		.acpi_match_table = ACPI_PTR(ts3a227e_acpi_match),
	},
	.probe = ts3a227e_i2c_probe,
	.id_table = ts3a227e_i2c_ids,
};
module_i2c_driver(ts3a227e_driver);

MODULE_DESCRIPTION("ASoC ts3a227e driver");
MODULE_AUTHOR("Dylan Reid <dgreid@chromium.org>");
MODULE_LICENSE("GPL v2");
