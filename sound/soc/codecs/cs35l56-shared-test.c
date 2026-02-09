// SPDX-License-Identifier: GPL-2.0-only
//
// KUnit test for the Cirrus Logic cs35l56-shared module.
//
// Copyright (C) 2026 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/resource.h>
#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/device/faux.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/regmap.h>
#include <linux/seq_buf.h>
#include <sound/cs35l56.h>

struct cs35l56_shared_test_priv {
	struct kunit *test;
	struct faux_device *amp_dev;
	struct regmap *registers;
	struct cs35l56_base *cs35l56_base;
	u8 applied_pad_pull_state[CS35L56_MAX_GPIO];
};

struct cs35l56_shared_test_param {
	int spkid_gpios[4];
	int spkid_pulls[4];
	unsigned long gpio_status;
	int spkid;
};

KUNIT_DEFINE_ACTION_WRAPPER(faux_device_destroy_wrapper, faux_device_destroy,
			    struct faux_device *)

KUNIT_DEFINE_ACTION_WRAPPER(regmap_exit_wrapper, regmap_exit, struct regmap *)

static const struct regmap_config cs35l56_shared_test_mock_registers_regmap = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = CS35L56_DSP1_PMEM_5114,
	.cache_type = REGCACHE_MAPLE,
};

static const struct regmap_bus cs35l56_shared_test_mock_registers_regmap_bus = {
	/* No handlers because it is always in cache-only */
};

static unsigned int cs35l56_shared_test_read_gpio_status(struct cs35l56_shared_test_priv *priv)
{
	const struct cs35l56_shared_test_param *param = priv->test->param_value;
	unsigned int reg_offs, pad_cfg, val;
	unsigned int status = 0;
	unsigned int mask = 1;

	for (reg_offs = 0; reg_offs < CS35L56_MAX_GPIO * sizeof(u32); reg_offs += sizeof(u32)) {
		regmap_read(priv->registers, CS35L56_SYNC_GPIO1_CFG + reg_offs, &pad_cfg);
		regmap_read(priv->registers, CS35L56_GPIO1_CTRL1 + reg_offs, &val);

		/* Only read a value if set as an input pin and as a GPIO */
		val &= (CS35L56_GPIO_DIR_MASK | CS35L56_GPIO_FN_MASK);
		if ((pad_cfg & CS35L56_PAD_GPIO_IE) &&
		    (val == (CS35L56_GPIO_DIR_MASK | CS35L56_GPIO_FN_GPIO)))
			status |= (param->gpio_status & mask);

		mask <<= 1;
	}

	return status;
}

static int cs35l56_shared_test_updt_gpio_pres(struct cs35l56_shared_test_priv *priv,
					      unsigned int reg, unsigned int val)
{
	int i, ret;

	ret = regmap_write(priv->registers, reg, val);
	if (ret)
		return ret;

	if (val & CS35L56_UPDT_GPIO_PRES) {
		/* Simulate transferring register state to internal latches */
		for (i = 0; i < ARRAY_SIZE(priv->applied_pad_pull_state); i++) {
			reg = CS35L56_SYNC_GPIO1_CFG + (i * sizeof(u32));
			regmap_read(priv->registers, reg, &val);
			val = FIELD_GET(CS35L56_PAD_GPIO_PULL_MASK, val);
			priv->applied_pad_pull_state[i] = val;
		}
	}

	return 0;
}

static int cs35l56_shared_test_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct cs35l56_shared_test_priv *priv = context;

	switch (reg) {
	case CS35L56_SYNC_GPIO1_CFG ... CS35L56_ASP2_DIO_GPIO13_CFG:
	case CS35L56_GPIO1_CTRL1 ... CS35L56_GPIO13_CTRL1:
		return regmap_read(priv->registers, reg, val);
	case CS35L56_UPDATE_REGS:
		*val = 0;
		return 0;
	case CS35L56_GPIO_STATUS1:
		*val = cs35l56_shared_test_read_gpio_status(priv);
		return 0;
	default:
		kunit_fail_current_test("Bad regmap read address %#x\n", reg);
		return -EINVAL;
	}
}

