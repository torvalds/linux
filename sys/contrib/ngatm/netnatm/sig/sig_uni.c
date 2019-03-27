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
 * $Begemot: libunimsg/netnatm/sig/sig_uni.c,v 1.11 2004/08/05 07:11:03 brandt Exp $
 *
 * Instance handling
 */

#include <netnatm/unimsg.h>
#include <netnatm/saal/sscopdef.h>
#include <netnatm/saal/sscfudef.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/sig/uni.h>
#include <netnatm/sig/unisig.h>

#include <netnatm/sig/unipriv.h>

#ifdef UNICORE
UNICORE
#endif

#define STR(S) [S] = #S
static const char *custat_names[] = {
	STR(CU_STAT0),
	STR(CU_STAT1),
	STR(CU_STAT2),
	STR(CU_STAT3),
};
static const char *globstat_names[] = {
	STR(UNI_CALLSTATE_REST0),
	STR(UNI_CALLSTATE_REST1),
	STR(UNI_CALLSTATE_REST2),
};

static const char *sig_names[] = {
	STR(UNIAPI_ERROR),
	STR(UNIAPI_CALL_CREATED),
	STR(UNIAPI_CALL_DESTROYED),
	STR(UNIAPI_PARTY_CREATED),
	STR(UNIAPI_PARTY_DESTROYED),
	STR(UNIAPI_LINK_ESTABLISH_request),
	STR(UNIAPI_LINK_ESTABLISH_confirm),
	STR(UNIAPI_LINK_RELEASE_request),
	STR(UNIAPI_LINK_RELEASE_confirm),
	STR(UNIAPI_RESET_request),
	STR(UNIAPI_RESET_confirm),
	STR(UNIAPI_RESET_indication),
	STR(UNIAPI_RESET_ERROR_indication),
	STR(UNIAPI_RESET_response),
	STR(UNIAPI_RESET_ERROR_response),
	STR(UNIAPI_RESET_STATUS_indication),
	STR(UNIAPI_SETUP_request),
	STR(UNIAPI_SETUP_indication),
	STR(UNIAPI_SETUP_response),
	STR(UNIAPI_SETUP_confirm),
	STR(UNIAPI_SETUP_COMPLETE_indication),
	STR(UNIAPI_SETUP_COMPLETE_request),
	STR(UNIAPI_ALERTING_request),
	STR(UNIAPI_ALERTING_indication),
	STR(UNIAPI_PROCEEDING_request),
	STR(UNIAPI_PROCEEDING_indication),
	STR(UNIAPI_RELEASE_request),
	STR(UNIAPI_RELEASE_indication),
	STR(UNIAPI_RELEASE_response),
	STR(UNIAPI_RELEASE_confirm),
	STR(UNIAPI_NOTIFY_request),
	STR(UNIAPI_NOTIFY_indication),
	STR(UNIAPI_STATUS_indication),
	STR(UNIAPI_STATUS_ENQUIRY_request),
	STR(UNIAPI_ADD_PARTY_request),
	STR(UNIAPI_ADD_PARTY_indication),
	STR(UNIAPI_PARTY_ALERTING_request),
	STR(UNIAPI_PARTY_ALERTING_indication),
	STR(UNIAPI_ADD_PARTY_ACK_request),
	STR(UNIAPI_ADD_PARTY_ACK_indication),
	STR(UNIAPI_ADD_PARTY_REJ_request),
	STR(UNIAPI_ADD_PARTY_REJ_indication),
	STR(UNIAPI_DROP_PARTY_request),
	STR(UNIAPI_DROP_PARTY_indication),
	STR(UNIAPI_DROP_PARTY_ACK_request),
	STR(UNIAPI_DROP_PARTY_ACK_indication),
	STR(UNIAPI_ABORT_CALL_request),
};

static const char *verb_names[] = {
# define UNI_DEBUG_DEFINE(D) [UNI_FAC_##D] = #D,
	UNI_DEBUG_FACILITIES
# undef UNI_DEBUG_DEFINE
};

const char *
uni_facname(enum uni_verb fac)
{
	static char buf[40];

	if (fac >= UNI_MAXFACILITY) {
		sprintf(buf, "FAC%u", fac);
		return (buf);
	}
	return (verb_names[fac]);
}

const char *
uni_signame(enum uni_sig sig)
{
	static char buf[40];

	if (sig >= UNIAPI_MAXSIG) {
		sprintf(buf, "UNIAPI_SIG%u", sig);
		return (buf);
	}
	return (sig_names[sig]);
}

