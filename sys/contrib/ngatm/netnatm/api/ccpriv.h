/*
 * Copyright (c) 2003-2004
 *	Hartmut Brandt
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY THE AUTHOR
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Begemot: libunimsg/netnatm/api/ccpriv.h,v 1.2 2005/05/23 11:49:17 brandt_h Exp $
 *
 * ATM API as defined per af-saa-0108
 *
 * Private declarations.
 */
#ifdef _KERNEL
#ifdef __FreeBSD__
#include <netgraph/atm/ccatm/ng_ccatm_cust.h>
#endif
#else	/* !_KERNEL */
#include "cccust.h"
#endif

struct ccuser;
struct ccconn;
struct ccaddr;
struct ccport;
struct ccdata;
struct ccsig;
struct ccparty;

LIST_HEAD(ccuser_list, ccuser);
LIST_HEAD(ccconn_list, ccconn);
TAILQ_HEAD(ccaddr_list, ccaddr);
TAILQ_HEAD(ccport_list, ccport);
TAILQ_HEAD(ccsig_list, ccsig);
LIST_HEAD(ccparty_list, ccparty);

/*
 * Private node data.
 */
struct ccdata {
	struct ccuser_list	user_list;	/* instance list */
	struct ccport_list	port_list;	/* list of ports */
	struct ccconn_list	orphaned_conns;	/* list of connections */
	struct ccsig_list	sigs;		/* current signals */
	struct ccsig_list	def_sigs;	/* deferred signals */
	struct ccsig_list	free_sigs;	/* free signals */

	const struct cc_funcs	*funcs;
	uint32_t		cookie;		/* cookie generator */
	u_int			log;		/* logging flags */
};

/* retrieve info on local ports */
struct atm_port_list *cc_get_local_port_info(struct ccdata *,
    u_int, size_t *);

/* log */
#ifdef CCATM_DEBUG
#if defined(__GNUC__) && __GNUC__ < 3
#define	cc_log(CC, FMT, ARGS...) do {					\
	(CC)->funcs->log("%s (data=%p): " FMT, __FUNCTION__,		\
	    (CC) , ## ARGS);						\
    } while (0)
#else
#define	cc_log(CC, FMT, ...) do {					\
	(CC)->funcs->log("%s (data=%p): " FMT, __func__,		\
	    (CC), __VA_ARGS__);						\
    } while (0)
#endif
#else
#if defined(__GNUC__) && __GNUC__ < 3
#define	cc_log(CC, FMT, ARGS...) do { } while (0)
#else
#define	cc_log(CC, FMT, ...) do { } while (0)
#endif
#endif

/*
 * structure to remember cookies for outstanding requests
 * we also remember the request itself but don't use it.
 */
struct ccreq {
	TAILQ_ENTRY(ccreq)	link;
	uint32_t		cookie;
	uint32_t		req;
	struct ccconn		*conn;
};
TAILQ_HEAD(ccreq_list, ccreq);

/*
 * Port data. Each port has one UNI stack below.
 * The port number is in param.port. The number is assigned when the
 * hook to the uni is connected. This hook has the name 'uni<port>'.
 */
struct ccport {
	void		*uarg;		/* hook to UNI protocol */
	struct ccdata 	*cc;		/* back pointer to node */
	enum {
		CCPORT_STOPPED,		/* halted */
		CCPORT_RUNNING,		/* ok */
	}		admin;		/* admin status */
	struct ccconn_list conn_list;	/* list of connections */
	struct ccaddr_list addr_list;	/* list of network addresses */
	struct atm_port_info param;	/* parameters */

	/* list of outstanding requests */
	struct ccreq_list	cookies;

	TAILQ_ENTRY(ccport) node_link;
};

#ifdef CCATM_DEBUG
#if defined(__GNUC__) && __GNUC__ < 3
#define	cc_port_log(P, FMT, ARGS...) do {				\
	(P)->cc->funcs->log("%s (port=%p/%u): " FMT, __FUNCTION__,	\
	    (P), (P)->param.port , ## ARGS);				\
    } while (0)
#else
#define	cc_port_log(P, FMT, ...) do {					\
	(P)->cc->funcs->log("%s (port=%p/%u): " FMT, __func__,		\
	    (P), (P)->param.port, __VA_ARGS__);				\
    } while (0)
#endif
#else
#if defined(__GNUC__) && __GNUC__ < 3
#define	cc_port_log(P, FMT, ARGS...) do { } while (0)
#else
#define	cc_port_log(P, FMT, ...) do { } while (0)
#endif
#endif