static int cs35l56_shared_test_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct cs35l56_shared_test_priv *priv = context;

	switch (reg) {
	case CS35L56_UPDATE_REGS:
		return cs35l56_shared_test_updt_gpio_pres(priv, reg, val);
	case CS35L56_SYNC_GPIO1_CFG ... CS35L56_ASP2_DIO_GPIO13_CFG:
	case CS35L56_GPIO1_CTRL1 ... CS35L56_GPIO13_CTRL1:
		return regmap_write(priv->registers, reg, val);
	default:
		kunit_fail_current_test("Bad regmap write address %#x\n", reg);
		return -EINVAL;
	}
}

static const struct regmap_bus cs35l56_shared_test_regmap_bus = {
	.reg_read = cs35l56_shared_test_reg_read,
	.reg_write = cs35l56_shared_test_reg_write,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

/*
 * Self-test that the mock GPIO registers obey the configuration bits.
 * Other tests rely on the mocked registers only returning a GPIO state
 * if the pin is correctly set as a GPIO input.
 */
static void cs35l56_shared_test_mock_gpio_status_selftest(struct kunit *test)
{
	const struct cs35l56_shared_test_param *param = test->param_value;
	struct cs35l56_shared_test_priv *priv = test->priv;
	struct cs35l56_base *cs35l56_base = priv->cs35l56_base;
	unsigned int reg, val;

	KUNIT_ASSERT_NOT_NULL(test, param);

	/* Set all pins non-GPIO and output. Mock GPIO_STATUS should read 0 */
	for (reg = CS35L56_GPIO1_CTRL1; reg <= CS35L56_GPIO13_CTRL1; reg += sizeof(u32))
		KUNIT_ASSERT_EQ(test, 0, regmap_write(priv->registers, reg, 0));

	/* Set all pads as inputs */
	for (reg = CS35L56_SYNC_GPIO1_CFG; reg <= CS35L56_ASP2_DIO_GPIO13_CFG; reg += sizeof(u32))
		KUNIT_ASSERT_EQ(test, 0, regmap_write(priv->registers, reg, CS35L56_PAD_GPIO_IE));

	KUNIT_ASSERT_EQ(test, 0, regmap_read(cs35l56_base->regmap, CS35L56_GPIO_STATUS1, &val));
	KUNIT_EXPECT_EQ(test, val, 0);

	/* Set all pins as GPIO outputs. Mock GPIO_STATUS should read 0 */
	for (reg = CS35L56_GPIO1_CTRL1; reg <= CS35L56_GPIO13_CTRL1; reg += sizeof(u32))
		KUNIT_ASSERT_EQ(test, 0, regmap_write(priv->registers, reg, CS35L56_GPIO_FN_GPIO));

	KUNIT_ASSERT_EQ(test, 0, regmap_read(cs35l56_base->regmap, CS35L56_GPIO_STATUS1, &val));
	KUNIT_EXPECT_EQ(test, val, 0);

	/* Set all pins as non-GPIO inputs. Mock GPIO_STATUS should read 0 */
	for (reg = CS35L56_GPIO1_CTRL1; reg <= CS35L56_GPIO13_CTRL1; reg += sizeof(u32))
		KUNIT_ASSERT_EQ(test, 0, regmap_write(priv->registers, reg, CS35L56_GPIO_DIR_MASK));

	KUNIT_ASSERT_EQ(test, 0, regmap_read(cs35l56_base->regmap, CS35L56_GPIO_STATUS1, &val));
	KUNIT_EXPECT_EQ(test, val, 0);

	/* Set all pins as GPIO inputs. Mock GPIO_STATUS should match param->gpio_status */
	for (reg = CS35L56_GPIO1_CTRL1; reg <= CS35L56_GPIO13_CTRL1; reg += sizeof(u32))
		KUNIT_ASSERT_EQ(test, 0,
				regmap_write(priv->registers, reg,
					     CS35L56_GPIO_DIR_MASK | CS35L56_GPIO_FN_GPIO));

	KUNIT_ASSERT_EQ(test, 0, regmap_read(cs35l56_base->regmap, CS35L56_GPIO_STATUS1, &val));
	KUNIT_EXPECT_EQ(test, val, param->gpio_status);

	/* Set all pads as outputs. Mock GPIO_STATUS should read 0 */
	for (reg = CS35L56_SYNC_GPIO1_CFG; reg <= CS35L56_ASP2_DIO_GPIO13_CFG; reg += sizeof(u32))
		KUNIT_ASSERT_EQ(test, 0, regmap_write(priv->registers, reg, 0));

	KUNIT_ASSERT_EQ(test, 0, regmap_read(cs35l56_base->regmap, CS35L56_GPIO_STATUS1, &val));
	KUNIT_EXPECT_EQ(test, val, 0);
}

/* Test that the listed chip pins are assembled into a speaker ID integer. */
static void cs35l56_shared_test_get_onchip_speaker_id(struct kunit *test)
{
	const struct cs35l56_shared_test_param *param = test->param_value;
	struct cs35l56_shared_test_priv *priv = test->priv;
	struct cs35l56_base *cs35l56_base = priv->cs35l56_base;
	unsigned int i, reg;

	/* Set all pins non-GPIO and output */
	for (reg = CS35L56_GPIO1_CTRL1; reg <= CS35L56_GPIO13_CTRL1; reg += sizeof(u32))
		KUNIT_ASSERT_EQ(test, 0, regmap_write(priv->registers, reg, 0));

	for (reg = CS35L56_SYNC_GPIO1_CFG; reg <= CS35L56_ASP2_DIO_GPIO13_CFG; reg += sizeof(u32))
		KUNIT_ASSERT_EQ(test, 0, regmap_write(priv->registers, reg, 0));

	/* Init GPIO array */
	for (i = 0; i < ARRAY_SIZE(param->spkid_gpios); i++) {
		if (param->spkid_gpios[i] < 0)
			break;

		cs35l56_base->onchip_spkid_gpios[i] = param->spkid_gpios[i] - 1;
		cs35l56_base->num_onchip_spkid_gpios++;
	}

	cs35l56_base->num_onchip_spkid_pulls = 0;

	KUNIT_EXPECT_EQ(test, cs35l56_configure_onchip_spkid_pads(cs35l56_base), 0);
	KUNIT_EXPECT_EQ(test, cs35l56_read_onchip_spkid(cs35l56_base), param->spkid);
}

/* Test that the listed chip pins and the corresponding pads are configured correctly. */
static void cs35l56_shared_test_onchip_speaker_id_pad_config(struct kunit *test)
{
	const struct cs35l56_shared_test_param *param = test->param_value;
	struct cs35l56_shared_test_priv *priv = test->priv;
	struct cs35l56_base *cs35l56_base = priv->cs35l56_base;
	unsigned int i, reg, val;

	/* Init values in all pin registers */
	for (reg = CS35L56_GPIO1_CTRL1; reg <= CS35L56_GPIO13_CTRL1; reg += sizeof(u32))
		KUNIT_ASSERT_EQ(test, 0, regmap_write(priv->registers, reg, 0));

	for (reg = CS35L56_SYNC_GPIO1_CFG; reg <= CS35L56_ASP2_DIO_GPIO13_CFG; reg += sizeof(u32))
		KUNIT_ASSERT_EQ(test, 0, regmap_write(priv->registers, reg, 0));

	/* Init GPIO array */
	for (i = 0; i < ARRAY_SIZE(param->spkid_gpios); i++) {
		if (param->spkid_gpios[i] < 0)
			break;

		cs35l56_base->onchip_spkid_gpios[i] = param->spkid_gpios[i] - 1;
		cs35l56_base->num_onchip_spkid_gpios++;
	}

	/* Init pulls array */
	for (i = 0; i < ARRAY_SIZE(param->spkid_pulls); i++) {
		if (param->spkid_pulls[i] < 0)
			break;

		cs35l56_base->onchip_spkid_pulls[i] = param->spkid_pulls[i];
		cs35l56_base->num_onchip_spkid_pulls++;
	}

	KUNIT_EXPECT_EQ(test, cs35l56_configure_onchip_spkid_pads(cs35l56_base), 0);

	for (i = 0; i < ARRAY_SIZE(param->spkid_gpios); i++) {
		if (param->spkid_gpios[i] < 0)
			break;

		/* Pad should be an input */
		reg = CS35L56_SYNC_GPIO1_CFG + ((param->spkid_gpios[i] - 1) * sizeof(u32));
		KUNIT_EXPECT_EQ(test, regmap_read(priv->registers, reg, &val), 0);
		KUNIT_EXPECT_EQ(test, val & CS35L56_PAD_GPIO_IE, CS35L56_PAD_GPIO_IE);

		/* Specified pulls should be set, others should be none */
		if (i < cs35l56_base->num_onchip_spkid_pulls) {
			KUNIT_EXPECT_EQ(test, val & CS35L56_PAD_GPIO_PULL_MASK,
					FIELD_PREP(CS35L56_PAD_GPIO_PULL_MASK,
						   param->spkid_pulls[i]));
		} else {
			KUNIT_EXPECT_EQ(test, val & CS35L56_PAD_GPIO_PULL_MASK,
					CS35L56_PAD_PULL_NONE);
		}

		/* Pulls for all specfied GPIOs should have been transferred to AO latch */
		if (i < cs35l56_base->num_onchip_spkid_pulls) {
			KUNIT_EXPECT_EQ(test,
					priv->applied_pad_pull_state[param->spkid_gpios[i] - 1],
					param->spkid_pulls[i]);
		} else {
			KUNIT_EXPECT_EQ(test,
					priv->applied_pad_pull_state[param->spkid_gpios[i] - 1],
					CS35L56_PAD_PULL_NONE);
		}
	}
}

/* Test that the listed chip pins are stashed correctly. */
static void cs35l56_shared_test_stash_onchip_spkid_pins(struct kunit *test)
{
	const struct cs35l56_shared_test_param *param = test->param_value;
	struct cs35l56_shared_test_priv *priv = test->priv;
	struct cs35l56_base *cs35l56_base = priv->cs35l56_base;
	u32 gpios[5], pulls[5];
	int i, num_gpios, num_pulls;

	static_assert(ARRAY_SIZE(gpios) >= ARRAY_SIZE(param->spkid_gpios));
	static_assert(ARRAY_SIZE(pulls) >= ARRAY_SIZE(param->spkid_pulls));

	num_gpios = 0;
	for (i = 0; i < ARRAY_SIZE(param->spkid_gpios); i++) {
		if (param->spkid_gpios[i] < 0)
			break;

		gpios[i] = (u32)param->spkid_gpios[i];
		num_gpios++;
	}

	num_pulls = 0;
	for (i = 0; i < ARRAY_SIZE(param->spkid_pulls); i++) {
		if (param->spkid_pulls[i] < 0)
			break;

		pulls[i] = (u32)param->spkid_pulls[i];
		num_pulls++;
	}

	cs35l56_base->num_onchip_spkid_gpios = 0;
	cs35l56_base->num_onchip_spkid_pulls = 0;

	KUNIT_ASSERT_LE(test, num_gpios, ARRAY_SIZE(cs35l56_base->onchip_spkid_gpios));
	KUNIT_ASSERT_LE(test, num_pulls, ARRAY_SIZE(cs35l56_base->onchip_spkid_pulls));

	KUNIT_EXPECT_EQ(test,
			cs35l56_check_and_save_onchip_spkid_gpios(cs35l56_base,
								  gpios, num_gpios,
								  pulls, num_pulls),
			0);

	KUNIT_EXPECT_EQ(test, cs35l56_base->num_onchip_spkid_gpios, num_gpios);
	KUNIT_EXPECT_EQ(test, cs35l56_base->num_onchip_spkid_pulls, num_pulls);

	/* GPIO numbers are adjusted from 1-based to 0-based */
	for (i = 0; i < num_gpios; i++)
		KUNIT_EXPECT_EQ(test, cs35l56_base->onchip_spkid_gpios[i], gpios[i] - 1);

	for (i = 0; i < num_pulls; i++)
		KUNIT_EXPECT_EQ(test, cs35l56_base->onchip_spkid_pulls[i], pulls[i]);
}

/* Test that illegal GPIO numbers are rejected. */
static void cs35l56_shared_test_stash_onchip_spkid_pins_reject_invalid(struct kunit *test)
{
	struct cs35l56_shared_test_priv *priv = test->priv;
	struct cs35l56_base *cs35l56_base = priv->cs35l56_base;
	u32 gpios[8] = { }, pulls[8] = { };

	KUNIT_EXPECT_LE(test,
			cs35l56_check_and_save_onchip_spkid_gpios(cs35l56_base,
								  gpios, 1,
								  pulls, 0),
			0);

	switch (cs35l56_base->type) {
	case 0x54:
	case 0x56:
	case 0x57:
		gpios[0] = CS35L56_MAX_GPIO + 1;
		break;
	case 0x63:
		gpios[0] = CS35L63_MAX_GPIO + 1;
		break;
	default:
		kunit_fail_current_test("Unsupported type:%#x\n", cs35l56_base->type);
		return;
	}
	KUNIT_EXPECT_LE(test,
			cs35l56_check_and_save_onchip_spkid_gpios(cs35l56_base,
								  gpios, 1,
								  pulls, 0),
			0);

	gpios[0] = 1;
	pulls[0] = 3;
	KUNIT_EXPECT_LE(test,
			cs35l56_check_and_save_onchip_spkid_gpios(cs35l56_base,
								  gpios, 1,
								  pulls, 1),
			0);

	static_assert(ARRAY_SIZE(gpios) > ARRAY_SIZE(cs35l56_base->onchip_spkid_gpios));
	static_assert(ARRAY_SIZE(pulls) > ARRAY_SIZE(cs35l56_base->onchip_spkid_pulls));
	KUNIT_EXPECT_EQ(test,
			cs35l56_check_and_save_onchip_spkid_gpios(cs35l56_base,
								  gpios, ARRAY_SIZE(gpios),
								  pulls, 0),
			-EOVERFLOW);
	KUNIT_EXPECT_EQ(test,
			cs35l56_check_and_save_onchip_spkid_gpios(cs35l56_base,
								  gpios, 1,
								  pulls, ARRAY_SIZE(pulls)),
			-EOVERFLOW);
}

static void cs35l56_shared_test_onchip_speaker_id_not_defined(struct kunit *test)
{
	struct cs35l56_shared_test_priv *priv = test->priv;
	struct cs35l56_base *cs35l56_base = priv->cs35l56_base;

	memset(cs35l56_base->onchip_spkid_gpios, 0, sizeof(cs35l56_base->onchip_spkid_gpios));
	memset(cs35l56_base->onchip_spkid_pulls, 0, sizeof(cs35l56_base->onchip_spkid_pulls));
	cs35l56_base->num_onchip_spkid_gpios = 0;
	cs35l56_base->num_onchip_spkid_pulls = 0;
	KUNIT_EXPECT_EQ(test, cs35l56_configure_onchip_spkid_pads(cs35l56_base), 0);
	KUNIT_EXPECT_EQ(test, cs35l56_read_onchip_spkid(cs35l56_base), -ENOENT);
}

static int cs35l56_shared_test_case_regmap_init(struct kunit *test,
						const struct regmap_config *regmap_config)
{
	struct cs35l56_shared_test_priv *priv = test->priv;
	struct cs35l56_base *cs35l56_base;

	/*
	 * Create a dummy regmap to simulate a register map by holding the
	 * values of all simulated registers in the regmap cache.
	 */
	priv->registers = regmap_init(&priv->amp_dev->dev,
				      &cs35l56_shared_test_mock_registers_regmap_bus,
				      priv,
				      &cs35l56_shared_test_mock_registers_regmap);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->registers);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, regmap_exit_wrapper,
						  priv->registers));
	regcache_cache_only(priv->registers, true);

	/* Create dummy regmap for cs35l56 driver */
	cs35l56_base = priv->cs35l56_base;
	cs35l56_base->regmap = regmap_init(cs35l56_base->dev,
					   &cs35l56_shared_test_regmap_bus,
					   priv,
					   regmap_config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cs35l56_base->regmap);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, regmap_exit_wrapper,
						  cs35l56_base->regmap));

	return 0;
}

