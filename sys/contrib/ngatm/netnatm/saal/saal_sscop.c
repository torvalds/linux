/*
 * Copyright (c) 1996-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
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
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * $Begemot: libunimsg/netnatm/saal/saal_sscop.c,v 1.11 2004/07/08 08:22:13 brandt Exp $
 *
 * Core SSCOP code (ITU-T Q.2110)
 */

#include <netnatm/saal/sscop.h>
#include <netnatm/saal/sscoppriv.h>

#ifndef FAILURE
#define FAILURE(S)
#endif

#define MKSTR(S)	#S

static const char *const sscop_sigs[] = {
	MKSTR(SSCOP_ESTABLISH_request),
	MKSTR(SSCOP_ESTABLISH_indication),
	MKSTR(SSCOP_ESTABLISH_response),
	MKSTR(SSCOP_ESTABLISH_confirm),
	MKSTR(SSCOP_RELEASE_request),
	MKSTR(SSCOP_RELEASE_indication),
	MKSTR(SSCOP_RELEASE_confirm),
	MKSTR(SSCOP_DATA_request),
	MKSTR(SSCOP_DATA_indication),
	MKSTR(SSCOP_UDATA_request),
	MKSTR(SSCOP_UDATA_indication),
	MKSTR(SSCOP_RECOVER_indication),
	MKSTR(SSCOP_RECOVER_response),
	MKSTR(SSCOP_RESYNC_request),
	MKSTR(SSCOP_RESYNC_indication),
	MKSTR(SSCOP_RESYNC_response),
	MKSTR(SSCOP_RESYNC_confirm),
	MKSTR(SSCOP_RETRIEVE_request),
	MKSTR(SSCOP_RETRIEVE_indication),
	MKSTR(SSCOP_RETRIEVE_COMPL_indication),
};

static const char *const sscop_msigs[] = {
	MKSTR(SSCOP_MDATA_request),
	MKSTR(SSCOP_MDATA_indication),
	MKSTR(SSCOP_MERROR_indication),
};

static const char *const states[] = {
	MKSTR(SSCOP_IDLE),
	MKSTR(SSCOP_OUT_PEND),
	MKSTR(SSCOP_IN_PEND),
	MKSTR(SSCOP_OUT_DIS_PEND),
	MKSTR(SSCOP_OUT_RESYNC_PEND),
	MKSTR(SSCOP_IN_RESYNC_PEND),
	MKSTR(SSCOP_OUT_REC_PEND),
	MKSTR(SSCOP_REC_PEND),
	MKSTR(SSCOP_IN_REC_PEND),
	MKSTR(SSCOP_READY),
};

#ifdef SSCOP_DEBUG
static const char *const events[] = {
	MKSTR(SIG_BGN),
	MKSTR(SIG_BGAK),
	MKSTR(SIG_END),
	MKSTR(SIG_ENDAK),
	MKSTR(SIG_RS),
	MKSTR(SIG_RSAK),
	MKSTR(SIG_BGREJ),
	MKSTR(SIG_SD),
	MKSTR(SIG_ER),
	MKSTR(SIG_POLL),
	MKSTR(SIG_STAT),
	MKSTR(SIG_USTAT),
	MKSTR(SIG_UD),
	MKSTR(SIG_MD),
	MKSTR(SIG_ERAK),

	MKSTR(SIG_T_CC),
	MKSTR(SIG_T_POLL),
	MKSTR(SIG_T_KA),
	MKSTR(SIG_T_NR),
	MKSTR(SIG_T_IDLE),

	MKSTR(SIG_PDU_Q),
	MKSTR(SIG_USER_DATA),
	MKSTR(SIG_ESTAB_REQ),
	MKSTR(SIG_ESTAB_RESP),
	MKSTR(SIG_RELEASE_REQ),
	MKSTR(SIG_RECOVER),
	MKSTR(SIG_SYNC_REQ),
	MKSTR(SIG_SYNC_RESP),
	MKSTR(SIG_UDATA),
	MKSTR(SIG_MDATA),
	MKSTR(SIG_UPDU_Q),
	MKSTR(SIG_MPDU_Q),
	MKSTR(SIG_RETRIEVE),
};

static const char *const pdus[] = {
	"illegale PDU type 0",		/* no PDU type 0 */
	MKSTR(PDU_BGN),
	MKSTR(PDU_BGAK),
	MKSTR(PDU_END),
	MKSTR(PDU_ENDAK),
	MKSTR(PDU_RS),
	MKSTR(PDU_RSAK),
	MKSTR(PDU_BGREJ),
	MKSTR(PDU_SD),
	MKSTR(PDU_ER),
	MKSTR(PDU_POLL),
	MKSTR(PDU_STAT),
	MKSTR(PDU_USTAT),
	MKSTR(PDU_UD),
	MKSTR(PDU_MD),
	MKSTR(PDU_ERAK),
};
#endif

MEMINIT();

static void sscop_signal(struct sscop *, u_int, struct sscop_msg *);
static void sscop_save_signal(struct sscop *, u_int, struct sscop_msg *);
static void handle_sigs(struct sscop *);
static void sscop_set_state(struct sscop *, u_int);

/************************************************************/


/************************************************************/
/*
 * Queue macros
 */
#define SSCOP_MSG_FREE(MSG)						\
    do {								\
	if(MSG) {							\
		MBUF_FREE((MSG)->m);					\
		MSG_FREE((MSG));					\
	}								\
    } while(0)

static inline struct sscop_msg *QFIND(sscop_msgq_head_t *q, u_int rn)
{
	struct sscop_msg *msg = NULL, *m;
	MSGQ_FOREACH(m, q) {
		if(m->seqno == rn) {
			msg = m;
			break;
		}
	}
	return msg;
}

#define QINSERT(Q,M)							\
    do {								\
	struct sscop_msg *_msg = NULL, *_m;				\
	MSGQ_FOREACH(_m, (Q)) {						\
		if (_m->seqno > (M)->seqno) {				\
			_msg = _m;					\
			break;						\
		}							\
	}								\
	if (_msg != NULL)							\
		MSGQ_INSERT_BEFORE(_msg, (M));				\
	else								\
		MSGQ_APPEND((Q), (M));					\
    } while (0)


/*
 * Send an error indication to the management plane.
 */
#define MAAL_ERROR(S,E,C) 						\
    do {								\
	VERBOSE(S, SSCOP_DBG_USIG, ((S), (S)->aarg,			\
	    "MAA-Signal %s in state %s", 				\
	    sscop_msigs[SSCOP_MERROR_indication], states[(S)->state]));	\
	(S)->funcs->send_manage((S), (S)->aarg,				\
	    SSCOP_MERROR_indication, NULL, (E), (C));			\
    } while(0)

#define MAAL_DATA(S,M) 							\
    do {								\
	VERBOSE(S, SSCOP_DBG_USIG, ((S), (S)->aarg,			\
	    "MAA-Signal %s in state %s",				\
	    sscop_msigs[SSCOP_MDATA_indication], states[(S)->state]));	\
	(S)->funcs->send_manage((S), (S)->aarg,				\
	    SSCOP_MDATA_indication, (M), 0, 0);				\
    } while(0)

#define AAL_DATA(S,D,M,N)						\
    do {								\
	VERBOSE(S, SSCOP_DBG_USIG, ((S), (S)->aarg,			\
	    "AA-Signal %s in state %s",					\
	    sscop_sigs[D], states[(S)->state]));			\
	(S)->funcs->send_upper((S), (S)->aarg, (D), (M), (N));		\
    } while(0)

#define AAL_SIG(S,D)							\
    do {								\
	VERBOSE(S, SSCOP_DBG_USIG, ((S), (S)->aarg,			\
	    "AA-Signal %s in state %s",					\
	    sscop_sigs[D], states[(S)->state]));			\
	(S)->funcs->send_upper((S), (S)->aarg, (D), NULL, 0);		\
    } while(0)

#ifdef SSCOP_DEBUG
#define AAL_SEND(S,M) do {						\
	if (ISVERBOSE(S, SSCOP_DBG_PDU))				\
		sscop_dump_pdu(S, "tx", (M));				\
	(S)->funcs->send_lower((S), (S)->aarg, (M));			\
    } while(0)
#else
#define AAL_SEND(S,M) (S)->funcs->send_lower((S), (S)->aarg, (M))
#endif


/*
 * Free a save user-to-user data buffer and set the pointer to zero
 * to signal, that it is free.
 */
#define FREE_UU(F)							\
	do {								\
		if(sscop->F) {						\
			MBUF_FREE(sscop->F);				\
			sscop->F = NULL;				\
		}							\
	} while(0)

#define SET_UU(F,U)							\
	do {								\
		FREE_UU(F);						\
		sscop->F = U->m;					\
		U->m = NULL;						\
		SSCOP_MSG_FREE(U);					\
	} while(0)

#define AAL_UU_SIGNAL(S, SIG, M, PL, SN)				\
	do {								\
		if(MBUF_LEN((M)->m) > 0) { 				\
			MBUF_UNPAD((M)->m,(PL));			\
			AAL_DATA((S), (SIG), (M)->m, (SN)); 		\
			(M)->m = NULL;					\
		} else {						\
			AAL_DATA((S), (SIG), NULL, (SN));		\
		}							\
		SSCOP_MSG_FREE((M));					\
	} while(0)



TIMER_FUNC(cc, CC)
TIMER_FUNC(nr, NR)
TIMER_FUNC(ka, KA)
TIMER_FUNC(poll, POLL)
TIMER_FUNC(idle, IDLE)

/************************************************************/
/*
 * INSTANCE AND TYPE HANDLING.
 */
#ifdef SSCOP_DEBUG
static void
sscop_dump_pdu(struct sscop *sscop, const char *dir,
    const struct SSCOP_MBUF_T *m)
{
	u_int32_t v1, v2, v3, v4;
	u_int size = MBUF_LEN(m);
	u_int n, i;

	if (size < 8)
		return;

	v1 = MBUF_TRAIL32(m, -1);
	v2 = MBUF_TRAIL32(m, -2);

	switch ((v1 >> 24) & 0xf) {

	  case 0:
		return;

	  case PDU_BGN:
		sscop->funcs->verbose(sscop, sscop->aarg,
		    "%s BGN n(mr)=%u n(sq)=%u pl=%u",
		    dir, v1 & 0xffffff, v2 & 0xff, (v1 >> 30) & 0x3);
		return;

	  case PDU_BGAK:
		sscop->funcs->verbose(sscop, sscop->aarg,
		    "%s BGAK n(mr)=%u pl=%u",
		    dir, v1 & 0xffffff, (v1 >> 30) & 0x3);
		return;

	  case PDU_END:
		sscop->funcs->verbose(sscop, sscop->aarg,
		    "%s END r=%u s=%u pl=%u",
		    dir, (v1 >> 29) & 1, (v1 >> 28) & 1, (v1 >> 30) & 0x3);
		return;

	  case PDU_ENDAK:
		sscop->funcs->verbose(sscop, sscop->aarg, "%s ENDAK", dir);
		return;

	  case PDU_RS:
		sscop->funcs->verbose(sscop, sscop->aarg,
		    "%s RS n(mr)=%u n(sq)=%u pl=%u",
		    dir, v1 & 0xffffff, v2 & 0xff, (v1 >> 30) & 0x3);
		return;

	  case PDU_RSAK:
		sscop->funcs->verbose(sscop, sscop->aarg, "%s RSAK n(mr)=%u",
		    dir, v1 & 0xffffff);
		return;

	  case PDU_BGREJ:
		sscop->funcs->verbose(sscop, sscop->aarg, "%s BGREJ pl=%u",
		    dir, (v1 >> 30) & 0x3);
		return;

	  case PDU_SD:
		sscop->funcs->verbose(sscop, sscop->aarg, "%s SD n(s)=%u pl=%u",
		    dir, v1 & 0xffffff, (v1 >> 30) & 0x3);
		return;

	  case PDU_ER:
		sscop->funcs->verbose(sscop, sscop->aarg, "%s ER n(mr)=%u n(sq)=%u",
		    dir, v1 & 0xffffff, v2 & 0xff);
		return;

	  case PDU_POLL:
		sscop->funcs->verbose(sscop, sscop->aarg, "%s POLL n(s)=%u n(ps)=%u",
		    dir, v1 & 0xffffff, v2 & 0xffffff);
		return;

	  case PDU_STAT:
		if (size < 12)
			return;
		v3 = MBUF_TRAIL32(m, -3);
		sscop->funcs->verbose(sscop, sscop->aarg,
		    "%s STAT n(r)=%u n(mr)=%u n(ps)=%u",
		    dir, v1 & 0xffffff, v2 & 0xffffff, v3 & 0xffffff);
		n = (size - 12) / 4;
		for (i = 0; i < (size - 12) / 4; i++, n--) {
			v4 = MBUF_TRAIL32(m, -4 - (int)i);
			sscop->funcs->verbose(sscop, sscop->aarg,
			    "   LE(%u)=%u", n, v4 & 0xffffff);
		}
		return;

	  case PDU_USTAT:
		if (size < 16)
			return;
		sscop->funcs->verbose(sscop, sscop->aarg,
		    "%s STAT n(r)=%u n(mr)=%u LE1=%u LE2=%u",
		    dir, v1 & 0xffffff, v2 & 0xffffff,
		    MBUF_TRAIL32(m, -4) & 0xffffff,
		    MBUF_TRAIL32(m, -3) & 0xffffff);
		return;

	  case PDU_UD:
		sscop->funcs->verbose(sscop, sscop->aarg,
		    "%s UD pl=%u", dir, (v1 >> 30) & 0x3);
		return;

	  case PDU_MD:
		sscop->funcs->verbose(sscop, sscop->aarg,
		    "%s MD pl=%u", dir, (v1 >> 30) & 0x3);
		return;

	  case PDU_ERAK:
		sscop->funcs->verbose(sscop, sscop->aarg,
		    "%s ERAK n(mr)=%u", dir, v1 & 0xffffff);
		return;
	}
}
#endif


/*
 * Initialize state of variables
 */
static void
sscop_init(struct sscop *sscop)
{
	sscop->state = SSCOP_IDLE;

	sscop->vt_sq = 0;
	sscop->vr_sq = 0;
	sscop->clear_buffers = 1;

	sscop->ll_busy = 0;

	sscop->rxq = 0;
}

static void
sscop_clear(struct sscop *sscop)
{
	TIMER_STOP(sscop, cc);
	TIMER_STOP(sscop, ka);
	TIMER_STOP(sscop, nr);
	TIMER_STOP(sscop, idle);
	TIMER_STOP(sscop, poll);

	FREE_UU(uu_bgn);
	FREE_UU(uu_bgak);
	FREE_UU(uu_bgrej);
	FREE_UU(uu_end);
	FREE_UU(uu_rs);

	MSGQ_CLEAR(&sscop->xq);
	MSGQ_CLEAR(&sscop->uxq);
	MSGQ_CLEAR(&sscop->mxq);
	MSGQ_CLEAR(&sscop->xbuf);
	MSGQ_CLEAR(&sscop->rbuf);

	SIGQ_CLEAR(&sscop->sigs);
	SIGQ_CLEAR(&sscop->saved_sigs);
}


/*
 * Allocate instance memory, initialize the state of all variables.
 */
struct sscop *
sscop_create(void *a, const struct sscop_funcs *funcs)
{
	struct sscop *sscop;

	MEMZALLOC(sscop, struct sscop *, sizeof(struct sscop));
	if (sscop == NULL)
		return (NULL);

	if (a == NULL)
		sscop->aarg = sscop;
	else
		sscop->aarg = a;
	sscop->funcs = funcs;

	sscop->maxk = MAXK;
	sscop->maxj = MAXJ;
	sscop->maxcc = MAXCC;
	sscop->maxpd = MAXPD;
	sscop->maxstat = MAXSTAT;
	sscop->timercc = TIMERCC;
	sscop->timerka = TIMERKA;
	sscop->timernr = TIMERNR;
	sscop->timerpoll = TIMERPOLL;
	sscop->timeridle = TIMERIDLE;
	sscop->robustness = 0;
	sscop->poll_after_rex = 0;
	sscop->mr = MAXMR;

	TIMER_INIT(sscop, cc);
	TIMER_INIT(sscop, nr);
	TIMER_INIT(sscop, ka);
	TIMER_INIT(sscop, poll);
	TIMER_INIT(sscop, idle);

	MSGQ_INIT(&sscop->xq);
	MSGQ_INIT(&sscop->uxq);
	MSGQ_INIT(&sscop->mxq);
	MSGQ_INIT(&sscop->rbuf);
	MSGQ_INIT(&sscop->xbuf);

	SIGQ_INIT(&sscop->sigs);
	SIGQ_INIT(&sscop->saved_sigs);

	sscop_init(sscop);

	return (sscop);
}

/*
 * Free all resources in a sscop instance
 */
void
sscop_destroy(struct sscop *sscop)
{
	sscop_reset(sscop);

	MEMFREE(sscop);
}

/*
 * Reset the SSCOP instance.
 */
void
sscop_reset(struct sscop *sscop)
{
	sscop_clear(sscop);
	sscop_init(sscop);
}

void
sscop_getparam(const struct sscop *sscop, struct sscop_param *p)
{
	p->timer_cc = sscop->timercc;
	p->timer_poll = sscop->timerpoll;
	p->timer_keep_alive = sscop->timerka;
	p->timer_no_response = sscop->timernr;
	p->timer_idle = sscop->timeridle;
	p->maxk = sscop->maxk;
	p->maxj = sscop->maxj;
	p->maxcc = sscop->maxcc;
	p->maxpd = sscop->maxpd;
	p->maxstat = sscop->maxstat;
	p->mr = sscop->mr;
	p->flags = 0;
	if(sscop->robustness)
		p->flags |= SSCOP_ROBUST;
	if(sscop->poll_after_rex)
		p->flags |= SSCOP_POLLREX;
}

