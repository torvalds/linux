// SPDX-License-Identifier: GPL-2.0
//
// CS35l41 ALSA HDA audio driver
//
// Copyright 2021 Cirrus Logic, Inc.
//
// Author: Lucas Tanure <tanureal@opensource.cirrus.com>

#include <linux/acpi.h>
#include <linux/module.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"
#include "hda_component.h"
#include "cs35l41_hda.h"

static const struct reg_sequence cs35l41_hda_config[] = {
	{ CS35L41_PLL_CLK_CTRL,		0x00000430 }, // 3200000Hz, BCLK Input, PLL_REFCLK_EN = 1
	{ CS35L41_GLOBAL_CLK_CTRL,	0x00000003 }, // GLOBAL_FS = 48 kHz
	{ CS35L41_SP_ENABLES,		0x00010000 }, // ASP_RX1_EN = 1
	{ CS35L41_SP_RATE_CTRL,		0x00000021 }, // ASP_BCLK_FREQ = 3.072 MHz
	{ CS35L41_SP_FORMAT,		0x20200200 }, // 24 bits, I2S, BCLK Slave, FSYNC Slave
	{ CS35L41_DAC_PCM1_SRC,		0x00000008 }, // DACPCM1_SRC = ASPRX1
	{ CS35L41_AMP_DIG_VOL_CTRL,	0x00000000 }, // AMP_VOL_PCM  0.0 dB
	{ CS35L41_AMP_GAIN_CTRL,	0x00000084 }, // AMP_GAIN_PCM 4.5 dB
	{ CS35L41_PWR_CTRL2,		0x00000001 }, // AMP_EN = 1
};

static const struct reg_sequence cs35l41_hda_start_bst[] = {
	{ CS35L41_PWR_CTRL2,		0x00000021 }, // BST_EN = 10, AMP_EN = 1
	{ CS35L41_PWR_CTRL1,		0x00000001, 3000}, // set GLOBAL_EN = 1
};

static const struct reg_sequence cs35l41_hda_stop_bst[] = {
	{ CS35L41_PWR_CTRL1,		0x00000000, 3000}, // set GLOBAL_EN = 0
};

// only on amps where GPIO1 is used to control ext. VSPK switch
static const struct reg_sequence cs35l41_start_ext_vspk[] = {
	{ 0x00000040,			0x00000055 },
	{ 0x00000040,			0x000000AA },
	{ 0x00007438,			0x00585941 },
	{ 0x00007414,			0x08C82222 },
	{ 0x0000742C,			0x00000009 },
	{ 0x00011008,			0x00008001 },
	{ 0x0000742C,			0x0000000F },
	{ 0x0000742C,			0x00000079 },
	{ 0x00007438,			0x00585941 },
	{ CS35L41_PWR_CTRL1,		0x00000001, 3000}, // set GLOBAL_EN = 1
	{ 0x0000742C,			0x000000F9 },
	{ 0x00007438,			0x00580941 },
	{ 0x00000040,			0x000000CC },
	{ 0x00000040,			0x00000033 },
};

//only on amps where GPIO1 is used to control ext. VSPK switch
static const struct reg_sequence cs35l41_stop_ext_vspk[] = {
	{ 0x00000040,			0x00000055 },
	{ 0x00000040,			0x000000AA },
	{ 0x00007438,			0x00585941 },
	{ 0x00002014,			0x00000000, 3000}, // set GLOBAL_EN = 0
	{ 0x0000742C,			0x00000009 },
	{ 0x00007438,			0x00580941 },
	{ 0x00011008,			0x00000001 },
	{ 0x0000393C,			0x000000C0, 6000},
	{ 0x0000393C,			0x00000000 },
	{ 0x00007414,			0x00C82222 },
	{ 0x0000742C,			0x00000000 },
	{ 0x00000040,			0x000000CC },
	{ 0x00000040,			0x00000033 },
};

static const struct reg_sequence cs35l41_safe_to_active[] = {
	{ 0x00000040,			0x00000055 },
	{ 0x00000040,			0x000000AA },
	{ 0x0000742C,			0x0000000F },
	{ 0x0000742C,			0x00000079 },
	{ 0x00007438,			0x00585941 },
	{ CS35L41_PWR_CTRL1,		0x00000001, 2000 }, // GLOBAL_EN = 1
	{ 0x0000742C,			0x000000F9 },
	{ 0x00007438,			0x00580941 },
	{ 0x00000040,			0x000000CC },
	{ 0x00000040,			0x00000033 },
};