static int cs35l56_shared_test_case_base_init(struct kunit *test, u8 type, u8 rev,
					      const struct regmap_config *regmap_config)
{
	struct cs35l56_shared_test_priv *priv;
	int ret;

	KUNIT_ASSERT_NOT_NULL(test, cs_amp_test_hooks);

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	test->priv = priv;
	priv->test = test;

	/* Create dummy amp driver dev */
	priv->amp_dev = faux_device_create("cs35l56_shared_test_drv", NULL, NULL);
	KUNIT_ASSERT_NOT_NULL(test, priv->amp_dev);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test,
						  faux_device_destroy_wrapper,
						  priv->amp_dev));

	priv->cs35l56_base = kunit_kzalloc(test, sizeof(*priv->cs35l56_base), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv->cs35l56_base);
	priv->cs35l56_base->dev = &priv->amp_dev->dev;
	priv->cs35l56_base->type = type;
	priv->cs35l56_base->rev = rev;

	if (regmap_config) {
		ret = cs35l56_shared_test_case_regmap_init(test, regmap_config);
		if (ret)
			return ret;
	}

	return 0;
}

static int cs35l56_shared_test_case_regmap_init_L56_B0_sdw(struct kunit *test)
{
	return cs35l56_shared_test_case_base_init(test, 0x56, 0xb0, &cs35l56_regmap_sdw);
}

