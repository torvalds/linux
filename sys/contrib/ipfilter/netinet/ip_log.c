/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $FreeBSD$
 * Id: ip_log.c,v 2.75.2.19 2007/09/09 11:32:06 darrenr Exp $
 */
#include <sys/param.h>
#if defined(KERNEL) || defined(_KERNEL)
# undef KERNEL
# undef _KERNEL
# define        KERNEL	1
# define        _KERNEL	1
#endif
#if defined(__FreeBSD__) && !defined(_KERNEL)
# include <osreldate.h>
#endif
#ifndef SOLARIS
# if defined(sun) && defined(__SVR4)
#  define	SOLARIS		1
# else
#  define	SOLARIS		0
# endif
#endif
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/file.h>
#ifndef _KERNEL
# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# include <ctype.h>
# define _KERNEL
# define KERNEL
# include <sys/uio.h>
# undef _KERNEL
# undef KERNEL
#endif
#if defined(__FreeBSD_version) && defined(_KERNEL)
# include <sys/fcntl.h>
# include <sys/filio.h>
#else
# include <sys/ioctl.h>
#endif
#include <sys/time.h>
#if defined(_KERNEL)
# include <sys/systm.h>
# if (defined(NetBSD) && (__NetBSD_Version__ >= 104000000))
#  include <sys/proc.h>
# endif
#endif /* _KERNEL */
# if defined(NetBSD) || defined(__FreeBSD_version)
#  include <sys/dirent.h>
# include <sys/mbuf.h>
# include <sys/select.h>
# endif
# if defined(__FreeBSD_version)
#  include <sys/selinfo.h>
# endif
#if SOLARIS && defined(_KERNEL)
#  include <sys/filio.h>
#  include <sys/cred.h>
#  include <sys/ddi.h>
#  include <sys/sunddi.h>
#  include <sys/ksynch.h>
#  include <sys/kmem.h>
#  include <sys/mkdev.h>
#  include <sys/dditypes.h>
#  include <sys/cmn_err.h>
#endif /* SOLARIS && _KERNEL */
# include <sys/protosw.h>
#include <sys/socket.h>

#include <net/if.h>
#ifdef sun
# include <net/af.h>
#endif
#if defined(__FreeBSD_version)
# include <net/if_var.h>
#endif
#include <netinet/in.h>
# include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#ifdef USE_INET6
# include <netinet/icmp6.h>
#endif
# include <netinet/ip_var.h>
#ifndef _KERNEL
# include <syslog.h>
#endif
#include "netinet/ip_compat.h"
#include <netinet/tcpip.h>
#include "netinet/ip_fil.h"
#include "netinet/ip_nat.h"
#include "netinet/ip_frag.h"
#include "netinet/ip_state.h"
#include "netinet/ip_auth.h"
#if defined(__FreeBSD_version) || defined(__NetBSD__)
# include <sys/malloc.h>
#endif
/* END OF INCLUDES */

#ifdef	IPFILTER_LOG

# if defined(IPL_SELECT)
#  include	<machine/sys/user.h>
#  include	<sys/kthread_iface.h>
#  define	READ_COLLISION	0x001
extern int selwait;
# endif /* IPL_SELECT */

typedef struct ipf_log_softc_s {
	ipfmutex_t	ipl_mutex[IPL_LOGSIZE];
# if SOLARIS && defined(_KERNEL)
	kcondvar_t	ipl_wait[IPL_LOGSIZE];
# endif
	iplog_t		**iplh[IPL_LOGSIZE];
	iplog_t		*iplt[IPL_LOGSIZE];
	iplog_t		*ipll[IPL_LOGSIZE];
	u_long		ipl_logfail[IPL_LOGSIZE];
	u_long		ipl_logok[IPL_LOGSIZE];
	fr_info_t	ipl_crc[IPL_LOGSIZE];
	u_32_t		ipl_counter[IPL_LOGSIZE];
	int		ipl_suppress;
	int		ipl_logall;
	int		ipl_log_init;
	int		ipl_logsize;
	int		ipl_used[IPL_LOGSIZE];
	int		ipl_magic[IPL_LOGSIZE];
	ipftuneable_t	*ipf_log_tune;
	int		ipl_readers[IPL_LOGSIZE];
} ipf_log_softc_t;

