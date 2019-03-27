/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 * $FreeBSD$
 */

/*
 * CTL frontend for the iSCSI protocol.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/condvar.h>
#include <sys/endian.h>
#include <sys/file.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/unistd.h>
#include <sys/nv.h>
#include <sys/dnv.h>
#include <vm/uma.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_error.h>
#include <cam/ctl/ctl_frontend.h>
#include <cam/ctl/ctl_debug.h>
#include <cam/ctl/ctl_ha.h>
#include <cam/ctl/ctl_ioctl.h>
#include <cam/ctl/ctl_private.h>

#include <dev/iscsi/icl.h>
#include <dev/iscsi/icl_wrappers.h>
#include <dev/iscsi/iscsi_proto.h>
#include <cam/ctl/ctl_frontend_iscsi.h>

#ifdef ICL_KERNEL_PROXY
#include <sys/socketvar.h>
#endif

#ifdef ICL_KERNEL_PROXY
FEATURE(cfiscsi_kernel_proxy, "iSCSI target built with ICL_KERNEL_PROXY");
#endif

static MALLOC_DEFINE(M_CFISCSI, "cfiscsi", "Memory used for CTL iSCSI frontend");
static uma_zone_t cfiscsi_data_wait_zone;

SYSCTL_NODE(_kern_cam_ctl, OID_AUTO, iscsi, CTLFLAG_RD, 0,
    "CAM Target Layer iSCSI Frontend");
static int debug = 1;
SYSCTL_INT(_kern_cam_ctl_iscsi, OID_AUTO, debug, CTLFLAG_RWTUN,
    &debug, 1, "Enable debug messages");
static int ping_timeout = 5;
SYSCTL_INT(_kern_cam_ctl_iscsi, OID_AUTO, ping_timeout, CTLFLAG_RWTUN,
    &ping_timeout, 5, "Interval between ping (NOP-Out) requests, in seconds");
static int login_timeout = 60;
SYSCTL_INT(_kern_cam_ctl_iscsi, OID_AUTO, login_timeout, CTLFLAG_RWTUN,
    &login_timeout, 60, "Time to wait for ctld(8) to finish Login Phase, in seconds");
static int maxtags = 256;
SYSCTL_INT(_kern_cam_ctl_iscsi, OID_AUTO, maxtags, CTLFLAG_RWTUN,
    &maxtags, 0, "Max number of requests queued by initiator");

#define	CFISCSI_DEBUG(X, ...)						\
	do {								\
		if (debug > 1) {					\
			printf("%s: " X "\n",				\
			    __func__, ## __VA_ARGS__);			\
		}							\
	} while (0)

#define	CFISCSI_WARN(X, ...)						\
	do {								\
		if (debug > 0) {					\
			printf("WARNING: %s: " X "\n",			\
			    __func__, ## __VA_ARGS__);			\
		}							\
	} while (0)

#define	CFISCSI_SESSION_DEBUG(S, X, ...)				\
	do {								\
		if (debug > 1) {					\
			printf("%s: %s (%s): " X "\n",			\
			    __func__, S->cs_initiator_addr,		\
			    S->cs_initiator_name, ## __VA_ARGS__);	\
		}							\
	} while (0)

#define	CFISCSI_SESSION_WARN(S, X, ...)					\
	do  {								\
		if (debug > 0) {					\
			printf("WARNING: %s (%s): " X "\n",		\
			    S->cs_initiator_addr,			\
			    S->cs_initiator_name, ## __VA_ARGS__);	\
		}							\
	} while (0)

#define CFISCSI_SESSION_LOCK(X)		mtx_lock(&X->cs_lock)
#define CFISCSI_SESSION_UNLOCK(X)	mtx_unlock(&X->cs_lock)
#define CFISCSI_SESSION_LOCK_ASSERT(X)	mtx_assert(&X->cs_lock, MA_OWNED)

#define	CONN_SESSION(X)			((struct cfiscsi_session *)(X)->ic_prv0)
#define	PDU_SESSION(X)			CONN_SESSION((X)->ip_conn)
#define	PDU_EXPDATASN(X)		(X)->ip_prv0
#define	PDU_TOTAL_TRANSFER_LEN(X)	(X)->ip_prv1
#define	PDU_R2TSN(X)			(X)->ip_prv2

static int	cfiscsi_init(void);
static int	cfiscsi_shutdown(void);
static void	cfiscsi_online(void *arg);
static void	cfiscsi_offline(void *arg);
static int	cfiscsi_info(void *arg, struct sbuf *sb);
static int	cfiscsi_ioctl(struct cdev *dev,
		    u_long cmd, caddr_t addr, int flag, struct thread *td);
static void	cfiscsi_datamove(union ctl_io *io);
static void	cfiscsi_datamove_in(union ctl_io *io);
static void	cfiscsi_datamove_out(union ctl_io *io);
static void	cfiscsi_done(union ctl_io *io);
static bool	cfiscsi_pdu_update_cmdsn(const struct icl_pdu *request);
static void	cfiscsi_pdu_handle_nop_out(struct icl_pdu *request);
static void	cfiscsi_pdu_handle_scsi_command(struct icl_pdu *request);
static void	cfiscsi_pdu_handle_task_request(struct icl_pdu *request);
static void	cfiscsi_pdu_handle_data_out(struct icl_pdu *request);
static void	cfiscsi_pdu_handle_logout_request(struct icl_pdu *request);
static void	cfiscsi_session_terminate(struct cfiscsi_session *cs);
static struct cfiscsi_data_wait	*cfiscsi_data_wait_new(
		    struct cfiscsi_session *cs, union ctl_io *io,
		    uint32_t initiator_task_tag,
		    uint32_t *target_transfer_tagp);
static void	cfiscsi_data_wait_free(struct cfiscsi_session *cs,
		    struct cfiscsi_data_wait *cdw);
static struct cfiscsi_target	*cfiscsi_target_find(struct cfiscsi_softc
		    *softc, const char *name, uint16_t tag);
static struct cfiscsi_target	*cfiscsi_target_find_or_create(
    struct cfiscsi_softc *softc, const char *name, const char *alias,
    uint16_t tag);
static void	cfiscsi_target_release(struct cfiscsi_target *ct);
static void	cfiscsi_session_delete(struct cfiscsi_session *cs);

static struct cfiscsi_softc cfiscsi_softc;

static struct ctl_frontend cfiscsi_frontend =
{
	.name = "iscsi",
	.init = cfiscsi_init,
	.ioctl = cfiscsi_ioctl,
	.shutdown = cfiscsi_shutdown,
};
CTL_FRONTEND_DECLARE(cfiscsi, cfiscsi_frontend);
MODULE_DEPEND(cfiscsi, icl, 1, 1, 1);

static struct icl_pdu *
cfiscsi_pdu_new_response(struct icl_pdu *request, int flags)
{

	return (icl_pdu_new(request->ip_conn, flags));
}

static bool
cfiscsi_pdu_update_cmdsn(const struct icl_pdu *request)
{
	const struct iscsi_bhs_scsi_command *bhssc;
	struct cfiscsi_session *cs;
	uint32_t cmdsn, expstatsn;

	cs = PDU_SESSION(request);

	/*
	 * Every incoming PDU - not just NOP-Out - resets the ping timer.
	 * The purpose of the timeout is to reset the connection when it stalls;
	 * we don't want this to happen when NOP-In or NOP-Out ends up delayed
	 * in some queue.
	 *
	 * XXX: Locking?
	 */
	cs->cs_timeout = 0;

	/*
	 * Data-Out PDUs don't contain CmdSN.
	 */
	if ((request->ip_bhs->bhs_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) ==
	    ISCSI_BHS_OPCODE_SCSI_DATA_OUT)
		return (false);

	/*
	 * We're only using fields common for all the request
	 * (initiator -> target) PDUs.
	 */
	bhssc = (const struct iscsi_bhs_scsi_command *)request->ip_bhs;
	cmdsn = ntohl(bhssc->bhssc_cmdsn);
	expstatsn = ntohl(bhssc->bhssc_expstatsn);

	CFISCSI_SESSION_LOCK(cs);
#if 0
	if (expstatsn != cs->cs_statsn) {
		CFISCSI_SESSION_DEBUG(cs, "received PDU with ExpStatSN %d, "
		    "while current StatSN is %d", expstatsn,
		    cs->cs_statsn);
	}
#endif

	if ((request->ip_bhs->bhs_opcode & ISCSI_BHS_OPCODE_IMMEDIATE) == 0) {
		/*
		 * The target MUST silently ignore any non-immediate command
		 * outside of this range.
		 */
		if (ISCSI_SNLT(cmdsn, cs->cs_cmdsn) ||
		    ISCSI_SNGT(cmdsn, cs->cs_cmdsn - 1 + maxtags)) {
			CFISCSI_SESSION_UNLOCK(cs);
			CFISCSI_SESSION_WARN(cs, "received PDU with CmdSN %u, "
			    "while expected %u", cmdsn, cs->cs_cmdsn);
			return (true);
		}

		/*
		 * We don't support multiple connections now, so any
		 * discontinuity in CmdSN means lost PDUs.  Since we don't
		 * support PDU retransmission -- terminate the connection.
		 */
		if (cmdsn != cs->cs_cmdsn) {
			CFISCSI_SESSION_UNLOCK(cs);
			CFISCSI_SESSION_WARN(cs, "received PDU with CmdSN %u, "
			    "while expected %u; dropping connection",
			    cmdsn, cs->cs_cmdsn);
			cfiscsi_session_terminate(cs);
			return (true);
		}
		cs->cs_cmdsn++;
	}

	CFISCSI_SESSION_UNLOCK(cs);

	return (false);
}

static void
cfiscsi_pdu_handle(struct icl_pdu *request)
{
	struct cfiscsi_session *cs;
	bool ignore;

	cs = PDU_SESSION(request);

	ignore = cfiscsi_pdu_update_cmdsn(request);
	if (ignore) {
		icl_pdu_free(request);
		return;
	}

	/*
	 * Handle the PDU; this includes e.g. receiving the remaining
	 * part of PDU and submitting the SCSI command to CTL
	 * or queueing a reply.  The handling routine is responsible
	 * for freeing the PDU when it's no longer needed.
	 */
	switch (request->ip_bhs->bhs_opcode &
	    ~ISCSI_BHS_OPCODE_IMMEDIATE) {
	case ISCSI_BHS_OPCODE_NOP_OUT:
		cfiscsi_pdu_handle_nop_out(request);
		break;
	case ISCSI_BHS_OPCODE_SCSI_COMMAND:
		cfiscsi_pdu_handle_scsi_command(request);
		break;
	case ISCSI_BHS_OPCODE_TASK_REQUEST:
		cfiscsi_pdu_handle_task_request(request);
		break;
	case ISCSI_BHS_OPCODE_SCSI_DATA_OUT:
		cfiscsi_pdu_handle_data_out(request);
		break;
	case ISCSI_BHS_OPCODE_LOGOUT_REQUEST:
		cfiscsi_pdu_handle_logout_request(request);
		break;
	default:
		CFISCSI_SESSION_WARN(cs, "received PDU with unsupported "
		    "opcode 0x%x; dropping connection",
		    request->ip_bhs->bhs_opcode);
		icl_pdu_free(request);
		cfiscsi_session_terminate(cs);
	}

}

static void
cfiscsi_receive_callback(struct icl_pdu *request)
{
#ifdef ICL_KERNEL_PROXY
	struct cfiscsi_session *cs;

	cs = PDU_SESSION(request);
	if (cs->cs_waiting_for_ctld || cs->cs_login_phase) {
		if (cs->cs_login_pdu == NULL)
			cs->cs_login_pdu = request;
		else
			icl_pdu_free(request);
		cv_signal(&cs->cs_login_cv);
		return;
	}
#endif

	cfiscsi_pdu_handle(request);
}

static void
cfiscsi_error_callback(struct icl_conn *ic)
{
	struct cfiscsi_session *cs;

	cs = CONN_SESSION(ic);

	CFISCSI_SESSION_WARN(cs, "connection error; dropping connection");
	cfiscsi_session_terminate(cs);
}

static int
cfiscsi_pdu_prepare(struct icl_pdu *response)
{
	struct cfiscsi_session *cs;
	struct iscsi_bhs_scsi_response *bhssr;
	bool advance_statsn = true;

	cs = PDU_SESSION(response);

	CFISCSI_SESSION_LOCK_ASSERT(cs);

	/*
	 * We're only using fields common for all the response
	 * (target -> initiator) PDUs.
	 */
	bhssr = (struct iscsi_bhs_scsi_response *)response->ip_bhs;

	/*
	 * 10.8.3: "The StatSN for this connection is not advanced
	 * after this PDU is sent."
	 */
	if (bhssr->bhssr_opcode == ISCSI_BHS_OPCODE_R2T)
		advance_statsn = false;

	/*
	 * 10.19.2: "However, when the Initiator Task Tag is set to 0xffffffff,
	 * StatSN for the connection is not advanced after this PDU is sent."
	 */
	if (bhssr->bhssr_opcode == ISCSI_BHS_OPCODE_NOP_IN && 
	    bhssr->bhssr_initiator_task_tag == 0xffffffff)
		advance_statsn = false;

	/*
	 * See the comment below - StatSN is not meaningful and must
	 * not be advanced.
	 */
	if (bhssr->bhssr_opcode == ISCSI_BHS_OPCODE_SCSI_DATA_IN &&
	    (bhssr->bhssr_flags & BHSDI_FLAGS_S) == 0)
		advance_statsn = false;

	/*
	 * 10.7.3: "The fields StatSN, Status, and Residual Count
	 * only have meaningful content if the S bit is set to 1."
	 */
	if (bhssr->bhssr_opcode != ISCSI_BHS_OPCODE_SCSI_DATA_IN ||
	    (bhssr->bhssr_flags & BHSDI_FLAGS_S))
		bhssr->bhssr_statsn = htonl(cs->cs_statsn);
	bhssr->bhssr_expcmdsn = htonl(cs->cs_cmdsn);
	bhssr->bhssr_maxcmdsn = htonl(cs->cs_cmdsn - 1 +
	    imax(0, maxtags - cs->cs_outstanding_ctl_pdus));

	if (advance_statsn)
		cs->cs_statsn++;

	return (0);
}

static void
cfiscsi_pdu_queue(struct icl_pdu *response)
{
	struct cfiscsi_session *cs;

	cs = PDU_SESSION(response);

	CFISCSI_SESSION_LOCK(cs);
	cfiscsi_pdu_prepare(response);
	icl_pdu_queue(response);
	CFISCSI_SESSION_UNLOCK(cs);
}

