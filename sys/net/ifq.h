/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	From: @(#)if.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef	_NET_IFQ_H_
#define	_NET_IFQ_H_

#ifdef _KERNEL
#include <sys/mbuf.h>		/* ifqueue only? */
#include <sys/buf_ring.h>
#include <net/vnet.h>
#endif /* _KERNEL */
#include <sys/lock.h>		/* XXX */
#include <sys/mutex.h>		/* struct ifqueue */

/*
 * Couple of ugly extra definitions that are required since ifq.h
 * is splitted from if_var.h.
 */
#define	IF_DUNIT_NONE	-1

#include <net/altq/if_altq.h>

/*
 * Structure defining a queue for a network interface.
 */
struct	ifqueue {
	struct	mbuf *ifq_head;
	struct	mbuf *ifq_tail;
	int	ifq_len;
	int	ifq_maxlen;
	struct	mtx ifq_mtx;
};

#ifdef _KERNEL
/*
 * Output queues (ifp->if_snd) and slow device input queues (*ifp->if_slowq)
 * are queues of messages stored on ifqueue structures
 * (defined above).  Entries are added to and deleted from these structures
 * by these macros.
 */
#define IF_LOCK(ifq)		mtx_lock(&(ifq)->ifq_mtx)
#define IF_UNLOCK(ifq)		mtx_unlock(&(ifq)->ifq_mtx)
#define	IF_LOCK_ASSERT(ifq)	mtx_assert(&(ifq)->ifq_mtx, MA_OWNED)
#define	_IF_QFULL(ifq)		((ifq)->ifq_len >= (ifq)->ifq_maxlen)
#define	_IF_QLEN(ifq)		((ifq)->ifq_len)

#define	_IF_ENQUEUE(ifq, m) do { 				\
	(m)->m_nextpkt = NULL;					\
	if ((ifq)->ifq_tail == NULL) 				\
		(ifq)->ifq_head = m; 				\
	else 							\
		(ifq)->ifq_tail->m_nextpkt = m; 		\
	(ifq)->ifq_tail = m; 					\
	(ifq)->ifq_len++; 					\
} while (0)

#define IF_ENQUEUE(ifq, m) do {					\
	IF_LOCK(ifq); 						\
	_IF_ENQUEUE(ifq, m); 					\
	IF_UNLOCK(ifq); 					\
} while (0)

#define	_IF_PREPEND(ifq, m) do {				\
	(m)->m_nextpkt = (ifq)->ifq_head; 			\
	if ((ifq)->ifq_tail == NULL) 				\
		(ifq)->ifq_tail = (m); 				\
	(ifq)->ifq_head = (m); 					\
	(ifq)->ifq_len++; 					\
} while (0)

#define IF_PREPEND(ifq, m) do {		 			\
	IF_LOCK(ifq); 						\
	_IF_PREPEND(ifq, m); 					\
	IF_UNLOCK(ifq); 					\
} while (0)

#define	_IF_DEQUEUE(ifq, m) do { 				\
	(m) = (ifq)->ifq_head; 					\
	if (m) { 						\
		if (((ifq)->ifq_head = (m)->m_nextpkt) == NULL)	\
			(ifq)->ifq_tail = NULL; 		\
		(m)->m_nextpkt = NULL; 				\
		(ifq)->ifq_len--; 				\
	} 							\
} while (0)

#define IF_DEQUEUE(ifq, m) do { 				\
	IF_LOCK(ifq); 						\
	_IF_DEQUEUE(ifq, m); 					\
	IF_UNLOCK(ifq); 					\
} while (0)

#define	_IF_DEQUEUE_ALL(ifq, m) do {				\
	(m) = (ifq)->ifq_head;					\
	(ifq)->ifq_head = (ifq)->ifq_tail = NULL;		\
	(ifq)->ifq_len = 0;					\
} while (0)

#define	IF_DEQUEUE_ALL(ifq, m) do {				\
	IF_LOCK(ifq); 						\
	_IF_DEQUEUE_ALL(ifq, m);				\
	IF_UNLOCK(ifq); 					\
} while (0)

#define	_IF_POLL(ifq, m)	((m) = (ifq)->ifq_head)
#define	IF_POLL(ifq, m)		_IF_POLL(ifq, m)

