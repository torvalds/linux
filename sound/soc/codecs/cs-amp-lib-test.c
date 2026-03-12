// SPDX-License-Identifier: GPL-2.0-only
//
// KUnit test for the Cirrus common amplifier library.
//
// Copyright (C) 2024 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/resource.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <kunit/static_stub.h>
#include <linux/device/faux.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/gpio/driver.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/pci_ids.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <sound/cs-amp-lib.h>

#define CIRRUS_LOGIC_CALIBRATION_EFI_NAME L"CirrusSmartAmpCalibrationData"
#define CIRRUS_LOGIC_CALIBRATION_EFI_GUID \
	EFI_GUID(0x02f9af02, 0x7734, 0x4233, 0xb4, 0x3d, 0x93, 0xfe, 0x5a, 0xa3, 0x5d, 0xb3)

#define LENOVO_SPEAKER_ID_EFI_NAME L"SdwSpeaker"
#define LENOVO_SPEAKER_ID_EFI_GUID \
	EFI_GUID(0x48df970e, 0xe27f, 0x460a, 0xb5, 0x86, 0x77, 0x19, 0x80, 0x1d, 0x92, 0x82)

#define HP_SPEAKER_ID_EFI_NAME L"HPSpeakerID"
#define HP_SPEAKER_ID_EFI_GUID \
	EFI_GUID(0xc49593a4, 0xd099, 0x419b, 0xa2, 0xc3, 0x67, 0xe9, 0x80, 0xe6, 0x1d, 0x1e)

#define HP_CALIBRATION_EFI_NAME L"SmartAmpCalibrationData"
#define HP_CALIBRATION_EFI_GUID \
	EFI_GUID(0x53559579, 0x8753, 0x4f5c, 0x91, 0x30, 0xe8, 0x2a, 0xcf, 0xb8, 0xd8, 0x93)

KUNIT_DEFINE_ACTION_WRAPPER(faux_device_destroy_wrapper, faux_device_destroy,
			    struct faux_device *)

struct cs_amp_lib_test_priv {
	struct faux_device *amp_dev;

	struct cirrus_amp_efi_data *cal_blob;
	struct list_head ctl_write_list;
	u32 efi_attr;
};

struct cs_amp_lib_test_ctl_write_entry {
	struct list_head list;
	unsigned int value;
	char name[16];
};

struct cs_amp_lib_test_param {
	int num_amps;
	int amp_index;
	char *vendor_sysid;
	char *expected_sysid;
};

static struct cirrus_amp_efi_data *cs_amp_lib_test_cal_blob_dup(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_efi_data *temp;

	KUNIT_ASSERT_EQ(test, struct_size(priv->cal_blob, data, priv->cal_blob->count),
			priv->cal_blob->size);
	temp = kunit_kmalloc(test, priv->cal_blob->size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, temp);
	memcpy(temp, priv->cal_blob, priv->cal_blob->size);

	return temp;
}

static void cs_amp_lib_test_init_dummy_cal_blob(struct kunit *test, int num_amps)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	unsigned int blob_size;
	int i;

	blob_size = struct_size(priv->cal_blob, data, num_amps);

	priv->cal_blob = kunit_kzalloc(test, blob_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv->cal_blob);

	priv->cal_blob->size = blob_size;
	priv->cal_blob->count = num_amps;

	get_random_bytes(priv->cal_blob->data, flex_array_size(priv->cal_blob, data, num_amps));

	/* Ensure all timestamps are non-zero to mark the entry valid. */
	for (i = 0; i < num_amps; i++)
		priv->cal_blob->data[i].calTime[0] |= 1;

	/*
	 * Ensure that all UIDs are non-zero and unique.
	 * Make both words non-zero and not equal values, so that
	 * tests can verify that both words were checked or changed.
	 */
	for (i = 0; i < num_amps; i++) {
		*(u8 *)&priv->cal_blob->data[i].calTarget[0] = i + 1;
		*(u8 *)&priv->cal_blob->data[i].calTarget[1] = i;
	}
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
							    u32 *returned_attr,
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

	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, 0, 0, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -EOVERFLOW);
}

/* Redirected get_efi_variable to simulate that the count is larger than the file */
static efi_status_t cs_amp_lib_test_get_efi_variable_bad_count(efi_char16_t *name,
							       efi_guid_t *guid,
							       u32 *returned_attr,
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

	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, 0, 0, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -EOVERFLOW);
}

/* Redirected get_efi_variable to simulate that the variable not found */
static efi_status_t cs_amp_lib_test_get_efi_variable_none(efi_char16_t *name,
							  efi_guid_t *guid,
							  u32 *returned_attr,
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

	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, 0, 0, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);
}

/* Redirected get_efi_variable to simulate reading a cal data blob */
static efi_status_t cs_amp_lib_test_get_efi_variable(efi_char16_t *name,
						     efi_guid_t *guid,
						     u32 *returned_attr,
						     unsigned long *size,
						     void *buf)
{
	static const efi_char16_t expected_name[] = CIRRUS_LOGIC_CALIBRATION_EFI_NAME;
	static const efi_guid_t expected_guid = CIRRUS_LOGIC_CALIBRATION_EFI_GUID;
	struct kunit *test = kunit_get_current_test();
	struct cs_amp_lib_test_priv *priv = test->priv;

	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, name);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, guid);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, size);

	if (memcmp(name, expected_name, sizeof(expected_name)) ||
	    efi_guidcmp(*guid, expected_guid))
		return -EFI_NOT_FOUND;

	if (!buf) {
		*size = priv->cal_blob->size;
		return EFI_BUFFER_TOO_SMALL;
	}

	KUNIT_ASSERT_GE_MSG(test, ksize(buf), priv->cal_blob->size, "Buffer to small");

	memcpy(buf, priv->cal_blob, priv->cal_blob->size);

	if (returned_attr) {
		if (priv->efi_attr)
			*returned_attr = priv->efi_attr;
		else
			*returned_attr = EFI_VARIABLE_NON_VOLATILE |
					 EFI_VARIABLE_BOOTSERVICE_ACCESS |
					 EFI_VARIABLE_RUNTIME_ACCESS;
	}

	return EFI_SUCCESS;
}

#define CS_AMP_LIB_ZERO_FILLED_BLOB_SIZE \
	struct_size_t(struct cirrus_amp_efi_data, data, 8)

