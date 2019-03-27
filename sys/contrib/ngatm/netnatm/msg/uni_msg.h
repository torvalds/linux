/* This file was created automatically
 * Source file: $Begemot: libunimsg/atm/msg/msg.def,v 1.3 2003/09/19 11:58:15 hbb Exp $
 * $FreeBSD$
 */

#ifndef _NETNATM_MSG_UNI_MSG_H_
#define _NETNATM_MSG_UNI_MSG_H_

struct uni_alerting {
	struct uni_msghdr	hdr;
	struct uni_ie_connid	connid;
	struct uni_ie_epref	epref;
	struct uni_ie_notify	notify;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_uu	uu;
	struct uni_ie_report	report;
	struct uni_ie_unrec	unrec;
};

struct uni_call_proc {
	struct uni_msghdr	hdr;
	struct uni_ie_connid	connid;
	struct uni_ie_epref	epref;
	struct uni_ie_notify	notify;
	struct uni_ie_unrec	unrec;
};

struct uni_connect {
	struct uni_msghdr	hdr;
	struct uni_ie_aal	aal;
	struct uni_ie_blli	blli;
	struct uni_ie_connid	connid;
	struct uni_ie_epref	epref;
	struct uni_ie_notify	notify;
	struct uni_ie_conned	conned;
	struct uni_ie_connedsub	connedsub;
	struct uni_ie_eetd	eetd;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_uu	uu;
	struct uni_ie_traffic	traffic;
	struct uni_ie_exqos	exqos;
	struct uni_ie_facility	facility;
	struct uni_ie_abrsetup	abrsetup;
	struct uni_ie_abradd	abradd;
	struct uni_ie_called_soft	called_soft;
	struct uni_ie_report	report;
	struct uni_ie_unrec	unrec;
};

struct uni_connect_ack {
	struct uni_msghdr	hdr;
	struct uni_ie_notify	notify;
	struct uni_ie_unrec	unrec;
};

struct uni_release {
	struct uni_msghdr	hdr;
	struct uni_ie_cause	cause[2];
	struct uni_ie_notify	notify;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_uu	uu;
	struct uni_ie_facility	facility;
	struct uni_ie_crankback	crankback;
	struct uni_ie_unrec	unrec;
};

struct uni_release_compl {
	struct uni_msghdr	hdr;
	struct uni_ie_cause	cause[2];
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_uu	uu;
	struct uni_ie_crankback	crankback;
	struct uni_ie_unrec	unrec;
};

struct uni_setup {
	struct uni_msghdr	hdr;
	struct uni_ie_aal	aal;
	struct uni_ie_traffic	traffic;
	struct uni_ie_bearer	bearer;
	struct uni_ie_bhli	bhli;
	struct uni_ie_repeat	blli_repeat;
	struct uni_ie_blli	blli[UNI_NUM_IE_BLLI];
	struct uni_ie_called	called;
	struct uni_ie_calledsub	calledsub[UNI_NUM_IE_CALLEDSUB];
	struct uni_ie_calling	calling;
	struct uni_ie_callingsub	callingsub[UNI_NUM_IE_CALLINGSUB];
	struct uni_ie_connid	connid;
	struct uni_ie_qos	qos;
	struct uni_ie_eetd	eetd;
	struct uni_ie_notify	notify;
	struct uni_ie_scompl	scompl;
	struct uni_ie_tns	tns[UNI_NUM_IE_TNS];
	struct uni_ie_epref	epref;
	struct uni_ie_atraffic	atraffic;
	struct uni_ie_mintraffic	mintraffic;
	struct uni_ie_uu	uu;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_lij_callid	lij_callid;
	struct uni_ie_lij_param	lij_param;
	struct uni_ie_lij_seqno	lij_seqno;
	struct uni_ie_exqos	exqos;
	struct uni_ie_abrsetup	abrsetup;
	struct uni_ie_abradd	abradd;
	struct uni_ie_cscope	cscope;
	struct uni_ie_calling_soft	calling_soft;
	struct uni_ie_called_soft	called_soft;
	struct uni_ie_repeat	dtl_repeat;
	struct uni_ie_dtl	dtl[UNI_NUM_IE_DTL];
	struct uni_ie_report	report;
	struct uni_ie_mdcr	mdcr;
	struct uni_ie_unrec	unrec;
};