static int cs35l56_shared_test_case_regmap_init_L56_B0_spi(struct kunit *test)
{
	return cs35l56_shared_test_case_base_init(test, 0x56, 0xb0, &cs35l56_regmap_spi);
}

static int cs35l56_shared_test_case_regmap_init_L56_B0_i2c(struct kunit *test)
{
	return cs35l56_shared_test_case_base_init(test, 0x56, 0xb0, &cs35l56_regmap_i2c);
}

static int cs35l56_shared_test_case_regmap_init_L56_B2_sdw(struct kunit *test)
{
	return cs35l56_shared_test_case_base_init(test, 0x56, 0xb2, &cs35l56_regmap_sdw);
}

static int cs35l56_shared_test_case_regmap_init_L56_B2_spi(struct kunit *test)
{
	return cs35l56_shared_test_case_base_init(test, 0x56, 0xb2, &cs35l56_regmap_spi);
}

static int cs35l56_shared_test_case_regmap_init_L56_B2_i2c(struct kunit *test)
{
	return cs35l56_shared_test_case_base_init(test, 0x56, 0xb2, &cs35l56_regmap_i2c);
}

static int cs35l56_shared_test_case_regmap_init_L63_A1_sdw(struct kunit *test)
{
	return cs35l56_shared_test_case_base_init(test, 0x63, 0xa1, &cs35l63_regmap_sdw);
}

