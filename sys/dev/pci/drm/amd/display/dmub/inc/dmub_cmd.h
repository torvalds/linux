/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef DMUB_CMD_H
#define DMUB_CMD_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/delay.h>

#include "atomfirmware.h"

//<DMUB_TYPES>==================================================================
/* Basic type definitions. */

#define __forceinline inline

/**
 * Flag from driver to indicate that ABM should be disabled gradually
 * by slowly reversing all backlight programming and pixel compensation.
 */
#define SET_ABM_PIPE_GRADUALLY_DISABLE           0

/**
 * Flag from driver to indicate that ABM should be disabled immediately
 * and undo all backlight programming and pixel compensation.
 */
#define SET_ABM_PIPE_IMMEDIATELY_DISABLE         255

/**
 * Flag from driver to indicate that ABM should be disabled immediately
 * and keep the current backlight programming and pixel compensation.
 */
#define SET_ABM_PIPE_IMMEDIATE_KEEP_GAIN_DISABLE 254

/**
 * Flag from driver to set the current ABM pipe index or ABM operating level.
 */
#define SET_ABM_PIPE_NORMAL                      1

/**
 * Number of ambient light levels in ABM algorithm.
 */
#define NUM_AMBI_LEVEL                  5

/**
 * Number of operating/aggression levels in ABM algorithm.
 */
#define NUM_AGGR_LEVEL                  4

/**
 * Number of segments in the gamma curve.
 */
#define NUM_POWER_FN_SEGS               8

/**
 * Number of segments in the backlight curve.
 */
#define NUM_BL_CURVE_SEGS               16

/**
 * Maximum number of segments in ABM ACE curve.
 */
#define ABM_MAX_NUM_OF_ACE_SEGMENTS         64

/**
 * Maximum number of bins in ABM histogram.
 */
#define ABM_MAX_NUM_OF_HG_BINS              64

/* Maximum number of SubVP streams */
#define DMUB_MAX_SUBVP_STREAMS 2

/* Define max FPO streams as 4 for now. Current implementation today
 * only supports 1, but could be more in the future. Reduce array
 * size to ensure the command size remains less than 64 bytes if
 * adding new fields.
 */
#define DMUB_MAX_FPO_STREAMS 4

/* Maximum number of streams on any ASIC. */
#define DMUB_MAX_STREAMS 6

/* Maximum number of planes on any ASIC. */
#define DMUB_MAX_PLANES 6

/* Maximum number of phantom planes on any ASIC */
#define DMUB_MAX_PHANTOM_PLANES ((DMUB_MAX_PLANES) / 2)

/* Trace buffer offset for entry */
#define TRACE_BUFFER_ENTRY_OFFSET 16

/**
 * Maximum number of dirty rects supported by FW.
 */
#define DMUB_MAX_DIRTY_RECTS 3

/**
 *
 * PSR control version legacy
 */
#define DMUB_CMD_PSR_CONTROL_VERSION_UNKNOWN 0x0
/**
 * PSR control version with multi edp support
 */
#define DMUB_CMD_PSR_CONTROL_VERSION_1 0x1


/**
 * ABM control version legacy
 */
#define DMUB_CMD_ABM_CONTROL_VERSION_UNKNOWN 0x0

/**
 * ABM control version with multi edp support
 */
#define DMUB_CMD_ABM_CONTROL_VERSION_1 0x1

/**
 * Physical framebuffer address location, 64-bit.
 */
#ifndef PHYSICAL_ADDRESS_LOC
#define PHYSICAL_ADDRESS_LOC union large_integer
#endif

/**
 * OS/FW agnostic memcpy
 */
#ifndef dmub_memcpy
#define dmub_memcpy(dest, source, bytes) memcpy((dest), (source), (bytes))
#endif

/**
 * OS/FW agnostic memset
 */
#ifndef dmub_memset
#define dmub_memset(dest, val, bytes) memset((dest), (val), (bytes))
#endif

#pragma pack(push, 1)
/**
 * OS/FW agnostic udelay
 */
#ifndef dmub_udelay
#define dmub_udelay(microseconds) udelay(microseconds)
#endif

#pragma pack(push, 1)
#define ABM_NUM_OF_ACE_SEGMENTS         5

union abm_flags {
	struct {
		/**
		 * @abm_enabled: Indicates if ABM is enabled.
		 */
		unsigned int abm_enabled : 1;

		/**
		 * @disable_abm_requested: Indicates if driver has requested ABM to be disabled.
		 */
		unsigned int disable_abm_requested : 1;

		/**
		 * @disable_abm_immediately: Indicates if driver has requested ABM to be disabled immediately.
		 */
		unsigned int disable_abm_immediately : 1;

		/**
		 * @disable_abm_immediate_keep_gain: Indicates if driver has requested ABM
		 * to be disabled immediately and keep gain.
		 */
		unsigned int disable_abm_immediate_keep_gain : 1;

		/**
		 * @fractional_pwm: Indicates if fractional duty cycle for backlight PWM is enabled.
		 */
		unsigned int fractional_pwm : 1;

		/**
		 * @abm_gradual_bl_change: Indicates if algorithm has completed gradual adjustment
		 * of user backlight level.
		 */
		unsigned int abm_gradual_bl_change : 1;

		/**
		 * @abm_new_frame: Indicates if a new frame update needed for ABM to ramp up into steady
		 */
		unsigned int abm_new_frame : 1;

		/**
		 * @vb_scaling_enabled: Indicates variBright Scaling Enable
		 */
		unsigned int vb_scaling_enabled : 1;
	} bitfields;

	unsigned int u32All;
};
#pragma pack(pop)

struct abm_save_restore {
	/**
	 * @flags: Misc. ABM flags.
	 */
	union abm_flags flags;

	/**
	 * @pause: true:  pause ABM and get state
	 *         false: unpause ABM after setting state
	 */
	uint32_t pause;

	/**
	 * @next_ace_slope: Next ACE slopes to be programmed in HW (u3.13)
	 */
	uint32_t next_ace_slope[ABM_NUM_OF_ACE_SEGMENTS];

	/**
	 * @next_ace_thresh: Next ACE thresholds to be programmed in HW (u10.6)
	 */
	uint32_t next_ace_thresh[ABM_NUM_OF_ACE_SEGMENTS];

	/**
	 * @next_ace_offset: Next ACE offsets to be programmed in HW (u10.6)
	 */
	uint32_t next_ace_offset[ABM_NUM_OF_ACE_SEGMENTS];


	/**
	 * @knee_threshold: Current x-position of ACE knee (u0.16).
	 */
	uint32_t knee_threshold;
	/**
	 * @current_gain: Current backlight reduction (u16.16).
	 */
	uint32_t current_gain;
	/**
	 * @curr_bl_level: Current actual backlight level converging to target backlight level.
	 */
	uint16_t curr_bl_level;

	/**
	 * @curr_user_bl_level: Current nominal backlight level converging to level requested by user.
	 */
	uint16_t curr_user_bl_level;

};

/**
 * union dmub_addr - DMUB physical/virtual 64-bit address.
 */
union dmub_addr {
	struct {
		uint32_t low_part; /**< Lower 32 bits */
		uint32_t high_part; /**< Upper 32 bits */
	} u; /*<< Low/high bit access */
	uint64_t quad_part; /*<< 64 bit address */
};
#pragma pack(pop)

/**
 * Dirty rect definition.
 */
struct dmub_rect {
	/**
	 * Dirty rect x offset.
	 */
	uint32_t x;

	/**
	 * Dirty rect y offset.
	 */
	uint32_t y;

	/**
	 * Dirty rect width.
	 */
	uint32_t width;

	/**
	 * Dirty rect height.
	 */
	uint32_t height;
};

/**
 * Flags that can be set by driver to change some PSR behaviour.
 */
union dmub_psr_debug_flags {
	/**
	 * Debug flags.
	 */
	struct {
		/**
		 * Enable visual confirm in FW.
		 */
		uint32_t visual_confirm : 1;

		/**
		 * Force all selective updates to bw full frame updates.
		 */
		uint32_t force_full_frame_update : 1;

		/**
		 * Use HW Lock Mgr object to do HW locking in FW.
		 */
		uint32_t use_hw_lock_mgr : 1;

		/**
		 * Use TPS3 signal when restore main link.
		 */
		uint32_t force_wakeup_by_tps3 : 1;

		/**
		 * Back to back flip, therefore cannot power down PHY
		 */
		uint32_t back_to_back_flip : 1;

		/**
		 * Enable visual confirm for IPS
		 */
		uint32_t enable_ips_visual_confirm : 1;
	} bitfields;

	/**
	 * Union for debug flags.
	 */
	uint32_t u32All;
};

/**
 * Flags that can be set by driver to change some Replay behaviour.
 */
union replay_debug_flags {
	struct {
		/**
		 * 0x1 (bit 0)
		 * Enable visual confirm in FW.
		 */
		uint32_t visual_confirm : 1;

		/**
		 * 0x2 (bit 1)
		 * @skip_crc: Set if need to skip CRC.
		 */
		uint32_t skip_crc : 1;

		/**
		 * 0x4 (bit 2)
		 * @force_link_power_on: Force disable ALPM control
		 */
		uint32_t force_link_power_on : 1;

		/**
		 * 0x8 (bit 3)
		 * @force_phy_power_on: Force phy power on
		 */
		uint32_t force_phy_power_on : 1;

		/**
		 * 0x10 (bit 4)
		 * @timing_resync_disabled: Disabled Replay normal sleep mode timing resync
		 */
		uint32_t timing_resync_disabled : 1;

		/**
		 * 0x20 (bit 5)
		 * @skip_crtc_disabled: CRTC disable skipped
		 */
		uint32_t skip_crtc_disabled : 1;

		/**
		 * 0x40 (bit 6)
		 * @force_defer_one_frame_update: Force defer one frame update in ultra sleep mode
		 */
		uint32_t force_defer_one_frame_update : 1;

		/**
		 * 0x80 (bit 7)
		 * @disable_delay_alpm_on: Force disable delay alpm on
		 */
		uint32_t disable_delay_alpm_on : 1;

		/**
		 * 0x100 (bit 8)
		 * @disable_desync_error_check: Force disable desync error check
		 */
		uint32_t disable_desync_error_check : 1;

		/**
		 * 0x200 (bit 9)
		 * @force_self_update_when_abm_non_steady: Force self update if abm is not steady
		 */
		uint32_t force_self_update_when_abm_non_steady : 1;

		/**
		 * 0x400 (bit 10)
		 * @enable_ips_visual_confirm: Enable IPS visual confirm when entering IPS
		 * If we enter IPS2, the Visual confirm bar will change to yellow
		 */
		uint32_t enable_ips_visual_confirm : 1;

		/**
		 * 0x800 (bit 11)
		 * @enable_ips_residency_profiling: Enable IPS residency profiling
		 */
		uint32_t enable_ips_residency_profiling : 1;

		uint32_t reserved : 20;
	} bitfields;

	uint32_t u32All;
};

union replay_hw_flags {
	struct {
		/**
		 * @allow_alpm_fw_standby_mode: To indicate whether the
		 * ALPM FW standby mode is allowed
		 */
		uint32_t allow_alpm_fw_standby_mode : 1;

		/*
		 * @dsc_enable_status: DSC enable status in driver
		 */
		uint32_t dsc_enable_status : 1;

		/**
		 * @fec_enable_status: receive fec enable/disable status from driver
		 */
		uint32_t fec_enable_status : 1;

		/*
		 * @smu_optimizations_en: SMU power optimization.
		 * Only when active display is Replay capable and display enters Replay.
		 * Trigger interrupt to SMU to powerup/down.
		 */
		uint32_t smu_optimizations_en : 1;

		/**
		 * @phy_power_state: Indicates current phy power state
		 */
		uint32_t phy_power_state : 1;

		/**
		 * @link_power_state: Indicates current link power state
		 */
		uint32_t link_power_state : 1;
		/**
		 * Use TPS3 signal when restore main link.
		 */
		uint32_t force_wakeup_by_tps3 : 1;
	} bitfields;

	uint32_t u32All;
};

/**
 * DMUB feature capabilities.
 * After DMUB init, driver will query FW capabilities prior to enabling certain features.
 */
struct dmub_feature_caps {
	/**
	 * Max PSR version supported by FW.
	 */
	uint8_t psr;
	uint8_t fw_assisted_mclk_switch_ver;
	uint8_t reserved[4];
	uint8_t subvp_psr_support;
	uint8_t gecc_enable;
	uint8_t replay_supported;
	uint8_t replay_reserved[3];
};

struct dmub_visual_confirm_color {
	/**
	 * Maximum 10 bits color value
	 */
	uint16_t color_r_cr;
	uint16_t color_g_y;
	uint16_t color_b_cb;
	uint16_t panel_inst;
};

//==============================================================================
//</DMUB_TYPES>=================================================================
//==============================================================================
//< DMUB_META>==================================================================
//==============================================================================
#pragma pack(push, 1)

/* Magic value for identifying dmub_fw_meta_info */
#define DMUB_FW_META_MAGIC 0x444D5542

/* Offset from the end of the file to the dmub_fw_meta_info */
#define DMUB_FW_META_OFFSET 0x24

/**
 * union dmub_fw_meta_feature_bits - Static feature bits for pre-initialization
 */
union dmub_fw_meta_feature_bits {
	struct {
		uint32_t shared_state_link_detection : 1; /**< 1 supports link detection via shared state */
		uint32_t reserved : 31;
	} bits; /**< status bits */
	uint32_t all; /**< 32-bit access to status bits */
};

/**
 * struct dmub_fw_meta_info - metadata associated with fw binary
 *
 * NOTE: This should be considered a stable API. Fields should
 *       not be repurposed or reordered. New fields should be
 *       added instead to extend the structure.
 *
 * @magic_value: magic value identifying DMUB firmware meta info
 * @fw_region_size: size of the firmware state region
 * @trace_buffer_size: size of the tracebuffer region
 * @fw_version: the firmware version information
 * @dal_fw: 1 if the firmware is DAL
 * @shared_state_size: size of the shared state region in bytes
 * @shared_state_features: number of shared state features
 */
struct dmub_fw_meta_info {
	uint32_t magic_value; /**< magic value identifying DMUB firmware meta info */
	uint32_t fw_region_size; /**< size of the firmware state region */
	uint32_t trace_buffer_size; /**< size of the tracebuffer region */
	uint32_t fw_version; /**< the firmware version information */
	uint8_t dal_fw; /**< 1 if the firmware is DAL */
	uint8_t reserved[3]; /**< padding bits */
	uint32_t shared_state_size; /**< size of the shared state region in bytes */
	uint16_t shared_state_features; /**< number of shared state features */
	uint16_t reserved2; /**< padding bytes */
	union dmub_fw_meta_feature_bits feature_bits; /**< static feature bits */
};

/**
 * union dmub_fw_meta - ensures that dmub_fw_meta_info remains 64 bytes
 */
union dmub_fw_meta {
	struct dmub_fw_meta_info info; /**< metadata info */
	uint8_t reserved[64]; /**< padding bits */
};

#pragma pack(pop)

//==============================================================================
//< DMUB Trace Buffer>================================================================
//==============================================================================
#if !defined(TENSILICA) && !defined(DMUB_TRACE_ENTRY_DEFINED)
/**
 * dmub_trace_code_t - firmware trace code, 32-bits
 */
typedef uint32_t dmub_trace_code_t;

/**
 * struct dmcub_trace_buf_entry - Firmware trace entry
 */
struct dmcub_trace_buf_entry {
	dmub_trace_code_t trace_code; /**< trace code for the event */
	uint32_t tick_count; /**< the tick count at time of trace */
	uint32_t param0; /**< trace defined parameter 0 */
	uint32_t param1; /**< trace defined parameter 1 */
};
#endif

//==============================================================================
//< DMUB_STATUS>================================================================
//==============================================================================

/**
 * DMCUB scratch registers can be used to determine firmware status.
 * Current scratch register usage is as follows:
 *
 * SCRATCH0: FW Boot Status register
 * SCRATCH5: LVTMA Status Register
 * SCRATCH15: FW Boot Options register
 */

/**
 * union dmub_fw_boot_status - Status bit definitions for SCRATCH0.
 */
union dmub_fw_boot_status {
	struct {
		uint32_t dal_fw : 1; /**< 1 if DAL FW */
		uint32_t mailbox_rdy : 1; /**< 1 if mailbox ready */
		uint32_t optimized_init_done : 1; /**< 1 if optimized init done */
		uint32_t restore_required : 1; /**< 1 if driver should call restore */
		uint32_t defer_load : 1; /**< 1 if VBIOS data is deferred programmed */
		uint32_t fams_enabled : 1; /**< 1 if VBIOS data is deferred programmed */
		uint32_t detection_required: 1; /**<  if detection need to be triggered by driver */
		uint32_t hw_power_init_done: 1; /**< 1 if hw power init is completed */
		uint32_t ono_regions_enabled: 1; /**< 1 if ONO regions are enabled */
	} bits; /**< status bits */
	uint32_t all; /**< 32-bit access to status bits */
};

/**
 * enum dmub_fw_boot_status_bit - Enum bit definitions for SCRATCH0.
 */
enum dmub_fw_boot_status_bit {
	DMUB_FW_BOOT_STATUS_BIT_DAL_FIRMWARE = (1 << 0), /**< 1 if DAL FW */
	DMUB_FW_BOOT_STATUS_BIT_MAILBOX_READY = (1 << 1), /**< 1 if mailbox ready */
	DMUB_FW_BOOT_STATUS_BIT_OPTIMIZED_INIT_DONE = (1 << 2), /**< 1 if init done */
	DMUB_FW_BOOT_STATUS_BIT_RESTORE_REQUIRED = (1 << 3), /**< 1 if driver should call restore */
	DMUB_FW_BOOT_STATUS_BIT_DEFERRED_LOADED = (1 << 4), /**< 1 if VBIOS data is deferred programmed */
	DMUB_FW_BOOT_STATUS_BIT_FAMS_ENABLED = (1 << 5), /**< 1 if FAMS is enabled*/
	DMUB_FW_BOOT_STATUS_BIT_DETECTION_REQUIRED = (1 << 6), /**< 1 if detection need to be triggered by driver*/
	DMUB_FW_BOOT_STATUS_BIT_HW_POWER_INIT_DONE = (1 << 7), /**< 1 if hw power init is completed */
	DMUB_FW_BOOT_STATUS_BIT_ONO_REGIONS_ENABLED = (1 << 8), /**< 1 if ONO regions are enabled */
};

/* Register bit definition for SCRATCH5 */
union dmub_lvtma_status {
	struct {
		uint32_t psp_ok : 1;
		uint32_t edp_on : 1;
		uint32_t reserved : 30;
	} bits;
	uint32_t all;
};

enum dmub_lvtma_status_bit {
	DMUB_LVTMA_STATUS_BIT_PSP_OK = (1 << 0),
	DMUB_LVTMA_STATUS_BIT_EDP_ON = (1 << 1),
};

enum dmub_ips_disable_type {
	DMUB_IPS_ENABLE = 0,
	DMUB_IPS_DISABLE_ALL = 1,
	DMUB_IPS_DISABLE_IPS1 = 2,
	DMUB_IPS_DISABLE_IPS2 = 3,
	DMUB_IPS_DISABLE_IPS2_Z10 = 4,
	DMUB_IPS_DISABLE_DYNAMIC = 5,
	DMUB_IPS_RCG_IN_ACTIVE_IPS2_IN_OFF = 6,
};

#define DMUB_IPS1_ALLOW_MASK 0x00000001
#define DMUB_IPS2_ALLOW_MASK 0x00000002
#define DMUB_IPS1_COMMIT_MASK 0x00000004
#define DMUB_IPS2_COMMIT_MASK 0x00000008

/**
 * union dmub_fw_boot_options - Boot option definitions for SCRATCH14
 */
union dmub_fw_boot_options {
	struct {
		uint32_t pemu_env : 1; /**< 1 if PEMU */
		uint32_t fpga_env : 1; /**< 1 if FPGA */
		uint32_t optimized_init : 1; /**< 1 if optimized init */
		uint32_t skip_phy_access : 1; /**< 1 if PHY access should be skipped */
		uint32_t disable_clk_gate: 1; /**< 1 if clock gating should be disabled */
		uint32_t skip_phy_init_panel_sequence: 1; /**< 1 to skip panel init seq */
		uint32_t z10_disable: 1; /**< 1 to disable z10 */
		uint32_t enable_dpia: 1; /**< 1 if DPIA should be enabled */
		uint32_t invalid_vbios_data: 1; /**< 1 if VBIOS data table is invalid */
		uint32_t dpia_supported: 1; /**< 1 if DPIA is supported on this platform */
		uint32_t sel_mux_phy_c_d_phy_f_g: 1; /**< 1 if PHYF/PHYG should be enabled on DCN31 */
		/**< 1 if all root clock gating is enabled and low power memory is enabled*/
		uint32_t power_optimization: 1;
		uint32_t diag_env: 1; /* 1 if diagnostic environment */
		uint32_t gpint_scratch8: 1; /* 1 if GPINT is in scratch8*/
		uint32_t usb4_cm_version: 1; /**< 1 CM support */
		uint32_t dpia_hpd_int_enable_supported: 1; /* 1 if dpia hpd int enable supported */
		uint32_t enable_non_transparent_setconfig: 1; /* 1 if dpia use conventional dp lt flow*/
		uint32_t disable_clk_ds: 1; /* 1 if disallow dispclk_ds and dppclk_ds*/
		uint32_t disable_timeout_recovery : 1; /* 1 if timeout recovery should be disabled */
		uint32_t ips_pg_disable: 1; /* 1 to disable ONO domains power gating*/
		uint32_t ips_disable: 3; /* options to disable ips support*/
		uint32_t ips_sequential_ono: 1; /**< 1 to enable sequential ONO IPS sequence */
		uint32_t disable_sldo_opt: 1; /**< 1 to disable SLDO optimizations */
		uint32_t reserved : 7; /**< reserved */
	} bits; /**< boot bits */
	uint32_t all; /**< 32-bit access to bits */
};

