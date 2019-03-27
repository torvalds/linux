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
#if !defined(_KERNEL)
# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# define _KERNEL
# include <sys/uio.h>
# undef _KERNEL
#endif
#if defined(_KERNEL) && defined(__FreeBSD_version)
# include <sys/filio.h>
# include <sys/fcntl.h>
#else
# include <sys/ioctl.h>
#endif
# include <sys/protosw.h>
#include <sys/socket.h>
#if defined(_KERNEL)
# include <sys/systm.h>
# if !defined(__SVR4) && !defined(__svr4__)
#  include <sys/mbuf.h>
# endif
#endif
#if !defined(__SVR4) && !defined(__svr4__)
# if defined(_KERNEL)
#  include <sys/kernel.h>
# endif
#else
# include <sys/byteorder.h>
# ifdef _KERNEL
#  include <sys/dditypes.h>
# endif
# include <sys/stream.h>
# include <sys/kmem.h>
#endif
#include <net/if.h>
#ifdef sun
# include <net/af.h>
#endif
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
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
#include "netinet/ip_auth.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_proxy.h"
#include "netinet/ip_sync.h"
/* END OF INCLUDES */

#if !defined(lint)
static const char sccsid[] = "@(#)ip_frag.c	1.11 3/24/96 (C) 1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$FreeBSD$";
/* static const char rcsid[] = "@(#)$Id: ip_frag.c,v 2.77.2.12 2007/09/20 12:51:51 darrenr Exp $"; */
#endif


#ifdef USE_MUTEXES
static ipfr_t *ipfr_frag_new __P((ipf_main_softc_t *, ipf_frag_softc_t *,
				  fr_info_t *, u_32_t, ipfr_t **,
				  ipfrwlock_t *));
static ipfr_t *ipf_frag_lookup __P((ipf_main_softc_t *, ipf_frag_softc_t *, fr_info_t *, ipfr_t **, ipfrwlock_t *));
static void ipf_frag_deref __P((void *, ipfr_t **, ipfrwlock_t *));
static int ipf_frag_next __P((ipf_main_softc_t *, ipftoken_t *, ipfgeniter_t *,
			      ipfr_t **, ipfrwlock_t *));
#else
static ipfr_t *ipfr_frag_new __P((ipf_main_softc_t *, ipf_frag_softc_t *,
				  fr_info_t *, u_32_t, ipfr_t **));
static ipfr_t *ipf_frag_lookup __P((ipf_main_softc_t *, ipf_frag_softc_t *, fr_info_t *, ipfr_t **));
static void ipf_frag_deref __P((void *, ipfr_t **));
static int ipf_frag_next __P((ipf_main_softc_t *, ipftoken_t *, ipfgeniter_t *,
			      ipfr_t **));
#endif
static void ipf_frag_delete __P((ipf_main_softc_t *, ipfr_t *, ipfr_t ***));
static void ipf_frag_free __P((ipf_frag_softc_t *, ipfr_t *));

static frentry_t ipfr_block;

static ipftuneable_t ipf_frag_tuneables[] = {
	{ { (void *)offsetof(ipf_frag_softc_t, ipfr_size) },
		"frag_size",		1,	0x7fffffff,
		stsizeof(ipf_frag_softc_t, ipfr_size),
		IPFT_WRDISABLED,	NULL,	NULL },
	{ { (void *)offsetof(ipf_frag_softc_t, ipfr_ttl) },
		"frag_ttl",		1,	0x7fffffff,
		stsizeof(ipf_frag_softc_t, ipfr_ttl),
		0,			NULL,	NULL },
	{ { NULL },
		NULL,			0,	0,
		0,
		0,			NULL,	NULL }
};

