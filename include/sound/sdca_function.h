/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 *
 * Copyright(c) 2024 Intel Corporation
 */

#ifndef __SDCA_FUNCTION_H__
#define __SDCA_FUNCTION_H__

#include <linux/bits.h>
#include <linux/types.h>
#include <linux/hid.h>

struct device;
struct sdca_entity;
struct sdca_function_desc;

#define SDCA_NO_INTERRUPT -1

/*
 * The addressing space for SDCA relies on 7 bits for Entities, so a
 * maximum of 128 Entities per function can be represented.
 */
#define SDCA_MAX_ENTITY_COUNT 128

/*
 * Sanity check on number of initialization writes, can be expanded if needed.
 */
#define SDCA_MAX_INIT_COUNT 2048

/*
 * The Cluster IDs are 16-bit, so a maximum of 65535 Clusters per
 * function can be represented, however limit this to a slightly
 * more reasonable value. Can be expanded if needed.
 */
#define SDCA_MAX_CLUSTER_COUNT 256

/*
 * Sanity check on number of channels per Cluster, can be expanded if needed.
 */
#define SDCA_MAX_CHANNEL_COUNT 32

/*
 * Sanity check on number of PDE delays, can be expanded if needed.
 */
#define SDCA_MAX_DELAY_COUNT 256

/*
 * Sanity check on size of affected controls data, can be expanded if needed.
 */
#define SDCA_MAX_AFFECTED_COUNT 2048

/**
 * enum sdca_function_type - SDCA Function Type codes
 * @SDCA_FUNCTION_TYPE_SMART_AMP: Amplifier with protection features.
 * @SDCA_FUNCTION_TYPE_SIMPLE_AMP: Subset of SmartAmp.
 * @SDCA_FUNCTION_TYPE_SMART_MIC: Smart microphone with acoustic triggers.
 * @SDCA_FUNCTION_TYPE_SIMPLE_MIC: Subset of SmartMic.
 * @SDCA_FUNCTION_TYPE_SPEAKER_MIC: Combination of SmartMic and SmartAmp.
 * @SDCA_FUNCTION_TYPE_UAJ: 3.5mm Universal Audio jack.
 * @SDCA_FUNCTION_TYPE_RJ: Retaskable jack.
 * @SDCA_FUNCTION_TYPE_SIMPLE_JACK: Subset of UAJ.
 * @SDCA_FUNCTION_TYPE_HID: Human Interface Device, for e.g. buttons.
 * @SDCA_FUNCTION_TYPE_IMP_DEF: Implementation-defined function.
 *
 * SDCA Function Types from SDCA specification v1.0a Section 5.1.2
 * all Function types not described are reserved.
 *
 * Note that SIMPLE_AMP, SIMPLE_MIC and SIMPLE_JACK Function Types
 * are NOT defined in SDCA 1.0a, but they were defined in earlier
 * drafts and are planned for 1.1.
 */
enum sdca_function_type {
	SDCA_FUNCTION_TYPE_SMART_AMP			= 0x01,
	SDCA_FUNCTION_TYPE_SIMPLE_AMP			= 0x02,
	SDCA_FUNCTION_TYPE_SMART_MIC			= 0x03,
	SDCA_FUNCTION_TYPE_SIMPLE_MIC			= 0x04,
	SDCA_FUNCTION_TYPE_SPEAKER_MIC			= 0x05,
	SDCA_FUNCTION_TYPE_UAJ				= 0x06,
	SDCA_FUNCTION_TYPE_RJ				= 0x07,
	SDCA_FUNCTION_TYPE_SIMPLE_JACK			= 0x08,
	SDCA_FUNCTION_TYPE_HID				= 0x0A,
	SDCA_FUNCTION_TYPE_IMP_DEF			= 0x1F,
};

/* Human-readable names used for kernel logs and Function device registration/bind */
#define	SDCA_FUNCTION_TYPE_SMART_AMP_NAME		"SmartAmp"
#define	SDCA_FUNCTION_TYPE_SIMPLE_AMP_NAME		"SimpleAmp"
#define	SDCA_FUNCTION_TYPE_SMART_MIC_NAME		"SmartMic"
#define	SDCA_FUNCTION_TYPE_SIMPLE_MIC_NAME		"SimpleMic"
#define	SDCA_FUNCTION_TYPE_SPEAKER_MIC_NAME		"SpeakerMic"
#define	SDCA_FUNCTION_TYPE_UAJ_NAME			"UAJ"
#define	SDCA_FUNCTION_TYPE_RJ_NAME			"RJ"
#define	SDCA_FUNCTION_TYPE_SIMPLE_NAME			"SimpleJack"
#define	SDCA_FUNCTION_TYPE_HID_NAME			"HID"
#define	SDCA_FUNCTION_TYPE_IMP_DEF_NAME			"ImplementationDefined"

/**
 * struct sdca_init_write - a single initialization write
 * @addr: Register address to be written
 * @val: Single byte value to be written
 */
struct sdca_init_write {
	u32 addr;
	u8 val;
};

/**
 * define SDCA_CTL_TYPE - create a unique identifier for an SDCA Control
 * @ent: Entity Type code.
 * @sel: Control Selector code.
 *
 * Sometimes there is a need to identify a type of Control, for example to
 * determine what name the control should have. SDCA Selectors are reused
 * across Entity types, as such it is necessary to combine both the Entity
 * Type and the Control Selector to obtain a unique identifier.
 */
#define SDCA_CTL_TYPE(ent, sel) ((ent) << 8 | (sel))

/**
 * define SDCA_CTL_TYPE_S - static version of SDCA_CTL_TYPE
 * @ent: Entity name, for example IT, MFPU, etc. this string can be read
 * from the last characters of the SDCA_ENTITY_TYPE_* macros.
 * @sel: Control Selector name, for example MIC_BIAS, MUTE, etc. this
 * string can be read from the last characters of the SDCA_CTL_*_*
 * macros.
 *
 * Short hand to specific a Control type statically for example:
 * SDCA_CTL_TYPE_S(IT, MIC_BIAS).
 */
#define SDCA_CTL_TYPE_S(ent, sel) SDCA_CTL_TYPE(SDCA_ENTITY_TYPE_##ent, \
						SDCA_CTL_##ent##_##sel)

/**
 * enum sdca_it_controls - SDCA Controls for Input Terminal
 *
 * Control Selectors for Input Terminal from SDCA specification v1.0
 * section 6.2.1.3.
 */
enum sdca_it_controls {
	SDCA_CTL_IT_MIC_BIAS				= 0x03,
	SDCA_CTL_IT_USAGE				= 0x04,
	SDCA_CTL_IT_LATENCY				= 0x08,
	SDCA_CTL_IT_CLUSTERINDEX			= 0x10,
	SDCA_CTL_IT_DATAPORT_SELECTOR			= 0x11,
	SDCA_CTL_IT_MATCHING_GUID			= 0x12,
	SDCA_CTL_IT_KEEP_ALIVE				= 0x13,
	SDCA_CTL_IT_NDAI_STREAM				= 0x14,
	SDCA_CTL_IT_NDAI_CATEGORY			= 0x15,
	SDCA_CTL_IT_NDAI_CODINGTYPE			= 0x16,
	SDCA_CTL_IT_NDAI_PACKETTYPE			= 0x17,
};

/**
 * enum sdca_ot_controls - SDCA Controls for Output Terminal
 *
 * Control Selectors for Output Terminal from SDCA specification v1.0
 * section 6.2.2.3.
 */
enum sdca_ot_controls {
	SDCA_CTL_OT_USAGE				= 0x04,
	SDCA_CTL_OT_LATENCY				= 0x08,
	SDCA_CTL_OT_DATAPORT_SELECTOR			= 0x11,
	SDCA_CTL_OT_MATCHING_GUID			= 0x12,
	SDCA_CTL_OT_KEEP_ALIVE				= 0x13,
	SDCA_CTL_OT_NDAI_STREAM				= 0x14,
	SDCA_CTL_OT_NDAI_CATEGORY			= 0x15,
	SDCA_CTL_OT_NDAI_CODINGTYPE			= 0x16,
	SDCA_CTL_OT_NDAI_PACKETTYPE			= 0x17,
};

/**
 * enum sdca_usage_range - Column definitions for Usage
 */
enum sdca_usage_range {
	SDCA_USAGE_NUMBER				= 0,
	SDCA_USAGE_CBN					= 1,
	SDCA_USAGE_SAMPLE_RATE				= 2,
	SDCA_USAGE_SAMPLE_WIDTH				= 3,
	SDCA_USAGE_FULL_SCALE				= 4,
	SDCA_USAGE_NOISE_FLOOR				= 5,
	SDCA_USAGE_TAG					= 6,
	SDCA_USAGE_NCOLS				= 7,
};

/**
 * enum sdca_dataport_selector_range - Column definitions for DataPort_Selector
 */
enum sdca_dataport_selector_range {
	SDCA_DATAPORT_SELECTOR_NCOLS			= 16,
	SDCA_DATAPORT_SELECTOR_NROWS			= 4,
};

/**
 * enum sdca_mu_controls - SDCA Controls for Mixer Unit
 *
 * Control Selectors for Mixer Unit from SDCA specification v1.0
 * section 6.3.4.2.
 */
enum sdca_mu_controls {
	SDCA_CTL_MU_MIXER				= 0x01,
	SDCA_CTL_MU_LATENCY				= 0x06,
};

