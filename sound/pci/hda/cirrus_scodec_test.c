// SPDX-License-Identifier: GPL-2.0-only
//
// KUnit test for the Cirrus side-codec library.
//
// Copyright (C) 2023 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/test.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "cirrus_scodec.h"

struct cirrus_scodec_test_gpio {
	unsigned int pin_state;
	struct gpio_chip chip;
};

struct cirrus_scodec_test_priv {
	struct platform_device amp_pdev;
	struct platform_device *gpio_pdev;
	struct cirrus_scodec_test_gpio *gpio_priv;
};

static int cirrus_scodec_test_gpio_get_direction(struct gpio_chip *chip,
						 unsigned int offset)
{
	return GPIO_LINE_DIRECTION_IN;
}

static int cirrus_scodec_test_gpio_direction_in(struct gpio_chip *chip,
						unsigned int offset)
{
	return 0;
}

static int cirrus_scodec_test_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct cirrus_scodec_test_gpio *gpio_priv = gpiochip_get_data(chip);

	return !!(gpio_priv->pin_state & BIT(offset));
}

static int cirrus_scodec_test_gpio_direction_out(struct gpio_chip *chip,
						 unsigned int offset, int value)
{
	return -EOPNOTSUPP;
}

static void cirrus_scodec_test_gpio_set(struct gpio_chip *chip, unsigned int offset,
					int value)
{
}

static int cirrus_scodec_test_gpio_set_config(struct gpio_chip *gc,
					      unsigned int offset,
					      unsigned long config)
{
	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_OUTPUT:
	case PIN_CONFIG_OUTPUT_ENABLE:
		return -EOPNOTSUPP;
	default:
		return 0;
	}
}

static const struct gpio_chip cirrus_scodec_test_gpio_chip = {
	.label			= "cirrus_scodec_test_gpio",
	.owner			= THIS_MODULE,
	.request		= gpiochip_generic_request,
	.free			= gpiochip_generic_free,
	.get_direction		= cirrus_scodec_test_gpio_get_direction,
	.direction_input	= cirrus_scodec_test_gpio_direction_in,
	.get			= cirrus_scodec_test_gpio_get,
	.direction_output	= cirrus_scodec_test_gpio_direction_out,
	.set			= cirrus_scodec_test_gpio_set,
	.set_config		= cirrus_scodec_test_gpio_set_config,
	.base			= -1,
	.ngpio			= 32,
};

static int cirrus_scodec_test_gpio_probe(struct platform_device *pdev)
{
	struct cirrus_scodec_test_gpio *gpio_priv;
	int ret;

	gpio_priv = devm_kzalloc(&pdev->dev, sizeof(*gpio_priv), GFP_KERNEL);
	if (!gpio_priv)
		return -ENOMEM;

	/* GPIO core modifies our struct gpio_chip so use a copy */
	gpio_priv->chip = cirrus_scodec_test_gpio_chip;
	ret = devm_gpiochip_add_data(&pdev->dev, &gpio_priv->chip, gpio_priv);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to add gpiochip\n");

	dev_set_drvdata(&pdev->dev, gpio_priv);

	return 0;
}

static struct platform_driver cirrus_scodec_test_gpio_driver = {
	.driver.name	= "cirrus_scodec_test_gpio_drv",
	.probe		= cirrus_scodec_test_gpio_probe,
};

/* software_node referencing the gpio driver */
static const struct software_node cirrus_scodec_test_gpio_swnode = {
	.name = "cirrus_scodec_test_gpio",
};

static int cirrus_scodec_test_create_gpio(struct kunit *test)
{
	struct cirrus_scodec_test_priv *priv = test->priv;
	int ret;

	priv->gpio_pdev = platform_device_alloc(cirrus_scodec_test_gpio_driver.driver.name, -1);
	if (!priv->gpio_pdev)
		return -ENOMEM;

	ret = device_add_software_node(&priv->gpio_pdev->dev, &cirrus_scodec_test_gpio_swnode);
	if (ret) {
		platform_device_put(priv->gpio_pdev);
		KUNIT_FAIL(test, "Failed to add swnode to gpio: %d\n", ret);
		return ret;
	}

	ret = platform_device_add(priv->gpio_pdev);
	if (ret) {
		platform_device_put(priv->gpio_pdev);
		KUNIT_FAIL(test, "Failed to add gpio platform device: %d\n", ret);
		return ret;
	}

	priv->gpio_priv = dev_get_drvdata(&priv->gpio_pdev->dev);
	if (!priv->gpio_priv) {
		platform_device_put(priv->gpio_pdev);
		KUNIT_FAIL(test, "Failed to get gpio private data\n");
		return -EINVAL;
	}

	return 0;
}

