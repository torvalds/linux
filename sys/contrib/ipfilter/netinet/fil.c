/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Copyright 2008 Sun Microsystems.
 *
 * $Id$
 *
 */
#if defined(KERNEL) || defined(_KERNEL)
# undef KERNEL
# undef _KERNEL
# define        KERNEL	1
# define        _KERNEL	1
#endif
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#if defined(_KERNEL) && defined(__FreeBSD_version)
#  if !defined(IPFILTER_LKM)
#   include "opt_inet6.h"
#  endif
# include <sys/filio.h>
#else
# include <sys/ioctl.h>
#endif
#if defined(__SVR4) || defined(sun) /* SOLARIS */
# include <sys/filio.h>
#endif
# include <sys/fcntl.h>
#if defined(_KERNEL)
# include <sys/systm.h>
# include <sys/file.h>
#else
# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# include <stddef.h>
# include <sys/file.h>
# define _KERNEL
# include <sys/uio.h>
# undef _KERNEL
#endif
#if !defined(__SVR4)
# include <sys/mbuf.h>
#else
#  include <sys/byteorder.h>
# if (SOLARIS2 < 5) && defined(sun)
#  include <sys/dditypes.h>
# endif
#endif
# include <sys/protosw.h>
#include <sys/socket.h>
#include <net/if.h>
#ifdef sun
# include <net/af.h>
#endif
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
# include <netinet/udp.h>
# include <netinet/ip_icmp.h>
#include "netinet/ip_compat.h"
#ifdef	USE_INET6
# include <netinet/icmp6.h>
# if !SOLARIS && defined(_KERNEL)
#  include <netinet6/in6_var.h>
# endif
#endif
#include "netinet/ip_fil.h"
#include "netinet/ip_nat.h"
#include "netinet/ip_frag.h"
#include "netinet/ip_state.h"
#include "netinet/ip_proxy.h"
#include "netinet/ip_auth.h"
#ifdef IPFILTER_SCAN
# include "netinet/ip_scan.h"
#endif
#include "netinet/ip_sync.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_pool.h"
#include "netinet/ip_htable.h"
#ifdef IPFILTER_COMPILED
# include "netinet/ip_rules.h"
#endif
#if defined(IPFILTER_BPF) && defined(_KERNEL)
# include <net/bpf.h>
#endif
#if defined(__FreeBSD_version)
# include <sys/malloc.h>
#endif
#include "netinet/ipl.h"

#if defined(__NetBSD__) && (__NetBSD_Version__ >= 104230000)
# include <sys/callout.h>
extern struct callout ipf_slowtimer_ch;
#endif
/* END OF INCLUDES */

#if !defined(lint)
static const char sccsid[] = "@(#)fil.c	1.36 6/5/96 (C) 1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$FreeBSD$";
/* static const char rcsid[] = "@(#)$Id: fil.c,v 2.243.2.125 2007/10/10 09:27:20 darrenr Exp $"; */
#endif

#ifndef	_KERNEL
# include "ipf.h"
# include "ipt.h"
extern	int	opts;
extern	int	blockreason;
#endif /* _KERNEL */

#define	LBUMP(x)	softc->x++
#define	LBUMPD(x, y)	do { softc->x.y++; DT(y); } while (0)

static	INLINE int	ipf_check_ipf __P((fr_info_t *, frentry_t *, int));
static	u_32_t		ipf_checkcipso __P((fr_info_t *, u_char *, int));
static	u_32_t		ipf_checkripso __P((u_char *));
static	u_32_t		ipf_decaps __P((fr_info_t *, u_32_t, int));
#ifdef IPFILTER_LOG
static	frentry_t	*ipf_dolog __P((fr_info_t *, u_32_t *));
#endif
static	int		ipf_flushlist __P((ipf_main_softc_t *, int *,
					   frentry_t **));
static	int		ipf_flush_groups __P((ipf_main_softc_t *, frgroup_t **,
					      int));
static	ipfunc_t	ipf_findfunc __P((ipfunc_t));
static	void		*ipf_findlookup __P((ipf_main_softc_t *, int,
					     frentry_t *,
					     i6addr_t *, i6addr_t *));
static	frentry_t	*ipf_firewall __P((fr_info_t *, u_32_t *));
static	int		ipf_fr_matcharray __P((fr_info_t *, int *));
static	int		ipf_frruleiter __P((ipf_main_softc_t *, void *, int,
					    void *));
static	void		ipf_funcfini __P((ipf_main_softc_t *, frentry_t *));
static	int		ipf_funcinit __P((ipf_main_softc_t *, frentry_t *));
static	int		ipf_geniter __P((ipf_main_softc_t *, ipftoken_t *,
					 ipfgeniter_t *));
static	void		ipf_getstat __P((ipf_main_softc_t *,
					 struct friostat *, int));
static	int		ipf_group_flush __P((ipf_main_softc_t *, frgroup_t *));
static	void		ipf_group_free __P((frgroup_t *));
static	int		ipf_grpmapfini __P((struct ipf_main_softc_s *,
					    frentry_t *));
static	int		ipf_grpmapinit __P((struct ipf_main_softc_s *,
					    frentry_t *));
static	frentry_t	*ipf_nextrule __P((ipf_main_softc_t *, int, int,
					   frentry_t *, int));
static	int		ipf_portcheck __P((frpcmp_t *, u_32_t));
static	INLINE int	ipf_pr_ah __P((fr_info_t *));
static	INLINE void	ipf_pr_esp __P((fr_info_t *));
static	INLINE void	ipf_pr_gre __P((fr_info_t *));
static	INLINE void	ipf_pr_udp __P((fr_info_t *));
static	INLINE void	ipf_pr_tcp __P((fr_info_t *));
static	INLINE void	ipf_pr_icmp __P((fr_info_t *));
static	INLINE void	ipf_pr_ipv4hdr __P((fr_info_t *));
static	INLINE void	ipf_pr_short __P((fr_info_t *, int));
static	INLINE int	ipf_pr_tcpcommon __P((fr_info_t *));
static	INLINE int	ipf_pr_udpcommon __P((fr_info_t *));
static	void		ipf_rule_delete __P((ipf_main_softc_t *, frentry_t *f,
					     int, int));
static	void		ipf_rule_expire_insert __P((ipf_main_softc_t *,
						    frentry_t *, int));
static	int		ipf_synclist __P((ipf_main_softc_t *, frentry_t *,
					  void *));
static	void		ipf_token_flush __P((ipf_main_softc_t *));
static	void		ipf_token_unlink __P((ipf_main_softc_t *,
					      ipftoken_t *));
static	ipftuneable_t	*ipf_tune_findbyname __P((ipftuneable_t *,
						  const char *));
static	ipftuneable_t	*ipf_tune_findbycookie __P((ipftuneable_t **, void *,
						    void **));
static	int		ipf_updateipid __P((fr_info_t *));
static	int		ipf_settimeout __P((struct ipf_main_softc_s *,
					    struct ipftuneable *,
					    ipftuneval_t *));
#if !defined(_KERNEL) || SOLARIS
static	int		ppsratecheck(struct timeval *, int *, int);
#endif


/*
 * bit values for identifying presence of individual IP options
 * All of these tables should be ordered by increasing key value on the left
 * hand side to allow for binary searching of the array and include a trailer
 * with a 0 for the bitmask for linear searches to easily find the end with.
 */
static const	struct	optlist	ipopts[20] = {
	{ IPOPT_NOP,	0x000001 },
	{ IPOPT_RR,	0x000002 },
	{ IPOPT_ZSU,	0x000004 },
	{ IPOPT_MTUP,	0x000008 },
	{ IPOPT_MTUR,	0x000010 },
	{ IPOPT_ENCODE,	0x000020 },
	{ IPOPT_TS,	0x000040 },
	{ IPOPT_TR,	0x000080 },
	{ IPOPT_SECURITY, 0x000100 },
	{ IPOPT_LSRR,	0x000200 },
	{ IPOPT_E_SEC,	0x000400 },
	{ IPOPT_CIPSO,	0x000800 },
	{ IPOPT_SATID,	0x001000 },
	{ IPOPT_SSRR,	0x002000 },
	{ IPOPT_ADDEXT,	0x004000 },
	{ IPOPT_VISA,	0x008000 },
	{ IPOPT_IMITD,	0x010000 },
	{ IPOPT_EIP,	0x020000 },
	{ IPOPT_FINN,	0x040000 },
	{ 0,		0x000000 }
};

#ifdef USE_INET6
static const struct optlist ip6exthdr[] = {
	{ IPPROTO_HOPOPTS,		0x000001 },
	{ IPPROTO_IPV6,			0x000002 },
	{ IPPROTO_ROUTING,		0x000004 },
	{ IPPROTO_FRAGMENT,		0x000008 },
	{ IPPROTO_ESP,			0x000010 },
	{ IPPROTO_AH,			0x000020 },
	{ IPPROTO_NONE,			0x000040 },
	{ IPPROTO_DSTOPTS,		0x000080 },
	{ IPPROTO_MOBILITY,		0x000100 },
	{ 0,				0 }
};
#endif

/*
 * bit values for identifying presence of individual IP security options
 */
static const	struct	optlist	secopt[8] = {
	{ IPSO_CLASS_RES4,	0x01 },
	{ IPSO_CLASS_TOPS,	0x02 },
	{ IPSO_CLASS_SECR,	0x04 },
	{ IPSO_CLASS_RES3,	0x08 },
	{ IPSO_CLASS_CONF,	0x10 },
	{ IPSO_CLASS_UNCL,	0x20 },
	{ IPSO_CLASS_RES2,	0x40 },
	{ IPSO_CLASS_RES1,	0x80 }
};

char	ipfilter_version[] = IPL_VERSION;

int	ipf_features = 0
#ifdef	IPFILTER_LKM
		| IPF_FEAT_LKM
#endif
#ifdef	IPFILTER_LOG
		| IPF_FEAT_LOG
#endif
		| IPF_FEAT_LOOKUP
#ifdef	IPFILTER_BPF
		| IPF_FEAT_BPF
#endif
#ifdef	IPFILTER_COMPILED
		| IPF_FEAT_COMPILED
#endif
#ifdef	IPFILTER_CKSUM
		| IPF_FEAT_CKSUM
#endif
		| IPF_FEAT_SYNC
#ifdef	IPFILTER_SCAN
		| IPF_FEAT_SCAN
#endif
#ifdef	USE_INET6
		| IPF_FEAT_IPV6
#endif
	;


/*
 * Table of functions available for use with call rules.
 */
static ipfunc_resolve_t ipf_availfuncs[] = {
	{ "srcgrpmap", ipf_srcgrpmap, ipf_grpmapinit, ipf_grpmapfini },
	{ "dstgrpmap", ipf_dstgrpmap, ipf_grpmapinit, ipf_grpmapfini },
	{ "",	      NULL,	      NULL,	      NULL }
};

static ipftuneable_t ipf_main_tuneables[] = {
	{ { (void *)offsetof(struct ipf_main_softc_s, ipf_flags) },
		"ipf_flags",		0,	0xffffffff,
		stsizeof(ipf_main_softc_t, ipf_flags),
		0,			NULL,	NULL },
	{ { (void *)offsetof(struct ipf_main_softc_s, ipf_active) },
		"active",		0,	0,
		stsizeof(ipf_main_softc_t, ipf_active),
		IPFT_RDONLY,		NULL,	NULL },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_control_forwarding) },
		"control_forwarding",	0, 1,
		stsizeof(ipf_main_softc_t, ipf_control_forwarding),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_update_ipid) },
		"update_ipid",		0,	1,
		stsizeof(ipf_main_softc_t, ipf_update_ipid),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_chksrc) },
		"chksrc",		0,	1,
		stsizeof(ipf_main_softc_t, ipf_chksrc),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_minttl) },
		"min_ttl",		0,	1,
		stsizeof(ipf_main_softc_t, ipf_minttl),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_icmpminfragmtu) },
		"icmp_minfragmtu",	0,	1,
		stsizeof(ipf_main_softc_t, ipf_icmpminfragmtu),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_pass) },
		"default_pass",		0,	0xffffffff,
		stsizeof(ipf_main_softc_t, ipf_pass),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_tcpidletimeout) },
		"tcp_idle_timeout",	1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_tcpidletimeout),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_tcpclosewait) },
		"tcp_close_wait",	1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_tcpclosewait),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_tcplastack) },
		"tcp_last_ack",		1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_tcplastack),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_tcptimeout) },
		"tcp_timeout",		1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_tcptimeout),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_tcpsynsent) },
		"tcp_syn_sent",		1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_tcpsynsent),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_tcpsynrecv) },
		"tcp_syn_received",	1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_tcpsynrecv),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_tcpclosed) },
		"tcp_closed",		1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_tcpclosed),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_tcphalfclosed) },
		"tcp_half_closed",	1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_tcphalfclosed),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_tcptimewait) },
		"tcp_time_wait",	1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_tcptimewait),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_udptimeout) },
		"udp_timeout",		1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_udptimeout),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_udpacktimeout) },
		"udp_ack_timeout",	1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_udpacktimeout),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_icmptimeout) },
		"icmp_timeout",		1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_icmptimeout),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_icmpacktimeout) },
		"icmp_ack_timeout",	1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_icmpacktimeout),
		0,			NULL,	ipf_settimeout },
	{ { (void *)offsetof(ipf_main_softc_t, ipf_iptimeout) },
		"ip_timeout",		1,	0x7fffffff,
		stsizeof(ipf_main_softc_t, ipf_iptimeout),
		0,			NULL,	ipf_settimeout },
#if defined(INSTANCES) && defined(_KERNEL)
	{ { (void *)offsetof(ipf_main_softc_t, ipf_get_loopback) },
		"intercept_loopback",	0,	1,
		stsizeof(ipf_main_softc_t, ipf_get_loopback),
		0,			NULL,	ipf_set_loopback },
#endif
	{ { 0 },
		NULL,			0,	0,
		0,
		0,			NULL,	NULL }
};


/*
 * The next section of code is a collection of small routines that set
 * fields in the fr_info_t structure passed based on properties of the
 * current packet.  There are different routines for the same protocol
 * for each of IPv4 and IPv6.  Adding a new protocol, for which there
 * will "special" inspection for setup, is now more easily done by adding
 * a new routine and expanding the ipf_pr_ipinit*() function rather than by
 * adding more code to a growing switch statement.
 */
#ifdef USE_INET6
static	INLINE int	ipf_pr_ah6 __P((fr_info_t *));
static	INLINE void	ipf_pr_esp6 __P((fr_info_t *));
static	INLINE void	ipf_pr_gre6 __P((fr_info_t *));
static	INLINE void	ipf_pr_udp6 __P((fr_info_t *));
static	INLINE void	ipf_pr_tcp6 __P((fr_info_t *));
static	INLINE void	ipf_pr_icmp6 __P((fr_info_t *));
static	INLINE void	ipf_pr_ipv6hdr __P((fr_info_t *));
static	INLINE void	ipf_pr_short6 __P((fr_info_t *, int));
static	INLINE int	ipf_pr_hopopts6 __P((fr_info_t *));
static	INLINE int	ipf_pr_mobility6 __P((fr_info_t *));
static	INLINE int	ipf_pr_routing6 __P((fr_info_t *));
static	INLINE int	ipf_pr_dstopts6 __P((fr_info_t *));
static	INLINE int	ipf_pr_fragment6 __P((fr_info_t *));
static	INLINE struct ip6_ext *ipf_pr_ipv6exthdr __P((fr_info_t *, int, int));


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_short6                                               */
/* Returns:     void                                                        */
/* Parameters:  fin(I)  - pointer to packet information                     */
/*              xmin(I) - minimum header size                               */
/*                                                                          */
/* IPv6 Only                                                                */
/* This is function enforces the 'is a packet too short to be legit' rule   */
/* for IPv6 and marks the packet with FI_SHORT if so.  See function comment */
/* for ipf_pr_short() for more details.                                     */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_short6(fin, xmin)
	fr_info_t *fin;
	int xmin;
{

	if (fin->fin_dlen < xmin)
		fin->fin_flx |= FI_SHORT;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_ipv6hdr                                              */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv6 Only                                                                */
/* Copy values from the IPv6 header into the fr_info_t struct and call the  */
/* per-protocol analyzer if it exists.  In validating the packet, a protocol*/
/* analyzer may pullup or free the packet itself so we need to be vigiliant */
/* of that possibility arising.                                             */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_ipv6hdr(fin)
	fr_info_t *fin;
{
	ip6_t *ip6 = (ip6_t *)fin->fin_ip;
	int p, go = 1, i, hdrcount;
	fr_ip_t *fi = &fin->fin_fi;

	fin->fin_off = 0;

	fi->fi_tos = 0;
	fi->fi_optmsk = 0;
	fi->fi_secmsk = 0;
	fi->fi_auth = 0;

	p = ip6->ip6_nxt;
	fin->fin_crc = p;
	fi->fi_ttl = ip6->ip6_hlim;
	fi->fi_src.in6 = ip6->ip6_src;
	fin->fin_crc += fi->fi_src.i6[0];
	fin->fin_crc += fi->fi_src.i6[1];
	fin->fin_crc += fi->fi_src.i6[2];
	fin->fin_crc += fi->fi_src.i6[3];
	fi->fi_dst.in6 = ip6->ip6_dst;
	fin->fin_crc += fi->fi_dst.i6[0];
	fin->fin_crc += fi->fi_dst.i6[1];
	fin->fin_crc += fi->fi_dst.i6[2];
	fin->fin_crc += fi->fi_dst.i6[3];
	fin->fin_id = 0;
	if (IN6_IS_ADDR_MULTICAST(&fi->fi_dst.in6))
		fin->fin_flx |= FI_MULTICAST|FI_MBCAST;

	hdrcount = 0;
	while (go && !(fin->fin_flx & FI_SHORT)) {
		switch (p)
		{
		case IPPROTO_UDP :
			ipf_pr_udp6(fin);
			go = 0;
			break;

		case IPPROTO_TCP :
			ipf_pr_tcp6(fin);
			go = 0;
			break;

		case IPPROTO_ICMPV6 :
			ipf_pr_icmp6(fin);
			go = 0;
			break;

		case IPPROTO_GRE :
			ipf_pr_gre6(fin);
			go = 0;
			break;

		case IPPROTO_HOPOPTS :
			p = ipf_pr_hopopts6(fin);
			break;

		case IPPROTO_MOBILITY :
			p = ipf_pr_mobility6(fin);
			break;

		case IPPROTO_DSTOPTS :
			p = ipf_pr_dstopts6(fin);
			break;

		case IPPROTO_ROUTING :
			p = ipf_pr_routing6(fin);
			break;

		case IPPROTO_AH :
			p = ipf_pr_ah6(fin);
			break;

		case IPPROTO_ESP :
			ipf_pr_esp6(fin);
			go = 0;
			break;

		case IPPROTO_IPV6 :
			for (i = 0; ip6exthdr[i].ol_bit != 0; i++)
				if (ip6exthdr[i].ol_val == p) {
					fin->fin_flx |= ip6exthdr[i].ol_bit;
					break;
				}
			go = 0;
			break;

		case IPPROTO_NONE :
			go = 0;
			break;

		case IPPROTO_FRAGMENT :
			p = ipf_pr_fragment6(fin);
			/*
			 * Given that the only fragments we want to let through
			 * (where fin_off != 0) are those where the non-first
			 * fragments only have data, we can safely stop looking
			 * at headers if this is a non-leading fragment.
			 */
			if (fin->fin_off != 0)
				go = 0;
			break;

		default :
			go = 0;
			break;
		}
		hdrcount++;

		/*
		 * It is important to note that at this point, for the
		 * extension headers (go != 0), the entire header may not have
		 * been pulled up when the code gets to this point.  This is
		 * only done for "go != 0" because the other header handlers
		 * will all pullup their complete header.  The other indicator
		 * of an incomplete packet is that this was just an extension
		 * header.
		 */
		if ((go != 0) && (p != IPPROTO_NONE) &&
		    (ipf_pr_pullup(fin, 0) == -1)) {
			p = IPPROTO_NONE;
			break;
		}
	}

	/*
	 * Some of the above functions, like ipf_pr_esp6(), can call ipf_pullup
	 * and destroy whatever packet was here.  The caller of this function
	 * expects us to return if there is a problem with ipf_pullup.
	 */
	if (fin->fin_m == NULL) {
		ipf_main_softc_t *softc = fin->fin_main_soft;

		LBUMPD(ipf_stats[fin->fin_out], fr_v6_bad);
		return;
	}

	fi->fi_p = p;

	/*
	 * IPv6 fragment case 1 - see comment for ipf_pr_fragment6().
	 * "go != 0" imples the above loop hasn't arrived at a layer 4 header.
	 */
	if ((go != 0) && (fin->fin_flx & FI_FRAG) && (fin->fin_off == 0)) {
		ipf_main_softc_t *softc = fin->fin_main_soft;

		fin->fin_flx |= FI_BAD;
		DT2(ipf_fi_bad_ipv6_frag_1, fr_info_t *, fin, int, go);
		LBUMPD(ipf_stats[fin->fin_out], fr_v6_badfrag);
		LBUMP(ipf_stats[fin->fin_out].fr_v6_bad);
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_ipv6exthdr                                           */
/* Returns:     struct ip6_ext * - pointer to the start of the next header  */
/*                                 or NULL if there is a prolblem.          */
/* Parameters:  fin(I)      - pointer to packet information                 */
/*              multiple(I) - flag indicating yes/no if multiple occurances */
/*                            of this extension header are allowed.         */
/*              proto(I)    - protocol number for this extension header     */
/*                                                                          */
/* IPv6 Only                                                                */
/* This function embodies a number of common checks that all IPv6 extension */
/* headers must be subjected to.  For example, making sure the packet is    */
/* big enough for it to be in, checking if it is repeated and setting a     */
/* flag to indicate its presence.                                           */
/* ------------------------------------------------------------------------ */
static INLINE struct ip6_ext *
ipf_pr_ipv6exthdr(fin, multiple, proto)
	fr_info_t *fin;
	int multiple, proto;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	struct ip6_ext *hdr;
	u_short shift;
	int i;

	fin->fin_flx |= FI_V6EXTHDR;

				/* 8 is default length of extension hdr */
	if ((fin->fin_dlen - 8) < 0) {
		fin->fin_flx |= FI_SHORT;
		LBUMPD(ipf_stats[fin->fin_out], fr_v6_ext_short);
		return NULL;
	}

	if (ipf_pr_pullup(fin, 8) == -1) {
		LBUMPD(ipf_stats[fin->fin_out], fr_v6_ext_pullup);
		return NULL;
	}

	hdr = fin->fin_dp;
	switch (proto)
	{
	case IPPROTO_FRAGMENT :
		shift = 8;
		break;
	default :
		shift = 8 + (hdr->ip6e_len << 3);
		break;
	}

	if (shift > fin->fin_dlen) {	/* Nasty extension header length? */
		fin->fin_flx |= FI_BAD;
		DT3(ipf_fi_bad_pr_ipv6exthdr_len, fr_info_t *, fin, u_short, shift, u_short, fin->fin_dlen);
		LBUMPD(ipf_stats[fin->fin_out], fr_v6_ext_hlen);
		return NULL;
	}

	fin->fin_dp = (char *)fin->fin_dp + shift;
	fin->fin_dlen -= shift;

	/*
	 * If we have seen a fragment header, do not set any flags to indicate
	 * the presence of this extension header as it has no impact on the
	 * end result until after it has been defragmented.
	 */
	if (fin->fin_flx & FI_FRAG)
		return hdr;

	for (i = 0; ip6exthdr[i].ol_bit != 0; i++)
		if (ip6exthdr[i].ol_val == proto) {
			/*
			 * Most IPv6 extension headers are only allowed once.
			 */
			if ((multiple == 0) &&
			    ((fin->fin_optmsk & ip6exthdr[i].ol_bit) != 0)) {
				fin->fin_flx |= FI_BAD;
				DT2(ipf_fi_bad_ipv6exthdr_once, fr_info_t *, fin, u_int, (fin->fin_optmsk & ip6exthdr[i].ol_bit));
			} else
				fin->fin_optmsk |= ip6exthdr[i].ol_bit;
			break;
		}

	return hdr;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_hopopts6                                             */
/* Returns:     int    - value of the next header or IPPROTO_NONE if error  */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv6 Only                                                                */
/* This is function checks pending hop by hop options extension header      */
/* ------------------------------------------------------------------------ */
static INLINE int
ipf_pr_hopopts6(fin)
	fr_info_t *fin;
{
	struct ip6_ext *hdr;

	hdr = ipf_pr_ipv6exthdr(fin, 0, IPPROTO_HOPOPTS);
	if (hdr == NULL)
		return IPPROTO_NONE;
	return hdr->ip6e_nxt;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_mobility6                                            */
/* Returns:     int    - value of the next header or IPPROTO_NONE if error  */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv6 Only                                                                */
/* This is function checks the IPv6 mobility extension header               */
/* ------------------------------------------------------------------------ */
static INLINE int
ipf_pr_mobility6(fin)
	fr_info_t *fin;
{
	struct ip6_ext *hdr;

	hdr = ipf_pr_ipv6exthdr(fin, 0, IPPROTO_MOBILITY);
	if (hdr == NULL)
		return IPPROTO_NONE;
	return hdr->ip6e_nxt;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_routing6                                             */
/* Returns:     int    - value of the next header or IPPROTO_NONE if error  */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv6 Only                                                                */
/* This is function checks pending routing extension header                 */
/* ------------------------------------------------------------------------ */
static INLINE int
ipf_pr_routing6(fin)
	fr_info_t *fin;
{
	struct ip6_routing *hdr;

	hdr = (struct ip6_routing *)ipf_pr_ipv6exthdr(fin, 0, IPPROTO_ROUTING);
	if (hdr == NULL)
		return IPPROTO_NONE;

	switch (hdr->ip6r_type)
	{
	case 0 :
		/*
		 * Nasty extension header length?
		 */
		if (((hdr->ip6r_len >> 1) < hdr->ip6r_segleft) ||
		    (hdr->ip6r_segleft && (hdr->ip6r_len & 1))) {
			ipf_main_softc_t *softc = fin->fin_main_soft;

			fin->fin_flx |= FI_BAD;
			DT1(ipf_fi_bad_routing6, fr_info_t *, fin);
			LBUMPD(ipf_stats[fin->fin_out], fr_v6_rh_bad);
			return IPPROTO_NONE;
		}
		break;

	default :
		break;
	}

	return hdr->ip6r_nxt;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_fragment6                                            */
/* Returns:     int    - value of the next header or IPPROTO_NONE if error  */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv6 Only                                                                */
/* Examine the IPv6 fragment header and extract fragment offset information.*/
/*                                                                          */
/* Fragments in IPv6 are extraordinarily difficult to deal with - much more */
/* so than in IPv4.  There are 5 cases of fragments with IPv6 that all      */
/* packets with a fragment header can fit into.  They are as follows:       */
/*                                                                          */
/* 1.  [IPv6][0-n EH][FH][0-n EH] (no L4HDR present)                        */
/* 2.  [IPV6][0-n EH][FH][0-n EH][L4HDR part] (short)                       */
/* 3.  [IPV6][0-n EH][FH][L4HDR part][0-n data] (short)                     */
/* 4.  [IPV6][0-n EH][FH][0-n EH][L4HDR][0-n data]                          */
/* 5.  [IPV6][0-n EH][FH][data]                                             */
/*                                                                          */
/* IPV6 = IPv6 header, FH = Fragment Header,                                */
/* 0-n EH = 0 or more extension headers, 0-n data = 0 or more bytes of data */
/*                                                                          */
/* Packets that match 1, 2, 3 will be dropped as the only reasonable        */
/* scenario in which they happen is in extreme circumstances that are most  */
/* likely to be an indication of an attack rather than normal traffic.      */
/* A type 3 packet may be sent by an attacked after a type 4 packet.  There */
/* are two rules that can be used to guard against type 3 packets: L4       */
/* headers must always be in a packet that has the offset field set to 0    */
/* and no packet is allowed to overlay that where offset = 0.               */
/* ------------------------------------------------------------------------ */
static INLINE int
ipf_pr_fragment6(fin)
	fr_info_t *fin;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	struct ip6_frag *frag;

	fin->fin_flx |= FI_FRAG;

	frag = (struct ip6_frag *)ipf_pr_ipv6exthdr(fin, 0, IPPROTO_FRAGMENT);
	if (frag == NULL) {
		LBUMPD(ipf_stats[fin->fin_out], fr_v6_frag_bad);
		return IPPROTO_NONE;
	}

	if ((frag->ip6f_offlg & IP6F_MORE_FRAG) != 0) {
		/*
		 * Any fragment that isn't the last fragment must have its
		 * length as a multiple of 8.
		 */
		if ((fin->fin_plen & 7) != 0) {
			fin->fin_flx |= FI_BAD;
			DT2(ipf_fi_bad_frag_not_8, fr_info_t *, fin, u_int, (fin->fin_plen & 7));
		}
	}

	fin->fin_fraghdr = frag;
	fin->fin_id = frag->ip6f_ident;
	fin->fin_off = ntohs(frag->ip6f_offlg & IP6F_OFF_MASK);
	if (fin->fin_off != 0)
		fin->fin_flx |= FI_FRAGBODY;

	/*
	 * Jumbograms aren't handled, so the max. length is 64k
	 */
	if ((fin->fin_off << 3) + fin->fin_dlen > 65535) {
		  fin->fin_flx |= FI_BAD;
		  DT2(ipf_fi_bad_jumbogram, fr_info_t *, fin, u_int, ((fin->fin_off << 3) + fin->fin_dlen));
	}

	/*
	 * We don't know where the transport layer header (or whatever is next
	 * is), as it could be behind destination options (amongst others) so
	 * return the fragment header as the type of packet this is.  Note that
	 * this effectively disables the fragment cache for > 1 protocol at a
	 * time.
	 */
	return frag->ip6f_nxt;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_dstopts6                                             */
/* Returns:     int    - value of the next header or IPPROTO_NONE if error  */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv6 Only                                                                */
/* This is function checks pending destination options extension header     */
/* ------------------------------------------------------------------------ */
static INLINE int
ipf_pr_dstopts6(fin)
	fr_info_t *fin;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	struct ip6_ext *hdr;

	hdr = ipf_pr_ipv6exthdr(fin, 0, IPPROTO_DSTOPTS);
	if (hdr == NULL) {
		LBUMPD(ipf_stats[fin->fin_out], fr_v6_dst_bad);
		return IPPROTO_NONE;
	}
	return hdr->ip6e_nxt;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_icmp6                                                */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv6 Only                                                                */
/* This routine is mainly concerned with determining the minimum valid size */
/* for an ICMPv6 packet.                                                    */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_icmp6(fin)
	fr_info_t *fin;
{
	int minicmpsz = sizeof(struct icmp6_hdr);
	struct icmp6_hdr *icmp6;

	if (ipf_pr_pullup(fin, ICMP6ERR_MINPKTLEN - sizeof(ip6_t)) == -1) {
		ipf_main_softc_t *softc = fin->fin_main_soft;

		LBUMPD(ipf_stats[fin->fin_out], fr_v6_icmp6_pullup);
		return;
	}

	if (fin->fin_dlen > 1) {
		ip6_t *ip6;

		icmp6 = fin->fin_dp;

		fin->fin_data[0] = *(u_short *)icmp6;

		if ((icmp6->icmp6_type & ICMP6_INFOMSG_MASK) != 0)
			fin->fin_flx |= FI_ICMPQUERY;

		switch (icmp6->icmp6_type)
		{
		case ICMP6_ECHO_REPLY :
		case ICMP6_ECHO_REQUEST :
			if (fin->fin_dlen >= 6)
				fin->fin_data[1] = icmp6->icmp6_id;
			minicmpsz = ICMP6ERR_MINPKTLEN - sizeof(ip6_t);
			break;

		case ICMP6_DST_UNREACH :
		case ICMP6_PACKET_TOO_BIG :
		case ICMP6_TIME_EXCEEDED :
		case ICMP6_PARAM_PROB :
			fin->fin_flx |= FI_ICMPERR;
			minicmpsz = ICMP6ERR_IPICMPHLEN - sizeof(ip6_t);
			if (fin->fin_plen < ICMP6ERR_IPICMPHLEN)
				break;

			if (M_LEN(fin->fin_m) < fin->fin_plen) {
				if (ipf_coalesce(fin) != 1)
					return;
			}

			if (ipf_pr_pullup(fin, ICMP6ERR_MINPKTLEN) == -1)
				return;

			/*
			 * If the destination of this packet doesn't match the
			 * source of the original packet then this packet is
			 * not correct.
			 */
			icmp6 = fin->fin_dp;
			ip6 = (ip6_t *)((char *)icmp6 + ICMPERR_ICMPHLEN);
			if (IP6_NEQ(&fin->fin_fi.fi_dst,
				    (i6addr_t *)&ip6->ip6_src)) {
				fin->fin_flx |= FI_BAD;
				DT1(ipf_fi_bad_icmp6, fr_info_t *, fin);
			}
			break;
		default :
			break;
		}
	}

	ipf_pr_short6(fin, minicmpsz);
	if ((fin->fin_flx & (FI_SHORT|FI_BAD)) == 0) {
		u_char p = fin->fin_p;

		fin->fin_p = IPPROTO_ICMPV6;
		ipf_checkv6sum(fin);
		fin->fin_p = p;
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_udp6                                                 */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv6 Only                                                                */
/* Analyse the packet for IPv6/UDP properties.                              */
/* Is not expected to be called for fragmented packets.                     */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_udp6(fin)
	fr_info_t *fin;
{

	if (ipf_pr_udpcommon(fin) == 0) {
		u_char p = fin->fin_p;

		fin->fin_p = IPPROTO_UDP;
		ipf_checkv6sum(fin);
		fin->fin_p = p;
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_tcp6                                                 */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv6 Only                                                                */
/* Analyse the packet for IPv6/TCP properties.                              */
/* Is not expected to be called for fragmented packets.                     */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_tcp6(fin)
	fr_info_t *fin;
{

	if (ipf_pr_tcpcommon(fin) == 0) {
		u_char p = fin->fin_p;

		fin->fin_p = IPPROTO_TCP;
		ipf_checkv6sum(fin);
		fin->fin_p = p;
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_esp6                                                 */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv6 Only                                                                */
/* Analyse the packet for ESP properties.                                   */
/* The minimum length is taken to be the SPI (32bits) plus a tail (32bits)  */
/* even though the newer ESP packets must also have a sequence number that  */
/* is 32bits as well, it is not possible(?) to determine the version from a */
/* simple packet header.                                                    */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_esp6(fin)
	fr_info_t *fin;
{

	if ((fin->fin_off == 0) && (ipf_pr_pullup(fin, 8) == -1)) {
		ipf_main_softc_t *softc = fin->fin_main_soft;

		LBUMPD(ipf_stats[fin->fin_out], fr_v6_esp_pullup);
		return;
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_ah6                                                  */
/* Returns:     int    - value of the next header or IPPROTO_NONE if error  */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv6 Only                                                                */
/* Analyse the packet for AH properties.                                    */
/* The minimum length is taken to be the combination of all fields in the   */
/* header being present and no authentication data (null algorithm used.)   */
/* ------------------------------------------------------------------------ */
static INLINE int
ipf_pr_ah6(fin)
	fr_info_t *fin;
{
	authhdr_t *ah;

	fin->fin_flx |= FI_AH;

	ah = (authhdr_t *)ipf_pr_ipv6exthdr(fin, 0, IPPROTO_HOPOPTS);
	if (ah == NULL) {
		ipf_main_softc_t *softc = fin->fin_main_soft;

		LBUMPD(ipf_stats[fin->fin_out], fr_v6_ah_bad);
		return IPPROTO_NONE;
	}

	ipf_pr_short6(fin, sizeof(*ah));

	/*
	 * No need for another pullup, ipf_pr_ipv6exthdr() will pullup
	 * enough data to satisfy ah_next (the very first one.)
	 */
	return ah->ah_next;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_gre6                                                 */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* Analyse the packet for GRE properties.                                   */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_gre6(fin)
	fr_info_t *fin;
{
	grehdr_t *gre;

	if (ipf_pr_pullup(fin, sizeof(grehdr_t)) == -1) {
		ipf_main_softc_t *softc = fin->fin_main_soft;

		LBUMPD(ipf_stats[fin->fin_out], fr_v6_gre_pullup);
		return;
	}

	gre = fin->fin_dp;
	if (GRE_REV(gre->gr_flags) == 1)
		fin->fin_data[0] = gre->gr_call;
}
#endif	/* USE_INET6 */


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_pullup                                               */
/* Returns:     int     - 0 == pullup succeeded, -1 == failure              */
/* Parameters:  fin(I)  - pointer to packet information                     */
/*              plen(I) - length (excluding L3 header) to pullup            */
/*                                                                          */
/* Short inline function to cut down on code duplication to perform a call  */
/* to ipf_pullup to ensure there is the required amount of data,            */
/* consecutively in the packet buffer.                                      */
/*                                                                          */
/* This function pulls up 'extra' data at the location of fin_dp.  fin_dp   */
/* points to the first byte after the complete layer 3 header, which will   */
/* include all of the known extension headers for IPv6 or options for IPv4. */
/*                                                                          */
/* Since fr_pullup() expects the total length of bytes to be pulled up, it  */
/* is necessary to add those we can already assume to be pulled up (fin_dp  */
/* - fin_ip) to what is passed through.                                     */
/* ------------------------------------------------------------------------ */
int
ipf_pr_pullup(fin, plen)
	fr_info_t *fin;
	int plen;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;

	if (fin->fin_m != NULL) {
		if (fin->fin_dp != NULL)
			plen += (char *)fin->fin_dp -
				((char *)fin->fin_ip + fin->fin_hlen);
		plen += fin->fin_hlen;
		if (M_LEN(fin->fin_m) < plen + fin->fin_ipoff) {
#if defined(_KERNEL)
			if (ipf_pullup(fin->fin_m, fin, plen) == NULL) {
				DT(ipf_pullup_fail);
				LBUMP(ipf_stats[fin->fin_out].fr_pull[1]);
				return -1;
			}
			LBUMP(ipf_stats[fin->fin_out].fr_pull[0]);
#else
			LBUMP(ipf_stats[fin->fin_out].fr_pull[1]);
			/*
			 * Fake ipf_pullup failing
			 */
			fin->fin_reason = FRB_PULLUP;
			*fin->fin_mp = NULL;
			fin->fin_m = NULL;
			fin->fin_ip = NULL;
			return -1;
#endif
		}
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_short                                                */
/* Returns:     void                                                        */
/* Parameters:  fin(I)  - pointer to packet information                     */
/*              xmin(I) - minimum header size                               */
/*                                                                          */
/* Check if a packet is "short" as defined by xmin.  The rule we are        */
/* applying here is that the packet must not be fragmented within the layer */
/* 4 header.  That is, it must not be a fragment that has its offset set to */
/* start within the layer 4 header (hdrmin) or if it is at offset 0, the    */
/* entire layer 4 header must be present (min).                             */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_short(fin, xmin)
	fr_info_t *fin;
	int xmin;
{

	if (fin->fin_off == 0) {
		if (fin->fin_dlen < xmin)
			fin->fin_flx |= FI_SHORT;
	} else if (fin->fin_off < xmin) {
		fin->fin_flx |= FI_SHORT;
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_icmp                                                 */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv4 Only                                                                */
/* Do a sanity check on the packet for ICMP (v4).  In nearly all cases,     */
/* except extrememly bad packets, both type and code will be present.       */
/* The expected minimum size of an ICMP packet is very much dependent on    */
/* the type of it.                                                          */
/*                                                                          */
/* XXX - other ICMP sanity checks?                                          */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_icmp(fin)
	fr_info_t *fin;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	int minicmpsz = sizeof(struct icmp);
	icmphdr_t *icmp;
	ip_t *oip;

	ipf_pr_short(fin, ICMPERR_ICMPHLEN);

	if (fin->fin_off != 0) {
		LBUMPD(ipf_stats[fin->fin_out], fr_v4_icmp_frag);
		return;
	}

	if (ipf_pr_pullup(fin, ICMPERR_ICMPHLEN) == -1) {
		LBUMPD(ipf_stats[fin->fin_out], fr_v4_icmp_pullup);
		return;
	}

	icmp = fin->fin_dp;

	fin->fin_data[0] = *(u_short *)icmp;
	fin->fin_data[1] = icmp->icmp_id;

	switch (icmp->icmp_type)
	{
	case ICMP_ECHOREPLY :
	case ICMP_ECHO :
	/* Router discovery messaes - RFC 1256 */
	case ICMP_ROUTERADVERT :
	case ICMP_ROUTERSOLICIT :
		fin->fin_flx |= FI_ICMPQUERY;
		minicmpsz = ICMP_MINLEN;
		break;
	/*
	 * type(1) + code(1) + cksum(2) + id(2) seq(2) +
	 * 3 * timestamp(3 * 4)
	 */
	case ICMP_TSTAMP :
	case ICMP_TSTAMPREPLY :
		fin->fin_flx |= FI_ICMPQUERY;
		minicmpsz = 20;
		break;
	/*
	 * type(1) + code(1) + cksum(2) + id(2) seq(2) +
	 * mask(4)
	 */
	case ICMP_IREQ :
	case ICMP_IREQREPLY :
	case ICMP_MASKREQ :
	case ICMP_MASKREPLY :
		fin->fin_flx |= FI_ICMPQUERY;
		minicmpsz = 12;
		break;
	/*
	 * type(1) + code(1) + cksum(2) + id(2) seq(2) + ip(20+)
	 */
	case ICMP_UNREACH :
#ifdef icmp_nextmtu
		if (icmp->icmp_code == ICMP_UNREACH_NEEDFRAG) {
			if (icmp->icmp_nextmtu < softc->ipf_icmpminfragmtu) {
				fin->fin_flx |= FI_BAD;
				DT3(ipf_fi_bad_icmp_nextmtu, fr_info_t *, fin, u_int, icmp->icmp_nextmtu, u_int, softc->ipf_icmpminfragmtu);
			}
		}
#endif
		/* FALLTHROUGH */
	case ICMP_SOURCEQUENCH :
	case ICMP_REDIRECT :
	case ICMP_TIMXCEED :
	case ICMP_PARAMPROB :
		fin->fin_flx |= FI_ICMPERR;
		if (ipf_coalesce(fin) != 1) {
			LBUMPD(ipf_stats[fin->fin_out], fr_icmp_coalesce);
			return;
		}

		/*
		 * ICMP error packets should not be generated for IP
		 * packets that are a fragment that isn't the first
		 * fragment.
		 */
		oip = (ip_t *)((char *)fin->fin_dp + ICMPERR_ICMPHLEN);
		if ((ntohs(oip->ip_off) & IP_OFFMASK) != 0) {
			fin->fin_flx |= FI_BAD;
			DT2(ipf_fi_bad_icmp_err, fr_info_t, fin, u_int, (ntohs(oip->ip_off) & IP_OFFMASK));
		}

		/*
		 * If the destination of this packet doesn't match the
		 * source of the original packet then this packet is
		 * not correct.
		 */
		if (oip->ip_src.s_addr != fin->fin_daddr) {
			fin->fin_flx |= FI_BAD;
			DT1(ipf_fi_bad_src_ne_dst, fr_info_t *, fin);
		}
		break;
	default :
		break;
	}

	ipf_pr_short(fin, minicmpsz);

	ipf_checkv4sum(fin);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_tcpcommon                                            */
/* Returns:     int    - 0 = header ok, 1 = bad packet, -1 = buffer error   */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* TCP header sanity checking.  Look for bad combinations of TCP flags,     */
/* and make some checks with how they interact with other fields.           */
/* If compiled with IPFILTER_CKSUM, check to see if the TCP checksum is     */
/* valid and mark the packet as bad if not.                                 */
/* ------------------------------------------------------------------------ */
static INLINE int
ipf_pr_tcpcommon(fin)
	fr_info_t *fin;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	int flags, tlen;
	tcphdr_t *tcp;

	fin->fin_flx |= FI_TCPUDP;
	if (fin->fin_off != 0) {
		LBUMPD(ipf_stats[fin->fin_out], fr_tcp_frag);
		return 0;
	}

	if (ipf_pr_pullup(fin, sizeof(*tcp)) == -1) {
		LBUMPD(ipf_stats[fin->fin_out], fr_tcp_pullup);
		return -1;
	}

	tcp = fin->fin_dp;
	if (fin->fin_dlen > 3) {
		fin->fin_sport = ntohs(tcp->th_sport);
		fin->fin_dport = ntohs(tcp->th_dport);
	}

	if ((fin->fin_flx & FI_SHORT) != 0) {
		LBUMPD(ipf_stats[fin->fin_out], fr_tcp_short);
		return 1;
	}

	/*
	 * Use of the TCP data offset *must* result in a value that is at
	 * least the same size as the TCP header.
	 */
	tlen = TCP_OFF(tcp) << 2;
	if (tlen < sizeof(tcphdr_t)) {
		LBUMPD(ipf_stats[fin->fin_out], fr_tcp_small);
		fin->fin_flx |= FI_BAD;
		DT3(ipf_fi_bad_tlen, fr_info_t, fin, u_int, tlen, u_int, sizeof(tcphdr_t));
		return 1;
	}

	flags = tcp->th_flags;
	fin->fin_tcpf = tcp->th_flags;

	/*
	 * If the urgent flag is set, then the urgent pointer must
	 * also be set and vice versa.  Good TCP packets do not have
	 * just one of these set.
	 */
	if ((flags & TH_URG) != 0 && (tcp->th_urp == 0)) {
		fin->fin_flx |= FI_BAD;
		DT3(ipf_fi_bad_th_urg, fr_info_t*, fin, u_int, (flags & TH_URG), u_int, tcp->th_urp);
#if 0
	} else if ((flags & TH_URG) == 0 && (tcp->th_urp != 0)) {
		/*
		 * Ignore this case (#if 0) as it shows up in "real"
		 * traffic with bogus values in the urgent pointer field.
		 */
		fin->fin_flx |= FI_BAD;
		DT3(ipf_fi_bad_th_urg0, fr_info_t *, fin, u_int, (flags & TH_URG), u_int, tcp->th_urp);
#endif
	} else if (((flags & (TH_SYN|TH_FIN)) != 0) &&
		   ((flags & (TH_RST|TH_ACK)) == TH_RST)) {
		/* TH_FIN|TH_RST|TH_ACK seems to appear "naturally" */
		fin->fin_flx |= FI_BAD;
		DT1(ipf_fi_bad_th_fin_rst_ack, fr_info_t, fin);
#if 1
	} else if (((flags & TH_SYN) != 0) &&
		   ((flags & (TH_URG|TH_PUSH)) != 0)) {
		/*
		 * SYN with URG and PUSH set is not for normal TCP but it is
		 * possible(?) with T/TCP...but who uses T/TCP?
		 */
		fin->fin_flx |= FI_BAD;
		DT1(ipf_fi_bad_th_syn_urg_psh, fr_info_t *, fin);
#endif
	} else if (!(flags & TH_ACK)) {
		/*
		 * If the ack bit isn't set, then either the SYN or
		 * RST bit must be set.  If the SYN bit is set, then
		 * we expect the ACK field to be 0.  If the ACK is
		 * not set and if URG, PSH or FIN are set, consdier
		 * that to indicate a bad TCP packet.
		 */
		if ((flags == TH_SYN) && (tcp->th_ack != 0)) {
			/*
			 * Cisco PIX sets the ACK field to a random value.
			 * In light of this, do not set FI_BAD until a patch
			 * is available from Cisco to ensure that
			 * interoperability between existing systems is
			 * achieved.
			 */
			/*fin->fin_flx |= FI_BAD*/;
			/*DT1(ipf_fi_bad_th_syn_ack, fr_info_t *, fin);*/
		} else if (!(flags & (TH_RST|TH_SYN))) {
			fin->fin_flx |= FI_BAD;
			DT1(ipf_fi_bad_th_rst_syn, fr_info_t *, fin);
		} else if ((flags & (TH_URG|TH_PUSH|TH_FIN)) != 0) {
			fin->fin_flx |= FI_BAD;
			DT1(ipf_fi_bad_th_urg_push_fin, fr_info_t *, fin);
		}
	}
	if (fin->fin_flx & FI_BAD) {
		LBUMPD(ipf_stats[fin->fin_out], fr_tcp_bad_flags);
		return 1;
	}

	/*
	 * At this point, it's not exactly clear what is to be gained by
	 * marking up which TCP options are and are not present.  The one we
	 * are most interested in is the TCP window scale.  This is only in
	 * a SYN packet [RFC1323] so we don't need this here...?
	 * Now if we were to analyse the header for passive fingerprinting,
	 * then that might add some weight to adding this...
	 */
	if (tlen == sizeof(tcphdr_t)) {
		return 0;
	}

	if (ipf_pr_pullup(fin, tlen) == -1) {
		LBUMPD(ipf_stats[fin->fin_out], fr_tcp_pullup);
		return -1;
	}

#if 0
	tcp = fin->fin_dp;
	ip = fin->fin_ip;
	s = (u_char *)(tcp + 1);
	off = IP_HL(ip) << 2;
# ifdef _KERNEL
	if (fin->fin_mp != NULL) {
		mb_t *m = *fin->fin_mp;

		if (off + tlen > M_LEN(m))
			return;
	}
# endif
	for (tlen -= (int)sizeof(*tcp); tlen > 0; ) {
		opt = *s;
		if (opt == '\0')
			break;
		else if (opt == TCPOPT_NOP)
			ol = 1;
		else {
			if (tlen < 2)
				break;
			ol = (int)*(s + 1);
			if (ol < 2 || ol > tlen)
				break;
		}

		for (i = 9, mv = 4; mv >= 0; ) {
			op = ipopts + i;
			if (opt == (u_char)op->ol_val) {
				optmsk |= op->ol_bit;
				break;
			}
		}
		tlen -= ol;
		s += ol;
	}
#endif /* 0 */

	return 0;
}



/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_udpcommon                                            */
/* Returns:     int    - 0 = header ok, 1 = bad packet                      */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* Extract the UDP source and destination ports, if present.  If compiled   */
/* with IPFILTER_CKSUM, check to see if the UDP checksum is valid.          */
/* ------------------------------------------------------------------------ */
static INLINE int
ipf_pr_udpcommon(fin)
	fr_info_t *fin;
{
	udphdr_t *udp;

	fin->fin_flx |= FI_TCPUDP;

	if (!fin->fin_off && (fin->fin_dlen > 3)) {
		if (ipf_pr_pullup(fin, sizeof(*udp)) == -1) {
			ipf_main_softc_t *softc = fin->fin_main_soft;

			fin->fin_flx |= FI_SHORT;
			LBUMPD(ipf_stats[fin->fin_out], fr_udp_pullup);
			return 1;
		}

		udp = fin->fin_dp;

		fin->fin_sport = ntohs(udp->uh_sport);
		fin->fin_dport = ntohs(udp->uh_dport);
	}

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_tcp                                                  */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv4 Only                                                                */
/* Analyse the packet for IPv4/TCP properties.                              */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_tcp(fin)
	fr_info_t *fin;
{

	ipf_pr_short(fin, sizeof(tcphdr_t));

	if (ipf_pr_tcpcommon(fin) == 0)
		ipf_checkv4sum(fin);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_udp                                                  */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv4 Only                                                                */
/* Analyse the packet for IPv4/UDP properties.                              */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_udp(fin)
	fr_info_t *fin;
{

	ipf_pr_short(fin, sizeof(udphdr_t));

	if (ipf_pr_udpcommon(fin) == 0)
		ipf_checkv4sum(fin);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_esp                                                  */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* Analyse the packet for ESP properties.                                   */
/* The minimum length is taken to be the SPI (32bits) plus a tail (32bits)  */
/* even though the newer ESP packets must also have a sequence number that  */
/* is 32bits as well, it is not possible(?) to determine the version from a */
/* simple packet header.                                                    */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_esp(fin)
	fr_info_t *fin;
{

	if (fin->fin_off == 0) {
		ipf_pr_short(fin, 8);
		if (ipf_pr_pullup(fin, 8) == -1) {
			ipf_main_softc_t *softc = fin->fin_main_soft;

			LBUMPD(ipf_stats[fin->fin_out], fr_v4_esp_pullup);
		}
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_ah                                                   */
/* Returns:     int    - value of the next header or IPPROTO_NONE if error  */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* Analyse the packet for AH properties.                                    */
/* The minimum length is taken to be the combination of all fields in the   */
/* header being present and no authentication data (null algorithm used.)   */
/* ------------------------------------------------------------------------ */
static INLINE int
ipf_pr_ah(fin)
	fr_info_t *fin;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	authhdr_t *ah;
	int len;

	fin->fin_flx |= FI_AH;
	ipf_pr_short(fin, sizeof(*ah));

	if (((fin->fin_flx & FI_SHORT) != 0) || (fin->fin_off != 0)) {
		LBUMPD(ipf_stats[fin->fin_out], fr_v4_ah_bad);
		return IPPROTO_NONE;
	}

	if (ipf_pr_pullup(fin, sizeof(*ah)) == -1) {
		DT(fr_v4_ah_pullup_1);
		LBUMP(ipf_stats[fin->fin_out].fr_v4_ah_pullup);
		return IPPROTO_NONE;
	}

	ah = (authhdr_t *)fin->fin_dp;

	len = (ah->ah_plen + 2) << 2;
	ipf_pr_short(fin, len);
	if (ipf_pr_pullup(fin, len) == -1) {
		DT(fr_v4_ah_pullup_2);
		LBUMP(ipf_stats[fin->fin_out].fr_v4_ah_pullup);
		return IPPROTO_NONE;
	}

	/*
	 * Adjust fin_dp and fin_dlen for skipping over the authentication
	 * header.
	 */
	fin->fin_dp = (char *)fin->fin_dp + len;
	fin->fin_dlen -= len;
	return ah->ah_next;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_gre                                                  */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* Analyse the packet for GRE properties.                                   */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_gre(fin)
	fr_info_t *fin;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	grehdr_t *gre;

	ipf_pr_short(fin, sizeof(grehdr_t));

	if (fin->fin_off != 0) {
		LBUMPD(ipf_stats[fin->fin_out], fr_v4_gre_frag);
		return;
	}

	if (ipf_pr_pullup(fin, sizeof(grehdr_t)) == -1) {
		LBUMPD(ipf_stats[fin->fin_out], fr_v4_gre_pullup);
		return;
	}

	gre = fin->fin_dp;
	if (GRE_REV(gre->gr_flags) == 1)
		fin->fin_data[0] = gre->gr_call;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pr_ipv4hdr                                              */
/* Returns:     void                                                        */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* IPv4 Only                                                                */
/* Analyze the IPv4 header and set fields in the fr_info_t structure.       */
/* Check all options present and flag their presence if any exist.          */
/* ------------------------------------------------------------------------ */
static INLINE void
ipf_pr_ipv4hdr(fin)
	fr_info_t *fin;
{
	u_short optmsk = 0, secmsk = 0, auth = 0;
	int hlen, ol, mv, p, i;
	const struct optlist *op;
	u_char *s, opt;
	u_short off;
	fr_ip_t *fi;
	ip_t *ip;

	fi = &fin->fin_fi;
	hlen = fin->fin_hlen;

	ip = fin->fin_ip;
	p = ip->ip_p;
	fi->fi_p = p;
	fin->fin_crc = p;
	fi->fi_tos = ip->ip_tos;
	fin->fin_id = ip->ip_id;
	off = ntohs(ip->ip_off);

	/* Get both TTL and protocol */
	fi->fi_p = ip->ip_p;
	fi->fi_ttl = ip->ip_ttl;

	/* Zero out bits not used in IPv6 address */
	fi->fi_src.i6[1] = 0;
	fi->fi_src.i6[2] = 0;
	fi->fi_src.i6[3] = 0;
	fi->fi_dst.i6[1] = 0;
	fi->fi_dst.i6[2] = 0;
	fi->fi_dst.i6[3] = 0;

	fi->fi_saddr = ip->ip_src.s_addr;
	fin->fin_crc += fi->fi_saddr;
	fi->fi_daddr = ip->ip_dst.s_addr;
	fin->fin_crc += fi->fi_daddr;
	if (IN_CLASSD(ntohl(fi->fi_daddr)))
		fin->fin_flx |= FI_MULTICAST|FI_MBCAST;

	/*
	 * set packet attribute flags based on the offset and
	 * calculate the byte offset that it represents.
	 */
	off &= IP_MF|IP_OFFMASK;
	if (off != 0) {
		int morefrag = off & IP_MF;

		fi->fi_flx |= FI_FRAG;
		off &= IP_OFFMASK;
		if (off != 0) {
			fin->fin_flx |= FI_FRAGBODY;
			off <<= 3;
			if ((off + fin->fin_dlen > 65535) ||
			    (fin->fin_dlen == 0) ||
			    ((morefrag != 0) && ((fin->fin_dlen & 7) != 0))) {
				/*
				 * The length of the packet, starting at its
				 * offset cannot exceed 65535 (0xffff) as the
				 * length of an IP packet is only 16 bits.
				 *
				 * Any fragment that isn't the last fragment
				 * must have a length greater than 0 and it
				 * must be an even multiple of 8.
				 */
				fi->fi_flx |= FI_BAD;
				DT1(ipf_fi_bad_fragbody_gt_65535, fr_info_t *, fin);
			}
		}
	}
	fin->fin_off = off;

	/*
	 * Call per-protocol setup and checking
	 */
	if (p == IPPROTO_AH) {
		/*
		 * Treat AH differently because we expect there to be another
		 * layer 4 header after it.
		 */
		p = ipf_pr_ah(fin);
	}

	switch (p)
	{
	case IPPROTO_UDP :
		ipf_pr_udp(fin);
		break;
	case IPPROTO_TCP :
		ipf_pr_tcp(fin);
		break;
	case IPPROTO_ICMP :
		ipf_pr_icmp(fin);
		break;
	case IPPROTO_ESP :
		ipf_pr_esp(fin);
		break;
	case IPPROTO_GRE :
		ipf_pr_gre(fin);
		break;
	}

	ip = fin->fin_ip;
	if (ip == NULL)
		return;

	/*
	 * If it is a standard IP header (no options), set the flag fields
	 * which relate to options to 0.
	 */
	if (hlen == sizeof(*ip)) {
		fi->fi_optmsk = 0;
		fi->fi_secmsk = 0;
		fi->fi_auth = 0;
		return;
	}

	/*
	 * So the IP header has some IP options attached.  Walk the entire
	 * list of options present with this packet and set flags to indicate
	 * which ones are here and which ones are not.  For the somewhat out
	 * of date and obscure security classification options, set a flag to
	 * represent which classification is present.
	 */
	fi->fi_flx |= FI_OPTIONS;

	for (s = (u_char *)(ip + 1), hlen -= (int)sizeof(*ip); hlen > 0; ) {
		opt = *s;
		if (opt == '\0')
			break;
		else if (opt == IPOPT_NOP)
			ol = 1;
		else {
			if (hlen < 2)
				break;
			ol = (int)*(s + 1);
			if (ol < 2 || ol > hlen)
				break;
		}
		for (i = 9, mv = 4; mv >= 0; ) {
			op = ipopts + i;

			if ((opt == (u_char)op->ol_val) && (ol > 4)) {
				u_32_t doi;

				switch (opt)
				{
				case IPOPT_SECURITY :
					if (optmsk & op->ol_bit) {
						fin->fin_flx |= FI_BAD;
						DT2(ipf_fi_bad_ipopt_security, fr_info_t *, fin, u_short, (optmsk & op->ol_bit));
					} else {
						doi = ipf_checkripso(s);
						secmsk = doi >> 16;
						auth = doi & 0xffff;
					}
					break;

				case IPOPT_CIPSO :

					if (optmsk & op->ol_bit) {
						fin->fin_flx |= FI_BAD;
						DT2(ipf_fi_bad_ipopt_cipso, fr_info_t *, fin, u_short, (optmsk & op->ol_bit));
					} else {
						doi = ipf_checkcipso(fin,
								     s, ol);
						secmsk = doi >> 16;
						auth = doi & 0xffff;
					}
					break;
				}
				optmsk |= op->ol_bit;
			}

			if (opt < op->ol_val)
				i -= mv;
			else
				i += mv;
			mv--;
		}
		hlen -= ol;
		s += ol;
	}

	/*
	 *
	 */
	if (auth && !(auth & 0x0100))
		auth &= 0xff00;
	fi->fi_optmsk = optmsk;
	fi->fi_secmsk = secmsk;
	fi->fi_auth = auth;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_checkripso                                              */
/* Returns:     void                                                        */
/* Parameters:  s(I)   - pointer to start of RIPSO option                   */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static u_32_t
ipf_checkripso(s)
	u_char *s;
{
	const struct optlist *sp;
	u_short secmsk = 0, auth = 0;
	u_char sec;
	int j, m;

	sec = *(s + 2);	/* classification */
	for (j = 3, m = 2; m >= 0; ) {
		sp = secopt + j;
		if (sec == sp->ol_val) {
			secmsk |= sp->ol_bit;
			auth = *(s + 3);
			auth *= 256;
			auth += *(s + 4);
			break;
		}
		if (sec < sp->ol_val)
			j -= m;
		else
			j += m;
		m--;
	}

	return (secmsk << 16) | auth;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_checkcipso                                              */
/* Returns:     u_32_t  - 0 = failure, else the doi from the header         */
/* Parameters:  fin(IO) - pointer to packet information                     */
/*              s(I)    - pointer to start of CIPSO option                  */
/*              ol(I)   - length of CIPSO option field                      */
/*                                                                          */
/* This function returns the domain of integrity (DOI) field from the CIPSO */
/* header and returns that whilst also storing the highest sensitivity      */
/* value found in the fr_info_t structure.                                  */
/*                                                                          */
/* No attempt is made to extract the category bitmaps as these are defined  */
/* by the user (rather than the protocol) and can be rather numerous on the */
/* end nodes.                                                               */
/* ------------------------------------------------------------------------ */
static u_32_t
ipf_checkcipso(fin, s, ol)
	fr_info_t *fin;
	u_char *s;
	int ol;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	fr_ip_t *fi;
	u_32_t doi;
	u_char *t, tag, tlen, sensitivity;
	int len;

	if (ol < 6 || ol > 40) {
		LBUMPD(ipf_stats[fin->fin_out], fr_v4_cipso_bad);
		fin->fin_flx |= FI_BAD;
		DT2(ipf_fi_bad_checkcipso_ol, fr_info_t *, fin, u_int, ol);
		return 0;
	}

	fi = &fin->fin_fi;
	fi->fi_sensitivity = 0;
	/*
	 * The DOI field MUST be there.
	 */
	bcopy(s + 2, &doi, sizeof(doi));

	t = (u_char *)s + 6;
	for (len = ol - 6; len >= 2; len -= tlen, t+= tlen) {
		tag = *t;
		tlen = *(t + 1);
		if (tlen > len || tlen < 4 || tlen > 34) {
			LBUMPD(ipf_stats[fin->fin_out], fr_v4_cipso_tlen);
			fin->fin_flx |= FI_BAD;
			DT2(ipf_fi_bad_checkcipso_tlen, fr_info_t *, fin, u_int, tlen);
			return 0;
		}

		sensitivity = 0;
		/*
		 * Tag numbers 0, 1, 2, 5 are laid out in the CIPSO Internet
		 * draft (16 July 1992) that has expired.
		 */
		if (tag == 0) {
			fin->fin_flx |= FI_BAD;
			DT2(ipf_fi_bad_checkcipso_tag, fr_info_t *, fin, u_int, tag);
			continue;
		} else if (tag == 1) {
			if (*(t + 2) != 0) {
				fin->fin_flx |= FI_BAD;
				DT2(ipf_fi_bad_checkcipso_tag1_t2, fr_info_t *, fin, u_int, (*t + 2));
				continue;
			}
			sensitivity = *(t + 3);
			/* Category bitmap for categories 0-239 */

		} else if (tag == 4) {
			if (*(t + 2) != 0) {
				fin->fin_flx |= FI_BAD;
				DT2(ipf_fi_bad_checkcipso_tag4_t2, fr_info_t *, fin, u_int, (*t + 2));
				continue;
			}
			sensitivity = *(t + 3);
			/* Enumerated categories, 16bits each, upto 15 */

		} else if (tag == 5) {
			if (*(t + 2) != 0) {
				fin->fin_flx |= FI_BAD;
				DT2(ipf_fi_bad_checkcipso_tag5_t2, fr_info_t *, fin, u_int, (*t + 2));
				continue;
			}
			sensitivity = *(t + 3);
			/* Range of categories (2*16bits), up to 7 pairs */

		} else if (tag > 127) {
			/* Custom defined DOI */
			;
		} else {
			fin->fin_flx |= FI_BAD;
			DT2(ipf_fi_bad_checkcipso_tag127, fr_info_t *, fin, u_int, tag);
			continue;
		}

		if (sensitivity > fi->fi_sensitivity)
			fi->fi_sensitivity = sensitivity;
	}

	return doi;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_makefrip                                                */
/* Returns:     int     - 0 == packet ok, -1 == packet freed                */
/* Parameters:  hlen(I) - length of IP packet header                        */
/*              ip(I)   - pointer to the IP header                          */
/*              fin(IO) - pointer to packet information                     */
/*                                                                          */
/* Compact the IP header into a structure which contains just the info.     */
/* which is useful for comparing IP headers with and store this information */
/* in the fr_info_t structure pointer to by fin.  At present, it is assumed */
/* this function will be called with either an IPv4 or IPv6 packet.         */
/* ------------------------------------------------------------------------ */
int
ipf_makefrip(hlen, ip, fin)
	int hlen;
	ip_t *ip;
	fr_info_t *fin;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	int v;

	fin->fin_depth = 0;
	fin->fin_hlen = (u_short)hlen;
	fin->fin_ip = ip;
	fin->fin_rule = 0xffffffff;
	fin->fin_group[0] = -1;
	fin->fin_group[1] = '\0';
	fin->fin_dp = (char *)ip + hlen;

	v = fin->fin_v;
	if (v == 4) {
		fin->fin_plen = ntohs(ip->ip_len);
		fin->fin_dlen = fin->fin_plen - hlen;
		ipf_pr_ipv4hdr(fin);
#ifdef	USE_INET6
	} else if (v == 6) {
		fin->fin_plen = ntohs(((ip6_t *)ip)->ip6_plen);
		fin->fin_dlen = fin->fin_plen;
		fin->fin_plen += hlen;

		ipf_pr_ipv6hdr(fin);
#endif
	}
	if (fin->fin_ip == NULL) {
		LBUMP(ipf_stats[fin->fin_out].fr_ip_freed);
		return -1;
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_portcheck                                               */
/* Returns:     int - 1 == port matched, 0 == port match failed             */
/* Parameters:  frp(I) - pointer to port check `expression'                 */
/*              pop(I) - port number to evaluate                            */
/*                                                                          */
/* Perform a comparison of a port number against some other(s), using a     */
/* structure with compare information stored in it.                         */
/* ------------------------------------------------------------------------ */
static INLINE int
ipf_portcheck(frp, pop)
	frpcmp_t *frp;
	u_32_t pop;
{
	int err = 1;
	u_32_t po;

	po = frp->frp_port;

	/*
	 * Do opposite test to that required and continue if that succeeds.
	 */
	switch (frp->frp_cmp)
	{
	case FR_EQUAL :
		if (pop != po) /* EQUAL */
			err = 0;
		break;
	case FR_NEQUAL :
		if (pop == po) /* NOTEQUAL */
			err = 0;
		break;
	case FR_LESST :
		if (pop >= po) /* LESSTHAN */
			err = 0;
		break;
	case FR_GREATERT :
		if (pop <= po) /* GREATERTHAN */
			err = 0;
		break;
	case FR_LESSTE :
		if (pop > po) /* LT or EQ */
			err = 0;
		break;
	case FR_GREATERTE :
		if (pop < po) /* GT or EQ */
			err = 0;
		break;
	case FR_OUTRANGE :
		if (pop >= po && pop <= frp->frp_top) /* Out of range */
			err = 0;
		break;
	case FR_INRANGE :
		if (pop <= po || pop >= frp->frp_top) /* In range */
			err = 0;
		break;
	case FR_INCRANGE :
		if (pop < po || pop > frp->frp_top) /* Inclusive range */
			err = 0;
		break;
	default :
		break;
	}
	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_tcpudpchk                                               */
/* Returns:     int - 1 == protocol matched, 0 == check failed              */
/* Parameters:  fda(I) - pointer to packet information                      */
/*              ft(I)  - pointer to structure with comparison data          */
/*                                                                          */
/* Compares the current pcket (assuming it is TCP/UDP) information with a   */
/* structure containing information that we want to match against.          */
/* ------------------------------------------------------------------------ */
int
ipf_tcpudpchk(fi, ft)
	fr_ip_t *fi;
	frtuc_t *ft;
{
	int err = 1;

	/*
	 * Both ports should *always* be in the first fragment.
	 * So far, I cannot find any cases where they can not be.
	 *
	 * compare destination ports
	 */
	if (ft->ftu_dcmp)
		err = ipf_portcheck(&ft->ftu_dst, fi->fi_ports[1]);

	/*
	 * compare source ports
	 */
	if (err && ft->ftu_scmp)
		err = ipf_portcheck(&ft->ftu_src, fi->fi_ports[0]);

	/*
	 * If we don't have all the TCP/UDP header, then how can we
	 * expect to do any sort of match on it ?  If we were looking for
	 * TCP flags, then NO match.  If not, then match (which should
	 * satisfy the "short" class too).
	 */
	if (err && (fi->fi_p == IPPROTO_TCP)) {
		if (fi->fi_flx & FI_SHORT)
			return !(ft->ftu_tcpf | ft->ftu_tcpfm);
		/*
		 * Match the flags ?  If not, abort this match.
		 */
		if (ft->ftu_tcpfm &&
		    ft->ftu_tcpf != (fi->fi_tcpf & ft->ftu_tcpfm)) {
			FR_DEBUG(("f. %#x & %#x != %#x\n", fi->fi_tcpf,
				 ft->ftu_tcpfm, ft->ftu_tcpf));
			err = 0;
		}
	}
	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_check_ipf                                               */
/* Returns:     int - 0 == match, else no match                             */
/* Parameters:  fin(I)     - pointer to packet information                  */
/*              fr(I)      - pointer to filter rule                         */
/*              portcmp(I) - flag indicating whether to attempt matching on */
/*                           TCP/UDP port data.                             */
/*                                                                          */
/* Check to see if a packet matches an IPFilter rule.  Checks of addresses, */
/* port numbers, etc, for "standard" IPFilter rules are all orchestrated in */
/* this function.                                                           */
/* ------------------------------------------------------------------------ */
static INLINE int
ipf_check_ipf(fin, fr, portcmp)
	fr_info_t *fin;
	frentry_t *fr;
	int portcmp;
{
	u_32_t	*ld, *lm, *lip;
	fripf_t *fri;
	fr_ip_t *fi;
	int i;

	fi = &fin->fin_fi;
	fri = fr->fr_ipf;
	lip = (u_32_t *)fi;
	lm = (u_32_t *)&fri->fri_mip;
	ld = (u_32_t *)&fri->fri_ip;

	/*
	 * first 32 bits to check coversion:
	 * IP version, TOS, TTL, protocol
	 */
	i = ((*lip & *lm) != *ld);
	FR_DEBUG(("0. %#08x & %#08x != %#08x\n",
		   ntohl(*lip), ntohl(*lm), ntohl(*ld)));
	if (i)
		return 1;

	/*
	 * Next 32 bits is a constructed bitmask indicating which IP options
	 * are present (if any) in this packet.
	 */
	lip++, lm++, ld++;
	i = ((*lip & *lm) != *ld);
	FR_DEBUG(("1. %#08x & %#08x != %#08x\n",
		   ntohl(*lip), ntohl(*lm), ntohl(*ld)));
	if (i != 0)
		return 1;

	lip++, lm++, ld++;
	/*
	 * Unrolled loops (4 each, for 32 bits) for address checks.
	 */
	/*
	 * Check the source address.
	 */
	if (fr->fr_satype == FRI_LOOKUP) {
		i = (*fr->fr_srcfunc)(fin->fin_main_soft, fr->fr_srcptr,
				      fi->fi_v, lip, fin->fin_plen);
		if (i == -1)
			return 1;
		lip += 3;
		lm += 3;
		ld += 3;
	} else {
		i = ((*lip & *lm) != *ld);
		FR_DEBUG(("2a. %#08x & %#08x != %#08x\n",
			   ntohl(*lip), ntohl(*lm), ntohl(*ld)));
		if (fi->fi_v == 6) {
			lip++, lm++, ld++;
			i |= ((*lip & *lm) != *ld);
			FR_DEBUG(("2b. %#08x & %#08x != %#08x\n",
				   ntohl(*lip), ntohl(*lm), ntohl(*ld)));
			lip++, lm++, ld++;
			i |= ((*lip & *lm) != *ld);
			FR_DEBUG(("2c. %#08x & %#08x != %#08x\n",
				   ntohl(*lip), ntohl(*lm), ntohl(*ld)));
			lip++, lm++, ld++;
			i |= ((*lip & *lm) != *ld);
			FR_DEBUG(("2d. %#08x & %#08x != %#08x\n",
				   ntohl(*lip), ntohl(*lm), ntohl(*ld)));
		} else {
			lip += 3;
			lm += 3;
			ld += 3;
		}
	}
	i ^= (fr->fr_flags & FR_NOTSRCIP) >> 6;
	if (i != 0)
		return 1;

	/*
	 * Check the destination address.
	 */
	lip++, lm++, ld++;
	if (fr->fr_datype == FRI_LOOKUP) {
		i = (*fr->fr_dstfunc)(fin->fin_main_soft, fr->fr_dstptr,
				      fi->fi_v, lip, fin->fin_plen);
		if (i == -1)
			return 1;
		lip += 3;
		lm += 3;
		ld += 3;
	} else {
		i = ((*lip & *lm) != *ld);
		FR_DEBUG(("3a. %#08x & %#08x != %#08x\n",
			   ntohl(*lip), ntohl(*lm), ntohl(*ld)));
		if (fi->fi_v == 6) {
			lip++, lm++, ld++;
			i |= ((*lip & *lm) != *ld);
			FR_DEBUG(("3b. %#08x & %#08x != %#08x\n",
				   ntohl(*lip), ntohl(*lm), ntohl(*ld)));
			lip++, lm++, ld++;
			i |= ((*lip & *lm) != *ld);
			FR_DEBUG(("3c. %#08x & %#08x != %#08x\n",
				   ntohl(*lip), ntohl(*lm), ntohl(*ld)));
			lip++, lm++, ld++;
			i |= ((*lip & *lm) != *ld);
			FR_DEBUG(("3d. %#08x & %#08x != %#08x\n",
				   ntohl(*lip), ntohl(*lm), ntohl(*ld)));
		} else {
			lip += 3;
			lm += 3;
			ld += 3;
		}
	}
	i ^= (fr->fr_flags & FR_NOTDSTIP) >> 7;
	if (i != 0)
		return 1;
	/*
	 * IP addresses matched.  The next 32bits contains:
	 * mast of old IP header security & authentication bits.
	 */
	lip++, lm++, ld++;
	i = (*ld - (*lip & *lm));
	FR_DEBUG(("4. %#08x & %#08x != %#08x\n", *lip, *lm, *ld));

	/*
	 * Next we have 32 bits of packet flags.
	 */
	lip++, lm++, ld++;
	i |= (*ld - (*lip & *lm));
	FR_DEBUG(("5. %#08x & %#08x != %#08x\n", *lip, *lm, *ld));

	if (i == 0) {
		/*
		 * If a fragment, then only the first has what we're
		 * looking for here...
		 */
		if (portcmp) {
			if (!ipf_tcpudpchk(&fin->fin_fi, &fr->fr_tuc))
				i = 1;
		} else {
			if (fr->fr_dcmp || fr->fr_scmp ||
			    fr->fr_tcpf || fr->fr_tcpfm)
				i = 1;
			if (fr->fr_icmpm || fr->fr_icmp) {
				if (((fi->fi_p != IPPROTO_ICMP) &&
				     (fi->fi_p != IPPROTO_ICMPV6)) ||
				    fin->fin_off || (fin->fin_dlen < 2))
					i = 1;
				else if ((fin->fin_data[0] & fr->fr_icmpm) !=
					 fr->fr_icmp) {
					FR_DEBUG(("i. %#x & %#x != %#x\n",
						 fin->fin_data[0],
						 fr->fr_icmpm, fr->fr_icmp));
					i = 1;
				}
			}
		}
	}
	return i;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_scanlist                                                */
/* Returns:     int - result flags of scanning filter list                  */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              pass(I) - default result to return for filtering            */
/*                                                                          */
/* Check the input/output list of rules for a match to the current packet.  */
/* If a match is found, the value of fr_flags from the rule becomes the     */
/* return value and fin->fin_fr points to the matched rule.                 */
/*                                                                          */
/* This function may be called recusively upto 16 times (limit inbuilt.)    */
/* When unwinding, it should finish up with fin_depth as 0.                 */
/*                                                                          */
/* Could be per interface, but this gets real nasty when you don't have,    */
/* or can't easily change, the kernel source code to .                      */
/* ------------------------------------------------------------------------ */
int
ipf_scanlist(fin, pass)
	fr_info_t *fin;
	u_32_t pass;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	int rulen, portcmp, off, skip;
	struct frentry *fr, *fnext;
	u_32_t passt, passo;

	/*
	 * Do not allow nesting deeper than 16 levels.
	 */
	if (fin->fin_depth >= 16)
		return pass;

	fr = fin->fin_fr;

	/*
	 * If there are no rules in this list, return now.
	 */
	if (fr == NULL)
		return pass;

	skip = 0;
	portcmp = 0;
	fin->fin_depth++;
	fin->fin_fr = NULL;
	off = fin->fin_off;

	if ((fin->fin_flx & FI_TCPUDP) && (fin->fin_dlen > 3) && !off)
		portcmp = 1;

	for (rulen = 0; fr; fr = fnext, rulen++) {
		fnext = fr->fr_next;
		if (skip != 0) {
			FR_VERBOSE(("SKIP %d (%#x)\n", skip, fr->fr_flags));
			skip--;
			continue;
		}

		/*
		 * In all checks below, a null (zero) value in the
		 * filter struture is taken to mean a wildcard.
		 *
		 * check that we are working for the right interface
		 */
#ifdef	_KERNEL
		if (fr->fr_ifa && fr->fr_ifa != fin->fin_ifp)
			continue;
#else
		if (opts & (OPT_VERBOSE|OPT_DEBUG))
			printf("\n");
		FR_VERBOSE(("%c", FR_ISSKIP(pass) ? 's' :
				  FR_ISPASS(pass) ? 'p' :
				  FR_ISACCOUNT(pass) ? 'A' :
				  FR_ISAUTH(pass) ? 'a' :
				  (pass & FR_NOMATCH) ? 'n' :'b'));
		if (fr->fr_ifa && fr->fr_ifa != fin->fin_ifp)
			continue;
		FR_VERBOSE((":i"));
#endif

		switch (fr->fr_type)
		{
		case FR_T_IPF :
		case FR_T_IPF_BUILTIN :
			if (ipf_check_ipf(fin, fr, portcmp))
				continue;
			break;
#if defined(IPFILTER_BPF)
		case FR_T_BPFOPC :
		case FR_T_BPFOPC_BUILTIN :
		    {
			u_char *mc;
			int wlen;

			if (*fin->fin_mp == NULL)
				continue;
			if (fin->fin_family != fr->fr_family)
				continue;
			mc = (u_char *)fin->fin_m;
			wlen = fin->fin_dlen + fin->fin_hlen;
			if (!bpf_filter(fr->fr_data, mc, wlen, 0))
				continue;
			break;
		    }
#endif
		case FR_T_CALLFUNC_BUILTIN :
		    {
			frentry_t *f;

			f = (*fr->fr_func)(fin, &pass);
			if (f != NULL)
				fr = f;
			else
				continue;
			break;
		    }

		case FR_T_IPFEXPR :
		case FR_T_IPFEXPR_BUILTIN :
			if (fin->fin_family != fr->fr_family)
				continue;
			if (ipf_fr_matcharray(fin, fr->fr_data) == 0)
				continue;
			break;

		default :
			break;
		}

		if ((fin->fin_out == 0) && (fr->fr_nattag.ipt_num[0] != 0)) {
			if (fin->fin_nattag == NULL)
				continue;
			if (ipf_matchtag(&fr->fr_nattag, fin->fin_nattag) == 0)
				continue;
		}
		FR_VERBOSE(("=%d/%d.%d *", fr->fr_grhead, fr->fr_group, rulen));

		passt = fr->fr_flags;

		/*
		 * If the rule is a "call now" rule, then call the function
		 * in the rule, if it exists and use the results from that.
		 * If the function pointer is bad, just make like we ignore
		 * it, except for increasing the hit counter.
		 */
		if ((passt & FR_CALLNOW) != 0) {
			frentry_t *frs;

			ATOMIC_INC64(fr->fr_hits);
			if ((fr->fr_func == NULL) ||
			    (fr->fr_func == (ipfunc_t)-1))
				continue;

			frs = fin->fin_fr;
			fin->fin_fr = fr;
			fr = (*fr->fr_func)(fin, &passt);
			if (fr == NULL) {
				fin->fin_fr = frs;
				continue;
			}
			passt = fr->fr_flags;
		}
		fin->fin_fr = fr;

#ifdef  IPFILTER_LOG
		/*
		 * Just log this packet...
		 */
		if ((passt & FR_LOGMASK) == FR_LOG) {
			if (ipf_log_pkt(fin, passt) == -1) {
				if (passt & FR_LOGORBLOCK) {
					DT(frb_logfail);
					passt &= ~FR_CMDMASK;
					passt |= FR_BLOCK|FR_QUICK;
					fin->fin_reason = FRB_LOGFAIL;
				}
			}
		}
#endif /* IPFILTER_LOG */

		MUTEX_ENTER(&fr->fr_lock);
		fr->fr_bytes += (U_QUAD_T)fin->fin_plen;
		fr->fr_hits++;
		MUTEX_EXIT(&fr->fr_lock);
		fin->fin_rule = rulen;

		passo = pass;
		if (FR_ISSKIP(passt)) {
			skip = fr->fr_arg;
			continue;
		} else if (((passt & FR_LOGMASK) != FR_LOG) &&
			   ((passt & FR_LOGMASK) != FR_DECAPSULATE)) {
			pass = passt;
		}

		if (passt & (FR_RETICMP|FR_FAKEICMP))
			fin->fin_icode = fr->fr_icode;

		if (fr->fr_group != -1) {
			(void) strncpy(fin->fin_group,
				       FR_NAME(fr, fr_group),
				       strlen(FR_NAME(fr, fr_group)));
		} else {
			fin->fin_group[0] = '\0';
		}

		FR_DEBUG(("pass %#x/%#x/%x\n", passo, pass, passt));

		if (fr->fr_grphead != NULL) {
			fin->fin_fr = fr->fr_grphead->fg_start;
			FR_VERBOSE(("group %s\n", FR_NAME(fr, fr_grhead)));

			if (FR_ISDECAPS(passt))
				passt = ipf_decaps(fin, pass, fr->fr_icode);
			else
				passt = ipf_scanlist(fin, pass);

			if (fin->fin_fr == NULL) {
				fin->fin_rule = rulen;
				if (fr->fr_group != -1)
					(void) strncpy(fin->fin_group,
						       fr->fr_names +
						       fr->fr_group,
						       strlen(fr->fr_names +
							      fr->fr_group));
				fin->fin_fr = fr;
				passt = pass;
			}
			pass = passt;
		}

		if (pass & FR_QUICK) {
			/*
			 * Finally, if we've asked to track state for this
			 * packet, set it up.  Add state for "quick" rules
			 * here so that if the action fails we can consider
			 * the rule to "not match" and keep on processing
			 * filter rules.
			 */
			if ((pass & FR_KEEPSTATE) && !FR_ISAUTH(pass) &&
			    !(fin->fin_flx & FI_STATE)) {
				int out = fin->fin_out;

				fin->fin_fr = fr;
				if (ipf_state_add(softc, fin, NULL, 0) == 0) {
					LBUMPD(ipf_stats[out], fr_ads);
				} else {
					LBUMPD(ipf_stats[out], fr_bads);
					pass = passo;
					continue;
				}
			}
			break;
		}
	}
	fin->fin_depth--;
	return pass;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_acctpkt                                                 */
/* Returns:     frentry_t* - always returns NULL                            */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              passp(IO) - pointer to current/new filter decision (unused) */
/*                                                                          */
/* Checks a packet against accounting rules, if there are any for the given */
/* IP protocol version.                                                     */
/*                                                                          */
/* N.B.: this function returns NULL to match the prototype used by other    */
/* functions called from the IPFilter "mainline" in ipf_check().            */
/* ------------------------------------------------------------------------ */
frentry_t *
ipf_acctpkt(fin, passp)
	fr_info_t *fin;
	u_32_t *passp;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	char group[FR_GROUPLEN];
	frentry_t *fr, *frsave;
	u_32_t pass, rulen;

	passp = passp;
	fr = softc->ipf_acct[fin->fin_out][softc->ipf_active];

	if (fr != NULL) {
		frsave = fin->fin_fr;
		bcopy(fin->fin_group, group, FR_GROUPLEN);
		rulen = fin->fin_rule;
		fin->fin_fr = fr;
		pass = ipf_scanlist(fin, FR_NOMATCH);
		if (FR_ISACCOUNT(pass)) {
			LBUMPD(ipf_stats[0], fr_acct);
		}
		fin->fin_fr = frsave;
		bcopy(group, fin->fin_group, FR_GROUPLEN);
		fin->fin_rule = rulen;
	}
	return NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_firewall                                                */
/* Returns:     frentry_t* - returns pointer to matched rule, if no matches */
/*                           were found, returns NULL.                      */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              passp(IO) - pointer to current/new filter decision (unused) */
/*                                                                          */
/* Applies an appropriate set of firewall rules to the packet, to see if    */
/* there are any matches.  The first check is to see if a match can be seen */
/* in the cache.  If not, then search an appropriate list of rules.  Once a */
/* matching rule is found, take any appropriate actions as defined by the   */
/* rule - except logging.                                                   */
/* ------------------------------------------------------------------------ */
static frentry_t *
ipf_firewall(fin, passp)
	fr_info_t *fin;
	u_32_t *passp;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	frentry_t *fr;
	u_32_t pass;
	int out;

	out = fin->fin_out;
	pass = *passp;

	/*
	 * This rule cache will only affect packets that are not being
	 * statefully filtered.
	 */
	fin->fin_fr = softc->ipf_rules[out][softc->ipf_active];
	if (fin->fin_fr != NULL)
		pass = ipf_scanlist(fin, softc->ipf_pass);

	if ((pass & FR_NOMATCH)) {
		LBUMPD(ipf_stats[out], fr_nom);
	}
	fr = fin->fin_fr;

	/*
	 * Apply packets per second rate-limiting to a rule as required.
	 */
	if ((fr != NULL) && (fr->fr_pps != 0) &&
	    !ppsratecheck(&fr->fr_lastpkt, &fr->fr_curpps, fr->fr_pps)) {
		DT2(frb_ppsrate, fr_info_t *, fin, frentry_t *, fr);
		pass &= ~(FR_CMDMASK|FR_RETICMP|FR_RETRST);
		pass |= FR_BLOCK;
		LBUMPD(ipf_stats[out], fr_ppshit);
		fin->fin_reason = FRB_PPSRATE;
	}

	/*
	 * If we fail to add a packet to the authorization queue, then we
	 * drop the packet later.  However, if it was added then pretend
	 * we've dropped it already.
	 */
	if (FR_ISAUTH(pass)) {
		if (ipf_auth_new(fin->fin_m, fin) != 0) {
			DT1(frb_authnew, fr_info_t *, fin);
			fin->fin_m = *fin->fin_mp = NULL;
			fin->fin_reason = FRB_AUTHNEW;
			fin->fin_error = 0;
		} else {
			IPFERROR(1);
			fin->fin_error = ENOSPC;
		}
	}

	if ((fr != NULL) && (fr->fr_func != NULL) &&
	    (fr->fr_func != (ipfunc_t)-1) && !(pass & FR_CALLNOW))
		(void) (*fr->fr_func)(fin, &pass);

	/*
	 * If a rule is a pre-auth rule, check again in the list of rules
	 * loaded for authenticated use.  It does not particulary matter
	 * if this search fails because a "preauth" result, from a rule,
	 * is treated as "not a pass", hence the packet is blocked.
	 */
	if (FR_ISPREAUTH(pass)) {
		pass = ipf_auth_pre_scanlist(softc, fin, pass);
	}

	/*
	 * If the rule has "keep frag" and the packet is actually a fragment,
	 * then create a fragment state entry.
	 */
	if (pass & FR_KEEPFRAG) {
		if (fin->fin_flx & FI_FRAG) {
			if (ipf_frag_new(softc, fin, pass) == -1) {
				LBUMP(ipf_stats[out].fr_bnfr);
			} else {
				LBUMP(ipf_stats[out].fr_nfr);
			}
		} else {
			LBUMP(ipf_stats[out].fr_cfr);
		}
	}

	fr = fin->fin_fr;
	*passp = pass;

	return fr;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_check                                                   */
/* Returns:     int -  0 == packet allowed through,                         */
/*              User space:                                                 */
/*                    -1 == packet blocked                                  */
/*                     1 == packet not matched                              */
/*                    -2 == requires authentication                         */
/*              Kernel:                                                     */
/*                   > 0 == filter error # for packet                       */
/* Parameters: ctx(I)  - pointer to the instance context                    */
/*             ip(I)   - pointer to start of IPv4/6 packet                  */
/*             hlen(I) - length of header                                   */
/*             ifp(I)  - pointer to interface this packet is on             */
/*             out(I)  - 0 == packet going in, 1 == packet going out        */
/*             mp(IO)  - pointer to caller's buffer pointer that holds this */
/*                       IP packet.                                         */
/* Solaris:                                                                 */
/*             qpi(I)  - pointer to STREAMS queue information for this      */
/*                       interface & direction.                             */
/*                                                                          */
/* ipf_check() is the master function for all IPFilter packet processing.   */
/* It orchestrates: Network Address Translation (NAT), checking for packet  */
/* authorisation (or pre-authorisation), presence of related state info.,   */
/* generating log entries, IP packet accounting, routing of packets as      */
/* directed by firewall rules and of course whether or not to allow the     */
/* packet to be further processed by the kernel.                            */
/*                                                                          */
/* For packets blocked, the contents of "mp" will be NULL'd and the buffer  */
/* freed.  Packets passed may be returned with the pointer pointed to by    */
/* by "mp" changed to a new buffer.                                         */
/* ------------------------------------------------------------------------ */
int
ipf_check(ctx, ip, hlen, ifp, out
#if defined(_KERNEL) && defined(MENTAT)
	, qif, mp)
	void *qif;
#else
	, mp)
#endif
	mb_t **mp;
	ip_t *ip;
	int hlen;
	void *ifp;
	int out;
	void *ctx;
{
	/*
	 * The above really sucks, but short of writing a diff
	 */
	ipf_main_softc_t *softc = ctx;
	fr_info_t frinfo;
	fr_info_t *fin = &frinfo;
	u_32_t pass = softc->ipf_pass;
	frentry_t *fr = NULL;
	int v = IP_V(ip);
	mb_t *mc = NULL;
	mb_t *m;
	/*
	 * The first part of ipf_check() deals with making sure that what goes
	 * into the filtering engine makes some sense.  Information about the
	 * the packet is distilled, collected into a fr_info_t structure and
	 * the an attempt to ensure the buffer the packet is in is big enough
	 * to hold all the required packet headers.
	 */
#ifdef	_KERNEL
# ifdef MENTAT
	qpktinfo_t *qpi = qif;

#  ifdef __sparc
	if ((u_int)ip & 0x3)
		return 2;
#  endif
# else
	SPL_INT(s);
# endif

	if (softc->ipf_running <= 0) {
		return 0;
	}

	bzero((char *)fin, sizeof(*fin));

# ifdef MENTAT
	if (qpi->qpi_flags & QF_BROADCAST)
		fin->fin_flx |= FI_MBCAST|FI_BROADCAST;
	if (qpi->qpi_flags & QF_MULTICAST)
		fin->fin_flx |= FI_MBCAST|FI_MULTICAST;
	m = qpi->qpi_m;
	fin->fin_qfm = m;
	fin->fin_qpi = qpi;
# else /* MENTAT */

	m = *mp;

#  if defined(M_MCAST)
	if ((m->m_flags & M_MCAST) != 0)
		fin->fin_flx |= FI_MBCAST|FI_MULTICAST;
#  endif
#  if defined(M_MLOOP)
	if ((m->m_flags & M_MLOOP) != 0)
		fin->fin_flx |= FI_MBCAST|FI_MULTICAST;
#  endif
#  if defined(M_BCAST)
	if ((m->m_flags & M_BCAST) != 0)
		fin->fin_flx |= FI_MBCAST|FI_BROADCAST;
#  endif
#  ifdef M_CANFASTFWD
	/*
	 * XXX For now, IP Filter and fast-forwarding of cached flows
	 * XXX are mutually exclusive.  Eventually, IP Filter should
	 * XXX get a "can-fast-forward" filter rule.
	 */
	m->m_flags &= ~M_CANFASTFWD;
#  endif /* M_CANFASTFWD */
#  if defined(CSUM_DELAY_DATA) && (!defined(__FreeBSD_version) || \
				   (__FreeBSD_version < 501108))
	/*
	 * disable delayed checksums.
	 */
	if (m->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
		in_delayed_cksum(m);
		m->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}
#  endif /* CSUM_DELAY_DATA */
# endif /* MENTAT */
#else
	bzero((char *)fin, sizeof(*fin));
	m = *mp;
# if defined(M_MCAST)
	if ((m->m_flags & M_MCAST) != 0)
		fin->fin_flx |= FI_MBCAST|FI_MULTICAST;
# endif
# if defined(M_MLOOP)
	if ((m->m_flags & M_MLOOP) != 0)
		fin->fin_flx |= FI_MBCAST|FI_MULTICAST;
# endif
# if defined(M_BCAST)
	if ((m->m_flags & M_BCAST) != 0)
		fin->fin_flx |= FI_MBCAST|FI_BROADCAST;
# endif
#endif /* _KERNEL */

	fin->fin_v = v;
	fin->fin_m = m;
	fin->fin_ip = ip;
	fin->fin_mp = mp;
	fin->fin_out = out;
	fin->fin_ifp = ifp;
	fin->fin_error = ENETUNREACH;
	fin->fin_hlen = (u_short)hlen;
	fin->fin_dp = (char *)ip + hlen;
	fin->fin_main_soft = softc;

	fin->fin_ipoff = (char *)ip - MTOD(m, char *);

	SPL_NET(s);

#ifdef	USE_INET6
	if (v == 6) {
		LBUMP(ipf_stats[out].fr_ipv6);
		/*
		 * Jumbo grams are quite likely too big for internal buffer
		 * structures to handle comfortably, for now, so just drop
		 * them.
		 */
		if (((ip6_t *)ip)->ip6_plen == 0) {
			DT1(frb_jumbo, ip6_t *, (ip6_t *)ip);
			pass = FR_BLOCK|FR_NOMATCH;
			fin->fin_reason = FRB_JUMBO;
			goto finished;
		}
		fin->fin_family = AF_INET6;
	} else
#endif
	{
		fin->fin_family = AF_INET;
	}

	if (ipf_makefrip(hlen, ip, fin) == -1) {
		DT1(frb_makefrip, fr_info_t *, fin);
		pass = FR_BLOCK|FR_NOMATCH;
		fin->fin_reason = FRB_MAKEFRIP;
		goto finished;
	}

	/*
	 * For at least IPv6 packets, if a m_pullup() fails then this pointer
	 * becomes NULL and so we have no packet to free.
	 */
	if (*fin->fin_mp == NULL)
		goto finished;

	if (!out) {
		if (v == 4) {
			if (softc->ipf_chksrc && !ipf_verifysrc(fin)) {
				LBUMPD(ipf_stats[0], fr_v4_badsrc);
				fin->fin_flx |= FI_BADSRC;
			}
			if (fin->fin_ip->ip_ttl < softc->ipf_minttl) {
				LBUMPD(ipf_stats[0], fr_v4_badttl);
				fin->fin_flx |= FI_LOWTTL;
			}
		}
#ifdef USE_INET6
		else  if (v == 6) {
			if (((ip6_t *)ip)->ip6_hlim < softc->ipf_minttl) {
				LBUMPD(ipf_stats[0], fr_v6_badttl);
				fin->fin_flx |= FI_LOWTTL;
			}
		}
#endif
	}

	if (fin->fin_flx & FI_SHORT) {
		LBUMPD(ipf_stats[out], fr_short);
	}

	READ_ENTER(&softc->ipf_mutex);

	if (!out) {
		switch (fin->fin_v)
		{
		case 4 :
			if (ipf_nat_checkin(fin, &pass) == -1) {
				goto filterdone;
			}
			break;
#ifdef USE_INET6
		case 6 :
			if (ipf_nat6_checkin(fin, &pass) == -1) {
				goto filterdone;
			}
			break;
#endif
		default :
			break;
		}
	}
	/*
	 * Check auth now.
	 * If a packet is found in the auth table, then skip checking
	 * the access lists for permission but we do need to consider
	 * the result as if it were from the ACL's.  In addition, being
	 * found in the auth table means it has been seen before, so do
	 * not pass it through accounting (again), lest it be counted twice.
	 */
	fr = ipf_auth_check(fin, &pass);
	if (!out && (fr == NULL))
		(void) ipf_acctpkt(fin, NULL);

	if (fr == NULL) {
		if ((fin->fin_flx & FI_FRAG) != 0)
			fr = ipf_frag_known(fin, &pass);

		if (fr == NULL)
			fr = ipf_state_check(fin, &pass);
	}

	if ((pass & FR_NOMATCH) || (fr == NULL))
		fr = ipf_firewall(fin, &pass);

	/*
	 * If we've asked to track state for this packet, set it up.
	 * Here rather than ipf_firewall because ipf_checkauth may decide
	 * to return a packet for "keep state"
	 */
	if ((pass & FR_KEEPSTATE) && (fin->fin_m != NULL) &&
	    !(fin->fin_flx & FI_STATE)) {
		if (ipf_state_add(softc, fin, NULL, 0) == 0) {
			LBUMP(ipf_stats[out].fr_ads);
		} else {
			LBUMP(ipf_stats[out].fr_bads);
			if (FR_ISPASS(pass)) {
				DT(frb_stateadd);
				pass &= ~FR_CMDMASK;
				pass |= FR_BLOCK;
				fin->fin_reason = FRB_STATEADD;
			}
		}
	}

	fin->fin_fr = fr;
	if ((fr != NULL) && !(fin->fin_flx & FI_STATE)) {
		fin->fin_dif = &fr->fr_dif;
		fin->fin_tif = &fr->fr_tifs[fin->fin_rev];
	}

	/*
	 * Only count/translate packets which will be passed on, out the
	 * interface.
	 */
	if (out && FR_ISPASS(pass)) {
		(void) ipf_acctpkt(fin, NULL);

		switch (fin->fin_v)
		{
		case 4 :
			if (ipf_nat_checkout(fin, &pass) == -1) {
				;
			} else if ((softc->ipf_update_ipid != 0) && (v == 4)) {
				if (ipf_updateipid(fin) == -1) {
					DT(frb_updateipid);
					LBUMP(ipf_stats[1].fr_ipud);
					pass &= ~FR_CMDMASK;
					pass |= FR_BLOCK;
					fin->fin_reason = FRB_UPDATEIPID;
				} else {
					LBUMP(ipf_stats[0].fr_ipud);
				}
			}
			break;
#ifdef USE_INET6
		case 6 :
			(void) ipf_nat6_checkout(fin, &pass);
			break;
#endif
		default :
			break;
		}
	}

filterdone:
#ifdef	IPFILTER_LOG
	if ((softc->ipf_flags & FF_LOGGING) || (pass & FR_LOGMASK)) {
		(void) ipf_dolog(fin, &pass);
	}
#endif

	/*
	 * The FI_STATE flag is cleared here so that calling ipf_state_check
	 * will work when called from inside of fr_fastroute.  Although
	 * there is a similar flag, FI_NATED, for NAT, it does have the same
	 * impact on code execution.
	 */
	fin->fin_flx &= ~FI_STATE;

#if defined(FASTROUTE_RECURSION)
	/*
	 * Up the reference on fr_lock and exit ipf_mutex. The generation of
	 * a packet below can sometimes cause a recursive call into IPFilter.
	 * On those platforms where that does happen, we need to hang onto
	 * the filter rule just in case someone decides to remove or flush it
	 * in the meantime.
	 */
	if (fr != NULL) {
		MUTEX_ENTER(&fr->fr_lock);
		fr->fr_ref++;
		MUTEX_EXIT(&fr->fr_lock);
	}

	RWLOCK_EXIT(&softc->ipf_mutex);
#endif

	if ((pass & FR_RETMASK) != 0) {
		/*
		 * Should we return an ICMP packet to indicate error
		 * status passing through the packet filter ?
		 * WARNING: ICMP error packets AND TCP RST packets should
		 * ONLY be sent in repsonse to incoming packets.  Sending
		 * them in response to outbound packets can result in a
		 * panic on some operating systems.
		 */
		if (!out) {
			if (pass & FR_RETICMP) {
				int dst;

				if ((pass & FR_RETMASK) == FR_FAKEICMP)
					dst = 1;
				else
					dst = 0;
				(void) ipf_send_icmp_err(ICMP_UNREACH, fin,
							 dst);
				LBUMP(ipf_stats[0].fr_ret);
			} else if (((pass & FR_RETMASK) == FR_RETRST) &&
				   !(fin->fin_flx & FI_SHORT)) {
				if (((fin->fin_flx & FI_OOW) != 0) ||
				    (ipf_send_reset(fin) == 0)) {
					LBUMP(ipf_stats[1].fr_ret);
				}
			}

			/*
			 * When using return-* with auth rules, the auth code
			 * takes over disposing of this packet.
			 */
			if (FR_ISAUTH(pass) && (fin->fin_m != NULL)) {
				DT1(frb_authcapture, fr_info_t *, fin);
				fin->fin_m = *fin->fin_mp = NULL;
				fin->fin_reason = FRB_AUTHCAPTURE;
				m = NULL;
			}
		} else {
			if (pass & FR_RETRST) {
				fin->fin_error = ECONNRESET;
			}
		}
	}

	/*
	 * After the above so that ICMP unreachables and TCP RSTs get
	 * created properly.
	 */
	if (FR_ISBLOCK(pass) && (fin->fin_flx & FI_NEWNAT))
		ipf_nat_uncreate(fin);

	/*
	 * If we didn't drop off the bottom of the list of rules (and thus
	 * the 'current' rule fr is not NULL), then we may have some extra
	 * instructions about what to do with a packet.
	 * Once we're finished return to our caller, freeing the packet if
	 * we are dropping it.
	 */
	if (fr != NULL) {
		frdest_t *fdp;

		/*
		 * Generate a duplicated packet first because ipf_fastroute
		 * can lead to fin_m being free'd... not good.
		 */
		fdp = fin->fin_dif;
		if ((fdp != NULL) && (fdp->fd_ptr != NULL) &&
		    (fdp->fd_ptr != (void *)-1)) {
			mc = M_COPY(fin->fin_m);
			if (mc != NULL)
				ipf_fastroute(mc, &mc, fin, fdp);
		}

		fdp = fin->fin_tif;
		if (!out && (pass & FR_FASTROUTE)) {
			/*
			 * For fastroute rule, no destination interface defined
			 * so pass NULL as the frdest_t parameter
			 */
			(void) ipf_fastroute(fin->fin_m, mp, fin, NULL);
			m = *mp = NULL;
		} else if ((fdp != NULL) && (fdp->fd_ptr != NULL) &&
			   (fdp->fd_ptr != (struct ifnet *)-1)) {
			/* this is for to rules: */
			ipf_fastroute(fin->fin_m, mp, fin, fdp);
			m = *mp = NULL;
		}

#if defined(FASTROUTE_RECURSION)
		(void) ipf_derefrule(softc, &fr);
#endif
	}
#if !defined(FASTROUTE_RECURSION)
	RWLOCK_EXIT(&softc->ipf_mutex);
#endif

finished:
	if (!FR_ISPASS(pass)) {
		LBUMP(ipf_stats[out].fr_block);
		if (*mp != NULL) {
#ifdef _KERNEL
			FREE_MB_T(*mp);
#endif
			m = *mp = NULL;
		}
	} else {
		LBUMP(ipf_stats[out].fr_pass);
	}

	SPL_X(s);

#ifdef _KERNEL
	if (FR_ISPASS(pass))
		return 0;
	LBUMP(ipf_stats[out].fr_blocked[fin->fin_reason]);
	return fin->fin_error;
#else /* _KERNEL */
	if (*mp != NULL)
		(*mp)->mb_ifp = fin->fin_ifp;
	blockreason = fin->fin_reason;
	FR_VERBOSE(("fin_flx %#x pass %#x ", fin->fin_flx, pass));
	/*if ((pass & FR_CMDMASK) == (softc->ipf_pass & FR_CMDMASK))*/
		if ((pass & FR_NOMATCH) != 0)
			return 1;

	if ((pass & FR_RETMASK) != 0)
		switch (pass & FR_RETMASK)
		{
		case FR_RETRST :
			return 3;
		case FR_RETICMP :
			return 4;
		case FR_FAKEICMP :
			return 5;
		}

	switch (pass & FR_CMDMASK)
	{
	case FR_PASS :
		return 0;
	case FR_BLOCK :
		return -1;
	case FR_AUTH :
		return -2;
	case FR_ACCOUNT :
		return -3;
	case FR_PREAUTH :
		return -4;
	}
	return 2;
#endif /* _KERNEL */
}


#ifdef	IPFILTER_LOG
/* ------------------------------------------------------------------------ */
/* Function:    ipf_dolog                                                   */
/* Returns:     frentry_t* - returns contents of fin_fr (no change made)    */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              passp(IO) - pointer to current/new filter decision (unused) */
/*                                                                          */
/* Checks flags set to see how a packet should be logged, if it is to be    */
/* logged.  Adjust statistics based on its success or not.                  */
/* ------------------------------------------------------------------------ */
frentry_t *
ipf_dolog(fin, passp)
	fr_info_t *fin;
	u_32_t *passp;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	u_32_t pass;
	int out;

	out = fin->fin_out;
	pass = *passp;

	if ((softc->ipf_flags & FF_LOGNOMATCH) && (pass & FR_NOMATCH)) {
		pass |= FF_LOGNOMATCH;
		LBUMPD(ipf_stats[out], fr_npkl);
		goto logit;

	} else if (((pass & FR_LOGMASK) == FR_LOGP) ||
	    (FR_ISPASS(pass) && (softc->ipf_flags & FF_LOGPASS))) {
		if ((pass & FR_LOGMASK) != FR_LOGP)
			pass |= FF_LOGPASS;
		LBUMPD(ipf_stats[out], fr_ppkl);
		goto logit;

	} else if (((pass & FR_LOGMASK) == FR_LOGB) ||
		   (FR_ISBLOCK(pass) && (softc->ipf_flags & FF_LOGBLOCK))) {
		if ((pass & FR_LOGMASK) != FR_LOGB)
			pass |= FF_LOGBLOCK;
		LBUMPD(ipf_stats[out], fr_bpkl);

logit:
		if (ipf_log_pkt(fin, pass) == -1) {
			/*
			 * If the "or-block" option has been used then
			 * block the packet if we failed to log it.
			 */
			if ((pass & FR_LOGORBLOCK) && FR_ISPASS(pass)) {
				DT1(frb_logfail2, u_int, pass);
				pass &= ~FR_CMDMASK;
				pass |= FR_BLOCK;
				fin->fin_reason = FRB_LOGFAIL2;
			}
		}
		*passp = pass;
	}

	return fin->fin_fr;
}
#endif /* IPFILTER_LOG */


/* ------------------------------------------------------------------------ */
/* Function:    ipf_cksum                                                   */
/* Returns:     u_short - IP header checksum                                */
/* Parameters:  addr(I) - pointer to start of buffer to checksum            */
/*              len(I)  - length of buffer in bytes                         */
/*                                                                          */
/* Calculate the two's complement 16 bit checksum of the buffer passed.     */
/*                                                                          */
/* N.B.: addr should be 16bit aligned.                                      */
/* ------------------------------------------------------------------------ */
u_short
ipf_cksum(addr, len)
	u_short *addr;
	int len;
{
	u_32_t sum = 0;

	for (sum = 0; len > 1; len -= 2)
		sum += *addr++;

	/* mop up an odd byte, if necessary */
	if (len == 1)
		sum += *(u_char *)addr;

	/*
	 * add back carry outs from top 16 bits to low 16 bits
	 */
	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);			/* add carry */
	return (u_short)(~sum);
}


/* ------------------------------------------------------------------------ */
/* Function:    fr_cksum                                                    */
/* Returns:     u_short - layer 4 checksum                                  */
/* Parameters:  fin(I)     - pointer to packet information                  */
/*              ip(I)      - pointer to IP header                           */
/*              l4proto(I) - protocol to caclulate checksum for             */
/*              l4hdr(I)   - pointer to layer 4 header                      */
/*                                                                          */
/* Calculates the TCP checksum for the packet held in "m", using the data   */
/* in the IP header "ip" to seed it.                                        */
/*                                                                          */
/* NB: This function assumes we've pullup'd enough for all of the IP header */
/* and the TCP header.  We also assume that data blocks aren't allocated in */
/* odd sizes.                                                               */
/*                                                                          */
/* Expects ip_len and ip_off to be in network byte order when called.       */
/* ------------------------------------------------------------------------ */
u_short
fr_cksum(fin, ip, l4proto, l4hdr)
	fr_info_t *fin;
	ip_t *ip;
	int l4proto;
	void *l4hdr;
{
	u_short *sp, slen, sumsave, *csump;
	u_int sum, sum2;
	int hlen;
	int off;
#ifdef	USE_INET6
	ip6_t *ip6;
#endif

	csump = NULL;
	sumsave = 0;
	sp = NULL;
	slen = 0;
	hlen = 0;
	sum = 0;

	sum = htons((u_short)l4proto);
	/*
	 * Add up IP Header portion
	 */
#ifdef	USE_INET6
	if (IP_V(ip) == 4) {
#endif
		hlen = IP_HL(ip) << 2;
		off = hlen;
		sp = (u_short *)&ip->ip_src;
		sum += *sp++;	/* ip_src */
		sum += *sp++;
		sum += *sp++;	/* ip_dst */
		sum += *sp++;
#ifdef	USE_INET6
	} else if (IP_V(ip) == 6) {
		ip6 = (ip6_t *)ip;
		hlen = sizeof(*ip6);
		off = ((char *)fin->fin_dp - (char *)fin->fin_ip);
		sp = (u_short *)&ip6->ip6_src;
		sum += *sp++;	/* ip6_src */
		sum += *sp++;
		sum += *sp++;
		sum += *sp++;
		sum += *sp++;
		sum += *sp++;
		sum += *sp++;
		sum += *sp++;
		/* This needs to be routing header aware. */
		sum += *sp++;	/* ip6_dst */
		sum += *sp++;
		sum += *sp++;
		sum += *sp++;
		sum += *sp++;
		sum += *sp++;
		sum += *sp++;
		sum += *sp++;
	} else {
		return 0xffff;
	}
#endif
	slen = fin->fin_plen - off;
	sum += htons(slen);

	switch (l4proto)
	{
	case IPPROTO_UDP :
		csump = &((udphdr_t *)l4hdr)->uh_sum;
		break;

	case IPPROTO_TCP :
		csump = &((tcphdr_t *)l4hdr)->th_sum;
		break;
	case IPPROTO_ICMP :
		csump = &((icmphdr_t *)l4hdr)->icmp_cksum;
		sum = 0;	/* Pseudo-checksum is not included */
		break;
#ifdef USE_INET6
	case IPPROTO_ICMPV6 :
		csump = &((struct icmp6_hdr *)l4hdr)->icmp6_cksum;
		break;
#endif
	default :
		break;
	}

	if (csump != NULL) {
		sumsave = *csump;
		*csump = 0;
	}

	sum2 = ipf_pcksum(fin, off, sum);
	if (csump != NULL)
		*csump = sumsave;
	return sum2;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_findgroup                                               */
/* Returns:     frgroup_t * - NULL = group not found, else pointer to group */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              group(I) - group name to search for                         */
/*              unit(I)  - device to which this group belongs               */
/*              set(I)   - which set of rules (inactive/inactive) this is   */
/*              fgpp(O)  - pointer to place to store pointer to the pointer */
/*                         to where to add the next (last) group or where   */
/*                         to delete group from.                            */
/*                                                                          */
/* Search amongst the defined groups for a particular group number.         */
/* ------------------------------------------------------------------------ */
frgroup_t *
ipf_findgroup(softc, group, unit, set, fgpp)
	ipf_main_softc_t *softc;
	char *group;
	minor_t unit;
	int set;
	frgroup_t ***fgpp;
{
	frgroup_t *fg, **fgp;

	/*
	 * Which list of groups to search in is dependent on which list of
	 * rules are being operated on.
	 */
	fgp = &softc->ipf_groups[unit][set];

	while ((fg = *fgp) != NULL) {
		if (strncmp(group, fg->fg_name, FR_GROUPLEN) == 0)
			break;
		else
			fgp = &fg->fg_next;
	}
	if (fgpp != NULL)
		*fgpp = fgp;
	return fg;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_group_add                                               */
/* Returns:     frgroup_t * - NULL == did not create group,                 */
/*                            != NULL == pointer to the group               */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              num(I)   - group number to add                              */
/*              head(I)  - rule pointer that is using this as the head      */
/*              flags(I) - rule flags which describe the type of rule it is */
/*              unit(I)  - device to which this group will belong to        */
/*              set(I)   - which set of rules (inactive/inactive) this is   */
/* Write Locks: ipf_mutex                                                   */
/*                                                                          */
/* Add a new group head, or if it already exists, increase the reference    */
/* count to it.                                                             */
/* ------------------------------------------------------------------------ */
frgroup_t *
ipf_group_add(softc, group, head, flags, unit, set)
	ipf_main_softc_t *softc;
	char *group;
	void *head;
	u_32_t flags;
	minor_t unit;
	int set;
{
	frgroup_t *fg, **fgp;
	u_32_t gflags;

	if (group == NULL)
		return NULL;

	if (unit == IPL_LOGIPF && *group == '\0')
		return NULL;

	fgp = NULL;
	gflags = flags & FR_INOUT;

	fg = ipf_findgroup(softc, group, unit, set, &fgp);
	if (fg != NULL) {
		if (fg->fg_head == NULL && head != NULL)
			fg->fg_head = head;
		if (fg->fg_flags == 0)
			fg->fg_flags = gflags;
		else if (gflags != fg->fg_flags)
			return NULL;
		fg->fg_ref++;
		return fg;
	}

	KMALLOC(fg, frgroup_t *);
	if (fg != NULL) {
		fg->fg_head = head;
		fg->fg_start = NULL;
		fg->fg_next = *fgp;
		bcopy(group, fg->fg_name, strlen(group) + 1);
		fg->fg_flags = gflags;
		fg->fg_ref = 1;
		fg->fg_set = &softc->ipf_groups[unit][set];
		*fgp = fg;
	}
	return fg;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_group_del                                               */
/* Returns:     int      - number of rules deleted                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              group(I) - group name to delete                             */
/*              fr(I)    - filter rule from which group is referenced       */
/* Write Locks: ipf_mutex                                                   */
/*                                                                          */
/* This function is called whenever a reference to a group is to be dropped */
/* and thus its reference count needs to be lowered and the group free'd if */
/* the reference count reaches zero. Passing in fr is really for the sole   */
/* purpose of knowing when the head rule is being deleted.                  */
/* ------------------------------------------------------------------------ */
void
ipf_group_del(softc, group, fr)
	ipf_main_softc_t *softc;
	frgroup_t *group;
	frentry_t *fr;
{

	if (group->fg_head == fr)
		group->fg_head = NULL;

	group->fg_ref--;
	if ((group->fg_ref == 0) && (group->fg_start == NULL))
		ipf_group_free(group);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_group_free                                              */
/* Returns:     Nil                                                         */
/* Parameters:  group(I) - pointer to filter rule group                     */
/*                                                                          */
/* Remove the group from the list of groups and free it.                    */
/* ------------------------------------------------------------------------ */
static void
ipf_group_free(group)
	frgroup_t *group;
{
	frgroup_t **gp;

	for (gp = group->fg_set; *gp != NULL; gp = &(*gp)->fg_next) {
		if (*gp == group) {
			*gp = group->fg_next;
			break;
		}
	}
	KFREE(group);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_group_flush                                             */
/* Returns:     int      - number of rules flush from group                 */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/* Parameters:  group(I) - pointer to filter rule group                     */
/*                                                                          */
/* Remove all of the rules that currently are listed under the given group. */
/* ------------------------------------------------------------------------ */
static int
ipf_group_flush(softc, group)
	ipf_main_softc_t *softc;
	frgroup_t *group;
{
	int gone = 0;

	(void) ipf_flushlist(softc, &gone, &group->fg_start);

	return gone;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_getrulen                                                */
/* Returns:     frentry_t * - NULL == not found, else pointer to rule n     */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/* Parameters:  unit(I)  - device for which to count the rule's number      */
/*              flags(I) - which set of rules to find the rule in           */
/*              group(I) - group name                                       */
/*              n(I)     - rule number to find                              */
/*                                                                          */
/* Find rule # n in group # g and return a pointer to it.  Return NULl if   */
/* group # g doesn't exist or there are less than n rules in the group.     */
/* ------------------------------------------------------------------------ */
frentry_t *
ipf_getrulen(softc, unit, group, n)
	ipf_main_softc_t *softc;
	int unit;
	char *group;
	u_32_t n;
{
	frentry_t *fr;
	frgroup_t *fg;

	fg = ipf_findgroup(softc, group, unit, softc->ipf_active, NULL);
	if (fg == NULL)
		return NULL;
	for (fr = fg->fg_start; fr && n; fr = fr->fr_next, n--)
		;
	if (n != 0)
		return NULL;
	return fr;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_flushlist                                               */
/* Returns:     int - >= 0 - number of flushed rules                        */
/* Parameters:  softc(I)   - pointer to soft context main structure         */
/*              nfreedp(O) - pointer to int where flush count is stored     */
/*              listp(I)   - pointer to list to flush pointer               */
/* Write Locks: ipf_mutex                                                   */
/*                                                                          */
/* Recursively flush rules from the list, descending groups as they are     */
/* encountered.  if a rule is the head of a group and it has lost all its   */
/* group members, then also delete the group reference.  nfreedp is needed  */
/* to store the accumulating count of rules removed, whereas the returned   */
/* value is just the number removed from the current list.  The latter is   */
/* needed to correctly adjust reference counts on rules that define groups. */
/*                                                                          */
/* NOTE: Rules not loaded from user space cannot be flushed.                */
/* ------------------------------------------------------------------------ */
static int
ipf_flushlist(softc, nfreedp, listp)
	ipf_main_softc_t *softc;
	int *nfreedp;
	frentry_t **listp;
{
	int freed = 0;
	frentry_t *fp;

	while ((fp = *listp) != NULL) {
		if ((fp->fr_type & FR_T_BUILTIN) ||
		    !(fp->fr_flags & FR_COPIED)) {
			listp = &fp->fr_next;
			continue;
		}
		*listp = fp->fr_next;
		if (fp->fr_next != NULL)
			fp->fr_next->fr_pnext = fp->fr_pnext;
		fp->fr_pnext = NULL;

		if (fp->fr_grphead != NULL) {
			freed += ipf_group_flush(softc, fp->fr_grphead);
			fp->fr_names[fp->fr_grhead] = '\0';
		}

		if (fp->fr_icmpgrp != NULL) {
			freed += ipf_group_flush(softc, fp->fr_icmpgrp);
			fp->fr_names[fp->fr_icmphead] = '\0';
		}

		if (fp->fr_srctrack.ht_max_nodes)
			ipf_rb_ht_flush(&fp->fr_srctrack);

		fp->fr_next = NULL;

		ASSERT(fp->fr_ref > 0);
		if (ipf_derefrule(softc, &fp) == 0)
			freed++;
	}
	*nfreedp += freed;
	return freed;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_flush                                                   */
/* Returns:     int - >= 0 - number of flushed rules                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              unit(I)  - device for which to flush rules                  */
/*              flags(I) - which set of rules to flush                      */
/*                                                                          */
/* Calls flushlist() for all filter rules (accounting, firewall - both IPv4 */
/* and IPv6) as defined by the value of flags.                              */
/* ------------------------------------------------------------------------ */
int
ipf_flush(softc, unit, flags)
	ipf_main_softc_t *softc;
	minor_t unit;
	int flags;
{
	int flushed = 0, set;

	WRITE_ENTER(&softc->ipf_mutex);

	set = softc->ipf_active;
	if ((flags & FR_INACTIVE) == FR_INACTIVE)
		set = 1 - set;

	if (flags & FR_OUTQUE) {
		ipf_flushlist(softc, &flushed, &softc->ipf_rules[1][set]);
		ipf_flushlist(softc, &flushed, &softc->ipf_acct[1][set]);
	}
	if (flags & FR_INQUE) {
		ipf_flushlist(softc, &flushed, &softc->ipf_rules[0][set]);
		ipf_flushlist(softc, &flushed, &softc->ipf_acct[0][set]);
	}

	flushed += ipf_flush_groups(softc, &softc->ipf_groups[unit][set],
				    flags & (FR_INQUE|FR_OUTQUE));

	RWLOCK_EXIT(&softc->ipf_mutex);

	if (unit == IPL_LOGIPF) {
		int tmp;

		tmp = ipf_flush(softc, IPL_LOGCOUNT, flags);
		if (tmp >= 0)
			flushed += tmp;
	}
	return flushed;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_flush_groups                                            */
/* Returns:     int - >= 0 - number of flushed rules                        */
/* Parameters:  softc(I)  - soft context pointerto work with                */
/*              grhead(I) - pointer to the start of the group list to flush */
/*              flags(I)  - which set of rules to flush                     */
/*                                                                          */
/* Walk through all of the groups under the given group head and remove all */
/* of those that match the flags passed in. The for loop here is bit more   */
/* complicated than usual because the removal of a rule with ipf_derefrule  */
/* may end up removing not only the structure pointed to by "fg" but also   */
/* what is fg_next and fg_next after that. So if a filter rule is actually  */
/* removed from the group then it is necessary to start again.              */
/* ------------------------------------------------------------------------ */
static int
ipf_flush_groups(softc, grhead, flags)
	ipf_main_softc_t *softc;
	frgroup_t **grhead;
	int flags;
{
	frentry_t *fr, **frp;
	frgroup_t *fg, **fgp;
	int flushed = 0;
	int removed = 0;

	for (fgp = grhead; (fg = *fgp) != NULL; ) {
		while ((fg != NULL) && ((fg->fg_flags & flags) == 0))
			fg = fg->fg_next;
		if (fg == NULL)
			break;
		removed = 0;
		frp = &fg->fg_start;
		while ((removed == 0) && ((fr = *frp) != NULL)) {
			if ((fr->fr_flags & flags) == 0) {
				frp = &fr->fr_next;
			} else {
				if (fr->fr_next != NULL)
					fr->fr_next->fr_pnext = fr->fr_pnext;
				*frp = fr->fr_next;
				fr->fr_pnext = NULL;
				fr->fr_next = NULL;
				(void) ipf_derefrule(softc, &fr);
				flushed++;
				removed++;
			}
		}
		if (removed == 0)
			fgp = &fg->fg_next;
	}
	return flushed;
}


/* ------------------------------------------------------------------------ */
/* Function:    memstr                                                      */
/* Returns:     char *  - NULL if failed, != NULL pointer to matching bytes */
/* Parameters:  src(I)  - pointer to byte sequence to match                 */
/*              dst(I)  - pointer to byte sequence to search                */
/*              slen(I) - match length                                      */
/*              dlen(I) - length available to search in                     */
/*                                                                          */
/* Search dst for a sequence of bytes matching those at src and extend for  */
/* slen bytes.                                                              */
/* ------------------------------------------------------------------------ */
char *
memstr(src, dst, slen, dlen)
	const char *src;
	char *dst;
	size_t slen, dlen;
{
	char *s = NULL;

	while (dlen >= slen) {
		if (bcmp(src, dst, slen) == 0) {
			s = dst;
			break;
		}
		dst++;
		dlen--;
	}
	return s;
}
/* ------------------------------------------------------------------------ */
/* Function:    ipf_fixskip                                                 */
/* Returns:     Nil                                                         */
/* Parameters:  listp(IO)    - pointer to start of list with skip rule      */
/*              rp(I)        - rule added/removed with skip in it.          */
/*              addremove(I) - adjustment (-1/+1) to make to skip count,    */
/*                             depending on whether a rule was just added   */
/*                             or removed.                                  */
/*                                                                          */
/* Adjust all the rules in a list which would have skip'd past the position */
/* where we are inserting to skip to the right place given the change.      */
/* ------------------------------------------------------------------------ */
void
ipf_fixskip(listp, rp, addremove)
	frentry_t **listp, *rp;
	int addremove;
{
	int rules, rn;
	frentry_t *fp;

	rules = 0;
	for (fp = *listp; (fp != NULL) && (fp != rp); fp = fp->fr_next)
		rules++;

	if (!fp)
		return;

	for (rn = 0, fp = *listp; fp && (fp != rp); fp = fp->fr_next, rn++)
		if (FR_ISSKIP(fp->fr_flags) && (rn + fp->fr_arg >= rules))
			fp->fr_arg += addremove;
}


#ifdef	_KERNEL
/* ------------------------------------------------------------------------ */
/* Function:    count4bits                                                  */
/* Returns:     int - >= 0 - number of consecutive bits in input            */
/* Parameters:  ip(I) - 32bit IP address                                    */
/*                                                                          */
/* IPv4 ONLY                                                                */
/* count consecutive 1's in bit mask.  If the mask generated by counting    */
/* consecutive 1's is different to that passed, return -1, else return #    */
/* of bits.                                                                 */
/* ------------------------------------------------------------------------ */
int
count4bits(ip)
	u_32_t	ip;
{
	u_32_t	ipn;
	int	cnt = 0, i, j;

	ip = ipn = ntohl(ip);
	for (i = 32; i; i--, ipn *= 2)
		if (ipn & 0x80000000)
			cnt++;
		else
			break;
	ipn = 0;
	for (i = 32, j = cnt; i; i--, j--) {
		ipn *= 2;
		if (j > 0)
			ipn++;
	}
	if (ipn == ip)
		return cnt;
	return -1;
}


/* ------------------------------------------------------------------------ */
/* Function:    count6bits                                                  */
/* Returns:     int - >= 0 - number of consecutive bits in input            */
/* Parameters:  msk(I) - pointer to start of IPv6 bitmask                   */
/*                                                                          */
/* IPv6 ONLY                                                                */
/* count consecutive 1's in bit mask.                                       */
/* ------------------------------------------------------------------------ */
# ifdef USE_INET6
int
count6bits(msk)
	u_32_t *msk;
{
	int i = 0, k;
	u_32_t j;

	for (k = 3; k >= 0; k--)
		if (msk[k] == 0xffffffff)
			i += 32;
		else {
			for (j = msk[k]; j; j <<= 1)
				if (j & 0x80000000)
					i++;
		}
	return i;
}
# endif
#endif /* _KERNEL */


/* ------------------------------------------------------------------------ */
/* Function:    ipf_synclist                                                */
/* Returns:     int    - 0 = no failures, else indication of first failure  */
/* Parameters:  fr(I)  - start of filter list to sync interface names for   */
/*              ifp(I) - interface pointer for limiting sync lookups        */
/* Write Locks: ipf_mutex                                                   */
/*                                                                          */
/* Walk through a list of filter rules and resolve any interface names into */
/* pointers.  Where dynamic addresses are used, also update the IP address  */
/* used in the rule.  The interface pointer is used to limit the lookups to */
/* a specific set of matching names if it is non-NULL.                      */
/* Errors can occur when resolving the destination name of to/dup-to fields */
/* when the name points to a pool and that pool doest not exist. If this    */
/* does happen then it is necessary to check if there are any lookup refs   */
/* that need to be dropped before returning with an error.                  */
/* ------------------------------------------------------------------------ */
static int
ipf_synclist(softc, fr, ifp)
	ipf_main_softc_t *softc;
	frentry_t *fr;
	void *ifp;
{
	frentry_t *frt, *start = fr;
	frdest_t *fdp;
	char *name;
	int error;
	void *ifa;
	int v, i;

	error = 0;

	for (; fr; fr = fr->fr_next) {
		if (fr->fr_family == AF_INET)
			v = 4;
		else if (fr->fr_family == AF_INET6)
			v = 6;
		else
			v = 0;

		/*
		 * Lookup all the interface names that are part of the rule.
		 */
		for (i = 0; i < 4; i++) {
			if ((ifp != NULL) && (fr->fr_ifas[i] != ifp))
				continue;
			if (fr->fr_ifnames[i] == -1)
				continue;
			name = FR_NAME(fr, fr_ifnames[i]);
			fr->fr_ifas[i] = ipf_resolvenic(softc, name, v);
		}

		if ((fr->fr_type & ~FR_T_BUILTIN) == FR_T_IPF) {
			if (fr->fr_satype != FRI_NORMAL &&
			    fr->fr_satype != FRI_LOOKUP) {
				ifa = ipf_resolvenic(softc, fr->fr_names +
						     fr->fr_sifpidx, v);
				ipf_ifpaddr(softc, v, fr->fr_satype, ifa,
					    &fr->fr_src6, &fr->fr_smsk6);
			}
			if (fr->fr_datype != FRI_NORMAL &&
			    fr->fr_datype != FRI_LOOKUP) {
				ifa = ipf_resolvenic(softc, fr->fr_names +
						     fr->fr_sifpidx, v);
				ipf_ifpaddr(softc, v, fr->fr_datype, ifa,
					    &fr->fr_dst6, &fr->fr_dmsk6);
			}
		}

		fdp = &fr->fr_tifs[0];
		if ((ifp == NULL) || (fdp->fd_ptr == ifp)) {
			error = ipf_resolvedest(softc, fr->fr_names, fdp, v);
			if (error != 0)
				goto unwind;
		}

		fdp = &fr->fr_tifs[1];
		if ((ifp == NULL) || (fdp->fd_ptr == ifp)) {
			error = ipf_resolvedest(softc, fr->fr_names, fdp, v);
			if (error != 0)
				goto unwind;
		}

		fdp = &fr->fr_dif;
		if ((ifp == NULL) || (fdp->fd_ptr == ifp)) {
			error = ipf_resolvedest(softc, fr->fr_names, fdp, v);
			if (error != 0)
				goto unwind;
		}

		if (((fr->fr_type & ~FR_T_BUILTIN) == FR_T_IPF) &&
		    (fr->fr_satype == FRI_LOOKUP) && (fr->fr_srcptr == NULL)) {
			fr->fr_srcptr = ipf_lookup_res_num(softc,
							   fr->fr_srctype,
							   IPL_LOGIPF,
							   fr->fr_srcnum,
							   &fr->fr_srcfunc);
		}
		if (((fr->fr_type & ~FR_T_BUILTIN) == FR_T_IPF) &&
		    (fr->fr_datype == FRI_LOOKUP) && (fr->fr_dstptr == NULL)) {
			fr->fr_dstptr = ipf_lookup_res_num(softc,
							   fr->fr_dsttype,
							   IPL_LOGIPF,
							   fr->fr_dstnum,
							   &fr->fr_dstfunc);
		}
	}
	return 0;

unwind:
	for (frt = start; frt != fr; fr = fr->fr_next) {
		if (((frt->fr_type & ~FR_T_BUILTIN) == FR_T_IPF) &&
		    (frt->fr_satype == FRI_LOOKUP) && (frt->fr_srcptr != NULL))
				ipf_lookup_deref(softc, frt->fr_srctype,
						 frt->fr_srcptr);
		if (((frt->fr_type & ~FR_T_BUILTIN) == FR_T_IPF) &&
		    (frt->fr_datype == FRI_LOOKUP) && (frt->fr_dstptr != NULL))
				ipf_lookup_deref(softc, frt->fr_dsttype,
						 frt->fr_dstptr);
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_sync                                                    */
/* Returns:     void                                                        */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* ipf_sync() is called when we suspect that the interface list or          */
/* information about interfaces (like IP#) has changed.  Go through all     */
/* filter rules, NAT entries and the state table and check if anything      */
/* needs to be changed/updated.                                             */
/* ------------------------------------------------------------------------ */
int
ipf_sync(softc, ifp)
	ipf_main_softc_t *softc;
	void *ifp;
{
	int i;

# if !SOLARIS
	ipf_nat_sync(softc, ifp);
	ipf_state_sync(softc, ifp);
	ipf_lookup_sync(softc, ifp);
# endif

	WRITE_ENTER(&softc->ipf_mutex);
	(void) ipf_synclist(softc, softc->ipf_acct[0][softc->ipf_active], ifp);
	(void) ipf_synclist(softc, softc->ipf_acct[1][softc->ipf_active], ifp);
	(void) ipf_synclist(softc, softc->ipf_rules[0][softc->ipf_active], ifp);
	(void) ipf_synclist(softc, softc->ipf_rules[1][softc->ipf_active], ifp);

	for (i = 0; i < IPL_LOGSIZE; i++) {
		frgroup_t *g;

		for (g = softc->ipf_groups[i][0]; g != NULL; g = g->fg_next)
			(void) ipf_synclist(softc, g->fg_start, ifp);
		for (g = softc->ipf_groups[i][1]; g != NULL; g = g->fg_next)
			(void) ipf_synclist(softc, g->fg_start, ifp);
	}
	RWLOCK_EXIT(&softc->ipf_mutex);

	return 0;
}


/*
 * In the functions below, bcopy() is called because the pointer being
 * copied _from_ in this instance is a pointer to a char buf (which could
 * end up being unaligned) and on the kernel's local stack.
 */
/* ------------------------------------------------------------------------ */
/* Function:    copyinptr                                                   */
/* Returns:     int - 0 = success, else failure                             */
/* Parameters:  src(I)  - pointer to the source address                     */
/*              dst(I)  - destination address                               */
/*              size(I) - number of bytes to copy                           */
/*                                                                          */
/* Copy a block of data in from user space, given a pointer to the pointer  */
/* to start copying from (src) and a pointer to where to store it (dst).    */
/* NB: src - pointer to user space pointer, dst - kernel space pointer      */
/* ------------------------------------------------------------------------ */
int
copyinptr(softc, src, dst, size)
	ipf_main_softc_t *softc;
	void *src, *dst;
	size_t size;
{
	caddr_t ca;
	int error;

# if SOLARIS
	error = COPYIN(src, &ca, sizeof(ca));
	if (error != 0)
		return error;
# else
	bcopy(src, (caddr_t)&ca, sizeof(ca));
# endif
	error = COPYIN(ca, dst, size);
	if (error != 0) {
		IPFERROR(3);
		error = EFAULT;
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    copyoutptr                                                  */
/* Returns:     int - 0 = success, else failure                             */
/* Parameters:  src(I)  - pointer to the source address                     */
/*              dst(I)  - destination address                               */
/*              size(I) - number of bytes to copy                           */
/*                                                                          */
/* Copy a block of data out to user space, given a pointer to the pointer   */
/* to start copying from (src) and a pointer to where to store it (dst).    */
/* NB: src - kernel space pointer, dst - pointer to user space pointer.     */
/* ------------------------------------------------------------------------ */
int
copyoutptr(softc, src, dst, size)
	ipf_main_softc_t *softc;
	void *src, *dst;
	size_t size;
{
	caddr_t ca;
	int error;

	bcopy(dst, (caddr_t)&ca, sizeof(ca));
	error = COPYOUT(src, ca, size);
	if (error != 0) {
		IPFERROR(4);
		error = EFAULT;
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lock                                                    */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  data(I)  - pointer to lock value to set                     */
/*              lockp(O) - pointer to location to store old lock value      */
/*                                                                          */
/* Get the new value for the lock integer, set it and return the old value  */
/* in *lockp.                                                               */
/* ------------------------------------------------------------------------ */
int
ipf_lock(data, lockp)
	caddr_t data;
	int *lockp;
{
	int arg, err;

	err = BCOPYIN(data, &arg, sizeof(arg));
	if (err != 0)
		return EFAULT;
	err = BCOPYOUT(lockp, data, sizeof(*lockp));
	if (err != 0)
		return EFAULT;
	*lockp = arg;
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_getstat                                                 */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              fiop(I)  - pointer to ipfilter stats structure              */
/*              rev(I)   - version claim by program doing ioctl             */
/*                                                                          */
/* Stores a copy of current pointers, counters, etc, in the friostat        */
/* structure.                                                               */
/* If IPFILTER_COMPAT is compiled, we pretend to be whatever version the    */
/* program is looking for. This ensure that validation of the version it    */
/* expects will always succeed. Thus kernels with IPFILTER_COMPAT will      */
/* allow older binaries to work but kernels without it will not.            */
/* ------------------------------------------------------------------------ */
/*ARGSUSED*/
static void
ipf_getstat(softc, fiop, rev)
	ipf_main_softc_t *softc;
	friostat_t *fiop;
	int rev;
{
	int i;

	bcopy((char *)softc->ipf_stats, (char *)fiop->f_st,
	      sizeof(ipf_statistics_t) * 2);
	fiop->f_locks[IPL_LOGSTATE] = -1;
	fiop->f_locks[IPL_LOGNAT] = -1;
	fiop->f_locks[IPL_LOGIPF] = -1;
	fiop->f_locks[IPL_LOGAUTH] = -1;

	fiop->f_ipf[0][0] = softc->ipf_rules[0][0];
	fiop->f_acct[0][0] = softc->ipf_acct[0][0];
	fiop->f_ipf[0][1] = softc->ipf_rules[0][1];
	fiop->f_acct[0][1] = softc->ipf_acct[0][1];
	fiop->f_ipf[1][0] = softc->ipf_rules[1][0];
	fiop->f_acct[1][0] = softc->ipf_acct[1][0];
	fiop->f_ipf[1][1] = softc->ipf_rules[1][1];
	fiop->f_acct[1][1] = softc->ipf_acct[1][1];

	fiop->f_ticks = softc->ipf_ticks;
	fiop->f_active = softc->ipf_active;
	fiop->f_froute[0] = softc->ipf_frouteok[0];
	fiop->f_froute[1] = softc->ipf_frouteok[1];
	fiop->f_rb_no_mem = softc->ipf_rb_no_mem;
	fiop->f_rb_node_max = softc->ipf_rb_node_max;

	fiop->f_running = softc->ipf_running;
	for (i = 0; i < IPL_LOGSIZE; i++) {
		fiop->f_groups[i][0] = softc->ipf_groups[i][0];
		fiop->f_groups[i][1] = softc->ipf_groups[i][1];
	}
#ifdef  IPFILTER_LOG
	fiop->f_log_ok = ipf_log_logok(softc, IPL_LOGIPF);
	fiop->f_log_fail = ipf_log_failures(softc, IPL_LOGIPF);
	fiop->f_logging = 1;
#else
	fiop->f_log_ok = 0;
	fiop->f_log_fail = 0;
	fiop->f_logging = 0;
#endif
	fiop->f_defpass = softc->ipf_pass;
	fiop->f_features = ipf_features;

#ifdef IPFILTER_COMPAT
	sprintf(fiop->f_version, "IP Filter: v%d.%d.%d",
		(rev / 1000000) % 100,
		(rev / 10000) % 100,
		(rev / 100) % 100);
#else
	rev = rev;
	(void) strncpy(fiop->f_version, ipfilter_version,
		       sizeof(fiop->f_version));
#endif
}


#ifdef	USE_INET6
int icmptoicmp6types[ICMP_MAXTYPE+1] = {
	ICMP6_ECHO_REPLY,	/* 0: ICMP_ECHOREPLY */
	-1,			/* 1: UNUSED */
	-1,			/* 2: UNUSED */
	ICMP6_DST_UNREACH,	/* 3: ICMP_UNREACH */
	-1,			/* 4: ICMP_SOURCEQUENCH */
	ND_REDIRECT,		/* 5: ICMP_REDIRECT */
	-1,			/* 6: UNUSED */
	-1,			/* 7: UNUSED */
	ICMP6_ECHO_REQUEST,	/* 8: ICMP_ECHO */
	-1,			/* 9: UNUSED */
	-1,			/* 10: UNUSED */
	ICMP6_TIME_EXCEEDED,	/* 11: ICMP_TIMXCEED */
	ICMP6_PARAM_PROB,	/* 12: ICMP_PARAMPROB */
	-1,			/* 13: ICMP_TSTAMP */
	-1,			/* 14: ICMP_TSTAMPREPLY */
	-1,			/* 15: ICMP_IREQ */
	-1,			/* 16: ICMP_IREQREPLY */
	-1,			/* 17: ICMP_MASKREQ */
	-1,			/* 18: ICMP_MASKREPLY */
};


int	icmptoicmp6unreach[ICMP_MAX_UNREACH] = {
	ICMP6_DST_UNREACH_ADDR,		/* 0: ICMP_UNREACH_NET */
	ICMP6_DST_UNREACH_ADDR,		/* 1: ICMP_UNREACH_HOST */
	-1,				/* 2: ICMP_UNREACH_PROTOCOL */
	ICMP6_DST_UNREACH_NOPORT,	/* 3: ICMP_UNREACH_PORT */
	-1,				/* 4: ICMP_UNREACH_NEEDFRAG */
	ICMP6_DST_UNREACH_NOTNEIGHBOR,	/* 5: ICMP_UNREACH_SRCFAIL */
	ICMP6_DST_UNREACH_ADDR,		/* 6: ICMP_UNREACH_NET_UNKNOWN */
	ICMP6_DST_UNREACH_ADDR,		/* 7: ICMP_UNREACH_HOST_UNKNOWN */
	-1,				/* 8: ICMP_UNREACH_ISOLATED */
	ICMP6_DST_UNREACH_ADMIN,	/* 9: ICMP_UNREACH_NET_PROHIB */
	ICMP6_DST_UNREACH_ADMIN,	/* 10: ICMP_UNREACH_HOST_PROHIB */
	-1,				/* 11: ICMP_UNREACH_TOSNET */
	-1,				/* 12: ICMP_UNREACH_TOSHOST */
	ICMP6_DST_UNREACH_ADMIN,	/* 13: ICMP_UNREACH_ADMIN_PROHIBIT */
};
int	icmpreplytype6[ICMP6_MAXTYPE + 1];
#endif

int	icmpreplytype4[ICMP_MAXTYPE + 1];


/* ------------------------------------------------------------------------ */
/* Function:    ipf_matchicmpqueryreply                                     */
/* Returns:     int - 1 if "icmp" is a valid reply to "ic" else 0.          */
/* Parameters:  v(I)    - IP protocol version (4 or 6)                      */
/*              ic(I)   - ICMP information                                  */
/*              icmp(I) - ICMP packet header                                */
/*              rev(I)  - direction (0 = forward/1 = reverse) of packet     */
/*                                                                          */
/* Check if the ICMP packet defined by the header pointed to by icmp is a   */
/* reply to one as described by what's in ic.  If it is a match, return 1,  */
/* else return 0 for no match.                                              */
/* ------------------------------------------------------------------------ */
int
ipf_matchicmpqueryreply(v, ic, icmp, rev)
	int v;
	icmpinfo_t *ic;
	icmphdr_t *icmp;
	int rev;
{
	int ictype;

	ictype = ic->ici_type;

	if (v == 4) {
		/*
		 * If we matched its type on the way in, then when going out
		 * it will still be the same type.
		 */
		if ((!rev && (icmp->icmp_type == ictype)) ||
		    (rev && (icmpreplytype4[ictype] == icmp->icmp_type))) {
			if (icmp->icmp_type != ICMP_ECHOREPLY)
				return 1;
			if (icmp->icmp_id == ic->ici_id)
				return 1;
		}
	}
#ifdef	USE_INET6
	else if (v == 6) {
		if ((!rev && (icmp->icmp_type == ictype)) ||
		    (rev && (icmpreplytype6[ictype] == icmp->icmp_type))) {
			if (icmp->icmp_type != ICMP6_ECHO_REPLY)
				return 1;
			if (icmp->icmp_id == ic->ici_id)
				return 1;
		}
	}
#endif
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_rule_compare                                            */
/* Parameters:  fr1(I) - first rule structure to compare                    */
/*              fr2(I) - second rule structure to compare                   */
/* Returns:     int    - 0 == rules are the same, else mismatch             */
/*                                                                          */
/* Compare two rules and return 0 if they match or a number indicating      */
/* which of the individual checks failed.                                   */
/* ------------------------------------------------------------------------ */
static int
ipf_rule_compare(frentry_t *fr1, frentry_t *fr2)
{
	if (fr1->fr_cksum != fr2->fr_cksum)
		return 1;
	if (fr1->fr_size != fr2->fr_size)
		return 2;
	if (fr1->fr_dsize != fr2->fr_dsize)
		return 3;
	if (bcmp((char *)&fr1->fr_func, (char *)&fr2->fr_func,
		 fr1->fr_size - offsetof(struct frentry, fr_func)) != 0)
		return 4;
	if (fr1->fr_data && !fr2->fr_data)
		return 5;
	if (!fr1->fr_data && fr2->fr_data)
		return 6;
	if (fr1->fr_data) {
		if (bcmp(fr1->fr_caddr, fr2->fr_caddr, fr1->fr_dsize))
			return 7;
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    frrequest                                                   */
/* Returns:     int - 0 == success, > 0 == errno value                      */
/* Parameters:  unit(I)     - device for which this is for                  */
/*              req(I)      - ioctl command (SIOC*)                         */
/*              data(I)     - pointr to ioctl data                          */
/*              set(I)      - 1 or 0 (filter set)                           */
/*              makecopy(I) - flag indicating whether data points to a rule */
/*                            in kernel space & hence doesn't need copying. */
/*                                                                          */
/* This function handles all the requests which operate on the list of      */
/* filter rules.  This includes adding, deleting, insertion.  It is also    */
/* responsible for creating groups when a "head" rule is loaded.  Interface */
/* names are resolved here and other sanity checks are made on the content  */
/* of the rule structure being loaded.  If a rule has user defined timeouts */
/* then make sure they are created and initialised before exiting.          */
/* ------------------------------------------------------------------------ */
int
frrequest(softc, unit, req, data, set, makecopy)
	ipf_main_softc_t *softc;
	int unit;
	ioctlcmd_t req;
	int set, makecopy;
	caddr_t data;
{
	int error = 0, in, family, addrem, need_free = 0;
	frentry_t frd, *fp, *f, **fprev, **ftail;
	void *ptr, *uptr, *cptr;
	u_int *p, *pp;
	frgroup_t *fg;
	char *group;

	ptr = NULL;
	cptr = NULL;
	fg = NULL;
	fp = &frd;
	if (makecopy != 0) {
		bzero(fp, sizeof(frd));
		error = ipf_inobj(softc, data, NULL, fp, IPFOBJ_FRENTRY);
		if (error) {
			return error;
		}
		if ((fp->fr_type & FR_T_BUILTIN) != 0) {
			IPFERROR(6);
			return EINVAL;
		}
		KMALLOCS(f, frentry_t *, fp->fr_size);
		if (f == NULL) {
			IPFERROR(131);
			return ENOMEM;
		}
		bzero(f, fp->fr_size);
		error = ipf_inobjsz(softc, data, f, IPFOBJ_FRENTRY,
				    fp->fr_size);
		if (error) {
			KFREES(f, fp->fr_size);
			return error;
		}

		fp = f;
		f = NULL;
		fp->fr_next = NULL;
		fp->fr_dnext = NULL;
		fp->fr_pnext = NULL;
		fp->fr_pdnext = NULL;
		fp->fr_grp = NULL;
		fp->fr_grphead = NULL;
		fp->fr_icmpgrp = NULL;
		fp->fr_isc = (void *)-1;
		fp->fr_ptr = NULL;
		fp->fr_ref = 0;
		fp->fr_flags |= FR_COPIED;
	} else {
		fp = (frentry_t *)data;
		if ((fp->fr_type & FR_T_BUILTIN) == 0) {
			IPFERROR(7);
			return EINVAL;
		}
		fp->fr_flags &= ~FR_COPIED;
	}

	if (((fp->fr_dsize == 0) && (fp->fr_data != NULL)) ||
	    ((fp->fr_dsize != 0) && (fp->fr_data == NULL))) {
		IPFERROR(8);
		error = EINVAL;
		goto donenolock;
	}

	family = fp->fr_family;
	uptr = fp->fr_data;

	if (req == (ioctlcmd_t)SIOCINAFR || req == (ioctlcmd_t)SIOCINIFR ||
	    req == (ioctlcmd_t)SIOCADAFR || req == (ioctlcmd_t)SIOCADIFR)
		addrem = 0;
	else if (req == (ioctlcmd_t)SIOCRMAFR || req == (ioctlcmd_t)SIOCRMIFR)
		addrem = 1;
	else if (req == (ioctlcmd_t)SIOCZRLST)
		addrem = 2;
	else {
		IPFERROR(9);
		error = EINVAL;
		goto donenolock;
	}

	/*
	 * Only filter rules for IPv4 or IPv6 are accepted.
	 */
	if (family == AF_INET) {
		/*EMPTY*/;
#ifdef	USE_INET6
	} else if (family == AF_INET6) {
		/*EMPTY*/;
#endif
	} else if (family != 0) {
		IPFERROR(10);
		error = EINVAL;
		goto donenolock;
	}

	/*
	 * If the rule is being loaded from user space, i.e. we had to copy it
	 * into kernel space, then do not trust the function pointer in the
	 * rule.
	 */
	if ((makecopy == 1) && (fp->fr_func != NULL)) {
		if (ipf_findfunc(fp->fr_func) == NULL) {
			IPFERROR(11);
			error = ESRCH;
			goto donenolock;
		}

		if (addrem == 0) {
			error = ipf_funcinit(softc, fp);
			if (error != 0)
				goto donenolock;
		}
	}
	if ((fp->fr_flags & FR_CALLNOW) &&
	    ((fp->fr_func == NULL) || (fp->fr_func == (ipfunc_t)-1))) {
		IPFERROR(142);
		error = ESRCH;
		goto donenolock;
	}
	if (((fp->fr_flags & FR_CMDMASK) == FR_CALL) &&
	    ((fp->fr_func == NULL) || (fp->fr_func == (ipfunc_t)-1))) {
		IPFERROR(143);
		error = ESRCH;
		goto donenolock;
	}

	ptr = NULL;
	cptr = NULL;

	if (FR_ISACCOUNT(fp->fr_flags))
		unit = IPL_LOGCOUNT;

	/*
	 * Check that each group name in the rule has a start index that
	 * is valid.
	 */
	if (fp->fr_icmphead != -1) {
		if ((fp->fr_icmphead < 0) ||
		    (fp->fr_icmphead >= fp->fr_namelen)) {
			IPFERROR(136);
			error = EINVAL;
			goto donenolock;
		}
		if (!strcmp(FR_NAME(fp, fr_icmphead), "0"))
			fp->fr_names[fp->fr_icmphead] = '\0';
	}

	if (fp->fr_grhead != -1) {
		if ((fp->fr_grhead < 0) ||
		    (fp->fr_grhead >= fp->fr_namelen)) {
			IPFERROR(137);
			error = EINVAL;
			goto donenolock;
		}
		if (!strcmp(FR_NAME(fp, fr_grhead), "0"))
			fp->fr_names[fp->fr_grhead] = '\0';
	}

	if (fp->fr_group != -1) {
		if ((fp->fr_group < 0) ||
		    (fp->fr_group >= fp->fr_namelen)) {
			IPFERROR(138);
			error = EINVAL;
			goto donenolock;
		}
		if ((req != (int)SIOCZRLST) && (fp->fr_group != -1)) {
			/*
			 * Allow loading rules that are in groups to cause
			 * them to be created if they don't already exit.
			 */
			group = FR_NAME(fp, fr_group);
			if (addrem == 0) {
				fg = ipf_group_add(softc, group, NULL,
						   fp->fr_flags, unit, set);
				fp->fr_grp = fg;
			} else {
				fg = ipf_findgroup(softc, group, unit,
						   set, NULL);
				if (fg == NULL) {
					IPFERROR(12);
					error = ESRCH;
					goto donenolock;
				}
			}

			if (fg->fg_flags == 0) {
				fg->fg_flags = fp->fr_flags & FR_INOUT;
			} else if (fg->fg_flags != (fp->fr_flags & FR_INOUT)) {
				IPFERROR(13);
				error = ESRCH;
				goto donenolock;
			}
		}
	} else {
		/*
		 * If a rule is going to be part of a group then it does
		 * not matter whether it is an in or out rule, but if it
		 * isn't in a group, then it does...
		 */
		if ((fp->fr_flags & (FR_INQUE|FR_OUTQUE)) == 0) {
			IPFERROR(14);
			error = EINVAL;
			goto donenolock;
		}
	}
	in = (fp->fr_flags & FR_INQUE) ? 0 : 1;

	/*
	 * Work out which rule list this change is being applied to.
	 */
	ftail = NULL;
	fprev = NULL;
	if (unit == IPL_LOGAUTH) {
                if ((fp->fr_tifs[0].fd_ptr != NULL) ||
		    (fp->fr_tifs[1].fd_ptr != NULL) ||
		    (fp->fr_dif.fd_ptr != NULL) ||
		    (fp->fr_flags & FR_FASTROUTE)) {
			softc->ipf_interror = 145;
			error = EINVAL;
			goto donenolock;
		}
		fprev = ipf_auth_rulehead(softc);
	} else {
		if (FR_ISACCOUNT(fp->fr_flags))
			fprev = &softc->ipf_acct[in][set];
		else if ((fp->fr_flags & (FR_OUTQUE|FR_INQUE)) != 0)
			fprev = &softc->ipf_rules[in][set];
	}
	if (fprev == NULL) {
		IPFERROR(15);
		error = ESRCH;
		goto donenolock;
	}

	if (fg != NULL)
		fprev = &fg->fg_start;

	/*
	 * Copy in extra data for the rule.
	 */
	if (fp->fr_dsize != 0) {
		if (makecopy != 0) {
			KMALLOCS(ptr, void *, fp->fr_dsize);
			if (ptr == NULL) {
				IPFERROR(16);
				error = ENOMEM;
				goto donenolock;
			}

			/*
			 * The bcopy case is for when the data is appended
			 * to the rule by ipf_in_compat().
			 */
			if (uptr >= (void *)fp &&
			    uptr < (void *)((char *)fp + fp->fr_size)) {
				bcopy(uptr, ptr, fp->fr_dsize);
				error = 0;
			} else {
				error = COPYIN(uptr, ptr, fp->fr_dsize);
				if (error != 0) {
					IPFERROR(17);
					error = EFAULT;
					goto donenolock;
				}
			}
		} else {
			ptr = uptr;
		}
		fp->fr_data = ptr;
	} else {
		fp->fr_data = NULL;
	}

	/*
	 * Perform per-rule type sanity checks of their members.
	 * All code after this needs to be aware that allocated memory
	 * may need to be free'd before exiting.
	 */
	switch (fp->fr_type & ~FR_T_BUILTIN)
	{
#if defined(IPFILTER_BPF)
	case FR_T_BPFOPC :
		if (fp->fr_dsize == 0) {
			IPFERROR(19);
			error = EINVAL;
			break;
		}
		if (!bpf_validate(ptr, fp->fr_dsize/sizeof(struct bpf_insn))) {
			IPFERROR(20);
			error = EINVAL;
			break;
		}
		break;
#endif
	case FR_T_IPF :
		/*
		 * Preparation for error case at the bottom of this function.
		 */
		if (fp->fr_datype == FRI_LOOKUP)
			fp->fr_dstptr = NULL;
		if (fp->fr_satype == FRI_LOOKUP)
			fp->fr_srcptr = NULL;

		if (fp->fr_dsize != sizeof(fripf_t)) {
			IPFERROR(21);
			error = EINVAL;
			break;
		}

		/*
		 * Allowing a rule with both "keep state" and "with oow" is
		 * pointless because adding a state entry to the table will
		 * fail with the out of window (oow) flag set.
		 */
		if ((fp->fr_flags & FR_KEEPSTATE) && (fp->fr_flx & FI_OOW)) {
			IPFERROR(22);
			error = EINVAL;
			break;
		}

		switch (fp->fr_satype)
		{
		case FRI_BROADCAST :
		case FRI_DYNAMIC :
		case FRI_NETWORK :
		case FRI_NETMASKED :
		case FRI_PEERADDR :
			if (fp->fr_sifpidx < 0) {
				IPFERROR(23);
				error = EINVAL;
			}
			break;
		case FRI_LOOKUP :
			fp->fr_srcptr = ipf_findlookup(softc, unit, fp,
						       &fp->fr_src6,
						       &fp->fr_smsk6);
			if (fp->fr_srcfunc == NULL) {
				IPFERROR(132);
				error = ESRCH;
				break;
			}
			break;
		case FRI_NORMAL :
			break;
		default :
			IPFERROR(133);
			error = EINVAL;
			break;
		}
		if (error != 0)
			break;

		switch (fp->fr_datype)
		{
		case FRI_BROADCAST :
		case FRI_DYNAMIC :
		case FRI_NETWORK :
		case FRI_NETMASKED :
		case FRI_PEERADDR :
			if (fp->fr_difpidx < 0) {
				IPFERROR(24);
				error = EINVAL;
			}
			break;
		case FRI_LOOKUP :
			fp->fr_dstptr = ipf_findlookup(softc, unit, fp,
						       &fp->fr_dst6,
						       &fp->fr_dmsk6);
			if (fp->fr_dstfunc == NULL) {
				IPFERROR(134);
				error = ESRCH;
			}
			break;
		case FRI_NORMAL :
			break;
		default :
			IPFERROR(135);
			error = EINVAL;
		}
		break;

	case FR_T_NONE :
	case FR_T_CALLFUNC :
	case FR_T_COMPIPF :
		break;

	case FR_T_IPFEXPR :
		if (ipf_matcharray_verify(fp->fr_data, fp->fr_dsize) == -1) {
			IPFERROR(25);
			error = EINVAL;
		}
		break;

	default :
		IPFERROR(26);
		error = EINVAL;
		break;
	}
	if (error != 0)
		goto donenolock;

	if (fp->fr_tif.fd_name != -1) {
		if ((fp->fr_tif.fd_name < 0) ||
		    (fp->fr_tif.fd_name >= fp->fr_namelen)) {
			IPFERROR(139);
			error = EINVAL;
			goto donenolock;
		}
	}

	if (fp->fr_dif.fd_name != -1) {
		if ((fp->fr_dif.fd_name < 0) ||
		    (fp->fr_dif.fd_name >= fp->fr_namelen)) {
			IPFERROR(140);
			error = EINVAL;
			goto donenolock;
		}
	}

	if (fp->fr_rif.fd_name != -1) {
		if ((fp->fr_rif.fd_name < 0) ||
		    (fp->fr_rif.fd_name >= fp->fr_namelen)) {
			IPFERROR(141);
			error = EINVAL;
			goto donenolock;
		}
	}

	/*
	 * Lookup all the interface names that are part of the rule.
	 */
	error = ipf_synclist(softc, fp, NULL);
	if (error != 0)
		goto donenolock;
	fp->fr_statecnt = 0;
	if (fp->fr_srctrack.ht_max_nodes != 0)
		ipf_rb_ht_init(&fp->fr_srctrack);

	/*
	 * Look for an existing matching filter rule, but don't include the
	 * next or interface pointer in the comparison (fr_next, fr_ifa).
	 * This elminates rules which are indentical being loaded.  Checksum
	 * the constant part of the filter rule to make comparisons quicker
	 * (this meaning no pointers are included).
	 */
	for (fp->fr_cksum = 0, p = (u_int *)&fp->fr_func, pp = &fp->fr_cksum;
	     p < pp; p++)
		fp->fr_cksum += *p;
	pp = (u_int *)(fp->fr_caddr + fp->fr_dsize);
	for (p = (u_int *)fp->fr_data; p < pp; p++)
		fp->fr_cksum += *p;

	WRITE_ENTER(&softc->ipf_mutex);

	/*
	 * Now that the filter rule lists are locked, we can walk the
	 * chain of them without fear.
	 */
	ftail = fprev;
	for (f = *ftail; (f = *ftail) != NULL; ftail = &f->fr_next) {
		if (fp->fr_collect <= f->fr_collect) {
			ftail = fprev;
			f = NULL;
			break;
		}
		fprev = ftail;
	}

	for (; (f = *ftail) != NULL; ftail = &f->fr_next) {
		if (ipf_rule_compare(fp, f) == 0)
			break;
	}

	/*
	 * If zero'ing statistics, copy current to caller and zero.
	 */
	if (addrem == 2) {
		if (f == NULL) {
			IPFERROR(27);
			error = ESRCH;
		} else {
			/*
			 * Copy and reduce lock because of impending copyout.
			 * Well we should, but if we do then the atomicity of
			 * this call and the correctness of fr_hits and
			 * fr_bytes cannot be guaranteed.  As it is, this code
			 * only resets them to 0 if they are successfully
			 * copied out into user space.
			 */
			bcopy((char *)f, (char *)fp, f->fr_size);
			/* MUTEX_DOWNGRADE(&softc->ipf_mutex); */

			/*
			 * When we copy this rule back out, set the data
			 * pointer to be what it was in user space.
			 */
			fp->fr_data = uptr;
			error = ipf_outobj(softc, data, fp, IPFOBJ_FRENTRY);

			if (error == 0) {
				if ((f->fr_dsize != 0) && (uptr != NULL))
					error = COPYOUT(f->fr_data, uptr,
							f->fr_dsize);
					if (error != 0) {
						IPFERROR(28);
						error = EFAULT;
					}
				if (error == 0) {
					f->fr_hits = 0;
					f->fr_bytes = 0;
				}
			}
		}

		if (makecopy != 0) {
			if (ptr != NULL) {
				KFREES(ptr, fp->fr_dsize);
			}
			KFREES(fp, fp->fr_size);
		}
		RWLOCK_EXIT(&softc->ipf_mutex);
		return error;
	}

  	if (!f) {
		/*
		 * At the end of this, ftail must point to the place where the
		 * new rule is to be saved/inserted/added.
		 * For SIOCAD*FR, this should be the last rule in the group of
		 * rules that have equal fr_collect fields.
		 * For SIOCIN*FR, ...
		 */
		if (req == (ioctlcmd_t)SIOCADAFR ||
		    req == (ioctlcmd_t)SIOCADIFR) {

			for (ftail = fprev; (f = *ftail) != NULL; ) {
				if (f->fr_collect > fp->fr_collect)
					break;
				ftail = &f->fr_next;
				fprev = ftail;
			}
			ftail = fprev;
			f = NULL;
			ptr = NULL;
		} else if (req == (ioctlcmd_t)SIOCINAFR ||
			   req == (ioctlcmd_t)SIOCINIFR) {
			while ((f = *fprev) != NULL) {
				if (f->fr_collect >= fp->fr_collect)
					break;
				fprev = &f->fr_next;
			}
  			ftail = fprev;
  			if (fp->fr_hits != 0) {
				while (fp->fr_hits && (f = *ftail)) {
					if (f->fr_collect != fp->fr_collect)
						break;
					fprev = ftail;
  					ftail = &f->fr_next;
					fp->fr_hits--;
				}
  			}
  			f = NULL;
  			ptr = NULL;
		}
	}

	/*
	 * Request to remove a rule.
	 */
	if (addrem == 1) {
		if (!f) {
			IPFERROR(29);
			error = ESRCH;
		} else {
			/*
			 * Do not allow activity from user space to interfere
			 * with rules not loaded that way.
			 */
			if ((makecopy == 1) && !(f->fr_flags & FR_COPIED)) {
				IPFERROR(30);
				error = EPERM;
				goto done;
			}

			/*
			 * Return EBUSY if the rule is being reference by
			 * something else (eg state information.)
			 */
			if (f->fr_ref > 1) {
				IPFERROR(31);
				error = EBUSY;
				goto done;
			}
#ifdef	IPFILTER_SCAN
			if (f->fr_isctag != -1 &&
			    (f->fr_isc != (struct ipscan *)-1))
				ipf_scan_detachfr(f);
#endif

			if (unit == IPL_LOGAUTH) {
				error = ipf_auth_precmd(softc, req, f, ftail);
				goto done;
			}

			ipf_rule_delete(softc, f, unit, set);

			need_free = makecopy;
		}
	} else {
		/*
		 * Not removing, so we must be adding/inserting a rule.
		 */
		if (f != NULL) {
			IPFERROR(32);
			error = EEXIST;
			goto done;
		}
		if (unit == IPL_LOGAUTH) {
			error = ipf_auth_precmd(softc, req, fp, ftail);
			goto done;
		}

		MUTEX_NUKE(&fp->fr_lock);
		MUTEX_INIT(&fp->fr_lock, "filter rule lock");
		if (fp->fr_die != 0)
			ipf_rule_expire_insert(softc, fp, set);

		fp->fr_hits = 0;
		if (makecopy != 0)
			fp->fr_ref = 1;
		fp->fr_pnext = ftail;
		fp->fr_next = *ftail;
		if (fp->fr_next != NULL)
			fp->fr_next->fr_pnext = &fp->fr_next;
		*ftail = fp;
		if (addrem == 0)
			ipf_fixskip(ftail, fp, 1);

		fp->fr_icmpgrp = NULL;
		if (fp->fr_icmphead != -1) {
			group = FR_NAME(fp, fr_icmphead);
			fg = ipf_group_add(softc, group, fp, 0, unit, set);
			fp->fr_icmpgrp = fg;
		}

		fp->fr_grphead = NULL;
		if (fp->fr_grhead != -1) {
			group = FR_NAME(fp, fr_grhead);
			fg = ipf_group_add(softc, group, fp, fp->fr_flags,
					   unit, set);
			fp->fr_grphead = fg;
		}
	}
done:
	RWLOCK_EXIT(&softc->ipf_mutex);
donenolock:
	if (need_free || (error != 0)) {
		if ((fp->fr_type & ~FR_T_BUILTIN) == FR_T_IPF) {
			if ((fp->fr_satype == FRI_LOOKUP) &&
			    (fp->fr_srcptr != NULL))
				ipf_lookup_deref(softc, fp->fr_srctype,
						 fp->fr_srcptr);
			if ((fp->fr_datype == FRI_LOOKUP) &&
			    (fp->fr_dstptr != NULL))
				ipf_lookup_deref(softc, fp->fr_dsttype,
						 fp->fr_dstptr);
		}
		if (fp->fr_grp != NULL) {
			WRITE_ENTER(&softc->ipf_mutex);
			ipf_group_del(softc, fp->fr_grp, fp);
			RWLOCK_EXIT(&softc->ipf_mutex);
		}
		if ((ptr != NULL) && (makecopy != 0)) {
			KFREES(ptr, fp->fr_dsize);
		}
		KFREES(fp, fp->fr_size);
	}
	return (error);
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_rule_delete                                              */
/* Returns:    Nil                                                          */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*             f(I)     - pointer to the rule being deleted                 */
/*             ftail(I) - pointer to the pointer to f                       */
/*             unit(I)  - device for which this is for                      */
/*             set(I)   - 1 or 0 (filter set)                               */
/*                                                                          */
/* This function attempts to do what it can to delete a filter rule: remove */
/* it from any linked lists and remove any groups it is responsible for.    */
/* But in the end, removing a rule can only drop the reference count - we   */
/* must use that as the guide for whether or not it can be freed.           */
/* ------------------------------------------------------------------------ */
static void
ipf_rule_delete(softc, f, unit, set)
	ipf_main_softc_t *softc;
	frentry_t *f;
	int unit, set;
{

	/*
	 * If fr_pdnext is set, then the rule is on the expire list, so
	 * remove it from there.
	 */
	if (f->fr_pdnext != NULL) {
		*f->fr_pdnext = f->fr_dnext;
		if (f->fr_dnext != NULL)
			f->fr_dnext->fr_pdnext = f->fr_pdnext;
		f->fr_pdnext = NULL;
		f->fr_dnext = NULL;
	}

	ipf_fixskip(f->fr_pnext, f, -1);
	if (f->fr_pnext != NULL)
		*f->fr_pnext = f->fr_next;
	if (f->fr_next != NULL)
		f->fr_next->fr_pnext = f->fr_pnext;
	f->fr_pnext = NULL;
	f->fr_next = NULL;

	(void) ipf_derefrule(softc, &f);
}

/* ------------------------------------------------------------------------ */
/* Function:   ipf_rule_expire_insert                                       */
/* Returns:    Nil                                                          */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*             f(I)     - pointer to rule to be added to expire list        */
/*             set(I)   - 1 or 0 (filter set)                               */
/*                                                                          */
/* If the new rule has a given expiration time, insert it into the list of  */
/* expiring rules with the ones to be removed first added to the front of   */
/* the list. The insertion is O(n) but it is kept sorted for quick scans at */
/* expiration interval checks.                                              */
/* ------------------------------------------------------------------------ */
static void
ipf_rule_expire_insert(softc, f, set)
	ipf_main_softc_t *softc;
	frentry_t *f;
	int set;
{
	frentry_t *fr;

	/*
	 */

	f->fr_die = softc->ipf_ticks + IPF_TTLVAL(f->fr_die);
	for (fr = softc->ipf_rule_explist[set]; fr != NULL;
	     fr = fr->fr_dnext) {
		if (f->fr_die < fr->fr_die)
			break;
		if (fr->fr_dnext == NULL) {
			/*
			 * We've got to the last rule and everything
			 * wanted to be expired before this new node,
			 * so we have to tack it on the end...
			 */
			fr->fr_dnext = f;
			f->fr_pdnext = &fr->fr_dnext;
			fr = NULL;
			break;
		}
	}

	if (softc->ipf_rule_explist[set] == NULL) {
		softc->ipf_rule_explist[set] = f;
		f->fr_pdnext = &softc->ipf_rule_explist[set];
	} else if (fr != NULL) {
		f->fr_dnext = fr;
		f->fr_pdnext = fr->fr_pdnext;
		fr->fr_pdnext = &f->fr_dnext;
	}
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_findlookup                                               */
/* Returns:    NULL = failure, else success                                 */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*             unit(I)  - ipf device we want to find match for              */
/*             fp(I)    - rule for which lookup is for                      */
/*             addrp(I) - pointer to lookup information in address struct   */
/*             maskp(O) - pointer to lookup information for storage         */
/*                                                                          */
/* When using pools and hash tables to store addresses for matching in      */
/* rules, it is necessary to resolve both the object referred to by the     */
/* name or address (and return that pointer) and also provide the means by  */
/* which to determine if an address belongs to that object to make the      */
/* packet matching quicker.                                                 */
/* ------------------------------------------------------------------------ */
static void *
ipf_findlookup(softc, unit, fr, addrp, maskp)
	ipf_main_softc_t *softc;
	int unit;
	frentry_t *fr;
	i6addr_t *addrp, *maskp;
{
	void *ptr = NULL;

	switch (addrp->iplookupsubtype)
	{
	case 0 :
		ptr = ipf_lookup_res_num(softc, unit, addrp->iplookuptype,
					 addrp->iplookupnum,
					 &maskp->iplookupfunc);
		break;
	case 1 :
		if (addrp->iplookupname < 0)
			break;
		if (addrp->iplookupname >= fr->fr_namelen)
			break;
		ptr = ipf_lookup_res_name(softc, unit, addrp->iplookuptype,
					  fr->fr_names + addrp->iplookupname,
					  &maskp->iplookupfunc);
		break;
	default :
		break;
	}

	return ptr;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_funcinit                                                */
/* Returns:     int - 0 == success, else ESRCH: cannot resolve rule details */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              fr(I)    - pointer to filter rule                           */
/*                                                                          */
/* If a rule is a call rule, then check if the function it points to needs  */
/* an init function to be called now the rule has been loaded.              */
/* ------------------------------------------------------------------------ */
static int
ipf_funcinit(softc, fr)
	ipf_main_softc_t *softc;
	frentry_t *fr;
{
	ipfunc_resolve_t *ft;
	int err;

	IPFERROR(34);
	err = ESRCH;

	for (ft = ipf_availfuncs; ft->ipfu_addr != NULL; ft++)
		if (ft->ipfu_addr == fr->fr_func) {
			err = 0;
			if (ft->ipfu_init != NULL)
				err = (*ft->ipfu_init)(softc, fr);
			break;
		}
	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_funcfini                                                */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              fr(I)    - pointer to filter rule                           */
/*                                                                          */
/* For a given filter rule, call the matching "fini" function if the rule   */
/* is using a known function that would have resulted in the "init" being   */
/* called for ealier.                                                       */
/* ------------------------------------------------------------------------ */
static void
ipf_funcfini(softc, fr)
	ipf_main_softc_t *softc;
	frentry_t *fr;
{
	ipfunc_resolve_t *ft;

	for (ft = ipf_availfuncs; ft->ipfu_addr != NULL; ft++)
		if (ft->ipfu_addr == fr->fr_func) {
			if (ft->ipfu_fini != NULL)
				(void) (*ft->ipfu_fini)(softc, fr);
			break;
		}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_findfunc                                                */
/* Returns:     ipfunc_t - pointer to function if found, else NULL          */
/* Parameters:  funcptr(I) - function pointer to lookup                     */
/*                                                                          */
/* Look for a function in the table of known functions.                     */
/* ------------------------------------------------------------------------ */
static ipfunc_t
ipf_findfunc(funcptr)
	ipfunc_t funcptr;
{
	ipfunc_resolve_t *ft;

	for (ft = ipf_availfuncs; ft->ipfu_addr != NULL; ft++)
		if (ft->ipfu_addr == funcptr)
			return funcptr;
	return NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_resolvefunc                                             */
/* Returns:     int - 0 == success, else error                              */
/* Parameters:  data(IO) - ioctl data pointer to ipfunc_resolve_t struct    */
/*                                                                          */
/* Copy in a ipfunc_resolve_t structure and then fill in the missing field. */
/* This will either be the function name (if the pointer is set) or the     */
/* function pointer if the name is set.  When found, fill in the other one  */
/* so that the entire, complete, structure can be copied back to user space.*/
/* ------------------------------------------------------------------------ */
int
ipf_resolvefunc(softc, data)
	ipf_main_softc_t *softc;
	void *data;
{
	ipfunc_resolve_t res, *ft;
	int error;

	error = BCOPYIN(data, &res, sizeof(res));
	if (error != 0) {
		IPFERROR(123);
		return EFAULT;
	}

	if (res.ipfu_addr == NULL && res.ipfu_name[0] != '\0') {
		for (ft = ipf_availfuncs; ft->ipfu_addr != NULL; ft++)
			if (strncmp(res.ipfu_name, ft->ipfu_name,
				    sizeof(res.ipfu_name)) == 0) {
				res.ipfu_addr = ft->ipfu_addr;
				res.ipfu_init = ft->ipfu_init;
				if (COPYOUT(&res, data, sizeof(res)) != 0) {
					IPFERROR(35);
					return EFAULT;
				}
				return 0;
			}
	}
	if (res.ipfu_addr != NULL && res.ipfu_name[0] == '\0') {
		for (ft = ipf_availfuncs; ft->ipfu_addr != NULL; ft++)
			if (ft->ipfu_addr == res.ipfu_addr) {
				(void) strncpy(res.ipfu_name, ft->ipfu_name,
					       sizeof(res.ipfu_name));
				res.ipfu_init = ft->ipfu_init;
				if (COPYOUT(&res, data, sizeof(res)) != 0) {
					IPFERROR(36);
					return EFAULT;
				}
				return 0;
			}
	}
	IPFERROR(37);
	return ESRCH;
}


#if !defined(_KERNEL) || SOLARIS
/*
 * From: NetBSD
 * ppsratecheck(): packets (or events) per second limitation.
 */
int
ppsratecheck(lasttime, curpps, maxpps)
	struct timeval *lasttime;
	int *curpps;
	int maxpps;	/* maximum pps allowed */
{
	struct timeval tv, delta;
	int rv;

	GETKTIME(&tv);

	delta.tv_sec = tv.tv_sec - lasttime->tv_sec;
	delta.tv_usec = tv.tv_usec - lasttime->tv_usec;
	if (delta.tv_usec < 0) {
		delta.tv_sec--;
		delta.tv_usec += 1000000;
	}

	/*
	 * check for 0,0 is so that the message will be seen at least once.
	 * if more than one second have passed since the last update of
	 * lasttime, reset the counter.
	 *
	 * we do increment *curpps even in *curpps < maxpps case, as some may
	 * try to use *curpps for stat purposes as well.
	 */
	if ((lasttime->tv_sec == 0 && lasttime->tv_usec == 0) ||
	    delta.tv_sec >= 1) {
		*lasttime = tv;
		*curpps = 0;
		rv = 1;
	} else if (maxpps < 0)
		rv = 1;
	else if (*curpps < maxpps)
		rv = 1;
	else
		rv = 0;
	*curpps = *curpps + 1;

	return (rv);
}
#endif


/* ------------------------------------------------------------------------ */
/* Function:    ipf_derefrule                                               */
/* Returns:     int   - 0 == rule freed up, else rule not freed             */
/* Parameters:  fr(I) - pointer to filter rule                              */
/*                                                                          */
/* Decrement the reference counter to a rule by one.  If it reaches zero,   */
/* free it and any associated storage space being used by it.               */
/* ------------------------------------------------------------------------ */
int
ipf_derefrule(softc, frp)
	ipf_main_softc_t *softc;
	frentry_t **frp;
{
	frentry_t *fr;
	frdest_t *fdp;

	fr = *frp;
	*frp = NULL;

	MUTEX_ENTER(&fr->fr_lock);
	fr->fr_ref--;
	if (fr->fr_ref == 0) {
		MUTEX_EXIT(&fr->fr_lock);
		MUTEX_DESTROY(&fr->fr_lock);

		ipf_funcfini(softc, fr);

		fdp = &fr->fr_tif;
		if (fdp->fd_type == FRD_DSTLIST)
			ipf_lookup_deref(softc, IPLT_DSTLIST, fdp->fd_ptr);

		fdp = &fr->fr_rif;
		if (fdp->fd_type == FRD_DSTLIST)
			ipf_lookup_deref(softc, IPLT_DSTLIST, fdp->fd_ptr);

		fdp = &fr->fr_dif;
		if (fdp->fd_type == FRD_DSTLIST)
			ipf_lookup_deref(softc, IPLT_DSTLIST, fdp->fd_ptr);

		if ((fr->fr_type & ~FR_T_BUILTIN) == FR_T_IPF &&
		    fr->fr_satype == FRI_LOOKUP)
			ipf_lookup_deref(softc, fr->fr_srctype, fr->fr_srcptr);
		if ((fr->fr_type & ~FR_T_BUILTIN) == FR_T_IPF &&
		    fr->fr_datype == FRI_LOOKUP)
			ipf_lookup_deref(softc, fr->fr_dsttype, fr->fr_dstptr);

		if (fr->fr_grp != NULL)
			ipf_group_del(softc, fr->fr_grp, fr);

		if (fr->fr_grphead != NULL)
			ipf_group_del(softc, fr->fr_grphead, fr);

		if (fr->fr_icmpgrp != NULL)
			ipf_group_del(softc, fr->fr_icmpgrp, fr);

		if ((fr->fr_flags & FR_COPIED) != 0) {
			if (fr->fr_dsize) {
				KFREES(fr->fr_data, fr->fr_dsize);
			}
			KFREES(fr, fr->fr_size);
			return 0;
		}
		return 1;
	} else {
		MUTEX_EXIT(&fr->fr_lock);
	}
	return -1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_grpmapinit                                              */
/* Returns:     int - 0 == success, else ESRCH because table entry not found*/
/* Parameters:  fr(I) - pointer to rule to find hash table for              */
/*                                                                          */
/* Looks for group hash table fr_arg and stores a pointer to it in fr_ptr.  */
/* fr_ptr is later used by ipf_srcgrpmap and ipf_dstgrpmap.                 */
/* ------------------------------------------------------------------------ */
static int
ipf_grpmapinit(softc, fr)
	ipf_main_softc_t *softc;
	frentry_t *fr;
{
	char name[FR_GROUPLEN];
	iphtable_t *iph;

#if defined(SNPRINTF) && defined(_KERNEL)
	SNPRINTF(name, sizeof(name), "%d", fr->fr_arg);
#else
	(void) sprintf(name, "%d", fr->fr_arg);
#endif
	iph = ipf_lookup_find_htable(softc, IPL_LOGIPF, name);
	if (iph == NULL) {
		IPFERROR(38);
		return ESRCH;
	}
	if ((iph->iph_flags & FR_INOUT) != (fr->fr_flags & FR_INOUT)) {
		IPFERROR(39);
		return ESRCH;
	}
	iph->iph_ref++;
	fr->fr_ptr = iph;
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_grpmapfini                                              */
/* Returns:     int - 0 == success, else ESRCH because table entry not found*/
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              fr(I)    - pointer to rule to release hash table for        */
/*                                                                          */
/* For rules that have had ipf_grpmapinit called, ipf_lookup_deref needs to */
/* be called to undo what ipf_grpmapinit caused to be done.                 */
/* ------------------------------------------------------------------------ */
static int
ipf_grpmapfini(softc, fr)
	ipf_main_softc_t *softc;
	frentry_t *fr;
{
	iphtable_t *iph;
	iph = fr->fr_ptr;
	if (iph != NULL)
		ipf_lookup_deref(softc, IPLT_HASH, iph);
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_srcgrpmap                                               */
/* Returns:     frentry_t * - pointer to "new last matching" rule or NULL   */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              passp(IO) - pointer to current/new filter decision (unused) */
/*                                                                          */
/* Look for a rule group head in a hash table, using the source address as  */
/* the key, and descend into that group and continue matching rules against */
/* the packet.                                                              */
/* ------------------------------------------------------------------------ */
frentry_t *
ipf_srcgrpmap(fin, passp)
	fr_info_t *fin;
	u_32_t *passp;
{
	frgroup_t *fg;
	void *rval;

	rval = ipf_iphmfindgroup(fin->fin_main_soft, fin->fin_fr->fr_ptr,
				 &fin->fin_src);
	if (rval == NULL)
		return NULL;

	fg = rval;
	fin->fin_fr = fg->fg_start;
	(void) ipf_scanlist(fin, *passp);
	return fin->fin_fr;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_dstgrpmap                                               */
/* Returns:     frentry_t * - pointer to "new last matching" rule or NULL   */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              passp(IO) - pointer to current/new filter decision (unused) */
/*                                                                          */
/* Look for a rule group head in a hash table, using the destination        */
/* address as the key, and descend into that group and continue matching    */
/* rules against  the packet.                                               */
/* ------------------------------------------------------------------------ */
frentry_t *
ipf_dstgrpmap(fin, passp)
	fr_info_t *fin;
	u_32_t *passp;
{
	frgroup_t *fg;
	void *rval;

	rval = ipf_iphmfindgroup(fin->fin_main_soft, fin->fin_fr->fr_ptr,
				 &fin->fin_dst);
	if (rval == NULL)
		return NULL;

	fg = rval;
	fin->fin_fr = fg->fg_start;
	(void) ipf_scanlist(fin, *passp);
	return fin->fin_fr;
}

/*
 * Queue functions
 * ===============
 * These functions manage objects on queues for efficient timeouts.  There
 * are a number of system defined queues as well as user defined timeouts.
 * It is expected that a lock is held in the domain in which the queue
 * belongs (i.e. either state or NAT) when calling any of these functions
 * that prevents ipf_freetimeoutqueue() from being called at the same time
 * as any other.
 */


/* ------------------------------------------------------------------------ */
/* Function:    ipf_addtimeoutqueue                                         */
/* Returns:     struct ifqtq * - NULL if malloc fails, else pointer to      */
/*                               timeout queue with given interval.         */
/* Parameters:  parent(I)  - pointer to pointer to parent node of this list */
/*                           of interface queues.                           */
/*              seconds(I) - timeout value in seconds for this queue.       */
/*                                                                          */
/* This routine first looks for a timeout queue that matches the interval   */
/* being requested.  If it finds one, increments the reference counter and  */
/* returns a pointer to it.  If none are found, it allocates a new one and  */
/* inserts it at the top of the list.                                       */
/*                                                                          */
/* Locking.                                                                 */
/* It is assumed that the caller of this function has an appropriate lock   */
/* held (exclusively) in the domain that encompases 'parent'.               */
/* ------------------------------------------------------------------------ */
ipftq_t *
ipf_addtimeoutqueue(softc, parent, seconds)
	ipf_main_softc_t *softc;
	ipftq_t **parent;
	u_int seconds;
{
	ipftq_t *ifq;
	u_int period;

	period = seconds * IPF_HZ_DIVIDE;

	MUTEX_ENTER(&softc->ipf_timeoutlock);
	for (ifq = *parent; ifq != NULL; ifq = ifq->ifq_next) {
		if (ifq->ifq_ttl == period) {
			/*
			 * Reset the delete flag, if set, so the structure
			 * gets reused rather than freed and reallocated.
			 */
			MUTEX_ENTER(&ifq->ifq_lock);
			ifq->ifq_flags &= ~IFQF_DELETE;
			ifq->ifq_ref++;
			MUTEX_EXIT(&ifq->ifq_lock);
			MUTEX_EXIT(&softc->ipf_timeoutlock);

			return ifq;
		}
	}

	KMALLOC(ifq, ipftq_t *);
	if (ifq != NULL) {
		MUTEX_NUKE(&ifq->ifq_lock);
		IPFTQ_INIT(ifq, period, "ipftq mutex");
		ifq->ifq_next = *parent;
		ifq->ifq_pnext = parent;
		ifq->ifq_flags = IFQF_USER;
		ifq->ifq_ref++;
		*parent = ifq;
		softc->ipf_userifqs++;
	}
	MUTEX_EXIT(&softc->ipf_timeoutlock);
	return ifq;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_deletetimeoutqueue                                      */
/* Returns:     int    - new reference count value of the timeout queue     */
/* Parameters:  ifq(I) - timeout queue which is losing a reference.         */
/* Locks:       ifq->ifq_lock                                               */
/*                                                                          */
/* This routine must be called when we're discarding a pointer to a timeout */
/* queue object, taking care of the reference counter.                      */
/*                                                                          */
/* Now that this just sets a DELETE flag, it requires the expire code to    */
/* check the list of user defined timeout queues and call the free function */
/* below (currently commented out) to stop memory leaking.  It is done this */
/* way because the locking may not be sufficient to safely do a free when   */
/* this function is called.                                                 */
/* ------------------------------------------------------------------------ */
int
ipf_deletetimeoutqueue(ifq)
	ipftq_t *ifq;
{

	ifq->ifq_ref--;
	if ((ifq->ifq_ref == 0) && ((ifq->ifq_flags & IFQF_USER) != 0)) {
		ifq->ifq_flags |= IFQF_DELETE;
	}

	return ifq->ifq_ref;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_freetimeoutqueue                                        */
/* Parameters:  ifq(I) - timeout queue which is losing a reference.         */
/* Returns:     Nil                                                         */
/*                                                                          */
/* Locking:                                                                 */
/* It is assumed that the caller of this function has an appropriate lock   */
/* held (exclusively) in the domain that encompases the callers "domain".   */
/* The ifq_lock for this structure should not be held.                      */
/*                                                                          */
/* Remove a user defined timeout queue from the list of queues it is in and */
/* tidy up after this is done.                                              */
/* ------------------------------------------------------------------------ */
void
ipf_freetimeoutqueue(softc, ifq)
	ipf_main_softc_t *softc;
	ipftq_t *ifq;
{

	if (((ifq->ifq_flags & IFQF_DELETE) == 0) || (ifq->ifq_ref != 0) ||
	    ((ifq->ifq_flags & IFQF_USER) == 0)) {
		printf("ipf_freetimeoutqueue(%lx) flags 0x%x ttl %d ref %d\n",
		       (u_long)ifq, ifq->ifq_flags, ifq->ifq_ttl,
		       ifq->ifq_ref);
		return;
	}

	/*
	 * Remove from its position in the list.
	 */
	*ifq->ifq_pnext = ifq->ifq_next;
	if (ifq->ifq_next != NULL)
		ifq->ifq_next->ifq_pnext = ifq->ifq_pnext;
	ifq->ifq_next = NULL;
	ifq->ifq_pnext = NULL;

	MUTEX_DESTROY(&ifq->ifq_lock);
	ATOMIC_DEC(softc->ipf_userifqs);
	KFREE(ifq);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_deletequeueentry                                        */
/* Returns:     Nil                                                         */
/* Parameters:  tqe(I) - timeout queue entry to delete                      */
/*                                                                          */
/* Remove a tail queue entry from its queue and make it an orphan.          */
/* ipf_deletetimeoutqueue is called to make sure the reference count on the */
/* queue is correct.  We can't, however, call ipf_freetimeoutqueue because  */
/* the correct lock(s) may not be held that would make it safe to do so.    */
/* ------------------------------------------------------------------------ */
void
ipf_deletequeueentry(tqe)
	ipftqent_t *tqe;
{
	ipftq_t *ifq;

	ifq = tqe->tqe_ifq;

	MUTEX_ENTER(&ifq->ifq_lock);

	if (tqe->tqe_pnext != NULL) {
		*tqe->tqe_pnext = tqe->tqe_next;
		if (tqe->tqe_next != NULL)
			tqe->tqe_next->tqe_pnext = tqe->tqe_pnext;
		else    /* we must be the tail anyway */
			ifq->ifq_tail = tqe->tqe_pnext;

		tqe->tqe_pnext = NULL;
		tqe->tqe_ifq = NULL;
	}

	(void) ipf_deletetimeoutqueue(ifq);
	ASSERT(ifq->ifq_ref > 0);

	MUTEX_EXIT(&ifq->ifq_lock);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_queuefront                                              */
/* Returns:     Nil                                                         */
/* Parameters:  tqe(I) - pointer to timeout queue entry                     */
/*                                                                          */
/* Move a queue entry to the front of the queue, if it isn't already there. */
/* ------------------------------------------------------------------------ */
void
ipf_queuefront(tqe)
	ipftqent_t *tqe;
{
	ipftq_t *ifq;

	ifq = tqe->tqe_ifq;
	if (ifq == NULL)
		return;

	MUTEX_ENTER(&ifq->ifq_lock);
	if (ifq->ifq_head != tqe) {
		*tqe->tqe_pnext = tqe->tqe_next;
		if (tqe->tqe_next)
			tqe->tqe_next->tqe_pnext = tqe->tqe_pnext;
		else
			ifq->ifq_tail = tqe->tqe_pnext;

		tqe->tqe_next = ifq->ifq_head;
		ifq->ifq_head->tqe_pnext = &tqe->tqe_next;
		ifq->ifq_head = tqe;
		tqe->tqe_pnext = &ifq->ifq_head;
	}
	MUTEX_EXIT(&ifq->ifq_lock);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_queueback                                               */
/* Returns:     Nil                                                         */
/* Parameters:  ticks(I) - ipf tick time to use with this call              */
/*              tqe(I)   - pointer to timeout queue entry                   */
/*                                                                          */
/* Move a queue entry to the back of the queue, if it isn't already there.  */
/* We use use ticks to calculate the expiration and mark for when we last   */
/* touched the structure.                                                   */
/* ------------------------------------------------------------------------ */
void
ipf_queueback(ticks, tqe)
	u_long ticks;
	ipftqent_t *tqe;
{
	ipftq_t *ifq;

	ifq = tqe->tqe_ifq;
	if (ifq == NULL)
		return;
	tqe->tqe_die = ticks + ifq->ifq_ttl;
	tqe->tqe_touched = ticks;

	MUTEX_ENTER(&ifq->ifq_lock);
	if (tqe->tqe_next != NULL) {		/* at the end already ? */
		/*
		 * Remove from list
		 */
		*tqe->tqe_pnext = tqe->tqe_next;
		tqe->tqe_next->tqe_pnext = tqe->tqe_pnext;

		/*
		 * Make it the last entry.
		 */
		tqe->tqe_next = NULL;
		tqe->tqe_pnext = ifq->ifq_tail;
		*ifq->ifq_tail = tqe;
		ifq->ifq_tail = &tqe->tqe_next;
	}
	MUTEX_EXIT(&ifq->ifq_lock);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_queueappend                                             */
/* Returns:     Nil                                                         */
/* Parameters:  ticks(I)  - ipf tick time to use with this call             */
/*              tqe(I)    - pointer to timeout queue entry                  */
/*              ifq(I)    - pointer to timeout queue                        */
/*              parent(I) - owing object pointer                            */
/*                                                                          */
/* Add a new item to this queue and put it on the very end.                 */
/* We use use ticks to calculate the expiration and mark for when we last   */
/* touched the structure.                                                   */
/* ------------------------------------------------------------------------ */
void
ipf_queueappend(ticks, tqe, ifq, parent)
	u_long ticks;
	ipftqent_t *tqe;
	ipftq_t *ifq;
	void *parent;
{

	MUTEX_ENTER(&ifq->ifq_lock);
	tqe->tqe_parent = parent;
	tqe->tqe_pnext = ifq->ifq_tail;
	*ifq->ifq_tail = tqe;
	ifq->ifq_tail = &tqe->tqe_next;
	tqe->tqe_next = NULL;
	tqe->tqe_ifq = ifq;
	tqe->tqe_die = ticks + ifq->ifq_ttl;
	tqe->tqe_touched = ticks;
	ifq->ifq_ref++;
	MUTEX_EXIT(&ifq->ifq_lock);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_movequeue                                               */
/* Returns:     Nil                                                         */
/* Parameters:  tq(I)   - pointer to timeout queue information              */
/*              oifp(I) - old timeout queue entry was on                    */
/*              nifp(I) - new timeout queue to put entry on                 */
/*                                                                          */
/* Move a queue entry from one timeout queue to another timeout queue.      */
/* If it notices that the current entry is already last and does not need   */
/* to move queue, the return.                                               */
/* ------------------------------------------------------------------------ */
void
ipf_movequeue(ticks, tqe, oifq, nifq)
	u_long ticks;
	ipftqent_t *tqe;
	ipftq_t *oifq, *nifq;
{

	/*
	 * If the queue hasn't changed and we last touched this entry at the
	 * same ipf time, then we're not going to achieve anything by either
	 * changing the ttl or moving it on the queue.
	 */
	if (oifq == nifq && tqe->tqe_touched == ticks)
		return;

	/*
	 * For any of this to be outside the lock, there is a risk that two
	 * packets entering simultaneously, with one changing to a different
	 * queue and one not, could end up with things in a bizarre state.
	 */
	MUTEX_ENTER(&oifq->ifq_lock);

	tqe->tqe_touched = ticks;
	tqe->tqe_die = ticks + nifq->ifq_ttl;
	/*
	 * Is the operation here going to be a no-op ?
	 */
	if (oifq == nifq) {
		if ((tqe->tqe_next == NULL) ||
		    (tqe->tqe_next->tqe_die == tqe->tqe_die)) {
			MUTEX_EXIT(&oifq->ifq_lock);
			return;
		}
	}

	/*
	 * Remove from the old queue
	 */
	*tqe->tqe_pnext = tqe->tqe_next;
	if (tqe->tqe_next)
		tqe->tqe_next->tqe_pnext = tqe->tqe_pnext;
	else
		oifq->ifq_tail = tqe->tqe_pnext;
	tqe->tqe_next = NULL;

	/*
	 * If we're moving from one queue to another, release the
	 * lock on the old queue and get a lock on the new queue.
	 * For user defined queues, if we're moving off it, call
	 * delete in case it can now be freed.
	 */
	if (oifq != nifq) {
		tqe->tqe_ifq = NULL;

		(void) ipf_deletetimeoutqueue(oifq);

		MUTEX_EXIT(&oifq->ifq_lock);

		MUTEX_ENTER(&nifq->ifq_lock);

		tqe->tqe_ifq = nifq;
		nifq->ifq_ref++;
	}

	/*
	 * Add to the bottom of the new queue
	 */
	tqe->tqe_pnext = nifq->ifq_tail;
	*nifq->ifq_tail = tqe;
	nifq->ifq_tail = &tqe->tqe_next;
	MUTEX_EXIT(&nifq->ifq_lock);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_updateipid                                              */
/* Returns:     int - 0 == success, -1 == error (packet should be droppped) */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* When we are doing NAT, change the IP of every packet to represent a      */
/* single sequence of packets coming from the host, hiding any host         */
/* specific sequencing that might otherwise be revealed.  If the packet is  */
/* a fragment, then store the 'new' IPid in the fragment cache and look up  */
/* the fragment cache for non-leading fragments.  If a non-leading fragment */
/* has no match in the cache, return an error.                              */
/* ------------------------------------------------------------------------ */
static int
ipf_updateipid(fin)
	fr_info_t *fin;
{
	u_short id, ido, sums;
	u_32_t sumd, sum;
	ip_t *ip;

	ip = fin->fin_ip;
	ido = ntohs(ip->ip_id);
	if (fin->fin_off != 0) {
		sum = ipf_frag_ipidknown(fin);
		if (sum == 0xffffffff)
			return -1;
		sum &= 0xffff;
		id = (u_short)sum;
		ip->ip_id = htons(id);
	} else {
		ip_fillid(ip);
		id = ntohs(ip->ip_id);
		if ((fin->fin_flx & FI_FRAG) != 0)
			(void) ipf_frag_ipidnew(fin, (u_32_t)id);
	}

	if (id == ido)
		return 0;
	CALC_SUMD(ido, id, sumd);	/* DESTRUCTIVE MACRO! id,ido change */
	sum = (~ntohs(ip->ip_sum)) & 0xffff;
	sum += sumd;
	sum = (sum >> 16) + (sum & 0xffff);
	sum = (sum >> 16) + (sum & 0xffff);
	sums = ~(u_short)sum;
	ip->ip_sum = htons(sums);
	return 0;
}


#ifdef	NEED_FRGETIFNAME
/* ------------------------------------------------------------------------ */
/* Function:    ipf_getifname                                               */
/* Returns:     char *    - pointer to interface name                       */
/* Parameters:  ifp(I)    - pointer to network interface                    */
/*              buffer(O) - pointer to where to store interface name        */
/*                                                                          */
/* Constructs an interface name in the buffer passed.  The buffer passed is */
/* expected to be at least LIFNAMSIZ in bytes big.  If buffer is passed in  */
/* as a NULL pointer then return a pointer to a static array.               */
/* ------------------------------------------------------------------------ */
char *
ipf_getifname(ifp, buffer)
	struct ifnet *ifp;
	char *buffer;
{
	static char namebuf[LIFNAMSIZ];
# if defined(MENTAT) || defined(__FreeBSD__)
	int unit, space;
	char temp[20];
	char *s;
# endif

	if (buffer == NULL)
		buffer = namebuf;
	(void) strncpy(buffer, ifp->if_name, LIFNAMSIZ);
	buffer[LIFNAMSIZ - 1] = '\0';
# if defined(MENTAT) || defined(__FreeBSD__)
	for (s = buffer; *s; s++)
		;
	unit = ifp->if_unit;
	space = LIFNAMSIZ - (s - buffer);
	if ((space > 0) && (unit >= 0)) {
#  if defined(SNPRINTF) && defined(_KERNEL)
		SNPRINTF(temp, sizeof(temp), "%d", unit);
#  else
		(void) sprintf(temp, "%d", unit);
#  endif
		(void) strncpy(s, temp, space);
	}
# endif
	return buffer;
}
#endif


/* ------------------------------------------------------------------------ */
/* Function:    ipf_ioctlswitch                                             */
/* Returns:     int     - -1 continue processing, else ioctl return value   */
/* Parameters:  unit(I) - device unit opened                                */
/*              data(I) - pointer to ioctl data                             */
/*              cmd(I)  - ioctl command                                     */
/*              mode(I) - mode value                                        */
/*              uid(I)  - uid making the ioctl call                         */
/*              ctx(I)  - pointer to context data                           */
/*                                                                          */
/* Based on the value of unit, call the appropriate ioctl handler or return */
/* EIO if ipfilter is not running.   Also checks if write perms are req'd   */
/* for the device in order to execute the ioctl.  A special case is made    */
/* SIOCIPFINTERROR so that the same code isn't required in every handler.   */
/* The context data pointer is passed through as this is used as the key    */
/* for locating a matching token for continued access for walking lists,    */
/* etc.                                                                     */
/* ------------------------------------------------------------------------ */
int
ipf_ioctlswitch(softc, unit, data, cmd, mode, uid, ctx)
	ipf_main_softc_t *softc;
	int unit, mode, uid;
	ioctlcmd_t cmd;
	void *data, *ctx;
{
	int error = 0;

	switch (cmd)
	{
	case SIOCIPFINTERROR :
		error = BCOPYOUT(&softc->ipf_interror, data,
				 sizeof(softc->ipf_interror));
		if (error != 0) {
			IPFERROR(40);
			error = EFAULT;
		}
		return error;
	default :
		break;
	}

	switch (unit)
	{
	case IPL_LOGIPF :
		error = ipf_ipf_ioctl(softc, data, cmd, mode, uid, ctx);
		break;
	case IPL_LOGNAT :
		if (softc->ipf_running > 0) {
			error = ipf_nat_ioctl(softc, data, cmd, mode,
					      uid, ctx);
		} else {
			IPFERROR(42);
			error = EIO;
		}
		break;
	case IPL_LOGSTATE :
		if (softc->ipf_running > 0) {
			error = ipf_state_ioctl(softc, data, cmd, mode,
						uid, ctx);
		} else {
			IPFERROR(43);
			error = EIO;
		}
		break;
	case IPL_LOGAUTH :
		if (softc->ipf_running > 0) {
			error = ipf_auth_ioctl(softc, data, cmd, mode,
					       uid, ctx);
		} else {
			IPFERROR(44);
			error = EIO;
		}
		break;
	case IPL_LOGSYNC :
		if (softc->ipf_running > 0) {
			error = ipf_sync_ioctl(softc, data, cmd, mode,
					       uid, ctx);
		} else {
			error = EIO;
			IPFERROR(45);
		}
		break;
	case IPL_LOGSCAN :
#ifdef IPFILTER_SCAN
		if (softc->ipf_running > 0)
			error = ipf_scan_ioctl(softc, data, cmd, mode,
					       uid, ctx);
		else
#endif
		{
			error = EIO;
			IPFERROR(46);
		}
		break;
	case IPL_LOGLOOKUP :
		if (softc->ipf_running > 0) {
			error = ipf_lookup_ioctl(softc, data, cmd, mode,
						 uid, ctx);
		} else {
			error = EIO;
			IPFERROR(47);
		}
		break;
	default :
		IPFERROR(48);
		error = EIO;
		break;
	}

	return error;
}


/*
 * This array defines the expected size of objects coming into the kernel
 * for the various recognised object types. The first column is flags (see
 * below), 2nd column is current size, 3rd column is the version number of
 * when the current size became current.
 * Flags:
 * 1 = minimum size, not absolute size
 */
static	int	ipf_objbytes[IPFOBJ_COUNT][3] = {
	{ 1,	sizeof(struct frentry),		5010000 },	/* 0 */
	{ 1,	sizeof(struct friostat),	5010000 },
	{ 0,	sizeof(struct fr_info),		5010000 },
	{ 0,	sizeof(struct ipf_authstat),	4010100 },
	{ 0,	sizeof(struct ipfrstat),	5010000 },
	{ 1,	sizeof(struct ipnat),		5010000 },	/* 5 */
	{ 0,	sizeof(struct natstat),		5010000 },
	{ 0,	sizeof(struct ipstate_save),	5010000 },
	{ 1,	sizeof(struct nat_save),	5010000 },
	{ 0,	sizeof(struct natlookup),	5010000 },
	{ 1,	sizeof(struct ipstate),		5010000 },	/* 10 */
	{ 0,	sizeof(struct ips_stat),	5010000 },
	{ 0,	sizeof(struct frauth),		5010000 },
	{ 0,	sizeof(struct ipftune),		4010100 },
	{ 0,	sizeof(struct nat),		5010000 },
	{ 0,	sizeof(struct ipfruleiter),	4011400 },	/* 15 */
	{ 0,	sizeof(struct ipfgeniter),	4011400 },
	{ 0,	sizeof(struct ipftable),	4011400 },
	{ 0,	sizeof(struct ipflookupiter),	4011400 },
	{ 0,	sizeof(struct ipftq) * IPF_TCP_NSTATES },
	{ 1,	0,				0	}, /* IPFEXPR */
	{ 0,	0,				0	}, /* PROXYCTL */
	{ 0,	sizeof (struct fripf),		5010000	}
};


/* ------------------------------------------------------------------------ */
/* Function:    ipf_inobj                                                   */
/* Returns:     int     - 0 = success, else failure                         */
/* Parameters:  softc(I) - soft context pointerto work with                 */
/*              data(I)  - pointer to ioctl data                            */
/*              objp(O)  - where to store ipfobj structure                  */
/*              ptr(I)   - pointer to data to copy out                      */
/*              type(I)  - type of structure being moved                    */
/*                                                                          */
/* Copy in the contents of what the ipfobj_t points to.  In future, we      */
/* add things to check for version numbers, sizes, etc, to make it backward */
/* compatible at the ABI for user land.                                     */
/* If objp is not NULL then we assume that the caller wants to see what is  */
/* in the ipfobj_t structure being copied in. As an example, this can tell  */
/* the caller what version of ipfilter the ioctl program was written to.    */
/* ------------------------------------------------------------------------ */
int
ipf_inobj(softc, data, objp, ptr, type)
	ipf_main_softc_t *softc;
	void *data;
	ipfobj_t *objp;
	void *ptr;
	int type;
{
	ipfobj_t obj;
	int error;
	int size;

	if ((type < 0) || (type >= IPFOBJ_COUNT)) {
		IPFERROR(49);
		return EINVAL;
	}

	if (objp == NULL)
		objp = &obj;
	error = BCOPYIN(data, objp, sizeof(*objp));
	if (error != 0) {
		IPFERROR(124);
		return EFAULT;
	}

	if (objp->ipfo_type != type) {
		IPFERROR(50);
		return EINVAL;
	}

	if (objp->ipfo_rev >= ipf_objbytes[type][2]) {
		if ((ipf_objbytes[type][0] & 1) != 0) {
			if (objp->ipfo_size < ipf_objbytes[type][1]) {
				IPFERROR(51);
				return EINVAL;
			}
			size =  ipf_objbytes[type][1];
		} else if (objp->ipfo_size == ipf_objbytes[type][1]) {
			size =  objp->ipfo_size;
		} else {
			IPFERROR(52);
			return EINVAL;
		}
		error = COPYIN(objp->ipfo_ptr, ptr, size);
		if (error != 0) {
			IPFERROR(55);
			error = EFAULT;
		}
	} else {
#ifdef  IPFILTER_COMPAT
		error = ipf_in_compat(softc, objp, ptr, 0);
#else
		IPFERROR(54);
		error = EINVAL;
#endif
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_inobjsz                                                 */
/* Returns:     int     - 0 = success, else failure                         */
/* Parameters:  softc(I) - soft context pointerto work with                 */
/*              data(I)  - pointer to ioctl data                            */
/*              ptr(I)   - pointer to store real data in                    */
/*              type(I)  - type of structure being moved                    */
/*              sz(I)    - size of data to copy                             */
/*                                                                          */
/* As per ipf_inobj, except the size of the object to copy in is passed in  */
/* but it must not be smaller than the size defined for the type and the    */
/* type must allow for varied sized objects.  The extra requirement here is */
/* that sz must match the size of the object being passed in - this is not  */
/* not possible nor required in ipf_inobj().                                */
/* ------------------------------------------------------------------------ */
int
ipf_inobjsz(softc, data, ptr, type, sz)
	ipf_main_softc_t *softc;
	void *data;
	void *ptr;
	int type, sz;
{
	ipfobj_t obj;
	int error;

	if ((type < 0) || (type >= IPFOBJ_COUNT)) {
		IPFERROR(56);
		return EINVAL;
	}

	error = BCOPYIN(data, &obj, sizeof(obj));
	if (error != 0) {
		IPFERROR(125);
		return EFAULT;
	}

	if (obj.ipfo_type != type) {
		IPFERROR(58);
		return EINVAL;
	}

	if (obj.ipfo_rev >= ipf_objbytes[type][2]) {
		if (((ipf_objbytes[type][0] & 1) == 0) ||
		    (sz < ipf_objbytes[type][1])) {
			IPFERROR(57);
			return EINVAL;
		}
		error = COPYIN(obj.ipfo_ptr, ptr, sz);
		if (error != 0) {
			IPFERROR(61);
			error = EFAULT;
		}
	} else {
#ifdef	IPFILTER_COMPAT
		error = ipf_in_compat(softc, &obj, ptr, sz);
#else
		IPFERROR(60);
		error = EINVAL;
#endif
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_outobjsz                                                */
/* Returns:     int     - 0 = success, else failure                         */
/* Parameters:  data(I) - pointer to ioctl data                             */
/*              ptr(I)  - pointer to store real data in                     */
/*              type(I) - type of structure being moved                     */
/*              sz(I)   - size of data to copy                              */
/*                                                                          */
/* As per ipf_outobj, except the size of the object to copy out is passed in*/
/* but it must not be smaller than the size defined for the type and the    */
/* type must allow for varied sized objects.  The extra requirement here is */
/* that sz must match the size of the object being passed in - this is not  */
/* not possible nor required in ipf_outobj().                               */
/* ------------------------------------------------------------------------ */
int
ipf_outobjsz(softc, data, ptr, type, sz)
	ipf_main_softc_t *softc;
	void *data;
	void *ptr;
	int type, sz;
{
	ipfobj_t obj;
	int error;

	if ((type < 0) || (type >= IPFOBJ_COUNT)) {
		IPFERROR(62);
		return EINVAL;
	}

	error = BCOPYIN(data, &obj, sizeof(obj));
	if (error != 0) {
		IPFERROR(127);
		return EFAULT;
	}

	if (obj.ipfo_type != type) {
		IPFERROR(63);
		return EINVAL;
	}

	if (obj.ipfo_rev >= ipf_objbytes[type][2]) {
		if (((ipf_objbytes[type][0] & 1) == 0) ||
		    (sz < ipf_objbytes[type][1])) {
			IPFERROR(146);
			return EINVAL;
		}
		error = COPYOUT(ptr, obj.ipfo_ptr, sz);
		if (error != 0) {
			IPFERROR(66);
			error = EFAULT;
		}
	} else {
#ifdef	IPFILTER_COMPAT
		error = ipf_out_compat(softc, &obj, ptr);
#else
		IPFERROR(65);
		error = EINVAL;
#endif
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_outobj                                                  */
/* Returns:     int     - 0 = success, else failure                         */
/* Parameters:  data(I) - pointer to ioctl data                             */
/*              ptr(I)  - pointer to store real data in                     */
/*              type(I) - type of structure being moved                     */
/*                                                                          */
/* Copy out the contents of what ptr is to where ipfobj points to.  In      */
/* future, we add things to check for version numbers, sizes, etc, to make  */
/* it backward  compatible at the ABI for user land.                        */
/* ------------------------------------------------------------------------ */
int
ipf_outobj(softc, data, ptr, type)
	ipf_main_softc_t *softc;
	void *data;
	void *ptr;
	int type;
{
	ipfobj_t obj;
	int error;

	if ((type < 0) || (type >= IPFOBJ_COUNT)) {
		IPFERROR(67);
		return EINVAL;
	}

	error = BCOPYIN(data, &obj, sizeof(obj));
	if (error != 0) {
		IPFERROR(126);
		return EFAULT;
	}

	if (obj.ipfo_type != type) {
		IPFERROR(68);
		return EINVAL;
	}

	if (obj.ipfo_rev >= ipf_objbytes[type][2]) {
		if ((ipf_objbytes[type][0] & 1) != 0) {
			if (obj.ipfo_size < ipf_objbytes[type][1]) {
				IPFERROR(69);
				return EINVAL;
			}
		} else if (obj.ipfo_size != ipf_objbytes[type][1]) {
			IPFERROR(70);
			return EINVAL;
		}

		error = COPYOUT(ptr, obj.ipfo_ptr, obj.ipfo_size);
		if (error != 0) {
			IPFERROR(73);
			error = EFAULT;
		}
	} else {
#ifdef	IPFILTER_COMPAT
		error = ipf_out_compat(softc, &obj, ptr);
#else
		IPFERROR(72);
		error = EINVAL;
#endif
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_outobjk                                                 */
/* Returns:     int     - 0 = success, else failure                         */
/* Parameters:  obj(I)  - pointer to data description structure             */
/*              ptr(I)  - pointer to kernel data to copy out                */
/*                                                                          */
/* In the above functions, the ipfobj_t structure is copied into the kernel,*/
/* telling ipfilter how to copy out data. In this instance, the ipfobj_t is */
/* already populated with information and now we just need to use it.       */
/* There is no need for this function to have a "type" parameter as there   */
/* is no point in validating information that comes from the kernel with    */
/* itself.                                                                  */
/* ------------------------------------------------------------------------ */
int
ipf_outobjk(softc, obj, ptr)
	ipf_main_softc_t *softc;
	ipfobj_t *obj;
	void *ptr;
{
	int type = obj->ipfo_type;
	int error;

	if ((type < 0) || (type >= IPFOBJ_COUNT)) {
		IPFERROR(147);
		return EINVAL;
	}

	if (obj->ipfo_rev >= ipf_objbytes[type][2]) {
		if ((ipf_objbytes[type][0] & 1) != 0) {
			if (obj->ipfo_size < ipf_objbytes[type][1]) {
				IPFERROR(148);
				return EINVAL;
			}

		} else if (obj->ipfo_size != ipf_objbytes[type][1]) {
			IPFERROR(149);
			return EINVAL;
		}

		error = COPYOUT(ptr, obj->ipfo_ptr, obj->ipfo_size);
		if (error != 0) {
			IPFERROR(150);
			error = EFAULT;
		}
	} else {
#ifdef  IPFILTER_COMPAT
		error = ipf_out_compat(softc, obj, ptr);
#else
		IPFERROR(151);
		error = EINVAL;
#endif
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_checkl4sum                                              */
/* Returns:     int     - 0 = good, -1 = bad, 1 = cannot check              */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* If possible, calculate the layer 4 checksum for the packet.  If this is  */
/* not possible, return without indicating a failure or success but in a    */
/* way that is ditinguishable. This function should only be called by the   */
/* ipf_checkv6sum() for each platform.                                      */
/* ------------------------------------------------------------------------ */
INLINE int
ipf_checkl4sum(fin)
	fr_info_t *fin;
{
	u_short sum, hdrsum, *csump;
	udphdr_t *udp;
	int dosum;

	/*
	 * If the TCP packet isn't a fragment, isn't too short and otherwise
	 * isn't already considered "bad", then validate the checksum.  If
	 * this check fails then considered the packet to be "bad".
	 */
	if ((fin->fin_flx & (FI_FRAG|FI_SHORT|FI_BAD)) != 0)
		return 1;

	csump = NULL;
	hdrsum = 0;
	dosum = 0;
	sum = 0;

	switch (fin->fin_p)
	{
	case IPPROTO_TCP :
		csump = &((tcphdr_t *)fin->fin_dp)->th_sum;
		dosum = 1;
		break;

	case IPPROTO_UDP :
		udp = fin->fin_dp;
		if (udp->uh_sum != 0) {
			csump = &udp->uh_sum;
			dosum = 1;
		}
		break;

#ifdef USE_INET6
	case IPPROTO_ICMPV6 :
		csump = &((struct icmp6_hdr *)fin->fin_dp)->icmp6_cksum;
		dosum = 1;
		break;
#endif

	case IPPROTO_ICMP :
		csump = &((struct icmp *)fin->fin_dp)->icmp_cksum;
		dosum = 1;
		break;

	default :
		return 1;
		/*NOTREACHED*/
	}

	if (csump != NULL)
		hdrsum = *csump;

	if (dosum) {
		sum = fr_cksum(fin, fin->fin_ip, fin->fin_p, fin->fin_dp);
	}
#if !defined(_KERNEL)
	if (sum == hdrsum) {
		FR_DEBUG(("checkl4sum: %hx == %hx\n", sum, hdrsum));
	} else {
		FR_DEBUG(("checkl4sum: %hx != %hx\n", sum, hdrsum));
	}
#endif
	DT2(l4sums, u_short, hdrsum, u_short, sum);
	if (hdrsum == sum) {
		fin->fin_cksum = FI_CK_SUMOK;
		return 0;
	}
	fin->fin_cksum = FI_CK_BAD;
	return -1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_ifpfillv4addr                                           */
/* Returns:     int     - 0 = address update, -1 = address not updated      */
/* Parameters:  atype(I)   - type of network address update to perform      */
/*              sin(I)     - pointer to source of address information       */
/*              mask(I)    - pointer to source of netmask information       */
/*              inp(I)     - pointer to destination address store           */
/*              inpmask(I) - pointer to destination netmask store           */
/*                                                                          */
/* Given a type of network address update (atype) to perform, copy          */
/* information from sin/mask into inp/inpmask.  If ipnmask is NULL then no  */
/* netmask update is performed unless FRI_NETMASKED is passed as atype, in  */
/* which case the operation fails.  For all values of atype other than      */
/* FRI_NETMASKED, if inpmask is non-NULL then the mask is set to an all 1s  */
/* value.                                                                   */
/* ------------------------------------------------------------------------ */
int
ipf_ifpfillv4addr(atype, sin, mask, inp, inpmask)
	int atype;
	struct sockaddr_in *sin, *mask;
	struct in_addr *inp, *inpmask;
{
	if (inpmask != NULL && atype != FRI_NETMASKED)
		inpmask->s_addr = 0xffffffff;

	if (atype == FRI_NETWORK || atype == FRI_NETMASKED) {
		if (atype == FRI_NETMASKED) {
			if (inpmask == NULL)
				return -1;
			inpmask->s_addr = mask->sin_addr.s_addr;
		}
		inp->s_addr = sin->sin_addr.s_addr & mask->sin_addr.s_addr;
	} else {
		inp->s_addr = sin->sin_addr.s_addr;
	}
	return 0;
}


#ifdef	USE_INET6
/* ------------------------------------------------------------------------ */
/* Function:    ipf_ifpfillv6addr                                           */
/* Returns:     int     - 0 = address update, -1 = address not updated      */
/* Parameters:  atype(I)   - type of network address update to perform      */
/*              sin(I)     - pointer to source of address information       */
/*              mask(I)    - pointer to source of netmask information       */
/*              inp(I)     - pointer to destination address store           */
/*              inpmask(I) - pointer to destination netmask store           */
/*                                                                          */
/* Given a type of network address update (atype) to perform, copy          */
/* information from sin/mask into inp/inpmask.  If ipnmask is NULL then no  */
/* netmask update is performed unless FRI_NETMASKED is passed as atype, in  */
/* which case the operation fails.  For all values of atype other than      */
/* FRI_NETMASKED, if inpmask is non-NULL then the mask is set to an all 1s  */
/* value.                                                                   */
/* ------------------------------------------------------------------------ */
int
ipf_ifpfillv6addr(atype, sin, mask, inp, inpmask)
	int atype;
	struct sockaddr_in6 *sin, *mask;
	i6addr_t *inp, *inpmask;
{
	i6addr_t *src, *and;

	src = (i6addr_t *)&sin->sin6_addr;
	and = (i6addr_t *)&mask->sin6_addr;

	if (inpmask != NULL && atype != FRI_NETMASKED) {
		inpmask->i6[0] = 0xffffffff;
		inpmask->i6[1] = 0xffffffff;
		inpmask->i6[2] = 0xffffffff;
		inpmask->i6[3] = 0xffffffff;
	}

	if (atype == FRI_NETWORK || atype == FRI_NETMASKED) {
		if (atype == FRI_NETMASKED) {
			if (inpmask == NULL)
				return -1;
			inpmask->i6[0] = and->i6[0];
			inpmask->i6[1] = and->i6[1];
			inpmask->i6[2] = and->i6[2];
			inpmask->i6[3] = and->i6[3];
		}

		inp->i6[0] = src->i6[0] & and->i6[0];
		inp->i6[1] = src->i6[1] & and->i6[1];
		inp->i6[2] = src->i6[2] & and->i6[2];
		inp->i6[3] = src->i6[3] & and->i6[3];
	} else {
		inp->i6[0] = src->i6[0];
		inp->i6[1] = src->i6[1];
		inp->i6[2] = src->i6[2];
		inp->i6[3] = src->i6[3];
	}
	return 0;
}
#endif


/* ------------------------------------------------------------------------ */
/* Function:    ipf_matchtag                                                */
/* Returns:     0 == mismatch, 1 == match.                                  */
/* Parameters:  tag1(I) - pointer to first tag to compare                   */
/*              tag2(I) - pointer to second tag to compare                  */
/*                                                                          */
/* Returns true (non-zero) or false(0) if the two tag structures can be     */
/* considered to be a match or not match, respectively.  The tag is 16      */
/* bytes long (16 characters) but that is overlayed with 4 32bit ints so    */
/* compare the ints instead, for speed. tag1 is the master of the           */
/* comparison.  This function should only be called with both tag1 and tag2 */
/* as non-NULL pointers.                                                    */
/* ------------------------------------------------------------------------ */
int
ipf_matchtag(tag1, tag2)
	ipftag_t *tag1, *tag2;
{
	if (tag1 == tag2)
		return 1;

	if ((tag1->ipt_num[0] == 0) && (tag2->ipt_num[0] == 0))
		return 1;

	if ((tag1->ipt_num[0] == tag2->ipt_num[0]) &&
	    (tag1->ipt_num[1] == tag2->ipt_num[1]) &&
	    (tag1->ipt_num[2] == tag2->ipt_num[2]) &&
	    (tag1->ipt_num[3] == tag2->ipt_num[3]))
		return 1;
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_coalesce                                                */
/* Returns:     1 == success, -1 == failure, 0 == no change                 */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* Attempt to get all of the packet data into a single, contiguous buffer.  */
/* If this call returns a failure then the buffers have also been freed.    */
/* ------------------------------------------------------------------------ */
int
ipf_coalesce(fin)
	fr_info_t *fin;
{

	if ((fin->fin_flx & FI_COALESCE) != 0)
		return 1;

	/*
	 * If the mbuf pointers indicate that there is no mbuf to work with,
	 * return but do not indicate success or failure.
	 */
	if (fin->fin_m == NULL || fin->fin_mp == NULL)
		return 0;

#if defined(_KERNEL)
	if (ipf_pullup(fin->fin_m, fin, fin->fin_plen) == NULL) {
		ipf_main_softc_t *softc = fin->fin_main_soft;

		DT1(frb_coalesce, fr_info_t *, fin);
		LBUMP(ipf_stats[fin->fin_out].fr_badcoalesces);
# ifdef MENTAT
		FREE_MB_T(*fin->fin_mp);
# endif
		fin->fin_reason = FRB_COALESCE;
		*fin->fin_mp = NULL;
		fin->fin_m = NULL;
		return -1;
	}
#else
	fin = fin;	/* LINT */
#endif
	return 1;
}


/*
 * The following table lists all of the tunable variables that can be
 * accessed via SIOCIPFGET/SIOCIPFSET/SIOCIPFGETNEXt.  The format of each row
 * in the table below is as follows:
 *
 * pointer to value, name of value, minimum, maximum, size of the value's
 *     container, value attribute flags
 *
 * For convienience, IPFT_RDONLY means the value is read-only, IPFT_WRDISABLED
 * means the value can only be written to when IPFilter is loaded but disabled.
 * The obvious implication is if neither of these are set then the value can be
 * changed at any time without harm.
 */


/* ------------------------------------------------------------------------ */
/* Function:    ipf_tune_findbycookie                                       */
/* Returns:     NULL = search failed, else pointer to tune struct           */
/* Parameters:  cookie(I) - cookie value to search for amongst tuneables    */
/*              next(O)   - pointer to place to store the cookie for the    */
/*                          "next" tuneable, if it is desired.              */
/*                                                                          */
/* This function is used to walk through all of the existing tunables with  */
/* successive calls.  It searches the known tunables for the one which has  */
/* a matching value for "cookie" - ie its address.  When returning a match, */
/* the next one to be found may be returned inside next.                    */
/* ------------------------------------------------------------------------ */
static ipftuneable_t *
ipf_tune_findbycookie(ptop, cookie, next)
	ipftuneable_t **ptop;
	void *cookie, **next;
{
	ipftuneable_t *ta, **tap;

	for (ta = *ptop; ta->ipft_name != NULL; ta++)
		if (ta == cookie) {
			if (next != NULL) {
				/*
				 * If the next entry in the array has a name
				 * present, then return a pointer to it for
				 * where to go next, else return a pointer to
				 * the dynaminc list as a key to search there
				 * next.  This facilitates a weak linking of
				 * the two "lists" together.
				 */
				if ((ta + 1)->ipft_name != NULL)
					*next = ta + 1;
				else
					*next = ptop;
			}
			return ta;
		}

	for (tap = ptop; (ta = *tap) != NULL; tap = &ta->ipft_next)
		if (tap == cookie) {
			if (next != NULL)
				*next = &ta->ipft_next;
			return ta;
		}

	if (next != NULL)
		*next = NULL;
	return NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_tune_findbyname                                         */
/* Returns:     NULL = search failed, else pointer to tune struct           */
/* Parameters:  name(I) - name of the tuneable entry to find.               */
/*                                                                          */
/* Search the static array of tuneables and the list of dynamic tuneables   */
/* for an entry with a matching name.  If we can find one, return a pointer */
/* to the matching structure.                                               */
/* ------------------------------------------------------------------------ */
static ipftuneable_t *
ipf_tune_findbyname(top, name)
	ipftuneable_t *top;
	const char *name;
{
	ipftuneable_t *ta;

	for (ta = top; ta != NULL; ta = ta->ipft_next)
		if (!strcmp(ta->ipft_name, name)) {
			return ta;
		}

	return NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_tune_add_array                                          */
/* Returns:     int - 0 == success, else failure                            */
/* Parameters:  newtune - pointer to new tune array to add to tuneables     */
/*                                                                          */
/* Appends tune structures from the array passed in (newtune) to the end of */
/* the current list of "dynamic" tuneable parameters.                       */
/* If any entry to be added is already present (by name) then the operation */
/* is aborted - entries that have been added are removed before returning.  */
/* An entry with no name (NULL) is used as the indication that the end of   */
/* the array has been reached.                                              */
/* ------------------------------------------------------------------------ */
int
ipf_tune_add_array(softc, newtune)
	ipf_main_softc_t *softc;
	ipftuneable_t *newtune;
{
	ipftuneable_t *nt, *dt;
	int error = 0;

	for (nt = newtune; nt->ipft_name != NULL; nt++) {
		error = ipf_tune_add(softc, nt);
		if (error != 0) {
			for (dt = newtune; dt != nt; dt++) {
				(void) ipf_tune_del(softc, dt);
			}
		}
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_tune_array_link                                         */
/* Returns:     0 == success, -1 == failure                                 */
/* Parameters:  softc(I) - soft context pointerto work with                 */
/*              array(I) - pointer to an array of tuneables                 */
/*                                                                          */
/* Given an array of tunables (array), append them to the current list of   */
/* tuneables for this context (softc->ipf_tuners.) To properly prepare the  */
/* the array for being appended to the list, initialise all of the next     */
/* pointers so we don't need to walk parts of it with ++ and others with    */
/* next. The array is expected to have an entry with a NULL name as the     */
/* terminator. Trying to add an array with no non-NULL names will return as */
/* a failure.                                                               */
/* ------------------------------------------------------------------------ */
int
ipf_tune_array_link(softc, array)
	ipf_main_softc_t *softc;
	ipftuneable_t *array;
{
	ipftuneable_t *t, **p;

	t = array;
	if (t->ipft_name == NULL)
		return -1;

	for (; t[1].ipft_name != NULL; t++)
		t[0].ipft_next = &t[1];
	t->ipft_next = NULL;

	/*
	 * Since a pointer to the last entry isn't kept, we need to find it
	 * each time we want to add new variables to the list.
	 */
	for (p = &softc->ipf_tuners; (t = *p) != NULL; p = &t->ipft_next)
		if (t->ipft_name == NULL)
			break;
	*p = array;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_tune_array_unlink                                       */
/* Returns:     0 == success, -1 == failure                                 */
/* Parameters:  softc(I) - soft context pointerto work with                 */
/*              array(I) - pointer to an array of tuneables                 */
/*                                                                          */
/* ------------------------------------------------------------------------ */
int
ipf_tune_array_unlink(softc, array)
	ipf_main_softc_t *softc;
	ipftuneable_t *array;
{
	ipftuneable_t *t, **p;

	for (p = &softc->ipf_tuners; (t = *p) != NULL; p = &t->ipft_next)
		if (t == array)
			break;
	if (t == NULL)
		return -1;

	for (; t[1].ipft_name != NULL; t++)
		;

	*p = t->ipft_next;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_tune_array_copy                                          */
/* Returns:    NULL = failure, else pointer to new array                    */
/* Parameters: base(I)     - pointer to structure base                      */
/*             size(I)     - size of the array at template                  */
/*             template(I) - original array to copy                         */
/*                                                                          */
/* Allocate memory for a new set of tuneable values and copy everything     */
/* from template into the new region of memory.  The new region is full of  */
/* uninitialised pointers (ipft_next) so set them up.  Now, ipftp_offset... */
/*                                                                          */
/* NOTE: the following assumes that sizeof(long) == sizeof(void *)          */
/* In the array template, ipftp_offset is the offset (in bytes) of the      */
/* location of the tuneable value inside the structure pointed to by base.  */
/* As ipftp_offset is a union over the pointers to the tuneable values, if  */
/* we add base to the copy's ipftp_offset, copy ends up with a pointer in   */
/* ipftp_void that points to the stored value.                              */
/* ------------------------------------------------------------------------ */
ipftuneable_t *
ipf_tune_array_copy(base, size, template)
	void *base;
	size_t size;
	ipftuneable_t *template;
{
	ipftuneable_t *copy;
	int i;


	KMALLOCS(copy, ipftuneable_t *, size);
	if (copy == NULL) {
		return NULL;
	}
	bcopy(template, copy, size);

	for (i = 0; copy[i].ipft_name; i++) {
		copy[i].ipft_una.ipftp_offset += (u_long)base;
		copy[i].ipft_next = copy + i + 1;
	}

	return copy;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_tune_add                                                */
/* Returns:     int - 0 == success, else failure                            */
/* Parameters:  newtune - pointer to new tune entry to add to tuneables     */
/*                                                                          */
/* Appends tune structures from the array passed in (newtune) to the end of */
/* the current list of "dynamic" tuneable parameters.  Once added, the      */
/* owner of the object is not expected to ever change "ipft_next".          */
/* ------------------------------------------------------------------------ */
int
ipf_tune_add(softc, newtune)
	ipf_main_softc_t *softc;
	ipftuneable_t *newtune;
{
	ipftuneable_t *ta, **tap;

	ta = ipf_tune_findbyname(softc->ipf_tuners, newtune->ipft_name);
	if (ta != NULL) {
		IPFERROR(74);
		return EEXIST;
	}

	for (tap = &softc->ipf_tuners; *tap != NULL; tap = &(*tap)->ipft_next)
		;

	newtune->ipft_next = NULL;
	*tap = newtune;
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_tune_del                                                */
/* Returns:     int - 0 == success, else failure                            */
/* Parameters:  oldtune - pointer to tune entry to remove from the list of  */
/*                        current dynamic tuneables                         */
/*                                                                          */
/* Search for the tune structure, by pointer, in the list of those that are */
/* dynamically added at run time.  If found, adjust the list so that this   */
/* structure is no longer part of it.                                       */
/* ------------------------------------------------------------------------ */
int
ipf_tune_del(softc, oldtune)
	ipf_main_softc_t *softc;
	ipftuneable_t *oldtune;
{
	ipftuneable_t *ta, **tap;
	int error = 0;

	for (tap = &softc->ipf_tuners; (ta = *tap) != NULL;
	     tap = &ta->ipft_next) {
		if (ta == oldtune) {
			*tap = oldtune->ipft_next;
			oldtune->ipft_next = NULL;
			break;
		}
	}

	if (ta == NULL) {
		error = ESRCH;
		IPFERROR(75);
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_tune_del_array                                          */
/* Returns:     int - 0 == success, else failure                            */
/* Parameters:  oldtune - pointer to tuneables array                        */
/*                                                                          */
/* Remove each tuneable entry in the array from the list of "dynamic"       */
/* tunables.  If one entry should fail to be found, an error will be        */
/* returned and no further ones removed.                                    */
/* An entry with a NULL name is used as the indicator of the last entry in  */
/* the array.                                                               */
/* ------------------------------------------------------------------------ */
int
ipf_tune_del_array(softc, oldtune)
	ipf_main_softc_t *softc;
	ipftuneable_t *oldtune;
{
	ipftuneable_t *ot;
	int error = 0;

	for (ot = oldtune; ot->ipft_name != NULL; ot++) {
		error = ipf_tune_del(softc, ot);
		if (error != 0)
			break;
	}

	return error;

}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_tune                                                    */
/* Returns:     int - 0 == success, else failure                            */
/* Parameters:  cmd(I)  - ioctl command number                              */
/*              data(I) - pointer to ioctl data structure                   */
/*                                                                          */
/* Implement handling of SIOCIPFGETNEXT, SIOCIPFGET and SIOCIPFSET.  These  */
/* three ioctls provide the means to access and control global variables    */
/* within IPFilter, allowing (for example) timeouts and table sizes to be   */
/* changed without rebooting, reloading or recompiling.  The initialisation */
/* and 'destruction' routines of the various components of ipfilter are all */
/* each responsible for handling their own values being too big.            */
/* ------------------------------------------------------------------------ */
int
ipf_ipftune(softc, cmd, data)
	ipf_main_softc_t *softc;
	ioctlcmd_t cmd;
	void *data;
{
	ipftuneable_t *ta;
	ipftune_t tu;
	void *cookie;
	int error;

	error = ipf_inobj(softc, data, NULL, &tu, IPFOBJ_TUNEABLE);
	if (error != 0)
		return error;

	tu.ipft_name[sizeof(tu.ipft_name) - 1] = '\0';
	cookie = tu.ipft_cookie;
	ta = NULL;

	switch (cmd)
	{
	case SIOCIPFGETNEXT :
		/*
		 * If cookie is non-NULL, assume it to be a pointer to the last
		 * entry we looked at, so find it (if possible) and return a
		 * pointer to the next one after it.  The last entry in the
		 * the table is a NULL entry, so when we get to it, set cookie
		 * to NULL and return that, indicating end of list, erstwhile
		 * if we come in with cookie set to NULL, we are starting anew
		 * at the front of the list.
		 */
		if (cookie != NULL) {
			ta = ipf_tune_findbycookie(&softc->ipf_tuners,
						   cookie, &tu.ipft_cookie);
		} else {
			ta = softc->ipf_tuners;
			tu.ipft_cookie = ta + 1;
		}
		if (ta != NULL) {
			/*
			 * Entry found, but does the data pointed to by that
			 * row fit in what we can return?
			 */
			if (ta->ipft_sz > sizeof(tu.ipft_un)) {
				IPFERROR(76);
				return EINVAL;
			}

			tu.ipft_vlong = 0;
			if (ta->ipft_sz == sizeof(u_long))
				tu.ipft_vlong = *ta->ipft_plong;
			else if (ta->ipft_sz == sizeof(u_int))
				tu.ipft_vint = *ta->ipft_pint;
			else if (ta->ipft_sz == sizeof(u_short))
				tu.ipft_vshort = *ta->ipft_pshort;
			else if (ta->ipft_sz == sizeof(u_char))
				tu.ipft_vchar = *ta->ipft_pchar;

			tu.ipft_sz = ta->ipft_sz;
			tu.ipft_min = ta->ipft_min;
			tu.ipft_max = ta->ipft_max;
			tu.ipft_flags = ta->ipft_flags;
			bcopy(ta->ipft_name, tu.ipft_name,
			      MIN(sizeof(tu.ipft_name),
				  strlen(ta->ipft_name) + 1));
		}
		error = ipf_outobj(softc, data, &tu, IPFOBJ_TUNEABLE);
		break;

	case SIOCIPFGET :
	case SIOCIPFSET :
		/*
		 * Search by name or by cookie value for a particular entry
		 * in the tuning paramter table.
		 */
		IPFERROR(77);
		error = ESRCH;
		if (cookie != NULL) {
			ta = ipf_tune_findbycookie(&softc->ipf_tuners,
						   cookie, NULL);
			if (ta != NULL)
				error = 0;
		} else if (tu.ipft_name[0] != '\0') {
			ta = ipf_tune_findbyname(softc->ipf_tuners,
						 tu.ipft_name);
			if (ta != NULL)
				error = 0;
		}
		if (error != 0)
			break;

		if (cmd == (ioctlcmd_t)SIOCIPFGET) {
			/*
			 * Fetch the tuning parameters for a particular value
			 */
			tu.ipft_vlong = 0;
			if (ta->ipft_sz == sizeof(u_long))
				tu.ipft_vlong = *ta->ipft_plong;
			else if (ta->ipft_sz == sizeof(u_int))
				tu.ipft_vint = *ta->ipft_pint;
			else if (ta->ipft_sz == sizeof(u_short))
				tu.ipft_vshort = *ta->ipft_pshort;
			else if (ta->ipft_sz == sizeof(u_char))
				tu.ipft_vchar = *ta->ipft_pchar;
			tu.ipft_cookie = ta;
			tu.ipft_sz = ta->ipft_sz;
			tu.ipft_min = ta->ipft_min;
			tu.ipft_max = ta->ipft_max;
			tu.ipft_flags = ta->ipft_flags;
			error = ipf_outobj(softc, data, &tu, IPFOBJ_TUNEABLE);

		} else if (cmd == (ioctlcmd_t)SIOCIPFSET) {
			/*
			 * Set an internal parameter.  The hard part here is
			 * getting the new value safely and correctly out of
			 * the kernel (given we only know its size, not type.)
			 */
			u_long in;

			if (((ta->ipft_flags & IPFT_WRDISABLED) != 0) &&
			    (softc->ipf_running > 0)) {
				IPFERROR(78);
				error = EBUSY;
				break;
			}

			in = tu.ipft_vlong;
			if (in < ta->ipft_min || in > ta->ipft_max) {
				IPFERROR(79);
				error = EINVAL;
				break;
			}

			if (ta->ipft_func != NULL) {
				SPL_INT(s);

				SPL_NET(s);
				error = (*ta->ipft_func)(softc, ta,
							 &tu.ipft_un);
				SPL_X(s);

			} else if (ta->ipft_sz == sizeof(u_long)) {
				tu.ipft_vlong = *ta->ipft_plong;
				*ta->ipft_plong = in;

			} else if (ta->ipft_sz == sizeof(u_int)) {
				tu.ipft_vint = *ta->ipft_pint;
				*ta->ipft_pint = (u_int)(in & 0xffffffff);

			} else if (ta->ipft_sz == sizeof(u_short)) {
				tu.ipft_vshort = *ta->ipft_pshort;
				*ta->ipft_pshort = (u_short)(in & 0xffff);

			} else if (ta->ipft_sz == sizeof(u_char)) {
				tu.ipft_vchar = *ta->ipft_pchar;
				*ta->ipft_pchar = (u_char)(in & 0xff);
			}
			error = ipf_outobj(softc, data, &tu, IPFOBJ_TUNEABLE);
		}
		break;

	default :
		IPFERROR(80);
		error = EINVAL;
		break;
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_zerostats                                               */
/* Returns:     int - 0 = success, else failure                             */
/* Parameters:  data(O) - pointer to pointer for copying data back to       */
/*                                                                          */
/* Copies the current statistics out to userspace and then zero's the       */
/* current ones in the kernel. The lock is only held across the bzero() as  */
/* the copyout may result in paging (ie network activity.)                  */
/* ------------------------------------------------------------------------ */
int
ipf_zerostats(softc, data)
	ipf_main_softc_t *softc;
	caddr_t	data;
{
	friostat_t fio;
	ipfobj_t obj;
	int error;

	error = ipf_inobj(softc, data, &obj, &fio, IPFOBJ_IPFSTAT);
	if (error != 0)
		return error;
	ipf_getstat(softc, &fio, obj.ipfo_rev);
	error = ipf_outobj(softc, data, &fio, IPFOBJ_IPFSTAT);
	if (error != 0)
		return error;

	WRITE_ENTER(&softc->ipf_mutex);
	bzero(&softc->ipf_stats, sizeof(softc->ipf_stats));
	RWLOCK_EXIT(&softc->ipf_mutex);

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_resolvedest                                             */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              base(I)  - where strings are stored                         */
/*              fdp(IO)  - pointer to destination information to resolve    */
/*              v(I)     - IP protocol version to match                     */
/*                                                                          */
/* Looks up an interface name in the frdest structure pointed to by fdp and */
/* if a matching name can be found for the particular IP protocol version   */
/* then store the interface pointer in the frdest struct.  If no match is   */
/* found, then set the interface pointer to be -1 as NULL is considered to  */
/* indicate there is no information at all in the structure.                */
/* ------------------------------------------------------------------------ */
int
ipf_resolvedest(softc, base, fdp, v)
	ipf_main_softc_t *softc;
	char *base;
	frdest_t *fdp;
	int v;
{
	int errval = 0;
	void *ifp;

	ifp = NULL;

	if (fdp->fd_name != -1) {
		if (fdp->fd_type == FRD_DSTLIST) {
			ifp = ipf_lookup_res_name(softc, IPL_LOGIPF,
						  IPLT_DSTLIST,
						  base + fdp->fd_name,
						  NULL);
			if (ifp == NULL) {
				IPFERROR(144);
				errval = ESRCH;
			}
		} else {
			ifp = GETIFP(base + fdp->fd_name, v);
			if (ifp == NULL)
				ifp = (void *)-1;
		}
	}
	fdp->fd_ptr = ifp;

	if ((ifp != NULL) && (ifp != (void *)-1)) {
		fdp->fd_local = ipf_deliverlocal(softc, v, ifp, &fdp->fd_ip6);
	}

	return errval;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_resolvenic                                              */
/* Returns:     void* - NULL = wildcard name, -1 = failed to find NIC, else */
/*                      pointer to interface structure for NIC              */
/* Parameters:  softc(I)- pointer to soft context main structure            */
/*              name(I) - complete interface name                           */
/*              v(I)    - IP protocol version                               */
/*                                                                          */
/* Look for a network interface structure that firstly has a matching name  */
/* to that passed in and that is also being used for that IP protocol       */
/* version (necessary on some platforms where there are separate listings   */
/* for both IPv4 and IPv6 on the same physical NIC.                         */
/* ------------------------------------------------------------------------ */
void *
ipf_resolvenic(softc, name, v)
	ipf_main_softc_t *softc;
	char *name;
	int v;
{
	void *nic;

	softc = softc;	/* gcc -Wextra */
	if (name[0] == '\0')
		return NULL;

	if ((name[1] == '\0') && ((name[0] == '-') || (name[0] == '*'))) {
		return NULL;
	}

	nic = GETIFP(name, v);
	if (nic == NULL)
		nic = (void *)-1;
	return nic;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_token_expire                                            */
/* Returns:     None.                                                       */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* This function is run every ipf tick to see if there are any tokens that  */
/* have been held for too long and need to be freed up.                     */
/* ------------------------------------------------------------------------ */
void
ipf_token_expire(softc)
	ipf_main_softc_t *softc;
{
	ipftoken_t *it;

	WRITE_ENTER(&softc->ipf_tokens);
	while ((it = softc->ipf_token_head) != NULL) {
		if (it->ipt_die > softc->ipf_ticks)
			break;

		ipf_token_deref(softc, it);
	}
	RWLOCK_EXIT(&softc->ipf_tokens);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_token_flush                                             */
/* Returns:     None.                                                       */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Loop through all of the existing tokens and call deref to see if they    */
/* can be freed. Normally a function like this might just loop on           */
/* ipf_token_head but there is a chance that a token might have a ref count */
/* of greater than one and in that case the the reference would drop twice  */
/* by code that is only entitled to drop it once.                           */
/* ------------------------------------------------------------------------ */
static void
ipf_token_flush(softc)
	ipf_main_softc_t *softc;
{
	ipftoken_t *it, *next;

	WRITE_ENTER(&softc->ipf_tokens);
	for (it = softc->ipf_token_head; it != NULL; it = next) {
		next = it->ipt_next;
		(void) ipf_token_deref(softc, it);
	}
	RWLOCK_EXIT(&softc->ipf_tokens);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_token_del                                               */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I)- pointer to soft context main structure            */
/*              type(I) - the token type to match                           */
/*              uid(I)  - uid owning the token                              */
/*              ptr(I)  - context pointer for the token                     */
/*                                                                          */
/* This function looks for a a token in the current list that matches up    */
/* the fields (type, uid, ptr).  If none is found, ESRCH is returned, else  */
/* call ipf_token_dewref() to remove it from the list. In the event that    */
/* the token has a reference held elsewhere, setting ipt_complete to 2      */
/* enables debugging to distinguish between the two paths that ultimately   */
/* lead to a token to be deleted.                                           */
/* ------------------------------------------------------------------------ */
int
ipf_token_del(softc, type, uid, ptr)
	ipf_main_softc_t *softc;
	int type, uid;
	void *ptr;
{
	ipftoken_t *it;
	int error;

	IPFERROR(82);
	error = ESRCH;

	WRITE_ENTER(&softc->ipf_tokens);
	for (it = softc->ipf_token_head; it != NULL; it = it->ipt_next) {
		if (ptr == it->ipt_ctx && type == it->ipt_type &&
		    uid == it->ipt_uid) {
			it->ipt_complete = 2;
			ipf_token_deref(softc, it);
			error = 0;
			break;
		}
	}
	RWLOCK_EXIT(&softc->ipf_tokens);

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_token_mark_complete                                     */
/* Returns:     None.                                                       */
/* Parameters:  token(I) - pointer to token structure                       */
/*                                                                          */
/* Mark a token as being ineligable for being found with ipf_token_find.    */
/* ------------------------------------------------------------------------ */
void
ipf_token_mark_complete(token)
	ipftoken_t *token;
{
	if (token->ipt_complete == 0)
		token->ipt_complete = 1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_token_find                                               */
/* Returns:     ipftoken_t * - NULL if no memory, else pointer to token     */
/* Parameters:  softc(I)- pointer to soft context main structure            */
/*              type(I) - the token type to match                           */
/*              uid(I)  - uid owning the token                              */
/*              ptr(I)  - context pointer for the token                     */
/*                                                                          */
/* This function looks for a live token in the list of current tokens that  */
/* matches the tuple (type, uid, ptr).  If one cannot be found then one is  */
/* allocated.  If one is found then it is moved to the top of the list of   */
/* currently active tokens.                                                 */
/* ------------------------------------------------------------------------ */
ipftoken_t *
ipf_token_find(softc, type, uid, ptr)
	ipf_main_softc_t *softc;
	int type, uid;
	void *ptr;
{
	ipftoken_t *it, *new;

	KMALLOC(new, ipftoken_t *);
	if (new != NULL)
		bzero((char *)new, sizeof(*new));

	WRITE_ENTER(&softc->ipf_tokens);
	for (it = softc->ipf_token_head; it != NULL; it = it->ipt_next) {
		if ((ptr == it->ipt_ctx) && (type == it->ipt_type) &&
		    (uid == it->ipt_uid) && (it->ipt_complete < 2))
			break;
	}

	if (it == NULL) {
		it = new;
		new = NULL;
		if (it == NULL) {
			RWLOCK_EXIT(&softc->ipf_tokens);
			return NULL;
		}
		it->ipt_ctx = ptr;
		it->ipt_uid = uid;
		it->ipt_type = type;
		it->ipt_ref = 1;
	} else {
		if (new != NULL) {
			KFREE(new);
			new = NULL;
		}

		if (it->ipt_complete > 0)
			it = NULL;
		else
			ipf_token_unlink(softc, it);
	}

	if (it != NULL) {
		it->ipt_pnext = softc->ipf_token_tail;
		*softc->ipf_token_tail = it;
		softc->ipf_token_tail = &it->ipt_next;
		it->ipt_next = NULL;
		it->ipt_ref++;

		it->ipt_die = softc->ipf_ticks + 20;
	}

	RWLOCK_EXIT(&softc->ipf_tokens);

	return it;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_token_unlink                                            */
/* Returns:     None.                                                       */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              token(I) - pointer to token structure                       */
/* Write Locks: ipf_tokens                                                  */
/*                                                                          */
/* This function unlinks a token structure from the linked list of tokens   */
/* that "own" it.  The head pointer never needs to be explicitly adjusted   */
/* but the tail does due to the linked list implementation.                 */
/* ------------------------------------------------------------------------ */
static void
ipf_token_unlink(softc, token)
	ipf_main_softc_t *softc;
	ipftoken_t *token;
{

	if (softc->ipf_token_tail == &token->ipt_next)
		softc->ipf_token_tail = token->ipt_pnext;

	*token->ipt_pnext = token->ipt_next;
	if (token->ipt_next != NULL)
		token->ipt_next->ipt_pnext = token->ipt_pnext;
	token->ipt_next = NULL;
	token->ipt_pnext = NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_token_deref                                             */
/* Returns:     int      - 0 == token freed, else reference count           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              token(I) - pointer to token structure                       */
/* Write Locks: ipf_tokens                                                  */
/*                                                                          */
/* Drop the reference count on the token structure and if it drops to zero, */
/* call the dereference function for the token type because it is then      */
/* possible to free the token data structure.                               */
/* ------------------------------------------------------------------------ */
int
ipf_token_deref(softc, token)
	ipf_main_softc_t *softc;
	ipftoken_t *token;
{
	void *data, **datap;

	ASSERT(token->ipt_ref > 0);
	token->ipt_ref--;
	if (token->ipt_ref > 0)
		return token->ipt_ref;

	data = token->ipt_data;
	datap = &data;

	if ((data != NULL) && (data != (void *)-1)) {
		switch (token->ipt_type)
		{
		case IPFGENITER_IPF :
			(void) ipf_derefrule(softc, (frentry_t **)datap);
			break;
		case IPFGENITER_IPNAT :
			WRITE_ENTER(&softc->ipf_nat);
			ipf_nat_rule_deref(softc, (ipnat_t **)datap);
			RWLOCK_EXIT(&softc->ipf_nat);
			break;
		case IPFGENITER_NAT :
			ipf_nat_deref(softc, (nat_t **)datap);
			break;
		case IPFGENITER_STATE :
			ipf_state_deref(softc, (ipstate_t **)datap);
			break;
		case IPFGENITER_FRAG :
			ipf_frag_pkt_deref(softc, (ipfr_t **)datap);
			break;
		case IPFGENITER_NATFRAG :
			ipf_frag_nat_deref(softc, (ipfr_t **)datap);
			break;
		case IPFGENITER_HOSTMAP :
			WRITE_ENTER(&softc->ipf_nat);
			ipf_nat_hostmapdel(softc, (hostmap_t **)datap);
			RWLOCK_EXIT(&softc->ipf_nat);
			break;
		default :
			ipf_lookup_iterderef(softc, token->ipt_type, data);
			break;
		}
	}

	ipf_token_unlink(softc, token);
	KFREE(token);
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nextrule                                                */
/* Returns:     frentry_t * - NULL == no more rules, else pointer to next   */
/* Parameters:  softc(I)    - pointer to soft context main structure        */
/*              fr(I)       - pointer to filter rule                        */
/*              out(I)      - 1 == out rules, 0 == input rules              */
/*                                                                          */
/* Starting with "fr", find the next rule to visit. This includes visiting  */
/* the list of rule groups if either fr is NULL (empty list) or it is the   */
/* last rule in the list. When walking rule lists, it is either input or    */
/* output rules that are returned, never both.                              */
/* ------------------------------------------------------------------------ */
static frentry_t *
ipf_nextrule(softc, active, unit, fr, out)
	ipf_main_softc_t *softc;
	int active, unit;
	frentry_t *fr;
	int out;
{
	frentry_t *next;
	frgroup_t *fg;

	if (fr != NULL && fr->fr_group != -1) {
		fg = ipf_findgroup(softc, fr->fr_names + fr->fr_group,
				   unit, active, NULL);
		if (fg != NULL)
			fg = fg->fg_next;
	} else {
		fg = softc->ipf_groups[unit][active];
	}

	while (fg != NULL) {
		next = fg->fg_start;
		while (next != NULL) {
			if (out) {
				if (next->fr_flags & FR_OUTQUE)
					return next;
			} else if (next->fr_flags & FR_INQUE) {
				return next;
			}
			next = next->fr_next;
		}
		if (next == NULL)
			fg = fg->fg_next;
	}

	return NULL;
}

/* ------------------------------------------------------------------------ */
/* Function:    ipf_getnextrule                                             */
/* Returns:     int - 0 = success, else error                               */
/* Parameters:  softc(I)- pointer to soft context main structure            */
/*              t(I)   - pointer to destination information to resolve      */
/*              ptr(I) - pointer to ipfobj_t to copyin from user space      */
/*                                                                          */
/* This function's first job is to bring in the ipfruleiter_t structure via */
/* the ipfobj_t structure to determine what should be the next rule to      */
/* return. Once the ipfruleiter_t has been brought in, it then tries to     */
/* find the 'next rule'.  This may include searching rule group lists or    */
/* just be as simple as looking at the 'next' field in the rule structure.  */
/* When we have found the rule to return, increase its reference count and  */
/* if we used an existing rule to get here, decrease its reference count.   */
/* ------------------------------------------------------------------------ */
int
ipf_getnextrule(softc, t, ptr)
	ipf_main_softc_t *softc;
	ipftoken_t *t;
	void *ptr;
{
	frentry_t *fr, *next, zero;
	ipfruleiter_t it;
	int error, out;
	frgroup_t *fg;
	ipfobj_t obj;
	int predict;
	char *dst;
	int unit;

	if (t == NULL || ptr == NULL) {
		IPFERROR(84);
		return EFAULT;
	}

	error = ipf_inobj(softc, ptr, &obj, &it, IPFOBJ_IPFITER);
	if (error != 0)
		return error;

	if ((it.iri_inout < 0) || (it.iri_inout > 3)) {
		IPFERROR(85);
		return EINVAL;
	}
	if ((it.iri_active != 0) && (it.iri_active != 1)) {
		IPFERROR(86);
		return EINVAL;
	}
	if (it.iri_nrules == 0) {
		IPFERROR(87);
		return ENOSPC;
	}
	if (it.iri_rule == NULL) {
		IPFERROR(88);
		return EFAULT;
	}

	fg = NULL;
	fr = t->ipt_data;
	if ((it.iri_inout & F_OUT) != 0)
		out = 1;
	else
		out = 0;
	if ((it.iri_inout & F_ACIN) != 0)
		unit = IPL_LOGCOUNT;
	else
		unit = IPL_LOGIPF;

	READ_ENTER(&softc->ipf_mutex);
	if (fr == NULL) {
		if (*it.iri_group == '\0') {
			if (unit == IPL_LOGCOUNT) {
				next = softc->ipf_acct[out][it.iri_active];
			} else {
				next = softc->ipf_rules[out][it.iri_active];
			}
			if (next == NULL)
				next = ipf_nextrule(softc, it.iri_active,
						    unit, NULL, out);
		} else {
			fg = ipf_findgroup(softc, it.iri_group, unit,
					   it.iri_active, NULL);
			if (fg != NULL)
				next = fg->fg_start;
			else
				next = NULL;
		}
	} else {
		next = fr->fr_next;
		if (next == NULL)
			next = ipf_nextrule(softc, it.iri_active, unit,
					    fr, out);
	}

	if (next != NULL && next->fr_next != NULL)
		predict = 1;
	else if (ipf_nextrule(softc, it.iri_active, unit, next, out) != NULL)
		predict = 1;
	else
		predict = 0;

	if (fr != NULL)
		(void) ipf_derefrule(softc, &fr);

	obj.ipfo_type = IPFOBJ_FRENTRY;
	dst = (char *)it.iri_rule;

	if (next != NULL) {
		obj.ipfo_size = next->fr_size;
		MUTEX_ENTER(&next->fr_lock);
		next->fr_ref++;
		MUTEX_EXIT(&next->fr_lock);
		t->ipt_data = next;
	} else {
		obj.ipfo_size = sizeof(frentry_t);
		bzero(&zero, sizeof(zero));
		next = &zero;
		t->ipt_data = NULL;
	}
	it.iri_rule = predict ? next : NULL;
	if (predict == 0)
		ipf_token_mark_complete(t);

	RWLOCK_EXIT(&softc->ipf_mutex);

	obj.ipfo_ptr = dst;
	error = ipf_outobjk(softc, &obj, next);
	if (error == 0 && t->ipt_data != NULL) {
		dst += obj.ipfo_size;
		if (next->fr_data != NULL) {
			ipfobj_t dobj;

			if (next->fr_type == FR_T_IPFEXPR)
				dobj.ipfo_type = IPFOBJ_IPFEXPR;
			else
				dobj.ipfo_type = IPFOBJ_FRIPF;
			dobj.ipfo_size = next->fr_dsize;
			dobj.ipfo_rev = obj.ipfo_rev;
			dobj.ipfo_ptr = dst;
			error = ipf_outobjk(softc, &dobj, next->fr_data);
		}
	}

	if ((fr != NULL) && (next == &zero))
		(void) ipf_derefrule(softc, &fr);

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frruleiter                                              */
/* Returns:     int - 0 = success, else error                               */
/* Parameters:  softc(I)- pointer to soft context main structure            */
/*              data(I) - the token type to match                           */
/*              uid(I)  - uid owning the token                              */
/*              ptr(I)  - context pointer for the token                     */
/*                                                                          */
/* This function serves as a stepping stone between ipf_ipf_ioctl and       */
/* ipf_getnextrule.  It's role is to find the right token in the kernel for */
/* the process doing the ioctl and use that to ask for the next rule.       */
/* ------------------------------------------------------------------------ */
static int
ipf_frruleiter(softc, data, uid, ctx)
	ipf_main_softc_t *softc;
	void *data, *ctx;
	int uid;
{
	ipftoken_t *token;
	ipfruleiter_t it;
	ipfobj_t obj;
	int error;

	token = ipf_token_find(softc, IPFGENITER_IPF, uid, ctx);
	if (token != NULL) {
		error = ipf_getnextrule(softc, token, data);
		WRITE_ENTER(&softc->ipf_tokens);
		ipf_token_deref(softc, token);
		RWLOCK_EXIT(&softc->ipf_tokens);
	} else {
		error = ipf_inobj(softc, data, &obj, &it, IPFOBJ_IPFITER);
		if (error != 0)
			return error;
		it.iri_rule = NULL;
		error = ipf_outobj(softc, data, &it, IPFOBJ_IPFITER);
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_geniter                                                 */
/* Returns:     int - 0 = success, else error                               */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              token(I) - pointer to ipftoken_t structure                  */
/*              itp(I)   - pointer to iterator data                         */
/*                                                                          */
/* Decide which iterator function to call using information passed through  */
/* the ipfgeniter_t structure at itp.                                       */
/* ------------------------------------------------------------------------ */
static int
ipf_geniter(softc, token, itp)
	ipf_main_softc_t *softc;
	ipftoken_t *token;
	ipfgeniter_t *itp;
{
	int error;

	switch (itp->igi_type)
	{
	case IPFGENITER_FRAG :
		error = ipf_frag_pkt_next(softc, token, itp);
		break;
	default :
		IPFERROR(92);
		error = EINVAL;
		break;
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_genericiter                                             */
/* Returns:     int - 0 = success, else error                               */
/* Parameters:  softc(I)- pointer to soft context main structure            */
/*              data(I) - the token type to match                           */
/*              uid(I)  - uid owning the token                              */
/*              ptr(I)  - context pointer for the token                     */
/*                                                                          */
/* Handle the SIOCGENITER ioctl for the ipfilter device. The primary role   */
/* ------------------------------------------------------------------------ */
int
ipf_genericiter(softc, data, uid, ctx)
	ipf_main_softc_t *softc;
	void *data, *ctx;
	int uid;
{
	ipftoken_t *token;
	ipfgeniter_t iter;
	int error;

	error = ipf_inobj(softc, data, NULL, &iter, IPFOBJ_GENITER);
	if (error != 0)
		return error;

	token = ipf_token_find(softc, iter.igi_type, uid, ctx);
	if (token != NULL) {
		token->ipt_subtype = iter.igi_type;
		error = ipf_geniter(softc, token, &iter);
		WRITE_ENTER(&softc->ipf_tokens);
		ipf_token_deref(softc, token);
		RWLOCK_EXIT(&softc->ipf_tokens);
	} else {
		IPFERROR(93);
		error = 0;
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_ipf_ioctl                                               */
/* Returns:     int - 0 = success, else error                               */
/* Parameters:  softc(I)- pointer to soft context main structure           */
/*              data(I) - the token type to match                           */
/*              cmd(I)  - the ioctl command number                          */
/*              mode(I) - mode flags for the ioctl                          */
/*              uid(I)  - uid owning the token                              */
/*              ptr(I)  - context pointer for the token                     */
/*                                                                          */
/* This function handles all of the ioctl command that are actually isssued */
/* to the /dev/ipl device.                                                  */
/* ------------------------------------------------------------------------ */
int
ipf_ipf_ioctl(softc, data, cmd, mode, uid, ctx)
	ipf_main_softc_t *softc;
	caddr_t data;
	ioctlcmd_t cmd;
	int mode, uid;
	void *ctx;
{
	friostat_t fio;
	int error, tmp;
	ipfobj_t obj;
	SPL_INT(s);

	switch (cmd)
	{
	case SIOCFRENB :
		if (!(mode & FWRITE)) {
			IPFERROR(94);
			error = EPERM;
		} else {
			error = BCOPYIN(data, &tmp, sizeof(tmp));
			if (error != 0) {
				IPFERROR(95);
				error = EFAULT;
				break;
			}

			WRITE_ENTER(&softc->ipf_global);
			if (tmp) {
				if (softc->ipf_running > 0)
					error = 0;
				else
					error = ipfattach(softc);
				if (error == 0)
					softc->ipf_running = 1;
				else
					(void) ipfdetach(softc);
			} else {
				if (softc->ipf_running == 1)
					error = ipfdetach(softc);
				else
					error = 0;
				if (error == 0)
					softc->ipf_running = -1;
			}
			RWLOCK_EXIT(&softc->ipf_global);
		}
		break;

	case SIOCIPFSET :
		if (!(mode & FWRITE)) {
			IPFERROR(96);
			error = EPERM;
			break;
		}
		/* FALLTHRU */
	case SIOCIPFGETNEXT :
	case SIOCIPFGET :
		error = ipf_ipftune(softc, cmd, (void *)data);
		break;

	case SIOCSETFF :
		if (!(mode & FWRITE)) {
			IPFERROR(97);
			error = EPERM;
		} else {
			error = BCOPYIN(data, &softc->ipf_flags,
					sizeof(softc->ipf_flags));
			if (error != 0) {
				IPFERROR(98);
				error = EFAULT;
			}
		}
		break;

	case SIOCGETFF :
		error = BCOPYOUT(&softc->ipf_flags, data,
				 sizeof(softc->ipf_flags));
		if (error != 0) {
			IPFERROR(99);
			error = EFAULT;
		}
		break;

	case SIOCFUNCL :
		error = ipf_resolvefunc(softc, (void *)data);
		break;

	case SIOCINAFR :
	case SIOCRMAFR :
	case SIOCADAFR :
	case SIOCZRLST :
		if (!(mode & FWRITE)) {
			IPFERROR(100);
			error = EPERM;
		} else {
			error = frrequest(softc, IPL_LOGIPF, cmd, (caddr_t)data,
					  softc->ipf_active, 1);
		}
		break;

	case SIOCINIFR :
	case SIOCRMIFR :
	case SIOCADIFR :
		if (!(mode & FWRITE)) {
			IPFERROR(101);
			error = EPERM;
		} else {
			error = frrequest(softc, IPL_LOGIPF, cmd, (caddr_t)data,
					  1 - softc->ipf_active, 1);
		}
		break;

	case SIOCSWAPA :
		if (!(mode & FWRITE)) {
			IPFERROR(102);
			error = EPERM;
		} else {
			WRITE_ENTER(&softc->ipf_mutex);
			error = BCOPYOUT(&softc->ipf_active, data,
					 sizeof(softc->ipf_active));
			if (error != 0) {
				IPFERROR(103);
				error = EFAULT;
			} else {
				softc->ipf_active = 1 - softc->ipf_active;
			}
			RWLOCK_EXIT(&softc->ipf_mutex);
		}
		break;

	case SIOCGETFS :
		error = ipf_inobj(softc, (void *)data, &obj, &fio,
				  IPFOBJ_IPFSTAT);
		if (error != 0)
			break;
		ipf_getstat(softc, &fio, obj.ipfo_rev);
		error = ipf_outobj(softc, (void *)data, &fio, IPFOBJ_IPFSTAT);
		break;

	case SIOCFRZST :
		if (!(mode & FWRITE)) {
			IPFERROR(104);
			error = EPERM;
		} else
			error = ipf_zerostats(softc, (caddr_t)data);
		break;

	case SIOCIPFFL :
		if (!(mode & FWRITE)) {
			IPFERROR(105);
			error = EPERM;
		} else {
			error = BCOPYIN(data, &tmp, sizeof(tmp));
			if (!error) {
				tmp = ipf_flush(softc, IPL_LOGIPF, tmp);
				error = BCOPYOUT(&tmp, data, sizeof(tmp));
				if (error != 0) {
					IPFERROR(106);
					error = EFAULT;
				}
			} else {
				IPFERROR(107);
				error = EFAULT;
			}
		}
		break;

#ifdef USE_INET6
	case SIOCIPFL6 :
		if (!(mode & FWRITE)) {
			IPFERROR(108);
			error = EPERM;
		} else {
			error = BCOPYIN(data, &tmp, sizeof(tmp));
			if (!error) {
				tmp = ipf_flush(softc, IPL_LOGIPF, tmp);
				error = BCOPYOUT(&tmp, data, sizeof(tmp));
				if (error != 0) {
					IPFERROR(109);
					error = EFAULT;
				}
			} else {
				IPFERROR(110);
				error = EFAULT;
			}
		}
		break;
#endif

	case SIOCSTLCK :
		if (!(mode & FWRITE)) {
			IPFERROR(122);
			error = EPERM;
		} else {
			error = BCOPYIN(data, &tmp, sizeof(tmp));
			if (error == 0) {
				ipf_state_setlock(softc->ipf_state_soft, tmp);
				ipf_nat_setlock(softc->ipf_nat_soft, tmp);
				ipf_frag_setlock(softc->ipf_frag_soft, tmp);
				ipf_auth_setlock(softc->ipf_auth_soft, tmp);
			} else {
				IPFERROR(111);
				error = EFAULT;
			}
		}
		break;

#ifdef	IPFILTER_LOG
	case SIOCIPFFB :
		if (!(mode & FWRITE)) {
			IPFERROR(112);
			error = EPERM;
		} else {
			tmp = ipf_log_clear(softc, IPL_LOGIPF);
			error = BCOPYOUT(&tmp, data, sizeof(tmp));
			if (error) {
				IPFERROR(113);
				error = EFAULT;
			}
		}
		break;
#endif /* IPFILTER_LOG */

	case SIOCFRSYN :
		if (!(mode & FWRITE)) {
			IPFERROR(114);
			error = EPERM;
		} else {
			WRITE_ENTER(&softc->ipf_global);
#if (defined(MENTAT) && defined(_KERNEL)) && !defined(INSTANCES)
			error = ipfsync();
#else
			ipf_sync(softc, NULL);
			error = 0;
#endif
			RWLOCK_EXIT(&softc->ipf_global);

		}
		break;

	case SIOCGFRST :
		error = ipf_outobj(softc, (void *)data,
				   ipf_frag_stats(softc->ipf_frag_soft),
				   IPFOBJ_FRAGSTAT);
		break;

#ifdef	IPFILTER_LOG
	case FIONREAD :
		tmp = ipf_log_bytesused(softc, IPL_LOGIPF);
		error = BCOPYOUT(&tmp, data, sizeof(tmp));
		break;
#endif

	case SIOCIPFITER :
		SPL_SCHED(s);
		error = ipf_frruleiter(softc, data, uid, ctx);
		SPL_X(s);
		break;

	case SIOCGENITER :
		SPL_SCHED(s);
		error = ipf_genericiter(softc, data, uid, ctx);
		SPL_X(s);
		break;

	case SIOCIPFDELTOK :
		error = BCOPYIN(data, &tmp, sizeof(tmp));
		if (error == 0) {
			SPL_SCHED(s);
			error = ipf_token_del(softc, tmp, uid, ctx);
			SPL_X(s);
		}
		break;

	default :
		IPFERROR(115);
		error = EINVAL;
		break;
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_decaps                                                  */
/* Returns:     int        - -1 == decapsulation failed, else bit mask of   */
/*                           flags indicating packet filtering decision.    */
/* Parameters:  fin(I)     - pointer to packet information                  */
/*              pass(I)    - IP protocol version to match                   */
/*              l5proto(I) - layer 5 protocol to decode UDP data as.        */
/*                                                                          */
/* This function is called for packets that are wrapt up in other packets,  */
/* for example, an IP packet that is the entire data segment for another IP */
/* packet.  If the basic constraints for this are satisfied, change the     */
/* buffer to point to the start of the inner packet and start processing    */
/* rules belonging to the head group this rule specifies.                   */
/* ------------------------------------------------------------------------ */
u_32_t
ipf_decaps(fin, pass, l5proto)
	fr_info_t *fin;
	u_32_t pass;
	int l5proto;
{
	fr_info_t fin2, *fino = NULL;
	int elen, hlen, nh;
	grehdr_t gre;
	ip_t *ip;
	mb_t *m;

	if ((fin->fin_flx & FI_COALESCE) == 0)
		if (ipf_coalesce(fin) == -1)
			goto cantdecaps;

	m = fin->fin_m;
	hlen = fin->fin_hlen;

	switch (fin->fin_p)
	{
	case IPPROTO_UDP :
		/*
		 * In this case, the specific protocol being decapsulated
		 * inside UDP frames comes from the rule.
		 */
		nh = fin->fin_fr->fr_icode;
		break;

	case IPPROTO_GRE :	/* 47 */
		bcopy(fin->fin_dp, (char *)&gre, sizeof(gre));
		hlen += sizeof(grehdr_t);
		if (gre.gr_R|gre.gr_s)
			goto cantdecaps;
		if (gre.gr_C)
			hlen += 4;
		if (gre.gr_K)
			hlen += 4;
		if (gre.gr_S)
			hlen += 4;

		nh = IPPROTO_IP;

		/*
		 * If the routing options flag is set, validate that it is
		 * there and bounce over it.
		 */
#if 0
		/* This is really heavy weight and lots of room for error, */
		/* so for now, put it off and get the simple stuff right.  */
		if (gre.gr_R) {
			u_char off, len, *s;
			u_short af;
			int end;

			end = 0;
			s = fin->fin_dp;
			s += hlen;
			aplen = fin->fin_plen - hlen;
			while (aplen > 3) {
				af = (s[0] << 8) | s[1];
				off = s[2];
				len = s[3];
				aplen -= 4;
				s += 4;
				if (af == 0 && len == 0) {
					end = 1;
					break;
				}
				if (aplen < len)
					break;
				s += len;
				aplen -= len;
			}
			if (end != 1)
				goto cantdecaps;
			hlen = s - (u_char *)fin->fin_dp;
		}
#endif
		break;

#ifdef IPPROTO_IPIP
	case IPPROTO_IPIP :	/* 4 */
#endif
		nh = IPPROTO_IP;
		break;

	default :	/* Includes ESP, AH is special for IPv4 */
		goto cantdecaps;
	}

	switch (nh)
	{
	case IPPROTO_IP :
	case IPPROTO_IPV6 :
		break;
	default :
		goto cantdecaps;
	}

	bcopy((char *)fin, (char *)&fin2, sizeof(fin2));
	fino = fin;
	fin = &fin2;
	elen = hlen;
#if defined(MENTAT) && defined(_KERNEL)
	m->b_rptr += elen;
#else
	m->m_data += elen;
	m->m_len -= elen;
#endif
	fin->fin_plen -= elen;

	ip = (ip_t *)((char *)fin->fin_ip + elen);

	/*
	 * Make sure we have at least enough data for the network layer
	 * header.
	 */
	if (IP_V(ip) == 4)
		hlen = IP_HL(ip) << 2;
#ifdef USE_INET6
	else if (IP_V(ip) == 6)
		hlen = sizeof(ip6_t);
#endif
	else
		goto cantdecaps2;

	if (fin->fin_plen < hlen)
		goto cantdecaps2;

	fin->fin_dp = (char *)ip + hlen;

	if (IP_V(ip) == 4) {
		/*
		 * Perform IPv4 header checksum validation.
		 */
		if (ipf_cksum((u_short *)ip, hlen))
			goto cantdecaps2;
	}

	if (ipf_makefrip(hlen, ip, fin) == -1) {
cantdecaps2:
		if (m != NULL) {
#if defined(MENTAT) && defined(_KERNEL)
			m->b_rptr -= elen;
#else
			m->m_data -= elen;
			m->m_len += elen;
#endif
		}
cantdecaps:
		DT1(frb_decapfrip, fr_info_t *, fin);
		pass &= ~FR_CMDMASK;
		pass |= FR_BLOCK|FR_QUICK;
		fin->fin_reason = FRB_DECAPFRIP;
		return -1;
	}

	pass = ipf_scanlist(fin, pass);

	/*
	 * Copy the packet filter "result" fields out of the fr_info_t struct
	 * that is local to the decapsulation processing and back into the
	 * one we were called with.
	 */
	fino->fin_flx = fin->fin_flx;
	fino->fin_rev = fin->fin_rev;
	fino->fin_icode = fin->fin_icode;
	fino->fin_rule = fin->fin_rule;
	(void) strncpy(fino->fin_group, fin->fin_group, FR_GROUPLEN);
	fino->fin_fr = fin->fin_fr;
	fino->fin_error = fin->fin_error;
	fino->fin_mp = fin->fin_mp;
	fino->fin_m = fin->fin_m;
	m = fin->fin_m;
	if (m != NULL) {
#if defined(MENTAT) && defined(_KERNEL)
		m->b_rptr -= elen;
#else
		m->m_data -= elen;
		m->m_len += elen;
#endif
	}
	return pass;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_matcharray_load                                         */
/* Returns:     int         - 0 = success, else error                       */
/* Parameters:  softc(I)    - pointer to soft context main structure        */
/*              data(I)     - pointer to ioctl data                         */
/*              objp(I)     - ipfobj_t structure to load data into          */
/*              arrayptr(I) - pointer to location to store array pointer    */
/*                                                                          */
/* This function loads in a mathing array through the ipfobj_t struct that  */
/* describes it.  Sanity checking and array size limitations are enforced   */
/* in this function to prevent userspace from trying to load in something   */
/* that is insanely big.  Once the size of the array is known, the memory   */
/* required is malloc'd and returned through changing *arrayptr.  The       */
/* contents of the array are verified before returning.  Only in the event  */
/* of a successful call is the caller required to free up the malloc area.  */
/* ------------------------------------------------------------------------ */
int
ipf_matcharray_load(softc, data, objp, arrayptr)
	ipf_main_softc_t *softc;
	caddr_t data;
	ipfobj_t *objp;
	int **arrayptr;
{
	int arraysize, *array, error;

	*arrayptr = NULL;

	error = BCOPYIN(data, objp, sizeof(*objp));
	if (error != 0) {
		IPFERROR(116);
		return EFAULT;
	}

	if (objp->ipfo_type != IPFOBJ_IPFEXPR) {
		IPFERROR(117);
		return EINVAL;
	}

	if (((objp->ipfo_size & 3) != 0) || (objp->ipfo_size == 0) ||
	    (objp->ipfo_size > 1024)) {
		IPFERROR(118);
		return EINVAL;
	}

	arraysize = objp->ipfo_size * sizeof(*array);
	KMALLOCS(array, int *, arraysize);
	if (array == NULL) {
		IPFERROR(119);
		return ENOMEM;
	}

	error = COPYIN(objp->ipfo_ptr, array, arraysize);
	if (error != 0) {
		KFREES(array, arraysize);
		IPFERROR(120);
		return EFAULT;
	}

	if (ipf_matcharray_verify(array, arraysize) != 0) {
		KFREES(array, arraysize);
		IPFERROR(121);
		return EINVAL;
	}

	*arrayptr = array;
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_matcharray_verify                                       */
/* Returns:     Nil                                                         */
/* Parameters:  array(I)     - pointer to matching array                    */
/*              arraysize(I) - number of elements in the array              */
/*                                                                          */
/* Verify the contents of a matching array by stepping through each element */
/* in it.  The actual commands in the array are not verified for            */
/* correctness, only that all of the sizes are correctly within limits.     */
/* ------------------------------------------------------------------------ */
int
ipf_matcharray_verify(array, arraysize)
	int *array, arraysize;
{
	int i, nelem, maxidx;
	ipfexp_t *e;

	nelem = arraysize / sizeof(*array);

	/*
	 * Currently, it makes no sense to have an array less than 6
	 * elements long - the initial size at the from, a single operation
	 * (minimum 4 in length) and a trailer, for a total of 6.
	 */
	if ((array[0] < 6) || (arraysize < 24) || (arraysize > 4096)) {
		return -1;
	}

	/*
	 * Verify the size of data pointed to by array with how long
	 * the array claims to be itself.
	 */
	if (array[0] * sizeof(*array) != arraysize) {
		return -1;
	}

	maxidx = nelem - 1;
	/*
	 * The last opcode in this array should be an IPF_EXP_END.
	 */
	if (array[maxidx] != IPF_EXP_END) {
		return -1;
	}

	for (i = 1; i < maxidx; ) {
		e = (ipfexp_t *)(array + i);

		/*
		 * The length of the bits to check must be at least 1
		 * (or else there is nothing to comapre with!) and it
		 * cannot exceed the length of the data present.
		 */
		if ((e->ipfe_size < 1 ) ||
		    (e->ipfe_size + i > maxidx)) {
			return -1;
		}
		i += e->ipfe_size;
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_fr_matcharray                                           */
/* Returns:     int      - 0 = match failed, else positive match            */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              array(I) - pointer to matching array                        */
/*                                                                          */
/* This function is used to apply a matching array against a packet and     */
/* return an indication of whether or not the packet successfully matches   */
/* all of the commands in it.                                               */
/* ------------------------------------------------------------------------ */
static int
ipf_fr_matcharray(fin, array)
	fr_info_t *fin;
	int *array;
{
	int i, n, *x, rv, p;
	ipfexp_t *e;

	rv = 0;
	n = array[0];
	x = array + 1;

	for (; n > 0; x += 3 + x[3], rv = 0) {
		e = (ipfexp_t *)x;
		if (e->ipfe_cmd == IPF_EXP_END)
			break;
		n -= e->ipfe_size;

		/*
		 * The upper 16 bits currently store the protocol value.
		 * This is currently used with TCP and UDP port compares and
		 * allows "tcp.port = 80" without requiring an explicit
		 " "ip.pr = tcp" first.
		 */
		p = e->ipfe_cmd >> 16;
		if ((p != 0) && (p != fin->fin_p))
			break;

		switch (e->ipfe_cmd)
		{
		case IPF_EXP_IP_PR :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (fin->fin_p == e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_IP_SRCADDR :
			if (fin->fin_v != 4)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= ((fin->fin_saddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]);
			}
			break;

		case IPF_EXP_IP_DSTADDR :
			if (fin->fin_v != 4)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= ((fin->fin_daddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]);
			}
			break;

		case IPF_EXP_IP_ADDR :
			if (fin->fin_v != 4)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= ((fin->fin_saddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]) ||
				      ((fin->fin_daddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]);
			}
			break;

#ifdef USE_INET6
		case IPF_EXP_IP6_SRCADDR :
			if (fin->fin_v != 6)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= IP6_MASKEQ(&fin->fin_src6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]);
			}
			break;

		case IPF_EXP_IP6_DSTADDR :
			if (fin->fin_v != 6)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= IP6_MASKEQ(&fin->fin_dst6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]);
			}
			break;

		case IPF_EXP_IP6_ADDR :
			if (fin->fin_v != 6)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= IP6_MASKEQ(&fin->fin_src6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]) ||
				      IP6_MASKEQ(&fin->fin_dst6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]);
			}
			break;
#endif

		case IPF_EXP_UDP_PORT :
		case IPF_EXP_TCP_PORT :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (fin->fin_sport == e->ipfe_arg0[i]) ||
				      (fin->fin_dport == e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_UDP_SPORT :
		case IPF_EXP_TCP_SPORT :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (fin->fin_sport == e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_UDP_DPORT :
		case IPF_EXP_TCP_DPORT :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (fin->fin_dport == e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_TCP_FLAGS :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= ((fin->fin_tcpf &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]);
			}
			break;
		}
		rv ^= e->ipfe_not;

		if (rv == 0)
			break;
	}

	return rv;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_queueflush                                              */
/* Returns:     int - number of entries flushed (0 = none)                  */
/* Parameters:  softc(I)    - pointer to soft context main structure        */
/*              deletefn(I) - function to call to delete entry              */
/*              ipfqs(I)    - top of the list of ipf internal queues        */
/*              userqs(I)   - top of the list of user defined timeouts      */
/*                                                                          */
/* This fucntion gets called when the state/NAT hash tables fill up and we  */
/* need to try a bit harder to free up some space.  The algorithm used here */
/* split into two parts but both halves have the same goal: to reduce the   */
/* number of connections considered to be "active" to the low watermark.    */
/* There are two steps in doing this:                                       */
/* 1) Remove any TCP connections that are already considered to be "closed" */
/*    but have not yet been removed from the state table.  The two states   */
/*    TCPS_TIME_WAIT and TCPS_CLOSED are considered to be the perfect       */
/*    candidates for this style of removal.  If freeing up entries in       */
/*    CLOSED or both CLOSED and TIME_WAIT brings us to the low watermark,   */
/*    we do not go on to step 2.                                            */
/*                                                                          */
/* 2) Look for the oldest entries on each timeout queue and free them if    */
/*    they are within the given window we are considering.  Where the       */
/*    window starts and the steps taken to increase its size depend upon    */
/*    how long ipf has been running (ipf_ticks.)  Anything modified in the  */
/*    last 30 seconds is not touched.                                       */
/*                                              touched                     */
/*         die     ipf_ticks  30*1.5    1800*1.5   |  43200*1.5             */
/*           |          |        |           |     |     |                  */
/* future <--+----------+--------+-----------+-----+-----+-----------> past */
/*                     now        \_int=30s_/ \_int=1hr_/ \_int=12hr        */
/*                                                                          */
/* Points to note:                                                          */
/* - tqe_die is the time, in the future, when entries die.                  */
/* - tqe_die - ipf_ticks is how long left the connection has to live in ipf */
/*   ticks.                                                                 */
/* - tqe_touched is when the entry was last used by NAT/state               */
/* - the closer tqe_touched is to ipf_ticks, the further tqe_die will be    */
/*   ipf_ticks any given timeout queue and vice versa.                      */
/* - both tqe_die and tqe_touched increase over time                        */
/* - timeout queues are sorted with the highest value of tqe_die at the     */
/*   bottom and therefore the smallest values of each are at the top        */
/* - the pointer passed in as ipfqs should point to an array of timeout     */
/*   queues representing each of the TCP states                             */
/*                                                                          */
/* We start by setting up a maximum range to scan for things to move of     */
/* iend (newest) to istart (oldest) in chunks of "interval".  If nothing is */
/* found in that range, "interval" is adjusted (so long as it isn't 30) and */
/* we start again with a new value for "iend" and "istart".  This is        */
/* continued until we either finish the scan of 30 second intervals or the  */
/* low water mark is reached.                                               */
/* ------------------------------------------------------------------------ */
int
ipf_queueflush(softc, deletefn, ipfqs, userqs, activep, size, low)
	ipf_main_softc_t *softc;
	ipftq_delete_fn_t deletefn;
	ipftq_t *ipfqs, *userqs;
	u_int *activep;
	int size, low;
{
	u_long interval, istart, iend;
	ipftq_t *ifq, *ifqnext;
	ipftqent_t *tqe, *tqn;
	int removed = 0;

	for (tqn = ipfqs[IPF_TCPS_CLOSED].ifq_head; ((tqe = tqn) != NULL); ) {
		tqn = tqe->tqe_next;
		if ((*deletefn)(softc, tqe->tqe_parent) == 0)
			removed++;
	}
	if ((*activep * 100 / size) > low) {
		for (tqn = ipfqs[IPF_TCPS_TIME_WAIT].ifq_head;
		     ((tqe = tqn) != NULL); ) {
			tqn = tqe->tqe_next;
			if ((*deletefn)(softc, tqe->tqe_parent) == 0)
				removed++;
		}
	}

	if ((*activep * 100 / size) <= low) {
		return removed;
	}

	/*
	 * NOTE: Use of "* 15 / 10" is required here because if "* 1.5" is
	 *       used then the operations are upgraded to floating point
	 *       and kernels don't like floating point...
	 */
	if (softc->ipf_ticks > IPF_TTLVAL(43200 * 15 / 10)) {
		istart = IPF_TTLVAL(86400 * 4);
		interval = IPF_TTLVAL(43200);
	} else if (softc->ipf_ticks > IPF_TTLVAL(1800 * 15 / 10)) {
		istart = IPF_TTLVAL(43200);
		interval = IPF_TTLVAL(1800);
	} else if (softc->ipf_ticks > IPF_TTLVAL(30 * 15 / 10)) {
		istart = IPF_TTLVAL(1800);
		interval = IPF_TTLVAL(30);
	} else {
		return 0;
	}
	if (istart > softc->ipf_ticks) {
		if (softc->ipf_ticks - interval < interval)
			istart = interval;
		else
			istart = (softc->ipf_ticks / interval) * interval;
	}

	iend = softc->ipf_ticks - interval;

	while ((*activep * 100 / size) > low) {
		u_long try;

		try = softc->ipf_ticks - istart;

		for (ifq = ipfqs; ifq != NULL; ifq = ifq->ifq_next) {
			for (tqn = ifq->ifq_head; ((tqe = tqn) != NULL); ) {
				if (try < tqe->tqe_touched)
					break;
				tqn = tqe->tqe_next;
				if ((*deletefn)(softc, tqe->tqe_parent) == 0)
					removed++;
			}
		}

		for (ifq = userqs; ifq != NULL; ifq = ifqnext) {
			ifqnext = ifq->ifq_next;

			for (tqn = ifq->ifq_head; ((tqe = tqn) != NULL); ) {
				if (try < tqe->tqe_touched)
					break;
				tqn = tqe->tqe_next;
				if ((*deletefn)(softc, tqe->tqe_parent) == 0)
					removed++;
			}
		}

		if (try >= iend) {
			if (interval == IPF_TTLVAL(43200)) {
				interval = IPF_TTLVAL(1800);
			} else if (interval == IPF_TTLVAL(1800)) {
				interval = IPF_TTLVAL(30);
			} else {
				break;
			}
			if (interval >= softc->ipf_ticks)
				break;

			iend = softc->ipf_ticks - interval;
		}
		istart -= interval;
	}

	return removed;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_deliverlocal                                            */
/* Returns:     int - 1 = local address, 0 = non-local address              */
/* Parameters:  softc(I)     - pointer to soft context main structure       */
/*              ipversion(I) - IP protocol version (4 or 6)                 */
/*              ifp(I)       - network interface pointer                    */
/*              ipaddr(I)    - IPv4/6 destination address                   */
/*                                                                          */
/* This fucntion is used to determine in the address "ipaddr" belongs to    */
/* the network interface represented by ifp.                                */
/* ------------------------------------------------------------------------ */
int
ipf_deliverlocal(softc, ipversion, ifp, ipaddr)
	ipf_main_softc_t *softc;
	int ipversion;
	void *ifp;
	i6addr_t *ipaddr;
{
	i6addr_t addr;
	int islocal = 0;

	if (ipversion == 4) {
		if (ipf_ifpaddr(softc, 4, FRI_NORMAL, ifp, &addr, NULL) == 0) {
			if (addr.in4.s_addr == ipaddr->in4.s_addr)
				islocal = 1;
		}

#ifdef USE_INET6
	} else if (ipversion == 6) {
		if (ipf_ifpaddr(softc, 6, FRI_NORMAL, ifp, &addr, NULL) == 0) {
			if (IP6_EQ(&addr, ipaddr))
				islocal = 1;
		}
#endif
	}

	return islocal;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_settimeout                                              */
/* Returns:     int - 0 = success, -1 = failure                             */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              t(I)     - pointer to tuneable array entry                  */
/*              p(I)     - pointer to values passed in to apply             */
/*                                                                          */
/* This function is called to set the timeout values for each distinct      */
/* queue timeout that is available.  When called, it calls into both the    */
/* state and NAT code, telling them to update their timeout queues.         */
/* ------------------------------------------------------------------------ */
static int
ipf_settimeout(softc, t, p)
	struct ipf_main_softc_s *softc;
	ipftuneable_t *t;
	ipftuneval_t *p;
{

	/*
	 * ipf_interror should be set by the functions called here, not
	 * by this function - it's just a middle man.
	 */
	if (ipf_state_settimeout(softc, t, p) == -1)
		return -1;
	if (ipf_nat_settimeout(softc, t, p) == -1)
		return -1;
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_apply_timeout                                           */
/* Returns:     int - 0 = success, -1 = failure                             */
/* Parameters:  head(I)    - pointer to tuneable array entry                */
/*              seconds(I) - pointer to values passed in to apply           */
/*                                                                          */
/* This function applies a timeout of "seconds" to the timeout queue that   */
/* is pointed to by "head".  All entries on this list have an expiration    */
/* set to be the current tick value of ipf plus the ttl.  Given that this   */
/* function should only be called when the delta is non-zero, the task is   */
/* to walk the entire list and apply the change.  The sort order will not   */
/* change.  The only catch is that this is O(n) across the list, so if the  */
/* queue has lots of entries (10s of thousands or 100s of thousands), it    */
/* could take a relatively long time to work through them all.              */
/* ------------------------------------------------------------------------ */
void
ipf_apply_timeout(head, seconds)
	ipftq_t *head;
	u_int seconds;
{
	u_int oldtimeout, newtimeout;
	ipftqent_t *tqe;
	int delta;

	MUTEX_ENTER(&head->ifq_lock);
	oldtimeout = head->ifq_ttl;
	newtimeout = IPF_TTLVAL(seconds);
	delta = oldtimeout - newtimeout;

	head->ifq_ttl = newtimeout;

	for (tqe = head->ifq_head; tqe != NULL; tqe = tqe->tqe_next) {
		tqe->tqe_die += delta;
	}
	MUTEX_EXIT(&head->ifq_lock);
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_settimeout_tcp                                           */
/* Returns:    int - 0 = successfully applied, -1 = failed                  */
/* Parameters: t(I)   - pointer to tuneable to change                       */
/*             p(I)   - pointer to new timeout information                  */
/*             tab(I) - pointer to table of TCP queues                      */
/*                                                                          */
/* This function applies the new timeout (p) to the TCP tunable (t) and     */
/* updates all of the entries on the relevant timeout queue by calling      */
/* ipf_apply_timeout().                                                     */
/* ------------------------------------------------------------------------ */
int
ipf_settimeout_tcp(t, p, tab)
	ipftuneable_t *t;
	ipftuneval_t *p;
	ipftq_t *tab;
{
	if (!strcmp(t->ipft_name, "tcp_idle_timeout") ||
	    !strcmp(t->ipft_name, "tcp_established")) {
		ipf_apply_timeout(&tab[IPF_TCPS_ESTABLISHED], p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "tcp_close_wait")) {
		ipf_apply_timeout(&tab[IPF_TCPS_CLOSE_WAIT], p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "tcp_last_ack")) {
		ipf_apply_timeout(&tab[IPF_TCPS_LAST_ACK], p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "tcp_timeout")) {
		ipf_apply_timeout(&tab[IPF_TCPS_LISTEN], p->ipftu_int);
		ipf_apply_timeout(&tab[IPF_TCPS_HALF_ESTAB], p->ipftu_int);
		ipf_apply_timeout(&tab[IPF_TCPS_CLOSING], p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "tcp_listen")) {
		ipf_apply_timeout(&tab[IPF_TCPS_LISTEN], p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "tcp_half_established")) {
		ipf_apply_timeout(&tab[IPF_TCPS_HALF_ESTAB], p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "tcp_closing")) {
		ipf_apply_timeout(&tab[IPF_TCPS_CLOSING], p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "tcp_syn_received")) {
		ipf_apply_timeout(&tab[IPF_TCPS_SYN_RECEIVED], p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "tcp_syn_sent")) {
		ipf_apply_timeout(&tab[IPF_TCPS_SYN_SENT], p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "tcp_closed")) {
		ipf_apply_timeout(&tab[IPF_TCPS_CLOSED], p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "tcp_half_closed")) {
		ipf_apply_timeout(&tab[IPF_TCPS_CLOSED], p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "tcp_time_wait")) {
		ipf_apply_timeout(&tab[IPF_TCPS_TIME_WAIT], p->ipftu_int);
	} else {
		/*
		 * ipf_interror isn't set here because it should be set
		 * by whatever called this function.
		 */
		return -1;
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_main_soft_create                                         */
/* Returns:    NULL = failure, else success                                 */
/* Parameters: arg(I) - pointer to soft context structure if already allocd */
/*                                                                          */
/* Create the foundation soft context structure. In circumstances where it  */
/* is not required to dynamically allocate the context, a pointer can be    */
/* passed in (rather than NULL) to a structure to be initialised.           */
/* The main thing of interest is that a number of locks are initialised     */
/* here instead of in the where might be expected - in the relevant create  */
/* function elsewhere.  This is done because the current locking design has */
/* some areas where these locks are used outside of their module.           */
/* Possibly the most important exercise that is done here is setting of all */
/* the timeout values, allowing them to be changed before init().           */
/* ------------------------------------------------------------------------ */
void *
ipf_main_soft_create(arg)
	void *arg;
{
	ipf_main_softc_t *softc;

	if (arg == NULL) {
		KMALLOC(softc, ipf_main_softc_t *);
		if (softc == NULL)
			return NULL;
	} else {
		softc = arg;
	}

	bzero((char *)softc, sizeof(*softc));

	/*
	 * This serves as a flag as to whether or not the softc should be
	 * free'd when _destroy is called.
	 */
	softc->ipf_dynamic_softc = (arg == NULL) ? 1 : 0;

	softc->ipf_tuners = ipf_tune_array_copy(softc,
						sizeof(ipf_main_tuneables),
						ipf_main_tuneables);
	if (softc->ipf_tuners == NULL) {
		ipf_main_soft_destroy(softc);
		return NULL;
	}

	MUTEX_INIT(&softc->ipf_rw, "ipf rw mutex");
	MUTEX_INIT(&softc->ipf_timeoutlock, "ipf timeout lock");
	RWLOCK_INIT(&softc->ipf_global, "ipf filter load/unload mutex");
	RWLOCK_INIT(&softc->ipf_mutex, "ipf filter rwlock");
	RWLOCK_INIT(&softc->ipf_tokens, "ipf token rwlock");
	RWLOCK_INIT(&softc->ipf_state, "ipf state rwlock");
	RWLOCK_INIT(&softc->ipf_nat, "ipf IP NAT rwlock");
	RWLOCK_INIT(&softc->ipf_poolrw, "ipf pool rwlock");
	RWLOCK_INIT(&softc->ipf_frag, "ipf frag rwlock");

	softc->ipf_token_head = NULL;
	softc->ipf_token_tail = &softc->ipf_token_head;

	softc->ipf_tcpidletimeout = FIVE_DAYS;
	softc->ipf_tcpclosewait = IPF_TTLVAL(2 * TCP_MSL);
	softc->ipf_tcplastack = IPF_TTLVAL(30);
	softc->ipf_tcptimewait = IPF_TTLVAL(2 * TCP_MSL);
	softc->ipf_tcptimeout = IPF_TTLVAL(2 * TCP_MSL);
	softc->ipf_tcpsynsent = IPF_TTLVAL(2 * TCP_MSL);
	softc->ipf_tcpsynrecv = IPF_TTLVAL(2 * TCP_MSL);
	softc->ipf_tcpclosed = IPF_TTLVAL(30);
	softc->ipf_tcphalfclosed = IPF_TTLVAL(2 * 3600);
	softc->ipf_udptimeout = IPF_TTLVAL(120);
	softc->ipf_udpacktimeout = IPF_TTLVAL(12);
	softc->ipf_icmptimeout = IPF_TTLVAL(60);
	softc->ipf_icmpacktimeout = IPF_TTLVAL(6);
	softc->ipf_iptimeout = IPF_TTLVAL(60);

#if defined(IPFILTER_DEFAULT_BLOCK)
	softc->ipf_pass = FR_BLOCK|FR_NOMATCH;
#else
	softc->ipf_pass = (IPF_DEFAULT_PASS)|FR_NOMATCH;
#endif
	softc->ipf_minttl = 4;
	softc->ipf_icmpminfragmtu = 68;
	softc->ipf_flags = IPF_LOGGING;

	return softc;
}

/* ------------------------------------------------------------------------ */
/* Function:   ipf_main_soft_init                                           */
/* Returns:    0 = success, -1 = failure                                    */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*                                                                          */
/* A null-op function that exists as a placeholder so that the flow in      */
/* other functions is obvious.                                              */
/* ------------------------------------------------------------------------ */
/*ARGSUSED*/
int
ipf_main_soft_init(softc)
	ipf_main_softc_t *softc;
{
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_main_soft_destroy                                        */
/* Returns:    void                                                         */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*                                                                          */
/* Undo everything that we did in ipf_main_soft_create.                     */
/*                                                                          */
/* The most important check that needs to be made here is whether or not    */
/* the structure was allocated by ipf_main_soft_create() by checking what   */
/* value is stored in ipf_dynamic_main.                                     */
/* ------------------------------------------------------------------------ */
/*ARGSUSED*/
void
ipf_main_soft_destroy(softc)
	ipf_main_softc_t *softc;
{

	RW_DESTROY(&softc->ipf_frag);
	RW_DESTROY(&softc->ipf_poolrw);
	RW_DESTROY(&softc->ipf_nat);
	RW_DESTROY(&softc->ipf_state);
	RW_DESTROY(&softc->ipf_tokens);
	RW_DESTROY(&softc->ipf_mutex);
	RW_DESTROY(&softc->ipf_global);
	MUTEX_DESTROY(&softc->ipf_timeoutlock);
	MUTEX_DESTROY(&softc->ipf_rw);

	if (softc->ipf_tuners != NULL) {
		KFREES(softc->ipf_tuners, sizeof(ipf_main_tuneables));
	}
	if (softc->ipf_dynamic_softc == 1) {
		KFREE(softc);
	}
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_main_soft_fini                                           */
/* Returns:    0 = success, -1 = failure                                    */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*                                                                          */
/* Clean out the rules which have been added since _init was last called,   */
/* the only dynamic part of the mainline.                                   */
/* ------------------------------------------------------------------------ */
int
ipf_main_soft_fini(softc)
	ipf_main_softc_t *softc;
{
	(void) ipf_flush(softc, IPL_LOGIPF, FR_INQUE|FR_OUTQUE|FR_INACTIVE);
	(void) ipf_flush(softc, IPL_LOGIPF, FR_INQUE|FR_OUTQUE);
	(void) ipf_flush(softc, IPL_LOGCOUNT, FR_INQUE|FR_OUTQUE|FR_INACTIVE);
	(void) ipf_flush(softc, IPL_LOGCOUNT, FR_INQUE|FR_OUTQUE);

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_main_load                                                */
/* Returns:    0 = success, -1 = failure                                    */
/* Parameters: none                                                         */
/*                                                                          */
/* Handle global initialisation that needs to be done for the base part of  */
/* IPFilter. At present this just amounts to initialising some ICMP lookup  */
/* arrays that get used by the state/NAT code.                              */
/* ------------------------------------------------------------------------ */
int
ipf_main_load()
{
	int i;

	/* fill icmp reply type table */
	for (i = 0; i <= ICMP_MAXTYPE; i++)
		icmpreplytype4[i] = -1;
	icmpreplytype4[ICMP_ECHO] = ICMP_ECHOREPLY;
	icmpreplytype4[ICMP_TSTAMP] = ICMP_TSTAMPREPLY;
	icmpreplytype4[ICMP_IREQ] = ICMP_IREQREPLY;
	icmpreplytype4[ICMP_MASKREQ] = ICMP_MASKREPLY;

#ifdef  USE_INET6
	/* fill icmp reply type table */
	for (i = 0; i <= ICMP6_MAXTYPE; i++)
		icmpreplytype6[i] = -1;
	icmpreplytype6[ICMP6_ECHO_REQUEST] = ICMP6_ECHO_REPLY;
	icmpreplytype6[ICMP6_MEMBERSHIP_QUERY] = ICMP6_MEMBERSHIP_REPORT;
	icmpreplytype6[ICMP6_NI_QUERY] = ICMP6_NI_REPLY;
	icmpreplytype6[ND_ROUTER_SOLICIT] = ND_ROUTER_ADVERT;
	icmpreplytype6[ND_NEIGHBOR_SOLICIT] = ND_NEIGHBOR_ADVERT;
#endif

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_main_unload                                              */
/* Returns:    0 = success, -1 = failure                                    */
/* Parameters: none                                                         */
/*                                                                          */
/* A null-op function that exists as a placeholder so that the flow in      */
/* other functions is obvious.                                              */
/* ------------------------------------------------------------------------ */
int
ipf_main_unload()
{
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_load_all                                                 */
/* Returns:    0 = success, -1 = failure                                    */
/* Parameters: none                                                         */
/*                                                                          */
/* Work through all of the subsystems inside IPFilter and call the load     */
/* function for each in an order that won't lead to a crash :)              */
/* ------------------------------------------------------------------------ */
int
ipf_load_all()
{
	if (ipf_main_load() == -1)
		return -1;

	if (ipf_state_main_load() == -1)
		return -1;

	if (ipf_nat_main_load() == -1)
		return -1;

	if (ipf_frag_main_load() == -1)
		return -1;

	if (ipf_auth_main_load() == -1)
		return -1;

	if (ipf_proxy_main_load() == -1)
		return -1;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_unload_all                                               */
/* Returns:    0 = success, -1 = failure                                    */
/* Parameters: none                                                         */
/*                                                                          */
/* Work through all of the subsystems inside IPFilter and call the unload   */
/* function for each in an order that won't lead to a crash :)              */
/* ------------------------------------------------------------------------ */
int
ipf_unload_all()
{
	if (ipf_proxy_main_unload() == -1)
		return -1;

	if (ipf_auth_main_unload() == -1)
		return -1;

	if (ipf_frag_main_unload() == -1)
		return -1;

	if (ipf_nat_main_unload() == -1)
		return -1;

	if (ipf_state_main_unload() == -1)
		return -1;

	if (ipf_main_unload() == -1)
		return -1;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_create_all                                               */
/* Returns:    NULL = failure, else success                                 */
/* Parameters: arg(I) - pointer to soft context main structure              */
/*                                                                          */
/* Work through all of the subsystems inside IPFilter and call the create   */
/* function for each in an order that won't lead to a crash :)              */
/* ------------------------------------------------------------------------ */
ipf_main_softc_t *
ipf_create_all(arg)
	void *arg;
{
	ipf_main_softc_t *softc;

	softc = ipf_main_soft_create(arg);
	if (softc == NULL)
		return NULL;

#ifdef IPFILTER_LOG
	softc->ipf_log_soft = ipf_log_soft_create(softc);
	if (softc->ipf_log_soft == NULL) {
		ipf_destroy_all(softc);
		return NULL;
	}
#endif

	softc->ipf_lookup_soft = ipf_lookup_soft_create(softc);
	if (softc->ipf_lookup_soft == NULL) {
		ipf_destroy_all(softc);
		return NULL;
	}

	softc->ipf_sync_soft = ipf_sync_soft_create(softc);
	if (softc->ipf_sync_soft == NULL) {
		ipf_destroy_all(softc);
		return NULL;
	}

	softc->ipf_state_soft = ipf_state_soft_create(softc);
	if (softc->ipf_state_soft == NULL) {
		ipf_destroy_all(softc);
		return NULL;
	}

	softc->ipf_nat_soft = ipf_nat_soft_create(softc);
	if (softc->ipf_nat_soft == NULL) {
		ipf_destroy_all(softc);
		return NULL;
	}

	softc->ipf_frag_soft = ipf_frag_soft_create(softc);
	if (softc->ipf_frag_soft == NULL) {
		ipf_destroy_all(softc);
		return NULL;
	}

	softc->ipf_auth_soft = ipf_auth_soft_create(softc);
	if (softc->ipf_auth_soft == NULL) {
		ipf_destroy_all(softc);
		return NULL;
	}

	softc->ipf_proxy_soft = ipf_proxy_soft_create(softc);
	if (softc->ipf_proxy_soft == NULL) {
		ipf_destroy_all(softc);
		return NULL;
	}

	return softc;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_destroy_all                                              */
/* Returns:    void                                                         */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*                                                                          */
/* Work through all of the subsystems inside IPFilter and call the destroy  */
/* function for each in an order that won't lead to a crash :)              */
/*                                                                          */
/* Every one of these functions is expected to succeed, so there is no      */
/* checking of return values.                                               */
/* ------------------------------------------------------------------------ */
void
ipf_destroy_all(softc)
	ipf_main_softc_t *softc;
{

	if (softc->ipf_state_soft != NULL) {
		ipf_state_soft_destroy(softc, softc->ipf_state_soft);
		softc->ipf_state_soft = NULL;
	}

	if (softc->ipf_nat_soft != NULL) {
		ipf_nat_soft_destroy(softc, softc->ipf_nat_soft);
		softc->ipf_nat_soft = NULL;
	}

	if (softc->ipf_frag_soft != NULL) {
		ipf_frag_soft_destroy(softc, softc->ipf_frag_soft);
		softc->ipf_frag_soft = NULL;
	}

	if (softc->ipf_auth_soft != NULL) {
		ipf_auth_soft_destroy(softc, softc->ipf_auth_soft);
		softc->ipf_auth_soft = NULL;
	}

	if (softc->ipf_proxy_soft != NULL) {
		ipf_proxy_soft_destroy(softc, softc->ipf_proxy_soft);
		softc->ipf_proxy_soft = NULL;
	}

	if (softc->ipf_sync_soft != NULL) {
		ipf_sync_soft_destroy(softc, softc->ipf_sync_soft);
		softc->ipf_sync_soft = NULL;
	}

	if (softc->ipf_lookup_soft != NULL) {
		ipf_lookup_soft_destroy(softc, softc->ipf_lookup_soft);
		softc->ipf_lookup_soft = NULL;
	}

#ifdef IPFILTER_LOG
	if (softc->ipf_log_soft != NULL) {
		ipf_log_soft_destroy(softc, softc->ipf_log_soft);
		softc->ipf_log_soft = NULL;
	}
#endif

	ipf_main_soft_destroy(softc);
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_init_all                                                 */
/* Returns:    0 = success, -1 = failure                                    */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*                                                                          */
/* Work through all of the subsystems inside IPFilter and call the init     */
/* function for each in an order that won't lead to a crash :)              */
/* ------------------------------------------------------------------------ */
int
ipf_init_all(softc)
	ipf_main_softc_t *softc;
{

	if (ipf_main_soft_init(softc) == -1)
		return -1;

#ifdef IPFILTER_LOG
	if (ipf_log_soft_init(softc, softc->ipf_log_soft) == -1)
		return -1;
#endif

	if (ipf_lookup_soft_init(softc, softc->ipf_lookup_soft) == -1)
		return -1;

	if (ipf_sync_soft_init(softc, softc->ipf_sync_soft) == -1)
		return -1;

	if (ipf_state_soft_init(softc, softc->ipf_state_soft) == -1)
		return -1;

	if (ipf_nat_soft_init(softc, softc->ipf_nat_soft) == -1)
		return -1;

	if (ipf_frag_soft_init(softc, softc->ipf_frag_soft) == -1)
		return -1;

	if (ipf_auth_soft_init(softc, softc->ipf_auth_soft) == -1)
		return -1;

	if (ipf_proxy_soft_init(softc, softc->ipf_proxy_soft) == -1)
		return -1;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_fini_all                                                 */
/* Returns:    0 = success, -1 = failure                                    */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*                                                                          */
/* Work through all of the subsystems inside IPFilter and call the fini     */
/* function for each in an order that won't lead to a crash :)              */
/* ------------------------------------------------------------------------ */
int
ipf_fini_all(softc)
	ipf_main_softc_t *softc;
{

	ipf_token_flush(softc);

	if (ipf_proxy_soft_fini(softc, softc->ipf_proxy_soft) == -1)
		return -1;

	if (ipf_auth_soft_fini(softc, softc->ipf_auth_soft) == -1)
		return -1;

	if (ipf_frag_soft_fini(softc, softc->ipf_frag_soft) == -1)
		return -1;

	if (ipf_nat_soft_fini(softc, softc->ipf_nat_soft) == -1)
		return -1;

	if (ipf_state_soft_fini(softc, softc->ipf_state_soft) == -1)
		return -1;

	if (ipf_sync_soft_fini(softc, softc->ipf_sync_soft) == -1)
		return -1;

	if (ipf_lookup_soft_fini(softc, softc->ipf_lookup_soft) == -1)
		return -1;

#ifdef IPFILTER_LOG
	if (ipf_log_soft_fini(softc, softc->ipf_log_soft) == -1)
		return -1;
#endif

	if (ipf_main_soft_fini(softc) == -1)
		return -1;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_rule_expire                                             */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* At present this function exists just to support temporary addition of    */
/* firewall rules. Both inactive and active lists are scanned for items to  */
/* purge, as by rights, the expiration is computed as soon as the rule is   */
/* loaded in.                                                               */
/* ------------------------------------------------------------------------ */
void
ipf_rule_expire(softc)
	ipf_main_softc_t *softc;
{
	frentry_t *fr;

	if ((softc->ipf_rule_explist[0] == NULL) &&
	    (softc->ipf_rule_explist[1] == NULL))
		return;

	WRITE_ENTER(&softc->ipf_mutex);

	while ((fr = softc->ipf_rule_explist[0]) != NULL) {
		/*
		 * Because the list is kept sorted on insertion, the fist
		 * one that dies in the future means no more work to do.
		 */
		if (fr->fr_die > softc->ipf_ticks)
			break;
		ipf_rule_delete(softc, fr, IPL_LOGIPF, 0);
	}

	while ((fr = softc->ipf_rule_explist[1]) != NULL) {
		/*
		 * Because the list is kept sorted on insertion, the fist
		 * one that dies in the future means no more work to do.
		 */
		if (fr->fr_die > softc->ipf_ticks)
			break;
		ipf_rule_delete(softc, fr, IPL_LOGIPF, 1);
	}

	RWLOCK_EXIT(&softc->ipf_mutex);
}


static int ipf_ht_node_cmp __P((struct host_node_s *, struct host_node_s *));
static void ipf_ht_node_make_key __P((host_track_t *, host_node_t *, int,
				      i6addr_t *));

host_node_t RBI_ZERO(ipf_rb);
RBI_CODE(ipf_rb, host_node_t, hn_entry, ipf_ht_node_cmp)


/* ------------------------------------------------------------------------ */
/* Function:    ipf_ht_node_cmp                                             */
/* Returns:     int   - 0 == nodes are the same, ..                         */
/* Parameters:  k1(I) - pointer to first key to compare                     */
/*              k2(I) - pointer to second key to compare                    */
/*                                                                          */
/* The "key" for the node is a combination of two fields: the address       */
/* family and the address itself.                                           */
/*                                                                          */
/* Because we're not actually interpreting the address data, it isn't       */
/* necessary to convert them to/from network/host byte order. The mask is   */
/* just used to remove bits that aren't significant - it doesn't matter     */
/* where they are, as long as they're always in the same place.             */
/*                                                                          */
/* As with IP6_EQ, comparing IPv6 addresses starts at the bottom because    */
/* this is where individual ones will differ the most - but not true for    */
/* for /48's, etc.                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_ht_node_cmp(k1, k2)
	struct host_node_s *k1, *k2;
{
	int i;

	i = (k2->hn_addr.adf_family - k1->hn_addr.adf_family);
	if (i != 0)
		return i;

	if (k1->hn_addr.adf_family == AF_INET)
		return (k2->hn_addr.adf_addr.in4.s_addr -
			k1->hn_addr.adf_addr.in4.s_addr);

	i = k2->hn_addr.adf_addr.i6[3] - k1->hn_addr.adf_addr.i6[3];
	if (i != 0)
		return i;
	i = k2->hn_addr.adf_addr.i6[2] - k1->hn_addr.adf_addr.i6[2];
	if (i != 0)
		return i;
	i = k2->hn_addr.adf_addr.i6[1] - k1->hn_addr.adf_addr.i6[1];
	if (i != 0)
		return i;
	i = k2->hn_addr.adf_addr.i6[0] - k1->hn_addr.adf_addr.i6[0];
	return i;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_ht_node_make_key                                        */
/* Returns:     Nil                                                         */
/* parameters:  htp(I)    - pointer to address tracking structure           */
/*              key(I)    - where to store masked address for lookup        */
/*              family(I) - protocol family of address                      */
/*              addr(I)   - pointer to network address                      */
/*                                                                          */
/* Using the "netmask" (number of bits) stored parent host tracking struct, */
/* copy the address passed in into the key structure whilst masking out the */
/* bits that we don't want.                                                 */
/*                                                                          */
/* Because the parser will set ht_netmask to 128 if there is no protocol    */
/* specified (the parser doesn't know if it should be a v4 or v6 rule), we  */
/* have to be wary of that and not allow 32-128 to happen.                  */
/* ------------------------------------------------------------------------ */
static void
ipf_ht_node_make_key(htp, key, family, addr)
	host_track_t *htp;
	host_node_t *key;
	int family;
	i6addr_t *addr;
{
	key->hn_addr.adf_family = family;
	if (family == AF_INET) {
		u_32_t mask;
		int bits;

		key->hn_addr.adf_len = sizeof(key->hn_addr.adf_addr.in4);
		bits = htp->ht_netmask;
		if (bits >= 32) {
			mask = 0xffffffff;
		} else {
			mask = htonl(0xffffffff << (32 - bits));
		}
		key->hn_addr.adf_addr.in4.s_addr = addr->in4.s_addr & mask;
#ifdef USE_INET6
	} else {
		int bits = htp->ht_netmask;

		key->hn_addr.adf_len = sizeof(key->hn_addr.adf_addr.in6);
		if (bits > 96) {
			key->hn_addr.adf_addr.i6[3] = addr->i6[3] &
					     htonl(0xffffffff << (128 - bits));
			key->hn_addr.adf_addr.i6[2] = addr->i6[2];
			key->hn_addr.adf_addr.i6[1] = addr->i6[2];
			key->hn_addr.adf_addr.i6[0] = addr->i6[2];
		} else if (bits > 64) {
			key->hn_addr.adf_addr.i6[3] = 0;
			key->hn_addr.adf_addr.i6[2] = addr->i6[2] &
					     htonl(0xffffffff << (96 - bits));
			key->hn_addr.adf_addr.i6[1] = addr->i6[1];
			key->hn_addr.adf_addr.i6[0] = addr->i6[0];
		} else if (bits > 32) {
			key->hn_addr.adf_addr.i6[3] = 0;
			key->hn_addr.adf_addr.i6[2] = 0;
			key->hn_addr.adf_addr.i6[1] = addr->i6[1] &
					     htonl(0xffffffff << (64 - bits));
			key->hn_addr.adf_addr.i6[0] = addr->i6[0];
		} else {
			key->hn_addr.adf_addr.i6[3] = 0;
			key->hn_addr.adf_addr.i6[2] = 0;
			key->hn_addr.adf_addr.i6[1] = 0;
			key->hn_addr.adf_addr.i6[0] = addr->i6[0] &
					     htonl(0xffffffff << (32 - bits));
		}
#endif
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_ht_node_add                                             */
/* Returns:     int       - 0 == success,  -1 == failure                    */
/* Parameters:  softc(I)  - pointer to soft context main structure          */
/*              htp(I)    - pointer to address tracking structure           */
/*              family(I) - protocol family of address                      */
/*              addr(I)   - pointer to network address                      */
/*                                                                          */
/* NOTE: THIS FUNCTION MUST BE CALLED WITH AN EXCLUSIVE LOCK THAT PREVENTS  */
/*       ipf_ht_node_del FROM RUNNING CONCURRENTLY ON THE SAME htp.         */
/*                                                                          */
/* After preparing the key with the address information to find, look in    */
/* the red-black tree to see if the address is known. A successful call to  */
/* this function can mean one of two things: a new node was added to the    */
/* tree or a matching node exists and we're able to bump up its activity.   */
/* ------------------------------------------------------------------------ */
int
ipf_ht_node_add(softc, htp, family, addr)
	ipf_main_softc_t *softc;
	host_track_t *htp;
	int family;
	i6addr_t *addr;
{
	host_node_t *h;
	host_node_t k;

	ipf_ht_node_make_key(htp, &k, family, addr);

	h = RBI_SEARCH(ipf_rb, &htp->ht_root, &k);
	if (h == NULL) {
		if (htp->ht_cur_nodes >= htp->ht_max_nodes)
			return -1;
		KMALLOC(h, host_node_t *);
		if (h == NULL) {
			DT(ipf_rb_no_mem);
			LBUMP(ipf_rb_no_mem);
			return -1;
		}

		/*
		 * If there was a macro to initialise the RB node then that
		 * would get used here, but there isn't...
		 */
		bzero((char *)h, sizeof(*h));
		h->hn_addr = k.hn_addr;
		h->hn_addr.adf_family = k.hn_addr.adf_family;
		RBI_INSERT(ipf_rb, &htp->ht_root, h);
		htp->ht_cur_nodes++;
	} else {
		if ((htp->ht_max_per_node != 0) &&
		    (h->hn_active >= htp->ht_max_per_node)) {
			DT(ipf_rb_node_max);
			LBUMP(ipf_rb_node_max);
			return -1;
		}
	}

	h->hn_active++;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_ht_node_del                                             */
/* Returns:     int       - 0 == success,  -1 == failure                    */
/* parameters:  htp(I)    - pointer to address tracking structure           */
/*              family(I) - protocol family of address                      */
/*              addr(I)   - pointer to network address                      */
/*                                                                          */
/* NOTE: THIS FUNCTION MUST BE CALLED WITH AN EXCLUSIVE LOCK THAT PREVENTS  */
/*       ipf_ht_node_add FROM RUNNING CONCURRENTLY ON THE SAME htp.         */
/*                                                                          */
/* Try and find the address passed in amongst the leavese on this tree to   */
/* be friend. If found then drop the active account for that node drops by  */
/* one. If that count reaches 0, it is time to free it all up.              */
/* ------------------------------------------------------------------------ */
int
ipf_ht_node_del(htp, family, addr)
	host_track_t *htp;
	int family;
	i6addr_t *addr;
{
	host_node_t *h;
	host_node_t k;

	ipf_ht_node_make_key(htp, &k, family, addr);

	h = RBI_SEARCH(ipf_rb, &htp->ht_root, &k);
	if (h == NULL) {
		return -1;
	} else {
		h->hn_active--;
		if (h->hn_active == 0) {
			(void) RBI_DELETE(ipf_rb, &htp->ht_root, h);
			htp->ht_cur_nodes--;
			KFREE(h);
		}
	}

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_rb_ht_init                                              */
/* Returns:     Nil                                                         */
/* Parameters:  head(I) - pointer to host tracking structure                */
/*                                                                          */
/* Initialise the host tracking structure to be ready for use above.        */
/* ------------------------------------------------------------------------ */
void
ipf_rb_ht_init(head)
	host_track_t *head;
{
	RBI_INIT(ipf_rb, &head->ht_root);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_rb_ht_freenode                                          */
/* Returns:     Nil                                                         */
/* Parameters:  head(I) - pointer to host tracking structure                */
/*              arg(I)  - additional argument from walk caller              */
/*                                                                          */
/* Free an actual host_node_t structure.                                    */
/* ------------------------------------------------------------------------ */
void
ipf_rb_ht_freenode(node, arg)
	host_node_t *node;
	void *arg;
{
	KFREE(node);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_rb_ht_flush                                             */
/* Returns:     Nil                                                         */
/* Parameters:  head(I) - pointer to host tracking structure                */
/*                                                                          */
/* Remove all of the nodes in the tree tracking hosts by calling a walker   */
/* and free'ing each one.                                                   */
/* ------------------------------------------------------------------------ */
void
ipf_rb_ht_flush(head)
	host_track_t *head;
{
	RBI_WALK(ipf_rb, &head->ht_root, ipf_rb_ht_freenode, NULL);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_slowtimer                                               */
/* Returns:     Nil                                                         */
/* Parameters:  ptr(I) - pointer to main ipf soft context structure         */
/*                                                                          */
/* Slowly expire held state for fragments.  Timeouts are set * in           */
/* expectation of this being called twice per second.                       */
/* ------------------------------------------------------------------------ */
void
ipf_slowtimer(softc)
	ipf_main_softc_t *softc;
{

	ipf_token_expire(softc);
	ipf_frag_expire(softc);
	ipf_state_expire(softc);
	ipf_nat_expire(softc);
	ipf_auth_expire(softc);
	ipf_lookup_expire(softc);
	ipf_rule_expire(softc);
	ipf_sync_expire(softc);
	softc->ipf_ticks++;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_inet_mask_add                                           */
/* Returns:     Nil                                                         */
/* Parameters:  bits(I) - pointer to nat context information                */
/*              mtab(I) - pointer to mask hash table structure              */
/*                                                                          */
/* When called, bits represents the mask of a new NAT rule that has just    */
/* been added. This function inserts a bitmask into the array of masks to   */
/* search when searching for a matching NAT rule for a packet.              */
/* Prevention of duplicate masks is achieved by checking the use count for  */
/* a given netmask.                                                         */
/* ------------------------------------------------------------------------ */
void
ipf_inet_mask_add(bits, mtab)
	int bits;
	ipf_v4_masktab_t *mtab;
{
	u_32_t mask;
	int i, j;

	mtab->imt4_masks[bits]++;
	if (mtab->imt4_masks[bits] > 1)
		return;

	if (bits == 0)
		mask = 0;
	else
		mask = 0xffffffff << (32 - bits);

	for (i = 0; i < 33; i++) {
		if (ntohl(mtab->imt4_active[i]) < mask) {
			for (j = 32; j > i; j--)
				mtab->imt4_active[j] = mtab->imt4_active[j - 1];
			mtab->imt4_active[i] = htonl(mask);
			break;
		}
	}
	mtab->imt4_max++;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_inet_mask_del                                           */
/* Returns:     Nil                                                         */
/* Parameters:  bits(I) - number of bits set in the netmask                 */
/*              mtab(I) - pointer to mask hash table structure              */
/*                                                                          */
/* Remove the 32bit bitmask represented by "bits" from the collection of    */
/* netmasks stored inside of mtab.                                          */
/* ------------------------------------------------------------------------ */
void
ipf_inet_mask_del(bits, mtab)
	int bits;
	ipf_v4_masktab_t *mtab;
{
	u_32_t mask;
	int i, j;

	mtab->imt4_masks[bits]--;
	if (mtab->imt4_masks[bits] > 0)
		return;

	mask = htonl(0xffffffff << (32 - bits));
	for (i = 0; i < 33; i++) {
		if (mtab->imt4_active[i] == mask) {
			for (j = i + 1; j < 33; j++)
				mtab->imt4_active[j - 1] = mtab->imt4_active[j];
			break;
		}
	}
	mtab->imt4_max--;
	ASSERT(mtab->imt4_max >= 0);
}


#ifdef USE_INET6
/* ------------------------------------------------------------------------ */
/* Function:    ipf_inet6_mask_add                                          */
/* Returns:     Nil                                                         */
/* Parameters:  bits(I) - number of bits set in mask                        */
/*              mask(I) - pointer to mask to add                            */
/*              mtab(I) - pointer to mask hash table structure              */
/*                                                                          */
/* When called, bitcount represents the mask of a IPv6 NAT map rule that    */
/* has just been added. This function inserts a bitmask into the array of   */
/* masks to search when searching for a matching NAT rule for a packet.     */
/* Prevention of duplicate masks is achieved by checking the use count for  */
/* a given netmask.                                                         */
/* ------------------------------------------------------------------------ */
void
ipf_inet6_mask_add(bits, mask, mtab)
	int bits;
	i6addr_t *mask;
	ipf_v6_masktab_t *mtab;
{
	i6addr_t zero;
	int i, j;

	mtab->imt6_masks[bits]++;
	if (mtab->imt6_masks[bits] > 1)
		return;

	if (bits == 0) {
		mask = &zero;
		zero.i6[0] = 0;
		zero.i6[1] = 0;
		zero.i6[2] = 0;
		zero.i6[3] = 0;
	}

	for (i = 0; i < 129; i++) {
		if (IP6_LT(&mtab->imt6_active[i], mask)) {
			for (j = 128; j > i; j--)
				mtab->imt6_active[j] = mtab->imt6_active[j - 1];
			mtab->imt6_active[i] = *mask;
			break;
		}
	}
	mtab->imt6_max++;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_inet6_mask_del                                          */
/* Returns:     Nil                                                         */
/* Parameters:  bits(I) - number of bits set in mask                        */
/*              mask(I) - pointer to mask to remove                         */
/*              mtab(I) - pointer to mask hash table structure              */
/*                                                                          */
/* Remove the 128bit bitmask represented by "bits" from the collection of   */
/* netmasks stored inside of mtab.                                          */
/* ------------------------------------------------------------------------ */
void
ipf_inet6_mask_del(bits, mask, mtab)
	int bits;
	i6addr_t *mask;
	ipf_v6_masktab_t *mtab;
{
	i6addr_t zero;
	int i, j;

	mtab->imt6_masks[bits]--;
	if (mtab->imt6_masks[bits] > 0)
		return;

	if (bits == 0)
		mask = &zero;
	zero.i6[0] = 0;
	zero.i6[1] = 0;
	zero.i6[2] = 0;
	zero.i6[3] = 0;

	for (i = 0; i < 129; i++) {
		if (IP6_EQ(&mtab->imt6_active[i], mask)) {
			for (j = i + 1; j < 129; j++) {
				mtab->imt6_active[j - 1] = mtab->imt6_active[j];
				if (IP6_EQ(&mtab->imt6_active[j - 1], &zero))
					break;
			}
			break;
		}
	}
	mtab->imt6_max--;
	ASSERT(mtab->imt6_max >= 0);
}
#endif
