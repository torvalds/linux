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
# include <stdlib.h>
# ifdef _STDC_C99
#  include <stdbool.h>
# endif
# include <string.h>
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
#if defined(__NetBSD__)
# include <machine/cpu.h>
#endif
#if defined(_KERNEL) && defined(__NetBSD__) && (__NetBSD_Version__ >= 104000000)
# include <sys/proc.h>
#endif
#if defined(__NetBSD_Version__) &&  (__NetBSD_Version__ >= 400000) && \
     !defined(_KERNEL)
# include <stdbool.h>
#endif
#include <net/if.h>
#ifdef sun
# include <net/af.h>
#endif
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
# include <netinet/ip_var.h>
#if !defined(_KERNEL)
# define	KERNEL
# define	_KERNEL
# define	NOT_KERNEL
#endif
#ifdef	NOT_KERNEL
# undef	_KERNEL
# undef	KERNEL
#endif
#include <netinet/tcp.h>
#  if defined(__FreeBSD_version)
#   include <net/if_var.h>
#    define IF_QFULL _IF_QFULL
#    define IF_DROP _IF_DROP
#  endif
#  include <netinet/in_var.h>
#  include <netinet/tcp_fsm.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include "netinet/ip_compat.h"
#include <netinet/tcpip.h>
#include "netinet/ip_fil.h"
#include "netinet/ip_auth.h"
#if !defined(MENTAT)
# include <net/netisr.h>
# ifdef __FreeBSD__
#  include <machine/cpufunc.h>
# endif
#endif
#if defined(__FreeBSD_version)
# include <sys/malloc.h>
# if defined(_KERNEL) && !defined(IPFILTER_LKM)
#  include <sys/libkern.h>
#  include <sys/systm.h>
# endif
#endif
/* END OF INCLUDES */

#if !defined(lint)
static const char rcsid[] = "@(#)$FreeBSD$";
/* static const char rcsid[] = "@(#)$Id: ip_auth.c,v 2.73.2.24 2007/09/09 11:32:04 darrenr Exp $"; */
#endif


static void ipf_auth_deref __P((frauthent_t **));
static void ipf_auth_deref_unlocked __P((ipf_auth_softc_t *, frauthent_t **));
static int ipf_auth_geniter __P((ipf_main_softc_t *, ipftoken_t *,
				 ipfgeniter_t *, ipfobj_t *));
