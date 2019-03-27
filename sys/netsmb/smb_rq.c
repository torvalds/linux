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
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/mbuf.h>

#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_rq.h>
#include <netsmb/smb_subr.h>
#include <netsmb/smb_tran.h>

static MALLOC_DEFINE(M_SMBRQ, "SMBRQ", "SMB request");

MODULE_DEPEND(netsmb, libmchain, 1, 1, 1);

static int  smb_rq_reply(struct smb_rq *rqp);
static int  smb_rq_enqueue(struct smb_rq *rqp);
static int  smb_rq_getenv(struct smb_connobj *layer,
		struct smb_vc **vcpp, struct smb_share **sspp);
static int  smb_rq_new(struct smb_rq *rqp, u_char cmd);
static int  smb_t2_reply(struct smb_t2rq *t2p);

int
smb_rq_alloc(struct smb_connobj *layer, u_char cmd, struct smb_cred *scred,
	struct smb_rq **rqpp)
{
	struct smb_rq *rqp;
	int error;

	rqp = malloc(sizeof(*rqp), M_SMBRQ, M_WAITOK);
	if (rqp == NULL)
		return ENOMEM;
	error = smb_rq_init(rqp, layer, cmd, scred);
	rqp->sr_flags |= SMBR_ALLOCED;
	if (error) {
		smb_rq_done(rqp);
		return error;
	}
	*rqpp = rqp;
	return 0;
}

static char tzero[12];

int
smb_rq_init(struct smb_rq *rqp, struct smb_connobj *layer, u_char cmd,
	struct smb_cred *scred)
{
	int error;

	bzero(rqp, sizeof(*rqp));
	smb_sl_init(&rqp->sr_slock, "srslock");
	error = smb_rq_getenv(layer, &rqp->sr_vc, &rqp->sr_share);
	if (error)
		return error;
	error = smb_vc_access(rqp->sr_vc, scred, SMBM_EXEC);
	if (error)
		return error;
	if (rqp->sr_share) {
		error = smb_share_access(rqp->sr_share, scred, SMBM_EXEC);
		if (error)
			return error;
	}
	rqp->sr_cred = scred;
	rqp->sr_mid = smb_vc_nextmid(rqp->sr_vc);
	return smb_rq_new(rqp, cmd);
}

static int
smb_rq_new(struct smb_rq *rqp, u_char cmd)
{
	struct smb_vc *vcp = rqp->sr_vc;
	struct mbchain *mbp = &rqp->sr_rq;
	int error;
	u_int16_t flags2;

	rqp->sr_sendcnt = 0;
	mb_done(mbp);
	md_done(&rqp->sr_rp);
	error = mb_init(mbp);
	if (error)
		return error;
	mb_put_mem(mbp, SMB_SIGNATURE, SMB_SIGLEN, MB_MSYSTEM);
	mb_put_uint8(mbp, cmd);
	mb_put_uint32le(mbp, 0);		/* DosError */
	mb_put_uint8(mbp, vcp->vc_hflags);
	flags2 = vcp->vc_hflags2;
	if (cmd == SMB_COM_TRANSACTION || cmd == SMB_COM_TRANSACTION_SECONDARY)
		flags2 &= ~SMB_FLAGS2_UNICODE;
	if (cmd == SMB_COM_NEGOTIATE)
		flags2 &= ~SMB_FLAGS2_SECURITY_SIGNATURE;
	mb_put_uint16le(mbp, flags2);
	if ((flags2 & SMB_FLAGS2_SECURITY_SIGNATURE) == 0) {
		mb_put_mem(mbp, tzero, 12, MB_MSYSTEM);
		rqp->sr_rqsig = NULL;
	} else {
		mb_put_uint16le(mbp, 0 /*scred->sc_p->p_pid >> 16*/);
		rqp->sr_rqsig = (u_int8_t *)mb_reserve(mbp, 8);
		mb_put_uint16le(mbp, 0);
	}
	rqp->sr_rqtid = mb_reserve(mbp, sizeof(u_int16_t));
	mb_put_uint16le(mbp, 1 /*scred->sc_p->p_pid & 0xffff*/);
	rqp->sr_rquid = mb_reserve(mbp, sizeof(u_int16_t));
	mb_put_uint16le(mbp, rqp->sr_mid);
	return 0;
}