int
sscop_setparam(struct sscop *sscop, struct sscop_param *p, u_int *pmask)
{
	u_int mask = *pmask;

	/* can change only in idle state */
	if (sscop->state != SSCOP_IDLE)
		return (EISCONN);

	*pmask = 0;

	/*
	 * first check all parameters
	 */
	if ((mask & SSCOP_SET_TCC) && p->timer_cc == 0)
		*pmask |= SSCOP_SET_TCC;
	if ((mask & SSCOP_SET_TPOLL) && p->timer_poll == 0)
		*pmask |= SSCOP_SET_TPOLL;
	if ((mask & SSCOP_SET_TKA) && p->timer_keep_alive == 0)
		*pmask |= SSCOP_SET_TKA;
	if ((mask & SSCOP_SET_TNR) && p->timer_no_response == 0)
		*pmask |= SSCOP_SET_TNR;
	if ((mask & SSCOP_SET_TIDLE) && p->timer_idle == 0)
		*pmask |= SSCOP_SET_TIDLE;
	if ((mask & SSCOP_SET_MAXK) && p->maxk > MAXMAXK)
		*pmask |= SSCOP_SET_MAXK;
	if ((mask & SSCOP_SET_MAXJ) && p->maxj > MAXMAXJ)
		*pmask |= SSCOP_SET_MAXJ;
	if ((mask & SSCOP_SET_MAXCC) && p->maxcc > 255)
		*pmask |= SSCOP_SET_MAXCC;
	if ((mask & SSCOP_SET_MAXPD) && p->maxpd >= (1 << 24))
		*pmask |= SSCOP_SET_MAXPD;
	if ((mask & SSCOP_SET_MAXSTAT) && 
	    ((p->maxstat & 1) == 0 || p->maxstat == 1 || p->maxstat == 2 ||
	    p->maxstat * 4 > MAXMAXK - 8))
		*pmask |= SSCOP_SET_MAXSTAT;
	if ((mask & SSCOP_SET_MR) && p->mr >= (1 << 24) - 1)
		*pmask |= SSCOP_SET_MR;

	if (*pmask)
		return (EINVAL);


	/*
	 * now set it
	 */
	if (mask & SSCOP_SET_TCC)
		sscop->timercc = p->timer_cc;

	if (mask & SSCOP_SET_TPOLL)
		sscop->timerpoll = p->timer_poll;

	if (mask & SSCOP_SET_TKA)
		sscop->timerka = p->timer_keep_alive;

	if (mask & SSCOP_SET_TNR)
		sscop->timernr = p->timer_no_response;

	if (mask & SSCOP_SET_TIDLE)
		sscop->timeridle = p->timer_idle;

	if (mask & SSCOP_SET_MAXK)
		sscop->maxk = p->maxk;
	if (mask & SSCOP_SET_MAXJ)
		sscop->maxj = p->maxj;

	if (mask & SSCOP_SET_MAXCC)
		sscop->maxcc = p->maxcc;
	if (mask & SSCOP_SET_MAXPD)
		sscop->maxpd = p->maxpd;
	if (mask & SSCOP_SET_MAXSTAT)
		sscop->maxstat = p->maxstat;

	if (mask & SSCOP_SET_MR)
		sscop->mr = p->mr;

	if (mask & SSCOP_SET_ROBUST)
		sscop->robustness = ((p->flags & SSCOP_ROBUST) != 0);

	if (mask & SSCOP_SET_POLLREX)
		sscop->poll_after_rex = ((p->flags & SSCOP_POLLREX) != 0);

	return (0);
}

enum sscop_state
sscop_getstate(const struct sscop *sscop)
{
	return (sscop->state);
}


/************************************************************/
/*
 * EXTERNAL INPUT SIGNAL MAPPING
 */

/*
 * Map AA signal to SSCOP internal signal
 */
int
sscop_aasig(struct sscop *sscop, enum sscop_aasig sig,
    struct SSCOP_MBUF_T *m, u_int arg)
{
	struct sscop_msg *msg;

	if (sig >= sizeof(sscop_sigs)/sizeof(sscop_sigs[0])) {
		VERBOSE(sscop, SSCOP_DBG_INSIG, (sscop, sscop->aarg,
		    "AA-Signal %u - bad signal", sig));
		MBUF_FREE(m);
		return (EINVAL);
	}
	VERBOSE(sscop, SSCOP_DBG_INSIG, (sscop, sscop->aarg,
	    "AA-Signal %s in state %s with%s message",
	    sscop_sigs[sig], states[sscop->state], m ? "" : "out"));

	MSG_ALLOC(msg);
	if (msg == NULL) {
		FAILURE("sscop: cannot allocate aasig");
		MBUF_FREE(m);
		return (ENOMEM);
	}

	switch(sig) {

	  case SSCOP_ESTABLISH_request:
		msg->m = m;
		msg->rexmit = arg;
		sscop_signal(sscop, SIG_ESTAB_REQ, msg);
		break;

	  case SSCOP_ESTABLISH_response:
		msg->m = m;
		msg->rexmit = arg;
		sscop_signal(sscop, SIG_ESTAB_RESP, msg);
		break;

	  case SSCOP_RELEASE_request:
		msg->m = m;
		sscop_signal(sscop, SIG_RELEASE_REQ, msg);
		break;

	  case SSCOP_DATA_request:
		msg->m = m;
		sscop_signal(sscop, SIG_USER_DATA, msg);
		break;

	  case SSCOP_UDATA_request:
		msg->m = m;
		sscop_signal(sscop, SIG_UDATA, msg);
		break;

	  case SSCOP_RECOVER_response:
		MBUF_FREE(m);
		MSG_FREE(msg);
		sscop_signal(sscop, SIG_RECOVER, NULL);
		break;

	  case SSCOP_RESYNC_request:
		msg->m = m;
		sscop_signal(sscop, SIG_SYNC_REQ, msg);
		break;

	  case SSCOP_RESYNC_response:
		MBUF_FREE(m);
		MSG_FREE(msg);
		sscop_signal(sscop, SIG_SYNC_RESP, NULL);
		break;

	  case SSCOP_RETRIEVE_request:
		MBUF_FREE(m);
		msg->rexmit = arg;
		sscop_signal(sscop, SIG_RETRIEVE, msg);
		break;

	  case SSCOP_ESTABLISH_indication:
	  case SSCOP_ESTABLISH_confirm:
	  case SSCOP_RELEASE_indication:
	  case SSCOP_RELEASE_confirm:
	  case SSCOP_DATA_indication:
	  case SSCOP_UDATA_indication:
	  case SSCOP_RECOVER_indication:
	  case SSCOP_RESYNC_indication:
	  case SSCOP_RESYNC_confirm:
	  case SSCOP_RETRIEVE_indication:
	  case SSCOP_RETRIEVE_COMPL_indication:
		MBUF_FREE(m);
		MSG_FREE(msg);
		return EINVAL;
	}

	return 0;
}

/*
 * Signal from layer management.
 */
int
sscop_maasig(struct sscop *sscop, enum sscop_maasig sig, struct SSCOP_MBUF_T *m)
{
	struct sscop_msg *msg;

	if (sig >= sizeof(sscop_msigs)/sizeof(sscop_msigs[0])) {
		VERBOSE(sscop, SSCOP_DBG_INSIG, (sscop, sscop->aarg,
		    "MAA-Signal %u - bad signal", sig));
		MBUF_FREE(m);
		return (EINVAL);
	}
	VERBOSE(sscop, SSCOP_DBG_INSIG, (sscop, sscop->aarg,
	    "MAA-Signal %s in state %s with%s message",
	    sscop_msigs[sig], states[sscop->state], m ? "" : "out"));

	MSG_ALLOC(msg);
	if (msg == NULL) {
		FAILURE("sscop: cannot allocate maasig");
		MBUF_FREE(m);
		return (ENOMEM);
	}

	switch (sig) {

	  case SSCOP_MDATA_request:
		msg->m = m;
		sscop_signal(sscop, SIG_MDATA, msg);
		break;

	  case SSCOP_MDATA_indication:
	  case SSCOP_MERROR_indication:
		MBUF_FREE(m);
		MSG_FREE(msg);
		return (EINVAL);
	}
	return (0);
}

/*
 * Map PDU to SSCOP signal.
 */
void
sscop_input(struct sscop *sscop, struct SSCOP_MBUF_T *m)
{
	struct sscop_msg *msg;
	union pdu pdu;
	u_int size;

	MSG_ALLOC(msg);
	if(msg == NULL) {
		FAILURE("sscop: cannot allocate in pdu msg");
		MBUF_FREE(m);
		return;
	}

	msg->m = m;
	msg->rexmit = 0;

	size = MBUF_LEN(m);

	if(size % 4 != 0 || size < 4)
		goto err;

	pdu.sscop_null = MBUF_TRAIL32(m, -1);

	VERBOSE(sscop, SSCOP_DBG_PDU, (sscop, sscop->aarg,
	    "got %s, size=%u", pdus[pdu.sscop_type], size));

#ifdef SSCOP_DEBUG
#define ENSURE(C,F)	if(!(C)) { VERBOSE(sscop, SSCOP_DBG_PDU, F); goto err; }
#else
#define ENSURE(C,F)	if(!(C)) goto err
#endif

#ifdef SSCOP_DEBUG
	if (ISVERBOSE(sscop, SSCOP_DBG_PDU))
		sscop_dump_pdu(sscop, "rx", m);
#endif

	switch(pdu.sscop_type) {

          default:
		ENSURE(0, (sscop, sscop->aarg,
		    "Bad PDU type %u", pdu.sscop_type));
		break;

	  case PDU_BGN:
		ENSURE(size >= 8U, (sscop, sscop->aarg,
			"PDU_BGN size=%u", size));
		ENSURE(size >= 8U + pdu.sscop_pl, (sscop, sscop->aarg,
			"PDU_BGN size=%u pl=%u", size, pdu.sscop_pl));
		ENSURE(size <= 8U + sscop->maxj, (sscop, sscop->aarg,
			"PDU_BGN size=%u", size));
		sscop_signal(sscop, SIG_BGN, msg);
		break;

	  case PDU_BGAK:
		ENSURE(size >= 8U, (sscop, sscop->aarg,
			"PDU_BGAK size=%u", size));
		ENSURE(size >= 8U + pdu.sscop_pl, (sscop, sscop->aarg,
			"PDU_BGAK size=%u pl=%u", size, pdu.sscop_pl));
		ENSURE(size <= 8U + sscop->maxj, (sscop, sscop->aarg,
			"PDU_BGAK size=%u", size));
		sscop_signal(sscop, SIG_BGAK, msg);
		break;

	  case PDU_END:
		ENSURE(size >= 8U, (sscop, sscop->aarg,
			"PDU_END size=%u", size));
		ENSURE(size >= 8U + pdu.sscop_pl, (sscop, sscop->aarg,
			"PDU_END size=%u pl=%u", size, pdu.sscop_pl));
		ENSURE(size <= 8U + sscop->maxj, (sscop, sscop->aarg,
			"PDU_END size=%u", size));
		sscop_signal(sscop, SIG_END, msg);
		break;

	  case PDU_ENDAK:
		ENSURE(size == 8U, (sscop, sscop->aarg,
			"PDU_ENDAK size=%u", size));
		sscop_signal(sscop, SIG_ENDAK, msg);
		break;

	  case PDU_BGREJ:
		ENSURE(size >= 8U, (sscop, sscop->aarg,
			"PDU_BGREJ size=%u", size));
		ENSURE(size >= 8U + pdu.sscop_pl, (sscop, sscop->aarg,
			"PDU_BGREJ size=%u pl=%u", size, pdu.sscop_pl));
		ENSURE(size <= 8U + sscop->maxj, (sscop, sscop->aarg,
			"PDU_BGREJ size=%u", size));
		sscop_signal(sscop, SIG_BGREJ, msg);
		break;

	  case PDU_SD:
		ENSURE(size >= 4U + pdu.sscop_pl, (sscop, sscop->aarg,
			"PDU_SD size=%u pl=%u", size, pdu.sscop_pl));
		ENSURE(size <= 4U + sscop->maxk, (sscop, sscop->aarg,
			"PDU_SD size=%u", size));
		sscop_signal(sscop, SIG_SD, msg);
		break;

	  case PDU_UD:
		ENSURE(size >= 4U + pdu.sscop_pl, (sscop, sscop->aarg,
			"PDU_UD size=%u pl=%u", size, pdu.sscop_pl));
		ENSURE(size <= 4U + sscop->maxk, (sscop, sscop->aarg,
			"PDU_UD size=%u", size));
		sscop_signal(sscop, SIG_UD, msg);
		break;

	  case PDU_MD:
		ENSURE(size >= 4U + pdu.sscop_pl, (sscop, sscop->aarg,
			"PDU_MD size=%u pl=%u", size, pdu.sscop_pl));
		ENSURE(size <= 4U + sscop->maxk, (sscop, sscop->aarg,
			"PDU_MD size=%u", size));
		sscop_signal(sscop, SIG_MD, msg);
		break;

	  case PDU_POLL:
		ENSURE(size == 8U, (sscop, sscop->aarg,
			"PDU_POLL size=%u", size));
		sscop_signal(sscop, SIG_POLL, msg);
		break;

	  case PDU_STAT:
		ENSURE(size >= 12U, (sscop, sscop->aarg,
			"PDU_STAT size=%u", size));
		ENSURE(size <= 12U + 4 * sscop->maxstat, (sscop, sscop->aarg,
			"PDU_STAT size=%u", size));
		sscop_signal(sscop, SIG_STAT, msg);
		break;

	  case PDU_RS:
		ENSURE(size >= 8U, (sscop, sscop->aarg,
			"PDU_RS size=%u", size));
		ENSURE(size >= 8U + pdu.sscop_pl, (sscop, sscop->aarg,
			"PDU_RS size=%u pl=%u", size, pdu.sscop_pl));
		ENSURE(size <= 8U + sscop->maxj, (sscop, sscop->aarg,
			"PDU_RS size=%u", size));
		sscop_signal(sscop, SIG_RS, msg);
		break;

	  case PDU_RSAK:
		ENSURE(size == 8U, (sscop, sscop->aarg,
			"PDU_RSAK size=%u", size));
		sscop_signal(sscop, SIG_RSAK, msg);
		break;

	  case PDU_ER:
		ENSURE(size == 8U, (sscop, sscop->aarg,
			"PDU_ER size=%u", size));
		sscop_signal(sscop, SIG_ER, msg);
		break;

	  case PDU_ERAK:
		ENSURE(size == 8U, (sscop, sscop->aarg,
			"PDU_ERAK size=%u", size));
		sscop_signal(sscop, SIG_ERAK, msg);
		break;

	  case PDU_USTAT:
		ENSURE(size == 16U, (sscop, sscop->aarg,
			"PDU_ERAK size=%u", size));
		sscop_signal(sscop, SIG_USTAT, msg);
		break;
	}
#undef ENSURE
	return;

  err:
	MAAL_ERROR(sscop, 'U', 0);
	SSCOP_MSG_FREE(msg);
}

/************************************************************/
/*
 * UTILITIES
 */

/*
 * Move the receiver window by N packets
 */
u_int
sscop_window(struct sscop *sscop, u_int n)
{
	sscop->vr_mr += n;
	return (SEQNO_DIFF(sscop->vr_mr, sscop->vr_r));
}

/*
 * Lower layer busy handling
 */
u_int
sscop_setbusy(struct sscop *sscop, int busy)
{
	u_int old = sscop->ll_busy;

	if (busy > 0)
		sscop->ll_busy = 1;
	else if (busy == 0) {
		sscop->ll_busy = 0;
		if(old)
			handle_sigs(sscop);
	}

	return (old);
}

const char *
sscop_signame(enum sscop_aasig sig)
{
	static char str[40];

	if (sig >= sizeof(sscop_sigs)/sizeof(sscop_sigs[0])) {
		sprintf(str, "BAD SSCOP_AASIG %u", sig);
		return (str);
	} else {
		return (sscop_sigs[sig]);
	}
}

const char *
sscop_msigname(enum sscop_maasig sig)
{
	static char str[40];

	if (sig >= sizeof(sscop_msigs)/sizeof(sscop_msigs[0])) {
		sprintf(str, "BAD SSCOP_MAASIG %u", sig);
		return (str);
	} else {
		return (sscop_msigs[sig]);
	}
}

const char *
sscop_statename(enum sscop_state s)
{
	static char str[40];

	if (s >= sizeof(states)/sizeof(states[0])) {
		sprintf(str, "BAD SSCOP_STATE %u", s);
		return (str);
	} else {
		return (states[s]);
	}
}


/************************************************************/
/*
 * MACROS
 */

/*
 * p 75: release buffers
 */
static void
m_release_buffers(struct sscop *sscop)
{
	MSGQ_CLEAR(&sscop->xq);
	MSGQ_CLEAR(&sscop->xbuf);
	sscop->rxq = 0;
	MSGQ_CLEAR(&sscop->rbuf);
}

/*
 * P 75: Prepare retrival
 */
static void
m_prepare_retrieval(struct sscop *sscop)
{
	struct sscop_msg *msg;

	if (sscop->clear_buffers) {
		MSGQ_CLEAR(&sscop->xq);
		MSGQ_CLEAR(&sscop->xbuf);
	}
	MSGQ_FOREACH(msg, &sscop->xbuf)
		msg->rexmit = 0;
	sscop->rxq = 0;

	MSGQ_CLEAR(&sscop->rbuf);
}