#define CONN_STATES					\
	DEF(CONN_NULL)			/*  C0 */	\
	DEF(CONN_OUT_PREPARING)		/*  C1 */	\
	DEF(CONN_OUT_WAIT_CREATE)	/*  C2 */	\
	DEF(CONN_OUT_WAIT_OK)		/*  C3 */	\
	DEF(CONN_OUT_WAIT_CONF)		/*  C4 */	\
							\
	DEF(CONN_ACTIVE)		/*  C5 */	\
							\
	DEF(CONN_IN_PREPARING)		/* C10 */	\
	DEF(CONN_IN_WAITING)		/* C21 */	\
	DEF(CONN_IN_ARRIVED)		/* C11 */	\
	DEF(CONN_IN_WAIT_ACCEPT_OK)	/* C12 */	\
	DEF(CONN_IN_WAIT_COMPL)		/* C13 */	\
							\
	DEF(CONN_REJ_WAIT_OK)		/* C14 */	\
	DEF(CONN_REL_IN_WAIT_OK)	/* C15 */	\
	DEF(CONN_REL_WAIT_OK)		/* C20 */	\
							\
	DEF(CONN_AB_WAIT_REQ_OK)	/* C33 */	\
	DEF(CONN_AB_WAIT_RESP_OK)	/* C34 */	\
	DEF(CONN_AB_FLUSH_IND)		/* C35 */	\
	DEF(CONN_OUT_WAIT_DESTROY)	/* C37 */

enum conn_state {
#define DEF(N) N,
	CONN_STATES
#undef DEF
};

#define	CONN_SIGS							\
	DEF(CONNECT_OUTGOING)	/* U */					\
	DEF(ARRIVAL)		/* U */					\
	DEF(RELEASE)		/* U */					\
	DEF(REJECT)		/* U */					\
	DEF(ACCEPT)		/* U newuser */				\
	DEF(ADD_PARTY)		/* U ident */				\
	DEF(DROP_PARTY)		/* U ident */				\
	DEF(USER_ABORT)		/* U */					\
									\
	DEF(CREATED)		/* P msg */				\
	DEF(DESTROYED)		/* P */					\
	DEF(SETUP_CONFIRM)	/* P msg */				\
	DEF(SETUP_IND)		/* P msg */				\
	DEF(SETUP_COMPL)	/* P msg */				\
	DEF(PROC_IND)		/* P msg */				\
	DEF(ALERTING_IND)	/* P msg */				\
	DEF(REL_CONF)		/* P msg */				\
	DEF(REL_IND)		/* P msg */				\
	DEF(PARTY_CREATED)	/* P msg */				\
	DEF(PARTY_DESTROYED)	/* P msg */				\
	DEF(PARTY_ALERTING_IND)	/* P msg */				\
	DEF(PARTY_ADD_ACK_IND)	/* P msg */				\
	DEF(PARTY_ADD_REJ_IND)	/* P msg */				\
	DEF(DROP_PARTY_IND)	/* P msg */				\
	DEF(DROP_PARTY_ACK_IND)	/* P msg */				\
									\
	DEF(OK)			/* P msg */				\
	DEF(ERROR)		/* P msg */

enum conn_sig {
#define DEF(NAME) CONN_SIG_##NAME,
CONN_SIGS
#undef DEF
};
extern const char *const cc_conn_sigtab[];

/*
 * This describes a connection and must be in sync with the UNI
 * stack.
 */
struct ccconn {
	enum conn_state		state;	/* API state of the connection */
	struct ccdata		*cc;	/* owner node */
	struct ccport		*port;	/* the port we belong to */
	struct ccuser		*user;	/* user instance we belong to */
    	TAILQ_ENTRY(ccconn)	connq_link;	/* queue of the owner */
	LIST_ENTRY(ccconn) 	port_link;	/* link in list of port */
	struct uni_cref		cref;
	uint8_t			reason;
	struct ccuser		*acceptor;

	/* attributes */
	uint32_t		blli_selector;
	struct uni_ie_blli	blli[UNI_NUM_IE_BLLI];

