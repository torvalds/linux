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

/*
 * Connection engine.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/sysctl.h>
#include <sys/socketvar.h>

#include <sys/iconv.h>

#include <netsmb/smb.h>
#include <netsmb/smb_subr.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_tran.h>
#include <netsmb/smb_trantcp.h>

static struct smb_connobj smb_vclist;
static int smb_vcnext = 1;	/* next unique id for VC */

SYSCTL_NODE(_net, OID_AUTO, smb, CTLFLAG_RW, NULL, "SMB protocol");

static MALLOC_DEFINE(M_SMBCONN, "smb_conn", "SMB connection");

static void smb_co_init(struct smb_connobj *cp, int level, char *ilockname,
    char *lockname);
static void smb_co_done(struct smb_connobj *cp);
static int  smb_vc_disconnect(struct smb_vc *vcp);
static void smb_vc_free(struct smb_connobj *cp);
static void smb_vc_gone(struct smb_connobj *cp, struct smb_cred *scred);
static smb_co_free_t smb_share_free;
static smb_co_gone_t smb_share_gone;

static int  smb_sysctl_treedump(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_net_smb, OID_AUTO, treedump, CTLFLAG_RD | CTLTYPE_OPAQUE,
	    NULL, 0, smb_sysctl_treedump, "S,treedump", "Requester tree");

int
smb_sm_init(void)
{

	smb_co_init(&smb_vclist, SMBL_SM, "smbsm ilock", "smbsm");
	sx_xlock(&smb_vclist.co_interlock);
	smb_co_unlock(&smb_vclist);
	sx_unlock(&smb_vclist.co_interlock);
	return 0;
}

int
smb_sm_done(void)
{

	/* XXX: hold the mutex */
	if (smb_vclist.co_usecount > 1) {
		SMBERROR("%d connections still active\n", smb_vclist.co_usecount - 1);
		return EBUSY;
	}
	smb_co_done(&smb_vclist);
	return 0;
}

static int
smb_sm_lockvclist(void)
{
	int error;

	sx_xlock(&smb_vclist.co_interlock);
	error = smb_co_lock(&smb_vclist);
	sx_unlock(&smb_vclist.co_interlock);

	return error;
}

static void
smb_sm_unlockvclist(void)
{

	sx_xlock(&smb_vclist.co_interlock);
	smb_co_unlock(&smb_vclist);
	sx_unlock(&smb_vclist.co_interlock);
}

static int
smb_sm_lookupint(struct smb_vcspec *vcspec, struct smb_sharespec *shspec,
	struct smb_cred *scred,	struct smb_vc **vcpp)
{
	struct smb_connobj *scp;
	struct smb_vc *vcp;
	int exact = 1;
	int error;

	vcspec->shspec = shspec;
	error = ENOENT;
	vcp = NULL;
	SMBCO_FOREACH(scp, &smb_vclist) {
		vcp = (struct smb_vc *)scp;
		error = smb_vc_lock(vcp);
		if (error)
			continue;

		if ((vcp->obj.co_flags & SMBV_PRIVATE) ||
		    !CONNADDREQ(vcp->vc_paddr, vcspec->sap) ||
		    strcmp(vcp->vc_username, vcspec->username) != 0)
			goto err1;
		if (vcspec->owner != SMBM_ANY_OWNER) {
			if (vcp->vc_uid != vcspec->owner)
				goto err1;
		} else
			exact = 0;
		if (vcspec->group != SMBM_ANY_GROUP) {
			if (vcp->vc_grp != vcspec->group)
				goto err1;
		} else
			exact = 0;
		if (vcspec->mode & SMBM_EXACT) {
			if (!exact || (vcspec->mode & SMBM_MASK) !=
			    vcp->vc_mode)
				goto err1;
		}
		if (smb_vc_access(vcp, scred, vcspec->mode) != 0)
			goto err1;
		vcspec->ssp = NULL;
		if (shspec) {
			error = (int)smb_vc_lookupshare(vcp, shspec, scred,
			    &vcspec->ssp);
			if (error)
				goto fail;
		}
		error = 0;
		break;
	err1:
		error = 1;
	fail:
		smb_vc_unlock(vcp);
	}
	if (vcp) {
		smb_vc_ref(vcp);
		*vcpp = vcp;
	}
	return (error);
}

