/*
 *  Server-side procedures for NFSv4.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson   <andros@umich.edu>
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
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <linux/fs_struct.h>
#include <linux/file.h>
#include <linux/falloc.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/sunrpc/addr.h>
#include <linux/nfs_ssc.h>

#include "idmap.h"
#include "cache.h"
#include "xdr4.h"
#include "vfs.h"
#include "current_stateid.h"
#include "netns.h"
#include "acl.h"
#include "pnfs.h"
#include "trace.h"

#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
#include <linux/security.h>

static inline void
nfsd4_security_inode_setsecctx(struct svc_fh *resfh, struct xdr_netobj *label, u32 *bmval)
{
	struct inode *inode = d_inode(resfh->fh_dentry);
	int status;

	inode_lock(inode);
	status = security_inode_setsecctx(resfh->fh_dentry,
		label->data, label->len);
	inode_unlock(inode);

	if (status)
		/*
		 * XXX: We should really fail the whole open, but we may
		 * already have created a new file, so it may be too
		 * late.  For now this seems the least of evils:
		 */
		bmval[2] &= ~FATTR4_WORD2_SECURITY_LABEL;

	return;
}
#else
static inline void
nfsd4_security_inode_setsecctx(struct svc_fh *resfh, struct xdr_netobj *label, u32 *bmval)
{ }
#endif

#define NFSDDBG_FACILITY		NFSDDBG_PROC

static u32 nfsd_attrmask[] = {
	NFSD_WRITEABLE_ATTRS_WORD0,
	NFSD_WRITEABLE_ATTRS_WORD1,
	NFSD_WRITEABLE_ATTRS_WORD2
};

static u32 nfsd41_ex_attrmask[] = {
	NFSD_SUPPATTR_EXCLCREAT_WORD0,
	NFSD_SUPPATTR_EXCLCREAT_WORD1,
	NFSD_SUPPATTR_EXCLCREAT_WORD2
};

static __be32
check_attr_support(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		   u32 *bmval, u32 *writable)
{
	struct dentry *dentry = cstate->current_fh.fh_dentry;
	struct svc_export *exp = cstate->current_fh.fh_export;

	if (!nfsd_attrs_supported(cstate->minorversion, bmval))
		return nfserr_attrnotsupp;
	if ((bmval[0] & FATTR4_WORD0_ACL) && !IS_POSIXACL(d_inode(dentry)))
		return nfserr_attrnotsupp;
	if ((bmval[2] & FATTR4_WORD2_SECURITY_LABEL) &&
			!(exp->ex_flags & NFSEXP_SECURITY_LABEL))
		return nfserr_attrnotsupp;
	if (writable && !bmval_is_subset(bmval, writable))
		return nfserr_inval;
	if (writable && (bmval[2] & FATTR4_WORD2_MODE_UMASK) &&
			(bmval[1] & FATTR4_WORD1_MODE))
		return nfserr_inval;
	return nfs_ok;
}

static __be32
nfsd4_check_open_attributes(struct svc_rqst *rqstp,
	struct nfsd4_compound_state *cstate, struct nfsd4_open *open)
{
	__be32 status = nfs_ok;

	if (open->op_create == NFS4_OPEN_CREATE) {
		if (open->op_createmode == NFS4_CREATE_UNCHECKED
		    || open->op_createmode == NFS4_CREATE_GUARDED)
			status = check_attr_support(rqstp, cstate,
					open->op_bmval, nfsd_attrmask);
		else if (open->op_createmode == NFS4_CREATE_EXCLUSIVE4_1)
			status = check_attr_support(rqstp, cstate,
					open->op_bmval, nfsd41_ex_attrmask);
	}

	return status;
}

static int
is_create_with_attrs(struct nfsd4_open *open)
{
	return open->op_create == NFS4_OPEN_CREATE
		&& (open->op_createmode == NFS4_CREATE_UNCHECKED
		    || open->op_createmode == NFS4_CREATE_GUARDED
		    || open->op_createmode == NFS4_CREATE_EXCLUSIVE4_1);
}

/*
 * if error occurs when setting the acl, just clear the acl bit
 * in the returned attr bitmap.
 */
static void
do_set_nfs4_acl(struct svc_rqst *rqstp, struct svc_fh *fhp,
		struct nfs4_acl *acl, u32 *bmval)
{
	__be32 status;

	status = nfsd4_set_nfs4_acl(rqstp, fhp, acl);
	if (status)
		/*
		 * We should probably fail the whole open at this point,
		 * but we've already created the file, so it's too late;
		 * So this seems the least of evils:
		 */
		bmval[0] &= ~FATTR4_WORD0_ACL;
}

static inline void
fh_dup2(struct svc_fh *dst, struct svc_fh *src)
{
	fh_put(dst);
	dget(src->fh_dentry);
	if (src->fh_export)
		exp_get(src->fh_export);
	*dst = *src;
}

static __be32
do_open_permission(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open, int accmode)
{
	__be32 status;

	if (open->op_truncate &&
		!(open->op_share_access & NFS4_SHARE_ACCESS_WRITE))
		return nfserr_inval;

	accmode |= NFSD_MAY_READ_IF_EXEC;

	if (open->op_share_access & NFS4_SHARE_ACCESS_READ)
		accmode |= NFSD_MAY_READ;
	if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
		accmode |= (NFSD_MAY_WRITE | NFSD_MAY_TRUNC);
	if (open->op_share_deny & NFS4_SHARE_DENY_READ)
		accmode |= NFSD_MAY_WRITE;

	status = fh_verify(rqstp, current_fh, S_IFREG, accmode);

	return status;
}

static __be32 nfsd_check_obj_isreg(struct svc_fh *fh)
{
	umode_t mode = d_inode(fh->fh_dentry)->i_mode;

	if (S_ISREG(mode))
		return nfs_ok;
	if (S_ISDIR(mode))
		return nfserr_isdir;
	/*
	 * Using err_symlink as our catch-all case may look odd; but
	 * there's no other obvious error for this case in 4.0, and we
	 * happen to know that it will cause the linux v4 client to do
	 * the right thing on attempts to open something other than a
	 * regular file.
	 */
	return nfserr_symlink;
}

static void nfsd4_set_open_owner_reply_cache(struct nfsd4_compound_state *cstate, struct nfsd4_open *open, struct svc_fh *resfh)
{
	if (nfsd4_has_session(cstate))
		return;
	fh_copy_shallow(&open->op_openowner->oo_owner.so_replay.rp_openfh,
			&resfh->fh_handle);
}

static __be32
do_open_lookup(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate, struct nfsd4_open *open, struct svc_fh **resfh)
{
	struct svc_fh *current_fh = &cstate->current_fh;
	int accmode;
	__be32 status;

	*resfh = kmalloc(sizeof(struct svc_fh), GFP_KERNEL);
	if (!*resfh)
		return nfserr_jukebox;
	fh_init(*resfh, NFS4_FHSIZE);
	open->op_truncate = false;

	if (open->op_create) {
		/* FIXME: check session persistence and pnfs flags.
		 * The nfsv4.1 spec requires the following semantics:
		 *
		 * Persistent   | pNFS   | Server REQUIRED | Client Allowed
		 * Reply Cache  | server |                 |
		 * -------------+--------+-----------------+--------------------
		 * no           | no     | EXCLUSIVE4_1    | EXCLUSIVE4_1
		 *              |        |                 | (SHOULD)
		 *              |        | and EXCLUSIVE4  | or EXCLUSIVE4
		 *              |        |                 | (SHOULD NOT)
		 * no           | yes    | EXCLUSIVE4_1    | EXCLUSIVE4_1
		 * yes          | no     | GUARDED4        | GUARDED4
		 * yes          | yes    | GUARDED4        | GUARDED4
		 */

		/*
		 * Note: create modes (UNCHECKED,GUARDED...) are the same
		 * in NFSv4 as in v3 except EXCLUSIVE4_1.
		 */
		current->fs->umask = open->op_umask;
		status = do_nfsd_create(rqstp, current_fh, open->op_fname.data,
					open->op_fname.len, &open->op_iattr,
					*resfh, open->op_createmode,
					(u32 *)open->op_verf.data,
					&open->op_truncate, &open->op_created);
		current->fs->umask = 0;

		if (!status && open->op_label.len)
			nfsd4_security_inode_setsecctx(*resfh, &open->op_label, open->op_bmval);

		/*
		 * Following rfc 3530 14.2.16, and rfc 5661 18.16.4
		 * use the returned bitmask to indicate which attributes
		 * we used to store the verifier:
		 */
		if (nfsd_create_is_exclusive(open->op_createmode) && status == 0)
			open->op_bmval[1] |= (FATTR4_WORD1_TIME_ACCESS |
						FATTR4_WORD1_TIME_MODIFY);
	} else
		/*
		 * Note this may exit with the parent still locked.
		 * We will hold the lock until nfsd4_open's final
		 * lookup, to prevent renames or unlinks until we've had
		 * a chance to an acquire a delegation if appropriate.
		 */
		status = nfsd_lookup(rqstp, current_fh,
				     open->op_fname.data, open->op_fname.len, *resfh);
	if (status)
		goto out;
	status = nfsd_check_obj_isreg(*resfh);
	if (status)
		goto out;

	if (is_create_with_attrs(open) && open->op_acl != NULL)
		do_set_nfs4_acl(rqstp, *resfh, open->op_acl, open->op_bmval);

	nfsd4_set_open_owner_reply_cache(cstate, open, *resfh);
	accmode = NFSD_MAY_NOP;
	if (open->op_created ||
			open->op_claim_type == NFS4_OPEN_CLAIM_DELEGATE_CUR)
		accmode |= NFSD_MAY_OWNER_OVERRIDE;
	status = do_open_permission(rqstp, *resfh, open, accmode);
	set_change_info(&open->op_cinfo, current_fh);
out:
	return status;
}

static __be32
do_open_fhandle(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate, struct nfsd4_open *open)
{
	struct svc_fh *current_fh = &cstate->current_fh;
	__be32 status;
	int accmode = 0;

	/* We don't know the target directory, and therefore can not
	* set the change info
	*/

	memset(&open->op_cinfo, 0, sizeof(struct nfsd4_change_info));

	nfsd4_set_open_owner_reply_cache(cstate, open, current_fh);

	open->op_truncate = (open->op_iattr.ia_valid & ATTR_SIZE) &&
		(open->op_iattr.ia_size == 0);
	/*
	 * In the delegation case, the client is telling us about an
	 * open that it *already* performed locally, some time ago.  We
	 * should let it succeed now if possible.
	 *
	 * In the case of a CLAIM_FH open, on the other hand, the client
	 * may be counting on us to enforce permissions (the Linux 4.1
	 * client uses this for normal opens, for example).
	 */
	if (open->op_claim_type == NFS4_OPEN_CLAIM_DELEG_CUR_FH)
		accmode = NFSD_MAY_OWNER_OVERRIDE;

	status = do_open_permission(rqstp, current_fh, open, accmode);

	return status;
}

static void
copy_clientid(clientid_t *clid, struct nfsd4_session *session)
{
	struct nfsd4_sessionid *sid =
			(struct nfsd4_sessionid *)session->se_sessionid.data;

	clid->cl_boot = sid->clientid.cl_boot;
	clid->cl_id = sid->clientid.cl_id;
}

static __be32
nfsd4_open(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   union nfsd4_op_u *u)
{
	struct nfsd4_open *open = &u->open;
	__be32 status;
	struct svc_fh *resfh = NULL;
	struct net *net = SVC_NET(rqstp);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	bool reclaim = false;

	dprintk("NFSD: nfsd4_open filename %.*s op_openowner %p\n",
		(int)open->op_fname.len, open->op_fname.data,
		open->op_openowner);

	/* This check required by spec. */
	if (open->op_create && open->op_claim_type != NFS4_OPEN_CLAIM_NULL)
		return nfserr_inval;

	open->op_created = false;
	/*
	 * RFC5661 18.51.3
	 * Before RECLAIM_COMPLETE done, server should deny new lock
	 */
	if (nfsd4_has_session(cstate) &&
	    !test_bit(NFSD4_CLIENT_RECLAIM_COMPLETE,
		      &cstate->session->se_client->cl_flags) &&
	    open->op_claim_type != NFS4_OPEN_CLAIM_PREVIOUS)
		return nfserr_grace;

	if (nfsd4_has_session(cstate))
		copy_clientid(&open->op_clientid, cstate->session);

	/* check seqid for replay. set nfs4_owner */
	status = nfsd4_process_open1(cstate, open, nn);
	if (status == nfserr_replay_me) {
		struct nfs4_replay *rp = &open->op_openowner->oo_owner.so_replay;
		fh_put(&cstate->current_fh);
		fh_copy_shallow(&cstate->current_fh.fh_handle,
				&rp->rp_openfh);
		status = fh_verify(rqstp, &cstate->current_fh, 0, NFSD_MAY_NOP);
		if (status)
			dprintk("nfsd4_open: replay failed"
				" restoring previous filehandle\n");
		else
			status = nfserr_replay_me;
	}
	if (status)
		goto out;
	if (open->op_xdr_error) {
		status = open->op_xdr_error;
		goto out;
	}

	status = nfsd4_check_open_attributes(rqstp, cstate, open);
	if (status)
		goto out;

	/* Openowner is now set, so sequence id will get bumped.  Now we need
	 * these checks before we do any creates: */
	status = nfserr_grace;
	if (opens_in_grace(net) && open->op_claim_type != NFS4_OPEN_CLAIM_PREVIOUS)
		goto out;
	status = nfserr_no_grace;
	if (!opens_in_grace(net) && open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS)
		goto out;

	switch (open->op_claim_type) {
		case NFS4_OPEN_CLAIM_DELEGATE_CUR:
		case NFS4_OPEN_CLAIM_NULL:
			status = do_open_lookup(rqstp, cstate, open, &resfh);
			if (status)
				goto out;
			break;
		case NFS4_OPEN_CLAIM_PREVIOUS:
			status = nfs4_check_open_reclaim(&open->op_clientid,
							 cstate, nn);
			if (status)
				goto out;
			open->op_openowner->oo_flags |= NFS4_OO_CONFIRMED;
			reclaim = true;
			fallthrough;
		case NFS4_OPEN_CLAIM_FH:
		case NFS4_OPEN_CLAIM_DELEG_CUR_FH:
			status = do_open_fhandle(rqstp, cstate, open);
			if (status)
				goto out;
			resfh = &cstate->current_fh;
			break;
		case NFS4_OPEN_CLAIM_DELEG_PREV_FH:
             	case NFS4_OPEN_CLAIM_DELEGATE_PREV:
			dprintk("NFSD: unsupported OPEN claim type %d\n",
				open->op_claim_type);
			status = nfserr_notsupp;
			goto out;
		default:
			dprintk("NFSD: Invalid OPEN claim type %d\n",
				open->op_claim_type);
			status = nfserr_inval;
			goto out;
	}
	/*
	 * nfsd4_process_open2() does the actual opening of the file.  If
	 * successful, it (1) truncates the file if open->op_truncate was
	 * set, (2) sets open->op_stateid, (3) sets open->op_delegation.
	 */
	status = nfsd4_process_open2(rqstp, resfh, open);
	WARN(status && open->op_created,
	     "nfsd4_process_open2 failed to open newly-created file! status=%u\n",
	     be32_to_cpu(status));
	if (reclaim && !status)
		nn->somebody_reclaimed = true;
out:
	if (resfh && resfh != &cstate->current_fh) {
		fh_dup2(&cstate->current_fh, resfh);
		fh_put(resfh);
		kfree(resfh);
	}
	nfsd4_cleanup_open_state(cstate, open);
	nfsd4_bump_seqid(cstate, status);
	return status;
}

