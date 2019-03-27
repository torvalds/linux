/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
 * All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
 
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/unistd.h>

#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_rq.h>
#include <netsmb/smb_tran.h>
#include <netsmb/smb_trantcp.h>


#define SMBIOD_SLEEP_TIMO	2
#define	SMBIOD_PING_TIMO	60	/* seconds */

#define	SMB_IOD_EVLOCKPTR(iod)	(&((iod)->iod_evlock))
#define	SMB_IOD_EVLOCK(iod)	smb_sl_lock(&((iod)->iod_evlock))
#define	SMB_IOD_EVUNLOCK(iod)	smb_sl_unlock(&((iod)->iod_evlock))

#define	SMB_IOD_RQLOCKPTR(iod)	(&((iod)->iod_rqlock))
#define	SMB_IOD_RQLOCK(iod)	smb_sl_lock(&((iod)->iod_rqlock))
#define	SMB_IOD_RQUNLOCK(iod)	smb_sl_unlock(&((iod)->iod_rqlock))

#define	smb_iod_wakeup(iod)	wakeup(&(iod)->iod_flags)


static MALLOC_DEFINE(M_SMBIOD, "SMBIOD", "SMB network io daemon");

static int smb_iod_next;

static int  smb_iod_sendall(struct smbiod *iod);
static int  smb_iod_disconnect(struct smbiod *iod);
static void smb_iod_thread(void *);

static __inline void
smb_iod_rqprocessed(struct smb_rq *rqp, int error)
{
	SMBRQ_SLOCK(rqp);
	rqp->sr_lerror = error;
	rqp->sr_rpgen++;
	rqp->sr_state = SMBRQ_NOTIFIED;
	wakeup(&rqp->sr_state);
	SMBRQ_SUNLOCK(rqp);
}

static void
smb_iod_invrq(struct smbiod *iod)
{
	struct smb_rq *rqp;

	/*
	 * Invalidate all outstanding requests for this connection
	 */
	SMB_IOD_RQLOCK(iod);
	TAILQ_FOREACH(rqp, &iod->iod_rqlist, sr_link) {
		rqp->sr_flags |= SMBR_RESTART;
		smb_iod_rqprocessed(rqp, ENOTCONN);
	}
	SMB_IOD_RQUNLOCK(iod);
}

static void
smb_iod_closetran(struct smbiod *iod)
{
	struct smb_vc *vcp = iod->iod_vc;
	struct thread *td = iod->iod_td;

	if (vcp->vc_tdata == NULL)
		return;
	SMB_TRAN_DISCONNECT(vcp, td);
	SMB_TRAN_DONE(vcp, td);
	vcp->vc_tdata = NULL;
}

static void
smb_iod_dead(struct smbiod *iod)
{
	iod->iod_state = SMBIOD_ST_DEAD;
	smb_iod_closetran(iod);
	smb_iod_invrq(iod);
}

static int
smb_iod_connect(struct smbiod *iod)
{
	struct smb_vc *vcp = iod->iod_vc;
	struct thread *td = iod->iod_td;
	int error;

	SMBIODEBUG("%d\n", iod->iod_state);
	switch(iod->iod_state) {
	    case SMBIOD_ST_VCACTIVE:
		SMBERROR("called for already opened connection\n");
		return EISCONN;
	    case SMBIOD_ST_DEAD:
		return ENOTCONN;	/* XXX: last error code ? */
	    default:
		break;
	}
	vcp->vc_genid++;
	error = 0;

	error = (int)SMB_TRAN_CREATE(vcp, td);
	if (error)
		goto fail;
	SMBIODEBUG("tcreate\n");
	if (vcp->vc_laddr) {
		error = (int)SMB_TRAN_BIND(vcp, vcp->vc_laddr, td);
		if (error)
			goto fail;
	}
	SMBIODEBUG("tbind\n");
	error = (int)SMB_TRAN_CONNECT(vcp, vcp->vc_paddr, td);
	if (error)
		goto fail;
	SMB_TRAN_SETPARAM(vcp, SMBTP_SELECTID, &iod->iod_flags);
	iod->iod_state = SMBIOD_ST_TRANACTIVE;
	SMBIODEBUG("tconnect\n");
	/* vcp->vc_mid = 0;*/
	error = (int)smb_smb_negotiate(vcp, &iod->iod_scred);
	if (error)
		goto fail;
	SMBIODEBUG("snegotiate\n");
	error = (int)smb_smb_ssnsetup(vcp, &iod->iod_scred);
	if (error)
		goto fail;
	iod->iod_state = SMBIOD_ST_VCACTIVE;
	SMBIODEBUG("completed\n");
	smb_iod_invrq(iod);
	return (0);

 fail:
	smb_iod_dead(iod);
	return (error);
}