enum dmub_fw_boot_options_bit {
	DMUB_FW_BOOT_OPTION_BIT_PEMU_ENV = (1 << 0), /**< 1 if PEMU */
	DMUB_FW_BOOT_OPTION_BIT_FPGA_ENV = (1 << 1), /**< 1 if FPGA */
	DMUB_FW_BOOT_OPTION_BIT_OPTIMIZED_INIT_DONE = (1 << 2), /**< 1 if optimized init done */
};

//==============================================================================
//< DMUB_SHARED_STATE>==========================================================
//==============================================================================

/**
 * Shared firmware state between driver and firmware for lockless communication
 * in situations where the inbox/outbox may be unavailable.
 *
 * Each structure *must* be at most 256-bytes in size. The layout allocation is
 * described below:
 *
 * [Header (256 Bytes)][Feature 1 (256 Bytes)][Feature 2 (256 Bytes)]...
 */

/**
 * enum dmub_shared_state_feature_id - List of shared state features.
 */
enum dmub_shared_state_feature_id {
	DMUB_SHARED_SHARE_FEATURE__INVALID = 0,
	DMUB_SHARED_SHARE_FEATURE__IPS_FW = 1,
	DMUB_SHARED_SHARE_FEATURE__IPS_DRIVER = 2,
	DMUB_SHARED_STATE_FEATURE__LAST, /* Total number of features. */
};

/**
 * struct dmub_shared_state_ips_fw - Firmware signals for IPS.
 */
union dmub_shared_state_ips_fw_signals {
	struct {
		uint32_t ips1_commit : 1;  /**< 1 if in IPS1 */
		uint32_t ips2_commit : 1; /**< 1 if in IPS2 */
		uint32_t in_idle : 1; /**< 1 if DMCUB is in idle */
		uint32_t detection_required : 1; /**< 1 if detection is required */
		uint32_t reserved_bits : 28; /**< Reversed */
	} bits;
	uint32_t all;
};

/**
 * struct dmub_shared_state_ips_signals - Firmware signals for IPS.
 */
union dmub_shared_state_ips_driver_signals {
	struct {
		uint32_t allow_pg : 1; /**< 1 if PG is allowed */
		uint32_t allow_ips1 : 1; /**< 1 is IPS1 is allowed */
		uint32_t allow_ips2 : 1; /**< 1 is IPS1 is allowed */
		uint32_t allow_z10 : 1; /**< 1 if Z10 is allowed */
		uint32_t allow_idle : 1; /**< 1 if driver is allowing idle */
		uint32_t reserved_bits : 27; /**< Reversed bits */
	} bits;
	uint32_t all;
};

/**
 * IPS FW Version
 */
#define DMUB_SHARED_STATE__IPS_FW_VERSION 1

/**
 * struct dmub_shared_state_ips_fw - Firmware state for IPS.
 */
struct dmub_shared_state_ips_fw {
	union dmub_shared_state_ips_fw_signals signals; /**< 4 bytes, IPS signal bits */
	uint32_t rcg_entry_count; /**< Entry counter for RCG */
	uint32_t rcg_exit_count; /**< Exit counter for RCG */
	uint32_t ips1_entry_count; /**< Entry counter for IPS1 */
	uint32_t ips1_exit_count; /**< Exit counter for IPS1 */
	uint32_t ips2_entry_count; /**< Entry counter for IPS2 */
	uint32_t ips2_exit_count; /**< Exit counter for IPS2 */
	uint32_t reserved[55]; /**< Reversed, to be updated when adding new fields. */
}; /* 248-bytes, fixed */

/**
 * IPS Driver Version
 */
#define DMUB_SHARED_STATE__IPS_DRIVER_VERSION 1

/**
 * struct dmub_shared_state_ips_driver - Driver state for IPS.
 */
struct dmub_shared_state_ips_driver {
	union dmub_shared_state_ips_driver_signals signals; /**< 4 bytes, IPS signal bits */
	uint32_t reserved[61]; /**< Reversed, to be updated when adding new fields. */
}; /* 248-bytes, fixed */

/**
 * enum dmub_shared_state_feature_common - Generic payload.
 */
struct dmub_shared_state_feature_common {
	uint32_t padding[62];
}; /* 248-bytes, fixed */

/**
 * enum dmub_shared_state_feature_header - Feature description.
 */
struct dmub_shared_state_feature_header {
	uint16_t id; /**< Feature ID */
	uint16_t version; /**< Feature version */
	uint32_t reserved; /**< Reserved bytes. */
}; /* 8 bytes, fixed */

/**
 * struct dmub_shared_state_feature_block - Feature block.
 */
struct dmub_shared_state_feature_block {
	struct dmub_shared_state_feature_header header; /**< Shared state header. */
	union dmub_shared_feature_state_union {
		struct dmub_shared_state_feature_common common; /**< Generic data */
		struct dmub_shared_state_ips_fw ips_fw; /**< IPS firmware state */
		struct dmub_shared_state_ips_driver ips_driver; /**< IPS driver state */
	} data; /**< Shared state data. */
}; /* 256-bytes, fixed */

/**
 * Shared state size in bytes.
 */
#define DMUB_FW_HEADER_SHARED_STATE_SIZE \
	((DMUB_SHARED_STATE_FEATURE__LAST + 1) * sizeof(struct dmub_shared_state_feature_block))

//==============================================================================
//</DMUB_STATUS>================================================================
//==============================================================================
//< DMUB_VBIOS>=================================================================
//==============================================================================

/*
 * enum dmub_cmd_vbios_type - VBIOS commands.
 *
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */
enum dmub_cmd_vbios_type {
	/**
	 * Configures the DIG encoder.
	 */
	DMUB_CMD__VBIOS_DIGX_ENCODER_CONTROL = 0,
	/**
	 * Controls the PHY.
	 */
	DMUB_CMD__VBIOS_DIG1_TRANSMITTER_CONTROL = 1,
	/**
	 * Sets the pixel clock/symbol clock.
	 */
	DMUB_CMD__VBIOS_SET_PIXEL_CLOCK = 2,
	/**
	 * Enables or disables power gating.
	 */
	DMUB_CMD__VBIOS_ENABLE_DISP_POWER_GATING = 3,
	/**
	 * Controls embedded panels.
	 */
	DMUB_CMD__VBIOS_LVTMA_CONTROL = 15,
	/**
	 * Query DP alt status on a transmitter.
	 */
	DMUB_CMD__VBIOS_TRANSMITTER_QUERY_DP_ALT  = 26,
	/**
	 * Control PHY FSM
	 */
	DMUB_CMD__VBIOS_TRANSMITTER_SET_PHY_FSM  = 29,
	/**
	 * Controls domain power gating
	 */
	DMUB_CMD__VBIOS_DOMAIN_CONTROL = 28,
};

//==============================================================================
//</DMUB_VBIOS>=================================================================
//==============================================================================
//< DMUB_GPINT>=================================================================
//==============================================================================

/**
 * The shifts and masks below may alternatively be used to format and read
 * the command register bits.
 */

#define DMUB_GPINT_DATA_PARAM_MASK 0xFFFF
#define DMUB_GPINT_DATA_PARAM_SHIFT 0

#define DMUB_GPINT_DATA_COMMAND_CODE_MASK 0xFFF
#define DMUB_GPINT_DATA_COMMAND_CODE_SHIFT 16

#define DMUB_GPINT_DATA_STATUS_MASK 0xF
#define DMUB_GPINT_DATA_STATUS_SHIFT 28

/**
 * Command responses.
 */

/**
 * Return response for DMUB_GPINT__STOP_FW command.
 */
#define DMUB_GPINT__STOP_FW_RESPONSE 0xDEADDEAD

/**
 * union dmub_gpint_data_register - Format for sending a command via the GPINT.
 */
union dmub_gpint_data_register {
	struct {
		uint32_t param : 16; /**< 16-bit parameter */
		uint32_t command_code : 12; /**< GPINT command */
		uint32_t status : 4; /**< Command status bit */
	} bits; /**< GPINT bit access */
	uint32_t all; /**< GPINT  32-bit access */
};

/*
 * enum dmub_gpint_command - GPINT command to DMCUB FW
 *
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */
enum dmub_gpint_command {
	/**
	 * Invalid command, ignored.
	 */
	DMUB_GPINT__INVALID_COMMAND = 0,
	/**
	 * DESC: Queries the firmware version.
	 * RETURN: Firmware version.
	 */
	DMUB_GPINT__GET_FW_VERSION = 1,
	/**
	 * DESC: Halts the firmware.
	 * RETURN: DMUB_GPINT__STOP_FW_RESPONSE (0xDEADDEAD) when halted
	 */
	DMUB_GPINT__STOP_FW = 2,
	/**
	 * DESC: Get PSR state from FW.
	 * RETURN: PSR state enum. This enum may need to be converted to the legacy PSR state value.
	 */
	DMUB_GPINT__GET_PSR_STATE = 7,
	/**
	 * DESC: Notifies DMCUB of the currently active streams.
	 * ARGS: Stream mask, 1 bit per active stream index.
	 */
	DMUB_GPINT__IDLE_OPT_NOTIFY_STREAM_MASK = 8,
	/**
	 * DESC: Start PSR residency counter. Stop PSR resdiency counter and get value.
	 * ARGS: We can measure residency from various points. The argument will specify the residency mode.
	 *       By default, it is measured from after we powerdown the PHY, to just before we powerup the PHY.
	 * RETURN: PSR residency in milli-percent.
	 */
	DMUB_GPINT__PSR_RESIDENCY = 9,

	/**
	 * DESC: Notifies DMCUB detection is done so detection required can be cleared.
	 */
	DMUB_GPINT__NOTIFY_DETECTION_DONE = 12,

	/**
	 * DESC: Get REPLAY state from FW.
	 * RETURN: REPLAY state enum. This enum may need to be converted to the legacy REPLAY state value.
	 */
	DMUB_GPINT__GET_REPLAY_STATE = 13,

	/**
	 * DESC: Start REPLAY residency counter. Stop REPLAY resdiency counter and get value.
	 * ARGS: We can measure residency from various points. The argument will specify the residency mode.
	 *       By default, it is measured from after we powerdown the PHY, to just before we powerup the PHY.
	 * RETURN: REPLAY residency in milli-percent.
	 */
	DMUB_GPINT__REPLAY_RESIDENCY = 14,

	/**
	 * DESC: Copy bounding box to the host.
	 * ARGS: Version of bounding box to copy
	 * RETURN: Result of copying bounding box
	 */
	DMUB_GPINT__BB_COPY = 96,

	/**
	 * DESC: Updates the host addresses bit48~bit63 for bounding box.
	 * ARGS: The word3 for the 64 bit address
	 */
	DMUB_GPINT__SET_BB_ADDR_WORD3 = 97,

	/**
	 * DESC: Updates the host addresses bit32~bit47 for bounding box.
	 * ARGS: The word2 for the 64 bit address
	 */
	DMUB_GPINT__SET_BB_ADDR_WORD2 = 98,

	/**
	 * DESC: Updates the host addresses bit16~bit31 for bounding box.
	 * ARGS: The word1 for the 64 bit address
	 */
	DMUB_GPINT__SET_BB_ADDR_WORD1 = 99,

	/**
	 * DESC: Updates the host addresses bit0~bit15 for bounding box.
	 * ARGS: The word0 for the 64 bit address
	 */
	DMUB_GPINT__SET_BB_ADDR_WORD0 = 100,

	/**
	 * DESC: Updates the trace buffer lower 32-bit mask.
	 * ARGS: The new mask
	 * RETURN: Lower 32-bit mask.
	 */
	DMUB_GPINT__UPDATE_TRACE_BUFFER_MASK = 101,

	/**
	 * DESC: Updates the trace buffer mask bit0~bit15.
	 * ARGS: The new mask
	 * RETURN: Lower 32-bit mask.
	 */
	DMUB_GPINT__SET_TRACE_BUFFER_MASK_WORD0 = 102,

	/**
	 * DESC: Updates the trace buffer mask bit16~bit31.
	 * ARGS: The new mask
	 * RETURN: Lower 32-bit mask.
	 */
	DMUB_GPINT__SET_TRACE_BUFFER_MASK_WORD1 = 103,

	/**
	 * DESC: Updates the trace buffer mask bit32~bit47.
	 * ARGS: The new mask
	 * RETURN: Lower 32-bit mask.
	 */
	DMUB_GPINT__SET_TRACE_BUFFER_MASK_WORD2 = 114,

	/**
	 * DESC: Updates the trace buffer mask bit48~bit63.
	 * ARGS: The new mask
	 * RETURN: Lower 32-bit mask.
	 */
	DMUB_GPINT__SET_TRACE_BUFFER_MASK_WORD3 = 115,

	/**
	 * DESC: Read the trace buffer mask bi0~bit15.
	 */
	DMUB_GPINT__GET_TRACE_BUFFER_MASK_WORD0 = 116,

	/**
	 * DESC: Read the trace buffer mask bit16~bit31.
	 */
	DMUB_GPINT__GET_TRACE_BUFFER_MASK_WORD1 = 117,

	/**
	 * DESC: Read the trace buffer mask bi32~bit47.
	 */
	DMUB_GPINT__GET_TRACE_BUFFER_MASK_WORD2 = 118,

	/**
	 * DESC: Updates the trace buffer mask bit32~bit63.
	 */
	DMUB_GPINT__GET_TRACE_BUFFER_MASK_WORD3 = 119,

	/**
	 * DESC: Enable measurements for various task duration
	 * ARGS: 0 - Disable measurement
	 *       1 - Enable measurement
	 */
	DMUB_GPINT__TRACE_DMUB_WAKE_ACTIVITY = 123,
};

/**
 * INBOX0 generic command definition
 */
union dmub_inbox0_cmd_common {
	struct {
		uint32_t command_code: 8; /**< INBOX0 command code */
		uint32_t param: 24; /**< 24-bit parameter */
	} bits;
	uint32_t all;
};

/**
 * INBOX0 hw_lock command definition
 */
union dmub_inbox0_cmd_lock_hw {
	struct {
		uint32_t command_code: 8;

		/* NOTE: Must be have enough bits to match: enum hw_lock_client */
		uint32_t hw_lock_client: 2;

		/* NOTE: Below fields must match with: struct dmub_hw_lock_inst_flags */
		uint32_t otg_inst: 3;
		uint32_t opp_inst: 3;
		uint32_t dig_inst: 3;

		/* NOTE: Below fields must match with: union dmub_hw_lock_flags */
		uint32_t lock_pipe: 1;
		uint32_t lock_cursor: 1;
		uint32_t lock_dig: 1;
		uint32_t triple_buffer_lock: 1;

		uint32_t lock: 1;				/**< Lock */
		uint32_t should_release: 1;		/**< Release */
		uint32_t reserved: 7; 			/**< Reserved for extending more clients, HW, etc. */
	} bits;
	uint32_t all;
};

union dmub_inbox0_data_register {
	union dmub_inbox0_cmd_common inbox0_cmd_common;
	union dmub_inbox0_cmd_lock_hw inbox0_cmd_lock_hw;
};

enum dmub_inbox0_command {
	/**
	 * DESC: Invalid command, ignored.
	 */
	DMUB_INBOX0_CMD__INVALID_COMMAND = 0,
	/**
	 * DESC: Notification to acquire/release HW lock
	 * ARGS:
	 */
	DMUB_INBOX0_CMD__HW_LOCK = 1,
};
//==============================================================================
//</DMUB_GPINT>=================================================================
//==============================================================================
//< DMUB_CMD>===================================================================
//==============================================================================

/**
 * Size in bytes of each DMUB command.
 */
#define DMUB_RB_CMD_SIZE 64

/**
 * Maximum number of items in the DMUB ringbuffer.
 */
#define DMUB_RB_MAX_ENTRY 128

/**
 * Ringbuffer size in bytes.
 */
#define DMUB_RB_SIZE (DMUB_RB_CMD_SIZE * DMUB_RB_MAX_ENTRY)

/**
 * REG_SET mask for reg offload.
 */
#define REG_SET_MASK 0xFFFF

/*
 * enum dmub_cmd_type - DMUB inbox command.
 *
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */
enum dmub_cmd_type {
	/**
	 * Invalid command.
	 */
	DMUB_CMD__NULL = 0,
	/**
	 * Read modify write register sequence offload.
	 */
	DMUB_CMD__REG_SEQ_READ_MODIFY_WRITE = 1,
	/**
	 * Field update register sequence offload.
	 */
	DMUB_CMD__REG_SEQ_FIELD_UPDATE_SEQ = 2,
	/**
	 * Burst write sequence offload.
	 */
	DMUB_CMD__REG_SEQ_BURST_WRITE = 3,
	/**
	 * Reg wait sequence offload.
	 */
	DMUB_CMD__REG_REG_WAIT = 4,
	/**
	 * Workaround to avoid HUBP underflow during NV12 playback.
	 */
	DMUB_CMD__PLAT_54186_WA = 5,
	/**
	 * Command type used to query FW feature caps.
	 */
	DMUB_CMD__QUERY_FEATURE_CAPS = 6,
	/**
	 * Command type used to get visual confirm color.
	 */
	DMUB_CMD__GET_VISUAL_CONFIRM_COLOR = 8,
	/**
	 * Command type used for all PSR commands.
	 */
	DMUB_CMD__PSR = 64,
	/**
	 * Command type used for all MALL commands.
	 */
	DMUB_CMD__MALL = 65,
	/**
	 * Command type used for all ABM commands.
	 */
	DMUB_CMD__ABM = 66,
	/**
	 * Command type used to update dirty rects in FW.
	 */
	DMUB_CMD__UPDATE_DIRTY_RECT = 67,
	/**
	 * Command type used to update cursor info in FW.
	 */
	DMUB_CMD__UPDATE_CURSOR_INFO = 68,
	/**
	 * Command type used for HW locking in FW.
	 */
	DMUB_CMD__HW_LOCK = 69,
	/**
	 * Command type used to access DP AUX.
	 */
	DMUB_CMD__DP_AUX_ACCESS = 70,
	/**
	 * Command type used for OUTBOX1 notification enable
	 */
	DMUB_CMD__OUTBOX1_ENABLE = 71,

	/**
	 * Command type used for all idle optimization commands.
	 */
	DMUB_CMD__IDLE_OPT = 72,
	/**
	 * Command type used for all clock manager commands.
	 */
	DMUB_CMD__CLK_MGR = 73,
	/**
	 * Command type used for all panel control commands.
	 */
	DMUB_CMD__PANEL_CNTL = 74,

	/**
	 * Command type used for all CAB commands.
	 */
	DMUB_CMD__CAB_FOR_SS = 75,

	DMUB_CMD__FW_ASSISTED_MCLK_SWITCH = 76,

	/**
	 * Command type used for interfacing with DPIA.
	 */
	DMUB_CMD__DPIA = 77,
	/**
	 * Command type used for EDID CEA parsing
	 */
	DMUB_CMD__EDID_CEA = 79,
	/**
	 * Command type used for getting usbc cable ID
	 */
	DMUB_CMD_GET_USBC_CABLE_ID = 81,
	/**
	 * Command type used to query HPD state.
	 */
	DMUB_CMD__QUERY_HPD_STATE = 82,
	/**
	 * Command type used for all VBIOS interface commands.
	 */
	/**
	 * Command type used for all REPLAY commands.
	 */
	DMUB_CMD__REPLAY = 83,

	/**
	 * Command type used for all SECURE_DISPLAY commands.
	 */
	DMUB_CMD__SECURE_DISPLAY = 85,

	/**
	 * Command type used to set DPIA HPD interrupt state
	 */
	DMUB_CMD__DPIA_HPD_INT_ENABLE = 86,

	/**
	 * Command type used for all PSP commands.
	 */
	DMUB_CMD__PSP = 88,

	DMUB_CMD__VBIOS = 128,
};

/**
 * enum dmub_out_cmd_type - DMUB outbox commands.
 */
enum dmub_out_cmd_type {
	/**
	 * Invalid outbox command, ignored.
	 */
	DMUB_OUT_CMD__NULL = 0,
	/**
	 * Command type used for DP AUX Reply data notification
	 */
	DMUB_OUT_CMD__DP_AUX_REPLY = 1,
	/**
	 * Command type used for DP HPD event notification
	 */
	DMUB_OUT_CMD__DP_HPD_NOTIFY = 2,
	/**
	 * Command type used for SET_CONFIG Reply notification
	 */
	DMUB_OUT_CMD__SET_CONFIG_REPLY = 3,
	/**
	 * Command type used for USB4 DPIA notification
	 */
	DMUB_OUT_CMD__DPIA_NOTIFICATION = 5,
	/**
	 * Command type used for HPD redetect notification
	 */
	DMUB_OUT_CMD__HPD_SENSE_NOTIFY = 6,
};

/* DMUB_CMD__DPIA command sub-types. */
enum dmub_cmd_dpia_type {
	DMUB_CMD__DPIA_DIG1_DPIA_CONTROL = 0,
	DMUB_CMD__DPIA_SET_CONFIG_ACCESS = 1,
	DMUB_CMD__DPIA_MST_ALLOC_SLOTS = 2,
	DMUB_CMD__DPIA_SET_TPS_NOTIFICATION = 3,
};

/* DMUB_OUT_CMD__DPIA_NOTIFICATION command types. */
enum dmub_cmd_dpia_notification_type {
	DPIA_NOTIFY__BW_ALLOCATION = 0,
};

#pragma pack(push, 1)

/**
 * struct dmub_cmd_header - Common command header fields.
 */
struct dmub_cmd_header {
	unsigned int type : 8; /**< command type */
	unsigned int sub_type : 8; /**< command sub type */
	unsigned int ret_status : 1; /**< 1 if returned data, 0 otherwise */
	unsigned int multi_cmd_pending : 1; /**< 1 if multiple commands chained together */
	unsigned int reserved0 : 6; /**< reserved bits */
	unsigned int payload_bytes : 6;  /* payload excluding header - up to 60 bytes */
	unsigned int reserved1 : 2; /**< reserved bits */
};

