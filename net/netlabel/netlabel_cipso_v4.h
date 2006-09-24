/*
 * NetLabel CIPSO/IPv4 Support
 *
 * This file defines the CIPSO/IPv4 functions for the NetLabel system.  The
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

#ifndef _NETLABEL_CIPSO_V4
#define _NETLABEL_CIPSO_V4

#include <net/netlabel.h>

/*
 * The following NetLabel payloads are supported by the CIPSO subsystem, all
 * of which are preceeded by the nlmsghdr struct.
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
 *   Sent by an application to add a new DOI mapping table, after completion
 *   of the task the kernel should ACK this message.
 *
 *   +---------------+--------------------+---------------------+
 *   | DOI (32 bits) | map type (32 bits) | tag count (32 bits) | ...
 *   +---------------+--------------------+---------------------+
 *
 *   +-----------------+
 *   | tag #X (8 bits) | ... repeated
 *   +-----------------+
 *
 *   +-------------- ---- --- -- -
 *   | mapping data
 *   +-------------- ---- --- -- -
 *
 *     DOI:          the DOI value
 *     map type:     the mapping table type (defined in the cipso_ipv4.h header
 *                   as CIPSO_V4_MAP_*)
 *     tag count:    the number of tags, must be greater than zero
 *     tag:          the CIPSO tag for the DOI, tags listed first are given
 *                   higher priorirty when sending packets
 *     mapping data: specific to the map type (see below)
 *
 *   CIPSO_V4_MAP_STD
 *
 *   +------------------+-----------------------+----------------------+
 *   | levels (32 bits) | max l level (32 bits) | max r level (8 bits) | ...
 *   +------------------+-----------------------+----------------------+
 *
 *   +----------------------+---------------------+---------------------+
 *   | categories (32 bits) | max l cat (32 bits) | max r cat (16 bits) | ...
 *   +----------------------+---------------------+---------------------+
 *
 *   +--------------------------+-------------------------+
 *   | local level #X (32 bits) | CIPSO level #X (8 bits) | ... repeated
 *   +--------------------------+-------------------------+
 *
 *   +-----------------------------+-----------------------------+
 *   | local category #X (32 bits) | CIPSO category #X (16 bits) | ... repeated
 *   +-----------------------------+-----------------------------+
 *
 *     levels:         the number of level mappings
 *     max l level:    the highest local level
 *     max r level:    the highest remote/CIPSO level
 *     categories:     the number of category mappings
 *     max l cat:      the highest local category
 *     max r cat:      the highest remote/CIPSO category
 *     local level:    the local part of a level mapping
 *     CIPSO level:    the remote/CIPSO part of a level mapping
 *     local category: the local part of a category mapping
 *     CIPSO category: the remote/CIPSO part of a category mapping
 *
 *   CIPSO_V4_MAP_PASS
 *
 *   No mapping data is needed for this map type.
 *
 * o REMOVE:
 *   Sent by an application to remove a specific DOI mapping table from the
 *   CIPSO V4 system.  The kernel should ACK this message.
 *
 *   +---------------+
 *   | DOI (32 bits) |
 *   +---------------+
 *
 *     DOI:          the DOI value
 *
 * o LIST:
 *   Sent by an application to list the details of a DOI definition.  The
 *   kernel should send an ACK on error or a response as indicated below.  The
 *   application generated message format is shown below.
 *
 *   +---------------+
 *   | DOI (32 bits) |
 *   +---------------+
 *
 *     DOI:          the DOI value
 *
 *   The valid response message format depends on the type of the DOI mapping,
 *   the known formats are shown below.
 *
 *   +--------------------+
 *   | map type (32 bits) | ...
 *   +--------------------+
 *
 *     map type:       the DOI mapping table type (defined in the cipso_ipv4.h
 *                     header as CIPSO_V4_MAP_*)
 *
 *   (map type == CIPSO_V4_MAP_STD)
 *
 *   +----------------+------------------+----------------------+
 *   | tags (32 bits) | levels (32 bits) | categories (32 bits) | ...
 *   +----------------+------------------+----------------------+
 *
 *   +-----------------+
 *   | tag #X (8 bits) | ... repeated
 *   +-----------------+
 *
 *   +--------------------------+-------------------------+
 *   | local level #X (32 bits) | CIPSO level #X (8 bits) | ... repeated
 *   +--------------------------+-------------------------+
 *
 *   +-----------------------------+-----------------------------+
 *   | local category #X (32 bits) | CIPSO category #X (16 bits) | ... repeated
 *   +-----------------------------+-----------------------------+
 *
 *     tags:           the number of CIPSO tag types
 *     levels:         the number of level mappings
 *     categories:     the number of category mappings
 *     tag:            the tag number, tags listed first are given higher
 *                     priority when sending packets
 *     local level:    the local part of a level mapping
 *     CIPSO level:    the remote/CIPSO part of a level mapping
 *     local category: the local part of a category mapping
 *     CIPSO category: the remote/CIPSO part of a category mapping
 *
 *   (map type == CIPSO_V4_MAP_PASS)
 *
 *   +----------------+
 *   | tags (32 bits) | ...
 *   +----------------+
 *
 *   +-----------------+
 *   | tag #X (8 bits) | ... repeated
 *   +-----------------+
 *
 *     tags:           the number of CIPSO tag types
 *     tag:            the tag number, tags listed first are given higher
 *                     priority when sending packets
 *
 * o LISTALL:
 *   This message is sent by an application to list the valid DOIs on the
 *   system.  There is no payload and the kernel should respond with an ACK
 *   or the following message.
 *
 *   +---------------------+------------------+-----------------------+
 *   | DOI count (32 bits) | DOI #X (32 bits) | map type #X (32 bits) |
 *   +---------------------+------------------+-----------------------+
 *
 *   +-----------------------+
 *   | map type #X (32 bits) | ...
 *   +-----------------------+
 *
 *     DOI count:      the number of DOIs
 *     DOI:            the DOI value
 *     map type:       the DOI mapping table type (defined in the cipso_ipv4.h
 *                     header as CIPSO_V4_MAP_*)
 *
 */

/* NetLabel CIPSOv4 commands */
enum {
	NLBL_CIPSOV4_C_UNSPEC,
	NLBL_CIPSOV4_C_ACK,
	NLBL_CIPSOV4_C_ADD,
	NLBL_CIPSOV4_C_REMOVE,
	NLBL_CIPSOV4_C_LIST,
	NLBL_CIPSOV4_C_LISTALL,
	__NLBL_CIPSOV4_C_MAX,
};
#define NLBL_CIPSOV4_C_MAX (__NLBL_CIPSOV4_C_MAX - 1)

/* NetLabel protocol functions */
int netlbl_cipsov4_genl_init(void);

#endif
