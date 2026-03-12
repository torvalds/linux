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
#include <linux/property.h>
#include <linux/seq_buf.h>
#include <linux/soundwire/sdw.h>
#include <sound/cs35l56.h>
#include <sound/cs-amp-lib.h>
#include "cs35l56.h"

KUNIT_DEFINE_ACTION_WRAPPER(faux_device_destroy_wrapper, faux_device_destroy,
			    struct faux_device *)

KUNIT_DEFINE_ACTION_WRAPPER(software_node_unregister_node_group_wrapper,
			    software_node_unregister_node_group,
			    const struct software_node * const *)

KUNIT_DEFINE_ACTION_WRAPPER(software_node_unregister_wrapper,
			    software_node_unregister,
			    const struct software_node *)

KUNIT_DEFINE_ACTION_WRAPPER(device_remove_software_node_wrapper,
			    device_remove_software_node,
			    struct device *)

struct cs35l56_test_priv {
	struct faux_device *amp_dev;
	struct cs35l56_private *cs35l56_priv;

	const char *ssidexv2;

	bool read_onchip_spkid_called;
	bool configure_onchip_spkid_pads_called;
};

struct cs35l56_test_param {
	u8 type;
	u8 rev;

	s32 spkid_gpios[4];
	s32 spkid_pulls[4];
};

static const struct software_node cs35l56_test_dev_sw_node =
	SOFTWARE_NODE("SWD1", NULL, NULL);

static const struct software_node cs35l56_test_af01_sw_node =
	SOFTWARE_NODE("AF01", NULL, &cs35l56_test_dev_sw_node);

static const struct software_node *cs35l56_test_dev_and_af01_node_group[] = {
	&cs35l56_test_dev_sw_node,
	&cs35l56_test_af01_sw_node,
	NULL
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

/*
 * Test that cs35l56_process_xu_properties() correctly parses the GPIO and
 * pull values from properties into the arrays in struct cs35l56_base.
 *
 * This test creates the node tree:
 *
 * Node("SWD1") { // top-level device node
 *	Node("AF01") {
 *		Node("mipi-sdca-function-expansion-subproperties") {
 *			property: "01fa-spk-id-gpios-onchip"
 *			property: 01fa-spk-id-gpios-onchip-pull
 *		}
 *	}
 * }
 *
 * Note that in ACPI "mipi-sdca-function-expansion-subproperties" is
 * a special _DSD property that points to a Device(EXT0) node but behaves
 * as an alias of the EXT0 node. The equivalent in software nodes is to
 * create a Node named "mipi-sdca-function-expansion-subproperties" with
 * the properties.
 *
 */
static void cs35l56_test_parse_xu_onchip_spkid(struct kunit *test)
{
	const struct cs35l56_test_param *param = test->param_value;
	struct cs35l56_test_priv *priv = test->priv;
	struct cs35l56_private *cs35l56 = priv->cs35l56_priv;
	struct software_node *ext0_node;
	int num_gpios = 0;
	int num_pulls = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(param->spkid_gpios); i++, num_gpios++) {
		if (param->spkid_gpios[i] < 0)
			break;
	}
	KUNIT_ASSERT_LE(test, num_gpios, ARRAY_SIZE(cs35l56->base.onchip_spkid_gpios));

	for (i = 0; i < ARRAY_SIZE(param->spkid_pulls); i++, num_pulls++) {
		if (param->spkid_pulls[i] < 0)
			break;
	}
	KUNIT_ASSERT_LE(test, num_pulls, ARRAY_SIZE(cs35l56->base.onchip_spkid_pulls));

	const struct property_entry ext0_props[] = {
		PROPERTY_ENTRY_U32_ARRAY_LEN("01fa-spk-id-gpios-onchip",
					     param->spkid_gpios, num_gpios),
		PROPERTY_ENTRY_U32_ARRAY_LEN("01fa-spk-id-gpios-onchip-pull",
					     param->spkid_pulls, num_pulls),
		{ }
	};

	KUNIT_ASSERT_EQ(test,
			software_node_register_node_group(cs35l56_test_dev_and_af01_node_group),
			0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test,
						  software_node_unregister_node_group_wrapper,
						  cs35l56_test_dev_and_af01_node_group),
			0);

	ext0_node = kunit_kzalloc(test, sizeof(*ext0_node), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ext0_node);
	*ext0_node = SOFTWARE_NODE("mipi-sdca-function-expansion-subproperties",
				    ext0_props, &cs35l56_test_af01_sw_node);

	KUNIT_ASSERT_EQ(test, software_node_register(ext0_node), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test,
						  software_node_unregister_wrapper,
						  ext0_node),
			0);

	KUNIT_ASSERT_EQ(test,
			device_add_software_node(cs35l56->base.dev, &cs35l56_test_dev_sw_node), 0);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test,
						  device_remove_software_node_wrapper,
						  cs35l56->base.dev));

	KUNIT_EXPECT_EQ(test, cs35l56_process_xu_properties(cs35l56), 0);

	KUNIT_EXPECT_EQ(test, cs35l56->base.num_onchip_spkid_gpios, num_gpios);
	KUNIT_EXPECT_EQ(test, cs35l56->base.num_onchip_spkid_pulls, num_pulls);

	for (i = 0; i < ARRAY_SIZE(param->spkid_gpios); i++) {
		if (param->spkid_gpios[i] < 0)
			break;

		/*
		 * cs35l56_process_xu_properties() stores the GPIO numbers
		 * zero-based, which is one less than the value in the property.
		 */
		KUNIT_EXPECT_EQ_MSG(test, cs35l56->base.onchip_spkid_gpios[i],
				    param->spkid_gpios[i] - 1,
				    "i=%d", i);
	}

	for (i = 0; i < ARRAY_SIZE(param->spkid_pulls); i++) {
		if (param->spkid_pulls[i] < 0)
			break;

		KUNIT_EXPECT_EQ_MSG(test, cs35l56->base.onchip_spkid_pulls[i],
				    param->spkid_pulls[i], "i=%d", i);
	}
}

