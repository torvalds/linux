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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/eventhandler.h>
#include <sys/file.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/sx.h>
#include <vm/uma.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_xpt.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_periph.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <dev/iscsi/icl.h>
#include <dev/iscsi/icl_wrappers.h>
#include <dev/iscsi/iscsi_ioctl.h>
#include <dev/iscsi/iscsi_proto.h>
#include <dev/iscsi/iscsi.h>

#ifdef ICL_KERNEL_PROXY
#include <sys/socketvar.h>
#endif

#ifdef ICL_KERNEL_PROXY
FEATURE(iscsi_kernel_proxy, "iSCSI initiator built with ICL_KERNEL_PROXY");
#endif

/*
 * XXX: This is global so the iscsi_unload() can access it.
 * 	Think about how to do this properly.
 */
static struct iscsi_softc	*sc;

SYSCTL_NODE(_kern, OID_AUTO, iscsi, CTLFLAG_RD, 0, "iSCSI initiator");
static int debug = 1;
SYSCTL_INT(_kern_iscsi, OID_AUTO, debug, CTLFLAG_RWTUN,
    &debug, 0, "Enable debug messages");
static int ping_timeout = 5;
SYSCTL_INT(_kern_iscsi, OID_AUTO, ping_timeout, CTLFLAG_RWTUN, &ping_timeout,
    0, "Timeout for ping (NOP-Out) requests, in seconds");
static int iscsid_timeout = 60;
SYSCTL_INT(_kern_iscsi, OID_AUTO, iscsid_timeout, CTLFLAG_RWTUN, &iscsid_timeout,
    0, "Time to wait for iscsid(8) to handle reconnection, in seconds");
static int login_timeout = 60;
SYSCTL_INT(_kern_iscsi, OID_AUTO, login_timeout, CTLFLAG_RWTUN, &login_timeout,
    0, "Time to wait for iscsid(8) to finish Login Phase, in seconds");
static int maxtags = 255;
SYSCTL_INT(_kern_iscsi, OID_AUTO, maxtags, CTLFLAG_RWTUN, &maxtags,
    0, "Max number of IO requests queued");
static int fail_on_disconnection = 0;
SYSCTL_INT(_kern_iscsi, OID_AUTO, fail_on_disconnection, CTLFLAG_RWTUN,
    &fail_on_disconnection, 0, "Destroy CAM SIM on connection failure");
static int fail_on_shutdown = 1;
SYSCTL_INT(_kern_iscsi, OID_AUTO, fail_on_shutdown, CTLFLAG_RWTUN,
    &fail_on_shutdown, 0, "Fail disconnected sessions on shutdown");

static MALLOC_DEFINE(M_ISCSI, "iSCSI", "iSCSI initiator");
static uma_zone_t iscsi_outstanding_zone;

#define	CONN_SESSION(X)	((struct iscsi_session *)X->ic_prv0)
#define	PDU_SESSION(X)	(CONN_SESSION(X->ip_conn))

