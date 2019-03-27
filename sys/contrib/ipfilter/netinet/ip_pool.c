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
#include <sys/file.h>
#if !defined(_KERNEL) && !defined(__KERNEL__)
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# define _KERNEL
# include <sys/uio.h>
# undef _KERNEL
#else
# include <sys/systm.h>
# if defined(NetBSD) && (__NetBSD_Version__ >= 104000000)
#  include <sys/proc.h>
# endif
#endif
#include <sys/time.h>
#if defined(_KERNEL) && !defined(SOLARIS2)
# include <sys/mbuf.h>
#endif
#if defined(__SVR4)
# include <sys/byteorder.h>
# ifdef _KERNEL
#  include <sys/dditypes.h>
# endif
# include <sys/stream.h>
# include <sys/kmem.h>
#endif
#if defined(__FreeBSD_version)
# include <sys/malloc.h>
#endif

#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#if !defined(_KERNEL)
# include "ipf.h"
#endif

#include "netinet/ip_compat.h"
#include "netinet/ip_fil.h"
#include "netinet/ip_pool.h"
#include "netinet/radix_ipf.h"

/* END OF INCLUDES */

#if !defined(lint)
static const char sccsid[] = "@(#)ip_fil.c	2.41 6/5/96 (C) 1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif

typedef struct ipf_pool_softc_s {
	void		*ipf_radix;
	ip_pool_t	*ipf_pool_list[LOOKUP_POOL_SZ];
	ipf_pool_stat_t	ipf_pool_stats;
	ip_pool_node_t	*ipf_node_explist;
} ipf_pool_softc_t;


static void ipf_pool_clearnodes __P((ipf_main_softc_t *, ipf_pool_softc_t *,
				     ip_pool_t *));
static int ipf_pool_create __P((ipf_main_softc_t *, ipf_pool_softc_t *, iplookupop_t *));
static int ipf_pool_deref __P((ipf_main_softc_t *, void *, void *));
static int ipf_pool_destroy __P((ipf_main_softc_t *, ipf_pool_softc_t *, int, char *));
static void *ipf_pool_exists __P((ipf_pool_softc_t *, int, char *));
static void *ipf_pool_find __P((void *, int, char *));
static ip_pool_node_t *ipf_pool_findeq __P((ipf_pool_softc_t *, ip_pool_t *,
					    addrfamily_t *, addrfamily_t *));
static void ipf_pool_free __P((ipf_main_softc_t *, ipf_pool_softc_t *,
			       ip_pool_t *));
static int ipf_pool_insert_node __P((ipf_main_softc_t *, ipf_pool_softc_t *,
				     ip_pool_t *, struct ip_pool_node *));
static int ipf_pool_iter_deref __P((ipf_main_softc_t *, void *, int, int, void *));
static int ipf_pool_iter_next __P((ipf_main_softc_t *,  void *, ipftoken_t *,
				   ipflookupiter_t *));
static size_t ipf_pool_flush __P((ipf_main_softc_t *, void *, iplookupflush_t *));
static int ipf_pool_node_add __P((ipf_main_softc_t *, void *, iplookupop_t *,
				  int));
static int ipf_pool_node_del __P((ipf_main_softc_t *, void *, iplookupop_t *,
				  int));
static void ipf_pool_node_deref __P((ipf_pool_softc_t *, ip_pool_node_t *));
static int ipf_pool_remove_node __P((ipf_main_softc_t *, ipf_pool_softc_t *,
				     ip_pool_t *, ip_pool_node_t *));
static int ipf_pool_search __P((ipf_main_softc_t *, void *, int,
				void *, u_int));
static void *ipf_pool_soft_create __P((ipf_main_softc_t *));
static void ipf_pool_soft_destroy __P((ipf_main_softc_t *, void *));
static void ipf_pool_soft_fini __P((ipf_main_softc_t *, void *));
static int ipf_pool_soft_init __P((ipf_main_softc_t *, void *));
static int ipf_pool_stats_get __P((ipf_main_softc_t *, void *, iplookupop_t *));
static int ipf_pool_table_add __P((ipf_main_softc_t *, void *, iplookupop_t *));
static int ipf_pool_table_del __P((ipf_main_softc_t *, void *, iplookupop_t *));
static void *ipf_pool_select_add_ref __P((void *, int, char *));
static void ipf_pool_expire __P((ipf_main_softc_t *, void *));

ipf_lookup_t ipf_pool_backend = {
	IPLT_POOL,
	ipf_pool_soft_create,
	ipf_pool_soft_destroy,
	ipf_pool_soft_init,
	ipf_pool_soft_fini,
	ipf_pool_search,
	ipf_pool_flush,
	ipf_pool_iter_deref,
	ipf_pool_iter_next,
	ipf_pool_node_add,
	ipf_pool_node_del,
	ipf_pool_stats_get,
	ipf_pool_table_add,
	ipf_pool_table_del,
	ipf_pool_deref,
	ipf_pool_find,
	ipf_pool_select_add_ref,
	NULL,
	ipf_pool_expire,
	NULL
};


#ifdef TEST_POOL
void treeprint __P((ip_pool_t *));