static void cs35l56_shared_test_gpio_param_desc(const struct cs35l56_shared_test_param *param,
						char *desc)
{
	DECLARE_SEQ_BUF(gpios, 1 + (2 * ARRAY_SIZE(param->spkid_gpios)));
	DECLARE_SEQ_BUF(pulls, 1 + (2 * ARRAY_SIZE(param->spkid_pulls)));
	int i;

	for (i = 0; i < ARRAY_SIZE(param->spkid_gpios); i++) {
		if (param->spkid_gpios[i] < 0)
			break;

		seq_buf_printf(&gpios, "%s%d", (i == 0) ? "" : ",", param->spkid_gpios[i]);
	}

	for (i = 0; i < ARRAY_SIZE(param->spkid_pulls); i++) {
		if (param->spkid_pulls[i] < 0)
			break;

		seq_buf_printf(&pulls, "%s%d", (i == 0) ? "" : ",", param->spkid_pulls[i]);
	}

	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "gpios:{%s} pulls:{%s} status:%#lx spkid:%d",
		 seq_buf_str(&gpios), seq_buf_str(&pulls), param->gpio_status, param->spkid);
}

static const struct cs35l56_shared_test_param cs35l56_shared_test_gpios_selftest_cases[] = {
	{ .spkid_gpios = { -1 }, .gpio_status = GENMASK(12, 0) },
};
KUNIT_ARRAY_PARAM(cs35l56_shared_test_gpios_selftest,
		  cs35l56_shared_test_gpios_selftest_cases,
		  cs35l56_shared_test_gpio_param_desc);