void
smb_rq_done(struct smb_rq *rqp)
{
	mb_done(&rqp->sr_rq);
	md_done(&rqp->sr_rp);
	smb_sl_destroy(&rqp->sr_slock);
	if (rqp->sr_flags & SMBR_ALLOCED)
		free(rqp, M_SMBRQ);
}

/*
 * Simple request-reply exchange
 */
int
smb_rq_simple(struct smb_rq *rqp)
{
	struct smb_vc *vcp = rqp->sr_vc;
	int error = EINVAL, i;

	for (i = 0; i < SMB_MAXRCN; i++) {
		rqp->sr_flags &= ~SMBR_RESTART;
		rqp->sr_timo = vcp->vc_timo;
		rqp->sr_state = SMBRQ_NOTSENT;
		error = smb_rq_enqueue(rqp);
		if (error)
			return error;
		error = smb_rq_reply(rqp);
		if (error == 0)
			break;
		if ((rqp->sr_flags & (SMBR_RESTART | SMBR_NORESTART)) != SMBR_RESTART)
			break;
	}
	return error;
}

static int
smb_rq_enqueue(struct smb_rq *rqp)
{
	struct smb_share *ssp = rqp->sr_share;
	int error;

	if (ssp == NULL || rqp->sr_cred == &rqp->sr_vc->vc_iod->iod_scred) {
		return smb_iod_addrq(rqp);
	}
	for (;;) {
		SMBS_ST_LOCK(ssp);
		if (ssp->ss_flags & SMBS_RECONNECTING) {
			msleep(&ssp->ss_vcgenid, SMBS_ST_LOCKPTR(ssp),
			    PWAIT | PDROP, "90trcn", hz);
			if (smb_td_intr(rqp->sr_cred->scr_td))
				return EINTR;
			continue;
		}
		if (smb_share_valid(ssp) || (ssp->ss_flags & SMBS_CONNECTED) == 0) {
			SMBS_ST_UNLOCK(ssp);
		} else {
			SMBS_ST_UNLOCK(ssp);
			error = smb_iod_request(rqp->sr_vc->vc_iod,
			    SMBIOD_EV_TREECONNECT | SMBIOD_EV_SYNC, ssp);
			if (error)
				return error;
		}
		error = smb_iod_addrq(rqp);
		if (error != EXDEV)
			break;
	}
	return error;
}

void
smb_rq_wstart(struct smb_rq *rqp)
{
	rqp->sr_wcount = mb_reserve(&rqp->sr_rq, sizeof(u_int8_t));
	rqp->sr_rq.mb_count = 0;
}

void
smb_rq_wend(struct smb_rq *rqp)
{
	if (rqp->sr_wcount == NULL) {
		SMBERROR("no wcount\n");	/* actually panic */
		return;
	}
	if (rqp->sr_rq.mb_count & 1)
		SMBERROR("odd word count\n");
	*rqp->sr_wcount = rqp->sr_rq.mb_count / 2;
}

void
smb_rq_bstart(struct smb_rq *rqp)
{
	rqp->sr_bcount = mb_reserve(&rqp->sr_rq, sizeof(u_short));
	rqp->sr_rq.mb_count = 0;
}

void
smb_rq_bend(struct smb_rq *rqp)
{
	int bcnt;

	if (rqp->sr_bcount == NULL) {
		SMBERROR("no bcount\n");	/* actually panic */
		return;
	}
	bcnt = rqp->sr_rq.mb_count;
	if (bcnt > 0xffff)
		SMBERROR("byte count too large (%d)\n", bcnt);
	le16enc(rqp->sr_bcount, bcnt);
}

int
smb_rq_intr(struct smb_rq *rqp)
{
	if (rqp->sr_flags & SMBR_INTR)
		return EINTR;
	return smb_td_intr(rqp->sr_cred->scr_td);
}

int
smb_rq_getrequest(struct smb_rq *rqp, struct mbchain **mbpp)
{
	*mbpp = &rqp->sr_rq;
	return 0;
}

int
smb_rq_getreply(struct smb_rq *rqp, struct mdchain **mbpp)
{
	*mbpp = &rqp->sr_rp;
	return 0;
}

static int
smb_rq_getenv(struct smb_connobj *layer,
	struct smb_vc **vcpp, struct smb_share **sspp)
{
	struct smb_vc *vcp = NULL;
	struct smb_share *ssp = NULL;
	struct smb_connobj *cp;
	int error = 0;