struct unicx *
uni_context(struct uni *uni)
{
	return (&uni->cx);
}

static void
uni_init(struct uni *uni)
{
	uni->working = 0;
	uni->cref_alloc = 12;
	uni->custat = CU_STAT0;
	uni->glob_start = UNI_CALLSTATE_REST0;
	uni->glob_respond = UNI_CALLSTATE_REST0;
}

static void
uni_stop(struct uni *uni)
{
	struct call *c;

	while ((c = TAILQ_FIRST(&uni->calls)) != NULL) {
		TAILQ_REMOVE(&uni->calls, c, link);
		uni_destroy_call(c, 1);
	}

	SIGQ_CLEAR(&uni->workq);
	SIGQ_CLEAR(&uni->delq);
}

/*
 * INSTANCE HANDLING
 */
struct uni *
uni_create(void *arg, const struct uni_funcs *funcs)
{
	struct uni *uni;

	if ((uni = INS_ALLOC()) == NULL)
		return (NULL);

	uni_init(uni);

	uni->funcs = funcs;
	uni->arg = arg;
	uni->proto = UNIPROTO_UNI40U;
	uni->sb_tb = 0;
	TAILQ_INIT(&uni->workq);
	TAILQ_INIT(&uni->delq);
	TIMER_INIT_UNI(uni, t309);
	uni->timer309 = UNI_T309_DEFAULT;
	TAILQ_INIT(&uni->calls);
	uni_initcx(&uni->cx);
	TIMER_INIT_UNI(uni, t317);
	TIMER_INIT_UNI(uni, t316);

	uni->timer301 = UNI_T301_DEFAULT;
	uni->init303 = UNI_T303_CNT_DEFAULT;
	uni->timer303 = UNI_T303_DEFAULT;
	uni->init308 = UNI_T308_CNT_DEFAULT;
	uni->timer308 = UNI_T308_DEFAULT;
	uni->timer310 = UNI_T310U_DEFAULT;
	uni->timer313 = UNI_T313_DEFAULT;
	uni->init316 = UNI_T316_CNT_DEFAULT;
	uni->timer316 = UNI_T316_DEFAULT;
	uni->timer317 = UNI_T317_DEFAULT;
	uni->timer322 = UNI_T322_DEFAULT;
	uni->init322 = UNI_T322_CNT_DEFAULT;
	uni->timer397 = UNI_T397_DEFAULT;
	uni->timer398 = UNI_T398_DEFAULT;
	uni->timer399 = UNI_T399U_DEFAULT;

	return (uni);
}

void 
uni_destroy(struct uni *uni)
{
	uni_stop(uni);

	TIMER_DESTROY_UNI(uni, t309);
	TIMER_DESTROY_UNI(uni, t316);
	TIMER_DESTROY_UNI(uni, t317);

	INS_FREE(uni);
}

void
uni_reset(struct uni *uni)
{
	uni_stop(uni);
	uni_init(uni);
}


/*
 * DISPATCH SSCOP SIGNAL
 */
void
uni_saal_input(struct uni *uni, enum saal_sig sig, struct uni_msg *m)
{
	switch (sig) {

	  case SAAL_ESTABLISH_indication:
		if (m != NULL)
			uni_msg_destroy(m);
		uni_enq_coord(uni, SIGO_SAAL_ESTABLISH_indication, 0, NULL);
		break;

	  case SAAL_ESTABLISH_confirm:
		if (m != NULL)
			uni_msg_destroy(m);
		uni_enq_coord(uni, SIGO_SAAL_ESTABLISH_confirm, 0, NULL);
		break;

	  case SAAL_RELEASE_confirm:
		if (m != NULL)
			uni_msg_destroy(m);
		uni_enq_coord(uni, SIGO_SAAL_RELEASE_confirm, 0, NULL);
		break;

	  case SAAL_RELEASE_indication:
		if (m != NULL)
			uni_msg_destroy(m);
		uni_enq_coord(uni, SIGO_SAAL_RELEASE_indication, 0, NULL);
		break;

	  case SAAL_DATA_indication:
		uni_enq_coord(uni, SIGO_SAAL_DATA_indication, 0, m);
		break;

	  case SAAL_UDATA_indication:
		uni_enq_coord(uni, SIGO_SAAL_UDATA_indication, 0, m);
		break;

	  default:
		VERBOSE(uni, UNI_FAC_ERR, 1, "bogus saal signal %u", sig);
		if (m != NULL)
			uni_msg_destroy(m);
		break;
	}
}

