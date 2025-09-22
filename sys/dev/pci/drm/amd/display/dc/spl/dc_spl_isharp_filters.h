// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DC_SPL_ISHARP_FILTERS_H__
#define __DC_SPL_ISHARP_FILTERS_H__

#include "dc_spl_types.h"

#define ISHARP_LUT_TABLE_SIZE 32
const uint32_t *spl_get_filter_isharp_1D_lut_0(void);
const uint32_t *spl_get_filter_isharp_1D_lut_0p5x(void);
const uint32_t *spl_get_filter_isharp_1D_lut_1p0x(void);
const uint32_t *spl_get_filter_isharp_1D_lut_1p5x(void);
const uint32_t *spl_get_filter_isharp_1D_lut_2p0x(void);
const uint32_t *spl_get_filter_isharp_1D_lut_3p0x(void);
uint16_t *spl_get_filter_isharp_bs_4tap_in_6_64p(void);
uint16_t *spl_get_filter_isharp_bs_4tap_64p(void);
uint16_t *spl_get_filter_isharp_bs_3tap_64p(void);
const uint16_t *spl_get_filter_isharp_wide_6tap_64p(void);
uint16_t *spl_dscl_get_blur_scale_coeffs_64p(int taps);

#define NUM_SHARPNESS_ADJ_LEVELS 6
struct scale_ratio_to_sharpness_level_adj {
	unsigned int ratio_numer;
	unsigned int ratio_denom;
	unsigned int level_down_adj; /* adjust sharpness level down */
};

struct isharp_1D_lut_pregen {
	unsigned int sharpness_numer;
	unsigned int sharpness_denom;
	uint32_t value[ISHARP_LUT_TABLE_SIZE];
};

enum system_setup {
	SDR_NL = 0,
	SDR_L,
	HDR_NL,
	HDR_L,
	NUM_SHARPNESS_SETUPS
};

void spl_init_blur_scale_coeffs(void);
void spl_set_blur_scale_data(struct dscl_prog_data *dscl_prog_data,
	const struct spl_scaler_data *data);

void spl_build_isharp_1dlut_from_reference_curve(struct spl_fixed31_32 ratio, enum system_setup setup,
	struct adaptive_sharpness sharpness, enum scale_to_sharpness_policy scale_to_sharpness_policy);
uint32_t *spl_get_pregen_filter_isharp_1D_lut(enum system_setup setup);
#endif /* __DC_SPL_ISHARP_FILTERS_H__ */
