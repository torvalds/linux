/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * @(#)ip_state.h	1.3 1/12/96 (C) 1995 Darren Reed
 * $FreeBSD$
 * Id: ip_state.h,v 2.68.2.10 2007/10/16 09:33:24 darrenr Exp $
 */
#ifndef	__IP_STATE_H__
#define	__IP_STATE_H__

#if defined(__STDC__) || defined(__GNUC__) || defined(_AIX51)
# define	SIOCDELST	_IOW('r', 61, struct ipfobj)
#else
# define	SIOCDELST	_IOW(r, 61, struct ipfobj)
#endif

struct ipscan;

#ifndef	IPSTATE_SIZE
# define	IPSTATE_SIZE	5737
#endif
#ifndef	IPSTATE_MAX
# define	IPSTATE_MAX	4013	/* Maximum number of states held */
#endif

#define	PAIRS(s1,d1,s2,d2)	((((s1) == (s2)) && ((d1) == (d2))) ||\
				 (((s1) == (d2)) && ((d1) == (s2))))
#define	IPPAIR(s1,d1,s2,d2)	PAIRS((s1).s_addr, (d1).s_addr, \
				      (s2).s_addr, (d2).s_addr)


typedef struct ipstate {
	ipfmutex_t	is_lock;
	struct	ipstate	*is_next;
	struct	ipstate	**is_pnext;
	struct	ipstate	*is_hnext;
	struct	ipstate	**is_phnext;
	struct	ipstate	**is_me;
	void		*is_ifp[4];
	void		*is_sync;
	frentry_t	*is_rule;
	struct	ipftq	*is_tqehead[2];
	struct	ipscan	*is_isc;
	U_QUAD_T	is_pkts[4];
	U_QUAD_T	is_bytes[4];
	U_QUAD_T	is_icmppkts[4];
	struct	ipftqent is_sti;
	u_int	is_frage[2];
	int	is_ref;			/* reference count */
	int	is_isninc[2];
	u_short	is_sumd[2];
	i6addr_t	is_src;
	i6addr_t	is_dst;
	u_int	is_pass;
	u_char	is_p;			/* Protocol */
	u_char	is_v;
	int	is_family;
	u_32_t	is_hv;
	u_32_t	is_tag;
	u_32_t	is_opt[2];		/* packet options set */
	u_32_t	is_optmsk[2];		/*    "      "    mask */
	u_short	is_sec;			/* security options set */
	u_short	is_secmsk;		/*    "        "    mask */
	u_short	is_auth;		/* authentication options set */
	u_short	is_authmsk;		/*    "              "    mask */
	union {
		icmpinfo_t	is_ics;
		tcpinfo_t	is_ts;
		udpinfo_t	is_us;
		greinfo_t	is_ug;
	} is_ps;
	u_32_t	is_flags;
	int	is_flx[2][2];
	u_32_t	is_rulen;		/* rule number when created */
	u_32_t	is_s0[2];
	u_short	is_smsk[2];
	frdest_t	is_dif;
	frdest_t	is_tifs[2];
	char	is_group[FR_GROUPLEN];
	char	is_sbuf[2][16];
	char	is_ifname[4][LIFNAMSIZ];
} ipstate_t;

#define	is_die		is_sti.tqe_die
#define	is_state	is_sti.tqe_state
#define	is_touched	is_sti.tqe_touched
#define	is_saddr	is_src.in4.s_addr
#define	is_daddr	is_dst.in4.s_addr
#define	is_icmp		is_ps.is_ics
#define	is_type		is_icmp.ici_type
#define	is_tcp		is_ps.is_ts
#define	is_udp		is_ps.is_us
#define is_send		is_tcp.ts_data[0].td_end
#define is_dend		is_tcp.ts_data[1].td_end
#define is_maxswin	is_tcp.ts_data[0].td_maxwin
#define is_maxdwin	is_tcp.ts_data[1].td_maxwin
#define is_maxsend	is_tcp.ts_data[0].td_maxend
#define is_maxdend	is_tcp.ts_data[1].td_maxend
#define	is_swinscale	is_tcp.ts_data[0].td_winscale
#define	is_dwinscale	is_tcp.ts_data[1].td_winscale
#define	is_swinflags	is_tcp.ts_data[0].td_winflags
#define	is_dwinflags	is_tcp.ts_data[1].td_winflags
#define	is_sport	is_tcp.ts_sport
#define	is_dport	is_tcp.ts_dport
#define	is_ifpin	is_ifp[0]
#define	is_ifpout	is_ifp[2]
#define	is_gre		is_ps.is_ug
#define	is_call		is_gre.gs_call

