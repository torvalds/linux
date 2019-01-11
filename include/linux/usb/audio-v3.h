// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2017 Ruslan Bilovol <ruslan.bilovol@gmail.com>
 *
 * This file holds USB constants and structures defined
 * by the USB DEVICE CLASS DEFINITION FOR AUDIO DEVICES Release 3.0.
 */

#ifndef __LINUX_USB_AUDIO_V3_H
#define __LINUX_USB_AUDIO_V3_H

#include <linux/types.h>

/*
 * v1.0, v2.0 and v3.0 of this standard have many things in common. For the rest
 * of the definitions, please refer to audio.h and audio-v2.h
 */

/* All High Capability descriptors have these 2 fields at the beginning */
struct uac3_hc_descriptor_header {
	__le16 wLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__le16 wDescriptorID;
} __attribute__ ((packed));

/* 4.3.1 CLUSTER DESCRIPTOR HEADER */
struct uac3_cluster_header_descriptor {
	__le16 wLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__le16 wDescriptorID;
	__u8 bNrChannels;
} __attribute__ ((packed));

/* 4.3.2.1 SEGMENTS */
struct uac3_cluster_segment_descriptor {
	__le16 wLength;
	__u8 bSegmentType;
	/* __u8[0]; segment-specific data */
} __attribute__ ((packed));

/* 4.3.2.1.1 END SEGMENT */
struct uac3_cluster_end_segment_descriptor {
	__le16 wLength;
	__u8 bSegmentType;		/* Constant END_SEGMENT */
} __attribute__ ((packed));

/* 4.3.2.1.3.1 INFORMATION SEGMENT */
struct uac3_cluster_information_segment_descriptor {
	__le16 wLength;
	__u8 bSegmentType;
	__u8 bChPurpose;
	__u8 bChRelationship;
	__u8 bChGroupID;
} __attribute__ ((packed));

/* 4.5.2 CLASS-SPECIFIC AC INTERFACE DESCRIPTOR */
struct uac3_ac_header_descriptor {
	__u8 bLength;			/* 10 */
	__u8 bDescriptorType;		/* CS_INTERFACE descriptor type */
	__u8 bDescriptorSubtype;	/* HEADER descriptor subtype */
	__u8 bCategory;

	/* includes Clock Source, Unit, Terminal, and Power Domain desc. */
	__le16 wTotalLength;

	__le32 bmControls;
} __attribute__ ((packed));

/* 4.5.2.1 INPUT TERMINAL DESCRIPTOR */
struct uac3_input_terminal_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bTerminalID;
	__le16 wTerminalType;
	__u8 bAssocTerminal;
	__u8 bCSourceID;
	__le32 bmControls;
	__le16 wClusterDescrID;
	__le16 wExTerminalDescrID;
	__le16 wConnectorsDescrID;
	__le16 wTerminalDescrStr;
} __attribute__((packed));

/* 4.5.2.2 OUTPUT TERMINAL DESCRIPTOR */
struct uac3_output_terminal_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bTerminalID;
	__le16 wTerminalType;
	__u8 bAssocTerminal;
	__u8 bSourceID;
	__u8 bCSourceID;
	__le32 bmControls;
	__le16 wExTerminalDescrID;
	__le16 wConnectorsDescrID;
	__le16 wTerminalDescrStr;
} __attribute__((packed));

/* 4.5.2.7 FEATURE UNIT DESCRIPTOR */
struct uac3_feature_unit_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bUnitID;
	__u8 bSourceID;
	/* bmaControls is actually u32,
	 * but u8 is needed for the hybrid parser */
	__u8 bmaControls[0]; /* variable length */
	/* wFeatureDescrStr omitted */
} __attribute__((packed));

#define UAC3_DT_FEATURE_UNIT_SIZE(ch)		(7 + ((ch) + 1) * 4)

/* As above, but more useful for defining your own descriptors */
#define DECLARE_UAC3_FEATURE_UNIT_DESCRIPTOR(ch)		\
struct uac3_feature_unit_descriptor_##ch {			\
	__u8 bLength;						\
	__u8 bDescriptorType;					\
	__u8 bDescriptorSubtype;				\
	__u8 bUnitID;						\
	__u8 bSourceID;						\
	__le32 bmaControls[ch + 1];				\
	__le16 wFeatureDescrStr;				\
} __attribute__ ((packed))