int
main(argc, argv)
	int argc;
	char *argv[];
{
	ip_pool_node_t node;
	addrfamily_t a, b;
	iplookupop_t op;
	ip_pool_t *ipo;
	i6addr_t ip;

	RWLOCK_INIT(softc->ipf_poolrw, "poolrw");
	ipf_pool_init();

	bzero((char *)&ip, sizeof(ip));
	bzero((char *)&op, sizeof(op));
	bzero((char *)&node, sizeof(node));
	strcpy(op.iplo_name, "0");

	if (ipf_pool_create(&op) == 0)
		ipo = ipf_pool_exists(0, "0");

	node.ipn_addr.adf_family = AF_INET;

	node.ipn_addr.adf_addr.in4.s_addr = 0x0a010203;
	node.ipn_mask.adf_addr.in4.s_addr = 0xffffffff;
	node.ipn_info = 1;
	ipf_pool_insert_node(ipo, &node);

	node.ipn_addr.adf_addr.in4.s_addr = 0x0a000000;
	node.ipn_mask.adf_addr.in4.s_addr = 0xff000000;
	node.ipn_info = 0;
	ipf_pool_insert_node(ipo, &node);

	node.ipn_addr.adf_addr.in4.s_addr = 0x0a010100;
	node.ipn_mask.adf_addr.in4.s_addr = 0xffffff00;
	node.ipn_info = 1;
	ipf_pool_insert_node(ipo, &node);

	node.ipn_addr.adf_addr.in4.s_addr = 0x0a010200;
	node.ipn_mask.adf_addr.in4.s_addr = 0xffffff00;
	node.ipn_info = 0;
	ipf_pool_insert_node(ipo, &node);

	node.ipn_addr.adf_addr.in4.s_addr = 0x0a010000;
	node.ipn_mask.adf_addr.in4.s_addr = 0xffff0000;
	node.ipn_info = 1;
	ipf_pool_insert_node(ipo, &node);

	node.ipn_addr.adf_addr.in4.s_addr = 0x0a01020f;
	node.ipn_mask.adf_addr.in4.s_addr = 0xffffffff;
	node.ipn_info = 1;
	ipf_pool_insert_node(ipo, &node);
#ifdef	DEBUG_POOL
	treeprint(ipo);
#endif
	ip.in4.s_addr = 0x0a00aabb;
	printf("search(%#x) = %d (0)\n", ip.in4.s_addr,
		ipf_pool_search(ipo, 4, &ip, 1));

	ip.in4.s_addr = 0x0a000001;
	printf("search(%#x) = %d (0)\n", ip.in4.s_addr,
		ipf_pool_search(ipo, 4, &ip, 1));

	ip.in4.s_addr = 0x0a000101;
	printf("search(%#x) = %d (0)\n", ip.in4.s_addr,
		ipf_pool_search(ipo, 4, &ip, 1));

	ip.in4.s_addr = 0x0a010001;
	printf("search(%#x) = %d (1)\n", ip.in4.s_addr,
		ipf_pool_search(ipo, 4, &ip, 1));

	ip.in4.s_addr = 0x0a010101;
	printf("search(%#x) = %d (1)\n", ip.in4.s_addr,
		ipf_pool_search(ipo, 4, &ip, 1));

	ip.in4.s_addr = 0x0a010201;
	printf("search(%#x) = %d (0)\n", ip.in4.s_addr,
		ipf_pool_search(ipo, 4, &ip, 1));

	ip.in4.s_addr = 0x0a010203;
	printf("search(%#x) = %d (1)\n", ip.in4.s_addr,
		ipf_pool_search(ipo, 4, &ip, 1));

	ip.in4.s_addr = 0x0a01020f;
	printf("search(%#x) = %d (1)\n", ip.in4.s_addr,
		ipf_pool_search(ipo, 4, &ip, 1));

	ip.in4.s_addr = 0x0b00aabb;
	printf("search(%#x) = %d (-1)\n", ip.in4.s_addr,
		ipf_pool_search(ipo, 4, &ip, 1));

#ifdef	DEBUG_POOL
	treeprint(ipo);
#endif

	ipf_pool_fini();

	return 0;
}


void
treeprint(ipo)
	ip_pool_t *ipo;
{
	ip_pool_node_t *c;

	for (c = ipo->ipo_list; c != NULL; c = c->ipn_next)
		printf("Node %p(%s) (%#x/%#x) = %d hits %lu\n",
			c, c->ipn_name, c->ipn_addr.adf_addr.in4.s_addr,
			c->ipn_mask.adf_addr.in4.s_addr,
			c->ipn_info, c->ipn_hits);
}
#endif /* TEST_POOL */


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_soft_create                                        */
/* Returns:     void *   - NULL = failure, else pointer to local context    */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Initialise the routing table data structures where required.             */
/* ------------------------------------------------------------------------ */
static void *
ipf_pool_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_pool_softc_t *softp;

	KMALLOC(softp, ipf_pool_softc_t *);
	if (softp == NULL) {
		IPFERROR(70032);
		return NULL;
	}

	bzero((char *)softp, sizeof(*softp));

	softp->ipf_radix = ipf_rx_create();
	if (softp->ipf_radix == NULL) {
		IPFERROR(70033);
		KFREE(softp);
		return NULL;
	}

	return softp;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_soft_init                                          */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*                                                                          */