static const struct reg_sequence cs35l41_active_to_safe[] = {
	{ 0x00000040,			0x00000055 },
	{ 0x00000040,			0x000000AA },
	{ 0x00007438,			0x00585941 },
	{ CS35L41_AMP_DIG_VOL_CTRL,	0x0000A678 }, // AMP_VOL_PCM Mute
	{ CS35L41_PWR_CTRL2,		0x00000000 }, // AMP_EN = 0
	{ CS35L41_PWR_CTRL1,		0x00000000 },
	{ 0x0000742C,			0x00000009, 2000 },
	{ 0x00007438,			0x00580941 },
	{ 0x00000040,			0x000000CC },
	{ 0x00000040,			0x00000033 },
};

static const struct reg_sequence cs35l41_reset_to_safe[] = {
	{ 0x00000040,			0x00000055 },
	{ 0x00000040,			0x000000AA },
	{ 0x00007438,			0x00585941 },
	{ 0x00007414,			0x08C82222 },
	{ 0x0000742C,			0x00000009 },
	{ 0x00000040,			0x000000CC },
	{ 0x00000040,			0x00000033 },
};

static const struct cs35l41_hda_reg_sequence cs35l41_hda_reg_seq_no_bst = {
	.probe		= cs35l41_reset_to_safe,
	.num_probe	= ARRAY_SIZE(cs35l41_reset_to_safe),
	.open		= cs35l41_hda_config,
	.num_open	= ARRAY_SIZE(cs35l41_hda_config),
	.prepare	= cs35l41_safe_to_active,
	.num_prepare	= ARRAY_SIZE(cs35l41_safe_to_active),
	.cleanup	= cs35l41_active_to_safe,
	.num_cleanup	= ARRAY_SIZE(cs35l41_active_to_safe),
};

static const struct cs35l41_hda_reg_sequence cs35l41_hda_reg_seq_ext_bst = {
	.open		= cs35l41_hda_config,
	.num_open	= ARRAY_SIZE(cs35l41_hda_config),
	.prepare	= cs35l41_start_ext_vspk,
	.num_prepare	= ARRAY_SIZE(cs35l41_start_ext_vspk),
	.cleanup	= cs35l41_stop_ext_vspk,
	.num_cleanup	= ARRAY_SIZE(cs35l41_stop_ext_vspk),
};

static const struct cs35l41_hda_reg_sequence cs35l41_hda_reg_seq_int_bst = {
	.open		= cs35l41_hda_config,
	.num_open	= ARRAY_SIZE(cs35l41_hda_config),
	.prepare	= cs35l41_hda_start_bst,
	.num_prepare	= ARRAY_SIZE(cs35l41_hda_start_bst),
	.cleanup	= cs35l41_hda_stop_bst,
	.num_cleanup	= ARRAY_SIZE(cs35l41_hda_stop_bst),
};

static void cs35l41_hda_playback_hook(struct device *dev, int action)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	const struct cs35l41_hda_reg_sequence *reg_seq = cs35l41->reg_seq;
	struct regmap *reg = cs35l41->regmap;
	int ret = 0;

	switch (action) {
	case HDA_GEN_PCM_ACT_OPEN:
		if (reg_seq->open)
			ret = regmap_multi_reg_write(reg, reg_seq->open, reg_seq->num_open);
		break;
	case HDA_GEN_PCM_ACT_PREPARE:
		if (reg_seq->prepare)
			ret = regmap_multi_reg_write(reg, reg_seq->prepare, reg_seq->num_prepare);
		break;
	case HDA_GEN_PCM_ACT_CLEANUP:
		if (reg_seq->cleanup)
			ret = regmap_multi_reg_write(reg, reg_seq->cleanup, reg_seq->num_cleanup);
		break;
	case HDA_GEN_PCM_ACT_CLOSE:
		if (reg_seq->close)
			ret = regmap_multi_reg_write(reg, reg_seq->close, reg_seq->num_close);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		dev_warn(cs35l41->dev, "Failed to apply multi reg write: %d\n", ret);
}