/*
 * OPEN is the only seqid-mutating operation whose decoding can fail
 * with a seqid-mutating error (specifically, decoding of user names in
 * the attributes).  Therefore we have to do some processing to look up
 * the stateowner so that we can bump the seqid.
 */
static __be32 nfsd4_open_omfg(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate, struct nfsd4_op *op)
{
	struct nfsd4_open *open = &op->u.open;

	if (!seqid_mutating_err(ntohl(op->status)))
		return op->status;
	if (nfsd4_has_session(cstate))
		return op->status;
	open->op_xdr_error = op->status;
	return nfsd4_open(rqstp, cstate, &op->u);
}

/*
 * filehandle-manipulating ops.
 */
static __be32
nfsd4_getfh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    union nfsd4_op_u *u)
{
	u->getfh = &cstate->current_fh;
	return nfs_ok;
}

static __be32
nfsd4_putfh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    union nfsd4_op_u *u)
{
	struct nfsd4_putfh *putfh = &u->putfh;
	__be32 ret;

	fh_put(&cstate->current_fh);
	cstate->current_fh.fh_handle.fh_size = putfh->pf_fhlen;
	memcpy(&cstate->current_fh.fh_handle.fh_base, putfh->pf_fhval,
	       putfh->pf_fhlen);
	ret = fh_verify(rqstp, &cstate->current_fh, 0, NFSD_MAY_BYPASS_GSS);
#ifdef CONFIG_NFSD_V4_2_INTER_SSC
	if (ret == nfserr_stale && putfh->no_verify) {
		SET_FH_FLAG(&cstate->current_fh, NFSD4_FH_FOREIGN);
		ret = 0;
	}
#endif
	return ret;
}

static __be32
nfsd4_putrootfh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		union nfsd4_op_u *u)
{
	__be32 status;

	fh_put(&cstate->current_fh);
	status = exp_pseudoroot(rqstp, &cstate->current_fh);
	return status;
}

static __be32
nfsd4_restorefh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		union nfsd4_op_u *u)
{
	if (!cstate->save_fh.fh_dentry)
		return nfserr_restorefh;

	fh_dup2(&cstate->current_fh, &cstate->save_fh);
	if (HAS_CSTATE_FLAG(cstate, SAVED_STATE_ID_FLAG)) {
		memcpy(&cstate->current_stateid, &cstate->save_stateid, sizeof(stateid_t));
		SET_CSTATE_FLAG(cstate, CURRENT_STATE_ID_FLAG);
	}
	return nfs_ok;
}

static __be32
nfsd4_savefh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     union nfsd4_op_u *u)
{
	fh_dup2(&cstate->save_fh, &cstate->current_fh);
	if (HAS_CSTATE_FLAG(cstate, CURRENT_STATE_ID_FLAG)) {
		memcpy(&cstate->save_stateid, &cstate->current_stateid, sizeof(stateid_t));
		SET_CSTATE_FLAG(cstate, SAVED_STATE_ID_FLAG);
	}
	return nfs_ok;
}

/*
 * misc nfsv4 ops
 */
static __be32
nfsd4_access(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     union nfsd4_op_u *u)
{
	struct nfsd4_access *access = &u->access;
	u32 access_full;

	access_full = NFS3_ACCESS_FULL;
	if (cstate->minorversion >= 2)
		access_full |= NFS4_ACCESS_XALIST | NFS4_ACCESS_XAREAD |
			       NFS4_ACCESS_XAWRITE;

	if (access->ac_req_access & ~access_full)
		return nfserr_inval;

	access->ac_resp_access = access->ac_req_access;
	return nfsd_access(rqstp, &cstate->current_fh, &access->ac_resp_access,
			   &access->ac_supported);
}

static void gen_boot_verifier(nfs4_verifier *verifier, struct net *net)
{
	__be32 *verf = (__be32 *)verifier->data;

	BUILD_BUG_ON(2*sizeof(*verf) != sizeof(verifier->data));

	nfsd_copy_boot_verifier(verf, net_generic(net, nfsd_net_id));
}

static __be32
nfsd4_commit(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     union nfsd4_op_u *u)
{
	struct nfsd4_commit *commit = &u->commit;

	return nfsd_commit(rqstp, &cstate->current_fh, commit->co_offset,
			     commit->co_count,
			     (__be32 *)commit->co_verf.data);
}

static __be32
nfsd4_create(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     union nfsd4_op_u *u)
{
	struct nfsd4_create *create = &u->create;
	struct svc_fh resfh;
	__be32 status;
	dev_t rdev;

	fh_init(&resfh, NFS4_FHSIZE);

	status = fh_verify(rqstp, &cstate->current_fh, S_IFDIR, NFSD_MAY_NOP);
	if (status)
		return status;

	status = check_attr_support(rqstp, cstate, create->cr_bmval,
				    nfsd_attrmask);
	if (status)
		return status;

	current->fs->umask = create->cr_umask;
	switch (create->cr_type) {
	case NF4LNK:
		status = nfsd_symlink(rqstp, &cstate->current_fh,
				      create->cr_name, create->cr_namelen,
				      create->cr_data, &resfh);
		break;

	case NF4BLK:
		status = nfserr_inval;
		rdev = MKDEV(create->cr_specdata1, create->cr_specdata2);
		if (MAJOR(rdev) != create->cr_specdata1 ||
		    MINOR(rdev) != create->cr_specdata2)
			goto out_umask;
		status = nfsd_create(rqstp, &cstate->current_fh,
				     create->cr_name, create->cr_namelen,
				     &create->cr_iattr, S_IFBLK, rdev, &resfh);
		break;

	case NF4CHR:
		status = nfserr_inval;
		rdev = MKDEV(create->cr_specdata1, create->cr_specdata2);
		if (MAJOR(rdev) != create->cr_specdata1 ||
		    MINOR(rdev) != create->cr_specdata2)
			goto out_umask;
		status = nfsd_create(rqstp, &cstate->current_fh,
				     create->cr_name, create->cr_namelen,
				     &create->cr_iattr,S_IFCHR, rdev, &resfh);
		break;

	case NF4SOCK:
		status = nfsd_create(rqstp, &cstate->current_fh,
				     create->cr_name, create->cr_namelen,
				     &create->cr_iattr, S_IFSOCK, 0, &resfh);
		break;

	case NF4FIFO:
		status = nfsd_create(rqstp, &cstate->current_fh,
				     create->cr_name, create->cr_namelen,
				     &create->cr_iattr, S_IFIFO, 0, &resfh);
		break;

	case NF4DIR:
		create->cr_iattr.ia_valid &= ~ATTR_SIZE;
		status = nfsd_create(rqstp, &cstate->current_fh,
				     create->cr_name, create->cr_namelen,
				     &create->cr_iattr, S_IFDIR, 0, &resfh);
		break;

	default:
		status = nfserr_badtype;
	}

	if (status)
		goto out;

	if (create->cr_label.len)
		nfsd4_security_inode_setsecctx(&resfh, &create->cr_label, create->cr_bmval);

	if (create->cr_acl != NULL)
		do_set_nfs4_acl(rqstp, &resfh, create->cr_acl,
				create->cr_bmval);

	fh_unlock(&cstate->current_fh);
	set_change_info(&create->cr_cinfo, &cstate->current_fh);
	fh_dup2(&cstate->current_fh, &resfh);
out:
	fh_put(&resfh);
out_umask:
	current->fs->umask = 0;
	return status;
}

static __be32
nfsd4_getattr(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      union nfsd4_op_u *u)
{
	struct nfsd4_getattr *getattr = &u->getattr;
	__be32 status;

	status = fh_verify(rqstp, &cstate->current_fh, 0, NFSD_MAY_NOP);
	if (status)
		return status;

	if (getattr->ga_bmval[1] & NFSD_WRITEONLY_ATTRS_WORD1)
		return nfserr_inval;

	getattr->ga_bmval[0] &= nfsd_suppattrs[cstate->minorversion][0];
	getattr->ga_bmval[1] &= nfsd_suppattrs[cstate->minorversion][1];
	getattr->ga_bmval[2] &= nfsd_suppattrs[cstate->minorversion][2];

	getattr->ga_fhp = &cstate->current_fh;
	return nfs_ok;
}

static __be32
nfsd4_link(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   union nfsd4_op_u *u)
{
	struct nfsd4_link *link = &u->link;
	__be32 status;

	status = nfsd_link(rqstp, &cstate->current_fh,
			   link->li_name, link->li_namelen, &cstate->save_fh);
	if (!status)
		set_change_info(&link->li_cinfo, &cstate->current_fh);
	return status;
}

static __be32 nfsd4_do_lookupp(struct svc_rqst *rqstp, struct svc_fh *fh)
{
	struct svc_fh tmp_fh;
	__be32 ret;

	fh_init(&tmp_fh, NFS4_FHSIZE);
	ret = exp_pseudoroot(rqstp, &tmp_fh);
	if (ret)
		return ret;
	if (tmp_fh.fh_dentry == fh->fh_dentry) {
		fh_put(&tmp_fh);
		return nfserr_noent;
	}
	fh_put(&tmp_fh);
	return nfsd_lookup(rqstp, fh, "..", 2, fh);
}

static __be32
nfsd4_lookupp(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      union nfsd4_op_u *u)
{
	return nfsd4_do_lookupp(rqstp, &cstate->current_fh);
}

static __be32
nfsd4_lookup(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     union nfsd4_op_u *u)
{
	return nfsd_lookup(rqstp, &cstate->current_fh,
			   u->lookup.lo_name, u->lookup.lo_len,
			   &cstate->current_fh);
}

static __be32
nfsd4_read(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   union nfsd4_op_u *u)
{
	struct nfsd4_read *read = &u->read;
	__be32 status;

	read->rd_nf = NULL;
	if (read->rd_offset >= OFFSET_MAX)
		return nfserr_inval;

	trace_nfsd_read_start(rqstp, &cstate->current_fh,
			      read->rd_offset, read->rd_length);

	/*
	 * If we do a zero copy read, then a client will see read data
	 * that reflects the state of the file *after* performing the
	 * following compound.
	 *
	 * To ensure proper ordering, we therefore turn off zero copy if
	 * the client wants us to do more in this compound:
	 */
	if (!nfsd4_last_compound_op(rqstp))
		clear_bit(RQ_SPLICE_OK, &rqstp->rq_flags);

	/* check stateid */
	status = nfs4_preprocess_stateid_op(rqstp, cstate, &cstate->current_fh,
					&read->rd_stateid, RD_STATE,
					&read->rd_nf, NULL);
	if (status) {
		dprintk("NFSD: nfsd4_read: couldn't process stateid!\n");
		goto out;
	}
	status = nfs_ok;
out:
	read->rd_rqstp = rqstp;
	read->rd_fhp = &cstate->current_fh;
	return status;
}


static void
nfsd4_read_release(union nfsd4_op_u *u)
{
	if (u->read.rd_nf)
		nfsd_file_put(u->read.rd_nf);
	trace_nfsd_read_done(u->read.rd_rqstp, u->read.rd_fhp,
			     u->read.rd_offset, u->read.rd_length);
}

static __be32
nfsd4_readdir(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      union nfsd4_op_u *u)
{
	struct nfsd4_readdir *readdir = &u->readdir;
	u64 cookie = readdir->rd_cookie;
	static const nfs4_verifier zeroverf;

	/* no need to check permission - this will be done in nfsd_readdir() */

	if (readdir->rd_bmval[1] & NFSD_WRITEONLY_ATTRS_WORD1)
		return nfserr_inval;

	readdir->rd_bmval[0] &= nfsd_suppattrs[cstate->minorversion][0];
	readdir->rd_bmval[1] &= nfsd_suppattrs[cstate->minorversion][1];
	readdir->rd_bmval[2] &= nfsd_suppattrs[cstate->minorversion][2];

	if ((cookie == 1) || (cookie == 2) ||
	    (cookie == 0 && memcmp(readdir->rd_verf.data, zeroverf.data, NFS4_VERIFIER_SIZE)))
		return nfserr_bad_cookie;

	readdir->rd_rqstp = rqstp;
	readdir->rd_fhp = &cstate->current_fh;
	return nfs_ok;
}

