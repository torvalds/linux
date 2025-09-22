/*	$OpenBSD: bpf.c,v 1.234 2025/07/07 02:28:50 jsg Exp $	*/
/*	$NetBSD: bpf.c,v 1.33 1997/02/21 23:59:35 thorpej Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2010, 2014 Henning Brauer <henning@openbsd.org>
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)bpf.c	8.2 (Berkeley) 3/28/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/timeout.h>
#include <sys/signalvar.h>
#include <sys/ioctl.h>
#include <sys/conf.h>
#include <sys/vnode.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/atomic.h>
#include <sys/event.h>
#include <sys/mutex.h>
#include <sys/refcnt.h>
#include <sys/smr.h>
#include <sys/specdev.h>
#include <sys/sigio.h>
#include <sys/task.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/bpf.h>
#include <net/bpfdesc.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include "vlan.h"

#define BPF_BUFSIZE 32768

#define BPF_S_IDLE	0
#define BPF_S_WAIT	1
#define BPF_S_DONE	2

#define PRINET  26			/* interruptible */

/*
 * Locks used to protect data:
 *	a	atomic
 */

/*
 * The default read buffer size is patchable.
 */
int bpf_bufsize = BPF_BUFSIZE;		/* [a] */
int bpf_maxbufsize = BPF_MAXBUFSIZE;	/* [a] */

/*
 *  bpf_iflist is the list of interfaces; each corresponds to an ifnet
 *  bpf_d_list is the list of descriptors
 */
TAILQ_HEAD(, bpf_if) bpf_iflist = TAILQ_HEAD_INITIALIZER(bpf_iflist);
LIST_HEAD(, bpf_d) bpf_d_list = LIST_HEAD_INITIALIZER(bpf_d_list);

int	bpf_allocbufs(struct bpf_d *);
void	bpf_ifname(struct bpf_if*, struct ifreq *);
void	bpf_mcopy(const void *, void *, size_t);
int	bpf_movein(struct uio *, struct bpf_d *, struct mbuf **,
	    struct sockaddr *);
int	bpf_setif(struct bpf_d *, struct ifreq *);
int	bpfkqfilter(dev_t, struct knote *);
void	bpf_wakeup(struct bpf_d *);
void	bpf_wakeup_cb(void *);
void	bpf_wait_cb(void *);
int	_bpf_mtap(caddr_t, const struct mbuf *, const struct mbuf *, u_int);
void	bpf_catchpacket(struct bpf_d *, u_char *, size_t, size_t,
	    const struct bpf_hdr *);
int	bpf_getdltlist(struct bpf_d *, struct bpf_dltlist *);
int	bpf_setdlt(struct bpf_d *, u_int);

void	filt_bpfrdetach(struct knote *);
int	filt_bpfread(struct knote *, long);
int	filt_bpfreadmodify(struct kevent *, struct knote *);
int	filt_bpfreadprocess(struct knote *, struct kevent *);

struct bpf_d *bpfilter_lookup(int);

/*
 * Called holding ``bd_mtx''.
 */
void	bpf_attachd(struct bpf_d *, struct bpf_if *);
void	bpf_detachd(struct bpf_d *);
void	bpf_resetd(struct bpf_d *);

void	bpf_prog_smr(void *);
void	bpf_d_smr(void *);

/*
 * Reference count access to descriptor buffers
 */
void	bpf_get(struct bpf_d *);
void	bpf_put(struct bpf_d *);

int
bpf_movein(struct uio *uio, struct bpf_d *d, struct mbuf **mp,
    struct sockaddr *sockp)
{
	struct bpf_program_smr *bps;
	struct bpf_insn *fcode = NULL;
	struct mbuf *m;
	struct m_tag *mtag;
	int error;
	u_int hlen, alen, mlen;
	u_int len;
	u_int linktype;
	u_int slen;

	/*
	 * Build a sockaddr based on the data link layer type.
	 * We do this at this level because the ethernet header
	 * is copied directly into the data field of the sockaddr.
	 * In the case of SLIP, there is no header and the packet
	 * is forwarded as is.
	 * Also, we are careful to leave room at the front of the mbuf
	 * for the link level header.
	 */
	linktype = d->bd_bif->bif_dlt;
	switch (linktype) {

	case DLT_SLIP:
		sockp->sa_family = AF_INET;
		hlen = 0;
		break;

	case DLT_PPP:
		sockp->sa_family = AF_UNSPEC;
		hlen = 0;
		break;

	case DLT_EN10MB:
		sockp->sa_family = AF_UNSPEC;
		/* XXX Would MAXLINKHDR be better? */
		hlen = ETHER_HDR_LEN;
		break;

	case DLT_IEEE802_11:
	case DLT_IEEE802_11_RADIO:
		sockp->sa_family = AF_UNSPEC;
		hlen = 0;
		break;

	case DLT_RAW:
	case DLT_NULL:
		sockp->sa_family = AF_UNSPEC;
		hlen = 0;
		break;

	case DLT_LOOP:
		sockp->sa_family = AF_UNSPEC;
		hlen = sizeof(u_int32_t);
		break;

	default:
		return (EIO);
	}

	if (uio->uio_resid > MAXMCLBYTES)
		return (EMSGSIZE);
	len = uio->uio_resid;
	if (len < hlen)
		return (EINVAL);

	/*
	 * Get the length of the payload so we can align it properly.
	 */
	alen = len - hlen;

	/*
	 * Allocate enough space for headers and the aligned payload.
	 */
	mlen = max(max_linkhdr, hlen) + roundup(alen, sizeof(long));
	if (mlen > MAXMCLBYTES)
		return (EMSGSIZE);

	MGETHDR(m, M_WAIT, MT_DATA);
	if (mlen > MHLEN) {
		MCLGETL(m, M_WAIT, mlen);
		if ((m->m_flags & M_EXT) == 0) {
			error = ENOBUFS;
			goto bad;
		}
	}

	m_align(m, alen); /* Align the payload. */
	m->m_data -= hlen;

	m->m_pkthdr.ph_ifidx = 0;
	m->m_pkthdr.len = len;
	m->m_len = len;

	error = uiomove(mtod(m, caddr_t), len, uio);
	if (error)
		goto bad;

	smr_read_enter();
	bps = SMR_PTR_GET(&d->bd_wfilter);
	if (bps != NULL)
		fcode = bps->bps_bf.bf_insns;
	slen = bpf_filter(fcode, mtod(m, u_char *), len, len);
	smr_read_leave();

	if (slen < len) {
		error = EPERM;
		goto bad;
	}

	/*
	 * Make room for link header, and copy it to sockaddr
	 */
	if (hlen != 0) {
		if (linktype == DLT_LOOP) {
			u_int32_t af;

			/* the link header indicates the address family */
			KASSERT(hlen == sizeof(u_int32_t));
			memcpy(&af, m->m_data, hlen);
			sockp->sa_family = ntohl(af);
		} else
			memcpy(sockp->sa_data, m->m_data, hlen);

		m->m_pkthdr.len -= hlen;
		m->m_len -= hlen;
		m->m_data += hlen;
	}

