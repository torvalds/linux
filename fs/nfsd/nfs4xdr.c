/*
 *  fs/nfs/nfs4xdr.c
 *
 *  Server-side XDR for NFSv4
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
 * TODO: Neil Brown made the following observation:  We currently
 * initially reserve NFSD_BUFSIZE space on the transmit queue and
 * never release any of that until the request is complete.
 * It would be good to calculate a new maximum response size while
 * decoding the COMPOUND, and call svc_reserve with this number
 * at the end of nfs4svc_decode_compoundargs.
 */

#include <linux/param.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/vfs.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>
#include <linux/nfsd_idmap.h>
#include <linux/nfs4.h>
#include <linux/nfs4_acl.h>

#define NFSDDBG_FACILITY		NFSDDBG_XDR

static int
check_filename(char *str, int len, int err)
{
	int i;

	if (len == 0)
		return nfserr_inval;
	if (isdotent(str, len))
		return err;
	for (i = 0; i < len; i++)
		if (str[i] == '/')
			return err;
	return 0;
}

/*
 * START OF "GENERIC" DECODE ROUTINES.
 *   These may look a little ugly since they are imported from a "generic"
 * set of XDR encode/decode routines which are intended to be shared by
 * all of our NFSv4 implementations (OpenBSD, MacOS X...).
 *
 * If the pain of reading these is too great, it should be a straightforward
 * task to translate them into Linux-specific versions which are more
 * consistent with the style used in NFSv2/v3...
 */
#define DECODE_HEAD				\
	u32 *p;					\
	int status
#define DECODE_TAIL				\
	status = 0;				\
out:						\
	return status;				\
xdr_error:					\
	printk(KERN_NOTICE "xdr error! (%s:%d)\n", __FILE__, __LINE__);	\
	status = nfserr_bad_xdr;		\
	goto out

#define READ32(x)         (x) = ntohl(*p++)
#define READ64(x)         do {			\
	(x) = (u64)ntohl(*p++) << 32;		\
	(x) |= ntohl(*p++);			\
} while (0)
#define READTIME(x)       do {			\
	p++;					\
	(x) = ntohl(*p++);			\
	p++;					\
} while (0)
#define READMEM(x,nbytes) do {			\
	x = (char *)p;				\
	p += XDR_QUADLEN(nbytes);		\
} while (0)
#define SAVEMEM(x,nbytes) do {			\
	if (!(x = (p==argp->tmp || p == argp->tmpp) ? \
 		savemem(argp, p, nbytes) :	\
 		(char *)p)) {			\
		printk(KERN_NOTICE "xdr error! (%s:%d)\n", __FILE__, __LINE__); \
		goto xdr_error;			\
		}				\
	p += XDR_QUADLEN(nbytes);		\
} while (0)
#define COPYMEM(x,nbytes) do {			\
	memcpy((x), p, nbytes);			\
	p += XDR_QUADLEN(nbytes);		\
} while (0)

/* READ_BUF, read_buf(): nbytes must be <= PAGE_SIZE */
#define READ_BUF(nbytes)  do {			\
	if (nbytes <= (u32)((char *)argp->end - (char *)argp->p)) {	\
		p = argp->p;			\
		argp->p += XDR_QUADLEN(nbytes);	\
	} else if (!(p = read_buf(argp, nbytes))) { \
		printk(KERN_NOTICE "xdr error! (%s:%d)\n", __FILE__, __LINE__); \
		goto xdr_error;			\
	}					\
} while (0)

static u32 *read_buf(struct nfsd4_compoundargs *argp, int nbytes)
{
	/* We want more bytes than seem to be available.
	 * Maybe we need a new page, maybe we have just run out
	 */
	int avail = (char*)argp->end - (char*)argp->p;
	u32 *p;
	if (avail + argp->pagelen < nbytes)
		return NULL;
	if (avail + PAGE_SIZE < nbytes) /* need more than a page !! */
		return NULL;
	/* ok, we can do it with the current plus the next page */
	if (nbytes <= sizeof(argp->tmp))
		p = argp->tmp;
	else {
		if (argp->tmpp)
			kfree(argp->tmpp);
		p = argp->tmpp = kmalloc(nbytes, GFP_KERNEL);
		if (!p)
			return NULL;
		
	}
	memcpy(p, argp->p, avail);
	/* step to next page */
	argp->p = page_address(argp->pagelist[0]);
	argp->pagelist++;
	if (argp->pagelen < PAGE_SIZE) {
		argp->end = p + (argp->pagelen>>2);
		argp->pagelen = 0;
	} else {
		argp->end = p + (PAGE_SIZE>>2);
		argp->pagelen -= PAGE_SIZE;
	}
	memcpy(((char*)p)+avail, argp->p, (nbytes - avail));
	argp->p += XDR_QUADLEN(nbytes - avail);
	return p;
}

static int
defer_free(struct nfsd4_compoundargs *argp,
		void (*release)(const void *), void *p)
{
	struct tmpbuf *tb;

	tb = kmalloc(sizeof(*tb), GFP_KERNEL);
	if (!tb)
		return -ENOMEM;
	tb->buf = p;
	tb->release = release;
	tb->next = argp->to_free;
	argp->to_free = tb;
	return 0;
}

static char *savemem(struct nfsd4_compoundargs *argp, u32 *p, int nbytes)
{
	void *new = NULL;
	if (p == argp->tmp) {
		new = kmalloc(nbytes, GFP_KERNEL);
		if (!new) return NULL;
		p = new;
		memcpy(p, argp->tmp, nbytes);
	} else {
		if (p != argp->tmpp)
			BUG();
		argp->tmpp = NULL;
	}
	if (defer_free(argp, kfree, p)) {
		kfree(new);
		return NULL;
	} else
		return (char *)p;
}


static int
nfsd4_decode_bitmap(struct nfsd4_compoundargs *argp, u32 *bmval)
{
	u32 bmlen;
	DECODE_HEAD;

	bmval[0] = 0;
	bmval[1] = 0;

	READ_BUF(4);
	READ32(bmlen);
	if (bmlen > 1000)
		goto xdr_error;

	READ_BUF(bmlen << 2);
	if (bmlen > 0)
		READ32(bmval[0]);
	if (bmlen > 1)
		READ32(bmval[1]);

	DECODE_TAIL;
}

static int
nfsd4_decode_fattr(struct nfsd4_compoundargs *argp, u32 *bmval, struct iattr *iattr,
    struct nfs4_acl **acl)
{
	int expected_len, len = 0;
	u32 dummy32;
	char *buf;

	DECODE_HEAD;
	iattr->ia_valid = 0;
	if ((status = nfsd4_decode_bitmap(argp, bmval)))
		return status;

	/*
	 * According to spec, unsupported attributes return ERR_NOTSUPP;
	 * read-only attributes return ERR_INVAL.
	 */
	if ((bmval[0] & ~NFSD_SUPPORTED_ATTRS_WORD0) || (bmval[1] & ~NFSD_SUPPORTED_ATTRS_WORD1))
		return nfserr_attrnotsupp;
	if ((bmval[0] & ~NFSD_WRITEABLE_ATTRS_WORD0) || (bmval[1] & ~NFSD_WRITEABLE_ATTRS_WORD1))
		return nfserr_inval;

	READ_BUF(4);
	READ32(expected_len);

	if (bmval[0] & FATTR4_WORD0_SIZE) {
		READ_BUF(8);
		len += 8;
		READ64(iattr->ia_size);
		iattr->ia_valid |= ATTR_SIZE;
	}
	if (bmval[0] & FATTR4_WORD0_ACL) {
		int nace, i;
		struct nfs4_ace ace;

		READ_BUF(4); len += 4;
		READ32(nace);

		*acl = nfs4_acl_new();
		if (*acl == NULL) {
			status = -ENOMEM;
			goto out_nfserr;
		}
		defer_free(argp, (void (*)(const void *))nfs4_acl_free, *acl);

		for (i = 0; i < nace; i++) {
			READ_BUF(16); len += 16;
			READ32(ace.type);
			READ32(ace.flag);
			READ32(ace.access_mask);
			READ32(dummy32);
			READ_BUF(dummy32);
			len += XDR_QUADLEN(dummy32) << 2;
			READMEM(buf, dummy32);
			ace.whotype = nfs4_acl_get_whotype(buf, dummy32);
			status = 0;
			if (ace.whotype != NFS4_ACL_WHO_NAMED)
				ace.who = 0;
			else if (ace.flag & NFS4_ACE_IDENTIFIER_GROUP)
				status = nfsd_map_name_to_gid(argp->rqstp,
						buf, dummy32, &ace.who);
			else
				status = nfsd_map_name_to_uid(argp->rqstp,
						buf, dummy32, &ace.who);
			if (status)
				goto out_nfserr;
			if (nfs4_acl_add_ace(*acl, ace.type, ace.flag,
				 ace.access_mask, ace.whotype, ace.who) != 0) {
				status = -ENOMEM;
				goto out_nfserr;
			}
		}
	} else
		*acl = NULL;
	if (bmval[1] & FATTR4_WORD1_MODE) {
		READ_BUF(4);
		len += 4;
		READ32(iattr->ia_mode);
		iattr->ia_mode &= (S_IFMT | S_IALLUGO);
		iattr->ia_valid |= ATTR_MODE;
	}
	if (bmval[1] & FATTR4_WORD1_OWNER) {
		READ_BUF(4);
		len += 4;
		READ32(dummy32);
		READ_BUF(dummy32);
		len += (XDR_QUADLEN(dummy32) << 2);
		READMEM(buf, dummy32);
		if ((status = nfsd_map_name_to_uid(argp->rqstp, buf, dummy32, &iattr->ia_uid)))
			goto out_nfserr;
		iattr->ia_valid |= ATTR_UID;
	}
	if (bmval[1] & FATTR4_WORD1_OWNER_GROUP) {
		READ_BUF(4);
		len += 4;
		READ32(dummy32);
		READ_BUF(dummy32);
		len += (XDR_QUADLEN(dummy32) << 2);
		READMEM(buf, dummy32);
		if ((status = nfsd_map_name_to_gid(argp->rqstp, buf, dummy32, &iattr->ia_gid)))
			goto out_nfserr;
		iattr->ia_valid |= ATTR_GID;
	}
	if (bmval[1] & FATTR4_WORD1_TIME_ACCESS_SET) {
		READ_BUF(4);
		len += 4;
		READ32(dummy32);
		switch (dummy32) {
		case NFS4_SET_TO_CLIENT_TIME:
			/* We require the high 32 bits of 'seconds' to be 0, and we ignore
			   all 32 bits of 'nseconds'. */
			READ_BUF(12);
			len += 12;
			READ32(dummy32);
			if (dummy32)
				return nfserr_inval;
			READ32(iattr->ia_atime.tv_sec);
			READ32(iattr->ia_atime.tv_nsec);
			if (iattr->ia_atime.tv_nsec >= (u32)1000000000)
				return nfserr_inval;
			iattr->ia_valid |= (ATTR_ATIME | ATTR_ATIME_SET);
			break;
		case NFS4_SET_TO_SERVER_TIME:
			iattr->ia_valid |= ATTR_ATIME;
			break;
		default:
			goto xdr_error;
		}
	}
	if (bmval[1] & FATTR4_WORD1_TIME_METADATA) {
		/* We require the high 32 bits of 'seconds' to be 0, and we ignore
		   all 32 bits of 'nseconds'. */
		READ_BUF(12);
		len += 12;
		READ32(dummy32);
		if (dummy32)
			return nfserr_inval;
		READ32(iattr->ia_ctime.tv_sec);
		READ32(iattr->ia_ctime.tv_nsec);
		if (iattr->ia_ctime.tv_nsec >= (u32)1000000000)
			return nfserr_inval;
		iattr->ia_valid |= ATTR_CTIME;
	}
	if (bmval[1] & FATTR4_WORD1_TIME_MODIFY_SET) {
		READ_BUF(4);
		len += 4;
		READ32(dummy32);
		switch (dummy32) {
		case NFS4_SET_TO_CLIENT_TIME:
			/* We require the high 32 bits of 'seconds' to be 0, and we ignore
			   all 32 bits of 'nseconds'. */
			READ_BUF(12);
			len += 12;
			READ32(dummy32);
			if (dummy32)
				return nfserr_inval;
			READ32(iattr->ia_mtime.tv_sec);
			READ32(iattr->ia_mtime.tv_nsec);
			if (iattr->ia_mtime.tv_nsec >= (u32)1000000000)
				return nfserr_inval;
			iattr->ia_valid |= (ATTR_MTIME | ATTR_MTIME_SET);
			break;
		case NFS4_SET_TO_SERVER_TIME:
			iattr->ia_valid |= ATTR_MTIME;
			break;
		default:
			goto xdr_error;
		}
	}
	if (len != expected_len)
		goto xdr_error;

