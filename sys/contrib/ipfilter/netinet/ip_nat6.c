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
#if defined(_KERNEL) && defined(__NetBSD_Version__) && \
    (__NetBSD_Version__ >= 399002000)
# include <sys/kauth.h>
#endif
#if !defined(_KERNEL)
# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# define _KERNEL
# ifdef ipf_nat6__OpenBSD__
struct file;
# endif
# include <sys/uio.h>
# undef _KERNEL
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
# ifdef _KERNEL
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
#include <net/route.h>
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
static const char rcsid[] = "@(#)$Id: ip_nat6.c,v 1.22.2.20 2012/07/22 08:04:23 darren_r Exp $";
#endif

#ifdef USE_INET6
static struct hostmap *ipf_nat6_hostmap __P((ipf_nat_softc_t *, ipnat_t *,
					     i6addr_t *, i6addr_t *,
					     i6addr_t *, u_32_t));
static int ipf_nat6_match __P((fr_info_t *, ipnat_t *));
static void ipf_nat6_tabmove __P((ipf_nat_softc_t *, nat_t *));
static int ipf_nat6_decap __P((fr_info_t *, nat_t *));
static int ipf_nat6_nextaddr __P((fr_info_t *, nat_addr_t *, i6addr_t *,
				  i6addr_t *));
static int ipf_nat6_icmpquerytype __P((int));
static int ipf_nat6_out __P((fr_info_t *, nat_t *, int, u_32_t));
static int ipf_nat6_in __P((fr_info_t *, nat_t *, int, u_32_t));
static int ipf_nat6_builddivertmp __P((ipf_nat_softc_t *, ipnat_t *));
static int ipf_nat6_nextaddrinit __P((ipf_main_softc_t *, char *,
				      nat_addr_t *, int, void *));
static int ipf_nat6_insert __P((ipf_main_softc_t *, ipf_nat_softc_t *,
				nat_t *));


#define	NINCLSIDE6(y,x)	ATOMIC_INCL(softn->ipf_nat_stats.ns_side6[y].x)
#define	NBUMPSIDE(y,x)	softn->ipf_nat_stats.ns_side[y].x++
#define	NBUMPSIDE6(y,x)	softn->ipf_nat_stats.ns_side6[y].x++
#define	NBUMPSIDE6D(y,x) \
			do { \
				softn->ipf_nat_stats.ns_side6[y].x++; \
				DT(x); \
			} while (0)
