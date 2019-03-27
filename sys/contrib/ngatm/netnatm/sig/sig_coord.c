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
 * $Begemot: libunimsg/netnatm/sig/sig_coord.c,v 1.12 2004/08/05 07:11:01 brandt Exp $
 *
 * Coordinator
 */

#include <netnatm/unimsg.h>
#include <netnatm/saal/sscfudef.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/sig/uni.h>

#include <netnatm/sig/unipriv.h>
#include <netnatm/sig/unimkmsg.h>

#define STR(S) [S] = #S
static const char *const cunames[] = {
	STR(CU_STAT0),
	STR(CU_STAT1),
	STR(CU_STAT2),
	STR(CU_STAT3),
};

#define DEF_PRIV_SIG(NAME, FROM)	[SIG##NAME] =	"SIG"#NAME,
static const char *const coord_sigs[] = {
	DEF_COORD_SIGS
};
#undef DEF_PRIV_SIG

static void sig_all_calls(struct uni *, u_int sig);
static void set_custat(struct uni *, enum cu_stat);

static void input_dummy(struct uni *uni, struct uni_msg *m, struct uni_all *u);
static void input_global(struct uni *uni, struct uni_msg *m, struct uni_all *u);
static void input_unknown(struct uni *uni, struct uni_msg *m, struct uni_all *u);
static void input_cobi(struct call *c, struct uni_msg *m, struct uni_all *u);
static void input_call(struct call *c, struct uni_msg *m, struct uni_all *u);

TIMER_FUNC_UNI(t309, t309_func)

/*
 * All those 'bogus signal' printouts are not specified in the SDLs.
 */


/*
 * SAAL-ESTABLISH.indication
 *
 * This means either a resynchronisation or error-recovery or
 * an incoming SSCOP connection.
 */
static void
coord_saal_establish_indication(struct uni *uni)
{
	switch (uni->custat) {

	  case CU_STAT0:	/* Q.2931:Coord-U 4/10 */
	  case CU_STAT3:	/* Q.2931:Coord-U 5/10 */
		sig_all_calls(uni, SIGC_LINK_ESTABLISH_indication);
		set_custat(uni, CU_STAT3);
		break;

	  case CU_STAT1:
	  case CU_STAT2:
		VERBOSE0(uni, UNI_FAC_COORD,
		    "signal saal_establish.indication in CU%u", uni->custat);
		break;

	  default:
		ASSERT(0, ("CU_STAT*"));
	}
}

/*
 * SAAL-ESTABLISH.confirm
 */
static void
coord_saal_establish_confirm(struct uni *uni)
{
	switch (uni->custat) {

	  case CU_STAT0:
	  case CU_STAT2:
		VERBOSE0(uni, UNI_FAC_COORD,
		    "signal saal_establish.confirm in CU%u", uni->custat);
		break;

	  case CU_STAT1:
		/*
		 * Q.2931:Co-ord-U 4/10
		 */
		TIMER_STOP_UNI(uni, t309);
		sig_all_calls(uni, SIGC_LINK_ESTABLISH_confirm);
		uni->funcs->uni_output(uni, uni->arg,
		    UNIAPI_LINK_ESTABLISH_confirm, 0, NULL);
		set_custat(uni, CU_STAT3);
		break;

	  case CU_STAT3:
		/*
		 * Q.2931:Coord-U 5/10
		 */
		sig_all_calls(uni, SIGC_LINK_ESTABLISH_confirm);
		uni->funcs->uni_output(uni, uni->arg,
		    UNIAPI_LINK_ESTABLISH_confirm, 0, NULL);
		break;

	  default:
		ASSERT(0, ("CU_STAT*"));
	}
}

/*
 * SAAL-RELEASE.confirm
 */
static void
coord_saal_release_confirm(struct uni *uni)
{
	switch (uni->custat) {

	  case CU_STAT0:
	  case CU_STAT1:
	  case CU_STAT3:
		VERBOSE0(uni, UNI_FAC_COORD,
		    "signal saal_release.confirm in CU%u", uni->custat);
		break;

	  case CU_STAT2:
		/*
		 * Q.2931:Coord-U 5/10
		 */
		uni->funcs->uni_output(uni, uni->arg,
		    UNIAPI_LINK_RELEASE_confirm, 0, NULL);
		set_custat(uni, CU_STAT0);
		break;

	  default:
		ASSERT(0, ("CU_STAT*"));
	}
}

