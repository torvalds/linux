/*
 * <linux/usb/audio.h> -- USB Audio definitions.
 *
 * Copyright (C) 2006 Thumtronics Pty Ltd.
 * Developed for Thumtronics by Grey Innovation
 * Ben Williamson <ben.williamson@greyinnovation.com>
 *
 * This software is distributed under the terms of the GNU General Public
 * License ("GPL") version 2, as published by the Free Software Foundation.
 *
 * This file holds USB constants and structures defined
 * by the USB Device Class Definition for Audio Devices.
 * Comments below reference relevant sections of that document:
 *
 * http://www.usb.org/developers/devclass_docs/audio10.pdf
 */

#ifndef __LINUX_USB_AUDIO_H
#define __LINUX_USB_AUDIO_H

#include <linux/types.h>

/* A.2 Audio Interface Subclass Codes */
#define USB_SUBCLASS_AUDIOCONTROL	0x01
#define USB_SUBCLASS_AUDIOSTREAMING	0x02
#define USB_SUBCLASS_MIDISTREAMING	0x03

#define UAC_VERSION_1			0x00
#define UAC_VERSION_2			0x20

/* A.5 Audio Class-Specific AC Interface Descriptor Subtypes */
#define UAC_HEADER			0x01
#define UAC_INPUT_TERMINAL		0x02
#define UAC_OUTPUT_TERMINAL		0x03
#define UAC_MIXER_UNIT			0x04
#define UAC_SELECTOR_UNIT		0x05
#define UAC_FEATURE_UNIT		0x06
#define UAC_PROCESSING_UNIT_V1		0x07
#define UAC_EXTENSION_UNIT_V1		0x08

/* UAC v2.0 types */
#define UAC_EFFECT_UNIT			0x07
#define UAC_PROCESSING_UNIT_V2		0x08
#define UAC_EXTENSION_UNIT_V2		0x09
#define UAC_CLOCK_SOURCE		0x0a
#define UAC_CLOCK_SELECTOR		0x0b
#define UAC_CLOCK_MULTIPLIER		0x0c
#define UAC_SAMPLE_RATE_CONVERTER	0x0d

/* A.6 Audio Class-Specific AS Interface Descriptor Subtypes */
#define UAC_AS_GENERAL			0x01
#define UAC_FORMAT_TYPE			0x02
#define UAC_FORMAT_SPECIFIC		0x03

/* A.8 Audio Class-Specific Endpoint Descriptor Subtypes */
#define UAC_EP_GENERAL			0x01

/* A.9 Audio Class-Specific Request Codes */
#define UAC_SET_			0x00
#define UAC_GET_			0x80

#define UAC__CUR			0x1
#define UAC__MIN			0x2
#define UAC__MAX			0x3
#define UAC__RES			0x4
#define UAC__MEM			0x5

#define UAC_SET_CUR			(UAC_SET_ | UAC__CUR)
#define UAC_GET_CUR			(UAC_GET_ | UAC__CUR)
#define UAC_SET_MIN			(UAC_SET_ | UAC__MIN)
#define UAC_GET_MIN			(UAC_GET_ | UAC__MIN)
#define UAC_SET_MAX			(UAC_SET_ | UAC__MAX)
#define UAC_GET_MAX			(UAC_GET_ | UAC__MAX)
#define UAC_SET_RES			(UAC_SET_ | UAC__RES)
#define UAC_GET_RES			(UAC_GET_ | UAC__RES)
#define UAC_SET_MEM			(UAC_SET_ | UAC__MEM)
#define UAC_GET_MEM			(UAC_GET_ | UAC__MEM)

#define UAC_GET_STAT			0xff

/* Audio class v2.0 handles all the parameter calls differently */
#define UAC2_CS_CUR			0x01
#define UAC2_CS_RANGE			0x02

/* MIDI - A.1 MS Class-Specific Interface Descriptor Subtypes */
#define UAC_MS_HEADER			0x01
#define UAC_MIDI_IN_JACK		0x02
#define UAC_MIDI_OUT_JACK		0x03

/* MIDI - A.1 MS Class-Specific Endpoint Descriptor Subtypes */
#define UAC_MS_GENERAL			0x01

/* Terminals - 2.1 USB Terminal Types */
#define UAC_TERMINAL_UNDEFINED		0x100
#define UAC_TERMINAL_STREAMING		0x101
#define UAC_TERMINAL_VENDOR_SPEC	0x1FF

