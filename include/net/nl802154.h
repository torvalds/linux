/*
 * nl802154.h
 *
 * Copyright (C) 2007, 2008, 2009 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef IEEE802154_NL_H
#define IEEE802154_NL_H

struct net_device;
struct ieee802154_addr;

/**
 * ieee802154_nl_assoc_indic - Notify userland of an association request.
 * @dev: The network device on which this association request was
 *       received.
 * @addr: The address of the device requesting association.
 * @cap: The capability information field from the device.
 *
 * This informs a userland coordinator of a device requesting to
 * associate with the PAN controlled by the coordinator.
 *
 * Note: This is in section 7.3.1 of the IEEE 802.15.4-2006 document.
 */
int ieee802154_nl_assoc_indic(struct net_device *dev,
		struct ieee802154_addr *addr, u8 cap);

/**
 * ieee802154_nl_assoc_confirm - Notify userland of association.
 * @dev: The device which has completed association.
 * @short_addr: The short address assigned to the device.
 * @status: The status of the association.
 *
 * Inform userland of the result of an association request. If the
 * association request included asking the coordinator to allocate
 * a short address then it is returned in @short_addr.
 *
 * Note: This is in section 7.3.2 of the IEEE 802.15.4 document.
 */
int ieee802154_nl_assoc_confirm(struct net_device *dev,
		u16 short_addr, u8 status);

/**
 * ieee802154_nl_disassoc_indic - Notify userland of disassociation.
 * @dev: The device on which disassociation was indicated.
 * @addr: The device which is disassociating.
 * @reason: The reason for the disassociation.
 *
 * Inform userland that a device has disassociated from the network.
 *
 * Note: This is in section 7.3.3 of the IEEE 802.15.4 document.
 */
int ieee802154_nl_disassoc_indic(struct net_device *dev,
		struct ieee802154_addr *addr, u8 reason);

/**
 * ieee802154_nl_disassoc_confirm - Notify userland of disassociation
 * completion.
 * @dev: The device on which disassociation was ordered.
 * @status: The result of the disassociation.
 *
 * Inform userland of the result of requesting that a device
 * disassociate, or the result of requesting that we disassociate from
 * a PAN managed by another coordinator.
 *
 * Note: This is in section 7.1.4.3 of the IEEE 802.15.4 document.
 */
int ieee802154_nl_disassoc_confirm(struct net_device *dev,
		u8 status);

/**
 * ieee802154_nl_scan_confirm - Notify userland of completion of scan.
 * @dev: The device which was instructed to scan.
 * @status: The status of the scan operation.
 * @scan_type: What type of scan was performed.
 * @unscanned: Any channels that the device was unable to scan.
 * @edl: The energy levels (if a passive scan).
 *
 *
 * Note: This is in section 7.1.11 of the IEEE 802.15.4 document.
 * Note: This API does not permit the return of an active scan result.
 */
int ieee802154_nl_scan_confirm(struct net_device *dev,
		u8 status, u8 scan_type, u32 unscanned, u8 page,
		u8 *edl/*, struct list_head *pan_desc_list */);

/**
 * ieee802154_nl_beacon_indic - Notify userland of a received beacon.
 * @dev: The device on which a beacon was received.
 * @panid: The PAN of the coordinator.
 * @coord_addr: The short address of the coordinator on that PAN.
 *
 * Note: This is in section 7.1.5 of the IEEE 802.15.4 document.
 * Note: This API does not provide extended information such as what
 * channel the PAN is on or what the LQI of the beacon frame was on
 * receipt.
 * Note: This API cannot indicate a beacon frame for a coordinator
 *       operating in long addressing mode.
 */
int ieee802154_nl_beacon_indic(struct net_device *dev, u16 panid,
		u16 coord_addr);

/**
 * ieee802154_nl_start_confirm - Notify userland of completion of start.
 * @dev: The device which was instructed to scan.
 * @status: The status of the scan operation.
 *
 * Note: This is in section 7.1.14 of the IEEE 802.15.4 document.
 */
int ieee802154_nl_start_confirm(struct net_device *dev, u8 status);

#endif
