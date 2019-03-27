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
 * $Begemot: libunimsg/netnatm/sig/unipriv.h,v 1.17 2004/07/08 08:22:25 brandt Exp $
 *
 * Private UNI stuff.
 */
#ifndef unipriv_h
#define unipriv_h

#ifdef _KERNEL
#ifdef __FreeBSD__
#include <netgraph/atm/uni/ng_uni_cust.h>
#endif
#else
#include "unicust.h"
#endif

struct call;
struct party;

enum cu_stat {
	CU_STAT0,	/* AAL connection released */
	CU_STAT1,	/* awaiting establish */
	CU_STAT2,	/* awaiting release */
	CU_STAT3,	/* AAL connection established */
};

/*
 * Internal Signals
 */
#define DEF_COORD_SIGS						\
  DEF_PRIV_SIG(O_SAAL_ESTABLISH_indication,	SAAL)		\
  DEF_PRIV_SIG(O_SAAL_ESTABLISH_confirm,	SAAL)		\
  DEF_PRIV_SIG(O_SAAL_RELEASE_indication,	SAAL)		\
  DEF_PRIV_SIG(O_SAAL_RELEASE_confirm,		SAAL)		\
  DEF_PRIV_SIG(O_SAAL_DATA_indication,		SAAL)		\
  DEF_PRIV_SIG(O_SAAL_UDATA_indication,		SAAL)		\
  DEF_PRIV_SIG(O_T309,				Coord)		\
  DEF_PRIV_SIG(O_DATA,				Coord)		\
  DEF_PRIV_SIG(O_LINK_ESTABLISH_request,	API)		\
  DEF_PRIV_SIG(O_LINK_RELEASE_request,		API)		\
  DEF_PRIV_SIG(O_RESET_request,			API)		\
  DEF_PRIV_SIG(O_RESET_response,		API)		\
  DEF_PRIV_SIG(O_RESET_ERROR_response,		API)		\
  DEF_PRIV_SIG(O_SETUP_request,			API)		\
  DEF_PRIV_SIG(O_SETUP_response,		API)		\
  DEF_PRIV_SIG(O_SETUP_COMPLETE_request,	API)		\
  DEF_PRIV_SIG(O_PROCEEDING_request,		API)		\
  DEF_PRIV_SIG(O_ALERTING_request,		API)		\
  DEF_PRIV_SIG(O_RELEASE_request,		API)		\
  DEF_PRIV_SIG(O_RELEASE_response,		API)		\
  DEF_PRIV_SIG(O_NOTIFY_request,		API)		\
  DEF_PRIV_SIG(O_STATUS_ENQUIRY_request,	API)		\
  DEF_PRIV_SIG(O_ADD_PARTY_request,		API)		\
  DEF_PRIV_SIG(O_PARTY_ALERTING_request,	API)		\
  DEF_PRIV_SIG(O_ADD_PARTY_ACK_request,		API)		\
  DEF_PRIV_SIG(O_ADD_PARTY_REJ_request,		API)		\
  DEF_PRIV_SIG(O_DROP_PARTY_request,		API)		\
  DEF_PRIV_SIG(O_DROP_PARTY_ACK_request,	API)		\
  DEF_PRIV_SIG(O_ABORT_CALL_request,		API)		\
  DEF_PRIV_SIG(O_CALL_DESTROYED,		CallControl)	\
  DEF_PRIV_SIG(O_RESET_indication,		ResetRespond)	\
  DEF_PRIV_SIG(O_END,				Coord)

#define DEF_RESPOND_SIGS					\
  DEF_PRIV_SIG(R_RESTART,			Coord)		\
  DEF_PRIV_SIG(R_STATUS,			Coord)		\
  DEF_PRIV_SIG(R_RESET_response,		Coord)		\
  DEF_PRIV_SIG(R_RESET_ERROR_response,		Coord)		\
  DEF_PRIV_SIG(R_T317,				ResetRespond)	\
  DEF_PRIV_SIG(R_END,				ResetRespond)

#define DEF_START_SIGS						\
  DEF_PRIV_SIG(S_RESTART_ACK,			Coord)		\
  DEF_PRIV_SIG(S_STATUS,			Coord)		\
  DEF_PRIV_SIG(S_RESET_request,			Coord)		\
  DEF_PRIV_SIG(S_T316,				ResetStart)	\
  DEF_PRIV_SIG(S_END,				ResetStart)