static int cs35l41_hda_channel_map(struct device *dev, unsigned int tx_num, unsigned int *tx_slot,
				    unsigned int rx_num, unsigned int *rx_slot)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);

	return cs35l41_set_channels(cs35l41->dev, cs35l41->regmap, tx_num, tx_slot, rx_num,
				    rx_slot);
}

static int cs35l41_hda_bind(struct device *dev, struct device *master, void *master_data)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct hda_component *comps = master_data;

	if (!comps || cs35l41->index < 0 || cs35l41->index >= HDA_MAX_COMPONENTS)
		return -EINVAL;

	comps = &comps[cs35l41->index];
	if (comps->dev)
		return -EBUSY;

	comps->dev = dev;
	strscpy(comps->name, dev_name(dev), sizeof(comps->name));
	comps->playback_hook = cs35l41_hda_playback_hook;
	comps->set_channel_map = cs35l41_hda_channel_map;

	return 0;
}

static void cs35l41_hda_unbind(struct device *dev, struct device *master, void *master_data)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct hda_component *comps = master_data;

	if (comps[cs35l41->index].dev == dev)
		memset(&comps[cs35l41->index], 0, sizeof(*comps));
}

static const struct component_ops cs35l41_hda_comp_ops = {
	.bind = cs35l41_hda_bind,
	.unbind = cs35l41_hda_unbind,
};

static int cs35l41_hda_apply_properties(struct cs35l41_hda *cs35l41,
					const struct cs35l41_hda_hw_config *hw_cfg)
{
	bool internal_boost = false;
	int ret;

	if (!hw_cfg) {
		cs35l41->reg_seq = &cs35l41_hda_reg_seq_no_bst;
		return 0;
	}

	if (hw_cfg->bst_ind || hw_cfg->bst_cap || hw_cfg->bst_ipk)
		internal_boost = true;

	switch (hw_cfg->gpio1_func) {
	case CS35L41_NOT_USED:
		break;
	case CS35l41_VSPK_SWITCH:
		regmap_update_bits(cs35l41->regmap, CS35L41_GPIO_PAD_CONTROL,
				   CS35L41_GPIO1_CTRL_MASK, 1 << CS35L41_GPIO1_CTRL_SHIFT);
		break;
	case CS35l41_SYNC:
		regmap_update_bits(cs35l41->regmap, CS35L41_GPIO_PAD_CONTROL,
				   CS35L41_GPIO1_CTRL_MASK, 2 << CS35L41_GPIO1_CTRL_SHIFT);
		break;
	default:
		dev_err(cs35l41->dev, "Invalid function %d for GPIO1\n", hw_cfg->gpio1_func);
		return -EINVAL;
	}

	switch (hw_cfg->gpio2_func) {
	case CS35L41_NOT_USED:
		break;
	case CS35L41_INTERRUPT:
		regmap_update_bits(cs35l41->regmap, CS35L41_GPIO_PAD_CONTROL,
				   CS35L41_GPIO2_CTRL_MASK, 2 << CS35L41_GPIO2_CTRL_SHIFT);
		break;
	default:
		dev_err(cs35l41->dev, "Invalid function %d for GPIO2\n", hw_cfg->gpio2_func);
		return -EINVAL;
	}

	if (internal_boost) {
		cs35l41->reg_seq = &cs35l41_hda_reg_seq_int_bst;
		if (!(hw_cfg->bst_ind && hw_cfg->bst_cap && hw_cfg->bst_ipk))
			return -EINVAL;
		ret = cs35l41_boost_config(cs35l41->dev, cs35l41->regmap,
					   hw_cfg->bst_ind, hw_cfg->bst_cap, hw_cfg->bst_ipk);
		if (ret)
			return ret;
	} else {
		cs35l41->reg_seq = &cs35l41_hda_reg_seq_ext_bst;
	}

	return cs35l41_hda_channel_map(cs35l41->dev, 0, NULL, 1, (unsigned int *)&hw_cfg->spk_pos);
}

