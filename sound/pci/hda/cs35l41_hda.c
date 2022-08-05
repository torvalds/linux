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
	{ CS35L41_PLL_CLK_CTRL,		0x00000430 }, // 3072000Hz, BCLK Input, PLL_REFCLK_EN = 1
	{ CS35L41_GLOBAL_CLK_CTRL,	0x00000003 }, // GLOBAL_FS = 48 kHz
	{ CS35L41_SP_ENABLES,		0x00010000 }, // ASP_RX1_EN = 1
	{ CS35L41_SP_RATE_CTRL,		0x00000021 }, // ASP_BCLK_FREQ = 3.072 MHz
	{ CS35L41_SP_FORMAT,		0x20200200 }, // 32 bits RX/TX slots, I2S, clk consumer
	{ CS35L41_DAC_PCM1_SRC,		0x00000008 }, // DACPCM1_SRC = ASPRX1
	{ CS35L41_AMP_DIG_VOL_CTRL,	0x00000000 }, // AMP_VOL_PCM  0.0 dB
	{ CS35L41_AMP_GAIN_CTRL,	0x00000084 }, // AMP_GAIN_PCM 4.5 dB
};

static const struct reg_sequence cs35l41_hda_mute[] = {
	{ CS35L41_AMP_GAIN_CTRL,	0x00000000 }, // AMP_GAIN_PCM 0.5 dB
	{ CS35L41_AMP_DIG_VOL_CTRL,	0x0000A678 }, // AMP_VOL_PCM Mute
};

/* Protection release cycle to get the speaker out of Safe-Mode */
static void cs35l41_error_release(struct device *dev, struct regmap *regmap, unsigned int mask)
{
	regmap_write(regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
	regmap_set_bits(regmap, CS35L41_PROTECT_REL_ERR_IGN, mask);
	regmap_clear_bits(regmap, CS35L41_PROTECT_REL_ERR_IGN, mask);
}

/* Clear all errors to release safe mode. Global Enable must be cleared first. */
static void cs35l41_irq_release(struct cs35l41_hda *cs35l41)
{
	cs35l41_error_release(cs35l41->dev, cs35l41->regmap, cs35l41->irq_errors);
	cs35l41->irq_errors = 0;
}

static void cs35l41_hda_playback_hook(struct device *dev, int action)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	struct regmap *reg = cs35l41->regmap;
	int ret = 0;

	switch (action) {
	case HDA_GEN_PCM_ACT_OPEN:
		regmap_multi_reg_write(reg, cs35l41_hda_config, ARRAY_SIZE(cs35l41_hda_config));
		ret = regmap_update_bits(reg, CS35L41_PWR_CTRL2,
					 CS35L41_AMP_EN_MASK, 1 << CS35L41_AMP_EN_SHIFT);
		if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST)
			regmap_write(reg, CS35L41_GPIO1_CTRL1, 0x00008001);
		break;
	case HDA_GEN_PCM_ACT_PREPARE:
		ret = cs35l41_global_enable(reg, cs35l41->hw_cfg.bst_type, 1);
		break;
	case HDA_GEN_PCM_ACT_CLEANUP:
		regmap_multi_reg_write(reg, cs35l41_hda_mute, ARRAY_SIZE(cs35l41_hda_mute));
		ret = cs35l41_global_enable(reg, cs35l41->hw_cfg.bst_type, 0);
		break;
	case HDA_GEN_PCM_ACT_CLOSE:
		ret = regmap_update_bits(reg, CS35L41_PWR_CTRL2,
					 CS35L41_AMP_EN_MASK, 0 << CS35L41_AMP_EN_SHIFT);
		if (cs35l41->hw_cfg.bst_type == CS35L41_EXT_BOOST)
			regmap_write(reg, CS35L41_GPIO1_CTRL1, 0x00000001);
		cs35l41_irq_release(cs35l41);
		break;
	default:
		dev_warn(cs35l41->dev, "Playback action not supported: %d\n", action);
		break;
	}

	if (ret)
		dev_err(cs35l41->dev, "Regmap access fail: %d\n", ret);
}