#define DEF_CALL_SIGS						\
  DEF_PRIV_SIG(C_LINK_ESTABLISH_confirm,	Coord)		\
  DEF_PRIV_SIG(C_LINK_ESTABLISH_indication,	Coord)		\
  DEF_PRIV_SIG(C_LINK_ESTABLISH_ERROR_indication,Coord)		\
  DEF_PRIV_SIG(C_LINK_RELEASE_indication,	Coord)		\
  DEF_PRIV_SIG(C_SETUP_request,			Coord)		\
  DEF_PRIV_SIG(C_SETUP_response,		Coord)		\
  DEF_PRIV_SIG(C_SETUP_COMPLETE_request,	Coord)		\
  DEF_PRIV_SIG(C_PROCEEDING_request,		Coord)		\
  DEF_PRIV_SIG(C_ALERTING_request,		Coord)		\
  DEF_PRIV_SIG(C_RELEASE_request,		Coord)		\
  DEF_PRIV_SIG(C_RELEASE_response,		Coord)		\
  DEF_PRIV_SIG(C_NOTIFY_request,		Coord)		\
  DEF_PRIV_SIG(C_STATUS_ENQUIRY_request,	Coord)		\
  DEF_PRIV_SIG(C_ADD_PARTY_request,		Coord)		\
  DEF_PRIV_SIG(C_PARTY_ALERTING_request,	Coord)		\
  DEF_PRIV_SIG(C_ADD_PARTY_ACK_request,		Coord)		\
  DEF_PRIV_SIG(C_ADD_PARTY_REJ_request,		Coord)		\
  DEF_PRIV_SIG(C_DROP_PARTY_request,		Coord)		\
  DEF_PRIV_SIG(C_DROP_PARTY_ACK_request,	Coord)		\
  DEF_PRIV_SIG(C_ABORT_CALL_request,		Coord)		\
  DEF_PRIV_SIG(C_UNKNOWN,			Coord)		\
  DEF_PRIV_SIG(C_SETUP,				Coord)		\
  DEF_PRIV_SIG(C_CALL_PROC,			Coord)		\
  DEF_PRIV_SIG(C_ALERTING,			Coord)		\
  DEF_PRIV_SIG(C_CONNECT,			Coord)		\
  DEF_PRIV_SIG(C_CONNECT_ACK,			Coord)		\
  DEF_PRIV_SIG(C_RELEASE,			Coord)		\
  DEF_PRIV_SIG(C_RELEASE_COMPL,			Coord)		\
  DEF_PRIV_SIG(C_COBISETUP,			Coord)		\
  DEF_PRIV_SIG(C_NOTIFY,			Coord)		\
  DEF_PRIV_SIG(C_STATUS,			Coord)		\
  DEF_PRIV_SIG(C_STATUS_ENQ,			Coord)		\
  DEF_PRIV_SIG(C_ADD_PARTY,			Coord)		\
  DEF_PRIV_SIG(C_PARTY_ALERTING,		Coord)		\
  DEF_PRIV_SIG(C_ADD_PARTY_ACK,			Coord)		\
  DEF_PRIV_SIG(C_ADD_PARTY_REJ,			Coord)		\
  DEF_PRIV_SIG(C_DROP_PARTY,			Coord)		\
  DEF_PRIV_SIG(C_DROP_PARTY_ACK,		Coord)		\
  DEF_PRIV_SIG(C_CALL_DELETE,			CallControl)	\
  DEF_PRIV_SIG(C_T301,				CallControl)	\
  DEF_PRIV_SIG(C_T303,				CallControl)	\
  DEF_PRIV_SIG(C_T308,				CallControl)	\
  DEF_PRIV_SIG(C_T310,				CallControl)	\
  DEF_PRIV_SIG(C_T313,				CallControl)	\
  DEF_PRIV_SIG(C_T322,				CallControl)	\
  DEF_PRIV_SIG(C_DROP_PARTY_indication,		PartyControl)	\
  DEF_PRIV_SIG(C_SEND_DROP_PARTY,		PartyControl)	\
  DEF_PRIV_SIG(C_DROP_PARTY_ACK_indication,	PartyControl)	\
  DEF_PRIV_SIG(C_SEND_DROP_PARTY_ACK,		PartyControl)	\
  DEF_PRIV_SIG(C_ADD_PARTY_REJ_indication,	PartyControl)	\
  DEF_PRIV_SIG(C_SEND_ADD_PARTY_REJ,		PartyControl)	\
  DEF_PRIV_SIG(C_SEND_STATUS_ENQ,		PartyControl)	\
  DEF_PRIV_SIG(C_PARTY_DESTROYED,		PartyControl)	\
  DEF_PRIV_SIG(C_END,				CallControl)

