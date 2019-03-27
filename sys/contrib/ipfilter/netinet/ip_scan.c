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
#include <sys/time.h>
#include <sys/errno.h>
#if !defined(_KERNEL)
# include <stdlib.h>
# include <string.h>
# define _KERNEL
# include <sys/uio.h>
# undef _KERNEL
#else
# include <sys/systm.h>
# if !defined(__SVR4)
#  include <sys/mbuf.h>
# endif
#endif
#include <sys/socket.h>
# include <sys/ioccom.h>
#ifdef __FreeBSD__
# include <sys/filio.h>
# include <sys/malloc.h>
#else
# include <sys/ioctl.h>
#endif

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <net/if.h>


#include "netinet/ip_compat.h"
#include "netinet/ip_fil.h"
#include "netinet/ip_state.h"
#include "netinet/ip_scan.h"
/* END OF INCLUDES */

#if !defined(lint)
static const char sccsid[] = "@(#)ip_state.c	1.8 6/5/96 (C) 1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif

#ifdef	IPFILTER_SCAN	/* endif at bottom of file */


ipscan_t	*ipf_scan_list = NULL,
		*ipf_scan_tail = NULL;
ipscanstat_t	ipf_scan_stat;
# ifdef USE_MUTEXES
ipfrwlock_t	ipf_scan_rwlock;
# endif

# ifndef isalpha
#  define	isalpha(x)	(((x) >= 'A' && 'Z' >= (x)) || \
				 ((x) >= 'a' && 'z' >= (x)))
# endif


int ipf_scan_add __P((caddr_t));
int ipf_scan_remove __P((caddr_t));
struct ipscan *ipf_scan_lookup __P((char *));
int ipf_scan_matchstr __P((sinfo_t *, char *, int));
int ipf_scan_matchisc __P((ipscan_t *, ipstate_t *, int, int, int *));
int ipf_scan_match __P((ipstate_t *));

static int	ipf_scan_inited = 0;


int
ipf_scan_init()
{
	RWLOCK_INIT(&ipf_scan_rwlock, "ip scan rwlock");
	ipf_scan_inited = 1;
	return 0;
}


void
ipf_scan_unload(ipf_main_softc_t *arg)
{
	if (ipf_scan_inited == 1) {
		RW_DESTROY(&ipf_scan_rwlock);
		ipf_scan_inited = 0;
	}
}


int
ipf_scan_add(data)
	caddr_t data;
{
	ipscan_t *i, *isc;
	int err;

	KMALLOC(isc, ipscan_t *);
	if (!isc) {
		ipf_interror = 90001;
		return ENOMEM;
	}

	err = copyinptr(data, isc, sizeof(*isc));
	if (err) {
		KFREE(isc);
		return err;
	}

	WRITE_ENTER(&ipf_scan_rwlock);

	i = ipf_scan_lookup(isc->ipsc_tag);
	if (i != NULL) {
		RWLOCK_EXIT(&ipf_scan_rwlock);
		KFREE(isc);
		ipf_interror = 90002;
		return EEXIST;
	}

	if (ipf_scan_tail) {
		ipf_scan_tail->ipsc_next = isc;
		isc->ipsc_pnext = &ipf_scan_tail->ipsc_next;
		ipf_scan_tail = isc;
	} else {
		ipf_scan_list = isc;
		ipf_scan_tail = isc;
		isc->ipsc_pnext = &ipf_scan_list;
	}
	isc->ipsc_next = NULL;

	isc->ipsc_hits = 0;
	isc->ipsc_fref = 0;
	isc->ipsc_sref = 0;
	isc->ipsc_active = 0;

	ipf_scan_stat.iscs_entries++;
	RWLOCK_EXIT(&ipf_scan_rwlock);
	return 0;
}