/* Terminal Control Selectors */
/* 4.3.2  Class-Specific AC Interface Descriptor */
struct uac_ac_header_descriptor_v1 {
	__u8  bLength;			/* 8 + n */
	__u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	__u8  bDescriptorSubtype;	/* UAC_MS_HEADER */
	__le16 bcdADC;			/* 0x0100 */
	__le16 wTotalLength;		/* includes Unit and Terminal desc. */
	__u8  bInCollection;		/* n */
	__u8  baInterfaceNr[];		/* [n] */
} __attribute__ ((packed));

#define UAC_DT_AC_HEADER_SIZE(n)	(8 + (n))

/* As above, but more useful for defining your own descriptors: */
#define DECLARE_UAC_AC_HEADER_DESCRIPTOR(n) 			\
struct uac_ac_header_descriptor_v1_##n {			\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubtype;				\
	__le16 bcdADC;						\
	__le16 wTotalLength;					\
	__u8  bInCollection;					\
	__u8  baInterfaceNr[n];					\
} __attribute__ ((packed))

/* 4.3.2.1 Input Terminal Descriptor */
struct uac_input_terminal_descriptor {
	__u8  bLength;			/* in bytes: 12 */
	__u8  bDescriptorType;		/* CS_INTERFACE descriptor type */
	__u8  bDescriptorSubtype;	/* INPUT_TERMINAL descriptor subtype */
	__u8  bTerminalID;		/* Constant uniquely terminal ID */
	__le16 wTerminalType;		/* USB Audio Terminal Types */
	__u8  bAssocTerminal;		/* ID of the Output Terminal associated */
	__u8  bNrChannels;		/* Number of logical output channels */
	__le16 wChannelConfig;
	__u8  iChannelNames;
	__u8  iTerminal;
} __attribute__ ((packed));

#define UAC_DT_INPUT_TERMINAL_SIZE			12

/* Terminals - 2.2 Input Terminal Types */
#define UAC_INPUT_TERMINAL_UNDEFINED			0x200
#define UAC_INPUT_TERMINAL_MICROPHONE			0x201
#define UAC_INPUT_TERMINAL_DESKTOP_MICROPHONE		0x202
#define UAC_INPUT_TERMINAL_PERSONAL_MICROPHONE		0x203
#define UAC_INPUT_TERMINAL_OMNI_DIR_MICROPHONE		0x204
#define UAC_INPUT_TERMINAL_MICROPHONE_ARRAY		0x205
#define UAC_INPUT_TERMINAL_PROC_MICROPHONE_ARRAY	0x206

/* Terminals - control selectors */

#define UAC_TERMINAL_CS_COPY_PROTECT_CONTROL		0x01

/* 4.3.2.2 Output Terminal Descriptor */
struct uac_output_terminal_descriptor_v1 {
	__u8  bLength;			/* in bytes: 9 */
	__u8  bDescriptorType;		/* CS_INTERFACE descriptor type */
	__u8  bDescriptorSubtype;	/* OUTPUT_TERMINAL descriptor subtype */
	__u8  bTerminalID;		/* Constant uniquely terminal ID */
	__le16 wTerminalType;		/* USB Audio Terminal Types */
	__u8  bAssocTerminal;		/* ID of the Input Terminal associated */
	__u8  bSourceID;		/* ID of the connected Unit or Terminal*/
	__u8  iTerminal;
} __attribute__ ((packed));

#define UAC_DT_OUTPUT_TERMINAL_SIZE			9

/* Terminals - 2.3 Output Terminal Types */
#define UAC_OUTPUT_TERMINAL_UNDEFINED			0x300
#define UAC_OUTPUT_TERMINAL_SPEAKER			0x301
#define UAC_OUTPUT_TERMINAL_HEADPHONES			0x302
#define UAC_OUTPUT_TERMINAL_HEAD_MOUNTED_DISPLAY_AUDIO	0x303
#define UAC_OUTPUT_TERMINAL_DESKTOP_SPEAKER		0x304
#define UAC_OUTPUT_TERMINAL_ROOM_SPEAKER		0x305
#define UAC_OUTPUT_TERMINAL_COMMUNICATION_SPEAKER	0x306
#define UAC_OUTPUT_TERMINAL_LOW_FREQ_EFFECTS_SPEAKER	0x307

/* Set bControlSize = 2 as default setting */
#define UAC_DT_FEATURE_UNIT_SIZE(ch)		(7 + ((ch) + 1) * 2)

