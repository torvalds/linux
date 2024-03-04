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
	struct cirrus_amp_cal_data data[];
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
int cs_amp_get_efi_calibration_data(struct device *dev, u64 target_uid, int amp_index,
				    struct cirrus_amp_cal_data *out_data);

struct cs_amp_test_hooks {
	efi_status_t (*get_efi_variable)(efi_char16_t *name,
					 efi_guid_t *guid,
					 unsigned long *size,
					 void *buf);

	int (*write_cal_coeff)(struct cs_dsp *dsp,
			       const struct cirrus_amp_cal_controls *controls,
			       const char *ctl_name, u32 val);
};

extern const struct cs_amp_test_hooks * const cs_amp_test_hooks;

#endif /* CS_AMP_LIB_H */
