/*
 * Copyright (c) 1996-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
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
 * $Begemot: libunimsg/netnatm/sig/sig_reset.c,v 1.11 2004/08/05 07:11:03 brandt Exp $
 *
 * Reset-start and reset-respond
 */

#include <netnatm/unimsg.h>
#include <netnatm/saal/sscfudef.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/sig/uni.h>

#include <netnatm/sig/unipriv.h>
#include <netnatm/sig/unimkmsg.h>

static void response_restart(struct uni *, struct uni_msg *, struct uni_all *);
static void response_status(struct uni *, struct uni_msg *, struct uni_all *);

static void response_t317(struct uni *);

static void response_error(struct uni *, struct uniapi_reset_error_response *,
    uint32_t cookie);
static void response_response(struct uni *, struct uniapi_reset_response *,
    uint32_t);

static void start_request(struct uni *, struct uniapi_reset_request *,
    uint32_t);

static void start_t316(struct uni *);

static void start_restart_ack(struct uni *, struct uni_msg *, struct uni_all *);
static void start_status(struct uni *, struct uni_msg *, struct uni_all *);

static int restart_forward(struct uni *, const struct uni_all *);

#define DEF_PRIV_SIG(NAME, FROM)	[SIG##NAME] =	"SIG"#NAME,
static const char *const start_sigs[] = {
	DEF_START_SIGS
};
#undef DEF_PRIV_SIG

#define DEF_PRIV_SIG(NAME, FROM)	[SIG##NAME] =	"SIG"#NAME,
static const char *const respond_sigs[] = {
	DEF_RESPOND_SIGS
};
#undef DEF_PRIV_SIG

TIMER_FUNC_UNI(t317, t317_func)
TIMER_FUNC_UNI(t316, t316_func)

/*
 * Reset-Start process.
 */
void
uni_sig_start(struct uni *uni, u_int sig, uint32_t cookie,
    struct uni_msg *m, struct uni_all *u)
{
	if (sig >= SIGS_END) {
		VERBOSE(uni, UNI_FAC_ERR, 1, "Signal %d outside of range to "
		    "Reset-Start", sig);
		if (m)
			uni_msg_destroy(m);
		if (u)
			UNI_FREE(u);
		return;
	}

	VERBOSE(uni, UNI_FAC_RESTART, 1,
	    "Signal %s in state %u of Reset-Start; cookie %u",
	    start_sigs[sig], uni->glob_start, cookie);

	switch (sig) {

	/*
	 * User requests
	 */
	  case SIGS_RESET_request:
		start_request(uni,
		    uni_msg_rptr(m, struct uniapi_reset_request *), cookie);
		uni_msg_destroy(m);
		break;

	/*
	 * Timers
	 */
	  case SIGS_T316:
		start_t316(uni);
		break;

	/*
	 * SAAL
	 */
	  case SIGS_RESTART_ACK:
		start_restart_ack(uni, m, u);
		uni_msg_destroy(m);
		UNI_FREE(u);
		break;

	  case SIGS_STATUS:
		start_status(uni, m, u);
		uni_msg_destroy(m);
		UNI_FREE(u);
		break;

	  case SIGS_END:
		break;
	}
}

/*
 * Reset-request from USER.
 *
 * Q.2931:Reset-Start 1/2
 */
static void
start_request(struct uni *uni, struct uniapi_reset_request *req, uint32_t cookie)
{
	struct uni_all *resp;
	int err;

	if (uni->glob_start != UNI_CALLSTATE_REST0) {
		uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALLSTATE, cookie, 0);
		return;
	}

	if ((resp = UNI_ALLOC()) == NULL) {
		uniapi_uni_error(uni, UNIAPI_ERROR_NOMEM, cookie, 0);
		return;
	}

	MK_MSG_ORIG(resp, UNI_RESTART, 0, 0);
	resp->u.restart.restart = req->restart;
	resp->u.restart.connid = req->connid;

	if (restart_forward(uni, resp))
		return;

	uni->connid_start = req->connid;
	uni->restart_start = req->restart;

	if ((err = uni_send_output(resp, uni)) != 0)
		uniapi_uni_error(uni, UNIAPI_ERROR_ENCODING, cookie, 0);
	UNI_FREE(resp);
	if (err)
		return;

	uni->cnt316 = 0;
	TIMER_START_UNI(uni, t316, uni->timer316);
	uni->glob_start = UNI_CALLSTATE_REST1;

	VERBOSE(uni, UNI_FAC_RESTART, 1, "Reset-Start state := 1");


	uniapi_uni_error(uni, UNIAPI_OK, cookie, 0);
}