static int
smb_iod_disconnect(struct smbiod *iod)
{
	struct smb_vc *vcp = iod->iod_vc;

	SMBIODEBUG("\n");
	if (iod->iod_state == SMBIOD_ST_VCACTIVE) {
		smb_smb_ssnclose(vcp, &iod->iod_scred);
		iod->iod_state = SMBIOD_ST_TRANACTIVE;
	}
	vcp->vc_smbuid = SMB_UID_UNKNOWN;
	smb_iod_closetran(iod);
	iod->iod_state = SMBIOD_ST_NOTCONN;
	return 0;
}

static int
smb_iod_treeconnect(struct smbiod *iod, struct smb_share *ssp)
{
	int error;

	if (iod->iod_state != SMBIOD_ST_VCACTIVE) {
		if (iod->iod_state != SMBIOD_ST_DEAD)
			return ENOTCONN;
		iod->iod_state = SMBIOD_ST_RECONNECT;
		error = smb_iod_connect(iod);
		if (error)
			return error;
	}
	SMBIODEBUG("tree reconnect\n");
	SMBS_ST_LOCK(ssp);
	ssp->ss_flags |= SMBS_RECONNECTING;
	SMBS_ST_UNLOCK(ssp);
	error = smb_smb_treeconnect(ssp, &iod->iod_scred);
	SMBS_ST_LOCK(ssp);
	ssp->ss_flags &= ~SMBS_RECONNECTING;
	SMBS_ST_UNLOCK(ssp);
	wakeup(&ssp->ss_vcgenid);
	return error;
}

static int
smb_iod_sendrq(struct smbiod *iod, struct smb_rq *rqp)
{
	struct thread *td = iod->iod_td;
	struct smb_vc *vcp = iod->iod_vc;
	struct smb_share *ssp = rqp->sr_share;
	struct mbuf *m;
	int error;

	SMBIODEBUG("iod_state = %d\n", iod->iod_state);
	switch (iod->iod_state) {
	    case SMBIOD_ST_NOTCONN:
		smb_iod_rqprocessed(rqp, ENOTCONN);
		return 0;
	    case SMBIOD_ST_DEAD:
		iod->iod_state = SMBIOD_ST_RECONNECT;
		return 0;
	    case SMBIOD_ST_RECONNECT:
		return 0;
	    default:
		break;
	}
	if (rqp->sr_sendcnt == 0) {
#ifdef movedtoanotherplace
		if (vcp->vc_maxmux != 0 && iod->iod_muxcnt >= vcp->vc_maxmux)
			return 0;
#endif
		le16enc(rqp->sr_rqtid, ssp ? ssp->ss_tid : SMB_TID_UNKNOWN);
		le16enc(rqp->sr_rquid, vcp ? vcp->vc_smbuid : 0);
		mb_fixhdr(&rqp->sr_rq);
		if (vcp->vc_hflags2 & SMB_FLAGS2_SECURITY_SIGNATURE)
			smb_rq_sign(rqp);
	}
	if (rqp->sr_sendcnt++ > 5) {
		rqp->sr_flags |= SMBR_RESTART;
		smb_iod_rqprocessed(rqp, rqp->sr_lerror);
		/*
		 * If all attempts to send a request failed, then
		 * something is seriously hosed.
		 */
		return ENOTCONN;
	}
	SMBSDEBUG("M:%04x, P:%04x, U:%04x, T:%04x\n", rqp->sr_mid, 0, 0, 0);
	m_dumpm(rqp->sr_rq.mb_top);
	m = m_copym(rqp->sr_rq.mb_top, 0, M_COPYALL, M_WAITOK);
	error = rqp->sr_lerror = SMB_TRAN_SEND(vcp, m, td);
	if (error == 0) {
		getnanotime(&rqp->sr_timesent);
		iod->iod_lastrqsent = rqp->sr_timesent;
		rqp->sr_flags |= SMBR_SENT;
		rqp->sr_state = SMBRQ_SENT;
		return 0;
	}
	/*
	 * Check for fatal errors
	 */
	if (SMB_TRAN_FATAL(vcp, error)) {
		/*
		 * No further attempts should be made
		 */
		return ENOTCONN;
	}
	if (smb_rq_intr(rqp))
		smb_iod_rqprocessed(rqp, EINTR);
	return 0;
}