static struct {
	const char	*name;
	enum uni_sig	sig;
	size_t		arglen;
	u_int		coord_sig;
	u_int		proto;
#define UNIU	0x01
#define UNIN	0x02
#define PNNI	0x04
} maptab[] = {
	{ "LINK-ESTABLISH.request", UNIAPI_LINK_ESTABLISH_request,
	    0,
	    SIGO_LINK_ESTABLISH_request, UNIU | UNIN },
	{ "LINK-RELEASE.request", UNIAPI_LINK_RELEASE_request,
	    0,
	    SIGO_LINK_RELEASE_request, UNIU | UNIN },

	{ "RESET.request", UNIAPI_RESET_request,
	    sizeof(struct uniapi_reset_request),
	    SIGO_RESET_request, UNIU | UNIN },
	{ "RESET-ERROR.response", UNIAPI_RESET_ERROR_response,
	    sizeof(struct uniapi_reset_error_response),
	    SIGO_RESET_ERROR_response, UNIU | UNIN },
	{ "RESET.response", UNIAPI_RESET_response,
	    sizeof(struct uniapi_reset_response),
	    SIGO_RESET_response, UNIU | UNIN },

	{ "SETUP.request", UNIAPI_SETUP_request,
	    sizeof(struct uniapi_setup_request),
	    SIGO_SETUP_request, UNIU | UNIN },
	{ "SETUP.response", UNIAPI_SETUP_response,
	    sizeof(struct uniapi_setup_response),
	    SIGO_SETUP_response, UNIU | UNIN },
	{ "SETUP-COMPLETE.request", UNIAPI_SETUP_COMPLETE_request,
	    sizeof(struct uniapi_setup_complete_request),
	    SIGO_SETUP_COMPLETE_request, UNIN },
	{ "PROCEEDING.request", UNIAPI_PROCEEDING_request,
	    sizeof(struct uniapi_proceeding_request),
	    SIGO_PROCEEDING_request, UNIU | UNIN },
	{ "ALERTING.request", UNIAPI_ALERTING_request,
	    sizeof(struct uniapi_alerting_request),
	    SIGO_ALERTING_request, UNIU | UNIN },
	{ "RELEASE.request", UNIAPI_RELEASE_request,
	    sizeof(struct uniapi_release_request),
	    SIGO_RELEASE_request, UNIU | UNIN },
	{ "RELEASE.response", UNIAPI_RELEASE_response,
	    sizeof(struct uniapi_release_response),
	    SIGO_RELEASE_response, UNIU | UNIN },
	{ "NOTIFY.request", UNIAPI_NOTIFY_request,
	    sizeof(struct uniapi_notify_request),
	    SIGO_NOTIFY_request, UNIU | UNIN },
	{ "STATUS-ENQUIRY.request", UNIAPI_STATUS_ENQUIRY_request,
	    sizeof(struct uniapi_status_enquiry_request),
	    SIGO_STATUS_ENQUIRY_request, UNIU | UNIN },

	{ "ADD-PARTY.request", UNIAPI_ADD_PARTY_request,
	    sizeof(struct uniapi_add_party_request),
	    SIGO_ADD_PARTY_request, UNIU | UNIN },
	{ "ADD-PARTY-ACK.request", UNIAPI_ADD_PARTY_ACK_request,
	    sizeof(struct uniapi_add_party_ack_request),
	    SIGO_ADD_PARTY_ACK_request, UNIU | UNIN },
	{ "ADD-PARTY-REJ.request", UNIAPI_ADD_PARTY_REJ_request,
	    sizeof(struct uniapi_add_party_rej_request),
	    SIGO_ADD_PARTY_REJ_request, UNIU | UNIN },
	{ "PARTY-ALERTING.request", UNIAPI_PARTY_ALERTING_request,
	    sizeof(struct uniapi_party_alerting_request),
	    SIGO_PARTY_ALERTING_request, UNIU | UNIN },
	{ "DROP-PARTY.request", UNIAPI_DROP_PARTY_request,
	    sizeof(struct uniapi_drop_party_request),
	    SIGO_DROP_PARTY_request, UNIU | UNIN },
	{ "DROP-PARTY-ACK.request", UNIAPI_DROP_PARTY_ACK_request,
	    sizeof(struct uniapi_drop_party_ack_request),
	    SIGO_DROP_PARTY_ACK_request, UNIU | UNIN },

	{ "ABORT-CALL.request", UNIAPI_ABORT_CALL_request,
	    sizeof(struct uniapi_abort_call_request),
	    SIGO_ABORT_CALL_request, UNIU | UNIN },

	{ NULL, 0, 0, 0, 0 }
};