	DECODE_TAIL;

out_nfserr:
	status = nfserrno(status);
	goto out;
}

static int
nfsd4_decode_access(struct nfsd4_compoundargs *argp, struct nfsd4_access *access)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(access->ac_req_access);

	DECODE_TAIL;
}

static int
nfsd4_decode_close(struct nfsd4_compoundargs *argp, struct nfsd4_close *close)
{
	DECODE_HEAD;

	close->cl_stateowner = NULL;
	READ_BUF(4 + sizeof(stateid_t));
	READ32(close->cl_seqid);
	READ32(close->cl_stateid.si_generation);
	COPYMEM(&close->cl_stateid.si_opaque, sizeof(stateid_opaque_t));

	DECODE_TAIL;
}


static int
nfsd4_decode_commit(struct nfsd4_compoundargs *argp, struct nfsd4_commit *commit)
{
	DECODE_HEAD;

	READ_BUF(12);
	READ64(commit->co_offset);
	READ32(commit->co_count);

	DECODE_TAIL;
}

static int
nfsd4_decode_create(struct nfsd4_compoundargs *argp, struct nfsd4_create *create)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(create->cr_type);
	switch (create->cr_type) {
	case NF4LNK:
		READ_BUF(4);
		READ32(create->cr_linklen);
		READ_BUF(create->cr_linklen);
		SAVEMEM(create->cr_linkname, create->cr_linklen);
		break;
	case NF4BLK:
	case NF4CHR:
		READ_BUF(8);
		READ32(create->cr_specdata1);
		READ32(create->cr_specdata2);
		break;
	case NF4SOCK:
	case NF4FIFO:
	case NF4DIR:
	default:
		break;
	}

	READ_BUF(4);
	READ32(create->cr_namelen);
	READ_BUF(create->cr_namelen);
	SAVEMEM(create->cr_name, create->cr_namelen);
	if ((status = check_filename(create->cr_name, create->cr_namelen, nfserr_inval)))
		return status;

	if ((status = nfsd4_decode_fattr(argp, create->cr_bmval, &create->cr_iattr, &create->cr_acl)))
		goto out;

	DECODE_TAIL;
}

static inline int
nfsd4_decode_delegreturn(struct nfsd4_compoundargs *argp, struct nfsd4_delegreturn *dr)
{
	DECODE_HEAD;

	READ_BUF(sizeof(stateid_t));
	READ32(dr->dr_stateid.si_generation);
	COPYMEM(&dr->dr_stateid.si_opaque, sizeof(stateid_opaque_t));

	DECODE_TAIL;
}

static inline int
nfsd4_decode_getattr(struct nfsd4_compoundargs *argp, struct nfsd4_getattr *getattr)
{
	return nfsd4_decode_bitmap(argp, getattr->ga_bmval);
}

static int
nfsd4_decode_link(struct nfsd4_compoundargs *argp, struct nfsd4_link *link)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(link->li_namelen);
	READ_BUF(link->li_namelen);
	SAVEMEM(link->li_name, link->li_namelen);
	if ((status = check_filename(link->li_name, link->li_namelen, nfserr_inval)))
		return status;

	DECODE_TAIL;
}

static int
nfsd4_decode_lock(struct nfsd4_compoundargs *argp, struct nfsd4_lock *lock)
{
	DECODE_HEAD;

	lock->lk_stateowner = NULL;
	/*
	* type, reclaim(boolean), offset, length, new_lock_owner(boolean)
	*/
	READ_BUF(28);
	READ32(lock->lk_type);
	if ((lock->lk_type < NFS4_READ_LT) || (lock->lk_type > NFS4_WRITEW_LT))
		goto xdr_error;
	READ32(lock->lk_reclaim);
	READ64(lock->lk_offset);
	READ64(lock->lk_length);
	READ32(lock->lk_is_new);

	if (lock->lk_is_new) {
		READ_BUF(36);
		READ32(lock->lk_new_open_seqid);
		READ32(lock->lk_new_open_stateid.si_generation);

		COPYMEM(&lock->lk_new_open_stateid.si_opaque, sizeof(stateid_opaque_t));
		READ32(lock->lk_new_lock_seqid);
		COPYMEM(&lock->lk_new_clientid, sizeof(clientid_t));
		READ32(lock->lk_new_owner.len);
		READ_BUF(lock->lk_new_owner.len);
		READMEM(lock->lk_new_owner.data, lock->lk_new_owner.len);
	} else {
		READ_BUF(20);
		READ32(lock->lk_old_lock_stateid.si_generation);
		COPYMEM(&lock->lk_old_lock_stateid.si_opaque, sizeof(stateid_opaque_t));
		READ32(lock->lk_old_lock_seqid);
	}

	DECODE_TAIL;
}

static int
nfsd4_decode_lockt(struct nfsd4_compoundargs *argp, struct nfsd4_lockt *lockt)
{
	DECODE_HEAD;
		        
	READ_BUF(32);
	READ32(lockt->lt_type);
	if((lockt->lt_type < NFS4_READ_LT) || (lockt->lt_type > NFS4_WRITEW_LT))
		goto xdr_error;
	READ64(lockt->lt_offset);
	READ64(lockt->lt_length);
	COPYMEM(&lockt->lt_clientid, 8);
	READ32(lockt->lt_owner.len);
	READ_BUF(lockt->lt_owner.len);
	READMEM(lockt->lt_owner.data, lockt->lt_owner.len);

	DECODE_TAIL;
}

static int
nfsd4_decode_locku(struct nfsd4_compoundargs *argp, struct nfsd4_locku *locku)
{
	DECODE_HEAD;

	locku->lu_stateowner = NULL;
	READ_BUF(24 + sizeof(stateid_t));
	READ32(locku->lu_type);
	if ((locku->lu_type < NFS4_READ_LT) || (locku->lu_type > NFS4_WRITEW_LT))
		goto xdr_error;
	READ32(locku->lu_seqid);
	READ32(locku->lu_stateid.si_generation);
	COPYMEM(&locku->lu_stateid.si_opaque, sizeof(stateid_opaque_t));
	READ64(locku->lu_offset);
	READ64(locku->lu_length);

	DECODE_TAIL;
}

static int
nfsd4_decode_lookup(struct nfsd4_compoundargs *argp, struct nfsd4_lookup *lookup)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(lookup->lo_len);
	READ_BUF(lookup->lo_len);
	SAVEMEM(lookup->lo_name, lookup->lo_len);
	if ((status = check_filename(lookup->lo_name, lookup->lo_len, nfserr_noent)))
		return status;

	DECODE_TAIL;
}