/*
 * T316 timeout function
 */
static void
t316_func(struct uni *uni)
{
	uni_enq_start(uni, SIGS_T316, 0, NULL, NULL);
}

/*
 * Q.2931:Reset-Start 1/2
 */
static void
start_t316(struct uni *uni)
{
	if (uni->glob_start != UNI_CALLSTATE_REST1) {
		VERBOSE0(uni, UNI_FAC_ERR, "T316 in state %d",
		    uni->glob_start);
		return;
	}

	if (++uni->cnt316 == uni->init316) {
		struct uni_msg *app;
		struct uniapi_reset_error_indication *resp;

		VERBOSE(uni, UNI_FAC_RESTART, 1, "Reset-Start error");

		resp = ALLOC_API(struct uniapi_reset_error_indication, app);
		if (resp != NULL) {
			resp->source = 0;
			resp->reason = UNIAPI_RESET_ERROR_NO_RESPONSE,

			uni->funcs->uni_output(uni, uni->arg,
			    UNIAPI_RESET_ERROR_indication, 0, app);
		}

		uni->glob_start = UNI_CALLSTATE_REST0;
		VERBOSE(uni, UNI_FAC_RESTART, 1, "Reset-Start state := 0");
	} else {
		struct uni_all *resp;

		if ((resp = UNI_ALLOC()) == NULL)
			return;

		MK_MSG_ORIG(resp, UNI_RESTART, 0, 0);
		resp->u.restart.restart = uni->restart_start;
		resp->u.restart.connid = uni->connid_start;

		(void)uni_send_output(resp, uni);

		UNI_FREE(resp);

		TIMER_START_UNI(uni, t316, uni->timer316);
	}
}

/*
 * Got RESTART_ACK.
 */
static void
start_restart_ack(struct uni *uni, struct uni_msg *m, struct uni_all *u)
{
	enum uni_callstate new_state;
	struct uniapi_reset_confirm *conf;
	struct uni_msg *app;

	if (uni->glob_start == UNI_CALLSTATE_REST0) {
		uni_respond_status_mtype(uni, &u->u.hdr.cref, uni->glob_start,
		    UNI_CAUSE_MSG_INCOMP, UNI_RESTART_ACK);
		return;
	}

	if (uni->glob_start != UNI_CALLSTATE_REST1) {
		ASSERT(0, ("bad global call state in Reset-Start"));
		return;
	}

	/*
	 * If body decoding fails, this is because IEs are wrong.
	 */
	(void)uni_decode_body(m, u, &uni->cx);
	MANDATE_IE(uni, u->u.restart_ack.restart, UNI_IE_RESTART);

	if (IE_ISGOOD(u->u.restart_ack.restart)) {
		/*
		 * Q.2931: 5.5.2.2
		 */
		if (u->u.restart_ack.restart.rclass == UNI_RESTART_ALL &&
		    IE_ISGOOD(u->u.restart_ack.connid)) {
			(void)UNI_SAVE_IERR(&uni->cx, UNI_IE_CONNID,
			    u->u.restart_ack.connid.h.act,
			    UNI_IERR_UNK);
		} else if ((u->u.restart_ack.restart.rclass == UNI_RESTART_PATH ||
			    u->u.restart_ack.restart.rclass == UNI_RESTART_CHANNEL)) {
			MANDATE_IE(uni, u->u.restart_ack.connid, UNI_IE_CONNID);
		}
	}
	/*
	 * Compare the information elements now, because
	 * we may need the new callstate for the status message
	 * below.
	 */
	new_state = UNI_CALLSTATE_REST1;

	if (IE_ISGOOD(u->u.restart_ack.restart) &&
	    IE_ISGOOD(uni->restart_start) &&
	    u->u.restart_ack.restart.rclass == uni->restart_start.rclass &&
	    !IE_ISGOOD(u->u.restart_ack.connid) == !IE_ISGOOD(uni->connid_start) &&
	    (!IE_ISGOOD(uni->connid_start) ||
	       (u->u.restart_ack.connid.vpci == uni->connid_start.vpci &&
	        u->u.restart_ack.connid.vci == uni->connid_start.vci)))
		new_state = UNI_CALLSTATE_REST0;

	switch (uni_verify(uni, u->u.hdr.act)) {
	  case VFY_RAIM:
	  case VFY_RAI:
		uni_respond_status_verify(uni, &u->u.hdr.cref,
		    UNI_CALLSTATE_REST1, NULL, 0);
	  case VFY_I:
		return;

	  case VFY_CLR:
		uni->glob_start = UNI_CALLSTATE_REST0;
		VERBOSE(uni, UNI_FAC_RESTART, 1,
		    "Reset-Start state := 0");
		return;

	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(uni, &u->u.hdr.cref,
		    new_state, NULL, 0);
	  case VFY_OK:
		break;
	}

	if (new_state == UNI_CALLSTATE_REST1)
		/*
		 * Q.2931: 5.5.1.2/2
		 */
		return;

	/*
	 * Build restart.confirm signal for application
	 */
	if (!IE_ISGOOD(u->u.restart_ack.connid))
		u->u.restart.connid.h.present = 0;


	if ((conf = ALLOC_API(struct uniapi_reset_confirm, app)) == NULL)
		return;
	conf->restart = u->u.restart.restart;
	conf->connid = u->u.restart.connid;

	TIMER_STOP_UNI(uni, t316);

	uni->funcs->uni_output(uni, uni->arg, UNIAPI_RESET_confirm, 0, app);

	uni->glob_start = UNI_CALLSTATE_REST0;
	VERBOSE(uni, UNI_FAC_RESTART, 1, "Reset-Start state := 0");
}

