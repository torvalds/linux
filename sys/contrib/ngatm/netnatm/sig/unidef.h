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
 * $Begemot: libunimsg/netnatm/sig/unidef.h,v 1.9 2004/07/08 08:22:24 brandt Exp $
 *
 * UNI public definitions.
 */
#ifndef _ATM_SIG_UNIDEF_H_
#define _ATM_SIG_UNIDEF_H_

#ifdef _KERNEL
#include <sys/stdint.h>
#else
#include <stdint.h>
#endif

/*
 * Debug facilities
 */
#define UNI_DEBUG_FACILITIES		\
	UNI_DEBUG_DEFINE(TIMEOUT)	\
	UNI_DEBUG_DEFINE(RESTART)	\
	UNI_DEBUG_DEFINE(SAAL)		\
	UNI_DEBUG_DEFINE(PARSE)		\
	UNI_DEBUG_DEFINE(CALL)		\
	UNI_DEBUG_DEFINE(WARN)		\
	UNI_DEBUG_DEFINE(COORD)		\
	UNI_DEBUG_DEFINE(API)		\
	UNI_DEBUG_DEFINE(MSG)		\
	UNI_DEBUG_DEFINE(ERR)		\
	UNI_DEBUG_DEFINE(VERIFY)	\

enum uni_verb {
#define UNI_DEBUG_DEFINE(D) UNI_FAC_##D,
	UNI_DEBUG_FACILITIES
#undef UNI_DEBUG_DEFINE

	UNI_MAXFACILITY,
};

/*
 * Default timer values and repeat counts
 */
#define UNI_T301_DEFAULT	180000
#define UNI_T303_DEFAULT	4000
#define UNI_T303_CNT_DEFAULT	2
#define UNI_T308_DEFAULT	30000
#define UNI_T308_CNT_DEFAULT	2
#define UNI_T309_DEFAULT	10000
#define UNI_T310U_DEFAULT	30000
#define UNI_T310N_DEFAULT	10000
#define UNI_T313_DEFAULT	4000
#define UNI_T316_DEFAULT	120000
#define UNI_T316_CNT_DEFAULT	2
#define UNI_T317_DEFAULT	90000
#define UNI_T322_DEFAULT	4000
#define UNI_T322_CNT_DEFAULT	2
#define UNI_T397_DEFAULT	UNI_T301_DEFAULT
#define UNI_T398_DEFAULT	4000
#define UNI_T399U_DEFAULT	(UNI_T303_DEFAULT + UNI_T310U_DEFAULT)
#define UNI_T399N_DEFAULT	(UNI_T303_DEFAULT + UNI_T310N_DEFAULT)

/*
 * Protocol support
 */
enum uni_proto {
	UNIPROTO_UNI40U,	/* UNI4.0 user side */
	UNIPROTO_UNI40N,	/* UNI4.0 network side */
	UNIPROTO_PNNI10,	/* PNNI1.0 */
};
enum uni_popt {
	UNIPROTO_GFP	= 0x0001,	/* enable GFP */
	UNIPROTO_SB_TB	= 0x0002,	/* Coincident Sb-Tb/Tb */

	UNIPROTO_ALLMASK = 0x0003,
};

/*
 * Other options
 */
enum uni_option {
	UNIOPT_GIT_HARD		= 0x0001,	/* harder check of GIT IE */
	UNIOPT_BEARER_HARD	= 0x0002,	/* harder check of BEARER IE */
	UNIOPT_CAUSE_HARD	= 0x0004,	/* harder check of CAUSE IE */

	UNIOPT_ALLMASK		= 0x0007,
};

/*
 * UNI configuration
 */
struct uni_config {
	uint32_t	proto;		/* which protocol */
	uint32_t	popt;		/* protocol option */
	uint32_t	option;		/* other options */
	uint32_t	timer301;	/* T301 */
	uint32_t	timer303;	/* T303 */
	uint32_t	init303;	/* T303 retransmission count */
	uint32_t	timer308;	/* T308 */
	uint32_t	init308;	/* T308 retransmission count */
	uint32_t	timer309;	/* T309 */
	uint32_t	timer310;	/* T310 */
	uint32_t	timer313;	/* T313 */
	uint32_t	timer316;	/* T316 */
	uint32_t	init316;	/* T316 retransmission count */
	uint32_t	timer317;	/* T317 */
	uint32_t	timer322;	/* T322 */
	uint32_t	init322;	/* T322 retransmission count */
	uint32_t	timer397;	/* T397 */
	uint32_t	timer398;	/* T398 */
	uint32_t	timer399;	/* T399 */
};
enum uni_config_mask {
	UNICFG_PROTO	= 0x00000001,
	UNICFG_TIMER301	= 0x00000002,
	UNICFG_TIMER303	= 0x00000004,
	UNICFG_INIT303	= 0x00000008,
	UNICFG_TIMER308	= 0x00000010,
	UNICFG_INIT308	= 0x00000020,
	UNICFG_TIMER309	= 0x00000040,
	UNICFG_TIMER310	= 0x00000080,
	UNICFG_TIMER313	= 0x00000100,
	UNICFG_TIMER316	= 0x00000200,
	UNICFG_INIT316	= 0x00000400,
	UNICFG_TIMER317	= 0x00000800,
	UNICFG_TIMER322	= 0x00001000,
	UNICFG_INIT322	= 0x00002000,
	UNICFG_TIMER397	= 0x00004000,
	UNICFG_TIMER398	= 0x00008000,
	UNICFG_TIMER399	= 0x00010000,

