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
 * The following NetLabel payloads are supported by the management interface,
 * all of which are preceeded by the nlmsghdr struct.
 *
 * o ACK:
 *   Sent by the kernel in response to an applications message, applications
 *   should never send this message.
 *
 *   +----------------------+-----------------------+
 *   | seq number (32 bits) | return code (32 bits) |
 *   +----------------------+-----------------------+
 *
 *     seq number:  the sequence number of the original message, taken from the
 *                  nlmsghdr structure
 *     return code: return value, based on errno values
 *
 * o ADD:
 *   Sent by an application to add a domain mapping to the NetLabel system.
 *   The kernel should respond with an ACK.
 *
 *   +-------------------+
 *   | domains (32 bits) | ...
 *   +-------------------+
 *
 *     domains: the number of domains in the message
 *
 *   +--------------------------+-------------------------+
 *   | domain string (variable) | protocol type (32 bits) | ...
 *   +--------------------------+-------------------------+
 *
 *   +-------------- ---- --- -- -
 *   | mapping data                ... repeated
 *   +-------------- ---- --- -- -
 *
 *     domain string: the domain string, NULL terminated
 *     protocol type: the protocol type (defined by NETLBL_NLTYPE_*)
 *     mapping data:  specific to the map type (see below)
 *
 *   NETLBL_NLTYPE_UNLABELED
 *
 *     No mapping data for this protocol type.
 *
 *   NETLBL_NLTYPE_CIPSOV4
 *
 *   +---------------+
 *   | doi (32 bits) |
 *   +---------------+
 *
 *     doi:  the CIPSO DOI value
 *
 * o REMOVE:
 *   Sent by an application to remove a domain mapping from the NetLabel
 *   system.  The kernel should ACK this message.
 *
 *   +-------------------+
 *   | domains (32 bits) | ...
 *   +-------------------+
 *
 *     domains: the number of domains in the message
 *
 *   +--------------------------+
 *   | domain string (variable) | ...
 *   +--------------------------+
 *
 *     domain string: the domain string, NULL terminated
 *
 * o LIST:
 *   This message can be sent either from an application or by the kernel in
 *   response to an application generated LIST message.  When sent by an
 *   application there is no payload.  The kernel should respond to a LIST
 *   message either with a LIST message on success or an ACK message on
 *   failure.
 *
 *   +-------------------+
 *   | domains (32 bits) | ...
 *   +-------------------+
 *
 *     domains: the number of domains in the message
 *
 *   +--------------------------+
 *   | domain string (variable) | ...
 *   +--------------------------+
 *
 *   +-------------------------+-------------- ---- --- -- -
 *   | protocol type (32 bits) | mapping data                ... repeated
 *   +-------------------------+-------------- ---- --- -- -
 *
 *     domain string: the domain string, NULL terminated
 *     protocol type: the protocol type (defined by NETLBL_NLTYPE_*)
 *     mapping data:  specific to the map type (see below)
 *
 *   NETLBL_NLTYPE_UNLABELED
 *
 *     No mapping data for this protocol type.
 *
 *   NETLBL_NLTYPE_CIPSOV4
 *
 *   +----------------+---------------+
 *   | type (32 bits) | doi (32 bits) |
 *   +----------------+---------------+
 *
 *     type: the CIPSO mapping table type (defined in the cipso_ipv4.h header
 *           as CIPSO_V4_MAP_*)
 *     doi:  the CIPSO DOI value
 *
 * o ADDDEF:
 *   Sent by an application to set the default domain mapping for the NetLabel
 *   system.  The kernel should respond with an ACK.
 *
 *   +-------------------------+-------------- ---- --- -- -
 *   | protocol type (32 bits) | mapping data                ... repeated
 *   +-------------------------+-------------- ---- --- -- -
 *
 *     protocol type: the protocol type (defined by NETLBL_NLTYPE_*)
 *     mapping data:  specific to the map type (see below)
 *
 *   NETLBL_NLTYPE_UNLABELED
 *
 *     No mapping data for this protocol type.
 *
 *   NETLBL_NLTYPE_CIPSOV4
 *
 *   +---------------+
 *   | doi (32 bits) |
 *   +---------------+
 *
 *     doi:  the CIPSO DOI value
 *
 * o REMOVEDEF:
 *   Sent by an application to remove the default domain mapping from the
 *   NetLabel system, there is no payload.  The kernel should ACK this message.
 *
 * o LISTDEF:
 *   This message can be sent either from an application or by the kernel in
 *   response to an application generated LISTDEF message.  When sent by an
 *   application there is no payload.  The kernel should respond to a
 *   LISTDEF message either with a LISTDEF message on success or an ACK message
 *   on failure.
 *
 *   +-------------------------+-------------- ---- --- -- -
 *   | protocol type (32 bits) | mapping data                ... repeated
 *   +-------------------------+-------------- ---- --- -- -
 *
 *     protocol type: the protocol type (defined by NETLBL_NLTYPE_*)
 *     mapping data:  specific to the map type (see below)
 *
 *   NETLBL_NLTYPE_UNLABELED
 *
 *     No mapping data for this protocol type.
 *
 *   NETLBL_NLTYPE_CIPSOV4
 *
 *   +----------------+---------------+
 *   | type (32 bits) | doi (32 bits) |
 *   +----------------+---------------+
 *
 *     type: the CIPSO mapping table type (defined in the cipso_ipv4.h header
 *           as CIPSO_V4_MAP_*)
 *     doi:  the CIPSO DOI value
 *
 * o MODULES:
 *   Sent by an application to request a list of configured NetLabel modules
 *   in the kernel.  When sent by an application there is no payload.
 *
 *   +-------------------+
 *   | modules (32 bits) | ...
 *   +-------------------+
 *
 *     modules: the number of modules in the message, if this is an application
 *              generated message and the value is zero then return a list of
 *              the configured modules
 *
 *   +------------------+
 *   | module (32 bits) | ... repeated
 *   +------------------+
 *
 *     module: the module number as defined by NETLBL_NLTYPE_*
 *
 * o VERSION:
 *   Sent by an application to request the NetLabel version string.  When sent
 *   by an application there is no payload.  This message type is also used by
 *   the kernel to respond to an VERSION request.
 *
 *   +-------------------+
 *   | version (32 bits) |
 *   +-------------------+
 *
 *     version: the protocol version number
 *
 */

/* NetLabel Management commands */
enum {
	NLBL_MGMT_C_UNSPEC,
	NLBL_MGMT_C_ACK,
	NLBL_MGMT_C_ADD,
	NLBL_MGMT_C_REMOVE,
	NLBL_MGMT_C_LIST,
	NLBL_MGMT_C_ADDDEF,
	NLBL_MGMT_C_REMOVEDEF,
	NLBL_MGMT_C_LISTDEF,
	NLBL_MGMT_C_MODULES,
	NLBL_MGMT_C_VERSION,
	__NLBL_MGMT_C_MAX,
};
#define NLBL_MGMT_C_MAX (__NLBL_MGMT_C_MAX - 1)

/* NetLabel protocol functions */
int netlbl_mgmt_genl_init(void);

#endif