static void cirrus_scodec_test_set_gpio_ref_arg(struct software_node_ref_args *arg,
						int gpio_num)
{
	struct software_node_ref_args template =
		SOFTWARE_NODE_REFERENCE(&cirrus_scodec_test_gpio_swnode, gpio_num, 0);

	*arg = template;
}

static int cirrus_scodec_test_set_spkid_swnode(struct kunit *test,
					       struct device *dev,
					       struct software_node_ref_args *args,
					       int num_args)
{
	const struct property_entry props_template[] = {
		PROPERTY_ENTRY_REF_ARRAY_LEN("spk-id-gpios", args, num_args),
		{ }
	};
	struct property_entry *props;
	struct software_node *node;

	node = kunit_kzalloc(test, sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	props = kunit_kzalloc(test, sizeof(props_template), GFP_KERNEL);
	if (!props)
		return -ENOMEM;

	memcpy(props, props_template, sizeof(props_template));
	node->properties = props;

	return device_add_software_node(dev, node);
}

struct cirrus_scodec_test_spkid_param {
	int num_amps;
	int gpios_per_amp;
	int num_amps_sharing;
};

static void cirrus_scodec_test_spkid_parse(struct kunit *test)
{
	struct cirrus_scodec_test_priv *priv = test->priv;
	const struct cirrus_scodec_test_spkid_param *param = test->param_value;
	int num_spk_id_refs = param->num_amps * param->gpios_per_amp;
	struct software_node_ref_args *refs;
	struct device *dev = &priv->amp_pdev.dev;
	unsigned int v;
	int i, ret;

	refs = kunit_kcalloc(test, num_spk_id_refs, sizeof(*refs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, refs);

	for (i = 0, v = 0; i < num_spk_id_refs; ) {
		cirrus_scodec_test_set_gpio_ref_arg(&refs[i++], v++);

		/*
		 * If amps are sharing GPIOs repeat the last set of
		 * GPIOs until we've done that number of amps.
		 * We have done all GPIOs for an amp when i is a multiple
		 * of gpios_per_amp.
		 * We have done all amps sharing the same GPIOs when i is
		 * a multiple of (gpios_per_amp * num_amps_sharing).
		 */
		if (!(i % param->gpios_per_amp) &&
		    (i % (param->gpios_per_amp * param->num_amps_sharing)))
			v -= param->gpios_per_amp;
	}

	ret = cirrus_scodec_test_set_spkid_swnode(test, dev, refs, num_spk_id_refs);
	KUNIT_EXPECT_EQ_MSG(test, ret, 0, "Failed to add swnode\n");

	for (i = 0; i < param->num_amps; ++i) {
		for (v = 0; v < (1 << param->gpios_per_amp); ++v) {
			/* Set only the GPIO bits used by this amp */
			priv->gpio_priv->pin_state =
				v << (param->gpios_per_amp * (i / param->num_amps_sharing));

			ret = cirrus_scodec_get_speaker_id(dev, i, param->num_amps, -1);
			KUNIT_EXPECT_EQ_MSG(test, ret, v,
					    "get_speaker_id failed amp:%d pin_state:%#x\n",
					    i, priv->gpio_priv->pin_state);
		}
	}
}

static void cirrus_scodec_test_no_spkid(struct kunit *test)
{
	struct cirrus_scodec_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_pdev.dev;
	int ret;

	ret = cirrus_scodec_get_speaker_id(dev, 0, 4, -1);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);
}

static void cirrus_scodec_test_dev_release(struct device *dev)
{
}

static int cirrus_scodec_test_case_init(struct kunit *test)
{
	struct cirrus_scodec_test_priv *priv;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	test->priv = priv;

	/* Create dummy GPIO */
	ret = cirrus_scodec_test_create_gpio(test);
	if (ret < 0)
		return ret;

	/* Create dummy amp driver dev */
	priv->amp_pdev.name = "cirrus_scodec_test_amp_drv";
	priv->amp_pdev.id = -1;
	priv->amp_pdev.dev.release = cirrus_scodec_test_dev_release;
	ret = platform_device_register(&priv->amp_pdev);
	KUNIT_ASSERT_GE_MSG(test, ret, 0, "Failed to register amp platform device\n");

	return 0;
}

static void cirrus_scodec_test_case_exit(struct kunit *test)
{
	struct cirrus_scodec_test_priv *priv = test->priv;

	if (priv->amp_pdev.name)
		platform_device_unregister(&priv->amp_pdev);

	if (priv->gpio_pdev) {
		device_remove_software_node(&priv->gpio_pdev->dev);
		platform_device_unregister(priv->gpio_pdev);
	}
}

static int cirrus_scodec_test_suite_init(struct kunit_suite *suite)
{
	int ret;

	/* Register mock GPIO driver */
	ret = platform_driver_register(&cirrus_scodec_test_gpio_driver);
	if (ret < 0) {
		kunit_err(suite, "Failed to register gpio platform driver, %d\n", ret);
		return ret;
	}

	return 0;
}

static void cirrus_scodec_test_suite_exit(struct kunit_suite *suite)
{
	platform_driver_unregister(&cirrus_scodec_test_gpio_driver);
}

static const struct cirrus_scodec_test_spkid_param cirrus_scodec_test_spkid_param_cases[] = {
	{ .num_amps = 2, .gpios_per_amp = 1, .num_amps_sharing = 1 },
	{ .num_amps = 2, .gpios_per_amp = 2, .num_amps_sharing = 1 },
	{ .num_amps = 2, .gpios_per_amp = 3, .num_amps_sharing = 1 },
	{ .num_amps = 2, .gpios_per_amp = 4, .num_amps_sharing = 1 },
	{ .num_amps = 3, .gpios_per_amp = 1, .num_amps_sharing = 1 },
	{ .num_amps = 3, .gpios_per_amp = 2, .num_amps_sharing = 1 },
	{ .num_amps = 3, .gpios_per_amp = 3, .num_amps_sharing = 1 },
	{ .num_amps = 3, .gpios_per_amp = 4, .num_amps_sharing = 1 },
	{ .num_amps = 4, .gpios_per_amp = 1, .num_amps_sharing = 1 },
	{ .num_amps = 4, .gpios_per_amp = 2, .num_amps_sharing = 1 },
	{ .num_amps = 4, .gpios_per_amp = 3, .num_amps_sharing = 1 },
	{ .num_amps = 4, .gpios_per_amp = 4, .num_amps_sharing = 1 },

	/* Same GPIO shared by all amps */
	{ .num_amps = 2, .gpios_per_amp = 1, .num_amps_sharing = 2 },
	{ .num_amps = 2, .gpios_per_amp = 2, .num_amps_sharing = 2 },
	{ .num_amps = 2, .gpios_per_amp = 3, .num_amps_sharing = 2 },
	{ .num_amps = 2, .gpios_per_amp = 4, .num_amps_sharing = 2 },
	{ .num_amps = 3, .gpios_per_amp = 1, .num_amps_sharing = 3 },
	{ .num_amps = 3, .gpios_per_amp = 2, .num_amps_sharing = 3 },
	{ .num_amps = 3, .gpios_per_amp = 3, .num_amps_sharing = 3 },
	{ .num_amps = 3, .gpios_per_amp = 4, .num_amps_sharing = 3 },
	{ .num_amps = 4, .gpios_per_amp = 1, .num_amps_sharing = 4 },
	{ .num_amps = 4, .gpios_per_amp = 2, .num_amps_sharing = 4 },
	{ .num_amps = 4, .gpios_per_amp = 3, .num_amps_sharing = 4 },
	{ .num_amps = 4, .gpios_per_amp = 4, .num_amps_sharing = 4 },

	/* Two sets of shared GPIOs */
	{ .num_amps = 4, .gpios_per_amp = 1, .num_amps_sharing = 2 },
	{ .num_amps = 4, .gpios_per_amp = 2, .num_amps_sharing = 2 },
	{ .num_amps = 4, .gpios_per_amp = 3, .num_amps_sharing = 2 },
	{ .num_amps = 4, .gpios_per_amp = 4, .num_amps_sharing = 2 },
};

static void cirrus_scodec_test_spkid_param_desc(const struct cirrus_scodec_test_spkid_param *param,
						char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "amps:%d gpios_per_amp:%d num_amps_sharing:%d",
		 param->num_amps, param->gpios_per_amp, param->num_amps_sharing);
}

KUNIT_ARRAY_PARAM(cirrus_scodec_test_spkid, cirrus_scodec_test_spkid_param_cases,
		  cirrus_scodec_test_spkid_param_desc);

static struct kunit_case cirrus_scodec_test_cases[] = {
	KUNIT_CASE_PARAM(cirrus_scodec_test_spkid_parse, cirrus_scodec_test_spkid_gen_params),
	KUNIT_CASE(cirrus_scodec_test_no_spkid),
	{ } /* terminator */
};

static struct kunit_suite cirrus_scodec_test_suite = {
	.name = "snd-hda-scodec-cs35l56-test",
	.suite_init = cirrus_scodec_test_suite_init,
	.suite_exit = cirrus_scodec_test_suite_exit,
	.init = cirrus_scodec_test_case_init,
	.exit = cirrus_scodec_test_case_exit,
	.test_cases = cirrus_scodec_test_cases,
};

kunit_test_suite(cirrus_scodec_test_suite);

MODULE_IMPORT_NS("SND_HDA_CIRRUS_SCODEC");
MODULE_DESCRIPTION("KUnit test for the Cirrus side-codec library");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