	struct uni_ie_bearer	bearer;
	struct uni_ie_traffic	traffic;
	struct uni_ie_qos	qos;
	struct uni_ie_exqos	exqos;
	struct uni_ie_called	called;
	struct uni_ie_calledsub	calledsub;
	struct uni_ie_aal	aal;
	struct uni_ie_epref	epref;
	struct uni_ie_conned	conned;
	struct uni_ie_connedsub	connedsub;
	struct uni_ie_eetd	eetd;
	struct uni_ie_abrsetup	abrsetup;
	struct uni_ie_abradd	abradd;
	struct uni_ie_mdcr	mdcr;

	struct uni_ie_calling	calling;
	struct uni_ie_callingsub callingsub;
	struct uni_ie_connid	connid;
	struct uni_ie_tns	tns[UNI_NUM_IE_TNS];
	struct uni_ie_atraffic	atraffic;
	struct uni_ie_mintraffic mintraffic;
	struct uni_ie_cscope	cscope;
	struct uni_ie_bhli	bhli;

	/* bit mask of written attributes in A6 */
	u_int			dirty_attr;

	struct uni_ie_cause	cause[2];

	struct ccparty_list	parties;
};

/* dirty attribute mask values */
enum {
	CCDIRTY_AAL		= 0x0001,
	CCDIRTY_BLLI		= 0x0002,
	CCDIRTY_CONNID		= 0x0004,
	CCDIRTY_NOTIFY		= 0x0008,	/* XXX */
	CCDIRTY_EETD		= 0x0010,
	CCDIRTY_GIT		= 0x0020,	/* XXX */
	CCDIRTY_UU		= 0x0040,	/* XXX */
	CCDIRTY_TRAFFIC		= 0x0080,
	CCDIRTY_EXQOS		= 0x0100,
	CCDIRTY_ABRSETUP	= 0x0200,
	CCDIRTY_ABRADD		= 0x0400,
};

/* set conn to new state */
void cc_conn_set_state(struct ccconn *, enum conn_state);

/* return string for state */
const char *cc_conn_state2str(u_int);

/* connect connection to user */
void cc_connect_to_user(struct ccconn *, struct ccuser *);

/* disconnect from the user */
void cc_disconnect_from_user(struct ccconn *);

/* abort the connection */
void cc_conn_abort(struct ccconn *, int);

/* destroy a connection */
void cc_conn_destroy(struct ccconn *);

/* create a connection */
struct ccconn *cc_conn_create(struct ccdata *);

/* assign to port */
void cc_conn_ins_port(struct ccconn *, struct ccport *);

/* remove from port */
void cc_conn_rem_port(struct ccconn *);

/* dispatch a connection to a user or reject it */
void cc_conn_dispatch(struct ccconn *);

/* disconnect from acceptor */
void cc_conn_reset_acceptor(struct ccconn *);

/* log on a connection */
#ifdef CCATM_DEBUG
#if defined(__GNUC__) && __GNUC__ < 3
#define	cc_conn_log(C, FMT, ARGS...) do {				\
	(C)->cc->funcs->log("%s (conn=%p): " FMT, __FUNCTION__,		\
	    (C) , ## ARGS);						\
    } while (0)
#else
#define	cc_conn_log(C, FMT, ...) do {					\
	(C)->cc->funcs->log("%s (conn=%p): " FMT, __func__,		\
	    (C), __VA_ARGS__);						\
    } while (0)
#endif
#else
#if defined(__GNUC__) && __GNUC__ < 3
#define	cc_conn_log(C, FMT, ARGS...) do { } while (0)
#else
#define	cc_conn_log(C, FMT, ...) do { } while (0)
#endif
#endif

/* handle signal to connection */
void cc_conn_sig_handle(struct ccconn *, enum conn_sig, void *arg, u_int iarg);

/*
 * Mp connection parties
 */
#define	PARTY_STATES							\
	DEF(NULL)		/* 0 created */				\
	DEF(ACTIVE)		/* 1 active */				\
	DEF(ADD_WAIT_CREATE)	/* 2 wait for PARTY_CREATE */		\
	DEF(ADD_WAIT_OK)	/* 3 wait for OK for ADD.request */	\
	DEF(ADD_WAIT_ACK)	/* 4 wait for ADD.ack/rej */		\
	DEF(DROP_WAIT_OK)	/* 5 wait for OK for DROP.request */	\
	DEF(DROP_WAIT_ACK)	/* 6 wait for DROP.ack */		\
	DEF(WAIT_DESTROY)	/* 7 wait for destroy */		\
	DEF(WAIT_SETUP_COMPL)	/* 8 wait for setup.complete */		\
	DEF(WAIT_DROP_ACK_OK)	/* 9 wait for OK for DROP_ACK.request */\
	DEF(WAIT_SETUP_CONF)	/* 10 wait for setup.confirm */		\
	DEF(ADD_DROP_WAIT_OK)	/* 11 wait for ok to DROP.request */	\
	DEF(ADD_DROPACK_WAIT_OK)/* 12 wait for ok to DROP_ACK.req */

