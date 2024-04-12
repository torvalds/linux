// SPDX-License-Identifier: GPL-2.0-only
//
// KUnit test for the Cirrus common amplifier library.
//
// Copyright (C) 2024 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/test.h>
#include <kunit/static_stub.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/gpio/driver.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <sound/cs-amp-lib.h>

struct cs_amp_lib_test_priv {
	struct platform_device amp_pdev;

	struct cirrus_amp_efi_data *cal_blob;
	struct list_head ctl_write_list;
};

struct cs_amp_lib_test_ctl_write_entry {
	struct list_head list;
	unsigned int value;
	char name[16];
};

struct cs_amp_lib_test_param {
	int num_amps;
	int amp_index;
};

static void cs_amp_lib_test_init_dummy_cal_blob(struct kunit *test, int num_amps)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	unsigned int blob_size;

	blob_size = offsetof(struct cirrus_amp_efi_data, data) +
		    sizeof(struct cirrus_amp_cal_data) * num_amps;

	priv->cal_blob = kunit_kzalloc(test, blob_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv->cal_blob);

	priv->cal_blob->size = blob_size;
	priv->cal_blob->count = num_amps;

	get_random_bytes(priv->cal_blob->data, sizeof(struct cirrus_amp_cal_data) * num_amps);
}

static u64 cs_amp_lib_test_get_target_uid(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	const struct cs_amp_lib_test_param *param = test->param_value;
	u64 uid;

	uid = priv->cal_blob->data[param->amp_index].calTarget[1];
	uid <<= 32;
	uid |= priv->cal_blob->data[param->amp_index].calTarget[0];

	return uid;
}

/* Redirected get_efi_variable to simulate that the file is too short */
static efi_status_t cs_amp_lib_test_get_efi_variable_nohead(efi_char16_t *name,
							    efi_guid_t *guid,
							    unsigned long *size,
							    void *buf)
{
	if (!buf) {
		*size = offsetof(struct cirrus_amp_efi_data, data) - 1;
		return EFI_BUFFER_TOO_SMALL;
	}

	return EFI_NOT_FOUND;
}

/* Should return -EOVERFLOW if the header is larger than the EFI data */
static void cs_amp_lib_test_cal_data_too_short_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_cal_data result_data;
	int ret;

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_nohead);

	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, 0, 0, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -EOVERFLOW);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);
}

/* Redirected get_efi_variable to simulate that the count is larger than the file */
static efi_status_t cs_amp_lib_test_get_efi_variable_bad_count(efi_char16_t *name,
							       efi_guid_t *guid,
							       unsigned long *size,
							       void *buf)
{
	struct kunit *test = kunit_get_current_test();
	struct cs_amp_lib_test_priv *priv = test->priv;

	if (!buf) {
		/*
		 * Return a size that is shorter than required for the
		 * declared number of entries.
		 */
		*size = priv->cal_blob->size - 1;
		return EFI_BUFFER_TOO_SMALL;
	}

	memcpy(buf, priv->cal_blob, priv->cal_blob->size - 1);

	return EFI_SUCCESS;
}

/* Should return -EOVERFLOW if the entry count is larger than the EFI data */
static void cs_amp_lib_test_cal_count_too_big_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_cal_data result_data;
	int ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, 8);

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_bad_count);

	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, 0, 0, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -EOVERFLOW);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);
}

/* Redirected get_efi_variable to simulate that the variable not found */
static efi_status_t cs_amp_lib_test_get_efi_variable_none(efi_char16_t *name,
							  efi_guid_t *guid,
							  unsigned long *size,
							  void *buf)
{
	return EFI_NOT_FOUND;
}

/* If EFI doesn't contain a cal data variable the result should be -ENOENT */
static void cs_amp_lib_test_no_cal_data_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_cal_data result_data;
	int ret;

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_none);

	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, 0, 0, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);
}