/* Redirected get_efi_variable to simulate reading a prealloced zero-filled blob */
static efi_status_t cs_amp_lib_test_get_efi_variable_all_zeros(efi_char16_t *name,
							       efi_guid_t *guid,
							       u32 *returned_attr,
							       unsigned long *size,
							       void *buf)
{
	static const efi_char16_t expected_name[] = CIRRUS_LOGIC_CALIBRATION_EFI_NAME;
	static const efi_guid_t expected_guid = CIRRUS_LOGIC_CALIBRATION_EFI_GUID;
	struct kunit *test = kunit_get_current_test();
	struct cs_amp_lib_test_priv *priv = test->priv;

	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, name);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, guid);

	if (memcmp(name, expected_name, sizeof(expected_name)) ||
	    efi_guidcmp(*guid, expected_guid))
		return -EFI_NOT_FOUND;

	if (!buf) {
		*size = CS_AMP_LIB_ZERO_FILLED_BLOB_SIZE;
		return EFI_BUFFER_TOO_SMALL;
	}

	KUNIT_ASSERT_EQ(test, *size, struct_size(priv->cal_blob, data, 8));
	priv->cal_blob = kunit_kzalloc(test, CS_AMP_LIB_ZERO_FILLED_BLOB_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv->cal_blob);
	memset(buf, 0, CS_AMP_LIB_ZERO_FILLED_BLOB_SIZE);

	if (returned_attr) {
		if (priv->efi_attr)
			*returned_attr = priv->efi_attr;
		else
			*returned_attr = EFI_VARIABLE_NON_VOLATILE |
					 EFI_VARIABLE_BOOTSERVICE_ACCESS |
					 EFI_VARIABLE_RUNTIME_ACCESS;
	}

	return EFI_SUCCESS;
}

static efi_status_t cs_amp_lib_test_get_hp_cal_efi_variable(efi_char16_t *name,
							    efi_guid_t *guid,
							    u32 *returned_attr,
							    unsigned long *size,
							    void *buf)
{
	static const efi_char16_t expected_name[] = L"SmartAmpCalibrationData";
	static const efi_guid_t expected_guid =
		EFI_GUID(0x53559579, 0x8753, 0x4f5c, 0x91, 0x30, 0xe8, 0x2a, 0xcf, 0xb8, 0xd8, 0x93);
	struct kunit *test = kunit_get_current_test();
	struct cs_amp_lib_test_priv *priv = test->priv;

	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, name);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, guid);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, size);

	if (memcmp(name, expected_name, sizeof(expected_name)) ||
	    efi_guidcmp(*guid, expected_guid))
		return -EFI_NOT_FOUND;

	if (!buf) {
		*size = priv->cal_blob->size;
		return EFI_BUFFER_TOO_SMALL;
	}

	KUNIT_ASSERT_GE_MSG(test, ksize(buf), priv->cal_blob->size, "Buffer to small");

	memcpy(buf, priv->cal_blob, priv->cal_blob->size);

	if (returned_attr) {
		*returned_attr = EFI_VARIABLE_NON_VOLATILE |
				 EFI_VARIABLE_BOOTSERVICE_ACCESS |
				 EFI_VARIABLE_RUNTIME_ACCESS;
	}

	return EFI_SUCCESS;
}

/* Get cal data block from HP variable. */
static void cs_amp_lib_test_get_hp_efi_cal(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_cal_data result_data;
	int ret;

	cs_amp_lib_test_init_dummy_cal_blob(test, 2);

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_hp_cal_efi_variable);

	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, 0, 0, &result_data);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_MEMEQ(test, &result_data, &priv->cal_blob->data[0], sizeof(result_data));
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
	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, target_uid, -1, &result_data);
	KUNIT_EXPECT_EQ(test, ret, 0);

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

	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, 0,
					      param->amp_index, &result_data);
	KUNIT_EXPECT_EQ(test, ret, 0);

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
	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, target_uid,
					      param->amp_index, &result_data);
	KUNIT_EXPECT_EQ(test, ret, 0);

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
	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, target_uid,
					      param->amp_index, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);
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

	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, bad_target_uid,
					      param->amp_index, &result_data);
	KUNIT_EXPECT_EQ(test, ret, 0);

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

	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, bad_target_uid, -1,
					      &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);
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

	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, bad_target_uid, 99,
					      &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);
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

	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, 0, 99, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);
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

	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, 0, -1, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);
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

	ret = cs_amp_get_efi_calibration_data(&priv->amp_dev->dev, 0, -1, &result_data);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);
}

/*
 * If an entry has a timestamp of 0 it should be ignored even if it has
 * a matching target UID.
 */
static void cs_amp_lib_test_get_efi_cal_empty_entry_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_cal_data result_data;
	u64 uid;

	cs_amp_lib_test_init_dummy_cal_blob(test, 8);

	/* Mark the 3rd entry invalid by zeroing calTime */
	priv->cal_blob->data[2].calTime[0] = 0;
	priv->cal_blob->data[2].calTime[1] = 0;

	/* Get the UID value of the 3rd entry */
	uid = priv->cal_blob->data[2].calTarget[1];
	uid <<= 32;
	uid |= priv->cal_blob->data[2].calTarget[0];

	/* Redirect calls to get EFI data */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);

	/* Lookup by UID should not find it */
	KUNIT_EXPECT_EQ(test,
			cs_amp_get_efi_calibration_data(&priv->amp_dev->dev,
							uid, -1,
							&result_data),
			-ENOENT);

	/* Get by index should ignore it */
	KUNIT_EXPECT_EQ(test,
			cs_amp_get_efi_calibration_data(&priv->amp_dev->dev,
							0, 2,
							&result_data),
			-ENOENT);
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
	dsp->dev = &priv->amp_dev->dev;

	get_random_bytes(&data, sizeof(data));

	/* Redirect calls to write firmware controls */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->write_cal_coeff,
				   cs_amp_lib_test_write_cal_coeff);

	ret = cs_amp_write_cal_coeffs(dsp, &cs_amp_lib_test_calibration_controls, &data);
	KUNIT_EXPECT_EQ(test, ret, 0);
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

static int cs_amp_lib_test_read_cal_coeff(struct cs_dsp *dsp,
					  const struct cirrus_amp_cal_controls *controls,
					  const char *ctl_name, u32 *val)
{
	struct kunit *test = kunit_get_current_test();

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctl_name);
	KUNIT_EXPECT_PTR_EQ(test, controls, &cs_amp_lib_test_calibration_controls);

	if (strcmp(ctl_name, controls->ambient) == 0)
		*val = 19;
	else if (strcmp(ctl_name, controls->calr) == 0)
		*val = 1077;
	else if (strcmp(ctl_name, controls->status) == 0)
		*val = 2;
	else
		kunit_fail_current_test("Bad control '%s'\n", ctl_name);

	return 0;
}

static void cs_amp_lib_test_read_cal_data_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cirrus_amp_cal_data data = { 0 };
	struct cs_dsp *dsp;
	int ret;

	dsp = kunit_kzalloc(test, sizeof(*dsp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dsp);
	dsp->dev = &priv->amp_dev->dev;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->read_cal_coeff,
				   cs_amp_lib_test_read_cal_coeff);

	ret = cs_amp_read_cal_coeffs(dsp, &cs_amp_lib_test_calibration_controls, &data);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, 19, data.calAmbient);
	KUNIT_EXPECT_EQ(test, 1077, data.calR);
	KUNIT_EXPECT_EQ(test, 2, data.calStatus);
	KUNIT_EXPECT_NE(test, 0, data.calTime[0] | data.calTime[1]);
}

