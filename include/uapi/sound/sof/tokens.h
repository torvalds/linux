/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *         Keyon Jie <yang.jie@linux.intel.com>
 */

/*
 * Topology IDs and tokens.
 *
 * ** MUST BE ALIGNED WITH TOPOLOGY CONFIGURATION TOKEN VALUES **
 */

#ifndef __INCLUDE_UAPI_SOF_TOPOLOGY_H__
#define __INCLUDE_UAPI_SOF_TOPOLOGY_H__

/*
 * Kcontrol IDs
 */
#define SOF_TPLG_KCTL_VOL_ID	256
#define SOF_TPLG_KCTL_ENUM_ID	257
#define SOF_TPLG_KCTL_BYTES_ID	258
#define SOF_TPLG_KCTL_SWITCH_ID	259

/*
 * Tokens - must match values in topology configurations
 */

/* buffers */
#define SOF_TKN_BUF_SIZE			100
#define SOF_TKN_BUF_CAPS			101

/* DAI */
/* Token retired with ABI 3.2, do not use for new capabilities
 * #define	SOF_TKN_DAI_DMAC_CONFIG			153
 */
#define SOF_TKN_DAI_TYPE			154
#define SOF_TKN_DAI_INDEX			155
#define SOF_TKN_DAI_DIRECTION			156

/* scheduling */
#define SOF_TKN_SCHED_PERIOD			200
#define SOF_TKN_SCHED_PRIORITY			201
#define SOF_TKN_SCHED_MIPS			202
#define SOF_TKN_SCHED_CORE			203
#define SOF_TKN_SCHED_FRAMES			204
#define SOF_TKN_SCHED_TIME_DOMAIN		205

/* volume */
#define SOF_TKN_VOLUME_RAMP_STEP_TYPE		250
#define SOF_TKN_VOLUME_RAMP_STEP_MS		251

/* SRC */
#define SOF_TKN_SRC_RATE_IN			300
#define SOF_TKN_SRC_RATE_OUT			301

/* PCM */
#define SOF_TKN_PCM_DMAC_CONFIG			353

/* Generic components */
#define SOF_TKN_COMP_PERIOD_SINK_COUNT		400
#define SOF_TKN_COMP_PERIOD_SOURCE_COUNT	401
#define SOF_TKN_COMP_FORMAT			402
/* Token retired with ABI 3.2, do not use for new capabilities
 * #define SOF_TKN_COMP_PRELOAD_COUNT		403
 */

/* SSP */
#define SOF_TKN_INTEL_SSP_CLKS_CONTROL		500
#define SOF_TKN_INTEL_SSP_MCLK_ID		501
#define SOF_TKN_INTEL_SSP_SAMPLE_BITS		502
#define SOF_TKN_INTEL_SSP_FRAME_PULSE_WIDTH	503
#define SOF_TKN_INTEL_SSP_QUIRKS		504
#define SOF_TKN_INTEL_SSP_TDM_PADDING_PER_SLOT	505
#define SOF_TKN_INTEL_SSP_BCLK_DELAY		506

/* DMIC */
#define SOF_TKN_INTEL_DMIC_DRIVER_VERSION	600
#define SOF_TKN_INTEL_DMIC_CLK_MIN		601
#define SOF_TKN_INTEL_DMIC_CLK_MAX		602
#define SOF_TKN_INTEL_DMIC_DUTY_MIN		603
#define SOF_TKN_INTEL_DMIC_DUTY_MAX		604
#define SOF_TKN_INTEL_DMIC_NUM_PDM_ACTIVE	605
#define SOF_TKN_INTEL_DMIC_SAMPLE_RATE		608
#define SOF_TKN_INTEL_DMIC_FIFO_WORD_LENGTH	609
#define SOF_TKN_INTEL_DMIC_UNMUTE_RAMP_TIME_MS  610

/* DMIC PDM */
#define SOF_TKN_INTEL_DMIC_PDM_CTRL_ID		700
#define SOF_TKN_INTEL_DMIC_PDM_MIC_A_Enable	701
#define SOF_TKN_INTEL_DMIC_PDM_MIC_B_Enable	702
#define SOF_TKN_INTEL_DMIC_PDM_POLARITY_A	703
#define SOF_TKN_INTEL_DMIC_PDM_POLARITY_B	704
#define SOF_TKN_INTEL_DMIC_PDM_CLK_EDGE		705
#define SOF_TKN_INTEL_DMIC_PDM_SKEW		706

/* Tone */
#define SOF_TKN_TONE_SAMPLE_RATE		800

/* Processing Components */
#define SOF_TKN_PROCESS_TYPE                    900

/* for backward compatibility */
#define SOF_TKN_EFFECT_TYPE	SOF_TKN_PROCESS_TYPE

/* SAI */
#define SOF_TKN_IMX_SAI_FIRST_TOKEN		1000
/* TODO: Add SAI tokens */

/* ESAI */
#define SOF_TKN_IMX_ESAI_MCLK_ID		1100

/* Led control for mute switches */
#define SOF_TKN_MUTE_LED_USE			1300
#define SOF_TKN_MUTE_LED_DIRECTION		1301

#endif