/**
 * enum sdca_su_controls - SDCA Controls for Selector Unit
 *
 * Control Selectors for Selector Unit from SDCA specification v1.0
 * section 6.3.8.3.
 */
enum sdca_su_controls {
	SDCA_CTL_SU_SELECTOR				= 0x01,
	SDCA_CTL_SU_LATENCY				= 0x02,
};

/**
 * enum sdca_fu_controls - SDCA Controls for Feature Unit
 *
 * Control Selectors for Feature Unit from SDCA specification v1.0
 * section 6.3.2.3.
 */
enum sdca_fu_controls {
	SDCA_CTL_FU_MUTE				= 0x01,
	SDCA_CTL_FU_CHANNEL_VOLUME			= 0x02,
	SDCA_CTL_FU_AGC					= 0x07,
	SDCA_CTL_FU_BASS_BOOST				= 0x09,
	SDCA_CTL_FU_LOUDNESS				= 0x0A,
	SDCA_CTL_FU_GAIN				= 0x0B,
	SDCA_CTL_FU_LATENCY				= 0x10,
};

/**
 * enum sdca_volume_range - Column definitions for Q7.8dB volumes/gains
 */
enum sdca_volume_range {
	SDCA_VOLUME_LINEAR_MIN				= 0,
	SDCA_VOLUME_LINEAR_MAX				= 1,
	SDCA_VOLUME_LINEAR_STEP				= 2,
	SDCA_VOLUME_LINEAR_NCOLS			= 3,
};

/**
 * enum sdca_xu_controls - SDCA Controls for Extension Unit
 *
 * Control Selectors for Extension Unit from SDCA specification v1.0
 * section 6.3.10.3.
 */
enum sdca_xu_controls {
	SDCA_CTL_XU_BYPASS				= 0x01,
	SDCA_CTL_XU_LATENCY				= 0x06,
	SDCA_CTL_XU_XU_ID				= 0x07,
	SDCA_CTL_XU_XU_VERSION				= 0x08,
	SDCA_CTL_XU_FDL_CURRENTOWNER			= 0x10,
	SDCA_CTL_XU_FDL_MESSAGEOFFSET			= 0x12,
	SDCA_CTL_XU_FDL_MESSAGELENGTH			= 0x13,
	SDCA_CTL_XU_FDL_STATUS				= 0x14,
	SDCA_CTL_XU_FDL_SET_INDEX			= 0x15,
	SDCA_CTL_XU_FDL_HOST_REQUEST			= 0x16,
};

/**
 * enum sdca_cs_controls - SDCA Controls for Clock Source
 *
 * Control Selectors for Clock Source from SDCA specification v1.0
 * section 6.4.1.3.
 */
enum sdca_cs_controls {
	SDCA_CTL_CS_CLOCK_VALID				= 0x02,
	SDCA_CTL_CS_SAMPLERATEINDEX			= 0x10,
};

/**
 * enum sdca_samplerateindex_range - Column definitions for SampleRateIndex
 */
enum sdca_samplerateindex_range {
	SDCA_SAMPLERATEINDEX_INDEX			= 0,
	SDCA_SAMPLERATEINDEX_RATE			= 1,
	SDCA_SAMPLERATEINDEX_NCOLS			= 2,
};

/**
 * enum sdca_cx_controls - SDCA Controls for Clock Selector
 *
 * Control Selectors for Clock Selector from SDCA specification v1.0
 * section 6.4.2.3.
 */
enum sdca_cx_controls {
	SDCA_CTL_CX_CLOCK_SELECT			= 0x01,
};

/**
 * enum sdca_pde_controls - SDCA Controls for Power Domain Entity
 *
 * Control Selectors for Power Domain Entity from SDCA specification
 * v1.0 section 6.5.2.2.
 */
enum sdca_pde_controls {
	SDCA_CTL_PDE_REQUESTED_PS			= 0x01,
	SDCA_CTL_PDE_ACTUAL_PS				= 0x10,
};

/**
 * enum sdca_requested_ps_range - Column definitions for Requested PS
 */
enum sdca_requested_ps_range {
	SDCA_REQUESTED_PS_STATE				= 0,
	SDCA_REQUESTED_PS_NCOLS				= 1,
};

/**
 * enum sdca_ge_controls - SDCA Controls for Group Unit
 *
 * Control Selectors for Group Unit from SDCA specification v1.0
 * section 6.5.1.4.
 */
enum sdca_ge_controls {
	SDCA_CTL_GE_SELECTED_MODE			= 0x01,
	SDCA_CTL_GE_DETECTED_MODE			= 0x02,
};

/**
 * enum sdca_selected_mode_range - Column definitions for Selected Mode
 */
enum sdca_selected_mode_range {
	SDCA_SELECTED_MODE_INDEX			= 0,
	SDCA_SELECTED_MODE_TERM_TYPE			= 1,
	SDCA_SELECTED_MODE_NCOLS			= 2,
};

/**
 * enum sdca_detected_mode_values - Predefined GE Detected Mode values
 */
enum sdca_detected_mode_values {
	SDCA_DETECTED_MODE_JACK_UNPLUGGED		= 0,
	SDCA_DETECTED_MODE_JACK_UNKNOWN			= 1,
	SDCA_DETECTED_MODE_DETECTION_IN_PROGRESS	= 2,
};

/**
 * enum sdca_spe_controls - SDCA Controls for Security & Privacy Unit
 *
 * Control Selectors for Security & Privacy Unit from SDCA
 * specification v1.0 Section 6.5.3.2.
 */
enum sdca_spe_controls {
	SDCA_CTL_SPE_PRIVATE				= 0x01,
	SDCA_CTL_SPE_PRIVACY_POLICY			= 0x02,
	SDCA_CTL_SPE_PRIVACY_LOCKSTATE			= 0x03,
	SDCA_CTL_SPE_PRIVACY_OWNER			= 0x04,
	SDCA_CTL_SPE_AUTHTX_CURRENTOWNER		= 0x10,
	SDCA_CTL_SPE_AUTHTX_MESSAGEOFFSET		= 0x12,
	SDCA_CTL_SPE_AUTHTX_MESSAGELENGTH		= 0x13,
	SDCA_CTL_SPE_AUTHRX_CURRENTOWNER		= 0x14,
	SDCA_CTL_SPE_AUTHRX_MESSAGEOFFSET		= 0x16,
	SDCA_CTL_SPE_AUTHRX_MESSAGELENGTH		= 0x17,
};

/**
 * enum sdca_cru_controls - SDCA Controls for Channel Remapping Unit
 *
 * Control Selectors for Channel Remapping Unit from SDCA
 * specification v1.0 Section 6.3.1.3.
 */
enum sdca_cru_controls {
	SDCA_CTL_CRU_LATENCY				= 0x06,
	SDCA_CTL_CRU_CLUSTERINDEX			= 0x10,
};

/**
 * enum sdca_udmpu_controls - SDCA Controls for Up-Down Mixer Processing Unit
 *
 * Control Selectors for Up-Down Mixer Processing Unit from SDCA
 * specification v1.0 Section 6.3.9.3.
 */
enum sdca_udmpu_controls {
	SDCA_CTL_UDMPU_LATENCY				= 0x06,
	SDCA_CTL_UDMPU_CLUSTERINDEX			= 0x10,
	SDCA_CTL_UDMPU_ACOUSTIC_ENERGY_LEVEL_MONITOR	= 0x11,
	SDCA_CTL_UDMPU_ULTRASOUND_LOOP_GAIN		= 0x12,
	SDCA_CTL_UDMPU_OPAQUESET_0			= 0x18,
	SDCA_CTL_UDMPU_OPAQUESET_1			= 0x19,
	SDCA_CTL_UDMPU_OPAQUESET_2			= 0x1A,
	SDCA_CTL_UDMPU_OPAQUESET_3			= 0x1B,
	SDCA_CTL_UDMPU_OPAQUESET_4			= 0x1C,
	SDCA_CTL_UDMPU_OPAQUESET_5			= 0x1D,
	SDCA_CTL_UDMPU_OPAQUESET_6			= 0x1E,
	SDCA_CTL_UDMPU_OPAQUESET_7			= 0x1F,
	SDCA_CTL_UDMPU_OPAQUESET_8			= 0x20,
	SDCA_CTL_UDMPU_OPAQUESET_9			= 0x21,
	SDCA_CTL_UDMPU_OPAQUESET_10			= 0x22,
	SDCA_CTL_UDMPU_OPAQUESET_11			= 0x23,
	SDCA_CTL_UDMPU_OPAQUESET_12			= 0x24,
	SDCA_CTL_UDMPU_OPAQUESET_13			= 0x25,
	SDCA_CTL_UDMPU_OPAQUESET_14			= 0x26,
	SDCA_CTL_UDMPU_OPAQUESET_15			= 0x27,
	SDCA_CTL_UDMPU_OPAQUESET_16			= 0x28,
	SDCA_CTL_UDMPU_OPAQUESET_17			= 0x29,
	SDCA_CTL_UDMPU_OPAQUESET_18			= 0x2A,
	SDCA_CTL_UDMPU_OPAQUESET_19			= 0x2B,
	SDCA_CTL_UDMPU_OPAQUESET_20			= 0x2C,
	SDCA_CTL_UDMPU_OPAQUESET_21			= 0x2D,
	SDCA_CTL_UDMPU_OPAQUESET_22			= 0x2E,
	SDCA_CTL_UDMPU_OPAQUESET_23			= 0x2F,
};