int
ipf_scan_remove(data)
	caddr_t data;
{
	ipscan_t isc, *i;
	int err;

	err = copyinptr(data, &isc, sizeof(isc));
	if (err)
		return err;

	WRITE_ENTER(&ipf_scan_rwlock);

	i = ipf_scan_lookup(isc.ipsc_tag);
	if (i == NULL)
		err = ENOENT;
	else {
		if (i->ipsc_fref) {
			RWLOCK_EXIT(&ipf_scan_rwlock);
			ipf_interror = 90003;
			return EBUSY;
		}

		*i->ipsc_pnext = i->ipsc_next;
		if (i->ipsc_next)
			i->ipsc_next->ipsc_pnext = i->ipsc_pnext;
		else {
			if (i->ipsc_pnext == &ipf_scan_list)
				ipf_scan_tail = NULL;
			else
				ipf_scan_tail = *(*i->ipsc_pnext)->ipsc_pnext;
		}

		ipf_scan_stat.iscs_entries--;
		KFREE(i);
	}
	RWLOCK_EXIT(&ipf_scan_rwlock);
	return err;
}


struct ipscan *
ipf_scan_lookup(tag)
	char *tag;
{
	ipscan_t *i;

	for (i = ipf_scan_list; i; i = i->ipsc_next)
		if (!strcmp(i->ipsc_tag, tag))
			return i;
	return NULL;
}


int
ipf_scan_attachfr(fr)
	struct frentry *fr;
{
	ipscan_t *i;

	if (fr->fr_isctag != -1) {
		READ_ENTER(&ipf_scan_rwlock);
		i = ipf_scan_lookup(fr->fr_isctag + fr->fr_names);
		if (i != NULL) {
			ATOMIC_INC32(i->ipsc_fref);
		}
		RWLOCK_EXIT(&ipf_scan_rwlock);
		if (i == NULL) {
			ipf_interror = 90004;
			return ENOENT;
		}
		fr->fr_isc = i;
	}
	return 0;
}


int
ipf_scan_attachis(is)
	struct ipstate *is;
{
	frentry_t *fr;
	ipscan_t *i;

	READ_ENTER(&ipf_scan_rwlock);
	fr = is->is_rule;
	if (fr != NULL) {
		i = fr->fr_isc;
		if ((i != NULL) && (i != (ipscan_t *)-1)) {
			is->is_isc = i;
			ATOMIC_INC32(i->ipsc_sref);
			if (i->ipsc_clen)
				is->is_flags |= IS_SC_CLIENT;
			else
				is->is_flags |= IS_SC_MATCHC;
			if (i->ipsc_slen)
				is->is_flags |= IS_SC_SERVER;
			else
				is->is_flags |= IS_SC_MATCHS;
		}
	}
	RWLOCK_EXIT(&ipf_scan_rwlock);
	return 0;
}


int
ipf_scan_detachfr(fr)
	struct frentry *fr;
{
	ipscan_t *i;

	i = fr->fr_isc;
	if (i != NULL) {
		ATOMIC_DEC32(i->ipsc_fref);
	}
	return 0;
}


int
ipf_scan_detachis(is)
	struct ipstate *is;
{
	ipscan_t *i;

	READ_ENTER(&ipf_scan_rwlock);
	if ((i = is->is_isc) && (i != (ipscan_t *)-1)) {
		ATOMIC_DEC32(i->ipsc_sref);
		is->is_isc = NULL;
		is->is_flags &= ~(IS_SC_CLIENT|IS_SC_SERVER);
	}
	RWLOCK_EXIT(&ipf_scan_rwlock);
	return 0;
}


/*
 * 'string' compare for scanning
 */
int
ipf_scan_matchstr(sp, str, n)
	sinfo_t *sp;
	char *str;
	int n;
{
	char *s, *t, *up;
	int i = n;

	if (i > sp->s_len)
		i = sp->s_len;
	up = str;

	for (s = sp->s_txt, t = sp->s_msk; i; i--, s++, t++, up++)
		switch ((int)*t)
		{
		case '.' :
			if (*s != *up)
				return 1;
			break;
		case '?' :
			if (!ISALPHA(*up) || ((*s & 0x5f) != (*up & 0x5f)))
				return 1;
			break;
		case '*' :
			break;
		}
	return 0;
}


/*
 * Returns 3 if both server and client match, 2 if just server,
 * 1 if just client
 */
