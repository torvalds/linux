/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2011 Daniel Braniss <danny@cs.huji.ac.il>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 | $Id: iscsivar.h 743 2009-08-08 10:54:53Z danny $
 */
#define ISCSI_MAX_LUNS		128	// don't touch this
#if ISCSI_MAX_LUNS > 8
/*
 | for this to work 
 | sysctl kern.cam.cam_srch_hi=1
 */
#endif

#ifndef ISCSI_INITIATOR_DEBUG
#define ISCSI_INITIATOR_DEBUG 1
#endif

#ifdef ISCSI_INITIATOR_DEBUG
extern int iscsi_debug;
#define debug(level, fmt, args...)	do {if(level <= iscsi_debug)\
	printf("%s: " fmt "\n", __func__ , ##args);} while(0)
#define sdebug(level, fmt, args...)	do {if(level <= iscsi_debug)\
     	printf("%d] %s: " fmt "\n", sp->sid, __func__ , ##args);} while(0)
#define debug_called(level)		do {if(level <= iscsi_debug)\
	printf("%s: called\n",  __func__);} while(0)
#else
#define debug(level, fmt, args...)
#define debug_called(level)
#define sdebug(level, fmt, args...)
#endif /* ISCSI_INITIATOR_DEBUG */

#define xdebug(fmt, args...)	printf(">>> %s: " fmt "\n", __func__ , ##args)

#define MAX_SESSIONS	ISCSI_MAX_TARGETS
#define MAX_PDUS	(MAX_SESSIONS*256) // XXX: at the moment this is arbitrary

typedef uint32_t digest_t(const void *, int len, uint32_t ocrc);

MALLOC_DECLARE(M_ISCSI);
MALLOC_DECLARE(M_ISCSIBUF);

#define ISOK2DIG(dig, pp)	((dig != NULL) && ((pp->ipdu.bhs.opcode & 0x1f) != ISCSI_LOGIN_CMD))

#ifndef BIT
#define BIT(n)	(1 <<(n))
#endif

#define ISC_SM_RUN	BIT(0)
#define ISC_SM_RUNNING	BIT(1)

#define ISC_LINK_UP	BIT(2)
#define ISC_CON_RUN	BIT(3)
#define ISC_CON_RUNNING	BIT(4)
#define ISC_KILL	BIT(5)
#define ISC_OQNOTEMPTY	BIT(6)
#define ISC_OWAITING	BIT(7)
#define ISC_FFPHASE	BIT(8)

#define ISC_CAMDEVS	BIT(9)
#define ISC_SCANWAIT	BIT(10)

#define ISC_MEMWAIT	BIT(11)
#define ISC_SIGNALED	BIT(12)

#define ISC_HOLD	BIT(15)
#define ISC_HOLDED	BIT(16)

#define ISC_SHUTDOWN	BIT(31)

/*
 | some stats
 */
struct i_stats {
     int	npdu;	// number of pdus malloc'ed.
     int	nrecv;	// unprocessed received pdus
     int	nsent;	// sent pdus

     int	nrsp, max_rsp;
     int	nrsv, max_rsv;
     int	ncsnd, max_csnd;
     int	nisnd, max_isnd;
     int	nwsnd, max_wsnd;
     int	nhld, max_hld;

     struct bintime t_sent;
     struct bintime t_recv;
};

/*
 | one per 'session'
 */

typedef TAILQ_HEAD(, pduq) queue_t;

typedef struct isc_session {
     TAILQ_ENTRY(isc_session)	sp_link;
     int		flags;
     struct cdev	*dev;
     struct socket	*soc;
     struct file	*fp;
     struct thread	*td;

     struct proc 	*proc; // the userland process
     int		signal;
     struct proc 	*soc_proc;
     struct proc	*stp;	// the sm thread

     struct isc_softc	*isc;

     digest_t   	*hdrDigest;     // the digest alg. if any
     digest_t   	*dataDigest;    // the digest alg. if any

     int		sid;		// Session ID
     sn_t       	sn;             // sequence number stuff;
     int		cws;		// current window size

     int		target_nluns; // this and target_lun are
				      // hopefully temporal till I
				      // figure out a better way.
     int		target_lun[ISCSI_MAX_LUNS/(sizeof(int)*8) + 1];

     struct mtx		rsp_mtx;
     struct mtx		rsv_mtx;
     struct mtx		snd_mtx;
     struct mtx		hld_mtx;
     struct mtx		io_mtx;
     queue_t		rsp;
     queue_t		rsv;
     queue_t		csnd;
     queue_t		isnd;
     queue_t		wsnd;
     queue_t		hld;				

     isc_opt_t		opt;	// negotiable values

     struct i_stats	stats;
     bhs_t		bhs;
     struct uio		uio;
     struct iovec	iov;
     /*
      | cam stuff
      */
     struct cam_sim	*cam_sim;
     struct cam_path	*cam_path;
     struct mtx		cam_mtx;
     /*
      | sysctl stuff
      */
     struct sysctl_ctx_list	clist;
     struct sysctl_oid	*oid;
     int	douio;	//XXX: turn on/off uio on read
} isc_session_t;

typedef struct pduq {
     TAILQ_ENTRY(pduq)	pq_link;

     caddr_t		buf;
     u_int		len;	// the total length of the pdu
     pdu_t		pdu;
     union ccb		*ccb;

     struct uio		uio;
     struct iovec	iov[5];	// XXX: careful ...
     struct mbuf	*mp;
     struct bintime	ts;
     queue_t		*pduq;		
} pduq_t;
/*
 */
struct isc_softc {
     struct mtx		isc_mtx;
     TAILQ_HEAD(,isc_session)	isc_sess;
     int		nsess;
     struct cdev	*dev;
     char		isid[6];	// Initiator Session ID (48 bits)
     struct unrhdr	*unit;
     struct sx 		unit_sx;

     uma_zone_t		pdu_zone;	// pool of free pdu's
     TAILQ_HEAD(,pduq)	freepdu;

#ifdef  ISCSI_INITIATOR_DEBUG
     int		 npdu_alloc, npdu_max; // for instrumentation
#endif
#ifdef DO_EVENTHANDLER
     eventhandler_tag	eh;
#endif
     /*
      | sysctl stuff
      */
     struct sysctl_ctx_list	clist;
     struct sysctl_oid		*oid;
};

#ifdef  ISCSI_INITIATOR_DEBUG
extern struct mtx iscsi_dbg_mtx;
#endif

void	isc_start_receiver(isc_session_t *sp);
void	isc_stop_receiver(isc_session_t *sp);

int	isc_sendPDU(isc_session_t *sp, pduq_t *pq);
int	isc_qout(isc_session_t *sp, pduq_t *pq);
int	i_prepPDU(isc_session_t *sp, pduq_t *pq);

int	ism_fullfeature(struct cdev *dev, int flag);

int	i_pdu_flush(isc_session_t *sc);
int	i_setopt(isc_session_t *sp, isc_opt_t *opt);
void	i_freeopt(isc_opt_t *opt);

int	ic_init(isc_session_t *sp);
void	ic_destroy(isc_session_t *sp);
void	ic_lost_target(isc_session_t *sp, int target);
int	ic_getCamVals(isc_session_t *sp, iscsi_cam_t *cp);

void	ism_recv(isc_session_t *sp, pduq_t *pq);
int	ism_start(isc_session_t *sp);
void	ism_restart(isc_session_t *sp);
void	ism_stop(isc_session_t *sp);

int	scsi_encap(struct cam_sim *sim, union ccb *ccb);
int	scsi_decap(isc_session_t *sp, pduq_t *opq, pduq_t *pq);
void	iscsi_r2t(isc_session_t *sp, pduq_t *opq, pduq_t *pq);
void	iscsi_done(isc_session_t *sp, pduq_t *opq, pduq_t *pq);
void	iscsi_reject(isc_session_t *sp, pduq_t *opq, pduq_t *pq);
void	iscsi_async(isc_session_t *sp,  pduq_t *pq);
void	iscsi_cleanup(isc_session_t *sp);
int	iscsi_requeue(isc_session_t *sp);

// Serial Number Arithmetic
#define _MAXINCR	0x7FFFFFFF	// 2 ^ 31 - 1
#define SNA_GT(i1, i2)	((i1 != i2) && (\
	(i1 < i2 && i2 - i1 > _MAXINCR) ||\
	(i1 > i2 && i1 - i2 < _MAXINCR))?1: 0)