#define	IS_WSPORT	SI_W_SPORT	/* 0x00100 */
#define	IS_WDPORT	SI_W_DPORT	/* 0x00200 */
#define	IS_WSADDR	SI_W_SADDR	/* 0x00400 */
#define	IS_WDADDR	SI_W_DADDR	/* 0x00800 */
#define	IS_NEWFR	SI_NEWFR	/* 0x01000 */
#define	IS_CLONE	SI_CLONE	/* 0x02000 */
#define	IS_CLONED	SI_CLONED	/* 0x04000 */
#define	IS_TCPFSM			   0x10000
#define	IS_STRICT			   0x20000
#define	IS_ISNSYN			   0x40000
#define	IS_ISNACK			   0x80000
#define	IS_STATESYNC			   0x100000
#define	IS_LOOSE			   0x200000
/*
 * IS_SC flags are for scan-operations that need to be recognised in state.
 */
#define	IS_SC_CLIENT			0x10000000
#define	IS_SC_SERVER			0x20000000
#define	IS_SC_MATCHC			0x40000000
#define	IS_SC_MATCHS			0x80000000
#define	IS_SC_MATCHALL	(IS_SC_MATCHC|IS_SC_MATCHC)
#define	IS_SC_ALL	(IS_SC_MATCHC|IS_SC_MATCHC|IS_SC_CLIENT|IS_SC_SERVER)

/*
 * Flags that can be passed into ipf_addstate
 */
#define	IS_INHERITED			0x0fffff00

#define	TH_OPENING	(TH_SYN|TH_ACK)
/*
 * is_flags:
 * Bits 0 - 3 are use as a mask with the current packet's bits to check for
 * whether it is short, tcp/udp, a fragment or the presence of IP options.
 * Bits 4 - 7 are set from the initial packet and contain what the packet
 * anded with bits 0-3 must match.
 * Bits 8,9 are used to indicate wildcard source/destination port matching.
 * Bits 10,11 are reserved for other wildcard flag compatibility.
 * Bits 12,13 are for scaning.
 */

typedef	struct	ipstate_save	{
	void	*ips_next;
	struct	ipstate	ips_is;
	struct	frentry	ips_fr;
} ipstate_save_t;

#define	ips_rule	ips_is.is_rule


typedef	struct	ipslog	{
	U_QUAD_T	isl_pkts[4];
	U_QUAD_T	isl_bytes[4];
	i6addr_t	isl_src;
	i6addr_t	isl_dst;
	u_32_t	isl_tag;
	u_short	isl_type;
	union {
		u_short	isl_filler[2];
		u_short	isl_ports[2];
		u_short	isl_icmp;
	} isl_ps;
	u_char	isl_v;
	u_char	isl_p;
	u_char	isl_flags;
	u_char	isl_state[2];
	u_32_t	isl_rulen;
	char	isl_group[FR_GROUPLEN];
} ipslog_t;

#define	isl_sport	isl_ps.isl_ports[0]
#define	isl_dport	isl_ps.isl_ports[1]
#define	isl_itype	isl_ps.isl_icmp

#define	ISL_NEW			0
#define	ISL_CLONE		1
#define	ISL_STATECHANGE		2
#define	ISL_EXPIRE		0xffff
#define	ISL_FLUSH		0xfffe
#define	ISL_REMOVE		0xfffd
#define	ISL_INTERMEDIATE	0xfffc
#define	ISL_KILLED		0xfffb
#define	ISL_ORPHAN		0xfffa
#define	ISL_UNLOAD		0xfff9


typedef	struct	ips_stat {
	u_int	iss_active;
	u_int	iss_active_proto[256];
	u_long	iss_add_bad;
	u_long	iss_add_dup;
	u_long	iss_add_locked;
	u_long	iss_add_oow;
	u_long	iss_bucket_full;
	u_long	iss_check_bad;
	u_long	iss_check_miss;
	u_long	iss_check_nattag;
	u_long	iss_check_notag;
	u_long	iss_clone_nomem;
	u_long	iss_cloned;
	u_long	iss_expire;
	u_long	iss_fin;
	u_long	iss_flush_all;
	u_long	iss_flush_closing;
	u_long	iss_flush_queue;
	u_long	iss_flush_state;
	u_long	iss_flush_timeout;
	u_long	iss_hits;
	u_long	iss_icmp6_icmperr;
	u_long	iss_icmp6_miss;
	u_long	iss_icmp6_notinfo;
	u_long	iss_icmp6_notquery;
	u_long	iss_icmp_bad;
	u_long	iss_icmp_banned;
	u_long	iss_icmp_headblock;
	u_long	iss_icmp_hits;
	u_long	iss_icmp_icmperr;
	u_long	iss_icmp_miss;
	u_long	iss_icmp_notquery;
	u_long	iss_icmp_short;
	u_long	iss_icmp_toomany;
	u_int	iss_inuse;
	ipstate_t *iss_list;
	u_long	iss_log_fail;
	u_long	iss_log_ok;
	u_long	iss_lookup_badifp;
	u_long	iss_lookup_badport;
	u_long	iss_lookup_miss;
	u_long	iss_max;
	u_long	iss_max_ref;
	u_long	iss_max_track;
	u_long	iss_miss_mask;
	u_long	iss_nomem;
	u_long	iss_oow;
	u_long	iss_orphan;
	u_long	iss_proto[256];
	u_long	iss_scan_block;
	u_long	iss_state_max;
	u_long	iss_state_size;
	u_long	iss_states[IPF_TCP_NSTATES];
	ipstate_t **iss_table;
	u_long	iss_tcp_closing;
	u_long	iss_tcp_oow;
	u_long	iss_tcp_rstadd;
	u_long	iss_tcp_toosmall;
	u_long	iss_tcp_badopt;
	u_long	iss_tcp_fsm;
	u_long	iss_tcp_strict;
	ipftq_t	*iss_tcptab;
	u_int	iss_ticks;
	u_long	iss_wild;
	u_long	iss_winsack;
	u_int	*iss_bucketlen;
} ips_stat_t;