struct uni_status {
	struct uni_msghdr	hdr;
	struct uni_ie_callstate	callstate;
	struct uni_ie_cause	cause;
	struct uni_ie_epref	epref;
	struct uni_ie_epstate	epstate;
	struct uni_ie_unrec	unrec;
};

struct uni_status_enq {
	struct uni_msghdr	hdr;
	struct uni_ie_epref	epref;
	struct uni_ie_unrec	unrec;
};

struct uni_notify {
	struct uni_msghdr	hdr;
	struct uni_ie_notify	notify;
	struct uni_ie_epref	epref;
	struct uni_ie_unrec	unrec;
};

struct uni_restart {
	struct uni_msghdr	hdr;
	struct uni_ie_connid	connid;
	struct uni_ie_restart	restart;
	struct uni_ie_unrec	unrec;
};

struct uni_restart_ack {
	struct uni_msghdr	hdr;
	struct uni_ie_connid	connid;
	struct uni_ie_restart	restart;
	struct uni_ie_unrec	unrec;
};

struct uni_add_party {
	struct uni_msghdr	hdr;
	struct uni_ie_aal	aal;
	struct uni_ie_bhli	bhli;
	struct uni_ie_blli	blli;
	struct uni_ie_called	called;
	struct uni_ie_calledsub	calledsub[UNI_NUM_IE_CALLEDSUB];
	struct uni_ie_calling	calling;
	struct uni_ie_callingsub	callingsub[UNI_NUM_IE_CALLINGSUB];
	struct uni_ie_scompl	scompl;
	struct uni_ie_tns	tns[UNI_NUM_IE_TNS];
	struct uni_ie_epref	epref;
	struct uni_ie_notify	notify;
	struct uni_ie_eetd	eetd;
	struct uni_ie_uu	uu;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_lij_seqno	lij_seqno;
	struct uni_ie_calling_soft	calling_soft;
	struct uni_ie_called_soft	called_soft;
	struct uni_ie_repeat	dtl_repeat;
	struct uni_ie_dtl	dtl[UNI_NUM_IE_DTL];
	struct uni_ie_unrec	unrec;
};

struct uni_add_party_ack {
	struct uni_msghdr	hdr;
	struct uni_ie_epref	epref;
	struct uni_ie_aal	aal;
	struct uni_ie_blli	blli;
	struct uni_ie_notify	notify;
	struct uni_ie_eetd	eetd;
	struct uni_ie_conned	conned;
	struct uni_ie_connedsub	connedsub;
	struct uni_ie_uu	uu;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_called_soft	called_soft;
	struct uni_ie_unrec	unrec;
};

struct uni_party_alerting {
	struct uni_msghdr	hdr;
	struct uni_ie_epref	epref;
	struct uni_ie_notify	notify;
	struct uni_ie_uu	uu;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_unrec	unrec;
};

struct uni_add_party_rej {
	struct uni_msghdr	hdr;
	struct uni_ie_cause	cause;
	struct uni_ie_epref	epref;
	struct uni_ie_uu	uu;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_crankback	crankback;
	struct uni_ie_unrec	unrec;
};

struct uni_drop_party {
	struct uni_msghdr	hdr;
	struct uni_ie_cause	cause;
	struct uni_ie_epref	epref;
	struct uni_ie_notify	notify;
	struct uni_ie_uu	uu;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_unrec	unrec;
};

struct uni_drop_party_ack {
	struct uni_msghdr	hdr;
	struct uni_ie_epref	epref;
	struct uni_ie_cause	cause;
	struct uni_ie_uu	uu;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_unrec	unrec;
};