static void cs_amp_lib_test_write_ambient_test(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct cs_amp_lib_test_ctl_write_entry *entry;
	struct cs_dsp *dsp;
	int ret;

	dsp = kunit_kzalloc(test, sizeof(*dsp), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dsp);
	dsp->dev = &priv->amp_dev->dev;

	/* Redirect calls to write firmware controls */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->write_cal_coeff,
				   cs_amp_lib_test_write_cal_coeff);

	ret = cs_amp_write_ambient_temp(dsp, &cs_amp_lib_test_calibration_controls, 18);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, list_count_nodes(&priv->ctl_write_list), 1);

	entry = list_first_entry(&priv->ctl_write_list, typeof(*entry), list);
	KUNIT_EXPECT_STREQ(test, entry->name, cs_amp_lib_test_calibration_controls.ambient);
	KUNIT_EXPECT_EQ(test, entry->value, 18);
}

static efi_status_t cs_amp_lib_test_set_efi_variable(efi_char16_t *name,
						     efi_guid_t *guid,
						     u32 attr,
						     unsigned long size,
						     void *buf)
{
	static const efi_char16_t expected_name[] = CIRRUS_LOGIC_CALIBRATION_EFI_NAME;
	static const efi_guid_t expected_guid = CIRRUS_LOGIC_CALIBRATION_EFI_GUID;
	struct kunit *test = kunit_get_current_test();
	struct cs_amp_lib_test_priv *priv = test->priv;

	KUNIT_ASSERT_NOT_NULL(test, name);
	KUNIT_ASSERT_NOT_NULL(test, guid);

	if (memcmp(name, expected_name, sizeof(expected_name)) ||
	    efi_guidcmp(*guid, expected_guid))
		return -EFI_NOT_FOUND;

	KUNIT_ASSERT_NOT_NULL(test, buf);
	KUNIT_ASSERT_NE(test, 0, size);

	kunit_kfree(test, priv->cal_blob);
	priv->cal_blob = kunit_kmalloc(test, size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv->cal_blob);
	memcpy(priv->cal_blob, buf, size);
	priv->efi_attr = attr;

	return EFI_SUCCESS;
}

static efi_status_t cs_amp_lib_test_set_efi_variable_denied(efi_char16_t *name,
							    efi_guid_t *guid,
							    u32 attr,
							    unsigned long size,
							    void *buf)
{
	return EFI_WRITE_PROTECTED;
}

#define CS_AMP_CAL_DEFAULT_EFI_ATTR			\
		(EFI_VARIABLE_NON_VOLATILE |		\
		 EFI_VARIABLE_BOOTSERVICE_ACCESS |	\
		 EFI_VARIABLE_RUNTIME_ACCESS)

static void cs_amp_lib_test_create_new_cal_efi(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	struct cirrus_amp_cal_data data;
	int i;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_none);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/* For unspecified number of amps */
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, -1, &data));
	KUNIT_EXPECT_EQ(test, CS_AMP_CAL_DEFAULT_EFI_ATTR, priv->efi_attr);
	KUNIT_EXPECT_GE(test, priv->cal_blob->count, 1);
	KUNIT_EXPECT_LE(test, priv->cal_blob->count, 8);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, priv->cal_blob->count),
			priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	for (i = 1; i < priv->cal_blob->count; i++)
		KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[i], sizeof(data)));

	/* For 2 amps */
	priv->cal_blob = NULL;
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 2, &data));
	KUNIT_EXPECT_EQ(test, CS_AMP_CAL_DEFAULT_EFI_ATTR, priv->efi_attr);
	KUNIT_EXPECT_EQ(test, 2, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 2), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[1], sizeof(data)));

	/* For 4 amps */
	priv->cal_blob = NULL;
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 4, &data));
	KUNIT_EXPECT_EQ(test, 4, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 4), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[1], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));

	/* For 6 amps */
	priv->cal_blob = NULL;
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[1], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));
}

static void cs_amp_lib_test_create_new_cal_efi_indexed(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_none);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/* In slot 0 */
	priv->cal_blob = NULL;
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 0, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[1], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));

	/* In slot 1 */
	priv->cal_blob = NULL;
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 1, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[0], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));

	/* In slot 5 */
	priv->cal_blob = NULL;
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 5, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[5], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[0], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[1], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
}

static void cs_amp_lib_test_create_new_cal_efi_indexed_no_max(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	struct cirrus_amp_cal_data data;
	int i;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_none);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/* In slot 0 with unspecified number of amps */
	priv->cal_blob = NULL;
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 0, -1, &data));
	KUNIT_EXPECT_GE(test, priv->cal_blob->count, 1);
	KUNIT_EXPECT_LE(test, priv->cal_blob->count, 8);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, priv->cal_blob->count),
			priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	for (i = 1; i < priv->cal_blob->count; i++)
		KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[i], sizeof(data)));

	/* In slot 1 with unspecified number of amps  */
	priv->cal_blob = NULL;
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 1, -1, &data));
	KUNIT_EXPECT_GE(test, priv->cal_blob->count, 2);
	KUNIT_EXPECT_LE(test, priv->cal_blob->count, 8);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, priv->cal_blob->count),
			priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[0], sizeof(data)));
	for (i = 2; i < priv->cal_blob->count; i++)
		KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[i], sizeof(data)));

	/* In slot 5 with unspecified number of amps  */
	priv->cal_blob = NULL;
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 5, -1, &data));
	KUNIT_EXPECT_GE(test, priv->cal_blob->count, 6);
	KUNIT_EXPECT_LE(test, priv->cal_blob->count, 8);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, priv->cal_blob->count),
			priv->cal_blob->size);
	for (i = 0; (i < 5) && (i < priv->cal_blob->count); i++)
		KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[i], sizeof(data)));

	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[5], sizeof(data));
	for (i = 6; i < priv->cal_blob->count; i++)
		KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[i], sizeof(data)));
}

static void cs_amp_lib_test_grow_append_cal_efi(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const struct cirrus_amp_efi_data *original_blob;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/* Initially 1 used entry grown to 2 entries */
	cs_amp_lib_test_init_dummy_cal_blob(test, 1);
	KUNIT_ASSERT_EQ(test, 1, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 2, &data));
	KUNIT_EXPECT_EQ(test, CS_AMP_CAL_DEFAULT_EFI_ATTR, priv->efi_attr);
	KUNIT_EXPECT_EQ(test, 2, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 2), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));

	/* Initially 1 entry grown to 4 entries */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 1);
	KUNIT_ASSERT_EQ(test, 1, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 4, &data));
	KUNIT_EXPECT_EQ(test, 4, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 4), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));

	/* Initially 2 entries grown to 4 entries */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 2);
	KUNIT_ASSERT_EQ(test, 2, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 4, &data));
	KUNIT_EXPECT_EQ(test, 4, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 4), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));

	/* Initially 1 entry grown to 6 entries */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 1);
	KUNIT_ASSERT_EQ(test, 1, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));

	/* Initially 4 entries grown to 6 entries */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));
}

