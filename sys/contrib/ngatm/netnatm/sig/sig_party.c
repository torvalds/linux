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
 * $Begemot: libunimsg/netnatm/sig/sig_party.c,v 1.18 2004/08/05 07:11:01 brandt Exp $
 *
 * Party instance handling
 */

#include <netnatm/unimsg.h>
#include <netnatm/saal/sscfudef.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/sig/uni.h>

#include <netnatm/sig/unipriv.h>
#include <netnatm/sig/unimkmsg.h>
#include <netnatm/sig/unimsgcpy.h>

static void drop_partyE(struct party *p);
static int epstate_compat(struct party *, enum uni_epstate);

#define DEF_PRIV_SIG(NAME, FROM)	[SIG##NAME] =	"SIG"#NAME,
static const char *const party_sigs[] = {
	DEF_PARTY_SIGS
};
#undef DEF_PRIV_SIG

TIMER_FUNC_PARTY(t397, t397_func)
TIMER_FUNC_PARTY(t398, t398_func)
TIMER_FUNC_PARTY(t399, t399_func)

static __inline void
set_party_state(struct party *p, enum uni_epstate state)
{
	if (p->state != state) {
		VERBOSE(p->call->uni, UNI_FAC_CALL, 1,
		    "party %u/%u %u/%u PU%u -> PU%u",
		    p->call->cref, p->call->mine,
		    p->epref, p->flags & PARTY_MINE, p->state, state);
		p->state = state;
	}
}

/*
 * Create a party with a given endpoint reference.
 * No check is done, that a party with this epref does not alreay exist.
 */
struct party *
uni_create_partyx(struct call *c, u_int epref, u_int mine, uint32_t cookie)
{
	struct party *p;
	struct uni_msg *api;
	struct uniapi_party_created *ind;

	mine = (mine ? PARTY_MINE : 0);

	if ((p = PARTY_ALLOC()) == NULL)
		return (NULL);

	if ((ind = ALLOC_API(struct uniapi_party_created, api)) == NULL) {
		PARTY_FREE(p);
		return (NULL);
	}

	ind->cref.cref = c->cref;
	ind->cref.flag = c->mine;
	MK_IE_EPREF(ind->epref, epref, mine);
	ind->epref.h.act = UNI_IEACT_DEFAULT;

	p->call = c;
	p->epref = epref;
	p->flags = mine;
	p->state = UNI_EPSTATE_NULL;;

	TIMER_INIT_PARTY(p, t397);
	TIMER_INIT_PARTY(p, t398);
	TIMER_INIT_PARTY(p, t399);

	TAILQ_INSERT_HEAD(&c->parties, p, link);

	c->uni->funcs->uni_output(c->uni, c->uni->arg,
	    UNIAPI_PARTY_CREATED, cookie, api);

	VERBOSE(c->uni, UNI_FAC_CALL, 1, "created party %u/%s %u/%s",
	    p->call->cref, p->call->mine ? "mine" : "his",
	    p->epref, (p->flags & PARTY_MINE) ? "mine" : "his");

	return (p);
	
}

struct party *
uni_create_party(struct call *c, struct uni_ie_epref *epref)
{
	return (uni_create_partyx(c, epref->epref, epref->flag, 0));
}

struct party *
uni_find_party(struct call *c, struct uni_ie_epref *epref)
{
	struct party *p;

	TAILQ_FOREACH(p, &c->parties, link)
		if (p->epref == epref->epref &&
		    (!(p->flags & PARTY_MINE) == !epref->flag))
			return (p);
	return (NULL);
}
struct party *
uni_find_partyx(struct call *c, u_int epref, u_int mine)
{
	struct party *p;

	TAILQ_FOREACH(p, &c->parties, link)
		if (p->epref == epref && (!(p->flags & PARTY_MINE) == !mine))
			return (p);
	return (NULL);
}

/*
 * Destroy a party.
 * This function is assumed to remove the party from the parent's call
 * party list.
 */
void
uni_destroy_party(struct party *p, int really)
{
	struct uni_msg *api;
	struct uniapi_party_destroyed *ind;

	TIMER_DESTROY_PARTY(p, t397);
	TIMER_DESTROY_PARTY(p, t398);
	TIMER_DESTROY_PARTY(p, t399);

	TAILQ_REMOVE(&p->call->parties, p, link);

	uni_delsig(p->call->uni, SIG_PARTY, p->call, p);

	if (!really) {
		ind = ALLOC_API(struct uniapi_party_destroyed, api);
		if (ind != NULL) {
			ind->cref.cref = p->call->cref;
			ind->cref.flag = p->call->mine;
			ind->epref.epref = p->epref;
			ind->epref.flag = p->flags & PARTY_MINE;
			ind->epref.h.act = UNI_IEACT_DEFAULT;
			IE_SETPRESENT(ind->epref);

			uni_enq_call(p->call, SIGC_PARTY_DESTROYED, 0, api, NULL);
		}

		uni_enq_party(p, SIGP_PARTY_DELETE, 0, NULL, NULL);
		return;
	}
	PARTY_FREE(p);
}

/*
 * Count number of parties in active states.
 * If the argument is 0 only ACTIVE parties are counter
 * If the argument is 1 only parties in establishing states are counted
 * If the argument is 2 both are counted.
 */