/*
 * P 75: Prepare retrival
 */
static void
m_prepare_recovery(struct sscop *sscop)
{
	struct sscop_msg *msg;

	if(sscop->clear_buffers) {
		MSGQ_CLEAR(&sscop->xq);
		MSGQ_CLEAR(&sscop->xbuf);
	}
	MSGQ_FOREACH(msg, &sscop->xbuf)
		msg->rexmit = 0;
	sscop->rxq = 0;
}


/*
 * P 75: Clear transmitter
 */
static void
m_clear_transmitter(struct sscop *sscop)
{
	if(!sscop->clear_buffers) {
		MSGQ_CLEAR(&sscop->xq);
		MSGQ_CLEAR(&sscop->xbuf);
	}
}


/*
 * p 75: Deliver data
 * Freeing the message is the responibility of the handler function.
 */
static void
m_deliver_data(struct sscop *sscop)
{
	struct sscop_msg *msg;
	u_int sn;

	if ((msg = MSGQ_GET(&sscop->rbuf)) == NULL)
		return;

	if (sscop->clear_buffers) {
		MSGQ_CLEAR(&sscop->rbuf);
		return;
	}

	sn = msg->seqno + 1;
	AAL_DATA(sscop, SSCOP_DATA_indication, msg->m, msg->seqno);
	MSG_FREE(msg);

	while ((msg = MSGQ_GET(&sscop->rbuf)) != NULL) {
		ASSERT(msg->seqno == sn);
		if (++sn == SSCOP_MAXSEQNO)
			sn = 0;
		AAL_DATA(sscop, SSCOP_DATA_indication, msg->m, msg->seqno);
		MSG_FREE(msg);
	}
}

/*
 * P 75: Initialize state variables
 */
static void
m_initialize_state(struct sscop *sscop)
{
	sscop->vt_s = 0;
	sscop->vt_ps = 0;
	sscop->vt_a = 0;

	sscop->vt_pa = 1;
	sscop->vt_pd = 0;
	sscop->credit = 1;

	sscop->vr_r = 0;
	sscop->vr_h = 0;
}

/*
 * p 76: Data retrieval
 */
static void
m_data_retrieval(struct sscop *sscop, u_int rn)
{
	struct sscop_msg *s;

	if (rn != SSCOP_RETRIEVE_UNKNOWN) {
		if(rn >= SSCOP_RETRIEVE_TOTAL)
			rn = sscop->vt_a;
		else
			rn++;
		while(rn >= sscop->vt_a && rn < sscop->vt_s) {
			if(rn == SSCOP_MAXSEQNO) rn = 0;
			if((s = QFIND(&sscop->xbuf, rn)) != NULL) {
				MSGQ_REMOVE(&sscop->xbuf, s);
				AAL_DATA(sscop, SSCOP_RETRIEVE_indication,
					s->m, 0);
				MSG_FREE(s);
			}
			rn++;
		}
	}

	while((s = MSGQ_GET(&sscop->xq)) != NULL) {
		AAL_DATA(sscop, SSCOP_RETRIEVE_indication, s->m, 0);
		MSG_FREE(s);
	}
	AAL_SIG(sscop, SSCOP_RETRIEVE_COMPL_indication);
}

/*
 * P 76: Detect retransmission. PDU type must already be stripped.
 */
static int
m_detect_retransmission(struct sscop *sscop, struct sscop_msg *msg)
{
	union bgn bgn;

	bgn.sscop_null = MBUF_TRAIL32(msg->m, -1);

	if (sscop->vr_sq == bgn.sscop_bgns)
		return (1);

	sscop->vr_sq = bgn.sscop_bgns;
	return (0);
}

/*
 * P 76: Set POLL timer
 */
static void
m_set_poll_timer(struct sscop *sscop)
{
	if(MSGQ_EMPTY(&sscop->xq) && sscop->vt_s == sscop->vt_a)
		TIMER_RESTART(sscop, ka);
	else
		TIMER_RESTART(sscop, poll);
}

/*
 * P 77: Reset data transfer timers
 */
static void
m_reset_data_xfer_timers(struct sscop *sscop)
{
	TIMER_STOP(sscop, ka);
	TIMER_STOP(sscop, nr);
	TIMER_STOP(sscop, idle);
	TIMER_STOP(sscop, poll);
}

/*
 * P 77: Set data transfer timers
 */
static void
m_set_data_xfer_timers(struct sscop *sscop)
{
	TIMER_RESTART(sscop, poll);
	TIMER_RESTART(sscop, nr);
}

/*
 * P 77: Initialize VR(MR)
 */
static void
m_initialize_mr(struct sscop *sscop)
{
	sscop->vr_mr = sscop->mr;
}

/************************************************************/
/*
 * CONDITIONS
 */
static int
c_ready_pduq(struct sscop *sscop)
{
	if (!sscop->ll_busy &&
	    (sscop->rxq != 0 ||
	    sscop->vt_s < sscop->vt_ms ||
	    TIMER_ISACT(sscop, idle)))
		return (1);
	return (0);
}

/************************************************************/
/*
 * SEND PDUS
 */

/*
 * Send BG PDU.
 */
static void
send_bgn(struct sscop *sscop, struct SSCOP_MBUF_T *uu)
{
	union pdu pdu;
	union bgn bgn;
	struct SSCOP_MBUF_T *m;

	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_BGN;
	pdu.sscop_ns = sscop->vr_mr;

	bgn.sscop_null = 0;
	bgn.sscop_bgns = sscop->vt_sq;

	if(uu) {
		if ((m = MBUF_DUP(uu)) == NULL) {
			FAILURE("sscop: cannot allocate BGN");
			return;
		}
		pdu.sscop_pl += MBUF_PAD4(m);
	} else {
		if ((m = MBUF_ALLOC(8)) == NULL) {
			FAILURE("sscop: cannot allocate BGN");
			return;
		}
	}

	MBUF_APPEND32(m, bgn.sscop_null);
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send BGREJ PDU.
 */
static void
send_bgrej(struct sscop *sscop, struct SSCOP_MBUF_T *uu)
{
	union pdu pdu;
	union bgn bgn;
	struct SSCOP_MBUF_T *m;

	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_BGREJ;
	bgn.sscop_null = 0;

	if(uu) {
		if((m = MBUF_DUP(uu)) == NULL) {
			FAILURE("sscop: cannot allocate BGREJ");
			return;
		}
		pdu.sscop_pl += MBUF_PAD4(m);
	} else {
		if((m = MBUF_ALLOC(8)) == NULL) {
			FAILURE("sscop: cannot allocate BGREJ");
			return;
		}
	}

	MBUF_APPEND32(m, bgn.sscop_null);
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send BGAK PDU.
 */
static void
send_bgak(struct sscop *sscop, struct SSCOP_MBUF_T *uu)
{
	union pdu pdu;
	union bgn bgn;
	struct SSCOP_MBUF_T *m;

	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_BGAK;
	pdu.sscop_ns = sscop->vr_mr;
	bgn.sscop_null = 0;

	if(uu) {
		if((m = MBUF_DUP(uu)) == NULL) {
			FAILURE("sscop: cannot allocate BGAK");
			return;
		}
		pdu.sscop_pl += MBUF_PAD4(m);
	} else {
		if((m = MBUF_ALLOC(8)) == NULL) {
			FAILURE("sscop: cannot allocate BGAK");
			return;
		}
	}

	MBUF_APPEND32(m, bgn.sscop_null);
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send SD PDU. The function makes a duplicate of the message.
 */
static void
send_sd(struct sscop *sscop, struct SSCOP_MBUF_T *m, u_int seqno)
{
	union pdu pdu;

	if((m = MBUF_DUP(m)) == NULL) {
		FAILURE("sscop: cannot allocate SD");
		return;
	}

	pdu.sscop_null = 0;
	pdu.sscop_pl = 0;
	pdu.sscop_type = PDU_SD;
	pdu.sscop_ns = seqno;

	pdu.sscop_pl += MBUF_PAD4(m);

	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send a UD PDU. The caller must free the sscop msg part.
 */
static void
send_ud(struct sscop *sscop, struct SSCOP_MBUF_T *m)
{
	union pdu pdu;

	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_UD;

	pdu.sscop_pl += MBUF_PAD4(m);

	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send a MD PDU. The caller must free the sscop msg part.
 */
static void
send_md(struct sscop *sscop, struct SSCOP_MBUF_T *m)
{
	union pdu pdu;

	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_MD;

	pdu.sscop_pl += MBUF_PAD4(m);

	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send END PDU.
 */
static void
send_end(struct sscop *sscop, int src, struct SSCOP_MBUF_T *uu)
{
	union pdu pdu;
	struct SSCOP_MBUF_T *m;

	sscop->last_end_src = src;

	pdu.sscop_null = 0;
	pdu.sscop_s = src;
	pdu.sscop_type = PDU_END;

	if(uu) {
		if((m = MBUF_DUP(uu)) == NULL) {
			FAILURE("sscop: cannot allocate END");
			return;
		}
		pdu.sscop_pl += MBUF_PAD4(m);
	} else {
		if((m = MBUF_ALLOC(8)) == NULL) {
			FAILURE("sscop: cannot allocate END");
			return;
		}
	}

	MBUF_APPEND32(m, 0);
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send USTAT PDU. List must be terminated by -1.
 */
static void
send_ustat(struct sscop *sscop, ...)
{
	va_list ap;
	int f;
	u_int n;
	union pdu pdu;
	union seqno seqno;
	struct SSCOP_MBUF_T *m;

	va_start(ap, sscop);
	n = 0;
	while((f = va_arg(ap, int)) >= 0)
		n++;
	va_end(ap);

	if((m = MBUF_ALLOC(n * 4 + 8)) == NULL) {
		FAILURE("sscop: cannot allocate USTAT");
		return;
	}

	va_start(ap, sscop);
	while((f = va_arg(ap, int)) >= 0) {
		seqno.sscop_null = 0;
		seqno.sscop_n = f;
		MBUF_APPEND32(m, seqno.sscop_null);
	}
	va_end(ap);

	seqno.sscop_null = 0;
	seqno.sscop_n = sscop->vr_mr;
	MBUF_APPEND32(m, seqno.sscop_null);

	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_USTAT;
	pdu.sscop_ns = sscop->vr_r;
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send ER PDU.
 */
static void
send_er(struct sscop *sscop)
{
	union pdu pdu;
	union bgn bgn;
	struct SSCOP_MBUF_T *m;

	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_ER;
	pdu.sscop_ns = sscop->vr_mr;

	bgn.sscop_null = 0;
	bgn.sscop_bgns = sscop->vt_sq;

	if((m = MBUF_ALLOC(8)) == NULL) {
		FAILURE("sscop: cannot allocate ER");
		return;
	}
	MBUF_APPEND32(m, bgn.sscop_null);
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send POLL PDU.
 */
static void
send_poll(struct sscop *sscop)
{
	union pdu pdu;
	union seqno seqno;
	struct SSCOP_MBUF_T *m;

	seqno.sscop_null = 0;
	seqno.sscop_n = sscop->vt_ps;

	pdu.sscop_null = 0;
	pdu.sscop_ns = sscop->vt_s;
	pdu.sscop_type = PDU_POLL;

	if((m = MBUF_ALLOC(8)) == NULL) {
		FAILURE("sscop: cannot allocate POLL");
		return;
	}
	MBUF_APPEND32(m, seqno.sscop_null);
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send STAT PDU. List is already in buffer.
 */
static void
send_stat(struct sscop *sscop, u_int nps, struct SSCOP_MBUF_T *m)
{
	union pdu pdu;
	union seqno seqno;

	seqno.sscop_null = 0;
	seqno.sscop_n = nps;
	MBUF_APPEND32(m, seqno.sscop_null);

	seqno.sscop_null = 0;
	seqno.sscop_n = sscop->vr_mr;
	MBUF_APPEND32(m, seqno.sscop_null);

	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_STAT;
	pdu.sscop_ns = sscop->vr_r;
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send ENDAK PDU.
 */
static void
send_endak(struct sscop *sscop)
{
	union pdu pdu;
	union seqno seqno;
	struct SSCOP_MBUF_T *m;

	seqno.sscop_null = 0;
	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_ENDAK;

	if((m = MBUF_ALLOC(8)) == NULL) {
		FAILURE("sscop: cannot allocate ENDAK");
		return;
	}
	MBUF_APPEND32(m, seqno.sscop_null);
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send ERAK PDU.
 */
static void
send_erak(struct sscop *sscop)
{
	union pdu pdu;
	union seqno seqno;
	struct SSCOP_MBUF_T *m;

	seqno.sscop_null = 0;
	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_ERAK;
	pdu.sscop_ns = sscop->vr_mr;

	if((m = MBUF_ALLOC(8)) == NULL) {
		FAILURE("sscop: cannot allocate ERAK");
		return;
	}
	MBUF_APPEND32(m, seqno.sscop_null);
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send RS PDU
 */
static void
send_rs(struct sscop *sscop, int resend, struct SSCOP_MBUF_T *uu)
{
	union pdu pdu;
	union bgn bgn;
	struct SSCOP_MBUF_T *m;

	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_RS;
	pdu.sscop_ns = resend ? sscop->rs_mr : sscop->vr_mr;

	bgn.sscop_null = 0;
	bgn.sscop_bgns = resend ? sscop->rs_sq : sscop->vt_sq;

	sscop->rs_mr = pdu.sscop_ns;
	sscop->rs_sq = bgn.sscop_bgns;

	if(uu) {
		if((m = MBUF_DUP(uu)) == NULL) {
			FAILURE("sscop: cannot allocate RS");
			return;
		}
		pdu.sscop_pl += MBUF_PAD4(m);
	} else {
		if((m = MBUF_ALLOC(8)) == NULL) {
			FAILURE("sscop: cannot allocate RS");
			return;
		}
	}

	MBUF_APPEND32(m, bgn.sscop_null);
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/*
 * Send RSAK pdu
 */
static void
send_rsak(struct sscop *sscop)
{
	union pdu pdu;
	union seqno seqno;
	struct SSCOP_MBUF_T *m;

	seqno.sscop_null = 0;
	pdu.sscop_null = 0;
	pdu.sscop_type = PDU_RSAK;
	pdu.sscop_ns = sscop->vr_mr;

	if((m = MBUF_ALLOC(8)) == NULL) {
		FAILURE("sscop: cannot allocate RSAK");
		return;
	}

	MBUF_APPEND32(m, seqno.sscop_null);
	MBUF_APPEND32(m, pdu.sscop_null);

	AAL_SEND(sscop, m);
}

/************************************************************/
/*
 * P 31; IDLE && AA-ESTABLISH-request
 *	arg is UU data (opt).
 */
static void
sscop_idle_establish_req(struct sscop *sscop, struct sscop_msg *uu)
{
	u_int br = uu->rexmit;

	SET_UU(uu_bgn, uu);

	m_clear_transmitter(sscop);

	sscop->clear_buffers = br;

	sscop->vt_cc = 1;
	sscop->vt_sq++;

	m_initialize_mr(sscop);

	send_bgn(sscop, sscop->uu_bgn);

	TIMER_RESTART(sscop, cc);

	sscop_set_state(sscop, SSCOP_OUT_PEND);
}

/*
 * P 31: IDLE && BGN PDU
 *	arg is the received PDU (freed).
 */
static void
sscop_idle_bgn(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;
	union bgn bgn;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(sscop->robustness) {
		bgn.sscop_null = MBUF_STRIP32(msg->m);
		sscop->vr_sq = bgn.sscop_bgns;
	} else {
		if(m_detect_retransmission(sscop, msg)) {
			send_bgrej(sscop, sscop->uu_bgrej);
			SSCOP_MSG_FREE(msg);
			return;
		}
		(void)MBUF_STRIP32(msg->m);
	}

	sscop->vt_ms = pdu.sscop_ns;
	sscop_set_state(sscop, SSCOP_IN_PEND);

	AAL_UU_SIGNAL(sscop, SSCOP_ESTABLISH_indication, msg, pdu.sscop_pl, 0);
}

/*
 * p 31: IDLE && ENDAK PDU
 * p 34: OUT_PEND && ENDAK PDU
 * p 34: OUT_PEND && SD PDU
 * p 34: OUT_PEND && ERAK PDU
 * p 34: OUT_PEND && END PDU
 * p 34: OUT_PEND && STAT PDU
 * p 34: OUT_PEND && USTAT PDU
 * p 34: OUT_PEND && POLL PDU
 * p 36: OUT_PEND && RS PDU
 * p 36: OUT_PEND && RSAK PDU
 * p 40: OUTGOING_DISCONNECT_PENDING && SD PDU
 * p 40: OUTGOING_DISCONNECT_PENDING && BGAK PDU
 * p 40: OUTGOING_DISCONNECT_PENDING && POLL PDU
 * p 40: OUTGOING_DISCONNECT_PENDING && STAT PDU
 * p 40: OUTGOING_DISCONNECT_PENDING && USTAT PDU
 * p 41: OUTGOING_DISCONNECT_PENDING && ERAK PDU
 * p 42: OUTGOING_DISCONNECT_PENDING && ER PDU
 * p 42: OUTGOING_DISCONNECT_PENDING && RS PDU
 * p 42: OUTGOING_DISCONNECT_PENDING && RSAK PDU
 * p 43: OUTGOING_RESYNC && ER PDU
 * p 43: OUTGOING_RESYNC && POLL PDU
 * p 44: OUTGOING_RESYNC && STAT PDU
 * p 44: OUTGOING_RESYNC && USTAT PDU
 * p 45: OUTGOING_RESYNC && BGAK PDU
 * p 45: OUTGOING_RESYNC && SD PDU
 * p 45: OUTGOING_RESYNC && ERAK PDU
 * P 60: READY && BGAK PDU
 * P 60: READY && ERAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_ignore_pdu(struct sscop *sscop __unused, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
}

/*
 * p 31: IDLE && END PDU
 *	arg is pdu (freed).
 */
static void
sscop_idle_end(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	send_endak(sscop);
}

/*
 * p 31: IDLE && ER PDU
 *	arg is pdu (freed).
 */
static void
sscop_idle_er(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'L', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
}

/*
 * p 31: IDLE && BGREJ PDU
 *	arg is pdu (freed).
 */
static void
sscop_idle_bgrej(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'D', 0);
	FREE_UU(uu_end);
}

/*
 * p 32: IDLE && POLL PDU
 *	arg is pdu (freed).
 */
static void
sscop_idle_poll(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'G', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
}

/*
 * p 32: IDLE && SD PDU
 *	arg is pdu (freed).
 */
static void
sscop_idle_sd(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'A', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
}

/*
 * p 32: IDLE && BGAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_idle_bgak(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'C', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
}

/*
 * p 32: IDLE && ERAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_idle_erak(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'M', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
}

/*
 * p 32: IDLE && STAT PDU
 *	arg is pdu (freed).
 */
static void
sscop_idle_stat(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'H', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
}

/*
 * p 32: IDLE && USTAT PDU
 *	arg is pdu (freed).
 */
static void
sscop_idle_ustat(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'I', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
}

/*
 * p 33: IDLE & RS PDU
 *	arg is pdu (freed).
 */
static void
sscop_idle_rs(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'J', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
}

/*
 * p 33: IDLE & RSAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_idle_rsak(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'K', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
}

/*
 * p 33: IDLE && PDU_Q
 * p XX: OUTPEND && PDU_Q
 * p 39: IN_PEND && PDU_Q
 * p 45: OUT_RESYNC_PEND && PDU_Q
 * p 48: IN_RESYNC_PEND && PDU_Q
 *	no arg
 */
static void
sscop_flush_pduq(struct sscop *sscop __unused, struct sscop_msg *unused __unused)
{
#if 0
	MSGQ_CLEAR(&sscop->xq);
#endif
}

/*
 * p 34: OUT_PEND && BGAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_outpend_bgak(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	(void)MBUF_STRIP32(msg->m);

	TIMER_STOP(sscop, cc);
	sscop->vt_ms = pdu.sscop_ns;

	AAL_UU_SIGNAL(sscop, SSCOP_ESTABLISH_confirm, msg, pdu.sscop_pl, 0);

	m_initialize_state(sscop);
	m_set_data_xfer_timers(sscop);

	sscop_set_state(sscop, SSCOP_READY);
}

/*
 * P 34: OUT_PEND && BGREJ PDU
 */
static void
sscop_outpend_bgrej(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	(void)MBUF_STRIP32(msg->m);

	TIMER_STOP(sscop, cc);

	AAL_UU_SIGNAL(sscop, SSCOP_RELEASE_indication, msg, pdu.sscop_pl, 0);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * P 35: OUT_PEND && TIMER_CC expiry
 *	no arg
 */
static void
sscop_outpend_tcc(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	if(sscop->vt_cc >= sscop->maxcc) {
		MAAL_ERROR(sscop, 'O', 0);
		FREE_UU(uu_end);
		send_end(sscop, 1, NULL);

		AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);

		sscop_set_state(sscop, SSCOP_IDLE);
	} else {
		sscop->vt_cc++;
		send_bgn(sscop, sscop->uu_bgn);
		TIMER_RESTART(sscop, cc);
	}
}

/*
 * P 35: OUT_PEND && RELEASE_REQ
 *	arg is UU
 */
static void
sscop_outpend_release_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_end, uu);

	TIMER_STOP(sscop, cc);
	sscop->vt_cc = 1;
	send_end(sscop, 0, sscop->uu_end);
	TIMER_RESTART(sscop, cc);

	sscop_set_state(sscop, SSCOP_OUT_DIS_PEND);
}

/*
 * P 36: OUT_PEND && BGN PDU
 *	arg is the received PDU (freed).
 */
static void
sscop_outpend_bgn(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		SSCOP_MSG_FREE(msg);
		return;
	}
	(void)MBUF_STRIP32(msg->m);

	TIMER_STOP(sscop, cc);

	sscop->vt_ms = pdu.sscop_ns;

	m_initialize_mr(sscop);

	send_bgak(sscop, sscop->uu_bgak);
 
	AAL_UU_SIGNAL(sscop, SSCOP_ESTABLISH_confirm, msg, pdu.sscop_pl, 0);

	m_initialize_state(sscop);

	m_set_data_xfer_timers(sscop);

	sscop_set_state(sscop, SSCOP_READY);
}

/*
 * p 37: IN_PEND && AA-ESTABLISH.response
 *	arg is UU
 */
static void
sscop_inpend_establish_resp(struct sscop *sscop, struct sscop_msg *uu)
{
	u_int br = uu->rexmit;

	SET_UU(uu_bgak, uu);

	m_clear_transmitter(sscop);
	sscop->clear_buffers = br;
	m_initialize_mr(sscop);
	send_bgak(sscop, sscop->uu_bgak);
	m_initialize_state(sscop);
	m_set_data_xfer_timers(sscop);

	sscop_set_state(sscop, SSCOP_READY);
}

/*
 * p 37: IN_PEND && AA-RELEASE.request
 *	arg is uu.
 */
static void
sscop_inpend_release_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_bgrej, uu);

	send_bgrej(sscop, sscop->uu_bgrej);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 37: IN_PEND && BGN PDU
 *	arg is pdu. (freed)
 */
static void
sscop_inpend_bgn(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		SSCOP_MSG_FREE(msg);
		return;
	}
	(void)MBUF_STRIP32(msg->m);

	sscop->vt_ms = pdu.sscop_ns;

	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 0);
	AAL_UU_SIGNAL(sscop, SSCOP_ESTABLISH_indication, msg, pdu.sscop_pl, 0);
}

/*
 * p 37: IN_PEND && ER PDU
 *	arg is pdu (freed).
 */
static void
sscop_inpend_er(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'L', 0);
	SSCOP_MSG_FREE(msg);
}

/*
 * p 37: IN_PEND && ENDAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_inpend_endak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'F', 0);

	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);

	sscop_set_state(sscop, SSCOP_IDLE);

	SSCOP_MSG_FREE(msg);
}

/*
 * p 38: IN_PEND && BGAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_inpend_bgak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'C', 0);

	SSCOP_MSG_FREE(msg);
}

/*
 * p 38: IN_PEND && BGREJ PDU
 *	arg is pdu (freed).
 */
static void
sscop_inpend_bgrej(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'D', 0);

	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);

	SSCOP_MSG_FREE(msg);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 38: IN_PEND && SD PDU
 *	arg is pdu (freed).
 */
static void
sscop_inpend_sd(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'A', 0);

	SSCOP_MSG_FREE(msg);

	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);

	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 38: IN_PEND && USTAT PDU
 *	arg is pdu (freed).
 */
static void
sscop_inpend_ustat(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'I', 0);

	SSCOP_MSG_FREE(msg);

	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);

	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 38: IN_PEND && STAT PDU
 *	arg is pdu (freed).
 */
static void
sscop_inpend_stat(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'H', 0);

	SSCOP_MSG_FREE(msg);

	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);

	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 38: IN_PEND && POLL PDU
 *	arg is pdu (freed).
 */
static void
sscop_inpend_poll(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'G', 0);

	SSCOP_MSG_FREE(msg);

	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);

	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 39: IN_PEND && ERAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_inpend_erak(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'M', 0);
}