static __be32
nfsd4_readlink(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	       union nfsd4_op_u *u)
{
	u->readlink.rl_rqstp = rqstp;
	u->readlink.rl_fhp = &cstate->current_fh;
	return nfs_ok;
}

static __be32
nfsd4_remove(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     union nfsd4_op_u *u)
{
	struct nfsd4_remove *remove = &u->remove;
	__be32 status;

	if (opens_in_grace(SVC_NET(rqstp)))
		return nfserr_grace;
	status = nfsd_unlink(rqstp, &cstate->current_fh, 0,
			     remove->rm_name, remove->rm_namelen);
	if (!status) {
		fh_unlock(&cstate->current_fh);
		set_change_info(&remove->rm_cinfo, &cstate->current_fh);
	}
	return status;
}

static __be32
nfsd4_rename(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     union nfsd4_op_u *u)
{
	struct nfsd4_rename *rename = &u->rename;
	__be32 status;

	if (opens_in_grace(SVC_NET(rqstp)))
		return nfserr_grace;
	status = nfsd_rename(rqstp, &cstate->save_fh, rename->rn_sname,
			     rename->rn_snamelen, &cstate->current_fh,
			     rename->rn_tname, rename->rn_tnamelen);
	if (status)
		return status;
	set_change_info(&rename->rn_sinfo, &cstate->current_fh);
	set_change_info(&rename->rn_tinfo, &cstate->save_fh);
	return nfs_ok;
}

static __be32
nfsd4_secinfo(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      union nfsd4_op_u *u)
{
	struct nfsd4_secinfo *secinfo = &u->secinfo;
	struct svc_export *exp;
	struct dentry *dentry;
	__be32 err;

	err = fh_verify(rqstp, &cstate->current_fh, S_IFDIR, NFSD_MAY_EXEC);
	if (err)
		return err;
	err = nfsd_lookup_dentry(rqstp, &cstate->current_fh,
				    secinfo->si_name, secinfo->si_namelen,
				    &exp, &dentry);
	if (err)
		return err;
	fh_unlock(&cstate->current_fh);
	if (d_really_is_negative(dentry)) {
		exp_put(exp);
		err = nfserr_noent;
	} else
		secinfo->si_exp = exp;
	dput(dentry);
	if (cstate->minorversion)
		/* See rfc 5661 section 2.6.3.1.1.8 */
		fh_put(&cstate->current_fh);
	return err;
}

static __be32
nfsd4_secinfo_no_name(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		union nfsd4_op_u *u)
{
	__be32 err;

	switch (u->secinfo_no_name.sin_style) {
	case NFS4_SECINFO_STYLE4_CURRENT_FH:
		break;
	case NFS4_SECINFO_STYLE4_PARENT:
		err = nfsd4_do_lookupp(rqstp, &cstate->current_fh);
		if (err)
			return err;
		break;
	default:
		return nfserr_inval;
	}

	u->secinfo_no_name.sin_exp = exp_get(cstate->current_fh.fh_export);
	fh_put(&cstate->current_fh);
	return nfs_ok;
}

static void
nfsd4_secinfo_release(union nfsd4_op_u *u)
{
	if (u->secinfo.si_exp)
		exp_put(u->secinfo.si_exp);
}

static void
nfsd4_secinfo_no_name_release(union nfsd4_op_u *u)
{
	if (u->secinfo_no_name.sin_exp)
		exp_put(u->secinfo_no_name.sin_exp);
}

static __be32
nfsd4_setattr(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      union nfsd4_op_u *u)
{
	struct nfsd4_setattr *setattr = &u->setattr;
	__be32 status = nfs_ok;
	int err;

	if (setattr->sa_iattr.ia_valid & ATTR_SIZE) {
		status = nfs4_preprocess_stateid_op(rqstp, cstate,
				&cstate->current_fh, &setattr->sa_stateid,
				WR_STATE, NULL, NULL);
		if (status) {
			dprintk("NFSD: nfsd4_setattr: couldn't process stateid!\n");
			return status;
		}
	}
	err = fh_want_write(&cstate->current_fh);
	if (err)
		return nfserrno(err);
	status = nfs_ok;

	status = check_attr_support(rqstp, cstate, setattr->sa_bmval,
				    nfsd_attrmask);
	if (status)
		goto out;

	if (setattr->sa_acl != NULL)
		status = nfsd4_set_nfs4_acl(rqstp, &cstate->current_fh,
					    setattr->sa_acl);
	if (status)
		goto out;
	if (setattr->sa_label.len)
		status = nfsd4_set_nfs4_label(rqstp, &cstate->current_fh,
				&setattr->sa_label);
	if (status)
		goto out;
	status = nfsd_setattr(rqstp, &cstate->current_fh, &setattr->sa_iattr,
				0, (time64_t)0);
out:
	fh_drop_write(&cstate->current_fh);
	return status;
}

static __be32
nfsd4_write(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    union nfsd4_op_u *u)
{
	struct nfsd4_write *write = &u->write;
	stateid_t *stateid = &write->wr_stateid;
	struct nfsd_file *nf = NULL;
	__be32 status = nfs_ok;
	unsigned long cnt;
	int nvecs;

	if (write->wr_offset >= OFFSET_MAX)
		return nfserr_inval;

	cnt = write->wr_buflen;
	trace_nfsd_write_start(rqstp, &cstate->current_fh,
			       write->wr_offset, cnt);
	status = nfs4_preprocess_stateid_op(rqstp, cstate, &cstate->current_fh,
						stateid, WR_STATE, &nf, NULL);
	if (status) {
		dprintk("NFSD: nfsd4_write: couldn't process stateid!\n");
		return status;
	}

	write->wr_how_written = write->wr_stable_how;

	nvecs = svc_fill_write_vector(rqstp, write->wr_pagelist,
				      &write->wr_head, write->wr_buflen);
	WARN_ON_ONCE(nvecs > ARRAY_SIZE(rqstp->rq_vec));

	status = nfsd_vfs_write(rqstp, &cstate->current_fh, nf,
				write->wr_offset, rqstp->rq_vec, nvecs, &cnt,
				write->wr_how_written,
				(__be32 *)write->wr_verifier.data);
	nfsd_file_put(nf);

	write->wr_bytes_written = cnt;
	trace_nfsd_write_done(rqstp, &cstate->current_fh,
			      write->wr_offset, cnt);
	return status;
}

static __be32
nfsd4_verify_copy(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  stateid_t *src_stateid, struct nfsd_file **src,
		  stateid_t *dst_stateid, struct nfsd_file **dst)
{
	__be32 status;

	if (!cstate->save_fh.fh_dentry)
		return nfserr_nofilehandle;

	status = nfs4_preprocess_stateid_op(rqstp, cstate, &cstate->save_fh,
					    src_stateid, RD_STATE, src, NULL);
	if (status) {
		dprintk("NFSD: %s: couldn't process src stateid!\n", __func__);
		goto out;
	}

	status = nfs4_preprocess_stateid_op(rqstp, cstate, &cstate->current_fh,
					    dst_stateid, WR_STATE, dst, NULL);
	if (status) {
		dprintk("NFSD: %s: couldn't process dst stateid!\n", __func__);
		goto out_put_src;
	}

	/* fix up for NFS-specific error code */
	if (!S_ISREG(file_inode((*src)->nf_file)->i_mode) ||
	    !S_ISREG(file_inode((*dst)->nf_file)->i_mode)) {
		status = nfserr_wrong_type;
		goto out_put_dst;
	}

out:
	return status;
out_put_dst:
	nfsd_file_put(*dst);
out_put_src:
	nfsd_file_put(*src);
	goto out;
}

static __be32
nfsd4_clone(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		union nfsd4_op_u *u)
{
	struct nfsd4_clone *clone = &u->clone;
	struct nfsd_file *src, *dst;
	__be32 status;

	status = nfsd4_verify_copy(rqstp, cstate, &clone->cl_src_stateid, &src,
				   &clone->cl_dst_stateid, &dst);
	if (status)
		goto out;

	status = nfsd4_clone_file_range(src, clone->cl_src_pos,
			dst, clone->cl_dst_pos, clone->cl_count,
			EX_ISSYNC(cstate->current_fh.fh_export));

	nfsd_file_put(dst);
	nfsd_file_put(src);
out:
	return status;
}

void nfs4_put_copy(struct nfsd4_copy *copy)
{
	if (!refcount_dec_and_test(&copy->refcount))
		return;
	kfree(copy);
}

static bool
check_and_set_stop_copy(struct nfsd4_copy *copy)
{
	bool value;

	spin_lock(&copy->cp_clp->async_lock);
	value = copy->stopped;
	if (!copy->stopped)
		copy->stopped = true;
	spin_unlock(&copy->cp_clp->async_lock);
	return value;
}

static void nfsd4_stop_copy(struct nfsd4_copy *copy)
{
	/* only 1 thread should stop the copy */
	if (!check_and_set_stop_copy(copy))
		kthread_stop(copy->copy_task);
	nfs4_put_copy(copy);
}

static struct nfsd4_copy *nfsd4_get_copy(struct nfs4_client *clp)
{
	struct nfsd4_copy *copy = NULL;

	spin_lock(&clp->async_lock);
	if (!list_empty(&clp->async_copies)) {
		copy = list_first_entry(&clp->async_copies, struct nfsd4_copy,
					copies);
		refcount_inc(&copy->refcount);
	}
	spin_unlock(&clp->async_lock);
	return copy;
}

void nfsd4_shutdown_copy(struct nfs4_client *clp)
{
	struct nfsd4_copy *copy;

	while ((copy = nfsd4_get_copy(clp)) != NULL)
		nfsd4_stop_copy(copy);
}
#ifdef CONFIG_NFSD_V4_2_INTER_SSC

extern struct file *nfs42_ssc_open(struct vfsmount *ss_mnt,
				   struct nfs_fh *src_fh,
				   nfs4_stateid *stateid);
extern void nfs42_ssc_close(struct file *filep);

extern void nfs_sb_deactive(struct super_block *sb);

#define NFSD42_INTERSSC_MOUNTOPS "vers=4.2,addr=%s,sec=sys"

/*
 * Support one copy source server for now.
 */
static __be32
nfsd4_interssc_connect(struct nl4_server *nss, struct svc_rqst *rqstp,
		       struct vfsmount **mount)
{
	struct file_system_type *type;
	struct vfsmount *ss_mnt;
	struct nfs42_netaddr *naddr;
	struct sockaddr_storage tmp_addr;
	size_t tmp_addrlen, match_netid_len = 3;
	char *startsep = "", *endsep = "", *match_netid = "tcp";
	char *ipaddr, *dev_name, *raw_data;
	int len, raw_len;
	__be32 status = nfserr_inval;

	naddr = &nss->u.nl4_addr;
	tmp_addrlen = rpc_uaddr2sockaddr(SVC_NET(rqstp), naddr->addr,
					 naddr->addr_len,
					 (struct sockaddr *)&tmp_addr,
					 sizeof(tmp_addr));
	if (tmp_addrlen == 0)
		goto out_err;

	if (tmp_addr.ss_family == AF_INET6) {
		startsep = "[";
		endsep = "]";
		match_netid = "tcp6";
		match_netid_len = 4;
	}

	if (naddr->netid_len != match_netid_len ||
		strncmp(naddr->netid, match_netid, naddr->netid_len))
		goto out_err;

	/* Construct the raw data for the vfs_kern_mount call */
	len = RPC_MAX_ADDRBUFLEN + 1;
	ipaddr = kzalloc(len, GFP_KERNEL);
	if (!ipaddr)
		goto out_err;

	rpc_ntop((struct sockaddr *)&tmp_addr, ipaddr, len);

	/* 2 for ipv6 endsep and startsep. 3 for ":/" and trailing '/0'*/

	raw_len = strlen(NFSD42_INTERSSC_MOUNTOPS) + strlen(ipaddr);
	raw_data = kzalloc(raw_len, GFP_KERNEL);
	if (!raw_data)
		goto out_free_ipaddr;

	snprintf(raw_data, raw_len, NFSD42_INTERSSC_MOUNTOPS, ipaddr);

	status = nfserr_nodev;
	type = get_fs_type("nfs");
	if (!type)
		goto out_free_rawdata;

	/* Set the server:<export> for the vfs_kern_mount call */
	dev_name = kzalloc(len + 5, GFP_KERNEL);
	if (!dev_name)
		goto out_free_rawdata;
	snprintf(dev_name, len + 5, "%s%s%s:/", startsep, ipaddr, endsep);

	/* Use an 'internal' mount: SB_KERNMOUNT -> MNT_INTERNAL */
	ss_mnt = vfs_kern_mount(type, SB_KERNMOUNT, dev_name, raw_data);
	module_put(type->owner);
	if (IS_ERR(ss_mnt))
		goto out_free_devname;

	status = 0;
	*mount = ss_mnt;

out_free_devname:
	kfree(dev_name);
out_free_rawdata:
	kfree(raw_data);
out_free_ipaddr:
	kfree(ipaddr);
out_err:
	return status;
}

static void
nfsd4_interssc_disconnect(struct vfsmount *ss_mnt)
{
	nfs_do_sb_deactive(ss_mnt->mnt_sb);
	mntput(ss_mnt);
}

/*
 * Verify COPY destination stateid.
 *
 * Connect to the source server with NFSv4.1.
 * Create the source struct file for nfsd_copy_range.
 * Called with COPY cstate:
 *    SAVED_FH: source filehandle
 *    CURRENT_FH: destination filehandle
 */
static __be32
nfsd4_setup_inter_ssc(struct svc_rqst *rqstp,
		      struct nfsd4_compound_state *cstate,
		      struct nfsd4_copy *copy, struct vfsmount **mount)
{
	struct svc_fh *s_fh = NULL;
	stateid_t *s_stid = &copy->cp_src_stateid;
	__be32 status = nfserr_inval;