int
smb_sm_lookup(struct smb_vcspec *vcspec, struct smb_sharespec *shspec,
	struct smb_cred *scred,	struct smb_vc **vcpp)
{
	struct smb_vc *vcp;
	struct smb_share *ssp = NULL;
	int error;

	*vcpp = vcp = NULL;

	error = smb_sm_lockvclist();
	if (error)
		return error;
	error = smb_sm_lookupint(vcspec, shspec, scred, vcpp);
	if (error == 0 || (vcspec->flags & SMBV_CREATE) == 0) {
		smb_sm_unlockvclist();
		return error;
	}
	error = smb_sm_lookupint(vcspec, NULL, scred, &vcp);
	if (error) {
		error = smb_vc_create(vcspec, scred, &vcp);
		if (error)
			goto out;
		error = smb_vc_connect(vcp, scred);
		if (error)
			goto out;
	}
	if (shspec == NULL)
		goto out;
	error = smb_share_create(vcp, shspec, scred, &ssp);
	if (error)
		goto out;
	error = smb_smb_treeconnect(ssp, scred);
	if (error == 0)
		vcspec->ssp = ssp;
	else
		smb_share_put(ssp, scred);
out:
	smb_sm_unlockvclist();
	if (error == 0)
		*vcpp = vcp;
	else if (vcp) {
		smb_vc_lock(vcp);
		smb_vc_put(vcp, scred);
	}
	return error;
}

/*
 * Common code for connection object
 */
static void
smb_co_init(struct smb_connobj *cp, int level, char *ilockname, char *lockname)
{
	SLIST_INIT(&cp->co_children);
	sx_init_flags(&cp->co_interlock, ilockname, SX_RECURSE);
	cv_init(&cp->co_lock, "smblock");
	cp->co_lockcnt = 0;
	cp->co_locker = NULL;
	cp->co_level = level;
	cp->co_usecount = 1;
	sx_xlock(&cp->co_interlock);
	smb_co_lock(cp);
	sx_unlock(&cp->co_interlock);
}

static void
smb_co_done(struct smb_connobj *cp)
{

	sx_destroy(&cp->co_interlock);
	cv_destroy(&cp->co_lock);
	cp->co_locker = NULL;
	cp->co_flags = 0;
	cp->co_lockcnt = 0;
}

static void
smb_co_gone(struct smb_connobj *cp, struct smb_cred *scred)
{
	struct smb_connobj *parent;

	if (cp->co_gone)
		cp->co_gone(cp, scred);
	parent = cp->co_parent;
	if (parent) {
		sx_xlock(&parent->co_interlock);
		smb_co_lock(parent);
		sx_unlock(&parent->co_interlock);
		SLIST_REMOVE(&parent->co_children, cp, smb_connobj, co_next);
		smb_co_put(parent, scred);
	}
	if (cp->co_free)
		cp->co_free(cp);
}

void
smb_co_ref(struct smb_connobj *cp)
{

	sx_xlock(&cp->co_interlock);
	cp->co_usecount++;
	sx_unlock(&cp->co_interlock);
}

void
smb_co_rele(struct smb_connobj *cp, struct smb_cred *scred)
{

	sx_xlock(&cp->co_interlock);
	smb_co_unlock(cp);
	if (cp->co_usecount > 1) {
		cp->co_usecount--;
		sx_unlock(&cp->co_interlock);
		return;
	}
	if (cp->co_usecount == 0) {
		SMBERROR("negative use_count for object %d", cp->co_level);
		sx_unlock(&cp->co_interlock);
		return;
	}
	cp->co_usecount--;
	cp->co_flags |= SMBO_GONE;
	sx_unlock(&cp->co_interlock);
	smb_co_gone(cp, scred);
}

int
smb_co_get(struct smb_connobj *cp, struct smb_cred *scred)
{
	int error;

	MPASS(sx_xholder(&cp->co_interlock) == curthread);
	cp->co_usecount++;
	error = smb_co_lock(cp);
	if (error) 
		cp->co_usecount--;
	return error;
}