u_int
uni_party_act_count(struct call *c, int kind)
{
	struct party *p;
	u_int cnt;

	cnt = 0;
	TAILQ_FOREACH(p, &c->parties, link) {
		switch (p->state) {

		  case UNI_EPSTATE_ACTIVE:
			if (kind == 0 || kind == 2)
				cnt++;
			break;

		  case UNI_EPSTATE_ALERT_RCVD:
		  case UNI_EPSTATE_ADD_INIT:
		  case UNI_EPSTATE_ALERT_DLVD:
		  case UNI_EPSTATE_ADD_RCVD:
			if (kind == 1 || kind == 2)
				cnt++;
			break;

		  default:
			break;
		}
	}
	return (cnt);
}

static void
stop_all_party_timers(struct party *p)
{
	TIMER_STOP_PARTY(p, t397);
	TIMER_STOP_PARTY(p, t398);
	TIMER_STOP_PARTY(p, t399);
}
/************************************************************/

/*
 * Add-party.request
 *
 * Q.2971:Party-control-U 3 (PU0)
 * Q.2971:Party-control-N 3 (PN0)
 */
static void
pun0_add_party_request(struct party *p, struct uni_msg *api, uint32_t cookie)
{
	struct uni_all *add;
	struct uniapi_add_party_request *req =
	    uni_msg_rptr(api, struct uniapi_add_party_request *);

	if ((add = UNI_ALLOC()) == NULL) {
		uni_msg_destroy(api);
		uniapi_party_error(p, UNIAPI_ERROR_NOMEM, cookie);
		return;
	}

	add->u.add_party = req->add;
	MK_MSG_ORIG(add, UNI_ADD_PARTY, p->call->cref, !p->call->mine);
	uni_send_output(add, p->call->uni);
	UNI_FREE(add);

	TIMER_START_PARTY(p, t399, p->call->uni->timer399);

	set_party_state(p, UNI_EPSTATE_ADD_INIT);

	uni_msg_destroy(api);
	uniapi_party_error(p, UNIAPI_OK, cookie);
}

/*
 * Add-party-ack.request
 *
 * Q.2971:Party-Control-U 6 PU2
 * Q.2971:Party-Control-U 7 PU3
 * Q.2971:Party-Control-N 6 PN2
 * Q.2971:Party-Control-N 7 PN3
 */
static void
punx_add_party_ack_request(struct party *p, struct uni_msg *m, uint32_t cookie)
{
	struct uni_all *ack;
	struct uniapi_add_party_ack_request *req =
	    uni_msg_rptr(m, struct uniapi_add_party_ack_request *);

	if ((ack = UNI_ALLOC()) == NULL) {
		uniapi_party_error(p, UNIAPI_ERROR_NOMEM, cookie);
		uni_msg_destroy(m);
		return;
	}
	ack->u.add_party_ack = req->ack;
	MK_MSG_ORIG(ack, UNI_ADD_PARTY_ACK, p->call->cref, !p->call->mine);
	uni_send_output(ack, p->call->uni);
	UNI_FREE(ack);

	set_party_state(p, UNI_EPSTATE_ACTIVE);

	uni_msg_destroy(m);
	uniapi_party_error(p, UNIAPI_OK, cookie);
}

/*
 * Add-party-rej.request
 *
 * Q.2971:Party-Control-U 6 PU2
 * Q.2971:Party-Control-N 6 PN2
 */
static void
pun2_add_party_rej_request(struct party *p, struct uni_msg *m, uint32_t cookie)
{
	struct uni_all *rej;
	struct uniapi_add_party_rej_request *req =
	    uni_msg_rptr(m, struct uniapi_add_party_rej_request *);

	if ((rej = UNI_ALLOC()) == NULL) {
		uniapi_party_error(p, UNIAPI_ERROR_NOMEM, cookie);
		uni_msg_destroy(m);
		return;
	}

	stop_all_party_timers(p);

	rej->u.add_party_rej = req->rej;
	MK_MSG_ORIG(rej, UNI_ADD_PARTY_REJ, p->call->cref, !p->call->mine);
	uni_enq_call(p->call, SIGC_SEND_ADD_PARTY_REJ, cookie, NULL, rej);

	uni_msg_destroy(m);
	p->state = UNI_EPSTATE_NULL;
	uniapi_party_error(p, UNIAPI_OK, cookie);

	uni_destroy_party(p, 0);
}

/*
 * ADD PARTY in PU0, PN0
 *
 * Q.2971:Party-Control-U 3/14 PU0
 */