/*
 * SAAL failure.
 */
static void
coord_saal_release_indication(struct uni *uni)
{
	switch (uni->custat) {

	  case CU_STAT0:
	  case CU_STAT2:
		VERBOSE0(uni, UNI_FAC_COORD,
		    "signal saal_release.indication in CU%u", uni->custat);
		break;

	  case CU_STAT1:
	  case CU_STAT3:
		/*
		 * Q.2931:Coord-U 4/10
		 * Q.2931:Coord-U 5/10
		 */
		sig_all_calls(uni, SIGC_LINK_RELEASE_indication);
		set_custat(uni, CU_STAT0);
		break;

	  default:
		ASSERT(0, ("CU_STAT*"));
	}
}

/*
 * Link-establish.request from USER. This can also come from
 * a call instance. In this case 'cookie' is zero.
 */
static void
coord_link_establish_request(struct uni *uni, uint32_t cookie)
{
	switch (uni->custat) {

	  case CU_STAT0:
		/*
		 * Q.2931:Coord-U 4/10
		 */
		uni->funcs->saal_output(uni, uni->arg,
		    SAAL_ESTABLISH_request, NULL);
		if (!TIMER_ISACT(uni, t309))
			TIMER_START_UNI(uni, t309, uni->timer309);
		set_custat(uni, CU_STAT1);
		if (cookie)
			uniapi_uni_error(uni, UNIAPI_OK, cookie, 0);
		break;

	  case CU_STAT1:
		/*
		 * Q.2931:Coord-U 4/10
		 * This is probably missing from the delay field.
		 */
		uni_delenq_coord(uni, SIGO_LINK_ESTABLISH_request,
		    cookie, NULL);
		break;

	  case CU_STAT2:
		uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALLSTATE, cookie, 0);
		if (cookie == 0)
			VERBOSE0(uni, UNI_FAC_COORD,
			    "signal link-establish.request in CU%u",
			    uni->custat);
		break;

	  case CU_STAT3:
		/*
		 * Q.2931:Coord-U 5/10
		 */
		uni->funcs->uni_output(uni, uni->arg,
		    UNIAPI_LINK_ESTABLISH_confirm, 0, NULL);
		uniapi_uni_error(uni, UNIAPI_OK, cookie, 0);
		break;

	  default:
		ASSERT(0, ("CU_STAT*"));
	}
}

/*
 * Link-release.request from user
 */
static void
coord_link_release_request(struct uni *uni, u_int cookie)
{
	switch (uni->custat) {

	  case CU_STAT0:
	  case CU_STAT1:
	  case CU_STAT2:
		uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALLSTATE, cookie, 0);
		break;

	  case CU_STAT3:
		/*
		 * Q.2931:Coord-U 5/10
		 */
		uni->funcs->saal_output(uni, uni->arg,
		    SAAL_RELEASE_request, NULL);
		set_custat(uni, CU_STAT2);
		uniapi_uni_error(uni, UNIAPI_OK, cookie, 0);
		break;

	  default:
		ASSERT(0, ("CU_STAT*"));
	}
}

/*
 * T309 timeout signal
 */
static void
coord_t309(struct uni *uni)
{
	switch (uni->custat) {

	  case CU_STAT0:
	  case CU_STAT1:
		/*
		 * Q.2931:Coord-U 4/10
		 */
		sig_all_calls(uni, SIGC_LINK_ESTABLISH_ERROR_indication);
		set_custat(uni, CU_STAT0);
		/* this is not in the SDLs, but how will the call control
		 * know, that starting the LINK has failed otherwise? */
		uni->funcs->uni_output(uni, uni->arg,
		    UNIAPI_LINK_RELEASE_confirm, 0, NULL);
		break;

	  case CU_STAT2:
	  case CU_STAT3:
		VERBOSE0(uni, UNI_FAC_COORD,
		    "signal T309 in CU%u", uni->custat);
		break;

	  default:
		ASSERT(0, ("CU_STAT*"));
	}
}

/*
 * Message from SAAL
 */
