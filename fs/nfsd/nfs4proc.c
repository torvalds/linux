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
#include <linux/file.h>
#include <linux/falloc.h>
#include <linux/slab.h>

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
	open->op_truncate = 0;

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
		status = do_nfsd_create(rqstp, current_fh, open->op_fname.data,
					open->op_fname.len, &open->op_iattr,
					*resfh, open->op_createmode,
					(u32 *)open->op_verf.data,
					&open->op_truncate, &open->op_created);

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
	   struct nfsd4_open *open)
{
	__be32 status;
	struct svc_fh *resfh = NULL;
	struct net *net = SVC_NET(rqstp);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	dprintk("NFSD: nfsd4_open filename %.*s op_openowner %p\n",
		(int)open->op_fname.len, open->op_fname.data,
		open->op_openowner);

	/* This check required by spec. */
	if (open->op_create && open->op_claim_type != NFS4_OPEN_CLAIM_NULL)
		return nfserr_inval;

	open->op_created = 0;
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
	struct nfsd4_open *open = (struct nfsd4_open *)&op->u;

	if (!seqid_mutating_err(ntohl(op->status)))
		return op->status;
	if (nfsd4_has_session(cstate))
		return op->status;
	open->op_xdr_error = op->status;
	return nfsd4_open(rqstp, cstate, open);
}

/*
 * filehandle-manipulating ops.
 */
static __be32
nfsd4_getfh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct svc_fh **getfh)
{
	if (!cstate->current_fh.fh_dentry)
		return nfserr_nofilehandle;

	*getfh = &cstate->current_fh;
	return nfs_ok;
}

static __be32
nfsd4_putfh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_putfh *putfh)
{
	fh_put(&cstate->current_fh);
	cstate->current_fh.fh_handle.fh_size = putfh->pf_fhlen;
	memcpy(&cstate->current_fh.fh_handle.fh_base, putfh->pf_fhval,
	       putfh->pf_fhlen);
	return fh_verify(rqstp, &cstate->current_fh, 0, NFSD_MAY_BYPASS_GSS);
}

static __be32
nfsd4_putrootfh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		void *arg)
{
	__be32 status;

	fh_put(&cstate->current_fh);
	status = exp_pseudoroot(rqstp, &cstate->current_fh);
	return status;
}

static __be32
nfsd4_restorefh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		void *arg)
{
	if (!cstate->save_fh.fh_dentry)
		return nfserr_restorefh;

	fh_dup2(&cstate->current_fh, &cstate->save_fh);
	if (HAS_STATE_ID(cstate, SAVED_STATE_ID_FLAG)) {
		memcpy(&cstate->current_stateid, &cstate->save_stateid, sizeof(stateid_t));
		SET_STATE_ID(cstate, CURRENT_STATE_ID_FLAG);
	}
	return nfs_ok;
}

static __be32
nfsd4_savefh(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     void *arg)
{
	if (!cstate->current_fh.fh_dentry)
		return nfserr_nofilehandle;

	fh_dup2(&cstate->save_fh, &cstate->current_fh);
	if (HAS_STATE_ID(cstate, CURRENT_STATE_ID_FLAG)) {
		memcpy(&cstate->save_stateid, &cstate->current_stateid, sizeof(stateid_t));
		SET_STATE_ID(cstate, SAVED_STATE_ID_FLAG);
	}
	return nfs_ok;
}

/*
 * misc nfsv4 ops
 */
static __be32
nfsd4_access(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_access *access)
{
	if (access->ac_req_access & ~NFS3_ACCESS_FULL)
		return nfserr_inval;

	access->ac_resp_access = access->ac_req_access;
	return nfsd_access(rqstp, &cstate->current_fh, &access->ac_resp_access,
			   &access->ac_supported);
}

static void gen_boot_verifier(nfs4_verifier *verifier, struct net *net)
{
	__be32 verf[2];
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	/*
	 * This is opaque to client, so no need to byte-swap. Use
	 * __force to keep sparse happy
	 */
	verf[0] = (__force __be32)nn->nfssvc_boot.tv_sec;
	verf[1] = (__force __be32)nn->nfssvc_boot.tv_usec;
	memcpy(verifier->data, verf, sizeof(verifier->data));
}