	/* Verify the destination stateid and set dst struct file*/
	status = nfs4_preprocess_stateid_op(rqstp, cstate, &cstate->current_fh,
					    &copy->cp_dst_stateid,
					    WR_STATE, &copy->nf_dst, NULL);
	if (status)
		goto out;

	status = nfsd4_interssc_connect(&copy->cp_src, rqstp, mount);
	if (status)
		goto out;

	s_fh = &cstate->save_fh;

	copy->c_fh.size = s_fh->fh_handle.fh_size;
	memcpy(copy->c_fh.data, &s_fh->fh_handle.fh_base, copy->c_fh.size);
	copy->stateid.seqid = cpu_to_be32(s_stid->si_generation);
	memcpy(copy->stateid.other, (void *)&s_stid->si_opaque,
	       sizeof(stateid_opaque_t));

	status = 0;
out:
	return status;
}

static void
nfsd4_cleanup_inter_ssc(struct vfsmount *ss_mnt, struct nfsd_file *src,
			struct nfsd_file *dst)
{
	nfs42_ssc_close(src->nf_file);
	fput(src->nf_file);
	nfsd_file_put(dst);
	mntput(ss_mnt);
}

#else /* CONFIG_NFSD_V4_2_INTER_SSC */

static __be32
nfsd4_setup_inter_ssc(struct svc_rqst *rqstp,
		      struct nfsd4_compound_state *cstate,
		      struct nfsd4_copy *copy,
		      struct vfsmount **mount)
{
	*mount = NULL;
	return nfserr_inval;
}

static void
nfsd4_cleanup_inter_ssc(struct vfsmount *ss_mnt, struct nfsd_file *src,
			struct nfsd_file *dst)
{
}

static void
nfsd4_interssc_disconnect(struct vfsmount *ss_mnt)
{
}

static struct file *nfs42_ssc_open(struct vfsmount *ss_mnt,
				   struct nfs_fh *src_fh,
				   nfs4_stateid *stateid)
{
	return NULL;
}
#endif /* CONFIG_NFSD_V4_2_INTER_SSC */

static __be32
nfsd4_setup_intra_ssc(struct svc_rqst *rqstp,
		      struct nfsd4_compound_state *cstate,
		      struct nfsd4_copy *copy)
{
	return nfsd4_verify_copy(rqstp, cstate, &copy->cp_src_stateid,
				 &copy->nf_src, &copy->cp_dst_stateid,
				 &copy->nf_dst);
}

static void
nfsd4_cleanup_intra_ssc(struct nfsd_file *src, struct nfsd_file *dst)
{
	nfsd_file_put(src);
	nfsd_file_put(dst);
}

static void nfsd4_cb_offload_release(struct nfsd4_callback *cb)
{
	struct nfsd4_copy *copy = container_of(cb, struct nfsd4_copy, cp_cb);

	nfs4_put_copy(copy);
}

static int nfsd4_cb_offload_done(struct nfsd4_callback *cb,
				 struct rpc_task *task)
{
	return 1;
}

static const struct nfsd4_callback_ops nfsd4_cb_offload_ops = {
	.release = nfsd4_cb_offload_release,
	.done = nfsd4_cb_offload_done
};

static void nfsd4_init_copy_res(struct nfsd4_copy *copy, bool sync)
{
	copy->cp_res.wr_stable_how = NFS_UNSTABLE;
	copy->cp_synchronous = sync;
	gen_boot_verifier(&copy->cp_res.wr_verifier, copy->cp_clp->net);
}

static ssize_t _nfsd_copy_file_range(struct nfsd4_copy *copy)
{
	ssize_t bytes_copied = 0;
	size_t bytes_total = copy->cp_count;
	u64 src_pos = copy->cp_src_pos;
	u64 dst_pos = copy->cp_dst_pos;

	do {
		if (kthread_should_stop())
			break;
		bytes_copied = nfsd_copy_file_range(copy->nf_src->nf_file,
				src_pos, copy->nf_dst->nf_file, dst_pos,
				bytes_total);
		if (bytes_copied <= 0)
			break;
		bytes_total -= bytes_copied;
		copy->cp_res.wr_bytes_written += bytes_copied;
		src_pos += bytes_copied;
		dst_pos += bytes_copied;
	} while (bytes_total > 0 && !copy->cp_synchronous);
	return bytes_copied;
}

static __be32 nfsd4_do_copy(struct nfsd4_copy *copy, bool sync)
{
	__be32 status;
	ssize_t bytes;

	bytes = _nfsd_copy_file_range(copy);
	/* for async copy, we ignore the error, client can always retry
	 * to get the error
	 */
	if (bytes < 0 && !copy->cp_res.wr_bytes_written)
		status = nfserrno(bytes);
	else {
		nfsd4_init_copy_res(copy, sync);
		status = nfs_ok;
	}

	if (!copy->cp_intra) /* Inter server SSC */
		nfsd4_cleanup_inter_ssc(copy->ss_mnt, copy->nf_src,
					copy->nf_dst);
	else
		nfsd4_cleanup_intra_ssc(copy->nf_src, copy->nf_dst);

	return status;
}

static void dup_copy_fields(struct nfsd4_copy *src, struct nfsd4_copy *dst)
{
	dst->cp_src_pos = src->cp_src_pos;
	dst->cp_dst_pos = src->cp_dst_pos;
	dst->cp_count = src->cp_count;
	dst->cp_synchronous = src->cp_synchronous;
	memcpy(&dst->cp_res, &src->cp_res, sizeof(src->cp_res));
	memcpy(&dst->fh, &src->fh, sizeof(src->fh));
	dst->cp_clp = src->cp_clp;
	dst->nf_dst = nfsd_file_get(src->nf_dst);
	dst->cp_intra = src->cp_intra;
	if (src->cp_intra) /* for inter, file_src doesn't exist yet */
		dst->nf_src = nfsd_file_get(src->nf_src);

	memcpy(&dst->cp_stateid, &src->cp_stateid, sizeof(src->cp_stateid));
	memcpy(&dst->cp_src, &src->cp_src, sizeof(struct nl4_server));
	memcpy(&dst->stateid, &src->stateid, sizeof(src->stateid));
	memcpy(&dst->c_fh, &src->c_fh, sizeof(src->c_fh));
	dst->ss_mnt = src->ss_mnt;
}

static void cleanup_async_copy(struct nfsd4_copy *copy)
{
	nfs4_free_copy_state(copy);
	nfsd_file_put(copy->nf_dst);
	if (copy->cp_intra)
		nfsd_file_put(copy->nf_src);
	spin_lock(&copy->cp_clp->async_lock);
	list_del(&copy->copies);
	spin_unlock(&copy->cp_clp->async_lock);
	nfs4_put_copy(copy);
}

static int nfsd4_do_async_copy(void *data)
{
	struct nfsd4_copy *copy = (struct nfsd4_copy *)data;
	struct nfsd4_copy *cb_copy;

	if (!copy->cp_intra) { /* Inter server SSC */
		copy->nf_src = kzalloc(sizeof(struct nfsd_file), GFP_KERNEL);
		if (!copy->nf_src) {
			copy->nfserr = nfserr_serverfault;
			nfsd4_interssc_disconnect(copy->ss_mnt);
			goto do_callback;
		}
		copy->nf_src->nf_file = nfs42_ssc_open(copy->ss_mnt, &copy->c_fh,
					      &copy->stateid);
		if (IS_ERR(copy->nf_src->nf_file)) {
			copy->nfserr = nfserr_offload_denied;
			nfsd4_interssc_disconnect(copy->ss_mnt);
			goto do_callback;
		}
	}

	copy->nfserr = nfsd4_do_copy(copy, 0);
do_callback:
	cb_copy = kzalloc(sizeof(struct nfsd4_copy), GFP_KERNEL);
	if (!cb_copy)
		goto out;
	refcount_set(&cb_copy->refcount, 1);
	memcpy(&cb_copy->cp_res, &copy->cp_res, sizeof(copy->cp_res));
	cb_copy->cp_clp = copy->cp_clp;
	cb_copy->nfserr = copy->nfserr;
	memcpy(&cb_copy->fh, &copy->fh, sizeof(copy->fh));
	nfsd4_init_cb(&cb_copy->cp_cb, cb_copy->cp_clp,
			&nfsd4_cb_offload_ops, NFSPROC4_CLNT_CB_OFFLOAD);
	nfsd4_run_cb(&cb_copy->cp_cb);
out:
	if (!copy->cp_intra)
		kfree(copy->nf_src);
	cleanup_async_copy(copy);
	return 0;
}

static __be32
nfsd4_copy(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		union nfsd4_op_u *u)
{
	struct nfsd4_copy *copy = &u->copy;
	__be32 status;
	struct nfsd4_copy *async_copy = NULL;

	if (!copy->cp_intra) { /* Inter server SSC */
		if (!inter_copy_offload_enable || copy->cp_synchronous) {
			status = nfserr_notsupp;
			goto out;
		}
		status = nfsd4_setup_inter_ssc(rqstp, cstate, copy,
				&copy->ss_mnt);
		if (status)
			return nfserr_offload_denied;
	} else {
		status = nfsd4_setup_intra_ssc(rqstp, cstate, copy);
		if (status)
			return status;
	}

	copy->cp_clp = cstate->clp;
	memcpy(&copy->fh, &cstate->current_fh.fh_handle,
		sizeof(struct knfsd_fh));
	if (!copy->cp_synchronous) {
		struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

		status = nfserrno(-ENOMEM);
		async_copy = kzalloc(sizeof(struct nfsd4_copy), GFP_KERNEL);
		if (!async_copy)
			goto out_err;
		if (!nfs4_init_copy_state(nn, copy))
			goto out_err;
		refcount_set(&async_copy->refcount, 1);
		memcpy(&copy->cp_res.cb_stateid, &copy->cp_stateid.stid,
			sizeof(copy->cp_res.cb_stateid));
		dup_copy_fields(copy, async_copy);
		async_copy->copy_task = kthread_create(nfsd4_do_async_copy,
				async_copy, "%s", "copy thread");
		if (IS_ERR(async_copy->copy_task))
			goto out_err;
		spin_lock(&async_copy->cp_clp->async_lock);
		list_add(&async_copy->copies,
				&async_copy->cp_clp->async_copies);
		spin_unlock(&async_copy->cp_clp->async_lock);
		wake_up_process(async_copy->copy_task);
		status = nfs_ok;
	} else {
		status = nfsd4_do_copy(copy, 1);
	}
out:
	return status;
out_err:
	if (async_copy)
		cleanup_async_copy(async_copy);
	status = nfserrno(-ENOMEM);
	if (!copy->cp_intra)
		nfsd4_interssc_disconnect(copy->ss_mnt);
	goto out;
}

struct nfsd4_copy *
find_async_copy(struct nfs4_client *clp, stateid_t *stateid)
{
	struct nfsd4_copy *copy;

	spin_lock(&clp->async_lock);
	list_for_each_entry(copy, &clp->async_copies, copies) {
		if (memcmp(&copy->cp_stateid.stid, stateid, NFS4_STATEID_SIZE))
			continue;
		refcount_inc(&copy->refcount);
		spin_unlock(&clp->async_lock);
		return copy;
	}
	spin_unlock(&clp->async_lock);
	return NULL;
}

static __be32
nfsd4_offload_cancel(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     union nfsd4_op_u *u)
{
	struct nfsd4_offload_status *os = &u->offload_status;
	struct nfsd4_copy *copy;
	struct nfs4_client *clp = cstate->clp;

	copy = find_async_copy(clp, &os->stateid);
	if (!copy) {
		struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

		return manage_cpntf_state(nn, &os->stateid, clp, NULL);
	} else
		nfsd4_stop_copy(copy);

	return nfs_ok;
}

static __be32
nfsd4_copy_notify(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  union nfsd4_op_u *u)
{
	struct nfsd4_copy_notify *cn = &u->copy_notify;
	__be32 status;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	struct nfs4_stid *stid;
	struct nfs4_cpntf_state *cps;
	struct nfs4_client *clp = cstate->clp;

	status = nfs4_preprocess_stateid_op(rqstp, cstate, &cstate->current_fh,
					&cn->cpn_src_stateid, RD_STATE, NULL,
					&stid);
	if (status)
		return status;

	cn->cpn_sec = nn->nfsd4_lease;
	cn->cpn_nsec = 0;

	status = nfserrno(-ENOMEM);
	cps = nfs4_alloc_init_cpntf_state(nn, stid);
	if (!cps)
		goto out;
	memcpy(&cn->cpn_cnr_stateid, &cps->cp_stateid.stid, sizeof(stateid_t));
	memcpy(&cps->cp_p_stateid, &stid->sc_stateid, sizeof(stateid_t));
	memcpy(&cps->cp_p_clid, &clp->cl_clientid, sizeof(clientid_t));

	/* For now, only return one server address in cpn_src, the
	 * address used by the client to connect to this server.
	 */
	cn->cpn_src.nl4_type = NL4_NETADDR;
	status = nfsd4_set_netaddr((struct sockaddr *)&rqstp->rq_daddr,
				 &cn->cpn_src.u.nl4_addr);
	WARN_ON_ONCE(status);
	if (status) {
		nfs4_put_cpntf_state(nn, cps);
		goto out;
	}
out:
	nfs4_put_stid(stid);
	return status;
}

static __be32
nfsd4_fallocate(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		struct nfsd4_fallocate *fallocate, int flags)
{
	__be32 status;
	struct nfsd_file *nf;

	status = nfs4_preprocess_stateid_op(rqstp, cstate, &cstate->current_fh,
					    &fallocate->falloc_stateid,
					    WR_STATE, &nf, NULL);
	if (status != nfs_ok) {
		dprintk("NFSD: nfsd4_fallocate: couldn't process stateid!\n");
		return status;
	}

	status = nfsd4_vfs_fallocate(rqstp, &cstate->current_fh, nf->nf_file,
				     fallocate->falloc_offset,
				     fallocate->falloc_length,
				     flags);
	nfsd_file_put(nf);
	return status;
}
static __be32
nfsd4_offload_status(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     union nfsd4_op_u *u)
{
	struct nfsd4_offload_status *os = &u->offload_status;
	__be32 status = 0;
	struct nfsd4_copy *copy;
	struct nfs4_client *clp = cstate->clp;

	copy = find_async_copy(clp, &os->stateid);
	if (copy) {
		os->count = copy->cp_res.wr_bytes_written;
		nfs4_put_copy(copy);
	} else
		status = nfserr_bad_stateid;

	return status;
}

