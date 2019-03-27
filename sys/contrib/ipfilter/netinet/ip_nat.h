/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * @(#)ip_nat.h	1.5 2/4/96
 * $FreeBSD$
 * Id: ip_nat.h,v 2.90.2.20 2007/09/25 08:27:32 darrenr Exp $
 */

#ifndef	__IP_NAT_H__
#define	__IP_NAT_H__

#ifndef	SOLARIS
# if defined(sun) && defined(__SVR4)
#  define	SOLARIS		1
# else
#  define	SOLARIS		0
# endif
#endif

#if defined(__STDC__) || defined(__GNUC__) || defined(_AIX51)
#define	SIOCADNAT	_IOW('r', 60, struct ipfobj)
#define	SIOCRMNAT	_IOW('r', 61, struct ipfobj)
#define	SIOCGNATS	_IOWR('r', 62, struct ipfobj)
#define	SIOCGNATL	_IOWR('r', 63, struct ipfobj)
#define	SIOCPURGENAT	_IOWR('r', 100, struct ipfobj)
#else
#define	SIOCADNAT	_IOW(r, 60, struct ipfobj)
#define	SIOCRMNAT	_IOW(r, 61, struct ipfobj)
#define	SIOCGNATS	_IOWR(r, 62, struct ipfobj)
#define	SIOCGNATL	_IOWR(r, 63, struct ipfobj)
#define	SIOCPURGENAT	_IOWR(r, 100, struct ipfobj)
#endif

#undef	LARGE_NAT	/* define	this if you're setting up a system to NAT
			 * LARGE numbers of networks/hosts - i.e. in the
			 * hundreds or thousands.  In such a case, you should
			 * also change the RDR_SIZE and NAT_SIZE below to more
			 * appropriate sizes.  The figures below were used for
			 * a setup with 1000-2000 networks to NAT.
			 */
#ifndef NAT_SIZE
# ifdef LARGE_NAT
#  define	NAT_SIZE	2047
# else
#  define	NAT_SIZE	127
# endif
#endif
#ifndef RDR_SIZE
# ifdef LARGE_NAT
#  define	RDR_SIZE	2047
# else
#  define	RDR_SIZE	127
# endif
#endif
#ifndef HOSTMAP_SIZE
# ifdef LARGE_NAT
#  define	HOSTMAP_SIZE	8191
# else
#  define	HOSTMAP_SIZE	2047
# endif
#endif
#ifndef NAT_TABLE_MAX
/*
 * This is newly introduced and for the sake of "least surprise", the numbers
 * present aren't what we'd normally use for creating a proper hash table.
 */
# ifdef	LARGE_NAT
#  define	NAT_TABLE_MAX	180000
# else
#  define	NAT_TABLE_MAX	30000
# endif
#endif
#ifndef NAT_TABLE_SZ
# ifdef LARGE_NAT
#  define	NAT_TABLE_SZ	16383
# else
#  define	NAT_TABLE_SZ	2047
# endif
#endif
#ifndef	APR_LABELLEN
#define	APR_LABELLEN	16
#endif
#define	NAT_HW_CKSUM		0x80000000
#define	NAT_HW_CKSUM_PART	0x40000000

#define	DEF_NAT_AGE	1200     /* 10 minutes (600 seconds) */

struct ipstate;
struct ap_session;

/*
 * This structure is used in the active NAT table and represents an
 * active NAT session.
 */
typedef	struct	nat	{
	ipfmutex_t	nat_lock;
	struct	nat	*nat_next;
	struct	nat	**nat_pnext;
	struct	nat	*nat_hnext[2];
	struct	nat	**nat_phnext[2];
	struct	hostmap	*nat_hm;
	void		*nat_data;
	struct	nat	**nat_me;
	struct	ipstate	*nat_state;
	struct	ap_session	*nat_aps;		/* proxy session */
	frentry_t	*nat_fr;	/* filter rule ptr if appropriate */
	struct	ipnat	*nat_ptr;	/* pointer back to the rule */
	void		*nat_ifps[2];
	void		*nat_sync;
	ipftqent_t	nat_tqe;
	int		nat_mtu[2];
	u_32_t		nat_flags;
	u_32_t		nat_sumd[2];	/* ip checksum delta for data segment*/
	u_32_t		nat_ipsumd;	/* ip checksum delta for ip header */
	u_32_t		nat_mssclamp;	/* if != zero clamp MSS to this */
	i6addr_t	nat_odst6;
	i6addr_t	nat_osrc6;
	i6addr_t	nat_ndst6;
	i6addr_t	nat_nsrc6;
	U_QUAD_T	nat_pkts[2];
	U_QUAD_T	nat_bytes[2];
	union	{
		udpinfo_t	nat_unu;
		tcpinfo_t	nat_unt;
		icmpinfo_t	nat_uni;
		greinfo_t	nat_ugre;
	} nat_unold, nat_unnew;
	int		nat_use;
	int		nat_pr[2];		/* protocol for NAT */
	int		nat_dir;
	int		nat_ref;		/* reference count */
	u_int		nat_hv[2];
	char		nat_ifnames[2][LIFNAMSIZ];
	int		nat_rev;		/* 0 = forward, 1 = reverse */
	int		nat_dlocal;
	int		nat_v[2];		/* 0 = old, 1 = new */
	u_int		nat_redir;		/* copy of in_redir */
} nat_t;

#define	nat_osrcip	nat_osrc6.in4
#define	nat_odstip	nat_odst6.in4
#define	nat_nsrcip	nat_nsrc6.in4
#define	nat_ndstip	nat_ndst6.in4
#define	nat_osrcaddr	nat_osrc6.in4.s_addr
#define	nat_odstaddr	nat_odst6.in4.s_addr
#define	nat_nsrcaddr	nat_nsrc6.in4.s_addr
#define	nat_ndstaddr	nat_ndst6.in4.s_addr
#define	nat_age		nat_tqe.tqe_die
#define	nat_osport	nat_unold.nat_unt.ts_sport
#define	nat_odport	nat_unold.nat_unt.ts_dport
#define	nat_nsport	nat_unnew.nat_unt.ts_sport
#define	nat_ndport	nat_unnew.nat_unt.ts_dport
#define	nat_oicmpid	nat_unold.nat_uni.ici_id
#define	nat_nicmpid	nat_unnew.nat_uni.ici_id
#define	nat_type	nat_unold.nat_uni.ici_type
#define	nat_oseq	nat_unold.nat_uni.ici_seq
#define	nat_nseq	nat_unnew.nat_uni.ici_seq
#define	nat_tcpstate	nat_tqe.tqe_state
#define	nat_die		nat_tqe.tqe_die
#define	nat_touched	nat_tqe.tqe_touched

/*
 * Values for nat_dir
 */
#define	NAT_INBOUND	0
#define	NAT_OUTBOUND	1
#define	NAT_ENCAPIN	2
#define	NAT_ENCAPOUT	3
#define	NAT_DIVERTIN	4
#define	NAT_DIVERTOUT	5

/*
 * Definitions for nat_flags
 */
#define	NAT_TCP		0x0001	/* IPN_TCP */
#define	NAT_UDP		0x0002	/* IPN_UDP */
#define	NAT_ICMPERR	0x0004	/* IPN_ICMPERR */
#define	NAT_ICMPQUERY	0x0008	/* IPN_ICMPQUERY */
#define	NAT_SEARCH	0x0010
#define	NAT_SLAVE	0x0020	/* Slave connection for a proxy */
#define	NAT_NOTRULEPORT	0x0040	/* Don't use the port # in the NAT rule */

#define	NAT_TCPUDP	(NAT_TCP|NAT_UDP)
#define	NAT_TCPUDPICMP	(NAT_TCP|NAT_UDP|NAT_ICMPERR)
#define	NAT_TCPUDPICMPQ	(NAT_TCP|NAT_UDP|NAT_ICMPQUERY)
#define	NAT_FROMRULE	(NAT_TCP|NAT_UDP)

/* 0x0100 reserved for FI_W_SPORT */
/* 0x0200 reserved for FI_W_DPORT */
/* 0x0400 reserved for FI_W_SADDR */
/* 0x0800 reserved for FI_W_DADDR */
/* 0x1000 reserved for FI_W_NEWFR */
/* 0x2000 reserved for SI_CLONE */
/* 0x4000 reserved for SI_CLONED */
/* 0x8000 reserved for SI_IGNOREPKT */

#define	NAT_DEBUG	0x800000

typedef	struct nat_addr_s {
	i6addr_t	na_addr[2];
	i6addr_t	na_nextaddr;
	int		na_atype;
	int		na_function;
} nat_addr_t;

#define	na_nextip	na_nextaddr.in4.s_addr
#define	na_nextip6	na_nextaddr.in6
#define	na_num		na_addr[0].iplookupnum
#define	na_type		na_addr[0].iplookuptype
#define	na_subtype	na_addr[0].iplookupsubtype
#define	na_ptr		na_addr[1].iplookupptr
#define	na_func		na_addr[1].iplookupfunc


/*
 * This structure represents an actual NAT rule, loaded by ipnat.
 */
typedef	struct	ipnat	{
	ipfmutex_t	in_lock;
	struct	ipnat	*in_next;		/* NAT rule list next */
	struct	ipnat	**in_pnext;		/* prior rdr next ptr */
	struct	ipnat	*in_rnext;		/* rdr rule hash next */
	struct	ipnat	**in_prnext;		/* prior rdr next ptr */
	struct	ipnat	*in_mnext;		/* map rule hash next */
	struct	ipnat	**in_pmnext;		/* prior map next ptr */
	struct	ipftq	*in_tqehead[2];
	void		*in_ifps[2];
	void		*in_apr;
	char		*in_comment;
	mb_t		*in_divmp;
	void		*in_pconf;
	U_QUAD_T	in_pkts[2];
	U_QUAD_T	in_bytes[2];
	u_long		in_space;
	u_long		in_hits;
	int		in_size;
	int		in_use;
	u_int		in_hv[2];
	int		in_flineno;		/* conf. file line number */
	int		in_stepnext;
	int		in_dlocal;
	u_short		in_dpnext;
	u_short		in_spnext;
	/* From here to the end is covered by IPN_CMPSIZ */
	u_char		in_v[2];		/* 0 = old, 1 = new */
	u_32_t		in_flags;
	u_32_t		in_mssclamp;		/* if != 0 clamp MSS to this */
	u_int		in_age[2];
	int		in_redir;		/* see below for values */
	int		in_pr[2];		/* protocol. */
	nat_addr_t	in_ndst;
	nat_addr_t	in_nsrc;
	nat_addr_t	in_osrc;
	nat_addr_t	in_odst;
	frtuc_t		in_tuc;
	u_short		in_ppip;		/* ports per IP. */
	u_short		in_ippip;		/* IP #'s per IP# */
	u_short		in_ndports[2];
	u_short		in_nsports[2];
	int		in_ifnames[2];
	int		in_plabel;	/* proxy label. */
	int		in_pconfig;	/* proxy label. */
	ipftag_t	in_tag;
	int		in_namelen;
	char		in_names[1];
} ipnat_t;

/*
 *      MAP-IN MAP-OUT RDR-IN RDR-OUT
 * osrc    X   == src  == src    X
 * odst    X   == dst  == dst    X
 * nsrc == dst   X       X    == dst
 * ndst == src   X       X    == src
 */
#define	in_dpmin	in_ndports[0]	/* Also holds static redir port */
#define	in_dpmax	in_ndports[1]
#define	in_spmin	in_nsports[0]	/* Also holds static redir port */
#define	in_spmax	in_nsports[1]
#define	in_ndport	in_ndports[0]
#define	in_nsport	in_nsports[0]
#define	in_dipnext	in_ndst.na_nextaddr.in4
#define	in_dipnext6	in_ndst.na_nextaddr
#define	in_dnip		in_ndst.na_nextaddr.in4.s_addr
#define	in_dnip6	in_ndst.na_nextaddr
#define	in_sipnext	in_nsrc.na_nextaddr.in4
#define	in_snip		in_nsrc.na_nextaddr.in4.s_addr
#define	in_snip6	in_nsrc.na_nextaddr
#define	in_odstip	in_odst.na_addr[0].in4
#define	in_odstip6	in_odst.na_addr[0]
#define	in_odstaddr	in_odst.na_addr[0].in4.s_addr
#define	in_odstmsk	in_odst.na_addr[1].in4.s_addr
#define	in_odstmsk6	in_odst.na_addr[1]
#define	in_odstatype	in_odst.na_atype
#define	in_osrcip	in_osrc.na_addr[0].in4
#define	in_osrcip6	in_osrc.na_addr[0]
#define	in_osrcaddr	in_osrc.na_addr[0].in4.s_addr
#define	in_osrcmsk	in_osrc.na_addr[1].in4.s_addr
#define	in_osrcmsk6	in_osrc.na_addr[1]
#define	in_osrcatype	in_osrc.na_atype
#define	in_ndstip	in_ndst.na_addr[0].in4
#define	in_ndstip6	in_ndst.na_addr[0]
#define	in_ndstaddr	in_ndst.na_addr[0].in4.s_addr
#define	in_ndstmsk	in_ndst.na_addr[1].in4.s_addr
#define	in_ndstmsk6	in_ndst.na_addr[1]
#define	in_ndstatype	in_ndst.na_atype
#define	in_ndstafunc	in_ndst.na_function
#define	in_nsrcip	in_nsrc.na_addr[0].in4
#define	in_nsrcip6	in_nsrc.na_addr[0]
#define	in_nsrcaddr	in_nsrc.na_addr[0].in4.s_addr
#define	in_nsrcmsk	in_nsrc.na_addr[1].in4.s_addr
#define	in_nsrcmsk6	in_nsrc.na_addr[1]
#define	in_nsrcatype	in_nsrc.na_atype
#define	in_nsrcafunc	in_nsrc.na_function
#define	in_scmp		in_tuc.ftu_scmp
#define	in_dcmp		in_tuc.ftu_dcmp
#define	in_stop		in_tuc.ftu_stop
#define	in_dtop		in_tuc.ftu_dtop
#define	in_osport	in_tuc.ftu_sport
#define	in_odport	in_tuc.ftu_dport
#define	in_ndstnum	in_ndst.na_addr[0].iplookupnum
#define	in_ndsttype	in_ndst.na_addr[0].iplookuptype
#define	in_ndstptr	in_ndst.na_addr[1].iplookupptr
#define	in_ndstfunc	in_ndst.na_addr[1].iplookupfunc
#define	in_nsrcnum	in_nsrc.na_addr[0].iplookupnum
#define	in_nsrctype	in_nsrc.na_addr[0].iplookuptype
#define	in_nsrcptr	in_nsrc.na_addr[1].iplookupptr
#define	in_nsrcfunc	in_nsrc.na_addr[1].iplookupfunc
#define	in_odstnum	in_odst.na_addr[0].iplookupnum
#define	in_odsttype	in_odst.na_addr[0].iplookuptype
#define	in_odstptr	in_odst.na_addr[1].iplookupptr
#define	in_odstfunc	in_odst.na_addr[1].iplookupfunc
#define	in_osrcnum	in_osrc.na_addr[0].iplookupnum
#define	in_osrctype	in_osrc.na_addr[0].iplookuptype
#define	in_osrcptr	in_osrc.na_addr[1].iplookupptr
#define	in_osrcfunc	in_osrc.na_addr[1].iplookupfunc
#define	in_icmpidmin	in_nsports[0]
#define	in_icmpidmax	in_nsports[1]

/*
 * Bit definitions for in_flags
 */
#define	IPN_ANY		0x00000
#define	IPN_TCP		0x00001
#define	IPN_UDP		0x00002
#define	IPN_TCPUDP	(IPN_TCP|IPN_UDP)
#define	IPN_ICMPERR	0x00004
#define	IPN_TCPUDPICMP	(IPN_TCP|IPN_UDP|IPN_ICMPERR)
#define	IPN_ICMPQUERY	0x00008
#define	IPN_TCPUDPICMPQ	(IPN_TCP|IPN_UDP|IPN_ICMPQUERY)
#define	IPN_RF		(IPN_TCPUDP|IPN_DELETE|IPN_ICMPERR)
#define	IPN_AUTOPORTMAP	0x00010
#define	IPN_FILTER	0x00020
#define	IPN_SPLIT	0x00040
#define	IPN_ROUNDR	0x00080
#define	IPN_SIPRANGE	0x00100
#define	IPN_DIPRANGE	0x00200
#define	IPN_NOTSRC	0x00400
#define	IPN_NOTDST	0x00800
#define	IPN_NO		0x01000
#define	IPN_DYNSRCIP	0x02000	/* dynamic src IP# */
#define	IPN_DYNDSTIP	0x04000	/* dynamic dst IP# */
#define	IPN_DELETE	0x08000
#define	IPN_STICKY	0x10000
#define	IPN_FRAG	0x20000
#define	IPN_FIXEDSPORT	0x40000
#define	IPN_FIXEDDPORT	0x80000
#define	IPN_FINDFORWARD	0x100000
#define	IPN_IN		0x200000
#define	IPN_SEQUENTIAL	0x400000
#define	IPN_PURGE	0x800000
#define	IPN_PROXYRULE	0x1000000
#define	IPN_USERFLAGS	(IPN_TCPUDP|IPN_AUTOPORTMAP|IPN_SIPRANGE|IPN_SPLIT|\
			 IPN_ROUNDR|IPN_FILTER|IPN_NOTSRC|IPN_NOTDST|IPN_NO|\
			 IPN_FRAG|IPN_STICKY|IPN_FIXEDDPORT|IPN_ICMPQUERY|\
			 IPN_DIPRANGE|IPN_SEQUENTIAL|IPN_PURGE)

