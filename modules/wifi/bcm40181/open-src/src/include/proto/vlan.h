/*
 * 802.1Q VLAN protocol definitions
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: vlan.h,v 9.7 2009/03/13 01:11:50 Exp $
 */

#ifndef _vlan_h_
#define _vlan_h_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

#define VLAN_VID_MASK		0xfff	/* low 12 bits are vlan id */
#define	VLAN_CFI_SHIFT		12	/* canonical format indicator bit */
#define VLAN_PRI_SHIFT		13	/* user priority */

#define VLAN_PRI_MASK		7	/* 3 bits of priority */

#define	VLAN_TAG_LEN		4
#define	VLAN_TAG_OFFSET		(2 * ETHER_ADDR_LEN)	/* offset in Ethernet II packet only */

#define VLAN_TPID		0x8100	/* VLAN ethertype/Tag Protocol ID */

struct ethervlan_header {
	uint8	ether_dhost[ETHER_ADDR_LEN];
	uint8	ether_shost[ETHER_ADDR_LEN];
	uint16	vlan_type;		/* 0x8100 */
	uint16	vlan_tag;		/* priority, cfi and vid */
	uint16	ether_type;
};

#define	ETHERVLAN_HDR_LEN	(ETHER_HDR_LEN + VLAN_TAG_LEN)


/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#define ETHERVLAN_MOVE_HDR(d, s) \
do { \
	struct ethervlan_header t; \
	t = *(struct ethervlan_header *)(s); \
	*(struct ethervlan_header *)(d) = t; \
} while (0)

#endif /* _vlan_h_ */