static int
nfsd4_decode_open(struct nfsd4_compoundargs *argp, struct nfsd4_open *open)
{
	DECODE_HEAD;

	memset(open->op_bmval, 0, sizeof(open->op_bmval));
	open->op_iattr.ia_valid = 0;
	open->op_stateowner = NULL;

	/* seqid, share_access, share_deny, clientid, ownerlen */
	READ_BUF(16 + sizeof(clientid_t));
	READ32(open->op_seqid);
	READ32(open->op_share_access);
	READ32(open->op_share_deny);
	COPYMEM(&open->op_clientid, sizeof(clientid_t));
	READ32(open->op_owner.len);

	/* owner, open_flag */
	READ_BUF(open->op_owner.len + 4);
	SAVEMEM(open->op_owner.data, open->op_owner.len);
	READ32(open->op_create);
	switch (open->op_create) {
	case NFS4_OPEN_NOCREATE:
		break;
	case NFS4_OPEN_CREATE:
		READ_BUF(4);
		READ32(open->op_createmode);
		switch (open->op_createmode) {
		case NFS4_CREATE_UNCHECKED:
		case NFS4_CREATE_GUARDED:
			if ((status = nfsd4_decode_fattr(argp, open->op_bmval, &open->op_iattr, &open->op_acl)))
				goto out;
			break;
		case NFS4_CREATE_EXCLUSIVE:
			READ_BUF(8);
			COPYMEM(open->op_verf.data, 8);
			break;
		default:
			goto xdr_error;
		}
		break;
	default:
		goto xdr_error;
	}

	/* open_claim */
	READ_BUF(4);
	READ32(open->op_claim_type);
	switch (open->op_claim_type) {
	case NFS4_OPEN_CLAIM_NULL:
	case NFS4_OPEN_CLAIM_DELEGATE_PREV:
		READ_BUF(4);
		READ32(open->op_fname.len);
		READ_BUF(open->op_fname.len);
		SAVEMEM(open->op_fname.data, open->op_fname.len);
		if ((status = check_filename(open->op_fname.data, open->op_fname.len, nfserr_inval)))
			return status;
		break;
	case NFS4_OPEN_CLAIM_PREVIOUS:
		READ_BUF(4);
		READ32(open->op_delegate_type);
		break;
	case NFS4_OPEN_CLAIM_DELEGATE_CUR:
		READ_BUF(sizeof(stateid_t) + 4);
		COPYMEM(&open->op_delegate_stateid, sizeof(stateid_t));
		READ32(open->op_fname.len);
		READ_BUF(open->op_fname.len);
		SAVEMEM(open->op_fname.data, open->op_fname.len);
		if ((status = check_filename(open->op_fname.data, open->op_fname.len, nfserr_inval)))
			return status;
		break;
	default:
		goto xdr_error;
	}

	DECODE_TAIL;
}

static int
nfsd4_decode_open_confirm(struct nfsd4_compoundargs *argp, struct nfsd4_open_confirm *open_conf)
{
	DECODE_HEAD;
		    
	open_conf->oc_stateowner = NULL;
	READ_BUF(4 + sizeof(stateid_t));
	READ32(open_conf->oc_req_stateid.si_generation);
	COPYMEM(&open_conf->oc_req_stateid.si_opaque, sizeof(stateid_opaque_t));
	READ32(open_conf->oc_seqid);
						        
	DECODE_TAIL;
}

static int
nfsd4_decode_open_downgrade(struct nfsd4_compoundargs *argp, struct nfsd4_open_downgrade *open_down)
{
	DECODE_HEAD;
		    
	open_down->od_stateowner = NULL;
	READ_BUF(12 + sizeof(stateid_t));
	READ32(open_down->od_stateid.si_generation);
	COPYMEM(&open_down->od_stateid.si_opaque, sizeof(stateid_opaque_t));
	READ32(open_down->od_seqid);
	READ32(open_down->od_share_access);
	READ32(open_down->od_share_deny);
						        
	DECODE_TAIL;
}

static int
nfsd4_decode_putfh(struct nfsd4_compoundargs *argp, struct nfsd4_putfh *putfh)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(putfh->pf_fhlen);
	if (putfh->pf_fhlen > NFS4_FHSIZE)
		goto xdr_error;
	READ_BUF(putfh->pf_fhlen);
	SAVEMEM(putfh->pf_fhval, putfh->pf_fhlen);

	DECODE_TAIL;
}

static int
nfsd4_decode_read(struct nfsd4_compoundargs *argp, struct nfsd4_read *read)
{
	DECODE_HEAD;

	READ_BUF(sizeof(stateid_t) + 12);
	READ32(read->rd_stateid.si_generation);
	COPYMEM(&read->rd_stateid.si_opaque, sizeof(stateid_opaque_t));
	READ64(read->rd_offset);
	READ32(read->rd_length);

	DECODE_TAIL;
}

static int
nfsd4_decode_readdir(struct nfsd4_compoundargs *argp, struct nfsd4_readdir *readdir)
{
	DECODE_HEAD;

	READ_BUF(24);
	READ64(readdir->rd_cookie);
	COPYMEM(readdir->rd_verf.data, sizeof(readdir->rd_verf.data));
	READ32(readdir->rd_dircount);    /* just in case you needed a useless field... */
	READ32(readdir->rd_maxcount);
	if ((status = nfsd4_decode_bitmap(argp, readdir->rd_bmval)))
		goto out;

	DECODE_TAIL;
}

static int
nfsd4_decode_remove(struct nfsd4_compoundargs *argp, struct nfsd4_remove *remove)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(remove->rm_namelen);
	READ_BUF(remove->rm_namelen);
	SAVEMEM(remove->rm_name, remove->rm_namelen);
	if ((status = check_filename(remove->rm_name, remove->rm_namelen, nfserr_noent)))
		return status;

	DECODE_TAIL;
}

static int
nfsd4_decode_rename(struct nfsd4_compoundargs *argp, struct nfsd4_rename *rename)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(rename->rn_snamelen);
	READ_BUF(rename->rn_snamelen + 4);
	SAVEMEM(rename->rn_sname, rename->rn_snamelen);
	READ32(rename->rn_tnamelen);
	READ_BUF(rename->rn_tnamelen);
	SAVEMEM(rename->rn_tname, rename->rn_tnamelen);
	if ((status = check_filename(rename->rn_sname, rename->rn_snamelen, nfserr_noent)))
		return status;
	if ((status = check_filename(rename->rn_tname, rename->rn_tnamelen, nfserr_inval)))
		return status;

	DECODE_TAIL;
}

static int
nfsd4_decode_renew(struct nfsd4_compoundargs *argp, clientid_t *clientid)
{
	DECODE_HEAD;

	READ_BUF(sizeof(clientid_t));
	COPYMEM(clientid, sizeof(clientid_t));

	DECODE_TAIL;
}

static int
nfsd4_decode_setattr(struct nfsd4_compoundargs *argp, struct nfsd4_setattr *setattr)
{
	DECODE_HEAD;

	READ_BUF(sizeof(stateid_t));
	READ32(setattr->sa_stateid.si_generation);
	COPYMEM(&setattr->sa_stateid.si_opaque, sizeof(stateid_opaque_t));
	if ((status = nfsd4_decode_fattr(argp, setattr->sa_bmval, &setattr->sa_iattr, &setattr->sa_acl)))
		goto out;

	DECODE_TAIL;
}

static int
nfsd4_decode_setclientid(struct nfsd4_compoundargs *argp, struct nfsd4_setclientid *setclientid)
{
	DECODE_HEAD;

	READ_BUF(12);
	COPYMEM(setclientid->se_verf.data, 8);
	READ32(setclientid->se_namelen);

	READ_BUF(setclientid->se_namelen + 8);
	SAVEMEM(setclientid->se_name, setclientid->se_namelen);
	READ32(setclientid->se_callback_prog);
	READ32(setclientid->se_callback_netid_len);

	READ_BUF(setclientid->se_callback_netid_len + 4);
	SAVEMEM(setclientid->se_callback_netid_val, setclientid->se_callback_netid_len);
	READ32(setclientid->se_callback_addr_len);

	READ_BUF(setclientid->se_callback_addr_len + 4);
	SAVEMEM(setclientid->se_callback_addr_val, setclientid->se_callback_addr_len);
	READ32(setclientid->se_callback_ident);

	DECODE_TAIL;
}

static int
nfsd4_decode_setclientid_confirm(struct nfsd4_compoundargs *argp, struct nfsd4_setclientid_confirm *scd_c)
{
	DECODE_HEAD;

	READ_BUF(8 + sizeof(nfs4_verifier));
	COPYMEM(&scd_c->sc_clientid, 8);
	COPYMEM(&scd_c->sc_confirm, sizeof(nfs4_verifier));

	DECODE_TAIL;
}

/* Also used for NVERIFY */
static int
nfsd4_decode_verify(struct nfsd4_compoundargs *argp, struct nfsd4_verify *verify)
{
#if 0
	struct nfsd4_compoundargs save = {
		.p = argp->p,
		.end = argp->end,
		.rqstp = argp->rqstp,
	};
	u32             ve_bmval[2];
	struct iattr    ve_iattr;           /* request */
	struct nfs4_acl *ve_acl;            /* request */
#endif
	DECODE_HEAD;

	if ((status = nfsd4_decode_bitmap(argp, verify->ve_bmval)))
		goto out;

	/* For convenience's sake, we compare raw xdr'd attributes in
	 * nfsd4_proc_verify; however we still decode here just to return
	 * correct error in case of bad xdr. */
#if 0
	status = nfsd4_decode_fattr(ve_bmval, &ve_iattr, &ve_acl);
	if (status == nfserr_inval) {
		status = nfserrno(status);
		goto out;
	}
#endif
	READ_BUF(4);
	READ32(verify->ve_attrlen);
	READ_BUF(verify->ve_attrlen);
	SAVEMEM(verify->ve_attrval, verify->ve_attrlen);

	DECODE_TAIL;
}

static int
nfsd4_decode_write(struct nfsd4_compoundargs *argp, struct nfsd4_write *write)
{
	int avail;
	int v;
	int len;
	DECODE_HEAD;

	READ_BUF(sizeof(stateid_opaque_t) + 20);
	READ32(write->wr_stateid.si_generation);
	COPYMEM(&write->wr_stateid.si_opaque, sizeof(stateid_opaque_t));
	READ64(write->wr_offset);
	READ32(write->wr_stable_how);
	if (write->wr_stable_how > 2)
		goto xdr_error;
	READ32(write->wr_buflen);

	/* Sorry .. no magic macros for this.. *
	 * READ_BUF(write->wr_buflen);
	 * SAVEMEM(write->wr_buf, write->wr_buflen);
	 */
	avail = (char*)argp->end - (char*)argp->p;
	if (avail + argp->pagelen < write->wr_buflen) {
		printk(KERN_NOTICE "xdr error! (%s:%d)\n", __FILE__, __LINE__); 
		goto xdr_error;
	}
	write->wr_vec[0].iov_base = p;
	write->wr_vec[0].iov_len = avail;
	v = 0;
	len = write->wr_buflen;
	while (len > write->wr_vec[v].iov_len) {
		len -= write->wr_vec[v].iov_len;
		v++;
		write->wr_vec[v].iov_base = page_address(argp->pagelist[0]);
		argp->pagelist++;
		if (argp->pagelen >= PAGE_SIZE) {
			write->wr_vec[v].iov_len = PAGE_SIZE;
			argp->pagelen -= PAGE_SIZE;
		} else {
			write->wr_vec[v].iov_len = argp->pagelen;
			argp->pagelen -= len;
		}
	}
	argp->end = (u32*) (write->wr_vec[v].iov_base + write->wr_vec[v].iov_len);
	argp->p = (u32*)  (write->wr_vec[v].iov_base + (XDR_QUADLEN(len) << 2));
	write->wr_vec[v].iov_len = len;
	write->wr_vlen = v+1;

	DECODE_TAIL;
}

