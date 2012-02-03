/*
 * Broadcom Ethernettype  protocol definitions
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
 * $Id: bcmeth.h,v 9.12 2009/12/29 19:57:18 Exp $
 */

/*
 * Broadcom Ethernet protocol defines
 */

#ifndef _BCMETH_H_
#define _BCMETH_H_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

#ifndef LINUX_POSTMOGRIFY_REMOVAL
/* ETHER_TYPE_BRCM is defined in ethernet.h */

/*
 * Following the 2byte BRCM ether_type is a 16bit BRCM subtype field
 * in one of two formats: (only subtypes 32768-65535 are in use now)
 *
 * subtypes 0-32767:
 *     8 bit subtype (0-127)
 *     8 bit length in bytes (0-255)
 *
 * subtypes 32768-65535:
 *     16 bit big-endian subtype
 *     16 bit big-endian length in bytes (0-65535)
 *
 * length is the number of additional bytes beyond the 4 or 6 byte header
 *
 * Reserved values:
 * 0 reserved
 * 5-15 reserved for iLine protocol assignments
 * 17-126 reserved, assignable
 * 127 reserved
 * 32768 reserved
 * 32769-65534 reserved, assignable
 * 65535 reserved
 */

/* 
 * While adding the subtypes and their specific processing code make sure
 * bcmeth_bcm_hdr_t is the first data structure in the user specific data structure definition
 */

#define	BCMILCP_SUBTYPE_RATE		1
#define	BCMILCP_SUBTYPE_LINK		2
#define	BCMILCP_SUBTYPE_CSA		3
#define	BCMILCP_SUBTYPE_LARQ		4
#define BCMILCP_SUBTYPE_VENDOR		5
#define	BCMILCP_SUBTYPE_FLH		17

#define BCMILCP_SUBTYPE_VENDOR_LONG	32769
#define BCMILCP_SUBTYPE_CERT		32770
#define BCMILCP_SUBTYPE_SES		32771


#define BCMILCP_BCM_SUBTYPE_RESERVED		0
#define BCMILCP_BCM_SUBTYPE_EVENT		1
#define BCMILCP_BCM_SUBTYPE_SES			2
/*
 * The EAPOL type is not used anymore. Instead EAPOL messages are now embedded
 * within BCMILCP_BCM_SUBTYPE_EVENT type messages
 */
/* #define BCMILCP_BCM_SUBTYPE_EAPOL		3 */
#define BCMILCP_BCM_SUBTYPE_DPT			4

#define BCMILCP_BCM_SUBTYPEHDR_MINLENGTH	8
#define BCMILCP_BCM_SUBTYPEHDR_VERSION		0
#endif /* LINUX_POSTMOGRIFY_REMOVAL */

/* These fields are stored in network order */
typedef BWL_PRE_PACKED_STRUCT struct bcmeth_hdr
{
	uint16	subtype;	/* Vendor specific..32769 */
	uint16	length;
	uint8	version;	/* Version is 0 */
	uint8	oui[3];		/* Broadcom OUI */
	/* user specific Data */
	uint16	usr_subtype;
} BWL_POST_PACKED_STRUCT bcmeth_hdr_t;


/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif	/*  _BCMETH_H_ */
