// SPDX-License-Identifier: GPL-2.0-only
//
// Test cases for wm_adsp library.
//
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/device.h>
#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <linux/slab.h>
#include "wm_adsp.h"

KUNIT_DEFINE_ACTION_WRAPPER(_put_device_wrapper, put_device, struct device *);

struct wm_adsp_fw_find_test {
	struct wm_adsp dsp;

	struct wm_adsp_fw_files found_fw;
	char searched_fw_files[768];
};

struct wm_adsp_fw_find_test_params {
	const char *part;
	const char *dsp_name;
	const char *fwf_name;
	const char *system_name;
	const char *alsa_name;
	bool wmfw_optional;
	bool bin_mandatory;

	/* If non-NULL this file should be returned as "found" */
	const char *expect_wmfw;

	/* If non-NULL this file should be returned as "found" */
	const char *expect_bin;

	/* Space-separated list of filenames in expected order of searching */
	const char *expected_searches;

	/* NULL-terminated array of pointers to filenames to simulate directory content */
	const char * const *dir_files;
};

/* Dummy struct firmware to return from wm_adsp_request_firmware_files */
static const struct firmware wm_adsp_find_test_dummy_firmware;

static void wm_adsp_fw_find_test_release_firmware_files_stub(struct wm_adsp_fw_files *fw)
{
	/*
	 * fw->wmfw.firmware and fw->coeff.firmware allocated by this KUnit
	 * test are dummies not allocated by the real request_firmware() call
	 * so they must not be passed to release_firmware().
	 * This function replaces wm_adsp_release_firmware_files().
	 */

	if (!fw)
		return;

	kfree(fw->wmfw.filename);
	kfree(fw->coeff.filename);

	fw->wmfw.firmware = NULL;
	fw->coeff.firmware = NULL;
	fw->wmfw.filename = NULL;
	fw->coeff.filename = NULL;
}

static void wm_adsp_free_found_fw(struct kunit *test)
{
	struct wm_adsp_fw_find_test *priv = test->priv;

	wm_adsp_fw_find_test_release_firmware_files_stub(&priv->found_fw);
}

/* Simple lookup of a filename in a list of names */
static int wm_adsp_fw_find_test_firmware_request_simple_stub(const struct firmware **firmware,
							     const char *filename,
							     struct device *dev)
{
	struct kunit *test = kunit_get_current_test();
	const struct wm_adsp_fw_find_test_params *params = test->param_value;
	int i;

	/* Non-parameterized test? */
	if (!params)
		return -ENOENT;

	if (!params->dir_files)
		return -ENOENT;

	for (i = 0; params->dir_files[i]; i++) {
		if (strcmp(params->dir_files[i], filename) == 0) {
			*firmware = &wm_adsp_find_test_dummy_firmware;
			return 0;
		}
	}

	return -ENOENT;
}

