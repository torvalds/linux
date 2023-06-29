/* SPDX-License-Identifier: GPL-2.0 */
/*
 * <linux/usb/midi-v2.h> -- USB MIDI 2.0 definitions.
 */

#ifndef __LINUX_USB_MIDI_V2_H
#define __LINUX_USB_MIDI_V2_H

#include <linux/types.h>
#include <linux/usb/midi.h>

/* A.1 MS Class-Specific Interface Descriptor Types */
#define USB_DT_CS_GR_TRM_BLOCK	0x26

/* A.1 MS Class-Specific Interface Descriptor Subtypes */
/* same as MIDI 1.0 */

/* A.2 MS Class-Specific Endpoint Descriptor Subtypes */
#define USB_MS_GENERAL_2_0	0x02

/* A.3 MS Class-Specific Group Terminal Block Descriptor Subtypes */
#define USB_MS_GR_TRM_BLOCK_UNDEFINED	0x00
#define USB_MS_GR_TRM_BLOCK_HEADER	0x01
#define USB_MS_GR_TRM_BLOCK		0x02

/* A.4 MS Interface Header MIDIStreaming Class Revision */
#define USB_MS_REV_MIDI_1_0		0x0100
#define USB_MS_REV_MIDI_2_0		0x0200

/* A.5 MS MIDI IN and OUT Jack Types */
/* same as MIDI 1.0 */

/* A.6 Group Terminal Block Types */
#define USB_MS_GR_TRM_BLOCK_TYPE_BIDIRECTIONAL	0x00
#define USB_MS_GR_TRM_BLOCK_TYPE_INPUT_ONLY	0x01
#define USB_MS_GR_TRM_BLOCK_TYPE_OUTPUT_ONLY	0x02

/* A.7 Group Terminal Default MIDI Protocol */
#define USB_MS_MIDI_PROTO_UNKNOWN	0x00 /* Unknown (Use MIDI-CI) */
#define USB_MS_MIDI_PROTO_1_0_64	0x01 /* MIDI 1.0, UMP up to 64bits */
#define USB_MS_MIDI_PROTO_1_0_64_JRTS	0x02 /* MIDI 1.0, UMP up to 64bits, Jitter Reduction Timestamps */
#define USB_MS_MIDI_PROTO_1_0_128	0x03 /* MIDI 1.0, UMP up to 128bits */
#define USB_MS_MIDI_PROTO_1_0_128_JRTS	0x04 /* MIDI 1.0, UMP up to 128bits, Jitter Reduction Timestamps */
#define USB_MS_MIDI_PROTO_2_0		0x11 /* MIDI 2.0 */
#define USB_MS_MIDI_PROTO_2_0_JRTS	0x12 /* MIDI 2.0, Jitter Reduction Timestamps */

/* 5.2.2.1 Class-Specific MS Interface Header Descriptor */
/* Same as MIDI 1.0, use struct usb_ms_header_descriptor */

/* 5.3.2 Class-Specific MIDI Streaming Data Endpoint Descriptor */
struct usb_ms20_endpoint_descriptor {
	__u8  bLength;			/* 4+n */
	__u8  bDescriptorType;		/* USB_DT_CS_ENDPOINT */
	__u8  bDescriptorSubtype;	/* USB_MS_GENERAL_2_0 */
	__u8  bNumGrpTrmBlock;		/* Number of Group Terminal Blocks: n */
	__u8  baAssoGrpTrmBlkID[];	/* ID of the Group Terminal Blocks [n] */
} __packed;

#define USB_DT_MS20_ENDPOINT_SIZE(n)	(4 + (n))

/* As above, but more useful for defining your own descriptors: */
#define DECLARE_USB_MS20_ENDPOINT_DESCRIPTOR(n)			\
struct usb_ms20_endpoint_descriptor_##n {			\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubtype;				\
	__u8  bNumGrpTrmBlock;					\
	__u8  baAssoGrpTrmBlkID[n];				\
} __packed

/* 5.4.1 Class-Specific Group Terminal Block Header Descriptor */
struct usb_ms20_gr_trm_block_header_descriptor {
	__u8  bLength;			/* 5 */
	__u8  bDescriptorType;		/* USB_DT_CS_GR_TRM_BLOCK */
	__u8  bDescriptorSubtype;	/* USB_MS_GR_TRM_BLOCK_HEADER */
	__le16 wTotalLength;		/* Total number of bytes */
} __packed;

/* 5.4.2.1 Group Terminal Block Descriptor */
struct usb_ms20_gr_trm_block_descriptor {
	__u8  bLength;			/* 13 */
	__u8  bDescriptorType;		/* USB_DT_CS_GR_TRM_BLOCK */
	__u8  bDescriptorSubtype;	/* USB_MS_GR_TRM_BLOCK */
	__u8  bGrpTrmBlkID;		/* ID of this Group Terminal Block */
	__u8  bGrpTrmBlkType;		/* Group Terminal Block Type */
	__u8  nGroupTrm;		/* The first member Group Terminal in this block */
	__u8  nNumGroupTrm;		/* Number of member Group Terminals spanned */
	__u8  iBlockItem;		/* String ID of Block item */
	__u8  bMIDIProtocol;		/* Default MIDI protocol */
	__le16 wMaxInputBandwidth;	/* Max input bandwidth capability in 4kB/s */
	__le16 wMaxOutputBandwidth;	/* Max output bandwidth capability in 4kB/s */
} __packed;

#endif /* __LINUX_USB_MIDI_V2_H */
