/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: ip_dstlist.h,v 1.5.2.6 2012/07/22 08:04:23 darren_r Exp $
 */

#ifndef	__IP_DSTLIST_H__
#define	__IP_DSTLIST_H__

typedef struct ipf_dstnode {
	struct ipf_dstnode	*ipfd_next;
	struct ipf_dstnode	**ipfd_pnext;
	ipfmutex_t		ipfd_lock;
	frdest_t		ipfd_dest;
	u_long			ipfd_syncat;
	int			ipfd_flags;
	int			ipfd_size;
	int			ipfd_states;
	int			ipfd_ref;
	int			ipfd_uid;
	char			ipfd_names[1];
} ipf_dstnode_t;

typedef enum ippool_policy_e {
	IPLDP_NONE = 0,
	IPLDP_ROUNDROBIN,
	IPLDP_CONNECTION,
	IPLDP_RANDOM,
	IPLDP_HASHED,
	IPLDP_SRCHASH,
	IPLDP_DSTHASH
} ippool_policy_t;

typedef struct ippool_dst {
	struct ippool_dst	*ipld_next;
	struct ippool_dst	**ipld_pnext;
	ipfmutex_t		ipld_lock;
	int			ipld_seed;
	int			ipld_unit;
	int			ipld_ref;
	int			ipld_flags;
	int			ipld_nodes;
	int			ipld_maxnodes;
	ippool_policy_t		ipld_policy;
	ipf_dstnode_t		**ipld_dests;
	ipf_dstnode_t		*ipld_selected;
	char			ipld_name[FR_GROUPLEN];
} ippool_dst_t;

#define	IPDST_DELETE		0x01

typedef	struct dstlist_stat_s {
	void			*ipls_list[LOOKUP_POOL_SZ];
	int			ipls_numlists;
	u_long			ipls_nomem;
	int			ipls_numnodes;
	int			ipls_numdereflists;
	int			ipls_numderefnodes;
} ipf_dstl_stat_t;

extern ipf_lookup_t ipf_dstlist_backend;

extern int ipf_dstlist_select_node __P((fr_info_t *, void *, u_32_t *,
					frdest_t *));

#endif /* __IP_DSTLIST_H__ */
