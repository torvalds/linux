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
 * $Begemot: libunimsg/netnatm/sig/sig_call.c,v 1.65 2004/08/05 07:11:00 brandt Exp $
 *
 * Call instance handling
 *
 * Note:
 *	In all functions that handle messages from the user or from
 *	the SAAL, commit memory allocation always at the begin of the
 *	function. If allocation fails, ignore saal messages and
 *	respond with an error to user messages.
 */

#include <netnatm/unimsg.h>
#include <netnatm/saal/sscfudef.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/sig/uni.h>

#include <netnatm/sig/unipriv.h>
#include <netnatm/sig/unimkmsg.h>
#include <netnatm/sig/unimsgcpy.h>

static enum call_state state_compat(struct call *, enum uni_callstate);
static void respond_drop_party_ack(struct call *, struct uni_ie_epref *, u_int);


#define DEF_PRIV_SIG(NAME, FROM)	[SIG##NAME] =	"SIG"#NAME,
static const char *const call_sigs[] = {
	DEF_CALL_SIGS
};
#undef DEF_PRIV_SIG

TIMER_FUNC_CALL(t308, t308_func)
TIMER_FUNC_CALL(t303, t303_func)
TIMER_FUNC_CALL(t301, t301_func)
TIMER_FUNC_CALL(t310, t310_func)
TIMER_FUNC_CALL(t313, t313_func)
TIMER_FUNC_CALL(t322, t322_func)

const struct callstates callstates[] = {
	[CALLST_NULL] =	{ "NU0",	UNI_CALLSTATE_U0 },
	[CALLST_U1] =	{ "U1",		UNI_CALLSTATE_U1 },
	[CALLST_U3] =	{ "U3",		UNI_CALLSTATE_U3 },
	[CALLST_U4] =	{ "U4",		UNI_CALLSTATE_U4 },
	[CALLST_U6] =	{ "U6",		UNI_CALLSTATE_U6 },
	[CALLST_U7] =	{ "U7",		UNI_CALLSTATE_U7 },
	[CALLST_U8] =	{ "U8",		UNI_CALLSTATE_U8 },
	[CALLST_U9] =	{ "U9",		UNI_CALLSTATE_U9 },
	[CALLST_U10] =	{ "U10",	UNI_CALLSTATE_U10 },
	[CALLST_U11] =	{ "U11",	UNI_CALLSTATE_U11 },
	[CALLST_U12] =	{ "U12",	UNI_CALLSTATE_U12 },
	[CALLST_N1] =	{ "N1",		UNI_CALLSTATE_N1 },
	[CALLST_N3] =	{ "N3",		UNI_CALLSTATE_N3 },
	[CALLST_N4] =	{ "N4",		UNI_CALLSTATE_N4 },
	[CALLST_N6] =	{ "N6",		UNI_CALLSTATE_N6 },
	[CALLST_N7] =	{ "N7",		UNI_CALLSTATE_N7 },
	[CALLST_N8] =	{ "N8",		UNI_CALLSTATE_N8 },
	[CALLST_N9] =	{ "N9",		UNI_CALLSTATE_N9 },
	[CALLST_N10] =	{ "N10",	UNI_CALLSTATE_N10 },
	[CALLST_N11] =	{ "N11",	UNI_CALLSTATE_N11 },
	[CALLST_N12] =	{ "N12",	UNI_CALLSTATE_N12 },
};

static void unx_send_add_party_rej(struct call *c, struct uni_all *u);

static __inline void
set_call_state(struct call *c, enum call_state state)
{
	ASSERT(state == CALLST_NULL ||
	    (c->uni->proto == UNIPROTO_UNI40U &&
	     (state >= CALLST_U1 && state <= CALLST_U12)) ||
	    (c->uni->proto == UNIPROTO_UNI40N &&
	     (state >= CALLST_N1 && state <= CALLST_N12)),
	    ("setting wrong callstate for proto %u: %u", c->uni->proto, state));

	if (c->cstate != state) {
		VERBOSE(c->uni, UNI_FAC_CALL, 1, "call %d/%d %s -> %s",
		    c->cref, c->mine, callstates[c->cstate].name,
		    callstates[state].name);
		c->cstate = state;
	}
}

static enum uni_callstate
map_callstate(enum call_state state)
{
	return (callstates[state].ext);
}

/*
 * Find the call. Assume, that the cref is one of a message just received.
 * That is, if the call reference flag is 0 it is his call, if it is 1 it
 * is my call.
 */
struct call *
uni_find_call(struct uni *uni, struct uni_cref *cref)
{
	struct call *c;

	TAILQ_FOREACH(c, &uni->calls, link)
		if (c->cref == cref->cref && (!c->mine == !cref->flag))
			return (c);
	return (NULL);
}
struct call *
uni_find_callx(struct uni *uni, u_int cref, u_int mine)
{
	struct call *c;

	TAILQ_FOREACH(c, &uni->calls, link)
		if (c->cref == cref && !c->mine == !mine)
			return (c);
	return (NULL);
}

/*
 * Create a new call instance. The type must be set by the caller.
 */
struct call *
uni_create_call(struct uni *uni, u_int cref, u_int mine, uint32_t cookie)
{
	struct call *c;
	struct uniapi_call_created *ind;
	struct uni_msg *api;

	if ((c = CALL_ALLOC()) == NULL)
		return (NULL);

	if ((ind = ALLOC_API(struct uniapi_call_created, api)) == NULL) {
		CALL_FREE(c);
		return (NULL);
	}
	ind->cref.cref = cref;
	ind->cref.flag = mine;

	c->uni = uni;
	c->type = CALL_NULL;
	c->cref = cref;
	c->mine = mine;
	c->cstate = CALLST_NULL;
	TAILQ_INIT(&c->parties);

	TIMER_INIT_CALL(c, t301);
	TIMER_INIT_CALL(c, t303);
	TIMER_INIT_CALL(c, t308);
	TIMER_INIT_CALL(c, t310);
	TIMER_INIT_CALL(c, t313);
	TIMER_INIT_CALL(c, t322);

	TAILQ_INSERT_HEAD(&uni->calls, c, link);

	uni->funcs->uni_output(uni, uni->arg, UNIAPI_CALL_CREATED, cookie, api);

	VERBOSE(c->uni, UNI_FAC_CALL, 1, "created call %u/%s",
	    c->cref, c->mine ? "mine" : "his");

	return (c);
}

struct call *
uni_create_new_call(struct uni *uni, uint32_t cookie)
{
	struct call *c;
	uint32_t old = uni->cref_alloc++;

  again:
	if (uni->cref_alloc == (1 << 23))
		uni->cref_alloc = 1;
	if (uni->cref_alloc == old)
		return (NULL);	/* all crefs exhausted!!! */
	TAILQ_FOREACH(c, &uni->calls, link)
		if (c->mine && c->cref == uni->cref_alloc) {
			uni->cref_alloc++;
			goto again;
		}
	return (uni_create_call(uni, uni->cref_alloc, 1, cookie));
}

/*
 * Assume timers are all stopped. Memory is not actually freed unless
 * the reference count drops to 0.
 * This function is assumed to remove the call from the parent UNI's 
 * call queue.
 */
void
uni_destroy_call(struct call *c, int really)
{
	struct uniapi_call_destroyed *ind;
	struct uni_msg *api;
	struct party *p;

	VERBOSE(c->uni, UNI_FAC_CALL, 1, "destroying call %u/%s",
	    c->cref, c->mine ? "mine" : "his");

	TIMER_DESTROY_CALL(c, t301);
	TIMER_DESTROY_CALL(c, t303);
	TIMER_DESTROY_CALL(c, t308);
	TIMER_DESTROY_CALL(c, t310);
	TIMER_DESTROY_CALL(c, t313);
	TIMER_DESTROY_CALL(c, t322);
	TAILQ_REMOVE(&c->uni->calls, c, link);

	uni_delsig(c->uni, SIG_CALL, c, NULL);

	while ((p = TAILQ_FIRST(&c->parties)) != NULL) {
		TAILQ_REMOVE(&c->parties, p, link);
		uni_destroy_party(p, really);
	}

	if (!really) {
		ind = ALLOC_API(struct uniapi_call_destroyed, api);
		if (ind != NULL) {
			ind->cref.cref = c->cref;
			ind->cref.flag = c->mine;

			uni_enq_coord(c->uni, SIGO_CALL_DESTROYED, 0, api);
		}

		uni_enq_call(c, SIGC_CALL_DELETE, 0, NULL, NULL);
		return;
	}

	CALL_FREE(c);
}

static void
allocate_epref(struct call *c, struct uni_ie_epref *epref)
{
	struct party *p;
	uint32_t old = c->epref_alloc++;

  again:
	if (c->epref_alloc == (1 << 15))
		c->epref_alloc = 0;
	if (c->epref_alloc == old)
		return;		/* all crefs exhausted!!! */
	TAILQ_FOREACH(p, &c->parties, link)
		if (p->epref == c->epref_alloc) {
			c->epref_alloc++;
			goto again;
		}
	IE_SETPRESENT(*epref);
	epref->flag = 0;
	epref->epref = c->epref_alloc;

	epref->h.coding = UNI_CODING_ITU;
	epref->h.act = UNI_IEACT_DEFAULT;
}

static void
reset_all_timers(struct call *c)
{
	TIMER_STOP_CALL(c, t301);
	TIMER_STOP_CALL(c, t303);
	TIMER_STOP_CALL(c, t308);
	TIMER_STOP_CALL(c, t310);
	TIMER_STOP_CALL(c, t313);
	TIMER_STOP_CALL(c, t322);
}

/*
 * Initiate call clearing because of a problem. This is label D in 
 * the SDLs and is called from many places.
 * The call must have constructed the cause IE in struct call.
 *
 * Q.2971:Call-Control-U 27/39
 * Q.2971:Call-Control-N 28/39
 *
 * Memory problems are handled differently here: we simply ignore them
 * by not sending messages or user indications. Because of T308 we
 * may be lucky to send the message in a second run.
 *
 * It is assumed, that the cause for the release is constructed by
 * the calling function in uni->cause.
 */
static void
clear_callD(struct call *c)
{
	struct uni_msg *api;
	struct uniapi_release_indication *ind;
	struct party *p;
	struct uni_all *rel;

	/*
	 * Send indication to API
	 */
	if ((ind = ALLOC_API(struct uniapi_release_indication, api)) != NULL) {
		ind->release.hdr.cref.cref = c->cref;
		ind->release.hdr.cref.flag = c->mine;
		ind->release.hdr.act = UNI_MSGACT_DEFAULT;
		ind->release.cause[0] = c->uni->cause;

		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_RELEASE_indication, 0, api);
	}

	reset_all_timers(c);

	if (c->type == CALL_LEAF || c->type == CALL_ROOT) {
		TAILQ_FOREACH(p, &c->parties, link) {
			uni_enq_party(p, SIGP_RELEASE_request, 0, NULL, NULL);
		}
	}

	memset(&c->msg_release, 0, sizeof(c->msg_release));
	c->msg_release.cause[0] = c->uni->cause;

	if ((rel = UNI_ALLOC()) != NULL) {
		rel->u.release = c->msg_release;
		MK_MSG_ORIG(rel, UNI_RELEASE, c->cref, !c->mine);
		(void)uni_send_output(rel, c->uni);
		UNI_FREE(rel);
	}

	TIMER_START_CALL(c, t308, c->uni->timer308);
	c->cnt308 = 0;

	if (c->uni->proto == UNIPROTO_UNI40N)
		set_call_state(c, CALLST_N12);
	else
		set_call_state(c, CALLST_U11);
}


/**********************************************************************/
/*
 * SETUP message in state NULL
 *
 * Q.2971:Call-Control-U 4/39
 * Q.2971:Call-Control-N 4/39
 */
static void
un0_setup(struct call *c, struct uni_msg *m, struct uni_all *u,
    enum call_state new_state)
{
	struct uni_all *resp;
	struct party *p;
	struct uniapi_setup_indication *ind;
	struct uni_msg *api;
	enum verify v;

	if ((ind = ALLOC_API(struct uniapi_setup_indication, api)) == NULL) {
  clear:
		uni_destroy_call(c, 0);
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}

	/*
	 * Analyze message
	 */
	(void)uni_decode_body(m, u, &c->uni->cx);
	MANDATE_IE(c->uni, u->u.setup.bearer, UNI_IE_BEARER);
	MANDATE_IE(c->uni, u->u.setup.traffic, UNI_IE_TRAFFIC);
	MANDATE_IE(c->uni, u->u.setup.called, UNI_IE_CALLED);

	/*
	 * UNI4.0: 9.1.1.2 Notes 2/3
	 */
	if (!IE_ISPRESENT(u->u.setup.qos))
		MANDATE_IE(c->uni, u->u.setup.exqos, UNI_IE_EXQOS);
	if (!IE_ISPRESENT(u->u.setup.exqos))
		MANDATE_IE(c->uni, u->u.setup.qos, UNI_IE_QOS);

	/*
	 * Q.2971
	 */
	if (IE_ISGOOD(u->u.setup.bearer) &&
	    u->u.setup.bearer.cfg == UNI_BEARER_MP) {
		if (IE_ISGOOD(u->u.setup.epref) &&
		   u->u.setup.epref.flag == 1) {
			IE_SETERROR(u->u.setup.epref);
			(void)UNI_SAVE_IERR(&c->uni->cx, UNI_IE_EPREF,
			    u->u.setup.epref.h.act, UNI_IERR_BAD);
		}
		uni_mandate_epref(c->uni, &u->u.setup.epref);
	}

	v = uni_verify(c->uni, u->u.hdr.act);
	switch (v) {

	  case VFY_RAI:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    UNI_CALLSTATE_U0, NULL, 0);
		/* FALLTHRU */
	  case VFY_I:
		uni_msg_destroy(api);
		goto clear;

	  case VFY_RAIM:
	  case VFY_CLR:
		if ((resp = UNI_ALLOC()) != NULL) {
			MK_MSG_RESP(resp, UNI_RELEASE_COMPL, &u->u.hdr.cref);
			uni_vfy_collect_ies(c->uni);
			resp->u.release_compl.cause[0] = c->uni->cause;
			uni_send_output(resp, c->uni);
			UNI_FREE(resp);
		}
		uni_msg_destroy(api);
		goto clear;

	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(new_state), NULL, 0);
		/* FALLTHRU */
	  case VFY_OK:
		break;
	}

	if (u->u.setup.bearer.cfg == UNI_BEARER_P2P) {
		c->type = CALL_P2P;

	} else {
		c->type = CALL_LEAF;
		if ((p = uni_create_party(c, &u->u.setup.epref)) == NULL) {
			uni_msg_destroy(api);
			goto clear;
		}
		uni_enq_party(p, SIGP_SETUP, 0, NULL, NULL);
	}

	ind->setup.hdr = u->u.hdr;
	copy_msg_setup(&u->u.setup, &ind->setup);
	c->uni->funcs->uni_output(c->uni, c->uni->arg,
	    UNIAPI_SETUP_indication, 0, api);

	uni_msg_destroy(m);
	UNI_FREE(u);

	set_call_state(c, new_state);
}

/*
 * Setup.request from user
 *
 * Q.2971:Call-Control-U 4/39 (U0)
 * Q.2971:Call-Control-N 4/39 (N0)
 */
static void
un0_setup_request(struct call *c, struct uni_msg *m, uint32_t cookie,
    enum call_state new_state)
{
	struct uniapi_setup_request *arg =
	    uni_msg_rptr(m, struct uniapi_setup_request *);
	struct uni_setup *setup = &arg->setup;
	struct uni_all *out;
	struct party *p;

	if (!IE_ISGOOD(setup->bearer)) {
		uni_msg_destroy(m);
		uniapi_call_error(c, UNIAPI_ERROR_MISSING_IE, cookie);
		uni_destroy_call(c, 0);
		return;
	}
	if ((out = UNI_ALLOC()) == NULL) {
		uni_msg_destroy(m);
		uniapi_call_error(c, UNIAPI_ERROR_NOMEM, cookie);
		uni_destroy_call(c, 0);
		return;
	}

	c->msg_setup = *setup;

	if (IE_ISGOOD(setup->connid))
		c->connid = setup->connid;

	if (setup->bearer.cfg == UNI_BEARER_P2P) {
		c->type = CALL_P2P;
	} else {
		c->type = CALL_ROOT;

		/*
		 * If the user didn't specify a endpoint reference,
		 * use 0. Use IE_IGNORE accoring to Appendix II Q.2971
		 */
		if (!IE_ISPRESENT(c->msg_setup.epref)) {
			MK_IE_EPREF(c->msg_setup.epref, 0, 0);
			if (c->uni->proto == UNIPROTO_UNI40N)
				c->msg_setup.epref.h.act = UNI_IEACT_IGNORE;

		} else if (!IE_ISGOOD(c->msg_setup.epref)) {
			uni_msg_destroy(m);
			uniapi_call_error(c, UNIAPI_ERROR_BAD_IE, cookie);
			uni_destroy_call(c, 0);
			return;
		}
		if ((p = uni_create_partyx(c, 0, 1, cookie)) == NULL) {
			uni_msg_destroy(m);
			uniapi_call_error(c, UNIAPI_ERROR_NOMEM, cookie);
			uni_destroy_call(c, 0);
			return;
		}
		uni_enq_party(p, SIGP_SETUP_request, cookie, NULL, NULL);
	}

