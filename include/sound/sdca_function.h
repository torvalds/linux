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

struct device;
struct sdca_function_desc;

/*
 * The addressing space for SDCA relies on 7 bits for Entities, so a
 * maximum of 128 Entities per function can be represented.
 */
#define SDCA_MAX_ENTITY_COUNT 128

/*
 * Sanity check on number of initialization writes, can be expanded if needed.
 */
#define SDCA_MAX_INIT_COUNT 2048

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
 * SDAC_CTL_TYPE_S(IT, MIC_BIAS).
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
 * struct sdca_control - information for one SDCA Control
 * @label: Name for the Control, from SDCA Specification v1.0, section 7.1.7.
 * @sel: Identifier used for addressing.
 * @value: Holds the Control value for constants and defaults.
 * @nbits: Number of bits used in the Control.
 * @interrupt_position: SCDA interrupt line that will alert to changes on this
 * Control.
 * @cn_list: A bitmask showing the valid Control Numbers within this Control,
 * Control Numbers typically represent channels.
 * @mode: Access mode of the Control.
 * @layers: Bitmask of access layers of the Control.
 * @deferrable: Indicates if the access to the Control can be deferred.
 * @has_default: Indicates the Control has a default value to be written.
 * @has_fixed: Indicates the Control only supports a single value.
 */
struct sdca_control {
	const char *label;
	int sel;

	int value;
	int nbits;
	int interrupt_position;
	u64 cn_list;

	enum sdca_access_mode mode;
	u8 layers;

	bool deferrable;
	bool has_default;
	bool has_fixed;
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
 * struct sdca_entity - information for one SDCA Entity
 * @label: String such as "OT 12".
 * @id: Identifier used for addressing.
 * @type: Type code for the Entity.
 * @sources: Dynamically allocated array pointing to each input Entity
 * connected to this Entity.
 * @controls: Dynamically allocated array of Controls.
 * @num_sources: Number of sources for the Entity.
 * @num_controls: Number of Controls for the Entity.
 */
struct sdca_entity {
	const char *label;
	int id;
	enum sdca_entity_type type;

	struct sdca_entity **sources;
	struct sdca_control *controls;
	int num_sources;
	int num_controls;
};

/**
 * struct sdca_function_data - top-level information for one SDCA function
 * @desc: Pointer to short descriptor from initial parsing.
 * @init_table: Pointer to a table of initialization writes.
 * @entities: Dynamically allocated array of Entities.
 * @num_init_table: Number of initialization writes.
 * @num_entities: Number of Entities reported in this Function.
 * @busy_max_delay: Maximum Function busy delay in microseconds, before an
 * error should be reported.
 */
struct sdca_function_data {
	struct sdca_function_desc *desc;

	struct sdca_init_write *init_table;
	struct sdca_entity *entities;
	int num_init_table;
	int num_entities;

	unsigned int busy_max_delay;
};

int sdca_parse_function(struct device *dev,
			struct sdca_function_desc *desc,
			struct sdca_function_data *function);

#endif