void
smb_co_put(struct smb_connobj *cp, struct smb_cred *scred)
{

	sx_xlock(&cp->co_interlock);
	if (cp->co_usecount > 1) {
		cp->co_usecount--;
	} else if (cp->co_usecount == 1) {
		cp->co_usecount--;
		cp->co_flags |= SMBO_GONE;
	} else {
		SMBERROR("negative usecount");
	}
	smb_co_unlock(cp);
	sx_unlock(&cp->co_interlock);
	if ((cp->co_flags & SMBO_GONE) == 0)
		return;
	smb_co_gone(cp, scred);
}

int
smb_co_lock(struct smb_connobj *cp)
{

	MPASS(sx_xholder(&cp->co_interlock) == curthread); 
	for (;;) {
		if (cp->co_flags & SMBO_GONE)
			return EINVAL;
		if (cp->co_locker == NULL) {
			cp->co_locker = curthread;
			return 0;
		}
		if (cp->co_locker == curthread) {
			cp->co_lockcnt++;
			return 0;
		}
		cv_wait(&cp->co_lock, &cp->co_interlock);
	}
}

void
smb_co_unlock(struct smb_connobj *cp)
{

	MPASS(sx_xholder(&cp->co_interlock) == curthread); 
	MPASS(cp->co_locker == curthread);
	if (cp->co_lockcnt != 0) {
		cp->co_lockcnt--;
		return;
	}
	cp->co_locker = NULL;
	cv_signal(&cp->co_lock);
}

static void
smb_co_addchild(struct smb_connobj *parent, struct smb_connobj *child)
{

	smb_co_ref(parent);
	SLIST_INSERT_HEAD(&parent->co_children, child, co_next);
	child->co_parent = parent;
}

/*
 * Session implementation
 */

int
smb_vc_create(struct smb_vcspec *vcspec,
	struct smb_cred *scred, struct smb_vc **vcpp)
{
	struct smb_vc *vcp;
	struct ucred *cred = scred->scr_cred;
	uid_t uid = vcspec->owner;
	gid_t gid = vcspec->group;
	uid_t realuid = cred->cr_uid;
	char *domain = vcspec->domain;
	int error, isroot;

	isroot = smb_suser(cred) == 0;
	/*
	 * Only superuser can create VCs with different uid and gid
	 */
	if (uid != SMBM_ANY_OWNER && uid != realuid && !isroot)
		return EPERM;
	if (gid != SMBM_ANY_GROUP && !groupmember(gid, cred) && !isroot)
		return EPERM;

	vcp = smb_zmalloc(sizeof(*vcp), M_SMBCONN, M_WAITOK);
	smb_co_init(VCTOCP(vcp), SMBL_VC, "smb_vc ilock", "smb_vc");
	vcp->obj.co_free = smb_vc_free;
	vcp->obj.co_gone = smb_vc_gone;
	vcp->vc_number = smb_vcnext++;
	vcp->vc_timo = SMB_DEFRQTIMO;
	vcp->vc_smbuid = SMB_UID_UNKNOWN;
	vcp->vc_mode = vcspec->rights & SMBM_MASK;
	vcp->obj.co_flags = vcspec->flags & (SMBV_PRIVATE | SMBV_SINGLESHARE);
	vcp->vc_tdesc = &smb_tran_nbtcp_desc;
	vcp->vc_seqno = 0;
	vcp->vc_mackey = NULL;
	vcp->vc_mackeylen = 0;

	if (uid == SMBM_ANY_OWNER)
		uid = realuid;
	if (gid == SMBM_ANY_GROUP)
		gid = cred->cr_groups[0];
	vcp->vc_uid = uid;
	vcp->vc_grp = gid;

	smb_sl_init(&vcp->vc_stlock, "vcstlock");
	error = ENOMEM;

