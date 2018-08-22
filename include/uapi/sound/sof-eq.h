// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note)) OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>
//

#ifndef EQ_H
#define EQ_H

/* FIR EQ type */

/* Component will reject non-matching configuration. The version number need
 * to be incremented with any ABI changes in function fir_cmd().
 */
#define SOF_EQ_FIR_ABI_VERSION  1

#define SOF_EQ_FIR_IDX_SWITCH	0

#define SOF_EQ_FIR_MAX_SIZE 4096 /* Max size allowed for coef data in bytes */

/*
 * eq_fir_configuration data structure contains this information
 *     uint16_t channels_in_config
 *         This describes the number of channels in this EQ config data. It
 *         can be different from PLATFORM_MAX_CHANNELS.
 *     uint16_t number_of_responses
 *         0=no responses, 1=one response defined, 2=two responses defined, etc.
 *     int16_t data[]
 *         assign_response[STREAM_MAX_CHANNELS]
 *             -1 = not defined, 0 = use first response, 1 = use 2nd response, etc.
 *             E.g. {0, 0, 0, 0, -1, -1, -1, -1} would apply to channels 0-3 the
 *	       same first defined response and leave channels 4-7 unequalized.
 *         coef_data[]
 *             Repeated data { filter_length, input_shift, output_shift, h[] }
 *	       for every EQ response defined where vector h has filter_length
 *             number of coefficients. Coefficients in h[] are in Q1.15 format.
 *             E.g. 16384 (Q1.15) = 0.5. The shifts are number of right shifts.
 */

struct sof_eq_fir_config {
	uint16_t channels_in_config;
	uint16_t number_of_responses;
	int16_t data[];
};

/* IIR EQ type */

/* Component will reject non-matching configuration. The version number need
 * to be incremented with any ABI changes in function fir_cmd().
 */
#define SOF_EQ_FIR_ABI_VERSION  1

#define SOF_EQ_IIR_IDX_SWITCH   0

#define SOF_EQ_IIR_MAX_SIZE 1024 /* Max size allowed for coef data in bytes */

/* eq_iir_configuration
 *     uint32_t channels_in_config
 *         This describes the number of channels in this EQ config data. It
 *         can be different from PLATFORM_MAX_CHANNELS.
 *     uint32_t number_of_responses_defined
 *         0=no responses, 1=one response defined, 2=two responses defined, etc.
 *     int32_t data[]
 *         Data consist of two parts. First is the response assign vector that
 *	   has length of channels_in_config. The latter part is coefficient
 *         data.
 *         uint32_t assign_response[channels_in_config]
 *             -1 = not defined, 0 = use first response, 1 = use 2nd, etc.
 *             E.g. {0, 0, 0, 0, -1, -1, -1, -1} would apply to channels 0-3 the
 *             same first defined response and leave channels 4-7 unequalized.
 *         coefficient_data[]
 *             <1st EQ>
 *             uint32_t num_biquads
 *             uint32_t num_biquads_in_series
 *             <1st biquad>
 *             int32_t coef_a2       Q2.30 format
 *             int32_t coef_a1       Q2.30 format
 *             int32_t coef_b2       Q2.30 format
 *             int32_t coef_b1       Q2.30 format
 *             int32_t coef_b0       Q2.30 format
 *             int32_t output_shift  number of shifts right, shift left is negative
 *             int32_t output_gain   Q2.14 format
 *             <2nd biquad>
 *             ...
 *             <2nd EQ>
 *
 *         Note: A flat response biquad can be made with a section set to
 *         b0 = 1.0, gain = 1.0, and other parameters set to 0
 *         {0, 0, 0, 0, 1073741824, 0, 16484}
 */

struct sof_eq_iir_config {
	uint32_t channels_in_config;
	uint32_t number_of_responses;
	int32_t data[];
};

#endif /* EQ_H */