/*
 | inlines
 */
#ifdef _CAM_CAM_XPT_SIM_H

#if __FreeBSD_version <  600000
#define CAM_LOCK(arg)
#define CAM_ULOCK(arg)

static __inline void
XPT_DONE(isc_session_t *sp, union ccb *ccb)
{
     mtx_lock(&Giant);
     xpt_done(ccb);
     mtx_unlock(&Giant);
}
#elif __FreeBSD_version >= 700000
#define CAM_LOCK(arg)	mtx_lock(&arg->cam_mtx)
#define CAM_UNLOCK(arg)	mtx_unlock(&arg->cam_mtx)

static __inline void
XPT_DONE(isc_session_t *sp, union ccb *ccb)
{
     CAM_LOCK(sp);
     xpt_done(ccb);
     CAM_UNLOCK(sp);
}
#else
//__FreeBSD_version >= 600000
#define CAM_LOCK(arg)
#define CAM_UNLOCK(arg)
#define XPT_DONE(ignore, arg)	xpt_done(arg)
#endif

#endif /* _CAM_CAM_XPT_SIM_H */

static __inline pduq_t *
pdu_alloc(struct isc_softc *isc, int wait)
{
     pduq_t	*pq;

     pq = (pduq_t *)uma_zalloc(isc->pdu_zone, wait /* M_WAITOK or M_NOWAIT*/);
     if(pq == NULL) {
	  debug(7, "out of mem");
	  return NULL;
     }
#ifdef ISCSI_INITIATOR_DEBUG
     mtx_lock(&iscsi_dbg_mtx);
     isc->npdu_alloc++;
     if(isc->npdu_alloc > isc->npdu_max)
	  isc->npdu_max = isc->npdu_alloc;
     mtx_unlock(&iscsi_dbg_mtx);
#endif
     memset(pq, 0, sizeof(pduq_t));

     return pq;
}