	/*
	 * Prepend the data link type as a mbuf tag
	 */
	mtag = m_tag_get(PACKET_TAG_DLT, sizeof(u_int), M_WAIT);
	*(u_int *)(mtag + 1) = linktype;
	m_tag_prepend(m, mtag);

	*mp = m;
	return (0);
 bad:
	m_freem(m);
	return (error);
}

/*
 * Attach file to the bpf interface, i.e. make d listen on bp.
 */
void
bpf_attachd(struct bpf_d *d, struct bpf_if *bp)
{
	MUTEX_ASSERT_LOCKED(&d->bd_mtx);

	/*
	 * Point d at bp, and add d to the interface's list of listeners.
	 * Finally, point the driver's bpf cookie at the interface so
	 * it will divert packets to bpf.
	 */

	d->bd_bif = bp;

	KERNEL_ASSERT_LOCKED();
	SMR_SLIST_INSERT_HEAD_LOCKED(&bp->bif_dlist, d, bd_next);

	*bp->bif_driverp = bp;
}

/*
 * Detach a file from its interface.
 */
void
bpf_detachd(struct bpf_d *d)
{
	struct bpf_if *bp;

	MUTEX_ASSERT_LOCKED(&d->bd_mtx);

	bp = d->bd_bif;
	/* Not attached. */
	if (bp == NULL)
		return;

	/* Remove ``d'' from the interface's descriptor list. */
	KERNEL_ASSERT_LOCKED();
	SMR_SLIST_REMOVE_LOCKED(&bp->bif_dlist, d, bpf_d, bd_next);

	if (SMR_SLIST_EMPTY_LOCKED(&bp->bif_dlist)) {
		/*
		 * Let the driver know that there are no more listeners.
		 */
		*bp->bif_driverp = NULL;
	}

	d->bd_bif = NULL;

	/*
	 * Check if this descriptor had requested promiscuous mode.
	 * If so, turn it off.
	 */
	if (d->bd_promisc) {
		int error;

		KASSERT(bp->bif_ifp != NULL);

		d->bd_promisc = 0;

		bpf_get(d);
		mtx_leave(&d->bd_mtx);
		NET_LOCK();
		error = ifpromisc(bp->bif_ifp, 0);
		NET_UNLOCK();
		mtx_enter(&d->bd_mtx);
		bpf_put(d);

		if (error && !(error == EINVAL || error == ENODEV ||
		    error == ENXIO))
			/*
			 * Something is really wrong if we were able to put
			 * the driver into promiscuous mode, but can't
			 * take it out.
			 */
			panic("bpf: ifpromisc failed");
	}
}

void
bpfilterattach(int n)
{
}

/*
 * Open ethernet device.  Returns ENXIO for illegal minor device number,
 * EBUSY if file is open by another process.
 */
int
bpfopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct bpf_d *bd;
	int unit = minor(dev);

	if (unit & ((1 << CLONE_SHIFT) - 1))
		return (ENXIO);

	KASSERT(bpfilter_lookup(unit) == NULL);

	/* create on demand */
	if ((bd = malloc(sizeof(*bd), M_DEVBUF, M_NOWAIT|M_ZERO)) == NULL)
		return (EBUSY);

	/* Mark "free" and do most initialization. */
	bd->bd_unit = unit;
	bd->bd_bufsize = atomic_load_int(&bpf_bufsize);
	bd->bd_sig = SIGIO;
	mtx_init(&bd->bd_mtx, IPL_NET);
	task_set(&bd->bd_wake_task, bpf_wakeup_cb, bd);
	timeout_set(&bd->bd_wait_tmo, bpf_wait_cb, bd);
	smr_init(&bd->bd_smr);
	sigio_init(&bd->bd_sigio);
	klist_init_mutex(&bd->bd_klist, &bd->bd_mtx);

	bd->bd_rtout = 0;	/* no timeout by default */
	bd->bd_wtout = INFSLP;	/* wait for the buffer to fill by default */

	refcnt_init(&bd->bd_refcnt);
	LIST_INSERT_HEAD(&bpf_d_list, bd, bd_list);

	return (0);
}

/*
 * Close the descriptor by detaching it from its interface,
 * deallocating its buffers, and marking it free.
 */
int
bpfclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct bpf_d *d;

	d = bpfilter_lookup(minor(dev));
	mtx_enter(&d->bd_mtx);
	bpf_detachd(d);
	bpf_wakeup(d);
	LIST_REMOVE(d, bd_list);
	mtx_leave(&d->bd_mtx);
	bpf_put(d);

	return (0);
}

/*
 * Rotate the packet buffers in descriptor d.  Move the store buffer
 * into the hold slot, and the free buffer into the store slot.
 * Zero the length of the new store buffer.
 */
#define ROTATE_BUFFERS(d) \
	KASSERT(d->bd_in_uiomove == 0); \
	MUTEX_ASSERT_LOCKED(&d->bd_mtx); \
	(d)->bd_hbuf = (d)->bd_sbuf; \
	(d)->bd_hlen = (d)->bd_slen; \
	(d)->bd_sbuf = (d)->bd_fbuf; \
	(d)->bd_state = BPF_S_IDLE; \
	(d)->bd_slen = 0; \
	(d)->bd_fbuf = NULL;

/*
 *  bpfread - read next chunk of packets from buffers
 */