/**
 * enum sdca_mfpu_controls - SDCA Controls for Multi-Function Processing Unit
 *
 * Control Selectors for Multi-Function Processing Unit from SDCA
 * specification v1.0 Section 6.3.3.4.
 */
enum sdca_mfpu_controls {
	SDCA_CTL_MFPU_BYPASS				= 0x01,
	SDCA_CTL_MFPU_ALGORITHM_READY			= 0x04,
	SDCA_CTL_MFPU_ALGORITHM_ENABLE			= 0x05,
	SDCA_CTL_MFPU_LATENCY				= 0x08,
	SDCA_CTL_MFPU_ALGORITHM_PREPARE			= 0x09,
	SDCA_CTL_MFPU_CLUSTERINDEX			= 0x10,
	SDCA_CTL_MFPU_CENTER_FREQUENCY_INDEX		= 0x11,
	SDCA_CTL_MFPU_ULTRASOUND_LEVEL			= 0x12,
	SDCA_CTL_MFPU_AE_NUMBER				= 0x13,
	SDCA_CTL_MFPU_AE_CURRENTOWNER			= 0x14,
	SDCA_CTL_MFPU_AE_MESSAGEOFFSET			= 0x16,
	SDCA_CTL_MFPU_AE_MESSAGELENGTH			= 0x17,
};

/**
 * enum sdca_smpu_controls - SDCA Controls for Smart Mic Processing Unit
 *
 * Control Selectors for Smart Mic Processing Unit from SDCA
 * specification v1.0 Section 6.3.7.3.
 */
enum sdca_smpu_controls {
	SDCA_CTL_SMPU_LATENCY				= 0x06,
	SDCA_CTL_SMPU_TRIGGER_ENABLE			= 0x10,
	SDCA_CTL_SMPU_TRIGGER_STATUS			= 0x11,
	SDCA_CTL_SMPU_HIST_BUFFER_MODE			= 0x12,
	SDCA_CTL_SMPU_HIST_BUFFER_PREAMBLE		= 0x13,
	SDCA_CTL_SMPU_HIST_ERROR			= 0x14,
	SDCA_CTL_SMPU_TRIGGER_EXTENSION			= 0x15,
	SDCA_CTL_SMPU_TRIGGER_READY			= 0x16,
	SDCA_CTL_SMPU_HIST_CURRENTOWNER			= 0x18,
	SDCA_CTL_SMPU_HIST_MESSAGEOFFSET		= 0x1A,
	SDCA_CTL_SMPU_HIST_MESSAGELENGTH		= 0x1B,
	SDCA_CTL_SMPU_DTODTX_CURRENTOWNER		= 0x1C,
	SDCA_CTL_SMPU_DTODTX_MESSAGEOFFSET		= 0x1E,
	SDCA_CTL_SMPU_DTODTX_MESSAGELENGTH		= 0x1F,
	SDCA_CTL_SMPU_DTODRX_CURRENTOWNER		= 0x20,
	SDCA_CTL_SMPU_DTODRX_MESSAGEOFFSET		= 0x22,
	SDCA_CTL_SMPU_DTODRX_MESSAGELENGTH		= 0x23,
};

/**
 * enum sdca_sapu_controls - SDCA Controls for Smart Amp Processing Unit
 *
 * Control Selectors for Smart Amp Processing Unit from SDCA
 * specification v1.0 Section 6.3.6.3.
 */
enum sdca_sapu_controls {
	SDCA_CTL_SAPU_LATENCY				= 0x05,
	SDCA_CTL_SAPU_PROTECTION_MODE			= 0x10,
	SDCA_CTL_SAPU_PROTECTION_STATUS			= 0x11,
	SDCA_CTL_SAPU_OPAQUESETREQ_INDEX		= 0x12,
	SDCA_CTL_SAPU_DTODTX_CURRENTOWNER		= 0x14,
	SDCA_CTL_SAPU_DTODTX_MESSAGEOFFSET		= 0x16,
	SDCA_CTL_SAPU_DTODTX_MESSAGELENGTH		= 0x17,
	SDCA_CTL_SAPU_DTODRX_CURRENTOWNER		= 0x18,
	SDCA_CTL_SAPU_DTODRX_MESSAGEOFFSET		= 0x1A,
	SDCA_CTL_SAPU_DTODRX_MESSAGELENGTH		= 0x1B,
};

/**
 * enum sdca_ppu_controls - SDCA Controls for Post Processing Unit
 *
 * Control Selectors for Post Processing Unit from SDCA specification
 * v1.0 Section 6.3.5.3.
 */
enum sdca_ppu_controls {
	SDCA_CTL_PPU_LATENCY				= 0x06,
	SDCA_CTL_PPU_POSTURENUMBER			= 0x10,
	SDCA_CTL_PPU_POSTUREEXTENSION			= 0x11,
	SDCA_CTL_PPU_HORIZONTALBALANCE			= 0x12,
	SDCA_CTL_PPU_VERTICALBALANCE			= 0x13,
};

/**
 * enum sdca_tg_controls - SDCA Controls for Tone Generator Entity
 *
 * Control Selectors for Tone Generator from SDCA specification v1.0
 * Section 6.5.4.4.
 */
enum sdca_tg_controls {
	SDCA_CTL_TG_TONE_DIVIDER			= 0x10,
};

/**
 * enum sdca_hide_controls - SDCA Controls for HIDE Entity
 *
 * Control Selectors for HIDE from SDCA specification v1.0 Section
 * 6.6.1.2.
 */
enum sdca_hide_controls {
	SDCA_CTL_HIDE_HIDTX_CURRENTOWNER		= 0x10,
	SDCA_CTL_HIDE_HIDTX_MESSAGEOFFSET		= 0x12,
	SDCA_CTL_HIDE_HIDTX_MESSAGELENGTH		= 0x13,
	SDCA_CTL_HIDE_HIDRX_CURRENTOWNER		= 0x14,
	SDCA_CTL_HIDE_HIDRX_MESSAGEOFFSET		= 0x16,
	SDCA_CTL_HIDE_HIDRX_MESSAGELENGTH		= 0x17,
};

/**
 * enum sdca_entity0_controls - SDCA Controls for Entity 0
 *
 * Control Selectors for Entity 0 from SDCA specification v1.0 Section
 * 6.7.1.1.
 */
enum sdca_entity0_controls {
	SDCA_CTL_ENTITY_0_COMMIT_GROUP_MASK		= 0x01,
	SDCA_CTL_ENTITY_0_FUNCTION_SDCA_VERSION		= 0x04,
	SDCA_CTL_ENTITY_0_FUNCTION_TYPE			= 0x05,
	SDCA_CTL_ENTITY_0_FUNCTION_MANUFACTURER_ID	= 0x06,
	SDCA_CTL_ENTITY_0_FUNCTION_ID			= 0x07,
	SDCA_CTL_ENTITY_0_FUNCTION_VERSION		= 0x08,
	SDCA_CTL_ENTITY_0_FUNCTION_EXTENSION_ID		= 0x09,
	SDCA_CTL_ENTITY_0_FUNCTION_EXTENSION_VERSION	= 0x0A,
	SDCA_CTL_ENTITY_0_FUNCTION_STATUS		= 0x10,
	SDCA_CTL_ENTITY_0_FUNCTION_ACTION		= 0x11,
	SDCA_CTL_ENTITY_0_MATCHING_GUID			= 0x12,
	SDCA_CTL_ENTITY_0_DEVICE_MANUFACTURER_ID	= 0x2C,
	SDCA_CTL_ENTITY_0_DEVICE_PART_ID		= 0x2D,
	SDCA_CTL_ENTITY_0_DEVICE_VERSION		= 0x2E,
	SDCA_CTL_ENTITY_0_DEVICE_SDCA_VERSION		= 0x2F,

	/* Function Status Bits */
	SDCA_CTL_ENTITY_0_DEVICE_NEWLY_ATTACHED		= BIT(0),
	SDCA_CTL_ENTITY_0_INTS_DISABLED_ABNORMALLY	= BIT(1),
	SDCA_CTL_ENTITY_0_STREAMING_STOPPED_ABNORMALLY	= BIT(2),
	SDCA_CTL_ENTITY_0_FUNCTION_FAULT		= BIT(3),
	SDCA_CTL_ENTITY_0_UMP_SEQUENCE_FAULT		= BIT(4),
	SDCA_CTL_ENTITY_0_FUNCTION_NEEDS_INITIALIZATION	= BIT(5),
	SDCA_CTL_ENTITY_0_FUNCTION_HAS_BEEN_RESET	= BIT(6),
	SDCA_CTL_ENTITY_0_FUNCTION_BUSY			= BIT(7),
};

