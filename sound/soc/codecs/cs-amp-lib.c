// SPDX-License-Identifier: GPL-2.0-only
//
// Common code for Cirrus Logic Smart Amplifiers
//
// Copyright (C) 2024 Cirrus Logic, Inc. and
//               Cirrus Logic International Semiconductor Ltd.

#include <asm/byteorder.h>
#include <kunit/static_stub.h>
#include <linux/debugfs.h>
#include <linux/dev_printk.h>
#include <linux/efi.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/types.h>
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

#define DELL_SSIDEXV2_EFI_NAME L"SSIDexV2Data"
#define DELL_SSIDEXV2_EFI_GUID \
	EFI_GUID(0x6a5f35df, 0x1432, 0x4656, 0x85, 0x97, 0x31, 0x04, 0xd5, 0xbf, 0x3a, 0xb0)

static const struct cs_amp_lib_cal_efivar {
	efi_char16_t *name;
	efi_guid_t *guid;
} cs_amp_lib_cal_efivars[] = {
	{
		.name = HP_CALIBRATION_EFI_NAME,
		.guid = &HP_CALIBRATION_EFI_GUID,
	},
	{
		.name = CIRRUS_LOGIC_CALIBRATION_EFI_NAME,
		.guid = &CIRRUS_LOGIC_CALIBRATION_EFI_GUID,
	},
};

#define CS_AMP_CAL_DEFAULT_EFI_ATTR			\
		(EFI_VARIABLE_NON_VOLATILE |		\
		 EFI_VARIABLE_BOOTSERVICE_ACCESS |	\
		 EFI_VARIABLE_RUNTIME_ACCESS)

/* Offset from Unix time to Windows time (100ns since 1 Jan 1601) */
#define UNIX_TIME_TO_WINDOWS_TIME_OFFSET	116444736000000000ULL

static DEFINE_MUTEX(cs_amp_efi_cal_write_lock);

static u64 cs_amp_time_now_in_windows_time(void)
{
	u64 time_in_100ns = div_u64(ktime_get_real_ns(), 100);

	return time_in_100ns + UNIX_TIME_TO_WINDOWS_TIME_OFFSET;
}

static int cs_amp_write_cal_coeff(struct cs_dsp *dsp,
				  const struct cirrus_amp_cal_controls *controls,
				  const char *ctl_name, u32 val)
{
	struct cs_dsp_coeff_ctl *cs_ctl;
	__be32 beval = cpu_to_be32(val);
	int ret;

	KUNIT_STATIC_STUB_REDIRECT(cs_amp_write_cal_coeff, dsp, controls, ctl_name, val);

	if (IS_REACHABLE(CONFIG_FW_CS_DSP)) {
		mutex_lock(&dsp->pwr_lock);
		cs_ctl = cs_dsp_get_ctl(dsp, ctl_name, controls->mem_region, controls->alg_id);
		ret = cs_dsp_coeff_write_ctrl(cs_ctl, 0, &beval, sizeof(beval));
		mutex_unlock(&dsp->pwr_lock);

		if (ret < 0) {
			dev_err(dsp->dev, "Failed to write to '%s': %d\n", ctl_name, ret);
			return ret;
		}

		return 0;
	}

	return -ENODEV;
}

static int cs_amp_read_cal_coeff(struct cs_dsp *dsp,
				 const struct cirrus_amp_cal_controls *controls,
				 const char *ctl_name, u32 *val)
{
	struct cs_dsp_coeff_ctl *cs_ctl;
	__be32 beval;
	int ret;

	KUNIT_STATIC_STUB_REDIRECT(cs_amp_read_cal_coeff, dsp, controls, ctl_name, val);

	if (!IS_REACHABLE(CONFIG_FW_CS_DSP))
		return -ENODEV;

	scoped_guard(mutex, &dsp->pwr_lock) {
		cs_ctl = cs_dsp_get_ctl(dsp, ctl_name, controls->mem_region, controls->alg_id);
		ret = cs_dsp_coeff_read_ctrl(cs_ctl, 0, &beval, sizeof(beval));
	}

	if (ret < 0) {
		dev_err(dsp->dev, "Failed to write to '%s': %d\n", ctl_name, ret);
		return ret;
	}

	*val = be32_to_cpu(beval);

	return 0;
}

