/* This file was created automatically
 * Source file: $Begemot: libunimsg/atm/msg/msg.def,v 1.3 2003/09/19 11:58:15 hbb Exp $
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/param.h>

#ifdef _KERNEL
#include <sys/libkern.h>
#else
#include <string.h>
#endif
#include <netnatm/unimsg.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/msg/priv.h>
#include <netnatm/msg/privmsg.c>

static void
print_alerting(struct uni_alerting *msg, struct unicx *cx)
{
	u_int i;

	if(msg->connid.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CONNID, (union uni_ieall *)&msg->connid, cx);
	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->uu.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UU, (union uni_ieall *)&msg->uu, cx);
	if(msg->report.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_REPORT, (union uni_ieall *)&msg->report, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_alerting(struct uni_alerting *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->connid);
	else
		ret |= uni_check_ie(UNI_IE_CONNID, (union uni_ieall *)&m->connid, cx);
	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->uu);
	else
		ret |= uni_check_ie(UNI_IE_UU, (union uni_ieall *)&m->uu, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->report);
	else
		ret |= uni_check_ie(UNI_IE_REPORT, (union uni_ieall *)&m->report, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_alerting(struct uni_msg *msg, struct uni_alerting *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_ALERTING, cx, &mlen))
		return (-2);

	if((p->connid.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CONNID, msg, (union uni_ieall *)&p->connid, cx))
		return (UNI_IE_CONNID);
	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->uu.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UU, msg, (union uni_ieall *)&p->uu, cx))
		return (UNI_IE_UU);
	if((p->report.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_REPORT, msg, (union uni_ieall *)&p->report, cx))
		return (UNI_IE_REPORT);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_alerting(struct uni_alerting *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_CONNID:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->connid.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CONNID, (union uni_ieall *)&out->connid, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UU:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->uu.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UU, (union uni_ieall *)&out->uu, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_REPORT:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->report.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_REPORT, (union uni_ieall *)&out->report, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_alerting = {
	0,
	"alerting",
	(uni_msg_print_f)print_alerting,
	(uni_msg_check_f)check_alerting,
	(uni_msg_encode_f)encode_alerting,
	(uni_msg_decode_f)decode_alerting
};

static void
print_call_proc(struct uni_call_proc *msg, struct unicx *cx)
{
	if(msg->connid.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CONNID, (union uni_ieall *)&msg->connid, cx);
	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_call_proc(struct uni_call_proc *m, struct unicx *cx)
{
	int ret = 0;

	ret |= uni_check_ie(UNI_IE_CONNID, (union uni_ieall *)&m->connid, cx);
	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->notify);
	else
		ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_call_proc(struct uni_msg *msg, struct uni_call_proc *p, struct unicx *cx)
{
	u_int mlen;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_CALL_PROC, cx, &mlen))
		return (-2);

	if((p->connid.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CONNID, msg, (union uni_ieall *)&p->connid, cx))
		return (UNI_IE_CONNID);
	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_call_proc(struct uni_call_proc *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	switch (ie) {

	  case UNI_IE_CONNID:
		out->connid.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CONNID, (union uni_ieall *)&out->connid, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_call_proc = {
	0,
	"call_proc",
	(uni_msg_print_f)print_call_proc,
	(uni_msg_check_f)check_call_proc,
	(uni_msg_encode_f)encode_call_proc,
	(uni_msg_decode_f)decode_call_proc
};

static void
print_connect(struct uni_connect *msg, struct unicx *cx)
{
	u_int i;

	if(msg->aal.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_AAL, (union uni_ieall *)&msg->aal, cx);
	if(msg->blli.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_BLLI, (union uni_ieall *)&msg->blli, cx);
	if(msg->connid.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CONNID, (union uni_ieall *)&msg->connid, cx);
	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	if(msg->conned.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CONNED, (union uni_ieall *)&msg->conned, cx);
	if(msg->connedsub.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CONNEDSUB, (union uni_ieall *)&msg->connedsub, cx);
	if(msg->eetd.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EETD, (union uni_ieall *)&msg->eetd, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->uu.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UU, (union uni_ieall *)&msg->uu, cx);
	if(msg->traffic.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_TRAFFIC, (union uni_ieall *)&msg->traffic, cx);
	if(msg->exqos.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EXQOS, (union uni_ieall *)&msg->exqos, cx);
	if(msg->facility.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_FACILITY, (union uni_ieall *)&msg->facility, cx);
	if(msg->abrsetup.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_ABRSETUP, (union uni_ieall *)&msg->abrsetup, cx);
	if(msg->abradd.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_ABRADD, (union uni_ieall *)&msg->abradd, cx);
	if(msg->called_soft.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLED_SOFT, (union uni_ieall *)&msg->called_soft, cx);
	if(msg->report.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_REPORT, (union uni_ieall *)&msg->report, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_connect(struct uni_connect *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_AAL, (union uni_ieall *)&m->aal, cx);
	ret |= uni_check_ie(UNI_IE_BLLI, (union uni_ieall *)&m->blli, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->connid);
	else
		ret |= uni_check_ie(UNI_IE_CONNID, (union uni_ieall *)&m->connid, cx);
	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	ret |= uni_check_ie(UNI_IE_CONNED, (union uni_ieall *)&m->conned, cx);
	ret |= uni_check_ie(UNI_IE_CONNEDSUB, (union uni_ieall *)&m->connedsub, cx);
	ret |= uni_check_ie(UNI_IE_EETD, (union uni_ieall *)&m->eetd, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->uu);
	else
		ret |= uni_check_ie(UNI_IE_UU, (union uni_ieall *)&m->uu, cx);
	ret |= uni_check_ie(UNI_IE_TRAFFIC, (union uni_ieall *)&m->traffic, cx);
	ret |= uni_check_ie(UNI_IE_EXQOS, (union uni_ieall *)&m->exqos, cx);
	if(!(cx->q2932))
		ret |= IE_ISPRESENT(m->facility);
	else
		ret |= uni_check_ie(UNI_IE_FACILITY, (union uni_ieall *)&m->facility, cx);
	ret |= uni_check_ie(UNI_IE_ABRSETUP, (union uni_ieall *)&m->abrsetup, cx);
	ret |= uni_check_ie(UNI_IE_ABRADD, (union uni_ieall *)&m->abradd, cx);
	if(!(cx->pnni))
		ret |= IE_ISPRESENT(m->called_soft);
	else
		ret |= uni_check_ie(UNI_IE_CALLED_SOFT, (union uni_ieall *)&m->called_soft, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->report);
	else
		ret |= uni_check_ie(UNI_IE_REPORT, (union uni_ieall *)&m->report, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_connect(struct uni_msg *msg, struct uni_connect *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_CONNECT, cx, &mlen))
		return (-2);

	if((p->aal.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_AAL, msg, (union uni_ieall *)&p->aal, cx))
		return (UNI_IE_AAL);
	if((p->blli.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_BLLI, msg, (union uni_ieall *)&p->blli, cx))
		return (UNI_IE_BLLI);
	if((p->connid.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CONNID, msg, (union uni_ieall *)&p->connid, cx))
		return (UNI_IE_CONNID);
	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	if((p->conned.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CONNED, msg, (union uni_ieall *)&p->conned, cx))
		return (UNI_IE_CONNED);
	if((p->connedsub.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CONNEDSUB, msg, (union uni_ieall *)&p->connedsub, cx))
		return (UNI_IE_CONNEDSUB);
	if((p->eetd.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EETD, msg, (union uni_ieall *)&p->eetd, cx))
		return (UNI_IE_EETD);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->uu.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UU, msg, (union uni_ieall *)&p->uu, cx))
		return (UNI_IE_UU);
	if((p->traffic.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_TRAFFIC, msg, (union uni_ieall *)&p->traffic, cx))
		return (UNI_IE_TRAFFIC);
	if((p->exqos.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EXQOS, msg, (union uni_ieall *)&p->exqos, cx))
		return (UNI_IE_EXQOS);
	if((p->facility.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_FACILITY, msg, (union uni_ieall *)&p->facility, cx))
		return (UNI_IE_FACILITY);
	if((p->abrsetup.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_ABRSETUP, msg, (union uni_ieall *)&p->abrsetup, cx))
		return (UNI_IE_ABRSETUP);
	if((p->abradd.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_ABRADD, msg, (union uni_ieall *)&p->abradd, cx))
		return (UNI_IE_ABRADD);
	if((p->called_soft.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLED_SOFT, msg, (union uni_ieall *)&p->called_soft, cx))
		return (UNI_IE_CALLED_SOFT);
	if((p->report.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_REPORT, msg, (union uni_ieall *)&p->report, cx))
		return (UNI_IE_REPORT);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_connect(struct uni_connect *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_AAL:
		out->aal.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_AAL, (union uni_ieall *)&out->aal, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_BLLI:
		out->blli.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_BLLI, (union uni_ieall *)&out->blli, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CONNID:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->connid.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CONNID, (union uni_ieall *)&out->connid, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CONNED:
		out->conned.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CONNED, (union uni_ieall *)&out->conned, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CONNEDSUB:
		out->connedsub.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CONNEDSUB, (union uni_ieall *)&out->connedsub, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EETD:
		out->eetd.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EETD, (union uni_ieall *)&out->eetd, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UU:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->uu.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UU, (union uni_ieall *)&out->uu, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_TRAFFIC:
		out->traffic.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_TRAFFIC, (union uni_ieall *)&out->traffic, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EXQOS:
		out->exqos.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EXQOS, (union uni_ieall *)&out->exqos, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_FACILITY:
		if (!(cx->q2932))
			return (DEC_ILL);
		out->facility.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_FACILITY, (union uni_ieall *)&out->facility, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_ABRSETUP:
		out->abrsetup.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_ABRSETUP, (union uni_ieall *)&out->abrsetup, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_ABRADD:
		out->abradd.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_ABRADD, (union uni_ieall *)&out->abradd, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLED_SOFT:
		if (!(cx->pnni))
			return (DEC_ILL);
		out->called_soft.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLED_SOFT, (union uni_ieall *)&out->called_soft, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_REPORT:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->report.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_REPORT, (union uni_ieall *)&out->report, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_connect = {
	0,
	"connect",
	(uni_msg_print_f)print_connect,
	(uni_msg_check_f)check_connect,
	(uni_msg_encode_f)encode_connect,
	(uni_msg_decode_f)decode_connect
};

static void
print_connect_ack(struct uni_connect_ack *msg, struct unicx *cx)
{
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_connect_ack(struct uni_connect_ack *m, struct unicx *cx)
{
	int ret = 0;

	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_connect_ack(struct uni_msg *msg, struct uni_connect_ack *p, struct unicx *cx)
{
	u_int mlen;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_CONNECT_ACK, cx, &mlen))
		return (-2);

	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_connect_ack(struct uni_connect_ack *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	switch (ie) {

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_connect_ack = {
	0,
	"connect_ack",
	(uni_msg_print_f)print_connect_ack,
	(uni_msg_check_f)check_connect_ack,
	(uni_msg_encode_f)encode_connect_ack,
	(uni_msg_decode_f)decode_connect_ack
};

static void
print_release(struct uni_release *msg, struct unicx *cx)
{
	u_int i;

	for(i = 0; i < 2; i++)
		if(msg->cause[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_CAUSE, (union uni_ieall *)&msg->cause[i], cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->uu.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UU, (union uni_ieall *)&msg->uu, cx);
	if(msg->facility.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_FACILITY, (union uni_ieall *)&msg->facility, cx);
	if(msg->crankback.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CRANKBACK, (union uni_ieall *)&msg->crankback, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_release(struct uni_release *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	for(i = 0; i < 2 ; i++) {
		ret |= uni_check_ie(UNI_IE_CAUSE, (union uni_ieall *)&m->cause[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->uu);
	else
		ret |= uni_check_ie(UNI_IE_UU, (union uni_ieall *)&m->uu, cx);
	if(!(cx->q2932))
		ret |= IE_ISPRESENT(m->facility);
	else
		ret |= uni_check_ie(UNI_IE_FACILITY, (union uni_ieall *)&m->facility, cx);
	if(!(cx->pnni))
		ret |= IE_ISPRESENT(m->crankback);
	else
		ret |= uni_check_ie(UNI_IE_CRANKBACK, (union uni_ieall *)&m->crankback, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_release(struct uni_msg *msg, struct uni_release *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_RELEASE, cx, &mlen))
		return (-2);

	for(i = 0; i < 2; i++)
		if((p->cause[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_CAUSE, msg, (union uni_ieall *)&p->cause[i], cx))
		return ((i << 16) + UNI_IE_CAUSE);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->uu.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UU, msg, (union uni_ieall *)&p->uu, cx))
		return (UNI_IE_UU);
	if((p->facility.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_FACILITY, msg, (union uni_ieall *)&p->facility, cx))
		return (UNI_IE_FACILITY);
	if((p->crankback.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CRANKBACK, msg, (union uni_ieall *)&p->crankback, cx))
		return (UNI_IE_CRANKBACK);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_release(struct uni_release *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_CAUSE:
		for(i = 0; i < 2; i++)
			if (!IE_ISPRESENT(out->cause[i])) {
				out->cause[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_CAUSE, (union uni_ieall *)&out->cause[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UU:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->uu.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UU, (union uni_ieall *)&out->uu, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_FACILITY:
		if (!(cx->q2932))
			return (DEC_ILL);
		out->facility.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_FACILITY, (union uni_ieall *)&out->facility, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CRANKBACK:
		if (!(cx->pnni))
			return (DEC_ILL);
		out->crankback.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CRANKBACK, (union uni_ieall *)&out->crankback, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_release = {
	0,
	"release",
	(uni_msg_print_f)print_release,
	(uni_msg_check_f)check_release,
	(uni_msg_encode_f)encode_release,
	(uni_msg_decode_f)decode_release
};

static void
print_release_compl(struct uni_release_compl *msg, struct unicx *cx)
{
	u_int i;

	for(i = 0; i < 2; i++)
		if(msg->cause[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_CAUSE, (union uni_ieall *)&msg->cause[i], cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->uu.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UU, (union uni_ieall *)&msg->uu, cx);
	if(msg->crankback.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CRANKBACK, (union uni_ieall *)&msg->crankback, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_release_compl(struct uni_release_compl *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	for(i = 0; i < 2 ; i++) {
		ret |= uni_check_ie(UNI_IE_CAUSE, (union uni_ieall *)&m->cause[i], cx);
	}
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		if(!(!cx->pnni))
			ret |= IE_ISPRESENT(m->git[i]);
		else
			ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->uu);
	else
		ret |= uni_check_ie(UNI_IE_UU, (union uni_ieall *)&m->uu, cx);
	if(!(cx->pnni))
		ret |= IE_ISPRESENT(m->crankback);
	else
		ret |= uni_check_ie(UNI_IE_CRANKBACK, (union uni_ieall *)&m->crankback, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_release_compl(struct uni_msg *msg, struct uni_release_compl *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_RELEASE_COMPL, cx, &mlen))
		return (-2);

	for(i = 0; i < 2; i++)
		if((p->cause[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_CAUSE, msg, (union uni_ieall *)&p->cause[i], cx))
		return ((i << 16) + UNI_IE_CAUSE);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->uu.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UU, msg, (union uni_ieall *)&p->uu, cx))
		return (UNI_IE_UU);
	if((p->crankback.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CRANKBACK, msg, (union uni_ieall *)&p->crankback, cx))
		return (UNI_IE_CRANKBACK);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_release_compl(struct uni_release_compl *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_CAUSE:
		for(i = 0; i < 2; i++)
			if (!IE_ISPRESENT(out->cause[i])) {
				out->cause[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_CAUSE, (union uni_ieall *)&out->cause[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_GIT:
		if (!(!cx->pnni))
			return (DEC_ILL);
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UU:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->uu.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UU, (union uni_ieall *)&out->uu, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CRANKBACK:
		if (!(cx->pnni))
			return (DEC_ILL);
		out->crankback.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CRANKBACK, (union uni_ieall *)&out->crankback, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_release_compl = {
	0,
	"release_compl",
	(uni_msg_print_f)print_release_compl,
	(uni_msg_check_f)check_release_compl,
	(uni_msg_encode_f)encode_release_compl,
	(uni_msg_decode_f)decode_release_compl
};

static void
print_setup(struct uni_setup *msg, struct unicx *cx)
{
	u_int i;

	if(msg->aal.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_AAL, (union uni_ieall *)&msg->aal, cx);
	if(msg->traffic.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_TRAFFIC, (union uni_ieall *)&msg->traffic, cx);
	if(msg->bearer.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_BEARER, (union uni_ieall *)&msg->bearer, cx);
	if(msg->bhli.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_BHLI, (union uni_ieall *)&msg->bhli, cx);
	if(msg->blli_repeat.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_REPEAT, (union uni_ieall *)&msg->blli_repeat, cx);
	for(i = 0; i < UNI_NUM_IE_BLLI; i++)
		if(msg->blli[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_BLLI, (union uni_ieall *)&msg->blli[i], cx);
	if(msg->called.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLED, (union uni_ieall *)&msg->called, cx);
	for(i = 0; i < UNI_NUM_IE_CALLEDSUB; i++)
		if(msg->calledsub[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_CALLEDSUB, (union uni_ieall *)&msg->calledsub[i], cx);
	if(msg->calling.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLING, (union uni_ieall *)&msg->calling, cx);
	for(i = 0; i < UNI_NUM_IE_CALLINGSUB; i++)
		if(msg->callingsub[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_CALLINGSUB, (union uni_ieall *)&msg->callingsub[i], cx);
	if(msg->connid.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CONNID, (union uni_ieall *)&msg->connid, cx);
	if(msg->qos.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_QOS, (union uni_ieall *)&msg->qos, cx);
	if(msg->eetd.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EETD, (union uni_ieall *)&msg->eetd, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	if(msg->scompl.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_SCOMPL, (union uni_ieall *)&msg->scompl, cx);
	for(i = 0; i < UNI_NUM_IE_TNS; i++)
		if(msg->tns[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_TNS, (union uni_ieall *)&msg->tns[i], cx);
	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->atraffic.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_ATRAFFIC, (union uni_ieall *)&msg->atraffic, cx);
	if(msg->mintraffic.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_MINTRAFFIC, (union uni_ieall *)&msg->mintraffic, cx);
	if(msg->uu.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UU, (union uni_ieall *)&msg->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->lij_callid.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_LIJ_CALLID, (union uni_ieall *)&msg->lij_callid, cx);
	if(msg->lij_param.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_LIJ_PARAM, (union uni_ieall *)&msg->lij_param, cx);
	if(msg->lij_seqno.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&msg->lij_seqno, cx);
	if(msg->exqos.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EXQOS, (union uni_ieall *)&msg->exqos, cx);
	if(msg->abrsetup.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_ABRSETUP, (union uni_ieall *)&msg->abrsetup, cx);
	if(msg->abradd.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_ABRADD, (union uni_ieall *)&msg->abradd, cx);
	if(msg->cscope.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CSCOPE, (union uni_ieall *)&msg->cscope, cx);
	if(msg->calling_soft.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLING_SOFT, (union uni_ieall *)&msg->calling_soft, cx);
	if(msg->called_soft.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLED_SOFT, (union uni_ieall *)&msg->called_soft, cx);
	if(msg->dtl_repeat.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_REPEAT, (union uni_ieall *)&msg->dtl_repeat, cx);
	for(i = 0; i < UNI_NUM_IE_DTL; i++)
		if(msg->dtl[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_DTL, (union uni_ieall *)&msg->dtl[i], cx);
	if(msg->report.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_REPORT, (union uni_ieall *)&msg->report, cx);
	if(msg->mdcr.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_MDCR, (union uni_ieall *)&msg->mdcr, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_setup(struct uni_setup *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_AAL, (union uni_ieall *)&m->aal, cx);
	ret |= uni_check_ie(UNI_IE_TRAFFIC, (union uni_ieall *)&m->traffic, cx);
	ret |= uni_check_ie(UNI_IE_BEARER, (union uni_ieall *)&m->bearer, cx);
	ret |= uni_check_ie(UNI_IE_BHLI, (union uni_ieall *)&m->bhli, cx);
	ret |= uni_check_ie(UNI_IE_REPEAT, (union uni_ieall *)&m->blli_repeat, cx);
	for(i = 0; i < UNI_NUM_IE_BLLI ; i++) {
		ret |= uni_check_ie(UNI_IE_BLLI, (union uni_ieall *)&m->blli[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_CALLED, (union uni_ieall *)&m->called, cx);
	for(i = 0; i < UNI_NUM_IE_CALLEDSUB ; i++) {
		ret |= uni_check_ie(UNI_IE_CALLEDSUB, (union uni_ieall *)&m->calledsub[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_CALLING, (union uni_ieall *)&m->calling, cx);
	for(i = 0; i < UNI_NUM_IE_CALLINGSUB ; i++) {
		ret |= uni_check_ie(UNI_IE_CALLINGSUB, (union uni_ieall *)&m->callingsub[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_CONNID, (union uni_ieall *)&m->connid, cx);
	ret |= uni_check_ie(UNI_IE_QOS, (union uni_ieall *)&m->qos, cx);
	ret |= uni_check_ie(UNI_IE_EETD, (union uni_ieall *)&m->eetd, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->scompl);
	else
		ret |= uni_check_ie(UNI_IE_SCOMPL, (union uni_ieall *)&m->scompl, cx);
	for(i = 0; i < UNI_NUM_IE_TNS ; i++) {
		ret |= uni_check_ie(UNI_IE_TNS, (union uni_ieall *)&m->tns[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_ATRAFFIC, (union uni_ieall *)&m->atraffic, cx);
	ret |= uni_check_ie(UNI_IE_MINTRAFFIC, (union uni_ieall *)&m->mintraffic, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->uu);
	else
		ret |= uni_check_ie(UNI_IE_UU, (union uni_ieall *)&m->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->lij_callid);
	else
		ret |= uni_check_ie(UNI_IE_LIJ_CALLID, (union uni_ieall *)&m->lij_callid, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->lij_param);
	else
		ret |= uni_check_ie(UNI_IE_LIJ_PARAM, (union uni_ieall *)&m->lij_param, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->lij_seqno);
	else
		ret |= uni_check_ie(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&m->lij_seqno, cx);
	ret |= uni_check_ie(UNI_IE_EXQOS, (union uni_ieall *)&m->exqos, cx);
	ret |= uni_check_ie(UNI_IE_ABRSETUP, (union uni_ieall *)&m->abrsetup, cx);
	ret |= uni_check_ie(UNI_IE_ABRADD, (union uni_ieall *)&m->abradd, cx);
	ret |= uni_check_ie(UNI_IE_CSCOPE, (union uni_ieall *)&m->cscope, cx);
	if(!(cx->pnni))
		ret |= IE_ISPRESENT(m->calling_soft);
	else
		ret |= uni_check_ie(UNI_IE_CALLING_SOFT, (union uni_ieall *)&m->calling_soft, cx);
	if(!(cx->pnni))
		ret |= IE_ISPRESENT(m->called_soft);
	else
		ret |= uni_check_ie(UNI_IE_CALLED_SOFT, (union uni_ieall *)&m->called_soft, cx);
	if(!(cx->pnni))
		ret |= IE_ISPRESENT(m->dtl_repeat);
	else
		ret |= uni_check_ie(UNI_IE_REPEAT, (union uni_ieall *)&m->dtl_repeat, cx);
	for(i = 0; i < UNI_NUM_IE_DTL ; i++) {
		if(!(cx->pnni))
			ret |= IE_ISPRESENT(m->dtl[i]);
		else
			ret |= uni_check_ie(UNI_IE_DTL, (union uni_ieall *)&m->dtl[i], cx);
	}
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->report);
	else
		ret |= uni_check_ie(UNI_IE_REPORT, (union uni_ieall *)&m->report, cx);
	ret |= uni_check_ie(UNI_IE_MDCR, (union uni_ieall *)&m->mdcr, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_setup(struct uni_msg *msg, struct uni_setup *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_SETUP, cx, &mlen))
		return (-2);

	if((p->aal.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_AAL, msg, (union uni_ieall *)&p->aal, cx))
		return (UNI_IE_AAL);
	if((p->traffic.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_TRAFFIC, msg, (union uni_ieall *)&p->traffic, cx))
		return (UNI_IE_TRAFFIC);
	if((p->bearer.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_BEARER, msg, (union uni_ieall *)&p->bearer, cx))
		return (UNI_IE_BEARER);
	if((p->bhli.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_BHLI, msg, (union uni_ieall *)&p->bhli, cx))
		return (UNI_IE_BHLI);
	if((p->blli_repeat.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_BLLI, msg, (union uni_ieall *)&p->blli_repeat, cx))
		return (0x10000000 + UNI_IE_BLLI);
	for(i = 0; i < UNI_NUM_IE_BLLI; i++)
		if((p->blli[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_BLLI, msg, (union uni_ieall *)&p->blli[i], cx))
		return ((i << 16) + UNI_IE_BLLI);
	if((p->called.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLED, msg, (union uni_ieall *)&p->called, cx))
		return (UNI_IE_CALLED);
	for(i = 0; i < UNI_NUM_IE_CALLEDSUB; i++)
		if((p->calledsub[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_CALLEDSUB, msg, (union uni_ieall *)&p->calledsub[i], cx))
		return ((i << 16) + UNI_IE_CALLEDSUB);
	if((p->calling.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLING, msg, (union uni_ieall *)&p->calling, cx))
		return (UNI_IE_CALLING);
	for(i = 0; i < UNI_NUM_IE_CALLINGSUB; i++)
		if((p->callingsub[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_CALLINGSUB, msg, (union uni_ieall *)&p->callingsub[i], cx))
		return ((i << 16) + UNI_IE_CALLINGSUB);
	if((p->connid.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CONNID, msg, (union uni_ieall *)&p->connid, cx))
		return (UNI_IE_CONNID);
	if((p->qos.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_QOS, msg, (union uni_ieall *)&p->qos, cx))
		return (UNI_IE_QOS);
	if((p->eetd.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EETD, msg, (union uni_ieall *)&p->eetd, cx))
		return (UNI_IE_EETD);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	if((p->scompl.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_SCOMPL, msg, (union uni_ieall *)&p->scompl, cx))
		return (UNI_IE_SCOMPL);
	for(i = 0; i < UNI_NUM_IE_TNS; i++)
		if((p->tns[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_TNS, msg, (union uni_ieall *)&p->tns[i], cx))
		return ((i << 16) + UNI_IE_TNS);
	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->atraffic.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_ATRAFFIC, msg, (union uni_ieall *)&p->atraffic, cx))
		return (UNI_IE_ATRAFFIC);
	if((p->mintraffic.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_MINTRAFFIC, msg, (union uni_ieall *)&p->mintraffic, cx))
		return (UNI_IE_MINTRAFFIC);
	if((p->uu.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UU, msg, (union uni_ieall *)&p->uu, cx))
		return (UNI_IE_UU);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->lij_callid.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_LIJ_CALLID, msg, (union uni_ieall *)&p->lij_callid, cx))
		return (UNI_IE_LIJ_CALLID);
	if((p->lij_param.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_LIJ_PARAM, msg, (union uni_ieall *)&p->lij_param, cx))
		return (UNI_IE_LIJ_PARAM);
	if((p->lij_seqno.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_LIJ_SEQNO, msg, (union uni_ieall *)&p->lij_seqno, cx))
		return (UNI_IE_LIJ_SEQNO);
	if((p->exqos.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EXQOS, msg, (union uni_ieall *)&p->exqos, cx))
		return (UNI_IE_EXQOS);
	if((p->abrsetup.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_ABRSETUP, msg, (union uni_ieall *)&p->abrsetup, cx))
		return (UNI_IE_ABRSETUP);
	if((p->abradd.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_ABRADD, msg, (union uni_ieall *)&p->abradd, cx))
		return (UNI_IE_ABRADD);
	if((p->cscope.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CSCOPE, msg, (union uni_ieall *)&p->cscope, cx))
		return (UNI_IE_CSCOPE);
	if((p->calling_soft.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLING_SOFT, msg, (union uni_ieall *)&p->calling_soft, cx))
		return (UNI_IE_CALLING_SOFT);
	if((p->called_soft.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLED_SOFT, msg, (union uni_ieall *)&p->called_soft, cx))
		return (UNI_IE_CALLED_SOFT);
	if((p->dtl_repeat.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_DTL, msg, (union uni_ieall *)&p->dtl_repeat, cx))
		return (0x10000000 + UNI_IE_DTL);
	for(i = 0; i < UNI_NUM_IE_DTL; i++)
		if((p->dtl[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_DTL, msg, (union uni_ieall *)&p->dtl[i], cx))
		return ((i << 16) + UNI_IE_DTL);
	if((p->report.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_REPORT, msg, (union uni_ieall *)&p->report, cx))
		return (UNI_IE_REPORT);
	if((p->mdcr.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_MDCR, msg, (union uni_ieall *)&p->mdcr, cx))
		return (UNI_IE_MDCR);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_setup(struct uni_setup *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_AAL:
		out->aal.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_AAL, (union uni_ieall *)&out->aal, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_TRAFFIC:
		out->traffic.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_TRAFFIC, (union uni_ieall *)&out->traffic, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_BEARER:
		out->bearer.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_BEARER, (union uni_ieall *)&out->bearer, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_BHLI:
		out->bhli.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_BHLI, (union uni_ieall *)&out->bhli, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_BLLI:
		if (IE_ISPRESENT(cx->repeat))
			out->blli_repeat = cx->repeat;
		for(i = 0; i < UNI_NUM_IE_BLLI; i++)
			if (!IE_ISPRESENT(out->blli[i])) {
				out->blli[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_BLLI, (union uni_ieall *)&out->blli[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_CALLED:
		out->called.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLED, (union uni_ieall *)&out->called, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLEDSUB:
		for(i = 0; i < UNI_NUM_IE_CALLEDSUB; i++)
			if (!IE_ISPRESENT(out->calledsub[i])) {
				out->calledsub[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_CALLEDSUB, (union uni_ieall *)&out->calledsub[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_CALLING:
		out->calling.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLING, (union uni_ieall *)&out->calling, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLINGSUB:
		for(i = 0; i < UNI_NUM_IE_CALLINGSUB; i++)
			if (!IE_ISPRESENT(out->callingsub[i])) {
				out->callingsub[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_CALLINGSUB, (union uni_ieall *)&out->callingsub[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_CONNID:
		out->connid.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CONNID, (union uni_ieall *)&out->connid, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_QOS:
		out->qos.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_QOS, (union uni_ieall *)&out->qos, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EETD:
		out->eetd.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EETD, (union uni_ieall *)&out->eetd, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_SCOMPL:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->scompl.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_SCOMPL, (union uni_ieall *)&out->scompl, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_TNS:
		for(i = 0; i < UNI_NUM_IE_TNS; i++)
			if (!IE_ISPRESENT(out->tns[i])) {
				out->tns[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_TNS, (union uni_ieall *)&out->tns[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_ATRAFFIC:
		out->atraffic.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_ATRAFFIC, (union uni_ieall *)&out->atraffic, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_MINTRAFFIC:
		out->mintraffic.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_MINTRAFFIC, (union uni_ieall *)&out->mintraffic, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UU:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->uu.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UU, (union uni_ieall *)&out->uu, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_LIJ_CALLID:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->lij_callid.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_LIJ_CALLID, (union uni_ieall *)&out->lij_callid, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_LIJ_PARAM:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->lij_param.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_LIJ_PARAM, (union uni_ieall *)&out->lij_param, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_LIJ_SEQNO:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->lij_seqno.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&out->lij_seqno, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EXQOS:
		out->exqos.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EXQOS, (union uni_ieall *)&out->exqos, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_ABRSETUP:
		out->abrsetup.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_ABRSETUP, (union uni_ieall *)&out->abrsetup, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_ABRADD:
		out->abradd.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_ABRADD, (union uni_ieall *)&out->abradd, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CSCOPE:
		out->cscope.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CSCOPE, (union uni_ieall *)&out->cscope, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLING_SOFT:
		if (!(cx->pnni))
			return (DEC_ILL);
		out->calling_soft.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLING_SOFT, (union uni_ieall *)&out->calling_soft, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLED_SOFT:
		if (!(cx->pnni))
			return (DEC_ILL);
		out->called_soft.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLED_SOFT, (union uni_ieall *)&out->called_soft, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_DTL:
		if (!(cx->pnni))
			return (DEC_ILL);
		if (IE_ISPRESENT(cx->repeat))
			out->dtl_repeat = cx->repeat;
		for(i = 0; i < UNI_NUM_IE_DTL; i++)
			if (!IE_ISPRESENT(out->dtl[i])) {
				out->dtl[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_DTL, (union uni_ieall *)&out->dtl[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_REPORT:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->report.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_REPORT, (union uni_ieall *)&out->report, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_MDCR:
		out->mdcr.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_MDCR, (union uni_ieall *)&out->mdcr, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_REPEAT:
		cx->repeat.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if (uni_decode_ie_body(UNI_IE_REPEAT, (union uni_ieall *)&cx->repeat, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_setup = {
	0,
	"setup",
	(uni_msg_print_f)print_setup,
	(uni_msg_check_f)check_setup,
	(uni_msg_encode_f)encode_setup,
	(uni_msg_decode_f)decode_setup
};

static void
print_status(struct uni_status *msg, struct unicx *cx)
{
	if(msg->callstate.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLSTATE, (union uni_ieall *)&msg->callstate, cx);
	if(msg->cause.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CAUSE, (union uni_ieall *)&msg->cause, cx);
	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->epstate.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPSTATE, (union uni_ieall *)&msg->epstate, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_status(struct uni_status *m, struct unicx *cx)
{
	int ret = 0;

	ret |= uni_check_ie(UNI_IE_CALLSTATE, (union uni_ieall *)&m->callstate, cx);
	ret |= uni_check_ie(UNI_IE_CAUSE, (union uni_ieall *)&m->cause, cx);
	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_EPSTATE, (union uni_ieall *)&m->epstate, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_status(struct uni_msg *msg, struct uni_status *p, struct unicx *cx)
{
	u_int mlen;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_STATUS, cx, &mlen))
		return (-2);

	if((p->callstate.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLSTATE, msg, (union uni_ieall *)&p->callstate, cx))
		return (UNI_IE_CALLSTATE);
	if((p->cause.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CAUSE, msg, (union uni_ieall *)&p->cause, cx))
		return (UNI_IE_CAUSE);
	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->epstate.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPSTATE, msg, (union uni_ieall *)&p->epstate, cx))
		return (UNI_IE_EPSTATE);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_status(struct uni_status *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	switch (ie) {

	  case UNI_IE_CALLSTATE:
		out->callstate.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLSTATE, (union uni_ieall *)&out->callstate, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CAUSE:
		out->cause.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CAUSE, (union uni_ieall *)&out->cause, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EPSTATE:
		out->epstate.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPSTATE, (union uni_ieall *)&out->epstate, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_status = {
	0,
	"status",
	(uni_msg_print_f)print_status,
	(uni_msg_check_f)check_status,
	(uni_msg_encode_f)encode_status,
	(uni_msg_decode_f)decode_status
};

static void
print_status_enq(struct uni_status_enq *msg, struct unicx *cx)
{
	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_status_enq(struct uni_status_enq *m, struct unicx *cx)
{
	int ret = 0;

	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_status_enq(struct uni_msg *msg, struct uni_status_enq *p, struct unicx *cx)
{
	u_int mlen;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_STATUS_ENQ, cx, &mlen))
		return (-2);

	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_status_enq(struct uni_status_enq *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	switch (ie) {

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_status_enq = {
	0,
	"status_enq",
	(uni_msg_print_f)print_status_enq,
	(uni_msg_check_f)check_status_enq,
	(uni_msg_encode_f)encode_status_enq,
	(uni_msg_decode_f)decode_status_enq
};

static void
print_notify(struct uni_notify *msg, struct unicx *cx)
{
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_notify(struct uni_notify *m, struct unicx *cx)
{
	int ret = 0;

	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_notify(struct uni_msg *msg, struct uni_notify *p, struct unicx *cx)
{
	u_int mlen;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_NOTIFY, cx, &mlen))
		return (-2);

	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_notify(struct uni_notify *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	switch (ie) {

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_notify = {
	0,
	"notify",
	(uni_msg_print_f)print_notify,
	(uni_msg_check_f)check_notify,
	(uni_msg_encode_f)encode_notify,
	(uni_msg_decode_f)decode_notify
};

static void
print_restart(struct uni_restart *msg, struct unicx *cx)
{
	if(msg->connid.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CONNID, (union uni_ieall *)&msg->connid, cx);
	if(msg->restart.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_RESTART, (union uni_ieall *)&msg->restart, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_restart(struct uni_restart *m, struct unicx *cx)
{
	int ret = 0;

	ret |= uni_check_ie(UNI_IE_CONNID, (union uni_ieall *)&m->connid, cx);
	ret |= uni_check_ie(UNI_IE_RESTART, (union uni_ieall *)&m->restart, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_restart(struct uni_msg *msg, struct uni_restart *p, struct unicx *cx)
{
	u_int mlen;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_RESTART, cx, &mlen))
		return (-2);

	if((p->connid.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CONNID, msg, (union uni_ieall *)&p->connid, cx))
		return (UNI_IE_CONNID);
	if((p->restart.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_RESTART, msg, (union uni_ieall *)&p->restart, cx))
		return (UNI_IE_RESTART);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_restart(struct uni_restart *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	switch (ie) {

	  case UNI_IE_CONNID:
		out->connid.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CONNID, (union uni_ieall *)&out->connid, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_RESTART:
		out->restart.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_RESTART, (union uni_ieall *)&out->restart, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_restart = {
	0,
	"restart",
	(uni_msg_print_f)print_restart,
	(uni_msg_check_f)check_restart,
	(uni_msg_encode_f)encode_restart,
	(uni_msg_decode_f)decode_restart
};

static void
print_restart_ack(struct uni_restart_ack *msg, struct unicx *cx)
{
	if(msg->connid.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CONNID, (union uni_ieall *)&msg->connid, cx);
	if(msg->restart.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_RESTART, (union uni_ieall *)&msg->restart, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_restart_ack(struct uni_restart_ack *m, struct unicx *cx)
{
	int ret = 0;

	ret |= uni_check_ie(UNI_IE_CONNID, (union uni_ieall *)&m->connid, cx);
	ret |= uni_check_ie(UNI_IE_RESTART, (union uni_ieall *)&m->restart, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_restart_ack(struct uni_msg *msg, struct uni_restart_ack *p, struct unicx *cx)
{
	u_int mlen;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_RESTART_ACK, cx, &mlen))
		return (-2);

	if((p->connid.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CONNID, msg, (union uni_ieall *)&p->connid, cx))
		return (UNI_IE_CONNID);
	if((p->restart.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_RESTART, msg, (union uni_ieall *)&p->restart, cx))
		return (UNI_IE_RESTART);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_restart_ack(struct uni_restart_ack *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	switch (ie) {

	  case UNI_IE_CONNID:
		out->connid.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CONNID, (union uni_ieall *)&out->connid, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_RESTART:
		out->restart.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_RESTART, (union uni_ieall *)&out->restart, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_restart_ack = {
	0,
	"restart_ack",
	(uni_msg_print_f)print_restart_ack,
	(uni_msg_check_f)check_restart_ack,
	(uni_msg_encode_f)encode_restart_ack,
	(uni_msg_decode_f)decode_restart_ack
};

static void
print_add_party(struct uni_add_party *msg, struct unicx *cx)
{
	u_int i;

	if(msg->aal.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_AAL, (union uni_ieall *)&msg->aal, cx);
	if(msg->bhli.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_BHLI, (union uni_ieall *)&msg->bhli, cx);
	if(msg->blli.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_BLLI, (union uni_ieall *)&msg->blli, cx);
	if(msg->called.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLED, (union uni_ieall *)&msg->called, cx);
	for(i = 0; i < UNI_NUM_IE_CALLEDSUB; i++)
		if(msg->calledsub[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_CALLEDSUB, (union uni_ieall *)&msg->calledsub[i], cx);
	if(msg->calling.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLING, (union uni_ieall *)&msg->calling, cx);
	for(i = 0; i < UNI_NUM_IE_CALLINGSUB; i++)
		if(msg->callingsub[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_CALLINGSUB, (union uni_ieall *)&msg->callingsub[i], cx);
	if(msg->scompl.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_SCOMPL, (union uni_ieall *)&msg->scompl, cx);
	for(i = 0; i < UNI_NUM_IE_TNS; i++)
		if(msg->tns[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_TNS, (union uni_ieall *)&msg->tns[i], cx);
	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	if(msg->eetd.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EETD, (union uni_ieall *)&msg->eetd, cx);
	if(msg->uu.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UU, (union uni_ieall *)&msg->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->lij_seqno.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&msg->lij_seqno, cx);
	if(msg->calling_soft.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLING_SOFT, (union uni_ieall *)&msg->calling_soft, cx);
	if(msg->called_soft.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLED_SOFT, (union uni_ieall *)&msg->called_soft, cx);
	if(msg->dtl_repeat.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_REPEAT, (union uni_ieall *)&msg->dtl_repeat, cx);
	for(i = 0; i < UNI_NUM_IE_DTL; i++)
		if(msg->dtl[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_DTL, (union uni_ieall *)&msg->dtl[i], cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_add_party(struct uni_add_party *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_AAL, (union uni_ieall *)&m->aal, cx);
	ret |= uni_check_ie(UNI_IE_BHLI, (union uni_ieall *)&m->bhli, cx);
	ret |= uni_check_ie(UNI_IE_BLLI, (union uni_ieall *)&m->blli, cx);
	ret |= uni_check_ie(UNI_IE_CALLED, (union uni_ieall *)&m->called, cx);
	for(i = 0; i < UNI_NUM_IE_CALLEDSUB ; i++) {
		ret |= uni_check_ie(UNI_IE_CALLEDSUB, (union uni_ieall *)&m->calledsub[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_CALLING, (union uni_ieall *)&m->calling, cx);
	for(i = 0; i < UNI_NUM_IE_CALLINGSUB ; i++) {
		ret |= uni_check_ie(UNI_IE_CALLINGSUB, (union uni_ieall *)&m->callingsub[i], cx);
	}
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->scompl);
	else
		ret |= uni_check_ie(UNI_IE_SCOMPL, (union uni_ieall *)&m->scompl, cx);
	for(i = 0; i < UNI_NUM_IE_TNS ; i++) {
		ret |= uni_check_ie(UNI_IE_TNS, (union uni_ieall *)&m->tns[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	ret |= uni_check_ie(UNI_IE_EETD, (union uni_ieall *)&m->eetd, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->uu);
	else
		ret |= uni_check_ie(UNI_IE_UU, (union uni_ieall *)&m->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->lij_seqno);
	else
		ret |= uni_check_ie(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&m->lij_seqno, cx);
	if(!(cx->pnni))
		ret |= IE_ISPRESENT(m->calling_soft);
	else
		ret |= uni_check_ie(UNI_IE_CALLING_SOFT, (union uni_ieall *)&m->calling_soft, cx);
	if(!(cx->pnni))
		ret |= IE_ISPRESENT(m->called_soft);
	else
		ret |= uni_check_ie(UNI_IE_CALLED_SOFT, (union uni_ieall *)&m->called_soft, cx);
	if(!(cx->pnni))
		ret |= IE_ISPRESENT(m->dtl_repeat);
	else
		ret |= uni_check_ie(UNI_IE_REPEAT, (union uni_ieall *)&m->dtl_repeat, cx);
	for(i = 0; i < UNI_NUM_IE_DTL ; i++) {
		if(!(cx->pnni))
			ret |= IE_ISPRESENT(m->dtl[i]);
		else
			ret |= uni_check_ie(UNI_IE_DTL, (union uni_ieall *)&m->dtl[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_add_party(struct uni_msg *msg, struct uni_add_party *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_ADD_PARTY, cx, &mlen))
		return (-2);

	if((p->aal.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_AAL, msg, (union uni_ieall *)&p->aal, cx))
		return (UNI_IE_AAL);
	if((p->bhli.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_BHLI, msg, (union uni_ieall *)&p->bhli, cx))
		return (UNI_IE_BHLI);
	if((p->blli.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_BLLI, msg, (union uni_ieall *)&p->blli, cx))
		return (UNI_IE_BLLI);
	if((p->called.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLED, msg, (union uni_ieall *)&p->called, cx))
		return (UNI_IE_CALLED);
	for(i = 0; i < UNI_NUM_IE_CALLEDSUB; i++)
		if((p->calledsub[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_CALLEDSUB, msg, (union uni_ieall *)&p->calledsub[i], cx))
		return ((i << 16) + UNI_IE_CALLEDSUB);
	if((p->calling.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLING, msg, (union uni_ieall *)&p->calling, cx))
		return (UNI_IE_CALLING);
	for(i = 0; i < UNI_NUM_IE_CALLINGSUB; i++)
		if((p->callingsub[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_CALLINGSUB, msg, (union uni_ieall *)&p->callingsub[i], cx))
		return ((i << 16) + UNI_IE_CALLINGSUB);
	if((p->scompl.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_SCOMPL, msg, (union uni_ieall *)&p->scompl, cx))
		return (UNI_IE_SCOMPL);
	for(i = 0; i < UNI_NUM_IE_TNS; i++)
		if((p->tns[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_TNS, msg, (union uni_ieall *)&p->tns[i], cx))
		return ((i << 16) + UNI_IE_TNS);
	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	if((p->eetd.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EETD, msg, (union uni_ieall *)&p->eetd, cx))
		return (UNI_IE_EETD);
	if((p->uu.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UU, msg, (union uni_ieall *)&p->uu, cx))
		return (UNI_IE_UU);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->lij_seqno.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_LIJ_SEQNO, msg, (union uni_ieall *)&p->lij_seqno, cx))
		return (UNI_IE_LIJ_SEQNO);
	if((p->calling_soft.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLING_SOFT, msg, (union uni_ieall *)&p->calling_soft, cx))
		return (UNI_IE_CALLING_SOFT);
	if((p->called_soft.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLED_SOFT, msg, (union uni_ieall *)&p->called_soft, cx))
		return (UNI_IE_CALLED_SOFT);
	if((p->dtl_repeat.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_DTL, msg, (union uni_ieall *)&p->dtl_repeat, cx))
		return (0x10000000 + UNI_IE_DTL);
	for(i = 0; i < UNI_NUM_IE_DTL; i++)
		if((p->dtl[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_DTL, msg, (union uni_ieall *)&p->dtl[i], cx))
		return ((i << 16) + UNI_IE_DTL);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_add_party(struct uni_add_party *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_AAL:
		out->aal.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_AAL, (union uni_ieall *)&out->aal, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_BHLI:
		out->bhli.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_BHLI, (union uni_ieall *)&out->bhli, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_BLLI:
		out->blli.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_BLLI, (union uni_ieall *)&out->blli, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLED:
		out->called.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLED, (union uni_ieall *)&out->called, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLEDSUB:
		for(i = 0; i < UNI_NUM_IE_CALLEDSUB; i++)
			if (!IE_ISPRESENT(out->calledsub[i])) {
				out->calledsub[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_CALLEDSUB, (union uni_ieall *)&out->calledsub[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_CALLING:
		out->calling.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLING, (union uni_ieall *)&out->calling, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLINGSUB:
		for(i = 0; i < UNI_NUM_IE_CALLINGSUB; i++)
			if (!IE_ISPRESENT(out->callingsub[i])) {
				out->callingsub[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_CALLINGSUB, (union uni_ieall *)&out->callingsub[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_SCOMPL:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->scompl.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_SCOMPL, (union uni_ieall *)&out->scompl, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_TNS:
		for(i = 0; i < UNI_NUM_IE_TNS; i++)
			if (!IE_ISPRESENT(out->tns[i])) {
				out->tns[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_TNS, (union uni_ieall *)&out->tns[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EETD:
		out->eetd.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EETD, (union uni_ieall *)&out->eetd, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UU:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->uu.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UU, (union uni_ieall *)&out->uu, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_LIJ_SEQNO:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->lij_seqno.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&out->lij_seqno, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLING_SOFT:
		if (!(cx->pnni))
			return (DEC_ILL);
		out->calling_soft.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLING_SOFT, (union uni_ieall *)&out->calling_soft, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLED_SOFT:
		if (!(cx->pnni))
			return (DEC_ILL);
		out->called_soft.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLED_SOFT, (union uni_ieall *)&out->called_soft, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_DTL:
		if (!(cx->pnni))
			return (DEC_ILL);
		if (IE_ISPRESENT(cx->repeat))
			out->dtl_repeat = cx->repeat;
		for(i = 0; i < UNI_NUM_IE_DTL; i++)
			if (!IE_ISPRESENT(out->dtl[i])) {
				out->dtl[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_DTL, (union uni_ieall *)&out->dtl[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_REPEAT:
		cx->repeat.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if (uni_decode_ie_body(UNI_IE_REPEAT, (union uni_ieall *)&cx->repeat, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_add_party = {
	0,
	"add_party",
	(uni_msg_print_f)print_add_party,
	(uni_msg_check_f)check_add_party,
	(uni_msg_encode_f)encode_add_party,
	(uni_msg_decode_f)decode_add_party
};

static void
print_add_party_ack(struct uni_add_party_ack *msg, struct unicx *cx)
{
	u_int i;

	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->aal.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_AAL, (union uni_ieall *)&msg->aal, cx);
	if(msg->blli.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_BLLI, (union uni_ieall *)&msg->blli, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	if(msg->eetd.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EETD, (union uni_ieall *)&msg->eetd, cx);
	if(msg->conned.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CONNED, (union uni_ieall *)&msg->conned, cx);
	if(msg->connedsub.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CONNEDSUB, (union uni_ieall *)&msg->connedsub, cx);
	if(msg->uu.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UU, (union uni_ieall *)&msg->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->called_soft.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLED_SOFT, (union uni_ieall *)&msg->called_soft, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_add_party_ack(struct uni_add_party_ack *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_AAL, (union uni_ieall *)&m->aal, cx);
	ret |= uni_check_ie(UNI_IE_BLLI, (union uni_ieall *)&m->blli, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	ret |= uni_check_ie(UNI_IE_EETD, (union uni_ieall *)&m->eetd, cx);
	ret |= uni_check_ie(UNI_IE_CONNED, (union uni_ieall *)&m->conned, cx);
	ret |= uni_check_ie(UNI_IE_CONNEDSUB, (union uni_ieall *)&m->connedsub, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->uu);
	else
		ret |= uni_check_ie(UNI_IE_UU, (union uni_ieall *)&m->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	if(!(cx->pnni))
		ret |= IE_ISPRESENT(m->called_soft);
	else
		ret |= uni_check_ie(UNI_IE_CALLED_SOFT, (union uni_ieall *)&m->called_soft, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_add_party_ack(struct uni_msg *msg, struct uni_add_party_ack *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_ADD_PARTY_ACK, cx, &mlen))
		return (-2);

	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->aal.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_AAL, msg, (union uni_ieall *)&p->aal, cx))
		return (UNI_IE_AAL);
	if((p->blli.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_BLLI, msg, (union uni_ieall *)&p->blli, cx))
		return (UNI_IE_BLLI);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	if((p->eetd.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EETD, msg, (union uni_ieall *)&p->eetd, cx))
		return (UNI_IE_EETD);
	if((p->conned.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CONNED, msg, (union uni_ieall *)&p->conned, cx))
		return (UNI_IE_CONNED);
	if((p->connedsub.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CONNEDSUB, msg, (union uni_ieall *)&p->connedsub, cx))
		return (UNI_IE_CONNEDSUB);
	if((p->uu.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UU, msg, (union uni_ieall *)&p->uu, cx))
		return (UNI_IE_UU);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->called_soft.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLED_SOFT, msg, (union uni_ieall *)&p->called_soft, cx))
		return (UNI_IE_CALLED_SOFT);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_add_party_ack(struct uni_add_party_ack *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_AAL:
		out->aal.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_AAL, (union uni_ieall *)&out->aal, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_BLLI:
		out->blli.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_BLLI, (union uni_ieall *)&out->blli, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EETD:
		out->eetd.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EETD, (union uni_ieall *)&out->eetd, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CONNED:
		out->conned.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CONNED, (union uni_ieall *)&out->conned, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CONNEDSUB:
		out->connedsub.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CONNEDSUB, (union uni_ieall *)&out->connedsub, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UU:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->uu.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UU, (union uni_ieall *)&out->uu, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_CALLED_SOFT:
		if (!(cx->pnni))
			return (DEC_ILL);
		out->called_soft.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLED_SOFT, (union uni_ieall *)&out->called_soft, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_add_party_ack = {
	0,
	"add_party_ack",
	(uni_msg_print_f)print_add_party_ack,
	(uni_msg_check_f)check_add_party_ack,
	(uni_msg_encode_f)encode_add_party_ack,
	(uni_msg_decode_f)decode_add_party_ack
};

static void
print_party_alerting(struct uni_party_alerting *msg, struct unicx *cx)
{
	u_int i;

	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	if(msg->uu.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UU, (union uni_ieall *)&msg->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_party_alerting(struct uni_party_alerting *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->uu);
	else
		ret |= uni_check_ie(UNI_IE_UU, (union uni_ieall *)&m->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_party_alerting(struct uni_msg *msg, struct uni_party_alerting *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_PARTY_ALERTING, cx, &mlen))
		return (-2);

	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	if((p->uu.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UU, msg, (union uni_ieall *)&p->uu, cx))
		return (UNI_IE_UU);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_party_alerting(struct uni_party_alerting *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UU:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->uu.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UU, (union uni_ieall *)&out->uu, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_party_alerting = {
	0,
	"party_alerting",
	(uni_msg_print_f)print_party_alerting,
	(uni_msg_check_f)check_party_alerting,
	(uni_msg_encode_f)encode_party_alerting,
	(uni_msg_decode_f)decode_party_alerting
};

static void
print_add_party_rej(struct uni_add_party_rej *msg, struct unicx *cx)
{
	u_int i;

	if(msg->cause.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CAUSE, (union uni_ieall *)&msg->cause, cx);
	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->uu.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UU, (union uni_ieall *)&msg->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->crankback.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CRANKBACK, (union uni_ieall *)&msg->crankback, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_add_party_rej(struct uni_add_party_rej *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_CAUSE, (union uni_ieall *)&m->cause, cx);
	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->uu);
	else
		ret |= uni_check_ie(UNI_IE_UU, (union uni_ieall *)&m->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	if(!(cx->pnni))
		ret |= IE_ISPRESENT(m->crankback);
	else
		ret |= uni_check_ie(UNI_IE_CRANKBACK, (union uni_ieall *)&m->crankback, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_add_party_rej(struct uni_msg *msg, struct uni_add_party_rej *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_ADD_PARTY_REJ, cx, &mlen))
		return (-2);

	if((p->cause.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CAUSE, msg, (union uni_ieall *)&p->cause, cx))
		return (UNI_IE_CAUSE);
	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->uu.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UU, msg, (union uni_ieall *)&p->uu, cx))
		return (UNI_IE_UU);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->crankback.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CRANKBACK, msg, (union uni_ieall *)&p->crankback, cx))
		return (UNI_IE_CRANKBACK);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_add_party_rej(struct uni_add_party_rej *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_CAUSE:
		out->cause.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CAUSE, (union uni_ieall *)&out->cause, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UU:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->uu.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UU, (union uni_ieall *)&out->uu, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_CRANKBACK:
		if (!(cx->pnni))
			return (DEC_ILL);
		out->crankback.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CRANKBACK, (union uni_ieall *)&out->crankback, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_add_party_rej = {
	0,
	"add_party_rej",
	(uni_msg_print_f)print_add_party_rej,
	(uni_msg_check_f)check_add_party_rej,
	(uni_msg_encode_f)encode_add_party_rej,
	(uni_msg_decode_f)decode_add_party_rej
};

static void
print_drop_party(struct uni_drop_party *msg, struct unicx *cx)
{
	u_int i;

	if(msg->cause.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CAUSE, (union uni_ieall *)&msg->cause, cx);
	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	if(msg->uu.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UU, (union uni_ieall *)&msg->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_drop_party(struct uni_drop_party *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_CAUSE, (union uni_ieall *)&m->cause, cx);
	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->uu);
	else
		ret |= uni_check_ie(UNI_IE_UU, (union uni_ieall *)&m->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_drop_party(struct uni_msg *msg, struct uni_drop_party *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_DROP_PARTY, cx, &mlen))
		return (-2);

	if((p->cause.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CAUSE, msg, (union uni_ieall *)&p->cause, cx))
		return (UNI_IE_CAUSE);
	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	if((p->uu.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UU, msg, (union uni_ieall *)&p->uu, cx))
		return (UNI_IE_UU);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_drop_party(struct uni_drop_party *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_CAUSE:
		out->cause.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CAUSE, (union uni_ieall *)&out->cause, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UU:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->uu.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UU, (union uni_ieall *)&out->uu, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_drop_party = {
	0,
	"drop_party",
	(uni_msg_print_f)print_drop_party,
	(uni_msg_check_f)check_drop_party,
	(uni_msg_encode_f)encode_drop_party,
	(uni_msg_decode_f)decode_drop_party
};

static void
print_drop_party_ack(struct uni_drop_party_ack *msg, struct unicx *cx)
{
	u_int i;

	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->cause.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CAUSE, (union uni_ieall *)&msg->cause, cx);
	if(msg->uu.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UU, (union uni_ieall *)&msg->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_drop_party_ack(struct uni_drop_party_ack *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_CAUSE, (union uni_ieall *)&m->cause, cx);
	if(!(!cx->pnni))
		ret |= IE_ISPRESENT(m->uu);
	else
		ret |= uni_check_ie(UNI_IE_UU, (union uni_ieall *)&m->uu, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_drop_party_ack(struct uni_msg *msg, struct uni_drop_party_ack *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_DROP_PARTY_ACK, cx, &mlen))
		return (-2);

	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->cause.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CAUSE, msg, (union uni_ieall *)&p->cause, cx))
		return (UNI_IE_CAUSE);
	if((p->uu.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UU, msg, (union uni_ieall *)&p->uu, cx))
		return (UNI_IE_UU);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_drop_party_ack(struct uni_drop_party_ack *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CAUSE:
		out->cause.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CAUSE, (union uni_ieall *)&out->cause, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UU:
		if (!(!cx->pnni))
			return (DEC_ILL);
		out->uu.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UU, (union uni_ieall *)&out->uu, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_drop_party_ack = {
	0,
	"drop_party_ack",
	(uni_msg_print_f)print_drop_party_ack,
	(uni_msg_check_f)check_drop_party_ack,
	(uni_msg_encode_f)encode_drop_party_ack,
	(uni_msg_decode_f)decode_drop_party_ack
};

static void
print_leaf_setup_req(struct uni_leaf_setup_req *msg, struct unicx *cx)
{
	u_int i;

	for(i = 0; i < UNI_NUM_IE_TNS; i++)
		if(msg->tns[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_TNS, (union uni_ieall *)&msg->tns[i], cx);
	if(msg->calling.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLING, (union uni_ieall *)&msg->calling, cx);
	for(i = 0; i < UNI_NUM_IE_CALLINGSUB; i++)
		if(msg->callingsub[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_CALLINGSUB, (union uni_ieall *)&msg->callingsub[i], cx);
	if(msg->called.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLED, (union uni_ieall *)&msg->called, cx);
	for(i = 0; i < UNI_NUM_IE_CALLEDSUB; i++)
		if(msg->calledsub[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_CALLEDSUB, (union uni_ieall *)&msg->calledsub[i], cx);
	if(msg->lij_callid.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_LIJ_CALLID, (union uni_ieall *)&msg->lij_callid, cx);
	if(msg->lij_seqno.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&msg->lij_seqno, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_leaf_setup_req(struct uni_leaf_setup_req *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	for(i = 0; i < UNI_NUM_IE_TNS ; i++) {
		ret |= uni_check_ie(UNI_IE_TNS, (union uni_ieall *)&m->tns[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_CALLING, (union uni_ieall *)&m->calling, cx);
	for(i = 0; i < UNI_NUM_IE_CALLINGSUB ; i++) {
		ret |= uni_check_ie(UNI_IE_CALLINGSUB, (union uni_ieall *)&m->callingsub[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_CALLED, (union uni_ieall *)&m->called, cx);
	for(i = 0; i < UNI_NUM_IE_CALLEDSUB ; i++) {
		ret |= uni_check_ie(UNI_IE_CALLEDSUB, (union uni_ieall *)&m->calledsub[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_LIJ_CALLID, (union uni_ieall *)&m->lij_callid, cx);
	ret |= uni_check_ie(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&m->lij_seqno, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_leaf_setup_req(struct uni_msg *msg, struct uni_leaf_setup_req *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_LEAF_SETUP_REQ, cx, &mlen))
		return (-2);

	for(i = 0; i < UNI_NUM_IE_TNS; i++)
		if((p->tns[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_TNS, msg, (union uni_ieall *)&p->tns[i], cx))
		return ((i << 16) + UNI_IE_TNS);
	if((p->calling.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLING, msg, (union uni_ieall *)&p->calling, cx))
		return (UNI_IE_CALLING);
	for(i = 0; i < UNI_NUM_IE_CALLINGSUB; i++)
		if((p->callingsub[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_CALLINGSUB, msg, (union uni_ieall *)&p->callingsub[i], cx))
		return ((i << 16) + UNI_IE_CALLINGSUB);
	if((p->called.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLED, msg, (union uni_ieall *)&p->called, cx))
		return (UNI_IE_CALLED);
	for(i = 0; i < UNI_NUM_IE_CALLEDSUB; i++)
		if((p->calledsub[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_CALLEDSUB, msg, (union uni_ieall *)&p->calledsub[i], cx))
		return ((i << 16) + UNI_IE_CALLEDSUB);
	if((p->lij_callid.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_LIJ_CALLID, msg, (union uni_ieall *)&p->lij_callid, cx))
		return (UNI_IE_LIJ_CALLID);
	if((p->lij_seqno.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_LIJ_SEQNO, msg, (union uni_ieall *)&p->lij_seqno, cx))
		return (UNI_IE_LIJ_SEQNO);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_leaf_setup_req(struct uni_leaf_setup_req *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_TNS:
		for(i = 0; i < UNI_NUM_IE_TNS; i++)
			if (!IE_ISPRESENT(out->tns[i])) {
				out->tns[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_TNS, (union uni_ieall *)&out->tns[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_CALLING:
		out->calling.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLING, (union uni_ieall *)&out->calling, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLINGSUB:
		for(i = 0; i < UNI_NUM_IE_CALLINGSUB; i++)
			if (!IE_ISPRESENT(out->callingsub[i])) {
				out->callingsub[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_CALLINGSUB, (union uni_ieall *)&out->callingsub[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_CALLED:
		out->called.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLED, (union uni_ieall *)&out->called, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLEDSUB:
		for(i = 0; i < UNI_NUM_IE_CALLEDSUB; i++)
			if (!IE_ISPRESENT(out->calledsub[i])) {
				out->calledsub[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_CALLEDSUB, (union uni_ieall *)&out->calledsub[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_LIJ_CALLID:
		out->lij_callid.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_LIJ_CALLID, (union uni_ieall *)&out->lij_callid, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_LIJ_SEQNO:
		out->lij_seqno.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&out->lij_seqno, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_leaf_setup_req = {
	0,
	"leaf_setup_req",
	(uni_msg_print_f)print_leaf_setup_req,
	(uni_msg_check_f)check_leaf_setup_req,
	(uni_msg_encode_f)encode_leaf_setup_req,
	(uni_msg_decode_f)decode_leaf_setup_req
};

static void
print_leaf_setup_fail(struct uni_leaf_setup_fail *msg, struct unicx *cx)
{
	u_int i;

	if(msg->cause.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CAUSE, (union uni_ieall *)&msg->cause, cx);
	if(msg->called.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLED, (union uni_ieall *)&msg->called, cx);
	if(msg->calledsub.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLEDSUB, (union uni_ieall *)&msg->calledsub, cx);
	if(msg->lij_seqno.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&msg->lij_seqno, cx);
	for(i = 0; i < UNI_NUM_IE_TNS; i++)
		if(msg->tns[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_TNS, (union uni_ieall *)&msg->tns[i], cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_leaf_setup_fail(struct uni_leaf_setup_fail *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_CAUSE, (union uni_ieall *)&m->cause, cx);
	ret |= uni_check_ie(UNI_IE_CALLED, (union uni_ieall *)&m->called, cx);
	ret |= uni_check_ie(UNI_IE_CALLEDSUB, (union uni_ieall *)&m->calledsub, cx);
	ret |= uni_check_ie(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&m->lij_seqno, cx);
	for(i = 0; i < UNI_NUM_IE_TNS ; i++) {
		ret |= uni_check_ie(UNI_IE_TNS, (union uni_ieall *)&m->tns[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_leaf_setup_fail(struct uni_msg *msg, struct uni_leaf_setup_fail *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_LEAF_SETUP_FAIL, cx, &mlen))
		return (-2);

	if((p->cause.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CAUSE, msg, (union uni_ieall *)&p->cause, cx))
		return (UNI_IE_CAUSE);
	if((p->called.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLED, msg, (union uni_ieall *)&p->called, cx))
		return (UNI_IE_CALLED);
	if((p->calledsub.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLEDSUB, msg, (union uni_ieall *)&p->calledsub, cx))
		return (UNI_IE_CALLEDSUB);
	if((p->lij_seqno.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_LIJ_SEQNO, msg, (union uni_ieall *)&p->lij_seqno, cx))
		return (UNI_IE_LIJ_SEQNO);
	for(i = 0; i < UNI_NUM_IE_TNS; i++)
		if((p->tns[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_TNS, msg, (union uni_ieall *)&p->tns[i], cx))
		return ((i << 16) + UNI_IE_TNS);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_leaf_setup_fail(struct uni_leaf_setup_fail *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_CAUSE:
		out->cause.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CAUSE, (union uni_ieall *)&out->cause, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLED:
		out->called.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLED, (union uni_ieall *)&out->called, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLEDSUB:
		out->calledsub.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLEDSUB, (union uni_ieall *)&out->calledsub, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_LIJ_SEQNO:
		out->lij_seqno.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_LIJ_SEQNO, (union uni_ieall *)&out->lij_seqno, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_TNS:
		for(i = 0; i < UNI_NUM_IE_TNS; i++)
			if (!IE_ISPRESENT(out->tns[i])) {
				out->tns[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_TNS, (union uni_ieall *)&out->tns[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_leaf_setup_fail = {
	0,
	"leaf_setup_fail",
	(uni_msg_print_f)print_leaf_setup_fail,
	(uni_msg_check_f)check_leaf_setup_fail,
	(uni_msg_encode_f)encode_leaf_setup_fail,
	(uni_msg_decode_f)decode_leaf_setup_fail
};

static void
print_cobisetup(struct uni_cobisetup *msg, struct unicx *cx)
{
	if(msg->facility.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_FACILITY, (union uni_ieall *)&msg->facility, cx);
	if(msg->called.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLED, (union uni_ieall *)&msg->called, cx);
	if(msg->calledsub.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLEDSUB, (union uni_ieall *)&msg->calledsub, cx);
	if(msg->calling.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLING, (union uni_ieall *)&msg->calling, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_cobisetup(struct uni_cobisetup *m, struct unicx *cx)
{
	int ret = 0;

	ret |= uni_check_ie(UNI_IE_FACILITY, (union uni_ieall *)&m->facility, cx);
	ret |= uni_check_ie(UNI_IE_CALLED, (union uni_ieall *)&m->called, cx);
	ret |= uni_check_ie(UNI_IE_CALLEDSUB, (union uni_ieall *)&m->calledsub, cx);
	ret |= uni_check_ie(UNI_IE_CALLING, (union uni_ieall *)&m->calling, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_cobisetup(struct uni_msg *msg, struct uni_cobisetup *p, struct unicx *cx)
{
	u_int mlen;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_COBISETUP, cx, &mlen))
		return (-2);

	if((p->facility.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_FACILITY, msg, (union uni_ieall *)&p->facility, cx))
		return (UNI_IE_FACILITY);
	if((p->called.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLED, msg, (union uni_ieall *)&p->called, cx))
		return (UNI_IE_CALLED);
	if((p->calledsub.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLEDSUB, msg, (union uni_ieall *)&p->calledsub, cx))
		return (UNI_IE_CALLEDSUB);
	if((p->calling.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLING, msg, (union uni_ieall *)&p->calling, cx))
		return (UNI_IE_CALLING);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_cobisetup(struct uni_cobisetup *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	switch (ie) {

	  case UNI_IE_FACILITY:
		out->facility.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_FACILITY, (union uni_ieall *)&out->facility, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLED:
		out->called.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLED, (union uni_ieall *)&out->called, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLEDSUB:
		out->calledsub.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLEDSUB, (union uni_ieall *)&out->calledsub, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLING:
		out->calling.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLING, (union uni_ieall *)&out->calling, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_cobisetup = {
	0,
	"cobisetup",
	(uni_msg_print_f)print_cobisetup,
	(uni_msg_check_f)check_cobisetup,
	(uni_msg_encode_f)encode_cobisetup,
	(uni_msg_decode_f)decode_cobisetup
};

static void
print_facility(struct uni_facility *msg, struct unicx *cx)
{
	if(msg->facility.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_FACILITY, (union uni_ieall *)&msg->facility, cx);
	if(msg->called.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLED, (union uni_ieall *)&msg->called, cx);
	if(msg->calledsub.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLEDSUB, (union uni_ieall *)&msg->calledsub, cx);
	if(msg->calling.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CALLING, (union uni_ieall *)&msg->calling, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_facility(struct uni_facility *m, struct unicx *cx)
{
	int ret = 0;

	ret |= uni_check_ie(UNI_IE_FACILITY, (union uni_ieall *)&m->facility, cx);
	ret |= uni_check_ie(UNI_IE_CALLED, (union uni_ieall *)&m->called, cx);
	ret |= uni_check_ie(UNI_IE_CALLEDSUB, (union uni_ieall *)&m->calledsub, cx);
	ret |= uni_check_ie(UNI_IE_CALLING, (union uni_ieall *)&m->calling, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_facility(struct uni_msg *msg, struct uni_facility *p, struct unicx *cx)
{
	u_int mlen;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_FACILITY, cx, &mlen))
		return (-2);

	if((p->facility.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_FACILITY, msg, (union uni_ieall *)&p->facility, cx))
		return (UNI_IE_FACILITY);
	if((p->called.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLED, msg, (union uni_ieall *)&p->called, cx))
		return (UNI_IE_CALLED);
	if((p->calledsub.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLEDSUB, msg, (union uni_ieall *)&p->calledsub, cx))
		return (UNI_IE_CALLEDSUB);
	if((p->calling.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CALLING, msg, (union uni_ieall *)&p->calling, cx))
		return (UNI_IE_CALLING);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_facility(struct uni_facility *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	switch (ie) {

	  case UNI_IE_FACILITY:
		out->facility.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_FACILITY, (union uni_ieall *)&out->facility, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLED:
		out->called.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLED, (union uni_ieall *)&out->called, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLEDSUB:
		out->calledsub.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLEDSUB, (union uni_ieall *)&out->calledsub, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_CALLING:
		out->calling.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CALLING, (union uni_ieall *)&out->calling, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_facility = {
	0,
	"facility",
	(uni_msg_print_f)print_facility,
	(uni_msg_check_f)check_facility,
	(uni_msg_encode_f)encode_facility,
	(uni_msg_decode_f)decode_facility
};

static void
print_modify_req(struct uni_modify_req *msg, struct unicx *cx)
{
	u_int i;

	if(msg->traffic.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_TRAFFIC, (union uni_ieall *)&msg->traffic, cx);
	if(msg->atraffic.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_ATRAFFIC, (union uni_ieall *)&msg->atraffic, cx);
	if(msg->mintraffic.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_MINTRAFFIC, (union uni_ieall *)&msg->mintraffic, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_modify_req(struct uni_modify_req *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_TRAFFIC, (union uni_ieall *)&m->traffic, cx);
	ret |= uni_check_ie(UNI_IE_ATRAFFIC, (union uni_ieall *)&m->atraffic, cx);
	ret |= uni_check_ie(UNI_IE_MINTRAFFIC, (union uni_ieall *)&m->mintraffic, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_modify_req(struct uni_msg *msg, struct uni_modify_req *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_MODIFY_REQ, cx, &mlen))
		return (-2);

	if((p->traffic.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_TRAFFIC, msg, (union uni_ieall *)&p->traffic, cx))
		return (UNI_IE_TRAFFIC);
	if((p->atraffic.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_ATRAFFIC, msg, (union uni_ieall *)&p->atraffic, cx))
		return (UNI_IE_ATRAFFIC);
	if((p->mintraffic.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_MINTRAFFIC, msg, (union uni_ieall *)&p->mintraffic, cx))
		return (UNI_IE_MINTRAFFIC);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_modify_req(struct uni_modify_req *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_TRAFFIC:
		out->traffic.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_TRAFFIC, (union uni_ieall *)&out->traffic, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_ATRAFFIC:
		out->atraffic.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_ATRAFFIC, (union uni_ieall *)&out->atraffic, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_MINTRAFFIC:
		out->mintraffic.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_MINTRAFFIC, (union uni_ieall *)&out->mintraffic, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_modify_req = {
	0,
	"modify_req",
	(uni_msg_print_f)print_modify_req,
	(uni_msg_check_f)check_modify_req,
	(uni_msg_encode_f)encode_modify_req,
	(uni_msg_decode_f)decode_modify_req
};

static void
print_modify_ack(struct uni_modify_ack *msg, struct unicx *cx)
{
	u_int i;

	if(msg->report.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_REPORT, (union uni_ieall *)&msg->report, cx);
	if(msg->traffic.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_TRAFFIC, (union uni_ieall *)&msg->traffic, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_modify_ack(struct uni_modify_ack *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_REPORT, (union uni_ieall *)&m->report, cx);
	ret |= uni_check_ie(UNI_IE_TRAFFIC, (union uni_ieall *)&m->traffic, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_modify_ack(struct uni_msg *msg, struct uni_modify_ack *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_MODIFY_ACK, cx, &mlen))
		return (-2);

	if((p->report.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_REPORT, msg, (union uni_ieall *)&p->report, cx))
		return (UNI_IE_REPORT);
	if((p->traffic.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_TRAFFIC, msg, (union uni_ieall *)&p->traffic, cx))
		return (UNI_IE_TRAFFIC);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_modify_ack(struct uni_modify_ack *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_REPORT:
		out->report.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_REPORT, (union uni_ieall *)&out->report, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_TRAFFIC:
		out->traffic.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_TRAFFIC, (union uni_ieall *)&out->traffic, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_modify_ack = {
	0,
	"modify_ack",
	(uni_msg_print_f)print_modify_ack,
	(uni_msg_check_f)check_modify_ack,
	(uni_msg_encode_f)encode_modify_ack,
	(uni_msg_decode_f)decode_modify_ack
};

static void
print_modify_rej(struct uni_modify_rej *msg, struct unicx *cx)
{
	u_int i;

	if(msg->cause.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_CAUSE, (union uni_ieall *)&msg->cause, cx);
	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_modify_rej(struct uni_modify_rej *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_CAUSE, (union uni_ieall *)&m->cause, cx);
	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_modify_rej(struct uni_msg *msg, struct uni_modify_rej *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_MODIFY_REJ, cx, &mlen))
		return (-2);

	if((p->cause.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_CAUSE, msg, (union uni_ieall *)&p->cause, cx))
		return (UNI_IE_CAUSE);
	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_modify_rej(struct uni_modify_rej *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_CAUSE:
		out->cause.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_CAUSE, (union uni_ieall *)&out->cause, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_modify_rej = {
	0,
	"modify_rej",
	(uni_msg_print_f)print_modify_rej,
	(uni_msg_check_f)check_modify_rej,
	(uni_msg_encode_f)encode_modify_rej,
	(uni_msg_decode_f)decode_modify_rej
};

static void
print_conn_avail(struct uni_conn_avail *msg, struct unicx *cx)
{
	u_int i;

	if(msg->notify.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_NOTIFY, (union uni_ieall *)&msg->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if(msg->git[i].h.present & UNI_IE_PRESENT)
			uni_print_ie_internal(UNI_IE_GIT, (union uni_ieall *)&msg->git[i], cx);
	if(msg->report.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_REPORT, (union uni_ieall *)&msg->report, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_conn_avail(struct uni_conn_avail *m, struct unicx *cx)
{
	int ret = 0;
	u_int i;

	ret |= uni_check_ie(UNI_IE_NOTIFY, (union uni_ieall *)&m->notify, cx);
	for(i = 0; i < UNI_NUM_IE_GIT ; i++) {
		ret |= uni_check_ie(UNI_IE_GIT, (union uni_ieall *)&m->git[i], cx);
	}
	ret |= uni_check_ie(UNI_IE_REPORT, (union uni_ieall *)&m->report, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_conn_avail(struct uni_msg *msg, struct uni_conn_avail *p, struct unicx *cx)
{
	u_int mlen;
	u_int i;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_CONN_AVAIL, cx, &mlen))
		return (-2);

	if((p->notify.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_NOTIFY, msg, (union uni_ieall *)&p->notify, cx))
		return (UNI_IE_NOTIFY);
	for(i = 0; i < UNI_NUM_IE_GIT; i++)
		if((p->git[i].h.present & UNI_IE_PRESENT) &&
		   uni_encode_ie(UNI_IE_GIT, msg, (union uni_ieall *)&p->git[i], cx))
		return ((i << 16) + UNI_IE_GIT);
	if((p->report.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_REPORT, msg, (union uni_ieall *)&p->report, cx))
		return (UNI_IE_REPORT);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_conn_avail(struct uni_conn_avail *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	u_int i;

	switch (ie) {

	  case UNI_IE_NOTIFY:
		out->notify.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_NOTIFY, (union uni_ieall *)&out->notify, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_GIT:
		for(i = 0; i < UNI_NUM_IE_GIT; i++)
			if (!IE_ISPRESENT(out->git[i])) {
				out->git[i].h = *hdr;
				if (hdr->present & UNI_IE_ERROR)
					return (DEC_ERR);
				if(uni_decode_ie_body(UNI_IE_GIT, (union uni_ieall *)&out->git[i], msg, ielen, cx))
					return (DEC_ERR);
				break;
			}
		break;

	  case UNI_IE_REPORT:
		out->report.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_REPORT, (union uni_ieall *)&out->report, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_conn_avail = {
	0,
	"conn_avail",
	(uni_msg_print_f)print_conn_avail,
	(uni_msg_check_f)check_conn_avail,
	(uni_msg_encode_f)encode_conn_avail,
	(uni_msg_decode_f)decode_conn_avail
};

static void
print_unknown(struct uni_unknown *msg, struct unicx *cx)
{
	if(msg->epref.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_EPREF, (union uni_ieall *)&msg->epref, cx);
	if(msg->unrec.h.present & UNI_IE_PRESENT)
		uni_print_ie_internal(UNI_IE_UNREC, (union uni_ieall *)&msg->unrec, cx);
}

static int
check_unknown(struct uni_unknown *m, struct unicx *cx)
{
	int ret = 0;

	ret |= uni_check_ie(UNI_IE_EPREF, (union uni_ieall *)&m->epref, cx);
	ret |= uni_check_ie(UNI_IE_UNREC, (union uni_ieall *)&m->unrec, cx);

	return ret;
}

static int
encode_unknown(struct uni_msg *msg, struct uni_unknown *p, struct unicx *cx)
{
	u_int mlen;

	if(uni_encode_msg_hdr(msg, &p->hdr, UNI_UNKNOWN, cx, &mlen))
		return (-2);

	if((p->epref.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_EPREF, msg, (union uni_ieall *)&p->epref, cx))
		return (UNI_IE_EPREF);
	if((p->unrec.h.present & UNI_IE_PRESENT) &&
	   uni_encode_ie(UNI_IE_UNREC, msg, (union uni_ieall *)&p->unrec, cx))
		return (UNI_IE_UNREC);

	msg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;
	msg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;

	return (0);
}

static int
decode_unknown(struct uni_unknown *out, struct uni_msg *msg,
    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,
    struct unicx *cx)
{
	switch (ie) {

	  case UNI_IE_EPREF:
		out->epref.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_EPREF, (union uni_ieall *)&out->epref, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  case UNI_IE_UNREC:
		out->unrec.h = *hdr;
		if (hdr->present & UNI_IE_ERROR)
			return (DEC_ERR);
		if(uni_decode_ie_body(UNI_IE_UNREC, (union uni_ieall *)&out->unrec, msg, ielen, cx))
			return (DEC_ERR);
		break;

	  default:
		return (DEC_ILL);
	}
	return (DEC_OK);
}

static const struct msgdecl decl_unknown = {
	0,
	"unknown",
	(uni_msg_print_f)print_unknown,
	(uni_msg_check_f)check_unknown,
	(uni_msg_encode_f)encode_unknown,
	(uni_msg_decode_f)decode_unknown
};

const struct msgdecl *uni_msgtable[256] = {
	&decl_unknown,	/* 0x00 */
	&decl_alerting,	/* 0x01 */
	&decl_call_proc,	/* 0x02 */
	&decl_unknown,	/* 0x03 */
	&decl_unknown,	/* 0x04 */
	&decl_setup,	/* 0x05 */
	&decl_unknown,	/* 0x06 */
	&decl_connect,	/* 0x07 */
	&decl_unknown,	/* 0x08 */
	&decl_unknown,	/* 0x09 */
	&decl_unknown,	/* 0x0a */
	&decl_unknown,	/* 0x0b */
	&decl_unknown,	/* 0x0c */
	&decl_unknown,	/* 0x0d */
	&decl_unknown,	/* 0x0e */
	&decl_connect_ack,	/* 0x0f */
	&decl_unknown,	/* 0x10 */
	&decl_unknown,	/* 0x11 */
	&decl_unknown,	/* 0x12 */
	&decl_unknown,	/* 0x13 */
	&decl_unknown,	/* 0x14 */
	&decl_cobisetup,	/* 0x15 */
	&decl_unknown,	/* 0x16 */
	&decl_unknown,	/* 0x17 */
	&decl_unknown,	/* 0x18 */
	&decl_unknown,	/* 0x19 */
	&decl_unknown,	/* 0x1a */
	&decl_unknown,	/* 0x1b */
	&decl_unknown,	/* 0x1c */
	&decl_unknown,	/* 0x1d */
	&decl_unknown,	/* 0x1e */
	&decl_unknown,	/* 0x1f */
	&decl_unknown,	/* 0x20 */
	&decl_unknown,	/* 0x21 */
	&decl_unknown,	/* 0x22 */
	&decl_unknown,	/* 0x23 */
	&decl_unknown,	/* 0x24 */
	&decl_unknown,	/* 0x25 */
	&decl_unknown,	/* 0x26 */
	&decl_unknown,	/* 0x27 */
	&decl_unknown,	/* 0x28 */
	&decl_unknown,	/* 0x29 */
	&decl_unknown,	/* 0x2a */
	&decl_unknown,	/* 0x2b */
	&decl_unknown,	/* 0x2c */
	&decl_unknown,	/* 0x2d */
	&decl_unknown,	/* 0x2e */
	&decl_unknown,	/* 0x2f */
	&decl_unknown,	/* 0x30 */
	&decl_unknown,	/* 0x31 */
	&decl_unknown,	/* 0x32 */
	&decl_unknown,	/* 0x33 */
	&decl_unknown,	/* 0x34 */
	&decl_unknown,	/* 0x35 */
	&decl_unknown,	/* 0x36 */
	&decl_unknown,	/* 0x37 */
	&decl_unknown,	/* 0x38 */
	&decl_unknown,	/* 0x39 */
	&decl_unknown,	/* 0x3a */
	&decl_unknown,	/* 0x3b */
	&decl_unknown,	/* 0x3c */
	&decl_unknown,	/* 0x3d */
	&decl_unknown,	/* 0x3e */
	&decl_unknown,	/* 0x3f */
	&decl_unknown,	/* 0x40 */
	&decl_unknown,	/* 0x41 */
	&decl_unknown,	/* 0x42 */
	&decl_unknown,	/* 0x43 */
	&decl_unknown,	/* 0x44 */
	&decl_unknown,	/* 0x45 */
	&decl_restart,	/* 0x46 */
	&decl_unknown,	/* 0x47 */
	&decl_unknown,	/* 0x48 */
	&decl_unknown,	/* 0x49 */
	&decl_unknown,	/* 0x4a */
	&decl_unknown,	/* 0x4b */
	&decl_unknown,	/* 0x4c */
	&decl_release,	/* 0x4d */
	&decl_restart_ack,	/* 0x4e */
	&decl_unknown,	/* 0x4f */
	&decl_unknown,	/* 0x50 */
	&decl_unknown,	/* 0x51 */
	&decl_unknown,	/* 0x52 */
	&decl_unknown,	/* 0x53 */
	&decl_unknown,	/* 0x54 */
	&decl_unknown,	/* 0x55 */
	&decl_unknown,	/* 0x56 */
	&decl_unknown,	/* 0x57 */
	&decl_unknown,	/* 0x58 */
	&decl_unknown,	/* 0x59 */
	&decl_release_compl,	/* 0x5a */
	&decl_unknown,	/* 0x5b */
	&decl_unknown,	/* 0x5c */
	&decl_unknown,	/* 0x5d */
	&decl_unknown,	/* 0x5e */
	&decl_unknown,	/* 0x5f */
	&decl_unknown,	/* 0x60 */
	&decl_unknown,	/* 0x61 */
	&decl_facility,	/* 0x62 */
	&decl_unknown,	/* 0x63 */
	&decl_unknown,	/* 0x64 */
	&decl_unknown,	/* 0x65 */
	&decl_unknown,	/* 0x66 */
	&decl_unknown,	/* 0x67 */
	&decl_unknown,	/* 0x68 */
	&decl_unknown,	/* 0x69 */
	&decl_unknown,	/* 0x6a */
	&decl_unknown,	/* 0x6b */
	&decl_unknown,	/* 0x6c */
	&decl_unknown,	/* 0x6d */
	&decl_notify,	/* 0x6e */
	&decl_unknown,	/* 0x6f */
	&decl_unknown,	/* 0x70 */
	&decl_unknown,	/* 0x71 */
	&decl_unknown,	/* 0x72 */
	&decl_unknown,	/* 0x73 */
	&decl_unknown,	/* 0x74 */
	&decl_status_enq,	/* 0x75 */
	&decl_unknown,	/* 0x76 */
	&decl_unknown,	/* 0x77 */
	&decl_unknown,	/* 0x78 */
	&decl_unknown,	/* 0x79 */
	&decl_unknown,	/* 0x7a */
	&decl_unknown,	/* 0x7b */
	&decl_unknown,	/* 0x7c */
	&decl_status,	/* 0x7d */
	&decl_unknown,	/* 0x7e */
	&decl_unknown,	/* 0x7f */
	&decl_add_party,	/* 0x80 */
	&decl_add_party_ack,	/* 0x81 */
	&decl_add_party_rej,	/* 0x82 */
	&decl_drop_party,	/* 0x83 */
	&decl_drop_party_ack,	/* 0x84 */
	&decl_party_alerting,	/* 0x85 */
	&decl_unknown,	/* 0x86 */
	&decl_unknown,	/* 0x87 */
	&decl_modify_req,	/* 0x88 */
	&decl_modify_ack,	/* 0x89 */
	&decl_modify_rej,	/* 0x8a */
	&decl_conn_avail,	/* 0x8b */
	&decl_unknown,	/* 0x8c */
	&decl_unknown,	/* 0x8d */
	&decl_unknown,	/* 0x8e */
	&decl_unknown,	/* 0x8f */
	&decl_leaf_setup_fail,	/* 0x90 */
	&decl_leaf_setup_req,	/* 0x91 */
	&decl_unknown,	/* 0x92 */
	&decl_unknown,	/* 0x93 */
	&decl_unknown,	/* 0x94 */
	&decl_unknown,	/* 0x95 */
	&decl_unknown,	/* 0x96 */
	&decl_unknown,	/* 0x97 */
	&decl_unknown,	/* 0x98 */
	&decl_unknown,	/* 0x99 */
	&decl_unknown,	/* 0x9a */
	&decl_unknown,	/* 0x9b */
	&decl_unknown,	/* 0x9c */
	&decl_unknown,	/* 0x9d */
	&decl_unknown,	/* 0x9e */
	&decl_unknown,	/* 0x9f */
	&decl_unknown,	/* 0xa0 */
	&decl_unknown,	/* 0xa1 */
	&decl_unknown,	/* 0xa2 */
	&decl_unknown,	/* 0xa3 */
	&decl_unknown,	/* 0xa4 */
	&decl_unknown,	/* 0xa5 */
	&decl_unknown,	/* 0xa6 */
	&decl_unknown,	/* 0xa7 */
	&decl_unknown,	/* 0xa8 */
	&decl_unknown,	/* 0xa9 */
	&decl_unknown,	/* 0xaa */
	&decl_unknown,	/* 0xab */
	&decl_unknown,	/* 0xac */
	&decl_unknown,	/* 0xad */
	&decl_unknown,	/* 0xae */
	&decl_unknown,	/* 0xaf */
	&decl_unknown,	/* 0xb0 */
	&decl_unknown,	/* 0xb1 */
	&decl_unknown,	/* 0xb2 */
	&decl_unknown,	/* 0xb3 */
	&decl_unknown,	/* 0xb4 */
	&decl_unknown,	/* 0xb5 */
	&decl_unknown,	/* 0xb6 */
	&decl_unknown,	/* 0xb7 */
	&decl_unknown,	/* 0xb8 */
	&decl_unknown,	/* 0xb9 */
	&decl_unknown,	/* 0xba */
	&decl_unknown,	/* 0xbb */
	&decl_unknown,	/* 0xbc */
	&decl_unknown,	/* 0xbd */
	&decl_unknown,	/* 0xbe */
	&decl_unknown,	/* 0xbf */
	&decl_unknown,	/* 0xc0 */
	&decl_unknown,	/* 0xc1 */
	&decl_unknown,	/* 0xc2 */
	&decl_unknown,	/* 0xc3 */
	&decl_unknown,	/* 0xc4 */
	&decl_unknown,	/* 0xc5 */
	&decl_unknown,	/* 0xc6 */
	&decl_unknown,	/* 0xc7 */
	&decl_unknown,	/* 0xc8 */
	&decl_unknown,	/* 0xc9 */
	&decl_unknown,	/* 0xca */
	&decl_unknown,	/* 0xcb */
	&decl_unknown,	/* 0xcc */
	&decl_unknown,	/* 0xcd */
	&decl_unknown,	/* 0xce */
	&decl_unknown,	/* 0xcf */
	&decl_unknown,	/* 0xd0 */
	&decl_unknown,	/* 0xd1 */
	&decl_unknown,	/* 0xd2 */
	&decl_unknown,	/* 0xd3 */
	&decl_unknown,	/* 0xd4 */
	&decl_unknown,	/* 0xd5 */
	&decl_unknown,	/* 0xd6 */
	&decl_unknown,	/* 0xd7 */
	&decl_unknown,	/* 0xd8 */
	&decl_unknown,	/* 0xd9 */
	&decl_unknown,	/* 0xda */
	&decl_unknown,	/* 0xdb */
	&decl_unknown,	/* 0xdc */
	&decl_unknown,	/* 0xdd */
	&decl_unknown,	/* 0xde */
	&decl_unknown,	/* 0xdf */
	&decl_unknown,	/* 0xe0 */
	&decl_unknown,	/* 0xe1 */
	&decl_unknown,	/* 0xe2 */
	&decl_unknown,	/* 0xe3 */
	&decl_unknown,	/* 0xe4 */
	&decl_unknown,	/* 0xe5 */
	&decl_unknown,	/* 0xe6 */
	&decl_unknown,	/* 0xe7 */
	&decl_unknown,	/* 0xe8 */
	&decl_unknown,	/* 0xe9 */
	&decl_unknown,	/* 0xea */
	&decl_unknown,	/* 0xeb */
	&decl_unknown,	/* 0xec */
	&decl_unknown,	/* 0xed */
	&decl_unknown,	/* 0xee */
	&decl_unknown,	/* 0xef */
	&decl_unknown,	/* 0xf0 */
	&decl_unknown,	/* 0xf1 */
	&decl_unknown,	/* 0xf2 */
	&decl_unknown,	/* 0xf3 */
	&decl_unknown,	/* 0xf4 */
	&decl_unknown,	/* 0xf5 */
	&decl_unknown,	/* 0xf6 */
	&decl_unknown,	/* 0xf7 */
	&decl_unknown,	/* 0xf8 */
	&decl_unknown,	/* 0xf9 */
	&decl_unknown,	/* 0xfa */
	&decl_unknown,	/* 0xfb */
	&decl_unknown,	/* 0xfc */
	&decl_unknown,	/* 0xfd */
	&decl_unknown,	/* 0xfe */
	&decl_unknown,	/* 0xff */
};