/*
 * Reset-Start got a STATUS message.
 *
 * Q.2931: Reset-Start 2/2
 *
 * In Q.2931 only CALLSTATE_REST1 is allowed, this seems silly and to contradict
 * 5.6.12. So allow it in any state.
 *
 * The following states are considered compatible:
 *
 *  Sender   Receiver(we)
 *  ------   --------
 *  Rest0     Rest0	this is the normal state OK!
 *  Rest2     Rest0	this may be the result of no answer from the API
 *			on the remote end and the us finally timing out. ERROR!
 *  Rest2     Rest1	this is normal. OK!
 *  Rest0     Rest1	RESTART_ACK was probably lost. OK!
 *
 * All others are wrong.
 */
static void
start_status(struct uni *uni, struct uni_msg *m, struct uni_all *u)
{
	(void)uni_decode_body(m, u, &uni->cx);
	MANDATE_IE(uni, u->u.status.callstate, UNI_IE_CALLSTATE);
	MANDATE_IE(uni, u->u.status.cause, UNI_IE_CAUSE);
	switch (uni_verify(uni, u->u.hdr.act)) {
	  case VFY_CLR:
		uni->glob_start = UNI_CALLSTATE_REST0;
		VERBOSE(uni, UNI_FAC_RESTART, 1, "Reset-Start state := 0");
		return;

	  case VFY_RAIM:
	  case VFY_RAI:
	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(uni, &u->u.hdr.cref, uni->glob_start,
		    NULL, 0);
	  case VFY_I:
	  case VFY_OK:
		break;
	}
	if (!IE_ISGOOD(u->u.status.callstate)) {
		/*
		 * As a result of the strange handling above, we must
		 * process a STATUS with an invalid or missing callstate!
		 */
		return;
	}
	if ((u->u.status.callstate.state == UNI_CALLSTATE_REST0 &&
	     uni->glob_start == UNI_CALLSTATE_REST0) ||
	    (u->u.status.callstate.state == UNI_CALLSTATE_REST0 &&
	     uni->glob_start == UNI_CALLSTATE_REST1) ||
	    (u->u.status.callstate.state == UNI_CALLSTATE_REST2 &&
	     uni->glob_start == UNI_CALLSTATE_REST1)) {
		/*
		 * Implementation dependend procedure:
		 * Inform the API
		 */
		struct uniapi_reset_status_indication *resp;
		struct uni_msg *app;

		resp = ALLOC_API(struct uniapi_reset_status_indication, app);
		if (resp == NULL)
			return;
		resp->cref = u->u.hdr.cref;
		resp->callstate = u->u.status.callstate;
		if (IE_ISGOOD(u->u.status.cause))
			resp->cause = u->u.status.cause;

		uni->funcs->uni_output(uni, uni->arg,
		    UNIAPI_RESET_STATUS_indication, 0, app);
		
	} else {
		struct uniapi_reset_error_indication *resp;
		struct uni_msg *app;

		resp = ALLOC_API(struct uniapi_reset_error_indication, app);
		if (resp != NULL) {
			resp->source = 0;
			resp->reason = UNIAPI_RESET_ERROR_PEER_INCOMP_STATE,

			uni->funcs->uni_output(uni, uni->arg,
			    UNIAPI_RESET_ERROR_indication, 0, app);
		}
	}
}