/*
 * Values for in_redir
 */
#define	NAT_MAP		0x01
#define	NAT_REDIRECT	0x02
#define	NAT_BIMAP	(NAT_MAP|NAT_REDIRECT)
#define	NAT_MAPBLK	0x04
#define	NAT_REWRITE	0x08
#define	NAT_ENCAP	0x10
#define	NAT_DIVERTUDP	0x20

#define	MAPBLK_MINPORT	1024	/* don't use reserved ports for src port */
#define	USABLE_PORTS	(65536 - MAPBLK_MINPORT)

#define	IPN_CMPSIZ	(sizeof(ipnat_t) - offsetof(ipnat_t, in_v))

typedef	struct	natlookup {
	i6addr_t	nl_inipaddr;
	i6addr_t	nl_outipaddr;
	i6addr_t	nl_realipaddr;
	int		nl_v;
	int		nl_flags;
	u_short		nl_inport;
	u_short		nl_outport;
	u_short		nl_realport;
} natlookup_t;

#define	nl_inip		nl_inipaddr.in4
#define	nl_outip	nl_outipaddr.in4
#define	nl_realip	nl_realipaddr.in4
#define	nl_inip6	nl_inipaddr.in6
#define	nl_outip6	nl_outipaddr.in6
#define	nl_realip6	nl_realipaddr.in6


