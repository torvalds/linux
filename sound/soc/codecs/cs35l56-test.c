// SPDX-License-Identifier: GPL-2.0-only
//
// KUnit test for the Cirrus Logic cs35l56 driver.
//
// Copyright (C) 2026 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/resource.h>
#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/efi.h>
#include <linux/device/faux.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/pci_ids.h>
#include <linux/soundwire/sdw.h>
#include <sound/cs35l56.h>
#include <sound/cs-amp-lib.h>
#include "cs35l56.h"

KUNIT_DEFINE_ACTION_WRAPPER(faux_device_destroy_wrapper, faux_device_destroy,
			    struct faux_device *)

struct cs35l56_test_priv {
	struct faux_device *amp_dev;
	struct cs35l56_private *cs35l56_priv;

	const char *ssidexv2;
};

struct cs35l56_test_param {
	u8 type;
	u8 rev;
};

static const char *cs35l56_test_devm_get_vendor_specific_variant_id_none(struct device *dev,
									 int ssid_vendor,
									 int ssid_device)
{
	return ERR_PTR(-ENOENT);
}

static void cs35l56_test_l56_b0_suffix_sdw(struct kunit *test)
{
	struct cs35l56_test_priv *priv = test->priv;
	struct cs35l56_private *cs35l56 = priv->cs35l56_priv;

	/* Set device type info */
	cs35l56->base.type = 0x56;
	cs35l56->base.rev = 0xb0;

	/* Set the ALSA name prefix */
	cs35l56->component->name_prefix = "AMP1";

	/* Set SoundWire link and UID number */
	cs35l56->sdw_link_num = 1;
	cs35l56->sdw_unique_id = 5;

	kunit_activate_static_stub(test,
				   cs35l56_test_devm_get_vendor_specific_variant_id_none,
				   cs_amp_devm_get_vendor_specific_variant_id);

	KUNIT_EXPECT_EQ(test, 0, cs35l56_set_fw_suffix(cs35l56));

	/* Priority suffix should be the legacy ALSA prefix */
	KUNIT_EXPECT_STREQ(test, cs35l56->dsp.fwf_suffix, "AMP1");

	/* Fallback suffix should be the new SoundWire ID */
	KUNIT_EXPECT_STREQ(test, cs35l56->fallback_fw_suffix, "l1u5");
}

static void cs35l56_test_suffix_sdw(struct kunit *test)
{
	struct cs35l56_test_priv *priv = test->priv;
	struct cs35l56_private *cs35l56 = priv->cs35l56_priv;

	/* Set the ALSA name prefix */
	cs35l56->component->name_prefix = "AMP1";

	/* Set SoundWire link and UID number */
	cs35l56->sdw_link_num = 1;
	cs35l56->sdw_unique_id = 5;

	kunit_activate_static_stub(test,
				   cs35l56_test_devm_get_vendor_specific_variant_id_none,
				   cs_amp_devm_get_vendor_specific_variant_id);

	KUNIT_EXPECT_EQ(test, 0, cs35l56_set_fw_suffix(cs35l56));

	/* Suffix should be the SoundWire ID without a fallback */
	KUNIT_EXPECT_STREQ(test, cs35l56->dsp.fwf_suffix, "l1u5");
	KUNIT_EXPECT_NULL(test, cs35l56->fallback_fw_suffix);
}

static void cs35l56_test_suffix_i2cspi(struct kunit *test)
{
	struct cs35l56_test_priv *priv = test->priv;
	struct cs35l56_private *cs35l56 = priv->cs35l56_priv;

	/* Set the ALSA name prefix */
	cs35l56->component->name_prefix = "AMP1";

	kunit_activate_static_stub(test,
				   cs35l56_test_devm_get_vendor_specific_variant_id_none,
				   cs_amp_devm_get_vendor_specific_variant_id);

	KUNIT_EXPECT_EQ(test, 0, cs35l56_set_fw_suffix(cs35l56));

	/* Suffix strings should not be set: use default wm_adsp suffixing */
	KUNIT_EXPECT_NULL(test, cs35l56->dsp.fwf_suffix);
	KUNIT_EXPECT_NULL(test, cs35l56->fallback_fw_suffix);
}

static efi_status_t cs35l56_test_get_efi_ssidexv2(efi_char16_t *name,
						  efi_guid_t *guid,
						  u32 *returned_attr,
						  unsigned long *size,
						  void *buf)
{
	struct kunit *test = kunit_get_current_test();
	struct cs35l56_test_priv *priv = test->priv;
	unsigned int len;

	KUNIT_ASSERT_NOT_NULL(test, priv->ssidexv2);
	len = strlen(priv->ssidexv2);

	if (*size < len) {
		*size = len;
		return EFI_BUFFER_TOO_SMALL;
	}

	KUNIT_ASSERT_NOT_NULL(test, buf);
	memcpy(buf, priv->ssidexv2, len);

	return EFI_SUCCESS;
}

