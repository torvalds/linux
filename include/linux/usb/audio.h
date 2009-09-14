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
#define USB_SUBCLASS_VENDOR_SPEC	0xff

/* A.5 Audio Class-Specific AC interface Descriptor Subtypes*/
#define HEADER				0x01
#define INPUT_TERMINAL			0x02
#define OUTPUT_TERMINAL			0x03
#define MIXER_UNIT			0x04
#define SELECTOR_UNIT			0x05
#define FEATURE_UNIT			0x06
#define PROCESSING_UNIT			0x07
#define EXTENSION_UNIT			0x08

#define AS_GENERAL			0x01
#define FORMAT_TYPE			0x02
#define FORMAT_SPECIFIC			0x03

#define EP_GENERAL			0x01

#define MS_GENERAL			0x01
#define MIDI_IN_JACK			0x02
#define MIDI_OUT_JACK			0x03

/* endpoint attributes */
#define EP_ATTR_MASK			0x0c
#define EP_ATTR_ASYNC			0x04
#define EP_ATTR_ADAPTIVE		0x08
#define EP_ATTR_SYNC			0x0c

/* cs endpoint attributes */
#define EP_CS_ATTR_SAMPLE_RATE		0x01
#define EP_CS_ATTR_PITCH_CONTROL	0x02
#define EP_CS_ATTR_FILL_MAX		0x80

/* Audio Class specific Request Codes */
#define USB_AUDIO_SET_INTF		0x21
#define USB_AUDIO_SET_ENDPOINT		0x22
#define USB_AUDIO_GET_INTF		0xa1
#define USB_AUDIO_GET_ENDPOINT		0xa2

#define SET_	0x00
#define GET_	0x80

#define _CUR	0x1
#define _MIN	0x2
#define _MAX	0x3
#define _RES	0x4
#define _MEM	0x5

#define SET_CUR		(SET_ | _CUR)
#define GET_CUR		(GET_ | _CUR)
#define SET_MIN		(SET_ | _MIN)
#define GET_MIN		(GET_ | _MIN)
#define SET_MAX		(SET_ | _MAX)
#define GET_MAX		(GET_ | _MAX)
#define SET_RES		(SET_ | _RES)
#define GET_RES		(GET_ | _RES)
#define SET_MEM		(SET_ | _MEM)
#define GET_MEM		(GET_ | _MEM)

#define GET_STAT	0xff

#define USB_AC_TERMINAL_UNDEFINED	0x100
#define USB_AC_TERMINAL_STREAMING	0x101
#define USB_AC_TERMINAL_VENDOR_SPEC	0x1FF

/* Terminal Control Selectors */
/* 4.3.2  Class-Specific AC Interface Descriptor */
struct usb_ac_header_descriptor {
	__u8  bLength;			/* 8 + n */
	__u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	__u8  bDescriptorSubtype;	/* USB_MS_HEADER */
	__le16 bcdADC;			/* 0x0100 */
	__le16 wTotalLength;		/* includes Unit and Terminal desc. */
	__u8  bInCollection;		/* n */
	__u8  baInterfaceNr[];		/* [n] */
} __attribute__ ((packed));

#define USB_DT_AC_HEADER_SIZE(n)	(8 + (n))

/* As above, but more useful for defining your own descriptors: */
#define DECLARE_USB_AC_HEADER_DESCRIPTOR(n) 			\
struct usb_ac_header_descriptor_##n {				\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubtype;				\
	__le16 bcdADC;						\
	__le16 wTotalLength;					\
	__u8  bInCollection;					\
	__u8  baInterfaceNr[n];					\
} __attribute__ ((packed))