	uni_msg_destroy(m);

	out->u.setup = c->msg_setup;
	MK_MSG_ORIG(out, UNI_SETUP, c->cref, !c->mine);
	(void)uni_send_output(out, c->uni);
	UNI_FREE(out);

	TIMER_START_CALL(c, t303, c->uni->timer303);
	c->cnt303 = 0;

	set_call_state(c, new_state);

	uniapi_call_error(c, UNIAPI_OK, cookie);
}

/*
 * CALL PROCEEDING message
 *
 * Q.2971:Call-Control-U 6/39 (in U1)
 * Q.2971:Call-Control-N 11/39 (in N6)
 */
static void
u1n6_call_proc(struct call *c, struct uni_msg *m, struct uni_all *u,
    enum call_state new_state)
{
	struct uni_call_proc *cp = &u->u.call_proc;
	struct uniapi_proceeding_indication *ind;
	struct uni_msg *api;

	ind = ALLOC_API(struct uniapi_proceeding_indication, api);
	if (ind == NULL) {
  ignore:
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}
	/*
	 * Analyze message
	 */
	(void)uni_decode_body(m, u, &c->uni->cx);
	if (!IE_ISPRESENT(c->connid) && !IE_ISGOOD(cp->connid))
		uni_mandate_ie(c->uni, UNI_IE_CONNID);

	/*
	 * Q.2971: L3MU_01_03 requests us to ignore the message if
	 * the EPREF is missing.
	 */
	if (c->msg_setup.bearer.cfg == UNI_BEARER_MP &&
	    IE_ISPRESENT(c->msg_setup.epref)) {
		if (!IE_ISPRESENT(cp->epref))
			uni_mandate_ie(c->uni, UNI_IE_EPREF);				\

		else if (IE_ISGOOD(cp->epref) &&
		    (cp->epref.flag != 1 ||
		     cp->epref.epref != c->msg_setup.epref.epref)) {
			IE_SETERROR(cp->epref);
			(void)UNI_SAVE_IERR(&c->uni->cx, UNI_IE_EPREF,
			    cp->epref.h.act, UNI_IERR_BAD);
		}
	}

	switch (uni_verify(c->uni, u->u.hdr.act)) {

	  case VFY_CLR:
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
		/* FALLTHRU */
	  case VFY_I:
		uni_msg_destroy(api);
		goto ignore;

	  case VFY_RAIM:
	  case VFY_RAI:
	  report:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), NULL, 0);
		uni_msg_destroy(api);
		goto ignore;

	  case VFY_RAP:
	  case VFY_RAPU:
		if (c->type == CALL_ROOT && !IE_ISGOOD(cp->epref))
			goto report;
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(new_state), NULL, 0);
		/* FALLTHRU */
	  case VFY_OK:
		break;
	}

	TIMER_STOP_CALL(c, t303);

	if (IE_ISGOOD(cp->connid))
		c->connid = cp->connid;

	ind->call_proc.hdr = u->u.hdr;
	copy_msg_call_proc(cp, &ind->call_proc);
	c->uni->funcs->uni_output(c->uni, c->uni->arg,
	    UNIAPI_PROCEEDING_indication, 0, api);

	TIMER_START_CALL(c, t310, c->uni->timer310);

	uni_msg_destroy(m);
	UNI_FREE(u);

	set_call_state(c, new_state);
}

/*
 * T303 tick.
 *
 * Q.2971:Call-Control-U 6/39
 * Q.2971:Call-Control-N 11/39
 */
static void
u1n6_t303(struct call *c)
{
	struct uni_all *msg;
	struct uniapi_release_confirm *conf;
	struct uni_msg *api;

	VERBOSE(c->uni, UNI_FAC_TIMEOUT, 1, "call %u/%s T303 tick %d",
	    c->cref, c->mine ? "mine" : "his", c->cnt303 + 1);

	if (++c->cnt303 < c->uni->init303) {
		if ((msg = UNI_ALLOC()) != NULL) {
			msg->u.setup = c->msg_setup;
			MK_MSG_ORIG(msg, UNI_SETUP, c->cref, !c->mine);
			(void)uni_send_output(msg, c->uni);
			UNI_FREE(msg);
		}
		TIMER_START_CALL(c, t303, c->uni->timer303);
		return;
	}

	/*
	 * Send indication to API
	 */
	if ((conf = ALLOC_API(struct uniapi_release_confirm, api)) != NULL) {
		conf->release.hdr.cref.cref = c->cref;
		conf->release.hdr.cref.flag = c->mine;
		conf->release.hdr.act = UNI_MSGACT_DEFAULT;
		MK_IE_CAUSE(conf->release.cause[0], UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_NO_RESPONSE);

		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_RELEASE_confirm, 0, api);
	}

	/*
	 * send to party (there may be only one)
	 */
	if (c->type == CALL_ROOT && !TAILQ_EMPTY(&c->parties)) {
		uni_enq_party(TAILQ_FIRST(&c->parties),
		    SIGP_RELEASE_confirm, 0, NULL, NULL);
	}
	uni_destroy_call(c, 0);
}

/*
 * T310 (Call Proceeding) timer tick.
 *
 * Q.2971:Call-Control-U 7/39
 * Q.2971:Call-Control-N 17/39
 */
static void
u3n9_t310(struct call *c)
{
	VERBOSE(c->uni, UNI_FAC_TIMEOUT, 1, "call %u/%s T310 tick",
	    c->cref, c->mine ? "mine" : "his");

	MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER, UNI_CAUSE_NO_RESPONSE);
	clear_callD(c);
}

/*
 * T301 (Alerting) timer tick.
 *
 * Q.2971:Call-Control-U Missing
 * Q.2971:Call-Control-N 14/39
 */
static void
u4n7_t301(struct call *c)
{
	VERBOSE(c->uni, UNI_FAC_TIMEOUT, 1, "call %u/%s T301 tick",
	    c->cref, c->mine ? "mine" : "his");

	MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER, UNI_CAUSE_NO_RESP_ALERT);
	clear_callD(c);
}

/*
 * ALERTING received
 *
 * Q.2971:Call-Control-U 37/39 (U1)
 * Q.2971:Call-Control-U 7/39 (U3)
 * Q.2971:Call-Control-N 9/39 (N6)
 * Q.2971:Call-Control-N 17/39 (N9)
 *
 * There are two errors in the user side SDL Annex A:
 *
 *   - the resetted timers are swapped (T310 and T303)
 *
 *   - for U1 we should go to C12, not C3 to start T301.
 */
static void
unx_alerting(struct call *c, struct uni_msg *m, struct uni_all *u,
    enum call_state new_state)
{
	struct uni_alerting *al = &u->u.alerting;
	struct uniapi_alerting_indication *ind;
	struct uni_msg *api;

	ind = ALLOC_API(struct uniapi_alerting_indication, api);
	if (ind == NULL) {
  ignore:
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}

	/*
	 * Analyze message
	 */
	(void)uni_decode_body(m, u, &c->uni->cx);
	if (!IE_ISPRESENT(c->connid) && !IE_ISGOOD(al->connid))
		uni_mandate_ie(c->uni, UNI_IE_CONNID);

	/*
	 * Q.2971: L3MU_01_04 requests us to ignore the message if the
	 * EPREF is missing.
	 */
	if (c->msg_setup.bearer.cfg == UNI_BEARER_MP &&
	    IE_ISPRESENT(c->msg_setup.epref)) {
		if (!IE_ISPRESENT(al->epref))
			uni_mandate_ie(c->uni, UNI_IE_EPREF);				\

		else if (IE_ISGOOD(al->epref) &&
		    (al->epref.flag != 1 ||
		     al->epref.epref != c->msg_setup.epref.epref)) {
			IE_SETERROR(al->epref);
			(void)UNI_SAVE_IERR(&c->uni->cx, UNI_IE_EPREF,
			    al->epref.h.act, UNI_IERR_BAD);
		}
	}

	switch (uni_verify(c->uni, u->u.hdr.act)) {

	  case VFY_CLR:
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
	  case VFY_I:
		uni_msg_destroy(api);
	  	goto ignore;

	  case VFY_RAIM:
	  case VFY_RAI:
	  report:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), NULL, 0);
		uni_msg_destroy(api);
	  	goto ignore;

	  case VFY_RAP:
	  case VFY_RAPU:
		if (c->type == CALL_ROOT && !IE_ISGOOD(al->epref))
			goto report;
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), NULL, 0);
	  case VFY_OK:
		break;
	}

	if (c->cstate == CALLST_U1 || c->cstate == CALLST_N6)
		TIMER_STOP_CALL(c, t303);
	else if (c->cstate == CALLST_U3 || c->cstate == CALLST_N9)
		TIMER_STOP_CALL(c, t310);

	if (IE_ISGOOD(al->connid))
		c->connid = al->connid;

	ind->alerting.hdr = u->u.hdr;
	copy_msg_alerting(al, &ind->alerting);

	if (c->type == CALL_LEAF || c->type == CALL_ROOT) {
		uni_enq_party(TAILQ_FIRST(&c->parties), SIGP_ALERTING,
		    0, NULL, NULL);
		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_ALERTING_indication, 0, api);
	} else {
		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_ALERTING_indication, 0, api);
		TIMER_START_CALL(c, t301, c->uni->timer301);
	}
	UNI_FREE(u);
	uni_msg_destroy(m);

	set_call_state(c, new_state);
}

/*
 * Proceeding.request from API
 *
 * Q.2971:Call-Control-U 12/39 (U6)
 * Q.2971:Call-Control-N 6/39 (N1)
 */
static void
u6n1_proceeding_request(struct call *c, struct uni_msg *m, uint32_t cookie,
    enum call_state new_state)
{
	struct uni_all *msg;
	struct uniapi_proceeding_request *arg =
	    uni_msg_rptr(m, struct uniapi_proceeding_request *);

	if ((msg = UNI_ALLOC()) == NULL) {
		uni_msg_destroy(m);
		uniapi_call_error(c, UNIAPI_ERROR_NOMEM, cookie);
		return;
	}

	if (IE_ISGOOD(arg->call_proc.connid))
		c->connid = arg->call_proc.connid;

	msg->u.call_proc = arg->call_proc;
	MK_MSG_ORIG(msg, UNI_CALL_PROC, c->cref, !c->mine);
	(void)uni_send_output(msg, c->uni);
	UNI_FREE(msg);

	set_call_state(c, new_state);

	uni_msg_destroy(m);

	uniapi_call_error(c, UNIAPI_OK, cookie);
}

/*
 * Alerting.request from API
 *
 * Q.2971:Call-Control-U 13/39 (U6)
 * Q.2971:Call-Control-U 17/39 (U9)
 * Q.2971:Call-Control-N 38/39 (N1)
 * Q.2971:Call-Control-N 7/39  (N3)
 */
static void
unx_alerting_request(struct call *c, struct uni_msg *m, uint32_t cookie,
    enum call_state new_state)
{
	struct uni_all *msg;
	struct uniapi_alerting_request *arg =
	    uni_msg_rptr(m, struct uniapi_alerting_request *);

	if ((msg = UNI_ALLOC()) == NULL) {
		uni_msg_destroy(m);
		uniapi_call_error(c, UNIAPI_ERROR_NOMEM, cookie);
		return;
	}

	if (c->type == CALL_ROOT || c->type == CALL_LEAF) {
		uni_enq_party(TAILQ_FIRST(&c->parties),
		    SIGP_ALERTING_request, cookie, NULL, NULL);
	}

	/*
	 * It's not really clear, what happens, if we send another
	 * connid in CALL_PROC and ALERTING
	 */
	if (!IE_ISGOOD(c->connid) && IE_ISGOOD(arg->alerting.connid))
		c->connid = arg->alerting.connid;

	msg->u.alerting = arg->alerting;
	MK_MSG_ORIG(msg, UNI_ALERTING, c->cref, !c->mine);
	(void)uni_send_output(msg, c->uni);
	UNI_FREE(msg);

	set_call_state(c, new_state);

	uni_msg_destroy(m);

	uniapi_call_error(c, UNIAPI_OK, cookie);
}


/*
 * Setup.response from API
 *
 * Q.2971:Call-Control-U 13/39	(U6)
 * Q.2971:Call-Control-U 14/39	(U7)
 * Q.2971:Call-Control-U 17/39	(U9)
 * Q.2971:Call-Control-N 39/39  (N1)
 * Q.2971:Call-Control-N 7/39   (N3)
 * Q.2971:Call-Control-N 8/39   (N4)
 */
static void
unx_setup_response(struct call *c, struct uni_msg *m, uint32_t cookie,
    enum call_state new_state)
{
	struct uni_all *msg;
	struct uniapi_setup_response *arg =
	    uni_msg_rptr(m, struct uniapi_setup_response *);
	struct party *p;

	if ((msg = UNI_ALLOC()) == NULL) {
		uni_msg_destroy(m);
		uniapi_call_error(c, UNIAPI_ERROR_NOMEM, cookie);
		return;
	}

	if (!IE_ISGOOD(c->connid) && IE_ISGOOD(arg->connect.connid))
		c->connid = arg->connect.connid;

	if (IE_ISGOOD(arg->connect.epref)) {
		p = uni_find_partyx(c, arg->connect.epref.epref,
		    !arg->connect.epref.flag);
		if (p == NULL) {
			uniapi_call_error(c, UNIAPI_ERROR_BAD_PARTY, cookie);
			UNI_FREE(msg);
			uni_msg_destroy(m);
			return;
		}
		/* we need to remember that we have sent the CONNECT from this
		 * party because the CONNECT ACK must move only this party
		 * into P7 */
		p->flags |= PARTY_CONNECT;

	} else if (c->type == CALL_LEAF) {
		/* XXX don't mandate if only one party */
		uniapi_call_error(c, UNIAPI_ERROR_BAD_PARTY, cookie);
		UNI_FREE(msg);
		uni_msg_destroy(m);
		return;
	}

	/* inform the parties on the network side */
	if (c->uni->proto == UNIPROTO_UNI40N && c->type == CALL_LEAF)
		TAILQ_FOREACH(p, &c->parties, link)
			uni_enq_party(p, SIGP_SETUP_response, 0, NULL, NULL);

	msg->u.connect = arg->connect;
	MK_MSG_ORIG(msg, UNI_CONNECT, c->cref, !c->mine);
	(void)uni_send_output(msg, c->uni);
	UNI_FREE(msg);

	if (c->uni->proto == UNIPROTO_UNI40U)
		TIMER_START_CALL(c, t313, c->uni->timer313);

	set_call_state(c, new_state);

	uni_msg_destroy(m);

	uniapi_call_error(c, UNIAPI_OK, cookie);
}

/*
 * Setup_complete.request
 *
 * Q.2971:Call-Control-N 15/39 (N8)
 */
static void
n8_setup_compl_request(struct call *c, struct uni_msg *m, uint32_t cookie,
    enum call_state new_state)
{
	struct uni_all *msg;
	struct uniapi_setup_complete_request *arg =
	    uni_msg_rptr(m, struct uniapi_setup_complete_request *);
	struct party *p;

	if ((msg = UNI_ALLOC()) == NULL) {
		uni_msg_destroy(m);
		uniapi_call_error(c, UNIAPI_ERROR_NOMEM, cookie);
		return;
	}

	/* inform the parties on the network side */
	if (c->uni->proto == UNIPROTO_UNI40N &&
	    (c->type == CALL_LEAF || c->type == CALL_ROOT)) {
		TAILQ_FOREACH(p, &c->parties, link)
			uni_enq_party(p, SIGP_SETUP_COMPL_request,
			    0, NULL, NULL);
	}

	msg->u.connect_ack = arg->connect_ack;
	MK_MSG_ORIG(msg, UNI_CONNECT_ACK, c->cref, !c->mine);
	(void)uni_send_output(msg, c->uni);
	UNI_FREE(msg);

	set_call_state(c, new_state);

	uni_msg_destroy(m);

	uniapi_call_error(c, UNIAPI_OK, cookie);
}

/*
 * CONNECT message
 *
 * Q.2971:Call-Control-U 7-8/39  (U3)
 * Q.2971:Call-Control-U 11/39   (U4)
 * Q.2971:Call-Control-U 37/39   (U1)
 * Q.2971:Call-Control-N 9-10/39 (N6)
 * Q.2971:Call-Control-N 14/39   (N7)
 * Q.2971:Call-Control-N 17/39   (N9)
 */
static void
unx_connect(struct call *c, struct uni_msg *m, struct uni_all *u,
    enum call_state new_state)
{
	struct uni_connect *co = &u->u.connect;
	struct uniapi_setup_confirm *conf;
	struct uni_msg *api;
	struct uni_all *ack;
	struct party *p;