	vcp->vc_paddr = sodupsockaddr(vcspec->sap, M_WAITOK);
	if (vcp->vc_paddr == NULL)
		goto fail;
	vcp->vc_laddr = sodupsockaddr(vcspec->lap, M_WAITOK);
	if (vcp->vc_laddr == NULL)
		goto fail;
	vcp->vc_pass = smb_strdup(vcspec->pass);
	if (vcp->vc_pass == NULL)
		goto fail;
	vcp->vc_domain = smb_strdup((domain && domain[0]) ? domain :
	    "NODOMAIN");
	if (vcp->vc_domain == NULL)
		goto fail;
	vcp->vc_srvname = smb_strdup(vcspec->srvname);
	if (vcp->vc_srvname == NULL)
		goto fail;
	vcp->vc_username = smb_strdup(vcspec->username);
	if (vcp->vc_username == NULL)
		goto fail;
	error = (int)iconv_open("tolower", vcspec->localcs, &vcp->vc_tolower);
	if (error)
		goto fail;
	error = (int)iconv_open("toupper", vcspec->localcs, &vcp->vc_toupper);
	if (error)
		goto fail;
	if (vcspec->servercs[0]) {
		error = (int)iconv_open(vcspec->servercs, vcspec->localcs,
		    &vcp->vc_cp_toserver);
		if (error)
			goto fail;
		error = (int)iconv_open(vcspec->localcs, vcspec->servercs,
		    &vcp->vc_cp_tolocal);
		if (error)
			goto fail;
		vcp->vc_toserver = vcp->vc_cp_toserver;
		vcp->vc_tolocal = vcp->vc_cp_tolocal;
		iconv_add(ENCODING_UNICODE, ENCODING_UNICODE, SMB_UNICODE_NAME);
		iconv_add(ENCODING_UNICODE, SMB_UNICODE_NAME, ENCODING_UNICODE);
		error = (int)iconv_open(SMB_UNICODE_NAME, vcspec->localcs,
		    &vcp->vc_ucs_toserver);
		if (!error) {
			error = (int)iconv_open(vcspec->localcs, SMB_UNICODE_NAME,
			    &vcp->vc_ucs_tolocal);
		}
		if (error) {
			if (vcp->vc_ucs_toserver)
				iconv_close(vcp->vc_ucs_toserver);
			vcp->vc_ucs_toserver = NULL;
			vcp->vc_ucs_tolocal = NULL;
		}
	}
	error = (int)smb_iod_create(vcp);
	if (error)
		goto fail;
	*vcpp = vcp;
	smb_co_addchild(&smb_vclist, VCTOCP(vcp));
	return (0);

 fail:
	smb_vc_put(vcp, scred);
	return (error);
}

static void
smb_vc_free(struct smb_connobj *cp)
{
	struct smb_vc *vcp = CPTOVC(cp);

	if (vcp->vc_iod)
		smb_iod_destroy(vcp->vc_iod);
	SMB_STRFREE(vcp->vc_username);
	SMB_STRFREE(vcp->vc_srvname);
	SMB_STRFREE(vcp->vc_pass);
	SMB_STRFREE(vcp->vc_domain);
	if (vcp->vc_mackey)
		free(vcp->vc_mackey, M_SMBTEMP);
	if (vcp->vc_paddr)
		free(vcp->vc_paddr, M_SONAME);
	if (vcp->vc_laddr)
		free(vcp->vc_laddr, M_SONAME);
	if (vcp->vc_tolower)
		iconv_close(vcp->vc_tolower);
	if (vcp->vc_toupper)
		iconv_close(vcp->vc_toupper);
	if (vcp->vc_tolocal)
		vcp->vc_tolocal = NULL;
	if (vcp->vc_toserver)
		vcp->vc_toserver = NULL;
	if (vcp->vc_cp_tolocal)
		iconv_close(vcp->vc_cp_tolocal);
	if (vcp->vc_cp_toserver)
		iconv_close(vcp->vc_cp_toserver);
	if (vcp->vc_ucs_tolocal)
		iconv_close(vcp->vc_ucs_tolocal);
	if (vcp->vc_ucs_toserver)
		iconv_close(vcp->vc_ucs_toserver);
	smb_co_done(VCTOCP(vcp));
	smb_sl_destroy(&vcp->vc_stlock);
	free(vcp, M_SMBCONN);
}

/*
 * Called when use count of VC dropped to zero.
 */
static void
smb_vc_gone(struct smb_connobj *cp, struct smb_cred *scred)
{
	struct smb_vc *vcp = CPTOVC(cp);

	smb_vc_disconnect(vcp);
}

void
smb_vc_ref(struct smb_vc *vcp)
{
	smb_co_ref(VCTOCP(vcp));
}

void
smb_vc_rele(struct smb_vc *vcp, struct smb_cred *scred)
{
	smb_co_rele(VCTOCP(vcp), scred);
}

