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

/**
 * enum sdca_entity_type - SDCA Entity Type codes
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
 * @num_sources: Number of sources for the Entity.
 */
struct sdca_entity {
	const char *label;
	int id;
	enum sdca_entity_type type;

	struct sdca_entity **sources;
	int num_sources;
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