	conf = ALLOC_API(struct uniapi_setup_confirm, api);
	if (conf == NULL) {
  ignore:
		UNI_FREE(u);
		uni_msg_destroy(m);
		return;
	}
	if ((ack = UNI_ALLOC()) == NULL) {
		uni_msg_destroy(api);
		goto ignore;
	}

	/*
	 * Analyze message
	 */
	(void)uni_decode_body(m, u, &c->uni->cx);
	if (!IE_ISPRESENT(c->connid) && !IE_ISGOOD(co->connid))
		uni_mandate_ie(c->uni, UNI_IE_CONNID);

	/*
	 * Q.2971: L3MU_01_05 requires the epref to be present.
	 */
	p = NULL;
	if (c->msg_setup.bearer.cfg == UNI_BEARER_MP) {
		if (IE_ISPRESENT(c->msg_setup.epref)) {
			if (!IE_ISPRESENT(co->epref))
				uni_mandate_ie(c->uni, UNI_IE_EPREF);				\

			if (IE_ISGOOD(co->epref) &&
			    co->epref.flag != 1) {
				IE_SETERROR(co->epref);
				(void)UNI_SAVE_IERR(&c->uni->cx, UNI_IE_EPREF,
				    co->epref.h.act, UNI_IERR_BAD);
			}
		}

		if (IE_ISGOOD(co->epref)) {
			p = uni_find_party(c, &co->epref);
			if (p == NULL) {
				respond_drop_party_ack(c, &co->epref,
				    UNI_CAUSE_ENDP_INV);
				uni_msg_destroy(api);
				UNI_FREE(ack);
				goto ignore;
			}
		}
	}

	switch (uni_verify(c->uni, u->u.hdr.act)) {

	  case VFY_CLR:
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
		/* FALLTHRU */
	  case VFY_I:
		uni_msg_destroy(api);
		UNI_FREE(ack);
	  	goto ignore;

	  case VFY_RAIM:
	  case VFY_RAI:
	  report:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), NULL, 0);
		uni_msg_destroy(api);
		UNI_FREE(ack);
	  	goto ignore;

	  case VFY_RAP:
	  case VFY_RAPU:
		if (c->type == CALL_ROOT && !IE_ISGOOD(co->epref))
			goto report;
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(new_state), NULL, 0);
		/* FALLTHRU */
	  case VFY_OK:
		break;
	}

	if (IE_ISGOOD(co->connid))
		c->connid = co->connid;

	if (c->cstate == CALLST_U1 || c->cstate == CALLST_N6)
		TIMER_STOP_CALL(c, t303);
	else if (c->cstate == CALLST_U3 || c->cstate == CALLST_N9)
		TIMER_STOP_CALL(c, t310);
	else if (c->cstate == CALLST_U4 || c->cstate == CALLST_N7) {
		if(c->type == CALL_P2P)
			TIMER_STOP_CALL(c, t301);
	}

	/*
	 * This is sent to the party only on the user side and only
	 * to the one party in the epref (L3MU_05_03).
	 */
	if (c->uni->proto == UNIPROTO_UNI40U &&
	    (c->type == CALL_LEAF || c->type == CALL_ROOT))
		uni_enq_party(p, SIGP_CONNECT, 0, NULL, NULL);

	conf->connect.hdr = u->u.hdr;
	copy_msg_connect(co, &conf->connect);
	c->uni->funcs->uni_output(c->uni, c->uni->arg,
	    UNIAPI_SETUP_confirm, 0, api);

	if (c->uni->proto == UNIPROTO_UNI40U) {
		/* this is left to the application on the network side */
		MK_MSG_ORIG(ack, UNI_CONNECT_ACK, c->cref, !c->mine);
		(void)uni_send_output(ack, c->uni);
		UNI_FREE(ack);
	}

	UNI_FREE(u);
	uni_msg_destroy(m);

	set_call_state(c, new_state);
}

/*
 * T313 (Connect) timer tick.
 *
 * Q.2971:Call-Control-U 15/39
 */
static void
u8_t313(struct call *c)
{
	VERBOSE(c->uni, UNI_FAC_TIMEOUT, 1, "call %u/%s T313 tick",
	    c->cref, c->mine ? "mine" : "his");

	MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER, UNI_CAUSE_RECOVER);
	ADD_CAUSE_TIMER(c->uni->cause, "313");
	clear_callD(c);
}

/*
 * CONNECT ACKNOWLEDGE message in U8
 *
 * Q.2971:Call-Control-U 15-16/39
 */
static void
u8_connect_ack(struct call *c, struct uni_msg *m, struct uni_all *u,
    enum call_state new_state)
{
	struct uniapi_setup_complete_indication *ind;
	struct uni_msg *api;

	ind = ALLOC_API(struct uniapi_setup_complete_indication, api);
	if (ind == NULL) {
  ignore:
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}

	/*
	 * Analyze message
	 */
	(void)uni_decode_body(m, u, &c->uni->cx);

	switch (uni_verify(c->uni, u->u.hdr.act)) {

	  case VFY_CLR:
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
		/* FALLTHRU */
	  case VFY_I:
		uni_msg_destroy(api);
	  	goto ignore;

	  case VFY_RAIM:
	  case VFY_RAI:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), NULL, 0);
		uni_msg_destroy(api);
	  	goto ignore;

	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(new_state), NULL, 0);
		/* FALLTHRU */
	  case VFY_OK:
		break;
	}

	TIMER_STOP_CALL(c, t313);

	if (c->type == CALL_LEAF) {
		struct party *p;

		TAILQ_FOREACH(p, &c->parties, link) {
			if (p->flags & PARTY_CONNECT) {
				uni_enq_party(p, SIGP_CONNECT_ACK,
				    0, NULL, NULL);
				break;
			}
		}
	}

	ind->connect_ack.hdr = u->u.hdr;
	copy_msg_connect_ack(&u->u.connect_ack, &ind->connect_ack);
	c->uni->funcs->uni_output(c->uni, c->uni->arg,
	    UNIAPI_SETUP_COMPLETE_indication, 0, api);

	UNI_FREE(u);
	uni_msg_destroy(m);

	set_call_state(c, new_state);
}

/*
 * CONNECT ACKNOWLEDGE message in N10
 *
 * Q.2971:Call-Control-N 18/39
 */
static void
n10_connect_ack(struct call *c, struct uni_msg *m, struct uni_all *u)
{
	/*
	 * Analyze message
	 */
	(void)uni_decode_body(m, u, &c->uni->cx);

	switch (uni_verify(c->uni, u->u.hdr.act)) {

	  case VFY_CLR:
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
		/* FALLTHRU */
	  case VFY_I:
		uni_msg_destroy(m);
		UNI_FREE(u);
	  	return;

	  case VFY_RAIM:
	  case VFY_RAI:
	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), NULL, 0);
		/* FALLTHRU */
	  case VFY_OK:
		uni_msg_destroy(m);
		UNI_FREE(u);
	  	return;
	}
}

/*
 * Release.response in U6 or U12.
 *
 * Q.2971:Call-Control-U 12/39 (U6)
 * Q.2971:Call-Control-U 30/39 (U12)
 * Q.2971:Call-Control-N 6/39  (N1)
 * Q.2971:Call-Control-N 29/39 (N11)
 */
static void
unx_release_response(struct call *c, struct uni_msg *m, uint32_t cookie)
{
	struct party *p;
	struct uni_all *msg;
	struct uniapi_release_response *arg =
	    uni_msg_rptr(m, struct uniapi_release_response *);

	if ((msg = UNI_ALLOC()) == NULL) {
		uniapi_call_error(c, UNIAPI_ERROR_NOMEM, cookie);
		uni_msg_destroy(m);
		return;
	}

	if (c->cstate == CALLST_U6 || c->cstate == CALLST_N1) {
		if (c->type == CALL_ROOT || c->type == CALL_LEAF) {
			TAILQ_FOREACH(p, &c->parties, link)
				uni_enq_party(p, SIGP_RELEASE_response,
				   cookie, NULL, NULL);
		}
	}
	msg->u.release_compl = arg->release_compl;
	MK_MSG_ORIG(msg, UNI_RELEASE_COMPL, c->cref, !c->mine);
	(void)uni_send_output(msg, c->uni);
	UNI_FREE(msg);

	uni_msg_destroy(m);

	uniapi_call_error(c, UNIAPI_OK, cookie);

	uni_destroy_call(c, 0);
}

/*
 * Got a RELEASE COMPLETE in any state expect U0
 *
 * Q.2971:Call-Control-U 25/39
 * Q.2971:Call-Control-N 26/39
 *
 * This is also called from the restart processes.
 */
void
uni_release_compl(struct call *c, struct uni_all *u)
{
	struct uni_msg *api;
	struct uniapi_release_confirm *conf;
	struct party *p;
	u_int i, j;

	if ((conf = ALLOC_API(struct uniapi_release_confirm, api)) == NULL)
		return;

	reset_all_timers(c);
	if (c->type == CALL_ROOT || c->type == CALL_LEAF) {
		TAILQ_FOREACH(p, &c->parties, link)
			uni_enq_party(p, SIGP_RELEASE_COMPL, 0, NULL, NULL);
		/* YYY optional call reoffering 10.3.3/10.3.4 */
	}
	conf->release.hdr = u->u.hdr;

	for (i = j = 0; i < 2; i++)
		if (IE_ISGOOD(u->u.release_compl.cause[i]))
			conf->release.cause[j++] = u->u.release_compl.cause[i];
	for (i = j = 0; i < UNI_NUM_IE_GIT; i++)
		if (IE_ISGOOD(u->u.release_compl.git[i]))
			conf->release.git[j++] = u->u.release_compl.git[i];
	if (IE_ISGOOD(u->u.release_compl.uu))
		conf->release.uu = u->u.release_compl.uu;
	if (IE_ISGOOD(u->u.release_compl.crankback))
		conf->release.crankback = u->u.release_compl.crankback;

	c->uni->funcs->uni_output(c->uni, c->uni->arg,
	    UNIAPI_RELEASE_confirm, 0, api);

	uni_destroy_call(c, 0);
}
static void
unx_release_compl(struct call *c, struct uni_msg *m, struct uni_all *u)
{

	(void)uni_decode_body(m, u, &c->uni->cx);
	(void)uni_verify(c->uni, u->u.hdr.act);	/* no point :-) */

	uni_release_compl(c, u);

	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * Got a RELEASE COMPLETE in any state expect U0 and U11
 *
 * Q.2971:Call-Control-U 25/39
 * Q.2971:Call-Control-N 26/39
 */
static void
unx_release(struct call *c, struct uni_msg *m, struct uni_all *u,
    enum call_state new_state)
{
	struct uniapi_release_indication *ind;
	struct uni_msg *api;

	if ((ind = ALLOC_API(struct uniapi_release_indication, api)) == NULL) {
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}

	(void)uni_decode_body(m, u, &c->uni->cx);
	(void)uni_verify(c->uni, u->u.hdr.act);	/* no point :-) */

	reset_all_timers(c);
	if (c->type == CALL_ROOT || c->type == CALL_LEAF) {
		struct party *p;

		TAILQ_FOREACH(p, &c->parties, link)
			uni_enq_party(p, SIGP_RELEASE, 0, NULL, NULL);
		/* YYY optional call reoffering 10.3.3/10.3.4 */
	}
	if (c->cstate != new_state) {
		/*
		 * According to Q.2971 we should send a 2nd
		 * Release.indication.
		 * According to Q.2931 the recipte of a RELEASE in U12/N11
		 * is illegal.
		 * According to us make it legal, but don't send a 2nd
		 * indication.
		 */
		ind->release.hdr = u->u.hdr;
		copy_msg_release(&u->u.release, &ind->release);

		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_RELEASE_indication, 0, api);
	} else
		uni_msg_destroy(api);

	uni_msg_destroy(m);
	UNI_FREE(u);

	set_call_state(c, new_state);
}

/*
 * Got RELEASE in U11 or N12
 *
 * Q.2971:Call-Control-U 28/39
 * Q.2971:Call-Control-N 30/39
 */
static void
u11n12_release(struct call *c, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_release_confirm *conf;
	struct uni_msg *api;

	if ((conf = ALLOC_API(struct uniapi_release_confirm, api)) == NULL) {
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}

	(void)uni_decode_body(m, u, &c->uni->cx);
	(void)uni_verify(c->uni, u->u.hdr.act);	/* no point :-) */

	TIMER_STOP_CALL(c, t308);

	conf->release.hdr = u->u.hdr;
	copy_msg_release(&u->u.release, &conf->release);

	c->uni->funcs->uni_output(c->uni, c->uni->arg,
	    UNIAPI_RELEASE_confirm, 0, api);

	uni_msg_destroy(m);
	UNI_FREE(u);

	uni_destroy_call(c, 0);
}

/*
 * NOTIFY message
 *
 * Q.2971:Call-Control-U 18/39
 * Q.2971:Call-Control-N 19/39
 */
static void
unx_notify(struct call *c, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_notify_indication *ind;
	struct uni_msg *api;
	struct party *p = NULL;

	if ((ind = ALLOC_API(struct uniapi_notify_indication, api)) == NULL) {
  ignore:
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}

	/*
	 * Analyze message
	 */
	(void)uni_decode_body(m, u, &c->uni->cx);
	MANDATE_IE(c->uni, u->u.notify.notify, UNI_IE_NOTIFY);

	if (IE_ISGOOD(u->u.notify.epref)) {
		if ((p = uni_find_party(c, &u->u.notify.epref)) == NULL) {
			respond_drop_party_ack(c, &u->u.notify.epref,
			    UNI_CAUSE_ENDP_INV);
			uni_msg_destroy(api);
			goto ignore;
		}
	}

	switch (uni_verify(c->uni, u->u.hdr.act)) {

	  case VFY_CLR:
		uni_msg_destroy(api);
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
	  	goto ignore;

	  case VFY_RAIM:
	  case VFY_RAI:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), &u->u.notify.epref,
		    p ? p->state : 0);
		/* FALLTHRU */
	  case VFY_I:
		uni_msg_destroy(api);
	  	goto ignore;

	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), &u->u.notify.epref,
		    p ? p->state : 0);
	  case VFY_OK:
		/* FALLTHRU */
	  	break;
	}

	ind->notify.hdr = u->u.hdr;
	copy_msg_notify(&u->u.notify, &ind->notify);
	c->uni->funcs->uni_output(c->uni, c->uni->arg,
	    UNIAPI_NOTIFY_indication, 0, api);

	UNI_FREE(u);
	uni_msg_destroy(m);
}

/*
 * Notify.request from user
 *
 * Q.2971:Call-Control-U 18/39
 * Q.2971:Call-Control-N 19/39
 */
static void
unx_notify_request(struct call *c, struct uni_msg *m, uint32_t cookie)
{
	struct uni_all *msg;
	struct uniapi_notify_request *arg =
	    uni_msg_rptr(m, struct uniapi_notify_request *);

	if ((msg = UNI_ALLOC()) == NULL) {
		uni_msg_destroy(m);
		uniapi_call_error(c, UNIAPI_ERROR_NOMEM, cookie);
		return;
	}

	msg->u.notify = arg->notify;
	MK_MSG_ORIG(msg, UNI_NOTIFY, c->cref, !c->mine);
	(void)uni_send_output(msg, c->uni);
	UNI_FREE(msg);

	uni_msg_destroy(m);

	uniapi_call_error(c, UNIAPI_OK, cookie);
}

/**********************************************************************/

/*
 * Release.request from API in any state except U11, U12, N11, N12
 *
 * Q.2971:Call-Control-U 27/39
 * Q.2971:Call-Control-N 28/39
 */
static void
unx_release_request(struct call *c, struct uni_msg *m, uint32_t cookie,
    enum call_state new_state)
{
	struct uni_all *msg;
	struct uniapi_release_request *arg =
	    uni_msg_rptr(m, struct uniapi_release_request *);
	struct party *p;

	if ((msg = UNI_ALLOC()) == NULL) {
		uni_msg_destroy(m);
		uniapi_call_error(c, UNIAPI_ERROR_NOMEM, cookie);
		return;
	}

	reset_all_timers(c);

	if (c->type == CALL_LEAF || c->type == CALL_ROOT) {
		TAILQ_FOREACH(p, &c->parties, link) {
			uni_enq_party(p, SIGP_RELEASE_request, cookie,
			    NULL, NULL);
		}
	}

	c->msg_release = arg->release;
	if (!IE_ISPRESENT(c->msg_release.cause[0]) &&
	    !IE_ISPRESENT(c->msg_release.cause[1]))
		MK_IE_CAUSE(c->msg_release.cause[0], UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_UNSPEC);

	msg->u.release = c->msg_release;
	MK_MSG_ORIG(msg, UNI_RELEASE, c->cref, !c->mine);
	(void)uni_send_output(msg, c->uni);
	UNI_FREE(msg);

	TIMER_START_CALL(c, t308, c->uni->timer308);
	c->cnt308 = 0;

	set_call_state(c, new_state);

	uni_msg_destroy(m);

	uniapi_call_error(c, UNIAPI_OK, cookie);
}

/*
 * Message with unknown EPREF - send a drop party according to 9.5.3.2.3a)
 */