static int
nfsd4_decode_release_lockowner(struct nfsd4_compoundargs *argp, struct nfsd4_release_lockowner *rlockowner)
{
	DECODE_HEAD;

	READ_BUF(12);
	COPYMEM(&rlockowner->rl_clientid, sizeof(clientid_t));
	READ32(rlockowner->rl_owner.len);
	READ_BUF(rlockowner->rl_owner.len);
	READMEM(rlockowner->rl_owner.data, rlockowner->rl_owner.len);

	DECODE_TAIL;
}

static int
nfsd4_decode_compound(struct nfsd4_compoundargs *argp)
{
	DECODE_HEAD;
	struct nfsd4_op *op;
	int i;

	/*
	 * XXX: According to spec, we should check the tag
	 * for UTF-8 compliance.  I'm postponing this for
	 * now because it seems that some clients do use
	 * binary tags.
	 */
	READ_BUF(4);
	READ32(argp->taglen);
	READ_BUF(argp->taglen + 8);
	SAVEMEM(argp->tag, argp->taglen);
	READ32(argp->minorversion);
	READ32(argp->opcnt);

	if (argp->taglen > NFSD4_MAX_TAGLEN)
		goto xdr_error;
	if (argp->opcnt > 100)
		goto xdr_error;

	if (argp->opcnt > sizeof(argp->iops)/sizeof(argp->iops[0])) {
		argp->ops = kmalloc(argp->opcnt * sizeof(*argp->ops), GFP_KERNEL);
		if (!argp->ops) {
			argp->ops = argp->iops;
			printk(KERN_INFO "nfsd: couldn't allocate room for COMPOUND\n");
			goto xdr_error;
		}
	}

	for (i = 0; i < argp->opcnt; i++) {
		op = &argp->ops[i];
		op->replay = NULL;

		/*
		 * We can't use READ_BUF() here because we need to handle
		 * a missing opcode as an OP_WRITE + 1. So we need to check
		 * to see if we're truly at the end of our buffer or if there
		 * is another page we need to flip to.
		 */

		if (argp->p == argp->end) {
			if (argp->pagelen < 4) {
				/* There isn't an opcode still on the wire */
				op->opnum = OP_WRITE + 1;
				op->status = nfserr_bad_xdr;
				argp->opcnt = i+1;
				break;
			}

			/*
			 * False alarm. We just hit a page boundary, but there
			 * is still data available.  Move pointer across page
			 * boundary.  *snip from READ_BUF*
			 */
			argp->p = page_address(argp->pagelist[0]);
			argp->pagelist++;
			if (argp->pagelen < PAGE_SIZE) {
				argp->end = p + (argp->pagelen>>2);
				argp->pagelen = 0;
			} else {
				argp->end = p + (PAGE_SIZE>>2);
				argp->pagelen -= PAGE_SIZE;
			}
		}
		op->opnum = ntohl(*argp->p++);

		switch (op->opnum) {
		case 2: /* Reserved operation */
			op->opnum = OP_ILLEGAL;
			if (argp->minorversion == 0)
				op->status = nfserr_op_illegal;
			else
				op->status = nfserr_minor_vers_mismatch;
			break;
		case OP_ACCESS:
			op->status = nfsd4_decode_access(argp, &op->u.access);
			break;
		case OP_CLOSE:
			op->status = nfsd4_decode_close(argp, &op->u.close);
			break;
		case OP_COMMIT:
			op->status = nfsd4_decode_commit(argp, &op->u.commit);
			break;
		case OP_CREATE:
			op->status = nfsd4_decode_create(argp, &op->u.create);
			break;
		case OP_DELEGRETURN:
			op->status = nfsd4_decode_delegreturn(argp, &op->u.delegreturn);
			break;
		case OP_GETATTR:
			op->status = nfsd4_decode_getattr(argp, &op->u.getattr);
			break;
		case OP_GETFH:
			op->status = nfs_ok;
			break;
		case OP_LINK:
			op->status = nfsd4_decode_link(argp, &op->u.link);
			break;
		case OP_LOCK:
			op->status = nfsd4_decode_lock(argp, &op->u.lock);
			break;
		case OP_LOCKT:
			op->status = nfsd4_decode_lockt(argp, &op->u.lockt);
			break;
		case OP_LOCKU:
			op->status = nfsd4_decode_locku(argp, &op->u.locku);
			break;
		case OP_LOOKUP:
			op->status = nfsd4_decode_lookup(argp, &op->u.lookup);
			break;
		case OP_LOOKUPP:
			op->status = nfs_ok;
			break;
		case OP_NVERIFY:
			op->status = nfsd4_decode_verify(argp, &op->u.nverify);
			break;
		case OP_OPEN:
			op->status = nfsd4_decode_open(argp, &op->u.open);
			break;
		case OP_OPEN_CONFIRM:
			op->status = nfsd4_decode_open_confirm(argp, &op->u.open_confirm);
			break;
		case OP_OPEN_DOWNGRADE:
			op->status = nfsd4_decode_open_downgrade(argp, &op->u.open_downgrade);
			break;
		case OP_PUTFH:
			op->status = nfsd4_decode_putfh(argp, &op->u.putfh);
			break;
		case OP_PUTROOTFH:
			op->status = nfs_ok;
			break;
		case OP_READ:
			op->status = nfsd4_decode_read(argp, &op->u.read);
			break;
		case OP_READDIR:
			op->status = nfsd4_decode_readdir(argp, &op->u.readdir);
			break;
		case OP_READLINK:
			op->status = nfs_ok;
			break;
		case OP_REMOVE:
			op->status = nfsd4_decode_remove(argp, &op->u.remove);
			break;
		case OP_RENAME:
			op->status = nfsd4_decode_rename(argp, &op->u.rename);
			break;
		case OP_RESTOREFH:
			op->status = nfs_ok;
			break;
		case OP_RENEW:
			op->status = nfsd4_decode_renew(argp, &op->u.renew);
			break;
		case OP_SAVEFH:
			op->status = nfs_ok;
			break;
		case OP_SETATTR:
			op->status = nfsd4_decode_setattr(argp, &op->u.setattr);
			break;
		case OP_SETCLIENTID:
			op->status = nfsd4_decode_setclientid(argp, &op->u.setclientid);
			break;
		case OP_SETCLIENTID_CONFIRM:
			op->status = nfsd4_decode_setclientid_confirm(argp, &op->u.setclientid_confirm);
			break;
		case OP_VERIFY:
			op->status = nfsd4_decode_verify(argp, &op->u.verify);
			break;
		case OP_WRITE:
			op->status = nfsd4_decode_write(argp, &op->u.write);
			break;
		case OP_RELEASE_LOCKOWNER:
			op->status = nfsd4_decode_release_lockowner(argp, &op->u.release_lockowner);
			break;
		default:
			op->opnum = OP_ILLEGAL;
			op->status = nfserr_op_illegal;
			break;
		}

		if (op->status) {
			argp->opcnt = i+1;
			break;
		}
	}

	DECODE_TAIL;
}
/*
 * END OF "GENERIC" DECODE ROUTINES.
 */

/*
 * START OF "GENERIC" ENCODE ROUTINES.
 *   These may look a little ugly since they are imported from a "generic"
 * set of XDR encode/decode routines which are intended to be shared by
 * all of our NFSv4 implementations (OpenBSD, MacOS X...).
 *
 * If the pain of reading these is too great, it should be a straightforward
 * task to translate them into Linux-specific versions which are more
 * consistent with the style used in NFSv2/v3...
 */
#define ENCODE_HEAD              u32 *p

#define WRITE32(n)               *p++ = htonl(n)
#define WRITE64(n)               do {				\
	*p++ = htonl((u32)((n) >> 32));				\
	*p++ = htonl((u32)(n));					\
} while (0)
#define WRITEMEM(ptr,nbytes)     do {				\
	*(p + XDR_QUADLEN(nbytes) -1) = 0;                      \
	memcpy(p, ptr, nbytes);					\
	p += XDR_QUADLEN(nbytes);				\
} while (0)
#define WRITECINFO(c)		do {				\
	*p++ = htonl(c.atomic);					\
	*p++ = htonl(c.before_ctime_sec);				\
	*p++ = htonl(c.before_ctime_nsec);				\
	*p++ = htonl(c.after_ctime_sec);				\
	*p++ = htonl(c.after_ctime_nsec);				\
} while (0)

#define RESERVE_SPACE(nbytes)	do {				\
	p = resp->p;						\
	BUG_ON(p + XDR_QUADLEN(nbytes) > resp->end);		\
} while (0)
#define ADJUST_ARGS()		resp->p = p

/*
 * Header routine to setup seqid operation replay cache
 */
#define ENCODE_SEQID_OP_HEAD					\
	u32 *p;							\
	u32 *save;						\
								\
	save = resp->p;

/*
 * Routine for encoding the result of a "seqid-mutating" NFSv4 operation.  This
 * is where sequence id's are incremented, and the replay cache is filled.
 * Note that we increment sequence id's here, at the last moment, so we're sure
 * we know whether the error to be returned is a sequence id mutating error.
 */