static void cs_amp_lib_test_grow_append_cal_efi_indexed(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const struct cirrus_amp_efi_data *original_blob;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/* Initially 1 entry grown to 2 entries using slot 1 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 1);
	KUNIT_ASSERT_EQ(test, 1, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 1, 2, &data));
	KUNIT_EXPECT_EQ(test, 2, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 2), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));

	/* Initially 1 entry grown to 6 entries using slot 1 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 1);
	KUNIT_ASSERT_EQ(test, 1, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 1, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));

	/* Initially 2 entries grown to 6 entries using slot 2 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 2);
	KUNIT_ASSERT_EQ(test, 2, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 2, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));

	/* Initially 2 entries grown to 6 entries using slot 4 */
	kunit_kfree(test, original_blob);
	kunit_kfree(test, priv->cal_blob);
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 2);
	KUNIT_ASSERT_EQ(test, 2, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 4, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));
}

static void cs_amp_lib_test_cal_efi_all_zeros_add_first(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	struct cirrus_amp_cal_data data;
	int i;

	/* Simulate a BIOS reserving EFI space that is entirely zero-filled. */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_all_zeros);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/*
	 * Add an entry. The header should be filled in to match the
	 * original EFI variable size.
	 */
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, -1, &data));
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 8), priv->cal_blob->size);
	KUNIT_EXPECT_EQ(test, 8, priv->cal_blob->count);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	for (i = 1; i < priv->cal_blob->count; i++) {
		KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[i].calTime[0]);
		KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[i].calTime[1]);
	}
}

static void cs_amp_lib_test_cal_efi_all_zeros_add_first_no_shrink(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	struct cirrus_amp_cal_data data;
	int i;

	/* Simulate a BIOS reserving EFI space that is entirely zero-filled. */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_all_zeros);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/*
	 * Add an entry. The header should be filled in to match the
	 * original EFI variable size. A number of amps less than the
	 * available preallocated space does not shrink the EFI variable.
	 */
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 4, &data));
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 8), priv->cal_blob->size);
	KUNIT_EXPECT_EQ(test, 8, priv->cal_blob->count);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	for (i = 1; i < priv->cal_blob->count; i++) {
		KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[i].calTime[0]);
		KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[i].calTime[1]);
	}
}

static void cs_amp_lib_test_cal_efi_all_zeros_add_first_indexed(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	struct cirrus_amp_cal_data data;
	int i;

	/* Simulate a BIOS reserving EFI space that is entirely zero-filled. */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_all_zeros);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/*
	 * Write entry to slot 2. The header should be filled in to match
	 * the original EFI variable size.
	 */
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 2, -1, &data));
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 8), priv->cal_blob->size);
	KUNIT_EXPECT_EQ(test, 8, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[0].calTime[0]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[0].calTime[1]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[1].calTime[0]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[1].calTime[1]);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[2], sizeof(data));
	for (i = 3; i < priv->cal_blob->count; i++) {
		KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[i].calTime[0]);
		KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[i].calTime[1]);
	}
}

static void cs_amp_lib_test_cal_efi_all_zeros_add_first_indexed_no_shrink(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	struct cirrus_amp_cal_data data;
	int i;

	/* Simulate a BIOS reserving EFI space that is entirely zero-filled. */
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_all_zeros);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/*
	 * Write entry to slot 2. The header should be filled in to match
	 * the original EFI variable size. A number of amps less than the
	 * available preallocated space does not shrink the EFI variable.
	 */
	get_random_bytes(&data, sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 2, 4, &data));
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 8), priv->cal_blob->size);
	KUNIT_EXPECT_EQ(test, 8, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[0].calTime[0]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[0].calTime[1]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[1].calTime[0]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[1].calTime[1]);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[2], sizeof(data));
	for (i = 3; i < priv->cal_blob->count; i++) {
		KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[i].calTime[0]);
		KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[i].calTime[1]);
	}
}

static void cs_amp_lib_test_grow_append_cal_efi_indexed_no_max(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const struct cirrus_amp_efi_data *original_blob;
	struct cirrus_amp_cal_data data;
	int i;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/* Initially 1 entry adding slot 1 */
	cs_amp_lib_test_init_dummy_cal_blob(test, 1);
	KUNIT_ASSERT_EQ(test, 1, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 1, -1, &data));
	KUNIT_EXPECT_GE(test, priv->cal_blob->count, 2);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, priv->cal_blob->count),
			priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	for (i = 2; i < priv->cal_blob->count; i++)
		KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[i], sizeof(data)));

	/* Initially 1 entry adding slot 3 */
	cs_amp_lib_test_init_dummy_cal_blob(test, 1);
	KUNIT_ASSERT_EQ(test, 1, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 3, -1, &data));
	KUNIT_EXPECT_GE(test, priv->cal_blob->count, 4);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, priv->cal_blob->count),
			priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[1], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[3], sizeof(data));
	for (i = 4; i < priv->cal_blob->count; i++)
		KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[i], sizeof(data)));

	/* Initially 2 entries adding slot 3 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 2);
	KUNIT_ASSERT_EQ(test, 2, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 3, -1, &data));
	KUNIT_EXPECT_GE(test, priv->cal_blob->count, 1);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, priv->cal_blob->count),
			priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[3], sizeof(data));
	for (i = 4; i < priv->cal_blob->count; i++)
		KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[i], sizeof(data)));

	/* Initially 4 entries adding slot 4 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 4, -1, &data));
	KUNIT_EXPECT_GE(test, priv->cal_blob->count, 1);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, priv->cal_blob->count),
			priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[4], sizeof(data));
	for (i = 5; i < priv->cal_blob->count; i++)
		KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[i], sizeof(data)));

	/* Initially 4 entries adding slot 6 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 6, -1, &data));
	KUNIT_EXPECT_GE(test, priv->cal_blob->count, 1);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, priv->cal_blob->count),
			priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[6], sizeof(data));
	for (i = 7; i < priv->cal_blob->count; i++)
		KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[i], sizeof(data)));
}

static void cs_amp_lib_test_grow_cal_efi_replace_indexed(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const struct cirrus_amp_efi_data *original_blob;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/* Initially 1 entry grown to 2 entries overwriting slot 0 */
	cs_amp_lib_test_init_dummy_cal_blob(test, 1);
	KUNIT_ASSERT_EQ(test, 1, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 0, 2, &data));
	KUNIT_EXPECT_EQ(test, 2, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 2), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[1], sizeof(data)));

	/* Initially 2 entries grown to 4 entries overwriting slot 1 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 2);
	KUNIT_ASSERT_EQ(test, 2, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 1, 4, &data));
	KUNIT_EXPECT_EQ(test, 4, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 4), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));

	/* Initially 4 entries grown to 6 entries overwriting slot 1 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 1, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));

	/* Initially 4 entries grown to 6 entries overwriting slot 3 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 3, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));

	/* Initially 6 entries grown to 8 entries overwriting slot 4 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 6);
	KUNIT_ASSERT_EQ(test, 6, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa; /* won't match */
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 4, 8, &data));
	KUNIT_EXPECT_EQ(test, 8, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 8), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[6], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[7], sizeof(data)));
}