enum party_state {
#define	DEF(N)	PARTY_##N,
PARTY_STATES
#undef DEF
};

struct ccparty {
	struct ccconn		*conn;	/* owner */
	LIST_ENTRY(ccparty)	link;
	enum party_state	state;
	struct uni_ie_called	called;
	struct uni_ie_epref	epref;
};

/* set party to new state */
void cc_party_set_state(struct ccparty *, enum party_state);

/* return string for state */
const char *cc_party_state2str(u_int);

/* create new party */
struct ccparty *cc_party_create(struct ccconn *, u_int ident, u_int flag);

/* log on a party */
#ifdef CCATM_DEBUG
#if defined(__GNUC__) && __GNUC__ < 3
#define	cc_party_log(P, FMT, ARGS...) do {				\
	(P)->conn->cc->funcs->log("%s (conn=%p, party=%p): " FMT,	\
	    __FUNCTION__, (P)->conn, (P) , ## ARGS);			\
    } while (0)
#else
#define	cc_party_log(P, FMT, ...) do {					\
	(P)->conn->cc->funcs->log("%s (conn=%p, party=%p): " FMT,	\
	__func__, (P)->conn, (P), __VA_ARGS__);				\
    } while (0)
#endif
#else
#if defined(__GNUC__) && __GNUC__ < 3
#define	cc_party_log(P, FMT, ARGS...) do { } while (0)
#else
#define	cc_party_log(P, FMT, ...) do { } while (0)
#endif
#endif

/*
 * This is kind of a user socket, i.e. the entity managed towards the
 * upper layer.
 */
#define USER_STATES							\
	DEF(USER_NULL)		/* U0 none */				\
	DEF(USER_OUT_PREPARING)	/* U1 process set/query requests */	\
	DEF(USER_OUT_WAIT_OK)	/* U2 wait for OK to setup */		\
	DEF(USER_OUT_WAIT_CONF)	/* U3 wait for SETUP.confirm */		\
	DEF(USER_ACTIVE)	/* U4 A8-9-10/U10 */			\
	DEF(USER_REL_WAIT)	/* U5 wait for release to compl */	\
	DEF(USER_IN_PREPARING)	/* U6 set SAP */			\
	DEF(USER_IN_WAITING)	/* U7 wait and dispatch */		\
	DEF(USER_IN_ARRIVED)	/* U8 waiting for rej/acc */		\
	DEF(USER_IN_WAIT_REJ)	/* U9 wait for rejecting */		\
	DEF(USER_IN_WAIT_ACC)	/* U10 wait for accepting */		\
	DEF(USER_IN_ACCEPTING)	/* U11 wait for SETUP_complete */	\
	DEF(USER_REL_WAIT_SCOMP)/* U12 wait for SETUP_complete */	\
	DEF(USER_REL_WAIT_SCONF)/* U13 wait for SETUP.confirm */	\
	DEF(USER_REL_WAIT_CONF)	/* U14 wait for confirm */		\
	DEF(USER_REL_WAIT_CONN)	/* U15 wait for CONN_OK */

enum user_state {
#define	DEF(N) N,
USER_STATES
#undef DEF
};