static void
cfiscsi_pdu_handle_nop_out(struct icl_pdu *request)
{
	struct cfiscsi_session *cs;
	struct iscsi_bhs_nop_out *bhsno;
	struct iscsi_bhs_nop_in *bhsni;
	struct icl_pdu *response;
	void *data = NULL;
	size_t datasize;
	int error;

	cs = PDU_SESSION(request);
	bhsno = (struct iscsi_bhs_nop_out *)request->ip_bhs;

	if (bhsno->bhsno_initiator_task_tag == 0xffffffff) {
		/*
		 * Nothing to do, iscsi_pdu_update_statsn() already
		 * zeroed the timeout.
		 */
		icl_pdu_free(request);
		return;
	}

	datasize = icl_pdu_data_segment_length(request);
	if (datasize > 0) {
		data = malloc(datasize, M_CFISCSI, M_NOWAIT | M_ZERO);
		if (data == NULL) {
			CFISCSI_SESSION_WARN(cs, "failed to allocate memory; "
			    "dropping connection");
			icl_pdu_free(request);
			cfiscsi_session_terminate(cs);
			return;
		}
		icl_pdu_get_data(request, 0, data, datasize);
	}

	response = cfiscsi_pdu_new_response(request, M_NOWAIT);
	if (response == NULL) {
		CFISCSI_SESSION_WARN(cs, "failed to allocate memory; "
		    "droppping connection");
		free(data, M_CFISCSI);
		icl_pdu_free(request);
		cfiscsi_session_terminate(cs);
		return;
	}
	bhsni = (struct iscsi_bhs_nop_in *)response->ip_bhs;
	bhsni->bhsni_opcode = ISCSI_BHS_OPCODE_NOP_IN;
	bhsni->bhsni_flags = 0x80;
	bhsni->bhsni_initiator_task_tag = bhsno->bhsno_initiator_task_tag;
	bhsni->bhsni_target_transfer_tag = 0xffffffff;
	if (datasize > 0) {
		error = icl_pdu_append_data(response, data, datasize, M_NOWAIT);
		if (error != 0) {
			CFISCSI_SESSION_WARN(cs, "failed to allocate memory; "
			    "dropping connection");
			free(data, M_CFISCSI);
			icl_pdu_free(request);
			icl_pdu_free(response);
			cfiscsi_session_terminate(cs);
			return;
		}
		free(data, M_CFISCSI);
	}

	icl_pdu_free(request);
	cfiscsi_pdu_queue(response);
}

static void
cfiscsi_pdu_handle_scsi_command(struct icl_pdu *request)
{
	struct iscsi_bhs_scsi_command *bhssc;
	struct cfiscsi_session *cs;
	union ctl_io *io;
	int error;

	cs = PDU_SESSION(request);
	bhssc = (struct iscsi_bhs_scsi_command *)request->ip_bhs;
	//CFISCSI_SESSION_DEBUG(cs, "initiator task tag 0x%x",
	//    bhssc->bhssc_initiator_task_tag);

	if (request->ip_data_len > 0 && cs->cs_immediate_data == false) {
		CFISCSI_SESSION_WARN(cs, "unsolicited data with "
		    "ImmediateData=No; dropping connection");
		icl_pdu_free(request);
		cfiscsi_session_terminate(cs);
		return;
	}
	io = ctl_alloc_io(cs->cs_target->ct_port.ctl_pool_ref);
	ctl_zero_io(io);
	io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = request;
	io->io_hdr.io_type = CTL_IO_SCSI;
	io->io_hdr.nexus.initid = cs->cs_ctl_initid;
	io->io_hdr.nexus.targ_port = cs->cs_target->ct_port.targ_port;
	io->io_hdr.nexus.targ_lun = ctl_decode_lun(be64toh(bhssc->bhssc_lun));
	io->scsiio.tag_num = bhssc->bhssc_initiator_task_tag;
	switch ((bhssc->bhssc_flags & BHSSC_FLAGS_ATTR)) {
	case BHSSC_FLAGS_ATTR_UNTAGGED:
		io->scsiio.tag_type = CTL_TAG_UNTAGGED;
		break;
	case BHSSC_FLAGS_ATTR_SIMPLE:
		io->scsiio.tag_type = CTL_TAG_SIMPLE;
		break;
	case BHSSC_FLAGS_ATTR_ORDERED:
        	io->scsiio.tag_type = CTL_TAG_ORDERED;
		break;
	case BHSSC_FLAGS_ATTR_HOQ:
        	io->scsiio.tag_type = CTL_TAG_HEAD_OF_QUEUE;
		break;
	case BHSSC_FLAGS_ATTR_ACA:
		io->scsiio.tag_type = CTL_TAG_ACA;
		break;
	default:
		io->scsiio.tag_type = CTL_TAG_UNTAGGED;
		CFISCSI_SESSION_WARN(cs, "unhandled tag type %d",
		    bhssc->bhssc_flags & BHSSC_FLAGS_ATTR);
		break;
	}
	io->scsiio.cdb_len = sizeof(bhssc->bhssc_cdb); /* Which is 16. */
	memcpy(io->scsiio.cdb, bhssc->bhssc_cdb, sizeof(bhssc->bhssc_cdb));
	refcount_acquire(&cs->cs_outstanding_ctl_pdus);
	error = ctl_queue(io);
	if (error != CTL_RETVAL_COMPLETE) {
		CFISCSI_SESSION_WARN(cs, "ctl_queue() failed; error %d; "
		    "dropping connection", error);
		ctl_free_io(io);
		refcount_release(&cs->cs_outstanding_ctl_pdus);
		icl_pdu_free(request);
		cfiscsi_session_terminate(cs);
	}
}

static void
cfiscsi_pdu_handle_task_request(struct icl_pdu *request)
{
	struct iscsi_bhs_task_management_request *bhstmr;
	struct iscsi_bhs_task_management_response *bhstmr2;
	struct icl_pdu *response;
	struct cfiscsi_session *cs;
	union ctl_io *io;
	int error;

	cs = PDU_SESSION(request);
	bhstmr = (struct iscsi_bhs_task_management_request *)request->ip_bhs;
	io = ctl_alloc_io(cs->cs_target->ct_port.ctl_pool_ref);
	ctl_zero_io(io);
	io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = request;
	io->io_hdr.io_type = CTL_IO_TASK;
	io->io_hdr.nexus.initid = cs->cs_ctl_initid;
	io->io_hdr.nexus.targ_port = cs->cs_target->ct_port.targ_port;
	io->io_hdr.nexus.targ_lun = ctl_decode_lun(be64toh(bhstmr->bhstmr_lun));
	io->taskio.tag_type = CTL_TAG_SIMPLE; /* XXX */

	switch (bhstmr->bhstmr_function & ~0x80) {
	case BHSTMR_FUNCTION_ABORT_TASK:
#if 0
		CFISCSI_SESSION_DEBUG(cs, "BHSTMR_FUNCTION_ABORT_TASK");
#endif
		io->taskio.task_action = CTL_TASK_ABORT_TASK;
		io->taskio.tag_num = bhstmr->bhstmr_referenced_task_tag;
		break;
	case BHSTMR_FUNCTION_ABORT_TASK_SET:
#if 0
		CFISCSI_SESSION_DEBUG(cs, "BHSTMR_FUNCTION_ABORT_TASK_SET");
#endif
		io->taskio.task_action = CTL_TASK_ABORT_TASK_SET;
		break;
	case BHSTMR_FUNCTION_CLEAR_TASK_SET:
#if 0
		CFISCSI_SESSION_DEBUG(cs, "BHSTMR_FUNCTION_CLEAR_TASK_SET");
#endif
		io->taskio.task_action = CTL_TASK_CLEAR_TASK_SET;
		break;
	case BHSTMR_FUNCTION_LOGICAL_UNIT_RESET:
#if 0
		CFISCSI_SESSION_DEBUG(cs, "BHSTMR_FUNCTION_LOGICAL_UNIT_RESET");
#endif
		io->taskio.task_action = CTL_TASK_LUN_RESET;
		break;
	case BHSTMR_FUNCTION_TARGET_WARM_RESET:
#if 0
		CFISCSI_SESSION_DEBUG(cs, "BHSTMR_FUNCTION_TARGET_WARM_RESET");
#endif
		io->taskio.task_action = CTL_TASK_TARGET_RESET;
		break;
	case BHSTMR_FUNCTION_TARGET_COLD_RESET:
#if 0
		CFISCSI_SESSION_DEBUG(cs, "BHSTMR_FUNCTION_TARGET_COLD_RESET");
#endif
		io->taskio.task_action = CTL_TASK_TARGET_RESET;
		break;
	case BHSTMR_FUNCTION_QUERY_TASK:
#if 0
		CFISCSI_SESSION_DEBUG(cs, "BHSTMR_FUNCTION_QUERY_TASK");
#endif
		io->taskio.task_action = CTL_TASK_QUERY_TASK;
		io->taskio.tag_num = bhstmr->bhstmr_referenced_task_tag;
		break;
	case BHSTMR_FUNCTION_QUERY_TASK_SET:
#if 0
		CFISCSI_SESSION_DEBUG(cs, "BHSTMR_FUNCTION_QUERY_TASK_SET");
#endif
		io->taskio.task_action = CTL_TASK_QUERY_TASK_SET;
		break;
	case BHSTMR_FUNCTION_I_T_NEXUS_RESET:
#if 0
		CFISCSI_SESSION_DEBUG(cs, "BHSTMR_FUNCTION_I_T_NEXUS_RESET");
#endif
		io->taskio.task_action = CTL_TASK_I_T_NEXUS_RESET;
		break;
	case BHSTMR_FUNCTION_QUERY_ASYNC_EVENT:
#if 0
		CFISCSI_SESSION_DEBUG(cs, "BHSTMR_FUNCTION_QUERY_ASYNC_EVENT");
#endif
		io->taskio.task_action = CTL_TASK_QUERY_ASYNC_EVENT;
		break;
	default:
		CFISCSI_SESSION_DEBUG(cs, "unsupported function 0x%x",
		    bhstmr->bhstmr_function & ~0x80);
		ctl_free_io(io);

		response = cfiscsi_pdu_new_response(request, M_NOWAIT);
		if (response == NULL) {
			CFISCSI_SESSION_WARN(cs, "failed to allocate memory; "
			    "dropping connection");
			icl_pdu_free(request);
			cfiscsi_session_terminate(cs);
			return;
		}
		bhstmr2 = (struct iscsi_bhs_task_management_response *)
		    response->ip_bhs;
		bhstmr2->bhstmr_opcode = ISCSI_BHS_OPCODE_TASK_RESPONSE;
		bhstmr2->bhstmr_flags = 0x80;
		bhstmr2->bhstmr_response =
		    BHSTMR_RESPONSE_FUNCTION_NOT_SUPPORTED;
		bhstmr2->bhstmr_initiator_task_tag =
		    bhstmr->bhstmr_initiator_task_tag;
		icl_pdu_free(request);
		cfiscsi_pdu_queue(response);
		return;
	}

	refcount_acquire(&cs->cs_outstanding_ctl_pdus);
	error = ctl_queue(io);
	if (error != CTL_RETVAL_COMPLETE) {
		CFISCSI_SESSION_WARN(cs, "ctl_queue() failed; error %d; "
		    "dropping connection", error);
		ctl_free_io(io);
		refcount_release(&cs->cs_outstanding_ctl_pdus);
		icl_pdu_free(request);
		cfiscsi_session_terminate(cs);
	}
}