#define _IF_DRAIN(ifq) do { 					\
	struct mbuf *m; 					\
	for (;;) { 						\
		_IF_DEQUEUE(ifq, m); 				\
		if (m == NULL) 					\
			break; 					\
		m_freem(m); 					\
	} 							\
} while (0)

#define IF_DRAIN(ifq) do {					\
	IF_LOCK(ifq);						\
	_IF_DRAIN(ifq);						\
	IF_UNLOCK(ifq);						\
} while(0)

int	if_handoff(struct ifqueue *ifq, struct mbuf *m, struct ifnet *ifp,
	    int adjust);
#define	IF_HANDOFF(ifq, m, ifp)			\
	if_handoff((struct ifqueue *)ifq, m, ifp, 0)
#define	IF_HANDOFF_ADJ(ifq, m, ifp, adj)	\
	if_handoff((struct ifqueue *)ifq, m, ifp, adj)

void	if_start(struct ifnet *);

#define	IFQ_ENQUEUE(ifq, m, err)					\
do {									\
	IF_LOCK(ifq);							\
	if (ALTQ_IS_ENABLED(ifq))					\
		ALTQ_ENQUEUE(ifq, m, NULL, err);			\
	else {								\
		if (_IF_QFULL(ifq)) {					\
			m_freem(m);					\
			(err) = ENOBUFS;				\
		} else {						\
			_IF_ENQUEUE(ifq, m);				\
			(err) = 0;					\
		}							\
	}								\
	IF_UNLOCK(ifq);							\
} while (0)

#define	IFQ_DEQUEUE_NOLOCK(ifq, m)					\
do {									\
	if (TBR_IS_ENABLED(ifq))					\
		(m) = tbr_dequeue_ptr(ifq, ALTDQ_REMOVE);		\
	else if (ALTQ_IS_ENABLED(ifq))					\
		ALTQ_DEQUEUE(ifq, m);					\
	else								\
		_IF_DEQUEUE(ifq, m);					\
} while (0)

#define	IFQ_DEQUEUE(ifq, m)						\
do {									\
	IF_LOCK(ifq);							\
	IFQ_DEQUEUE_NOLOCK(ifq, m);					\
	IF_UNLOCK(ifq);							\
} while (0)

#define	IFQ_POLL_NOLOCK(ifq, m)						\
do {									\
	if (TBR_IS_ENABLED(ifq))					\
		(m) = tbr_dequeue_ptr(ifq, ALTDQ_POLL);			\
	else if (ALTQ_IS_ENABLED(ifq))					\
		ALTQ_POLL(ifq, m);					\
	else								\
		_IF_POLL(ifq, m);					\
} while (0)

#define	IFQ_POLL(ifq, m)						\
do {									\
	IF_LOCK(ifq);							\
	IFQ_POLL_NOLOCK(ifq, m);					\
	IF_UNLOCK(ifq);							\
} while (0)

#define	IFQ_PURGE_NOLOCK(ifq)						\
do {									\
	if (ALTQ_IS_ENABLED(ifq)) {					\
		ALTQ_PURGE(ifq);					\
	} else								\
		_IF_DRAIN(ifq);						\
} while (0)

#define	IFQ_PURGE(ifq)							\
do {									\
	IF_LOCK(ifq);							\
	IFQ_PURGE_NOLOCK(ifq);						\
	IF_UNLOCK(ifq);							\
} while (0)

#define	IFQ_SET_READY(ifq)						\
	do { ((ifq)->altq_flags |= ALTQF_READY); } while (0)

#define	IFQ_LOCK(ifq)			IF_LOCK(ifq)
#define	IFQ_UNLOCK(ifq)			IF_UNLOCK(ifq)
#define	IFQ_LOCK_ASSERT(ifq)		IF_LOCK_ASSERT(ifq)
#define	IFQ_IS_EMPTY(ifq)		((ifq)->ifq_len == 0)
#define	IFQ_INC_LEN(ifq)		((ifq)->ifq_len++)
#define	IFQ_DEC_LEN(ifq)		(--(ifq)->ifq_len)
#define	IFQ_SET_MAXLEN(ifq, len)	((ifq)->ifq_maxlen = (len))

/*
 * The IFF_DRV_OACTIVE test should really occur in the device driver, not in
 * the handoff logic, as that flag is locked by the device driver.
 */
