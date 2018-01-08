/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note)) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Author: Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>
 */

#ifndef TONE_H
#define TONE_H

/* Component will reject non-matching configuration. The version number need
 * to be incremented with any ABI changes in function fir_cmd().
 */
#define SOF_TONE_ABI_VERSION		1

#define SOF_TONE_IDX_FREQUENCY		0
#define SOF_TONE_IDX_AMPLITUDE		1
#define SOF_TONE_IDX_FREQ_MULT		2
#define SOF_TONE_IDX_AMPL_MULT		3
#define SOF_TONE_IDX_LENGTH		4
#define SOF_TONE_IDX_PERIOD		5
#define SOF_TONE_IDX_REPEATS		6
#define SOF_TONE_IDX_LIN_RAMP_STEP	7

#endif /* TONE_ABI_H */
