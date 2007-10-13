/*
 * NetLabel Management Support
 *
 * This file defines the management functions for the NetLabel system.  The
 * NetLabel system manages static and dynamic label mappings for network
 * protocols such as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul.moore@hp.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;  if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef _NETLABEL_MGMT_H
#define _NETLABEL_MGMT_H

#include <net/netlabel.h>

/*
 * The following NetLabel payloads are supported by the management interface.
 *
 * o ADD:
 *   Sent by an application to add a domain mapping to the NetLabel system.
 *
 *   Required attributes:
 *
 *     NLBL_MGMT_A_DOMAIN
 *     NLBL_MGMT_A_PROTOCOL
 *
 *   If using NETLBL_NLTYPE_CIPSOV4 the following attributes are required:
 *
 *     NLBL_MGMT_A_CV4DOI
 *
 *   If using NETLBL_NLTYPE_UNLABELED no other attributes are required.
 *
 * o REMOVE:
 *   Sent by an application to remove a domain mapping from the NetLabel
 *   system.
 *
 *   Required attributes:
 *
 *     NLBL_MGMT_A_DOMAIN
 *
 * o LISTALL:
 *   This message can be sent either from an application or by the kernel in
 *   response to an application generated LISTALL message.  When sent by an
 *   application there is no payload and the NLM_F_DUMP flag should be set.
 *   The kernel should respond with a series of the following messages.
 *
 *   Required attributes:
 *
 *     NLBL_MGMT_A_DOMAIN
 *     NLBL_MGMT_A_PROTOCOL
 *
 *   If using NETLBL_NLTYPE_CIPSOV4 the following attributes are required:
 *
 *     NLBL_MGMT_A_CV4DOI
 *
 *   If using NETLBL_NLTYPE_UNLABELED no other attributes are required.
 *
 * o ADDDEF:
 *   Sent by an application to set the default domain mapping for the NetLabel
 *   system.
 *
 *   Required attributes:
 *
 *     NLBL_MGMT_A_PROTOCOL
 *
 *   If using NETLBL_NLTYPE_CIPSOV4 the following attributes are required:
 *
 *     NLBL_MGMT_A_CV4DOI
 *
 *   If using NETLBL_NLTYPE_UNLABELED no other attributes are required.
 *
 * o REMOVEDEF:
 *   Sent by an application to remove the default domain mapping from the
 *   NetLabel system, there is no payload.
 *
 * o LISTDEF:
 *   This message can be sent either from an application or by the kernel in
 *   response to an application generated LISTDEF message.  When sent by an
 *   application there is no payload.  On success the kernel should send a
 *   response using the following format.
 *
 *   Required attributes:
 *
 *     NLBL_MGMT_A_PROTOCOL
 *
 *   If using NETLBL_NLTYPE_CIPSOV4 the following attributes are required:
 *
 *     NLBL_MGMT_A_CV4DOI
 *
 *   If using NETLBL_NLTYPE_UNLABELED no other attributes are required.
 *
 * o PROTOCOLS:
 *   Sent by an application to request a list of configured NetLabel protocols
 *   in the kernel.  When sent by an application there is no payload and the
 *   NLM_F_DUMP flag should be set.  The kernel should respond with a series of
 *   the following messages.
 *
 *   Required attributes:
 *
 *     NLBL_MGMT_A_PROTOCOL
 *
 * o VERSION:
 *   Sent by an application to request the NetLabel version.  When sent by an
 *   application there is no payload.  This message type is also used by the
 *   kernel to respond to an VERSION request.
 *
 *   Required attributes:
 *
 *     NLBL_MGMT_A_VERSION
 *
 */

/* NetLabel Management commands */
enum {
	NLBL_MGMT_C_UNSPEC,
	NLBL_MGMT_C_ADD,
	NLBL_MGMT_C_REMOVE,
	NLBL_MGMT_C_LISTALL,
	NLBL_MGMT_C_ADDDEF,
	NLBL_MGMT_C_REMOVEDEF,
	NLBL_MGMT_C_LISTDEF,
	NLBL_MGMT_C_PROTOCOLS,
	NLBL_MGMT_C_VERSION,
	__NLBL_MGMT_C_MAX,
};
#define NLBL_MGMT_C_MAX (__NLBL_MGMT_C_MAX - 1)

/* NetLabel Management attributes */
enum {
	NLBL_MGMT_A_UNSPEC,
	NLBL_MGMT_A_DOMAIN,
	/* (NLA_NUL_STRING)
	 * the NULL terminated LSM domain string */
	NLBL_MGMT_A_PROTOCOL,
	/* (NLA_U32)
	 * the NetLabel protocol type (defined by NETLBL_NLTYPE_*) */
	NLBL_MGMT_A_VERSION,
	/* (NLA_U32)
	 * the NetLabel protocol version number (defined by
	 * NETLBL_PROTO_VERSION) */
	NLBL_MGMT_A_CV4DOI,
	/* (NLA_U32)
	 * the CIPSOv4 DOI value */
	__NLBL_MGMT_A_MAX,
};
#define NLBL_MGMT_A_MAX (__NLBL_MGMT_A_MAX - 1)

/* NetLabel protocol functions */
int netlbl_mgmt_genl_init(void);

/* NetLabel misc management functions */
void netlbl_mgmt_protocount_inc(void);
void netlbl_mgmt_protocount_dec(void);
u32 netlbl_mgmt_protocount_value(void);

#endif