#define SDCA_CTL_MIC_BIAS_NAME				"Mic Bias"
#define SDCA_CTL_USAGE_NAME				"Usage"
#define SDCA_CTL_LATENCY_NAME				"Latency"
#define SDCA_CTL_CLUSTERINDEX_NAME			"Cluster Index"
#define SDCA_CTL_DATAPORT_SELECTOR_NAME			"Dataport Selector"
#define SDCA_CTL_MATCHING_GUID_NAME			"Matching GUID"
#define SDCA_CTL_KEEP_ALIVE_NAME			"Keep Alive"
#define SDCA_CTL_NDAI_STREAM_NAME			"NDAI Stream"
#define SDCA_CTL_NDAI_CATEGORY_NAME			"NDAI Category"
#define SDCA_CTL_NDAI_CODINGTYPE_NAME			"NDAI Coding Type"
#define SDCA_CTL_NDAI_PACKETTYPE_NAME			"NDAI Packet Type"
#define SDCA_CTL_MIXER_NAME				"Mixer"
#define SDCA_CTL_SELECTOR_NAME				"Selector"
#define SDCA_CTL_MUTE_NAME				"Mute"
#define SDCA_CTL_CHANNEL_VOLUME_NAME			"Channel Volume"
#define SDCA_CTL_AGC_NAME				"AGC"
#define SDCA_CTL_BASS_BOOST_NAME			"Bass Boost"
#define SDCA_CTL_LOUDNESS_NAME				"Loudness"
#define SDCA_CTL_GAIN_NAME				"Gain"
#define SDCA_CTL_BYPASS_NAME				"Bypass"
#define SDCA_CTL_XU_ID_NAME				"XU ID"
#define SDCA_CTL_XU_VERSION_NAME			"XU Version"
#define SDCA_CTL_FDL_CURRENTOWNER_NAME			"FDL Current Owner"
#define SDCA_CTL_FDL_MESSAGEOFFSET_NAME			"FDL Message Offset"
#define SDCA_CTL_FDL_MESSAGELENGTH_NAME			"FDL Message Length"
#define SDCA_CTL_FDL_STATUS_NAME			"FDL Status"
#define SDCA_CTL_FDL_SET_INDEX_NAME			"FDL Set Index"
#define SDCA_CTL_FDL_HOST_REQUEST_NAME			"FDL Host Request"
#define SDCA_CTL_CLOCK_VALID_NAME			"Clock Valid"
#define SDCA_CTL_SAMPLERATEINDEX_NAME			"Sample Rate Index"
#define SDCA_CTL_CLOCK_SELECT_NAME			"Clock Select"
#define SDCA_CTL_REQUESTED_PS_NAME			"Requested PS"
#define SDCA_CTL_ACTUAL_PS_NAME				"Actual PS"
#define SDCA_CTL_SELECTED_MODE_NAME			"Selected Mode"
#define SDCA_CTL_DETECTED_MODE_NAME			"Detected Mode"
#define SDCA_CTL_PRIVATE_NAME				"Private"
#define SDCA_CTL_PRIVACY_POLICY_NAME			"Privacy Policy"
#define SDCA_CTL_PRIVACY_LOCKSTATE_NAME			"Privacy Lockstate"
#define SDCA_CTL_PRIVACY_OWNER_NAME			"Privacy Owner"
#define SDCA_CTL_AUTHTX_CURRENTOWNER_NAME		"AuthTX Current Owner"
#define SDCA_CTL_AUTHTX_MESSAGEOFFSET_NAME		"AuthTX Message Offset"
#define SDCA_CTL_AUTHTX_MESSAGELENGTH_NAME		"AuthTX Message Length"
#define SDCA_CTL_AUTHRX_CURRENTOWNER_NAME		"AuthRX Current Owner"
#define SDCA_CTL_AUTHRX_MESSAGEOFFSET_NAME		"AuthRX Message Offset"
#define SDCA_CTL_AUTHRX_MESSAGELENGTH_NAME		"AuthRX Message Length"
#define SDCA_CTL_ACOUSTIC_ENERGY_LEVEL_MONITOR_NAME	"Acoustic Energy Level Monitor"
#define SDCA_CTL_ULTRASOUND_LOOP_GAIN_NAME		"Ultrasound Loop Gain"
#define SDCA_CTL_OPAQUESET_0_NAME			"Opaqueset 0"
#define SDCA_CTL_OPAQUESET_1_NAME			"Opaqueset 1"
#define SDCA_CTL_OPAQUESET_2_NAME			"Opaqueset 2"
#define SDCA_CTL_OPAQUESET_3_NAME			"Opaqueset 3"
#define SDCA_CTL_OPAQUESET_4_NAME			"Opaqueset 4"
#define SDCA_CTL_OPAQUESET_5_NAME			"Opaqueset 5"
#define SDCA_CTL_OPAQUESET_6_NAME			"Opaqueset 6"
#define SDCA_CTL_OPAQUESET_7_NAME			"Opaqueset 7"
#define SDCA_CTL_OPAQUESET_8_NAME			"Opaqueset 8"
#define SDCA_CTL_OPAQUESET_9_NAME			"Opaqueset 9"
#define SDCA_CTL_OPAQUESET_10_NAME			"Opaqueset 10"
#define SDCA_CTL_OPAQUESET_11_NAME			"Opaqueset 11"
#define SDCA_CTL_OPAQUESET_12_NAME			"Opaqueset 12"
#define SDCA_CTL_OPAQUESET_13_NAME			"Opaqueset 13"
#define SDCA_CTL_OPAQUESET_14_NAME			"Opaqueset 14"
#define SDCA_CTL_OPAQUESET_15_NAME			"Opaqueset 15"
#define SDCA_CTL_OPAQUESET_16_NAME			"Opaqueset 16"
#define SDCA_CTL_OPAQUESET_17_NAME			"Opaqueset 17"
#define SDCA_CTL_OPAQUESET_18_NAME			"Opaqueset 18"
#define SDCA_CTL_OPAQUESET_19_NAME			"Opaqueset 19"
#define SDCA_CTL_OPAQUESET_20_NAME			"Opaqueset 20"
#define SDCA_CTL_OPAQUESET_21_NAME			"Opaqueset 21"
#define SDCA_CTL_OPAQUESET_22_NAME			"Opaqueset 22"
#define SDCA_CTL_OPAQUESET_23_NAME			"Opaqueset 23"
#define SDCA_CTL_ALGORITHM_READY_NAME			"Algorithm Ready"
#define SDCA_CTL_ALGORITHM_ENABLE_NAME			"Algorithm Enable"
#define SDCA_CTL_ALGORITHM_PREPARE_NAME			"Algorithm Prepare"
#define SDCA_CTL_CENTER_FREQUENCY_INDEX_NAME		"Center Frequency Index"
#define SDCA_CTL_ULTRASOUND_LEVEL_NAME			"Ultrasound Level"
#define SDCA_CTL_AE_NUMBER_NAME				"AE Number"
#define SDCA_CTL_AE_CURRENTOWNER_NAME			"AE Current Owner"
#define SDCA_CTL_AE_MESSAGEOFFSET_NAME			"AE Message Offset"
#define SDCA_CTL_AE_MESSAGELENGTH_NAME			"AE Message Length"
#define SDCA_CTL_TRIGGER_ENABLE_NAME			"Trigger Enable"
#define SDCA_CTL_TRIGGER_STATUS_NAME			"Trigger Status"
#define SDCA_CTL_HIST_BUFFER_MODE_NAME			"Hist Buffer Mode"
#define SDCA_CTL_HIST_BUFFER_PREAMBLE_NAME		"Hist Buffer Preamble"
#define SDCA_CTL_HIST_ERROR_NAME			"Hist Error"
#define SDCA_CTL_TRIGGER_EXTENSION_NAME			"Trigger Extension"
#define SDCA_CTL_TRIGGER_READY_NAME			"Trigger Ready"
#define SDCA_CTL_HIST_CURRENTOWNER_NAME			"Hist Current Owner"
#define SDCA_CTL_HIST_MESSAGEOFFSET_NAME		"Hist Message Offset"
#define SDCA_CTL_HIST_MESSAGELENGTH_NAME		"Hist Message Length"
#define SDCA_CTL_DTODTX_CURRENTOWNER_NAME		"DTODTX Current Owner"
#define SDCA_CTL_DTODTX_MESSAGEOFFSET_NAME		"DTODTX Message Offset"
#define SDCA_CTL_DTODTX_MESSAGELENGTH_NAME		"DTODTX Message Length"
#define SDCA_CTL_DTODRX_CURRENTOWNER_NAME		"DTODRX Current Owner"
#define SDCA_CTL_DTODRX_MESSAGEOFFSET_NAME		"DTODRX Message Offset"
#define SDCA_CTL_DTODRX_MESSAGELENGTH_NAME		"DTODRX Message Length"
#define SDCA_CTL_PROTECTION_MODE_NAME			"Protection Mode"
#define SDCA_CTL_PROTECTION_STATUS_NAME			"Protection Status"
#define SDCA_CTL_OPAQUESETREQ_INDEX_NAME		"Opaqueset Req Index"
#define SDCA_CTL_DTODTX_CURRENTOWNER_NAME		"DTODTX Current Owner"
#define SDCA_CTL_DTODTX_MESSAGEOFFSET_NAME		"DTODTX Message Offset"
#define SDCA_CTL_DTODTX_MESSAGELENGTH_NAME		"DTODTX Message Length"
#define SDCA_CTL_DTODRX_CURRENTOWNER_NAME		"DTODRX Current Owner"
#define SDCA_CTL_DTODRX_MESSAGEOFFSET_NAME		"DTODRX Message Offset"
#define SDCA_CTL_DTODRX_MESSAGELENGTH_NAME		"DTODRX Message Length"
#define SDCA_CTL_POSTURENUMBER_NAME			"Posture Number"
#define SDCA_CTL_POSTUREEXTENSION_NAME			"Posture Extension"
#define SDCA_CTL_HORIZONTALBALANCE_NAME			"Horizontal Balance"
#define SDCA_CTL_VERTICALBALANCE_NAME			"Vertical Balance"
#define SDCA_CTL_TONE_DIVIDER_NAME			"Tone Divider"
#define SDCA_CTL_HIDTX_CURRENTOWNER_NAME		"HIDTX Current Owner"
#define SDCA_CTL_HIDTX_MESSAGEOFFSET_NAME		"HIDTX Message Offset"
#define SDCA_CTL_HIDTX_MESSAGELENGTH_NAME		"HIDTX Message Length"
#define SDCA_CTL_HIDRX_CURRENTOWNER_NAME		"HIDRX Current Owner"
#define SDCA_CTL_HIDRX_MESSAGEOFFSET_NAME		"HIDRX Message Offset"
#define SDCA_CTL_HIDRX_MESSAGELENGTH_NAME		"HIDRX Message Length"
#define SDCA_CTL_COMMIT_GROUP_MASK_NAME			"Commit Group Mask"
#define SDCA_CTL_FUNCTION_SDCA_VERSION_NAME		"Function SDCA Version"
#define SDCA_CTL_FUNCTION_TYPE_NAME			"Function Type"
#define SDCA_CTL_FUNCTION_MANUFACTURER_ID_NAME		"Function Manufacturer ID"
#define SDCA_CTL_FUNCTION_ID_NAME			"Function ID"
#define SDCA_CTL_FUNCTION_VERSION_NAME			"Function Version"
#define SDCA_CTL_FUNCTION_EXTENSION_ID_NAME		"Function Extension ID"
#define SDCA_CTL_FUNCTION_EXTENSION_VERSION_NAME	"Function Extension Version"
#define SDCA_CTL_FUNCTION_STATUS_NAME			"Function Status"
#define SDCA_CTL_FUNCTION_ACTION_NAME			"Function Action"
#define SDCA_CTL_DEVICE_MANUFACTURER_ID_NAME		"Device Manufacturer ID"
#define SDCA_CTL_DEVICE_PART_ID_NAME			"Device Part ID"
#define SDCA_CTL_DEVICE_VERSION_NAME			"Device Version"
#define SDCA_CTL_DEVICE_SDCA_VERSION_NAME		"Device SDCA Version"

