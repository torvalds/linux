/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * NetLabel CALIPSO Support
 *
 * This file defines the CALIPSO functions for the NetLabel system.  The
 * NetLabel system manages static and dynamic label mappings for network
 * protocols such as CIPSO and RIPSO.
 *
 * Authors: Paul Moore <paul@paul-moore.com>
 *          Huw Davies <huw@codeweavers.com>
 */

/* (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 * (c) Copyright Huw Davies <huw@codeweavers.com>, 2015
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
 * o REMOVE:
 *   Sent by an application to remove a specific DOI mapping table from the
 *   CALIPSO system.
 *
 *   Required attributes:
 *
 *     NLBL_CALIPSO_A_DOI
 *
 * o LIST:
 *   Sent by an application to list the details of a DOI definition.  On
 *   success the kernel should send a response using the following format.
 *
 *   Required attributes:
 *
 *     NLBL_CALIPSO_A_DOI
 *
 *   The valid response message format depends on the type of the DOI mapping,
 *   the defined formats are shown below.
 *
 *   Required attributes:
 *
 *     NLBL_CALIPSO_A_MTYPE
 *
 *   If using CALIPSO_MAP_PASS no additional attributes are required.
 *
 * o LISTALL:
 *   This message is sent by an application to list the valid DOIs on the
 *   system.  When sent by an application there is no payload and the
 *   NLM_F_DUMP flag should be set.  The kernel should respond with a series of
 *   the following messages.
 *
 *   Required attributes:
 *
 *    NLBL_CALIPSO_A_DOI
 *    NLBL_CALIPSO_A_MTYPE
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
int calipso_doi_remove(u32 doi, struct netlbl_audit *audit_info);
struct calipso_doi *calipso_doi_getdef(u32 doi);
void calipso_doi_putdef(struct calipso_doi *doi_def);
int calipso_doi_walk(u32 *skip_cnt,
		     int (*callback)(struct calipso_doi *doi_def, void *arg),
		     void *cb_arg);
int calipso_sock_getattr(struct sock *sk, struct netlbl_lsm_secattr *secattr);
int calipso_sock_setattr(struct sock *sk,
			 const struct calipso_doi *doi_def,
			 const struct netlbl_lsm_secattr *secattr);
void calipso_sock_delattr(struct sock *sk);
int calipso_req_setattr(struct request_sock *req,
			const struct calipso_doi *doi_def,
			const struct netlbl_lsm_secattr *secattr);
void calipso_req_delattr(struct request_sock *req);
unsigned char *calipso_optptr(const struct sk_buff *skb);
int calipso_getattr(const unsigned char *calipso,
		    struct netlbl_lsm_secattr *secattr);
int calipso_skbuff_setattr(struct sk_buff *skb,
			   const struct calipso_doi *doi_def,
			   const struct netlbl_lsm_secattr *secattr);
int calipso_skbuff_delattr(struct sk_buff *skb);
void calipso_cache_invalidate(void);
int calipso_cache_add(const unsigned char *calipso_ptr,
		      const struct netlbl_lsm_secattr *secattr);

#endif
