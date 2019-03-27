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
#include <sys/param.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/file.h>
#if !defined(_KERNEL)
# include <stdlib.h>
# include <string.h>
# define _KERNEL
# include <sys/uio.h>
# undef _KERNEL
#endif
#include <sys/socket.h>
#if defined(__FreeBSD_version)
# include <sys/malloc.h>
#endif
#if defined(__FreeBSD__)
#  include <sys/cdefs.h>
#  include <sys/proc.h>
#endif
#if !defined(__SVR4)
# include <sys/mbuf.h>
#endif
#if defined(_KERNEL)
# include <sys/systm.h>
#else
# include "ipf.h"
#endif
#include <netinet/in.h>
#include <net/if.h>

#include "netinet/ip_compat.h"
#include "netinet/ip_fil.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_htable.h"
/* END OF INCLUDES */

#if !defined(lint)
static const char rcsid[] = "@(#)$Id$";
#endif

# ifdef USE_INET6
static iphtent_t *ipf_iphmfind6 __P((iphtable_t *, i6addr_t *));
# endif
static iphtent_t *ipf_iphmfind __P((iphtable_t *, struct in_addr *));
static int ipf_iphmfindip __P((ipf_main_softc_t *, void *, int, void *, u_int));
static int ipf_htable_clear __P((ipf_main_softc_t *, void *, iphtable_t *));
static int ipf_htable_create __P((ipf_main_softc_t *, void *, iplookupop_t *));
static int ipf_htable_deref __P((ipf_main_softc_t *, void *, void *));
static int ipf_htable_destroy __P((ipf_main_softc_t *, void *, int, char *));
static void *ipf_htable_exists __P((void *, int, char *));
static size_t ipf_htable_flush __P((ipf_main_softc_t *, void *,
				    iplookupflush_t *));
static void ipf_htable_free __P((void *, iphtable_t *));
static int ipf_htable_iter_deref __P((ipf_main_softc_t *, void *, int,
				      int, void *));
static int ipf_htable_iter_next __P((ipf_main_softc_t *, void *, ipftoken_t *,
				     ipflookupiter_t *));
static int ipf_htable_node_add __P((ipf_main_softc_t *, void *,
				    iplookupop_t *, int));
static int ipf_htable_node_del __P((ipf_main_softc_t *, void *,
				    iplookupop_t *, int));
static int ipf_htable_remove __P((ipf_main_softc_t *, void *, iphtable_t *));
static void *ipf_htable_soft_create __P((ipf_main_softc_t *));
static void ipf_htable_soft_destroy __P((ipf_main_softc_t *, void *));
static int ipf_htable_soft_init __P((ipf_main_softc_t *, void *));
static void ipf_htable_soft_fini __P((ipf_main_softc_t *, void *));
static int ipf_htable_stats_get __P((ipf_main_softc_t *, void *,
				     iplookupop_t *));
static int ipf_htable_table_add __P((ipf_main_softc_t *, void *,
				     iplookupop_t *));
static int ipf_htable_table_del __P((ipf_main_softc_t *, void *,
				     iplookupop_t *));
static int ipf_htent_deref __P((void *, iphtent_t *));
static iphtent_t *ipf_htent_find __P((iphtable_t *, iphtent_t *));
static int ipf_htent_insert __P((ipf_main_softc_t *, void *, iphtable_t *,
				 iphtent_t *));
static int ipf_htent_remove __P((ipf_main_softc_t *, void *, iphtable_t *,
				 iphtent_t *));
static void *ipf_htable_select_add_ref __P((void *, int, char *));
static void ipf_htable_expire __P((ipf_main_softc_t *, void *));


typedef struct ipf_htable_softc_s {
	u_long		ipht_nomem[LOOKUP_POOL_SZ];
	u_long		ipf_nhtables[LOOKUP_POOL_SZ];
	u_long		ipf_nhtnodes[LOOKUP_POOL_SZ];
	iphtable_t	*ipf_htables[LOOKUP_POOL_SZ];
	iphtent_t	*ipf_node_explist;
} ipf_htable_softc_t;

ipf_lookup_t ipf_htable_backend = {
	IPLT_HASH,
	ipf_htable_soft_create,
	ipf_htable_soft_destroy,
	ipf_htable_soft_init,
	ipf_htable_soft_fini,
	ipf_iphmfindip,
	ipf_htable_flush,
	ipf_htable_iter_deref,
	ipf_htable_iter_next,
	ipf_htable_node_add,
	ipf_htable_node_del,
	ipf_htable_stats_get,
	ipf_htable_table_add,
	ipf_htable_table_del,
	ipf_htable_deref,
	ipf_htable_exists,
	ipf_htable_select_add_ref,
	NULL,
	ipf_htable_expire,
	NULL
};


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_soft_create                                      */
/* Returns:     void *   - NULL = failure, else pointer to local context    */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Initialise the routing table data structures where required.             */
/* ------------------------------------------------------------------------ */
static void *
ipf_htable_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_htable_softc_t *softh;

	KMALLOC(softh, ipf_htable_softc_t *);
	if (softh == NULL) {
		IPFERROR(30026);
		return NULL;
	}

	bzero((char *)softh, sizeof(*softh));

	return softh;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_soft_destroy                                     */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*                                                                          */