static int _cs_amp_write_cal_coeffs(struct cs_dsp *dsp,
				    const struct cirrus_amp_cal_controls *controls,
				    const struct cirrus_amp_cal_data *data)
{
	int ret;

	dev_dbg(dsp->dev, "Calibration: Ambient=%#x, Status=%#x, CalR=%d\n",
		data->calAmbient, data->calStatus, data->calR);

	if (list_empty(&dsp->ctl_list)) {
		dev_info(dsp->dev, "Calibration disabled due to missing firmware controls\n");
		return -ENOENT;
	}

	ret = cs_amp_write_cal_coeff(dsp, controls, controls->ambient, data->calAmbient);
	if (ret)
		return ret;

	ret = cs_amp_write_cal_coeff(dsp, controls, controls->calr, data->calR);
	if (ret)
		return ret;

	ret = cs_amp_write_cal_coeff(dsp, controls, controls->status, data->calStatus);
	if (ret)
		return ret;

	ret = cs_amp_write_cal_coeff(dsp, controls, controls->checksum, data->calR + 1);
	if (ret)
		return ret;

	return 0;
}

static int _cs_amp_read_cal_coeffs(struct cs_dsp *dsp,
				    const struct cirrus_amp_cal_controls *controls,
				    struct cirrus_amp_cal_data *data)
{
	u64 time;
	u32 val;
	int ret;

	if (list_empty(&dsp->ctl_list)) {
		dev_info(dsp->dev, "Calibration disabled due to missing firmware controls\n");
		return -ENOENT;
	}

	ret = cs_amp_read_cal_coeff(dsp, controls, controls->ambient, &val);
	if (ret)
		return ret;

	data->calAmbient = (s8)val;

	ret = cs_amp_read_cal_coeff(dsp, controls, controls->calr, &val);
	if (ret)
		return ret;

	data->calR = (u16)val;

	ret = cs_amp_read_cal_coeff(dsp, controls, controls->status, &val);
	if (ret)
		return ret;

	data->calStatus = (u8)val;

	/* Fill in timestamp */
	time = cs_amp_time_now_in_windows_time();
	data->calTime[0] = (u32)time;
	data->calTime[1] = (u32)(time >> 32);

	return 0;
}

/**
 * cs_amp_write_cal_coeffs - Write calibration data to firmware controls.
 * @dsp:	Pointer to struct cs_dsp.
 * @controls:	Pointer to definition of firmware controls to be written.
 * @data:	Pointer to calibration data.
 *
 * Returns: 0 on success, else negative error value.
 */
int cs_amp_write_cal_coeffs(struct cs_dsp *dsp,
			    const struct cirrus_amp_cal_controls *controls,
			    const struct cirrus_amp_cal_data *data)
{
	if (IS_REACHABLE(CONFIG_FW_CS_DSP) || IS_ENABLED(CONFIG_SND_SOC_CS_AMP_LIB_TEST_HOOKS))
		return _cs_amp_write_cal_coeffs(dsp, controls, data);
	else
		return -ENODEV;
}
EXPORT_SYMBOL_NS_GPL(cs_amp_write_cal_coeffs, "SND_SOC_CS_AMP_LIB");

/**
 * cs_amp_read_cal_coeffs - Read calibration data from firmware controls.
 * @dsp:	Pointer to struct cs_dsp.
 * @controls:	Pointer to definition of firmware controls to be read.
 * @data:	Pointer to calibration data where results will be written.
 *
 * Returns: 0 on success, else negative error value.
 */