static bool
cfiscsi_handle_data_segment(struct icl_pdu *request, struct cfiscsi_data_wait *cdw)
{
	struct iscsi_bhs_data_out *bhsdo;
	struct cfiscsi_session *cs;
	struct ctl_sg_entry ctl_sg_entry, *ctl_sglist;
	size_t copy_len, len, off, buffer_offset;
	int ctl_sg_count;
	union ctl_io *io;

	cs = PDU_SESSION(request);

	KASSERT((request->ip_bhs->bhs_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) ==
	    ISCSI_BHS_OPCODE_SCSI_DATA_OUT ||
	    (request->ip_bhs->bhs_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) ==
	    ISCSI_BHS_OPCODE_SCSI_COMMAND,
	    ("bad opcode 0x%x", request->ip_bhs->bhs_opcode));

	/*
	 * We're only using fields common for Data-Out and SCSI Command PDUs.
	 */
	bhsdo = (struct iscsi_bhs_data_out *)request->ip_bhs;

	io = cdw->cdw_ctl_io;
	KASSERT((io->io_hdr.flags & CTL_FLAG_DATA_MASK) != CTL_FLAG_DATA_IN,
	    ("CTL_FLAG_DATA_IN"));

#if 0
	CFISCSI_SESSION_DEBUG(cs, "received %zd bytes out of %d",
	    request->ip_data_len, io->scsiio.kern_total_len);
#endif

	if (io->scsiio.kern_sg_entries > 0) {
		ctl_sglist = (struct ctl_sg_entry *)io->scsiio.kern_data_ptr;
		ctl_sg_count = io->scsiio.kern_sg_entries;
	} else {
		ctl_sglist = &ctl_sg_entry;
		ctl_sglist->addr = io->scsiio.kern_data_ptr;
		ctl_sglist->len = io->scsiio.kern_data_len;
		ctl_sg_count = 1;
	}

	if ((request->ip_bhs->bhs_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) ==
	    ISCSI_BHS_OPCODE_SCSI_DATA_OUT)
		buffer_offset = ntohl(bhsdo->bhsdo_buffer_offset);
	else
		buffer_offset = 0;
	len = icl_pdu_data_segment_length(request);

	/*
	 * Make sure the offset, as sent by the initiator, matches the offset
	 * we're supposed to be at in the scatter-gather list.
	 */
	if (buffer_offset >
	    io->scsiio.kern_rel_offset + io->scsiio.ext_data_filled ||
	    buffer_offset + len <=
	    io->scsiio.kern_rel_offset + io->scsiio.ext_data_filled) {
		CFISCSI_SESSION_WARN(cs, "received bad buffer offset %zd, "
		    "expected %zd; dropping connection", buffer_offset,
		    (size_t)io->scsiio.kern_rel_offset +
		    (size_t)io->scsiio.ext_data_filled);
		ctl_set_data_phase_error(&io->scsiio);
		cfiscsi_session_terminate(cs);
		return (true);
	}

	/*
	 * This is the offset within the PDU data segment, as opposed
	 * to buffer_offset, which is the offset within the task (SCSI
	 * command).
	 */
	off = io->scsiio.kern_rel_offset + io->scsiio.ext_data_filled -
	    buffer_offset;

	/*
	 * Iterate over the scatter/gather segments, filling them with data
	 * from the PDU data segment.  Note that this can get called multiple
	 * times for one SCSI command; the cdw structure holds state for the
	 * scatter/gather list.
	 */
	for (;;) {
		KASSERT(cdw->cdw_sg_index < ctl_sg_count,
		    ("cdw->cdw_sg_index >= ctl_sg_count"));
		if (cdw->cdw_sg_len == 0) {
			cdw->cdw_sg_addr = ctl_sglist[cdw->cdw_sg_index].addr;
			cdw->cdw_sg_len = ctl_sglist[cdw->cdw_sg_index].len;
		}
		KASSERT(off <= len, ("len > off"));
		copy_len = len - off;
		if (copy_len > cdw->cdw_sg_len)
			copy_len = cdw->cdw_sg_len;

		icl_pdu_get_data(request, off, cdw->cdw_sg_addr, copy_len);
		cdw->cdw_sg_addr += copy_len;
		cdw->cdw_sg_len -= copy_len;
		off += copy_len;
		io->scsiio.ext_data_filled += copy_len;
		io->scsiio.kern_data_resid -= copy_len;

		if (cdw->cdw_sg_len == 0) {
			/*
			 * End of current segment.
			 */
			if (cdw->cdw_sg_index == ctl_sg_count - 1) {
				/*
				 * Last segment in scatter/gather list.
				 */
				break;
			}
			cdw->cdw_sg_index++;
		}

		if (off == len) {
			/*
			 * End of PDU payload.
			 */
			break;
		}
	}

	if (len > off) {
		/*
		 * In case of unsolicited data, it's possible that the buffer
		 * provided by CTL is smaller than negotiated FirstBurstLength.
		 * Just ignore the superfluous data; will ask for them with R2T
		 * on next call to cfiscsi_datamove().
		 *
		 * This obviously can only happen with SCSI Command PDU. 
		 */
		if ((request->ip_bhs->bhs_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) ==
		    ISCSI_BHS_OPCODE_SCSI_COMMAND)
			return (true);

		CFISCSI_SESSION_WARN(cs, "received too much data: got %zd bytes, "
		    "expected %zd; dropping connection",
		    icl_pdu_data_segment_length(request), off);
		ctl_set_data_phase_error(&io->scsiio);
		cfiscsi_session_terminate(cs);
		return (true);
	}

	if (io->scsiio.ext_data_filled == cdw->cdw_r2t_end &&
	    (bhsdo->bhsdo_flags & BHSDO_FLAGS_F) == 0) {
		CFISCSI_SESSION_WARN(cs, "got the final packet without "
		    "the F flag; flags = 0x%x; dropping connection",
		    bhsdo->bhsdo_flags);
		ctl_set_data_phase_error(&io->scsiio);
		cfiscsi_session_terminate(cs);
		return (true);
	}

	if (io->scsiio.ext_data_filled != cdw->cdw_r2t_end &&
	    (bhsdo->bhsdo_flags & BHSDO_FLAGS_F) != 0) {
		if ((request->ip_bhs->bhs_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) ==
		    ISCSI_BHS_OPCODE_SCSI_DATA_OUT) {
			CFISCSI_SESSION_WARN(cs, "got the final packet, but the "
			    "transmitted size was %zd bytes instead of %d; "
			    "dropping connection",
			    (size_t)io->scsiio.ext_data_filled,
			    cdw->cdw_r2t_end);
			ctl_set_data_phase_error(&io->scsiio);
			cfiscsi_session_terminate(cs);
			return (true);
		} else {
			/*
			 * For SCSI Command PDU, this just means we need to
			 * solicit more data by sending R2T.
			 */
			return (false);
		}
	}

	if (io->scsiio.ext_data_filled == cdw->cdw_r2t_end) {
#if 0
		CFISCSI_SESSION_DEBUG(cs, "no longer expecting Data-Out with target "
		    "transfer tag 0x%x", cdw->cdw_target_transfer_tag);
#endif

		return (true);
	}

	return (false);
}

static void
cfiscsi_pdu_handle_data_out(struct icl_pdu *request)
{
	struct iscsi_bhs_data_out *bhsdo;
	struct cfiscsi_session *cs;
	struct cfiscsi_data_wait *cdw = NULL;
	union ctl_io *io;
	bool done;

	cs = PDU_SESSION(request);
	bhsdo = (struct iscsi_bhs_data_out *)request->ip_bhs;

	CFISCSI_SESSION_LOCK(cs);
	TAILQ_FOREACH(cdw, &cs->cs_waiting_for_data_out, cdw_next) {
#if 0
		CFISCSI_SESSION_DEBUG(cs, "have ttt 0x%x, itt 0x%x; looking for "
		    "ttt 0x%x, itt 0x%x",
		    bhsdo->bhsdo_target_transfer_tag,
		    bhsdo->bhsdo_initiator_task_tag,
		    cdw->cdw_target_transfer_tag, cdw->cdw_initiator_task_tag));
#endif
		if (bhsdo->bhsdo_target_transfer_tag ==
		    cdw->cdw_target_transfer_tag)
			break;
	}
	CFISCSI_SESSION_UNLOCK(cs);
	if (cdw == NULL) {
		CFISCSI_SESSION_WARN(cs, "data transfer tag 0x%x, initiator task tag "
		    "0x%x, not found; dropping connection",
		    bhsdo->bhsdo_target_transfer_tag, bhsdo->bhsdo_initiator_task_tag);
		icl_pdu_free(request);
		cfiscsi_session_terminate(cs);
		return;
	}

	if (cdw->cdw_datasn != ntohl(bhsdo->bhsdo_datasn)) {
		CFISCSI_SESSION_WARN(cs, "received Data-Out PDU with "
		    "DataSN %u, while expected %u; dropping connection",
		    ntohl(bhsdo->bhsdo_datasn), cdw->cdw_datasn);
		icl_pdu_free(request);
		cfiscsi_session_terminate(cs);
		return;
	}
	cdw->cdw_datasn++;

	io = cdw->cdw_ctl_io;
	KASSERT((io->io_hdr.flags & CTL_FLAG_DATA_MASK) != CTL_FLAG_DATA_IN,
	    ("CTL_FLAG_DATA_IN"));

	done = cfiscsi_handle_data_segment(request, cdw);
	if (done) {
		CFISCSI_SESSION_LOCK(cs);
		TAILQ_REMOVE(&cs->cs_waiting_for_data_out, cdw, cdw_next);
		CFISCSI_SESSION_UNLOCK(cs);
		done = (io->scsiio.ext_data_filled != cdw->cdw_r2t_end ||
		    io->scsiio.ext_data_filled == io->scsiio.kern_data_len);
		cfiscsi_data_wait_free(cs, cdw);
		io->io_hdr.flags &= ~CTL_FLAG_DMA_INPROG;
		if (done)
			io->scsiio.be_move_done(io);
		else
			cfiscsi_datamove_out(io);
	}

	icl_pdu_free(request);
}

static void
cfiscsi_pdu_handle_logout_request(struct icl_pdu *request)
{
	struct iscsi_bhs_logout_request *bhslr;
	struct iscsi_bhs_logout_response *bhslr2;
	struct icl_pdu *response;
	struct cfiscsi_session *cs;

	cs = PDU_SESSION(request);
	bhslr = (struct iscsi_bhs_logout_request *)request->ip_bhs;
	switch (bhslr->bhslr_reason & 0x7f) {
	case BHSLR_REASON_CLOSE_SESSION:
	case BHSLR_REASON_CLOSE_CONNECTION:
		response = cfiscsi_pdu_new_response(request, M_NOWAIT);
		if (response == NULL) {
			CFISCSI_SESSION_DEBUG(cs, "failed to allocate memory");
			icl_pdu_free(request);
			cfiscsi_session_terminate(cs);
			return;
		}
		bhslr2 = (struct iscsi_bhs_logout_response *)response->ip_bhs;
		bhslr2->bhslr_opcode = ISCSI_BHS_OPCODE_LOGOUT_RESPONSE;
		bhslr2->bhslr_flags = 0x80;
		bhslr2->bhslr_response = BHSLR_RESPONSE_CLOSED_SUCCESSFULLY;
		bhslr2->bhslr_initiator_task_tag =
		    bhslr->bhslr_initiator_task_tag;
		icl_pdu_free(request);
		cfiscsi_pdu_queue(response);
		cfiscsi_session_terminate(cs);
		break;
	case BHSLR_REASON_REMOVE_FOR_RECOVERY:
		response = cfiscsi_pdu_new_response(request, M_NOWAIT);
		if (response == NULL) {
			CFISCSI_SESSION_WARN(cs,
			    "failed to allocate memory; dropping connection");
			icl_pdu_free(request);
			cfiscsi_session_terminate(cs);
			return;
		}
		bhslr2 = (struct iscsi_bhs_logout_response *)response->ip_bhs;
		bhslr2->bhslr_opcode = ISCSI_BHS_OPCODE_LOGOUT_RESPONSE;
		bhslr2->bhslr_flags = 0x80;
		bhslr2->bhslr_response = BHSLR_RESPONSE_RECOVERY_NOT_SUPPORTED;
		bhslr2->bhslr_initiator_task_tag =
		    bhslr->bhslr_initiator_task_tag;
		icl_pdu_free(request);
		cfiscsi_pdu_queue(response);
		break;
	default:
		CFISCSI_SESSION_WARN(cs, "invalid reason 0%x; dropping connection",
		    bhslr->bhslr_reason);
		icl_pdu_free(request);
		cfiscsi_session_terminate(cs);
		break;
	}
}

static void
cfiscsi_callout(void *context)
{
	struct icl_pdu *cp;
	struct iscsi_bhs_nop_in *bhsni;
	struct cfiscsi_session *cs;

	cs = context;

	if (cs->cs_terminating) 
		return;

	callout_schedule(&cs->cs_callout, 1 * hz);

	atomic_add_int(&cs->cs_timeout, 1);

#ifdef ICL_KERNEL_PROXY
	if (cs->cs_waiting_for_ctld || cs->cs_login_phase) {
		if (login_timeout > 0 && cs->cs_timeout > login_timeout) {
			CFISCSI_SESSION_WARN(cs, "login timed out after "
			    "%d seconds; dropping connection", cs->cs_timeout);
			cfiscsi_session_terminate(cs);
		}
		return;
	}
#endif

	if (ping_timeout <= 0) {
		/*
		 * Pings are disabled.  Don't send NOP-In in this case;
		 * user might have disabled pings to work around problems
		 * with certain initiators that can't properly handle
		 * NOP-In, such as iPXE.  Reset the timeout, to avoid
		 * triggering reconnection, should the user decide to
		 * reenable them.
		 */
		cs->cs_timeout = 0;
		return;
	}

	if (cs->cs_timeout >= ping_timeout) {
		CFISCSI_SESSION_WARN(cs, "no ping reply (NOP-Out) after %d seconds; "
		    "dropping connection",  ping_timeout);
		cfiscsi_session_terminate(cs);
		return;
	}

	/*
	 * If the ping was reset less than one second ago - which means
	 * that we've received some PDU during the last second - assume
	 * the traffic flows correctly and don't bother sending a NOP-Out.
	 *
	 * (It's 2 - one for one second, and one for incrementing is_timeout
	 * earlier in this routine.)
	 */
	if (cs->cs_timeout < 2)
		return;

	cp = icl_pdu_new(cs->cs_conn, M_NOWAIT);
	if (cp == NULL) {
		CFISCSI_SESSION_WARN(cs, "failed to allocate memory");
		return;
	}
	bhsni = (struct iscsi_bhs_nop_in *)cp->ip_bhs;
	bhsni->bhsni_opcode = ISCSI_BHS_OPCODE_NOP_IN;
	bhsni->bhsni_flags = 0x80;
	bhsni->bhsni_initiator_task_tag = 0xffffffff;

	cfiscsi_pdu_queue(cp);
}

static struct cfiscsi_data_wait *
cfiscsi_data_wait_new(struct cfiscsi_session *cs, union ctl_io *io,
    uint32_t initiator_task_tag, uint32_t *target_transfer_tagp)
{
	struct cfiscsi_data_wait *cdw;
	int error;

	cdw = uma_zalloc(cfiscsi_data_wait_zone, M_NOWAIT | M_ZERO);
	if (cdw == NULL) {
		CFISCSI_SESSION_WARN(cs,
		    "failed to allocate %zd bytes", sizeof(*cdw));
		return (NULL);
	}

	error = icl_conn_transfer_setup(cs->cs_conn, io, target_transfer_tagp,
	    &cdw->cdw_icl_prv);
	if (error != 0) {
		CFISCSI_SESSION_WARN(cs,
		    "icl_conn_transfer_setup() failed with error %d", error);
		uma_zfree(cfiscsi_data_wait_zone, cdw);
		return (NULL);
	}

	cdw->cdw_ctl_io = io;
	cdw->cdw_target_transfer_tag = *target_transfer_tagp;
	cdw->cdw_initiator_task_tag = initiator_task_tag;

	return (cdw);
}

static void
cfiscsi_data_wait_free(struct cfiscsi_session *cs,
    struct cfiscsi_data_wait *cdw)
{

	icl_conn_transfer_done(cs->cs_conn, cdw->cdw_icl_prv);
	uma_zfree(cfiscsi_data_wait_zone, cdw);
}

