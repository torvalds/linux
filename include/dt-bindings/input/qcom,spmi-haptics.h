/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This header provides constants for pmi8998 SPMI haptics options.
 */

#ifndef _DT_BINDINGS_QCOM_PMIC_SPMI_HAPTICS_
#define _DT_BINDINGS_QCOM_PMIC_SPMI_HAPTICS_

// Actuator types
#define HAP_TYPE_LRA		0
#define HAP_TYPE_ERM		1

// LRA Wave type
#define HAP_WAVE_SINE		0
#define HAP_WAVE_SQUARE		1

// Play modes
#define HAP_PLAY_DIRECT		0
#define HAP_PLAY_BUFFER		1
#define HAP_PLAY_AUDIO		2
#define HAP_PLAY_PWM		3

#define HAP_PLAY_MAX		HAP_PLAY_PWM

// Auto resonance type
#define HAP_AUTO_RES_NONE	0
#define HAP_AUTO_RES_ZXD	1
#define HAP_AUTO_RES_QWD	2
#define HAP_AUTO_RES_MAX_QWD	3
#define HAP_AUTO_RES_ZXD_EOP	4

#endif /* _DT_BINDINGS_QCOM_PMIC_SPMI_HAPTICS_ */