typedef struct  nat_save    {
	void	*ipn_next;
	struct	nat	ipn_nat;
	struct	ipnat	ipn_ipnat;
	struct	frentry ipn_fr;
	int	ipn_dsize;
	char	ipn_data[4];
} nat_save_t;

#define	ipn_rule	ipn_nat.nat_fr

typedef	struct	natget	{
	void	*ng_ptr;
	int	ng_sz;
} natget_t;


/*
 * This structure gets used to help NAT sessions keep the same NAT rule (and
 * thus translation for IP address) when:
 * (a) round-robin redirects are in use
 * (b) different IP add
 */
typedef	struct	hostmap	{
	struct	hostmap	*hm_hnext;
	struct	hostmap	**hm_phnext;
	struct	hostmap	*hm_next;
	struct	hostmap	**hm_pnext;
	struct	ipnat	*hm_ipnat;
	i6addr_t	hm_osrcip6;
	i6addr_t	hm_odstip6;
	i6addr_t	hm_nsrcip6;
	i6addr_t	hm_ndstip6;
	u_32_t		hm_port;
	int		hm_ref;
	int		hm_hv;
	int		hm_v;
} hostmap_t;

#define	hm_osrcip	hm_osrcip6.in4
#define	hm_odstip	hm_odstip6.in4
#define	hm_nsrcip	hm_nsrcip6.in4
#define	hm_ndstip	hm_ndstip6.in4
#define	hm_osrc6	hm_osrcip6.in6
#define	hm_odst6	hm_odstip6.in6
#define	hm_nsrc6	hm_nsrcip6.in6
#define	hm_ndst6	hm_ndstip6.in6