int
bpfread(dev_t dev, struct uio *uio, int ioflag)
{
	uint64_t end, now;
	struct bpf_d *d;
	caddr_t hbuf;
	int error, hlen;

	KERNEL_ASSERT_LOCKED();

	d = bpfilter_lookup(minor(dev));
	if (d->bd_bif == NULL)
		return (ENXIO);

	bpf_get(d);
	mtx_enter(&d->bd_mtx);

	/*
	 * Restrict application to use a buffer the same size as
	 * as kernel buffers.
	 */
	if (uio->uio_resid != d->bd_bufsize) {
		error = EINVAL;
		goto out;
	}

	/*
	 * If there's a timeout, mark when the read should end.
	 */
	if (d->bd_rtout != 0) {
		now = nsecuptime();
		end = now + d->bd_rtout;
		if (end < now)
			end = UINT64_MAX;
	}

	/*
	 * If the hold buffer is empty, then do a timed sleep, which
	 * ends when the timeout expires or when enough packets
	 * have arrived to fill the store buffer.
	 */
	while (d->bd_hbuf == NULL) {
		if (d->bd_bif == NULL) {
			/* interface is gone */
			if (d->bd_slen == 0) {
				error = EIO;
				goto out;
			}
			ROTATE_BUFFERS(d);
			break;
		}
		if (d->bd_state == BPF_S_DONE) {
			/*
			 * A packet(s) either arrived since the previous
			 * read or arrived while we were asleep.
			 * Rotate the buffers and return what's here.
			 */
			ROTATE_BUFFERS(d);
			break;
		}
		if (ISSET(ioflag, IO_NDELAY)) {
			/* User requested non-blocking I/O */
			error = EWOULDBLOCK;
		} else if (d->bd_rtout == 0) {
			/* No read timeout set. */
			d->bd_nreaders++;
			error = msleep_nsec(d, &d->bd_mtx, PRINET|PCATCH,
			    "bpf", INFSLP);
			d->bd_nreaders--;
		} else if ((now = nsecuptime()) < end) {
			/* Read timeout has not expired yet. */
			d->bd_nreaders++;
			error = msleep_nsec(d, &d->bd_mtx, PRINET|PCATCH,
			    "bpf", end - now);
			d->bd_nreaders--;
		} else {
			/* Read timeout has expired. */
			error = EWOULDBLOCK;
		}
		if (error == EINTR || error == ERESTART)
			goto out;
		if (error == EWOULDBLOCK) {
			/*
			 * On a timeout, return what's in the buffer,
			 * which may be nothing.  If there is something
			 * in the store buffer, we can rotate the buffers.
			 */
			if (d->bd_hbuf != NULL)
				/*
				 * We filled up the buffer in between
				 * getting the timeout and arriving
				 * here, so we don't need to rotate.
				 */
				break;

			if (d->bd_slen == 0) {
				error = 0;
				goto out;
			}
			ROTATE_BUFFERS(d);
			break;
		}
	}
	/*
	 * At this point, we know we have something in the hold slot.
	 */
	hbuf = d->bd_hbuf;
	hlen = d->bd_hlen;
	d->bd_hbuf = NULL;
	d->bd_hlen = 0;
	d->bd_fbuf = NULL;
	d->bd_in_uiomove = 1;

	/*
	 * Move data from hold buffer into user space.
	 * We know the entire buffer is transferred since
	 * we checked above that the read buffer is bpf_bufsize bytes.
	 */
	mtx_leave(&d->bd_mtx);
	error = uiomove(hbuf, hlen, uio);
	mtx_enter(&d->bd_mtx);

	/* Ensure that bpf_resetd() or ROTATE_BUFFERS() haven't been called. */
	KASSERT(d->bd_fbuf == NULL);
	KASSERT(d->bd_hbuf == NULL);
	d->bd_fbuf = hbuf;
	d->bd_in_uiomove = 0;
out:
	mtx_leave(&d->bd_mtx);
	bpf_put(d);

	return (error);
}

/*
 * If there are processes sleeping on this descriptor, wake them up.
 */
void
bpf_wakeup(struct bpf_d *d)
{
	MUTEX_ASSERT_LOCKED(&d->bd_mtx);

	if (d->bd_nreaders)
		wakeup(d);

	knote_locked(&d->bd_klist, 0);

	/*
	 * As long as pgsigio() needs to be protected
	 * by the KERNEL_LOCK() we have to delay the wakeup to
	 * another context to keep the hot path KERNEL_LOCK()-free.
	 */
	if (d->bd_async && d->bd_sig) {
		bpf_get(d);
		if (!task_add(systq, &d->bd_wake_task))
			bpf_put(d);
	}
}

void
bpf_wakeup_cb(void *xd)
{
	struct bpf_d *d = xd;

	if (d->bd_async && d->bd_sig)
		pgsigio(&d->bd_sigio, d->bd_sig, 0);

	bpf_put(d);
}

void
bpf_wait_cb(void *xd)
{
	struct bpf_d *d = xd;

	mtx_enter(&d->bd_mtx);
	if (d->bd_state == BPF_S_WAIT) {
		d->bd_state = BPF_S_DONE;
		bpf_wakeup(d);
	}
	mtx_leave(&d->bd_mtx);

	bpf_put(d);
}

int
bpfwrite(dev_t dev, struct uio *uio, int ioflag)
{
	struct bpf_d *d;
	struct ifnet *ifp;
	struct mbuf *m;
	int error;
	struct sockaddr_storage dst;

	KERNEL_ASSERT_LOCKED();

	d = bpfilter_lookup(minor(dev));
	if (d->bd_bif == NULL)
		return (ENXIO);

	bpf_get(d);
	ifp = d->bd_bif->bif_ifp;

	if (ifp == NULL || (ifp->if_flags & IFF_UP) == 0) {
		error = ENETDOWN;
		goto out;
	}

	if (uio->uio_resid == 0) {
		error = 0;
		goto out;
	}

	error = bpf_movein(uio, d, &m, sstosa(&dst));
	if (error)
		goto out;

	if (m->m_pkthdr.len > ifp->if_mtu) {
		m_freem(m);
		error = EMSGSIZE;
		goto out;
	}

	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;
	m->m_pkthdr.pf.prio = ifp->if_llprio;

	if (d->bd_hdrcmplt && dst.ss_family == AF_UNSPEC)
		dst.ss_family = pseudo_AF_HDRCMPLT;

	NET_LOCK();
	error = ifp->if_output(ifp, m, sstosa(&dst), NULL);
	NET_UNLOCK();

out:
	bpf_put(d);
	return (error);
}

/*
 * Reset a descriptor by flushing its packet buffer and clearing the
 * receive and drop counts.
 */
void
bpf_resetd(struct bpf_d *d)
{
	MUTEX_ASSERT_LOCKED(&d->bd_mtx);
	KASSERT(d->bd_in_uiomove == 0);

	if (timeout_del(&d->bd_wait_tmo))
		bpf_put(d);

	if (d->bd_hbuf != NULL) {
		/* Free the hold buffer. */
		d->bd_fbuf = d->bd_hbuf;
		d->bd_hbuf = NULL;
	}
	d->bd_state = BPF_S_IDLE;
	d->bd_slen = 0;
	d->bd_hlen = 0;
	d->bd_rcount = 0;
	d->bd_dcount = 0;
}

static int
bpf_set_wtout(struct bpf_d *d, uint64_t wtout)
{
	mtx_enter(&d->bd_mtx);
	d->bd_wtout = wtout;
	mtx_leave(&d->bd_mtx);

	return (0);
}

static int
bpf_set_wtimeout(struct bpf_d *d, const struct timeval *tv)
{
	uint64_t nsec;

	if (tv->tv_sec < 0 || !timerisvalid(tv))
		return (EINVAL);

	nsec = TIMEVAL_TO_NSEC(tv);
	if (nsec > SEC_TO_NSEC(300))
		return (EINVAL);
	if (nsec > MAXTSLP)
		return (EOVERFLOW);

	return (bpf_set_wtout(d, nsec));
}

static int
bpf_get_wtimeout(struct bpf_d *d, struct timeval *tv)
{
	uint64_t nsec;

	mtx_enter(&d->bd_mtx);
	nsec = d->bd_wtout;
	mtx_leave(&d->bd_mtx);

	if (nsec == INFSLP)
		return (ENXIO);

	memset(tv, 0, sizeof(*tv));
	NSEC_TO_TIMEVAL(nsec, tv);

	return (0);
}

