/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#ifndef	__IP_POOL_H__
#define	__IP_POOL_H__

#include "netinet/ip_lookup.h"
#include "radix_ipf.h"

#define	IP_POOL_NOMATCH		0
#define	IP_POOL_POSITIVE	1

typedef	struct ip_pool_node {
	ipf_rdx_node_t		ipn_nodes[2];
	addrfamily_t		ipn_addr;
	addrfamily_t		ipn_mask;
	int			ipn_uid;
	int			ipn_info;
	int			ipn_ref;
	char			ipn_name[FR_GROUPLEN];
	U_QUAD_T		ipn_hits;
	U_QUAD_T		ipn_bytes;
	u_long			ipn_die;
	struct ip_pool_node	*ipn_next, **ipn_pnext;
	struct ip_pool_node	*ipn_dnext, **ipn_pdnext;
	struct ip_pool_s	*ipn_owner;
} ip_pool_node_t;


typedef	struct ip_pool_s {
	struct ip_pool_s	*ipo_next;
	struct ip_pool_s	**ipo_pnext;
	ipf_rdx_head_t		*ipo_head;
	ip_pool_node_t		*ipo_list;
	ip_pool_node_t		**ipo_tail;
	ip_pool_node_t		*ipo_nextaddr;
	void			*ipo_radix;
	u_long			ipo_hits;
	int			ipo_unit;
	int			ipo_flags;
	int			ipo_ref;
	char			ipo_name[FR_GROUPLEN];
} ip_pool_t;

#define	IPOOL_DELETE	0x01
#define	IPOOL_ANON	0x02


typedef	struct	ipf_pool_stat	{
	u_long			ipls_pools;
	u_long			ipls_tables;
	u_long			ipls_nodes;
	ip_pool_t		*ipls_list[LOOKUP_POOL_SZ];
} ipf_pool_stat_t;

extern	ipf_lookup_t	ipf_pool_backend;

#ifndef _KERNEL
extern	void	ipf_pool_dump __P((ipf_main_softc_t *, void *));
#endif

#endif /* __IP_POOL_H__ */