int
ipf_scan_matchisc(isc, is, cl, sl, maxm)
	ipscan_t *isc;
	ipstate_t *is;
	int cl, sl, maxm[2];
{
	int i, j, k, n, ret = 0, flags;

	flags = is->is_flags;

	/*
	 * If we've already matched more than what is on offer, then
	 * assume we have a better match already and forget this one.
	 */
	if (maxm != NULL) {
		if (isc->ipsc_clen < maxm[0])
			return 0;
		if (isc->ipsc_slen < maxm[1])
			return 0;
		j = maxm[0];
		k = maxm[1];
	} else {
		j = 0;
		k = 0;
	}

	if (!isc->ipsc_clen)
		ret = 1;
	else if (((flags & (IS_SC_MATCHC|IS_SC_CLIENT)) == IS_SC_CLIENT) &&
		 cl && isc->ipsc_clen) {
		i = 0;
		n = MIN(cl, isc->ipsc_clen);
		if ((n > 0) && (!maxm || (n >= maxm[1]))) {
			if (!ipf_scan_matchstr(&isc->ipsc_cl,
					       is->is_sbuf[0], n)) {
				i++;
				ret |= 1;
				if (n > j)
					j = n;
			}
		}
	}

	if (!isc->ipsc_slen)
		ret |= 2;
	else if (((flags & (IS_SC_MATCHS|IS_SC_SERVER)) == IS_SC_SERVER) &&
		 sl && isc->ipsc_slen) {
		i = 0;
		n = MIN(cl, isc->ipsc_slen);
		if ((n > 0) && (!maxm || (n >= maxm[1]))) {
			if (!ipf_scan_matchstr(&isc->ipsc_sl,
					       is->is_sbuf[1], n)) {
				i++;
				ret |= 2;
				if (n > k)
					k = n;
			}
		}
	}

	if (maxm && (ret == 3)) {
		maxm[0] = j;
		maxm[1] = k;
	}
	return ret;
}


int
ipf_scan_match(is)
	ipstate_t *is;
{
	int i, j, k, n, cl, sl, maxm[2];
	ipscan_t *isc, *lm;
	tcpdata_t *t;

	for (cl = 0, n = is->is_smsk[0]; n & 1; n >>= 1)
		cl++;
	for (sl = 0, n = is->is_smsk[1]; n & 1; n >>= 1)
		sl++;

	j = 0;
	isc = is->is_isc;
	if (isc != NULL) {
		/*
		 * Known object to scan for.
		 */
		i = ipf_scan_matchisc(isc, is, cl, sl, NULL);
		if (i & 1) {
			is->is_flags |= IS_SC_MATCHC;
			is->is_flags &= ~IS_SC_CLIENT;
		} else if (cl >= isc->ipsc_clen)
			is->is_flags &= ~IS_SC_CLIENT;
		if (i & 2) {
			is->is_flags |= IS_SC_MATCHS;
			is->is_flags &= ~IS_SC_SERVER;
		} else if (sl >= isc->ipsc_slen)
			is->is_flags &= ~IS_SC_SERVER;
	} else {
		i = 0;
		lm = NULL;
		maxm[0] = 0;
		maxm[1] = 0;
		for (k = 0, isc = ipf_scan_list; isc; isc = isc->ipsc_next) {
			i = ipf_scan_matchisc(isc, is, cl, sl, maxm);
			if (i) {
				/*
				 * We only want to remember the best match
				 * and the number of times we get a best
				 * match.
				 */
				if ((j == 3) && (i < 3))
					continue;
				if ((i == 3) && (j != 3))
					k = 1;
				else
					k++;
				j = i;
				lm = isc;
			}
		}
		if (k == 1)
			isc = lm;
		if (isc == NULL)
			return 0;

		/*
		 * No matches or partial matches, so reset the respective
		 * search flag.
		 */
		if (!(j & 1))
			is->is_flags &= ~IS_SC_CLIENT;

		if (!(j & 2))
			is->is_flags &= ~IS_SC_SERVER;

		/*
		 * If we found the best match, then set flags appropriately.
		 */
		if ((j == 3) && (k == 1)) {
			is->is_flags &= ~(IS_SC_SERVER|IS_SC_CLIENT);
			is->is_flags |= (IS_SC_MATCHS|IS_SC_MATCHC);
		}
	}

	/*
	 * If the acknowledged side of a connection has moved past the data in
	 * which we are interested, then reset respective flag.
	 */
	t = &is->is_tcp.ts_data[0];
	if (t->td_end > is->is_s0[0] + 15)
		is->is_flags &= ~IS_SC_CLIENT;

	t = &is->is_tcp.ts_data[1];
	if (t->td_end > is->is_s0[1] + 15)
		is->is_flags &= ~IS_SC_SERVER;

	/*
	 * Matching complete ?
	 */
	j = ISC_A_NONE;
	if ((is->is_flags & IS_SC_MATCHALL) == IS_SC_MATCHALL) {
		j = isc->ipsc_action;
		ipf_scan_stat.iscs_acted++;
	} else if ((is->is_isc != NULL) &&
		   ((is->is_flags & IS_SC_MATCHALL) != IS_SC_MATCHALL) &&
		   !(is->is_flags & (IS_SC_CLIENT|IS_SC_SERVER))) {
		/*
		 * Matching failed...
		 */
		j = isc->ipsc_else;
		ipf_scan_stat.iscs_else++;
	}

	switch (j)
	{
	case  ISC_A_CLOSE :
		/*
		 * If as a result of a successful match we are to
		 * close a connection, change the "keep state" info.
		 * to block packets and generate TCP RST's.
		 */
		is->is_pass &= ~FR_RETICMP;
		is->is_pass |= FR_RETRST;
		break;
	default :
		break;
	}

	return i;
}