/* Initialise the routing table data structures where required.             */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_soft_init(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_pool_softc_t *softp = arg;

	ipf_rx_init(softp->ipf_radix);

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_soft_fini                                          */
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
ipf_pool_soft_fini(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_pool_softc_t *softp = arg;
	ip_pool_t *p, *q;
	int i;

	softc = arg;

	for (i = -1; i <= IPL_LOGMAX; i++) {
		for (q = softp->ipf_pool_list[i + 1]; (p = q) != NULL; ) {
			q = p->ipo_next;
			(void) ipf_pool_destroy(softc, arg, i, p->ipo_name);
		}
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_soft_destroy                                       */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*                                                                          */
/* Clean up the pool by free'ing the radix tree associated with it and free */
/* up the pool context too.                                                 */
/* ------------------------------------------------------------------------ */
static void
ipf_pool_soft_destroy(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_pool_softc_t *softp = arg;

	ipf_rx_destroy(softp->ipf_radix);

	KFREE(softp);
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_pool_node_add                                            */
/* Returns:    int - 0 = success, else error                                */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*             arg(I)   - pointer to local context to use                   */
/*             op(I) - pointer to lookup operatin data                      */
/*                                                                          */
/* When adding a new node, a check is made to ensure that the address/mask  */
/* pair supplied has been appropriately prepared by applying the mask to    */
/* the address prior to calling for the pair to be added.                   */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_node_add(softc, arg, op, uid)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupop_t *op;
	int uid;
{
	ip_pool_node_t node, *m;
	ip_pool_t *p;
	int err;

	if (op->iplo_size != sizeof(node)) {
		IPFERROR(70014);
		return EINVAL;
	}

	err = COPYIN(op->iplo_struct, &node, sizeof(node));
	if (err != 0) {
		IPFERROR(70015);
		return EFAULT;
	}

	p = ipf_pool_find(arg, op->iplo_unit, op->iplo_name);
	if (p == NULL) {
		IPFERROR(70017);
		return ESRCH;
	}

	if (node.ipn_addr.adf_family == AF_INET) {
		if (node.ipn_addr.adf_len != offsetof(addrfamily_t, adf_addr) +
					     sizeof(struct in_addr)) {
			IPFERROR(70028);
			return EINVAL;
		}
	}
#ifdef USE_INET6
	else if (node.ipn_addr.adf_family == AF_INET6) {
		if (node.ipn_addr.adf_len != offsetof(addrfamily_t, adf_addr) +
					     sizeof(struct in6_addr)) {
			IPFERROR(70034);
			return EINVAL;
		}
	}
#endif
	if (node.ipn_mask.adf_len != node.ipn_addr.adf_len) {
		IPFERROR(70029);
		return EINVAL;
	}

	/*
	 * Check that the address/mask pair works.
	 */
	if (node.ipn_addr.adf_family == AF_INET) {
		if ((node.ipn_addr.adf_addr.in4.s_addr &
		     node.ipn_mask.adf_addr.in4.s_addr) !=
		    node.ipn_addr.adf_addr.in4.s_addr) {
			IPFERROR(70035);
			return EINVAL;
		}
	}
#ifdef USE_INET6
	else if (node.ipn_addr.adf_family == AF_INET6) {
		if (IP6_MASKNEQ(&node.ipn_addr.adf_addr.in6,
				&node.ipn_mask.adf_addr.in6,
				&node.ipn_addr.adf_addr.in6)) {
			IPFERROR(70036);
			return EINVAL;
		}
	}
#endif

	/*
	 * add an entry to a pool - return an error if it already
	 * exists remove an entry from a pool - if it exists
	 * - in both cases, the pool *must* exist!
	 */
	m = ipf_pool_findeq(arg, p, &node.ipn_addr, &node.ipn_mask);
	if (m != NULL) {
		IPFERROR(70018);
		return EEXIST;
	}
	err = ipf_pool_insert_node(softc, arg, p, &node);

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_pool_node_del                                            */
/* Returns:    int - 0 = success, else error                                */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*             arg(I)   - pointer to local context to use                   */
/*             op(I)    - pointer to lookup operatin data                   */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_node_del(softc, arg, op, uid)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupop_t *op;
	int uid;
{
	ip_pool_node_t node, *m;
	ip_pool_t *p;
	int err;


	if (op->iplo_size != sizeof(node)) {
		IPFERROR(70019);
		return EINVAL;
	}
	node.ipn_uid = uid;

	err = COPYIN(op->iplo_struct, &node, sizeof(node));
	if (err != 0) {
		IPFERROR(70020);
		return EFAULT;
	}

	if (node.ipn_addr.adf_family == AF_INET) {
		if (node.ipn_addr.adf_len != offsetof(addrfamily_t, adf_addr) +
					     sizeof(struct in_addr)) {
			IPFERROR(70030);
			return EINVAL;
		}
	}
#ifdef USE_INET6
	else if (node.ipn_addr.adf_family == AF_INET6) {
		if (node.ipn_addr.adf_len != offsetof(addrfamily_t, adf_addr) +
					     sizeof(struct in6_addr)) {
			IPFERROR(70037);
			return EINVAL;
		}
	}
#endif
	if (node.ipn_mask.adf_len != node.ipn_addr.adf_len) {
		IPFERROR(70031);
		return EINVAL;
	}

	p = ipf_pool_find(arg, op->iplo_unit, op->iplo_name);
	if (p == NULL) {
		IPFERROR(70021);
		return ESRCH;
	}

	m = ipf_pool_findeq(arg, p, &node.ipn_addr, &node.ipn_mask);
	if (m == NULL) {
		IPFERROR(70022);
		return ENOENT;
	}

	if ((uid != 0) && (uid != m->ipn_uid)) {
		IPFERROR(70024);
		return EACCES;
	}

	err = ipf_pool_remove_node(softc, arg, p, m);

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_pool_table_add                                           */
/* Returns:    int - 0 = success, else error                                */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*             arg(I)   - pointer to local context to use                   */
/*             op(I)    - pointer to lookup operatin data                   */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_table_add(softc, arg, op)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupop_t *op;
{
	int err;

	if (((op->iplo_arg & LOOKUP_ANON) == 0) &&
	    (ipf_pool_find(arg, op->iplo_unit, op->iplo_name) != NULL)) {
		IPFERROR(70023);
		err = EEXIST;
	} else {
		err = ipf_pool_create(softc, arg, op);
	}

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:   ipf_pool_table_del                                           */
/* Returns:    int - 0 = success, else error                                */
/* Parameters: softc(I) - pointer to soft context main structure            */
/*             arg(I)   - pointer to local context to use                   */
/*             op(I)    - pointer to lookup operatin data                   */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_table_del(softc, arg, op)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupop_t *op;
{
	return ipf_pool_destroy(softc, arg, op->iplo_unit, op->iplo_name);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_statistics                                         */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              op(I)    - pointer to lookup operatin data                  */
/*                                                                          */
/* Copy the current statistics out into user space, collecting pool list    */
/* pointers as appropriate for later use.                                   */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_stats_get(softc, arg, op)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupop_t *op;
{
	ipf_pool_softc_t *softp = arg;
	ipf_pool_stat_t stats;
	int unit, i, err = 0;

	if (op->iplo_size != sizeof(ipf_pool_stat_t)) {
		IPFERROR(70001);
		return EINVAL;
	}

	bcopy((char *)&softp->ipf_pool_stats, (char *)&stats, sizeof(stats));
	unit = op->iplo_unit;
	if (unit == IPL_LOGALL) {
		for (i = 0; i <= LOOKUP_POOL_MAX; i++)
			stats.ipls_list[i] = softp->ipf_pool_list[i];
	} else if (unit >= 0 && unit <= IPL_LOGMAX) {
		unit++;						/* -1 => 0 */
		if (op->iplo_name[0] != '\0')
			stats.ipls_list[unit] = ipf_pool_exists(softp, unit - 1,
								op->iplo_name);
		else
			stats.ipls_list[unit] = softp->ipf_pool_list[unit];
	} else {
		IPFERROR(70025);
		err = EINVAL;
	}
	if (err == 0) {
		err = COPYOUT(&stats, op->iplo_struct, sizeof(stats));
		if (err != 0) {
			IPFERROR(70026);
			return EFAULT;
		}
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_exists                                             */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softp(I) - pointer to soft context pool information         */
/*              unit(I)  - ipfilter device to which we are working on       */
/*              name(I)  - name of the pool                                 */
/*                                                                          */
/* Find a matching pool inside the collection of pools for a particular     */
/* device, indicated by the unit number.                                    */
/* ------------------------------------------------------------------------ */
static void *
ipf_pool_exists(softp, unit, name)
	ipf_pool_softc_t *softp;
	int unit;
	char *name;
{
	ip_pool_t *p;
	int i;

	if (unit == IPL_LOGALL) {
		for (i = 0; i <= LOOKUP_POOL_MAX; i++) {
			for (p = softp->ipf_pool_list[i]; p != NULL;
			     p = p->ipo_next) {
				if (strncmp(p->ipo_name, name,
					    sizeof(p->ipo_name)) == 0)
					break;
			}
			if (p != NULL)
				break;
		}
	} else {
		for (p = softp->ipf_pool_list[unit + 1]; p != NULL;
		     p = p->ipo_next)
			if (strncmp(p->ipo_name, name,
				    sizeof(p->ipo_name)) == 0)
				break;
	}
	return p;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_find                                               */
/* Returns:     int    - 0 = success, else error                            */
/* Parameters:  arg(I)  - pointer to local context to use                   */
/*              unit(I) - ipfilter device to which we are working on        */
/*              name(I)  - name of the pool                                 */
/*                                                                          */
/* Find a matching pool inside the collection of pools for a particular     */
/* device, indicated by the unit number.  If it is marked for deletion then */
/* pretend it does not exist.                                               */
/* ------------------------------------------------------------------------ */
static void *
ipf_pool_find(arg, unit, name)
	void *arg;
	int unit;
	char *name;
{
	ipf_pool_softc_t *softp = arg;
	ip_pool_t *p;

	p = ipf_pool_exists(softp, unit, name);
	if ((p != NULL) && (p->ipo_flags & IPOOL_DELETE))
		return NULL;

	return p;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_select_add_ref                                     */
/* Returns:     int - 0 = success, else error                               */
/* Parameters:  arg(I)  - pointer to local context to use                   */
/*              unit(I) - ipfilter device to which we are working on        */
/*              name(I)  - name of the pool                                 */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static void *
ipf_pool_select_add_ref(arg, unit, name)
	void *arg;
	int unit;
	char *name;
{
	ip_pool_t *p;

	p = ipf_pool_find(arg, -1, name);
	if (p == NULL)
		p = ipf_pool_find(arg, unit, name);
	if (p != NULL) {
		ATOMIC_INC32(p->ipo_ref);
	}
	return p;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_findeq                                             */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softp(I) - pointer to soft context pool information         */
/*              ipo(I)  - pointer to the pool getting the new node.         */
/*              addr(I) - pointer to address information to match on        */
/*              mask(I) - pointer to the address mask to match              */
/*                                                                          */
/* Searches for an exact match of an entry in the pool.                     */
/* ------------------------------------------------------------------------ */
extern void printhostmask __P((int, u_32_t *, u_32_t *));
static ip_pool_node_t *
ipf_pool_findeq(softp, ipo, addr, mask)
	ipf_pool_softc_t *softp;
	ip_pool_t *ipo;
	addrfamily_t *addr, *mask;
{
	ipf_rdx_node_t *n;

	n = ipo->ipo_head->lookup(ipo->ipo_head, addr, mask);
	return (ip_pool_node_t *)n;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_search                                             */
/* Returns:     int     - 0 == +ve match, -1 == error, 1 == -ve/no match    */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              tptr(I)    - pointer to the pool to search                  */
/*              version(I) - IP protocol version (4 or 6)                   */
/*              dptr(I)    - pointer to address information                 */
/*              bytes(I)   - length of packet                               */
/*                                                                          */
/* Search the pool for a given address and return a search result.          */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_search(softc, tptr, ipversion, dptr, bytes)
	ipf_main_softc_t *softc;
	void *tptr;
	int ipversion;
	void *dptr;
	u_int bytes;
{
	ipf_rdx_node_t *rn;
	ip_pool_node_t *m;
	i6addr_t *addr;
	addrfamily_t v;
	ip_pool_t *ipo;
	int rv;

	ipo = tptr;
	if (ipo == NULL)
		return -1;

	rv = 1;
	m = NULL;
	addr = (i6addr_t *)dptr;
	bzero(&v, sizeof(v));

	if (ipversion == 4) {
		v.adf_family = AF_INET;
		v.adf_len = offsetof(addrfamily_t, adf_addr) +
			    sizeof(struct in_addr);
		v.adf_addr.in4 = addr->in4;
#ifdef USE_INET6
	} else if (ipversion == 6) {
		v.adf_family = AF_INET6;
		v.adf_len = offsetof(addrfamily_t, adf_addr) +
			    sizeof(struct in6_addr);
		v.adf_addr.in6 = addr->in6;
#endif
	} else
		return -1;

	READ_ENTER(&softc->ipf_poolrw);

	rn = ipo->ipo_head->matchaddr(ipo->ipo_head, &v);

	if ((rn != NULL) && (rn->root == 0)) {
		m = (ip_pool_node_t *)rn;
		ipo->ipo_hits++;
		m->ipn_bytes += bytes;
		m->ipn_hits++;
		rv = m->ipn_info;
	}
	RWLOCK_EXIT(&softc->ipf_poolrw);
	return rv;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_insert_node                                        */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softp(I) - pointer to soft context pool information         */
/*              ipo(I)   - pointer to the pool getting the new node.        */
/*              node(I)  - structure with address/mask to add               */
/* Locks:       WRITE(ipf_poolrw)                                           */
/*                                                                          */
/* Add another node to the pool given by ipo.  The three parameters passed  */
/* in (addr, mask, info) shold all be stored in the node.                   */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_insert_node(softc, softp, ipo, node)
	ipf_main_softc_t *softc;
	ipf_pool_softc_t *softp;
	ip_pool_t *ipo;
	struct ip_pool_node *node;
{
	ipf_rdx_node_t *rn;
	ip_pool_node_t *x;

	if ((node->ipn_addr.adf_len > sizeof(*rn)) ||
	    (node->ipn_addr.adf_len < 4)) {
		IPFERROR(70003);
		return EINVAL;
	}

	if ((node->ipn_mask.adf_len > sizeof(*rn)) ||
	    (node->ipn_mask.adf_len < 4)) {
		IPFERROR(70004);
		return EINVAL;
	}

	KMALLOC(x, ip_pool_node_t *);
	if (x == NULL) {
		IPFERROR(70002);
		return ENOMEM;
	}

	*x = *node;
	bzero((char *)x->ipn_nodes, sizeof(x->ipn_nodes));
	x->ipn_owner = ipo;
	x->ipn_hits = 0;
	x->ipn_next = NULL;
	x->ipn_pnext = NULL;
	x->ipn_dnext = NULL;
	x->ipn_pdnext = NULL;

	if (x->ipn_die != 0) {
		/*
		 * If the new node has a given expiration time, insert it
		 * into the list of expiring nodes with the ones to be
		 * removed first added to the front of the list. The
		 * insertion is O(n) but it is kept sorted for quick scans
		 * at expiration interval checks.
		 */
		ip_pool_node_t *n;

		x->ipn_die = softc->ipf_ticks + IPF_TTLVAL(x->ipn_die);
		for (n = softp->ipf_node_explist; n != NULL; n = n->ipn_dnext) {
			if (x->ipn_die < n->ipn_die)
				break;
			if (n->ipn_dnext == NULL) {
				/*
				 * We've got to the last node and everything
				 * wanted to be expired before this new node,
				 * so we have to tack it on the end...
				 */
				n->ipn_dnext = x;
				x->ipn_pdnext = &n->ipn_dnext;
				n = NULL;
				break;
			}
		}

		if (softp->ipf_node_explist == NULL) {
			softp->ipf_node_explist = x;
			x->ipn_pdnext = &softp->ipf_node_explist;
		} else if (n != NULL) {
			x->ipn_dnext = n;
			x->ipn_pdnext = n->ipn_pdnext;
			n->ipn_pdnext = &x->ipn_dnext;
		}
	}

	rn = ipo->ipo_head->addaddr(ipo->ipo_head, &x->ipn_addr, &x->ipn_mask,
				    x->ipn_nodes);
#ifdef	DEBUG_POOL
	printf("Added %p at %p\n", x, rn);
#endif

	if (rn == NULL) {
		KFREE(x);
		IPFERROR(70005);
		return ENOMEM;
	}

	x->ipn_ref = 1;
	x->ipn_pnext = ipo->ipo_tail;
	*ipo->ipo_tail = x;
	ipo->ipo_tail = &x->ipn_next;

	softp->ipf_pool_stats.ipls_nodes++;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_create                                             */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softp(I) - pointer to soft context pool information         */
/*              op(I)    - pointer to iplookup struct with call details     */
/* Locks:       WRITE(ipf_poolrw)                                           */
/*                                                                          */
/* Creates a new group according to the paramters passed in via the         */
/* iplookupop structure.  Does not check to see if the group already exists */
/* when being inserted - assume this has already been done.  If the pool is */
/* marked as being anonymous, give it a new, unique, identifier.  Call any  */
/* other functions required to initialise the structure.                    */
/*                                                                          */
/* If the structure is flagged for deletion then reset the flag and return, */
/* as this likely means we've tried to free a pool that is in use (flush)   */
/* and now want to repopulate it with "new" data.                           */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_create(softc, softp, op)
	ipf_main_softc_t *softc;
	ipf_pool_softc_t *softp;
	iplookupop_t *op;
{
	char name[FR_GROUPLEN];
	int poolnum, unit;
	ip_pool_t *h;

	unit = op->iplo_unit;

	if ((op->iplo_arg & LOOKUP_ANON) == 0) {
		h = ipf_pool_exists(softp, unit, op->iplo_name);
		if (h != NULL) {
			if ((h->ipo_flags & IPOOL_DELETE) == 0) {
				IPFERROR(70006);
				return EEXIST;
			}
			h->ipo_flags &= ~IPOOL_DELETE;
			return 0;
		}
	}

	KMALLOC(h, ip_pool_t *);
	if (h == NULL) {
		IPFERROR(70007);
		return ENOMEM;
	}
	bzero(h, sizeof(*h));

	if (ipf_rx_inithead(softp->ipf_radix, &h->ipo_head) != 0) {
		KFREE(h);
		IPFERROR(70008);
		return ENOMEM;
	}

	if ((op->iplo_arg & LOOKUP_ANON) != 0) {
		ip_pool_t *p;

		h->ipo_flags |= IPOOL_ANON;
		poolnum = LOOKUP_ANON;

#if defined(SNPRINTF) && defined(_KERNEL)
		SNPRINTF(name, sizeof(name), "%x", poolnum);
#else
		(void)sprintf(name, "%x", poolnum);
#endif

		for (p = softp->ipf_pool_list[unit + 1]; p != NULL; ) {
			if (strncmp(name, p->ipo_name,
				    sizeof(p->ipo_name)) == 0) {
				poolnum++;
#if defined(SNPRINTF) && defined(_KERNEL)
				SNPRINTF(name, sizeof(name), "%x", poolnum);
#else
				(void)sprintf(name, "%x", poolnum);
#endif
				p = softp->ipf_pool_list[unit + 1];
			} else
				p = p->ipo_next;
		}

		(void)strncpy(h->ipo_name, name, sizeof(h->ipo_name));
		(void)strncpy(op->iplo_name, name, sizeof(op->iplo_name));
	} else {
		(void)strncpy(h->ipo_name, op->iplo_name, sizeof(h->ipo_name));
	}

	h->ipo_radix = softp->ipf_radix;
	h->ipo_ref = 1;
	h->ipo_list = NULL;
	h->ipo_tail = &h->ipo_list;
	h->ipo_unit = unit;
	h->ipo_next = softp->ipf_pool_list[unit + 1];
	if (softp->ipf_pool_list[unit + 1] != NULL)
		softp->ipf_pool_list[unit + 1]->ipo_pnext = &h->ipo_next;
	h->ipo_pnext = &softp->ipf_pool_list[unit + 1];
	softp->ipf_pool_list[unit + 1] = h;

	softp->ipf_pool_stats.ipls_pools++;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_remove_node                                        */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              ipo(I)   - pointer to the pool to remove the node from.     */
/*              ipe(I)   - address being deleted as a node                  */
/* Locks:       WRITE(ipf_poolrw)                                           */
/*                                                                          */
/* Remove a node from the pool given by ipo.                                */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_remove_node(softc, softp, ipo, ipe)
	ipf_main_softc_t *softc;
	ipf_pool_softc_t *softp;
	ip_pool_t *ipo;
	ip_pool_node_t *ipe;
{
	void *ptr;

	if (ipo->ipo_tail == &ipe->ipn_next)
		ipo->ipo_tail = ipe->ipn_pnext;

	if (ipe->ipn_pnext != NULL)
		*ipe->ipn_pnext = ipe->ipn_next;
	if (ipe->ipn_next != NULL)
		ipe->ipn_next->ipn_pnext = ipe->ipn_pnext;

	if (ipe->ipn_pdnext != NULL)
		*ipe->ipn_pdnext = ipe->ipn_dnext;
	if (ipe->ipn_dnext != NULL)
		ipe->ipn_dnext->ipn_pdnext = ipe->ipn_pdnext;

	ptr = ipo->ipo_head->deladdr(ipo->ipo_head, &ipe->ipn_addr,
				     &ipe->ipn_mask);

	if (ptr != NULL) {
		ipf_pool_node_deref(softp, ipe);
		return 0;
	}
	IPFERROR(70027);
	return ESRCH;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_destroy                                            */
/* Returns:     int    - 0 = success, else error                            */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softp(I) - pointer to soft context pool information         */
/*              unit(I)  - ipfilter device to which we are working on      */
/*              name(I)  - name of the pool                                 */
/* Locks:       WRITE(ipf_poolrw) or WRITE(ipf_global)                      */
/*                                                                          */
/* Search for a pool using paramters passed in and if it's not otherwise    */
/* busy, free it.  If it is busy, clear all of its nodes, mark it for being */
/* deleted and return an error saying it is busy.                           */
/*                                                                          */
/* NOTE: Because this function is called out of ipfdetach() where ipf_poolrw*/
/* may not be initialised, we can't use an ASSERT to enforce the locking    */
/* assertion that one of the two (ipf_poolrw,ipf_global) is held.           */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_destroy(softc, softp, unit, name)
	ipf_main_softc_t *softc;
	ipf_pool_softc_t *softp;
	int unit;
	char *name;
{
	ip_pool_t *ipo;

	ipo = ipf_pool_exists(softp, unit, name);
	if (ipo == NULL) {
		IPFERROR(70009);
		return ESRCH;
	}

	if (ipo->ipo_ref != 1) {
		ipf_pool_clearnodes(softc, softp, ipo);
		ipo->ipo_flags |= IPOOL_DELETE;
		return 0;
	}

	ipf_pool_free(softc, softp, ipo);
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_flush                                              */
/* Returns:     int    - number of pools deleted                            */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              fp(I)    - which pool(s) to flush                           */
/* Locks:       WRITE(ipf_poolrw) or WRITE(ipf_global)                      */
/*                                                                          */
/* Free all pools associated with the device that matches the unit number   */
/* passed in with operation.                                                */
/*                                                                          */
/* NOTE: Because this function is called out of ipfdetach() where ipf_poolrw*/
/* may not be initialised, we can't use an ASSERT to enforce the locking    */
/* assertion that one of the two (ipf_poolrw,ipf_global) is held.           */
/* ------------------------------------------------------------------------ */
static size_t
ipf_pool_flush(softc, arg, fp)
	ipf_main_softc_t *softc;
	void *arg;
	iplookupflush_t *fp;
{
	ipf_pool_softc_t *softp = arg;
	int i, num = 0, unit, err;
	ip_pool_t *p, *q;

	unit = fp->iplf_unit;
	for (i = -1; i <= IPL_LOGMAX; i++) {
		if (unit != IPLT_ALL && i != unit)
			continue;
		for (q = softp->ipf_pool_list[i + 1]; (p = q) != NULL; ) {
			q = p->ipo_next;
			err = ipf_pool_destroy(softc, softp, i, p->ipo_name);
			if (err == 0)
				num++;
		}
	}
	return num;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_free                                               */
/* Returns:     void                                                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softp(I) - pointer to soft context pool information         */
/*              ipo(I) - pointer to pool structure                          */
/* Locks:       WRITE(ipf_poolrw) or WRITE(ipf_global)                      */
/*                                                                          */
/* Deletes the pool strucutre passed in from the list of pools and deletes  */
/* all of the address information stored in it, including any tree data     */
/* structures also allocated.                                               */
/*                                                                          */
/* NOTE: Because this function is called out of ipfdetach() where ipf_poolrw*/
/* may not be initialised, we can't use an ASSERT to enforce the locking    */
/* assertion that one of the two (ipf_poolrw,ipf_global) is held.           */
/* ------------------------------------------------------------------------ */
static void
ipf_pool_free(softc, softp, ipo)
	ipf_main_softc_t *softc;
	ipf_pool_softc_t *softp;
	ip_pool_t *ipo;
{

	ipf_pool_clearnodes(softc, softp, ipo);

	if (ipo->ipo_next != NULL)
		ipo->ipo_next->ipo_pnext = ipo->ipo_pnext;
	*ipo->ipo_pnext = ipo->ipo_next;
	ipf_rx_freehead(ipo->ipo_head);
	KFREE(ipo);

	softp->ipf_pool_stats.ipls_pools--;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_clearnodes                                         */
/* Returns:     void                                                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              softp(I) - pointer to soft context pool information         */
/*              ipo(I)   - pointer to pool structure                        */
/* Locks:       WRITE(ipf_poolrw) or WRITE(ipf_global)                      */
/*                                                                          */
/* Deletes all nodes stored in a pool structure.                            */
/* ------------------------------------------------------------------------ */
static void
ipf_pool_clearnodes(softc, softp, ipo)
	ipf_main_softc_t *softc;
	ipf_pool_softc_t *softp;
	ip_pool_t *ipo;
{
	ip_pool_node_t *n, **next;

	for (next = &ipo->ipo_list; (n = *next) != NULL; )
		ipf_pool_remove_node(softc, softp, ipo, n);

	ipo->ipo_list = NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_deref                                              */
/* Returns:     void                                                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              pool(I)  - pointer to pool structure                        */
/* Locks:       WRITE(ipf_poolrw)                                           */
/*                                                                          */
/* Drop the number of known references to this pool structure by one and if */
/* we arrive at zero known references, free it.                             */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_deref(softc, arg, pool)
	ipf_main_softc_t *softc;
	void *arg, *pool;
{
	ip_pool_t *ipo = pool;

	ipo->ipo_ref--;

	if (ipo->ipo_ref == 0)
		ipf_pool_free(softc, arg, ipo);

	else if ((ipo->ipo_ref == 1) && (ipo->ipo_flags & IPOOL_DELETE))
		ipf_pool_destroy(softc, arg, ipo->ipo_unit, ipo->ipo_name);

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_node_deref                                         */
/* Returns:     void                                                        */
/* Parameters:  softp(I) - pointer to soft context pool information         */
/*              ipn(I)   - pointer to pool structure                        */
/* Locks:       WRITE(ipf_poolrw)                                           */
/*                                                                          */
/* Drop a reference to the pool node passed in and if we're the last, free  */
/* it all up and adjust the stats accordingly.                              */
/* ------------------------------------------------------------------------ */
static void
ipf_pool_node_deref(softp, ipn)
	ipf_pool_softc_t *softp;
	ip_pool_node_t *ipn;
{

	ipn->ipn_ref--;

	if (ipn->ipn_ref == 0) {
		KFREE(ipn);
		softp->ipf_pool_stats.ipls_nodes--;
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_iter_next                                          */
/* Returns:     void                                                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              token(I) - pointer to pool structure                        */
/*              ilp(IO)  - pointer to pool iterating structure              */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_iter_next(softc, arg, token, ilp)
	ipf_main_softc_t *softc;
	void *arg;
	ipftoken_t *token;
	ipflookupiter_t *ilp;
{
	ipf_pool_softc_t *softp = arg;
	ip_pool_node_t *node, zn, *nextnode;
	ip_pool_t *ipo, zp, *nextipo;
	void *pnext;
	int err;

	err = 0;
	node = NULL;
	nextnode = NULL;
	ipo = NULL;
	nextipo = NULL;

	READ_ENTER(&softc->ipf_poolrw);

	switch (ilp->ili_otype)
	{
	case IPFLOOKUPITER_LIST :
		ipo = token->ipt_data;
		if (ipo == NULL) {
			nextipo = softp->ipf_pool_list[(int)ilp->ili_unit + 1];
		} else {
			nextipo = ipo->ipo_next;
		}

		if (nextipo != NULL) {
			ATOMIC_INC32(nextipo->ipo_ref);
			token->ipt_data = nextipo;
		} else {
			bzero((char *)&zp, sizeof(zp));
			nextipo = &zp;
			token->ipt_data = NULL;
		}
		pnext = nextipo->ipo_next;
		break;

	case IPFLOOKUPITER_NODE :
		node = token->ipt_data;
		if (node == NULL) {
			ipo = ipf_pool_exists(arg, ilp->ili_unit,
					      ilp->ili_name);
			if (ipo == NULL) {
				IPFERROR(70010);
				err = ESRCH;
			} else {
				nextnode = ipo->ipo_list;
				ipo = NULL;
			}
		} else {
			nextnode = node->ipn_next;
		}

		if (nextnode != NULL) {
			ATOMIC_INC32(nextnode->ipn_ref);
			token->ipt_data = nextnode;
		} else {
			bzero((char *)&zn, sizeof(zn));
			nextnode = &zn;
			token->ipt_data = NULL;
		}
		pnext = nextnode->ipn_next;
		break;

	default :
		IPFERROR(70011);
		pnext = NULL;
		err = EINVAL;
		break;
	}

	RWLOCK_EXIT(&softc->ipf_poolrw);
	if (err != 0)
		return err;

	switch (ilp->ili_otype)
	{
	case IPFLOOKUPITER_LIST :
		err = COPYOUT(nextipo, ilp->ili_data, sizeof(*nextipo));
		if (err != 0)  {
			IPFERROR(70012);
			err = EFAULT;
		}
		if (ipo != NULL) {
			WRITE_ENTER(&softc->ipf_poolrw);
			ipf_pool_deref(softc, softp, ipo);
			RWLOCK_EXIT(&softc->ipf_poolrw);
		}
		break;

	case IPFLOOKUPITER_NODE :
		err = COPYOUT(nextnode, ilp->ili_data, sizeof(*nextnode));
		if (err != 0) {
			IPFERROR(70013);
			err = EFAULT;
		}
		if (node != NULL) {
			WRITE_ENTER(&softc->ipf_poolrw);
			ipf_pool_node_deref(softp, node);
			RWLOCK_EXIT(&softc->ipf_poolrw);
		}
		break;
	}
	if (pnext == NULL)
		ipf_token_mark_complete(token);

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_iterderef                                          */
/* Returns:     void                                                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              unit(I)  - ipfilter device to which we are working on       */
/* Locks:       WRITE(ipf_poolrw)                                           */
/*                                                                          */
/* ------------------------------------------------------------------------ */
static int
ipf_pool_iter_deref(softc, arg, otype, unit, data)
	ipf_main_softc_t *softc;
	void *arg;
	int otype;
	int unit;
	void *data;
{
	ipf_pool_softc_t *softp = arg;

	if (data == NULL)
		return EINVAL;

	if (unit < 0 || unit > IPL_LOGMAX)
		return EINVAL;

	switch (otype)
	{
	case IPFLOOKUPITER_LIST :
		ipf_pool_deref(softc, softp, (ip_pool_t *)data);
		break;

	case IPFLOOKUPITER_NODE :
		ipf_pool_node_deref(softp, (ip_pool_node_t *)data);
		break;
	default :
		break;
	}

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_pool_expire                                             */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*                                                                          */
/* At present this function exists just to support temporary addition of    */
/* nodes to the address pool.                                               */
/* ------------------------------------------------------------------------ */
static void
ipf_pool_expire(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_pool_softc_t *softp = arg;
	ip_pool_node_t *n;

	while ((n = softp->ipf_node_explist) != NULL) {
		/*
		 * Because the list is kept sorted on insertion, the fist
		 * one that dies in the future means no more work to do.
		 */
		if (n->ipn_die > softc->ipf_ticks)
			break;
		ipf_pool_remove_node(softc, softp, n->ipn_owner, n);
	}
}




#ifndef _KERNEL
void
ipf_pool_dump(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_pool_softc_t *softp = arg;
	ip_pool_t *ipl;
	int i;

	printf("List of configured pools\n");
	for (i = 0; i <= LOOKUP_POOL_MAX; i++)
		for (ipl = softp->ipf_pool_list[i]; ipl != NULL;
		     ipl = ipl->ipo_next)
			printpool(ipl, bcopywrap, NULL, opts, NULL);
}
#endif
