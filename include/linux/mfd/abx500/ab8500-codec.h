/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>
 *         for ST-Ericsson.
 *
 * License terms:
 */

#ifndef AB8500_CORE_CODEC_H
#define AB8500_CORE_CODEC_H

/* Mic-types */
enum amic_type {
	AMIC_TYPE_SINGLE_ENDED,
	AMIC_TYPE_DIFFERENTIAL
};

/* Mic-biases */
enum amic_micbias {
	AMIC_MICBIAS_VAMIC1,
	AMIC_MICBIAS_VAMIC2,
	AMIC_MICBIAS_UNKNOWN
};

/* Bias-voltage */
enum ear_cm_voltage {
	EAR_CMV_0_95V,
	EAR_CMV_1_10V,
	EAR_CMV_1_27V,
	EAR_CMV_1_58V,
	EAR_CMV_UNKNOWN
};

/* Analog microphone settings */
struct amic_settings {
	enum amic_type mic1_type;
	enum amic_type mic2_type;
	enum amic_micbias mic1a_micbias;
	enum amic_micbias mic1b_micbias;
	enum amic_micbias mic2_micbias;
};

/* Platform data structure for the audio-parts of the AB8500 */
struct ab8500_codec_platform_data {
	struct amic_settings amics;
	enum ear_cm_voltage ear_cmv;
};

#endif
