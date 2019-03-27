/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 1997-2009 by Matthew Jacob
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *
 */
/*
 * Machine and OS Independent Target Mode Code for the Qlogic SCSI/FC adapters.
 */
/*
 * Bug fixes gratefully acknowledged from:
 *	Oded Kedem <oded@kashya.com>
 */
/*
 * Include header file appropriate for platform we're building on.
 */

#ifdef	__NetBSD__
#include <dev/ic/isp_netbsd.h>
#endif
#ifdef	__FreeBSD__
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/isp/isp_freebsd.h>
#endif
#ifdef	__OpenBSD__
#include <dev/ic/isp_openbsd.h>
#endif
#ifdef	__linux__
#include "isp_linux.h"
#endif

#ifdef	ISP_TARGET_MODE
static const char atiocope[] = "ATIO returned for LUN %x because it was in the middle of Bus Device Reset on bus %d";
static const char atior[] = "ATIO returned for LUN %x from handle 0x%x because a Bus Reset occurred on bus %d";
static const char rqo[] = "%s: Request Queue Overflow";

static void isp_got_msg_fc(ispsoftc_t *, in_fcentry_t *);
static void isp_got_tmf_24xx(ispsoftc_t *, at7_entry_t *);
static void isp_handle_abts(ispsoftc_t *, abts_t *);
static void isp_handle_atio2(ispsoftc_t *, at2_entry_t *);
static void isp_handle_ctio2(ispsoftc_t *, ct2_entry_t *);
static void isp_handle_ctio7(ispsoftc_t *, ct7_entry_t *);
static void isp_handle_notify(ispsoftc_t *, in_fcentry_t *);
static void isp_handle_notify_24xx(ispsoftc_t *, in_fcentry_24xx_t *);

/*
 * The Qlogic driver gets an interrupt to look at response queue entries.
 * Some of these are status completions for initiatior mode commands, but
 * if target mode is enabled, we get a whole wad of response queue entries
 * to be handled here.
 *
 * Basically the split into 3 main groups: Lun Enable/Modification responses,
 * SCSI Command processing, and Immediate Notification events.
 *
 * You start by writing a request queue entry to enable target mode (and
 * establish some resource limitations which you can modify later).
 * The f/w responds with a LUN ENABLE or LUN MODIFY response with
 * the status of this action. If the enable was successful, you can expect...
 *
 * Response queue entries with SCSI commands encapsulate show up in an ATIO
 * (Accept Target IO) type- sometimes with enough info to stop the command at
 * this level. Ultimately the driver has to feed back to the f/w's request
 * queue a sequence of CTIOs (continue target I/O) that describe data to
 * be moved and/or status to be sent) and finally finishing with sending
 * to the f/w's response queue an ATIO which then completes the handshake
 * with the f/w for that command. There's a lot of variations on this theme,
 * including flags you can set in the CTIO for the Qlogic 2X00 fibre channel
 * cards that 'auto-replenish' the f/w's ATIO count, but this is the basic
 * gist of it.
 *
 * The third group that can show up in the response queue are Immediate
 * Notification events. These include things like notifications of SCSI bus
 * resets, or Bus Device Reset messages or other messages received. This
 * a classic oddbins area. It can get  a little weird because you then turn
 * around and acknowledge the Immediate Notify by writing an entry onto the
 * request queue and then the f/w turns around and gives you an acknowledgement
 * to *your* acknowledgement on the response queue (the idea being to let
 * the f/w tell you when the event is *really* over I guess).
 *
 */


/*
 * A new response queue entry has arrived. The interrupt service code
 * has already swizzled it into the platform dependent from canonical form.
 *
 * Because of the way this driver is designed, unfortunately most of the
 * actual synchronization work has to be done in the platform specific
 * code- we have no synchroniation primitives in the common code.
 */

int
isp_target_notify(ispsoftc_t *isp, void *vptr, uint32_t *optrp)
{
	union {
		at2_entry_t	*at2iop;
		at2e_entry_t	*at2eiop;
		at7_entry_t	*at7iop;
		ct2_entry_t	*ct2iop;
		ct2e_entry_t	*ct2eiop;
		ct7_entry_t	*ct7iop;
		lun_entry_t	*lunenp;
		in_fcentry_t	*inot_fcp;
		in_fcentry_e_t	*inote_fcp;
		in_fcentry_24xx_t *inot_24xx;
		na_fcentry_t	*nack_fcp;
		na_fcentry_e_t	*nacke_fcp;
		na_fcentry_24xx_t *nack_24xx;
		isphdr_t	*hp;
		abts_t		*abts;
		abts_rsp_t	*abts_rsp;
		els_t		*els;
		void *		*vp;
#define	at2iop		unp.at2iop
#define	at2eiop		unp.at2eiop
#define	at7iop		unp.at7iop
#define	ct2iop		unp.ct2iop
#define	ct2eiop		unp.ct2eiop
#define	ct7iop		unp.ct7iop
#define	lunenp		unp.lunenp
#define	inot_fcp	unp.inot_fcp
#define	inote_fcp	unp.inote_fcp
#define	inot_24xx	unp.inot_24xx
#define	nack_fcp	unp.nack_fcp
#define	nacke_fcp	unp.nacke_fcp
#define	nack_24xx	unp.nack_24xx
#define	abts		unp.abts
#define	abts_rsp	unp.abts_rsp
#define els		unp.els
#define	hdrp		unp.hp
	} unp;
	uint8_t local[QENTRY_LEN];
	int type, len, level, rval = 1;

	type = isp_get_response_type(isp, (isphdr_t *)vptr);
	unp.vp = vptr;

	ISP_TDQE(isp, "isp_target_notify", (int) *optrp, vptr);

	switch (type) {
	case RQSTYPE_ATIO:
		isp_get_atio7(isp, at7iop, (at7_entry_t *) local);
		at7iop = (at7_entry_t *) local;
		/*
		 * Check for and do something with commands whose
		 * IULEN extends past a single queue entry.
		 */
		len = at7iop->at_ta_len & 0x0fff;
		if (len > (QENTRY_LEN - 8)) {
			len -= (QENTRY_LEN - 8);
			isp_prt(isp, ISP_LOGINFO, "long IU length (%d) ignored", len);
			while (len > 0) {
				*optrp =  ISP_NXT_QENTRY(*optrp, RESULT_QUEUE_LEN(isp));
				len -= QENTRY_LEN;
			}
		}
		/*
		 * Check for a task management function
		 */
		if (at7iop->at_cmnd.fcp_cmnd_task_management) {
			isp_got_tmf_24xx(isp, at7iop);
			break;
		}
		/*
		 * Just go straight to outer layer for this one.
		 */
		isp_async(isp, ISPASYNC_TARGET_ACTION, local);
		break;

	case RQSTYPE_ATIO2:
		if (ISP_CAP_2KLOGIN(isp)) {
			isp_get_atio2e(isp, at2eiop, (at2e_entry_t *) local);
		} else {
			isp_get_atio2(isp, at2iop, (at2_entry_t *) local);
		}
		isp_handle_atio2(isp, (at2_entry_t *) local);
		break;

	case RQSTYPE_CTIO3:
	case RQSTYPE_CTIO2:
		if (ISP_CAP_2KLOGIN(isp)) {
			isp_get_ctio2e(isp, ct2eiop, (ct2e_entry_t *) local);
		} else {
			isp_get_ctio2(isp, ct2iop, (ct2_entry_t *) local);
		}
		isp_handle_ctio2(isp, (ct2_entry_t *) local);
		break;

	case RQSTYPE_CTIO7:
		isp_get_ctio7(isp, ct7iop, (ct7_entry_t *) local);
		isp_handle_ctio7(isp, (ct7_entry_t *) local);
		break;

	case RQSTYPE_NOTIFY:
		if (IS_24XX(isp)) {
			isp_get_notify_24xx(isp, inot_24xx, (in_fcentry_24xx_t *)local);
			isp_handle_notify_24xx(isp, (in_fcentry_24xx_t *)local);
			break;
		}
		if (ISP_CAP_2KLOGIN(isp))
			isp_get_notify_fc_e(isp, inote_fcp, (in_fcentry_e_t *)local);
		else
			isp_get_notify_fc(isp, inot_fcp, (in_fcentry_t *)local);
		isp_handle_notify(isp, (in_fcentry_t *)local);
		break;

	case RQSTYPE_NOTIFY_ACK:
		/*
		 * The ISP is acknowledging our acknowledgement of an
		 * Immediate Notify entry for some asynchronous event.
		 */
		if (IS_24XX(isp)) {
			isp_get_notify_ack_24xx(isp, nack_24xx, (na_fcentry_24xx_t *) local);
			nack_24xx = (na_fcentry_24xx_t *) local;
			if (nack_24xx->na_status != NA_OK) {
				level = ISP_LOGINFO;
			} else {
				level = ISP_LOGTDEBUG1;
			}
			isp_prt(isp, level, "Notify Ack Status=0x%x; Subcode 0x%x seqid=0x%x", nack_24xx->na_status, nack_24xx->na_status_subcode, nack_24xx->na_rxid);
		} else {
			if (ISP_CAP_2KLOGIN(isp)) {
				isp_get_notify_ack_fc_e(isp, nacke_fcp, (na_fcentry_e_t *)local);
			} else {
				isp_get_notify_ack_fc(isp, nack_fcp, (na_fcentry_t *)local);
			}
			nack_fcp = (na_fcentry_t *)local;
			if (nack_fcp->na_status != NA_OK) {
				level = ISP_LOGINFO;
			} else {
				level = ISP_LOGTDEBUG1;
			}
			isp_prt(isp, level, "Notify Ack Status=0x%x seqid 0x%x", nack_fcp->na_status, nack_fcp->na_seqid);
		}
		break;

	case RQSTYPE_ABTS_RCVD:
		isp_get_abts(isp, abts, (abts_t *)local);
		isp_handle_abts(isp, (abts_t *)local);
		break;
	case RQSTYPE_ABTS_RSP:
		isp_get_abts_rsp(isp, abts_rsp, (abts_rsp_t *)local);
		abts_rsp = (abts_rsp_t *) local;
		if (abts_rsp->abts_rsp_status) {
			level = ISP_LOGINFO;
		} else {
			level = ISP_LOGTDEBUG0;
		}
		isp_prt(isp, level, "ABTS RSP response[0x%x]: status=0x%x sub=(0x%x 0x%x)", abts_rsp->abts_rsp_rxid_task, abts_rsp->abts_rsp_status,
		    abts_rsp->abts_rsp_payload.rsp.subcode1, abts_rsp->abts_rsp_payload.rsp.subcode2);
		break;
	default:
		isp_prt(isp, ISP_LOGERR, "%s: unknown entry type 0x%x", __func__, type);
		rval = 0;
		break;
	}
#undef	atiop
#undef	at2iop
#undef	at2eiop
#undef	at7iop
#undef	ctiop
#undef	ct2iop
#undef	ct2eiop
#undef	ct7iop
#undef	lunenp
#undef	inotp
#undef	inot_fcp
#undef	inote_fcp
#undef	inot_24xx
#undef	nackp
#undef	nack_fcp
#undef	nacke_fcp
#undef	hack_24xx
#undef	abts
#undef	abts_rsp
#undef	els
#undef	hdrp
	return (rval);
}