static __be32
nfsd4_allocate(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	       union nfsd4_op_u *u)
{
	return nfsd4_fallocate(rqstp, cstate, &u->allocate, 0);
}

static __be32
nfsd4_deallocate(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		 union nfsd4_op_u *u)
{
	return nfsd4_fallocate(rqstp, cstate, &u->deallocate,
			       FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE);
}

static __be32
nfsd4_seek(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   union nfsd4_op_u *u)
{
	struct nfsd4_seek *seek = &u->seek;
	int whence;
	__be32 status;
	struct nfsd_file *nf;

	status = nfs4_preprocess_stateid_op(rqstp, cstate, &cstate->current_fh,
					    &seek->seek_stateid,
					    RD_STATE, &nf, NULL);
	if (status) {
		dprintk("NFSD: nfsd4_seek: couldn't process stateid!\n");
		return status;
	}

	switch (seek->seek_whence) {
	case NFS4_CONTENT_DATA:
		whence = SEEK_DATA;
		break;
	case NFS4_CONTENT_HOLE:
		whence = SEEK_HOLE;
		break;
	default:
		status = nfserr_union_notsupp;
		goto out;
	}

	/*
	 * Note:  This call does change file->f_pos, but nothing in NFSD
	 *        should ever file->f_pos.
	 */
	seek->seek_pos = vfs_llseek(nf->nf_file, seek->seek_offset, whence);
	if (seek->seek_pos < 0)
		status = nfserrno(seek->seek_pos);
	else if (seek->seek_pos >= i_size_read(file_inode(nf->nf_file)))
		seek->seek_eof = true;

out:
	nfsd_file_put(nf);
	return status;
}

/* This routine never returns NFS_OK!  If there are no other errors, it
 * will return NFSERR_SAME or NFSERR_NOT_SAME depending on whether the
 * attributes matched.  VERIFY is implemented by mapping NFSERR_SAME
 * to NFS_OK after the call; NVERIFY by mapping NFSERR_NOT_SAME to NFS_OK.
 */
static __be32
_nfsd4_verify(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_verify *verify)
{
	__be32 *buf, *p;
	int count;
	__be32 status;

	status = fh_verify(rqstp, &cstate->current_fh, 0, NFSD_MAY_NOP);
	if (status)
		return status;

	status = check_attr_support(rqstp, cstate, verify->ve_bmval, NULL);
	if (status)
		return status;

	if ((verify->ve_bmval[0] & FATTR4_WORD0_RDATTR_ERROR)
	    || (verify->ve_bmval[1] & NFSD_WRITEONLY_ATTRS_WORD1))
		return nfserr_inval;
	if (verify->ve_attrlen & 3)
		return nfserr_inval;

	/* count in words:
	 *   bitmap_len(1) + bitmap(2) + attr_len(1) = 4
	 */
	count = 4 + (verify->ve_attrlen >> 2);
	buf = kmalloc(count << 2, GFP_KERNEL);
	if (!buf)
		return nfserr_jukebox;

	p = buf;
	status = nfsd4_encode_fattr_to_buf(&p, count, &cstate->current_fh,
				    cstate->current_fh.fh_export,
				    cstate->current_fh.fh_dentry,
				    verify->ve_bmval,
				    rqstp, 0);
	/*
	 * If nfsd4_encode_fattr() ran out of space, assume that's because
	 * the attributes are longer (hence different) than those given:
	 */
	if (status == nfserr_resource)
		status = nfserr_not_same;
	if (status)
		goto out_kfree;

	/* skip bitmap */
	p = buf + 1 + ntohl(buf[0]);
	status = nfserr_not_same;
	if (ntohl(*p++) != verify->ve_attrlen)
		goto out_kfree;
	if (!memcmp(p, verify->ve_attrval, verify->ve_attrlen))
		status = nfserr_same;

out_kfree:
	kfree(buf);
	return status;
}

static __be32
nfsd4_nverify(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      union nfsd4_op_u *u)
{
	__be32 status;

	status = _nfsd4_verify(rqstp, cstate, &u->verify);
	return status == nfserr_not_same ? nfs_ok : status;
}

static __be32
nfsd4_verify(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     union nfsd4_op_u *u)
{
	__be32 status;

	status = _nfsd4_verify(rqstp, cstate, &u->nverify);
	return status == nfserr_same ? nfs_ok : status;
}

#ifdef CONFIG_NFSD_PNFS
static const struct nfsd4_layout_ops *
nfsd4_layout_verify(struct svc_export *exp, unsigned int layout_type)
{
	if (!exp->ex_layout_types) {
		dprintk("%s: export does not support pNFS\n", __func__);
		return NULL;
	}

	if (layout_type >= LAYOUT_TYPE_MAX ||
	    !(exp->ex_layout_types & (1 << layout_type))) {
		dprintk("%s: layout type %d not supported\n",
			__func__, layout_type);
		return NULL;
	}

	return nfsd4_layout_ops[layout_type];
}

static __be32
nfsd4_getdeviceinfo(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate, union nfsd4_op_u *u)
{
	struct nfsd4_getdeviceinfo *gdp = &u->getdeviceinfo;
	const struct nfsd4_layout_ops *ops;
	struct nfsd4_deviceid_map *map;
	struct svc_export *exp;
	__be32 nfserr;

	dprintk("%s: layout_type %u dev_id [0x%llx:0x%x] maxcnt %u\n",
	       __func__,
	       gdp->gd_layout_type,
	       gdp->gd_devid.fsid_idx, gdp->gd_devid.generation,
	       gdp->gd_maxcount);

	map = nfsd4_find_devid_map(gdp->gd_devid.fsid_idx);
	if (!map) {
		dprintk("%s: couldn't find device ID to export mapping!\n",
			__func__);
		return nfserr_noent;
	}

	exp = rqst_exp_find(rqstp, map->fsid_type, map->fsid);
	if (IS_ERR(exp)) {
		dprintk("%s: could not find device id\n", __func__);
		return nfserr_noent;
	}

	nfserr = nfserr_layoutunavailable;
	ops = nfsd4_layout_verify(exp, gdp->gd_layout_type);
	if (!ops)
		goto out;

	nfserr = nfs_ok;
	if (gdp->gd_maxcount != 0) {
		nfserr = ops->proc_getdeviceinfo(exp->ex_path.mnt->mnt_sb,
				rqstp, cstate->session->se_client, gdp);
	}

	gdp->gd_notify_types &= ops->notify_types;
out:
	exp_put(exp);
	return nfserr;
}

static void
nfsd4_getdeviceinfo_release(union nfsd4_op_u *u)
{
	kfree(u->getdeviceinfo.gd_device);
}

static __be32
nfsd4_layoutget(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate, union nfsd4_op_u *u)
{
	struct nfsd4_layoutget *lgp = &u->layoutget;
	struct svc_fh *current_fh = &cstate->current_fh;
	const struct nfsd4_layout_ops *ops;
	struct nfs4_layout_stateid *ls;
	__be32 nfserr;
	int accmode = NFSD_MAY_READ_IF_EXEC;

	switch (lgp->lg_seg.iomode) {
	case IOMODE_READ:
		accmode |= NFSD_MAY_READ;
		break;
	case IOMODE_RW:
		accmode |= NFSD_MAY_READ | NFSD_MAY_WRITE;
		break;
	default:
		dprintk("%s: invalid iomode %d\n",
			__func__, lgp->lg_seg.iomode);
		nfserr = nfserr_badiomode;
		goto out;
	}

	nfserr = fh_verify(rqstp, current_fh, 0, accmode);
	if (nfserr)
		goto out;

	nfserr = nfserr_layoutunavailable;
	ops = nfsd4_layout_verify(current_fh->fh_export, lgp->lg_layout_type);
	if (!ops)
		goto out;

	/*
	 * Verify minlength and range as per RFC5661:
	 *  o  If loga_length is less than loga_minlength,
	 *     the metadata server MUST return NFS4ERR_INVAL.
	 *  o  If the sum of loga_offset and loga_minlength exceeds
	 *     NFS4_UINT64_MAX, and loga_minlength is not
	 *     NFS4_UINT64_MAX, the error NFS4ERR_INVAL MUST result.
	 *  o  If the sum of loga_offset and loga_length exceeds
	 *     NFS4_UINT64_MAX, and loga_length is not NFS4_UINT64_MAX,
	 *     the error NFS4ERR_INVAL MUST result.
	 */
	nfserr = nfserr_inval;
	if (lgp->lg_seg.length < lgp->lg_minlength ||
	    (lgp->lg_minlength != NFS4_MAX_UINT64 &&
	     lgp->lg_minlength > NFS4_MAX_UINT64 - lgp->lg_seg.offset) ||
	    (lgp->lg_seg.length != NFS4_MAX_UINT64 &&
	     lgp->lg_seg.length > NFS4_MAX_UINT64 - lgp->lg_seg.offset))
		goto out;
	if (lgp->lg_seg.length == 0)
		goto out;

	nfserr = nfsd4_preprocess_layout_stateid(rqstp, cstate, &lgp->lg_sid,
						true, lgp->lg_layout_type, &ls);
	if (nfserr) {
		trace_nfsd_layout_get_lookup_fail(&lgp->lg_sid);
		goto out;
	}

	nfserr = nfserr_recallconflict;
	if (atomic_read(&ls->ls_stid.sc_file->fi_lo_recalls))
		goto out_put_stid;

	nfserr = ops->proc_layoutget(d_inode(current_fh->fh_dentry),
				     current_fh, lgp);
	if (nfserr)
		goto out_put_stid;

	nfserr = nfsd4_insert_layout(lgp, ls);

out_put_stid:
	mutex_unlock(&ls->ls_mutex);
	nfs4_put_stid(&ls->ls_stid);
out:
	return nfserr;
}

static void
nfsd4_layoutget_release(union nfsd4_op_u *u)
{
	kfree(u->layoutget.lg_content);
}

static __be32
nfsd4_layoutcommit(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate, union nfsd4_op_u *u)
{
	struct nfsd4_layoutcommit *lcp = &u->layoutcommit;
	const struct nfsd4_layout_seg *seg = &lcp->lc_seg;
	struct svc_fh *current_fh = &cstate->current_fh;
	const struct nfsd4_layout_ops *ops;
	loff_t new_size = lcp->lc_last_wr + 1;
	struct inode *inode;
	struct nfs4_layout_stateid *ls;
	__be32 nfserr;

	nfserr = fh_verify(rqstp, current_fh, 0, NFSD_MAY_WRITE);
	if (nfserr)
		goto out;

	nfserr = nfserr_layoutunavailable;
	ops = nfsd4_layout_verify(current_fh->fh_export, lcp->lc_layout_type);
	if (!ops)
		goto out;
	inode = d_inode(current_fh->fh_dentry);

	nfserr = nfserr_inval;
	if (new_size <= seg->offset) {
		dprintk("pnfsd: last write before layout segment\n");
		goto out;
	}
	if (new_size > seg->offset + seg->length) {
		dprintk("pnfsd: last write beyond layout segment\n");
		goto out;
	}
	if (!lcp->lc_newoffset && new_size > i_size_read(inode)) {
		dprintk("pnfsd: layoutcommit beyond EOF\n");
		goto out;
	}

	nfserr = nfsd4_preprocess_layout_stateid(rqstp, cstate, &lcp->lc_sid,
						false, lcp->lc_layout_type,
						&ls);
	if (nfserr) {
		trace_nfsd_layout_commit_lookup_fail(&lcp->lc_sid);
		/* fixup error code as per RFC5661 */
		if (nfserr == nfserr_bad_stateid)
			nfserr = nfserr_badlayout;
		goto out;
	}

	/* LAYOUTCOMMIT does not require any serialization */
	mutex_unlock(&ls->ls_mutex);

	if (new_size > i_size_read(inode)) {
		lcp->lc_size_chg = 1;
		lcp->lc_newsize = new_size;
	} else {
		lcp->lc_size_chg = 0;
	}

	nfserr = ops->proc_layoutcommit(inode, lcp);
	nfs4_put_stid(&ls->ls_stid);
out:
	return nfserr;
}

static __be32
nfsd4_layoutreturn(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate, union nfsd4_op_u *u)
{
	struct nfsd4_layoutreturn *lrp = &u->layoutreturn;
	struct svc_fh *current_fh = &cstate->current_fh;
	__be32 nfserr;

	nfserr = fh_verify(rqstp, current_fh, 0, NFSD_MAY_NOP);
	if (nfserr)
		goto out;

	nfserr = nfserr_layoutunavailable;
	if (!nfsd4_layout_verify(current_fh->fh_export, lrp->lr_layout_type))
		goto out;

	switch (lrp->lr_seg.iomode) {
	case IOMODE_READ:
	case IOMODE_RW:
	case IOMODE_ANY:
		break;
	default:
		dprintk("%s: invalid iomode %d\n", __func__,
			lrp->lr_seg.iomode);
		nfserr = nfserr_inval;
		goto out;
	}

	switch (lrp->lr_return_type) {
	case RETURN_FILE:
		nfserr = nfsd4_return_file_layouts(rqstp, cstate, lrp);
		break;
	case RETURN_FSID:
	case RETURN_ALL:
		nfserr = nfsd4_return_client_layouts(rqstp, cstate, lrp);
		break;
	default:
		dprintk("%s: invalid return_type %d\n", __func__,
			lrp->lr_return_type);
		nfserr = nfserr_inval;
		break;
	}
out:
	return nfserr;
}
#endif /* CONFIG_NFSD_PNFS */

