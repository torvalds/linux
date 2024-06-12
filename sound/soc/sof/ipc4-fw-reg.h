/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022 Intel Corporation
 */

#ifndef __IPC4_FW_REG_H__
#define __IPC4_FW_REG_H__

#define SOF_IPC4_INVALID_STREAM_POSITION	ULLONG_MAX

/**
 * struct sof_ipc4_pipeline_registers - Pipeline start and end information in fw
 * @stream_start_offset: Stream start offset (LPIB) reported by mixin
 * module allocated on pipeline attached to Host Output Gateway when
 * first data is being mixed to mixout module. When data is not mixed
 * (right after creation/after reset) value "(u64)-1" is reported
 * @stream_end_offset: Stream end offset (LPIB) reported by mixin
 * module allocated on pipeline attached to Host Output Gateway
 * during transition from RUNNING to PAUSED. When data is not mixed
 * (right after creation or after reset) value "(u64)-1" is reported.
 * When first data is mixed then value "0"is reported.
 */
struct sof_ipc4_pipeline_registers {
	u64 stream_start_offset;
	u64 stream_end_offset;
} __packed __aligned(4);

#define SOF_IPC4_PV_MAX_SUPPORTED_CHANNELS 8

/**
 * struct sof_ipc4_peak_volume_regs - Volume information in fw
 * @peak_meter: Peak volume value in fw
 * @current_volume: Current volume value in fw
 * @target_volume: Target volume value in fw
 */
struct sof_ipc4_peak_volume_regs {
	u32 peak_meter[SOF_IPC4_PV_MAX_SUPPORTED_CHANNELS];
	u32 current_volume[SOF_IPC4_PV_MAX_SUPPORTED_CHANNELS];
	u32 target_volume[SOF_IPC4_PV_MAX_SUPPORTED_CHANNELS];
} __packed __aligned(4);

/**
 * struct sof_ipc4_llp_reading - Llp information in fw
 * @llp_l: Lower part of 64-bit LLP
 * @llp_u: Upper part of 64-bit LLP
 * @wclk_l: Lower part of 64-bit Wallclock
 * @wclk_u: Upper part of 64-bit Wallclock
 */
struct sof_ipc4_llp_reading {
	u32 llp_l;
	u32 llp_u;
	u32 wclk_l;
	u32 wclk_u;
} __packed __aligned(4);

/**
 * struct of sof_ipc4_llp_reading_extended - Extended llp info
 * @llp_reading: Llp information in memory window
 * @tpd_low: Total processed data (low part)
 * @tpd_high: Total processed data (high part)
 */
struct sof_ipc4_llp_reading_extended {
	struct sof_ipc4_llp_reading llp_reading;
	u32 tpd_low;
	u32 tpd_high;
} __packed __aligned(4);

/**
 * struct sof_ipc4_llp_reading_slot - Llp slot information in memory window
 * @node_id: Dai gateway node id
 * @reading: Llp information in memory window
 */
struct sof_ipc4_llp_reading_slot {
	u32 node_id;
	struct sof_ipc4_llp_reading reading;
} __packed __aligned(4);

/* ROM information */
#define SOF_IPC4_FW_FUSE_VALUE_MASK		GENMASK(7, 0)
#define SOF_IPC4_FW_LOAD_METHOD_MASK		BIT(8)
#define SOF_IPC4_FW_DOWNLINK_IPC_USE_DMA_MASK	BIT(9)
#define SOF_IPC4_FW_LOAD_METHOD_REV_MASK	GENMASK(11, 10)
#define SOF_IPC4_FW_REVISION_MIN_MASK		GENMASK(15, 12)
#define SOF_IPC4_FW_REVISION_MAJ_MASK		GENMASK(19, 16)
#define SOF_IPC4_FW_VERSION_MIN_MASK		GENMASK(23, 20)
#define SOF_IPC4_FW_VERSION_MAJ_MASK		GENMASK(27, 24)

/* Number of dsp core supported in FW Regs. */
#define SOF_IPC4_MAX_SUPPORTED_ADSP_CORES	8

/* Number of host pipeline registers slots in FW Regs. */
#define SOF_IPC4_MAX_PIPELINE_REG_SLOTS		16

/* Number of PeakVol registers slots in FW Regs. */
#define SOF_IPC4_MAX_PEAK_VOL_REG_SLOTS		16

/* Number of GPDMA LLP Reading slots in FW Regs. */
#define SOF_IPC4_MAX_LLP_GPDMA_READING_SLOTS	24

/* Number of Aggregated SNDW Reading slots in FW Regs. */
#define SOF_IPC4_MAX_LLP_SNDW_READING_SLOTS	15

/* Current ABI version of the Fw registers layout. */
#define SOF_IPC4_FW_REGS_ABI_VER		1

/**
 * struct sof_ipc4_fw_registers - FW Registers exposes additional
 * DSP / FW state information to the driver
 * @fw_status: Current ROM / FW status
 * @lec: Last ROM / FW error code
 * @fps: Current DSP clock status
 * @lnec: Last Native Error Code(from external library)
 * @ltr: Copy of LTRC HW register value(FW only)
 * @rsvd0: Reserved0
 * @rom_info: ROM info
 * @abi_ver: Version of the layout, set to the current FW_REGS_ABI_VER
 * @slave_core_sts: Slave core states
 * @rsvd2: Reserved2
 * @pipeline_regs: State of pipelines attached to host output  gateways
 * @peak_vol_regs: State of PeakVol instances indexed by the PeakVol's instance_id
 * @llp_gpdma_reading_slots: LLP Readings for single link gateways
 * @llp_sndw_reading_slots: SNDW aggregated link gateways
 * @llp_evad_reading_slot: LLP Readings for EVAD gateway
 */
struct sof_ipc4_fw_registers {
	u32 fw_status;
	u32 lec;
	u32 fps;
	u32 lnec;
	u32 ltr;
	u32 rsvd0;
	u32 rom_info;
	u32 abi_ver;
	u8 slave_core_sts[SOF_IPC4_MAX_SUPPORTED_ADSP_CORES];
	u32 rsvd2[6];

	struct sof_ipc4_pipeline_registers
		pipeline_regs[SOF_IPC4_MAX_PIPELINE_REG_SLOTS];

	struct sof_ipc4_peak_volume_regs
		peak_vol_regs[SOF_IPC4_MAX_PEAK_VOL_REG_SLOTS];

	struct sof_ipc4_llp_reading_slot
		llp_gpdma_reading_slots[SOF_IPC4_MAX_LLP_GPDMA_READING_SLOTS];

	struct sof_ipc4_llp_reading_slot
		llp_sndw_reading_slots[SOF_IPC4_MAX_LLP_SNDW_READING_SLOTS];

	struct sof_ipc4_llp_reading_slot llp_evad_reading_slot;
} __packed __aligned(4);

#endif