/*
 * struct dmub_cmd_read_modify_write_sequence - Read modify write
 *
 * 60 payload bytes can hold up to 5 sets of read modify writes,
 * each take 3 dwords.
 *
 * number of sequences = header.payload_bytes / sizeof(struct dmub_cmd_read_modify_write_sequence)
 *
 * modify_mask = 0xffff'ffff means all fields are going to be updated.  in this case
 * command parser will skip the read and we can use modify_mask = 0xffff'ffff as reg write
 */
struct dmub_cmd_read_modify_write_sequence {
	uint32_t addr; /**< register address */
	uint32_t modify_mask; /**< modify mask */
	uint32_t modify_value; /**< modify value */
};

/**
 * Maximum number of ops in read modify write sequence.
 */
#define DMUB_READ_MODIFY_WRITE_SEQ__MAX 5

/**
 * struct dmub_cmd_read_modify_write_sequence - Read modify write command.
 */
struct dmub_rb_cmd_read_modify_write {
	struct dmub_cmd_header header;  /**< command header */
	/**
	 * Read modify write sequence.
	 */
	struct dmub_cmd_read_modify_write_sequence seq[DMUB_READ_MODIFY_WRITE_SEQ__MAX];
};

/*
 * Update a register with specified masks and values sequeunce
 *
 * 60 payload bytes can hold address + up to 7 sets of mask/value combo, each take 2 dword
 *
 * number of field update sequence = (header.payload_bytes - sizeof(addr)) / sizeof(struct read_modify_write_sequence)
 *
 *
 * USE CASE:
 *   1. auto-increment register where additional read would update pointer and produce wrong result
 *   2. toggle a bit without read in the middle
 */

struct dmub_cmd_reg_field_update_sequence {
	uint32_t modify_mask; /**< 0xffff'ffff to skip initial read */
	uint32_t modify_value; /**< value to update with */
};

/**
 * Maximum number of ops in field update sequence.
 */
#define DMUB_REG_FIELD_UPDATE_SEQ__MAX 7

/**
 * struct dmub_rb_cmd_reg_field_update_sequence - Field update command.
 */
struct dmub_rb_cmd_reg_field_update_sequence {
	struct dmub_cmd_header header; /**< command header */
	uint32_t addr; /**< register address */
	/**
	 * Field update sequence.
	 */
	struct dmub_cmd_reg_field_update_sequence seq[DMUB_REG_FIELD_UPDATE_SEQ__MAX];
};


/**
 * Maximum number of burst write values.
 */
#define DMUB_BURST_WRITE_VALUES__MAX  14

/*
 * struct dmub_rb_cmd_burst_write - Burst write
 *
 * support use case such as writing out LUTs.
 *
 * 60 payload bytes can hold up to 14 values to write to given address
 *
 * number of payload = header.payload_bytes / sizeof(struct read_modify_write_sequence)
 */
struct dmub_rb_cmd_burst_write {
	struct dmub_cmd_header header; /**< command header */
	uint32_t addr; /**< register start address */
	/**
	 * Burst write register values.
	 */
	uint32_t write_values[DMUB_BURST_WRITE_VALUES__MAX];
};

/**
 * struct dmub_rb_cmd_common - Common command header
 */
struct dmub_rb_cmd_common {
	struct dmub_cmd_header header; /**< command header */
	/**
	 * Padding to RB_CMD_SIZE
	 */
	uint8_t cmd_buffer[DMUB_RB_CMD_SIZE - sizeof(struct dmub_cmd_header)];
};

/**
 * struct dmub_cmd_reg_wait_data - Register wait data
 */
struct dmub_cmd_reg_wait_data {
	uint32_t addr; /**< Register address */
	uint32_t mask; /**< Mask for register bits */
	uint32_t condition_field_value; /**< Value to wait for */
	uint32_t time_out_us; /**< Time out for reg wait in microseconds */
};

/**
 * struct dmub_rb_cmd_reg_wait - Register wait command
 */
struct dmub_rb_cmd_reg_wait {
	struct dmub_cmd_header header; /**< Command header */
	struct dmub_cmd_reg_wait_data reg_wait; /**< Register wait data */
};

/**
 * struct dmub_cmd_PLAT_54186_wa - Underflow workaround
 *
 * Reprograms surface parameters to avoid underflow.
 */
struct dmub_cmd_PLAT_54186_wa {
	uint32_t DCSURF_SURFACE_CONTROL; /**< reg value */
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH; /**< reg value */
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS; /**< reg value */
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C; /**< reg value */
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS_C; /**< reg value */
	struct {
		uint32_t hubp_inst : 4; /**< HUBP instance */
		uint32_t tmz_surface : 1; /**< TMZ enable or disable */
		uint32_t immediate :1; /**< Immediate flip */
		uint32_t vmid : 4; /**< VMID */
		uint32_t grph_stereo : 1; /**< 1 if stereo */
		uint32_t reserved : 21; /**< Reserved */
	} flip_params; /**< Pageflip parameters */
	uint32_t reserved[9]; /**< Reserved bits */
};

/**
 * struct dmub_rb_cmd_PLAT_54186_wa - Underflow workaround command
 */
struct dmub_rb_cmd_PLAT_54186_wa {
	struct dmub_cmd_header header; /**< Command header */
	struct dmub_cmd_PLAT_54186_wa flip; /**< Flip data */
};

/**
 * enum dmub_cmd_mall_type - MALL commands
 */
enum dmub_cmd_mall_type {
	/**
	 * Allows display refresh from MALL.
	 */
	DMUB_CMD__MALL_ACTION_ALLOW = 0,
	/**
	 * Disallows display refresh from MALL.
	 */
	DMUB_CMD__MALL_ACTION_DISALLOW = 1,
	/**
	 * Cursor copy for MALL.
	 */
	DMUB_CMD__MALL_ACTION_COPY_CURSOR = 2,
	/**
	 * Controls DF requests.
	 */
	DMUB_CMD__MALL_ACTION_NO_DF_REQ = 3,
};

/**
 * struct dmub_rb_cmd_mall - MALL command data.
 */
struct dmub_rb_cmd_mall {
	struct dmub_cmd_header header; /**< Common command header */
	union dmub_addr cursor_copy_src; /**< Cursor copy address */
	union dmub_addr cursor_copy_dst; /**< Cursor copy destination */
	uint32_t tmr_delay; /**< Timer delay */
	uint32_t tmr_scale; /**< Timer scale */
	uint16_t cursor_width; /**< Cursor width in pixels */
	uint16_t cursor_pitch; /**< Cursor pitch in pixels */
	uint16_t cursor_height; /**< Cursor height in pixels */
	uint8_t cursor_bpp; /**< Cursor bits per pixel */
	uint8_t debug_bits; /**< Debug bits */

	uint8_t reserved1; /**< Reserved bits */
	uint8_t reserved2; /**< Reserved bits */
};

/**
 * enum dmub_cmd_cab_type - CAB command data.
 */
enum dmub_cmd_cab_type {
	/**
	 * No idle optimizations (i.e. no CAB)
	 */
	DMUB_CMD__CAB_NO_IDLE_OPTIMIZATION = 0,
	/**
	 * No DCN requests for memory
	 */
	DMUB_CMD__CAB_NO_DCN_REQ = 1,
	/**
	 * Fit surfaces in CAB (i.e. CAB enable)
	 */
	DMUB_CMD__CAB_DCN_SS_FIT_IN_CAB = 2,
	/**
	 * Do not fit surfaces in CAB (i.e. no CAB)
	 */
	DMUB_CMD__CAB_DCN_SS_NOT_FIT_IN_CAB = 3,
};

/**
 * struct dmub_rb_cmd_cab - CAB command data.
 */
struct dmub_rb_cmd_cab_for_ss {
	struct dmub_cmd_header header;
	uint8_t cab_alloc_ways; /* total number of ways */
	uint8_t debug_bits;     /* debug bits */
};

/**
 * Enum for indicating which MCLK switch mode per pipe
 */
enum mclk_switch_mode {
	NONE = 0,
	FPO = 1,
	SUBVP = 2,
	VBLANK = 3,
};

/* Per pipe struct which stores the MCLK switch mode
 * data to be sent to DMUB.
 * Named "v2" for now -- once FPO and SUBVP are fully merged
 * the type name can be updated
 */
struct dmub_cmd_fw_assisted_mclk_switch_pipe_data_v2 {
	union {
		struct {
			uint32_t pix_clk_100hz;
			uint16_t main_vblank_start;
			uint16_t main_vblank_end;
			uint16_t mall_region_lines;
			uint16_t prefetch_lines;
			uint16_t prefetch_to_mall_start_lines;
			uint16_t processing_delay_lines;
			uint16_t htotal; // required to calculate line time for multi-display cases
			uint16_t vtotal;
			uint8_t main_pipe_index;
			uint8_t phantom_pipe_index;
			/* Since the microschedule is calculated in terms of OTG lines,
			 * include any scaling factors to make sure when we get accurate
			 * conversion when programming MALL_START_LINE (which is in terms
			 * of HUBP lines). If 4K is being downscaled to 1080p, scale factor
			 * is 1/2 (numerator = 1, denominator = 2).
			 */
			uint8_t scale_factor_numerator;
			uint8_t scale_factor_denominator;
			uint8_t is_drr;
			uint8_t main_split_pipe_index;
			uint8_t phantom_split_pipe_index;
		} subvp_data;

		struct {
			uint32_t pix_clk_100hz;
			uint16_t vblank_start;
			uint16_t vblank_end;
			uint16_t vstartup_start;
			uint16_t vtotal;
			uint16_t htotal;
			uint8_t vblank_pipe_index;
			uint8_t padding[1];
			struct {
				uint8_t drr_in_use;
				uint8_t drr_window_size_ms;	// Indicates largest VMIN/VMAX adjustment per frame
				uint16_t min_vtotal_supported;	// Min VTOTAL that supports switching in VBLANK
				uint16_t max_vtotal_supported;	// Max VTOTAL that can support SubVP static scheduling
				uint8_t use_ramping;		// Use ramping or not
				uint8_t drr_vblank_start_margin;
			} drr_info;				// DRR considered as part of SubVP + VBLANK case
		} vblank_data;
	} pipe_config;

	/* - subvp_data in the union (pipe_config) takes up 27 bytes.
	 * - Make the "mode" field a uint8_t instead of enum so we only use 1 byte (only
	 *   for the DMCUB command, cast to enum once we populate the DMCUB subvp state).
	 */
	uint8_t mode; // enum mclk_switch_mode
};

/**
 * Config data for Sub-VP and FPO
 * Named "v2" for now -- once FPO and SUBVP are fully merged
 * the type name can be updated
 */
struct dmub_cmd_fw_assisted_mclk_switch_config_v2 {
	uint16_t watermark_a_cache;
	uint8_t vertical_int_margin_us;
	uint8_t pstate_allow_width_us;
	struct dmub_cmd_fw_assisted_mclk_switch_pipe_data_v2 pipe_data[DMUB_MAX_SUBVP_STREAMS];
};

/**
 * DMUB rb command definition for Sub-VP and FPO
 * Named "v2" for now -- once FPO and SUBVP are fully merged
 * the type name can be updated
 */
struct dmub_rb_cmd_fw_assisted_mclk_switch_v2 {
	struct dmub_cmd_header header;
	struct dmub_cmd_fw_assisted_mclk_switch_config_v2 config_data;
};

struct dmub_flip_addr_info {
	uint32_t surf_addr_lo;
	uint32_t surf_addr_c_lo;
	uint32_t meta_addr_lo;
	uint32_t meta_addr_c_lo;
	uint16_t surf_addr_hi;
	uint16_t surf_addr_c_hi;
	uint16_t meta_addr_hi;
	uint16_t meta_addr_c_hi;
};

struct dmub_fams2_flip_info {
	union {
		struct {
			uint8_t is_immediate: 1;
		} bits;
		uint8_t all;
	} config;
	uint8_t otg_inst;
	uint8_t pipe_mask;
	uint8_t pad;
	struct dmub_flip_addr_info addr_info;
};

struct dmub_rb_cmd_fams2_flip {
	struct dmub_cmd_header header;
	struct dmub_fams2_flip_info flip_info;
};

struct dmub_optc_state_v2 {
	uint32_t v_total_min;
	uint32_t v_total_max;
	uint32_t v_total_mid;
	uint32_t v_total_mid_frame_num;
	uint8_t program_manual_trigger;
	uint8_t tg_inst;
	uint8_t pad[2];
};

struct dmub_optc_position {
	uint32_t vpos;
	uint32_t hpos;
	uint32_t frame;
};

struct dmub_rb_cmd_fams2_drr_update {
	struct dmub_cmd_header header;
	struct dmub_optc_state_v2 dmub_optc_state_req;
};

/* HW and FW global configuration data for FAMS2 */
/* FAMS2 types and structs */
enum fams2_stream_type {
	FAMS2_STREAM_TYPE_NONE = 0,
	FAMS2_STREAM_TYPE_VBLANK = 1,
	FAMS2_STREAM_TYPE_VACTIVE = 2,
	FAMS2_STREAM_TYPE_DRR = 3,
	FAMS2_STREAM_TYPE_SUBVP = 4,
};

/* dynamic stream state */
struct dmub_fams2_legacy_stream_dynamic_state {
	uint8_t force_allow_at_vblank;
	uint8_t pad[3];
};

struct dmub_fams2_subvp_stream_dynamic_state {
	uint16_t viewport_start_hubp_vline;
	uint16_t viewport_height_hubp_vlines;
	uint16_t viewport_start_c_hubp_vline;
	uint16_t viewport_height_c_hubp_vlines;
	uint16_t phantom_viewport_height_hubp_vlines;
	uint16_t phantom_viewport_height_c_hubp_vlines;
	uint16_t microschedule_start_otg_vline;
	uint16_t mall_start_otg_vline;
	uint16_t mall_start_hubp_vline;
	uint16_t mall_start_c_hubp_vline;
	uint8_t force_allow_at_vblank_only;
	uint8_t pad[3];
};

struct dmub_fams2_drr_stream_dynamic_state {
	uint16_t stretched_vtotal;
	uint8_t use_cur_vtotal;
	uint8_t pad;
};

struct dmub_fams2_stream_dynamic_state {
	uint64_t ref_tick;
	uint32_t cur_vtotal;
	uint16_t adjusted_allow_end_otg_vline;
	uint8_t pad[2];
	struct dmub_optc_position ref_otg_pos;
	struct dmub_optc_position target_otg_pos;
	union {
		struct dmub_fams2_legacy_stream_dynamic_state legacy;
		struct dmub_fams2_subvp_stream_dynamic_state subvp;
		struct dmub_fams2_drr_stream_dynamic_state drr;
	} sub_state;
};

/* static stream state */
struct dmub_fams2_legacy_stream_static_state {
	uint8_t vactive_det_fill_delay_otg_vlines;
	uint8_t programming_delay_otg_vlines;
};

struct dmub_fams2_subvp_stream_static_state {
	uint16_t vratio_numerator;
	uint16_t vratio_denominator;
	uint16_t phantom_vtotal;
	uint16_t phantom_vactive;
	union {
		struct {
			uint8_t is_multi_planar : 1;
			uint8_t is_yuv420 : 1;
		} bits;
		uint8_t all;
	} config;
	uint8_t programming_delay_otg_vlines;
	uint8_t prefetch_to_mall_otg_vlines;
	uint8_t phantom_otg_inst;
	uint8_t phantom_pipe_mask;
	uint8_t phantom_plane_pipe_masks[DMUB_MAX_PHANTOM_PLANES]; // phantom pipe mask per plane (for flip passthrough)
};

struct dmub_fams2_drr_stream_static_state {
	uint16_t nom_stretched_vtotal;
	uint8_t programming_delay_otg_vlines;
	uint8_t only_stretch_if_required;
	uint8_t pad[2];
};

struct dmub_fams2_stream_static_state {
	enum fams2_stream_type type;
	uint32_t otg_vline_time_ns;
	uint32_t otg_vline_time_ticks;
	uint16_t htotal;
	uint16_t vtotal; // nominal vtotal
	uint16_t vblank_start;
	uint16_t vblank_end;
	uint16_t max_vtotal;
	uint16_t allow_start_otg_vline;
	uint16_t allow_end_otg_vline;
	uint16_t drr_keepout_otg_vline; // after this vline, vtotal cannot be changed
	uint8_t scheduling_delay_otg_vlines; // min time to budget for ready to microschedule start
	uint8_t contention_delay_otg_vlines; // time to budget for contention on execution
	uint8_t vline_int_ack_delay_otg_vlines; // min time to budget for vertical interrupt firing
	uint8_t allow_to_target_delay_otg_vlines; // time from allow vline to target vline
	union {
		struct {
			uint8_t is_drr: 1; // stream is DRR enabled
			uint8_t clamp_vtotal_min: 1; // clamp vtotal to min instead of nominal
			uint8_t min_ttu_vblank_usable: 1; // if min ttu vblank is above wm, no force pstate is needed in blank
		} bits;
		uint8_t all;
	} config;
	uint8_t otg_inst;
	uint8_t pipe_mask; // pipe mask for the whole config
	uint8_t num_planes;
	uint8_t plane_pipe_masks[DMUB_MAX_PLANES]; // pipe mask per plane (for flip passthrough)
	uint8_t pad[DMUB_MAX_PLANES % 4];
	union {
		struct dmub_fams2_legacy_stream_static_state legacy;
		struct dmub_fams2_subvp_stream_static_state subvp;
		struct dmub_fams2_drr_stream_static_state drr;
	} sub_state;
};

/**
 * enum dmub_fams2_allow_delay_check_mode - macroscheduler mode for breaking on excessive
 * p-state request to allow latency
 */
enum dmub_fams2_allow_delay_check_mode {
	/* No check for request to allow delay */
	FAMS2_ALLOW_DELAY_CHECK_NONE = 0,
	/* Check for request to allow delay */
	FAMS2_ALLOW_DELAY_CHECK_FROM_START = 1,
	/* Check for prepare to allow delay */
	FAMS2_ALLOW_DELAY_CHECK_FROM_PREPARE = 2,
};

union dmub_fams2_global_feature_config {
	struct {
		uint32_t enable: 1;
		uint32_t enable_ppt_check: 1;
		uint32_t enable_stall_recovery: 1;
		uint32_t enable_debug: 1;
		uint32_t enable_offload_flip: 1;
		uint32_t enable_visual_confirm: 1;
		uint32_t allow_delay_check_mode: 2;
		uint32_t reserved: 24;
	} bits;
	uint32_t all;
};

struct dmub_cmd_fams2_global_config {
	uint32_t max_allow_delay_us; // max delay to assert allow from uclk change begin
	uint32_t lock_wait_time_us; // time to forecast acquisition of lock
	uint32_t num_streams;
	union dmub_fams2_global_feature_config features;
	uint32_t recovery_timeout_us;
	uint32_t hwfq_flip_programming_delay_us;
};

union dmub_cmd_fams2_config {
	struct dmub_cmd_fams2_global_config global;
	struct dmub_fams2_stream_static_state stream;
};

/**
 * DMUB rb command definition for FAMS2 (merged SubVP, FPO, Legacy)
 */
struct dmub_rb_cmd_fams2 {
	struct dmub_cmd_header header;
	union dmub_cmd_fams2_config config;
};

/**
 * enum dmub_cmd_idle_opt_type - Idle optimization command type.
 */
enum dmub_cmd_idle_opt_type {
	/**
	 * DCN hardware restore.
	 */
	DMUB_CMD__IDLE_OPT_DCN_RESTORE = 0,

	/**
	 * DCN hardware save.
	 */
	DMUB_CMD__IDLE_OPT_DCN_SAVE_INIT = 1,

	/**
	 * DCN hardware notify idle.
	 */
	DMUB_CMD__IDLE_OPT_DCN_NOTIFY_IDLE = 2,

	/**
	 * DCN hardware notify power state.
	 */
	DMUB_CMD__IDLE_OPT_SET_DC_POWER_STATE = 3,
};

/**
 * struct dmub_rb_cmd_idle_opt_dcn_restore - DCN restore command data.
 */
struct dmub_rb_cmd_idle_opt_dcn_restore {
	struct dmub_cmd_header header; /**< header */
};

/**
 * struct dmub_dcn_notify_idle_cntl_data - Data passed to FW in a DMUB_CMD__IDLE_OPT_DCN_NOTIFY_IDLE command.
 */
struct dmub_dcn_notify_idle_cntl_data {
	uint8_t driver_idle;
	uint8_t skip_otg_disable;
	uint8_t reserved[58];
};

/**
 * struct dmub_rb_cmd_idle_opt_dcn_notify_idle - Data passed to FW in a DMUB_CMD__IDLE_OPT_DCN_NOTIFY_IDLE command.
 */
struct dmub_rb_cmd_idle_opt_dcn_notify_idle {
	struct dmub_cmd_header header; /**< header */
	struct dmub_dcn_notify_idle_cntl_data cntl_data;
};

/**
 * enum dmub_idle_opt_dc_power_state - DC power states.
 */
enum dmub_idle_opt_dc_power_state {
	DMUB_IDLE_OPT_DC_POWER_STATE_UNKNOWN = 0,
	DMUB_IDLE_OPT_DC_POWER_STATE_D0 = 1,
	DMUB_IDLE_OPT_DC_POWER_STATE_D1 = 2,
	DMUB_IDLE_OPT_DC_POWER_STATE_D2 = 4,
	DMUB_IDLE_OPT_DC_POWER_STATE_D3 = 8,
};

/**
 * struct dmub_idle_opt_set_dc_power_state_data - Data passed to FW in a DMUB_CMD__IDLE_OPT_SET_DC_POWER_STATE command.
 */
struct dmub_idle_opt_set_dc_power_state_data {
	uint8_t power_state; /**< power state */
	uint8_t pad[3]; /**< padding */
};

/**
 * struct dmub_rb_cmd_idle_opt_set_dc_power_state - Data passed to FW in a DMUB_CMD__IDLE_OPT_SET_DC_POWER_STATE command.
 */
struct dmub_rb_cmd_idle_opt_set_dc_power_state {
	struct dmub_cmd_header header; /**< header */
	struct dmub_idle_opt_set_dc_power_state_data data;
};

/**
 * struct dmub_clocks - Clock update notification.
 */