void
uni_uni_input(struct uni *uni, enum uni_sig sig, uint32_t cookie,
    struct uni_msg *m)
{
	u_int i;

	for (i = 0; maptab[i].name != NULL; i++) {
		if (maptab[i].sig == sig) {
			if (uni->proto == UNIPROTO_UNI40U) {
				if (!(maptab[i].proto & UNIU))
					uniapi_uni_error(uni,
					    UNIAPI_ERROR_BAD_SIGNAL, cookie, 0);
			} else if(uni->proto == UNIPROTO_UNI40N) {
				if (!(maptab[i].proto & UNIN))
					uniapi_uni_error(uni,
					    UNIAPI_ERROR_BAD_SIGNAL, cookie, 0);
			} else if(uni->proto == UNIPROTO_PNNI10) {
				if (!(maptab[i].proto & PNNI))
					uniapi_uni_error(uni,
					    UNIAPI_ERROR_BAD_SIGNAL, cookie, 0);
			} else {
				uniapi_uni_error(uni,
				    UNIAPI_ERROR_BAD_SIGNAL, cookie, 0);
			}
			if (uni_msg_len(m) != maptab[i].arglen) {
				VERBOSE(uni, UNI_FAC_ERR, 1, "bogus data in %s"
				    " (expecting %zu, got %zu)", maptab[i].name,
				    maptab[i].arglen, uni_msg_len(m));
				uni_msg_destroy(m);
				uniapi_uni_error(uni, UNIAPI_ERROR_BAD_ARG,
				    cookie, 0);
				return;
			}
			if (maptab[i].arglen == 0) {
				uni_msg_destroy(m);
				m = NULL;
			}
			VERBOSE(uni, UNI_FAC_API, 1, "got signal %s - "
			    "delivering to Coord", maptab[i].name);
			uni_enq_coord(uni, maptab[i].coord_sig, cookie, m);
			return;
		}
	}
	VERBOSE(uni, UNI_FAC_ERR, 1, "bogus uni signal %u", sig);
	uni_msg_destroy(m);
	uniapi_uni_error(uni, UNIAPI_ERROR_BAD_SIGNAL, cookie, 0);
}
#undef UNIU
#undef UNIN
#undef PNNI

/**************************************************************/

void
uni_work(struct uni *uni)
{
	struct sig *s;

	if (uni->working)
		return;
	uni->working = 1;

	while ((s = TAILQ_FIRST(&uni->workq)) != NULL) {
		TAILQ_REMOVE(&uni->workq, s, link);
		switch (s->type) {

		  case SIG_COORD:
			uni_sig_coord(uni, s->sig, s->cookie, s->msg);
			break;

		  case SIG_RESET_START:
			uni_sig_start(uni, s->sig, s->cookie, s->msg, s->u);
			break;

		  case SIG_RESET_RESP:
			uni_sig_respond(uni, s->sig, s->cookie, s->msg, s->u);
			break;

		  case SIG_CALL:
			uni_sig_call(s->call, s->sig, s->cookie, s->msg, s->u);
			break;

		  case SIG_PARTY:
			uni_sig_party(s->party, s->sig, s->cookie, s->msg, s->u);
			break;

		  default:
			ASSERT(0, ("bad signal type"));
		}
		SIG_FREE(s);
	}

	uni->working = 0;
}

/*
 * Enqueue a signal in the working queue
 */
void
uni_enq_sig(struct uni *uni, u_int type, struct call *call,
    struct party *party, uint32_t sig, uint32_t cookie,
    struct uni_msg *msg, struct uni_all *u)
{
	struct sig *s;

	if ((s = SIG_ALLOC()) != NULL) {
		s->type = type;
		s->sig = sig;
		s->cookie = cookie;
		s->msg = msg;
		s->call = call;
		s->party = party;
		s->u = u;
		TAILQ_INSERT_TAIL(&uni->workq, s, link);
	}
}

/*
 * Enqueue a signal in the delayed queue
 */
void
uni_delenq_sig(struct uni *uni, u_int type, struct call *call,
    struct party *party, uint32_t sig, uint32_t cookie,
    struct uni_msg *msg, struct uni_all *u)
{
	struct sig *s;

