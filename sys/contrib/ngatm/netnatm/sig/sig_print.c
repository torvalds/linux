/*
 * Copyright (c) 2002-2003
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
 *         Kendy Kutzner <kutzner@fokus.fraunhofer.de>
 *
 * $Begemot: libunimsg/netnatm/sig/sig_print.c,v 1.6 2004/08/05 07:11:02 brandt Exp $
 */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/libkern.h>
#include <machine/stdarg.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#endif

#include <netnatm/saal/sscfu.h>
#include <netnatm/msg/uni_hdr.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/msg/uniprint.h>
#include <netnatm/sig/uni.h>
#include <netnatm/sig/unisig.h>
#include <netnatm/sig/unidef.h>

const char * 
uni_strerr(u_int err)
{
	static const char *const errstr[] = {
#define DEF(NAME, VAL, STR) [UNIAPI_##NAME] = STR,
UNIAPI_DEF_ERRORS(DEF)
#undef DEF
	};
	static char buf[100];

	if (err >= sizeof(errstr)/sizeof(errstr[0]) || errstr[err] == NULL) {
		sprintf(buf, "Unknown error %u", err);
		return (buf);
	}
	return (errstr[err]);
}

#define D(M) [M] = #M
static const char *const msgs[] = {
	D(UNIAPI_ERROR),
	D(UNIAPI_CALL_CREATED),
	D(UNIAPI_CALL_DESTROYED),
	D(UNIAPI_PARTY_CREATED),
	D(UNIAPI_PARTY_DESTROYED),
	D(UNIAPI_LINK_ESTABLISH_request),
	D(UNIAPI_LINK_ESTABLISH_confirm),
	D(UNIAPI_LINK_RELEASE_request),
	D(UNIAPI_LINK_RELEASE_confirm),
	D(UNIAPI_RESET_request),
	D(UNIAPI_RESET_confirm),
	D(UNIAPI_RESET_indication),
	D(UNIAPI_RESET_ERROR_indication),
	D(UNIAPI_RESET_response),
	D(UNIAPI_RESET_ERROR_response),
	D(UNIAPI_RESET_STATUS_indication),
	D(UNIAPI_SETUP_request),
	D(UNIAPI_SETUP_indication),
	D(UNIAPI_SETUP_response),
	D(UNIAPI_SETUP_confirm),
	D(UNIAPI_SETUP_COMPLETE_indication),
	D(UNIAPI_SETUP_COMPLETE_request),
	D(UNIAPI_ALERTING_request),
	D(UNIAPI_ALERTING_indication),
	D(UNIAPI_PROCEEDING_request),
	D(UNIAPI_PROCEEDING_indication),
	D(UNIAPI_RELEASE_request),
	D(UNIAPI_RELEASE_indication),
	D(UNIAPI_RELEASE_response),
	D(UNIAPI_RELEASE_confirm),
	D(UNIAPI_NOTIFY_request),
	D(UNIAPI_NOTIFY_indication),
	D(UNIAPI_STATUS_indication),
	D(UNIAPI_STATUS_ENQUIRY_request),
	D(UNIAPI_ADD_PARTY_request),
	D(UNIAPI_ADD_PARTY_indication),
	D(UNIAPI_PARTY_ALERTING_request),
	D(UNIAPI_PARTY_ALERTING_indication),
	D(UNIAPI_ADD_PARTY_ACK_request),
	D(UNIAPI_ADD_PARTY_ACK_indication),
	D(UNIAPI_ADD_PARTY_REJ_request),
	D(UNIAPI_ADD_PARTY_REJ_indication),
	D(UNIAPI_DROP_PARTY_request),
	D(UNIAPI_DROP_PARTY_indication),
	D(UNIAPI_DROP_PARTY_ACK_request),
	D(UNIAPI_DROP_PARTY_ACK_indication),
	D(UNIAPI_ABORT_CALL_request),
};
#undef D