struct dmub_clocks {
	uint32_t dispclk_khz; /**< dispclk kHz */
	uint32_t dppclk_khz; /**< dppclk kHz */
	uint32_t dcfclk_khz; /**< dcfclk kHz */
	uint32_t dcfclk_deep_sleep_khz; /**< dcfclk deep sleep kHz */
};

/**
 * enum dmub_cmd_clk_mgr_type - Clock manager commands.
 */
enum dmub_cmd_clk_mgr_type {
	/**
	 * Notify DMCUB of clock update.
	 */
	DMUB_CMD__CLK_MGR_NOTIFY_CLOCKS = 0,
};

/**
 * struct dmub_rb_cmd_clk_mgr_notify_clocks - Clock update notification.
 */
struct dmub_rb_cmd_clk_mgr_notify_clocks {
	struct dmub_cmd_header header; /**< header */
	struct dmub_clocks clocks; /**< clock data */
};

/**
 * struct dmub_cmd_digx_encoder_control_data - Encoder control data.
 */
struct dmub_cmd_digx_encoder_control_data {
	union dig_encoder_control_parameters_v1_5 dig; /**< payload */
};

/**
 * struct dmub_rb_cmd_digx_encoder_control - Encoder control command.
 */
struct dmub_rb_cmd_digx_encoder_control {
	struct dmub_cmd_header header;  /**< header */
	struct dmub_cmd_digx_encoder_control_data encoder_control; /**< payload */
};

/**
 * struct dmub_cmd_set_pixel_clock_data - Set pixel clock data.
 */
struct dmub_cmd_set_pixel_clock_data {
	struct set_pixel_clock_parameter_v1_7 clk; /**< payload */
};

/**
 * struct dmub_cmd_set_pixel_clock_data - Set pixel clock command.
 */
struct dmub_rb_cmd_set_pixel_clock {
	struct dmub_cmd_header header; /**< header */
	struct dmub_cmd_set_pixel_clock_data pixel_clock; /**< payload */
};

/**
 * struct dmub_cmd_enable_disp_power_gating_data - Display power gating.
 */
struct dmub_cmd_enable_disp_power_gating_data {
	struct enable_disp_power_gating_parameters_v2_1 pwr; /**< payload */
};

/**
 * struct dmub_rb_cmd_enable_disp_power_gating - Display power command.
 */
struct dmub_rb_cmd_enable_disp_power_gating {
	struct dmub_cmd_header header; /**< header */
	struct dmub_cmd_enable_disp_power_gating_data power_gating;  /**< payload */
};

/**
 * struct dmub_dig_transmitter_control_data_v1_7 - Transmitter control.
 */
struct dmub_dig_transmitter_control_data_v1_7 {
	uint8_t phyid; /**< 0=UNIPHYA, 1=UNIPHYB, 2=UNIPHYC, 3=UNIPHYD, 4=UNIPHYE, 5=UNIPHYF */
	uint8_t action; /**< Defined as ATOM_TRANSMITER_ACTION_xxx */
	union {
		uint8_t digmode; /**< enum atom_encode_mode_def */
		uint8_t dplaneset; /**< DP voltage swing and pre-emphasis value, "DP_LANE_SET__xDB_y_zV" */
	} mode_laneset;
	uint8_t lanenum; /**< Number of lanes */
	union {
		uint32_t symclk_10khz; /**< Symbol Clock in 10Khz */
	} symclk_units;
	uint8_t hpdsel; /**< =1: HPD1, =2: HPD2, ..., =6: HPD6, =0: HPD is not assigned */
	uint8_t digfe_sel; /**< DIG front-end selection, bit0 means DIG0 FE is enabled */
	uint8_t connobj_id; /**< Connector Object Id defined in ObjectId.h */
	uint8_t HPO_instance; /**< HPO instance (0: inst0, 1: inst1) */
	uint8_t reserved1; /**< For future use */
	uint8_t reserved2[3]; /**< For future use */
	uint32_t reserved3[11]; /**< For future use */
};

/**
 * union dmub_cmd_dig1_transmitter_control_data - Transmitter control data.
 */
union dmub_cmd_dig1_transmitter_control_data {
	struct dig_transmitter_control_parameters_v1_6 dig; /**< payload */
	struct dmub_dig_transmitter_control_data_v1_7 dig_v1_7;  /**< payload 1.7 */
};

/**
 * struct dmub_rb_cmd_dig1_transmitter_control - Transmitter control command.
 */
struct dmub_rb_cmd_dig1_transmitter_control {
	struct dmub_cmd_header header; /**< header */
	union dmub_cmd_dig1_transmitter_control_data transmitter_control; /**< payload */
};

/**
 * struct dmub_rb_cmd_domain_control_data - Data for DOMAIN power control
 */
struct dmub_rb_cmd_domain_control_data {
	uint8_t inst : 6; /**< DOMAIN instance to control */
	uint8_t power_gate : 1; /**< 1=power gate, 0=power up */
	uint8_t reserved[3]; /**< Reserved for future use */
};

/**
 * struct dmub_rb_cmd_domain_control - Controls DOMAIN power gating
 */
struct dmub_rb_cmd_domain_control {
	struct dmub_cmd_header header; /**< header */
	struct dmub_rb_cmd_domain_control_data data; /**< payload */
};

/**
 * DPIA tunnel command parameters.
 */
struct dmub_cmd_dig_dpia_control_data {
	uint8_t enc_id;         /** 0 = ENGINE_ID_DIGA, ... */
	uint8_t action;         /** ATOM_TRANSMITER_ACTION_DISABLE/ENABLE/SETUP_VSEMPH */
	union {
		uint8_t digmode;    /** enum atom_encode_mode_def */
		uint8_t dplaneset;  /** DP voltage swing and pre-emphasis value */
	} mode_laneset;
	uint8_t lanenum;        /** Lane number 1, 2, 4, 8 */
	uint32_t symclk_10khz;  /** Symbol Clock in 10Khz */
	uint8_t hpdsel;         /** =0: HPD is not assigned */
	uint8_t digfe_sel;      /** DIG stream( front-end ) selection, bit0 - DIG0 FE */
	uint8_t dpia_id;        /** Index of DPIA */
	uint8_t fec_rdy : 1;
	uint8_t reserved : 7;
	uint32_t reserved1;
};

/**
 * DMUB command for DPIA tunnel control.
 */
struct dmub_rb_cmd_dig1_dpia_control {
	struct dmub_cmd_header header;
	struct dmub_cmd_dig_dpia_control_data dpia_control;
};

/**
 * SET_CONFIG Command Payload
 */
struct set_config_cmd_payload {
	uint8_t msg_type; /* set config message type */
	uint8_t msg_data; /* set config message data */
};

/**
 * Data passed from driver to FW in a DMUB_CMD__DPIA_SET_CONFIG_ACCESS command.
 */
struct dmub_cmd_set_config_control_data {
	struct set_config_cmd_payload cmd_pkt;
	uint8_t instance; /* DPIA instance */
	uint8_t immed_status; /* Immediate status returned in case of error */
};

/**
 * DMUB command structure for SET_CONFIG command.
 */
struct dmub_rb_cmd_set_config_access {
	struct dmub_cmd_header header; /* header */
	struct dmub_cmd_set_config_control_data set_config_control; /* set config data */
};

/**
 * Data passed from driver to FW in a DMUB_CMD__DPIA_MST_ALLOC_SLOTS command.
 */
struct dmub_cmd_mst_alloc_slots_control_data {
	uint8_t mst_alloc_slots; /* mst slots to be allotted */
	uint8_t instance; /* DPIA instance */
	uint8_t immed_status; /* Immediate status returned as there is no outbox msg posted */
	uint8_t mst_slots_in_use; /* returns slots in use for error cases */
};

/**
 * DMUB command structure for SET_ command.
 */
struct dmub_rb_cmd_set_mst_alloc_slots {
	struct dmub_cmd_header header; /* header */
	struct dmub_cmd_mst_alloc_slots_control_data mst_slots_control; /* mst slots control */
};

/**
 * Data passed from driver to FW in a DMUB_CMD__SET_TPS_NOTIFICATION command.
 */
struct dmub_cmd_tps_notification_data {
	uint8_t instance; /* DPIA instance */
	uint8_t tps; /* requested training pattern */
	uint8_t reserved1;
	uint8_t reserved2;
};

/**
 * DMUB command structure for SET_TPS_NOTIFICATION command.
 */
struct dmub_rb_cmd_set_tps_notification {
	struct dmub_cmd_header header; /* header */
	struct dmub_cmd_tps_notification_data tps_notification; /* set tps_notification data */
};

/**
 * DMUB command structure for DPIA HPD int enable control.
 */
struct dmub_rb_cmd_dpia_hpd_int_enable {
	struct dmub_cmd_header header; /* header */
	uint32_t enable; /* dpia hpd interrupt enable */
};

/**
 * struct dmub_rb_cmd_dpphy_init - DPPHY init.
 */
struct dmub_rb_cmd_dpphy_init {
	struct dmub_cmd_header header; /**< header */
	uint8_t reserved[60]; /**< reserved bits */
};

/**
 * enum dp_aux_request_action - DP AUX request command listing.
 *
 * 4 AUX request command bits are shifted to high nibble.
 */
enum dp_aux_request_action {
	/** I2C-over-AUX write request */
	DP_AUX_REQ_ACTION_I2C_WRITE		= 0x00,
	/** I2C-over-AUX read request */
	DP_AUX_REQ_ACTION_I2C_READ		= 0x10,
	/** I2C-over-AUX write status request */
	DP_AUX_REQ_ACTION_I2C_STATUS_REQ	= 0x20,
	/** I2C-over-AUX write request with MOT=1 */
	DP_AUX_REQ_ACTION_I2C_WRITE_MOT		= 0x40,
	/** I2C-over-AUX read request with MOT=1 */
	DP_AUX_REQ_ACTION_I2C_READ_MOT		= 0x50,
	/** I2C-over-AUX write status request with MOT=1 */
	DP_AUX_REQ_ACTION_I2C_STATUS_REQ_MOT	= 0x60,
	/** Native AUX write request */
	DP_AUX_REQ_ACTION_DPCD_WRITE		= 0x80,
	/** Native AUX read request */
	DP_AUX_REQ_ACTION_DPCD_READ		= 0x90
};

/**
 * enum aux_return_code_type - DP AUX process return code listing.
 */
enum aux_return_code_type {
	/** AUX process succeeded */
	AUX_RET_SUCCESS = 0,
	/** AUX process failed with unknown reason */
	AUX_RET_ERROR_UNKNOWN,
	/** AUX process completed with invalid reply */
	AUX_RET_ERROR_INVALID_REPLY,
	/** AUX process timed out */
	AUX_RET_ERROR_TIMEOUT,
	/** HPD was low during AUX process */
	AUX_RET_ERROR_HPD_DISCON,
	/** Failed to acquire AUX engine */
	AUX_RET_ERROR_ENGINE_ACQUIRE,
	/** AUX request not supported */
	AUX_RET_ERROR_INVALID_OPERATION,
	/** AUX process not available */
	AUX_RET_ERROR_PROTOCOL_ERROR,
};

/**
 * enum aux_channel_type - DP AUX channel type listing.
 */
enum aux_channel_type {
	/** AUX thru Legacy DP AUX */
	AUX_CHANNEL_LEGACY_DDC,
	/** AUX thru DPIA DP tunneling */
	AUX_CHANNEL_DPIA
};

/**
 * struct aux_transaction_parameters - DP AUX request transaction data
 */
struct aux_transaction_parameters {
	uint8_t is_i2c_over_aux; /**< 0=native AUX, 1=I2C-over-AUX */
	uint8_t action; /**< enum dp_aux_request_action */
	uint8_t length; /**< DP AUX request data length */
	uint8_t reserved; /**< For future use */
	uint32_t address; /**< DP AUX address */
	uint8_t data[16]; /**< DP AUX write data */
};

/**
 * Data passed from driver to FW in a DMUB_CMD__DP_AUX_ACCESS command.
 */
struct dmub_cmd_dp_aux_control_data {
	uint8_t instance; /**< AUX instance or DPIA instance */
	uint8_t manual_acq_rel_enable; /**< manual control for acquiring or releasing AUX channel */
	uint8_t sw_crc_enabled; /**< Use software CRC for tunneling packet instead of hardware CRC */
	uint8_t reserved0; /**< For future use */
	uint16_t timeout; /**< timeout time in us */
	uint16_t reserved1; /**< For future use */
	enum aux_channel_type type; /**< enum aux_channel_type */
	struct aux_transaction_parameters dpaux; /**< struct aux_transaction_parameters */
};

/**
 * Definition of a DMUB_CMD__DP_AUX_ACCESS command.
 */
struct dmub_rb_cmd_dp_aux_access {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__DP_AUX_ACCESS command.
	 */
	struct dmub_cmd_dp_aux_control_data aux_control;
};

/**
 * Definition of a DMUB_CMD__OUTBOX1_ENABLE command.
 */
struct dmub_rb_cmd_outbox1_enable {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 *  enable: 0x0 -> disable outbox1 notification (default value)
	 *			0x1 -> enable outbox1 notification
	 */
	uint32_t enable;
};

/* DP AUX Reply command - OutBox Cmd */
/**
 * Data passed to driver from FW in a DMUB_OUT_CMD__DP_AUX_REPLY command.
 */
struct aux_reply_data {
	/**
	 * Aux cmd
	 */
	uint8_t command;
	/**
	 * Aux reply data length (max: 16 bytes)
	 */
	uint8_t length;
	/**
	 * Alignment only
	 */
	uint8_t pad[2];
	/**
	 * Aux reply data
	 */
	uint8_t data[16];
};

/**
 * Control Data passed to driver from FW in a DMUB_OUT_CMD__DP_AUX_REPLY command.
 */
struct aux_reply_control_data {
	/**
	 * Reserved for future use
	 */
	uint32_t handle;
	/**
	 * Aux Instance
	 */
	uint8_t instance;
	/**
	 * Aux transaction result: definition in enum aux_return_code_type
	 */
	uint8_t result;
	/**
	 * Alignment only
	 */
	uint16_t pad;
};

/**
 * Definition of a DMUB_OUT_CMD__DP_AUX_REPLY command.
 */
struct dmub_rb_cmd_dp_aux_reply {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Control Data passed to driver from FW in a DMUB_OUT_CMD__DP_AUX_REPLY command.
	 */
	struct aux_reply_control_data control;
	/**
	 * Data passed to driver from FW in a DMUB_OUT_CMD__DP_AUX_REPLY command.
	 */
	struct aux_reply_data reply_data;
};

/* DP HPD Notify command - OutBox Cmd */
/**
 * DP HPD Type
 */
enum dp_hpd_type {
	/**
	 * Normal DP HPD
	 */
	DP_HPD = 0,
	/**
	 * DP HPD short pulse
	 */
	DP_IRQ
};

/**
 * DP HPD Status
 */
enum dp_hpd_status {
	/**
	 * DP_HPD status low
	 */
	DP_HPD_UNPLUG = 0,
	/**
	 * DP_HPD status high
	 */
	DP_HPD_PLUG
};

/**
 * Data passed to driver from FW in a DMUB_OUT_CMD__DP_HPD_NOTIFY command.
 */
struct dp_hpd_data {
	/**
	 * DP HPD instance
	 */
	uint8_t instance;
	/**
	 * HPD type
	 */
	uint8_t hpd_type;
	/**
	 * HPD status: only for type: DP_HPD to indicate status
	 */
	uint8_t hpd_status;
	/**
	 * Alignment only
	 */
	uint8_t pad;
};

/**
 * Definition of a DMUB_OUT_CMD__DP_HPD_NOTIFY command.
 */
struct dmub_rb_cmd_dp_hpd_notify {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed to driver from FW in a DMUB_OUT_CMD__DP_HPD_NOTIFY command.
	 */
	struct dp_hpd_data hpd_data;
};

/**
 * Definition of a SET_CONFIG reply from DPOA.
 */
enum set_config_status {
	SET_CONFIG_PENDING = 0,
	SET_CONFIG_ACK_RECEIVED,
	SET_CONFIG_RX_TIMEOUT,
	SET_CONFIG_UNKNOWN_ERROR,
};

/**
 * Definition of a set_config reply
 */
struct set_config_reply_control_data {
	uint8_t instance; /* DPIA Instance */
	uint8_t status; /* Set Config reply */
	uint16_t pad; /* Alignment */
};

/**
 * Definition of a DMUB_OUT_CMD__SET_CONFIG_REPLY command.
 */
struct dmub_rb_cmd_dp_set_config_reply {
	struct dmub_cmd_header header;
	struct set_config_reply_control_data set_config_reply_control;
};

/**
 * Definition of a DPIA notification header
 */
struct dpia_notification_header {
	uint8_t instance; /**< DPIA Instance */
	uint8_t reserved[3];
	enum dmub_cmd_dpia_notification_type type; /**< DPIA notification type */
};

/**
 * Definition of the common data struct of DPIA notification
 */
struct dpia_notification_common {
	uint8_t cmd_buffer[DMUB_RB_CMD_SIZE - sizeof(struct dmub_cmd_header)
								- sizeof(struct dpia_notification_header)];
};

/**
 * Definition of a DPIA notification data
 */
struct dpia_bw_allocation_notify_data {
	union {
		struct {
			uint16_t cm_bw_alloc_support: 1; /**< USB4 CM BW Allocation mode support */
			uint16_t bw_request_failed: 1; /**< BW_Request_Failed */
			uint16_t bw_request_succeeded: 1; /**< BW_Request_Succeeded */
			uint16_t est_bw_changed: 1; /**< Estimated_BW changed */
			uint16_t bw_alloc_cap_changed: 1; /**< BW_Allocation_Capabiity_Changed */
			uint16_t reserved: 11; /**< Reserved */
		} bits;

		uint16_t flags;
	};

	uint8_t cm_id; /**< CM ID */
	uint8_t group_id; /**< Group ID */
	uint8_t granularity; /**< BW Allocation Granularity */
	uint8_t estimated_bw; /**< Estimated_BW */
	uint8_t allocated_bw; /**< Allocated_BW */
	uint8_t reserved;
};

/**
 * union dpia_notify_data_type - DPIA Notification in Outbox command
 */
union dpia_notification_data {
	/**
	 * DPIA Notification for common data struct
	 */
	struct dpia_notification_common common_data;

	/**
	 * DPIA Notification for DP BW Allocation support
	 */
	struct dpia_bw_allocation_notify_data dpia_bw_alloc;
};

/**
 * Definition of a DPIA notification payload
 */
struct dpia_notification_payload {
	struct dpia_notification_header header;
	union dpia_notification_data data; /**< DPIA notification payload data */
};

/**
 * Definition of a DMUB_OUT_CMD__DPIA_NOTIFICATION command.
 */
struct dmub_rb_cmd_dpia_notification {
	struct dmub_cmd_header header; /**< DPIA notification header */
	struct dpia_notification_payload payload; /**< DPIA notification payload */
};

/**
 * Data passed from driver to FW in a DMUB_CMD__QUERY_HPD_STATE command.
 */
struct dmub_cmd_hpd_state_query_data {
	uint8_t instance; /**< HPD instance or DPIA instance */
	uint8_t result; /**< For returning HPD state */
	uint16_t pad; /** < Alignment */
	enum aux_channel_type ch_type; /**< enum aux_channel_type */
	enum aux_return_code_type status; /**< for returning the status of command */
};

/**
 * Definition of a DMUB_CMD__QUERY_HPD_STATE command.
 */
struct dmub_rb_cmd_query_hpd_state {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__QUERY_HPD_STATE command.
	 */
	struct dmub_cmd_hpd_state_query_data data;
};

/**
 * struct dmub_rb_cmd_hpd_sense_notify - HPD sense notification data.
 */
struct dmub_rb_cmd_hpd_sense_notify_data {
	uint32_t old_hpd_sense_mask; /**< Old HPD sense mask */
	uint32_t new_hpd_sense_mask; /**< New HPD sense mask */
};

/**
 * struct dmub_rb_cmd_hpd_sense_notify - DMUB_OUT_CMD__HPD_SENSE_NOTIFY command.
 */
struct dmub_rb_cmd_hpd_sense_notify {
	struct dmub_cmd_header header; /**< header */
	struct dmub_rb_cmd_hpd_sense_notify_data data; /**< payload */
};

/*
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */

/**
 * PSR command sub-types.
 */
enum dmub_cmd_psr_type {
	/**
	 * Set PSR version support.
	 */
	DMUB_CMD__PSR_SET_VERSION		= 0,
	/**
	 * Copy driver-calculated parameters to PSR state.
	 */
	DMUB_CMD__PSR_COPY_SETTINGS		= 1,
	/**
	 * Enable PSR.
	 */
	DMUB_CMD__PSR_ENABLE			= 2,

	/**
	 * Disable PSR.
	 */
	DMUB_CMD__PSR_DISABLE			= 3,

	/**
	 * Set PSR level.
	 * PSR level is a 16-bit value dicated by driver that
	 * will enable/disable different functionality.
	 */
	DMUB_CMD__PSR_SET_LEVEL			= 4,

	/**
	 * Forces PSR enabled until an explicit PSR disable call.
	 */
	DMUB_CMD__PSR_FORCE_STATIC		= 5,
	/**
	 * Set vtotal in psr active for FreeSync PSR.
	 */
	DMUB_CMD__SET_SINK_VTOTAL_IN_PSR_ACTIVE = 6,
	/**
	 * Set PSR power option
	 */
	DMUB_CMD__SET_PSR_POWER_OPT = 7,
};

/**
 * Different PSR residency modes.
 * Different modes change the definition of PSR residency.
 */
enum psr_residency_mode {
	PSR_RESIDENCY_MODE_PHY = 0,
	PSR_RESIDENCY_MODE_ALPM,
	PSR_RESIDENCY_MODE_ENABLEMENT_PERIOD,
	/* Do not add below. */
	PSR_RESIDENCY_MODE_LAST_ELEMENT,
};

