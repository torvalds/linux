/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_IDMAP_H
#define	_SYS_IDMAP_H


/* Idmap status codes */
#define	IDMAP_SUCCESS			0
#define	IDMAP_NEXT			1
#define	IDMAP_ERR_OTHER			-10000
#define	IDMAP_ERR_INTERNAL		-9999
#define	IDMAP_ERR_MEMORY		-9998
#define	IDMAP_ERR_NORESULT		-9997
#define	IDMAP_ERR_NOTUSER		-9996
#define	IDMAP_ERR_NOTGROUP		-9995
#define	IDMAP_ERR_NOTSUPPORTED		-9994
#define	IDMAP_ERR_W2U_NAMERULE		-9993
#define	IDMAP_ERR_U2W_NAMERULE		-9992
#define	IDMAP_ERR_CACHE			-9991
#define	IDMAP_ERR_DB			-9990
#define	IDMAP_ERR_ARG			-9989
#define	IDMAP_ERR_SID			-9988
#define	IDMAP_ERR_IDTYPE		-9987
#define	IDMAP_ERR_RPC_HANDLE		-9986
#define	IDMAP_ERR_RPC			-9985
#define	IDMAP_ERR_CLIENT_HANDLE		-9984
#define	IDMAP_ERR_BUSY			-9983
#define	IDMAP_ERR_PERMISSION_DENIED	-9982
#define	IDMAP_ERR_NOMAPPING		-9981
#define	IDMAP_ERR_NEW_ID_ALLOC_REQD	-9980
#define	IDMAP_ERR_DOMAIN		-9979
#define	IDMAP_ERR_SECURITY		-9978
#define	IDMAP_ERR_NOTFOUND		-9977
#define	IDMAP_ERR_DOMAIN_NOTFOUND	-9976
#define	IDMAP_ERR_UPDATE_NOTALLOWED	-9975
#define	IDMAP_ERR_CFG			-9974
#define	IDMAP_ERR_CFG_CHANGE		-9973
#define	IDMAP_ERR_NOTMAPPED_WELLKNOWN	-9972
#define	IDMAP_ERR_RETRIABLE_NET_ERR	-9971
#define	IDMAP_ERR_W2U_NAMERULE_CONFLICT	-9970
#define	IDMAP_ERR_U2W_NAMERULE_CONFLICT	-9969
#define	IDMAP_ERR_BAD_UTF8		-9968
#define	IDMAP_ERR_NONE_GENERATED	-9967
#define	IDMAP_ERR_PROP_UNKNOWN		-9966
#define	IDMAP_ERR_NS_LDAP_OP_FAILED	-9965
#define	IDMAP_ERR_NS_LDAP_PARTIAL	-9964
#define	IDMAP_ERR_NS_LDAP_CFG		-9963
#define	IDMAP_ERR_NS_LDAP_BAD_WINNAME	-9962
#define	IDMAP_ERR_NO_ACTIVEDIRECTORY	-9961

/* Reserved GIDs for some well-known SIDs */
#define	IDMAP_WK_LOCAL_SYSTEM_GID	2147483648U /* 0x80000000 */
#define	IDMAP_WK_CREATOR_GROUP_GID	2147483649U
#define	IDMAP_WK__MAX_GID		2147483649U

/* Reserved UIDs for some well-known SIDs */
#define	IDMAP_WK_CREATOR_OWNER_UID	2147483648U
#define	IDMAP_WK__MAX_UID		2147483648U

/* Reserved SIDs */
#define	IDMAP_WK_CREATOR_SID_AUTHORITY	"S-1-3"

/*
 * Max door RPC size for ID mapping (can't be too large relative to the
 * default user-land thread stack size, since clnt_door_call()
 * alloca()s).  See libidmap:idmap_init().
 */
#define	IDMAP_MAX_DOOR_RPC		(256 * 1024)

#define	IDMAP_SENTINEL_PID		UINT32_MAX
#define	IDMAP_ID_IS_EPHEMERAL(pid)	\
	(((pid) > INT32_MAX) && ((pid) != IDMAP_SENTINEL_PID))

#endif /* _SYS_IDMAP_H */