static const struct cs35l56_shared_test_param cs35l56_shared_test_onchip_spkid_cases[] = {
	{ .spkid_gpios = { 1, -1 },	  .gpio_status = 0,			.spkid = 0 },
	{ .spkid_gpios = { 1, -1 },	  .gpio_status = ~BIT(0),		.spkid = 0 },
	{ .spkid_gpios = { 1, -1 },	  .gpio_status = BIT(0),		.spkid = 1 },

	{ .spkid_gpios = { 7, -1 },	  .gpio_status = 0,			.spkid = 0 },
	{ .spkid_gpios = { 7, -1 },	  .gpio_status = ~BIT(6),		.spkid = 0 },
	{ .spkid_gpios = { 7, -1 },	  .gpio_status = BIT(6),		.spkid = 1 },

	{ .spkid_gpios = { 1, 7, -1 },	  .gpio_status = 0,			.spkid = 0 },
	{ .spkid_gpios = { 1, 7, -1 },	  .gpio_status = ~(BIT(0) | BIT(6)),	.spkid = 0 },
	{ .spkid_gpios = { 1, 7, -1 },	  .gpio_status = BIT(6),		.spkid = 1 },
	{ .spkid_gpios = { 1, 7, -1 },	  .gpio_status = BIT(0),		.spkid = 2 },
	{ .spkid_gpios = { 1, 7, -1 },	  .gpio_status = BIT(6) | BIT(0),	.spkid = 3 },

	{ .spkid_gpios = { 7, 1, -1 },	  .gpio_status = 0,			.spkid = 0 },
	{ .spkid_gpios = { 7, 1, -1 },	  .gpio_status = ~(BIT(6) | BIT(0)),	.spkid = 0 },
	{ .spkid_gpios = { 7, 1, -1 },	  .gpio_status = BIT(0),		.spkid = 1 },
	{ .spkid_gpios = { 7, 1, -1 },	  .gpio_status = BIT(6),		.spkid = 2 },
	{ .spkid_gpios = { 7, 1, -1 },	  .gpio_status = BIT(6) | BIT(0),	.spkid = 3 },

	{ .spkid_gpios = { 3, 7, 1, -1 }, .gpio_status = 0,			   .spkid = 0 },
	{ .spkid_gpios = { 3, 7, 1, -1 }, .gpio_status = BIT(0),		   .spkid = 1 },
	{ .spkid_gpios = { 3, 7, 1, -1 }, .gpio_status = BIT(6),		   .spkid = 2 },
	{ .spkid_gpios = { 3, 7, 1, -1 }, .gpio_status = BIT(6) | BIT(0),	   .spkid = 3 },
	{ .spkid_gpios = { 3, 7, 1, -1 }, .gpio_status = BIT(2),		   .spkid = 4 },
	{ .spkid_gpios = { 3, 7, 1, -1 }, .gpio_status = BIT(2) | BIT(0),	   .spkid = 5 },
	{ .spkid_gpios = { 3, 7, 1, -1 }, .gpio_status = BIT(2) | BIT(6),	   .spkid = 6 },
	{ .spkid_gpios = { 3, 7, 1, -1 }, .gpio_status = BIT(2) | BIT(6) | BIT(0), .spkid = 7 },
};
KUNIT_ARRAY_PARAM(cs35l56_shared_test_onchip_spkid, cs35l56_shared_test_onchip_spkid_cases,
		  cs35l56_shared_test_gpio_param_desc);