int
isp_target_put_entry(ispsoftc_t *isp, void *ap)
{
	void *outp;
	uint8_t etype = ((isphdr_t *) ap)->rqs_entry_type;

	outp = isp_getrqentry(isp);
	if (outp == NULL) {
		isp_prt(isp, ISP_LOGWARN, rqo, __func__); 
		return (-1);
	}
	switch (etype) {
	case RQSTYPE_NOTIFY_ACK:
		if (IS_24XX(isp))
			isp_put_notify_24xx_ack(isp, (na_fcentry_24xx_t *)ap,
			    (na_fcentry_24xx_t *)outp);
		else if (ISP_CAP_2KLOGIN(isp))
			isp_put_notify_ack_fc_e(isp, (na_fcentry_e_t *)ap,
			    (na_fcentry_e_t *)outp);
		else
			isp_put_notify_ack_fc(isp, ap, (na_fcentry_t *)outp);
		break;
	case RQSTYPE_ATIO2:
		if (ISP_CAP_2KLOGIN(isp))
			isp_put_atio2e(isp, (at2e_entry_t *)ap,
			    (at2e_entry_t *)outp);
		else
			isp_put_atio2(isp, (at2_entry_t *)ap,
			    (at2_entry_t *)outp);
		break;
	case RQSTYPE_CTIO2:
		if (ISP_CAP_2KLOGIN(isp))
			isp_put_ctio2e(isp, (ct2e_entry_t *)ap,
			    (ct2e_entry_t *)outp);
		else
			isp_put_ctio2(isp, (ct2_entry_t *)ap,
			    (ct2_entry_t *)outp);
		break;
	case RQSTYPE_CTIO7:
		isp_put_ctio7(isp, (ct7_entry_t *)ap, (ct7_entry_t *)outp);
		break;
	case RQSTYPE_ABTS_RSP:
		isp_put_abts_rsp(isp, (abts_rsp_t *)ap, (abts_rsp_t *)outp);
		break;
	default:
		isp_prt(isp, ISP_LOGERR, "%s: Unknown type 0x%x", __func__, etype);
		return (-1);
	}
	ISP_TDQE(isp, __func__, isp->isp_reqidx, ap);
	ISP_SYNC_REQUEST(isp);
	return (0);
}

int
isp_target_put_atio(ispsoftc_t *isp, void *arg)
{
	at2_entry_t *aep = arg;
	union {
		at2_entry_t _atio2;
		at2e_entry_t _atio2e;
	} atun;

	ISP_MEMZERO(&atun, sizeof atun);
	atun._atio2.at_header.rqs_entry_type = RQSTYPE_ATIO2;
	atun._atio2.at_header.rqs_entry_count = 1;
	if (ISP_CAP_SCCFW(isp)) {
		atun._atio2.at_scclun = aep->at_scclun;
	} else {
		atun._atio2.at_lun = (uint8_t) aep->at_lun;
	}
	if (ISP_CAP_2KLOGIN(isp)) {
		atun._atio2e.at_iid = ((at2e_entry_t *)aep)->at_iid;
	} else {
		atun._atio2.at_iid = aep->at_iid;
	}
	atun._atio2.at_rxid = aep->at_rxid;
	atun._atio2.at_status = CT_OK;
	return (isp_target_put_entry(isp, &atun));
}

/*
 * Command completion- both for handling cases of no resources or
 * no blackhole driver, or other cases where we have to, inline,
 * finish the command sanely, or for normal command completion.
 *
 * The 'completion' code value has the scsi status byte in the low 8 bits.
 * If status is a CHECK CONDITION and bit 8 is nonzero, then bits 12..15 have
 * the sense key and  bits 16..23 have the ASCQ and bits 24..31 have the ASC
 * values.
 *
 * NB: the key, asc, ascq, cannot be used for parallel SCSI as it doesn't
 * NB: inline SCSI sense reporting. As such, we lose this information. XXX.
 *
 * For both parallel && fibre channel, we use the feature that does
 * an automatic resource autoreplenish so we don't have then later do
 * put of an atio to replenish the f/w's resource count.
 */