/*
 * Process incoming packets
 */
static int
smb_iod_recvall(struct smbiod *iod)
{
	struct smb_vc *vcp = iod->iod_vc;
	struct thread *td = iod->iod_td;
	struct smb_rq *rqp;
	struct mbuf *m;
	u_char *hp;
	u_short mid;
	int error;

	switch (iod->iod_state) {
	    case SMBIOD_ST_NOTCONN:
	    case SMBIOD_ST_DEAD:
	    case SMBIOD_ST_RECONNECT:
		return 0;
	    default:
		break;
	}
	for (;;) {
		m = NULL;
		error = SMB_TRAN_RECV(vcp, &m, td);
		if (error == EWOULDBLOCK)
			break;
		if (SMB_TRAN_FATAL(vcp, error)) {
			smb_iod_dead(iod);
			break;
		}
		if (error)
			break;
		if (m == NULL) {
			SMBERROR("tran return NULL without error\n");
			error = EPIPE;
			continue;
		}
		m = m_pullup(m, SMB_HDRLEN);
		if (m == NULL)
			continue;	/* wait for a good packet */
		/*
		 * Now we got an entire and possibly invalid SMB packet.
		 * Be careful while parsing it.
		 */
		m_dumpm(m);
		hp = mtod(m, u_char*);
		if (bcmp(hp, SMB_SIGNATURE, SMB_SIGLEN) != 0) {
			m_freem(m);
			continue;
		}
		mid = SMB_HDRMID(hp);
		SMBSDEBUG("mid %04x\n", (u_int)mid);
		SMB_IOD_RQLOCK(iod);
		TAILQ_FOREACH(rqp, &iod->iod_rqlist, sr_link) {
			if (rqp->sr_mid != mid)
				continue;
			SMBRQ_SLOCK(rqp);
			if (rqp->sr_rp.md_top == NULL) {
				md_initm(&rqp->sr_rp, m);
			} else {
				if (rqp->sr_flags & SMBR_MULTIPACKET) {
					md_append_record(&rqp->sr_rp, m);
				} else {
					SMBRQ_SUNLOCK(rqp);
					SMBERROR("duplicate response %d (ignored)\n", mid);
					break;
				}
			}
			SMBRQ_SUNLOCK(rqp);
			smb_iod_rqprocessed(rqp, 0);
			break;
		}
		SMB_IOD_RQUNLOCK(iod);
		if (rqp == NULL) {
			SMBERROR("drop resp with mid %d\n", (u_int)mid);
/*			smb_printrqlist(vcp);*/
			m_freem(m);
		}
	}
	/*
	 * check for interrupts
	 */
	SMB_IOD_RQLOCK(iod);
	TAILQ_FOREACH(rqp, &iod->iod_rqlist, sr_link) {
		if (smb_td_intr(rqp->sr_cred->scr_td)) {
			smb_iod_rqprocessed(rqp, EINTR);
		}
	}
	SMB_IOD_RQUNLOCK(iod);
	return 0;
}