static void
cfiscsi_session_terminate_tasks(struct cfiscsi_session *cs)
{
	struct cfiscsi_data_wait *cdw;
	union ctl_io *io;
	int error, last, wait;

	if (cs->cs_target == NULL)
		return;		/* No target yet, so nothing to do. */
	io = ctl_alloc_io(cs->cs_target->ct_port.ctl_pool_ref);
	ctl_zero_io(io);
	io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = cs;
	io->io_hdr.io_type = CTL_IO_TASK;
	io->io_hdr.nexus.initid = cs->cs_ctl_initid;
	io->io_hdr.nexus.targ_port = cs->cs_target->ct_port.targ_port;
	io->io_hdr.nexus.targ_lun = 0;
	io->taskio.tag_type = CTL_TAG_SIMPLE; /* XXX */
	io->taskio.task_action = CTL_TASK_I_T_NEXUS_RESET;
	wait = cs->cs_outstanding_ctl_pdus;
	refcount_acquire(&cs->cs_outstanding_ctl_pdus);
	error = ctl_queue(io);
	if (error != CTL_RETVAL_COMPLETE) {
		CFISCSI_SESSION_WARN(cs, "ctl_queue() failed; error %d", error);
		refcount_release(&cs->cs_outstanding_ctl_pdus);
		ctl_free_io(io);
	}

	CFISCSI_SESSION_LOCK(cs);
	while ((cdw = TAILQ_FIRST(&cs->cs_waiting_for_data_out)) != NULL) {
		TAILQ_REMOVE(&cs->cs_waiting_for_data_out, cdw, cdw_next);
		CFISCSI_SESSION_UNLOCK(cs);
		/*
		 * Set nonzero port status; this prevents backends from
		 * assuming that the data transfer actually succeeded
		 * and writing uninitialized data to disk.
		 */
		cdw->cdw_ctl_io->io_hdr.flags &= ~CTL_FLAG_DMA_INPROG;
		cdw->cdw_ctl_io->scsiio.io_hdr.port_status = 42;
		cdw->cdw_ctl_io->scsiio.be_move_done(cdw->cdw_ctl_io);
		cfiscsi_data_wait_free(cs, cdw);
		CFISCSI_SESSION_LOCK(cs);
	}
	CFISCSI_SESSION_UNLOCK(cs);

	/*
	 * Wait for CTL to terminate all the tasks.
	 */
	if (wait > 0)
		CFISCSI_SESSION_WARN(cs,
		    "waiting for CTL to terminate %d tasks", wait);
	for (;;) {
		refcount_acquire(&cs->cs_outstanding_ctl_pdus);
		last = refcount_release(&cs->cs_outstanding_ctl_pdus);
		if (last != 0)
			break;
		tsleep(__DEVOLATILE(void *, &cs->cs_outstanding_ctl_pdus),
		    0, "cfiscsi_terminate", hz / 100);
	}
	if (wait > 0)
		CFISCSI_SESSION_WARN(cs, "tasks terminated");
}

static void
cfiscsi_maintenance_thread(void *arg)
{
	struct cfiscsi_session *cs;

	cs = arg;

	for (;;) {
		CFISCSI_SESSION_LOCK(cs);
		if (cs->cs_terminating == false || cs->cs_handoff_in_progress)
			cv_wait(&cs->cs_maintenance_cv, &cs->cs_lock);
		CFISCSI_SESSION_UNLOCK(cs);

		if (cs->cs_terminating && cs->cs_handoff_in_progress == false) {

			/*
			 * We used to wait up to 30 seconds to deliver queued
			 * PDUs to the initiator.  We also tried hard to deliver
			 * SCSI Responses for the aborted PDUs.  We don't do
			 * that anymore.  We might need to revisit that.
			 */
			callout_drain(&cs->cs_callout);
			icl_conn_close(cs->cs_conn);

			/*
			 * At this point ICL receive thread is no longer
			 * running; no new tasks can be queued.
			 */
			cfiscsi_session_terminate_tasks(cs);
			cfiscsi_session_delete(cs);
			kthread_exit();
			return;
		}
		CFISCSI_SESSION_DEBUG(cs, "nothing to do");
	}
}

static void
cfiscsi_session_terminate(struct cfiscsi_session *cs)
{

	cs->cs_terminating = true;
	cv_signal(&cs->cs_maintenance_cv);
#ifdef ICL_KERNEL_PROXY
	cv_signal(&cs->cs_login_cv);
#endif
}

static int
cfiscsi_session_register_initiator(struct cfiscsi_session *cs)
{
	struct cfiscsi_target *ct;
	char *name;
	int i;

	KASSERT(cs->cs_ctl_initid == -1, ("already registered"));

	ct = cs->cs_target;
	name = strdup(cs->cs_initiator_id, M_CTL);
	i = ctl_add_initiator(&ct->ct_port, -1, 0, name);
	if (i < 0) {
		CFISCSI_SESSION_WARN(cs, "ctl_add_initiator failed with error %d",
		    i);
		cs->cs_ctl_initid = -1;
		return (1);
	}
	cs->cs_ctl_initid = i;
#if 0
	CFISCSI_SESSION_DEBUG(cs, "added initiator id %d", i);
#endif

	return (0);
}

static void
cfiscsi_session_unregister_initiator(struct cfiscsi_session *cs)
{
	int error;

	if (cs->cs_ctl_initid == -1)
		return;

	error = ctl_remove_initiator(&cs->cs_target->ct_port, cs->cs_ctl_initid);
	if (error != 0) {
		CFISCSI_SESSION_WARN(cs, "ctl_remove_initiator failed with error %d",
		    error);
	}
	cs->cs_ctl_initid = -1;
}

static struct cfiscsi_session *
cfiscsi_session_new(struct cfiscsi_softc *softc, const char *offload)
{
	struct cfiscsi_session *cs;
	int error;

	cs = malloc(sizeof(*cs), M_CFISCSI, M_NOWAIT | M_ZERO);
	if (cs == NULL) {
		CFISCSI_WARN("malloc failed");
		return (NULL);
	}
	cs->cs_ctl_initid = -1;

	refcount_init(&cs->cs_outstanding_ctl_pdus, 0);
	TAILQ_INIT(&cs->cs_waiting_for_data_out);
	mtx_init(&cs->cs_lock, "cfiscsi_lock", NULL, MTX_DEF);
	cv_init(&cs->cs_maintenance_cv, "cfiscsi_mt");
#ifdef ICL_KERNEL_PROXY
	cv_init(&cs->cs_login_cv, "cfiscsi_login");
#endif

	/*
	 * The purpose of this is to avoid racing with session shutdown.
	 * Otherwise we could have the maintenance thread call icl_conn_close()
	 * before we call icl_conn_handoff().
	 */
	cs->cs_handoff_in_progress = true;

	cs->cs_conn = icl_new_conn(offload, false, "cfiscsi", &cs->cs_lock);
	if (cs->cs_conn == NULL) {
		free(cs, M_CFISCSI);
		return (NULL);
	}
	cs->cs_conn->ic_receive = cfiscsi_receive_callback;
	cs->cs_conn->ic_error = cfiscsi_error_callback;
	cs->cs_conn->ic_prv0 = cs;

	error = kthread_add(cfiscsi_maintenance_thread, cs, NULL, NULL, 0, 0, "cfiscsimt");
	if (error != 0) {
		CFISCSI_SESSION_WARN(cs, "kthread_add(9) failed with error %d", error);
		free(cs, M_CFISCSI);
		return (NULL);
	}

	mtx_lock(&softc->lock);
	cs->cs_id = ++softc->last_session_id;
	TAILQ_INSERT_TAIL(&softc->sessions, cs, cs_next);
	mtx_unlock(&softc->lock);

	/*
	 * Start pinging the initiator.
	 */
	callout_init(&cs->cs_callout, 1);
	callout_reset(&cs->cs_callout, 1 * hz, cfiscsi_callout, cs);

	return (cs);
}

static void
cfiscsi_session_delete(struct cfiscsi_session *cs)
{
	struct cfiscsi_softc *softc;

	softc = &cfiscsi_softc;

	KASSERT(cs->cs_outstanding_ctl_pdus == 0,
	    ("destroying session with outstanding CTL pdus"));
	KASSERT(TAILQ_EMPTY(&cs->cs_waiting_for_data_out),
	    ("destroying session with non-empty queue"));

	mtx_lock(&softc->lock);
	TAILQ_REMOVE(&softc->sessions, cs, cs_next);
	mtx_unlock(&softc->lock);

	cfiscsi_session_unregister_initiator(cs);
	if (cs->cs_target != NULL)
		cfiscsi_target_release(cs->cs_target);
	icl_conn_close(cs->cs_conn);
	icl_conn_free(cs->cs_conn);
	free(cs, M_CFISCSI);
	cv_signal(&softc->sessions_cv);
}

static int
cfiscsi_init(void)
{
	struct cfiscsi_softc *softc;

	softc = &cfiscsi_softc;
	bzero(softc, sizeof(*softc));
	mtx_init(&softc->lock, "cfiscsi", NULL, MTX_DEF);

	cv_init(&softc->sessions_cv, "cfiscsi_sessions");
#ifdef ICL_KERNEL_PROXY
	cv_init(&softc->accept_cv, "cfiscsi_accept");
#endif
	TAILQ_INIT(&softc->sessions);
	TAILQ_INIT(&softc->targets);

	cfiscsi_data_wait_zone = uma_zcreate("cfiscsi_data_wait",
	    sizeof(struct cfiscsi_data_wait), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);

	return (0);
}

static int
cfiscsi_shutdown(void)
{
	struct cfiscsi_softc *softc = &cfiscsi_softc;

	if (!TAILQ_EMPTY(&softc->sessions) || !TAILQ_EMPTY(&softc->targets))
		return (EBUSY);

	uma_zdestroy(cfiscsi_data_wait_zone);
#ifdef ICL_KERNEL_PROXY
	cv_destroy(&softc->accept_cv);
#endif
	cv_destroy(&softc->sessions_cv);
	mtx_destroy(&softc->lock);
	return (0);
}

#ifdef ICL_KERNEL_PROXY
static void
cfiscsi_accept(struct socket *so, struct sockaddr *sa, int portal_id)
{
	struct cfiscsi_session *cs;

	cs = cfiscsi_session_new(&cfiscsi_softc, NULL);
	if (cs == NULL) {
		CFISCSI_WARN("failed to create session");
		return;
	}

	icl_conn_handoff_sock(cs->cs_conn, so);
	cs->cs_initiator_sa = sa;
	cs->cs_portal_id = portal_id;
	cs->cs_handoff_in_progress = false;
	cs->cs_waiting_for_ctld = true;
	cv_signal(&cfiscsi_softc.accept_cv);

	CFISCSI_SESSION_LOCK(cs);
	/*
	 * Wake up the maintenance thread if we got scheduled for termination
	 * somewhere between cfiscsi_session_new() and icl_conn_handoff_sock().
	 */
	if (cs->cs_terminating)
		cfiscsi_session_terminate(cs);
	CFISCSI_SESSION_UNLOCK(cs);
}
#endif

static void
cfiscsi_online(void *arg)
{
	struct cfiscsi_softc *softc;
	struct cfiscsi_target *ct;
	int online;

	ct = (struct cfiscsi_target *)arg;
	softc = ct->ct_softc;

	mtx_lock(&softc->lock);
	if (ct->ct_online) {
		mtx_unlock(&softc->lock);
		return;
	}
	ct->ct_online = 1;
	online = softc->online++;
	mtx_unlock(&softc->lock);
	if (online > 0)
		return;

#ifdef ICL_KERNEL_PROXY
	if (softc->listener != NULL)
		icl_listen_free(softc->listener);
	softc->listener = icl_listen_new(cfiscsi_accept);
#endif
}

static void
cfiscsi_offline(void *arg)
{
	struct cfiscsi_softc *softc;
	struct cfiscsi_target *ct;
	struct cfiscsi_session *cs;
	int error, online;

	ct = (struct cfiscsi_target *)arg;
	softc = ct->ct_softc;

	mtx_lock(&softc->lock);
	if (!ct->ct_online) {
		mtx_unlock(&softc->lock);
		return;
	}
	ct->ct_online = 0;
	online = --softc->online;

	do {
		TAILQ_FOREACH(cs, &softc->sessions, cs_next) {
			if (cs->cs_target == ct)
				cfiscsi_session_terminate(cs);
		}
		TAILQ_FOREACH(cs, &softc->sessions, cs_next) {
			if (cs->cs_target == ct)
				break;
		}
		if (cs != NULL) {
			error = cv_wait_sig(&softc->sessions_cv, &softc->lock);
			if (error != 0) {
				CFISCSI_SESSION_DEBUG(cs,
				    "cv_wait failed with error %d\n", error);
				break;
			}
		}
	} while (cs != NULL && ct->ct_online == 0);
	mtx_unlock(&softc->lock);
	if (online > 0)
		return;

#ifdef ICL_KERNEL_PROXY
	icl_listen_free(softc->listener);
	softc->listener = NULL;
#endif
}

static int
cfiscsi_info(void *arg, struct sbuf *sb)
{
	struct cfiscsi_target *ct = (struct cfiscsi_target *)arg;
	int retval;

	retval = sbuf_printf(sb, "\t<cfiscsi_state>%d</cfiscsi_state>\n",
	    ct->ct_state);
	return (retval);
}

static void
cfiscsi_ioctl_handoff(struct ctl_iscsi *ci)
{
	struct cfiscsi_softc *softc;
	struct cfiscsi_session *cs, *cs2;
	struct cfiscsi_target *ct;
	struct ctl_iscsi_handoff_params *cihp;
	int error;

	cihp = (struct ctl_iscsi_handoff_params *)&(ci->data);
	softc = &cfiscsi_softc;

	CFISCSI_DEBUG("new connection from %s (%s) to %s",
	    cihp->initiator_name, cihp->initiator_addr,
	    cihp->target_name);

	ct = cfiscsi_target_find(softc, cihp->target_name,
	    cihp->portal_group_tag);
	if (ct == NULL) {
		ci->status = CTL_ISCSI_ERROR;
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "%s: target not found", __func__);
		return;
	}

#ifdef ICL_KERNEL_PROXY
	if (cihp->socket > 0 && cihp->connection_id > 0) {
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "both socket and connection_id set");
		ci->status = CTL_ISCSI_ERROR;
		cfiscsi_target_release(ct);
		return;
	}
	if (cihp->socket == 0) {
		mtx_lock(&cfiscsi_softc.lock);
		TAILQ_FOREACH(cs, &cfiscsi_softc.sessions, cs_next) {
			if (cs->cs_id == cihp->connection_id)
				break;
		}
		if (cs == NULL) {
			mtx_unlock(&cfiscsi_softc.lock);
			snprintf(ci->error_str, sizeof(ci->error_str),
			    "connection not found");
			ci->status = CTL_ISCSI_ERROR;
			cfiscsi_target_release(ct);
			return;
		}
		mtx_unlock(&cfiscsi_softc.lock);
	} else {
#endif
		cs = cfiscsi_session_new(softc, cihp->offload);
		if (cs == NULL) {
			ci->status = CTL_ISCSI_ERROR;
			snprintf(ci->error_str, sizeof(ci->error_str),
			    "%s: cfiscsi_session_new failed", __func__);
			cfiscsi_target_release(ct);
			return;
		}
#ifdef ICL_KERNEL_PROXY
	}
