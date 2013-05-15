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
#include <linux/slab.h>

#include "idmap.h"
#include "cache.h"
#include "xdr4.h"
#include "vfs.h"
#include "current_stateid.h"
#include "netns.h"

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

	/*
	 * Check about attributes are supported by the NFSv4 server or not.
	 * According to spec, unsupported attributes return ERR_ATTRNOTSUPP.
	 */
	if ((bmval[0] & ~nfsd_suppattrs0(cstate->minorversion)) ||
	    (bmval[1] & ~nfsd_suppattrs1(cstate->minorversion)) ||
	    (bmval[2] & ~nfsd_suppattrs2(cstate->minorversion)))
		return nfserr_attrnotsupp;

	/*
	 * Check FATTR4_WORD0_ACL can be supported
	 * in current environment or not.
	 */
	if (bmval[0] & FATTR4_WORD0_ACL) {
		if (!IS_POSIXACL(dentry->d_inode))
			return nfserr_attrnotsupp;
	}

	/*
	 * According to spec, read-only attributes return ERR_INVAL.
	 */
	if (writable) {
		if ((bmval[0] & ~writable[0]) || (bmval[1] & ~writable[1]) ||
		    (bmval[2] & ~writable[2]))
			return nfserr_inval;
	}

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
		cache_get(&src->fh_export->h);
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
	umode_t mode = fh->fh_dentry->d_inode->i_mode;

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
do_open_lookup(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate, struct nfsd4_open *open)
{
	struct svc_fh *current_fh = &cstate->current_fh;
	struct svc_fh *resfh;
	int accmode;
	__be32 status;

	resfh = kmalloc(sizeof(struct svc_fh), GFP_KERNEL);
	if (!resfh)
		return nfserr_jukebox;
	fh_init(resfh, NFS4_FHSIZE);
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
					resfh, open->op_createmode,
					(u32 *)open->op_verf.data,
					&open->op_truncate, &open->op_created);

		/*
		 * Following rfc 3530 14.2.16, use the returned bitmask
		 * to indicate which attributes we used to store the
		 * verifier:
		 */
		if (open->op_createmode == NFS4_CREATE_EXCLUSIVE && status == 0)
			open->op_bmval[1] = (FATTR4_WORD1_TIME_ACCESS |
							FATTR4_WORD1_TIME_MODIFY);
	} else {
		status = nfsd_lookup(rqstp, current_fh,
				     open->op_fname.data, open->op_fname.len, resfh);
		fh_unlock(current_fh);
	}
	if (status)
		goto out;
	status = nfsd_check_obj_isreg(resfh);
	if (status)
		goto out;

	if (is_create_with_attrs(open) && open->op_acl != NULL)
		do_set_nfs4_acl(rqstp, resfh, open->op_acl, open->op_bmval);

	nfsd4_set_open_owner_reply_cache(cstate, open, resfh);
	accmode = NFSD_MAY_NOP;
	if (open->op_created)
		accmode |= NFSD_MAY_OWNER_OVERRIDE;
	status = do_open_permission(rqstp, resfh, open, accmode);
	set_change_info(&open->op_cinfo, current_fh);
	fh_dup2(current_fh, resfh);