static void wm_adsp_fw_find_test_pick_file(struct kunit *test)
{
	struct wm_adsp_fw_find_test *priv = test->priv;
	const struct wm_adsp_fw_find_test_params *params = test->param_value;
	struct wm_adsp *dsp = &priv->dsp;
	int i, ret;

	/* Concatenate string of dir content for error messages */
	for (i = 0; params->dir_files[i]; i++) {
		strlcat(priv->searched_fw_files, params->dir_files[i],
			sizeof(priv->searched_fw_files));
		strlcat(priv->searched_fw_files, ";",
			sizeof(priv->searched_fw_files));
	}

	dsp->cs_dsp.name = params->dsp_name;
	dsp->part = params->part;
	dsp->fwf_name = params->fwf_name;
	dsp->system_name = params->system_name;
	dsp->component->name_prefix = params->alsa_name;
	dsp->wmfw_optional = params->wmfw_optional;
	dsp->bin_mandatory = params->bin_mandatory;

	kunit_activate_static_stub(test,
				   wm_adsp_firmware_request,
				   wm_adsp_fw_find_test_firmware_request_simple_stub);
	kunit_activate_static_stub(test,
				   wm_adsp_release_firmware_files,
				   wm_adsp_fw_find_test_release_firmware_files_stub);

	ret = wm_adsp_request_firmware_files(dsp, &priv->found_fw);
	kunit_deactivate_static_stub(test, wm_adsp_firmware_request);
	kunit_deactivate_static_stub(test, wm_adsp_release_firmware_files);

	KUNIT_EXPECT_EQ_MSG(test, ret,
			    (params->expect_wmfw || params->expect_bin) ? 0 : -ENOENT,
			    "%s\n", priv->searched_fw_files);

	KUNIT_EXPECT_EQ_MSG(test, !!priv->found_fw.wmfw.filename, !!params->expect_wmfw,
			    "%s\n", priv->searched_fw_files);
	KUNIT_EXPECT_EQ_MSG(test, !!priv->found_fw.coeff.filename, !!params->expect_bin,
			    "%s\n", priv->searched_fw_files);

	if (params->expect_wmfw) {
		KUNIT_EXPECT_STREQ_MSG(test, priv->found_fw.wmfw.filename, params->expect_wmfw,
				       "%s\n", priv->searched_fw_files);
	}

	if (params->expect_bin) {
		KUNIT_EXPECT_STREQ_MSG(test, priv->found_fw.coeff.filename, params->expect_bin,
				       "%s\n", priv->searched_fw_files);
	}
}

static int wm_adsp_fw_find_test_firmware_request_stub(const struct firmware **firmware,
						      const char *filename,
						      struct device *dev)
{
	struct kunit *test = kunit_get_current_test();
	const struct wm_adsp_fw_find_test_params *params = test->param_value;
	struct wm_adsp_fw_find_test *priv = test->priv;

	/*
	 * Searches are accumulated as a single string of space-separated names.
	 * The list of expected searches are stored the same way in
	 * struct wm_adsp_fw_find_test_params. This allows for comparision using
	 * a simple KUNIT_EXPECT_STREQ(), which avoids the risk of bugs in a
	 * more complex custom comparison.
	 */
	if (priv->searched_fw_files[0] != '\0')
		strlcat(priv->searched_fw_files, " ", sizeof(priv->searched_fw_files));

	strlcat(priv->searched_fw_files, filename, sizeof(priv->searched_fw_files));

	/* Non-parameterized test? */
	if (!params)
		return -ENOENT;

	if (params->expect_wmfw && (strcmp(filename, params->expect_wmfw) == 0)) {
		*firmware = &wm_adsp_find_test_dummy_firmware;
		return 0;
	}

	if (params->expect_bin && (strcmp(filename, params->expect_bin) == 0)) {
		*firmware = &wm_adsp_find_test_dummy_firmware;
		return 0;
	}

	return -ENOENT;
}

static void wm_adsp_fw_find_test_search_order(struct kunit *test)
{
	struct wm_adsp_fw_find_test *priv = test->priv;
	const struct wm_adsp_fw_find_test_params *params = test->param_value;
	struct wm_adsp *dsp = &priv->dsp;

	dsp->cs_dsp.name = params->dsp_name;
	dsp->part = params->part;
	dsp->fwf_name = params->fwf_name;
	dsp->system_name = params->system_name;
	dsp->component->name_prefix = params->alsa_name;
	dsp->wmfw_optional = params->wmfw_optional;

	kunit_activate_static_stub(test,
				   wm_adsp_firmware_request,
				   wm_adsp_fw_find_test_firmware_request_stub);
	kunit_activate_static_stub(test,
				   wm_adsp_release_firmware_files,
				   wm_adsp_fw_find_test_release_firmware_files_stub);

	wm_adsp_request_firmware_files(dsp, &priv->found_fw);
	kunit_deactivate_static_stub(test, wm_adsp_firmware_request);
	kunit_deactivate_static_stub(test, wm_adsp_release_firmware_files);

	KUNIT_EXPECT_STREQ(test, priv->searched_fw_files, params->expected_searches);

	KUNIT_EXPECT_EQ(test, !!priv->found_fw.wmfw.filename, !!params->expect_wmfw);
	if (params->expect_wmfw)
		KUNIT_EXPECT_STREQ(test, priv->found_fw.wmfw.filename, params->expect_wmfw);

	KUNIT_EXPECT_EQ(test, !!priv->found_fw.coeff.filename, !!params->expect_bin);
	if (params->expect_bin)
		KUNIT_EXPECT_STREQ(test, priv->found_fw.coeff.filename, params->expect_bin);

	/* Either we get a filename and firmware, or neither */
	KUNIT_EXPECT_EQ(test, !!priv->found_fw.wmfw.filename, !!priv->found_fw.wmfw.firmware);
	KUNIT_EXPECT_EQ(test, !!priv->found_fw.coeff.filename, !!priv->found_fw.coeff.firmware);
}