static struct cs35l41_hda_hw_config *cs35l41_hda_read_acpi(struct cs35l41_hda *cs35l41,
							   const char *hid, int id)
{
	struct cs35l41_hda_hw_config *hw_cfg;
	u32 values[HDA_MAX_COMPONENTS];
	struct acpi_device *adev;
	struct device *physdev;
	char *property;
	size_t nval;
	int i, ret;

	adev = acpi_dev_get_first_match_dev(hid, NULL, -1);
	if (!adev) {
		dev_err(cs35l41->dev, "Failed to find an ACPI device for %s\n", hid);
		return ERR_PTR(-ENODEV);
	}

	physdev = get_device(acpi_get_first_physical_node(adev));
	acpi_dev_put(adev);

	property = "cirrus,dev-index";
	ret = device_property_count_u32(physdev, property);
	if (ret <= 0)
		goto no_acpi_dsd;

	if (ret > ARRAY_SIZE(values)) {
		ret = -EINVAL;
		goto err;
	}
	nval = ret;

	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err;

	cs35l41->index = -1;
	for (i = 0; i < nval; i++) {
		if (values[i] == id) {
			cs35l41->index = i;
			break;
		}
	}
	if (cs35l41->index == -1) {
		dev_err(cs35l41->dev, "No index found in %s\n", property);
		ret = -ENODEV;
		goto err;
	}

	/* To use the same release code for all laptop variants we can't use devm_ version of
	 * gpiod_get here, as CLSA010* don't have a fully functional bios with an _DSD node
	 */
	cs35l41->reset_gpio = fwnode_gpiod_get_index(&adev->fwnode, "reset", cs35l41->index,
						     GPIOD_OUT_LOW, "cs35l41-reset");

	hw_cfg = kzalloc(sizeof(*hw_cfg), GFP_KERNEL);
	if (!hw_cfg) {
		ret = -ENOMEM;
		goto err;
	}

	property = "cirrus,speaker-position";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err_free;
	hw_cfg->spk_pos = values[cs35l41->index];

	property = "cirrus,gpio1-func";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err_free;
	hw_cfg->gpio1_func = values[cs35l41->index];

	property = "cirrus,gpio2-func";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err_free;
	hw_cfg->gpio2_func = values[cs35l41->index];

	property = "cirrus,boost-peak-milliamp";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret == 0)
		hw_cfg->bst_ipk = values[cs35l41->index];

	property = "cirrus,boost-ind-nanohenry";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret == 0)
		hw_cfg->bst_ind = values[cs35l41->index];

	property = "cirrus,boost-cap-microfarad";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret == 0)
		hw_cfg->bst_cap = values[cs35l41->index];

	put_device(physdev);

	return hw_cfg;

err_free:
	kfree(hw_cfg);
err:
	put_device(physdev);
	dev_err(cs35l41->dev, "Failed property %s: %d\n", property, ret);

	return ERR_PTR(ret);

no_acpi_dsd:
	/*
	 * Device CLSA0100 doesn't have _DSD so a gpiod_get by the label reset won't work.
	 * And devices created by i2c-multi-instantiate don't have their device struct pointing to
	 * the correct fwnode, so acpi_dev must be used here.
	 * And devm functions expect that the device requesting the resource has the correct
	 * fwnode.
	 */
	if (strncmp(hid, "CLSA0100", 8) != 0)
		return ERR_PTR(-EINVAL);

	/* check I2C address to assign the index */
	cs35l41->index = id == 0x40 ? 0 : 1;
	cs35l41->reset_gpio = gpiod_get_index(physdev, NULL, 0, GPIOD_OUT_HIGH);
	cs35l41->vspk_always_on = true;
	put_device(physdev);

	return NULL;
}

int cs35l41_hda_probe(struct device *dev, const char *device_name, int id, int irq,
		      struct regmap *regmap)
{
	unsigned int int_sts, regid, reg_revid, mtl_revid, chipid, int_status;
	struct cs35l41_hda_hw_config *acpi_hw_cfg;
	struct cs35l41_hda *cs35l41;
	int ret;

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	cs35l41 = devm_kzalloc(dev, sizeof(*cs35l41), GFP_KERNEL);
	if (!cs35l41)
		return -ENOMEM;

	cs35l41->dev = dev;
	cs35l41->irq = irq;
	cs35l41->regmap = regmap;
	dev_set_drvdata(dev, cs35l41);

	acpi_hw_cfg = cs35l41_hda_read_acpi(cs35l41, device_name, id);
	if (IS_ERR(acpi_hw_cfg))
		return PTR_ERR(acpi_hw_cfg);

