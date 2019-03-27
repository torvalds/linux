/*-
 * Copyright (c) 1991-1997 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Network Research
 *	Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
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
 * $KAME: altq_classq.h,v 1.6 2003/01/07 07:33:38 kjc Exp $
 * $FreeBSD$
 */
/*
 * class queue definitions extracted from rm_class.h.
 */
#ifndef _ALTQ_ALTQ_CLASSQ_H_
#define	_ALTQ_ALTQ_CLASSQ_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Packet Queue types: RED or DROPHEAD.
 */
#define	Q_DROPHEAD	0x00
#define	Q_RED		0x01
#define	Q_RIO		0x02
#define	Q_DROPTAIL	0x03
#define	Q_CODEL		0x04

#ifdef _KERNEL

/*
 * Packet Queue structures and macros to manipulate them.
 */
struct _class_queue_ {
	struct mbuf	*tail_;	/* Tail of packet queue */
	int	qlen_;		/* Queue length (in number of packets) */
	int	qlim_;		/* Queue limit (in number of packets*) */
	int	qsize_;		/* Queue size (in number of bytes*) */
	int	qtype_;		/* Queue type */
};

typedef struct _class_queue_	class_queue_t;

#define	qtype(q)	(q)->qtype_		/* Get queue type */
#define	qlimit(q)	(q)->qlim_		/* Max packets to be queued */
#define	qlen(q)		(q)->qlen_		/* Current queue length. */
#define	qsize(q)	(q)->qsize_		/* Current queue size. */
#define	qtail(q)	(q)->tail_		/* Tail of the queue */
#define	qhead(q)	((q)->tail_ ? (q)->tail_->m_nextpkt : NULL)

#define	qempty(q)	((q)->qlen_ == 0)	/* Is the queue empty?? */
#define	q_is_codel(q)	((q)->qtype_ == Q_CODEL) /* Is the queue a codel queue */
#define	q_is_red(q)	((q)->qtype_ == Q_RED)	/* Is the queue a red queue */
#define	q_is_rio(q)	((q)->qtype_ == Q_RIO)	/* Is the queue a rio queue */
#define	q_is_red_or_rio(q)	((q)->qtype_ == Q_RED || (q)->qtype_ == Q_RIO)

#if !defined(__GNUC__) || defined(ALTQ_DEBUG)

extern void		_addq(class_queue_t *, struct mbuf *);
extern struct mbuf	*_getq(class_queue_t *);
extern struct mbuf	*_getq_tail(class_queue_t *);
extern struct mbuf	*_getq_random(class_queue_t *);
extern void		_removeq(class_queue_t *, struct mbuf *);
extern void		_flushq(class_queue_t *);

#else /* __GNUC__ && !ALTQ_DEBUG */
/*
 * inlined versions
 */
static __inline void
_addq(class_queue_t *q, struct mbuf *m)
{
        struct mbuf *m0;

	if ((m0 = qtail(q)) != NULL)
		m->m_nextpkt = m0->m_nextpkt;
	else
		m0 = m;
	m0->m_nextpkt = m;
	qtail(q) = m;
	qlen(q)++;
	qsize(q) += m_pktlen(m);
}

static __inline struct mbuf *
_getq(class_queue_t *q)
{
	struct mbuf  *m, *m0;

	if ((m = qtail(q)) == NULL)
		return (NULL);
	if ((m0 = m->m_nextpkt) != m)
		m->m_nextpkt = m0->m_nextpkt;
	else
		qtail(q) = NULL;
	qlen(q)--;
	qsize(q) -= m_pktlen(m0);
	m0->m_nextpkt = NULL;
	return (m0);
}

/* drop a packet at the tail of the queue */
static __inline struct mbuf *
_getq_tail(class_queue_t *q)
{
	struct mbuf *m, *m0, *prev;

	if ((m = m0 = qtail(q)) == NULL)
		return NULL;
	do {
		prev = m0;
		m0 = m0->m_nextpkt;
	} while (m0 != m);
	prev->m_nextpkt = m->m_nextpkt;
	if (prev == m)
		qtail(q) = NULL;
	else
		qtail(q) = prev;
	qlen(q)--;
	m->m_nextpkt = NULL;
	return (m);
}

/* randomly select a packet in the queue */
static __inline struct mbuf *
_getq_random(class_queue_t *q)
{
	struct mbuf *m;
	int i, n;

	if ((m = qtail(q)) == NULL)
		return NULL;
	if (m->m_nextpkt == m)
		qtail(q) = NULL;
	else {
		struct mbuf *prev = NULL;

		n = random() % qlen(q) + 1;
		for (i = 0; i < n; i++) {
			prev = m;
			m = m->m_nextpkt;
		}
		prev->m_nextpkt = m->m_nextpkt;
		if (m == qtail(q))
			qtail(q) = prev;
	}
	qlen(q)--;
	m->m_nextpkt = NULL;
	return (m);
}

static __inline void
_removeq(class_queue_t *q, struct mbuf *m)
{
	struct mbuf *m0, *prev;

	m0 = qtail(q);
	do {
		prev = m0;
		m0 = m0->m_nextpkt;
	} while (m0 != m);
	prev->m_nextpkt = m->m_nextpkt;
	if (prev == m)
		qtail(q) = NULL;
	else if (qtail(q) == m)
		qtail(q) = prev;
	qlen(q)--;
}

static __inline void
_flushq(class_queue_t *q)
{
	struct mbuf *m;

	while ((m = _getq(q)) != NULL)
		m_freem(m);
}

#endif /* __GNUC__ && !ALTQ_DEBUG */

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* _ALTQ_ALTQ_CLASSQ_H_ */