static int magic[IPL_LOGSIZE] = { IPL_MAGIC, IPL_MAGIC_NAT, IPL_MAGIC_STATE,
				  IPL_MAGIC, IPL_MAGIC, IPL_MAGIC,
				  IPL_MAGIC, IPL_MAGIC };

static ipftuneable_t ipf_log_tuneables[] = {
	/* log */
	{ { (void *)offsetof(ipf_log_softc_t, ipl_suppress) },
		"log_suppress",		0,	1,
		stsizeof(ipf_log_softc_t, ipl_suppress),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_log_softc_t, ipl_logall) },
		"log_all",		0,	1,
		stsizeof(ipf_log_softc_t, ipl_logall),
		0,			NULL,	NULL },
	{ { (void *)offsetof(ipf_log_softc_t, ipl_logsize) },
		"log_size",		0,	0x80000,
		stsizeof(ipf_log_softc_t, ipl_logsize),
		0,			NULL,	NULL },
	{ { NULL },		NULL,			0,	0,
		0,
		0,			NULL,	NULL }
};


int
ipf_log_main_load()
{
	return 0;
}


int
ipf_log_main_unload()
{
	return 0;
}

/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_soft_create                                         */
/* Returns:     void * - NULL = failure, else pointer to log context data   */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Initialise log buffers & pointers.  Also iniialised the CRC to a local   */
/* secret for use in calculating the "last log checksum".                   */
/* ------------------------------------------------------------------------ */
void *
ipf_log_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_log_softc_t *softl;
	int i;

	KMALLOC(softl, ipf_log_softc_t *);
	if (softl == NULL)
		return NULL;

	bzero((char *)softl, sizeof(*softl));
	bcopy((char *)magic, (char *)softl->ipl_magic, sizeof(magic));

	softl->ipf_log_tune = ipf_tune_array_copy(softl,
						  sizeof(ipf_log_tuneables),
						  ipf_log_tuneables);
	if (softl->ipf_log_tune == NULL) {
		ipf_log_soft_destroy(softc, softl);
		return NULL;
	}
	if (ipf_tune_array_link(softc, softl->ipf_log_tune) == -1) {
		ipf_log_soft_destroy(softc, softl);
		return NULL;
	}

	for (i = IPL_LOGMAX; i >= 0; i--) {
		MUTEX_INIT(&softl->ipl_mutex[i], "ipf log mutex");
	}

	softl->ipl_suppress = 1;
	softl->ipl_logall = 0;
	softl->ipl_log_init = 0;
	softl->ipl_logsize = IPFILTER_LOGSIZE;

	return softl;
}