static void
coord_saal_data_indication(struct uni *uni, struct uni_msg *m)
{
	struct uni_all *u;
	struct call *c;

	memset(&uni->cause, 0, sizeof(uni->cause));
	if ((u = UNI_ALLOC()) == NULL) {
		uni_msg_destroy(m);
		return;
	}
	if (uni_decode_head(m, u, &uni->cx)) {
		VERBOSE(uni, UNI_FAC_COORD, 2, "bogus message - ignored");
		uni_msg_destroy(m);
		UNI_FREE(u);
		return;
	}
	if (u->u.hdr.cref.cref == CREF_DUMMY) {
		if (uni->cx.q2932) {
			input_dummy(uni, m, u);
		} else {
			VERBOSE(uni, UNI_FAC_COORD, 2, "dummy cref - ignored");
			UNI_FREE(u);
			uni_msg_destroy(m);
		}
		return;
	}

	if (u->u.hdr.cref.cref == CREF_GLOBAL)
		input_global(uni, m, u);
	else if ((c = uni_find_call(uni, &u->u.hdr.cref)) == NULL)
		input_unknown(uni, m, u);
	else if (c->type == CALL_COBI)
		input_cobi(c, m, u);
	else
		input_call(c, m, u);
}

/*
 * Message with global call reference
 *
 * Q.2931:Coord-U (X) 7/10
 */
static void
input_global(struct uni *uni, struct uni_msg *m, struct uni_all *u)
{
	VERBOSE(uni, UNI_FAC_COORD, 2, "GLOB MTYPE = %x", u->mtype);

	switch (u->mtype) {

	  default:
		/*
		 * Q.2931:Coord-U 7/10
		 * Q.2931: 5.6.3.2e
		 * Amd4:   29e
		 */
		uni_respond_status(uni, &u->u.hdr.cref,
		    u->u.hdr.cref.flag ? uni->glob_start : uni->glob_respond,
		    UNI_CAUSE_CREF_INV);
		break;

	  case UNI_RESTART:
		if (u->u.hdr.cref.flag) {
			/*
			 * Q.2931:Coord-U 7/10 (5.6.3.2h)
			 */
			uni_respond_status(uni, &u->u.hdr.cref,
			    uni->glob_start, UNI_CAUSE_CREF_INV);
			break;
		}
		uni_enq_resp(uni, SIGR_RESTART, 0, m, u);
		return;

	  case UNI_RESTART_ACK:
		if (!u->u.hdr.cref.flag) {
			/*
			 * Q.2931:Coord-U 7/10 (5.6.3.2h)
			 * Note, that the SDL diagram contains an error.
			 * The error with the 'YES' label should go to the
			 * box below 'OTHER'.
			 */
			uni_respond_status(uni, &u->u.hdr.cref,
			    uni->glob_respond, UNI_CAUSE_CREF_INV);
			break;
		}
		uni_enq_start(uni, SIGS_RESTART_ACK, 0, m, u);
		return;

	  case UNI_STATUS:
		if (u->u.hdr.cref.flag)
			uni_enq_start(uni, SIGS_STATUS, 0, m, u);
		else
			uni_enq_resp(uni, SIGR_STATUS, 0, m, u);
		return;
	}
	uni_msg_destroy(m);
	UNI_FREE(u);
}

/*
 * Q.2931:Coord-U 8/10
 *
 * Message for an unknown call reference
 */