/**
 * enum sdca_control_datatype - SDCA Control Data Types
 *
 * Data Types as described in the SDCA specification v1.0 section
 * 7.3.
 */
enum sdca_control_datatype {
	SDCA_CTL_DATATYPE_ONEBIT,
	SDCA_CTL_DATATYPE_INTEGER,
	SDCA_CTL_DATATYPE_SPEC_ENCODED_VALUE,
	SDCA_CTL_DATATYPE_BCD,
	SDCA_CTL_DATATYPE_Q7P8DB,
	SDCA_CTL_DATATYPE_BYTEINDEX,
	SDCA_CTL_DATATYPE_POSTURENUMBER,
	SDCA_CTL_DATATYPE_DP_INDEX,
	SDCA_CTL_DATATYPE_BITINDEX,
	SDCA_CTL_DATATYPE_BITMAP,
	SDCA_CTL_DATATYPE_GUID,
	SDCA_CTL_DATATYPE_IMPDEF,
};

/**
 * enum sdca_access_mode - SDCA Control access mode
 *
 * Access modes as described in the SDCA specification v1.0 section
 * 7.1.8.2.
 */
enum sdca_access_mode {
	SDCA_ACCESS_MODE_RW				= 0x0,
	SDCA_ACCESS_MODE_DUAL				= 0x1,
	SDCA_ACCESS_MODE_RW1C				= 0x2,
	SDCA_ACCESS_MODE_RO				= 0x3,
	SDCA_ACCESS_MODE_RW1S				= 0x4,
	SDCA_ACCESS_MODE_DC				= 0x5,
};

/**
 * enum sdca_access_layer - SDCA Control access layer
 *
 * Access layers as described in the SDCA specification v1.0 section
 * 7.1.9.
 */
enum sdca_access_layer {
	SDCA_ACCESS_LAYER_USER				= 1 << 0,
	SDCA_ACCESS_LAYER_APPLICATION			= 1 << 1,
	SDCA_ACCESS_LAYER_CLASS				= 1 << 2,
	SDCA_ACCESS_LAYER_PLATFORM			= 1 << 3,
	SDCA_ACCESS_LAYER_DEVICE			= 1 << 4,
	SDCA_ACCESS_LAYER_EXTENSION			= 1 << 5,
};

/**
 * struct sdca_control_range - SDCA Control range table
 * @cols: Number of columns in the range table.
 * @rows: Number of rows in the range table.
 * @data: Array of values contained in the range table.
 */
struct sdca_control_range {
	unsigned int cols;
	unsigned int rows;
	u32 *data;
};

/**
 * struct sdca_control - information for one SDCA Control
 * @label: Name for the Control, from SDCA Specification v1.0, section 7.1.7.
 * @sel: Identifier used for addressing.
 * @nbits: Number of bits used in the Control.
 * @values: Holds the Control value for constants and defaults.
 * @cn_list: A bitmask showing the valid Control Numbers within this Control,
 * Control Numbers typically represent channels.
 * @interrupt_position: SCDA interrupt line that will alert to changes on this
 * Control.
 * @type: Format of the data in the Control.
 * @range: Buffer describing valid range of values for the Control.
 * @mode: Access mode of the Control.
 * @layers: Bitmask of access layers of the Control.
 * @deferrable: Indicates if the access to the Control can be deferred.
 * @has_default: Indicates the Control has a default value to be written.
 * @has_fixed: Indicates the Control only supports a single value.
 */
struct sdca_control {
	const char *label;
	int sel;

	int nbits;
	int *values;
	u64 cn_list;
	int interrupt_position;

	enum sdca_control_datatype type;
	struct sdca_control_range range;
	enum sdca_access_mode mode;
	u8 layers;

	bool deferrable;
	bool has_default;
	bool has_fixed;
};

/**
 * enum sdca_terminal_type - SDCA Terminal Types
 *
 * Indicate what a Terminal Entity is used for, see in section 6.2.3
 * of the SDCA v1.0 specification.
 */
enum sdca_terminal_type {
	/* Table 77 - Data Port*/
	SDCA_TERM_TYPE_GENERIC				= 0x101,
	SDCA_TERM_TYPE_ULTRASOUND			= 0x180,
	SDCA_TERM_TYPE_CAPTURE_DIRECT_PCM_MIC		= 0x181,
	SDCA_TERM_TYPE_RAW_PDM_MIC			= 0x182,
	SDCA_TERM_TYPE_SPEECH				= 0x183,
	SDCA_TERM_TYPE_VOICE				= 0x184,
	SDCA_TERM_TYPE_SECONDARY_PCM_MIC		= 0x185,
	SDCA_TERM_TYPE_ACOUSTIC_CONTEXT_AWARENESS	= 0x186,
	SDCA_TERM_TYPE_DTOD_STREAM			= 0x187,
	SDCA_TERM_TYPE_REFERENCE_STREAM			= 0x188,
	SDCA_TERM_TYPE_SENSE_CAPTURE			= 0x189,
	SDCA_TERM_TYPE_STREAMING_MIC			= 0x18A,
	SDCA_TERM_TYPE_OPTIMIZATION_STREAM		= 0x190,
	SDCA_TERM_TYPE_PDM_RENDER_STREAM		= 0x191,
	SDCA_TERM_TYPE_COMPANION_DATA			= 0x192,
	/* Table 78 - Transducer */
	SDCA_TERM_TYPE_MICROPHONE_TRANSDUCER		= 0x201,
	SDCA_TERM_TYPE_MICROPHONE_ARRAY_TRANSDUCER	= 0x205,
	SDCA_TERM_TYPE_PRIMARY_FULL_RANGE_SPEAKER	= 0x380,
	SDCA_TERM_TYPE_PRIMARY_LFE_SPEAKER		= 0x381,
	SDCA_TERM_TYPE_PRIMARY_TWEETER_SPEAKER		= 0x382,
	SDCA_TERM_TYPE_PRIMARY_ULTRASOUND_SPEAKER	= 0x383,
	SDCA_TERM_TYPE_SECONDARY_FULL_RANGE_SPEAKER	= 0x390,
	SDCA_TERM_TYPE_SECONDARY_LFE_SPEAKER		= 0x391,
	SDCA_TERM_TYPE_SECONDARY_TWEETER_SPEAKER	= 0x392,
	SDCA_TERM_TYPE_SECONDARY_ULTRASOUND_SPEAKER	= 0x393,
	SDCA_TERM_TYPE_TERTIARY_FULL_RANGE_SPEAKER	= 0x3A0,
	SDCA_TERM_TYPE_TERTIARY_LFE_SPEAKER		= 0x3A1,
	SDCA_TERM_TYPE_TERTIARY_TWEETER_SPEAKER		= 0x3A2,
	SDCA_TERM_TYPE_TERTIARY_ULTRASOUND_SPEAKER	= 0x3A3,
	SDCA_TERM_TYPE_SPDIF				= 0x605,
	SDCA_TERM_TYPE_NDAI_DISPLAY_AUDIO		= 0x610,
	SDCA_TERM_TYPE_NDAI_USB				= 0x612,
	SDCA_TERM_TYPE_NDAI_BLUETOOTH_MAIN		= 0x614,
	SDCA_TERM_TYPE_NDAI_BLUETOOTH_ALTERNATE		= 0x615,
	SDCA_TERM_TYPE_NDAI_BLUETOOTH_BOTH		= 0x616,
	SDCA_TERM_TYPE_LINEIN_STEREO			= 0x680,
	SDCA_TERM_TYPE_LINEIN_FRONT_LR			= 0x681,
	SDCA_TERM_TYPE_LINEIN_CENTER_LFE		= 0x682,
	SDCA_TERM_TYPE_LINEIN_SURROUND_LR		= 0x683,
	SDCA_TERM_TYPE_LINEIN_REAR_LR			= 0x684,
	SDCA_TERM_TYPE_LINEOUT_STEREO			= 0x690,
	SDCA_TERM_TYPE_LINEOUT_FRONT_LR			= 0x691,
	SDCA_TERM_TYPE_LINEOUT_CENTER_LFE		= 0x692,
	SDCA_TERM_TYPE_LINEOUT_SURROUND_LR		= 0x693,
	SDCA_TERM_TYPE_LINEOUT_REAR_LR			= 0x694,
	SDCA_TERM_TYPE_MIC_JACK				= 0x6A0,
	SDCA_TERM_TYPE_STEREO_JACK			= 0x6B0,
	SDCA_TERM_TYPE_FRONT_LR_JACK			= 0x6B1,
	SDCA_TERM_TYPE_CENTER_LFE_JACK			= 0x6B2,
	SDCA_TERM_TYPE_SURROUND_LR_JACK			= 0x6B3,
	SDCA_TERM_TYPE_REAR_LR_JACK			= 0x6B4,
	SDCA_TERM_TYPE_HEADPHONE_JACK			= 0x6C0,
	SDCA_TERM_TYPE_HEADSET_JACK			= 0x6D0,
	/* Table 79 - System */
	SDCA_TERM_TYPE_SENSE_DATA			= 0x280,
	SDCA_TERM_TYPE_PRIVACY_SIGNALING		= 0x741,
	SDCA_TERM_TYPE_PRIVACY_INDICATORS		= 0x747,
};

