// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright (c) 2025 Qualcomm Technologies, Inc. All rights reserved.

/*
 * WCD9378 (Tambora) SDCA SimpleJack codec. Supplies the static SDCA
 * topology on DT platforms where no ACPI/DisCo enumeration exists.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/workqueue.h>
#include <sound/pcm.h>
#include <sound/sdca.h>
#include <sound/sdca_class.h>
#include <sound/sdca_function.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include "wcd9378-sdca.h"

struct wcd9378_priv {
	struct sdca_class_drv class;
};

static struct sdca_entity wcd9378_sdca_entities[];
/*
 * Entity array index map. Order matches the ASL entity-id-list;
 * Function (Entity 0) is last.
 *
 *  [0]  E001 IT 41    (0x1)   [12] E00F IT 33    (0xf)
 *  [1]  E002 CS 41    (0x2)   [13] E010 PDE 34   (0x10)
 *  [2]  E003 MFPU 21  (0x3)   [14] E011 FU 33    (0x11)
 *  [3]  E004 XU 42    (0x4)   [15] E012 SU 35    (0x12)
 *  [4]  E007 SU 43    (0x7)   [16] E013 XU 36    (0x13)
 *  [5]  E008 SU 45    (0x8)   [17] E015 CS 36    (0x15)
 *  [6]  E009 PDE 47   (0x9)   [18] E016 OT 36    (0x16)
 *  [7]  E00A OT 43    (0xa)   [19] E017 MFPU 236 (0x17)
 *  [8]  E00B OT 45    (0xb)   [20] E018 CS 236   (0x18)
 *  [9]  E00C GE 35    (0xc)   [21] E019 OT 236   (0x19)
 *  [10] E00D IT 131   (0xd)   [22] E006 FU 6     (0x6)
 *  [11] E00E CS 131   (0xe)   [23] E000 Function (0x0)
 */
#define QSJ_IT41	0
#define QSJ_CS41	1
#define QSJ_MFPU21	2
#define QSJ_XU42	3
#define QSJ_SU43	4
#define QSJ_SU45	5
#define QSJ_PDE47	6
#define QSJ_OT43	7
#define QSJ_OT45	8
#define QSJ_GE35	9
#define QSJ_IT131	10
#define QSJ_CS131	11
#define QSJ_IT33	12
#define QSJ_PDE34	13
#define QSJ_FU33	14
#define QSJ_SU35	15
#define QSJ_XU36	16
#define QSJ_CS36	17
#define QSJ_OT36	18
#define QSJ_MFPU236	19
#define QSJ_CS236	20
#define QSJ_OT236	21
#define QSJ_FU6		22

/* Range data: {cols, rows, data[cols*rows]}, transcribed from ASL. */

/* IT 41 Usage: HIFI (0x2). ULP (0x3) does not route to the HPH analog driver. */
static u32 range_it41_usage_data[] = {
	/* usage, CBN, sample_rate, sample_width, full_scale, noise_floor, tag */
	0x2, 0x2d0, 0xbb80, 0x10, 0x0, 0x0, 0x0,
};

static u32 range_it41_cluster_data[] = { 0x1, 0x1 };