static void cs35l56_test_ssidexv2_suffix_sdw(struct kunit *test)
{
	struct cs35l56_test_priv *priv = test->priv;
	struct cs35l56_private *cs35l56 = priv->cs35l56_priv;

	/* Set the ALSA name prefix */
	cs35l56->component->name_prefix = "AMP1";

	/* Set SoundWire link and UID number */
	cs35l56->sdw_link_num = 1;
	cs35l56->sdw_unique_id = 5;

	/* Set a SSID to enable lookup of SSIDExV2 */
	snd_soc_card_set_pci_ssid(cs35l56->component->card, PCI_VENDOR_ID_DELL, 0x1234);

	priv->ssidexv2 = "10281234_01_BB_CC";

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs35l56_test_get_efi_ssidexv2);

	KUNIT_EXPECT_EQ(test, 0, cs35l56_set_fw_suffix(cs35l56));

	/* Priority suffix should be the SSIDExV2 string with SoundWire ID */
	KUNIT_EXPECT_STREQ(test, cs35l56->dsp.fwf_suffix, "01-l1u5");

	/* Fallback suffix should be the SoundWireID */
	KUNIT_EXPECT_STREQ(test, cs35l56->fallback_fw_suffix, "l1u5");
}

static void cs35l56_test_ssidexv2_suffix_i2cspi(struct kunit *test)
{
	struct cs35l56_test_priv *priv = test->priv;
	struct cs35l56_private *cs35l56 = priv->cs35l56_priv;

	/* Set the ALSA name prefix */
	cs35l56->component->name_prefix = "AMP1";

	/* Set a SSID to enable lookup of SSIDExV2 */
	snd_soc_card_set_pci_ssid(cs35l56->component->card, PCI_VENDOR_ID_DELL, 0x1234);

	priv->ssidexv2 = "10281234_01_BB_CC";

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs35l56_test_get_efi_ssidexv2);

	KUNIT_EXPECT_EQ(test, 0, cs35l56_set_fw_suffix(cs35l56));

	/* Priority suffix should be the SSIDExV2 string with ALSA name prefix */
	KUNIT_EXPECT_STREQ(test, cs35l56->dsp.fwf_suffix, "01-AMP1");

	/* Fallback suffix should be the ALSA name prefix */
	KUNIT_EXPECT_STREQ(test, cs35l56->fallback_fw_suffix, "AMP1");
}

/*
 * CS35L56 B0 SoundWire should ignore any SSIDExV2 suffix. It isn't needed
 * on any products with B0 silicon and would interfere with the fallback
 * to legacy naming convention for early B0-based laptops.
 */
static void cs35l56_test_l56_b0_ssidexv2_ignored_suffix_sdw(struct kunit *test)
{
	struct cs35l56_test_priv *priv = test->priv;
	struct cs35l56_private *cs35l56 = priv->cs35l56_priv;

	/* Set device type info */
	cs35l56->base.type = 0x56;
	cs35l56->base.rev = 0xb0;

	/* Set the ALSA name prefix */
	cs35l56->component->name_prefix = "AMP1";

	/* Set SoundWire link and UID number */
	cs35l56->sdw_link_num = 1;
	cs35l56->sdw_unique_id = 5;

	/* Set a SSID to enable lookup of SSIDExV2 */
	snd_soc_card_set_pci_ssid(cs35l56->component->card, PCI_VENDOR_ID_DELL, 0x1234);

	priv->ssidexv2 = "10281234_01_BB_CC";

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs35l56_test_get_efi_ssidexv2);

	KUNIT_EXPECT_EQ(test, 0, cs35l56_set_fw_suffix(cs35l56));

	/* Priority suffix should be the legacy ALSA prefix */
	KUNIT_EXPECT_STREQ(test, cs35l56->dsp.fwf_suffix, "AMP1");

	/* Fallback suffix should be the new SoundWire ID */
	KUNIT_EXPECT_STREQ(test, cs35l56->fallback_fw_suffix, "l1u5");
}