#define SDCA_TERM_TYPE_LINEIN_STEREO_NAME		"LineIn Stereo"
#define SDCA_TERM_TYPE_LINEIN_FRONT_LR_NAME		"LineIn Front-LR"
#define SDCA_TERM_TYPE_LINEIN_CENTER_LFE_NAME		"LineIn Center-LFE"
#define SDCA_TERM_TYPE_LINEIN_SURROUND_LR_NAME		"LineIn Surround-LR"
#define SDCA_TERM_TYPE_LINEIN_REAR_LR_NAME		"LineIn Rear-LR"
#define SDCA_TERM_TYPE_LINEOUT_STEREO_NAME		"LineOut Stereo"
#define SDCA_TERM_TYPE_LINEOUT_FRONT_LR_NAME		"LineOut Front-LR"
#define SDCA_TERM_TYPE_LINEOUT_CENTER_LFE_NAME		"LineOut Center-LFE"
#define SDCA_TERM_TYPE_LINEOUT_SURROUND_LR_NAME		"LineOut Surround-LR"
#define SDCA_TERM_TYPE_LINEOUT_REAR_LR_NAME		"LineOut Rear-LR"
#define SDCA_TERM_TYPE_MIC_JACK_NAME			"Microphone"
#define SDCA_TERM_TYPE_STEREO_JACK_NAME			"Speaker Stereo"
#define SDCA_TERM_TYPE_FRONT_LR_JACK_NAME		"Speaker Front-LR"
#define SDCA_TERM_TYPE_CENTER_LFE_JACK_NAME		"Speaker Center-LFE"
#define SDCA_TERM_TYPE_SURROUND_LR_JACK_NAME		"Speaker Surround-LR"
#define SDCA_TERM_TYPE_REAR_LR_JACK_NAME		"Speaker Rear-LR"
#define SDCA_TERM_TYPE_HEADPHONE_JACK_NAME		"Headphone"
#define SDCA_TERM_TYPE_HEADSET_JACK_NAME		"Headset"

/**
 * enum sdca_connector_type - SDCA Connector Types
 *
 * Indicate the type of Connector that a Terminal Entity represents,
 * see section 6.2.4 of the SDCA v1.0 specification.
 */
enum sdca_connector_type {
	SDCA_CONN_TYPE_UNKNOWN				= 0x00,
	SDCA_CONN_TYPE_2P5MM_JACK			= 0x01,
	SDCA_CONN_TYPE_3P5MM_JACK			= 0x02,
	SDCA_CONN_TYPE_QUARTER_INCH_JACK		= 0x03,
	SDCA_CONN_TYPE_XLR				= 0x05,
	SDCA_CONN_TYPE_SPDIF_OPTICAL			= 0x06,
	SDCA_CONN_TYPE_RCA				= 0x07,
	SDCA_CONN_TYPE_DIN				= 0x0E,
	SDCA_CONN_TYPE_MINI_DIN				= 0x0F,
	SDCA_CONN_TYPE_EIAJ_OPTICAL			= 0x13,
	SDCA_CONN_TYPE_HDMI				= 0x14,
	SDCA_CONN_TYPE_DISPLAYPORT			= 0x17,
	SDCA_CONN_TYPE_LIGHTNING			= 0x1B,
	SDCA_CONN_TYPE_USB_C				= 0x1E,
	SDCA_CONN_TYPE_OTHER				= 0xFF,
};

/**
 * struct sdca_entity_iot - information specific to Input/Output Entities
 * @clock: Pointer to the Entity providing this Terminal's clock.
 * @type: Usage of the Terminal Entity.
 * @connector: Physical Connector of the Terminal Entity.
 * @reference: Physical Jack number of the Terminal Entity.
 * @num_transducer: Number of transducers attached to the Terminal Entity.
 * @is_dataport: Boolean indicating if this Terminal represents a Dataport.
 */
struct sdca_entity_iot {
	struct sdca_entity *clock;

	enum sdca_terminal_type type;
	enum sdca_connector_type connector;
	int reference;
	int num_transducer;

	bool is_dataport;
};

/**
 * enum sdca_clock_type - SDCA Clock Types
 *
 * Indicate the synchronicity of an Clock Entity, see section 6.4.1.3
 * of the SDCA v1.0 specification.
 */
enum sdca_clock_type {
	SDCA_CLOCK_TYPE_EXTERNAL			= 0x00,
	SDCA_CLOCK_TYPE_INTERNAL_ASYNC			= 0x01,
	SDCA_CLOCK_TYPE_INTERNAL_SYNC			= 0x02,
	SDCA_CLOCK_TYPE_INTERNAL_SOURCE_SYNC		= 0x03,
};

/**
 * struct sdca_entity_cs - information specific to Clock Source Entities
 * @type: Synchronicity of the Clock Source.
 * @max_delay: The maximum delay in microseconds before the clock is stable.
 */
struct sdca_entity_cs {
	enum sdca_clock_type type;
	unsigned int max_delay;
};

/**
 * enum sdca_pde_power_state - SDCA Power States
 *
 * SDCA Power State values from SDCA specification v1.0 Section 7.12.4.
 */
enum sdca_pde_power_state {
	SDCA_PDE_PS0					= 0x0,
	SDCA_PDE_PS1					= 0x1,
	SDCA_PDE_PS2					= 0x2,
	SDCA_PDE_PS3					= 0x3,
	SDCA_PDE_PS4					= 0x4,
};

/**
 * struct sdca_pde_delay - describes the delay changing between 2 power states
 * @from_ps: The power state being exited.
 * @to_ps: The power state being entered.
 * @us: The delay in microseconds switching between the two states.
 */
struct sdca_pde_delay {
	int from_ps;
	int to_ps;
	unsigned int us;
};

/**
 * struct sdca_entity_pde - information specific to Power Domain Entities
 * @managed: Dynamically allocated array pointing to each Entity
 * controlled by this PDE.
 * @max_delay: Dynamically allocated array of delays for switching
 * between power states.
 * @num_managed: Number of Entities controlled by this PDE.
 * @num_max_delay: Number of delays specified for state changes.
 */
struct sdca_entity_pde {
	struct sdca_entity **managed;
	struct sdca_pde_delay *max_delay;
	int num_managed;
	int num_max_delay;
};

/**
 * enum sdca_entity_type - SDCA Entity Type codes
 * @SDCA_ENTITY_TYPE_ENTITY_0: Entity 0, not actually from the
 * specification but useful internally as an Entity structure
 * is allocated for Entity 0, to hold Entity 0 controls.
 * @SDCA_ENTITY_TYPE_IT: Input Terminal.
 * @SDCA_ENTITY_TYPE_OT: Output Terminal.
 * @SDCA_ENTITY_TYPE_MU: Mixer Unit.
 * @SDCA_ENTITY_TYPE_SU: Selector Unit.
 * @SDCA_ENTITY_TYPE_FU: Feature Unit.
 * @SDCA_ENTITY_TYPE_XU: Extension Unit.
 * @SDCA_ENTITY_TYPE_CS: Clock Source.
 * @SDCA_ENTITY_TYPE_CX: Clock selector.
 * @SDCA_ENTITY_TYPE_PDE: Power-Domain Entity.
 * @SDCA_ENTITY_TYPE_GE: Group Entity.
 * @SDCA_ENTITY_TYPE_SPE: Security & Privacy Entity.
 * @SDCA_ENTITY_TYPE_CRU: Channel Remapping Unit.
 * @SDCA_ENTITY_TYPE_UDMPU: Up-Down Mixer Processing Unit.
 * @SDCA_ENTITY_TYPE_MFPU: Multi-Function Processing Unit.
 * @SDCA_ENTITY_TYPE_SMPU: Smart Microphone Processing Unit.
 * @SDCA_ENTITY_TYPE_SAPU: Smart Amp Processing Unit.
 * @SDCA_ENTITY_TYPE_PPU: Posture Processing Unit.
 * @SDCA_ENTITY_TYPE_TG: Tone Generator.
 * @SDCA_ENTITY_TYPE_HIDE: Human Interface Device Entity.
 *
 * SDCA Entity Types from SDCA specification v1.0 Section 6.1.2
 * all Entity Types not described are reserved.
 */