int
smb_vc_get(struct smb_vc *vcp, struct smb_cred *scred)
{
	struct smb_connobj *cp;
	int error;

	cp = VCTOCP(vcp);
	sx_xlock(&cp->co_interlock);
	error = smb_co_get(cp, scred);
	sx_unlock(&cp->co_interlock);
	return error;
}

void
smb_vc_put(struct smb_vc *vcp, struct smb_cred *scred)
{
	smb_co_put(VCTOCP(vcp), scred);
}

int
smb_vc_lock(struct smb_vc *vcp)
{
	struct smb_connobj *cp;
	int error;

	cp = VCTOCP(vcp);
	sx_xlock(&cp->co_interlock);
	error = smb_co_lock(cp);
	sx_unlock(&cp->co_interlock);
	return error;
}

void
smb_vc_unlock(struct smb_vc *vcp)
{

	struct smb_connobj *cp;

	cp = VCTOCP(vcp);
	sx_xlock(&cp->co_interlock);
	smb_co_unlock(cp);
	sx_unlock(&cp->co_interlock);
}

int
smb_vc_access(struct smb_vc *vcp, struct smb_cred *scred, mode_t mode)
{
	struct ucred *cred = scred->scr_cred;

	if (smb_suser(cred) == 0 || cred->cr_uid == vcp->vc_uid)
		return 0;
	mode >>= 3;
	if (!groupmember(vcp->vc_grp, cred))
		mode >>= 3;
	return (vcp->vc_mode & mode) == mode ? 0 : EACCES;
}

static int
smb_vc_cmpshare(struct smb_share *ssp, struct smb_sharespec *dp)
{
	int exact = 1;

	if (strcmp(ssp->ss_name, dp->name) != 0)
		return 1;
	if (dp->owner != SMBM_ANY_OWNER) {
		if (ssp->ss_uid != dp->owner)
			return 1;
	} else
		exact = 0;
	if (dp->group != SMBM_ANY_GROUP) {
		if (ssp->ss_grp != dp->group)
			return 1;
	} else
		exact = 0;

	if (dp->mode & SMBM_EXACT) {
		if (!exact)
			return 1;
		return (dp->mode & SMBM_MASK) == ssp->ss_mode ? 0 : 1;
	}
	if (smb_share_access(ssp, dp->scred, dp->mode) != 0)
		return 1;
	return 0;
}

/*
 * Lookup share in the given VC. Share referenced and locked on return.
 * VC expected to be locked on entry and will be left locked on exit.
 */
int
smb_vc_lookupshare(struct smb_vc *vcp, struct smb_sharespec *dp,
	struct smb_cred *scred,	struct smb_share **sspp)
{
	struct smb_connobj *scp = NULL;
	struct smb_share *ssp = NULL;
	int error;

	*sspp = NULL;
	dp->scred = scred;
	SMBCO_FOREACH(scp, VCTOCP(vcp)) {
		ssp = (struct smb_share *)scp;
		error = smb_share_lock(ssp);
		if (error)
			continue;
		if (smb_vc_cmpshare(ssp, dp) == 0)
			break;
		smb_share_unlock(ssp);
	}
	if (ssp) {
		smb_share_ref(ssp);
		*sspp = ssp;
		error = 0;
	} else
		error = ENOENT;
	return error;
}

int
smb_vc_connect(struct smb_vc *vcp, struct smb_cred *scred)
{

	return smb_iod_request(vcp->vc_iod, SMBIOD_EV_CONNECT | SMBIOD_EV_SYNC, NULL);
}

/*
 * Destroy VC to server, invalidate shares linked with it.
 * Transport should be locked on entry.
 */
int
smb_vc_disconnect(struct smb_vc *vcp)
{

	if (vcp->vc_iod != NULL)
		smb_iod_request(vcp->vc_iod, SMBIOD_EV_DISCONNECT |
		    SMBIOD_EV_SYNC, NULL);
	return 0;
}

static char smb_emptypass[] = "";

const char *
smb_vc_getpass(struct smb_vc *vcp)
{
	if (vcp->vc_pass)
		return vcp->vc_pass;
	return smb_emptypass;
}