out:
	fh_put(resfh);
	kfree(resfh);
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
	struct nfsd4_compoundres *resp;
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

	nfs4_lock_state();

	/* check seqid for replay. set nfs4_owner */
	resp = rqstp->rq_resp;
	status = nfsd4_process_open1(&resp->cstate, open, nn);
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
	if (locks_in_grace(net) && open->op_claim_type != NFS4_OPEN_CLAIM_PREVIOUS)
		goto out;
	status = nfserr_no_grace;
	if (!locks_in_grace(net) && open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS)
		goto out;

	switch (open->op_claim_type) {
		case NFS4_OPEN_CLAIM_DELEGATE_CUR:
		case NFS4_OPEN_CLAIM_NULL:
			status = do_open_lookup(rqstp, cstate, open);
			if (status)
				goto out;
			break;
		case NFS4_OPEN_CLAIM_PREVIOUS:
			open->op_openowner->oo_flags |= NFS4_OO_CONFIRMED;
			status = nfs4_check_open_reclaim(&open->op_clientid,
							 cstate->minorversion,
							 nn);
			if (status)
				goto out;
		case NFS4_OPEN_CLAIM_FH:
		case NFS4_OPEN_CLAIM_DELEG_CUR_FH:
			status = do_open_fhandle(rqstp, cstate, open);
			if (status)
				goto out;
			break;
		case NFS4_OPEN_CLAIM_DELEG_PREV_FH:
             	case NFS4_OPEN_CLAIM_DELEGATE_PREV:
			open->op_openowner->oo_flags |= NFS4_OO_CONFIRMED;
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
	status = nfsd4_process_open2(rqstp, &cstate->current_fh, open);
	WARN_ON(status && open->op_created);
out:
	nfsd4_cleanup_open_state(open, status);
	if (open->op_openowner && !nfsd4_has_session(cstate))
		cstate->replay_owner = &open->op_openowner->oo_owner;
	nfsd4_bump_seqid(cstate, status);
	if (!cstate->replay_owner)
		nfs4_unlock_state();
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

	verf[0] = (__be32)nn->nfssvc_boot.tv_sec;
	verf[1] = (__be32)nn->nfssvc_boot.tv_usec;
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

	status = fh_verify(rqstp, &cstate->current_fh, S_IFDIR,
			   NFSD_MAY_CREATE);
	if (status)
		return status;

	status = check_attr_support(rqstp, cstate, create->cr_bmval,
				    nfsd_attrmask);
	if (status)
		return status;

	switch (create->cr_type) {
	case NF4LNK:
		/* ugh! we have to null-terminate the linktext, or
		 * vfs_symlink() will choke.  it is always safe to
		 * null-terminate by brute force, since at worst we
		 * will overwrite the first byte of the create namelen
		 * in the XDR buffer, which has already been extracted
		 * during XDR decode.
		 */
		create->cr_linkname[create->cr_linklen] = 0;

		status = nfsd_symlink(rqstp, &cstate->current_fh,
				      create->cr_name, create->cr_namelen,
				      create->cr_linkname, create->cr_linklen,
				      &resfh, &create->cr_iattr);
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

	getattr->ga_bmval[0] &= nfsd_suppattrs0(cstate->minorversion);
	getattr->ga_bmval[1] &= nfsd_suppattrs1(cstate->minorversion);
	getattr->ga_bmval[2] &= nfsd_suppattrs2(cstate->minorversion);

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

	/* no need to check permission - this will be done in nfsd_read() */

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
		rqstp->rq_splice_ok = false;

	nfs4_lock_state();
	/* check stateid */
	if ((status = nfs4_preprocess_stateid_op(SVC_NET(rqstp),
						 cstate, &read->rd_stateid,
						 RD_STATE, &read->rd_filp))) {
		dprintk("NFSD: nfsd4_read: couldn't process stateid!\n");
		goto out;
	}
	if (read->rd_filp)
		get_file(read->rd_filp);
	status = nfs_ok;
out:
	nfs4_unlock_state();
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

	readdir->rd_bmval[0] &= nfsd_suppattrs0(cstate->minorversion);
	readdir->rd_bmval[1] &= nfsd_suppattrs1(cstate->minorversion);
	readdir->rd_bmval[2] &= nfsd_suppattrs2(cstate->minorversion);

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

	if (locks_in_grace(SVC_NET(rqstp)))
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
	if (locks_in_grace(SVC_NET(rqstp)) &&
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
	struct svc_fh resfh;
	struct svc_export *exp;
	struct dentry *dentry;
	__be32 err;

	fh_init(&resfh, NFS4_FHSIZE);
	err = fh_verify(rqstp, &cstate->current_fh, S_IFDIR, NFSD_MAY_EXEC);
	if (err)
		return err;
	err = nfsd_lookup_dentry(rqstp, &cstate->current_fh,
				    secinfo->si_name, secinfo->si_namelen,
				    &exp, &dentry);
	if (err)
		return err;
	if (dentry->d_inode == NULL) {
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
	exp_get(cstate->current_fh.fh_export);
	sin->sin_exp = cstate->current_fh.fh_export;
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
		nfs4_lock_state();
		status = nfs4_preprocess_stateid_op(SVC_NET(rqstp), cstate,
			&setattr->sa_stateid, WR_STATE, NULL);
		nfs4_unlock_state();
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

	/* no need to check permission - this will be done in nfsd_write() */

	if (write->wr_offset >= OFFSET_MAX)
		return nfserr_inval;

	nfs4_lock_state();
	status = nfs4_preprocess_stateid_op(SVC_NET(rqstp),
					cstate, stateid, WR_STATE, &filp);
	if (status) {
		nfs4_unlock_state();
		dprintk("NFSD: nfsd4_write: couldn't process stateid!\n");
		return status;
	}
	if (filp)
		get_file(filp);
	nfs4_unlock_state();

	cnt = write->wr_buflen;
	write->wr_how_written = write->wr_stable_how;
	gen_boot_verifier(&write->wr_verifier, SVC_NET(rqstp));

	nvecs = fill_in_write_vector(rqstp->rq_vec, write);
	WARN_ON_ONCE(nvecs > ARRAY_SIZE(rqstp->rq_vec));

	status =  nfsd_write(rqstp, &cstate->current_fh, filp,
			     write->wr_offset, rqstp->rq_vec, nvecs,
			     &cnt, &write->wr_how_written);
	if (filp)
		fput(filp);

	write->wr_bytes_written = cnt;

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
	status = nfsd4_encode_fattr(&cstate->current_fh,
				    cstate->current_fh.fh_export,
				    cstate->current_fh.fh_dentry, &p,
				    count, verify->ve_bmval,
				    rqstp, 0);

	/* this means that nfsd4_encode_fattr() ran out of space */
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

/*
 * NULL call.
 */
static __be32
nfsd4_proc_null(struct svc_rqst *rqstp, void *argp, void *resp)
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

#ifdef NFSD_DEBUG
static const char *nfsd4_op_name(unsigned opnum);
#endif

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

	nextd = OPDESC(next);
	/*
	 * Rest of 2.6.3.1.1: certain operations will return WRONGSEC
	 * errors themselves as necessary; others should check for them
	 * now:
	 */
	return !(nextd->op_flags & OP_HANDLES_WRONGSEC);
}

/*
 * COMPOUND call.
 */
static __be32
nfsd4_proc_compound(struct svc_rqst *rqstp,
		    struct nfsd4_compoundargs *args,
		    struct nfsd4_compoundres *resp)
{
	struct nfsd4_op	*op;
	struct nfsd4_operation *opdesc;
	struct nfsd4_compound_state *cstate = &resp->cstate;
	int		slack_bytes;
	u32		plen = 0;
	__be32		status;

	resp->xbuf = &rqstp->rq_res;
	resp->p = rqstp->rq_res.head[0].iov_base +
						rqstp->rq_res.head[0].iov_len;
	resp->tagp = resp->p;
	/* reserve space for: taglen, tag, and opcnt */
	resp->p += 2 + XDR_QUADLEN(args->taglen);
	resp->end = rqstp->rq_res.head[0].iov_base + PAGE_SIZE;
	resp->taglen = args->taglen;
	resp->tag = args->tag;
	resp->opcnt = 0;
	resp->rqstp = rqstp;
	resp->cstate.minorversion = args->minorversion;
	resp->cstate.replay_owner = NULL;
	resp->cstate.session = NULL;
	fh_init(&resp->cstate.current_fh, NFS4_FHSIZE);
	fh_init(&resp->cstate.save_fh, NFS4_FHSIZE);
	/*
	 * Don't use the deferral mechanism for NFSv4; compounds make it
	 * too hard to avoid non-idempotency problems.
	 */
	rqstp->rq_usedeferral = 0;

	/*
	 * According to RFC3010, this takes precedence over all other errors.
	 */
	status = nfserr_minor_vers_mismatch;
	if (args->minorversion > nfsd_supported_minorversion)
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

		/* We must be able to encode a successful response to
		 * this operation, with enough room left over to encode a
		 * failed response to the next operation.  If we don't
		 * have enough room, fail with ERR_RESOURCE.
		 */
		slack_bytes = (char *)resp->end - (char *)resp->p;
		if (slack_bytes < COMPOUND_SLACK_SPACE
				+ COMPOUND_ERR_SLACK_SPACE) {
			BUG_ON(slack_bytes < COMPOUND_ERR_SLACK_SPACE);
			op->status = nfserr_resource;
			goto encode_op;
		}

		opdesc = OPDESC(op);

		if (!cstate->current_fh.fh_dentry) {
			if (!(opdesc->op_flags & ALLOWED_WITHOUT_FH)) {
				op->status = nfserr_nofilehandle;
				goto encode_op;
			}
		} else if (cstate->current_fh.fh_export->ex_fslocs.migrated &&
			  !(opdesc->op_flags & ALLOWED_ON_ABSENT_FS)) {
			op->status = nfserr_moved;
			goto encode_op;
		}

		/* If op is non-idempotent */
		if (opdesc->op_flags & OP_MODIFIES_SOMETHING) {
			plen = opdesc->op_rsize_bop(rqstp, op);
			op->status = nfsd4_check_resp_size(resp, plen);
		}

		if (op->status)
			goto encode_op;

		if (opdesc->op_get_currentstateid)
			opdesc->op_get_currentstateid(cstate, &op->u);
		op->status = opdesc->op_func(rqstp, cstate, &op->u);

		if (!op->status) {
			if (opdesc->op_set_currentstateid)
				opdesc->op_set_currentstateid(cstate, &op->u);

			if (opdesc->op_flags & OP_CLEAR_STATEID)
				clear_current_stateid(cstate);

			if (need_wrongsec_check(rqstp))
				op->status = check_nfsd_access(cstate->current_fh.fh_export, rqstp);
		}

encode_op:
		/* Only from SEQUENCE */
		if (resp->cstate.status == nfserr_replay_cache) {
			dprintk("%s NFS4.1 replay from cache\n", __func__);
			status = op->status;
			goto out;
		}
		if (op->status == nfserr_replay_me) {
			op->replay = &cstate->replay_owner->so_replay;
			nfsd4_encode_replay(resp, op);
			status = op->status = op->replay->rp_status;
		} else {
			nfsd4_encode_operation(resp, op);
			status = op->status;
		}

		dprintk("nfsv4 compound op %p opcnt %d #%d: %d: status %d\n",
			args->ops, args->opcnt, resp->opcnt, op->opnum,
			be32_to_cpu(status));

		if (cstate->replay_owner) {
			nfs4_unlock_state();
			cstate->replay_owner = NULL;
		}
		/* XXX Ugh, we need to get rid of this kind of special case: */
		if (op->opnum == OP_READ && op->u.read.rd_filp)
			fput(op->u.read.rd_filp);

		nfsd4_increment_op_stats(op->opnum);
	}

	resp->cstate.status = status;
	fh_put(&resp->cstate.current_fh);
	fh_put(&resp->cstate.save_fh);
	BUG_ON(resp->cstate.replay_owner);
out:
	/* Reset deferral mechanism for RPC deferrals */
	rqstp->rq_usedeferral = 1;
	dprintk("nfsv4 compound returned %d\n", ntohl(status));
	return status;
}

#define op_encode_hdr_size		(2)
#define op_encode_stateid_maxsz		(XDR_QUADLEN(NFS4_STATEID_SIZE))
#define op_encode_verifier_maxsz	(XDR_QUADLEN(NFS4_VERIFIER_SIZE))
#define op_encode_change_info_maxsz	(5)
#define nfs4_fattr_bitmap_maxsz		(4)

#define op_encode_lockowner_maxsz	(1 + XDR_QUADLEN(IDMAP_NAMESZ))
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

static inline u32 nfsd4_commit_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_verifier_maxsz) * sizeof(__be32);
}