/*
 *  FIONREAD		Check for read packet available.
 *  BIOCGBLEN		Get buffer len [for read()].
 *  BIOCSETF		Set read filter.
 *  BIOCSETFNR          Set read filter without resetting descriptor.
 *  BIOCFLUSH		Flush read packet buffer.
 *  BIOCPROMISC		Put interface into promiscuous mode.
 *  BIOCGDLTLIST	Get supported link layer types.
 *  BIOCGDLT		Get link layer type.
 *  BIOCSDLT		Set link layer type.
 *  BIOCGETIF		Get interface name.
 *  BIOCSETIF		Set interface.
 *  BIOCSRTIMEOUT	Set read timeout.
 *  BIOCGRTIMEOUT	Get read timeout.
 *  BIOCSWTIMEOUT	Set wait timeout.
 *  BIOCGWTIMEOUT	Get wait timeout.
 *  BIOCDWTIMEOUT	Del wait timeout.
 *  BIOCGSTATS		Get packet stats.
 *  BIOCIMMEDIATE	Set immediate mode.
 *  BIOCVERSION		Get filter language version.
 *  BIOCGHDRCMPLT	Get "header already complete" flag
 *  BIOCSHDRCMPLT	Set "header already complete" flag
 */
int
bpfioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	struct bpf_d *d;
	int error = 0;

	d = bpfilter_lookup(minor(dev));
	if (d->bd_locked && suser(p) != 0) {
		/* list of allowed ioctls when locked and not root */
		switch (cmd) {
		case BIOCGBLEN:
		case BIOCFLUSH:
		case BIOCGDLT:
		case BIOCGDLTLIST:
		case BIOCGETIF:
		case BIOCGRTIMEOUT:
		case BIOCGWTIMEOUT:
		case BIOCGSTATS:
		case BIOCVERSION:
		case BIOCGRSIG:
		case BIOCGHDRCMPLT:
		case FIONREAD:
		case BIOCLOCK:
		case BIOCSRTIMEOUT:
		case BIOCSWTIMEOUT:
		case BIOCDWTIMEOUT:
		case BIOCIMMEDIATE:
		case TIOCGPGRP:
		case BIOCGDIRFILT:
			break;
		default:
			return (EPERM);
		}
	}

	bpf_get(d);

	switch (cmd) {
	default:
		error = EINVAL;
		break;

	/*
	 * Check for read packet available.
	 */
	case FIONREAD:
		{
			int n;

			mtx_enter(&d->bd_mtx);
			n = d->bd_slen;
			if (d->bd_hbuf != NULL)
				n += d->bd_hlen;
			mtx_leave(&d->bd_mtx);

			*(int *)addr = n;
			break;
		}

	/*
	 * Get buffer len [for read()].
	 */
	case BIOCGBLEN:
		*(u_int *)addr = d->bd_bufsize;
		break;

	/*
	 * Set buffer length.
	 */
	case BIOCSBLEN:
		if (d->bd_bif != NULL)
			error = EINVAL;
		else {
			u_int size = *(u_int *)addr;
			int bpf_maxbufsize_local =
			    atomic_load_int(&bpf_maxbufsize);

			if (size > bpf_maxbufsize_local)
				*(u_int *)addr = size = bpf_maxbufsize_local;
			else if (size < BPF_MINBUFSIZE)
				*(u_int *)addr = size = BPF_MINBUFSIZE;
			mtx_enter(&d->bd_mtx);
			d->bd_bufsize = size;
			mtx_leave(&d->bd_mtx);
		}
		break;

	/*
	 * Set link layer read/write filter.
	 */
	case BIOCSETF:
	case BIOCSETFNR:
	case BIOCSETWF:
		error = bpf_setf(d, (struct bpf_program *)addr, cmd);
		break;

	/*
	 * Flush read packet buffer.
	 */
	case BIOCFLUSH:
		mtx_enter(&d->bd_mtx);
		bpf_resetd(d);
		mtx_leave(&d->bd_mtx);
		break;

	/*
	 * Put interface into promiscuous mode.
	 */
	case BIOCPROMISC:
		if (d->bd_bif == NULL) {
			/*
			 * No interface attached yet.
			 */
			error = EINVAL;
		} else if (d->bd_bif->bif_ifp != NULL) {
			if (d->bd_promisc == 0) {
				MUTEX_ASSERT_UNLOCKED(&d->bd_mtx);
				NET_LOCK();
				error = ifpromisc(d->bd_bif->bif_ifp, 1);
				NET_UNLOCK();
				if (error == 0)
					d->bd_promisc = 1;
			}
		}
		break;

	/*
	 * Get a list of supported device parameters.
	 */
	case BIOCGDLTLIST:
		if (d->bd_bif == NULL)
			error = EINVAL;
		else
			error = bpf_getdltlist(d, (struct bpf_dltlist *)addr);
		break;

	/*
	 * Get device parameters.
	 */
	case BIOCGDLT:
		if (d->bd_bif == NULL)
			error = EINVAL;
		else
			*(u_int *)addr = d->bd_bif->bif_dlt;
		break;

	/*
	 * Set device parameters.
	 */
	case BIOCSDLT:
		if (d->bd_bif == NULL)
			error = EINVAL;
		else {
			mtx_enter(&d->bd_mtx);
			error = bpf_setdlt(d, *(u_int *)addr);
			mtx_leave(&d->bd_mtx);
		}
		break;

	/*
	 * Set interface name.
	 */
	case BIOCGETIF:
		if (d->bd_bif == NULL)
			error = EINVAL;
		else
			bpf_ifname(d->bd_bif, (struct ifreq *)addr);
		break;

	/*
	 * Set interface.
	 */
	case BIOCSETIF:
		error = bpf_setif(d, (struct ifreq *)addr);
		break;

	/*
	 * Set read timeout.
	 */
	case BIOCSRTIMEOUT:
		{
			struct timeval *tv = (struct timeval *)addr;
			uint64_t rtout;

			if (tv->tv_sec < 0 || !timerisvalid(tv)) {
				error = EINVAL;
				break;
			}
			rtout = TIMEVAL_TO_NSEC(tv);
			if (rtout > MAXTSLP) {
				error = EOVERFLOW;
				break;
			}
			mtx_enter(&d->bd_mtx);
			d->bd_rtout = rtout;
			mtx_leave(&d->bd_mtx);
			break;
		}

	/*
	 * Get read timeout.
	 */
	case BIOCGRTIMEOUT:
		{
			struct timeval *tv = (struct timeval *)addr;

			memset(tv, 0, sizeof(*tv));
			mtx_enter(&d->bd_mtx);
			NSEC_TO_TIMEVAL(d->bd_rtout, tv);
			mtx_leave(&d->bd_mtx);
			break;
		}

	/*
	 * Get packet stats.
	 */
	case BIOCGSTATS:
		{
			struct bpf_stat *bs = (struct bpf_stat *)addr;

			bs->bs_recv = d->bd_rcount;
			bs->bs_drop = d->bd_dcount;
			break;
		}

	/*
	 * Set immediate mode.
	 */
	case BIOCIMMEDIATE:
		error = bpf_set_wtout(d, *(int *)addr ? 0 : INFSLP);
		break;

	/*
	 * Wait timeout.
	 */
	case BIOCSWTIMEOUT:
		error = bpf_set_wtimeout(d, (const struct timeval *)addr);
		break;
	case BIOCGWTIMEOUT:
		error = bpf_get_wtimeout(d, (struct timeval *)addr);
		break;
	case BIOCDWTIMEOUT:
		error = bpf_set_wtout(d, INFSLP);
		break;

	case BIOCVERSION:
		{
			struct bpf_version *bv = (struct bpf_version *)addr;

			bv->bv_major = BPF_MAJOR_VERSION;
			bv->bv_minor = BPF_MINOR_VERSION;
			break;
		}

	case BIOCGHDRCMPLT:	/* get "header already complete" flag */
		*(u_int *)addr = d->bd_hdrcmplt;
		break;

	case BIOCSHDRCMPLT:	/* set "header already complete" flag */
		d->bd_hdrcmplt = *(u_int *)addr ? 1 : 0;
		break;

	case BIOCLOCK:		/* set "locked" flag (no reset) */
		d->bd_locked = 1;
		break;

	case BIOCGFILDROP:	/* get "filter-drop" flag */
		*(u_int *)addr = d->bd_fildrop;
		break;

	case BIOCSFILDROP: {	/* set "filter-drop" flag */
		unsigned int fildrop = *(u_int *)addr;
		switch (fildrop) {
		case BPF_FILDROP_PASS:
		case BPF_FILDROP_CAPTURE:
		case BPF_FILDROP_DROP:
			d->bd_fildrop = fildrop;
			break;
		default:
			error = EINVAL;
			break;
		}
		break;
	}

	case BIOCGDIRFILT:	/* get direction filter */
		*(u_int *)addr = d->bd_dirfilt;
		break;

	case BIOCSDIRFILT:	/* set direction filter */
		d->bd_dirfilt = (*(u_int *)addr) &
		    (BPF_DIRECTION_IN|BPF_DIRECTION_OUT);
		break;

	case FIOASYNC:		/* Send signal on receive packets */
		d->bd_async = *(int *)addr;
		break;

	case FIOSETOWN:		/* Process or group to send signals to */
	case TIOCSPGRP:
		error = sigio_setown(&d->bd_sigio, cmd, addr);
		break;

	case FIOGETOWN:
	case TIOCGPGRP:
		sigio_getown(&d->bd_sigio, cmd, addr);
		break;

	case BIOCSRSIG:		/* Set receive signal */
		{
			u_int sig;

			sig = *(u_int *)addr;

			if (sig >= NSIG)
				error = EINVAL;
			else
				d->bd_sig = sig;
			break;
		}
	case BIOCGRSIG:
		*(u_int *)addr = d->bd_sig;
		break;
	}

	bpf_put(d);
	return (error);
}