static __be32
nfsd4_getxattr(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	       union nfsd4_op_u *u)
{
	struct nfsd4_getxattr *getxattr = &u->getxattr;

	return nfsd_getxattr(rqstp, &cstate->current_fh,
			     getxattr->getxa_name, &getxattr->getxa_buf,
			     &getxattr->getxa_len);
}

static __be32
nfsd4_setxattr(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   union nfsd4_op_u *u)
{
	struct nfsd4_setxattr *setxattr = &u->setxattr;
	__be32 ret;

	if (opens_in_grace(SVC_NET(rqstp)))
		return nfserr_grace;

	ret = nfsd_setxattr(rqstp, &cstate->current_fh, setxattr->setxa_name,
			    setxattr->setxa_buf, setxattr->setxa_len,
			    setxattr->setxa_flags);

	if (!ret)
		set_change_info(&setxattr->setxa_cinfo, &cstate->current_fh);

	return ret;
}

static __be32
nfsd4_listxattrs(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   union nfsd4_op_u *u)
{
	/*
	 * Get the entire list, then copy out only the user attributes
	 * in the encode function.
	 */
	return nfsd_listxattr(rqstp, &cstate->current_fh,
			     &u->listxattrs.lsxa_buf, &u->listxattrs.lsxa_len);
}

static __be32
nfsd4_removexattr(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   union nfsd4_op_u *u)
{
	struct nfsd4_removexattr *removexattr = &u->removexattr;
	__be32 ret;

	if (opens_in_grace(SVC_NET(rqstp)))
		return nfserr_grace;

	ret = nfsd_removexattr(rqstp, &cstate->current_fh,
	    removexattr->rmxa_name);

	if (!ret)
		set_change_info(&removexattr->rmxa_cinfo, &cstate->current_fh);

	return ret;
}

/*
 * NULL call.
 */
static __be32
nfsd4_proc_null(struct svc_rqst *rqstp)
{
	return rpc_success;
}

static inline void nfsd4_increment_op_stats(u32 opnum)
{
	if (opnum >= FIRST_NFS4_OP && opnum <= LAST_NFS4_OP)
		nfsdstats.nfs4_opcount[opnum]++;
}

static const struct nfsd4_operation nfsd4_ops[];

static const char *nfsd4_op_name(unsigned opnum);

/*
 * Enforce NFSv4.1 COMPOUND ordering rules:
 *
 * Also note, enforced elsewhere:
 *	- SEQUENCE other than as first op results in
 *	  NFS4ERR_SEQUENCE_POS. (Enforced in nfsd4_sequence().)
 *	- BIND_CONN_TO_SESSION must be the only op in its compound.
 *	  (Enforced in nfsd4_bind_conn_to_session().)
 *	- DESTROY_SESSION must be the final operation in a compound, if
 *	  sessionid's in SEQUENCE and DESTROY_SESSION are the same.
 *	  (Enforced in nfsd4_destroy_session().)
 */
static __be32 nfs41_check_op_ordering(struct nfsd4_compoundargs *args)
{
	struct nfsd4_op *first_op = &args->ops[0];

	/* These ordering requirements don't apply to NFSv4.0: */
	if (args->minorversion == 0)
		return nfs_ok;
	/* This is weird, but OK, not our problem: */
	if (args->opcnt == 0)
		return nfs_ok;
	if (first_op->status == nfserr_op_illegal)
		return nfs_ok;
	if (!(nfsd4_ops[first_op->opnum].op_flags & ALLOWED_AS_FIRST_OP))
		return nfserr_op_not_in_session;
	if (first_op->opnum == OP_SEQUENCE)
		return nfs_ok;
	/*
	 * So first_op is something allowed outside a session, like
	 * EXCHANGE_ID; but then it has to be the only op in the
	 * compound:
	 */
	if (args->opcnt != 1)
		return nfserr_not_only_op;
	return nfs_ok;
}

const struct nfsd4_operation *OPDESC(struct nfsd4_op *op)
{
	return &nfsd4_ops[op->opnum];
}

bool nfsd4_cache_this_op(struct nfsd4_op *op)
{
	if (op->opnum == OP_ILLEGAL)
		return false;
	return OPDESC(op)->op_flags & OP_CACHEME;
}

static bool need_wrongsec_check(struct svc_rqst *rqstp)
{
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct nfsd4_compoundargs *argp = rqstp->rq_argp;
	struct nfsd4_op *this = &argp->ops[resp->opcnt - 1];
	struct nfsd4_op *next = &argp->ops[resp->opcnt];
	const struct nfsd4_operation *thisd = OPDESC(this);
	const struct nfsd4_operation *nextd;

	/*
	 * Most ops check wronsec on our own; only the putfh-like ops
	 * have special rules.
	 */
	if (!(thisd->op_flags & OP_IS_PUTFH_LIKE))
		return false;
	/*
	 * rfc 5661 2.6.3.1.1.6: don't bother erroring out a
	 * put-filehandle operation if we're not going to use the
	 * result:
	 */
	if (argp->opcnt == resp->opcnt)
		return false;
	if (next->opnum == OP_ILLEGAL)
		return false;
	nextd = OPDESC(next);
	/*
	 * Rest of 2.6.3.1.1: certain operations will return WRONGSEC
	 * errors themselves as necessary; others should check for them
	 * now:
	 */
	return !(nextd->op_flags & OP_HANDLES_WRONGSEC);
}

static void svcxdr_init_encode(struct svc_rqst *rqstp,
			       struct nfsd4_compoundres *resp)
{
	struct xdr_stream *xdr = &resp->xdr;
	struct xdr_buf *buf = &rqstp->rq_res;
	struct kvec *head = buf->head;

	xdr->buf = buf;
	xdr->iov = head;
	xdr->p   = head->iov_base + head->iov_len;
	xdr->end = head->iov_base + PAGE_SIZE - rqstp->rq_auth_slack;
	/* Tail and page_len should be zero at this point: */
	buf->len = buf->head[0].iov_len;
	xdr->scratch.iov_len = 0;
	xdr->page_ptr = buf->pages - 1;
	buf->buflen = PAGE_SIZE * (1 + rqstp->rq_page_end - buf->pages)
		- rqstp->rq_auth_slack;
}

#ifdef CONFIG_NFSD_V4_2_INTER_SSC
static void
check_if_stalefh_allowed(struct nfsd4_compoundargs *args)
{
	struct nfsd4_op	*op, *current_op = NULL, *saved_op = NULL;
	struct nfsd4_copy *copy;
	struct nfsd4_putfh *putfh;
	int i;

	/* traverse all operation and if it's a COPY compound, mark the
	 * source filehandle to skip verification
	 */
	for (i = 0; i < args->opcnt; i++) {
		op = &args->ops[i];
		if (op->opnum == OP_PUTFH)
			current_op = op;
		else if (op->opnum == OP_SAVEFH)
			saved_op = current_op;
		else if (op->opnum == OP_RESTOREFH)
			current_op = saved_op;
		else if (op->opnum == OP_COPY) {
			copy = (struct nfsd4_copy *)&op->u;
			if (!saved_op) {
				op->status = nfserr_nofilehandle;
				return;
			}
			putfh = (struct nfsd4_putfh *)&saved_op->u;
			if (!copy->cp_intra)
				putfh->no_verify = true;
		}
	}
}
#else
static void
check_if_stalefh_allowed(struct nfsd4_compoundargs *args)
{
}
#endif

/*
 * COMPOUND call.
 */
static __be32
nfsd4_proc_compound(struct svc_rqst *rqstp)
{
	struct nfsd4_compoundargs *args = rqstp->rq_argp;
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct nfsd4_op	*op;
	struct nfsd4_compound_state *cstate = &resp->cstate;
	struct svc_fh *current_fh = &cstate->current_fh;
	struct svc_fh *save_fh = &cstate->save_fh;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	__be32		status;

	svcxdr_init_encode(rqstp, resp);
	resp->tagp = resp->xdr.p;
	/* reserve space for: taglen, tag, and opcnt */
	xdr_reserve_space(&resp->xdr, 8 + args->taglen);
	resp->taglen = args->taglen;
	resp->tag = args->tag;
	resp->rqstp = rqstp;
	cstate->minorversion = args->minorversion;
	fh_init(current_fh, NFS4_FHSIZE);
	fh_init(save_fh, NFS4_FHSIZE);
	/*
	 * Don't use the deferral mechanism for NFSv4; compounds make it
	 * too hard to avoid non-idempotency problems.
	 */
	clear_bit(RQ_USEDEFERRAL, &rqstp->rq_flags);

	/*
	 * According to RFC3010, this takes precedence over all other errors.
	 */
	status = nfserr_minor_vers_mismatch;
	if (nfsd_minorversion(nn, args->minorversion, NFSD_TEST) <= 0)
		goto out;
	status = nfserr_resource;
	if (args->opcnt > NFSD_MAX_OPS_PER_COMPOUND)
		goto out;

	status = nfs41_check_op_ordering(args);
	if (status) {
		op = &args->ops[0];
		op->status = status;
		resp->opcnt = 1;
		goto encode_op;
	}
	check_if_stalefh_allowed(args);

	rqstp->rq_lease_breaker = (void **)&cstate->clp;

	trace_nfsd_compound(rqstp, args->opcnt);
	while (!status && resp->opcnt < args->opcnt) {
		op = &args->ops[resp->opcnt++];

		/*
		 * The XDR decode routines may have pre-set op->status;
		 * for example, if there is a miscellaneous XDR error
		 * it will be set to nfserr_bad_xdr.
		 */
		if (op->status) {
			if (op->opnum == OP_OPEN)
				op->status = nfsd4_open_omfg(rqstp, cstate, op);
			goto encode_op;
		}
		if (!current_fh->fh_dentry &&
				!HAS_FH_FLAG(current_fh, NFSD4_FH_FOREIGN)) {
			if (!(op->opdesc->op_flags & ALLOWED_WITHOUT_FH)) {
				op->status = nfserr_nofilehandle;
				goto encode_op;
			}
		} else if (current_fh->fh_export &&
			   current_fh->fh_export->ex_fslocs.migrated &&
			  !(op->opdesc->op_flags & ALLOWED_ON_ABSENT_FS)) {
			op->status = nfserr_moved;
			goto encode_op;
		}

		fh_clear_wcc(current_fh);

		/* If op is non-idempotent */
		if (op->opdesc->op_flags & OP_MODIFIES_SOMETHING) {
			/*
			 * Don't execute this op if we couldn't encode a
			 * succesful reply:
			 */
			u32 plen = op->opdesc->op_rsize_bop(rqstp, op);
			/*
			 * Plus if there's another operation, make sure
			 * we'll have space to at least encode an error:
			 */
			if (resp->opcnt < args->opcnt)
				plen += COMPOUND_ERR_SLACK_SPACE;
			op->status = nfsd4_check_resp_size(resp, plen);
		}

		if (op->status)
			goto encode_op;

		if (op->opdesc->op_get_currentstateid)
			op->opdesc->op_get_currentstateid(cstate, &op->u);
		op->status = op->opdesc->op_func(rqstp, cstate, &op->u);

		/* Only from SEQUENCE */
		if (cstate->status == nfserr_replay_cache) {
			dprintk("%s NFS4.1 replay from cache\n", __func__);
			status = op->status;
			goto out;
		}
		if (!op->status) {
			if (op->opdesc->op_set_currentstateid)
				op->opdesc->op_set_currentstateid(cstate, &op->u);

			if (op->opdesc->op_flags & OP_CLEAR_STATEID)
				clear_current_stateid(cstate);

			if (current_fh->fh_export &&
					need_wrongsec_check(rqstp))
				op->status = check_nfsd_access(current_fh->fh_export, rqstp);
		}
encode_op:
		if (op->status == nfserr_replay_me) {
			op->replay = &cstate->replay_owner->so_replay;
			nfsd4_encode_replay(&resp->xdr, op);
			status = op->status = op->replay->rp_status;
		} else {
			nfsd4_encode_operation(resp, op);
			status = op->status;
		}

		trace_nfsd_compound_status(args->opcnt, resp->opcnt, status,
					   nfsd4_op_name(op->opnum));

		nfsd4_cstate_clear_replay(cstate);
		nfsd4_increment_op_stats(op->opnum);
	}

	fh_put(current_fh);
	fh_put(save_fh);
	BUG_ON(cstate->replay_owner);
out:
	cstate->status = status;
	/* Reset deferral mechanism for RPC deferrals */
	set_bit(RQ_USEDEFERRAL, &rqstp->rq_flags);
	return rpc_success;
}

#define op_encode_hdr_size		(2)
#define op_encode_stateid_maxsz		(XDR_QUADLEN(NFS4_STATEID_SIZE))
#define op_encode_verifier_maxsz	(XDR_QUADLEN(NFS4_VERIFIER_SIZE))
#define op_encode_change_info_maxsz	(5)
#define nfs4_fattr_bitmap_maxsz		(4)

/* We'll fall back on returning no lockowner if run out of space: */
#define op_encode_lockowner_maxsz	(0)
#define op_encode_lock_denied_maxsz	(8 + op_encode_lockowner_maxsz)

#define nfs4_owner_maxsz		(1 + XDR_QUADLEN(IDMAP_NAMESZ))

#define op_encode_ace_maxsz		(3 + nfs4_owner_maxsz)
#define op_encode_delegation_maxsz	(1 + op_encode_stateid_maxsz + 1 + \
					 op_encode_ace_maxsz)

#define op_encode_channel_attrs_maxsz	(6 + 1 + 1)

static inline u32 nfsd4_only_status_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size) * sizeof(__be32);
}

static inline u32 nfsd4_status_stateid_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_stateid_maxsz)* sizeof(__be32);
}