/*
 * Structure used to pass information in to nat_newmap and nat_newrdr.
 */
typedef struct	natinfo	{
	ipnat_t		*nai_np;
	u_32_t		nai_sum1;
	u_32_t		nai_sum2;
	struct	in_addr	nai_ip;		/* In host byte order */
	u_short		nai_port;
	u_short		nai_nport;
	u_short		nai_sport;
	u_short		nai_dport;
} natinfo_t;


typedef	struct nat_stat_side {
	u_int	*ns_bucketlen;
	nat_t	**ns_table;
	u_long	ns_added;
	u_long	ns_appr_fail;
	u_long	ns_badnat;
	u_long	ns_badnatnew;
	u_long	ns_badnextaddr;
	u_long	ns_bucket_max;
	u_long	ns_clone_nomem;
	u_long	ns_decap_bad;
	u_long	ns_decap_fail;
	u_long	ns_decap_pullup;
	u_long	ns_divert_dup;
	u_long	ns_divert_exist;
	u_long	ns_drop;
	u_long	ns_encap_dup;
	u_long	ns_encap_pullup;
	u_long	ns_exhausted;
	u_long	ns_icmp_address;
	u_long	ns_icmp_basic;
	u_long	ns_icmp_mbuf;
	u_long	ns_icmp_notfound;
	u_long	ns_icmp_rebuild;
	u_long	ns_icmp_short;
	u_long	ns_icmp_size;
	u_long	ns_ifpaddrfail;
	u_long	ns_ignored;
	u_long	ns_insert_fail;
	u_long	ns_inuse;
	u_long	ns_log;
	u_long	ns_lookup_miss;
	u_long	ns_lookup_nowild;
	u_long	ns_new_ifpaddr;
	u_long	ns_memfail;
	u_long	ns_table_max;
	u_long	ns_translated;
	u_long	ns_unfinalised;
	u_long	ns_wrap;
	u_long	ns_xlate_null;
	u_long	ns_xlate_exists;
	u_long	ns_ipf_proxy_fail;
	u_long	ns_uncreate[2];
} nat_stat_side_t;


