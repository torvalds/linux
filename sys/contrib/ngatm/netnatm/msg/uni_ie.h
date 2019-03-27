/* This file was created automatically
 * Source file: $Begemot: libunimsg/atm/msg/ie.def,v 1.3 2003/09/19 11:58:15 hbb Exp $
 * $FreeBSD$
 */

#ifndef _NETNATM_MSG_UNI_IE_H_
#define _NETNATM_MSG_UNI_IE_H_

union uni_ieall {
	struct uni_iehdr h;
	struct uni_ie_cause cause;
	struct uni_ie_callstate callstate;
	struct uni_ie_facility facility;
	struct uni_ie_notify notify;
	struct uni_ie_eetd eetd;
	struct uni_ie_conned conned;
	struct uni_ie_connedsub connedsub;
	struct uni_ie_epref epref;
	struct uni_ie_epstate epstate;
	struct uni_ie_aal aal;
	struct uni_ie_traffic traffic;
	struct uni_ie_connid connid;
	struct uni_ie_qos qos;
	struct uni_ie_bhli bhli;
	struct uni_ie_bearer bearer;
	struct uni_ie_blli blli;
	struct uni_ie_lshift lshift;
	struct uni_ie_nlshift nlshift;
	struct uni_ie_scompl scompl;
	struct uni_ie_repeat repeat;
	struct uni_ie_calling calling;
	struct uni_ie_callingsub callingsub;
	struct uni_ie_called called;
	struct uni_ie_calledsub calledsub;
	struct uni_ie_tns tns;
	struct uni_ie_restart restart;
	struct uni_ie_uu uu;
	struct uni_ie_git git;
	struct uni_ie_mintraffic mintraffic;
	struct uni_ie_atraffic atraffic;
	struct uni_ie_abrsetup abrsetup;
	struct uni_ie_report report;
	struct uni_ie_called_soft called_soft;
	struct uni_ie_crankback crankback;
	struct uni_ie_dtl dtl;
	struct uni_ie_calling_soft calling_soft;
	struct uni_ie_abradd abradd;
	struct uni_ie_lij_callid lij_callid;
	struct uni_ie_lij_param lij_param;
	struct uni_ie_lij_seqno lij_seqno;
	struct uni_ie_cscope cscope;
	struct uni_ie_exqos exqos;
	struct uni_ie_mdcr mdcr;
	struct uni_ie_unrec unrec;
};

#endif