#define DEF_PARTY_SIGS							\
  DEF_PRIV_SIG(P_SETUP,				CallControl)		\
  DEF_PRIV_SIG(P_ALERTING,			CallControl)		\
  DEF_PRIV_SIG(P_CONNECT,			CallControl)		\
  DEF_PRIV_SIG(P_CONNECT_ACK,			CallControl)		\
  DEF_PRIV_SIG(P_RELEASE,			CallControl)		\
  DEF_PRIV_SIG(P_RELEASE_COMPL,			CallControl)		\
  DEF_PRIV_SIG(P_STATUS,			CallControl)		\
  DEF_PRIV_SIG(P_ADD_PARTY,			CallControl)		\
  DEF_PRIV_SIG(P_PARTY_ALERTING,		CallControl)		\
  DEF_PRIV_SIG(P_ADD_PARTY_ACK,			CallControl)		\
  DEF_PRIV_SIG(P_ADD_PARTY_REJ,			CallControl)		\
  DEF_PRIV_SIG(P_DROP_PARTY,			CallControl)		\
  DEF_PRIV_SIG(P_DROP_PARTY_ACK,		CallControl)		\
  DEF_PRIV_SIG(P_SETUP_request,			CallControl)		\
  DEF_PRIV_SIG(P_SETUP_response,		CallControl)		\
  DEF_PRIV_SIG(P_SETUP_COMPL_request,		CallControl)		\
  DEF_PRIV_SIG(P_ALERTING_request,		CallControl)		\
  DEF_PRIV_SIG(P_RELEASE_request,		CallControl)		\
  DEF_PRIV_SIG(P_RELEASE_response,		CallControl)		\
  DEF_PRIV_SIG(P_RELEASE_confirm,		CallControl)		\
  DEF_PRIV_SIG(P_STATUS_ENQUIRY_request,	CallControl)		\
  DEF_PRIV_SIG(P_ADD_PARTY_request,		CallControl)		\
  DEF_PRIV_SIG(P_PARTY_ALERTING_request,	CallControl)		\
  DEF_PRIV_SIG(P_ADD_PARTY_ACK_request,		CallControl)		\
  DEF_PRIV_SIG(P_ADD_PARTY_REJ_request,		CallControl)		\
  DEF_PRIV_SIG(P_DROP_PARTY_request,		CallControl)		\
  DEF_PRIV_SIG(P_DROP_PARTY_ACK_request,	CallControl)		\
  DEF_PRIV_SIG(P_PARTY_DELETE,			PartyControl)		\
  DEF_PRIV_SIG(P_T397,				PartyControl)		\
  DEF_PRIV_SIG(P_T398,				PartyControl)		\
  DEF_PRIV_SIG(P_T399,				PartyControl)		\
  DEF_PRIV_SIG(P_END,				PartyControl)


#define DEF_PRIV_SIG(NAME, FROM)	SIG##NAME,
enum coord_sig {
	DEF_COORD_SIGS
};
enum respond_sig {
	DEF_RESPOND_SIGS
};
enum start_sig {
	DEF_START_SIGS
};
enum call_sig {
	DEF_CALL_SIGS
};
enum party_sig {
	DEF_PARTY_SIGS
};
#undef DEF_PRIV_SIG

/*************************************************************
 *
 * SIGNALS and SIGNAL QUEUES
 */
enum {
	SIG_COORD,
	SIG_RESET_START,
	SIG_RESET_RESP,
	SIG_CALL,
	SIG_PARTY,
};

struct sig {
	TAILQ_ENTRY(sig) link;
	u_int		type;	/* one of the above */
	struct call	*call;	/* call to send to */
	struct party	*party;	/* party to send to */
	uint32_t	sig;	/* the signal */
	uint32_t	cookie;	/* user cookie */
	struct uni_msg	*msg;	/* attached message */
	struct uni_all	*u;	/* dito */
};
TAILQ_HEAD(sigqueue, sig);

#define SIGQ_CLEAR(Q)							\
    do {								\
	struct sig *s;							\
	while(!TAILQ_EMPTY(Q)) {					\
		s = TAILQ_FIRST(Q);					\
		TAILQ_REMOVE(Q, s, link);				\
		if(s->msg) uni_msg_destroy(s->msg);			\
		if(s->u) UNI_FREE(s->u);				\
		SIG_FREE(s);						\
	}								\
    } while(0)