typedef struct ipf_state_softc_s {
	ipfmutex_t	ipf_stinsert;
	int		ipf_state_logging;
	int		ipf_state_lock;
	int		ipf_state_doflush;
	u_int		ipf_state_inited;
	u_int		ipf_state_max;
	u_int		ipf_state_maxbucket;
	u_int		ipf_state_size;
	u_int		ipf_state_wm_freq;
	u_int		ipf_state_wm_high;
	u_int		ipf_state_wm_low;
	u_int		ipf_state_wm_last;
	u_long		*ipf_state_seed;
	ipstate_t	*ipf_state_list;
	ipstate_t	**ipf_state_table;
	ipftuneable_t	*ipf_state_tune;
	ipftq_t		*ipf_state_usertq;
	ipftq_t		ipf_state_pending;
	ipftq_t		ipf_state_deletetq;
	ipftq_t		ipf_state_udptq;
	ipftq_t		ipf_state_udpacktq;
	ipftq_t		ipf_state_iptq;
	ipftq_t		ipf_state_icmptq;
	ipftq_t		ipf_state_icmpacktq;
	ipftq_t		ipf_state_tcptq[IPF_TCP_NSTATES];
	ips_stat_t	ipf_state_stats;
} ipf_state_softc_t;


#ifndef _KERNEL
extern	void	ipf_state_dump __P((ipf_main_softc_t *, void *));
#endif
extern	int	ipf_tcp_age __P((struct ipftqent *, struct fr_info *,
				struct ipftq *, int, int));
extern	int	ipf_tcpinwindow __P((struct fr_info *, struct tcpdata *,
				    struct tcpdata *, tcphdr_t *, int));

extern	int	ipf_state_add __P((ipf_main_softc_t *, fr_info_t *,
				   ipstate_t **, u_int));
extern	frentry_t *ipf_state_check __P((struct fr_info *, u_32_t *));
extern	void	ipf_state_deref __P((ipf_main_softc_t *, ipstate_t **));
extern	void	ipf_state_expire __P((ipf_main_softc_t *));
extern	int	ipf_state_flush __P((ipf_main_softc_t *, int, int));
extern	ipstate_t *ipf_state_lookup __P((fr_info_t *, tcphdr_t *, ipftq_t **));
extern	int	ipf_state_init __P((void));
extern	int	ipf_state_insert __P((ipf_main_softc_t *, struct ipstate *, int));
extern	int	ipf_state_ioctl __P((ipf_main_softc_t *, caddr_t, ioctlcmd_t, int, int, void *));
extern	void	ipf_state_log __P((ipf_main_softc_t *, struct ipstate *, u_int));
extern	int	ipf_state_matchflush __P((ipf_main_softc_t *, caddr_t));
extern	int	ipf_state_rehash __P((ipf_main_softc_t *, ipftuneable_t *, ipftuneval_t *));
extern	void	ipf_state_setqueue __P((ipf_main_softc_t *, ipstate_t *, int));
extern	void	ipf_state_setpending __P((ipf_main_softc_t *, ipstate_t *));
extern	int	ipf_state_settimeout __P((struct ipf_main_softc_s *, ipftuneable_t *, ipftuneval_t *));
extern	void	ipf_state_sync __P((ipf_main_softc_t *, void *));
extern	void	ipf_state_update __P((fr_info_t *, ipstate_t *));

extern	void	ipf_sttab_init __P((ipf_main_softc_t *, struct ipftq *));
extern	void	ipf_sttab_destroy __P((struct ipftq *));
extern	void	ipf_state_setlock __P((void *, int));
extern	int	ipf_state_main_load __P((void));
extern	int	ipf_state_main_unload __P((void));
extern	void	*ipf_state_soft_create __P((ipf_main_softc_t *));
extern	void	ipf_state_soft_destroy __P((ipf_main_softc_t *, void *));
extern	int	ipf_state_soft_init __P((ipf_main_softc_t *, void *));
extern	int	ipf_state_soft_fini __P((ipf_main_softc_t *, void *));
extern	ipftq_t	*ipf_state_add_tq __P((ipf_main_softc_t *, int));

#endif /* __IP_STATE_H__ */