	if ((s = SIG_ALLOC()) != NULL) {
		s->type = type;
		s->sig = sig;
		s->cookie = cookie;
		s->msg = msg;
		s->call = call;
		s->party = party;
		s->u = u;
		TAILQ_INSERT_TAIL(&uni->delq, s, link);
	}
}

/**************************************************************/

void
uniapi_uni_error(struct uni *uni, uint32_t reason, uint32_t cookie,
    uint32_t state)
{
	struct uni_msg *resp;
	struct uniapi_error *err;

	if (cookie == 0)
		return;

	resp = uni_msg_alloc(sizeof(struct uniapi_error));
	err = uni_msg_wptr(resp, struct uniapi_error *);
	resp->b_wptr += sizeof(struct uniapi_error);

	err->reason = reason;
	err->state = state;

	uni->funcs->uni_output(uni, uni->arg, UNIAPI_ERROR, cookie, resp);
}

void
uniapi_call_error(struct call *c, uint32_t reason, uint32_t cookie)
{
	uniapi_uni_error(c->uni, reason, cookie, callstates[c->cstate].ext);
}
void
uniapi_party_error(struct party *p, uint32_t reason, uint32_t cookie)
{
	uniapi_uni_error(p->call->uni, reason, cookie,
	    callstates[p->call->cstate].ext);
}

/**************************************************************/
void
uni_status(struct uni *uni, void *arg)
{
	uni->funcs->status(uni, uni->arg, arg,
	    "working: %s\n", uni->working ? "yes" : "no");
	uni->funcs->status(uni, uni->arg, arg,
	    "work queue: %sempty\n", TAILQ_EMPTY(&uni->workq)? "" : "not ");
	uni->funcs->status(uni, uni->arg, arg,
	    "delayed work queue: %sempty\n",
	    TAILQ_EMPTY(&uni->delq)? "" : "not ");
	uni->funcs->status(uni, uni->arg, arg,
	    "coordinator: %s\n", custat_names[uni->custat]);
	uni->funcs->status(uni, uni->arg, arg,
	    "reset-start: %s\n", globstat_names[uni->glob_start]);
	uni->funcs->status(uni, uni->arg, arg,
	    "reset-respond: %s\n", globstat_names[uni->glob_respond]);
}

void
uni_undel(struct uni *uni, int (*filter)(struct sig *, void *), void *arg)
{
	struct sigqueue		newq;
	struct sig *s, *s1;

	if (TAILQ_EMPTY(&uni->delq))
		return;

	TAILQ_INIT(&newq);

	s = TAILQ_FIRST(&uni->delq);
	while (s != NULL) {
		s1 = TAILQ_NEXT(s, link);
		if ((*filter)(s, arg)) {
			TAILQ_REMOVE(&uni->delq, s, link);
			TAILQ_INSERT_TAIL(&uni->workq, s, link);
		}
		s = s1;
	}
}

void
uni_delsig(struct uni *uni, u_int type, struct call *c, struct party *p)
{
	struct sig *s, *s1;

	s = TAILQ_FIRST(&uni->workq);
	while (s != NULL) {
		s1 = TAILQ_NEXT(s, link);
		if ((type == SIG_CALL && s->type == SIG_CALL &&
		    s->call == c) ||
		    (type == SIG_PARTY && s->type == SIG_PARTY &&
		    s->call == c && s->party == p)) {
			TAILQ_REMOVE(&uni->workq, s, link);
			if (s->msg)
				uni_msg_destroy(s->msg);
			if (s->u)
				UNI_FREE(s->u);
			SIG_FREE(s);
		}
		s = s1;
	}

	s = TAILQ_FIRST(&uni->delq);
	while (s != NULL) {
		s1 = TAILQ_NEXT(s, link);
		if ((type == SIG_CALL && s->type == SIG_CALL &&
		    s->call == c) ||
		    (type == SIG_PARTY && s->type == SIG_PARTY &&
		    s->call == c && s->party == p)) {
			TAILQ_REMOVE(&uni->delq, s, link);
			if (s->msg)
				uni_msg_destroy(s->msg);
			if (s->u)
				UNI_FREE(s->u);
			SIG_FREE(s);						\
		}
		s = s1;
	}
}

/**************************************************************/