/* As above, but more useful for defining your own descriptors: */
#define DECLARE_UAC_FEATURE_UNIT_DESCRIPTOR(ch) 		\
struct uac_feature_unit_descriptor_##ch {			\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubtype;				\
	__u8  bUnitID;						\
	__u8  bSourceID;					\
	__u8  bControlSize;					\
	__le16 bmaControls[ch + 1];				\
	__u8  iFeature;						\
} __attribute__ ((packed))

/* 4.5.2 Class-Specific AS Interface Descriptor */
struct uac_as_header_descriptor_v1 {
	__u8  bLength;			/* in bytes: 7 */
	__u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	__u8  bDescriptorSubtype;	/* AS_GENERAL */
	__u8  bTerminalLink;		/* Terminal ID of connected Terminal */
	__u8  bDelay;			/* Delay introduced by the data path */
	__le16 wFormatTag;		/* The Audio Data Format */
} __attribute__ ((packed));

struct uac_as_header_descriptor_v2 {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bTerminalLink;
	__u8 bmControls;
	__u8 bFormatType;
	__u32 bmFormats;
	__u8 bNrChannels;
	__u32 bmChannelConfig;
	__u8 iChannelNames;
} __attribute__((packed));

#define UAC_DT_AS_HEADER_SIZE		7

/* Formats - A.1.1 Audio Data Format Type I Codes */
#define UAC_FORMAT_TYPE_I_UNDEFINED	0x0
#define UAC_FORMAT_TYPE_I_PCM		0x1
#define UAC_FORMAT_TYPE_I_PCM8		0x2
#define UAC_FORMAT_TYPE_I_IEEE_FLOAT	0x3
#define UAC_FORMAT_TYPE_I_ALAW		0x4
#define UAC_FORMAT_TYPE_I_MULAW		0x5

struct uac_format_type_i_continuous_descriptor {
	__u8  bLength;			/* in bytes: 8 + (ns * 3) */
	__u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	__u8  bDescriptorSubtype;	/* FORMAT_TYPE */
	__u8  bFormatType;		/* FORMAT_TYPE_1 */
	__u8  bNrChannels;		/* physical channels in the stream */
	__u8  bSubframeSize;		/* */
	__u8  bBitResolution;
	__u8  bSamFreqType;
	__u8  tLowerSamFreq[3];
	__u8  tUpperSamFreq[3];
} __attribute__ ((packed));

#define UAC_FORMAT_TYPE_I_CONTINUOUS_DESC_SIZE	14

struct uac_format_type_i_discrete_descriptor {
	__u8  bLength;			/* in bytes: 8 + (ns * 3) */
	__u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	__u8  bDescriptorSubtype;	/* FORMAT_TYPE */
	__u8  bFormatType;		/* FORMAT_TYPE_1 */
	__u8  bNrChannels;		/* physical channels in the stream */
	__u8  bSubframeSize;		/* */
	__u8  bBitResolution;
	__u8  bSamFreqType;
	__u8  tSamFreq[][3];
} __attribute__ ((packed));

#define DECLARE_UAC_FORMAT_TYPE_I_DISCRETE_DESC(n) 		\
struct uac_format_type_i_discrete_descriptor_##n {		\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubtype;				\
	__u8  bFormatType;					\
	__u8  bNrChannels;					\
	__u8  bSubframeSize;					\
	__u8  bBitResolution;					\
	__u8  bSamFreqType;					\
	__u8  tSamFreq[n][3];					\
} __attribute__ ((packed))

#define UAC_FORMAT_TYPE_I_DISCRETE_DESC_SIZE(n)	(8 + (n * 3))

struct uac_format_type_i_ext_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bSubslotSize;
	__u8 bFormatType;
	__u8 bBitResolution;
	__u8 bHeaderLength;
	__u8 bControlSize;
	__u8 bSideBandProtocol;
} __attribute__((packed));


/* Formats - Audio Data Format Type I Codes */

#define UAC_FORMAT_TYPE_II_MPEG	0x1001
#define UAC_FORMAT_TYPE_II_AC3	0x1002

struct uac_format_type_ii_discrete_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bFormatType;
	__le16 wMaxBitRate;
	__le16 wSamplesPerFrame;
	__u8 bSamFreqType;
	__u8 tSamFreq[][3];
} __attribute__((packed));

struct uac_format_type_ii_ext_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bFormatType;
	__u16 wMaxBitRate;
	__u16 wSamplesPerFrame;
	__u8 bHeaderLength;
	__u8 bSideBandProtocol;
} __attribute__((packed));

