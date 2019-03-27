/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
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
#include <sys/file.h>
#if defined(_KERNEL) && \
    (defined(__NetBSD_Version) && (__NetBSD_Version >= 399002000))
# include <sys/kauth.h>
#endif
#if !defined(_KERNEL)
# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# define KERNEL
# ifdef _OpenBSD__
struct file;
# endif
# include <sys/uio.h>
# undef KERNEL
#endif
#if defined(_KERNEL) && defined(__FreeBSD_version)
# include <sys/filio.h>
# include <sys/fcntl.h>
#else
# include <sys/ioctl.h>
#endif
# include <sys/fcntl.h>
# include <sys/protosw.h>
#include <sys/socket.h>
#if defined(_KERNEL)
# include <sys/systm.h>
# if !defined(__SVR4)
#  include <sys/mbuf.h>
# endif
#endif
#if defined(__SVR4)
# include <sys/filio.h>
# include <sys/byteorder.h>
# ifdef KERNEL
#  include <sys/dditypes.h>
# endif
# include <sys/stream.h>
# include <sys/kmem.h>
#endif
#if defined(__FreeBSD_version)
# include <sys/queue.h>
#endif
#include <net/if.h>
#if defined(__FreeBSD_version)
# include <net/if_var.h>
#endif
#ifdef sun
# include <net/af.h>
#endif
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#ifdef RFC1825
# include <vpn/md5.h>
# include <vpn/ipsec.h>
extern struct ifnet vpnif;
#endif

# include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include "netinet/ip_compat.h"
#include <netinet/tcpip.h>
#include "netinet/ipl.h"
#include "netinet/ip_fil.h"
#include "netinet/ip_nat.h"
#include "netinet/ip_frag.h"
#include "netinet/ip_state.h"
#include "netinet/ip_proxy.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_dstlist.h"
#include "netinet/ip_sync.h"
#if defined(__FreeBSD_version)
# include <sys/malloc.h>
#endif
#ifdef HAS_SYS_MD5_H
# include <sys/md5.h>
#else
# include "md5.h"
#endif
/* END OF INCLUDES */

#undef	SOCKADDR_IN
#define	SOCKADDR_IN	struct sockaddr_in

#if !defined(lint)
static const char sccsid[] = "@(#)ip_nat.c	1.11 6/5/96 (C) 1995 Darren Reed";
static const char rcsid[] = "@(#)$FreeBSD$";
/* static const char rcsid[] = "@(#)$Id: ip_nat.c,v 2.195.2.102 2007/10/16 10:08:10 darrenr Exp $"; */
#endif


#define	NATFSUM(n,v,f)	((v) == 4 ? (n)->f.in4.s_addr : (n)->f.i6[0] + \
			 (n)->f.i6[1] + (n)->f.i6[2] + (n)->f.i6[3])
#define	NBUMP(x)	softn->(x)++
#define	NBUMPD(x, y)	do { \
				softn->x.y++; \
				DT(y); \
			} while (0)
#define	NBUMPSIDE(y,x)	softn->ipf_nat_stats.ns_side[y].x++
#define	NBUMPSIDED(y,x)	do { softn->ipf_nat_stats.ns_side[y].x++; \
			     DT(x); } while (0)
#define	NBUMPSIDEX(y,x,z) \
			do { softn->ipf_nat_stats.ns_side[y].x++; \
			     DT(z); } while (0)
#define	NBUMPSIDEDF(y,x)do { softn->ipf_nat_stats.ns_side[y].x++; \
			     DT1(x, fr_info_t *, fin); } while (0)

static ipftuneable_t ipf_nat_tuneables[] = {
	/* nat */
	{ { (void *)offsetof(ipf_nat_softc_t, ipf_nat_lock) },
		"nat_lock",	0,	1,
		stsizeof(ipf_nat_softc_t, ipf_nat_lock),
		IPFT_RDONLY,		NULL,	NULL },
	{ { (void *)offsetof(ipf_nat_softc_t, ipf_nat_table_sz) },
		"nat_table_size", 1,	0x7fffffff,
		stsizeof(ipf_nat_softc_t, ipf_nat_table_sz),
		0,			NULL,	ipf_nat_rehash },
	{ { (void *)offsetof(ipf_nat_softc_t, ipf_nat_table_max) },
		"nat_table_max", 1,	0x7fffffff,
		stsizeof(ipf_nat_softc_t, ipf_nat_table_max),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_nat_softc_t, ipf_nat_maprules_sz) },
		"nat_rules_size", 1,	0x7fffffff,
		stsizeof(ipf_nat_softc_t, ipf_nat_maprules_sz),
		0,			NULL,	ipf_nat_rehash_rules },
	{ { (void *)offsetof(ipf_nat_softc_t, ipf_nat_rdrrules_sz) },
		"rdr_rules_size", 1,	0x7fffffff,
		stsizeof(ipf_nat_softc_t, ipf_nat_rdrrules_sz),
		0,			NULL,	ipf_nat_rehash_rules },
	{ { (void *)offsetof(ipf_nat_softc_t, ipf_nat_hostmap_sz) },
		"hostmap_size",	1,	0x7fffffff,
		stsizeof(ipf_nat_softc_t, ipf_nat_hostmap_sz),
		0,			NULL,	ipf_nat_hostmap_rehash },
	{ { (void *)offsetof(ipf_nat_softc_t, ipf_nat_maxbucket) },
		"nat_maxbucket",1,	0x7fffffff,
		stsizeof(ipf_nat_softc_t, ipf_nat_maxbucket),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_nat_softc_t, ipf_nat_logging) },
		"nat_logging",	0,	1,
		stsizeof(ipf_nat_softc_t, ipf_nat_logging),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_nat_softc_t, ipf_nat_doflush) },
		"nat_doflush",	0,	1,
		stsizeof(ipf_nat_softc_t, ipf_nat_doflush),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_nat_softc_t, ipf_nat_table_wm_low) },
		"nat_table_wm_low",	1,	99,
		stsizeof(ipf_nat_softc_t, ipf_nat_table_wm_low),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_nat_softc_t, ipf_nat_table_wm_high) },
		"nat_table_wm_high",	2,	100,
		stsizeof(ipf_nat_softc_t, ipf_nat_table_wm_high),
		0,			NULL,	NULL },
	{ { 0 },
		NULL,			0,	0,
		0,
		0,			NULL,	NULL }
};

/* ======================================================================== */
/* How the NAT is organised and works.                                      */
/*                                                                          */
/* Inside (interface y) NAT       Outside (interface x)                     */
/* -------------------- -+- -------------------------------------           */
/* Packet going          |   out, processsed by ipf_nat_checkout() for x    */
/* ------------>         |   ------------>                                  */
/* src=10.1.1.1          |   src=192.1.1.1                                  */
/*                       |                                                  */
/*                       |   in, processed by ipf_nat_checkin() for x       */
/* <------------         |   <------------                                  */
/* dst=10.1.1.1          |   dst=192.1.1.1                                  */
/* -------------------- -+- -------------------------------------           */
/* ipf_nat_checkout() - changes ip_src and if required, sport               */
/*             - creates a new mapping, if required.                        */
/* ipf_nat_checkin()  - changes ip_dst and if required, dport               */
/*                                                                          */
/* In the NAT table, internal source is recorded as "in" and externally     */
/* seen as "out".                                                           */
/* ======================================================================== */


#if SOLARIS && !defined(INSTANCES)
extern	int		pfil_delayed_copy;
#endif

static	int	ipf_nat_flush_entry __P((ipf_main_softc_t *, void *));
static	int	ipf_nat_getent __P((ipf_main_softc_t *, caddr_t, int));
static	int	ipf_nat_getsz __P((ipf_main_softc_t *, caddr_t, int));
static	int	ipf_nat_putent __P((ipf_main_softc_t *, caddr_t, int));
static	void	ipf_nat_addmap __P((ipf_nat_softc_t *, ipnat_t *));
static	void	ipf_nat_addrdr __P((ipf_nat_softc_t *, ipnat_t *));
static	int	ipf_nat_builddivertmp __P((ipf_nat_softc_t *, ipnat_t *));
static	int	ipf_nat_clearlist __P((ipf_main_softc_t *, ipf_nat_softc_t *));
static	int	ipf_nat_cmp_rules __P((ipnat_t *, ipnat_t *));
static	int	ipf_nat_decap __P((fr_info_t *, nat_t *));
static	void	ipf_nat_delrule __P((ipf_main_softc_t *, ipf_nat_softc_t *,
				     ipnat_t *, int));
static	int	ipf_nat_extraflush __P((ipf_main_softc_t *, ipf_nat_softc_t *, int));
static	int	ipf_nat_finalise __P((fr_info_t *, nat_t *));
static	int	ipf_nat_flushtable __P((ipf_main_softc_t *, ipf_nat_softc_t *));
static	int	ipf_nat_getnext __P((ipf_main_softc_t *, ipftoken_t *,
				     ipfgeniter_t *, ipfobj_t *));
static	int	ipf_nat_gettable __P((ipf_main_softc_t *, ipf_nat_softc_t *,
				      char *));
static	hostmap_t *ipf_nat_hostmap __P((ipf_nat_softc_t *, ipnat_t *,
					struct in_addr, struct in_addr,
					struct in_addr, u_32_t));
static	int	ipf_nat_icmpquerytype __P((int));
static	int	ipf_nat_iterator __P((ipf_main_softc_t *, ipftoken_t *,
				      ipfgeniter_t *, ipfobj_t *));
static	int	ipf_nat_match __P((fr_info_t *, ipnat_t *));
static	int	ipf_nat_matcharray __P((nat_t *, int *, u_long));
static	int	ipf_nat_matchflush __P((ipf_main_softc_t *, ipf_nat_softc_t *,
					caddr_t));
static	void	ipf_nat_mssclamp __P((tcphdr_t *, u_32_t, fr_info_t *,
				      u_short *));
static	int	ipf_nat_newmap __P((fr_info_t *, nat_t *, natinfo_t *));
static	int	ipf_nat_newdivert __P((fr_info_t *, nat_t *, natinfo_t *));
static	int	ipf_nat_newrdr __P((fr_info_t *, nat_t *, natinfo_t *));
static	int	ipf_nat_newrewrite __P((fr_info_t *, nat_t *, natinfo_t *));
static	int	ipf_nat_nextaddr __P((fr_info_t *, nat_addr_t *, u_32_t *,
				      u_32_t *));
static	int	ipf_nat_nextaddrinit __P((ipf_main_softc_t *, char *,
					  nat_addr_t *, int, void *));
static	int	ipf_nat_resolverule __P((ipf_main_softc_t *, ipnat_t *));
static	int	ipf_nat_ruleaddrinit __P((ipf_main_softc_t *,
					  ipf_nat_softc_t *, ipnat_t *));
static	void	ipf_nat_rule_fini __P((ipf_main_softc_t *, ipnat_t *));
static	int	ipf_nat_rule_init __P((ipf_main_softc_t *, ipf_nat_softc_t *,
				       ipnat_t *));
static	int	ipf_nat_siocaddnat __P((ipf_main_softc_t *, ipf_nat_softc_t *,
					ipnat_t *, int));
static	void	ipf_nat_siocdelnat __P((ipf_main_softc_t *, ipf_nat_softc_t *,
					ipnat_t *, int));
static	void	ipf_nat_tabmove __P((ipf_nat_softc_t *, nat_t *));