#endif

	/*
	 * First PDU of Full Feature phase has the same CmdSN as the last
	 * PDU from the Login Phase received from the initiator.  Thus,
	 * the -1 below.
	 */
	cs->cs_cmdsn = cihp->cmdsn;
	cs->cs_statsn = cihp->statsn;
	cs->cs_max_recv_data_segment_length = cihp->max_recv_data_segment_length;
	cs->cs_max_send_data_segment_length = cihp->max_send_data_segment_length;
	cs->cs_max_burst_length = cihp->max_burst_length;
	cs->cs_first_burst_length = cihp->first_burst_length;
	cs->cs_immediate_data = !!cihp->immediate_data;
	if (cihp->header_digest == CTL_ISCSI_DIGEST_CRC32C)
		cs->cs_conn->ic_header_crc32c = true;
	if (cihp->data_digest == CTL_ISCSI_DIGEST_CRC32C)
		cs->cs_conn->ic_data_crc32c = true;

	strlcpy(cs->cs_initiator_name,
	    cihp->initiator_name, sizeof(cs->cs_initiator_name));
	strlcpy(cs->cs_initiator_addr,
	    cihp->initiator_addr, sizeof(cs->cs_initiator_addr));
	strlcpy(cs->cs_initiator_alias,
	    cihp->initiator_alias, sizeof(cs->cs_initiator_alias));
	memcpy(cs->cs_initiator_isid,
	    cihp->initiator_isid, sizeof(cs->cs_initiator_isid));
	snprintf(cs->cs_initiator_id, sizeof(cs->cs_initiator_id),
	    "%s,i,0x%02x%02x%02x%02x%02x%02x", cs->cs_initiator_name,
	    cihp->initiator_isid[0], cihp->initiator_isid[1],
	    cihp->initiator_isid[2], cihp->initiator_isid[3],
	    cihp->initiator_isid[4], cihp->initiator_isid[5]);

	mtx_lock(&softc->lock);
	if (ct->ct_online == 0) {
		mtx_unlock(&softc->lock);
		cs->cs_handoff_in_progress = false;
		cfiscsi_session_terminate(cs);
		cfiscsi_target_release(ct);
		ci->status = CTL_ISCSI_ERROR;
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "%s: port offline", __func__);
		return;
	}
	cs->cs_target = ct;
	mtx_unlock(&softc->lock);

restart:
	if (!cs->cs_terminating) {
		mtx_lock(&softc->lock);
		TAILQ_FOREACH(cs2, &softc->sessions, cs_next) {
			if (cs2 != cs && cs2->cs_tasks_aborted == false &&
			    cs->cs_target == cs2->cs_target &&
			    strcmp(cs->cs_initiator_id, cs2->cs_initiator_id) == 0) {
				if (strcmp(cs->cs_initiator_addr,
				    cs2->cs_initiator_addr) != 0) {
					CFISCSI_SESSION_WARN(cs2,
					    "session reinstatement from "
					    "different address %s",
					    cs->cs_initiator_addr);
				} else {
					CFISCSI_SESSION_DEBUG(cs2,
					    "session reinstatement");
				}
				cfiscsi_session_terminate(cs2);
				mtx_unlock(&softc->lock);
				pause("cfiscsi_reinstate", 1);
				goto restart;
			}
		}
		mtx_unlock(&softc->lock);
	}

	/*
	 * Register initiator with CTL.
	 */
	cfiscsi_session_register_initiator(cs);

#ifdef ICL_KERNEL_PROXY
	if (cihp->socket > 0) {
#endif
		error = icl_conn_handoff(cs->cs_conn, cihp->socket);
		if (error != 0) {
			cs->cs_handoff_in_progress = false;
			cfiscsi_session_terminate(cs);
			ci->status = CTL_ISCSI_ERROR;
			snprintf(ci->error_str, sizeof(ci->error_str),
			    "%s: icl_conn_handoff failed with error %d",
			    __func__, error);
			return;
		}
#ifdef ICL_KERNEL_PROXY
	}
#endif

#ifdef ICL_KERNEL_PROXY
	cs->cs_login_phase = false;

	/*
	 * First PDU of the Full Feature phase has likely already arrived.
	 * We have to pick it up and execute properly.
	 */
	if (cs->cs_login_pdu != NULL) {
		CFISCSI_SESSION_DEBUG(cs, "picking up first PDU");
		cfiscsi_pdu_handle(cs->cs_login_pdu);
		cs->cs_login_pdu = NULL;
	}
#endif

	CFISCSI_SESSION_LOCK(cs);
	cs->cs_handoff_in_progress = false;

	/*
	 * Wake up the maintenance thread if we got scheduled for termination.
	 */
	if (cs->cs_terminating)
		cfiscsi_session_terminate(cs);
	CFISCSI_SESSION_UNLOCK(cs);

	ci->status = CTL_ISCSI_OK;
}

static void
cfiscsi_ioctl_list(struct ctl_iscsi *ci)
{
	struct ctl_iscsi_list_params *cilp;
	struct cfiscsi_session *cs;
	struct cfiscsi_softc *softc;
	struct sbuf *sb;
	int error;

	cilp = (struct ctl_iscsi_list_params *)&(ci->data);
	softc = &cfiscsi_softc;

	sb = sbuf_new(NULL, NULL, cilp->alloc_len, SBUF_FIXEDLEN);
	if (sb == NULL) {
		ci->status = CTL_ISCSI_ERROR;
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "Unable to allocate %d bytes for iSCSI session list",
		    cilp->alloc_len);
		return;
	}

	sbuf_printf(sb, "<ctlislist>\n");
	mtx_lock(&softc->lock);
	TAILQ_FOREACH(cs, &softc->sessions, cs_next) {
#ifdef ICL_KERNEL_PROXY
		if (cs->cs_target == NULL)
			continue;
#endif
		error = sbuf_printf(sb, "<connection id=\"%d\">"
		    "<initiator>%s</initiator>"
		    "<initiator_addr>%s</initiator_addr>"
		    "<initiator_alias>%s</initiator_alias>"
		    "<target>%s</target>"
		    "<target_alias>%s</target_alias>"
		    "<target_portal_group_tag>%u</target_portal_group_tag>"
		    "<header_digest>%s</header_digest>"
		    "<data_digest>%s</data_digest>"
		    "<max_recv_data_segment_length>%d</max_recv_data_segment_length>"
		    "<max_send_data_segment_length>%d</max_send_data_segment_length>"
		    "<max_burst_length>%d</max_burst_length>"
		    "<first_burst_length>%d</first_burst_length>"
		    "<immediate_data>%d</immediate_data>"
		    "<iser>%d</iser>"
		    "<offload>%s</offload>"
		    "</connection>\n",
		    cs->cs_id,
		    cs->cs_initiator_name, cs->cs_initiator_addr, cs->cs_initiator_alias,
		    cs->cs_target->ct_name, cs->cs_target->ct_alias,
		    cs->cs_target->ct_tag,
		    cs->cs_conn->ic_header_crc32c ? "CRC32C" : "None",
		    cs->cs_conn->ic_data_crc32c ? "CRC32C" : "None",
		    cs->cs_max_recv_data_segment_length,
		    cs->cs_max_send_data_segment_length,
		    cs->cs_max_burst_length,
		    cs->cs_first_burst_length,
		    cs->cs_immediate_data,
		    cs->cs_conn->ic_iser,
		    cs->cs_conn->ic_offload);
		if (error != 0)
			break;
	}
	mtx_unlock(&softc->lock);
	error = sbuf_printf(sb, "</ctlislist>\n");
	if (error != 0) {
		sbuf_delete(sb);
		ci->status = CTL_ISCSI_LIST_NEED_MORE_SPACE;
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "Out of space, %d bytes is too small", cilp->alloc_len);
		return;
	}
	sbuf_finish(sb);

	error = copyout(sbuf_data(sb), cilp->conn_xml, sbuf_len(sb) + 1);
	if (error != 0) {
		sbuf_delete(sb);
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "copyout failed with error %d", error);
		ci->status = CTL_ISCSI_ERROR;
		return;
	}
	cilp->fill_len = sbuf_len(sb) + 1;
	ci->status = CTL_ISCSI_OK;
	sbuf_delete(sb);
}

static void
cfiscsi_ioctl_logout(struct ctl_iscsi *ci)
{
	struct icl_pdu *response;
	struct iscsi_bhs_asynchronous_message *bhsam;
	struct ctl_iscsi_logout_params *cilp;
	struct cfiscsi_session *cs;
	struct cfiscsi_softc *softc;
	int found = 0;

	cilp = (struct ctl_iscsi_logout_params *)&(ci->data);
	softc = &cfiscsi_softc;

	mtx_lock(&softc->lock);
	TAILQ_FOREACH(cs, &softc->sessions, cs_next) {
		if (cilp->all == 0 && cs->cs_id != cilp->connection_id &&
		    strcmp(cs->cs_initiator_name, cilp->initiator_name) != 0 &&
		    strcmp(cs->cs_initiator_addr, cilp->initiator_addr) != 0)
			continue;

		response = icl_pdu_new(cs->cs_conn, M_NOWAIT);
		if (response == NULL) {
			ci->status = CTL_ISCSI_ERROR;
			snprintf(ci->error_str, sizeof(ci->error_str),
			    "Unable to allocate memory");
			mtx_unlock(&softc->lock);
			return;
		}
		bhsam =
		    (struct iscsi_bhs_asynchronous_message *)response->ip_bhs;
		bhsam->bhsam_opcode = ISCSI_BHS_OPCODE_ASYNC_MESSAGE;
		bhsam->bhsam_flags = 0x80;
		bhsam->bhsam_async_event = BHSAM_EVENT_TARGET_REQUESTS_LOGOUT;
		bhsam->bhsam_parameter3 = htons(10);
		cfiscsi_pdu_queue(response);
		found++;
	}
	mtx_unlock(&softc->lock);

	if (found == 0) {
		ci->status = CTL_ISCSI_SESSION_NOT_FOUND;
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "No matching connections found");
		return;
	}

	ci->status = CTL_ISCSI_OK;
}

static void
cfiscsi_ioctl_terminate(struct ctl_iscsi *ci)
{
	struct icl_pdu *response;
	struct iscsi_bhs_asynchronous_message *bhsam;
	struct ctl_iscsi_terminate_params *citp;
	struct cfiscsi_session *cs;
	struct cfiscsi_softc *softc;
	int found = 0;

	citp = (struct ctl_iscsi_terminate_params *)&(ci->data);
	softc = &cfiscsi_softc;

	mtx_lock(&softc->lock);
	TAILQ_FOREACH(cs, &softc->sessions, cs_next) {
		if (citp->all == 0 && cs->cs_id != citp->connection_id &&
		    strcmp(cs->cs_initiator_name, citp->initiator_name) != 0 &&
		    strcmp(cs->cs_initiator_addr, citp->initiator_addr) != 0)
			continue;

		response = icl_pdu_new(cs->cs_conn, M_NOWAIT);
		if (response == NULL) {
			/*
			 * Oh well.  Just terminate the connection.
			 */
		} else {
			bhsam = (struct iscsi_bhs_asynchronous_message *)
			    response->ip_bhs;
			bhsam->bhsam_opcode = ISCSI_BHS_OPCODE_ASYNC_MESSAGE;
			bhsam->bhsam_flags = 0x80;
			bhsam->bhsam_0xffffffff = 0xffffffff;
			bhsam->bhsam_async_event =
			    BHSAM_EVENT_TARGET_TERMINATES_SESSION;
			cfiscsi_pdu_queue(response);
		}
		cfiscsi_session_terminate(cs);
		found++;
	}
	mtx_unlock(&softc->lock);

	if (found == 0) {
		ci->status = CTL_ISCSI_SESSION_NOT_FOUND;
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "No matching connections found");
		return;
	}

	ci->status = CTL_ISCSI_OK;
}

static void
cfiscsi_ioctl_limits(struct ctl_iscsi *ci)
{
	struct ctl_iscsi_limits_params *cilp;
	struct icl_drv_limits idl;
	int error;

	cilp = (struct ctl_iscsi_limits_params *)&(ci->data);

	error = icl_limits(cilp->offload, false, &idl);
	if (error != 0) {
		ci->status = CTL_ISCSI_ERROR;
		snprintf(ci->error_str, sizeof(ci->error_str),
			"%s: icl_limits failed with error %d",
			__func__, error);
		return;
	}

	cilp->max_recv_data_segment_length =
	    idl.idl_max_recv_data_segment_length;
	cilp->max_send_data_segment_length =
	    idl.idl_max_send_data_segment_length;
	cilp->max_burst_length = idl.idl_max_burst_length;
	cilp->first_burst_length = idl.idl_first_burst_length;

	ci->status = CTL_ISCSI_OK;
}

#ifdef ICL_KERNEL_PROXY
static void
cfiscsi_ioctl_listen(struct ctl_iscsi *ci)
{
	struct ctl_iscsi_listen_params *cilp;
	struct sockaddr *sa;
	int error;

	cilp = (struct ctl_iscsi_listen_params *)&(ci->data);

	if (cfiscsi_softc.listener == NULL) {
		CFISCSI_DEBUG("no listener");
		snprintf(ci->error_str, sizeof(ci->error_str), "no listener");
		ci->status = CTL_ISCSI_ERROR;
		return;
	}

	error = getsockaddr(&sa, (void *)cilp->addr, cilp->addrlen);
	if (error != 0) {
		CFISCSI_DEBUG("getsockaddr, error %d", error);
		snprintf(ci->error_str, sizeof(ci->error_str), "getsockaddr failed");
		ci->status = CTL_ISCSI_ERROR;
		return;
	}

	error = icl_listen_add(cfiscsi_softc.listener, cilp->iser, cilp->domain,
	    cilp->socktype, cilp->protocol, sa, cilp->portal_id);
	if (error != 0) {
		free(sa, M_SONAME);
		CFISCSI_DEBUG("icl_listen_add, error %d", error);
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "icl_listen_add failed, error %d", error);
		ci->status = CTL_ISCSI_ERROR;
		return;
	}

	ci->status = CTL_ISCSI_OK;
}