static const struct cs35l56_shared_test_param cs35l56_shared_test_onchip_spkid_pull_cases[] = {
	{ .spkid_gpios = { 1, -1 },		.spkid_pulls = { 1, -1 }, },
	{ .spkid_gpios = { 1, -1 },		.spkid_pulls = { 2, -1 }, },

	{ .spkid_gpios = { 7, -1 },		.spkid_pulls = { 1, -1 }, },
	{ .spkid_gpios = { 7, -1 },		.spkid_pulls = { 2, -1 }, },

	{ .spkid_gpios = { 1, 7, -1 },		.spkid_pulls = { 1, 1, -1 }, },
	{ .spkid_gpios = { 1, 7, -1 },		.spkid_pulls = { 2, 2, -1 }, },

	{ .spkid_gpios = { 7, 1, -1 },		.spkid_pulls = { 1, 1, -1 }, },
	{ .spkid_gpios = { 7, 1, -1 },		.spkid_pulls = { 2, 2, -1 }, },

	{ .spkid_gpios = { 3, 7, 1, -1 },	.spkid_pulls = { 1, 1, 1, -1 }, },
	{ .spkid_gpios = { 3, 7, 1, -1 },	.spkid_pulls = { 2, 2, 2, -1 }, },
};
KUNIT_ARRAY_PARAM(cs35l56_shared_test_onchip_spkid_pull,
		  cs35l56_shared_test_onchip_spkid_pull_cases,
		  cs35l56_shared_test_gpio_param_desc);