static void
respond_drop_party_ack(struct call *c, struct uni_ie_epref *epref,
    u_int cause)
{
	struct uni_all *msg;

	if ((msg = UNI_ALLOC()) == NULL)
		return;

	MK_MSG_ORIG(msg, UNI_DROP_PARTY_ACK, c->cref, !c->mine);
	MK_IE_EPREF(msg->u.drop_party_ack.epref, epref->epref, !epref->flag);
	MK_IE_CAUSE(msg->u.drop_party_ack.cause, UNI_CAUSE_LOC_USER, cause);
	(void)uni_send_output(msg, c->uni);
	UNI_FREE(msg);
}

/*
 * T308 (RELEASE) timer
 *
 * Q.2971:Call-Control-U 28/39
 * Q.2971:Call-Control-N 30/39
 */
static void
u11n12_t308(struct call *c)
{
	struct uni_all *msg;
	struct uni_msg *api;
	struct uniapi_release_confirm *conf;

	VERBOSE(c->uni, UNI_FAC_TIMEOUT, 1, "call %u/%s T308 tick %d",
	    c->cref, c->mine ? "mine" : "his", c->cnt308 + 1);

	if (++c->cnt308 < c->uni->init308) {
		if ((msg = UNI_ALLOC()) != NULL) {
			msg->u.release = c->msg_release;
			MK_MSG_ORIG(msg, UNI_RELEASE, c->cref, !c->mine);
			if (!IE_ISPRESENT(msg->u.release.cause[1])) {
				MK_IE_CAUSE(msg->u.release.cause[1],
				    UNI_CAUSE_LOC_USER, UNI_CAUSE_RECOVER);
				ADD_CAUSE_TIMER(msg->u.release.cause[1], "308");
			}
			(void)uni_send_output(msg, c->uni);
			UNI_FREE(msg);
		}
		TIMER_START_CALL(c, t308, c->uni->timer308);
		return;
	}

	/*
	 * Send indication to API
	 */
	if ((conf = ALLOC_API(struct uniapi_release_confirm, api)) != NULL) {
		conf->release.hdr.cref.cref = c->cref;
		conf->release.hdr.cref.flag = c->mine;
		conf->release.hdr.act = UNI_MSGACT_DEFAULT;
		MK_IE_CAUSE(conf->release.cause[0], UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_RECOVER);
		ADD_CAUSE_TIMER(conf->release.cause[0], "308");

		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_RELEASE_confirm, 0, api);
	}

	uni_destroy_call(c, 0);
}
/**********************************************************************/

/*
 * STATUS in U11/U12
 *
 * Q.2971:Call-Control-U 29/39 (U11)
 * Q.2971:Call-Control-U 30/39 (U12)
 * Q.2971:Call-Control-N 29/39 (N11)
 * Q.2971:Call-Control-N 31/39 (N12)
 */
static void
un11un12_status(struct call *c, struct uni_msg *m, struct uni_all *u)
{
	enum call_state ns;
	struct uniapi_release_confirm *conf;
	struct uni_msg *api;
	struct party *p;
	struct uniapi_status_indication *stat;

	/*
	 * Analyze message
	 */
	(void)uni_decode_body(m, u, &c->uni->cx);
	MANDATE_IE(c->uni, u->u.status.callstate, UNI_IE_CALLSTATE);
	MANDATE_IE(c->uni, u->u.status.cause, UNI_IE_CAUSE);

	ns = c->cstate;
	if (IE_ISGOOD(u->u.status.callstate) &&
	    u->u.status.callstate.state == UNI_CALLSTATE_U0)
		ns = CALLST_NULL;

	p = NULL;
	if (IE_ISGOOD(u->u.status.epref))
		p = uni_find_party(c, &u->u.status.epref);

	switch (uni_verify(c->uni, u->u.hdr.act)) {

	  case VFY_CLR:
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
		uni_msg_destroy(m);
		UNI_FREE(u);
	  	return;

	  case VFY_RAIM:
	  case VFY_RAI:
	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(ns), &u->u.status.epref,
		    p ? p->state : UNI_EPSTATE_NULL);
	  case VFY_I:
	  case VFY_OK:
	  	break;
	}

	if (ns == c->cstate) {
		/*
		 * Inform API
		 */
		stat = ALLOC_API(struct uniapi_status_indication, api);
		if (stat != NULL) {
			stat->cref = u->u.hdr.cref;
			stat->my_state = map_callstate(c->cstate);
			stat->his_state = u->u.status.callstate;
			stat->his_cause = u->u.status.cause;
			stat->epref = u->u.status.epref;
			stat->epstate = u->u.status.epstate;
			stat->my_cause = 0;
			c->uni->funcs->uni_output(c->uni, c->uni->arg,
			    UNIAPI_STATUS_indication, 0, api);
		}

		uni_msg_destroy(m);
		UNI_FREE(u);

		return;
	}

	uni_msg_destroy(m);
	UNI_FREE(u);

	/*
	 * Send indication to API
	 */
	if ((conf = ALLOC_API(struct uniapi_release_confirm, api)) != NULL) {
		conf->release.hdr.cref.cref = c->cref;
		conf->release.hdr.cref.flag = c->mine;
		conf->release.hdr.act = UNI_MSGACT_DEFAULT;
		MK_IE_CAUSE(conf->release.cause[0], UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_MSG_INCOMP);
		ADD_CAUSE_MTYPE(conf->release.cause[0], UNI_STATUS);

		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_RELEASE_confirm, 0, api);
	}

	uni_destroy_call(c, 0);
}

static int
status_enq_filter(struct sig *sig, void *arg)
{
	return (sig->type == SIG_CALL &&
	    (struct call *)arg == sig->call &&
	    sig->sig == SIGC_SEND_STATUS_ENQ);
}

/*
 * STATUS in any state except U0/U11/U12 N0/N11/N12
 *
 * Q.2971:Call-Control-U 32/39
 * Q.2971:Call-Control-N 33/39
 */
static void
unx_status(struct call *c, struct uni_msg *m, struct uni_all *u)
{
	struct uniapi_status_indication *stat;
	struct uniapi_release_confirm *conf;
	enum call_state ns;
	struct uni_msg *api;
	struct party *p;

	/*
	 * Analyze message
	 */
	(void)uni_decode_body(m, u, &c->uni->cx);
	MANDATE_IE(c->uni, u->u.status.callstate, UNI_IE_CALLSTATE);
	MANDATE_IE(c->uni, u->u.status.cause, UNI_IE_CAUSE);

	ns = c->cstate;
	if (IE_ISGOOD(u->u.status.callstate))
		ns = state_compat(c, u->u.status.callstate.state);

	p = NULL;
	if (IE_ISGOOD(u->u.status.epref)) {
		p = uni_find_party(c, &u->u.status.epref);
		MANDATE_IE(c->uni, u->u.status.epstate, UNI_IE_EPSTATE);
	}

	switch (uni_verify(c->uni, u->u.hdr.act)) {

	  case VFY_CLR:
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
		uni_msg_destroy(m);
		UNI_FREE(u);
	  	return;

	  case VFY_RAIM:
	  case VFY_RAI:
	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(ns), &u->u.notify.epref,
		    p ? p->state : UNI_EPSTATE_NULL);
		/* FALLTHRU */
	  case VFY_I:
	  case VFY_OK:
	  	break;
	}

	if (u->u.status.callstate.state == UNI_CALLSTATE_U0) {
		/* release_complete */
		uni_msg_destroy(m);
		UNI_FREE(u);

		if (c->type == CALL_LEAF || c->type == CALL_ROOT) {
			TAILQ_FOREACH(p, &c->parties, link)
				uni_enq_party(p, SIGP_RELEASE_COMPL,
				    0, NULL, NULL);
		}
		/*
		 * Send indication to API
		 */
		conf = ALLOC_API(struct uniapi_release_confirm, api);
		if (conf != NULL) {
			conf->release.hdr.cref.cref = c->cref;
			conf->release.hdr.cref.flag = c->mine;
			conf->release.hdr.act = UNI_MSGACT_DEFAULT;
			MK_IE_CAUSE(conf->release.cause[0], UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_MSG_INCOMP);
			ADD_CAUSE_MTYPE(conf->release.cause[0], UNI_STATUS);

			c->uni->funcs->uni_output(c->uni, c->uni->arg,
			    UNIAPI_RELEASE_confirm, 0, api);
		}
		uni_destroy_call(c, 0);
		return;
	}

	if (IE_ISGOOD(u->u.status.cause) &&
	    u->u.status.cause.cause == UNI_CAUSE_STATUS) {
		c->se_active = 0;
		TIMER_STOP_CALL(c, t322);
		uni_undel(c->uni, status_enq_filter, c);
	}

	/*
	 * Inform API
	 */
	if ((stat = ALLOC_API(struct uniapi_status_indication, api)) != NULL) {
		stat->cref = u->u.hdr.cref;
		stat->my_state = map_callstate(c->cstate);
		stat->his_state = u->u.status.callstate;
		stat->his_cause = u->u.status.cause;
		stat->epref = u->u.status.epref;
		stat->epstate = u->u.status.epstate;
	}

	if (ns == c->cstate) {
		/* compatible or recovered */
		if (p != NULL)
			uni_enq_party(p, SIGP_STATUS, 0, m, u);
		else {
			if (IE_ISGOOD(u->u.status.epref) &&
			    (!IE_ISGOOD(u->u.status.epstate) ||
			     u->u.status.epstate.state != UNI_EPSTATE_NULL))
				respond_drop_party_ack(c, &u->u.status.epref,
				    UNI_CAUSE_MSG_INCOMP);

			uni_msg_destroy(m);
			UNI_FREE(u);
		}
		if (stat != NULL) {
			stat->my_cause = 0;
			c->uni->funcs->uni_output(c->uni, c->uni->arg,
			    UNIAPI_STATUS_indication, 0, api);
		}

		return;
	}

	/* incompatible */
	if (stat != NULL) {
		stat->my_cause = UNI_CAUSE_MSG_INCOMP;
		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_STATUS_indication, 0, api);
	}

	MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER, UNI_CAUSE_MSG_INCOMP);

	uni_msg_destroy(m);
	UNI_FREE(u);
	
	clear_callD(c);
}

/*
 * Enquiry peer status
 *
 * Q.2971:Call-Control-U 31/39
 * Q.2971:Call-Control-N 32/39
 */
static void
unx_status_enquiry_request(struct call *c, struct uni_msg *msg, uint32_t cookie)
{
	struct uniapi_status_enquiry_request *arg =
	    uni_msg_rptr(msg, struct uniapi_status_enquiry_request *);
	struct party *p;
	struct uni_all *stat;

	if (c->se_active) {
		/* This case is not handled in the SDLs */
		uniapi_call_error(c, UNIAPI_ERROR_BUSY, cookie);
		uni_msg_destroy(msg);
		return;
	}
	if ((c->type == CALL_ROOT || c->type == CALL_LEAF) &&
	    IE_ISGOOD(arg->epref)) {
		if ((p = uni_find_partyx(c, arg->epref.epref, !arg->epref.flag))
		    == NULL) {
			uniapi_call_error(c, UNIAPI_ERROR_BAD_PARTY, cookie);
			uni_msg_destroy(msg);
			return;
		}
		uni_msg_destroy(msg);
		uni_enq_party(p, SIGP_STATUS_ENQUIRY_request, cookie,
		    NULL, NULL);
		return;
	}
	if ((stat = UNI_ALLOC()) == NULL) {
		uniapi_call_error(c, UNIAPI_ERROR_NOMEM, cookie);
		uni_msg_destroy(msg);
		return;
	}
	memset(&c->stat_epref, 0, sizeof(c->stat_epref));
	MK_MSG_ORIG(stat, UNI_STATUS_ENQ, c->cref, !c->mine);
	(void)uni_send_output(stat, c->uni);
	UNI_FREE(stat);

	TIMER_START_CALL(c, t322, c->uni->timer322);
	c->cnt322 = 0;
	c->se_active = 1;

	uniapi_call_error(c, UNIAPI_OK, cookie);
}

/*
 * T322 tick
 *
 * Q.2971:Call-Control-U 34/39
 * Q.2971:Call-Control-N 35/39
 */
static void
unx_t322(struct call *c)
{
	struct uni_all *stat;

	VERBOSE(c->uni, UNI_FAC_TIMEOUT, 1, "call %u/%s T322 tick %d",
	    c->cref, c->mine ? "mine" : "his", c->cnt322 + 1);

	if (++c->cnt322 < c->uni->init322) {
		if ((stat = UNI_ALLOC()) != NULL) {
			MK_MSG_ORIG(stat, UNI_STATUS_ENQ, c->cref, !c->mine);
			stat->u.status_enq.epref = c->stat_epref;
			(void)uni_send_output(stat, c->uni);
			UNI_FREE(stat);
		}
		TIMER_START_CALL(c, t322, c->uni->timer322);
		return;
	}
	c->se_active = 0;

	MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER, UNI_CAUSE_RECOVER);
	ADD_CAUSE_TIMER(c->uni->cause, "322");

	clear_callD(c);
}

/*
 * STATUS ENQUIRY message
 *
 * Q.2971:Call-Control-U 31/39
 * Q.2971:Call-Control-N 32/39
 */
static void
unx_status_enq(struct call *c, struct uni_msg *m, struct uni_all *u)
{
	struct party *p = NULL;
	u_int epref, flag;

	/*
	 * Analyze message
	 */
	(void)uni_decode_body(m, u, &c->uni->cx);

	switch (uni_verify(c->uni, u->u.hdr.act)) {

	  case VFY_CLR:
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
		uni_msg_destroy(m);
		UNI_FREE(u);
	  	return;

	  case VFY_RAIM:
	  case VFY_RAI:
	  case VFY_RAP:
	  case VFY_RAPU:
	  case VFY_I:
	  case VFY_OK:
	  	break;
	}

	uni_msg_destroy(m);

	if ((c->type == CALL_ROOT || c->type == CALL_LEAF) &&
	    IE_ISGOOD(u->u.status_enq.epref)) {
		p = uni_find_party(c, &u->u.status_enq.epref);

		epref = u->u.status_enq.epref.epref;
		flag = u->u.status_enq.epref.flag;
		memset(u, 0, sizeof(*u));
		MK_IE_EPREF(u->u.status.epref, epref, !flag);

		if (p != NULL)
			MK_IE_EPSTATE(u->u.status.epstate, p->state);
		else
			MK_IE_EPSTATE(u->u.status.epstate, UNI_EPSTATE_NULL);
	} else
		memset(u, 0, sizeof(*u));


	MK_MSG_ORIG(u, UNI_STATUS, c->cref, !c->mine);
	MK_IE_CALLSTATE(u->u.status.callstate, map_callstate(c->cstate));
	MK_IE_CAUSE(u->u.status.cause, UNI_CAUSE_LOC_USER, UNI_CAUSE_STATUS);
	(void)uni_send_output(u, c->uni);
	UNI_FREE(u);
}

/**********************************************************************/

/*
 * Link-release.indication from SAAL in state U10 or N10.
 *
 * Q.2971:Call-Control-U 19/39
 * Q.2971:Call-Control-N 20/39
 */
static void
un10_link_release_indication(struct call *c)
{
	struct party *p;

	if (c->type == CALL_LEAF || c->type == CALL_ROOT)
		TAILQ_FOREACH(p, &c->parties, link) {
			if (p->state != UNI_EPSTATE_ACTIVE)
				uni_enq_party(p, SIGP_RELEASE_COMPL,
				    0, NULL, NULL);
		}

	uni_enq_coord(c->uni, SIGO_LINK_ESTABLISH_request, 0, NULL);
}

/*
 * Link-release.indication from SAAL in all state except U10 and N10.
 *
 * Q.2971:Call-Control-U 36/39
 * Q.2971:Call-Control-N 37/39
 */
static void
unx_link_release_indication(struct call *c)
{
	struct uniapi_release_confirm *conf;
	struct uni_msg *api;
	struct party *p;

	if (c->type == CALL_LEAF || c->type == CALL_ROOT)
		TAILQ_FOREACH(p, &c->parties, link)
			uni_enq_party(p, SIGP_RELEASE_COMPL, 0, NULL, NULL);
	
	if ((conf = ALLOC_API(struct uniapi_release_confirm, api)) != NULL) {
		conf->release.hdr.cref.cref = c->cref;
		conf->release.hdr.cref.flag = c->mine;
		conf->release.hdr.act = UNI_MSGACT_DEFAULT;
		MK_IE_CAUSE(conf->release.cause[0], UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_DST_OOO);

		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_RELEASE_confirm, 0, api);
	}

	uni_destroy_call(c, 0);
}

/*
 * Failed to establish SAAL link. Can happen only in U10 or N10.
 *
 * Q.2971:Call-Control-U 19/39
 * Q.2971:Call-Control-N 20/39
 */