#define ENCODE_SEQID_OP_TAIL(stateowner) do {			\
	if (seqid_mutating_err(nfserr) && stateowner) { 	\
		stateowner->so_seqid++;				\
		stateowner->so_replay.rp_status = nfserr;   	\
		stateowner->so_replay.rp_buflen = 		\
			  (((char *)(resp)->p - (char *)save)); \
		memcpy(stateowner->so_replay.rp_buf, save,      \
 			stateowner->so_replay.rp_buflen); 	\
	} } while (0);


static u32 nfs4_ftypes[16] = {
        NF4BAD,  NF4FIFO, NF4CHR, NF4BAD,
        NF4DIR,  NF4BAD,  NF4BLK, NF4BAD,
        NF4REG,  NF4BAD,  NF4LNK, NF4BAD,
        NF4SOCK, NF4BAD,  NF4LNK, NF4BAD,
};

static int
nfsd4_encode_name(struct svc_rqst *rqstp, int whotype, uid_t id, int group,
			u32 **p, int *buflen)
{
	int status;

	if (*buflen < (XDR_QUADLEN(IDMAP_NAMESZ) << 2) + 4)
		return nfserr_resource;
	if (whotype != NFS4_ACL_WHO_NAMED)
		status = nfs4_acl_write_who(whotype, (u8 *)(*p + 1));
	else if (group)
		status = nfsd_map_gid_to_name(rqstp, id, (u8 *)(*p + 1));
	else
		status = nfsd_map_uid_to_name(rqstp, id, (u8 *)(*p + 1));
	if (status < 0)
		return nfserrno(status);
	*p = xdr_encode_opaque(*p, NULL, status);
	*buflen -= (XDR_QUADLEN(status) << 2) + 4;
	BUG_ON(*buflen < 0);
	return 0;
}

static inline int
nfsd4_encode_user(struct svc_rqst *rqstp, uid_t uid, u32 **p, int *buflen)
{
	return nfsd4_encode_name(rqstp, NFS4_ACL_WHO_NAMED, uid, 0, p, buflen);
}

static inline int
nfsd4_encode_group(struct svc_rqst *rqstp, uid_t gid, u32 **p, int *buflen)
{
	return nfsd4_encode_name(rqstp, NFS4_ACL_WHO_NAMED, gid, 1, p, buflen);
}

static inline int
nfsd4_encode_aclname(struct svc_rqst *rqstp, int whotype, uid_t id, int group,
		u32 **p, int *buflen)
{
	return nfsd4_encode_name(rqstp, whotype, id, group, p, buflen);
}


/*
 * Note: @fhp can be NULL; in this case, we might have to compose the filehandle
 * ourselves.
 *
 * @countp is the buffer size in _words_; upon successful return this becomes
 * replaced with the number of words written.
 */
int
nfsd4_encode_fattr(struct svc_fh *fhp, struct svc_export *exp,
		struct dentry *dentry, u32 *buffer, int *countp, u32 *bmval,
		struct svc_rqst *rqstp)
{
	u32 bmval0 = bmval[0];
	u32 bmval1 = bmval[1];
	struct kstat stat;
	struct svc_fh tempfh;
	struct kstatfs statfs;
	int buflen = *countp << 2;
	u32 *attrlenp;
	u32 dummy;
	u64 dummy64;
	u32 *p = buffer;
	int status;
	int aclsupport = 0;
	struct nfs4_acl *acl = NULL;

	BUG_ON(bmval1 & NFSD_WRITEONLY_ATTRS_WORD1);
	BUG_ON(bmval0 & ~NFSD_SUPPORTED_ATTRS_WORD0);
	BUG_ON(bmval1 & ~NFSD_SUPPORTED_ATTRS_WORD1);

	status = vfs_getattr(exp->ex_mnt, dentry, &stat);
	if (status)
		goto out_nfserr;
	if ((bmval0 & (FATTR4_WORD0_FILES_FREE | FATTR4_WORD0_FILES_TOTAL)) ||
	    (bmval1 & (FATTR4_WORD1_SPACE_AVAIL | FATTR4_WORD1_SPACE_FREE |
		       FATTR4_WORD1_SPACE_TOTAL))) {
		status = vfs_statfs(dentry->d_inode->i_sb, &statfs);
		if (status)
			goto out_nfserr;
	}
	if ((bmval0 & (FATTR4_WORD0_FILEHANDLE | FATTR4_WORD0_FSID)) && !fhp) {
		fh_init(&tempfh, NFS4_FHSIZE);
		status = fh_compose(&tempfh, exp, dentry, NULL);
		if (status)
			goto out;
		fhp = &tempfh;
	}
	if (bmval0 & (FATTR4_WORD0_ACL | FATTR4_WORD0_ACLSUPPORT
			| FATTR4_WORD0_SUPPORTED_ATTRS)) {
		status = nfsd4_get_nfs4_acl(rqstp, dentry, &acl);
		aclsupport = (status == 0);
		if (bmval0 & FATTR4_WORD0_ACL) {
			if (status == -EOPNOTSUPP)
				bmval0 &= ~FATTR4_WORD0_ACL;
			else if (status == -EINVAL) {
				status = nfserr_attrnotsupp;
				goto out;
			} else if (status != 0)
				goto out_nfserr;
		}
	}
	if ((buflen -= 16) < 0)
		goto out_resource;

	WRITE32(2);
	WRITE32(bmval0);
	WRITE32(bmval1);
	attrlenp = p++;                /* to be backfilled later */

	if (bmval0 & FATTR4_WORD0_SUPPORTED_ATTRS) {
		if ((buflen -= 12) < 0)
			goto out_resource;
		WRITE32(2);
		WRITE32(aclsupport ?
			NFSD_SUPPORTED_ATTRS_WORD0 :
			NFSD_SUPPORTED_ATTRS_WORD0 & ~FATTR4_WORD0_ACL);
		WRITE32(NFSD_SUPPORTED_ATTRS_WORD1);
	}
	if (bmval0 & FATTR4_WORD0_TYPE) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		dummy = nfs4_ftypes[(stat.mode & S_IFMT) >> 12];
		if (dummy == NF4BAD)
			goto out_serverfault;
		WRITE32(dummy);
	}
	if (bmval0 & FATTR4_WORD0_FH_EXPIRE_TYPE) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		if (exp->ex_flags & NFSEXP_NOSUBTREECHECK)
			WRITE32(NFS4_FH_PERSISTENT);
		else
			WRITE32(NFS4_FH_PERSISTENT|NFS4_FH_VOL_RENAME);
	}
	if (bmval0 & FATTR4_WORD0_CHANGE) {
		/*
		 * Note: This _must_ be consistent with the scheme for writing
		 * change_info, so any changes made here must be reflected there
		 * as well.  (See xdr4.h:set_change_info() and the WRITECINFO()
		 * macro above.)
		 */
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE32(stat.ctime.tv_sec);
		WRITE32(stat.ctime.tv_nsec);
	}
	if (bmval0 & FATTR4_WORD0_SIZE) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64(stat.size);
	}
	if (bmval0 & FATTR4_WORD0_LINK_SUPPORT) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_SYMLINK_SUPPORT) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_NAMED_ATTR) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(0);
	}
	if (bmval0 & FATTR4_WORD0_FSID) {
		if ((buflen -= 16) < 0)
			goto out_resource;
		if (is_fsid(fhp, rqstp->rq_reffh)) {
			WRITE64((u64)exp->ex_fsid);
			WRITE64((u64)0);
		} else {
			WRITE32(0);
			WRITE32(MAJOR(stat.dev));
			WRITE32(0);
			WRITE32(MINOR(stat.dev));
		}
	}
	if (bmval0 & FATTR4_WORD0_UNIQUE_HANDLES) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(0);
	}
	if (bmval0 & FATTR4_WORD0_LEASE_TIME) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(NFSD_LEASE_TIME);
	}
	if (bmval0 & FATTR4_WORD0_RDATTR_ERROR) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(0);
	}
	if (bmval0 & FATTR4_WORD0_ACL) {
		struct nfs4_ace *ace;
		struct list_head *h;

		if (acl == NULL) {
			if ((buflen -= 4) < 0)
				goto out_resource;

			WRITE32(0);
			goto out_acl;
		}
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(acl->naces);

		list_for_each(h, &acl->ace_head) {
			ace = list_entry(h, struct nfs4_ace, l_ace);

			if ((buflen -= 4*3) < 0)
				goto out_resource;
			WRITE32(ace->type);
			WRITE32(ace->flag);
			WRITE32(ace->access_mask & NFS4_ACE_MASK_ALL);
			status = nfsd4_encode_aclname(rqstp, ace->whotype,
				ace->who, ace->flag & NFS4_ACE_IDENTIFIER_GROUP,
				&p, &buflen);
			if (status == nfserr_resource)
				goto out_resource;
			if (status)
				goto out;
		}
	}
