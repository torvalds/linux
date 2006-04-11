/*
 *  fs/nfsd/nfs4proc.c
 *
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
 *
 * Note: some routines in this file are just trivial wrappers
 * (e.g. nfsd4_lookup()) defined solely for the sake of consistent
 * naming.  Since all such routines have been declared "inline",
 * there shouldn't be any associated overhead.  At some point in
 * the future, I might inline these "by hand" to clean up a
 * little.
 */

#include <linux/param.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/file.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>
#include <linux/nfs4_acl.h>

#define NFSDDBG_FACILITY		NFSDDBG_PROC

static inline void
fh_dup2(struct svc_fh *dst, struct svc_fh *src)
{
	fh_put(dst);
	dget(src->fh_dentry);
	if (src->fh_export)
		cache_get(&src->fh_export->h);
	*dst = *src;
}

static int
do_open_permission(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	int accmode, status;

	if (open->op_truncate &&
		!(open->op_share_access & NFS4_SHARE_ACCESS_WRITE))
		return nfserr_inval;

	accmode = MAY_NOP;
	if (open->op_share_access & NFS4_SHARE_ACCESS_READ)
		accmode = MAY_READ;
	if (open->op_share_deny & NFS4_SHARE_ACCESS_WRITE)
		accmode |= (MAY_WRITE | MAY_TRUNC);
	accmode |= MAY_OWNER_OVERRIDE;

	status = fh_verify(rqstp, current_fh, S_IFREG, accmode);

	return status;
}

static int
do_open_lookup(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	struct svc_fh resfh;
	int status;

	fh_init(&resfh, NFS4_FHSIZE);
	open->op_truncate = 0;

	if (open->op_create) {
		/*
		 * Note: create modes (UNCHECKED,GUARDED...) are the same
		 * in NFSv4 as in v3.
		 */
		status = nfsd_create_v3(rqstp, current_fh, open->op_fname.data,
					open->op_fname.len, &open->op_iattr,
					&resfh, open->op_createmode,
					(u32 *)open->op_verf.data, &open->op_truncate);
	}
	else {
		status = nfsd_lookup(rqstp, current_fh,
				     open->op_fname.data, open->op_fname.len, &resfh);
		fh_unlock(current_fh);
	}

	if (!status) {
		set_change_info(&open->op_cinfo, current_fh);

		/* set reply cache */
		fh_dup2(current_fh, &resfh);
		open->op_stateowner->so_replay.rp_openfh_len =
			resfh.fh_handle.fh_size;
		memcpy(open->op_stateowner->so_replay.rp_openfh,
				&resfh.fh_handle.fh_base,
				resfh.fh_handle.fh_size);

		status = do_open_permission(rqstp, current_fh, open);
	}

	fh_put(&resfh);
	return status;
}

static int
do_open_fhandle(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	int status;

	/* Only reclaims from previously confirmed clients are valid */
	if ((status = nfs4_check_open_reclaim(&open->op_clientid)))
		return status;

	/* We don't know the target directory, and therefore can not
	* set the change info
	*/

	memset(&open->op_cinfo, 0, sizeof(struct nfsd4_change_info));

	/* set replay cache */
	open->op_stateowner->so_replay.rp_openfh_len = current_fh->fh_handle.fh_size;
	memcpy(open->op_stateowner->so_replay.rp_openfh,
		&current_fh->fh_handle.fh_base,
		current_fh->fh_handle.fh_size);

	open->op_truncate = (open->op_iattr.ia_valid & ATTR_SIZE) &&
		(open->op_iattr.ia_size == 0);

	status = do_open_permission(rqstp, current_fh, open);

	return status;
}