static inline u32 nfsd4_create_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_change_info_maxsz
		+ nfs4_fattr_bitmap_maxsz) * sizeof(__be32);
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
	rlen = op->u.read.rd_length;

	if (rlen > maxcount)
		rlen = maxcount;

	return (op_encode_hdr_size + 2) * sizeof(__be32) + rlen;
}

static inline u32 nfsd4_readdir_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	u32 rlen = op->u.readdir.rd_maxcount;

	if (rlen > PAGE_SIZE)
		rlen = PAGE_SIZE;

	return (op_encode_hdr_size + op_encode_verifier_maxsz)
		 * sizeof(__be32) + rlen;
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

static inline u32 nfsd4_setattr_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + nfs4_fattr_bitmap_maxsz) * sizeof(__be32);
}

static inline u32 nfsd4_setclientid_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + 2 + 1024) * sizeof(__be32);
}

static inline u32 nfsd4_write_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + op_encode_verifier_maxsz) * sizeof(__be32);
}

static inline u32 nfsd4_exchange_id_rsize(struct svc_rqst *rqstp, struct nfsd4_op *op)
{
	return (op_encode_hdr_size + 2 + 1 + /* eir_clientid, eir_sequenceid */\
		1 + 1 + 0 + /* eir_flags, spr_how, SP4_NONE (for now) */\
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

static struct nfsd4_operation nfsd4_ops[] = {
	[OP_ACCESS] = {
		.op_func = (nfsd4op_func)nfsd4_access,
		.op_name = "OP_ACCESS",
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
		.op_name = "OP_GETATTR",
	},
	[OP_GETFH] = {
		.op_func = (nfsd4op_func)nfsd4_getfh,
		.op_name = "OP_GETFH",
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
	},
	[OP_LOOKUPP] = {
		.op_func = (nfsd4op_func)nfsd4_lookupp,
		.op_flags = OP_HANDLES_WRONGSEC | OP_CLEAR_STATEID,
		.op_name = "OP_LOOKUPP",
	},
	[OP_NVERIFY] = {
		.op_func = (nfsd4op_func)nfsd4_nverify,
		.op_name = "OP_NVERIFY",
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
				| OP_IS_PUTFH_LIKE | OP_MODIFIES_SOMETHING
				| OP_CLEAR_STATEID,
		.op_name = "OP_PUTFH",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_PUTPUBFH] = {
		.op_func = (nfsd4op_func)nfsd4_putrootfh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_IS_PUTFH_LIKE | OP_MODIFIES_SOMETHING
				| OP_CLEAR_STATEID,
		.op_name = "OP_PUTPUBFH",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_PUTROOTFH] = {
		.op_func = (nfsd4op_func)nfsd4_putrootfh,
		.op_flags = ALLOWED_WITHOUT_FH | ALLOWED_ON_ABSENT_FS
				| OP_IS_PUTFH_LIKE | OP_MODIFIES_SOMETHING
				| OP_CLEAR_STATEID,
		.op_name = "OP_PUTROOTFH",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
	[OP_READ] = {
		.op_func = (nfsd4op_func)nfsd4_read,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_READ",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_read_rsize,
		.op_get_currentstateid = (stateid_getter)nfsd4_get_readstateid,
	},
	[OP_READDIR] = {
		.op_func = (nfsd4op_func)nfsd4_readdir,
		.op_flags = OP_MODIFIES_SOMETHING,
		.op_name = "OP_READDIR",
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_readdir_rsize,
	},
	[OP_READLINK] = {
		.op_func = (nfsd4op_func)nfsd4_readlink,
		.op_name = "OP_READLINK",
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
	},
	[OP_TEST_STATEID] = {
		.op_func = (nfsd4op_func)nfsd4_test_stateid,
		.op_flags = ALLOWED_WITHOUT_FH,
		.op_name = "OP_TEST_STATEID",
	},
	[OP_FREE_STATEID] = {
		.op_func = (nfsd4op_func)nfsd4_free_stateid,
		.op_flags = ALLOWED_WITHOUT_FH | OP_MODIFIES_SOMETHING,
		.op_name = "OP_FREE_STATEID",
		.op_get_currentstateid = (stateid_getter)nfsd4_get_freestateid,
		.op_rsize_bop = (nfsd4op_rsize)nfsd4_only_status_rsize,
	},
};

#ifdef NFSD_DEBUG
static const char *nfsd4_op_name(unsigned opnum)
{
	if (opnum < ARRAY_SIZE(nfsd4_ops))
		return nfsd4_ops[opnum].op_name;
	return "unknown_operation";
}
#endif

#define nfsd4_voidres			nfsd4_voidargs
struct nfsd4_voidargs { int dummy; };

static struct svc_procedure		nfsd_procedures4[2] = {
	[NFSPROC4_NULL] = {
		.pc_func = (svc_procfunc) nfsd4_proc_null,
		.pc_encode = (kxdrproc_t) nfs4svc_encode_voidres,
		.pc_argsize = sizeof(struct nfsd4_voidargs),
		.pc_ressize = sizeof(struct nfsd4_voidres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = 1,
	},
	[NFSPROC4_COMPOUND] = {
		.pc_func = (svc_procfunc) nfsd4_proc_compound,
		.pc_decode = (kxdrproc_t) nfs4svc_decode_compoundargs,
		.pc_encode = (kxdrproc_t) nfs4svc_encode_compoundres,
		.pc_argsize = sizeof(struct nfsd4_compoundargs),
		.pc_ressize = sizeof(struct nfsd4_compoundres),
		.pc_release = nfsd4_release_compoundargs,
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = NFSD_BUFSIZE/4,
	},
};

struct svc_version	nfsd_version4 = {
		.vs_vers	= 4,
		.vs_nproc	= 2,
		.vs_proc	= nfsd_procedures4,
		.vs_dispatch	= nfsd_dispatch,
		.vs_xdrsize	= NFS4_SVC_XDRSIZE,
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