/* 4.5.2.12 CLOCK SOURCE DESCRIPTOR */
struct uac3_clock_source_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bClockID;
	__u8 bmAttributes;
	__le32 bmControls;
	__u8 bReferenceTerminal;
	__le16 wClockSourceStr;
} __attribute__((packed));

/* bmAttribute fields */
#define UAC3_CLOCK_SOURCE_TYPE_EXT	0x0
#define UAC3_CLOCK_SOURCE_TYPE_INT	0x1
#define UAC3_CLOCK_SOURCE_ASYNC		(0 << 2)
#define UAC3_CLOCK_SOURCE_SYNCED_TO_SOF	(1 << 1)

/* 4.5.2.13 CLOCK SELECTOR DESCRIPTOR */
struct uac3_clock_selector_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bClockID;
	__u8 bNrInPins;
	__u8 baCSourceID[];
	/* bmControls and wCSelectorDescrStr omitted */
} __attribute__((packed));

/* 4.5.2.14 CLOCK MULTIPLIER DESCRIPTOR */
struct uac3_clock_multiplier_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bClockID;
	__u8 bCSourceID;
	__le32 bmControls;
	__le16 wCMultiplierDescrStr;
} __attribute__((packed));

/* 4.5.2.15 POWER DOMAIN DESCRIPTOR */
struct uac3_power_domain_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bPowerDomainID;
	__le16 waRecoveryTime1;
	__le16 waRecoveryTime2;
	__u8 bNrEntities;
	__u8 baEntityID[];
	/* wPDomainDescrStr omitted */
} __attribute__((packed));

/* As above, but more useful for defining your own descriptors */
#define DECLARE_UAC3_POWER_DOMAIN_DESCRIPTOR(n)			\
struct uac3_power_domain_descriptor_##n {			\
	__u8 bLength;						\
	__u8 bDescriptorType;					\
	__u8 bDescriptorSubtype;				\
	__u8 bPowerDomainID;					\
	__le16 waRecoveryTime1;					\
	__le16 waRecoveryTime2;					\
	__u8 bNrEntities;					\
	__u8 baEntityID[n];					\
	__le16 wPDomainDescrStr;					\
} __attribute__ ((packed))

/* 4.7.2 CLASS-SPECIFIC AS INTERFACE DESCRIPTOR */
struct uac3_as_header_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bTerminalLink;
	__le32 bmControls;
	__le16 wClusterDescrID;
	__le64 bmFormats;
	__u8 bSubslotSize;
	__u8 bBitResolution;
	__le16 bmAuxProtocols;
	__u8 bControlSize;
} __attribute__((packed));

#define UAC3_FORMAT_TYPE_I_RAW_DATA	(1 << 6)

/* 4.8.1.2 CLASS-SPECIFIC AS ISOCHRONOUS AUDIO DATA ENDPOINT DESCRIPTOR */
struct uac3_iso_endpoint_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__le32 bmControls;
	__u8 bLockDelayUnits;
	__le16 wLockDelay;
} __attribute__((packed));

/* 5.2.1.6.1 INSERTION CONTROL PARAMETER BLOCK */
struct uac3_insertion_ctl_blk {
	__u8 bSize;
	__u8 bmConInserted;
} __attribute__ ((packed));

/* 6.1 INTERRUPT DATA MESSAGE */
struct uac3_interrupt_data_msg {
	__u8 bInfo;
	__u8 bSourceType;
	__le16 wValue;
	__le16 wIndex;
} __attribute__((packed));

/* A.2 AUDIO AUDIO FUNCTION SUBCLASS CODES */
#define UAC3_FUNCTION_SUBCLASS_UNDEFINED	0x00
#define UAC3_FUNCTION_SUBCLASS_FULL_ADC_3_0	0x01
/* BADD profiles */
#define UAC3_FUNCTION_SUBCLASS_GENERIC_IO	0x20
#define UAC3_FUNCTION_SUBCLASS_HEADPHONE	0x21
#define UAC3_FUNCTION_SUBCLASS_SPEAKER		0x22
#define UAC3_FUNCTION_SUBCLASS_MICROPHONE	0x23
#define UAC3_FUNCTION_SUBCLASS_HEADSET		0x24
#define UAC3_FUNCTION_SUBCLASS_HEADSET_ADAPTER	0x25
#define UAC3_FUNCTION_SUBCLASS_SPEAKERPHONE	0x26