#define	NBUMPSIDE6DX(y,x,z) \
			do { \
				softn->ipf_nat_stats.ns_side6[y].x++; \
				DT(z); \
			} while (0)


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_ruleaddrinit                                       */
/* Returns:     int   - 0 == success, else failure                          */
/* Parameters:  in(I) - NAT rule that requires address fields to be init'd  */
/*                                                                          */
/* For each of the source/destination address fields in a NAT rule, call    */
/* ipf_nat6_nextaddrinit() to prepare the structure for active duty.  Other */
/* IPv6 specific actions can also be taken care of here.                    */
/* ------------------------------------------------------------------------ */
int
ipf_nat6_ruleaddrinit(softc, softn, n)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	ipnat_t *n;
{
	int idx, error;

	if (n->in_redir == NAT_BIMAP) {
		n->in_ndstip6 = n->in_osrcip6;
		n->in_ndstmsk6 = n->in_osrcmsk6;
		n->in_odstip6 = n->in_nsrcip6;
		n->in_odstmsk6 = n->in_nsrcmsk6;

	}

	if (n->in_redir & NAT_REDIRECT)
		idx = 1;
	else
		idx = 0;
	/*
	 * Initialise all of the address fields.
	 */
	error = ipf_nat6_nextaddrinit(softc, n->in_names, &n->in_osrc, 1,
				      n->in_ifps[idx]);
	if (error != 0)
		return error;

	error = ipf_nat6_nextaddrinit(softc, n->in_names, &n->in_odst, 1,
				      n->in_ifps[idx]);
	if (error != 0)
		return error;

	error = ipf_nat6_nextaddrinit(softc, n->in_names, &n->in_nsrc, 1,
				      n->in_ifps[idx]);
	if (error != 0)
		return error;

	error = ipf_nat6_nextaddrinit(softc, n->in_names, &n->in_ndst, 1,
				      n->in_ifps[idx]);
	if (error != 0)
		return error;

	if (n->in_redir & NAT_DIVERTUDP)
		ipf_nat6_builddivertmp(softn, n);
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_addrdr                                             */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to add                           */
/*                                                                          */
/* Adds a redirect rule to the hash table of redirect rules and the list of */
/* loaded NAT rules.  Updates the bitmask indicating which netmasks are in  */
/* use by redirect rules.                                                   */
/* ------------------------------------------------------------------------ */
void
ipf_nat6_addrdr(softn, n)
	ipf_nat_softc_t *softn;
	ipnat_t *n;
{
	i6addr_t *mask;
	ipnat_t **np;
	i6addr_t j;
	u_int hv;
	int k;

	if ((n->in_redir & NAT_BIMAP) == NAT_BIMAP) {
		k = count6bits(n->in_nsrcmsk6.i6);
		mask = &n->in_nsrcmsk6;
		IP6_AND(&n->in_odstip6, &n->in_odstmsk6, &j);
		hv = NAT_HASH_FN6(&j, 0, softn->ipf_nat_rdrrules_sz);

	} else if (n->in_odstatype == FRI_NORMAL) {
		k = count6bits(n->in_odstmsk6.i6);
		mask = &n->in_odstmsk6;
		IP6_AND(&n->in_odstip6, &n->in_odstmsk6, &j);
		hv = NAT_HASH_FN6(&j, 0, softn->ipf_nat_rdrrules_sz);
	} else {
		k = 0;
		hv = 0;
		mask = NULL;
	}
	ipf_inet6_mask_add(k, mask, &softn->ipf_nat6_rdr_mask);

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
/* Function:    ipf_nat6_addmap                                             */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to add                           */
/*                                                                          */
/* Adds a NAT map rule to the hash table of rules and the list of  loaded   */
/* NAT rules.  Updates the bitmask indicating which netmasks are in use by  */
/* redirect rules.                                                          */
/* ------------------------------------------------------------------------ */
void
ipf_nat6_addmap(softn, n)
	ipf_nat_softc_t *softn;
	ipnat_t *n;
{
	i6addr_t *mask;
	ipnat_t **np;
	i6addr_t j;
	u_int hv;
	int k;

	if (n->in_osrcatype == FRI_NORMAL) {
		k = count6bits(n->in_osrcmsk6.i6);
		mask = &n->in_osrcmsk6;
		IP6_AND(&n->in_osrcip6, &n->in_osrcmsk6, &j);
		hv = NAT_HASH_FN6(&j, 0, softn->ipf_nat_maprules_sz);
	} else {
		k = 0;
		hv = 0;
		mask = NULL;
	}
	ipf_inet6_mask_add(k, mask, &softn->ipf_nat6_map_mask);

	np = softn->ipf_nat_map_rules + hv;
	while (*np != NULL)
		np = &(*np)->in_mnext;
	n->in_mnext = NULL;
	n->in_pmnext = np;
	n->in_hv[1] = hv;
	n->in_use++;
	*np = n;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_del_rdr                                             */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to delete                        */
/*                                                                          */
/* Removes a NAT rdr rule from the hash table of NAT rdr rules.             */
/* ------------------------------------------------------------------------ */
void
ipf_nat6_delrdr(softn, n)
	ipf_nat_softc_t *softn;
	ipnat_t *n;
{
	i6addr_t *mask;
	int k;

	if ((n->in_redir & NAT_BIMAP) == NAT_BIMAP) {
		k = count6bits(n->in_nsrcmsk6.i6);
		mask = &n->in_nsrcmsk6;
	} else if (n->in_odstatype == FRI_NORMAL) {
		k = count6bits(n->in_odstmsk6.i6);
		mask = &n->in_odstmsk6;
	} else {
		k = 0;
		mask = NULL;
	}
	ipf_inet6_mask_del(k, mask, &softn->ipf_nat6_rdr_mask);

	if (n->in_rnext != NULL)
		n->in_rnext->in_prnext = n->in_prnext;
	*n->in_prnext = n->in_rnext;
	n->in_use--;
}
                                        
                       
/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_delmap                                             */
/* Returns:     Nil                                                         */
/* Parameters:  n(I) - pointer to NAT rule to delete                        */
/*                                                                          */
/* Removes a NAT map rule from the hash table of NAT map rules.             */
/* ------------------------------------------------------------------------ */
void
ipf_nat6_delmap(softn, n)
	ipf_nat_softc_t *softn;
	ipnat_t *n;
{
	i6addr_t *mask;
	int k;

	if (n->in_osrcatype == FRI_NORMAL) {
		k = count6bits(n->in_osrcmsk6.i6);
		mask = &n->in_osrcmsk6;
	} else {
		k = 0;
		mask = NULL;
	}
	ipf_inet6_mask_del(k, mask, &softn->ipf_nat6_map_mask);

	if (n->in_mnext != NULL)
		n->in_mnext->in_pmnext = n->in_pmnext;
	*n->in_pmnext = n->in_mnext;
	n->in_use--;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_hostmap                                            */
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
ipf_nat6_hostmap(softn, np, src, dst, map, port)
	ipf_nat_softc_t *softn;
	ipnat_t *np;
	i6addr_t *src, *dst, *map;
	u_32_t port;
{
	hostmap_t *hm;
	u_int hv;

	hv = (src->i6[3] ^ dst->i6[3]);
	hv += (src->i6[2] ^ dst->i6[2]);
	hv += (src->i6[1] ^ dst->i6[1]);
	hv += (src->i6[0] ^ dst->i6[0]);
	hv += src->i6[3];
	hv += src->i6[2];
	hv += src->i6[1];
	hv += src->i6[0];
	hv += dst->i6[3];
	hv += dst->i6[2];
	hv += dst->i6[1];
	hv += dst->i6[0];
	hv %= HOSTMAP_SIZE;
	for (hm = softn->ipf_hm_maptable[hv]; hm; hm = hm->hm_next)
		if (IP6_EQ(&hm->hm_osrc6, src) &&
		    IP6_EQ(&hm->hm_odst6, dst) &&
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
		hm->hm_osrcip6 = *src;
		hm->hm_odstip6 = *dst;
		hm->hm_nsrcip6 = *map;
		hm->hm_ndstip6.i6[0] = 0;
		hm->hm_ndstip6.i6[1] = 0;
		hm->hm_ndstip6.i6[2] = 0;
		hm->hm_ndstip6.i6[3] = 0;
		hm->hm_ref = 1;
		hm->hm_port = port;
		hm->hm_hv = hv;
		hm->hm_v = 6;
		softn->ipf_nat_stats.ns_hm_new++;
	} else {
		softn->ipf_nat_stats.ns_hm_newfail++;
	}
	return hm;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_newmap                                             */
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
int
ipf_nat6_newmap(fin, nat, ni)
	fr_info_t *fin;
	nat_t *nat;
	natinfo_t *ni;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_short st_port, dport, sport, port, sp, dp;
	i6addr_t in, st_ip;
	hostmap_t *hm;
	u_32_t flags;
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
	st_ip = np->in_snip6;
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
		in = np->in_nsrc.na_nextaddr;
		if (l == 0) {
			/*
			 * Check to see if there is an existing NAT
			 * setup for this IP address pair.
			 */
			hm = ipf_nat6_hostmap(softn, np, &fin->fin_src6,
					      &fin->fin_dst6, &in, 0);
			if (hm != NULL)
				in = hm->hm_nsrcip6;
		} else if ((l == 1) && (hm != NULL)) {
			ipf_nat_hostmapdel(softc, &hm);
		}

		nat->nat_hm = hm;

		if (IP6_ISONES(&np->in_nsrcmsk6) && (np->in_spnext == 0)) {
			if (l > 0) {
				NBUMPSIDE6DX(1, ns_exhausted, ns_exhausted_1);
				return -1;
			}
		}

		if ((np->in_redir == NAT_BIMAP) &&
		    IP6_EQ(&np->in_osrcmsk6, &np->in_nsrcmsk6)) {
			i6addr_t temp;
			/*
			 * map the address block in a 1:1 fashion
			 */
			temp.i6[0] = fin->fin_src6.i6[0] &
				     ~np->in_osrcmsk6.i6[0];
			temp.i6[1] = fin->fin_src6.i6[1] &
				     ~np->in_osrcmsk6.i6[1];
			temp.i6[2] = fin->fin_src6.i6[2] &
				     ~np->in_osrcmsk6.i6[0];
			temp.i6[3] = fin->fin_src6.i6[3] &
				     ~np->in_osrcmsk6.i6[3];
			in = np->in_nsrcip6;
			IP6_MERGE(&in, &temp, &np->in_osrc);

#ifdef NEED_128BIT_MATH
		} else if (np->in_redir & NAT_MAPBLK) {
			if ((l >= np->in_ppip) || ((l > 0) &&
			     !(flags & IPN_TCPUDP))) {
				NBUMPSIDE6DX(1, ns_exhausted, ns_exhausted_2);
				return -1;
			}
			/*
			 * map-block - Calculate destination address.
			 */
			IP6_MASK(&in, &fin->fin_src6, &np->in_osrcmsk6);
			in = ntohl(in);
			inb = in;
			in.s_addr /= np->in_ippip;
			in.s_addr &= ntohl(~np->in_nsrcmsk6);
			in.s_addr += ntohl(np->in_nsrcaddr6);
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
#endif

		} else if (IP6_ISZERO(&np->in_nsrcaddr) &&
			   IP6_ISONES(&np->in_nsrcmsk)) {
			/*
			 * 0/32 - use the interface's IP address.
			 */
			if ((l > 0) ||
			    ipf_ifpaddr(softc, 6, FRI_NORMAL, fin->fin_ifp,
				       &in, NULL) == -1) {
				NBUMPSIDE6DX(1, ns_new_ifpaddr,
					     ns_new_ifpaddr_1);
				return -1;
			}

		} else if (IP6_ISZERO(&np->in_nsrcip6) &&
			   IP6_ISZERO(&np->in_nsrcmsk6)) {
			/*
			 * 0/0 - use the original source address/port.
			 */
			if (l > 0) {
				NBUMPSIDE6DX(1, ns_exhausted, ns_exhausted_3);
				return -1;
			}
			in = fin->fin_src6;

		} else if (!IP6_ISONES(&np->in_nsrcmsk6) &&
			   (np->in_spnext == 0) && ((l > 0) || (hm == NULL))) {
			IP6_INC(&np->in_snip6);
		}

		natl = NULL;

		if ((flags & IPN_TCPUDP) &&
		    ((np->in_redir & NAT_MAPBLK) == 0) &&
		    (np->in_flags & IPN_AUTOPORTMAP)) {
#ifdef NEED_128BIT_MATH
			/*
			 * "ports auto" (without map-block)
			 */
			if ((l > 0) && (l % np->in_ppip == 0)) {
				if ((l > np->in_ppip) &&
				    !IP6_ISONES(&np->in_nsrcmsk)) {
					IP6_INC(&np->in_snip6)
				}
			}
			if (np->in_ppip != 0) {
				port = ntohs(sport);
				port += (l % np->in_ppip);
				port %= np->in_ppip;
				port += np->in_ppip *
					(ntohl(fin->fin_src6) %
					 np->in_ippip);
				port += MAPBLK_MINPORT;
				port = htons(port);
			}
#endif

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
				if (!IP6_ISONES(&np->in_nsrcmsk6)) {
					IP6_INC(&np->in_snip6);
				}
			}
		}

		if (np->in_flags & IPN_SIPRANGE) {
			if (IP6_GT(&np->in_snip, &np->in_nsrcmsk))
				np->in_snip6 = np->in_nsrcip6;
		} else {
			i6addr_t a1, a2;

			a1 = np->in_snip6;
			IP6_INC(&a1);
			IP6_AND(&a1, &np->in_nsrcmsk6, &a2);

			if (!IP6_ISONES(&np->in_nsrcmsk6) &&
			    IP6_GT(&a2, &np->in_nsrcip6)) {
				IP6_ADD(&np->in_nsrcip6, 1, &np->in_snip6);
			}
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
		sp = fin->fin_data[0];
		dp = fin->fin_data[1];
		fin->fin_data[0] = fin->fin_data[1];
		fin->fin_data[1] = ntohs(port);
		natl = ipf_nat6_inlookup(fin, flags & ~(SI_WILDP|NAT_SEARCH),
					 (u_int)fin->fin_p, &fin->fin_dst6.in6,
					 &in.in6);
		fin->fin_data[0] = sp;
		fin->fin_data[1] = dp;

		/*
		 * Has the search wrapped around and come back to the
		 * start ?
		 */
		if ((natl != NULL) &&
		    (np->in_spnext != 0) && (st_port == np->in_spnext) &&
		    (!IP6_ISZERO(&np->in_snip6) &&
		     IP6_EQ(&st_ip, &np->in_snip6))) {
			NBUMPSIDE6D(1, ns_wrap);
			return -1;
		}
		l++;
	} while (natl != NULL);

	/* Setup the NAT table */
	nat->nat_osrc6 = fin->fin_src6;
	nat->nat_nsrc6 = in;
	nat->nat_odst6 = fin->fin_dst6;
	nat->nat_ndst6 = fin->fin_dst6;
	if (nat->nat_hm == NULL)
		nat->nat_hm = ipf_nat6_hostmap(softn, np, &fin->fin_src6,
					       &fin->fin_dst6,
					       &nat->nat_nsrc6, 0);

	if (flags & IPN_TCPUDP) {
		nat->nat_osport = sport;
		nat->nat_nsport = port;	/* sport */
		nat->nat_odport = dport;
		nat->nat_ndport = dport;
		((tcphdr_t *)fin->fin_dp)->th_sport = port;
	} else if (flags & IPN_ICMPQUERY) {
		nat->nat_oicmpid = fin->fin_data[1];
		((struct icmp6_hdr *)fin->fin_dp)->icmp6_id = port;
		nat->nat_nicmpid = port;
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_newrdr                                             */
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
int
ipf_nat6_newrdr(fin, nat, ni)
	fr_info_t *fin;
	nat_t *nat;
	natinfo_t *ni;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_short nport, dport, sport;
	u_short sp, dp;
	hostmap_t *hm;
	u_32_t flags;
	i6addr_t in;
	ipnat_t *np;
	nat_t *natl;
	int move;

	move = 1;
	hm = NULL;
	in.i6[0] = 0;
	in.i6[1] = 0;
	in.i6[2] = 0;
	in.i6[3] = 0;
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
		hm = ipf_nat6_hostmap(softn, NULL, &fin->fin_src6,
				      &fin->fin_dst6, &in, (u_32_t)dport);
		if (hm != NULL) {
			in = hm->hm_ndstip6;
			np = hm->hm_ipnat;
			ni->nai_np = np;
			move = 0;
		}
	}

	/*
	 * Otherwise, it's an inbound packet. Most likely, we don't
	 * want to rewrite source ports and source addresses. Instead,
	 * we want to rewrite to a fixed internal address and fixed
	 * internal port.
	 */
	if (np->in_flags & IPN_SPLIT) {
		in = np->in_dnip6;

		if ((np->in_flags & (IPN_ROUNDR|IPN_STICKY)) == IPN_STICKY) {
			hm = ipf_nat6_hostmap(softn, NULL, &fin->fin_src6,
					      &fin->fin_dst6, &in,
					      (u_32_t)dport);
			if (hm != NULL) {
				in = hm->hm_ndstip6;
				move = 0;
			}
		}

		if (hm == NULL || hm->hm_ref == 1) {
			if (IP6_EQ(&np->in_ndstip6, &in)) {
				np->in_dnip6 = np->in_ndstmsk6;
				move = 0;
			} else {
				np->in_dnip6 = np->in_ndstip6;
			}
		}

	} else if (IP6_ISZERO(&np->in_ndstaddr) &&
		   IP6_ISONES(&np->in_ndstmsk)) {
		/*
		 * 0/32 - use the interface's IP address.
		 */
		if (ipf_ifpaddr(softc, 6, FRI_NORMAL, fin->fin_ifp,
			       &in, NULL) == -1) {
			NBUMPSIDE6DX(0, ns_new_ifpaddr, ns_new_ifpaddr_2);
			return -1;
		}

	} else if (IP6_ISZERO(&np->in_ndstip6) &&
		   IP6_ISZERO(&np->in_ndstmsk6)) {
		/*
		 * 0/0 - use the original destination address/port.
		 */
		in = fin->fin_dst6;

	} else if (np->in_redir == NAT_BIMAP &&
		   IP6_EQ(&np->in_ndstmsk6, &np->in_odstmsk6)) {
		i6addr_t temp;
		/*
		 * map the address block in a 1:1 fashion
		 */
		temp.i6[0] = fin->fin_dst6.i6[0] & ~np->in_osrcmsk6.i6[0];
		temp.i6[1] = fin->fin_dst6.i6[1] & ~np->in_osrcmsk6.i6[1];
		temp.i6[2] = fin->fin_dst6.i6[2] & ~np->in_osrcmsk6.i6[0];
		temp.i6[3] = fin->fin_dst6.i6[3] & ~np->in_osrcmsk6.i6[3];
		in = np->in_ndstip6;
		IP6_MERGE(&in, &temp, &np->in_ndstmsk6);
	} else {
		in = np->in_ndstip6;
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
	if (IP6_ISZERO(&in)) {
		if (nport == dport) {
			NBUMPSIDE6D(0, ns_xlate_null);
			return -1;
		}
		in = fin->fin_dst6;
	}

	/*
	 * Check to see if this redirect mapping already exists and if
	 * it does, return "failure" (allowing it to be created will just
	 * cause one or both of these "connections" to stop working.)
	 */
	sp = fin->fin_data[0];
	dp = fin->fin_data[1];
	fin->fin_data[1] = fin->fin_data[0];
	fin->fin_data[0] = ntohs(nport);
	natl = ipf_nat6_outlookup(fin, flags & ~(SI_WILDP|NAT_SEARCH),
				  (u_int)fin->fin_p, &in.in6,
				  &fin->fin_src6.in6);
	fin->fin_data[0] = sp;
	fin->fin_data[1] = dp;
	if (natl != NULL) {
		NBUMPSIDE6D(0, ns_xlate_exists);
		return -1;
	}

	nat->nat_ndst6 = in;
	nat->nat_odst6 = fin->fin_dst6;
	nat->nat_nsrc6 = fin->fin_src6;
	nat->nat_osrc6 = fin->fin_src6;
	if ((nat->nat_hm == NULL) && ((np->in_flags & IPN_STICKY) != 0))
		nat->nat_hm = ipf_nat6_hostmap(softn, np, &fin->fin_src6,
					       &fin->fin_dst6, &in,
					       (u_32_t)dport);

	if (flags & IPN_TCPUDP) {
		nat->nat_odport = dport;
		nat->nat_ndport = nport;
		nat->nat_osport = sport;
		nat->nat_nsport = sport;
		((tcphdr_t *)fin->fin_dp)->th_dport = nport;
	} else if (flags & IPN_ICMPQUERY) {
		nat->nat_oicmpid = fin->fin_data[1];
		((struct icmp6_hdr *)fin->fin_dp)->icmp6_id = nport;
		nat->nat_nicmpid = nport;
	}

	return move;
}

/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_add                                                */
/* Returns:     nat6_t*      - NULL == failure to create new NAT structure, */
/*                             else pointer to new NAT structure            */
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
ipf_nat6_add(fin, np, natsave, flags, direction)
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
#if SOLARIS && defined(_KERNEL) && defined(ICK_M_CTL_MAGIC)
	qpktinfo_t *qpi = fin->fin_qpi;
#endif

	nsp = &softn->ipf_nat_stats;

	if ((nsp->ns_active * 100 / softn->ipf_nat_table_max) >
	    softn->ipf_nat_table_wm_high) {
		softn->ipf_nat_doflush = 1;
	}

	if (nsp->ns_active >= softn->ipf_nat_table_max) {
		NBUMPSIDE6(fin->fin_out, ns_table_max);
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
		NBUMPSIDE6(fin->fin_out, ns_memfail);
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
		/* The icmp6_id field is used by the sender to identify the
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
		move = ipf_nat6_newdivert(fin, nat, &ni);

	} else if (np->in_redir & NAT_REWRITE) {
		move = ipf_nat6_newrewrite(fin, nat, &ni);

	} else if (direction == NAT_OUTBOUND) {
		/*
		 * We can now arrange to call this for the same connection
		 * because ipf_nat6_new doesn't protect the code path into
		 * this function.
		 */
		natl = ipf_nat6_outlookup(fin, nflags, (u_int)fin->fin_p,
					  &fin->fin_src6.in6,
					  &fin->fin_dst6.in6);
		if (natl != NULL) {
			KFREE(nat);
			nat = natl;
			goto done;
		}

		move = ipf_nat6_newmap(fin, nat, &ni);
	} else {
		/*
		 * NAT_INBOUND is used for redirects rules
		 */
		natl = ipf_nat6_inlookup(fin, nflags, (u_int)fin->fin_p,
					 &fin->fin_src6.in6,
					 &fin->fin_dst6.in6);
		if (natl != NULL) {
			KFREE(nat);
			nat = natl;
			goto done;
		}

		move = ipf_nat6_newrdr(fin, nat, &ni);
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
			NBUMPSIDE6D(fin->fin_out, ns_appr_fail);
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

	if (ipf_nat6_finalise(fin, nat) == -1) {
		goto badnat;
	}

	np->in_use++;

	if ((move == 1) && (np->in_flags & IPN_ROUNDR)) {
		if ((np->in_redir & (NAT_REDIRECT|NAT_MAP)) == NAT_REDIRECT) {
			ipf_nat6_delrdr(softn, np);
			ipf_nat6_addrdr(softn, np);
		} else if ((np->in_redir & (NAT_REDIRECT|NAT_MAP)) == NAT_MAP) {
			ipf_nat6_delmap(softn, np);
			ipf_nat6_addmap(softn, np);
		}
	}

	if (flags & SI_WILDP)
		nsp->ns_wilds++;
	softn->ipf_nat_stats.ns_proto[nat->nat_pr[0]]++;

	goto done;
badnat:
	NBUMPSIDE6(fin->fin_out, ns_badnatnew);
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
/* Function:    ipf_nat6_finalise                                           */
/* Returns:     int - 0 == sucess, -1 == failure                            */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to NAT entry                               */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* This is the tail end of constructing a new NAT entry and is the same     */
/* for both IPv4 and IPv6.                                                  */
/* ------------------------------------------------------------------------ */
/*ARGSUSED*/
int
ipf_nat6_finalise(fin, nat)
	fr_info_t *fin;
	nat_t *nat;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_32_t sum1, sum2, sumd;
	frentry_t *fr;
	u_32_t flags;

	flags = nat->nat_flags;

	switch (fin->fin_p)
	{
	case IPPROTO_ICMPV6 :
		sum1 = LONG_SUM6(&nat->nat_osrc6);
		sum1 += ntohs(nat->nat_oicmpid);
		sum2 = LONG_SUM6(&nat->nat_nsrc6);
		sum2 += ntohs(nat->nat_nicmpid);
		CALC_SUMD(sum1, sum2, sumd);
		nat->nat_sumd[0] = (sumd & 0xffff) + (sumd >> 16);

		sum1 = LONG_SUM6(&nat->nat_odst6);
		sum2 = LONG_SUM6(&nat->nat_ndst6);
		CALC_SUMD(sum1, sum2, sumd);
		nat->nat_sumd[0] += (sumd & 0xffff) + (sumd >> 16);
		break;

	case IPPROTO_TCP :
	case IPPROTO_UDP :
		sum1 = LONG_SUM6(&nat->nat_osrc6);
		sum1 += ntohs(nat->nat_osport);
		sum2 = LONG_SUM6(&nat->nat_nsrc6);
		sum2 += ntohs(nat->nat_nsport);
		CALC_SUMD(sum1, sum2, sumd);
		nat->nat_sumd[0] = (sumd & 0xffff) + (sumd >> 16);

		sum1 = LONG_SUM6(&nat->nat_odst6);
		sum1 += ntohs(nat->nat_odport);
		sum2 = LONG_SUM6(&nat->nat_ndst6);
		sum2 += ntohs(nat->nat_ndport);
		CALC_SUMD(sum1, sum2, sumd);
		nat->nat_sumd[0] += (sumd & 0xffff) + (sumd >> 16);
		break;

	default :
		sum1 = LONG_SUM6(&nat->nat_osrc6);
		sum2 = LONG_SUM6(&nat->nat_nsrc6);
		CALC_SUMD(sum1, sum2, sumd);
		nat->nat_sumd[0] = (sumd & 0xffff) + (sumd >> 16);

		sum1 = LONG_SUM6(&nat->nat_odst6);
		sum2 = LONG_SUM6(&nat->nat_ndst6);
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
		sum1 = LONG_SUM6(&nat->nat_nsrc6);
		sum1 += LONG_SUM6(&nat->nat_ndst6);
	} else {
		sum1 = LONG_SUM6(&nat->nat_osrc6);
		sum1 += LONG_SUM6(&nat->nat_odst6);
	}
	sum1 += nat->nat_pr[1];
	nat->nat_sumd[1] = (sum1 & 0xffff) + (sum1 >> 16);

	if ((nat->nat_flags & SI_CLONE) == 0)
		nat->nat_sync = ipf_sync_new(softc, SMC_NAT, fin, nat);

	if ((nat->nat_ifps[0] != NULL) && (nat->nat_ifps[0] != (void *)-1)) {
		nat->nat_mtu[0] = GETIFMTU_6(nat->nat_ifps[0]);
	}

	if ((nat->nat_ifps[1] != NULL) && (nat->nat_ifps[1] != (void *)-1)) {
		nat->nat_mtu[1] = GETIFMTU_6(nat->nat_ifps[1]);
	}

	nat->nat_v[0] = 6;
	nat->nat_v[1] = 6;

	if (ipf_nat6_insert(softc, softn, nat) == 0) {
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

	NBUMPSIDE6D(fin->fin_out, ns_unfinalised);
	/*
	 * nat6_insert failed, so cleanup time...
	 */
	if (nat->nat_sync != NULL)
		ipf_sync_del_nat(softc->ipf_sync_soft, nat->nat_sync);
	return -1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_insert                                             */
/* Returns:     int - 0 == sucess, -1 == failure                            */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softn(I) - pointer to NAT context structure                 */
/*              nat(I) - pointer to NAT structure                           */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* Insert a NAT entry into the hash tables for searching and add it to the  */
/* list of active NAT entries.  Adjust global counters when complete.       */
/* ------------------------------------------------------------------------ */
static int
ipf_nat6_insert(softc, softn, nat)
	ipf_main_softc_t *softc;
	ipf_nat_softc_t *softn;
	nat_t *nat;
{
	u_int hv1, hv2;
	u_32_t sp, dp;
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
		hv1 = NAT_HASH_FN6(&nat->nat_osrc6, sp, 0xffffffff);
		hv1 = NAT_HASH_FN6(&nat->nat_odst6, hv1 + dp,
				   softn->ipf_nat_table_sz);

		/*
		 * TRACE nat6_osrc6, nat6_osport, nat6_odst6,
		 * nat6_odport, hv1
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
		hv2 = NAT_HASH_FN6(&nat->nat_nsrc6, sp, 0xffffffff);
		hv2 = NAT_HASH_FN6(&nat->nat_ndst6, hv2 + dp,
				   softn->ipf_nat_table_sz);
		/*
		 * TRACE nat6_nsrcaddr, nat6_nsport, nat6_ndstaddr,
		 * nat6_ndport, hv1
		 */
	} else {
		hv1 = NAT_HASH_FN6(&nat->nat_osrc6, 0, 0xffffffff);
		hv1 = NAT_HASH_FN6(&nat->nat_odst6, hv1,
				   softn->ipf_nat_table_sz);
		/* TRACE nat6_osrcip6, nat6_odstip6, hv1 */

		hv2 = NAT_HASH_FN6(&nat->nat_nsrc6, 0, 0xffffffff);
		hv2 = NAT_HASH_FN6(&nat->nat_ndst6, hv2,
				   softn->ipf_nat_table_sz);
		/* TRACE nat6_nsrcip6, nat6_ndstip6, hv2 */
	}

	nat->nat_hv[0] = hv1;
	nat->nat_hv[1] = hv2;

	MUTEX_INIT(&nat->nat_lock, "nat entry lock");

	in = nat->nat_ptr;
	nat->nat_ref = nat->nat_me ? 2 : 1;

	nat->nat_ifnames[0][LIFNAMSIZ - 1] = '\0';
	nat->nat_ifps[0] = ipf_resolvenic(softc, nat->nat_ifnames[0],
					  nat->nat_v[0]);

	if (nat->nat_ifnames[1][0] != '\0') {
		nat->nat_ifnames[1][LIFNAMSIZ - 1] = '\0';
		nat->nat_ifps[1] = ipf_resolvenic(softc, nat->nat_ifnames[1],
						  nat->nat_v[1]);
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
		nat->nat_mtu[0] = GETIFMTU_6(nat->nat_ifps[0]);
	}
	if ((nat->nat_ifps[1] != NULL) && (nat->nat_ifps[1] != (void *)-1)) {
		nat->nat_mtu[1] = GETIFMTU_6(nat->nat_ifps[1]);
	}

	return ipf_nat_hashtab_add(softc, softn, nat);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_icmperrorlookup                                    */
/* Returns:     nat6_t* - point to matching NAT structure                    */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              dir(I) - direction of packet (in/out)                       */
/*                                                                          */
/* Check if the ICMP error message is related to an existing TCP, UDP or    */
/* ICMP query nat entry.  It is assumed that the packet is already of the   */
/* the required length.                                                     */
/* ------------------------------------------------------------------------ */
nat_t *
ipf_nat6_icmperrorlookup(fin, dir)
	fr_info_t *fin;
	int dir;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	struct icmp6_hdr *icmp6, *orgicmp;
	int flags = 0, type, minlen;
	nat_stat_side_t *nside;
	tcphdr_t *tcp = NULL;
	u_short data[2];
	ip6_t *oip6;
	nat_t *nat;
	u_int p;

	minlen = 40;
	icmp6 = fin->fin_dp;
	type = icmp6->icmp6_type;
	nside = &softn->ipf_nat_stats.ns_side6[fin->fin_out];
	/*
	 * Does it at least have the return (basic) IP header ?
	 * Only a basic IP header (no options) should be with an ICMP error
	 * header.  Also, if it's not an error type, then return.
	 */
	if (!(fin->fin_flx & FI_ICMPERR)) {
		ATOMIC_INCL(nside->ns_icmp_basic);
		return NULL;
	}

	/*
	 * Check packet size
	 */
	if (fin->fin_plen < ICMP6ERR_IPICMPHLEN) {
		ATOMIC_INCL(nside->ns_icmp_size);
		return NULL;
	}
	oip6 = (ip6_t *)((char *)fin->fin_dp + 8);

	/*
	 * Is the buffer big enough for all of it ?  It's the size of the IP
	 * header claimed in the encapsulated part which is of concern.  It
	 * may be too big to be in this buffer but not so big that it's
	 * outside the ICMP packet, leading to TCP deref's causing problems.
	 * This is possible because we don't know how big oip_hl is when we
	 * do the pullup early in ipf_check() and thus can't gaurantee it is
	 * all here now.
	 */
#ifdef  _KERNEL
	{
	mb_t *m;

	m = fin->fin_m;
# if defined(MENTAT)
	if ((char *)oip6 + fin->fin_dlen - ICMPERR_ICMPHLEN >
	    (char *)m->b_wptr) {
		ATOMIC_INCL(nside->ns_icmp_mbuf);
		return NULL;
	}
# else
	if ((char *)oip6 + fin->fin_dlen - ICMPERR_ICMPHLEN >
	    (char *)fin->fin_ip + M_LEN(m)) {
		ATOMIC_INCL(nside->ns_icmp_mbuf);
		return NULL;
	}
# endif
	}
#endif

	if (IP6_NEQ(&fin->fin_dst6, &oip6->ip6_src)) {
		ATOMIC_INCL(nside->ns_icmp_address);
		return NULL;
	}

	p = oip6->ip6_nxt;
	if (p == IPPROTO_TCP)
		flags = IPN_TCP;
	else if (p == IPPROTO_UDP)
		flags = IPN_UDP;
	else if (p == IPPROTO_ICMPV6) {
		orgicmp = (struct icmp6_hdr *)(oip6 + 1);

		/* see if this is related to an ICMP query */
		if (ipf_nat6_icmpquerytype(orgicmp->icmp6_type)) {
			data[0] = fin->fin_data[0];
			data[1] = fin->fin_data[1];
			fin->fin_data[0] = 0;
			fin->fin_data[1] = orgicmp->icmp6_id;

			flags = IPN_ICMPERR|IPN_ICMPQUERY;
			/*
			 * NOTE : dir refers to the direction of the original
			 *        ip packet. By definition the icmp error
			 *        message flows in the opposite direction.
			 */
			if (dir == NAT_INBOUND)
				nat = ipf_nat6_inlookup(fin, flags, p,
						        &oip6->ip6_dst,
						        &oip6->ip6_src);
			else
				nat = ipf_nat6_outlookup(fin, flags, p,
							 &oip6->ip6_dst,
							 &oip6->ip6_src);
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
		tcp = (tcphdr_t *)(oip6 + 1);
		fin->fin_data[0] = ntohs(tcp->th_dport);
		fin->fin_data[1] = ntohs(tcp->th_sport);

		if (dir == NAT_INBOUND) {
			nat = ipf_nat6_inlookup(fin, flags, p, &oip6->ip6_dst,
						&oip6->ip6_src);
		} else {
			nat = ipf_nat6_outlookup(fin, flags, p, &oip6->ip6_dst,
						 &oip6->ip6_src);
		}
		fin->fin_data[0] = data[0];
		fin->fin_data[1] = data[1];
		return nat;
	}
	if (dir == NAT_INBOUND)
		nat = ipf_nat6_inlookup(fin, 0, p, &oip6->ip6_dst,
					&oip6->ip6_src);
	else
		nat = ipf_nat6_outlookup(fin, 0, p, &oip6->ip6_dst,
					 &oip6->ip6_src);

	return nat;
}


/* result = ip1 - ip2 */
u_32_t
ipf_nat6_ip6subtract(ip1, ip2)
	i6addr_t *ip1, *ip2;
{
	i6addr_t l1, l2, d;
	u_short *s1, *s2, *ds;
	u_32_t r;
	int i, neg;

	neg = 0;
	l1 = *ip1;
	l2 = *ip2;
	s1 = (u_short *)&l1;
	s2 = (u_short *)&l2;
	ds = (u_short *)&d;

	for (i = 7; i > 0; i--) {
		if (s1[i] > s2[i]) {
			ds[i] = s2[i] + 0x10000 - s1[i];
			s2[i - 1] += 0x10000;
		} else {
			ds[i] = s2[i] - s1[i];
		}
	}
	if (s2[0] > s1[0]) {
		ds[0] = s2[0] + 0x10000 - s1[0];
		neg = 1;
	} else {
		ds[0] = s2[0] - s1[0];
	}

	for (i = 0, r = 0; i < 8; i++) {
		r += ds[i];
	}

	return r;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_icmperror                                          */
/* Returns:     nat6_t* - point to matching NAT structure                    */
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
ipf_nat6_icmperror(fin, nflags, dir)
	fr_info_t *fin;
	u_int *nflags;
	int dir;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_32_t sum1, sum2, sumd, sumd2;
	i6addr_t a1, a2, a3, a4;
	struct icmp6_hdr *icmp6;
	int flags, dlen, odst;
	u_short *csump;
	tcphdr_t *tcp;
	ip6_t *oip6;
	nat_t *nat;
	void *dp;

	if ((fin->fin_flx & (FI_SHORT|FI_FRAGBODY))) {
		NBUMPSIDE6D(fin->fin_out, ns_icmp_short);
		return NULL;
	}

	/*
	 * ipf_nat6_icmperrorlookup() will return NULL for `defective' packets.
	 */
	if ((fin->fin_v != 6) || !(nat = ipf_nat6_icmperrorlookup(fin, dir))) {
		NBUMPSIDE6D(fin->fin_out, ns_icmp_notfound);
		return NULL;
	}

	tcp = NULL;
	csump = NULL;
	flags = 0;
	sumd2 = 0;
	*nflags = IPN_ICMPERR;
	icmp6 = fin->fin_dp;
	oip6 = (ip6_t *)((u_char *)icmp6 + sizeof(*icmp6));
	dp = (u_char *)oip6 + sizeof(*oip6);
	if (oip6->ip6_nxt == IPPROTO_TCP) {
		tcp = (tcphdr_t *)dp;
		csump = (u_short *)&tcp->th_sum;
		flags = IPN_TCP;
	} else if (oip6->ip6_nxt == IPPROTO_UDP) {
		udphdr_t *udp;

		udp = (udphdr_t *)dp;
		tcp = (tcphdr_t *)dp;
		csump = (u_short *)&udp->uh_sum;
		flags = IPN_UDP;
	} else if (oip6->ip6_nxt == IPPROTO_ICMPV6)
		flags = IPN_ICMPQUERY;
	dlen = fin->fin_plen - ((char *)dp - (char *)fin->fin_ip);

	/*
	 * Need to adjust ICMP header to include the real IP#'s and
	 * port #'s.  Only apply a checksum change relative to the
	 * IP address change as it will be modified again in ipf_nat6_checkout
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
	 * changed oip IP addresses and oip6->ip6_sum. However, these
	 * two changes cancel each other out (if the delta for
	 * the IP address is x, then the delta for ip_sum is minus x),
	 * so no change in the icmp_cksum is necessary.
	 *
	 * Inbound ICMP
	 * ------------
	 * MAP rule, SRC=a,DST=b -> SRC=c,DST=b
	 * - response to outgoing packet (a,b)=>(c,b) (OIP_SRC=c,OIP_DST=b)
	 * - OIP_SRC(c)=nat6_newsrcip,          OIP_DST(b)=nat6_newdstip
	 *=> OIP_SRC(c)=nat6_oldsrcip,          OIP_DST(b)=nat6_olddstip
	 *
	 * RDR rule, SRC=a,DST=b -> SRC=a,DST=c
	 * - response to outgoing packet (c,a)=>(b,a) (OIP_SRC=b,OIP_DST=a)
	 * - OIP_SRC(b)=nat6_olddstip,          OIP_DST(a)=nat6_oldsrcip
	 *=> OIP_SRC(b)=nat6_newdstip,          OIP_DST(a)=nat6_newsrcip
	 *
	 * REWRITE out rule, SRC=a,DST=b -> SRC=c,DST=d
	 * - response to outgoing packet (a,b)=>(c,d) (OIP_SRC=c,OIP_DST=d)
	 * - OIP_SRC(c)=nat6_newsrcip,          OIP_DST(d)=nat6_newdstip
	 *=> OIP_SRC(c)=nat6_oldsrcip,          OIP_DST(d)=nat6_olddstip
	 *
	 * REWRITE in rule, SRC=a,DST=b -> SRC=c,DST=d
	 * - response to outgoing packet (d,c)=>(b,a) (OIP_SRC=b,OIP_DST=a)
	 * - OIP_SRC(b)=nat6_olddstip,          OIP_DST(a)=nat6_oldsrcip
	 *=> OIP_SRC(b)=nat6_newdstip,          OIP_DST(a)=nat6_newsrcip
	 *
	 * Outbound ICMP
	 * -------------
	 * MAP rule, SRC=a,DST=b -> SRC=c,DST=b
	 * - response to incoming packet (b,c)=>(b,a) (OIP_SRC=b,OIP_DST=a)
	 * - OIP_SRC(b)=nat6_olddstip,          OIP_DST(a)=nat6_oldsrcip
	 *=> OIP_SRC(b)=nat6_newdstip,          OIP_DST(a)=nat6_newsrcip
	 *
	 * RDR rule, SRC=a,DST=b -> SRC=a,DST=c
	 * - response to incoming packet (a,b)=>(a,c) (OIP_SRC=a,OIP_DST=c)
	 * - OIP_SRC(a)=nat6_newsrcip,          OIP_DST(c)=nat6_newdstip
	 *=> OIP_SRC(a)=nat6_oldsrcip,          OIP_DST(c)=nat6_olddstip
	 *
	 * REWRITE out rule, SRC=a,DST=b -> SRC=c,DST=d
	 * - response to incoming packet (d,c)=>(b,a) (OIP_SRC=c,OIP_DST=d)
	 * - OIP_SRC(c)=nat6_olddstip,          OIP_DST(d)=nat6_oldsrcip
	 *=> OIP_SRC(b)=nat6_newdstip,          OIP_DST(a)=nat6_newsrcip
	 *
	 * REWRITE in rule, SRC=a,DST=b -> SRC=c,DST=d
	 * - response to incoming packet (a,b)=>(c,d) (OIP_SRC=b,OIP_DST=a)
	 * - OIP_SRC(b)=nat6_newsrcip,          OIP_DST(a)=nat6_newdstip
	 *=> OIP_SRC(a)=nat6_oldsrcip,          OIP_DST(c)=nat6_olddstip
	 */

	if (((fin->fin_out == 0) && ((nat->nat_redir & NAT_MAP) != 0)) ||
	    ((fin->fin_out == 1) && ((nat->nat_redir & NAT_REDIRECT) != 0))) {
		a1 = nat->nat_osrc6;
		a4.in6 = oip6->ip6_src;
		a3 = nat->nat_odst6;
		a2.in6 = oip6->ip6_dst;
		oip6->ip6_src = a1.in6;
		oip6->ip6_dst = a3.in6;
		odst = 1;
	} else {
		a1 = nat->nat_ndst6;
		a2.in6 = oip6->ip6_dst;
		a3 = nat->nat_nsrc6;
		a4.in6 = oip6->ip6_src;
		oip6->ip6_dst = a3.in6;
		oip6->ip6_src = a1.in6;
		odst = 0;
	}

	sumd = 0;
	if (IP6_NEQ(&a3, &a2) || IP6_NEQ(&a1, &a4)) {
		if (IP6_GT(&a3, &a2)) {
			sumd = ipf_nat6_ip6subtract(&a2, &a3);
			sumd--;
		} else {
			sumd = ipf_nat6_ip6subtract(&a2, &a3);
		}
		if (IP6_GT(&a1, &a4)) {
			sumd += ipf_nat6_ip6subtract(&a4, &a1);
			sumd--;
		} else {
			sumd += ipf_nat6_ip6subtract(&a4, &a1);
		}
		sumd = ~sumd;
	}

	sumd2 = sumd;
	sum1 = 0;
	sum2 = 0;

	/*
	 * Fix UDP pseudo header checksum to compensate for the
	 * IP address change.
	 */
	if (((flags & IPN_TCPUDP) != 0) && (dlen >= 4)) {
		u_32_t sum3, sum4;
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
		sumd += sum1 - sum4;
		sumd += sum3 - sum2;

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
			if (oip6->ip6_nxt == IPPROTO_UDP) {
				if ((dlen >= 8) && (*csump != 0)) {
					ipf_fix_datacksum(csump, sumd);
				} else {
					sumd2 = sum4 - sum1;
					if (sum1 > sum4)
						sumd2--;
					sumd2 += sum2 - sum3;
					if (sum3 > sum2)
						sumd2--;
				}
			} else if (oip6->ip6_nxt == IPPROTO_TCP) {
				if (dlen >= 18) {
					ipf_fix_datacksum(csump, sumd);
				} else {
					sumd2 = sum4 - sum1;
					if (sum1 > sum4)
						sumd2--;
					sumd2 += sum2 - sum3;
					if (sum3 > sum2)
						sumd2--;
				}
			}
			if (sumd2 != 0) {
				sumd2 = (sumd2 & 0xffff) + (sumd2 >> 16);
				sumd2 = (sumd2 & 0xffff) + (sumd2 >> 16);
				sumd2 = (sumd2 & 0xffff) + (sumd2 >> 16);
				ipf_fix_incksum(0, &icmp6->icmp6_cksum,
						sumd2, 0);
			}
		}
	} else if (((flags & IPN_ICMPQUERY) != 0) && (dlen >= 8)) {
		struct icmp6_hdr *orgicmp;

		/*
		 * XXX - what if this is bogus hl and we go off the end ?
		 * In this case, ipf_nat6_icmperrorlookup() will have
		 * returned NULL.
		 */
		orgicmp = (struct icmp6_hdr *)dp;

		if (odst == 1) {
			if (orgicmp->icmp6_id != nat->nat_osport) {

				/*
				 * Fix ICMP checksum (of the offening ICMP
				 * query packet) to compensate the change
				 * in the ICMP id of the offending ICMP
				 * packet.
				 *
				 * Since you modify orgicmp->icmp6_id with
				 * a delta (say x) and you compensate that
				 * in origicmp->icmp6_cksum with a delta
				 * minus x, you don't have to adjust the
				 * overall icmp->icmp6_cksum
				 */
				sum1 = ntohs(orgicmp->icmp6_id);
				sum2 = ntohs(nat->nat_osport);
				CALC_SUMD(sum1, sum2, sumd);
				orgicmp->icmp6_id = nat->nat_oicmpid;
				ipf_fix_datacksum(&orgicmp->icmp6_cksum, sumd);
			}
		} /* nat6_dir == NAT_INBOUND is impossible for icmp queries */
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
/* Function:    ipf_nat6_inlookup                                           */
/* Returns:     nat6_t*   - NULL == no match,                               */
/*                          else pointer to matching NAT entry              */
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
ipf_nat6_inlookup(fin, flags, p, src, mapdst)
	fr_info_t *fin;
	u_int flags, p;
	struct in6_addr *src , *mapdst;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	u_short sport, dport;
	grehdr_t *gre;
	ipnat_t *ipn;
	u_int sflags;
	nat_t *nat;
	int nflags;
	i6addr_t dst;
	void *ifp;
	u_int hv;

	ifp = fin->fin_ifp;
	sport = 0;
	dport = 0;
	gre = NULL;
	dst.in6 = *mapdst;
	sflags = flags & NAT_TCPUDPICMP;

	switch (p)
	{
	case IPPROTO_TCP :
	case IPPROTO_UDP :
		sport = htons(fin->fin_data[0]);
		dport = htons(fin->fin_data[1]);
		break;
	case IPPROTO_ICMPV6 :
		if (flags & IPN_ICMPERR)
			sport = fin->fin_data[1];
		else
			dport = fin->fin_data[1];
		break;
	default :
		break;
	}


	if ((flags & SI_WILDP) != 0)
		goto find_in_wild_ports;

	hv = NAT_HASH_FN6(&dst, dport, 0xffffffff);
	hv = NAT_HASH_FN6(src, hv + sport, softn->ipf_nat_table_sz);
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
			if (nat->nat_v[0] != 6)
				continue;
			if (IP6_NEQ(&nat->nat_osrc6, src) ||
			    IP6_NEQ(&nat->nat_odst6, &dst))
				continue;
			if ((nat->nat_flags & IPN_TCPUDP) != 0) {
				if (nat->nat_osport != sport)
					continue;
				if (nat->nat_odport != dport)
					continue;

			} else if (p == IPPROTO_ICMPV6) {
				if (nat->nat_osport != dport) {
					continue;
				}
			}
			break;
		case NAT_OUTBOUND :
			if (nat->nat_v[1] != 6)
				continue;
			if (IP6_NEQ(&nat->nat_ndst6, src) ||
			    IP6_NEQ(&nat->nat_nsrc6, &dst))
				continue;
			if ((nat->nat_flags & IPN_TCPUDP) != 0) {
				if (nat->nat_ndport != sport)
					continue;
				if (nat->nat_nsport != dport)
					continue;

			} else if (p == IPPROTO_ICMPV6) {
				if (nat->nat_osport != dport) {
					continue;
				}
			}
			break;
		}


		if ((nat->nat_flags & IPN_TCPUDP) != 0) {
			ipn = nat->nat_ptr;
#ifdef IPF_V6_PROXIES
			if ((ipn != NULL) && (nat->nat_aps != NULL))
				if (appr_match(fin, nat) != 0)
					continue;
#endif
		}
		if ((nat->nat_ifps[0] == NULL) && (ifp != NULL)) {
			nat->nat_ifps[0] = ifp;
			nat->nat_mtu[0] = GETIFMTU_6(ifp);
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
		NBUMPSIDE6DX(0, ns_lookup_miss, ns_lookup_miss_1);
		return NULL;
	}
	if (softn->ipf_nat_stats.ns_wilds == 0 || (fin->fin_flx & FI_NOWILD)) {
		NBUMPSIDE6D(0, ns_lookup_nowild);
		return NULL;
	}

	RWLOCK_EXIT(&softc->ipf_nat);

	hv = NAT_HASH_FN6(&dst, 0, 0xffffffff);
	hv = NAT_HASH_FN6(src, hv, softn->ipf_nat_table_sz);
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

		switch (nat->nat_dir)
		{
		case NAT_INBOUND :
			if (nat->nat_v[0] != 6)
				continue;
			if (IP6_NEQ(&nat->nat_osrc6, src) ||
			    IP6_NEQ(&nat->nat_odst6, &dst))
				continue;
			break;
		case NAT_OUTBOUND :
			if (nat->nat_v[1] != 6)
				continue;
			if (IP6_NEQ(&nat->nat_ndst6, src) ||
			    IP6_NEQ(&nat->nat_nsrc6, &dst))
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
			} else {
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
				nat->nat_mtu[0] = GETIFMTU_6(ifp);
			}
			nat->nat_flags &= ~(SI_W_DPORT|SI_W_SPORT);
			ipf_nat6_tabmove(softn, nat);
			break;
		}
	}

	MUTEX_DOWNGRADE(&softc->ipf_nat);

	if (nat == NULL) {
		NBUMPSIDE6DX(0, ns_lookup_miss, ns_lookup_miss_2);
	}
	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_tabmove                                            */
/* Returns:     Nil                                                         */
/* Parameters:  nat(I) - pointer to NAT structure                           */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* This function is only called for TCP/UDP NAT table entries where the     */
/* original was placed in the table without hashing on the ports and we now */
/* want to include hashing on port numbers.                                 */
/* ------------------------------------------------------------------------ */
static void
ipf_nat6_tabmove(softn, nat)
	ipf_nat_softc_t *softn;
	nat_t *nat;
{
	nat_t **natp;
	u_int hv0, hv1;

	if (nat->nat_flags & SI_CLONE)
		return;

	/*
	 * Remove the NAT entry from the old location
	 */
	if (nat->nat_hnext[0])
		nat->nat_hnext[0]->nat_phnext[0] = nat->nat_phnext[0];
	*nat->nat_phnext[0] = nat->nat_hnext[0];
	softn->ipf_nat_stats.ns_side[0].ns_bucketlen[nat->nat_hv[0]]--;

	if (nat->nat_hnext[1])
		nat->nat_hnext[1]->nat_phnext[1] = nat->nat_phnext[1];
	*nat->nat_phnext[1] = nat->nat_hnext[1];
	softn->ipf_nat_stats.ns_side[1].ns_bucketlen[nat->nat_hv[1]]--;

	/*
	 * Add into the NAT table in the new position
	 */
	hv0 = NAT_HASH_FN6(&nat->nat_osrc6, nat->nat_osport, 0xffffffff);
	hv0 = NAT_HASH_FN6(&nat->nat_odst6, hv0 + nat->nat_odport,
			   softn->ipf_nat_table_sz);
	hv1 = NAT_HASH_FN6(&nat->nat_nsrc6, nat->nat_nsport, 0xffffffff);
	hv1 = NAT_HASH_FN6(&nat->nat_ndst6, hv1 + nat->nat_ndport,
			   softn->ipf_nat_table_sz);

	if (nat->nat_dir == NAT_INBOUND || nat->nat_dir == NAT_DIVERTIN) {
		u_int swap;

		swap = hv0;
		hv0 = hv1;
		hv1 = swap;
	}

	/* TRACE nat_osrc6, nat_osport, nat_odst6, nat_odport, hv0 */
	/* TRACE nat_nsrc6, nat_nsport, nat_ndst6, nat_ndport, hv1 */

	nat->nat_hv[0] = hv0;
	natp = &softn->ipf_nat_table[0][hv0];
	if (*natp)
		(*natp)->nat_phnext[0] = &nat->nat_hnext[0];
	nat->nat_phnext[0] = natp;
	nat->nat_hnext[0] = *natp;
	*natp = nat;
	softn->ipf_nat_stats.ns_side[0].ns_bucketlen[hv0]++;

	nat->nat_hv[1] = hv1;
	natp = &softn->ipf_nat_table[1][hv1];
	if (*natp)
		(*natp)->nat_phnext[1] = &nat->nat_hnext[1];
	nat->nat_phnext[1] = natp;
	nat->nat_hnext[1] = *natp;
	*natp = nat;
	softn->ipf_nat_stats.ns_side[1].ns_bucketlen[hv1]++;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_outlookup                                          */
/* Returns:     nat6_t*  - NULL == no match,                                */
/*                         else pointer to matching NAT entry               */
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
ipf_nat6_outlookup(fin, flags, p, src, dst)
	fr_info_t *fin;
	u_int flags, p;
	struct in6_addr *src , *dst;
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
	sport = 0;
	dport = 0;

	switch (p)
	{
	case IPPROTO_TCP :
	case IPPROTO_UDP :
		sport = htons(fin->fin_data[0]);
		dport = htons(fin->fin_data[1]);
		break;
	case IPPROTO_ICMPV6 :
		if (flags & IPN_ICMPERR)
			sport = fin->fin_data[1];
		else
			dport = fin->fin_data[1];
		break;
	default :
		break;
	}

	if ((flags & SI_WILDP) != 0)
		goto find_out_wild_ports;

	hv = NAT_HASH_FN6(src, sport, 0xffffffff);
	hv = NAT_HASH_FN6(dst, hv + dport, softn->ipf_nat_table_sz);
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
			if (nat->nat_v[1] != 6)
				continue;
			if (IP6_NEQ(&nat->nat_ndst6, src) ||
			    IP6_NEQ(&nat->nat_nsrc6, dst))
				continue;

			if ((nat->nat_flags & IPN_TCPUDP) != 0) {
				if (nat->nat_ndport != sport)
					continue;
				if (nat->nat_nsport != dport)
					continue;

			} else if (p == IPPROTO_ICMPV6) {
				if (nat->nat_osport != dport) {
					continue;
				}
			}
			break;
		case NAT_OUTBOUND :
			if (nat->nat_v[0] != 6)
				continue;
			if (IP6_NEQ(&nat->nat_osrc6, src) ||
			    IP6_NEQ(&nat->nat_odst6, dst))
				continue;

			if ((nat->nat_flags & IPN_TCPUDP) != 0) {
				if (nat->nat_odport != dport)
					continue;
				if (nat->nat_osport != sport)
					continue;

			} else if (p == IPPROTO_ICMPV6) {
				if (nat->nat_osport != dport) {
					continue;
				}
			}
			break;
		}

		ipn = nat->nat_ptr;
#ifdef IPF_V6_PROXIES
		if ((ipn != NULL) && (nat->nat_aps != NULL))
			if (appr_match(fin, nat) != 0)
				continue;
#endif

		if ((nat->nat_ifps[1] == NULL) && (ifp != NULL)) {
			nat->nat_ifps[1] = ifp;
			nat->nat_mtu[1] = GETIFMTU_6(ifp);
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
		NBUMPSIDE6DX(1, ns_lookup_miss, ns_lookup_miss_3);
		return NULL;
	}
	if (softn->ipf_nat_stats.ns_wilds == 0 || (fin->fin_flx & FI_NOWILD)) {
		NBUMPSIDE6D(1, ns_lookup_nowild);
		return NULL;
	}

	RWLOCK_EXIT(&softc->ipf_nat);

	hv = NAT_HASH_FN6(src, 0, 0xffffffff);
	hv = NAT_HASH_FN6(dst, hv, softn->ipf_nat_table_sz);

	WRITE_ENTER(&softc->ipf_nat);

	nat = softn->ipf_nat_table[0][hv];
	for (; nat; nat = nat->nat_hnext[0]) {
		if (nat->nat_ifps[1] != NULL) {
			if ((ifp != NULL) && (ifp != nat->nat_ifps[1]))
				continue;
		}

		if (nat->nat_pr[1] != fin->fin_p)
			continue;

		switch (nat->nat_dir)
		{
		case NAT_INBOUND :
			if (nat->nat_v[1] != 6)
				continue;
			if (IP6_NEQ(&nat->nat_ndst6, src) ||
			    IP6_NEQ(&nat->nat_nsrc6, dst))
				continue;
			break;
		case NAT_OUTBOUND :
			if (nat->nat_v[0] != 6)
			continue;
			if (IP6_NEQ(&nat->nat_osrc6, src) ||
			    IP6_NEQ(&nat->nat_odst6, dst))
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
			} else {
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
				nat->nat_mtu[1] = GETIFMTU_6(ifp);
			}
			nat->nat_flags &= ~(SI_W_DPORT|SI_W_SPORT);
			ipf_nat6_tabmove(softn, nat);
			break;
		}
	}

	MUTEX_DOWNGRADE(&softc->ipf_nat);

	if (nat == NULL) {
		NBUMPSIDE6DX(1, ns_lookup_miss, ns_lookup_miss_4);
	}
	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_lookupredir                                        */
/* Returns:     nat6_t* - NULL == no match,                                 */
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
ipf_nat6_lookupredir(np)
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
		fi.fin_p = IPPROTO_ICMPV6;

	/*
	 * We can do two sorts of lookups:
	 * - IPN_IN: we have the `real' and `out' address, look for `in'.
	 * - default: we have the `in' and `out' address, look for `real'.
	 */
	if (np->nl_flags & IPN_IN) {
		if ((nat = ipf_nat6_inlookup(&fi, np->nl_flags, fi.fin_p,
					     &np->nl_realip6,
					     &np->nl_outip6))) {
			np->nl_inip6 = nat->nat_odst6.in6;
			np->nl_inport = nat->nat_odport;
		}
	} else {
		/*
		 * If nl_inip is non null, this is a lookup based on the real
		 * ip address. Else, we use the fake.
		 */
		if ((nat = ipf_nat6_outlookup(&fi, np->nl_flags, fi.fin_p,
					      &np->nl_inip6, &np->nl_outip6))) {

			if ((np->nl_flags & IPN_FINDFORWARD) != 0) {
				fr_info_t fin;
				bzero((char *)&fin, sizeof(fin));
				fin.fin_p = nat->nat_pr[0];
				fin.fin_data[0] = ntohs(nat->nat_ndport);
				fin.fin_data[1] = ntohs(nat->nat_nsport);
				if (ipf_nat6_inlookup(&fin, np->nl_flags,
						     fin.fin_p,
						     &nat->nat_ndst6.in6,
						     &nat->nat_nsrc6.in6) !=
				    NULL) {
					np->nl_flags &= ~IPN_FINDFORWARD;
				}
			}

			np->nl_realip6 = nat->nat_odst6.in6;
			np->nl_realport = nat->nat_odport;
		}
 	}

	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_match                                              */
/* Returns:     int - 0 == no match, 1 == match                             */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              np(I)    - pointer to NAT rule                              */
/*                                                                          */
/* Pull the matching of a packet against a NAT rule out of that complex     */
/* loop inside ipf_nat6_checkin() and lay it out properly in its own        */
/* function.                                                                */
/* ------------------------------------------------------------------------ */
static int
ipf_nat6_match(fin, np)
	fr_info_t *fin;
	ipnat_t *np;
{
	frtuc_t *ft;
	int match;

	match = 0;
	switch (np->in_osrcatype)
	{
	case FRI_NORMAL :
		match = IP6_MASKNEQ(&fin->fin_src6, &np->in_osrcmsk6,
				    &np->in_osrcip6);
		break;
	case FRI_LOOKUP :
		match = (*np->in_osrcfunc)(fin->fin_main_soft, np->in_osrcptr,
					   6, &fin->fin_src6, fin->fin_plen);
		break;
	}
	match ^= ((np->in_flags & IPN_NOTSRC) != 0);
	if (match)
		return 0;

	match = 0;
	switch (np->in_odstatype)
	{
	case FRI_NORMAL :
		match = IP6_MASKNEQ(&fin->fin_dst6, &np->in_odstmsk6,
				    &np->in_odstip6);
		break;
	case FRI_LOOKUP :
		match = (*np->in_odstfunc)(fin->fin_main_soft, np->in_odstptr,
					   6, &fin->fin_dst6, fin->fin_plen);
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
/* Function:    ipf_nat6_checkout                                           */
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
ipf_nat6_checkout(fin, passp)
	fr_info_t *fin;
	u_32_t *passp;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	struct icmp6_hdr *icmp6 = NULL;
	struct ifnet *ifp, *sifp;
	tcphdr_t *tcp = NULL;
	int rval, natfailed;
	ipnat_t *np = NULL;
	u_int nflags = 0;
	i6addr_t ipa, iph;
	int natadd = 1;
	frentry_t *fr;
	nat_t *nat;

	if (softn->ipf_nat_stats.ns_rules == 0 || softn->ipf_nat_lock != 0)
		return 0;

	icmp6 = NULL;
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
		case IPPROTO_ICMPV6 :
			icmp6 = fin->fin_dp;

			/*
			 * Apart from ECHO request and reply, all other
			 * informational messages should not be translated
			 * so as to keep IPv6 working.
			 */
			if (icmp6->icmp6_type > ICMP6_ECHO_REPLY)
				return 0;

			/*
			 * This is an incoming packet, so the destination is
			 * the icmp6_id and the source port equals 0
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

	ipa = fin->fin_src6;

	READ_ENTER(&softc->ipf_nat);

	if ((fin->fin_p == IPPROTO_ICMPV6) && !(nflags & IPN_ICMPQUERY) &&
	    (nat = ipf_nat6_icmperror(fin, &nflags, NAT_OUTBOUND)))
		/*EMPTY*/;
	else if ((fin->fin_flx & FI_FRAG) && (nat = ipf_frag_natknown(fin)))
		natadd = 0;
	else if ((nat = ipf_nat6_outlookup(fin, nflags|NAT_SEARCH,
					   (u_int)fin->fin_p,
					   &fin->fin_src6.in6,
					   &fin->fin_dst6.in6))) {
		nflags = nat->nat_flags;
	} else if (fin->fin_off == 0) {
		u_32_t hv, nmsk = 0;
		i6addr_t *msk;

		/*
		 * If there is no current entry in the nat table for this IP#,
		 * create one for it (if there is a matching rule).
		 */
maskloop:
		msk = &softn->ipf_nat6_map_active_masks[nmsk];
		IP6_AND(&ipa, msk, &iph);
		hv = NAT_HASH_FN6(&iph, 0, softn->ipf_nat_maprules_sz);
		for (np = softn->ipf_nat_map_rules[hv]; np; np = np->in_mnext) {
			if ((np->in_ifps[1] && (np->in_ifps[1] != ifp)))
				continue;
			if (np->in_v[0] != 6)
				continue;
			if (np->in_pr[1] && (np->in_pr[1] != fin->fin_p))
				continue;
			if ((np->in_flags & IPN_RF) &&
			    !(np->in_flags & nflags))
				continue;
			if (np->in_flags & IPN_FILTER) {
				switch (ipf_nat6_match(fin, np))
				{
				case 0 :
					continue;
				case -1 :
					rval = -1;
					goto outmatchfail;
				case 1 :
				default :
					break;
				}
			} else if (!IP6_MASKEQ(&ipa, &np->in_osrcmsk,
					       &np->in_osrcip6))
				continue;

			if ((fr != NULL) &&
			    !ipf_matchtag(&np->in_tag, &fr->fr_nattag))
				continue;

#ifdef IPF_V6_PROXIES
			if (np->in_plabel != -1) {
				if (((np->in_flags & IPN_FILTER) == 0) &&
				    (np->in_odport != fin->fin_data[1]))
					continue;
				if (appr_ok(fin, tcp, np) == 0)
					continue;
			}
#endif

			if (np->in_flags & IPN_NO) {
				np->in_hits++;
				break;
			}

			MUTEX_ENTER(&softn->ipf_nat_new);
			nat = ipf_nat6_add(fin, np, NULL, nflags, NAT_OUTBOUND);
			MUTEX_EXIT(&softn->ipf_nat_new);
			if (nat != NULL) {
				np->in_hits++;
				break;
			}
			natfailed = -1;
		}
		if ((np == NULL) && (nmsk < softn->ipf_nat6_map_max)) {
			nmsk++;
			goto maskloop;
		}
	}

	if (nat != NULL) {
		rval = ipf_nat6_out(fin, nat, natadd, nflags);
		if (rval == 1) {
			MUTEX_ENTER(&nat->nat_lock);
			ipf_nat_update(fin, nat);
			nat->nat_bytes[1] += fin->fin_plen;
			nat->nat_pkts[1]++;
			MUTEX_EXIT(&nat->nat_lock);
		}
	} else
		rval = natfailed;
outmatchfail:
	RWLOCK_EXIT(&softc->ipf_nat);

	switch (rval)
	{
	case -1 :
		if (passp != NULL) {
			NBUMPSIDE6D(1, ns_drop);
			*passp = FR_BLOCK;
			fin->fin_reason = FRB_NATV6;
		}
		fin->fin_flx |= FI_BADNAT;
		NBUMPSIDE6D(1, ns_badnat);
		break;
	case 0 :
		NBUMPSIDE6D(1, ns_ignored);
		break;
	case 1 :
		NBUMPSIDE6D(1, ns_translated);
		break;
	}
	fin->fin_ifp = sifp;
	return rval;
}

/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_out                                                */
/* Returns:     int - -1 == packet failed NAT checks so block it,           */
/*                     1 == packet was successfully translated.             */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              nat(I)    - pointer to NAT structure                        */
/*              natadd(I) - flag indicating if it is safe to add frag cache */
/*              nflags(I) - NAT flags set for this packet                   */
/*                                                                          */
/* Translate a packet coming "out" on an interface.                         */
/* ------------------------------------------------------------------------ */
static int
ipf_nat6_out(fin, nat, natadd, nflags)
	fr_info_t *fin;
	nat_t *nat;
	int natadd;
	u_32_t nflags;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	struct icmp6_hdr *icmp6;
	tcphdr_t *tcp;
	ipnat_t *np;
	int skip;
	int i;

	tcp = NULL;
	icmp6 = NULL;
	np = nat->nat_ptr;

	if ((natadd != 0) && (fin->fin_flx & FI_FRAG) && (np != NULL))
		(void) ipf_frag_natnew(softc, fin, 0, nat);

	/*
	 * Address assignment is after the checksum modification because
	 * we are using the address in the packet for determining the
	 * correct checksum offset (the ICMP error could be coming from
	 * anyone...)
	 */
	switch (nat->nat_dir)
	{
	case NAT_OUTBOUND :
		fin->fin_ip6->ip6_src = nat->nat_nsrc6.in6;
		fin->fin_src6 = nat->nat_nsrc6;
		fin->fin_ip6->ip6_dst = nat->nat_ndst6.in6;
		fin->fin_dst6 = nat->nat_ndst6;
		break;

	case NAT_INBOUND :
		fin->fin_ip6->ip6_src = nat->nat_odst6.in6;
		fin->fin_src6 = nat->nat_ndst6;
		fin->fin_ip6->ip6_dst = nat->nat_osrc6.in6;
		fin->fin_dst6 = nat->nat_nsrc6;
		break;

	case NAT_DIVERTIN :
	    {
		mb_t *m;

		skip = ipf_nat6_decap(fin, nat);
		if (skip <= 0) {
			NBUMPSIDE6D(1, ns_decap_fail);
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
		udphdr_t *uh;
		ip6_t *ip6;
		mb_t *m;

		m = M_DUP(np->in_divmp);
		if (m == NULL) {
			NBUMPSIDE6D(1, ns_divert_dup);
			return -1;
		}

		ip6 = MTOD(m, ip6_t *);

		ip6->ip6_plen = htons(fin->fin_plen + 8);

		uh = (udphdr_t *)(ip6 + 1);
		uh->uh_ulen = htons(fin->fin_plen);

		PREP_MB_T(fin, m);

		fin->fin_ip6 = ip6;
		fin->fin_plen += sizeof(ip6_t) + 8;	/* UDP + new IPv4 hdr */
		fin->fin_dlen += sizeof(ip6_t) + 8;	/* UDP + old IPv4 hdr */

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
			icmp6 = fin->fin_dp;
			icmp6->icmp6_id = nat->nat_nicmpid;
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
			NBUMPSIDE6D(1, ns_ipf_proxy_fail);
		}
	} else {
		i = 1;
	}
	fin->fin_flx |= FI_NATED;
	return i;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_checkin                                            */
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
ipf_nat6_checkin(fin, passp)
	fr_info_t *fin;
	u_32_t *passp;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	struct icmp6_hdr *icmp6;
	u_int nflags, natadd;
	int rval, natfailed;
	struct ifnet *ifp;
	i6addr_t ipa, iph;
	tcphdr_t *tcp;
	u_short dport;
	ipnat_t *np;
	nat_t *nat;

	if (softn->ipf_nat_stats.ns_rules == 0 || softn->ipf_nat_lock != 0)
		return 0;

	tcp = NULL;
	icmp6 = NULL;
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
		case IPPROTO_ICMPV6 :
			icmp6 = fin->fin_dp;

			/*
			 * Apart from ECHO request and reply, all other
			 * informational messages should not be translated
			 * so as to keep IPv6 working.
			 */
			if (icmp6->icmp6_type > ICMP6_ECHO_REPLY)
				return 0;

			/*
			 * This is an incoming packet, so the destination is
			 * the icmp6_id and the source port equals 0
			 */
			if ((fin->fin_flx & FI_ICMPQUERY) != 0) {
				nflags = IPN_ICMPQUERY;
				dport = icmp6->icmp6_id;
			} break;
		default :
			break;
		}

		if ((nflags & IPN_TCPUDP)) {
			tcp = fin->fin_dp;
			dport = fin->fin_data[1];
		}
	}

	ipa = fin->fin_dst6;

	READ_ENTER(&softc->ipf_nat);

	if ((fin->fin_p == IPPROTO_ICMPV6) && !(nflags & IPN_ICMPQUERY) &&
	    (nat = ipf_nat6_icmperror(fin, &nflags, NAT_INBOUND)))
		/*EMPTY*/;
	else if ((fin->fin_flx & FI_FRAG) && (nat = ipf_frag_natknown(fin)))
		natadd = 0;
	else if ((nat = ipf_nat6_inlookup(fin, nflags|NAT_SEARCH,
					  (u_int)fin->fin_p,
					  &fin->fin_src6.in6, &ipa.in6))) {
		nflags = nat->nat_flags;
	} else if (fin->fin_off == 0) {
		u_32_t hv, rmsk = 0;
		i6addr_t *msk;

		/*
		 * If there is no current entry in the nat table for this IP#,
		 * create one for it (if there is a matching rule).
		 */
maskloop:
		msk = &softn->ipf_nat6_rdr_active_masks[rmsk];
		IP6_AND(&ipa, msk, &iph);
		hv = NAT_HASH_FN6(&iph, 0, softn->ipf_nat_rdrrules_sz);
		for (np = softn->ipf_nat_rdr_rules[hv]; np; np = np->in_rnext) {
			if (np->in_ifps[0] && (np->in_ifps[0] != ifp))
				continue;
			if (np->in_v[0] != 6)
				continue;
			if (np->in_pr[0] && (np->in_pr[0] != fin->fin_p))
				continue;
			if ((np->in_flags & IPN_RF) && !(np->in_flags & nflags))
				continue;
			if (np->in_flags & IPN_FILTER) {
				switch (ipf_nat6_match(fin, np))
				{
				case 0 :
					continue;
				case -1 :
					rval = -1;
					goto inmatchfail;
				case 1 :
				default :
					break;
				}
			} else {
				if (!IP6_MASKEQ(&ipa, &np->in_odstmsk6,
						&np->in_odstip6)) {
					continue;
				}
				if (np->in_odport &&
				    ((np->in_dtop < dport) ||
				     (dport < np->in_odport)))
					continue;
			}

#ifdef IPF_V6_PROXIES
			if (np->in_plabel != -1) {
				if (!appr_ok(fin, tcp, np)) {
					continue;
				}
			}
#endif

			if (np->in_flags & IPN_NO) {
				np->in_hits++;
				break;
			}

			MUTEX_ENTER(&softn->ipf_nat_new);
			nat = ipf_nat6_add(fin, np, NULL, nflags, NAT_INBOUND);
			MUTEX_EXIT(&softn->ipf_nat_new);
			if (nat != NULL) {
				np->in_hits++;
				break;
			}
			natfailed = -1;
		}

		if ((np == NULL) && (rmsk < softn->ipf_nat6_rdr_max)) {
			rmsk++;
			goto maskloop;
		}
	}
	if (nat != NULL) {
		rval = ipf_nat6_in(fin, nat, natadd, nflags);
		if (rval == 1) {
			MUTEX_ENTER(&nat->nat_lock);
			ipf_nat_update(fin, nat);
			nat->nat_bytes[0] += fin->fin_plen;
			nat->nat_pkts[0]++;
			MUTEX_EXIT(&nat->nat_lock);
		}
	} else
		rval = natfailed;
inmatchfail:
	RWLOCK_EXIT(&softc->ipf_nat);

	switch (rval)
	{
	case -1 :
		if (passp != NULL) {
			NBUMPSIDE6D(0, ns_drop);
			*passp = FR_BLOCK;
			fin->fin_reason = FRB_NATV6;
		}
		fin->fin_flx |= FI_BADNAT;
		NBUMPSIDE6D(0, ns_badnat);
		break;
	case 0 :
		NBUMPSIDE6D(0, ns_ignored);
		break;
	case 1 :
		NBUMPSIDE6D(0, ns_translated);
		break;
	}
	return rval;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_in                                                 */
/* Returns:     int - -1 == packet failed NAT checks so block it,           */
/*                     1 == packet was successfully translated.             */
/* Parameters:  fin(I)    - pointer to packet information                   */
/*              nat(I)    - pointer to NAT structure                        */
/*              natadd(I) - flag indicating if it is safe to add frag cache */
/*              nflags(I) - NAT flags set for this packet                   */
/* Locks Held:   (READ)                                              */
/*                                                                          */
/* Translate a packet coming "in" on an interface.                          */
/* ------------------------------------------------------------------------ */
static int
ipf_nat6_in(fin, nat, natadd, nflags)
	fr_info_t *fin;
	nat_t *nat;
	int natadd;
	u_32_t nflags;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	struct icmp6_hdr *icmp6;
	u_short *csump;
	tcphdr_t *tcp;
	ipnat_t *np;
	int skip;
	int i;

	tcp = NULL;
	csump = NULL;
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
				NBUMPSIDE6D(0, ns_ipf_proxy_fail);
				return -1;
			}
		}
	}

	ipf_sync_update(softc, SMC_NAT, fin, nat->nat_sync);

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
			fin->fin_ip6->ip6_src = nat->nat_nsrc6.in6;
			fin->fin_src6 = nat->nat_nsrc6;
		}
		fin->fin_ip6->ip6_dst = nat->nat_ndst6.in6;
		fin->fin_dst6 = nat->nat_ndst6;
		break;

	case NAT_OUTBOUND :
		if ((fin->fin_flx & FI_ICMPERR) == 0) {
			fin->fin_ip6->ip6_src = nat->nat_odst6.in6;
			fin->fin_src6 = nat->nat_odst6;
		}
		fin->fin_ip6->ip6_dst = nat->nat_osrc6.in6;
		fin->fin_dst6 = nat->nat_osrc6;
		break;

	case NAT_DIVERTIN :
	    {
		udphdr_t *uh;
		ip6_t *ip6;
		mb_t *m;

		m = M_DUP(np->in_divmp);
		if (m == NULL) {
			NBUMPSIDE6D(0, ns_divert_dup);
			return -1;
		}

		ip6 = MTOD(m, ip6_t *);
		ip6->ip6_plen = htons(fin->fin_plen + sizeof(udphdr_t));

		uh = (udphdr_t *)(ip6 + 1);
		uh->uh_ulen = ntohs(fin->fin_plen);

		PREP_MB_T(fin, m);

		fin->fin_ip6 = ip6;
		fin->fin_plen += sizeof(ip6_t) + 8;	/* UDP + new IPv6 hdr */
		fin->fin_dlen += sizeof(ip6_t) + 8;	/* UDP + old IPv6 hdr */

		nflags &= ~IPN_TCPUDPICMP;

		break;
	    }

	case NAT_DIVERTOUT :
	    {
		mb_t *m;

		skip = ipf_nat6_decap(fin, nat);
		if (skip <= 0) {
			NBUMPSIDE6D(0, ns_decap_fail);
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
			icmp6 = fin->fin_dp;

			icmp6->icmp6_id = nat->nat_nicmpid;
		}

		csump = ipf_nat_proto(fin, nat, nflags);
	}

	/*
	 * The above comments do not hold for layer 4 (or higher) checksums...
	 */
	if (csump != NULL) {
		if (nat->nat_dir == NAT_OUTBOUND)
			ipf_fix_incksum(0, csump, nat->nat_sumd[0], 0);
		else
			ipf_fix_outcksum(0, csump, nat->nat_sumd[0], 0);
	}
	fin->fin_flx |= FI_NATED;
	if (np != NULL && np->in_tag.ipt_num[0] != 0)
		fin->fin_nattag = &np->in_tag;
	return 1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_newrewrite                                         */
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
int
ipf_nat6_newrewrite(fin, nat, nai)
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

		if ((src_search == 0) && (np->in_spnext == 0) &&
		    (dst_search == 0) && (np->in_dpnext == 0)) {
			if (l > 0)
				return -1;
		}

		/*
		 * Find a new source address
		 */
		if (ipf_nat6_nextaddr(fin, &np->in_nsrc, &frnat.fin_src6,
				 &frnat.fin_src6) == -1) {
			return -1;
		}

		if (IP6_ISZERO(&np->in_nsrcip6) &&
		    IP6_ISONES(&np->in_nsrcmsk6)) {
			src_search = 0;
			if (np->in_stepnext == 0)
				np->in_stepnext = 1;

		} else if (IP6_ISZERO(&np->in_nsrcip6) &&
			   IP6_ISZERO(&np->in_nsrcmsk6)) {
			src_search = 0;
			if (np->in_stepnext == 0)
				np->in_stepnext = 1;

		} else if (IP6_ISONES(&np->in_nsrcmsk)) {
			src_search = 0;
			if (np->in_stepnext == 0)
				np->in_stepnext = 1;

		} else if (!IP6_ISONES(&np->in_nsrcmsk6)) {
			if (np->in_stepnext == 0 && changed == -1) {
				IP6_INC(&np->in_snip);
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

		if (ipf_nat6_nextaddr(fin, &np->in_ndst, &frnat.fin_dst6,
				      &frnat.fin_dst6) == -1)
			return -1;

		if (IP6_ISZERO(&np->in_ndstip6) &&
		    IP6_ISONES(&np->in_ndstmsk6)) {
			dst_search = 0;
			if (np->in_stepnext == 2)
				np->in_stepnext = 3;

		} else if (IP6_ISZERO(&np->in_ndstip6) &&
			   IP6_ISZERO(&np->in_ndstmsk6)) {
			dst_search = 0;
			if (np->in_stepnext == 2)
				np->in_stepnext = 3;

		} else if (IP6_ISONES(&np->in_ndstmsk6)) {
			dst_search = 0;
			if (np->in_stepnext == 2)
				np->in_stepnext = 3;

		} else if (!IP6_ISONES(&np->in_ndstmsk6)) {
			if ((np->in_stepnext == 2) && (changed == -1) &&
			    (natl != NULL)) {
				changed = 2;
				np->in_stepnext++;
				IP6_INC(&np->in_dnip6);
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
			natl = ipf_nat6_inlookup(&frnat,
					    flags & ~(SI_WILDP|NAT_SEARCH),
					    (u_int)frnat.fin_p,
					    &frnat.fin_dst6.in6,
					    &frnat.fin_src6.in6);

		} else {
			natl = ipf_nat6_outlookup(&frnat,
					     flags & ~(SI_WILDP|NAT_SEARCH),
					     (u_int)frnat.fin_p,
					     &frnat.fin_dst6.in6,
					     &frnat.fin_src6.in6);
		}
		if (flags & IPN_TCPUDP) {
			swap = frnat.fin_data[0];
			frnat.fin_data[0] = frnat.fin_data[1];
			frnat.fin_data[1] = swap;
		}

		/* TRACE natl, in_stepnext, l */

		if ((natl != NULL) && (l > 8))	/* XXX 8 is arbitrary */
			return -1;

		np->in_stepnext &= 0x3;

		l++;
		changed = -1;
	} while (natl != NULL);
	nat->nat_osrc6 = fin->fin_src6;
	nat->nat_odst6 = fin->fin_dst6;
	nat->nat_nsrc6 = frnat.fin_src6;
	nat->nat_ndst6 = frnat.fin_dst6;

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
/* Function:    ipf_nat6_newdivert                                          */
/* Returns:     int - -1 == error, 0 == success                             */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to NAT entry                               */
/*              ni(I)  - pointer to structure with misc. information needed */
/*                       to create new NAT entry.                           */
/* Write Lock:  ipf_nat                                                     */
/*                                                                          */
/* Create a new NAT divert session as defined by the NAT rule.  This is     */
/* somewhat different to other NAT session creation routines because we     */
/* do not iterate through either port numbers or IP addresses, searching    */
/* for a unique mapping, however, a complimentary duplicate check is made.  */
/* ------------------------------------------------------------------------ */
int
ipf_nat6_newdivert(fin, nat, nai)
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
	nat->nat_osrc6 = fin->fin_src6;
	nat->nat_odst6 = fin->fin_dst6;
	nat->nat_osport = htons(fin->fin_data[0]);
	nat->nat_odport = htons(fin->fin_data[1]);
	frnat.fin_src6 = np->in_snip6;
	frnat.fin_dst6 = np->in_dnip6;

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
		natl = ipf_nat6_inlookup(&frnat, 0, p, &frnat.fin_dst6.in6,
					 &frnat.fin_src6.in6);

	} else {
		natl = ipf_nat6_outlookup(&frnat, 0, p, &frnat.fin_dst6.in6,
					  &frnat.fin_src6.in6);
	}

	if (natl != NULL) {
		NBUMPSIDE6D(fin->fin_out, ns_divert_exist);
		return -1;
	}

	nat->nat_nsrc6 = frnat.fin_src6;
	nat->nat_ndst6 = frnat.fin_dst6;
	if (np->in_redir & NAT_DIVERTUDP) {
		nat->nat_nsport = htons(frnat.fin_data[0]);
		nat->nat_ndport = htons(frnat.fin_data[1]);
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
/* Function:    nat6_builddivertmp                                          */
/* Returns:     int - -1 == error, 0 == success                             */
/* Parameters:  np(I) - pointer to a NAT rule                               */
/*                                                                          */
/* For divert rules, a skeleton packet representing what will be prepended  */
/* to the real packet is created.  Even though we don't have the full       */
/* packet here, a checksum is calculated that we update later when we       */
/* fill in the final details.  At present a 0 checksum for UDP is being set */
/* here because it is expected that divert will be used for localhost.      */
/* ------------------------------------------------------------------------ */
static int
ipf_nat6_builddivertmp(softn, np)
	ipf_nat_softc_t *softn;
	ipnat_t *np;
{
	udphdr_t *uh;
	size_t len;
	ip6_t *ip6;

	if ((np->in_redir & NAT_DIVERTUDP) != 0)
		len = sizeof(ip6_t) + sizeof(udphdr_t);
	else
		len = sizeof(ip6_t);

	ALLOC_MB_T(np->in_divmp, len);
	if (np->in_divmp == NULL) {
		ATOMIC_INCL(softn->ipf_nat_stats.ns_divert_build);
		return -1;
	}

	/*
	 * First, the header to get the packet diverted to the new destination
	 */
	ip6 = MTOD(np->in_divmp, ip6_t *);
	ip6->ip6_vfc = 0x60;
	if ((np->in_redir & NAT_DIVERTUDP) != 0)
		ip6->ip6_nxt = IPPROTO_UDP;
	else
		ip6->ip6_nxt = IPPROTO_IPIP;
	ip6->ip6_hlim = 255;
	ip6->ip6_plen = 0;
	ip6->ip6_src = np->in_snip6.in6;
	ip6->ip6_dst = np->in_dnip6.in6;

	if (np->in_redir & NAT_DIVERTUDP) {
		uh = (udphdr_t *)((u_char *)ip6 + sizeof(*ip6));
		uh->uh_sum = 0;
		uh->uh_ulen = 8;
		uh->uh_sport = htons(np->in_spnext);
		uh->uh_dport = htons(np->in_dpnext);
	}

	return 0;
}


#define	MINDECAP	(sizeof(ip6_t) + sizeof(udphdr_t) + sizeof(ip6_t))

/* ------------------------------------------------------------------------ */
/* Function:    nat6_decap                                                  */
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
ipf_nat6_decap(fin, nat)
	fr_info_t *fin;
	nat_t *nat;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	char *hdr;
	int skip;
	mb_t *m;

	if ((fin->fin_flx & FI_ICMPERR) != 0) {
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
		if (fin->fin_plen < (skip + sizeof(ip6_t)))
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
	if (M_LEN(m) < skip + sizeof(ip6_t)) {
		if (ipf_pr_pullup(fin, skip + sizeof(ip6_t)) == -1)
			return -1;
	}

	hdr = MTOD(fin->fin_m, char *);
	fin->fin_ip6 = (ip6_t *)(hdr + skip);

	if (ipf_pr_pullup(fin, skip + sizeof(ip6_t)) == -1) {
		NBUMPSIDE6D(fin->fin_out, ns_decap_pullup);
		return -1;
	}

	fin->fin_hlen = sizeof(ip6_t);
	fin->fin_dlen -= skip;
	fin->fin_plen -= skip;
	fin->fin_ipoff += skip;

	if (ipf_makefrip(sizeof(ip6_t), (ip_t *)hdr, fin) == -1) {
		NBUMPSIDE6D(fin->fin_out, ns_decap_bad);
		return -1;
	}

	return skip;
}


/* ------------------------------------------------------------------------ */
/* Function:    nat6_nextaddr                                               */
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
ipf_nat6_nextaddr(fin, na, old, dst)
	fr_info_t *fin;
	nat_addr_t *na;
	i6addr_t *old, *dst;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_nat_softc_t *softn = softc->ipf_nat_soft;
	i6addr_t newip, new;
	u_32_t amin, amax;
	int error;

	new.i6[0] = 0;
	new.i6[1] = 0;
	new.i6[2] = 0;
	new.i6[3] = 0;
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
		return -1;
	}

	error = -1;
	switch (na->na_function)
	{
	case IPLT_DSTLIST :
		error = ipf_dstlist_select_node(fin, na->na_ptr, dst->i6,
						NULL);
		break;

	case IPLT_NONE :
		/*
		 * 0/0 as the new address means leave it alone.
		 */
		if (na->na_addr[0].in4.s_addr == 0 &&
		    na->na_addr[1].in4.s_addr == 0) {
			new = *old;

		/*
		 * 0/32 means get the interface's address
		 */
		} else if (IP6_ISZERO(&na->na_addr[0].in6) &&
			   IP6_ISONES(&na->na_addr[1].in6)) {
			if (ipf_ifpaddr(softc, 6, na->na_atype,
				       fin->fin_ifp, &newip, NULL) == -1) {
				NBUMPSIDE6(fin->fin_out, ns_ifpaddrfail);
				return -1;
			}
			new = newip;
		} else {
			new.in6 = na->na_nextip6;
		}
		*dst = new;
		error = 0;
		break;

	default :
		NBUMPSIDE6(fin->fin_out, ns_badnextaddr);
		break;
	}

	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_nextaddrinit                                       */
/* Returns:     int - 0 == success, else error number                       */
/* Parameters:  na(I)      - NAT address information for generating new addr*/
/*              base(I)    - start of where to find strings                 */
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
/* come out of ipf_nat6_nextaddr() will be.                                 */
/* ------------------------------------------------------------------------ */
static int
ipf_nat6_nextaddrinit(softc, base, na, initial, ifp)
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
			IPFERROR(60072);
			return ESRCH;
		}
		if (na->na_ptr == NULL) {
			IPFERROR(60073);
			return ESRCH;
		}
		break;
	case FRI_DYNAMIC :
	case FRI_BROADCAST :
	case FRI_NETWORK :
	case FRI_NETMASKED :
	case FRI_PEERADDR :
		if (ifp != NULL)
			(void )ipf_ifpaddr(softc, 6, na->na_atype, ifp,
					   &na->na_addr[0],
					   &na->na_addr[1]);
		break;

	case FRI_SPLIT :
	case FRI_RANGE :
		if (initial)
			na->na_nextip6 = na->na_addr[0].in6;
		break;

	case FRI_NONE :
		IP6_ANDASSIGN(&na->na_addr[0].in6, &na->na_addr[1].in6);
		return 0;

	case FRI_NORMAL :
		IP6_ANDASSIGN(&na->na_addr[0].in6, &na->na_addr[1].in6);
		break;

	default :
		IPFERROR(60074);
		return EINVAL;
	}

	if (initial && (na->na_atype == FRI_NORMAL)) {
		if (IP6_ISZERO(&na->na_addr[0].in6)) {
			if (IP6_ISONES(&na->na_addr[1].in6) ||
			    IP6_ISZERO(&na->na_addr[1].in6)) {
				return 0;
			}
		}

		na->na_nextip6 = na->na_addr[0].in6;
		if (!IP6_ISONES(&na->na_addr[1].in6)) {
			IP6_INC(&na->na_nextip6);
		}
	}

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nat6_icmpquerytype                                      */
/* Returns:     int - 1 == success, 0 == failure                            */
/* Parameters:  icmptype(I) - ICMP type number                              */
/*                                                                          */
/* Tests to see if the ICMP type number passed is a query/response type or  */
/* not.                                                                     */
/* ------------------------------------------------------------------------ */
static int
ipf_nat6_icmpquerytype(icmptype)
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

	case ICMP6_ECHO_REPLY:
	case ICMP6_ECHO_REQUEST:
	/* route aedvertisement/solliciation is currently unsupported: */
	/* it would require rewriting the ICMP data section            */
	case ICMP6_MEMBERSHIP_QUERY:
	case ICMP6_MEMBERSHIP_REPORT:
	case ICMP6_MEMBERSHIP_REDUCTION:
	case ICMP6_WRUREQUEST:
	case ICMP6_WRUREPLY:
	case MLD6_MTRACE_RESP:
	case MLD6_MTRACE:
		return 1;
	default:
		return 0;
	}
}
#endif /* USE_INET6 */