int
smb_iod_request(struct smbiod *iod, int event, void *ident)
{
	struct smbiod_event *evp;
	int error;

	SMBIODEBUG("\n");
	evp = smb_zmalloc(sizeof(*evp), M_SMBIOD, M_WAITOK);
	evp->ev_type = event;
	evp->ev_ident = ident;
	SMB_IOD_EVLOCK(iod);
	STAILQ_INSERT_TAIL(&iod->iod_evlist, evp, ev_link);
	if ((event & SMBIOD_EV_SYNC) == 0) {
		SMB_IOD_EVUNLOCK(iod);
		smb_iod_wakeup(iod);
		return 0;
	}
	smb_iod_wakeup(iod);
	msleep(evp, SMB_IOD_EVLOCKPTR(iod), PWAIT | PDROP, "90evw", 0);
	error = evp->ev_error;
	free(evp, M_SMBIOD);
	return error;
}

/*
 * Place request in the queue.
 * Request from smbiod have a high priority.
 */
int
smb_iod_addrq(struct smb_rq *rqp)
{
	struct smb_vc *vcp = rqp->sr_vc;
	struct smbiod *iod = vcp->vc_iod;
	int error;

	SMBIODEBUG("\n");
	if (rqp->sr_cred->scr_td != NULL &&
	    rqp->sr_cred->scr_td->td_proc == iod->iod_p) {
		rqp->sr_flags |= SMBR_INTERNAL;
		SMB_IOD_RQLOCK(iod);
		TAILQ_INSERT_HEAD(&iod->iod_rqlist, rqp, sr_link);
		SMB_IOD_RQUNLOCK(iod);
		for (;;) {
			if (smb_iod_sendrq(iod, rqp) != 0) {
				smb_iod_dead(iod);
				break;
			}
			/*
			 * we don't need to lock state field here
			 */
			if (rqp->sr_state != SMBRQ_NOTSENT)
				break;
			tsleep(&iod->iod_flags, PWAIT, "90sndw", hz);
		}
		if (rqp->sr_lerror)
			smb_iod_removerq(rqp);
		return rqp->sr_lerror;
	}

	switch (iod->iod_state) {
	    case SMBIOD_ST_NOTCONN:
		return ENOTCONN;
	    case SMBIOD_ST_DEAD:
		error = smb_iod_request(vcp->vc_iod, SMBIOD_EV_CONNECT | SMBIOD_EV_SYNC, NULL);
		if (error)
			return error;
		return EXDEV;
	    default:
		break;
	}

	SMB_IOD_RQLOCK(iod);
	for (;;) {
		if (vcp->vc_maxmux == 0) {
			SMBERROR("maxmux == 0\n");
			break;
		}
		if (iod->iod_muxcnt < vcp->vc_maxmux)
			break;
		iod->iod_muxwant++;
		msleep(&iod->iod_muxwant, SMB_IOD_RQLOCKPTR(iod),
		    PWAIT, "90mux", 0);
	}
	iod->iod_muxcnt++;
	TAILQ_INSERT_TAIL(&iod->iod_rqlist, rqp, sr_link);
	SMB_IOD_RQUNLOCK(iod);
	smb_iod_wakeup(iod);
	return 0;
}

int
smb_iod_removerq(struct smb_rq *rqp)
{
	struct smb_vc *vcp = rqp->sr_vc;
	struct smbiod *iod = vcp->vc_iod;

	SMBIODEBUG("\n");
	if (rqp->sr_flags & SMBR_INTERNAL) {
		SMB_IOD_RQLOCK(iod);
		TAILQ_REMOVE(&iod->iod_rqlist, rqp, sr_link);
		SMB_IOD_RQUNLOCK(iod);
		return 0;
	}
	SMB_IOD_RQLOCK(iod);
	while (rqp->sr_flags & SMBR_XLOCK) {
		rqp->sr_flags |= SMBR_XLOCKWANT;
		msleep(rqp, SMB_IOD_RQLOCKPTR(iod), PWAIT, "90xrm", 0);
	}
	TAILQ_REMOVE(&iod->iod_rqlist, rqp, sr_link);
	iod->iod_muxcnt--;
	if (iod->iod_muxwant) {
		iod->iod_muxwant--;
		wakeup(&iod->iod_muxwant);
	}
	SMB_IOD_RQUNLOCK(iod);
	return 0;
}