/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_soft_init                                           */
/* Returns:     int - 0 == success (always returned)                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Initialise log buffers & pointers.  Also iniialised the CRC to a local   */
/* secret for use in calculating the "last log checksum".                   */
/* ------------------------------------------------------------------------ */
int
ipf_log_soft_init(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_log_softc_t *softl = arg;
	int i;

	for (i = IPL_LOGMAX; i >= 0; i--) {
		softl->iplt[i] = NULL;
		softl->ipll[i] = NULL;
		softl->iplh[i] = &softl->iplt[i];
		bzero((char *)&softl->ipl_crc[i], sizeof(softl->ipl_crc[i]));
# ifdef	IPL_SELECT
		softl->iplog_ss[i].read_waiter = 0;
		softl->iplog_ss[i].state = 0;
# endif
	}


	softl->ipl_log_init = 1;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_soft_fini                                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to log context structure                 */
/*                                                                          */
/* Clean up any log data that has accumulated without being read.           */
/* ------------------------------------------------------------------------ */
int
ipf_log_soft_fini(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_log_softc_t *softl = arg;
	int i;

	if (softl->ipl_log_init == 0)
		return 0;

	softl->ipl_log_init = 0;

	for (i = IPL_LOGMAX; i >= 0; i--) {
		(void) ipf_log_clear(softc, i);

		/*
		 * This is a busy-wait loop so as to avoid yet another lock
		 * to wait on.
		 */
		MUTEX_ENTER(&softl->ipl_mutex[i]);
		while (softl->ipl_readers[i] > 0) {
# if SOLARIS && defined(_KERNEL)
			cv_broadcast(&softl->ipl_wait[i]);
			MUTEX_EXIT(&softl->ipl_mutex[i]);
			delay(100);
			pollwakeup(&softc->ipf_poll_head[i], POLLRDNORM);
# else
			MUTEX_EXIT(&softl->ipl_mutex[i]);
			WAKEUP(softl->iplh, i);
			POLLWAKEUP(i);
# endif
			MUTEX_ENTER(&softl->ipl_mutex[i]);
		}
		MUTEX_EXIT(&softl->ipl_mutex[i]);
	}

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_soft_destroy                                        */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to log context structure                 */
/*                                                                          */
/* When this function is called, it is expected that there are no longer    */
/* any threads active in the reading code path or the logging code path.    */
/* ------------------------------------------------------------------------ */
void
ipf_log_soft_destroy(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_log_softc_t *softl = arg;
	int i;

	for (i = IPL_LOGMAX; i >= 0; i--) {
# if SOLARIS && defined(_KERNEL)
		cv_destroy(&softl->ipl_wait[i]);
# endif
		MUTEX_DESTROY(&softl->ipl_mutex[i]);
	}

	if (softl->ipf_log_tune != NULL) {
		ipf_tune_array_unlink(softc, softl->ipf_log_tune);
		KFREES(softl->ipf_log_tune, sizeof(ipf_log_tuneables));
		softl->ipf_log_tune = NULL;
	}

	KFREE(softl);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_pkt                                                 */
/* Returns:     int      - 0 == success, -1 == failure                      */
/* Parameters:  fin(I)   - pointer to packet information                    */
/*              flags(I) - flags from filter rules                          */
/*                                                                          */
/* Create a log record for a packet given that it has been triggered by a   */
/* rule (or the default setting).  Calculate the transport protocol header  */
/* size using predetermined size of a couple of popular protocols and thus  */
/* how much data to copy into the log, including part of the data body if   */
/* requested.                                                               */
/* ------------------------------------------------------------------------ */
int
ipf_log_pkt(fin, flags)
	fr_info_t *fin;
	u_int flags;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_log_softc_t *softl = softc->ipf_log_soft;
	register size_t hlen;
	int types[2], mlen;
	size_t sizes[2];
	void *ptrs[2];
	ipflog_t ipfl;
	u_char p;
	mb_t *m;
# if SOLARIS && defined(_KERNEL) && !defined(FW_HOOKS)
	qif_t *ifp;
# else
	struct ifnet *ifp;
# endif /* SOLARIS */

	m = fin->fin_m;
	if (m == NULL)
		return -1;

	ipfl.fl_nattag.ipt_num[0] = 0;
	ifp = fin->fin_ifp;
	hlen = (char *)fin->fin_dp - (char *)fin->fin_ip;

	/*
	 * calculate header size.
	 */
	if (fin->fin_off == 0) {
		p = fin->fin_fi.fi_p;
		if (p == IPPROTO_TCP)
			hlen += MIN(sizeof(tcphdr_t), fin->fin_dlen);
		else if (p == IPPROTO_UDP)
			hlen += MIN(sizeof(udphdr_t), fin->fin_dlen);
		else if (p == IPPROTO_ICMP) {
			struct icmp *icmp;

			icmp = (struct icmp *)fin->fin_dp;

			/*
			 * For ICMP, if the packet is an error packet, also
			 * include the information about the packet which
			 * caused the error.
			 */
			switch (icmp->icmp_type)
			{
			case ICMP_UNREACH :
			case ICMP_SOURCEQUENCH :
			case ICMP_REDIRECT :
			case ICMP_TIMXCEED :
			case ICMP_PARAMPROB :
				hlen += MIN(sizeof(struct icmp) + 8,
					    fin->fin_dlen);
				break;
			default :
				hlen += MIN(sizeof(struct icmp),
					    fin->fin_dlen);
				break;
			}
		}
# ifdef USE_INET6
		else if (p == IPPROTO_ICMPV6) {
			struct icmp6_hdr *icmp;

			icmp = (struct icmp6_hdr *)fin->fin_dp;

			/*
			 * For ICMPV6, if the packet is an error packet, also
			 * include the information about the packet which
			 * caused the error.
			 */
			if (icmp->icmp6_type < 128) {
				hlen += MIN(sizeof(struct icmp6_hdr) + 8,
					    fin->fin_dlen);
			} else {
				hlen += MIN(sizeof(struct icmp6_hdr),
					    fin->fin_dlen);
			}
		}
# endif
	}
	/*
	 * Get the interface number and name to which this packet is
	 * currently associated.
	 */
# if SOLARIS && defined(_KERNEL)
#  if !defined(FW_HOOKS)
	ipfl.fl_unit = (u_int)ifp->qf_ppa;
#  endif
	COPYIFNAME(fin->fin_v, ifp, ipfl.fl_ifname);
# else
#  if (defined(NetBSD) && (NetBSD  <= 1991011) && (NetBSD >= 199603)) || \
      defined(__FreeBSD_version)
	COPYIFNAME(fin->fin_v, ifp, ipfl.fl_ifname);
#  else
	ipfl.fl_unit = (u_int)ifp->if_unit;
#   if defined(_KERNEL)
	if ((ipfl.fl_ifname[0] = ifp->if_name[0]))
		if ((ipfl.fl_ifname[1] = ifp->if_name[1]))
			if ((ipfl.fl_ifname[2] = ifp->if_name[2]))
				ipfl.fl_ifname[3] = ifp->if_name[3];
#   else
	(void) strncpy(ipfl.fl_ifname, IFNAME(ifp), sizeof(ipfl.fl_ifname));
	ipfl.fl_ifname[sizeof(ipfl.fl_ifname) - 1] = '\0';
#   endif
#  endif
# endif /* __hpux || SOLARIS */
	mlen = fin->fin_plen - hlen;
	if (!softl->ipl_logall) {
		mlen = (flags & FR_LOGBODY) ? MIN(mlen, 128) : 0;
	} else if ((flags & FR_LOGBODY) == 0) {
		mlen = 0;
	}
	if (mlen < 0)
		mlen = 0;
	ipfl.fl_plen = (u_char)mlen;
	ipfl.fl_hlen = (u_char)hlen;
	ipfl.fl_rule = fin->fin_rule;
	(void) strncpy(ipfl.fl_group, fin->fin_group, FR_GROUPLEN);
	if (fin->fin_fr != NULL) {
		ipfl.fl_loglevel = fin->fin_fr->fr_loglevel;
		ipfl.fl_logtag = fin->fin_fr->fr_logtag;
	} else {
		ipfl.fl_loglevel = 0xffff;
		ipfl.fl_logtag = FR_NOLOGTAG;
	}
	if (fin->fin_nattag != NULL)
		bcopy(fin->fin_nattag, (void *)&ipfl.fl_nattag,
		      sizeof(ipfl.fl_nattag));
	ipfl.fl_flags = flags;
	ipfl.fl_breason = (fin->fin_reason & 0xff);
	ipfl.fl_dir = fin->fin_out;
	ipfl.fl_lflags = fin->fin_flx;
	ipfl.fl_family = fin->fin_family;
	ptrs[0] = (void *)&ipfl;
	sizes[0] = sizeof(ipfl);
	types[0] = 0;
# if defined(MENTAT) && defined(_KERNEL)
	/*
	 * Are we copied from the mblk or an aligned array ?
	 */
	if (fin->fin_ip == (ip_t *)m->b_rptr) {
		ptrs[1] = m;
		sizes[1] = hlen + mlen;
		types[1] = 1;
	} else {
		ptrs[1] = fin->fin_ip;
		sizes[1] = hlen + mlen;
		types[1] = 0;
	}
# else
	ptrs[1] = m;
	sizes[1] = hlen + mlen;
	types[1] = 1;
# endif /* MENTAT */
	return ipf_log_items(softc, IPL_LOGIPF, fin, ptrs, sizes, types, 2);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_items                                               */
/* Returns:     int       - 0 == success, -1 == failure                     */
/* Parameters:  softc(I)  - pointer to main soft context                    */
/*              unit(I)   - device we are reading from                      */
/*              fin(I)    - pointer to packet information                   */
/*              items(I)  - array of pointers to log data                   */
/*              itemsz(I) - array of size of valid memory pointed to        */
/*              types(I)  - type of data pointed to by items pointers       */
/*              cnt(I)    - number of elements in arrays items/itemsz/types */
/*                                                                          */
/* Takes an array of parameters and constructs one record to include the    */
/* miscellaneous packet information, as well as packet data, for reading    */
/* from the log device.                                                     */
/* ------------------------------------------------------------------------ */
int
ipf_log_items(softc, unit, fin, items, itemsz, types, cnt)
	ipf_main_softc_t *softc;
	int unit;
	fr_info_t *fin;
	void **items;
	size_t *itemsz;
	int *types, cnt;
{
	ipf_log_softc_t *softl = softc->ipf_log_soft;
	caddr_t buf, ptr;
	iplog_t *ipl;
	size_t len;
	int i;
	SPL_INT(s);

	/*
	 * Get the total amount of data to be logged.
	 */
	for (i = 0, len = sizeof(iplog_t); i < cnt; i++)
		len += itemsz[i];

	SPL_NET(s);
	MUTEX_ENTER(&softl->ipl_mutex[unit]);
	softl->ipl_counter[unit]++;
	/*
	 * check that we have space to record this information and can
	 * allocate that much.
	 */
	if ((softl->ipl_used[unit] + len) > softl->ipl_logsize) {
		softl->ipl_logfail[unit]++;
		MUTEX_EXIT(&softl->ipl_mutex[unit]);
		return -1;
	}

	KMALLOCS(buf, caddr_t, len);
	if (buf == NULL) {
		softl->ipl_logfail[unit]++;
		MUTEX_EXIT(&softl->ipl_mutex[unit]);
		return -1;
	}
	ipl = (iplog_t *)buf;
	ipl->ipl_magic = softl->ipl_magic[unit];
	ipl->ipl_count = 1;
	ipl->ipl_seqnum = softl->ipl_counter[unit];
	ipl->ipl_next = NULL;
	ipl->ipl_dsize = len;
#ifdef _KERNEL
	GETKTIME(&ipl->ipl_sec);
#else
	ipl->ipl_sec = 0;
	ipl->ipl_usec = 0;
#endif

	/*
	 * Loop through all the items to be logged, copying each one to the
	 * buffer.  Use bcopy for normal data or the mb_t copyout routine.
	 */
	for (i = 0, ptr = buf + sizeof(*ipl); i < cnt; i++) {
		if (types[i] == 0) {
			bcopy(items[i], ptr, itemsz[i]);
		} else if (types[i] == 1) {
			COPYDATA(items[i], 0, itemsz[i], ptr);
		}
		ptr += itemsz[i];
	}
	/*
	 * Check to see if this log record has a CRC which matches the last
	 * record logged.  If it does, just up the count on the previous one
	 * rather than create a new one.
	 */
	if (softl->ipl_suppress) {
		if ((fin != NULL) && (fin->fin_off == 0)) {
			if ((softl->ipll[unit] != NULL) &&
			    (fin->fin_crc == softl->ipl_crc[unit].fin_crc) &&
			    bcmp((char *)fin, (char *)&softl->ipl_crc[unit],
				 FI_LCSIZE) == 0) {
				softl->ipll[unit]->ipl_count++;
				MUTEX_EXIT(&softl->ipl_mutex[unit]);
				SPL_X(s);
				KFREES(buf, len);
				return 0;
			}
			bcopy((char *)fin, (char *)&softl->ipl_crc[unit],
			      FI_LCSIZE);
			softl->ipl_crc[unit].fin_crc = fin->fin_crc;
		} else
			bzero((char *)&softl->ipl_crc[unit], FI_CSIZE);
	}

	/*
	 * advance the log pointer to the next empty record and deduct the
	 * amount of space we're going to use.
	 */
	softl->ipl_logok[unit]++;
	softl->ipll[unit] = ipl;
	*softl->iplh[unit] = ipl;
	softl->iplh[unit] = &ipl->ipl_next;
	softl->ipl_used[unit] += len;

	/*
	 * Now that the log record has been completed and added to the queue,
	 * wake up any listeners who may want to read it.
	 */
# if SOLARIS && defined(_KERNEL)
	cv_signal(&softl->ipl_wait[unit]);
	MUTEX_EXIT(&softl->ipl_mutex[unit]);
	pollwakeup(&softc->ipf_poll_head[unit], POLLRDNORM);
# else
	MUTEX_EXIT(&softl->ipl_mutex[unit]);
	WAKEUP(softl->iplh, unit);
	POLLWAKEUP(unit);
# endif
	SPL_X(s);
# ifdef	IPL_SELECT
	iplog_input_ready(unit);
# endif
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_read                                                */
/* Returns:     int      - 0 == success, else error value.                  */
/* Parameters:  softc(I) - pointer to main soft context                     */
/*              unit(I)  - device we are reading from                       */
/*              uio(O)   - pointer to information about where to store data */
/*                                                                          */
/* Called to handle a read on an IPFilter device.  Returns only complete    */
/* log messages - will not partially copy a log record out to userland.     */
/*                                                                          */
/* NOTE: This function will block and wait for a signal to return data if   */
/* there is none present.  Asynchronous I/O is not implemented.             */
/* ------------------------------------------------------------------------ */
int
ipf_log_read(softc, unit, uio)
	ipf_main_softc_t *softc;
	minor_t unit;
	struct uio *uio;
{
	ipf_log_softc_t *softl = softc->ipf_log_soft;
	size_t dlen, copied;
	int error = 0;
	iplog_t *ipl;
	SPL_INT(s);

	if (softl->ipl_log_init == 0) {
		IPFERROR(40007);
		return 0;
	}

	/*
	 * Sanity checks.  Make sure the minor # is valid and we're copying
	 * a valid chunk of data.
	 */
	if (IPL_LOGMAX < unit) {
		IPFERROR(40001);
		return ENXIO;
	}
	if (uio->uio_resid == 0)
		return 0;

	if (uio->uio_resid < sizeof(iplog_t)) {
		IPFERROR(40002);
		return EINVAL;
	}
	if (uio->uio_resid > softl->ipl_logsize) {
		IPFERROR(40005);
		return EINVAL;
	}

	/*
	 * Lock the log so we can snapshot the variables.  Wait for a signal
	 * if the log is empty.
	 */
	SPL_NET(s);
	MUTEX_ENTER(&softl->ipl_mutex[unit]);
	softl->ipl_readers[unit]++;

	while (softl->ipl_log_init == 1 && softl->iplt[unit] == NULL) {
# if SOLARIS && defined(_KERNEL)
		if (!cv_wait_sig(&softl->ipl_wait[unit],
				 &softl->ipl_mutex[unit].ipf_lk)) {
			softl->ipl_readers[unit]--;
			MUTEX_EXIT(&softl->ipl_mutex[unit]);
			IPFERROR(40003);
			return EINTR;
		}
# else
		MUTEX_EXIT(&softl->ipl_mutex[unit]);
		SPL_X(s);
		error = SLEEP(unit + softl->iplh, "ipl sleep");
		SPL_NET(s);
		MUTEX_ENTER(&softl->ipl_mutex[unit]);
		if (error) {
			softl->ipl_readers[unit]--;
			MUTEX_EXIT(&softl->ipl_mutex[unit]);
			IPFERROR(40004);
			return error;
		}
# endif /* SOLARIS */
	}
	if (softl->ipl_log_init != 1) {
		softl->ipl_readers[unit]--;
		MUTEX_EXIT(&softl->ipl_mutex[unit]);
		IPFERROR(40008);
		return EIO;
	}

# if (defined(BSD) && (BSD >= 199101)) || defined(__FreeBSD__)
	uio->uio_rw = UIO_READ;
# endif

	for (copied = 0; (ipl = softl->iplt[unit]) != NULL; copied += dlen) {
		dlen = ipl->ipl_dsize;
		if (dlen > uio->uio_resid)
			break;
		/*
		 * Don't hold the mutex over the uiomove call.
		 */
		softl->iplt[unit] = ipl->ipl_next;
		softl->ipl_used[unit] -= dlen;
		MUTEX_EXIT(&softl->ipl_mutex[unit]);
		SPL_X(s);
		error = UIOMOVE(ipl, dlen, UIO_READ, uio);
		if (error) {
			SPL_NET(s);
			MUTEX_ENTER(&softl->ipl_mutex[unit]);
			IPFERROR(40006);
			ipl->ipl_next = softl->iplt[unit];
			softl->iplt[unit] = ipl;
			softl->ipl_used[unit] += dlen;
			break;
		}
		MUTEX_ENTER(&softl->ipl_mutex[unit]);
		KFREES((caddr_t)ipl, dlen);
		SPL_NET(s);
	}
	if (!softl->iplt[unit]) {
		softl->ipl_used[unit] = 0;
		softl->iplh[unit] = &softl->iplt[unit];
		softl->ipll[unit] = NULL;
	}

	softl->ipl_readers[unit]--;
	MUTEX_EXIT(&softl->ipl_mutex[unit]);
	SPL_X(s);
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_clear                                               */
/* Returns:     int      - number of log bytes cleared.                     */
/* Parameters:  softc(I) - pointer to main soft context                     */
/*              unit(I)  - device we are reading from                       */
/*                                                                          */
/* Deletes all queued up log records for a given output device.             */
/* ------------------------------------------------------------------------ */
int
ipf_log_clear(softc, unit)
	ipf_main_softc_t *softc;
	minor_t unit;
{
	ipf_log_softc_t *softl = softc->ipf_log_soft;
	iplog_t *ipl;
	int used;
	SPL_INT(s);

	SPL_NET(s);
	MUTEX_ENTER(&softl->ipl_mutex[unit]);
	while ((ipl = softl->iplt[unit]) != NULL) {
		softl->iplt[unit] = ipl->ipl_next;
		KFREES((caddr_t)ipl, ipl->ipl_dsize);
	}
	softl->iplh[unit] = &softl->iplt[unit];
	softl->ipll[unit] = NULL;
	used = softl->ipl_used[unit];
	softl->ipl_used[unit] = 0;
	bzero((char *)&softl->ipl_crc[unit], FI_CSIZE);
	MUTEX_EXIT(&softl->ipl_mutex[unit]);
	SPL_X(s);
	return used;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_canread                                             */
/* Returns:     int      - 0 == no data to read, 1 = data present           */
/* Parameters:  softc(I) - pointer to main soft context                     */
/*              unit(I)  - device we are reading from                       */
/*                                                                          */
/* Returns an indication of whether or not there is data present in the     */
/* current buffer for the selected ipf device.                              */
/* ------------------------------------------------------------------------ */
int
ipf_log_canread(softc, unit)
	ipf_main_softc_t *softc;
	int unit;
{
	ipf_log_softc_t *softl = softc->ipf_log_soft;

	return softl->iplt[unit] != NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_canread                                             */
/* Returns:     int      - 0 == no data to read, 1 = data present           */
/* Parameters:  softc(I) - pointer to main soft context                     */
/*              unit(I)  - device we are reading from                       */
/*                                                                          */
/* Returns how many bytes are currently held in log buffers for the         */
/* selected ipf device.                                                     */
/* ------------------------------------------------------------------------ */
int
ipf_log_bytesused(softc, unit)
	ipf_main_softc_t *softc;
	int unit;
{
	ipf_log_softc_t *softl = softc->ipf_log_soft;

	if (softl == NULL)
		return 0;

	return softl->ipl_used[unit];
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_failures                                            */
/* Returns:     U_QUAD_T - number of log failures                           */
/* Parameters:  softc(I) - pointer to main soft context                     */
/*              unit(I)  - device we are reading from                       */
/*                                                                          */
/* Returns how many times we've tried to log a packet but failed to do so   */
/* for the selected ipf device.                                             */
/* ------------------------------------------------------------------------ */
u_long
ipf_log_failures(softc, unit)
	ipf_main_softc_t *softc;
	int unit;
{
	ipf_log_softc_t *softl = softc->ipf_log_soft;

	if (softl == NULL)
		return 0;

	return softl->ipl_logfail[unit];
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_log_logok                                               */
/* Returns:     U_QUAD_T - number of packets logged                         */
/* Parameters:  softc(I) - pointer to main soft context                     */
/*              unit(I)  - device we are reading from                       */
/*                                                                          */
/* Returns how many times we've successfully logged a packet for the        */
/* selected ipf device.                                                     */
/* ------------------------------------------------------------------------ */
u_long
ipf_log_logok(softc, unit)
	ipf_main_softc_t *softc;
	int unit;
{
	ipf_log_softc_t *softl = softc->ipf_log_soft;

	if (softl == NULL)
		return 0;

	return softl->ipl_logok[unit];
}
#endif /* IPFILTER_LOG */
