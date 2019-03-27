/* $FreeBSD$ */
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
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/file.h>
#if defined(__FreeBSD_version) && defined(_KERNEL)
# include <sys/fcntl.h>
# include <sys/filio.h>
#else
# include <sys/ioctl.h>
#endif
#if !defined(_KERNEL)
# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# define _KERNEL
# include <sys/uio.h>
# undef _KERNEL
#endif
#include <sys/socket.h>
#include <net/if.h>
#if defined(__FreeBSD__)
# include <sys/cdefs.h>
# include <sys/proc.h>
#endif
#if defined(_KERNEL)
# include <sys/systm.h>
# if !defined(__SVR4)
#  include <sys/mbuf.h>
# endif
#else
# include "ipf.h"
#endif
#include <netinet/in.h>

#include "netinet/ip_compat.h"
#include "netinet/ip_fil.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_pool.h"
#include "netinet/ip_htable.h"
#include "netinet/ip_dstlist.h"
/* END OF INCLUDES */

#if !defined(lint)
static const char rcsid[] = "@(#)$Id$";
#endif

/*
 * In this file, ip_pool.c, ip_htable.c and ip_dstlist.c, you will find the
 * range for unit is [-1,IPL_LOGMAX]. The -1 is considered to be a valid number
 * and represents a "wildcard" or "all" units (IPL_LOGALL). The reason for not
 * starting the numbering at 0 is because the numbers [0,IPL_LOGMAX] correspond
 * to the minor device number for their respective device. Thus where there is
 * array indexing on the unit, +1 is used to map [-1.IPL_LOGMAX] to
 * [0.POOL_LOOKUP_MAX].
 */
static int ipf_lookup_addnode __P((ipf_main_softc_t *, caddr_t, int));
static int ipf_lookup_delnode __P((ipf_main_softc_t *, caddr_t, int));
static int ipf_lookup_addtable __P((ipf_main_softc_t *, caddr_t));
static int ipf_lookup_deltable __P((ipf_main_softc_t *, caddr_t));
static int ipf_lookup_stats __P((ipf_main_softc_t *, caddr_t));
static int ipf_lookup_flush __P((ipf_main_softc_t *, caddr_t));
static int ipf_lookup_iterate __P((ipf_main_softc_t *, void *, int, void *));
static int ipf_lookup_deltok __P((ipf_main_softc_t *, void *, int, void *));

#define	MAX_BACKENDS	3
static ipf_lookup_t *backends[MAX_BACKENDS] = {
	&ipf_pool_backend,
	&ipf_htable_backend,
	&ipf_dstlist_backend
};


typedef struct ipf_lookup_softc_s {
	void		*ipf_back[MAX_BACKENDS];
} ipf_lookup_softc_t;


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_init                                             */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Initialise all of the subcomponents of the lookup infrstructure.         */
/* ------------------------------------------------------------------------ */
void *
ipf_lookup_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_lookup_softc_t *softl;
	ipf_lookup_t **l;
	int i;

	KMALLOC(softl, ipf_lookup_softc_t *);
	if (softl == NULL)
		return NULL;

	bzero((char *)softl, sizeof(*softl));

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++) {
		softl->ipf_back[i] = (*(*l)->ipfl_create)(softc);
		if (softl->ipf_back[i] == NULL) {
			ipf_lookup_soft_destroy(softc, softl);
			return NULL;
		}
	}

	return softl;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_soft_init                                        */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*                                                                          */