static void
cfiscsi_ioctl_accept(struct ctl_iscsi *ci)
{
	struct ctl_iscsi_accept_params *ciap;
	struct cfiscsi_session *cs;
	int error;

	ciap = (struct ctl_iscsi_accept_params *)&(ci->data);

	mtx_lock(&cfiscsi_softc.lock);
	for (;;) {
		TAILQ_FOREACH(cs, &cfiscsi_softc.sessions, cs_next) {
			if (cs->cs_waiting_for_ctld)
				break;
		}
		if (cs != NULL)
			break;
		error = cv_wait_sig(&cfiscsi_softc.accept_cv, &cfiscsi_softc.lock);
		if (error != 0) {
			mtx_unlock(&cfiscsi_softc.lock);
			snprintf(ci->error_str, sizeof(ci->error_str), "interrupted");
			ci->status = CTL_ISCSI_ERROR;
			return;
		}
	}
	mtx_unlock(&cfiscsi_softc.lock);

	cs->cs_waiting_for_ctld = false;
	cs->cs_login_phase = true;

	ciap->connection_id = cs->cs_id;
	ciap->portal_id = cs->cs_portal_id;
	ciap->initiator_addrlen = cs->cs_initiator_sa->sa_len;
	error = copyout(cs->cs_initiator_sa, ciap->initiator_addr,
	    cs->cs_initiator_sa->sa_len);
	if (error != 0) {
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "copyout failed with error %d", error);
		ci->status = CTL_ISCSI_ERROR;
		return;
	}

	ci->status = CTL_ISCSI_OK;
}

static void
cfiscsi_ioctl_send(struct ctl_iscsi *ci)
{
	struct ctl_iscsi_send_params *cisp;
	struct cfiscsi_session *cs;
	struct icl_pdu *ip;
	size_t datalen;
	void *data;
	int error;

	cisp = (struct ctl_iscsi_send_params *)&(ci->data);

	mtx_lock(&cfiscsi_softc.lock);
	TAILQ_FOREACH(cs, &cfiscsi_softc.sessions, cs_next) {
		if (cs->cs_id == cisp->connection_id)
			break;
	}
	if (cs == NULL) {
		mtx_unlock(&cfiscsi_softc.lock);
		snprintf(ci->error_str, sizeof(ci->error_str), "connection not found");
		ci->status = CTL_ISCSI_ERROR;
		return;
	}
	mtx_unlock(&cfiscsi_softc.lock);

#if 0
	if (cs->cs_login_phase == false)
		return (EBUSY);
#endif

	if (cs->cs_terminating) {
		snprintf(ci->error_str, sizeof(ci->error_str), "connection is terminating");
		ci->status = CTL_ISCSI_ERROR;
		return;
	}

	datalen = cisp->data_segment_len;
	/*
	 * XXX
	 */
	//if (datalen > CFISCSI_MAX_DATA_SEGMENT_LENGTH) {
	if (datalen > 65535) {
		snprintf(ci->error_str, sizeof(ci->error_str), "data segment too big");
		ci->status = CTL_ISCSI_ERROR;
		return;
	}
	if (datalen > 0) {
		data = malloc(datalen, M_CFISCSI, M_WAITOK);
		error = copyin(cisp->data_segment, data, datalen);
		if (error != 0) {
			free(data, M_CFISCSI);
			snprintf(ci->error_str, sizeof(ci->error_str), "copyin error %d", error);
			ci->status = CTL_ISCSI_ERROR;
			return;
		}
	}

	ip = icl_pdu_new(cs->cs_conn, M_WAITOK);
	memcpy(ip->ip_bhs, cisp->bhs, sizeof(*ip->ip_bhs));
	if (datalen > 0) {
		icl_pdu_append_data(ip, data, datalen, M_WAITOK);
		free(data, M_CFISCSI);
	}
	CFISCSI_SESSION_LOCK(cs);
	icl_pdu_queue(ip);
	CFISCSI_SESSION_UNLOCK(cs);
	ci->status = CTL_ISCSI_OK;
}

static void
cfiscsi_ioctl_receive(struct ctl_iscsi *ci)
{
	struct ctl_iscsi_receive_params *cirp;
	struct cfiscsi_session *cs;
	struct icl_pdu *ip;
	void *data;
	int error;

	cirp = (struct ctl_iscsi_receive_params *)&(ci->data);

	mtx_lock(&cfiscsi_softc.lock);
	TAILQ_FOREACH(cs, &cfiscsi_softc.sessions, cs_next) {
		if (cs->cs_id == cirp->connection_id)
			break;
	}
	if (cs == NULL) {
		mtx_unlock(&cfiscsi_softc.lock);
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "connection not found");
		ci->status = CTL_ISCSI_ERROR;
		return;
	}
	mtx_unlock(&cfiscsi_softc.lock);

#if 0
	if (is->is_login_phase == false)
		return (EBUSY);
#endif

	CFISCSI_SESSION_LOCK(cs);
	while (cs->cs_login_pdu == NULL && cs->cs_terminating == false) {
		error = cv_wait_sig(&cs->cs_login_cv, &cs->cs_lock);
		if (error != 0) {
			CFISCSI_SESSION_UNLOCK(cs);
			snprintf(ci->error_str, sizeof(ci->error_str),
			    "interrupted by signal");
			ci->status = CTL_ISCSI_ERROR;
			return;
		}
	}

	if (cs->cs_terminating) {
		CFISCSI_SESSION_UNLOCK(cs);
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "connection terminating");
		ci->status = CTL_ISCSI_ERROR;
		return;
	}
	ip = cs->cs_login_pdu;
	cs->cs_login_pdu = NULL;
	CFISCSI_SESSION_UNLOCK(cs);

	if (ip->ip_data_len > cirp->data_segment_len) {
		icl_pdu_free(ip);
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "data segment too big");
		ci->status = CTL_ISCSI_ERROR;
		return;
	}

	copyout(ip->ip_bhs, cirp->bhs, sizeof(*ip->ip_bhs));
	if (ip->ip_data_len > 0) {
		data = malloc(ip->ip_data_len, M_CFISCSI, M_WAITOK);
		icl_pdu_get_data(ip, 0, data, ip->ip_data_len);
		copyout(data, cirp->data_segment, ip->ip_data_len);
		free(data, M_CFISCSI);
	}

	icl_pdu_free(ip);
	ci->status = CTL_ISCSI_OK;
}

#endif /* !ICL_KERNEL_PROXY */

static void
cfiscsi_ioctl_port_create(struct ctl_req *req)
{
	struct cfiscsi_target *ct;
	struct ctl_port *port;
	const char *target, *alias, *val;
	struct scsi_vpd_id_descriptor *desc;
	int retval, len, idlen;
	uint16_t tag;

	target = dnvlist_get_string(req->args_nvl, "cfiscsi_target", NULL);
	alias = dnvlist_get_string(req->args_nvl, "cfiscsi_target_alias", NULL);
	val = dnvlist_get_string(req->args_nvl, "cfiscsi_portal_group_tag",
	    NULL);


	if (target == NULL || val == NULL) {
		req->status = CTL_LUN_ERROR;
		snprintf(req->error_str, sizeof(req->error_str),
		    "Missing required argument");
		return;
	}

	tag = strtoul(val, NULL, 0);
	ct = cfiscsi_target_find_or_create(&cfiscsi_softc, target, alias, tag);
	if (ct == NULL) {
		req->status = CTL_LUN_ERROR;
		snprintf(req->error_str, sizeof(req->error_str),
		    "failed to create target \"%s\"", target);
		return;
	}
	if (ct->ct_state == CFISCSI_TARGET_STATE_ACTIVE) {
		req->status = CTL_LUN_ERROR;
		snprintf(req->error_str, sizeof(req->error_str),
		    "target \"%s\" for portal group tag %u already exists",
		    target, tag);
		cfiscsi_target_release(ct);
		return;
	}
	port = &ct->ct_port;
	// WAT
	if (ct->ct_state == CFISCSI_TARGET_STATE_DYING)
		goto done;

	port->frontend = &cfiscsi_frontend;
	port->port_type = CTL_PORT_ISCSI;
	/* XXX KDM what should the real number be here? */
	port->num_requested_ctl_io = 4096;
	port->port_name = "iscsi";
	port->physical_port = (int)tag;
	port->virtual_port = ct->ct_target_id;
	port->port_online = cfiscsi_online;
	port->port_offline = cfiscsi_offline;
	port->port_info = cfiscsi_info;
	port->onoff_arg = ct;
	port->fe_datamove = cfiscsi_datamove;
	port->fe_done = cfiscsi_done;
	port->targ_port = -1;
	port->options = nvlist_clone(req->args_nvl);

	/* Generate Port ID. */
	idlen = strlen(target) + strlen(",t,0x0001") + 1;
	idlen = roundup2(idlen, 4);
	len = sizeof(struct scsi_vpd_device_id) + idlen;
	port->port_devid = malloc(sizeof(struct ctl_devid) + len,
	    M_CTL, M_WAITOK | M_ZERO);
	port->port_devid->len = len;
	desc = (struct scsi_vpd_id_descriptor *)port->port_devid->data;
	desc->proto_codeset = (SCSI_PROTO_ISCSI << 4) | SVPD_ID_CODESET_UTF8;
	desc->id_type = SVPD_ID_PIV | SVPD_ID_ASSOC_PORT |
	    SVPD_ID_TYPE_SCSI_NAME;
	desc->length = idlen;
	snprintf(desc->identifier, idlen, "%s,t,0x%4.4x", target, tag);

	/* Generate Target ID. */
	idlen = strlen(target) + 1;
	idlen = roundup2(idlen, 4);
	len = sizeof(struct scsi_vpd_device_id) + idlen;
	port->target_devid = malloc(sizeof(struct ctl_devid) + len,
	    M_CTL, M_WAITOK | M_ZERO);
	port->target_devid->len = len;
	desc = (struct scsi_vpd_id_descriptor *)port->target_devid->data;
	desc->proto_codeset = (SCSI_PROTO_ISCSI << 4) | SVPD_ID_CODESET_UTF8;
	desc->id_type = SVPD_ID_PIV | SVPD_ID_ASSOC_TARGET |
	    SVPD_ID_TYPE_SCSI_NAME;
	desc->length = idlen;
	strlcpy(desc->identifier, target, idlen);

	retval = ctl_port_register(port);
	if (retval != 0) {
		free(port->port_devid, M_CFISCSI);
		free(port->target_devid, M_CFISCSI);
		cfiscsi_target_release(ct);
		req->status = CTL_LUN_ERROR;
		snprintf(req->error_str, sizeof(req->error_str),
		    "ctl_port_register() failed with error %d", retval);
		return;
	}
done:
	ct->ct_state = CFISCSI_TARGET_STATE_ACTIVE;
	req->status = CTL_LUN_OK;
	req->result_nvl = nvlist_create(0);
	nvlist_add_number(req->result_nvl, "port_id", port->targ_port);
}

static void
cfiscsi_ioctl_port_remove(struct ctl_req *req)
{
	struct cfiscsi_target *ct;
	const char *target, *val;
	uint16_t tag;

	target = dnvlist_get_string(req->args_nvl, "cfiscsi_target", NULL);
	val = dnvlist_get_string(req->args_nvl, "cfiscsi_portal_group_tag",
	    NULL);

	if (target == NULL || val == NULL) {
		req->status = CTL_LUN_ERROR;
		snprintf(req->error_str, sizeof(req->error_str),
		    "Missing required argument");
		return;
	}

	tag = strtoul(val, NULL, 0);
	ct = cfiscsi_target_find(&cfiscsi_softc, target, tag);
	if (ct == NULL) {
		req->status = CTL_LUN_ERROR;
		snprintf(req->error_str, sizeof(req->error_str),
		    "can't find target \"%s\"", target);
		return;
	}

	ct->ct_state = CFISCSI_TARGET_STATE_DYING;
	ctl_port_offline(&ct->ct_port);
	cfiscsi_target_release(ct);
	cfiscsi_target_release(ct);
	req->status = CTL_LUN_OK;
}

static int
cfiscsi_ioctl(struct cdev *dev,
    u_long cmd, caddr_t addr, int flag, struct thread *td)
{
	struct ctl_iscsi *ci;
	struct ctl_req *req;

	if (cmd == CTL_PORT_REQ) {
		req = (struct ctl_req *)addr;
		switch (req->reqtype) {
		case CTL_REQ_CREATE:
			cfiscsi_ioctl_port_create(req);
			break;
		case CTL_REQ_REMOVE:
			cfiscsi_ioctl_port_remove(req);
			break;
		default:
			req->status = CTL_LUN_ERROR;
			snprintf(req->error_str, sizeof(req->error_str),
			    "Unsupported request type %d", req->reqtype);
		}
		return (0);
	}

	if (cmd != CTL_ISCSI)
		return (ENOTTY);

	ci = (struct ctl_iscsi *)addr;
	switch (ci->type) {
	case CTL_ISCSI_HANDOFF:
		cfiscsi_ioctl_handoff(ci);
		break;
	case CTL_ISCSI_LIST:
		cfiscsi_ioctl_list(ci);
		break;
	case CTL_ISCSI_LOGOUT:
		cfiscsi_ioctl_logout(ci);
		break;
	case CTL_ISCSI_TERMINATE:
		cfiscsi_ioctl_terminate(ci);
		break;
	case CTL_ISCSI_LIMITS:
		cfiscsi_ioctl_limits(ci);
		break;
#ifdef ICL_KERNEL_PROXY
	case CTL_ISCSI_LISTEN:
		cfiscsi_ioctl_listen(ci);
		break;
	case CTL_ISCSI_ACCEPT:
		cfiscsi_ioctl_accept(ci);
		break;
	case CTL_ISCSI_SEND:
		cfiscsi_ioctl_send(ci);
		break;
	case CTL_ISCSI_RECEIVE:
		cfiscsi_ioctl_receive(ci);
		break;
#else
	case CTL_ISCSI_LISTEN:
	case CTL_ISCSI_ACCEPT:
	case CTL_ISCSI_SEND:
	case CTL_ISCSI_RECEIVE:
		ci->status = CTL_ISCSI_ERROR;
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "%s: CTL compiled without ICL_KERNEL_PROXY",
		    __func__);
		break;
#endif /* !ICL_KERNEL_PROXY */
	default:
		ci->status = CTL_ISCSI_ERROR;
		snprintf(ci->error_str, sizeof(ci->error_str),
		    "%s: invalid iSCSI request type %d", __func__, ci->type);
		break;
	}

	return (0);
}

static void
cfiscsi_target_hold(struct cfiscsi_target *ct)
{

	refcount_acquire(&ct->ct_refcount);
}