static inline u32 nfsd4_access_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	/* ac_supported, ac_resp_access */
	return (op_encode_hdr_size + 2)* sizeof(__be32);
}

static inline u32 nfsd4_commit_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_verifier_maxsz) * sizeof(__be32);
}

static inline u32 nfsd4_create_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_change_info_maxsz
		+ nfs4_fattr_bitmap_maxsz) * sizeof(__be32);
}

/*
 * Note since this is an idempotent operation we won't insist on failing
 * the op prematurely if the estimate is too large.  We may turn off splice
 * reads unnecessarily.
 */
static inline u32 nfsd4_getattr_rsize(struct svc_rqst *rqstp,
				      struct nfsd4_op *op)
{
	u32 *bmap = op->u.getattr.ga_bmval;
	u32 bmap0 = bmap[0], bmap1 = bmap[1], bmap2 = bmap[2];
	u32 ret = 0;

	if (bmap0 & FATTR4_WORD0_ACL)
		return svc_max_payload(rqstp);
	if (bmap0 & FATTR4_WORD0_FS_LOCATIONS)
		return svc_max_payload(rqstp);

	if (bmap1 & FATTR4_WORD1_OWNER) {
		ret += IDMAP_NAMESZ + 4;
		bmap1 &= ~FATTR4_WORD1_OWNER;
	}
	if (bmap1 & FATTR4_WORD1_OWNER_GROUP) {
		ret += IDMAP_NAMESZ + 4;
		bmap1 &= ~FATTR4_WORD1_OWNER_GROUP;
	}
	if (bmap0 & FATTR4_WORD0_FILEHANDLE) {
		ret += NFS4_FHSIZE + 4;
		bmap0 &= ~FATTR4_WORD0_FILEHANDLE;
	}
	if (bmap2 & FATTR4_WORD2_SECURITY_LABEL) {
		ret += NFS4_MAXLABELLEN + 12;
		bmap2 &= ~FATTR4_WORD2_SECURITY_LABEL;
	}
	/*
	 * Largest of remaining attributes are 16 bytes (e.g.,
	 * supported_attributes)
	 */
	ret += 16 * (hweight32(bmap0) + hweight32(bmap1) + hweight32(bmap2));
	/* bitmask, length */
	ret += 20;
	return ret;
}

static inline u32 nfsd4_getfh_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + 1) * sizeof(__be32) + NFS4_FHSIZE;
}

static inline u32 nfsd4_link_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_change_info_maxsz)
		* sizeof(__be32);
}

static inline u32 nfsd4_lock_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_lock_denied_maxsz)
		* sizeof(__be32);
}

static inline u32 nfsd4_open_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_stateid_maxsz
		+ op_encode_change_info_maxsz + 1
		+ nfs4_fattr_bitmap_maxsz
		+ op_encode_delegation_maxsz) * sizeof(__be32);
}

static inline u32 nfsd4_read_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	u32 maxcount = 0, rlen = 0;

	maxcount = svc_max_payload(rqstp);
	rlen = min(op->u.read.rd_length, maxcount);

	return (op_encode_hdr_size + 2 + XDR_QUADLEN(rlen)) * sizeof(__be32);
}

static inline u32 nfsd4_read_plus_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	u32 maxcount = svc_max_payload(rqstp);
	u32 rlen = min(op->u.read.rd_length, maxcount);
	/*
	 * If we detect that the file changed during hole encoding, then we
	 * recover by encoding the remaining reply as data. This means we need
	 * to set aside enough room to encode two data segments.
	 */
	u32 seg_len = 2 * (1 + 2 + 1);

	return (op_encode_hdr_size + 2 + seg_len + XDR_QUADLEN(rlen)) * sizeof(__be32);
}

static inline u32 nfsd4_readdir_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	u32 maxcount = 0, rlen = 0;

	maxcount = svc_max_payload(rqstp);
	rlen = min(op->u.readdir.rd_maxcount, maxcount);

	return (op_encode_hdr_size + op_encode_verifier_maxsz +
		XDR_QUADLEN(rlen)) * sizeof(__be32);
}

static inline u32 nfsd4_readlink_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + 1) * sizeof(__be32) + PAGE_SIZE;
}

static inline u32 nfsd4_remove_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_change_info_maxsz)
		* sizeof(__be32);
}

static inline u32 nfsd4_rename_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_change_info_maxsz
		+ op_encode_change_info_maxsz) * sizeof(__be32);
}

static inline u32 nfsd4_sequence_rsize(struct svc_rqst *rqstp,
				       struct nfsd4_op *op)
{
	return (op_encode_hdr_size
		+ XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + 5) * sizeof(__be32);
}

static inline u32 nfsd4_test_stateid_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + 1 + op->u.test_stateid.ts_num_ids)
		* sizeof(__be32);
}

static inline u32 nfsd4_setattr_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + nfs4_fattr_bitmap_maxsz) * sizeof(__be32);
}

static inline u32 nfsd4_secinfo_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + RPC_AUTH_MAXFLAVOR *
		(4 + XDR_QUADLEN(GSS_OID_MAX_LEN))) * sizeof(__be32);
}

static inline u32 nfsd4_setclientid_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + 2 + XDR_QUADLEN(NFS4_VERIFIER_SIZE)) *
								sizeof(__be32);
}

static inline u32 nfsd4_write_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + 2 + op_encode_verifier_maxsz) * sizeof(__be32);
}

static inline u32 nfsd4_exchange_id_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + 2 + 1 + /* eir_clientid, eir_sequenceid */\
		1 + 1 + /* eir_flags, spr_how */\
		4 + /* spo_must_enforce & _allow with bitmap */\
		2 + /*eir_server_owner.so_minor_id */\
		/* eir_server_owner.so_major_id<> */\
		XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + 1 +\
		/* eir_server_scope<> */\
		XDR_QUADLEN(NFS4_OPAQUE_LIMIT) + 1 +\
		1 + /* eir_server_impl_id array length */\
		0 /* ignored eir_server_impl_id contents */) * sizeof(__be32);
}

static inline u32 nfsd4_bind_conn_to_session_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + \
		XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + /* bctsr_sessid */\
		2 /* bctsr_dir, use_conn_in_rdma_mode */) * sizeof(__be32);
}

static inline u32 nfsd4_create_session_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + \
		XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + /* sessionid */\
		2 + /* csr_sequence, csr_flags */\
		op_encode_channel_attrs_maxsz + \
		op_encode_channel_attrs_maxsz) * sizeof(__be32);
}

static inline u32 nfsd4_copy_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size +
		1 /* wr_callback */ +
		op_encode_stateid_maxsz /* wr_callback */ +
		2 /* wr_count */ +
		1 /* wr_committed */ +
		op_encode_verifier_maxsz +
		1 /* cr_consecutive */ +
		1 /* cr_synchronous */) * sizeof(__be32);
}

static inline u32 nfsd4_offload_status_rsize(struct svc_rqst *rqstp,
					     struct nfsd4_op *op)
{
	return (op_encode_hdr_size +
		2 /* osr_count */ +
		1 /* osr_complete<1> optional 0 for now */) * sizeof(__be32);
}

static inline u32 nfsd4_copy_notify_rsize(struct svc_rqst *rqstp,
					struct nfsd4_op *op)
{
	return (op_encode_hdr_size +
		3 /* cnr_lease_time */ +
		1 /* We support one cnr_source_server */ +
		1 /* cnr_stateid seq */ +
		op_encode_stateid_maxsz /* cnr_stateid */ +
		1 /* num cnr_source_server*/ +
		1 /* nl4_type */ +
		1 /* nl4 size */ +
		XDR_QUADLEN(NFS4_OPAQUE_LIMIT) /*nl4_loc + nl4_loc_sz */)
		* sizeof(__be32);
}

#ifdef CONFIG_NFSD_PNFS
static inline u32 nfsd4_getdeviceinfo_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	u32 maxcount = 0, rlen = 0;

	maxcount = svc_max_payload(rqstp);
	rlen = min(op->u.getdeviceinfo.gd_maxcount, maxcount);

	return (op_encode_hdr_size +
		1 /* gd_layout_type*/ +
		XDR_QUADLEN(rlen) +
		2 /* gd_notify_types */) * sizeof(__be32);
}

/*
 * At this stage we don't really know what layout driver will handle the request,
 * so we need to define an arbitrary upper bound here.
 */
#define MAX_LAYOUT_SIZE		128
static inline u32 nfsd4_layoutget_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size +
		1 /* logr_return_on_close */ +
		op_encode_stateid_maxsz +
		1 /* nr of layouts */ +
		MAX_LAYOUT_SIZE) * sizeof(__be32);
}

static inline u32 nfsd4_layoutcommit_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size +
		1 /* locr_newsize */ +
		2 /* ns_size */) * sizeof(__be32);
}

static inline u32 nfsd4_layoutreturn_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size +
		1 /* lrs_stateid */ +
		op_encode_stateid_maxsz) * sizeof(__be32);
}
#endif /* CONFIG_NFSD_PNFS */


static inline u32 nfsd4_seek_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + 3) * sizeof(__be32);
}

static inline u32 nfsd4_getxattr_rsize(struct svc_rqst *rqstp,
				       struct nfsd4_op *op)
{
	u32 maxcount, rlen;

	maxcount = svc_max_payload(rqstp);
	rlen = min_t(u32, XATTR_SIZE_MAX, maxcount);

	return (op_encode_hdr_size + 1 + XDR_QUADLEN(rlen)) * sizeof(__be32);
}

static inline u32 nfsd4_setxattr_rsize(struct svc_rqst *rqstp,
				       struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_change_info_maxsz)
		* sizeof(__be32);
}
static inline u32 nfsd4_listxattrs_rsize(struct svc_rqst *rqstp,
					 struct nfsd4_op *op)
{
	u32 maxcount, rlen;

	maxcount = svc_max_payload(rqstp);
	rlen = min(op->u.listxattrs.lsxa_maxcount, maxcount);

	return (op_encode_hdr_size + 4 + XDR_QUADLEN(rlen)) * sizeof(__be32);
}

static inline u32 nfsd4_removexattr_rsize(struct svc_rqst *rqstp,
					  struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_change_info_maxsz)
		* sizeof(__be32);
}