enum sdca_entity_type {
	SDCA_ENTITY_TYPE_ENTITY_0			= 0x00,
	SDCA_ENTITY_TYPE_IT				= 0x02,
	SDCA_ENTITY_TYPE_OT				= 0x03,
	SDCA_ENTITY_TYPE_MU				= 0x05,
	SDCA_ENTITY_TYPE_SU				= 0x06,
	SDCA_ENTITY_TYPE_FU				= 0x07,
	SDCA_ENTITY_TYPE_XU				= 0x0A,
	SDCA_ENTITY_TYPE_CS				= 0x0B,
	SDCA_ENTITY_TYPE_CX				= 0x0C,
	SDCA_ENTITY_TYPE_PDE				= 0x11,
	SDCA_ENTITY_TYPE_GE				= 0x12,
	SDCA_ENTITY_TYPE_SPE				= 0x13,
	SDCA_ENTITY_TYPE_CRU				= 0x20,
	SDCA_ENTITY_TYPE_UDMPU				= 0x21,
	SDCA_ENTITY_TYPE_MFPU				= 0x22,
	SDCA_ENTITY_TYPE_SMPU				= 0x23,
	SDCA_ENTITY_TYPE_SAPU				= 0x24,
	SDCA_ENTITY_TYPE_PPU				= 0x25,
	SDCA_ENTITY_TYPE_TG				= 0x30,
	SDCA_ENTITY_TYPE_HIDE				= 0x31,
};

/**
 * struct sdca_ge_control - control entry in the affected controls list
 * @id: Entity ID of the Control affected.
 * @sel: Control Selector of the Control affected.
 * @cn: Control Number of the Control affected.
 * @val: Value written to Control for this Mode.
 */
struct sdca_ge_control {
	int id;
	int sel;
	int cn;
	int val;
};

/**
 * struct sdca_ge_mode - mode entry in the affected controls list
 * @controls: Dynamically allocated array of controls written for this Mode.
 * @num_controls: Number of controls written in this Mode.
 * @val: GE Selector Mode value.
 */
struct sdca_ge_mode {
	struct sdca_ge_control *controls;
	int num_controls;
	int val;
};

/**
 * struct sdca_entity_ge - information specific to Group Entities
 * @kctl: ALSA control pointer that can be used by linked Entities.
 * @modes: Dynamically allocated array of Modes and the Controls written
 * in each mode.
 * @num_modes: Number of Modes.
 */
struct sdca_entity_ge {
	struct snd_kcontrol_new *kctl;
	struct sdca_ge_mode *modes;
	int num_modes;
};

/**
 * struct sdca_entity_hide - information specific to HIDE Entities
 * @hid: HID device structure
 * @num_hidtx_ids: number of HIDTx Report ID
 * @num_hidrx_ids: number of HIDRx Report ID
 * @hidtx_ids: HIDTx Report ID
 * @hidrx_ids: HIDRx Report ID
 * @af_number_list: which Audio Function Numbers within this Device are
 * sending/receiving the messages in this HIDE
 * @hide_reside_function_num: indicating which Audio Function Numbers
 * within this Device
 * @max_delay: the maximum time in microseconds allowed for the Device
 * to change the ownership from Device to Host
 * @hid_report_desc: HID Report Descriptor for the HIDE Entity
 * @hid_desc: HID descriptor for the HIDE Entity
 */
struct sdca_entity_hide {
	struct hid_device *hid;
	unsigned int *hidtx_ids;
	unsigned int *hidrx_ids;
	int num_hidtx_ids;
	int num_hidrx_ids;
	unsigned int af_number_list[SDCA_MAX_FUNCTION_COUNT];
	unsigned int hide_reside_function_num;
	unsigned int max_delay;
	unsigned char *hid_report_desc;
	struct hid_descriptor hid_desc;
};

/**
 * struct sdca_entity - information for one SDCA Entity
 * @label: String such as "OT 12".
 * @id: Identifier used for addressing.
 * @type: Type code for the Entity.
 * @group: Pointer to Group Entity controlling this one, NULL if N/A.
 * @sources: Dynamically allocated array pointing to each input Entity
 * connected to this Entity.
 * @controls: Dynamically allocated array of Controls.
 * @num_sources: Number of sources for the Entity.
 * @num_controls: Number of Controls for the Entity.
 * @iot: Input/Output Terminal specific Entity properties.
 * @cs: Clock Source specific Entity properties.
 * @pde: Power Domain Entity specific Entity properties.
 * @ge: Group Entity specific Entity properties.
 * @hide: HIDE Entity specific Entity properties.
 */
struct sdca_entity {
	const char *label;
	int id;
	enum sdca_entity_type type;

	struct sdca_entity *group;
	struct sdca_entity **sources;
	struct sdca_control *controls;
	int num_sources;
	int num_controls;
	union {
		struct sdca_entity_iot iot;
		struct sdca_entity_cs cs;
		struct sdca_entity_pde pde;
		struct sdca_entity_ge ge;
		struct sdca_entity_hide hide;
	};
};

/**
 * enum sdca_channel_purpose - SDCA Channel Purpose code
 *
 * Channel Purpose codes as described in the SDCA specification v1.0
 * section 11.4.3.
 */
enum sdca_channel_purpose {
	/* Table 210 - Purpose */
	SDCA_CHAN_PURPOSE_GENERIC_AUDIO			= 0x01,
	SDCA_CHAN_PURPOSE_VOICE				= 0x02,
	SDCA_CHAN_PURPOSE_SPEECH			= 0x03,
	SDCA_CHAN_PURPOSE_AMBIENT			= 0x04,
	SDCA_CHAN_PURPOSE_REFERENCE			= 0x05,
	SDCA_CHAN_PURPOSE_ULTRASOUND			= 0x06,
	SDCA_CHAN_PURPOSE_SENSE				= 0x08,
	SDCA_CHAN_PURPOSE_SILENCE			= 0xFE,
	SDCA_CHAN_PURPOSE_NON_AUDIO			= 0xFF,
	/* Table 211 - Amp Sense */
	SDCA_CHAN_PURPOSE_SENSE_V1			= 0x09,
	SDCA_CHAN_PURPOSE_SENSE_V2			= 0x0A,
	SDCA_CHAN_PURPOSE_SENSE_V12_INTERLEAVED		= 0x10,
	SDCA_CHAN_PURPOSE_SENSE_V21_INTERLEAVED		= 0x11,
	SDCA_CHAN_PURPOSE_SENSE_V12_PACKED		= 0x12,
	SDCA_CHAN_PURPOSE_SENSE_V21_PACKED		= 0x13,
	SDCA_CHAN_PURPOSE_SENSE_V1212_INTERLEAVED	= 0x14,
	SDCA_CHAN_PURPOSE_SENSE_V2121_INTERLEAVED	= 0x15,
	SDCA_CHAN_PURPOSE_SENSE_V1122_INTERLEAVED	= 0x16,
	SDCA_CHAN_PURPOSE_SENSE_V2211_INTERLEAVED	= 0x17,
	SDCA_CHAN_PURPOSE_SENSE_V1212_PACKED		= 0x18,
	SDCA_CHAN_PURPOSE_SENSE_V2121_PACKED		= 0x19,
	SDCA_CHAN_PURPOSE_SENSE_V1122_PACKED		= 0x1A,
	SDCA_CHAN_PURPOSE_SENSE_V2211_PACKED		= 0x1B,
};

/**
 * enum sdca_channel_relationship - SDCA Channel Relationship code
 *
 * Channel Relationship codes as described in the SDCA specification
 * v1.0 section 11.4.2.
 */
