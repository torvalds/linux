/*	$OpenBSD: lldp.h,v 1.1 2025/05/02 05:57:02 dlg Exp $ */

/*
 * Copyright (c) 2025 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _NET_LLDP_H_
#define _NET_LLDP_H_

#define LLDP_TLV_END			0
#define LLDP_TLV_CHASSIS_ID		1
#define LLDP_TLV_PORT_ID		2
#define LLDP_TLV_TTL			3
#define LLDP_TLV_PORT_DESCR		4
#define LLDP_TLV_SYSTEM_NAME		5
#define LLDP_TLV_SYSTEM_DESCR		6
#define LLDP_TLV_SYSTEM_CAP		7
#define LLDP_TLV_MANAGEMENT_ADDR	8
#define LLDP_TLV_ORG			127

/*
 * Chassis ID subtype enumeration
 */
#define LLDP_CHASSIS_ID_CHASSIS		1	/* Chassis component */
#define LLDP_CHASSIS_ID_IFALIAS		2	/* Interface alias */
#define LLDP_CHASSIS_ID_PORT		3	/* Port component */
#define LLDP_CHASSIS_ID_MACADDR		4	/* MAC address */
#define LLDP_CHASSIS_ID_ADDR		5	/* Network address */
#define LLDP_CHASSIS_ID_IFNAME		6	/* Interface name */
#define LLDP_CHASSIS_ID_LOCAL		7	/* Locally assigned */

/*
 * Port ID subtype enumeration
 */
#define LLDP_PORT_ID_IFALIAS		1	/* Interface alias */
#define LLDP_PORT_ID_PORT		2	/* Port component */
#define LLDP_PORT_ID_MACADDR		3	/* MAC address */
#define LLDP_PORT_ID_ADDR		4	/* Network address */
#define LLDP_PORT_ID_IFNAME		5	/* Interface name */
#define LLDP_PORT_ID_AGENTCID		6	/* Agent circuit ID */
#define LLDP_PORT_ID_LOCAL		7	/* Locally assigned */

/*
 * System Capabilities bits
 */
#define LLDP_SYSTEM_CAP_OTHER		(1 << 0)
#define LLDP_SYSTEM_CAP_REPEATER	(1 << 1)
#define LLDP_SYSTEM_CAP_BRIDGE		(1 << 2)
#define LLDP_SYSTEM_CAP_WLAN		(1 << 3)
#define LLDP_SYSTEM_CAP_ROUTER		(1 << 4)
#define LLDP_SYSTEM_CAP_TELEPHONE	(1 << 5)
#define LLDP_SYSTEM_CAP_DOCSIS		(1 << 6)
#define LLDP_SYSTEM_CAP_STATION		(1 << 7)

#endif /* _NET_LLDP_H_ */