static __be32
nfsd4_commit(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_commit *commit)
{
	gen_boot_verifier(&commit->co_verf, SVC_NET(rqstp));
	return nfsd_commit(rqstp, &cstate->current_fh, commit->co_offset,
			     commit->co_count);
}

static __be32
nfsd4_create(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_create *create)
{
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

	switch (create->cr_type) {
	case NF4LNK:
		status = nfsd_symlink(rqstp, &cstate->current_fh,
				      create->cr_name, create->cr_namelen,
				      create->cr_data, &resfh);
		break;

	case NF4BLK:
		rdev = MKDEV(create->cr_specdata1, create->cr_specdata2);
		if (MAJOR(rdev) != create->cr_specdata1 ||
		    MINOR(rdev) != create->cr_specdata2)
			return nfserr_inval;
		status = nfsd_create(rqstp, &cstate->current_fh,
				     create->cr_name, create->cr_namelen,
				     &create->cr_iattr, S_IFBLK, rdev, &resfh);
		break;

	case NF4CHR:
		rdev = MKDEV(create->cr_specdata1, create->cr_specdata2);
		if (MAJOR(rdev) != create->cr_specdata1 ||
		    MINOR(rdev) != create->cr_specdata2)
			return nfserr_inval;
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
	return status;
}

static __be32
nfsd4_getattr(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      struct nfsd4_getattr *getattr)
{
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
	   struct nfsd4_link *link)
{
	__be32 status = nfserr_nofilehandle;

	if (!cstate->save_fh.fh_dentry)
		return status;
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
	      void *arg)
{
	return nfsd4_do_lookupp(rqstp, &cstate->current_fh);
}

static __be32
nfsd4_lookup(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_lookup *lookup)
{
	return nfsd_lookup(rqstp, &cstate->current_fh,
			   lookup->lo_name, lookup->lo_len,
			   &cstate->current_fh);
}

static __be32
nfsd4_read(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   struct nfsd4_read *read)
{
	__be32 status;

	read->rd_filp = NULL;
	if (read->rd_offset >= OFFSET_MAX)
		return nfserr_inval;

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
					&read->rd_filp, &read->rd_tmp_file);
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

static __be32
nfsd4_readdir(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      struct nfsd4_readdir *readdir)
{
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
	       struct nfsd4_readlink *readlink)
{
	readlink->rl_rqstp = rqstp;
	readlink->rl_fhp = &cstate->current_fh;
	return nfs_ok;
}

static __be32
nfsd4_remove(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_remove *remove)
{
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
	     struct nfsd4_rename *rename)
{
	__be32 status = nfserr_nofilehandle;

	if (!cstate->save_fh.fh_dentry)
		return status;
	if (opens_in_grace(SVC_NET(rqstp)) &&
		!(cstate->save_fh.fh_export->ex_flags & NFSEXP_NOSUBTREECHECK))
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
	      struct nfsd4_secinfo *secinfo)
{
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
	      struct nfsd4_secinfo_no_name *sin)
{
	__be32 err;

	switch (sin->sin_style) {
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

	sin->sin_exp = exp_get(cstate->current_fh.fh_export);
	fh_put(&cstate->current_fh);
	return nfs_ok;
}

static __be32
nfsd4_setattr(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	      struct nfsd4_setattr *setattr)
{
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
				0, (time_t)0);
out:
	fh_drop_write(&cstate->current_fh);
	return status;
}

static int fill_in_write_vector(struct kvec *vec, struct nfsd4_write *write)
{
        int i = 1;
        int buflen = write->wr_buflen;

        vec[0].iov_base = write->wr_head.iov_base;
        vec[0].iov_len = min_t(int, buflen, write->wr_head.iov_len);
        buflen -= vec[0].iov_len;

        while (buflen) {
                vec[i].iov_base = page_address(write->wr_pagelist[i - 1]);
                vec[i].iov_len = min_t(int, PAGE_SIZE, buflen);
                buflen -= vec[i].iov_len;
                i++;
        }
        return i;
}

static __be32
nfsd4_write(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_write *write)
{
	stateid_t *stateid = &write->wr_stateid;
	struct file *filp = NULL;
	__be32 status = nfs_ok;
	unsigned long cnt;
	int nvecs;

	if (write->wr_offset >= OFFSET_MAX)
		return nfserr_inval;

	status = nfs4_preprocess_stateid_op(rqstp, cstate, &cstate->current_fh,
						stateid, WR_STATE, &filp, NULL);
	if (status) {
		dprintk("NFSD: nfsd4_write: couldn't process stateid!\n");
		return status;
	}

	cnt = write->wr_buflen;
	write->wr_how_written = write->wr_stable_how;
	gen_boot_verifier(&write->wr_verifier, SVC_NET(rqstp));

	nvecs = fill_in_write_vector(rqstp->rq_vec, write);
	WARN_ON_ONCE(nvecs > ARRAY_SIZE(rqstp->rq_vec));

	status = nfsd_vfs_write(rqstp, &cstate->current_fh, filp,
				write->wr_offset, rqstp->rq_vec, nvecs, &cnt,
				write->wr_how_written);
	fput(filp);

	write->wr_bytes_written = cnt;

	return status;
}

static __be32
nfsd4_verify_copy(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  stateid_t *src_stateid, struct file **src,
		  stateid_t *dst_stateid, struct file **dst)
{
	__be32 status;

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
	if (!S_ISREG(file_inode(*src)->i_mode) ||
	    !S_ISREG(file_inode(*dst)->i_mode)) {
		status = nfserr_wrong_type;
		goto out_put_dst;
	}

out:
	return status;
out_put_dst:
	fput(*dst);
out_put_src:
	fput(*src);
	goto out;
}

static __be32
nfsd4_clone(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		struct nfsd4_clone *clone)
{
	struct file *src, *dst;
	__be32 status;

	status = nfsd4_verify_copy(rqstp, cstate, &clone->cl_src_stateid, &src,
				   &clone->cl_dst_stateid, &dst);
	if (status)
		goto out;

	status = nfsd4_clone_file_range(src, clone->cl_src_pos,
			dst, clone->cl_dst_pos, clone->cl_count);

	fput(dst);
	fput(src);
out:
	return status;
}

static __be32
nfsd4_copy(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		struct nfsd4_copy *copy)
{
	struct file *src, *dst;
	__be32 status;
	ssize_t bytes;

	status = nfsd4_verify_copy(rqstp, cstate, &copy->cp_src_stateid, &src,
				   &copy->cp_dst_stateid, &dst);
	if (status)
		goto out;

	bytes = nfsd_copy_file_range(src, copy->cp_src_pos,
			dst, copy->cp_dst_pos, copy->cp_count);

	if (bytes < 0)
		status = nfserrno(bytes);
	else {
		copy->cp_res.wr_bytes_written = bytes;
		copy->cp_res.wr_stable_how = NFS_UNSTABLE;
		copy->cp_consecutive = 1;
		copy->cp_synchronous = 1;
		gen_boot_verifier(&copy->cp_res.wr_verifier, SVC_NET(rqstp));
		status = nfs_ok;
	}

	fput(src);
	fput(dst);
out:
	return status;
}

static __be32
nfsd4_fallocate(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		struct nfsd4_fallocate *fallocate, int flags)
{
	__be32 status = nfserr_notsupp;
	struct file *file;

	status = nfs4_preprocess_stateid_op(rqstp, cstate, &cstate->current_fh,
					    &fallocate->falloc_stateid,
					    WR_STATE, &file, NULL);
	if (status != nfs_ok) {
		dprintk("NFSD: nfsd4_fallocate: couldn't process stateid!\n");
		return status;
	}

	status = nfsd4_vfs_fallocate(rqstp, &cstate->current_fh, file,
				     fallocate->falloc_offset,
				     fallocate->falloc_length,
				     flags);
	fput(file);
	return status;
}

static __be32
nfsd4_allocate(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	       struct nfsd4_fallocate *fallocate)
{
	return nfsd4_fallocate(rqstp, cstate, fallocate, 0);
}

static __be32
nfsd4_deallocate(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		 struct nfsd4_fallocate *fallocate)
{
	return nfsd4_fallocate(rqstp, cstate, fallocate,
			       FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE);
}

static __be32
nfsd4_seek(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		struct nfsd4_seek *seek)
{
	int whence;
	__be32 status;
	struct file *file;

	status = nfs4_preprocess_stateid_op(rqstp, cstate, &cstate->current_fh,
					    &seek->seek_stateid,
					    RD_STATE, &file, NULL);
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
	seek->seek_pos = vfs_llseek(file, seek->seek_offset, whence);
	if (seek->seek_pos < 0)
		status = nfserrno(seek->seek_pos);
	else if (seek->seek_pos >= i_size_read(file_inode(file)))
		seek->seek_eof = true;

out:
	fput(file);
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
	      struct nfsd4_verify *verify)
{
	__be32 status;

	status = _nfsd4_verify(rqstp, cstate, verify);
	return status == nfserr_not_same ? nfs_ok : status;
}

static __be32
nfsd4_verify(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	     struct nfsd4_verify *verify)
{
	__be32 status;

	status = _nfsd4_verify(rqstp, cstate, verify);
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
		struct nfsd4_compound_state *cstate,
		struct nfsd4_getdeviceinfo *gdp)
{
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

static __be32
nfsd4_layoutget(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_layoutget *lgp)
{
	struct svc_fh *current_fh = &cstate->current_fh;
	const struct nfsd4_layout_ops *ops;
	struct nfs4_layout_stateid *ls;
	__be32 nfserr;
	int accmode;

	switch (lgp->lg_seg.iomode) {
	case IOMODE_READ:
		accmode = NFSD_MAY_READ;
		break;
	case IOMODE_RW:
		accmode = NFSD_MAY_READ | NFSD_MAY_WRITE;
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
		trace_layout_get_lookup_fail(&lgp->lg_sid);
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

static __be32
nfsd4_layoutcommit(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_layoutcommit *lcp)
{
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
		trace_layout_commit_lookup_fail(&lcp->lc_sid);
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
		struct nfsd4_compound_state *cstate,
		struct nfsd4_layoutreturn *lrp)
{
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

/*
 * NULL call.
 */
static __be32
nfsd4_proc_null(struct svc_rqst *rqstp)
{
	return nfs_ok;
}

static inline void nfsd4_increment_op_stats(u32 opnum)
{
	if (opnum >= FIRST_NFS4_OP && opnum <= LAST_NFS4_OP)
		nfsdstats.nfs4_opcount[opnum]++;
}

typedef __be32(*nfsd4op_func)(struct svc_rqst *, struct nfsd4_compound_state *,
			      void *);
typedef u32(*nfsd4op_rsize)(struct svc_rqst *, struct nfsd4_op *op);

typedef void(*stateid_setter)(struct nfsd4_compound_state *, void *);
typedef void(*stateid_getter)(struct nfsd4_compound_state *, void *);

enum nfsd4_op_flags {
	ALLOWED_WITHOUT_FH = 1 << 0,	/* No current filehandle required */
	ALLOWED_ON_ABSENT_FS = 1 << 1,	/* ops processed on absent fs */
	ALLOWED_AS_FIRST_OP = 1 << 2,	/* ops reqired first in compound */
	/* For rfc 5661 section 2.6.3.1.1: */
	OP_HANDLES_WRONGSEC = 1 << 3,
	OP_IS_PUTFH_LIKE = 1 << 4,
	/*
	 * These are the ops whose result size we estimate before
	 * encoding, to avoid performing an op then not being able to
	 * respond or cache a response.  This includes writes and setattrs
	 * as well as the operations usually called "nonidempotent":
	 */
	OP_MODIFIES_SOMETHING = 1 << 5,
	/*
	 * Cache compounds containing these ops in the xid-based drc:
	 * We use the DRC for compounds containing non-idempotent
	 * operations, *except* those that are 4.1-specific (since
	 * sessions provide their own EOS), and except for stateful
	 * operations other than setclientid and setclientid_confirm
	 * (since sequence numbers provide EOS for open, lock, etc in
	 * the v4.0 case).
	 */
	OP_CACHEME = 1 << 6,
	/*
	 * These are ops which clear current state id.
	 */
	OP_CLEAR_STATEID = 1 << 7,
};

struct nfsd4_operation {
	nfsd4op_func op_func;
	u32 op_flags;
	char *op_name;
	/* Try to get response size before operation */
	nfsd4op_rsize op_rsize_bop;
	stateid_getter op_get_currentstateid;
	stateid_setter op_set_currentstateid;
};

static struct nfsd4_operation nfsd4_ops[];

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
	struct nfsd4_op *op = &args->ops[0];

	/* These ordering requirements don't apply to NFSv4.0: */
	if (args->minorversion == 0)
		return nfs_ok;
	/* This is weird, but OK, not our problem: */
	if (args->opcnt == 0)
		return nfs_ok;
	if (op->status == nfserr_op_illegal)
		return nfs_ok;
	if (!(nfsd4_ops[op->opnum].op_flags & ALLOWED_AS_FIRST_OP))
		return nfserr_op_not_in_session;
	if (op->opnum == OP_SEQUENCE)
		return nfs_ok;
	if (args->opcnt != 1)
		return nfserr_not_only_op;
	return nfs_ok;
}

static inline struct nfsd4_operation *OPDESC(struct nfsd4_op *op)
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
	struct nfsd4_operation *thisd;
	struct nfsd4_operation *nextd;

	thisd = OPDESC(this);
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

/*
 * COMPOUND call.
 */
static __be32
nfsd4_proc_compound(struct svc_rqst *rqstp)
{
	struct nfsd4_compoundargs *args = rqstp->rq_argp;
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct nfsd4_op	*op;
	struct nfsd4_operation *opdesc;
	struct nfsd4_compound_state *cstate = &resp->cstate;
	struct svc_fh *current_fh = &cstate->current_fh;
	struct svc_fh *save_fh = &cstate->save_fh;
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
	if (nfsd_minorversion(args->minorversion, NFSD_TEST) <= 0)
		goto out;

	status = nfs41_check_op_ordering(args);
	if (status) {
		op = &args->ops[0];
		op->status = status;
		goto encode_op;
	}

	while (!status && resp->opcnt < args->opcnt) {
		op = &args->ops[resp->opcnt++];

		dprintk("nfsv4 compound op #%d/%d: %d (%s)\n",
			resp->opcnt, args->opcnt, op->opnum,
			nfsd4_op_name(op->opnum));
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

		opdesc = OPDESC(op);

		if (!current_fh->fh_dentry) {
			if (!(opdesc->op_flags & ALLOWED_WITHOUT_FH)) {
				op->status = nfserr_nofilehandle;
				goto encode_op;
			}
		} else if (current_fh->fh_export->ex_fslocs.migrated &&
			  !(opdesc->op_flags & ALLOWED_ON_ABSENT_FS)) {
			op->status = nfserr_moved;
			goto encode_op;
		}

		fh_clear_wcc(current_fh);

		/* If op is non-idempotent */
		if (opdesc->op_flags & OP_MODIFIES_SOMETHING) {
			/*
			 * Don't execute this op if we couldn't encode a
			 * succesful reply:
			 */
			u32 plen = opdesc->op_rsize_bop(rqstp, op);
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

		if (opdesc->op_get_currentstateid)
			opdesc->op_get_currentstateid(cstate, &op->u);
		op->status = opdesc->op_func(rqstp, cstate, &op->u);

		/* Only from SEQUENCE */
		if (cstate->status == nfserr_replay_cache) {
			dprintk("%s NFS4.1 replay from cache\n", __func__);
			status = op->status;
			goto out;
		}
		if (!op->status) {
			if (opdesc->op_set_currentstateid)
				opdesc->op_set_currentstateid(cstate, &op->u);

			if (opdesc->op_flags & OP_CLEAR_STATEID)
				clear_current_stateid(cstate);

			if (need_wrongsec_check(rqstp))
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

		dprintk("nfsv4 compound op %p opcnt %d #%d: %d: status %d\n",
			args->ops, args->opcnt, resp->opcnt, op->opnum,
			be32_to_cpu(status));

		nfsd4_cstate_clear_replay(cstate);
		nfsd4_increment_op_stats(op->opnum);
	}

	cstate->status = status;
	fh_put(current_fh);
	fh_put(save_fh);
	BUG_ON(cstate->replay_owner);
out:
	/* Reset deferral mechanism for RPC deferrals */
	set_bit(RQ_USEDEFERRAL, &rqstp->rq_flags);
	dprintk("nfsv4 compound returned %d\n", ntohl(status));
	return status;
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

static struct nfsd4_operation nfsd4_ops[] = {
	[OP_ACCESS] = {
		.op_func = (nfsd4op_func)nfsd4_access,
		.op_name = "OP_ACCESS",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_access_rsize,
	},
	[OP_CLOSE] = {
		.op_func = (nfsd4op_func)nfsd4_close,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_CLOSE",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_status_stateid_rsize,
		.op_get_currentstateid = (stateid_getter)nfsd4_get_closestateid,
		.op_set_currentstateid = (stateid_setter)nfsd4_set_closestateid,
	},
	[OP_COMMIT] = {
		.op_func = (nfsd4op_func)nfsd4_commit,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_COMMIT",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_commit_rsize,
	},
	[OP_CREATE] = {
		.op_func = (nfsd4op_func)nfsd4_create,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME | OP_CLEAR_STATEID,
		.op_name = "OP_CREATE",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_create_rsize,
	},
	[OP_DELEGRETURN] = {
		.op_func = (nfsd4op_func)nfsd4_delegreturn,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_DELEGRETURN",
		.op_rsize_bop = nfsd4_only_status_rsize,
		.op_get_currentstateid = (stateid_getter)nfsd4_get_delegreturnstateid,
	},
	[OP_GETATTR] = {
		.op_func = (nfsd4op_func)nfsd4_getattr,
		.op_flags = ALLOWED_ON_ABSENT_FS,
		.op_rsize_bop = nfsd4_getattr_rsize,
		.op_name = "OP_GETATTR",
	},
	[OP_GETFH] = {
		.op_func = (nfsd4op_func)nfsd4_getfh,
		.op_name = "OP_GETFH",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_getfh_rsize,
	},
	[OP_LINK] = {
		.op_func = (nfsd4op_func)nfsd4_link,
		.op_flags = ALLOWED_ON_ABSENT_FS | OP_MODIFIES_SOMETHING
				| OP_CACHEME,
		.op_name = "OP_LINK",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_link_rsize,
	},
	[OP_LOCK] = {
		.op_func = (nfsd4op_func)nfsd4_lock,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_LOCK",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_lock_rsize,
		.op_set_currentstateid = (stateid_setter)nfsd4_set_lockstateid,
	},
	[OP_LOCKT] = {
		.op_func = (nfsd4op_func)nfsd4_lockt,
		.op_name = "OP_LOCKT",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_lock_rsize,
	},
	[OP_LOCKU] = {
		.op_func = (nfsd4op_func)nfsd4_locku,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_LOCKU",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_status_stateid_rsize,
		.op_get_currentstateid = (stateid_getter)nfsd4_get_lockustateid,
	},
	[OP_LOOKUP] = {
		.op_func = (nfsd4op_func)nfsd4_lookup,
		.op_flags = OP_HANDLES_WRONGSEC | OP_CLEAR_STATEID,
		.op_name = "OP_LOOKUP",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_LOOKUPP] = {
		.op_func = (nfsd4op_func)nfsd4_lookupp,
		.op_flags = OP_HANDLES_WRONGSEC | OP_CLEAR_STATEID,
		.op_name = "OP_LOOKUPP",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_NVERIFY] = {
		.op_func = (nfsd4op_func)nfsd4_nverify,
		.op_name = "OP_NVERIFY",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_OPEN] = {
		.op_func = (nfsd4op_func)nfsd4_open,
		.op_flags = OP_HANDLES_WRONGSEC | OP_MODIFIES_SOMETHING,
		.op_name = "OP_OPEN",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_open_rsize,
		.op_set_currentstateid = (stateid_setter)nfsd4_set_openstateid,
	},
	[OP_OPEN_CONFIRM] = {
		.op_func = (nfsd4op_func)nfsd4_open_confirm,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_OPEN_CONFIRM",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_status_stateid_rsize,
	},
	[OP_OPEN_DOWNGRADE] = {
		.op_func = (nfsd4op_func)nfsd4_open_downgrade,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_OPEN_DOWNGRADE",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_status_stateid_rsize,
		.op_get_currentstateid = (stateid_getter)nfsd4_get_opendowngradestateid,
		.op_set_currentstateid = (stateid_setter)nfsd4_set_opendowngradestateid,
	},
	[OP_PUTFH] = {
		.op_func = (nfsd4op_func)nfsd4_putfh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_IS_PUTFH_LIKE | OP_CLEAR_STATEID,
		.op_name = "OP_PUTFH",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_PUTPUBFH] = {
		.op_func = (nfsd4op_func)nfsd4_putrootfh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_IS_PUTFH_LIKE | OP_CLEAR_STATEID,
		.op_name = "OP_PUTPUBFH",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_PUTROOTFH] = {
		.op_func = (nfsd4op_func)nfsd4_putrootfh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_IS_PUTFH_LIKE | OP_CLEAR_STATEID,
		.op_name = "OP_PUTROOTFH",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_READ] = {
		.op_func = (nfsd4op_func)nfsd4_read,
		.op_name = "OP_READ",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_read_rsize,
		.op_get_currentstateid = (stateid_getter)nfsd4_get_readstateid,
	},
	[OP_READDIR] = {
		.op_func = (nfsd4op_func)nfsd4_readdir,
		.op_name = "OP_READDIR",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_readdir_rsize,
	},
	[OP_READLINK] = {
		.op_func = (nfsd4op_func)nfsd4_readlink,
		.op_name = "OP_READLINK",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_readlink_rsize,
	},
	[OP_REMOVE] = {
		.op_func = (nfsd4op_func)nfsd4_remove,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_REMOVE",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_remove_rsize,
	},
	[OP_RENAME] = {
		.op_func = (nfsd4op_func)nfsd4_rename,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_RENAME",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_rename_rsize,
	},
	[OP_RENEW] = {
		.op_func = (nfsd4op_func)nfsd4_renew,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_RENEW",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,

	},
	[OP_RESTOREFH] = {
		.op_func = (nfsd4op_func)nfsd4_restorefh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_IS_PUTFH_LIKE | OP_MODIFIES_SOMETHING,
		.op_name = "OP_RESTOREFH",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_SAVEFH] = {
		.op_func = (nfsd4op_func)nfsd4_savefh,
		.op_flags = OP_HANDLES_WRONGSEC | OP_MODIFIES_SOMETHING,
		.op_name = "OP_SAVEFH",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_SECINFO] = {
		.op_func = (nfsd4op_func)nfsd4_secinfo,
		.op_flags = OP_HANDLES_WRONGSEC,
		.op_name = "OP_SECINFO",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_secinfo_rsize,
	},
	[OP_SETATTR] = {
		.op_func = (nfsd4op_func)nfsd4_setattr,
		.op_name = "OP_SETATTR",
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_setattr_rsize,
		.op_get_currentstateid = (stateid_getter)nfsd4_get_setattrstateid,
	},
	[OP_SETCLIENTID] = {
		.op_func = (nfsd4op_func)nfsd4_setclientid,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_SETCLIENTID",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_setclientid_rsize,
	},
	[OP_SETCLIENTID_CONFIRM] = {
		.op_func = (nfsd4op_func)nfsd4_setclientid_confirm,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_SETCLIENTID_CONFIRM",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_VERIFY] = {
		.op_func = (nfsd4op_func)nfsd4_verify,
		.op_name = "OP_VERIFY",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_WRITE] = {
		.op_func = (nfsd4op_func)nfsd4_write,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_WRITE",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_write_rsize,
		.op_get_currentstateid = (stateid_getter)nfsd4_get_writestateid,
	},
	[OP_RELEASE_LOCKOWNER] = {
		.op_func = (nfsd4op_func)nfsd4_release_lockowner,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_RELEASE_LOCKOWNER",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},

	/* NFSv4.1 operations */
	[OP_EXCHANGE_ID] = {
		.op_func = (nfsd4op_func)nfsd4_exchange_id,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_EXCHANGE_ID",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_exchange_id_rsize,
	},
	[OP_BACKCHANNEL_CTL] = {
		.op_func = (nfsd4op_func)nfsd4_backchannel_ctl,
		.op_flags = ALLOWED_WITHOUT_FH | OP_MODIFIES_SOMETHING,
		.op_name = "OP_BACKCHANNEL_CTL",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_BIND_CONN_TO_SESSION] = {
		.op_func = (nfsd4op_func)nfsd4_bind_conn_to_session,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_BIND_CONN_TO_SESSION",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_bind_conn_to_session_rsize,
	},
	[OP_CREATE_SESSION] = {
		.op_func = (nfsd4op_func)nfsd4_create_session,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_CREATE_SESSION",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_create_session_rsize,
	},
	[OP_DESTROY_SESSION] = {
		.op_func = (nfsd4op_func)nfsd4_destroy_session,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_DESTROY_SESSION",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_SEQUENCE] = {
		.op_func = (nfsd4op_func)nfsd4_sequence,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP,
		.op_name = "OP_SEQUENCE",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_sequence_rsize,
	},
	[OP_DESTROY_CLIENTID] = {
		.op_func = (nfsd4op_func)nfsd4_destroy_clientid,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_AS_FIRST_OP
				| OP_MODIFIES_SOMETHING,
		.op_name = "OP_DESTROY_CLIENTID",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_RECLAIM_COMPLETE] = {
		.op_func = (nfsd4op_func)nfsd4_reclaim_complete,
		.op_flags = ALLOWED_WITHOUT_FH | OP_MODIFIES_SOMETHING,
		.op_name = "OP_RECLAIM_COMPLETE",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_SECINFO_NO_NAME] = {
		.op_func = (nfsd4op_func)nfsd4_secinfo_no_name,
		.op_flags = OP_HANDLES_WRONGSEC,
		.op_name = "OP_SECINFO_NO_NAME",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_secinfo_rsize,
	},
	[OP_TEST_STATEID] = {
		.op_func = (nfsd4op_func)nfsd4_test_stateid,
		.op_flags = ALLOWED_WITHOUT_FH,
		.op_name = "OP_TEST_STATEID",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_test_stateid_rsize,
	},
	[OP_FREE_STATEID] = {
		.op_func = (nfsd4op_func)nfsd4_free_stateid,
		.op_flags = ALLOWED_WITHOUT_FH | OP_MODIFIES_SOMETHING,
		.op_name = "OP_FREE_STATEID",
		.op_get_currentstateid = (stateid_getter)nfsd4_get_freestateid,
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
#ifdef CONFIG_NFSD_PNFS
	[OP_GETDEVICEINFO] = {
		.op_func = (nfsd4op_func)nfsd4_getdeviceinfo,
		.op_flags = ALLOWED_WITHOUT_FH,
		.op_name = "OP_GETDEVICEINFO",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_getdeviceinfo_rsize,
	},
	[OP_LAYOUTGET] = {
		.op_func = (nfsd4op_func)nfsd4_layoutget,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_LAYOUTGET",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_layoutget_rsize,
	},
	[OP_LAYOUTCOMMIT] = {
		.op_func = (nfsd4op_func)nfsd4_layoutcommit,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_LAYOUTCOMMIT",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_layoutcommit_rsize,
	},
	[OP_LAYOUTRETURN] = {
		.op_func = (nfsd4op_func)nfsd4_layoutreturn,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_LAYOUTRETURN",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_layoutreturn_rsize,
	},
#endif /* CONFIG_NFSD_PNFS */

	/* NFSv4.2 operations */
	[OP_ALLOCATE] = {
		.op_func = (nfsd4op_func)nfsd4_allocate,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_ALLOCATE",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_DEALLOCATE] = {
		.op_func = (nfsd4op_func)nfsd4_deallocate,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_DEALLOCATE",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_CLONE] = {
		.op_func = (nfsd4op_func)nfsd4_clone,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_CLONE",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_COPY] = {
		.op_func = (nfsd4op_func)nfsd4_copy,
		.op_flags = OP_MODIFIES_SOMETHING | OP_CACHEME,
		.op_name = "OP_COPY",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_copy_rsize,
	},
	[OP_SEEK] = {
		.op_func = (nfsd4op_func)nfsd4_seek,
		.op_name = "OP_SEEK",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_seek_rsize,
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

	if (cstate->spo_must_allowed == true)
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

static struct svc_procedure		nfsd_procedures4[2] = {
	[NFSPROC4_NULL] = {
		.pc_func = nfsd4_proc_null,
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

struct svc_version	nfsd_version4 = {
	.vs_vers		= 4,
	.vs_nproc		= 2,
	.vs_proc		= nfsd_procedures4,
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
