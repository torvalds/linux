/*
 * Copyright (c) 2001-2003
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
 * $Begemot: libunimsg/netnatm/sig/sig_verify.c,v 1.19 2004/07/08 08:22:23 brandt Exp $
 *
 * Message verification with explicit action indicators.
 */

#include <netnatm/unimsg.h>
#include <netnatm/saal/sscfudef.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/sig/uni.h>

#include <netnatm/sig/unipriv.h>
#include <netnatm/sig/unimkmsg.h>

void
uni_mandate_ie(struct uni *uni, enum uni_ietype ie)
{
	struct uni_ierr *e;

	FOREACH_ERR(e, uni)
		if (e->ie == ie) {
			e->man = 1;
			return;
		}
	if (UNI_SAVE_IERR(&uni->cx, ie, UNI_IEACT_DEFAULT, UNI_IERR_MIS))
		uni->cx.err[uni->cx.errcnt - 1].man = 1;
}

/*
 * This special handling is required for ADD PARTY, PARTY ALERTING and
 * ADD PARTY ACKNOWLEDGE by Q.2971 9.5.3.2.1.
 * It means, that the EPREF should be handled as mandatory only if
 * no other IEs have explicit action indicators.
 */
void
uni_mandate_epref(struct uni *uni, struct uni_ie_epref *epref)
{
	struct uni_ierr *e;
	int maxact;

	if (!IE_ISPRESENT(*epref)) {
		/*
		 * 9.5.3.2.1 -- missing endpoint reference
		 */

		/*
		 * a) if any unrecognized or IE with error has a CLEAR
		 *    action indicator, this takes precedence.
		 * b) if any unrecognized or IE with error has a
		 *    discard message and report action indicator, this takes
		 *    precedence.
		 * c) if any unrecognized or IE with error has a
		 *    discard message action indicator, this takes
		 *    precedence.
		 *
		 * In any of these cases we must remove the EPREF IE
		 * if it has CLEAR, otherwise the CLEAR would take over.
		 */
		maxact = -1;
		FOREACH_ERR(e, uni) {
			if (e->ie == UNI_IE_EPREF)
				continue;
			if (e->act == UNI_IEACT_CLEAR)
				maxact = UNI_IEACT_CLEAR;
			else if (e->act == UNI_IEACT_MSG_REPORT) {
				if (maxact == -1 && maxact != UNI_IEACT_CLEAR)
					maxact = UNI_IEACT_MSG_REPORT;
			} else if (e->act == UNI_IEACT_MSG_IGNORE) {
				if (maxact == -1)
					maxact = UNI_IEACT_MSG_IGNORE;
			}
		}

		if (maxact != -1) {
			/* ok, second pass to remove UNI_IE_EPREF */
			FOREACH_ERR(e, uni)
				if (e->ie == UNI_IE_EPREF) {
					memmove(e, e + 1,
					    (uni->cx.errcnt - (e - uni->cx.err)
					    - 1) * sizeof(uni->cx.err[0]));
					uni->cx.errcnt--;
					break;
				}
			return;

		}

		/*
		 * d) if nothing of the above, the IE is mandatory
		 */
		uni_mandate_ie(uni, UNI_IE_EPREF);
		return;
		
	}
	if (IE_ISGOOD(*epref))
		return;

	/*
	 * It has an error obviously
	 * 9.5.3.2.2
	 *
	 * It turns out, that Q.2931 handling just does the right thing
	 * if we don't mandate the IE.
	 */
	return;
}

/*
 * Look, what to do with this message. We assume, that the message itself is
 * recognized.
 *
 * This is rather complicated. We must use the information provided in the
 * fields of the context, because IEs with length errors may not be set
 * altogether.
 */