enum dmub_cmd_fams_type {
	DMUB_CMD__FAMS_SETUP_FW_CTRL	= 0,
	DMUB_CMD__FAMS_DRR_UPDATE		= 1,
	DMUB_CMD__HANDLE_SUBVP_CMD	= 2, // specifically for SubVP cmd
	/**
	 * For SubVP set manual trigger in FW because it
	 * triggers DRR_UPDATE_PENDING which SubVP relies
	 * on (for any SubVP cases that use a DRR display)
	 */
	DMUB_CMD__FAMS_SET_MANUAL_TRIGGER = 3,
	DMUB_CMD__FAMS2_CONFIG = 4,
	DMUB_CMD__FAMS2_DRR_UPDATE = 5,
	DMUB_CMD__FAMS2_FLIP = 6,
};

/**
 * PSR versions.
 */
enum psr_version {
	/**
	 * PSR version 1.
	 */
	PSR_VERSION_1				= 0,
	/**
	 * Freesync PSR SU.
	 */
	PSR_VERSION_SU_1			= 1,
	/**
	 * PSR not supported.
	 */
	PSR_VERSION_UNSUPPORTED			= 0xFF,	// psr_version field is only 8 bits wide
};

/**
 * PHY Link rate for DP.
 */
enum phy_link_rate {
	/**
	 * not supported.
	 */
	PHY_RATE_UNKNOWN = 0,
	/**
	 * Rate_1 (RBR)	- 1.62 Gbps/Lane
	 */
	PHY_RATE_162 = 1,
	/**
	 * Rate_2		- 2.16 Gbps/Lane
	 */
	PHY_RATE_216 = 2,
	/**
	 * Rate_3		- 2.43 Gbps/Lane
	 */
	PHY_RATE_243 = 3,
	/**
	 * Rate_4 (HBR)	- 2.70 Gbps/Lane
	 */
	PHY_RATE_270 = 4,
	/**
	 * Rate_5 (RBR2)- 3.24 Gbps/Lane
	 */
	PHY_RATE_324 = 5,
	/**
	 * Rate_6		- 4.32 Gbps/Lane
	 */
	PHY_RATE_432 = 6,
	/**
	 * Rate_7 (HBR2)- 5.40 Gbps/Lane
	 */
	PHY_RATE_540 = 7,
	/**
	 * Rate_8 (HBR3)- 8.10 Gbps/Lane
	 */
	PHY_RATE_810 = 8,
	/**
	 * UHBR10 - 10.0 Gbps/Lane
	 */
	PHY_RATE_1000 = 9,
	/**
	 * UHBR13.5 - 13.5 Gbps/Lane
	 */
	PHY_RATE_1350 = 10,
	/**
	 * UHBR10 - 20.0 Gbps/Lane
	 */
	PHY_RATE_2000 = 11,

	PHY_RATE_675 = 12,
	/**
	 * Rate 12 - 6.75 Gbps/Lane
	 */
};

/**
 * enum dmub_phy_fsm_state - PHY FSM states.
 * PHY FSM state to transit to during PSR enable/disable.
 */
enum dmub_phy_fsm_state {
	DMUB_PHY_FSM_POWER_UP_DEFAULT = 0,
	DMUB_PHY_FSM_RESET,
	DMUB_PHY_FSM_RESET_RELEASED,
	DMUB_PHY_FSM_SRAM_LOAD_DONE,
	DMUB_PHY_FSM_INITIALIZED,
	DMUB_PHY_FSM_CALIBRATED,
	DMUB_PHY_FSM_CALIBRATED_LP,
	DMUB_PHY_FSM_CALIBRATED_PG,
	DMUB_PHY_FSM_POWER_DOWN,
	DMUB_PHY_FSM_PLL_EN,
	DMUB_PHY_FSM_TX_EN,
	DMUB_PHY_FSM_TX_EN_TEST_MODE,
	DMUB_PHY_FSM_FAST_LP,
	DMUB_PHY_FSM_P2_PLL_OFF_CPM,
	DMUB_PHY_FSM_P2_PLL_OFF_PG,
	DMUB_PHY_FSM_P2_PLL_OFF,
	DMUB_PHY_FSM_P2_PLL_ON,
};

/**
 * Data passed from driver to FW in a DMUB_CMD__PSR_COPY_SETTINGS command.
 */
struct dmub_cmd_psr_copy_settings_data {
	/**
	 * Flags that can be set by driver to change some PSR behaviour.
	 */
	union dmub_psr_debug_flags debug;
	/**
	 * 16-bit value dicated by driver that will enable/disable different functionality.
	 */
	uint16_t psr_level;
	/**
	 * DPP HW instance.
	 */
	uint8_t dpp_inst;
	/**
	 * MPCC HW instance.
	 * Not used in dmub fw,
	 * dmub fw will get active opp by reading odm registers.
	 */
	uint8_t mpcc_inst;
	/**
	 * OPP HW instance.
	 * Not used in dmub fw,
	 * dmub fw will get active opp by reading odm registers.
	 */
	uint8_t opp_inst;
	/**
	 * OTG HW instance.
	 */
	uint8_t otg_inst;
	/**
	 * DIG FE HW instance.
	 */
	uint8_t digfe_inst;
	/**
	 * DIG BE HW instance.
	 */
	uint8_t digbe_inst;
	/**
	 * DP PHY HW instance.
	 */
	uint8_t dpphy_inst;
	/**
	 * AUX HW instance.
	 */
	uint8_t aux_inst;
	/**
	 * Determines if SMU optimzations are enabled/disabled.
	 */
	uint8_t smu_optimizations_en;
	/**
	 * Unused.
	 * TODO: Remove.
	 */
	uint8_t frame_delay;
	/**
	 * If RFB setup time is greater than the total VBLANK time,
	 * it is not possible for the sink to capture the video frame
	 * in the same frame the SDP is sent. In this case,
	 * the frame capture indication bit should be set and an extra
	 * static frame should be transmitted to the sink.
	 */
	uint8_t frame_cap_ind;
	/**
	 * Granularity of Y offset supported by sink.
	 */
	uint8_t su_y_granularity;
	/**
	 * Indicates whether sink should start capturing
	 * immediately following active scan line,
	 * or starting with the 2nd active scan line.
	 */
	uint8_t line_capture_indication;
	/**
	 * Multi-display optimizations are implemented on certain ASICs.
	 */
	uint8_t multi_disp_optimizations_en;
	/**
	 * The last possible line SDP may be transmitted without violating
	 * the RFB setup time or entering the active video frame.
	 */
	uint16_t init_sdp_deadline;
	/**
	 * @ rate_control_caps : Indicate FreeSync PSR Sink Capabilities
	 */
	uint8_t rate_control_caps ;
	/*
	 * Force PSRSU always doing full frame update
	 */
	uint8_t force_ffu_mode;
	/**
	 * Length of each horizontal line in us.
	 */
	uint32_t line_time_in_us;
	/**
	 * FEC enable status in driver
	 */
	uint8_t fec_enable_status;
	/**
	 * FEC re-enable delay when PSR exit.
	 * unit is 100us, range form 0~255(0xFF).
	 */
	uint8_t fec_enable_delay_in100us;
	/**
	 * PSR control version.
	 */
	uint8_t cmd_version;
	/**
	 * Panel Instance.
	 * Panel instance to identify which psr_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/*
	 * DSC enable status in driver
	 */
	uint8_t dsc_enable_status;
	/*
	 * Use FSM state for PSR power up/down
	 */
	uint8_t use_phy_fsm;
	/**
	 * frame delay for frame re-lock
	 */
	uint8_t relock_delay_frame_cnt;
	/**
	 * esd recovery indicate.
	 */
	uint8_t esd_recovery;
	/**
	 * DSC Slice height.
	 */
	uint16_t dsc_slice_height;
	/**
	 * Some panels request main link off before xth vertical line
	 */
	uint16_t poweroff_before_vertical_line;
	/**
	 * Some panels cannot handle idle pattern during PSR entry.
	 * To power down phy before disable stream to avoid sending
	 * idle pattern.
	 */
	uint8_t power_down_phy_before_disable_stream;
};

/**
 * Definition of a DMUB_CMD__PSR_COPY_SETTINGS command.
 */
struct dmub_rb_cmd_psr_copy_settings {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__PSR_COPY_SETTINGS command.
	 */
	struct dmub_cmd_psr_copy_settings_data psr_copy_settings_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__PSR_SET_LEVEL command.
 */
struct dmub_cmd_psr_set_level_data {
	/**
	 * 16-bit value dicated by driver that will enable/disable different functionality.
	 */
	uint16_t psr_level;
	/**
	 * PSR control version.
	 */
	uint8_t cmd_version;
	/**
	 * Panel Instance.
	 * Panel instance to identify which psr_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
};

/**
 * Definition of a DMUB_CMD__PSR_SET_LEVEL command.
 */
struct dmub_rb_cmd_psr_set_level {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of a DMUB_CMD__PSR_SET_LEVEL command.
	 */
	struct dmub_cmd_psr_set_level_data psr_set_level_data;
};

struct dmub_rb_cmd_psr_enable_data {
	/**
	 * PSR control version.
	 */
	uint8_t cmd_version;
	/**
	 * Panel Instance.
	 * Panel instance to identify which psr_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * Phy state to enter.
	 * Values to use are defined in dmub_phy_fsm_state
	 */
	uint8_t phy_fsm_state;
	/**
	 * Phy rate for DP - RBR/HBR/HBR2/HBR3.
	 * Set this using enum phy_link_rate.
	 * This does not support HDMI/DP2 for now.
	 */
	uint8_t phy_rate;
};

/**
 * Definition of a DMUB_CMD__PSR_ENABLE command.
 * PSR enable/disable is controlled using the sub_type.
 */
struct dmub_rb_cmd_psr_enable {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	struct dmub_rb_cmd_psr_enable_data data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__PSR_SET_VERSION command.
 */
struct dmub_cmd_psr_set_version_data {
	/**
	 * PSR version that FW should implement.
	 */
	enum psr_version version;
	/**
	 * PSR control version.
	 */
	uint8_t cmd_version;
	/**
	 * Panel Instance.
	 * Panel instance to identify which psr_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
};

/**
 * Definition of a DMUB_CMD__PSR_SET_VERSION command.
 */
struct dmub_rb_cmd_psr_set_version {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__PSR_SET_VERSION command.
	 */
	struct dmub_cmd_psr_set_version_data psr_set_version_data;
};

struct dmub_cmd_psr_force_static_data {
	/**
	 * PSR control version.
	 */
	uint8_t cmd_version;
	/**
	 * Panel Instance.
	 * Panel instance to identify which psr_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
};

/**
 * Definition of a DMUB_CMD__PSR_FORCE_STATIC command.
 */
struct dmub_rb_cmd_psr_force_static {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__PSR_FORCE_STATIC command.
	 */
	struct dmub_cmd_psr_force_static_data psr_force_static_data;
};

/**
 * PSR SU debug flags.
 */
union dmub_psr_su_debug_flags {
	/**
	 * PSR SU debug flags.
	 */
	struct {
		/**
		 * Update dirty rect in SW only.
		 */
		uint8_t update_dirty_rect_only : 1;
		/**
		 * Reset the cursor/plane state before processing the call.
		 */
		uint8_t reset_state : 1;
	} bitfields;

	/**
	 * Union for debug flags.
	 */
	uint32_t u32All;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__UPDATE_DIRTY_RECT command.
 * This triggers a selective update for PSR SU.
 */
struct dmub_cmd_update_dirty_rect_data {
	/**
	 * Dirty rects from OS.
	 */
	struct dmub_rect src_dirty_rects[DMUB_MAX_DIRTY_RECTS];
	/**
	 * PSR SU debug flags.
	 */
	union dmub_psr_su_debug_flags debug_flags;
	/**
	 * OTG HW instance.
	 */
	uint8_t pipe_idx;
	/**
	 * Number of dirty rects.
	 */
	uint8_t dirty_rect_count;
	/**
	 * PSR control version.
	 */
	uint8_t cmd_version;
	/**
	 * Panel Instance.
	 * Panel instance to identify which psr_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
};

/**
 * Definition of a DMUB_CMD__UPDATE_DIRTY_RECT command.
 */
struct dmub_rb_cmd_update_dirty_rect {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__UPDATE_DIRTY_RECT command.
	 */
	struct dmub_cmd_update_dirty_rect_data update_dirty_rect_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__UPDATE_CURSOR_INFO command.
 */
union dmub_reg_cursor_control_cfg {
	struct {
		uint32_t     cur_enable: 1;
		uint32_t         reser0: 3;
		uint32_t cur_2x_magnify: 1;
		uint32_t         reser1: 3;
		uint32_t           mode: 3;
		uint32_t         reser2: 5;
		uint32_t          pitch: 2;
		uint32_t         reser3: 6;
		uint32_t line_per_chunk: 5;
		uint32_t         reser4: 3;
	} bits;
	uint32_t raw;
};
struct dmub_cursor_position_cache_hubp {
	union dmub_reg_cursor_control_cfg cur_ctl;
	union dmub_reg_position_cfg {
		struct {
			uint32_t cur_x_pos: 16;
			uint32_t cur_y_pos: 16;
		} bits;
		uint32_t raw;
	} position;
	union dmub_reg_hot_spot_cfg {
		struct {
			uint32_t hot_x: 16;
			uint32_t hot_y: 16;
		} bits;
		uint32_t raw;
	} hot_spot;
	union dmub_reg_dst_offset_cfg {
		struct {
			uint32_t dst_x_offset: 13;
			uint32_t reserved: 19;
		} bits;
		uint32_t raw;
	} dst_offset;
};

union dmub_reg_cur0_control_cfg {
	struct {
		uint32_t     cur0_enable: 1;
		uint32_t  expansion_mode: 1;
		uint32_t          reser0: 1;
		uint32_t     cur0_rom_en: 1;
		uint32_t            mode: 3;
		uint32_t        reserved: 25;
	} bits;
	uint32_t raw;
};
struct dmub_cursor_position_cache_dpp {
	union dmub_reg_cur0_control_cfg cur0_ctl;
};
struct dmub_cursor_position_cfg {
	struct  dmub_cursor_position_cache_hubp pHubp;
	struct  dmub_cursor_position_cache_dpp  pDpp;
	uint8_t pipe_idx;
	/*
	 * Padding is required. To be 4 Bytes Aligned.
	 */
	uint8_t padding[3];
};

struct dmub_cursor_attribute_cache_hubp {
	uint32_t SURFACE_ADDR_HIGH;
	uint32_t SURFACE_ADDR;
	union    dmub_reg_cursor_control_cfg  cur_ctl;
	union    dmub_reg_cursor_size_cfg {
		struct {
			uint32_t width: 16;
			uint32_t height: 16;
		} bits;
		uint32_t raw;
	} size;
	union    dmub_reg_cursor_settings_cfg {
		struct {
			uint32_t     dst_y_offset: 8;
			uint32_t chunk_hdl_adjust: 2;
			uint32_t         reserved: 22;
		} bits;
		uint32_t raw;
	} settings;
};
struct dmub_cursor_attribute_cache_dpp {
	union dmub_reg_cur0_control_cfg cur0_ctl;
};
struct dmub_cursor_attributes_cfg {
	struct  dmub_cursor_attribute_cache_hubp aHubp;
	struct  dmub_cursor_attribute_cache_dpp  aDpp;
};

struct dmub_cmd_update_cursor_payload0 {
	/**
	 * Cursor dirty rects.
	 */
	struct dmub_rect cursor_rect;
	/**
	 * PSR SU debug flags.
	 */
	union dmub_psr_su_debug_flags debug_flags;
	/**
	 * Cursor enable/disable.
	 */
	uint8_t enable;
	/**
	 * OTG HW instance.
	 */
	uint8_t pipe_idx;
	/**
	 * PSR control version.
	 */
	uint8_t cmd_version;
	/**
	 * Panel Instance.
	 * Panel instance to identify which psr_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * Cursor Position Register.
	 * Registers contains Hubp & Dpp modules
	 */
	struct dmub_cursor_position_cfg position_cfg;
};

struct dmub_cmd_update_cursor_payload1 {
	struct dmub_cursor_attributes_cfg attribute_cfg;
};

union dmub_cmd_update_cursor_info_data {
	struct dmub_cmd_update_cursor_payload0 payload0;
	struct dmub_cmd_update_cursor_payload1 payload1;
};
/**
 * Definition of a DMUB_CMD__UPDATE_CURSOR_INFO command.
 */
struct dmub_rb_cmd_update_cursor_info {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__UPDATE_CURSOR_INFO command.
	 */
	union dmub_cmd_update_cursor_info_data update_cursor_info_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__SET_SINK_VTOTAL_IN_PSR_ACTIVE command.
 */
struct dmub_cmd_psr_set_vtotal_data {
	/**
	 * 16-bit value dicated by driver that indicates the vtotal in PSR active requirement when screen idle..
	 */
	uint16_t psr_vtotal_idle;
	/**
	 * PSR control version.
	 */
	uint8_t cmd_version;
	/**
	 * Panel Instance.
	 * Panel instance to identify which psr_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/*
	 * 16-bit value dicated by driver that indicates the vtotal in PSR active requirement when doing SU/FFU.
	 */
	uint16_t psr_vtotal_su;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad2[2];
};

/**
 * Definition of a DMUB_CMD__SET_SINK_VTOTAL_IN_PSR_ACTIVE command.
 */
struct dmub_rb_cmd_psr_set_vtotal {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of a DMUB_CMD__SET_SINK_VTOTAL_IN_PSR_ACTIVE command.
	 */
	struct dmub_cmd_psr_set_vtotal_data psr_set_vtotal_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__SET_PSR_POWER_OPT command.
 */
struct dmub_cmd_psr_set_power_opt_data {
	/**
	 * PSR control version.
	 */
	uint8_t cmd_version;
	/**
	 * Panel Instance.
	 * Panel instance to identify which psr_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
	/**
	 * PSR power option
	 */
	uint32_t power_opt;
};

/**
 * Definition of a DMUB_CMD__SET_PSR_POWER_OPT command.
 */
struct dmub_rb_cmd_psr_set_power_opt {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of a DMUB_CMD__SET_PSR_POWER_OPT command.
	 */
	struct dmub_cmd_psr_set_power_opt_data psr_set_power_opt_data;
};

/**
 * Definition of Replay Residency GPINT command.
 * Bit[0] - Residency mode for Revision 0
 * Bit[1] - Enable/Disable state
 * Bit[2-3] - Revision number
 * Bit[4-7] - Residency mode for Revision 1
 * Bit[8] - Panel instance
 * Bit[9-15] - Reserved
 */

enum pr_residency_mode {
	PR_RESIDENCY_MODE_PHY = 0x0,
	PR_RESIDENCY_MODE_ALPM,
	PR_RESIDENCY_MODE_IPS2,
	PR_RESIDENCY_MODE_FRAME_CNT,
	PR_RESIDENCY_MODE_ENABLEMENT_PERIOD,
};

#define REPLAY_RESIDENCY_MODE_SHIFT            (0)
#define REPLAY_RESIDENCY_ENABLE_SHIFT          (1)
#define REPLAY_RESIDENCY_REVISION_SHIFT        (2)
#define REPLAY_RESIDENCY_MODE2_SHIFT           (4)

#define REPLAY_RESIDENCY_MODE_MASK             (0x1 << REPLAY_RESIDENCY_MODE_SHIFT)
# define REPLAY_RESIDENCY_FIELD_MODE_PHY       (0x0 << REPLAY_RESIDENCY_MODE_SHIFT)
# define REPLAY_RESIDENCY_FIELD_MODE_ALPM      (0x1 << REPLAY_RESIDENCY_MODE_SHIFT)

#define REPLAY_RESIDENCY_MODE2_MASK            (0xF << REPLAY_RESIDENCY_MODE2_SHIFT)
# define REPLAY_RESIDENCY_FIELD_MODE2_IPS      (0x1 << REPLAY_RESIDENCY_MODE2_SHIFT)
# define REPLAY_RESIDENCY_FIELD_MODE2_FRAME_CNT    (0x2 << REPLAY_RESIDENCY_MODE2_SHIFT)
# define REPLAY_RESIDENCY_FIELD_MODE2_EN_PERIOD	(0x3 << REPLAY_RESIDENCY_MODE2_SHIFT)

#define REPLAY_RESIDENCY_ENABLE_MASK           (0x1 << REPLAY_RESIDENCY_ENABLE_SHIFT)
# define REPLAY_RESIDENCY_DISABLE              (0x0 << REPLAY_RESIDENCY_ENABLE_SHIFT)
# define REPLAY_RESIDENCY_ENABLE               (0x1 << REPLAY_RESIDENCY_ENABLE_SHIFT)

#define REPLAY_RESIDENCY_REVISION_MASK         (0x3 << REPLAY_RESIDENCY_REVISION_SHIFT)
# define REPLAY_RESIDENCY_REVISION_0           (0x0 << REPLAY_RESIDENCY_REVISION_SHIFT)
# define REPLAY_RESIDENCY_REVISION_1           (0x1 << REPLAY_RESIDENCY_REVISION_SHIFT)

/**
 * Definition of a replay_state.
 */
enum replay_state {
	REPLAY_STATE_0			= 0x0,
	REPLAY_STATE_1			= 0x10,
	REPLAY_STATE_1A			= 0x11,
	REPLAY_STATE_2			= 0x20,
	REPLAY_STATE_2A			= 0x21,
	REPLAY_STATE_3			= 0x30,
	REPLAY_STATE_3INIT		= 0x31,
	REPLAY_STATE_4			= 0x40,
	REPLAY_STATE_4A			= 0x41,
	REPLAY_STATE_4B			= 0x42,
	REPLAY_STATE_4C			= 0x43,
	REPLAY_STATE_4D			= 0x44,
	REPLAY_STATE_4E			= 0x45,
	REPLAY_STATE_4B_LOCKED		= 0x4A,
	REPLAY_STATE_4C_UNLOCKED	= 0x4B,
	REPLAY_STATE_5			= 0x50,
	REPLAY_STATE_5A			= 0x51,
	REPLAY_STATE_5B			= 0x52,
	REPLAY_STATE_5A_LOCKED		= 0x5A,
	REPLAY_STATE_5B_UNLOCKED	= 0x5B,
	REPLAY_STATE_6			= 0x60,
	REPLAY_STATE_6A			= 0x61,
	REPLAY_STATE_6B			= 0x62,
	REPLAY_STATE_INVALID		= 0xFF,
};

/**
 * Replay command sub-types.
 */
enum dmub_cmd_replay_type {
	/**
	 * Copy driver-calculated parameters to REPLAY state.
	 */
	DMUB_CMD__REPLAY_COPY_SETTINGS		= 0,
	/**
	 * Enable REPLAY.
	 */
	DMUB_CMD__REPLAY_ENABLE			= 1,
	/**
	 * Set Replay power option.
	 */
	DMUB_CMD__SET_REPLAY_POWER_OPT		= 2,
	/**
	 * Set coasting vtotal.
	 */
	DMUB_CMD__REPLAY_SET_COASTING_VTOTAL	= 3,
	/**
	 * Set power opt and coasting vtotal.
	 */
	DMUB_CMD__REPLAY_SET_POWER_OPT_AND_COASTING_VTOTAL	= 4,
	/**
	 * Set disabled iiming sync.
	 */
	DMUB_CMD__REPLAY_SET_TIMING_SYNC_SUPPORTED	= 5,
	/**
	 * Set Residency Frameupdate Timer.
	 */
	DMUB_CMD__REPLAY_SET_RESIDENCY_FRAMEUPDATE_TIMER = 6,
	/**
	 * Set pseudo vtotal
	 */
	DMUB_CMD__REPLAY_SET_PSEUDO_VTOTAL = 7,
	/**
	 * Set adaptive sync sdp enabled
	 */
	DMUB_CMD__REPLAY_DISABLED_ADAPTIVE_SYNC_SDP = 8,
	/**
	 * Set Replay General command.
	 */
	DMUB_CMD__REPLAY_SET_GENERAL_CMD = 16,
};

/**
 * Replay general command sub-types.
 */
enum dmub_cmd_replay_general_subtype {
	REPLAY_GENERAL_CMD_NOT_SUPPORTED = -1,
	/**
	 * TODO: For backward compatible, allow new command only.
	 * REPLAY_GENERAL_CMD_SET_TIMING_SYNC_SUPPORTED,
	 * REPLAY_GENERAL_CMD_SET_RESIDENCY_FRAMEUPDATE_TIMER,
	 * REPLAY_GENERAL_CMD_SET_PSEUDO_VTOTAL,
	 */
	REPLAY_GENERAL_CMD_DISABLED_ADAPTIVE_SYNC_SDP,
	REPLAY_GENERAL_CMD_DISABLED_DESYNC_ERROR_DETECTION,
};

/**
 * Data passed from driver to FW in a DMUB_CMD__REPLAY_COPY_SETTINGS command.
 */
struct dmub_cmd_replay_copy_settings_data {
	/**
	 * Flags that can be set by driver to change some replay behaviour.
	 */
	union replay_debug_flags debug;