static int
smb_vc_getinfo(struct smb_vc *vcp, struct smb_vc_info *vip)
{
	bzero(vip, sizeof(struct smb_vc_info));
	vip->itype = SMB_INFO_VC;
	vip->usecount = vcp->obj.co_usecount;
	vip->uid = vcp->vc_uid;
	vip->gid = vcp->vc_grp;
	vip->mode = vcp->vc_mode;
	vip->flags = vcp->obj.co_flags;
	vip->sopt = vcp->vc_sopt;
	vip->iodstate = vcp->vc_iod->iod_state;
	bzero(&vip->sopt.sv_skey, sizeof(vip->sopt.sv_skey));
	snprintf(vip->srvname, sizeof(vip->srvname), "%s", vcp->vc_srvname);
	snprintf(vip->vcname, sizeof(vip->vcname), "%s", vcp->vc_username);
	return 0;
}

u_short
smb_vc_nextmid(struct smb_vc *vcp)
{
	u_short r;
	
	sx_xlock(&vcp->obj.co_interlock);
	r = vcp->vc_mid++;
	sx_unlock(&vcp->obj.co_interlock);
	return r;
}

/*
 * Share implementation
 */
/*
 * Allocate share structure and attach it to the given VC
 * Connection expected to be locked on entry. Share will be returned
 * in locked state.
 */
int
smb_share_create(struct smb_vc *vcp, struct smb_sharespec *shspec,
	struct smb_cred *scred, struct smb_share **sspp)
{
	struct smb_share *ssp;
	struct ucred *cred = scred->scr_cred;
	uid_t realuid = cred->cr_uid;
	uid_t uid = shspec->owner;
	gid_t gid = shspec->group;
	int error, isroot;

	isroot = smb_suser(cred) == 0;
	/*
	 * Only superuser can create shares with different uid and gid
	 */
	if (uid != SMBM_ANY_OWNER && uid != realuid && !isroot)
		return EPERM;
	if (gid != SMBM_ANY_GROUP && !groupmember(gid, cred) && !isroot)
		return EPERM;
	error = smb_vc_lookupshare(vcp, shspec, scred, &ssp);
	if (!error) {
		smb_share_put(ssp, scred);
		return EEXIST;
	}
	if (uid == SMBM_ANY_OWNER)
		uid = realuid;
	if (gid == SMBM_ANY_GROUP)
		gid = cred->cr_groups[0];
	ssp = smb_zmalloc(sizeof(*ssp), M_SMBCONN, M_WAITOK);
	smb_co_init(SSTOCP(ssp), SMBL_SHARE, "smbss ilock", "smbss");
	ssp->obj.co_free = smb_share_free;
	ssp->obj.co_gone = smb_share_gone;
	smb_sl_init(&ssp->ss_stlock, "ssstlock");
	ssp->ss_name = smb_strdup(shspec->name);
	if (shspec->pass && shspec->pass[0])
		ssp->ss_pass = smb_strdup(shspec->pass);
	ssp->ss_type = shspec->stype;
	ssp->ss_tid = SMB_TID_UNKNOWN;
	ssp->ss_uid = uid;
	ssp->ss_grp = gid;
	ssp->ss_mode = shspec->rights & SMBM_MASK;
	smb_co_addchild(VCTOCP(vcp), SSTOCP(ssp));
	*sspp = ssp;
	return 0;
}

static void
smb_share_free(struct smb_connobj *cp)
{
	struct smb_share *ssp = CPTOSS(cp);

	SMB_STRFREE(ssp->ss_name);
	SMB_STRFREE(ssp->ss_pass);
	smb_sl_destroy(&ssp->ss_stlock);
	smb_co_done(SSTOCP(ssp));
	free(ssp, M_SMBCONN);
}

static void
smb_share_gone(struct smb_connobj *cp, struct smb_cred *scred)
{
	struct smb_share *ssp = CPTOSS(cp);

	smb_smb_treedisconnect(ssp, scred);
}

void
smb_share_ref(struct smb_share *ssp)
{
	smb_co_ref(SSTOCP(ssp));
}

void
smb_share_rele(struct smb_share *ssp, struct smb_cred *scred)
{
	smb_co_rele(SSTOCP(ssp), scred);
}

int
smb_share_get(struct smb_share *ssp, struct smb_cred *scred)
{
	struct smb_connobj *cp = SSTOCP(ssp);
	int error;

	sx_xlock(&cp->co_interlock);
	error = smb_co_get(cp, scred);
	sx_unlock(&cp->co_interlock);
	return error;
}