void
uni_print_api(char *buf, size_t bufsiz, u_int type, u_int cookie,
    const void *msg, struct unicx *cx)
{
	int old_dont_init = cx->dont_init;

	uni_print_init(buf, bufsiz, cx);
	cx->dont_init = 1;

	if (type >= sizeof(msgs) / sizeof(msgs[0]) || msgs[type] == NULL) {
		uni_print_flag("UNIAPI_UNKNOWN", cx);
		uni_print_entry(cx, "sig", "%u", type);
		uni_print_entry(cx, "cookie", "%u", cookie);
		goto out;
	}

	uni_print_flag(msgs[type], cx);
	uni_print_entry(cx, "cookie", "%u", cookie);
	cx->indent++;

	switch (type) {

	  case UNIAPI_ERROR:
	    {
		const struct uniapi_error *api = msg;

		uni_print_eol(cx);
		uni_print_entry(cx, "reason", "%s", uni_strerr(api->reason));
		uni_print_entry(cx, "state", "U%u", api->state);
		break;
	    }

	  case UNIAPI_CALL_CREATED:
	    {
		const struct uniapi_call_created *api = msg;

		uni_print_cref(NULL, 0, &api->cref, cx);
		break;
	    }

	  case UNIAPI_CALL_DESTROYED:
	    {
		const struct uniapi_call_destroyed *api = msg;

		uni_print_cref(NULL, 0, &api->cref, cx);
		break;
	    }

	  case UNIAPI_PARTY_CREATED:
	    {
		const struct uniapi_party_created *api = msg;

		uni_print_cref(NULL, 0, &api->cref, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_EPREF,
		    (const union uni_ieall *)&api->epref, cx);
		break;
	    }

	  case UNIAPI_PARTY_DESTROYED:
	    {
		const struct uniapi_party_destroyed *api = msg;

		uni_print_cref(NULL, 0, &api->cref, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_EPREF,
		    (const union uni_ieall *)&api->epref, cx);
		break;
	    }

	  case UNIAPI_LINK_ESTABLISH_request:
	  case UNIAPI_LINK_ESTABLISH_confirm:
	  case UNIAPI_LINK_RELEASE_request:
	  case UNIAPI_LINK_RELEASE_confirm:
		break;

	  case UNIAPI_RESET_request:
	    {
		const struct uniapi_reset_request *api = msg;

		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_RESTART,
		    (const union uni_ieall *)&api->restart, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_CONNID,
		    (const union uni_ieall *)&api->restart, cx);
		break;
	    }

	  case UNIAPI_RESET_confirm:
	    {
		const struct uniapi_reset_confirm *api = msg;

		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_RESTART,
		    (const union uni_ieall *)&api->restart, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_CONNID,
		    (const union uni_ieall *)&api->restart, cx);
		break;
	    }

	  case UNIAPI_RESET_indication:
	    {
		const struct uniapi_reset_indication *api = msg;

		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_RESTART,
		    (const union uni_ieall *)&api->restart, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_CONNID,
		    (const union uni_ieall *)&api->restart, cx);
		break;
	    }

	  case UNIAPI_RESET_ERROR_indication:
	    {
		const struct uniapi_reset_error_indication *api = msg;
		static const struct uni_print_tbl reason[] = {
#define DEF(NAME, VALUE, STR) { STR, VALUE },
			UNIAPI_DEF_RESET_ERRORS(DEF)
#undef DEF
			{ NULL, 0 }
		};
		static const struct uni_print_tbl source[] = {
			{ "start", 0 },
			{ "respond", 1 },
			{ NULL, 0 }
		};

		uni_print_eol(cx);
		uni_print_tbl("source", api->source, source, cx);
		uni_print_tbl("reason", api->reason, reason, cx);
		break;
	    }

	  case UNIAPI_RESET_response:
	    {
		const struct uniapi_reset_response *api = msg;

		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_RESTART,
		    (const union uni_ieall *)&api->restart, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_CONNID,
		    (const union uni_ieall *)&api->restart, cx);
		break;
	    }

	  case UNIAPI_RESET_ERROR_response:
	    {
		const struct uniapi_reset_error_response *api = msg;

		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_CAUSE,
		    (const union uni_ieall *)&api->cause, cx);
		break;
	    }

	  case UNIAPI_RESET_STATUS_indication:
	    {
		const struct uniapi_reset_status_indication *api = msg;

		uni_print_cref(NULL, 0, &api->cref, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_CALLSTATE,
		    (const union uni_ieall *)&api->callstate, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_CAUSE,
		    (const union uni_ieall *)&api->cause, cx);
		break;
	    }

	  case UNIAPI_SETUP_request:
	    {
		const struct uniapi_setup_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_SETUP,
		    (const union uni_msgall *)&api->setup, cx);
		break;
	    }

	  case UNIAPI_SETUP_indication:
	    {
		const struct uniapi_setup_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_SETUP,
		    (const union uni_msgall *)&api->setup, cx);
		break;
	    }

	  case UNIAPI_SETUP_response:
	    {
		const struct uniapi_setup_response *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_CONNECT,
		    (const union uni_msgall *)&api->connect, cx);
		break;
	    }

	  case UNIAPI_SETUP_confirm:
	    {
		const struct uniapi_setup_confirm *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_CONNECT,
		    (const union uni_msgall *)&api->connect, cx);
		break;
	    }

	  case UNIAPI_SETUP_COMPLETE_indication:
	    {
		const struct uniapi_setup_complete_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_CONNECT_ACK,
		    (const union uni_msgall *)&api->connect_ack, cx);
		break;
	    }

	  case UNIAPI_SETUP_COMPLETE_request:
	    {
		const struct uniapi_setup_complete_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_CONNECT_ACK,
		    (const union uni_msgall *)&api->connect_ack, cx);
		break;
	    }

	  case UNIAPI_ALERTING_request:
	    {
		const struct uniapi_alerting_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_ALERTING,
		    (const union uni_msgall *)&api->alerting, cx);
		break;
	    }

	  case UNIAPI_ALERTING_indication:
	    {
		const struct uniapi_alerting_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_ALERTING,
		    (const union uni_msgall *)&api->alerting, cx);
		break;
	    }

	  case UNIAPI_PROCEEDING_request:
	    {
		const struct uniapi_proceeding_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_CALL_PROC,
		    (const union uni_msgall *)&api->call_proc, cx);
		break;
	    }

	  case UNIAPI_PROCEEDING_indication:
	    {
		const struct uniapi_proceeding_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_CALL_PROC,
		    (const union uni_msgall *)&api->call_proc, cx);
		break;
	    }

	  case UNIAPI_RELEASE_request:
	    {
		const struct uniapi_release_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_RELEASE,
		    (const union uni_msgall *)&api->release, cx);
		break;
	    }

	  case UNIAPI_RELEASE_indication:
	    {
		const struct uniapi_release_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_RELEASE,
		    (const union uni_msgall *)&api->release, cx);
		break;
	    }

	  case UNIAPI_RELEASE_response:
	    {
		const struct uniapi_release_response *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_RELEASE_COMPL,
		    (const union uni_msgall *)&api->release_compl, cx);
		break;
	    }
	  case UNIAPI_RELEASE_confirm:
	    {
		const struct uniapi_release_confirm *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_RELEASE,
		    (const union uni_msgall *)&api->release, cx);
		break;
	    }

	  case UNIAPI_NOTIFY_request:
	    {
		const struct uniapi_notify_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_NOTIFY,
		    (const union uni_msgall *)&api->notify, cx);
		break;
	    }

	  case UNIAPI_NOTIFY_indication:
	    {
		const struct uniapi_notify_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_NOTIFY,
		    (const union uni_msgall *)&api->notify, cx);
		break;
	    }

	  case UNIAPI_STATUS_indication:
	    {
		const struct uniapi_status_indication *api = msg;

		uni_print_cref(NULL, 0, &api->cref, cx);
		uni_print_eol(cx);
		uni_print_entry(cx, "my_state", "U%u", api->my_state);
		uni_print_entry(cx, "my_cause", "%s",
		    uni_ie_cause2str(UNI_CODING_ITU, api->my_cause));
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_CALLSTATE,
		    (const union uni_ieall *)&api->his_state, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_CAUSE,
		    (const union uni_ieall *)&api->his_cause, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_EPREF,
		    (const union uni_ieall *)&api->epref, cx);
		break;
	    }

	  case UNIAPI_STATUS_ENQUIRY_request:
	    {
		const struct uniapi_status_enquiry_request *api = msg;

		uni_print_cref(NULL, 0, &api->cref, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_EPREF,
		    (const union uni_ieall *)&api->epref, cx);
		break;
	    }

	  case UNIAPI_ADD_PARTY_request:
	    {
		const struct uniapi_add_party_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_ADD_PARTY,
		    (const union uni_msgall *)&api->add, cx);
		break;
	    }

	  case UNIAPI_ADD_PARTY_indication:
	    {
		const struct uniapi_add_party_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_ADD_PARTY,
		    (const union uni_msgall *)&api->add, cx);
		break;
	    }

	  case UNIAPI_PARTY_ALERTING_request:
	    {
		const struct uniapi_party_alerting_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_PARTY_ALERTING,
		    (const union uni_msgall *)&api->alert, cx);
		break;
	    }

	  case UNIAPI_PARTY_ALERTING_indication:
	    {
		const struct uniapi_party_alerting_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_PARTY_ALERTING,
		    (const union uni_msgall *)&api->alert, cx);
		break;
	    }

	  case UNIAPI_ADD_PARTY_ACK_request:
	    {
		const struct uniapi_add_party_ack_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_ADD_PARTY_ACK,
		    (const union uni_msgall *)&api->ack, cx);
		break;
	    }

	  case UNIAPI_ADD_PARTY_ACK_indication:
	    {
		const struct uniapi_add_party_ack_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_ADD_PARTY_ACK,
		    (const union uni_msgall *)&api->ack, cx);
		break;
	    }

	  case UNIAPI_ADD_PARTY_REJ_request:
	    {
		const struct uniapi_add_party_rej_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_ADD_PARTY_REJ,
		    (const union uni_msgall *)&api->rej, cx);
		break;
	    }

	  case UNIAPI_ADD_PARTY_REJ_indication:
	    {
		const struct uniapi_add_party_rej_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_ADD_PARTY_REJ,
		    (const union uni_msgall *)&api->rej, cx);
		break;
	    }

	  case UNIAPI_DROP_PARTY_request:
	    {
		const struct uniapi_drop_party_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_DROP_PARTY,
		    (const union uni_msgall *)&api->drop, cx);
		break;
	    }

	  case UNIAPI_DROP_PARTY_indication:
	    {
		const struct uniapi_drop_party_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_DROP_PARTY,
		    (const union uni_msgall *)&api->drop, cx);
		break;
	    }

	  case UNIAPI_DROP_PARTY_ACK_request:
	    {
		const struct uniapi_drop_party_ack_request *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_DROP_PARTY_ACK,
		    (const union uni_msgall *)&api->ack, cx);
		break;
	    }

	  case UNIAPI_DROP_PARTY_ACK_indication:
	    {
		const struct uniapi_drop_party_ack_indication *api = msg;

		uni_print_eol(cx);
		uni_print_msg(NULL, 0, UNI_DROP_PARTY,
		    (const union uni_msgall *)&api->drop, cx);
		uni_print_eol(cx);
		uni_print_ie(NULL, 0, UNI_IE_CRANKBACK,
		    (const union uni_ieall *)&api->crankback, cx);
		break;
	    }

	  case UNIAPI_ABORT_CALL_request:
	    {
		const struct uniapi_abort_call_request *api = msg;

		uni_print_cref(NULL, 0, &api->cref, cx);
		break;
	    }
	}

  out:
	cx->dont_init = old_dont_init;
}
