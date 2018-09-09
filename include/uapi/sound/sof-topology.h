// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note)) OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//         Keyon Jie <yang.jie@linux.intel.com>
//

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

/*
 * Tokens - must match values in topology configurations
 */

/* buffers */
#define SOF_TKN_BUF_SIZE			100
#define SOF_TKN_BUF_CAPS			101

/* DAI */
#define	SOF_TKN_DAI_DMAC_CONFIG			153
#define SOF_TKN_DAI_TYPE			154
#define SOF_TKN_DAI_INDEX			155

/* scheduling */
#define SOF_TKN_SCHED_DEADLINE			200
#define SOF_TKN_SCHED_PRIORITY			201
#define SOF_TKN_SCHED_MIPS			202
#define SOF_TKN_SCHED_CORE			203
#define SOF_TKN_SCHED_FRAMES			204
#define SOF_TKN_SCHED_TIMER			205

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
#define SOF_TKN_COMP_PRELOAD_COUNT		403

/* SSP */
#define SOF_TKN_INTEL_SSP_MCLK_KEEP_ACTIVE	500
#define SOF_TKN_INTEL_SSP_BCLK_KEEP_ACTIVE	501
#define SOF_TKN_INTEL_SSP_FS_KEEP_ACTIVE	502
#define SOF_TKN_INTEL_SSP_MCLK_ID		503
#define SOF_TKN_INTEL_SSP_SAMPLE_BITS		504
#define SOF_TKN_INTEL_SSP_FRAME_PULSE_WIDTH	505
#define SOF_TKN_INTEL_SSP_QUIRKS		506
#define SOF_TKN_INTEL_SSP_TDM_PADDING_PER_SLOT	507

/* DMIC */
#define SOF_TKN_INTEL_DMIC_DRIVER_VERSION	600
#define SOF_TKN_INTEL_DMIC_CLK_MIN		601
#define SOF_TKN_INTEL_DMIC_CLK_MAX		602
#define SOF_TKN_INTEL_DMIC_DUTY_MIN		603
#define SOF_TKN_INTEL_DMIC_DUTY_MAX		604
#define SOF_TKN_INTEL_DMIC_NUM_PDM_ACTIVE	605
#define SOF_TKN_INTEL_DMIC_SAMPLE_RATE		608
#define SOF_TKN_INTEL_DMIC_FIFO_WORD_LENGTH	609

/* DMIC PDM */
#define SOF_TKN_INTEL_DMIC_PDM_CTRL_ID		700
#define SOF_TKN_INTEL_DMIC_PDM_MIC_A_Enable	701
#define SOF_TKN_INTEL_DMIC_PDM_MIC_B_Enable	702
#define SOF_TKN_INTEL_DMIC_PDM_POLARITY_A	703
#define SOF_TKN_INTEL_DMIC_PDM_POLARITY_B	704
#define SOF_TKN_INTEL_DMIC_PDM_CLK_EDGE		705
#define SOF_TKN_INTEL_DMIC_PDM_SKEW		706

#define SOF_TKN_EFFECT_TYPE                     900

#endif