static __inline void
pdu_free(struct isc_softc *isc, pduq_t *pq)
{
     if(pq->mp)
	  m_freem(pq->mp);
#ifdef NO_USE_MBUF
     if(pq->buf != NULL)
	  free(pq->buf, M_ISCSIBUF);
#endif
     uma_zfree(isc->pdu_zone, pq);
#ifdef ISCSI_INITIATOR_DEBUG
     mtx_lock(&iscsi_dbg_mtx);
     isc->npdu_alloc--;
     mtx_unlock(&iscsi_dbg_mtx);
#endif
}

static __inline void
i_nqueue_rsp(isc_session_t *sp, pduq_t *pq)
{
     mtx_lock(&sp->rsp_mtx);
     if(++sp->stats.nrsp > sp->stats.max_rsp)
	  sp->stats.max_rsp = sp->stats.nrsp;
     TAILQ_INSERT_TAIL(&sp->rsp, pq, pq_link);
     mtx_unlock(&sp->rsp_mtx);
}

static __inline pduq_t *
i_dqueue_rsp(isc_session_t *sp)
{
     pduq_t *pq;

     mtx_lock(&sp->rsp_mtx);
     if((pq = TAILQ_FIRST(&sp->rsp)) != NULL) {
	  sp->stats.nrsp--;
	  TAILQ_REMOVE(&sp->rsp, pq, pq_link);
     }
     mtx_unlock(&sp->rsp_mtx);

     return pq;
}

static __inline void
i_nqueue_rsv(isc_session_t *sp, pduq_t *pq)
{
     mtx_lock(&sp->rsv_mtx);
     if(++sp->stats.nrsv > sp->stats.max_rsv)
	  sp->stats.max_rsv = sp->stats.nrsv;
     TAILQ_INSERT_TAIL(&sp->rsv, pq, pq_link);
     mtx_unlock(&sp->rsv_mtx);
}