static void wm_adsp_fw_find_test_find_firmware_byindex(struct kunit *test)
{
	struct wm_adsp_fw_find_test *priv = test->priv;
	struct wm_adsp *dsp = &priv->dsp;
	const char *fw_name;

	dsp->cs_dsp.name = "cs1234";
	dsp->part = "dsp1";

	for (dsp->fw = 0;; dsp->fw++) {
		fw_name = wm_adsp_get_fwf_name_by_index(dsp->fw);
		if (!fw_name)
			break;

		kunit_activate_static_stub(test,
					   wm_adsp_firmware_request,
					   wm_adsp_fw_find_test_firmware_request_stub);
		kunit_activate_static_stub(test,
					   wm_adsp_release_firmware_files,
					   wm_adsp_fw_find_test_release_firmware_files_stub);

		wm_adsp_request_firmware_files(dsp, &priv->found_fw);

		kunit_deactivate_static_stub(test, wm_adsp_firmware_request);
		kunit_deactivate_static_stub(test, wm_adsp_release_firmware_files);

		KUNIT_EXPECT_NOT_NULL_MSG(test,
					  strstr(priv->searched_fw_files, fw_name),
					  "fw#%d Did not find '%s' in '%s'\n",
					  dsp->fw, fw_name, priv->searched_fw_files);

		wm_adsp_free_found_fw(test);
		memset(priv->searched_fw_files, 0, sizeof(priv->searched_fw_files));
	}
}

static int wm_adsp_fw_find_test_case_init(struct kunit *test)
{
	struct wm_adsp_fw_find_test *priv;
	struct device *test_dev;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Require dummy struct snd_soc_component for the alsa name prefix string */
	priv->dsp.component = kunit_kzalloc(test, sizeof(*priv->dsp.component), GFP_KERNEL);
	if (!priv->dsp.component)
		return -ENOMEM;

	test->priv = priv;

	/* Create dummy amp device */
	test_dev = kunit_device_register(test, "wm_adsp_test_drv");
	if (IS_ERR(test_dev))
		return PTR_ERR(test_dev);

	priv->dsp.cs_dsp.dev = get_device(test_dev);
	if (!priv->dsp.cs_dsp.dev)
		return -ENODEV;

	ret = kunit_add_action_or_reset(test, _put_device_wrapper, priv->dsp.cs_dsp.dev);
	if (ret)
		return ret;

	return 0;
}

static void wm_adsp_fw_find_test_case_exit(struct kunit *test)
{
	wm_adsp_free_found_fw(test);
}

static void wm_adsp_fw_find_test_param_desc(const struct wm_adsp_fw_find_test_params *param,
					    char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE,
		 "%s %s fwf_name:%s system:%s alsa_name:%s %s expects:(%s %s)",
		  param->part, param->dsp_name,
		  param->fwf_name ? param->fwf_name : "",
		  param->system_name ? param->system_name : "",
		  param->alsa_name ? param->alsa_name : "",
		  param->wmfw_optional ? "wmfw_optional" : "",
		  param->expect_wmfw ? param->expect_wmfw : "",
		  param->expect_bin ? param->expect_bin : "");
}