static void
pun0_add_party(struct party *p, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_add_party_indication *ind;
	struct uni_msg *api;

	ind = ALLOC_API(struct uniapi_add_party_indication, api);
	if (ind != NULL) {
		ind->add.hdr = u->u.hdr;
		copy_msg_add_party(&u->u.add_party, &ind->add);
		p->call->uni->funcs->uni_output(p->call->uni, p->call->uni->arg,
		    UNIAPI_ADD_PARTY_indication, 0, api);
	}
	set_party_state(p, UNI_EPSTATE_ADD_RCVD);

	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * PARTY-ALERTING.request
 *
 * Q.2971:Party-Control-U 6 (PU2)
 * Q.2971:Party-Control-N 6 (PN2)
 */
static void
pun2_party_alerting_request(struct party *p, struct uni_msg *api,
    uint32_t cookie)
{
	struct uni_all *alert;
	struct uniapi_party_alerting_request *req =
	    uni_msg_rptr(api, struct uniapi_party_alerting_request *);

	if ((alert = UNI_ALLOC()) == NULL) {
		uniapi_party_error(p, UNIAPI_ERROR_NOMEM, cookie);
		uni_msg_destroy(api);
		return;
	}
	alert->u.party_alerting = req->alert;
	MK_MSG_ORIG(alert, UNI_PARTY_ALERTING,
	     p->call->cref, !p->call->mine);
	uni_send_output(alert, p->call->uni);
	UNI_FREE(alert);

	set_party_state(p, UNI_EPSTATE_ALERT_DLVD);

	uni_msg_destroy(api);
	uniapi_party_error(p, UNIAPI_OK, cookie);
}

/*
 * PARTY-ALERTING in state PU1/PN1
 *
 * Q.2971:Party-Control-U 14
 * Q.2971:Party-Control-N 5
 */
static void
pun1_party_alerting(struct party *p, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_party_alerting_indication *ind;
	struct uni_msg *api;

	ind = ALLOC_API(struct uniapi_party_alerting_indication, api);
	if (ind == NULL) {
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}
	TIMER_STOP_PARTY(p, t399);

	ind->alert.hdr = u->u.hdr;
	copy_msg_party_alerting(&u->u.party_alerting, &ind->alert);

	p->call->uni->funcs->uni_output(p->call->uni, p->call->uni->arg,
	    UNIAPI_PARTY_ALERTING_indication, 0, api);

	TIMER_START_PARTY(p, t397, p->call->uni->timer397);

	uni_msg_destroy(m);
	UNI_FREE(u);

	set_party_state(p, UNI_EPSTATE_ALERT_RCVD);
}

/*
 * ADD-PARTY-ACK
 *
 * Q.2971:Party-Control-U 4 (PU1)
 * Q.2971:Party-Control-U 7 (PU4)
 * Q.2971:Party-Control-N 4 (PN1)
 * Q.2971:Party-Control-N 7 (PN4)
 */
static void
pun1pun4_add_party_ack(struct party *p, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_add_party_ack_indication *ind;
	struct uni_msg *api;

	ind = ALLOC_API(struct uniapi_add_party_ack_indication, api);
	if (ind == NULL) {
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}

	if (p->state == UNI_EPSTATE_ADD_INIT)
		TIMER_STOP_PARTY(p, t399);
	else
		TIMER_STOP_PARTY(p, t397);

	ind->ack.hdr = u->u.hdr;
	copy_msg_add_party_ack(&u->u.add_party_ack, &ind->ack);

	p->call->uni->funcs->uni_output(p->call->uni, p->call->uni->arg,
	    UNIAPI_ADD_PARTY_ACK_indication, 0, api);

	uni_msg_destroy(m);
	UNI_FREE(u);

	set_party_state(p, UNI_EPSTATE_ACTIVE);
}

/*
 * ADD-PARTY-REJECT
 *
 * Q.2971:Party-Control-U 4 (PU1)
 * Q.2971:Party-Control-N 4 (PN1)
 */
static void
pun1_add_party_rej(struct party *p, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_add_party_rej_indication *ind;
	struct uni_msg *api;

	ind = ALLOC_API(struct uniapi_add_party_rej_indication, api);
	if (ind == NULL) {
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}

	TIMER_STOP_PARTY(p, t399);

	ind->rej.hdr = u->u.hdr;
	copy_msg_add_party_rej(&u->u.add_party_rej, &ind->rej);
	uni_enq_call(p->call, SIGC_ADD_PARTY_REJ_indication, 0, api, NULL);

	uni_destroy_party(p, 0);

	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * ADD-PARTY-REJECT
 *
 * Q.2971:Party-Control-U 10 (PU5)
 * Q.2971:Party-Control-N 10 (PN5)
 */
static void
pun5_add_party_rej(struct party *p, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_drop_party_ack_indication *ind;
	struct uni_msg *api;

	ind = ALLOC_API(struct uniapi_drop_party_ack_indication, api);
	if (ind == NULL) {
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}

	ind->drop.hdr = u->u.hdr;
	COPY_FROM_ADD_REJ(u, &ind->drop);
	if (IE_ISGOOD(u->u.add_party_rej.crankback))
		ind->crankback = u->u.add_party_rej.crankback;
	uni_enq_call(p->call, SIGC_DROP_PARTY_ACK_indication, 0, api, NULL);

	TIMER_STOP_PARTY(p, t398);

	uni_destroy_party(p, 0);

	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * DROP-PARTY-ACKNOWLEDGE
 *
 * Q.2971:Party-Control-U 8
 * Q.2971:Party-Control-N 8
 *
 * Message already verified in Call-Control!
 */
static void
punx_drop_party_ack(struct party *p, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_drop_party_ack_indication *ind;
	struct uni_msg *api;

	stop_all_party_timers(p);

	ind = ALLOC_API(struct uniapi_drop_party_ack_indication, api);
	if (ind != NULL) {
		ind->drop.hdr = u->u.hdr;
		COPY_FROM_DROP_ACK(u, &ind->drop);
		uni_enq_call(p->call, SIGC_DROP_PARTY_ACK_indication,
		    0, api, NULL);
	}

	uni_destroy_party(p, 0);

	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * DROP PARTY message in any state except PU5/PN5
 *
 * Q.2971:Party-Control-U 9
 * Q.2971:Party-Control-N 9
 */
static void
punx_drop_party(struct party *p, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_drop_party_indication *ind;
	struct uni_msg *api;

	ind = ALLOC_API(struct uniapi_drop_party_indication, api);
	if (ind == NULL) {
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}

	ind->drop.hdr = u->u.hdr;
	copy_msg_drop_party(&u->u.drop_party, &ind->drop);

	/* need the cause even if it is bad */
	if (IE_ISERROR(u->u.drop_party.cause))
		ind->drop.cause = u->u.drop_party.cause;

	ind->my_cause = p->call->uni->cause;

	uni_enq_call(p->call, SIGC_DROP_PARTY_indication, 0, api, NULL);

	TIMER_STOP_PARTY(p, t397);
	TIMER_STOP_PARTY(p, t399);

	uni_msg_destroy(m);
	UNI_FREE(u);

	set_party_state(p, UNI_EPSTATE_DROP_RCVD);
}

/*
 * DROP PARTY message in state PU5/PN5
 *
 * Q.2971:Party-Control-U 10
 * Q.2971:Party-Control-N 10
 */
static void
pun5_drop_party(struct party *p, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_drop_party_ack_indication *ind;
	struct uni_msg *api;

	ind = ALLOC_API(struct uniapi_drop_party_ack_indication, api);
	if (ind == NULL) {
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}

	ind->drop.hdr = u->u.hdr;
	copy_msg_drop_party(&u->u.drop_party, &ind->drop);

	/* need the cause even if it is bad */
	if (IE_ISERROR(u->u.drop_party.cause))
		ind->drop.cause = u->u.drop_party.cause;

	uni_enq_call(p->call, SIGC_DROP_PARTY_ACK_indication, 0, api, NULL);

	TIMER_STOP_PARTY(p, t398);

	uni_msg_destroy(m);
	UNI_FREE(u);

	set_party_state(p, UNI_EPSTATE_DROP_RCVD);

	uni_destroy_party(p, 0);
}

/************************************************************/

/*
 * T399
 *
 * Q.2971:Party-Control-U 4 (PU1)
 * Q.2971:Party-Control-N 4 (PN1)
 */
static void
pun1_t399(struct party *p)
{
	if (p->call->uni->proto == UNIPROTO_UNI40N) {
		MK_IE_CAUSE(p->call->uni->cause, UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_NO_RESPONSE);
	} else {
		MK_IE_CAUSE(p->call->uni->cause, UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_RECOVER);
		ADD_CAUSE_TIMER(p->call->uni->cause, "399");
	}

	drop_partyE(p);
}

/*
 * T398
 *
 * Q.2971:Party-Control-U 10 (PU5)
 * Q.2971:Party-Control-N 10 (PN5)
 */
static void
pun5_t398(struct party *p)
{
	struct uniapi_drop_party_ack_indication *ind;
	struct uni_all *drop;
	struct uni_msg *api;

	MK_IE_CAUSE(p->call->uni->cause,
	    UNI_CAUSE_LOC_USER, UNI_CAUSE_RECOVER);
	ADD_CAUSE_TIMER(p->call->uni->cause, "398");
	/*
	 * Send indication to API
	 */
	ind = ALLOC_API(struct uniapi_drop_party_ack_indication, api);
	if (ind != NULL) {
		ind->drop.hdr.cref.cref = p->call->cref;
		ind->drop.hdr.cref.flag = p->call->mine;
		ind->drop.hdr.act = UNI_MSGACT_DEFAULT;
		MK_IE_EPREF(ind->drop.epref, p->epref, p->flags & PARTY_MINE);
		ind->drop.cause = p->call->uni->cause;
		uni_enq_call(p->call, SIGC_DROP_PARTY_ACK_indication,
		    0, api, NULL);
	}

	/*
	 * Send DROP PARTY ACK
	 */
	if ((drop = UNI_ALLOC()) != NULL) {
		MK_MSG_ORIG(drop, UNI_DROP_PARTY_ACK,
		    p->call->cref, !p->call->mine);
		MK_IE_EPREF(drop->u.drop_party_ack.epref,
		    p->epref, !(p->flags & PARTY_MINE));
		drop->u.drop_party_ack.cause = p->call->uni->cause;
		uni_enq_call(p->call, SIGC_SEND_DROP_PARTY_ACK, 0, NULL, drop);
	}

	uni_destroy_party(p, 0);
}

/*
 * T397
 *
 * Q.2971:Party-Control-U 7 (PU4)
 * Q.2971:Party-Control-N 7 (PN4)
 */
static void
pun4_t397(struct party *p)
{
	MK_IE_CAUSE(p->call->uni->cause, UNI_CAUSE_LOC_USER,
	    UNI_CAUSE_RECOVER);
	ADD_CAUSE_TIMER(p->call->uni->cause, "397");

	drop_partyE(p);
}

/************************************************************/

/*
 * Drop a party because of an error condition.
 * This is label E on page Party-Control-U 8/14.
 *
 * It is assumed, that the caller has constructed the cause in
 * p->call->uni->cause.
 */
static void
drop_partyE(struct party *p)
{
	struct uni_msg *api;
	struct uniapi_drop_party_indication *ind;
	struct uni_all *drop;

	/*
	 * Send indication to API
	 */
	if ((ind = ALLOC_API(struct uniapi_drop_party_indication, api)) != NULL) {
		ind->drop.hdr.cref.cref = p->call->cref;
		ind->drop.hdr.cref.flag = p->call->mine;
		ind->drop.hdr.act = UNI_MSGACT_DEFAULT;
		MK_IE_EPREF(ind->drop.epref, p->epref, p->flags & PARTY_MINE);
		ind->drop.cause = p->call->uni->cause;
		uni_enq_call(p->call, SIGC_DROP_PARTY_indication, 0, api, NULL);
	}
	TIMER_STOP_PARTY(p, t399);
	TIMER_STOP_PARTY(p, t397);
	TIMER_START_PARTY(p, t398, p->call->uni->timer398);

	if ((drop = UNI_ALLOC()) != NULL) {
		drop->u.drop_party.cause = p->call->uni->cause;
		MK_MSG_ORIG(drop, UNI_DROP_PARTY, p->call->cref, !p->call->mine);
		MK_IE_EPREF(drop->u.drop_party.epref, p->epref,
		    !(p->flags & PARTY_MINE));
		uni_enq_call(p->call, SIGC_SEND_DROP_PARTY, 0, NULL, drop);
	}

	set_party_state(p, UNI_EPSTATE_DROP_INIT);
}

/*
 * Drop party request in Px1, Px3, Px4 or Px7
 *
 * Q.2971:Party-Control-U 8
 * Q.2971:Party-Control-N 8
 */
static void
punx_drop_party_request(struct party *p, struct uni_msg *api, uint32_t cookie)
{
	struct uniapi_drop_party_request *req =
	    uni_msg_rptr(api, struct uniapi_drop_party_request *);
	struct uni_all *drop;

	if ((drop = UNI_ALLOC()) == NULL) {
		uniapi_party_error(p, UNIAPI_ERROR_NOMEM, cookie);
		uni_msg_destroy(api);
		return;
	}

	TIMER_STOP_PARTY(p, t399);
	TIMER_STOP_PARTY(p, t397);
	TIMER_START_PARTY(p, t398, p->call->uni->timer398);

	drop->u.drop_party = req->drop;
	MK_MSG_ORIG(drop, UNI_DROP_PARTY, p->call->cref, !p->call->mine);
	uni_enq_call(p->call, SIGC_SEND_DROP_PARTY, cookie, NULL, drop);

	set_party_state(p, UNI_EPSTATE_DROP_INIT);

	uni_msg_destroy(api);
	uniapi_party_error(p, UNIAPI_OK, cookie);
}

/*
 * Drop-party-ack.request in Px6
 *
 * Q.2971:Party-Control-U 9
 * Q.2971:Party-Control-N 9
 */
static void
pun6_drop_party_ack_request(struct party *p, struct uni_msg *api, uint32_t cookie)
{
	struct uniapi_drop_party_ack_request *req =
	    uni_msg_rptr(api, struct uniapi_drop_party_ack_request *);
	struct uni_all *ack;

	if ((ack = UNI_ALLOC()) == NULL) {
		uni_msg_destroy(api);
		uniapi_party_error(p, UNIAPI_ERROR_NOMEM, cookie);
		return;
	}
	ack->u.drop_party_ack = req->ack;
	MK_MSG_ORIG(ack, UNI_DROP_PARTY_ACK, p->call->cref, !p->call->mine);
	uni_enq_call(p->call, SIGC_SEND_DROP_PARTY_ACK, cookie, NULL, ack);

	stop_all_party_timers(p);

	uni_msg_destroy(api);
	uniapi_party_error(p, UNIAPI_OK, cookie);

	uni_destroy_party(p, 0);
}
/************************************************************/
/*
 * Party status enquiry request from API or call-control
 *
 * Q.2971:Party-Control-U 12
 * Q.2971:Party-Control-N 12
 */
static void
punx_status_enquiry_request(struct party *p, uint32_t cookie)
{
	struct uni_all *enq;

	if((enq = UNI_ALLOC()) == NULL) {
		uniapi_party_error(p, UNIAPI_ERROR_NOMEM, cookie);
		return;
	}
	MK_IE_EPREF(enq->u.status_enq.epref, p->epref,
	    !(p->flags & PARTY_MINE));
	MK_MSG_ORIG(enq, UNI_STATUS_ENQ, p->call->cref, !p->call->mine);
	uni_enq_call(p->call, SIGC_SEND_STATUS_ENQ, cookie, NULL, enq);

	uniapi_party_error(p, UNIAPI_OK, cookie);
}

/*
 * STATUS in any state except PU5/PN5
 *
 * Q.2971:Party-Control-U 12
 * Q.2971:Party-Control-N 12
 */
static void
punx_status(struct party *p, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_drop_party_ack_indication *ind;
	struct uni_msg *api;

	if (u->u.status.epstate.state == UNI_EPSTATE_NULL) {
		/* should not happend */
		ind = ALLOC_API(struct uniapi_drop_party_ack_indication, api);
		if (ind != NULL) {
			ind->drop.hdr = u->u.hdr;
			ind->drop.cause = u->u.status.cause;
			ind->drop.epref = u->u.status.epref;
			uni_enq_call(p->call, SIGC_DROP_PARTY_ACK_indication,
			    0, api, NULL);
		}
		stop_all_party_timers(p);

		uni_destroy_party(p, 0);
	} else {
		if (epstate_compat(p, u->u.status.epstate.state)) {
			if(u->u.status.cause.cause == UNI_CAUSE_MANDAT ||
			   u->u.status.cause.cause == UNI_CAUSE_MTYPE_NIMPL ||
			   u->u.status.cause.cause == UNI_CAUSE_IE_NIMPL ||
			   u->u.status.cause.cause == UNI_CAUSE_IE_INV) {
				MK_IE_CAUSE(p->call->uni->cause,
				    UNI_CAUSE_LOC_USER,
				    UNI_CAUSE_UNSPEC);
				drop_partyE(p);
			}
		} else {
			MK_IE_CAUSE(p->call->uni->cause,
			    UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_MSG_INCOMP);
			drop_partyE(p);
		}
	}

	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * STATUS in PU5/PN5
 *
 * Q.2971:Party-Control-U 10
 * Q.2971:Party-Control-N 10
 */
static void
pun5_status(struct party *p, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_drop_party_ack_indication *ind;
	struct uni_msg *api;

	if (u->u.status.epstate.state == UNI_EPSTATE_NULL) {
		ind = ALLOC_API(struct uniapi_drop_party_ack_indication, api);
		if (ind != NULL) {
			ind->drop.hdr = u->u.hdr;
			ind->drop.cause = u->u.status.cause;
			ind->drop.epref = u->u.status.epref;
			uni_enq_call(p->call, SIGC_DROP_PARTY_ACK_indication,
			    0, api, NULL);
		}
		TIMER_STOP_PARTY(p, t398);

		uni_destroy_party(p, 0);
	}

	uni_msg_destroy(m);
	UNI_FREE(u);
}

/************************************************************/

void
uni_sig_party(struct party *p, enum party_sig sig, uint32_t cookie,
    struct uni_msg *msg, struct uni_all *u)
{
	if (sig >= SIGP_END) {
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "Signal %d outside of range to Party-Control", sig);
		if (msg)
			uni_msg_destroy(msg);
		if (u)
			UNI_FREE(u);
		return;
	}
	VERBOSE(p->call->uni, UNI_FAC_CALL, 1,
	    "Signal %s in state %u of party %u/%s (call %u/%s in state %s)"
	    "; cookie %u", party_sigs[sig], p->state, p->epref,
	    (p->flags & PARTY_MINE) ? "mine" : "his", p->call->cref,
	    p->call->mine ? "mine" : "his", callstates[p->call->cstate].name,
	    cookie);

	switch (sig) {

	  case SIGP_PARTY_DELETE:
		PARTY_FREE(p);
		break;

	  /*
	   * Messages
	   */
	  case SIGP_SETUP:
		if (p->state == UNI_EPSTATE_NULL) {
			/* Q.2971:Call-Control-U 3/13 */
			/* Q.2971:Call-Control-N 3/13 */
			set_party_state(p, UNI_EPSTATE_ADD_RCVD);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "SETUP in ps=%u", p->state);
		break;

	  case SIGP_ALERTING:
		if (p->state == UNI_EPSTATE_ADD_INIT) {
			/* Q.2971:Call-Control-U 14 */
			/* Q.2971:Call-Control-N 5 */
			TIMER_START_PARTY(p, t397, p->call->uni->timer397);
			set_party_state(p, UNI_EPSTATE_ALERT_RCVD);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "ALERTING in ps=%u", p->state);
		break;

	  case SIGP_CONNECT:
		if (p->state == UNI_EPSTATE_ADD_INIT) {
			/* Q.2971:Call-Control-U 4/13 */
			TIMER_STOP_PARTY(p, t399);
			set_party_state(p, UNI_EPSTATE_ACTIVE);
			break;
		}
		if (p->state == UNI_EPSTATE_ALERT_RCVD) {
			/* Q.2971:Call-Control-U 7/13 */
			TIMER_STOP_PARTY(p, t397);
			set_party_state(p, UNI_EPSTATE_ACTIVE);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "CONNECT in ps=%u", p->state);
		break;

	  case SIGP_CONNECT_ACK:
		if (p->state == UNI_EPSTATE_ADD_RCVD ||
		    p->state == UNI_EPSTATE_ALERT_DLVD) {
			/* Q.2971:Call-Control-U 6/13 */
			/* Q.2971:Call-Control-U 7/13 */
			p->flags &= ~PARTY_CONNECT;
			set_party_state(p, UNI_EPSTATE_ACTIVE);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "CONNECT in ps=%u", p->state);
		break;

	  case SIGP_RELEASE:
		if (p->state == UNI_EPSTATE_DROP_INIT) {
			/* Q.2971:Party-Control-U 10/14 */
			/* Q.2971:Party-Control-N 10/14 */
			TIMER_STOP_PARTY(p, t398);
			uni_destroy_party(p, 0);
			break;
		}
		/* Q.2971:Party-Control-U 11/14 */
		/* Q.2971:Party-Control-N 11/14 */
		TIMER_STOP_PARTY(p, t397);
		TIMER_STOP_PARTY(p, t399);
		uni_destroy_party(p, 0);
		break;

	  case SIGP_RELEASE_COMPL:
		/* Q.2971:Party-Control-U 11/14 */
		/* Q.2971:Party-Control-N 11/14 */
		stop_all_party_timers(p);
		uni_destroy_party(p, 0);
		break;

	  case SIGP_RELEASE_confirm:
		/* not in the SDLs */
		stop_all_party_timers(p);
		uni_destroy_party(p, 0);
		break;

	  case SIGP_RELEASE_request:
		if (p->state == UNI_EPSTATE_DROP_INIT) {
			/* Q.2971:Party-Control-U 10 */
			/* Q.2971:Party-Control-N 10 */
			uni_destroy_party(p, 0);
			break;
		}
		/* Q.2971:Party-Control-U 11 */
		/* Q.2971:Party-Control-N 11 */
		TIMER_STOP_PARTY(p, t397);
		TIMER_STOP_PARTY(p, t399);
		uni_destroy_party(p, 0);
		break;

	  case SIGP_RELEASE_response:
		/* Q.2971:Party-Control-U 11 */
		/* Q.2971:Party-Control-N 11 */
		stop_all_party_timers(p);
		uni_destroy_party(p, 0);
		break;

	  case SIGP_ADD_PARTY:
		if (p->state == UNI_EPSTATE_NULL) {
			/* Q.2971:Party-Control-U 3 PU0 */
			/* Q.2971:Party-Control-N 3 PN0 */
			pun0_add_party(p, msg, u);
			break;
		}
		if (p->state == UNI_EPSTATE_ADD_RCVD) {
			/* Q.2971:Party-Control-U 6 PU2 */
			/* Q.2971:Party-Control-N 6 PN2 */
			uni_msg_destroy(msg);
			UNI_FREE(u);
			break;
		}
		uni_bad_message(p->call, u, UNI_CAUSE_MSG_INCOMP,
		    &u->u.add_party.epref, p->state);
		uni_msg_destroy(msg);
		UNI_FREE(u);
		break;

	  case SIGP_PARTY_ALERTING:
		if (p->state == UNI_EPSTATE_ADD_INIT) {
			/* Q.2971:Party-Control-U 14 */
			/* Q.2971:Party-Control-N 5 */
			pun1_party_alerting(p, msg, u);
			break;
		}
		uni_bad_message(p->call, u, UNI_CAUSE_MSG_INCOMP,
		    &u->u.party_alerting.epref, p->state);
		uni_msg_destroy(msg);
		UNI_FREE(u);
		break;

	  case SIGP_ADD_PARTY_ACK:
		if (p->state == UNI_EPSTATE_ADD_INIT ||
		    p->state == UNI_EPSTATE_ALERT_RCVD) {
			/* Q.2971:Party-Control-U 4 (PU1) */
			/* Q.2971:Party-Control-U 7 (PU4) */
			/* Q.2971:Party-Control-N 4 (PN1) */
			/* Q.2971:Party-Control-N 7 (PN4) */
			pun1pun4_add_party_ack(p, msg, u);
			break;
		}
		uni_bad_message(p->call, u, UNI_CAUSE_MSG_INCOMP,
		    &u->u.add_party_ack.epref, p->state);
		uni_msg_destroy(msg);
		UNI_FREE(u);
		break;

	  case SIGP_ADD_PARTY_REJ:
		if (p->state == UNI_EPSTATE_ADD_INIT) {
			/* Q.2971:Party-Control-U 4 (PU1) */
			/* Q.2971:Party-Control-N 4 (PN1) */
			pun1_add_party_rej(p, msg, u);
			break;
		}
		if (p->state == UNI_EPSTATE_DROP_INIT) {
			/* Q.2971:Party-Control-U 10 (PU5) */
			/* Q.2971:Party-Control-N 10 (PN5) */
			pun5_add_party_rej(p, msg, u);
			break;
		}
		uni_bad_message(p->call, u, UNI_CAUSE_MSG_INCOMP,
		    &u->u.add_party_rej.epref, p->state);
		uni_msg_destroy(msg);
		UNI_FREE(u);
		break;

	  case SIGP_DROP_PARTY_ACK:
		/* Q.2971:Party-Control-U 8 */
		/* Q.2971:Party-Control-N 8 */
		punx_drop_party_ack(p, msg, u);
		break;

	  case SIGP_DROP_PARTY:
		if (p->state == UNI_EPSTATE_DROP_INIT)
			/* Q.2971:Party-Control-U 10 */
			/* Q.2971:Party-Control-N 10 */
			pun5_drop_party(p, msg, u);
		else
			/* Q.2971:Party-Control-U 9 */
			/* Q.2971:Party-Control-N 9 */
			punx_drop_party(p, msg, u);
		break;

	  case SIGP_STATUS:
		if (p->state == UNI_EPSTATE_DROP_INIT)
			/* Q.2971:Party-Control-U 10 */
			/* Q.2971:Party-Control-N 10 */
			pun5_status(p, msg, u);
		else
			/* Q.2971:Party-Control-U 12 */
			/* Q.2971:Party-Control-N 12 */
			punx_status(p, msg, u);
		break;

	  /*
	   * User
	   */
	  case SIGP_SETUP_request:
		if (p->state == UNI_EPSTATE_NULL) {
			/* Q.2971:Party-Control-U 3 */
			/* Q.2971:Party-Control-N 3 */
			set_party_state(p, UNI_EPSTATE_ADD_INIT);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "SETUP.request in ps=%u", p->state);
		uniapi_party_error(p, UNIAPI_ERROR_BAD_EPSTATE, cookie);
		break;

	  case SIGP_SETUP_response:
		if (p->state == UNI_EPSTATE_ADD_RCVD ||
		    p->state == UNI_EPSTATE_ALERT_DLVD) {
			/* Q.2971:Party-Control-N 6 (PN2) */
			/* Q.2971:Party-Control-N 7 (PN3) */
			set_party_state(p, UNI_EPSTATE_ACTIVE);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "SETUP.response in ps=%u", p->state);
		uniapi_party_error(p, UNIAPI_ERROR_BAD_EPSTATE, cookie);
		break;

	  case SIGP_SETUP_COMPL_request:
		if (p->state == UNI_EPSTATE_ADD_INIT) {
			/* Q.2971:Party-Control-N 4 */
			TIMER_STOP_PARTY(p, t399);
			set_party_state(p, UNI_EPSTATE_ACTIVE);
			break;
		}
		if (p->state == UNI_EPSTATE_ALERT_RCVD) {
			/* Q.2971:Party-Control-N 7 */
			TIMER_STOP_PARTY(p, t397);
			set_party_state(p, UNI_EPSTATE_ACTIVE);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "SETUP_COMPL.request in ps=%u", p->state);
		uniapi_party_error(p, UNIAPI_ERROR_BAD_EPSTATE, cookie);
		break;

	  case SIGP_ADD_PARTY_request:
		if (p->state == UNI_EPSTATE_NULL) {
			/* Q.2971:Party-control-U 3 (PU0) */
			/* Q.2971:Party-control-N 3 (PN0) */
			pun0_add_party_request(p, msg, cookie);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "Add-party.request in ps=%u", p->state);
		uniapi_party_error(p, UNIAPI_ERROR_BAD_EPSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGP_ALERTING_request:
		/* Q.2971:Party-Control-U 6 (PU2) */
		/* Q.2971:Party-Control-N 6 (PN2) */
		set_party_state(p, UNI_EPSTATE_ALERT_DLVD);
		break;

	  case SIGP_PARTY_ALERTING_request:
		if (p->state == UNI_EPSTATE_ADD_RCVD) {
			/* Q.2971:Party-Control-U 6 (PU2) */
			/* Q.2971:Party-Control-N 6 (PN2) */
			pun2_party_alerting_request(p, msg, cookie);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "Party-alerting.request in ps=%u", p->state);
		uniapi_party_error(p, UNIAPI_ERROR_BAD_EPSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGP_ADD_PARTY_ACK_request:
		if (p->state == UNI_EPSTATE_ADD_RCVD ||
		    p->state == UNI_EPSTATE_ALERT_DLVD) {
			/* Q.2971:Party-Control-U 6 PU2 */
			/* Q.2971:Party-Control-U 7 PU3 */
			/* Q.2971:Party-Control-N 6 PN2 */
			/* Q.2971:Party-Control-N 7 PN3 */
			punx_add_party_ack_request(p, msg, cookie);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "Add-party-ack.request in ps=%u", p->state);
		uniapi_party_error(p, UNIAPI_ERROR_BAD_EPSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGP_ADD_PARTY_REJ_request:
		if (p->state == UNI_EPSTATE_ADD_RCVD) {
			/* Q.2971:Party-Control-U 6 PU2 */
			/* Q.2971:Party-Control-N 6 PN2 */
			pun2_add_party_rej_request(p, msg, cookie);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "Add-party-rej.request in ps=%u", p->state);
		uniapi_party_error(p, UNIAPI_ERROR_BAD_EPSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGP_DROP_PARTY_request:
		if (p->state == UNI_EPSTATE_ADD_INIT ||
		    p->state == UNI_EPSTATE_ALERT_DLVD ||
		    p->state == UNI_EPSTATE_ALERT_RCVD ||
		    p->state == UNI_EPSTATE_ACTIVE) {
			/* Q.2971:Party-Control-U 8 */
			/* Q.2971:Party-Control-N 8 */
			punx_drop_party_request(p, msg, cookie);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "Drop-party.request in ps=%u", p->state);
		uniapi_party_error(p, UNIAPI_ERROR_BAD_EPSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGP_DROP_PARTY_ACK_request:
		if (p->state == UNI_EPSTATE_DROP_RCVD) {
			/* Q.2971:Party-Control-U 9 */
			/* Q.2971:Party-Control-N 9 */
			pun6_drop_party_ack_request(p, msg, cookie);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "Drop-party-ack.request in ps=%u", p->state);
		uniapi_party_error(p, UNIAPI_ERROR_BAD_EPSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGP_STATUS_ENQUIRY_request:
		/* Q.2971:Party-Control-U 12 */
		/* Q.2971:Party-Control-N 12 */
		punx_status_enquiry_request(p, cookie);
		break;

	  /*
	   * Timers
	   */
	  case SIGP_T397:
		if (p->state == UNI_EPSTATE_ALERT_RCVD) {
			/* Q.2971:Party-Control-U 7 (PU4) */
			/* Q.2971:Party-Control-N 7 (PN4) */
			pun4_t397(p);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "T397 in ps=%u", p->state);
		break;

	  case SIGP_T398:
		if (p->state == UNI_EPSTATE_DROP_INIT) {
			/* Q.2971:Party-Control-U 10 (PU5) */
			/* Q.2971:Party-Control-N 10 (PN5) */
			pun5_t398(p);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "T398 in ps=%u", p->state);
		break;

	  case SIGP_T399:
		if (p->state == UNI_EPSTATE_ADD_INIT) {
			/* Q.2971:Party-Control-U 4 (PU1) */
			/* Q.2971:Party-Control-N 4 (PN1) */
			pun1_t399(p);
			break;
		}
		VERBOSE(p->call->uni, UNI_FAC_ERR, 1,
		    "T399 in ps=%u", p->state);
		break;

	  case SIGP_END:
		break;
	}
}

static void
t397_func(struct party *p)
{
	uni_enq_party(p, SIGP_T397, 0, NULL, NULL);
}
static void
t398_func(struct party *p)
{
	uni_enq_party(p, SIGP_T398, 0, NULL, NULL);
}
static void
t399_func(struct party *p)
{
	uni_enq_party(p, SIGP_T399, 0, NULL, NULL);
}

static int
epstate_compat(struct party *p, enum uni_epstate state)
{
	if (p->state == UNI_EPSTATE_ADD_INIT ||
	    p->state == UNI_EPSTATE_ALERT_RCVD)
		if (state == UNI_EPSTATE_ADD_INIT ||
		    state == UNI_EPSTATE_ALERT_RCVD)
			return (0);
	if (p->state == UNI_EPSTATE_ADD_RCVD ||
	    p->state == UNI_EPSTATE_ALERT_DLVD)
		if (state == UNI_EPSTATE_ADD_RCVD ||
		    state == UNI_EPSTATE_ALERT_DLVD)
			return (0);
	return (1);
}
