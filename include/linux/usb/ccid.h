/*
 *  Copyright (c) 2018  Vincent Pelletier
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