/* Initialise all of the subcomponents of the lookup infrstructure.         */
/* ------------------------------------------------------------------------ */
int
ipf_lookup_soft_init(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_lookup_softc_t *softl = (ipf_lookup_softc_t *)arg;
	int err = 0;
	int i;

	for (i = 0; i < MAX_BACKENDS; i++) {
		err = (*backends[i]->ipfl_init)(softc, softl->ipf_back[i]);
		if (err != 0)
			break;
	}

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_soft_fini                                        */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*                                                                          */
/* Call the fini function in each backend to cleanup all allocated data.    */
/* ------------------------------------------------------------------------ */
int
ipf_lookup_soft_fini(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_lookup_softc_t *softl = (ipf_lookup_softc_t *)arg;
	int i;

	for (i = 0; i < MAX_BACKENDS; i++) {
		if (softl->ipf_back[i] != NULL)
			(*backends[i]->ipfl_fini)(softc,
						  softl->ipf_back[i]);
	}

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_expire                                           */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Step through each of the backends and call their expire functions,       */
/* allowing them to delete any lifetime limited data.                       */
/* ------------------------------------------------------------------------ */
void
ipf_lookup_expire(softc)
	ipf_main_softc_t *softc;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	int i;

	WRITE_ENTER(&softc->ipf_poolrw);
	for (i = 0; i < MAX_BACKENDS; i++)
		(*backends[i]->ipfl_expire)(softc, softl->ipf_back[i]);
	RWLOCK_EXIT(&softc->ipf_poolrw);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_softc_destroy                                    */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*                                                                          */
/* Free up all pool related memory that has been allocated whilst IPFilter  */
/* has been running.  Also, do any other deinitialisation required such     */
/* ipf_lookup_init() can be called again, safely.                           */
/* ------------------------------------------------------------------------ */
void
ipf_lookup_soft_destroy(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_lookup_softc_t *softl = (ipf_lookup_softc_t *)arg;
	int i;

	for (i = 0; i < MAX_BACKENDS; i++) {
		if (softl->ipf_back[i] != NULL)
			(*backends[i]->ipfl_destroy)(softc,
						     softl->ipf_back[i]);
	}

	KFREE(softl);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_ioctl                                            */
/* Returns:     int      - 0 = success, else error                          */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to local context to use                  */
/*              data(IO) - pointer to ioctl data to be copied to/from user  */
/*                         space.                                           */
/*              cmd(I)   - ioctl command number                             */
/*              mode(I)  - file mode bits used with open                    */
/*              uid(I)   - uid of process doing ioctl                       */
/*              ctx(I)   - pointer that represents context for uid          */
/*                                                                          */
/* Handle ioctl commands sent to the ioctl device.  For the most part, this */
/* involves just calling another function to handle the specifics of each   */
/* command.                                                                 */
/* ------------------------------------------------------------------------ */
int
ipf_lookup_ioctl(softc, data, cmd, mode, uid, ctx)
	ipf_main_softc_t *softc;
	caddr_t data;
	ioctlcmd_t cmd;
	int mode, uid;
	void *ctx;
{
	int err;
	SPL_INT(s);

	mode = mode;	/* LINT */

	SPL_NET(s);

	switch (cmd)
	{
	case SIOCLOOKUPADDNODE :
	case SIOCLOOKUPADDNODEW :
		WRITE_ENTER(&softc->ipf_poolrw);
		err = ipf_lookup_addnode(softc, data, uid);
		RWLOCK_EXIT(&softc->ipf_poolrw);
		break;

	case SIOCLOOKUPDELNODE :
	case SIOCLOOKUPDELNODEW :
		WRITE_ENTER(&softc->ipf_poolrw);
		err = ipf_lookup_delnode(softc, data, uid);
		RWLOCK_EXIT(&softc->ipf_poolrw);
		break;

	case SIOCLOOKUPADDTABLE :
		WRITE_ENTER(&softc->ipf_poolrw);
		err = ipf_lookup_addtable(softc, data);
		RWLOCK_EXIT(&softc->ipf_poolrw);
		break;

	case SIOCLOOKUPDELTABLE :
		WRITE_ENTER(&softc->ipf_poolrw);
		err = ipf_lookup_deltable(softc, data);
		RWLOCK_EXIT(&softc->ipf_poolrw);
		break;

	case SIOCLOOKUPSTAT :
	case SIOCLOOKUPSTATW :
		WRITE_ENTER(&softc->ipf_poolrw);
		err = ipf_lookup_stats(softc, data);
		RWLOCK_EXIT(&softc->ipf_poolrw);
		break;

	case SIOCLOOKUPFLUSH :
		WRITE_ENTER(&softc->ipf_poolrw);
		err = ipf_lookup_flush(softc, data);
		RWLOCK_EXIT(&softc->ipf_poolrw);
		break;

	case SIOCLOOKUPITER :
		err = ipf_lookup_iterate(softc, data, uid, ctx);
		break;

	case SIOCIPFDELTOK :
		err = ipf_lookup_deltok(softc, data, uid, ctx);
		break;

	default :
		IPFERROR(50001);
		err = EINVAL;
		break;
	}
	SPL_X(s);
	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_addnode                                          */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I) - pointer to data from ioctl call                   */
/*                                                                          */
/* Add a new data node to a lookup structure.  First, check to see if the   */
/* parent structure refered to by name exists and if it does, then go on to */
/* add a node to it.                                                        */
/* ------------------------------------------------------------------------ */
static int
ipf_lookup_addnode(softc, data, uid)
	ipf_main_softc_t *softc;
	caddr_t data;
	int uid;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	iplookupop_t op;
	ipf_lookup_t **l;
	int err;
	int i;

	err = BCOPYIN(data, &op, sizeof(op));
	if (err != 0) {
		IPFERROR(50002);
		return EFAULT;
	}

	if ((op.iplo_unit < 0 || op.iplo_unit > IPL_LOGMAX) &&
	    (op.iplo_unit != IPLT_ALL)) {
		IPFERROR(50003);
		return EINVAL;
	}

	op.iplo_name[sizeof(op.iplo_name) - 1] = '\0';

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++) {
		if (op.iplo_type == (*l)->ipfl_type) {
			err = (*(*l)->ipfl_node_add)(softc,
						     softl->ipf_back[i],
						     &op, uid);
			break;
		}
	}

	if (i == MAX_BACKENDS) {
		IPFERROR(50012);
		err = EINVAL;
	}

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_delnode                                          */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I) - pointer to data from ioctl call                   */
/*                                                                          */
/* Delete a node from a lookup table by first looking for the table it is   */
/* in and then deleting the entry that gets found.                          */
/* ------------------------------------------------------------------------ */
static int
ipf_lookup_delnode(softc, data, uid)
	ipf_main_softc_t *softc;
	caddr_t data;
	int uid;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	iplookupop_t op;
	ipf_lookup_t **l;
	int err;
	int i;

	err = BCOPYIN(data, &op, sizeof(op));
	if (err != 0) {
		IPFERROR(50042);
		return EFAULT;
	}

	if ((op.iplo_unit < 0 || op.iplo_unit > IPL_LOGMAX) &&
	    (op.iplo_unit != IPLT_ALL)) {
		IPFERROR(50013);
		return EINVAL;
	}

	op.iplo_name[sizeof(op.iplo_name) - 1] = '\0';

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++) {
		if (op.iplo_type == (*l)->ipfl_type) {
			err = (*(*l)->ipfl_node_del)(softc, softl->ipf_back[i],
						     &op, uid);
			break;
		}
	}

	if (i == MAX_BACKENDS) {
		IPFERROR(50021);
		err = EINVAL;
	}
	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_addtable                                         */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I) - pointer to data from ioctl call                   */