#define	IFQ_HANDOFF_ADJ(ifp, m, adj, err)				\
do {									\
	int len;							\
	short mflags;							\
									\
	len = (m)->m_pkthdr.len;					\
	mflags = (m)->m_flags;						\
	IFQ_ENQUEUE(&(ifp)->if_snd, m, err);				\
	if ((err) == 0) {						\
		if_inc_counter((ifp), IFCOUNTER_OBYTES, len + (adj));	\
		if (mflags & M_MCAST)					\
			if_inc_counter((ifp), IFCOUNTER_OMCASTS, 1);	\
		if (((ifp)->if_drv_flags & IFF_DRV_OACTIVE) == 0)	\
			if_start(ifp);					\
	} else								\
		if_inc_counter((ifp), IFCOUNTER_OQDROPS, 1);		\
} while (0)

#define	IFQ_HANDOFF(ifp, m, err)					\
	IFQ_HANDOFF_ADJ(ifp, m, 0, err)

#define	IFQ_DRV_DEQUEUE(ifq, m)						\
do {									\
	(m) = (ifq)->ifq_drv_head;					\
	if (m) {							\
		if (((ifq)->ifq_drv_head = (m)->m_nextpkt) == NULL)	\
			(ifq)->ifq_drv_tail = NULL;			\
		(m)->m_nextpkt = NULL;					\
		(ifq)->ifq_drv_len--;					\
	} else {							\
		IFQ_LOCK(ifq);						\
		IFQ_DEQUEUE_NOLOCK(ifq, m);				\
		while ((ifq)->ifq_drv_len < (ifq)->ifq_drv_maxlen) {	\
			struct mbuf *m0;				\
			IFQ_DEQUEUE_NOLOCK(ifq, m0);			\
			if (m0 == NULL)					\
				break;					\
			m0->m_nextpkt = NULL;				\
			if ((ifq)->ifq_drv_tail == NULL)		\
				(ifq)->ifq_drv_head = m0;		\
			else						\
				(ifq)->ifq_drv_tail->m_nextpkt = m0;	\
			(ifq)->ifq_drv_tail = m0;			\
			(ifq)->ifq_drv_len++;				\
		}							\
		IFQ_UNLOCK(ifq);					\
	}								\
} while (0)

#define	IFQ_DRV_PREPEND(ifq, m)						\
do {									\
	(m)->m_nextpkt = (ifq)->ifq_drv_head;				\
	if ((ifq)->ifq_drv_tail == NULL)				\
		(ifq)->ifq_drv_tail = (m);				\
	(ifq)->ifq_drv_head = (m);					\
	(ifq)->ifq_drv_len++;						\
} while (0)

#define	IFQ_DRV_IS_EMPTY(ifq)						\
	(((ifq)->ifq_drv_len == 0) && ((ifq)->ifq_len == 0))

#define	IFQ_DRV_PURGE(ifq)						\
do {									\
	struct mbuf *m, *n = (ifq)->ifq_drv_head;			\
	while((m = n) != NULL) {					\
		n = m->m_nextpkt;					\
		m_freem(m);						\
	}								\
	(ifq)->ifq_drv_head = (ifq)->ifq_drv_tail = NULL;		\
	(ifq)->ifq_drv_len = 0;						\
	IFQ_PURGE(ifq);							\
} while (0)

static __inline int
drbr_enqueue(struct ifnet *ifp, struct buf_ring *br, struct mbuf *m)
{	
	int error = 0;

#ifdef ALTQ
	if (ALTQ_IS_ENABLED(&ifp->if_snd)) {
		IFQ_ENQUEUE(&ifp->if_snd, m, error);
		if (error)
			if_inc_counter((ifp), IFCOUNTER_OQDROPS, 1);
		return (error);
	}
#endif
	error = buf_ring_enqueue(br, m);
	if (error)
		m_freem(m);

	return (error);
}

static __inline void
drbr_putback(struct ifnet *ifp, struct buf_ring *br, struct mbuf *new)
{
	/*
	 * The top of the list needs to be swapped 
	 * for this one.
	 */
#ifdef ALTQ
	if (ifp != NULL && ALTQ_IS_ENABLED(&ifp->if_snd)) {
		/* 
		 * Peek in altq case dequeued it
		 * so put it back.
		 */
		IFQ_DRV_PREPEND(&ifp->if_snd, new);
		return;
	}
#endif
	buf_ring_putback_sc(br, new);
}