out_acl:
	if (bmval0 & FATTR4_WORD0_ACLSUPPORT) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(aclsupport ?
			ACL4_SUPPORT_ALLOW_ACL|ACL4_SUPPORT_DENY_ACL : 0);
	}
	if (bmval0 & FATTR4_WORD0_CANSETTIME) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_CASE_INSENSITIVE) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_CASE_PRESERVING) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_CHOWN_RESTRICTED) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_FILEHANDLE) {
		buflen -= (XDR_QUADLEN(fhp->fh_handle.fh_size) << 2) + 4;
		if (buflen < 0)
			goto out_resource;
		WRITE32(fhp->fh_handle.fh_size);
		WRITEMEM(&fhp->fh_handle.fh_base, fhp->fh_handle.fh_size);
	}
	if (bmval0 & FATTR4_WORD0_FILEID) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) stat.ino);
	}
	if (bmval0 & FATTR4_WORD0_FILES_AVAIL) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) statfs.f_ffree);
	}
	if (bmval0 & FATTR4_WORD0_FILES_FREE) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) statfs.f_ffree);
	}
	if (bmval0 & FATTR4_WORD0_FILES_TOTAL) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) statfs.f_files);
	}
	if (bmval0 & FATTR4_WORD0_HOMOGENEOUS) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_MAXFILESIZE) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64(~(u64)0);
	}
	if (bmval0 & FATTR4_WORD0_MAXLINK) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(255);
	}
	if (bmval0 & FATTR4_WORD0_MAXNAME) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(~(u32) 0);
	}
	if (bmval0 & FATTR4_WORD0_MAXREAD) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) NFSSVC_MAXBLKSIZE);
	}
	if (bmval0 & FATTR4_WORD0_MAXWRITE) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) NFSSVC_MAXBLKSIZE);
	}
	if (bmval1 & FATTR4_WORD1_MODE) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(stat.mode & S_IALLUGO);
	}
	if (bmval1 & FATTR4_WORD1_NO_TRUNC) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval1 & FATTR4_WORD1_NUMLINKS) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(stat.nlink);
	}
	if (bmval1 & FATTR4_WORD1_OWNER) {
		status = nfsd4_encode_user(rqstp, stat.uid, &p, &buflen);
		if (status == nfserr_resource)
			goto out_resource;
		if (status)
			goto out;
	}
	if (bmval1 & FATTR4_WORD1_OWNER_GROUP) {
		status = nfsd4_encode_group(rqstp, stat.gid, &p, &buflen);
		if (status == nfserr_resource)
			goto out_resource;
		if (status)
			goto out;
	}
	if (bmval1 & FATTR4_WORD1_RAWDEV) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE32((u32) MAJOR(stat.rdev));
		WRITE32((u32) MINOR(stat.rdev));
	}
	if (bmval1 & FATTR4_WORD1_SPACE_AVAIL) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		dummy64 = (u64)statfs.f_bavail * (u64)statfs.f_bsize;
		WRITE64(dummy64);
	}
	if (bmval1 & FATTR4_WORD1_SPACE_FREE) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		dummy64 = (u64)statfs.f_bfree * (u64)statfs.f_bsize;
		WRITE64(dummy64);
	}
	if (bmval1 & FATTR4_WORD1_SPACE_TOTAL) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		dummy64 = (u64)statfs.f_blocks * (u64)statfs.f_bsize;
		WRITE64(dummy64);
	}
	if (bmval1 & FATTR4_WORD1_SPACE_USED) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		dummy64 = (u64)stat.blocks << 9;
		WRITE64(dummy64);
	}
	if (bmval1 & FATTR4_WORD1_TIME_ACCESS) {
		if ((buflen -= 12) < 0)
			goto out_resource;
		WRITE32(0);
		WRITE32(stat.atime.tv_sec);
		WRITE32(stat.atime.tv_nsec);
	}
	if (bmval1 & FATTR4_WORD1_TIME_DELTA) {
		if ((buflen -= 12) < 0)
			goto out_resource;
		WRITE32(0);
		WRITE32(1);
		WRITE32(0);
	}
	if (bmval1 & FATTR4_WORD1_TIME_METADATA) {
		if ((buflen -= 12) < 0)
			goto out_resource;
		WRITE32(0);
		WRITE32(stat.ctime.tv_sec);
		WRITE32(stat.ctime.tv_nsec);
	}
	if (bmval1 & FATTR4_WORD1_TIME_MODIFY) {
		if ((buflen -= 12) < 0)
			goto out_resource;
		WRITE32(0);
		WRITE32(stat.mtime.tv_sec);
		WRITE32(stat.mtime.tv_nsec);
	}
	if (bmval1 & FATTR4_WORD1_MOUNTED_ON_FILEID) {
		struct dentry *mnt_pnt, *mnt_root;

		if ((buflen -= 8) < 0)
                	goto out_resource;
		mnt_root = exp->ex_mnt->mnt_root;
		if (mnt_root->d_inode == dentry->d_inode) {
			mnt_pnt = exp->ex_mnt->mnt_mountpoint;
			WRITE64((u64) mnt_pnt->d_inode->i_ino);
		} else
                	WRITE64((u64) stat.ino);
	}
	*attrlenp = htonl((char *)p - (char *)attrlenp - 4);
	*countp = p - buffer;
	status = nfs_ok;

out:
	nfs4_acl_free(acl);
	if (fhp == &tempfh)
		fh_put(&tempfh);
	return status;
out_nfserr:
	status = nfserrno(status);
	goto out;
out_resource:
	*countp = 0;
	status = nfserr_resource;
	goto out;
out_serverfault:
	status = nfserr_serverfault;
	goto out;
}

static int
nfsd4_encode_dirent_fattr(struct nfsd4_readdir *cd,
		const char *name, int namlen, u32 *p, int *buflen)
{
	struct svc_export *exp = cd->rd_fhp->fh_export;
	struct dentry *dentry;
	int nfserr;

	dentry = lookup_one_len(name, cd->rd_fhp->fh_dentry, namlen);
	if (IS_ERR(dentry))
		return nfserrno(PTR_ERR(dentry));

	exp_get(exp);
	if (d_mountpoint(dentry)) {
		if (nfsd_cross_mnt(cd->rd_rqstp, &dentry, &exp)) {
		/*
		 * -EAGAIN is the only error returned from
		 * nfsd_cross_mnt() and it indicates that an
		 * up-call has  been initiated to fill in the export
		 * options on exp.  When the answer comes back,
		 * this call will be retried.
		 */
			nfserr = nfserr_dropit;
			goto out_put;
		}

	}
	nfserr = nfsd4_encode_fattr(NULL, exp, dentry, p, buflen, cd->rd_bmval,
					cd->rd_rqstp);
out_put:
	dput(dentry);
	exp_put(exp);
	return nfserr;
}

static u32 *
nfsd4_encode_rdattr_error(u32 *p, int buflen, int nfserr)
{
	u32 *attrlenp;

	if (buflen < 6)
		return NULL;
	*p++ = htonl(2);
	*p++ = htonl(FATTR4_WORD0_RDATTR_ERROR); /* bmval0 */
	*p++ = htonl(0);			 /* bmval1 */

	attrlenp = p++;
	*p++ = nfserr;       /* no htonl */
	*attrlenp = htonl((char *)p - (char *)attrlenp - 4);
	return p;
}

static int
nfsd4_encode_dirent(struct readdir_cd *ccd, const char *name, int namlen,
		    loff_t offset, ino_t ino, unsigned int d_type)
{
	struct nfsd4_readdir *cd = container_of(ccd, struct nfsd4_readdir, common);
	int buflen;
	u32 *p = cd->buffer;
	int nfserr = nfserr_toosmall;

	/* In nfsv4, "." and ".." never make it onto the wire.. */
	if (name && isdotent(name, namlen)) {
		cd->common.err = nfs_ok;
		return 0;
	}

	if (cd->offset)
		xdr_encode_hyper(cd->offset, (u64) offset);

	buflen = cd->buflen - 4 - XDR_QUADLEN(namlen);
	if (buflen < 0)
		goto fail;

	*p++ = xdr_one;                             /* mark entry present */
	cd->offset = p;                             /* remember pointer */
	p = xdr_encode_hyper(p, NFS_OFFSET_MAX);    /* offset of next entry */
	p = xdr_encode_array(p, name, namlen);      /* name length & name */

	nfserr = nfsd4_encode_dirent_fattr(cd, name, namlen, p, &buflen);
	switch (nfserr) {
	case nfs_ok:
		p += buflen;
		break;
	case nfserr_resource:
		nfserr = nfserr_toosmall;
		goto fail;
	case nfserr_dropit:
		goto fail;
	default:
		/*
		 * If the client requested the RDATTR_ERROR attribute,
		 * we stuff the error code into this attribute
		 * and continue.  If this attribute was not requested,
		 * then in accordance with the spec, we fail the
		 * entire READDIR operation(!)
		 */
		if (!(cd->rd_bmval[0] & FATTR4_WORD0_RDATTR_ERROR))
			goto fail;
		nfserr = nfserr_toosmall;
		p = nfsd4_encode_rdattr_error(p, buflen, nfserr);
		if (p == NULL)
			goto fail;
	}
	cd->buflen -= (p - cd->buffer);
	cd->buffer = p;
	cd->common.err = nfs_ok;
	return 0;
fail:
	cd->common.err = nfserr;
	return -EINVAL;
}

static void
nfsd4_encode_access(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_access *access)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(8);
		WRITE32(access->ac_supported);
		WRITE32(access->ac_resp_access);
		ADJUST_ARGS();
	}
}

static void
nfsd4_encode_close(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_close *close)
{
	ENCODE_SEQID_OP_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(sizeof(stateid_t));
		WRITE32(close->cl_stateid.si_generation);
		WRITEMEM(&close->cl_stateid.si_opaque, sizeof(stateid_opaque_t));
		ADJUST_ARGS();
	}
	ENCODE_SEQID_OP_TAIL(close->cl_stateowner);
}


static void
nfsd4_encode_commit(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_commit *commit)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(8);
		WRITEMEM(commit->co_verf.data, 8);
		ADJUST_ARGS();
	}
}

static void
nfsd4_encode_create(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_create *create)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(32);
		WRITECINFO(create->cr_cinfo);
		WRITE32(2);
		WRITE32(create->cr_bmval[0]);
		WRITE32(create->cr_bmval[1]);
		ADJUST_ARGS();
	}
}

static int
nfsd4_encode_getattr(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_getattr *getattr)
{
	struct svc_fh *fhp = getattr->ga_fhp;
	int buflen;

	if (nfserr)
		return nfserr;

	buflen = resp->end - resp->p - (COMPOUND_ERR_SLACK_SPACE >> 2);
	nfserr = nfsd4_encode_fattr(fhp, fhp->fh_export, fhp->fh_dentry,
				    resp->p, &buflen, getattr->ga_bmval,
				    resp->rqstp);

	if (!nfserr)
		resp->p += buflen;
	return nfserr;
}

static void
nfsd4_encode_getfh(struct nfsd4_compoundres *resp, int nfserr, struct svc_fh *fhp)
{
	unsigned int len;
	ENCODE_HEAD;

	if (!nfserr) {
		len = fhp->fh_handle.fh_size;
		RESERVE_SPACE(len + 4);
		WRITE32(len);
		WRITEMEM(&fhp->fh_handle.fh_base, len);
		ADJUST_ARGS();
	}
}