/*                                                                          */
/* Create a new lookup table, if one doesn't already exist using the name   */
/* for this one.                                                            */
/* ------------------------------------------------------------------------ */
static int
ipf_lookup_addtable(softc, data)
	ipf_main_softc_t *softc;
	caddr_t data;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	iplookupop_t op;
	ipf_lookup_t **l;
	int err, i;

	err = BCOPYIN(data, &op, sizeof(op));
	if (err != 0) {
		IPFERROR(50022);
		return EFAULT;
	}

	if ((op.iplo_unit < 0 || op.iplo_unit > IPL_LOGMAX) &&
	    (op.iplo_unit != IPLT_ALL)) {
		IPFERROR(50023);
		return EINVAL;
	}

	op.iplo_name[sizeof(op.iplo_name) - 1] = '\0';

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++) {
		if (op.iplo_type == (*l)->ipfl_type) {
			err = (*(*l)->ipfl_table_add)(softc,
						      softl->ipf_back[i],
						      &op);
			break;
		}
	}

	if (i == MAX_BACKENDS) {
		IPFERROR(50026);
		err = EINVAL;
	}

	/*
	 * For anonymous pools, copy back the operation struct because in the
	 * case of success it will contain the new table's name.
	 */
	if ((err == 0) && ((op.iplo_arg & LOOKUP_ANON) != 0)) {
		err = BCOPYOUT(&op, data, sizeof(op));
		if (err != 0) {
			IPFERROR(50027);
			err = EFAULT;
		}
	}

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_deltable                                         */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I) - pointer to data from ioctl call                   */
/*                                                                          */
/* Decodes ioctl request to remove a particular hash table or pool and      */
/* calls the relevant function to do the cleanup.                           */
/* ------------------------------------------------------------------------ */
static int
ipf_lookup_deltable(softc, data)
	ipf_main_softc_t *softc;
	caddr_t data;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	iplookupop_t op;
	ipf_lookup_t **l;
	int err, i;

	err = BCOPYIN(data, &op, sizeof(op));
	if (err != 0) {
		IPFERROR(50028);
		return EFAULT;
	}

	if ((op.iplo_unit < 0 || op.iplo_unit > IPL_LOGMAX) &&
	    (op.iplo_unit != IPLT_ALL)) {
		IPFERROR(50029);
		return EINVAL;
	}

	op.iplo_name[sizeof(op.iplo_name) - 1] = '\0';

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++) {
		if (op.iplo_type == (*l)->ipfl_type) {
			err = (*(*l)->ipfl_table_del)(softc,
						      softl->ipf_back[i],
						      &op);
			break;
		}
	}

	if (i == MAX_BACKENDS) {
		IPFERROR(50030);
		err = EINVAL;
	}
	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_stats                                            */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I) - pointer to data from ioctl call                   */