	/**
	 * @flags: Flags used to determine feature functionality.
	 */
	union replay_hw_flags flags;

	/**
	 * DPP HW instance.
	 */
	uint8_t dpp_inst;
	/**
	 * OTG HW instance.
	 */
	uint8_t otg_inst;
	/**
	 * DIG FE HW instance.
	 */
	uint8_t digfe_inst;
	/**
	 * DIG BE HW instance.
	 */
	uint8_t digbe_inst;
	/**
	 * AUX HW instance.
	 */
	uint8_t aux_inst;
	/**
	 * Panel Instance.
	 * Panel isntance to identify which psr_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * @pixel_deviation_per_line: Indicate the maximum pixel deviation per line compare
	 * to Source timing when Sink maintains coasting vtotal during the Replay normal sleep mode
	 */
	uint8_t pixel_deviation_per_line;
	/**
	 * @max_deviation_line: The max number of deviation line that can keep the timing
	 * synchronized between the Source and Sink during Replay normal sleep mode.
	 */
	uint8_t max_deviation_line;
	/**
	 * Length of each horizontal line in ns.
	 */
	uint32_t line_time_in_ns;
	/**
	 * PHY instance.
	 */
	uint8_t dpphy_inst;
	/**
	 * Determines if SMU optimzations are enabled/disabled.
	 */
	uint8_t smu_optimizations_en;
	/**
	 * Determines if timing sync are enabled/disabled.
	 */
	uint8_t replay_timing_sync_supported;
	/*
	 * Use FSM state for Replay power up/down
	 */
	uint8_t use_phy_fsm;
};

/**
 * Definition of a DMUB_CMD__REPLAY_COPY_SETTINGS command.
 */
struct dmub_rb_cmd_replay_copy_settings {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__REPLAY_COPY_SETTINGS command.
	 */
	struct dmub_cmd_replay_copy_settings_data replay_copy_settings_data;
};

/**
 * Replay disable / enable state for dmub_rb_cmd_replay_enable_data.enable
 */
enum replay_enable {
	/**
	 * Disable REPLAY.
	 */
	REPLAY_DISABLE				= 0,
	/**
	 * Enable REPLAY.
	 */
	REPLAY_ENABLE				= 1,
};

/**
 * Data passed from driver to FW in a DMUB_CMD__REPLAY_ENABLE command.
 */
struct dmub_rb_cmd_replay_enable_data {
	/**
	 * Replay enable or disable.
	 */
	uint8_t enable;
	/**
	 * Panel Instance.
	 * Panel isntance to identify which replay_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * Phy state to enter.
	 * Values to use are defined in dmub_phy_fsm_state
	 */
	uint8_t phy_fsm_state;
	/**
	 * Phy rate for DP - RBR/HBR/HBR2/HBR3.
	 * Set this using enum phy_link_rate.
	 * This does not support HDMI/DP2 for now.
	 */
	uint8_t phy_rate;
};

/**
 * Definition of a DMUB_CMD__REPLAY_ENABLE command.
 * Replay enable/disable is controlled using action in data.
 */
struct dmub_rb_cmd_replay_enable {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	struct dmub_rb_cmd_replay_enable_data data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__SET_REPLAY_POWER_OPT command.
 */
struct dmub_cmd_replay_set_power_opt_data {
	/**
	 * Panel Instance.
	 * Panel isntance to identify which replay_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[3];
	/**
	 * REPLAY power option
	 */
	uint32_t power_opt;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__REPLAY_SET_TIMING_SYNC_SUPPORTED command.
 */
struct dmub_cmd_replay_set_timing_sync_data {
	/**
	 * Panel Instance.
	 * Panel isntance to identify which replay_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * REPLAY set_timing_sync
	 */
	uint8_t timing_sync_supported;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
};

/**
 * Data passed from driver to FW in a DMUB_CMD__REPLAY_SET_PSEUDO_VTOTAL command.
 */
struct dmub_cmd_replay_set_pseudo_vtotal {
	/**
	 * Panel Instance.
	 * Panel isntance to identify which replay_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * Source Vtotal that Replay + IPS + ABM full screen video src vtotal
	 */
	uint16_t vtotal;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad;
};
struct dmub_cmd_replay_disabled_adaptive_sync_sdp_data {
	/**
	 * Panel Instance.
	 * Panel isntance to identify which replay_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * enabled: set adaptive sync sdp enabled
	 */
	uint8_t force_disabled;

	uint8_t pad[2];
};
struct dmub_cmd_replay_set_general_cmd_data {
	/**
	 * Panel Instance.
	 * Panel isntance to identify which replay_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * subtype: replay general cmd sub type
	 */
	uint8_t subtype;

	uint8_t pad[2];
	/**
	 * config data with param1 and param2
	 */
	uint32_t param1;

	uint32_t param2;
};

/**
 * Definition of a DMUB_CMD__SET_REPLAY_POWER_OPT command.
 */
struct dmub_rb_cmd_replay_set_power_opt {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of a DMUB_CMD__SET_REPLAY_POWER_OPT command.
	 */
	struct dmub_cmd_replay_set_power_opt_data replay_set_power_opt_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__REPLAY_SET_COASTING_VTOTAL command.
 */
struct dmub_cmd_replay_set_coasting_vtotal_data {
	/**
	 * 16-bit value dicated by driver that indicates the coasting vtotal.
	 */
	uint16_t coasting_vtotal;
	/**
	 * REPLAY control version.
	 */
	uint8_t cmd_version;
	/**
	 * Panel Instance.
	 * Panel isntance to identify which replay_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * 16-bit value dicated by driver that indicates the coasting vtotal high byte part.
	 */
	uint16_t coasting_vtotal_high;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
};

/**
 * Definition of a DMUB_CMD__REPLAY_SET_COASTING_VTOTAL command.
 */
struct dmub_rb_cmd_replay_set_coasting_vtotal {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of a DMUB_CMD__REPLAY_SET_COASTING_VTOTAL command.
	 */
	struct dmub_cmd_replay_set_coasting_vtotal_data replay_set_coasting_vtotal_data;
};

/**
 * Definition of a DMUB_CMD__REPLAY_SET_POWER_OPT_AND_COASTING_VTOTAL command.
 */
struct dmub_rb_cmd_replay_set_power_opt_and_coasting_vtotal {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of a DMUB_CMD__SET_REPLAY_POWER_OPT command.
	 */
	struct dmub_cmd_replay_set_power_opt_data replay_set_power_opt_data;
	/**
	 * Definition of a DMUB_CMD__REPLAY_SET_COASTING_VTOTAL command.
	 */
	struct dmub_cmd_replay_set_coasting_vtotal_data replay_set_coasting_vtotal_data;
};

/**
 * Definition of a DMUB_CMD__REPLAY_SET_TIMING_SYNC_SUPPORTED command.
 */
struct dmub_rb_cmd_replay_set_timing_sync {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of DMUB_CMD__REPLAY_SET_TIMING_SYNC_SUPPORTED command.
	 */
	struct dmub_cmd_replay_set_timing_sync_data replay_set_timing_sync_data;
};

/**
 * Definition of a DMUB_CMD__REPLAY_SET_PSEUDO_VTOTAL command.
 */
struct dmub_rb_cmd_replay_set_pseudo_vtotal {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of DMUB_CMD__REPLAY_SET_PSEUDO_VTOTAL command.
	 */
	struct dmub_cmd_replay_set_pseudo_vtotal data;
};

/**
 * Definition of a DMUB_CMD__REPLAY_DISABLED_ADAPTIVE_SYNC_SDP command.
 */
struct dmub_rb_cmd_replay_disabled_adaptive_sync_sdp {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of DMUB_CMD__REPLAY_DISABLED_ADAPTIVE_SYNC_SDP command.
	 */
	struct dmub_cmd_replay_disabled_adaptive_sync_sdp_data data;
};

/**
 * Definition of a DMUB_CMD__REPLAY_SET_GENERAL_CMD command.
 */
struct dmub_rb_cmd_replay_set_general_cmd {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of DMUB_CMD__REPLAY_SET_GENERAL_CMD command.
	 */
	struct dmub_cmd_replay_set_general_cmd_data data;
};

/**
 * Data passed from driver to FW in  DMUB_CMD__REPLAY_SET_RESIDENCY_FRAMEUPDATE_TIMER command.
 */
struct dmub_cmd_replay_frameupdate_timer_data {
	/**
	 * Panel Instance.
	 * Panel isntance to identify which replay_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * Replay Frameupdate Timer Enable or not
	 */
	uint8_t enable;
	/**
	 * REPLAY force reflash frame update number
	 */
	uint16_t frameupdate_count;
};
/**
 * Definition of DMUB_CMD__REPLAY_SET_RESIDENCY_FRAMEUPDATE_TIMER
 */
struct dmub_rb_cmd_replay_set_frameupdate_timer {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of a DMUB_CMD__SET_REPLAY_POWER_OPT command.
	 */
	struct dmub_cmd_replay_frameupdate_timer_data data;
};

/**
 * Definition union of replay command set
 */
union dmub_replay_cmd_set {
	/**
	 * Panel Instance.
	 * Panel isntance to identify which replay_state to use
	 * Currently the support is only for 0 or 1
	 */
	uint8_t panel_inst;
	/**
	 * Definition of DMUB_CMD__REPLAY_SET_TIMING_SYNC_SUPPORTED command data.
	 */
	struct dmub_cmd_replay_set_timing_sync_data sync_data;
	/**
	 * Definition of DMUB_CMD__REPLAY_SET_RESIDENCY_FRAMEUPDATE_TIMER command data.
	 */
	struct dmub_cmd_replay_frameupdate_timer_data timer_data;
	/**
	 * Definition of DMUB_CMD__REPLAY_SET_PSEUDO_VTOTAL command data.
	 */
	struct dmub_cmd_replay_set_pseudo_vtotal pseudo_vtotal_data;
	/**
	 * Definition of DMUB_CMD__REPLAY_DISABLED_ADAPTIVE_SYNC_SDP command data.
	 */
	struct dmub_cmd_replay_disabled_adaptive_sync_sdp_data disabled_adaptive_sync_sdp_data;
	/**
	 * Definition of DMUB_CMD__REPLAY_SET_GENERAL_CMD command data.
	 */
	struct dmub_cmd_replay_set_general_cmd_data set_general_cmd_data;
};

/**
 * Set of HW components that can be locked.
 *
 * Note: If updating with more HW components, fields
 * in dmub_inbox0_cmd_lock_hw must be updated to match.
 */
union dmub_hw_lock_flags {
	/**
	 * Set of HW components that can be locked.
	 */
	struct {
		/**
		 * Lock/unlock OTG master update lock.
		 */
		uint8_t lock_pipe   : 1;
		/**
		 * Lock/unlock cursor.
		 */
		uint8_t lock_cursor : 1;
		/**
		 * Lock/unlock global update lock.
		 */
		uint8_t lock_dig    : 1;
		/**
		 * Triple buffer lock requires additional hw programming to usual OTG master lock.
		 */
		uint8_t triple_buffer_lock : 1;
	} bits;

	/**
	 * Union for HW Lock flags.
	 */
	uint8_t u8All;
};

/**
 * Instances of HW to be locked.
 *
 * Note: If updating with more HW components, fields
 * in dmub_inbox0_cmd_lock_hw must be updated to match.
 */
struct dmub_hw_lock_inst_flags {
	/**
	 * OTG HW instance for OTG master update lock.
	 */
	uint8_t otg_inst;
	/**
	 * OPP instance for cursor lock.
	 */
	uint8_t opp_inst;
	/**
	 * OTG HW instance for global update lock.
	 * TODO: Remove, and re-use otg_inst.
	 */
	uint8_t dig_inst;
	/**
	 * Explicit pad to 4 byte boundary.
	 */
	uint8_t pad;
};

/**
 * Clients that can acquire the HW Lock Manager.
 *
 * Note: If updating with more clients, fields in
 * dmub_inbox0_cmd_lock_hw must be updated to match.
 */
enum hw_lock_client {
	/**
	 * Driver is the client of HW Lock Manager.
	 */
	HW_LOCK_CLIENT_DRIVER = 0,
	/**
	 * PSR SU is the client of HW Lock Manager.
	 */
	HW_LOCK_CLIENT_PSR_SU		= 1,
	HW_LOCK_CLIENT_SUBVP = 3,
	/**
	 * Replay is the client of HW Lock Manager.
	 */
	HW_LOCK_CLIENT_REPLAY		= 4,
	HW_LOCK_CLIENT_FAMS2 = 5,
	/**
	 * Invalid client.
	 */
	HW_LOCK_CLIENT_INVALID = 0xFFFFFFFF,
};

/**
 * Data passed to HW Lock Mgr in a DMUB_CMD__HW_LOCK command.
 */
struct dmub_cmd_lock_hw_data {
	/**
	 * Specifies the client accessing HW Lock Manager.
	 */
	enum hw_lock_client client;
	/**
	 * HW instances to be locked.
	 */
	struct dmub_hw_lock_inst_flags inst_flags;
	/**
	 * Which components to be locked.
	 */
	union dmub_hw_lock_flags hw_locks;
	/**
	 * Specifies lock/unlock.
	 */
	uint8_t lock;
	/**
	 * HW can be unlocked separately from releasing the HW Lock Mgr.
	 * This flag is set if the client wishes to release the object.
	 */
	uint8_t should_release;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad;
};

/**
 * Definition of a DMUB_CMD__HW_LOCK command.
 * Command is used by driver and FW.
 */
struct dmub_rb_cmd_lock_hw {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed to HW Lock Mgr in a DMUB_CMD__HW_LOCK command.
	 */
	struct dmub_cmd_lock_hw_data lock_hw_data;
};

/**
 * ABM command sub-types.
 */
enum dmub_cmd_abm_type {
	/**
	 * Initialize parameters for ABM algorithm.
	 * Data is passed through an indirect buffer.
	 */
	DMUB_CMD__ABM_INIT_CONFIG	= 0,
	/**
	 * Set OTG and panel HW instance.
	 */
	DMUB_CMD__ABM_SET_PIPE		= 1,
	/**
	 * Set user requested backklight level.
	 */
	DMUB_CMD__ABM_SET_BACKLIGHT	= 2,
	/**
	 * Set ABM operating/aggression level.
	 */
	DMUB_CMD__ABM_SET_LEVEL		= 3,
	/**
	 * Set ambient light level.
	 */
	DMUB_CMD__ABM_SET_AMBIENT_LEVEL	= 4,
	/**
	 * Enable/disable fractional duty cycle for backlight PWM.
	 */
	DMUB_CMD__ABM_SET_PWM_FRAC	= 5,

	/**
	 * unregister vertical interrupt after steady state is reached
	 */
	DMUB_CMD__ABM_PAUSE	= 6,

	/**
	 * Save and Restore ABM state. On save we save parameters, and
	 * on restore we update state with passed in data.
	 */
	DMUB_CMD__ABM_SAVE_RESTORE	= 7,

	/**
	 * Query ABM caps.
	 */
	DMUB_CMD__ABM_QUERY_CAPS	= 8,

	/**
	 * Set ABM Events
	 */
	DMUB_CMD__ABM_SET_EVENT	= 9,

	/**
	 * Get the current ACE curve.
	 */
	DMUB_CMD__ABM_GET_ACE_CURVE = 10,
};

struct abm_ace_curve {
	/**
	 * @offsets: ACE curve offsets.
	 */
	uint32_t offsets[ABM_MAX_NUM_OF_ACE_SEGMENTS];

	/**
	 * @thresholds: ACE curve thresholds.
	 */
	uint32_t thresholds[ABM_MAX_NUM_OF_ACE_SEGMENTS];

	/**
	 * @slopes: ACE curve slopes.
	 */
	uint32_t slopes[ABM_MAX_NUM_OF_ACE_SEGMENTS];
};

struct fixed_pt_format {
	/**
	 * @sign_bit: Indicates whether one bit is reserved for the sign.
	 */
	bool sign_bit;

	/**
	 * @num_int_bits: Number of bits used for integer part.
	 */
	uint8_t num_int_bits;

	/**
	 * @num_frac_bits: Number of bits used for fractional part.
	 */
	uint8_t num_frac_bits;

	/**
	 * @pad: Explicit padding to 4 byte boundary.
	 */
	uint8_t pad;
};

struct abm_caps {
	/**
	 * @num_hg_bins: Number of histogram bins.
	 */
	uint8_t num_hg_bins;

	/**
	 * @num_ace_segments: Number of ACE curve segments.
	 */
	uint8_t num_ace_segments;

	/**
	 * @pad: Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];

	/**
	 * @ace_thresholds_format: Format of the ACE thresholds. If not programmable, it is set to 0.
	 */
	struct fixed_pt_format ace_thresholds_format;

	/**
	 * @ace_offsets_format: Format of the ACE offsets. If not programmable, it is set to 0.
	 */
	struct fixed_pt_format ace_offsets_format;