/* 4.3.2.1 Input Terminal Descriptor */
struct usb_input_terminal_descriptor {
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

#define USB_DT_AC_INPUT_TERMINAL_SIZE			12

#define USB_AC_INPUT_TERMINAL_UNDEFINED			0x200
#define USB_AC_INPUT_TERMINAL_MICROPHONE		0x201
#define USB_AC_INPUT_TERMINAL_DESKTOP_MICROPHONE	0x202
#define USB_AC_INPUT_TERMINAL_PERSONAL_MICROPHONE	0x203
#define USB_AC_INPUT_TERMINAL_OMNI_DIR_MICROPHONE	0x204
#define USB_AC_INPUT_TERMINAL_MICROPHONE_ARRAY		0x205
#define USB_AC_INPUT_TERMINAL_PROC_MICROPHONE_ARRAY	0x206

/* 4.3.2.2 Output Terminal Descriptor */
struct usb_output_terminal_descriptor {
	__u8  bLength;			/* in bytes: 9 */
	__u8  bDescriptorType;		/* CS_INTERFACE descriptor type */
	__u8  bDescriptorSubtype;	/* OUTPUT_TERMINAL descriptor subtype */
	__u8  bTerminalID;		/* Constant uniquely terminal ID */
	__le16 wTerminalType;		/* USB Audio Terminal Types */
	__u8  bAssocTerminal;		/* ID of the Input Terminal associated */
	__u8  bSourceID;		/* ID of the connected Unit or Terminal*/
	__u8  iTerminal;
} __attribute__ ((packed));

#define USB_DT_AC_OUTPUT_TERMINAL_SIZE				9

#define USB_AC_OUTPUT_TERMINAL_UNDEFINED			0x300
#define USB_AC_OUTPUT_TERMINAL_SPEAKER				0x301
#define USB_AC_OUTPUT_TERMINAL_HEADPHONES			0x302
#define USB_AC_OUTPUT_TERMINAL_HEAD_MOUNTED_DISPLAY_AUDIO	0x303
#define USB_AC_OUTPUT_TERMINAL_DESKTOP_SPEAKER			0x304
#define USB_AC_OUTPUT_TERMINAL_ROOM_SPEAKER			0x305
#define USB_AC_OUTPUT_TERMINAL_COMMUNICATION_SPEAKER		0x306
#define USB_AC_OUTPUT_TERMINAL_LOW_FREQ_EFFECTS_SPEAKER		0x307

/* Set bControlSize = 2 as default setting */
#define USB_DT_AC_FEATURE_UNIT_SIZE(ch)		(7 + ((ch) + 1) * 2)

/* As above, but more useful for defining your own descriptors: */
#define DECLARE_USB_AC_FEATURE_UNIT_DESCRIPTOR(ch) 		\
struct usb_ac_feature_unit_descriptor_##ch {			\
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
struct usb_as_header_descriptor {
	__u8  bLength;			/* in bytes: 7 */
	__u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	__u8  bDescriptorSubtype;	/* AS_GENERAL */
	__u8  bTerminalLink;		/* Terminal ID of connected Terminal */
	__u8  bDelay;			/* Delay introduced by the data path */
	__le16 wFormatTag;		/* The Audio Data Format */
} __attribute__ ((packed));

#define USB_DT_AS_HEADER_SIZE		7

#define USB_AS_AUDIO_FORMAT_TYPE_I_UNDEFINED	0x0
#define USB_AS_AUDIO_FORMAT_TYPE_I_PCM		0x1
#define USB_AS_AUDIO_FORMAT_TYPE_I_PCM8		0x2
#define USB_AS_AUDIO_FORMAT_TYPE_I_IEEE_FLOAT	0x3
#define USB_AS_AUDIO_FORMAT_TYPE_I_ALAW		0x4
#define USB_AS_AUDIO_FORMAT_TYPE_I_MULAW	0x5

struct usb_as_format_type_i_continuous_descriptor {
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

#define USB_AS_FORMAT_TYPE_I_CONTINUOUS_DESC_SIZE	14

struct usb_as_formate_type_i_discrete_descriptor {
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

#define DECLARE_USB_AS_FORMAT_TYPE_I_DISCRETE_DESC(n) 		\
struct usb_as_formate_type_i_discrete_descriptor_##n {		\
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

#define USB_AS_FORMAT_TYPE_I_DISCRETE_DESC_SIZE(n)	(8 + (n * 3))

#define USB_AS_FORMAT_TYPE_UNDEFINED	0x0
#define USB_AS_FORMAT_TYPE_I		0x1
#define USB_AS_FORMAT_TYPE_II		0x2
#define USB_AS_FORMAT_TYPE_III		0x3

#define USB_AS_ENDPOINT_ASYNC		(1 << 2)
#define USB_AS_ENDPOINT_ADAPTIVE	(2 << 2)
#define USB_AS_ENDPOINT_SYNC		(3 << 2)

struct usb_as_iso_endpoint_descriptor {
	__u8  bLength;			/* in bytes: 7 */
	__u8  bDescriptorType;		/* USB_DT_CS_ENDPOINT */
	__u8  bDescriptorSubtype;	/* EP_GENERAL */
	__u8  bmAttributes;
	__u8  bLockDelayUnits;
	__le16 wLockDelay;
};
#define USB_AS_ISO_ENDPOINT_DESC_SIZE	7

#define FU_CONTROL_UNDEFINED		0x00
#define MUTE_CONTROL			0x01
#define VOLUME_CONTROL			0x02
#define BASS_CONTROL			0x03
#define MID_CONTROL			0x04
#define TREBLE_CONTROL			0x05
#define GRAPHIC_EQUALIZER_CONTROL	0x06
#define AUTOMATIC_GAIN_CONTROL		0x07
#define DELAY_CONTROL			0x08
#define BASS_BOOST_CONTROL		0x09
#define LOUDNESS_CONTROL		0x0a

#define FU_MUTE		(1 << (MUTE_CONTROL - 1))
#define FU_VOLUME	(1 << (VOLUME_CONTROL - 1))
#define FU_BASS		(1 << (BASS_CONTROL - 1))
#define FU_MID		(1 << (MID_CONTROL - 1))
#define FU_TREBLE	(1 << (TREBLE_CONTROL - 1))
#define FU_GRAPHIC_EQ	(1 << (GRAPHIC_EQUALIZER_CONTROL - 1))
#define FU_AUTO_GAIN	(1 << (AUTOMATIC_GAIN_CONTROL - 1))
#define FU_DELAY	(1 << (DELAY_CONTROL - 1))
#define FU_BASS_BOOST	(1 << (BASS_BOOST_CONTROL - 1))
#define FU_LOUDNESS	(1 << (LOUDNESS_CONTROL - 1))

struct usb_audio_control {
	struct list_head list;
	const char *name;
	u8 type;
	int data[5];
	int (*set)(struct usb_audio_control *con, u8 cmd, int value);
	int (*get)(struct usb_audio_control *con, u8 cmd);
};

static inline int generic_set_cmd(struct usb_audio_control *con, u8 cmd, int value)
{
	con->data[cmd] = value;

	return 0;
}

static inline int generic_get_cmd(struct usb_audio_control *con, u8 cmd)
{
	return con->data[cmd];
}

struct usb_audio_control_selector {
	struct list_head list;
	struct list_head control;
	u8 id;
	const char *name;
	u8 type;
	struct usb_descriptor_header *desc;
};

#endif /* __LINUX_USB_AUDIO_H */
