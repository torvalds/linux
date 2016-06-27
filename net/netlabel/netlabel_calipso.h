/*
 * NetLabel CALIPSO Support
 *
 * This file defines the CALIPSO functions for the NetLabel system.  The
 * NetLabel system manages static and dynamic label mappings for network
 * protocols such as CIPSO and RIPSO.
 *
 * Authors: Paul Moore <paul@paul-moore.com>
 *          Huw Davies <huw@codeweavers.com>
 *
 */

/* (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 * (c) Copyright Huw Davies <huw@codeweavers.com>, 2015
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
 * along with this program;  if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _NETLABEL_CALIPSO
#define _NETLABEL_CALIPSO

#include <net/netlabel.h>
#include <net/calipso.h>

/* The following NetLabel payloads are supported by the CALIPSO subsystem.
 *
 * o ADD:
 *   Sent by an application to add a new DOI mapping table.
 *
 *   Required attributes:
 *
 *     NLBL_CALIPSO_A_DOI
 *     NLBL_CALIPSO_A_MTYPE
 *
 *   If using CALIPSO_MAP_PASS no additional attributes are required.
 *
 */

/* NetLabel CALIPSO commands */
enum {
	NLBL_CALIPSO_C_UNSPEC,
	NLBL_CALIPSO_C_ADD,
	NLBL_CALIPSO_C_REMOVE,
	NLBL_CALIPSO_C_LIST,
	NLBL_CALIPSO_C_LISTALL,
	__NLBL_CALIPSO_C_MAX,
};

/* NetLabel CALIPSO attributes */
enum {
	NLBL_CALIPSO_A_UNSPEC,
	NLBL_CALIPSO_A_DOI,
	/* (NLA_U32)
	 * the DOI value */
	NLBL_CALIPSO_A_MTYPE,
	/* (NLA_U32)
	 * the mapping table type (defined in the calipso.h header as
	 * CALIPSO_MAP_*) */
	__NLBL_CALIPSO_A_MAX,
};

#define NLBL_CALIPSO_A_MAX (__NLBL_CALIPSO_A_MAX - 1)

/* NetLabel protocol functions */
#if IS_ENABLED(CONFIG_IPV6)
int netlbl_calipso_genl_init(void);
#else
static inline int netlbl_calipso_genl_init(void)
{
	return 0;
}
#endif

int calipso_doi_add(struct calipso_doi *doi_def,
		    struct netlbl_audit *audit_info);
void calipso_doi_free(struct calipso_doi *doi_def);

#endif