int
isp_endcmd(ispsoftc_t *isp, ...)
{
	uint32_t code, hdl;
	uint8_t sts;
	union {
		ct2_entry_t _ctio2;
		ct2e_entry_t _ctio2e;
		ct7_entry_t _ctio7;
	} un;
	va_list ap;
	int vpidx, nphdl;

	ISP_MEMZERO(&un, sizeof un);

	if (IS_24XX(isp)) {
		at7_entry_t *aep;
		ct7_entry_t *cto = &un._ctio7;

		va_start(ap, isp);
		aep = va_arg(ap, at7_entry_t *);
		nphdl = va_arg(ap, int);
		/*
		 * Note that vpidx may equal 0xff (unknown) here
		 */
		vpidx = va_arg(ap, int);
		code = va_arg(ap, uint32_t);
		hdl = va_arg(ap, uint32_t);
		va_end(ap);
		isp_prt(isp, ISP_LOGTDEBUG0, "%s: [RX_ID 0x%x] chan %d code %x", __func__, aep->at_rxid, vpidx, code);

		sts = code & 0xff;
		cto->ct_header.rqs_entry_type = RQSTYPE_CTIO7;
		cto->ct_header.rqs_entry_count = 1;
		cto->ct_nphdl = nphdl;
		cto->ct_rxid = aep->at_rxid;
		cto->ct_iid_lo = (aep->at_hdr.s_id[1] << 8) | aep->at_hdr.s_id[2];
		cto->ct_iid_hi = aep->at_hdr.s_id[0];
		cto->ct_oxid = aep->at_hdr.ox_id;
		cto->ct_scsi_status = sts;
		cto->ct_vpidx = vpidx;
		cto->ct_flags = CT7_NOACK;
		if (code & ECMD_TERMINATE) {
			cto->ct_flags |= CT7_TERMINATE;
		} else if (code & ECMD_SVALID) {
			cto->ct_flags |= CT7_FLAG_MODE1 | CT7_SENDSTATUS;
			cto->ct_scsi_status |= (FCP_SNSLEN_VALID << 8);
			cto->ct_senselen = min(16, MAXRESPLEN_24XX);
			ISP_MEMZERO(cto->rsp.m1.ct_resp, sizeof (cto->rsp.m1.ct_resp));
			cto->rsp.m1.ct_resp[0] = 0xf0;
			cto->rsp.m1.ct_resp[2] = (code >> 12) & 0xf;
			cto->rsp.m1.ct_resp[7] = 8;
			cto->rsp.m1.ct_resp[12] = (code >> 16) & 0xff;
			cto->rsp.m1.ct_resp[13] = (code >> 24) & 0xff;
		} else if (code & ECMD_RVALID) {
			cto->ct_flags |= CT7_FLAG_MODE1 | CT7_SENDSTATUS;
			cto->ct_scsi_status |= (FCP_RSPLEN_VALID << 8);
			cto->rsp.m1.ct_resplen = 4;
			ISP_MEMZERO(cto->rsp.m1.ct_resp, sizeof (cto->rsp.m1.ct_resp));
			cto->rsp.m1.ct_resp[0] = (code >> 12) & 0xf;
			cto->rsp.m1.ct_resp[1] = (code >> 16) & 0xff;
			cto->rsp.m1.ct_resp[2] = (code >> 24) & 0xff;
			cto->rsp.m1.ct_resp[3] = 0;
		} else {
			cto->ct_flags |= CT7_FLAG_MODE1 | CT7_SENDSTATUS;
		}
		if (aep->at_cmnd.cdb_dl.sf.fcp_cmnd_dl != 0) {
			cto->ct_resid = aep->at_cmnd.cdb_dl.sf.fcp_cmnd_dl;
			cto->ct_scsi_status |= (FCP_RESID_UNDERFLOW << 8);
		}
		cto->ct_syshandle = hdl;
	} else {
		at2_entry_t *aep;
		ct2_entry_t *cto = &un._ctio2;

		va_start(ap, isp);
		aep = va_arg(ap, at2_entry_t *);
		/* nphdl and vpidx are unused here. */
		nphdl = va_arg(ap, int);
		vpidx = va_arg(ap, int);
		code = va_arg(ap, uint32_t);
		hdl = va_arg(ap, uint32_t);
		va_end(ap);

		isp_prt(isp, ISP_LOGTDEBUG0, "%s: [RX_ID 0x%x] code %x", __func__, aep->at_rxid, code);

		sts = code & 0xff;
		cto->ct_header.rqs_entry_type = RQSTYPE_CTIO2;
		cto->ct_header.rqs_entry_count = 1;
		if (ISP_CAP_SCCFW(isp) == 0) {
			cto->ct_lun = aep->at_lun;
		}
		if (ISP_CAP_2KLOGIN(isp)) {
			un._ctio2e.ct_iid = ((at2e_entry_t *)aep)->at_iid;
		} else {
			cto->ct_iid = aep->at_iid;
		}
		cto->ct_rxid = aep->at_rxid;
		cto->rsp.m1.ct_scsi_status = sts;
		cto->ct_flags = CT2_SENDSTATUS | CT2_NO_DATA | CT2_FLAG_MODE1;
		if (hdl == 0) {
			cto->ct_flags |= CT2_CCINCR;
		}
		if (aep->at_datalen) {
			cto->ct_resid = aep->at_datalen;
			cto->rsp.m1.ct_scsi_status |= CT2_DATA_UNDER;
		}
		if (sts == SCSI_CHECK && (code & ECMD_SVALID)) {
			cto->rsp.m1.ct_resp[0] = 0xf0;
			cto->rsp.m1.ct_resp[2] = (code >> 12) & 0xf;
			cto->rsp.m1.ct_resp[7] = 8;
			cto->rsp.m1.ct_resp[12] = (code >> 24) & 0xff;
			cto->rsp.m1.ct_resp[13] = (code >> 16) & 0xff;
			cto->rsp.m1.ct_senselen = 16;
			cto->rsp.m1.ct_scsi_status |= CT2_SNSLEN_VALID;
		}
		cto->ct_syshandle = hdl;
	}
	return (isp_target_put_entry(isp, &un));
}

/*
 * These are either broadcast events or specifically CTIO fast completion
 */

void
isp_target_async(ispsoftc_t *isp, int bus, int event)
{
	isp_notify_t notify;

	ISP_MEMZERO(&notify, sizeof (isp_notify_t));
	notify.nt_hba = isp;
	notify.nt_wwn = INI_ANY;
	notify.nt_nphdl = NIL_HANDLE;
	notify.nt_sid = PORT_ANY;
	notify.nt_did = PORT_ANY;
	notify.nt_tgt = TGT_ANY;
	notify.nt_channel = bus;
	notify.nt_lun = LUN_ANY;
	notify.nt_tagval = TAG_ANY;
	notify.nt_tagval |= (((uint64_t)(isp->isp_serno++)) << 32);

	switch (event) {
	case ASYNC_LOOP_UP:
	case ASYNC_PTPMODE:
		isp_prt(isp, ISP_LOGTDEBUG0, "%s: LOOP UP", __func__);
		notify.nt_ncode = NT_LINK_UP;
		isp_async(isp, ISPASYNC_TARGET_NOTIFY, &notify);
		break;
	case ASYNC_LOOP_DOWN:
		isp_prt(isp, ISP_LOGTDEBUG0, "%s: LOOP DOWN", __func__);
		notify.nt_ncode = NT_LINK_DOWN;
		isp_async(isp, ISPASYNC_TARGET_NOTIFY, &notify);
		break;
	case ASYNC_LIP_ERROR:
	case ASYNC_LIP_NOS_OLS_RECV:
	case ASYNC_LIP_OCCURRED:
	case ASYNC_LOOP_RESET:
		isp_prt(isp, ISP_LOGTDEBUG0, "%s: LIP RESET", __func__);
		notify.nt_ncode = NT_LIP_RESET;
		isp_async(isp, ISPASYNC_TARGET_NOTIFY, &notify);
		break;
	case ASYNC_BUS_RESET:
	case ASYNC_TIMEOUT_RESET:	/* XXX: where does this come from ? */
		isp_prt(isp, ISP_LOGTDEBUG0, "%s: BUS RESET", __func__);
		notify.nt_ncode = NT_BUS_RESET;
		isp_async(isp, ISPASYNC_TARGET_NOTIFY, &notify);
		break;
	case ASYNC_DEVICE_RESET:
		isp_prt(isp, ISP_LOGTDEBUG0, "%s: DEVICE RESET", __func__);
		notify.nt_ncode = NT_TARGET_RESET;
		isp_async(isp, ISPASYNC_TARGET_NOTIFY, &notify);
		break;
	case ASYNC_CTIO_DONE:
	{
		uint8_t storage[QENTRY_LEN];
		isp_prt(isp, ISP_LOGTDEBUG0, "%s: CTIO DONE", __func__);
		memset(storage, 0, QENTRY_LEN);
		if (IS_24XX(isp)) {
			ct7_entry_t *ct = (ct7_entry_t *) storage;
			ct->ct_header.rqs_entry_type = RQSTYPE_CTIO7;
			ct->ct_nphdl = CT7_OK;
			ct->ct_syshandle = bus;
			ct->ct_flags = CT7_SENDSTATUS;
		} else {
            		/* This should also suffice for 2K login code */
			ct2_entry_t *ct = (ct2_entry_t *) storage;
			ct->ct_header.rqs_entry_type = RQSTYPE_CTIO2;
			ct->ct_status = CT_OK;
			ct->ct_syshandle = bus;
			ct->ct_flags = CT2_SENDSTATUS|CT2_FASTPOST;
		}
		isp_async(isp, ISPASYNC_TARGET_ACTION, storage);
		break;
	}
	default:
		isp_prt(isp, ISP_LOGERR, "%s: unknown event 0x%x", __func__, event);
		break;
	}
}