/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_main_load                                           */
/* Returns:     int - 0 == success, -1 == failure                           */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* The only global NAT structure that needs to be initialised is the filter */
/* rule that is used with blocking packets.                                 */
/* ------------------------------------------------------------------------ */
int
ipf_nat_main_load()
{

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_main_unload                                         */
/* Returns:     int - 0 == success, -1 == failure                           */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* A null-op function that exists as a placeholder so that the flow in      */
/* other functions is obvious.                                              */
/* ------------------------------------------------------------------------ */
int
ipf_nat_main_unload()
{
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_soft_create                                         */
/* Returns:     void * - NULL = failure, else pointer to NAT context        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Allocate the initial soft context structure for NAT and populate it with */
/* some default values. Creating the tables is left until we call _init so  */
/* that sizes can be changed before we get under way.                       */
/* ------------------------------------------------------------------------ */
void *
ipf_nat_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_nat_softc_t *softn;

	KMALLOC(softn, ipf_nat_softc_t *);
	if (softn == NULL)
		return NULL;

	bzero((char *)softn, sizeof(*softn));

	softn->ipf_nat_tune = ipf_tune_array_copy(softn,
						  sizeof(ipf_nat_tuneables),
						  ipf_nat_tuneables);
	if (softn->ipf_nat_tune == NULL) {
		ipf_nat_soft_destroy(softc, softn);
		return NULL;
	}
	if (ipf_tune_array_link(softc, softn->ipf_nat_tune) == -1) {
		ipf_nat_soft_destroy(softc, softn);
		return NULL;
	}

	softn->ipf_nat_list_tail = &softn->ipf_nat_list;

	softn->ipf_nat_table_max = NAT_TABLE_MAX;
	softn->ipf_nat_table_sz = NAT_TABLE_SZ;
	softn->ipf_nat_maprules_sz = NAT_SIZE;
	softn->ipf_nat_rdrrules_sz = RDR_SIZE;
	softn->ipf_nat_hostmap_sz = HOSTMAP_SIZE;
	softn->ipf_nat_doflush = 0;
#ifdef  IPFILTER_LOG
	softn->ipf_nat_logging = 1;
#else
	softn->ipf_nat_logging = 0;
#endif

	softn->ipf_nat_defage = DEF_NAT_AGE;
	softn->ipf_nat_defipage = IPF_TTLVAL(60);
	softn->ipf_nat_deficmpage = IPF_TTLVAL(3);
	softn->ipf_nat_table_wm_high = 99;
	softn->ipf_nat_table_wm_low = 90;

	return softn;
}

/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_soft_destroy                                        */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* ------------------------------------------------------------------------ */
void
ipf_nat_soft_destroy(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_nat_softc_t *softn = arg;

	if (softn->ipf_nat_tune != NULL) {
		ipf_tune_array_unlink(softc, softn->ipf_nat_tune);
		KFREES(softn->ipf_nat_tune, sizeof(ipf_nat_tuneables));
		softn->ipf_nat_tune = NULL;
	}

	KFREE(softn);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_init                                                */
/* Returns:     int - 0 == success, -1 == failure                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Initialise all of the NAT locks, tables and other structures.            */
/* ------------------------------------------------------------------------ */
int
ipf_nat_soft_init(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_nat_softc_t *softn = arg;
	ipftq_t *tq;
	int i;

	KMALLOCS(softn->ipf_nat_table[0], nat_t **, \
		 sizeof(nat_t *) * softn->ipf_nat_table_sz);

	if (softn->ipf_nat_table[0] != NULL) {
		bzero((char *)softn->ipf_nat_table[0],
		      softn->ipf_nat_table_sz * sizeof(nat_t *));
	} else {
		return -1;
	}

	KMALLOCS(softn->ipf_nat_table[1], nat_t **, \
		 sizeof(nat_t *) * softn->ipf_nat_table_sz);

	if (softn->ipf_nat_table[1] != NULL) {
		bzero((char *)softn->ipf_nat_table[1],
		      softn->ipf_nat_table_sz * sizeof(nat_t *));
	} else {
		return -2;
	}

	KMALLOCS(softn->ipf_nat_map_rules, ipnat_t **, \
		 sizeof(ipnat_t *) * softn->ipf_nat_maprules_sz);

	if (softn->ipf_nat_map_rules != NULL) {
		bzero((char *)softn->ipf_nat_map_rules,
		      softn->ipf_nat_maprules_sz * sizeof(ipnat_t *));
	} else {
		return -3;
	}

	KMALLOCS(softn->ipf_nat_rdr_rules, ipnat_t **, \
		 sizeof(ipnat_t *) * softn->ipf_nat_rdrrules_sz);

	if (softn->ipf_nat_rdr_rules != NULL) {
		bzero((char *)softn->ipf_nat_rdr_rules,
		      softn->ipf_nat_rdrrules_sz * sizeof(ipnat_t *));
	} else {
		return -4;
	}

	KMALLOCS(softn->ipf_hm_maptable, hostmap_t **, \
		 sizeof(hostmap_t *) * softn->ipf_nat_hostmap_sz);

	if (softn->ipf_hm_maptable != NULL) {
		bzero((char *)softn->ipf_hm_maptable,
		      sizeof(hostmap_t *) * softn->ipf_nat_hostmap_sz);
	} else {
		return -5;
	}
	softn->ipf_hm_maplist = NULL;

	KMALLOCS(softn->ipf_nat_stats.ns_side[0].ns_bucketlen, u_int *,
		 softn->ipf_nat_table_sz * sizeof(u_int));

	if (softn->ipf_nat_stats.ns_side[0].ns_bucketlen == NULL) {
		return -6;
	}
	bzero((char *)softn->ipf_nat_stats.ns_side[0].ns_bucketlen,
	      softn->ipf_nat_table_sz * sizeof(u_int));

	KMALLOCS(softn->ipf_nat_stats.ns_side[1].ns_bucketlen, u_int *,
		 softn->ipf_nat_table_sz * sizeof(u_int));

	if (softn->ipf_nat_stats.ns_side[1].ns_bucketlen == NULL) {
		return -7;
	}

	bzero((char *)softn->ipf_nat_stats.ns_side[1].ns_bucketlen,
	      softn->ipf_nat_table_sz * sizeof(u_int));

	if (softn->ipf_nat_maxbucket == 0) {
		for (i = softn->ipf_nat_table_sz; i > 0; i >>= 1)
			softn->ipf_nat_maxbucket++;
		softn->ipf_nat_maxbucket *= 2;
	}

	ipf_sttab_init(softc, softn->ipf_nat_tcptq);
	/*
	 * Increase this because we may have "keep state" following this too
	 * and packet storms can occur if this is removed too quickly.
	 */
	softn->ipf_nat_tcptq[IPF_TCPS_CLOSED].ifq_ttl = softc->ipf_tcplastack;
	softn->ipf_nat_tcptq[IPF_TCP_NSTATES - 1].ifq_next =
							&softn->ipf_nat_udptq;

	IPFTQ_INIT(&softn->ipf_nat_udptq, softn->ipf_nat_defage,
		   "nat ipftq udp tab");
	softn->ipf_nat_udptq.ifq_next = &softn->ipf_nat_udpacktq;

	IPFTQ_INIT(&softn->ipf_nat_udpacktq, softn->ipf_nat_defage,
		   "nat ipftq udpack tab");
	softn->ipf_nat_udpacktq.ifq_next = &softn->ipf_nat_icmptq;

	IPFTQ_INIT(&softn->ipf_nat_icmptq, softn->ipf_nat_deficmpage,
		   "nat icmp ipftq tab");
	softn->ipf_nat_icmptq.ifq_next = &softn->ipf_nat_icmpacktq;

	IPFTQ_INIT(&softn->ipf_nat_icmpacktq, softn->ipf_nat_defage,
		   "nat icmpack ipftq tab");
	softn->ipf_nat_icmpacktq.ifq_next = &softn->ipf_nat_iptq;

	IPFTQ_INIT(&softn->ipf_nat_iptq, softn->ipf_nat_defipage,
		   "nat ip ipftq tab");
	softn->ipf_nat_iptq.ifq_next = &softn->ipf_nat_pending;

	IPFTQ_INIT(&softn->ipf_nat_pending, 1, "nat pending ipftq tab");
	softn->ipf_nat_pending.ifq_next = NULL;

	for (i = 0, tq = softn->ipf_nat_tcptq; i < IPF_TCP_NSTATES; i++, tq++) {
		if (tq->ifq_ttl < softn->ipf_nat_deficmpage)
			tq->ifq_ttl = softn->ipf_nat_deficmpage;
#ifdef LARGE_NAT
		else if (tq->ifq_ttl > softn->ipf_nat_defage)
			tq->ifq_ttl = softn->ipf_nat_defage;
#endif
	}

	/*
	 * Increase this because we may have "keep state" following
	 * this too and packet storms can occur if this is removed
	 * too quickly.
	 */
	softn->ipf_nat_tcptq[IPF_TCPS_CLOSED].ifq_ttl = softc->ipf_tcplastack;

	MUTEX_INIT(&softn->ipf_nat_new, "ipf nat new mutex");
	MUTEX_INIT(&softn->ipf_nat_io, "ipf nat io mutex");

	softn->ipf_nat_inited = 1;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_soft_fini                                           */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Free all memory used by NAT structures allocated at runtime.             */
/* ------------------------------------------------------------------------ */
int
ipf_nat_soft_fini(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_nat_softc_t *softn = arg;
	ipftq_t *ifq, *ifqnext;

	(void) ipf_nat_clearlist(softc, softn);
	(void) ipf_nat_flushtable(softc, softn);

	/*
	 * Proxy timeout queues are not cleaned here because although they
	 * exist on the NAT list, ipf_proxy_unload is called after unload
	 * and the proxies actually are responsible for them being created.
	 * Should the proxy timeouts have their own list?  There's no real
	 * justification as this is the only complication.
	 */
	for (ifq = softn->ipf_nat_utqe; ifq != NULL; ifq = ifqnext) {
		ifqnext = ifq->ifq_next;
		if (ipf_deletetimeoutqueue(ifq) == 0)
			ipf_freetimeoutqueue(softc, ifq);
	}

	if (softn->ipf_nat_table[0] != NULL) {
		KFREES(softn->ipf_nat_table[0],
		       sizeof(nat_t *) * softn->ipf_nat_table_sz);
		softn->ipf_nat_table[0] = NULL;
	}
	if (softn->ipf_nat_table[1] != NULL) {
		KFREES(softn->ipf_nat_table[1],
		       sizeof(nat_t *) * softn->ipf_nat_table_sz);
		softn->ipf_nat_table[1] = NULL;
	}
	if (softn->ipf_nat_map_rules != NULL) {
		KFREES(softn->ipf_nat_map_rules,
		       sizeof(ipnat_t *) * softn->ipf_nat_maprules_sz);
		softn->ipf_nat_map_rules = NULL;
	}
	if (softn->ipf_nat_rdr_rules != NULL) {
		KFREES(softn->ipf_nat_rdr_rules,
		       sizeof(ipnat_t *) * softn->ipf_nat_rdrrules_sz);
		softn->ipf_nat_rdr_rules = NULL;
	}
	if (softn->ipf_hm_maptable != NULL) {
		KFREES(softn->ipf_hm_maptable,
		       sizeof(hostmap_t *) * softn->ipf_nat_hostmap_sz);
		softn->ipf_hm_maptable = NULL;
	}
	if (softn->ipf_nat_stats.ns_side[0].ns_bucketlen != NULL) {
		KFREES(softn->ipf_nat_stats.ns_side[0].ns_bucketlen,
		       sizeof(u_int) * softn->ipf_nat_table_sz);
		softn->ipf_nat_stats.ns_side[0].ns_bucketlen = NULL;
	}
	if (softn->ipf_nat_stats.ns_side[1].ns_bucketlen != NULL) {
		KFREES(softn->ipf_nat_stats.ns_side[1].ns_bucketlen,
		       sizeof(u_int) * softn->ipf_nat_table_sz);
		softn->ipf_nat_stats.ns_side[1].ns_bucketlen = NULL;
	}

	if (softn->ipf_nat_inited == 1) {
		softn->ipf_nat_inited = 0;
		ipf_sttab_destroy(softn->ipf_nat_tcptq);

		MUTEX_DESTROY(&softn->ipf_nat_new);
		MUTEX_DESTROY(&softn->ipf_nat_io);

		MUTEX_DESTROY(&softn->ipf_nat_udptq.ifq_lock);
		MUTEX_DESTROY(&softn->ipf_nat_udpacktq.ifq_lock);
		MUTEX_DESTROY(&softn->ipf_nat_icmptq.ifq_lock);
		MUTEX_DESTROY(&softn->ipf_nat_icmpacktq.ifq_lock);
		MUTEX_DESTROY(&softn->ipf_nat_iptq.ifq_lock);
		MUTEX_DESTROY(&softn->ipf_nat_pending.ifq_lock);
	}

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_setlock                                             */
/* Returns:     Nil                                                         */
/* Parameters:  arg(I) - pointer to soft state information                  */
/*              tmp(I) - new lock value                                     */
/*                                                                          */
/* Set the "lock status" of NAT to the value in tmp.                        */
/* ------------------------------------------------------------------------ */
void
ipf_nat_setlock(arg, tmp)
	void *arg;
	int tmp;
{
	ipf_nat_softc_t *softn = arg;

	softn->ipf_nat_lock = tmp;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_addrdr                                              */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to add                           */
/*                                                                          */
/* Adds a redirect rule to the hash table of redirect rules and the list of */
/* loaded NAT rules.  Updates the bitmask indicating which netmasks are in  */
/* use by redirect rules.                                                   */
/* ------------------------------------------------------------------------ */
static void
ipf_nat_addrdr(softn, n)
	ipf_nat_softc_t *softn;
	ipnat_t *n;
{
	ipnat_t **np;
	u_32_t j;
	u_int hv;
	u_int rhv;
	int k;

	if (n->in_odstatype == FRI_NORMAL) {
		k = count4bits(n->in_odstmsk);
		ipf_inet_mask_add(k, &softn->ipf_nat_rdr_mask);
		j = (n->in_odstaddr & n->in_odstmsk);
		rhv = NAT_HASH_FN(j, 0, 0xffffffff);
	} else {
		ipf_inet_mask_add(0, &softn->ipf_nat_rdr_mask);
		j = 0;
		rhv = 0;
	}
	hv = rhv % softn->ipf_nat_rdrrules_sz;
	np = softn->ipf_nat_rdr_rules + hv;
	while (*np != NULL)
		np = &(*np)->in_rnext;
	n->in_rnext = NULL;
	n->in_prnext = np;
	n->in_hv[0] = hv;
	n->in_use++;
	*np = n;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_addmap                                              */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to add                           */
/*                                                                          */
/* Adds a NAT map rule to the hash table of rules and the list of  loaded   */
/* NAT rules.  Updates the bitmask indicating which netmasks are in use by  */
/* redirect rules.                                                          */
/* ------------------------------------------------------------------------ */
static void
ipf_nat_addmap(softn, n)
	ipf_nat_softc_t *softn;
	ipnat_t *n;
{
	ipnat_t **np;
	u_32_t j;
	u_int hv;
	u_int rhv;
	int k;

	if (n->in_osrcatype == FRI_NORMAL) {
		k = count4bits(n->in_osrcmsk);
		ipf_inet_mask_add(k, &softn->ipf_nat_map_mask);
		j = (n->in_osrcaddr & n->in_osrcmsk);
		rhv = NAT_HASH_FN(j, 0, 0xffffffff);
	} else {
		ipf_inet_mask_add(0, &softn->ipf_nat_map_mask);
		j = 0;
		rhv = 0;
	}
	hv = rhv % softn->ipf_nat_maprules_sz;
	np = softn->ipf_nat_map_rules + hv;
	while (*np != NULL)
		np = &(*np)->in_mnext;
	n->in_mnext = NULL;
	n->in_pmnext = np;
	n->in_hv[1] = rhv;
	n->in_use++;
	*np = n;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_delrdr                                              */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to delete                        */
/*                                                                          */
/* Removes a redirect rule from the hash table of redirect rules.           */
/* ------------------------------------------------------------------------ */
void
ipf_nat_delrdr(softn, n)
	ipf_nat_softc_t *softn;
	ipnat_t *n;
{
	if (n->in_odstatype == FRI_NORMAL) {
		int k = count4bits(n->in_odstmsk);
		ipf_inet_mask_del(k, &softn->ipf_nat_rdr_mask);
	} else {
		ipf_inet_mask_del(0, &softn->ipf_nat_rdr_mask);
	}
	if (n->in_rnext)
		n->in_rnext->in_prnext = n->in_prnext;
	*n->in_prnext = n->in_rnext;
	n->in_use--;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_delmap                                              */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to delete                        */
/*                                                                          */
/* Removes a NAT map rule from the hash table of NAT map rules.             */
/* ------------------------------------------------------------------------ */
void
ipf_nat_delmap(softn, n)
	ipf_nat_softc_t *softn;
	ipnat_t *n;
{
	if (n->in_osrcatype == FRI_NORMAL) {
		int k = count4bits(n->in_osrcmsk);
		ipf_inet_mask_del(k, &softn->ipf_nat_map_mask);
	} else {
		ipf_inet_mask_del(0, &softn->ipf_nat_map_mask);
	}
	if (n->in_mnext != NULL)
		n->in_mnext->in_pmnext = n->in_pmnext;
	*n->in_pmnext = n->in_mnext;
	n->in_use--;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_hostmap                                             */
/* Returns:     struct hostmap* - NULL if no hostmap could be created,      */
/*                                else a pointer to the hostmapping to use  */
/* Parameters:  np(I)   - pointer to NAT rule                               */
/*              real(I) - real IP address                                   */
/*              map(I)  - mapped IP address                                 */
/*              port(I) - destination port number                           */
/* Write Locks: ipf_nat                                                     */
/*                                                                          */
/* Check if an ip address has already been allocated for a given mapping    */
/* that is not doing port based translation.  If is not yet allocated, then */
/* create a new entry if a non-NULL NAT rule pointer has been supplied.     */
/* ------------------------------------------------------------------------ */
static struct hostmap *
ipf_nat_hostmap(softn, np, src, dst, map, port)
	ipf_nat_softc_t *softn;
	ipnat_t *np;
	struct in_addr src;
	struct in_addr dst;
	struct in_addr map;
	u_32_t port;
{
	hostmap_t *hm;
	u_int hv, rhv;

	hv = (src.s_addr ^ dst.s_addr);
	hv += src.s_addr;
	hv += dst.s_addr;
	rhv = hv;
	hv %= softn->ipf_nat_hostmap_sz;
	for (hm = softn->ipf_hm_maptable[hv]; hm; hm = hm->hm_hnext)
		if ((hm->hm_osrcip.s_addr == src.s_addr) &&
		    (hm->hm_odstip.s_addr == dst.s_addr) &&
		    ((np == NULL) || (np == hm->hm_ipnat)) &&
		    ((port == 0) || (port == hm->hm_port))) {
			softn->ipf_nat_stats.ns_hm_addref++;
			hm->hm_ref++;
			return hm;
		}

	if (np == NULL) {
		softn->ipf_nat_stats.ns_hm_nullnp++;
		return NULL;
	}

	KMALLOC(hm, hostmap_t *);
	if (hm) {
		hm->hm_next = softn->ipf_hm_maplist;
		hm->hm_pnext = &softn->ipf_hm_maplist;
		if (softn->ipf_hm_maplist != NULL)
			softn->ipf_hm_maplist->hm_pnext = &hm->hm_next;
		softn->ipf_hm_maplist = hm;
		hm->hm_hnext = softn->ipf_hm_maptable[hv];
		hm->hm_phnext = softn->ipf_hm_maptable + hv;
		if (softn->ipf_hm_maptable[hv] != NULL)
			softn->ipf_hm_maptable[hv]->hm_phnext = &hm->hm_hnext;
		softn->ipf_hm_maptable[hv] = hm;
		hm->hm_ipnat = np;
		np->in_use++;
		hm->hm_osrcip = src;
		hm->hm_odstip = dst;
		hm->hm_nsrcip = map;
		hm->hm_ndstip.s_addr = 0;
		hm->hm_ref = 1;
		hm->hm_port = port;
		hm->hm_hv = rhv;
		hm->hm_v = 4;
		softn->ipf_nat_stats.ns_hm_new++;
	} else {
		softn->ipf_nat_stats.ns_hm_newfail++;
	}
	return hm;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_hostmapdel                                          */
/* Returns:     Nil                                                         */
/* Parameters:  hmp(I) - pointer to hostmap structure pointer               */
/* Write Locks: ipf_nat                                                     */
/*                                                                          */
/* Decrement the references to this hostmap structure by one.  If this      */
/* reaches zero then remove it and free it.                                 */
/* ------------------------------------------------------------------------ */
void
ipf_nat_hostmapdel(softc, hmp)
	ipf_main_softc_t *softc;
	struct hostmap **hmp;
{
	struct hostmap *hm;

	hm = *hmp;
	*hmp = NULL;

	hm->hm_ref--;
	if (hm->hm_ref == 0) {
		ipf_nat_rule_deref(softc, &hm->hm_ipnat);
		if (hm->hm_hnext)
			hm->hm_hnext->hm_phnext = hm->hm_phnext;
		*hm->hm_phnext = hm->hm_hnext;
		if (hm->hm_next)
			hm->hm_next->hm_pnext = hm->hm_pnext;
		*hm->hm_pnext = hm->hm_next;
		KFREE(hm);
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_fix_outcksum                                            */
/* Returns:     Nil                                                         */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              sp(I)  - location of 16bit checksum to update               */
/*              n((I)  - amount to adjust checksum by                       */
/*                                                                          */
/* Adjusts the 16bit checksum by "n" for packets going out.                 */
/* ------------------------------------------------------------------------ */
void
ipf_fix_outcksum(cksum, sp, n, partial)
	int cksum;
	u_short *sp;
	u_32_t n, partial;
{
	u_short sumshort;
	u_32_t sum1;

	if (n == 0)
		return;

	if (cksum == 4) {
		*sp = 0;
		return;
	}
	if (cksum == 2) {
		sum1 = partial;
		sum1 = (sum1 & 0xffff) + (sum1 >> 16);
		*sp = htons(sum1);
		return;
	}
	sum1 = (~ntohs(*sp)) & 0xffff;
	sum1 += (n);
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	/* Again */
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	sumshort = ~(u_short)sum1;
	*(sp) = htons(sumshort);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_fix_incksum                                             */
/* Returns:     Nil                                                         */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              sp(I)  - location of 16bit checksum to update               */
/*              n((I)  - amount to adjust checksum by                       */
/*                                                                          */
/* Adjusts the 16bit checksum by "n" for packets going in.                  */
/* ------------------------------------------------------------------------ */
void
ipf_fix_incksum(cksum, sp, n, partial)
	int cksum;
	u_short *sp;
	u_32_t n, partial;
{
	u_short sumshort;
	u_32_t sum1;

	if (n == 0)
		return;

	if (cksum == 4) {
		*sp = 0;
		return;
	}
	if (cksum == 2) {
		sum1 = partial;
		sum1 = (sum1 & 0xffff) + (sum1 >> 16);
		*sp = htons(sum1);
		return;
	}

	sum1 = (~ntohs(*sp)) & 0xffff;
	sum1 += ~(n) & 0xffff;
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	/* Again */
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	sumshort = ~(u_short)sum1;
	*(sp) = htons(sumshort);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_fix_datacksum                                           */
/* Returns:     Nil                                                         */
/* Parameters:  sp(I)  - location of 16bit checksum to update               */
/*              n((I)  - amount to adjust checksum by                       */
/*                                                                          */
/* Fix_datacksum is used *only* for the adjustments of checksums in the     */
/* data section of an IP packet.                                            */
/*                                                                          */
/* The only situation in which you need to do this is when NAT'ing an       */
/* ICMP error message. Such a message, contains in its body the IP header   */
/* of the original IP packet, that causes the error.                        */
/*                                                                          */
/* You can't use fix_incksum or fix_outcksum in that case, because for the  */
/* kernel the data section of the ICMP error is just data, and no special   */
/* processing like hardware cksum or ntohs processing have been done by the */
/* kernel on the data section.                                              */
/* ------------------------------------------------------------------------ */
void
ipf_fix_datacksum(sp, n)
	u_short *sp;
	u_32_t n;
{
	u_short sumshort;
	u_32_t sum1;

	if (n == 0)
		return;

	sum1 = (~ntohs(*sp)) & 0xffff;
	sum1 += (n);
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	/* Again */
	sum1 = (sum1 >> 16) + (sum1 & 0xffff);
	sumshort = ~(u_short)sum1;
	*(sp) = htons(sumshort);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_ioctl                                               */
/* Returns:     int - 0 == success, != 0 == failure                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I)  - pointer to ioctl data                            */
/*              cmd(I)   - ioctl command integer                            */
/*              mode(I)  - file mode bits used with open                    */
/*              uid(I)   - uid of calling process                           */
/*              ctx(I)   - pointer used as key for finding context          */
/*                                                                          */
/* Processes an ioctl call made to operate on the IP Filter NAT device.     */
/* ------------------------------------------------------------------------ */
int
ipf_nat_ioctl(softc, data, cmd, mode, uid, ctx)
	ipf_main_softc_t *softc;
	ioctlcmd_t cmd;
	caddr_t data;
	int mode, uid;
	void *ctx;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	int error = 0, ret, arg, getlock;
	ipnat_t *nat, *nt, *n;
	ipnat_t natd;
	SPL_INT(s);

#if BSD_GE_YEAR(199306) && defined(_KERNEL)
# if NETBSD_GE_REV(399002000)
	if ((mode & FWRITE) &&
	     kauth_authorize_network(curlwp->l_cred, KAUTH_NETWORK_FIREWALL,
				     KAUTH_REQ_NETWORK_FIREWALL_FW,
				     NULL, NULL, NULL))
# else
#  if defined(__FreeBSD_version)
	if (securelevel_ge(curthread->td_ucred, 3) && (mode & FWRITE))
#  else
	if ((securelevel >= 3) && (mode & FWRITE))
#  endif
# endif
	{
		IPFERROR(60001);
		return EPERM;
	}
#endif

	getlock = (mode & NAT_LOCKHELD) ? 0 : 1;

	n = NULL;
	nt = NULL;
	nat = NULL;

	if ((cmd == (ioctlcmd_t)SIOCADNAT) || (cmd == (ioctlcmd_t)SIOCRMNAT) ||
	    (cmd == (ioctlcmd_t)SIOCPURGENAT)) {
		if (mode & NAT_SYSSPACE) {
			bcopy(data, (char *)&natd, sizeof(natd));
			nat = &natd;
			error = 0;
		} else {
			bzero(&natd, sizeof(natd));
			error = ipf_inobj(softc, data, NULL, &natd,
					  IPFOBJ_IPNAT);
			if (error != 0)
				goto done;

			if (natd.in_size < sizeof(ipnat_t)) {
				error = EINVAL;
				goto done;
			}
			KMALLOCS(nt, ipnat_t *, natd.in_size);
			if (nt == NULL) {
				IPFERROR(60070);
				error = ENOMEM;
				goto done;
			}
			bzero(nt, natd.in_size);
			error = ipf_inobjsz(softc, data, nt, IPFOBJ_IPNAT,
					    natd.in_size);
			if (error)
				goto done;
			nat = nt;
		}

		/*
		 * For add/delete, look to see if the NAT entry is
		 * already present
		 */
		nat->in_flags &= IPN_USERFLAGS;
		if ((nat->in_redir & NAT_MAPBLK) == 0) {
			if (nat->in_osrcatype == FRI_NORMAL ||
			    nat->in_osrcatype == FRI_NONE)
				nat->in_osrcaddr &= nat->in_osrcmsk;
			if (nat->in_odstatype == FRI_NORMAL ||
			    nat->in_odstatype == FRI_NONE)
				nat->in_odstaddr &= nat->in_odstmsk;
			if ((nat->in_flags & (IPN_SPLIT|IPN_SIPRANGE)) == 0) {
				if (nat->in_nsrcatype == FRI_NORMAL)
					nat->in_nsrcaddr &= nat->in_nsrcmsk;
				if (nat->in_ndstatype == FRI_NORMAL)
					nat->in_ndstaddr &= nat->in_ndstmsk;
			}
		}

		error = ipf_nat_rule_init(softc, softn, nat);
		if (error != 0)
			goto done;

		MUTEX_ENTER(&softn->ipf_nat_io);
		for (n = softn->ipf_nat_list; n != NULL; n = n->in_next)
			if (ipf_nat_cmp_rules(nat, n) == 0)
				break;
	}

	switch (cmd)
	{
#ifdef  IPFILTER_LOG
	case SIOCIPFFB :
	{
		int tmp;

		if (!(mode & FWRITE)) {
			IPFERROR(60002);
			error = EPERM;
		} else {
			tmp = ipf_log_clear(softc, IPL_LOGNAT);
			error = BCOPYOUT(&tmp, data, sizeof(tmp));
			if (error != 0) {
				IPFERROR(60057);
				error = EFAULT;
			}
		}
		break;
	}

	case SIOCSETLG :
		if (!(mode & FWRITE)) {
			IPFERROR(60003);
			error = EPERM;
		} else {
			error = BCOPYIN(data, &softn->ipf_nat_logging,
					sizeof(softn->ipf_nat_logging));
			if (error != 0)
				error = EFAULT;
		}
		break;

	case SIOCGETLG :
		error = BCOPYOUT(&softn->ipf_nat_logging, data,
				 sizeof(softn->ipf_nat_logging));
		if (error != 0) {
			IPFERROR(60004);
			error = EFAULT;
		}
		break;

	case FIONREAD :
		arg = ipf_log_bytesused(softc, IPL_LOGNAT);
		error = BCOPYOUT(&arg, data, sizeof(arg));
		if (error != 0) {
			IPFERROR(60005);
			error = EFAULT;
		}
		break;
#endif
	case SIOCADNAT :
		if (!(mode & FWRITE)) {
			IPFERROR(60006);
			error = EPERM;
		} else if (n != NULL) {
			natd.in_flineno = n->in_flineno;
			(void) ipf_outobj(softc, data, &natd, IPFOBJ_IPNAT);
			IPFERROR(60007);
			error = EEXIST;
		} else if (nt == NULL) {
			IPFERROR(60008);
			error = ENOMEM;
		}
		if (error != 0) {
			MUTEX_EXIT(&softn->ipf_nat_io);
			break;
		}
		if (nat != nt)
			bcopy((char *)nat, (char *)nt, sizeof(*n));
		error = ipf_nat_siocaddnat(softc, softn, nt, getlock);
		MUTEX_EXIT(&softn->ipf_nat_io);
		if (error == 0) {
			nat = NULL;
			nt = NULL;
		}
		break;

	case SIOCRMNAT :
	case SIOCPURGENAT :
		if (!(mode & FWRITE)) {
			IPFERROR(60009);
			error = EPERM;
			n = NULL;
		} else if (n == NULL) {
			IPFERROR(60010);
			error = ESRCH;
		}

		if (error != 0) {
			MUTEX_EXIT(&softn->ipf_nat_io);
			break;
		}
		if (cmd == (ioctlcmd_t)SIOCPURGENAT) {
			error = ipf_outobjsz(softc, data, n, IPFOBJ_IPNAT,
					     n->in_size);
			if (error) {
				MUTEX_EXIT(&softn->ipf_nat_io);
				goto done;
			}
			n->in_flags |= IPN_PURGE;
		}
		ipf_nat_siocdelnat(softc, softn, n, getlock);

		MUTEX_EXIT(&softn->ipf_nat_io);
		n = NULL;
		break;

	case SIOCGNATS :
	    {
		natstat_t *nsp = &softn->ipf_nat_stats;

		nsp->ns_side[0].ns_table = softn->ipf_nat_table[0];
		nsp->ns_side[1].ns_table = softn->ipf_nat_table[1];
		nsp->ns_list = softn->ipf_nat_list;
		nsp->ns_maptable = softn->ipf_hm_maptable;
		nsp->ns_maplist = softn->ipf_hm_maplist;
		nsp->ns_nattab_sz = softn->ipf_nat_table_sz;
		nsp->ns_nattab_max = softn->ipf_nat_table_max;
		nsp->ns_rultab_sz = softn->ipf_nat_maprules_sz;
		nsp->ns_rdrtab_sz = softn->ipf_nat_rdrrules_sz;
		nsp->ns_hostmap_sz = softn->ipf_nat_hostmap_sz;
		nsp->ns_instances = softn->ipf_nat_instances;
		nsp->ns_ticks = softc->ipf_ticks;
#ifdef IPFILTER_LOGGING
		nsp->ns_log_ok = ipf_log_logok(softc, IPF_LOGNAT);
		nsp->ns_log_fail = ipf_log_failures(softc, IPF_LOGNAT);
#else
		nsp->ns_log_ok = 0;
		nsp->ns_log_fail = 0;
#endif
		error = ipf_outobj(softc, data, nsp, IPFOBJ_NATSTAT);
		break;
	    }

	case SIOCGNATL :
	    {
		natlookup_t nl;

		error = ipf_inobj(softc, data, NULL, &nl, IPFOBJ_NATLOOKUP);
		if (error == 0) {
			void *ptr;

			if (getlock) {
				READ_ENTER(&softc->ipf_nat);
			}

			switch (nl.nl_v)
			{
			case 4 :
				ptr = ipf_nat_lookupredir(&nl);
				break;
#ifdef USE_INET6
			case 6 :
				ptr = ipf_nat6_lookupredir(&nl);
				break;
#endif
			default:
				ptr = NULL;
				break;
			}

			if (getlock) {
				RWLOCK_EXIT(&softc->ipf_nat);
			}
			if (ptr != NULL) {
				error = ipf_outobj(softc, data, &nl,
						   IPFOBJ_NATLOOKUP);
			} else {
				IPFERROR(60011);
				error = ESRCH;
			}
		}
		break;
	    }

	case SIOCIPFFL :	/* old SIOCFLNAT & SIOCCNATL */
		if (!(mode & FWRITE)) {
			IPFERROR(60012);
			error = EPERM;
			break;
		}
		if (getlock) {
			WRITE_ENTER(&softc->ipf_nat);
		}

		error = BCOPYIN(data, &arg, sizeof(arg));
		if (error != 0) {
			IPFERROR(60013);
			error = EFAULT;
		} else {
			if (arg == 0)
				ret = ipf_nat_flushtable(softc, softn);
			else if (arg == 1)
				ret = ipf_nat_clearlist(softc, softn);
			else
				ret = ipf_nat_extraflush(softc, softn, arg);
			ipf_proxy_flush(softc->ipf_proxy_soft, arg);
		}

		if (getlock) {
			RWLOCK_EXIT(&softc->ipf_nat);
		}
		if (error == 0) {
			error = BCOPYOUT(&ret, data, sizeof(ret));
		}
		break;

	case SIOCMATCHFLUSH :
		if (!(mode & FWRITE)) {
			IPFERROR(60014);
			error = EPERM;
			break;
		}
		if (getlock) {
			WRITE_ENTER(&softc->ipf_nat);
		}

		error = ipf_nat_matchflush(softc, softn, data);

		if (getlock) {
			RWLOCK_EXIT(&softc->ipf_nat);
		}
		break;

	case SIOCPROXY :
		error = ipf_proxy_ioctl(softc, data, cmd, mode, ctx);
		break;

	case SIOCSTLCK :
		if (!(mode & FWRITE)) {
			IPFERROR(60015);
			error = EPERM;
		} else {
			error = ipf_lock(data, &softn->ipf_nat_lock);
		}
		break;

	case SIOCSTPUT :
		if ((mode & FWRITE) != 0) {
			error = ipf_nat_putent(softc, data, getlock);
		} else {
			IPFERROR(60016);
			error = EACCES;
		}
		break;

	case SIOCSTGSZ :
		if (softn->ipf_nat_lock) {
			error = ipf_nat_getsz(softc, data, getlock);
		} else {
			IPFERROR(60017);
			error = EACCES;
		}
		break;

	case SIOCSTGET :
		if (softn->ipf_nat_lock) {
			error = ipf_nat_getent(softc, data, getlock);
		} else {
			IPFERROR(60018);
			error = EACCES;
		}
		break;

	case SIOCGENITER :
	    {
		ipfgeniter_t iter;
		ipftoken_t *token;
		ipfobj_t obj;

		error = ipf_inobj(softc, data, &obj, &iter, IPFOBJ_GENITER);
		if (error != 0)
			break;

		SPL_SCHED(s);
		token = ipf_token_find(softc, iter.igi_type, uid, ctx);
		if (token != NULL) {
			error  = ipf_nat_iterator(softc, token, &iter, &obj);
			WRITE_ENTER(&softc->ipf_tokens);
			ipf_token_deref(softc, token);
			RWLOCK_EXIT(&softc->ipf_tokens);
		}
		SPL_X(s);
		break;
	    }

	case SIOCIPFDELTOK :
		error = BCOPYIN(data, &arg, sizeof(arg));
		if (error == 0) {
			SPL_SCHED(s);
			error = ipf_token_del(softc, arg, uid, ctx);
			SPL_X(s);
		} else {
			IPFERROR(60019);
			error = EFAULT;
		}
		break;

	case SIOCGTQTAB :
		error = ipf_outobj(softc, data, softn->ipf_nat_tcptq,
				   IPFOBJ_STATETQTAB);
		break;

	case SIOCGTABL :
		error = ipf_nat_gettable(softc, softn, data);
		break;

	default :
		IPFERROR(60020);
		error = EINVAL;
		break;
	}
done:
	if (nat != NULL)
		ipf_nat_rule_fini(softc, nat);
	if (nt != NULL)
		KFREES(nt, nt->in_size);
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_siocaddnat                                          */
/* Returns:     int - 0 == success, != 0 == failure                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              n(I)       - pointer to new NAT rule                        */
/*              np(I)      - pointer to where to insert new NAT rule        */
/*              getlock(I) - flag indicating if lock on  is held            */
/* Mutex Locks: ipf_nat_io                                                   */
/*                                                                          */
/* Handle SIOCADNAT.  Resolve and calculate details inside the NAT rule     */
/* from information passed to the kernel, then add it  to the appropriate   */
/* NAT rule table(s).                                                       */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_siocaddnat(softc, softn, n, getlock)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	ipnat_t *n;
	int getlock;
{
	int error = 0;

	if (ipf_nat_resolverule(softc, n) != 0) {
		IPFERROR(60022);
		return ENOENT;
	}

	if ((n->in_age[0] == 0) && (n->in_age[1] != 0)) {
		IPFERROR(60023);
		return EINVAL;
	}

	if (n->in_redir == (NAT_DIVERTUDP|NAT_MAP)) {
		/*
		 * Prerecord whether or not the destination of the divert
		 * is local or not to the interface the packet is going
		 * to be sent out.
		 */
		n->in_dlocal = ipf_deliverlocal(softc, n->in_v[1],
						n->in_ifps[1], &n->in_ndstip6);
	}

	if (getlock) {
		WRITE_ENTER(&softc->ipf_nat);
	}
	n->in_next = NULL;
	n->in_pnext = softn->ipf_nat_list_tail;
	*n->in_pnext = n;
	softn->ipf_nat_list_tail = &n->in_next;
	n->in_use++;

	if (n->in_redir & NAT_REDIRECT) {
		n->in_flags &= ~IPN_NOTDST;
		switch (n->in_v[0])
		{
		case 4 :
			ipf_nat_addrdr(softn, n);
			break;
#ifdef USE_INET6
		case 6 :
			ipf_nat6_addrdr(softn, n);
			break;
#endif
		default :
			break;
		}
		ATOMIC_INC32(softn->ipf_nat_stats.ns_rules_rdr);
	}

	if (n->in_redir & (NAT_MAP|NAT_MAPBLK)) {
		n->in_flags &= ~IPN_NOTSRC;
		switch (n->in_v[0])
		{
		case 4 :
			ipf_nat_addmap(softn, n);
			break;
#ifdef USE_INET6
		case 6 :
			ipf_nat6_addmap(softn, n);
			break;
#endif
		default :
			break;
		}
		ATOMIC_INC32(softn->ipf_nat_stats.ns_rules_map);
	}

	if (n->in_age[0] != 0)
		n->in_tqehead[0] = ipf_addtimeoutqueue(softc,
						       &softn->ipf_nat_utqe,
						       n->in_age[0]);

	if (n->in_age[1] != 0)
		n->in_tqehead[1] = ipf_addtimeoutqueue(softc,
						       &softn->ipf_nat_utqe,
						       n->in_age[1]);

	MUTEX_INIT(&n->in_lock, "ipnat rule lock");

	n = NULL;
	ATOMIC_INC32(softn->ipf_nat_stats.ns_rules);
#if SOLARIS && !defined(INSTANCES)
	pfil_delayed_copy = 0;
#endif
	if (getlock) {
		RWLOCK_EXIT(&softc->ipf_nat);			/* WRITE */
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_ruleaddrinit                                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              n(I)     - pointer to NAT rule                              */
/*                                                                          */
/* Initialise all of the NAT address structures in a NAT rule.              */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_ruleaddrinit(softc, softn, n)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	ipnat_t *n;
{
	int idx, error;

	if ((n->in_ndst.na_atype == FRI_LOOKUP) &&
	    (n->in_ndst.na_type != IPLT_DSTLIST)) {
		IPFERROR(60071);
		return EINVAL;
	}
	if ((n->in_nsrc.na_atype == FRI_LOOKUP) &&
	    (n->in_nsrc.na_type != IPLT_DSTLIST)) {
		IPFERROR(60069);
		return EINVAL;
	}

	if (n->in_redir == NAT_BIMAP) {
		n->in_ndstaddr = n->in_osrcaddr;
		n->in_ndstmsk = n->in_osrcmsk;
		n->in_odstaddr = n->in_nsrcaddr;
		n->in_odstmsk = n->in_nsrcmsk;

	}

	if (n->in_redir & NAT_REDIRECT)
		idx = 1;
	else
		idx = 0;
	/*
	 * Initialise all of the address fields.
	 */
	error = ipf_nat_nextaddrinit(softc, n->in_names, &n->in_osrc, 1,
				     n->in_ifps[idx]);
	if (error != 0)
		return error;

	error = ipf_nat_nextaddrinit(softc, n->in_names, &n->in_odst, 1,
				     n->in_ifps[idx]);
	if (error != 0)
		return error;

	error = ipf_nat_nextaddrinit(softc, n->in_names, &n->in_nsrc, 1,
				     n->in_ifps[idx]);
	if (error != 0)
		return error;

	error = ipf_nat_nextaddrinit(softc, n->in_names, &n->in_ndst, 1,
				     n->in_ifps[idx]);
	if (error != 0)
		return error;

	if (n->in_redir & NAT_DIVERTUDP)
		ipf_nat_builddivertmp(softn, n);

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_resolvrule                                          */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              n(I)     - pointer to NAT rule                              */
/*                                                                          */
/* Handle SIOCADNAT.  Resolve and calculate details inside the NAT rule     */
/* from information passed to the kernel, then add it  to the appropriate   */
/* NAT rule table(s).                                                       */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_resolverule(softc, n)
	ipf_main_softc_t *softc;
	ipnat_t *n;
{
	char *base;

	base = n->in_names;

	n->in_ifps[0] = ipf_resolvenic(softc, base + n->in_ifnames[0],
				       n->in_v[0]);

	if (n->in_ifnames[1] == -1) {
		n->in_ifnames[1] = n->in_ifnames[0];
		n->in_ifps[1] = n->in_ifps[0];
	} else {
		n->in_ifps[1] = ipf_resolvenic(softc, base + n->in_ifnames[1],
					       n->in_v[1]);
	}

	if (n->in_plabel != -1) {
		if (n->in_redir & NAT_REDIRECT)
			n->in_apr = ipf_proxy_lookup(softc->ipf_proxy_soft,
						     n->in_pr[0],
						     base + n->in_plabel);
		else
			n->in_apr = ipf_proxy_lookup(softc->ipf_proxy_soft,
						     n->in_pr[1],
						     base + n->in_plabel);
		if (n->in_apr == NULL)
			return -1;
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_siocdelnat                                          */
/* Returns:     int - 0 == success, != 0 == failure                         */
/* Parameters:  softc(I)   - pointer to soft context main structure         */
/*              softn(I)   - pointer to NAT context structure               */
/*              n(I)       - pointer to new NAT rule                        */
/*              getlock(I) - flag indicating if lock on  is held            */
/* Mutex Locks: ipf_nat_io                                                  */
/*                                                                          */
/* Handle SIOCADNAT.  Resolve and calculate details inside the NAT rule     */
/* from information passed to the kernel, then add it  to the appropriate   */
/* NAT rule table(s).                                                       */
/* ------------------------------------------------------------------------ */
static void
ipf_nat_siocdelnat(softc, softn, n, getlock)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	ipnat_t *n;
	int getlock;
{
	if (getlock) {
		WRITE_ENTER(&softc->ipf_nat);
	}

	ipf_nat_delrule(softc, softn, n, 1);

	if (getlock) {
		RWLOCK_EXIT(&softc->ipf_nat);			/* READ/WRITE */
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_getsz                                               */
/* Returns:     int - 0 == success, != 0 is the error value.                */
/* Parameters:  softc(I)   - pointer to soft context main structure         */
/*              data(I)    - pointer to natget structure with kernel        */
/*                           pointer get the size of.                       */
/*              getlock(I) - flag indicating whether or not the caller      */
/*                           holds a lock on ipf_nat                        */
/*                                                                          */
/* Handle SIOCSTGSZ.                                                        */
/* Return the size of the nat list entry to be copied back to user space.   */
/* The size of the entry is stored in the ng_sz field and the enture natget */
/* structure is copied back to the user.                                    */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_getsz(softc, data, getlock)
	ipf_main_softc_t *softc;
	caddr_t data;
	int getlock;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	ap_session_t *aps;
	nat_t *nat, *n;
	natget_t ng;
	int error;

	error = BCOPYIN(data, &ng, sizeof(ng));
	if (error != 0) {
		IPFERROR(60024);
		return EFAULT;
	}

	if (getlock) {
		READ_ENTER(&softc->ipf_nat);
	}

	nat = ng.ng_ptr;
	if (!nat) {
		nat = softn->ipf_nat_instances;
		ng.ng_sz = 0;
		/*
		 * Empty list so the size returned is 0.  Simple.
		 */
		if (nat == NULL) {
			if (getlock) {
				RWLOCK_EXIT(&softc->ipf_nat);
			}
			error = BCOPYOUT(&ng, data, sizeof(ng));
			if (error != 0) {
				IPFERROR(60025);
				return EFAULT;
			}
			return 0;
		}
	} else {
		/*
		 * Make sure the pointer we're copying from exists in the
		 * current list of entries.  Security precaution to prevent
		 * copying of random kernel data.
		 */
		for (n = softn->ipf_nat_instances; n; n = n->nat_next)
			if (n == nat)
				break;
		if (n == NULL) {
			if (getlock) {
				RWLOCK_EXIT(&softc->ipf_nat);
			}
			IPFERROR(60026);
			return ESRCH;
		}
	}

	/*
	 * Incluse any space required for proxy data structures.
	 */
	ng.ng_sz = sizeof(nat_save_t);
	aps = nat->nat_aps;
	if (aps != NULL) {
		ng.ng_sz += sizeof(ap_session_t) - 4;
		if (aps->aps_data != 0)
			ng.ng_sz += aps->aps_psiz;
	}
	if (getlock) {
		RWLOCK_EXIT(&softc->ipf_nat);
	}

	error = BCOPYOUT(&ng, data, sizeof(ng));
	if (error != 0) {
		IPFERROR(60027);
		return EFAULT;
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_getent                                              */
/* Returns:     int - 0 == success, != 0 is the error value.                */
/* Parameters:  softc(I)   - pointer to soft context main structure         */
/*              data(I)    - pointer to natget structure with kernel pointer*/
/*                           to NAT structure to copy out.                  */
/*              getlock(I) - flag indicating whether or not the caller      */
/*                           holds a lock on ipf_nat                        */
/*                                                                          */
/* Handle SIOCSTGET.                                                        */
/* Copies out NAT entry to user space.  Any additional data held for a      */
/* proxy is also copied, as to is the NAT rule which was responsible for it */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_getent(softc, data, getlock)
	ipf_main_softc_t *softc;
	caddr_t data;
	int getlock;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	int error, outsize;
	ap_session_t *aps;
	nat_save_t *ipn, ipns;
	nat_t *n, *nat;

	error = ipf_inobj(softc, data, NULL, &ipns, IPFOBJ_NATSAVE);
	if (error != 0)
		return error;

	if ((ipns.ipn_dsize < sizeof(ipns)) || (ipns.ipn_dsize > 81920)) {
		IPFERROR(60028);
		return EINVAL;
	}

	KMALLOCS(ipn, nat_save_t *, ipns.ipn_dsize);
	if (ipn == NULL) {
		IPFERROR(60029);
		return ENOMEM;
	}

	if (getlock) {
		READ_ENTER(&softc->ipf_nat);
	}

	ipn->ipn_dsize = ipns.ipn_dsize;
	nat = ipns.ipn_next;
	if (nat == NULL) {
		nat = softn->ipf_nat_instances;
		if (nat == NULL) {
			if (softn->ipf_nat_instances == NULL) {
				IPFERROR(60030);
				error = ENOENT;
			}
			goto finished;
		}
	} else {
		/*
		 * Make sure the pointer we're copying from exists in the
		 * current list of entries.  Security precaution to prevent
		 * copying of random kernel data.
		 */
		for (n = softn->ipf_nat_instances; n; n = n->nat_next)
			if (n == nat)
				break;
		if (n == NULL) {
			IPFERROR(60031);
			error = ESRCH;
			goto finished;
		}
	}
	ipn->ipn_next = nat->nat_next;

	/*
	 * Copy the NAT structure.
	 */
	bcopy((char *)nat, &ipn->ipn_nat, sizeof(*nat));

	/*
	 * If we have a pointer to the NAT rule it belongs to, save that too.
	 */
	if (nat->nat_ptr != NULL)
		bcopy((char *)nat->nat_ptr, (char *)&ipn->ipn_ipnat,
		      sizeof(nat->nat_ptr));

	/*
	 * If we also know the NAT entry has an associated filter rule,
	 * save that too.
	 */
	if (nat->nat_fr != NULL)
		bcopy((char *)nat->nat_fr, (char *)&ipn->ipn_fr,
		      sizeof(ipn->ipn_fr));

	/*
	 * Last but not least, if there is an application proxy session set
	 * up for this NAT entry, then copy that out too, including any
	 * private data saved along side it by the proxy.
	 */
	aps = nat->nat_aps;
	outsize = ipn->ipn_dsize - sizeof(*ipn) + sizeof(ipn->ipn_data);
	if (aps != NULL) {
		char *s;

		if (outsize < sizeof(*aps)) {
			IPFERROR(60032);
			error = ENOBUFS;
			goto finished;
		}

		s = ipn->ipn_data;
		bcopy((char *)aps, s, sizeof(*aps));
		s += sizeof(*aps);
		outsize -= sizeof(*aps);
		if ((aps->aps_data != NULL) && (outsize >= aps->aps_psiz))
			bcopy(aps->aps_data, s, aps->aps_psiz);
		else {
			IPFERROR(60033);
			error = ENOBUFS;
		}
	}
	if (error == 0) {
		error = ipf_outobjsz(softc, data, ipn, IPFOBJ_NATSAVE,
				     ipns.ipn_dsize);
	}

finished:
	if (ipn != NULL) {
		KFREES(ipn, ipns.ipn_dsize);
	}
	if (getlock) {
		RWLOCK_EXIT(&softc->ipf_nat);
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_putent                                              */
/* Returns:     int - 0 == success, != 0 is the error value.                */
/* Parameters:  softc(I)   - pointer to soft context main structure         */
/*              data(I)    - pointer to natget structure with NAT           */
/*                           structure information to load into the kernel  */
/*              getlock(I) - flag indicating whether or not a write lock    */
/*                           on is already held.                            */
/*                                                                          */
/* Handle SIOCSTPUT.                                                        */
/* Loads a NAT table entry from user space, including a NAT rule, proxy and */
/* firewall rule data structures, if pointers to them indicate so.          */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_putent(softc, data, getlock)
	ipf_main_softc_t *softc;
	caddr_t data;
	int getlock;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	nat_save_t ipn, *ipnn;
	ap_session_t *aps;
	nat_t *n, *nat;
	frentry_t *fr;
	fr_info_t fin;
	ipnat_t *in;
	int error;

	error = ipf_inobj(softc, data, NULL, &ipn, IPFOBJ_NATSAVE);
	if (error != 0)
		return error;

	/*
	 * Initialise early because of code at junkput label.
	 */
	n = NULL;
	in = NULL;
	aps = NULL;
	nat = NULL;
	ipnn = NULL;
	fr = NULL;

	/*
	 * New entry, copy in the rest of the NAT entry if it's size is more
	 * than just the nat_t structure.
	 */
	if (ipn.ipn_dsize > sizeof(ipn)) {
		if (ipn.ipn_dsize > 81920) {
			IPFERROR(60034);
			error = ENOMEM;
			goto junkput;
		}

		KMALLOCS(ipnn, nat_save_t *, ipn.ipn_dsize);
		if (ipnn == NULL) {
			IPFERROR(60035);
			return ENOMEM;
		}

		bzero(ipnn, ipn.ipn_dsize);
		error = ipf_inobjsz(softc, data, ipnn, IPFOBJ_NATSAVE,
				    ipn.ipn_dsize);
		if (error != 0) {
			goto junkput;
		}
	} else
		ipnn = &ipn;

	KMALLOC(nat, nat_t *);
	if (nat == NULL) {
		IPFERROR(60037);
		error = ENOMEM;
		goto junkput;
	}

	bcopy((char *)&ipnn->ipn_nat, (char *)nat, sizeof(*nat));

	switch (nat->nat_v[0])
	{
	case 4:
#ifdef USE_INET6
	case 6 :
#endif
		break;
	default :
		IPFERROR(60061);
		error = EPROTONOSUPPORT;
		goto junkput;
		/*NOTREACHED*/
	}

	/*
	 * Initialize all these so that ipf_nat_delete() doesn't cause a crash.
	 */
	bzero((char *)nat, offsetof(struct nat, nat_tqe));
	nat->nat_tqe.tqe_pnext = NULL;
	nat->nat_tqe.tqe_next = NULL;
	nat->nat_tqe.tqe_ifq = NULL;
	nat->nat_tqe.tqe_parent = nat;

	/*
	 * Restore the rule associated with this nat session
	 */
	in = ipnn->ipn_nat.nat_ptr;
	if (in != NULL) {
		KMALLOCS(in, ipnat_t *, ipnn->ipn_ipnat.in_size);
		nat->nat_ptr = in;
		if (in == NULL) {
			IPFERROR(60038);
			error = ENOMEM;
			goto junkput;
		}
		bcopy((char *)&ipnn->ipn_ipnat, (char *)in,
		      ipnn->ipn_ipnat.in_size);
		in->in_use = 1;
		in->in_flags |= IPN_DELETE;

		ATOMIC_INC32(softn->ipf_nat_stats.ns_rules);

		if (ipf_nat_resolverule(softc, in) != 0) {
			IPFERROR(60039);
			error = ESRCH;
			goto junkput;
		}
	}

	/*
	 * Check that the NAT entry doesn't already exist in the kernel.
	 *
	 * For NAT_OUTBOUND, we're lookup for a duplicate MAP entry.  To do
	 * this, we check to see if the inbound combination of addresses and
	 * ports is already known.  Similar logic is applied for NAT_INBOUND.
	 *
	 */
	bzero((char *)&fin, sizeof(fin));
	fin.fin_v = nat->nat_v[0];
	fin.fin_p = nat->nat_pr[0];
	fin.fin_rev = nat->nat_rev;
	fin.fin_ifp = nat->nat_ifps[0];
	fin.fin_data[0] = ntohs(nat->nat_ndport);
	fin.fin_data[1] = ntohs(nat->nat_nsport);

	switch (nat->nat_dir)
	{
	case NAT_OUTBOUND :
	case NAT_DIVERTOUT :
		if (getlock) {
			READ_ENTER(&softc->ipf_nat);
		}

		fin.fin_v = nat->nat_v[1];
		if (nat->nat_v[1] == 4) {
			n = ipf_nat_inlookup(&fin, nat->nat_flags, fin.fin_p,
					     nat->nat_ndstip, nat->nat_nsrcip);
#ifdef USE_INET6
		} else if (nat->nat_v[1] == 6) {
			n = ipf_nat6_inlookup(&fin, nat->nat_flags, fin.fin_p,
					      &nat->nat_ndst6.in6,
					      &nat->nat_nsrc6.in6);
#endif
		}

		if (getlock) {
			RWLOCK_EXIT(&softc->ipf_nat);
		}
		if (n != NULL) {
			IPFERROR(60040);
			error = EEXIST;
			goto junkput;
		}
		break;

	case NAT_INBOUND :
	case NAT_DIVERTIN :
		if (getlock) {
			READ_ENTER(&softc->ipf_nat);
		}

		if (fin.fin_v == 4) {
			n = ipf_nat_outlookup(&fin, nat->nat_flags, fin.fin_p,
					      nat->nat_ndstip,
					      nat->nat_nsrcip);
#ifdef USE_INET6
		} else if (fin.fin_v == 6) {
			n = ipf_nat6_outlookup(&fin, nat->nat_flags, fin.fin_p,
					       &nat->nat_ndst6.in6,
					       &nat->nat_nsrc6.in6);
#endif
		}

		if (getlock) {
			RWLOCK_EXIT(&softc->ipf_nat);
		}
		if (n != NULL) {
			IPFERROR(60041);
			error = EEXIST;
			goto junkput;
		}
		break;

	default :
		IPFERROR(60042);
		error = EINVAL;
		goto junkput;
	}

	/*
	 * Restore ap_session_t structure.  Include the private data allocated
	 * if it was there.
	 */
	aps = nat->nat_aps;
	if (aps != NULL) {
		KMALLOC(aps, ap_session_t *);
		nat->nat_aps = aps;
		if (aps == NULL) {
			IPFERROR(60043);
			error = ENOMEM;
			goto junkput;
		}
		bcopy(ipnn->ipn_data, (char *)aps, sizeof(*aps));
		if (in != NULL)
			aps->aps_apr = in->in_apr;
		else
			aps->aps_apr = NULL;
		if (aps->aps_psiz != 0) {
			if (aps->aps_psiz > 81920) {
				IPFERROR(60044);
				error = ENOMEM;
				goto junkput;
			}
			KMALLOCS(aps->aps_data, void *, aps->aps_psiz);
			if (aps->aps_data == NULL) {
				IPFERROR(60045);
				error = ENOMEM;
				goto junkput;
			}
			bcopy(ipnn->ipn_data + sizeof(*aps), aps->aps_data,
			      aps->aps_psiz);
		} else {
			aps->aps_psiz = 0;
			aps->aps_data = NULL;
		}
	}

	/*
	 * If there was a filtering rule associated with this entry then
	 * build up a new one.
	 */
	fr = nat->nat_fr;
	if (fr != NULL) {
		if ((nat->nat_flags & SI_NEWFR) != 0) {
			KMALLOC(fr, frentry_t *);
			nat->nat_fr = fr;
			if (fr == NULL) {
				IPFERROR(60046);
				error = ENOMEM;
				goto junkput;
			}
			ipnn->ipn_nat.nat_fr = fr;
			fr->fr_ref = 1;
			(void) ipf_outobj(softc, data, ipnn, IPFOBJ_NATSAVE);
			bcopy((char *)&ipnn->ipn_fr, (char *)fr, sizeof(*fr));

			fr->fr_ref = 1;
			fr->fr_dsize = 0;
			fr->fr_data = NULL;
			fr->fr_type = FR_T_NONE;

			MUTEX_NUKE(&fr->fr_lock);
			MUTEX_INIT(&fr->fr_lock, "nat-filter rule lock");
		} else {
			if (getlock) {
				READ_ENTER(&softc->ipf_nat);
			}
			for (n = softn->ipf_nat_instances; n; n = n->nat_next)
				if (n->nat_fr == fr)
					break;

			if (n != NULL) {
				MUTEX_ENTER(&fr->fr_lock);
				fr->fr_ref++;
				MUTEX_EXIT(&fr->fr_lock);
			}
			if (getlock) {
				RWLOCK_EXIT(&softc->ipf_nat);
			}

			if (n == NULL) {
				IPFERROR(60047);
				error = ESRCH;
				goto junkput;
			}
		}
	}

	if (ipnn != &ipn) {
		KFREES(ipnn, ipn.ipn_dsize);
		ipnn = NULL;
	}

	if (getlock) {
		WRITE_ENTER(&softc->ipf_nat);
	}

	if (fin.fin_v == 4)
		error = ipf_nat_finalise(&fin, nat);
#ifdef USE_INET6
	else
		error = ipf_nat6_finalise(&fin, nat);
#endif

	if (getlock) {
		RWLOCK_EXIT(&softc->ipf_nat);
	}

	if (error == 0)
		return 0;

	IPFERROR(60048);
	error = ENOMEM;

junkput:
	if (fr != NULL) {
		(void) ipf_derefrule(softc, &fr);
	}

	if ((ipnn != NULL) && (ipnn != &ipn)) {
		KFREES(ipnn, ipn.ipn_dsize);
	}
	if (nat != NULL) {
		if (aps != NULL) {
			if (aps->aps_data != NULL) {
				KFREES(aps->aps_data, aps->aps_psiz);
			}
			KFREE(aps);
		}
		if (in != NULL) {
			if (in->in_apr)
				ipf_proxy_deref(in->in_apr);
			KFREES(in, in->in_size);
		}
		KFREE(nat);
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_delete                                              */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I)   - pointer to soft context main structure         */
/*              nat(I)     - pointer to NAT structure to delete             */
/*              logtype(I) - type of LOG record to create before deleting   */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* Delete a nat entry from the various lists and table.  If NAT logging is  */
/* enabled then generate a NAT log record for this event.                   */
/* ------------------------------------------------------------------------ */
void
ipf_nat_delete(softc, nat, logtype)
	ipf_main_softc_t *softc;
	struct nat *nat;
	int logtype;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	int madeorphan = 0, bkt, removed = 0;
	nat_stat_side_t *nss;
	struct ipnat *ipn;

	if (logtype != 0 && softn->ipf_nat_logging != 0)
		ipf_nat_log(softc, softn, nat, logtype);

	/*
	 * Take it as a general indication that all the pointers are set if
	 * nat_pnext is set.
	 */
	if (nat->nat_pnext != NULL) {
		removed = 1;

		bkt = nat->nat_hv[0] % softn->ipf_nat_table_sz;
		nss = &softn->ipf_nat_stats.ns_side[0];
		if (nss->ns_bucketlen[bkt] > 0)
			nss->ns_bucketlen[bkt]--;
		if (nss->ns_bucketlen[bkt] == 0) {
			nss->ns_inuse--;
		}

		bkt = nat->nat_hv[1] % softn->ipf_nat_table_sz;
		nss = &softn->ipf_nat_stats.ns_side[1];
		if (nss->ns_bucketlen[bkt] > 0)
			nss->ns_bucketlen[bkt]--;
		if (nss->ns_bucketlen[bkt] == 0) {
			nss->ns_inuse--;
		}

		*nat->nat_pnext = nat->nat_next;
		if (nat->nat_next != NULL) {
			nat->nat_next->nat_pnext = nat->nat_pnext;
			nat->nat_next = NULL;
		}
		nat->nat_pnext = NULL;

		*nat->nat_phnext[0] = nat->nat_hnext[0];
		if (nat->nat_hnext[0] != NULL) {
			nat->nat_hnext[0]->nat_phnext[0] = nat->nat_phnext[0];
			nat->nat_hnext[0] = NULL;
		}
		nat->nat_phnext[0] = NULL;

		*nat->nat_phnext[1] = nat->nat_hnext[1];
		if (nat->nat_hnext[1] != NULL) {
			nat->nat_hnext[1]->nat_phnext[1] = nat->nat_phnext[1];
			nat->nat_hnext[1] = NULL;
		}
		nat->nat_phnext[1] = NULL;

		if ((nat->nat_flags & SI_WILDP) != 0) {
			ATOMIC_DEC32(softn->ipf_nat_stats.ns_wilds);
		}
		madeorphan = 1;
	}

	if (nat->nat_me != NULL) {
		*nat->nat_me = NULL;
		nat->nat_me = NULL;
		nat->nat_ref--;
		ASSERT(nat->nat_ref >= 0);
	}

	if (nat->nat_tqe.tqe_ifq != NULL) {
		/*
		 * No call to ipf_freetimeoutqueue() is made here, they are
		 * garbage collected in ipf_nat_expire().
		 */
		(void) ipf_deletequeueentry(&nat->nat_tqe);
	}

	if (nat->nat_sync) {
		ipf_sync_del_nat(softc->ipf_sync_soft, nat->nat_sync);
		nat->nat_sync = NULL;
	}

	if (logtype == NL_EXPIRE)
		softn->ipf_nat_stats.ns_expire++;

	MUTEX_ENTER(&nat->nat_lock);
	/*
	 * NL_DESTROY should only be passed in when we've got nat_ref >= 2.
	 * This happens when a nat'd packet is blocked and we want to throw
	 * away the NAT session.
	 */
	if (logtype == NL_DESTROY) {
		if (nat->nat_ref > 2) {
			nat->nat_ref -= 2;
			MUTEX_EXIT(&nat->nat_lock);
			if (removed)
				softn->ipf_nat_stats.ns_orphans++;
			return;
		}
	} else if (nat->nat_ref > 1) {
		nat->nat_ref--;
		MUTEX_EXIT(&nat->nat_lock);
		if (madeorphan == 1)
			softn->ipf_nat_stats.ns_orphans++;
		return;
	}
	ASSERT(nat->nat_ref >= 0);
	MUTEX_EXIT(&nat->nat_lock);

	nat->nat_ref = 0;

	if (madeorphan == 0)
		softn->ipf_nat_stats.ns_orphans--;

	/*
	 * At this point, nat_ref can be either 0 or -1
	 */
	softn->ipf_nat_stats.ns_proto[nat->nat_pr[0]]--;

	if (nat->nat_fr != NULL) {
		(void) ipf_derefrule(softc, &nat->nat_fr);
	}

	if (nat->nat_hm != NULL) {
		ipf_nat_hostmapdel(softc, &nat->nat_hm);
	}

	/*
	 * If there is an active reference from the nat entry to its parent
	 * rule, decrement the rule's reference count and free it too if no
	 * longer being used.
	 */
	ipn = nat->nat_ptr;
	nat->nat_ptr = NULL;

	if (ipn != NULL) {
		ipn->in_space++;
		ipf_nat_rule_deref(softc, &ipn);
	}

	if (nat->nat_aps != NULL) {
		ipf_proxy_free(softc, nat->nat_aps);
		nat->nat_aps = NULL;
	}

	MUTEX_DESTROY(&nat->nat_lock);

	softn->ipf_nat_stats.ns_active--;

	/*
	 * If there's a fragment table entry too for this nat entry, then
	 * dereference that as well.  This is after nat_lock is released
	 * because of Tru64.
	 */
	ipf_frag_natforget(softc, (void *)nat);

	KFREE(nat);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_flushtable                                          */
/* Returns:     int - number of NAT rules deleted                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* Deletes all currently active NAT sessions.  In deleting each NAT entry a */
/* log record should be emitted in ipf_nat_delete() if NAT logging is       */
/* enabled.                                                                 */
/* ------------------------------------------------------------------------ */
/*
 * nat_flushtable - clear the NAT table of all mapping entries.
 */
static int
ipf_nat_flushtable(softc, softn)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
{
	nat_t *nat;
	int j = 0;

	/*
	 * ALL NAT mappings deleted, so lets just make the deletions
	 * quicker.
	 */
	if (softn->ipf_nat_table[0] != NULL)
		bzero((char *)softn->ipf_nat_table[0],
		      sizeof(softn->ipf_nat_table[0]) *
		      softn->ipf_nat_table_sz);
	if (softn->ipf_nat_table[1] != NULL)
		bzero((char *)softn->ipf_nat_table[1],
		      sizeof(softn->ipf_nat_table[1]) *
		      softn->ipf_nat_table_sz);

	while ((nat = softn->ipf_nat_instances) != NULL) {
		ipf_nat_delete(softc, nat, NL_FLUSH);
		j++;
	}

	return j;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_clearlist                                           */
/* Returns:     int - number of NAT/RDR rules deleted                       */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*                                                                          */
/* Delete all rules in the current list of rules.  There is nothing elegant */
/* about this cleanup: simply free all entries on the list of rules and     */
/* clear out the tables used for hashed NAT rule lookups.                   */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_clearlist(softc, softn)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
{
	ipnat_t *n;
	int i = 0;

	if (softn->ipf_nat_map_rules != NULL) {
		bzero((char *)softn->ipf_nat_map_rules,
		      sizeof(*softn->ipf_nat_map_rules) *
		      softn->ipf_nat_maprules_sz);
	}
	if (softn->ipf_nat_rdr_rules != NULL) {
		bzero((char *)softn->ipf_nat_rdr_rules,
		      sizeof(*softn->ipf_nat_rdr_rules) *
		      softn->ipf_nat_rdrrules_sz);
	}

	while ((n = softn->ipf_nat_list) != NULL) {
		ipf_nat_delrule(softc, softn, n, 0);
		i++;
	}
#if SOLARIS && !defined(INSTANCES)
	pfil_delayed_copy = 1;
#endif
	return i;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_delrule                                             */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              np(I)    - pointer to NAT rule to delete                    */
/*              purge(I) - 1 == allow purge, 0 == prevent purge             */
/* Locks:       WRITE(ipf_nat)                                              */
/*                                                                          */
/* Preventing "purge" from occuring is allowed because when all of the NAT  */
/* rules are being removed, allowing the "purge" to walk through the list   */
/* of NAT sessions, possibly multiple times, would be a large performance   */
/* hit, on the order of O(N^2).                                             */
/* ------------------------------------------------------------------------ */
static void
ipf_nat_delrule(softc, softn, np, purge)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	ipnat_t *np;
	int purge;
{

	if (np->in_pnext != NULL) {
		*np->in_pnext = np->in_next;
		if (np->in_next != NULL)
			np->in_next->in_pnext = np->in_pnext;
		if (softn->ipf_nat_list_tail == &np->in_next)
			softn->ipf_nat_list_tail = np->in_pnext;
	}

	if ((purge == 1) && ((np->in_flags & IPN_PURGE) != 0)) {
		nat_t *next;
		nat_t *nat;

		for (next = softn->ipf_nat_instances; (nat = next) != NULL;) {
			next = nat->nat_next;
			if (nat->nat_ptr == np)
				ipf_nat_delete(softc, nat, NL_PURGE);
		}
	}

	if ((np->in_flags & IPN_DELETE) == 0) {
		if (np->in_redir & NAT_REDIRECT) {
			switch (np->in_v[0])
			{
			case 4 :
				ipf_nat_delrdr(softn, np);
				break;
#ifdef USE_INET6
			case 6 :
				ipf_nat6_delrdr(softn, np);
				break;
#endif
			}
		}
		if (np->in_redir & (NAT_MAPBLK|NAT_MAP)) {
			switch (np->in_v[0])
			{
			case 4 :
				ipf_nat_delmap(softn, np);
				break;
#ifdef USE_INET6
			case 6 :
				ipf_nat6_delmap(softn, np);
				break;
#endif
			}
		}
	}

	np->in_flags |= IPN_DELETE;
	ipf_nat_rule_deref(softc, &np);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_newmap                                              */
/* Returns:     int - -1 == error, 0 == success                             */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to NAT entry                               */
/*              ni(I)  - pointer to structure with misc. information needed */
/*                       to create new NAT entry.                           */
/*                                                                          */
/* Given an empty NAT structure, populate it with new information about a   */
/* new NAT session, as defined by the matching NAT rule.                    */
/* ni.nai_ip is passed in uninitialised and must be set, in host byte order,*/
/* to the new IP address for the translation.                               */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_newmap(fin, nat, ni)
	fr_info_t *fin;
	nat_t *nat;
	natinfo_t *ni;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_short st_port, dport, sport, port, sp, dp;
	struct in_addr in, inb;
	hostmap_t *hm;
	u_32_t flags;
	u_32_t st_ip;
	ipnat_t *np;
	nat_t *natl;
	int l;

	/*
	 * If it's an outbound packet which doesn't match any existing
	 * record, then create a new port
	 */
	l = 0;
	hm = NULL;
	np = ni->nai_np;
	st_ip = np->in_snip;
	st_port = np->in_spnext;
	flags = nat->nat_flags;

	if (flags & IPN_ICMPQUERY) {
		sport = fin->fin_data[1];
		dport = 0;
	} else {
		sport = htons(fin->fin_data[0]);
		dport = htons(fin->fin_data[1]);
	}

	/*
	 * Do a loop until we either run out of entries to try or we find
	 * a NAT mapping that isn't currently being used.  This is done
	 * because the change to the source is not (usually) being fixed.
	 */
	do {
		port = 0;
		in.s_addr = htonl(np->in_snip);
		if (l == 0) {
			/*
			 * Check to see if there is an existing NAT
			 * setup for this IP address pair.
			 */
			hm = ipf_nat_hostmap(softn, np, fin->fin_src,
					     fin->fin_dst, in, 0);
			if (hm != NULL)
				in.s_addr = hm->hm_nsrcip.s_addr;
		} else if ((l == 1) && (hm != NULL)) {
			ipf_nat_hostmapdel(softc, &hm);
		}
		in.s_addr = ntohl(in.s_addr);

		nat->nat_hm = hm;

		if ((np->in_nsrcmsk == 0xffffffff) && (np->in_spnext == 0)) {
			if (l > 0) {
				NBUMPSIDEX(1, ns_exhausted, ns_exhausted_1);
				DT4(ns_exhausted_1, fr_info_t *, fin, nat_t *, nat, natinfo_t *, ni, ipnat_t *, np);
				return -1;
			}
		}

		if (np->in_redir == NAT_BIMAP &&
		    np->in_osrcmsk == np->in_nsrcmsk) {
			/*
			 * map the address block in a 1:1 fashion
			 */
			in.s_addr = np->in_nsrcaddr;
			in.s_addr |= fin->fin_saddr & ~np->in_osrcmsk;
			in.s_addr = ntohl(in.s_addr);

		} else if (np->in_redir & NAT_MAPBLK) {
			if ((l >= np->in_ppip) || ((l > 0) &&
			     !(flags & IPN_TCPUDP))) {
				NBUMPSIDEX(1, ns_exhausted, ns_exhausted_2);
				DT4(ns_exhausted_2, fr_info_t *, fin, nat_t *, nat, natinfo_t *, ni, ipnat_t *, np);
				return -1;
			}
			/*
			 * map-block - Calculate destination address.
			 */
			in.s_addr = ntohl(fin->fin_saddr);
			in.s_addr &= ntohl(~np->in_osrcmsk);
			inb.s_addr = in.s_addr;
			in.s_addr /= np->in_ippip;
			in.s_addr &= ntohl(~np->in_nsrcmsk);
			in.s_addr += ntohl(np->in_nsrcaddr);
			/*
			 * Calculate destination port.
			 */
			if ((flags & IPN_TCPUDP) &&
			    (np->in_ppip != 0)) {
				port = ntohs(sport) + l;
				port %= np->in_ppip;
				port += np->in_ppip *
					(inb.s_addr % np->in_ippip);
				port += MAPBLK_MINPORT;
				port = htons(port);
			}

		} else if ((np->in_nsrcaddr == 0) &&
			   (np->in_nsrcmsk == 0xffffffff)) {
			i6addr_t in6;

			/*
			 * 0/32 - use the interface's IP address.
			 */
			if ((l > 0) ||
			    ipf_ifpaddr(softc, 4, FRI_NORMAL, fin->fin_ifp,
				       &in6, NULL) == -1) {
				NBUMPSIDEX(1, ns_new_ifpaddr, ns_new_ifpaddr_1);
				DT4(ns_new_ifpaddr_1, fr_info_t *, fin, nat_t *, nat, natinfo_t *, ni, ipnat_t *, np);
				return -1;
			}
			in.s_addr = ntohl(in6.in4.s_addr);

		} else if ((np->in_nsrcaddr == 0) && (np->in_nsrcmsk == 0)) {
			/*
			 * 0/0 - use the original source address/port.
			 */
			if (l > 0) {
				NBUMPSIDEX(1, ns_exhausted, ns_exhausted_3);
				DT4(ns_exhausted_3, fr_info_t *, fin, nat_t *, nat, natinfo_t *, ni, ipnat_t *, np);
				return -1;
			}
			in.s_addr = ntohl(fin->fin_saddr);

		} else if ((np->in_nsrcmsk != 0xffffffff) &&
			   (np->in_spnext == 0) && ((l > 0) || (hm == NULL)))
			np->in_snip++;

		natl = NULL;

		if ((flags & IPN_TCPUDP) &&
		    ((np->in_redir & NAT_MAPBLK) == 0) &&
		    (np->in_flags & IPN_AUTOPORTMAP)) {
			/*
			 * "ports auto" (without map-block)
			 */
			if ((l > 0) && (l % np->in_ppip == 0)) {
				if ((l > np->in_ppip) &&
				    np->in_nsrcmsk != 0xffffffff)
					np->in_snip++;
			}
			if (np->in_ppip != 0) {
				port = ntohs(sport);
				port += (l % np->in_ppip);
				port %= np->in_ppip;
				port += np->in_ppip *
					(ntohl(fin->fin_saddr) %
					 np->in_ippip);
				port += MAPBLK_MINPORT;
				port = htons(port);
			}

		} else if (((np->in_redir & NAT_MAPBLK) == 0) &&
			   (flags & IPN_TCPUDPICMP) && (np->in_spnext != 0)) {
			/*
			 * Standard port translation.  Select next port.
			 */
			if (np->in_flags & IPN_SEQUENTIAL) {
				port = np->in_spnext;
			} else {
				port = ipf_random() % (np->in_spmax -
						       np->in_spmin + 1);
				port += np->in_spmin;
			}
			port = htons(port);
			np->in_spnext++;

			if (np->in_spnext > np->in_spmax) {
				np->in_spnext = np->in_spmin;
				if (np->in_nsrcmsk != 0xffffffff)
					np->in_snip++;
			}
		}

		if (np->in_flags & IPN_SIPRANGE) {
			if (np->in_snip > ntohl(np->in_nsrcmsk))
				np->in_snip = ntohl(np->in_nsrcaddr);
		} else {
			if ((np->in_nsrcmsk != 0xffffffff) &&
			    ((np->in_snip + 1) & ntohl(np->in_nsrcmsk)) >
			    ntohl(np->in_nsrcaddr))
				np->in_snip = ntohl(np->in_nsrcaddr) + 1;
		}

		if ((port == 0) && (flags & (IPN_TCPUDPICMP|IPN_ICMPQUERY)))
			port = sport;

		/*
		 * Here we do a lookup of the connection as seen from
		 * the outside.  If an IP# pair already exists, try
		 * again.  So if you have A->B becomes C->B, you can
		 * also have D->E become C->E but not D->B causing
		 * another C->B.  Also take protocol and ports into
		 * account when determining whether a pre-existing
		 * NAT setup will cause an external conflict where
		 * this is appropriate.
		 */
		inb.s_addr = htonl(in.s_addr);
		sp = fin->fin_data[0];
		dp = fin->fin_data[1];
		fin->fin_data[0] = fin->fin_data[1];
		fin->fin_data[1] = ntohs(port);
		natl = ipf_nat_inlookup(fin, flags & ~(SI_WILDP|NAT_SEARCH),
					(u_int)fin->fin_p, fin->fin_dst, inb);
		fin->fin_data[0] = sp;
		fin->fin_data[1] = dp;

		/*
		 * Has the search wrapped around and come back to the
		 * start ?
		 */
		if ((natl != NULL) &&
		    (np->in_spnext != 0) && (st_port == np->in_spnext) &&
		    (np->in_snip != 0) && (st_ip == np->in_snip)) {
			NBUMPSIDED(1, ns_wrap);
			DT4(ns_wrap, fr_info_t *, fin, nat_t *, nat, natinfo_t *, ni, ipnat_t *, np);
			return -1;
		}
		l++;
	} while (natl != NULL);

	/* Setup the NAT table */
	nat->nat_osrcip = fin->fin_src;
	nat->nat_nsrcaddr = htonl(in.s_addr);
	nat->nat_odstip = fin->fin_dst;
	nat->nat_ndstip = fin->fin_dst;
	if (nat->nat_hm == NULL)
		nat->nat_hm = ipf_nat_hostmap(softn, np, fin->fin_src,
					      fin->fin_dst, nat->nat_nsrcip,
					      0);

	if (flags & IPN_TCPUDP) {
		nat->nat_osport = sport;
		nat->nat_nsport = port;	/* sport */
		nat->nat_odport = dport;
		nat->nat_ndport = dport;
		((tcphdr_t *)fin->fin_dp)->th_sport = port;
	} else if (flags & IPN_ICMPQUERY) {
		nat->nat_oicmpid = fin->fin_data[1];
		((icmphdr_t *)fin->fin_dp)->icmp_id = port;
		nat->nat_nicmpid = port;
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_newrdr                                              */
/* Returns:     int - -1 == error, 0 == success (no move), 1 == success and */
/*                    allow rule to be moved if IPN_ROUNDR is set.          */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to NAT entry                               */
/*              ni(I)  - pointer to structure with misc. information needed */
/*                       to create new NAT entry.                           */
/*                                                                          */
/* ni.nai_ip is passed in uninitialised and must be set, in host byte order,*/
/* to the new IP address for the translation.                               */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_newrdr(fin, nat, ni)
	fr_info_t *fin;
	nat_t *nat;
	natinfo_t *ni;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_short nport, dport, sport;
	struct in_addr in, inb;
	u_short sp, dp;
	hostmap_t *hm;
	u_32_t flags;
	ipnat_t *np;
	nat_t *natl;
	int move;

	move = 1;
	hm = NULL;
	in.s_addr = 0;
	np = ni->nai_np;
	flags = nat->nat_flags;

	if (flags & IPN_ICMPQUERY) {
		dport = fin->fin_data[1];
		sport = 0;
	} else {
		sport = htons(fin->fin_data[0]);
		dport = htons(fin->fin_data[1]);
	}

	/* TRACE sport, dport */


	/*
	 * If the matching rule has IPN_STICKY set, then we want to have the
	 * same rule kick in as before.  Why would this happen?  If you have
	 * a collection of rdr rules with "round-robin sticky", the current
	 * packet might match a different one to the previous connection but
	 * we want the same destination to be used.
	 */
	if (((np->in_flags & (IPN_ROUNDR|IPN_SPLIT)) != 0) &&
	    ((np->in_flags & IPN_STICKY) != 0)) {
		hm = ipf_nat_hostmap(softn, NULL, fin->fin_src, fin->fin_dst,
				     in, (u_32_t)dport);
		if (hm != NULL) {
			in.s_addr = ntohl(hm->hm_ndstip.s_addr);
			np = hm->hm_ipnat;
			ni->nai_np = np;
			move = 0;
			ipf_nat_hostmapdel(softc, &hm);
		}
	}

	/*
	 * Otherwise, it's an inbound packet. Most likely, we don't
	 * want to rewrite source ports and source addresses. Instead,
	 * we want to rewrite to a fixed internal address and fixed
	 * internal port.
	 */
	if (np->in_flags & IPN_SPLIT) {
		in.s_addr = np->in_dnip;
		inb.s_addr = htonl(in.s_addr);

		if ((np->in_flags & (IPN_ROUNDR|IPN_STICKY)) == IPN_STICKY) {
			hm = ipf_nat_hostmap(softn, NULL, fin->fin_src,
					     fin->fin_dst, inb, (u_32_t)dport);
			if (hm != NULL) {
				in.s_addr = hm->hm_ndstip.s_addr;
				move = 0;
			}
		}

		if (hm == NULL || hm->hm_ref == 1) {
			if (np->in_ndstaddr == htonl(in.s_addr)) {
				np->in_dnip = ntohl(np->in_ndstmsk);
				move = 0;
			} else {
				np->in_dnip = ntohl(np->in_ndstaddr);
			}
		}
		if (hm != NULL)
			ipf_nat_hostmapdel(softc, &hm);

	} else if ((np->in_ndstaddr == 0) && (np->in_ndstmsk == 0xffffffff)) {
		i6addr_t in6;

		/*
		 * 0/32 - use the interface's IP address.
		 */
		if (ipf_ifpaddr(softc, 4, FRI_NORMAL, fin->fin_ifp,
			       &in6, NULL) == -1) {
			NBUMPSIDEX(0, ns_new_ifpaddr, ns_new_ifpaddr_2);
			DT3(ns_new_ifpaddr_2, fr_info_t *, fin, nat_t *, nat, natinfo_t, ni);
			return -1;
		}
		in.s_addr = ntohl(in6.in4.s_addr);

	} else if ((np->in_ndstaddr == 0) && (np->in_ndstmsk== 0)) {
		/*
		 * 0/0 - use the original destination address/port.
		 */
		in.s_addr = ntohl(fin->fin_daddr);

	} else if (np->in_redir == NAT_BIMAP &&
		   np->in_ndstmsk == np->in_odstmsk) {
		/*
		 * map the address block in a 1:1 fashion
		 */
		in.s_addr = np->in_ndstaddr;
		in.s_addr |= fin->fin_daddr & ~np->in_ndstmsk;
		in.s_addr = ntohl(in.s_addr);
	} else {
		in.s_addr = ntohl(np->in_ndstaddr);
	}

	if ((np->in_dpnext == 0) || ((flags & NAT_NOTRULEPORT) != 0))
		nport = dport;
	else {
		/*
		 * Whilst not optimized for the case where
		 * pmin == pmax, the gain is not significant.
		 */
		if (((np->in_flags & IPN_FIXEDDPORT) == 0) &&
		    (np->in_odport != np->in_dtop)) {
			nport = ntohs(dport) - np->in_odport + np->in_dpmax;
			nport = htons(nport);
		} else {
			nport = htons(np->in_dpnext);
			np->in_dpnext++;
			if (np->in_dpnext > np->in_dpmax)
				np->in_dpnext = np->in_dpmin;
		}
	}

	/*
	 * When the redirect-to address is set to 0.0.0.0, just
	 * assume a blank `forwarding' of the packet.  We don't
	 * setup any translation for this either.
	 */
	if (in.s_addr == 0) {
		if (nport == dport) {
			NBUMPSIDED(0, ns_xlate_null);
			return -1;
		}
		in.s_addr = ntohl(fin->fin_daddr);
	}

	/*
	 * Check to see if this redirect mapping already exists and if
	 * it does, return "failure" (allowing it to be created will just
	 * cause one or both of these "connections" to stop working.)
	 */
	inb.s_addr = htonl(in.s_addr);
	sp = fin->fin_data[0];
	dp = fin->fin_data[1];
	fin->fin_data[1] = fin->fin_data[0];
	fin->fin_data[0] = ntohs(nport);
	natl = ipf_nat_outlookup(fin, flags & ~(SI_WILDP|NAT_SEARCH),
			     (u_int)fin->fin_p, inb, fin->fin_src);
	fin->fin_data[0] = sp;
	fin->fin_data[1] = dp;
	if (natl != NULL) {
		DT2(ns_new_xlate_exists, fr_info_t *, fin, nat_t *, natl);
		NBUMPSIDE(0, ns_xlate_exists);
		return -1;
	}

	inb.s_addr = htonl(in.s_addr);
	nat->nat_ndstaddr = htonl(in.s_addr);
	nat->nat_odstip = fin->fin_dst;
	nat->nat_nsrcip = fin->fin_src;
	nat->nat_osrcip = fin->fin_src;
	if ((nat->nat_hm == NULL) && ((np->in_flags & IPN_STICKY) != 0))
		nat->nat_hm = ipf_nat_hostmap(softn, np, fin->fin_src,
					      fin->fin_dst, inb, (u_32_t)dport);

	if (flags & IPN_TCPUDP) {
		nat->nat_odport = dport;
		nat->nat_ndport = nport;
		nat->nat_osport = sport;
		nat->nat_nsport = sport;
		((tcphdr_t *)fin->fin_dp)->th_dport = nport;
	} else if (flags & IPN_ICMPQUERY) {
		nat->nat_oicmpid = fin->fin_data[1];
		((icmphdr_t *)fin->fin_dp)->icmp_id = nport;
		nat->nat_nicmpid = nport;
	}

	return move;
}

/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_add                                                 */
/* Returns:     nat_t* - NULL == failure to create new NAT structure,       */
/*                       else pointer to new NAT structure                  */
/* Parameters:  fin(I)       - pointer to packet information                */
/*              np(I)        - pointer to NAT rule                          */
/*              natsave(I)   - pointer to where to store NAT struct pointer */
/*              flags(I)     - flags describing the current packet          */
/*              direction(I) - direction of packet (in/out)                 */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* Attempts to create a new NAT entry.  Does not actually change the packet */
/* in any way.                                                              */
/*                                                                          */
/* This fucntion is in three main parts: (1) deal with creating a new NAT   */
/* structure for a "MAP" rule (outgoing NAT translation); (2) deal with     */
/* creating a new NAT structure for a "RDR" rule (incoming NAT translation) */
/* and (3) building that structure and putting it into the NAT table(s).    */
/*                                                                          */
/* NOTE: natsave should NOT be used top point back to an ipstate_t struct   */
/*       as it can result in memory being corrupted.                        */
/* ------------------------------------------------------------------------ */
nat_t *
ipf_nat_add(fin, np, natsave, flags, direction)
	fr_info_t *fin;
	ipnat_t *np;
	nat_t **natsave;
	u_int flags;
	int direction;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	hostmap_t *hm = NULL;
	nat_t *nat, *natl;
	natstat_t *nsp;
	u_int nflags;
	natinfo_t ni;
	int move;

	nsp = &softn->ipf_nat_stats;

	if ((nsp->ns_active * 100 / softn->ipf_nat_table_max) >
	    softn->ipf_nat_table_wm_high) {
		softn->ipf_nat_doflush = 1;
	}

	if (nsp->ns_active >= softn->ipf_nat_table_max) {
		NBUMPSIDED(fin->fin_out, ns_table_max);
		DT2(ns_table_max, nat_stat_t *, nsp, ipf_nat_softc_t *, softn);
		return NULL;
	}

	move = 1;
	nflags = np->in_flags & flags;
	nflags &= NAT_FROMRULE;

	ni.nai_np = np;
	ni.nai_dport = 0;
	ni.nai_sport = 0;

	/* Give me a new nat */
	KMALLOC(nat, nat_t *);
	if (nat == NULL) {
		DT(ns_memfail);
		NBUMPSIDED(fin->fin_out, ns_memfail);
		/*
		 * Try to automatically tune the max # of entries in the
		 * table allowed to be less than what will cause kmem_alloc()
		 * to fail and try to eliminate panics due to out of memory
		 * conditions arising.
		 */
		if ((softn->ipf_nat_table_max > softn->ipf_nat_table_sz) &&
		    (nsp->ns_active > 100)) {
			softn->ipf_nat_table_max = nsp->ns_active - 100;
			printf("table_max reduced to %d\n",
				softn->ipf_nat_table_max);
		}
		return NULL;
	}

	if (flags & IPN_ICMPQUERY) {
		/*
		 * In the ICMP query NAT code, we translate the ICMP id fields
		 * to make them unique. This is indepedent of the ICMP type
		 * (e.g. in the unlikely event that a host sends an echo and
		 * an tstamp request with the same id, both packets will have
		 * their ip address/id field changed in the same way).
		 */
		/* The icmp_id field is used by the sender to identify the
		 * process making the icmp request. (the receiver justs
		 * copies it back in its response). So, it closely matches
		 * the concept of source port. We overlay sport, so we can
		 * maximally reuse the existing code.
		 */
		ni.nai_sport = fin->fin_data[1];
		ni.nai_dport = 0;
	}

	bzero((char *)nat, sizeof(*nat));
	nat->nat_flags = flags;
	nat->nat_redir = np->in_redir;
	nat->nat_dir = direction;
	nat->nat_pr[0] = fin->fin_p;
	nat->nat_pr[1] = fin->fin_p;

	/*
	 * Search the current table for a match and create a new mapping
	 * if there is none found.
	 */
	if (np->in_redir & NAT_DIVERTUDP) {
		move = ipf_nat_newdivert(fin, nat, &ni);

	} else if (np->in_redir & NAT_REWRITE) {
		move = ipf_nat_newrewrite(fin, nat, &ni);

	} else if (direction == NAT_OUTBOUND) {
		/*
		 * We can now arrange to call this for the same connection
		 * because ipf_nat_new doesn't protect the code path into
		 * this function.
		 */
		natl = ipf_nat_outlookup(fin, nflags, (u_int)fin->fin_p,
				     fin->fin_src, fin->fin_dst);
		if (natl != NULL) {
			KFREE(nat);
			nat = natl;
			goto done;
		}

		move = ipf_nat_newmap(fin, nat, &ni);
	} else {
		/*
		 * NAT_INBOUND is used for redirects rules
		 */
		natl = ipf_nat_inlookup(fin, nflags, (u_int)fin->fin_p,
					fin->fin_src, fin->fin_dst);
		if (natl != NULL) {
			KFREE(nat);
			nat = natl;
			goto done;
		}

		move = ipf_nat_newrdr(fin, nat, &ni);
	}
	if (move == -1)
		goto badnat;

	np = ni.nai_np;

	nat->nat_mssclamp = np->in_mssclamp;
	nat->nat_me = natsave;
	nat->nat_fr = fin->fin_fr;
	nat->nat_rev = fin->fin_rev;
	nat->nat_ptr = np;
	nat->nat_dlocal = np->in_dlocal;

	if ((np->in_apr != NULL) && ((nat->nat_flags & NAT_SLAVE) == 0)) {
		if (ipf_proxy_new(fin, nat) == -1) {
			NBUMPSIDED(fin->fin_out, ns_appr_fail);
			DT3(ns_appr_fail, fr_info_t *, fin, nat_t *, nat, ipnat_t *, np);
			goto badnat;
		}
	}

	nat->nat_ifps[0] = np->in_ifps[0];
	if (np->in_ifps[0] != NULL) {
		COPYIFNAME(np->in_v[0], np->in_ifps[0], nat->nat_ifnames[0]);
	}

	nat->nat_ifps[1] = np->in_ifps[1];
	if (np->in_ifps[1] != NULL) {
		COPYIFNAME(np->in_v[1], np->in_ifps[1], nat->nat_ifnames[1]);
	}

	if (ipf_nat_finalise(fin, nat) == -1) {
		goto badnat;
	}

	np->in_use++;

	if ((move == 1) && (np->in_flags & IPN_ROUNDR)) {
		if ((np->in_redir & (NAT_REDIRECT|NAT_MAP)) == NAT_REDIRECT) {
			ipf_nat_delrdr(softn, np);
			ipf_nat_addrdr(softn, np);
		} else if ((np->in_redir & (NAT_REDIRECT|NAT_MAP)) == NAT_MAP) {
			ipf_nat_delmap(softn, np);
			ipf_nat_addmap(softn, np);
		}
	}

	if (flags & SI_WILDP)
		nsp->ns_wilds++;
	nsp->ns_proto[nat->nat_pr[0]]++;

	goto done;
badnat:
	DT3(ns_badnatnew, fr_info_t *, fin, nat_t *, nat, ipnat_t *, np);
	NBUMPSIDE(fin->fin_out, ns_badnatnew);
	if ((hm = nat->nat_hm) != NULL)
		ipf_nat_hostmapdel(softc, &hm);
	KFREE(nat);
	nat = NULL;
done:
	if (nat != NULL && np != NULL)
		np->in_hits++;
	if (natsave != NULL)
		*natsave = nat;
	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_finalise                                            */
/* Returns:     int - 0 == sucess, -1 == failure                            */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to NAT entry                               */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* This is the tail end of constructing a new NAT entry and is the same     */
/* for both IPv4 and IPv6.                                                  */
/* ------------------------------------------------------------------------ */
/*ARGSUSED*/
static int
ipf_nat_finalise(fin, nat)
	fr_info_t *fin;
	nat_t *nat;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_32_t sum1, sum2, sumd;
	frentry_t *fr;
	u_32_t flags;
#if SOLARIS && defined(_KERNEL) && defined(ICK_M_CTL_MAGIC)
	qpktinfo_t *qpi = fin->fin_qpi;
#endif

	flags = nat->nat_flags;

	switch (nat->nat_pr[0])
	{
	case IPPROTO_ICMP :
		sum1 = LONG_SUM(ntohs(nat->nat_oicmpid));
		sum2 = LONG_SUM(ntohs(nat->nat_nicmpid));
		CALC_SUMD(sum1, sum2, sumd);
		nat->nat_sumd[0] = (sumd & 0xffff) + (sumd >> 16);

		break;

	default :
		sum1 = LONG_SUM(ntohl(nat->nat_osrcaddr) + \
				ntohs(nat->nat_osport));
		sum2 = LONG_SUM(ntohl(nat->nat_nsrcaddr) + \
				ntohs(nat->nat_nsport));
		CALC_SUMD(sum1, sum2, sumd);
		nat->nat_sumd[0] = (sumd & 0xffff) + (sumd >> 16);

		sum1 = LONG_SUM(ntohl(nat->nat_odstaddr) + \
				ntohs(nat->nat_odport));
		sum2 = LONG_SUM(ntohl(nat->nat_ndstaddr) + \
				ntohs(nat->nat_ndport));
		CALC_SUMD(sum1, sum2, sumd);
		nat->nat_sumd[0] += (sumd & 0xffff) + (sumd >> 16);
		break;
	}

	/*
	 * Compute the partial checksum, just in case.
	 * This is only ever placed into outbound packets so care needs
	 * to be taken over which pair of addresses are used.
	 */
	if (nat->nat_dir == NAT_OUTBOUND) {
		sum1 = LONG_SUM(ntohl(nat->nat_nsrcaddr));
		sum1 += LONG_SUM(ntohl(nat->nat_ndstaddr));
	} else {
		sum1 = LONG_SUM(ntohl(nat->nat_osrcaddr));
		sum1 += LONG_SUM(ntohl(nat->nat_odstaddr));
	}
	sum1 += nat->nat_pr[1];
	nat->nat_sumd[1] = (sum1 & 0xffff) + (sum1 >> 16);

	sum1 = LONG_SUM(ntohl(nat->nat_osrcaddr));
	sum2 = LONG_SUM(ntohl(nat->nat_nsrcaddr));
	CALC_SUMD(sum1, sum2, sumd);
	nat->nat_ipsumd = (sumd & 0xffff) + (sumd >> 16);

	sum1 = LONG_SUM(ntohl(nat->nat_odstaddr));
	sum2 = LONG_SUM(ntohl(nat->nat_ndstaddr));
	CALC_SUMD(sum1, sum2, sumd);
	nat->nat_ipsumd += (sumd & 0xffff) + (sumd >> 16);

	nat->nat_v[0] = 4;
	nat->nat_v[1] = 4;

	if ((nat->nat_ifps[0] != NULL) && (nat->nat_ifps[0] != (void *)-1)) {
		nat->nat_mtu[0] = GETIFMTU_4(nat->nat_ifps[0]);
	}

	if ((nat->nat_ifps[1] != NULL) && (nat->nat_ifps[1] != (void *)-1)) {
		nat->nat_mtu[1] = GETIFMTU_4(nat->nat_ifps[1]);
	}

	if ((nat->nat_flags & SI_CLONE) == 0)
		nat->nat_sync = ipf_sync_new(softc, SMC_NAT, fin, nat);

	if (ipf_nat_insert(softc, softn, nat) == 0) {
		if (softn->ipf_nat_logging)
			ipf_nat_log(softc, softn, nat, NL_NEW);
		fr = nat->nat_fr;
		if (fr != NULL) {
			MUTEX_ENTER(&fr->fr_lock);
			fr->fr_ref++;
			MUTEX_EXIT(&fr->fr_lock);
		}
		return 0;
	}

	NBUMPSIDED(fin->fin_out, ns_unfinalised);
	DT2(ns_unfinalised, fr_info_t *, fin, nat_t *, nat);
	/*
	 * nat_insert failed, so cleanup time...
	 */
	if (nat->nat_sync != NULL)
		ipf_sync_del_nat(softc->ipf_sync_soft, nat->nat_sync);
	return -1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_insert                                              */
/* Returns:     int - 0 == sucess, -1 == failure                            */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              nat(I) - pointer to NAT structure                           */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* Insert a NAT entry into the hash tables for searching and add it to the  */
/* list of active NAT entries.  Adjust global counters when complete.       */
/* ------------------------------------------------------------------------ */
int
ipf_nat_insert(softc, softn, nat)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	nat_t *nat;
{
	u_int hv0, hv1;
	u_int sp, dp;
	ipnat_t *in;

	/*
	 * Try and return an error as early as possible, so calculate the hash
	 * entry numbers first and then proceed.
	 */
	if ((nat->nat_flags & (SI_W_SPORT|SI_W_DPORT)) == 0) {
		if ((nat->nat_flags & IPN_TCPUDP) != 0) {
			sp = nat->nat_osport;
			dp = nat->nat_odport;
		} else if ((nat->nat_flags & IPN_ICMPQUERY) != 0) {
			sp = 0;
			dp = nat->nat_oicmpid;
		} else {
			sp = 0;
			dp = 0;
		}
		hv0 = NAT_HASH_FN(nat->nat_osrcaddr, sp, 0xffffffff);
		hv0 = NAT_HASH_FN(nat->nat_odstaddr, hv0 + dp, 0xffffffff);
		/*
		 * TRACE nat_osrcaddr, nat_osport, nat_odstaddr,
		 * nat_odport, hv0
		 */

		if ((nat->nat_flags & IPN_TCPUDP) != 0) {
			sp = nat->nat_nsport;
			dp = nat->nat_ndport;
		} else if ((nat->nat_flags & IPN_ICMPQUERY) != 0) {
			sp = 0;
			dp = nat->nat_nicmpid;
		} else {
			sp = 0;
			dp = 0;
		}
		hv1 = NAT_HASH_FN(nat->nat_nsrcaddr, sp, 0xffffffff);
		hv1 = NAT_HASH_FN(nat->nat_ndstaddr, hv1 + dp, 0xffffffff);
		/*
		 * TRACE nat_nsrcaddr, nat_nsport, nat_ndstaddr,
		 * nat_ndport, hv1
		 */
	} else {
		hv0 = NAT_HASH_FN(nat->nat_osrcaddr, 0, 0xffffffff);
		hv0 = NAT_HASH_FN(nat->nat_odstaddr, hv0, 0xffffffff);
		/* TRACE nat_osrcaddr, nat_odstaddr, hv0 */

		hv1 = NAT_HASH_FN(nat->nat_nsrcaddr, 0, 0xffffffff);
		hv1 = NAT_HASH_FN(nat->nat_ndstaddr, hv1, 0xffffffff);
		/* TRACE nat_nsrcaddr, nat_ndstaddr, hv1 */
	}

	nat->nat_hv[0] = hv0;
	nat->nat_hv[1] = hv1;

	MUTEX_INIT(&nat->nat_lock, "nat entry lock");

	in = nat->nat_ptr;
	nat->nat_ref = nat->nat_me ? 2 : 1;

	nat->nat_ifnames[0][LIFNAMSIZ - 1] = '\0';
	nat->nat_ifps[0] = ipf_resolvenic(softc, nat->nat_ifnames[0], 4);

	if (nat->nat_ifnames[1][0] != '\0') {
		nat->nat_ifnames[1][LIFNAMSIZ - 1] = '\0';
		nat->nat_ifps[1] = ipf_resolvenic(softc,
						  nat->nat_ifnames[1], 4);
	} else if (in->in_ifnames[1] != -1) {
		char *name;

		name = in->in_names + in->in_ifnames[1];
		if (name[1] != '\0' && name[0] != '-' && name[0] != '*') {
			(void) strncpy(nat->nat_ifnames[1],
				       nat->nat_ifnames[0], LIFNAMSIZ);
			nat->nat_ifnames[1][LIFNAMSIZ - 1] = '\0';
			nat->nat_ifps[1] = nat->nat_ifps[0];
		}
	}
	if ((nat->nat_ifps[0] != NULL) && (nat->nat_ifps[0] != (void *)-1)) {
		nat->nat_mtu[0] = GETIFMTU_4(nat->nat_ifps[0]);
	}
	if ((nat->nat_ifps[1] != NULL) && (nat->nat_ifps[1] != (void *)-1)) {
		nat->nat_mtu[1] = GETIFMTU_4(nat->nat_ifps[1]);
	}

	return ipf_nat_hashtab_add(softc, softn, nat);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_hashtab_add                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              nat(I) - pointer to NAT structure                           */
/*                                                                          */
/* Handle the insertion of a NAT entry into the table/list.                 */
/* ------------------------------------------------------------------------ */
int
ipf_nat_hashtab_add(softc, softn, nat)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	nat_t *nat;
{
	nat_t **natp;
	u_int hv0;
	u_int hv1;

	hv0 = nat->nat_hv[0] % softn->ipf_nat_table_sz;
	hv1 = nat->nat_hv[1] % softn->ipf_nat_table_sz;

	if (nat->nat_dir == NAT_INBOUND || nat->nat_dir == NAT_DIVERTIN) {
		u_int swap;

		swap = hv0;
		hv0 = hv1;
		hv1 = swap;
	}

	if (softn->ipf_nat_stats.ns_side[0].ns_bucketlen[hv0] >=
	    softn->ipf_nat_maxbucket) {
		DT1(ns_bucket_max_0, int,
		    softn->ipf_nat_stats.ns_side[0].ns_bucketlen[hv0]);
		NBUMPSIDE(0, ns_bucket_max);
		return -1;
	}

	if (softn->ipf_nat_stats.ns_side[1].ns_bucketlen[hv1] >=
	    softn->ipf_nat_maxbucket) {
		DT1(ns_bucket_max_1, int,
		    softn->ipf_nat_stats.ns_side[1].ns_bucketlen[hv1]);
		NBUMPSIDE(1, ns_bucket_max);
		return -1;
	}

	/*
	 * The ordering of operations in the list and hash table insertion
	 * is very important.  The last operation for each task should be
	 * to update the top of the list, after all the "nexts" have been
	 * done so that walking the list while it is being done does not
	 * find strange pointers.
	 *
	 * Global list of NAT instances
	 */
	nat->nat_next = softn->ipf_nat_instances;
	nat->nat_pnext = &softn->ipf_nat_instances;
	if (softn->ipf_nat_instances)
		softn->ipf_nat_instances->nat_pnext = &nat->nat_next;
	softn->ipf_nat_instances = nat;

	/*
	 * Inbound hash table.
	 */
	natp = &softn->ipf_nat_table[0][hv0];
	nat->nat_phnext[0] = natp;
	nat->nat_hnext[0] = *natp;
	if (*natp) {
		(*natp)->nat_phnext[0] = &nat->nat_hnext[0];
	} else {
		NBUMPSIDE(0, ns_inuse);
	}
	*natp = nat;
	NBUMPSIDE(0, ns_bucketlen[hv0]);

	/*
	 * Outbound hash table.
	 */
	natp = &softn->ipf_nat_table[1][hv1];
	nat->nat_phnext[1] = natp;
	nat->nat_hnext[1] = *natp;
	if (*natp)
		(*natp)->nat_phnext[1] = &nat->nat_hnext[1];
	else {
		NBUMPSIDE(1, ns_inuse);
	}
	*natp = nat;
	NBUMPSIDE(1, ns_bucketlen[hv1]);

	ipf_nat_setqueue(softc, softn, nat);

	if (nat->nat_dir & NAT_OUTBOUND) {
		NBUMPSIDE(1, ns_added);
	} else {
		NBUMPSIDE(0, ns_added);
	}
	softn->ipf_nat_stats.ns_active++;
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_icmperrorlookup                                     */
/* Returns:     nat_t* - point to matching NAT structure                    */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              dir(I) - direction of packet (in/out)                       */
/*                                                                          */
/* Check if the ICMP error message is related to an existing TCP, UDP or    */
/* ICMP query nat entry.  It is assumed that the packet is already of the   */
/* the required length.                                                     */
/* ------------------------------------------------------------------------ */
nat_t *
ipf_nat_icmperrorlookup(fin, dir)
	fr_info_t *fin;
	int dir;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	int flags = 0, type, minlen;
	icmphdr_t *icmp, *orgicmp;
	nat_stat_side_t *nside;
	tcphdr_t *tcp = NULL;
	u_short data[2];
	nat_t *nat;
	ip_t *oip;
	u_int p;

	icmp = fin->fin_dp;
	type = icmp->icmp_type;
	nside = &softn->ipf_nat_stats.ns_side[fin->fin_out];
	/*
	 * Does it at least have the return (basic) IP header ?
	 * Only a basic IP header (no options) should be with an ICMP error
	 * header.  Also, if it's not an error type, then return.
	 */
	if ((fin->fin_hlen != sizeof(ip_t)) || !(fin->fin_flx & FI_ICMPERR)) {
		ATOMIC_INCL(nside->ns_icmp_basic);
		return NULL;
	}

	/*
	 * Check packet size
	 */
	oip = (ip_t *)((char *)fin->fin_dp + 8);
	minlen = IP_HL(oip) << 2;
	if ((minlen < sizeof(ip_t)) ||
	    (fin->fin_plen < ICMPERR_IPICMPHLEN + minlen)) {
		ATOMIC_INCL(nside->ns_icmp_size);
		return NULL;
	}

	/*
	 * Is the buffer big enough for all of it ?  It's the size of the IP
	 * header claimed in the encapsulated part which is of concern.  It
	 * may be too big to be in this buffer but not so big that it's
	 * outside the ICMP packet, leading to TCP deref's causing problems.
	 * This is possible because we don't know how big oip_hl is when we
	 * do the pullup early in ipf_check() and thus can't gaurantee it is
	 * all here now.
	 */
#ifdef  ipf_nat_KERNEL
	{
	mb_t *m;

	m = fin->fin_m;
# if defined(MENTAT)
	if ((char *)oip + fin->fin_dlen - ICMPERR_ICMPHLEN >
	    (char *)m->b_wptr) {
		ATOMIC_INCL(nside->ns_icmp_mbuf);
		return NULL;
	}
# else
	if ((char *)oip + fin->fin_dlen - ICMPERR_ICMPHLEN >
	    (char *)fin->fin_ip + M_LEN(m)) {
		ATOMIC_INCL(nside->ns_icmp_mbuf);
		return NULL;
	}
# endif
	}
#endif

	if (fin->fin_daddr != oip->ip_src.s_addr) {
		ATOMIC_INCL(nside->ns_icmp_address);
		return NULL;
	}

	p = oip->ip_p;
	if (p == IPPROTO_TCP)
		flags = IPN_TCP;
	else if (p == IPPROTO_UDP)
		flags = IPN_UDP;
	else if (p == IPPROTO_ICMP) {
		orgicmp = (icmphdr_t *)((char *)oip + (IP_HL(oip) << 2));

		/* see if this is related to an ICMP query */
		if (ipf_nat_icmpquerytype(orgicmp->icmp_type)) {
			data[0] = fin->fin_data[0];
			data[1] = fin->fin_data[1];
			fin->fin_data[0] = 0;
			fin->fin_data[1] = orgicmp->icmp_id;

			flags = IPN_ICMPERR|IPN_ICMPQUERY;
			/*
			 * NOTE : dir refers to the direction of the original
			 *        ip packet. By definition the icmp error
			 *        message flows in the opposite direction.
			 */
			if (dir == NAT_INBOUND)
				nat = ipf_nat_inlookup(fin, flags, p,
						       oip->ip_dst,
						       oip->ip_src);
			else
				nat = ipf_nat_outlookup(fin, flags, p,
							oip->ip_dst,
							oip->ip_src);
			fin->fin_data[0] = data[0];
			fin->fin_data[1] = data[1];
			return nat;
		}
	}

	if (flags & IPN_TCPUDP) {
		minlen += 8;		/* + 64bits of data to get ports */
		/* TRACE (fin,minlen) */
		if (fin->fin_plen < ICMPERR_IPICMPHLEN + minlen) {
			ATOMIC_INCL(nside->ns_icmp_short);
			return NULL;
		}

		data[0] = fin->fin_data[0];
		data[1] = fin->fin_data[1];
		tcp = (tcphdr_t *)((char *)oip + (IP_HL(oip) << 2));
		fin->fin_data[0] = ntohs(tcp->th_dport);
		fin->fin_data[1] = ntohs(tcp->th_sport);

		if (dir == NAT_INBOUND) {
			nat = ipf_nat_inlookup(fin, flags, p, oip->ip_dst,
					       oip->ip_src);
		} else {
			nat = ipf_nat_outlookup(fin, flags, p, oip->ip_dst,
					    oip->ip_src);
		}
		fin->fin_data[0] = data[0];
		fin->fin_data[1] = data[1];
		return nat;
	}
	if (dir == NAT_INBOUND)
		nat = ipf_nat_inlookup(fin, 0, p, oip->ip_dst, oip->ip_src);
	else
		nat = ipf_nat_outlookup(fin, 0, p, oip->ip_dst, oip->ip_src);

	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_icmperror                                           */
/* Returns:     nat_t* - point to matching NAT structure                    */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              nflags(I) - NAT flags for this packet                       */
/*              dir(I)    - direction of packet (in/out)                    */
/*                                                                          */
/* Fix up an ICMP packet which is an error message for an existing NAT      */
/* session.  This will correct both packet header data and checksums.       */
/*                                                                          */
/* This should *ONLY* be used for incoming ICMP error packets to make sure  */
/* a NAT'd ICMP packet gets correctly recognised.                           */
/* ------------------------------------------------------------------------ */
nat_t *
ipf_nat_icmperror(fin, nflags, dir)
	fr_info_t *fin;
	u_int *nflags;
	int dir;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_32_t sum1, sum2, sumd, sumd2;
	struct in_addr a1, a2, a3, a4;
	int flags, dlen, odst;
	icmphdr_t *icmp;
	u_short *csump;
	tcphdr_t *tcp;
	nat_t *nat;
	ip_t *oip;
	void *dp;

	if ((fin->fin_flx & (FI_SHORT|FI_FRAGBODY))) {
		NBUMPSIDED(fin->fin_out, ns_icmp_short);
		return NULL;
	}

	/*
	 * ipf_nat_icmperrorlookup() will return NULL for `defective' packets.
	 */
	if ((fin->fin_v != 4) || !(nat = ipf_nat_icmperrorlookup(fin, dir))) {
		NBUMPSIDED(fin->fin_out, ns_icmp_notfound);
		return NULL;
	}

	tcp = NULL;
	csump = NULL;
	flags = 0;
	sumd2 = 0;
	*nflags = IPN_ICMPERR;
	icmp = fin->fin_dp;
	oip = (ip_t *)&icmp->icmp_ip;
	dp = (((char *)oip) + (IP_HL(oip) << 2));
	if (oip->ip_p == IPPROTO_TCP) {
		tcp = (tcphdr_t *)dp;
		csump = (u_short *)&tcp->th_sum;
		flags = IPN_TCP;
	} else if (oip->ip_p == IPPROTO_UDP) {
		udphdr_t *udp;

		udp = (udphdr_t *)dp;
		tcp = (tcphdr_t *)dp;
		csump = (u_short *)&udp->uh_sum;
		flags = IPN_UDP;
	} else if (oip->ip_p == IPPROTO_ICMP)
		flags = IPN_ICMPQUERY;
	dlen = fin->fin_plen - ((char *)dp - (char *)fin->fin_ip);

	/*
	 * Need to adjust ICMP header to include the real IP#'s and
	 * port #'s.  Only apply a checksum change relative to the
	 * IP address change as it will be modified again in ipf_nat_checkout
	 * for both address and port.  Two checksum changes are
	 * necessary for the two header address changes.  Be careful
	 * to only modify the checksum once for the port # and twice
	 * for the IP#.
	 */

	/*
	 * Step 1
	 * Fix the IP addresses in the offending IP packet. You also need
	 * to adjust the IP header checksum of that offending IP packet.
	 *
	 * Normally, you would expect that the ICMP checksum of the
	 * ICMP error message needs to be adjusted as well for the
	 * IP address change in oip.
	 * However, this is a NOP, because the ICMP checksum is
	 * calculated over the complete ICMP packet, which includes the
	 * changed oip IP addresses and oip->ip_sum. However, these
	 * two changes cancel each other out (if the delta for
	 * the IP address is x, then the delta for ip_sum is minus x),
	 * so no change in the icmp_cksum is necessary.
	 *
	 * Inbound ICMP
	 * ------------
	 * MAP rule, SRC=a,DST=b -> SRC=c,DST=b
	 * - response to outgoing packet (a,b)=>(c,b) (OIP_SRC=c,OIP_DST=b)
	 * - OIP_SRC(c)=nat_newsrcip,          OIP_DST(b)=nat_newdstip
	 *=> OIP_SRC(c)=nat_oldsrcip,          OIP_DST(b)=nat_olddstip
	 *
	 * RDR rule, SRC=a,DST=b -> SRC=a,DST=c
	 * - response to outgoing packet (c,a)=>(b,a) (OIP_SRC=b,OIP_DST=a)
	 * - OIP_SRC(b)=nat_olddstip,          OIP_DST(a)=nat_oldsrcip
	 *=> OIP_SRC(b)=nat_newdstip,          OIP_DST(a)=nat_newsrcip
	 *
	 * REWRITE out rule, SRC=a,DST=b -> SRC=c,DST=d
	 * - response to outgoing packet (a,b)=>(c,d) (OIP_SRC=c,OIP_DST=d)
	 * - OIP_SRC(c)=nat_newsrcip,          OIP_DST(d)=nat_newdstip
	 *=> OIP_SRC(c)=nat_oldsrcip,          OIP_DST(d)=nat_olddstip
	 *
	 * REWRITE in rule, SRC=a,DST=b -> SRC=c,DST=d
	 * - response to outgoing packet (d,c)=>(b,a) (OIP_SRC=b,OIP_DST=a)
	 * - OIP_SRC(b)=nat_olddstip,          OIP_DST(a)=nat_oldsrcip
	 *=> OIP_SRC(b)=nat_newdstip,          OIP_DST(a)=nat_newsrcip
	 *
	 * Outbound ICMP
	 * -------------
	 * MAP rule, SRC=a,DST=b -> SRC=c,DST=b
	 * - response to incoming packet (b,c)=>(b,a) (OIP_SRC=b,OIP_DST=a)
	 * - OIP_SRC(b)=nat_olddstip,          OIP_DST(a)=nat_oldsrcip
	 *=> OIP_SRC(b)=nat_newdstip,          OIP_DST(a)=nat_newsrcip
	 *
	 * RDR rule, SRC=a,DST=b -> SRC=a,DST=c
	 * - response to incoming packet (a,b)=>(a,c) (OIP_SRC=a,OIP_DST=c)
	 * - OIP_SRC(a)=nat_newsrcip,          OIP_DST(c)=nat_newdstip
	 *=> OIP_SRC(a)=nat_oldsrcip,          OIP_DST(c)=nat_olddstip
	 *
	 * REWRITE out rule, SRC=a,DST=b -> SRC=c,DST=d
	 * - response to incoming packet (d,c)=>(b,a) (OIP_SRC=c,OIP_DST=d)
	 * - OIP_SRC(c)=nat_olddstip,          OIP_DST(d)=nat_oldsrcip
	 *=> OIP_SRC(b)=nat_newdstip,          OIP_DST(a)=nat_newsrcip
	 *
	 * REWRITE in rule, SRC=a,DST=b -> SRC=c,DST=d
	 * - response to incoming packet (a,b)=>(c,d) (OIP_SRC=b,OIP_DST=a)
	 * - OIP_SRC(b)=nat_newsrcip,          OIP_DST(a)=nat_newdstip
	 *=> OIP_SRC(a)=nat_oldsrcip,          OIP_DST(c)=nat_olddstip
	 */

	if (((fin->fin_out == 0) && ((nat->nat_redir & NAT_MAP) != 0)) ||
	    ((fin->fin_out == 1) && ((nat->nat_redir & NAT_REDIRECT) != 0))) {
		a1.s_addr = ntohl(nat->nat_osrcaddr);
		a4.s_addr = ntohl(oip->ip_src.s_addr);
		a3.s_addr = ntohl(nat->nat_odstaddr);
		a2.s_addr = ntohl(oip->ip_dst.s_addr);
		oip->ip_src.s_addr = htonl(a1.s_addr);
		oip->ip_dst.s_addr = htonl(a3.s_addr);
		odst = 1;
	} else {
		a1.s_addr = ntohl(nat->nat_ndstaddr);
		a2.s_addr = ntohl(oip->ip_dst.s_addr);
		a3.s_addr = ntohl(nat->nat_nsrcaddr);
		a4.s_addr = ntohl(oip->ip_src.s_addr);
		oip->ip_dst.s_addr = htonl(a3.s_addr);
		oip->ip_src.s_addr = htonl(a1.s_addr);
		odst = 0;
	}
	sum1 = 0;
	sum2 = 0;
	sumd = 0;
	CALC_SUMD(a2.s_addr, a3.s_addr, sum1);
	CALC_SUMD(a4.s_addr, a1.s_addr, sum2);
	sumd = sum2 + sum1;
	if (sumd != 0)
		ipf_fix_datacksum(&oip->ip_sum, sumd);

	sumd2 = sumd;
	sum1 = 0;
	sum2 = 0;

	/*
	 * Fix UDP pseudo header checksum to compensate for the
	 * IP address change.
	 */
	if (((flags & IPN_TCPUDP) != 0) && (dlen >= 4)) {
		u_32_t sum3, sum4, sumt;

		/*
		 * Step 2 :
		 * For offending TCP/UDP IP packets, translate the ports as
		 * well, based on the NAT specification. Of course such
		 * a change may be reflected in the ICMP checksum as well.
		 *
		 * Since the port fields are part of the TCP/UDP checksum
		 * of the offending IP packet, you need to adjust that checksum
		 * as well... except that the change in the port numbers should
		 * be offset by the checksum change.  However, the TCP/UDP
		 * checksum will also need to change if there has been an
		 * IP address change.
		 */
		if (odst == 1) {
			sum1 = ntohs(nat->nat_osport);
			sum4 = ntohs(tcp->th_sport);
			sum3 = ntohs(nat->nat_odport);
			sum2 = ntohs(tcp->th_dport);

			tcp->th_sport = htons(sum1);
			tcp->th_dport = htons(sum3);
		} else {
			sum1 = ntohs(nat->nat_ndport);
			sum2 = ntohs(tcp->th_dport);
			sum3 = ntohs(nat->nat_nsport);
			sum4 = ntohs(tcp->th_sport);

			tcp->th_dport = htons(sum3);
			tcp->th_sport = htons(sum1);
		}
		CALC_SUMD(sum4, sum1, sumt);
		sumd += sumt;
		CALC_SUMD(sum2, sum3, sumt);
		sumd += sumt;

		if (sumd != 0 || sumd2 != 0) {
			/*
			 * At this point, sumd is the delta to apply to the
			 * TCP/UDP header, given the changes in both the IP
			 * address and the ports and sumd2 is the delta to
			 * apply to the ICMP header, given the IP address
			 * change delta that may need to be applied to the
			 * TCP/UDP checksum instead.
			 *
			 * If we will both the IP and TCP/UDP checksums
			 * then the ICMP checksum changes by the address
			 * delta applied to the TCP/UDP checksum.  If we
			 * do not change the TCP/UDP checksum them we
			 * apply the delta in ports to the ICMP checksum.
			 */
			if (oip->ip_p == IPPROTO_UDP) {
				if ((dlen >= 8) && (*csump != 0)) {
					ipf_fix_datacksum(csump, sumd);
				} else {
					CALC_SUMD(sum1, sum4, sumd2);
					CALC_SUMD(sum3, sum2, sumt);
					sumd2 += sumt;
				}
			} else if (oip->ip_p == IPPROTO_TCP) {
				if (dlen >= 18) {
					ipf_fix_datacksum(csump, sumd);
				} else {
					CALC_SUMD(sum1, sum4, sumd2);
					CALC_SUMD(sum3, sum2, sumt);
					sumd2 += sumt;
				}
			}
			if (sumd2 != 0) {
				sumd2 = (sumd2 & 0xffff) + (sumd2 >> 16);
				sumd2 = (sumd2 & 0xffff) + (sumd2 >> 16);
				sumd2 = (sumd2 & 0xffff) + (sumd2 >> 16);
				ipf_fix_incksum(0, &icmp->icmp_cksum, sumd2, 0);
			}
		}
	} else if (((flags & IPN_ICMPQUERY) != 0) && (dlen >= 8)) {
		icmphdr_t *orgicmp;

		/*
		 * XXX - what if this is bogus hl and we go off the end ?
		 * In this case, ipf_nat_icmperrorlookup() will have
		 * returned NULL.
		 */
		orgicmp = (icmphdr_t *)dp;

		if (odst == 1) {
			if (orgicmp->icmp_id != nat->nat_osport) {

				/*
				 * Fix ICMP checksum (of the offening ICMP
				 * query packet) to compensate the change
				 * in the ICMP id of the offending ICMP
				 * packet.
				 *
				 * Since you modify orgicmp->icmp_id with
				 * a delta (say x) and you compensate that
				 * in origicmp->icmp_cksum with a delta
				 * minus x, you don't have to adjust the
				 * overall icmp->icmp_cksum
				 */
				sum1 = ntohs(orgicmp->icmp_id);
				sum2 = ntohs(nat->nat_oicmpid);
				CALC_SUMD(sum1, sum2, sumd);
				orgicmp->icmp_id = nat->nat_oicmpid;
				ipf_fix_datacksum(&orgicmp->icmp_cksum, sumd);
			}
		} /* nat_dir == NAT_INBOUND is impossible for icmp queries */
	}
	return nat;
}


/*
 *       MAP-IN    MAP-OUT   RDR-IN   RDR-OUT
 * osrc    X       == src    == src      X
 * odst    X       == dst    == dst      X
 * nsrc  == dst      X         X      == dst
 * ndst  == src      X         X      == src
 * MAP = NAT_OUTBOUND, RDR = NAT_INBOUND
 */
/*
 * NB: these lookups don't lock access to the list, it assumed that it has
 * already been done!
 */
/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_inlookup                                            */
/* Returns:     nat_t* - NULL == no match,                                  */
/*                       else pointer to matching NAT entry                 */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              flags(I)  - NAT flags for this packet                       */
/*              p(I)      - protocol for this packet                        */
/*              src(I)    - source IP address                               */
/*              mapdst(I) - destination IP address                          */
/*                                                                          */
/* Lookup a nat entry based on the mapped destination ip address/port and   */
/* real source address/port.  We use this lookup when receiving a packet,   */
/* we're looking for a table entry, based on the destination address.       */
/*                                                                          */
/* NOTE: THE PACKET BEING CHECKED (IF FOUND) HAS A MAPPING ALREADY.         */
/*                                                                          */
/* NOTE: IT IS ASSUMED THAT  IS ONLY HELD WITH A READ LOCK WHEN             */
/*       THIS FUNCTION IS CALLED WITH NAT_SEARCH SET IN nflags.             */
/*                                                                          */
/* flags   -> relevant are IPN_UDP/IPN_TCP/IPN_ICMPQUERY that indicate if   */
/*            the packet is of said protocol                                */
/* ------------------------------------------------------------------------ */
nat_t *
ipf_nat_inlookup(fin, flags, p, src, mapdst)
	fr_info_t *fin;
	u_int flags, p;
	struct in_addr src , mapdst;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_short sport, dport;
	grehdr_t *gre;
	ipnat_t *ipn;
	u_int sflags;
	nat_t *nat;
	int nflags;
	u_32_t dst;
	void *ifp;
	u_int hv, rhv;

	ifp = fin->fin_ifp;
	gre = NULL;
	dst = mapdst.s_addr;
	sflags = flags & NAT_TCPUDPICMP;

	switch (p)
	{
	case IPPROTO_TCP :
	case IPPROTO_UDP :
		sport = htons(fin->fin_data[0]);
		dport = htons(fin->fin_data[1]);
		break;
	case IPPROTO_ICMP :
		sport = 0;
		dport = fin->fin_data[1];
		break;
	default :
		sport = 0;
		dport = 0;
		break;
	}


	if ((flags & SI_WILDP) != 0)
		goto find_in_wild_ports;

	rhv = NAT_HASH_FN(dst, dport, 0xffffffff);
	rhv = NAT_HASH_FN(src.s_addr, rhv + sport, 0xffffffff);
	hv = rhv % softn->ipf_nat_table_sz;
	nat = softn->ipf_nat_table[1][hv];
	/* TRACE dst, dport, src, sport, hv, nat */

	for (; nat; nat = nat->nat_hnext[1]) {
		if (nat->nat_ifps[0] != NULL) {
			if ((ifp != NULL) && (ifp != nat->nat_ifps[0]))
				continue;
		}

		if (nat->nat_pr[0] != p)
			continue;

		switch (nat->nat_dir)
		{
		case NAT_INBOUND :
		case NAT_DIVERTIN :
			if (nat->nat_v[0] != 4)
				continue;
			if (nat->nat_osrcaddr != src.s_addr ||
			    nat->nat_odstaddr != dst)
				continue;
			if ((nat->nat_flags & IPN_TCPUDP) != 0) {
				if (nat->nat_osport != sport)
					continue;
				if (nat->nat_odport != dport)
					continue;

			} else if (p == IPPROTO_ICMP) {
				if (nat->nat_osport != dport) {
					continue;
				}
			}
			break;
		case NAT_DIVERTOUT :
			if (nat->nat_dlocal)
				continue;
		case NAT_OUTBOUND :
			if (nat->nat_v[1] != 4)
				continue;
			if (nat->nat_dlocal)
				continue;
			if (nat->nat_dlocal)
				continue;
			if (nat->nat_ndstaddr != src.s_addr ||
			    nat->nat_nsrcaddr != dst)
				continue;
			if ((nat->nat_flags & IPN_TCPUDP) != 0) {
				if (nat->nat_ndport != sport)
					continue;
				if (nat->nat_nsport != dport)
					continue;

			} else if (p == IPPROTO_ICMP) {
				if (nat->nat_osport != dport) {
					continue;
				}
			}
			break;
		}


		if ((nat->nat_flags & IPN_TCPUDP) != 0) {
			ipn = nat->nat_ptr;
			if ((ipn != NULL) && (nat->nat_aps != NULL))
				if (ipf_proxy_match(fin, nat) != 0)
					continue;
		}
		if ((nat->nat_ifps[0] == NULL) && (ifp != NULL)) {
			nat->nat_ifps[0] = ifp;
			nat->nat_mtu[0] = GETIFMTU_4(ifp);
		}
		return nat;
	}

	/*
	 * So if we didn't find it but there are wildcard members in the hash
	 * table, go back and look for them.  We do this search and update here
	 * because it is modifying the NAT table and we want to do this only
	 * for the first packet that matches.  The exception, of course, is
	 * for "dummy" (FI_IGNORE) lookups.
	 */
find_in_wild_ports:
	if (!(flags & NAT_TCPUDP) || !(flags & NAT_SEARCH)) {
		NBUMPSIDEX(0, ns_lookup_miss, ns_lookup_miss_0);
		return NULL;
	}
	if (softn->ipf_nat_stats.ns_wilds == 0 || (fin->fin_flx & FI_NOWILD)) {
		NBUMPSIDEX(0, ns_lookup_nowild, ns_lookup_nowild_0);
		return NULL;
	}

	RWLOCK_EXIT(&softc->ipf_nat);

	hv = NAT_HASH_FN(dst, 0, 0xffffffff);
	hv = NAT_HASH_FN(src.s_addr, hv, softn->ipf_nat_table_sz);
	WRITE_ENTER(&softc->ipf_nat);

	nat = softn->ipf_nat_table[1][hv];
	/* TRACE dst, src, hv, nat */
	for (; nat; nat = nat->nat_hnext[1]) {
		if (nat->nat_ifps[0] != NULL) {
			if ((ifp != NULL) && (ifp != nat->nat_ifps[0]))
				continue;
		}

		if (nat->nat_pr[0] != fin->fin_p)
			continue;

		switch (nat->nat_dir & (NAT_INBOUND|NAT_OUTBOUND))
		{
		case NAT_INBOUND :
			if (nat->nat_v[0] != 4)
				continue;
			if (nat->nat_osrcaddr != src.s_addr ||
			    nat->nat_odstaddr != dst)
				continue;
			break;
		case NAT_OUTBOUND :
			if (nat->nat_v[1] != 4)
				continue;
			if (nat->nat_ndstaddr != src.s_addr ||
			    nat->nat_nsrcaddr != dst)
				continue;
			break;
		}

		nflags = nat->nat_flags;
		if (!(nflags & (NAT_TCPUDP|SI_WILDP)))
			continue;

		if (ipf_nat_wildok(nat, (int)sport, (int)dport, nflags,
				   NAT_INBOUND) == 1) {
			if ((fin->fin_flx & FI_IGNORE) != 0)
				break;
			if ((nflags & SI_CLONE) != 0) {
				nat = ipf_nat_clone(fin, nat);
				if (nat == NULL)
					break;
			} else {
				MUTEX_ENTER(&softn->ipf_nat_new);
				softn->ipf_nat_stats.ns_wilds--;
				MUTEX_EXIT(&softn->ipf_nat_new);
			}

			if (nat->nat_dir == NAT_INBOUND) {
				if (nat->nat_osport == 0) {
					nat->nat_osport = sport;
					nat->nat_nsport = sport;
				}
				if (nat->nat_odport == 0) {
					nat->nat_odport = dport;
					nat->nat_ndport = dport;
				}
			} else if (nat->nat_dir == NAT_OUTBOUND) {
				if (nat->nat_osport == 0) {
					nat->nat_osport = dport;
					nat->nat_nsport = dport;
				}
				if (nat->nat_odport == 0) {
					nat->nat_odport = sport;
					nat->nat_ndport = sport;
				}
			}
			if ((nat->nat_ifps[0] == NULL) && (ifp != NULL)) {
				nat->nat_ifps[0] = ifp;
				nat->nat_mtu[0] = GETIFMTU_4(ifp);
			}
			nat->nat_flags &= ~(SI_W_DPORT|SI_W_SPORT);
			ipf_nat_tabmove(softn, nat);
			break;
		}
	}

	MUTEX_DOWNGRADE(&softc->ipf_nat);

	if (nat == NULL) {
		NBUMPSIDE(0, ns_lookup_miss);
	}
	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_tabmove                                             */
/* Returns:     Nil                                                         */
/* Parameters:  softn(I) - pointer to NAT context structure                 */
/*              nat(I)   - pointer to NAT structure                         */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* This function is only called for TCP/UDP NAT table entries where the     */
/* original was placed in the table without hashing on the ports and we now */
/* want to include hashing on port numbers.                                 */
/* ------------------------------------------------------------------------ */
static void
ipf_nat_tabmove(softn, nat)
	ipf_nat_softc_t *softn;
	nat_t *nat;
{
	u_int hv0, hv1, rhv0, rhv1;
	natstat_t *nsp;
	nat_t **natp;

	if (nat->nat_flags & SI_CLONE)
		return;

	nsp = &softn->ipf_nat_stats;
	/*
	 * Remove the NAT entry from the old location
	 */
	if (nat->nat_hnext[0])
		nat->nat_hnext[0]->nat_phnext[0] = nat->nat_phnext[0];
	*nat->nat_phnext[0] = nat->nat_hnext[0];
	nsp->ns_side[0].ns_bucketlen[nat->nat_hv[0] %
				     softn->ipf_nat_table_sz]--;

	if (nat->nat_hnext[1])
		nat->nat_hnext[1]->nat_phnext[1] = nat->nat_phnext[1];
	*nat->nat_phnext[1] = nat->nat_hnext[1];
	nsp->ns_side[1].ns_bucketlen[nat->nat_hv[1] %
				     softn->ipf_nat_table_sz]--;

	/*
	 * Add into the NAT table in the new position
	 */
	rhv0 = NAT_HASH_FN(nat->nat_osrcaddr, nat->nat_osport, 0xffffffff);
	rhv0 = NAT_HASH_FN(nat->nat_odstaddr, rhv0 + nat->nat_odport,
			   0xffffffff);
	rhv1 = NAT_HASH_FN(nat->nat_nsrcaddr, nat->nat_nsport, 0xffffffff);
	rhv1 = NAT_HASH_FN(nat->nat_ndstaddr, rhv1 + nat->nat_ndport,
			   0xffffffff);

	hv0 = rhv0 % softn->ipf_nat_table_sz;
	hv1 = rhv1 % softn->ipf_nat_table_sz;

	if (nat->nat_dir == NAT_INBOUND || nat->nat_dir == NAT_DIVERTIN) {
		u_int swap;

		swap = hv0;
		hv0 = hv1;
		hv1 = swap;
	}

	/* TRACE nat_osrcaddr, nat_osport, nat_odstaddr, nat_odport, hv0 */
	/* TRACE nat_nsrcaddr, nat_nsport, nat_ndstaddr, nat_ndport, hv1 */

	nat->nat_hv[0] = rhv0;
	natp = &softn->ipf_nat_table[0][hv0];
	if (*natp)
		(*natp)->nat_phnext[0] = &nat->nat_hnext[0];
	nat->nat_phnext[0] = natp;
	nat->nat_hnext[0] = *natp;
	*natp = nat;
	nsp->ns_side[0].ns_bucketlen[hv0]++;

	nat->nat_hv[1] = rhv1;
	natp = &softn->ipf_nat_table[1][hv1];
	if (*natp)
		(*natp)->nat_phnext[1] = &nat->nat_hnext[1];
	nat->nat_phnext[1] = natp;
	nat->nat_hnext[1] = *natp;
	*natp = nat;
	nsp->ns_side[1].ns_bucketlen[hv1]++;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_outlookup                                           */
/* Returns:     nat_t* - NULL == no match,                                  */
/*                       else pointer to matching NAT entry                 */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              flags(I) - NAT flags for this packet                        */
/*              p(I)     - protocol for this packet                         */
/*              src(I)   - source IP address                                */
/*              dst(I)   - destination IP address                           */
/*              rw(I)    - 1 == write lock on  held, 0 == read lock.        */
/*                                                                          */
/* Lookup a nat entry based on the source 'real' ip address/port and        */
/* destination address/port.  We use this lookup when sending a packet out, */
/* we're looking for a table entry, based on the source address.            */
/*                                                                          */
/* NOTE: THE PACKET BEING CHECKED (IF FOUND) HAS A MAPPING ALREADY.         */
/*                                                                          */
/* NOTE: IT IS ASSUMED THAT  IS ONLY HELD WITH A READ LOCK WHEN             */
/*       THIS FUNCTION IS CALLED WITH NAT_SEARCH SET IN nflags.             */
/*                                                                          */
/* flags   -> relevant are IPN_UDP/IPN_TCP/IPN_ICMPQUERY that indicate if   */
/*            the packet is of said protocol                                */
/* ------------------------------------------------------------------------ */
nat_t *
ipf_nat_outlookup(fin, flags, p, src, dst)
	fr_info_t *fin;
	u_int flags, p;
	struct in_addr src , dst;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_short sport, dport;
	u_int sflags;
	ipnat_t *ipn;
	nat_t *nat;
	void *ifp;
	u_int hv;

	ifp = fin->fin_ifp;
	sflags = flags & IPN_TCPUDPICMP;

	switch (p)
	{
	case IPPROTO_TCP :
	case IPPROTO_UDP :
		sport = htons(fin->fin_data[0]);
		dport = htons(fin->fin_data[1]);
		break;
	case IPPROTO_ICMP :
		sport = 0;
		dport = fin->fin_data[1];
		break;
	default :
		sport = 0;
		dport = 0;
		break;
	}

	if ((flags & SI_WILDP) != 0)
		goto find_out_wild_ports;

	hv = NAT_HASH_FN(src.s_addr, sport, 0xffffffff);
	hv = NAT_HASH_FN(dst.s_addr, hv + dport, softn->ipf_nat_table_sz);
	nat = softn->ipf_nat_table[0][hv];

	/* TRACE src, sport, dst, dport, hv, nat */

	for (; nat; nat = nat->nat_hnext[0]) {
		if (nat->nat_ifps[1] != NULL) {
			if ((ifp != NULL) && (ifp != nat->nat_ifps[1]))
				continue;
		}

		if (nat->nat_pr[1] != p)
			continue;

		switch (nat->nat_dir)
		{
		case NAT_INBOUND :
		case NAT_DIVERTIN :
			if (nat->nat_v[1] != 4)
				continue;
			if (nat->nat_ndstaddr != src.s_addr ||
			    nat->nat_nsrcaddr != dst.s_addr)
				continue;

			if ((nat->nat_flags & IPN_TCPUDP) != 0) {
				if (nat->nat_ndport != sport)
					continue;
				if (nat->nat_nsport != dport)
					continue;

			} else if (p == IPPROTO_ICMP) {
				if (nat->nat_osport != dport) {
					continue;
				}
			}
			break;
		case NAT_OUTBOUND :
		case NAT_DIVERTOUT :
			if (nat->nat_v[0] != 4)
				continue;
			if (nat->nat_osrcaddr != src.s_addr ||
			    nat->nat_odstaddr != dst.s_addr)
				continue;

			if ((nat->nat_flags & IPN_TCPUDP) != 0) {
				if (nat->nat_odport != dport)
					continue;
				if (nat->nat_osport != sport)
					continue;

			} else if (p == IPPROTO_ICMP) {
				if (nat->nat_osport != dport) {
					continue;
				}
			}
			break;
		}

		ipn = nat->nat_ptr;
		if ((ipn != NULL) && (nat->nat_aps != NULL))
			if (ipf_proxy_match(fin, nat) != 0)
				continue;

		if ((nat->nat_ifps[1] == NULL) && (ifp != NULL)) {
			nat->nat_ifps[1] = ifp;
			nat->nat_mtu[1] = GETIFMTU_4(ifp);
		}
		return nat;
	}

	/*
	 * So if we didn't find it but there are wildcard members in the hash
	 * table, go back and look for them.  We do this search and update here
	 * because it is modifying the NAT table and we want to do this only
	 * for the first packet that matches.  The exception, of course, is
	 * for "dummy" (FI_IGNORE) lookups.
	 */
find_out_wild_ports:
	if (!(flags & NAT_TCPUDP) || !(flags & NAT_SEARCH)) {
		NBUMPSIDEX(1, ns_lookup_miss, ns_lookup_miss_1);
		return NULL;
	}
	if (softn->ipf_nat_stats.ns_wilds == 0 || (fin->fin_flx & FI_NOWILD)) {
		NBUMPSIDEX(1, ns_lookup_nowild, ns_lookup_nowild_1);
		return NULL;
	}

	RWLOCK_EXIT(&softc->ipf_nat);

	hv = NAT_HASH_FN(src.s_addr, 0, 0xffffffff);
	hv = NAT_HASH_FN(dst.s_addr, hv, softn->ipf_nat_table_sz);

	WRITE_ENTER(&softc->ipf_nat);

	nat = softn->ipf_nat_table[0][hv];
	for (; nat; nat = nat->nat_hnext[0]) {
		if (nat->nat_ifps[1] != NULL) {
			if ((ifp != NULL) && (ifp != nat->nat_ifps[1]))
				continue;
		}

		if (nat->nat_pr[1] != fin->fin_p)
			continue;

		switch (nat->nat_dir & (NAT_INBOUND|NAT_OUTBOUND))
		{
		case NAT_INBOUND :
			if (nat->nat_v[1] != 4)
				continue;
			if (nat->nat_ndstaddr != src.s_addr ||
			    nat->nat_nsrcaddr != dst.s_addr)
				continue;
			break;
		case NAT_OUTBOUND :
			if (nat->nat_v[0] != 4)
				continue;
			if (nat->nat_osrcaddr != src.s_addr ||
			    nat->nat_odstaddr != dst.s_addr)
				continue;
			break;
		}

		if (!(nat->nat_flags & (NAT_TCPUDP|SI_WILDP)))
			continue;

		if (ipf_nat_wildok(nat, (int)sport, (int)dport, nat->nat_flags,
				   NAT_OUTBOUND) == 1) {
			if ((fin->fin_flx & FI_IGNORE) != 0)
				break;
			if ((nat->nat_flags & SI_CLONE) != 0) {
				nat = ipf_nat_clone(fin, nat);
				if (nat == NULL)
					break;
			} else {
				MUTEX_ENTER(&softn->ipf_nat_new);
				softn->ipf_nat_stats.ns_wilds--;
				MUTEX_EXIT(&softn->ipf_nat_new);
			}

			if (nat->nat_dir == NAT_OUTBOUND) {
				if (nat->nat_osport == 0) {
					nat->nat_osport = sport;
					nat->nat_nsport = sport;
				}
				if (nat->nat_odport == 0) {
					nat->nat_odport = dport;
					nat->nat_ndport = dport;
				}
			} else if (nat->nat_dir == NAT_INBOUND) {
				if (nat->nat_osport == 0) {
					nat->nat_osport = dport;
					nat->nat_nsport = dport;
				}
				if (nat->nat_odport == 0) {
					nat->nat_odport = sport;
					nat->nat_ndport = sport;
				}
			}
			if ((nat->nat_ifps[1] == NULL) && (ifp != NULL)) {
				nat->nat_ifps[1] = ifp;
				nat->nat_mtu[1] = GETIFMTU_4(ifp);
			}
			nat->nat_flags &= ~(SI_W_DPORT|SI_W_SPORT);
			ipf_nat_tabmove(softn, nat);
			break;
		}
	}

	MUTEX_DOWNGRADE(&softc->ipf_nat);

	if (nat == NULL) {
		NBUMPSIDE(1, ns_lookup_miss);
	}
	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_lookupredir                                         */
/* Returns:     nat_t* - NULL == no match,                                  */
/*                       else pointer to matching NAT entry                 */
/* Parameters:  np(I) - pointer to description of packet to find NAT table  */
/*                      entry for.                                          */
/*                                                                          */
/* Lookup the NAT tables to search for a matching redirect                  */
/* The contents of natlookup_t should imitate those found in a packet that  */
/* would be translated - ie a packet coming in for RDR or going out for MAP.*/
/* We can do the lookup in one of two ways, imitating an inbound or         */
/* outbound  packet.  By default we assume outbound, unless IPN_IN is set.  */
/* For IN, the fields are set as follows:                                   */
/*     nl_real* = source information                                        */
/*     nl_out* = destination information (translated)                       */
/* For an out packet, the fields are set like this:                         */
/*     nl_in* = source information (untranslated)                           */
/*     nl_out* = destination information (translated)                       */
/* ------------------------------------------------------------------------ */
nat_t *
ipf_nat_lookupredir(np)
	natlookup_t *np;
{
	fr_info_t fi;
	nat_t *nat;

	bzero((char *)&fi, sizeof(fi));
	if (np->nl_flags & IPN_IN) {
		fi.fin_data[0] = ntohs(np->nl_realport);
		fi.fin_data[1] = ntohs(np->nl_outport);
	} else {
		fi.fin_data[0] = ntohs(np->nl_inport);
		fi.fin_data[1] = ntohs(np->nl_outport);
	}
	if (np->nl_flags & IPN_TCP)
		fi.fin_p = IPPROTO_TCP;
	else if (np->nl_flags & IPN_UDP)
		fi.fin_p = IPPROTO_UDP;
	else if (np->nl_flags & (IPN_ICMPERR|IPN_ICMPQUERY))
		fi.fin_p = IPPROTO_ICMP;

	/*
	 * We can do two sorts of lookups:
	 * - IPN_IN: we have the `real' and `out' address, look for `in'.
	 * - default: we have the `in' and `out' address, look for `real'.
	 */
	if (np->nl_flags & IPN_IN) {
		if ((nat = ipf_nat_inlookup(&fi, np->nl_flags, fi.fin_p,
					    np->nl_realip, np->nl_outip))) {
			np->nl_inip = nat->nat_odstip;
			np->nl_inport = nat->nat_odport;
		}
	} else {
		/*
		 * If nl_inip is non null, this is a lookup based on the real
		 * ip address. Else, we use the fake.
		 */
		if ((nat = ipf_nat_outlookup(&fi, np->nl_flags, fi.fin_p,
					 np->nl_inip, np->nl_outip))) {

			if ((np->nl_flags & IPN_FINDFORWARD) != 0) {
				fr_info_t fin;
				bzero((char *)&fin, sizeof(fin));
				fin.fin_p = nat->nat_pr[0];
				fin.fin_data[0] = ntohs(nat->nat_ndport);
				fin.fin_data[1] = ntohs(nat->nat_nsport);
				if (ipf_nat_inlookup(&fin, np->nl_flags,
						     fin.fin_p, nat->nat_ndstip,
						     nat->nat_nsrcip) != NULL) {
					np->nl_flags &= ~IPN_FINDFORWARD;
				}
			}

			np->nl_realip = nat->nat_odstip;
			np->nl_realport = nat->nat_odport;
		}
 	}

	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_match                                               */
/* Returns:     int - 0 == no match, 1 == match                             */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              np(I)    - pointer to NAT rule                              */
/*                                                                          */
/* Pull the matching of a packet against a NAT rule out of that complex     */
/* loop inside ipf_nat_checkin() and lay it out properly in its own function. */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_match(fin, np)
	fr_info_t *fin;
	ipnat_t *np;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	frtuc_t *ft;
	int match;

	match = 0;
	switch (np->in_osrcatype)
	{
	case FRI_NORMAL :
		match = ((fin->fin_saddr & np->in_osrcmsk) != np->in_osrcaddr);
		break;
	case FRI_LOOKUP :
		match = (*np->in_osrcfunc)(softc, np->in_osrcptr,
					   4, &fin->fin_saddr, fin->fin_plen);
		break;
	}
	match ^= ((np->in_flags & IPN_NOTSRC) != 0);
	if (match)
		return 0;

	match = 0;
	switch (np->in_odstatype)
	{
	case FRI_NORMAL :
		match = ((fin->fin_daddr & np->in_odstmsk) != np->in_odstaddr);
		break;
	case FRI_LOOKUP :
		match = (*np->in_odstfunc)(softc, np->in_odstptr,
					   4, &fin->fin_daddr, fin->fin_plen);
		break;
	}

	match ^= ((np->in_flags & IPN_NOTDST) != 0);
	if (match)
		return 0;

	ft = &np->in_tuc;
	if (!(fin->fin_flx & FI_TCPUDP) ||
	    (fin->fin_flx & (FI_SHORT|FI_FRAGBODY))) {
		if (ft->ftu_scmp || ft->ftu_dcmp)
			return 0;
		return 1;
	}

	return ipf_tcpudpchk(&fin->fin_fi, ft);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_update                                              */
/* Returns:     Nil                                                         */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to NAT structure                           */
/*                                                                          */
/* Updates the lifetime of a NAT table entry for non-TCP packets.  Must be  */
/* called with fin_rev updated - i.e. after calling ipf_nat_proto().        */
/*                                                                          */
/* This *MUST* be called after ipf_nat_proto() as it expects fin_rev to     */
/* already be set.                                                          */
/* ------------------------------------------------------------------------ */
void
ipf_nat_update(fin, nat)
	fr_info_t *fin;
	nat_t *nat;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	ipftq_t *ifq, *ifq2;
	ipftqent_t *tqe;
	ipnat_t *np = nat->nat_ptr;

	tqe = &nat->nat_tqe;
	ifq = tqe->tqe_ifq;

	/*
	 * We allow over-riding of NAT timeouts from NAT rules, even for
	 * TCP, however, if it is TCP and there is no rule timeout set,
	 * then do not update the timeout here.
	 */
	if (np != NULL) {
		np->in_bytes[fin->fin_rev] += fin->fin_plen;
		ifq2 = np->in_tqehead[fin->fin_rev];
	} else {
		ifq2 = NULL;
	}

	if (nat->nat_pr[0] == IPPROTO_TCP && ifq2 == NULL) {
		(void) ipf_tcp_age(&nat->nat_tqe, fin, softn->ipf_nat_tcptq,
				   0, 2);
	} else {
		if (ifq2 == NULL) {
			if (nat->nat_pr[0] == IPPROTO_UDP)
				ifq2 = fin->fin_rev ? &softn->ipf_nat_udpacktq :
						      &softn->ipf_nat_udptq;
			else if (nat->nat_pr[0] == IPPROTO_ICMP ||
				 nat->nat_pr[0] == IPPROTO_ICMPV6)
				ifq2 = fin->fin_rev ? &softn->ipf_nat_icmpacktq:
						      &softn->ipf_nat_icmptq;
			else
				ifq2 = &softn->ipf_nat_iptq;
		}

		ipf_movequeue(softc->ipf_ticks, tqe, ifq, ifq2);
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_checkout                                            */
/* Returns:     int - -1 == packet failed NAT checks so block it,           */
/*                     0 == no packet translation occurred,                 */
/*                     1 == packet was successfully translated.             */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              passp(I) - pointer to filtering result flags                */
/*                                                                          */
/* Check to see if an outcoming packet should be changed.  ICMP packets are */
/* first checked to see if they match an existing entry (if an error),      */
/* otherwise a search of the current NAT table is made.  If neither results */
/* in a match then a search for a matching NAT rule is made.  Create a new  */
/* NAT entry if a we matched a NAT rule.  Lastly, actually change the       */
/* packet header(s) as required.                                            */
/* ------------------------------------------------------------------------ */
int
ipf_nat_checkout(fin, passp)
	fr_info_t *fin;
	u_32_t *passp;
{
	ipnat_t *np = NULL, *npnext;
	struct ifnet *ifp, *sifp;
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	icmphdr_t *icmp = NULL;
	tcphdr_t *tcp = NULL;
	int rval, natfailed;
	u_int nflags = 0;
	u_32_t ipa, iph;
	int natadd = 1;
	frentry_t *fr;
	nat_t *nat;

	if (fin->fin_v == 6) {
#ifdef USE_INET6
		return ipf_nat6_checkout(fin, passp);
#else
		return 0;
#endif
	}

	softc = fin->fin_main_soft;
	softn = softc->ipf_nat_soft;

	if (softn->ipf_nat_lock != 0)
		return 0;
	if (softn->ipf_nat_stats.ns_rules == 0 &&
	    softn->ipf_nat_instances == NULL)
		return 0;

	natfailed = 0;
	fr = fin->fin_fr;
	sifp = fin->fin_ifp;
	if (fr != NULL) {
		ifp = fr->fr_tifs[fin->fin_rev].fd_ptr;
		if ((ifp != NULL) && (ifp != (void *)-1))
			fin->fin_ifp = ifp;
	}
	ifp = fin->fin_ifp;

	if (!(fin->fin_flx & FI_SHORT) && (fin->fin_off == 0)) {
		switch (fin->fin_p)
		{
		case IPPROTO_TCP :
			nflags = IPN_TCP;
			break;
		case IPPROTO_UDP :
			nflags = IPN_UDP;
			break;
		case IPPROTO_ICMP :
			icmp = fin->fin_dp;

			/*
			 * This is an incoming packet, so the destination is
			 * the icmp_id and the source port equals 0
			 */
			if ((fin->fin_flx & FI_ICMPQUERY) != 0)
				nflags = IPN_ICMPQUERY;
			break;
		default :
			break;
		}

		if ((nflags & IPN_TCPUDP))
			tcp = fin->fin_dp;
	}

	ipa = fin->fin_saddr;

	READ_ENTER(&softc->ipf_nat);

	if ((fin->fin_p == IPPROTO_ICMP) && !(nflags & IPN_ICMPQUERY) &&
	    (nat = ipf_nat_icmperror(fin, &nflags, NAT_OUTBOUND)))
		/*EMPTY*/;
	else if ((fin->fin_flx & FI_FRAG) && (nat = ipf_frag_natknown(fin)))
		natadd = 0;
	else if ((nat = ipf_nat_outlookup(fin, nflags|NAT_SEARCH,
				      (u_int)fin->fin_p, fin->fin_src,
				      fin->fin_dst))) {
		nflags = nat->nat_flags;
	} else if (fin->fin_off == 0) {
		u_32_t hv, msk, nmsk = 0;

		/*
		 * If there is no current entry in the nat table for this IP#,
		 * create one for it (if there is a matching rule).
		 */
maskloop:
		msk = softn->ipf_nat_map_active_masks[nmsk];
		iph = ipa & msk;
		hv = NAT_HASH_FN(iph, 0, softn->ipf_nat_maprules_sz);
retry_roundrobin:
		for (np = softn->ipf_nat_map_rules[hv]; np; np = npnext) {
			npnext = np->in_mnext;
			if ((np->in_ifps[1] && (np->in_ifps[1] != ifp)))
				continue;
			if (np->in_v[0] != 4)
				continue;
			if (np->in_pr[1] && (np->in_pr[1] != fin->fin_p))
				continue;
			if ((np->in_flags & IPN_RF) &&
			    !(np->in_flags & nflags))
				continue;
			if (np->in_flags & IPN_FILTER) {
				switch (ipf_nat_match(fin, np))
				{
				case 0 :
					continue;
				case -1 :
					rval = -3;
					goto outmatchfail;
				case 1 :
				default :
					break;
				}
			} else if ((ipa & np->in_osrcmsk) != np->in_osrcaddr)
				continue;

			if ((fr != NULL) &&
			    !ipf_matchtag(&np->in_tag, &fr->fr_nattag))
				continue;

			if (np->in_plabel != -1) {
				if (((np->in_flags & IPN_FILTER) == 0) &&
				    (np->in_odport != fin->fin_data[1]))
					continue;
				if (ipf_proxy_ok(fin, tcp, np) == 0)
					continue;
			}

			if (np->in_flags & IPN_NO) {
				np->in_hits++;
				break;
			}
			MUTEX_ENTER(&softn->ipf_nat_new);
			/*
			 * If we've matched a round-robin rule but it has
			 * moved in the list since we got it, start over as
			 * this is now no longer correct.
			 */
			if (npnext != np->in_mnext) {
				if ((np->in_flags & IPN_ROUNDR) != 0) {
					MUTEX_EXIT(&softn->ipf_nat_new);
					goto retry_roundrobin;
				}
				npnext = np->in_mnext;
			}

			nat = ipf_nat_add(fin, np, NULL, nflags, NAT_OUTBOUND);
			MUTEX_EXIT(&softn->ipf_nat_new);
			if (nat != NULL) {
				natfailed = 0;
				break;
			}
			natfailed = -2;
		}
		if ((np == NULL) && (nmsk < softn->ipf_nat_map_max)) {
			nmsk++;
			goto maskloop;
		}
	}

	if (nat != NULL) {
		rval = ipf_nat_out(fin, nat, natadd, nflags);
		if (rval == 1) {
			MUTEX_ENTER(&nat->nat_lock);
			ipf_nat_update(fin, nat);
			nat->nat_bytes[1] += fin->fin_plen;
			nat->nat_pkts[1]++;
			fin->fin_pktnum = nat->nat_pkts[1];
			MUTEX_EXIT(&nat->nat_lock);
		}
	} else
		rval = natfailed;
outmatchfail:
	RWLOCK_EXIT(&softc->ipf_nat);

	switch (rval)
	{
	case -3 :
		/* ipf_nat_match() failure */
		/* FALLTHROUGH */
	case -2 :
		/* retry_roundrobin loop failure */
		/* FALLTHROUGH */
	case -1 :
		/* proxy failure detected by ipf_nat_out() */
		if (passp != NULL) {
			DT2(frb_natv4out, fr_info_t *, fin, int, rval);
			NBUMPSIDED(1, ns_drop);
			*passp = FR_BLOCK;
			fin->fin_reason = FRB_NATV4;
		}
		fin->fin_flx |= FI_BADNAT;
		NBUMPSIDED(1, ns_badnat);
		rval = -1;	/* We only return -1 on error. */
		break;
	case 0 :
		NBUMPSIDE(1, ns_ignored);
		break;
	case 1 :
		NBUMPSIDE(1, ns_translated);
		break;
	}
	fin->fin_ifp = sifp;
	return rval;
}

/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_out                                                 */
/* Returns:     int - -1 == packet failed NAT checks so block it,           */
/*                     1 == packet was successfully translated.             */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              nat(I)    - pointer to NAT structure                        */
/*              natadd(I) - flag indicating if it is safe to add frag cache */
/*              nflags(I) - NAT flags set for this packet                   */
/*                                                                          */
/* Translate a packet coming "out" on an interface.                         */
/* ------------------------------------------------------------------------ */
int
ipf_nat_out(fin, nat, natadd, nflags)
	fr_info_t *fin;
	nat_t *nat;
	int natadd;
	u_32_t nflags;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	icmphdr_t *icmp;
	tcphdr_t *tcp;
	ipnat_t *np;
	int skip;
	int i;

	tcp = NULL;
	icmp = NULL;
	np = nat->nat_ptr;

	if ((natadd != 0) && (fin->fin_flx & FI_FRAG) && (np != NULL))
		(void) ipf_frag_natnew(softc, fin, 0, nat);

	/*
	 * Fix up checksums, not by recalculating them, but
	 * simply computing adjustments.
	 * This is only done for STREAMS based IP implementations where the
	 * checksum has already been calculated by IP.  In all other cases,
	 * IPFilter is called before the checksum needs calculating so there
	 * is no call to modify whatever is in the header now.
	 */
	if (nflags == IPN_ICMPERR) {
		u_32_t s1, s2, sumd, msumd;

		s1 = LONG_SUM(ntohl(fin->fin_saddr));
		if (nat->nat_dir == NAT_OUTBOUND) {
			s2 = LONG_SUM(ntohl(nat->nat_nsrcaddr));
		} else {
			s2 = LONG_SUM(ntohl(nat->nat_odstaddr));
		}
		CALC_SUMD(s1, s2, sumd);
		msumd = sumd;

		s1 = LONG_SUM(ntohl(fin->fin_daddr));
		if (nat->nat_dir == NAT_OUTBOUND) {
			s2 = LONG_SUM(ntohl(nat->nat_ndstaddr));
		} else {
			s2 = LONG_SUM(ntohl(nat->nat_osrcaddr));
		}
		CALC_SUMD(s1, s2, sumd);
		msumd += sumd;

		ipf_fix_outcksum(0, &fin->fin_ip->ip_sum, msumd, 0);
	}
#if !defined(_KERNEL) || defined(MENTAT) || defined(__sgi) || \
    defined(linux) || defined(BRIDGE_IPF) || defined(__FreeBSD__)
	else {
		/*
		 * Strictly speaking, this isn't necessary on BSD
		 * kernels because they do checksum calculation after
		 * this code has run BUT if ipfilter is being used
		 * to do NAT as a bridge, that code doesn't exist.
		 */
		switch (nat->nat_dir)
		{
		case NAT_OUTBOUND :
			ipf_fix_outcksum(fin->fin_cksum & FI_CK_L4PART,
					 &fin->fin_ip->ip_sum,
					 nat->nat_ipsumd, 0);
			break;

		case NAT_INBOUND :
			ipf_fix_incksum(fin->fin_cksum & FI_CK_L4PART,
					&fin->fin_ip->ip_sum,
					nat->nat_ipsumd, 0);
			break;

		default :
			break;
		}
	}
#endif

	/*
	 * Address assignment is after the checksum modification because
	 * we are using the address in the packet for determining the
	 * correct checksum offset (the ICMP error could be coming from
	 * anyone...)
	 */
	switch (nat->nat_dir)
	{
	case NAT_OUTBOUND :
		fin->fin_ip->ip_src = nat->nat_nsrcip;
		fin->fin_saddr = nat->nat_nsrcaddr;
		fin->fin_ip->ip_dst = nat->nat_ndstip;
		fin->fin_daddr = nat->nat_ndstaddr;
		break;

	case NAT_INBOUND :
		fin->fin_ip->ip_src = nat->nat_odstip;
		fin->fin_saddr = nat->nat_ndstaddr;
		fin->fin_ip->ip_dst = nat->nat_osrcip;
		fin->fin_daddr = nat->nat_nsrcaddr;
		break;

	case NAT_DIVERTIN :
	    {
		mb_t *m;

		skip = ipf_nat_decap(fin, nat);
		if (skip <= 0) {
			NBUMPSIDED(1, ns_decap_fail);
			return -1;
		}

		m = fin->fin_m;

#if defined(MENTAT) && defined(_KERNEL)
		m->b_rptr += skip;
#else
		m->m_data += skip;
		m->m_len -= skip;

# ifdef M_PKTHDR
		if (m->m_flags & M_PKTHDR)
			m->m_pkthdr.len -= skip;
# endif
#endif

		MUTEX_ENTER(&nat->nat_lock);
		ipf_nat_update(fin, nat);
		MUTEX_EXIT(&nat->nat_lock);
		fin->fin_flx |= FI_NATED;
		if (np != NULL && np->in_tag.ipt_num[0] != 0)
			fin->fin_nattag = &np->in_tag;
		return 1;
		/* NOTREACHED */
	    }

	case NAT_DIVERTOUT :
	    {
		u_32_t s1, s2, sumd;
		udphdr_t *uh;
		ip_t *ip;
		mb_t *m;

		m = M_DUP(np->in_divmp);
		if (m == NULL) {
			NBUMPSIDED(1, ns_divert_dup);
			return -1;
		}

		ip = MTOD(m, ip_t *);
		ip_fillid(ip);
		s2 = ntohs(ip->ip_id);

		s1 = ip->ip_len;
		ip->ip_len = ntohs(ip->ip_len);
		ip->ip_len += fin->fin_plen;
		ip->ip_len = htons(ip->ip_len);
		s2 += ntohs(ip->ip_len);
		CALC_SUMD(s1, s2, sumd);

		uh = (udphdr_t *)(ip + 1);
		uh->uh_ulen += fin->fin_plen;
		uh->uh_ulen = htons(uh->uh_ulen);
#if !defined(_KERNEL) || defined(MENTAT) || \
    defined(BRIDGE_IPF) || defined(__FreeBSD__)
		ipf_fix_outcksum(0, &ip->ip_sum, sumd, 0);
#endif

		PREP_MB_T(fin, m);

		fin->fin_src = ip->ip_src;
		fin->fin_dst = ip->ip_dst;
		fin->fin_ip = ip;
		fin->fin_plen += sizeof(ip_t) + 8;	/* UDP + IPv4 hdr */
		fin->fin_dlen += sizeof(ip_t) + 8;	/* UDP + IPv4 hdr */

		nflags &= ~IPN_TCPUDPICMP;

		break;
	    }

	default :
		break;
	}

	if (!(fin->fin_flx & FI_SHORT) && (fin->fin_off == 0)) {
		u_short *csump;

		if ((nat->nat_nsport != 0) && (nflags & IPN_TCPUDP)) {
			tcp = fin->fin_dp;

			switch (nat->nat_dir)
			{
			case NAT_OUTBOUND :
				tcp->th_sport = nat->nat_nsport;
				fin->fin_data[0] = ntohs(nat->nat_nsport);
				tcp->th_dport = nat->nat_ndport;
				fin->fin_data[1] = ntohs(nat->nat_ndport);
				break;

			case NAT_INBOUND :
				tcp->th_sport = nat->nat_odport;
				fin->fin_data[0] = ntohs(nat->nat_odport);
				tcp->th_dport = nat->nat_osport;
				fin->fin_data[1] = ntohs(nat->nat_osport);
				break;
			}
		}

		if ((nat->nat_nsport != 0) && (nflags & IPN_ICMPQUERY)) {
			icmp = fin->fin_dp;
			icmp->icmp_id = nat->nat_nicmpid;
		}

		csump = ipf_nat_proto(fin, nat, nflags);

		/*
		 * The above comments do not hold for layer 4 (or higher)
		 * checksums...
		 */
		if (csump != NULL) {
			if (nat->nat_dir == NAT_OUTBOUND)
				ipf_fix_outcksum(fin->fin_cksum, csump,
						 nat->nat_sumd[0],
						 nat->nat_sumd[1] +
						 fin->fin_dlen);
			else
				ipf_fix_incksum(fin->fin_cksum, csump,
						nat->nat_sumd[0],
						nat->nat_sumd[1] +
						fin->fin_dlen);
		}
	}

	ipf_sync_update(softc, SMC_NAT, fin, nat->nat_sync);
	/* ------------------------------------------------------------- */
	/* A few quick notes:                                            */
	/*      Following are test conditions prior to calling the       */
	/*      ipf_proxy_check routine.                                 */
	/*                                                               */
	/*      A NULL tcp indicates a non TCP/UDP packet.  When dealing */
	/*      with a redirect rule, we attempt to match the packet's   */
	/*      source port against in_dport, otherwise we'd compare the */
	/*      packet's destination.                                    */
	/* ------------------------------------------------------------- */
	if ((np != NULL) && (np->in_apr != NULL)) {
		i = ipf_proxy_check(fin, nat);
		if (i == 0) {
			i = 1;
		} else if (i == -1) {
			NBUMPSIDED(1, ns_ipf_proxy_fail);
		}
	} else {
		i = 1;
	}
	fin->fin_flx |= FI_NATED;
	return i;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_checkin                                             */
/* Returns:     int - -1 == packet failed NAT checks so block it,           */
/*                     0 == no packet translation occurred,                 */
/*                     1 == packet was successfully translated.             */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              passp(I) - pointer to filtering result flags                */
/*                                                                          */
/* Check to see if an incoming packet should be changed.  ICMP packets are  */
/* first checked to see if they match an existing entry (if an error),      */
/* otherwise a search of the current NAT table is made.  If neither results */
/* in a match then a search for a matching NAT rule is made.  Create a new  */
/* NAT entry if a we matched a NAT rule.  Lastly, actually change the       */
/* packet header(s) as required.                                            */
/* ------------------------------------------------------------------------ */
int
ipf_nat_checkin(fin, passp)
	fr_info_t *fin;
	u_32_t *passp;
{
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	u_int nflags, natadd;
	ipnat_t *np, *npnext;
	int rval, natfailed;
	struct ifnet *ifp;
	struct in_addr in;
	icmphdr_t *icmp;
	tcphdr_t *tcp;
	u_short dport;
	nat_t *nat;
	u_32_t iph;

	softc = fin->fin_main_soft;
	softn = softc->ipf_nat_soft;

	if (softn->ipf_nat_lock != 0)
		return 0;
	if (softn->ipf_nat_stats.ns_rules == 0 &&
	    softn->ipf_nat_instances == NULL)
		return 0;

	tcp = NULL;
	icmp = NULL;
	dport = 0;
	natadd = 1;
	nflags = 0;
	natfailed = 0;
	ifp = fin->fin_ifp;

	if (!(fin->fin_flx & FI_SHORT) && (fin->fin_off == 0)) {
		switch (fin->fin_p)
		{
		case IPPROTO_TCP :
			nflags = IPN_TCP;
			break;
		case IPPROTO_UDP :
			nflags = IPN_UDP;
			break;
		case IPPROTO_ICMP :
			icmp = fin->fin_dp;

			/*
			 * This is an incoming packet, so the destination is
			 * the icmp_id and the source port equals 0
			 */
			if ((fin->fin_flx & FI_ICMPQUERY) != 0) {
				nflags = IPN_ICMPQUERY;
				dport = icmp->icmp_id;
			} break;
		default :
			break;
		}

		if ((nflags & IPN_TCPUDP)) {
			tcp = fin->fin_dp;
			dport = fin->fin_data[1];
		}
	}

	in = fin->fin_dst;

	READ_ENTER(&softc->ipf_nat);

	if ((fin->fin_p == IPPROTO_ICMP) && !(nflags & IPN_ICMPQUERY) &&
	    (nat = ipf_nat_icmperror(fin, &nflags, NAT_INBOUND)))
		/*EMPTY*/;
	else if ((fin->fin_flx & FI_FRAG) && (nat = ipf_frag_natknown(fin)))
		natadd = 0;
	else if ((nat = ipf_nat_inlookup(fin, nflags|NAT_SEARCH,
					 (u_int)fin->fin_p,
					 fin->fin_src, in))) {
		nflags = nat->nat_flags;
	} else if (fin->fin_off == 0) {
		u_32_t hv, msk, rmsk = 0;

		/*
		 * If there is no current entry in the nat table for this IP#,
		 * create one for it (if there is a matching rule).
		 */
maskloop:
		msk = softn->ipf_nat_rdr_active_masks[rmsk];
		iph = in.s_addr & msk;
		hv = NAT_HASH_FN(iph, 0, softn->ipf_nat_rdrrules_sz);
retry_roundrobin:
		/* TRACE (iph,msk,rmsk,hv,softn->ipf_nat_rdrrules_sz) */
		for (np = softn->ipf_nat_rdr_rules[hv]; np; np = npnext) {
			npnext = np->in_rnext;
			if (np->in_ifps[0] && (np->in_ifps[0] != ifp))
				continue;
			if (np->in_v[0] != 4)
				continue;
			if (np->in_pr[0] && (np->in_pr[0] != fin->fin_p))
				continue;
			if ((np->in_flags & IPN_RF) && !(np->in_flags & nflags))
				continue;
			if (np->in_flags & IPN_FILTER) {
				switch (ipf_nat_match(fin, np))
				{
				case 0 :
					continue;
				case -1 :
					rval = -3;
					goto inmatchfail;
				case 1 :
				default :
					break;
				}
			} else {
				if ((in.s_addr & np->in_odstmsk) !=
				    np->in_odstaddr)
					continue;
				if (np->in_odport &&
				    ((np->in_dtop < dport) ||
				     (dport < np->in_odport)))
					continue;
			}

			if (np->in_plabel != -1) {
				if (!ipf_proxy_ok(fin, tcp, np)) {
					continue;
				}
			}

			if (np->in_flags & IPN_NO) {
				np->in_hits++;
				break;
			}

			MUTEX_ENTER(&softn->ipf_nat_new);
			/*
			 * If we've matched a round-robin rule but it has
			 * moved in the list since we got it, start over as
			 * this is now no longer correct.
			 */
			if (npnext != np->in_rnext) {
				if ((np->in_flags & IPN_ROUNDR) != 0) {
					MUTEX_EXIT(&softn->ipf_nat_new);
					goto retry_roundrobin;
				}
				npnext = np->in_rnext;
			}

			nat = ipf_nat_add(fin, np, NULL, nflags, NAT_INBOUND);
			MUTEX_EXIT(&softn->ipf_nat_new);
			if (nat != NULL) {
				natfailed = 0;
				break;
			}
			natfailed = -2;
		}
		if ((np == NULL) && (rmsk < softn->ipf_nat_rdr_max)) {
			rmsk++;
			goto maskloop;
		}
	}

	if (nat != NULL) {
		rval = ipf_nat_in(fin, nat, natadd, nflags);
		if (rval == 1) {
			MUTEX_ENTER(&nat->nat_lock);
			ipf_nat_update(fin, nat);
			nat->nat_bytes[0] += fin->fin_plen;
			nat->nat_pkts[0]++;
			fin->fin_pktnum = nat->nat_pkts[0];
			MUTEX_EXIT(&nat->nat_lock);
		}
	} else
		rval = natfailed;
inmatchfail:
	RWLOCK_EXIT(&softc->ipf_nat);

	switch (rval)
	{
	case -3 :
		/* ipf_nat_match() failure */
		/* FALLTHROUGH */
	case -2 :
		/* retry_roundrobin loop failure */
		/* FALLTHROUGH */
	case -1 :
		/* proxy failure detected by ipf_nat_in() */
		if (passp != NULL) {
			DT2(frb_natv4in, fr_info_t *, fin, int, rval);
			NBUMPSIDED(0, ns_drop);
			*passp = FR_BLOCK;
			fin->fin_reason = FRB_NATV4;
		}
		fin->fin_flx |= FI_BADNAT;
		NBUMPSIDED(0, ns_badnat);
		rval = -1;	/* We only return -1 on error. */
		break;
	case 0 :
		NBUMPSIDE(0, ns_ignored);
		break;
	case 1 :
		NBUMPSIDE(0, ns_translated);
		break;
	}
	return rval;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_in                                                  */
/* Returns:     int - -1 == packet failed NAT checks so block it,           */
/*                     1 == packet was successfully translated.             */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              nat(I)    - pointer to NAT structure                        */
/*              natadd(I) - flag indicating if it is safe to add frag cache */
/*              nflags(I) - NAT flags set for this packet                   */
/* Locks Held:  ipf_nat(READ)                                               */
/*                                                                          */
/* Translate a packet coming "in" on an interface.                          */
/* ------------------------------------------------------------------------ */
int
ipf_nat_in(fin, nat, natadd, nflags)
	fr_info_t *fin;
	nat_t *nat;
	int natadd;
	u_32_t nflags;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_32_t sumd, ipsumd, sum1, sum2;
	icmphdr_t *icmp;
	tcphdr_t *tcp;
	ipnat_t *np;
	int skip;
	int i;

	tcp = NULL;
	np = nat->nat_ptr;
	fin->fin_fr = nat->nat_fr;

	if (np != NULL) {
		if ((natadd != 0) && (fin->fin_flx & FI_FRAG))
			(void) ipf_frag_natnew(softc, fin, 0, nat);

	/* ------------------------------------------------------------- */
	/* A few quick notes:                                            */
	/*      Following are test conditions prior to calling the       */
	/*      ipf_proxy_check routine.                                 */
	/*                                                               */
	/*      A NULL tcp indicates a non TCP/UDP packet.  When dealing */
	/*      with a map rule, we attempt to match the packet's        */
	/*      source port against in_dport, otherwise we'd compare the */
	/*      packet's destination.                                    */
	/* ------------------------------------------------------------- */
		if (np->in_apr != NULL) {
			i = ipf_proxy_check(fin, nat);
			if (i == -1) {
				NBUMPSIDED(0, ns_ipf_proxy_fail);
				return -1;
			}
		}
	}

	ipf_sync_update(softc, SMC_NAT, fin, nat->nat_sync);

	ipsumd = nat->nat_ipsumd;
	/*
	 * Fix up checksums, not by recalculating them, but
	 * simply computing adjustments.
	 * Why only do this for some platforms on inbound packets ?
	 * Because for those that it is done, IP processing is yet to happen
	 * and so the IPv4 header checksum has not yet been evaluated.
	 * Perhaps it should always be done for the benefit of things like
	 * fast forwarding (so that it doesn't need to be recomputed) but with
	 * header checksum offloading, perhaps it is a moot point.
	 */

	switch (nat->nat_dir)
	{
	case NAT_INBOUND :
		if ((fin->fin_flx & FI_ICMPERR) == 0) {
			fin->fin_ip->ip_src = nat->nat_nsrcip;
			fin->fin_saddr = nat->nat_nsrcaddr;
		} else {
			sum1 = nat->nat_osrcaddr;
			sum2 = nat->nat_nsrcaddr;
			CALC_SUMD(sum1, sum2, sumd);
			ipsumd -= sumd;
		}
		fin->fin_ip->ip_dst = nat->nat_ndstip;
		fin->fin_daddr = nat->nat_ndstaddr;
#if !defined(_KERNEL) || defined(MENTAT) || defined(__sgi) || \
     defined(__osf__) || defined(linux)
		ipf_fix_outcksum(0, &fin->fin_ip->ip_sum, ipsumd, 0);
#endif
		break;

	case NAT_OUTBOUND :
		if ((fin->fin_flx & FI_ICMPERR) == 0) {
			fin->fin_ip->ip_src = nat->nat_odstip;
			fin->fin_saddr = nat->nat_odstaddr;
		} else {
			sum1 = nat->nat_odstaddr;
			sum2 = nat->nat_ndstaddr;
			CALC_SUMD(sum1, sum2, sumd);
			ipsumd -= sumd;
		}
		fin->fin_ip->ip_dst = nat->nat_osrcip;
		fin->fin_daddr = nat->nat_osrcaddr;
#if !defined(_KERNEL) || defined(MENTAT)
		ipf_fix_incksum(0, &fin->fin_ip->ip_sum, ipsumd, 0);
#endif
		break;

	case NAT_DIVERTIN :
	    {
		udphdr_t *uh;
		ip_t *ip;
		mb_t *m;

		m = M_DUP(np->in_divmp);
		if (m == NULL) {
			NBUMPSIDED(0, ns_divert_dup);
			return -1;
		}

		ip = MTOD(m, ip_t *);
		ip_fillid(ip);
		sum1 = ntohs(ip->ip_len);
		ip->ip_len = ntohs(ip->ip_len);
		ip->ip_len += fin->fin_plen;
		ip->ip_len = htons(ip->ip_len);

		uh = (udphdr_t *)(ip + 1);
		uh->uh_ulen += fin->fin_plen;
		uh->uh_ulen = htons(uh->uh_ulen);

		sum2 = ntohs(ip->ip_id) + ntohs(ip->ip_len);
		sum2 += ntohs(ip->ip_off) & IP_DF;
		CALC_SUMD(sum1, sum2, sumd);

#if !defined(_KERNEL) || defined(MENTAT)
		ipf_fix_outcksum(0, &ip->ip_sum, sumd, 0);
#endif
		PREP_MB_T(fin, m);

		fin->fin_ip = ip;
		fin->fin_plen += sizeof(ip_t) + 8;	/* UDP + new IPv4 hdr */
		fin->fin_dlen += sizeof(ip_t) + 8;	/* UDP + old IPv4 hdr */

		nflags &= ~IPN_TCPUDPICMP;

		break;
	    }

	case NAT_DIVERTOUT :
	    {
		mb_t *m;

		skip = ipf_nat_decap(fin, nat);
		if (skip <= 0) {
			NBUMPSIDED(0, ns_decap_fail);
			return -1;
		}

		m = fin->fin_m;

#if defined(MENTAT) && defined(_KERNEL)
		m->b_rptr += skip;
#else
		m->m_data += skip;
		m->m_len -= skip;

# ifdef M_PKTHDR
		if (m->m_flags & M_PKTHDR)
			m->m_pkthdr.len -= skip;
# endif
#endif

		ipf_nat_update(fin, nat);
		nflags &= ~IPN_TCPUDPICMP;
		fin->fin_flx |= FI_NATED;
		if (np != NULL && np->in_tag.ipt_num[0] != 0)
			fin->fin_nattag = &np->in_tag;
		return 1;
		/* NOTREACHED */
	    }
	}
	if (nflags & IPN_TCPUDP)
		tcp = fin->fin_dp;

	if (!(fin->fin_flx & FI_SHORT) && (fin->fin_off == 0)) {
		u_short *csump;

		if ((nat->nat_odport != 0) && (nflags & IPN_TCPUDP)) {
			switch (nat->nat_dir)
			{
			case NAT_INBOUND :
				tcp->th_sport = nat->nat_nsport;
				fin->fin_data[0] = ntohs(nat->nat_nsport);
				tcp->th_dport = nat->nat_ndport;
				fin->fin_data[1] = ntohs(nat->nat_ndport);
				break;

			case NAT_OUTBOUND :
				tcp->th_sport = nat->nat_odport;
				fin->fin_data[0] = ntohs(nat->nat_odport);
				tcp->th_dport = nat->nat_osport;
				fin->fin_data[1] = ntohs(nat->nat_osport);
				break;
			}
		}


		if ((nat->nat_odport != 0) && (nflags & IPN_ICMPQUERY)) {
			icmp = fin->fin_dp;

			icmp->icmp_id = nat->nat_nicmpid;
		}

		csump = ipf_nat_proto(fin, nat, nflags);

		/*
		 * The above comments do not hold for layer 4 (or higher)
		 * checksums...
		 */
		if (csump != NULL) {
			if (nat->nat_dir == NAT_OUTBOUND)
				ipf_fix_incksum(0, csump, nat->nat_sumd[0], 0);
			else
				ipf_fix_outcksum(0, csump, nat->nat_sumd[0], 0);
		}
	}

	fin->fin_flx |= FI_NATED;
	if (np != NULL && np->in_tag.ipt_num[0] != 0)
		fin->fin_nattag = &np->in_tag;
	return 1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_proto                                               */
/* Returns:     u_short* - pointer to transport header checksum to update,  */
/*                         NULL if the transport protocol is not recognised */
/*                         as needing a checksum update.                    */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              nat(I)    - pointer to NAT structure                        */
/*              nflags(I) - NAT flags set for this packet                   */
/*                                                                          */
/* Return the pointer to the checksum field for each protocol so understood.*/
/* If support for making other changes to a protocol header is required,    */
/* that is not strictly 'address' translation, such as clamping the MSS in  */
/* TCP down to a specific value, then do it from here.                      */
/* ------------------------------------------------------------------------ */
u_short *
ipf_nat_proto(fin, nat, nflags)
	fr_info_t *fin;
	nat_t *nat;
	u_int nflags;
{
	icmphdr_t *icmp;
	u_short *csump;
	tcphdr_t *tcp;
	udphdr_t *udp;

	csump = NULL;
	if (fin->fin_out == 0) {
		fin->fin_rev = (nat->nat_dir & NAT_OUTBOUND);
	} else {
		fin->fin_rev = ((nat->nat_dir & NAT_OUTBOUND) == 0);
	}

	switch (fin->fin_p)
	{
	case IPPROTO_TCP :
		tcp = fin->fin_dp;

		if ((nflags & IPN_TCP) != 0)
			csump = &tcp->th_sum;

		/*
		 * Do a MSS CLAMPING on a SYN packet,
		 * only deal IPv4 for now.
		 */
		if ((nat->nat_mssclamp != 0) && (tcp->th_flags & TH_SYN) != 0)
			ipf_nat_mssclamp(tcp, nat->nat_mssclamp, fin, csump);

		break;

	case IPPROTO_UDP :
		udp = fin->fin_dp;

		if ((nflags & IPN_UDP) != 0) {
			if (udp->uh_sum != 0)
				csump = &udp->uh_sum;
		}
		break;

	case IPPROTO_ICMP :
		icmp = fin->fin_dp;

		if ((nflags & IPN_ICMPQUERY) != 0) {
			if (icmp->icmp_cksum != 0)
				csump = &icmp->icmp_cksum;
		}
		break;

#ifdef USE_INET6
	case IPPROTO_ICMPV6 :
	    {
		struct icmp6_hdr *icmp6 = (struct icmp6_hdr *)fin->fin_dp;

		icmp6 = fin->fin_dp;

		if ((nflags & IPN_ICMPQUERY) != 0) {
			if (icmp6->icmp6_cksum != 0)
				csump = &icmp6->icmp6_cksum;
		}
		break;
	    }
#endif
	}
	return csump;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_expire                                              */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Check all of the timeout queues for entries at the top which need to be  */
/* expired.                                                                 */
/* ------------------------------------------------------------------------ */
void
ipf_nat_expire(softc)
	ipf_main_softc_t *softc;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	ipftq_t *ifq, *ifqnext;
	ipftqent_t *tqe, *tqn;
	int i;
	SPL_INT(s);

	SPL_NET(s);
	WRITE_ENTER(&softc->ipf_nat);
	for (ifq = softn->ipf_nat_tcptq, i = 0; ifq != NULL;
	     ifq = ifq->ifq_next) {
		for (tqn = ifq->ifq_head; ((tqe = tqn) != NULL); i++) {
			if (tqe->tqe_die > softc->ipf_ticks)
				break;
			tqn = tqe->tqe_next;
			ipf_nat_delete(softc, tqe->tqe_parent, NL_EXPIRE);
		}
	}

	for (ifq = softn->ipf_nat_utqe; ifq != NULL; ifq = ifq->ifq_next) {
		for (tqn = ifq->ifq_head; ((tqe = tqn) != NULL); i++) {
			if (tqe->tqe_die > softc->ipf_ticks)
				break;
			tqn = tqe->tqe_next;
			ipf_nat_delete(softc, tqe->tqe_parent, NL_EXPIRE);
		}
	}

	for (ifq = softn->ipf_nat_utqe; ifq != NULL; ifq = ifqnext) {
		ifqnext = ifq->ifq_next;

		if (((ifq->ifq_flags & IFQF_DELETE) != 0) &&
		    (ifq->ifq_ref == 0)) {
			ipf_freetimeoutqueue(softc, ifq);
		}
	}

	if (softn->ipf_nat_doflush != 0) {
		ipf_nat_extraflush(softc, softn, 2);
		softn->ipf_nat_doflush = 0;
	}

	RWLOCK_EXIT(&softc->ipf_nat);
	SPL_X(s);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_sync                                                */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              ifp(I) - pointer to network interface                       */
/*                                                                          */
/* Walk through all of the currently active NAT sessions, looking for those */
/* which need to have their translated address updated.                     */
/* ------------------------------------------------------------------------ */
void
ipf_nat_sync(softc, ifp)
	ipf_main_softc_t *softc;
	void *ifp;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_32_t sum1, sum2, sumd;
	i6addr_t in;
	ipnat_t *n;
	nat_t *nat;
	void *ifp2;
	int idx;
	SPL_INT(s);

	if (softc->ipf_running <= 0)
		return;

	/*
	 * Change IP addresses for NAT sessions for any protocol except TCP
	 * since it will break the TCP connection anyway.  The only rules
	 * which will get changed are those which are "map ... -> 0/32",
	 * where the rule specifies the address is taken from the interface.
	 */
	SPL_NET(s);
	WRITE_ENTER(&softc->ipf_nat);

	if (softc->ipf_running <= 0) {
		RWLOCK_EXIT(&softc->ipf_nat);
		return;
	}

	for (nat = softn->ipf_nat_instances; nat; nat = nat->nat_next) {
		if ((nat->nat_flags & IPN_TCP) != 0)
			continue;

		n = nat->nat_ptr;
		if (n != NULL) {
			if (n->in_v[1] == 4) {
				if (n->in_redir & NAT_MAP) {
					if ((n->in_nsrcaddr != 0) ||
					    (n->in_nsrcmsk != 0xffffffff))
						continue;
				} else if (n->in_redir & NAT_REDIRECT) {
					if ((n->in_ndstaddr != 0) ||
					    (n->in_ndstmsk != 0xffffffff))
						continue;
				}
			}
#ifdef USE_INET6
			if (n->in_v[1] == 4) {
				if (n->in_redir & NAT_MAP) {
					if (!IP6_ISZERO(&n->in_nsrcaddr) ||
					    !IP6_ISONES(&n->in_nsrcmsk))
						continue;
				} else if (n->in_redir & NAT_REDIRECT) {
					if (!IP6_ISZERO(&n->in_ndstaddr) ||
					    !IP6_ISONES(&n->in_ndstmsk))
						continue;
				}
			}
#endif
		}

		if (((ifp == NULL) || (ifp == nat->nat_ifps[0]) ||
		     (ifp == nat->nat_ifps[1]))) {
			nat->nat_ifps[0] = GETIFP(nat->nat_ifnames[0],
						  nat->nat_v[0]);
			if ((nat->nat_ifps[0] != NULL) &&
			    (nat->nat_ifps[0] != (void *)-1)) {
				nat->nat_mtu[0] = GETIFMTU_4(nat->nat_ifps[0]);
			}
			if (nat->nat_ifnames[1][0] != '\0') {
				nat->nat_ifps[1] = GETIFP(nat->nat_ifnames[1],
							  nat->nat_v[1]);
			} else {
				nat->nat_ifps[1] = nat->nat_ifps[0];
			}
			if ((nat->nat_ifps[1] != NULL) &&
			    (nat->nat_ifps[1] != (void *)-1)) {
				nat->nat_mtu[1] = GETIFMTU_4(nat->nat_ifps[1]);
			}
			ifp2 = nat->nat_ifps[0];
			if (ifp2 == NULL)
				continue;

			/*
			 * Change the map-to address to be the same as the
			 * new one.
			 */
			sum1 = NATFSUM(nat, nat->nat_v[1], nat_nsrc6);
			if (ipf_ifpaddr(softc, nat->nat_v[0], FRI_NORMAL, ifp2,
				       &in, NULL) != -1) {
				if (nat->nat_v[0] == 4)
					nat->nat_nsrcip = in.in4;
			}
			sum2 = NATFSUM(nat, nat->nat_v[1], nat_nsrc6);

			if (sum1 == sum2)
				continue;
			/*
			 * Readjust the checksum adjustment to take into
			 * account the new IP#.
			 */
			CALC_SUMD(sum1, sum2, sumd);
			/* XXX - dont change for TCP when solaris does
			 * hardware checksumming.
			 */
			sumd += nat->nat_sumd[0];
			nat->nat_sumd[0] = (sumd & 0xffff) + (sumd >> 16);
			nat->nat_sumd[1] = nat->nat_sumd[0];
		}
	}

	for (n = softn->ipf_nat_list; (n != NULL); n = n->in_next) {
		char *base = n->in_names;

		if ((ifp == NULL) || (n->in_ifps[0] == ifp))
			n->in_ifps[0] = ipf_resolvenic(softc,
						       base + n->in_ifnames[0],
						       n->in_v[0]);
		if ((ifp == NULL) || (n->in_ifps[1] == ifp))
			n->in_ifps[1] = ipf_resolvenic(softc,
						       base + n->in_ifnames[1],
						       n->in_v[1]);

		if (n->in_redir & NAT_REDIRECT)
			idx = 1;
		else
			idx = 0;

		if (((ifp == NULL) || (n->in_ifps[idx] == ifp)) &&
		    (n->in_ifps[idx] != NULL &&
		     n->in_ifps[idx] != (void *)-1)) {

			ipf_nat_nextaddrinit(softc, n->in_names, &n->in_osrc,
					     0, n->in_ifps[idx]);
			ipf_nat_nextaddrinit(softc, n->in_names, &n->in_odst,
					     0, n->in_ifps[idx]);
			ipf_nat_nextaddrinit(softc, n->in_names, &n->in_nsrc,
					     0, n->in_ifps[idx]);
			ipf_nat_nextaddrinit(softc, n->in_names, &n->in_ndst,
					     0, n->in_ifps[idx]);
		}
	}
	RWLOCK_EXIT(&softc->ipf_nat);
	SPL_X(s);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_icmpquerytype                                       */
/* Returns:     int - 1 == success, 0 == failure                            */
/* Parameters:  icmptype(I) - ICMP type number                              */
/*                                                                          */
/* Tests to see if the ICMP type number passed is a query/response type or  */
/* not.                                                                     */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_icmpquerytype(icmptype)
	int icmptype;
{

	/*
	 * For the ICMP query NAT code, it is essential that both the query
	 * and the reply match on the NAT rule. Because the NAT structure
	 * does not keep track of the icmptype, and a single NAT structure
	 * is used for all icmp types with the same src, dest and id, we
	 * simply define the replies as queries as well. The funny thing is,
	 * altough it seems silly to call a reply a query, this is exactly
	 * as it is defined in the IPv4 specification
	 */
	switch (icmptype)
	{
	case ICMP_ECHOREPLY:
	case ICMP_ECHO:
	/* route advertisement/solicitation is currently unsupported: */
	/* it would require rewriting the ICMP data section          */
	case ICMP_TSTAMP:
	case ICMP_TSTAMPREPLY:
	case ICMP_IREQ:
	case ICMP_IREQREPLY:
	case ICMP_MASKREQ:
	case ICMP_MASKREPLY:
		return 1;
	default:
		return 0;
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_log                                                     */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              nat(I)    - pointer to NAT structure                        */
/*              action(I) - action related to NAT structure being performed */
/*                                                                          */
/* Creates a NAT log entry.                                                 */
/* ------------------------------------------------------------------------ */
void
ipf_nat_log(softc, softn, nat, action)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	struct nat *nat;
	u_int action;
{
#ifdef	IPFILTER_LOG
# ifndef LARGE_NAT
	struct ipnat *np;
	int rulen;
# endif
	struct natlog natl;
	void *items[1];
	size_t sizes[1];
	int types[1];

	bcopy((char *)&nat->nat_osrc6, (char *)&natl.nl_osrcip,
	      sizeof(natl.nl_osrcip));
	bcopy((char *)&nat->nat_nsrc6, (char *)&natl.nl_nsrcip,
	      sizeof(natl.nl_nsrcip));
	bcopy((char *)&nat->nat_odst6, (char *)&natl.nl_odstip,
	      sizeof(natl.nl_odstip));
	bcopy((char *)&nat->nat_ndst6, (char *)&natl.nl_ndstip,
	      sizeof(natl.nl_ndstip));

	natl.nl_bytes[0] = nat->nat_bytes[0];
	natl.nl_bytes[1] = nat->nat_bytes[1];
	natl.nl_pkts[0] = nat->nat_pkts[0];
	natl.nl_pkts[1] = nat->nat_pkts[1];
	natl.nl_odstport = nat->nat_odport;
	natl.nl_osrcport = nat->nat_osport;
	natl.nl_nsrcport = nat->nat_nsport;
	natl.nl_ndstport = nat->nat_ndport;
	natl.nl_p[0] = nat->nat_pr[0];
	natl.nl_p[1] = nat->nat_pr[1];
	natl.nl_v[0] = nat->nat_v[0];
	natl.nl_v[1] = nat->nat_v[1];
	natl.nl_type = nat->nat_redir;
	natl.nl_action = action;
	natl.nl_rule = -1;

	bcopy(nat->nat_ifnames[0], natl.nl_ifnames[0],
	      sizeof(nat->nat_ifnames[0]));
	bcopy(nat->nat_ifnames[1], natl.nl_ifnames[1],
	      sizeof(nat->nat_ifnames[1]));

# ifndef LARGE_NAT
	if (nat->nat_ptr != NULL) {
		for (rulen = 0, np = softn->ipf_nat_list; np != NULL;
		     np = np->in_next, rulen++)
			if (np == nat->nat_ptr) {
				natl.nl_rule = rulen;
				break;
			}
	}
# endif
	items[0] = &natl;
	sizes[0] = sizeof(natl);
	types[0] = 0;

	(void) ipf_log_items(softc, IPL_LOGNAT, NULL, items, sizes, types, 1);
#endif
}




/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_rule_deref                                          */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              inp(I)   - pointer to pointer to NAT rule                   */
/* Write Locks: ipf_nat                                                     */
/*                                                                          */
/* Dropping the refernce count for a rule means that whatever held the      */
/* pointer to this rule (*inp) is no longer interested in it and when the   */
/* reference count drops to zero, any resources allocated for the rule can  */
/* be released and the rule itself free'd.                                  */
/* ------------------------------------------------------------------------ */
void
ipf_nat_rule_deref(softc, inp)
	ipf_main_softc_t *softc;
	ipnat_t **inp;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	ipnat_t *n;

	n = *inp;
	*inp = NULL;
	n->in_use--;
	if (n->in_use > 0)
		return;

	if (n->in_apr != NULL)
		ipf_proxy_deref(n->in_apr);

	ipf_nat_rule_fini(softc, n);

	if (n->in_redir & NAT_REDIRECT) {
		if ((n->in_flags & IPN_PROXYRULE) == 0) {
			ATOMIC_DEC32(softn->ipf_nat_stats.ns_rules_rdr);
		}
	}
	if (n->in_redir & (NAT_MAP|NAT_MAPBLK)) {
		if ((n->in_flags & IPN_PROXYRULE) == 0) {
			ATOMIC_DEC32(softn->ipf_nat_stats.ns_rules_map);
		}
	}

	if (n->in_tqehead[0] != NULL) {
		if (ipf_deletetimeoutqueue(n->in_tqehead[0]) == 0) {
			ipf_freetimeoutqueue(softc, n->in_tqehead[1]);
		}
	}

	if (n->in_tqehead[1] != NULL) {
		if (ipf_deletetimeoutqueue(n->in_tqehead[1]) == 0) {
			ipf_freetimeoutqueue(softc, n->in_tqehead[1]);
		}
	}

	if ((n->in_flags & IPN_PROXYRULE) == 0) {
		ATOMIC_DEC32(softn->ipf_nat_stats.ns_rules);
	}

	MUTEX_DESTROY(&n->in_lock);

	KFREES(n, n->in_size);

#if SOLARIS && !defined(INSTANCES)
	if (softn->ipf_nat_stats.ns_rules == 0)
		pfil_delayed_copy = 1;
#endif
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_deref                                               */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              natp(I)  - pointer to pointer to NAT table entry            */
/*                                                                          */
/* Decrement the reference counter for this NAT table entry and free it if  */
/* there are no more things using it.                                       */
/*                                                                          */
/* IF nat_ref == 1 when this function is called, then we have an orphan nat */
/* structure *because* it only gets called on paths _after_ nat_ref has been*/
/* incremented.  If nat_ref == 1 then we shouldn't decrement it here        */
/* because nat_delete() will do that and send nat_ref to -1.                */
/*                                                                          */
/* Holding the lock on nat_lock is required to serialise nat_delete() being */
/* called from a NAT flush ioctl with a deref happening because of a packet.*/
/* ------------------------------------------------------------------------ */
void
ipf_nat_deref(softc, natp)
	ipf_main_softc_t *softc;
	nat_t **natp;
{
	nat_t *nat;

	nat = *natp;
	*natp = NULL;

	MUTEX_ENTER(&nat->nat_lock);
	if (nat->nat_ref > 1) {
		nat->nat_ref--;
		ASSERT(nat->nat_ref >= 0);
		MUTEX_EXIT(&nat->nat_lock);
		return;
	}
	MUTEX_EXIT(&nat->nat_lock);

	WRITE_ENTER(&softc->ipf_nat);
	ipf_nat_delete(softc, nat, NL_EXPIRE);
	RWLOCK_EXIT(&softc->ipf_nat);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_clone                                               */
/* Returns:     ipstate_t* - NULL == cloning failed,                        */
/*                           else pointer to new state structure            */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              is(I)  - pointer to master state structure                  */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* Create a "duplcate" state table entry from the master.                   */
/* ------------------------------------------------------------------------ */
nat_t *
ipf_nat_clone(fin, nat)
	fr_info_t *fin;
	nat_t *nat;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	frentry_t *fr;
	nat_t *clone;
	ipnat_t *np;

	KMALLOC(clone, nat_t *);
	if (clone == NULL) {
		NBUMPSIDED(fin->fin_out, ns_clone_nomem);
		return NULL;
	}
	bcopy((char *)nat, (char *)clone, sizeof(*clone));

	MUTEX_NUKE(&clone->nat_lock);

	clone->nat_rev = fin->fin_rev;
	clone->nat_aps = NULL;
	/*
	 * Initialize all these so that ipf_nat_delete() doesn't cause a crash.
	 */
	clone->nat_tqe.tqe_pnext = NULL;
	clone->nat_tqe.tqe_next = NULL;
	clone->nat_tqe.tqe_ifq = NULL;
	clone->nat_tqe.tqe_parent = clone;

	clone->nat_flags &= ~SI_CLONE;
	clone->nat_flags |= SI_CLONED;

	if (clone->nat_hm)
		clone->nat_hm->hm_ref++;

	if (ipf_nat_insert(softc, softn, clone) == -1) {
		KFREE(clone);
		NBUMPSIDED(fin->fin_out, ns_insert_fail);
		return NULL;
	}

	np = clone->nat_ptr;
	if (np != NULL) {
		if (softn->ipf_nat_logging)
			ipf_nat_log(softc, softn, clone, NL_CLONE);
		np->in_use++;
	}
	fr = clone->nat_fr;
	if (fr != NULL) {
		MUTEX_ENTER(&fr->fr_lock);
		fr->fr_ref++;
		MUTEX_EXIT(&fr->fr_lock);
	}


	/*
	 * Because the clone is created outside the normal loop of things and
	 * TCP has special needs in terms of state, initialise the timeout
	 * state of the new NAT from here.
	 */
	if (clone->nat_pr[0] == IPPROTO_TCP) {
		(void) ipf_tcp_age(&clone->nat_tqe, fin, softn->ipf_nat_tcptq,
				   clone->nat_flags, 2);
	}
	clone->nat_sync = ipf_sync_new(softc, SMC_NAT, fin, clone);
	if (softn->ipf_nat_logging)
		ipf_nat_log(softc, softn, clone, NL_CLONE);
	return clone;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_nat_wildok                                               */
/* Returns:    int - 1 == packet's ports match wildcards                    */
/*                   0 == packet's ports don't match wildcards              */
/* Parameters: nat(I)   - NAT entry                                         */
/*             sport(I) - source port                                       */
/*             dport(I) - destination port                                  */
/*             flags(I) - wildcard flags                                    */
/*             dir(I)   - packet direction                                  */
/*                                                                          */
/* Use NAT entry and packet direction to determine which combination of     */
/* wildcard flags should be used.                                           */
/* ------------------------------------------------------------------------ */
int
ipf_nat_wildok(nat, sport, dport, flags, dir)
	nat_t *nat;
	int sport, dport, flags, dir;
{
	/*
	 * When called by       dir is set to
	 * nat_inlookup         NAT_INBOUND (0)
	 * nat_outlookup        NAT_OUTBOUND (1)
	 *
	 * We simply combine the packet's direction in dir with the original
	 * "intended" direction of that NAT entry in nat->nat_dir to decide
	 * which combination of wildcard flags to allow.
	 */
	switch ((dir << 1) | (nat->nat_dir & (NAT_INBOUND|NAT_OUTBOUND)))
	{
	case 3: /* outbound packet / outbound entry */
		if (((nat->nat_osport == sport) ||
		    (flags & SI_W_SPORT)) &&
		    ((nat->nat_odport == dport) ||
		    (flags & SI_W_DPORT)))
			return 1;
		break;
	case 2: /* outbound packet / inbound entry */
		if (((nat->nat_osport == dport) ||
		    (flags & SI_W_SPORT)) &&
		    ((nat->nat_odport == sport) ||
		    (flags & SI_W_DPORT)))
			return 1;
		break;
	case 1: /* inbound packet / outbound entry */
		if (((nat->nat_osport == dport) ||
		    (flags & SI_W_SPORT)) &&
		    ((nat->nat_odport == sport) ||
		    (flags & SI_W_DPORT)))
			return 1;
		break;
	case 0: /* inbound packet / inbound entry */
		if (((nat->nat_osport == sport) ||
		    (flags & SI_W_SPORT)) &&
		    ((nat->nat_odport == dport) ||
		    (flags & SI_W_DPORT)))
			return 1;
		break;
	default:
		break;
	}

	return(0);
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_mssclamp                                                */
/* Returns:     Nil                                                         */
/* Parameters:  tcp(I)    - pointer to TCP header                           */
/*              maxmss(I) - value to clamp the TCP MSS to                   */
/*              fin(I)    - pointer to packet information                   */
/*              csump(I)  - pointer to TCP checksum                         */
/*                                                                          */
/* Check for MSS option and clamp it if necessary.  If found and changed,   */
/* then the TCP header checksum will be updated to reflect the change in    */
/* the MSS.                                                                 */
/* ------------------------------------------------------------------------ */
static void
ipf_nat_mssclamp(tcp, maxmss, fin, csump)
	tcphdr_t *tcp;
	u_32_t maxmss;
	fr_info_t *fin;
	u_short *csump;
{
	u_char *cp, *ep, opt;
	int hlen, advance;
	u_32_t mss, sumd;

	hlen = TCP_OFF(tcp) << 2;
	if (hlen > sizeof(*tcp)) {
		cp = (u_char *)tcp + sizeof(*tcp);
		ep = (u_char *)tcp + hlen;

		while (cp < ep) {
			opt = cp[0];
			if (opt == TCPOPT_EOL)
				break;
			else if (opt == TCPOPT_NOP) {
				cp++;
				continue;
			}

			if (cp + 1 >= ep)
				break;
			advance = cp[1];
			if ((cp + advance > ep) || (advance <= 0))
				break;
			switch (opt)
			{
			case TCPOPT_MAXSEG:
				if (advance != 4)
					break;
				mss = cp[2] * 256 + cp[3];
				if (mss > maxmss) {
					cp[2] = maxmss / 256;
					cp[3] = maxmss & 0xff;
					CALC_SUMD(mss, maxmss, sumd);
					ipf_fix_outcksum(0, csump, sumd, 0);
				}
				break;
			default:
				/* ignore unknown options */
				break;
			}

			cp += advance;
		}
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_setqueue                                            */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              nat(I)- pointer to NAT structure                            */
/* Locks:       ipf_nat (read or write)                                     */
/*                                                                          */
/* Put the NAT entry on its default queue entry, using rev as a helped in   */
/* determining which queue it should be placed on.                          */
/* ------------------------------------------------------------------------ */
void
ipf_nat_setqueue(softc, softn, nat)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	nat_t *nat;
{
	ipftq_t *oifq, *nifq;
	int rev = nat->nat_rev;

	if (nat->nat_ptr != NULL)
		nifq = nat->nat_ptr->in_tqehead[rev];
	else
		nifq = NULL;

	if (nifq == NULL) {
		switch (nat->nat_pr[0])
		{
		case IPPROTO_UDP :
			nifq = &softn->ipf_nat_udptq;
			break;
		case IPPROTO_ICMP :
			nifq = &softn->ipf_nat_icmptq;
			break;
		case IPPROTO_TCP :
			nifq = softn->ipf_nat_tcptq +
			       nat->nat_tqe.tqe_state[rev];
			break;
		default :
			nifq = &softn->ipf_nat_iptq;
			break;
		}
	}

	oifq = nat->nat_tqe.tqe_ifq;
	/*
	 * If it's currently on a timeout queue, move it from one queue to
	 * another, else put it on the end of the newly determined queue.
	 */
	if (oifq != NULL)
		ipf_movequeue(softc->ipf_ticks, &nat->nat_tqe, oifq, nifq);
	else
		ipf_queueappend(softc->ipf_ticks, &nat->nat_tqe, nifq, nat);
	return;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_getnext                                                 */
/* Returns:     int - 0 == ok, else error                                   */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              t(I)   - pointer to ipftoken structure                      */
/*              itp(I) - pointer to ipfgeniter_t structure                  */
/*                                                                          */
/* Fetch the next nat/ipnat structure pointer from the linked list and      */
/* copy it out to the storage space pointed to by itp_data.  The next item  */
/* in the list to look at is put back in the ipftoken struture.             */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_getnext(softc, t, itp, objp)
	ipf_main_softc_t *softc;
	ipftoken_t *t;
	ipfgeniter_t *itp;
	ipfobj_t *objp;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	hostmap_t *hm, *nexthm = NULL, zerohm;
	ipnat_t *ipn, *nextipnat = NULL, zeroipn;
	nat_t *nat, *nextnat = NULL, zeronat;
	int error = 0;
	void *nnext;

	if (itp->igi_nitems != 1) {
		IPFERROR(60075);
		return ENOSPC;
	}

	READ_ENTER(&softc->ipf_nat);

	switch (itp->igi_type)
	{
	case IPFGENITER_HOSTMAP :
		hm = t->ipt_data;
		if (hm == NULL) {
			nexthm = softn->ipf_hm_maplist;
		} else {
			nexthm = hm->hm_next;
		}
		if (nexthm != NULL) {
			ATOMIC_INC32(nexthm->hm_ref);
			t->ipt_data = nexthm;
		} else {
			bzero(&zerohm, sizeof(zerohm));
			nexthm = &zerohm;
			t->ipt_data = NULL;
		}
		nnext = nexthm->hm_next;
		break;

	case IPFGENITER_IPNAT :
		ipn = t->ipt_data;
		if (ipn == NULL) {
			nextipnat = softn->ipf_nat_list;
		} else {
			nextipnat = ipn->in_next;
		}
		if (nextipnat != NULL) {
			ATOMIC_INC32(nextipnat->in_use);
			t->ipt_data = nextipnat;
		} else {
			bzero(&zeroipn, sizeof(zeroipn));
			nextipnat = &zeroipn;
			t->ipt_data = NULL;
		}
		nnext = nextipnat->in_next;
		break;

	case IPFGENITER_NAT :
		nat = t->ipt_data;
		if (nat == NULL) {
			nextnat = softn->ipf_nat_instances;
		} else {
			nextnat = nat->nat_next;
		}
		if (nextnat != NULL) {
			MUTEX_ENTER(&nextnat->nat_lock);
			nextnat->nat_ref++;
			MUTEX_EXIT(&nextnat->nat_lock);
			t->ipt_data = nextnat;
		} else {
			bzero(&zeronat, sizeof(zeronat));
			nextnat = &zeronat;
			t->ipt_data = NULL;
		}
		nnext = nextnat->nat_next;
		break;

	default :
		RWLOCK_EXIT(&softc->ipf_nat);
		IPFERROR(60055);
		return EINVAL;
	}

	RWLOCK_EXIT(&softc->ipf_nat);

	objp->ipfo_ptr = itp->igi_data;

	switch (itp->igi_type)
	{
	case IPFGENITER_HOSTMAP :
		error = COPYOUT(nexthm, objp->ipfo_ptr, sizeof(*nexthm));
		if (error != 0) {
			IPFERROR(60049);
			error = EFAULT;
		}
		if (hm != NULL) {
			WRITE_ENTER(&softc->ipf_nat);
			ipf_nat_hostmapdel(softc, &hm);
			RWLOCK_EXIT(&softc->ipf_nat);
		}
		break;

	case IPFGENITER_IPNAT :
		objp->ipfo_size = nextipnat->in_size;
		objp->ipfo_type = IPFOBJ_IPNAT;
		error = ipf_outobjk(softc, objp, nextipnat);
		if (ipn != NULL) {
			WRITE_ENTER(&softc->ipf_nat);
			ipf_nat_rule_deref(softc, &ipn);
			RWLOCK_EXIT(&softc->ipf_nat);
		}
		break;

	case IPFGENITER_NAT :
		objp->ipfo_size = sizeof(nat_t);
		objp->ipfo_type = IPFOBJ_NAT;
		error = ipf_outobjk(softc, objp, nextnat);
		if (nat != NULL)
			ipf_nat_deref(softc, &nat);

		break;
	}

	if (nnext == NULL)
		ipf_token_mark_complete(t);

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_extraflush                                              */
/* Returns:     int - 0 == success, -1 == failure                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              which(I) - how to flush the active NAT table                */
/* Write Locks: ipf_nat                                                     */
/*                                                                          */
/* Flush nat tables.  Three actions currently defined:                      */
/* which == 0 : flush all nat table entries                                 */
/* which == 1 : flush TCP connections which have started to close but are   */
/*	      stuck for some reason.                                        */
/* which == 2 : flush TCP connections which have been idle for a long time, */
/*	      starting at > 4 days idle and working back in successive half-*/
/*	      days to at most 12 hours old.  If this fails to free enough   */
/*            slots then work backwards in half hour slots to 30 minutes.   */
/*            If that too fails, then work backwards in 30 second intervals */
/*            for the last 30 minutes to at worst 30 seconds idle.          */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_extraflush(softc, softn, which)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	int which;
{
	nat_t *nat, **natp;
	ipftqent_t *tqn;
	ipftq_t *ifq;
	int removed;
	SPL_INT(s);

	removed = 0;

	SPL_NET(s);
	switch (which)
	{
	case 0 :
		softn->ipf_nat_stats.ns_flush_all++;
		/*
		 * Style 0 flush removes everything...
		 */
		for (natp = &softn->ipf_nat_instances;
		     ((nat = *natp) != NULL); ) {
			ipf_nat_delete(softc, nat, NL_FLUSH);
			removed++;
		}
		break;

	case 1 :
		softn->ipf_nat_stats.ns_flush_closing++;
		/*
		 * Since we're only interested in things that are closing,
		 * we can start with the appropriate timeout queue.
		 */
		for (ifq = softn->ipf_nat_tcptq + IPF_TCPS_CLOSE_WAIT;
		     ifq != NULL; ifq = ifq->ifq_next) {

			for (tqn = ifq->ifq_head; tqn != NULL; ) {
				nat = tqn->tqe_parent;
				tqn = tqn->tqe_next;
				if (nat->nat_pr[0] != IPPROTO_TCP ||
				    nat->nat_pr[1] != IPPROTO_TCP)
					break;
				ipf_nat_delete(softc, nat, NL_EXPIRE);
				removed++;
			}
		}

		/*
		 * Also need to look through the user defined queues.
		 */
		for (ifq = softn->ipf_nat_utqe; ifq != NULL;
		     ifq = ifq->ifq_next) {
			for (tqn = ifq->ifq_head; tqn != NULL; ) {
				nat = tqn->tqe_parent;
				tqn = tqn->tqe_next;
				if (nat->nat_pr[0] != IPPROTO_TCP ||
				    nat->nat_pr[1] != IPPROTO_TCP)
					continue;

				if ((nat->nat_tcpstate[0] >
				     IPF_TCPS_ESTABLISHED) &&
				    (nat->nat_tcpstate[1] >
				     IPF_TCPS_ESTABLISHED)) {
					ipf_nat_delete(softc, nat, NL_EXPIRE);
					removed++;
				}
			}
		}
		break;

		/*
		 * Args 5-11 correspond to flushing those particular states
		 * for TCP connections.
		 */
	case IPF_TCPS_CLOSE_WAIT :
	case IPF_TCPS_FIN_WAIT_1 :
	case IPF_TCPS_CLOSING :
	case IPF_TCPS_LAST_ACK :
	case IPF_TCPS_FIN_WAIT_2 :
	case IPF_TCPS_TIME_WAIT :
	case IPF_TCPS_CLOSED :
		softn->ipf_nat_stats.ns_flush_state++;
		tqn = softn->ipf_nat_tcptq[which].ifq_head;
		while (tqn != NULL) {
			nat = tqn->tqe_parent;
			tqn = tqn->tqe_next;
			ipf_nat_delete(softc, nat, NL_FLUSH);
			removed++;
		}
		break;

	default :
		if (which < 30)
			break;

		softn->ipf_nat_stats.ns_flush_timeout++;
		/*
		 * Take a large arbitrary number to mean the number of seconds
		 * for which which consider to be the maximum value we'll allow
		 * the expiration to be.
		 */
		which = IPF_TTLVAL(which);
		for (natp = &softn->ipf_nat_instances;
		     ((nat = *natp) != NULL); ) {
			if (softc->ipf_ticks - nat->nat_touched > which) {
				ipf_nat_delete(softc, nat, NL_FLUSH);
				removed++;
			} else
				natp = &nat->nat_next;
		}
		break;
	}

	if (which != 2) {
		SPL_X(s);
		return removed;
	}

	softn->ipf_nat_stats.ns_flush_queue++;

	/*
	 * Asked to remove inactive entries because the table is full, try
	 * again, 3 times, if first attempt failed with a different criteria
	 * each time.  The order tried in must be in decreasing age.
	 * Another alternative is to implement random drop and drop N entries
	 * at random until N have been freed up.
	 */
	if (softc->ipf_ticks - softn->ipf_nat_last_force_flush >
	    IPF_TTLVAL(5)) {
		softn->ipf_nat_last_force_flush = softc->ipf_ticks;

		removed = ipf_queueflush(softc, ipf_nat_flush_entry,
					 softn->ipf_nat_tcptq,
					 softn->ipf_nat_utqe,
					 &softn->ipf_nat_stats.ns_active,
					 softn->ipf_nat_table_sz,
					 softn->ipf_nat_table_wm_low);
	}

	SPL_X(s);
	return removed;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_flush_entry                                         */
/* Returns:     0 - always succeeds                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              entry(I) - pointer to NAT entry                             */
/* Write Locks: ipf_nat                                                     */
/*                                                                          */
/* This function is a stepping stone between ipf_queueflush() and           */
/* nat_dlete().  It is used so we can provide a uniform interface via the   */
/* ipf_queueflush() function.  Since the nat_delete() function returns void */
/* we translate that to mean it always succeeds in deleting something.      */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_flush_entry(softc, entry)
	ipf_main_softc_t *softc;
	void *entry;
{
	ipf_nat_delete(softc, entry, NL_FLUSH);
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_iterator                                            */
/* Returns:     int - 0 == ok, else error                                   */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              token(I) - pointer to ipftoken structure                    */
/*              itp(I)   - pointer to ipfgeniter_t structure                */
/*              obj(I)   - pointer to data description structure            */
/*                                                                          */
/* This function acts as a handler for the SIOCGENITER ioctls that use a    */
/* generic structure to iterate through a list.  There are three different  */
/* linked lists of NAT related information to go through: NAT rules, active */
/* NAT mappings and the NAT fragment cache.                                 */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_iterator(softc, token, itp, obj)
	ipf_main_softc_t *softc;
	ipftoken_t *token;
	ipfgeniter_t *itp;
	ipfobj_t *obj;
{
	int error;

	if (itp->igi_data == NULL) {
		IPFERROR(60052);
		return EFAULT;
	}

	switch (itp->igi_type)
	{
	case IPFGENITER_HOSTMAP :
	case IPFGENITER_IPNAT :
	case IPFGENITER_NAT :
		error = ipf_nat_getnext(softc, token, itp, obj);
		break;

	case IPFGENITER_NATFRAG :
		error = ipf_frag_nat_next(softc, token, itp);
		break;
	default :
		IPFERROR(60053);
		error = EINVAL;
		break;
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_setpending                                          */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              nat(I)   - pointer to NAT structure                         */
/* Locks:       ipf_nat (read or write)                                     */
/*                                                                          */
/* Put the NAT entry on to the pending queue - this queue has a very short  */
/* lifetime where items are put that can't be deleted straight away because */
/* of locking issues but we want to delete them ASAP, anyway.  In calling   */
/* this function, it is assumed that the owner (if there is one, as shown   */
/* by nat_me) is no longer interested in it.                                */
/* ------------------------------------------------------------------------ */
void
ipf_nat_setpending(softc, nat)
	ipf_main_softc_t *softc;
	nat_t *nat;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	ipftq_t *oifq;

	oifq = nat->nat_tqe.tqe_ifq;
	if (oifq != NULL)
		ipf_movequeue(softc->ipf_ticks, &nat->nat_tqe, oifq,
			      &softn->ipf_nat_pending);
	else
		ipf_queueappend(softc->ipf_ticks, &nat->nat_tqe,
				&softn->ipf_nat_pending, nat);

	if (nat->nat_me != NULL) {
		*nat->nat_me = NULL;
		nat->nat_me = NULL;
		nat->nat_ref--;
		ASSERT(nat->nat_ref >= 0);
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_newrewrite                                              */
/* Returns:     int - -1 == error, 0 == success (no move), 1 == success and */
/*                    allow rule to be moved if IPN_ROUNDR is set.          */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to NAT entry                               */
/*              ni(I)  - pointer to structure with misc. information needed */
/*                       to create new NAT entry.                           */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* This function is responsible for setting up an active NAT session where  */
/* we are changing both the source and destination parameters at the same   */
/* time.  The loop in here works differently to elsewhere - each iteration  */
/* is responsible for changing a single parameter that can be incremented.  */
/* So one pass may increase the source IP#, next source port, next dest. IP#*/
/* and the last destination port for a total of 4 iterations to try each.   */
/* This is done to try and exhaustively use the translation space available.*/
/* ------------------------------------------------------------------------ */
static int
ipf_nat_newrewrite(fin, nat, nai)
	fr_info_t *fin;
	nat_t *nat;
	natinfo_t *nai;
{
	int src_search = 1;
	int dst_search = 1;
	fr_info_t frnat;
	u_32_t flags;
	u_short swap;
	ipnat_t *np;
	nat_t *natl;
	int l = 0;
	int changed;

	natl = NULL;
	changed = -1;
	np = nai->nai_np;
	flags = nat->nat_flags;
	bcopy((char *)fin, (char *)&frnat, sizeof(*fin));

	nat->nat_hm = NULL;

	do {
		changed = -1;
		/* TRACE (l, src_search, dst_search, np) */
		DT4(ipf_nat_rewrite_1, int, l, int, src_search, int, dst_search, ipnat_t *, np);

		if ((src_search == 0) && (np->in_spnext == 0) &&
		    (dst_search == 0) && (np->in_dpnext == 0)) {
			if (l > 0)
				return -1;
		}

		/*
		 * Find a new source address
		 */
		if (ipf_nat_nextaddr(fin, &np->in_nsrc, &frnat.fin_saddr,
				     &frnat.fin_saddr) == -1) {
			return -1;
		}

		if ((np->in_nsrcaddr == 0) && (np->in_nsrcmsk == 0xffffffff)) {
			src_search = 0;
			if (np->in_stepnext == 0)
				np->in_stepnext = 1;

		} else if ((np->in_nsrcaddr == 0) && (np->in_nsrcmsk == 0)) {
			src_search = 0;
			if (np->in_stepnext == 0)
				np->in_stepnext = 1;

		} else if (np->in_nsrcmsk == 0xffffffff) {
			src_search = 0;
			if (np->in_stepnext == 0)
				np->in_stepnext = 1;

		} else if (np->in_nsrcmsk != 0xffffffff) {
			if (np->in_stepnext == 0 && changed == -1) {
				np->in_snip++;
				np->in_stepnext++;
				changed = 0;
			}
		}

		if ((flags & IPN_TCPUDPICMP) != 0) {
			if (np->in_spnext != 0)
				frnat.fin_data[0] = np->in_spnext;

			/*
			 * Standard port translation.  Select next port.
			 */
			if ((flags & IPN_FIXEDSPORT) != 0) {
				np->in_stepnext = 2;
			} else if ((np->in_stepnext == 1) &&
				   (changed == -1) && (natl != NULL)) {
				np->in_spnext++;
				np->in_stepnext++;
				changed = 1;
				if (np->in_spnext > np->in_spmax)
					np->in_spnext = np->in_spmin;
			}
		} else {
			np->in_stepnext = 2;
		}
		np->in_stepnext &= 0x3;

		/*
		 * Find a new destination address
		 */
		/* TRACE (fin, np, l, frnat) */
		DT4(ipf_nat_rewrite_2, frinfo_t *, fin, ipnat_t *, np, int, l, frinfo_t *, &frnat);

		if (ipf_nat_nextaddr(fin, &np->in_ndst, &frnat.fin_daddr,
				     &frnat.fin_daddr) == -1)
			return -1;
		if ((np->in_ndstaddr == 0) && (np->in_ndstmsk == 0xffffffff)) {
			dst_search = 0;
			if (np->in_stepnext == 2)
				np->in_stepnext = 3;

		} else if ((np->in_ndstaddr == 0) && (np->in_ndstmsk == 0)) {
			dst_search = 0;
			if (np->in_stepnext == 2)
				np->in_stepnext = 3;

		} else if (np->in_ndstmsk == 0xffffffff) {
			dst_search = 0;
			if (np->in_stepnext == 2)
				np->in_stepnext = 3;

		} else if (np->in_ndstmsk != 0xffffffff) {
			if ((np->in_stepnext == 2) && (changed == -1) &&
			    (natl != NULL)) {
				changed = 2;
				np->in_stepnext++;
				np->in_dnip++;
			}
		}

		if ((flags & IPN_TCPUDPICMP) != 0) {
			if (np->in_dpnext != 0)
				frnat.fin_data[1] = np->in_dpnext;

			/*
			 * Standard port translation.  Select next port.
			 */
			if ((flags & IPN_FIXEDDPORT) != 0) {
				np->in_stepnext = 0;
			} else if (np->in_stepnext == 3 && changed == -1) {
				np->in_dpnext++;
				np->in_stepnext++;
				changed = 3;
				if (np->in_dpnext > np->in_dpmax)
					np->in_dpnext = np->in_dpmin;
			}
		} else {
			if (np->in_stepnext == 3)
				np->in_stepnext = 0;
		}

		/* TRACE (frnat) */
		DT1(ipf_nat_rewrite_3, frinfo_t *, &frnat);

		/*
		 * Here we do a lookup of the connection as seen from
		 * the outside.  If an IP# pair already exists, try
		 * again.  So if you have A->B becomes C->B, you can
		 * also have D->E become C->E but not D->B causing
		 * another C->B.  Also take protocol and ports into
		 * account when determining whether a pre-existing
		 * NAT setup will cause an external conflict where
		 * this is appropriate.
		 *
		 * fin_data[] is swapped around because we are doing a
		 * lookup of the packet is if it were moving in the opposite
		 * direction of the one we are working with now.
		 */
		if (flags & IPN_TCPUDP) {
			swap = frnat.fin_data[0];
			frnat.fin_data[0] = frnat.fin_data[1];
			frnat.fin_data[1] = swap;
		}
		if (fin->fin_out == 1) {
			natl = ipf_nat_inlookup(&frnat,
						flags & ~(SI_WILDP|NAT_SEARCH),
						(u_int)frnat.fin_p,
						frnat.fin_dst, frnat.fin_src);

		} else {
			natl = ipf_nat_outlookup(&frnat,
						 flags & ~(SI_WILDP|NAT_SEARCH),
						 (u_int)frnat.fin_p,
						 frnat.fin_dst, frnat.fin_src);
		}
		if (flags & IPN_TCPUDP) {
			swap = frnat.fin_data[0];
			frnat.fin_data[0] = frnat.fin_data[1];
			frnat.fin_data[1] = swap;
		}

		/* TRACE natl, in_stepnext, l */
		DT3(ipf_nat_rewrite_2, nat_t *, natl, ipnat_t *, np , int, l);

		if ((natl != NULL) && (l > 8))	/* XXX 8 is arbitrary */
			return -1;

		np->in_stepnext &= 0x3;

		l++;
		changed = -1;
	} while (natl != NULL);

	nat->nat_osrcip = fin->fin_src;
	nat->nat_odstip = fin->fin_dst;
	nat->nat_nsrcip = frnat.fin_src;
	nat->nat_ndstip = frnat.fin_dst;

	if ((flags & IPN_TCPUDP) != 0) {
		nat->nat_osport = htons(fin->fin_data[0]);
		nat->nat_odport = htons(fin->fin_data[1]);
		nat->nat_nsport = htons(frnat.fin_data[0]);
		nat->nat_ndport = htons(frnat.fin_data[1]);
	} else if ((flags & IPN_ICMPQUERY) != 0) {
		nat->nat_oicmpid = fin->fin_data[1];
		nat->nat_nicmpid = frnat.fin_data[1];
	}

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_newdivert                                               */
/* Returns:     int - -1 == error, 0 == success                             */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to NAT entry                               */
/*              ni(I)  - pointer to structure with misc. information needed */
/*                       to create new NAT entry.                           */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* Create a new NAT  divert session as defined by the NAT rule.  This is    */
/* somewhat different to other NAT session creation routines because we     */
/* do not iterate through either port numbers or IP addresses, searching    */
/* for a unique mapping, however, a complimentary duplicate check is made.  */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_newdivert(fin, nat, nai)
	fr_info_t *fin;
	nat_t *nat;
	natinfo_t *nai;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	fr_info_t frnat;
	ipnat_t *np;
	nat_t *natl;
	int p;

	np = nai->nai_np;
	bcopy((char *)fin, (char *)&frnat, sizeof(*fin));

	nat->nat_pr[0] = 0;
	nat->nat_osrcaddr = fin->fin_saddr;
	nat->nat_odstaddr = fin->fin_daddr;
	frnat.fin_saddr = htonl(np->in_snip);
	frnat.fin_daddr = htonl(np->in_dnip);
	if ((nat->nat_flags & IPN_TCPUDP) != 0) {
		nat->nat_osport = htons(fin->fin_data[0]);
		nat->nat_odport = htons(fin->fin_data[1]);
	} else if ((nat->nat_flags & IPN_ICMPQUERY) != 0) {
		nat->nat_oicmpid = fin->fin_data[1];
	}

	if (np->in_redir & NAT_DIVERTUDP) {
		frnat.fin_data[0] = np->in_spnext;
		frnat.fin_data[1] = np->in_dpnext;
		frnat.fin_flx |= FI_TCPUDP;
		p = IPPROTO_UDP;
	} else {
		frnat.fin_flx &= ~FI_TCPUDP;
		p = IPPROTO_IPIP;
	}

	if (fin->fin_out == 1) {
		natl = ipf_nat_inlookup(&frnat, 0, p,
					frnat.fin_dst, frnat.fin_src);

	} else {
		natl = ipf_nat_outlookup(&frnat, 0, p,
					 frnat.fin_dst, frnat.fin_src);
	}

	if (natl != NULL) {
		NBUMPSIDED(fin->fin_out, ns_divert_exist);
		DT3(ns_divert_exist, fr_info_t *, fin, nat_t *, nat, natinfo_t, nai);
		return -1;
	}

	nat->nat_nsrcaddr = frnat.fin_saddr;
	nat->nat_ndstaddr = frnat.fin_daddr;
	if ((nat->nat_flags & IPN_TCPUDP) != 0) {
		nat->nat_nsport = htons(frnat.fin_data[0]);
		nat->nat_ndport = htons(frnat.fin_data[1]);
	} else if ((nat->nat_flags & IPN_ICMPQUERY) != 0) {
		nat->nat_nicmpid = frnat.fin_data[1];
	}

	nat->nat_pr[fin->fin_out] = fin->fin_p;
	nat->nat_pr[1 - fin->fin_out] = p;

	if (np->in_redir & NAT_REDIRECT)
		nat->nat_dir = NAT_DIVERTIN;
	else
		nat->nat_dir = NAT_DIVERTOUT;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_builddivertmp                                           */
/* Returns:     int - -1 == error, 0 == success                             */
/* Parameters:  softn(I) - pointer to NAT context structure                 */
/*              np(I)    - pointer to a NAT rule                            */
/*                                                                          */
/* For divert rules, a skeleton packet representing what will be prepended  */
/* to the real packet is created.  Even though we don't have the full       */
/* packet here, a checksum is calculated that we update later when we       */
/* fill in the final details.  At present a 0 checksum for UDP is being set */
/* here because it is expected that divert will be used for localhost.      */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_builddivertmp(softn, np)
	ipf_nat_softc_t *softn;
	ipnat_t *np;
{
	udphdr_t *uh;
	size_t len;
	ip_t *ip;

	if ((np->in_redir & NAT_DIVERTUDP) != 0)
		len = sizeof(ip_t) + sizeof(udphdr_t);
	else
		len = sizeof(ip_t);

	ALLOC_MB_T(np->in_divmp, len);
	if (np->in_divmp == NULL) {
		NBUMPD(ipf_nat_stats, ns_divert_build);
		return -1;
	}

	/*
	 * First, the header to get the packet diverted to the new destination
	 */
	ip = MTOD(np->in_divmp, ip_t *);
	IP_V_A(ip, 4);
	IP_HL_A(ip, 5);
	ip->ip_tos = 0;
	if ((np->in_redir & NAT_DIVERTUDP) != 0)
		ip->ip_p = IPPROTO_UDP;
	else
		ip->ip_p = IPPROTO_IPIP;
	ip->ip_ttl = 255;
	ip->ip_off = 0;
	ip->ip_sum = 0;
	ip->ip_len = htons(len);
	ip->ip_id = 0;
	ip->ip_src.s_addr = htonl(np->in_snip);
	ip->ip_dst.s_addr = htonl(np->in_dnip);
	ip->ip_sum = ipf_cksum((u_short *)ip, sizeof(*ip));

	if (np->in_redir & NAT_DIVERTUDP) {
		uh = (udphdr_t *)(ip + 1);
		uh->uh_sum = 0;
		uh->uh_ulen = 8;
		uh->uh_sport = htons(np->in_spnext);
		uh->uh_dport = htons(np->in_dpnext);
	}

	return 0;
}


#define	MINDECAP	(sizeof(ip_t) + sizeof(udphdr_t) + sizeof(ip_t))

/* ------------------------------------------------------------------------ */
/* Function:    nat_decap                                                   */
/* Returns:     int - -1 == error, 0 == success                             */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to current NAT session                     */
/*                                                                          */
/* This function is responsible for undoing a packet's encapsulation in the */
/* reverse of an encap/divert rule.  After removing the outer encapsulation */
/* it is necessary to call ipf_makefrip() again so that the contents of 'fin'*/
/* match the "new" packet as it may still be used by IPFilter elsewhere.    */
/* We use "dir" here as the basis for some of the expectations about the    */
/* outer header.  If we return an error, the goal is to leave the original  */
/* packet information undisturbed - this falls short at the end where we'd  */
/* need to back a backup copy of "fin" - expensive.                         */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_decap(fin, nat)
	fr_info_t *fin;
	nat_t *nat;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	char *hdr;
	int hlen;
	int skip;
	mb_t *m;

	if ((fin->fin_flx & FI_ICMPERR) != 0) {
		/*
		 * ICMP packets don't get decapsulated, instead what we need
		 * to do is change the ICMP reply from including (in the data
		 * portion for errors) the encapsulated packet that we sent
		 * out to something that resembles the original packet prior
		 * to encapsulation.  This isn't done here - all we're doing
		 * here is changing the outer address to ensure that it gets
		 * targetted back to the correct system.
		 */

		if (nat->nat_dir & NAT_OUTBOUND) {
			u_32_t sum1, sum2, sumd;

			sum1 = ntohl(fin->fin_daddr);
			sum2 = ntohl(nat->nat_osrcaddr);
			CALC_SUMD(sum1, sum2, sumd);
			fin->fin_ip->ip_dst = nat->nat_osrcip;
			fin->fin_daddr = nat->nat_osrcaddr;
#if !defined(_KERNEL) || defined(MENTAT)
			ipf_fix_outcksum(0, &fin->fin_ip->ip_sum, sumd, 0);
#endif
		}
		return 0;
	}

	m = fin->fin_m;
	skip = fin->fin_hlen;

	switch (nat->nat_dir)
	{
	case NAT_DIVERTIN :
	case NAT_DIVERTOUT :
		if (fin->fin_plen < MINDECAP)
			return -1;
		skip += sizeof(udphdr_t);
		break;

	case NAT_ENCAPIN :
	case NAT_ENCAPOUT :
		if (fin->fin_plen < (skip + sizeof(ip_t)))
			return -1;
		break;
	default :
		return -1;
		/* NOTREACHED */
	}

	/*
	 * The aim here is to keep the original packet details in "fin" for
	 * as long as possible so that returning with an error is for the
	 * original packet and there is little undoing work to do.
	 */
	if (M_LEN(m) < skip + sizeof(ip_t)) {
		if (ipf_pr_pullup(fin, skip + sizeof(ip_t)) == -1)
			return -1;
	}

	hdr = MTOD(fin->fin_m, char *);
	fin->fin_ip = (ip_t *)(hdr + skip);
	hlen = IP_HL(fin->fin_ip) << 2;

	if (ipf_pr_pullup(fin, skip + hlen) == -1) {
		NBUMPSIDED(fin->fin_out, ns_decap_pullup);
		return -1;
	}

	fin->fin_hlen = hlen;
	fin->fin_dlen -= skip;
	fin->fin_plen -= skip;
	fin->fin_ipoff += skip;

	if (ipf_makefrip(hlen, (ip_t *)hdr, fin) == -1) {
		NBUMPSIDED(fin->fin_out, ns_decap_bad);
		return -1;
	}

	return skip;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_nextaddr                                                */
/* Returns:     int - -1 == bad input (no new address),                     */
/*                     0 == success and dst has new address                 */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              na(I)  - how to generate new address                        */
/*              old(I) - original address being replaced                    */
/*              dst(O) - where to put the new address                       */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* This function uses the contents of the "na" structure, in combination    */
/* with "old" to produce a new address to store in "dst".  Not all of the   */
/* possible uses of "na" will result in a new address.                      */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_nextaddr(fin, na, old, dst)
	fr_info_t *fin;
	nat_addr_t *na;
	u_32_t *old, *dst;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_32_t amin, amax, new;
	i6addr_t newip;
	int error;

	new = 0;
	amin = na->na_addr[0].in4.s_addr;

	switch (na->na_atype)
	{
	case FRI_RANGE :
		amax = na->na_addr[1].in4.s_addr;
		break;

	case FRI_NETMASKED :
	case FRI_DYNAMIC :
	case FRI_NORMAL :
		/*
		 * Compute the maximum address by adding the inverse of the
		 * netmask to the minimum address.
		 */
		amax = ~na->na_addr[1].in4.s_addr;
		amax |= amin;
		break;

	case FRI_LOOKUP :
		break;

	case FRI_BROADCAST :
	case FRI_PEERADDR :
	case FRI_NETWORK :
	default :
		DT4(ns_na_atype, fr_info_t *, fin, nat_addr_t *, na, u_32_t *, old, u_32_t *, new);
		return -1;
	}

	error = -1;

	if (na->na_atype == FRI_LOOKUP) {
		if (na->na_type == IPLT_DSTLIST) {
			error = ipf_dstlist_select_node(fin, na->na_ptr, dst,
							NULL);
		} else {
			NBUMPSIDE(fin->fin_out, ns_badnextaddr);
			DT4(ns_badnextaddr_1, fr_info_t *, fin, nat_addr_t *, na, u_32_t *, old, u_32_t *, new);
		}

	} else if (na->na_atype == IPLT_NONE) {
		/*
		 * 0/0 as the new address means leave it alone.
		 */
		if (na->na_addr[0].in4.s_addr == 0 &&
		    na->na_addr[1].in4.s_addr == 0) {
			new = *old;

		/*
		 * 0/32 means get the interface's address
		 */
		} else if (na->na_addr[0].in4.s_addr == 0 &&
			   na->na_addr[1].in4.s_addr == 0xffffffff) {
			if (ipf_ifpaddr(softc, 4, na->na_atype,
					fin->fin_ifp, &newip, NULL) == -1) {
				NBUMPSIDED(fin->fin_out, ns_ifpaddrfail);
				DT4(ns_ifpaddrfail, fr_info_t *, fin, nat_addr_t *, na, u_32_t *, old, u_32_t *, new);
				return -1;
			}
			new = newip.in4.s_addr;
		} else {
			new = htonl(na->na_nextip);
		}
		*dst = new;
		error = 0;

	} else {
		NBUMPSIDE(fin->fin_out, ns_badnextaddr);
		DT4(ns_badnextaddr_2, fr_info_t *, fin, nat_addr_t *, na, u_32_t *, old, u_32_t *, new);
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat_nextaddrinit                                            */
/* Returns:     int - 0 == success, else error number                       */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              na(I)      - NAT address information for generating new addr*/
/*              initial(I) - flag indicating if it is the first call for    */
/*                           this "na" structure.                           */
/*              ifp(I)     - network interface to derive address            */
/*                           information from.                              */
/*                                                                          */
/* This function is expected to be called in two scenarious: when a new NAT */
/* rule is loaded into the kernel and when the list of NAT rules is sync'd  */
/* up with the valid network interfaces (possibly due to them changing.)    */
/* To distinguish between these, the "initial" parameter is used.  If it is */
/* 1 then this indicates the rule has just been reloaded and 0 for when we  */
/* are updating information.  This difference is important because in       */
/* instances where we are not updating address information associated with  */
/* a network interface, we don't want to disturb what the "next" address to */
/* come out of ipf_nat_nextaddr() will be.                                  */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_nextaddrinit(softc, base, na, initial, ifp)
	ipf_main_softc_t *softc;
	char *base;
	nat_addr_t *na;
	int initial;
	void *ifp;
{

	switch (na->na_atype)
	{
	case FRI_LOOKUP :
		if (na->na_subtype == 0) {
			na->na_ptr = ipf_lookup_res_num(softc, IPL_LOGNAT,
							na->na_type,
							na->na_num,
							&na->na_func);
		} else if (na->na_subtype == 1) {
			na->na_ptr = ipf_lookup_res_name(softc, IPL_LOGNAT,
							 na->na_type,
							 base + na->na_num,
							 &na->na_func);
		}
		if (na->na_func == NULL) {
			IPFERROR(60060);
			return ESRCH;
		}
		if (na->na_ptr == NULL) {
			IPFERROR(60056);
			return ESRCH;
		}
		break;

	case FRI_DYNAMIC :
	case FRI_BROADCAST :
	case FRI_NETWORK :
	case FRI_NETMASKED :
	case FRI_PEERADDR :
		if (ifp != NULL)
			(void )ipf_ifpaddr(softc, 4, na->na_atype, ifp,
					   &na->na_addr[0], &na->na_addr[1]);
		break;

	case FRI_SPLIT :
	case FRI_RANGE :
		if (initial)
			na->na_nextip = ntohl(na->na_addr[0].in4.s_addr);
		break;

	case FRI_NONE :
		na->na_addr[0].in4.s_addr &= na->na_addr[1].in4.s_addr;
		return 0;

	case FRI_NORMAL :
		na->na_addr[0].in4.s_addr &= na->na_addr[1].in4.s_addr;
		break;

	default :
		IPFERROR(60054);
		return EINVAL;
	}

	if (initial && (na->na_atype == FRI_NORMAL)) {
		if (na->na_addr[0].in4.s_addr == 0) {
			if ((na->na_addr[1].in4.s_addr == 0xffffffff) ||
			    (na->na_addr[1].in4.s_addr == 0)) {
				return 0;
			}
		}

		if (na->na_addr[1].in4.s_addr == 0xffffffff) {
			na->na_nextip = ntohl(na->na_addr[0].in4.s_addr);
		} else {
			na->na_nextip = ntohl(na->na_addr[0].in4.s_addr) + 1;
		}
	}

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_matchflush                                          */
/* Returns:     int - -1 == error, 0 == success                             */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              nat(I)   - pointer to current NAT session                   */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_matchflush(softc, softn, data)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	caddr_t data;
{
	int *array, flushed, error;
	nat_t *nat, *natnext;
	ipfobj_t obj;

	error = ipf_matcharray_load(softc, data, &obj, &array);
	if (error != 0)
		return error;

	flushed = 0;

	for (nat = softn->ipf_nat_instances; nat != NULL; nat = natnext) {
		natnext = nat->nat_next;
		if (ipf_nat_matcharray(nat, array, softc->ipf_ticks) == 0) {
			ipf_nat_delete(softc, nat, NL_FLUSH);
			flushed++;
		}
	}

	obj.ipfo_retval = flushed;
	error = BCOPYOUT(&obj, data, sizeof(obj));

	KFREES(array, array[0] * sizeof(*array));

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_matcharray                                          */
/* Returns:     int - -1 == error, 0 == success                             */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to current NAT session                     */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_matcharray(nat, array, ticks)
	nat_t *nat;
	int *array;
	u_long ticks;
{
	int i, n, *x, e, p;

	e = 0;
	n = array[0];
	x = array + 1;

	for (; n > 0; x += 3 + x[2]) {
		if (x[0] == IPF_EXP_END)
			break;
		e = 0;

		n -= x[2] + 3;
		if (n < 0)
			break;

		p = x[0] >> 16;
		if (p != 0 && p != nat->nat_pr[1])
			break;

		switch (x[0])
		{
		case IPF_EXP_IP_PR :
			for (i = 0; !e && i < x[2]; i++) {
				e |= (nat->nat_pr[1] == x[i + 3]);
			}
			break;

		case IPF_EXP_IP_SRCADDR :
			if (nat->nat_v[0] == 4) {
				for (i = 0; !e && i < x[2]; i++) {
					e |= ((nat->nat_osrcaddr & x[i + 4]) ==
					      x[i + 3]);
				}
			}
			if (nat->nat_v[1] == 4) {
				for (i = 0; !e && i < x[2]; i++) {
					e |= ((nat->nat_nsrcaddr & x[i + 4]) ==
					      x[i + 3]);
				}
			}
			break;

		case IPF_EXP_IP_DSTADDR :
			if (nat->nat_v[0] == 4) {
				for (i = 0; !e && i < x[2]; i++) {
					e |= ((nat->nat_odstaddr & x[i + 4]) ==
					      x[i + 3]);
				}
			}
			if (nat->nat_v[1] == 4) {
				for (i = 0; !e && i < x[2]; i++) {
					e |= ((nat->nat_ndstaddr & x[i + 4]) ==
					      x[i + 3]);
				}
			}
			break;

		case IPF_EXP_IP_ADDR :
			for (i = 0; !e && i < x[2]; i++) {
				if (nat->nat_v[0] == 4) {
					e |= ((nat->nat_osrcaddr & x[i + 4]) ==
					      x[i + 3]);
				}
				if (nat->nat_v[1] == 4) {
					e |= ((nat->nat_nsrcaddr & x[i + 4]) ==
					      x[i + 3]);
				}
				if (nat->nat_v[0] == 4) {
					e |= ((nat->nat_odstaddr & x[i + 4]) ==
					      x[i + 3]);
				}
				if (nat->nat_v[1] == 4) {
					e |= ((nat->nat_ndstaddr & x[i + 4]) ==
					      x[i + 3]);
				}
			}
			break;

#ifdef USE_INET6
		case IPF_EXP_IP6_SRCADDR :
			if (nat->nat_v[0] == 6) {
				for (i = 0; !e && i < x[3]; i++) {
					e |= IP6_MASKEQ(&nat->nat_osrc6,
							x + i + 7, x + i + 3);
				}
			}
			if (nat->nat_v[1] == 6) {
				for (i = 0; !e && i < x[3]; i++) {
					e |= IP6_MASKEQ(&nat->nat_nsrc6,
							x + i + 7, x + i + 3);
				}
			}
			break;

		case IPF_EXP_IP6_DSTADDR :
			if (nat->nat_v[0] == 6) {
				for (i = 0; !e && i < x[3]; i++) {
					e |= IP6_MASKEQ(&nat->nat_odst6,
							x + i + 7,
							x + i + 3);
				}
			}
			if (nat->nat_v[1] == 6) {
				for (i = 0; !e && i < x[3]; i++) {
					e |= IP6_MASKEQ(&nat->nat_ndst6,
							x + i + 7,
							x + i + 3);
				}
			}
			break;

		case IPF_EXP_IP6_ADDR :
			for (i = 0; !e && i < x[3]; i++) {
				if (nat->nat_v[0] == 6) {
					e |= IP6_MASKEQ(&nat->nat_osrc6,
							x + i + 7,
							x + i + 3);
				}
				if (nat->nat_v[0] == 6) {
					e |= IP6_MASKEQ(&nat->nat_odst6,
							x + i + 7,
							x + i + 3);
				}
				if (nat->nat_v[1] == 6) {
					e |= IP6_MASKEQ(&nat->nat_nsrc6,
							x + i + 7,
							x + i + 3);
				}
				if (nat->nat_v[1] == 6) {
					e |= IP6_MASKEQ(&nat->nat_ndst6,
							x + i + 7,
							x + i + 3);
				}
			}
			break;
#endif

		case IPF_EXP_UDP_PORT :
		case IPF_EXP_TCP_PORT :
			for (i = 0; !e && i < x[2]; i++) {
				e |= (nat->nat_nsport == x[i + 3]) ||
				     (nat->nat_ndport == x[i + 3]);
			}
			break;

		case IPF_EXP_UDP_SPORT :
		case IPF_EXP_TCP_SPORT :
			for (i = 0; !e && i < x[2]; i++) {
				e |= (nat->nat_nsport == x[i + 3]);
			}
			break;

		case IPF_EXP_UDP_DPORT :
		case IPF_EXP_TCP_DPORT :
			for (i = 0; !e && i < x[2]; i++) {
				e |= (nat->nat_ndport == x[i + 3]);
			}
			break;

		case IPF_EXP_TCP_STATE :
			for (i = 0; !e && i < x[2]; i++) {
				e |= (nat->nat_tcpstate[0] == x[i + 3]) ||
				     (nat->nat_tcpstate[1] == x[i + 3]);
			}
			break;

		case IPF_EXP_IDLE_GT :
			e |= (ticks - nat->nat_touched > x[3]);
			break;
		}
		e ^= x[1];

		if (!e)
			break;
	}

	return e;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_gettable                                            */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              data(I)  - pointer to ioctl data                            */
/*                                                                          */
/* This function handles ioctl requests for tables of nat information.      */
/* At present the only table it deals with is the hash bucket statistics.   */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_gettable(softc, softn, data)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	char *data;
{
	ipftable_t table;
	int error;

	error = ipf_inobj(softc, data, NULL, &table, IPFOBJ_GTABLE);
	if (error != 0)
		return error;

	switch (table.ita_type)
	{
	case IPFTABLE_BUCKETS_NATIN :
		error = COPYOUT(softn->ipf_nat_stats.ns_side[0].ns_bucketlen,
				table.ita_table,
				softn->ipf_nat_table_sz * sizeof(u_int));
		break;

	case IPFTABLE_BUCKETS_NATOUT :
		error = COPYOUT(softn->ipf_nat_stats.ns_side[1].ns_bucketlen,
				table.ita_table,
				softn->ipf_nat_table_sz * sizeof(u_int));
		break;

	default :
		IPFERROR(60058);
		return EINVAL;
	}

	if (error != 0) {
		IPFERROR(60059);
		error = EFAULT;
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_settimeout                                          */
/* Returns:     int  - 0 = success, else failure			    */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              t(I) - pointer to tunable                                   */
/*              p(I) - pointer to new tuning data                           */
/*                                                                          */
/* Apply the timeout change to the NAT timeout queues.                      */
/* ------------------------------------------------------------------------ */
int
ipf_nat_settimeout(softc, t, p)
	struct ipf_main_softc_s *softc;
	ipftuneable_t *t;
	ipftuneval_t *p;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;

	if (!strncmp(t->ipft_name, "tcp_", 4))
		return ipf_settimeout_tcp(t, p, softn->ipf_nat_tcptq);

	if (!strcmp(t->ipft_name, "udp_timeout")) {
		ipf_apply_timeout(&softn->ipf_nat_udptq, p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "udp_ack_timeout")) {
		ipf_apply_timeout(&softn->ipf_nat_udpacktq, p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "icmp_timeout")) {
		ipf_apply_timeout(&softn->ipf_nat_icmptq, p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "icmp_ack_timeout")) {
		ipf_apply_timeout(&softn->ipf_nat_icmpacktq, p->ipftu_int);
	} else if (!strcmp(t->ipft_name, "ip_timeout")) {
		ipf_apply_timeout(&softn->ipf_nat_iptq, p->ipftu_int);
	} else {
		IPFERROR(60062);
		return ESRCH;
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_rehash                                              */
/* Returns:     int  - 0 = success, else failure			    */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              t(I) - pointer to tunable                                   */
/*              p(I) - pointer to new tuning data                           */
/*                                                                          */
/* To change the size of the basic NAT table, we need to first allocate the */
/* new tables (lest it fails and we've got nowhere to store all of the NAT  */
/* sessions currently active) and then walk through the entire list and     */
/* insert them into the table.  There are two tables here: an inbound one   */
/* and an outbound one.  Each NAT entry goes into each table once.          */
/* ------------------------------------------------------------------------ */
int
ipf_nat_rehash(softc, t, p)
	ipf_main_softc_t *softc;
	ipftuneable_t *t;
	ipftuneval_t *p;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	nat_t **newtab[2], *nat, **natp;
	u_int *bucketlens[2];
	u_int maxbucket;
	u_int newsize;
	int error;
	u_int hv;
	int i;

	newsize = p->ipftu_int;
	/*
	 * In case there is nothing to do...
	 */
	if (newsize == softn->ipf_nat_table_sz)
		return 0;

	newtab[0] = NULL;
	newtab[1] = NULL;
	bucketlens[0] = NULL;
	bucketlens[1] = NULL;
	/*
	 * 4 tables depend on the NAT table size: the inbound looking table,
	 * the outbound lookup table and the hash chain length for each.
	 */
	KMALLOCS(newtab[0], nat_t **, newsize * sizeof(nat_t *));
	if (newtab[0] == NULL) {
		error = 60063;
		goto badrehash;
	}

	KMALLOCS(newtab[1], nat_t **, newsize * sizeof(nat_t *));
	if (newtab[1] == NULL) {
		error = 60064;
		goto badrehash;
	}

	KMALLOCS(bucketlens[0], u_int *, newsize * sizeof(u_int));
	if (bucketlens[0] == NULL) {
		error = 60065;
		goto badrehash;
	}

	KMALLOCS(bucketlens[1], u_int *, newsize * sizeof(u_int));
	if (bucketlens[1] == NULL) {
		error = 60066;
		goto badrehash;
	}

	/*
	 * Recalculate the maximum length based on the new size.
	 */
	for (maxbucket = 0, i = newsize; i > 0; i >>= 1)
		maxbucket++;
	maxbucket *= 2;

	bzero((char *)newtab[0], newsize * sizeof(nat_t *));
	bzero((char *)newtab[1], newsize * sizeof(nat_t *));
	bzero((char *)bucketlens[0], newsize * sizeof(u_int));
	bzero((char *)bucketlens[1], newsize * sizeof(u_int));

	WRITE_ENTER(&softc->ipf_nat);

	if (softn->ipf_nat_table[0] != NULL) {
		KFREES(softn->ipf_nat_table[0],
		       softn->ipf_nat_table_sz *
		       sizeof(*softn->ipf_nat_table[0]));
	}
	softn->ipf_nat_table[0] = newtab[0];

	if (softn->ipf_nat_table[1] != NULL) {
		KFREES(softn->ipf_nat_table[1],
		       softn->ipf_nat_table_sz *
		       sizeof(*softn->ipf_nat_table[1]));
	}
	softn->ipf_nat_table[1] = newtab[1];

	if (softn->ipf_nat_stats.ns_side[0].ns_bucketlen != NULL) {
		KFREES(softn->ipf_nat_stats.ns_side[0].ns_bucketlen,
		       softn->ipf_nat_table_sz * sizeof(u_int));
	}
	softn->ipf_nat_stats.ns_side[0].ns_bucketlen = bucketlens[0];

	if (softn->ipf_nat_stats.ns_side[1].ns_bucketlen != NULL) {
		KFREES(softn->ipf_nat_stats.ns_side[1].ns_bucketlen,
		       softn->ipf_nat_table_sz * sizeof(u_int));
	}
	softn->ipf_nat_stats.ns_side[1].ns_bucketlen = bucketlens[1];

#ifdef USE_INET6
	if (softn->ipf_nat_stats.ns_side6[0].ns_bucketlen != NULL) {
		KFREES(softn->ipf_nat_stats.ns_side6[0].ns_bucketlen,
		       softn->ipf_nat_table_sz * sizeof(u_int));
	}
	softn->ipf_nat_stats.ns_side6[0].ns_bucketlen = bucketlens[0];

	if (softn->ipf_nat_stats.ns_side6[1].ns_bucketlen != NULL) {
		KFREES(softn->ipf_nat_stats.ns_side6[1].ns_bucketlen,
		       softn->ipf_nat_table_sz * sizeof(u_int));
	}
	softn->ipf_nat_stats.ns_side6[1].ns_bucketlen = bucketlens[1];
#endif

	softn->ipf_nat_maxbucket = maxbucket;
	softn->ipf_nat_table_sz = newsize;
	/*
	 * Walk through the entire list of NAT table entries and put them
	 * in the new NAT table, somewhere.  Because we have a new table,
	 * we need to restart the counter of how many chains are in use.
	 */
	softn->ipf_nat_stats.ns_side[0].ns_inuse = 0;
	softn->ipf_nat_stats.ns_side[1].ns_inuse = 0;
#ifdef USE_INET6
	softn->ipf_nat_stats.ns_side6[0].ns_inuse = 0;
	softn->ipf_nat_stats.ns_side6[1].ns_inuse = 0;
#endif

	for (nat = softn->ipf_nat_instances; nat != NULL; nat = nat->nat_next) {
		nat->nat_hnext[0] = NULL;
		nat->nat_phnext[0] = NULL;
		hv = nat->nat_hv[0] % softn->ipf_nat_table_sz;

		natp = &softn->ipf_nat_table[0][hv];
		if (*natp) {
			(*natp)->nat_phnext[0] = &nat->nat_hnext[0];
		} else {
			NBUMPSIDE(0, ns_inuse);
		}
		nat->nat_phnext[0] = natp;
		nat->nat_hnext[0] = *natp;
		*natp = nat;
		NBUMPSIDE(0, ns_bucketlen[hv]);

		nat->nat_hnext[1] = NULL;
		nat->nat_phnext[1] = NULL;
		hv = nat->nat_hv[1] % softn->ipf_nat_table_sz;

		natp = &softn->ipf_nat_table[1][hv];
		if (*natp) {
			(*natp)->nat_phnext[1] = &nat->nat_hnext[1];
		} else {
			NBUMPSIDE(1, ns_inuse);
		}
		nat->nat_phnext[1] = natp;
		nat->nat_hnext[1] = *natp;
		*natp = nat;
		NBUMPSIDE(1, ns_bucketlen[hv]);
	}
	RWLOCK_EXIT(&softc->ipf_nat);

	return 0;

badrehash:
	if (bucketlens[1] != NULL) {
		KFREES(bucketlens[0], newsize * sizeof(u_int));
	}
	if (bucketlens[0] != NULL) {
		KFREES(bucketlens[0], newsize * sizeof(u_int));
	}
	if (newtab[0] != NULL) {
		KFREES(newtab[0], newsize * sizeof(nat_t *));
	}
	if (newtab[1] != NULL) {
		KFREES(newtab[1], newsize * sizeof(nat_t *));
	}
	IPFERROR(error);
	return ENOMEM;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_rehash_rules                                        */
/* Returns:     int  - 0 = success, else failure			    */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              t(I) - pointer to tunable                                   */
/*              p(I) - pointer to new tuning data                           */
/*                                                                          */
/* All of the NAT rules hang off of a hash table that is searched with a    */
/* hash on address after the netmask is applied.  There is a different table*/
/* for both inbound rules (rdr) and outbound (map.)  The resizing will only */
/* affect one of these two tables.                                          */
/* ------------------------------------------------------------------------ */
int
ipf_nat_rehash_rules(softc, t, p)
	ipf_main_softc_t *softc;
	ipftuneable_t *t;
	ipftuneval_t *p;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	ipnat_t **newtab, *np, ***old, **npp;
	u_int newsize;
	u_int mask;
	u_int hv;

	newsize = p->ipftu_int;
	/*
	 * In case there is nothing to do...
	 */
	if (newsize == *t->ipft_pint)
		return 0;

	/*
	 * All inbound rules have the NAT_REDIRECT bit set in in_redir and
	 * all outbound rules have either NAT_MAP or MAT_MAPBLK set.
	 * This if statement allows for some more generic code to be below,
	 * rather than two huge gobs of code that almost do the same thing.
	 */
	if (t->ipft_pint == &softn->ipf_nat_rdrrules_sz) {
		old = &softn->ipf_nat_rdr_rules;
		mask = NAT_REDIRECT;
	} else {
		old = &softn->ipf_nat_map_rules;
		mask = NAT_MAP|NAT_MAPBLK;
	}

	KMALLOCS(newtab, ipnat_t **, newsize * sizeof(ipnat_t *));
	if (newtab == NULL) {
		IPFERROR(60067);
		return ENOMEM;
	}

	bzero((char *)newtab, newsize * sizeof(ipnat_t *));

	WRITE_ENTER(&softc->ipf_nat);

	if (*old != NULL) {
		KFREES(*old, *t->ipft_pint * sizeof(ipnat_t **));
	}
	*old = newtab;
	*t->ipft_pint = newsize;

	for (np = softn->ipf_nat_list; np != NULL; np = np->in_next) {
		if ((np->in_redir & mask) == 0)
			continue;

		if (np->in_redir & NAT_REDIRECT) {
			np->in_rnext = NULL;
			hv = np->in_hv[0] % newsize;
			for (npp = newtab + hv; *npp != NULL; )
				npp = &(*npp)->in_rnext;
			np->in_prnext = npp;
			*npp = np;
		}
		if (np->in_redir & NAT_MAP) {
			np->in_mnext = NULL;
			hv = np->in_hv[1] % newsize;
			for (npp = newtab + hv; *npp != NULL; )
				npp = &(*npp)->in_mnext;
			np->in_pmnext = npp;
			*npp = np;
		}

	}
	RWLOCK_EXIT(&softc->ipf_nat);

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_hostmap_rehash                                      */
/* Returns:     int  - 0 = success, else failure			    */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              t(I) - pointer to tunable                                   */
/*              p(I) - pointer to new tuning data                           */
/*                                                                          */
/* Allocate and populate a new hash table that will contain a reference to  */
/* all of the active IP# translations currently in place.                   */
/* ------------------------------------------------------------------------ */
int
ipf_nat_hostmap_rehash(softc, t, p)
	ipf_main_softc_t *softc;
	ipftuneable_t *t;
	ipftuneval_t *p;
{
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	hostmap_t *hm, **newtab;
	u_int newsize;
	u_int hv;

	newsize = p->ipftu_int;
	/*
	 * In case there is nothing to do...
	 */
	if (newsize == *t->ipft_pint)
		return 0;

	KMALLOCS(newtab, hostmap_t **, newsize * sizeof(hostmap_t *));
	if (newtab == NULL) {
		IPFERROR(60068);
		return ENOMEM;
	}

	bzero((char *)newtab, newsize * sizeof(hostmap_t *));

	WRITE_ENTER(&softc->ipf_nat);
	if (softn->ipf_hm_maptable != NULL) {
		KFREES(softn->ipf_hm_maptable,
		       softn->ipf_nat_hostmap_sz * sizeof(hostmap_t *));
	}
	softn->ipf_hm_maptable = newtab;
	softn->ipf_nat_hostmap_sz = newsize;

	for (hm = softn->ipf_hm_maplist; hm != NULL; hm = hm->hm_next) {
		hv = hm->hm_hv % softn->ipf_nat_hostmap_sz;
		hm->hm_hnext = softn->ipf_hm_maptable[hv];
		hm->hm_phnext = softn->ipf_hm_maptable + hv;
		if (softn->ipf_hm_maptable[hv] != NULL)
			softn->ipf_hm_maptable[hv]->hm_phnext = &hm->hm_hnext;
		softn->ipf_hm_maptable[hv] = hm;
	}
	RWLOCK_EXIT(&softc->ipf_nat);

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_add_tq                                              */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* ------------------------------------------------------------------------ */
ipftq_t *
ipf_nat_add_tq(softc, ttl)
	ipf_main_softc_t *softc;
	int ttl;
{
	ipf_nat_softc_t *softs = softc->ipf_nat_soft;

	return ipf_addtimeoutqueue(softc, &softs->ipf_nat_utqe, ttl);
}

/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_uncreate                                            */
/* Returns:     Nil                                                         */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* This function is used to remove a NAT entry from the NAT table when we   */
/* decide that the create was actually in error. It is thus assumed that    */
/* fin_flx will have both FI_NATED and FI_NATNEW set. Because we're dealing */
/* with the translated packet (not the original), we have to reverse the    */
/* lookup. Although doing the lookup is expensive (relatively speaking), it */
/* is not anticipated that this will be a frequent occurance for normal     */
/* traffic patterns.                                                        */
/* ------------------------------------------------------------------------ */
void
ipf_nat_uncreate(fin)
	fr_info_t *fin;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	int nflags;
	nat_t *nat;

	switch (fin->fin_p)
	{
	case IPPROTO_TCP :
		nflags = IPN_TCP;
		break;
	case IPPROTO_UDP :
		nflags = IPN_UDP;
		break;
	default :
		nflags = 0;
		break;
	}

	WRITE_ENTER(&softc->ipf_nat);

	if (fin->fin_out == 0) {
		nat = ipf_nat_outlookup(fin, nflags, (u_int)fin->fin_p,
					fin->fin_dst, fin->fin_src);
	} else {
		nat = ipf_nat_inlookup(fin, nflags, (u_int)fin->fin_p,
				       fin->fin_src, fin->fin_dst);
	}

	if (nat != NULL) {
		NBUMPSIDE(fin->fin_out, ns_uncreate[0]);
		ipf_nat_delete(softc, nat, NL_DESTROY);
	} else {
		NBUMPSIDE(fin->fin_out, ns_uncreate[1]);
	}

	RWLOCK_EXIT(&softc->ipf_nat);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_cmp_rules                                           */
/* Returns:     int   - 0 == success, else rules do not match.              */
/* Parameters:  n1(I) - first rule to compare                               */
/*              n2(I) - first rule to compare                               */
/*                                                                          */
/* Compare two rules using pointers to each rule. A straight bcmp will not  */
/* work as some fields (such as in_dst, in_pkts) actually do change once    */
/* the rule has been loaded into the kernel. Whilst this function returns   */
/* various non-zero returns, they're strictly to aid in debugging. Use of   */
/* this function should simply care if the result is zero or not.           */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_cmp_rules(n1, n2)
	ipnat_t *n1, *n2;
{
	if (n1->in_size != n2->in_size)
		return 1;

	if (bcmp((char *)&n1->in_v, (char *)&n2->in_v,
		 offsetof(ipnat_t, in_ndst) - offsetof(ipnat_t, in_v)) != 0)
		return 2;

	if (bcmp((char *)&n1->in_tuc, (char *)&n2->in_tuc,
		 n1->in_size - offsetof(ipnat_t, in_tuc)) != 0)
		return 3;
	if (n1->in_ndst.na_atype != n2->in_ndst.na_atype)
		return 5;
	if (n1->in_ndst.na_function != n2->in_ndst.na_function)
		return 6;
	if (bcmp((char *)&n1->in_ndst.na_addr, (char *)&n2->in_ndst.na_addr,
		 sizeof(n1->in_ndst.na_addr)))
		return 7;
	if (n1->in_nsrc.na_atype != n2->in_nsrc.na_atype)
		return 8;
	if (n1->in_nsrc.na_function != n2->in_nsrc.na_function)
		return 9;
	if (bcmp((char *)&n1->in_nsrc.na_addr, (char *)&n2->in_nsrc.na_addr,
		 sizeof(n1->in_nsrc.na_addr)))
		return 10;
	if (n1->in_odst.na_atype != n2->in_odst.na_atype)
		return 11;
	if (n1->in_odst.na_function != n2->in_odst.na_function)
		return 12;
	if (bcmp((char *)&n1->in_odst.na_addr, (char *)&n2->in_odst.na_addr,
		 sizeof(n1->in_odst.na_addr)))
		return 13;
	if (n1->in_osrc.na_atype != n2->in_osrc.na_atype)
		return 14;
	if (n1->in_osrc.na_function != n2->in_osrc.na_function)
		return 15;
	if (bcmp((char *)&n1->in_osrc.na_addr, (char *)&n2->in_osrc.na_addr,
		 sizeof(n1->in_osrc.na_addr)))
		return 16;
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_rule_init                                           */
/* Returns:     int   - 0 == success, else rules do not match.              */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              n(I)     - first rule to compare                            */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_nat_rule_init(softc, softn, n)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	ipnat_t *n;
{
	int error = 0;

	if ((n->in_flags & IPN_SIPRANGE) != 0)
		n->in_nsrcatype = FRI_RANGE;

	if ((n->in_flags & IPN_DIPRANGE) != 0)
		n->in_ndstatype = FRI_RANGE;

	if ((n->in_flags & IPN_SPLIT) != 0)
		n->in_ndstatype = FRI_SPLIT;

	if ((n->in_redir & (NAT_MAP|NAT_REWRITE|NAT_DIVERTUDP)) != 0)
		n->in_spnext = n->in_spmin;

	if ((n->in_redir & (NAT_REWRITE|NAT_DIVERTUDP)) != 0) {
		n->in_dpnext = n->in_dpmin;
	} else if (n->in_redir == NAT_REDIRECT) {
		n->in_dpnext = n->in_dpmin;
	}

	n->in_stepnext = 0;

	switch (n->in_v[0])
	{
	case 4 :
		error = ipf_nat_ruleaddrinit(softc, softn, n);
		if (error != 0)
			return error;
		break;
#ifdef USE_INET6
	case 6 :
		error = ipf_nat6_ruleaddrinit(softc, softn, n);
		if (error != 0)
			return error;
		break;
#endif
	default :
		break;
	}

	if (n->in_redir == (NAT_DIVERTUDP|NAT_MAP)) {
		/*
		 * Prerecord whether or not the destination of the divert
		 * is local or not to the interface the packet is going
		 * to be sent out.
		 */
		n->in_dlocal = ipf_deliverlocal(softc, n->in_v[1],
						n->in_ifps[1], &n->in_ndstip6);
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat_rule_fini                                           */
/* Returns:     int   - 0 == success, else rules do not match.              */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              n(I)     - rule to work on                                  */
/*                                                                          */
/* This function is used to release any objects that were referenced during */
/* the rule initialisation. This is useful both when free'ing the rule and  */
/* when handling ioctls that need to initialise these fields but not        */
/* actually use them after the ioctl processing has finished.               */
/* ------------------------------------------------------------------------ */
static void
ipf_nat_rule_fini(softc, n)
	ipf_main_softc_t *softc;
	ipnat_t *n;
{
	if (n->in_odst.na_atype == FRI_LOOKUP && n->in_odst.na_ptr != NULL)
		ipf_lookup_deref(softc, n->in_odst.na_type, n->in_odst.na_ptr);

	if (n->in_osrc.na_atype == FRI_LOOKUP && n->in_osrc.na_ptr != NULL)
		ipf_lookup_deref(softc, n->in_osrc.na_type, n->in_osrc.na_ptr);

	if (n->in_ndst.na_atype == FRI_LOOKUP && n->in_ndst.na_ptr != NULL)
		ipf_lookup_deref(softc, n->in_ndst.na_type, n->in_ndst.na_ptr);

	if (n->in_nsrc.na_atype == FRI_LOOKUP && n->in_nsrc.na_ptr != NULL)
		ipf_lookup_deref(softc, n->in_nsrc.na_type, n->in_nsrc.na_ptr);

	if (n->in_divmp != NULL)
		FREE_MB_T(n->in_divmp);
}