/* Cases where firmware file not found. Tests full search sequence. */
static const struct wm_adsp_fw_find_test_params wm_adsp_fw_find_full_search_cases[] = {
	{ /* system name and alsa prefix, wmfw mandatory. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw",
	},
	{ /* system name and alsa prefix, wmfw optional. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* system name only, wmfw mandatory. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw",
	},
	{ /* system name only, wmfw optional. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.wmfw_optional = true,
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},

	/*
	 * TODO: Is this a bug? Device-specific bin is only allowed when there
	 * is a system_name. But if there isn't any meaningful system name on
	 * a product, why can't it load firmware files qualified by alsa prefix?
	 */

	{ /* Alsa prefix, wmfw mandatory. No system name so generic files only. */
		.part = "cs1234", .dsp_name = "dsp1", .alsa_name = "amp1",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw",
	},
	{ /* Alsa prefix, wmfw optional. No system name so generic files only. */
		.part = "cs1234", .dsp_name = "dsp1", .alsa_name = "amp1",
		.wmfw_optional = true,
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},

	{ /* fwf_name, system name and alsa prefix, wmfw mandatory. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .fwf_name = "ao",
		.expected_searches =
			"cirrus/cs1234-ao-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-ao-mbc-vss-abc123.wmfw "
			"cs1234-ao-mbc-vss.wmfw "
			"cirrus/cs1234-ao-mbc-vss.wmfw",
	},
	{ /* fwf_name, system name and alsa prefix, wmfw optional. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .fwf_name = "ao",
		.wmfw_optional = true,
		.expected_searches =
			"cirrus/cs1234-ao-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-ao-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-ao-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-ao-mbc-vss-abc123.bin "
			"cs1234-ao-mbc-vss.wmfw "
			"cirrus/cs1234-ao-mbc-vss.wmfw "
			"cirrus/cs1234-ao-mbc-vss.bin",
	},
};
KUNIT_ARRAY_PARAM(wm_adsp_fw_find_full_search,
		  wm_adsp_fw_find_full_search_cases,
		  wm_adsp_fw_find_test_param_desc);

/* Cases with system name and alsa prefix both given. */
static const struct wm_adsp_fw_find_test_params wm_adsp_fw_find_system_alsaname_cases[] = {
	{ /* Fully-qualified wmfw exists. No bin */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
	},
	{ /* Optional fully-qualified wmfw exists. No bin */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
	},
	{ /* Fully-qualified wmfw and bin exist. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
	},
	{ /* Optional fully-qualified wmfw and fully-qualified bin exist. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
	},
	{ /* wmfw matches system name only. No bin */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
	},
	{ /* Optional wmfw matches system name only. No bin */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
	},
	{ /* wmfw matches system name only. Fully-qualified bin. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
	},
	{ /* Optional wmfw matches system name only. Fully-qualified bin. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
	},
	{ /* wmfw and bin match system name only. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
	},
	{ /* Optional wmfw and bin match system name only. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
	},
	{ /* Optional wmfw not found. bin matches fully-qualified name. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin",
	},
	{ /* Optional wmfw not found. bin matches system name only. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
	},
	{ /* No qualified wmfw. Legacy generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1",
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No qualified optional wmfw. Legacy generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No qualified wmfw. Legacy generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1",
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No qualified optional wmfw. Legacy generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No qualified or legacy wmfw. Generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1",
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No optional qualified or legacy wmfw. Generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No qualified or legacy wmfw. Generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1",
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No optional qualified or legacy wmfw. Generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No optional qualified or generic wmfw. Generic bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123-amp1.bin "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
};
KUNIT_ARRAY_PARAM(wm_adsp_fw_find_system_alsaname,
		  wm_adsp_fw_find_system_alsaname_cases,
		  wm_adsp_fw_find_test_param_desc);

/* Cases with system name but without alsa name prefix. */
static const struct wm_adsp_fw_find_test_params wm_adsp_fw_find_system_cases[] = {
	{ /* Qualified wmfw found. No bin */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
	},
	{ /* Optional qualified wmfw found. No bin */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.wmfw_optional = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
	},
	{ /* Qualified wmfw found. Qualified bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
	},
	{ /* Optional qualified wmfw found. Qualified bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.wmfw_optional = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
	},
	{ /* Optional wmfw not found. Qualified bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.wmfw_optional = true,
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin",
	},
	{ /* No qualified wmfw. Legacy generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No qualified optional wmfw. Legacy generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.wmfw_optional = true,
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No qualified wmfw. Legacy generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No qualified optional wmfw. Legacy generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.wmfw_optional = true,
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No qualified or legacy wmfw. Generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No optional qualified or legacy wmfw. Generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.wmfw_optional = true,
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No qualified or legacy wmfw. Generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No optional qualified or legacy wmfw. Generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.wmfw_optional = true,
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No optional qualified or generic wmfw. Generic bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "ABC123",
		.wmfw_optional = true,
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc123.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc123.bin "
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
};
KUNIT_ARRAY_PARAM(wm_adsp_fw_find_system,
		  wm_adsp_fw_find_system_cases,
		  wm_adsp_fw_find_test_param_desc);

/* Cases without system name but with alsa name prefix. */
static const struct wm_adsp_fw_find_test_params wm_adsp_fw_find_alsaname_cases[] = {
	{ /* Legacy generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .alsa_name = "amp1",
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* wmfw optional. Legacy generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .alsa_name = "amp1",
		.wmfw_optional = true,
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* Legacy generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .alsa_name = "amp1",
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* wmfw optional. Legacy generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .alsa_name = "amp1",
		.wmfw_optional = true,
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No legacy wmfw. Generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .alsa_name = "amp1",
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No legacy wmfw. Optional generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1", .alsa_name = "amp1",
		.wmfw_optional = true,
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No legacy wmfw. Generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .alsa_name = "amp1",
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No legacy wmfw. Optional generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .alsa_name = "amp1",
		.wmfw_optional = true,
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No legacy or generic wmfw. Generic bin found. */
		.part = "cs1234", .dsp_name = "dsp1", .alsa_name = "amp1",
		.wmfw_optional = true,
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
};
KUNIT_ARRAY_PARAM(wm_adsp_fw_find_alsaname,
		  wm_adsp_fw_find_alsaname_cases,
		  wm_adsp_fw_find_test_param_desc);

/* Cases without system name or alsa name prefix. */
static const struct wm_adsp_fw_find_test_params wm_adsp_fw_find_noqual_cases[] = {
	{ /* Legacy generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1",
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* wmfw optional. Legacy generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1",
		.wmfw_optional = true,
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* Legacy generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1",
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* wmfw optional. Legacy generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1",
		.wmfw_optional = true,
		.expect_wmfw =  "cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No legacy wmfw. Generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1",
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No legacy wmfw. Optional generic wmfw found. No bin. */
		.part = "cs1234", .dsp_name = "dsp1",
		.wmfw_optional = true,
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No legacy wmfw. Generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1",
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No legacy wmfw. Optional generic wmfw and bin found. */
		.part = "cs1234", .dsp_name = "dsp1",
		.wmfw_optional = true,
		.expect_wmfw =  "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
	{ /* No legacy or generic wmfw. Generic bin found. */
		.part = "cs1234", .dsp_name = "dsp1",
		.wmfw_optional = true,
		.expect_bin =  "cirrus/cs1234-dsp1-mbc-vss.bin",
		.expected_searches =
			"cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss.bin",
	},
};
KUNIT_ARRAY_PARAM(wm_adsp_fw_find_noqual,
		  wm_adsp_fw_find_noqual_cases,
		  wm_adsp_fw_find_test_param_desc);

/*
 * Tests for filename normalization. The system name and alsa prefix strings
 * should be converted to lower-case and delimiters are converted to '-', except
 * for '.' which is preserved.
 */
static const struct wm_adsp_fw_find_test_params wm_adsp_fw_find_normalization_cases[] = {
	{
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "Vendor",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-vendor.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-vendor.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-vendor.bin",
	},
	{
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "Vendor Device",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-vendor-device.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-vendor-device.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-vendor-device.bin",
	},
	{
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "Vendor_Device",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-vendor-device.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-vendor-device.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-vendor-device.bin",
	},
	{
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "Vendor/Device",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-vendor-device.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-vendor-device.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-vendor-device.bin",
	},
	{
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "1234:56AB",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-1234-56ab.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-1234-56ab.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-1234-56ab.bin",
	},

	{
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "abc",
		.alsa_name = "LEFT",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc-left.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc-left.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc-left.bin",
	},
	{
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "abc",
		.alsa_name = "LEFT AMP",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc-left-amp.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc-left-amp.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc-left-amp.bin",
	},
	{
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "abc",
		.alsa_name = "Left Amp",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc-left-amp.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc-left-amp.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc-left-amp.bin",
	},
	{
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "abc",
		.alsa_name = "Amp_1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc-amp-1.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc-amp-1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc-amp-1.bin",
	},
	{
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "abc",
		.alsa_name = "cs1234.1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc-cs1234.1.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc-cs1234.1.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc-cs1234.1.bin",
	},
	{
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "abc",
		.alsa_name = "Spk/Jack",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-abc-spk-jack.wmfw",
		.expected_searches =
			"cirrus/cs1234-dsp1-mbc-vss-abc-spk-jack.wmfw "
			"cirrus/cs1234-dsp1-mbc-vss-abc-spk-jack.bin",
	},
};
KUNIT_ARRAY_PARAM(wm_adsp_fw_find_normalization,
		  wm_adsp_fw_find_normalization_cases,
		  wm_adsp_fw_find_test_param_desc);

/*
 * Dummy directory content for regression tests.
 * DSP part name and system name are used to select different available
 * files.
 *
 * System:
 * WFBF1111 = wmfw and bin fully-qualified
 * WSBF1111 = wmfw system-qualified, bin fully-qualified
 * WSBS1111 = wmfw and bin system-qualified
 * WFXX1111 = wmfw fully-qualified, bin not present
 * XXBF1111 = wmfw not present, bin fully-qualified
 *
 * Part:
 * cs1234	= for testing fully-qualified configurations
 * cs1234nobin	= generic wmfw without a bin available
 * wm1234	= legacy wmfw and bin
 * wm1234nobin	= legacy wmfw without bin
 */
static const char * const wm_adsp_fw_find_test_dir_all_files[] = {
	"cirrus/cs1234-dsp1-mbc-vss-wfbf1111-amp1.wmfw",
	"cirrus/cs1234-dsp1-mbc-vss-wfbf1111-l1u2.wmfw",
	"cirrus/cs1234-dsp1-mbc-vss-wfbf1111.wmfw",
	"cirrus/cs1234-dsp1-mbc-vss-wsbf1111.wmfw",
	"cirrus/cs1234-dsp1-mbc-vss-wsbs1111.wmfw",
	"cirrus/cs1234-dsp1-mbc-vss-wfxx1111.wmfw",
	"cirrus/cs1234-dsp1-mbc-vss.wmfw",
	"cirrus/cs1234nobin-dsp1-mbc-vss.wmfw",
	"cirrus/wm1234-dsp1-mbc-vss.wmfw",
	"cirrus/wm1234nobin-dsp1-mbc-vss.wmfw",
	"wm1234-dsp1-mbc-vss.wmfw",
	"wm1234nobin-dsp1-mbc-vss.wmfw",
	"cirrus/cs1234-dsp1-mbc-vss-wfbf1111-amp1.bin",
	"cirrus/cs1234-dsp1-mbc-vss-wfbf1111-l1u2.bin",
	"cirrus/cs1234-dsp1-mbc-vss-wsbf1111-amp1.bin",
	"cirrus/cs1234-dsp1-mbc-vss-wsbf1111-l1u2.bin",
	"cirrus/cs1234-dsp1-mbc-vss-wsbs1111.bin",
	"cirrus/cs1234-dsp1-mbc-vss-xxbf1111-amp1.bin",
	"cirrus/cs1234-dsp1-mbc-vss.bin",
	"cirrus/wm1234-dsp1-mbc-vss.bin",
	"wm1234-dsp1-mbc-vss.bin",
	NULL /* terminator */
};

/*
 * Regression testing that a change in the search algorithm doesn't change
 * which file is picked. This doesn't cover every possible combination, only
 * those that are already in use and typical cases.
 *
 * It wouldn't be efficent to fully prove the algorithm this way (too many
 * directory content combinations would be needed, and it only infers what the
 * algorithm searched for, it doesn't prove exactly what searches were made).
 * So the main testing is done by checking for the expected file searches.
 * This regression test is independent of the search algorithm.
 *
 * The main tests already prove that the algorithm only searches for files
 * with the correct qualifiers so we can assume that files with the wrong
 * qualifiers would not be picked and there's no need to test for that here.
 */
static const struct wm_adsp_fw_find_test_params wm_adsp_fw_find_pick_cases[] = {
	/*
	 * Amps
	 */
	{ /* Full info, wmfw and bin fully-qualified */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "WFBF1111",
		.alsa_name = "amp1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-wfbf1111-amp1.wmfw",
		.expect_bin = "cirrus/cs1234-dsp1-mbc-vss-wfbf1111-amp1.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw and bin fully-qualified */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "WFBF1111",
		.alsa_name = "l1u2",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-wfbf1111-l1u2.wmfw",
		.expect_bin = "cirrus/cs1234-dsp1-mbc-vss-wfbf1111-l1u2.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw only system-qualified, bin fully-qualified */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "WSBF1111",
		.alsa_name = "amp1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-wsbf1111.wmfw",
		.expect_bin = "cirrus/cs1234-dsp1-mbc-vss-wsbf1111-amp1.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw only system-qualified, bin fully-qualified */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "WSBF1111",
		.alsa_name = "l1u2",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-wsbf1111.wmfw",
		.expect_bin = "cirrus/cs1234-dsp1-mbc-vss-wsbf1111-l1u2.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw optional but present, and bin fully-qualified */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "WFBF1111",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-wfbf1111-amp1.wmfw",
		.expect_bin = "cirrus/cs1234-dsp1-mbc-vss-wfbf1111-amp1.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw and bin only system-qualified */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "WSBS1111",
		.alsa_name = "amp1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-wsbs1111.wmfw",
		.expect_bin = "cirrus/cs1234-dsp1-mbc-vss-wsbs1111.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw optional but system-qualified wmfm present, bin fully-qualified */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "WSBF1111",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-wsbf1111.wmfw",
		.expect_bin = "cirrus/cs1234-dsp1-mbc-vss-wsbf1111-amp1.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw optional not present, and bin fully-qualified */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "XXBF1111",
		.alsa_name = "amp1", .wmfw_optional = true,
		.expect_bin = "cirrus/cs1234-dsp1-mbc-vss-xxbf1111-amp1.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw and bin fully-qualified, bin mandatory and present */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "WFBF1111",
		.alsa_name = "amp1", .bin_mandatory = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-wfbf1111-amp1.wmfw",
		.expect_bin = "cirrus/cs1234-dsp1-mbc-vss-wfbf1111-amp1.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw and bin fully-qualified, bin mandatory but not present */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "WFXX1111",
		.alsa_name = "amp1", .bin_mandatory = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-wfxx1111.wmfw",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw optional but present, bin mandatory but not present */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "WFXX1111",
		.alsa_name = "amp1", .wmfw_optional = true, .bin_mandatory = true,
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss-wfxx1111.wmfw",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw and bin not present, generic fallbacks are present */
		.part = "cs1234", .dsp_name = "dsp1", .system_name = "XXXX1111",
		.alsa_name = "amp1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin = "cirrus/cs1234-dsp1-mbc-vss.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* Full info, wmfw and bin not present, generic wmfw present */
		.part = "cs1234nobin", .dsp_name = "dsp1", .system_name = "XXXX1111",
		.alsa_name = "amp1",
		.expect_wmfw = "cirrus/cs1234nobin-dsp1-mbc-vss.wmfw",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},

	/*
	 * Codecs
	 */
	{ /* No qualifiers. Generic wmfws exist, legacy should be chosen. */
		.part = "wm1234nobin", .dsp_name = "dsp1",
		.expect_wmfw = "wm1234nobin-dsp1-mbc-vss.wmfw",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* No qualifiers. Generic wmfw and bin exist, legacy should be chosen */
		.part = "wm1234", .dsp_name = "dsp1",
		.expect_wmfw = "wm1234-dsp1-mbc-vss.wmfw",
		.expect_bin = "wm1234-dsp1-mbc-vss.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* No qualifiers. New generic wmfw exists, no legacy files. */
		.part = "cs1234nobin", .dsp_name = "dsp1",
		.expect_wmfw = "cirrus/cs1234nobin-dsp1-mbc-vss.wmfw",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
	{ /* No qualifiers. New generic wmfw and bin exist, no legacy files. */
		.part = "cs1234", .dsp_name = "dsp1",
		.expect_wmfw = "cirrus/cs1234-dsp1-mbc-vss.wmfw",
		.expect_bin = "cirrus/cs1234-dsp1-mbc-vss.bin",
		.dir_files = wm_adsp_fw_find_test_dir_all_files,
	},
};
KUNIT_ARRAY_PARAM(wm_adsp_fw_find_pick,
		  wm_adsp_fw_find_pick_cases,
		  wm_adsp_fw_find_test_param_desc);

static struct kunit_case wm_adsp_fw_find_test_cases[] = {
	KUNIT_CASE_PARAM(wm_adsp_fw_find_test_search_order,
			 wm_adsp_fw_find_full_search_gen_params),

	KUNIT_CASE_PARAM(wm_adsp_fw_find_test_search_order,
			 wm_adsp_fw_find_system_alsaname_gen_params),

	KUNIT_CASE_PARAM(wm_adsp_fw_find_test_search_order,
			 wm_adsp_fw_find_system_gen_params),

	KUNIT_CASE_PARAM(wm_adsp_fw_find_test_search_order,
			 wm_adsp_fw_find_alsaname_gen_params),

	KUNIT_CASE_PARAM(wm_adsp_fw_find_test_search_order,
			 wm_adsp_fw_find_noqual_gen_params),

	KUNIT_CASE_PARAM(wm_adsp_fw_find_test_search_order,
			 wm_adsp_fw_find_normalization_gen_params),

	KUNIT_CASE_PARAM(wm_adsp_fw_find_test_pick_file,
			 wm_adsp_fw_find_pick_gen_params),

	KUNIT_CASE(wm_adsp_fw_find_test_find_firmware_byindex),

	{ } /* terminator */
};

static struct kunit_suite wm_adsp_fw_find_test_suite = {
	.name = "wm-adsp-fw-find",
	.init = wm_adsp_fw_find_test_case_init,
	.exit = wm_adsp_fw_find_test_case_exit,
	.test_cases = wm_adsp_fw_find_test_cases,
};

kunit_test_suite(wm_adsp_fw_find_test_suite);

MODULE_DESCRIPTION("KUnit test for Cirrus Logic wm_adsp driver");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