static void cs_amp_lib_test_grow_cal_efi_replace_by_uid(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const struct cirrus_amp_efi_data *original_blob;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/* Initially 1 entry grown to 2 entries overwriting slot 0 */
	cs_amp_lib_test_init_dummy_cal_blob(test, 1);
	KUNIT_ASSERT_EQ(test, 1, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[0].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 2, &data));
	KUNIT_EXPECT_EQ(test, 2, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 2), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[1], sizeof(data)));

	/* Initially 2 entries grown to 4 entries overwriting slot 1 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 2);
	KUNIT_ASSERT_EQ(test, 2, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[1].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 4, &data));
	KUNIT_EXPECT_EQ(test, 4, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 4), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[2], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[3], sizeof(data)));

	/* Initially 4 entries grown to 6 entries overwriting slot 1 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[1].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));

	/* Initially 4 entries grown to 6 entries overwriting slot 3 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[3].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 6, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[4], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[5], sizeof(data)));

	/* Initially 6 entries grown to 8 entries overwriting slot 4 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 6);
	KUNIT_ASSERT_EQ(test, 6, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[4].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, 8, &data));
	KUNIT_EXPECT_EQ(test, 8, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 8), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[6], sizeof(data)));
	KUNIT_EXPECT_TRUE(test, mem_is_zero(&priv->cal_blob->data[7], sizeof(data)));
}

static void cs_amp_lib_test_cal_efi_replace_by_uid(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const struct cirrus_amp_efi_data *original_blob;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	cs_amp_lib_test_init_dummy_cal_blob(test, 6);
	KUNIT_ASSERT_EQ(test, 6, priv->cal_blob->count);

	/* Replace entry matching slot 0 */
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[0].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[4], &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));

	/* Replace entry matching slot 4 */
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[4].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));

	/* Replace entry matching slot 3 */
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[3].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[4], &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));

	/* Replace entry matching slot 5 */
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[5].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[4], &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[5], sizeof(data));
}

static void cs_amp_lib_test_cal_efi_replace_by_index(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const struct cirrus_amp_efi_data *original_blob;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	cs_amp_lib_test_init_dummy_cal_blob(test, 6);
	KUNIT_ASSERT_EQ(test, 6, priv->cal_blob->count);

	/*
	 * Replace entry matching slot 0.
	 * data.calTarget is deliberately set different to current calTarget
	 * of the slot to check that the index forces that slot to be used.
	 */
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = ~priv->cal_blob->data[0].calTarget[0];
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 0, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[4], &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));

	/* Replace entry matching slot 4 */
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = ~priv->cal_blob->data[4].calTarget[0];
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 4, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));

	/* Replace entry matching slot 3 */
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = ~priv->cal_blob->data[3].calTarget[0];
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 3, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[4], &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));

	/* Replace entry matching slot 5 */
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = ~priv->cal_blob->data[5].calTarget[0];
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 5, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[4], &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[5], sizeof(data));
}

static void cs_amp_lib_test_cal_efi_deduplicate(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const struct cirrus_amp_efi_data *original_blob;
	struct cirrus_amp_cal_data data;
	int i;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	/*
	 * Replace entry matching slot 0.
	 * An active entry in slot 1 for the same UID should be marked empty.
	 * Other entries are unaltered.
	 */
	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[1].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 0, -1, &data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[1].calTime[0]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[1].calTime[1]);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));

	/*
	 * Replace entry matching slot 1.
	 * An active entry in slot 0 for the same UID should be marked empty.
	 * Other entries are unaltered.
	 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[0].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 1, -1, &data));
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[0].calTime[0]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[0].calTime[1]);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));

	/*
	 * Replace entry matching slot 1.
	 * An active entry in slot 3 for the same UID should be marked empty.
	 * Other entries are unaltered.
	 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	memcpy(data.calTarget, priv->cal_blob->data[3].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 1, -1, &data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[3].calTime[0]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[3].calTime[1]);

	/*
	 * Worst case, all entries have the same UID
	 */
	priv->cal_blob = NULL;
	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	for (i = 0; i < priv->cal_blob->count; i++) {
		priv->cal_blob->data[i].calTarget[0] = 0xe5e5e5e5;
		priv->cal_blob->data[i].calTarget[1] = 0xa7a7a7a7;
	}
	memcpy(data.calTarget, priv->cal_blob->data[2].calTarget, sizeof(data.calTarget));
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 2, -1, &data));
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[0].calTime[0]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[0].calTime[1]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[1].calTime[0]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[1].calTime[1]);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[3].calTime[0]);
	KUNIT_EXPECT_EQ(test, 0, priv->cal_blob->data[3].calTime[1]);
}

static void cs_amp_lib_test_cal_efi_find_free(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const struct cirrus_amp_efi_data *original_blob;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	cs_amp_lib_test_init_dummy_cal_blob(test, 6);
	KUNIT_ASSERT_EQ(test, 6, priv->cal_blob->count);

	/*
	 * Slot 0 is empty.
	 * data.calTarget is set to a value that won't match any existing entry.
	 */
	memset(&priv->cal_blob->data[0].calTime, 0, sizeof(priv->cal_blob->data[0].calTime));
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa;
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[4], &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));

	/* Slot 4 is empty */
	memset(&priv->cal_blob->data[4].calTime, 0, sizeof(priv->cal_blob->data[4].calTime));
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa;
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));

	/* Slot 3 is empty */
	memset(&priv->cal_blob->data[3].calTime, 0, sizeof(priv->cal_blob->data[3].calTime));
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa;
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[4], &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));

	/* Replace entry matching slot 5 */
	memset(&priv->cal_blob->data[5].calTime, 0, sizeof(priv->cal_blob->data[5].calTime));
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = 0xaaaaaaaa;
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[4], &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[5], sizeof(data));
}

static void cs_amp_lib_test_cal_efi_bad_cal_target(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	cs_amp_lib_test_init_dummy_cal_blob(test, 4);

	/* Zero calTarget is illegal */
	get_random_bytes(&data, sizeof(data));
	memset(data.calTarget, 0, sizeof(data.calTarget));
	KUNIT_EXPECT_LT(test, cs_amp_set_efi_calibration_data(dev, -1, -1, &data), 0);
	KUNIT_EXPECT_LT(test, cs_amp_set_efi_calibration_data(dev, 0, -1, &data), 0);
	KUNIT_EXPECT_LT(test, cs_amp_set_efi_calibration_data(dev, 0, 2, &data), 0);
}