/*
* Including all fields other than the name, a LOCK4denied structure requires
*   8(clientid) + 4(namelen) + 8(offset) + 8(length) + 4(type) = 32 bytes.
*/
static void
nfsd4_encode_lock_denied(struct nfsd4_compoundres *resp, struct nfsd4_lock_denied *ld)
{
	ENCODE_HEAD;

	RESERVE_SPACE(32 + XDR_LEN(ld->ld_sop ? ld->ld_sop->so_owner.len : 0));
	WRITE64(ld->ld_start);
	WRITE64(ld->ld_length);
	WRITE32(ld->ld_type);
	if (ld->ld_sop) {
		WRITEMEM(&ld->ld_clientid, 8);
		WRITE32(ld->ld_sop->so_owner.len);
		WRITEMEM(ld->ld_sop->so_owner.data, ld->ld_sop->so_owner.len);
		kref_put(&ld->ld_sop->so_ref, nfs4_free_stateowner);
	}  else {  /* non - nfsv4 lock in conflict, no clientid nor owner */
		WRITE64((u64)0); /* clientid */
		WRITE32(0); /* length of owner name */
	}
	ADJUST_ARGS();
}

static void
nfsd4_encode_lock(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_lock *lock)
{

	ENCODE_SEQID_OP_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(4 + sizeof(stateid_t));
		WRITE32(lock->lk_resp_stateid.si_generation);
		WRITEMEM(&lock->lk_resp_stateid.si_opaque, sizeof(stateid_opaque_t));
		ADJUST_ARGS();
	} else if (nfserr == nfserr_denied)
		nfsd4_encode_lock_denied(resp, &lock->lk_denied);

	ENCODE_SEQID_OP_TAIL(lock->lk_stateowner);
}

static void
nfsd4_encode_lockt(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_lockt *lockt)
{
	if (nfserr == nfserr_denied)
		nfsd4_encode_lock_denied(resp, &lockt->lt_denied);
}

static void
nfsd4_encode_locku(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_locku *locku)
{
	ENCODE_SEQID_OP_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(sizeof(stateid_t));
		WRITE32(locku->lu_stateid.si_generation);
		WRITEMEM(&locku->lu_stateid.si_opaque, sizeof(stateid_opaque_t));
		ADJUST_ARGS();
	}
				        
	ENCODE_SEQID_OP_TAIL(locku->lu_stateowner);
}


static void
nfsd4_encode_link(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_link *link)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(20);
		WRITECINFO(link->li_cinfo);
		ADJUST_ARGS();
	}
}


static void
nfsd4_encode_open(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_open *open)
{
	ENCODE_SEQID_OP_HEAD;

	if (nfserr)
		goto out;

	RESERVE_SPACE(36 + sizeof(stateid_t));
	WRITE32(open->op_stateid.si_generation);
	WRITEMEM(&open->op_stateid.si_opaque, sizeof(stateid_opaque_t));
	WRITECINFO(open->op_cinfo);
	WRITE32(open->op_rflags);
	WRITE32(2);
	WRITE32(open->op_bmval[0]);
	WRITE32(open->op_bmval[1]);
	WRITE32(open->op_delegate_type);
	ADJUST_ARGS();

	switch (open->op_delegate_type) {
	case NFS4_OPEN_DELEGATE_NONE:
		break;
	case NFS4_OPEN_DELEGATE_READ:
		RESERVE_SPACE(20 + sizeof(stateid_t));
		WRITEMEM(&open->op_delegate_stateid, sizeof(stateid_t));
		WRITE32(open->op_recall);

		/*
		 * TODO: ACE's in delegations
		 */
		WRITE32(NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE);
		WRITE32(0);
		WRITE32(0);
		WRITE32(0);   /* XXX: is NULL principal ok? */
		ADJUST_ARGS();
		break;
	case NFS4_OPEN_DELEGATE_WRITE:
		RESERVE_SPACE(32 + sizeof(stateid_t));
		WRITEMEM(&open->op_delegate_stateid, sizeof(stateid_t));
		WRITE32(0);

		/*
		 * TODO: space_limit's in delegations
		 */
		WRITE32(NFS4_LIMIT_SIZE);
		WRITE32(~(u32)0);
		WRITE32(~(u32)0);

		/*
		 * TODO: ACE's in delegations
		 */
		WRITE32(NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE);
		WRITE32(0);
		WRITE32(0);
		WRITE32(0);   /* XXX: is NULL principal ok? */
		ADJUST_ARGS();
		break;
	default:
		BUG();
	}
	/* XXX save filehandle here */
out:
	ENCODE_SEQID_OP_TAIL(open->op_stateowner);
}

static void
nfsd4_encode_open_confirm(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_open_confirm *oc)
{
	ENCODE_SEQID_OP_HEAD;
				        
	if (!nfserr) {
		RESERVE_SPACE(sizeof(stateid_t));
		WRITE32(oc->oc_resp_stateid.si_generation);
		WRITEMEM(&oc->oc_resp_stateid.si_opaque, sizeof(stateid_opaque_t));
		ADJUST_ARGS();
	}

	ENCODE_SEQID_OP_TAIL(oc->oc_stateowner);
}

static void
nfsd4_encode_open_downgrade(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_open_downgrade *od)
{
	ENCODE_SEQID_OP_HEAD;
				        
	if (!nfserr) {
		RESERVE_SPACE(sizeof(stateid_t));
		WRITE32(od->od_stateid.si_generation);
		WRITEMEM(&od->od_stateid.si_opaque, sizeof(stateid_opaque_t));
		ADJUST_ARGS();
	}

	ENCODE_SEQID_OP_TAIL(od->od_stateowner);
}

static int
nfsd4_encode_read(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_read *read)
{
	u32 eof;
	int v, pn;
	unsigned long maxcount; 
	long len;
	ENCODE_HEAD;

	if (nfserr)
		return nfserr;
	if (resp->xbuf->page_len)
		return nfserr_resource;

	RESERVE_SPACE(8); /* eof flag and byte count */

	maxcount = NFSSVC_MAXBLKSIZE;
	if (maxcount > read->rd_length)
		maxcount = read->rd_length;

	len = maxcount;
	v = 0;
	while (len > 0) {
		pn = resp->rqstp->rq_resused;
		svc_take_page(resp->rqstp);
		read->rd_iov[v].iov_base = page_address(resp->rqstp->rq_respages[pn]);
		read->rd_iov[v].iov_len = len < PAGE_SIZE ? len : PAGE_SIZE;
		v++;
		len -= PAGE_SIZE;
	}
	read->rd_vlen = v;

	nfserr = nfsd_read(read->rd_rqstp, read->rd_fhp, read->rd_filp,
			read->rd_offset, read->rd_iov, read->rd_vlen,
			&maxcount);

	if (nfserr == nfserr_symlink)
		nfserr = nfserr_inval;
	if (nfserr)
		return nfserr;
	eof = (read->rd_offset + maxcount >= read->rd_fhp->fh_dentry->d_inode->i_size);

	WRITE32(eof);
	WRITE32(maxcount);
	ADJUST_ARGS();
	resp->xbuf->head[0].iov_len = ((char*)resp->p) - (char*)resp->xbuf->head[0].iov_base;

	resp->xbuf->page_len = maxcount;

	/* read zero bytes -> don't set up tail */
	if(!maxcount)
		return 0;        

	/* set up page for remaining responses */
	svc_take_page(resp->rqstp);
	resp->xbuf->tail[0].iov_base = 
		page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);
	resp->rqstp->rq_restailpage = resp->rqstp->rq_resused-1;
	resp->xbuf->tail[0].iov_len = 0;
	resp->p = resp->xbuf->tail[0].iov_base;
	resp->end = resp->p + PAGE_SIZE/4;

	if (maxcount&3) {
		*(resp->p)++ = 0;
		resp->xbuf->tail[0].iov_base += maxcount&3;
		resp->xbuf->tail[0].iov_len = 4 - (maxcount&3);
	}
	return 0;
}

static int
nfsd4_encode_readlink(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_readlink *readlink)
{
	int maxcount;
	char *page;
	ENCODE_HEAD;

	if (nfserr)
		return nfserr;
	if (resp->xbuf->page_len)
		return nfserr_resource;

	svc_take_page(resp->rqstp);
	page = page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);

	maxcount = PAGE_SIZE;
	RESERVE_SPACE(4);

	/*
	 * XXX: By default, the ->readlink() VFS op will truncate symlinks
	 * if they would overflow the buffer.  Is this kosher in NFSv4?  If
	 * not, one easy fix is: if ->readlink() precisely fills the buffer,
	 * assume that truncation occurred, and return NFS4ERR_RESOURCE.
	 */
	nfserr = nfsd_readlink(readlink->rl_rqstp, readlink->rl_fhp, page, &maxcount);
	if (nfserr == nfserr_isdir)
		return nfserr_inval;
	if (nfserr)
		return nfserr;

	WRITE32(maxcount);
	ADJUST_ARGS();
	resp->xbuf->head[0].iov_len = ((char*)resp->p) - (char*)resp->xbuf->head[0].iov_base;

	svc_take_page(resp->rqstp);
	resp->xbuf->tail[0].iov_base = 
		page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);
	resp->rqstp->rq_restailpage = resp->rqstp->rq_resused-1;
	resp->xbuf->tail[0].iov_len = 0;
	resp->p = resp->xbuf->tail[0].iov_base;
	resp->end = resp->p + PAGE_SIZE/4;

	resp->xbuf->page_len = maxcount;
	if (maxcount&3) {
		*(resp->p)++ = 0;
		resp->xbuf->tail[0].iov_base += maxcount&3;
		resp->xbuf->tail[0].iov_len = 4 - (maxcount&3);
	}
	return 0;
}