/* Redirected get_efi_variable to simulate reading a cal data blob */
static efi_status_t cs_amp_lib_test_get_efi_variable(efi_char16_t *name,
						     efi_guid_t *guid,
						     unsigned long *size,
						     void *buf)
{
	static const efi_char16_t expected_name[] = L"CirrusSmartAmpCalibrationData";
	static const efi_guid_t expected_guid =
		EFI_GUID(0x02f9af02, 0x7734, 0x4233, 0xb4, 0x3d, 0x93, 0xfe, 0x5a, 0xa3, 0x5d, 0xb3);
	struct kunit *test = kunit_get_current_test();
	struct cs_amp_lib_test_priv *priv = test->priv;

	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, name);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, guid);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, size);

	KUNIT_EXPECT_MEMEQ(test, name, expected_name, sizeof(expected_name));
	KUNIT_EXPECT_MEMEQ(test, guid, &expected_guid, sizeof(expected_guid));

	if (!buf) {
		*size = priv->cal_blob->size;
		return EFI_BUFFER_TOO_SMALL;
	}

	KUNIT_ASSERT_GE_MSG(test, ksize(buf), priv->cal_blob->size, "Buffer to small");

	memcpy(buf, priv->cal_blob, priv->cal_blob->size);

	return EFI_SUCCESS;
}

/* Get cal data block for a given amp, matched by target UID. */
static void cs_amp_lib_test_get_efi_cal_by_uid_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	const struct cs_amp_lib_test_param *param = test->param_value;
	struct cirrus_amp_cal_data result_data;
	u64 target_uid;
	int ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, param->num_amps);

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);

	target_uid = cs_amp_lib_test_get_target_uid(test);
	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, target_uid, -1, &result_data);
	KUNIT_EXPECT_EQ(test, ret, 0);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);

	KUNIT_EXPECT_EQ(test, result_data.calTarget[0], target_uid & 0xFFFFFFFFULL);
	KUNIT_EXPECT_EQ(test, result_data.calTarget[1], target_uid >> 32);
	KUNIT_EXPECT_EQ(test, result_data.calTime[0],
			      priv->cal_blob->data[param->amp_index].calTime[0]);
	KUNIT_EXPECT_EQ(test, result_data.calTime[1],
			      priv->cal_blob->data[param->amp_index].calTime[1]);
	KUNIT_EXPECT_EQ(test, result_data.calAmbient,
			      priv->cal_blob->data[param->amp_index].calAmbient);
	KUNIT_EXPECT_EQ(test, result_data.calStatus,
			      priv->cal_blob->data[param->amp_index].calStatus);
	KUNIT_EXPECT_EQ(test, result_data.calR,
			      priv->cal_blob->data[param->amp_index].calR);
}

/* Get cal data block for a given amp index without checking target UID. */
static void cs_amp_lib_test_get_efi_cal_by_index_unchecked_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	const struct cs_amp_lib_test_param *param = test->param_value;
	struct cirrus_amp_cal_data result_data;
	int ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, param->num_amps);

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);

	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, 0,
					      param->amp_index, &result_data);
	KUNIT_EXPECT_EQ(test, ret, 0);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);

	KUNIT_EXPECT_EQ(test, result_data.calTime[0],
			      priv->cal_blob->data[param->amp_index].calTime[0]);
	KUNIT_EXPECT_EQ(test, result_data.calTime[1],
			      priv->cal_blob->data[param->amp_index].calTime[1]);
	KUNIT_EXPECT_EQ(test, result_data.calAmbient,
			      priv->cal_blob->data[param->amp_index].calAmbient);
	KUNIT_EXPECT_EQ(test, result_data.calStatus,
			      priv->cal_blob->data[param->amp_index].calStatus);
	KUNIT_EXPECT_EQ(test, result_data.calR,
			      priv->cal_blob->data[param->amp_index].calR);
}

/* Get cal data block for a given amp index with checked target UID. */
static void cs_amp_lib_test_get_efi_cal_by_index_checked_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	const struct cs_amp_lib_test_param *param = test->param_value;
	struct cirrus_amp_cal_data result_data;
	u64 target_uid;
	int ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, param->num_amps);

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);

	target_uid = cs_amp_lib_test_get_target_uid(test);
	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, target_uid,
					      param->amp_index, &result_data);
	KUNIT_EXPECT_EQ(test, ret, 0);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);

	KUNIT_EXPECT_EQ(test, result_data.calTime[0],
			      priv->cal_blob->data[param->amp_index].calTime[0]);
	KUNIT_EXPECT_EQ(test, result_data.calTime[1],
			      priv->cal_blob->data[param->amp_index].calTime[1]);
	KUNIT_EXPECT_EQ(test, result_data.calAmbient,
			      priv->cal_blob->data[param->amp_index].calAmbient);
	KUNIT_EXPECT_EQ(test, result_data.calStatus,
			      priv->cal_blob->data[param->amp_index].calStatus);
	KUNIT_EXPECT_EQ(test, result_data.calR,
			      priv->cal_blob->data[param->amp_index].calR);
}

