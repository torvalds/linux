/* This file was created automatically
 * Source file: $Begemot: libunimsg/atm/msg/msg.def,v 1.3 2003/09/19 11:58:15 hbb Exp $
 * $FreeBSD$
 */

#include <netnatm/msg/unistruct.h>
#include <netnatm/sig/unimsgcpy.h>

void
copy_msg_alerting(struct uni_alerting *src, struct uni_alerting *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->connid))
		dst->connid = src->connid;
	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->uu))
		dst->uu = src->uu;
	if(IE_ISGOOD(src->report))
		dst->report = src->report;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_call_proc(struct uni_call_proc *src, struct uni_call_proc *dst)
{
	if(IE_ISGOOD(src->connid))
		dst->connid = src->connid;
	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_connect(struct uni_connect *src, struct uni_connect *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->aal))
		dst->aal = src->aal;
	if(IE_ISGOOD(src->blli))
		dst->blli = src->blli;
	if(IE_ISGOOD(src->connid))
		dst->connid = src->connid;
	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	if(IE_ISGOOD(src->conned))
		dst->conned = src->conned;
	if(IE_ISGOOD(src->connedsub))
		dst->connedsub = src->connedsub;
	if(IE_ISGOOD(src->eetd))
		dst->eetd = src->eetd;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->uu))
		dst->uu = src->uu;
	if(IE_ISGOOD(src->traffic))
		dst->traffic = src->traffic;
	if(IE_ISGOOD(src->exqos))
		dst->exqos = src->exqos;
	if(IE_ISGOOD(src->facility))
		dst->facility = src->facility;
	if(IE_ISGOOD(src->abrsetup))
		dst->abrsetup = src->abrsetup;
	if(IE_ISGOOD(src->abradd))
		dst->abradd = src->abradd;
	if(IE_ISGOOD(src->called_soft))
		dst->called_soft = src->called_soft;
	if(IE_ISGOOD(src->report))
		dst->report = src->report;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_connect_ack(struct uni_connect_ack *src, struct uni_connect_ack *dst)
{
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_release(struct uni_release *src, struct uni_release *dst)
{
	u_int s, d;

	for(s = d = 0; s < 2; s++)
		if(IE_ISGOOD(src->cause[s]))
			dst->cause[d++] = src->cause[s];
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->uu))
		dst->uu = src->uu;
	if(IE_ISGOOD(src->facility))
		dst->facility = src->facility;
	if(IE_ISGOOD(src->crankback))
		dst->crankback = src->crankback;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_release_compl(struct uni_release_compl *src, struct uni_release_compl *dst)
{
	u_int s, d;

	for(s = d = 0; s < 2; s++)
		if(IE_ISGOOD(src->cause[s]))
			dst->cause[d++] = src->cause[s];
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->uu))
		dst->uu = src->uu;
	if(IE_ISGOOD(src->crankback))
		dst->crankback = src->crankback;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_setup(struct uni_setup *src, struct uni_setup *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->aal))
		dst->aal = src->aal;
	if(IE_ISGOOD(src->traffic))
		dst->traffic = src->traffic;
	if(IE_ISGOOD(src->bearer))
		dst->bearer = src->bearer;
	if(IE_ISGOOD(src->bhli))
		dst->bhli = src->bhli;
	if(IE_ISGOOD(src->blli_repeat))
		dst->blli_repeat = src->blli_repeat;
	for(s = d = 0; s < UNI_NUM_IE_BLLI; s++)
		if(IE_ISGOOD(src->blli[s]))
			dst->blli[d++] = src->blli[s];
	if(IE_ISGOOD(src->called))
		dst->called = src->called;
	for(s = d = 0; s < UNI_NUM_IE_CALLEDSUB; s++)
		if(IE_ISGOOD(src->calledsub[s]))
			dst->calledsub[d++] = src->calledsub[s];
	if(IE_ISGOOD(src->calling))
		dst->calling = src->calling;
	for(s = d = 0; s < UNI_NUM_IE_CALLINGSUB; s++)
		if(IE_ISGOOD(src->callingsub[s]))
			dst->callingsub[d++] = src->callingsub[s];
	if(IE_ISGOOD(src->connid))
		dst->connid = src->connid;
	if(IE_ISGOOD(src->qos))
		dst->qos = src->qos;
	if(IE_ISGOOD(src->eetd))
		dst->eetd = src->eetd;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	if(IE_ISGOOD(src->scompl))
		dst->scompl = src->scompl;
	for(s = d = 0; s < UNI_NUM_IE_TNS; s++)
		if(IE_ISGOOD(src->tns[s]))
			dst->tns[d++] = src->tns[s];
	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->atraffic))
		dst->atraffic = src->atraffic;
	if(IE_ISGOOD(src->mintraffic))
		dst->mintraffic = src->mintraffic;
	if(IE_ISGOOD(src->uu))
		dst->uu = src->uu;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->lij_callid))
		dst->lij_callid = src->lij_callid;
	if(IE_ISGOOD(src->lij_param))
		dst->lij_param = src->lij_param;
	if(IE_ISGOOD(src->lij_seqno))
		dst->lij_seqno = src->lij_seqno;
	if(IE_ISGOOD(src->exqos))
		dst->exqos = src->exqos;
	if(IE_ISGOOD(src->abrsetup))
		dst->abrsetup = src->abrsetup;
	if(IE_ISGOOD(src->abradd))
		dst->abradd = src->abradd;
	if(IE_ISGOOD(src->cscope))
		dst->cscope = src->cscope;
	if(IE_ISGOOD(src->calling_soft))
		dst->calling_soft = src->calling_soft;
	if(IE_ISGOOD(src->called_soft))
		dst->called_soft = src->called_soft;
	if(IE_ISGOOD(src->dtl_repeat))
		dst->dtl_repeat = src->dtl_repeat;
	for(s = d = 0; s < UNI_NUM_IE_DTL; s++)
		if(IE_ISGOOD(src->dtl[s]))
			dst->dtl[d++] = src->dtl[s];
	if(IE_ISGOOD(src->report))
		dst->report = src->report;
	if(IE_ISGOOD(src->mdcr))
		dst->mdcr = src->mdcr;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_status(struct uni_status *src, struct uni_status *dst)
{
	if(IE_ISGOOD(src->callstate))
		dst->callstate = src->callstate;
	if(IE_ISGOOD(src->cause))
		dst->cause = src->cause;
	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->epstate))
		dst->epstate = src->epstate;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_status_enq(struct uni_status_enq *src, struct uni_status_enq *dst)
{
	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_notify(struct uni_notify *src, struct uni_notify *dst)
{
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_restart(struct uni_restart *src, struct uni_restart *dst)
{
	if(IE_ISGOOD(src->connid))
		dst->connid = src->connid;
	if(IE_ISGOOD(src->restart))
		dst->restart = src->restart;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_restart_ack(struct uni_restart_ack *src, struct uni_restart_ack *dst)
{
	if(IE_ISGOOD(src->connid))
		dst->connid = src->connid;
	if(IE_ISGOOD(src->restart))
		dst->restart = src->restart;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_add_party(struct uni_add_party *src, struct uni_add_party *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->aal))
		dst->aal = src->aal;
	if(IE_ISGOOD(src->bhli))
		dst->bhli = src->bhli;
	if(IE_ISGOOD(src->blli))
		dst->blli = src->blli;
	if(IE_ISGOOD(src->called))
		dst->called = src->called;
	for(s = d = 0; s < UNI_NUM_IE_CALLEDSUB; s++)
		if(IE_ISGOOD(src->calledsub[s]))
			dst->calledsub[d++] = src->calledsub[s];
	if(IE_ISGOOD(src->calling))
		dst->calling = src->calling;
	for(s = d = 0; s < UNI_NUM_IE_CALLINGSUB; s++)
		if(IE_ISGOOD(src->callingsub[s]))
			dst->callingsub[d++] = src->callingsub[s];
	if(IE_ISGOOD(src->scompl))
		dst->scompl = src->scompl;
	for(s = d = 0; s < UNI_NUM_IE_TNS; s++)
		if(IE_ISGOOD(src->tns[s]))
			dst->tns[d++] = src->tns[s];
	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	if(IE_ISGOOD(src->eetd))
		dst->eetd = src->eetd;
	if(IE_ISGOOD(src->uu))
		dst->uu = src->uu;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->lij_seqno))
		dst->lij_seqno = src->lij_seqno;
	if(IE_ISGOOD(src->calling_soft))
		dst->calling_soft = src->calling_soft;
	if(IE_ISGOOD(src->called_soft))
		dst->called_soft = src->called_soft;
	if(IE_ISGOOD(src->dtl_repeat))
		dst->dtl_repeat = src->dtl_repeat;
	for(s = d = 0; s < UNI_NUM_IE_DTL; s++)
		if(IE_ISGOOD(src->dtl[s]))
			dst->dtl[d++] = src->dtl[s];
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_add_party_ack(struct uni_add_party_ack *src, struct uni_add_party_ack *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->aal))
		dst->aal = src->aal;
	if(IE_ISGOOD(src->blli))
		dst->blli = src->blli;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	if(IE_ISGOOD(src->eetd))
		dst->eetd = src->eetd;
	if(IE_ISGOOD(src->conned))
		dst->conned = src->conned;
	if(IE_ISGOOD(src->connedsub))
		dst->connedsub = src->connedsub;
	if(IE_ISGOOD(src->uu))
		dst->uu = src->uu;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->called_soft))
		dst->called_soft = src->called_soft;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_party_alerting(struct uni_party_alerting *src, struct uni_party_alerting *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	if(IE_ISGOOD(src->uu))
		dst->uu = src->uu;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_add_party_rej(struct uni_add_party_rej *src, struct uni_add_party_rej *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->cause))
		dst->cause = src->cause;
	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->uu))
		dst->uu = src->uu;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->crankback))
		dst->crankback = src->crankback;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_drop_party(struct uni_drop_party *src, struct uni_drop_party *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->cause))
		dst->cause = src->cause;
	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	if(IE_ISGOOD(src->uu))
		dst->uu = src->uu;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_drop_party_ack(struct uni_drop_party_ack *src, struct uni_drop_party_ack *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->cause))
		dst->cause = src->cause;
	if(IE_ISGOOD(src->uu))
		dst->uu = src->uu;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_leaf_setup_req(struct uni_leaf_setup_req *src, struct uni_leaf_setup_req *dst)
{
	u_int s, d;

	for(s = d = 0; s < UNI_NUM_IE_TNS; s++)
		if(IE_ISGOOD(src->tns[s]))
			dst->tns[d++] = src->tns[s];
	if(IE_ISGOOD(src->calling))
		dst->calling = src->calling;
	for(s = d = 0; s < UNI_NUM_IE_CALLINGSUB; s++)
		if(IE_ISGOOD(src->callingsub[s]))
			dst->callingsub[d++] = src->callingsub[s];
	if(IE_ISGOOD(src->called))
		dst->called = src->called;
	for(s = d = 0; s < UNI_NUM_IE_CALLEDSUB; s++)
		if(IE_ISGOOD(src->calledsub[s]))
			dst->calledsub[d++] = src->calledsub[s];
	if(IE_ISGOOD(src->lij_callid))
		dst->lij_callid = src->lij_callid;
	if(IE_ISGOOD(src->lij_seqno))
		dst->lij_seqno = src->lij_seqno;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_leaf_setup_fail(struct uni_leaf_setup_fail *src, struct uni_leaf_setup_fail *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->cause))
		dst->cause = src->cause;
	if(IE_ISGOOD(src->called))
		dst->called = src->called;
	if(IE_ISGOOD(src->calledsub))
		dst->calledsub = src->calledsub;
	if(IE_ISGOOD(src->lij_seqno))
		dst->lij_seqno = src->lij_seqno;
	for(s = d = 0; s < UNI_NUM_IE_TNS; s++)
		if(IE_ISGOOD(src->tns[s]))
			dst->tns[d++] = src->tns[s];
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_cobisetup(struct uni_cobisetup *src, struct uni_cobisetup *dst)
{
	if(IE_ISGOOD(src->facility))
		dst->facility = src->facility;
	if(IE_ISGOOD(src->called))
		dst->called = src->called;
	if(IE_ISGOOD(src->calledsub))
		dst->calledsub = src->calledsub;
	if(IE_ISGOOD(src->calling))
		dst->calling = src->calling;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_facility(struct uni_facility *src, struct uni_facility *dst)
{
	if(IE_ISGOOD(src->facility))
		dst->facility = src->facility;
	if(IE_ISGOOD(src->called))
		dst->called = src->called;
	if(IE_ISGOOD(src->calledsub))
		dst->calledsub = src->calledsub;
	if(IE_ISGOOD(src->calling))
		dst->calling = src->calling;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_modify_req(struct uni_modify_req *src, struct uni_modify_req *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->traffic))
		dst->traffic = src->traffic;
	if(IE_ISGOOD(src->atraffic))
		dst->atraffic = src->atraffic;
	if(IE_ISGOOD(src->mintraffic))
		dst->mintraffic = src->mintraffic;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_modify_ack(struct uni_modify_ack *src, struct uni_modify_ack *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->report))
		dst->report = src->report;
	if(IE_ISGOOD(src->traffic))
		dst->traffic = src->traffic;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_modify_rej(struct uni_modify_rej *src, struct uni_modify_rej *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->cause))
		dst->cause = src->cause;
	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_conn_avail(struct uni_conn_avail *src, struct uni_conn_avail *dst)
{
	u_int s, d;

	if(IE_ISGOOD(src->notify))
		dst->notify = src->notify;
	for(s = d = 0; s < UNI_NUM_IE_GIT; s++)
		if(IE_ISGOOD(src->git[s]))
			dst->git[d++] = src->git[s];
	if(IE_ISGOOD(src->report))
		dst->report = src->report;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}

void
copy_msg_unknown(struct uni_unknown *src, struct uni_unknown *dst)
{
	if(IE_ISGOOD(src->epref))
		dst->epref = src->epref;
	if(IE_ISGOOD(src->unrec))
		dst->unrec = src->unrec;
}