enum verify
uni_verify(struct uni *uni, enum uni_msgact msgact)
{
	struct uni_ierr *e1;

	if (uni->debug[UNI_FAC_VERIFY] >= 2) {
		FOREACH_ERR(e1, uni) {
			VERBOSE(uni, UNI_FAC_VERIFY, 2, "ie=%02x err=%u man=%d"
			    " act=%u", e1->ie, e1->err, e1->man, e1->act);
		}
	}

	/*
	 * Look for missing mandatory IEs. The action indicator is ignored
	 * according to 5.6.7.1. If IEs are missing the action is to
	 * ignore the message and report status for all messages except
	 * RELEASE, RELEASE_COMPLETE and SETUP. Because we must differentiate
	 * this RAI from other RAIs in this case, use another return code.
	 * Note, that mandatory IEs with errors are not handled here.
	 */
	FOREACH_ERR(e1, uni) {
		if (e1->err == UNI_IERR_MIS) {
			MK_IE_CAUSE(uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_MANDAT);
			VERBOSE(uni, UNI_FAC_VERIFY, 1, "RAIM");
			return (VFY_RAIM);
		}
	}

	/*
	 * When any IE with error specifies a CLR action indicator, this
	 * takes precedence obviously. There are two cases here:
	 * unrecognized IEs and IEs with error. So we look through the
	 * error array twice and send only one STATUS. Unrecognized will
	 * take precedence.
	 *
	 * 5.7.2a)
	 */
	FOREACH_ERR(e1, uni) {
		if (e1->act == UNI_IEACT_CLEAR && e1->err == UNI_IERR_UNK) {
			MK_IE_CAUSE(uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_IE_NIMPL);
			VERBOSE(uni, UNI_FAC_VERIFY, 1, "CLR1");
			return (VFY_CLR);
		}
	}

	FOREACH_ERR(e1, uni) {
		if (e1->act == UNI_IEACT_CLEAR &&
		   (e1->err == UNI_IERR_LEN || e1->err == UNI_IERR_BAD ||
		    e1->err == UNI_IERR_ACC)) {
			MK_IE_CAUSE(uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_IE_INV);
			VERBOSE(uni, UNI_FAC_VERIFY, 1, "CLR2");
			return (VFY_CLR);
		}
	}

	/*
	 * Now check, whether anybody wants to explicitly ignore the message
	 * and report status.
	 *
	 * 5.7.2a)
	 */
	FOREACH_ERR(e1, uni) {
		if (e1->act == UNI_IEACT_MSG_REPORT && e1->err == UNI_IERR_UNK) {
			MK_IE_CAUSE(uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_IE_NIMPL);
			VERBOSE(uni, UNI_FAC_VERIFY, 1, "RAI");
			return (VFY_RAI);
		}
	}

	FOREACH_ERR(e1, uni) {
		if (e1->act == UNI_IEACT_MSG_REPORT &&
		   (e1->err == UNI_IERR_LEN || e1->err == UNI_IERR_BAD ||
		    e1->err == UNI_IERR_ACC)) {
			MK_IE_CAUSE(uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_IE_INV);
			VERBOSE(uni, UNI_FAC_VERIFY, 1, "RAI");
			return (VFY_RAI);
		}
	}

	/*
	 * Now look whether some IE wants to explicitely ignore the message
	 * without any report.
	 */
	FOREACH_ERR(e1, uni) {
		if (e1->act == UNI_IEACT_MSG_IGNORE) {
			VERBOSE(uni, UNI_FAC_VERIFY, 1, "I1");
			return (VFY_I);
		}
	}

	/*
	 * At this point we have left only
	 *  mandatory and non-mandatory IEs with error that want the IE to be
	 *  ignored or ignored with report or defaulted.
	 * Because a mandatory IE with errors lead to
	 * the message beeing ignored, we make this of higher
	 * precedence, than the rest.
	 */
	FOREACH_ERR(e1, uni) {
		if (e1->man) {
			MK_IE_CAUSE(uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_MANDAT);
			VERBOSE(uni, UNI_FAC_VERIFY, 1, "RAI");
			return (VFY_RAI);
		}
	}

	/*
	 * Now look for ignoring the IE and reporting. This takes precedence
	 * over simply ignoring it. We also collect defaulted (non-mandatory)
	 * IEs.
	 *
	 * 5.7.2d) and 5.6.8.1
	 */
	FOREACH_ERR(e1, uni) {
		if ((e1->act == UNI_IEACT_DEFAULT ||
		     e1->act == UNI_IEACT_REPORT)
		    && e1->err != UNI_IERR_UNK) {
			MK_IE_CAUSE(uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_IE_INV);
			VERBOSE(uni, UNI_FAC_VERIFY, 1, "RAP");
			return (VFY_RAP);
		}
	}

	FOREACH_ERR(e1, uni) {
		if ((e1->act == UNI_IEACT_DEFAULT ||
		     e1->act == UNI_IEACT_REPORT)
		    && e1->err == UNI_IERR_UNK) {
			MK_IE_CAUSE(uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_IE_NIMPL);
			VERBOSE(uni, UNI_FAC_VERIFY, 1, "RAPU");
			return (VFY_RAPU);
		}
	}

	/*
	 * This leaves us with IEs, that want to be ignored. Among these may
	 * be mandatory IEs. If we have an mandatory IEs here in the error
	 * array, then the message wil not contain enough information and
	 * must be handled according to 5.8 as either in 5.6.7.1 (this
	 * means, that mandatory IEs cannot really be ignored) or 5.7.1.
	 */
	FOREACH_ERR(e1, uni) {
		if (e1->man) {
			MK_IE_CAUSE(uni->cause, UNI_CAUSE_LOC_USER,
			    UNI_CAUSE_MANDAT);
			if (msgact == UNI_MSGACT_CLEAR) {
				VERBOSE(uni, UNI_FAC_VERIFY, 1, "CLR3");
				return (VFY_CLR);
			}
			if (msgact == UNI_MSGACT_IGNORE) {
				VERBOSE(uni, UNI_FAC_VERIFY, 1, "I2");
				return (VFY_I);
			}
			VERBOSE(uni, UNI_FAC_VERIFY, 1, "RAI");
			return (VFY_RAI);
		}
	}

	/*
	 * Now only non-mandatory IEs are left, that want to be explicitely
	 * ignored.
	 */
	if (uni->cx.errcnt != 0)
		MK_IE_CAUSE(uni->cause, UNI_CAUSE_LOC_USER,
		    UNI_CAUSE_IE_INV);

	VERBOSE(uni, UNI_FAC_VERIFY, 1, "OK");
	return (VFY_OK);
}

