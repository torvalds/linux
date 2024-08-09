// SPDX-License-Identifier: GPL-2.0-only
//
// Common code for Cirrus Logic Smart Amplifiers
//
// Copyright (C) 2024 Cirrus Logic, Inc. and
//               Cirrus Logic International Semiconductor Ltd.

#include <asm/byteorder.h>
#include <kunit/static_stub.h>
#include <linux/dev_printk.h>
#include <linux/efi.h>
#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <sound/cs-amp-lib.h>

#define CS_AMP_CAL_GUID \
	EFI_GUID(0x02f9af02, 0x7734, 0x4233, 0xb4, 0x3d, 0x93, 0xfe, 0x5a, 0xa3, 0x5d, 0xb3)

#define CS_AMP_CAL_NAME	L"CirrusSmartAmpCalibrationData"

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
	if (IS_REACHABLE(CONFIG_FW_CS_DSP) || IS_ENABLED(CONFIG_SND_SOC_CS_AMP_LIB_TEST))
		return _cs_amp_write_cal_coeffs(dsp, controls, data);
	else
		return -ENODEV;
}
EXPORT_SYMBOL_NS_GPL(cs_amp_write_cal_coeffs, SND_SOC_CS_AMP_LIB);

static efi_status_t cs_amp_get_efi_variable(efi_char16_t *name,
					    efi_guid_t *guid,
					    unsigned long *size,
					    void *buf)
{
	u32 attr;

	KUNIT_STATIC_STUB_REDIRECT(cs_amp_get_efi_variable, name, guid, size, buf);

	if (efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE))
		return efi.get_variable(name, guid, &attr, size, buf);

	return EFI_NOT_FOUND;
}

static struct cirrus_amp_efi_data *cs_amp_get_cal_efi_buffer(struct device *dev)
{
	struct cirrus_amp_efi_data *efi_data;
	unsigned long data_size = 0;
	u8 *data;
	efi_status_t status;
	int ret;

	/* Get real size of UEFI variable */
	status = cs_amp_get_efi_variable(CS_AMP_CAL_NAME, &CS_AMP_CAL_GUID, &data_size, NULL);
	if (status != EFI_BUFFER_TOO_SMALL)
		return ERR_PTR(-ENOENT);

	if (data_size < sizeof(*efi_data)) {
		dev_err(dev, "EFI cal variable truncated\n");
		return ERR_PTR(-EOVERFLOW);
	}

	/* Get variable contents into buffer */
	data = kmalloc(data_size, GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	status = cs_amp_get_efi_variable(CS_AMP_CAL_NAME, &CS_AMP_CAL_GUID, &data_size, data);
	if (status != EFI_SUCCESS) {
		ret = -EINVAL;
		goto err;
	}

	efi_data = (struct cirrus_amp_efi_data *)data;
	dev_dbg(dev, "Calibration: Size=%d, Amp Count=%d\n", efi_data->size, efi_data->count);

	if ((efi_data->count > 128) ||
	    offsetof(struct cirrus_amp_efi_data, data[efi_data->count]) > data_size) {
		dev_err(dev, "EFI cal variable truncated\n");
		ret = -EOVERFLOW;
		goto err;
	}

	return efi_data;

err:
	kfree(data);
	dev_err(dev, "Failed to read calibration data from EFI: %d\n", ret);

	return ERR_PTR(ret);
}

static u64 cs_amp_cal_target_u64(const struct cirrus_amp_cal_data *data)
{
	return ((u64)data->calTarget[1] << 32) | data->calTarget[0];
}

static int _cs_amp_get_efi_calibration_data(struct device *dev, u64 target_uid, int amp_index,
					    struct cirrus_amp_cal_data *out_data)
{
	struct cirrus_amp_efi_data *efi_data;
	struct cirrus_amp_cal_data *cal = NULL;
	int i, ret;

	efi_data = cs_amp_get_cal_efi_buffer(dev);
	if (IS_ERR(efi_data))
		return PTR_ERR(efi_data);

	if (target_uid) {
		for (i = 0; i < efi_data->count; ++i) {
			u64 cal_target = cs_amp_cal_target_u64(&efi_data->data[i]);

			/* Skip entries with unpopulated silicon ID */
			if (cal_target == 0)
				continue;

			if (cal_target == target_uid) {
				cal = &efi_data->data[i];
				break;
			}
		}
	}

	if (!cal && (amp_index >= 0) && (amp_index < efi_data->count)) {
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
	if (IS_ENABLED(CONFIG_EFI) || IS_ENABLED(CONFIG_SND_SOC_CS_AMP_LIB_TEST))
		return _cs_amp_get_efi_calibration_data(dev, target_uid, amp_index, out_data);
	else
		return -ENOENT;
}
EXPORT_SYMBOL_NS_GPL(cs_amp_get_efi_calibration_data, SND_SOC_CS_AMP_LIB);

static const struct cs_amp_test_hooks cs_amp_test_hook_ptrs = {
	.get_efi_variable = cs_amp_get_efi_variable,
	.write_cal_coeff = cs_amp_write_cal_coeff,
};

const struct cs_amp_test_hooks * const cs_amp_test_hooks =
	PTR_IF(IS_ENABLED(CONFIG_SND_SOC_CS_AMP_LIB_TEST), &cs_amp_test_hook_ptrs);
EXPORT_SYMBOL_NS_GPL(cs_amp_test_hooks, SND_SOC_CS_AMP_LIB);

MODULE_DESCRIPTION("Cirrus Logic amplifier library");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(FW_CS_DSP);