#define	FBUMP(x)	softf->ipfr_stats.x++
#define	FBUMPD(x)	do { softf->ipfr_stats.x++; DT(x); } while (0)


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_main_load                                          */
/* Returns:     int - 0 == success, -1 == error                             */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* Initialise the filter rule associted with blocked packets - everyone can */
/* use it.                                                                  */
/* ------------------------------------------------------------------------ */
int
ipf_frag_main_load()
{
	bzero((char *)&ipfr_block, sizeof(ipfr_block));
	ipfr_block.fr_flags = FR_BLOCK|FR_QUICK;
	ipfr_block.fr_ref = 1;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_main_unload                                        */
/* Returns:     int - 0 == success, -1 == error                             */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* A null-op function that exists as a placeholder so that the flow in      */
/* other functions is obvious.                                              */
/* ------------------------------------------------------------------------ */
int
ipf_frag_main_unload()
{
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_soft_create                                        */
/* Returns:     void *   - NULL = failure, else pointer to local context    */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Allocate a new soft context structure to track fragment related info.    */
/* ------------------------------------------------------------------------ */
/*ARGSUSED*/
void *
ipf_frag_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_frag_softc_t *softf;

	KMALLOC(softf, ipf_frag_softc_t *);
	if (softf == NULL)
		return NULL;

	bzero((char *)softf, sizeof(*softf));

	RWLOCK_INIT(&softf->ipfr_ipidfrag, "frag ipid lock");
	RWLOCK_INIT(&softf->ipfr_frag, "ipf fragment rwlock");
	RWLOCK_INIT(&softf->ipfr_natfrag, "ipf NAT fragment rwlock");

	softf->ipf_frag_tune = ipf_tune_array_copy(softf,
						   sizeof(ipf_frag_tuneables),
						   ipf_frag_tuneables);
	if (softf->ipf_frag_tune == NULL) {
		ipf_frag_soft_destroy(softc, softf);
		return NULL;
	}
	if (ipf_tune_array_link(softc, softf->ipf_frag_tune) == -1) {
		ipf_frag_soft_destroy(softc, softf);
		return NULL;
	}

	softf->ipfr_size = IPFT_SIZE;
	softf->ipfr_ttl = IPF_TTLVAL(60);
	softf->ipfr_lock = 1;
	softf->ipfr_tail = &softf->ipfr_list;
	softf->ipfr_nattail = &softf->ipfr_natlist;
	softf->ipfr_ipidtail = &softf->ipfr_ipidlist;

	return softf;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_soft_destroy                                       */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*                                                                          */
/* Initialise the hash tables for the fragment cache lookups.               */
/* ------------------------------------------------------------------------ */
void
ipf_frag_soft_destroy(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_frag_softc_t *softf = arg;

	RW_DESTROY(&softf->ipfr_ipidfrag);
	RW_DESTROY(&softf->ipfr_frag);
	RW_DESTROY(&softf->ipfr_natfrag);

	if (softf->ipf_frag_tune != NULL) {
		ipf_tune_array_unlink(softc, softf->ipf_frag_tune);
		KFREES(softf->ipf_frag_tune, sizeof(ipf_frag_tuneables));
		softf->ipf_frag_tune = NULL;
	}

	KFREE(softf);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_soft_init                                          */
/* Returns:     int      - 0 == success, -1 == error                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*                                                                          */
/* Initialise the hash tables for the fragment cache lookups.               */
/* ------------------------------------------------------------------------ */
/*ARGSUSED*/
int
ipf_frag_soft_init(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_frag_softc_t *softf = arg;

	KMALLOCS(softf->ipfr_heads, ipfr_t **,
		 softf->ipfr_size * sizeof(ipfr_t *));
	if (softf->ipfr_heads == NULL)
		return -1;

	bzero((char *)softf->ipfr_heads, softf->ipfr_size * sizeof(ipfr_t *));

	KMALLOCS(softf->ipfr_nattab, ipfr_t **,
		 softf->ipfr_size * sizeof(ipfr_t *));
	if (softf->ipfr_nattab == NULL)
		return -2;

	bzero((char *)softf->ipfr_nattab, softf->ipfr_size * sizeof(ipfr_t *));

	KMALLOCS(softf->ipfr_ipidtab, ipfr_t **,
		 softf->ipfr_size * sizeof(ipfr_t *));
	if (softf->ipfr_ipidtab == NULL)
		return -3;

	bzero((char *)softf->ipfr_ipidtab,
	      softf->ipfr_size * sizeof(ipfr_t *));

	softf->ipfr_lock = 0;
	softf->ipfr_inited = 1;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_soft_fini                                          */
/* Returns:     int      - 0 == success, -1 == error                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*                                                                          */
/* Free all memory allocated whilst running and from initialisation.        */
/* ------------------------------------------------------------------------ */
int
ipf_frag_soft_fini(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_frag_softc_t *softf = arg;

	softf->ipfr_lock = 1;

	if (softf->ipfr_inited == 1) {
		ipf_frag_clear(softc);

		softf->ipfr_inited = 0;
	}

	if (softf->ipfr_heads != NULL)
		KFREES(softf->ipfr_heads,
		       softf->ipfr_size * sizeof(ipfr_t *));
	softf->ipfr_heads = NULL;

	if (softf->ipfr_nattab != NULL)
		KFREES(softf->ipfr_nattab,
		       softf->ipfr_size * sizeof(ipfr_t *));
	softf->ipfr_nattab = NULL;

	if (softf->ipfr_ipidtab != NULL)
		KFREES(softf->ipfr_ipidtab,
		       softf->ipfr_size * sizeof(ipfr_t *));
	softf->ipfr_ipidtab = NULL;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_set_lock                                           */
/* Returns:     Nil                                                         */
/* Parameters:  arg(I) - pointer to local context to use                    */
/*              tmp(I) - new value for lock                                 */
/*                                                                          */
/* Stub function that allows for external manipulation of ipfr_lock         */
/* ------------------------------------------------------------------------ */
void
ipf_frag_setlock(arg, tmp)
	void *arg;
	int tmp;
{
	ipf_frag_softc_t *softf = arg;

	softf->ipfr_lock = tmp;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_stats                                              */
/* Returns:     ipfrstat_t* - pointer to struct with current frag stats     */
/* Parameters:  arg(I) - pointer to local context to use                    */
/*                                                                          */
/* Updates ipfr_stats with current information and returns a pointer to it  */
/* ------------------------------------------------------------------------ */
ipfrstat_t *
ipf_frag_stats(arg)
	void *arg;
{
	ipf_frag_softc_t *softf = arg;

	softf->ipfr_stats.ifs_table = softf->ipfr_heads;
	softf->ipfr_stats.ifs_nattab = softf->ipfr_nattab;
	return &softf->ipfr_stats;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipfr_frag_new                                               */
/* Returns:     ipfr_t * - pointer to fragment cache state info or NULL     */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              table(I) - pointer to frag table to add to                  */
/*              lock(I)  - pointer to lock to get a write hold of           */
/*                                                                          */
/* Add a new entry to the fragment cache, registering it as having come     */
/* through this box, with the result of the filter operation.               */
/*                                                                          */
/* If this function succeeds, it returns with a write lock held on "lock".  */
/* If it fails, no lock is held on return.                                  */
/* ------------------------------------------------------------------------ */
static ipfr_t *
ipfr_frag_new(softc, softf, fin, pass, table
#ifdef USE_MUTEXES
, lock
#endif
)
	ipf_main_softc_t *softc;
	ipf_frag_softc_t *softf;
	fr_info_t *fin;
	u_32_t pass;
	ipfr_t *table[];
#ifdef USE_MUTEXES
	ipfrwlock_t *lock;
#endif
{
	ipfr_t *fra, frag, *fran;
	u_int idx, off;
	frentry_t *fr;

	if (softf->ipfr_stats.ifs_inuse >= softf->ipfr_size) {
		FBUMPD(ifs_maximum);
		return NULL;
	}

	if ((fin->fin_flx & (FI_FRAG|FI_BAD)) != FI_FRAG) {
		FBUMPD(ifs_newbad);
		return NULL;
	}

	if (pass & FR_FRSTRICT) {
		if (fin->fin_off != 0) {
			FBUMPD(ifs_newrestrictnot0);
			return NULL;
		}
	}

	frag.ipfr_v = fin->fin_v;
	idx = fin->fin_v;
	frag.ipfr_p = fin->fin_p;
	idx += fin->fin_p;
	frag.ipfr_id = fin->fin_id;
	idx += fin->fin_id;
	frag.ipfr_source = fin->fin_fi.fi_src;
	idx += frag.ipfr_src.s_addr;
	frag.ipfr_dest = fin->fin_fi.fi_dst;
	idx += frag.ipfr_dst.s_addr;
	frag.ipfr_ifp = fin->fin_ifp;
	idx *= 127;
	idx %= softf->ipfr_size;

	frag.ipfr_optmsk = fin->fin_fi.fi_optmsk & IPF_OPTCOPY;
	frag.ipfr_secmsk = fin->fin_fi.fi_secmsk;
	frag.ipfr_auth = fin->fin_fi.fi_auth;

	off = fin->fin_off >> 3;
	if (off == 0) {
		char *ptr;
		int end;

#ifdef USE_INET6
		if (fin->fin_v == 6) {

			ptr = (char *)fin->fin_fraghdr +
			      sizeof(struct ip6_frag);
		} else
#endif
		{
			ptr = fin->fin_dp;
		}
		end = fin->fin_plen - (ptr - (char *)fin->fin_ip);
		frag.ipfr_firstend = end >> 3;
	} else {
		frag.ipfr_firstend = 0;
	}

	/*
	 * allocate some memory, if possible, if not, just record that we
	 * failed to do so.
	 */
	KMALLOC(fran, ipfr_t *);
	if (fran == NULL) {
		FBUMPD(ifs_nomem);
		return NULL;
	}

	WRITE_ENTER(lock);

	/*
	 * first, make sure it isn't already there...
	 */
	for (fra = table[idx]; (fra != NULL); fra = fra->ipfr_hnext)
		if (!bcmp((char *)&frag.ipfr_ifp, (char *)&fra->ipfr_ifp,
			  IPFR_CMPSZ)) {
			RWLOCK_EXIT(lock);
			FBUMPD(ifs_exists);
			KFREE(fran);
			return NULL;
		}

	fra = fran;
	fran = NULL;
	fr = fin->fin_fr;
	fra->ipfr_rule = fr;
	if (fr != NULL) {
		MUTEX_ENTER(&fr->fr_lock);
		fr->fr_ref++;
		MUTEX_EXIT(&fr->fr_lock);
	}

	/*
	 * Insert the fragment into the fragment table, copy the struct used
	 * in the search using bcopy rather than reassign each field.
	 * Set the ttl to the default.
	 */
	if ((fra->ipfr_hnext = table[idx]) != NULL)
		table[idx]->ipfr_hprev = &fra->ipfr_hnext;
	fra->ipfr_hprev = table + idx;
	fra->ipfr_data = NULL;
	table[idx] = fra;
	bcopy((char *)&frag.ipfr_ifp, (char *)&fra->ipfr_ifp, IPFR_CMPSZ);
	fra->ipfr_v = fin->fin_v;
	fra->ipfr_ttl = softc->ipf_ticks + softf->ipfr_ttl;
	fra->ipfr_firstend = frag.ipfr_firstend;

	/*
	 * Compute the offset of the expected start of the next packet.
	 */
	if (off == 0)
		fra->ipfr_seen0 = 1;
	fra->ipfr_off = off + (fin->fin_dlen >> 3);
	fra->ipfr_pass = pass;
	fra->ipfr_ref = 1;
	fra->ipfr_pkts = 1;
	fra->ipfr_bytes = fin->fin_plen;
	FBUMP(ifs_inuse);
	FBUMP(ifs_new);
	return fra;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_new                                                */
/* Returns:     int - 0 == success, -1 == error                             */
/* Parameters:  fin(I)  - pointer to packet information                     */
/*                                                                          */
/* Add a new entry to the fragment cache table based on the current packet  */
/* ------------------------------------------------------------------------ */
int
ipf_frag_new(softc, fin, pass)
	ipf_main_softc_t *softc;
	u_32_t pass;
	fr_info_t *fin;
{
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;
	ipfr_t	*fra;

	if (softf->ipfr_lock != 0)
		return -1;

#ifdef USE_MUTEXES
	fra = ipfr_frag_new(softc, softf, fin, pass, softf->ipfr_heads, &softc->ipf_frag);
#else
	fra = ipfr_frag_new(softc, softf, fin, pass, softf->ipfr_heads);
#endif
	if (fra != NULL) {
		*softf->ipfr_tail = fra;
		fra->ipfr_prev = softf->ipfr_tail;
		softf->ipfr_tail = &fra->ipfr_next;
		fra->ipfr_next = NULL;
		RWLOCK_EXIT(&softc->ipf_frag);
	}
	return fra ? 0 : -1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_natnew                                             */
/* Returns:     int - 0 == success, -1 == error                             */
/* Parameters:  fin(I)  - pointer to packet information                     */
/*              nat(I)  - pointer to NAT structure                          */
/*                                                                          */
/* Create a new NAT fragment cache entry based on the current packet and    */
/* the NAT structure for this "session".                                    */
/* ------------------------------------------------------------------------ */
int
ipf_frag_natnew(softc, fin, pass, nat)
	ipf_main_softc_t *softc;
	fr_info_t *fin;
	u_32_t pass;
	nat_t *nat;
{
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;
	ipfr_t	*fra;

	if (softf->ipfr_lock != 0)
		return 0;

#ifdef USE_MUTEXES
	fra = ipfr_frag_new(softc, softf, fin, pass, softf->ipfr_nattab,
			    &softf->ipfr_natfrag);
#else
	fra = ipfr_frag_new(softc, softf, fin, pass, softf->ipfr_nattab);
#endif
	if (fra != NULL) {
		fra->ipfr_data = nat;
		nat->nat_data = fra;
		*softf->ipfr_nattail = fra;
		fra->ipfr_prev = softf->ipfr_nattail;
		softf->ipfr_nattail = &fra->ipfr_next;
		fra->ipfr_next = NULL;
		RWLOCK_EXIT(&softf->ipfr_natfrag);
		return 0;
	}
	return -1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_ipidnew                                            */
/* Returns:     int - 0 == success, -1 == error                             */
/* Parameters:  fin(I)  - pointer to packet information                     */
/*              ipid(I) - new IP ID for this fragmented packet              */
/*                                                                          */
/* Create a new fragment cache entry for this packet and store, as a data   */
/* pointer, the new IP ID value.                                            */
/* ------------------------------------------------------------------------ */
int
ipf_frag_ipidnew(fin, ipid)
	fr_info_t *fin;
	u_32_t ipid;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;
	ipfr_t	*fra;

	if (softf->ipfr_lock)
		return 0;

#ifdef USE_MUTEXES
	fra = ipfr_frag_new(softc, softf, fin, 0, softf->ipfr_ipidtab, &softf->ipfr_ipidfrag);
#else
	fra = ipfr_frag_new(softc, softf, fin, 0, softf->ipfr_ipidtab);
#endif
	if (fra != NULL) {
		fra->ipfr_data = (void *)(intptr_t)ipid;
		*softf->ipfr_ipidtail = fra;
		fra->ipfr_prev = softf->ipfr_ipidtail;
		softf->ipfr_ipidtail = &fra->ipfr_next;
		fra->ipfr_next = NULL;
		RWLOCK_EXIT(&softf->ipfr_ipidfrag);
	}
	return fra ? 0 : -1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_lookup                                             */
/* Returns:     ipfr_t * - pointer to ipfr_t structure if there's a         */
/*                         matching entry in the frag table, else NULL      */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              table(I) - pointer to fragment cache table to search        */
/*                                                                          */
/* Check the fragment cache to see if there is already a record of this     */
/* packet with its filter result known.                                     */
/*                                                                          */
/* If this function succeeds, it returns with a write lock held on "lock".  */
/* If it fails, no lock is held on return.                                  */
/* ------------------------------------------------------------------------ */
static ipfr_t *
ipf_frag_lookup(softc, softf, fin, table
#ifdef USE_MUTEXES
, lock
#endif
)
	ipf_main_softc_t *softc;
	ipf_frag_softc_t *softf;
	fr_info_t *fin;
	ipfr_t *table[];
#ifdef USE_MUTEXES
	ipfrwlock_t *lock;
#endif
{
	ipfr_t *f, frag;
	u_int idx;

	/*
	 * We don't want to let short packets match because they could be
	 * compromising the security of other rules that want to match on
	 * layer 4 fields (and can't because they have been fragmented off.)
	 * Why do this check here?  The counter acts as an indicator of this
	 * kind of attack, whereas if it was elsewhere, it wouldn't know if
	 * other matching packets had been seen.
	 */
	if (fin->fin_flx & FI_SHORT) {
		FBUMPD(ifs_short);
		return NULL;
	}

	if ((fin->fin_flx & FI_BAD) != 0) {
		FBUMPD(ifs_bad);
		return NULL;
	}

	/*
	 * For fragments, we record protocol, packet id, TOS and both IP#'s
	 * (these should all be the same for all fragments of a packet).
	 *
	 * build up a hash value to index the table with.
	 */
	frag.ipfr_v = fin->fin_v;
	idx = fin->fin_v;
	frag.ipfr_p = fin->fin_p;
	idx += fin->fin_p;
	frag.ipfr_id = fin->fin_id;
	idx += fin->fin_id;
	frag.ipfr_source = fin->fin_fi.fi_src;
	idx += frag.ipfr_src.s_addr;
	frag.ipfr_dest = fin->fin_fi.fi_dst;
	idx += frag.ipfr_dst.s_addr;
	frag.ipfr_ifp = fin->fin_ifp;
	idx *= 127;
	idx %= softf->ipfr_size;

	frag.ipfr_optmsk = fin->fin_fi.fi_optmsk & IPF_OPTCOPY;
	frag.ipfr_secmsk = fin->fin_fi.fi_secmsk;
	frag.ipfr_auth = fin->fin_fi.fi_auth;

	READ_ENTER(lock);

	/*
	 * check the table, careful to only compare the right amount of data
	 */
	for (f = table[idx]; f; f = f->ipfr_hnext) {
		if (!bcmp((char *)&frag.ipfr_ifp, (char *)&f->ipfr_ifp,
			  IPFR_CMPSZ)) {
			u_short	off;

			/*
			 * XXX - We really need to be guarding against the
			 * retransmission of (src,dst,id,offset-range) here
			 * because a fragmented packet is never resent with
			 * the same IP ID# (or shouldn't).
			 */
			off = fin->fin_off >> 3;
			if (f->ipfr_seen0) {
				if (off == 0) {
					FBUMPD(ifs_retrans0);
					continue;
				}

				/*
				 * Case 3. See comment for frpr_fragment6.
				 */
				if ((f->ipfr_firstend != 0) &&
				    (off < f->ipfr_firstend)) {
					FBUMP(ifs_overlap);
					DT2(ifs_overlap, u_short, off,
					    ipfr_t *, f);
					DT3(ipf_fi_bad_ifs_overlap, fr_info_t *, fin, u_short, off,
					    ipfr_t *, f);
					fin->fin_flx |= FI_BAD;
					break;
				}
			} else if (off == 0)
				f->ipfr_seen0 = 1;

			if (f != table[idx] && MUTEX_TRY_UPGRADE(lock)) {
				ipfr_t **fp;

				/*
				 * Move fragment info. to the top of the list
				 * to speed up searches.  First, delink...
				 */
				fp = f->ipfr_hprev;
				(*fp) = f->ipfr_hnext;
				if (f->ipfr_hnext != NULL)
					f->ipfr_hnext->ipfr_hprev = fp;
				/*
				 * Then put back at the top of the chain.
				 */
				f->ipfr_hnext = table[idx];
				table[idx]->ipfr_hprev = &f->ipfr_hnext;
				f->ipfr_hprev = table + idx;
				table[idx] = f;
				MUTEX_DOWNGRADE(lock);
			}

			/*
			 * If we've follwed the fragments, and this is the
			 * last (in order), shrink expiration time.
			 */
			if (off == f->ipfr_off) {
				f->ipfr_off = (fin->fin_dlen >> 3) + off;

				/*
				 * Well, we could shrink the expiration time
				 * but only if every fragment has been seen
				 * in order upto this, the last. ipfr_badorder
				 * is used here to count those out of order
				 * and if it equals 0 when we get to the last
				 * fragment then we can assume all of the
				 * fragments have been seen and in order.
				 */
#if 0
				/*
				 * Doing this properly requires moving it to
				 * the head of the list which is infesible.
				 */
				if ((more == 0) && (f->ipfr_badorder == 0))
					f->ipfr_ttl = softc->ipf_ticks + 1;
#endif
			} else {
				f->ipfr_badorder++;
				FBUMPD(ifs_unordered);
				if (f->ipfr_pass & FR_FRSTRICT) {
					FBUMPD(ifs_strict);
					continue;
				}
			}
			f->ipfr_pkts++;
			f->ipfr_bytes += fin->fin_plen;
			FBUMP(ifs_hits);
			return f;
		}
	}

	RWLOCK_EXIT(lock);
	FBUMP(ifs_miss);
	return NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_natknown                                           */
/* Returns:     nat_t* - pointer to 'parent' NAT structure if frag table    */
/*                       match found, else NULL                             */
/* Parameters:  fin(I)  - pointer to packet information                     */
/*                                                                          */
/* Functional interface for NAT lookups of the NAT fragment cache           */
/* ------------------------------------------------------------------------ */
nat_t *
ipf_frag_natknown(fin)
	fr_info_t *fin;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;
	nat_t	*nat;
	ipfr_t	*ipf;

	if ((softf->ipfr_lock) || !softf->ipfr_natlist)
		return NULL;
#ifdef USE_MUTEXES
	ipf = ipf_frag_lookup(softc, softf, fin, softf->ipfr_nattab,
			      &softf->ipfr_natfrag);
#else
	ipf = ipf_frag_lookup(softc, softf, fin, softf->ipfr_nattab);
#endif
	if (ipf != NULL) {
		nat = ipf->ipfr_data;
		/*
		 * This is the last fragment for this packet.
		 */
		if ((ipf->ipfr_ttl == softc->ipf_ticks + 1) && (nat != NULL)) {
			nat->nat_data = NULL;
			ipf->ipfr_data = NULL;
		}
		RWLOCK_EXIT(&softf->ipfr_natfrag);
	} else
		nat = NULL;
	return nat;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_ipidknown                                          */
/* Returns:     u_32_t - IPv4 ID for this packet if match found, else       */
/*                       return 0xfffffff to indicate no match.             */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* Functional interface for IP ID lookups of the IP ID fragment cache       */
/* ------------------------------------------------------------------------ */
u_32_t
ipf_frag_ipidknown(fin)
	fr_info_t *fin;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;
	ipfr_t	*ipf;
	u_32_t	id;

	if (softf->ipfr_lock || !softf->ipfr_ipidlist)
		return 0xffffffff;

#ifdef USE_MUTEXES
	ipf = ipf_frag_lookup(softc, softf, fin, softf->ipfr_ipidtab,
			      &softf->ipfr_ipidfrag);
#else
	ipf = ipf_frag_lookup(softc, softf, fin, softf->ipfr_ipidtab);
#endif
	if (ipf != NULL) {
		id = (u_32_t)(intptr_t)ipf->ipfr_data;
		RWLOCK_EXIT(&softf->ipfr_ipidfrag);
	} else
		id = 0xffffffff;
	return id;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_known                                              */
/* Returns:     frentry_t* - pointer to filter rule if a match is found in  */
/*                           the frag cache table, else NULL.               */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              passp(O) - pointer to where to store rule flags resturned   */
/*                                                                          */
/* Functional interface for normal lookups of the fragment cache.  If a     */
/* match is found, return the rule pointer and flags from the rule, except  */
/* that if FR_LOGFIRST is set, reset FR_LOG.                                */
/* ------------------------------------------------------------------------ */
frentry_t *
ipf_frag_known(fin, passp)
	fr_info_t *fin;
	u_32_t *passp;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;
	frentry_t *fr = NULL;
	ipfr_t	*fra;
	u_32_t pass;

	if ((softf->ipfr_lock) || (softf->ipfr_list == NULL))
		return NULL;

#ifdef USE_MUTEXES
	fra = ipf_frag_lookup(softc, softf, fin, softf->ipfr_heads,
			      &softc->ipf_frag);
#else
	fra = ipf_frag_lookup(softc, softf, fin, softf->ipfr_heads);
#endif
	if (fra != NULL) {
		if (fin->fin_flx & FI_BAD) {
			fr = &ipfr_block;
			fin->fin_reason = FRB_BADFRAG;
			DT2(ipf_frb_badfrag, fr_info_t *, fin, uint, fra);
		} else {
			fr = fra->ipfr_rule;
		}
		fin->fin_fr = fr;
		if (fr != NULL) {
			pass = fr->fr_flags;
			if ((pass & FR_KEEPSTATE) != 0) {
				fin->fin_flx |= FI_STATE;
				/*
				 * Reset the keep state flag here so that we
				 * don't try and add a new state entry because
				 * of a match here. That leads to blocking of
				 * the packet later because the add fails.
				 */
				pass &= ~FR_KEEPSTATE;
			}
			if ((pass & FR_LOGFIRST) != 0)
				pass &= ~(FR_LOGFIRST|FR_LOG);
			*passp = pass;
		}
		RWLOCK_EXIT(&softc->ipf_frag);
	}
	return fr;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_natforget                                          */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              ptr(I) - pointer to data structure                          */
/*                                                                          */
/* Search through all of the fragment cache entries for NAT and wherever a  */
/* pointer  is found to match ptr, reset it to NULL.                        */
/* ------------------------------------------------------------------------ */
void
ipf_frag_natforget(softc, ptr)
	ipf_main_softc_t *softc;
	void *ptr;
{
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;
	ipfr_t	*fr;

	WRITE_ENTER(&softf->ipfr_natfrag);
	for (fr = softf->ipfr_natlist; fr; fr = fr->ipfr_next)
		if (fr->ipfr_data == ptr)
			fr->ipfr_data = NULL;
	RWLOCK_EXIT(&softf->ipfr_natfrag);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_delete                                             */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              fra(I)   - pointer to fragment structure to delete          */
/*              tail(IO) - pointer to the pointer to the tail of the frag   */
/*                         list                                             */
/*                                                                          */
/* Remove a fragment cache table entry from the table & list.  Also free    */
/* the filter rule it is associated with it if it is no longer used as a    */
/* result of decreasing the reference count.                                */
/* ------------------------------------------------------------------------ */
static void
ipf_frag_delete(softc, fra, tail)
	ipf_main_softc_t *softc;
	ipfr_t *fra, ***tail;
{
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;

	if (fra->ipfr_next)
		fra->ipfr_next->ipfr_prev = fra->ipfr_prev;
	*fra->ipfr_prev = fra->ipfr_next;
	if (*tail == &fra->ipfr_next)
		*tail = fra->ipfr_prev;

	if (fra->ipfr_hnext)
		fra->ipfr_hnext->ipfr_hprev = fra->ipfr_hprev;
	*fra->ipfr_hprev = fra->ipfr_hnext;

	if (fra->ipfr_rule != NULL) {
		(void) ipf_derefrule(softc, &fra->ipfr_rule);
	}

	if (fra->ipfr_ref <= 0)
		ipf_frag_free(softf, fra);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_free                                               */
/* Returns:     Nil                                                         */
/* Parameters:  softf(I) - pointer to fragment context information          */
/*              fra(I)   - pointer to fragment structure to free            */
/*                                                                          */
/* Free up a fragment cache entry and bump relevent statistics.             */
/* ------------------------------------------------------------------------ */
static void
ipf_frag_free(softf, fra)
	ipf_frag_softc_t *softf;
	ipfr_t *fra;
{
	KFREE(fra);
	FBUMP(ifs_expire);
	softf->ipfr_stats.ifs_inuse--;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_clear                                              */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Free memory in use by fragment state information kept.  Do the normal    */
/* fragment state stuff first and then the NAT-fragment table.              */
/* ------------------------------------------------------------------------ */
void
ipf_frag_clear(softc)
	ipf_main_softc_t *softc;
{
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;
	ipfr_t	*fra;
	nat_t	*nat;

	WRITE_ENTER(&softc->ipf_frag);
	while ((fra = softf->ipfr_list) != NULL) {
		fra->ipfr_ref--;
		ipf_frag_delete(softc, fra, &softf->ipfr_tail);
	}
	softf->ipfr_tail = &softf->ipfr_list;
	RWLOCK_EXIT(&softc->ipf_frag);

	WRITE_ENTER(&softc->ipf_nat);
	WRITE_ENTER(&softf->ipfr_natfrag);
	while ((fra = softf->ipfr_natlist) != NULL) {
		nat = fra->ipfr_data;
		if (nat != NULL) {
			if (nat->nat_data == fra)
				nat->nat_data = NULL;
		}
		fra->ipfr_ref--;
		ipf_frag_delete(softc, fra, &softf->ipfr_nattail);
	}
	softf->ipfr_nattail = &softf->ipfr_natlist;
	RWLOCK_EXIT(&softf->ipfr_natfrag);
	RWLOCK_EXIT(&softc->ipf_nat);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_expire                                             */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Expire entries in the fragment cache table that have been there too long */
/* ------------------------------------------------------------------------ */
void
ipf_frag_expire(softc)
	ipf_main_softc_t *softc;
{
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;
	ipfr_t	**fp, *fra;
	nat_t	*nat;
	SPL_INT(s);

	if (softf->ipfr_lock)
		return;

	SPL_NET(s);
	WRITE_ENTER(&softc->ipf_frag);
	/*
	 * Go through the entire table, looking for entries to expire,
	 * which is indicated by the ttl being less than or equal to ipf_ticks.
	 */
	for (fp = &softf->ipfr_list; ((fra = *fp) != NULL); ) {
		if (fra->ipfr_ttl > softc->ipf_ticks)
			break;
		fra->ipfr_ref--;
		ipf_frag_delete(softc, fra, &softf->ipfr_tail);
	}
	RWLOCK_EXIT(&softc->ipf_frag);

	WRITE_ENTER(&softf->ipfr_ipidfrag);
	for (fp = &softf->ipfr_ipidlist; ((fra = *fp) != NULL); ) {
		if (fra->ipfr_ttl > softc->ipf_ticks)
			break;
		fra->ipfr_ref--;
		ipf_frag_delete(softc, fra, &softf->ipfr_ipidtail);
	}
	RWLOCK_EXIT(&softf->ipfr_ipidfrag);

	/*
	 * Same again for the NAT table, except that if the structure also
	 * still points to a NAT structure, and the NAT structure points back
	 * at the one to be free'd, NULL the reference from the NAT struct.
	 * NOTE: We need to grab both mutex's early, and in this order so as
	 * to prevent a deadlock if both try to expire at the same time.
	 * The extra if() statement here is because it locks out all NAT
	 * operations - no need to do that if there are no entries in this
	 * list, right?
	 */
	if (softf->ipfr_natlist != NULL) {
		WRITE_ENTER(&softc->ipf_nat);
		WRITE_ENTER(&softf->ipfr_natfrag);
		for (fp = &softf->ipfr_natlist; ((fra = *fp) != NULL); ) {
			if (fra->ipfr_ttl > softc->ipf_ticks)
				break;
			nat = fra->ipfr_data;
			if (nat != NULL) {
				if (nat->nat_data == fra)
					nat->nat_data = NULL;
			}
			fra->ipfr_ref--;
			ipf_frag_delete(softc, fra, &softf->ipfr_nattail);
		}
		RWLOCK_EXIT(&softf->ipfr_natfrag);
		RWLOCK_EXIT(&softc->ipf_nat);
	}
	SPL_X(s);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_pkt_next                                           */
/* Returns:     int      - 0 == success, else error                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              token(I) - pointer to token information for this caller     */
/*              itp(I)   - pointer to generic iterator from caller          */
/*                                                                          */
/* This function is used to step through the fragment cache list used for   */
/* filter rules. The hard work is done by the more generic ipf_frag_next.   */
/* ------------------------------------------------------------------------ */
int
ipf_frag_pkt_next(softc, token, itp)
	ipf_main_softc_t *softc;
	ipftoken_t *token;
	ipfgeniter_t *itp;
{
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;

#ifdef USE_MUTEXES
	return ipf_frag_next(softc, token, itp, &softf->ipfr_list,
			     &softf->ipfr_frag);
#else
	return ipf_frag_next(softc, token, itp, &softf->ipfr_list);
#endif
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_nat_next                                           */
/* Returns:     int      - 0 == success, else error                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              token(I) - pointer to token information for this caller     */
/*              itp(I)   - pointer to generic iterator from caller          */
/*                                                                          */
/* This function is used to step through the fragment cache list used for   */
/* NAT. The hard work is done by the more generic ipf_frag_next.            */
/* ------------------------------------------------------------------------ */
int
ipf_frag_nat_next(softc, token, itp)
	ipf_main_softc_t *softc;
	ipftoken_t *token;
	ipfgeniter_t *itp;
{
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;;

#ifdef USE_MUTEXES
	return ipf_frag_next(softc, token, itp, &softf->ipfr_natlist, 
			     &softf->ipfr_natfrag);
#else
	return ipf_frag_next(softc, token, itp, &softf->ipfr_natlist);
#endif
}

/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_next                                               */
/* Returns:     int      - 0 == success, else error                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              token(I) - pointer to token information for this caller     */
/*              itp(I)   - pointer to generic iterator from caller          */
/*              top(I)   - top of the fragment list                         */
/*              lock(I)  - fragment cache lock                              */
/*                                                                          */
/* This function is used to interate through the list of entries in the     */
/* fragment cache.  It increases the reference count on the one currently   */
/* being returned so that the caller can come back and resume from it later.*/
/*                                                                          */
/* This function is used for both the NAT fragment cache as well as the ipf */
/* fragment cache - hence the reason for passing in top and lock.           */
/* ------------------------------------------------------------------------ */
static int
ipf_frag_next(softc, token, itp, top
#ifdef USE_MUTEXES
, lock
#endif
)
	ipf_main_softc_t *softc;
	ipftoken_t *token;
	ipfgeniter_t *itp;
	ipfr_t **top;
#ifdef USE_MUTEXES
	ipfrwlock_t *lock;
#endif
{
	ipfr_t *frag, *next, zero;
	int error = 0;

	if (itp->igi_data == NULL) {
		IPFERROR(20001);
		return EFAULT;
	}

	if (itp->igi_nitems != 1) {
		IPFERROR(20003);
		return EFAULT;
	}

	frag = token->ipt_data;

	READ_ENTER(lock);

	if (frag == NULL)
		next = *top;
	else
		next = frag->ipfr_next;

	if (next != NULL) {
		ATOMIC_INC(next->ipfr_ref);
		token->ipt_data = next;
	} else {
		bzero(&zero, sizeof(zero));
		next = &zero;
		token->ipt_data = NULL;
	}
	if (next->ipfr_next == NULL)
		ipf_token_mark_complete(token);

	RWLOCK_EXIT(lock);

	error = COPYOUT(next, itp->igi_data, sizeof(*next));
	if (error != 0)
		IPFERROR(20002);

        if (frag != NULL) {
#ifdef USE_MUTEXES
		ipf_frag_deref(softc, &frag, lock);
#else
		ipf_frag_deref(softc, &frag);
#endif
        }
        return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_pkt_deref                                          */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I)  - pointer to frag cache pointer                    */
/*                                                                          */
/* This function is the external interface for dropping a reference to a    */
/* fragment cache entry used by filter rules.                               */
/* ------------------------------------------------------------------------ */
void
ipf_frag_pkt_deref(softc, data)
	ipf_main_softc_t *softc;
	void *data;
{
	ipfr_t **frp = data;

#ifdef USE_MUTEXES
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;

	ipf_frag_deref(softc->ipf_frag_soft, frp, &softf->ipfr_frag);
#else
	ipf_frag_deref(softc->ipf_frag_soft, frp);
#endif
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_nat_deref                                          */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I)  - pointer to frag cache pointer                    */
/*                                                                          */
/* This function is the external interface for dropping a reference to a    */
/* fragment cache entry used by NAT table entries.                          */
/* ------------------------------------------------------------------------ */
void
ipf_frag_nat_deref(softc, data)
	ipf_main_softc_t *softc;
	void *data;
{
	ipfr_t **frp = data;

#ifdef USE_MUTEXES
	ipf_frag_softc_t *softf = softc->ipf_frag_soft;

	ipf_frag_deref(softc->ipf_frag_soft, frp, &softf->ipfr_natfrag);
#else
	ipf_frag_deref(softc->ipf_frag_soft, frp);
#endif
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_frag_deref                                              */
/* Returns:     Nil                                                         */
/* Parameters:  frp(IO) - pointer to fragment structure to deference        */
/*              lock(I) - lock associated with the fragment                 */
/*                                                                          */
/* This function dereferences a fragment structure (ipfr_t).  The pointer   */
/* passed in will always be reset back to NULL, even if the structure is    */
/* not freed, to enforce the notion that the caller is no longer entitled   */
/* to use the pointer it is dropping the reference to.                      */
/* ------------------------------------------------------------------------ */
static void
ipf_frag_deref(arg, frp
#ifdef USE_MUTEXES
, lock
#endif
)
	void *arg;
	ipfr_t **frp;
#ifdef USE_MUTEXES
	ipfrwlock_t *lock;
#endif
{
	ipf_frag_softc_t *softf = arg;
	ipfr_t *fra;

	fra = *frp;
	*frp = NULL;

	WRITE_ENTER(lock);
	fra->ipfr_ref--;
	if (fra->ipfr_ref <= 0)
		ipf_frag_free(softf, fra);
	RWLOCK_EXIT(lock);
}