static void cs_amp_lib_test_cal_efi_write_denied(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const struct cirrus_amp_efi_data *original_blob;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable_denied);

	cs_amp_lib_test_init_dummy_cal_blob(test, 4);
	KUNIT_ASSERT_EQ(test, 4, priv->cal_blob->count);
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));

	/* Unspecified slot */
	KUNIT_EXPECT_LT(test, cs_amp_set_efi_calibration_data(dev, -1, -1, &data), 0);
	KUNIT_EXPECT_MEMEQ(test, original_blob, priv->cal_blob, original_blob->size);

	/* Unspecified slot with size */
	KUNIT_EXPECT_LT(test, cs_amp_set_efi_calibration_data(dev, -1, 6, &data), 0);
	KUNIT_EXPECT_MEMEQ(test, original_blob, priv->cal_blob, original_blob->size);

	/* Specified slot */
	KUNIT_EXPECT_LT(test, cs_amp_set_efi_calibration_data(dev, 1, -1, &data), 0);
	KUNIT_EXPECT_MEMEQ(test, original_blob, priv->cal_blob, original_blob->size);

	/* Specified slot with size */
	KUNIT_EXPECT_LT(test, cs_amp_set_efi_calibration_data(dev, 1, 6, &data), 0);
	KUNIT_EXPECT_MEMEQ(test, original_blob, priv->cal_blob, original_blob->size);
}

static void cs_amp_lib_test_cal_efi_attr_preserved(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_efi_variable);

	cs_amp_lib_test_init_dummy_cal_blob(test, 6);
	KUNIT_ASSERT_EQ(test, 6, priv->cal_blob->count);
	memset(&priv->cal_blob->data[0], 0, sizeof(priv->cal_blob->data[0]));
	get_random_bytes(&data, sizeof(data));

	/* Set a non-standard attr to return from get_efi_variable() */
	priv->efi_attr = EFI_VARIABLE_HARDWARE_ERROR_RECORD;

	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, -1, -1, &data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_EQ(test, priv->efi_attr, EFI_VARIABLE_HARDWARE_ERROR_RECORD);
}

static efi_status_t cs_amp_lib_test_set_hp_efi_cal_variable(efi_char16_t *name,
							    efi_guid_t *guid,
							    u32 attr,
							    unsigned long size,
							    void *buf)
{
	static const efi_char16_t expected_name[] = HP_CALIBRATION_EFI_NAME;
	static const efi_guid_t expected_guid = HP_CALIBRATION_EFI_GUID;
	struct kunit *test = kunit_get_current_test();
	struct cs_amp_lib_test_priv *priv = test->priv;

	KUNIT_ASSERT_NOT_NULL(test, name);
	KUNIT_ASSERT_NOT_NULL(test, guid);

	if (memcmp(name, expected_name, sizeof(expected_name)) ||
	    efi_guidcmp(*guid, expected_guid))
		return -EFI_ACCESS_DENIED;

	KUNIT_ASSERT_NOT_NULL(test, buf);
	KUNIT_ASSERT_NE(test, 0, size);

	kunit_kfree(test, priv->cal_blob);
	priv->cal_blob = kunit_kmalloc(test, size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv->cal_blob);
	memcpy(priv->cal_blob, buf, size);
	priv->efi_attr = attr;

	return EFI_SUCCESS;
}

/*
 * If the HP EFI exists it should be the one that is updated.
 */
static void cs_amp_lib_test_cal_efi_update_hp(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const struct cirrus_amp_efi_data *original_blob;
	struct cirrus_amp_cal_data data;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_hp_cal_efi_variable);
	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->set_efi_variable,
				   cs_amp_lib_test_set_hp_efi_cal_variable);

	cs_amp_lib_test_init_dummy_cal_blob(test, 6);
	KUNIT_ASSERT_EQ(test, 6, priv->cal_blob->count);

	/* Replace entry matching slot 4 */
	original_blob = cs_amp_lib_test_cal_blob_dup(test);
	get_random_bytes(&data, sizeof(data));
	data.calTarget[0] = ~priv->cal_blob->data[4].calTarget[0];
	KUNIT_EXPECT_EQ(test, 0, cs_amp_set_efi_calibration_data(dev, 4, -1, &data));
	KUNIT_EXPECT_EQ(test, 6, priv->cal_blob->count);
	KUNIT_EXPECT_EQ(test, struct_size(priv->cal_blob, data, 6), priv->cal_blob->size);
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[0], &priv->cal_blob->data[0], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[1], &priv->cal_blob->data[1], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[2], &priv->cal_blob->data[2], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[3], &priv->cal_blob->data[3], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &data, &priv->cal_blob->data[4], sizeof(data));
	KUNIT_EXPECT_MEMEQ(test, &original_blob->data[5], &priv->cal_blob->data[5], sizeof(data));
}

static void cs_amp_lib_test_spkid_lenovo_not_present(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_none);

	KUNIT_EXPECT_EQ(test, -ENOENT, cs_amp_get_vendor_spkid(dev));
}

static efi_status_t cs_amp_lib_test_get_efi_variable_lenovo_d0(efi_char16_t *name,
							       efi_guid_t *guid,
							       u32 *returned_attr,
							       unsigned long *size,
							       void *buf)
{
	struct kunit *test = kunit_get_current_test();

	if (efi_guidcmp(*guid, LENOVO_SPEAKER_ID_EFI_GUID) ||
	    memcmp(name, LENOVO_SPEAKER_ID_EFI_NAME, sizeof(LENOVO_SPEAKER_ID_EFI_NAME)))
		return EFI_NOT_FOUND;

	KUNIT_ASSERT_EQ(test, *size, 1);
	*size = 1;
	*(u8 *)buf = 0xd0;

	return EFI_SUCCESS;
}

static efi_status_t cs_amp_lib_test_get_efi_variable_lenovo_d1(efi_char16_t *name,
							       efi_guid_t *guid,
							       u32 *returned_attr,
							       unsigned long *size,
							       void *buf)
{
	struct kunit *test = kunit_get_current_test();

	if (efi_guidcmp(*guid, LENOVO_SPEAKER_ID_EFI_GUID) ||
	    memcmp(name, LENOVO_SPEAKER_ID_EFI_NAME, sizeof(LENOVO_SPEAKER_ID_EFI_NAME)))
		return EFI_NOT_FOUND;

	KUNIT_ASSERT_EQ(test, *size, 1);
	*size = 1;
	*(u8 *)buf = 0xd1;

	return EFI_SUCCESS;
}

static efi_status_t cs_amp_lib_test_get_efi_variable_lenovo_00(efi_char16_t *name,
							       efi_guid_t *guid,
							       u32 *returned_attr,
							       unsigned long *size,
							       void *buf)
{
	struct kunit *test = kunit_get_current_test();

	KUNIT_ASSERT_EQ(test, 0, efi_guidcmp(*guid, LENOVO_SPEAKER_ID_EFI_GUID));
	KUNIT_ASSERT_EQ(test, *size, 1);
	*size = 1;
	*(u8 *)buf = 0;

	return EFI_SUCCESS;
}