static void
input_unknown(struct uni *uni, struct uni_msg *m, struct uni_all *u)
{
	struct uni_all *resp;
	struct call *c;
	u_int cause = UNI_CAUSE_CREF_INV;

	VERBOSE(uni, UNI_FAC_COORD, 2, "UNKNOWN MTYPE = %x", u->mtype);

	switch (u->mtype) {

	  default:
		/*
		 * This message type is entirly unknown
		 *
		 * 5.6.4 and 5.7.1 are only when the call is not in the
		 * NULL state. This means, 5.6.3.2a takes over.
		 */
		break;

	  case UNI_SETUP:
		if (u->u.hdr.cref.flag)
			/*
			 * 5.6.3.2c
			 */
			goto drop;
		if ((c = uni_create_call(uni, u->u.hdr.cref.cref, 0, 0)) != NULL) {
			uni_enq_call(c, SIGC_SETUP, 0, m, u);
			return;
		}
		goto drop;

	  case UNI_RELEASE_COMPL:
		/*
		 * 5.6.3.2c
		 */
		goto drop;

	  case UNI_STATUS:
		/*
		 * 5.6.12
		 *
		 * The SDLs don't use the verify procedure and don't
		 * handle the case of an invalid callstate - we
		 * ignore the message, if the callstate is not good.
		 */
		(void)uni_decode_body(m, u, &uni->cx);
		if (!IE_ISGOOD(u->u.status.callstate))
			goto drop;
		if (u->u.status.callstate.state == UNI_CALLSTATE_U0)
			goto drop;
		cause = UNI_CAUSE_MSG_INCOMP;
		break;

	  case UNI_STATUS_ENQ:
		if ((resp = UNI_ALLOC()) == NULL)
			goto drop;

		(void)uni_decode_body(m, u, &uni->cx);
		MK_MSG_RESP(resp, UNI_STATUS, &u->u.hdr.cref);
		MK_IE_CALLSTATE(resp->u.status.callstate, UNI_CALLSTATE_U0);
		MK_IE_CAUSE(resp->u.status.cause, UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_STATUS);

		if (IE_ISGOOD(u->u.status_enq.epref)) {
			/* reflect epref as required by L3MU_PO */
			resp->u.status.epref = u->u.status_enq.epref;
			MK_IE_EPREF(resp->u.status.epref,
			    u->u.status_enq.epref.epref,
			    !u->u.status_enq.epref.flag);
			MK_IE_EPSTATE(resp->u.status.epstate, UNI_EPSTATE_NULL);
		}

		(void)uni_send_output(resp, uni);

		UNI_FREE(resp);
		goto drop;

	  case UNI_COBISETUP:
		if (u->u.hdr.cref.flag)
			/*
			 * 5.6.3.2c (probably)
			 */
			goto drop;
		if ((c = uni_create_call(uni, u->u.hdr.cref.cref, 0, 0)) != NULL) {
			uni_enq_call(c, SIGC_COBISETUP, 0, m, u);
			return;
		}
		goto drop;
	}

	/*
	 * 5.6.3.2a)
	 *
	 * Respond with a RELEASE COMPLETE
	 */
	if ((resp = UNI_ALLOC()) == NULL)
		goto drop;

	MK_MSG_RESP(resp, UNI_RELEASE_COMPL, &u->u.hdr.cref);
	MK_IE_CAUSE(resp->u.release_compl.cause[0], UNI_CAUSE_LOC_USER, cause);
	if (uni_diag(cause, UNI_CODING_ITU) == UNI_DIAG_MTYPE)
		ADD_CAUSE_MTYPE(resp->u.release_compl.cause[0], u->mtype);

	(void)uni_send_output(resp, uni);

	UNI_FREE(resp);

  drop:
	UNI_FREE(u);
	uni_msg_destroy(m);
}

static void
input_cobi(struct call *c __unused, struct uni_msg *m, struct uni_all *u)
{
	/* XXX */
	UNI_FREE(u);
	uni_msg_destroy(m);
}

static void
input_dummy(struct uni *uni __unused, struct uni_msg *m, struct uni_all *u)
{
	/* XXX */
	UNI_FREE(u);
	uni_msg_destroy(m);
}