typedef	struct	natstat	{
	nat_t		*ns_instances;
	ipnat_t		*ns_list;
	hostmap_t	*ns_maplist;
	hostmap_t	**ns_maptable;
	u_int		ns_active;
	u_long		ns_addtrpnt;
	u_long		ns_divert_build;
	u_long		ns_expire;
	u_long		ns_flush_all;
	u_long		ns_flush_closing;
	u_long		ns_flush_queue;
	u_long		ns_flush_state;
	u_long		ns_flush_timeout;
	u_long		ns_hm_new;
	u_long		ns_hm_newfail;
	u_long		ns_hm_addref;
	u_long		ns_hm_nullnp;
	u_long		ns_log_ok;
	u_long		ns_log_fail;
	u_int		ns_hostmap_sz;
	u_int		ns_nattab_sz;
	u_int		ns_nattab_max;
	u_int		ns_orphans;
	u_int		ns_rules;
	u_int		ns_rules_map;
	u_int		ns_rules_rdr;
	u_int		ns_rultab_sz;
	u_int		ns_rdrtab_sz;
	u_32_t		ns_ticks;
	u_int		ns_trpntab_sz;
	u_int		ns_wilds;
	u_long		ns_proto[256];
	nat_stat_side_t	ns_side[2];
#ifdef USE_INET6
	nat_stat_side_t	ns_side6[2];
#endif
} natstat_t;