/*
 * Set d's packet filter program to fp.  If this file already has a filter,
 * free it and replace it.  Returns EINVAL for bogus requests.
 */
int
bpf_setf(struct bpf_d *d, struct bpf_program *fp, u_long cmd)
{
	struct bpf_program_smr *bps, *old_bps;
	struct bpf_insn *fcode;
	u_int flen, size;

	KERNEL_ASSERT_LOCKED();

	if (fp->bf_insns == 0) {
		if (fp->bf_len != 0)
			return (EINVAL);
		bps = NULL;
	} else {
		flen = fp->bf_len;
		if (flen > BPF_MAXINSNS)
			return (EINVAL);

		fcode = mallocarray(flen, sizeof(*fp->bf_insns), M_DEVBUF,
		    M_WAITOK | M_CANFAIL);
		if (fcode == NULL)
			return (ENOMEM);

		size = flen * sizeof(*fp->bf_insns);
		if (copyin(fp->bf_insns, fcode, size) != 0 ||
		    bpf_validate(fcode, (int)flen) == 0) {
			free(fcode, M_DEVBUF, size);
			return (EINVAL);
		}

		bps = malloc(sizeof(*bps), M_DEVBUF, M_WAITOK);
		smr_init(&bps->bps_smr);
		bps->bps_bf.bf_len = flen;
		bps->bps_bf.bf_insns = fcode;
	}

	if (cmd != BIOCSETWF) {
		old_bps = SMR_PTR_GET_LOCKED(&d->bd_rfilter);
		SMR_PTR_SET_LOCKED(&d->bd_rfilter, bps);
	} else {
		old_bps = SMR_PTR_GET_LOCKED(&d->bd_wfilter);
		SMR_PTR_SET_LOCKED(&d->bd_wfilter, bps);
	}

	if (cmd == BIOCSETF) {
		mtx_enter(&d->bd_mtx);
		bpf_resetd(d);
		mtx_leave(&d->bd_mtx);
	}

	if (old_bps != NULL)
		smr_call(&old_bps->bps_smr, bpf_prog_smr, old_bps);

	return (0);
}

/*
 * Detach a file from its current interface (if attached at all) and attach
 * to the interface indicated by the name stored in ifr.
 * Return an errno or 0.
 */
int
bpf_setif(struct bpf_d *d, struct ifreq *ifr)
{
	struct bpf_if *bp;
	int error = 0;

	/*
	 * Look through attached interfaces for the named one.
	 */
	TAILQ_FOREACH(bp, &bpf_iflist, bif_next) {
		if (strcmp(bp->bif_name, ifr->ifr_name) == 0)
			break;
	}

	/* Not found. */
	if (bp == NULL)
		return (ENXIO);

	/*
	 * Allocate the packet buffers if we need to.
	 * If we're already attached to requested interface,
	 * just flush the buffer.
	 */
	mtx_enter(&d->bd_mtx);
	if (d->bd_sbuf == NULL) {
		if ((error = bpf_allocbufs(d)))
			goto out;
	}
	if (bp != d->bd_bif) {
		/*
		 * Detach if attached to something else.
		 */
		bpf_detachd(d);
		bpf_attachd(d, bp);
	}
	bpf_resetd(d);
out:
	mtx_leave(&d->bd_mtx);
	return (error);
}

/*
 * Copy the interface name to the ifreq.
 */
void
bpf_ifname(struct bpf_if *bif, struct ifreq *ifr)
{
	bcopy(bif->bif_name, ifr->ifr_name, sizeof(ifr->ifr_name));
}

const struct filterops bpfread_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_bpfrdetach,
	.f_event	= filt_bpfread,
	.f_modify	= filt_bpfreadmodify,
	.f_process	= filt_bpfreadprocess,
};

int
bpfkqfilter(dev_t dev, struct knote *kn)
{
	struct bpf_d *d;
	struct klist *klist;

	KERNEL_ASSERT_LOCKED();

	d = bpfilter_lookup(minor(dev));
	if (d == NULL)
		return (ENXIO);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		klist = &d->bd_klist;
		kn->kn_fop = &bpfread_filtops;
		break;
	default:
		return (EINVAL);
	}

	bpf_get(d);
	kn->kn_hook = d;
	klist_insert(klist, kn);

	return (0);
}

void
filt_bpfrdetach(struct knote *kn)
{
	struct bpf_d *d = kn->kn_hook;

	klist_remove(&d->bd_klist, kn);
	bpf_put(d);
}

int
filt_bpfread(struct knote *kn, long hint)
{
	struct bpf_d *d = kn->kn_hook;

	MUTEX_ASSERT_LOCKED(&d->bd_mtx);

	kn->kn_data = d->bd_hlen;
	if (d->bd_state == BPF_S_DONE)
		kn->kn_data += d->bd_slen;

	return (kn->kn_data > 0);
}