static void
input_call(struct call *c, struct uni_msg *m, struct uni_all *u)
{
	VERBOSE(c->uni, UNI_FAC_COORD, 2, "CALL MTYPE = %x %d/%s", 
		u->mtype, c->cref, c->mine ? "mine":"his");

	switch (u->mtype) {

	  case UNI_SETUP:
		/*
		 * Ignored
		 */
		break;

	  case UNI_CALL_PROC:
		uni_enq_call(c, SIGC_CALL_PROC, 0, m, u);
		return;

	  case UNI_ALERTING:
		uni_enq_call(c, SIGC_ALERTING, 0, m, u);
		return;

	  case UNI_RELEASE:
		uni_enq_call(c, SIGC_RELEASE, 0, m, u);
		return;

	  case UNI_RELEASE_COMPL:
		uni_enq_call(c, SIGC_RELEASE_COMPL, 0, m, u);
		return;

	  case UNI_CONNECT:
		uni_enq_call(c, SIGC_CONNECT, 0, m, u);
		return;

	  case UNI_CONNECT_ACK:
		uni_enq_call(c, SIGC_CONNECT_ACK, 0, m, u);
		return;

	  case UNI_NOTIFY:
		uni_enq_call(c, SIGC_NOTIFY, 0, m, u);
		return;

	  case UNI_STATUS:
		uni_enq_call(c, SIGC_STATUS, 0, m, u);
		return;

	  case UNI_STATUS_ENQ:
		uni_enq_call(c, SIGC_STATUS_ENQ, 0, m, u);
		return;

	  case UNI_ADD_PARTY:
		uni_enq_call(c, SIGC_ADD_PARTY, 0, m, u);
		return;

	  case UNI_PARTY_ALERTING:
		uni_enq_call(c, SIGC_PARTY_ALERTING, 0, m, u);
		return;

	  case UNI_ADD_PARTY_ACK:
		uni_enq_call(c, SIGC_ADD_PARTY_ACK, 0, m, u);
		return;

	  case UNI_ADD_PARTY_REJ:
		uni_enq_call(c, SIGC_ADD_PARTY_REJ, 0, m, u);
		return;

	  case UNI_DROP_PARTY:
		uni_enq_call(c, SIGC_DROP_PARTY, 0, m, u);
		return;

	  case UNI_DROP_PARTY_ACK:
		uni_enq_call(c, SIGC_DROP_PARTY_ACK, 0, m, u);
		return;

	  default:
		uni_enq_call(c, SIGC_UNKNOWN, 0, m, u);
		return;
	}
	UNI_FREE(u);
	uni_msg_destroy(m);
}


/*
 * This macro tries to implement the delaying behaviour for
 * message from the API when we are in the Awaiting-Establish state.
 * In this state, the message is delayed. If we drop back to CU 0,
 * everything gets unqueued and errors are returned for all that stuff.
 * If we progess to CUSTAT2 we process the requests.
 */
#define COMMON_DELAY(SIG, COOKIE)					\
		if (uni->custat == CU_STAT0 || uni->custat == CU_STAT2) {\
			uniapi_uni_error(uni, UNIAPI_ERROR_BADCU,	\
			    COOKIE, 0);					\
			break;						\
		}							\
		if (uni->custat == CU_STAT1) {				\
			uni_delenq_coord(uni, SIG, COOKIE, msg);	\
			break;						\
		}

/*
 * Signal handler of the coordinator
 */
void
uni_sig_coord(struct uni *uni, enum coord_sig sig, uint32_t cookie,
    struct uni_msg *msg)
{
	struct call *c;

	if (sig >= SIGO_END) {
		VERBOSE(uni, UNI_FAC_ERR, 1, "Signal %d outside of range to "
		    "Coord", sig);
		if (msg)
			uni_msg_destroy(msg);
		return;
	}

	VERBOSE(uni, UNI_FAC_COORD, 1, "Signal %s in state %s",
	    coord_sigs[sig], cunames[uni->custat]);