/************************************************************/
/*
 * Reset-Respond process.
 */
void
uni_sig_respond(struct uni *uni, u_int sig, uint32_t cookie,
    struct uni_msg *m, struct uni_all *u)
{
	if (sig >= SIGR_END) {
		VERBOSE(uni, UNI_FAC_ERR, 1, "Signal %d outside of range to "
		    "Reset-Respond", sig);
		if (m)
			uni_msg_destroy(m);
		if (u)
			UNI_FREE(u);
		return;
	}

	VERBOSE(uni, UNI_FAC_RESTART, 1,
	    "Signal %s in state %u of Reset-Respond; cookie %u",
	    respond_sigs[sig], uni->glob_respond, cookie);

	switch (sig) {

	/*
	 * SAAL
	 */
	  case SIGR_RESTART:
		response_restart(uni, m, u);
		uni_msg_destroy(m);
		UNI_FREE(u);
		break;

	  case SIGR_STATUS:
		response_status(uni, m, u);
		uni_msg_destroy(m);
		UNI_FREE(u);
		break;

	/*
	 * User
	 */
	  case SIGR_RESET_ERROR_response:
		response_error(uni,
		    uni_msg_rptr(m, struct uniapi_reset_error_response *),
		    cookie);
		uni_msg_destroy(m);
		break;

	  case SIGR_RESET_response:
		response_response(uni,
		    uni_msg_rptr(m, struct uniapi_reset_response *), cookie);
		uni_msg_destroy(m);
		break;

	/*
	 * Timers
	 */
	  case SIGR_T317:
		response_t317(uni);
		return;

	  case SIGR_END:
		break;
	}
}

/*
 * Send a RELEASE_COMPLETE to all affected calls as per
 * F.2.3(3)
 */
static int
restart_forward(struct uni *uni, const struct uni_all *u)
{
	struct call *c;
	struct uni_all *resp;

	if ((resp = UNI_ALLOC()) == NULL)
		return (-1);

	TAILQ_FOREACH(c, &uni->calls, link) {
		if (u->u.restart.restart.rclass == UNI_RESTART_ALL ||
		    (IE_ISPRESENT(c->connid) &&
		    u->u.restart.connid.vpci == c->connid.vpci &&
		    (u->u.restart.restart.rclass == UNI_RESTART_PATH ||
		    u->u.restart.connid.vci == c->connid.vci))) {
			MK_MSG_ORIG(resp, UNI_RELEASE_COMPL, c->cref, c->mine);
			uni_release_compl(c, resp);
		}
	}

	UNI_FREE(resp);
	return (0);
}

/*
 * Respond process got a restart message.
 * Doesn't free the messages.
 */
static void
response_restart(struct uni *uni, struct uni_msg *m, struct uni_all *u)
{
	struct uni_msg *app;
	struct uniapi_reset_indication *ind;

	if (uni->glob_respond == UNI_CALLSTATE_REST0) {
		/*
		 * If body decoding fails, this is because IEs are wrong.
		 */
		(void)uni_decode_body(m, u, &uni->cx);
		MANDATE_IE(uni, u->u.restart.restart, UNI_IE_RESTART);
		if (IE_ISGOOD(u->u.restart.restart)) {
			/*
			 * Q.2931: 5.5.2.2
			 */
			if (u->u.restart.restart.rclass == UNI_RESTART_ALL &&
			   IE_ISGOOD(u->u.restart.connid)) {
				(void)UNI_SAVE_IERR(&uni->cx, UNI_IE_CONNID,
				    u->u.restart.connid.h.act,
				    UNI_IERR_UNK);
			} else if ((u->u.restart.restart.rclass == UNI_RESTART_PATH ||
				   u->u.restart.restart.rclass == UNI_RESTART_CHANNEL)) {
				MANDATE_IE(uni, u->u.restart.connid, UNI_IE_CONNID);
			}
		}
		switch (uni_verify(uni, u->u.hdr.act)) {
		  case VFY_RAIM:
		  case VFY_RAI:
			uni_respond_status_verify(uni, &u->u.hdr.cref,
			    UNI_CALLSTATE_REST0, NULL, 0);
		  case VFY_CLR:
		  case VFY_I:
			return;

		  case VFY_RAP:
		  case VFY_RAPU:
			uni_respond_status_verify(uni, &u->u.hdr.cref,
			    UNI_CALLSTATE_REST2, NULL, 0);
		  case VFY_OK:
			break;
		}
		if (!IE_ISGOOD(u->u.restart.connid))
			u->u.restart.connid.h.present = 0;

		/*
		 * Send a RELEASE_COMPLETE to all affected calls as per
		 * F.2.3(3)
		 */
		if (restart_forward(uni, u))
			return;

		/*
		 * Build restart signal for application
		 */
		if ((ind = ALLOC_API(struct uniapi_reset_indication, app)) == NULL)
			return;

		ind->restart = u->u.restart.restart;
		ind->connid = u->u.restart.connid;

		uni_enq_coord(uni, SIGO_RESET_indication, 0, app);

		TIMER_START_UNI(uni, t317, uni->timer317);
		uni->glob_respond = UNI_CALLSTATE_REST2;

		VERBOSE(uni, UNI_FAC_RESTART, 1, "Reset-Respond state := 2");


	} else if (uni->glob_respond == UNI_CALLSTATE_REST2) {
		/*
		 * No need to decode the message. It is unexpected in this
		 * state so return a status.
		 */
		uni_respond_status_mtype(uni, &u->u.hdr.cref, uni->glob_respond,
		    UNI_CAUSE_MSG_INCOMP, UNI_RESTART);


	} else
		ASSERT(0, ("bad global call state in responder"));
}