static int cs35l56_test_dummy_read_onchip_spkid(struct cs35l56_base *cs35l56_base)
{
	struct kunit *test = kunit_get_current_test();
	struct cs35l56_test_priv *priv = test->priv;

	priv->read_onchip_spkid_called = true;

	return 4;
}

static int cs35l56_test_dummy_configure_onchip_spkid_pads(struct cs35l56_base *cs35l56_base)
{
	struct kunit *test = kunit_get_current_test();
	struct cs35l56_test_priv *priv = test->priv;

	priv->configure_onchip_spkid_pads_called = true;

	return 0;
}

static void cs35l56_test_set_fw_name_reads_onchip_spkid(struct kunit *test)
{
	struct cs35l56_test_priv *priv = test->priv;
	struct cs35l56_private *cs35l56 = priv->cs35l56_priv;

	/* Provide some on-chip GPIOs for spkid */
	cs35l56->base.onchip_spkid_gpios[0] = 1;
	cs35l56->base.num_onchip_spkid_gpios = 1;

	cs35l56->speaker_id = -ENOENT;

	kunit_activate_static_stub(test,
				   cs35l56_configure_onchip_spkid_pads,
				   cs35l56_test_dummy_configure_onchip_spkid_pads);
	kunit_activate_static_stub(test,
				   cs35l56_read_onchip_spkid,
				   cs35l56_test_dummy_read_onchip_spkid);

	priv->configure_onchip_spkid_pads_called = false;
	priv->read_onchip_spkid_called = false;
	KUNIT_EXPECT_EQ(test, cs35l56_set_fw_name(cs35l56->component), 0);
	KUNIT_EXPECT_TRUE(test, priv->configure_onchip_spkid_pads_called);
	KUNIT_EXPECT_TRUE(test, priv->read_onchip_spkid_called);
	KUNIT_EXPECT_EQ(test, cs35l56->speaker_id,
			cs35l56_test_dummy_read_onchip_spkid(&cs35l56->base));
}

static void cs35l56_test_set_fw_name_preserves_spkid_with_onchip_gpios(struct kunit *test)
{
	struct cs35l56_test_priv *priv = test->priv;
	struct cs35l56_private *cs35l56 = priv->cs35l56_priv;

	/* Provide some on-chip GPIOs for spkid */
	cs35l56->base.onchip_spkid_gpios[0] = 1;
	cs35l56->base.num_onchip_spkid_gpios = 1;

	/* Simulate that the driver already got a spkid from somewhere */
	cs35l56->speaker_id = 15;

	KUNIT_EXPECT_EQ(test, cs35l56_set_fw_name(cs35l56->component), 0);
	KUNIT_EXPECT_EQ(test, cs35l56->speaker_id, 15);
}

static void cs35l56_test_set_fw_name_preserves_spkid_without_onchip_gpios(struct kunit *test)
{
	struct cs35l56_test_priv *priv = test->priv;
	struct cs35l56_private *cs35l56 = priv->cs35l56_priv;

	cs35l56->base.num_onchip_spkid_gpios = 0;

	/* Simulate that the driver already got a spkid from somewhere */
	cs35l56->speaker_id = 15;

	KUNIT_EXPECT_EQ(test, cs35l56_set_fw_name(cs35l56->component), 0);
	KUNIT_EXPECT_EQ(test, cs35l56->speaker_id, 15);
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
	snd_soc_component_set_drvdata(cs35l56->component, cs35l56);

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

static void cs35l56_test_gpio_param_desc(const struct cs35l56_test_param *param, char *desc)
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

	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "gpios:{%s} pulls:{%s}",
		 seq_buf_str(&gpios), seq_buf_str(&pulls));
}

static const struct cs35l56_test_param cs35l56_test_onchip_spkid_cases[] = {
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
KUNIT_ARRAY_PARAM(cs35l56_test_onchip_spkid,
		  cs35l56_test_onchip_spkid_cases,
		  cs35l56_test_gpio_param_desc);

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

	KUNIT_CASE_PARAM(cs35l56_test_parse_xu_onchip_spkid,
			 cs35l56_test_onchip_spkid_gen_params),

	KUNIT_CASE(cs35l56_test_set_fw_name_reads_onchip_spkid),
	KUNIT_CASE(cs35l56_test_set_fw_name_preserves_spkid_with_onchip_gpios),
	KUNIT_CASE(cs35l56_test_set_fw_name_preserves_spkid_without_onchip_gpios),

	{ } /* terminator */
};

static struct kunit_case cs35l56_test_cases_not_soundwire[] = {
	KUNIT_CASE_PARAM(cs35l56_test_suffix_i2cspi, cs35l56_test_type_rev_all_gen_params),
	KUNIT_CASE_PARAM(cs35l56_test_ssidexv2_suffix_i2cspi,
			 cs35l56_test_type_rev_all_gen_params),

	KUNIT_CASE(cs35l56_test_set_fw_name_reads_onchip_spkid),
	KUNIT_CASE(cs35l56_test_set_fw_name_preserves_spkid_with_onchip_gpios),
	KUNIT_CASE(cs35l56_test_set_fw_name_preserves_spkid_without_onchip_gpios),

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
MODULE_IMPORT_NS("SND_SOC_CS35L56_SHARED");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
MODULE_DESCRIPTION("KUnit test for Cirrus Logic cs35l56 codec driver");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