static void cs_amp_lib_test_spkid_lenovo_d0(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_lenovo_d0);

	KUNIT_EXPECT_EQ(test, 0, cs_amp_get_vendor_spkid(dev));
}

static void cs_amp_lib_test_spkid_lenovo_d1(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_lenovo_d1);

	KUNIT_EXPECT_EQ(test, 1, cs_amp_get_vendor_spkid(dev));
}

static void cs_amp_lib_test_spkid_lenovo_illegal(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_lenovo_00);

	KUNIT_EXPECT_LT(test, cs_amp_get_vendor_spkid(dev), 0);
}

static efi_status_t cs_amp_lib_test_get_efi_variable_buf_too_small(efi_char16_t *name,
								   efi_guid_t *guid,
								   u32 *returned_attr,
								   unsigned long *size,
								   void *buf)
{
	return EFI_BUFFER_TOO_SMALL;
}

static void cs_amp_lib_test_spkid_lenovo_oversize(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_buf_too_small);

	KUNIT_EXPECT_LT(test, cs_amp_get_vendor_spkid(dev), 0);
}

static efi_status_t cs_amp_lib_test_get_efi_variable_hp_30(efi_char16_t *name,
							   efi_guid_t *guid,
							   u32 *returned_attr,
							   unsigned long *size,
							   void *buf)
{
	struct kunit *test = kunit_get_current_test();

	if (efi_guidcmp(*guid, HP_SPEAKER_ID_EFI_GUID) ||
	    memcmp(name, HP_SPEAKER_ID_EFI_NAME, sizeof(HP_SPEAKER_ID_EFI_NAME)))
		return EFI_NOT_FOUND;

	KUNIT_ASSERT_EQ(test, *size, 1);
	*size = 1;
	*(u8 *)buf = 0x30;

	return EFI_SUCCESS;
}

static efi_status_t cs_amp_lib_test_get_efi_variable_hp_31(efi_char16_t *name,
							   efi_guid_t *guid,
							   u32 *returned_attr,
							   unsigned long *size,
							   void *buf)
{
	struct kunit *test = kunit_get_current_test();

	if (efi_guidcmp(*guid, HP_SPEAKER_ID_EFI_GUID) ||
	    memcmp(name, HP_SPEAKER_ID_EFI_NAME, sizeof(HP_SPEAKER_ID_EFI_NAME)))
		return EFI_NOT_FOUND;

	KUNIT_ASSERT_EQ(test, *size, 1);
	*size = 1;
	*(u8 *)buf = 0x31;

	return EFI_SUCCESS;
}

static void cs_amp_lib_test_spkid_hp_30(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_hp_30);

	KUNIT_EXPECT_EQ(test, 0, cs_amp_get_vendor_spkid(dev));
}

static void cs_amp_lib_test_spkid_hp_31(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_hp_31);

	KUNIT_EXPECT_EQ(test, 1, cs_amp_get_vendor_spkid(dev));
}

static efi_status_t cs_amp_lib_test_get_efi_vendor_sysid(efi_char16_t *name,
							 efi_guid_t *guid,
							 u32 *returned_attr,
							 unsigned long *size,
							 void *buf)
{
	struct kunit *test = kunit_get_current_test();
	const struct cs_amp_lib_test_param *param = test->param_value;
	unsigned int len;

	KUNIT_ASSERT_NOT_NULL(test, param->vendor_sysid);
	len = strlen(param->vendor_sysid);

	if (*size < len) {
		*size = len;
		return EFI_BUFFER_TOO_SMALL;
	}

	KUNIT_ASSERT_NOT_NULL(test, buf);
	memcpy(buf, param->vendor_sysid, len);

	return EFI_SUCCESS;
}

/* Fetch SSIDExV2 string from UEFI */
static void cs_amp_lib_test_ssidexv2_fetch(struct kunit *test)
{
	const struct cs_amp_lib_test_param *param = test->param_value;
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const char *got;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_vendor_sysid);

	got = cs_amp_devm_get_vendor_specific_variant_id(dev, PCI_VENDOR_ID_DELL, 0xabcd);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, got);
	KUNIT_EXPECT_STREQ(test, got, param->expected_sysid);
}

/* Invalid SSIDExV2 string should be ignored */
static void cs_amp_lib_test_ssidexv2_fetch_invalid(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const char *got;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_vendor_sysid);

	got = cs_amp_devm_get_vendor_specific_variant_id(dev, PCI_VENDOR_ID_DELL, 0xabcd);
	KUNIT_EXPECT_NOT_NULL(test, got);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(got), -ENOENT);
}

static void cs_amp_lib_test_ssidexv2_not_dell(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const char *got;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_vendor_sysid);

	/* Not returned if SSID vendor is not Dell */
	got = cs_amp_devm_get_vendor_specific_variant_id(dev, PCI_VENDOR_ID_CIRRUS, 0xabcd);
	KUNIT_EXPECT_NOT_NULL(test, got);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(got), -ENOENT);
}

static void cs_amp_lib_test_vendor_variant_id_not_found(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv = test->priv;
	struct device *dev = &priv->amp_dev->dev;
	const char *got;

	kunit_activate_static_stub(test,
				   cs_amp_test_hooks->get_efi_variable,
				   cs_amp_lib_test_get_efi_variable_none);

	got = cs_amp_devm_get_vendor_specific_variant_id(dev, PCI_VENDOR_ID_DELL, 0xabcd);
	KUNIT_EXPECT_NOT_NULL(test, got);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(got), -ENOENT);

	got = cs_amp_devm_get_vendor_specific_variant_id(dev, -1, -1);
	KUNIT_EXPECT_NOT_NULL(test, got);
	KUNIT_EXPECT_EQ(test, PTR_ERR_OR_ZERO(got), -ENOENT);
}

static int cs_amp_lib_test_case_init(struct kunit *test)
{
	struct cs_amp_lib_test_priv *priv;

	KUNIT_ASSERT_NOT_NULL(test, cs_amp_test_hooks);

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	test->priv = priv;
	INIT_LIST_HEAD(&priv->ctl_write_list);

	/* Create dummy amp driver dev */
	priv->amp_dev = faux_device_create("cs_amp_lib_test_drv", NULL, NULL);
	KUNIT_ASSERT_NOT_NULL(test, priv->amp_dev);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test,
						  faux_device_destroy_wrapper,
						  priv->amp_dev));

	return 0;
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