static __inline pduq_t *
i_dqueue_rsv(isc_session_t *sp)
{
     pduq_t *pq;

     mtx_lock(&sp->rsv_mtx);
     if((pq = TAILQ_FIRST(&sp->rsv)) != NULL) {
	  sp->stats.nrsv--;
	  TAILQ_REMOVE(&sp->rsv, pq, pq_link);
     }
     mtx_unlock(&sp->rsv_mtx);

     return pq;
}

static __inline void
i_nqueue_csnd(isc_session_t *sp, pduq_t *pq)
{
     mtx_lock(&sp->snd_mtx);
     if(++sp->stats.ncsnd > sp->stats.max_csnd)
	  sp->stats.max_csnd = sp->stats.ncsnd;
     TAILQ_INSERT_TAIL(&sp->csnd, pq, pq_link);
     mtx_unlock(&sp->snd_mtx);
}

static __inline pduq_t *
i_dqueue_csnd(isc_session_t *sp)
{
     pduq_t *pq;

     mtx_lock(&sp->snd_mtx);
     if((pq = TAILQ_FIRST(&sp->csnd)) != NULL) {
	  sp->stats.ncsnd--;
	  TAILQ_REMOVE(&sp->csnd, pq, pq_link);
     }
     mtx_unlock(&sp->snd_mtx);

     return pq;
}

static __inline void
i_nqueue_isnd(isc_session_t *sp, pduq_t *pq)
{
     mtx_lock(&sp->snd_mtx);
     if(++sp->stats.nisnd > sp->stats.max_isnd)
	  sp->stats.max_isnd = sp->stats.nisnd;
     TAILQ_INSERT_TAIL(&sp->isnd, pq, pq_link);
     mtx_unlock(&sp->snd_mtx);
}

static __inline pduq_t *
i_dqueue_isnd(isc_session_t *sp)
{
     pduq_t *pq;

     mtx_lock(&sp->snd_mtx);
     if((pq = TAILQ_FIRST(&sp->isnd)) != NULL) {
	  sp->stats.nisnd--;
	  TAILQ_REMOVE(&sp->isnd, pq, pq_link);
     }
     mtx_unlock(&sp->snd_mtx);

     return pq;
}

static __inline void
i_nqueue_wsnd(isc_session_t *sp, pduq_t *pq)
{
     mtx_lock(&sp->snd_mtx);
     if(++sp->stats.nwsnd > sp->stats.max_wsnd)
	  sp->stats.max_wsnd = sp->stats.nwsnd;
     TAILQ_INSERT_TAIL(&sp->wsnd, pq, pq_link);
     mtx_unlock(&sp->snd_mtx);
}

static __inline pduq_t *
i_dqueue_wsnd(isc_session_t *sp)
{
     pduq_t *pq;

     mtx_lock(&sp->snd_mtx);
     if((pq = TAILQ_FIRST(&sp->wsnd)) != NULL) {
	  sp->stats.nwsnd--;
	  TAILQ_REMOVE(&sp->wsnd, pq, pq_link);
     }
     mtx_unlock(&sp->snd_mtx);

     return pq;
}

static __inline pduq_t *
i_dqueue_snd(isc_session_t *sp, int which)
{
     pduq_t *pq;

     pq = NULL;
     mtx_lock(&sp->snd_mtx);
     if((which & BIT(0)) && (pq = TAILQ_FIRST(&sp->isnd)) != NULL) {
	  sp->stats.nisnd--;
	  TAILQ_REMOVE(&sp->isnd, pq, pq_link);
	  pq->pduq = &sp->isnd;	// remember where you came from
     } else
     if((which & BIT(1)) && (pq = TAILQ_FIRST(&sp->wsnd)) != NULL) {
	  sp->stats.nwsnd--;
	  TAILQ_REMOVE(&sp->wsnd, pq, pq_link);
	  pq->pduq = &sp->wsnd;	// remember where you came from
     } else
     if((which & BIT(2)) && (pq = TAILQ_FIRST(&sp->csnd)) != NULL) {
	  sp->stats.ncsnd--;
	  TAILQ_REMOVE(&sp->csnd, pq, pq_link);
	  pq->pduq = &sp->csnd;	// remember where you came from
     }
     mtx_unlock(&sp->snd_mtx);

     return pq;
}

