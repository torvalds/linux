/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (c) 2018  Vincent Pelletier
 */
/*
 */
#ifndef __CCID_H
#define __CCID_H

#include <linux/types.h>

#define USB_INTERFACE_CLASS_CCID 0x0b

struct ccid_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__le16 bcdCCID;
	__u8  bMaxSlotIndex;
	__u8  bVoltageSupport;
	__le32 dwProtocols;
	__le32 dwDefaultClock;
	__le32 dwMaximumClock;
	__u8  bNumClockSupported;
	__le32 dwDataRate;
	__le32 dwMaxDataRate;
	__u8  bNumDataRatesSupported;
	__le32 dwMaxIFSD;
	__le32 dwSynchProtocols;
	__le32 dwMechanical;
	__le32 dwFeatures;
	__le32 dwMaxCCIDMessageLength;
	__u8  bClassGetResponse;
	__u8  bClassEnvelope;
	__le16 wLcdLayout;
	__u8  bPINSupport;
	__u8  bMaxCCIDBusySlots;
} __attribute__ ((packed));

#endif /* __CCID_H */