int
smb_iod_waitrq(struct smb_rq *rqp)
{
	struct smbiod *iod = rqp->sr_vc->vc_iod;
	int error;

	SMBIODEBUG("\n");
	if (rqp->sr_flags & SMBR_INTERNAL) {
		for (;;) {
			smb_iod_sendall(iod);
			smb_iod_recvall(iod);
			if (rqp->sr_rpgen != rqp->sr_rplast)
				break;
			tsleep(&iod->iod_flags, PWAIT, "90irq", hz);
		}
		smb_iod_removerq(rqp);
		return rqp->sr_lerror;

	}
	SMBRQ_SLOCK(rqp);
	if (rqp->sr_rpgen == rqp->sr_rplast)
		msleep(&rqp->sr_state, SMBRQ_SLOCKPTR(rqp), PWAIT, "90wrq", 0);
	rqp->sr_rplast++;
	SMBRQ_SUNLOCK(rqp);
	error = rqp->sr_lerror;
	if (rqp->sr_flags & SMBR_MULTIPACKET) {
		/*
		 * If request should stay in the list, then reinsert it
		 * at the end of queue so other waiters have chance to concur
		 */
		SMB_IOD_RQLOCK(iod);
		TAILQ_REMOVE(&iod->iod_rqlist, rqp, sr_link);
		TAILQ_INSERT_TAIL(&iod->iod_rqlist, rqp, sr_link);
		SMB_IOD_RQUNLOCK(iod);
	} else
		smb_iod_removerq(rqp);
	return error;
}


static int
smb_iod_sendall(struct smbiod *iod)
{
	struct smb_vc *vcp = iod->iod_vc;
	struct smb_rq *rqp;
	struct timespec ts, tstimeout;
	int herror;

	herror = 0;
	/*
	 * Loop through the list of requests and send them if possible
	 */
	SMB_IOD_RQLOCK(iod);
	TAILQ_FOREACH(rqp, &iod->iod_rqlist, sr_link) {
		switch (rqp->sr_state) {
		    case SMBRQ_NOTSENT:
			rqp->sr_flags |= SMBR_XLOCK;
			SMB_IOD_RQUNLOCK(iod);
			herror = smb_iod_sendrq(iod, rqp);
			SMB_IOD_RQLOCK(iod);
			rqp->sr_flags &= ~SMBR_XLOCK;
			if (rqp->sr_flags & SMBR_XLOCKWANT) {
				rqp->sr_flags &= ~SMBR_XLOCKWANT;
				wakeup(rqp);
			}
			break;
		    case SMBRQ_SENT:
			SMB_TRAN_GETPARAM(vcp, SMBTP_TIMEOUT, &tstimeout);
			timespecadd(&tstimeout, &tstimeout, &tstimeout);
			getnanotime(&ts);
			timespecsub(&ts, &tstimeout, &ts);
			if (timespeccmp(&ts, &rqp->sr_timesent, >)) {
				smb_iod_rqprocessed(rqp, ETIMEDOUT);
			}
			break;
		    default:
			break;
		}
		if (herror)
			break;
	}
	SMB_IOD_RQUNLOCK(iod);
	if (herror == ENOTCONN)
		smb_iod_dead(iod);
	return 0;
}

/*
 * "main" function for smbiod daemon
 */
