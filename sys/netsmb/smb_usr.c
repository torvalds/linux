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
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>

#include <sys/iconv.h>

#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_rq.h>
#include <netsmb/smb_subr.h>
#include <netsmb/smb_dev.h>

/*
 * helpers for nsmb device. Can be moved to the smb_dev.c file.
 */
static void smb_usr_vcspec_free(struct smb_vcspec *spec);

static int
smb_usr_vc2spec(struct smbioc_ossn *dp, struct smb_vcspec *spec)
{
	int flags = 0;

	bzero(spec, sizeof(*spec));

#ifdef NETSMB_NO_ANON_USER
	if (dp->ioc_user[0] == 0)
		return EINVAL;
#endif

	if (dp->ioc_server == NULL)
		return EINVAL;
	if (dp->ioc_localcs[0] == 0) {
		SMBERROR("no local charset ?\n");
		return EINVAL;
	}

	spec->sap = smb_memdupin(dp->ioc_server, dp->ioc_svlen);
	if (spec->sap == NULL)
		return ENOMEM;
	if (dp->ioc_local) {
		spec->lap = smb_memdupin(dp->ioc_local, dp->ioc_lolen);
		if (spec->lap == NULL) {
			smb_usr_vcspec_free(spec);
			return ENOMEM;
		}
	}
	spec->srvname = dp->ioc_srvname;
	spec->pass = dp->ioc_password;
	spec->domain = dp->ioc_workgroup;
	spec->username = dp->ioc_user;
	spec->mode = dp->ioc_mode;
	spec->rights = dp->ioc_rights;
	spec->owner = dp->ioc_owner;
	spec->group = dp->ioc_group;
	spec->localcs = dp->ioc_localcs;
	spec->servercs = dp->ioc_servercs;
	if (dp->ioc_opt & SMBVOPT_PRIVATE)
		flags |= SMBV_PRIVATE;
	if (dp->ioc_opt & SMBVOPT_SINGLESHARE)
		flags |= SMBV_PRIVATE | SMBV_SINGLESHARE;
	spec->flags = flags;
	return 0;
}

static void
smb_usr_vcspec_free(struct smb_vcspec *spec)
{
	if (spec->sap)
		smb_memfree(spec->sap);
	if (spec->lap)
		smb_memfree(spec->lap);
}

static int
smb_usr_share2spec(struct smbioc_oshare *dp, struct smb_sharespec *spec)
{
	bzero(spec, sizeof(*spec));
	spec->mode = dp->ioc_mode;
	spec->rights = dp->ioc_rights;
	spec->owner = dp->ioc_owner;
	spec->group = dp->ioc_group;
	spec->name = dp->ioc_share;
	spec->stype = dp->ioc_stype;
	spec->pass = dp->ioc_password;
	return 0;
}

int
smb_usr_lookup(struct smbioc_lookup *dp, struct smb_cred *scred,
	struct smb_vc **vcpp, struct smb_share **sspp)
{
	struct smb_vc *vcp = NULL;
	struct smb_vcspec vspec;			/* XXX */
	struct smb_sharespec sspec, *sspecp = NULL;	/* XXX */
	int error;

	if (dp->ioc_level < SMBL_VC || dp->ioc_level > SMBL_SHARE)
		return EINVAL;
	error = smb_usr_vc2spec(&dp->ioc_ssn, &vspec);
	if (error)
		return error;
	if (dp->ioc_flags & SMBLK_CREATE)
		vspec.flags |= SMBV_CREATE;

	if (dp->ioc_level >= SMBL_SHARE) {
		error = smb_usr_share2spec(&dp->ioc_sh, &sspec);
		if (error)
			goto out;
		sspecp = &sspec;
	}
	error = smb_sm_lookup(&vspec, sspecp, scred, &vcp);
	if (error == 0) {
		*vcpp = vcp;
		*sspp = vspec.ssp;
	}
out:
	smb_usr_vcspec_free(&vspec);
	return error;
}

/*
 * Connect to the resource specified by smbioc_ossn structure.
 * It may either find an existing connection or try to establish a new one.
 * If no errors occurred smb_vc returned locked and referenced.
 */
int
smb_usr_opensession(struct smbioc_ossn *dp, struct smb_cred *scred,
	struct smb_vc **vcpp)
{
	struct smb_vc *vcp = NULL;
	struct smb_vcspec vspec;
	int error;

	error = smb_usr_vc2spec(dp, &vspec);
	if (error)
		return error;
	if (dp->ioc_opt & SMBVOPT_CREATE)
		vspec.flags |= SMBV_CREATE;

	error = smb_sm_lookup(&vspec, NULL, scred, &vcp);
	smb_usr_vcspec_free(&vspec);
	return error;
}

int
smb_usr_openshare(struct smb_vc *vcp, struct smbioc_oshare *dp,
	struct smb_cred *scred, struct smb_share **sspp)
{
	struct smb_share *ssp;
	struct smb_sharespec shspec;
	int error;

	error = smb_usr_share2spec(dp, &shspec);
	if (error)
		return error;
	error = smb_vc_lookupshare(vcp, &shspec, scred, &ssp);
	if (error == 0) {
		*sspp = ssp;
		return 0;
	}
	if ((dp->ioc_opt & SMBSOPT_CREATE) == 0)
		return error;
	error = smb_share_create(vcp, &shspec, scred, &ssp);
	if (error)
		return error;
	error = smb_smb_treeconnect(ssp, scred);
	if (error) {
		smb_share_put(ssp, scred);
	} else
		*sspp = ssp;
	return error;
}