static void
un10_link_establish_error_indication(struct call *c)
{
	struct party *p;
	struct uni_msg *api;
	struct uniapi_release_confirm *conf;

	if (c->type == CALL_LEAF || c->type == CALL_ROOT)
		TAILQ_FOREACH(p, &c->parties, link)
			uni_enq_party(p, SIGP_RELEASE_COMPL, 0, NULL, NULL);

	if ((conf = ALLOC_API(struct uniapi_release_confirm, api)) != NULL) {
		conf->release.hdr.cref.cref = c->cref;
		conf->release.hdr.cref.flag = c->mine;
		conf->release.hdr.act = UNI_MSGACT_DEFAULT;
		MK_IE_CAUSE(conf->release.cause[0], UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_DST_OOO);

		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_RELEASE_confirm, 0, api);
	}

	uni_destroy_call(c, 0);
}

/*
 * Issue a STATUS ENQUIRY of we are not busy
 *
 * Q.2971: Call-Control-U: 34/39
 * Q.2971: Call-Control-N: 34/39
 */
static void
call_se(struct call *c)
{
	struct uni_all *stat;

	c->cnt322 = 0;
	if (c->se_active)
		return;

	memset(&c->stat_epref, 0, sizeof(c->stat_epref));
	if ((stat = UNI_ALLOC()) != NULL) {
		MK_MSG_ORIG(stat, UNI_STATUS_ENQ, c->cref, !c->mine);
		(void)uni_send_output(stat, c->uni);
		UNI_FREE(stat);
	}

	TIMER_START_CALL(c, t322, c->uni->timer322);
	c->se_active = 1;
}

/*
 * Link-establish.indication in U10
 *
 * Q.2971:Call-Control-U 19-20/39
 * Q.2971:Call-Control-N 20-22/39
 */
static void
un10_link_establish_indication(struct call *c)
{
	int act = 0;
	struct party *p;

	if (c->type == CALL_ROOT || c->type == CALL_LEAF) {
		TAILQ_FOREACH(p, &c->parties, link)
			if (p->state == UNI_EPSTATE_ACTIVE) {
				act = 1;
				uni_enq_party(p, SIGP_STATUS_ENQUIRY_request,
				    0, NULL, NULL);
			}
		if (act)
			return;
	}
	call_se(c);
}

/*
 * Link-establish.indication in NOT U10/U11/U12 N10/N11/N12
 *
 * Q.2971:Call-Control-U 36/39
 * Q.2971:Call-Control-N 37/39
 */
static void
unx_link_establish_indication(struct call *c)
{
	call_se(c);
}

/*
 * Link-establish.confirm in U10 or N10
 *
 * Q.2971:Call-Control-U 19/39
 * Q.2971:Call-Control-N 20/39
 */
static void
un10_link_establish_confirm(struct call *c)
{
	struct party *p;

	if (c->type == CALL_ROOT || c->type == CALL_LEAF) {
		TAILQ_FOREACH(p, &c->parties, link)
			uni_enq_party(p, SIGP_STATUS_ENQUIRY_request,
			    0, NULL, NULL);
		return;
	}

	call_se(c);
}

/*
 * STATUS ENQ from party
 *
 * Q.2971:Call-Control-U 21/39
 * Q.2971:Call-Control-U 25/39
 */
static void
unx_send_party_status_enq(struct call *c, struct uni_all *u)
{
	if (c->se_active) {
		uni_delenq_sig(c->uni, SIG_CALL, c, NULL,
		    SIGC_SEND_STATUS_ENQ, 0, NULL, u);
		return;
	}

	c->stat_epref = u->u.status_enq.epref;
	(void)uni_send_output(u, c->uni);
	UNI_FREE(u);

	TIMER_START_CALL(c, t322, c->uni->timer322);
	c->se_active = 1;
}

/**********************************************************************/

static void
make_drop_cause(struct call *c, struct uni_ie_cause *cause)
{

	if (!IE_ISGOOD(*cause)) {
		/* 9.5.7.1 paragraph 2 */
		if (IE_ISPRESENT(*cause))
			MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_IE_INV);
		else
			MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_MANDAT);
		c->uni->cause.u.ie.len = 1;
		c->uni->cause.u.ie.ie[0] = UNI_IE_CAUSE;
		c->uni->cause.h.present |= UNI_CAUSE_IE_P;

	} else if (!IE_ISGOOD(c->uni->cause))
		c->uni->cause = *cause;
}

/*
 * Drop-party.indication from Party-Control in any state.
 *
 * Q.2971:Call-Control-U 23/39
 */
static void
ux_drop_party_indication(struct call *c, struct uni_msg *api)
{
	struct uniapi_drop_party_indication *drop =
	    uni_msg_rptr(api, struct uniapi_drop_party_indication *);

	if (uni_party_act_count(c, 2) == 0) {
		if (c->cstate != CALLST_U11) {
			make_drop_cause(c, &drop->drop.cause);
			clear_callD(c);
		}
		uni_msg_destroy(api);
		return;
	}
	c->uni->funcs->uni_output(c->uni, c->uni->arg,
	    UNIAPI_DROP_PARTY_indication, 0, api);
}

/*
 * Drop-party.indication from Party-Control in any state.
 *
 * Q.2971:Call-Control-N 23/39
 */
static void
nx_drop_party_indication(struct call *c, struct uni_msg *api)
{
	struct uniapi_drop_party_indication *drop =
	    uni_msg_rptr(api, struct uniapi_drop_party_indication *);

	if (uni_party_act_count(c, 0) == 0) {
		if (uni_party_act_count(c, 1) == 0) {
			if (c->cstate != CALLST_U11) {
				make_drop_cause(c, &drop->drop.cause);
				clear_callD(c);
			}
			uni_msg_destroy(api);
		} else {
			c->uni->funcs->uni_output(c->uni, c->uni->arg,
			    UNIAPI_DROP_PARTY_indication, 0, api);
			set_call_state(c, CALLST_N7);
		}
	} else {
		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_DROP_PARTY_indication, 0, api);
	}
}

/*
 * Drop-party-ack.indication from Party-Control in any state.
 *
 * Q.2971:Call-Control-U 23/39
 */
static void
ux_drop_party_ack_indication(struct call *c, struct uni_msg *api)
{
	struct uniapi_drop_party_ack_indication *drop =
	    uni_msg_rptr(api, struct uniapi_drop_party_ack_indication *);

	if (uni_party_act_count(c, 2) == 0) {
		if (c->cstate != CALLST_U11) {
			make_drop_cause(c, &drop->drop.cause);
			clear_callD(c);
		}
		uni_msg_destroy(api);
		return;
	}
	c->uni->funcs->uni_output(c->uni, c->uni->arg,
	    UNIAPI_DROP_PARTY_ACK_indication, 0, api);
}

/*
 * Drop-party-ack.indication from Party-Control in any state.
 *
 * Q.2971:Call-Control-N 23/39
 */
static void
nx_drop_party_ack_indication(struct call *c, struct uni_msg *api)
{
	struct uniapi_drop_party_ack_indication *drop =
	    uni_msg_rptr(api, struct uniapi_drop_party_ack_indication *);

	if (uni_party_act_count(c, 0) == 0) {
		if (uni_party_act_count(c, 1) == 0) {
			if (c->cstate != CALLST_U11) {
				make_drop_cause(c, &drop->drop.cause);
				clear_callD(c);
			}
			uni_msg_destroy(api);
		} else {
			c->uni->funcs->uni_output(c->uni, c->uni->arg,
			    UNIAPI_DROP_PARTY_ACK_indication, 0, api);
			set_call_state(c, CALLST_N7);
		}
	} else {
		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_DROP_PARTY_ACK_indication, 0, api);
	}
}

/*
 * Add-party-rej.indication from Party-Control in any state.
 *
 * Q.2971:Call-Control-U 23/39
 */
static void
ux_add_party_rej_indication(struct call *c, struct uni_msg *api)
{
	struct uniapi_add_party_rej_indication *rej =
	    uni_msg_rptr(api, struct uniapi_add_party_rej_indication *);

	if (uni_party_act_count(c, 2) == 0) {
		if (c->cstate != CALLST_U11) {
			make_drop_cause(c, &rej->rej.cause);
			clear_callD(c);
		}
		uni_msg_destroy(api);
		return;
	}
	c->uni->funcs->uni_output(c->uni, c->uni->arg,
	    UNIAPI_ADD_PARTY_REJ_indication, 0, api);
}

/*
 * Add-party-rej.indication from Party-Control in any state.
 *
 * Q.2971:Call-Control-N 23/39
 */
static void
nx_add_party_rej_indication(struct call *c, struct uni_msg *api)
{
	struct uniapi_add_party_rej_indication *rej =
	    uni_msg_rptr(api, struct uniapi_add_party_rej_indication *);

	if (uni_party_act_count(c, 0) == 0) {
		if (uni_party_act_count(c, 1) == 0) {
			if (c->cstate != CALLST_U11) {
				make_drop_cause(c, &rej->rej.cause);
				clear_callD(c);
			}
			uni_msg_destroy(api);
		} else {
			c->uni->funcs->uni_output(c->uni, c->uni->arg,
			    UNIAPI_ADD_PARTY_REJ_indication, 0, api);
			set_call_state(c, CALLST_N7);
		}
	} else {
		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_ADD_PARTY_REJ_indication, 0, api);
	}
}

/*
 * Add-party.request from API in U4 or U10
 *
 * Q.2971:Call-Control-U 9-10/39 (U4)
 * Q.2971:Call-Control-U 21/39 (U10)
 * Q.2971:Call-Control-N 12/39 (N7)
 * Q.2971:Call-Control-N 22/39 (N10)
 */
static void
unx_add_party_request(struct call *c, struct uni_msg *msg, uint32_t cookie)
{
	struct uniapi_add_party_request *add =
	    uni_msg_rptr(msg, struct uniapi_add_party_request *);
	struct party *p;

	if (IE_ISGOOD(add->add.epref)) {
		if (add->add.epref.flag != 0) {
			uni_msg_destroy(msg);
			uniapi_call_error(c, UNIAPI_ERROR_BAD_IE, cookie);
			return;
		}
		p = uni_find_partyx(c, add->add.epref.epref, 1);
		if (p != NULL) {
			uni_msg_destroy(msg);
			uniapi_call_error(c, UNIAPI_ERROR_EPREF_INUSE, cookie);
			return;
		}
	} else if (!IE_ISPRESENT(add->add.epref)) {
		allocate_epref(c, &add->add.epref);
		if (!IE_ISPRESENT(add->add.epref)) {
			uni_msg_destroy(msg);
			uniapi_call_error(c, UNIAPI_ERROR_EPREF_INUSE, cookie);
			return;
		}
	} else {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_IE, cookie);
		return;
	}

	if ((p = uni_create_partyx(c, add->add.epref.epref, 1, cookie)) == NULL) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_NOMEM, cookie);
		return;
	}
	uni_enq_party(p, SIGP_ADD_PARTY_request, cookie, msg, NULL);
}

/*
 * Add-party-ack.request from API in U10/N10
 *
 * Q.2971:Call-Control-U 21/39
 * Q.2971:Call-Control-N 22/39
 */
static void
un10_add_party_ack_request(struct call *c, struct uni_msg *msg, uint32_t cookie)
{
	struct uniapi_add_party_ack_request *ack =
	    uni_msg_rptr(msg, struct uniapi_add_party_ack_request *);
	struct party *p;

	if (!IE_ISGOOD(ack->ack.epref)) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_IE, cookie);
		return;
	}
	if (ack->ack.epref.flag != 1) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_IE, cookie);
		return;
	}
	if ((p = uni_find_partyx(c, ack->ack.epref.epref, 0)) == NULL) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_PARTY, cookie);
		return;
	}

	uni_enq_party(p, SIGP_ADD_PARTY_ACK_request, cookie, msg, NULL);
}

/*
 * Party-alerting.request from API in U7/U8/U10
 *
 * Q.2971:Call-Control-U 14/39 U7
 * Q.2971:Call-Control-U 15/39 U8
 * Q.2971:Call-Control-U 21/39 U10
 * Q.2971:Call-Control-N 8/39  N4
 * Q.2971:Call-Control-N 22/39 N10
 */
static void
unx_party_alerting_request(struct call *c, struct uni_msg *msg, uint32_t cookie)
{
	struct uniapi_party_alerting_request *alert =
	    uni_msg_rptr(msg, struct uniapi_party_alerting_request *);
	struct party *p;

	if (!IE_ISGOOD(alert->alert.epref)) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_IE, cookie);
		return;
	}
	if (alert->alert.epref.flag != 1) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_IE, cookie);
		return;
	}
	if ((p = uni_find_partyx(c, alert->alert.epref.epref, 0)) == NULL) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_PARTY, cookie);
		return;
	}

	uni_enq_party(p, SIGP_PARTY_ALERTING_request, cookie, msg, NULL);
}

/*
 * Add-party-rej.request from API in U7/U8/U10/N4/N10
 *
 * Q.2971:Call-Control-U 14/39 U7
 * Q.2971:Call-Control-U 15/39 U8
 * Q.2971:Call-Control-U 21/39 U10
 * Q.2971:Call-Control-N 8/39  N4
 * Q.2971:Call-Control-N 22/39 N10
 */
static void
unx_add_party_rej_request(struct call *c, struct uni_msg *msg, uint32_t cookie)
{
	struct uniapi_add_party_rej_request *rej =
	    uni_msg_rptr(msg, struct uniapi_add_party_rej_request *);
	struct party *p;

	if (!IE_ISGOOD(rej->rej.epref)) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_IE, cookie);
		return;
	}
	if (rej->rej.epref.flag != 1) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_IE, cookie);
		return;
	}
	if ((p = uni_find_partyx(c, rej->rej.epref.epref, 0)) == NULL) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_PARTY, cookie);
		return;
	}

	uni_enq_party(p, SIGP_ADD_PARTY_REJ_request, cookie, msg, NULL);
}

/*
 * Drop-party.request from API in U1-U10
 *
 * Q.2971:Call-Control-U 21/39 U10
 * Q.2971:Call-Control-U 26/39 U1-U9
 * Q.2971:Call-Control-N 22/39 N10
 * Q.2971:Call-Control-N 27/39 N1-N9
 */
static void
unx_drop_party_request(struct call *c, struct uni_msg *msg, uint32_t cookie)
{
	struct uniapi_drop_party_request *drop =
	    uni_msg_rptr(msg, struct uniapi_drop_party_request *);
	struct party *p;

	if (!IE_ISGOOD(drop->drop.epref)) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_IE, cookie);
		return;
	}
	if ((p = uni_find_partyx(c, drop->drop.epref.epref, !drop->drop.epref.flag)) == NULL) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_PARTY, cookie);
		return;
	}

	uni_enq_party(p, SIGP_DROP_PARTY_request, cookie, msg, NULL);
}

/*
 * Drop-party-ack.request from API in U1-U10
 *
 * Q.2971:Call-Control-U 21/39 U10
 * Q.2971:Call-Control-U 26/39 U1-U9
 * Q.2971:Call-Control-N 22/39 N10
 * Q.2971:Call-Control-N 27/39 N1-N9
 */
static void
unx_drop_party_ack_request(struct call *c, struct uni_msg *msg,
    uint32_t cookie)
{
	struct uniapi_drop_party_ack_request *ack =
	    uni_msg_rptr(msg, struct uniapi_drop_party_ack_request *);
	struct party *p;

	if (!IE_ISGOOD(ack->ack.epref)) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_IE, cookie);
		return;
	}
	if ((p = uni_find_partyx(c, ack->ack.epref.epref, !ack->ack.epref.flag)) == NULL) {
		uni_msg_destroy(msg);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_PARTY, cookie);
		return;
	}

	uni_enq_party(p, SIGP_DROP_PARTY_ACK_request, cookie, msg, NULL);
}

/*
 * ADD PARTY in U7/U8/U10
 *
 * Q.2971:Call-Control-U 14/39  U7
 * Q.2971:Call-Control-U 15/39  U8
 * Q.2971:Call-Control-U 21/39  U10
 * Q.2971:Call-Control-N 8/39   N4
 * Q.2971:Call-Control-N 21/39  N10
 *
 * Body already decoded
 * XXX check EPREF flag
 */