struct uni_leaf_setup_req {
	struct uni_msghdr	hdr;
	struct uni_ie_tns	tns[UNI_NUM_IE_TNS];
	struct uni_ie_calling	calling;
	struct uni_ie_callingsub	callingsub[UNI_NUM_IE_CALLINGSUB];
	struct uni_ie_called	called;
	struct uni_ie_calledsub	calledsub[UNI_NUM_IE_CALLEDSUB];
	struct uni_ie_lij_callid	lij_callid;
	struct uni_ie_lij_seqno	lij_seqno;
	struct uni_ie_unrec	unrec;
};

struct uni_leaf_setup_fail {
	struct uni_msghdr	hdr;
	struct uni_ie_cause	cause;
	struct uni_ie_called	called;
	struct uni_ie_calledsub	calledsub;
	struct uni_ie_lij_seqno	lij_seqno;
	struct uni_ie_tns	tns[UNI_NUM_IE_TNS];
	struct uni_ie_unrec	unrec;
};

struct uni_cobisetup {
	struct uni_msghdr	hdr;
	struct uni_ie_facility	facility;
	struct uni_ie_called	called;
	struct uni_ie_calledsub	calledsub;
	struct uni_ie_calling	calling;
	struct uni_ie_notify	notify;
	struct uni_ie_unrec	unrec;
};

struct uni_facility {
	struct uni_msghdr	hdr;
	struct uni_ie_facility	facility;
	struct uni_ie_called	called;
	struct uni_ie_calledsub	calledsub;
	struct uni_ie_calling	calling;
	struct uni_ie_notify	notify;
	struct uni_ie_unrec	unrec;
};

struct uni_modify_req {
	struct uni_msghdr	hdr;
	struct uni_ie_traffic	traffic;
	struct uni_ie_atraffic	atraffic;
	struct uni_ie_mintraffic	mintraffic;
	struct uni_ie_notify	notify;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_unrec	unrec;
};

struct uni_modify_ack {
	struct uni_msghdr	hdr;
	struct uni_ie_report	report;
	struct uni_ie_traffic	traffic;
	struct uni_ie_notify	notify;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_unrec	unrec;
};

struct uni_modify_rej {
	struct uni_msghdr	hdr;
	struct uni_ie_cause	cause;
	struct uni_ie_notify	notify;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_unrec	unrec;
};

struct uni_conn_avail {
	struct uni_msghdr	hdr;
	struct uni_ie_notify	notify;
	struct uni_ie_git	git[UNI_NUM_IE_GIT];
	struct uni_ie_report	report;
	struct uni_ie_unrec	unrec;
};

struct uni_unknown {
	struct uni_msghdr	hdr;
	struct uni_ie_epref	epref;
	struct uni_ie_unrec	unrec;
};

union uni_msgall {
	struct uni_msghdr	hdr;
	struct uni_alerting	alerting;
	struct uni_call_proc	call_proc;
	struct uni_connect	connect;
	struct uni_connect_ack	connect_ack;	/* !pnni */
	struct uni_release	release;
	struct uni_release_compl	release_compl;
	struct uni_setup	setup;
	struct uni_status	status;
	struct uni_status_enq	status_enq;
	struct uni_notify	notify;
	struct uni_restart	restart;
	struct uni_restart_ack	restart_ack;
	struct uni_add_party	add_party;
	struct uni_add_party_ack	add_party_ack;
	struct uni_party_alerting	party_alerting;
	struct uni_add_party_rej	add_party_rej;
	struct uni_drop_party	drop_party;
	struct uni_drop_party_ack	drop_party_ack;
	struct uni_leaf_setup_req	leaf_setup_req;	/* !pnni */
	struct uni_leaf_setup_fail	leaf_setup_fail;	/* !pnni */
	struct uni_cobisetup	cobisetup;	/* !pnni&&q2932 */
	struct uni_facility	facility;	/* !pnni&&q2932 */
	struct uni_modify_req	modify_req;	/* !pnni */
	struct uni_modify_ack	modify_ack;	/* !pnni */
	struct uni_modify_rej	modify_rej;	/* !pnni */
	struct uni_conn_avail	conn_avail;	/* !pnni */
	struct uni_unknown	unknown;
};

#endif