	switch (sig) {

	  case SIGO_END:
		break;

	  case SIGO_DATA:	/* delayed output */
		if (uni->custat == CU_STAT0 || uni->custat == CU_STAT1)
			break;	/* drop */
		if (uni->custat == CU_STAT1)
			uni_delenq_coord(uni, SIGO_DATA, cookie, msg);/* ??? */
		else
			uni->funcs->saal_output(uni, uni->arg,
			    SAAL_DATA_request, msg);
		msg = NULL;
		break;

	  /*
	   * SAAL signals
	   */
	  case SIGO_SAAL_ESTABLISH_indication:
		coord_saal_establish_indication(uni);
		break;

	  case SIGO_SAAL_ESTABLISH_confirm:
		coord_saal_establish_confirm(uni);
		break;

	  case SIGO_SAAL_RELEASE_confirm:
		coord_saal_release_confirm(uni);
		break;

	  case SIGO_SAAL_RELEASE_indication:
		coord_saal_release_indication(uni);
		break;

	  case SIGO_SAAL_DATA_indication:
		coord_saal_data_indication(uni, msg);
		msg = NULL;
		break;

	  case SIGO_SAAL_UDATA_indication:
		VERBOSE0(uni, UNI_FAC_ERR, "SAAL_UDATA_indication");
		break;

	  /*
	   * Signals from USER
	   */
	  case SIGO_LINK_ESTABLISH_request:
		coord_link_establish_request(uni, cookie);
		break;

	  case SIGO_LINK_RELEASE_request:
		coord_link_release_request(uni, cookie);
		break;

	  case SIGO_RESET_request:
		uni_enq_start(uni, SIGS_RESET_request, cookie, msg, NULL);
		msg = NULL;
		if (uni->custat == CU_STAT0) {
			uni->funcs->saal_output(uni, uni->arg,
			    SAAL_ESTABLISH_request, NULL);
			if (!TIMER_ISACT(uni, t309))
				TIMER_START_UNI(uni, t309, uni->timer309);
			set_custat(uni, CU_STAT1);
		}
		break;

	  case SIGO_RESET_ERROR_response:
		COMMON_DELAY(SIGO_RESET_ERROR_response, cookie);
		uni_enq_resp(uni, SIGR_RESET_ERROR_response, cookie, msg, NULL);
		msg = NULL;
		break;

	  case SIGO_RESET_response:
		COMMON_DELAY(SIGO_RESET_response, cookie);
		uni_enq_resp(uni, SIGR_RESET_response, cookie, msg, NULL);
		msg = NULL;
		break;

	  case SIGO_SETUP_request:
		if ((c = uni_create_new_call(uni, cookie)) != NULL) {
			uni_enq_call(c, SIGC_SETUP_request, cookie, msg, NULL);
			msg = NULL;
			if (uni->custat == CU_STAT0) {
				uni->funcs->saal_output(uni, uni->arg,
				    SAAL_ESTABLISH_request, NULL);
				if (!TIMER_ISACT(uni, t309))
					TIMER_START_UNI(uni, t309, uni->timer309);
				set_custat(uni, CU_STAT1);
			}
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_NOMEM, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;

	  case SIGO_PROCEEDING_request:
	    {
		struct uniapi_proceeding_request *arg =
		    uni_msg_rptr(msg, struct uniapi_proceeding_request *);

		COMMON_DELAY(SIGO_PROCEEDING_request, cookie);
		if ((c = uni_find_call(uni, &arg->call_proc.hdr.cref)) != NULL) {
			uni_enq_call(c, SIGC_PROCEEDING_request, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_ALERTING_request:
	    {
		struct uniapi_alerting_request *arg =
		    uni_msg_rptr(msg, struct uniapi_alerting_request *);

		COMMON_DELAY(SIGO_ALERTING_request, cookie);
		if ((c = uni_find_call(uni, &arg->alerting.hdr.cref)) != NULL) {
			uni_enq_call(c, SIGC_ALERTING_request, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_SETUP_response:
	    {
		struct uniapi_setup_response *arg =
		    uni_msg_rptr(msg, struct uniapi_setup_response *);

		COMMON_DELAY(SIGO_SETUP_response, cookie);
		if ((c = uni_find_call(uni, &arg->connect.hdr.cref)) != NULL) {
			uni_enq_call(c, SIGC_SETUP_response, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_SETUP_COMPLETE_request:
	    {
		struct uniapi_setup_complete_request *arg =
		    uni_msg_rptr(msg, struct uniapi_setup_complete_request *);

		COMMON_DELAY(SIGO_SETUP_COMPLETE_request, cookie);
		if ((c = uni_find_call(uni, &arg->connect_ack.hdr.cref)) != NULL) {
			uni_enq_call(c, SIGC_SETUP_COMPLETE_request,
			    cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_RELEASE_request:
	    {
		struct uniapi_release_request *arg =
		    uni_msg_rptr(msg, struct uniapi_release_request *);

		COMMON_DELAY(SIGO_RELEASE_request, cookie);
		if ((c = uni_find_call(uni, &arg->release.hdr.cref)) != NULL) {
			uni_enq_call(c, SIGC_RELEASE_request, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_RELEASE_response:
	    {
		struct uniapi_release_response *arg =
		    uni_msg_rptr(msg, struct uniapi_release_response *);

		COMMON_DELAY(SIGO_RELEASE_response, cookie);
		if ((c = uni_find_call(uni, &arg->release_compl.hdr.cref)) != NULL) {
			uni_enq_call(c, SIGC_RELEASE_response, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_NOTIFY_request:
	    {
		struct uniapi_notify_request *arg =
		    uni_msg_rptr(msg, struct uniapi_notify_request *);

		COMMON_DELAY(SIGO_NOTIFY_request, cookie);
		if ((c = uni_find_call(uni, &arg->notify.hdr.cref)) != NULL) {
			uni_enq_call(c, SIGC_NOTIFY_request, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_STATUS_ENQUIRY_request:
	    {
		struct uniapi_status_enquiry_request *arg =
		    uni_msg_rptr(msg, struct uniapi_status_enquiry_request *);

		COMMON_DELAY(SIGO_STATUS_ENQUIRY_request, cookie);
		if ((c = uni_find_call(uni, &arg->cref)) != NULL) {
			uni_enq_call(c, SIGC_STATUS_ENQUIRY_request, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_ADD_PARTY_request:
	    {
		struct uniapi_add_party_request *arg =
		    uni_msg_rptr(msg, struct uniapi_add_party_request *);

		COMMON_DELAY(SIGO_ADD_PARTY_request, cookie);
		if ((c = uni_find_call(uni, &arg->add.hdr.cref)) != NULL) {
			if (c->type != CALL_ROOT) {
				uniapi_call_error(c, UNIAPI_ERROR_BAD_CTYPE,
				    cookie);
				break;
			}
			uni_enq_call(c, SIGC_ADD_PARTY_request, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_PARTY_ALERTING_request:
	    {
		struct uniapi_party_alerting_request *arg =
		    uni_msg_rptr(msg, struct uniapi_party_alerting_request *);

		COMMON_DELAY(SIGO_PARTY_ALERTING_request, cookie);
		if ((c = uni_find_call(uni, &arg->alert.hdr.cref)) != NULL) {
			if (c->type != CALL_LEAF) {
				uniapi_call_error(c, UNIAPI_ERROR_BAD_CTYPE,
				    cookie);
				break;
			}
			uni_enq_call(c, SIGC_PARTY_ALERTING_request, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_ADD_PARTY_ACK_request:
	    {
		struct uniapi_add_party_ack_request *arg =
		    uni_msg_rptr(msg, struct uniapi_add_party_ack_request *);

		COMMON_DELAY(SIGO_ADD_PARTY_ACK_request, cookie);
		if ((c = uni_find_call(uni, &arg->ack.hdr.cref)) != NULL) {
			if (c->type != CALL_LEAF) {
				uniapi_call_error(c, UNIAPI_ERROR_BAD_CTYPE,
				    cookie);
				break;
			}
			uni_enq_call(c, SIGC_ADD_PARTY_ACK_request, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_ADD_PARTY_REJ_request:
	    {
		struct uniapi_add_party_rej_request *arg =
		    uni_msg_rptr(msg, struct uniapi_add_party_rej_request *);

		COMMON_DELAY(SIGO_ADD_PARTY_REJ_request, cookie);
		if ((c = uni_find_call(uni, &arg->rej.hdr.cref)) != NULL) {
			if (c->type != CALL_LEAF) {
				uniapi_call_error(c, UNIAPI_ERROR_BAD_CTYPE,
				    cookie);
				break;
			}
			uni_enq_call(c, SIGC_ADD_PARTY_REJ_request, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_DROP_PARTY_request:
	    {
		struct uniapi_drop_party_request *arg =
		    uni_msg_rptr(msg, struct uniapi_drop_party_request *);

		COMMON_DELAY(SIGO_DROP_PARTY_request, cookie);
		if ((c = uni_find_call(uni, &arg->drop.hdr.cref)) != NULL) {
			if (c->type != CALL_ROOT && c->type != CALL_LEAF) {
				uniapi_call_error(c, UNIAPI_ERROR_BAD_CTYPE,
				    cookie);
				break;
			}
			uni_enq_call(c, SIGC_DROP_PARTY_request, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_DROP_PARTY_ACK_request:
	    {
		struct uniapi_drop_party_ack_request *arg =
		    uni_msg_rptr(msg, struct uniapi_drop_party_ack_request *);

		COMMON_DELAY(SIGO_DROP_PARTY_ACK_request, cookie);
		if ((c = uni_find_call(uni, &arg->ack.hdr.cref)) != NULL) {
			if (c->type != CALL_ROOT && c->type != CALL_LEAF) {
				uniapi_call_error(c, UNIAPI_ERROR_BAD_CTYPE,
				    cookie);
				break;
			}
			uni_enq_call(c, SIGC_DROP_PARTY_ACK_request, cookie, msg, NULL);
			msg = NULL;
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  case SIGO_ABORT_CALL_request:
	    {
		struct uniapi_abort_call_request *arg = 
		    uni_msg_rptr(msg, struct uniapi_abort_call_request *);

		if ((c = uni_find_call(uni, &arg->cref)) != NULL) {
			uni_enq_call(c, SIGC_ABORT_CALL_request, cookie, NULL, NULL);
		} else {
			uniapi_uni_error(uni, UNIAPI_ERROR_BAD_CALL, cookie,
			    UNI_CALLSTATE_U0);
		}
		break;
	    }

	  /*
	   * Call-Control
	   */
	  case SIGO_CALL_DESTROYED:
		uni->funcs->uni_output(uni, uni->arg,
		    UNIAPI_CALL_DESTROYED, 0, msg);
		msg = NULL;
		break;

	  /*
	   * ResetRespond
	   */
	  case SIGO_RESET_indication:
		uni->funcs->uni_output(uni, uni->arg,
		    UNIAPI_RESET_indication, 0, msg);
		msg = NULL;
		break;

	  /*
	   * Timeouts
	   */
	  case SIGO_T309:
		coord_t309(uni);
		break;

	}
	if (msg != NULL)
		uni_msg_destroy(msg);
}

/*
 * Send a signal to all call instances
 */
static void
sig_all_calls(struct uni *uni, u_int sig)
{
	struct call *call;

	TAILQ_FOREACH(call, &uni->calls, link)
		uni_enq_call(call, sig, 0, NULL, NULL);
}

/*
 * Set a new coordinator state - this moves all delayed coordinator
 * signals from the delayed queue to the signal queue.
 */
static int
cufilt(struct sig *s, void *arg __unused)
{
	return (s->type == SIG_COORD);
}

static void
set_custat(struct uni *uni, enum cu_stat nstate)
{
	if (uni->custat != nstate) {
		uni->custat = nstate;
		uni_undel(uni, cufilt, NULL);
	}
}

/*
 * T309 timeout function
 */
static void
t309_func(struct uni *uni)
{
	uni_enq_coord(uni, SIGO_T309, 0, NULL);
}

/*
 * Respond with a status message
 */
void
uni_respond_status(struct uni *uni, struct uni_cref *cref,
    enum uni_callstate cs, enum uni_cause c1)
{
	struct uni_all *resp;

	if ((resp = UNI_ALLOC()) == NULL)
		return;

	MK_MSG_RESP(resp, UNI_STATUS, cref);
	MK_IE_CALLSTATE(resp->u.status.callstate, cs);
	MK_IE_CAUSE(resp->u.status.cause, UNI_CAUSE_LOC_USER, c1);

	(void)uni_send_output(resp, uni);

	UNI_FREE(resp);
}

/*
 * Respond with a status message
 */
void
uni_respond_status_mtype(struct uni *uni, struct uni_cref *cref,
    enum uni_callstate cs, enum uni_cause c1, u_int mtype)
{
	struct uni_all *resp;

	if((resp = UNI_ALLOC()) == NULL)
		return;

	MK_MSG_RESP(resp, UNI_STATUS, cref);
	MK_IE_CALLSTATE(resp->u.status.callstate, cs);
	MK_IE_CAUSE(resp->u.status.cause, UNI_CAUSE_LOC_USER, c1);
	ADD_CAUSE_MTYPE(resp->u.status.cause, mtype);

	(void)uni_send_output(resp, uni);

	UNI_FREE(resp);
}

/*
 * Send a message. If we are in CUSTAT1, delay the message if we
 * are in CUSTAT3 send it, else drop it.
 */
int
uni_send_output(struct uni_all *u, struct uni *uni)
{
	struct uni_msg *m;
	int err;

	if (uni->custat == CU_STAT0 || uni->custat == CU_STAT2)
		return (0);

	m = uni_msg_alloc(1024);
	if ((err = uni_encode(m, u, &uni->cx)) != 0) {
		VERBOSE0(uni, UNI_FAC_ERR, "uni_encode failed: %08x", err);
		uni_msg_destroy(m);
		return (-1);
	}
	if (uni->custat == CU_STAT1)
		uni_delenq_coord(uni, SIGO_DATA, 0, m);
	else
		uni->funcs->saal_output(uni, uni->arg, SAAL_DATA_request, m);
	return (0);
}