	UNICFG_ALLMASK	= 0x0001ffff,
};

/*
 * API signals
 */
enum uni_sig {
	UNIAPI_ERROR			= 0,	/* UNI -> API */

	UNIAPI_CALL_CREATED		= 1,	/* UNI -> API */
	UNIAPI_CALL_DESTROYED		= 2,	/* UNI -> API */
	UNIAPI_PARTY_CREATED		= 3,	/* UNI -> API */
	UNIAPI_PARTY_DESTROYED		= 4,	/* UNI -> API */

	UNIAPI_LINK_ESTABLISH_request	= 5,	/* API -> UNI */
	UNIAPI_LINK_ESTABLISH_confirm	= 6,	/* UNI -> API */
	UNIAPI_LINK_RELEASE_request	= 7,	/* API -> UNI */
	UNIAPI_LINK_RELEASE_confirm	= 8,	/* UNI -> API */

	UNIAPI_RESET_request		= 9,	/* API -> UNI */
	UNIAPI_RESET_confirm		= 10,	/* UNI -> API */
	UNIAPI_RESET_indication		= 11,	/* UNI -> API */
	UNIAPI_RESET_ERROR_indication	= 12,	/* UNI -> API */
	UNIAPI_RESET_response		= 13,	/* API -> UNI */
	UNIAPI_RESET_ERROR_response	= 14,	/* API -> UNI */
	UNIAPI_RESET_STATUS_indication	= 15,	/* UNI -> API */

	UNIAPI_SETUP_request		= 16,	/* API -> UNI */
	UNIAPI_SETUP_indication		= 17,	/* UNI -> API */
	UNIAPI_SETUP_response		= 18,	/* API -> UNI */
	UNIAPI_SETUP_confirm		= 19,	/* UNI -> API */
	UNIAPI_SETUP_COMPLETE_indication= 20,	/* U-UNI -> API */
	UNIAPI_SETUP_COMPLETE_request	= 46,	/* API -> N-UNI */
	UNIAPI_ALERTING_request		= 21,	/* API -> UNI */
	UNIAPI_ALERTING_indication	= 22,	/* UNI -> API */
	UNIAPI_PROCEEDING_request	= 23,	/* API -> UNI */
	UNIAPI_PROCEEDING_indication	= 24,	/* UNI -> API */
	UNIAPI_RELEASE_request		= 25,	/* API -> UNI */
	UNIAPI_RELEASE_indication	= 26,	/* UNI -> API */
	UNIAPI_RELEASE_response		= 27,	/* API -> UNI */
	UNIAPI_RELEASE_confirm		= 28,	/* UNI -> API */
	UNIAPI_NOTIFY_request		= 29,	/* API -> UNI */
	UNIAPI_NOTIFY_indication	= 30,	/* UNI -> API */
	UNIAPI_STATUS_indication	= 31,	/* UNI -> API */
	UNIAPI_STATUS_ENQUIRY_request	= 32,	/* API -> UNI */

	UNIAPI_ADD_PARTY_request	= 33,	/* API -> UNI */
	UNIAPI_ADD_PARTY_indication	= 34,	/* UNI -> API */
	UNIAPI_PARTY_ALERTING_request	= 35,	/* API -> UNI */
	UNIAPI_PARTY_ALERTING_indication= 36,	/* UNI -> API */
	UNIAPI_ADD_PARTY_ACK_request	= 37,	/* API -> UNI */
	UNIAPI_ADD_PARTY_ACK_indication	= 38,	/* UNI -> API */
	UNIAPI_ADD_PARTY_REJ_request	= 39,	/* API -> UNI */
	UNIAPI_ADD_PARTY_REJ_indication	= 40,	/* UNI -> API */
	UNIAPI_DROP_PARTY_request	= 41,	/* API -> UNI */
	UNIAPI_DROP_PARTY_indication	= 42,	/* UNI -> API */
	UNIAPI_DROP_PARTY_ACK_request	= 43,	/* API -> UNI */
	UNIAPI_DROP_PARTY_ACK_indication= 44,	/* UNI -> API */