static void
cfiscsi_target_release(struct cfiscsi_target *ct)
{
	struct cfiscsi_softc *softc;

	softc = ct->ct_softc;
	mtx_lock(&softc->lock);
	if (refcount_release(&ct->ct_refcount)) {
		TAILQ_REMOVE(&softc->targets, ct, ct_next);
		mtx_unlock(&softc->lock);
		if (ct->ct_state != CFISCSI_TARGET_STATE_INVALID) {
			ct->ct_state = CFISCSI_TARGET_STATE_INVALID;
			if (ctl_port_deregister(&ct->ct_port) != 0)
				printf("%s: ctl_port_deregister() failed\n",
				    __func__);
		}
		free(ct, M_CFISCSI);

		return;
	}
	mtx_unlock(&softc->lock);
}

static struct cfiscsi_target *
cfiscsi_target_find(struct cfiscsi_softc *softc, const char *name, uint16_t tag)
{
	struct cfiscsi_target *ct;

	mtx_lock(&softc->lock);
	TAILQ_FOREACH(ct, &softc->targets, ct_next) {
		if (ct->ct_tag != tag ||
		    strcmp(name, ct->ct_name) != 0 ||
		    ct->ct_state != CFISCSI_TARGET_STATE_ACTIVE)
			continue;
		cfiscsi_target_hold(ct);
		mtx_unlock(&softc->lock);
		return (ct);
	}
	mtx_unlock(&softc->lock);

	return (NULL);
}

static struct cfiscsi_target *
cfiscsi_target_find_or_create(struct cfiscsi_softc *softc, const char *name,
    const char *alias, uint16_t tag)
{
	struct cfiscsi_target *ct, *newct;

	if (name[0] == '\0' || strlen(name) >= CTL_ISCSI_NAME_LEN)
		return (NULL);

	newct = malloc(sizeof(*newct), M_CFISCSI, M_WAITOK | M_ZERO);

	mtx_lock(&softc->lock);
	TAILQ_FOREACH(ct, &softc->targets, ct_next) {
		if (ct->ct_tag != tag ||
		    strcmp(name, ct->ct_name) != 0 ||
		    ct->ct_state == CFISCSI_TARGET_STATE_INVALID)
			continue;
		cfiscsi_target_hold(ct);
		mtx_unlock(&softc->lock);
		free(newct, M_CFISCSI);
		return (ct);
	}

	strlcpy(newct->ct_name, name, sizeof(newct->ct_name));
	if (alias != NULL)
		strlcpy(newct->ct_alias, alias, sizeof(newct->ct_alias));
	newct->ct_tag = tag;
	refcount_init(&newct->ct_refcount, 1);
	newct->ct_softc = softc;
	if (TAILQ_EMPTY(&softc->targets))
		softc->last_target_id = 0;
	newct->ct_target_id = ++softc->last_target_id;
	TAILQ_INSERT_TAIL(&softc->targets, newct, ct_next);
	mtx_unlock(&softc->lock);

	return (newct);
}

static void
cfiscsi_datamove_in(union ctl_io *io)
{
	struct cfiscsi_session *cs;
	struct icl_pdu *request, *response;
	const struct iscsi_bhs_scsi_command *bhssc;
	struct iscsi_bhs_data_in *bhsdi;
	struct ctl_sg_entry ctl_sg_entry, *ctl_sglist;
	size_t len, expected_len, sg_len, buffer_offset;
	const char *sg_addr;
	int ctl_sg_count, error, i;

	request = io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;
	cs = PDU_SESSION(request);

	bhssc = (const struct iscsi_bhs_scsi_command *)request->ip_bhs;
	KASSERT((bhssc->bhssc_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) ==
	    ISCSI_BHS_OPCODE_SCSI_COMMAND,
	    ("bhssc->bhssc_opcode != ISCSI_BHS_OPCODE_SCSI_COMMAND"));

	if (io->scsiio.kern_sg_entries > 0) {
		ctl_sglist = (struct ctl_sg_entry *)io->scsiio.kern_data_ptr;
		ctl_sg_count = io->scsiio.kern_sg_entries;
	} else {
		ctl_sglist = &ctl_sg_entry;
		ctl_sglist->addr = io->scsiio.kern_data_ptr;
		ctl_sglist->len = io->scsiio.kern_data_len;
		ctl_sg_count = 1;
	}

	/*
	 * This is the total amount of data to be transferred within the current
	 * SCSI command.  We need to record it so that we can properly report
	 * underflow/underflow.
	 */
	PDU_TOTAL_TRANSFER_LEN(request) = io->scsiio.kern_total_len;

	/*
	 * This is the offset within the current SCSI command; for the first
	 * call to cfiscsi_datamove() it will be 0, and for subsequent ones
	 * it will be the sum of lengths of previous ones.
	 */
	buffer_offset = io->scsiio.kern_rel_offset;

	/*
	 * This is the transfer length expected by the initiator.  In theory,
	 * it could be different from the correct amount of data from the SCSI
	 * point of view, even if that doesn't make any sense.
	 */
	expected_len = ntohl(bhssc->bhssc_expected_data_transfer_length);
#if 0
	if (expected_len != io->scsiio.kern_total_len) {
		CFISCSI_SESSION_DEBUG(cs, "expected transfer length %zd, "
		    "actual length %zd", expected_len,
		    (size_t)io->scsiio.kern_total_len);
	}
#endif

	if (buffer_offset >= expected_len) {
#if 0
		CFISCSI_SESSION_DEBUG(cs, "buffer_offset = %zd, "
		    "already sent the expected len", buffer_offset);
#endif
		io->scsiio.be_move_done(io);
		return;
	}

	i = 0;
	sg_addr = NULL;
	sg_len = 0;
	response = NULL;
	bhsdi = NULL;
	for (;;) {
		if (response == NULL) {
			response = cfiscsi_pdu_new_response(request, M_NOWAIT);
			if (response == NULL) {
				CFISCSI_SESSION_WARN(cs, "failed to "
				    "allocate memory; dropping connection");
				ctl_set_busy(&io->scsiio);
				io->scsiio.be_move_done(io);
				cfiscsi_session_terminate(cs);
				return;
			}
			bhsdi = (struct iscsi_bhs_data_in *)response->ip_bhs;
			bhsdi->bhsdi_opcode = ISCSI_BHS_OPCODE_SCSI_DATA_IN;
			bhsdi->bhsdi_initiator_task_tag =
			    bhssc->bhssc_initiator_task_tag;
			bhsdi->bhsdi_target_transfer_tag = 0xffffffff;
			bhsdi->bhsdi_datasn = htonl(PDU_EXPDATASN(request));
			PDU_EXPDATASN(request)++;
			bhsdi->bhsdi_buffer_offset = htonl(buffer_offset);
		}

		KASSERT(i < ctl_sg_count, ("i >= ctl_sg_count"));
		if (sg_len == 0) {
			sg_addr = ctl_sglist[i].addr;
			sg_len = ctl_sglist[i].len;
			KASSERT(sg_len > 0, ("sg_len <= 0"));
		}

		len = sg_len;

		/*
		 * Truncate to maximum data segment length.
		 */
		KASSERT(response->ip_data_len < cs->cs_max_send_data_segment_length,
		    ("ip_data_len %zd >= max_send_data_segment_length %d",
		    response->ip_data_len, cs->cs_max_send_data_segment_length));
		if (response->ip_data_len + len >
		    cs->cs_max_send_data_segment_length) {
			len = cs->cs_max_send_data_segment_length -
			    response->ip_data_len;
			KASSERT(len <= sg_len, ("len %zd > sg_len %zd",
			    len, sg_len));
		}

		/*
		 * Truncate to expected data transfer length.
		 */
		KASSERT(buffer_offset + response->ip_data_len < expected_len,
		    ("buffer_offset %zd + ip_data_len %zd >= expected_len %zd",
		    buffer_offset, response->ip_data_len, expected_len));
		if (buffer_offset + response->ip_data_len + len > expected_len) {
			CFISCSI_SESSION_DEBUG(cs, "truncating from %zd "
			    "to expected data transfer length %zd",
			    buffer_offset + response->ip_data_len + len, expected_len);
			len = expected_len - (buffer_offset + response->ip_data_len);
			KASSERT(len <= sg_len, ("len %zd > sg_len %zd",
			    len, sg_len));
		}

		error = icl_pdu_append_data(response, sg_addr, len, M_NOWAIT);
		if (error != 0) {
			CFISCSI_SESSION_WARN(cs, "failed to "
			    "allocate memory; dropping connection");
			icl_pdu_free(response);
			ctl_set_busy(&io->scsiio);
			io->scsiio.be_move_done(io);
			cfiscsi_session_terminate(cs);
			return;
		}
		sg_addr += len;
		sg_len -= len;
		io->scsiio.kern_data_resid -= len;

		KASSERT(buffer_offset + response->ip_data_len <= expected_len,
		    ("buffer_offset %zd + ip_data_len %zd > expected_len %zd",
		    buffer_offset, response->ip_data_len, expected_len));
		if (buffer_offset + response->ip_data_len == expected_len) {
			/*
			 * Already have the amount of data the initiator wanted.
			 */
			break;
		}

		if (sg_len == 0) {
			/*
			 * End of scatter-gather segment;
			 * proceed to the next one...
			 */
			if (i == ctl_sg_count - 1) {
				/*
				 * ... unless this was the last one.
				 */
				break;
			}
			i++;
		}

		if (response->ip_data_len == cs->cs_max_send_data_segment_length) {
			/*
			 * Can't stuff more data into the current PDU;
			 * queue it.  Note that's not enough to check
			 * for kern_data_resid == 0 instead; there
			 * may be several Data-In PDUs for the final
			 * call to cfiscsi_datamove(), and we want
			 * to set the F flag only on the last of them.
			 */
			buffer_offset += response->ip_data_len;
			if (buffer_offset == io->scsiio.kern_total_len ||
			    buffer_offset == expected_len) {
				buffer_offset -= response->ip_data_len;
				break;
			}
			cfiscsi_pdu_queue(response);
			response = NULL;
			bhsdi = NULL;
		}
	}
	if (response != NULL) {
		buffer_offset += response->ip_data_len;
		if (buffer_offset == io->scsiio.kern_total_len ||
		    buffer_offset == expected_len) {
			bhsdi->bhsdi_flags |= BHSDI_FLAGS_F;
			if (io->io_hdr.status == CTL_SUCCESS) {
				bhsdi->bhsdi_flags |= BHSDI_FLAGS_S;
				if (PDU_TOTAL_TRANSFER_LEN(request) <
				    ntohl(bhssc->bhssc_expected_data_transfer_length)) {
					bhsdi->bhsdi_flags |= BHSSR_FLAGS_RESIDUAL_UNDERFLOW;
					bhsdi->bhsdi_residual_count =
					    htonl(ntohl(bhssc->bhssc_expected_data_transfer_length) -
					    PDU_TOTAL_TRANSFER_LEN(request));
				} else if (PDU_TOTAL_TRANSFER_LEN(request) >
				    ntohl(bhssc->bhssc_expected_data_transfer_length)) {
					bhsdi->bhsdi_flags |= BHSSR_FLAGS_RESIDUAL_OVERFLOW;
					bhsdi->bhsdi_residual_count =
					    htonl(PDU_TOTAL_TRANSFER_LEN(request) -
					    ntohl(bhssc->bhssc_expected_data_transfer_length));
				}
				bhsdi->bhsdi_status = io->scsiio.scsi_status;
				io->io_hdr.flags |= CTL_FLAG_STATUS_SENT;
			}
		}
		KASSERT(response->ip_data_len > 0, ("sending empty Data-In"));
		cfiscsi_pdu_queue(response);
	}

	io->scsiio.be_move_done(io);
}

