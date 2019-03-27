/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $FreeBSD$
 * Id: ip_proxy.h,v 2.31.2.2 2005/03/12 19:33:48 darrenr Exp
 */

#ifndef	__IP_PROXY_H__
#define	__IP_PROXY_H__

#ifndef	SOLARIS
# if defined(sun) && (defined(__svr4__) || defined(__SVR4))
#  define	SOLARIS		1
# else
#  define	SOLARIS		0
# endif
#endif

#if defined(__STDC__) || defined(__GNUC__) || defined(_AIX51)
#define	SIOCPROXY	_IOWR('r', 64, struct ap_control)
#else
#define	SIOCPROXY	_IOWR(r, 64, struct ap_control)
#endif

#ifndef	APR_LABELLEN
#define	APR_LABELLEN	16
#endif
#define	AP_SESS_SIZE	53

struct	nat;
struct	ipnat;
struct	ipstate;

typedef	struct	ap_tcp {
	u_short	apt_sport;	/* source port */
	u_short	apt_dport;	/* destination port */
	short	apt_sel[2];	/* {seq,ack}{off,min} set selector */
	short	apt_seqoff[2];	/* sequence # difference */
	u_32_t	apt_seqmin[2];	/* don't change seq-off until after this */
	short	apt_ackoff[2];	/* sequence # difference */
	u_32_t	apt_ackmin[2];	/* don't change seq-off until after this */
	u_char	apt_state[2];	/* connection state */
} ap_tcp_t;

typedef	struct	ap_udp {
	u_short	apu_sport;	/* source port */
	u_short	apu_dport;	/* destination port */
} ap_udp_t;

typedef	struct ap_session {
	struct	aproxy	*aps_apr;
	union {
		struct	ap_tcp	apu_tcp;
		struct	ap_udp	apu_udp;
	} aps_un;
	U_QUAD_T aps_bytes;	/* bytes sent */
	U_QUAD_T aps_pkts;	/* packets sent */
	void	*aps_nat;	/* pointer back to nat struct */
	void	*aps_data;	/* private data */
	int	aps_psiz;	/* size of private data */
	struct	ap_session	*aps_next;
} ap_session_t;

#define	aps_sport	aps_un.apu_tcp.apt_sport
#define	aps_dport	aps_un.apu_tcp.apt_dport
#define	aps_sel		aps_un.apu_tcp.apt_sel
#define	aps_seqoff	aps_un.apu_tcp.apt_seqoff
#define	aps_seqmin	aps_un.apu_tcp.apt_seqmin
#define	aps_state	aps_un.apu_tcp.apt_state
#define	aps_ackoff	aps_un.apu_tcp.apt_ackoff
#define	aps_ackmin	aps_un.apu_tcp.apt_ackmin


typedef	struct	ap_control {
	char	apc_label[APR_LABELLEN];
	char	apc_config[APR_LABELLEN];
	u_char	apc_p;
	/*
	 * The following fields are upto the proxy's apr_ctl routine to deal
	 * with.  When the proxy gets this in kernel space, apc_data will
	 * point to a malloc'd region of memory of apc_dsize bytes.  If the
	 * proxy wants to keep that memory, it must set apc_data to NULL
	 * before it returns.  It is expected if this happens that it will
	 * take care to free it in apr_fini or otherwise as appropriate.
	 * apc_cmd is provided as a standard place to put simple commands,
	 * with apc_arg being available to put a simple arg.
	 */
	u_long	apc_cmd;
	u_long	apc_arg;
	void	*apc_data;
	size_t	apc_dsize;
} ap_ctl_t;

#define	APC_CMD_ADD	0
#define	APC_CMD_DEL	1