/*                                                                          */
/* Copy statistical information from inside the kernel back to user space.  */
/* ------------------------------------------------------------------------ */
static int
ipf_lookup_stats(softc, data)
	ipf_main_softc_t *softc;
	caddr_t data;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	iplookupop_t op;
	ipf_lookup_t **l;
	int err;
	int i;

	err = BCOPYIN(data, &op, sizeof(op));
	if (err != 0) {
		IPFERROR(50031);
		return EFAULT;
	}

	if ((op.iplo_unit < 0 || op.iplo_unit > IPL_LOGMAX) &&
	    (op.iplo_unit != IPLT_ALL)) {
		IPFERROR(50032);
		return EINVAL;
	}

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++) {
		if (op.iplo_type == (*l)->ipfl_type) {
			err = (*(*l)->ipfl_stats_get)(softc,
						      softl->ipf_back[i],
						      &op);
			break;
		}
	}

	if (i == MAX_BACKENDS) {
		IPFERROR(50033);
		err = EINVAL;
	}

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_flush                                            */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I) - pointer to data from ioctl call                   */
/*                                                                          */
/* A flush is called when we want to flush all the nodes from a particular  */
/* entry in the hash table/pool or want to remove all groups from those.    */
/* ------------------------------------------------------------------------ */
static int
ipf_lookup_flush(softc, data)
	ipf_main_softc_t *softc;
	caddr_t data;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	int err, unit, num, type, i;
	iplookupflush_t flush;
	ipf_lookup_t **l;

	err = BCOPYIN(data, &flush, sizeof(flush));
	if (err != 0) {
		IPFERROR(50034);
		return EFAULT;
	}

	unit = flush.iplf_unit;
	if ((unit < 0 || unit > IPL_LOGMAX) && (unit != IPLT_ALL)) {
		IPFERROR(50035);
		return EINVAL;
	}

	flush.iplf_name[sizeof(flush.iplf_name) - 1] = '\0';

	type = flush.iplf_type;
	IPFERROR(50036);
	err = EINVAL;
	num = 0;

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++) {
		if (type == (*l)->ipfl_type || type == IPLT_ALL) {
			err = 0;
			num += (*(*l)->ipfl_flush)(softc,
						   softl->ipf_back[i],
						   &flush);
		}
	}

	if (err == 0) {
		flush.iplf_count = num;
		err = BCOPYOUT(&flush, data, sizeof(flush));
		if (err != 0) {
			IPFERROR(50037);
			err = EFAULT;
		}
	}
	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_delref                                           */