/* A.7 AUDIO FUNCTION CATEGORY CODES */
#define UAC3_FUNCTION_SUBCLASS_UNDEFINED	0x00
#define UAC3_FUNCTION_DESKTOP_SPEAKER		0x01
#define UAC3_FUNCTION_HOME_THEATER		0x02
#define UAC3_FUNCTION_MICROPHONE		0x03
#define UAC3_FUNCTION_HEADSET			0x04
#define UAC3_FUNCTION_TELEPHONE			0x05
#define UAC3_FUNCTION_CONVERTER			0x06
#define UAC3_FUNCTION_SOUND_RECORDER		0x07
#define UAC3_FUNCTION_IO_BOX			0x08
#define UAC3_FUNCTION_MUSICAL_INSTRUMENT	0x09
#define UAC3_FUNCTION_PRO_AUDIO			0x0a
#define UAC3_FUNCTION_AUDIO_VIDEO		0x0b
#define UAC3_FUNCTION_CONTROL_PANEL		0x0c
#define UAC3_FUNCTION_HEADPHONE			0x0d
#define UAC3_FUNCTION_GENERIC_SPEAKER		0x0e
#define UAC3_FUNCTION_HEADSET_ADAPTER		0x0f
#define UAC3_FUNCTION_SPEAKERPHONE		0x10
#define UAC3_FUNCTION_OTHER			0xff

/* A.8 AUDIO CLASS-SPECIFIC DESCRIPTOR TYPES */
#define UAC3_CS_UNDEFINED		0x20
#define UAC3_CS_DEVICE			0x21
#define UAC3_CS_CONFIGURATION		0x22
#define UAC3_CS_STRING			0x23
#define UAC3_CS_INTERFACE		0x24
#define UAC3_CS_ENDPOINT		0x25
#define UAC3_CS_CLUSTER			0x26

/* A.10 CLUSTER DESCRIPTOR SEGMENT TYPES */
#define UAC3_SEGMENT_UNDEFINED		0x00
#define UAC3_CLUSTER_DESCRIPTION	0x01
#define UAC3_CLUSTER_VENDOR_DEFINED	0x1F
#define UAC3_CHANNEL_INFORMATION	0x20
#define UAC3_CHANNEL_AMBISONIC		0x21
#define UAC3_CHANNEL_DESCRIPTION	0x22
#define UAC3_CHANNEL_VENDOR_DEFINED	0xFE
#define UAC3_END_SEGMENT		0xFF

/* A.11 CHANNEL PURPOSE DEFINITIONS */
#define UAC3_PURPOSE_UNDEFINED		0x00
#define UAC3_PURPOSE_GENERIC_AUDIO	0x01
#define UAC3_PURPOSE_VOICE		0x02
#define UAC3_PURPOSE_SPEECH		0x03
#define UAC3_PURPOSE_AMBIENT		0x04
#define UAC3_PURPOSE_REFERENCE		0x05
#define UAC3_PURPOSE_ULTRASONIC		0x06
#define UAC3_PURPOSE_VIBROKINETIC	0x07
#define UAC3_PURPOSE_NON_AUDIO		0xFF