static int cs35l41_hda_channel_map(struct device *dev, unsigned int tx_num, unsigned int *tx_slot,
				    unsigned int rx_num, unsigned int *rx_slot)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);
	static const char * const channel_name[] = { "L", "R" };

	if (!cs35l41->amp_name) {
		if (*rx_slot >= ARRAY_SIZE(channel_name))
			return -EINVAL;

		cs35l41->amp_name = devm_kasprintf(cs35l41->dev, GFP_KERNEL, "%s%d",
						   channel_name[*rx_slot], cs35l41->channel_index);
		if (!cs35l41->amp_name)
			return -ENOMEM;
	}

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

static irqreturn_t cs35l41_bst_short_err(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "LBST Error\n");
	set_bit(CS35L41_BST_SHORT_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static irqreturn_t cs35l41_bst_dcm_uvp_err(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "DCM VBST Under Voltage Error\n");
	set_bit(CS35L41_BST_UVP_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static irqreturn_t cs35l41_bst_ovp_err(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "VBST Over Voltage error\n");
	set_bit(CS35L41_BST_OVP_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static irqreturn_t cs35l41_temp_err(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "Over temperature error\n");
	set_bit(CS35L41_TEMP_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static irqreturn_t cs35l41_temp_warn(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "Over temperature warning\n");
	set_bit(CS35L41_TEMP_WARN_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static irqreturn_t cs35l41_amp_short(int irq, void *data)
{
	struct cs35l41_hda *cs35l41 = data;

	dev_crit_ratelimited(cs35l41->dev, "Amp short error\n");
	set_bit(CS35L41_AMP_SHORT_ERR_RLS_SHIFT, &cs35l41->irq_errors);

	return IRQ_HANDLED;
}

static const struct cs35l41_irq cs35l41_irqs[] = {
	CS35L41_IRQ(BST_OVP_ERR, "Boost Overvoltage Error", cs35l41_bst_ovp_err),
	CS35L41_IRQ(BST_DCM_UVP_ERR, "Boost Undervoltage Error", cs35l41_bst_dcm_uvp_err),
	CS35L41_IRQ(BST_SHORT_ERR, "Boost Inductor Short Error", cs35l41_bst_short_err),
	CS35L41_IRQ(TEMP_WARN, "Temperature Warning", cs35l41_temp_warn),
	CS35L41_IRQ(TEMP_ERR, "Temperature Error", cs35l41_temp_err),
	CS35L41_IRQ(AMP_SHORT_ERR, "Amp Short", cs35l41_amp_short),
};

static const struct regmap_irq cs35l41_reg_irqs[] = {
	CS35L41_REG_IRQ(IRQ1_STATUS1, BST_OVP_ERR),
	CS35L41_REG_IRQ(IRQ1_STATUS1, BST_DCM_UVP_ERR),
	CS35L41_REG_IRQ(IRQ1_STATUS1, BST_SHORT_ERR),
	CS35L41_REG_IRQ(IRQ1_STATUS1, TEMP_WARN),
	CS35L41_REG_IRQ(IRQ1_STATUS1, TEMP_ERR),
	CS35L41_REG_IRQ(IRQ1_STATUS1, AMP_SHORT_ERR),
};

static const struct regmap_irq_chip cs35l41_regmap_irq_chip = {
	.name = "cs35l41 IRQ1 Controller",
	.status_base = CS35L41_IRQ1_STATUS1,
	.mask_base = CS35L41_IRQ1_MASK1,
	.ack_base = CS35L41_IRQ1_STATUS1,
	.num_regs = 4,
	.irqs = cs35l41_reg_irqs,
	.num_irqs = ARRAY_SIZE(cs35l41_reg_irqs),
};

static int cs35l41_hda_apply_properties(struct cs35l41_hda *cs35l41)
{
	struct cs35l41_hw_cfg *hw_cfg = &cs35l41->hw_cfg;
	bool using_irq = false;
	int irq, irq_pol;
	int ret;
	int i;

	if (!cs35l41->hw_cfg.valid)
		return -EINVAL;

	ret = cs35l41_init_boost(cs35l41->dev, cs35l41->regmap, hw_cfg);
	if (ret)
		return ret;

	if (hw_cfg->gpio1.valid) {
		switch (hw_cfg->gpio1.func) {
		case CS35L41_NOT_USED:
			break;
		case CS35l41_VSPK_SWITCH:
			hw_cfg->gpio1.func = CS35L41_GPIO1_GPIO;
			hw_cfg->gpio1.out_en = true;
			break;
		case CS35l41_SYNC:
			hw_cfg->gpio1.func = CS35L41_GPIO1_MDSYNC;
			break;
		default:
			dev_err(cs35l41->dev, "Invalid function %d for GPIO1\n",
				hw_cfg->gpio1.func);
			return -EINVAL;
		}
	}

	if (hw_cfg->gpio2.valid) {
		switch (hw_cfg->gpio2.func) {
		case CS35L41_NOT_USED:
			break;
		case CS35L41_INTERRUPT:
			using_irq = true;
			break;
		default:
			dev_err(cs35l41->dev, "Invalid GPIO2 function %d\n", hw_cfg->gpio2.func);
			return -EINVAL;
		}
	}

	irq_pol = cs35l41_gpio_config(cs35l41->regmap, hw_cfg);

	if (cs35l41->irq && using_irq) {
		ret = devm_regmap_add_irq_chip(cs35l41->dev, cs35l41->regmap, cs35l41->irq,
					       IRQF_ONESHOT | IRQF_SHARED | irq_pol,
					       0, &cs35l41_regmap_irq_chip, &cs35l41->irq_data);
		if (ret)
			return ret;

		for (i = 0; i < ARRAY_SIZE(cs35l41_irqs); i++) {
			irq = regmap_irq_get_virq(cs35l41->irq_data, cs35l41_irqs[i].irq);
			if (irq < 0)
				return irq;

			ret = devm_request_threaded_irq(cs35l41->dev, irq, NULL,
							cs35l41_irqs[i].handler,
							IRQF_ONESHOT | IRQF_SHARED | irq_pol,
							cs35l41_irqs[i].name, cs35l41);
			if (ret)
				return ret;
		}
	}

	return cs35l41_hda_channel_map(cs35l41->dev, 0, NULL, 1, &hw_cfg->spk_pos);
}

static int cs35l41_hda_read_acpi(struct cs35l41_hda *cs35l41, const char *hid, int id)
{
	struct cs35l41_hw_cfg *hw_cfg = &cs35l41->hw_cfg;
	u32 values[HDA_MAX_COMPONENTS];
	struct acpi_device *adev;
	struct device *physdev;
	char *property;
	size_t nval;
	int i, ret;

	adev = acpi_dev_get_first_match_dev(hid, NULL, -1);
	if (!adev) {
		dev_err(cs35l41->dev, "Failed to find an ACPI device for %s\n", hid);
		return -ENODEV;
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

	property = "cirrus,speaker-position";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err;
	hw_cfg->spk_pos = values[cs35l41->index];

	cs35l41->channel_index = 0;
	for (i = 0; i < cs35l41->index; i++)
		if (values[i] == hw_cfg->spk_pos)
			cs35l41->channel_index++;

	property = "cirrus,gpio1-func";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err;
	hw_cfg->gpio1.func = values[cs35l41->index];
	hw_cfg->gpio1.valid = true;

	property = "cirrus,gpio2-func";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret)
		goto err;
	hw_cfg->gpio2.func = values[cs35l41->index];
	hw_cfg->gpio2.valid = true;

	property = "cirrus,boost-peak-milliamp";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret == 0)
		hw_cfg->bst_ipk = values[cs35l41->index];
	else
		hw_cfg->bst_ipk = -1;

	property = "cirrus,boost-ind-nanohenry";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret == 0)
		hw_cfg->bst_ind = values[cs35l41->index];
	else
		hw_cfg->bst_ind = -1;

	property = "cirrus,boost-cap-microfarad";
	ret = device_property_read_u32_array(physdev, property, values, nval);
	if (ret == 0)
		hw_cfg->bst_cap = values[cs35l41->index];
	else
		hw_cfg->bst_cap = -1;

	if (hw_cfg->bst_ind > 0 || hw_cfg->bst_cap > 0 || hw_cfg->bst_ipk > 0)
		hw_cfg->bst_type = CS35L41_INT_BOOST;
	else
		hw_cfg->bst_type = CS35L41_EXT_BOOST;

	hw_cfg->valid = true;
	put_device(physdev);

	return 0;

err:
	put_device(physdev);
	dev_err(cs35l41->dev, "Failed property %s: %d\n", property, ret);

	return ret;

no_acpi_dsd:
	/*
	 * Device CLSA0100 doesn't have _DSD so a gpiod_get by the label reset won't work.
	 * And devices created by i2c-multi-instantiate don't have their device struct pointing to
	 * the correct fwnode, so acpi_dev must be used here.
	 * And devm functions expect that the device requesting the resource has the correct
	 * fwnode.
	 */
	if (strncmp(hid, "CLSA0100", 8) != 0)
		return -EINVAL;

	/* check I2C address to assign the index */
	cs35l41->index = id == 0x40 ? 0 : 1;
	cs35l41->hw_cfg.spk_pos = cs35l41->index;
	cs35l41->channel_index = 0;
	cs35l41->reset_gpio = gpiod_get_index(physdev, NULL, 0, GPIOD_OUT_HIGH);
	cs35l41->hw_cfg.bst_type = CS35L41_EXT_BOOST_NO_VSPK_SWITCH;
	hw_cfg->gpio2.func = CS35L41_GPIO2_INT_OPEN_DRAIN;
	hw_cfg->gpio2.valid = true;
	cs35l41->hw_cfg.valid = true;
	put_device(physdev);

	return 0;
}

int cs35l41_hda_probe(struct device *dev, const char *device_name, int id, int irq,
		      struct regmap *regmap)
{
	unsigned int int_sts, regid, reg_revid, mtl_revid, chipid, int_status;
	struct cs35l41_hda *cs35l41;
	int ret;

	BUILD_BUG_ON(ARRAY_SIZE(cs35l41_irqs) != ARRAY_SIZE(cs35l41_reg_irqs));
	BUILD_BUG_ON(ARRAY_SIZE(cs35l41_irqs) != CS35L41_NUM_IRQ);

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	cs35l41 = devm_kzalloc(dev, sizeof(*cs35l41), GFP_KERNEL);
	if (!cs35l41)
		return -ENOMEM;

	cs35l41->dev = dev;
	cs35l41->irq = irq;
	cs35l41->regmap = regmap;
	dev_set_drvdata(dev, cs35l41);

	ret = cs35l41_hda_read_acpi(cs35l41, device_name, id);
	if (ret) {
		dev_err_probe(cs35l41->dev, ret, "Platform not supported %d\n", ret);
		return ret;
	}

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

	ret = cs35l41_hda_apply_properties(cs35l41);
	if (ret)
		goto err;

	ret = component_add(cs35l41->dev, &cs35l41_hda_comp_ops);
	if (ret) {
		dev_err(cs35l41->dev, "Register component failed: %d\n", ret);
		goto err;
	}

	dev_info(cs35l41->dev, "Cirrus Logic CS35L41 (%x), Revision: %02X\n", regid, reg_revid);

	return 0;

err:
	if (cs35l41_safe_reset(cs35l41->regmap, cs35l41->hw_cfg.bst_type))
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
	gpiod_put(cs35l41->reset_gpio);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l41_hda_probe, SND_HDA_SCODEC_CS35L41);

void cs35l41_hda_remove(struct device *dev)
{
	struct cs35l41_hda *cs35l41 = dev_get_drvdata(dev);

	component_del(cs35l41->dev, &cs35l41_hda_comp_ops);

	if (cs35l41_safe_reset(cs35l41->regmap, cs35l41->hw_cfg.bst_type))
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
	gpiod_put(cs35l41->reset_gpio);
}
EXPORT_SYMBOL_NS_GPL(cs35l41_hda_remove, SND_HDA_SCODEC_CS35L41);

MODULE_DESCRIPTION("CS35L41 HDA Driver");
MODULE_AUTHOR("Lucas Tanure, Cirrus Logic Inc, <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