static int
nfsd4_encode_readdir(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_readdir *readdir)
{
	int maxcount;
	loff_t offset;
	u32 *page, *savep;
	ENCODE_HEAD;

	if (nfserr)
		return nfserr;
	if (resp->xbuf->page_len)
		return nfserr_resource;

	RESERVE_SPACE(8);  /* verifier */
	savep = p;

	/* XXX: Following NFSv3, we ignore the READDIR verifier for now. */
	WRITE32(0);
	WRITE32(0);
	ADJUST_ARGS();
	resp->xbuf->head[0].iov_len = ((char*)resp->p) - (char*)resp->xbuf->head[0].iov_base;

	maxcount = PAGE_SIZE;
	if (maxcount > readdir->rd_maxcount)
		maxcount = readdir->rd_maxcount;

	/*
	 * Convert from bytes to words, account for the two words already
	 * written, make sure to leave two words at the end for the next
	 * pointer and eof field.
	 */
	maxcount = (maxcount >> 2) - 4;
	if (maxcount < 0) {
		nfserr =  nfserr_toosmall;
		goto err_no_verf;
	}

	svc_take_page(resp->rqstp);
	page = page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);
	readdir->common.err = 0;
	readdir->buflen = maxcount;
	readdir->buffer = page;
	readdir->offset = NULL;

	offset = readdir->rd_cookie;
	nfserr = nfsd_readdir(readdir->rd_rqstp, readdir->rd_fhp,
			      &offset,
			      &readdir->common, nfsd4_encode_dirent);
	if (nfserr == nfs_ok &&
	    readdir->common.err == nfserr_toosmall &&
	    readdir->buffer == page) 
		nfserr = nfserr_toosmall;
	if (nfserr == nfserr_symlink)
		nfserr = nfserr_notdir;
	if (nfserr)
		goto err_no_verf;

	if (readdir->offset)
		xdr_encode_hyper(readdir->offset, offset);

	p = readdir->buffer;
	*p++ = 0;	/* no more entries */
	*p++ = htonl(readdir->common.err == nfserr_eof);
	resp->xbuf->page_len = ((char*)p) - (char*)page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);

	/* allocate a page for the tail */
	svc_take_page(resp->rqstp);
	resp->xbuf->tail[0].iov_base = 
		page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);
	resp->rqstp->rq_restailpage = resp->rqstp->rq_resused-1;
	resp->xbuf->tail[0].iov_len = 0;
	resp->p = resp->xbuf->tail[0].iov_base;
	resp->end = resp->p + PAGE_SIZE/4;

	return 0;
err_no_verf:
	p = savep;
	ADJUST_ARGS();
	return nfserr;
}

static void
nfsd4_encode_remove(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_remove *remove)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(20);
		WRITECINFO(remove->rm_cinfo);
		ADJUST_ARGS();
	}
}

static void
nfsd4_encode_rename(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_rename *rename)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(40);
		WRITECINFO(rename->rn_sinfo);
		WRITECINFO(rename->rn_tinfo);
		ADJUST_ARGS();
	}
}

/*
 * The SETATTR encode routine is special -- it always encodes a bitmap,
 * regardless of the error status.
 */
static void
nfsd4_encode_setattr(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_setattr *setattr)
{
	ENCODE_HEAD;

	RESERVE_SPACE(12);
	if (nfserr) {
		WRITE32(2);
		WRITE32(0);
		WRITE32(0);
	}
	else {
		WRITE32(2);
		WRITE32(setattr->sa_bmval[0]);
		WRITE32(setattr->sa_bmval[1]);
	}
	ADJUST_ARGS();
}

static void
nfsd4_encode_setclientid(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_setclientid *scd)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(8 + sizeof(nfs4_verifier));
		WRITEMEM(&scd->se_clientid, 8);
		WRITEMEM(&scd->se_confirm, sizeof(nfs4_verifier));
		ADJUST_ARGS();
	}
	else if (nfserr == nfserr_clid_inuse) {
		RESERVE_SPACE(8);
		WRITE32(0);
		WRITE32(0);
		ADJUST_ARGS();
	}
}

static void
nfsd4_encode_write(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_write *write)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(16);
		WRITE32(write->wr_bytes_written);
		WRITE32(write->wr_how_written);
		WRITEMEM(write->wr_verifier.data, 8);
		ADJUST_ARGS();
	}
}

void
nfsd4_encode_operation(struct nfsd4_compoundres *resp, struct nfsd4_op *op)
{
	u32 *statp;
	ENCODE_HEAD;

	RESERVE_SPACE(8);
	WRITE32(op->opnum);
	statp = p++;	/* to be backfilled at the end */
	ADJUST_ARGS();

	switch (op->opnum) {
	case OP_ACCESS:
		nfsd4_encode_access(resp, op->status, &op->u.access);
		break;
	case OP_CLOSE:
		nfsd4_encode_close(resp, op->status, &op->u.close);
		break;
	case OP_COMMIT:
		nfsd4_encode_commit(resp, op->status, &op->u.commit);
		break;
	case OP_CREATE:
		nfsd4_encode_create(resp, op->status, &op->u.create);
		break;
	case OP_DELEGRETURN:
		break;
	case OP_GETATTR:
		op->status = nfsd4_encode_getattr(resp, op->status, &op->u.getattr);
		break;
	case OP_GETFH:
		nfsd4_encode_getfh(resp, op->status, op->u.getfh);
		break;
	case OP_LINK:
		nfsd4_encode_link(resp, op->status, &op->u.link);
		break;
	case OP_LOCK:
		nfsd4_encode_lock(resp, op->status, &op->u.lock);
		break;
	case OP_LOCKT:
		nfsd4_encode_lockt(resp, op->status, &op->u.lockt);
		break;
	case OP_LOCKU:
		nfsd4_encode_locku(resp, op->status, &op->u.locku);
		break;
	case OP_LOOKUP:
		break;
	case OP_LOOKUPP:
		break;
	case OP_NVERIFY:
		break;
	case OP_OPEN:
		nfsd4_encode_open(resp, op->status, &op->u.open);
		break;
	case OP_OPEN_CONFIRM:
		nfsd4_encode_open_confirm(resp, op->status, &op->u.open_confirm);
		break;
	case OP_OPEN_DOWNGRADE:
		nfsd4_encode_open_downgrade(resp, op->status, &op->u.open_downgrade);
		break;
	case OP_PUTFH:
		break;
	case OP_PUTROOTFH:
		break;
	case OP_READ:
		op->status = nfsd4_encode_read(resp, op->status, &op->u.read);
		break;
	case OP_READDIR:
		op->status = nfsd4_encode_readdir(resp, op->status, &op->u.readdir);
		break;
	case OP_READLINK:
		op->status = nfsd4_encode_readlink(resp, op->status, &op->u.readlink);
		break;
	case OP_REMOVE:
		nfsd4_encode_remove(resp, op->status, &op->u.remove);
		break;
	case OP_RENAME:
		nfsd4_encode_rename(resp, op->status, &op->u.rename);
		break;
	case OP_RENEW:
		break;
	case OP_RESTOREFH:
		break;
	case OP_SAVEFH:
		break;
	case OP_SETATTR:
		nfsd4_encode_setattr(resp, op->status, &op->u.setattr);
		break;
	case OP_SETCLIENTID:
		nfsd4_encode_setclientid(resp, op->status, &op->u.setclientid);
		break;
	case OP_SETCLIENTID_CONFIRM:
		break;
	case OP_VERIFY:
		break;
	case OP_WRITE:
		nfsd4_encode_write(resp, op->status, &op->u.write);
		break;
	case OP_RELEASE_LOCKOWNER:
		break;
	default:
		break;
	}

	/*
	 * Note: We write the status directly, instead of using WRITE32(),
	 * since it is already in network byte order.
	 */
	*statp = op->status;
}

/* 
 * Encode the reply stored in the stateowner reply cache 
 * 
 * XDR note: do not encode rp->rp_buflen: the buffer contains the
 * previously sent already encoded operation.
 *
 * called with nfs4_lock_state() held
 */
void
nfsd4_encode_replay(struct nfsd4_compoundres *resp, struct nfsd4_op *op)
{
	ENCODE_HEAD;
	struct nfs4_replay *rp = op->replay;

	BUG_ON(!rp);

	RESERVE_SPACE(8);
	WRITE32(op->opnum);
	*p++ = rp->rp_status;  /* already xdr'ed */
	ADJUST_ARGS();

	RESERVE_SPACE(rp->rp_buflen);
	WRITEMEM(rp->rp_buf, rp->rp_buflen);
	ADJUST_ARGS();
}

/*
 * END OF "GENERIC" ENCODE ROUTINES.
 */

int
nfs4svc_encode_voidres(struct svc_rqst *rqstp, u32 *p, void *dummy)
{
        return xdr_ressize_check(rqstp, p);
}

void nfsd4_release_compoundargs(struct nfsd4_compoundargs *args)
{
	if (args->ops != args->iops) {
		kfree(args->ops);
		args->ops = args->iops;
	}
	if (args->tmpp) {
		kfree(args->tmpp);
		args->tmpp = NULL;
	}
	while (args->to_free) {
		struct tmpbuf *tb = args->to_free;
		args->to_free = tb->next;
		tb->release(tb->buf);
		kfree(tb);
	}
}

int
nfs4svc_decode_compoundargs(struct svc_rqst *rqstp, u32 *p, struct nfsd4_compoundargs *args)
{
	int status;

	args->p = p;
	args->end = rqstp->rq_arg.head[0].iov_base + rqstp->rq_arg.head[0].iov_len;
	args->pagelist = rqstp->rq_arg.pages;
	args->pagelen = rqstp->rq_arg.page_len;
	args->tmpp = NULL;
	args->to_free = NULL;
	args->ops = args->iops;
	args->rqstp = rqstp;

	status = nfsd4_decode_compound(args);
	if (status) {
		nfsd4_release_compoundargs(args);
	}
	return !status;
}

int
nfs4svc_encode_compoundres(struct svc_rqst *rqstp, u32 *p, struct nfsd4_compoundres *resp)
{
	/*
	 * All that remains is to write the tag and operation count...
	 */
	struct kvec *iov;
	p = resp->tagp;
	*p++ = htonl(resp->taglen);
	memcpy(p, resp->tag, resp->taglen);
	p += XDR_QUADLEN(resp->taglen);
	*p++ = htonl(resp->opcnt);

	if (rqstp->rq_res.page_len) 
		iov = &rqstp->rq_res.tail[0];
	else
		iov = &rqstp->rq_res.head[0];
	iov->iov_len = ((char*)resp->p) - (char*)iov->iov_base;
	BUG_ON(iov->iov_len > PAGE_SIZE);
	return 1;
}

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