typedef	struct	aproxy	{
	struct	aproxy	*apr_next;
	struct	aproxy	*apr_parent;
	char	apr_label[APR_LABELLEN];	/* Proxy label # */
	u_char	apr_p;				/* protocol */
	int	apr_flags;
	int	apr_ref;
	int	apr_clones;
	void	(* apr_load) __P((void));
	void	(* apr_unload) __P((void));
	void	*(* apr_create) __P((ipf_main_softc_t *));
	void	(* apr_destroy) __P((ipf_main_softc_t *, void *));
	int	(* apr_init) __P((ipf_main_softc_t *, void *));
	void	(* apr_fini) __P((ipf_main_softc_t *, void *));
	int	(* apr_new) __P((void *, fr_info_t *, ap_session_t *,
				 struct nat *));
	void	(* apr_del) __P((ipf_main_softc_t *, ap_session_t *));
	int	(* apr_inpkt) __P((void *, fr_info_t *, ap_session_t *,
				   struct nat *));
	int	(* apr_outpkt) __P((void *, fr_info_t *, ap_session_t *,
				    struct nat *));
	int	(* apr_match) __P((fr_info_t *, ap_session_t *, struct nat *));
	int	(* apr_ctl) __P((ipf_main_softc_t *, void *, ap_ctl_t *));
	int	(* apr_clear) __P((struct aproxy *));
	int	(* apr_flush) __P((struct aproxy *, int));
	void	*apr_soft;
} aproxy_t;

#define	APR_DELETE	1

#define	APR_ERR(x)	((x) << 16)
#define	APR_EXIT(x)	(((x) >> 16) & 0xffff)
#define	APR_INC(x)	((x) & 0xffff)


#ifdef _KERNEL
/*
 * Generic #define's to cover missing things in the kernel
 */
# ifndef isdigit
#  define isdigit(x)	((x) >= '0' && (x) <= '9')
# endif
# ifndef isupper
#  define isupper(x)	(((unsigned)(x) >= 'A') && ((unsigned)(x) <= 'Z'))
# endif
# ifndef islower
#  define islower(x)	(((unsigned)(x) >= 'a') && ((unsigned)(x) <= 'z'))
# endif
# ifndef isalpha
#  define isalpha(x)	(isupper(x) || islower(x))
# endif
# ifndef toupper
#  define toupper(x)	(isupper(x) ? (x) : (x) - 'a' + 'A')
# endif
# ifndef isspace
#  define isspace(x)	(((x) == ' ') || ((x) == '\r') || ((x) == '\n') || \
			 ((x) == '\t') || ((x) == '\b'))
# endif
#endif /* _KERNEL */

/*
 * For the ftp proxy.
 */
#define	FTP_BUFSZ	160
#define	IPF_FTPBUFSZ	160

typedef struct  ftpside {
	char	*ftps_rptr;
	char	*ftps_wptr;
	void	*ftps_ifp;
	u_32_t	ftps_seq[2];
	u_32_t	ftps_len;
	int	ftps_junk;
	int	ftps_cmds;
	int	ftps_cmd;
	char	ftps_buf[FTP_BUFSZ];
} ftpside_t;

typedef struct  ftpinfo {
	int 	  	ftp_passok;
	int		ftp_incok;
	void		*ftp_pendstate;
	nat_t		*ftp_pendnat;
	ftpside_t	ftp_side[2];
} ftpinfo_t;


/*
 * IPsec proxy
 */
typedef u_32_t		ipsec_cookie_t[2];

typedef struct ipsec_pxy {
	ipsec_cookie_t	ipsc_icookie;
	ipsec_cookie_t	ipsc_rcookie;
	int		ipsc_rckset;
	nat_t		*ipsc_nat;
	struct ipstate	*ipsc_state;
	ipnat_t		*ipsc_rule;
} ipsec_pxy_t;


/*
 * For the irc proxy.
 */
typedef	struct	ircinfo {
	size_t	irc_len;
	char	*irc_snick;
	char	*irc_dnick;
	char	*irc_type;
	char	*irc_arg;
	char	*irc_addr;
	u_32_t	irc_ipnum;
	u_short	irc_port;
} ircinfo_t;


/*
 * For the DNS "proxy"
 */
typedef struct dnsinfo {
        ipfmutex_t	dnsi_lock;
	u_short		dnsi_id;
	char		dnsi_buffer[512];
} dnsinfo_t;


/*
 * Real audio proxy structure and #defines
 */
typedef	struct	raudio_s {
	int	rap_seenpna;
	int	rap_seenver;
	int	rap_version;
	int	rap_eos;	/* End Of Startup */
	int	rap_gotid;
	int	rap_gotlen;
	int	rap_mode;
	int	rap_sdone;
	u_short	rap_plport;
	u_short	rap_prport;
	u_short	rap_srport;
	char	rap_svr[19];
	u_32_t	rap_sbf;	/* flag to indicate which of the 19 bytes have
				 * been filled
				 */
	u_32_t	rap_sseq;
} raudio_t;

#define	RA_ID_END	0
#define	RA_ID_UDP	1
#define	RA_ID_ROBUST	7