/*
 * Synthesize a message from the task management flags in a FCP_CMND_IU.
 */
static void
isp_got_msg_fc(ispsoftc_t *isp, in_fcentry_t *inp)
{
	isp_notify_t notify;
	static const char f1[] = "%s from N-port handle 0x%x lun %jx seq 0x%x";
	static const char f2[] = "unknown %s 0x%x lun %jx N-Port handle 0x%x task flags 0x%x seq 0x%x\n";
	uint16_t seqid, nphdl;

	ISP_MEMZERO(&notify, sizeof (isp_notify_t));
	notify.nt_hba = isp;
	notify.nt_wwn = INI_ANY;
	if (ISP_CAP_2KLOGIN(isp)) {
		notify.nt_nphdl = ((in_fcentry_e_t *)inp)->in_iid;
		nphdl = ((in_fcentry_e_t *)inp)->in_iid;
		seqid = ((in_fcentry_e_t *)inp)->in_seqid;
	} else {
		notify.nt_nphdl = inp->in_iid;
		nphdl = inp->in_iid;
		seqid = inp->in_seqid;
	}
	notify.nt_sid = PORT_ANY;
	notify.nt_did = PORT_ANY;

	/* nt_tgt set in outer layers */
	if (ISP_CAP_SCCFW(isp)) {
		notify.nt_lun = inp->in_scclun;
	} else {
		notify.nt_lun = inp->in_lun;
	}
	notify.nt_tagval = seqid;
	notify.nt_tagval |= (((uint64_t)(isp->isp_serno++)) << 32);
	notify.nt_need_ack = 1;
	notify.nt_lreserved = inp;

	if (inp->in_status != IN_MSG_RECEIVED) {
		isp_prt(isp, ISP_LOGINFO, f2, "immediate notify status", inp->in_status, notify.nt_lun, nphdl, inp->in_task_flags, inp->in_seqid);
		isp_async(isp, ISPASYNC_TARGET_NOTIFY_ACK, inp);
		return;
	}

	if (inp->in_task_flags & TASK_FLAGS_ABORT_TASK_SET) {
		isp_prt(isp, ISP_LOGINFO, f1, "ABORT TASK SET", nphdl, notify.nt_lun, inp->in_seqid);
		notify.nt_ncode = NT_ABORT_TASK_SET;
	} else if (inp->in_task_flags & TASK_FLAGS_CLEAR_TASK_SET) {
		isp_prt(isp, ISP_LOGINFO, f1, "CLEAR TASK SET", nphdl, notify.nt_lun, inp->in_seqid);
		notify.nt_ncode = NT_CLEAR_TASK_SET;
	} else if (inp->in_task_flags & TASK_FLAGS_LUN_RESET) {
		isp_prt(isp, ISP_LOGINFO, f1, "LUN RESET", nphdl, notify.nt_lun, inp->in_seqid);
		notify.nt_ncode = NT_LUN_RESET;
	} else if (inp->in_task_flags & TASK_FLAGS_TARGET_RESET) {
		isp_prt(isp, ISP_LOGINFO, f1, "TARGET RESET", nphdl, notify.nt_lun, inp->in_seqid);
		notify.nt_ncode = NT_TARGET_RESET;
	} else if (inp->in_task_flags & TASK_FLAGS_CLEAR_ACA) {
		isp_prt(isp, ISP_LOGINFO, f1, "CLEAR ACA", nphdl, notify.nt_lun, inp->in_seqid);
		notify.nt_ncode = NT_CLEAR_ACA;
	} else {
		isp_prt(isp, ISP_LOGWARN, f2, "task flag", inp->in_status, notify.nt_lun, nphdl, inp->in_task_flags,  inp->in_seqid);
		isp_async(isp, ISPASYNC_TARGET_NOTIFY_ACK, inp);
		return;
	}
	isp_async(isp, ISPASYNC_TARGET_NOTIFY, &notify);
}

static void
isp_got_tmf_24xx(ispsoftc_t *isp, at7_entry_t *aep)
{
	isp_notify_t notify;
	static const char f1[] = "%s from PortID 0x%06x lun %jx seq 0x%08x";
	static const char f2[] = "unknown Task Flag 0x%x lun %jx PortID 0x%x tag 0x%08x";
	fcportdb_t *lp;
	uint16_t chan;
	uint32_t sid, did;

	ISP_MEMZERO(&notify, sizeof (isp_notify_t));
	notify.nt_hba = isp;
	notify.nt_wwn = INI_ANY;
	notify.nt_lun = CAM_EXTLUN_BYTE_SWIZZLE(be64dec(aep->at_cmnd.fcp_cmnd_lun));
	notify.nt_tagval = aep->at_rxid;
	notify.nt_tagval |= (((uint64_t)(isp->isp_serno++)) << 32);
	notify.nt_lreserved = aep;
	sid = (aep->at_hdr.s_id[0] << 16) | (aep->at_hdr.s_id[1] << 8) | aep->at_hdr.s_id[2];
	did = (aep->at_hdr.d_id[0] << 16) | (aep->at_hdr.d_id[1] << 8) | aep->at_hdr.d_id[2];
	if (ISP_CAP_MULTI_ID(isp) && isp->isp_nchan > 1) {
		/* Channel has to be derived from D_ID */
		isp_find_chan_by_did(isp, did, &chan);
		if (chan == ISP_NOCHAN) {
			isp_prt(isp, ISP_LOGWARN,
			    "%s: D_ID 0x%x not found on any channel",
			    __func__, did);
			isp_endcmd(isp, aep, NIL_HANDLE, ISP_NOCHAN,
			    ECMD_TERMINATE, 0);
			return;
		}
	} else {
		chan = 0;
	}
	if (isp_find_pdb_by_portid(isp, chan, sid, &lp))
		notify.nt_nphdl = lp->handle;
	else
		notify.nt_nphdl = NIL_HANDLE;
	notify.nt_sid = sid;
	notify.nt_did = did;
	notify.nt_channel = chan;
	if (aep->at_cmnd.fcp_cmnd_task_management & FCP_CMND_TMF_QUERY_TASK_SET) {
		isp_prt(isp, ISP_LOGINFO, f1, "QUERY TASK SET", sid, notify.nt_lun, aep->at_rxid);
		notify.nt_ncode = NT_QUERY_TASK_SET;
	} else if (aep->at_cmnd.fcp_cmnd_task_management & FCP_CMND_TMF_ABORT_TASK_SET) {
		isp_prt(isp, ISP_LOGINFO, f1, "ABORT TASK SET", sid, notify.nt_lun, aep->at_rxid);
		notify.nt_ncode = NT_ABORT_TASK_SET;
	} else if (aep->at_cmnd.fcp_cmnd_task_management & FCP_CMND_TMF_CLEAR_TASK_SET) {
		isp_prt(isp, ISP_LOGINFO, f1, "CLEAR TASK SET", sid, notify.nt_lun, aep->at_rxid);
		notify.nt_ncode = NT_CLEAR_TASK_SET;
	} else if (aep->at_cmnd.fcp_cmnd_task_management & FCP_CMND_TMF_QUERY_ASYNC_EVENT) {
		isp_prt(isp, ISP_LOGINFO, f1, "QUERY ASYNC EVENT", sid, notify.nt_lun, aep->at_rxid);
		notify.nt_ncode = NT_QUERY_ASYNC_EVENT;
	} else if (aep->at_cmnd.fcp_cmnd_task_management & FCP_CMND_TMF_LUN_RESET) {
		isp_prt(isp, ISP_LOGINFO, f1, "LUN RESET", sid, notify.nt_lun, aep->at_rxid);
		notify.nt_ncode = NT_LUN_RESET;
	} else if (aep->at_cmnd.fcp_cmnd_task_management & FCP_CMND_TMF_TGT_RESET) {
		isp_prt(isp, ISP_LOGINFO, f1, "TARGET RESET", sid, notify.nt_lun, aep->at_rxid);
		notify.nt_ncode = NT_TARGET_RESET;
	} else if (aep->at_cmnd.fcp_cmnd_task_management & FCP_CMND_TMF_CLEAR_ACA) {
		isp_prt(isp, ISP_LOGINFO, f1, "CLEAR ACA", sid, notify.nt_lun, aep->at_rxid);
		notify.nt_ncode = NT_CLEAR_ACA;
	} else {
		isp_prt(isp, ISP_LOGWARN, f2, aep->at_cmnd.fcp_cmnd_task_management, notify.nt_lun, sid, aep->at_rxid);
		notify.nt_ncode = NT_UNKNOWN;
		isp_endcmd(isp, aep, notify.nt_nphdl, chan, ECMD_RVALID | (0x4 << 12), 0);
		return;
	}
	isp_async(isp, ISPASYNC_TARGET_NOTIFY, &notify);
}