int cs_amp_read_cal_coeffs(struct cs_dsp *dsp,
			   const struct cirrus_amp_cal_controls *controls,
			   struct cirrus_amp_cal_data *data)
{
	if (IS_REACHABLE(CONFIG_FW_CS_DSP) || IS_ENABLED(CONFIG_SND_SOC_CS_AMP_LIB_TEST_HOOKS))
		return _cs_amp_read_cal_coeffs(dsp, controls, data);
	else
		return -ENODEV;
}
EXPORT_SYMBOL_NS_GPL(cs_amp_read_cal_coeffs, "SND_SOC_CS_AMP_LIB");

/**
 * cs_amp_write_ambient_temp - write value to calibration ambient temperature
 * @dsp:	Pointer to struct cs_dsp.
 * @controls:	Pointer to definition of firmware controls to be read.
 * @temp:	Temperature in degrees celcius.
 *
 * Returns: 0 on success, else negative error value.
 */
int cs_amp_write_ambient_temp(struct cs_dsp *dsp,
			      const struct cirrus_amp_cal_controls *controls,
			      u32 temp)
{
	return cs_amp_write_cal_coeff(dsp, controls, controls->ambient, temp);
}
EXPORT_SYMBOL_NS_GPL(cs_amp_write_ambient_temp, "SND_SOC_CS_AMP_LIB");

static efi_status_t cs_amp_get_efi_variable(efi_char16_t *name,
					    efi_guid_t *guid,
					    u32 *returned_attr,
					    unsigned long *size,
					    void *buf)
{
	u32 attr;

	if (!returned_attr)
		returned_attr = &attr;

	KUNIT_STATIC_STUB_REDIRECT(cs_amp_get_efi_variable, name, guid,
				   returned_attr, size, buf);

	if (efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE))
		return efi.get_variable(name, guid, returned_attr, size, buf);

	return EFI_NOT_FOUND;
}

static efi_status_t cs_amp_set_efi_variable(efi_char16_t *name,
					    efi_guid_t *guid,
					    u32 attr,
					    unsigned long size,
					    void *buf)
{
	KUNIT_STATIC_STUB_REDIRECT(cs_amp_set_efi_variable, name, guid, attr, size, buf);

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_SET_VARIABLE))
		return EFI_NOT_FOUND;

	return efi.set_variable(name, guid, attr, size, buf);
}

static int cs_amp_convert_efi_status(efi_status_t status)
{
	switch (status) {
	case EFI_SUCCESS:
		return 0;
	case EFI_NOT_FOUND:
		return -ENOENT;
	case EFI_BUFFER_TOO_SMALL:
		return -EFBIG;
	case EFI_WRITE_PROTECTED:
	case EFI_UNSUPPORTED:
	case EFI_ACCESS_DENIED:
	case EFI_SECURITY_VIOLATION:
		return -EACCES;
	default:
		return -EIO;
	}
}