/* Returns:     void                                                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              type(I) - table type to operate on                          */
/*              ptr(I)  - pointer to object to remove reference for         */
/*                                                                          */
/* This function organises calling the correct deref function for a given   */
/* type of object being passed into it.                                     */
/* ------------------------------------------------------------------------ */
void
ipf_lookup_deref(softc, type, ptr)
	ipf_main_softc_t *softc;
	int type;
	void *ptr;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	int i;

	if (ptr == NULL)
		return;

	for (i = 0; i < MAX_BACKENDS; i++) {
		if (type == backends[i]->ipfl_type) {
			WRITE_ENTER(&softc->ipf_poolrw);
			(*backends[i]->ipfl_table_deref)(softc,
							 softl->ipf_back[i],
							 ptr);
			RWLOCK_EXIT(&softc->ipf_poolrw);
			break;
		}
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_iterate                                          */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I) - pointer to data from ioctl call                   */
/*              uid(I)  - uid of caller                                     */
/*              ctx(I)  - pointer to give the uid context                   */
/*                                                                          */
/* Decodes ioctl request to step through either hash tables or pools.       */
/* ------------------------------------------------------------------------ */
static int
ipf_lookup_iterate(softc, data, uid, ctx)
	ipf_main_softc_t *softc;
	void *data;
	int uid;
	void *ctx;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	ipflookupiter_t iter;
	ipftoken_t *token;
	int err, i;
	SPL_INT(s);

	err = ipf_inobj(softc, data, NULL, &iter, IPFOBJ_LOOKUPITER);
	if (err != 0)
		return err;

	if (iter.ili_unit < IPL_LOGALL && iter.ili_unit > IPL_LOGMAX) {
		IPFERROR(50038);
		return EINVAL;
	}

	if (iter.ili_ival != IPFGENITER_LOOKUP) {
		IPFERROR(50039);
		return EINVAL;
	}

	SPL_SCHED(s);
	token = ipf_token_find(softc, iter.ili_key, uid, ctx);
	if (token == NULL) {
		SPL_X(s);
		IPFERROR(50040);
		return ESRCH;
	}

	for (i = 0; i < MAX_BACKENDS; i++) {
		if (iter.ili_type == backends[i]->ipfl_type) {
			err = (*backends[i]->ipfl_iter_next)(softc,
							     softl->ipf_back[i],
							     token, &iter);
			break;
		}
	}
	SPL_X(s);

	if (i == MAX_BACKENDS) {
		IPFERROR(50041);
		err = EINVAL;
	}

	WRITE_ENTER(&softc->ipf_tokens);
	ipf_token_deref(softc, token);
	RWLOCK_EXIT(&softc->ipf_tokens);

	return err;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_iterderef                                        */
/* Returns:     void                                                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              type(I)  - backend type to iterate through                  */
/*              data(I)  - pointer to data from ioctl call                  */
/*                                                                          */
/* Decodes ioctl request to remove a particular hash table or pool and      */
/* calls the relevant function to do the cleanup.                           */
/* Because each of the backend types has a different data structure,        */
/* iteration is limited to one type at a time (i.e. it is not permitted to  */
/* go on from pool types to hash types as part of the "get next".)          */
/* ------------------------------------------------------------------------ */
void
ipf_lookup_iterderef(softc, type, data)
	ipf_main_softc_t *softc;
	u_32_t type;
	void *data;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	struct iplookupiterkey *lkey;
	iplookupiterkey_t key;
	int i;

	key.ilik_key = type;
	lkey = &key.ilik_unstr;

	if (lkey->ilik_ival != IPFGENITER_LOOKUP)
		return;

	WRITE_ENTER(&softc->ipf_poolrw);

	for (i = 0; i < MAX_BACKENDS; i++) {
		if (lkey->ilik_type == backends[i]->ipfl_type) {
			(*backends[i]->ipfl_iter_deref)(softc,
							softl->ipf_back[i],
							lkey->ilik_otype,
							lkey->ilik_unit,
							data);
			break;
		}
	}
	RWLOCK_EXIT(&softc->ipf_poolrw);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_deltok                                           */