int
isp_notify_ack(ispsoftc_t *isp, void *arg)
{
	char storage[QENTRY_LEN];

	/*
	 * This is in case a Task Management Function ends up here.
	 */
	if (IS_24XX(isp) && ((isphdr_t *)arg)->rqs_entry_type == RQSTYPE_ATIO) {
		at7_entry_t *aep = arg;
		return (isp_endcmd(isp, aep, NIL_HANDLE, 0, 0, 0));
	}

	ISP_MEMZERO(storage, QENTRY_LEN);
	if (IS_24XX(isp)) {
		in_fcentry_24xx_t *in = arg;
		na_fcentry_24xx_t *na = (na_fcentry_24xx_t *) storage;

		na->na_header.rqs_entry_type = RQSTYPE_NOTIFY_ACK;
		na->na_header.rqs_entry_count = 1;
		na->na_nphdl = in->in_nphdl;
		na->na_flags = in->in_flags;
		na->na_status = in->in_status;
		na->na_status_subcode = in->in_status_subcode;
		na->na_fwhandle = in->in_fwhandle;
		na->na_rxid = in->in_rxid;
		na->na_oxid = in->in_oxid;
		na->na_vpidx = in->in_vpidx;
		if (in->in_status == IN24XX_SRR_RCVD) {
			na->na_srr_rxid = in->in_srr_rxid;
			na->na_srr_reloff_hi = in->in_srr_reloff_hi;
			na->na_srr_reloff_lo = in->in_srr_reloff_lo;
			na->na_srr_iu = in->in_srr_iu;
			/*
			 * Whether we're accepting the SRR or rejecting
			 * it is determined by looking at the in_reserved
			 * field in the original notify structure.
			 */
			if (in->in_reserved) {
				na->na_srr_flags = 1;
				na->na_srr_reject_vunique = 0;
				/* Unable to perform this command at this time. */
				na->na_srr_reject_code = 9;
				/* Unable to supply the requested data. */
				na->na_srr_reject_explanation = 0x2a;
			}
		}
	} else {
		in_fcentry_t *in = arg;
		na_fcentry_t *na = (na_fcentry_t *) storage;
		int iid;

		ISP_MEMCPY(storage, arg, sizeof (isphdr_t));
		if (ISP_CAP_2KLOGIN(isp)) {
			iid = ((in_fcentry_e_t *)in)->in_iid;
			((na_fcentry_e_t *)na)->na_iid = iid;
		} else {
			iid = in->in_iid;
			na->na_iid = iid;
		}
		na->na_task_flags = in->in_task_flags & TASK_FLAGS_RESERVED_MASK;
		na->na_seqid = in->in_seqid;
		na->na_status = in->in_status;
		na->na_flags = NAFC_RCOUNT;
		/* We do not modify resource counts for LIP resets */
		if (in->in_status == IN_RESET)
			na->na_flags = NAFC_RST_CLRD;
		if (in->in_status == IN_MSG_RECEIVED) {
			na->na_flags |= NAFC_TVALID;
			na->na_response = 0;	/* XXX SUCCEEDED XXX */
		}
		na->na_header.rqs_entry_type = RQSTYPE_NOTIFY_ACK;
		na->na_header.rqs_entry_count = 1;
		isp_prt(isp, ISP_LOGTDEBUG0, "notify ack handle %x seqid %x flags %x tflags %x response %x", iid, na->na_seqid,
		    na->na_flags, na->na_task_flags, na->na_response);
	}
	return (isp_target_put_entry(isp, &storage));
}

int
isp_acknak_abts(ispsoftc_t *isp, void *arg, int errno)
{
	char storage[QENTRY_LEN];
	uint16_t tmpw;
	uint8_t tmpb;
	abts_t *abts = arg;
	abts_rsp_t *rsp = (abts_rsp_t *) storage;

	if (!IS_24XX(isp)) {
		isp_prt(isp, ISP_LOGERR, "%s: called for non-24XX card", __func__);
		return (0);
	}

	if (abts->abts_header.rqs_entry_type != RQSTYPE_ABTS_RCVD) {
		isp_prt(isp, ISP_LOGERR, "%s: called for non-ABTS entry (0x%x)", __func__, abts->abts_header.rqs_entry_type);
		return (0);
	}

	ISP_MEMCPY(rsp, abts, QENTRY_LEN);
	rsp->abts_rsp_header.rqs_entry_type = RQSTYPE_ABTS_RSP;

	/*
	 * Swap destination and source for response.
	 */
	rsp->abts_rsp_r_ctl = BA_ACC;
	tmpw = rsp->abts_rsp_did_lo;
	tmpb = rsp->abts_rsp_did_hi;
	rsp->abts_rsp_did_lo = rsp->abts_rsp_sid_lo;
	rsp->abts_rsp_did_hi = rsp->abts_rsp_sid_hi;
	rsp->abts_rsp_sid_lo = tmpw;
	rsp->abts_rsp_sid_hi = tmpb;

	rsp->abts_rsp_f_ctl_hi ^= 0x80; 	/* invert Exchange Context */
	rsp->abts_rsp_f_ctl_hi &= ~0x7f;	/* clear Sequence Initiator and other bits */
	rsp->abts_rsp_f_ctl_hi |= 0x10;		/* abort the whole exchange */
	rsp->abts_rsp_f_ctl_hi |= 0x8;		/* last data frame of sequence */
	rsp->abts_rsp_f_ctl_hi |= 0x1;		/* transfer Sequence Initiative */
	rsp->abts_rsp_f_ctl_lo = 0;

	if (errno == 0) {
		uint16_t rx_id, ox_id;

		rx_id = rsp->abts_rsp_rx_id;
		ox_id = rsp->abts_rsp_ox_id;
		ISP_MEMZERO(&rsp->abts_rsp_payload.ba_acc, sizeof (rsp->abts_rsp_payload.ba_acc));
                isp_prt(isp, ISP_LOGTINFO, "[0x%x] ABTS of 0x%x being BA_ACC'd", rsp->abts_rsp_rxid_abts, rsp->abts_rsp_rxid_task);
                rsp->abts_rsp_payload.ba_acc.aborted_rx_id = rx_id;
                rsp->abts_rsp_payload.ba_acc.aborted_ox_id = ox_id;
                rsp->abts_rsp_payload.ba_acc.high_seq_cnt = 0xffff;
	} else {
		ISP_MEMZERO(&rsp->abts_rsp_payload.ba_rjt, sizeof (rsp->abts_rsp_payload.ba_acc));
		switch (errno) {
		case ENOMEM:
			rsp->abts_rsp_payload.ba_rjt.reason = 5;	/* Logical Unit Busy */
			break;
		default:
			rsp->abts_rsp_payload.ba_rjt.reason = 9;	/* Unable to perform command request */
			break;
		}
	}
	return (isp_target_put_entry(isp, rsp));
}