static void
cfiscsi_datamove_out(union ctl_io *io)
{
	struct cfiscsi_session *cs;
	struct icl_pdu *request, *response;
	const struct iscsi_bhs_scsi_command *bhssc;
	struct iscsi_bhs_r2t *bhsr2t;
	struct cfiscsi_data_wait *cdw;
	struct ctl_sg_entry ctl_sg_entry, *ctl_sglist;
	uint32_t expected_len, datamove_len, r2t_off, r2t_len;
	uint32_t target_transfer_tag;
	bool done;

	request = io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;
	cs = PDU_SESSION(request);

	bhssc = (const struct iscsi_bhs_scsi_command *)request->ip_bhs;
	KASSERT((bhssc->bhssc_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) ==
	    ISCSI_BHS_OPCODE_SCSI_COMMAND,
	    ("bhssc->bhssc_opcode != ISCSI_BHS_OPCODE_SCSI_COMMAND"));

	/*
	 * We need to record it so that we can properly report
	 * underflow/underflow.
	 */
	PDU_TOTAL_TRANSFER_LEN(request) = io->scsiio.kern_total_len;

	/*
	 * Complete write underflow.  Not a single byte to read.  Return.
	 */
	expected_len = ntohl(bhssc->bhssc_expected_data_transfer_length);
	if (io->scsiio.kern_rel_offset >= expected_len) {
		io->scsiio.be_move_done(io);
		return;
	}
	datamove_len = MIN(io->scsiio.kern_data_len,
	    expected_len - io->scsiio.kern_rel_offset);

	target_transfer_tag =
	    atomic_fetchadd_32(&cs->cs_target_transfer_tag, 1);
	cdw = cfiscsi_data_wait_new(cs, io, bhssc->bhssc_initiator_task_tag,
	    &target_transfer_tag);
	if (cdw == NULL) {
		CFISCSI_SESSION_WARN(cs, "failed to "
		    "allocate memory; dropping connection");
		ctl_set_busy(&io->scsiio);
		io->scsiio.be_move_done(io);
		cfiscsi_session_terminate(cs);
		return;
	}
#if 0
	CFISCSI_SESSION_DEBUG(cs, "expecting Data-Out with initiator "
	    "task tag 0x%x, target transfer tag 0x%x",
	    bhssc->bhssc_initiator_task_tag, target_transfer_tag);
#endif

	cdw->cdw_ctl_io = io;
	cdw->cdw_target_transfer_tag = target_transfer_tag;
	cdw->cdw_initiator_task_tag = bhssc->bhssc_initiator_task_tag;
	cdw->cdw_r2t_end = datamove_len;
	cdw->cdw_datasn = 0;

	/* Set initial data pointer for the CDW respecting ext_data_filled. */
	if (io->scsiio.kern_sg_entries > 0) {
		ctl_sglist = (struct ctl_sg_entry *)io->scsiio.kern_data_ptr;
	} else {
		ctl_sglist = &ctl_sg_entry;
		ctl_sglist->addr = io->scsiio.kern_data_ptr;
		ctl_sglist->len = datamove_len;
	}
	cdw->cdw_sg_index = 0;
	cdw->cdw_sg_addr = ctl_sglist[cdw->cdw_sg_index].addr;
	cdw->cdw_sg_len = ctl_sglist[cdw->cdw_sg_index].len;
	r2t_off = io->scsiio.ext_data_filled;
	while (r2t_off > 0) {
		if (r2t_off >= cdw->cdw_sg_len) {
			r2t_off -= cdw->cdw_sg_len;
			cdw->cdw_sg_index++;
			cdw->cdw_sg_addr = ctl_sglist[cdw->cdw_sg_index].addr;
			cdw->cdw_sg_len = ctl_sglist[cdw->cdw_sg_index].len;
			continue;
		}
		cdw->cdw_sg_addr += r2t_off;
		cdw->cdw_sg_len -= r2t_off;
		r2t_off = 0;
	}

	if (cs->cs_immediate_data &&
	    io->scsiio.kern_rel_offset + io->scsiio.ext_data_filled <
	    icl_pdu_data_segment_length(request)) {
		done = cfiscsi_handle_data_segment(request, cdw);
		if (done) {
			cfiscsi_data_wait_free(cs, cdw);
			io->scsiio.be_move_done(io);
			return;
		}
	}

	r2t_off = io->scsiio.kern_rel_offset + io->scsiio.ext_data_filled;
	r2t_len = MIN(datamove_len - io->scsiio.ext_data_filled,
	    cs->cs_max_burst_length);
	cdw->cdw_r2t_end = io->scsiio.ext_data_filled + r2t_len;

	CFISCSI_SESSION_LOCK(cs);
	TAILQ_INSERT_TAIL(&cs->cs_waiting_for_data_out, cdw, cdw_next);
	CFISCSI_SESSION_UNLOCK(cs);

	/*
	 * XXX: We should limit the number of outstanding R2T PDUs
	 * 	per task to MaxOutstandingR2T.
	 */
	response = cfiscsi_pdu_new_response(request, M_NOWAIT);
	if (response == NULL) {
		CFISCSI_SESSION_WARN(cs, "failed to "
		    "allocate memory; dropping connection");
		ctl_set_busy(&io->scsiio);
		io->scsiio.be_move_done(io);
		cfiscsi_session_terminate(cs);
		return;
	}
	io->io_hdr.flags |= CTL_FLAG_DMA_INPROG;
	bhsr2t = (struct iscsi_bhs_r2t *)response->ip_bhs;
	bhsr2t->bhsr2t_opcode = ISCSI_BHS_OPCODE_R2T;
	bhsr2t->bhsr2t_flags = 0x80;
	bhsr2t->bhsr2t_lun = bhssc->bhssc_lun;
	bhsr2t->bhsr2t_initiator_task_tag = bhssc->bhssc_initiator_task_tag;
	bhsr2t->bhsr2t_target_transfer_tag = target_transfer_tag;
	/*
	 * XXX: Here we assume that cfiscsi_datamove() won't ever
	 *	be running concurrently on several CPUs for a given
	 *	command.
	 */
	bhsr2t->bhsr2t_r2tsn = htonl(PDU_R2TSN(request));
	PDU_R2TSN(request)++;
	/*
	 * This is the offset within the current SCSI command;
	 * i.e. for the first call of datamove(), it will be 0,
	 * and for subsequent ones it will be the sum of lengths
	 * of previous ones.
	 *
	 * The ext_data_filled is to account for unsolicited
	 * (immediate) data that might have already arrived.
	 */
	bhsr2t->bhsr2t_buffer_offset = htonl(r2t_off);
	/*
	 * This is the total length (sum of S/G lengths) this call
	 * to cfiscsi_datamove() is supposed to handle, limited by
	 * MaxBurstLength.
	 */
	bhsr2t->bhsr2t_desired_data_transfer_length = htonl(r2t_len);
	cfiscsi_pdu_queue(response);
}

static void
cfiscsi_datamove(union ctl_io *io)
{

	if ((io->io_hdr.flags & CTL_FLAG_DATA_MASK) == CTL_FLAG_DATA_IN)
		cfiscsi_datamove_in(io);
	else {
		/* We hadn't received anything during this datamove yet. */
		io->scsiio.ext_data_filled = 0;
		cfiscsi_datamove_out(io);
	}
}

static void
cfiscsi_scsi_command_done(union ctl_io *io)
{
	struct icl_pdu *request, *response;
	struct iscsi_bhs_scsi_command *bhssc;
	struct iscsi_bhs_scsi_response *bhssr;
#ifdef DIAGNOSTIC
	struct cfiscsi_data_wait *cdw;
#endif
	struct cfiscsi_session *cs;
	uint16_t sense_length;

	request = io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;
	cs = PDU_SESSION(request);
	bhssc = (struct iscsi_bhs_scsi_command *)request->ip_bhs;
	KASSERT((bhssc->bhssc_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) ==
	    ISCSI_BHS_OPCODE_SCSI_COMMAND,
	    ("replying to wrong opcode 0x%x", bhssc->bhssc_opcode));

	//CFISCSI_SESSION_DEBUG(cs, "initiator task tag 0x%x",
	//    bhssc->bhssc_initiator_task_tag);

#ifdef DIAGNOSTIC
	CFISCSI_SESSION_LOCK(cs);
	TAILQ_FOREACH(cdw, &cs->cs_waiting_for_data_out, cdw_next)
		KASSERT(bhssc->bhssc_initiator_task_tag !=
		    cdw->cdw_initiator_task_tag, ("dangling cdw"));
	CFISCSI_SESSION_UNLOCK(cs);
#endif

	/*
	 * Do not return status for aborted commands.
	 * There are exceptions, but none supported by CTL yet.
	 */
	if (((io->io_hdr.flags & CTL_FLAG_ABORT) &&
	     (io->io_hdr.flags & CTL_FLAG_ABORT_STATUS) == 0) ||
	    (io->io_hdr.flags & CTL_FLAG_STATUS_SENT)) {
		ctl_free_io(io);
		icl_pdu_free(request);
		return;
	}

	response = cfiscsi_pdu_new_response(request, M_WAITOK);
	bhssr = (struct iscsi_bhs_scsi_response *)response->ip_bhs;
	bhssr->bhssr_opcode = ISCSI_BHS_OPCODE_SCSI_RESPONSE;
	bhssr->bhssr_flags = 0x80;
	/*
	 * XXX: We don't deal with bidirectional under/overflows;
	 *	does anything actually support those?
	 */
	if (PDU_TOTAL_TRANSFER_LEN(request) <
	    ntohl(bhssc->bhssc_expected_data_transfer_length)) {
		bhssr->bhssr_flags |= BHSSR_FLAGS_RESIDUAL_UNDERFLOW;
		bhssr->bhssr_residual_count =
		    htonl(ntohl(bhssc->bhssc_expected_data_transfer_length) -
		    PDU_TOTAL_TRANSFER_LEN(request));
		//CFISCSI_SESSION_DEBUG(cs, "underflow; residual count %d",
		//    ntohl(bhssr->bhssr_residual_count));
	} else if (PDU_TOTAL_TRANSFER_LEN(request) > 
	    ntohl(bhssc->bhssc_expected_data_transfer_length)) {
		bhssr->bhssr_flags |= BHSSR_FLAGS_RESIDUAL_OVERFLOW;
		bhssr->bhssr_residual_count =
		    htonl(PDU_TOTAL_TRANSFER_LEN(request) -
		    ntohl(bhssc->bhssc_expected_data_transfer_length));
		//CFISCSI_SESSION_DEBUG(cs, "overflow; residual count %d",
		//    ntohl(bhssr->bhssr_residual_count));
	}
	bhssr->bhssr_response = BHSSR_RESPONSE_COMMAND_COMPLETED;
	bhssr->bhssr_status = io->scsiio.scsi_status;
	bhssr->bhssr_initiator_task_tag = bhssc->bhssc_initiator_task_tag;
	bhssr->bhssr_expdatasn = htonl(PDU_EXPDATASN(request));

	if (io->scsiio.sense_len > 0) {
#if 0
		CFISCSI_SESSION_DEBUG(cs, "returning %d bytes of sense data",
		    io->scsiio.sense_len);
#endif
		sense_length = htons(io->scsiio.sense_len);
		icl_pdu_append_data(response,
		    &sense_length, sizeof(sense_length), M_WAITOK);
		icl_pdu_append_data(response,
		    &io->scsiio.sense_data, io->scsiio.sense_len, M_WAITOK);
	}

	ctl_free_io(io);
	icl_pdu_free(request);
	cfiscsi_pdu_queue(response);
}

static void
cfiscsi_task_management_done(union ctl_io *io)
{
	struct icl_pdu *request, *response;
	struct iscsi_bhs_task_management_request *bhstmr;
	struct iscsi_bhs_task_management_response *bhstmr2;
	struct cfiscsi_data_wait *cdw, *tmpcdw;
	struct cfiscsi_session *cs, *tcs;
	struct cfiscsi_softc *softc;
	int cold_reset = 0;

	request = io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;
	cs = PDU_SESSION(request);
	bhstmr = (struct iscsi_bhs_task_management_request *)request->ip_bhs;
	KASSERT((bhstmr->bhstmr_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) ==
	    ISCSI_BHS_OPCODE_TASK_REQUEST,
	    ("replying to wrong opcode 0x%x", bhstmr->bhstmr_opcode));

#if 0
	CFISCSI_SESSION_DEBUG(cs, "initiator task tag 0x%x; referenced task tag 0x%x",
	    bhstmr->bhstmr_initiator_task_tag,
	    bhstmr->bhstmr_referenced_task_tag);
#endif

	if ((bhstmr->bhstmr_function & ~0x80) ==
	    BHSTMR_FUNCTION_ABORT_TASK) {
		/*
		 * Make sure we no longer wait for Data-Out for this command.
		 */
		CFISCSI_SESSION_LOCK(cs);
		TAILQ_FOREACH_SAFE(cdw,
		    &cs->cs_waiting_for_data_out, cdw_next, tmpcdw) {
			if (bhstmr->bhstmr_referenced_task_tag !=
			    cdw->cdw_initiator_task_tag)
				continue;

#if 0
			CFISCSI_SESSION_DEBUG(cs, "removing csw for initiator task "
			    "tag 0x%x", bhstmr->bhstmr_initiator_task_tag);
#endif
			TAILQ_REMOVE(&cs->cs_waiting_for_data_out,
			    cdw, cdw_next);
			io->io_hdr.flags &= ~CTL_FLAG_DMA_INPROG;
			cdw->cdw_ctl_io->scsiio.io_hdr.port_status = 43;
			cdw->cdw_ctl_io->scsiio.be_move_done(cdw->cdw_ctl_io);
			cfiscsi_data_wait_free(cs, cdw);
		}
		CFISCSI_SESSION_UNLOCK(cs);
	}
	if ((bhstmr->bhstmr_function & ~0x80) ==
	    BHSTMR_FUNCTION_TARGET_COLD_RESET &&
	    io->io_hdr.status == CTL_SUCCESS)
		cold_reset = 1;

	response = cfiscsi_pdu_new_response(request, M_WAITOK);
	bhstmr2 = (struct iscsi_bhs_task_management_response *)
	    response->ip_bhs;
	bhstmr2->bhstmr_opcode = ISCSI_BHS_OPCODE_TASK_RESPONSE;
	bhstmr2->bhstmr_flags = 0x80;
	switch (io->taskio.task_status) {
	case CTL_TASK_FUNCTION_COMPLETE:
		bhstmr2->bhstmr_response = BHSTMR_RESPONSE_FUNCTION_COMPLETE;
		break;
	case CTL_TASK_FUNCTION_SUCCEEDED:
		bhstmr2->bhstmr_response = BHSTMR_RESPONSE_FUNCTION_SUCCEEDED;
		break;
	case CTL_TASK_LUN_DOES_NOT_EXIST:
		bhstmr2->bhstmr_response = BHSTMR_RESPONSE_LUN_DOES_NOT_EXIST;
		break;
	case CTL_TASK_FUNCTION_NOT_SUPPORTED:
	default:
		bhstmr2->bhstmr_response = BHSTMR_RESPONSE_FUNCTION_NOT_SUPPORTED;
		break;
	}
	memcpy(bhstmr2->bhstmr_additional_reponse_information,
	    io->taskio.task_resp, sizeof(io->taskio.task_resp));
	bhstmr2->bhstmr_initiator_task_tag = bhstmr->bhstmr_initiator_task_tag;

	ctl_free_io(io);
	icl_pdu_free(request);
	cfiscsi_pdu_queue(response);

	if (cold_reset) {
		softc = cs->cs_target->ct_softc;
		mtx_lock(&softc->lock);
		TAILQ_FOREACH(tcs, &softc->sessions, cs_next) {
			if (tcs->cs_target == cs->cs_target)
				cfiscsi_session_terminate(tcs);
		}
		mtx_unlock(&softc->lock);
	}
}

static void
cfiscsi_done(union ctl_io *io)
{
	struct icl_pdu *request;
	struct cfiscsi_session *cs;

	KASSERT(((io->io_hdr.status & CTL_STATUS_MASK) != CTL_STATUS_NONE),
		("invalid CTL status %#x", io->io_hdr.status));

	if (io->io_hdr.io_type == CTL_IO_TASK &&
	    io->taskio.task_action == CTL_TASK_I_T_NEXUS_RESET) {
		/*
		 * Implicit task termination has just completed; nothing to do.
		 */
		cs = io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;
		cs->cs_tasks_aborted = true;
		refcount_release(&cs->cs_outstanding_ctl_pdus);
		wakeup(__DEVOLATILE(void *, &cs->cs_outstanding_ctl_pdus));
		ctl_free_io(io);
		return;
	}

	request = io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;
	cs = PDU_SESSION(request);

	switch (request->ip_bhs->bhs_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) {
	case ISCSI_BHS_OPCODE_SCSI_COMMAND:
		cfiscsi_scsi_command_done(io);
		break;
	case ISCSI_BHS_OPCODE_TASK_REQUEST:
		cfiscsi_task_management_done(io);
		break;
	default:
		panic("cfiscsi_done called with wrong opcode 0x%x",
		    request->ip_bhs->bhs_opcode);
	}

	refcount_release(&cs->cs_outstanding_ctl_pdus);
}