/* A.12 CHANNEL RELATIONSHIP DEFINITIONS */
#define UAC3_CH_RELATIONSHIP_UNDEFINED	0x00
#define UAC3_CH_MONO			0x01
#define UAC3_CH_LEFT			0x02
#define UAC3_CH_RIGHT			0x03
#define UAC3_CH_ARRAY			0x04
#define UAC3_CH_PATTERN_X		0x20
#define UAC3_CH_PATTERN_Y		0x21
#define UAC3_CH_PATTERN_A		0x22
#define UAC3_CH_PATTERN_B		0x23
#define UAC3_CH_PATTERN_M		0x24
#define UAC3_CH_PATTERN_S		0x25
#define UAC3_CH_FRONT_LEFT		0x80
#define UAC3_CH_FRONT_RIGHT		0x81
#define UAC3_CH_FRONT_CENTER		0x82
#define UAC3_CH_FRONT_LEFT_OF_CENTER	0x83
#define UAC3_CH_FRONT_RIGHT_OF_CENTER	0x84
#define UAC3_CH_FRONT_WIDE_LEFT		0x85
#define UAC3_CH_FRONT_WIDE_RIGHT	0x86
#define UAC3_CH_SIDE_LEFT		0x87
#define UAC3_CH_SIDE_RIGHT		0x88
#define UAC3_CH_SURROUND_ARRAY_LEFT	0x89
#define UAC3_CH_SURROUND_ARRAY_RIGHT	0x8A
#define UAC3_CH_BACK_LEFT		0x8B
#define UAC3_CH_BACK_RIGHT		0x8C
#define UAC3_CH_BACK_CENTER		0x8D
#define UAC3_CH_BACK_LEFT_OF_CENTER	0x8E
#define UAC3_CH_BACK_RIGHT_OF_CENTER	0x8F
#define UAC3_CH_BACK_WIDE_LEFT		0x90
#define UAC3_CH_BACK_WIDE_RIGHT		0x91
#define UAC3_CH_TOP_CENTER		0x92
#define UAC3_CH_TOP_FRONT_LEFT		0x93
#define UAC3_CH_TOP_FRONT_RIGHT		0x94
#define UAC3_CH_TOP_FRONT_CENTER	0x95
#define UAC3_CH_TOP_FRONT_LOC		0x96
#define UAC3_CH_TOP_FRONT_ROC		0x97
#define UAC3_CH_TOP_FRONT_WIDE_LEFT	0x98
#define UAC3_CH_TOP_FRONT_WIDE_RIGHT	0x99
#define UAC3_CH_TOP_SIDE_LEFT		0x9A
#define UAC3_CH_TOP_SIDE_RIGHT		0x9B
#define UAC3_CH_TOP_SURR_ARRAY_LEFT	0x9C
#define UAC3_CH_TOP_SURR_ARRAY_RIGHT	0x9D
#define UAC3_CH_TOP_BACK_LEFT		0x9E
#define UAC3_CH_TOP_BACK_RIGHT		0x9F
#define UAC3_CH_TOP_BACK_CENTER		0xA0
#define UAC3_CH_TOP_BACK_LOC		0xA1
#define UAC3_CH_TOP_BACK_ROC		0xA2
#define UAC3_CH_TOP_BACK_WIDE_LEFT	0xA3
#define UAC3_CH_TOP_BACK_WIDE_RIGHT	0xA4
#define UAC3_CH_BOTTOM_CENTER		0xA5
#define UAC3_CH_BOTTOM_FRONT_LEFT	0xA6
#define UAC3_CH_BOTTOM_FRONT_RIGHT	0xA7
#define UAC3_CH_BOTTOM_FRONT_CENTER	0xA8
#define UAC3_CH_BOTTOM_FRONT_LOC	0xA9
#define UAC3_CH_BOTTOM_FRONT_ROC	0xAA
#define UAC3_CH_BOTTOM_FRONT_WIDE_LEFT	0xAB
#define UAC3_CH_BOTTOM_FRONT_WIDE_RIGHT	0xAC
#define UAC3_CH_BOTTOM_SIDE_LEFT	0xAD
#define UAC3_CH_BOTTOM_SIDE_RIGHT	0xAE
#define UAC3_CH_BOTTOM_SURR_ARRAY_LEFT	0xAF
#define UAC3_CH_BOTTOM_SURR_ARRAY_RIGHT	0xB0
#define UAC3_CH_BOTTOM_BACK_LEFT	0xB1
#define UAC3_CH_BOTTOM_BACK_RIGHT	0xB2
#define UAC3_CH_BOTTOM_BACK_CENTER	0xB3
#define UAC3_CH_BOTTOM_BACK_LOC		0xB4
#define UAC3_CH_BOTTOM_BACK_ROC		0xB5
#define UAC3_CH_BOTTOM_BACK_WIDE_LEFT	0xB6
#define UAC3_CH_BOTTOM_BACK_WIDE_RIGHT	0xB7
#define UAC3_CH_LOW_FREQUENCY_EFFECTS	0xB8
#define UAC3_CH_LFE_LEFT		0xB9
#define UAC3_CH_LFE_RIGHT		0xBA
#define UAC3_CH_HEADPHONE_LEFT		0xBB
#define UAC3_CH_HEADPHONE_RIGHT		0xBC

/* A.15 AUDIO CLASS-SPECIFIC AC INTERFACE DESCRIPTOR SUBTYPES */
/* see audio.h for the rest, which is identical to v1 */
#define UAC3_EXTENDED_TERMINAL		0x04
#define UAC3_MIXER_UNIT			0x05
#define UAC3_SELECTOR_UNIT		0x06
#define UAC3_FEATURE_UNIT		0x07
#define UAC3_EFFECT_UNIT		0x08
#define UAC3_PROCESSING_UNIT		0x09
#define UAC3_EXTENSION_UNIT		0x0a
#define UAC3_CLOCK_SOURCE		0x0b
#define UAC3_CLOCK_SELECTOR		0x0c
#define UAC3_CLOCK_MULTIPLIER		0x0d
#define UAC3_SAMPLE_RATE_CONVERTER	0x0e
#define UAC3_CONNECTORS			0x0f
#define UAC3_POWER_DOMAIN		0x10