static void
isp_handle_abts(ispsoftc_t *isp, abts_t *abts)
{
	isp_notify_t notify, *nt = &notify;
	fcportdb_t *lp;
	uint16_t chan;
	uint32_t sid, did;

	did = (abts->abts_did_hi << 16) | abts->abts_did_lo;
	sid = (abts->abts_sid_hi << 16) | abts->abts_sid_lo;
	ISP_MEMZERO(nt, sizeof (isp_notify_t));

	nt->nt_hba = isp;
	nt->nt_did = did;
	nt->nt_nphdl = abts->abts_nphdl;
	nt->nt_sid = sid;
	if (ISP_CAP_MULTI_ID(isp) && isp->isp_nchan > 1) {
		/* Channel has to be derived from D_ID */
		isp_find_chan_by_did(isp, did, &chan);
		if (chan == ISP_NOCHAN) {
			isp_prt(isp, ISP_LOGWARN,
			    "%s: D_ID 0x%x not found on any channel",
			    __func__, did);
			isp_acknak_abts(isp, abts, ENXIO);
			return;
		}
	} else
		chan = 0;
	nt->nt_tgt = FCPARAM(isp, chan)->isp_wwpn;
	if (isp_find_pdb_by_handle(isp, chan, abts->abts_nphdl, &lp))
		nt->nt_wwn = lp->port_wwn;
	else
		nt->nt_wwn = INI_ANY;
	nt->nt_lun = LUN_ANY;
	nt->nt_need_ack = 1;
	nt->nt_tagval = abts->abts_rxid_task;
	nt->nt_tagval |= (((uint64_t) abts->abts_rxid_abts) << 32);
	isp_prt(isp, ISP_LOGTINFO, "[0x%x] ABTS from N-Port handle 0x%x"
	    " Port 0x%06x for task 0x%x (rx_id 0x%04x ox_id 0x%04x)",
	    abts->abts_rxid_abts, abts->abts_nphdl, sid, abts->abts_rxid_task,
	    abts->abts_rx_id, abts->abts_ox_id);
	nt->nt_channel = chan;
	nt->nt_ncode = NT_ABORT_TASK;
	nt->nt_lreserved = abts;
	isp_async(isp, ISPASYNC_TARGET_NOTIFY, &notify);
}

static void
isp_handle_atio2(ispsoftc_t *isp, at2_entry_t *aep)
{
	fcportdb_t *lp;
	int lun, iid;

	if (ISP_CAP_SCCFW(isp)) {
		lun = aep->at_scclun;
	} else {
		lun = aep->at_lun;
	}

	if (ISP_CAP_2KLOGIN(isp)) {
		iid = ((at2e_entry_t *)aep)->at_iid;
	} else {
		iid = aep->at_iid;
	}

	/*
	 * The firmware status (except for the QLTM_SVALID bit) indicates
	 * why this ATIO was sent to us.
	 *
	 * If QLTM_SVALID is set, the firware has recommended Sense Data.
	 *
	 * If the DISCONNECTS DISABLED bit is set in the flags field,
	 * we're still connected on the SCSI bus - i.e. the initiator
	 * did not set DiscPriv in the identify message. We don't care
	 * about this so it's ignored.
	 */

	switch (aep->at_status & ~QLTM_SVALID) {
	case AT_PATH_INVALID:
		/*
		 * ATIO rejected by the firmware due to disabled lun.
		 */
		isp_prt(isp, ISP_LOGERR, "rejected ATIO2 for disabled lun %x", lun);
		break;
	case AT_NOCAP:
		/*
		 * Requested Capability not available
		 * We sent an ATIO that overflowed the firmware's
		 * command resource count.
		 */
		isp_prt(isp, ISP_LOGERR, "rejected ATIO2 for lun %x- command count overflow", lun);
		break;

	case AT_BDR_MSG:
		/*
		 * If we send an ATIO to the firmware to increment
		 * its command resource count, and the firmware is
		 * recovering from a Bus Device Reset, it returns
		 * the ATIO with this status. We set the command
		 * resource count in the Enable Lun entry and no
		 * not increment it. Therefore we should never get
		 * this status here.
		 */
		isp_prt(isp, ISP_LOGERR, atiocope, lun, 0);
		break;

	case AT_CDB:		/* Got a CDB */

		/* Make sure we have this inititor in port database. */
		if (!IS_2100(isp) &&
		    (isp_find_pdb_by_handle(isp, 0, iid, &lp) == 0 ||
		     lp->state == FC_PORTDB_STATE_ZOMBIE)) {
		        fcparam *fcp = FCPARAM(isp, 0);
			uint64_t wwpn =
				(((uint64_t) aep->at_wwpn[0]) << 48) |
				(((uint64_t) aep->at_wwpn[1]) << 32) |
				(((uint64_t) aep->at_wwpn[2]) << 16) |
				(((uint64_t) aep->at_wwpn[3]) <<  0);
			isp_add_wwn_entry(isp, 0, wwpn, INI_NONE,
			    iid, PORT_ANY, 0);
			if (fcp->isp_loopstate > LOOP_LTEST_DONE)
				fcp->isp_loopstate = LOOP_LTEST_DONE;
			isp_async(isp, ISPASYNC_CHANGE_NOTIFY, 0,
			    ISPASYNC_CHANGE_PDB, iid, 0x06, 0xff);
		}

		/* Punt to platform specific layer. */
		isp_async(isp, ISPASYNC_TARGET_ACTION, aep);
		break;

	case AT_RESET:
		/*
		 * A bus reset came along an blew away this command. Why
		 * they do this in addition the async event code stuff,
		 * I dunno.
		 *
		 * Ignore it because the async event will clear things
		 * up for us.
		 */
		isp_prt(isp, ISP_LOGERR, atior, lun, iid, 0);
		break;


	default:
		isp_prt(isp, ISP_LOGERR, "Unknown ATIO2 status 0x%x from handle %d for lun %x", aep->at_status, iid, lun);
		(void) isp_target_put_atio(isp, aep);
		break;
	}
}

static void
isp_handle_ctio2(ispsoftc_t *isp, ct2_entry_t *ct)
{
	void *xs;
	int pl = ISP_LOGTDEBUG2;
	char *fmsg = NULL;

	if (ct->ct_syshandle) {
		xs = isp_find_xs(isp, ct->ct_syshandle);
		if (xs == NULL) {
			pl = ISP_LOGALL;
		}
	} else {
		xs = NULL;
	}

	switch (ct->ct_status & ~QLTM_SVALID) {
	case CT_BUS_ERROR:
		isp_prt(isp, ISP_LOGERR, "PCI DMA Bus Error");
		/* FALL Through */
	case CT_DATA_OVER:
	case CT_DATA_UNDER:
	case CT_OK:
		/*
		 * There are generally 2 possibilities as to why we'd get
		 * this condition:
		 * 	We sent or received data.
		 * 	We sent status & command complete.
		 */

		break;

	case CT_BDR_MSG:
		/*
		 * Target Reset function received.
		 *
		 * The firmware generates an async mailbox interrupt to
		 * notify us of this and returns outstanding CTIOs with this
		 * status. These CTIOs are handled in that same way as
		 * CT_ABORTED ones, so just fall through here.
		 */
		fmsg = "TARGET RESET";
		/*FALLTHROUGH*/
	case CT_RESET:
		if (fmsg == NULL)
			fmsg = "LIP Reset";
		/*FALLTHROUGH*/
	case CT_ABORTED:
		/*
		 * When an Abort message is received the firmware goes to
		 * Bus Free and returns all outstanding CTIOs with the status
		 * set, then sends us an Immediate Notify entry.
		 */
		if (fmsg == NULL) {
			fmsg = "ABORT";
		}

		isp_prt(isp, ISP_LOGTDEBUG0, "CTIO2 destroyed by %s: RX_ID=0x%x", fmsg, ct->ct_rxid);
		break;

	case CT_INVAL:
		/*
		 * CTIO rejected by the firmware - invalid data direction.
		 */
		isp_prt(isp, ISP_LOGERR, "CTIO2 had wrong data direction");
		break;

	case CT_RSELTMO:
		fmsg = "failure to reconnect to initiator";
		/*FALLTHROUGH*/
	case CT_TIMEOUT:
		if (fmsg == NULL)
			fmsg = "command";
		isp_prt(isp, ISP_LOGWARN, "Firmware timed out on %s", fmsg);
		break;

	case CT_ERR:
		fmsg = "Completed with Error";
		/*FALLTHROUGH*/
	case CT_LOGOUT:
		if (fmsg == NULL)
			fmsg = "Port Logout";
		/*FALLTHROUGH*/
	case CT_PORTUNAVAIL:
		if (fmsg == NULL)
			fmsg = "Port not available";
		/*FALLTHROUGH*/
	case CT_PORTCHANGED:
		if (fmsg == NULL)
			fmsg = "Port Changed";
		/*FALLTHROUGH*/
	case CT_NOACK:
		if (fmsg == NULL)
			fmsg = "unacknowledged Immediate Notify pending";
		isp_prt(isp, ISP_LOGWARN, "CTIO returned by f/w- %s", fmsg);
		break;

	case CT_INVRXID:
		/*
		 * CTIO rejected by the firmware because an invalid RX_ID.
		 * Just print a message.
		 */
		isp_prt(isp, ISP_LOGWARN, "CTIO2 completed with Invalid RX_ID 0x%x", ct->ct_rxid);
		break;

	default:
		isp_prt(isp, ISP_LOGERR, "Unknown CTIO2 status 0x%x", ct->ct_status & ~QLTM_SVALID);
		break;
	}

	if (xs == NULL) {
		/*
		 * There may be more than one CTIO for a data transfer,
		 * or this may be a status CTIO we're not monitoring.
		 *
		 * The assumption is that they'll all be returned in the
		 * order we got them.
		 */
		if (ct->ct_syshandle == 0) {
			if ((ct->ct_flags & CT2_SENDSTATUS) == 0) {
				isp_prt(isp, pl, "intermediate CTIO completed ok");
			} else {
				isp_prt(isp, pl, "unmonitored CTIO completed ok");
			}
		} else {
			isp_prt(isp, pl, "NO xs for CTIO (handle 0x%x) status 0x%x", ct->ct_syshandle, ct->ct_status & ~QLTM_SVALID);
		}
	} else {
		if ((ct->ct_flags & CT2_DATAMASK) != CT2_NO_DATA) {
			ISP_DMAFREE(isp, xs, ct->ct_syshandle);
		}
		if (ct->ct_flags & CT2_SENDSTATUS) {
			/*
			 * Sent status and command complete.
			 *
			 * We're now really done with this command, so we
			 * punt to the platform dependent layers because
			 * only there can we do the appropriate command
			 * complete thread synchronization.
			 */
			isp_prt(isp, pl, "status CTIO complete");
		} else {
			/*
			 * Final CTIO completed. Release DMA resources and
			 * notify platform dependent layers.
			 */
			isp_prt(isp, pl, "data CTIO complete");
		}
		isp_async(isp, ISPASYNC_TARGET_ACTION, ct);
		/*
		 * The platform layer will destroy the handle if appropriate.
		 */
	}
}