	switch (layer->co_level) {
	    case SMBL_VC:
		vcp = CPTOVC(layer);
		if (layer->co_parent == NULL) {
			SMBERROR("zombie VC %s\n", vcp->vc_srvname);
			error = EINVAL;
			break;
		}
		break;
	    case SMBL_SHARE:
		ssp = CPTOSS(layer);
		cp = layer->co_parent;
		if (cp == NULL) {
			SMBERROR("zombie share %s\n", ssp->ss_name);
			error = EINVAL;
			break;
		}
		error = smb_rq_getenv(cp, &vcp, NULL);
		if (error)
			break;
		break;
	    default:
		SMBERROR("invalid layer %d passed\n", layer->co_level);
		error = EINVAL;
	}
	if (vcpp)
		*vcpp = vcp;
	if (sspp)
		*sspp = ssp;
	return error;
}

/*
 * Wait for reply on the request
 */
static int
smb_rq_reply(struct smb_rq *rqp)
{
	struct mdchain *mdp = &rqp->sr_rp;
	u_int32_t tdw;
	u_int8_t tb;
	int error, rperror = 0;

	error = smb_iod_waitrq(rqp);
	if (error)
		return error;
	error = md_get_uint32(mdp, &tdw);
	if (error)
		return error;
	error = md_get_uint8(mdp, &tb);
	if (rqp->sr_vc->vc_hflags2 & SMB_FLAGS2_ERR_STATUS) {
		error = md_get_uint32le(mdp, &rqp->sr_error);
	} else {
		error = md_get_uint8(mdp, &rqp->sr_errclass);
		error = md_get_uint8(mdp, &tb);
		error = md_get_uint16le(mdp, &rqp->sr_serror);
		if (!error)
			rperror = smb_maperror(rqp->sr_errclass, rqp->sr_serror);
	}
	error = md_get_uint8(mdp, &rqp->sr_rpflags);
	error = md_get_uint16le(mdp, &rqp->sr_rpflags2);

	error = md_get_uint32(mdp, &tdw);
	error = md_get_uint32(mdp, &tdw);
	error = md_get_uint32(mdp, &tdw);

	error = md_get_uint16le(mdp, &rqp->sr_rptid);
	error = md_get_uint16le(mdp, &rqp->sr_rppid);
	error = md_get_uint16le(mdp, &rqp->sr_rpuid);
	error = md_get_uint16le(mdp, &rqp->sr_rpmid);

	if (error == 0 &&
	    (rqp->sr_vc->vc_hflags2 & SMB_FLAGS2_SECURITY_SIGNATURE))
		error = smb_rq_verify(rqp);

	SMBSDEBUG("M:%04x, P:%04x, U:%04x, T:%04x, E: %d:%d\n",
	    rqp->sr_rpmid, rqp->sr_rppid, rqp->sr_rpuid, rqp->sr_rptid,
	    rqp->sr_errclass, rqp->sr_serror);
	return error ? error : rperror;
}


#define ALIGN4(a)	(((a) + 3) & ~3)

/*
 * TRANS2 request implementation
 */
int
smb_t2_alloc(struct smb_connobj *layer, u_short setup, struct smb_cred *scred,
	struct smb_t2rq **t2pp)
{
	struct smb_t2rq *t2p;
	int error;

	t2p = malloc(sizeof(*t2p), M_SMBRQ, M_WAITOK);
	if (t2p == NULL)
		return ENOMEM;
	error = smb_t2_init(t2p, layer, setup, scred);
	t2p->t2_flags |= SMBT2_ALLOCED;
	if (error) {
		smb_t2_done(t2p);
		return error;
	}
	*t2pp = t2p;
	return 0;
}

int
smb_t2_init(struct smb_t2rq *t2p, struct smb_connobj *source, u_short setup,
	struct smb_cred *scred)
{
	int error;

	bzero(t2p, sizeof(*t2p));
	t2p->t2_source = source;
	t2p->t2_setupcount = 1;
	t2p->t2_setupdata = t2p->t2_setup;
	t2p->t2_setup[0] = setup;
	t2p->t2_fid = 0xffff;
	t2p->t2_cred = scred;
	error = smb_rq_getenv(source, &t2p->t2_vc, NULL);
	if (error)
		return error;
	return 0;
}

void
smb_t2_done(struct smb_t2rq *t2p)
{
	mb_done(&t2p->t2_tparam);
	mb_done(&t2p->t2_tdata);
	md_done(&t2p->t2_rparam);
	md_done(&t2p->t2_rdata);
	if (t2p->t2_flags & SMBT2_ALLOCED)
		free(t2p, M_SMBRQ);
}