#define	USER_SIGS						\
	DEF(PREPARE_OUTGOING)		/* U */			\
	DEF(CONNECT_OUTGOING)		/* U msg */		\
	DEF(PREPARE_INCOMING)		/* U msg */		\
	DEF(WAIT_ON_INCOMING)		/* U msg */		\
	DEF(REJECT_INCOMING)		/* U msg */		\
	DEF(ACCEPT_INCOMING)		/* U msg */		\
	DEF(CALL_RELEASE)		/* U msg */		\
	DEF(ADD_PARTY)			/* U msg */		\
	DEF(DROP_PARTY)			/* U msg */		\
	DEF(QUERY_ATTR)			/* U msg */		\
	DEF(QUERY_ATTR_X)		/* U msg */		\
	DEF(SET_ATTR)			/* U msg */		\
	DEF(SET_ATTR_X)			/* U msg */		\
	DEF(QUERY_STATE)		/* U */			\
	DEF(GET_LOCAL_PORT_INFO)	/* U msg */		\
	DEF(ABORT_CONNECTION)		/* U msg */		\
								\
	DEF(CONNECT_OUTGOING_OK)	/* */			\
	DEF(CONNECT_OUTGOING_ERR)	/* reason */		\
	DEF(SETUP_CONFIRM)		/* */			\
	DEF(SETUP_IND)			/* */			\
	DEF(REJECT_OK)			/* */			\
	DEF(REJECT_ERR)			/* reason */		\
	DEF(ACCEPT_OK)			/* */			\
	DEF(ACCEPT_ERR)			/* reason */		\
	DEF(ACCEPTING)			/* */			\
	DEF(SETUP_COMPL)		/* */			\
	DEF(RELEASE_CONFIRM)		/* */			\
	DEF(RELEASE_ERR)		/* reason */		\
	DEF(ADD_PARTY_ERR)		/* reason */		\
	DEF(ADD_PARTY_OK)		/* */			\
	DEF(ADD_PARTY_ACK)		/* leaf-ident */	\
	DEF(ADD_PARTY_REJ)		/* leaf-ident */	\
	DEF(DROP_PARTY_ERR)		/* reason */		\
	DEF(DROP_PARTY_OK)		/* */			\
	DEF(DROP_PARTY_IND)		/* leaf-ident */	\


enum user_sig {
#define	DEF(NAME)	USER_SIG_##NAME,
USER_SIGS
#undef DEF
};
extern const char *const cc_user_sigtab[];

struct ccuser {
	LIST_ENTRY(ccuser) 	node_link;	/* link in list of node */
	enum user_state		state;		/* type of this instance */
	struct ccdata		*cc;		/* the node */
	void			*uarg;		/* the hook (if any) */
	char			name[ATM_EPNAMSIZ];
	enum {
		USER_P2P,
		USER_ROOT,
		USER_LEAF
	}			config;		/* configuration */

	struct uni_sap		*sap;		/* listening SAP */
	u_int			queue_max;	/* maximum queue size */
	u_int			queue_act;	/* actual queue size */
	TAILQ_HEAD(,ccconn)	connq;		/* pending connections */
	struct ccconn		*accepted;
	struct uni_ie_cause	cause[2];	/* cause from connection */
	u_int			aborted;
};

/* set user to new state */
void cc_user_set_state(struct ccuser *, enum user_state);

/* return string for state */
const char *cc_user_state2str(u_int);

/* log on a user */
#ifdef CCATM_DEBUG
#if defined(__GNUC__) && __GNUC__ < 3
#define	cc_user_log(U, FMT, ARGS...) do {				\
	(U)->cc->funcs->log("%s (user=%p): " FMT, __FUNCTION__,		\
	    (U) , ## ARGS);						\
    } while (0)
#else
#define	cc_user_log(U, FMT, ...) do {					\
	(U)->cc->funcs->log("%s (user=%p): " FMT, __func__,		\
	    (U), __VA_ARGS__);						\
    } while (0)
#endif
#else
#if defined(__GNUC__) && __GNUC__ < 3
#define	cc_user_log(U, FMT, ARGS...) do { } while (0)
#else
#define	cc_user_log(U, FMT, ...) do { } while (0)
#endif
#endif

/* Handle a signal to this user */
void cc_user_sig_handle(struct ccuser *, enum user_sig, void *, u_int);

/*
 * Addresses
 */
struct ccaddr {
	TAILQ_ENTRY(ccaddr) port_link;
	struct uni_addr	addr;
};

/* signal to connection */
int cc_conn_sig(struct ccconn *, enum conn_sig, void *arg);

/* signal with message to connection */
int cc_conn_sig_msg(struct ccconn *, enum conn_sig, struct uni_msg *);
int cc_conn_sig_msg_nodef(struct ccconn *, enum conn_sig, struct uni_msg *);

/* response signal to connection */
int cc_conn_resp(struct ccconn *, enum conn_sig, u_int, u_int, u_int);

/* flush all signals to a given connection */
void cc_conn_sig_flush(struct ccconn *);

/* Queue a signal to this user */
int cc_user_sig(struct ccuser *, enum user_sig, void *, u_int);

/* Queue a signal with message to this user */
int cc_user_sig_msg(struct ccuser *, enum user_sig, struct uni_msg *);

/* Flush all signals to a given user */
void cc_user_sig_flush(struct ccuser *);

/* flush all signals */
void cc_sig_flush_all(struct ccdata *);