static void
isp_handle_ctio7(ispsoftc_t *isp, ct7_entry_t *ct)
{
	void *xs;
	int pl = ISP_LOGTDEBUG2;
	char *fmsg = NULL;

	if (ct->ct_syshandle) {
		xs = isp_find_xs(isp, ct->ct_syshandle);
		if (xs == NULL) {
			pl = ISP_LOGALL;
		}
	} else {
		xs = NULL;
	}

	switch (ct->ct_nphdl) {
	case CT7_BUS_ERROR:
		isp_prt(isp, ISP_LOGERR, "PCI DMA Bus Error");
		/* FALL Through */
	case CT7_DATA_OVER:
	case CT7_DATA_UNDER:
	case CT7_OK:
		/*
		 * There are generally 2 possibilities as to why we'd get
		 * this condition:
		 * 	We sent or received data.
		 * 	We sent status & command complete.
		 */

		break;

	case CT7_RESET:
		if (fmsg == NULL) {
			fmsg = "LIP Reset";
		}
		/*FALLTHROUGH*/
	case CT7_ABORTED:
		/*
		 * When an Abort message is received the firmware goes to
		 * Bus Free and returns all outstanding CTIOs with the status
		 * set, then sends us an Immediate Notify entry.
		 */
		if (fmsg == NULL) {
			fmsg = "ABORT";
		}
		isp_prt(isp, ISP_LOGTDEBUG0, "CTIO7 destroyed by %s: RX_ID=0x%x", fmsg, ct->ct_rxid);
		break;

	case CT7_TIMEOUT:
		if (fmsg == NULL) {
			fmsg = "command";
		}
		isp_prt(isp, ISP_LOGWARN, "Firmware timed out on %s", fmsg);
		break;

	case CT7_ERR:
		fmsg = "Completed with Error";
		/*FALLTHROUGH*/
	case CT7_LOGOUT:
		if (fmsg == NULL) {
			fmsg = "Port Logout";
		}
		/*FALLTHROUGH*/
	case CT7_PORTUNAVAIL:
		if (fmsg == NULL) {
			fmsg = "Port not available";
		}
		/*FALLTHROUGH*/
	case CT7_PORTCHANGED:
		if (fmsg == NULL) {
			fmsg = "Port Changed";
		}
		isp_prt(isp, ISP_LOGWARN, "CTIO returned by f/w- %s", fmsg);
		break;

	case CT7_INVRXID:
		/*
		 * CTIO rejected by the firmware because an invalid RX_ID.
		 * Just print a message.
		 */
		isp_prt(isp, ISP_LOGWARN, "CTIO7 completed with Invalid RX_ID 0x%x", ct->ct_rxid);
		break;

	case CT7_REASSY_ERR:
		isp_prt(isp, ISP_LOGWARN, "reassembly error");
		break;

	case CT7_SRR:
		isp_prt(isp, ISP_LOGTDEBUG0, "SRR received");
		break;

	default:
		isp_prt(isp, ISP_LOGERR, "Unknown CTIO7 status 0x%x", ct->ct_nphdl);
		break;
	}

	if (xs == NULL) {
		/*
		 * There may be more than one CTIO for a data transfer,
		 * or this may be a status CTIO we're not monitoring.
		 *
		 * The assumption is that they'll all be returned in the
		 * order we got them.
		 */
		if (ct->ct_syshandle == 0) {
			if (ct->ct_flags & CT7_TERMINATE) {
				isp_prt(isp, ISP_LOGINFO, "termination of [RX_ID 0x%x] complete", ct->ct_rxid);
			} else if ((ct->ct_flags & CT7_SENDSTATUS) == 0) {
				isp_prt(isp, pl, "intermediate CTIO completed ok");
			} else {
				isp_prt(isp, pl, "unmonitored CTIO completed ok");
			}
		} else {
			isp_prt(isp, pl, "NO xs for CTIO (handle 0x%x) status 0x%x", ct->ct_syshandle, ct->ct_nphdl);
		}
	} else {
		if ((ct->ct_flags & CT7_DATAMASK) != CT7_NO_DATA) {
			ISP_DMAFREE(isp, xs, ct->ct_syshandle);
		}
		if (ct->ct_flags & CT7_SENDSTATUS) {
			/*
			 * Sent status and command complete.
			 *
			 * We're now really done with this command, so we
			 * punt to the platform dependent layers because
			 * only there can we do the appropriate command
			 * complete thread synchronization.
			 */
			isp_prt(isp, pl, "status CTIO complete");
		} else {
			/*
			 * Final CTIO completed. Release DMA resources and
			 * notify platform dependent layers.
			 */
			isp_prt(isp, pl, "data CTIO complete");
		}
		isp_async(isp, ISPASYNC_TARGET_ACTION, ct);
		/*
		 * The platform layer will destroy the handle if appropriate.
		 */
	}
}