static void
response_t317(struct uni *uni)
{
	struct uniapi_reset_error_indication *resp;
	struct uni_msg *app;

	if (uni->glob_respond != UNI_CALLSTATE_REST2) {
		VERBOSE0(uni, UNI_FAC_ERR, "T317 in state %d",
		    uni->glob_respond);
		return;
	}

	VERBOSE(uni, UNI_FAC_RESTART, 1, "Reset-Respond error");

	if ((resp = ALLOC_API(struct uniapi_reset_error_indication, app)) != NULL) {
		resp->source = 1;
		resp->reason = UNIAPI_RESET_ERROR_NO_CONFIRM;

		uni->funcs->uni_output(uni, uni->arg,
		    UNIAPI_RESET_ERROR_indication, 0, app);
	}

	uni->glob_respond = UNI_CALLSTATE_REST0;
	VERBOSE(uni, UNI_FAC_RESTART, 1, "Reset-Respond state := 0");
}

/*
 * Error response from USER
 */
static void
response_error(struct uni *uni, struct uniapi_reset_error_response *c,
    uint32_t cookie)
{
	struct uni_all *resp;

	if (uni->glob_respond != UNI_CALLSTATE_REST2) {
		uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALLSTATE, cookie, 0);
		return;
	}

	if ((resp = UNI_ALLOC()) == NULL) {
		uniapi_uni_error(uni, UNIAPI_ERROR_NOMEM, cookie, 0);
		return;
	}

	MK_MSG_ORIG(resp, UNI_STATUS, 0, 1);
	MK_IE_CALLSTATE(resp->u.status.callstate, UNI_CALLSTATE_REST2);

	if (IE_ISGOOD(c->cause))
		resp->u.status.cause = c->cause;
	else {
		MK_IE_CAUSE(resp->u.status.cause, UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_CHANNEL_NEX);
		if (IE_ISGOOD(uni->connid_respond))
			ADD_CAUSE_CHANNID(resp->u.status.cause,
			    uni->connid_respond.vpci,
			    uni->connid_respond.vci);
	}

	if (uni_send_output(resp, uni) != 0) {
		uniapi_uni_error(uni, UNIAPI_ERROR_ENCODING, cookie, 0);
		UNI_FREE(resp);
		return;
	}

	uniapi_uni_error(uni, UNIAPI_OK, cookie, 0);
}

/*
 * Reset-response from user.
 */