static inline int
nfsd4_open(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open, struct nfs4_stateowner **replay_owner)
{
	int status;
	dprintk("NFSD: nfsd4_open filename %.*s op_stateowner %p\n",
		(int)open->op_fname.len, open->op_fname.data,
		open->op_stateowner);

	/* This check required by spec. */
	if (open->op_create && open->op_claim_type != NFS4_OPEN_CLAIM_NULL)
		return nfserr_inval;

	nfs4_lock_state();

	/* check seqid for replay. set nfs4_owner */
	status = nfsd4_process_open1(open);
	if (status == NFSERR_REPLAY_ME) {
		struct nfs4_replay *rp = &open->op_stateowner->so_replay;
		fh_put(current_fh);
		current_fh->fh_handle.fh_size = rp->rp_openfh_len;
		memcpy(&current_fh->fh_handle.fh_base, rp->rp_openfh,
				rp->rp_openfh_len);
		status = fh_verify(rqstp, current_fh, 0, MAY_NOP);
		if (status)
			dprintk("nfsd4_open: replay failed"
				" restoring previous filehandle\n");
		else
			status = NFSERR_REPLAY_ME;
	}
	if (status)
		goto out;

	/* Openowner is now set, so sequence id will get bumped.  Now we need
	 * these checks before we do any creates: */
	status = nfserr_grace;
	if (nfs4_in_grace() && open->op_claim_type != NFS4_OPEN_CLAIM_PREVIOUS)
		goto out;
	status = nfserr_no_grace;
	if (!nfs4_in_grace() && open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS)
		goto out;

	switch (open->op_claim_type) {
		case NFS4_OPEN_CLAIM_DELEGATE_CUR:
			status = nfserr_inval;
			if (open->op_create)
				goto out;
			/* fall through */
		case NFS4_OPEN_CLAIM_NULL:
			/*
			 * (1) set CURRENT_FH to the file being opened,
			 * creating it if necessary, (2) set open->op_cinfo,
			 * (3) set open->op_truncate if the file is to be
			 * truncated after opening, (4) do permission checking.
			 */
			status = do_open_lookup(rqstp, current_fh, open);
			if (status)
				goto out;
			break;
		case NFS4_OPEN_CLAIM_PREVIOUS:
			open->op_stateowner->so_confirmed = 1;
			/*
			 * The CURRENT_FH is already set to the file being
			 * opened.  (1) set open->op_cinfo, (2) set
			 * open->op_truncate if the file is to be truncated
			 * after opening, (3) do permission checking.
			*/
			status = do_open_fhandle(rqstp, current_fh, open);
			if (status)
				goto out;
			break;
             	case NFS4_OPEN_CLAIM_DELEGATE_PREV:
			open->op_stateowner->so_confirmed = 1;
			printk("NFSD: unsupported OPEN claim type %d\n",
				open->op_claim_type);
			status = nfserr_notsupp;
			goto out;
		default:
			printk("NFSD: Invalid OPEN claim type %d\n",
				open->op_claim_type);
			status = nfserr_inval;
			goto out;
	}
	/*
	 * nfsd4_process_open2() does the actual opening of the file.  If
	 * successful, it (1) truncates the file if open->op_truncate was
	 * set, (2) sets open->op_stateid, (3) sets open->op_delegation.
	 */
	status = nfsd4_process_open2(rqstp, current_fh, open);
out:
	if (open->op_stateowner) {
		nfs4_get_stateowner(open->op_stateowner);
		*replay_owner = open->op_stateowner;
	}
	nfs4_unlock_state();
	return status;
}

/*
 * filehandle-manipulating ops.
 */
static inline int
nfsd4_getfh(struct svc_fh *current_fh, struct svc_fh **getfh)
{
	if (!current_fh->fh_dentry)
		return nfserr_nofilehandle;

	*getfh = current_fh;
	return nfs_ok;
}

static inline int
nfsd4_putfh(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_putfh *putfh)
{
	fh_put(current_fh);
	current_fh->fh_handle.fh_size = putfh->pf_fhlen;
	memcpy(&current_fh->fh_handle.fh_base, putfh->pf_fhval, putfh->pf_fhlen);
	return fh_verify(rqstp, current_fh, 0, MAY_NOP);
}