#define	RAP_M_UDP	1
#define	RAP_M_ROBUST	2
#define	RAP_M_TCP	4
#define	RAP_M_UDP_ROBUST	(RAP_M_UDP|RAP_M_ROBUST)


/*
 * MSN RPC proxy
 */
typedef	struct	msnrpcinfo	{
	u_int		mri_flags;
	int		mri_cmd[2];
	u_int		mri_valid;
	struct	in_addr	mri_raddr;
	u_short		mri_rport;
} msnrpcinfo_t;


/*
 * Sun RPCBIND proxy
 */
#define RPCB_MAXMSG	888
#define RPCB_RES_PMAP	0	/* Response contains a v2 port. */
#define RPCB_RES_STRING	1	/* " " " v3 (GETADDR) string. */
#define RPCB_RES_LIST	2	/* " " " v4 (GETADDRLIST) list. */
#define RPCB_MAXREQS	32	/* Arbitrary limit on tracked transactions */

#define RPCB_REQMIN	40
#define RPCB_REQMAX	888
#define RPCB_REPMIN	20
#define	RPCB_REPMAX	604	/* XXX double check this! */

/*
 * These macros determine the number of bytes between p and the end of
 * r->rs_buf relative to l.
 */
#define RPCB_BUF_END(r) (char *)((r)->rm_msgbuf + (r)->rm_buflen)
#define RPCB_BUF_GEQ(r, p, l)   \
        ((RPCB_BUF_END((r)) > (char *)(p)) &&           \
         ((RPCB_BUF_END((r)) - (char *)(p)) >= (l)))
#define	RPCB_BUF_EQ(r, p, l)                            \
        (RPCB_BUF_END((r)) == ((char *)(p) + (l)))

/*
 * The following correspond to RPC(B) detailed in RFC183[13].
 */
#define RPCB_CALL		0
#define RPCB_REPLY		1
#define RPCB_MSG_VERSION	2
#define RPCB_PROG		100000
#define RPCB_GETPORT		3
#define RPCB_GETADDR		3
#define RPCB_GETADDRLIST	11
#define RPCB_MSG_ACCEPTED	0
#define RPCB_MSG_DENIED		1

/* BEGIN (Generic XDR structures) */
typedef struct xdr_string {
	u_32_t	*xs_len;
	char	*xs_str;
} xdr_string_t;

typedef struct xdr_auth {
	/* u_32_t	xa_flavor; */
	xdr_string_t	xa_string;
} xdr_auth_t;

typedef struct xdr_uaddr {
	u_32_t		xu_ip;
	u_short         xu_port;
	xdr_string_t	xu_str;
} xdr_uaddr_t;

typedef	struct xdr_proto {
	u_int		xp_proto;
	xdr_string_t	xp_str;
} xdr_proto_t;

#define xu_xslen	xu_str.xs_len
#define xu_xsstr	xu_str.xs_str
#define	xp_xslen	xp_str.xs_len
#define xp_xsstr	xp_str.xs_str
/* END (Generic XDR structures) */

/* BEGIN (RPC call structures) */
typedef struct pmap_args {
	/* u_32_t	pa_prog; */
	/* u_32_t	pa_vers; */
	u_32_t		*pa_prot;
	/* u_32_t	pa_port; */
} pmap_args_t;

typedef struct rpcb_args {
	/* u_32_t	*ra_prog; */
	/* u_32_t	*ra_vers; */
	xdr_proto_t	ra_netid;
	xdr_uaddr_t	ra_maddr;
	/* xdr_string_t	ra_owner; */
} rpcb_args_t;

typedef struct rpc_call {
	/* u_32_t	rc_rpcvers; */
	/* u_32_t	rc_prog; */
	u_32_t	*rc_vers;
	u_32_t	*rc_proc;
	xdr_auth_t	rc_authcred;
	xdr_auth_t	rc_authverf;
	union {
		pmap_args_t	ra_pmapargs;
		rpcb_args_t	ra_rpcbargs;
	} rpcb_args;
} rpc_call_t;

#define	rc_pmapargs	rpcb_args.ra_pmapargs
#define rc_rpcbargs	rpcb_args.ra_rpcbargs
/* END (RPC call structures) */

