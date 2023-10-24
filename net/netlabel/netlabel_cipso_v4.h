/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * NetLabel CIPSO/IPv4 Support
 *
 * This file defines the CIPSO/IPv4 functions for the NetLabel system.  The
 * NetLabel system manages static and dynamic label mappings for network
 * protocols such as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 */

#ifndef _NETLABEL_CIPSO_V4
#define _NETLABEL_CIPSO_V4

#include <net/netlabel.h>

/*
 * The following NetLabel payloads are supported by the CIPSO subsystem.
 *
 * o ADD:
 *   Sent by an application to add a new DOI mapping table.
 *
 *   Required attributes:
 *
 *     NLBL_CIPSOV4_A_DOI
 *     NLBL_CIPSOV4_A_MTYPE
 *     NLBL_CIPSOV4_A_TAGLST
 *
 *   If using CIPSO_V4_MAP_TRANS the following attributes are required:
 *
 *     NLBL_CIPSOV4_A_MLSLVLLST
 *     NLBL_CIPSOV4_A_MLSCATLST
 *
 *   If using CIPSO_V4_MAP_PASS or CIPSO_V4_MAP_LOCAL no additional attributes
 *   are required.
 *
 * o REMOVE:
 *   Sent by an application to remove a specific DOI mapping table from the
 *   CIPSO V4 system.
 *
 *   Required attributes:
 *
 *     NLBL_CIPSOV4_A_DOI
 *
 * o LIST:
 *   Sent by an application to list the details of a DOI definition.  On
 *   success the kernel should send a response using the following format.
 *
 *   Required attributes:
 *
 *     NLBL_CIPSOV4_A_DOI
 *
 *   The valid response message format depends on the type of the DOI mapping,
 *   the defined formats are shown below.
 *
 *   Required attributes:
 *
 *     NLBL_CIPSOV4_A_MTYPE
 *     NLBL_CIPSOV4_A_TAGLST
 *
 *   If using CIPSO_V4_MAP_TRANS the following attributes are required:
 *
 *     NLBL_CIPSOV4_A_MLSLVLLST
 *     NLBL_CIPSOV4_A_MLSCATLST
 *
 *   If using CIPSO_V4_MAP_PASS or CIPSO_V4_MAP_LOCAL no additional attributes
 *   are required.
 *
 * o LISTALL:
 *   This message is sent by an application to list the valid DOIs on the
 *   system.  When sent by an application there is no payload and the
 *   NLM_F_DUMP flag should be set.  The kernel should respond with a series of
 *   the following messages.
 *
 *   Required attributes:
 *
 *    NLBL_CIPSOV4_A_DOI
 *    NLBL_CIPSOV4_A_MTYPE
 *
 */

/* NetLabel CIPSOv4 commands */
enum {
	NLBL_CIPSOV4_C_UNSPEC,
	NLBL_CIPSOV4_C_ADD,
	NLBL_CIPSOV4_C_REMOVE,
	NLBL_CIPSOV4_C_LIST,
	NLBL_CIPSOV4_C_LISTALL,
	__NLBL_CIPSOV4_C_MAX,
};

/* NetLabel CIPSOv4 attributes */
enum {
	NLBL_CIPSOV4_A_UNSPEC,
	NLBL_CIPSOV4_A_DOI,
	/* (NLA_U32)
	 * the DOI value */
	NLBL_CIPSOV4_A_MTYPE,
	/* (NLA_U32)
	 * the mapping table type (defined in the cipso_ipv4.h header as
	 * CIPSO_V4_MAP_*) */
	NLBL_CIPSOV4_A_TAG,
	/* (NLA_U8)
	 * a CIPSO tag type, meant to be used within a NLBL_CIPSOV4_A_TAGLST
	 * attribute */
	NLBL_CIPSOV4_A_TAGLST,
	/* (NLA_NESTED)
	 * the CIPSO tag list for the DOI, there must be at least one
	 * NLBL_CIPSOV4_A_TAG attribute, tags listed first are given higher
	 * priorirty when sending packets */
	NLBL_CIPSOV4_A_MLSLVLLOC,
	/* (NLA_U32)
	 * the local MLS sensitivity level */
	NLBL_CIPSOV4_A_MLSLVLREM,
	/* (NLA_U32)
	 * the remote MLS sensitivity level */
	NLBL_CIPSOV4_A_MLSLVL,
	/* (NLA_NESTED)
	 * a MLS sensitivity level mapping, must contain only one attribute of
	 * each of the following types: NLBL_CIPSOV4_A_MLSLVLLOC and
	 * NLBL_CIPSOV4_A_MLSLVLREM */
	NLBL_CIPSOV4_A_MLSLVLLST,
	/* (NLA_NESTED)
	 * the CIPSO level mappings, there must be at least one
	 * NLBL_CIPSOV4_A_MLSLVL attribute */
	NLBL_CIPSOV4_A_MLSCATLOC,
	/* (NLA_U32)
	 * the local MLS category */
	NLBL_CIPSOV4_A_MLSCATREM,
	/* (NLA_U32)
	 * the remote MLS category */
	NLBL_CIPSOV4_A_MLSCAT,
	/* (NLA_NESTED)
	 * a MLS category mapping, must contain only one attribute of each of
	 * the following types: NLBL_CIPSOV4_A_MLSCATLOC and
	 * NLBL_CIPSOV4_A_MLSCATREM */
	NLBL_CIPSOV4_A_MLSCATLST,
	/* (NLA_NESTED)
	 * the CIPSO category mappings, there must be at least one
	 * NLBL_CIPSOV4_A_MLSCAT attribute */
	__NLBL_CIPSOV4_A_MAX,
};
#define NLBL_CIPSOV4_A_MAX (__NLBL_CIPSOV4_A_MAX - 1)

/* NetLabel protocol functions */
int netlbl_cipsov4_genl_init(void);

#endif