static inline int
nfsd4_putrootfh(struct svc_rqst *rqstp, struct svc_fh *current_fh)
{
	int status;

	fh_put(current_fh);
	status = exp_pseudoroot(rqstp->rq_client, current_fh,
			      &rqstp->rq_chandle);
	return status;
}

static inline int
nfsd4_restorefh(struct svc_fh *current_fh, struct svc_fh *save_fh)
{
	if (!save_fh->fh_dentry)
		return nfserr_restorefh;

	fh_dup2(current_fh, save_fh);
	return nfs_ok;
}

static inline int
nfsd4_savefh(struct svc_fh *current_fh, struct svc_fh *save_fh)
{
	if (!current_fh->fh_dentry)
		return nfserr_nofilehandle;

	fh_dup2(save_fh, current_fh);
	return nfs_ok;
}

/*
 * misc nfsv4 ops
 */
static inline int
nfsd4_access(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_access *access)
{
	if (access->ac_req_access & ~NFS3_ACCESS_FULL)
		return nfserr_inval;

	access->ac_resp_access = access->ac_req_access;
	return nfsd_access(rqstp, current_fh, &access->ac_resp_access, &access->ac_supported);
}

static inline int
nfsd4_commit(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_commit *commit)
{
	int status;

	u32 *p = (u32 *)commit->co_verf.data;
	*p++ = nfssvc_boot.tv_sec;
	*p++ = nfssvc_boot.tv_usec;

	status = nfsd_commit(rqstp, current_fh, commit->co_offset, commit->co_count);
	if (status == nfserr_symlink)
		status = nfserr_inval;
	return status;
}

static int
nfsd4_create(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_create *create)
{
	struct svc_fh resfh;
	int status;
	dev_t rdev;

	fh_init(&resfh, NFS4_FHSIZE);

	status = fh_verify(rqstp, current_fh, S_IFDIR, MAY_CREATE);
	if (status == nfserr_symlink)
		status = nfserr_notdir;
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

		status = nfsd_symlink(rqstp, current_fh, create->cr_name,
				      create->cr_namelen, create->cr_linkname,
				      create->cr_linklen, &resfh, &create->cr_iattr);
		break;

	case NF4BLK:
		rdev = MKDEV(create->cr_specdata1, create->cr_specdata2);
		if (MAJOR(rdev) != create->cr_specdata1 ||
		    MINOR(rdev) != create->cr_specdata2)
			return nfserr_inval;
		status = nfsd_create(rqstp, current_fh, create->cr_name,
				     create->cr_namelen, &create->cr_iattr,
				     S_IFBLK, rdev, &resfh);
		break;

	case NF4CHR:
		rdev = MKDEV(create->cr_specdata1, create->cr_specdata2);
		if (MAJOR(rdev) != create->cr_specdata1 ||
		    MINOR(rdev) != create->cr_specdata2)
			return nfserr_inval;
		status = nfsd_create(rqstp, current_fh, create->cr_name,
				     create->cr_namelen, &create->cr_iattr,
				     S_IFCHR, rdev, &resfh);
		break;

	case NF4SOCK:
		status = nfsd_create(rqstp, current_fh, create->cr_name,
				     create->cr_namelen, &create->cr_iattr,
				     S_IFSOCK, 0, &resfh);
		break;

	case NF4FIFO:
		status = nfsd_create(rqstp, current_fh, create->cr_name,
				     create->cr_namelen, &create->cr_iattr,
				     S_IFIFO, 0, &resfh);
		break;

	case NF4DIR:
		create->cr_iattr.ia_valid &= ~ATTR_SIZE;
		status = nfsd_create(rqstp, current_fh, create->cr_name,
				     create->cr_namelen, &create->cr_iattr,
				     S_IFDIR, 0, &resfh);
		break;

	default:
		status = nfserr_badtype;
	}

	if (!status) {
		fh_unlock(current_fh);
		set_change_info(&create->cr_cinfo, current_fh);
		fh_dup2(current_fh, &resfh);
	}

	fh_put(&resfh);
	return status;
}