/*
 * p 39: IN_PEND & RS PDU
 *	arg is pdu (freed).
 */
static void
sscop_inpend_rs(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'J', 0);
}

/*
 * p 39: IN_PEND & RSAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_inpend_rsak(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'K', 0);
}

/*
 * p 39: IN_PEND && END PDU
 *	arg is pdu (freed).
 *	no uui
 */
static void
sscop_inpend_end(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	(void)MBUF_STRIP32(msg->m);

	send_endak(sscop);

	AAL_UU_SIGNAL(sscop, SSCOP_RELEASE_indication,
		msg, pdu.sscop_pl, (u_int)pdu.sscop_s);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 40: OUT_DIS_PEND && SSCOP_ESTABLISH_request
 *	no arg.
 *	no uui.
 */
static void
sscop_outdis_establish_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_bgn, uu);

	TIMER_STOP(sscop, cc);
	m_clear_transmitter(sscop);
	sscop->clear_buffers = 1;
	sscop->vt_cc = 1;
	sscop->vt_sq++;
	m_initialize_mr(sscop);
	send_bgn(sscop, sscop->uu_bgn);
	TIMER_RESTART(sscop, cc);

	sscop_set_state(sscop, SSCOP_OUT_PEND);
}

/*
 * p 41: OUT_DIS_PEND && END PDU
 *	arg is pdu (freed).
 */
static void
sscop_outdis_end(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	(void)MBUF_STRIP32(msg->m);

	TIMER_STOP(sscop, cc);
	send_endak(sscop);

	AAL_UU_SIGNAL(sscop, SSCOP_RELEASE_confirm, msg, pdu.sscop_pl, 0);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 41: OUT_DIS_PEND && ENDAK PDU
 * p 41: OUT_DIS_PEND && BGREJ PDU
 *	arg is pdu (freed)
 */
static void
sscop_outdis_endak(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	(void)MBUF_STRIP32(msg->m);

	TIMER_STOP(sscop, cc);

	AAL_UU_SIGNAL(sscop, SSCOP_RELEASE_confirm, msg, pdu.sscop_pl, 0);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 41: OUT_DIS_PEND && TIMER CC expiry
 *	no arg
 */
static void
sscop_outdis_cc(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	if(sscop->vt_cc >= sscop->maxcc) {
		MAAL_ERROR(sscop, 'O', 0);
		AAL_SIG(sscop, SSCOP_RELEASE_confirm);
		sscop_set_state(sscop, SSCOP_IDLE);
	} else {
		sscop->vt_cc++;
		send_end(sscop, sscop->last_end_src, sscop->uu_end);
		TIMER_RESTART(sscop, cc);
	}
}

/*
 * p 42: OUT_DIS_PEND && BGN PDU
 *	arg is pdu (freed).
 */
static void
sscop_outdis_bgn(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		FREE_UU(uu_bgak);
		send_bgak(sscop, NULL);
		send_end(sscop, sscop->last_end_src, sscop->uu_end);
		SSCOP_MSG_FREE(msg);

	} else {
		(void)MBUF_STRIP32(msg->m);

		TIMER_STOP(sscop, cc);
		sscop->vt_ms = pdu.sscop_ns;
		AAL_SIG(sscop, SSCOP_RELEASE_confirm);
		AAL_UU_SIGNAL(sscop, SSCOP_ESTABLISH_indication,
			msg, pdu.sscop_pl, 0);
		sscop_set_state(sscop, SSCOP_IN_PEND);
	}
}

/*
 * p 43: OUT_RESYNC_PEND && BGN PDU
 *	arg is pdu (freed).
 */
static void
sscop_outsync_bgn(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		send_bgak(sscop, sscop->uu_bgak);
		send_rs(sscop, 1, sscop->uu_rs);
		SSCOP_MSG_FREE(msg);
	} else {
		(void)MBUF_STRIP32(msg->m);

		TIMER_STOP(sscop, cc);
		sscop->vt_ms = pdu.sscop_ns;
		AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 0);
		AAL_UU_SIGNAL(sscop, SSCOP_ESTABLISH_indication,
			msg, pdu.sscop_pl, 0);
		sscop_set_state(sscop, SSCOP_IN_PEND);
	}
}

/*
 * p 43: OUT_RESYNC_PEND && ENDAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_outsync_endak(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	TIMER_STOP(sscop, cc);
	MAAL_ERROR(sscop, 'F', 0);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 43: OUT_RESYNC_PEND && BGREJ PDU
 *	arg is pdu (freed).
 */
static void
sscop_outsync_bgrej(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	TIMER_STOP(sscop, cc);
	MAAL_ERROR(sscop, 'D', 0);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 43: OUT_RESYNC_PEND && END PDU
 *	arg is pdu (freed).
 *	no UU-data
 */
static void
sscop_outsync_end(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	(void)MBUF_STRIP32(msg->m);

	TIMER_STOP(sscop, cc);
	send_endak(sscop);
	AAL_UU_SIGNAL(sscop, SSCOP_RELEASE_indication, msg, pdu.sscop_pl,
		(u_int)pdu.sscop_s);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 44: OUT_RESYNC && TIMER CC expiry
 */
static void
sscop_outsync_cc(struct sscop *sscop, struct sscop_msg *msg __unused)
{
	if(sscop->vt_cc == sscop->maxcc) {
		MAAL_ERROR(sscop, 'O', 0);
		FREE_UU(uu_end);
		send_end(sscop, 1, NULL);
		AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
		sscop_set_state(sscop, SSCOP_IDLE);
	} else {
		sscop->vt_cc++;
		send_rs(sscop, 1, sscop->uu_rs);
		TIMER_RESTART(sscop, cc);
	}
}

/*
 * p 44: OUT_RESYNC && AA-RELEASE.request
 *	arg is UU
 */
static void
sscop_outsync_release_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_end, uu);

	TIMER_STOP(sscop, cc);
	sscop->vt_cc = 1;
	send_end(sscop, 0, sscop->uu_end);
	TIMER_RESTART(sscop, cc);
	sscop_set_state(sscop, SSCOP_OUT_DIS_PEND);
}

/*
 * p 45: OUT_RESYNC && RS PDU
 *	arg is pdu (freed).
 */
static void
sscop_outsync_rs(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		SSCOP_MSG_FREE(msg);
		return;
	}
	(void)MBUF_STRIP32(msg->m);

	TIMER_STOP(sscop, cc);
	sscop->vt_ms = pdu.sscop_ns;
	m_initialize_mr(sscop);
	send_rsak(sscop);
	AAL_UU_SIGNAL(sscop, SSCOP_RESYNC_confirm, msg, pdu.sscop_pl, 0);
	m_initialize_state(sscop);
	m_set_data_xfer_timers(sscop);
	sscop_set_state(sscop, SSCOP_READY);
}

/*
 * p 45: OUT_RESYNC && RSAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_outsync_rsak(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	SSCOP_MSG_FREE(msg);

	TIMER_STOP(sscop, cc);
	sscop->vt_ms = pdu.sscop_ns;
	AAL_SIG(sscop, SSCOP_RESYNC_confirm);
	m_initialize_state(sscop);
	m_set_data_xfer_timers(sscop);
	sscop_set_state(sscop, SSCOP_READY);
}

/*
 * p 46: IN_RESYNC_PEND && AA-RESYNC.response
 */
static void
sscop_insync_sync_resp(struct sscop *sscop, struct sscop_msg *noarg __unused)
{
	m_initialize_mr(sscop);
	send_rsak(sscop);
	m_clear_transmitter(sscop);
	m_initialize_state(sscop);
	m_set_data_xfer_timers(sscop);
	sscop_set_state(sscop, SSCOP_READY);
}

/*
 * p 46: IN_RESYNC_PEND && AA-RELEASE.request
 *	arg is uu
 */
static void
sscop_insync_release_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_end, uu);

	sscop->vt_cc = 1;
	send_end(sscop, 0, sscop->uu_end);
	TIMER_RESTART(sscop, cc);
	sscop_set_state(sscop, SSCOP_OUT_DIS_PEND);
}

/*
 * p 46: IN_RESYNC_PEND && ENDAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_endak(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'F', 0);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 46: IN_RESYNC_PEND && BGREJ PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_bgrej(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'D', 0);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 46: IN_RESYNC_PEND && END PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_end(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	(void)MBUF_STRIP32(msg->m);

	send_endak(sscop);
	AAL_UU_SIGNAL(sscop, SSCOP_RELEASE_indication,
		msg, pdu.sscop_pl, (u_int)pdu.sscop_s);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 47: IN_RESYNC_PEND && ER PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_er(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'L', 0);
}

/*
 * p 47: IN_RESYNC_PEND && BGN PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_bgn(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		MAAL_ERROR(sscop, 'B', 0);
		SSCOP_MSG_FREE(msg);
		return;
	}
	(void)MBUF_STRIP32(msg->m);

	sscop->vt_ms = pdu.sscop_ns;
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 0);
	AAL_UU_SIGNAL(sscop, SSCOP_ESTABLISH_indication, msg, pdu.sscop_pl, 0);

	sscop_set_state(sscop, SSCOP_IN_PEND);
}

/*
 * p 47: IN_RESYNC_PEND && SD PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_sd(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'A', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 47: IN_RESYNC_PEND && POLL PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_poll(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'G', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 47: IN_RESYNC_PEND && STAT PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_stat(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'H', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 47: IN_RESYNC_PEND && USTAT PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_ustat(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'I', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 48: IN_RESYNC_PEND && BGAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_bgak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'C', 0);
	SSCOP_MSG_FREE(msg);
}

/*
 * p 48: IN_RESYNC_PEND && ERAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_erak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'M', 0);
	SSCOP_MSG_FREE(msg);
}

/*
 * p 48: IN_RESYNC_PEND && RS PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_rs(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		SSCOP_MSG_FREE(msg);
		return;
	}
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'J', 0);
}

/*
 * p 48: IN_RESYNC_PEND && RSAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_insync_rsak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'K', 0);
	SSCOP_MSG_FREE(msg);
}


/*
 * p 49: OUT_REC_PEND && AA-DATA.request
 *	arg is message (queued).
 */