	UNIAPI_ABORT_CALL_request	= 45,	/* API -> UNI */

	UNIAPI_MAXSIG = 47
};

struct uniapi_error {
	uint32_t	reason;
	uint32_t	state;
};
/* keep this in sync with atmapi.h:enum atmerr */

#define UNIAPI_DEF_ERRORS(MACRO)					\
	MACRO(OK, 0, "no error")					\
	MACRO(ERROR_BAD_SIGNAL,	1, "unknown signal")			\
	MACRO(ERROR_BADCU,	2, "signal in bad co-ordinator state")	\
	MACRO(ERROR_BAD_CALLSTATE, 3, "signal in bad call state")	\
	MACRO(ERROR_BAD_EPSTATE, 4, "signal in bad endpoint state")	\
	MACRO(ERROR_BAD_ARG,	5, "bad argument")			\
	MACRO(ERROR_BAD_CALL,	6, "unknown call reference")		\
	MACRO(ERROR_BAD_PARTY,	7, "unknown party")			\
	MACRO(ERROR_BAD_CTYPE,	8, "bad type of call for signal")	\
	MACRO(ERROR_BAD_IE,	9, "bad information element")		\
	MACRO(ERROR_EPREF_INUSE, 10, "endpoint reference already in use") \
	MACRO(ERROR_MISSING_IE,	11, "missing information element")	\
	MACRO(ERROR_ENCODING,	12, "error during message encoding")	\
	MACRO(ERROR_NOMEM,	13, "out of memory")			\
	MACRO(ERROR_BUSY,	14, "status enquiry busy")

enum {
#define DEF(NAME, VAL, STR) UNIAPI_##NAME = VAL,
UNIAPI_DEF_ERRORS(DEF)
#undef DEF
};

struct uniapi_call_created {
	struct uni_cref		cref;
};
struct uniapi_call_destroyed {
	struct uni_cref		cref;
};
struct uniapi_party_created {
	struct uni_cref		cref;
	struct uni_ie_epref	epref;
};
struct uniapi_party_destroyed {
	struct uni_cref		cref;
	struct uni_ie_epref	epref;
};
struct uniapi_abort_call_request {
	struct uni_cref		cref;
};

struct uniapi_reset_request {
	struct uni_ie_restart	restart;
	struct uni_ie_connid	connid;
};

struct uniapi_reset_confirm {
	struct uni_ie_restart	restart;
	struct uni_ie_connid	connid;
};

struct uniapi_reset_indication {
	struct uni_ie_restart	restart;
	struct uni_ie_connid	connid;

};
struct uniapi_reset_error_indication {
	uint32_t		source;		/* 0 - start, 1 - response */
	uint32_t		reason;
};

#define UNIAPI_DEF_RESET_ERRORS(MACRO)				\
	MACRO(UNIAPI_RESET_ERROR_NO_CONFIRM,		0,	\
	    "no confirmation")					\
	MACRO(UNIAPI_RESET_ERROR_NO_RESPONSE,		1,	\
	    "no response")					\
	MACRO(UNIAPI_RESET_ERROR_PEER_INCOMP_STATE,	2,	\
	    "incompatible state")
enum {
#define DEF(NAME, VALUE, STR) NAME = VALUE,
UNIAPI_DEF_RESET_ERRORS(DEF)
#undef DEF
};

struct uniapi_reset_response {
	struct uni_ie_restart	restart;
	struct uni_ie_connid	connid;
};

struct uniapi_reset_error_response {
	struct uni_ie_cause	cause;
};

struct uniapi_reset_status_indication {
	struct uni_cref		cref;		/* STATUS message CREF */
	struct uni_ie_callstate	callstate;
	struct uni_ie_cause	cause;
};

struct uniapi_setup_request {
	struct uni_setup	setup;
};
struct uniapi_setup_indication {
	struct uni_setup	setup;
};
struct uniapi_setup_response {
	struct uni_connect	connect;
};
struct uniapi_setup_confirm {
	struct uni_connect	connect;
};
struct uniapi_setup_complete_indication {
	struct uni_connect_ack	connect_ack;
};
struct uniapi_setup_complete_request {
	struct uni_connect_ack	connect_ack;
};

struct uniapi_alerting_request {
	struct uni_alerting	alerting;
};

struct uniapi_alerting_indication {
	struct uni_alerting	alerting;
};

struct uniapi_proceeding_request {
	struct uni_call_proc	call_proc;
};

struct uniapi_proceeding_indication {
	struct uni_call_proc	call_proc;
};