static inline int
nfsd4_getattr(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_getattr *getattr)
{
	int status;

	status = fh_verify(rqstp, current_fh, 0, MAY_NOP);
	if (status)
		return status;

	if (getattr->ga_bmval[1] & NFSD_WRITEONLY_ATTRS_WORD1)
		return nfserr_inval;

	getattr->ga_bmval[0] &= NFSD_SUPPORTED_ATTRS_WORD0;
	getattr->ga_bmval[1] &= NFSD_SUPPORTED_ATTRS_WORD1;

	getattr->ga_fhp = current_fh;
	return nfs_ok;
}

static inline int
nfsd4_link(struct svc_rqst *rqstp, struct svc_fh *current_fh,
	   struct svc_fh *save_fh, struct nfsd4_link *link)
{
	int status = nfserr_nofilehandle;

	if (!save_fh->fh_dentry)
		return status;
	status = nfsd_link(rqstp, current_fh, link->li_name, link->li_namelen, save_fh);
	if (!status)
		set_change_info(&link->li_cinfo, current_fh);
	return status;
}

static int
nfsd4_lookupp(struct svc_rqst *rqstp, struct svc_fh *current_fh)
{
	struct svc_fh tmp_fh;
	int ret;

	fh_init(&tmp_fh, NFS4_FHSIZE);
	if((ret = exp_pseudoroot(rqstp->rq_client, &tmp_fh,
			      &rqstp->rq_chandle)) != 0)
		return ret;
	if (tmp_fh.fh_dentry == current_fh->fh_dentry) {
		fh_put(&tmp_fh);
		return nfserr_noent;
	}
	fh_put(&tmp_fh);
	return nfsd_lookup(rqstp, current_fh, "..", 2, current_fh);
}

static inline int
nfsd4_lookup(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_lookup *lookup)
{
	return nfsd_lookup(rqstp, current_fh, lookup->lo_name, lookup->lo_len, current_fh);
}

static inline int
nfsd4_read(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_read *read)
{
	int status;

	/* no need to check permission - this will be done in nfsd_read() */

	read->rd_filp = NULL;
	if (read->rd_offset >= OFFSET_MAX)
		return nfserr_inval;

	nfs4_lock_state();
	/* check stateid */
	if ((status = nfs4_preprocess_stateid_op(current_fh, &read->rd_stateid,
				CHECK_FH | RD_STATE, &read->rd_filp))) {
		dprintk("NFSD: nfsd4_read: couldn't process stateid!\n");
		goto out;
	}
	if (read->rd_filp)
		get_file(read->rd_filp);
	status = nfs_ok;
out:
	nfs4_unlock_state();
	read->rd_rqstp = rqstp;
	read->rd_fhp = current_fh;
	return status;
}

static inline int
nfsd4_readdir(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_readdir *readdir)
{
	u64 cookie = readdir->rd_cookie;
	static const nfs4_verifier zeroverf;

	/* no need to check permission - this will be done in nfsd_readdir() */

	if (readdir->rd_bmval[1] & NFSD_WRITEONLY_ATTRS_WORD1)
		return nfserr_inval;

	readdir->rd_bmval[0] &= NFSD_SUPPORTED_ATTRS_WORD0;
	readdir->rd_bmval[1] &= NFSD_SUPPORTED_ATTRS_WORD1;

	if ((cookie > ~(u32)0) || (cookie == 1) || (cookie == 2) ||
	    (cookie == 0 && memcmp(readdir->rd_verf.data, zeroverf.data, NFS4_VERIFIER_SIZE)))
		return nfserr_bad_cookie;

	readdir->rd_rqstp = rqstp;
	readdir->rd_fhp = current_fh;
	return nfs_ok;
}

static inline int
nfsd4_readlink(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_readlink *readlink)
{
	readlink->rl_rqstp = rqstp;
	readlink->rl_fhp = current_fh;
	return nfs_ok;
}

