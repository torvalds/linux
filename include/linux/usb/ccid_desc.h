/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011, 2017 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __LINUX_USB_CCID_DESC_H
#define __LINUX_USB_CCID_DESC_H

#include <linux/types.h>

/*CCID specification version 1.10*/
#define CCID1_10                               0x0110

#define SMART_CARD_DEVICE_CLASS                0x0B
/* Smart Card Device Class Descriptor Type */
#define CCID_DECRIPTOR_TYPE                    0x21

/* Table 5.3-1 Summary of CCID Class Specific Request */
#define CCIDGENERICREQ_ABORT                    0x01
#define CCIDGENERICREQ_GET_CLOCK_FREQUENCIES    0x02
#define CCIDGENERICREQ_GET_DATA_RATES           0x03

/* 6.1 Command Pipe, Bulk-OUT Messages */
#define PC_TO_RDR_ICCPOWERON                   0x62
#define PC_TO_RDR_ICCPOWEROFF                  0x63
#define PC_TO_RDR_GETSLOTSTATUS                0x65
#define PC_TO_RDR_XFRBLOCK                     0x6F
#define PC_TO_RDR_GETPARAMETERS                0x6C
#define PC_TO_RDR_RESETPARAMETERS              0x6D
#define PC_TO_RDR_SETPARAMETERS                0x61
#define PC_TO_RDR_ESCAPE                       0x6B
#define PC_TO_RDR_ICCCLOCK                     0x6E
#define PC_TO_RDR_T0APDU                       0x6A
#define PC_TO_RDR_SECURE                       0x69
#define PC_TO_RDR_MECHANICAL                   0x71
#define PC_TO_RDR_ABORT                        0x72
#define PC_TO_RDR_SETDATARATEANDCLOCKFREQUENCY 0x73

/* 6.2 Response Pipe, Bulk-IN Messages */
#define RDR_TO_PC_DATABLOCK                    0x80
#define RDR_TO_PC_SLOTSTATUS                   0x81
#define RDR_TO_PC_PARAMETERS                   0x82
#define RDR_TO_PC_ESCAPE                       0x83
#define RDR_TO_PC_DATARATEANDCLOCKFREQUENCY    0x84

/* 6.3 Interrupt-IN Messages */
#define RDR_TO_PC_NOTIFYSLOTCHANGE             0x50
#define RDR_TO_PC_HARDWAREERROR                0x51

/* Table 6.2-2 Slot error register when bmCommandStatus = 1 */
#define CMD_ABORTED                            0xFF
#define ICC_MUTE                               0xFE
#define XFR_PARITY_ERROR                       0xFD
#define XFR_OVERRUN                            0xFC
#define HW_ERROR                               0xFB
#define BAD_ATR_TS                             0xF8
#define BAD_ATR_TCK                            0xF7
#define ICC_PROTOCOL_NOT_SUPPORTED             0xF6
#define ICC_CLASS_NOT_SUPPORTED                0xF5
#define PROCEDURE_BYTE_CONFLICT                0xF4
#define DEACTIVATED_PROTOCOL                   0xF3
#define BUSY_WITH_AUTO_SEQUENCE                0xF2
#define PIN_TIMEOUT                            0xF0
#define PIN_CANCELLED                          0xEF
#define CMD_SLOT_BUSY                          0xE0

/* CCID rev 1.1, p.27 */
#define VOLTS_AUTO                             0x00
#define VOLTS_5_0                              0x01
#define VOLTS_3_0                              0x02
#define VOLTS_1_8                              0x03

/* 6.3.1 RDR_to_PC_NotifySlotChange */
#define ICC_NOT_PRESENT                        0x00
#define ICC_PRESENT                            0x01
#define ICC_CHANGE                             0x02
#define ICC_INSERTED_EVENT                     (ICC_PRESENT+ICC_CHANGE)

/* Identifies the length of type of subordinate descriptors of a CCID device
 * Table 5.1-1 Smart Card Device Class descriptors
 */
struct usb_ccid_class_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 bcdCCID;
	__u8  bMaxSlotIndex;
	__u8  bVoltageSupport;
	__u32  dwProtocols;
	__u32  dwDefaultClock;
	__u32  dwMaximumClock;
	__u8  bNumClockSupported;
	__u32  dwDataRate;
	__u32  dwMaxDataRate;
	__u8  bNumDataRatesSupported;
	__u32  dwMaxIFSD;
	__u32  dwSynchProtocols;
	__u32  dwMechanical;
	__u32  dwFeatures;
	__u32  dwMaxCCIDMessageLength;
	__u8  bClassGetResponse;
	__u8  bClassEnvelope;
	__u16 wLcdLayout;
	__u8  bPINSupport;
	__u8  bMaxCCIDBusySlots;
} __packed;
#endif