/*
 * Get cal data block for a given amp index with checked target UID.
 * The UID does not match so the result should be -ENOENT.
 */
static void cs_amp_lib_test_get_efi_cal_by_index_uid_mismatch_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	const struct cs_amp_lib_test_param *param = test->param_value;
	struct cirrus_amp_cal_data result_data;
	u64 target_uid;
	int ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, param->num_amps);

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);

	/* Get a target UID that won't match the entry */
	target_uid = ~cs_amp_lib_test_get_target_uid(test);
	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, target_uid,
					      param->amp_index, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);
}

/*
 * Get cal data block for a given amp, where the cal data does not
 * specify calTarget so the lookup falls back to using the index
 */
static void cs_amp_lib_test_get_efi_cal_by_index_fallback_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	const struct cs_amp_lib_test_param *param = test->param_value;
	struct cirrus_amp_cal_data result_data;
	static const u64 bad_target_uid = 0xBADCA100BABABABAULL;
	int i, ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, param->num_amps);

	/* Make all the target values zero so they are ignored */
	for (i = 0; i < priv->cal_blob->count; ++i) {
		priv->cal_blob->data[i].calTarget[0] = 0;
		priv->cal_blob->data[i].calTarget[1] = 0;
	}

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);

	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, bad_target_uid,
					      param->amp_index, &result_data);
	KUNIT_EXPECT_EQ(test, ret, 0);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);

	KUNIT_EXPECT_EQ(test, result_data.calTime[0],
			      priv->cal_blob->data[param->amp_index].calTime[0]);
	KUNIT_EXPECT_EQ(test, result_data.calTime[1],
			      priv->cal_blob->data[param->amp_index].calTime[1]);
	KUNIT_EXPECT_EQ(test, result_data.calAmbient,
			      priv->cal_blob->data[param->amp_index].calAmbient);
	KUNIT_EXPECT_EQ(test, result_data.calStatus,
			      priv->cal_blob->data[param->amp_index].calStatus);
	KUNIT_EXPECT_EQ(test, result_data.calR,
			      priv->cal_blob->data[param->amp_index].calR);
}

/*
 * If the target UID isn't present in the cal data, and there isn't an
 * index to fall back do, the result should be -ENOENT.
 */
static void cs_amp_lib_test_get_efi_cal_uid_not_found_noindex_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_cal_data result_data;
	static const u64 bad_target_uid = 0xBADCA100BABABABAULL;
	int i, ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, 8);

	/* Make all the target values != bad_target_uid */
	for (i = 0; i < priv->cal_blob->count; ++i) {
		priv->cal_blob->data[i].calTarget[0] &= ~(bad_target_uid & 0xFFFFFFFFULL);
		priv->cal_blob->data[i].calTarget[1] &= ~(bad_target_uid >> 32);
	}

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);

	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, bad_target_uid, -1,
					      &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);
}

/*
 * If the target UID isn't present in the cal data, and the index is
 * out of range, the result should be -ENOENT.
 */
static void cs_amp_lib_test_get_efi_cal_uid_not_found_index_not_found_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_cal_data result_data;
	static const u64 bad_target_uid = 0xBADCA100BABABABAULL;
	int i, ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, 8);

	/* Make all the target values != bad_target_uid */
	for (i = 0; i < priv->cal_blob->count; ++i) {
		priv->cal_blob->data[i].calTarget[0] &= ~(bad_target_uid & 0xFFFFFFFFULL);
		priv->cal_blob->data[i].calTarget[1] &= ~(bad_target_uid >> 32);
	}

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);

	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, bad_target_uid, 99,
					      &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);
}

/*
 * If the target UID isn't given, and the index is out of range, the
 * result should be -ENOENT.
 */
static void cs_amp_lib_test_get_efi_cal_no_uid_index_not_found_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_cal_data result_data;
	int ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, 8);

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);

	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, 0, 99, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);
}

/* If neither the target UID or the index is given the result should be -ENOENT. */
static void cs_amp_lib_test_get_efi_cal_no_uid_no_index_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_cal_data result_data;
	int ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, 8);

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);

	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, 0, -1, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);
}

/*
 * If the UID is passed as 0 this must not match an entry with an
 * unpopulated calTarget
 */
static void cs_amp_lib_test_get_efi_cal_zero_not_matched_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_cal_data result_data;
	int i, ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, 8);

	/* Make all the target values zero so they are ignored */
	for (i = 0; i < priv->cal_blob->count; ++i) {
		priv->cal_blob->data[i].calTarget[0] = 0;
		priv->cal_blob->data[i].calTarget[1] = 0;
	}

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);

	ret = cs_amp_get_efi_calibration_data(&priv->amp_pdev.dev, 0, -1, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->get_efi_variable);
}