/*
 * check if a packet matches what we're scanning for
 */
int
ipf_scan_packet(fin, is)
	fr_info_t *fin;
	ipstate_t *is;
{
	int i, j, rv, dlen, off, thoff;
	u_32_t seq, s0;
	tcphdr_t *tcp;

	rv = !IP6_EQ(&fin->fin_fi.fi_src, &is->is_src);
	tcp = fin->fin_dp;
	seq = ntohl(tcp->th_seq);

	if (!is->is_s0[rv])
		return 1;

	/*
	 * check if this packet has more data that falls within the first
	 * 16 bytes sent in either direction.
	 */
	s0 = is->is_s0[rv];
	off = seq - s0;
	if ((off > 15) || (off < 0))
		return 1;
	thoff = TCP_OFF(tcp) << 2;
	dlen = fin->fin_dlen - thoff;
	if (dlen <= 0)
		return 1;
	if (dlen > 16)
		dlen = 16;
	if (off + dlen > 16)
		dlen = 16 - off;

	j = 0xffff >> (16 - dlen);
	i = (0xffff & j) << off;
#ifdef _KERNEL
	COPYDATA(*(mb_t **)fin->fin_mp, fin->fin_plen - fin->fin_dlen + thoff,
		 dlen, (caddr_t)is->is_sbuf[rv] + off);
#endif
	is->is_smsk[rv] |= i;
	for (j = 0, i = is->is_smsk[rv]; i & 1; i >>= 1)
		j++;
	if (j == 0)
		return 1;

	(void) ipf_scan_match(is);
#if 0
	/*
	 * There is the potential here for plain text passwords to get
	 * buffered and stored for some time...
	 */
	if (!(is->is_flags & IS_SC_CLIENT))
		bzero(is->is_sbuf[0], sizeof(is->is_sbuf[0]));
	if (!(is->is_flags & IS_SC_SERVER))
		bzero(is->is_sbuf[1], sizeof(is->is_sbuf[1]));
#endif
	return 0;
}


int
ipf_scan_ioctl(data, cmd, mode, uid, ctx)
	caddr_t data;
	ioctlcmd_t cmd;
	int mode, uid;
	void *ctx;
{
	ipscanstat_t ipscs;
	int err = 0;

	switch (cmd)
	{
	case SIOCADSCA :
		err = ipf_scan_add(data);
		break;
	case SIOCRMSCA :
		err = ipf_scan_remove(data);
		break;
	case SIOCGSCST :
		bcopy((char *)&ipf_scan_stat, (char *)&ipscs, sizeof(ipscs));
		ipscs.iscs_list = ipf_scan_list;
		err = BCOPYOUT(&ipscs, data, sizeof(ipscs));
		if (err != 0) {
			ipf_interror = 90005;
			err = EFAULT;
		}
		break;
	default :
		err = EINVAL;
		break;
	}

	return err;
}
#endif	/* IPFILTER_SCAN */