static inline int
nfsd4_remove(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_remove *remove)
{
	int status;

	if (nfs4_in_grace())
		return nfserr_grace;
	status = nfsd_unlink(rqstp, current_fh, 0, remove->rm_name, remove->rm_namelen);
	if (status == nfserr_symlink)
		return nfserr_notdir;
	if (!status) {
		fh_unlock(current_fh);
		set_change_info(&remove->rm_cinfo, current_fh);
	}
	return status;
}

static inline int
nfsd4_rename(struct svc_rqst *rqstp, struct svc_fh *current_fh,
	     struct svc_fh *save_fh, struct nfsd4_rename *rename)
{
	int status = nfserr_nofilehandle;

	if (!save_fh->fh_dentry)
		return status;
	if (nfs4_in_grace() && !(save_fh->fh_export->ex_flags
					& NFSEXP_NOSUBTREECHECK))
		return nfserr_grace;
	status = nfsd_rename(rqstp, save_fh, rename->rn_sname,
			     rename->rn_snamelen, current_fh,
			     rename->rn_tname, rename->rn_tnamelen);

	/* the underlying filesystem returns different error's than required
	 * by NFSv4. both save_fh and current_fh have been verified.. */
	if (status == nfserr_isdir)
		status = nfserr_exist;
	else if ((status == nfserr_notdir) &&
                  (S_ISDIR(save_fh->fh_dentry->d_inode->i_mode) &&
                   S_ISDIR(current_fh->fh_dentry->d_inode->i_mode)))
		status = nfserr_exist;
	else if (status == nfserr_symlink)
		status = nfserr_notdir;

	if (!status) {
		set_change_info(&rename->rn_sinfo, current_fh);
		set_change_info(&rename->rn_tinfo, save_fh);
	}
	return status;
}

static inline int
nfsd4_setattr(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_setattr *setattr)
{
	int status = nfs_ok;

	if (setattr->sa_iattr.ia_valid & ATTR_SIZE) {
		nfs4_lock_state();
		status = nfs4_preprocess_stateid_op(current_fh,
			&setattr->sa_stateid, CHECK_FH | WR_STATE, NULL);
		nfs4_unlock_state();
		if (status) {
			dprintk("NFSD: nfsd4_setattr: couldn't process stateid!");
			return status;
		}
	}
	status = nfs_ok;
	if (setattr->sa_acl != NULL)
		status = nfsd4_set_nfs4_acl(rqstp, current_fh, setattr->sa_acl);
	if (status)
		return status;
	status = nfsd_setattr(rqstp, current_fh, &setattr->sa_iattr,
				0, (time_t)0);
	return status;
}

static inline int
nfsd4_write(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_write *write)
{
	stateid_t *stateid = &write->wr_stateid;
	struct file *filp = NULL;
	u32 *p;
	int status = nfs_ok;

	/* no need to check permission - this will be done in nfsd_write() */

	if (write->wr_offset >= OFFSET_MAX)
		return nfserr_inval;

	nfs4_lock_state();
	status = nfs4_preprocess_stateid_op(current_fh, stateid,
					CHECK_FH | WR_STATE, &filp);
	if (filp)
		get_file(filp);
	nfs4_unlock_state();

	if (status) {
		dprintk("NFSD: nfsd4_write: couldn't process stateid!\n");
		return status;
	}

	write->wr_bytes_written = write->wr_buflen;
	write->wr_how_written = write->wr_stable_how;
	p = (u32 *)write->wr_verifier.data;
	*p++ = nfssvc_boot.tv_sec;
	*p++ = nfssvc_boot.tv_usec;

	status =  nfsd_write(rqstp, current_fh, filp, write->wr_offset,
			write->wr_vec, write->wr_vlen, write->wr_buflen,
			&write->wr_how_written);
	if (filp)
		fput(filp);

	if (status == nfserr_symlink)
		status = nfserr_inval;
	return status;
}

/* This routine never returns NFS_OK!  If there are no other errors, it
 * will return NFSERR_SAME or NFSERR_NOT_SAME depending on whether the
 * attributes matched.  VERIFY is implemented by mapping NFSERR_SAME
 * to NFS_OK after the call; NVERIFY by mapping NFSERR_NOT_SAME to NFS_OK.
 */