static const struct cs_amp_lib_test_param cs_amp_lib_test_ssidexv2_param_cases[] = {
	{ .vendor_sysid = "abcd_00",		.expected_sysid = "00" },
	{ .vendor_sysid = "abcd_01",		.expected_sysid = "01" },
	{ .vendor_sysid = "abcd_XY",		.expected_sysid = "XY" },

	{ .vendor_sysid = "1028abcd_00",	.expected_sysid = "00" },
	{ .vendor_sysid = "1028abcd_01",	.expected_sysid = "01" },
	{ .vendor_sysid = "1028abcd_XY",	.expected_sysid = "XY" },

	{ .vendor_sysid = "abcd_00_WF",		.expected_sysid = "00" },
	{ .vendor_sysid = "abcd_01_WF",		.expected_sysid = "01" },
	{ .vendor_sysid = "abcd_XY_WF",		.expected_sysid = "XY" },

	{ .vendor_sysid = "1028abcd_00_WF",	.expected_sysid = "00" },
	{ .vendor_sysid = "1028abcd_01_WF",	.expected_sysid = "01" },
	{ .vendor_sysid = "1028abcd_XY_WF",	.expected_sysid = "XY" },

	{ .vendor_sysid = "abcd_00_AA_BB",	.expected_sysid = "00" },
	{ .vendor_sysid = "abcd_01_AA_BB",	.expected_sysid = "01" },
	{ .vendor_sysid = "abcd_XY_AA_BB",	.expected_sysid = "XY" },

	{ .vendor_sysid = "1028abcd_00_AA_BB",	.expected_sysid = "00" },
	{ .vendor_sysid = "1028abcd_01_AA_BB",	.expected_sysid = "01" },
	{ .vendor_sysid = "1028abcd_XY_A_BB",	.expected_sysid = "XY" },
};

static void cs_amp_lib_test_ssidexv2_param_desc(const struct cs_amp_lib_test_param *param,
						char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "vendor_sysid:'%s' expected_sysid:'%s'",
		 param->vendor_sysid, param->expected_sysid);
}

KUNIT_ARRAY_PARAM(cs_amp_lib_test_ssidexv2, cs_amp_lib_test_ssidexv2_param_cases,
		  cs_amp_lib_test_ssidexv2_param_desc);

static const struct cs_amp_lib_test_param cs_amp_lib_test_ssidexv2_invalid_param_cases[] = {
	{ .vendor_sysid = "abcd" },
	{ .vendor_sysid = "abcd_0" },
	{ .vendor_sysid = "abcd_1" },
	{ .vendor_sysid = "abcd_0_1" },
	{ .vendor_sysid = "abcd_1_1" },
	{ .vendor_sysid = "abcd_1_X" },
	{ .vendor_sysid = "abcd_1_X" },
	{ .vendor_sysid = "abcd_000" },
	{ .vendor_sysid = "abcd_010" },
	{ .vendor_sysid = "abcd_000_01" },
	{ .vendor_sysid = "abcd_000_01" },

	{ .vendor_sysid = "1234abcd" },
	{ .vendor_sysid = "1234abcd_0" },
	{ .vendor_sysid = "1234abcd_1" },
	{ .vendor_sysid = "1234abcd_0_1" },
	{ .vendor_sysid = "1234abcd_1_1" },
	{ .vendor_sysid = "1234abcd_1_X" },
	{ .vendor_sysid = "1234abcd_1_X" },
	{ .vendor_sysid = "1234abcd_000" },
	{ .vendor_sysid = "1234abcd_010" },
	{ .vendor_sysid = "1234abcd_000_01" },
	{ .vendor_sysid = "1234abcd_000_01" },
};

KUNIT_ARRAY_PARAM(cs_amp_lib_test_ssidexv2_invalid, cs_amp_lib_test_ssidexv2_invalid_param_cases,
		  cs_amp_lib_test_ssidexv2_param_desc);

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
	KUNIT_CASE(cs_amp_lib_test_get_hp_efi_cal),
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
	KUNIT_CASE(cs_amp_lib_test_get_efi_cal_empty_entry_test),

	/* Tests for writing and reading calibration data */
	KUNIT_CASE(cs_amp_lib_test_write_cal_data_test),
	KUNIT_CASE(cs_amp_lib_test_read_cal_data_test),
	KUNIT_CASE(cs_amp_lib_test_write_ambient_test),

	/* Test cases for writing cal data to UEFI */
	KUNIT_CASE(cs_amp_lib_test_create_new_cal_efi),
	KUNIT_CASE(cs_amp_lib_test_create_new_cal_efi_indexed),
	KUNIT_CASE(cs_amp_lib_test_create_new_cal_efi_indexed_no_max),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_all_zeros_add_first),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_all_zeros_add_first_no_shrink),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_all_zeros_add_first_indexed),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_all_zeros_add_first_indexed_no_shrink),
	KUNIT_CASE(cs_amp_lib_test_grow_append_cal_efi),
	KUNIT_CASE(cs_amp_lib_test_grow_append_cal_efi_indexed),
	KUNIT_CASE(cs_amp_lib_test_grow_append_cal_efi_indexed_no_max),
	KUNIT_CASE(cs_amp_lib_test_grow_cal_efi_replace_indexed),
	KUNIT_CASE(cs_amp_lib_test_grow_cal_efi_replace_by_uid),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_replace_by_uid),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_replace_by_index),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_deduplicate),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_find_free),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_bad_cal_target),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_write_denied),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_attr_preserved),
	KUNIT_CASE(cs_amp_lib_test_cal_efi_update_hp),

	/* Test cases for speaker ID */
	KUNIT_CASE(cs_amp_lib_test_spkid_lenovo_not_present),
	KUNIT_CASE(cs_amp_lib_test_spkid_lenovo_d0),
	KUNIT_CASE(cs_amp_lib_test_spkid_lenovo_d1),
	KUNIT_CASE(cs_amp_lib_test_spkid_lenovo_illegal),
	KUNIT_CASE(cs_amp_lib_test_spkid_lenovo_oversize),
	KUNIT_CASE(cs_amp_lib_test_spkid_hp_30),
	KUNIT_CASE(cs_amp_lib_test_spkid_hp_31),

	/* Test cases for SSIDExV2 */
	KUNIT_CASE_PARAM(cs_amp_lib_test_ssidexv2_fetch,
			 cs_amp_lib_test_ssidexv2_gen_params),
	KUNIT_CASE_PARAM(cs_amp_lib_test_ssidexv2_fetch_invalid,
			 cs_amp_lib_test_ssidexv2_invalid_gen_params),
	KUNIT_CASE_PARAM(cs_amp_lib_test_ssidexv2_not_dell,
			 cs_amp_lib_test_ssidexv2_gen_params),
	KUNIT_CASE(cs_amp_lib_test_vendor_variant_id_not_found),

	{ } /* terminator */
};

static struct kunit_suite cs_amp_lib_test_suite = {
	.name = "snd-soc-cs-amp-lib-test",
	.init = cs_amp_lib_test_case_init,
	.test_cases = cs_amp_lib_test_cases,
};

kunit_test_suite(cs_amp_lib_test_suite);

MODULE_IMPORT_NS("SND_SOC_CS_AMP_LIB");
MODULE_DESCRIPTION("KUnit test for Cirrus Logic amplifier library");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