/* Returns:     int     - 0 = success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I) - pointer to data from ioctl call                   */
/*              uid(I)  - uid of caller                                     */
/*              ctx(I)  - pointer to give the uid context                   */
/*                                                                          */
/* Deletes the token identified by the combination of (type,uid,ctx)        */
/* "key" is a combination of the table type, iterator type and the unit for */
/* which the token was being used.                                          */
/* ------------------------------------------------------------------------ */
int
ipf_lookup_deltok(softc, data, uid, ctx)
	ipf_main_softc_t *softc;
	void *data;
	int uid;
	void *ctx;
{
	int error, key;
	SPL_INT(s);

	SPL_SCHED(s);
	error = BCOPYIN(data, &key, sizeof(key));
	if (error == 0)
		error = ipf_token_del(softc, key, uid, ctx);
	SPL_X(s);
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_res_num                                          */
/* Returns:     void * - NULL = failure, else success.                      */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              unit(I)     - device for which this is for                  */
/*              type(I)     - type of lookup these parameters are for.      */
/*              number(I)   - table number to use when searching            */
/*              funcptr(IO) - pointer to pointer for storing IP address     */
/*                            searching function.                           */
/*                                                                          */
/* Search for the "table" number passed in amongst those configured for     */
/* that particular type.  If the type is recognised then the function to    */
/* call to do the IP address search will be change, regardless of whether   */
/* or not the "table" number exists.                                        */
/* ------------------------------------------------------------------------ */
void *
ipf_lookup_res_num(softc, unit, type, number, funcptr)
	ipf_main_softc_t *softc;
	int unit;
	u_int type;
	u_int number;
	lookupfunc_t *funcptr;
{
	char name[FR_GROUPLEN];

#if defined(SNPRINTF) && defined(_KERNEL)
	SNPRINTF(name, sizeof(name), "%u", number);
#else
	(void) sprintf(name, "%u", number);
#endif

	return ipf_lookup_res_name(softc, unit, type, name, funcptr);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_res_name                                         */
/* Returns:     void * - NULL = failure, else success.                      */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              unit(I)     - device for which this is for                  */
/*              type(I)     - type of lookup these parameters are for.      */
/*              name(I)     - table name to use when searching              */
/*              funcptr(IO) - pointer to pointer for storing IP address     */
/*                            searching function.                           */
/*                                                                          */
/* Search for the "table" number passed in amongst those configured for     */
/* that particular type.  If the type is recognised then the function to    */
/* call to do the IP address search will be changed, regardless of whether  */
/* or not the "table" number exists.                                        */
/* ------------------------------------------------------------------------ */
void *
ipf_lookup_res_name(softc, unit, type, name, funcptr)
	ipf_main_softc_t *softc;
	int unit;
	u_int type;
	char *name;
	lookupfunc_t *funcptr;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	ipf_lookup_t **l;
	void *ptr = NULL;
	int i;

	READ_ENTER(&softc->ipf_poolrw);

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++) {
		if (type == (*l)->ipfl_type) {
			ptr = (*(*l)->ipfl_select_add_ref)(softl->ipf_back[i],
							   unit, name);
			if (ptr != NULL && funcptr != NULL) {
				*funcptr = (*l)->ipfl_addr_find;
			}
			break;
		}
	}

	if (i == MAX_BACKENDS) {
		ptr = NULL;
		if (funcptr != NULL)
			*funcptr = NULL;
	}

	RWLOCK_EXIT(&softc->ipf_poolrw);

	return ptr;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_find_htable                                      */
/* Returns:     void * - NULL = failure, else success.                      */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              unit(I)     - device for which this is for                  */
/*              name(I)     - table name to use when searching              */
/*                                                                          */
/* To support the group-map feature, where a hash table maps address        */
/* networks to rule group numbers, we need to expose a function that uses   */
/* only the hash table backend.                                             */
/* ------------------------------------------------------------------------ */
void *
ipf_lookup_find_htable(softc, unit, name)
	ipf_main_softc_t *softc;
	int unit;
	char *name;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	ipf_lookup_t **l;
	void *tab = NULL;
	int i;

	READ_ENTER(&softc->ipf_poolrw);

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++)
		if (IPLT_HASH == (*l)->ipfl_type) {
			tab = ipf_htable_find(softl->ipf_back[i], unit, name);
			break;
		}

	RWLOCK_EXIT(&softc->ipf_poolrw);

	return tab;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_lookup_sync                                             */
/* Returns:     void                                                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* This function is the interface that the machine dependent sync functions */
/* call when a network interface name change occurs. It then calls the sync */
/* functions of the lookup implementations - if they have one.              */
/* ------------------------------------------------------------------------ */
/*ARGSUSED*/
void
ipf_lookup_sync(softc, ifp)
	ipf_main_softc_t *softc;
	void *ifp;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	ipf_lookup_t **l;
	int i;

	READ_ENTER(&softc->ipf_poolrw);

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++)
		if ((*l)->ipfl_sync != NULL)
			(*(*l)->ipfl_sync)(softc, softl->ipf_back[i]);

	RWLOCK_EXIT(&softc->ipf_poolrw);
}


#ifndef _KERNEL
void
ipf_lookup_dump(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_lookup_softc_t *softl = softc->ipf_lookup_soft;
	ipf_lookup_t **l;
	int i;

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++)
		if (IPLT_POOL == (*l)->ipfl_type) {
			ipf_pool_dump(softc, softl->ipf_back[i]);
			break;
		}

	for (i = 0, l = backends; i < MAX_BACKENDS; i++, l++)
		if (IPLT_HASH == (*l)->ipfl_type) {
			ipf_htable_dump(softc, softl->ipf_back[i]);
			break;
		}
}
#endif