static const struct cirrus_amp_cal_controls cs_amp_lib_test_calibration_controls = {
	.alg_id =	0x9f210,
	.mem_region =	WMFW_ADSP2_YM,
	.ambient =	"CAL_AMBIENT",
	.calr =		"CAL_R",
	.status =	"CAL_STATUS",
	.checksum =	"CAL_CHECKSUM",
};

static int cs_amp_lib_test_write_cal_coeff(struct cs_dsp *dsp,
					   const struct cirrus_amp_cal_controls *controls,
					   const char *ctl_name, u32 val)
{
	struct kunit *test = kunit_get_current_test();
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cs_amp_lib_test_ctl_write_entry *entry;

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctl_name);
	KUNIT_EXPECT_PTR_EQ(test, controls, &cs_amp_lib_test_calibration_controls);

	entry = kunit_kzalloc(test, sizeof(*entry), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, entry);

	INIT_LIST_HEAD(&entry->list);
	strscpy(entry->name, ctl_name, sizeof(entry->name));
	entry->value = val;

	list_add_tail(&entry->list, &priv->ctl_write_list);

	return 0;
}

static void cs_amp_lib_test_write_cal_data_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cs_amp_lib_test_ctl_write_entry *entry;
	struct cirrus_amp_cal_data data;
	struct cs_dsp *dsp;
	int ret;

	dsp = kunit_kzalloc(test, sizeof(*dsp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dsp);
	dsp->dev = &priv->amp_pdev.dev;

	get_random_bytes(&data, sizeof(data));

	/* Redirect calls to write firmware controls */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->write_cal_coeff,
				   cs_amp_lib_test_write_cal_coeff);

	ret = cs_amp_write_cal_coeffs(dsp, &cs_amp_lib_test_calibration_controls, &data);
	KUNIT_EXPECT_EQ(test, ret, 0);

	kunit_deactivate_static_stub(test, cs_amp_test_hooks->write_cal_coeff);

	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->ctl_write_list), 4);

	/* Checksum control must be written last */
	entry = list_last_entry(&priv->ctl_write_list, typeof(*entry), list);
	KUNIT_EXPECT_STREQ(test, entry->name, cs_amp_lib_test_calibration_controls.checksum);
	KUNIT_EXPECT_EQ(test, entry->value, data.calR + 1);
	list_del(&entry->list);

	entry = list_first_entry(&priv->ctl_write_list, typeof(*entry), list);
	KUNIT_EXPECT_STREQ(test, entry->name, cs_amp_lib_test_calibration_controls.ambient);
	KUNIT_EXPECT_EQ(test, entry->value, data.calAmbient);
	list_del(&entry->list);

	entry = list_first_entry(&priv->ctl_write_list, typeof(*entry), list);
	KUNIT_EXPECT_STREQ(test, entry->name, cs_amp_lib_test_calibration_controls.calr);
	KUNIT_EXPECT_EQ(test, entry->value, data.calR);
	list_del(&entry->list);

	entry = list_first_entry(&priv->ctl_write_list, typeof(*entry), list);
	KUNIT_EXPECT_STREQ(test, entry->name, cs_amp_lib_test_calibration_controls.status);
	KUNIT_EXPECT_EQ(test, entry->value, data.calStatus);
}

static void cs_amp_lib_test_dev_release(struct device *dev)
{
}

static int cs_amp_lib_test_case_init(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv;
	int ret;

	KUNIT_ASSERT_NOT_NULL(test, cs_amp_test_hooks);

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	test->priv = priv;
	INIT_LIST_HEAD(&priv->ctl_write_list);

	/* Create dummy amp driver dev */
	priv->amp_pdev.name = "cs_amp_lib_test_drv";
	priv->amp_pdev.id = -1;
	priv->amp_pdev.dev.release = cs_amp_lib_test_dev_release;
	ret = platform_device_register(&priv->amp_pdev);
	KUNIT_ASSERT_GE_MSG(test, ret, 0, "Failed to register amp platform device\n");

	return 0;
}

static void cs_amp_lib_test_case_exit(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;

	if (priv->amp_pdev.name)
		platform_device_unregister(&priv->amp_pdev);
}