static struct kunit_case cs35l56_shared_test_cases[] = {
	/* Tests for speaker id */
	KUNIT_CASE_PARAM(cs35l56_shared_test_mock_gpio_status_selftest,
			 cs35l56_shared_test_gpios_selftest_gen_params),
	KUNIT_CASE_PARAM(cs35l56_shared_test_get_onchip_speaker_id,
			 cs35l56_shared_test_onchip_spkid_gen_params),
	KUNIT_CASE_PARAM(cs35l56_shared_test_onchip_speaker_id_pad_config,
			 cs35l56_shared_test_onchip_spkid_gen_params),
	KUNIT_CASE_PARAM(cs35l56_shared_test_onchip_speaker_id_pad_config,
			 cs35l56_shared_test_onchip_spkid_pull_gen_params),
	KUNIT_CASE_PARAM(cs35l56_shared_test_stash_onchip_spkid_pins,
			 cs35l56_shared_test_onchip_spkid_pull_gen_params),
	KUNIT_CASE(cs35l56_shared_test_stash_onchip_spkid_pins_reject_invalid),
	KUNIT_CASE(cs35l56_shared_test_onchip_speaker_id_not_defined),
	{ }
};

static struct kunit_suite cs35l56_shared_test_suite_L56_B0_sdw = {
	.name = "snd-soc-cs35l56-shared-test_L56_B0_sdw",
	.init = cs35l56_shared_test_case_regmap_init_L56_B0_sdw,
	.test_cases = cs35l56_shared_test_cases,
};

static struct kunit_suite cs35l56_shared_test_suite_L56_B2_sdw = {
	.name = "snd-soc-cs35l56-shared-test_L56_B2_sdw",
	.init = cs35l56_shared_test_case_regmap_init_L56_B2_sdw,
	.test_cases = cs35l56_shared_test_cases,
};

static struct kunit_suite cs35l56_shared_test_suite_L63_A1_sdw = {
	.name = "snd-soc-cs35l56-shared-test_L63_A1_sdw",
	.init = cs35l56_shared_test_case_regmap_init_L63_A1_sdw,
	.test_cases = cs35l56_shared_test_cases,
};

static struct kunit_suite cs35l56_shared_test_suite_L56_B0_spi = {
	.name = "snd-soc-cs35l56-shared-test_L56_B0_spi",
	.init = cs35l56_shared_test_case_regmap_init_L56_B0_spi,
	.test_cases = cs35l56_shared_test_cases,
};

static struct kunit_suite cs35l56_shared_test_suite_L56_B2_spi = {
	.name = "snd-soc-cs35l56-shared-test_L56_B2_spi",
	.init = cs35l56_shared_test_case_regmap_init_L56_B2_spi,
	.test_cases = cs35l56_shared_test_cases,
};

static struct kunit_suite cs35l56_shared_test_suite_L56_B0_i2c = {
	.name = "snd-soc-cs35l56-shared-test_L56_B0_i2c",
	.init = cs35l56_shared_test_case_regmap_init_L56_B0_i2c,
	.test_cases = cs35l56_shared_test_cases,
};

static struct kunit_suite cs35l56_shared_test_suite_L56_B2_i2c = {
	.name = "snd-soc-cs35l56-shared-test_L56_B2_i2c",
	.init = cs35l56_shared_test_case_regmap_init_L56_B2_i2c,
	.test_cases = cs35l56_shared_test_cases,
};

kunit_test_suites(
	&cs35l56_shared_test_suite_L56_B0_sdw,
	&cs35l56_shared_test_suite_L56_B2_sdw,
	&cs35l56_shared_test_suite_L63_A1_sdw,

	&cs35l56_shared_test_suite_L56_B0_spi,
	&cs35l56_shared_test_suite_L56_B2_spi,

	&cs35l56_shared_test_suite_L56_B0_i2c,
	&cs35l56_shared_test_suite_L56_B2_i2c,
);

MODULE_IMPORT_NS("SND_SOC_CS35L56_SHARED");
MODULE_IMPORT_NS("SND_SOC_CS_AMP_LIB");
MODULE_DESCRIPTION("KUnit test for cs35l56-shared module");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