int
filt_bpfreadmodify(struct kevent *kev, struct knote *kn)
{
	struct bpf_d *d = kn->kn_hook;
	int active;

	mtx_enter(&d->bd_mtx);
	active = knote_modify_fn(kev, kn, filt_bpfread);
	mtx_leave(&d->bd_mtx);

	return (active);
}

int
filt_bpfreadprocess(struct knote *kn, struct kevent *kev)
{
	struct bpf_d *d = kn->kn_hook;
	int active;

	mtx_enter(&d->bd_mtx);
	active = knote_process_fn(kn, kev, filt_bpfread);
	mtx_leave(&d->bd_mtx);

	return (active);
}

/*
 * Copy data from an mbuf chain into a buffer.  This code is derived
 * from m_copydata in sys/uipc_mbuf.c.
 */
void
bpf_mcopy(const void *src_arg, void *dst_arg, size_t len)
{
	const struct mbuf *m;
	u_int count;
	u_char *dst;

	m = src_arg;
	dst = dst_arg;
	while (len > 0) {
		if (m == NULL)
			panic("bpf_mcopy");
		count = min(m->m_len, len);
		bcopy(mtod(m, caddr_t), (caddr_t)dst, count);
		m = m->m_next;
		dst += count;
		len -= count;
	}
}

int
bpf_mtap(caddr_t arg, const struct mbuf *m, u_int direction)
{
	return _bpf_mtap(arg, m, m, direction);
}

int
_bpf_mtap(caddr_t arg, const struct mbuf *mp, const struct mbuf *m,
    u_int direction)
{
	struct bpf_if *bp = (struct bpf_if *)arg;
	struct bpf_d *d;
	size_t pktlen, slen;
	const struct mbuf *m0;
	struct bpf_hdr tbh;
	int gothdr = 0;
	int drop = 0;

	if (m == NULL)
		return (0);

	if (bp == NULL)
		return (0);

	pktlen = 0;
	for (m0 = m; m0 != NULL; m0 = m0->m_next)
		pktlen += m0->m_len;

	smr_read_enter();
	SMR_SLIST_FOREACH(d, &bp->bif_dlist, bd_next) {
		struct bpf_program_smr *bps;
		struct bpf_insn *fcode = NULL;

		atomic_inc_long(&d->bd_rcount);

		if (ISSET(d->bd_dirfilt, direction))
			continue;

		bps = SMR_PTR_GET(&d->bd_rfilter);
		if (bps != NULL)
			fcode = bps->bps_bf.bf_insns;
		slen = bpf_mfilter(fcode, m, pktlen);

		if (slen == 0)
			continue;
		if (d->bd_fildrop != BPF_FILDROP_PASS)
			drop = 1;
		if (d->bd_fildrop != BPF_FILDROP_DROP) {
			if (!gothdr) {
				struct timeval tv;
				memset(&tbh, 0, sizeof(tbh));

				if (ISSET(mp->m_flags, M_PKTHDR)) {
					tbh.bh_ifidx = mp->m_pkthdr.ph_ifidx;
					tbh.bh_flowid = mp->m_pkthdr.ph_flowid;
					tbh.bh_flags = mp->m_pkthdr.pf.prio;
					if (ISSET(mp->m_pkthdr.csum_flags,
					    M_FLOWID))
						SET(tbh.bh_flags, BPF_F_FLOWID);
					tbh.bh_csumflags =
					    mp->m_pkthdr.csum_flags;

					m_microtime(mp, &tv);
				} else
					microtime(&tv);

				tbh.bh_tstamp.tv_sec = tv.tv_sec;
				tbh.bh_tstamp.tv_usec = tv.tv_usec;
				SET(tbh.bh_flags, direction << BPF_F_DIR_SHIFT);

				gothdr = 1;
			}

			mtx_enter(&d->bd_mtx);
			bpf_catchpacket(d, (u_char *)m, pktlen, slen, &tbh);
			mtx_leave(&d->bd_mtx);
		}
	}
	smr_read_leave();

	return (drop);
}

/*
 * Incoming linkage from device drivers, where a data buffer should be
 * prepended by an arbitrary header. In this situation we already have a
 * way of representing a chain of memory buffers, ie, mbufs, so reuse
 * the existing functionality by attaching the buffers to mbufs.
 *
 * Con up a minimal mbuf chain to pacify bpf by allocating (only) a
 * struct m_hdr each for the header and data on the stack.
 */
int
bpf_tap_hdr(caddr_t arg, const void *hdr, unsigned int hdrlen,
    const void *buf, unsigned int buflen, u_int direction)
{
	struct m_hdr mh, md;
	struct mbuf *m0 = NULL;
	struct mbuf **mp = &m0;

	if (hdr != NULL) {
		mh.mh_flags = 0;
		mh.mh_next = NULL;
		mh.mh_len = hdrlen;
		mh.mh_data = (void *)hdr;

		*mp = (struct mbuf *)&mh;
		mp = &mh.mh_next;
	}

	if (buf != NULL) {
		md.mh_flags = 0;
		md.mh_next = NULL;
		md.mh_len = buflen;
		md.mh_data = (void *)buf;

		*mp = (struct mbuf *)&md;
	}

	return bpf_mtap(arg, m0, direction);
}

/*
 * Incoming linkage from device drivers, where we have a mbuf chain
 * but need to prepend some arbitrary header from a linear buffer.
 *
 * Con up a minimal dummy header to pacify bpf.  Allocate (only) a
 * struct m_hdr on the stack.  This is safe as bpf only reads from the
 * fields in this header that we initialize, and will not try to free
 * it or keep a pointer to it.
 */
int
bpf_mtap_hdr(caddr_t arg, const void *data, u_int dlen, const struct mbuf *m,
    u_int direction)
{
	struct m_hdr mh;
	const struct mbuf *m0;

	if (dlen > 0) {
		mh.mh_flags = 0;
		mh.mh_next = (struct mbuf *)m;
		mh.mh_len = dlen;
		mh.mh_data = (void *)data;
		m0 = (struct mbuf *)&mh;
	} else
		m0 = m;

	return _bpf_mtap(arg, m, m0, direction);
}

/*
 * Incoming linkage from device drivers, where we have a mbuf chain
 * but need to prepend the address family.
 *
 * Con up a minimal dummy header to pacify bpf.  We allocate (only) a
 * struct m_hdr on the stack.  This is safe as bpf only reads from the
 * fields in this header that we initialize, and will not try to free
 * it or keep a pointer to it.
 */
int
bpf_mtap_af(caddr_t arg, u_int32_t af, const struct mbuf *m, u_int direction)
{
	u_int32_t    afh;

	afh = htonl(af);

	return bpf_mtap_hdr(arg, &afh, sizeof(afh), m, direction);
}

/*
 * Incoming linkage from device drivers, where we have a mbuf chain
 * but need to prepend a VLAN encapsulation header.
 *
 * Con up a minimal dummy header to pacify bpf.  Allocate (only) a
 * struct m_hdr on the stack.  This is safe as bpf only reads from the
 * fields in this header that we initialize, and will not try to free
 * it or keep a pointer to it.
 */