static const struct nfsd4_operation nfsd4_ops[] = {
	[OP_ACCESS] = {
		.op_func = nfsd4_access,
		.op_name = "OP_ACCESS",
		.op_rsize_bop = nfsd4_access_rsize,
	},
	[OP_CLOSE] = {
		.op_func = nfsd4_close,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_CLOSE",
		.op_rsize_bop = nfsd4_status_stateid_rsize,
		.op_get_currentstateid = nfsd4_get_closestateid,
		.op_set_currentstateid = nfsd4_set_closestateid,
	},
	[OP_COMMIT] = {
		.op_func = nfsd4_commit,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_COMMIT",
		.op_rsize_bop = nfsd4_commit_rsize,
	},
	[OP_CREATE] = {
		.op_func = nfsd4_create,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME | OP_CLEAR_STATEID,
		.op_name = "OP_CREATE",
		.op_rsize_bop = nfsd4_create_rsize,
	},
	[OP_DELEGRETURN] = {
		.op_func = nfsd4_delegreturn,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_DELEGRETURN",
		.op_rsize_bop = nfsd4_only_status_rsize,
		.op_get_currentstateid = nfsd4_get_delegreturnstateid,
	},
	[OP_GETATTR] = {
		.op_func = nfsd4_getattr,
		.op_flags = ALLOWED_ON_ABSENT_FS,
		.op_rsize_bop = nfsd4_getattr_rsize,
		.op_name = "OP_GETATTR",
	},
	[OP_GETFH] = {
		.op_func = nfsd4_getfh,
		.op_name = "OP_GETFH",
		.op_rsize_bop = nfsd4_getfh_rsize,
	},
	[OP_LINK] = {
		.op_func = nfsd4_link,
		.op_flags = ALLOWED_ON_ABSENT_FS | OP_MODIFIES_SOMETHING
				| OP_CACHEME,
		.op_name = "OP_LINK",
		.op_rsize_bop = nfsd4_link_rsize,
	},
	[OP_LOCK] = {
		.op_func = nfsd4_lock,
		.op_flags = OP_MODIFIES_SOMETHING |
				OP_NONTRIVIAL_ERROR_ENCODE,
		.op_name = "OP_LOCK",
		.op_rsize_bop = nfsd4_lock_rsize,
		.op_set_currentstateid = nfsd4_set_lockstateid,
	},
	[OP_LOCKT] = {
		.op_func = nfsd4_lockt,
		.op_flags = OP_NONTRIVIAL_ERROR_ENCODE,
		.op_name = "OP_LOCKT",
		.op_rsize_bop = nfsd4_lock_rsize,
	},
	[OP_LOCKU] = {
		.op_func = nfsd4_locku,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_LOCKU",
		.op_rsize_bop = nfsd4_status_stateid_rsize,
		.op_get_currentstateid = nfsd4_get_lockustateid,
	},
	[OP_LOOKUP] = {
		.op_func = nfsd4_lookup,
		.op_flags = OP_HANDLES_WRONGSEC | OP_CLEAR_STATEID,
		.op_name = "OP_LOOKUP",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_LOOKUPP] = {
		.op_func = nfsd4_lookupp,
		.op_flags = OP_HANDLES_WRONGSEC | OP_CLEAR_STATEID,
		.op_name = "OP_LOOKUPP",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_NVERIFY] = {
		.op_func = nfsd4_nverify,
		.op_name = "OP_NVERIFY",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_OPEN] = {
		.op_func = nfsd4_open,
		.op_flags = OP_HANDLES_WRONGSEC | OP_MODIFIES_SOMETHING,
		.op_name = "OP_OPEN",
		.op_rsize_bop = nfsd4_open_rsize,
		.op_set_currentstateid = nfsd4_set_openstateid,
	},
	[OP_OPEN_CONFIRM] = {
		.op_func = nfsd4_open_confirm,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_OPEN_CONFIRM",
		.op_rsize_bop = nfsd4_status_stateid_rsize,
	},
	[OP_OPEN_DOWNGRADE] = {
		.op_func = nfsd4_open_downgrade,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_OPEN_DOWNGRADE",
		.op_rsize_bop = nfsd4_status_stateid_rsize,
		.op_get_currentstateid = nfsd4_get_opendowngradestateid,
		.op_set_currentstateid = nfsd4_set_opendowngradestateid,
	},
	[OP_PUTFH] = {
		.op_func = nfsd4_putfh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_IS_PUTFH_LIKE | OP_CLEAR_STATEID,
		.op_name = "OP_PUTFH",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_PUTPUBFH] = {
		.op_func = nfsd4_putrootfh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_IS_PUTFH_LIKE | OP_CLEAR_STATEID,
		.op_name = "OP_PUTPUBFH",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_PUTROOTFH] = {
		.op_func = nfsd4_putrootfh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_IS_PUTFH_LIKE | OP_CLEAR_STATEID,
		.op_name = "OP_PUTROOTFH",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_READ] = {
		.op_func = nfsd4_read,
		.op_release = nfsd4_read_release,
		.op_name = "OP_READ",
		.op_rsize_bop = nfsd4_read_rsize,
		.op_get_currentstateid = nfsd4_get_readstateid,
	},
	[OP_READDIR] = {
		.op_func = nfsd4_readdir,
		.op_name = "OP_READDIR",
		.op_rsize_bop = nfsd4_readdir_rsize,
	},
	[OP_READLINK] = {
		.op_func = nfsd4_readlink,
		.op_name = "OP_READLINK",
		.op_rsize_bop = nfsd4_readlink_rsize,
	},
	[OP_REMOVE] = {
		.op_func = nfsd4_remove,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_REMOVE",
		.op_rsize_bop = nfsd4_remove_rsize,
	},
	[OP_RENAME] = {
		.op_func = nfsd4_rename,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_RENAME",
		.op_rsize_bop = nfsd4_rename_rsize,
	},
	[OP_RENEW] = {
		.op_func = nfsd4_renew,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_RENEW",
		.op_rsize_bop = nfsd4_only_status_rsize,

	},
	[OP_RESTOREFH] = {
		.op_func = nfsd4_restorefh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_IS_PUTFH_LIKE | OP_MODIFIES_SOMETHING,
		.op_name = "OP_RESTOREFH",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_SAVEFH] = {
		.op_func = nfsd4_savefh,
		.op_flags = OP_HANDLES_WRONGSEC | OP_MODIFIES_SOMETHING,
		.op_name = "OP_SAVEFH",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_SECINFO] = {
		.op_func = nfsd4_secinfo,
		.op_release = nfsd4_secinfo_release,
		.op_flags = OP_HANDLES_WRONGSEC,
		.op_name = "OP_SECINFO",
		.op_rsize_bop = nfsd4_secinfo_rsize,
	},
	[OP_SETATTR] = {
		.op_func = nfsd4_setattr,
		.op_name = "OP_SETATTR",
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME
				| OP_NONTRIVIAL_ERROR_ENCODE,
		.op_rsize_bop = nfsd4_setattr_rsize,
		.op_get_currentstateid = nfsd4_get_setattrstateid,
	},
	[OP_SETCLIENTID] = {
		.op_func = nfsd4_setclientid,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_MODIFIES_SOMETHING | OP_CACHEME
				| OP_NONTRIVIAL_ERROR_ENCODE,
		.op_name = "OP_SETCLIENTID",
		.op_rsize_bop = nfsd4_setclientid_rsize,
	},
	[OP_SETCLIENTID_CONFIRM] = {
		.op_func = nfsd4_setclientid_confirm,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_SETCLIENTID_CONFIRM",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_VERIFY] = {
		.op_func = nfsd4_verify,
		.op_name = "OP_VERIFY",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_WRITE] = {
		.op_func = nfsd4_write,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_WRITE",
		.op_rsize_bop = nfsd4_write_rsize,
		.op_get_currentstateid = nfsd4_get_writestateid,
	},
	[OP_RELEASE_LOCKOWNER] = {
		.op_func = nfsd4_release_lockowner,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_RELEASE_LOCKOWNER",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},

	/* NFSv4.1 operations */
	[OP_EXCHANGE_ID] = {
		.op_func = nfsd4_exchange_id,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_EXCHANGE_ID",
		.op_rsize_bop = nfsd4_exchange_id_rsize,
	},
	[OP_BACKCHANNEL_CTL] = {
		.op_func = nfsd4_backchannel_ctl,
		.op_flags = ALLOWED_WITHOUT_FH | OP_MODIFIES_SOMETHING,
		.op_name = "OP_BACKCHANNEL_CTL",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_BIND_CONN_TO_SESSION] = {
		.op_func = nfsd4_bind_conn_to_session,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_BIND_CONN_TO_SESSION",
		.op_rsize_bop = nfsd4_bind_conn_to_session_rsize,
	},
	[OP_CREATE_SESSION] = {
		.op_func = nfsd4_create_session,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_CREATE_SESSION",
		.op_rsize_bop = nfsd4_create_session_rsize,
	},
	[OP_DESTROY_SESSION] = {
		.op_func = nfsd4_destroy_session,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_DESTROY_SESSION",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_SEQUENCE] = {
		.op_func = nfsd4_sequence,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP,
		.op_name = "OP_SEQUENCE",
		.op_rsize_bop = nfsd4_sequence_rsize,
	},
	[OP_DESTROY_CLIENTID] = {
		.op_func = nfsd4_destroy_clientid,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_DESTROY_CLIENTID",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_RECLAIM_COMPLETE] = {
		.op_func = nfsd4_reclaim_complete,
		.op_flags = ALLOWED_WITHOUT_FH | OP_MODIFIES_SOMETHING,
		.op_name = "OP_RECLAIM_COMPLETE",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_SECINFO_NO_NAME] = {
		.op_func = nfsd4_secinfo_no_name,
		.op_release = nfsd4_secinfo_no_name_release,
		.op_flags = OP_HANDLES_WRONGSEC,
		.op_name = "OP_SECINFO_NO_NAME",
		.op_rsize_bop = nfsd4_secinfo_rsize,
	},
	[OP_TEST_STATEID] = {
		.op_func = nfsd4_test_stateid,
		.op_flags = ALLOWED_WITHOUT_FH,
		.op_name = "OP_TEST_STATEID",
		.op_rsize_bop = nfsd4_test_stateid_rsize,
	},
	[OP_FREE_STATEID] = {
		.op_func = nfsd4_free_stateid,
		.op_flags = ALLOWED_WITHOUT_FH | OP_MODIFIES_SOMETHING,
		.op_name = "OP_FREE_STATEID",
		.op_get_currentstateid = nfsd4_get_freestateid,
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
#ifdef CONFIG_NFSD_PNFS
	[OP_GETDEVICEINFO] = {
		.op_func = nfsd4_getdeviceinfo,
		.op_release = nfsd4_getdeviceinfo_release,
		.op_flags = ALLOWED_WITHOUT_FH,
		.op_name = "OP_GETDEVICEINFO",
		.op_rsize_bop = nfsd4_getdeviceinfo_rsize,
	},
	[OP_LAYOUTGET] = {
		.op_func = nfsd4_layoutget,
		.op_release = nfsd4_layoutget_release,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_LAYOUTGET",
		.op_rsize_bop = nfsd4_layoutget_rsize,
	},
	[OP_LAYOUTCOMMIT] = {
		.op_func = nfsd4_layoutcommit,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_LAYOUTCOMMIT",
		.op_rsize_bop = nfsd4_layoutcommit_rsize,
	},
	[OP_LAYOUTRETURN] = {
		.op_func = nfsd4_layoutreturn,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_LAYOUTRETURN",
		.op_rsize_bop = nfsd4_layoutreturn_rsize,
	},
#endif /* CONFIG_NFSD_PNFS */

	/* NFSv4.2 operations */
	[OP_ALLOCATE] = {
		.op_func = nfsd4_allocate,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_ALLOCATE",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_DEALLOCATE] = {
		.op_func = nfsd4_deallocate,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_DEALLOCATE",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_CLONE] = {
		.op_func = nfsd4_clone,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_CLONE",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_COPY] = {
		.op_func = nfsd4_copy,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_COPY",
		.op_rsize_bop = nfsd4_copy_rsize,
	},
	[OP_READ_PLUS] = {
		.op_func = nfsd4_read,
		.op_release = nfsd4_read_release,
		.op_name = "OP_READ_PLUS",
		.op_rsize_bop = nfsd4_read_plus_rsize,
		.op_get_currentstateid = nfsd4_get_readstateid,
	},
	[OP_SEEK] = {
		.op_func = nfsd4_seek,
		.op_name = "OP_SEEK",
		.op_rsize_bop = nfsd4_seek_rsize,
	},
	[OP_OFFLOAD_STATUS] = {
		.op_func = nfsd4_offload_status,
		.op_name = "OP_OFFLOAD_STATUS",
		.op_rsize_bop = nfsd4_offload_status_rsize,
	},
	[OP_OFFLOAD_CANCEL] = {
		.op_func = nfsd4_offload_cancel,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_OFFLOAD_CANCEL",
		.op_rsize_bop = nfsd4_only_status_rsize,
	},
	[OP_COPY_NOTIFY] = {
		.op_func = nfsd4_copy_notify,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_COPY_NOTIFY",
		.op_rsize_bop = nfsd4_copy_notify_rsize,
	},
	[OP_GETXATTR] = {
		.op_func = nfsd4_getxattr,
		.op_name = "OP_GETXATTR",
		.op_rsize_bop = nfsd4_getxattr_rsize,
	},
	[OP_SETXATTR] = {
		.op_func = nfsd4_setxattr,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_SETXATTR",
		.op_rsize_bop = nfsd4_setxattr_rsize,
	},
	[OP_LISTXATTRS] = {
		.op_func = nfsd4_listxattrs,
		.op_name = "OP_LISTXATTRS",
		.op_rsize_bop = nfsd4_listxattrs_rsize,
	},
	[OP_REMOVEXATTR] = {
		.op_func = nfsd4_removexattr,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_REMOVEXATTR",
		.op_rsize_bop = nfsd4_removexattr_rsize,
	},
};

/**
 * nfsd4_spo_must_allow - Determine if the compound op contains an
 * operation that is allowed to be sent with machine credentials
 *
 * @rqstp: a pointer to the struct svc_rqst
 *
 * Checks to see if the compound contains a spo_must_allow op
 * and confirms that it was sent with the proper machine creds.
 */

bool nfsd4_spo_must_allow(struct svc_rqst *rqstp)
{
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct nfsd4_compoundargs *argp = rqstp->rq_argp;
	struct nfsd4_op *this = &argp->ops[resp->opcnt - 1];
	struct nfsd4_compound_state *cstate = &resp->cstate;
	struct nfs4_op_map *allow = &cstate->clp->cl_spo_must_allow;
	u32 opiter;

	if (!cstate->minorversion)
		return false;

	if (cstate->spo_must_allowed)
		return true;

	opiter = resp->opcnt;
	while (opiter < argp->opcnt) {
		this = &argp->ops[opiter++];
		if (test_bit(this->opnum, allow->u.longs) &&
			cstate->clp->cl_mach_cred &&
			nfsd4_mach_creds_match(cstate->clp, rqstp)) {
			cstate->spo_must_allowed = true;
			return true;
		}
	}
	cstate->spo_must_allowed = false;
	return false;
}

int nfsd4_max_reply(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	if (op->opnum == OP_ILLEGAL || op->status == nfserr_notsupp)
		return op_encode_hdr_size * sizeof(__be32);

	BUG_ON(OPDESC(op)->op_rsize_bop == NULL);
	return OPDESC(op)->op_rsize_bop(rqstp, op);
}

void warn_on_nonidempotent_op(struct nfsd4_op *op)
{
	if (OPDESC(op)->op_flags & OP_MODIFIES_SOMETHING) {
		pr_err("unable to encode reply to nonidempotent op %d (%s)\n",
			op->opnum, nfsd4_op_name(op->opnum));
		WARN_ON_ONCE(1);
	}
}

static const char *nfsd4_op_name(unsigned opnum)
{
	if (opnum < ARRAY_SIZE(nfsd4_ops))
		return nfsd4_ops[opnum].op_name;
	return "unknown_operation";
}

#define nfsd4_voidres			nfsd4_voidargs
struct nfsd4_voidargs { int dummy; };

static const struct svc_procedure nfsd_procedures4[2] = {
	[NFSPROC4_NULL] = {
		.pc_func = nfsd4_proc_null,
		.pc_decode = nfs4svc_decode_voidarg,
		.pc_encode = nfs4svc_encode_voidres,
		.pc_argsize = sizeof(struct nfsd4_voidargs),
		.pc_ressize = sizeof(struct nfsd4_voidres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = 1,
	},
	[NFSPROC4_COMPOUND] = {
		.pc_func = nfsd4_proc_compound,
		.pc_decode = nfs4svc_decode_compoundargs,
		.pc_encode = nfs4svc_encode_compoundres,
		.pc_argsize = sizeof(struct nfsd4_compoundargs),
		.pc_ressize = sizeof(struct nfsd4_compoundres),
		.pc_release = nfsd4_release_compoundargs,
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = NFSD_BUFSIZE/4,
	},
};

static unsigned int nfsd_count3[ARRAY_SIZE(nfsd_procedures4)];
const struct svc_version nfsd_version4 = {
	.vs_vers		= 4,
	.vs_nproc		= 2,
	.vs_proc		= nfsd_procedures4,
	.vs_count		= nfsd_count3,
	.vs_dispatch		= nfsd_dispatch,
	.vs_xdrsize		= NFS4_SVC_XDRSIZE,
	.vs_rpcb_optnl		= true,
	.vs_need_cong_ctrl	= true,
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