void uni_sig_party(struct party *, enum party_sig, uint32_t cookie,
    struct uni_msg *, struct uni_all *);
void uni_sig_call(struct call *, enum call_sig, uint32_t cookie,
    struct uni_msg *, struct uni_all *);
void uni_sig_coord(struct uni *, enum coord_sig, uint32_t cookie,
    struct uni_msg *);
void uni_sig_start(struct uni *, enum start_sig, uint32_t cookie,
    struct uni_msg *, struct uni_all *);
void uni_sig_respond(struct uni *, enum respond_sig, uint32_t cookie,
    struct uni_msg *, struct uni_all *);

/*************************************************************
 *
 * CALL INSTANCES
 */
struct party {
	struct call		*call;
	TAILQ_ENTRY(party)	link;
	u_int			epref;	/* endpoint reference */
	u_int			flags;	/* flags */
	enum uni_epstate	state;	/* party state */

	struct uni_timer	t397;	/* T397 */
	struct uni_timer	t398;	/* T398 */
	struct uni_timer	t399;	/* T399 */
};
#define	PARTY_MINE	0x0001		/* must be 1 */
#define	PARTY_CONNECT	0x0002		/* connect request from this party */

TAILQ_HEAD(partyqueue, party);

void uni_destroy_party(struct party *, int);
struct party *uni_find_party(struct call *, struct uni_ie_epref *);
struct party *uni_find_partyx(struct call *, u_int epref, u_int mine);
struct party *uni_create_party(struct call *, struct uni_ie_epref *);
struct party *uni_create_partyx(struct call *, u_int epref, u_int mine,
    uint32_t cookie);
u_int uni_party_act_count(struct call *, int);

enum call_type {
	CALL_NULL,	/* not known yet */
	CALL_P2P,	/* normal point-to-point call */
	CALL_COBI,	/* Q.2932.1 COBI call */
	CALL_ROOT,	/* point-to-multipoint root */
	CALL_LEAF,	/* point-to-multipoint leaf */
};

enum call_state {
	CALLST_NULL,
	CALLST_U1, CALLST_U3, CALLST_U4, CALLST_U6, CALLST_U7, CALLST_U8,
	CALLST_U9, CALLST_U10, CALLST_U11, CALLST_U12,
	CALLST_N1, CALLST_N3, CALLST_N4, CALLST_N6, CALLST_N7, CALLST_N8,
	CALLST_N9, CALLST_N10, CALLST_N11, CALLST_N12
};

struct call {
	TAILQ_ENTRY(call)	link;	/* link between calls */
	struct uni		*uni;	/* backpointer to owning UNI */
	u_int			cref;	/* call reference value or lij seqno */
	u_int			mine;	/* if TRUE this is my call */
	enum call_type		type;	/* what call is it */
	enum call_state		cstate;	/* the state of the call */
	struct uni_ie_connid	connid;	/* the connection ID */
	struct uni_setup	msg_setup;	/* retransmission */
	struct uni_release	msg_release;	/* retransmission */
	struct uni_ie_epref	stat_epref;	/* retransmission */
	struct partyqueue	parties;
	u_int			se_active;	/* status enquiry active */
	u_int			epref_alloc;

	struct uni_timer	t308;	/* T303 */
	u_int			cnt308;

	struct uni_timer	t303;	/* T303 */
	u_int			cnt303;

	struct uni_timer	t301;	/* T301 */
	struct uni_timer	t310;	/* T310 */
	struct uni_timer	t313;	/* T313 */

	struct uni_timer	t322;	/* T322 */
	u_int			cnt322;
};

TAILQ_HEAD(callqueue, call);

struct call *uni_find_call(struct uni *, struct uni_cref *);
struct call *uni_find_callx(struct uni *, u_int cref, u_int mine);
struct call *uni_create_call(struct uni *, u_int cref, u_int mine,
	uint32_t cookie);
struct call *uni_create_new_call(struct uni *, uint32_t cookie);
void uni_destroy_call(struct call *, int);

void uni_bad_message(struct call *, struct uni_all *, u_int,
    struct uni_ie_epref *, int);

extern const struct callstates {
	const char *name;
	enum uni_callstate ext;
} callstates[];

/*************************************************************
 *
 * UNI INSTANCE
 */