typedef	struct	natlog {
	i6addr_t	nl_osrcip;
	i6addr_t	nl_odstip;
	i6addr_t	nl_nsrcip;
	i6addr_t	nl_ndstip;
	u_short		nl_osrcport;
	u_short		nl_odstport;
	u_short		nl_nsrcport;
	u_short		nl_ndstport;
	int		nl_action;
	int		nl_type;
	int		nl_rule;
	U_QUAD_T	nl_pkts[2];
	U_QUAD_T	nl_bytes[2];
	u_char		nl_p[2];
	u_char		nl_v[2];
	u_char		nl_ifnames[2][LIFNAMSIZ];
} natlog_t;


#define	NL_NEW		0
#define	NL_CLONE	1
#define	NL_PURGE	0xfffc
#define	NL_DESTROY	0xfffd
#define	NL_FLUSH	0xfffe
#define	NL_EXPIRE	0xffff

#define	NAT_HASH_FN(_k,_l,_m)	(((_k) + ((_k) >> 12) + _l) % (_m))
#define	NAT_HASH_FN6(_k,_l,_m)	((((u_32_t *)(_k))[3] \
				 + (((u_32_t *)(_k))[3] >> 12) \
				 + (((u_32_t *)(_k))[2]) \
				 + (((u_32_t *)(_k))[2] >> 12) \
				 + (((u_32_t *)(_k))[1]) \
				 + (((u_32_t *)(_k))[1] >> 12) \
				 + (((u_32_t *)(_k))[0]) \
				 + (((u_32_t *)(_k))[0] >> 12) \
				 + _l) % (_m))

#define	LONG_SUM(_i)	(((_i) & 0xffff) + ((_i) >> 16))
#define	LONG_SUM6(_i)	(LONG_SUM(ntohl(((u_32_t *)(_i))[0])) + \
			 LONG_SUM(ntohl(((u_32_t *)(_i))[1])) + \
			 LONG_SUM(ntohl(((u_32_t *)(_i))[2])) + \
			 LONG_SUM(ntohl(((u_32_t *)(_i))[3])))

#define	CALC_SUMD(s1, s2, sd) { \
			    (s1) = ((s1) & 0xffff) + ((s1) >> 16); \
			    (s2) = ((s2) & 0xffff) + ((s2) >> 16); \
			    /* Do it twice */ \
			    (s1) = ((s1) & 0xffff) + ((s1) >> 16); \
			    (s2) = ((s2) & 0xffff) + ((s2) >> 16); \
			    /* Because ~1 == -2, We really need ~1 == -1 */ \
			    if ((s1) > (s2)) (s2)--; \
			    (sd) = (s2) - (s1); \
			    (sd) = ((sd) & 0xffff) + ((sd) >> 16); }

#define	NAT_SYSSPACE		0x80000000
#define	NAT_LOCKHELD		0x40000000

/*
 * This is present in ip_nat.h because it needs to be shared between
 * ip_nat.c and ip_nat6.c
 */