enum sdca_channel_relationship {
	/* Table 206 - Streaming */
	SDCA_CHAN_REL_UNDEFINED				= 0x00,
	SDCA_CHAN_REL_GENERIC_MONO			= 0x01,
	SDCA_CHAN_REL_GENERIC_LEFT			= 0x02,
	SDCA_CHAN_REL_GENERIC_RIGHT			= 0x03,
	SDCA_CHAN_REL_GENERIC_TOP			= 0x48,
	SDCA_CHAN_REL_GENERIC_BOTTOM			= 0x49,
	SDCA_CHAN_REL_CAPTURE_DIRECT			= 0x4E,
	SDCA_CHAN_REL_RENDER_DIRECT			= 0x4F,
	SDCA_CHAN_REL_FRONT_LEFT			= 0x0B,
	SDCA_CHAN_REL_FRONT_RIGHT			= 0x0C,
	SDCA_CHAN_REL_FRONT_CENTER			= 0x0D,
	SDCA_CHAN_REL_SIDE_LEFT				= 0x12,
	SDCA_CHAN_REL_SIDE_RIGHT			= 0x13,
	SDCA_CHAN_REL_BACK_LEFT				= 0x16,
	SDCA_CHAN_REL_BACK_RIGHT			= 0x17,
	SDCA_CHAN_REL_LOW_FREQUENCY_EFFECTS		= 0x43,
	SDCA_CHAN_REL_SOUNDWIRE_MIC			= 0x55,
	SDCA_CHAN_REL_SENSE_TRANSDUCER_1		= 0x58,
	SDCA_CHAN_REL_SENSE_TRANSDUCER_2		= 0x59,
	SDCA_CHAN_REL_SENSE_TRANSDUCER_12		= 0x5A,
	SDCA_CHAN_REL_SENSE_TRANSDUCER_21		= 0x5B,
	SDCA_CHAN_REL_ECHOREF_NONE			= 0x70,
	SDCA_CHAN_REL_ECHOREF_1				= 0x71,
	SDCA_CHAN_REL_ECHOREF_2				= 0x72,
	SDCA_CHAN_REL_ECHOREF_3				= 0x73,
	SDCA_CHAN_REL_ECHOREF_4				= 0x74,
	SDCA_CHAN_REL_ECHOREF_ALL			= 0x75,
	SDCA_CHAN_REL_ECHOREF_LFE_ALL			= 0x76,
	/* Table 207 - Speaker */
	SDCA_CHAN_REL_PRIMARY_TRANSDUCER		= 0x50,
	SDCA_CHAN_REL_SECONDARY_TRANSDUCER		= 0x51,
	SDCA_CHAN_REL_TERTIARY_TRANSDUCER		= 0x52,
	SDCA_CHAN_REL_LOWER_LEFT_ALLTRANSDUCER		= 0x60,
	SDCA_CHAN_REL_LOWER_RIGHT_ALLTRANSDUCER		= 0x61,
	SDCA_CHAN_REL_UPPER_LEFT_ALLTRANSDUCER		= 0x62,
	SDCA_CHAN_REL_UPPER_RIGHT_ALLTRANSDUCER		= 0x63,
	SDCA_CHAN_REL_LOWER_LEFT_PRIMARY		= 0x64,
	SDCA_CHAN_REL_LOWER_RIGHT_PRIMARY		= 0x65,
	SDCA_CHAN_REL_UPPER_LEFT_PRIMARY		= 0x66,
	SDCA_CHAN_REL_UPPER_RIGHT_PRIMARY		= 0x67,
	SDCA_CHAN_REL_LOWER_LEFT_SECONDARY		= 0x68,
	SDCA_CHAN_REL_LOWER_RIGHT_SECONDARY		= 0x69,
	SDCA_CHAN_REL_UPPER_LEFT_SECONDARY		= 0x6A,
	SDCA_CHAN_REL_UPPER_RIGHT_SECONDARY		= 0x6B,
	SDCA_CHAN_REL_LOWER_LEFT_TERTIARY		= 0x6C,
	SDCA_CHAN_REL_LOWER_RIGHT_TERTIARY		= 0x6D,
	SDCA_CHAN_REL_UPPER_LEFT_TERTIARY		= 0x6E,
	SDCA_CHAN_REL_UPPER_RIGHT_TERTIARY		= 0x6F,
	SDCA_CHAN_REL_DERIVED_LOWER_LEFT_PRIMARY	= 0x94,
	SDCA_CHAN_REL_DERIVED_LOWER_RIGHT_PRIMARY	= 0x95,
	SDCA_CHAN_REL_DERIVED_UPPER_LEFT_PRIMARY	= 0x96,
	SDCA_CHAN_REL_DERIVED_UPPER_RIGHT_PRIMARY	= 0x97,
	SDCA_CHAN_REL_DERIVED_LOWER_LEFT_SECONDARY	= 0x98,
	SDCA_CHAN_REL_DERIVED_LOWER_RIGHT_SECONDARY	= 0x99,
	SDCA_CHAN_REL_DERIVED_UPPER_LEFT_SECONDARY	= 0x9A,
	SDCA_CHAN_REL_DERIVED_UPPER_RIGHT_SECONDARY	= 0x9B,
	SDCA_CHAN_REL_DERIVED_LOWER_LEFT_TERTIARY	= 0x9C,
	SDCA_CHAN_REL_DERIVED_LOWER_RIGHT_TERTIARY	= 0x9D,
	SDCA_CHAN_REL_DERIVED_UPPER_LEFT_TERTIARY	= 0x9E,
	SDCA_CHAN_REL_DERIVED_UPPER_RIGHT_TERTIARY	= 0x9F,
	SDCA_CHAN_REL_DERIVED_MONO_PRIMARY		= 0xA0,
	SDCA_CHAN_REL_DERIVED_MONO_SECONDARY		= 0xAB,
	SDCA_CHAN_REL_DERIVED_MONO_TERTIARY		= 0xAC,
	/* Table 208 - Equipment */
	SDCA_CHAN_REL_EQUIPMENT_LEFT			= 0x02,
	SDCA_CHAN_REL_EQUIPMENT_RIGHT			= 0x03,
	SDCA_CHAN_REL_EQUIPMENT_COMBINED		= 0x47,
	SDCA_CHAN_REL_EQUIPMENT_TOP			= 0x48,
	SDCA_CHAN_REL_EQUIPMENT_BOTTOM			= 0x49,
	SDCA_CHAN_REL_EQUIPMENT_TOP_LEFT		= 0x4A,
	SDCA_CHAN_REL_EQUIPMENT_BOTTOM_LEFT		= 0x4B,
	SDCA_CHAN_REL_EQUIPMENT_TOP_RIGHT		= 0x4C,
	SDCA_CHAN_REL_EQUIPMENT_BOTTOM_RIGHT		= 0x4D,
	SDCA_CHAN_REL_EQUIPMENT_SILENCED_OUTPUT		= 0x57,
	/* Table 209 - Other */
	SDCA_CHAN_REL_ARRAY				= 0x04,
	SDCA_CHAN_REL_MIC				= 0x53,
	SDCA_CHAN_REL_RAW				= 0x54,
	SDCA_CHAN_REL_SILENCED_MIC			= 0x56,
	SDCA_CHAN_REL_MULTI_SOURCE_1			= 0x78,
	SDCA_CHAN_REL_MULTI_SOURCE_2			= 0x79,
	SDCA_CHAN_REL_MULTI_SOURCE_3			= 0x7A,
	SDCA_CHAN_REL_MULTI_SOURCE_4			= 0x7B,
};

/**
 * struct sdca_channel - a single Channel with a Cluster
 * @id: Identifier used for addressing.
 * @purpose: Indicates the purpose of the Channel, usually to give
 * semantic meaning to the audio, eg. voice, ultrasound.
 * @relationship: Indicates the relationship of this Channel to others
 * in the Cluster, often used to identify the physical position of the
 * Channel eg. left.
 */
struct sdca_channel {
	int id;
	enum sdca_channel_purpose purpose;
	enum sdca_channel_relationship relationship;
};

/**
 * struct sdca_cluster - information about an SDCA Channel Cluster
 * @id: Identifier used for addressing.
 * @num_channels: Number of Channels within this Cluster.
 * @channels: Dynamically allocated array of Channels.
 */
struct sdca_cluster {
	int id;
	int num_channels;
	struct sdca_channel *channels;
};

/**
 * enum sdca_cluster_range - SDCA Range column definitions for ClusterIndex
 */
enum sdca_cluster_range {
	SDCA_CLUSTER_BYTEINDEX				= 0,
	SDCA_CLUSTER_CLUSTERID				= 1,
	SDCA_CLUSTER_NCOLS				= 2,
};

/**
 * struct sdca_function_data - top-level information for one SDCA function
 * @desc: Pointer to short descriptor from initial parsing.
 * @init_table: Pointer to a table of initialization writes.
 * @entities: Dynamically allocated array of Entities.
 * @clusters: Dynamically allocated array of Channel Clusters.
 * @num_init_table: Number of initialization writes.
 * @num_entities: Number of Entities reported in this Function.
 * @num_clusters: Number of Channel Clusters reported in this Function.
 * @busy_max_delay: Maximum Function busy delay in microseconds, before an
 * error should be reported.
 */
struct sdca_function_data {
	struct sdca_function_desc *desc;

	struct sdca_init_write *init_table;
	struct sdca_entity *entities;
	struct sdca_cluster *clusters;
	int num_init_table;
	int num_entities;
	int num_clusters;

	unsigned int busy_max_delay;
};

static inline u32 sdca_range(struct sdca_control_range *range,
			     unsigned int col, unsigned int row)
{
	return range->data[(row * range->cols) + col];
}

static inline u32 sdca_range_search(struct sdca_control_range *range,
				    int search_col, int value, int result_col)
{
	int i;

	for (i = 0; i < range->rows; i++) {
		if (sdca_range(range, search_col, i) == value)
			return sdca_range(range, result_col, i);
	}

	return 0;
}

int sdca_parse_function(struct device *dev,
			struct sdca_function_desc *desc,
			struct sdca_function_data *function);

struct sdca_control *sdca_selector_find_control(struct device *dev,
						struct sdca_entity *entity,
						const int sel);
struct sdca_control_range *sdca_control_find_range(struct device *dev,
						   struct sdca_entity *entity,
						   struct sdca_control *control,
						   int cols, int rows);
struct sdca_control_range *sdca_selector_find_range(struct device *dev,
						    struct sdca_entity *entity,
						    int sel, int cols, int rows);
struct sdca_cluster *sdca_id_find_cluster(struct device *dev,
					  struct sdca_function_data *function,
					  const int id);

#endif