static int
smb_t2_placedata(struct mbuf *mtop, u_int16_t offset, u_int16_t count,
	struct mdchain *mdp)
{
	struct mbuf *m, *m0;
	int len;

	m0 = m_split(mtop, offset, M_WAITOK);
	len = m_length(m0, &m);
	m->m_len -= len - count;
	if (mdp->md_top == NULL) {
		md_initm(mdp, m0);
	} else
		m_cat(mdp->md_top, m0);
	return 0;
}

static int
smb_t2_reply(struct smb_t2rq *t2p)
{
	struct mdchain *mdp;
	struct smb_rq *rqp = t2p->t2_rq;
	int error, totpgot, totdgot;
	u_int16_t totpcount, totdcount, pcount, poff, doff, pdisp, ddisp;
	u_int16_t tmp, bc, dcount;
	u_int8_t wc;

	error = smb_rq_reply(rqp);
	if (error)
		return error;
	if ((t2p->t2_flags & SMBT2_ALLSENT) == 0) {
		/* 
		 * this is an interim response, ignore it.
		 */
		SMBRQ_SLOCK(rqp);
		md_next_record(&rqp->sr_rp);
		SMBRQ_SUNLOCK(rqp);
		return 0;
	}
	/*
	 * Now we have to get all subsequent responses. The CIFS specification
	 * says that they can be disordered which is weird.
	 * TODO: timo
	 */
	totpgot = totdgot = 0;
	totpcount = totdcount = 0xffff;
	mdp = &rqp->sr_rp;
	for (;;) {
		m_dumpm(mdp->md_top);
		if ((error = md_get_uint8(mdp, &wc)) != 0)
			break;
		if (wc < 10) {
			error = ENOENT;
			break;
		}
		if ((error = md_get_uint16le(mdp, &tmp)) != 0)
			break;
		if (totpcount > tmp)
			totpcount = tmp;
		md_get_uint16le(mdp, &tmp);
		if (totdcount > tmp)
			totdcount = tmp;
		if ((error = md_get_uint16le(mdp, &tmp)) != 0 || /* reserved */
		    (error = md_get_uint16le(mdp, &pcount)) != 0 ||
		    (error = md_get_uint16le(mdp, &poff)) != 0 ||
		    (error = md_get_uint16le(mdp, &pdisp)) != 0)
			break;
		if (pcount != 0 && pdisp != totpgot) {
			SMBERROR("Can't handle disordered parameters %d:%d\n",
			    pdisp, totpgot);
			error = EINVAL;
			break;
		}
		if ((error = md_get_uint16le(mdp, &dcount)) != 0 ||
		    (error = md_get_uint16le(mdp, &doff)) != 0 ||
		    (error = md_get_uint16le(mdp, &ddisp)) != 0)
			break;
		if (dcount != 0 && ddisp != totdgot) {
			SMBERROR("Can't handle disordered data\n");
			error = EINVAL;
			break;
		}
		md_get_uint8(mdp, &wc);
		md_get_uint8(mdp, NULL);
		tmp = wc;
		while (tmp--)
			md_get_uint16(mdp, NULL);
		if ((error = md_get_uint16le(mdp, &bc)) != 0)
			break;
/*		tmp = SMB_HDRLEN + 1 + 10 * 2 + 2 * wc + 2;*/
		if (dcount) {
			error = smb_t2_placedata(mdp->md_top, doff, dcount,
			    &t2p->t2_rdata);
			if (error)
				break;
		}
		if (pcount) {
			error = smb_t2_placedata(mdp->md_top, poff, pcount,
			    &t2p->t2_rparam);
			if (error)
				break;
		}
		totpgot += pcount;
		totdgot += dcount;
		if (totpgot >= totpcount && totdgot >= totdcount) {
			error = 0;
			t2p->t2_flags |= SMBT2_ALLRECV;
			break;
		}
		/*
		 * We're done with this reply, look for the next one.
		 */
		SMBRQ_SLOCK(rqp);
		md_next_record(&rqp->sr_rp);
		SMBRQ_SUNLOCK(rqp);
		error = smb_rq_reply(rqp);
		if (error)
			break;
	}
	return error;
}

/*
 * Perform a full round of TRANS2 request
 */