void
smb_share_put(struct smb_share *ssp, struct smb_cred *scred)
{
	
	smb_co_put(SSTOCP(ssp), scred);
}

int
smb_share_lock(struct smb_share *ssp)
{
	struct smb_connobj *cp;
	int error;
	
	cp = SSTOCP(ssp);
	sx_xlock(&cp->co_interlock);
	error = smb_co_lock(cp);
	sx_unlock(&cp->co_interlock);
	return error;
}

void
smb_share_unlock(struct smb_share *ssp)
{
	struct smb_connobj *cp;
	
	cp = SSTOCP(ssp);
	sx_xlock(&cp->co_interlock);
	smb_co_unlock(cp);
	sx_unlock(&cp->co_interlock);
}

int
smb_share_access(struct smb_share *ssp, struct smb_cred *scred, mode_t mode)
{
	struct ucred *cred = scred->scr_cred;

	if (smb_suser(cred) == 0 || cred->cr_uid == ssp->ss_uid)
		return 0;
	mode >>= 3;
	if (!groupmember(ssp->ss_grp, cred))
		mode >>= 3;
	return (ssp->ss_mode & mode) == mode ? 0 : EACCES;
}

void
smb_share_invalidate(struct smb_share *ssp)
{
	ssp->ss_tid = SMB_TID_UNKNOWN;
}

int
smb_share_valid(struct smb_share *ssp)
{
	return ssp->ss_tid != SMB_TID_UNKNOWN &&
	    ssp->ss_vcgenid == SSTOVC(ssp)->vc_genid;
}

const char*
smb_share_getpass(struct smb_share *ssp)
{
	struct smb_vc *vcp;

	if (ssp->ss_pass)
		return ssp->ss_pass;
	vcp = SSTOVC(ssp);
	if (vcp->vc_pass)
		return vcp->vc_pass;
	return smb_emptypass;
}

static int
smb_share_getinfo(struct smb_share *ssp, struct smb_share_info *sip)
{
	bzero(sip, sizeof(struct smb_share_info));
	sip->itype = SMB_INFO_SHARE;
	sip->usecount = ssp->obj.co_usecount;
	sip->tid  = ssp->ss_tid;
	sip->type= ssp->ss_type;
	sip->uid = ssp->ss_uid;
	sip->gid = ssp->ss_grp;
	sip->mode= ssp->ss_mode;
	sip->flags = ssp->obj.co_flags;
	snprintf(sip->sname, sizeof(sip->sname), "%s", ssp->ss_name);
	return 0;
}

/*
 * Dump an entire tree into sysctl call
 */
static int
smb_sysctl_treedump(SYSCTL_HANDLER_ARGS)
{
	struct smb_connobj *scp1, *scp2;
	struct smb_vc *vcp;
	struct smb_share *ssp;
	struct smb_vc_info vci;
	struct smb_share_info ssi;
	int error, itype;

	error = sysctl_wire_old_buffer(req, 0);
	if (error)
		return (error);
	error = smb_sm_lockvclist();
	if (error)
		return error;
	SMBCO_FOREACH(scp1, &smb_vclist) {
		vcp = (struct smb_vc *)scp1;
		error = smb_vc_lock(vcp);
		if (error)
			continue;
		smb_vc_getinfo(vcp, &vci);
		error = SYSCTL_OUT(req, &vci, sizeof(struct smb_vc_info));
		if (error) {
			smb_vc_unlock(vcp);
			break;
		}
		SMBCO_FOREACH(scp2, VCTOCP(vcp)) {
			ssp = (struct smb_share *)scp2;
			error = smb_share_lock(ssp);
			if (error) {
				error = 0;
				continue;
			}
			smb_share_getinfo(ssp, &ssi);
			smb_share_unlock(ssp);
			error = SYSCTL_OUT(req, &ssi, sizeof(struct smb_share_info));
			if (error)
				break;
		}
		smb_vc_unlock(vcp);
		if (error)
			break;
	}
	if (!error) {
		itype = SMB_INFO_NONE;
		error = SYSCTL_OUT(req, &itype, sizeof(itype));
	}
	smb_sm_unlockvclist();
	return error;
}