static void *cs_amp_alloc_get_efi_variable(efi_char16_t *name,
					   efi_guid_t *guid,
					   u32 *returned_attr)
{
	efi_status_t status;
	unsigned long size = 0;

	status = cs_amp_get_efi_variable(name, guid, NULL, &size, NULL);
	if (status != EFI_BUFFER_TOO_SMALL)
		return ERR_PTR(cs_amp_convert_efi_status(status));

	/* Over-alloc to ensure strings are always NUL-terminated */
	void *buf __free(kfree) = kzalloc(size + 1, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	status = cs_amp_get_efi_variable(name, guid, returned_attr, &size, buf);
	if (status != EFI_SUCCESS)
		return ERR_PTR(cs_amp_convert_efi_status(status));

	return_ptr(buf);
}

static struct cirrus_amp_efi_data *cs_amp_get_cal_efi_buffer(struct device *dev,
							     efi_char16_t **name,
							     efi_guid_t **guid,
							     u32 *attr)
{
	struct cirrus_amp_efi_data *efi_data;
	unsigned long data_size = 0;
	u8 *data;
	efi_status_t status;
	int i, ret;

	/* Find EFI variable and get size */
	for (i = 0; i < ARRAY_SIZE(cs_amp_lib_cal_efivars); i++) {
		status = cs_amp_get_efi_variable(cs_amp_lib_cal_efivars[i].name,
						 cs_amp_lib_cal_efivars[i].guid,
						 attr, &data_size, NULL);
		if (status == EFI_BUFFER_TOO_SMALL)
			break;
	}

	if (status != EFI_BUFFER_TOO_SMALL)
		return ERR_PTR(-ENOENT);

	if (name)
		*name = cs_amp_lib_cal_efivars[i].name;

	if (guid)
		*guid = cs_amp_lib_cal_efivars[i].guid;

	if (data_size < sizeof(*efi_data)) {
		dev_err(dev, "EFI cal variable truncated\n");
		return ERR_PTR(-EOVERFLOW);
	}

	/* Get variable contents into buffer */
	data = kmalloc(data_size, GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	status = cs_amp_get_efi_variable(cs_amp_lib_cal_efivars[i].name,
					 cs_amp_lib_cal_efivars[i].guid,
					 attr, &data_size, data);
	if (status != EFI_SUCCESS) {
		ret = -EINVAL;
		goto err;
	}

	efi_data = (struct cirrus_amp_efi_data *)data;
	dev_dbg(dev, "Calibration: Size=%d, Amp Count=%d\n", efi_data->size, efi_data->count);

	if ((efi_data->count > 128) ||
	    struct_size(efi_data, data, efi_data->count) > data_size) {
		dev_err(dev, "EFI cal variable truncated\n");
		ret = -EOVERFLOW;
		goto err;
	}

	/* This could be zero-filled space pre-allocated by the BIOS */
	if (efi_data->size == 0)
		efi_data->size = data_size;

	return efi_data;

err:
	kfree(data);
	dev_err(dev, "Failed to read calibration data from EFI: %d\n", ret);

	return ERR_PTR(ret);
}

static int cs_amp_set_cal_efi_buffer(struct device *dev,
				     efi_char16_t *name,
				     efi_guid_t *guid,
				     u32 attr,
				     struct cirrus_amp_efi_data *data)
{
	efi_status_t status;

	status = cs_amp_set_efi_variable(name, guid, attr,
					 struct_size(data, data, data->count), data);

	return cs_amp_convert_efi_status(status);
}

static int _cs_amp_get_efi_calibration_data(struct device *dev, u64 target_uid, int amp_index,
					    struct cirrus_amp_cal_data *out_data)
{
	struct cirrus_amp_efi_data *efi_data;
	struct cirrus_amp_cal_data *cal = NULL;
	int i, ret;

	efi_data = cs_amp_get_cal_efi_buffer(dev, NULL, NULL, NULL);
	if (IS_ERR(efi_data))
		return PTR_ERR(efi_data);

	if (target_uid) {
		for (i = 0; i < efi_data->count; ++i) {
			u64 cal_target = cs_amp_cal_target_u64(&efi_data->data[i]);

			/* Skip empty entries */
			if (!efi_data->data[i].calTime[0] && !efi_data->data[i].calTime[1])
				continue;

			/* Skip entries with unpopulated silicon ID */
			if (cal_target == 0)
				continue;

			if (cal_target == target_uid) {
				cal = &efi_data->data[i];
				break;
			}
		}
	}

	if (!cal && (amp_index >= 0) && (amp_index < efi_data->count) &&
	    (efi_data->data[amp_index].calTime[0] || efi_data->data[amp_index].calTime[1])) {
		u64 cal_target = cs_amp_cal_target_u64(&efi_data->data[amp_index]);

		/*
		 * Treat unpopulated cal_target as a wildcard.
		 * If target_uid != 0 we can only get here if cal_target == 0
		 * or it didn't match any cal_target value.
		 * If target_uid == 0 it is a wildcard.
		 */
		if ((cal_target == 0) || (target_uid == 0))
			cal = &efi_data->data[amp_index];
		else
			dev_warn(dev, "Calibration entry %d does not match silicon ID", amp_index);
	}

	if (cal) {
		memcpy(out_data, cal, sizeof(*out_data));
		ret = 0;
	} else {
		dev_warn(dev, "No calibration for silicon ID %#llx\n", target_uid);
		ret = -ENOENT;
	}

	kfree(efi_data);

	return ret;
}

static int _cs_amp_set_efi_calibration_data(struct device *dev, int amp_index, int num_amps,
					    const struct cirrus_amp_cal_data *in_data)
{
	u64 cal_target = cs_amp_cal_target_u64(in_data);
	unsigned long num_entries;
	struct cirrus_amp_efi_data *data;
	efi_char16_t *name = CIRRUS_LOGIC_CALIBRATION_EFI_NAME;
	efi_guid_t *guid = &CIRRUS_LOGIC_CALIBRATION_EFI_GUID;
	u32 attr = CS_AMP_CAL_DEFAULT_EFI_ATTR;
	int i, ret;

	if (cal_target == 0)
		return -EINVAL;

	data = cs_amp_get_cal_efi_buffer(dev, &name, &guid, &attr);
	ret = PTR_ERR_OR_ZERO(data);
	if (ret == -ENOENT) {
		data = NULL;
		goto alloc_new;
	} else if (ret) {
		return ret;
	}

	/*
	 * If the EFI variable is just zero-filled reserved space the count
	 * must be set.
	 */
	if (data->count == 0)
		data->count = (data->size - sizeof(data)) / sizeof(data->data[0]);

	if (amp_index < 0) {
		/* Is there already a slot for this target? */
		for (amp_index = 0; amp_index < data->count; amp_index++) {
			if (cs_amp_cal_target_u64(&data->data[amp_index]) == cal_target)
				break;
		}

		/* Else find an empty slot */
		if (amp_index >= data->count) {
			for (amp_index = 0; amp_index < data->count; amp_index++) {
				if ((data->data[amp_index].calTime[0] == 0) &&
				    (data->data[amp_index].calTime[1] == 0))
					break;
			}
		}
	} else {
		/*
		 * If the index is forced there could be another active
		 * slot with the same calTarget. So deduplicate.
		 */
		for (i = 0; i < data->count; i++) {
			if (i == amp_index)
				continue;

			if ((data->data[i].calTime[0] == 0) && (data->data[i].calTime[1] == 0))
				continue;

			if (cs_amp_cal_target_u64(&data->data[i]) == cal_target)
				memset(data->data[i].calTime, 0, sizeof(data->data[i].calTime));
		}
	}

alloc_new:
	if (amp_index < 0)
		amp_index = 0;

	num_entries = max(num_amps, amp_index + 1);
	if (!data || (data->count < num_entries)) {
		struct cirrus_amp_efi_data *new_data;
		unsigned int new_data_size = struct_size(data, data, num_entries);

		new_data = kzalloc(new_data_size, GFP_KERNEL);
		if (!new_data) {
			ret = -ENOMEM;
			goto err;
		}

		if (data) {
			memcpy(new_data, data, struct_size(data, data, data->count));
			kfree(data);
		}

		data = new_data;
		data->count = num_entries;
		data->size = new_data_size;
	}

	data->data[amp_index] = *in_data;
	ret = cs_amp_set_cal_efi_buffer(dev, name, guid, attr, data);
	if (ret)
		dev_err(dev, "Failed writing calibration to EFI: %d\n", ret);
err:
	kfree(data);

	return ret;
}

/**
 * cs_amp_get_efi_calibration_data - get an entry from calibration data in EFI.
 * @dev:	struct device of the caller.
 * @target_uid:	UID to match, or zero to ignore UID matching.
 * @amp_index:	Entry index to use, or -1 to prevent lookup by index.
 * @out_data:	struct cirrus_amp_cal_data where the entry will be copied.
 *
 * This function can perform 3 types of lookup:
 *
 * (target_uid > 0, amp_index >= 0)
 *	UID search with fallback to using the array index.
 *	Search the calibration data for a non-zero calTarget that matches
 *	target_uid, and if found return that entry. Else, if the entry at
 *	[amp_index] has calTarget == 0, return that entry. Else fail.
 *
 * (target_uid > 0, amp_index < 0)
 *	UID search only.
 *	Search the calibration data for a non-zero calTarget that matches
 *	target_uid, and if found return that entry. Else fail.
 *
 * (target_uid == 0, amp_index >= 0)
 *	Array index fetch only.
 *	Return the entry at [amp_index].
 *
 * An array lookup will be skipped if amp_index exceeds the number of
 * entries in the calibration array, and in this case the return will
 * be -ENOENT. An out-of-range amp_index does not prevent matching by
 * target_uid - it has the same effect as passing amp_index < 0.
 *
 * If the EFI data is too short to be a valid entry, or the entry count
 * in the EFI data overflows the actual length of the data, this function
 * returns -EOVERFLOW.
 *
 * Return: 0 if the entry was found, -ENOENT if no entry was found,
 *	   -EOVERFLOW if the EFI file is corrupt, else other error value.
 */
int cs_amp_get_efi_calibration_data(struct device *dev, u64 target_uid, int amp_index,
				    struct cirrus_amp_cal_data *out_data)
{
	if (IS_ENABLED(CONFIG_EFI) || IS_ENABLED(CONFIG_SND_SOC_CS_AMP_LIB_TEST_HOOKS))
		return _cs_amp_get_efi_calibration_data(dev, target_uid, amp_index, out_data);
	else
		return -ENOENT;
}
EXPORT_SYMBOL_NS_GPL(cs_amp_get_efi_calibration_data, "SND_SOC_CS_AMP_LIB");

/**
 * cs_amp_set_efi_calibration_data - write a calibration data entry to EFI.
 * @dev:	struct device of the caller.
 * @amp_index:	Entry index to use, or -1 to use any available slot.
 * @num_amps:	Maximum number of amps to reserve slots for, or -1 to ignore.
 * @in_data:	struct cirrus_amp_cal_data entry to be written to EFI.
 *
 * If a Vendor-specific variable exists it will be updated,
 * else if the Cirrus variable exists it will be updated
 * else the Cirrus variable will be created.
 *
 * If amp_index >= 0 the data will be placed in this entry of the calibration
 * data array, overwriting what was in that entry. Any other entries with the
 * same calTarget will be marked empty.
 *
 * If amp_index < 0 and in_data->calTarget matches any existing entry, that
 * entry will be overwritten. Else the first available free entry will be used,
 * extending the size of the EFI variable if there are no free entries.
 *
 * If num_amps > 0 the EFI variable will be sized to contain at least this
 * many calibration entries, with any new entries marked empty.
 *
 * Return: 0 if the write was successful, -EFBIG if space could not be made in
 *	   the EFI file to add the entry, -EACCES if it was not possible to
 *	   read or write the EFI variable.
 */
int cs_amp_set_efi_calibration_data(struct device *dev, int amp_index, int num_amps,
				    const struct cirrus_amp_cal_data *in_data)
{
	if (IS_ENABLED(CONFIG_EFI) || IS_ENABLED(CONFIG_SND_SOC_CS_AMP_LIB_TEST_HOOKS)) {
		scoped_guard(mutex, &cs_amp_efi_cal_write_lock) {
			return _cs_amp_set_efi_calibration_data(dev, amp_index,
								num_amps, in_data);
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL_NS_GPL(cs_amp_set_efi_calibration_data, "SND_SOC_CS_AMP_LIB");

struct cs_amp_spkid_efi {
	efi_char16_t *name;
	efi_guid_t *guid;
	u8 values[2];
};

static int cs_amp_get_efi_byte_spkid(struct device *dev, const struct cs_amp_spkid_efi *info)
{
	efi_status_t status;
	unsigned long size;
	u8 spkid;
	int i, ret;

	size = sizeof(spkid);
	status = cs_amp_get_efi_variable(info->name, info->guid, NULL, &size, &spkid);
	ret = cs_amp_convert_efi_status(status);
	if (ret < 0)
		return ret;

	if (size == 0)
		return -ENOENT;

	for (i = 0; i < ARRAY_SIZE(info->values); i++) {
		if (info->values[i] == spkid)
			return i;
	}

	dev_err(dev, "EFI speaker ID bad value %#x\n", spkid);

	return -EINVAL;
}

static const struct cs_amp_spkid_efi cs_amp_spkid_byte_types[] = {
	{
		.name = LENOVO_SPEAKER_ID_EFI_NAME,
		.guid = &LENOVO_SPEAKER_ID_EFI_GUID,
		.values = { 0xd0, 0xd1 },
	},
	{
		.name = HP_SPEAKER_ID_EFI_NAME,
		.guid = &HP_SPEAKER_ID_EFI_GUID,
		.values = { 0x30, 0x31 },
	},
};

/**
 * cs_amp_get_vendor_spkid - get a speaker ID from vendor-specific storage
 * @dev:	pointer to struct device
 *
 * Known vendor-specific methods of speaker ID are checked and if one is
 * found its speaker ID value is returned.
 *
 * Return: >=0 is a valid speaker ID. -ENOENT if a vendor-specific method
 *	   was not found. -EACCES if the vendor-specific storage could not
 *	   be read. Other error values indicate that the data from the
 *	   vendor-specific storage was found but could not be understood.
 */
int cs_amp_get_vendor_spkid(struct device *dev)
{
	int i, ret;

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE) &&
	    !IS_ENABLED(CONFIG_SND_SOC_CS_AMP_LIB_TEST_HOOKS))
		return -ENOENT;

	for (i = 0; i < ARRAY_SIZE(cs_amp_spkid_byte_types); i++) {
		ret = cs_amp_get_efi_byte_spkid(dev, &cs_amp_spkid_byte_types[i]);
		if (ret != -ENOENT)
			return ret;
	}

	return -ENOENT;
}
EXPORT_SYMBOL_NS_GPL(cs_amp_get_vendor_spkid, "SND_SOC_CS_AMP_LIB");

static const char *cs_amp_devm_get_dell_ssidex(struct device *dev,
					       int ssid_vendor, int ssid_device)
{
	unsigned int hex_prefix;
	char audio_id[4];
	char delim;
	char *p;
	int ret;

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE) &&
	    !IS_ENABLED(CONFIG_SND_SOC_CS_AMP_LIB_TEST_HOOKS))
		return ERR_PTR(-ENOENT);

	char *ssidex_buf __free(kfree) = cs_amp_alloc_get_efi_variable(DELL_SSIDEXV2_EFI_NAME,
								       &DELL_SSIDEXV2_EFI_GUID,
								       NULL);
	ret = PTR_ERR_OR_ZERO(ssidex_buf);
	if (ret == -ENOENT)
		return ERR_PTR(-ENOENT);
	else if (ret < 0)
		return ssidex_buf;

	/*
	 * SSIDExV2 string is a series of underscore delimited fields.
	 * First field is all or part of the SSID. Second field should be
	 * a 2-character audio hardware id, followed by other identifiers.
	 * Older models did not have the 2-character audio id, so reject
	 * the string if the second field is not 2 characters.
	 */
	ret = sscanf(ssidex_buf, "%8x_%2s%c", &hex_prefix, audio_id, &delim);
	if (ret < 2)
		return ERR_PTR(-ENOENT);

	if ((ret == 3) && (delim != '_'))
		return ERR_PTR(-ENOENT);

	if (strlen(audio_id) != 2)
		return ERR_PTR(-ENOENT);

	p = devm_kstrdup(dev, audio_id, GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	return p;
}

/**
 * cs_amp_devm_get_vendor_specific_variant_id - get variant ID string
 * @dev:	 pointer to struct device
 * @ssid_vendor: PCI Subsystem Vendor (-1 if unknown)
 * @ssid_device: PCI Subsystem Device (-1 if unknown)
 *
 * Known vendor-specific hardware identifiers are checked and if one is
 * found its content is returned as a NUL-terminated string. The returned
 * string is devm-managed.
 *
 * The returned string is not guaranteed to be globally unique.
 * Generally it should be combined with some other qualifier, such as
 * PCI SSID, to create a globally unique ID.
 *
 * If the caller has a PCI SSID it should pass it in @ssid_vendor and
 * @ssid_device. If the vendor-spefic ID contains this SSID it will be
 * stripped from the returned string to prevent duplication.
 *
 * If the caller does not have a PCI SSID, pass -1 for @ssid_vendor and
 * @ssid_device.
 *
 * Return:
 * * a pointer to a devm-managed string
 * * ERR_PTR(-ENOENT) if no vendor-specific qualifier
 * * ERR_PTR error value
 */
const char *cs_amp_devm_get_vendor_specific_variant_id(struct device *dev,
						       int ssid_vendor,
						       int ssid_device)
{
	KUNIT_STATIC_STUB_REDIRECT(cs_amp_devm_get_vendor_specific_variant_id,
				   dev, ssid_vendor, ssid_device);

	if ((ssid_vendor == PCI_VENDOR_ID_DELL) || (ssid_vendor < 0))
		return cs_amp_devm_get_dell_ssidex(dev, ssid_vendor, ssid_device);

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_NS_GPL(cs_amp_devm_get_vendor_specific_variant_id, "SND_SOC_CS_AMP_LIB");

/**
 * cs_amp_create_debugfs - create a debugfs directory for a device
 *
 * @dev: pointer to struct device
 *
 * Creates a node under "cirrus_logic" in the root of the debugfs filesystem.
 * This is for Cirrus-specific debugfs functionality to be grouped in a
 * defined way, independently of the debugfs provided by ALSA/ASoC.
 * The general ALSA/ASoC debugfs may not be enabled, and does not necessarily
 * have a stable layout or naming convention.
 *
 * Return: Pointer to the dentry for the created directory, or -ENODEV.
 */
struct dentry *cs_amp_create_debugfs(struct device *dev)
{
	struct dentry *dir;

	dir = debugfs_lookup("cirrus_logic", NULL);
	if (!dir)
		dir = debugfs_create_dir("cirrus_logic", NULL);

	return debugfs_create_dir(dev_name(dev), dir);
}
EXPORT_SYMBOL_NS_GPL(cs_amp_create_debugfs, "SND_SOC_CS_AMP_LIB");

static const struct cs_amp_test_hooks cs_amp_test_hook_ptrs = {
	.get_efi_variable = cs_amp_get_efi_variable,
	.set_efi_variable = cs_amp_set_efi_variable,
	.write_cal_coeff = cs_amp_write_cal_coeff,
	.read_cal_coeff = cs_amp_read_cal_coeff,
};

const struct cs_amp_test_hooks * const cs_amp_test_hooks =
	PTR_IF(IS_ENABLED(CONFIG_SND_SOC_CS_AMP_LIB_TEST_HOOKS), &cs_amp_test_hook_ptrs);
EXPORT_SYMBOL_NS_GPL(cs_amp_test_hooks, "SND_SOC_CS_AMP_LIB");

MODULE_DESCRIPTION("Cirrus Logic amplifier library");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("FW_CS_DSP");