struct uni {
	void			*arg;	/* user arg */
	const struct uni_funcs	*funcs;

	enum uni_proto		proto;	/* protocol */
	struct unicx		cx;	/* decoding/coding context */
	int			sb_tb : 1;	/* Sb-Tb/Tb point */

	struct sigqueue		workq;	/* work queue */
	struct sigqueue		delq;	/* delayed signal queue */
	int			working;

	uint32_t		cref_alloc;

	enum cu_stat		custat;	/* coordinator state */
	struct uni_timer	t309;
	u_int			timer309;

	enum uni_callstate	glob_start;
	enum uni_callstate	glob_respond;
	struct uni_timer	t316;
	struct uni_timer	t317;
	struct uni_ie_connid	connid_start;
	struct uni_ie_connid	connid_respond;
	u_int			cnt316;
	struct uni_ie_restart	restart_start;

	struct callqueue	calls;

	struct uni_ie_cause	cause;	/* working area for verify */

	/* tuneable parameters */
	u_int			timer301;
	u_int			init303;
	u_int			timer303;
	u_int			init308;
	u_int			timer308;
	u_int			timer310;
	u_int			timer313;
	u_int			init316;
	u_int			timer316;
	u_int			timer317;
	u_int			timer322;
	u_int			init322;
	u_int			timer397;
	u_int			timer398;
	u_int			timer399;

	u_int			debug[UNI_MAXFACILITY];
};

void uniapi_uni_error(struct uni *uni, uint32_t reason, uint32_t cookie,
	uint32_t state);
void uniapi_call_error(struct call *c, uint32_t reason, uint32_t cookie);
void uniapi_party_error(struct party *p, uint32_t reason, uint32_t cookie);

/*************************************************************
 *
 * INLINE FUNCTIONS
 */

/* Enqueue a signal in the working queue */
void uni_enq_sig(struct uni *, u_int, struct call *, struct party *,
    uint32_t, uint32_t, struct uni_msg *, struct uni_all *);

/* Enqueue a signal in the delayed queue */
void uni_delenq_sig(struct uni *, u_int, struct call *, struct party *,
    uint32_t, uint32_t, struct uni_msg *, struct uni_all *);

/* Enqueue a signal to the coordinator */
#define	uni_enq_coord(UNI, SIG, COOKIE, MSG) do {			\
	uni_enq_sig((UNI), SIG_COORD, NULL, NULL,			\
	    (SIG), (COOKIE), (MSG), NULL);				\
    } while (0)

/* Enqueue a delayed signal to the coordinator */
#define	uni_delenq_coord(UNI, SIG, COOKIE, MSG) do {			\
	uni_delenq_sig((UNI), SIG_COORD, NULL, NULL,			\
	    (SIG), (COOKIE), (MSG), NULL);				\
    } while (0)

/* Enqueue a signal to a call */
#define uni_enq_call(CALL, SIG, COOKIE, MSG, U) do {			\
	uni_enq_sig((CALL)->uni, SIG_CALL, (CALL), NULL,		\
	    (SIG), (COOKIE), (MSG), (U));				\
    } while (0)

/* Enqueue a signal to a party */
#define uni_enq_party(PARTY, SIG, COOKIE, MSG, U) do {			\
	uni_enq_sig((PARTY)->call->uni, SIG_PARTY, (PARTY)->call,	\
	    (PARTY), (SIG), (COOKIE), (MSG), (U));			\
    } while (0)

/* Enqueue a signal to RESET-START */
#define	uni_enq_start(UNI, SIG, COOKIE, MSG, U) do {			\
	uni_enq_sig((UNI), SIG_RESET_START, NULL, NULL,			\
	    (SIG), (COOKIE), (MSG), (U));				\
    } while (0)

/* Enqueue a signal to RESET-RESPOND */
#define	uni_enq_resp(UNI, SIG, COOKIE, MSG, U) do {			\
	uni_enq_sig((UNI), SIG_RESET_RESP, NULL, NULL,			\
	    (SIG), (COOKIE), (MSG), (U));				\
    } while (0)

int uni_send_output(struct uni_all *u, struct uni *uni);
void uni_undel(struct uni *, int (*)(struct sig *, void *), void *);
void uni_delsig(struct uni *, u_int, struct call *, struct party *);

void uni_release_compl(struct call *, struct uni_all *);

/*************************************************************/
/*
 * Message verification.
 */