static void
unx_add_party(struct call *c, struct uni_msg *m, struct uni_all *u,
    int legal)
{
	struct uni_all *resp;
	struct uni_ierr *e1;
	struct party *p = NULL;
	enum verify vfy;

	uni_mandate_epref(c->uni, &u->u.add_party.epref);
	MANDATE_IE(c->uni, u->u.add_party.called, UNI_IE_CALLED);

	/*
	 * Do part of the verify handish: according to 9.5.7.2 we must send
	 * an ADD_PARTY_REJ if mandatory IEs are bad or missing instead of
	 * clearing the call. But we must send a STATUS, if it is the EPREF!
	 */
	if (IE_ISGOOD(u->u.add_party.epref)) {
		c->uni->cause.u.ie.len = 0;
		FOREACH_ERR(e1, c->uni) {
			if (e1->err == UNI_IERR_MIS) {
				MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER,
				    UNI_CAUSE_MANDAT);
				goto rej;
			}
		}
		FOREACH_ERR(e1, c->uni) {
			if (e1->man && e1->ie != UNI_IE_EPREF &&
			    e1->act == UNI_IEACT_DEFAULT) {
				MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER,
				    UNI_CAUSE_IE_INV);
  rej:
				uni_vfy_collect_ies(c->uni);
				if ((resp = UNI_ALLOC()) != NULL) {
					MK_MSG_RESP(resp, UNI_ADD_PARTY_REJ,
					   &u->u.hdr.cref);
					MK_IE_EPREF(resp->u.add_party_rej.epref,
					    u->u.add_party.epref.epref,
					    !u->u.add_party.epref.flag);
					resp->u.add_party_rej.cause =
					    c->uni->cause;

					unx_send_add_party_rej(c, resp);
				}
				goto ignore;
			}
		}
		p = uni_find_partyx(c, u->u.add_party.epref.epref,
		    u->u.add_party.epref.flag);
	}

	vfy = uni_verify(c->uni, u->u.hdr.act);

	switch (vfy) {

	  case VFY_CLR:
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
		goto ignore;

	  case VFY_RAIM:
	  case VFY_RAI:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), &u->u.add_party.epref,
		    p ? p->state : UNI_EPSTATE_NULL);
		/* FALLTHRU */
	  case VFY_I:
		goto ignore;

	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), &u->u.add_party.epref,
		    UNI_EPSTATE_ADD_RCVD);
	  case VFY_OK:
		/* FALLTHRU */
		break;
	}
	if (!legal) {
		uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
		    &u->u.add_party.epref, -1);
		return;
	}

	if (IE_ISGOOD(u->u.add_party.epref) && p == NULL &&
	    u->u.add_party.epref.flag) {
		IE_SETERROR(u->u.add_party.epref);
		(void)UNI_SAVE_IERR(&c->uni->cx, UNI_IE_EPREF,
		    u->u.add_party.epref.h.act, UNI_IERR_BAD);
	}

	if (!IE_ISGOOD(u->u.add_party.epref)) {
		/* 9.5.3.2.2 */
		if (vfy == VFY_OK) {
			MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_IE_INV);

			uni_respond_status_verify(c->uni, &u->u.hdr.cref,
			    map_callstate(c->cstate), NULL, 0);
		}
		goto ignore;
	}


	if (p == NULL && (p = uni_create_party(c, &u->u.add_party.epref))
	    == NULL)
		goto ignore;

	uni_enq_party(p, SIGP_ADD_PARTY, 0, m, u);
	return;

  ignore:
	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * ADD PARTY ACKNOWLEDGE
 *
 * Q.2971:Call-Control-U 21/39 U10
 * Q.2971:Call-Control-N 15/39 N8
 * Q.2971:Call-Control-N 22/39 N10
 */
static void
un10n8_add_party_ack(struct call *c, struct uni_msg *m, struct uni_all *u,
    int legal)
{
	struct party *p = NULL;

	if (IE_ISGOOD(u->u.add_party_ack.epref)) {
		if (u->u.add_party_ack.epref.flag == 0) {
			IE_SETERROR(u->u.add_party_ack.epref);
			(void)UNI_SAVE_IERR(&c->uni->cx, UNI_IE_EPREF,
			    u->u.add_party_ack.epref.h.act, UNI_IERR_BAD);
		} else {
	    		p = uni_find_partyx(c, u->u.add_party_ack.epref.epref, 1);
			if (p == NULL) {
				respond_drop_party_ack(c,
				    &u->u.add_party_ack.epref,
				    UNI_CAUSE_ENDP_INV);
				goto ignore;
			}
		}
	}
	uni_mandate_epref(c->uni, &u->u.add_party_ack.epref);

	switch (uni_verify(c->uni, u->u.hdr.act)) {

	  case VFY_CLR:
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
		goto ignore;

	  case VFY_RAIM:
	  case VFY_RAI:
	  report:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), &u->u.add_party_ack.epref,
		    p ? p->state : UNI_EPSTATE_NULL);
	  case VFY_I:
		goto ignore;

	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), &u->u.add_party_ack.epref,
		    p ? UNI_EPSTATE_ACTIVE : UNI_EPSTATE_NULL);
		if (!IE_ISGOOD(u->u.party_alerting.epref))
			/* See below */
			goto ignore;
		break;
	  case VFY_OK:
		if (!IE_ISGOOD(u->u.party_alerting.epref))
			/* this happens when the EPREF has bad format.
			 * The rules require us the message to be ignored
			 * (9.5.3.2.2e) and to report status.
			 */
			goto report;
		break;
	}
	if (legal) {
		/* p is != NULL here */
		uni_enq_party(p, SIGP_ADD_PARTY_ACK, 0, m, u);
		return;
	}
	if (p == NULL)
		/* Q.2971 9.5.3.2.3a) */
		respond_drop_party_ack(c, &u->u.add_party_ack.epref,
		    UNI_CAUSE_ENDP_INV);
	else
		uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
		    &u->u.add_party_ack.epref, p->state);

  ignore:
	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * Make the EPREF action default
 */
static void
default_act_epref(struct uni *uni, struct uni_ie_epref *epref)
{
	struct uni_ierr *e;

	FOREACH_ERR(e, uni)
		if (e->ie == UNI_IE_EPREF) {
			e->act = UNI_IEACT_DEFAULT;
			break;
		}
	epref->h.act = UNI_IEACT_DEFAULT;
}

/*
 * PARTY ALERTING message
 *
 * Q.2971:Call-Control-U 9/39   U4
 * Q.2971:Call-Control-U 21/39  U10
 * Q.2971:Call-Control-N 12/39  N7
 * Q.2971:Call-Control-N 15/39  N8
 * Q.2971:Call-Control-N 22/39  N10
 */
static void
unx_party_alerting(struct call *c, struct uni_msg *m, struct uni_all *u,
    int legal)
{
	struct party *p = NULL;

	if (IE_ISGOOD(u->u.party_alerting.epref)) {
		if (u->u.party_alerting.epref.flag == 0) {
			IE_SETERROR(u->u.party_alerting.epref);
			(void)UNI_SAVE_IERR(&c->uni->cx, UNI_IE_EPREF,
			    u->u.party_alerting.epref.h.act, UNI_IERR_BAD);
		} else {
	    		p = uni_find_partyx(c, u->u.party_alerting.epref.epref, 1);
			if (p == NULL) {
				respond_drop_party_ack(c,
				    &u->u.party_alerting.epref,
				    UNI_CAUSE_ENDP_INV);
				goto ignore;
			}
		}
	}
	uni_mandate_epref(c->uni, &u->u.party_alerting.epref);

	switch (uni_verify(c->uni, u->u.hdr.act)) {

	  case VFY_CLR:
		uni_vfy_collect_ies(c->uni);
		clear_callD(c);
		goto ignore;

	  case VFY_RAIM:
	  case VFY_RAI:
	  report:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), &u->u.party_alerting.epref,
		    p ? p->state : UNI_EPSTATE_NULL);
	  case VFY_I:
		goto ignore;

	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(c->uni, &u->u.hdr.cref,
		    map_callstate(c->cstate), &u->u.party_alerting.epref,
		    p ? UNI_EPSTATE_ALERT_RCVD : UNI_EPSTATE_NULL);
		if (!IE_ISGOOD(u->u.party_alerting.epref))
			/* See below */
			goto ignore;
		break;

	  case VFY_OK:
		if (!IE_ISGOOD(u->u.party_alerting.epref))
			/* The rules require us the message to be ignored
			 * (9.5.3.2.2e) and to report status.
			 */
			goto report;
		break;
	}
	if (legal) {
		/* p is != NULL here */
		uni_enq_party(p, SIGP_PARTY_ALERTING, 0, m, u);
		return;
	}
	if (p == NULL)
		/* Q.2971 9.5.3.2.3a) */
		respond_drop_party_ack(c, &u->u.party_alerting.epref,
		    UNI_CAUSE_ENDP_INV);
	else
		uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
		    &u->u.party_alerting.epref, p->state);

  ignore:
	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * Handle a bad/missing cause in a DROP_PARTY_ACK or ADD_PARTY_REJ
 *
 * If the IE is missing or bad and the action is defaulted handle as
 * cause #1 according to 9.5.7.1/2.
 * Otherwise keep the IE.
 */
static void
handle_bad_drop_cause(struct call *c, struct uni_ie_cause *cause, int mkcause)
{

	if (IE_ISGOOD(*cause))
		return;

	if (!IE_ISPRESENT(*cause)) {
		/* 9.5.7.1 */
		/* cannot make cause here because we need the 96 error */
		uni_vfy_remove_cause(c->uni);
		return;
	}
	if (cause->h.act != UNI_IEACT_DEFAULT)
		return;

	/* 9.5.7.2 */
	uni_vfy_remove_cause(c->uni);
	if (mkcause)
		MK_IE_CAUSE(*cause, UNI_CAUSE_LOC_USER, UNI_CAUSE_UNSPEC);
}

/*
 * ADD PARTY REJ from party control
 * Q.2971:Call-Control-U 21/39
 * Q.2971:Call-Control-U 24/39
 */
static void
unx_send_add_party_rej(struct call *c, struct uni_all *u)
{

	if (uni_party_act_count(c, 2) == 0) {
		if (c->cstate != CALLST_U11 && c->cstate != CALLST_N12) {
			c->uni->cause = u->u.add_party_rej.cause;
			clear_callD(c);
		}
	} else
		(void)uni_send_output(u, c->uni);
	UNI_FREE(u);
}

/*
 * ADD_PARTY_REJECT in U4/U10
 *
 * Q.2971:Call-Control-U 9/39 U4
 * Q.2971:Call-Control-U 21/39 U10
 * Q.2971:Call-Control-N 12/39 N7
 * Q.2971:Call-Control-N 15/39 N8
 * Q.2971:Call-Control-N 22/39 N10
 */
static void
unx_add_party_rej(struct call *c, struct uni_msg *m, struct uni_all *u,
    int legal)
{
	struct uni_add_party_rej *ar = &u->u.add_party_rej;
	struct party *p;

	if (IE_ISGOOD(ar->epref)) {
		p = uni_find_partyx(c, ar->epref.epref, ar->epref.flag);
		if (p == NULL)
			goto ignore;

		if (legal) {
			handle_bad_drop_cause(c, &ar->cause, 0);
			uni_vfy_remove_unknown(c->uni);
			switch (uni_verify(c->uni, u->u.hdr.act)) {

			  case VFY_CLR:
				goto clear;

			  case VFY_RAIM:
			  case VFY_RAI:
				uni_respond_status_verify(c->uni,
				    &u->u.hdr.cref, map_callstate(c->cstate),
				    &ar->epref, p->state);
			  case VFY_I:
				goto ignore;

			  case VFY_RAPU:
				uni_vfy_collect_ies(c->uni);
				break;

			  case VFY_RAP:
				uni_respond_status_verify(c->uni,
				    &u->u.hdr.cref, map_callstate(c->cstate),
				    &ar->epref, p->state);
			  case VFY_OK:
				break;
			}
			uni_enq_party(p, SIGP_ADD_PARTY_REJ, 0, m, u);
			return;
		}
		uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
		    &ar->epref, -1);
		return;
	}

	/* Q.2971: 9.5.3.2.1 last paragraph
	 *         9.5.3.2.2 second to last paragraph
	 * Make the action indicator default.
	 */
	default_act_epref(c->uni, &ar->epref);
	if (!IE_ISPRESENT(ar->epref))
		uni_mandate_ie(c->uni, UNI_IE_EPREF);
	(void)uni_verify(c->uni, u->u.hdr.act);

  clear:
	uni_vfy_collect_ies(c->uni);
	clear_callD(c);

  ignore:
	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * DROP_PARTY
 *
 * Q.2971:Call-Control-U 26/39 Ux
 * Q.2971:Call-Control-U 21/39 U10
 * Q.2971:Call-Control-N 27/39 Nx
 * Q.2971:Call-Control-N 22/39 N10
 */
static void
unx_drop_party(struct call *c, struct uni_msg *m, struct uni_all *u, int legal)
{
	struct uni_drop_party *dp = &u->u.drop_party;
	struct party *p;
	struct uni_ierr *e;

	if (IE_ISGOOD(dp->epref)) {
		p = uni_find_partyx(c, dp->epref.epref, dp->epref.flag);
		if (p == NULL) {
			respond_drop_party_ack(c, &dp->epref,
			    UNI_CAUSE_ENDP_INV);
			goto ignore;
		}
		handle_bad_drop_cause(c, &dp->cause, 0);
		uni_vfy_remove_unknown(c->uni);
		switch (uni_verify(c->uni, u->u.hdr.act)) {

		  case VFY_CLR:
			goto clear;

		  case VFY_RAIM:
		  case VFY_RAI:
			uni_respond_status_verify(c->uni, &u->u.hdr.cref,
			    map_callstate(c->cstate),
			    &u->u.drop_party.epref, p->state);
		  case VFY_I:
			goto ignore;

		  case VFY_RAPU:
			uni_vfy_collect_ies(c->uni);
			break;

		  case VFY_RAP:
			uni_respond_status_verify(c->uni, &u->u.hdr.cref,
			    map_callstate(c->cstate),
			    &dp->epref, UNI_EPSTATE_DROP_RCVD);
		  case VFY_OK:
			break;
		}
		if (legal) {
			uni_enq_party(p, SIGP_DROP_PARTY, 0, m, u);
			return;
		}
		uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP, &dp->epref, -1);
		goto ignore;
	}

	/* Q.2971: 9.5.3.2.1 last paragraph
	 *         9.5.3.2.2 second to last paragraph
	 * Make the action indicator default.
	 */
	FOREACH_ERR(e, c->uni)
		if (e->ie == UNI_IE_EPREF) {
			e->act = UNI_IEACT_DEFAULT;
			break;
		}
	dp->epref.h.act = UNI_IEACT_DEFAULT;

	if (!IE_ISPRESENT(dp->epref))
		uni_mandate_ie(c->uni, UNI_IE_EPREF);
	(void)uni_verify(c->uni, u->u.hdr.act);

  clear:
	uni_vfy_collect_ies(c->uni);
	clear_callD(c);
	uni_msg_destroy(m);
	UNI_FREE(u);
	return;

  ignore:
	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * DROP_PARTY_ACK
 *
 * Q.2971:Call-Control-U 26/39 Ux
 * Q.2971:Call-Control-U 21/39 U10
 * Q.2971:Call-Control-N 27/39 Nx
 * Q.2971:Call-Control-N 22/39 N10
 */
static void
unx_drop_party_ack(struct call *c, struct uni_msg *m, struct uni_all *u,
    int legal)
{
	struct party *p;
	struct uni_drop_party_ack *ack = &u->u.drop_party_ack;

	if (IE_ISGOOD(u->u.drop_party_ack.epref)) {
		p = uni_find_partyx(c, ack->epref.epref, ack->epref.flag);
		if (p != NULL) {
			handle_bad_drop_cause(c, &ack->cause, 1);
			uni_vfy_remove_unknown(c->uni);
			switch (uni_verify(c->uni, u->u.hdr.act)) {

			  case VFY_CLR:
				goto clear;

			  case VFY_RAIM:
			  case VFY_RAI:
				uni_respond_status_verify(c->uni,
				    &u->u.hdr.cref, map_callstate(c->cstate),
				    &ack->epref, p->state);
			  case VFY_I:
				goto ignore;

			  case VFY_RAP:
				uni_respond_status_verify(c->uni,
				    &u->u.hdr.cref, map_callstate(c->cstate),
				    &ack->epref, UNI_EPSTATE_NULL);
			  case VFY_RAPU:
			  case VFY_OK:
				break;
			}
			if (legal) {
				uni_enq_party(p, SIGP_DROP_PARTY_ACK, 0, m, u);
				return;
			}
			uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
			    &ack->epref, -1);
		}
		goto ignore;
	}

	/* Q.2971: 9.5.3.2.1 last paragraph
	 *         9.5.3.2.2 second to last paragraph
	 */
	(void)uni_verify(c->uni, u->u.hdr.act);
	MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER, UNI_CAUSE_IE_INV);

  clear:
	uni_vfy_collect_ies(c->uni);
	clear_callD(c);
	uni_msg_destroy(m);
	UNI_FREE(u);
	return;

  ignore:
	uni_msg_destroy(m);
	UNI_FREE(u);
}

/**********************************************************************/

/*
 * Bad or unrecognized message.
 *
 * Q.2971:Call-Control-U 35/39
 */
void
uni_bad_message(struct call *c, struct uni_all *u, u_int cause,
    struct uni_ie_epref *epref, int ps)
{
	struct uni_all *resp;
	struct party *p;

	if ((u->u.hdr.act == UNI_MSGACT_CLEAR &&
	    (c->cstate == CALLST_U11 ||
	     c->cstate == CALLST_U12 ||
	     c->cstate == CALLST_N11 ||
	     c->cstate == CALLST_N12)) ||
	    u->u.hdr.act == UNI_MSGACT_IGNORE)
		return;

	MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER, cause);
	ADD_CAUSE_MTYPE(c->uni->cause, u->mtype);

	if (u->u.hdr.act == UNI_MSGACT_CLEAR) {
		clear_callD(c);
		return;
	}

	/*
	 * Send STATUS
	 */
	if ((resp = UNI_ALLOC()) != NULL) {
		MK_MSG_RESP(resp, UNI_STATUS, &u->u.hdr.cref);
		MK_IE_CALLSTATE(resp->u.status.callstate,
		    map_callstate(c->cstate));
		resp->u.status.cause = c->uni->cause;

		if (epref != NULL && IE_ISGOOD(*epref)) {
			MK_IE_EPREF(resp->u.status.epref, epref->epref, !epref->flag);
			if (ps == -1) {
				p = uni_find_party(c, epref);
				if (p == NULL)
					ps = UNI_EPSTATE_NULL;
				else
					ps = p->state;
			}
			MK_IE_EPSTATE(resp->u.status.epstate, ps);
		}
		(void)uni_send_output(resp, c->uni);

		UNI_FREE(resp);
	}
}

/**********************************************************************/

/*
 * Unknown message in any state.
 *
 * Q.2971:Call-Control 35/39
 * Q.2971:Call-Control 36/39
 */
static void
unx_unknown(struct call *c, struct uni_msg *m, struct uni_all *u)
{
	/*
	 * Unrecognized message. Cannot call verify here, because
	 * it doesn't know about unrecognized messages.
	 */
	if (u->u.hdr.act == UNI_MSGACT_CLEAR) {
		MK_IE_CAUSE(c->uni->cause, UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_MTYPE_NIMPL);
		ADD_CAUSE_MTYPE(c->uni->cause, u->mtype);
		clear_callD(c);
	} else if(u->u.hdr.act == UNI_MSGACT_IGNORE) {
		;
	} else {
		(void)uni_decode_body(m, u, &c->uni->cx);
		uni_bad_message(c, u, UNI_CAUSE_MTYPE_NIMPL,
		    &u->u.unknown.epref, -1);
	}
	uni_msg_destroy(m);
	UNI_FREE(u);
}
/**********************************************************************/

void
uni_sig_call(struct call *c, enum call_sig sig, uint32_t cookie,
    struct uni_msg *msg, struct uni_all *u)
{
	if (sig >= SIGC_END) {
		VERBOSE(c->uni, UNI_FAC_ERR, 1,
		    "Signal %d outside of range to Call-Control", sig);
		if (msg)
			uni_msg_destroy(msg);
		if (u)
			UNI_FREE(u);
		return;
	}

	VERBOSE(c->uni, UNI_FAC_CALL, 1, "Signal %s in state %s of call %u/%s"
	    "; cookie %u", call_sigs[sig], callstates[c->cstate].name, c->cref,
	    c->mine ? "mine" : "his", cookie);

	switch (sig) {

	  case SIGC_LINK_RELEASE_indication:
		if (c->cstate == CALLST_U10 || c->cstate == CALLST_N10)
			/* Q.2971:Call-Control-U 36/39 */
			/* Q.2971:Call-Control-N 20/39 */
			un10_link_release_indication(c);
		else
			/* Q.2971:Call-Control-U 36/39 */
			/* Q.2971:Call-Control-N 37/39 */
			unx_link_release_indication(c);
		break;

	  case SIGC_LINK_ESTABLISH_ERROR_indication:
		if (c->cstate != CALLST_U10 && c->cstate != CALLST_N10) {
			VERBOSE(c->uni, UNI_FAC_ERR, 1,
			    "link-establish-error.indication in cs=%s",
			    callstates[c->cstate].name);
			break;
		}
		/* Q.2971:Call-Control-U 19/39 */
		/* Q.2971:Call-Control-N 20/39 */
		un10_link_establish_error_indication(c);
		break;

	  case SIGC_LINK_ESTABLISH_indication:
		switch (c->cstate) {

		  case CALLST_U1: case CALLST_N1:
		  case CALLST_U3: case CALLST_N3:
		  case CALLST_U4: case CALLST_N4:
		  case CALLST_U6: case CALLST_N6:
		  case CALLST_U7: case CALLST_N7:
		  case CALLST_U8: case CALLST_N8:
		  case CALLST_U9: case CALLST_N9:
			/* Q.2971:Call-Control-U 36/39 */
			/* Q.2971:Call-Control-N 37/39 */
			unx_link_establish_indication(c);
			break;

		  case CALLST_U10: case CALLST_N10:
			/* Q.2971:Call-Control-U 19/39 */
			/* Q.2971:Call-Control-N 20/39 */
			un10_link_establish_indication(c);
			break;

		  case CALLST_U11: case CALLST_N11:
		  case CALLST_U12: case CALLST_N12:
			/* Q.2971:Call-Control-U 36/39 */
			/* Q.2971:Call-Control-N 37/39 */
			break;

		  default:
			VERBOSE(c->uni, UNI_FAC_ERR, 1,
			    "link-establish.indication in cs=%s",
			    callstates[c->cstate].name);
		}
		break;

	  case SIGC_LINK_ESTABLISH_confirm:
		if (c->cstate != CALLST_U10 && c->cstate != CALLST_N10) {
			VERBOSE(c->uni, UNI_FAC_ERR, 1,
			    "link-establish.confirm in cs=%s",
			    callstates[c->cstate].name);
			break;
		}
		/* Q.2971:Call-Control-U 19/39 */
		/* Q.2971:Call-Control-N 20/39 */
		un10_link_establish_confirm(c);
		break;

	  case SIGC_UNKNOWN:
		/* Q.2971:Call-Control 35/39 */
		/* Q.2971:Call-Control 36/39 */
		unx_unknown(c, msg, u);
		break;

	  case SIGC_SETUP:
		if (c->cstate != CALLST_NULL) {
			(void)uni_decode_body(msg, u, &c->uni->cx);
			uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
			    &u->u.setup.epref, -1);
			goto drop;
		}
		if (c->uni->proto == UNIPROTO_UNI40N)
			/* Q.2971:Call-Control-N 4/39 */
			un0_setup(c, msg, u, CALLST_N1);
		else
			/* Q.2971:Call-Control-U 4/39 */
			un0_setup(c, msg, u, CALLST_U6);
		break;