static void
sscop_outrec_userdata(struct sscop *sscop, struct sscop_msg *msg)
{
	if(!sscop->clear_buffers) {
		MSGQ_APPEND(&sscop->xq, msg);
		sscop_signal(sscop, SIG_PDU_Q, msg);
	} else {
		SSCOP_MSG_FREE(msg);
	}
}

/*
 * p 49: OUT_REC_PEND && BGAK PDU
 *	arg is pdu (freed)
 */
static void
sscop_outrec_bgak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'C', 0);

	SSCOP_MSG_FREE(msg);
}

/*
 * p 49: OUT_REC_PEND && ERAK PDU
 *	arg is pdu (freed)
 */
static void
sscop_outrec_erak(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	TIMER_STOP(sscop, cc);
	sscop->vt_ms = pdu.sscop_ns;
	m_deliver_data(sscop);

	AAL_SIG(sscop, SSCOP_RECOVER_indication);

	sscop_set_state(sscop, SSCOP_REC_PEND);

	SSCOP_MSG_FREE(msg);
}

/*
 * p 49: OUT_REC_PEND && END PDU
 *	arg is pdu (freed)
 */
static void
sscop_outrec_end(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	(void)MBUF_STRIP32(msg->m);

	TIMER_STOP(sscop, cc);
	send_endak(sscop);
	AAL_UU_SIGNAL(sscop, SSCOP_RELEASE_indication,
		msg, pdu.sscop_pl, (u_int)pdu.sscop_s);

	MSGQ_CLEAR(&sscop->rbuf);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 49: OUT_REC_PEND && ENDAK PDU
 *	arg is pdu (freed)
 */
static void
sscop_outrec_endak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'F', 0);
	TIMER_STOP(sscop, cc);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	MSGQ_CLEAR(&sscop->rbuf);

	sscop_set_state(sscop, SSCOP_IDLE);

	SSCOP_MSG_FREE(msg);
}

/*
 * p 49: OUT_REC_PEND && BGREJ PDU
 *	arg is pdu (freed)
 */
static void
sscop_outrec_bgrej(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'D', 0);
	TIMER_STOP(sscop, cc);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	MSGQ_CLEAR(&sscop->rbuf);

	sscop_set_state(sscop, SSCOP_IDLE);

	SSCOP_MSG_FREE(msg);
}

/*
 * p 50: OUT_REC_PEND && TIMER CC expiry
 *	no arg.
 */
static void
sscop_outrec_cc(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	if(sscop->vt_cc >= sscop->maxcc) {
		MAAL_ERROR(sscop, 'O', 0);
		FREE_UU(uu_end);
		send_end(sscop, 1, NULL);
		AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
		MSGQ_CLEAR(&sscop->rbuf);
		sscop_set_state(sscop, SSCOP_IDLE);
	} else {
		sscop->vt_cc++;
		send_er(sscop);
		TIMER_RESTART(sscop, cc);
	}
}

/*
 * p 50: OUT_REC_PEND && SSCOP_RELEASE_request
 *	arg is UU
 */
static void
sscop_outrec_release_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_end, uu);

	TIMER_STOP(sscop, cc);
	sscop->vt_cc = 1;
	send_end(sscop, 0, sscop->uu_end);
	MSGQ_CLEAR(&sscop->rbuf);
	TIMER_RESTART(sscop, cc);

	sscop_set_state(sscop, SSCOP_OUT_DIS_PEND);
}

/*
 * p 51: OUT_REC_PEND && AA-RESYNC.request
 *	arg is uu
 */
static void
sscop_outrec_sync_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_rs, uu);

	TIMER_STOP(sscop, cc);
	sscop->vt_cc = 1;
	sscop->vt_sq++;
	m_initialize_mr(sscop);
	send_rs(sscop, 0, sscop->uu_rs);
	m_clear_transmitter(sscop);
	MSGQ_CLEAR(&sscop->rbuf);
	TIMER_RESTART(sscop, cc);
}

/*
 * p 51: OUT_REC_PEND && BGN PDU
 *	arg is pdu (freed).
 *	no uui
 */
static void
sscop_outrec_bgn(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		MAAL_ERROR(sscop, 'B', 0);
		SSCOP_MSG_FREE(msg);
	} else {
		(void)MBUF_STRIP32(msg->m);

		TIMER_STOP(sscop, cc);
		sscop->vt_ms = pdu.sscop_ns;
		AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 0);
		AAL_UU_SIGNAL(sscop, SSCOP_ESTABLISH_indication,
			msg, pdu.sscop_pl, 0);
		MSGQ_CLEAR(&sscop->rbuf);

		sscop_set_state(sscop, SSCOP_IN_PEND);
	}
}

/*
 * p 51: OUT_REC_PEND && ER PDU
 *	arg is pdu (freed).
 */
static void
sscop_outrec_er(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		MAAL_ERROR(sscop, 'L', 0);
	} else {
		TIMER_STOP(sscop, cc);
		sscop->vt_ms = pdu.sscop_ns;
		m_initialize_mr(sscop);
		send_erak(sscop);
		m_deliver_data(sscop);

		AAL_SIG(sscop, SSCOP_RECOVER_indication);

		sscop_set_state(sscop, SSCOP_REC_PEND);
	}

	SSCOP_MSG_FREE(msg);
}

/*
 * p 52: OUT_REC_PEND && SD PDU queued
 *	no arg.
 */
static void
sscop_outrec_pduq(struct sscop *sscop, struct sscop_msg *msg)
{
	sscop_save_signal(sscop, SIG_PDU_Q, msg);
}

/*
 * p 52: OUT_REC_PEND && RSAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_outrec_rsak(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'K', 0);
}

/*
 * p 52: OUT_REC_PEND && RS PDU
 *	arg is pdu (freed).
 */
static void
sscop_outrec_rs(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		SSCOP_MSG_FREE(msg);
		MAAL_ERROR(sscop, 'J', 0);
		return;
	}
	(void)MBUF_STRIP32(msg->m);

	TIMER_STOP(sscop, cc);
	sscop->vt_ms = pdu.sscop_ns;
	AAL_UU_SIGNAL(sscop, SSCOP_RESYNC_indication, msg, pdu.sscop_pl, 0);
	MSGQ_CLEAR(&sscop->rbuf);
	sscop_set_state(sscop, SSCOP_IN_RESYNC_PEND);
}

/*
 * p 53: REC_PEND && BGAK PDU
 *	arg is pdu (freed)
 */
static void
sscop_rec_bgak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'C', 0);

	SSCOP_MSG_FREE(msg);
}

/*
 * p 53: REC_PEND && END PDU
 *	arg is pdu (freed)
 *	no uui
 */
static void
sscop_rec_end(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	(void)MBUF_STRIP32(msg->m);

	send_endak(sscop);
	AAL_UU_SIGNAL(sscop, SSCOP_RELEASE_indication,
		msg, pdu.sscop_pl, (u_int)pdu.sscop_s);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 53: REC_PEND && ENDAK PDU
 *	arg is pdu (freed)
 */
static void
sscop_rec_endak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'F', 0);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
	SSCOP_MSG_FREE(msg);
}

/*
 * p 53: REC_PEND && BGREJ PDU
 *	arg is pdu (freed)
 */
static void
sscop_rec_bgrej(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'D', 0);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
	SSCOP_MSG_FREE(msg);
}

/*
 * p 54: REC_PEND && RELEASE
 *	arg is UU
 */
static void
sscop_rec_release_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_end, uu);

	sscop->vt_cc = 1;
	send_end(sscop, 0, sscop->uu_end);
	TIMER_RESTART(sscop, cc);

	sscop_set_state(sscop, SSCOP_OUT_DIS_PEND);
}

/*
 * p 54: REC_PEND && RSAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_rec_rsak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'K', 0);
	SSCOP_MSG_FREE(msg);
}


/*
 * p 54: REC_PEND && RS PDU
 *	arg is pdu (freed).
 */
static void
sscop_rec_rs(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		SSCOP_MSG_FREE(msg);
		MAAL_ERROR(sscop, 'J', 0);
		return;
	}
	(void)MBUF_STRIP32(msg->m);

	sscop->vt_ms = pdu.sscop_ns;
	AAL_UU_SIGNAL(sscop, SSCOP_RESYNC_indication, msg, pdu.sscop_pl, 0);

	sscop_set_state(sscop, SSCOP_IN_RESYNC_PEND);
}

/*
 * p 54: REC_PEND && RECOVER response
 *	no arg
 */
static void
sscop_rec_recover(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	if(!sscop->clear_buffers) {
		MSGQ_CLEAR(&sscop->xbuf);
	}
	m_initialize_state(sscop);
	m_set_data_xfer_timers(sscop);

	sscop_set_state(sscop, SSCOP_READY);
}

/*
 * p 54: REC_PEND && RESYNC request
 *	arg is uu
 */
static void
sscop_rec_sync_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_rs, uu);

	m_clear_transmitter(sscop);
	sscop->vt_cc = 1;
	sscop->vt_sq++;
	m_initialize_mr(sscop);
	send_rs(sscop, 0, sscop->uu_rs);
	TIMER_RESTART(sscop, cc);

	sscop_set_state(sscop, SSCOP_OUT_RESYNC_PEND);
}

/*
 * p 55: REC_PEND && SD PDU queued
 *	no arg
 */
static void
sscop_rec_pduq(struct sscop *sscop, struct sscop_msg *msg)
{
	sscop_save_signal(sscop, SIG_PDU_Q, msg);
}

/*
 * p 55: REC_PEND && ER PDU
 *	arg is pdu (freed).
 */
static void
sscop_rec_er(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		send_erak(sscop);
	} else {
		MAAL_ERROR(sscop, 'L', 0);
	}
	SSCOP_MSG_FREE(msg);
}

/*
 * p 55: REC_PEND && BGN PDU
 *	arg is pdu (freed)
 *	no uui
 */
static void
sscop_rec_bgn(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		MAAL_ERROR(sscop, 'B', 0);
		SSCOP_MSG_FREE(msg);
		return;
	}
	(void)MBUF_STRIP32(msg->m);

	sscop->vt_ms = pdu.sscop_ns;
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 0);
	AAL_UU_SIGNAL(sscop, SSCOP_ESTABLISH_indication, msg, pdu.sscop_pl, 0);

	sscop_set_state(sscop, SSCOP_IN_PEND);
}

/*
 * p 55: REC_PEND && STAT PDU
 *	arg is pdu (freed)
 */
static void
sscop_rec_stat(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'H', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
	SSCOP_MSG_FREE(msg);
}

/*
 * p 55: REC_PEND && USTAT PDU
 *	arg is pdu (freed)
 */
static void
sscop_rec_ustat(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'I', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	sscop_set_state(sscop, SSCOP_IDLE);
	SSCOP_MSG_FREE(msg);
}

/*
 * p 56: IN_REC_PEND && AA-RECOVER.response
 *	no arg
 */
static void
sscop_inrec_recover(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	if(!sscop->clear_buffers) {
		MSGQ_CLEAR(&sscop->xbuf);
	}
	m_initialize_mr(sscop);
	send_erak(sscop);
	m_initialize_state(sscop);
	m_set_data_xfer_timers(sscop);

	sscop_set_state(sscop, SSCOP_READY);
}

/*
 * p 56: IN_REC_PEND && SD PDU queued
 *	no arg
 */
static void
sscop_inrec_pduq(struct sscop *sscop, struct sscop_msg *msg)
{
	sscop_save_signal(sscop, SIG_PDU_Q, msg);
}

/*
 * p 56: IN_REC_PEND && AA-RELEASE.request
 *	arg is UU
 */
static void
sscop_inrec_release_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_end, uu);

	sscop->vt_cc = 1;
	send_end(sscop, 0, sscop->uu_end);
	TIMER_RESTART(sscop, cc);

	sscop_set_state(sscop, SSCOP_OUT_DIS_PEND);
}

/*
 * p 56: IN_REC_PEND && END PDU
 *	arg is pdu (freed).
 *	no uui
 */
static void
sscop_inrec_end(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	(void)MBUF_STRIP32(msg->m);

	send_endak(sscop);
	AAL_UU_SIGNAL(sscop, SSCOP_RELEASE_indication,
		msg, pdu.sscop_pl, (u_int)pdu.sscop_s);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 56: IN_REC_PEND && RESYNC_REQ
 */
static void
sscop_inrec_sync_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_rs, uu);

	m_clear_transmitter(sscop);
	sscop->vt_cc = 1;
	sscop->vt_sq++;
	m_initialize_mr(sscop);
	send_rs(sscop, 0, sscop->uu_rs);
	TIMER_RESTART(sscop, cc);

	sscop_set_state(sscop, SSCOP_OUT_RESYNC_PEND);
}


/*
 * p 57: IN_REC_PEND && ENDAK PDU
 *	arg is pdu (freed)
 */
static void
sscop_inrec_endak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'F', 0);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	SSCOP_MSG_FREE(msg);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 57: IN_REC_PEND && BGREJ PDU
 *	arg is pdu (freed)
 */
static void
sscop_inrec_bgrej(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'D', 0);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	SSCOP_MSG_FREE(msg);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 57: IN_REC_PEND && USTAT PDU
 *	arg is pdu (freed)
 */
static void
sscop_inrec_ustat(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'I', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	SSCOP_MSG_FREE(msg);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 57: IN_REC_PEND && STAT PDU
 *	arg is pdu (freed)
 */
static void
sscop_inrec_stat(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'H', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	SSCOP_MSG_FREE(msg);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 57: IN_REC_PEND && POLL PDU
 *	arg is pdu (freed)
 */
static void
sscop_inrec_poll(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'G', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	SSCOP_MSG_FREE(msg);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 57: IN_REC_PEND && SD PDU
 *	arg is pdu (freed)
 */
static void
sscop_inrec_sd(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'A', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	SSCOP_MSG_FREE(msg);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 58: IN_REC_PEND && RSAK PDU
 *	arg is pdu (freed).
 */
static void
sscop_inrec_rsak(struct sscop *sscop, struct sscop_msg *msg)
{
	SSCOP_MSG_FREE(msg);
	MAAL_ERROR(sscop, 'K', 0);
}

/*
 * p 58: IN_REC_PEND && RS PDU
 *	arg is pdu (freed).
 */
static void
sscop_inrec_rs(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		SSCOP_MSG_FREE(msg);
		MAAL_ERROR(sscop, 'J', 0);
		return;
	}
	(void)MBUF_STRIP32(msg->m);

	sscop->vt_ms = pdu.sscop_ns;
	AAL_UU_SIGNAL(sscop, SSCOP_RESYNC_indication, msg, pdu.sscop_pl, 0);

	sscop_set_state(sscop, SSCOP_IN_RESYNC_PEND);
}

/*
 * p 59: IN_REC_PEND && ER PDU
 *	arg is pdu (freed)
 */
static void
sscop_inrec_er(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(!m_detect_retransmission(sscop, msg)) {
		MAAL_ERROR(sscop, 'L', 0);
	}

	SSCOP_MSG_FREE(msg);
}

/*
 * p 59: IN_REC_PEND && BGN PDU
 *	arg is pdu (freed).
 *	no uui
 */
static void
sscop_inrec_bgn(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		MAAL_ERROR(sscop, 'B', 0);
		SSCOP_MSG_FREE(msg);
		return;
	}
	(void)MBUF_STRIP32(msg->m);

	sscop->vt_ms = pdu.sscop_ns;
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 0);
	AAL_UU_SIGNAL(sscop, SSCOP_ESTABLISH_indication, msg, pdu.sscop_pl, 0);

	sscop_set_state(sscop, SSCOP_IN_PEND);
}

/*
 * p 59: IN_REC_PEND && BGAK PDU
 *	arg is pdu (freed)
 *	no uui
 */
static void
sscop_inrec_bgak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'C', 0);
	SSCOP_MSG_FREE(msg);
}

/*
 * p 59: IN_REC_PEND && ERAK PDU
 *	arg is pdu (freed)
 *	no uui
 */
static void
sscop_inrec_erak(struct sscop *sscop, struct sscop_msg *msg)
{
	MAAL_ERROR(sscop, 'M', 0);
	SSCOP_MSG_FREE(msg);
}

/*
 * p 60: READY && RESYNC request
 *	arg is UU
 */
static void
sscop_ready_sync_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_rs, uu);

	m_reset_data_xfer_timers(sscop);
	sscop->vt_cc = 1;
	sscop->vt_sq++;
	m_initialize_mr(sscop);
	send_rs(sscop, 0, sscop->uu_rs);
	m_release_buffers(sscop);
	TIMER_RESTART(sscop, cc);

	sscop_set_state(sscop, SSCOP_OUT_RESYNC_PEND);
}


/*
 * p 60: READY && AA-RELEASE.request
 *	arg is uu.
 */
static void
sscop_ready_release_req(struct sscop *sscop, struct sscop_msg *uu)
{
	SET_UU(uu_end, uu);

	m_reset_data_xfer_timers(sscop);
	sscop->vt_cc = 1;
	send_end(sscop, 0, sscop->uu_end);
	m_prepare_retrieval(sscop);
	TIMER_RESTART(sscop, cc);

	sscop_set_state(sscop, SSCOP_OUT_DIS_PEND);
}

/*
 * p 61: READY && ER PDU
 *	arg is pdu (freed).
 */
static void
sscop_ready_er(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		TIMER_RESTART(sscop, nr);
		send_erak(sscop);
	} else {
		m_reset_data_xfer_timers(sscop);
		sscop->vt_ms = pdu.sscop_ns;
		m_prepare_recovery(sscop);
		m_deliver_data(sscop);

		AAL_SIG(sscop, SSCOP_RECOVER_indication);

		sscop_set_state(sscop, SSCOP_IN_REC_PEND);
	}

	SSCOP_MSG_FREE(msg);
}