	if (IS_ERR(cs35l41->reset_gpio)) {
		ret = PTR_ERR(cs35l41->reset_gpio);
		cs35l41->reset_gpio = NULL;
		if (ret == -EBUSY) {
			dev_info(cs35l41->dev, "Reset line busy, assuming shared reset\n");
		} else {
			dev_err_probe(cs35l41->dev, ret, "Failed to get reset GPIO: %d\n", ret);
			goto err;
		}
	}
	if (cs35l41->reset_gpio) {
		usleep_range(2000, 2100);
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 1);
	}

	usleep_range(2000, 2100);

	ret = regmap_read_poll_timeout(cs35l41->regmap, CS35L41_IRQ1_STATUS4, int_status,
				       int_status & CS35L41_OTP_BOOT_DONE, 1000, 100000);
	if (ret) {
		dev_err(cs35l41->dev, "Failed waiting for OTP_BOOT_DONE: %d\n", ret);
		goto err;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_IRQ1_STATUS3, &int_sts);
	if (ret || (int_sts & CS35L41_OTP_BOOT_ERR)) {
		dev_err(cs35l41->dev, "OTP Boot status %x error: %d\n",
			int_sts & CS35L41_OTP_BOOT_ERR, ret);
		ret = -EIO;
		goto err;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_DEVID, &regid);
	if (ret) {
		dev_err(cs35l41->dev, "Get Device ID failed: %d\n", ret);
		goto err;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_REVID, &reg_revid);
	if (ret) {
		dev_err(cs35l41->dev, "Get Revision ID failed: %d\n", ret);
		goto err;
	}

	mtl_revid = reg_revid & CS35L41_MTLREVID_MASK;

	chipid = (mtl_revid % 2) ? CS35L41R_CHIP_ID : CS35L41_CHIP_ID;
	if (regid != chipid) {
		dev_err(cs35l41->dev, "CS35L41 Device ID (%X). Expected ID %X\n", regid, chipid);
		ret = -ENODEV;
		goto err;
	}

	ret = cs35l41_test_key_unlock(cs35l41->dev, cs35l41->regmap);
	if (ret)
		goto err;

	ret = cs35l41_register_errata_patch(cs35l41->dev, cs35l41->regmap, reg_revid);
	if (ret)
		goto err;

	ret = cs35l41_otp_unpack(cs35l41->dev, cs35l41->regmap);
	if (ret) {
		dev_err(cs35l41->dev, "OTP Unpack failed: %d\n", ret);
		goto err;
	}

	ret = cs35l41_test_key_lock(cs35l41->dev, cs35l41->regmap);
	if (ret)
		goto err;

	ret = cs35l41_hda_apply_properties(cs35l41, acpi_hw_cfg);
	if (ret)
		goto err;
	kfree(acpi_hw_cfg);
	acpi_hw_cfg = NULL;

	if (cs35l41->reg_seq->probe) {
		ret = regmap_multi_reg_write(cs35l41->regmap, cs35l41->reg_seq->probe,
					     cs35l41->reg_seq->num_probe);
		if (ret) {
			dev_err(cs35l41->dev, "Fail to apply probe reg patch: %d\n", ret);
			goto err;
		}
	}

	ret = component_add(cs35l41->dev, &cs35l41_hda_comp_ops);
	if (ret) {
		dev_err(cs35l41->dev, "Register component failed: %d\n", ret);
		goto err;
	}

	dev_info(cs35l41->dev, "Cirrus Logic CS35L41 (%x), Revision: %02X\n", regid, reg_revid);

	return 0;

err:
	kfree(acpi_hw_cfg);
	if (!cs35l41->vspk_always_on)
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
	gpiod_put(cs35l41->reset_gpio);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l41_hda_probe, SND_HDA_SCODEC_CS35L41);

void cs35l41_hda_remove(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);

	component_del(cs35l41->dev, &cs35l41_hda_comp_ops);

	if (!cs35l41->vspk_always_on)
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
	gpiod_put(cs35l41->reset_gpio);
}
EXPORT_SYMBOL_NS_GPL(cs35l41_hda_remove, SND_HDA_SCODEC_CS35L41);

MODULE_DESCRIPTION("CS35L41 HDA Driver");
MODULE_AUTHOR("Lucas Tanure, Cirrus Logic Inc, <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