static __inline struct mbuf *
drbr_peek(struct ifnet *ifp, struct buf_ring *br)
{
#ifdef ALTQ
	struct mbuf *m;
	if (ifp != NULL && ALTQ_IS_ENABLED(&ifp->if_snd)) {
		/* 
		 * Pull it off like a dequeue
		 * since drbr_advance() does nothing
		 * for altq and drbr_putback() will
		 * use the old prepend function.
		 */
		IFQ_DEQUEUE(&ifp->if_snd, m);
		return (m);
	}
#endif
	return(buf_ring_peek_clear_sc(br));
}

static __inline void
drbr_flush(struct ifnet *ifp, struct buf_ring *br)
{
	struct mbuf *m;

#ifdef ALTQ
	if (ifp != NULL && ALTQ_IS_ENABLED(&ifp->if_snd))
		IFQ_PURGE(&ifp->if_snd);
#endif	
	while ((m = buf_ring_dequeue_sc(br)) != NULL)
		m_freem(m);
}

static __inline void
drbr_free(struct buf_ring *br, struct malloc_type *type)
{

	drbr_flush(NULL, br);
	buf_ring_free(br, type);
}

static __inline struct mbuf *
drbr_dequeue(struct ifnet *ifp, struct buf_ring *br)
{
#ifdef ALTQ
	struct mbuf *m;

	if (ifp != NULL && ALTQ_IS_ENABLED(&ifp->if_snd)) {	
		IFQ_DEQUEUE(&ifp->if_snd, m);
		return (m);
	}
#endif
	return (buf_ring_dequeue_sc(br));
}

static __inline void
drbr_advance(struct ifnet *ifp, struct buf_ring *br)
{
#ifdef ALTQ
	/* Nothing to do here since peek dequeues in altq case */
	if (ifp != NULL && ALTQ_IS_ENABLED(&ifp->if_snd))
		return;
#endif
	return (buf_ring_advance_sc(br));
}


static __inline struct mbuf *
drbr_dequeue_cond(struct ifnet *ifp, struct buf_ring *br,
    int (*func) (struct mbuf *, void *), void *arg) 
{
	struct mbuf *m;
#ifdef ALTQ
	if (ALTQ_IS_ENABLED(&ifp->if_snd)) {
		IFQ_LOCK(&ifp->if_snd);
		IFQ_POLL_NOLOCK(&ifp->if_snd, m);
		if (m != NULL && func(m, arg) == 0) {
			IFQ_UNLOCK(&ifp->if_snd);
			return (NULL);
		}
		IFQ_DEQUEUE_NOLOCK(&ifp->if_snd, m);
		IFQ_UNLOCK(&ifp->if_snd);
		return (m);
	}
#endif
	m = buf_ring_peek(br);
	if (m == NULL || func(m, arg) == 0)
		return (NULL);

	return (buf_ring_dequeue_sc(br));
}

static __inline int
drbr_empty(struct ifnet *ifp, struct buf_ring *br)
{
#ifdef ALTQ
	if (ALTQ_IS_ENABLED(&ifp->if_snd))
		return (IFQ_IS_EMPTY(&ifp->if_snd));
#endif
	return (buf_ring_empty(br));
}

static __inline int
drbr_needs_enqueue(struct ifnet *ifp, struct buf_ring *br)
{
#ifdef ALTQ
	if (ALTQ_IS_ENABLED(&ifp->if_snd))
		return (1);
#endif
	return (!buf_ring_empty(br));
}

static __inline int
drbr_inuse(struct ifnet *ifp, struct buf_ring *br)
{
#ifdef ALTQ
	if (ALTQ_IS_ENABLED(&ifp->if_snd))
		return (ifp->if_snd.ifq_len);
#endif
	return (buf_ring_count(br));
}

extern	int ifqmaxlen;

void	if_qflush(struct ifnet *);
void	ifq_init(struct ifaltq *, struct ifnet *ifp);
void	ifq_delete(struct ifaltq *);

#endif /* _KERNEL */
#endif /* !_NET_IFQ_H_ */