static const struct cs_amp_lib_test_param cs_amp_lib_test_get_cal_param_cases[] = {
	{ .num_amps = 2, .amp_index = 0 },
	{ .num_amps = 2, .amp_index = 1 },

	{ .num_amps = 3, .amp_index = 0 },
	{ .num_amps = 3, .amp_index = 1 },
	{ .num_amps = 3, .amp_index = 2 },

	{ .num_amps = 4, .amp_index = 0 },
	{ .num_amps = 4, .amp_index = 1 },
	{ .num_amps = 4, .amp_index = 2 },
	{ .num_amps = 4, .amp_index = 3 },

	{ .num_amps = 5, .amp_index = 0 },
	{ .num_amps = 5, .amp_index = 1 },
	{ .num_amps = 5, .amp_index = 2 },
	{ .num_amps = 5, .amp_index = 3 },
	{ .num_amps = 5, .amp_index = 4 },

	{ .num_amps = 6, .amp_index = 0 },
	{ .num_amps = 6, .amp_index = 1 },
	{ .num_amps = 6, .amp_index = 2 },
	{ .num_amps = 6, .amp_index = 3 },
	{ .num_amps = 6, .amp_index = 4 },
	{ .num_amps = 6, .amp_index = 5 },

	{ .num_amps = 8, .amp_index = 0 },
	{ .num_amps = 8, .amp_index = 1 },
	{ .num_amps = 8, .amp_index = 2 },
	{ .num_amps = 8, .amp_index = 3 },
	{ .num_amps = 8, .amp_index = 4 },
	{ .num_amps = 8, .amp_index = 5 },
	{ .num_amps = 8, .amp_index = 6 },
	{ .num_amps = 8, .amp_index = 7 },
};

static void cs_amp_lib_test_get_cal_param_desc(const struct cs_amp_lib_test_param *param,
					       char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "num_amps:%d amp_index:%d",
		 param->num_amps, param->amp_index);
}

KUNIT_ARRAY_PARAM(cs_amp_lib_test_get_cal, cs_amp_lib_test_get_cal_param_cases,
		  cs_amp_lib_test_get_cal_param_desc);

static struct kunit_case cs_amp_lib_test_cases[] = {
	/* Tests for getting calibration data from EFI */
	KUNIT_CASE(cs_amp_lib_test_cal_data_too_short_test),
	KUNIT_CASE(cs_amp_lib_test_cal_count_too_big_test),
	KUNIT_CASE(cs_amp_lib_test_no_cal_data_test),
	KUNIT_CASE(cs_amp_lib_test_get_efi_cal_uid_not_found_noindex_test),
	KUNIT_CASE(cs_amp_lib_test_get_efi_cal_uid_not_found_index_not_found_test),
	KUNIT_CASE(cs_amp_lib_test_get_efi_cal_no_uid_index_not_found_test),
	KUNIT_CASE(cs_amp_lib_test_get_efi_cal_no_uid_no_index_test),
	KUNIT_CASE(cs_amp_lib_test_get_efi_cal_zero_not_matched_test),
	KUNIT_CASE_PARAM(cs_amp_lib_test_get_efi_cal_by_uid_test,
			 cs_amp_lib_test_get_cal_gen_params),
	KUNIT_CASE_PARAM(cs_amp_lib_test_get_efi_cal_by_index_unchecked_test,
			 cs_amp_lib_test_get_cal_gen_params),
	KUNIT_CASE_PARAM(cs_amp_lib_test_get_efi_cal_by_index_checked_test,
			 cs_amp_lib_test_get_cal_gen_params),
	KUNIT_CASE_PARAM(cs_amp_lib_test_get_efi_cal_by_index_uid_mismatch_test,
			 cs_amp_lib_test_get_cal_gen_params),
	KUNIT_CASE_PARAM(cs_amp_lib_test_get_efi_cal_by_index_fallback_test,
			 cs_amp_lib_test_get_cal_gen_params),

	/* Tests for writing calibration data */
	KUNIT_CASE(cs_amp_lib_test_write_cal_data_test),

	{ } /* terminator */
};

static struct kunit_suite cs_amp_lib_test_suite = {
	.name = "snd-soc-cs-amp-lib-test",
	.init = cs_amp_lib_test_case_init,
	.exit = cs_amp_lib_test_case_exit,
	.test_cases = cs_amp_lib_test_cases,
};

kunit_test_suite(cs_amp_lib_test_suite);

MODULE_IMPORT_NS(SND_SOC_CS_AMP_LIB);
MODULE_DESCRIPTION("KUnit test for Cirrus Logic amplifier library");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