static int
nfsd4_verify(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_verify *verify)
{
	u32 *buf, *p;
	int count;
	int status;

	status = fh_verify(rqstp, current_fh, 0, MAY_NOP);
	if (status)
		return status;

	if ((verify->ve_bmval[0] & ~NFSD_SUPPORTED_ATTRS_WORD0)
	    || (verify->ve_bmval[1] & ~NFSD_SUPPORTED_ATTRS_WORD1))
		return nfserr_attrnotsupp;
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
		return nfserr_resource;

	status = nfsd4_encode_fattr(current_fh, current_fh->fh_export,
				    current_fh->fh_dentry, buf,
				    &count, verify->ve_bmval,
				    rqstp);

	/* this means that nfsd4_encode_fattr() ran out of space */
	if (status == nfserr_resource && count == 0)
		status = nfserr_not_same;
	if (status)
		goto out_kfree;

	p = buf + 3;
	status = nfserr_not_same;
	if (ntohl(*p++) != verify->ve_attrlen)
		goto out_kfree;
	if (!memcmp(p, verify->ve_attrval, verify->ve_attrlen))
		status = nfserr_same;

out_kfree:
	kfree(buf);
	return status;
}

/*
 * NULL call.
 */
static int
nfsd4_proc_null(struct svc_rqst *rqstp, void *argp, void *resp)
{
	return nfs_ok;
}


/*
 * COMPOUND call.
 */
static int
nfsd4_proc_compound(struct svc_rqst *rqstp,
		    struct nfsd4_compoundargs *args,
		    struct nfsd4_compoundres *resp)
{
	struct nfsd4_op	*op;
	struct svc_fh	*current_fh = NULL;
	struct svc_fh	*save_fh = NULL;
	struct nfs4_stateowner *replay_owner = NULL;
	int		slack_space;    /* in words, not bytes! */
	int		status;

	status = nfserr_resource;
	current_fh = kmalloc(sizeof(*current_fh), GFP_KERNEL);
	if (current_fh == NULL)
		goto out;
	fh_init(current_fh, NFS4_FHSIZE);
	save_fh = kmalloc(sizeof(*save_fh), GFP_KERNEL);
	if (save_fh == NULL)
		goto out;
	fh_init(save_fh, NFS4_FHSIZE);

	resp->xbuf = &rqstp->rq_res;
	resp->p = rqstp->rq_res.head[0].iov_base + rqstp->rq_res.head[0].iov_len;
	resp->tagp = resp->p;
	/* reserve space for: taglen, tag, and opcnt */
	resp->p += 2 + XDR_QUADLEN(args->taglen);
	resp->end = rqstp->rq_res.head[0].iov_base + PAGE_SIZE;
	resp->taglen = args->taglen;
	resp->tag = args->tag;
	resp->opcnt = 0;
	resp->rqstp = rqstp;

	/*
	 * According to RFC3010, this takes precedence over all other errors.
	 */
	status = nfserr_minor_vers_mismatch;
	if (args->minorversion > NFSD_SUPPORTED_MINOR_VERSION)
		goto out;