	  case SIGC_CALL_PROC:
		if (c->cstate == CALLST_U1) {
			/* Q.2971:Call-Control-U 6/39 */
			u1n6_call_proc(c, msg, u, CALLST_U3);
			break;
		}
		if (c->cstate == CALLST_N6) {
			/* Q.2971:Call-Control-N 11/39 */
			u1n6_call_proc(c, msg, u, CALLST_N9);
			break;
		}
		(void)uni_decode_body(msg, u, &c->uni->cx);
		uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
		    &u->u.call_proc.epref, -1);
		goto drop;

	  case SIGC_ALERTING:
		if (c->cstate == CALLST_U1 || c->cstate == CALLST_U3) {
			/* Q.2971:Call-Control-U 37/39 (U1) */
			/* Q.2971:Call-Control-U 7/39 (U3) */
			unx_alerting(c, msg, u, CALLST_U4);
			break;
		}
		if (c->cstate == CALLST_N6) {
			/* Q.2971:Call-Control-N 9/39 (N6) */
			/* Q.2971:Call-Control-N 17/39 (N9) */
			unx_alerting(c, msg, u, CALLST_N7);
			break;
		}
		(void)uni_decode_body(msg, u, &c->uni->cx);
		uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
		    &u->u.alerting.epref, -1);
		goto drop;

	  case SIGC_CONNECT:
		if (c->cstate == CALLST_U1 || c->cstate == CALLST_U3 ||
		    c->cstate == CALLST_U4) {
			/* Q.2971:Call-Control-U 7-8/39  (U3) */
			/* Q.2971:Call-Control-U 11/39   (U4) */
			/* Q.2971:Call-Control-U 37/39   (U1) */
			unx_connect(c, msg, u, CALLST_U10);
			break;
		}
		if (c->cstate == CALLST_N6 || c->cstate == CALLST_N7 ||
		    c->cstate == CALLST_N9) {
			/* Q.2971:Call-Control-N 9-10/39 (N6) */
			/* Q.2971:Call-Control-N 14/39   (N7) */
			/* Q.2971:Call-Control-N 17/39   (N9) */
			unx_connect(c, msg, u, CALLST_N8);
			break;
		}
		(void)uni_decode_body(msg, u, &c->uni->cx);
		uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
		    &u->u.connect.epref, -1);
		goto drop;

	  case SIGC_CONNECT_ACK:
		if (c->cstate == CALLST_U8) {
			/* Q.2971:Call-Control-U 15-16/39 */
			u8_connect_ack(c, msg, u, CALLST_U10);
			break;
		}
		if (c->cstate == CALLST_N10) {
			/* Q.2971:Call-Control-N 18/39 */
			n10_connect_ack(c, msg, u);
			break;
		}
		uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP, NULL, 0);
		goto drop;

	  case SIGC_RELEASE:
		switch (c->cstate) {

		  default:
			uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP, NULL, 0);
			goto drop;

		  case CALLST_U11:
		  case CALLST_N12:
			/* Q.2971:Call-Control-U 28/39 */
			/* Q.2971:Call-Control-N 30/39 */
			u11n12_release(c, msg, u);
			break;

		  case CALLST_U1:
		  case CALLST_U3:
		  case CALLST_U4:
		  case CALLST_U6:
		  case CALLST_U7:
		  case CALLST_U8:
		  case CALLST_U9:
		  case CALLST_U10:
		  case CALLST_U12:
			/* Q.2971:Call-Control-U 25/39 */
			unx_release(c, msg, u, CALLST_U12);
			break;

		  case CALLST_N1:
		  case CALLST_N3:
		  case CALLST_N4:
		  case CALLST_N6:
		  case CALLST_N7:
		  case CALLST_N8:
		  case CALLST_N9:
		  case CALLST_N10:
		  case CALLST_N11:
			/* Q.2971:Call-Control-N 26/39 */
			unx_release(c, msg, u, CALLST_N11);
			break;
		}
		break;

	  case SIGC_RELEASE_COMPL:
		/* Q.2971:Call-Control-U 25/39 */
		/* Q.2971:Call-Control-N 26/39 */
		unx_release_compl(c, msg, u);
		break;

	  case SIGC_NOTIFY:
		/* Q.2971:Call-Control-U 18/39 */
		/* Q.2971:Call-Control-N 19/39 */
		unx_notify(c, msg, u);
		break;

	  case SIGC_STATUS:
		if (c->cstate == CALLST_U11 || c->cstate == CALLST_U12 ||
		    c->cstate == CALLST_N11 || c->cstate == CALLST_N12) {
			/* Q.2971:Call-Control-U 29/39 (U11) */
			/* Q.2971:Call-Control-U 30/39 (U12) */
			/* Q.2971:Call-Control-N 29/39 (N11) */
			/* Q.2971:Call-Control-N 31/39 (N12) */
			un11un12_status(c, msg, u);
			break;
		}
		/* Q.2971:Call-Control-U 32/39 */
		/* Q.2971:Call-Control-N 33/39 */
		unx_status(c, msg, u);
		break;

	  case SIGC_STATUS_ENQ:
		/* Q.2971:Call-Control-U 31/39 */
		/* Q.2971:Call-Control-N 32/39 */
		unx_status_enq(c, msg, u);
		break;

	  case SIGC_ADD_PARTY:
		(void)uni_decode_body(msg, u, &c->uni->cx);

		if (c->type != CALL_LEAF && c->type != CALL_ROOT) {
			uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
			    &u->u.add_party.epref, UNI_EPSTATE_NULL);
			goto drop;
		}
		switch (c->cstate) {
		  case CALLST_U7:
		  case CALLST_U8:
		  case CALLST_U10:
		  case CALLST_N4:
		  case CALLST_N10:
			/* Q.2971:Call-Control-U 14/39  U7 */
			/* Q.2971:Call-Control-U 15/39  U8 */
			/* Q.2971:Call-Control-U 21/39  U10 */
			/* Q.2971:Call-Control-N 8/39   N4 */
			/* Q.2971:Call-Control-N 21/39  N10 */
			unx_add_party(c, msg, u, 1);
			break;

		  default:
			unx_add_party(c, msg, u, 0);
			goto drop;
		}
		break;

	  case SIGC_PARTY_ALERTING:
		(void)uni_decode_body(msg, u, &c->uni->cx);

		if (c->type != CALL_ROOT) {
			uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
			    &u->u.party_alerting.epref, -1);
			goto drop;
		}
		switch (c->cstate) {

		  default:
			/* Q.2971 9.5.3.2.3a) */
			unx_party_alerting(c, msg, u, 0);
			break;

		  case CALLST_U4:
		  case CALLST_U10:
			/* Q.2971:Call-Control-U 9/39   U4 */
			/* Q.2971:Call-Control-U 21/39  U10 */
			/* Q.2971:Call-Control-N 12/39  N7 */
			/* Q.2971:Call-Control-N 15/39  N8 */
			/* Q.2971:Call-Control-N 22/39  N10 */
			unx_party_alerting(c, msg, u, 1);
			break;
		}
		break;

	  case SIGC_ADD_PARTY_ACK:
		(void)uni_decode_body(msg, u, &c->uni->cx);

		if (c->type != CALL_ROOT) {
			uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
			    &u->u.add_party_rej.epref, -1);
			goto drop;
		}
		switch (c->cstate) {

		  case CALLST_U10:
			/* Q.2971:Call-Control-U 21/39 U10 */
			/* Q.2971:Call-Control-N 15/39 N8 */
			/* Q.2971:Call-Control-N 22/39 N10 */
			un10n8_add_party_ack(c, msg, u, 1);
			break;

		  default:
			/* Q.2971 9.5.3.2.3a) */
			un10n8_add_party_ack(c, msg, u, 0);
			break;
		}
		break;

	  case SIGC_ADD_PARTY_REJ:
		(void)uni_decode_body(msg, u, &c->uni->cx);

		if (c->type != CALL_ROOT) {
			uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
			    &u->u.add_party_rej.epref, -1);
			goto drop;
		}
		switch (c->cstate) {

		  case CALLST_U4:
	     	  case CALLST_U10:
		  case CALLST_N7:
		  case CALLST_N8:
		  case CALLST_N10:
			/* Q.2971:Call-Control-U 9/39 U4 */
			/* Q.2971:Call-Control-U 21/39 U10 */
			/* Q.2971:Call-Control-N 12/39 N7 */
			/* Q.2971:Call-Control-N 15/39 N8 */
			/* Q.2971:Call-Control-N 22/39 N10 */
			unx_add_party_rej(c, msg, u, 1);
			break;

		  default:
			/* Q.2971: 9.5.3.2.3b */
			unx_add_party_rej(c, msg, u, 0);
			break;
		}
		break;

	  case SIGC_DROP_PARTY:
		(void)uni_decode_body(msg, u, &c->uni->cx);

		if (c->type != CALL_ROOT && c->type != CALL_LEAF) {
			uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
			    &u->u.drop_party.epref, -1);
			goto drop;
		}
		switch (c->cstate) {
		  case CALLST_U11:
		  case CALLST_U12:
		  case CALLST_N11:
		  case CALLST_N12:
			/* Q.2971:Call-Control-U 28/39 U11 */
			/* Q.2971:Call-Control-U 30/39 U12 */
			/* Q.2971:Call-Control-N 29/39 N11 */
			/* Q.2971:Call-Control-N 30/39 N12 */
			goto drop;

		  case CALLST_NULL:
			uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
			    &u->u.drop_party.epref, UNI_EPSTATE_NULL);
			goto drop;

		  case CALLST_U3:
		  case CALLST_N3:
			/* L3MU_17_38 */
			unx_drop_party(c, msg, u, 0);
			break;

		  case CALLST_U8:
			if (c->uni->sb_tb) {
				/* L3MU_06_0[3-6] */
				unx_drop_party(c, msg, u, 0);
				break;
			}
			/* FALLTHRU */

		  default:
			/* Q.2971:Call-Control-U 26/39 Ux */
			/* Q.2971:Call-Control-U 21/39 U10 */
			/* Q.2971:Call-Control-N 27/39 Nx */
			/* Q.2971:Call-Control-N 21/39 N10 */
			unx_drop_party(c, msg, u, 1);
			break;
		}
		break;

	  case SIGC_DROP_PARTY_ACK:
		(void)uni_decode_body(msg, u, &c->uni->cx);

		if (c->type != CALL_ROOT && c->type != CALL_LEAF) {
			uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
			    &u->u.drop_party_ack.epref, -1);
			goto drop;
		}
		switch (c->cstate) {

		  case CALLST_U11:
		  case CALLST_U12:
			/* Q.2971:Call-Control-U 28/39 U11 */
			/* Q.2971:Call-Control-U 30/39 U12 */
			/* Q.2971:Call-Control-N 29/39 N11 */
			/* Q.2971:Call-Control-N 30/39 N12 */
			goto drop;

		  case CALLST_NULL:
			uni_bad_message(c, u, UNI_CAUSE_MSG_INCOMP,
			    &u->u.drop_party.epref, UNI_EPSTATE_NULL);
			goto drop;

		  case CALLST_U4:
		  case CALLST_N4:
		  case CALLST_U7:
		  case CALLST_N7:
		  case CALLST_U8:
		  case CALLST_N8:
		  case CALLST_U10:
		  case CALLST_N10:
			/* Q.2971:Call-Control-U 26/39 Ux */
			/* Q.2971:Call-Control-U 21/39 U10 */
			/* Q.2971:Call-Control-N 27/39 Nx */
			/* Q.2971:Call-Control-N 22/39 N10 */
			unx_drop_party_ack(c, msg, u, 1);
			break;

		  default:
			/* Q.2971 10.5 4th paragraph */
			unx_drop_party_ack(c, msg, u, 0);
			break;
		}
		break;

	  case SIGC_COBISETUP:	/* XXX */
		unx_unknown(c, msg, u);
		break;

	  /*
	   * User signals
	   */
	  case SIGC_SETUP_request:
		if (c->cstate == CALLST_NULL) {
			/* Q.2971:Call-Control-U 4/39 (U0) */
			/* Q.2971:Call-Control-N 4/39 (N0) */
			if (c->uni->proto == UNIPROTO_UNI40N)
				un0_setup_request(c, msg, cookie, CALLST_N6);
			else
				un0_setup_request(c, msg, cookie, CALLST_U1);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "setup.request in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_SETUP_response:
		if (c->cstate == CALLST_U6 || c->cstate == CALLST_U9 ||
		    c->cstate == CALLST_U7) {
			/* Q.2971:Call-Control-U 13/39	(U6) */
			/* Q.2971:Call-Control-U 14/39	(U7) */
			/* Q.2971:Call-Control-U 17/39	(U9) */
			unx_setup_response(c, msg, cookie, CALLST_U8);
			break;
		}
		if (c->cstate == CALLST_N1 || c->cstate == CALLST_N3 ||
		    c->cstate == CALLST_N4) {
			/* Q.2971:Call-Control-N 39/39  (N1) */
			/* Q.2971:Call-Control-N 7/39   (N3) */
			/* Q.2971:Call-Control-N 8/39   (N4) */
			unx_setup_response(c, msg, cookie, CALLST_N10);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "setup.response in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_SETUP_COMPLETE_request:
		if (c->cstate == CALLST_N8) {
			/* Q.2971:Call-Control-N 15/39 (N8) */
			n8_setup_compl_request(c, msg, cookie, CALLST_N10);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "setup_compl.request in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_PROCEEDING_request:
		if (c->cstate == CALLST_U6) {
			/* Q.2971:Call-Control-U 12/39 (U6) */
			u6n1_proceeding_request(c, msg, cookie, CALLST_U9);
			break;
		}
		if (c->cstate == CALLST_N1) {
			/* Q.2971:Call-Control-N 6/39 (N1) */
			u6n1_proceeding_request(c, msg, cookie, CALLST_N3);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "proceeding.request in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_ALERTING_request:
		if (c->cstate == CALLST_U6 || c->cstate == CALLST_U9) {
			/* Q.2971:Call-Control-U 13/39 (U6) */
			/* Q.2971:Call-Control-U 17/39 (U9) */
			unx_alerting_request(c, msg, cookie, CALLST_U7);
			break;
		}
		if (c->cstate == CALLST_N1 || c->cstate == CALLST_N3) {
			/* Q.2971:Call-Control-N 38/39 (N1) */
			/* Q.2971:Call-Control-N 7/39  (N3) */
			unx_alerting_request(c, msg, cookie, CALLST_N4);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "alerting.request in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_RELEASE_request:
		switch (c->cstate) {

		  case CALLST_U1:
		  case CALLST_U3:
		  case CALLST_U4:
		  case CALLST_U6:
		  case CALLST_U7:
		  case CALLST_U8:
		  case CALLST_U9:
		  case CALLST_U10:
			/* Q.2971:Call-Control-U 27/39 */
			unx_release_request(c, msg, cookie, CALLST_U11);
			break;

		  case CALLST_N1:
		  case CALLST_N3:
		  case CALLST_N4:
		  case CALLST_N6:
		  case CALLST_N7:
		  case CALLST_N8:
		  case CALLST_N9:
		  case CALLST_N10:
			/* Q.2971:Call-Control-N 28/39 */
			unx_release_request(c, msg, cookie, CALLST_N12);
			break;

		  case CALLST_U11:
		  case CALLST_U12:
		  case CALLST_N11:
		  case CALLST_N12:
		  case CALLST_NULL:
			VERBOSE(c->uni, UNI_FAC_ERR, 1,
			    "release.request in cs=%s",
			    callstates[c->cstate].name);
			uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE,
			    cookie);
			uni_msg_destroy(msg);
			break;
		}
		break;

	  case SIGC_RELEASE_response:
		if (c->cstate == CALLST_U6 || c->cstate == CALLST_U12 ||
		    c->cstate == CALLST_N1 || c->cstate == CALLST_N11) {
			/* Q.2971:Call-Control-U 12/39 (U6) */
			/* Q.2971:Call-Control-U 30/39 (U12) */
			/* Q.2971:Call-Control-N 6/39  (N1) */
			/* Q.2971:Call-Control-N 29/39 (N11) */
			unx_release_response(c, msg, cookie);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "release.response in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_NOTIFY_request:
		/* Q.2971:Call-Control-U 18/39 */
		/* Q.2971:Call-Control-N 19/39 */
		unx_notify_request(c, msg, cookie);
		break;

	  case SIGC_STATUS_ENQUIRY_request:
		/* Q.2971:Call-Control-U 31/39 */
		/* Q.2971:Call-Control-N 32/39 */
		unx_status_enquiry_request(c, msg, cookie);
		break;

	  case SIGC_ADD_PARTY_request:
		if (c->cstate == CALLST_U4 || c->cstate == CALLST_U10 ||
		    c->cstate == CALLST_N7 || c->cstate == CALLST_N10) {
			/* Q.2971:Call-Control-U 9-10/39 (U4) */
			/* Q.2971:Call-Control-U 21/39 (U10) */
			/* Q.2971:Call-Control-N 12/39 (N7) */
			/* Q.2971:Call-Control-N 22/39 (N10) */
			unx_add_party_request(c, msg, cookie);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "add-party.request in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_PARTY_ALERTING_request:
		if (c->cstate == CALLST_U7 || c->cstate == CALLST_U8 ||
		    c->cstate == CALLST_U10 ||
		    c->cstate == CALLST_N4 || c->cstate == CALLST_N10) {
			/* Q.2971:Call-Control-U 14/39 U7 */
			/* Q.2971:Call-Control-U 15/39 U8 */
			/* Q.2971:Call-Control-U 21/39 U10 */
			/* Q.2971:Call-Control-N 8/39  N4 */
			/* Q.2971:Call-Control-N 22/39 N10 */
			unx_party_alerting_request(c, msg, cookie);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1,
		    "party-alerting.request in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_ADD_PARTY_ACK_request:
		if (c->cstate == CALLST_U10 || c->cstate == CALLST_N10) {
			/* Q.2971:Call-Control-U 21/39 (U10) */
			/* Q.2971:Call-Control-N 22/39 (N10)*/
			un10_add_party_ack_request(c, msg, cookie);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1,
		    "add-party-ack.request in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_ADD_PARTY_REJ_request:
		if (c->cstate == CALLST_U7 || c->cstate == CALLST_U8 ||
		    c->cstate == CALLST_U10 ||
		    c->cstate == CALLST_N4 || c->cstate == CALLST_N10) {
			/* Q.2971:Call-Control-U 14/39 U7 */
			/* Q.2971:Call-Control-U 15/39 U8 */
			/* Q.2971:Call-Control-U 21/39 U10 */
			/* Q.2971:Call-Control-N 8/39  N4 */
			/* Q.2971:Call-Control-N 22/39 N10 */
			unx_add_party_rej_request(c, msg, cookie);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1,
		    "add-party-rej.request in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_DROP_PARTY_request:
		if (c->cstate != CALLST_U11 && c->cstate != CALLST_U12 &&
		    c->cstate != CALLST_N11 && c->cstate != CALLST_N12 &&
		    c->cstate != CALLST_NULL) {
			/* Q.2971:Call-Control-U 21/39 U10 */
			/* Q.2971:Call-Control-U 26/39 U1-U9 */
			/* Q.2971:Call-Control-N 22/39 N10 */
			/* Q.2971:Call-Control-N 27/39 N1-N9 */
			unx_drop_party_request(c, msg, cookie);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "drop-party.request in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_DROP_PARTY_ACK_request:
		if (c->cstate != CALLST_U11 && c->cstate != CALLST_U12 &&
		    c->cstate != CALLST_N11 && c->cstate != CALLST_N12 &&
		    c->cstate != CALLST_NULL) {
			/* Q.2971:Call-Control-U 21/39 U10 */
			/* Q.2971:Call-Control-U 26/39 U1-U9 */
			/* Q.2971:Call-Control-N 22/39 N10 */
			/* Q.2971:Call-Control-N 27/39 N1-N9 */
			unx_drop_party_ack_request(c, msg, cookie);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1,
		    "drop-party-ack.request in cs=%s",
		    callstates[c->cstate].name);
		uniapi_call_error(c, UNIAPI_ERROR_BAD_CALLSTATE, cookie);
		uni_msg_destroy(msg);
		break;

	  case SIGC_ABORT_CALL_request:
	    {
		struct uni *uni = c->uni;

		uni_destroy_call(c, 0);
		uniapi_uni_error(uni, UNIAPI_OK, cookie, UNI_CALLSTATE_U0);
		break;
	    }

	  /*
	   * Timers
	   */
	  case SIGC_T301:
		if (c->cstate == CALLST_U4 || c->cstate == CALLST_N7) {
			/* Q.2971:Call-Control-U Missing */
			/* Q.2971:Call-Control-N 14/39 */
			u4n7_t301(c);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "T301 in cs=%s",
		    callstates[c->cstate].name);
		break;

	  case SIGC_T303:
		if (c->cstate == CALLST_U1 || c->cstate == CALLST_N6) {
			/* Q.2971:Call-Control-U 6/39 */
			/* Q.2971:Call-Control-N 11/39 */
			u1n6_t303(c);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "T303 in cs=%s",
		    callstates[c->cstate].name);
		break;

	  case SIGC_T308:
		if (c->cstate == CALLST_U11 || c->cstate == CALLST_N12) {
			/* Q.2971:Call-Control-U 28/39 */
			/* Q.2971:Call-Control-N 30/39 */
			u11n12_t308(c);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "T308 in cs=%s",
		    callstates[c->cstate].name);
		break;

	  case SIGC_T310:
		if (c->cstate == CALLST_U3 || c->cstate == CALLST_N9) {
			/* Q.2971:Call-Control-U 7/39 */
			/* Q.2971:Call-Control-N 17/39 */
			u3n9_t310(c);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "T310 in cs=%s",
		    callstates[c->cstate].name);
		break;

	  case SIGC_T313:
		if (c->cstate == CALLST_U8) {
			/* Q.2971:Call-Control-U 15/39 */
			u8_t313(c);
			break;
		}
		VERBOSE(c->uni, UNI_FAC_ERR, 1, "T313 in cs=%s",
		    callstates[c->cstate].name);
		break;

	  case SIGC_T322:
		/* Q.2971:Call-Control-U 34/39 */
		/* Q.2971:Call-Control-N 35/39 */
		unx_t322(c);
		break;

	  case SIGC_CALL_DELETE:
		CALL_FREE(c);
		break;

	  /*
	   * Party-Control
	   */
	  case SIGC_DROP_PARTY_indication:
		if (c->uni->proto == UNIPROTO_UNI40U)
			/* Q.2971:Call-Control-U 23/39 */
			ux_drop_party_indication(c, msg);
		else
			/* Q.2971:Call-Control-N 23/39 */
			nx_drop_party_indication(c, msg);
		break;

	  case SIGC_DROP_PARTY_ACK_indication:
		if (c->uni->proto == UNIPROTO_UNI40U)
			/* Q.2971:Call-Control-U 23/39 */
			ux_drop_party_ack_indication(c, msg);
		else
			/* Q.2971:Call-Control-N 23/39 */
			nx_drop_party_ack_indication(c, msg);
		break;

	  case SIGC_ADD_PARTY_REJ_indication:
		if (c->uni->proto == UNIPROTO_UNI40U)
			/* Q.2971:Call-Control-U 23/39 */
			ux_add_party_rej_indication(c, msg);
		else
			/* Q.2971:Call-Control-N 23/39 */
			nx_add_party_rej_indication(c, msg);
		break;


	  case SIGC_SEND_DROP_PARTY:
		/* Q.2971:Call-Control-U 21/39 */
		/* Q.2971:Call-Control-U 25/39 */
		if (uni_party_act_count(c, 2) != 0)
			(void)uni_send_output(u, c->uni);
		else if(c->cstate != CALLST_U11) {
			c->uni->cause = u->u.drop_party.cause;
			clear_callD(c);
		}
		UNI_FREE(u);
		break;

	  case SIGC_SEND_DROP_PARTY_ACK:
		/* Q.2971:Call-Control-U 21/39 */
		/* Q.2971:Call-Control-U 25/39 */
		if (uni_party_act_count(c, 2) != 0)
			(void)uni_send_output(u, c->uni);
		else if (c->cstate != CALLST_U11) {
			c->uni->cause = u->u.drop_party_ack.cause;
			clear_callD(c);
		}
		UNI_FREE(u);
		break;

	  case SIGC_SEND_ADD_PARTY_REJ:
		/* Q.2971:Call-Control-U 21/39 */
		/* Q.2971:Call-Control-U 24/39 */
		unx_send_add_party_rej(c, u);
		break;

	  case SIGC_SEND_STATUS_ENQ:
		/* Q.2971:Call-Control-U 21/39 */
		/* Q.2971:Call-Control-U 25/39 */
		unx_send_party_status_enq(c, u);
		break;

	  case SIGC_PARTY_DESTROYED:
		c->uni->funcs->uni_output(c->uni, c->uni->arg,
		    UNIAPI_PARTY_DESTROYED, cookie, msg);
		break;

	  case SIGC_END:
		break;
	}

	return;

  drop:
	/*
	 * This is for SAAL message signals that should be dropped.
	 */
	uni_msg_destroy(msg);
	UNI_FREE(u);
}

/**********************************************************************/

/*
 * Timeout functions
 */
static void
t308_func(struct call *c)
{
	uni_enq_call(c, SIGC_T308, 0, NULL, NULL);
}
static void
t303_func(struct call *c)
{
	uni_enq_call(c, SIGC_T303, 0, NULL, NULL);
}
static void
t301_func(struct call *c)
{
	uni_enq_call(c, SIGC_T301, 0, NULL, NULL);
}
static void
t310_func(struct call *c)
{
	uni_enq_call(c, SIGC_T310, 0, NULL, NULL);
}
static void
t313_func(struct call *c)
{
	uni_enq_call(c, SIGC_T313, 0, NULL, NULL);
}

static void
t322_func(struct call *c)
{
	uni_enq_call(c, SIGC_T322, 0, NULL, NULL);
}

/**********************************************************************/

/*
 * Check whether the peer state is compatible with our state.
 * Return the new callstate we should go to (either U0 or the current
 * state).
 * None of the state is U0 here. My state is not U11 or U12.
 *
 * Well, this turns out to be not so easy: the status enquiry could have
 * been sent before we changed into the current state - the status will
 * report a previous state without anything been lost.
 *
 * Incoming states are incompatible with outgoing states. Everything is ok.
 */
static enum call_state
state_compat(struct call *c, enum uni_callstate peer)
{
	if ((c->cstate == CALLST_U1 ||
	     c->cstate == CALLST_U3 ||
	     c->cstate == CALLST_U4) &&
	   (peer == UNI_CALLSTATE_N6 ||
	    peer == UNI_CALLSTATE_N7 ||
	    peer == UNI_CALLSTATE_N8 ||
	    peer == UNI_CALLSTATE_N9))
		return (CALLST_NULL);

	if ((c->cstate == CALLST_N6 ||
	     c->cstate == CALLST_N7 ||
	     c->cstate == CALLST_N8 ||
	     c->cstate == CALLST_N9) &&
	    (peer == UNI_CALLSTATE_U1 ||
	     peer == UNI_CALLSTATE_U3 ||
	     peer == UNI_CALLSTATE_U4))
		return (CALLST_NULL);

	if ((peer == UNI_CALLSTATE_N1 ||
	     peer == UNI_CALLSTATE_N3 ||
	     peer == UNI_CALLSTATE_N4) &&
	   (c->cstate == CALLST_U6 ||
	    c->cstate == CALLST_U7 ||
	    c->cstate == CALLST_U8 ||
	    c->cstate == CALLST_N9))
		return (CALLST_NULL);

	if ((peer == UNI_CALLSTATE_U6 ||
	     peer == UNI_CALLSTATE_U7 ||
	     peer == UNI_CALLSTATE_U8 ||
	     peer == UNI_CALLSTATE_U9) &&
	   (c->cstate == CALLST_N1 ||
	    c->cstate == CALLST_N3 ||
	    c->cstate == CALLST_N4))
		return (CALLST_NULL);

	return (c->cstate);
}