/* BEGIN (RPC reply structures) */
typedef struct rpcb_entry {
	xdr_uaddr_t	re_maddr;
	xdr_proto_t	re_netid;
	/* u_32_t	re_semantics; */
	xdr_string_t	re_family;
	xdr_proto_t	re_proto;
	u_32_t		*re_more; /* 1 == another entry follows */
} rpcb_entry_t;

typedef struct rpcb_listp {
	u_32_t		*rl_list; /* 1 == list follows */
	int		rl_cnt;
	rpcb_entry_t	rl_entries[2]; /* TCP / UDP only */
} rpcb_listp_t;

typedef struct rpc_resp {
	/* u_32_t	rr_acceptdeny; */
	/* Omitted 'message denied' fork; we don't care about rejects. */
	xdr_auth_t	rr_authverf;
	/* u_32_t		*rr_astat;	*/
	union {
		u_32_t		*resp_pmap;
		xdr_uaddr_t	resp_getaddr;
		rpcb_listp_t	resp_getaddrlist;
	} rpcb_reply;
} rpc_resp_t;

#define	rr_v2	rpcb_reply.resp_pmap
#define rr_v3	rpcb_reply.resp_getaddr
#define	rr_v4	rpcb_reply.resp_getaddrlist
/* END (RPC reply structures) */

/* BEGIN (RPC message structure & macros) */
typedef struct rpc_msg {
	char	rm_msgbuf[RPCB_MAXMSG];	/* RPCB data buffer */
	u_int	rm_buflen;
	u_32_t	*rm_xid;
	/* u_32_t Call vs Reply */
	union {
		rpc_call_t	rb_call;
		rpc_resp_t	rb_resp;
	} rm_body;
} rpc_msg_t;

#define rm_call		rm_body.rb_call
#define rm_resp		rm_body.rb_resp
/* END (RPC message structure & macros) */

/*
 * These code paths aren't hot enough to warrant per transaction
 * mutexes.
 */
typedef struct rpcb_xact {
	struct	rpcb_xact	*rx_next;
	struct	rpcb_xact	**rx_pnext;
	u_32_t	rx_xid;		/* RPC transmission ID */
	u_int	rx_type;	/* RPCB response type */
	u_int	rx_ref;         /* reference count */
	u_int	rx_proto;	/* transport protocol (v2 only) */
} rpcb_xact_t;

typedef struct rpcb_session {
        ipfmutex_t	rs_rxlock;
	rpcb_xact_t	*rs_rxlist;
} rpcb_session_t;

/*
 * For an explanation, please see the following:
 *   RFC1832 - Sections 3.11, 4.4, and 4.5.
 */
#define XDRALIGN(x)	((((x) % 4) != 0) ? ((((x) + 3) / 4) * 4) : (x))

extern	int	ipf_proxy_add __P((void *, aproxy_t *));
extern	int	ipf_proxy_check __P((fr_info_t *, struct nat *));
extern	int	ipf_proxy_ctl __P((ipf_main_softc_t *, void *, ap_ctl_t *));
extern	int	ipf_proxy_del __P((aproxy_t *));
extern	void	ipf_proxy_deref __P((aproxy_t *));
extern	void	ipf_proxy_flush __P((void *, int));
extern	int	ipf_proxy_init __P((void));
extern	int	ipf_proxy_ioctl __P((ipf_main_softc_t *, caddr_t, ioctlcmd_t, int, void *));
extern	aproxy_t	*ipf_proxy_lookup __P((void *, u_int, char *));
extern	int	ipf_proxy_match __P((fr_info_t *, struct nat *));
extern	int	ipf_proxy_new __P((fr_info_t *, struct nat *));
extern	int	ipf_proxy_ok __P((fr_info_t *, tcphdr_t *, struct ipnat *));
extern	void	ipf_proxy_free __P((ipf_main_softc_t *, ap_session_t *));
extern	int	ipf_proxy_main_load __P((void));
extern	int	ipf_proxy_main_unload __P((void));
extern	ipnat_t	*ipf_proxy_rule_fwd __P((nat_t *));
extern	ipnat_t	*ipf_proxy_rule_rev __P((nat_t *));
extern	void	*ipf_proxy_soft_create __P((ipf_main_softc_t *));
extern	void	ipf_proxy_soft_destroy __P((ipf_main_softc_t *, void *));
extern	int	ipf_proxy_soft_init __P((ipf_main_softc_t *, void *));
extern	int	ipf_proxy_soft_fini __P((ipf_main_softc_t *, void *));

#endif /* __IP_PROXY_H__ */