static void
isp_handle_notify(ispsoftc_t *isp, in_fcentry_t *inp)
{
	fcportdb_t *lp;
	uint64_t wwn;
	uint32_t sid;
	uint16_t nphdl, status;
	isp_notify_t notify;

	status = inp->in_status;
	isp_prt(isp, ISP_LOGTDEBUG0, "Immediate Notify, status=0x%x seqid=0x%x",
	    status, inp->in_seqid);
	switch (status) {
	case IN_MSG_RECEIVED:
	case IN_IDE_RECEIVED:
		isp_got_msg_fc(isp, inp);
		return;
	case IN_RSRC_UNAVAIL:
		isp_prt(isp, ISP_LOGINFO, "Firmware out of ATIOs");
		isp_async(isp, ISPASYNC_TARGET_NOTIFY_ACK, inp);
		return;
	}

	if (ISP_CAP_2KLOGIN(isp))
		nphdl = ((in_fcentry_e_t *)inp)->in_iid;
	else
		nphdl = inp->in_iid;
	if (isp_find_pdb_by_handle(isp, 0, nphdl, &lp)) {
		wwn = lp->port_wwn;
		sid = lp->portid;
	} else {
		wwn = INI_ANY;
		sid = PORT_ANY;
	}

	ISP_MEMZERO(&notify, sizeof (isp_notify_t));
	notify.nt_hba = isp;
	notify.nt_wwn = wwn;
	notify.nt_tgt = FCPARAM(isp, 0)->isp_wwpn;
	notify.nt_nphdl = nphdl;
	notify.nt_sid = sid;
	notify.nt_did = PORT_ANY;
	if (ISP_CAP_SCCFW(isp))
		notify.nt_lun = inp->in_scclun;
	else
		notify.nt_lun = inp->in_lun;
	notify.nt_tagval = inp->in_seqid;
	notify.nt_tagval |= (((uint64_t)(isp->isp_serno++)) << 32);
	notify.nt_need_ack = 1;
	notify.nt_channel = 0;
	notify.nt_lreserved = inp;

	switch (status) {
	case IN_RESET:
		notify.nt_ncode = NT_BUS_RESET;
		break;
	case IN_PORT_LOGOUT:
		notify.nt_ncode = NT_LOGOUT;
		break;
	case IN_ABORT_TASK:
		notify.nt_ncode = NT_ABORT_TASK;
		break;
	case IN_GLOBAL_LOGO:
		notify.nt_ncode = NT_GLOBAL_LOGOUT;
		break;
	case IN_PORT_CHANGED:
		notify.nt_ncode = NT_CHANGED;
		break;
	default:
		isp_prt(isp, ISP_LOGINFO, "%s: unhandled status (0x%x)",
		    __func__, status);
		isp_async(isp, ISPASYNC_TARGET_NOTIFY_ACK, inp);
		return;
	}
	isp_async(isp, ISPASYNC_TARGET_NOTIFY, &notify);
}

static void
isp_handle_notify_24xx(ispsoftc_t *isp, in_fcentry_24xx_t *inot)
{
	uint8_t chan;
	uint16_t nphdl, prli_options = 0;
	uint32_t portid;
	fcportdb_t *lp;
	char *msg = NULL;
	uint8_t *ptr = (uint8_t *)inot;
	uint64_t wwpn = INI_NONE, wwnn = INI_NONE;
	isp_notify_t notify;
	char buf[16];

	nphdl = inot->in_nphdl;
	if (nphdl != NIL_HANDLE) {
		portid = inot->in_portid_hi << 16 | inot->in_portid_lo;
	} else {
		portid = PORT_ANY;
	}

	chan = ISP_GET_VPIDX(isp, inot->in_vpidx);
	if (chan >= isp->isp_nchan &&
	    inot->in_status != IN24XX_LIP_RESET &&
	    inot->in_status != IN24XX_LINK_RESET &&
	    inot->in_status != IN24XX_LINK_FAILED) {
		isp_prt(isp, ISP_LOGWARN, "%s: Received INOT with status %x on VP %x",
		    __func__, inot->in_status, chan);
		isp_async(isp, ISPASYNC_TARGET_NOTIFY_ACK, inot);
		return;
	}

	switch (inot->in_status) {
	case IN24XX_ELS_RCVD:
	{
		/*
		 * Note that we're just getting notification that an ELS was
		 * received (possibly with some associated information sent
		 * upstream).  This is *not* the same as being given the ELS
		 * frame to accept or reject.
		 */
		switch (inot->in_status_subcode) {
		case LOGO:
			msg = "LOGO";
			wwpn = be64dec(&ptr[IN24XX_PLOGI_WWPN_OFF]);
			isp_del_wwn_entry(isp, chan, wwpn, nphdl, portid);
			break;
		case PRLO:
			msg = "PRLO";
			break;
		case PLOGI:
			msg = "PLOGI";
			wwnn = be64dec(&ptr[IN24XX_PLOGI_WWNN_OFF]);
			wwpn = be64dec(&ptr[IN24XX_PLOGI_WWPN_OFF]);
			isp_add_wwn_entry(isp, chan, wwpn, wwnn,
			    nphdl, portid, prli_options);
			break;
		case PRLI:
			msg = "PRLI";
			prli_options = inot->in_prli_options;
			if (inot->in_flags & IN24XX_FLAG_PN_NN_VALID)
				wwnn = be64dec(&ptr[IN24XX_PRLI_WWNN_OFF]);
			wwpn = be64dec(&ptr[IN24XX_PRLI_WWPN_OFF]);
			isp_add_wwn_entry(isp, chan, wwpn, wwnn,
			    nphdl, portid, prli_options);
			break;
		case PDISC:
			msg = "PDISC";
			break;
		case ADISC:
			msg = "ADISC";
			break;
		default:
			ISP_SNPRINTF(buf, sizeof (buf), "ELS 0x%x",
			    inot->in_status_subcode);
			msg = buf;
			break;
		}
		if (inot->in_flags & IN24XX_FLAG_PUREX_IOCB) {
			isp_prt(isp, ISP_LOGERR, "%s Chan %d ELS N-port handle %x"
			    " PortID 0x%06x marked as needing a PUREX response",
			    msg, chan, nphdl, portid);
			break;
		}
		isp_prt(isp, ISP_LOGTDEBUG0, "%s Chan %d ELS N-port handle %x"
		    " PortID 0x%06x RX_ID 0x%x OX_ID 0x%x", msg, chan, nphdl,
		    portid, inot->in_rxid, inot->in_oxid);
		isp_async(isp, ISPASYNC_TARGET_NOTIFY_ACK, inot);
		break;
	}

	case IN24XX_PORT_LOGOUT:
		msg = "PORT LOGOUT";
		if (isp_find_pdb_by_handle(isp, chan, nphdl, &lp))
			isp_del_wwn_entry(isp, chan, lp->port_wwn, nphdl, lp->portid);
		/* FALLTHROUGH */
	case IN24XX_PORT_CHANGED:
		if (msg == NULL)
			msg = "PORT CHANGED";
		/* FALLTHROUGH */
	case IN24XX_LIP_RESET:
		if (msg == NULL)
			msg = "LIP RESET";
		isp_prt(isp, ISP_LOGINFO, "Chan %d %s (sub-status 0x%x) for "
		    "N-port handle 0x%x",
		    chan, msg, inot->in_status_subcode, nphdl);

		/*
		 * All subcodes here are irrelevant. What is relevant
		 * is that we need to terminate all active commands from
		 * this initiator (known by N-port handle).
		 */
		/* XXX IMPLEMENT XXX */
		isp_async(isp, ISPASYNC_TARGET_NOTIFY_ACK, inot);
		break;

	case IN24XX_SRR_RCVD:
#ifdef	ISP_TARGET_MODE
		ISP_MEMZERO(&notify, sizeof (isp_notify_t));
		notify.nt_hba = isp;
		notify.nt_wwn = INI_ANY;
		notify.nt_tgt = FCPARAM(isp, chan)->isp_wwpn;
		notify.nt_nphdl = nphdl;
		notify.nt_sid = portid;
		notify.nt_did = PORT_ANY;
		notify.nt_lun = LUN_ANY;
		notify.nt_tagval = inot->in_rxid;
		notify.nt_tagval |= ((uint64_t)inot->in_srr_rxid << 32);
		notify.nt_need_ack = 1;
		notify.nt_channel = chan;
		notify.nt_lreserved = inot;
		notify.nt_ncode = NT_SRR;
		isp_async(isp, ISPASYNC_TARGET_NOTIFY, &notify);
		break;
#else
		if (msg == NULL)
			msg = "SRR RCVD";
		/* FALLTHROUGH */
#endif
	case IN24XX_LINK_RESET:
		if (msg == NULL)
			msg = "LINK RESET";
	case IN24XX_LINK_FAILED:
		if (msg == NULL)
			msg = "LINK FAILED";
	default:
		isp_prt(isp, ISP_LOGWARN, "Chan %d %s", chan, msg);
		isp_async(isp, ISPASYNC_TARGET_NOTIFY_ACK, inot);
		break;
	}
}
#endif