/* A.20 PROCESSING UNIT PROCESS TYPES */
#define UAC3_PROCESS_UNDEFINED		0x00
#define UAC3_PROCESS_UP_DOWNMIX		0x01
#define UAC3_PROCESS_STEREO_EXTENDER	0x02
#define UAC3_PROCESS_MULTI_FUNCTION	0x03

/* A.22 AUDIO CLASS-SPECIFIC REQUEST CODES */
/* see audio-v2.h for the rest, which is identical to v2 */
#define UAC3_CS_REQ_INTEN			0x04
#define UAC3_CS_REQ_STRING			0x05
#define UAC3_CS_REQ_HIGH_CAPABILITY_DESCRIPTOR	0x06

/* A.23.1 AUDIOCONTROL INTERFACE CONTROL SELECTORS */
#define UAC3_AC_CONTROL_UNDEFINED		0x00
#define UAC3_AC_ACTIVE_INTERFACE_CONTROL	0x01
#define UAC3_AC_POWER_DOMAIN_CONTROL		0x02

/* A.23.5 TERMINAL CONTROL SELECTORS */
#define UAC3_TE_UNDEFINED			0x00
#define UAC3_TE_INSERTION			0x01
#define UAC3_TE_OVERLOAD			0x02
#define UAC3_TE_UNDERFLOW			0x03
#define UAC3_TE_OVERFLOW			0x04
#define UAC3_TE_LATENCY 			0x05

/* A.23.10 PROCESSING UNITS CONTROL SELECTROS */

/* Up/Down Mixer */
#define UAC3_UD_MODE_SELECT			0x01

/* Stereo Extender */
#define UAC3_EXT_WIDTH_CONTROL			0x01


/* BADD predefined Unit/Terminal values */
#define UAC3_BADD_IT_ID1	1  /* Input Terminal ID1: bTerminalID = 1 */
#define UAC3_BADD_FU_ID2	2  /* Feature Unit ID2: bUnitID = 2 */
#define UAC3_BADD_OT_ID3	3  /* Output Terminal ID3: bTerminalID = 3 */
#define UAC3_BADD_IT_ID4	4  /* Input Terminal ID4: bTerminalID = 4 */
#define UAC3_BADD_FU_ID5	5  /* Feature Unit ID5: bUnitID = 5 */
#define UAC3_BADD_OT_ID6	6  /* Output Terminal ID6: bTerminalID = 6 */
#define UAC3_BADD_FU_ID7	7  /* Feature Unit ID7: bUnitID = 7 */
#define UAC3_BADD_MU_ID8	8  /* Mixer Unit ID8: bUnitID = 8 */
#define UAC3_BADD_CS_ID9	9  /* Clock Source Entity ID9: bClockID = 9 */
#define UAC3_BADD_PD_ID10	10 /* Power Domain ID10: bPowerDomainID = 10 */
#define UAC3_BADD_PD_ID11	11 /* Power Domain ID11: bPowerDomainID = 11 */

/* BADD wMaxPacketSize of AS endpoints */
#define UAC3_BADD_EP_MAXPSIZE_SYNC_MONO_16		0x0060
#define UAC3_BADD_EP_MAXPSIZE_ASYNC_MONO_16		0x0062
#define UAC3_BADD_EP_MAXPSIZE_SYNC_MONO_24		0x0090
#define UAC3_BADD_EP_MAXPSIZE_ASYNC_MONO_24		0x0093
#define UAC3_BADD_EP_MAXPSIZE_SYNC_STEREO_16		0x00C0
#define UAC3_BADD_EP_MAXPSIZE_ASYNC_STEREO_16		0x00C4
#define UAC3_BADD_EP_MAXPSIZE_SYNC_STEREO_24		0x0120
#define UAC3_BADD_EP_MAXPSIZE_ASYNC_STEREO_24		0x0126

/* BADD sample rate is always fixed to 48kHz */
#define UAC3_BADD_SAMPLING_RATE				48000

/* BADD power domains recovery times in 50us increments */
#define UAC3_BADD_PD_RECOVER_D1D0			0x0258	/* 30ms */
#define UAC3_BADD_PD_RECOVER_D2D0			0x1770	/* 300ms */

#endif /* __LINUX_USB_AUDIO_V3_H */
