/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef CS_AMP_LIB_H
#define CS_AMP_LIB_H

#include <linux/efi.h>
#include <linux/types.h>

struct cs_dsp;

struct cirrus_amp_cal_data {
	u32 calTarget[2];
	u32 calTime[2];
	s8  calAmbient;
	u8  calStatus;
	u16 calR;
} __packed;

struct cirrus_amp_efi_data {
	u32 size;
	u32 count;
	struct cirrus_amp_cal_data data[] __counted_by(count);
} __packed;

/**
 * struct cirrus_amp_cal_controls - definition of firmware calibration controls
 * @alg_id:	ID of algorithm containing the controls.
 * @mem_region:	DSP memory region containing the controls.
 * @ambient:	Name of control for calAmbient value.
 * @calr:	Name of control for calR value.
 * @status:	Name of control for calStatus value.
 * @checksum:	Name of control for checksum value.
 */
struct cirrus_amp_cal_controls {
	unsigned int alg_id;
	int mem_region;
	const char *ambient;
	const char *calr;
	const char *status;
	const char *checksum;
};

int cs_amp_write_cal_coeffs(struct cs_dsp *dsp,
			    const struct cirrus_amp_cal_controls *controls,
			    const struct cirrus_amp_cal_data *data);
int cs_amp_read_cal_coeffs(struct cs_dsp *dsp,
			   const struct cirrus_amp_cal_controls *controls,
			   struct cirrus_amp_cal_data *data);
int cs_amp_write_ambient_temp(struct cs_dsp *dsp,
			      const struct cirrus_amp_cal_controls *controls,
			      u32 temp);
int cs_amp_get_efi_calibration_data(struct device *dev, u64 target_uid, int amp_index,
				    struct cirrus_amp_cal_data *out_data);
int cs_amp_set_efi_calibration_data(struct device *dev, int amp_index, int num_amps,
				    const struct cirrus_amp_cal_data *in_data);
int cs_amp_get_vendor_spkid(struct device *dev);
const char *cs_amp_devm_get_vendor_specific_variant_id(struct device *dev,
						       int ssid_vendor,
						       int ssid_device);
struct dentry *cs_amp_create_debugfs(struct device *dev);

static inline u64 cs_amp_cal_target_u64(const struct cirrus_amp_cal_data *data)
{
	return ((u64)data->calTarget[1] << 32) | data->calTarget[0];
}

struct cs_amp_test_hooks {
	efi_status_t (*get_efi_variable)(efi_char16_t *name,
					 efi_guid_t *guid,
					 u32 *returned_attr,
					 unsigned long *size,
					 void *buf);
	efi_status_t (*set_efi_variable)(efi_char16_t *name,
					 efi_guid_t *guid,
					 u32 attr,
					 unsigned long size,
					 void *buf);

	int (*write_cal_coeff)(struct cs_dsp *dsp,
			       const struct cirrus_amp_cal_controls *controls,
			       const char *ctl_name, u32 val);

	int (*read_cal_coeff)(struct cs_dsp *dsp,
			      const struct cirrus_amp_cal_controls *controls,
			      const char *ctl_name, u32 *val);
};
extern const struct cs_amp_test_hooks * const cs_amp_test_hooks;

#endif /* CS_AMP_LIB_H */