static int cs35l56_test_case_init_common(struct kunit *test)
{
	struct cs35l56_test_priv *priv;
	const struct cs35l56_test_param *param = test->param_value;
	struct cs35l56_private *cs35l56;

	KUNIT_ASSERT_NOT_NULL(test, cs_amp_test_hooks);

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	test->priv = priv;

	/* Create dummy amp driver dev */
	priv->amp_dev = faux_device_create("cs35l56_test_drv", NULL, NULL);
	KUNIT_ASSERT_NOT_NULL(test, priv->amp_dev);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test,
						  faux_device_destroy_wrapper,
						  priv->amp_dev));

	/* Construct minimal set of driver structs */
	priv->cs35l56_priv = kunit_kzalloc(test, sizeof(*priv->cs35l56_priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv->cs35l56_priv);
	cs35l56 = priv->cs35l56_priv;
	cs35l56->base.dev = &priv->amp_dev->dev;

	cs35l56->component = kunit_kzalloc(test, sizeof(*cs35l56->component), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, cs35l56->component);
	cs35l56->component->dev = cs35l56->base.dev;

	cs35l56->component->card = kunit_kzalloc(test, sizeof(*cs35l56->component->card),
						 GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, cs35l56->component->card);

	if (param) {
		cs35l56->base.type = param->type;
		cs35l56->base.rev = param->rev;
	}

	return 0;
}

static int cs35l56_test_case_init_soundwire(struct kunit *test)
{
	struct cs35l56_test_priv *priv;
	struct cs35l56_private *cs35l56;
	int ret;

	ret = cs35l56_test_case_init_common(test);
	if (ret)
		return ret;

	priv = test->priv;
	cs35l56 = priv->cs35l56_priv;

	/* Dummy to indicate this is Soundwire */
	cs35l56->sdw_peripheral = kunit_kzalloc(test, sizeof(*cs35l56->sdw_peripheral),
						GFP_KERNEL);
	if (!cs35l56->sdw_peripheral)
		return -ENOMEM;


	return 0;
}

static void cs35l56_test_type_rev_param_desc(const struct cs35l56_test_param *param,
					     char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "type: %02x rev: %02x",
		 param->type, param->rev);
}

static const struct cs35l56_test_param cs35l56_test_type_rev_ex_b0_param_cases[] = {
	{ .type = 0x56, .rev = 0xb2 },
	{ .type = 0x57, .rev = 0xb2 },
	{ .type = 0x63, .rev = 0xa1 },
};
KUNIT_ARRAY_PARAM(cs35l56_test_type_rev_ex_b0, cs35l56_test_type_rev_ex_b0_param_cases,
		  cs35l56_test_type_rev_param_desc);


static const struct cs35l56_test_param cs35l56_test_type_rev_all_param_cases[] = {
	{ .type = 0x56, .rev = 0xb0 },
	{ .type = 0x56, .rev = 0xb2 },
	{ .type = 0x57, .rev = 0xb2 },
	{ .type = 0x63, .rev = 0xa1 },
};
KUNIT_ARRAY_PARAM(cs35l56_test_type_rev_all, cs35l56_test_type_rev_all_param_cases,
		  cs35l56_test_type_rev_param_desc);

static struct kunit_case cs35l56_test_cases_soundwire[] = {
	KUNIT_CASE(cs35l56_test_l56_b0_suffix_sdw),
	KUNIT_CASE_PARAM(cs35l56_test_suffix_sdw, cs35l56_test_type_rev_ex_b0_gen_params),
	KUNIT_CASE_PARAM(cs35l56_test_ssidexv2_suffix_sdw,
			 cs35l56_test_type_rev_ex_b0_gen_params),
	KUNIT_CASE(cs35l56_test_l56_b0_ssidexv2_ignored_suffix_sdw),

	{ } /* terminator */
};

static struct kunit_case cs35l56_test_cases_not_soundwire[] = {
	KUNIT_CASE_PARAM(cs35l56_test_suffix_i2cspi, cs35l56_test_type_rev_all_gen_params),
	KUNIT_CASE_PARAM(cs35l56_test_ssidexv2_suffix_i2cspi,
			 cs35l56_test_type_rev_all_gen_params),

	{ } /* terminator */
};

static struct kunit_suite cs35l56_test_suite_soundwire = {
	.name = "snd-soc-cs35l56-test-soundwire",
	.init = cs35l56_test_case_init_soundwire,
	.test_cases = cs35l56_test_cases_soundwire,
};

static struct kunit_suite cs35l56_test_suite_not_soundwire = {
	.name = "snd-soc-cs35l56-test-not-soundwire",
	.init = cs35l56_test_case_init_common,
	.test_cases = cs35l56_test_cases_not_soundwire,
};

kunit_test_suites(
	&cs35l56_test_suite_soundwire,
	&cs35l56_test_suite_not_soundwire,
);

MODULE_IMPORT_NS("SND_SOC_CS_AMP_LIB");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
MODULE_DESCRIPTION("KUnit test for Cirrus Logic cs35l56 codec driver");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