struct uniapi_release_request {
	struct uni_release	release;
};
struct uniapi_release_indication {
	struct uni_release	release;
};
struct uniapi_release_response {
	struct uni_release_compl release_compl;
};
/*
 * A release confirm can come from a RELEASE COMPLETE or a RELEASE.
 * Because the IEs in a RELEASE COMPLETE are a subset of a RELEASE,
 * use the RELEASE here.
 */
struct uniapi_release_confirm {
	struct uni_release	release;
};

struct uniapi_notify_request {
	struct uni_notify	notify;
};
struct uniapi_notify_indication {
	struct uni_notify	notify;
};

struct uniapi_status_indication {
	struct uni_cref		cref;
	enum uni_callstate	my_state;
	enum uni_cause		my_cause;
	struct uni_ie_callstate	his_state;
	struct uni_ie_cause	his_cause;
	struct uni_ie_epref	epref;
	struct uni_ie_epstate	epstate;
};
struct uniapi_status_enquiry_request {
	struct uni_cref		cref;
	struct uni_ie_epref	epref;
};

struct uniapi_add_party_request {
	struct uni_add_party	add;
};
struct uniapi_add_party_indication {
	struct uni_add_party	add;
};

struct uniapi_party_alerting_request {
	struct uni_party_alerting alert;
};
struct uniapi_party_alerting_indication {
	struct uni_party_alerting alert;
};

struct uniapi_add_party_ack_request {
	struct uni_add_party_ack ack;
};
struct uniapi_add_party_ack_indication {
	struct uni_add_party_ack ack;
};
struct uniapi_add_party_rej_request {
	struct uni_add_party_rej rej;
};
struct uniapi_add_party_rej_indication {
	struct uni_add_party_rej rej;
};

struct uniapi_drop_party_request {
	struct uni_drop_party	drop;
};
struct uniapi_drop_party_indication {
	struct uni_drop_party	drop;
	struct uni_ie_cause	my_cause;
};

struct uniapi_drop_party_ack_request {
	struct uni_drop_party_ack ack;
};
struct uniapi_drop_party_ack_indication {
	struct uni_drop_party	drop;
	struct uni_ie_crankback	crankback;
};

union uniapi_all {
	struct uniapi_error			error;
	struct uniapi_call_created		call_created;
	struct uniapi_call_destroyed		call_destroyed;
	struct uniapi_party_created		party_created;
	struct uniapi_party_destroyed		party_destroyed;
	struct uniapi_abort_call_request	abort_call_request;
	struct uniapi_reset_request		reset_request;
	struct uniapi_reset_confirm		reset_confirm;
	struct uniapi_reset_indication		reset_indication;
	struct uniapi_reset_error_indication	reset_error_indication;
	struct uniapi_reset_response		reset_response;
	struct uniapi_reset_error_response	reset_error_response;
	struct uniapi_reset_status_indication	reset_status_indication;
	struct uniapi_setup_request		setup_request;
	struct uniapi_setup_indication		setup_indication;
	struct uniapi_setup_response		setup_response;
	struct uniapi_setup_confirm		setup_confirm;
	struct uniapi_setup_complete_indication	setup_complete_indication;
	struct uniapi_setup_complete_request	setup_complete_request;
	struct uniapi_alerting_request		alerting_request;
	struct uniapi_alerting_indication	alerting_indication;
	struct uniapi_proceeding_request	proceeding_request;
	struct uniapi_proceeding_indication	proceeding_indication;
	struct uniapi_release_request		release_request;
	struct uniapi_release_indication	release_indication;
	struct uniapi_release_response		release_response;
	struct uniapi_release_confirm		release_confirm;
	struct uniapi_notify_request		notify_request;
	struct uniapi_notify_indication		notify_indication;
	struct uniapi_status_indication		status_indication;
	struct uniapi_status_enquiry_request	status_enquiry_request;
	struct uniapi_add_party_request		add_party_request;
	struct uniapi_add_party_indication	add_party_indication;
	struct uniapi_party_alerting_request	party_alerting_request;
	struct uniapi_party_alerting_indication	party_alerting_indication;
	struct uniapi_add_party_ack_request	add_party_ack_request;
	struct uniapi_add_party_ack_indication	add_party_ack_indication;
	struct uniapi_add_party_rej_request	add_party_rej_request;
	struct uniapi_add_party_rej_indication	add_party_rej_indication;
	struct uniapi_drop_party_request	drop_party_request;
	struct uniapi_drop_party_indication	drop_party_indication;
	struct uniapi_drop_party_ack_request	drop_party_ack_request;
	struct uniapi_drop_party_ack_indication	drop_party_ack_indication;
};

#endif