static int
smb_t2_request_int(struct smb_t2rq *t2p)
{
	struct smb_vc *vcp = t2p->t2_vc;
	struct smb_cred *scred = t2p->t2_cred;
	struct mbchain *mbp;
	struct mdchain *mdp, mbparam, mbdata;
	struct mbuf *m;
	struct smb_rq *rqp;
	int totpcount, leftpcount, totdcount, leftdcount, len, txmax, i;
	int error, doff, poff, txdcount, txpcount, nmlen;

	m = t2p->t2_tparam.mb_top;
	if (m) {
		md_initm(&mbparam, m);	/* do not free it! */
		totpcount = m_fixhdr(m);
		if (totpcount > 0xffff)		/* maxvalue for u_short */
			return EINVAL;
	} else
		totpcount = 0;
	m = t2p->t2_tdata.mb_top;
	if (m) {
		md_initm(&mbdata, m);	/* do not free it! */
		totdcount =  m_fixhdr(m);
		if (totdcount > 0xffff)
			return EINVAL;
	} else
		totdcount = 0;
	leftdcount = totdcount;
	leftpcount = totpcount;
	txmax = vcp->vc_txmax;
	error = smb_rq_alloc(t2p->t2_source, t2p->t_name ?
	    SMB_COM_TRANSACTION : SMB_COM_TRANSACTION2, scred, &rqp);
	if (error)
		return error;
	rqp->sr_flags |= SMBR_MULTIPACKET;
	t2p->t2_rq = rqp;
	rqp->sr_t2 = t2p;
	mbp = &rqp->sr_rq;
	smb_rq_wstart(rqp);
	mb_put_uint16le(mbp, totpcount);
	mb_put_uint16le(mbp, totdcount);
	mb_put_uint16le(mbp, t2p->t2_maxpcount);
	mb_put_uint16le(mbp, t2p->t2_maxdcount);
	mb_put_uint8(mbp, t2p->t2_maxscount);
	mb_put_uint8(mbp, 0);			/* reserved */
	mb_put_uint16le(mbp, 0);			/* flags */
	mb_put_uint32le(mbp, 0);			/* Timeout */
	mb_put_uint16le(mbp, 0);			/* reserved 2 */
	len = mb_fixhdr(mbp);
	/*
	 * now we have known packet size as
	 * ALIGN4(len + 5 * 2 + setupcount * 2 + 2 + strlen(name) + 1),
	 * and need to decide which parts should go into the first request
	 */
	nmlen = t2p->t_name ? strlen(t2p->t_name) : 0;
	len = ALIGN4(len + 5 * 2 + t2p->t2_setupcount * 2 + 2 + nmlen + 1);
	if (len + leftpcount > txmax) {
		txpcount = min(leftpcount, txmax - len);
		poff = len;
		txdcount = 0;
		doff = 0;
	} else {
		txpcount = leftpcount;
		poff = txpcount ? len : 0;
		len = ALIGN4(len + txpcount);
		txdcount = min(leftdcount, txmax - len);
		doff = txdcount ? len : 0;
	}
	leftpcount -= txpcount;
	leftdcount -= txdcount;
	mb_put_uint16le(mbp, txpcount);
	mb_put_uint16le(mbp, poff);
	mb_put_uint16le(mbp, txdcount);
	mb_put_uint16le(mbp, doff);
	mb_put_uint8(mbp, t2p->t2_setupcount);
	mb_put_uint8(mbp, 0);
	for (i = 0; i < t2p->t2_setupcount; i++)
		mb_put_uint16le(mbp, t2p->t2_setupdata[i]);
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	/* TDUNICODE */
	if (t2p->t_name)
		mb_put_mem(mbp, t2p->t_name, nmlen, MB_MSYSTEM);
	mb_put_uint8(mbp, 0);	/* terminating zero */
	len = mb_fixhdr(mbp);
	if (txpcount) {
		mb_put_mem(mbp, NULL, ALIGN4(len) - len, MB_MZERO);
		error = md_get_mbuf(&mbparam, txpcount, &m);
		SMBSDEBUG("%d:%d:%d\n", error, txpcount, txmax);
		if (error)
			goto freerq;
		mb_put_mbuf(mbp, m);
	}
	len = mb_fixhdr(mbp);
	if (txdcount) {
		mb_put_mem(mbp, NULL, ALIGN4(len) - len, MB_MZERO);
		error = md_get_mbuf(&mbdata, txdcount, &m);
		if (error)
			goto freerq;
		mb_put_mbuf(mbp, m);
	}
	smb_rq_bend(rqp);	/* incredible, but thats it... */
	error = smb_rq_enqueue(rqp);
	if (error)
		goto freerq;
	if (leftpcount == 0 && leftdcount == 0)
		t2p->t2_flags |= SMBT2_ALLSENT;
	error = smb_t2_reply(t2p);
	if (error)
		goto bad;
	while (leftpcount || leftdcount) {
		t2p->t2_flags |= SMBT2_SECONDARY;
		error = smb_rq_new(rqp, t2p->t_name ? 
		    SMB_COM_TRANSACTION_SECONDARY : SMB_COM_TRANSACTION2_SECONDARY);
		if (error)
			goto bad;
		mbp = &rqp->sr_rq;
		smb_rq_wstart(rqp);
		mb_put_uint16le(mbp, totpcount);
		mb_put_uint16le(mbp, totdcount);
		len = mb_fixhdr(mbp);
		/*
		 * now we have known packet size as
		 * ALIGN4(len + 7 * 2 + 2) for T2 request, and -2 for T one,
		 * and need to decide which parts should go into request
		 */
		len = ALIGN4(len + 6 * 2 + 2);
		if (t2p->t_name == NULL)
			len += 2;
		if (len + leftpcount > txmax) {
			txpcount = min(leftpcount, txmax - len);
			poff = len;
			txdcount = 0;
			doff = 0;
		} else {
			txpcount = leftpcount;
			poff = txpcount ? len : 0;
			len = ALIGN4(len + txpcount);
			txdcount = min(leftdcount, txmax - len);
			doff = txdcount ? len : 0;
		}
		mb_put_uint16le(mbp, txpcount);
		mb_put_uint16le(mbp, poff);
		mb_put_uint16le(mbp, totpcount - leftpcount);
		mb_put_uint16le(mbp, txdcount);
		mb_put_uint16le(mbp, doff);
		mb_put_uint16le(mbp, totdcount - leftdcount);
		leftpcount -= txpcount;
		leftdcount -= txdcount;
		if (t2p->t_name == NULL)
			mb_put_uint16le(mbp, t2p->t2_fid);
		smb_rq_wend(rqp);
		smb_rq_bstart(rqp);
		mb_put_uint8(mbp, 0);	/* name */
		len = mb_fixhdr(mbp);
		if (txpcount) {
			mb_put_mem(mbp, NULL, ALIGN4(len) - len, MB_MZERO);
			error = md_get_mbuf(&mbparam, txpcount, &m);
			if (error)
				goto bad;
			mb_put_mbuf(mbp, m);
		}
		len = mb_fixhdr(mbp);
		if (txdcount) {
			mb_put_mem(mbp, NULL, ALIGN4(len) - len, MB_MZERO);
			error = md_get_mbuf(&mbdata, txdcount, &m);
			if (error)
				goto bad;
			mb_put_mbuf(mbp, m);
		}
		smb_rq_bend(rqp);
		rqp->sr_state = SMBRQ_NOTSENT;
		error = smb_iod_request(vcp->vc_iod, SMBIOD_EV_NEWRQ, NULL);
		if (error)
			goto bad;
	}	/* while left params or data */
	t2p->t2_flags |= SMBT2_ALLSENT;
	mdp = &t2p->t2_rdata;
	if (mdp->md_top) {
		m_fixhdr(mdp->md_top);
		md_initm(mdp, mdp->md_top);
	}
	mdp = &t2p->t2_rparam;
	if (mdp->md_top) {
		m_fixhdr(mdp->md_top);
		md_initm(mdp, mdp->md_top);
	}
bad:
	smb_iod_removerq(rqp);
freerq:
	smb_rq_done(rqp);
	if (error) {
		if (rqp->sr_flags & SMBR_RESTART)
			t2p->t2_flags |= SMBT2_RESTART;
		md_done(&t2p->t2_rparam);
		md_done(&t2p->t2_rdata);
	}
	return error;
}

int
smb_t2_request(struct smb_t2rq *t2p)
{
	int error = EINVAL, i;

	for (i = 0; i < SMB_MAXRCN; i++) {
		t2p->t2_flags &= ~SMBR_RESTART;
		error = smb_t2_request_int(t2p);
		if (error == 0)
			break;
		if ((t2p->t2_flags & (SMBT2_RESTART | SMBT2_NORESTART)) != SMBT2_RESTART)
			break;
	}
	return error;
}