	status = nfs_ok;
	while (!status && resp->opcnt < args->opcnt) {
		op = &args->ops[resp->opcnt++];

		dprintk("nfsv4 compound op #%d: %d\n", resp->opcnt, op->opnum);

		/*
		 * The XDR decode routines may have pre-set op->status;
		 * for example, if there is a miscellaneous XDR error
		 * it will be set to nfserr_bad_xdr.
		 */
		if (op->status)
			goto encode_op;

		/* We must be able to encode a successful response to
		 * this operation, with enough room left over to encode a
		 * failed response to the next operation.  If we don't
		 * have enough room, fail with ERR_RESOURCE.
		 */
/* FIXME - is slack_space *really* words, or bytes??? - neilb */
		slack_space = (char *)resp->end - (char *)resp->p;
		if (slack_space < COMPOUND_SLACK_SPACE + COMPOUND_ERR_SLACK_SPACE) {
			BUG_ON(slack_space < COMPOUND_ERR_SLACK_SPACE);
			op->status = nfserr_resource;
			goto encode_op;
		}

		/* All operations except RENEW, SETCLIENTID, RESTOREFH
		* SETCLIENTID_CONFIRM, PUTFH and PUTROOTFH
		* require a valid current filehandle
		*/
		if ((!current_fh->fh_dentry) &&
		   !((op->opnum == OP_PUTFH) || (op->opnum == OP_PUTROOTFH) ||
		   (op->opnum == OP_SETCLIENTID) ||
		   (op->opnum == OP_SETCLIENTID_CONFIRM) ||
		   (op->opnum == OP_RENEW) || (op->opnum == OP_RESTOREFH) ||
		   (op->opnum == OP_RELEASE_LOCKOWNER))) {
			op->status = nfserr_nofilehandle;
			goto encode_op;
		}
		switch (op->opnum) {
		case OP_ACCESS:
			op->status = nfsd4_access(rqstp, current_fh, &op->u.access);
			break;
		case OP_CLOSE:
			op->status = nfsd4_close(rqstp, current_fh, &op->u.close, &replay_owner);
			break;
		case OP_COMMIT:
			op->status = nfsd4_commit(rqstp, current_fh, &op->u.commit);
			break;
		case OP_CREATE:
			op->status = nfsd4_create(rqstp, current_fh, &op->u.create);
			break;
		case OP_DELEGRETURN:
			op->status = nfsd4_delegreturn(rqstp, current_fh, &op->u.delegreturn);
			break;
		case OP_GETATTR:
			op->status = nfsd4_getattr(rqstp, current_fh, &op->u.getattr);
			break;
		case OP_GETFH:
			op->status = nfsd4_getfh(current_fh, &op->u.getfh);
			break;
		case OP_LINK:
			op->status = nfsd4_link(rqstp, current_fh, save_fh, &op->u.link);
			break;
		case OP_LOCK:
			op->status = nfsd4_lock(rqstp, current_fh, &op->u.lock, &replay_owner);
			break;
		case OP_LOCKT:
			op->status = nfsd4_lockt(rqstp, current_fh, &op->u.lockt);
			break;
		case OP_LOCKU:
			op->status = nfsd4_locku(rqstp, current_fh, &op->u.locku, &replay_owner);
			break;
		case OP_LOOKUP:
			op->status = nfsd4_lookup(rqstp, current_fh, &op->u.lookup);
			break;
		case OP_LOOKUPP:
			op->status = nfsd4_lookupp(rqstp, current_fh);
			break;
		case OP_NVERIFY:
			op->status = nfsd4_verify(rqstp, current_fh, &op->u.nverify);
			if (op->status == nfserr_not_same)
				op->status = nfs_ok;
			break;
		case OP_OPEN:
			op->status = nfsd4_open(rqstp, current_fh, &op->u.open, &replay_owner);
			break;
		case OP_OPEN_CONFIRM:
			op->status = nfsd4_open_confirm(rqstp, current_fh, &op->u.open_confirm, &replay_owner);
			break;
		case OP_OPEN_DOWNGRADE:
			op->status = nfsd4_open_downgrade(rqstp, current_fh, &op->u.open_downgrade, &replay_owner);
			break;
		case OP_PUTFH:
			op->status = nfsd4_putfh(rqstp, current_fh, &op->u.putfh);
			break;
		case OP_PUTROOTFH:
			op->status = nfsd4_putrootfh(rqstp, current_fh);
			break;
		case OP_READ:
			op->status = nfsd4_read(rqstp, current_fh, &op->u.read);
			break;
		case OP_READDIR:
			op->status = nfsd4_readdir(rqstp, current_fh, &op->u.readdir);
			break;
		case OP_READLINK:
			op->status = nfsd4_readlink(rqstp, current_fh, &op->u.readlink);
			break;
		case OP_REMOVE:
			op->status = nfsd4_remove(rqstp, current_fh, &op->u.remove);
			break;
		case OP_RENAME:
			op->status = nfsd4_rename(rqstp, current_fh, save_fh, &op->u.rename);
			break;
		case OP_RENEW:
			op->status = nfsd4_renew(&op->u.renew);
			break;
		case OP_RESTOREFH:
			op->status = nfsd4_restorefh(current_fh, save_fh);
			break;
		case OP_SAVEFH:
			op->status = nfsd4_savefh(current_fh, save_fh);
			break;
		case OP_SETATTR:
			op->status = nfsd4_setattr(rqstp, current_fh, &op->u.setattr);
			break;
		case OP_SETCLIENTID:
			op->status = nfsd4_setclientid(rqstp, &op->u.setclientid);
			break;
		case OP_SETCLIENTID_CONFIRM:
			op->status = nfsd4_setclientid_confirm(rqstp, &op->u.setclientid_confirm);
			break;
		case OP_VERIFY:
			op->status = nfsd4_verify(rqstp, current_fh, &op->u.verify);
			if (op->status == nfserr_same)
				op->status = nfs_ok;
			break;
		case OP_WRITE:
			op->status = nfsd4_write(rqstp, current_fh, &op->u.write);
			break;
		case OP_RELEASE_LOCKOWNER:
			op->status = nfsd4_release_lockowner(rqstp, &op->u.release_lockowner);
			break;
		default:
			BUG_ON(op->status == nfs_ok);
			break;
		}

encode_op:
		if (op->status == NFSERR_REPLAY_ME) {
			op->replay = &replay_owner->so_replay;
			nfsd4_encode_replay(resp, op);
			status = op->status = op->replay->rp_status;
		} else {
			nfsd4_encode_operation(resp, op);
			status = op->status;
		}
		if (replay_owner && (replay_owner != (void *)(-1))) {
			nfs4_put_stateowner(replay_owner);
			replay_owner = NULL;
		}
		/* XXX Ugh, we need to get rid of this kind of special case: */
		if (op->opnum == OP_READ && op->u.read.rd_filp)
			fput(op->u.read.rd_filp);
	}

out:
	nfsd4_release_compoundargs(args);
	if (current_fh)
		fh_put(current_fh);
	kfree(current_fh);
	if (save_fh)
		fh_put(save_fh);
	kfree(save_fh);
	return status;
}