#define MANDATE_IE(UNI,MSG,IE)						\
    do {								\
	if (!IE_ISGOOD(MSG))						\
		uni_mandate_ie(UNI, IE);				\
    } while(0)

enum verify {
	VFY_OK,		/* ok */
	VFY_RAP,	/* report and proceed */
	VFY_RAPU,	/* report and proceed becuase of unknown IEs */
	VFY_I,		/* ignore */
	VFY_CLR,	/* clear call */
	VFY_RAI,	/* report and ignore */
	VFY_RAIM,	/* report and ignore because if mandat. IE miss */
};

void uni_mandate_ie(struct uni *, enum uni_ietype);
void uni_mandate_epref(struct uni *, struct uni_ie_epref *);
enum verify uni_verify(struct uni *, enum uni_msgact);
void uni_respond_status_verify(struct uni *, struct uni_cref *,
	enum uni_callstate, struct uni_ie_epref *, enum uni_epstate);
void uni_vfy_remove_unknown(struct uni *);
void uni_vfy_remove_cause(struct uni *);
void uni_vfy_collect_ies(struct uni *);


void uni_respond_status(struct uni *uni, struct uni_cref *cref,
    enum uni_callstate cs, enum uni_cause c1);
void uni_respond_status_mtype(struct uni *uni, struct uni_cref *cref,
    enum uni_callstate cs, enum uni_cause c1, u_int mtype);

#define	FOREACH_ERR(E, UNI) \
	for ((E) = (UNI)->cx.err; (E) < (UNI)->cx.err + (UNI)->cx.errcnt; (E)++)

#define ALLOC_API(TYPE,API)					\
    ({								\
	TYPE *_tmp = NULL;					\
								\
	if(((API) = uni_msg_alloc(sizeof(TYPE))) != NULL) {	\
		_tmp = uni_msg_wptr((API), TYPE *);		\
		(API)->b_wptr += sizeof(TYPE);			\
		memset(_tmp, 0, sizeof(TYPE));			\
	}							\
	_tmp;							\
    })

#if defined(__GNUC__) && __GNUC__ < 3

#define	VERBOSE(UNI, FAC, LEVEL, ARGS...) do {			\
	if ((UNI)->debug[(FAC)] >= (LEVEL)) {			\
		(UNI)->funcs->verbose((UNI), (UNI)->arg, (FAC) ,\
		   ## ARGS);					\
	}							\
    } while(0)

#define VERBOSE0(UNI, FAC, ARGS...) do {			\
	(UNI)->funcs->verbose((UNI), (UNI)->arg, (FAC) ,	\
	    ## ARGS);						\
    } while(0)

#else

#define VERBOSE(UNI, FAC, LEVEL, ...) do {			\
	if ((UNI)->debug[(FAC)] >= (LEVEL)) {			\
		(UNI)->funcs->verbose((UNI), (UNI)->arg, (FAC),	\
		    __VA_ARGS__);				\
	}							\
    } while(0)

#define VERBOSE0(UNI, FAC, ...) do {				\
	(UNI)->funcs->verbose((UNI), (UNI)->arg, (FAC),		\
	    __VA_ARGS__);					\
    } while(0)

#endif

#define TIMER_INIT_UNI(U,T)	_TIMER_INIT(U,T)
#define TIMER_INIT_CALL(C,T)	_TIMER_INIT(C,T)
#define TIMER_INIT_PARTY(P,T)	_TIMER_INIT(P,T)

#define TIMER_DESTROY_UNI(U,T)		_TIMER_DESTROY(U, (U)->T)
#define TIMER_DESTROY_CALL(C,T)		_TIMER_DESTROY((C)->uni, (C)->T)
#define TIMER_DESTROY_PARTY(P,T)	_TIMER_DESTROY((P)->call->uni, (P)->T)

#define TIMER_STOP_UNI(U,T)	_TIMER_STOP(U, (U)->T)
#define TIMER_STOP_CALL(C,T)	_TIMER_STOP((C)->uni, (C)->T)
#define TIMER_STOP_PARTY(P,T)	_TIMER_STOP((P)->call->uni, (P)->T)

#define TIMER_START_UNI(U,T,N) _TIMER_START(U, U, (U)->T, N, _##T##_func)
#define TIMER_START_CALL(C,T,N) _TIMER_START(C->uni, C, (C)->T, N, _##T##_func)
#define TIMER_START_PARTY(P,T,N) _TIMER_START(P->call->uni, P, (P)->T, N, _##T##_func)

#endif