/*
 * Collect the IE identifiers for some of the known cause codes.
 */
void
uni_vfy_collect_ies(struct uni *uni)
{
	struct uni_ierr *e;

#define STUFF_IE(IE)						\
	uni->cause.u.ie.ie[uni->cause.u.ie.len++] = (IE);	\
	if (uni->cause.u.ie.len == UNI_CAUSE_IE_N)		\
		break;

	uni->cause.u.ie.len = 0;
	if (uni->cause.cause == UNI_CAUSE_MANDAT) {
		FOREACH_ERR(e, uni) {
			if (e->err == UNI_IERR_MIS || e->man != 0) {
				STUFF_IE(e->ie);
			}
		}

	} else if (uni->cause.cause == UNI_CAUSE_IE_NIMPL) {
		FOREACH_ERR(e, uni) {
			if (e->err == UNI_IERR_UNK) {
				STUFF_IE(e->ie);
			}
		}

	} else if (uni->cause.cause == UNI_CAUSE_IE_INV) {
		FOREACH_ERR(e, uni) {
			if (e->err == UNI_IERR_LEN ||
			    e->err == UNI_IERR_BAD ||
			    e->err == UNI_IERR_ACC) {
				STUFF_IE(e->ie);
			}
		}
	} else
		return;

	if (uni->cause.u.ie.len != 0)
		uni->cause.h.present |= UNI_CAUSE_IE_P;
}


void
uni_respond_status_verify(struct uni *uni, struct uni_cref *cref,
    enum uni_callstate cs, struct uni_ie_epref *epref,
    enum uni_epstate ps)
{
	struct uni_all *resp;

	if ((resp = UNI_ALLOC()) == NULL)
		return;

	uni_vfy_collect_ies(uni);

	MK_MSG_RESP(resp, UNI_STATUS, cref);
	MK_IE_CALLSTATE(resp->u.status.callstate, cs);
	resp->u.status.cause = uni->cause;
	if (epref && IE_ISGOOD(*epref)) {
		MK_IE_EPREF(resp->u.status.epref, epref->epref, !epref->flag);
		MK_IE_EPSTATE(resp->u.status.epstate, ps);
	}

	uni_send_output(resp, uni);

	UNI_FREE(resp);
}

/*
 * Handling of Q.2971 9.5.8.1:
 */
void
uni_vfy_remove_unknown(struct uni *uni)
{
	struct uni_ierr *e1, *e0;
	int flag = 0;

	FOREACH_ERR(e1, uni) {
		if (e1->err == UNI_IERR_UNK) {
			if (e1->act == UNI_IEACT_CLEAR ||
			    e1->act == UNI_IEACT_MSG_IGNORE ||
			    e1->act == UNI_IEACT_MSG_REPORT)
				return;
			if (e1->act == UNI_IEACT_REPORT ||
			    e1->act == UNI_IEACT_DEFAULT)
				flag = 1;
		}
	}
	if (flag)
		return;
	e0 = e1 = uni->cx.err;
	while (e1 < uni->cx.err + uni->cx.errcnt) {
		if (e1->err != UNI_IERR_UNK) {
			if (e0 != e1)
				*e0 = *e1;
			e0++;
		}
		e1++;
	}
	uni->cx.errcnt = e0 - uni->cx.err;
}

/*
 * Handling for ADD_PARTY_REJ and DROP_PARTY_ACK with bad cause
 */
void
uni_vfy_remove_cause(struct uni *uni)
{
	struct uni_ierr *e1, *e0;

	e0 = e1 = uni->cx.err;
	while (e1 < uni->cx.err + uni->cx.errcnt) {
		if (e1->ie != UNI_IE_CAUSE) {
			if (e0 != e1)
				*e0 = *e1;
			e0++;
		}
		e1++;
	}
	uni->cx.errcnt = e0 - uni->cx.err;
}