static __inline void
i_rqueue_pdu(isc_session_t *sp, pduq_t *pq)
{
     mtx_lock(&sp->snd_mtx);
     KASSERT(pq->pduq != NULL, ("pq->pduq is NULL"));
     TAILQ_INSERT_TAIL(pq->pduq, pq, pq_link);
     mtx_unlock(&sp->snd_mtx);     
}

/*
 | Waiting for ACK (or something :-)
 */
static __inline void
i_nqueue_hld(isc_session_t *sp, pduq_t *pq)
{
     getbintime(&pq->ts);
     mtx_lock(&sp->hld_mtx);
     if(++sp->stats.nhld > sp->stats.max_hld)
	  sp->stats.max_hld = sp->stats.nhld;
     TAILQ_INSERT_TAIL(&sp->hld, pq, pq_link);
     mtx_unlock(&sp->hld_mtx);
     return;
}

static __inline void
i_remove_hld(isc_session_t *sp, pduq_t *pq)
{
     mtx_lock(&sp->hld_mtx);
     sp->stats.nhld--;
     TAILQ_REMOVE(&sp->hld, pq, pq_link);
     mtx_unlock(&sp->hld_mtx);
}

static __inline pduq_t *
i_dqueue_hld(isc_session_t *sp)
{
     pduq_t *pq;

     mtx_lock(&sp->hld_mtx);
     if((pq = TAILQ_FIRST(&sp->hld)) != NULL) {
	  sp->stats.nhld--;
	  TAILQ_REMOVE(&sp->hld, pq, pq_link);
     }
     mtx_unlock(&sp->hld_mtx);

     return pq;
}

static __inline pduq_t *
i_search_hld(isc_session_t *sp, int itt, int keep)
{
     pduq_t	*pq, *tmp;

     pq = NULL;

     mtx_lock(&sp->hld_mtx);
     TAILQ_FOREACH_SAFE(pq, &sp->hld, pq_link, tmp) {
	  if(pq->pdu.ipdu.bhs.itt == itt) {
	       if(!keep) {
		    sp->stats.nhld--;
		    TAILQ_REMOVE(&sp->hld, pq, pq_link);
	       }
	       break;
	  }
     }
     mtx_unlock(&sp->hld_mtx);

     return pq;
}

static __inline void
i_acked_hld(isc_session_t *sp, pdu_t *op)
{
     pduq_t	*pq, *tmp;
     u_int exp = sp->sn.expCmd;
     
     pq = NULL;
     mtx_lock(&sp->hld_mtx);
     TAILQ_FOREACH_SAFE(pq, &sp->hld, pq_link, tmp) {
	  if((op && op->ipdu.bhs.itt == pq->pdu.ipdu.bhs.itt)
	     || (pq->ccb == NULL
		 && (pq->pdu.ipdu.bhs.opcode != ISCSI_WRITE_DATA)
		 && SNA_GT(exp, ntohl(pq->pdu.ipdu.bhs.ExpStSN)))) {
	       sp->stats.nhld--;
	       TAILQ_REMOVE(&sp->hld, pq, pq_link);
	       pdu_free(sp->isc, pq);
	  }
     }
     mtx_unlock(&sp->hld_mtx);
}

static __inline void
i_mbufcopy(struct mbuf *mp, caddr_t dp, int len)
{
     struct mbuf *m;
     caddr_t bp;

     for(m = mp; m != NULL; m = m->m_next) {
	  bp = mtod(m, caddr_t);
	  /*
	   | the pdu is word (4 octed) aligned
	   | so len <= packet
	   */
	  memcpy(dp, bp, MIN(len, m->m_len));
	  dp += m->m_len;
	  len -= m->m_len;
	  if(len <= 0)
	       break;
     }
}