/* Clean up the pool by free'ing the radix tree associated with it and free */
/* up the pool context too.                                                 */
/* ------------------------------------------------------------------------ */
static void
ipf_htable_soft_destroy(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_htable_softc_t *softh = arg;

	KFREE(softh);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_soft_init                                        */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*                                                                          */
/* Initialise the hash table ready for use.                                 */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_soft_init(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_htable_softc_t *softh = arg;

	bzero((char *)softh, sizeof(*softh));

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_soft_fini                                        */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/* Locks:       WRITE(ipf_global)                                           */
/*                                                                          */
/* Clean up all the pool data structures allocated and call the cleanup     */
/* function for the radix tree that supports the pools. ipf_pool_destroy is */
/* used to delete the pools one by one to ensure they're properly freed up. */
/* ------------------------------------------------------------------------ */
static void
ipf_htable_soft_fini(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	iplookupflush_t fop;

	fop.iplf_type = IPLT_HASH;
	fop.iplf_unit = IPL_LOGALL;
	fop.iplf_arg = 0;
	fop.iplf_count = 0;
	*fop.iplf_name = '\0';
	ipf_htable_flush(softc, arg, &fop);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_stats_get                                        */
/* Returns:     int - 0 = success, else error                               */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              op(I)    - pointer to lookup operation data                 */
/*                                                                          */
/* Copy the relevant statistics out of internal structures and into the     */
/* structure used to export statistics.                                     */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_stats_get(softc, arg, op)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupop_t *op;
{
	ipf_htable_softc_t *softh = arg;
	iphtstat_t stats;
	int err;

	if (op->iplo_size != sizeof(stats)) {
		IPFERROR(30001);
		return EINVAL;
	}

	stats.iphs_tables = softh->ipf_htables[op->iplo_unit + 1];
	stats.iphs_numtables = softh->ipf_nhtables[op->iplo_unit + 1];
	stats.iphs_numnodes = softh->ipf_nhtnodes[op->iplo_unit + 1];
	stats.iphs_nomem = softh->ipht_nomem[op->iplo_unit + 1];

	err = COPYOUT(&stats, op->iplo_struct, sizeof(stats));
	if (err != 0) {
		IPFERROR(30013);
		return EFAULT;
	}
	return 0;

}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_create                                           */
/* Returns:     int - 0 = success, else error                               */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              op(I)    - pointer to lookup operation data                 */
/*                                                                          */
/* Create a new hash table using the template passed.                       */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_create(softc, arg, op)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupop_t *op;
{
	ipf_htable_softc_t *softh = arg;
	iphtable_t htab, *iph, *oiph;
	char name[FR_GROUPLEN];
	int err, i, unit;

	if (op->iplo_size != sizeof(htab)) {
		IPFERROR(30024);
		return EINVAL;
	}
	err = COPYIN(op->iplo_struct, &htab, sizeof(htab));
	if (err != 0) {
		IPFERROR(30003);
		return EFAULT;
	}

	unit = op->iplo_unit;
	if (htab.iph_unit != unit) {
		IPFERROR(30005);
		return EINVAL;
	}
	if (htab.iph_size < 1) {
		IPFERROR(30025);
		return EINVAL;
	}


	if ((op->iplo_arg & IPHASH_ANON) == 0) {
		iph = ipf_htable_exists(softh, unit, op->iplo_name);
		if (iph != NULL) {
			if ((iph->iph_flags & IPHASH_DELETE) == 0) {
				IPFERROR(30004);
				return EEXIST;
			}
			iph->iph_flags &= ~IPHASH_DELETE;
			iph->iph_ref++;
			return 0;
		}
	}

	KMALLOC(iph, iphtable_t *);
	if (iph == NULL) {
		softh->ipht_nomem[op->iplo_unit + 1]++;
		IPFERROR(30002);
		return ENOMEM;
	}
	*iph = htab;

	if ((op->iplo_arg & IPHASH_ANON) != 0) {
		i = IPHASH_ANON;
		do {
			i++;
#if defined(SNPRINTF) && defined(_KERNEL)
			SNPRINTF(name, sizeof(name), "%u", i);
#else
			(void)sprintf(name, "%u", i);
#endif
			for (oiph = softh->ipf_htables[unit + 1]; oiph != NULL;
			     oiph = oiph->iph_next)
				if (strncmp(oiph->iph_name, name,
					    sizeof(oiph->iph_name)) == 0)
					break;
		} while (oiph != NULL);

		(void)strncpy(iph->iph_name, name, sizeof(iph->iph_name));
		(void)strncpy(op->iplo_name, name, sizeof(op->iplo_name));
		iph->iph_type |= IPHASH_ANON;
	} else {
		(void)strncpy(iph->iph_name, op->iplo_name,
			      sizeof(iph->iph_name));
		iph->iph_name[sizeof(iph->iph_name) - 1] = '\0';
	}

	KMALLOCS(iph->iph_table, iphtent_t **,
		 iph->iph_size * sizeof(*iph->iph_table));
	if (iph->iph_table == NULL) {
		KFREE(iph);
		softh->ipht_nomem[unit + 1]++;
		IPFERROR(30006);
		return ENOMEM;
	}

	bzero((char *)iph->iph_table, iph->iph_size * sizeof(*iph->iph_table));
	iph->iph_maskset[0] = 0;
	iph->iph_maskset[1] = 0;
	iph->iph_maskset[2] = 0;
	iph->iph_maskset[3] = 0;

	iph->iph_ref = 1;
	iph->iph_list = NULL;
	iph->iph_tail = &iph->iph_list;
	iph->iph_next = softh->ipf_htables[unit + 1];
	iph->iph_pnext = &softh->ipf_htables[unit + 1];
	if (softh->ipf_htables[unit + 1] != NULL)
		softh->ipf_htables[unit + 1]->iph_pnext = &iph->iph_next;
	softh->ipf_htables[unit + 1] = iph;

	softh->ipf_nhtables[unit + 1]++;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_table_del                                        */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              op(I)    - pointer to lookup operation data                 */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_table_del(softc, arg, op)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupop_t *op;
{
	return ipf_htable_destroy(softc, arg, op->iplo_unit, op->iplo_name);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_destroy                                          */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              op(I)    - pointer to lookup operation data                 */
/*                                                                          */
/* Find the hash table that belongs to the relevant part of ipfilter with a */
/* matching name and attempt to destroy it.  If it is in use, empty it out  */
/* and mark it for deletion so that when all the references disappear, it   */
/* can be removed.                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_destroy(softc, arg, unit, name)
	ipf_main_softc_t *softc;
	void *arg;
	int unit;
	char *name;
{
	iphtable_t *iph;

	iph = ipf_htable_find(arg, unit, name);
	if (iph == NULL) {
		IPFERROR(30007);
		return ESRCH;
	}

	if (iph->iph_unit != unit) {
		IPFERROR(30008);
		return EINVAL;
	}

	if (iph->iph_ref != 0) {
		ipf_htable_clear(softc, arg, iph);
		iph->iph_flags |= IPHASH_DELETE;
		return 0;
	}

	ipf_htable_remove(softc, arg, iph);

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_clear                                            */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              iph(I)   - pointer to hash table to destroy                 */
/*                                                                          */
/* Clean out the hash table by walking the list of entries and removing     */
/* each one, one by one.                                                    */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_clear(softc, arg, iph)
	ipf_main_softc_t *softc;
	void *arg;
	iphtable_t *iph;
{
	iphtent_t *ipe;

	while ((ipe = iph->iph_list) != NULL)
		if (ipf_htent_remove(softc, arg, iph, ipe) != 0)
			return 1;
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_free                                             */
/* Returns:     Nil                                                         */
/* Parameters:  arg(I) - pointer to local context to use                    */
/*              iph(I) - pointer to hash table to destroy                   */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static void
ipf_htable_free(arg, iph)
	void *arg;
	iphtable_t *iph;
{
	ipf_htable_softc_t *softh = arg;

	if (iph->iph_next != NULL)
		iph->iph_next->iph_pnext = iph->iph_pnext;
	if (iph->iph_pnext != NULL)
		*iph->iph_pnext = iph->iph_next;
	iph->iph_pnext = NULL;
	iph->iph_next = NULL;

	softh->ipf_nhtables[iph->iph_unit + 1]--;

	KFREES(iph->iph_table, iph->iph_size * sizeof(*iph->iph_table));
	KFREE(iph);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_remove                                           */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              iph(I)   - pointer to hash table to destroy                 */
/*                                                                          */
/* It is necessary to unlink here as well as free (called by deref) so that */
/* the while loop in ipf_htable_flush() functions properly.                 */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_remove(softc, arg, iph)
	ipf_main_softc_t *softc;
	void *arg;
	iphtable_t *iph;
{

	if (ipf_htable_clear(softc, arg, iph) != 0)
		return 1;

	if (iph->iph_pnext != NULL)
		*iph->iph_pnext = iph->iph_next;
	if (iph->iph_next != NULL)
		iph->iph_next->iph_pnext = iph->iph_pnext;
	iph->iph_pnext = NULL;
	iph->iph_next = NULL;

	return ipf_htable_deref(softc, arg, iph);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_node_del                                         */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              op(I)    - pointer to lookup operation data                 */
/*              uid(I)   - real uid of process doing operation              */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_node_del(softc, arg, op, uid)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupop_t *op;
	int uid;
{
        iphtable_t *iph;
        iphtent_t hte, *ent;
	int err;

	if (op->iplo_size != sizeof(hte)) {
		IPFERROR(30014);
		return EINVAL;
	}

	err = COPYIN(op->iplo_struct, &hte, sizeof(hte));
	if (err != 0) {
		IPFERROR(30015);
		return EFAULT;
	}

	iph = ipf_htable_find(arg, op->iplo_unit, op->iplo_name);
	if (iph == NULL) {
		IPFERROR(30016);
		return ESRCH;
	}

	ent = ipf_htent_find(iph, &hte);
	if (ent == NULL) {
		IPFERROR(30022);
		return ESRCH;
	}

	if ((uid != 0) && (ent->ipe_uid != uid)) {
		IPFERROR(30023);
		return EACCES;
	}

	err = ipf_htent_remove(softc, arg, iph, ent);

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_node_del                                         */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              op(I)    - pointer to lookup operation data                 */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_table_add(softc, arg, op)
	ipf_main_softc_t *softc;
	void *arg;
        iplookupop_t *op;
{
	int err;

	if (ipf_htable_find(arg, op->iplo_unit, op->iplo_name) != NULL) {
		IPFERROR(30017);
		err = EEXIST;
	} else {
		err = ipf_htable_create(softc, arg, op);
	}

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htent_remove                                            */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              iph(I)   - pointer to hash table                            */
/*              ipe(I)   - pointer to hash table entry to remove            */
/*                                                                          */
/* Delete an entry from a hash table.                                       */
/* ------------------------------------------------------------------------ */
static int
ipf_htent_remove(softc, arg, iph, ipe)
	ipf_main_softc_t *softc;
	void *arg;
	iphtable_t *iph;
	iphtent_t *ipe;
{

	if (iph->iph_tail == &ipe->ipe_next)
		iph->iph_tail = ipe->ipe_pnext;

	if (ipe->ipe_hnext != NULL)
		ipe->ipe_hnext->ipe_phnext = ipe->ipe_phnext;
	if (ipe->ipe_phnext != NULL)
		*ipe->ipe_phnext = ipe->ipe_hnext;
	ipe->ipe_phnext = NULL;
	ipe->ipe_hnext = NULL;

	if (ipe->ipe_dnext != NULL)
		ipe->ipe_dnext->ipe_pdnext = ipe->ipe_pdnext;
	if (ipe->ipe_pdnext != NULL)
		*ipe->ipe_pdnext = ipe->ipe_dnext;
	ipe->ipe_pdnext = NULL;
	ipe->ipe_dnext = NULL;

	if (ipe->ipe_next != NULL)
		ipe->ipe_next->ipe_pnext = ipe->ipe_pnext;
	if (ipe->ipe_pnext != NULL)
		*ipe->ipe_pnext = ipe->ipe_next;
	ipe->ipe_pnext = NULL;
	ipe->ipe_next = NULL;

	switch (iph->iph_type & ~IPHASH_ANON)
	{
	case IPHASH_GROUPMAP :
		if (ipe->ipe_group != NULL)
			ipf_group_del(softc, ipe->ipe_ptr, NULL);
		break;

	default :
		ipe->ipe_ptr = NULL;
		ipe->ipe_value = 0;
		break;
	}

	return ipf_htent_deref(arg, ipe);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_deref                                            */
/* Returns:     int       - 0 = success, else error                         */
/* Parameters:  softc(I)  - pointer to soft context main structure          */
/*              arg(I)    - pointer to local context to use                 */
/*              object(I) - pointer to hash table                           */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_deref(softc, arg, object)
	ipf_main_softc_t *softc;
	void *arg, *object;
{
	ipf_htable_softc_t *softh = arg;
	iphtable_t *iph = object;
	int refs;

	iph->iph_ref--;
	refs = iph->iph_ref;

	if (iph->iph_ref == 0) {
		ipf_htable_free(softh, iph);
	}

	return refs;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htent_deref                                             */
/* Parameters:  arg(I) - pointer to local context to use                    */
/*              ipe(I) -                                                    */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_htent_deref(arg, ipe)
	void *arg;
	iphtent_t *ipe;
{
	ipf_htable_softc_t *softh = arg;

	ipe->ipe_ref--;
	if (ipe->ipe_ref == 0) {
		softh->ipf_nhtnodes[ipe->ipe_unit + 1]--;
		KFREE(ipe);

		return 0;
	}

	return ipe->ipe_ref;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_exists                                           */
/* Parameters:  arg(I) - pointer to local context to use                    */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static void *
ipf_htable_exists(arg, unit, name)
	void *arg;
	int unit;
	char *name;
{
	ipf_htable_softc_t *softh = arg;
	iphtable_t *iph;

	if (unit == IPL_LOGALL) {
		int i;

		for (i = 0; i <= LOOKUP_POOL_MAX; i++) {
			for (iph = softh->ipf_htables[i]; iph != NULL;
			     iph = iph->iph_next) {
				if (strncmp(iph->iph_name, name,
					    sizeof(iph->iph_name)) == 0)
					break;
			}
			if (iph != NULL)
				break;
		}
	} else {
		for (iph = softh->ipf_htables[unit + 1]; iph != NULL;
		     iph = iph->iph_next) {
			if (strncmp(iph->iph_name, name,
				    sizeof(iph->iph_name)) == 0)
				break;
		}
	}
	return iph;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_select_add_ref                                   */
/* Returns:     void *  - NULL = failure, else pointer to the hash table    */
/* Parameters:  arg(I)  - pointer to local context to use                   */
/*              unit(I) - ipfilter device to which we are working on        */
/*              name(I) - name of the hash table                            */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static void *
ipf_htable_select_add_ref(arg, unit, name)
	void *arg;
	int unit;
	char *name;
{
	iphtable_t *iph;

	iph = ipf_htable_exists(arg, unit, name);
	if (iph != NULL) {
		ATOMIC_INC32(iph->iph_ref);
	}
	return iph;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_find                                             */
/* Returns:     void *  - NULL = failure, else pointer to the hash table    */
/* Parameters:  arg(I)  - pointer to local context to use                   */
/*              unit(I) - ipfilter device to which we are working on        */
/*              name(I) - name of the hash table                            */
/*                                                                          */
/* This function is exposed becaues it is used in the group-map feature.    */
/* ------------------------------------------------------------------------ */
iphtable_t *
ipf_htable_find(arg, unit, name)
	void *arg;
	int unit;
	char *name;
{
	iphtable_t *iph;

	iph = ipf_htable_exists(arg, unit, name);
	if ((iph != NULL) && (iph->iph_flags & IPHASH_DELETE) == 0)
		return iph;

	return NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_flush                                            */
/* Returns:     size_t   - number of entries flushed                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              op(I)    - pointer to lookup operation data                 */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static size_t
ipf_htable_flush(softc, arg, op)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupflush_t *op;
{
	ipf_htable_softc_t *softh = arg;
	iphtable_t *iph;
	size_t freed;
	int i;

	freed = 0;

	for (i = -1; i <= IPL_LOGMAX; i++) {
		if (op->iplf_unit == i || op->iplf_unit == IPL_LOGALL) {
			while ((iph = softh->ipf_htables[i + 1]) != NULL) {
				if (ipf_htable_remove(softc, arg, iph) == 0) {
					freed++;
				} else {
					iph->iph_flags |= IPHASH_DELETE;
				}
			}
		}
	}

	return freed;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_node_add                                         */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              op(I)    - pointer to lookup operation data                 */
/*              uid(I)   - real uid of process doing operation              */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_node_add(softc, arg, op, uid)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupop_t *op;
	int uid;
{
	iphtable_t *iph;
	iphtent_t hte;
	int err;

	if (op->iplo_size != sizeof(hte)) {
		IPFERROR(30018);
		return EINVAL;
	}

	err = COPYIN(op->iplo_struct, &hte, sizeof(hte));
	if (err != 0) {
		IPFERROR(30019);
		return EFAULT;
	}
	hte.ipe_uid = uid;

	iph = ipf_htable_find(arg, op->iplo_unit, op->iplo_name);
	if (iph == NULL) {
		IPFERROR(30020);
		return ESRCH;
	}

	if (ipf_htent_find(iph, &hte) != NULL) {
		IPFERROR(30021);
		return EEXIST;
	}

	err = ipf_htent_insert(softc, arg, iph, &hte);

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htent_insert                                            */
/* Returns:     int      - 0 = success, -1 =  error                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              op(I)    - pointer to lookup operation data                 */
/*              ipeo(I)  -                                                  */
/*                                                                          */
/* Add an entry to a hash table.                                            */
/* ------------------------------------------------------------------------ */
static int
ipf_htent_insert(softc, arg, iph, ipeo)
	ipf_main_softc_t *softc;
	void *arg;
	iphtable_t *iph;
	iphtent_t *ipeo;
{
	ipf_htable_softc_t *softh = arg;
	iphtent_t *ipe;
	u_int hv;
	int bits;

	KMALLOC(ipe, iphtent_t *);
	if (ipe == NULL)
		return -1;

	bcopy((char *)ipeo, (char *)ipe, sizeof(*ipe));
	ipe->ipe_addr.i6[0] &= ipe->ipe_mask.i6[0];
	if (ipe->ipe_family == AF_INET) {
		bits = count4bits(ipe->ipe_mask.in4_addr);
		ipe->ipe_addr.i6[1] = 0;
		ipe->ipe_addr.i6[2] = 0;
		ipe->ipe_addr.i6[3] = 0;
		ipe->ipe_mask.i6[1] = 0;
		ipe->ipe_mask.i6[2] = 0;
		ipe->ipe_mask.i6[3] = 0;
		hv = IPE_V4_HASH_FN(ipe->ipe_addr.in4_addr,
				    ipe->ipe_mask.in4_addr, iph->iph_size);
	} else
#ifdef USE_INET6
	if (ipe->ipe_family == AF_INET6) {
		ipe->ipe_addr.i6[1] &= ipe->ipe_mask.i6[1];
		ipe->ipe_addr.i6[2] &= ipe->ipe_mask.i6[2];
		ipe->ipe_addr.i6[3] &= ipe->ipe_mask.i6[3];

		bits = count6bits(ipe->ipe_mask.i6);
		hv = IPE_V6_HASH_FN(ipe->ipe_addr.i6,
				    ipe->ipe_mask.i6, iph->iph_size);
	} else
#endif
	{
		KFREE(ipe);
		return -1;
	}

	ipe->ipe_owner = iph;
	ipe->ipe_ref = 1;
	ipe->ipe_hnext = iph->iph_table[hv];
	ipe->ipe_phnext = iph->iph_table + hv;

	if (iph->iph_table[hv] != NULL)
		iph->iph_table[hv]->ipe_phnext = &ipe->ipe_hnext;
	iph->iph_table[hv] = ipe;

	ipe->ipe_pnext = iph->iph_tail;
	*iph->iph_tail = ipe;
	iph->iph_tail = &ipe->ipe_next;
	ipe->ipe_next = NULL;

	if (ipe->ipe_die != 0) {
		/*
		 * If the new node has a given expiration time, insert it
		 * into the list of expiring nodes with the ones to be
		 * removed first added to the front of the list. The
		 * insertion is O(n) but it is kept sorted for quick scans
		 * at expiration interval checks.
		 */
		iphtent_t *n;

		ipe->ipe_die = softc->ipf_ticks + IPF_TTLVAL(ipe->ipe_die);
		for (n = softh->ipf_node_explist; n != NULL; n = n->ipe_dnext) {
			if (ipe->ipe_die < n->ipe_die)
				break;
			if (n->ipe_dnext == NULL) {
				/*
				 * We've got to the last node and everything
				 * wanted to be expired before this new node,
				 * so we have to tack it on the end...
				 */
				n->ipe_dnext = ipe;
				ipe->ipe_pdnext = &n->ipe_dnext;
				n = NULL;
				break;
			}
		}

		if (softh->ipf_node_explist == NULL) {
			softh->ipf_node_explist = ipe;
			ipe->ipe_pdnext = &softh->ipf_node_explist;
		} else if (n != NULL) {
			ipe->ipe_dnext = n;
			ipe->ipe_pdnext = n->ipe_pdnext;
			n->ipe_pdnext = &ipe->ipe_dnext;
		}
	}

	if (ipe->ipe_family == AF_INET) {
		ipf_inet_mask_add(bits, &iph->iph_v4_masks);
	}
#ifdef USE_INET6
	else if (ipe->ipe_family == AF_INET6) {
		ipf_inet6_mask_add(bits, &ipe->ipe_mask, &iph->iph_v6_masks);
	}
#endif

	switch (iph->iph_type & ~IPHASH_ANON)
	{
	case IPHASH_GROUPMAP :
		ipe->ipe_ptr = ipf_group_add(softc, ipe->ipe_group, NULL,
					   iph->iph_flags, IPL_LOGIPF,
					   softc->ipf_active);
		break;

	default :
		ipe->ipe_ptr = NULL;
		ipe->ipe_value = 0;
		break;
	}

	ipe->ipe_unit = iph->iph_unit;
	softh->ipf_nhtnodes[ipe->ipe_unit + 1]++;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htent_find                                              */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  iph(I)  - pointer to table to search                        */
/*              ipeo(I) - pointer to entry to find                          */
/*                                                                          */
/* While it isn't absolutely necessary to for the address and mask to be    */
/* passed in through an iphtent_t structure, one is always present when it  */
/* is time to call this function, so it is just more convenient.            */
/* ------------------------------------------------------------------------ */
static iphtent_t *
ipf_htent_find(iph, ipeo)
	iphtable_t *iph;
	iphtent_t *ipeo;
{
	iphtent_t ipe, *ent;
	u_int hv;
	int bits;

	bcopy((char *)ipeo, (char *)&ipe, sizeof(ipe));
	ipe.ipe_addr.i6[0] &= ipe.ipe_mask.i6[0];
	ipe.ipe_addr.i6[1] &= ipe.ipe_mask.i6[1];
	ipe.ipe_addr.i6[2] &= ipe.ipe_mask.i6[2];
	ipe.ipe_addr.i6[3] &= ipe.ipe_mask.i6[3];
	if (ipe.ipe_family == AF_INET) {
		bits = count4bits(ipe.ipe_mask.in4_addr);
		ipe.ipe_addr.i6[1] = 0;
		ipe.ipe_addr.i6[2] = 0;
		ipe.ipe_addr.i6[3] = 0;
		ipe.ipe_mask.i6[1] = 0;
		ipe.ipe_mask.i6[2] = 0;
		ipe.ipe_mask.i6[3] = 0;
		hv = IPE_V4_HASH_FN(ipe.ipe_addr.in4_addr,
				    ipe.ipe_mask.in4_addr, iph->iph_size);
	} else
#ifdef USE_INET6
	if (ipe.ipe_family == AF_INET6) {
		bits = count6bits(ipe.ipe_mask.i6);
		hv = IPE_V6_HASH_FN(ipe.ipe_addr.i6,
				    ipe.ipe_mask.i6, iph->iph_size);
	} else
#endif
		return NULL;

	for (ent = iph->iph_table[hv]; ent != NULL; ent = ent->ipe_hnext) {
		if (ent->ipe_family != ipe.ipe_family)
			continue;
		if (IP6_NEQ(&ipe.ipe_addr, &ent->ipe_addr))
			continue;
		if (IP6_NEQ(&ipe.ipe_mask, &ent->ipe_mask))
			continue;
		break;
	}

	return ent;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_iphmfindgroup                                           */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              tptr(I)  -                                                  */
/*              aptr(I)  -                                                  */
/*                                                                          */
/* Search a hash table for a matching entry and return the pointer stored   */
/* in it for use as the next group of rules to search.                      */
/*                                                                          */
/* This function is exposed becaues it is used in the group-map feature.    */
/* ------------------------------------------------------------------------ */
void *
ipf_iphmfindgroup(softc, tptr, aptr)
	ipf_main_softc_t *softc;
	void *tptr, *aptr;
{
	struct in_addr *addr;
	iphtable_t *iph;
	iphtent_t *ipe;
	void *rval;

	READ_ENTER(&softc->ipf_poolrw);
	iph = tptr;
	addr = aptr;

	ipe = ipf_iphmfind(iph, addr);
	if (ipe != NULL)
		rval = ipe->ipe_ptr;
	else
		rval = NULL;
	RWLOCK_EXIT(&softc->ipf_poolrw);
	return rval;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_iphmfindip                                              */
/* Returns:     int     - 0 == +ve match, -1 == error, 1 == -ve/no match    */
/* Parameters:  softc(I)     - pointer to soft context main structure       */
/*              tptr(I)      - pointer to the pool to search                */
/*              ipversion(I) - IP protocol version (4 or 6)                 */
/*              aptr(I)      - pointer to address information               */
/*              bytes(I)     - packet length                                */
/*                                                                          */
/* Search the hash table for a given address and return a search result.    */
/* ------------------------------------------------------------------------ */
static int
ipf_iphmfindip(softc, tptr, ipversion, aptr, bytes)
	ipf_main_softc_t *softc;
	void *tptr, *aptr;
	int ipversion;
	u_int bytes;
{
	struct in_addr *addr;
	iphtable_t *iph;
	iphtent_t *ipe;
	int rval;

	if (tptr == NULL || aptr == NULL)
		return -1;

	iph = tptr;
	addr = aptr;

	READ_ENTER(&softc->ipf_poolrw);
	if (ipversion == 4) {
		ipe = ipf_iphmfind(iph, addr);
#ifdef USE_INET6
	} else if (ipversion == 6) {
		ipe = ipf_iphmfind6(iph, (i6addr_t *)addr);
#endif
	} else {
		ipe = NULL;
	}

	if (ipe != NULL) {
		rval = 0;
		ipe->ipe_hits++;
		ipe->ipe_bytes += bytes;
	} else {
		rval = 1;
	}
	RWLOCK_EXIT(&softc->ipf_poolrw);
	return rval;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_iphmfindip                                              */
/* Parameters:  iph(I)  - pointer to hash table                             */
/*              addr(I) - pointer to IPv4 address                           */
/* Locks:  ipf_poolrw                                                       */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static iphtent_t *
ipf_iphmfind(iph, addr)
	iphtable_t *iph;
	struct in_addr *addr;
{
	u_32_t msk, ips;
	iphtent_t *ipe;
	u_int hv;
	int i;

	i = 0;
maskloop:
	msk = iph->iph_v4_masks.imt4_active[i];
	ips = addr->s_addr & msk;
	hv = IPE_V4_HASH_FN(ips, msk, iph->iph_size);
	for (ipe = iph->iph_table[hv]; (ipe != NULL); ipe = ipe->ipe_hnext) {
		if ((ipe->ipe_family != AF_INET) ||
		    (ipe->ipe_mask.in4_addr != msk) ||
		    (ipe->ipe_addr.in4_addr != ips)) {
			continue;
		}
		break;
	}

	if (ipe == NULL) {
		i++;
		if (i < iph->iph_v4_masks.imt4_max)
			goto maskloop;
	}
	return ipe;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_iter_next                                        */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              token(I) -                                                  */
/*              ilp(I)   -                                                  */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_iter_next(softc, arg, token, ilp)
	ipf_main_softc_t *softc;
	void *arg;
	ipftoken_t *token;
	ipflookupiter_t *ilp;
{
	ipf_htable_softc_t *softh = arg;
	iphtent_t *node, zn, *nextnode;
	iphtable_t *iph, zp, *nextiph;
	void *hnext;
	int err;

	err = 0;
	iph = NULL;
	node = NULL;
	nextiph = NULL;
	nextnode = NULL;

	READ_ENTER(&softc->ipf_poolrw);

	switch (ilp->ili_otype)
	{
	case IPFLOOKUPITER_LIST :
		iph = token->ipt_data;
		if (iph == NULL) {
			nextiph = softh->ipf_htables[(int)ilp->ili_unit + 1];
		} else {
			nextiph = iph->iph_next;
		}

		if (nextiph != NULL) {
			ATOMIC_INC(nextiph->iph_ref);
			token->ipt_data = nextiph;
		} else {
			bzero((char *)&zp, sizeof(zp));
			nextiph = &zp;
			token->ipt_data = NULL;
		}
		hnext = nextiph->iph_next;
		break;

	case IPFLOOKUPITER_NODE :
		node = token->ipt_data;
		if (node == NULL) {
			iph = ipf_htable_find(arg, ilp->ili_unit,
					      ilp->ili_name);
			if (iph == NULL) {
				IPFERROR(30009);
				err = ESRCH;
			} else {
				nextnode = iph->iph_list;
			}
		} else {
			nextnode = node->ipe_next;
		}

		if (nextnode != NULL) {
			ATOMIC_INC(nextnode->ipe_ref);
			token->ipt_data = nextnode;
		} else {
			bzero((char *)&zn, sizeof(zn));
			nextnode = &zn;
			token->ipt_data = NULL;
		}
		hnext = nextnode->ipe_next;
		break;

	default :
		IPFERROR(30010);
		err = EINVAL;
		hnext = NULL;
		break;
	}

	RWLOCK_EXIT(&softc->ipf_poolrw);
	if (err != 0)
		return err;

	switch (ilp->ili_otype)
	{
	case IPFLOOKUPITER_LIST :
		err = COPYOUT(nextiph, ilp->ili_data, sizeof(*nextiph));
		if (err != 0) {
			IPFERROR(30011);
			err = EFAULT;
		}
		if (iph != NULL) {
			WRITE_ENTER(&softc->ipf_poolrw);
			ipf_htable_deref(softc, softh, iph);
			RWLOCK_EXIT(&softc->ipf_poolrw);
		}
		break;

	case IPFLOOKUPITER_NODE :
		err = COPYOUT(nextnode, ilp->ili_data, sizeof(*nextnode));
		if (err != 0) {
			IPFERROR(30012);
			err = EFAULT;
		}
		if (node != NULL) {
			WRITE_ENTER(&softc->ipf_poolrw);
			ipf_htent_deref(softc, node);
			RWLOCK_EXIT(&softc->ipf_poolrw);
		}
		break;
	}

	if (hnext == NULL)
		ipf_token_mark_complete(token);

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_htable_iter_deref                                       */
/* Returns:     int      - 0 = success, else  error                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              otype(I) - which data structure type is being walked        */
/*              unit(I)  - ipfilter device to which we are working on       */
/*              data(I)  - pointer to old data structure                    */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_htable_iter_deref(softc, arg, otype, unit, data)
	ipf_main_softc_t *softc;
	void *arg;
	int otype;
	int unit;
	void *data;
{

	if (data == NULL)
		return EFAULT;

	if (unit < -1 || unit > IPL_LOGMAX)
		return EINVAL;

	switch (otype)
	{
	case IPFLOOKUPITER_LIST :
		ipf_htable_deref(softc, arg, (iphtable_t *)data);
		break;

	case IPFLOOKUPITER_NODE :
		ipf_htent_deref(arg, (iphtent_t *)data);
		break;
	default :
		break;
	}

	return 0;
}


#ifdef USE_INET6
/* ------------------------------------------------------------------------ */
/* Function:    ipf_iphmfind6                                               */
/* Parameters:  iph(I)  - pointer to hash table                             */
/*              addr(I) - pointer to IPv6 address                           */
/* Locks:  ipf_poolrw                                                       */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static iphtent_t *
ipf_iphmfind6(iph, addr)
	iphtable_t *iph;
	i6addr_t *addr;
{
	i6addr_t *msk, ips;
	iphtent_t *ipe;
	u_int hv;
	int i;

	i = 0;
maskloop:
	msk = iph->iph_v6_masks.imt6_active + i;
	ips.i6[0] = addr->i6[0] & msk->i6[0];
	ips.i6[1] = addr->i6[1] & msk->i6[1];
	ips.i6[2] = addr->i6[2] & msk->i6[2];
	ips.i6[3] = addr->i6[3] & msk->i6[3];
	hv = IPE_V6_HASH_FN(ips.i6, msk->i6, iph->iph_size);
	for (ipe = iph->iph_table[hv]; (ipe != NULL); ipe = ipe->ipe_next) {
		if ((ipe->ipe_family != AF_INET6) ||
		    IP6_NEQ(&ipe->ipe_mask, msk) ||
		    IP6_NEQ(&ipe->ipe_addr, &ips)) {
			continue;
		}
		break;
	}

	if (ipe == NULL) {
		i++;
		if (i < iph->iph_v6_masks.imt6_max)
			goto maskloop;
	}
	return ipe;
}
#endif


static void
ipf_htable_expire(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_htable_softc_t *softh = arg;
	iphtent_t *n;

	while ((n = softh->ipf_node_explist) != NULL) {
		if (n->ipe_die > softc->ipf_ticks)
			break;

		ipf_htent_remove(softc, softh, n->ipe_owner, n);
	}
}


#ifndef _KERNEL

/* ------------------------------------------------------------------------ */
/*                                                                          */
/* ------------------------------------------------------------------------ */
void
ipf_htable_dump(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_htable_softc_t *softh = arg;
	iphtable_t *iph;
	int i;

	printf("List of configured hash tables\n");
	for (i = 0; i < IPL_LOGSIZE; i++)
		for (iph = softh->ipf_htables[i]; iph != NULL;
		     iph = iph->iph_next)
			printhash(iph, bcopywrap, NULL, opts, NULL);

}
#endif