/* IT 41 DataPort selector: DP6 (HPH render). */
static u32 range_it41_dp_data[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* CS 41 SampleRateIndex: 1 -> 48kHz PCM. */
static u32 range_cs41_sr_data[] = { 0x1, 0xbb80 };

/* SU selector: disconnected / connected. */
static u32 range_su_sel_data[] = { 0x0, 0x1 };

/* PDE Requested_PS: PS0 / PS3. */
static u32 range_pde_req_ps_data[] = { 0x0, 0x3 };

/*
 * GE 35 SelectedMode -> terminal type.
 *
 * Tambora MBHC only handles mechanical detection; ADC HP/HS
 * discrimination is not implemented, so modes 0 (Unplugged) and 1
 * (Unknown) are aliased to Headphone (mode 4) to keep the DAPM path
 * alive. Mode 2 (Line-out) is not fitted on this board.
 */
static u32 range_ge35_mode_data[] = {
	0x0, 0x6c0,	/* Unplugged -> HPH (alias) */
	0x1, 0x6c0,	/* Unknown -> HPH (alias) */
	0x3, 0x6d0,	/* Headset */
	0x4, 0x6c0,	/* Headphone */
};

/* IT 131 Usage: optimization render stream at 192kHz. */
static u32 range_it131_usage_data[] = {
	0x3, 0x334, 0x2ee00, 0x8, 0x0, 0x0, 0x0,
};

static u32 range_it131_cluster_data[] = { 0x1, 0x3 };

/* IT 131 DataPort selector: DP7. */
static u32 range_it131_dp_data[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* CS 131 SampleRateIndex: 1 -> 192kHz. */
static u32 range_cs131_sr_data[] = { 0x1, 0x2ee00 };

/* IT 33 MIC_BIAS: SDCA MIC_BIAS index 0x5 (2.75V). */
static u32 range_it33_micbias_data[] = { 0x5 };

static u32 range_it33_usage_data[] = {
	0x1, 0x2c6, 0x0, 0x0, 0x0, 0x0, 0x0,
};

static u32 range_it33_cluster_data[] = { 0x1, 0x2 };

static u32 range_cs36_sr_data[] = { 0x1, 0xbb80 };

/* OT 36 Usage: PDM capture, host-visible 48kHz/16-bit PCM. */
static u32 range_ot36_usage_data[] = {
	0x1, 0x2c6, 0xbb80, 0x10, 0x0, 0x0, 0x0,
};

/* OT 36 DataPort selector: DP2. */
static u32 range_ot36_dp_data[] = {
	0xff, 0xff, 0x2, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static u32 range_cs236_sr_data[] = { 0x1, 0xbb80 };

/* OT 236 Usage: optimization capture at 192kHz (clocked by CS 131). */
static u32 range_ot236_usage_data[] = {
	0x1, 0x334, 0x2ee00, 0x8, 0x0, 0x0, 0x0,
};

/* OT 236 DataPort selector: DP5. */
static u32 range_ot236_dp_data[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0x5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

/* Entity 0 (Function) Control Values */
static int ctrl_fun_sdca_ver_vals[] = { 0x11 };
static int ctrl_fun_type_vals[] = { 0x08 };	/* SimpleJack */
static int ctrl_fun_man_id_vals[] = { 0x0217 };
static int ctrl_fun_id_vals[] = { 0x3 };
static int ctrl_fun_ver_vals[] = { 0x0 };
static int ctrl_dev_sdca_ver_vals[] = { 0x11 };

/* Entity 1 (IT 41) Control Values */
static int ctrl_it41_latency_vals[] = { 0x0 };
static int ctrl_it41_cluster_vals[] = { 0x1 };
static int ctrl_it41_dp_vals[] = { 0x6 };

/* Entity 2 (CS 41) Control Values */
static int ctrl_cs41_sr_vals[] = { 0x1 };

/* Entity 3 (MFPU 21) Control Values */
static int ctrl_mfpu21_bypass_vals[] = { 0x1 };

/* Entity 4 (XU 42) Control Values */
static int ctrl_xu42_id_vals[] = { 0x2131 };
static int ctrl_xu42_ver_vals[] = { 0x1 };

/* Entity 0xd (IT 131) Control Values */
static int ctrl_it131_latency_vals[] = { 0x0 };
static int ctrl_it131_cluster_vals[] = { 0x1 };
static int ctrl_it131_dp_vals[] = { 0x7 };

/* Entity 0xe (CS 131) Control Values */
static int ctrl_cs131_sr_vals[] = { 0x1 };

/* IT 33 MIC_BIAS: fixed 2.75V (SDCA MIC_BIAS index 0x5), reapplied by PDE34. */
static int ctrl_it33_micbias_vals[] = { 0x5 };
static int ctrl_it33_latency_vals[] = { 0x0 };
static int ctrl_it33_cluster_vals[] = { 0x1 };

/* Entity 0x13 (XU 36) Control Values */
static int ctrl_xu36_bypass_vals[] = { 0x1 };
static int ctrl_xu36_id_vals[] = { 0x2131 };
static int ctrl_xu36_ver_vals[] = { 0x1 };

/* Entity 0x15 (CS 36) Control Values */
static int ctrl_cs36_sr_vals[] = { 0x1 };

/* Entity 0x16 (OT 36) Control Values */
static int ctrl_ot36_latency_vals[] = { 0x0 };
static int ctrl_ot36_dp_vals[] = { 0x2 };

/* Entity 0x17 (MFPU 236) Control Values */
static int ctrl_mfpu236_bypass_vals[] = { 0x1 };

/* Entity 0x18 (CS 236) Control Values */
static int ctrl_cs236_sr_vals[] = { 0x1 };

/* Entity 0x19 (OT 236) Control Values */
static int ctrl_ot236_latency_vals[] = { 0x0 };
static int ctrl_ot236_dp_vals[] = { 0x5 };

/* Entity 0 (Function) Controls */
static struct sdca_control entity0_controls[] = {
	{ .sel = 0x1,  .mode = SDCA_ACCESS_MODE_RW,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_COMMIT_GROUP_MASK_NAME },
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_DC,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_fun_sdca_ver_vals, .has_fixed = true,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_FUNCTION_SDCA_VERSION_NAME },
	{ .sel = 0x5,  .mode = SDCA_ACCESS_MODE_DC,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_fun_type_vals, .has_fixed = true,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_FUNCTION_TYPE_NAME },
	{ .sel = 0x6,  .mode = SDCA_ACCESS_MODE_DC,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_fun_man_id_vals, .has_fixed = true,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_FUNCTION_MANUFACTURER_ID_NAME },
	{ .sel = 0x7,  .mode = SDCA_ACCESS_MODE_DC,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_fun_id_vals, .has_fixed = true,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_FUNCTION_ID_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_fun_ver_vals, .has_fixed = true,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_FUNCTION_VERSION_NAME },
	{ .sel = 0x9,  .mode = SDCA_ACCESS_MODE_RO,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .is_volatile = true, .label = SDCA_CTL_FUNCTION_EXTENSION_ID_NAME },
	{ .sel = 0xa,  .mode = SDCA_ACCESS_MODE_RO,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .is_volatile = true, .label = SDCA_CTL_FUNCTION_EXTENSION_VERSION_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_RW1C, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .is_volatile = true, .label = SDCA_CTL_FUNCTION_STATUS_NAME },
	{ .sel = 0x11, .mode = SDCA_ACCESS_MODE_RW1S, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .is_volatile = true, .label = SDCA_CTL_FUNCTION_ACTION_NAME },
	{ .sel = 0x2c, .mode = SDCA_ACCESS_MODE_RO,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .is_volatile = true, .label = SDCA_CTL_DEVICE_MANUFACTURER_ID_NAME },
	{ .sel = 0x2d, .mode = SDCA_ACCESS_MODE_RO,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .is_volatile = true, .label = SDCA_CTL_DEVICE_PART_ID_NAME },
	{ .sel = 0x2e, .mode = SDCA_ACCESS_MODE_RO,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .is_volatile = true, .label = SDCA_CTL_DEVICE_VERSION_NAME },
	{ .sel = 0x2f, .mode = SDCA_ACCESS_MODE_DC,   .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_dev_sdca_ver_vals, .has_fixed = true,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_DEVICE_SDCA_VERSION_NAME },
};

/* Entity 1 (IT 41) Controls */
static struct sdca_control entity_it41_controls[] = {
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_RW, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .range = { .cols = 0x7, .rows = 0x1, .data = range_it41_usage_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_USAGE_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .values = ctrl_it41_latency_vals, .has_fixed = true, .label = SDCA_CTL_LATENCY_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_it41_cluster_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_it41_cluster_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_CLUSTERINDEX_NAME },
	{ .sel = 0x11, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_it41_dp_vals, .has_fixed = true,
	  .range = { .cols = 0x10, .rows = 0x4, .data = range_it41_dp_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_DATAPORT_SELECTOR_NAME },
};

/* Entity 2 (CS 41) Controls */
static struct sdca_control entity_cs41_controls[] = {
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_cs41_sr_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_cs41_sr_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_SAMPLERATEINDEX_NAME },
};

/* Entity 3 (MFPU 21) Controls */
static struct sdca_entity *entity_mfpu21_sources[] = {
	&wcd9378_sdca_entities[QSJ_IT41],
	&wcd9378_sdca_entities[QSJ_IT131],
};

static struct sdca_control entity_mfpu21_controls[] = {
	{ .sel = SDCA_CTL_MFPU_BYPASS, .mode = SDCA_ACCESS_MODE_DC,
	  .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .has_reset = true, .reset = 1,
	  .values = ctrl_mfpu21_bypass_vals, .has_fixed = true, .label = SDCA_CTL_BYPASS_NAME },
};

/* Entity 4 (XU 42) Controls */
static struct sdca_entity *entity_xu42_sources[] = { &wcd9378_sdca_entities[QSJ_MFPU21] };

static struct sdca_control entity_xu42_controls[] = {
	{ .sel = SDCA_CTL_XU_BYPASS, .mode = SDCA_ACCESS_MODE_RW,
	  .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .has_reset = true, .reset = 1,
	  .label = SDCA_CTL_BYPASS_NAME },
	{ .sel = 0x7, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .values = ctrl_xu42_id_vals, .has_fixed = true, .label = SDCA_CTL_XU_ID_NAME },
	{ .sel = 0x8, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .values = ctrl_xu42_ver_vals, .has_fixed = true, .label = SDCA_CTL_XU_VERSION_NAME },
};

/*
 * SU Selector is RO but its value is deterministic from the current
 * GE mode which the class framework drives; leave it non-volatile so
 * regmap caches it and DAPM reads succeed when the codec is
 * runtime-suspended.
 */

/* Entity 7 (SU 43) Controls */
static struct sdca_entity *entity_su43_sources[] = { &wcd9378_sdca_entities[QSJ_XU42] };

static struct sdca_control entity_su43_controls[] = {
	{ .sel = 0x1, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_DEVICE,
	  .cn_list = 0x1,
	  .range = { .cols = 0x1, .rows = 0x2, .data = range_su_sel_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_SELECTOR_NAME },
};

/* Entity 8 (SU 45) Controls */
static struct sdca_entity *entity_su45_sources[] = { &wcd9378_sdca_entities[QSJ_XU42] };

static struct sdca_control entity_su45_controls[] = {
	{ .sel = 0x1, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_DEVICE,
	  .cn_list = 0x1,
	  .range = { .cols = 0x1, .rows = 0x2, .data = range_su_sel_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_SELECTOR_NAME },
};

/*
 * PDE 47 manages the HPH render path (OT 43, OT 45). FU 6 is listed
 * so its cached mute/volume are reasserted on PS0 entry.
 */
static struct sdca_entity *entity_pde47_managed[] = {
	&wcd9378_sdca_entities[QSJ_FU6],
	&wcd9378_sdca_entities[QSJ_OT43],
	&wcd9378_sdca_entities[QSJ_OT45],
};

static struct sdca_pde_delay pde47_delays[] = {
	{ .from_ps = 3, .to_ps = 0, .us = 30000 },
	{ .from_ps = 0, .to_ps = 3, .us = 30000 },
};

static struct sdca_control entity_pde47_controls[] = {
	{ .sel = SDCA_CTL_PDE_REQUESTED_PS, .mode = SDCA_ACCESS_MODE_RW,
	  .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .range = { .cols = 0x1, .rows = 0x2, .data = range_pde_req_ps_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .has_reset = true, .reset = 3,
	  .label = SDCA_CTL_REQUESTED_PS_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .is_volatile = true, .label = SDCA_CTL_ACTUAL_PS_NAME },
	/*
	 * HPH protection IRQs (OCP/CNP/SURGE) fire in codec HW but are
	 * not exposed to Linux; needs an SDCA framework interface for
	 * standalone status-bit IRQs. Wire up in a follow-up.
	 */
};

/* OT 43 (Headphone), OT 45 (Headset): no controls. */
static struct sdca_entity *entity_ot43_sources[] = { &wcd9378_sdca_entities[QSJ_IT41] };
static struct sdca_entity *entity_ot45_sources[] = { &wcd9378_sdca_entities[QSJ_IT41] };

/*
 * GE 35 mode -> SU selector. Modes 0/1 alias to Headphone (see
 * range_ge35_mode_data). SU 45 = source 1 (XU 42) for HPH paths,
 * SU 43 = source 1 (XU 42) for Headset.
 */
static struct sdca_ge_control ge35_mode0_controls[] = {
	{ .id = 0x8, .sel = 0x1, .cn = 0x0, .val = 0x1 },
};

static struct sdca_ge_control ge35_mode1_controls[] = {
	{ .id = 0x8, .sel = 0x1, .cn = 0x0, .val = 0x1 },
};

static struct sdca_ge_control ge35_mode3_controls[] = {
	{ .id = 0x7, .sel = 0x1, .cn = 0x0, .val = 0x1 },
};

static struct sdca_ge_control ge35_mode4_controls[] = {
	{ .id = 0x8, .sel = 0x1, .cn = 0x0, .val = 0x1 },
};

static struct sdca_ge_mode ge35_modes[] = {
	{ .val = 0x0, .num_controls = ARRAY_SIZE(ge35_mode0_controls), .controls = ge35_mode0_controls },
	{ .val = 0x1, .num_controls = ARRAY_SIZE(ge35_mode1_controls), .controls = ge35_mode1_controls },
	{ .val = 0x3, .num_controls = ARRAY_SIZE(ge35_mode3_controls), .controls = ge35_mode3_controls },
	{ .val = 0x4, .num_controls = ARRAY_SIZE(ge35_mode4_controls), .controls = ge35_mode4_controls },
};

static struct sdca_control entity_ge35_controls[] = {
	{ .sel = SDCA_CTL_GE_SELECTED_MODE, .mode = SDCA_ACCESS_MODE_RW,
	  .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .range = { .cols = 0x2, .rows = 0x4, .data = range_ge35_mode_data },
	  .has_reset = true, .reset = 0,
	  .label = SDCA_CTL_SELECTED_MODE_NAME },
	{ .sel = 0x2, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = 4, /* SDCA_4 = GE_DETECTED_MODE, see init_table INTMASK_1 */
	  .is_volatile = true, .label = SDCA_CTL_DETECTED_MODE_NAME },
};

static struct sdca_control entity_it131_controls[] = {
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_RW, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .range = { .cols = 0x7, .rows = 0x1, .data = range_it131_usage_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_USAGE_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .values = ctrl_it131_latency_vals, .has_fixed = true, .label = SDCA_CTL_LATENCY_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_it131_cluster_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_it131_cluster_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_CLUSTERINDEX_NAME },
	{ .sel = 0x11, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_it131_dp_vals, .has_fixed = true,
	  .range = { .cols = 0x10, .rows = 0x4, .data = range_it131_dp_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_DATAPORT_SELECTOR_NAME },
};

/* Entity 0xe (CS 131) Controls */
static struct sdca_control entity_cs131_controls[] = {
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_cs131_sr_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_cs131_sr_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_SAMPLERATEINDEX_NAME },
};

/* Entity 0xf (IT 33) - headset mic input */
static struct sdca_control entity_it33_controls[] = {
	{ .sel = 0x3,  .mode = SDCA_ACCESS_MODE_RW, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_it33_micbias_vals, .has_default = true,
	  .range = { .cols = 0x1, .rows = 0x1, .data = range_it33_micbias_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_MIC_BIAS_NAME },
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_RW, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .range = { .cols = 0x7, .rows = 0x1, .data = range_it33_usage_data },
	  .has_reset = true,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_USAGE_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .values = ctrl_it33_latency_vals, .has_fixed = true, .label = SDCA_CTL_LATENCY_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_it33_cluster_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_it33_cluster_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_CLUSTERINDEX_NAME },
};

/* Entity 0x10 (PDE 34) - manages IT 33 */
static struct sdca_entity *entity_pde34_managed[] = { &wcd9378_sdca_entities[QSJ_IT33] };

static struct sdca_pde_delay pde34_delays[] = {
	{ .from_ps = 3, .to_ps = 0, .us = 30000 },
	{ .from_ps = 0, .to_ps = 3, .us = 30000 },
};

static struct sdca_control entity_pde34_controls[] = {
	{ .sel = SDCA_CTL_PDE_REQUESTED_PS, .mode = SDCA_ACCESS_MODE_RW,
	  .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .range = { .cols = 0x1, .rows = 0x2, .data = range_pde_req_ps_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .has_reset = true, .reset = 3,
	  .label = SDCA_CTL_REQUESTED_PS_NAME },
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_RO, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .is_volatile = true, .label = SDCA_CTL_ACTUAL_PS_NAME },
};

/*
 * FU 6 (vendor FU42): HPH mute + Q7.8 volume. DUAL-mode CVR-alias
 * writes take effect without an SCP_COMMIT.
 */
/*
 * FU 6 volume range: MIN, MAX, STEP in Q7.8 (LSB = 1/256 dB).
 * 0x8000 = -128 dB, 0x7fff = +127.996 dB, STEP = 1. Framework
 * sign-extends and converts to 0.01 dB TLV via (val * 100) >> 8.
 */
static u32 range_fu6_vol_data[] = {
	0x00008000, 0x00007fff, 0x00000001,
};

static struct sdca_control entity_fu6_controls[] = {
	{ .sel = SDCA_CTL_FU_MUTE, .mode = SDCA_ACCESS_MODE_DUAL,
	  .layers = SDCA_ACCESS_LAYER_USER, .cn_list = 0x6, .nbits = 1,
	  .type = SDCA_CTL_DATATYPE_ONEBIT,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .has_reset = true, .reset = 1,
	  .label = SDCA_CTL_MUTE_NAME },
	{ .sel = SDCA_CTL_FU_CHANNEL_VOLUME, .mode = SDCA_ACCESS_MODE_DUAL,
	  .layers = SDCA_ACCESS_LAYER_USER, .cn_list = 0x6, .nbits = 16,
	  .type = SDCA_CTL_DATATYPE_Q7P8DB,
	  .range = { .cols = SDCA_VOLUME_LINEAR_NCOLS, .rows = 1,
		     .data = range_fu6_vol_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_CHANNEL_VOLUME_NAME },
};

/* Entity 0x11 (FU 33) - no controls */
static struct sdca_entity *entity_fu33_sources[] = { &wcd9378_sdca_entities[QSJ_IT33] };

/* Entity 0x12 (SU 35) Controls */
static struct sdca_entity *entity_su35_sources[] = { &wcd9378_sdca_entities[QSJ_FU33] };

/*
 * SU 35 is class-layer: the SDCA class framework drives the Selector
 * directly via a DAPM mux widget, so the Control must be writeable.
 */
static struct sdca_control entity_su35_controls[] = {
	{ .sel = 0x1, .mode = SDCA_ACCESS_MODE_RW, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .range = { .cols = 0x1, .rows = 0x2, .data = range_su_sel_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_SELECTOR_NAME },
};

/* Entity 0x13 (XU 36) Controls */
static struct sdca_entity *entity_xu36_sources[] = { &wcd9378_sdca_entities[QSJ_SU35] };

static struct sdca_control entity_xu36_controls[] = {
	/* bypass=1: pass mic signal through XU36 without proprietary processing */
	{ .sel = SDCA_CTL_XU_BYPASS, .mode = SDCA_ACCESS_MODE_RW,
	  .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .has_reset = true, .reset = 1,
	  .values = ctrl_xu36_bypass_vals, .has_default = true, .label = SDCA_CTL_BYPASS_NAME },
	{ .sel = 0x7, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .values = ctrl_xu36_id_vals, .has_fixed = true, .label = SDCA_CTL_XU_ID_NAME },
	{ .sel = 0x8, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .values = ctrl_xu36_ver_vals, .has_fixed = true, .label = SDCA_CTL_XU_VERSION_NAME },
};

/* Entity 0x15 (CS 36) Controls */
static struct sdca_control entity_cs36_controls[] = {
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_cs36_sr_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_cs36_sr_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_SAMPLERATEINDEX_NAME },
};

/* OT 36 mic capture: IT33 -> FU33 -> SU35 -> XU36 -> OT36. */
static struct sdca_entity *entity_ot36_sources[] = { &wcd9378_sdca_entities[QSJ_XU36] };

static struct sdca_control entity_ot36_controls[] = {
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_RW, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .range = { .cols = 0x7, .rows = 0x1, .data = range_ot36_usage_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_USAGE_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .values = ctrl_ot36_latency_vals, .has_fixed = true, .label = SDCA_CTL_LATENCY_NAME },
	{ .sel = 0x11, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_ot36_dp_vals, .has_fixed = true,
	  .range = { .cols = 0x10, .rows = 0x4, .data = range_ot36_dp_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_DATAPORT_SELECTOR_NAME },
};

/* Entity 0x17 (MFPU 236) Controls */
static struct sdca_control entity_mfpu236_controls[] = {
	{ .sel = SDCA_CTL_MFPU_BYPASS, .mode = SDCA_ACCESS_MODE_DC,
	  .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .has_reset = true, .reset = 1,
	  .values = ctrl_mfpu236_bypass_vals, .has_fixed = true, .label = SDCA_CTL_BYPASS_NAME },
};

/* Entity 0x18 (CS 236) Controls */
static struct sdca_control entity_cs236_controls[] = {
	{ .sel = 0x10, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_cs236_sr_vals, .has_fixed = true,
	  .range = { .cols = 0x2, .rows = 0x1, .data = range_cs236_sr_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_SAMPLERATEINDEX_NAME },
};

/* Entity 0x19 (OT 236) - optimization stream capture output */
static struct sdca_entity *entity_ot236_sources[] = { &wcd9378_sdca_entities[QSJ_IT33] };

static struct sdca_control entity_ot236_controls[] = {
	{ .sel = 0x4,  .mode = SDCA_ACCESS_MODE_RW, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .range = { .cols = 0x7, .rows = 0x1, .data = range_ot236_usage_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_USAGE_NAME },
	{ .sel = 0x8,  .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .values = ctrl_ot236_latency_vals, .has_fixed = true, .label = SDCA_CTL_LATENCY_NAME },
	{ .sel = 0x11, .mode = SDCA_ACCESS_MODE_DC, .layers = SDCA_ACCESS_LAYER_CLASS, .cn_list = 0x1,
	  .values = ctrl_ot236_dp_vals, .has_fixed = true,
	  .range = { .cols = 0x10, .rows = 0x4, .data = range_ot236_dp_data },
	  .interrupt_position = SDCA_NO_INTERRUPT,
	  .label = SDCA_CTL_DATAPORT_SELECTOR_NAME },
};

static struct sdca_entity wcd9378_sdca_entities[] = {
	/* [0] E001: IT 41 - PDM render stream input */
	{ .id = 0x1, .label = "IT 41", .type = SDCA_ENTITY_TYPE_IT,
	  .iot = { .type = 0x0191, .is_dataport = true, .clock = &wcd9378_sdca_entities[QSJ_CS41] },
	  .num_controls = ARRAY_SIZE(entity_it41_controls), .controls = entity_it41_controls },
	/* [1] E002: CS 41 */
	{ .id = 0x2, .label = "CS 41", .type = SDCA_ENTITY_TYPE_CS,
	  .cs = { .type = 0x0 },
	  .num_controls = ARRAY_SIZE(entity_cs41_controls), .controls = entity_cs41_controls },
	/* [2] E003: MFPU 21 */
	{ .id = 0x3, .label = "MFPU 21", .type = SDCA_ENTITY_TYPE_MFPU,
	  .num_controls = ARRAY_SIZE(entity_mfpu21_controls), .controls = entity_mfpu21_controls,
	  .num_sources = ARRAY_SIZE(entity_mfpu21_sources), .sources = entity_mfpu21_sources },
	/* [3] E004: XU 42 */
	{ .id = 0x4, .label = "XU 42", .type = SDCA_ENTITY_TYPE_XU,
	  .num_controls = ARRAY_SIZE(entity_xu42_controls), .controls = entity_xu42_controls,
	  .num_sources = ARRAY_SIZE(entity_xu42_sources), .sources = entity_xu42_sources },
	/* [4] E007: SU 43 - render selector (headset path), driven by GE 35 jack detection */
	{ .id = 0x7, .label = "SU 43", .type = SDCA_ENTITY_TYPE_SU,
	  .group = &wcd9378_sdca_entities[QSJ_GE35],
	  .num_controls = ARRAY_SIZE(entity_su43_controls), .controls = entity_su43_controls,
	  .num_sources = ARRAY_SIZE(entity_su43_sources), .sources = entity_su43_sources },
	/* [5] E008: SU 45 - render selector (headphone path), driven by GE 35 jack detection */
	{ .id = 0x8, .label = "SU 45", .type = SDCA_ENTITY_TYPE_SU,
	  .group = &wcd9378_sdca_entities[QSJ_GE35],
	  .num_controls = ARRAY_SIZE(entity_su45_controls), .controls = entity_su45_controls,
	  .num_sources = ARRAY_SIZE(entity_su45_sources), .sources = entity_su45_sources },
	/* [6] E009: PDE 47 - render power domain */
	{ .id = 0x9, .label = "PDE 47", .type = SDCA_ENTITY_TYPE_PDE,
	  .pde = { .num_managed = ARRAY_SIZE(entity_pde47_managed), .managed = entity_pde47_managed,
		   .num_max_delay = ARRAY_SIZE(pde47_delays), .max_delay = pde47_delays },
	  .num_controls = ARRAY_SIZE(entity_pde47_controls), .controls = entity_pde47_controls },
	/* [7] E00A: OT 43 - Headphone on jack */
	{ .id = 0xa, .label = "OT 43", .type = SDCA_ENTITY_TYPE_OT,
	  .iot = { .type = 0x06c0 },
	  .num_sources = ARRAY_SIZE(entity_ot43_sources), .sources = entity_ot43_sources },
	/* [8] E00B: OT 45 - Headset output on jack */
	{ .id = 0xb, .label = "OT 45", .type = SDCA_ENTITY_TYPE_OT,
	  .iot = { .type = 0x06d0 },
	  .num_sources = ARRAY_SIZE(entity_ot45_sources), .sources = entity_ot45_sources },
	/* [9] E00C: GE 35 - jack detection group entity */
	{ .id = 0xc, .label = "GE 35", .type = SDCA_ENTITY_TYPE_GE,
	  .ge = { .num_modes = ARRAY_SIZE(ge35_modes), .modes = ge35_modes },
	  .num_controls = ARRAY_SIZE(entity_ge35_controls), .controls = entity_ge35_controls },
	/* [10] E00D: IT 131 - optimization stream input */
	{ .id = 0xd, .label = "IT 131", .type = SDCA_ENTITY_TYPE_IT,
	  .iot = { .type = 0x0190, .is_dataport = true, .clock = &wcd9378_sdca_entities[QSJ_CS131] },
	  .num_controls = ARRAY_SIZE(entity_it131_controls), .controls = entity_it131_controls },
	/* [11] E00E: CS 131 */
	{ .id = 0xe, .label = "CS 131", .type = SDCA_ENTITY_TYPE_CS,
	  .cs = { .type = 0x0 },
	  .num_controls = ARRAY_SIZE(entity_cs131_controls), .controls = entity_cs131_controls },
	/* [12] E00F: IT 33 - headset mic input */
	{ .id = 0xf, .label = "IT 33", .type = SDCA_ENTITY_TYPE_IT,
	  .iot = { .type = 0x06d0 },
	  .num_controls = ARRAY_SIZE(entity_it33_controls), .controls = entity_it33_controls },
	/* [13] E010: PDE 34 - mic power domain */
	{ .id = 0x10, .label = "PDE 34", .type = SDCA_ENTITY_TYPE_PDE,
	  .pde = { .num_managed = ARRAY_SIZE(entity_pde34_managed), .managed = entity_pde34_managed,
		   .num_max_delay = ARRAY_SIZE(pde34_delays), .max_delay = pde34_delays },
	  .num_controls = ARRAY_SIZE(entity_pde34_controls), .controls = entity_pde34_controls },
	/* [14] E011: FU 33 - mic feature unit */
	{ .id = 0x11, .label = "FU 33", .type = SDCA_ENTITY_TYPE_FU,
	  .num_sources = ARRAY_SIZE(entity_fu33_sources), .sources = entity_fu33_sources },
	/* [15] E012: SU 35 - mic selector */
	{ .id = 0x12, .label = "SU 35", .type = SDCA_ENTITY_TYPE_SU,
	  .num_controls = ARRAY_SIZE(entity_su35_controls), .controls = entity_su35_controls,
	  .num_sources = ARRAY_SIZE(entity_su35_sources), .sources = entity_su35_sources },
	/* [16] E013: XU 36 - mic extension unit */
	{ .id = 0x13, .label = "XU 36", .type = SDCA_ENTITY_TYPE_XU,
	  .num_controls = ARRAY_SIZE(entity_xu36_controls), .controls = entity_xu36_controls,
	  .num_sources = ARRAY_SIZE(entity_xu36_sources), .sources = entity_xu36_sources },
	/* [17] E015: CS 36 */
	{ .id = 0x15, .label = "CS 36", .type = SDCA_ENTITY_TYPE_CS,
	  .cs = { .type = 0x0 },
	  .num_controls = ARRAY_SIZE(entity_cs36_controls), .controls = entity_cs36_controls },
	/* [18] E016: OT 36 - PDM mic capture output */
	{ .id = 0x16, .label = "OT 36", .type = SDCA_ENTITY_TYPE_OT,
	  .iot = { .type = 0x0191, .is_dataport = true, .clock = &wcd9378_sdca_entities[QSJ_CS36] },
	  .num_controls = ARRAY_SIZE(entity_ot36_controls), .controls = entity_ot36_controls,
	  .num_sources = ARRAY_SIZE(entity_ot36_sources), .sources = entity_ot36_sources },
	/* [19] E017: MFPU 236 - optimization TX processing */
	{ .id = 0x17, .label = "MFPU 236", .type = SDCA_ENTITY_TYPE_MFPU,
	  .num_controls = ARRAY_SIZE(entity_mfpu236_controls), .controls = entity_mfpu236_controls },
	/* [20] E018: CS 236 */
	{ .id = 0x18, .label = "CS 236", .type = SDCA_ENTITY_TYPE_CS,
	  .cs = { .type = 0x0 },
	  .num_controls = ARRAY_SIZE(entity_cs236_controls), .controls = entity_cs236_controls },
	/* [21] E019: OT 236 - optimization stream capture output */
	{ .id = 0x19, .label = "OT 236", .type = SDCA_ENTITY_TYPE_OT,
	  .iot = { .type = 0x0190, .is_dataport = true, .clock = &wcd9378_sdca_entities[QSJ_CS131] },
	  .num_controls = ARRAY_SIZE(entity_ot236_controls), .controls = entity_ot236_controls,
	  .num_sources = ARRAY_SIZE(entity_ot236_sources), .sources = entity_ot236_sources },
	/* E006: FU 6 (FU42) - vendor Feature Unit; HPH mute + Q7.8 volume. */
	{ .id = 0x6, .label = "FU 6", .type = SDCA_ENTITY_TYPE_FU,
	  .num_controls = ARRAY_SIZE(entity_fu6_controls), .controls = entity_fu6_controls },
	/* Entity 0 (Function) */
	{ .id = 0x0, .label = "entity0",
	  .num_controls = ARRAY_SIZE(entity0_controls), .controls = entity0_controls },
};

/* Clusters */
static struct sdca_channel cl1_channels[] = {	/* CL01 - render (HPH) stereo */
	{ .id = 0x1, .purpose = 0x1, .relationship = 0x2 },	/* Left */
	{ .id = 0x2, .purpose = 0x1, .relationship = 0x3 },	/* Right */
};

static struct sdca_channel cl2_channels[] = {	/* CL02 - mic capture mono */
	{ .id = 0xff, .purpose = 0x1, .relationship = 0x1 },
};

static struct sdca_channel cl3_channels[] = {	/* CL03 - optimization RX */
	{ .id = 0xff, .purpose = 0x1, .relationship = 0x1 },	/* Mono */
	{ .id = 0x1,  .purpose = 0x1, .relationship = 0x2 },	/* Left */
	{ .id = 0x2,  .purpose = 0x1, .relationship = 0x3 },	/* Right */
};

static struct sdca_channel cl5_channels[] = {	/* CL05 - optimization TX mono */
	{ .id = 0xff, .purpose = 0x1, .relationship = 0x1 },
};

static struct sdca_cluster wcd9378_sdca_clusters[] = {
	{ .id = 0x1, .num_channels = ARRAY_SIZE(cl1_channels), .channels = cl1_channels },
	{ .id = 0x2, .num_channels = ARRAY_SIZE(cl2_channels), .channels = cl2_channels },
	{ .id = 0x3, .num_channels = ARRAY_SIZE(cl3_channels), .channels = cl3_channels },
	{ .id = 0x5, .num_channels = ARRAY_SIZE(cl5_channels), .channels = cl5_channels },
};

/* Init table transcribed from ASL. */
static struct sdca_init_write wcd9378_sdca_init_table[] = {
	{ .addr = 0x401804f0, .val = 0x00 }, /* DIGITAL_PLATFORM_CTL */
	{ .addr = 0x4018046e, .val = 0x10 }, /* DIGITAL_INTR_MODE */
	{ .addr = 0x0000004d, .val = 0x01 }, /* SWRS_SCP_BUSCLOCK_BASE */
	{ .addr = 0x00000062, .val = 0x02 }, /* SWRS_SCP_BUSCLOCK_SCALE_BANK */
	{ .addr = 0x4018016a, .val = 0x80 }, /* CP_DTOP_CTRL_14 */
	{ .addr = 0x40180165, .val = 0x6b }, /* CP_DTOP_CTRL_9 */
	{ .addr = 0x40180103, .val = 0x1e }, /* SLEEP_CTL BG_CTL (0.9V) */
	{ .addr = 0x40180103, .val = 0x9e }, /* SLEEP_CTL BG_EN */
	{ .addr = 0x40180103, .val = 0xde }, /* SLEEP_CTL LDOL_BG_SEL */
	{ .addr = 0x40180029, .val = 0xb5 }, /* BIAS_VBG_FINE_ADJ */
	{ .addr = 0x40180001, .val = 0x80 }, /* ANA_BIAS ANALOG_BIAS_EN */
	{ .addr = 0x40180001, .val = 0xc0 }, /* ANA_BIAS PRECHRG_EN(1) */
	{ .addr = 0x40180001, .val = 0x80 }, /* ANA_BIAS PRECHRG_EN(0) */
	{ .addr = 0x4018007b, .val = 0xa2 }, /* TX_COM_TXFE_DIV_CTL SEQ_BYPASS */
	{ .addr = 0x40180465, .val = 0x17 }, /* PDM_WD_CTL0 TIME_OUT_SEL_PCM */
	{ .addr = 0x40180466, .val = 0x17 }, /* PDM_WD_CTL1 TIME_OUT_SEL_PCM */
	{ .addr = 0x4018006c, .val = 0x01 }, /* MICB1_TEST_CTL_2 IBIAS_LDO_DRIVER */
	{ .addr = 0x40180072, .val = 0x81 }, /* MICB3_TEST_CTL_2 IBIAS_LDO_DRIVER */
	{ .addr = 0x401800ce, .val = 0x38 }, /* HPH_OCP_CTL OCP_FSM_EN */
	{ .addr = 0x401800ce, .val = 0x3a }, /* HPH_OCP_CTL SCD_OP_EN */
	{ .addr = 0x401800d4, .val = 0xe1 }, /* HPH_L_TEST OCP_DET_EN */
	{ .addr = 0x401800d7, .val = 0xe1 }, /* HPH_R_TEST OCP_DET_EN */
	{ .addr = 0x4018044e, .val = 0x04 }, /* CDC_HPH_GAIN_CTL HPHL_RX_EN */
	{ .addr = 0x4018044e, .val = 0x0c }, /* CDC_HPH_GAIN_CTL HPHR_RX_EN */
	{ .addr = 0x4018000f, .val = 0x0c }, /* ANA_TX_CH2 GAIN (18.0dB) */
	{ .addr = 0x40180133, .val = 0x84 }, /* HPH_NEW_INT_RDAC_HD2_CTL_L */
	{ .addr = 0x40180136, .val = 0x84 }, /* HPH_NEW_INT_RDAC_HD2_CTL_R */
	{ .addr = 0x401800d9, .val = 0x19 }, /* HPH_RDAC_CLK_CTL1 OPAMP_CHOP_CLK_EN */
	{ .addr = 0x40180132, .val = 0x50 }, /* HPH_NEW_INT_RDAC_GAIN_CTL RDAC_GAINCTL(0.55) */
	{ .addr = 0x40180510, .val = 0x05 }, /* SEQR_CTRL HPH_UP_T0 */
	{ .addr = 0x40180519, .val = 0x05 }, /* SEQR_CTRL HPH_UP_T9 */
	{ .addr = 0x4018051b, .val = 0x06 }, /* SEQR_CTRL HPH_DN_T0 */
	{ .addr = 0x40180414, .val = 0x02 }, /* CDC_COMP_CTL_0 HPHL_COMP_EN */
	{ .addr = 0x40180414, .val = 0x03 }, /* CDC_COMP_CTL_0 HPHR_COMP_EN */
	{ .addr = 0x401804f2, .val = 0x80 }, /* DRE_DLY_VAL SWR_HPHL(0) */
	{ .addr = 0x401804f2, .val = 0x00 }, /* DRE_DLY_VAL SWR_HPHR(0) */
	{ .addr = 0x40180501, .val = 0x01 }, /* SEQR_CTRL SYS_USAGE_CTRL */
	/* Arms MBHC: jack insertion asserts SDCA_4 (GE_DETECTED_MODE). */
	{ .addr = 0x40180601, .val = 0x01 }, /* MBHC_CTRL DEVICE_DET */
	{ .addr = 0x40180414, .val = 0x00 }, /* CDC_COMP_CTL_0 */
	{ .addr = 0x401804f2, .val = 0x88 }, /* DRE_DLY_VAL */
	{ .addr = 0x40180517, .val = 0x07 }, /* SEQR_CTRL HPH_UP_T7 */
	{ .addr = 0x4018051c, .val = 0x07 }, /* SEQR_CTRL HPH_DN_T1 */
	{ .addr = 0x401800ce, .val = 0x28 }, /* HPH_OCP_CTL */
	{ .addr = 0x401800d4, .val = 0xe0 }, /* HPH_L_TEST */
	{ .addr = 0x401800d7, .val = 0xe0 }, /* HPH_R_TEST */
	{ .addr = 0x40180510, .val = 0x07 }, /* SEQR_CTRL HPH_UP_T0 */
	{ .addr = 0x40c80008, .val = 0x01 }, /* SMP_JACK_CTRL FUNC_ACT (RESET_FUNCTION_NOW) */
	{ .addr = 0x40c00008, .val = 0x02 }, /* SMP_JACK_CTRL CMT_GRP_MASK */
	{ .addr = 0x40c80000, .val = 0xff }, /* SMP_JACK_CTRL FUNC_STAT */
	{ .addr = 0x00000000, .val = 0x08 }, /* clear DP0 INT status SDCA_CASCADE */
	{ .addr = 0x00000041, .val = 0x08 }, /* SCP_INT_STATUS_MASK_1 PORT_0_CASCADE_3 */
	/* Only SDCA_4 unmasked; protection IRQs stay masked. */
	{ .addr = 0x0000005c, .val = 0x10 }, /* INTMASK_1 SDCA_4 (GE_DETECTED_MODE) */
	{ .addr = 0x4018016a, .val = 0x00 }, /* CP_DTOP_CTRL_14 */
	{ .addr = 0x40180165, .val = 0x6b }, /* CP_DTOP_CTRL_9 */
	/*
	 * L_DET_EN is left disabled: asserting SDCA_4 before the machine
	 * card registers the jack blocks the deferred card probe.
	 */
};

/* Function Descriptor */
static struct sdca_function_desc wcd9378_sdca_desc = {
	.adr  = 0x3,
	.type = SDCA_FUNCTION_TYPE_SIMPLE_JACK,
	.name = SDCA_FUNCTION_TYPE_SIMPLE_NAME,
};

/* Main Function Data */
static struct sdca_function_data wcd9378_sdca_data = {
	.desc             = &wcd9378_sdca_desc,
	.num_entities     = ARRAY_SIZE(wcd9378_sdca_entities),
	.entities         = wcd9378_sdca_entities,
	.num_clusters     = ARRAY_SIZE(wcd9378_sdca_clusters),
	.clusters         = wcd9378_sdca_clusters,
	.num_init_table   = ARRAY_SIZE(wcd9378_sdca_init_table),
	.init_table       = wcd9378_sdca_init_table,
	.reset_max_delay  = 25000, /* 25ms — WCD9378 power-on reset completes well within this */
};

/* Vendor SCP register: host clock divide-by-2 (bank 1 shadow). */
#define WCD9378_SCP_HOST_CLK_DIV2_CTL_B1	0xf0

/* Slave ports 1..8, mapped to master ports via qcom,port-mapping. */
#define WCD9378_SDCA_MAX_PORTS			8

static const char * const wcd9378_sdca_supplies[] = {
	"vdd-buck", "vdd-rxtx", "vdd-io", "vdd-mic-bias",
};

int wcd9378_sdca_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	struct device *dev = &slave->dev;
	struct sdw_dpn_prop *sink, *src;
	int ret;

	/*
	 * All SoundWire slave properties for compute-mode WCD9378 are
	 * fixed by the silicon and are not described in DT, so we do
	 * not call sdw_slave_read_prop() here.
	 */
	prop->use_domain_irq = true;
	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY |
			      SDW_SCP_INT1_IMPL_DEF;
	prop->simple_clk_stop_capable = true;
	prop->paging_support = true;
	prop->clock_reg_supported = true;
	prop->lane_control_support = true;

	/* Source ports: DP2 (headset mic), DP5 (optimisation TX). */
	prop->source_ports = BIT(2) | BIT(5);
	/* Sink ports: DP6 (HPH audio), DP7 (HPH envelope), DP8 (optimisation RX). */
	prop->sink_ports = BIT(6) | BIT(7) | BIT(8);

	src = devm_kcalloc(dev, 2, sizeof(*src), GFP_KERNEL);
	if (!src)
		return -ENOMEM;

	src[0].num = 2;
	src[0].type = SDW_DPN_SIMPLE;
	src[0].simple_ch_prep_sm = true;
	src[0].ch_prep_timeout = 10;
	src[0].max_ch = 1;
	src[0].min_ch = 1;

	src[1].num = 5;
	src[1].type = SDW_DPN_SIMPLE;
	src[1].simple_ch_prep_sm = true;
	src[1].ch_prep_timeout = 10;
	src[1].max_ch = 1;
	src[1].min_ch = 1;

	prop->src_dpn_prop = src;

	sink = devm_kcalloc(dev, 3, sizeof(*sink), GFP_KERNEL);
	if (!sink)
		return -ENOMEM;

	sink[0].num = 6;
	sink[0].type = SDW_DPN_SIMPLE;
	sink[0].simple_ch_prep_sm = true;
	sink[0].ch_prep_timeout = 10;
	sink[0].max_ch = 2;
	sink[0].min_ch = 1;

	sink[1].num = 7;
	sink[1].type = SDW_DPN_FULL;
	sink[1].simple_ch_prep_sm = true;
	sink[1].ch_prep_timeout = 10;
	sink[1].max_ch = 1;
	sink[1].min_ch = 1;

	sink[2].num = 8;
	sink[2].type = SDW_DPN_REDUCED;
	sink[2].simple_ch_prep_sm = true;
	sink[2].ch_prep_timeout = 10;
	sink[2].max_ch = 2;
	sink[2].min_ch = 1;

	prop->sink_dpn_prop = sink;

	ret = device_property_read_u32_array(dev, "qcom,port-mapping",
					     &slave->m_port_map[1],
					     WCD9378_SDCA_MAX_PORTS);
	if (ret)
		return dev_err_probe(dev, ret, "qcom,port-mapping missing\n");

	return 0;
}

static int wcd9378_sdca_populate_function(struct device *dev,
					  struct sdca_function_data *function)
{
	/* @function->desc is already set by the framework; fill payload only. */
	if (function->desc->type != wcd9378_sdca_desc.type)
		return -EINVAL;

	function->num_entities    = wcd9378_sdca_data.num_entities;
	function->entities        = wcd9378_sdca_data.entities;
	function->num_clusters    = wcd9378_sdca_data.num_clusters;
	function->clusters        = wcd9378_sdca_data.clusters;
	function->num_init_table  = wcd9378_sdca_data.num_init_table;
	function->init_table      = wcd9378_sdca_data.init_table;
	function->reset_max_delay = wcd9378_sdca_data.reset_max_delay;

	return 0;
}

static const struct sdca_class_ops wcd9378_sdca_class_ops = {
	.populate_function = wcd9378_sdca_populate_function,
};

int wcd9378_sdca_probe(struct sdw_slave *slave,
		       const struct sdw_device_id *id)
{
	struct device *dev = &slave->dev;
	struct sdca_device_data *data = &slave->sdca_data;
	struct wcd9378_priv *priv;
	struct gpio_desc *reset;
	int ret;

	/*
	 * 0x0217:0x0110 covers both mobile (non-SDCA) and SDCA-compliant
	 * fuse variants. Only SDCA-compliant nodes bind to this driver;
	 * mobile-mode nodes are handled by a different driver.
	 */
	if (!device_property_read_bool(dev, "qcom,sdca-compliant"))
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	/* No SPMI parent: supplies and reset live on the SoundWire DT node. */
	ret = devm_regulator_bulk_get_enable(dev,
					     ARRAY_SIZE(wcd9378_sdca_supplies),
					     wcd9378_sdca_supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable supplies\n");

	reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset),
				     "failed to get reset GPIO\n");

	if (reset) {
		gpiod_set_value(reset, 1);
		usleep_range(20, 30);
		gpiod_set_value(reset, 0);
		usleep_range(20, 30);
	}

	/* SCP writes below need the slave attached. */
	ret = sdw_slave_wait_for_init(slave, 5000);
	if (ret)
		return dev_err_probe(dev, ret,
				     "slave attach timeout: %d\n", ret);

	/*
	 * TX PDM clock: bank-1 shadow + SCP_COMMIT.  SCP survives PDE
	 * cycles; one-shot before any port is enabled.
	 */
	ret = sdw_write_no_pm(slave, WCD9378_SCP_HOST_CLK_DIV2_CTL_B1, 0x01);
	if (ret)
		return dev_err_probe(dev, ret,
				     "HOST_CLK_DIV2_CTL_B1: %d\n", ret);

	ret = sdw_write_no_pm(slave, SDW_SCP_COMMIT, 0x02);
	if (ret)
		return dev_err_probe(dev, ret, "SCP_COMMIT: %d\n", ret);

	/* DT has no DisCo enumeration; seed the descriptor here. */
	if (!data->num_functions) {
		data->function[0].type = wcd9378_sdca_desc.type;
		data->function[0].adr  = wcd9378_sdca_desc.adr;
		data->function[0].name = wcd9378_sdca_desc.name;
		data->num_functions    = 1;
	}

	return sdca_class_probe(slave, &priv->class, &wcd9378_sdca_class_ops);
}

void wcd9378_sdca_remove(struct sdw_slave *slave)
{
	struct wcd9378_priv *priv = dev_get_drvdata(&slave->dev);

	sdca_class_remove(&priv->class);
}

int wcd9378_sdca_runtime_suspend(struct device *dev)
{
	struct wcd9378_priv *priv = dev_get_drvdata(dev);

	return sdca_class_runtime_suspend(&priv->class);
}

int wcd9378_sdca_runtime_resume(struct device *dev)
{
	struct wcd9378_priv *priv = dev_get_drvdata(dev);

	return sdca_class_runtime_resume(&priv->class);
}

int wcd9378_sdca_system_suspend(struct device *dev)
{
	struct wcd9378_priv *priv = dev_get_drvdata(dev);

	return sdca_class_system_suspend(&priv->class);
}

int wcd9378_sdca_system_resume(struct device *dev)
{
	struct wcd9378_priv *priv = dev_get_drvdata(dev);

	return sdca_class_system_resume(&priv->class);
}

/* SoundWire slave-driver plumbing lives in wcd9378-sdw.c. */