int
bpf_mtap_ether(caddr_t arg, const struct mbuf *m, u_int direction)
{
#if NVLAN > 0
	struct ether_vlan_header evh;
	struct m_hdr mh, md;

	if ((m->m_flags & M_VLANTAG) == 0)
#endif
	{
		return _bpf_mtap(arg, m, m, direction);
	}

#if NVLAN > 0
	KASSERT(m->m_len >= ETHER_HDR_LEN);

	memcpy(&evh, mtod(m, char *), ETHER_HDR_LEN);
	evh.evl_proto = evh.evl_encap_proto;
	evh.evl_encap_proto = htons(ETHERTYPE_VLAN);
	evh.evl_tag = htons(m->m_pkthdr.ether_vtag);

	mh.mh_flags = 0;
	mh.mh_data = (caddr_t)&evh;
	mh.mh_len = sizeof(evh);
	mh.mh_next = (struct mbuf *)&md;

	md.mh_flags = 0;
	md.mh_data = m->m_data + ETHER_HDR_LEN;
	md.mh_len = m->m_len - ETHER_HDR_LEN;
	md.mh_next = m->m_next;

	return _bpf_mtap(arg, m, (struct mbuf *)&mh, direction);
#endif
}

/*
 * Move the packet data from interface memory (pkt) into the
 * store buffer.  Wake up listeners if needed.
 * "copy" is the routine called to do the actual data
 * transfer.  bcopy is passed in to copy contiguous chunks, while
 * bpf_mcopy is passed in to copy mbuf chains.  In the latter case,
 * pkt is really an mbuf.
 */
void
bpf_catchpacket(struct bpf_d *d, u_char *pkt, size_t pktlen, size_t snaplen,
    const struct bpf_hdr *tbh)
{
	struct bpf_hdr *bh;
	int totlen, curlen;
	int hdrlen, do_wakeup = 0;

	MUTEX_ASSERT_LOCKED(&d->bd_mtx);
	if (d->bd_bif == NULL)
		return;

	hdrlen = d->bd_bif->bif_hdrlen;

	/*
	 * Figure out how many bytes to move.  If the packet is
	 * greater or equal to the snapshot length, transfer that
	 * much.  Otherwise, transfer the whole packet (unless
	 * we hit the buffer size limit).
	 */
	totlen = hdrlen + min(snaplen, pktlen);
	if (totlen > d->bd_bufsize)
		totlen = d->bd_bufsize;

	/*
	 * Round up the end of the previous packet to the next longword.
	 */
	curlen = BPF_WORDALIGN(d->bd_slen);
	if (curlen + totlen > d->bd_bufsize) {
		/*
		 * This packet will overflow the storage buffer.
		 * Rotate the buffers if we can, then wakeup any
		 * pending reads.
		 */
		if (d->bd_fbuf == NULL) {
			/*
			 * We haven't completed the previous read yet,
			 * so drop the packet.
			 */
			++d->bd_dcount;
			return;
		}

		/* cancel pending wtime */
		if (timeout_del(&d->bd_wait_tmo))
			bpf_put(d);

		ROTATE_BUFFERS(d);
		do_wakeup = 1;
		curlen = 0;
	}

	/*
	 * Append the bpf header.
	 */
	bh = (struct bpf_hdr *)(d->bd_sbuf + curlen);
	*bh = *tbh;
	bh->bh_datalen = pktlen;
	bh->bh_hdrlen = hdrlen;
	bh->bh_caplen = totlen - hdrlen;

	/*
	 * Copy the packet data into the store buffer and update its length.
	 */
	bpf_mcopy(pkt, (u_char *)bh + hdrlen, bh->bh_caplen);
	d->bd_slen = curlen + totlen;

	switch (d->bd_wtout) {
	case 0:
		/*
		 * Immediate mode is set.  A packet arrived so any
		 * reads should be woken up.
		 */
		if (d->bd_state == BPF_S_IDLE)
			d->bd_state = BPF_S_DONE;
		do_wakeup = 1;
		break;
	case INFSLP:
		break;
	default:
		if (d->bd_state == BPF_S_IDLE) {
			d->bd_state = BPF_S_WAIT;

			bpf_get(d);
			if (!timeout_add_nsec(&d->bd_wait_tmo, d->bd_wtout))
				bpf_put(d);
		}
		break;
	}

	if (do_wakeup)
		bpf_wakeup(d);
}

/*
 * Initialize all nonzero fields of a descriptor.
 */
int
bpf_allocbufs(struct bpf_d *d)
{
	MUTEX_ASSERT_LOCKED(&d->bd_mtx);

	d->bd_fbuf = malloc(d->bd_bufsize, M_DEVBUF, M_NOWAIT);
	if (d->bd_fbuf == NULL)
		return (ENOMEM);

	d->bd_sbuf = malloc(d->bd_bufsize, M_DEVBUF, M_NOWAIT);
	if (d->bd_sbuf == NULL) {
		free(d->bd_fbuf, M_DEVBUF, d->bd_bufsize);
		d->bd_fbuf = NULL;
		return (ENOMEM);
	}

	d->bd_slen = 0;
	d->bd_hlen = 0;

	return (0);
}

void
bpf_prog_smr(void *bps_arg)
{
	struct bpf_program_smr *bps = bps_arg;

	free(bps->bps_bf.bf_insns, M_DEVBUF,
	    bps->bps_bf.bf_len * sizeof(struct bpf_insn));
	free(bps, M_DEVBUF, sizeof(struct bpf_program_smr));
}

void
bpf_d_smr(void *smr)
{
	struct bpf_d	*bd = smr;

	sigio_free(&bd->bd_sigio);
	free(bd->bd_sbuf, M_DEVBUF, bd->bd_bufsize);
	free(bd->bd_hbuf, M_DEVBUF, bd->bd_bufsize);
	free(bd->bd_fbuf, M_DEVBUF, bd->bd_bufsize);

	if (bd->bd_rfilter != NULL)
		bpf_prog_smr(bd->bd_rfilter);
	if (bd->bd_wfilter != NULL)
		bpf_prog_smr(bd->bd_wfilter);

	klist_free(&bd->bd_klist);
	free(bd, M_DEVBUF, sizeof(*bd));
}

void
bpf_get(struct bpf_d *bd)
{
	refcnt_take(&bd->bd_refcnt);
}

/*
 * Free buffers currently in use by a descriptor
 * when the reference count drops to zero.
 */
void
bpf_put(struct bpf_d *bd)
{
	if (refcnt_rele(&bd->bd_refcnt) == 0)
		return;

	smr_call(&bd->bd_smr, bpf_d_smr, bd);
}