typedef struct ipf_nat_softc_s {
	ipfmutex_t	ipf_nat_new;
	ipfmutex_t	ipf_nat_io;
	int		ipf_nat_doflush;
	int		ipf_nat_logging;
	int		ipf_nat_lock;
	int		ipf_nat_inited;
	int		ipf_nat_table_wm_high;
	int		ipf_nat_table_wm_low;
	u_int		ipf_nat_table_max;
	u_int		ipf_nat_table_sz;
	u_int		ipf_nat_maprules_sz;
	u_int		ipf_nat_rdrrules_sz;
	u_int		ipf_nat_hostmap_sz;
	u_int		ipf_nat_maxbucket;
	u_int		ipf_nat_last_force_flush;
	u_int		ipf_nat_defage;
	u_int		ipf_nat_defipage;
	u_int		ipf_nat_deficmpage;
	ipf_v4_masktab_t	ipf_nat_map_mask;
	ipf_v6_masktab_t	ipf_nat6_map_mask;
	ipf_v4_masktab_t	ipf_nat_rdr_mask;
	ipf_v6_masktab_t	ipf_nat6_rdr_mask;
	nat_t		**ipf_nat_table[2];
	nat_t		*ipf_nat_instances;
	ipnat_t		*ipf_nat_list;
	ipnat_t		**ipf_nat_list_tail;
	ipnat_t		**ipf_nat_map_rules;
	ipnat_t		**ipf_nat_rdr_rules;
	ipftq_t		*ipf_nat_utqe;
	hostmap_t	**ipf_hm_maptable ;
	hostmap_t	*ipf_hm_maplist ;
	ipftuneable_t	*ipf_nat_tune;
	ipftq_t		ipf_nat_udptq;
	ipftq_t		ipf_nat_udpacktq;
	ipftq_t		ipf_nat_icmptq;
	ipftq_t		ipf_nat_icmpacktq;
	ipftq_t		ipf_nat_iptq;
	ipftq_t		ipf_nat_pending;
	ipftq_t		ipf_nat_tcptq[IPF_TCP_NSTATES];
	natstat_t	ipf_nat_stats;
} ipf_nat_softc_t ;

#define	ipf_nat_map_max			ipf_nat_map_mask.imt4_max
#define	ipf_nat_rdr_max			ipf_nat_rdr_mask.imt4_max
#define	ipf_nat6_map_max		ipf_nat6_map_mask.imt6_max
#define	ipf_nat6_rdr_max		ipf_nat6_rdr_mask.imt6_max
#define	ipf_nat_map_active_masks	ipf_nat_map_mask.imt4_active
#define	ipf_nat_rdr_active_masks	ipf_nat_rdr_mask.imt4_active
#define	ipf_nat6_map_active_masks	ipf_nat6_map_mask.imt6_active
#define	ipf_nat6_rdr_active_masks	ipf_nat6_rdr_mask.imt6_active

extern	frentry_t 	ipfnatblock;

extern	void	ipf_fix_datacksum __P((u_short *, u_32_t));
extern	void	ipf_fix_incksum __P((int, u_short *, u_32_t, u_32_t));
extern	void	ipf_fix_outcksum __P((int, u_short *, u_32_t, u_32_t));

extern	int	ipf_nat_checkin __P((fr_info_t *, u_32_t *));
extern	int	ipf_nat_checkout __P((fr_info_t *, u_32_t *));
extern	void	ipf_nat_delete __P((ipf_main_softc_t *, struct nat *, int));
extern	void	ipf_nat_deref __P((ipf_main_softc_t *, nat_t **));
extern	void	ipf_nat_expire __P((ipf_main_softc_t *));
extern	int	ipf_nat_hashtab_add __P((ipf_main_softc_t *,
					 ipf_nat_softc_t *, nat_t *));
extern	void	ipf_nat_hostmapdel __P((ipf_main_softc_t *, hostmap_t **));
extern	int	ipf_nat_hostmap_rehash __P((ipf_main_softc_t *,
					    ipftuneable_t *, ipftuneval_t *));
extern	nat_t	*ipf_nat_icmperrorlookup __P((fr_info_t *, int));
extern	nat_t	*ipf_nat_icmperror __P((fr_info_t *, u_int *, int));
extern	int	ipf_nat_init __P((void));
extern	nat_t	*ipf_nat_inlookup __P((fr_info_t *, u_int, u_int,
				      struct in_addr, struct in_addr));
extern	int	ipf_nat_in __P((fr_info_t *, nat_t *, int, u_32_t));
extern	int	ipf_nat_insert __P((ipf_main_softc_t *, ipf_nat_softc_t *,
				    nat_t *));
extern	int	ipf_nat_ioctl __P((ipf_main_softc_t *, caddr_t, ioctlcmd_t,
				   int, int, void *));
extern	void	ipf_nat_log __P((ipf_main_softc_t *, ipf_nat_softc_t *,
				 struct nat *, u_int));
extern	nat_t	*ipf_nat_lookupredir __P((natlookup_t *));
extern	nat_t	*ipf_nat_maplookup __P((void *, u_int, struct in_addr,
				struct in_addr));
extern	nat_t	*ipf_nat_add __P((fr_info_t *, ipnat_t *, nat_t **,
				 u_int, int));
extern	int	ipf_nat_out __P((fr_info_t *, nat_t *, int, u_32_t));
extern	nat_t	*ipf_nat_outlookup __P((fr_info_t *, u_int, u_int,
				       struct in_addr, struct in_addr));