/* type III */
#define UAC_FORMAT_TYPE_III_IEC1937_AC3	0x2001
#define UAC_FORMAT_TYPE_III_IEC1937_MPEG1_LAYER1	0x2002
#define UAC_FORMAT_TYPE_III_IEC1937_MPEG2_NOEXT	0x2003
#define UAC_FORMAT_TYPE_III_IEC1937_MPEG2_EXT	0x2004
#define UAC_FORMAT_TYPE_III_IEC1937_MPEG2_LAYER1_LS	0x2005
#define UAC_FORMAT_TYPE_III_IEC1937_MPEG2_LAYER23_LS	0x2006

/* Formats - A.2 Format Type Codes */
#define UAC_FORMAT_TYPE_UNDEFINED	0x0
#define UAC_FORMAT_TYPE_I		0x1
#define UAC_FORMAT_TYPE_II		0x2
#define UAC_FORMAT_TYPE_III		0x3
#define UAC_EXT_FORMAT_TYPE_I		0x81
#define UAC_EXT_FORMAT_TYPE_II		0x82
#define UAC_EXT_FORMAT_TYPE_III		0x83

struct uac_iso_endpoint_descriptor {
	__u8  bLength;			/* in bytes: 7 */
	__u8  bDescriptorType;		/* USB_DT_CS_ENDPOINT */
	__u8  bDescriptorSubtype;	/* EP_GENERAL */
	__u8  bmAttributes;
	__u8  bLockDelayUnits;
	__le16 wLockDelay;
};
#define UAC_ISO_ENDPOINT_DESC_SIZE	7

#define UAC_EP_CS_ATTR_SAMPLE_RATE	0x01
#define UAC_EP_CS_ATTR_PITCH_CONTROL	0x02
#define UAC_EP_CS_ATTR_FILL_MAX		0x80

/* Audio class v2.0: CLOCK_SOURCE descriptor */

struct uac_clock_source_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bClockID;
	__u8 bmAttributes;
	__u8 bmControls;
	__u8 bAssocTerminal;
	__u8 iClockSource;
} __attribute__((packed));

/* A.10.2 Feature Unit Control Selectors */

struct uac_feature_unit_descriptor {
	__u8 bLength;
	__u8 bDescriptorType;
	__u8 bDescriptorSubtype;
	__u8 bUnitID;
	__u8 bSourceID;
	__u8 bControlSize;
	__u8 controls[0]; /* variable length */
} __attribute__((packed));

#define UAC_FU_CONTROL_UNDEFINED	0x00
#define UAC_MUTE_CONTROL		0x01
#define UAC_VOLUME_CONTROL		0x02
#define UAC_BASS_CONTROL		0x03
#define UAC_MID_CONTROL			0x04
#define UAC_TREBLE_CONTROL		0x05
#define UAC_GRAPHIC_EQUALIZER_CONTROL	0x06
#define UAC_AUTOMATIC_GAIN_CONTROL	0x07
#define UAC_DELAY_CONTROL		0x08
#define UAC_BASS_BOOST_CONTROL		0x09
#define UAC_LOUDNESS_CONTROL		0x0a

#define UAC_FU_MUTE		(1 << (UAC_MUTE_CONTROL - 1))
#define UAC_FU_VOLUME		(1 << (UAC_VOLUME_CONTROL - 1))
#define UAC_FU_BASS		(1 << (UAC_BASS_CONTROL - 1))
#define UAC_FU_MID		(1 << (UAC_MID_CONTROL - 1))
#define UAC_FU_TREBLE		(1 << (UAC_TREBLE_CONTROL - 1))
#define UAC_FU_GRAPHIC_EQ	(1 << (UAC_GRAPHIC_EQUALIZER_CONTROL - 1))
#define UAC_FU_AUTO_GAIN	(1 << (UAC_AUTOMATIC_GAIN_CONTROL - 1))
#define UAC_FU_DELAY		(1 << (UAC_DELAY_CONTROL - 1))
#define UAC_FU_BASS_BOOST	(1 << (UAC_BASS_BOOST_CONTROL - 1))
#define UAC_FU_LOUDNESS		(1 << (UAC_LOUDNESS_CONTROL - 1))

#ifdef __KERNEL__

struct usb_audio_control {
	struct list_head list;
	const char *name;
	u8 type;
	int data[5];
	int (*set)(struct usb_audio_control *con, u8 cmd, int value);
	int (*get)(struct usb_audio_control *con, u8 cmd);
};

struct usb_audio_control_selector {
	struct list_head list;
	struct list_head control;
	u8 id;
	const char *name;
	u8 type;
	struct usb_descriptor_header *desc;
};

#endif /* __KERNEL__ */

#endif /* __LINUX_USB_AUDIO_H */