void *
bpfsattach(caddr_t *bpfp, const char *name, u_int dlt, u_int hdrlen)
{
	struct bpf_if *bp;

	if ((bp = malloc(sizeof(*bp), M_DEVBUF, M_NOWAIT)) == NULL)
		panic("bpfattach");
	SMR_SLIST_INIT(&bp->bif_dlist);
	bp->bif_driverp = (struct bpf_if **)bpfp;
	bp->bif_name = name;
	bp->bif_ifp = NULL;
	bp->bif_dlt = dlt;

	TAILQ_INSERT_TAIL(&bpf_iflist, bp, bif_next);

	*bp->bif_driverp = NULL;

	/*
	 * Compute the length of the bpf header.  This is not necessarily
	 * equal to SIZEOF_BPF_HDR because we want to insert spacing such
	 * that the network layer header begins on a longword boundary (for
	 * performance reasons and to alleviate alignment restrictions).
	 */
	bp->bif_hdrlen = BPF_WORDALIGN(hdrlen + SIZEOF_BPF_HDR) - hdrlen;

	return (bp);
}

void *
bpfxattach(caddr_t *driverp, const char *name, struct ifnet *ifp,
    u_int dlt, u_int hdrlen)
{
	struct bpf_if *bp;

	bp = bpfsattach(driverp, name, dlt, hdrlen);
	bp->bif_ifp = ifp;

	return (bp);
}

void
bpfattach(caddr_t *driverp, struct ifnet *ifp, u_int dlt, u_int hdrlen)
{
	bpfxattach(driverp, ifp->if_xname, ifp, dlt, hdrlen);
}

/* Detach an interface from its attached bpf device.  */
void
bpfdetach(struct ifnet *ifp)
{
	struct bpf_if *bp, *nbp;

	KERNEL_ASSERT_LOCKED();

	TAILQ_FOREACH_SAFE(bp, &bpf_iflist, bif_next, nbp) {
		if (bp->bif_ifp == ifp)
			bpfsdetach(bp);
	}
	ifp->if_bpf = NULL;
}

void
bpfsdetach(void *p)
{
	struct bpf_if *bp = p;
	struct bpf_d *bd;
	int maj;

	KERNEL_ASSERT_LOCKED();

	/* Locate the major number. */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == bpfopen)
			break;

	while ((bd = SMR_SLIST_FIRST_LOCKED(&bp->bif_dlist))) {
		bpf_get(bd);
		vdevgone(maj, bd->bd_unit, bd->bd_unit, VCHR);
		klist_invalidate(&bd->bd_klist);
		bpf_put(bd);
	}

	TAILQ_REMOVE(&bpf_iflist, bp, bif_next);

	free(bp, M_DEVBUF, sizeof(*bp));
}

#ifndef SMALL_KERNEL
int
bpf_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case NET_BPF_BUFSIZE:
		return sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &bpf_bufsize, BPF_MINBUFSIZE,
		    atomic_load_int(&bpf_maxbufsize));
	case NET_BPF_MAXBUFSIZE:
		return sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &bpf_maxbufsize, BPF_MINBUFSIZE, MALLOC_MAX);
	default:
		return (EOPNOTSUPP);
	}

	/* NOTREACHED */
}
#endif /* SMALL_KERNEL */

struct bpf_d *
bpfilter_lookup(int unit)
{
	struct bpf_d *bd;

	KERNEL_ASSERT_LOCKED();

	LIST_FOREACH(bd, &bpf_d_list, bd_list)
		if (bd->bd_unit == unit)
			return (bd);
	return (NULL);
}

/*
 * Get a list of available data link type of the interface.
 */
int
bpf_getdltlist(struct bpf_d *d, struct bpf_dltlist *bfl)
{
	int n, error;
	struct bpf_if *bp;
	const char *name;

	name = d->bd_bif->bif_name;
	n = 0;
	error = 0;
	TAILQ_FOREACH(bp, &bpf_iflist, bif_next) {
		if (strcmp(name, bp->bif_name) != 0)
			continue;
		if (bfl->bfl_list != NULL) {
			if (n >= bfl->bfl_len)
				return (ENOMEM);
			error = copyout(&bp->bif_dlt,
			    bfl->bfl_list + n, sizeof(u_int));
			if (error)
				break;
		}
		n++;
	}

	bfl->bfl_len = n;
	return (error);
}

/*
 * Set the data link type of a BPF instance.
 */
int
bpf_setdlt(struct bpf_d *d, u_int dlt)
{
	const char *name;
	struct bpf_if *bp;

	MUTEX_ASSERT_LOCKED(&d->bd_mtx);
	if (d->bd_bif->bif_dlt == dlt)
		return (0);
	name = d->bd_bif->bif_name;
	TAILQ_FOREACH(bp, &bpf_iflist, bif_next) {
		if (strcmp(name, bp->bif_name) != 0)
			continue;
		if (bp->bif_dlt == dlt)
			break;
	}
	if (bp == NULL)
		return (EINVAL);
	bpf_detachd(d);
	bpf_attachd(d, bp);
	bpf_resetd(d);
	return (0);
}

u_int32_t	bpf_mbuf_ldw(const void *, u_int32_t, int *);
u_int32_t	bpf_mbuf_ldh(const void *, u_int32_t, int *);
u_int32_t	bpf_mbuf_ldb(const void *, u_int32_t, int *);

int		bpf_mbuf_copy(const struct mbuf *, u_int32_t,
		    void *, u_int32_t);

const struct bpf_ops bpf_mbuf_ops = {
	bpf_mbuf_ldw,
	bpf_mbuf_ldh,
	bpf_mbuf_ldb,
};

int
bpf_mbuf_copy(const struct mbuf *m, u_int32_t off, void *buf, u_int32_t len)
{
	u_int8_t *cp = buf;
	u_int32_t count;

	while (off >= m->m_len) {
		off -= m->m_len;

		m = m->m_next;
		if (m == NULL)
			return (-1);
	}

	for (;;) {
		count = min(m->m_len - off, len);

		memcpy(cp, m->m_data + off, count);
		len -= count;

		if (len == 0)
			return (0);

		m = m->m_next;
		if (m == NULL)
			break;

		cp += count;
		off = 0;
	}

	return (-1);
}

u_int32_t
bpf_mbuf_ldw(const void *m0, u_int32_t k, int *err)
{
	u_int32_t v;

	if (bpf_mbuf_copy(m0, k, &v, sizeof(v)) != 0) {
		*err = 1;
		return (0);
	}

	*err = 0;
	return ntohl(v);
}

u_int32_t
bpf_mbuf_ldh(const void *m0, u_int32_t k, int *err)
{
	u_int16_t v;

	if (bpf_mbuf_copy(m0, k, &v, sizeof(v)) != 0) {
		*err = 1;
		return (0);
	}

	*err = 0;
	return ntohs(v);
}

u_int32_t
bpf_mbuf_ldb(const void *m0, u_int32_t k, int *err)
{
	const struct mbuf *m = m0;
	u_int8_t v;

	while (k >= m->m_len) {
		k -= m->m_len;

		m = m->m_next;
		if (m == NULL) {
			*err = 1;
			return (0);
		}
	}
	v = m->m_data[k];

	*err = 0;
	return v;
}

u_int
bpf_mfilter(const struct bpf_insn *pc, const struct mbuf *m, u_int wirelen)
{
	return _bpf_filter(pc, &bpf_mbuf_ops, m, wirelen);
}