/*
 * p 61: READY && BGN PDU
 *	arg is pdu (freed)
 */
static void
sscop_ready_bgn(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		TIMER_RESTART(sscop, nr);
		send_bgak(sscop, sscop->uu_bgak);
		SSCOP_MSG_FREE(msg);
		return;
	}
	(void)MBUF_STRIP32(msg->m);

	m_reset_data_xfer_timers(sscop);
	sscop->vt_ms = pdu.sscop_ns;

	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 0);
	AAL_UU_SIGNAL(sscop, SSCOP_ESTABLISH_indication, msg, pdu.sscop_pl, 0);

	m_prepare_retrieval(sscop);

	sscop_set_state(sscop, SSCOP_IN_PEND);
}

/*
 * p 62: READY && ENDAK PDU
 *	arg is pdu (freed)
 */
static void
sscop_ready_endak(struct sscop *sscop, struct sscop_msg *msg)
{
	m_reset_data_xfer_timers(sscop);
	MAAL_ERROR(sscop, 'F', 0);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	m_prepare_retrieval(sscop);
	SSCOP_MSG_FREE(msg);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 62: READY && BGREJ PDU
 *	arg is pdu (freed)
 */
static void
sscop_ready_bgrej(struct sscop *sscop, struct sscop_msg *msg)
{
	m_reset_data_xfer_timers(sscop);
	MAAL_ERROR(sscop, 'D', 0);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	m_prepare_retrieval(sscop);
	SSCOP_MSG_FREE(msg);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 62: READY && RS PDU
 *	arg is pdu (freed)
 */
static void
sscop_ready_rs(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	if(m_detect_retransmission(sscop, msg)) {
		SSCOP_MSG_FREE(msg);
		TIMER_RESTART(sscop, nr);
		send_rsak(sscop);
		return;
	}
	(void)MBUF_STRIP32(msg->m);

	m_reset_data_xfer_timers(sscop);
	sscop->vt_ms = pdu.sscop_ns;
	AAL_UU_SIGNAL(sscop, SSCOP_RESYNC_indication, msg, pdu.sscop_pl, 0);
	m_prepare_retrieval(sscop);

	sscop_set_state(sscop, SSCOP_IN_RESYNC_PEND);
}

/*
 * p 62: READY && END PDU
 *	arg is pdu (freed)
 */
static void
sscop_ready_end(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	(void)MBUF_STRIP32(msg->m);

	m_reset_data_xfer_timers(sscop);
	send_endak(sscop);
	AAL_UU_SIGNAL(sscop, SSCOP_RELEASE_indication,
		msg, pdu.sscop_pl, (u_int)pdu.sscop_s);
	m_prepare_retrieval(sscop);

	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 63: READY && POLL expiry
 */
static void
sscop_ready_tpoll(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	sscop->vt_ps++;
	send_poll(sscop);
	sscop->vt_pd = 0;
	m_set_poll_timer(sscop);
}

/*
 * p 63: READY && KEEP_ALIVE expiry
 */
static void
sscop_ready_tka(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	sscop->vt_ps++;
	send_poll(sscop);
	sscop->vt_pd = 0;
	m_set_poll_timer(sscop);
}

/*
 * p 63: READY && IDLE expiry
 */
static void
sscop_ready_tidle(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	TIMER_RESTART(sscop, nr);
	sscop->vt_ps++;
	send_poll(sscop);
	sscop->vt_pd = 0;
	m_set_poll_timer(sscop);
}

/*
 * p 63: READY && NO_RESPONSE expiry
 *	no arg
 */
static void
sscop_ready_nr(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	m_reset_data_xfer_timers(sscop);
	MAAL_ERROR(sscop, 'P', 0);
	FREE_UU(uu_end);
	send_end(sscop, 1, NULL);
	AAL_DATA(sscop, SSCOP_RELEASE_indication, NULL, 1);
	m_prepare_retrieval(sscop);
	sscop_set_state(sscop, SSCOP_IDLE);
}

/*
 * p 63: READY && AA-DATA.request
 *	arg is message (queued).
 */
static void
sscop_ready_userdata(struct sscop *sscop, struct sscop_msg *msg)
{
	MSGQ_APPEND(&sscop->xq, msg);

	sscop_signal(sscop, SIG_PDU_Q, msg);
}

/*
 * p 64: READY && SD PDU queued up
 *	arg is unused.
 */
static void
sscop_ready_pduq(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	struct sscop_msg *msg;

	if(sscop->rxq != 0) {
		TAILQ_FOREACH(msg, &sscop->xbuf, link)
			if(msg->rexmit)
				break;
		ASSERT(msg != NULL);
		msg->rexmit = 0;
		sscop->rxq--;
		send_sd(sscop, msg->m, msg->seqno);
		msg->poll_seqno = sscop->vt_ps;
		if(sscop->poll_after_rex && sscop->rxq == 0)
			goto poll;			/* -> A */
		else
			goto maybe_poll;		/* -> B */

	}
	if(MSGQ_EMPTY(&sscop->xq))
		return;

	if(sscop->vt_s >= sscop->vt_ms) {
		/* Send windows closed */
		TIMER_STOP(sscop, idle);
		TIMER_RESTART(sscop, nr);
		goto poll;			/* -> A */

	} else {
		msg = MSGQ_GET(&sscop->xq);
		msg->seqno = sscop->vt_s;
		send_sd(sscop, msg->m, msg->seqno);
		msg->poll_seqno = sscop->vt_ps;
		sscop->vt_s++;
		MSGQ_APPEND(&sscop->xbuf, msg);
		goto maybe_poll;		/* -> B */
	}

	/*
	 * p 65: Poll handling
	 */
  maybe_poll:					/* label B */
	sscop->vt_pd++;
	if(TIMER_ISACT(sscop, poll)) {
		if(sscop->vt_pd < sscop->maxpd)
			return;
	} else {
		 if(TIMER_ISACT(sscop, idle)) {
			TIMER_STOP(sscop, idle);
			TIMER_RESTART(sscop, nr);
		} else {
			TIMER_STOP(sscop, ka);
		}
		if(sscop->vt_pd < sscop->maxpd) {
			TIMER_RESTART(sscop, poll);
			return;
		}
	}
  poll:						/* label A */
	sscop->vt_ps++;
	send_poll(sscop);
	sscop->vt_pd = 0;
	TIMER_RESTART(sscop, poll);
}

/*
 * p 67: common recovery start
 */
static void
sscop_recover(struct sscop *sscop)
{
	sscop->vt_cc = 1;
	sscop->vt_sq++;

	m_initialize_mr(sscop);
	send_er(sscop);
	m_prepare_recovery(sscop);

	TIMER_RESTART(sscop, cc);

	sscop_set_state(sscop, SSCOP_OUT_REC_PEND);
}

/*
 * p 66: READY && SD PDU
 *	arg is received message.
 */
static void
sscop_ready_sd(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;
	u_int sn;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	msg->seqno = pdu.sscop_ns;

	/* Fix padding */
	MBUF_UNPAD(msg->m, pdu.sscop_pl);

	if(msg->seqno >= sscop->vr_mr) {
		/* message outside window */
		if(sscop->vr_h < sscop->vr_mr) {
			send_ustat(sscop, sscop->vr_h, sscop->vr_mr, -1);
			sscop->vr_h = sscop->vr_mr;
		}
		SSCOP_MSG_FREE(msg);
		return;
	}

	if(msg->seqno == sscop->vr_r) {
		if(msg->seqno == sscop->vr_h) {
			sscop->vr_r = msg->seqno + 1;
			sscop->vr_h = msg->seqno + 1;

			AAL_DATA(sscop, SSCOP_DATA_indication,
				msg->m, msg->seqno);
			msg->m = NULL;
			SSCOP_MSG_FREE(msg);

			return;
		}
		for(;;) {
			AAL_DATA(sscop, SSCOP_DATA_indication,
				msg->m, msg->seqno);
			msg->m = NULL;
			SSCOP_MSG_FREE(msg);

			sscop->vr_r++;
			if((msg = MSGQ_PEEK(&sscop->rbuf)) == NULL)
				break;
			sn = msg->seqno;
			ASSERT(sn >= sscop->vr_r);
			if(sn != sscop->vr_r)
				break;
			msg = MSGQ_GET(&sscop->rbuf);
		}
		return;
	}

	/* Messages were lost */

	/* XXX Flow control */
	if(msg->seqno == sscop->vr_h) {
		QINSERT(&sscop->rbuf, msg);
		sscop->vr_h++;
		return;
	}
	if(sscop->vr_h < msg->seqno) {
		QINSERT(&sscop->rbuf, msg);
		send_ustat(sscop, sscop->vr_h, msg->seqno, -1);
		sscop->vr_h = msg->seqno + 1;
		return;
	}

	if(QFIND(&sscop->rbuf, msg->seqno) == NULL) {
		QINSERT(&sscop->rbuf, msg);
		return;
	}

	/* error: start recovery */
	SSCOP_MSG_FREE(msg);
	m_reset_data_xfer_timers(sscop);
	MAAL_ERROR(sscop, 'Q', 0);
	sscop_recover(sscop);
}

/*
 * p 67: READY && POLL PDU
 */
static void
sscop_ready_poll(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;
	union seqno seqno;
	u_int sn, nps;
	struct SSCOP_MBUF_T *m;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	seqno.sscop_null = MBUF_STRIP32(msg->m);

	if((u_int)pdu.sscop_ns < sscop->vr_h) {
		SSCOP_MSG_FREE(msg);
		m_reset_data_xfer_timers(sscop);
		MAAL_ERROR(sscop, 'Q', 0);
		sscop_recover(sscop);
		return;
	}
	nps = seqno.sscop_n;

	if((u_int)pdu.sscop_ns > sscop->vr_mr)
		sscop->vr_h = sscop->vr_mr;
	else
		sscop->vr_h = pdu.sscop_ns;

	SSCOP_MSG_FREE(msg);

	/* build stat pdu */
	if((m = MBUF_ALLOC(sscop->maxstat * 4 + 12)) == NULL) {
		FAILURE("sscop: cannot allocate STAT");
		return;
	}
	sn = sscop->vr_r;

	while(sn != sscop->vr_h) {
		/* loop through burst we already have */
		for(;;) {
			if(sn >= sscop->vr_h) {
				seqno.sscop_null = 0;
				seqno.sscop_n = sn;
				MBUF_APPEND32(m, seqno.sscop_null);
				goto out;
			}
			if(QFIND(&sscop->rbuf, sn) == NULL)
				break;
			sn++;
		}

		/* start of a hole */
		seqno.sscop_null = 0;
		seqno.sscop_n = sn;
		MBUF_APPEND32(m, seqno.sscop_null);
		if(MBUF_LEN(m)/4 >= sscop->maxstat) {
			send_stat(sscop, nps, m);
			if((m = MBUF_ALLOC(sscop->maxstat * 4 + 12)) == NULL) {
				FAILURE("sscop: cannot allocate STAT");
				return;
			}
			seqno.sscop_null = 0;
			seqno.sscop_n = sn;
			MBUF_APPEND32(m, seqno.sscop_null);
		}
		do {
			sn++;
		} while(sn < sscop->vr_h && !QFIND(&sscop->rbuf, sn));
		seqno.sscop_null = 0;
		seqno.sscop_n = sn;
		MBUF_APPEND32(m, seqno.sscop_null);
	}
  out:
	send_stat(sscop, nps, m);
}

/*
 * p 69: READY && USTAT PDU
 *	arg is msg (freed)
 */
static void
sscop_ready_ustat(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;
	union seqno nmr, sq1, sq2;
	u_int cnt;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	nmr.sscop_null = MBUF_STRIP32(msg->m);
	sq2.sscop_null = MBUF_STRIP32(msg->m);
	sq1.sscop_null = MBUF_STRIP32(msg->m);

	SSCOP_MSG_FREE(msg);

	cnt = sq1.sscop_n - sq2.sscop_n;

	if((u_int)pdu.sscop_ns < sscop->vt_a || (u_int)pdu.sscop_ns >= sscop->vt_s) {
		VERBERR(sscop, SSCOP_DBG_ERR, (sscop, sscop->aarg,
		    "USTAT: N(R) outside VT(A)...VT(S)-1: N(R)=%u VT(A)=%u "
		    "VT(S)=%u", (u_int)pdu.sscop_ns, sscop->vt_a, sscop->vt_s));
		goto err_f;
	}

	/* Acknowledge all messages between VT(A) and N(R)-1. N(R) is the new
	 * next in sequence-SD-number of the receiver and means, it has all
	 * messages below N(R). Remove all message below N(R) from the
	 * transmission buffer. It may already be removed because of an
	 * earlier selective ACK in a STAT message.
	 */
	while((msg = MSGQ_PEEK(&sscop->xbuf)) != NULL && msg->seqno < (u_int)pdu.sscop_ns) {
		ASSERT(msg->seqno >= sscop->vt_a);
		MSGQ_REMOVE(&sscop->xbuf, msg);
		SSCOP_MSG_FREE(msg);
	}

	/* Update the in-sequence acknowledge and the send window */
	sscop->vt_a = pdu.sscop_ns;
	sscop->vt_ms = nmr.sscop_n;

	/* check, that the range of requested re-transmissions is between
	 * the in-sequence-ack and the highest up-to-now transmitted SD
	 */
	if(sq1.sscop_n >= sq2.sscop_n
	    || (u_int)sq1.sscop_n < sscop->vt_a
	    || (u_int)sq2.sscop_n >= sscop->vt_s) {
		VERBERR(sscop, SSCOP_DBG_ERR, (sscop, sscop->aarg,
		    "USTAT: seq1 or seq2 outside VT(A)...VT(S)-1 or seq1>=seq2:"
		    " seq1=%u seq2=%u VT(A)=%u VT(S)=%u",
		    sq1.sscop_n, sq2.sscop_n, sscop->vt_a, sscop->vt_s));
		goto err_f;
	}

	/*
	 * Retransmit all messages from seq1 to seq2-1
	 */
	do {
		/*
		 * The message may not be in the transmit buffer if it was
		 * already acked by a STAT. This means, the receiver is
		 * confused.
		 */
		if((msg = QFIND(&sscop->xbuf, sq1.sscop_n)) == NULL) {
			VERBERR(sscop, SSCOP_DBG_ERR, (sscop, sscop->aarg,
			    "USTAT: message %u not found in xmit buffer",
			    sq1.sscop_n));
			goto err_f;
		}

		/*
		 * If it is not yet in the re-transmission queue, put it there
		 */
		if(!msg->rexmit) {
			msg->rexmit = 1;
			sscop->rxq++;
			sscop_signal(sscop, SIG_PDU_Q, msg);
		}
		sq1.sscop_n++;
	} while(sq1.sscop_n != sq2.sscop_n);

	/*
	 * report the re-transmission to the management
	 */
	MAAL_ERROR(sscop, 'V', cnt);
	return;

  err_f:
	m_reset_data_xfer_timers(sscop);
	MAAL_ERROR(sscop, 'T', 0);
	sscop_recover(sscop);
}

/*
 * p 70: READY && STAT PDU
 *	arg is msg (freed).
 */
static void
sscop_ready_stat(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;
	union seqno nps, nmr;
	u_int len, seq1, seq2, cnt;
	struct sscop_msg *m;

	pdu.sscop_null = MBUF_STRIP32(msg->m);
	nmr.sscop_null = MBUF_STRIP32(msg->m);
	nps.sscop_null = MBUF_STRIP32(msg->m);

	len = MBUF_LEN(msg->m) / 4;

	if((u_int)nps.sscop_n < sscop->vt_pa
	    || (u_int)nps.sscop_n > sscop->vt_ps) {
		SSCOP_MSG_FREE(msg);
		m_reset_data_xfer_timers(sscop);
		MAAL_ERROR(sscop, 'R', 0);
		sscop_recover(sscop);
		return;
	}

	if((u_int)pdu.sscop_ns < sscop->vt_a
	    || (u_int)pdu.sscop_ns > sscop->vt_s) {
		/*
		 * The in-sequence acknowledge, i.e. the receivers's next
		 * expected in-sequence msg is outside the window between
		 * the transmitters in-sequence ack and highest seqno -
		 * the receiver seems to be confused.
		 */
		VERBERR(sscop, SSCOP_DBG_ERR, (sscop, sscop->aarg,
		    "STAT: N(R) outside VT(A)...VT(S)-1: N(R)=%u VT(A)=%u "
		    "VT(S)=%u", (u_int)pdu.sscop_ns, sscop->vt_a, sscop->vt_s));
  err_H:
		SSCOP_MSG_FREE(msg);
		m_reset_data_xfer_timers(sscop);
		MAAL_ERROR(sscop, 'S', 0);
		sscop_recover(sscop);
		return;
	}

	/* Acknowledge all messages between VT(A) and N(R)-1. N(R) is the new
	 * next in sequence-SD-number of the receiver and means, it has all
	 * messages below N(R). Remove all message below N(R) from the
	 * transmission buffer. It may already be removed because of an
	 * earlier selective ACK in a STAT message.
	 */
	while((m = MSGQ_PEEK(&sscop->xbuf)) != NULL
	    && m->seqno < (u_int)pdu.sscop_ns) {
		ASSERT(m->seqno >= sscop->vt_a);
		MSGQ_REMOVE(&sscop->xbuf, m);
		SSCOP_MSG_FREE(m);
	}

	/*
	 * Update in-sequence ack, poll-ack and send window.
	 */
	sscop->vt_a = pdu.sscop_ns;
	sscop->vt_pa = nps.sscop_n;
	sscop->vt_ms = nmr.sscop_n;

	cnt = 0;
	if(len > 1) {
		seq1 = MBUF_GET32(msg->m);
		len--;
		if(seq1 >= sscop->vt_s) {
			VERBERR(sscop, SSCOP_DBG_ERR, (sscop, sscop->aarg,
			    "STAT: seq1 >= VT(S): seq1=%u VT(S)=%u",
			    seq1, sscop->vt_s));
			goto err_H;
		}

		for(;;) {
			seq2 = MBUF_GET32(msg->m);
			len--;
			if(seq1 >= seq2 || seq2 > sscop->vt_s) {
				VERBERR(sscop, SSCOP_DBG_ERR, (sscop,
				    sscop->aarg, "STAT: seq1 >= seq2 or "
				    "seq2 > VT(S): seq1=%u seq2=%u VT(S)=%u",
				    seq1, seq2, sscop->vt_s));
				goto err_H;
			}

			do {
				/*
				 * The receiver requests the re-transmission
				 * of some message, but has acknowledged it
				 * already in an earlier STAT (it isn't in the
				 * transmitt buffer anymore).
				 */
				if((m = QFIND(&sscop->xbuf, seq1)) == NULL) {
					VERBERR(sscop, SSCOP_DBG_ERR,
					    (sscop, sscop->aarg, "STAT: message"
					    " %u not found in xmit buffer",
					    seq1));
					goto err_H;
				}
				if(m->poll_seqno < (u_int)nps.sscop_n
				    && (u_int)nps.sscop_n <= sscop->vt_ps)
					if(!m->rexmit) {
						m->rexmit = 1;
						sscop->rxq++;
						cnt++;
						sscop_signal(sscop, SIG_PDU_Q, msg);
					}
			} while(++seq1 < seq2);

			if(len == 0)
				break;

			seq2 = MBUF_GET32(msg->m);
			len--;

			if(seq1 >= seq2 || seq2 > sscop->vt_s) {
				VERBERR(sscop, SSCOP_DBG_ERR, (sscop,
				    sscop->aarg, "STAT: seq1 >= seq2 or "
				    "seq2 > VT(S): seq1=%u seq2=%u VT(S)=%u",
				    seq1, seq2, sscop->vt_s));
				goto err_H;
			}

			/* OK now the sucessful transmitted messages. Note, that
			 * some messages may already be out of the buffer because
			 * of earlier STATS */
			do {
				if(sscop->clear_buffers) {
					if((m = QFIND(&sscop->xbuf, seq1)) != NULL) {
						MSGQ_REMOVE(&sscop->xbuf, m);
						SSCOP_MSG_FREE(m);
					}
				}
			} while(++seq1 != seq2);

			if(len == 0)
				break;
		}
		MAAL_ERROR(sscop, 'V', cnt);
	}
	SSCOP_MSG_FREE(msg);

	/* label L: */
	if(sscop->vt_s >= sscop->vt_ms) {
		/*
		 * The receiver has closed the window: report to management
		 */
		if(sscop->credit) {
			sscop->credit = 0;
			MAAL_ERROR(sscop, 'W', 0);
		}
	} else if(!sscop->credit) {
		/*
		 * The window was forcefully closed above, but
		 * now re-opened. Report to management.
		 */
		sscop->credit = 1;
		MAAL_ERROR(sscop, 'X', 0);
	}

	if(TIMER_ISACT(sscop, poll)) {
		TIMER_RESTART(sscop, nr);
	} else if(!TIMER_ISACT(sscop, idle)) {
		TIMER_STOP(sscop, ka);
		TIMER_STOP(sscop, nr);
		TIMER_RESTART(sscop, idle);
	}
}

/*
 * P. 73: any state & UDATA_REQUEST
 *	arg is pdu (queued)
 */
static void
sscop_udata_req(struct sscop *sscop, struct sscop_msg *msg)
{
	MSGQ_APPEND(&sscop->uxq, msg);
	sscop_signal(sscop, SIG_UPDU_Q, msg);
}

/*
 * P. 73: any state & MDATA_REQUEST
 *	arg is pdu (queued)
 */
static void
sscop_mdata_req(struct sscop *sscop, struct sscop_msg *msg)
{
	MSGQ_APPEND(&sscop->mxq, msg);
	sscop_signal(sscop, SIG_MPDU_Q, msg);
}

/*
 * P. 74: any state & UDATA queued
 *	no arg.
 */
static void
sscop_upduq(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	struct sscop_msg *msg;

	if(sscop->ll_busy)
		return;
	while((msg = MSGQ_GET(&sscop->uxq)) != NULL) {
		send_ud(sscop, msg->m);
		msg->m = NULL;
		SSCOP_MSG_FREE(msg);
	}
}

/*
 * P. 74: any state & MDATA queued
 *	no arg.
 */
static void
sscop_mpduq(struct sscop *sscop, struct sscop_msg *unused __unused)
{
	struct sscop_msg *msg;

	if(sscop->ll_busy)
		return;
	while((msg = MSGQ_GET(&sscop->mxq)) != NULL) {
		send_md(sscop, msg->m);
		msg->m = NULL;
		SSCOP_MSG_FREE(msg);
	}
}

/*
 * p 73: MD PDU
 *	arg is PDU
 */
static void
sscop_md(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	MBUF_UNPAD(msg->m, pdu.sscop_pl);

	MAAL_DATA(sscop, msg->m);
	msg->m = NULL;
	SSCOP_MSG_FREE(msg);
}

/*
 * p 73: UD PDU
 *	arg is PDU
 */
static void
sscop_ud(struct sscop *sscop, struct sscop_msg *msg)
{
	union pdu pdu;

	pdu.sscop_null = MBUF_STRIP32(msg->m);

	MBUF_UNPAD(msg->m, pdu.sscop_pl);

	AAL_DATA(sscop, SSCOP_UDATA_indication, msg->m, 0);
	msg->m = NULL;
	SSCOP_MSG_FREE(msg);
}


/*
 * p 33: IDLE & RETRIEVE
 * p 39: IN_PEND & RETRIEVE
 * p 42: OUT_DIS_PEND & RETRIEVE
 * p 48: IN_RESYNC_PEND & RETRIEVE
 * p 53: REC_PEND & RETRIEVE
 * p 58: IN_REC_PEND & RETRIEVE
 */
static void
sscop_retrieve(struct sscop *sscop, struct sscop_msg *msg)
{
	m_data_retrieval(sscop, msg->rexmit);
	SSCOP_MSG_FREE(msg);
}

/************************************************************/
/*
 * GENERAL EVENT HANDLING
 */

/*
 * State/event matrix.
 *
 * Entries marked with Z are not specified in Q.2110, but are added for
 * the sake of stability.
 */
static struct {
	void	(*func)(struct sscop *, struct sscop_msg *);
	int	(*cond)(struct sscop *);
} state_matrix[SSCOP_NSTATES][SIG_NUM] = {
	/* SSCOP_IDLE */ {
		/* SIG_BGN */		{ sscop_idle_bgn, NULL },
		/* SIG_BGAK */		{ sscop_idle_bgak, NULL },
		/* SIG_END */		{ sscop_idle_end, NULL },
		/* SIG_ENDAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_RS */		{ sscop_idle_rs, NULL },
		/* SIG_RSAK */		{ sscop_idle_rsak, NULL },
		/* SIG_BGREJ */		{ sscop_idle_bgrej, NULL },
		/* SIG_SD */		{ sscop_idle_sd, NULL },
		/* SIG_ER */		{ sscop_idle_er, NULL },
		/* SIG_POLL */		{ sscop_idle_poll, NULL },
		/* SIG_STAT */		{ sscop_idle_stat, NULL },
		/* SIG_USTAT */		{ sscop_idle_ustat, NULL },
		/* SIG_UD */		{ sscop_ud, NULL },
		/* SIG_MD */		{ sscop_md, NULL },
		/* SIG_ERAK */		{ sscop_idle_erak, NULL },
		/* SIG_T_CC */		{ NULL, NULL },
		/* SIG_T_POLL */	{ NULL, NULL },
		/* SIG_T_KA */		{ NULL, NULL },
		/* SIG_T_NR */		{ NULL, NULL },
		/* SIG_T_IDLE */	{ NULL, NULL },
		/* SIG_PDU_Q */		{ sscop_flush_pduq, NULL },
		/* SIG_USER_DATA */	{ NULL, NULL },
		/* SIG_ESTAB_REQ */	{ sscop_idle_establish_req, NULL },
		/* SIG_ESTAB_RESP */	{ NULL, NULL },
		/* SIG_RELEASE_REQ */	{ NULL, NULL },
		/* SIG_RECOVER */	{ NULL, NULL },
		/* SIG_SYNC_REQ */	{ NULL, NULL },
		/* SIG_SYNC_RESP */	{ NULL, NULL },
		/* SIG_UDATA */		{ sscop_udata_req, NULL },
		/* SIG_MDATA */		{ sscop_mdata_req, NULL },
		/* SIG_UPDU_Q */	{ sscop_upduq, NULL },
		/* SIG_MPDU_Q */	{ sscop_mpduq, NULL },
		/* SIG_RETRIEVE */	{ sscop_retrieve, NULL },
	},
	/* SSCOP_OUT_PEND */ {
		/* SIG_BGN */		{ sscop_outpend_bgn, NULL },
		/* SIG_BGAK */		{ sscop_outpend_bgak, NULL },
		/* SIG_END */		{ sscop_ignore_pdu, NULL },
		/* SIG_ENDAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_RS */		{ sscop_ignore_pdu, NULL },
		/* SIG_RSAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_BGREJ */		{ sscop_outpend_bgrej, NULL },
		/* SIG_SD */		{ sscop_ignore_pdu, NULL },
		/* SIG_ER */		{ sscop_ignore_pdu, NULL },
		/* SIG_POLL */		{ sscop_ignore_pdu, NULL },
		/* SIG_STAT */		{ sscop_ignore_pdu, NULL },
		/* SIG_USTAT */		{ sscop_ignore_pdu, NULL },
		/* SIG_UD */		{ sscop_ud, NULL },
		/* SIG_MD */		{ sscop_md, NULL },
		/* SIG_ERAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_T_CC */		{ sscop_outpend_tcc, NULL },
		/* SIG_T_POLL */	{ NULL, NULL },
		/* SIG_T_KA */		{ NULL, NULL },
		/* SIG_T_NR */		{ NULL, NULL },
		/* SIG_T_IDLE */	{ NULL, NULL },
		/* SIG_PDU_Q */		{ sscop_flush_pduq, NULL },
		/* SIG_USER_DATA */	{ NULL, NULL },
		/* SIG_ESTAB_REQ */	{ NULL, NULL },
		/* SIG_ESTAB_RESP */	{ NULL, NULL },
		/* SIG_RELEASE_REQ */	{ sscop_outpend_release_req, NULL },
		/* SIG_RECOVER */	{ NULL, NULL },
		/* SIG_SYNC_REQ */	{ NULL, NULL },
		/* SIG_SYNC_RESP */	{ NULL, NULL },
		/* SIG_UDATA */		{ sscop_udata_req, NULL },
		/* SIG_MDATA */		{ sscop_mdata_req, NULL },
		/* SIG_UPDU_Q */	{ sscop_upduq, NULL },
		/* SIG_MPDU_Q */	{ sscop_mpduq, NULL },
		/* SIG_RETRIEVE */	{ NULL, NULL },
	},
	/* SSCOP_IN_PEND */ {
		/* SIG_BGN */		{ sscop_inpend_bgn, NULL },
		/* SIG_BGAK */		{ sscop_inpend_bgak, NULL },
		/* SIG_END */		{ sscop_inpend_end, NULL },
		/* SIG_ENDAK */		{ sscop_inpend_endak, NULL },
		/* SIG_RS */		{ sscop_inpend_rs, NULL },
		/* SIG_RSAK */		{ sscop_inpend_rsak, NULL },
		/* SIG_BGREJ */		{ sscop_inpend_bgrej, NULL },
		/* SIG_SD */		{ sscop_inpend_sd, NULL },
		/* SIG_ER */		{ sscop_inpend_er, NULL },
		/* SIG_POLL */		{ sscop_inpend_poll, NULL },
		/* SIG_STAT */		{ sscop_inpend_stat, NULL },
		/* SIG_USTAT */		{ sscop_inpend_ustat, NULL },
		/* SIG_UD */		{ sscop_ud, NULL },
		/* SIG_MD */		{ sscop_md, NULL },
		/* SIG_ERAK */		{ sscop_inpend_erak, NULL },
		/* SIG_T_CC */		{ NULL, NULL },
		/* SIG_T_POLL */	{ NULL, NULL },
		/* SIG_T_KA */		{ NULL, NULL },
		/* SIG_T_NR */		{ NULL, NULL },
		/* SIG_T_IDLE */	{ NULL, NULL },
		/* SIG_PDU_Q */		{ sscop_flush_pduq, NULL },
		/* SIG_USER_DATA */	{ NULL, NULL },
		/* SIG_ESTAB_REQ */	{ NULL, NULL },
		/* SIG_ESTAB_RESP */	{ sscop_inpend_establish_resp, NULL },
		/* SIG_RELEASE_REQ */	{ sscop_inpend_release_req, NULL },
		/* SIG_RECOVER */	{ NULL, NULL },
		/* SIG_SYNC_REQ */	{ NULL, NULL },
		/* SIG_SYNC_RESP */	{ NULL, NULL },
		/* SIG_UDATA */		{ sscop_udata_req, NULL },
		/* SIG_MDATA */		{ sscop_mdata_req, NULL },
		/* SIG_UPDU_Q */	{ sscop_upduq, NULL },
		/* SIG_MPDU_Q */	{ sscop_mpduq, NULL },
		/* SIG_RETRIEVE */	{ sscop_retrieve, NULL },
	},
	/* SSCOP_OUT_DIS_PEND */ {
		/* SIG_BGN */		{ sscop_outdis_bgn, NULL },
		/* SIG_BGAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_END */		{ sscop_outdis_end, NULL },
		/* SIG_ENDAK */		{ sscop_outdis_endak, NULL },
		/* SIG_RS */		{ sscop_ignore_pdu, NULL },
		/* SIG_RSAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_BGREJ */		{ sscop_outdis_endak, NULL },
		/* SIG_SD */		{ sscop_ignore_pdu, NULL },
		/* SIG_ER */		{ sscop_ignore_pdu, NULL },
		/* SIG_POLL */		{ sscop_ignore_pdu, NULL },
		/* SIG_STAT */		{ sscop_ignore_pdu, NULL },
		/* SIG_USTAT */		{ sscop_ignore_pdu, NULL },
		/* SIG_UD */		{ sscop_ud, NULL },
		/* SIG_MD */		{ sscop_md, NULL },
		/* SIG_ERAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_T_CC */		{ sscop_outdis_cc, NULL },
		/* SIG_T_POLL */	{ NULL, NULL },
		/* SIG_T_KA */		{ NULL, NULL },
		/* SIG_T_NR */		{ NULL, NULL },
		/* SIG_T_IDLE */	{ NULL, NULL },
		/* SIG_PDU_Q */		{ sscop_flush_pduq, NULL },
		/* SIG_USER_DATA */	{ NULL, NULL },
		/* SIG_ESTAB_REQ */	{ sscop_outdis_establish_req, NULL },
		/* SIG_ESTAB_RESP */	{ NULL, NULL },
		/* SIG_RELEASE_REQ */	{ NULL, NULL },
		/* SIG_RECOVER */	{ NULL, NULL },
		/* SIG_SYNC_REQ */	{ NULL, NULL },
		/* SIG_SYNC_RESP */	{ NULL, NULL },
		/* SIG_UDATA */		{ sscop_udata_req, NULL },
		/* SIG_MDATA */		{ sscop_mdata_req, NULL },
		/* SIG_UPDU_Q */	{ sscop_upduq, NULL },
		/* SIG_MPDU_Q */	{ sscop_mpduq, NULL },
		/* SIG_RETRIEVE */	{ sscop_retrieve, NULL },
	},
	/* SSCOP_OUT_RESYNC_PEND */ {
		/* SIG_BGN */		{ sscop_outsync_bgn, NULL },
		/* SIG_BGAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_END */		{ sscop_outsync_end, NULL },
		/* SIG_ENDAK */		{ sscop_outsync_endak, NULL },
		/* SIG_RS */		{ sscop_outsync_rs, NULL },
		/* SIG_RSAK */		{ sscop_outsync_rsak, NULL },
		/* SIG_BGREJ */		{ sscop_outsync_bgrej, NULL },
		/* SIG_SD */		{ sscop_ignore_pdu, NULL },
		/* SIG_ER */		{ sscop_ignore_pdu, NULL },
		/* SIG_POLL */		{ sscop_ignore_pdu, NULL },
		/* SIG_STAT */		{ sscop_ignore_pdu, NULL },
		/* SIG_USTAT */		{ sscop_ignore_pdu, NULL },
		/* SIG_UD */		{ sscop_ud, NULL },
		/* SIG_MD */		{ sscop_md, NULL },
		/* SIG_ERAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_T_CC */		{ sscop_outsync_cc, NULL },
		/* SIG_T_POLL */	{ NULL, NULL },
		/* SIG_T_KA */		{ NULL, NULL },
		/* SIG_T_NR */		{ NULL, NULL },
		/* SIG_T_IDLE */	{ NULL, NULL },
		/* SIG_PDU_Q */		{ sscop_flush_pduq, NULL },
		/* SIG_USER_DATA */	{ NULL, NULL },
		/* SIG_ESTAB_REQ */	{ NULL, NULL },
		/* SIG_ESTAB_RESP */	{ NULL, NULL },
		/* SIG_RELEASE_REQ */	{ sscop_outsync_release_req, NULL },
		/* SIG_RECOVER */	{ NULL, NULL },
		/* SIG_SYNC_REQ */	{ NULL, NULL },
		/* SIG_SYNC_RESP */	{ NULL, NULL },
		/* SIG_UDATA */		{ sscop_udata_req, NULL },
		/* SIG_MDATA */		{ sscop_mdata_req, NULL },
		/* SIG_UPDU_Q */	{ sscop_upduq, NULL },
		/* SIG_MPDU_Q */	{ sscop_mpduq, NULL },
		/* SIG_RETRIEVE */	{ NULL, NULL },
	},
	/* SSCOP_IN_RESYNC_PEND */ {
		/* SIG_BGN */		{ sscop_insync_bgn, NULL },
		/* SIG_BGAK */		{ sscop_insync_bgak, NULL },
		/* SIG_END */		{ sscop_insync_end, NULL },
		/* SIG_ENDAK */		{ sscop_insync_endak, NULL },
		/* SIG_RS */		{ sscop_insync_rs, NULL },
		/* SIG_RSAK */		{ sscop_insync_rsak, NULL },
		/* SIG_BGREJ */		{ sscop_insync_bgrej, NULL },
		/* SIG_SD */		{ sscop_insync_sd, NULL },
		/* SIG_ER */		{ sscop_insync_er, NULL },
		/* SIG_POLL */		{ sscop_insync_poll, NULL },
		/* SIG_STAT */		{ sscop_insync_stat, NULL },
		/* SIG_USTAT */		{ sscop_insync_ustat, NULL },
		/* SIG_UD */		{ sscop_ud, NULL },
		/* SIG_MD */		{ sscop_md, NULL },
		/* SIG_ERAK */		{ sscop_insync_erak, NULL },
		/* SIG_T_CC */		{ NULL, NULL },
		/* SIG_T_POLL */	{ NULL, NULL },
		/* SIG_T_KA */		{ NULL, NULL },
		/* SIG_T_NR */		{ NULL, NULL },
		/* SIG_T_IDLE */	{ NULL, NULL },
		/* SIG_PDU_Q */		{ sscop_flush_pduq, NULL },
		/* SIG_USER_DATA */	{ NULL, NULL },
		/* SIG_ESTAB_REQ */	{ NULL, NULL },
		/* SIG_ESTAB_RESP */	{ NULL, NULL },
		/* SIG_RELEASE_REQ */	{ sscop_insync_release_req, NULL },
		/* SIG_RECOVER */	{ NULL, NULL },
		/* SIG_SYNC_REQ */	{ NULL, NULL },
		/* SIG_SYNC_RESP */	{ sscop_insync_sync_resp, NULL },
		/* SIG_UDATA */		{ sscop_udata_req, NULL },
		/* SIG_MDATA */		{ sscop_mdata_req, NULL },
		/* SIG_UPDU_Q */	{ sscop_upduq, NULL },
		/* SIG_MPDU_Q */	{ sscop_mpduq, NULL },
		/* SIG_RETRIEVE */	{ sscop_retrieve, NULL },
	},
	/* SSCOP_OUT_REC_PEND */ {
		/* SIG_BGN */		{ sscop_outrec_bgn, NULL },
		/* SIG_BGAK */		{ sscop_outrec_bgak, NULL },
		/* SIG_END */		{ sscop_outrec_end, NULL },
		/* SIG_ENDAK */		{ sscop_outrec_endak, NULL },
		/* SIG_RS */		{ sscop_outrec_rs, NULL },
		/* SIG_RSAK */		{ sscop_outrec_rsak, NULL },
		/* SIG_BGREJ */		{ sscop_outrec_bgrej, NULL },
		/* SIG_SD */		{ sscop_ignore_pdu, NULL },
		/* SIG_ER */		{ sscop_outrec_er, NULL },
		/* SIG_POLL */		{ sscop_ignore_pdu, NULL },
		/* SIG_STAT */		{ sscop_ignore_pdu, NULL },
		/* SIG_USTAT */		{ sscop_ignore_pdu, NULL },
		/* SIG_UD */		{ sscop_ud, NULL },
		/* SIG_MD */		{ sscop_md, NULL },
		/* SIG_ERAK */		{ sscop_outrec_erak, NULL },
		/* SIG_T_CC */		{ sscop_outrec_cc, NULL },
		/* SIG_T_POLL */	{ NULL, NULL },
		/* SIG_T_KA */		{ NULL, NULL },
		/* SIG_T_NR */		{ NULL, NULL },
		/* SIG_T_IDLE */	{ NULL, NULL },
		/* SIG_PDU_Q */		{ sscop_outrec_pduq, NULL },
		/* SIG_USER_DATA */	{ sscop_outrec_userdata, NULL },
		/* SIG_ESTAB_REQ */	{ NULL, NULL },
		/* SIG_ESTAB_RESP */	{ NULL, NULL },
		/* SIG_RELEASE_REQ */	{ sscop_outrec_release_req, NULL },
		/* SIG_RECOVER */	{ NULL, NULL },
		/* SIG_SYNC_REQ */	{ sscop_outrec_sync_req, NULL },
		/* SIG_SYNC_RESP */	{ NULL, NULL },
		/* SIG_UDATA */		{ sscop_udata_req, NULL },
		/* SIG_MDATA */		{ sscop_mdata_req, NULL },
		/* SIG_UPDU_Q */	{ sscop_upduq, NULL },
		/* SIG_MPDU_Q */	{ sscop_mpduq, NULL },
		/* SIG_RETRIEVE */	{ NULL, NULL },
	},
	/* SSCOP_REC_PEND */ {
		/* SIG_BGN */		{ sscop_rec_bgn, NULL },
		/* SIG_BGAK */		{ sscop_rec_bgak, NULL },
		/* SIG_END */		{ sscop_rec_end, NULL },
		/* SIG_ENDAK */		{ sscop_rec_endak, NULL },
		/* SIG_RS */		{ sscop_rec_rs, NULL },
		/* SIG_RSAK */		{ sscop_rec_rsak, NULL },
		/* SIG_BGREJ */		{ sscop_rec_bgrej, NULL },
		/* SIG_SD */		{ sscop_ignore_pdu, NULL },
		/* SIG_ER */		{ sscop_rec_er, NULL },
		/* SIG_POLL */		{ sscop_ignore_pdu, NULL },
		/* SIG_STAT */		{ sscop_rec_stat, NULL },
		/* SIG_USTAT */		{ sscop_rec_ustat, NULL },
		/* SIG_UD */		{ sscop_ud, NULL },
		/* SIG_MD */		{ sscop_md, NULL },
		/* SIG_ERAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_T_CC */		{ NULL, NULL },
		/* SIG_T_POLL */	{ NULL, NULL },
		/* SIG_T_KA */		{ NULL, NULL },
		/* SIG_T_NR */		{ NULL, NULL },
		/* SIG_T_IDLE */	{ NULL, NULL },
		/* SIG_PDU_Q */		{ sscop_rec_pduq, NULL },
		/* SIG_USER_DATA */	{ NULL, NULL },
		/* SIG_ESTAB_REQ */	{ NULL, NULL },
		/* SIG_ESTAB_RESP */	{ NULL, NULL },
		/* SIG_RELEASE_REQ */	{ sscop_rec_release_req, NULL },
		/* SIG_RECOVER */	{ sscop_rec_recover, NULL },
		/* SIG_SYNC_REQ */	{ sscop_rec_sync_req, NULL },
		/* SIG_SYNC_RESP */	{ NULL, NULL },
		/* SIG_UDATA */		{ sscop_udata_req, NULL },
		/* SIG_MDATA */		{ sscop_mdata_req, NULL },
		/* SIG_UPDU_Q */	{ sscop_upduq, NULL },
		/* SIG_MPDU_Q */	{ sscop_mpduq, NULL },
		/* SIG_RETRIEVE */	{ sscop_retrieve, NULL },
	},
	/* SSCOP_IN_REC_PEND */ {
		/* SIG_BGN */		{ sscop_inrec_bgn, NULL },
		/* SIG_BGAK */		{ sscop_inrec_bgak, NULL },
		/* SIG_END */		{ sscop_inrec_end, NULL },
		/* SIG_ENDAK */		{ sscop_inrec_endak, NULL },
		/* SIG_RS */		{ sscop_inrec_rs, NULL },
		/* SIG_RSAK */		{ sscop_inrec_rsak, NULL },
		/* SIG_BGREJ */		{ sscop_inrec_bgrej, NULL },
		/* SIG_SD */		{ sscop_inrec_sd, NULL },
		/* SIG_ER */		{ sscop_inrec_er, NULL },
		/* SIG_POLL */		{ sscop_inrec_poll, NULL },
		/* SIG_STAT */		{ sscop_inrec_stat, NULL },
		/* SIG_USTAT */		{ sscop_inrec_ustat, NULL },
		/* SIG_UD */		{ sscop_ud, NULL },
		/* SIG_MD */		{ sscop_md, NULL },
		/* SIG_ERAK */		{ sscop_inrec_erak, NULL },
		/* SIG_T_CC */		{ NULL, NULL },
		/* SIG_T_POLL */	{ NULL, NULL },
		/* SIG_T_KA */		{ NULL, NULL },
		/* SIG_T_NR */		{ NULL, NULL },
		/* SIG_T_IDLE */	{ NULL, NULL },
		/* SIG_PDU_Q */		{ sscop_inrec_pduq, NULL },
		/* SIG_USER_DATA */	{ NULL, NULL },
		/* SIG_ESTAB_REQ */	{ NULL, NULL },
		/* SIG_ESTAB_RESP */	{ NULL, NULL },
		/* SIG_RELEASE_REQ */	{ sscop_inrec_release_req, NULL },
		/* SIG_RECOVER */	{ sscop_inrec_recover, NULL },
		/* SIG_SYNC_REQ */	{ sscop_inrec_sync_req, NULL },
		/* SIG_SYNC_RESP */	{ NULL, NULL },
		/* SIG_UDATA */		{ sscop_udata_req, NULL },
		/* SIG_MDATA */		{ sscop_mdata_req, NULL },
		/* SIG_UPDU_Q */	{ sscop_upduq, NULL },
		/* SIG_MPDU_Q */	{ sscop_mpduq, NULL },
		/* SIG_RETRIEVE */	{ sscop_retrieve, NULL },
	},
	/* SSCOP_READY */ {
		/* SIG_BGN */		{ sscop_ready_bgn, NULL },
		/* SIG_BGAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_END */		{ sscop_ready_end, NULL },
		/* SIG_ENDAK */		{ sscop_ready_endak, NULL },
		/* SIG_RS */		{ sscop_ready_rs, NULL },
		/* SIG_RSAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_BGREJ */		{ sscop_ready_bgrej, NULL },
		/* SIG_SD */		{ sscop_ready_sd, NULL },
		/* SIG_ER */		{ sscop_ready_er, NULL },
		/* SIG_POLL */		{ sscop_ready_poll, NULL },
		/* SIG_STAT */		{ sscop_ready_stat, NULL },
		/* SIG_USTAT */		{ sscop_ready_ustat, NULL },
		/* SIG_UD */		{ sscop_ud, NULL },
		/* SIG_MD */		{ sscop_md, NULL },
		/* SIG_ERAK */		{ sscop_ignore_pdu, NULL },
		/* SIG_T_CC */		{ NULL, NULL },
		/* SIG_T_POLL */	{ sscop_ready_tpoll, NULL },
		/* SIG_T_KA */		{ sscop_ready_tka, NULL },
		/* SIG_T_NR */		{ sscop_ready_nr, NULL },
		/* SIG_T_IDLE */	{ sscop_ready_tidle, NULL },
		/* SIG_PDU_Q */		{ sscop_ready_pduq, c_ready_pduq },
		/* SIG_USER_DATA */	{ sscop_ready_userdata, NULL },
		/* SIG_ESTAB_REQ */	{ NULL, NULL },
		/* SIG_ESTAB_RESP */	{ NULL, NULL },
		/* SIG_RELEASE_REQ */	{ sscop_ready_release_req, NULL },
		/* SIG_RECOVER */	{ NULL, NULL },
		/* SIG_SYNC_REQ */	{ sscop_ready_sync_req, NULL },
		/* SIG_SYNC_RESP */	{ NULL, NULL },
		/* SIG_UDATA */		{ sscop_udata_req, NULL },
		/* SIG_MDATA */		{ sscop_mdata_req, NULL },
		/* SIG_UPDU_Q */	{ sscop_upduq, NULL },
		/* SIG_MPDU_Q */	{ sscop_mpduq, NULL },
		/* SIG_RETRIEVE */	{ NULL, NULL },
	}
};

/*
 * Try to execute a signal. It is executed if
 *   - it is illegal (in this case it is effectively ignored)
 *   - it has no condition
 *   - its condition is true
 * If it has a condition and that is false, the function does nothing and
 * returns 0.
 * If the signal gets executed, the signal function is responsible to release
 * the message (if any).
 */
static int
sig_exec(struct sscop *sscop, u_int sig, struct sscop_msg *msg)
{
	void (*func)(struct sscop *, struct sscop_msg *);
	int (*cond)(struct sscop *);

	func = state_matrix[sscop->state][sig].func;
	cond = state_matrix[sscop->state][sig].cond;

	if(func == NULL) {
		VERBOSE(sscop, SSCOP_DBG_BUG, (sscop, sscop->aarg,
		    "no handler for %s in state %s - ignored",
		    events[sig], states[sscop->state]));
		SSCOP_MSG_FREE(msg);
		return 1;
	}
	if(cond == NULL || (*cond)(sscop)) {
		VERBOSE(sscop, SSCOP_DBG_EXEC, (sscop, sscop->aarg,
		    "executing %s in %s", events[sig],
		    states[sscop->state]));
		(*func)(sscop, msg);
		return 1;
	}
	VERBOSE(sscop, SSCOP_DBG_EXEC, (sscop, sscop->aarg,
	    "delaying %s in %s", events[sig],
	    states[sscop->state]));

	return 0;
}

/*
 * Deliver a signal to the given sscop
 * If it is delivered from inside a signal handler - queue it. If not,
 * execute it. After execution loop through the queue and execute all
 * pending signals. Signals, that cannot be executed because of entry
 * conditions are skipped.
 */
static void
sscop_signal(struct sscop *sscop, u_int sig, struct sscop_msg *msg)
{
	struct sscop_sig *s;

	VERBOSE(sscop, SSCOP_DBG_INSIG, (sscop, sscop->aarg,
	    "got signal %s in state %s%s", events[sig],
	    states[sscop->state], sscop->in_sig ? " -- queuing" : ""));

	SIG_ALLOC(s);
	if(s == NULL) {
		FAILURE("sscop: cannot allocate signal");
		SSCOP_MSG_FREE(msg);
		return;
	}
	s->sig = sig;
	s->msg = msg;
	SIGQ_APPEND(&sscop->sigs, s);

	if(!sscop->in_sig)
		handle_sigs(sscop);
}

/*
 * Loop through the signal queue until we can't execute any signals.
 */
static void
handle_sigs(struct sscop *sscop)
{
	struct sscop_sig *s;
	sscop_sigq_head_t dsigs, q;
	int exec;

	sscop->in_sig++;
 
	/*
	 * Copy the current signal queue to the local one and empty
	 * the signal queue. Then loop through the signals. After one
	 * pass we have a list of delayed signals because of entry
	 * conditions and a new list of signals. Merge them. Repeat until
	 * the signal queue is either empty or contains only delayed signals.
	 */
	SIGQ_INIT(&q);
	SIGQ_INIT(&dsigs);
	do {
		exec = 0;

		/*
		 * Copy signal list and make sscop list empty
		 */
		SIGQ_MOVE(&sscop->sigs, &q);

		/*
		 * Loop through the list
		 */
		while((s = SIGQ_GET(&q)) != NULL) {
			if(sig_exec(sscop, s->sig, s->msg)) {
				exec = 1;
				SIG_FREE(s);
			} else {
				SIGQ_APPEND(&dsigs, s);
			}
		}

		/*
		 * Merge lists by inserting delayed signals in front of
		 * the signal list. preserving the order.
		 */
		SIGQ_PREPEND(&dsigs, &sscop->sigs);
	} while(exec);
	sscop->in_sig--;
}

/*
 * Save a signal that should be executed only if state changes.
 */
static void
sscop_save_signal(struct sscop *sscop, u_int sig, struct sscop_msg *msg)
{
	struct sscop_sig *s;

	SIG_ALLOC(s);
	if(s == NULL) {
		FAILURE("sscop: cannot allocate signal");
		SSCOP_MSG_FREE(msg);
		return;
	}
	s->sig = sig;
	s->msg = msg;
	SIGQ_APPEND(&sscop->saved_sigs, s);
}

/*
 * Set a new state. If signals are waiting for a state change - append them to
 * the signal queue, so they get executed.
 */
static void
sscop_set_state(struct sscop *sscop, u_int nstate)
{
	VERBOSE(sscop, SSCOP_DBG_STATE, (sscop, sscop->aarg,
	    "changing state from %s to %s",
	    states[sscop->state], states[nstate]));

	sscop->state = nstate;
	SIGQ_MOVE(&sscop->saved_sigs, &sscop->sigs);
}

void
sscop_setdebug(struct sscop *sscop, u_int n)
{
	sscop->debug = n;
}

u_int
sscop_getdebug(const struct sscop *sscop)
{
	return (sscop->debug);
}