#define nfs4svc_decode_voidargs		NULL
#define nfs4svc_release_void		NULL
#define nfsd4_voidres			nfsd4_voidargs
#define nfs4svc_release_compound	NULL
struct nfsd4_voidargs { int dummy; };

#define PROC(name, argt, rest, relt, cache, respsize)	\
 { (svc_procfunc) nfsd4_proc_##name,		\
   (kxdrproc_t) nfs4svc_decode_##argt##args,	\
   (kxdrproc_t) nfs4svc_encode_##rest##res,	\
   (kxdrproc_t) nfs4svc_release_##relt,		\
   sizeof(struct nfsd4_##argt##args),		\
   sizeof(struct nfsd4_##rest##res),		\
   0,						\
   cache,					\
   respsize,					\
 }

/*
 * TODO: At the present time, the NFSv4 server does not do XID caching
 * of requests.  Implementing XID caching would not be a serious problem,
 * although it would require a mild change in interfaces since one
 * doesn't know whether an NFSv4 request is idempotent until after the
 * XDR decode.  However, XID caching totally confuses pynfs (Peter
 * Astrand's regression testsuite for NFSv4 servers), which reuses
 * XID's liberally, so I've left it unimplemented until pynfs generates
 * better XID's.
 */
static struct svc_procedure		nfsd_procedures4[2] = {
  PROC(null,	 void,		void,		void,	  RC_NOCACHE, 1),
  PROC(compound, compound,	compound,	compound, RC_NOCACHE, NFSD_BUFSIZE/4)
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
