/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * @(#)ip_frag.h	1.5 3/24/96
 * $FreeBSD$
 * Id: ip_frag.h,v 2.23.2.1 2004/03/29 16:21:56 darrenr Exp
 */

#ifndef	__IP_FRAG_H__
#define	__IP_FRAG_H__

#define	IPFT_SIZE	257

typedef	struct	ipfr	{
	struct	ipfr	*ipfr_hnext, **ipfr_hprev;
	struct	ipfr	*ipfr_next, **ipfr_prev;
	void	*ipfr_data;
	frentry_t *ipfr_rule;
	u_long	ipfr_ttl;
	u_int	ipfr_pkts;
	u_int	ipfr_bytes;
	u_int	ipfr_badorder;
	int	ipfr_ref;
	u_short	ipfr_off;
	u_short	ipfr_firstend;
	u_char	ipfr_p;
	u_char	ipfr_seen0;
	/*
	 * All of the fields, from ipfr_ifp to ipfr_pass, are compared
	 * using bcmp to see if an identical entry is present.  It is
	 * therefore important for this set to remain together.
	 */
	void	*ipfr_ifp;
	i6addr_t	ipfr_source;
	i6addr_t	ipfr_dest;
	u_32_t	ipfr_optmsk;
	u_short	ipfr_secmsk;
	u_short	ipfr_auth;
	u_32_t	ipfr_id;
	u_32_t	ipfr_pass;
	int	ipfr_v;
} ipfr_t;

#define	ipfr_src	ipfr_source.in4
#define	ipfr_dst	ipfr_dest.in4


typedef	struct	ipfrstat {
	u_long	ifs_exists;	/* add & already exists */
	u_long	ifs_nomem;
	u_long	ifs_new;
	u_long	ifs_hits;
	u_long	ifs_expire;
	u_long	ifs_inuse;
	u_long	ifs_retrans0;
	u_long	ifs_short;
	u_long	ifs_bad;
	u_long	ifs_overlap;
	u_long	ifs_unordered;
	u_long	ifs_strict;
	u_long	ifs_miss;
	u_long	ifs_maximum;
	u_long	ifs_newbad;
	u_long	ifs_newrestrictnot0;
	struct	ipfr	**ifs_table;
	struct	ipfr	**ifs_nattab;
} ipfrstat_t;

typedef struct ipf_frag_softc_s  {
	ipfrwlock_t	ipfr_ipidfrag;
	ipfrwlock_t	ipfr_frag;
	ipfrwlock_t	ipfr_natfrag;
	int		ipfr_size;
	int		ipfr_ttl;
	int		ipfr_lock;
	int		ipfr_inited;
	ipftuneable_t	*ipf_frag_tune;
	ipfr_t		*ipfr_list;
	ipfr_t		**ipfr_tail;
	ipfr_t		*ipfr_natlist;
	ipfr_t		**ipfr_nattail;
	ipfr_t		*ipfr_ipidlist;
	ipfr_t		**ipfr_ipidtail;
	ipfr_t		**ipfr_heads;
	ipfr_t		**ipfr_nattab;
	ipfr_t		**ipfr_ipidtab;
	ipfrstat_t	ipfr_stats;
} ipf_frag_softc_t;

#define	IPFR_CMPSZ	(offsetof(ipfr_t, ipfr_pass) - \
			 offsetof(ipfr_t, ipfr_ifp))

extern	void	*ipf_frag_soft_create __P((ipf_main_softc_t *));
extern	int	ipf_frag_soft_init __P((ipf_main_softc_t *, void *));
extern	int	ipf_frag_soft_fini __P((ipf_main_softc_t *, void *));
extern	void	ipf_frag_soft_destroy __P((ipf_main_softc_t *, void *));
extern	int	ipf_frag_main_load __P((void));
extern	int	ipf_frag_main_unload __P((void));
extern	int	ipf_frag_load __P((void));
extern	void	ipf_frag_clear __P((ipf_main_softc_t *));
extern	void	ipf_frag_expire __P((ipf_main_softc_t *));
extern	void	ipf_frag_forget __P((void *));
extern	int	ipf_frag_init __P((void));
extern	u_32_t	ipf_frag_ipidknown __P((fr_info_t *));
extern	int	ipf_frag_ipidnew __P((fr_info_t *, u_32_t));
extern	frentry_t *ipf_frag_known __P((fr_info_t *, u_32_t *));
extern	void	ipf_frag_natforget __P((ipf_main_softc_t *, void *));
extern	int	ipf_frag_natnew __P((ipf_main_softc_t *, fr_info_t *, u_32_t, struct nat *));
extern	nat_t	*ipf_frag_natknown __P((fr_info_t *));
extern	int	ipf_frag_new __P((ipf_main_softc_t *, fr_info_t *, u_32_t));
extern	ipfrstat_t	*ipf_frag_stats __P((void *));
extern	void	ipf_frag_setlock __P((void *, int));
extern	void	ipf_frag_pkt_deref __P((ipf_main_softc_t *, void *));
extern	int	ipf_frag_pkt_next __P((ipf_main_softc_t *, ipftoken_t *,
				       ipfgeniter_t *));
extern	void	ipf_frag_nat_deref __P((ipf_main_softc_t *, void *));
extern	int	ipf_frag_nat_next __P((ipf_main_softc_t *, ipftoken_t *,
				       ipfgeniter_t *));
extern	void	ipf_slowtimer __P((ipf_main_softc_t *));

#endif	/* __IP_FRAG_H__ */