static __inline void
smb_iod_main(struct smbiod *iod)
{
/*	struct smb_vc *vcp = iod->iod_vc;*/
	struct smbiod_event *evp;
/*	struct timespec tsnow;*/
	int error;

	SMBIODEBUG("\n");
	error = 0;

	/*
	 * Check all interesting events
	 */
	for (;;) {
		SMB_IOD_EVLOCK(iod);
		evp = STAILQ_FIRST(&iod->iod_evlist);
		if (evp == NULL) {
			SMB_IOD_EVUNLOCK(iod);
			break;
		}
		STAILQ_REMOVE_HEAD(&iod->iod_evlist, ev_link);
		evp->ev_type |= SMBIOD_EV_PROCESSING;
		SMB_IOD_EVUNLOCK(iod);
		switch (evp->ev_type & SMBIOD_EV_MASK) {
		    case SMBIOD_EV_CONNECT:
			iod->iod_state = SMBIOD_ST_RECONNECT;
			evp->ev_error = smb_iod_connect(iod);
			break;
		    case SMBIOD_EV_DISCONNECT:
			evp->ev_error = smb_iod_disconnect(iod);
			break;
		    case SMBIOD_EV_TREECONNECT:
			evp->ev_error = smb_iod_treeconnect(iod, evp->ev_ident);
			break;
		    case SMBIOD_EV_SHUTDOWN:
			iod->iod_flags |= SMBIOD_SHUTDOWN;
			break;
		    case SMBIOD_EV_NEWRQ:
			break;
		}
		if (evp->ev_type & SMBIOD_EV_SYNC) {
			SMB_IOD_EVLOCK(iod);
			wakeup(evp);
			SMB_IOD_EVUNLOCK(iod);
		} else
			free(evp, M_SMBIOD);
	}
#if 0
	if (iod->iod_state == SMBIOD_ST_VCACTIVE) {
		getnanotime(&tsnow);
		timespecsub(&tsnow, &iod->iod_pingtimo, &tsnow);
		if (timespeccmp(&tsnow, &iod->iod_lastrqsent, >)) {
			smb_smb_echo(vcp, &iod->iod_scred);
		}
	}
#endif
	smb_iod_sendall(iod);
	smb_iod_recvall(iod);
	return;
}

void
smb_iod_thread(void *arg)
{
	struct smbiod *iod = arg;

	mtx_lock(&Giant);

	/*
	 * Here we assume that the thread structure will be the same
	 * for an entire kthread (kproc, to be more precise) life.
	 */
	iod->iod_td = curthread;
	smb_makescred(&iod->iod_scred, iod->iod_td, NULL);
	while ((iod->iod_flags & SMBIOD_SHUTDOWN) == 0) {
		smb_iod_main(iod);
		SMBIODEBUG("going to sleep for %d ticks\n", iod->iod_sleeptimo);
		if (iod->iod_flags & SMBIOD_SHUTDOWN)
			break;
		tsleep(&iod->iod_flags, PWAIT, "90idle", iod->iod_sleeptimo);
	}

	/* We can now safely destroy the mutexes and free the iod structure. */
	smb_sl_destroy(&iod->iod_rqlock);
	smb_sl_destroy(&iod->iod_evlock);
	free(iod, M_SMBIOD);
	mtx_unlock(&Giant);
	kproc_exit(0);
}

int
smb_iod_create(struct smb_vc *vcp)
{
	struct smbiod *iod;
	int error;

	iod = smb_zmalloc(sizeof(*iod), M_SMBIOD, M_WAITOK);
	iod->iod_id = smb_iod_next++;
	iod->iod_state = SMBIOD_ST_NOTCONN;
	iod->iod_vc = vcp;
	iod->iod_sleeptimo = hz * SMBIOD_SLEEP_TIMO;
	iod->iod_pingtimo.tv_sec = SMBIOD_PING_TIMO;
	getnanotime(&iod->iod_lastrqsent);
	vcp->vc_iod = iod;
	smb_sl_init(&iod->iod_rqlock, "90rql");
	TAILQ_INIT(&iod->iod_rqlist);
	smb_sl_init(&iod->iod_evlock, "90evl");
	STAILQ_INIT(&iod->iod_evlist);
	error = kproc_create(smb_iod_thread, iod, &iod->iod_p,
	    RFNOWAIT, 0, "smbiod%d", iod->iod_id);
	if (error) {
		SMBERROR("can't start smbiod: %d", error);
		vcp->vc_iod = NULL;
		smb_sl_destroy(&iod->iod_rqlock);
		smb_sl_destroy(&iod->iod_evlock);
		free(iod, M_SMBIOD);
		return error;
	}
	return 0;
}

int
smb_iod_destroy(struct smbiod *iod)
{
	smb_iod_request(iod, SMBIOD_EV_SHUTDOWN | SMBIOD_EV_SYNC, NULL);
	return 0;
}

int
smb_iod_init(void)
{
	return 0;
}

int
smb_iod_done(void)
{
	return 0;
}