void
uni_get_config(const struct uni *uni, struct uni_config *config)
{
	config->proto = uni->proto;

	config->popt = 0;
	if (uni->cx.q2932)
		config->popt |= UNIPROTO_GFP;

	config->option = 0;
	if (uni->cx.git_hard)
		config->option |= UNIOPT_GIT_HARD;
	if (uni->cx.bearer_hard)
		config->option |= UNIOPT_BEARER_HARD;
	if (uni->cx.cause_hard)
		config->option |= UNIOPT_CAUSE_HARD;
	if (uni->sb_tb)
		config->popt |= UNIPROTO_SB_TB;

	config->timer301 = uni->timer301;
	config->timer303 = uni->timer303;
	config->init303 = uni->init303;
	config->timer308 = uni->timer308;
	config->init308 = uni->init308;
	config->timer309 = uni->timer309;
	config->timer310 = uni->timer310;
	config->timer313 = uni->timer313;
	config->timer316 = uni->timer316;
	config->init316 = uni->init316;
	config->timer317 = uni->timer317;
	config->timer322 = uni->timer322;
	config->init322 = uni->init322;
	config->timer397 = uni->timer397;
	config->timer398 = uni->timer398;
	config->timer399 = uni->timer399;
}

void
uni_set_config(struct uni *uni, const struct uni_config *config,
    uint32_t *mask, uint32_t *popt_mask, uint32_t *opt_mask)
{
	int idle;

	idle = TAILQ_EMPTY(&uni->calls) &&
	    TAILQ_EMPTY(&uni->workq) &&
	    TAILQ_EMPTY(&uni->delq);

	if ((*mask & UNICFG_PROTO) && idle) {
		switch (config->proto) {

		  case UNIPROTO_UNI40U:
		  case UNIPROTO_UNI40N:
		  /* case UNIPROTO_PNNI10: XXX */
			uni->proto = config->proto;
			*mask &= ~UNICFG_PROTO;
			break;
		}
	}
	if (*popt_mask & UNIPROTO_GFP) {
		if (config->popt & UNIPROTO_GFP) {
			uni->cx.q2932 = 1;
			*popt_mask &= ~UNIPROTO_GFP;
		} else {
			if (!uni->cx.q2932 || idle) {
				uni->cx.q2932 = 0;
				*popt_mask &= ~UNIPROTO_GFP;
			}
		}
	}
	if (*popt_mask & UNIPROTO_SB_TB) {
		uni->sb_tb = ((config->popt & UNIPROTO_SB_TB) != 0);
		*popt_mask &= ~UNIPROTO_SB_TB;
	}
	if (*opt_mask & UNIOPT_GIT_HARD) {
		uni->cx.git_hard = ((config->option & UNIOPT_GIT_HARD) != 0);
		*opt_mask &= ~UNIOPT_GIT_HARD;
	}
	if (*opt_mask & UNIOPT_BEARER_HARD) {
		uni->cx.bearer_hard = ((config->option & UNIOPT_BEARER_HARD) != 0);
		*opt_mask &= ~UNIOPT_BEARER_HARD;
	}
	if (*opt_mask & UNIOPT_CAUSE_HARD) {
		uni->cx.cause_hard = ((config->option & UNIOPT_CAUSE_HARD) != 0);
		*opt_mask &= ~UNIOPT_CAUSE_HARD;
	}

#define SET_TIMER(NAME,name)						\
	if (*mask & UNICFG_##NAME) {					\
		uni->name = config->name;				\
		*mask &= ~UNICFG_##NAME;				\
	}

	SET_TIMER(TIMER301, timer301);
	SET_TIMER(TIMER303, timer303);
	SET_TIMER(INIT303,  init303);
	SET_TIMER(TIMER308, timer308);
	SET_TIMER(INIT308,  init308);
	SET_TIMER(TIMER309, timer309);
	SET_TIMER(TIMER310, timer310);
	SET_TIMER(TIMER313, timer313);
	SET_TIMER(TIMER316, timer316);
	SET_TIMER(INIT316,  init316);
	SET_TIMER(TIMER317, timer317);
	SET_TIMER(TIMER322, timer322);
	SET_TIMER(INIT322,  init322);
	SET_TIMER(TIMER397, timer397);
	SET_TIMER(TIMER398, timer398);
	SET_TIMER(TIMER399, timer399);

#undef SET_TIMER
}

void
uni_set_debug(struct uni *uni, enum uni_verb fac, u_int level)
{
	uni->debug[fac] = level;
}

u_int
uni_get_debug(const struct uni *uni, enum uni_verb fac)
{
	return (uni->debug[fac]);
}

u_int
uni_getcustate(const struct uni *uni)
{
	return (uni->custat);
}