int
smb_usr_simplerequest(struct smb_share *ssp, struct smbioc_rq *dp,
	struct smb_cred *scred)
{
	struct smb_rq *rqp;
	struct mbchain *mbp;
	struct mdchain *mdp;
	u_int8_t wc;
	u_int16_t bc;
	int error;

	switch (dp->ioc_cmd) {
	    case SMB_COM_TRANSACTION2:
	    case SMB_COM_TRANSACTION2_SECONDARY:
	    case SMB_COM_CLOSE_AND_TREE_DISC:
	    case SMB_COM_TREE_CONNECT:
	    case SMB_COM_TREE_DISCONNECT:
	    case SMB_COM_NEGOTIATE:
	    case SMB_COM_SESSION_SETUP_ANDX:
	    case SMB_COM_LOGOFF_ANDX:
	    case SMB_COM_TREE_CONNECT_ANDX:
		return EPERM;
	}
	rqp = malloc(sizeof(struct smb_rq), M_SMBTEMP, M_WAITOK);
	error = smb_rq_init(rqp, SSTOCP(ssp), dp->ioc_cmd, scred);
	if (error) {
		free(rqp, M_SMBTEMP);
		return error;
	}
	mbp = &rqp->sr_rq;
	smb_rq_wstart(rqp);
	error = mb_put_mem(mbp, dp->ioc_twords, dp->ioc_twc * 2, MB_MUSER);
	if (error)
		goto bad;
	smb_rq_wend(rqp);
	smb_rq_bstart(rqp);
	error = mb_put_mem(mbp, dp->ioc_tbytes, dp->ioc_tbc, MB_MUSER);
	if (error)
		goto bad;
	smb_rq_bend(rqp);
	error = smb_rq_simple(rqp);
	if (error)
		goto bad;
	mdp = &rqp->sr_rp;
	md_get_uint8(mdp, &wc);
	dp->ioc_rwc = wc;
	wc *= 2;
	if (wc > dp->ioc_rpbufsz) {
		error = EBADRPC;
		goto bad;
	}
	error = md_get_mem(mdp, dp->ioc_rpbuf, wc, MB_MUSER);
	if (error)
		goto bad;
	md_get_uint16le(mdp, &bc);
	if ((wc + bc) > dp->ioc_rpbufsz) {
		error = EBADRPC;
		goto bad;
	}
	dp->ioc_rbc = bc;
	error = md_get_mem(mdp, dp->ioc_rpbuf + wc, bc, MB_MUSER);
bad:
	dp->ioc_errclass = rqp->sr_errclass;
	dp->ioc_serror = rqp->sr_serror;
	dp->ioc_error = rqp->sr_error;
	smb_rq_done(rqp);
	free(rqp, M_SMBTEMP);
	return error;

}

static int
smb_cpdatain(struct mbchain *mbp, int len, caddr_t data)
{
	int error;

	if (len == 0)
		return 0;
	error = mb_init(mbp);
	if (error)
		return error;
	return mb_put_mem(mbp, data, len, MB_MUSER);
}

int
smb_usr_t2request(struct smb_share *ssp, struct smbioc_t2rq *dp,
	struct smb_cred *scred)
{
	struct smb_t2rq *t2p;
	struct mdchain *mdp;
	int error, len;

	if (dp->ioc_setupcnt > 3)
		return EINVAL;
	t2p = malloc(sizeof(struct smb_t2rq), M_SMBTEMP, M_WAITOK);
	error = smb_t2_init(t2p, SSTOCP(ssp), dp->ioc_setup[0], scred);
	if (error) {
		free(t2p, M_SMBTEMP);
		return error;
	}
	len = t2p->t2_setupcount = dp->ioc_setupcnt;
	if (len > 1)
		t2p->t2_setupdata = dp->ioc_setup; 
	if (dp->ioc_name) {
		t2p->t_name = smb_strdupin(dp->ioc_name, 128);
		if (t2p->t_name == NULL) {
			error = ENOMEM;
			goto bad;
		}
	}
	t2p->t2_maxscount = 0;
	t2p->t2_maxpcount = dp->ioc_rparamcnt;
	t2p->t2_maxdcount = dp->ioc_rdatacnt;
	error = smb_cpdatain(&t2p->t2_tparam, dp->ioc_tparamcnt, dp->ioc_tparam);
	if (error)
		goto bad;
	error = smb_cpdatain(&t2p->t2_tdata, dp->ioc_tdatacnt, dp->ioc_tdata);
	if (error)
		goto bad;
	error = smb_t2_request(t2p);
	if (error)
		goto bad;
	mdp = &t2p->t2_rparam;
	if (mdp->md_top) {
		len = m_fixhdr(mdp->md_top);
		if (len > dp->ioc_rparamcnt) {
			error = EMSGSIZE;
			goto bad;
		}
		dp->ioc_rparamcnt = len;
		error = md_get_mem(mdp, dp->ioc_rparam, len, MB_MUSER);
		if (error)
			goto bad;
	} else
		dp->ioc_rparamcnt = 0;
	mdp = &t2p->t2_rdata;
	if (mdp->md_top) {
		len = m_fixhdr(mdp->md_top);
		if (len > dp->ioc_rdatacnt) {
			error = EMSGSIZE;
			goto bad;
		}
		dp->ioc_rdatacnt = len;
		error = md_get_mem(mdp, dp->ioc_rdata, len, MB_MUSER);
	} else
		dp->ioc_rdatacnt = 0;
bad:
	if (t2p->t_name)
		smb_strfree(t2p->t_name);
	smb_t2_done(t2p);
	free(t2p, M_SMBTEMP);
	return error;
}