static int ipf_auth_reply __P((ipf_main_softc_t *, ipf_auth_softc_t *, char *));
static int ipf_auth_wait __P((ipf_main_softc_t *, ipf_auth_softc_t *, char *));
static int ipf_auth_flush __P((void *));


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_main_load                                          */
/* Returns:     int - 0 == success, else error                              */
/* Parameters:  None                                                        */
/*                                                                          */
/* A null-op function that exists as a placeholder so that the flow in      */
/* other functions is obvious.                                              */
/* ------------------------------------------------------------------------ */
int
ipf_auth_main_load()
{
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_main_unload                                        */
/* Returns:     int - 0 == success, else error                              */
/* Parameters:  None                                                        */
/*                                                                          */
/* A null-op function that exists as a placeholder so that the flow in      */
/* other functions is obvious.                                              */
/* ------------------------------------------------------------------------ */
int
ipf_auth_main_unload()
{
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_soft_create                                        */
/* Returns:     int - NULL = failure, else success                          */
/* Parameters:  softc(I) - pointer to soft context data                     */
/*                                                                          */
/* Create a structre to store all of the run-time data for packet auth in   */
/* and initialise some fields to their defaults.                            */
/* ------------------------------------------------------------------------ */
void *
ipf_auth_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_auth_softc_t *softa;

	KMALLOC(softa, ipf_auth_softc_t *);
	if (softa == NULL)
		return NULL;

	bzero((char *)softa, sizeof(*softa));

	softa->ipf_auth_size = FR_NUMAUTH;
	softa->ipf_auth_defaultage = 600;

	RWLOCK_INIT(&softa->ipf_authlk, "ipf IP User-Auth rwlock");
	MUTEX_INIT(&softa->ipf_auth_mx, "ipf auth log mutex");
#if SOLARIS && defined(_KERNEL)
	cv_init(&softa->ipf_auth_wait, "ipf auth condvar", CV_DRIVER, NULL);
#endif

	return softa;
}

/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_soft_init                                          */
/* Returns:     int - 0 == success, else error                              */
/* Parameters:  softc(I) - pointer to soft context data                     */
/*              arg(I)   - opaque pointer to auth context data              */
/*                                                                          */
/* Allocate memory and initialise data structures used in handling auth     */
/* rules.                                                                   */
/* ------------------------------------------------------------------------ */
int
ipf_auth_soft_init(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_auth_softc_t *softa = arg;

	KMALLOCS(softa->ipf_auth, frauth_t *,
		 softa->ipf_auth_size * sizeof(*softa->ipf_auth));
	if (softa->ipf_auth == NULL)
		return -1;
	bzero((char *)softa->ipf_auth,
	      softa->ipf_auth_size * sizeof(*softa->ipf_auth));

	KMALLOCS(softa->ipf_auth_pkts, mb_t **,
		 softa->ipf_auth_size * sizeof(*softa->ipf_auth_pkts));
	if (softa->ipf_auth_pkts == NULL)
		return -2;
	bzero((char *)softa->ipf_auth_pkts,
	      softa->ipf_auth_size * sizeof(*softa->ipf_auth_pkts));


	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_soft_fini                                          */
/* Returns:     int - 0 == success, else error                              */
/* Parameters:  softc(I) - pointer to soft context data                     */
/*              arg(I)   - opaque pointer to auth context data              */
/*                                                                          */
/* Free all network buffer memory used to keep saved packets that have been */
/* connectedd to the soft soft context structure *but* do not free that: it */
/* is free'd by _destroy().                                                 */
/* ------------------------------------------------------------------------ */
int
ipf_auth_soft_fini(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_auth_softc_t *softa = arg;
	frauthent_t *fae, **faep;
	frentry_t *fr, **frp;
	mb_t *m;
	int i;

	if (softa->ipf_auth != NULL) {
		KFREES(softa->ipf_auth,
		       softa->ipf_auth_size * sizeof(*softa->ipf_auth));
		softa->ipf_auth = NULL;
	}

	if (softa->ipf_auth_pkts != NULL) {
		for (i = 0; i < softa->ipf_auth_size; i++) {
			m = softa->ipf_auth_pkts[i];
			if (m != NULL) {
				FREE_MB_T(m);
				softa->ipf_auth_pkts[i] = NULL;
			}
		}
		KFREES(softa->ipf_auth_pkts,
		       softa->ipf_auth_size * sizeof(*softa->ipf_auth_pkts));
		softa->ipf_auth_pkts = NULL;
	}

	faep = &softa->ipf_auth_entries;
	while ((fae = *faep) != NULL) {
		*faep = fae->fae_next;
		KFREE(fae);
	}
	softa->ipf_auth_ip = NULL;

	if (softa->ipf_auth_rules != NULL) {
		for (frp = &softa->ipf_auth_rules; ((fr = *frp) != NULL); ) {
			if (fr->fr_ref == 1) {
				*frp = fr->fr_next;
				MUTEX_DESTROY(&fr->fr_lock);
				KFREE(fr);
			} else
				frp = &fr->fr_next;
		}
	}

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_soft_destroy                                       */
/* Returns:     void                                                        */
/* Parameters:  softc(I) - pointer to soft context data                     */
/*              arg(I)   - opaque pointer to auth context data              */
/*                                                                          */
/* Undo what was done in _create() - i.e. free the soft context data.       */
/* ------------------------------------------------------------------------ */
void
ipf_auth_soft_destroy(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_auth_softc_t *softa = arg;

# if SOLARIS && defined(_KERNEL)
	cv_destroy(&softa->ipf_auth_wait);
# endif
	MUTEX_DESTROY(&softa->ipf_auth_mx);
	RW_DESTROY(&softa->ipf_authlk);

	KFREE(softa);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_setlock                                            */
/* Returns:     void                                                        */
/* Paramters:   arg(I) - pointer to soft context data                       */
/*              tmp(I) - value to assign to auth lock                       */
/*                                                                          */
/* ------------------------------------------------------------------------ */
void
ipf_auth_setlock(arg, tmp)
	void *arg;
	int tmp;
{
	ipf_auth_softc_t *softa = arg;

	softa->ipf_auth_lock = tmp;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_check                                              */
/* Returns:     frentry_t* - pointer to ipf rule if match found, else NULL  */
/* Parameters:  fin(I)   - pointer to ipftoken structure                    */
/*              passp(I) - pointer to ipfgeniter structure                  */
/*                                                                          */
/* Check if a packet has authorization.  If the packet is found to match an */
/* authorization result and that would result in a feedback loop (i.e. it   */
/* will end up returning FR_AUTH) then return FR_BLOCK instead.             */
/* ------------------------------------------------------------------------ */
frentry_t *
ipf_auth_check(fin, passp)
	fr_info_t *fin;
	u_32_t *passp;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_auth_softc_t *softa = softc->ipf_auth_soft;
	frentry_t *fr;
	frauth_t *fra;
	u_32_t pass;
	u_short id;
	ip_t *ip;
	int i;

	if (softa->ipf_auth_lock || !softa->ipf_auth_used)
		return NULL;

	ip = fin->fin_ip;
	id = ip->ip_id;

	READ_ENTER(&softa->ipf_authlk);
	for (i = softa->ipf_auth_start; i != softa->ipf_auth_end; ) {
		/*
		 * index becomes -2 only after an SIOCAUTHW.  Check this in
		 * case the same packet gets sent again and it hasn't yet been
		 * auth'd.
		 */
		fra = softa->ipf_auth + i;
		if ((fra->fra_index == -2) && (id == fra->fra_info.fin_id) &&
		    !bcmp((char *)fin, (char *)&fra->fra_info, FI_CSIZE)) {
			/*
			 * Avoid feedback loop.
			 */
			if (!(pass = fra->fra_pass) || (FR_ISAUTH(pass))) {
				pass = FR_BLOCK;
				fin->fin_reason = FRB_AUTHFEEDBACK;
			}
			/*
			 * Create a dummy rule for the stateful checking to
			 * use and return.  Zero out any values we don't
			 * trust from userland!
			 */
			if ((pass & FR_KEEPSTATE) || ((pass & FR_KEEPFRAG) &&
			     (fin->fin_flx & FI_FRAG))) {
				KMALLOC(fr, frentry_t *);
				if (fr) {
					bcopy((char *)fra->fra_info.fin_fr,
					      (char *)fr, sizeof(*fr));
					fr->fr_grp = NULL;
					fr->fr_ifa = fin->fin_ifp;
					fr->fr_func = NULL;
					fr->fr_ref = 1;
					fr->fr_flags = pass;
					fr->fr_ifas[1] = NULL;
					fr->fr_ifas[2] = NULL;
					fr->fr_ifas[3] = NULL;
					MUTEX_INIT(&fr->fr_lock,
						   "ipf auth rule");
				}
			} else
				fr = fra->fra_info.fin_fr;
			fin->fin_fr = fr;
			fin->fin_flx |= fra->fra_flx;
			RWLOCK_EXIT(&softa->ipf_authlk);

			WRITE_ENTER(&softa->ipf_authlk);
			/*
			 * ipf_auth_rules is populated with the rules malloc'd
			 * above and only those.
			 */
			if ((fr != NULL) && (fr != fra->fra_info.fin_fr)) {
				fr->fr_next = softa->ipf_auth_rules;
				softa->ipf_auth_rules = fr;
			}
			softa->ipf_auth_stats.fas_hits++;
			fra->fra_index = -1;
			softa->ipf_auth_used--;
			softa->ipf_auth_replies--;
			if (i == softa->ipf_auth_start) {
				while (fra->fra_index == -1) {
					i++;
					fra++;
					if (i == softa->ipf_auth_size) {
						i = 0;
						fra = softa->ipf_auth;
					}
					softa->ipf_auth_start = i;
					if (i == softa->ipf_auth_end)
						break;
				}
				if (softa->ipf_auth_start ==
				    softa->ipf_auth_end) {
					softa->ipf_auth_next = 0;
					softa->ipf_auth_start = 0;
					softa->ipf_auth_end = 0;
				}
			}
			RWLOCK_EXIT(&softa->ipf_authlk);
			if (passp != NULL)
				*passp = pass;
			softa->ipf_auth_stats.fas_hits++;
			return fr;
		}
		i++;
		if (i == softa->ipf_auth_size)
			i = 0;
	}
	RWLOCK_EXIT(&softa->ipf_authlk);
	softa->ipf_auth_stats.fas_miss++;
	return NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_new                                                */
/* Returns:     int - 1 == success, 0 = did not put packet on auth queue    */
/* Parameters:  m(I)   - pointer to mb_t with packet in it                  */
/*              fin(I) - pointer to packet information                      */
/*                                                                          */
/* Check if we have room in the auth array to hold details for another      */
/* packet. If we do, store it and wake up any user programs which are       */
/* waiting to hear about these events.                                      */
/* ------------------------------------------------------------------------ */
int
ipf_auth_new(m, fin)
	mb_t *m;
	fr_info_t *fin;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_auth_softc_t *softa = softc->ipf_auth_soft;
#if defined(_KERNEL) && defined(MENTAT)
	qpktinfo_t *qpi = fin->fin_qpi;
#endif
	frauth_t *fra;
#if !defined(sparc) && !defined(m68k)
	ip_t *ip;
#endif
	int i;

	if (softa->ipf_auth_lock)
		return 0;

	WRITE_ENTER(&softa->ipf_authlk);
	if (((softa->ipf_auth_end + 1) % softa->ipf_auth_size) ==
	    softa->ipf_auth_start) {
		softa->ipf_auth_stats.fas_nospace++;
		RWLOCK_EXIT(&softa->ipf_authlk);
		return 0;
	}

	softa->ipf_auth_stats.fas_added++;
	softa->ipf_auth_used++;
	i = softa->ipf_auth_end++;
	if (softa->ipf_auth_end == softa->ipf_auth_size)
		softa->ipf_auth_end = 0;

	fra = softa->ipf_auth + i;
	fra->fra_index = i;
	if (fin->fin_fr != NULL)
		fra->fra_pass = fin->fin_fr->fr_flags;
	else
		fra->fra_pass = 0;
	fra->fra_age = softa->ipf_auth_defaultage;
	bcopy((char *)fin, (char *)&fra->fra_info, sizeof(*fin));
	fra->fra_flx = fra->fra_info.fin_flx & (FI_STATE|FI_NATED);
	fra->fra_info.fin_flx &= ~(FI_STATE|FI_NATED);
#if !defined(sparc) && !defined(m68k)
	/*
	 * No need to copyback here as we want to undo the changes, not keep
	 * them.
	 */
	ip = fin->fin_ip;
# if defined(MENTAT) && defined(_KERNEL)
	if ((ip == (ip_t *)m->b_rptr) && (fin->fin_v == 4))
# endif
	{
		register u_short bo;

		bo = ip->ip_len;
		ip->ip_len = htons(bo);
		bo = ip->ip_off;
		ip->ip_off = htons(bo);
	}
#endif
#if SOLARIS && defined(_KERNEL)
	COPYIFNAME(fin->fin_v, fin->fin_ifp, fra->fra_info.fin_ifname);
	m->b_rptr -= qpi->qpi_off;
	fra->fra_q = qpi->qpi_q;	/* The queue can disappear! */
	fra->fra_m = *fin->fin_mp;
	fra->fra_info.fin_mp = &fra->fra_m;
	softa->ipf_auth_pkts[i] = *(mblk_t **)fin->fin_mp;
	RWLOCK_EXIT(&softa->ipf_authlk);
	cv_signal(&softa->ipf_auth_wait);
	pollwakeup(&softc->ipf_poll_head[IPL_LOGAUTH], POLLIN|POLLRDNORM);
#else
	softa->ipf_auth_pkts[i] = m;
	RWLOCK_EXIT(&softa->ipf_authlk);
	WAKEUP(&softa->ipf_auth_next, 0);
#endif
	return 1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_ioctl                                              */
/* Returns:     int - 0 == success, else error                              */
/* Parameters:  data(IO) - pointer to ioctl data                            */
/*              cmd(I)   - ioctl command                                    */
/*              mode(I)  - mode flags associated with open descriptor       */
/*              uid(I)   - uid associatd with application making the call   */
/*              ctx(I)   - pointer for context                              */
/*                                                                          */
/* This function handles all of the ioctls recognised by the auth component */
/* in IPFilter - ie ioctls called on an open fd for /dev/ipf_auth           */
/* ------------------------------------------------------------------------ */
int
ipf_auth_ioctl(softc, data, cmd, mode, uid, ctx)
	ipf_main_softc_t *softc;
	caddr_t data;
	ioctlcmd_t cmd;
	int mode, uid;
	void *ctx;
{
	ipf_auth_softc_t *softa = softc->ipf_auth_soft;
	int error = 0, i;
	SPL_INT(s);

	switch (cmd)
	{
	case SIOCGENITER :
	    {
		ipftoken_t *token;
		ipfgeniter_t iter;
		ipfobj_t obj;

		error = ipf_inobj(softc, data, &obj, &iter, IPFOBJ_GENITER);
		if (error != 0)
			break;

		SPL_SCHED(s);
		token = ipf_token_find(softc, IPFGENITER_AUTH, uid, ctx);
		if (token != NULL)
			error = ipf_auth_geniter(softc, token, &iter, &obj);
		else {
			WRITE_ENTER(&softc->ipf_tokens);
			ipf_token_deref(softc, token);
			RWLOCK_EXIT(&softc->ipf_tokens);
			IPFERROR(10001);
			error = ESRCH;
		}
		SPL_X(s);

		break;
	    }

	case SIOCADAFR :
	case SIOCRMAFR :
		if (!(mode & FWRITE)) {
			IPFERROR(10002);
			error = EPERM;
		} else
			error = frrequest(softc, IPL_LOGAUTH, cmd, data,
					  softc->ipf_active, 1);
		break;

	case SIOCSTLCK :
		if (!(mode & FWRITE)) {
			IPFERROR(10003);
			error = EPERM;
		} else {
			error = ipf_lock(data, &softa->ipf_auth_lock);
		}
		break;

	case SIOCATHST:
		softa->ipf_auth_stats.fas_faelist = softa->ipf_auth_entries;
		error = ipf_outobj(softc, data, &softa->ipf_auth_stats,
				   IPFOBJ_AUTHSTAT);
		break;

	case SIOCIPFFL:
		SPL_NET(s);
		WRITE_ENTER(&softa->ipf_authlk);
		i = ipf_auth_flush(softa);
		RWLOCK_EXIT(&softa->ipf_authlk);
		SPL_X(s);
		error = BCOPYOUT(&i, data, sizeof(i));
		if (error != 0) {
			IPFERROR(10004);
			error = EFAULT;
		}
		break;

	case SIOCAUTHW:
		error = ipf_auth_wait(softc, softa, data);
		break;

	case SIOCAUTHR:
		error = ipf_auth_reply(softc, softa, data);
		break;

	default :
		IPFERROR(10005);
		error = EINVAL;
		break;
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_expire                                             */
/* Returns:     None                                                        */
/* Parameters:  None                                                        */
/*                                                                          */
/* Slowly expire held auth records.  Timeouts are set in expectation of     */
/* this being called twice per second.                                      */
/* ------------------------------------------------------------------------ */
void
ipf_auth_expire(softc)
	ipf_main_softc_t *softc;
{
	ipf_auth_softc_t *softa = softc->ipf_auth_soft;
	frauthent_t *fae, **faep;
	frentry_t *fr, **frp;
	frauth_t *fra;
	mb_t *m;
	int i;
	SPL_INT(s);

	if (softa->ipf_auth_lock)
		return;
	SPL_NET(s);
	WRITE_ENTER(&softa->ipf_authlk);
	for (i = 0, fra = softa->ipf_auth; i < softa->ipf_auth_size;
	     i++, fra++) {
		fra->fra_age--;
		if ((fra->fra_age == 0) &&
		    (softa->ipf_auth[i].fra_index != -1)) {
			if ((m = softa->ipf_auth_pkts[i]) != NULL) {
				FREE_MB_T(m);
				softa->ipf_auth_pkts[i] = NULL;
			} else if (softa->ipf_auth[i].fra_index == -2) {
				softa->ipf_auth_replies--;
			}
			softa->ipf_auth[i].fra_index = -1;
			softa->ipf_auth_stats.fas_expire++;
			softa->ipf_auth_used--;
		}
	}

	/*
	 * Expire pre-auth rules
	 */
	for (faep = &softa->ipf_auth_entries; ((fae = *faep) != NULL); ) {
		fae->fae_age--;
		if (fae->fae_age == 0) {
			ipf_auth_deref(&fae);
			softa->ipf_auth_stats.fas_expire++;
		} else
			faep = &fae->fae_next;
	}
	if (softa->ipf_auth_entries != NULL)
		softa->ipf_auth_ip = &softa->ipf_auth_entries->fae_fr;
	else
		softa->ipf_auth_ip = NULL;

	for (frp = &softa->ipf_auth_rules; ((fr = *frp) != NULL); ) {
		if (fr->fr_ref == 1) {
			*frp = fr->fr_next;
			MUTEX_DESTROY(&fr->fr_lock);
			KFREE(fr);
		} else
			frp = &fr->fr_next;
	}
	RWLOCK_EXIT(&softa->ipf_authlk);
	SPL_X(s);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_precmd                                             */
/* Returns:     int - 0 == success, else error                              */
/* Parameters:  cmd(I)  - ioctl command for rule                            */
/*              fr(I)   - pointer to ipf rule                               */
/*              fptr(I) - pointer to caller's 'fr'                          */
/*                                                                          */
/* ------------------------------------------------------------------------ */
int
ipf_auth_precmd(softc, cmd, fr, frptr)
	ipf_main_softc_t *softc;
	ioctlcmd_t cmd;
	frentry_t *fr, **frptr;
{
	ipf_auth_softc_t *softa = softc->ipf_auth_soft;
	frauthent_t *fae, **faep;
	int error = 0;
	SPL_INT(s);

	if ((cmd != SIOCADAFR) && (cmd != SIOCRMAFR)) {
		IPFERROR(10006);
		return EIO;
	}

	for (faep = &softa->ipf_auth_entries; ((fae = *faep) != NULL); ) {
		if (&fae->fae_fr == fr)
			break;
		else
			faep = &fae->fae_next;
	}

	if (cmd == (ioctlcmd_t)SIOCRMAFR) {
		if (fr == NULL || frptr == NULL) {
			IPFERROR(10007);
			error = EINVAL;

		} else if (fae == NULL) {
			IPFERROR(10008);
			error = ESRCH;

		} else {
			SPL_NET(s);
			WRITE_ENTER(&softa->ipf_authlk);
			*faep = fae->fae_next;
			if (softa->ipf_auth_ip == &fae->fae_fr)
				softa->ipf_auth_ip = softa->ipf_auth_entries ?
				    &softa->ipf_auth_entries->fae_fr : NULL;
			RWLOCK_EXIT(&softa->ipf_authlk);
			SPL_X(s);

			KFREE(fae);
		}
	} else if (fr != NULL && frptr != NULL) {
		KMALLOC(fae, frauthent_t *);
		if (fae != NULL) {
			bcopy((char *)fr, (char *)&fae->fae_fr,
			      sizeof(*fr));
			SPL_NET(s);
			WRITE_ENTER(&softa->ipf_authlk);
			fae->fae_age = softa->ipf_auth_defaultage;
			fae->fae_fr.fr_hits = 0;
			fae->fae_fr.fr_next = *frptr;
			fae->fae_ref = 1;
			*frptr = &fae->fae_fr;
			fae->fae_next = *faep;
			*faep = fae;
			softa->ipf_auth_ip = &softa->ipf_auth_entries->fae_fr;
			RWLOCK_EXIT(&softa->ipf_authlk);
			SPL_X(s);
		} else {
			IPFERROR(10009);
			error = ENOMEM;
		}
	} else {
		IPFERROR(10010);
		error = EINVAL;
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_flush                                              */
/* Returns:     int - number of auth entries flushed                        */
/* Parameters:  None                                                        */
/* Locks:       WRITE(ipf_authlk)                                           */
/*                                                                          */
/* This function flushs the ipf_auth_pkts array of any packet data with     */
/* references still there.                                                  */
/* It is expected that the caller has already acquired the correct locks or */
/* set the priority level correctly for this to block out other code paths  */
/* into these data structures.                                              */
/* ------------------------------------------------------------------------ */
static int
ipf_auth_flush(arg)
	void *arg;
{
	ipf_auth_softc_t *softa = arg;
	int i, num_flushed;
	mb_t *m;

	if (softa->ipf_auth_lock)
		return -1;

	num_flushed = 0;

	for (i = 0 ; i < softa->ipf_auth_size; i++) {
		if (softa->ipf_auth[i].fra_index != -1) {
			m = softa->ipf_auth_pkts[i];
			if (m != NULL) {
				FREE_MB_T(m);
				softa->ipf_auth_pkts[i] = NULL;
			}

			softa->ipf_auth[i].fra_index = -1;
			/* perhaps add & use a flush counter inst.*/
			softa->ipf_auth_stats.fas_expire++;
			num_flushed++;
		}
	}

	softa->ipf_auth_start = 0;
	softa->ipf_auth_end = 0;
	softa->ipf_auth_next = 0;
	softa->ipf_auth_used = 0;
	softa->ipf_auth_replies = 0;

	return num_flushed;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_waiting                                            */
/* Returns:     int - number of packets in the auth queue                   */
/* Parameters:  None                                                        */
/*                                                                          */
/* Simple truth check to see if there are any packets waiting in the auth   */
/* queue.                                                                   */
/* ------------------------------------------------------------------------ */
int
ipf_auth_waiting(softc)
	ipf_main_softc_t *softc;
{
	ipf_auth_softc_t *softa = softc->ipf_auth_soft;

	return (softa->ipf_auth_used != 0);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_geniter                                            */
/* Returns:     int - 0 == success, else error                              */
/* Parameters:  token(I) - pointer to ipftoken structure                    */
/*              itp(I)   - pointer to ipfgeniter structure                  */
/*              objp(I)  - pointer to ipf object destription                */
/*                                                                          */
/* Iterate through the list of entries in the auth queue list.              */
/* objp is used here to get the location of where to do the copy out to.    */
/* Stomping over various fields with new information will not harm anything */
/* ------------------------------------------------------------------------ */
static int
ipf_auth_geniter(softc, token, itp, objp)
	ipf_main_softc_t *softc;
	ipftoken_t *token;
	ipfgeniter_t *itp;
	ipfobj_t *objp;
{
	ipf_auth_softc_t *softa = softc->ipf_auth_soft;
	frauthent_t *fae, *next, zero;
	int error;

	if (itp->igi_data == NULL) {
		IPFERROR(10011);
		return EFAULT;
	}

	if (itp->igi_type != IPFGENITER_AUTH) {
		IPFERROR(10012);
		return EINVAL;
	}

	objp->ipfo_type = IPFOBJ_FRAUTH;
	objp->ipfo_ptr = itp->igi_data;
	objp->ipfo_size = sizeof(frauth_t);

	READ_ENTER(&softa->ipf_authlk);

	fae = token->ipt_data;
	if (fae == NULL) {
		next = softa->ipf_auth_entries;
	} else {
		next = fae->fae_next;
	}

	/*
	 * If we found an auth entry to use, bump its reference count
	 * so that it can be used for is_next when we come back.
	 */
	if (next != NULL) {
		ATOMIC_INC(next->fae_ref);
		token->ipt_data = next;
	} else {
		bzero(&zero, sizeof(zero));
		next = &zero;
		token->ipt_data = NULL;
	}

	RWLOCK_EXIT(&softa->ipf_authlk);

	error = ipf_outobjk(softc, objp, next);
	if (fae != NULL)
		ipf_auth_deref_unlocked(softa, &fae);

	if (next->fae_next == NULL)
		ipf_token_mark_complete(token);
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_deref_unlocked                                     */
/* Returns:     None                                                        */
/* Parameters:  faep(IO) - pointer to caller's frauthent_t pointer          */
/*                                                                          */
/* Wrapper for ipf_auth_deref for when a write lock on ipf_authlk is not    */
/* held.                                                                    */
/* ------------------------------------------------------------------------ */
static void
ipf_auth_deref_unlocked(softa, faep)
	ipf_auth_softc_t *softa;
	frauthent_t **faep;
{
	WRITE_ENTER(&softa->ipf_authlk);
	ipf_auth_deref(faep);
	RWLOCK_EXIT(&softa->ipf_authlk);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_deref                                              */
/* Returns:     None                                                        */
/* Parameters:  faep(IO) - pointer to caller's frauthent_t pointer          */
/* Locks:       WRITE(ipf_authlk)                                           */
/*                                                                          */
/* This function unconditionally sets the pointer in the caller to NULL,    */
/* to make it clear that it should no longer use that pointer, and drops    */
/* the reference count on the structure by 1.  If it reaches 0, free it up. */
/* ------------------------------------------------------------------------ */
static void
ipf_auth_deref(faep)
	frauthent_t **faep;
{
	frauthent_t *fae;

	fae = *faep;
	*faep = NULL;

	fae->fae_ref--;
	if (fae->fae_ref == 0) {
		KFREE(fae);
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_wait_pkt                                           */
/* Returns:     int - 0 == success, else error                              */
/* Parameters:  data(I) - pointer to data from ioctl call                   */
/*                                                                          */
/* This function is called when an application is waiting for a packet to   */
/* match an "auth" rule by issuing an SIOCAUTHW ioctl.  If there is already */
/* a packet waiting on the queue then we will return that _one_ immediately.*/
/* If there are no packets present in the queue (ipf_auth_pkts) then we go  */
/* to sleep.                                                                */
/* ------------------------------------------------------------------------ */
static int
ipf_auth_wait(softc, softa, data)
	ipf_main_softc_t *softc;
	ipf_auth_softc_t *softa;
	char *data;
{
	frauth_t auth, *au = &auth;
	int error, len, i;
	mb_t *m;
	char *t;
	SPL_INT(s);

ipf_auth_ioctlloop:
	error = ipf_inobj(softc, data, NULL, au, IPFOBJ_FRAUTH);
	if (error != 0)
		return error;

	/*
	 * XXX Locks are held below over calls to copyout...a better
	 * solution needs to be found so this isn't necessary.  The situation
	 * we are trying to guard against here is an error in the copyout
	 * steps should not cause the packet to "disappear" from the queue.
	 */
	SPL_NET(s);
	READ_ENTER(&softa->ipf_authlk);

	/*
	 * If ipf_auth_next is not equal to ipf_auth_end it will be because
	 * there is a packet waiting to be delt with in the ipf_auth_pkts
	 * array.  We copy as much of that out to user space as requested.
	 */
	if (softa->ipf_auth_used > 0) {
		while (softa->ipf_auth_pkts[softa->ipf_auth_next] == NULL) {
			softa->ipf_auth_next++;
			if (softa->ipf_auth_next == softa->ipf_auth_size)
				softa->ipf_auth_next = 0;
		}

		error = ipf_outobj(softc, data,
				   &softa->ipf_auth[softa->ipf_auth_next],
				   IPFOBJ_FRAUTH);
		if (error != 0) {
			RWLOCK_EXIT(&softa->ipf_authlk);
			SPL_X(s);
			return error;
		}

		if (auth.fra_len != 0 && auth.fra_buf != NULL) {
			/*
			 * Copy packet contents out to user space if
			 * requested.  Bail on an error.
			 */
			m = softa->ipf_auth_pkts[softa->ipf_auth_next];
			len = MSGDSIZE(m);
			if (len > auth.fra_len)
				len = auth.fra_len;
			auth.fra_len = len;

			for (t = auth.fra_buf; m && (len > 0); ) {
				i = MIN(M_LEN(m), len);
				error = copyoutptr(softc, MTOD(m, char *),
						   &t, i);
				len -= i;
				t += i;
				if (error != 0) {
					RWLOCK_EXIT(&softa->ipf_authlk);
					SPL_X(s);
					return error;
				}
				m = m->m_next;
			}
		}
		RWLOCK_EXIT(&softa->ipf_authlk);

		SPL_NET(s);
		WRITE_ENTER(&softa->ipf_authlk);
		softa->ipf_auth_next++;
		if (softa->ipf_auth_next == softa->ipf_auth_size)
			softa->ipf_auth_next = 0;
		RWLOCK_EXIT(&softa->ipf_authlk);
		SPL_X(s);

		return 0;
	}
	RWLOCK_EXIT(&softa->ipf_authlk);
	SPL_X(s);

	MUTEX_ENTER(&softa->ipf_auth_mx);
#ifdef	_KERNEL
# if	SOLARIS
	error = 0;
	if (!cv_wait_sig(&softa->ipf_auth_wait, &softa->ipf_auth_mx.ipf_lk)) {
		IPFERROR(10014);
		error = EINTR;
	}
# else /* SOLARIS */
	error = SLEEP(&softa->ipf_auth_next, "ipf_auth_next");
# endif /* SOLARIS */
#endif
	MUTEX_EXIT(&softa->ipf_auth_mx);
	if (error == 0)
		goto ipf_auth_ioctlloop;
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_auth_reply                                              */
/* Returns:     int - 0 == success, else error                              */
/* Parameters:  data(I) - pointer to data from ioctl call                   */
/*                                                                          */
/* This function is called by an application when it wants to return a      */
/* decision on a packet using the SIOCAUTHR ioctl.  This is after it has    */
/* received information using an SIOCAUTHW.  The decision returned in the   */
/* form of flags, the same as those used in each rule.                      */
/* ------------------------------------------------------------------------ */
static int
ipf_auth_reply(softc, softa, data)
	ipf_main_softc_t *softc;
	ipf_auth_softc_t *softa;
	char *data;
{
	frauth_t auth, *au = &auth, *fra;
	fr_info_t fin;
	int error, i;
	mb_t *m;
	SPL_INT(s);

	error = ipf_inobj(softc, data, NULL, &auth, IPFOBJ_FRAUTH);
	if (error != 0)
		return error;

	SPL_NET(s);
	WRITE_ENTER(&softa->ipf_authlk);

	i = au->fra_index;
	fra = softa->ipf_auth + i;
	error = 0;

	/*
	 * Check the validity of the information being returned with two simple
	 * checks.  First, the auth index value should be within the size of
	 * the array and second the packet id being returned should also match.
	 */
	if ((i < 0) || (i >= softa->ipf_auth_size)) {
		RWLOCK_EXIT(&softa->ipf_authlk);
		SPL_X(s);
		IPFERROR(10015);
		return ESRCH;
	}
	if  (fra->fra_info.fin_id != au->fra_info.fin_id) {
		RWLOCK_EXIT(&softa->ipf_authlk);
		SPL_X(s);
		IPFERROR(10019);
		return ESRCH;
	}

	m = softa->ipf_auth_pkts[i];
	fra->fra_index = -2;
	fra->fra_pass = au->fra_pass;
	softa->ipf_auth_pkts[i] = NULL;
	softa->ipf_auth_replies++;
	bcopy(&fra->fra_info, &fin, sizeof(fin));

	RWLOCK_EXIT(&softa->ipf_authlk);

	/*
	 * Re-insert the packet back into the packet stream flowing through
	 * the kernel in a manner that will mean IPFilter sees the packet
	 * again.  This is not the same as is done with fastroute,
	 * deliberately, as we want to resume the normal packet processing
	 * path for it.
	 */
#ifdef	_KERNEL
	if ((m != NULL) && (au->fra_info.fin_out != 0)) {
		error = ipf_inject(&fin, m);
		if (error != 0) {
			IPFERROR(10016);
			error = ENOBUFS;
			softa->ipf_auth_stats.fas_sendfail++;
		} else {
			softa->ipf_auth_stats.fas_sendok++;
		}
	} else if (m) {
		error = ipf_inject(&fin, m);
		if (error != 0) {
			IPFERROR(10017);
			error = ENOBUFS;
			softa->ipf_auth_stats.fas_quefail++;
		} else {
			softa->ipf_auth_stats.fas_queok++;
		}
	} else {
		IPFERROR(10018);
		error = EINVAL;
	}

	/*
	 * If we experience an error which will result in the packet
	 * not being processed, make sure we advance to the next one.
	 */
	if (error == ENOBUFS) {
		WRITE_ENTER(&softa->ipf_authlk);
		softa->ipf_auth_used--;
		fra->fra_index = -1;
		fra->fra_pass = 0;
		if (i == softa->ipf_auth_start) {
			while (fra->fra_index == -1) {
				i++;
				if (i == softa->ipf_auth_size)
					i = 0;
				softa->ipf_auth_start = i;
				if (i == softa->ipf_auth_end)
					break;
			}
			if (softa->ipf_auth_start == softa->ipf_auth_end) {
				softa->ipf_auth_next = 0;
				softa->ipf_auth_start = 0;
				softa->ipf_auth_end = 0;
			}
		}
		RWLOCK_EXIT(&softa->ipf_authlk);
	}
#endif /* _KERNEL */
	SPL_X(s);

	return 0;
}


u_32_t
ipf_auth_pre_scanlist(softc, fin, pass)
	ipf_main_softc_t *softc;
	fr_info_t *fin;
	u_32_t pass;
{
	ipf_auth_softc_t *softa = softc->ipf_auth_soft;

	if (softa->ipf_auth_ip != NULL)
		return ipf_scanlist(fin, softc->ipf_pass);

	return pass;
}


frentry_t **
ipf_auth_rulehead(softc)
	ipf_main_softc_t *softc;
{
	ipf_auth_softc_t *softa = softc->ipf_auth_soft;

	return &softa->ipf_auth_ip;
}