extern	u_short	*ipf_nat_proto __P((fr_info_t *, nat_t *, u_int));
extern	void	ipf_nat_rule_deref __P((ipf_main_softc_t *, ipnat_t **));
extern	void	ipf_nat_setqueue __P((ipf_main_softc_t *, ipf_nat_softc_t *,
				      nat_t *));
extern	void	ipf_nat_setpending __P((ipf_main_softc_t *, nat_t *));
extern	nat_t	*ipf_nat_tnlookup __P((fr_info_t *, int));
extern	void	ipf_nat_update __P((fr_info_t *, nat_t *));
extern	int	ipf_nat_rehash __P((ipf_main_softc_t *, ipftuneable_t *,
				    ipftuneval_t *));
extern	int	ipf_nat_rehash_rules __P((ipf_main_softc_t *, ipftuneable_t *,
					  ipftuneval_t *));
extern	int	ipf_nat_settimeout __P((struct ipf_main_softc_s *,
					ipftuneable_t *, ipftuneval_t *));
extern	void	ipf_nat_sync __P((ipf_main_softc_t *, void *));

extern	nat_t	*ipf_nat_clone __P((fr_info_t *, nat_t *));
extern	void	ipf_nat_delmap __P((ipf_nat_softc_t *, ipnat_t *));
extern	void	ipf_nat_delrdr __P((ipf_nat_softc_t *, ipnat_t *));
extern	int	ipf_nat_wildok __P((nat_t *, int, int, int, int));
extern	void	ipf_nat_setlock __P((void *, int));
extern	void	ipf_nat_load __P((void));
extern	void	*ipf_nat_soft_create __P((ipf_main_softc_t *));
extern	int	ipf_nat_soft_init __P((ipf_main_softc_t *, void *));
extern	void	ipf_nat_soft_destroy __P((ipf_main_softc_t *, void *));
extern	int	ipf_nat_soft_fini __P((ipf_main_softc_t *, void *));
extern	int	ipf_nat_main_load __P((void));
extern	int	ipf_nat_main_unload __P((void));
extern	ipftq_t	*ipf_nat_add_tq __P((ipf_main_softc_t *, int));
extern	void	ipf_nat_uncreate __P((fr_info_t *));

#ifdef USE_INET6
extern	nat_t	*ipf_nat6_add __P((fr_info_t *, ipnat_t *, nat_t **,
				   u_int, int));
extern	void	ipf_nat6_addrdr __P((ipf_nat_softc_t *, ipnat_t *));
extern	void	ipf_nat6_addmap __P((ipf_nat_softc_t *, ipnat_t *));
extern	void	ipf_nat6_addencap __P((ipf_nat_softc_t *, ipnat_t *));
extern	int	ipf_nat6_checkout __P((fr_info_t *, u_32_t *));
extern	int	ipf_nat6_checkin __P((fr_info_t *, u_32_t *));
extern	void	ipf_nat6_delmap __P((ipf_nat_softc_t *, ipnat_t *));
extern	void	ipf_nat6_delrdr __P((ipf_nat_softc_t *, ipnat_t *));
extern	int	ipf_nat6_finalise __P((fr_info_t *, nat_t *));
extern	nat_t	*ipf_nat6_icmperror __P((fr_info_t *, u_int *, int));
extern	nat_t	*ipf_nat6_icmperrorlookup __P((fr_info_t *, int));
extern	nat_t	*ipf_nat6_inlookup __P((fr_info_t *, u_int, u_int,
					struct in6_addr *, struct in6_addr *));
extern	u_32_t	ipf_nat6_ip6subtract __P((i6addr_t *, i6addr_t *));
extern	frentry_t *ipf_nat6_ipfin __P((fr_info_t *, u_32_t *));
extern	frentry_t *ipf_nat6_ipfout __P((fr_info_t *, u_32_t *));
extern	nat_t	*ipf_nat6_lookupredir __P((natlookup_t *));
extern	int	ipf_nat6_newmap __P((fr_info_t *, nat_t *, natinfo_t *));
extern	int	ipf_nat6_newrdr __P((fr_info_t *, nat_t *, natinfo_t *));
extern	nat_t	*ipf_nat6_outlookup __P((fr_info_t *, u_int, u_int,
					 struct in6_addr *, struct in6_addr *));
extern	int	ipf_nat6_newrewrite __P((fr_info_t *, nat_t *, natinfo_t *));
extern	int	ipf_nat6_newdivert __P((fr_info_t *, nat_t *, natinfo_t *));
extern	int	ipf_nat6_ruleaddrinit __P((ipf_main_softc_t *, ipf_nat_softc_t *, ipnat_t *));

#endif


#endif /* __IP_NAT_H__ */