static void
response_response(struct uni *uni, struct uniapi_reset_response *arg,
    uint32_t cookie)
{
	struct uni_all *resp;

	if (uni->glob_respond != UNI_CALLSTATE_REST2) {
		uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALLSTATE, cookie, 0);
		return;
	}

	if (!IE_ISGOOD(arg->restart)) {
		uniapi_uni_error(uni, UNIAPI_ERROR_MISSING_IE, cookie, 0);
		return;
	}

	if ((resp = UNI_ALLOC()) == NULL) {
		uniapi_uni_error(uni, UNIAPI_ERROR_NOMEM, cookie, 0);
		return;
	}

	TIMER_STOP_UNI(uni, t317);

	MK_MSG_ORIG(resp, UNI_RESTART_ACK, 0, 1);
	resp->u.restart.restart = arg->restart;
	if (IE_ISGOOD(arg->connid))
		resp->u.restart.connid = arg->connid;

	if (uni_send_output(resp, uni) != 0) {
		uniapi_uni_error(uni, UNIAPI_ERROR_ENCODING, cookie, 0);
		UNI_FREE(resp);
		return;
	}

	UNI_FREE(resp);

	uni->glob_respond = UNI_CALLSTATE_REST0;
	VERBOSE(uni, UNI_FAC_RESTART, 1, "Reset-Respond state := 0");

	uniapi_uni_error(uni, UNIAPI_OK, cookie, 0);
}

/*
 * Reset-Response got a STATUS message.
 *
 * Q.2931: Reset-Response 2/2
 *
 * In Q.2931 only CALLSTATE_REST2 is allowed, this seems silly and to contradict
 * 5.6.12. So allow it in any state.
 *
 * The following states are considered compatible:
 *
 *  Sender   Receiver
 *  ------   --------
 *  Rest0     Rest0	this is the normal state OK!
 *  Rest0     Rest2	this may be the result of no answer from the API
 *			and the Sender finally timing out. ERROR!
 *  Rest1     Rest2	this is normal. OK!
 *  Rest1     Rest0	RESTART_ACK was probably lost. OK!
 *
 * All others are wrong.
 */
static void
response_status(struct uni *uni, struct uni_msg *m, struct uni_all *u)
{
	(void)uni_decode_body(m, u, &uni->cx);
	MANDATE_IE(uni, u->u.status.callstate, UNI_IE_CALLSTATE);
	MANDATE_IE(uni, u->u.status.cause, UNI_IE_CAUSE);
	switch (uni_verify(uni, u->u.hdr.act)) {
	  case VFY_CLR:
		if (uni->proto == UNIPROTO_UNI40U) {
			uni->glob_respond = UNI_CALLSTATE_REST0;
			VERBOSE(uni, UNI_FAC_RESTART, 1,
			    "Reset-Respond state := 0");
			return;
		}
		break;

	  case VFY_RAIM:
	  case VFY_RAI:
	  case VFY_RAP:
	  case VFY_RAPU:
		uni_respond_status_verify(uni, &u->u.hdr.cref,
		    uni->glob_respond, NULL, 0);
	  case VFY_I:
	  case VFY_OK:
		break;
	}
	if (!IE_ISGOOD(u->u.status.callstate)) {
		/*
		 * As a result of the strange handling above, we must
		 * process a STATUS with an invalid or missing callstate!
		 */
		return;
	}
	if ((u->u.status.callstate.state == UNI_CALLSTATE_REST0 &&
	     uni->glob_respond == UNI_CALLSTATE_REST0) ||
	    (u->u.status.callstate.state == UNI_CALLSTATE_REST1 &&
	     uni->glob_respond == UNI_CALLSTATE_REST0) ||
	    (u->u.status.callstate.state == UNI_CALLSTATE_REST1 &&
	     uni->glob_respond == UNI_CALLSTATE_REST2)) {
		/*
		 * Implementation dependend procedure:
		 * Inform the API
		 */
		struct uniapi_reset_status_indication *resp;
		struct uni_msg *app;

		resp = ALLOC_API(struct uniapi_reset_status_indication, app);
		if (resp == NULL)
			return;

		resp->cref = u->u.hdr.cref;
		resp->callstate = u->u.status.callstate;
		if (IE_ISGOOD(u->u.status.cause))
			resp->cause = u->u.status.cause;

		uni->funcs->uni_output(uni, uni->arg,
		    UNIAPI_RESET_STATUS_indication, 0, app);
		
	} else {
		struct uniapi_reset_error_indication *resp;
		struct uni_msg *app;

		resp = ALLOC_API(struct uniapi_reset_error_indication, app);
		if (resp != NULL) {
			resp->source = 1;
			resp->reason = UNIAPI_RESET_ERROR_PEER_INCOMP_STATE,

			uni->funcs->uni_output(uni, uni->arg,
			    UNIAPI_RESET_ERROR_indication, 0, app);
		}
	}
}

/*
 * T317 timeout function
 */
static void
t317_func(struct uni *uni)
{
	uni_enq_resp(uni, SIGR_T317, 0, NULL, NULL);
}