#define	ISCSI_DEBUG(X, ...)						\
	do {								\
		if (debug > 1) 						\
			printf("%s: " X "\n", __func__, ## __VA_ARGS__);\
	} while (0)

#define	ISCSI_WARN(X, ...)						\
	do {								\
		if (debug > 0) {					\
			printf("WARNING: %s: " X "\n",			\
			    __func__, ## __VA_ARGS__);			\
		}							\
	} while (0)

#define	ISCSI_SESSION_DEBUG(S, X, ...)					\
	do {								\
		if (debug > 1) {					\
			printf("%s: %s (%s): " X "\n",			\
			    __func__, S->is_conf.isc_target_addr,	\
			    S->is_conf.isc_target, ## __VA_ARGS__);	\
		}							\
	} while (0)

#define	ISCSI_SESSION_WARN(S, X, ...)					\
	do {								\
		if (debug > 0) {					\
			printf("WARNING: %s (%s): " X "\n",		\
			    S->is_conf.isc_target_addr,			\
			    S->is_conf.isc_target, ## __VA_ARGS__);	\
		}							\
	} while (0)

#define ISCSI_SESSION_LOCK(X)		mtx_lock(&X->is_lock)
#define ISCSI_SESSION_UNLOCK(X)		mtx_unlock(&X->is_lock)
#define ISCSI_SESSION_LOCK_ASSERT(X)	mtx_assert(&X->is_lock, MA_OWNED)
#define ISCSI_SESSION_LOCK_ASSERT_NOT(X) mtx_assert(&X->is_lock, MA_NOTOWNED)

static int	iscsi_ioctl(struct cdev *dev, u_long cmd, caddr_t arg,
		    int mode, struct thread *td);

static struct cdevsw iscsi_cdevsw = {
     .d_version = D_VERSION,
     .d_ioctl   = iscsi_ioctl,
     .d_name    = "iscsi",
};

static void	iscsi_pdu_queue_locked(struct icl_pdu *request);
static void	iscsi_pdu_queue(struct icl_pdu *request);
static void	iscsi_pdu_update_statsn(const struct icl_pdu *response);
static void	iscsi_pdu_handle_nop_in(struct icl_pdu *response);
static void	iscsi_pdu_handle_scsi_response(struct icl_pdu *response);
static void	iscsi_pdu_handle_task_response(struct icl_pdu *response);
static void	iscsi_pdu_handle_data_in(struct icl_pdu *response);
static void	iscsi_pdu_handle_logout_response(struct icl_pdu *response);
static void	iscsi_pdu_handle_r2t(struct icl_pdu *response);
static void	iscsi_pdu_handle_async_message(struct icl_pdu *response);
static void	iscsi_pdu_handle_reject(struct icl_pdu *response);
static void	iscsi_session_reconnect(struct iscsi_session *is);
static void	iscsi_session_terminate(struct iscsi_session *is);
static void	iscsi_action(struct cam_sim *sim, union ccb *ccb);
static void	iscsi_poll(struct cam_sim *sim);
static struct iscsi_outstanding	*iscsi_outstanding_find(struct iscsi_session *is,
		    uint32_t initiator_task_tag);
static struct iscsi_outstanding	*iscsi_outstanding_add(struct iscsi_session *is,
		    struct icl_pdu *request, union ccb *ccb,
		    uint32_t *initiator_task_tagp);
static void	iscsi_outstanding_remove(struct iscsi_session *is,
		    struct iscsi_outstanding *io);

static bool
iscsi_pdu_prepare(struct icl_pdu *request)
{
	struct iscsi_session *is;
	struct iscsi_bhs_scsi_command *bhssc;

	is = PDU_SESSION(request);

	ISCSI_SESSION_LOCK_ASSERT(is);

	/*
	 * We're only using fields common for all the request
	 * (initiator -> target) PDUs.
	 */
	bhssc = (struct iscsi_bhs_scsi_command *)request->ip_bhs;

	/*
	 * Data-Out PDU does not contain CmdSN.
	 */
	if (bhssc->bhssc_opcode != ISCSI_BHS_OPCODE_SCSI_DATA_OUT) {
		if (ISCSI_SNGT(is->is_cmdsn, is->is_maxcmdsn) &&
		    (bhssc->bhssc_opcode & ISCSI_BHS_OPCODE_IMMEDIATE) == 0) {
			/*
			 * Current MaxCmdSN prevents us from sending any more
			 * SCSI Command PDUs to the target; postpone the PDU.
			 * It will get resent by either iscsi_pdu_queue(),
			 * or by maintenance thread.
			 */
#if 0
			ISCSI_SESSION_DEBUG(is, "postponing send, CmdSN %u, "
			    "ExpCmdSN %u, MaxCmdSN %u, opcode 0x%x",
			    is->is_cmdsn, is->is_expcmdsn, is->is_maxcmdsn,
			    bhssc->bhssc_opcode);
#endif
			return (true);
		}
		bhssc->bhssc_cmdsn = htonl(is->is_cmdsn);
		if ((bhssc->bhssc_opcode & ISCSI_BHS_OPCODE_IMMEDIATE) == 0)
			is->is_cmdsn++;
	}
	bhssc->bhssc_expstatsn = htonl(is->is_statsn + 1);

	return (false);
}

static void
iscsi_session_send_postponed(struct iscsi_session *is)
{
	struct icl_pdu *request;
	bool postpone;

	ISCSI_SESSION_LOCK_ASSERT(is);

	if (STAILQ_EMPTY(&is->is_postponed))
		return;
	while ((request = STAILQ_FIRST(&is->is_postponed)) != NULL) {
		postpone = iscsi_pdu_prepare(request);
		if (postpone)
			return;
		STAILQ_REMOVE_HEAD(&is->is_postponed, ip_next);
		icl_pdu_queue(request);
	}
	xpt_release_simq(is->is_sim, 1);
}

static void
iscsi_pdu_queue_locked(struct icl_pdu *request)
{
	struct iscsi_session *is;
	bool postpone;

	is = PDU_SESSION(request);
	ISCSI_SESSION_LOCK_ASSERT(is);
	iscsi_session_send_postponed(is);
	postpone = iscsi_pdu_prepare(request);
	if (postpone) {
		if (STAILQ_EMPTY(&is->is_postponed))
			xpt_freeze_simq(is->is_sim, 1);
		STAILQ_INSERT_TAIL(&is->is_postponed, request, ip_next);
		return;
	}
	icl_pdu_queue(request);
}

static void
iscsi_pdu_queue(struct icl_pdu *request)
{
	struct iscsi_session *is;

	is = PDU_SESSION(request);
	ISCSI_SESSION_LOCK(is);
	iscsi_pdu_queue_locked(request);
	ISCSI_SESSION_UNLOCK(is);
}

static void
iscsi_session_logout(struct iscsi_session *is)
{
	struct icl_pdu *request;
	struct iscsi_bhs_logout_request *bhslr;

	request = icl_pdu_new(is->is_conn, M_NOWAIT);
	if (request == NULL)
		return;

	bhslr = (struct iscsi_bhs_logout_request *)request->ip_bhs;
	bhslr->bhslr_opcode = ISCSI_BHS_OPCODE_LOGOUT_REQUEST;
	bhslr->bhslr_reason = BHSLR_REASON_CLOSE_SESSION;
	iscsi_pdu_queue_locked(request);
}

static void
iscsi_session_terminate_task(struct iscsi_session *is,
    struct iscsi_outstanding *io, cam_status status)
{

	ISCSI_SESSION_LOCK_ASSERT(is);

	if (io->io_ccb != NULL) {
		io->io_ccb->ccb_h.status &= ~(CAM_SIM_QUEUED | CAM_STATUS_MASK);
		io->io_ccb->ccb_h.status |= status;
		if ((io->io_ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
			io->io_ccb->ccb_h.status |= CAM_DEV_QFRZN;
			xpt_freeze_devq(io->io_ccb->ccb_h.path, 1);
			ISCSI_SESSION_DEBUG(is, "freezing devq");
		}
		xpt_done(io->io_ccb);
	}
	iscsi_outstanding_remove(is, io);
}

static void
iscsi_session_terminate_tasks(struct iscsi_session *is, cam_status status)
{
	struct iscsi_outstanding *io, *tmp;

	ISCSI_SESSION_LOCK_ASSERT(is);

	TAILQ_FOREACH_SAFE(io, &is->is_outstanding, io_next, tmp) {
		iscsi_session_terminate_task(is, io, status);
	}
}

static void
iscsi_session_cleanup(struct iscsi_session *is, bool destroy_sim)
{
	struct icl_pdu *pdu;

	ISCSI_SESSION_LOCK_ASSERT(is);

	/*
	 * Don't queue any new PDUs.
	 */
	if (is->is_sim != NULL && is->is_simq_frozen == false) {
		ISCSI_SESSION_DEBUG(is, "freezing");
		xpt_freeze_simq(is->is_sim, 1);
		is->is_simq_frozen = true;
	}

	/*
	 * Remove postponed PDUs.
	 */
	if (!STAILQ_EMPTY(&is->is_postponed))
		xpt_release_simq(is->is_sim, 1);
	while ((pdu = STAILQ_FIRST(&is->is_postponed)) != NULL) {
		STAILQ_REMOVE_HEAD(&is->is_postponed, ip_next);
		icl_pdu_free(pdu);
	}

	if (destroy_sim == false) {
		/*
		 * Terminate SCSI tasks, asking CAM to requeue them.
		 */
		iscsi_session_terminate_tasks(is, CAM_REQUEUE_REQ);
		return;
	}

	iscsi_session_terminate_tasks(is, CAM_DEV_NOT_THERE);

	if (is->is_sim == NULL)
		return;

	ISCSI_SESSION_DEBUG(is, "deregistering SIM");
	xpt_async(AC_LOST_DEVICE, is->is_path, NULL);

	if (is->is_simq_frozen) {
		xpt_release_simq(is->is_sim, 1);
		is->is_simq_frozen = false;
	}

	xpt_free_path(is->is_path);
	is->is_path = NULL;
	xpt_bus_deregister(cam_sim_path(is->is_sim));
	cam_sim_free(is->is_sim, TRUE /*free_devq*/);
	is->is_sim = NULL;
	is->is_devq = NULL;
}

static void
iscsi_maintenance_thread_reconnect(struct iscsi_session *is)
{

	icl_conn_close(is->is_conn);

	ISCSI_SESSION_LOCK(is);

	is->is_connected = false;
	is->is_reconnecting = false;
	is->is_login_phase = false;

#ifdef ICL_KERNEL_PROXY
	if (is->is_login_pdu != NULL) {
		icl_pdu_free(is->is_login_pdu);
		is->is_login_pdu = NULL;
	}
	cv_signal(&is->is_login_cv);
#endif
 
	if (fail_on_disconnection) {
		ISCSI_SESSION_DEBUG(is, "connection failed, destroying devices");
		iscsi_session_cleanup(is, true);
	} else {
		iscsi_session_cleanup(is, false);
	}
 
	KASSERT(TAILQ_EMPTY(&is->is_outstanding),
	    ("destroying session with active tasks"));
	KASSERT(STAILQ_EMPTY(&is->is_postponed),
	    ("destroying session with postponed PDUs"));

	if (is->is_conf.isc_enable == 0 && is->is_conf.isc_discovery == 0) {
		ISCSI_SESSION_UNLOCK(is);
		return;
	}

	/*
	 * Request immediate reconnection from iscsid(8).
	 */
	//ISCSI_SESSION_DEBUG(is, "waking up iscsid(8)");
	is->is_waiting_for_iscsid = true;
	strlcpy(is->is_reason, "Waiting for iscsid(8)", sizeof(is->is_reason));
	is->is_timeout = 0;
	ISCSI_SESSION_UNLOCK(is);
	cv_signal(&is->is_softc->sc_cv);
}

static void
iscsi_maintenance_thread_terminate(struct iscsi_session *is)
{
	struct iscsi_softc *sc;

	sc = is->is_softc;
	sx_xlock(&sc->sc_lock);
	TAILQ_REMOVE(&sc->sc_sessions, is, is_next);
	sx_xunlock(&sc->sc_lock);

	icl_conn_close(is->is_conn);
	callout_drain(&is->is_callout);

	ISCSI_SESSION_LOCK(is);

	KASSERT(is->is_terminating, ("is_terminating == false"));

#ifdef ICL_KERNEL_PROXY
	if (is->is_login_pdu != NULL) {
		icl_pdu_free(is->is_login_pdu);
		is->is_login_pdu = NULL;
	}
	cv_signal(&is->is_login_cv);
#endif

	iscsi_session_cleanup(is, true);

	KASSERT(TAILQ_EMPTY(&is->is_outstanding),
	    ("destroying session with active tasks"));
	KASSERT(STAILQ_EMPTY(&is->is_postponed),
	    ("destroying session with postponed PDUs"));

	ISCSI_SESSION_UNLOCK(is);

	icl_conn_free(is->is_conn);
	mtx_destroy(&is->is_lock);
	cv_destroy(&is->is_maintenance_cv);
#ifdef ICL_KERNEL_PROXY
	cv_destroy(&is->is_login_cv);
#endif

	ISCSI_SESSION_DEBUG(is, "terminated");
	free(is, M_ISCSI);

	/*
	 * The iscsi_unload() routine might be waiting.
	 */
	cv_signal(&sc->sc_cv);
}

static void
iscsi_maintenance_thread(void *arg)
{
	struct iscsi_session *is = arg;

	ISCSI_SESSION_LOCK(is);
	for (;;) {
		if (is->is_reconnecting == false &&
		    is->is_terminating == false &&
		    (STAILQ_EMPTY(&is->is_postponed) ||
		     ISCSI_SNGT(is->is_cmdsn, is->is_maxcmdsn)))
			cv_wait(&is->is_maintenance_cv, &is->is_lock);

		/* Terminate supersedes reconnect. */
		if (is->is_terminating) {
			ISCSI_SESSION_UNLOCK(is);
			iscsi_maintenance_thread_terminate(is);
			kthread_exit();
			return;
		}

		if (is->is_reconnecting) {
			ISCSI_SESSION_UNLOCK(is);
			iscsi_maintenance_thread_reconnect(is);
			ISCSI_SESSION_LOCK(is);
			continue;
		}

		iscsi_session_send_postponed(is);
	}
	ISCSI_SESSION_UNLOCK(is);
}

static void
iscsi_session_reconnect(struct iscsi_session *is)
{

	/*
	 * XXX: We can't use locking here, because
	 * 	it's being called from various contexts.
	 * 	Hope it doesn't break anything.
	 */
	if (is->is_reconnecting)
		return;

	is->is_reconnecting = true;
	cv_signal(&is->is_maintenance_cv);
}

static void
iscsi_session_terminate(struct iscsi_session *is)
{

	if (is->is_terminating)
		return;

	is->is_terminating = true;

#if 0
	iscsi_session_logout(is);
#endif
	cv_signal(&is->is_maintenance_cv);
}

static void
iscsi_callout(void *context)
{
	struct icl_pdu *request;
	struct iscsi_bhs_nop_out *bhsno;
	struct iscsi_session *is;
	bool reconnect_needed = false;

	is = context;

	ISCSI_SESSION_LOCK(is);
	if (is->is_terminating) {
		ISCSI_SESSION_UNLOCK(is);
		return;
	}

	callout_schedule(&is->is_callout, 1 * hz);

	if (is->is_conf.isc_enable == 0)
		goto out;

	is->is_timeout++;

	if (is->is_waiting_for_iscsid) {
		if (iscsid_timeout > 0 && is->is_timeout > iscsid_timeout) {
			ISCSI_SESSION_WARN(is, "timed out waiting for iscsid(8) "
			    "for %d seconds; reconnecting",
			    is->is_timeout);
			reconnect_needed = true;
		}
		goto out;
	}

	if (is->is_login_phase) {
		if (login_timeout > 0 && is->is_timeout > login_timeout) {
			ISCSI_SESSION_WARN(is, "login timed out after %d seconds; "
			    "reconnecting", is->is_timeout);
			reconnect_needed = true;
		}
		goto out;
	}

	if (ping_timeout <= 0) {
		/*
		 * Pings are disabled.  Don't send NOP-Out in this case.
		 * Reset the timeout, to avoid triggering reconnection,
		 * should the user decide to reenable them.
		 */
		is->is_timeout = 0;
		goto out;
	}

	if (is->is_timeout >= ping_timeout) {
		ISCSI_SESSION_WARN(is, "no ping reply (NOP-In) after %d seconds; "
		    "reconnecting", ping_timeout);
		reconnect_needed = true;
		goto out;
	}

	ISCSI_SESSION_UNLOCK(is);

	/*
	 * If the ping was reset less than one second ago - which means
	 * that we've received some PDU during the last second - assume
	 * the traffic flows correctly and don't bother sending a NOP-Out.
	 *
	 * (It's 2 - one for one second, and one for incrementing is_timeout
	 * earlier in this routine.)
	 */
	if (is->is_timeout < 2)
		return;

	request = icl_pdu_new(is->is_conn, M_NOWAIT);
	if (request == NULL) {
		ISCSI_SESSION_WARN(is, "failed to allocate PDU");
		return;
	}
	bhsno = (struct iscsi_bhs_nop_out *)request->ip_bhs;
	bhsno->bhsno_opcode = ISCSI_BHS_OPCODE_NOP_OUT |
	    ISCSI_BHS_OPCODE_IMMEDIATE;
	bhsno->bhsno_flags = 0x80;
	bhsno->bhsno_target_transfer_tag = 0xffffffff;
	iscsi_pdu_queue(request);
	return;

out:
	if (is->is_terminating) {
		ISCSI_SESSION_UNLOCK(is);
		return;
	}

	ISCSI_SESSION_UNLOCK(is);

	if (reconnect_needed)
		iscsi_session_reconnect(is);
}

static void
iscsi_pdu_update_statsn(const struct icl_pdu *response)
{
	const struct iscsi_bhs_data_in *bhsdi;
	struct iscsi_session *is;
	uint32_t expcmdsn, maxcmdsn, statsn;

	is = PDU_SESSION(response);

	ISCSI_SESSION_LOCK_ASSERT(is);

	/*
	 * We're only using fields common for all the response
	 * (target -> initiator) PDUs.
	 */
	bhsdi = (const struct iscsi_bhs_data_in *)response->ip_bhs;
	/*
	 * Ok, I lied.  In case of Data-In, "The fields StatSN, Status,
	 * and Residual Count only have meaningful content if the S bit
	 * is set to 1", so we also need to check the bit specific for
	 * Data-In PDU.
	 */
	if (bhsdi->bhsdi_opcode != ISCSI_BHS_OPCODE_SCSI_DATA_IN ||
	    (bhsdi->bhsdi_flags & BHSDI_FLAGS_S) != 0) {
		statsn = ntohl(bhsdi->bhsdi_statsn);
		if (statsn != is->is_statsn && statsn != (is->is_statsn + 1)) {
			/* XXX: This is normal situation for MCS */
			ISCSI_SESSION_WARN(is, "PDU 0x%x StatSN %u != "
			    "session ExpStatSN %u (or + 1); reconnecting",
			    bhsdi->bhsdi_opcode, statsn, is->is_statsn);
			iscsi_session_reconnect(is);
		}
		if (ISCSI_SNGT(statsn, is->is_statsn))
			is->is_statsn = statsn;
	}

	expcmdsn = ntohl(bhsdi->bhsdi_expcmdsn);
	maxcmdsn = ntohl(bhsdi->bhsdi_maxcmdsn);

	if (ISCSI_SNLT(maxcmdsn + 1, expcmdsn)) {
		ISCSI_SESSION_DEBUG(is,
		    "PDU MaxCmdSN %u + 1 < PDU ExpCmdSN %u; ignoring",
		    maxcmdsn, expcmdsn);
	} else {
		if (ISCSI_SNGT(maxcmdsn, is->is_maxcmdsn)) {
			is->is_maxcmdsn = maxcmdsn;

			/*
			 * Command window increased; kick the maintanance thread
			 * to send out postponed commands.
			 */
			if (!STAILQ_EMPTY(&is->is_postponed))
				cv_signal(&is->is_maintenance_cv);
		} else if (ISCSI_SNLT(maxcmdsn, is->is_maxcmdsn)) {
			/* XXX: This is normal situation for MCS */
			ISCSI_SESSION_DEBUG(is,
			    "PDU MaxCmdSN %u < session MaxCmdSN %u; ignoring",
			    maxcmdsn, is->is_maxcmdsn);
		}

		if (ISCSI_SNGT(expcmdsn, is->is_expcmdsn)) {
			is->is_expcmdsn = expcmdsn;
		} else if (ISCSI_SNLT(expcmdsn, is->is_expcmdsn)) {
			/* XXX: This is normal situation for MCS */
			ISCSI_SESSION_DEBUG(is,
			    "PDU ExpCmdSN %u < session ExpCmdSN %u; ignoring",
			    expcmdsn, is->is_expcmdsn);
		}
	}

	/*
	 * Every incoming PDU - not just NOP-In - resets the ping timer.
	 * The purpose of the timeout is to reset the connection when it stalls;
	 * we don't want this to happen when NOP-In or NOP-Out ends up delayed
	 * in some queue.
	 */
	is->is_timeout = 0;
}

static void
iscsi_receive_callback(struct icl_pdu *response)
{
	struct iscsi_session *is;

	is = PDU_SESSION(response);

	ISCSI_SESSION_LOCK(is);

	iscsi_pdu_update_statsn(response);

#ifdef ICL_KERNEL_PROXY
	if (is->is_login_phase) {
		if (is->is_login_pdu == NULL)
			is->is_login_pdu = response;
		else
			icl_pdu_free(response);
		ISCSI_SESSION_UNLOCK(is);
		cv_signal(&is->is_login_cv);
		return;
	}
#endif

	/*
	 * The handling routine is responsible for freeing the PDU
	 * when it's no longer needed.
	 */
	switch (response->ip_bhs->bhs_opcode) {
	case ISCSI_BHS_OPCODE_NOP_IN:
		iscsi_pdu_handle_nop_in(response);
		ISCSI_SESSION_UNLOCK(is);
		break;
	case ISCSI_BHS_OPCODE_SCSI_RESPONSE:
		iscsi_pdu_handle_scsi_response(response);
		/* Session lock dropped inside. */
		ISCSI_SESSION_LOCK_ASSERT_NOT(is);
		break;
	case ISCSI_BHS_OPCODE_TASK_RESPONSE:
		iscsi_pdu_handle_task_response(response);
		ISCSI_SESSION_UNLOCK(is);
		break;
	case ISCSI_BHS_OPCODE_SCSI_DATA_IN:
		iscsi_pdu_handle_data_in(response);
		/* Session lock dropped inside. */
		ISCSI_SESSION_LOCK_ASSERT_NOT(is);
		break;
	case ISCSI_BHS_OPCODE_LOGOUT_RESPONSE:
		iscsi_pdu_handle_logout_response(response);
		ISCSI_SESSION_UNLOCK(is);
		break;
	case ISCSI_BHS_OPCODE_R2T:
		iscsi_pdu_handle_r2t(response);
		ISCSI_SESSION_UNLOCK(is);
		break;
	case ISCSI_BHS_OPCODE_ASYNC_MESSAGE:
		iscsi_pdu_handle_async_message(response);
		ISCSI_SESSION_UNLOCK(is);
		break;
	case ISCSI_BHS_OPCODE_REJECT:
		iscsi_pdu_handle_reject(response);
		ISCSI_SESSION_UNLOCK(is);
		break;
	default:
		ISCSI_SESSION_WARN(is, "received PDU with unsupported "
		    "opcode 0x%x; reconnecting",
		    response->ip_bhs->bhs_opcode);
		iscsi_session_reconnect(is);
		ISCSI_SESSION_UNLOCK(is);
		icl_pdu_free(response);
	}
}

static void
iscsi_error_callback(struct icl_conn *ic)
{
	struct iscsi_session *is;

	is = CONN_SESSION(ic);

	ISCSI_SESSION_WARN(is, "connection error; reconnecting");
	iscsi_session_reconnect(is);
}

static void
iscsi_pdu_handle_nop_in(struct icl_pdu *response)
{
	struct iscsi_session *is;
	struct iscsi_bhs_nop_out *bhsno;
	struct iscsi_bhs_nop_in *bhsni;
	struct icl_pdu *request;
	void *data = NULL;
	size_t datasize;
	int error;

	is = PDU_SESSION(response);
	bhsni = (struct iscsi_bhs_nop_in *)response->ip_bhs;

	if (bhsni->bhsni_target_transfer_tag == 0xffffffff) {
		/*
		 * Nothing to do; iscsi_pdu_update_statsn() already
		 * zeroed the timeout.
		 */
		icl_pdu_free(response);
		return;
	}

	datasize = icl_pdu_data_segment_length(response);
	if (datasize > 0) {
		data = malloc(datasize, M_ISCSI, M_NOWAIT | M_ZERO);
		if (data == NULL) {
			ISCSI_SESSION_WARN(is, "failed to allocate memory; "
			    "reconnecting");
			icl_pdu_free(response);
			iscsi_session_reconnect(is);
			return;
		}
		icl_pdu_get_data(response, 0, data, datasize);
	}

	request = icl_pdu_new(response->ip_conn, M_NOWAIT);
	if (request == NULL) {
		ISCSI_SESSION_WARN(is, "failed to allocate memory; "
		    "reconnecting");
		free(data, M_ISCSI);
		icl_pdu_free(response);
		iscsi_session_reconnect(is);
		return;
	}
	bhsno = (struct iscsi_bhs_nop_out *)request->ip_bhs;
	bhsno->bhsno_opcode = ISCSI_BHS_OPCODE_NOP_OUT |
	    ISCSI_BHS_OPCODE_IMMEDIATE;
	bhsno->bhsno_flags = 0x80;
	bhsno->bhsno_initiator_task_tag = 0xffffffff;
	bhsno->bhsno_target_transfer_tag = bhsni->bhsni_target_transfer_tag;
	if (datasize > 0) {
		error = icl_pdu_append_data(request, data, datasize, M_NOWAIT);
		if (error != 0) {
			ISCSI_SESSION_WARN(is, "failed to allocate memory; "
			    "reconnecting");
			free(data, M_ISCSI);
			icl_pdu_free(request);
			icl_pdu_free(response);
			iscsi_session_reconnect(is);
			return;
		}
		free(data, M_ISCSI);
	}

	icl_pdu_free(response);
	iscsi_pdu_queue_locked(request);
}

static void
iscsi_pdu_handle_scsi_response(struct icl_pdu *response)
{
	struct iscsi_bhs_scsi_response *bhssr;
	struct iscsi_outstanding *io;
	struct iscsi_session *is;
	union ccb *ccb;
	struct ccb_scsiio *csio;
	size_t data_segment_len, received;
	uint16_t sense_len;
	uint32_t resid;

	is = PDU_SESSION(response);

	bhssr = (struct iscsi_bhs_scsi_response *)response->ip_bhs;
	io = iscsi_outstanding_find(is, bhssr->bhssr_initiator_task_tag);
	if (io == NULL || io->io_ccb == NULL) {
		ISCSI_SESSION_WARN(is, "bad itt 0x%x", bhssr->bhssr_initiator_task_tag);
		icl_pdu_free(response);
		iscsi_session_reconnect(is);
		ISCSI_SESSION_UNLOCK(is);
		return;
	}

	ccb = io->io_ccb;

	/*
	 * With iSER, after getting good response we can be sure
	 * that all the data has been successfully transferred.
	 */
	if (is->is_conn->ic_iser) {
		resid = ntohl(bhssr->bhssr_residual_count);
		if (bhssr->bhssr_flags & BHSSR_FLAGS_RESIDUAL_UNDERFLOW) {
			io->io_received = ccb->csio.dxfer_len - resid;
		} else if (bhssr->bhssr_flags & BHSSR_FLAGS_RESIDUAL_OVERFLOW) {
			ISCSI_SESSION_WARN(is, "overflow: target indicates %d", resid);
		} else {
			io->io_received = ccb->csio.dxfer_len;
		}
	}

	received = io->io_received;
	iscsi_outstanding_remove(is, io);
	ISCSI_SESSION_UNLOCK(is);

	if (bhssr->bhssr_response != BHSSR_RESPONSE_COMMAND_COMPLETED) {
		ISCSI_SESSION_WARN(is, "service response 0x%x", bhssr->bhssr_response);
 		if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
 			xpt_freeze_devq(ccb->ccb_h.path, 1);
			ISCSI_SESSION_DEBUG(is, "freezing devq");
		}
 		ccb->ccb_h.status = CAM_REQ_CMP_ERR | CAM_DEV_QFRZN;
	} else if (bhssr->bhssr_status == 0) {
		ccb->ccb_h.status = CAM_REQ_CMP;
	} else {
 		if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
 			xpt_freeze_devq(ccb->ccb_h.path, 1);
			ISCSI_SESSION_DEBUG(is, "freezing devq");
		}
 		ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR | CAM_DEV_QFRZN;
		ccb->csio.scsi_status = bhssr->bhssr_status;
	}

	csio = &ccb->csio;
	data_segment_len = icl_pdu_data_segment_length(response);
	if (data_segment_len > 0) {
		if (data_segment_len < sizeof(sense_len)) {
			ISCSI_SESSION_WARN(is, "truncated data segment (%zd bytes)",
			    data_segment_len);
			if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
				xpt_freeze_devq(ccb->ccb_h.path, 1);
				ISCSI_SESSION_DEBUG(is, "freezing devq");
			}
			ccb->ccb_h.status = CAM_REQ_CMP_ERR | CAM_DEV_QFRZN;
			goto out;
		}
		icl_pdu_get_data(response, 0, &sense_len, sizeof(sense_len));
		sense_len = ntohs(sense_len);
#if 0
		ISCSI_SESSION_DEBUG(is, "sense_len %d, data len %zd",
		    sense_len, data_segment_len);
#endif
		if (sizeof(sense_len) + sense_len > data_segment_len) {
			ISCSI_SESSION_WARN(is, "truncated data segment "
			    "(%zd bytes, should be %zd)",
			    data_segment_len, sizeof(sense_len) + sense_len);
			if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
				xpt_freeze_devq(ccb->ccb_h.path, 1);
				ISCSI_SESSION_DEBUG(is, "freezing devq");
			}
			ccb->ccb_h.status = CAM_REQ_CMP_ERR | CAM_DEV_QFRZN;
			goto out;
		} else if (sizeof(sense_len) + sense_len < data_segment_len)
			ISCSI_SESSION_WARN(is, "oversize data segment "
			    "(%zd bytes, should be %zd)",
			    data_segment_len, sizeof(sense_len) + sense_len);
		if (sense_len > csio->sense_len) {
			ISCSI_SESSION_DEBUG(is, "truncating sense from %d to %d",
			    sense_len, csio->sense_len);
			sense_len = csio->sense_len;
		}
		icl_pdu_get_data(response, sizeof(sense_len), &csio->sense_data, sense_len);
		csio->sense_resid = csio->sense_len - sense_len;
		ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
	}

out:
	if (bhssr->bhssr_flags & BHSSR_FLAGS_RESIDUAL_UNDERFLOW)
		csio->resid = ntohl(bhssr->bhssr_residual_count);

	if ((csio->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
		KASSERT(received <= csio->dxfer_len,
		    ("received > csio->dxfer_len"));
		if (received < csio->dxfer_len) {
			if (csio->resid != csio->dxfer_len - received) {
				ISCSI_SESSION_WARN(is, "underflow mismatch: "
				    "target indicates %d, we calculated %zd",
				    csio->resid, csio->dxfer_len - received);
			}
			csio->resid = csio->dxfer_len - received;
		}
	}

	xpt_done(ccb);
	icl_pdu_free(response);
}

static void
iscsi_pdu_handle_task_response(struct icl_pdu *response)
{
	struct iscsi_bhs_task_management_response *bhstmr;
	struct iscsi_outstanding *io, *aio;
	struct iscsi_session *is;

	is = PDU_SESSION(response);

	bhstmr = (struct iscsi_bhs_task_management_response *)response->ip_bhs;
	io = iscsi_outstanding_find(is, bhstmr->bhstmr_initiator_task_tag);
	if (io == NULL || io->io_ccb != NULL) {
		ISCSI_SESSION_WARN(is, "bad itt 0x%x",
		    bhstmr->bhstmr_initiator_task_tag);
		icl_pdu_free(response);
		iscsi_session_reconnect(is);
		return;
	}

	if (bhstmr->bhstmr_response != BHSTMR_RESPONSE_FUNCTION_COMPLETE) {
		ISCSI_SESSION_WARN(is, "task response 0x%x",
		    bhstmr->bhstmr_response);
	} else {
		aio = iscsi_outstanding_find(is, io->io_datasn);
		if (aio != NULL && aio->io_ccb != NULL)
			iscsi_session_terminate_task(is, aio, CAM_REQ_ABORTED);
	}

	iscsi_outstanding_remove(is, io);
	icl_pdu_free(response);
}

static void
iscsi_pdu_handle_data_in(struct icl_pdu *response)
{
	struct iscsi_bhs_data_in *bhsdi;
	struct iscsi_outstanding *io;
	struct iscsi_session *is;
	union ccb *ccb;
	struct ccb_scsiio *csio;
	size_t data_segment_len, received, oreceived;
	
	is = PDU_SESSION(response);
	bhsdi = (struct iscsi_bhs_data_in *)response->ip_bhs;
	io = iscsi_outstanding_find(is, bhsdi->bhsdi_initiator_task_tag);
	if (io == NULL || io->io_ccb == NULL) {
		ISCSI_SESSION_WARN(is, "bad itt 0x%x", bhsdi->bhsdi_initiator_task_tag);
		icl_pdu_free(response);
		iscsi_session_reconnect(is);
		ISCSI_SESSION_UNLOCK(is);
		return;
	}

	data_segment_len = icl_pdu_data_segment_length(response);
	if (data_segment_len == 0) {
		/*
		 * "The sending of 0 length data segments should be avoided,
		 * but initiators and targets MUST be able to properly receive
		 * 0 length data segments."
		 */
		ISCSI_SESSION_UNLOCK(is);
		icl_pdu_free(response);
		return;
	}

	/*
	 * We need to track this for security reasons - without it, malicious target
	 * could respond to SCSI READ without sending Data-In PDUs, which would result
	 * in read operation on the initiator side returning random kernel data.
	 */
	if (ntohl(bhsdi->bhsdi_buffer_offset) != io->io_received) {
		ISCSI_SESSION_WARN(is, "data out of order; expected offset %zd, got %zd",
		    io->io_received, (size_t)ntohl(bhsdi->bhsdi_buffer_offset));
		icl_pdu_free(response);
		iscsi_session_reconnect(is);
		ISCSI_SESSION_UNLOCK(is);
		return;
	}

	ccb = io->io_ccb;
	csio = &ccb->csio;

	if (io->io_received + data_segment_len > csio->dxfer_len) {
		ISCSI_SESSION_WARN(is, "oversize data segment (%zd bytes "
		    "at offset %zd, buffer is %d)",
		    data_segment_len, io->io_received, csio->dxfer_len);
		icl_pdu_free(response);
		iscsi_session_reconnect(is);
		ISCSI_SESSION_UNLOCK(is);
		return;
	}

	oreceived = io->io_received;
	io->io_received += data_segment_len;
	received = io->io_received;
	if ((bhsdi->bhsdi_flags & BHSDI_FLAGS_S) != 0)
		iscsi_outstanding_remove(is, io);
	ISCSI_SESSION_UNLOCK(is);

	icl_pdu_get_data(response, 0, csio->data_ptr + oreceived, data_segment_len);

	/*
	 * XXX: Check DataSN.
	 * XXX: Check F.
	 */
	if ((bhsdi->bhsdi_flags & BHSDI_FLAGS_S) == 0) {
		/*
		 * Nothing more to do.
		 */
		icl_pdu_free(response);
		return;
	}

	//ISCSI_SESSION_DEBUG(is, "got S flag; status 0x%x", bhsdi->bhsdi_status);
	if (bhsdi->bhsdi_status == 0) {
		ccb->ccb_h.status = CAM_REQ_CMP;
	} else {
		if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
			xpt_freeze_devq(ccb->ccb_h.path, 1);
			ISCSI_SESSION_DEBUG(is, "freezing devq");
		}
		ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR | CAM_DEV_QFRZN;
		csio->scsi_status = bhsdi->bhsdi_status;
	}

	if ((csio->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN) {
		KASSERT(received <= csio->dxfer_len,
		    ("received > csio->dxfer_len"));
		if (received < csio->dxfer_len) {
			csio->resid = ntohl(bhsdi->bhsdi_residual_count);
			if (csio->resid != csio->dxfer_len - received) {
				ISCSI_SESSION_WARN(is, "underflow mismatch: "
				    "target indicates %d, we calculated %zd",
				    csio->resid, csio->dxfer_len - received);
			}
			csio->resid = csio->dxfer_len - received;
		}
	}

	xpt_done(ccb);
	icl_pdu_free(response);
}

static void
iscsi_pdu_handle_logout_response(struct icl_pdu *response)
{

	ISCSI_SESSION_DEBUG(PDU_SESSION(response), "logout response");
	icl_pdu_free(response);
}

static void
iscsi_pdu_handle_r2t(struct icl_pdu *response)
{
	struct icl_pdu *request;
	struct iscsi_session *is;
	struct iscsi_bhs_r2t *bhsr2t;
	struct iscsi_bhs_data_out *bhsdo;
	struct iscsi_outstanding *io;
	struct ccb_scsiio *csio;
	size_t off, len, total_len;
	int error;

	is = PDU_SESSION(response);

	bhsr2t = (struct iscsi_bhs_r2t *)response->ip_bhs;
	io = iscsi_outstanding_find(is, bhsr2t->bhsr2t_initiator_task_tag);
	if (io == NULL || io->io_ccb == NULL) {
		ISCSI_SESSION_WARN(is, "bad itt 0x%x; reconnecting",
		    bhsr2t->bhsr2t_initiator_task_tag);
		icl_pdu_free(response);
		iscsi_session_reconnect(is);
		return;
	}

	csio = &io->io_ccb->csio;

	if ((csio->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_OUT) {
		ISCSI_SESSION_WARN(is, "received R2T for read command; reconnecting");
		icl_pdu_free(response);
		iscsi_session_reconnect(is);
		return;
	}

	/*
	 * XXX: Verify R2TSN.
	 */

	io->io_datasn = 0;

	off = ntohl(bhsr2t->bhsr2t_buffer_offset);
	if (off > csio->dxfer_len) {
		ISCSI_SESSION_WARN(is, "target requested invalid offset "
		    "%zd, buffer is is %d; reconnecting", off, csio->dxfer_len);
		icl_pdu_free(response);
		iscsi_session_reconnect(is);
		return;
	}

	total_len = ntohl(bhsr2t->bhsr2t_desired_data_transfer_length);
	if (total_len == 0 || total_len > csio->dxfer_len) {
		ISCSI_SESSION_WARN(is, "target requested invalid length "
		    "%zd, buffer is %d; reconnecting", total_len, csio->dxfer_len);
		icl_pdu_free(response);
		iscsi_session_reconnect(is);
		return;
	}

	//ISCSI_SESSION_DEBUG(is, "r2t; off %zd, len %zd", off, total_len);

	for (;;) {
		len = total_len;

		if (len > is->is_max_send_data_segment_length)
			len = is->is_max_send_data_segment_length;

		if (off + len > csio->dxfer_len) {
			ISCSI_SESSION_WARN(is, "target requested invalid "
			    "length/offset %zd, buffer is %d; reconnecting",
			    off + len, csio->dxfer_len);
			icl_pdu_free(response);
			iscsi_session_reconnect(is);
			return;
		}

		request = icl_pdu_new(response->ip_conn, M_NOWAIT);
		if (request == NULL) {
			icl_pdu_free(response);
			iscsi_session_reconnect(is);
			return;
		}

		bhsdo = (struct iscsi_bhs_data_out *)request->ip_bhs;
		bhsdo->bhsdo_opcode = ISCSI_BHS_OPCODE_SCSI_DATA_OUT;
		bhsdo->bhsdo_lun = bhsr2t->bhsr2t_lun;
		bhsdo->bhsdo_initiator_task_tag =
		    bhsr2t->bhsr2t_initiator_task_tag;
		bhsdo->bhsdo_target_transfer_tag =
		    bhsr2t->bhsr2t_target_transfer_tag;
		bhsdo->bhsdo_datasn = htonl(io->io_datasn++);
		bhsdo->bhsdo_buffer_offset = htonl(off);
		error = icl_pdu_append_data(request, csio->data_ptr + off, len,
		    M_NOWAIT);
		if (error != 0) {
			ISCSI_SESSION_WARN(is, "failed to allocate memory; "
			    "reconnecting");
			icl_pdu_free(request);
			icl_pdu_free(response);
			iscsi_session_reconnect(is);
			return;
		}

		off += len;
		total_len -= len;

		if (total_len == 0) {
			bhsdo->bhsdo_flags |= BHSDO_FLAGS_F;
			//ISCSI_SESSION_DEBUG(is, "setting F, off %zd", off);
		} else {
			//ISCSI_SESSION_DEBUG(is, "not finished, off %zd", off);
		}

		iscsi_pdu_queue_locked(request);

		if (total_len == 0)
			break;
	}

	icl_pdu_free(response);
}

static void
iscsi_pdu_handle_async_message(struct icl_pdu *response)
{
	struct iscsi_bhs_asynchronous_message *bhsam;
	struct iscsi_session *is;

	is = PDU_SESSION(response);
	bhsam = (struct iscsi_bhs_asynchronous_message *)response->ip_bhs;
	switch (bhsam->bhsam_async_event) {
	case BHSAM_EVENT_TARGET_REQUESTS_LOGOUT:
		ISCSI_SESSION_WARN(is, "target requests logout; removing session");
		iscsi_session_logout(is);
		iscsi_session_terminate(is);
		break;
	case BHSAM_EVENT_TARGET_TERMINATES_CONNECTION:
		ISCSI_SESSION_WARN(is, "target indicates it will drop the connection");
		break;
	case BHSAM_EVENT_TARGET_TERMINATES_SESSION:
		ISCSI_SESSION_WARN(is, "target indicates it will drop the session");
		break;
	default:
		/*
		 * XXX: Technically, we're obligated to also handle
		 * 	parameter renegotiation.
		 */
		ISCSI_SESSION_WARN(is, "ignoring AsyncEvent %d", bhsam->bhsam_async_event);
		break;
	}

	icl_pdu_free(response);
}

static void
iscsi_pdu_handle_reject(struct icl_pdu *response)
{
	struct iscsi_bhs_reject *bhsr;
	struct iscsi_session *is;

	is = PDU_SESSION(response);
	bhsr = (struct iscsi_bhs_reject *)response->ip_bhs;
	ISCSI_SESSION_WARN(is, "received Reject PDU, reason 0x%x; protocol error?",
	    bhsr->bhsr_reason);

	icl_pdu_free(response);
}

static int
iscsi_ioctl_daemon_wait(struct iscsi_softc *sc,
    struct iscsi_daemon_request *request)
{
	struct iscsi_session *is;
	struct icl_drv_limits idl;
	int error;

	sx_slock(&sc->sc_lock);
	for (;;) {
		TAILQ_FOREACH(is, &sc->sc_sessions, is_next) {
			ISCSI_SESSION_LOCK(is);
			if (is->is_conf.isc_enable == 0 &&
			    is->is_conf.isc_discovery == 0) {
				ISCSI_SESSION_UNLOCK(is);
				continue;
			}
			if (is->is_waiting_for_iscsid)
				break;
			ISCSI_SESSION_UNLOCK(is);
		}

		if (is == NULL) {
			/*
			 * No session requires attention from iscsid(8); wait.
			 */
			error = cv_wait_sig(&sc->sc_cv, &sc->sc_lock);
			if (error != 0) {
				sx_sunlock(&sc->sc_lock);
				return (error);
			}
			continue;
		}

		is->is_waiting_for_iscsid = false;
		is->is_login_phase = true;
		is->is_reason[0] = '\0';
		ISCSI_SESSION_UNLOCK(is);

		request->idr_session_id = is->is_id;
		memcpy(&request->idr_isid, &is->is_isid,
		    sizeof(request->idr_isid));
		request->idr_tsih = 0;	/* New or reinstated session. */
		memcpy(&request->idr_conf, &is->is_conf,
		    sizeof(request->idr_conf));

		error = icl_limits(is->is_conf.isc_offload,
		    is->is_conf.isc_iser, &idl);
		if (error != 0) {
			ISCSI_SESSION_WARN(is, "icl_limits for offload \"%s\" "
			    "failed with error %d", is->is_conf.isc_offload,
			    error);
			sx_sunlock(&sc->sc_lock);
			return (error);
		}
		request->idr_limits.isl_max_recv_data_segment_length =
		    idl.idl_max_recv_data_segment_length;
		request->idr_limits.isl_max_send_data_segment_length =
		    idl.idl_max_send_data_segment_length;
		request->idr_limits.isl_max_burst_length =
		    idl.idl_max_burst_length;
		request->idr_limits.isl_first_burst_length =
		    idl.idl_first_burst_length;

		sx_sunlock(&sc->sc_lock);
		return (0);
	}
}

static int
iscsi_ioctl_daemon_handoff(struct iscsi_softc *sc,
    struct iscsi_daemon_handoff *handoff)
{
	struct iscsi_session *is;
	struct icl_conn *ic;
	int error;

	sx_slock(&sc->sc_lock);

	/*
	 * Find the session to hand off socket to.
	 */
	TAILQ_FOREACH(is, &sc->sc_sessions, is_next) {
		if (is->is_id == handoff->idh_session_id)
			break;
	}
	if (is == NULL) {
		sx_sunlock(&sc->sc_lock);
		return (ESRCH);
	}
	ISCSI_SESSION_LOCK(is);
	ic = is->is_conn;
	if (is->is_conf.isc_discovery || is->is_terminating) {
		ISCSI_SESSION_UNLOCK(is);
		sx_sunlock(&sc->sc_lock);
		return (EINVAL);
	}
	if (is->is_connected) {
		/*
		 * This might have happened because another iscsid(8)
		 * instance handed off the connection in the meantime.
		 * Just return.
		 */
		ISCSI_SESSION_WARN(is, "handoff on already connected "
		    "session");
		ISCSI_SESSION_UNLOCK(is);
		sx_sunlock(&sc->sc_lock);
		return (EBUSY);
	}

	strlcpy(is->is_target_alias, handoff->idh_target_alias,
	    sizeof(is->is_target_alias));
	is->is_tsih = handoff->idh_tsih;
	is->is_statsn = handoff->idh_statsn;
	is->is_initial_r2t = handoff->idh_initial_r2t;
	is->is_immediate_data = handoff->idh_immediate_data;

	is->is_max_recv_data_segment_length =
	    handoff->idh_max_recv_data_segment_length;
	is->is_max_send_data_segment_length =
	    handoff->idh_max_send_data_segment_length;
	is->is_max_burst_length = handoff->idh_max_burst_length;
	is->is_first_burst_length = handoff->idh_first_burst_length;

	if (handoff->idh_header_digest == ISCSI_DIGEST_CRC32C)
		ic->ic_header_crc32c = true;
	else
		ic->ic_header_crc32c = false;
	if (handoff->idh_data_digest == ISCSI_DIGEST_CRC32C)
		ic->ic_data_crc32c = true;
	else
		ic->ic_data_crc32c = false;
	ic->ic_maxtags = maxtags;

	is->is_cmdsn = 0;
	is->is_expcmdsn = 0;
	is->is_maxcmdsn = 0;
	is->is_waiting_for_iscsid = false;
	is->is_login_phase = false;
	is->is_timeout = 0;
	is->is_connected = true;
	is->is_reason[0] = '\0';

	ISCSI_SESSION_UNLOCK(is);

	/*
	 * If we're going through the proxy, the idh_socket will be 0,
	 * and the ICL module can simply ignore this call.  It can also
	 * use it to determine it's no longer in the Login phase.
	 */
	error = icl_conn_handoff(ic, handoff->idh_socket);
	if (error != 0) {
		sx_sunlock(&sc->sc_lock);
		iscsi_session_terminate(is);
		return (error);
	}

	sx_sunlock(&sc->sc_lock);

	if (is->is_sim != NULL) {
		/*
		 * When reconnecting, there already is SIM allocated for the session.
		 */
		KASSERT(is->is_simq_frozen, ("reconnect without frozen simq"));
		ISCSI_SESSION_LOCK(is);
		ISCSI_SESSION_DEBUG(is, "releasing");
		xpt_release_simq(is->is_sim, 1);
		is->is_simq_frozen = false;
		ISCSI_SESSION_UNLOCK(is);

	} else {
		ISCSI_SESSION_LOCK(is);
		is->is_devq = cam_simq_alloc(ic->ic_maxtags);
		if (is->is_devq == NULL) {
			ISCSI_SESSION_WARN(is, "failed to allocate simq");
			iscsi_session_terminate(is);
			return (ENOMEM);
		}

		is->is_sim = cam_sim_alloc(iscsi_action, iscsi_poll, "iscsi",
		    is, is->is_id /* unit */, &is->is_lock,
		    1, ic->ic_maxtags, is->is_devq);
		if (is->is_sim == NULL) {
			ISCSI_SESSION_UNLOCK(is);
			ISCSI_SESSION_WARN(is, "failed to allocate SIM");
			cam_simq_free(is->is_devq);
			iscsi_session_terminate(is);
			return (ENOMEM);
		}

		error = xpt_bus_register(is->is_sim, NULL, 0);
		if (error != 0) {
			ISCSI_SESSION_UNLOCK(is);
			ISCSI_SESSION_WARN(is, "failed to register bus");
			iscsi_session_terminate(is);
			return (ENOMEM);
		}

		error = xpt_create_path(&is->is_path, /*periph*/NULL,
		    cam_sim_path(is->is_sim), CAM_TARGET_WILDCARD,
		    CAM_LUN_WILDCARD);
		if (error != CAM_REQ_CMP) {
			ISCSI_SESSION_UNLOCK(is);
			ISCSI_SESSION_WARN(is, "failed to create path");
			iscsi_session_terminate(is);
			return (ENOMEM);
		}
		ISCSI_SESSION_UNLOCK(is);
	}

	return (0);
}

static int
iscsi_ioctl_daemon_fail(struct iscsi_softc *sc,
    struct iscsi_daemon_fail *fail)
{
	struct iscsi_session *is;

	sx_slock(&sc->sc_lock);

	TAILQ_FOREACH(is, &sc->sc_sessions, is_next) {
		if (is->is_id == fail->idf_session_id)
			break;
	}
	if (is == NULL) {
		sx_sunlock(&sc->sc_lock);
		return (ESRCH);
	}
	ISCSI_SESSION_LOCK(is);
	ISCSI_SESSION_DEBUG(is, "iscsid(8) failed: %s",
	    fail->idf_reason);
	strlcpy(is->is_reason, fail->idf_reason, sizeof(is->is_reason));
	//is->is_waiting_for_iscsid = false;
	//is->is_login_phase = true;
	//iscsi_session_reconnect(is);
	ISCSI_SESSION_UNLOCK(is);
	sx_sunlock(&sc->sc_lock);

	return (0);
}

#ifdef ICL_KERNEL_PROXY
static int
iscsi_ioctl_daemon_connect(struct iscsi_softc *sc,
    struct iscsi_daemon_connect *idc)
{
	struct iscsi_session *is;
	struct sockaddr *from_sa, *to_sa;
	int error;

	sx_slock(&sc->sc_lock);
	TAILQ_FOREACH(is, &sc->sc_sessions, is_next) {
		if (is->is_id == idc->idc_session_id)
			break;
	}
	if (is == NULL) {
		sx_sunlock(&sc->sc_lock);
		return (ESRCH);
	}
	sx_sunlock(&sc->sc_lock);

	if (idc->idc_from_addrlen > 0) {
		error = getsockaddr(&from_sa, (void *)idc->idc_from_addr, idc->idc_from_addrlen);
		if (error != 0) {
			ISCSI_SESSION_WARN(is,
			    "getsockaddr failed with error %d", error);
			return (error);
		}
	} else {
		from_sa = NULL;
	}
	error = getsockaddr(&to_sa, (void *)idc->idc_to_addr, idc->idc_to_addrlen);
	if (error != 0) {
		ISCSI_SESSION_WARN(is, "getsockaddr failed with error %d",
		    error);
		free(from_sa, M_SONAME);
		return (error);
	}

	ISCSI_SESSION_LOCK(is);
	is->is_statsn = 0;
	is->is_cmdsn = 0;
	is->is_expcmdsn = 0;
	is->is_maxcmdsn = 0;
	is->is_waiting_for_iscsid = false;
	is->is_login_phase = true;
	is->is_timeout = 0;
	ISCSI_SESSION_UNLOCK(is);

	error = icl_conn_connect(is->is_conn, idc->idc_domain,
	    idc->idc_socktype, idc->idc_protocol, from_sa, to_sa);
	free(from_sa, M_SONAME);
	free(to_sa, M_SONAME);

	/*
	 * Digests are always disabled during login phase.
	 */
	is->is_conn->ic_header_crc32c = false;
	is->is_conn->ic_data_crc32c = false;

	return (error);
}

static int
iscsi_ioctl_daemon_send(struct iscsi_softc *sc,
    struct iscsi_daemon_send *ids)
{
	struct iscsi_session *is;
	struct icl_pdu *ip;
	size_t datalen;
	void *data;
	int error;

	sx_slock(&sc->sc_lock);
	TAILQ_FOREACH(is, &sc->sc_sessions, is_next) {
		if (is->is_id == ids->ids_session_id)
			break;
	}
	if (is == NULL) {
		sx_sunlock(&sc->sc_lock);
		return (ESRCH);
	}
	sx_sunlock(&sc->sc_lock);

	if (is->is_login_phase == false)
		return (EBUSY);

	if (is->is_terminating || is->is_reconnecting)
		return (EIO);

	datalen = ids->ids_data_segment_len;
	if (datalen > is->is_max_send_data_segment_length)
		return (EINVAL);
	if (datalen > 0) {
		data = malloc(datalen, M_ISCSI, M_WAITOK);
		error = copyin(ids->ids_data_segment, data, datalen);
		if (error != 0) {
			free(data, M_ISCSI);
			return (error);
		}
	}

	ip = icl_pdu_new(is->is_conn, M_WAITOK);
	memcpy(ip->ip_bhs, ids->ids_bhs, sizeof(*ip->ip_bhs));
	if (datalen > 0) {
		error = icl_pdu_append_data(ip, data, datalen, M_WAITOK);
		KASSERT(error == 0, ("icl_pdu_append_data(..., M_WAITOK) failed"));
		free(data, M_ISCSI);
	}
	iscsi_pdu_queue(ip);

	return (0);
}

static int
iscsi_ioctl_daemon_receive(struct iscsi_softc *sc,
    struct iscsi_daemon_receive *idr)
{
	struct iscsi_session *is;
	struct icl_pdu *ip;
	void *data;
	int error;

	sx_slock(&sc->sc_lock);
	TAILQ_FOREACH(is, &sc->sc_sessions, is_next) {
		if (is->is_id == idr->idr_session_id)
			break;
	}
	if (is == NULL) {
		sx_sunlock(&sc->sc_lock);
		return (ESRCH);
	}
	sx_sunlock(&sc->sc_lock);

	if (is->is_login_phase == false)
		return (EBUSY);

	ISCSI_SESSION_LOCK(is);
	while (is->is_login_pdu == NULL &&
	    is->is_terminating == false &&
	    is->is_reconnecting == false) {
		error = cv_wait_sig(&is->is_login_cv, &is->is_lock);
		if (error != 0) {
			ISCSI_SESSION_UNLOCK(is);
			return (error);
		}
	}
	if (is->is_terminating || is->is_reconnecting) {
		ISCSI_SESSION_UNLOCK(is);
		return (EIO);
	}
	ip = is->is_login_pdu;
	is->is_login_pdu = NULL;
	ISCSI_SESSION_UNLOCK(is);

	if (ip->ip_data_len > idr->idr_data_segment_len) {
		icl_pdu_free(ip);
		return (EMSGSIZE);
	}

	copyout(ip->ip_bhs, idr->idr_bhs, sizeof(*ip->ip_bhs));
	if (ip->ip_data_len > 0) {
		data = malloc(ip->ip_data_len, M_ISCSI, M_WAITOK);
		icl_pdu_get_data(ip, 0, data, ip->ip_data_len);
		copyout(data, idr->idr_data_segment, ip->ip_data_len);
		free(data, M_ISCSI);
	}

	icl_pdu_free(ip);

	return (0);
}
#endif /* ICL_KERNEL_PROXY */

static void
iscsi_sanitize_session_conf(struct iscsi_session_conf *isc)
{
	/*
	 * Just make sure all the fields are null-terminated.
	 *
	 * XXX: This is not particularly secure.  We should
	 * 	create our own conf and then copy in relevant
	 * 	fields.
	 */
	isc->isc_initiator[ISCSI_NAME_LEN - 1] = '\0';
	isc->isc_initiator_addr[ISCSI_ADDR_LEN - 1] = '\0';
	isc->isc_initiator_alias[ISCSI_ALIAS_LEN - 1] = '\0';
	isc->isc_target[ISCSI_NAME_LEN - 1] = '\0';
	isc->isc_target_addr[ISCSI_ADDR_LEN - 1] = '\0';
	isc->isc_user[ISCSI_NAME_LEN - 1] = '\0';
	isc->isc_secret[ISCSI_SECRET_LEN - 1] = '\0';
	isc->isc_mutual_user[ISCSI_NAME_LEN - 1] = '\0';
	isc->isc_mutual_secret[ISCSI_SECRET_LEN - 1] = '\0';
}

static bool
iscsi_valid_session_conf(const struct iscsi_session_conf *isc)
{

	if (isc->isc_initiator[0] == '\0') {
		ISCSI_DEBUG("empty isc_initiator");
		return (false);
	}

	if (isc->isc_target_addr[0] == '\0') {
		ISCSI_DEBUG("empty isc_target_addr");
		return (false);
	}

	if (isc->isc_discovery != 0 && isc->isc_target[0] != 0) {
		ISCSI_DEBUG("non-empty isc_target for discovery session");
		return (false);
	}

	if (isc->isc_discovery == 0 && isc->isc_target[0] == 0) {
		ISCSI_DEBUG("empty isc_target for non-discovery session");
		return (false);
	}

	return (true);
}

static int
iscsi_ioctl_session_add(struct iscsi_softc *sc, struct iscsi_session_add *isa)
{
	struct iscsi_session *is;
	const struct iscsi_session *is2;
	int error;

	iscsi_sanitize_session_conf(&isa->isa_conf);
	if (iscsi_valid_session_conf(&isa->isa_conf) == false)
		return (EINVAL);

	is = malloc(sizeof(*is), M_ISCSI, M_ZERO | M_WAITOK);
	memcpy(&is->is_conf, &isa->isa_conf, sizeof(is->is_conf));

	/*
	 * Set some default values, from RFC 3720, section 12.
	 *
	 * These values are updated by the handoff IOCTL, but are
	 * needed prior to the handoff to support sending the ISER
	 * login PDU.
	 */
	is->is_max_recv_data_segment_length = 8192;
	is->is_max_send_data_segment_length = 8192;
	is->is_max_burst_length = 262144;
	is->is_first_burst_length = 65536;

	sx_xlock(&sc->sc_lock);

	/*
	 * Prevent duplicates.
	 */
	TAILQ_FOREACH(is2, &sc->sc_sessions, is_next) {
		if (!!is->is_conf.isc_discovery !=
		    !!is2->is_conf.isc_discovery)
			continue;

		if (strcmp(is->is_conf.isc_target_addr,
		    is2->is_conf.isc_target_addr) != 0)
			continue;

		if (is->is_conf.isc_discovery == 0 &&
		    strcmp(is->is_conf.isc_target,
		    is2->is_conf.isc_target) != 0)
			continue;

		sx_xunlock(&sc->sc_lock);
		free(is, M_ISCSI);
		return (EBUSY);
	}

	is->is_conn = icl_new_conn(is->is_conf.isc_offload,
	    is->is_conf.isc_iser, "iscsi", &is->is_lock);
	if (is->is_conn == NULL) {
		sx_xunlock(&sc->sc_lock);
		free(is, M_ISCSI);
		return (EINVAL);
	}
	is->is_conn->ic_receive = iscsi_receive_callback;
	is->is_conn->ic_error = iscsi_error_callback;
	is->is_conn->ic_prv0 = is;
	TAILQ_INIT(&is->is_outstanding);
	STAILQ_INIT(&is->is_postponed);
	mtx_init(&is->is_lock, "iscsi_lock", NULL, MTX_DEF);
	cv_init(&is->is_maintenance_cv, "iscsi_mt");
#ifdef ICL_KERNEL_PROXY
	cv_init(&is->is_login_cv, "iscsi_login");
#endif

	is->is_softc = sc;
	sc->sc_last_session_id++;
	is->is_id = sc->sc_last_session_id;
	is->is_isid[0] = 0x80; /* RFC 3720, 10.12.5: 10b, "Random" ISID. */
	arc4rand(&is->is_isid[1], 5, 0);
	is->is_tsih = 0;
	callout_init(&is->is_callout, 1);

	error = kthread_add(iscsi_maintenance_thread, is, NULL, NULL, 0, 0, "iscsimt");
	if (error != 0) {
		ISCSI_SESSION_WARN(is, "kthread_add(9) failed with error %d", error);
		sx_xunlock(&sc->sc_lock);
		return (error);
	}

	callout_reset(&is->is_callout, 1 * hz, iscsi_callout, is);
	TAILQ_INSERT_TAIL(&sc->sc_sessions, is, is_next);

	ISCSI_SESSION_LOCK(is);
	/*
	 * Don't notify iscsid(8) if the session is disabled and it's not
	 * a discovery session,
	 */
	if (is->is_conf.isc_enable == 0 && is->is_conf.isc_discovery == 0) {
		ISCSI_SESSION_UNLOCK(is);
		sx_xunlock(&sc->sc_lock);
		return (0);
	}

	is->is_waiting_for_iscsid = true;
	strlcpy(is->is_reason, "Waiting for iscsid(8)", sizeof(is->is_reason));
	ISCSI_SESSION_UNLOCK(is);
	cv_signal(&sc->sc_cv);
	sx_xunlock(&sc->sc_lock);
	return (0);
}

static bool
iscsi_session_conf_matches(unsigned int id1, const struct iscsi_session_conf *c1,
    unsigned int id2, const struct iscsi_session_conf *c2)
{

	if (id2 != 0 && id2 != id1)
		return (false);
	if (c2->isc_target[0] != '\0' &&
	    strcmp(c1->isc_target, c2->isc_target) != 0)
		return (false);
	if (c2->isc_target_addr[0] != '\0' &&
	    strcmp(c1->isc_target_addr, c2->isc_target_addr) != 0)
		return (false);
	return (true);
}

static int
iscsi_ioctl_session_remove(struct iscsi_softc *sc,
    struct iscsi_session_remove *isr)
{
	struct iscsi_session *is, *tmp;
	bool found = false;

	iscsi_sanitize_session_conf(&isr->isr_conf);

	sx_xlock(&sc->sc_lock);
	TAILQ_FOREACH_SAFE(is, &sc->sc_sessions, is_next, tmp) {
		ISCSI_SESSION_LOCK(is);
		if (iscsi_session_conf_matches(is->is_id, &is->is_conf,
		    isr->isr_session_id, &isr->isr_conf)) {
			found = true;
			iscsi_session_logout(is);
			iscsi_session_terminate(is);
		}
		ISCSI_SESSION_UNLOCK(is);
	}
	sx_xunlock(&sc->sc_lock);

	if (!found)
		return (ESRCH);

	return (0);
}

static int
iscsi_ioctl_session_list(struct iscsi_softc *sc, struct iscsi_session_list *isl)
{
	int error;
	unsigned int i = 0;
	struct iscsi_session *is;
	struct iscsi_session_state iss;

	sx_slock(&sc->sc_lock);
	TAILQ_FOREACH(is, &sc->sc_sessions, is_next) {
		if (i >= isl->isl_nentries) {
			sx_sunlock(&sc->sc_lock);
			return (EMSGSIZE);
		}
		memset(&iss, 0, sizeof(iss));
		memcpy(&iss.iss_conf, &is->is_conf, sizeof(iss.iss_conf));
		iss.iss_id = is->is_id;
		strlcpy(iss.iss_target_alias, is->is_target_alias, sizeof(iss.iss_target_alias));
		strlcpy(iss.iss_reason, is->is_reason, sizeof(iss.iss_reason));
		strlcpy(iss.iss_offload, is->is_conn->ic_offload, sizeof(iss.iss_offload));

		if (is->is_conn->ic_header_crc32c)
			iss.iss_header_digest = ISCSI_DIGEST_CRC32C;
		else
			iss.iss_header_digest = ISCSI_DIGEST_NONE;

		if (is->is_conn->ic_data_crc32c)
			iss.iss_data_digest = ISCSI_DIGEST_CRC32C;
		else
			iss.iss_data_digest = ISCSI_DIGEST_NONE;

		iss.iss_max_send_data_segment_length =
		    is->is_max_send_data_segment_length;
		iss.iss_max_recv_data_segment_length =
		    is->is_max_recv_data_segment_length;
		iss.iss_max_burst_length = is->is_max_burst_length;
		iss.iss_first_burst_length = is->is_first_burst_length;
		iss.iss_immediate_data = is->is_immediate_data;
		iss.iss_connected = is->is_connected;

		error = copyout(&iss, isl->isl_pstates + i, sizeof(iss));
		if (error != 0) {
			sx_sunlock(&sc->sc_lock);
			return (error);
		}
		i++;
	}
	sx_sunlock(&sc->sc_lock);

	isl->isl_nentries = i;

	return (0);
}

static int
iscsi_ioctl_session_modify(struct iscsi_softc *sc,
    struct iscsi_session_modify *ism)
{
	struct iscsi_session *is;
	const struct iscsi_session *is2;

	iscsi_sanitize_session_conf(&ism->ism_conf);
	if (iscsi_valid_session_conf(&ism->ism_conf) == false)
		return (EINVAL);

	sx_xlock(&sc->sc_lock);
	TAILQ_FOREACH(is, &sc->sc_sessions, is_next) {
		ISCSI_SESSION_LOCK(is);
		if (is->is_id == ism->ism_session_id) {
			/* Note that the session remains locked. */
			break;
		}
		ISCSI_SESSION_UNLOCK(is);
	}
	if (is == NULL) {
		sx_xunlock(&sc->sc_lock);
		return (ESRCH);
	}

	/*
	 * Prevent duplicates.
	 */
	TAILQ_FOREACH(is2, &sc->sc_sessions, is_next) {
		if (is == is2)
			continue;

		if (!!ism->ism_conf.isc_discovery !=
		    !!is2->is_conf.isc_discovery)
			continue;

		if (strcmp(ism->ism_conf.isc_target_addr,
		    is2->is_conf.isc_target_addr) != 0)
			continue;

		if (ism->ism_conf.isc_discovery == 0 &&
		    strcmp(ism->ism_conf.isc_target,
		    is2->is_conf.isc_target) != 0)
			continue;

		ISCSI_SESSION_UNLOCK(is);
		sx_xunlock(&sc->sc_lock);
		return (EBUSY);
	}

	sx_xunlock(&sc->sc_lock);

	memcpy(&is->is_conf, &ism->ism_conf, sizeof(is->is_conf));
	ISCSI_SESSION_UNLOCK(is);

	iscsi_session_reconnect(is);

	return (0);
}

static int
iscsi_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int mode,
    struct thread *td)
{
	struct iscsi_softc *sc;

	sc = dev->si_drv1;

	switch (cmd) {
	case ISCSIDWAIT:
		return (iscsi_ioctl_daemon_wait(sc,
		    (struct iscsi_daemon_request *)arg));
	case ISCSIDHANDOFF:
		return (iscsi_ioctl_daemon_handoff(sc,
		    (struct iscsi_daemon_handoff *)arg));
	case ISCSIDFAIL:
		return (iscsi_ioctl_daemon_fail(sc,
		    (struct iscsi_daemon_fail *)arg));
#ifdef ICL_KERNEL_PROXY
	case ISCSIDCONNECT:
		return (iscsi_ioctl_daemon_connect(sc,
		    (struct iscsi_daemon_connect *)arg));
	case ISCSIDSEND:
		return (iscsi_ioctl_daemon_send(sc,
		    (struct iscsi_daemon_send *)arg));
	case ISCSIDRECEIVE:
		return (iscsi_ioctl_daemon_receive(sc,
		    (struct iscsi_daemon_receive *)arg));
#endif /* ICL_KERNEL_PROXY */
	case ISCSISADD:
		return (iscsi_ioctl_session_add(sc,
		    (struct iscsi_session_add *)arg));
	case ISCSISREMOVE:
		return (iscsi_ioctl_session_remove(sc,
		    (struct iscsi_session_remove *)arg));
	case ISCSISLIST:
		return (iscsi_ioctl_session_list(sc,
		    (struct iscsi_session_list *)arg));
	case ISCSISMODIFY:
		return (iscsi_ioctl_session_modify(sc,
		    (struct iscsi_session_modify *)arg));
	default:
		return (EINVAL);
	}
}

static struct iscsi_outstanding *
iscsi_outstanding_find(struct iscsi_session *is, uint32_t initiator_task_tag)
{
	struct iscsi_outstanding *io;

	ISCSI_SESSION_LOCK_ASSERT(is);

	TAILQ_FOREACH(io, &is->is_outstanding, io_next) {
		if (io->io_initiator_task_tag == initiator_task_tag)
			return (io);
	}
	return (NULL);
}

static struct iscsi_outstanding *
iscsi_outstanding_find_ccb(struct iscsi_session *is, union ccb *ccb)
{
	struct iscsi_outstanding *io;

	ISCSI_SESSION_LOCK_ASSERT(is);

	TAILQ_FOREACH(io, &is->is_outstanding, io_next) {
		if (io->io_ccb == ccb)
			return (io);
	}
	return (NULL);
}

static struct iscsi_outstanding *
iscsi_outstanding_add(struct iscsi_session *is, struct icl_pdu *request,
    union ccb *ccb, uint32_t *initiator_task_tagp)
{
	struct iscsi_outstanding *io;
	int error;

	ISCSI_SESSION_LOCK_ASSERT(is);

	io = uma_zalloc(iscsi_outstanding_zone, M_NOWAIT | M_ZERO);
	if (io == NULL) {
		ISCSI_SESSION_WARN(is, "failed to allocate %zd bytes",
		    sizeof(*io));
		return (NULL);
	}

	error = icl_conn_task_setup(is->is_conn, request, &ccb->csio,
	    initiator_task_tagp, &io->io_icl_prv);
	if (error != 0) {
		ISCSI_SESSION_WARN(is,
		    "icl_conn_task_setup() failed with error %d", error);
		uma_zfree(iscsi_outstanding_zone, io);
		return (NULL);
	}

	KASSERT(iscsi_outstanding_find(is, *initiator_task_tagp) == NULL,
	    ("initiator_task_tag 0x%x already added", *initiator_task_tagp));

	io->io_initiator_task_tag = *initiator_task_tagp;
	io->io_ccb = ccb;
	TAILQ_INSERT_TAIL(&is->is_outstanding, io, io_next);
	return (io);
}

static void
iscsi_outstanding_remove(struct iscsi_session *is, struct iscsi_outstanding *io)
{

	ISCSI_SESSION_LOCK_ASSERT(is);

	icl_conn_task_done(is->is_conn, io->io_icl_prv);
	TAILQ_REMOVE(&is->is_outstanding, io, io_next);
	uma_zfree(iscsi_outstanding_zone, io);
}

static void
iscsi_action_abort(struct iscsi_session *is, union ccb *ccb)
{
	struct icl_pdu *request;
	struct iscsi_bhs_task_management_request *bhstmr;
	struct ccb_abort *cab = &ccb->cab;
	struct iscsi_outstanding *io, *aio;
	uint32_t initiator_task_tag;

	ISCSI_SESSION_LOCK_ASSERT(is);

#if 0
	KASSERT(is->is_login_phase == false, ("%s called during Login Phase", __func__));
#else
	if (is->is_login_phase) {
		ccb->ccb_h.status = CAM_REQ_ABORTED;
		xpt_done(ccb);
		return;
	}
#endif

	aio = iscsi_outstanding_find_ccb(is, cab->abort_ccb);
	if (aio == NULL) {
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return;
	}

	request = icl_pdu_new(is->is_conn, M_NOWAIT);
	if (request == NULL) {
		ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
		xpt_done(ccb);
		return;
	}

	initiator_task_tag = is->is_initiator_task_tag++;

	io = iscsi_outstanding_add(is, request, NULL, &initiator_task_tag);
	if (io == NULL) {
		icl_pdu_free(request);
		ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
		xpt_done(ccb);
		return;
	}
	io->io_datasn = aio->io_initiator_task_tag;

	bhstmr = (struct iscsi_bhs_task_management_request *)request->ip_bhs;
	bhstmr->bhstmr_opcode = ISCSI_BHS_OPCODE_TASK_REQUEST;
	bhstmr->bhstmr_function = 0x80 | BHSTMR_FUNCTION_ABORT_TASK;
	bhstmr->bhstmr_lun = htobe64(CAM_EXTLUN_BYTE_SWIZZLE(ccb->ccb_h.target_lun));
	bhstmr->bhstmr_initiator_task_tag = initiator_task_tag;
	bhstmr->bhstmr_referenced_task_tag = aio->io_initiator_task_tag;

	iscsi_pdu_queue_locked(request);
}

static void
iscsi_action_scsiio(struct iscsi_session *is, union ccb *ccb)
{
	struct icl_pdu *request;
	struct iscsi_bhs_scsi_command *bhssc;
	struct ccb_scsiio *csio;
	struct iscsi_outstanding *io;
	size_t len;
	uint32_t initiator_task_tag;
	int error;

	ISCSI_SESSION_LOCK_ASSERT(is);

#if 0
	KASSERT(is->is_login_phase == false, ("%s called during Login Phase", __func__));
#else
	if (is->is_login_phase) {
		ISCSI_SESSION_DEBUG(is, "called during login phase");
		if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
			xpt_freeze_devq(ccb->ccb_h.path, 1);
			ISCSI_SESSION_DEBUG(is, "freezing devq");
		}
		ccb->ccb_h.status = CAM_REQ_ABORTED | CAM_DEV_QFRZN;
		xpt_done(ccb);
		return;
	}
#endif

	request = icl_pdu_new(is->is_conn, M_NOWAIT);
	if (request == NULL) {
		if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
			xpt_freeze_devq(ccb->ccb_h.path, 1);
			ISCSI_SESSION_DEBUG(is, "freezing devq");
		}
		ccb->ccb_h.status = CAM_RESRC_UNAVAIL | CAM_DEV_QFRZN;
		xpt_done(ccb);
		return;
	}

	initiator_task_tag = is->is_initiator_task_tag++;
	io = iscsi_outstanding_add(is, request, ccb, &initiator_task_tag);
	if (io == NULL) {
		icl_pdu_free(request);
		if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
			xpt_freeze_devq(ccb->ccb_h.path, 1);
			ISCSI_SESSION_DEBUG(is, "freezing devq");
		}
		ccb->ccb_h.status = CAM_RESRC_UNAVAIL | CAM_DEV_QFRZN;
		xpt_done(ccb);
		return;
	}

	csio = &ccb->csio;
	bhssc = (struct iscsi_bhs_scsi_command *)request->ip_bhs;
	bhssc->bhssc_opcode = ISCSI_BHS_OPCODE_SCSI_COMMAND;
	bhssc->bhssc_flags |= BHSSC_FLAGS_F;
	switch (csio->ccb_h.flags & CAM_DIR_MASK) {
	case CAM_DIR_IN:
		bhssc->bhssc_flags |= BHSSC_FLAGS_R;
		break;
	case CAM_DIR_OUT:
		bhssc->bhssc_flags |= BHSSC_FLAGS_W;
		break;
	}

	if ((ccb->ccb_h.flags & CAM_TAG_ACTION_VALID) != 0) {
		switch (csio->tag_action) {
		case MSG_HEAD_OF_Q_TAG:
			bhssc->bhssc_flags |= BHSSC_FLAGS_ATTR_HOQ;
			break;
		case MSG_ORDERED_Q_TAG:
			bhssc->bhssc_flags |= BHSSC_FLAGS_ATTR_ORDERED;
			break;
		case MSG_ACA_TASK:
			bhssc->bhssc_flags |= BHSSC_FLAGS_ATTR_ACA;
			break;
		case MSG_SIMPLE_Q_TAG:
		default:
			bhssc->bhssc_flags |= BHSSC_FLAGS_ATTR_SIMPLE;
			break;
		}
	} else
		bhssc->bhssc_flags |= BHSSC_FLAGS_ATTR_UNTAGGED;

	bhssc->bhssc_lun = htobe64(CAM_EXTLUN_BYTE_SWIZZLE(ccb->ccb_h.target_lun));
	bhssc->bhssc_initiator_task_tag = initiator_task_tag;
	bhssc->bhssc_expected_data_transfer_length = htonl(csio->dxfer_len);
	KASSERT(csio->cdb_len <= sizeof(bhssc->bhssc_cdb),
	    ("unsupported CDB size %zd", (size_t)csio->cdb_len));

	if (csio->ccb_h.flags & CAM_CDB_POINTER)
		memcpy(&bhssc->bhssc_cdb, csio->cdb_io.cdb_ptr, csio->cdb_len);
	else
		memcpy(&bhssc->bhssc_cdb, csio->cdb_io.cdb_bytes, csio->cdb_len);

	if (is->is_immediate_data &&
	    (csio->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {
		len = csio->dxfer_len;
		//ISCSI_SESSION_DEBUG(is, "adding %zd of immediate data", len);
		if (len > is->is_first_burst_length) {
			ISCSI_SESSION_DEBUG(is, "len %zd -> %d", len, is->is_first_burst_length);
			len = is->is_first_burst_length;
		}
		if (len > is->is_max_send_data_segment_length) {
			ISCSI_SESSION_DEBUG(is, "len %zd -> %d", len,
			    is->is_max_send_data_segment_length);
			len = is->is_max_send_data_segment_length;
		}

		error = icl_pdu_append_data(request, csio->data_ptr, len, M_NOWAIT);
		if (error != 0) {
			iscsi_outstanding_remove(is, io);
			icl_pdu_free(request);
			if ((ccb->ccb_h.status & CAM_DEV_QFRZN) == 0) {
				xpt_freeze_devq(ccb->ccb_h.path, 1);
				ISCSI_SESSION_DEBUG(is, "freezing devq");
			}
			ccb->ccb_h.status = CAM_RESRC_UNAVAIL | CAM_DEV_QFRZN;
			xpt_done(ccb);
			return;
		}
	}
	iscsi_pdu_queue_locked(request);
}

static void
iscsi_action(struct cam_sim *sim, union ccb *ccb)
{
	struct iscsi_session *is;

	is = cam_sim_softc(sim);

	ISCSI_SESSION_LOCK_ASSERT(is);

	if (is->is_terminating ||
	    (is->is_connected == false && fail_on_disconnection)) {
		ccb->ccb_h.status = CAM_DEV_NOT_THERE;
		xpt_done(ccb);
		return;
	}

	switch (ccb->ccb_h.func_code) {
	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		cpi->version_num = 1;
		cpi->hba_inquiry = PI_TAG_ABLE;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_EXTLUNS;
		/*
		 * XXX: It shouldn't ever be NULL; this could be turned
		 *      into a KASSERT eventually.
		 */
		if (is->is_conn == NULL)
			ISCSI_WARN("NULL conn");
		else if (is->is_conn->ic_unmapped)
			cpi->hba_misc |= PIM_UNMAPPED;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = 0;
		/*
		 * Note that the variable below is only relevant for targets
		 * that don't claim compliance with anything above SPC2, which
		 * means they don't support REPORT_LUNS.
		 */
		cpi->max_lun = 255;
		cpi->initiator_id = ~0;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "iSCSI", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 150000; /* XXX */
		cpi->transport = XPORT_ISCSI;
		cpi->transport_version = 0;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_SPC3;
		cpi->maxio = MAXPHYS;
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	{
		struct ccb_trans_settings	*cts;
		struct ccb_trans_settings_scsi	*scsi;

		cts = &ccb->cts;
		scsi = &cts->proto_specific.scsi;

		cts->protocol = PROTO_SCSI;
		cts->protocol_version = SCSI_REV_SPC3;
		cts->transport = XPORT_ISCSI;
		cts->transport_version = 0;
		scsi->valid = CTS_SCSI_VALID_TQ;
		scsi->flags = CTS_SCSI_FLAGS_TAG_ENB;
		cts->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, /*extended*/1);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
#if 0
	/*
	 * XXX: What's the point?
	 */
	case XPT_RESET_BUS:
	case XPT_TERM_IO:
		ISCSI_SESSION_DEBUG(is, "faking success for reset, abort, or term_io");
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
#endif
	case XPT_ABORT:
		iscsi_action_abort(is, ccb);
		return;
	case XPT_SCSI_IO:
		iscsi_action_scsiio(is, ccb);
		return;
	default:
#if 0
		ISCSI_SESSION_DEBUG(is, "got unsupported code 0x%x", ccb->ccb_h.func_code);
#endif
		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		break;
	}
	xpt_done(ccb);
}

static void
iscsi_poll(struct cam_sim *sim)
{

	KASSERT(0, ("%s: you're not supposed to be here", __func__));
}

static void
iscsi_terminate_sessions(struct iscsi_softc *sc)
{
	struct iscsi_session *is;

	sx_slock(&sc->sc_lock);
	TAILQ_FOREACH(is, &sc->sc_sessions, is_next)
		iscsi_session_terminate(is);
	while(!TAILQ_EMPTY(&sc->sc_sessions)) {
		ISCSI_DEBUG("waiting for sessions to terminate");
		cv_wait(&sc->sc_cv, &sc->sc_lock);
	}
	ISCSI_DEBUG("all sessions terminated");
	sx_sunlock(&sc->sc_lock);
}

static void
iscsi_shutdown_pre(struct iscsi_softc *sc)
{
	struct iscsi_session *is;

	if (!fail_on_shutdown)
		return;

	/*
	 * If we have any sessions waiting for reconnection, request
	 * maintenance thread to fail them immediately instead of waiting
	 * for reconnect timeout.
	 *
	 * This prevents LUNs with mounted filesystems that are supported
	 * by disconnected iSCSI sessions from hanging, however it will
	 * fail all queued BIOs.
	 */
	ISCSI_DEBUG("forcing failing all disconnected sessions due to shutdown");

	fail_on_disconnection = 1;

	sx_slock(&sc->sc_lock);
	TAILQ_FOREACH(is, &sc->sc_sessions, is_next) {
		ISCSI_SESSION_LOCK(is);
		if (!is->is_connected) {
			ISCSI_SESSION_DEBUG(is, "force failing disconnected session early");
			iscsi_session_reconnect(is);
		}
		ISCSI_SESSION_UNLOCK(is);
	}
	sx_sunlock(&sc->sc_lock);
}

static void
iscsi_shutdown_post(struct iscsi_softc *sc)
{

	if (panicstr == NULL) {
		ISCSI_DEBUG("removing all sessions due to shutdown");
		iscsi_terminate_sessions(sc);
	}
}

static int
iscsi_load(void)
{
	int error;

	sc = malloc(sizeof(*sc), M_ISCSI, M_ZERO | M_WAITOK);
	sx_init(&sc->sc_lock, "iscsi");
	TAILQ_INIT(&sc->sc_sessions);
	cv_init(&sc->sc_cv, "iscsi_cv");

	iscsi_outstanding_zone = uma_zcreate("iscsi_outstanding",
	    sizeof(struct iscsi_outstanding), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);

	error = make_dev_p(MAKEDEV_CHECKNAME, &sc->sc_cdev, &iscsi_cdevsw,
	    NULL, UID_ROOT, GID_WHEEL, 0600, "iscsi");
	if (error != 0) {
		ISCSI_WARN("failed to create device node, error %d", error);
		return (error);
	}
	sc->sc_cdev->si_drv1 = sc;

	sc->sc_shutdown_pre_eh = EVENTHANDLER_REGISTER(shutdown_pre_sync,
	    iscsi_shutdown_pre, sc, SHUTDOWN_PRI_FIRST);
	/*
	 * shutdown_post_sync needs to run after filesystem shutdown and before
	 * CAM shutdown - otherwise when rebooting with an iSCSI session that is
	 * disconnected but has outstanding requests, dashutdown() will hang on
	 * cam_periph_runccb().
	 */
	sc->sc_shutdown_post_eh = EVENTHANDLER_REGISTER(shutdown_post_sync,
	    iscsi_shutdown_post, sc, SHUTDOWN_PRI_DEFAULT - 1);

	return (0);
}

static int
iscsi_unload(void)
{

	if (sc->sc_cdev != NULL) {
		ISCSI_DEBUG("removing device node");
		destroy_dev(sc->sc_cdev);
		ISCSI_DEBUG("device node removed");
	}

	if (sc->sc_shutdown_pre_eh != NULL)
		EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->sc_shutdown_pre_eh);
	if (sc->sc_shutdown_post_eh != NULL)
		EVENTHANDLER_DEREGISTER(shutdown_post_sync, sc->sc_shutdown_post_eh);

	iscsi_terminate_sessions(sc);

	uma_zdestroy(iscsi_outstanding_zone);
	sx_destroy(&sc->sc_lock);
	cv_destroy(&sc->sc_cv);
	free(sc, M_ISCSI);
	return (0);
}

static int
iscsi_quiesce(void)
{
	sx_slock(&sc->sc_lock);
	if (!TAILQ_EMPTY(&sc->sc_sessions)) {
		sx_sunlock(&sc->sc_lock);
		return (EBUSY);
	}
	sx_sunlock(&sc->sc_lock);
	return (0);
}

static int
iscsi_modevent(module_t mod, int what, void *arg)
{
	int error;

	switch (what) {
	case MOD_LOAD:
		error = iscsi_load();
		break;
	case MOD_UNLOAD:
		error = iscsi_unload();
		break;
	case MOD_QUIESCE:
		error = iscsi_quiesce();
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

moduledata_t iscsi_data = {
	"iscsi",
	iscsi_modevent,
	0
};

DECLARE_MODULE(iscsi, iscsi_data, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_DEPEND(iscsi, cam, 1, 1, 1);
MODULE_DEPEND(iscsi, icl, 1, 1, 1);