	/**
	 * @ace_slopes_format: Format of the ACE slopes.
	 */
	struct fixed_pt_format ace_slopes_format;
};

/**
 * Parameters for ABM2.4 algorithm. Passed from driver to FW via an indirect buffer.
 * Requirements:
 *  - Padded explicitly to 32-bit boundary.
 *  - Must ensure this structure matches the one on driver-side,
 *    otherwise it won't be aligned.
 */
struct abm_config_table {
	/**
	 * Gamma curve thresholds, used for crgb conversion.
	 */
	uint16_t crgb_thresh[NUM_POWER_FN_SEGS];                 // 0B
	/**
	 * Gamma curve offsets, used for crgb conversion.
	 */
	uint16_t crgb_offset[NUM_POWER_FN_SEGS];                 // 16B
	/**
	 * Gamma curve slopes, used for crgb conversion.
	 */
	uint16_t crgb_slope[NUM_POWER_FN_SEGS];                  // 32B
	/**
	 * Custom backlight curve thresholds.
	 */
	uint16_t backlight_thresholds[NUM_BL_CURVE_SEGS];        // 48B
	/**
	 * Custom backlight curve offsets.
	 */
	uint16_t backlight_offsets[NUM_BL_CURVE_SEGS];           // 78B
	/**
	 * Ambient light thresholds.
	 */
	uint16_t ambient_thresholds_lux[NUM_AMBI_LEVEL];         // 112B
	/**
	 * Minimum programmable backlight.
	 */
	uint16_t min_abm_backlight;                              // 122B
	/**
	 * Minimum reduction values.
	 */
	uint8_t min_reduction[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 124B
	/**
	 * Maximum reduction values.
	 */
	uint8_t max_reduction[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 144B
	/**
	 * Bright positive gain.
	 */
	uint8_t bright_pos_gain[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL]; // 164B
	/**
	 * Dark negative gain.
	 */
	uint8_t dark_pos_gain[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 184B
	/**
	 * Hybrid factor.
	 */
	uint8_t hybrid_factor[NUM_AGGR_LEVEL];                   // 204B
	/**
	 * Contrast factor.
	 */
	uint8_t contrast_factor[NUM_AGGR_LEVEL];                 // 208B
	/**
	 * Deviation gain.
	 */
	uint8_t deviation_gain[NUM_AGGR_LEVEL];                  // 212B
	/**
	 * Minimum knee.
	 */
	uint8_t min_knee[NUM_AGGR_LEVEL];                        // 216B
	/**
	 * Maximum knee.
	 */
	uint8_t max_knee[NUM_AGGR_LEVEL];                        // 220B
	/**
	 * Unused.
	 */
	uint8_t iir_curve[NUM_AMBI_LEVEL];                       // 224B
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad3[3];                                         // 229B
	/**
	 * Backlight ramp reduction.
	 */
	uint16_t blRampReduction[NUM_AGGR_LEVEL];                // 232B
	/**
	 * Backlight ramp start.
	 */
	uint16_t blRampStart[NUM_AGGR_LEVEL];                    // 240B
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_PIPE command.
 */
struct dmub_cmd_abm_set_pipe_data {
	/**
	 * OTG HW instance.
	 */
	uint8_t otg_inst;

	/**
	 * Panel Control HW instance.
	 */
	uint8_t panel_inst;

	/**
	 * Controls how ABM will interpret a set pipe or set level command.
	 */
	uint8_t set_pipe_option;

	/**
	 * Unused.
	 * TODO: Remove.
	 */
	uint8_t ramping_boundary;

	/**
	 * PwrSeq HW Instance.
	 */
	uint8_t pwrseq_inst;

	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[3];
};

/**
 * Definition of a DMUB_CMD__ABM_SET_PIPE command.
 */
struct dmub_rb_cmd_abm_set_pipe {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_PIPE command.
	 */
	struct dmub_cmd_abm_set_pipe_data abm_set_pipe_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_BACKLIGHT command.
 */
struct dmub_cmd_abm_set_backlight_data {
	/**
	 * Number of frames to ramp to backlight user level.
	 */
	uint32_t frame_ramp;

	/**
	 * Requested backlight level from user.
	 */
	uint32_t backlight_user_level;

	/**
	 * ABM control version.
	 */
	uint8_t version;

	/**
	 * Panel Control HW instance mask.
	 * Bit 0 is Panel Control HW instance 0.
	 * Bit 1 is Panel Control HW instance 1.
	 */
	uint8_t panel_mask;

	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
};

/**
 * Definition of a DMUB_CMD__ABM_SET_BACKLIGHT command.
 */
struct dmub_rb_cmd_abm_set_backlight {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_BACKLIGHT command.
	 */
	struct dmub_cmd_abm_set_backlight_data abm_set_backlight_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_LEVEL command.
 */
struct dmub_cmd_abm_set_level_data {
	/**
	 * Set current ABM operating/aggression level.
	 */
	uint32_t level;

	/**
	 * ABM control version.
	 */
	uint8_t version;

	/**
	 * Panel Control HW instance mask.
	 * Bit 0 is Panel Control HW instance 0.
	 * Bit 1 is Panel Control HW instance 1.
	 */
	uint8_t panel_mask;

	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
};

/**
 * Definition of a DMUB_CMD__ABM_SET_LEVEL command.
 */
struct dmub_rb_cmd_abm_set_level {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_LEVEL command.
	 */
	struct dmub_cmd_abm_set_level_data abm_set_level_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_AMBIENT_LEVEL command.
 */
struct dmub_cmd_abm_set_ambient_level_data {
	/**
	 * Ambient light sensor reading from OS.
	 */
	uint32_t ambient_lux;

	/**
	 * ABM control version.
	 */
	uint8_t version;

	/**
	 * Panel Control HW instance mask.
	 * Bit 0 is Panel Control HW instance 0.
	 * Bit 1 is Panel Control HW instance 1.
	 */
	uint8_t panel_mask;

	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
};

/**
 * Definition of a DMUB_CMD__ABM_SET_AMBIENT_LEVEL command.
 */
struct dmub_rb_cmd_abm_set_ambient_level {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_AMBIENT_LEVEL command.
	 */
	struct dmub_cmd_abm_set_ambient_level_data abm_set_ambient_level_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_PWM_FRAC command.
 */
struct dmub_cmd_abm_set_pwm_frac_data {
	/**
	 * Enable/disable fractional duty cycle for backlight PWM.
	 * TODO: Convert to uint8_t.
	 */
	uint32_t fractional_pwm;

	/**
	 * ABM control version.
	 */
	uint8_t version;

	/**
	 * Panel Control HW instance mask.
	 * Bit 0 is Panel Control HW instance 0.
	 * Bit 1 is Panel Control HW instance 1.
	 */
	uint8_t panel_mask;

	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
};

/**
 * Definition of a DMUB_CMD__ABM_SET_PWM_FRAC command.
 */
struct dmub_rb_cmd_abm_set_pwm_frac {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_PWM_FRAC command.
	 */
	struct dmub_cmd_abm_set_pwm_frac_data abm_set_pwm_frac_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_INIT_CONFIG command.
 */
struct dmub_cmd_abm_init_config_data {
	/**
	 * Location of indirect buffer used to pass init data to ABM.
	 */
	union dmub_addr src;

	/**
	 * Indirect buffer length.
	 */
	uint16_t bytes;


	/**
	 * ABM control version.
	 */
	uint8_t version;

	/**
	 * Panel Control HW instance mask.
	 * Bit 0 is Panel Control HW instance 0.
	 * Bit 1 is Panel Control HW instance 1.
	 */
	uint8_t panel_mask;

	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
};

/**
 * Definition of a DMUB_CMD__ABM_INIT_CONFIG command.
 */
struct dmub_rb_cmd_abm_init_config {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_INIT_CONFIG command.
	 */
	struct dmub_cmd_abm_init_config_data abm_init_config_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_PAUSE command.
 */

struct dmub_cmd_abm_pause_data {

	/**
	 * Panel Control HW instance mask.
	 * Bit 0 is Panel Control HW instance 0.
	 * Bit 1 is Panel Control HW instance 1.
	 */
	uint8_t panel_mask;

	/**
	 * OTG hw instance
	 */
	uint8_t otg_inst;

	/**
	 * Enable or disable ABM pause
	 */
	uint8_t enable;

	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[1];
};

/**
 * Definition of a DMUB_CMD__ABM_PAUSE command.
 */
struct dmub_rb_cmd_abm_pause {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_PAUSE command.
	 */
	struct dmub_cmd_abm_pause_data abm_pause_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_QUERY_CAPS command.
 */
struct dmub_cmd_abm_query_caps_in {
	/**
	 * Panel instance.
	 */
	uint8_t panel_inst;

	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[3];
};

/**
 * Data passed from FW to driver in a DMUB_CMD__ABM_QUERY_CAPS command.
 */
struct dmub_cmd_abm_query_caps_out {
	/**
	 * SW Algorithm caps.
	 */
	struct abm_caps sw_caps;

	/**
	 * ABM HW caps.
	 */
	struct abm_caps hw_caps;
};

/**
 * Definition of a DMUB_CMD__ABM_QUERY_CAPS command.
 */
struct dmub_rb_cmd_abm_query_caps {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed between FW and driver in a DMUB_CMD__ABM_QUERY_CAPS command.
	 */
	union {
		struct dmub_cmd_abm_query_caps_in  abm_query_caps_in;
		struct dmub_cmd_abm_query_caps_out abm_query_caps_out;
	} data;
};

/**
 * enum dmub_abm_ace_curve_type - ACE curve type.
 */
enum dmub_abm_ace_curve_type {
	/**
	 * ACE curve as defined by the SW layer.
	 */
	ABM_ACE_CURVE_TYPE__SW = 0,
	/**
	 * ACE curve as defined by the SW to HW translation interface layer.
	 */
	ABM_ACE_CURVE_TYPE__SW_IF = 1,
};

/**
 * Definition of a DMUB_CMD__ABM_GET_ACE_CURVE command.
 */
struct dmub_rb_cmd_abm_get_ace_curve {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Address where ACE curve should be copied.
	 */
	union dmub_addr dest;

	/**
	 * Type of ACE curve being queried.
	 */
	enum dmub_abm_ace_curve_type ace_type;

	/**
	 * Indirect buffer length.
	 */
	uint16_t bytes;

	/**
	 * eDP panel instance.
	 */
	uint8_t panel_inst;

	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad;
};

/**
 * Definition of a DMUB_CMD__ABM_SAVE_RESTORE command.
 */
struct dmub_rb_cmd_abm_save_restore {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * OTG hw instance
	 */
	uint8_t otg_inst;

	/**
	 * Enable or disable ABM pause
	 */
	uint8_t freeze;

	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t debug;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_INIT_CONFIG command.
	 */
	struct dmub_cmd_abm_init_config_data abm_init_config_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_EVENT command.
 */

struct dmub_cmd_abm_set_event_data {

	/**
	 * VB Scaling Init. Strength Mapping
	 * Byte 0: 0~255 for VB level 0
	 * Byte 1: 0~255 for VB level 1
	 * Byte 2: 0~255 for VB level 2
	 * Byte 3: 0~255 for VB level 3
	 */
	uint32_t vb_scaling_strength_mapping;
	/**
	 * VariBright Scaling Enable
	 */
	uint8_t vb_scaling_enable;
	/**
	 * Panel Control HW instance mask.
	 * Bit 0 is Panel Control HW instance 0.
	 * Bit 1 is Panel Control HW instance 1.
	 */
	uint8_t panel_mask;

	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
};

/**
 * Definition of a DMUB_CMD__ABM_SET_EVENT command.
 */
struct dmub_rb_cmd_abm_set_event {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_EVENT command.
	 */
	struct dmub_cmd_abm_set_event_data abm_set_event_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__QUERY_FEATURE_CAPS command.
 */
struct dmub_cmd_query_feature_caps_data {
	/**
	 * DMUB feature capabilities.
	 * After DMUB init, driver will query FW capabilities prior to enabling certain features.
	 */
	struct dmub_feature_caps feature_caps;
};

/**
 * Definition of a DMUB_CMD__QUERY_FEATURE_CAPS command.
 */
struct dmub_rb_cmd_query_feature_caps {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__QUERY_FEATURE_CAPS command.
	 */
	struct dmub_cmd_query_feature_caps_data query_feature_caps_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__GET_VISUAL_CONFIRM_COLOR command.
 */
struct dmub_cmd_visual_confirm_color_data {
	/**
	 * DMUB visual confirm color
	 */
	struct dmub_visual_confirm_color visual_confirm_color;
};

/**
 * Definition of a DMUB_CMD__GET_VISUAL_CONFIRM_COLOR command.
 */
struct dmub_rb_cmd_get_visual_confirm_color {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__GET_VISUAL_CONFIRM_COLOR command.
	 */
	struct dmub_cmd_visual_confirm_color_data visual_confirm_color_data;
};

/**
 * enum dmub_cmd_panel_cntl_type - Panel control command.
 */
enum dmub_cmd_panel_cntl_type {
	/**
	 * Initializes embedded panel hardware blocks.
	 */
	DMUB_CMD__PANEL_CNTL_HW_INIT = 0,
	/**
	 * Queries backlight info for the embedded panel.
	 */
	DMUB_CMD__PANEL_CNTL_QUERY_BACKLIGHT_INFO = 1,
	/**
	 * Sets the PWM Freq as per user's requirement.
	 */
	DMUB_CMD__PANEL_DEBUG_PWM_FREQ = 2,
};

/**
 * struct dmub_cmd_panel_cntl_data - Panel control data.
 */
struct dmub_cmd_panel_cntl_data {
	uint32_t pwrseq_inst; /**< pwrseq instance */
	uint32_t current_backlight; /* in/out */
	uint32_t bl_pwm_cntl; /* in/out */
	uint32_t bl_pwm_period_cntl; /* in/out */
	uint32_t bl_pwm_ref_div1; /* in/out */
	uint8_t is_backlight_on : 1; /* in/out */
	uint8_t is_powered_on : 1; /* in/out */
	uint8_t padding[3];
	uint32_t bl_pwm_ref_div2; /* in/out */
	uint8_t reserved[4];
};

/**
 * struct dmub_rb_cmd_panel_cntl - Panel control command.
 */
struct dmub_rb_cmd_panel_cntl {
	struct dmub_cmd_header header; /**< header */
	struct dmub_cmd_panel_cntl_data data; /**< payload */
};

struct dmub_optc_state {
	uint32_t v_total_max;
	uint32_t v_total_min;
	uint32_t tg_inst;
};

struct dmub_rb_cmd_drr_update {
	struct dmub_cmd_header header;
	struct dmub_optc_state dmub_optc_state_req;
};

struct dmub_cmd_fw_assisted_mclk_switch_pipe_data {
	uint32_t pix_clk_100hz;
	uint8_t max_ramp_step;
	uint8_t pipes;
	uint8_t min_refresh_in_hz;
	uint8_t pipe_count;
	uint8_t pipe_index[4];
};

struct dmub_cmd_fw_assisted_mclk_switch_config {
	uint8_t fams_enabled;
	uint8_t visual_confirm_enabled;
	uint16_t vactive_stretch_margin_us; // Extra vblank stretch required when doing FPO + Vactive
	struct dmub_cmd_fw_assisted_mclk_switch_pipe_data pipe_data[DMUB_MAX_FPO_STREAMS];
};

struct dmub_rb_cmd_fw_assisted_mclk_switch {
	struct dmub_cmd_header header;
	struct dmub_cmd_fw_assisted_mclk_switch_config config_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__VBIOS_LVTMA_CONTROL command.
 */
struct dmub_cmd_lvtma_control_data {
	uint8_t uc_pwr_action; /**< LVTMA_ACTION */
	uint8_t bypass_panel_control_wait;
	uint8_t reserved_0[2]; /**< For future use */
	uint8_t pwrseq_inst; /**< LVTMA control instance */
	uint8_t reserved_1[3]; /**< For future use */
};

/**
 * Definition of a DMUB_CMD__VBIOS_LVTMA_CONTROL command.
 */
struct dmub_rb_cmd_lvtma_control {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__VBIOS_LVTMA_CONTROL command.
	 */
	struct dmub_cmd_lvtma_control_data data;
};

/**
 * Data passed in/out in a DMUB_CMD__VBIOS_TRANSMITTER_QUERY_DP_ALT command.
 */
struct dmub_rb_cmd_transmitter_query_dp_alt_data {
	uint8_t phy_id; /**< 0=UNIPHYA, 1=UNIPHYB, 2=UNIPHYC, 3=UNIPHYD, 4=UNIPHYE, 5=UNIPHYF */
	uint8_t is_usb; /**< is phy is usb */
	uint8_t is_dp_alt_disable; /**< is dp alt disable */
	uint8_t is_dp4; /**< is dp in 4 lane */
};

/**
 * Definition of a DMUB_CMD__VBIOS_TRANSMITTER_QUERY_DP_ALT command.
 */
struct dmub_rb_cmd_transmitter_query_dp_alt {
	struct dmub_cmd_header header; /**< header */
	struct dmub_rb_cmd_transmitter_query_dp_alt_data data; /**< payload */
};

struct phy_test_mode {
	uint8_t mode;
	uint8_t pat0;
	uint8_t pad[2];
};

/**
 * Data passed in/out in a DMUB_CMD__VBIOS_TRANSMITTER_SET_PHY_FSM command.
 */
struct dmub_rb_cmd_transmitter_set_phy_fsm_data {
	uint8_t phy_id; /**< 0=UNIPHYA, 1=UNIPHYB, 2=UNIPHYC, 3=UNIPHYD, 4=UNIPHYE, 5=UNIPHYF */
	uint8_t mode; /**< HDMI/DP/DP2 etc */
	uint8_t lane_num; /**< Number of lanes */
	uint32_t symclk_100Hz; /**< PLL symclock in 100hz */
	struct phy_test_mode test_mode;
	enum dmub_phy_fsm_state state;
	uint32_t status;
	uint8_t pad;
};

/**
 * Definition of a DMUB_CMD__VBIOS_TRANSMITTER_SET_PHY_FSM command.
 */
struct dmub_rb_cmd_transmitter_set_phy_fsm {
	struct dmub_cmd_header header; /**< header */
	struct dmub_rb_cmd_transmitter_set_phy_fsm_data data; /**< payload */
};

/**
 * Maximum number of bytes a chunk sent to DMUB for parsing
 */
#define DMUB_EDID_CEA_DATA_CHUNK_BYTES 8

/**
 *  Represent a chunk of CEA blocks sent to DMUB for parsing
 */
struct dmub_cmd_send_edid_cea {
	uint16_t offset;	/**< offset into the CEA block */
	uint8_t length;	/**< number of bytes in payload to copy as part of CEA block */
	uint16_t cea_total_length;  /**< total length of the CEA block */
	uint8_t payload[DMUB_EDID_CEA_DATA_CHUNK_BYTES]; /**< data chunk of the CEA block */
	uint8_t pad[3]; /**< padding and for future expansion */
};

/**
 * Result of VSDB parsing from CEA block
 */
struct dmub_cmd_edid_cea_amd_vsdb {
	uint8_t vsdb_found;		/**< 1 if parsing has found valid AMD VSDB */
	uint8_t freesync_supported;	/**< 1 if Freesync is supported */
	uint16_t amd_vsdb_version;	/**< AMD VSDB version */
	uint16_t min_frame_rate;	/**< Maximum frame rate */
	uint16_t max_frame_rate;	/**< Minimum frame rate */
};

/**
 * Result of sending a CEA chunk
 */
struct dmub_cmd_edid_cea_ack {
	uint16_t offset;	/**< offset of the chunk into the CEA block */
	uint8_t success;	/**< 1 if this sending of chunk succeeded */
	uint8_t pad;		/**< padding and for future expansion */
};

/**
 * Specify whether the result is an ACK/NACK or the parsing has finished
 */
enum dmub_cmd_edid_cea_reply_type {
	DMUB_CMD__EDID_CEA_AMD_VSDB	= 1, /**< VSDB parsing has finished */
	DMUB_CMD__EDID_CEA_ACK		= 2, /**< acknowledges the CEA sending is OK or failing */
};

/**
 * Definition of a DMUB_CMD__EDID_CEA command.
 */
struct dmub_rb_cmd_edid_cea {
	struct dmub_cmd_header header;	/**< Command header */
	union dmub_cmd_edid_cea_data {
		struct dmub_cmd_send_edid_cea input; /**< input to send CEA chunks */
		struct dmub_cmd_edid_cea_output { /**< output with results */
			uint8_t type;	/**< dmub_cmd_edid_cea_reply_type */
			union {
				struct dmub_cmd_edid_cea_amd_vsdb amd_vsdb;
				struct dmub_cmd_edid_cea_ack ack;
			};
		} output;	/**< output to retrieve ACK/NACK or VSDB parsing results */
	} data;	/**< Command data */

};

/**
 * struct dmub_cmd_cable_id_input - Defines the input of DMUB_CMD_GET_USBC_CABLE_ID command.
 */
struct dmub_cmd_cable_id_input {
	uint8_t phy_inst;  /**< phy inst for cable id data */
};

/**
 * struct dmub_cmd_cable_id_input - Defines the output of DMUB_CMD_GET_USBC_CABLE_ID command.
 */
struct dmub_cmd_cable_id_output {
	uint8_t UHBR10_20_CAPABILITY	:2; /**< b'01 for UHBR10 support, b'10 for both UHBR10 and UHBR20 support */
	uint8_t UHBR13_5_CAPABILITY	:1; /**< b'1 for UHBR13.5 support */
	uint8_t CABLE_TYPE		:3; /**< b'01 for passive cable, b'10 for active LRD cable, b'11 for active retimer cable */
	uint8_t RESERVED		:2; /**< reserved means not defined */
};

/**
 * Definition of a DMUB_CMD_GET_USBC_CABLE_ID command
 */
struct dmub_rb_cmd_get_usbc_cable_id {
	struct dmub_cmd_header header; /**< Command header */
	/**
	 * Data passed from driver to FW in a DMUB_CMD_GET_USBC_CABLE_ID command.
	 */
	union dmub_cmd_cable_id_data {
		struct dmub_cmd_cable_id_input input; /**< Input */
		struct dmub_cmd_cable_id_output output; /**< Output */
		uint8_t output_raw; /**< Raw data output */
	} data;
};

/**
 * Command type of a DMUB_CMD__SECURE_DISPLAY command
 */
enum dmub_cmd_secure_display_type {
	DMUB_CMD__SECURE_DISPLAY_TEST_CMD = 0,		/* test command to only check if inbox message works */
	DMUB_CMD__SECURE_DISPLAY_CRC_STOP_UPDATE,
	DMUB_CMD__SECURE_DISPLAY_CRC_WIN_NOTIFY
};

/**
 * Definition of a DMUB_CMD__SECURE_DISPLAY command
 */
struct dmub_rb_cmd_secure_display {
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to dmub firmware.
	 */
	struct dmub_cmd_roi_info {
		uint16_t x_start;
		uint16_t x_end;
		uint16_t y_start;
		uint16_t y_end;
		uint8_t otg_id;
		uint8_t phy_id;
	} roi_info;
};

/**
 * Command type of a DMUB_CMD__PSP command
 */
enum dmub_cmd_psp_type {
	DMUB_CMD__PSP_ASSR_ENABLE = 0
};

/**
 * Data passed from driver to FW in a DMUB_CMD__PSP_ASSR_ENABLE command.
 */
struct dmub_cmd_assr_enable_data {
	/**
	 * ASSR enable or disable.
	 */
	uint8_t enable;
	/**
	 * PHY port type.
	 * Indicates eDP / non-eDP port type
	 */
	uint8_t phy_port_type;
	/**
	 * PHY port ID.
	 */
	uint8_t phy_port_id;
	/**
	 * Link encoder index.
	 */
	uint8_t link_enc_index;
	/**
	 * HPO mode.
	 */
	uint8_t hpo_mode;

	/**
	 * Reserved field.
	 */
	uint8_t reserved[7];
};

/**
 * Definition of a DMUB_CMD__PSP_ASSR_ENABLE command.
 */
struct dmub_rb_cmd_assr_enable {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Assr data.
	 */
	struct dmub_cmd_assr_enable_data assr_data;

	/**
	 * Reserved field.
	 */
	uint32_t reserved[3];
};

/**
 * union dmub_rb_cmd - DMUB inbox command.
 */
union dmub_rb_cmd {
	/**
	 * Elements shared with all commands.
	 */
	struct dmub_rb_cmd_common cmd_common;
	/**
	 * Definition of a DMUB_CMD__REG_SEQ_READ_MODIFY_WRITE command.
	 */
	struct dmub_rb_cmd_read_modify_write read_modify_write;
	/**
	 * Definition of a DMUB_CMD__REG_SEQ_FIELD_UPDATE_SEQ command.
	 */
	struct dmub_rb_cmd_reg_field_update_sequence reg_field_update_seq;
	/**
	 * Definition of a DMUB_CMD__REG_SEQ_BURST_WRITE command.
	 */
	struct dmub_rb_cmd_burst_write burst_write;
	/**
	 * Definition of a DMUB_CMD__REG_REG_WAIT command.
	 */
	struct dmub_rb_cmd_reg_wait reg_wait;
	/**
	 * Definition of a DMUB_CMD__VBIOS_DIGX_ENCODER_CONTROL command.
	 */
	struct dmub_rb_cmd_digx_encoder_control digx_encoder_control;
	/**
	 * Definition of a DMUB_CMD__VBIOS_SET_PIXEL_CLOCK command.
	 */
	struct dmub_rb_cmd_set_pixel_clock set_pixel_clock;
	/**
	 * Definition of a DMUB_CMD__VBIOS_ENABLE_DISP_POWER_GATING command.
	 */
	struct dmub_rb_cmd_enable_disp_power_gating enable_disp_power_gating;
	/**
	 * Definition of a DMUB_CMD__VBIOS_DPPHY_INIT command.
	 */
	struct dmub_rb_cmd_dpphy_init dpphy_init;
	/**
	 * Definition of a DMUB_CMD__VBIOS_DIG1_TRANSMITTER_CONTROL command.
	 */
	struct dmub_rb_cmd_dig1_transmitter_control dig1_transmitter_control;
	/**
	 * Definition of a DMUB_CMD__VBIOS_DOMAIN_CONTROL command.
	 */
	struct dmub_rb_cmd_domain_control domain_control;
	/**
	 * Definition of a DMUB_CMD__PSR_SET_VERSION command.
	 */
	struct dmub_rb_cmd_psr_set_version psr_set_version;
	/**
	 * Definition of a DMUB_CMD__PSR_COPY_SETTINGS command.
	 */
	struct dmub_rb_cmd_psr_copy_settings psr_copy_settings;
	/**
	 * Definition of a DMUB_CMD__PSR_ENABLE command.
	 */
	struct dmub_rb_cmd_psr_enable psr_enable;
	/**
	 * Definition of a DMUB_CMD__PSR_SET_LEVEL command.
	 */
	struct dmub_rb_cmd_psr_set_level psr_set_level;
	/**
	 * Definition of a DMUB_CMD__PSR_FORCE_STATIC command.
	 */
	struct dmub_rb_cmd_psr_force_static psr_force_static;
	/**
	 * Definition of a DMUB_CMD__UPDATE_DIRTY_RECT command.
	 */
	struct dmub_rb_cmd_update_dirty_rect update_dirty_rect;
	/**
	 * Definition of a DMUB_CMD__UPDATE_CURSOR_INFO command.
	 */
	struct dmub_rb_cmd_update_cursor_info update_cursor_info;
	/**
	 * Definition of a DMUB_CMD__HW_LOCK command.
	 * Command is used by driver and FW.
	 */
	struct dmub_rb_cmd_lock_hw lock_hw;
	/**
	 * Definition of a DMUB_CMD__SET_SINK_VTOTAL_IN_PSR_ACTIVE command.
	 */
	struct dmub_rb_cmd_psr_set_vtotal psr_set_vtotal;
	/**
	 * Definition of a DMUB_CMD__SET_PSR_POWER_OPT command.
	 */
	struct dmub_rb_cmd_psr_set_power_opt psr_set_power_opt;
	/**
	 * Definition of a DMUB_CMD__PLAT_54186_WA command.
	 */
	struct dmub_rb_cmd_PLAT_54186_wa PLAT_54186_wa;
	/**
	 * Definition of a DMUB_CMD__MALL command.
	 */
	struct dmub_rb_cmd_mall mall;

	/**
	 * Definition of a DMUB_CMD__CAB command.
	 */
	struct dmub_rb_cmd_cab_for_ss cab;

	struct dmub_rb_cmd_fw_assisted_mclk_switch_v2 fw_assisted_mclk_switch_v2;

	/**
	 * Definition of a DMUB_CMD__IDLE_OPT_DCN_RESTORE command.
	 */
	struct dmub_rb_cmd_idle_opt_dcn_restore dcn_restore;

	/**
	 * Definition of a DMUB_CMD__CLK_MGR_NOTIFY_CLOCKS command.
	 */
	struct dmub_rb_cmd_clk_mgr_notify_clocks notify_clocks;

	/**
	 * Definition of DMUB_CMD__PANEL_CNTL commands.
	 */
	struct dmub_rb_cmd_panel_cntl panel_cntl;

	/**
	 * Definition of a DMUB_CMD__ABM_SET_PIPE command.
	 */
	struct dmub_rb_cmd_abm_set_pipe abm_set_pipe;

	/**
	 * Definition of a DMUB_CMD__ABM_SET_BACKLIGHT command.
	 */
	struct dmub_rb_cmd_abm_set_backlight abm_set_backlight;

	/**
	 * Definition of a DMUB_CMD__ABM_SET_LEVEL command.
	 */
	struct dmub_rb_cmd_abm_set_level abm_set_level;

	/**
	 * Definition of a DMUB_CMD__ABM_SET_AMBIENT_LEVEL command.
	 */
	struct dmub_rb_cmd_abm_set_ambient_level abm_set_ambient_level;

	/**
	 * Definition of a DMUB_CMD__ABM_SET_PWM_FRAC command.
	 */
	struct dmub_rb_cmd_abm_set_pwm_frac abm_set_pwm_frac;

	/**
	 * Definition of a DMUB_CMD__ABM_INIT_CONFIG command.
	 */
	struct dmub_rb_cmd_abm_init_config abm_init_config;

	/**
	 * Definition of a DMUB_CMD__ABM_PAUSE command.
	 */
	struct dmub_rb_cmd_abm_pause abm_pause;

	/**
	 * Definition of a DMUB_CMD__ABM_SAVE_RESTORE command.
	 */
	struct dmub_rb_cmd_abm_save_restore abm_save_restore;

	/**
	 * Definition of a DMUB_CMD__ABM_QUERY_CAPS command.
	 */
	struct dmub_rb_cmd_abm_query_caps abm_query_caps;

	/**
	 * Definition of a DMUB_CMD__ABM_GET_ACE_CURVE command.
	 */
	struct dmub_rb_cmd_abm_get_ace_curve abm_get_ace_curve;

	/**
	 * Definition of a DMUB_CMD__ABM_SET_EVENT command.
	 */
	struct dmub_rb_cmd_abm_set_event abm_set_event;

	/**
	 * Definition of a DMUB_CMD__DP_AUX_ACCESS command.
	 */
	struct dmub_rb_cmd_dp_aux_access dp_aux_access;

	/**
	 * Definition of a DMUB_CMD__OUTBOX1_ENABLE command.
	 */
	struct dmub_rb_cmd_outbox1_enable outbox1_enable;

	/**
	 * Definition of a DMUB_CMD__QUERY_FEATURE_CAPS command.
	 */
	struct dmub_rb_cmd_query_feature_caps query_feature_caps;

	/**
	 * Definition of a DMUB_CMD__GET_VISUAL_CONFIRM_COLOR command.
	 */
	struct dmub_rb_cmd_get_visual_confirm_color visual_confirm_color;
	struct dmub_rb_cmd_drr_update drr_update;
	struct dmub_rb_cmd_fw_assisted_mclk_switch fw_assisted_mclk_switch;

	/**
	 * Definition of a DMUB_CMD__VBIOS_LVTMA_CONTROL command.
	 */
	struct dmub_rb_cmd_lvtma_control lvtma_control;
	/**
	 * Definition of a DMUB_CMD__VBIOS_TRANSMITTER_QUERY_DP_ALT command.
	 */
	struct dmub_rb_cmd_transmitter_query_dp_alt query_dp_alt;
	/**
	 * Definition of a DMUB_CMD__VBIOS_TRANSMITTER_SET_PHY_FSM command.
	 */
	struct dmub_rb_cmd_transmitter_set_phy_fsm set_phy_fsm;
	/**
	 * Definition of a DMUB_CMD__DPIA_DIG1_CONTROL command.
	 */
	struct dmub_rb_cmd_dig1_dpia_control dig1_dpia_control;
	/**
	 * Definition of a DMUB_CMD__DPIA_SET_CONFIG_ACCESS command.
	 */
	struct dmub_rb_cmd_set_config_access set_config_access;
	/**
	 * Definition of a DMUB_CMD__DPIA_MST_ALLOC_SLOTS command.
	 */
	struct dmub_rb_cmd_set_mst_alloc_slots set_mst_alloc_slots;
	/**
	 * Definition of a DMUB_CMD__DPIA_SET_TPS_NOTIFICATION command.
	 */
	struct dmub_rb_cmd_set_tps_notification set_tps_notification;
	/**
	 * Definition of a DMUB_CMD__EDID_CEA command.
	 */
	struct dmub_rb_cmd_edid_cea edid_cea;
	/**
	 * Definition of a DMUB_CMD_GET_USBC_CABLE_ID command.
	 */
	struct dmub_rb_cmd_get_usbc_cable_id cable_id;

	/**
	 * Definition of a DMUB_CMD__QUERY_HPD_STATE command.
	 */
	struct dmub_rb_cmd_query_hpd_state query_hpd;
	/**
	 * Definition of a DMUB_CMD__SECURE_DISPLAY command.
	 */
	struct dmub_rb_cmd_secure_display secure_display;

	/**
	 * Definition of a DMUB_CMD__DPIA_HPD_INT_ENABLE command.
	 */
	struct dmub_rb_cmd_dpia_hpd_int_enable dpia_hpd_int_enable;
	/**
	 * Definition of a DMUB_CMD__IDLE_OPT_DCN_NOTIFY_IDLE command.
	 */
	struct dmub_rb_cmd_idle_opt_dcn_notify_idle idle_opt_notify_idle;
	/**
	 * Definition of a DMUB_CMD__IDLE_OPT_SET_DC_POWER_STATE command.
	 */
	struct dmub_rb_cmd_idle_opt_set_dc_power_state idle_opt_set_dc_power_state;
	/*
	 * Definition of a DMUB_CMD__REPLAY_COPY_SETTINGS command.
	 */
	struct dmub_rb_cmd_replay_copy_settings replay_copy_settings;
	/**
	 * Definition of a DMUB_CMD__REPLAY_ENABLE command.
	 */
	struct dmub_rb_cmd_replay_enable replay_enable;
	/**
	 * Definition of a DMUB_CMD__SET_REPLAY_POWER_OPT command.
	 */
	struct dmub_rb_cmd_replay_set_power_opt replay_set_power_opt;
	/**
	 * Definition of a DMUB_CMD__REPLAY_SET_COASTING_VTOTAL command.
	 */
	struct dmub_rb_cmd_replay_set_coasting_vtotal replay_set_coasting_vtotal;
	/**
	 * Definition of a DMUB_CMD__REPLAY_SET_POWER_OPT_AND_COASTING_VTOTAL command.
	 */
	struct dmub_rb_cmd_replay_set_power_opt_and_coasting_vtotal replay_set_power_opt_and_coasting_vtotal;

	struct dmub_rb_cmd_replay_set_timing_sync replay_set_timing_sync;
	/**
	 * Definition of a DMUB_CMD__REPLAY_SET_RESIDENCY_FRAMEUPDATE_TIMER command.
	 */
	struct dmub_rb_cmd_replay_set_frameupdate_timer replay_set_frameupdate_timer;
	/**
	 * Definition of a DMUB_CMD__REPLAY_SET_PSEUDO_VTOTAL command.
	 */
	struct dmub_rb_cmd_replay_set_pseudo_vtotal replay_set_pseudo_vtotal;
	/**
	 * Definition of a DMUB_CMD__REPLAY_DISABLED_ADAPTIVE_SYNC_SDP command.
	 */
	struct dmub_rb_cmd_replay_disabled_adaptive_sync_sdp replay_disabled_adaptive_sync_sdp;
	/**
	 * Definition of a DMUB_CMD__REPLAY_SET_GENERAL_CMD command.
	 */
	struct dmub_rb_cmd_replay_set_general_cmd replay_set_general_cmd;
	/**
	 * Definition of a DMUB_CMD__PSP_ASSR_ENABLE command.
	 */
	struct dmub_rb_cmd_assr_enable assr_enable;
	struct dmub_rb_cmd_fams2 fams2_config;

	struct dmub_rb_cmd_fams2_drr_update fams2_drr_update;

	struct dmub_rb_cmd_fams2_flip fams2_flip;
};

/**
 * union dmub_rb_out_cmd - Outbox command
 */
union dmub_rb_out_cmd {
	/**
	 * Parameters common to every command.
	 */
	struct dmub_rb_cmd_common cmd_common;
	/**
	 * AUX reply command.
	 */
	struct dmub_rb_cmd_dp_aux_reply dp_aux_reply;
	/**
	 * HPD notify command.
	 */
	struct dmub_rb_cmd_dp_hpd_notify dp_hpd_notify;
	/**
	 * SET_CONFIG reply command.
	 */
	struct dmub_rb_cmd_dp_set_config_reply set_config_reply;
	/**
	 * DPIA notification command.
	 */
	struct dmub_rb_cmd_dpia_notification dpia_notification;
	/**
	 * HPD sense notification command.
	 */
	struct dmub_rb_cmd_hpd_sense_notify hpd_sense_notify;
};
#pragma pack(pop)


//==============================================================================
//</DMUB_CMD>===================================================================
//==============================================================================
//< DMUB_RB>====================================================================
//==============================================================================

/**
 * struct dmub_rb_init_params - Initialization params for DMUB ringbuffer
 */
struct dmub_rb_init_params {
	void *ctx; /**< Caller provided context pointer */
	void *base_address; /**< CPU base address for ring's data */
	uint32_t capacity; /**< Ringbuffer capacity in bytes */
	uint32_t read_ptr; /**< Initial read pointer for consumer in bytes */
	uint32_t write_ptr; /**< Initial write pointer for producer in bytes */
};

/**
 * struct dmub_rb - Inbox or outbox DMUB ringbuffer
 */
struct dmub_rb {
	void *base_address; /**< CPU address for the ring's data */
	uint32_t rptr; /**< Read pointer for consumer in bytes */
	uint32_t wrpt; /**< Write pointer for producer in bytes */
	uint32_t capacity; /**< Ringbuffer capacity in bytes */

	void *ctx; /**< Caller provided context pointer */
	void *dmub; /**< Pointer to the DMUB interface */
};

/**
 * @brief Checks if the ringbuffer is empty.
 *
 * @param rb DMUB Ringbuffer
 * @return true if empty
 * @return false otherwise
 */
static inline bool dmub_rb_empty(struct dmub_rb *rb)
{
	return (rb->wrpt == rb->rptr);
}

/**
 * @brief Checks if the ringbuffer is full
 *
 * @param rb DMUB Ringbuffer
 * @return true if full
 * @return false otherwise
 */
static inline bool dmub_rb_full(struct dmub_rb *rb)
{
	uint32_t data_count;

	if (rb->wrpt >= rb->rptr)
		data_count = rb->wrpt - rb->rptr;
	else
		data_count = rb->capacity - (rb->rptr - rb->wrpt);

	return (data_count == (rb->capacity - DMUB_RB_CMD_SIZE));
}

/**
 * @brief Pushes a command into the ringbuffer
 *
 * @param rb DMUB ringbuffer
 * @param cmd The command to push
 * @return true if the ringbuffer was not full
 * @return false otherwise
 */
static inline bool dmub_rb_push_front(struct dmub_rb *rb,
				      const union dmub_rb_cmd *cmd)
{
	uint64_t volatile *dst = (uint64_t volatile *)((uint8_t *)(rb->base_address) + rb->wrpt);
	const uint64_t *src = (const uint64_t *)cmd;
	uint8_t i;

	if (dmub_rb_full(rb))
		return false;

	// copying data
	for (i = 0; i < DMUB_RB_CMD_SIZE / sizeof(uint64_t); i++)
		*dst++ = *src++;

	rb->wrpt += DMUB_RB_CMD_SIZE;

	if (rb->wrpt >= rb->capacity)
		rb->wrpt %= rb->capacity;

	return true;
}

/**
 * @brief Pushes a command into the DMUB outbox ringbuffer
 *
 * @param rb DMUB outbox ringbuffer
 * @param cmd Outbox command
 * @return true if not full
 * @return false otherwise
 */
static inline bool dmub_rb_out_push_front(struct dmub_rb *rb,
				      const union dmub_rb_out_cmd *cmd)
{
	uint8_t *dst = (uint8_t *)(rb->base_address) + rb->wrpt;
	const uint8_t *src = (const uint8_t *)cmd;

	if (dmub_rb_full(rb))
		return false;

	dmub_memcpy(dst, src, DMUB_RB_CMD_SIZE);

	rb->wrpt += DMUB_RB_CMD_SIZE;

	if (rb->wrpt >= rb->capacity)
		rb->wrpt %= rb->capacity;

	return true;
}

/**
 * @brief Returns the next unprocessed command in the ringbuffer.
 *
 * @param rb DMUB ringbuffer
 * @param cmd The command to return
 * @return true if not empty
 * @return false otherwise
 */
static inline bool dmub_rb_front(struct dmub_rb *rb,
				 union dmub_rb_cmd  **cmd)
{
	uint8_t *rb_cmd = (uint8_t *)(rb->base_address) + rb->rptr;

	if (dmub_rb_empty(rb))
		return false;

	*cmd = (union dmub_rb_cmd *)rb_cmd;

	return true;
}

/**
 * @brief Determines the next ringbuffer offset.
 *
 * @param rb DMUB inbox ringbuffer
 * @param num_cmds Number of commands
 * @param next_rptr The next offset in the ringbuffer
 */
static inline void dmub_rb_get_rptr_with_offset(struct dmub_rb *rb,
				  uint32_t num_cmds,
				  uint32_t *next_rptr)
{
	*next_rptr = rb->rptr + DMUB_RB_CMD_SIZE * num_cmds;

	if (*next_rptr >= rb->capacity)
		*next_rptr %= rb->capacity;
}

/**
 * @brief Returns a pointer to a command in the inbox.
 *
 * @param rb DMUB inbox ringbuffer
 * @param cmd The inbox command to return
 * @param rptr The ringbuffer offset
 * @return true if not empty
 * @return false otherwise
 */
static inline bool dmub_rb_peek_offset(struct dmub_rb *rb,
				 union dmub_rb_cmd  **cmd,
				 uint32_t rptr)
{
	uint8_t *rb_cmd = (uint8_t *)(rb->base_address) + rptr;

	if (dmub_rb_empty(rb))
		return false;

	*cmd = (union dmub_rb_cmd *)rb_cmd;

	return true;
}

/**
 * @brief Returns the next unprocessed command in the outbox.
 *
 * @param rb DMUB outbox ringbuffer
 * @param cmd The outbox command to return
 * @return true if not empty
 * @return false otherwise
 */
static inline bool dmub_rb_out_front(struct dmub_rb *rb,
				 union dmub_rb_out_cmd *cmd)
{
	const uint64_t volatile *src = (const uint64_t volatile *)((uint8_t *)(rb->base_address) + rb->rptr);
	uint64_t *dst = (uint64_t *)cmd;
	uint8_t i;

	if (dmub_rb_empty(rb))
		return false;

	// copying data
	for (i = 0; i < DMUB_RB_CMD_SIZE / sizeof(uint64_t); i++)
		*dst++ = *src++;

	return true;
}

/**
 * @brief Removes the front entry in the ringbuffer.
 *
 * @param rb DMUB ringbuffer
 * @return true if the command was removed
 * @return false if there were no commands
 */
static inline bool dmub_rb_pop_front(struct dmub_rb *rb)
{
	if (dmub_rb_empty(rb))
		return false;

	rb->rptr += DMUB_RB_CMD_SIZE;

	if (rb->rptr >= rb->capacity)
		rb->rptr %= rb->capacity;

	return true;
}

/**
 * @brief Flushes commands in the ringbuffer to framebuffer memory.
 *
 * Avoids a race condition where DMCUB accesses memory while
 * there are still writes in flight to framebuffer.
 *
 * @param rb DMUB ringbuffer
 */
static inline void dmub_rb_flush_pending(const struct dmub_rb *rb)
{
	uint32_t rptr = rb->rptr;
	uint32_t wptr = rb->wrpt;

	while (rptr != wptr) {
		uint64_t *data = (uint64_t *)((uint8_t *)(rb->base_address) + rptr);
		uint8_t i;

		for (i = 0; i < DMUB_RB_CMD_SIZE / sizeof(uint64_t); i++)
			(void)READ_ONCE(*data++);

		rptr += DMUB_RB_CMD_SIZE;
		if (rptr >= rb->capacity)
			rptr %= rb->capacity;
	}
}

/**
 * @brief Initializes a DMCUB ringbuffer
 *
 * @param rb DMUB ringbuffer
 * @param init_params initial configuration for the ringbuffer
 */
static inline void dmub_rb_init(struct dmub_rb *rb,
				struct dmub_rb_init_params *init_params)
{
	rb->base_address = init_params->base_address;
	rb->capacity = init_params->capacity;
	rb->rptr = init_params->read_ptr;
	rb->wrpt = init_params->write_ptr;
}

/**
 * @brief Copies output data from in/out commands into the given command.
 *
 * @param rb DMUB ringbuffer
 * @param cmd Command to copy data into
 */
static inline void dmub_rb_get_return_data(struct dmub_rb *rb,
					   union dmub_rb_cmd *cmd)
{
	// Copy rb entry back into command
	uint8_t *rd_ptr = (rb->rptr == 0) ?
		(uint8_t *)rb->base_address + rb->capacity - DMUB_RB_CMD_SIZE :
		(uint8_t *)rb->base_address + rb->rptr - DMUB_RB_CMD_SIZE;

	dmub_memcpy(cmd, rd_ptr, DMUB_RB_CMD_SIZE);
}

//==============================================================================
//</DMUB_RB>====================================================================
//==============================================================================
#endif /* _DMUB_CMD_H_ */
