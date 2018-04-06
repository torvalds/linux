/*
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
 */

#include <linux/file.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/statfs.h>
#include <linux/utsname.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/svcauth_gss.h>

#include "idmap.h"
#include "acl.h"
#include "xdr4.h"
#include "vfs.h"
#include "state.h"
#include "cache.h"
#include "netns.h"
#include "pnfs.h"

#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
#include <linux/security.h>
#endif


#define NFSDDBG_FACILITY		NFSDDBG_XDR

const u32 nfsd_suppattrs[3][3] = {
	{NFSD4_SUPPORTED_ATTRS_WORD0,
	 NFSD4_SUPPORTED_ATTRS_WORD1,
	 NFSD4_SUPPORTED_ATTRS_WORD2},

	{NFSD4_1_SUPPORTED_ATTRS_WORD0,
	 NFSD4_1_SUPPORTED_ATTRS_WORD1,
	 NFSD4_1_SUPPORTED_ATTRS_WORD2},

	{NFSD4_1_SUPPORTED_ATTRS_WORD0,
	 NFSD4_1_SUPPORTED_ATTRS_WORD1,
	 NFSD4_2_SUPPORTED_ATTRS_WORD2},
};

/*
 * As per referral draft, the fsid for a referral MUST be different from the fsid of the containing
 * directory in order to indicate to the client that a filesystem boundary is present
 * We use a fixed fsid for a referral
 */
#define NFS4_REFERRAL_FSID_MAJOR	0x8000000ULL
#define NFS4_REFERRAL_FSID_MINOR	0x8000000ULL

static __be32
check_filename(char *str, int len)
{
	int i;

	if (len == 0)
		return nfserr_inval;
	if (isdotent(str, len))
		return nfserr_badname;
	for (i = 0; i < len; i++)
		if (str[i] == '/')
			return nfserr_badname;
	return 0;
}

#define DECODE_HEAD				\
	__be32 *p;				\
	__be32 status
#define DECODE_TAIL				\
	status = 0;				\
out:						\
	return status;				\
xdr_error:					\
	dprintk("NFSD: xdr error (%s:%d)\n",	\
			__FILE__, __LINE__);	\
	status = nfserr_bad_xdr;		\
	goto out

#define READMEM(x,nbytes) do {			\
	x = (char *)p;				\
	p += XDR_QUADLEN(nbytes);		\
} while (0)
#define SAVEMEM(x,nbytes) do {			\
	if (!(x = (p==argp->tmp || p == argp->tmpp) ? \
 		savemem(argp, p, nbytes) :	\
 		(char *)p)) {			\
		dprintk("NFSD: xdr error (%s:%d)\n", \
				__FILE__, __LINE__); \
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
		dprintk("NFSD: xdr error (%s:%d)\n", \
				__FILE__, __LINE__); \
		goto xdr_error;			\
	}					\
} while (0)

static void next_decode_page(struct nfsd4_compoundargs *argp)
{
	argp->p = page_address(argp->pagelist[0]);
	argp->pagelist++;
	if (argp->pagelen < PAGE_SIZE) {
		argp->end = argp->p + XDR_QUADLEN(argp->pagelen);
		argp->pagelen = 0;
	} else {
		argp->end = argp->p + (PAGE_SIZE>>2);
		argp->pagelen -= PAGE_SIZE;
	}
}

static __be32 *read_buf(struct nfsd4_compoundargs *argp, u32 nbytes)
{
	/* We want more bytes than seem to be available.
	 * Maybe we need a new page, maybe we have just run out
	 */
	unsigned int avail = (char *)argp->end - (char *)argp->p;
	__be32 *p;

	if (argp->pagelen == 0) {
		struct kvec *vec = &argp->rqstp->rq_arg.tail[0];

		if (!argp->tail) {
			argp->tail = true;
			avail = vec->iov_len;
			argp->p = vec->iov_base;
			argp->end = vec->iov_base + avail;
		}

		if (avail < nbytes)
			return NULL;

		p = argp->p;
		argp->p += XDR_QUADLEN(nbytes);
		return p;
	}

	if (avail + argp->pagelen < nbytes)
		return NULL;
	if (avail + PAGE_SIZE < nbytes) /* need more than a page !! */
		return NULL;
	/* ok, we can do it with the current plus the next page */
	if (nbytes <= sizeof(argp->tmp))
		p = argp->tmp;
	else {
		kfree(argp->tmpp);
		p = argp->tmpp = kmalloc(nbytes, GFP_KERNEL);
		if (!p)
			return NULL;
		
	}
	/*
	 * The following memcpy is safe because read_buf is always
	 * called with nbytes > avail, and the two cases above both
	 * guarantee p points to at least nbytes bytes.
	 */
	memcpy(p, argp->p, avail);
	next_decode_page(argp);
	memcpy(((char*)p)+avail, argp->p, (nbytes - avail));
	argp->p += XDR_QUADLEN(nbytes - avail);
	return p;
}

static int zero_clientid(clientid_t *clid)
{
	return (clid->cl_boot == 0) && (clid->cl_id == 0);
}

/**
 * svcxdr_tmpalloc - allocate memory to be freed after compound processing
 * @argp: NFSv4 compound argument structure
 * @p: pointer to be freed (with kfree())
 *
 * Marks @p to be freed when processing the compound operation
 * described in @argp finishes.
 */
static void *
svcxdr_tmpalloc(struct nfsd4_compoundargs *argp, u32 len)
{
	struct svcxdr_tmpbuf *tb;

	tb = kmalloc(sizeof(*tb) + len, GFP_KERNEL);
	if (!tb)
		return NULL;
	tb->next = argp->to_free;
	argp->to_free = tb;
	return tb->buf;
}

/*
 * For xdr strings that need to be passed to other kernel api's
 * as null-terminated strings.
 *
 * Note null-terminating in place usually isn't safe since the
 * buffer might end on a page boundary.
 */
static char *
svcxdr_dupstr(struct nfsd4_compoundargs *argp, void *buf, u32 len)
{
	char *p = svcxdr_tmpalloc(argp, len + 1);

	if (!p)
		return NULL;
	memcpy(p, buf, len);
	p[len] = '\0';
	return p;
}

/**
 * savemem - duplicate a chunk of memory for later processing
 * @argp: NFSv4 compound argument structure to be freed with
 * @p: pointer to be duplicated
 * @nbytes: length to be duplicated
 *
 * Returns a pointer to a copy of @nbytes bytes of memory at @p
 * that are preserved until processing of the NFSv4 compound
 * operation described by @argp finishes.
 */
static char *savemem(struct nfsd4_compoundargs *argp, __be32 *p, int nbytes)
{
	void *ret;

	ret = svcxdr_tmpalloc(argp, nbytes);
	if (!ret)
		return NULL;
	memcpy(ret, p, nbytes);
	return ret;
}

/*
 * We require the high 32 bits of 'seconds' to be 0, and
 * we ignore all 32 bits of 'nseconds'.
 */
static __be32
nfsd4_decode_time(struct nfsd4_compoundargs *argp, struct timespec *tv)
{
	DECODE_HEAD;
	u64 sec;

	READ_BUF(12);
	p = xdr_decode_hyper(p, &sec);
	tv->tv_sec = sec;
	tv->tv_nsec = be32_to_cpup(p++);
	if (tv->tv_nsec >= (u32)1000000000)
		return nfserr_inval;

	DECODE_TAIL;
}

static __be32
nfsd4_decode_bitmap(struct nfsd4_compoundargs *argp, u32 *bmval)
{
	u32 bmlen;
	DECODE_HEAD;

	bmval[0] = 0;
	bmval[1] = 0;
	bmval[2] = 0;

	READ_BUF(4);
	bmlen = be32_to_cpup(p++);
	if (bmlen > 1000)
		goto xdr_error;

	READ_BUF(bmlen << 2);
	if (bmlen > 0)
		bmval[0] = be32_to_cpup(p++);
	if (bmlen > 1)
		bmval[1] = be32_to_cpup(p++);
	if (bmlen > 2)
		bmval[2] = be32_to_cpup(p++);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_fattr(struct nfsd4_compoundargs *argp, u32 *bmval,
		   struct iattr *iattr, struct nfs4_acl **acl,
		   struct xdr_netobj *label, int *umask)
{
	int expected_len, len = 0;
	u32 dummy32;
	char *buf;

	DECODE_HEAD;
	iattr->ia_valid = 0;
	if ((status = nfsd4_decode_bitmap(argp, bmval)))
		return status;

	if (bmval[0] & ~NFSD_WRITEABLE_ATTRS_WORD0
	    || bmval[1] & ~NFSD_WRITEABLE_ATTRS_WORD1
	    || bmval[2] & ~NFSD_WRITEABLE_ATTRS_WORD2) {
		if (nfsd_attrs_supported(argp->minorversion, bmval))
			return nfserr_inval;
		return nfserr_attrnotsupp;
	}

	READ_BUF(4);
	expected_len = be32_to_cpup(p++);

	if (bmval[0] & FATTR4_WORD0_SIZE) {
		READ_BUF(8);
		len += 8;
		p = xdr_decode_hyper(p, &iattr->ia_size);
		iattr->ia_valid |= ATTR_SIZE;
	}
	if (bmval[0] & FATTR4_WORD0_ACL) {
		u32 nace;
		struct nfs4_ace *ace;

		READ_BUF(4); len += 4;
		nace = be32_to_cpup(p++);

		if (nace > NFS4_ACL_MAX)
			return nfserr_fbig;

		*acl = svcxdr_tmpalloc(argp, nfs4_acl_bytes(nace));
		if (*acl == NULL)
			return nfserr_jukebox;

		(*acl)->naces = nace;
		for (ace = (*acl)->aces; ace < (*acl)->aces + nace; ace++) {
			READ_BUF(16); len += 16;
			ace->type = be32_to_cpup(p++);
			ace->flag = be32_to_cpup(p++);
			ace->access_mask = be32_to_cpup(p++);
			dummy32 = be32_to_cpup(p++);
			READ_BUF(dummy32);
			len += XDR_QUADLEN(dummy32) << 2;
			READMEM(buf, dummy32);
			ace->whotype = nfs4_acl_get_whotype(buf, dummy32);
			status = nfs_ok;
			if (ace->whotype != NFS4_ACL_WHO_NAMED)
				;
			else if (ace->flag & NFS4_ACE_IDENTIFIER_GROUP)
				status = nfsd_map_name_to_gid(argp->rqstp,
						buf, dummy32, &ace->who_gid);
			else
				status = nfsd_map_name_to_uid(argp->rqstp,
						buf, dummy32, &ace->who_uid);
			if (status)
				return status;
		}
	} else
		*acl = NULL;
	if (bmval[1] & FATTR4_WORD1_MODE) {
		READ_BUF(4);
		len += 4;
		iattr->ia_mode = be32_to_cpup(p++);
		iattr->ia_mode &= (S_IFMT | S_IALLUGO);
		iattr->ia_valid |= ATTR_MODE;
	}
	if (bmval[1] & FATTR4_WORD1_OWNER) {
		READ_BUF(4);
		len += 4;
		dummy32 = be32_to_cpup(p++);
		READ_BUF(dummy32);
		len += (XDR_QUADLEN(dummy32) << 2);
		READMEM(buf, dummy32);
		if ((status = nfsd_map_name_to_uid(argp->rqstp, buf, dummy32, &iattr->ia_uid)))
			return status;
		iattr->ia_valid |= ATTR_UID;
	}
	if (bmval[1] & FATTR4_WORD1_OWNER_GROUP) {
		READ_BUF(4);
		len += 4;
		dummy32 = be32_to_cpup(p++);
		READ_BUF(dummy32);
		len += (XDR_QUADLEN(dummy32) << 2);
		READMEM(buf, dummy32);
		if ((status = nfsd_map_name_to_gid(argp->rqstp, buf, dummy32, &iattr->ia_gid)))
			return status;
		iattr->ia_valid |= ATTR_GID;
	}
	if (bmval[1] & FATTR4_WORD1_TIME_ACCESS_SET) {
		READ_BUF(4);
		len += 4;
		dummy32 = be32_to_cpup(p++);
		switch (dummy32) {
		case NFS4_SET_TO_CLIENT_TIME:
			len += 12;
			status = nfsd4_decode_time(argp, &iattr->ia_atime);
			if (status)
				return status;
			iattr->ia_valid |= (ATTR_ATIME | ATTR_ATIME_SET);
			break;
		case NFS4_SET_TO_SERVER_TIME:
			iattr->ia_valid |= ATTR_ATIME;
			break;
		default:
			goto xdr_error;
		}
	}
	if (bmval[1] & FATTR4_WORD1_TIME_MODIFY_SET) {
		READ_BUF(4);
		len += 4;
		dummy32 = be32_to_cpup(p++);
		switch (dummy32) {
		case NFS4_SET_TO_CLIENT_TIME:
			len += 12;
			status = nfsd4_decode_time(argp, &iattr->ia_mtime);
			if (status)
				return status;
			iattr->ia_valid |= (ATTR_MTIME | ATTR_MTIME_SET);
			break;
		case NFS4_SET_TO_SERVER_TIME:
			iattr->ia_valid |= ATTR_MTIME;
			break;
		default:
			goto xdr_error;
		}
	}

	label->len = 0;
	if (IS_ENABLED(CONFIG_NFSD_V4_SECURITY_LABEL) &&
	    bmval[2] & FATTR4_WORD2_SECURITY_LABEL) {
		READ_BUF(4);
		len += 4;
		dummy32 = be32_to_cpup(p++); /* lfs: we don't use it */
		READ_BUF(4);
		len += 4;
		dummy32 = be32_to_cpup(p++); /* pi: we don't use it either */
		READ_BUF(4);
		len += 4;
		dummy32 = be32_to_cpup(p++);
		READ_BUF(dummy32);
		if (dummy32 > NFS4_MAXLABELLEN)
			return nfserr_badlabel;
		len += (XDR_QUADLEN(dummy32) << 2);
		READMEM(buf, dummy32);
		label->len = dummy32;
		label->data = svcxdr_dupstr(argp, buf, dummy32);
		if (!label->data)
			return nfserr_jukebox;
	}
	if (bmval[2] & FATTR4_WORD2_MODE_UMASK) {
		if (!umask)
			goto xdr_error;
		READ_BUF(8);
		len += 8;
		dummy32 = be32_to_cpup(p++);
		iattr->ia_mode = dummy32 & (S_IFMT | S_IALLUGO);
		dummy32 = be32_to_cpup(p++);
		*umask = dummy32 & S_IRWXUGO;
		iattr->ia_valid |= ATTR_MODE;
	}
	if (len != expected_len)
		goto xdr_error;

	DECODE_TAIL;
}

static __be32
nfsd4_decode_stateid(struct nfsd4_compoundargs *argp, stateid_t *sid)
{
	DECODE_HEAD;

	READ_BUF(sizeof(stateid_t));
	sid->si_generation = be32_to_cpup(p++);
	COPYMEM(&sid->si_opaque, sizeof(stateid_opaque_t));

	DECODE_TAIL;
}

static __be32
nfsd4_decode_access(struct nfsd4_compoundargs *argp, struct nfsd4_access *access)
{
	DECODE_HEAD;

	READ_BUF(4);
	access->ac_req_access = be32_to_cpup(p++);

	DECODE_TAIL;
}

static __be32 nfsd4_decode_cb_sec(struct nfsd4_compoundargs *argp, struct nfsd4_cb_sec *cbs)
{
	DECODE_HEAD;
	u32 dummy, uid, gid;
	char *machine_name;
	int i;
	int nr_secflavs;

	/* callback_sec_params4 */
	READ_BUF(4);
	nr_secflavs = be32_to_cpup(p++);
	if (nr_secflavs)
		cbs->flavor = (u32)(-1);
	else
		/* Is this legal? Be generous, take it to mean AUTH_NONE: */
		cbs->flavor = 0;
	for (i = 0; i < nr_secflavs; ++i) {
		READ_BUF(4);
		dummy = be32_to_cpup(p++);
		switch (dummy) {
		case RPC_AUTH_NULL:
			/* Nothing to read */
			if (cbs->flavor == (u32)(-1))
				cbs->flavor = RPC_AUTH_NULL;
			break;
		case RPC_AUTH_UNIX:
			READ_BUF(8);
			/* stamp */
			dummy = be32_to_cpup(p++);

			/* machine name */
			dummy = be32_to_cpup(p++);
			READ_BUF(dummy);
			SAVEMEM(machine_name, dummy);

			/* uid, gid */
			READ_BUF(8);
			uid = be32_to_cpup(p++);
			gid = be32_to_cpup(p++);

			/* more gids */
			READ_BUF(4);
			dummy = be32_to_cpup(p++);
			READ_BUF(dummy * 4);
			if (cbs->flavor == (u32)(-1)) {
				kuid_t kuid = make_kuid(&init_user_ns, uid);
				kgid_t kgid = make_kgid(&init_user_ns, gid);
				if (uid_valid(kuid) && gid_valid(kgid)) {
					cbs->uid = kuid;
					cbs->gid = kgid;
					cbs->flavor = RPC_AUTH_UNIX;
				} else {
					dprintk("RPC_AUTH_UNIX with invalid"
						"uid or gid ignoring!\n");
				}
			}
			break;
		case RPC_AUTH_GSS:
			dprintk("RPC_AUTH_GSS callback secflavor "
				"not supported!\n");
			READ_BUF(8);
			/* gcbp_service */
			dummy = be32_to_cpup(p++);
			/* gcbp_handle_from_server */
			dummy = be32_to_cpup(p++);
			READ_BUF(dummy);
			p += XDR_QUADLEN(dummy);
			/* gcbp_handle_from_client */
			READ_BUF(4);
			dummy = be32_to_cpup(p++);
			READ_BUF(dummy);
			break;
		default:
			dprintk("Illegal callback secflavor\n");
			return nfserr_inval;
		}
	}
	DECODE_TAIL;
}

static __be32 nfsd4_decode_backchannel_ctl(struct nfsd4_compoundargs *argp, struct nfsd4_backchannel_ctl *bc)
{
	DECODE_HEAD;

	READ_BUF(4);
	bc->bc_cb_program = be32_to_cpup(p++);
	nfsd4_decode_cb_sec(argp, &bc->bc_cb_sec);

	DECODE_TAIL;
}

static __be32 nfsd4_decode_bind_conn_to_session(struct nfsd4_compoundargs *argp, struct nfsd4_bind_conn_to_session *bcts)
{
	DECODE_HEAD;

	READ_BUF(NFS4_MAX_SESSIONID_LEN + 8);
	COPYMEM(bcts->sessionid.data, NFS4_MAX_SESSIONID_LEN);
	bcts->dir = be32_to_cpup(p++);
	/* XXX: skipping ctsa_use_conn_in_rdma_mode.  Perhaps Tom Tucker
	 * could help us figure out we should be using it. */
	DECODE_TAIL;
}

static __be32
nfsd4_decode_close(struct nfsd4_compoundargs *argp, struct nfsd4_close *close)
{
	DECODE_HEAD;

	READ_BUF(4);
	close->cl_seqid = be32_to_cpup(p++);
	return nfsd4_decode_stateid(argp, &close->cl_stateid);

	DECODE_TAIL;
}


static __be32
nfsd4_decode_commit(struct nfsd4_compoundargs *argp, struct nfsd4_commit *commit)
{
	DECODE_HEAD;

	READ_BUF(12);
	p = xdr_decode_hyper(p, &commit->co_offset);
	commit->co_count = be32_to_cpup(p++);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_create(struct nfsd4_compoundargs *argp, struct nfsd4_create *create)
{
	DECODE_HEAD;

	READ_BUF(4);
	create->cr_type = be32_to_cpup(p++);
	switch (create->cr_type) {
	case NF4LNK:
		READ_BUF(4);
		create->cr_datalen = be32_to_cpup(p++);
		READ_BUF(create->cr_datalen);
		create->cr_data = svcxdr_dupstr(argp, p, create->cr_datalen);
		if (!create->cr_data)
			return nfserr_jukebox;
		break;
	case NF4BLK:
	case NF4CHR:
		READ_BUF(8);
		create->cr_specdata1 = be32_to_cpup(p++);
		create->cr_specdata2 = be32_to_cpup(p++);
		break;
	case NF4SOCK:
	case NF4FIFO:
	case NF4DIR:
	default:
		break;
	}

	READ_BUF(4);
	create->cr_namelen = be32_to_cpup(p++);
	READ_BUF(create->cr_namelen);
	SAVEMEM(create->cr_name, create->cr_namelen);
	if ((status = check_filename(create->cr_name, create->cr_namelen)))
		return status;

	status = nfsd4_decode_fattr(argp, create->cr_bmval, &create->cr_iattr,
				    &create->cr_acl, &create->cr_label,
				    &create->cr_umask);
	if (status)
		goto out;

	DECODE_TAIL;
}

static inline __be32
nfsd4_decode_delegreturn(struct nfsd4_compoundargs *argp, struct nfsd4_delegreturn *dr)
{
	return nfsd4_decode_stateid(argp, &dr->dr_stateid);
}

static inline __be32
nfsd4_decode_getattr(struct nfsd4_compoundargs *argp, struct nfsd4_getattr *getattr)
{
	return nfsd4_decode_bitmap(argp, getattr->ga_bmval);
}

static __be32
nfsd4_decode_link(struct nfsd4_compoundargs *argp, struct nfsd4_link *link)
{
	DECODE_HEAD;

	READ_BUF(4);
	link->li_namelen = be32_to_cpup(p++);
	READ_BUF(link->li_namelen);
	SAVEMEM(link->li_name, link->li_namelen);
	if ((status = check_filename(link->li_name, link->li_namelen)))
		return status;

	DECODE_TAIL;
}

static __be32
nfsd4_decode_lock(struct nfsd4_compoundargs *argp, struct nfsd4_lock *lock)
{
	DECODE_HEAD;

	/*
	* type, reclaim(boolean), offset, length, new_lock_owner(boolean)
	*/
	READ_BUF(28);
	lock->lk_type = be32_to_cpup(p++);
	if ((lock->lk_type < NFS4_READ_LT) || (lock->lk_type > NFS4_WRITEW_LT))
		goto xdr_error;
	lock->lk_reclaim = be32_to_cpup(p++);
	p = xdr_decode_hyper(p, &lock->lk_offset);
	p = xdr_decode_hyper(p, &lock->lk_length);
	lock->lk_is_new = be32_to_cpup(p++);

	if (lock->lk_is_new) {
		READ_BUF(4);
		lock->lk_new_open_seqid = be32_to_cpup(p++);
		status = nfsd4_decode_stateid(argp, &lock->lk_new_open_stateid);
		if (status)
			return status;
		READ_BUF(8 + sizeof(clientid_t));
		lock->lk_new_lock_seqid = be32_to_cpup(p++);
		COPYMEM(&lock->lk_new_clientid, sizeof(clientid_t));
		lock->lk_new_owner.len = be32_to_cpup(p++);
		READ_BUF(lock->lk_new_owner.len);
		READMEM(lock->lk_new_owner.data, lock->lk_new_owner.len);
	} else {
		status = nfsd4_decode_stateid(argp, &lock->lk_old_lock_stateid);
		if (status)
			return status;
		READ_BUF(4);
		lock->lk_old_lock_seqid = be32_to_cpup(p++);
	}

	DECODE_TAIL;
}

static __be32
nfsd4_decode_lockt(struct nfsd4_compoundargs *argp, struct nfsd4_lockt *lockt)
{
	DECODE_HEAD;
		        
	READ_BUF(32);
	lockt->lt_type = be32_to_cpup(p++);
	if((lockt->lt_type < NFS4_READ_LT) || (lockt->lt_type > NFS4_WRITEW_LT))
		goto xdr_error;
	p = xdr_decode_hyper(p, &lockt->lt_offset);
	p = xdr_decode_hyper(p, &lockt->lt_length);
	COPYMEM(&lockt->lt_clientid, 8);
	lockt->lt_owner.len = be32_to_cpup(p++);
	READ_BUF(lockt->lt_owner.len);
	READMEM(lockt->lt_owner.data, lockt->lt_owner.len);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_locku(struct nfsd4_compoundargs *argp, struct nfsd4_locku *locku)
{
	DECODE_HEAD;

	READ_BUF(8);
	locku->lu_type = be32_to_cpup(p++);
	if ((locku->lu_type < NFS4_READ_LT) || (locku->lu_type > NFS4_WRITEW_LT))
		goto xdr_error;
	locku->lu_seqid = be32_to_cpup(p++);
	status = nfsd4_decode_stateid(argp, &locku->lu_stateid);
	if (status)
		return status;
	READ_BUF(16);
	p = xdr_decode_hyper(p, &locku->lu_offset);
	p = xdr_decode_hyper(p, &locku->lu_length);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_lookup(struct nfsd4_compoundargs *argp, struct nfsd4_lookup *lookup)
{
	DECODE_HEAD;

	READ_BUF(4);
	lookup->lo_len = be32_to_cpup(p++);
	READ_BUF(lookup->lo_len);
	SAVEMEM(lookup->lo_name, lookup->lo_len);
	if ((status = check_filename(lookup->lo_name, lookup->lo_len)))
		return status;

	DECODE_TAIL;
}

static __be32 nfsd4_decode_share_access(struct nfsd4_compoundargs *argp, u32 *share_access, u32 *deleg_want, u32 *deleg_when)
{
	__be32 *p;
	u32 w;

	READ_BUF(4);
	w = be32_to_cpup(p++);
	*share_access = w & NFS4_SHARE_ACCESS_MASK;
	*deleg_want = w & NFS4_SHARE_WANT_MASK;
	if (deleg_when)
		*deleg_when = w & NFS4_SHARE_WHEN_MASK;

	switch (w & NFS4_SHARE_ACCESS_MASK) {
	case NFS4_SHARE_ACCESS_READ:
	case NFS4_SHARE_ACCESS_WRITE:
	case NFS4_SHARE_ACCESS_BOTH:
		break;
	default:
		return nfserr_bad_xdr;
	}
	w &= ~NFS4_SHARE_ACCESS_MASK;
	if (!w)
		return nfs_ok;
	if (!argp->minorversion)
		return nfserr_bad_xdr;
	switch (w & NFS4_SHARE_WANT_MASK) {
	case NFS4_SHARE_WANT_NO_PREFERENCE:
	case NFS4_SHARE_WANT_READ_DELEG:
	case NFS4_SHARE_WANT_WRITE_DELEG:
	case NFS4_SHARE_WANT_ANY_DELEG:
	case NFS4_SHARE_WANT_NO_DELEG:
	case NFS4_SHARE_WANT_CANCEL:
		break;
	default:
		return nfserr_bad_xdr;
	}
	w &= ~NFS4_SHARE_WANT_MASK;
	if (!w)
		return nfs_ok;

	if (!deleg_when)	/* open_downgrade */
		return nfserr_inval;
	switch (w) {
	case NFS4_SHARE_SIGNAL_DELEG_WHEN_RESRC_AVAIL:
	case NFS4_SHARE_PUSH_DELEG_WHEN_UNCONTENDED:
	case (NFS4_SHARE_SIGNAL_DELEG_WHEN_RESRC_AVAIL |
	      NFS4_SHARE_PUSH_DELEG_WHEN_UNCONTENDED):
		return nfs_ok;
	}
xdr_error:
	return nfserr_bad_xdr;
}

static __be32 nfsd4_decode_share_deny(struct nfsd4_compoundargs *argp, u32 *x)
{
	__be32 *p;

	READ_BUF(4);
	*x = be32_to_cpup(p++);
	/* Note: unlinke access bits, deny bits may be zero. */
	if (*x & ~NFS4_SHARE_DENY_BOTH)
		return nfserr_bad_xdr;
	return nfs_ok;
xdr_error:
	return nfserr_bad_xdr;
}

static __be32 nfsd4_decode_opaque(struct nfsd4_compoundargs *argp, struct xdr_netobj *o)
{
	__be32 *p;

	READ_BUF(4);
	o->len = be32_to_cpup(p++);

	if (o->len == 0 || o->len > NFS4_OPAQUE_LIMIT)
		return nfserr_bad_xdr;

	READ_BUF(o->len);
	SAVEMEM(o->data, o->len);
	return nfs_ok;
xdr_error:
	return nfserr_bad_xdr;
}

static __be32
nfsd4_decode_open(struct nfsd4_compoundargs *argp, struct nfsd4_open *open)
{
	DECODE_HEAD;
	u32 dummy;

	memset(open->op_bmval, 0, sizeof(open->op_bmval));
	open->op_iattr.ia_valid = 0;
	open->op_openowner = NULL;

	open->op_xdr_error = 0;
	/* seqid, share_access, share_deny, clientid, ownerlen */
	READ_BUF(4);
	open->op_seqid = be32_to_cpup(p++);
	/* decode, yet ignore deleg_when until supported */
	status = nfsd4_decode_share_access(argp, &open->op_share_access,
					   &open->op_deleg_want, &dummy);
	if (status)
		goto xdr_error;
	status = nfsd4_decode_share_deny(argp, &open->op_share_deny);
	if (status)
		goto xdr_error;
	READ_BUF(sizeof(clientid_t));
	COPYMEM(&open->op_clientid, sizeof(clientid_t));
	status = nfsd4_decode_opaque(argp, &open->op_owner);
	if (status)
		goto xdr_error;
	READ_BUF(4);
	open->op_create = be32_to_cpup(p++);
	switch (open->op_create) {
	case NFS4_OPEN_NOCREATE:
		break;
	case NFS4_OPEN_CREATE:
		READ_BUF(4);
		open->op_createmode = be32_to_cpup(p++);
		switch (open->op_createmode) {
		case NFS4_CREATE_UNCHECKED:
		case NFS4_CREATE_GUARDED:
			status = nfsd4_decode_fattr(argp, open->op_bmval,
				&open->op_iattr, &open->op_acl, &open->op_label,
				&open->op_umask);
			if (status)
				goto out;
			break;
		case NFS4_CREATE_EXCLUSIVE:
			READ_BUF(NFS4_VERIFIER_SIZE);
			COPYMEM(open->op_verf.data, NFS4_VERIFIER_SIZE);
			break;
		case NFS4_CREATE_EXCLUSIVE4_1:
			if (argp->minorversion < 1)
				goto xdr_error;
			READ_BUF(NFS4_VERIFIER_SIZE);
			COPYMEM(open->op_verf.data, NFS4_VERIFIER_SIZE);
			status = nfsd4_decode_fattr(argp, open->op_bmval,
				&open->op_iattr, &open->op_acl, &open->op_label,
				&open->op_umask);
			if (status)
				goto out;
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
	open->op_claim_type = be32_to_cpup(p++);
	switch (open->op_claim_type) {
	case NFS4_OPEN_CLAIM_NULL:
	case NFS4_OPEN_CLAIM_DELEGATE_PREV:
		READ_BUF(4);
		open->op_fname.len = be32_to_cpup(p++);
		READ_BUF(open->op_fname.len);
		SAVEMEM(open->op_fname.data, open->op_fname.len);
		if ((status = check_filename(open->op_fname.data, open->op_fname.len)))
			return status;
		break;
	case NFS4_OPEN_CLAIM_PREVIOUS:
		READ_BUF(4);
		open->op_delegate_type = be32_to_cpup(p++);
		break;
	case NFS4_OPEN_CLAIM_DELEGATE_CUR:
		status = nfsd4_decode_stateid(argp, &open->op_delegate_stateid);
		if (status)
			return status;
		READ_BUF(4);
		open->op_fname.len = be32_to_cpup(p++);
		READ_BUF(open->op_fname.len);
		SAVEMEM(open->op_fname.data, open->op_fname.len);
		if ((status = check_filename(open->op_fname.data, open->op_fname.len)))
			return status;
		break;
	case NFS4_OPEN_CLAIM_FH:
	case NFS4_OPEN_CLAIM_DELEG_PREV_FH:
		if (argp->minorversion < 1)
			goto xdr_error;
		/* void */
		break;
	case NFS4_OPEN_CLAIM_DELEG_CUR_FH:
		if (argp->minorversion < 1)
			goto xdr_error;
		status = nfsd4_decode_stateid(argp, &open->op_delegate_stateid);
		if (status)
			return status;
		break;
	default:
		goto xdr_error;
	}

	DECODE_TAIL;
}

static __be32
nfsd4_decode_open_confirm(struct nfsd4_compoundargs *argp, struct nfsd4_open_confirm *open_conf)
{
	DECODE_HEAD;

	if (argp->minorversion >= 1)
		return nfserr_notsupp;

	status = nfsd4_decode_stateid(argp, &open_conf->oc_req_stateid);
	if (status)
		return status;
	READ_BUF(4);
	open_conf->oc_seqid = be32_to_cpup(p++);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_open_downgrade(struct nfsd4_compoundargs *argp, struct nfsd4_open_downgrade *open_down)
{
	DECODE_HEAD;
		    
	status = nfsd4_decode_stateid(argp, &open_down->od_stateid);
	if (status)
		return status;
	READ_BUF(4);
	open_down->od_seqid = be32_to_cpup(p++);
	status = nfsd4_decode_share_access(argp, &open_down->od_share_access,
					   &open_down->od_deleg_want, NULL);
	if (status)
		return status;
	status = nfsd4_decode_share_deny(argp, &open_down->od_share_deny);
	if (status)
		return status;
	DECODE_TAIL;
}

static __be32
nfsd4_decode_putfh(struct nfsd4_compoundargs *argp, struct nfsd4_putfh *putfh)
{
	DECODE_HEAD;

	READ_BUF(4);
	putfh->pf_fhlen = be32_to_cpup(p++);
	if (putfh->pf_fhlen > NFS4_FHSIZE)
		goto xdr_error;
	READ_BUF(putfh->pf_fhlen);
	SAVEMEM(putfh->pf_fhval, putfh->pf_fhlen);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_putpubfh(struct nfsd4_compoundargs *argp, void *p)
{
	if (argp->minorversion == 0)
		return nfs_ok;
	return nfserr_notsupp;
}

static __be32
nfsd4_decode_read(struct nfsd4_compoundargs *argp, struct nfsd4_read *read)
{
	DECODE_HEAD;

	status = nfsd4_decode_stateid(argp, &read->rd_stateid);
	if (status)
		return status;
	READ_BUF(12);
	p = xdr_decode_hyper(p, &read->rd_offset);
	read->rd_length = be32_to_cpup(p++);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_readdir(struct nfsd4_compoundargs *argp, struct nfsd4_readdir *readdir)
{
	DECODE_HEAD;

	READ_BUF(24);
	p = xdr_decode_hyper(p, &readdir->rd_cookie);
	COPYMEM(readdir->rd_verf.data, sizeof(readdir->rd_verf.data));
	readdir->rd_dircount = be32_to_cpup(p++);
	readdir->rd_maxcount = be32_to_cpup(p++);
	if ((status = nfsd4_decode_bitmap(argp, readdir->rd_bmval)))
		goto out;

	DECODE_TAIL;
}

static __be32
nfsd4_decode_remove(struct nfsd4_compoundargs *argp, struct nfsd4_remove *remove)
{
	DECODE_HEAD;

	READ_BUF(4);
	remove->rm_namelen = be32_to_cpup(p++);
	READ_BUF(remove->rm_namelen);
	SAVEMEM(remove->rm_name, remove->rm_namelen);
	if ((status = check_filename(remove->rm_name, remove->rm_namelen)))
		return status;

	DECODE_TAIL;
}

static __be32
nfsd4_decode_rename(struct nfsd4_compoundargs *argp, struct nfsd4_rename *rename)
{
	DECODE_HEAD;

	READ_BUF(4);
	rename->rn_snamelen = be32_to_cpup(p++);
	READ_BUF(rename->rn_snamelen);
	SAVEMEM(rename->rn_sname, rename->rn_snamelen);
	READ_BUF(4);
	rename->rn_tnamelen = be32_to_cpup(p++);
	READ_BUF(rename->rn_tnamelen);
	SAVEMEM(rename->rn_tname, rename->rn_tnamelen);
	if ((status = check_filename(rename->rn_sname, rename->rn_snamelen)))
		return status;
	if ((status = check_filename(rename->rn_tname, rename->rn_tnamelen)))
		return status;

	DECODE_TAIL;
}

static __be32
nfsd4_decode_renew(struct nfsd4_compoundargs *argp, clientid_t *clientid)
{
	DECODE_HEAD;

	if (argp->minorversion >= 1)
		return nfserr_notsupp;

	READ_BUF(sizeof(clientid_t));
	COPYMEM(clientid, sizeof(clientid_t));

	DECODE_TAIL;
}

static __be32
nfsd4_decode_secinfo(struct nfsd4_compoundargs *argp,
		     struct nfsd4_secinfo *secinfo)
{
	DECODE_HEAD;

	READ_BUF(4);
	secinfo->si_namelen = be32_to_cpup(p++);
	READ_BUF(secinfo->si_namelen);
	SAVEMEM(secinfo->si_name, secinfo->si_namelen);
	status = check_filename(secinfo->si_name, secinfo->si_namelen);
	if (status)
		return status;
	DECODE_TAIL;
}

static __be32
nfsd4_decode_secinfo_no_name(struct nfsd4_compoundargs *argp,
		     struct nfsd4_secinfo_no_name *sin)
{
	DECODE_HEAD;

	READ_BUF(4);
	sin->sin_style = be32_to_cpup(p++);
	DECODE_TAIL;
}

static __be32
nfsd4_decode_setattr(struct nfsd4_compoundargs *argp, struct nfsd4_setattr *setattr)
{
	__be32 status;

	status = nfsd4_decode_stateid(argp, &setattr->sa_stateid);
	if (status)
		return status;
	return nfsd4_decode_fattr(argp, setattr->sa_bmval, &setattr->sa_iattr,
				  &setattr->sa_acl, &setattr->sa_label, NULL);
}

static __be32
nfsd4_decode_setclientid(struct nfsd4_compoundargs *argp, struct nfsd4_setclientid *setclientid)
{
	DECODE_HEAD;

	if (argp->minorversion >= 1)
		return nfserr_notsupp;

	READ_BUF(NFS4_VERIFIER_SIZE);
	COPYMEM(setclientid->se_verf.data, NFS4_VERIFIER_SIZE);

	status = nfsd4_decode_opaque(argp, &setclientid->se_name);
	if (status)
		return nfserr_bad_xdr;
	READ_BUF(8);
	setclientid->se_callback_prog = be32_to_cpup(p++);
	setclientid->se_callback_netid_len = be32_to_cpup(p++);
	READ_BUF(setclientid->se_callback_netid_len);
	SAVEMEM(setclientid->se_callback_netid_val, setclientid->se_callback_netid_len);
	READ_BUF(4);
	setclientid->se_callback_addr_len = be32_to_cpup(p++);

	READ_BUF(setclientid->se_callback_addr_len);
	SAVEMEM(setclientid->se_callback_addr_val, setclientid->se_callback_addr_len);
	READ_BUF(4);
	setclientid->se_callback_ident = be32_to_cpup(p++);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_setclientid_confirm(struct nfsd4_compoundargs *argp, struct nfsd4_setclientid_confirm *scd_c)
{
	DECODE_HEAD;

	if (argp->minorversion >= 1)
		return nfserr_notsupp;

	READ_BUF(8 + NFS4_VERIFIER_SIZE);
	COPYMEM(&scd_c->sc_clientid, 8);
	COPYMEM(&scd_c->sc_confirm, NFS4_VERIFIER_SIZE);

	DECODE_TAIL;
}

/* Also used for NVERIFY */
static __be32
nfsd4_decode_verify(struct nfsd4_compoundargs *argp, struct nfsd4_verify *verify)
{
	DECODE_HEAD;

	if ((status = nfsd4_decode_bitmap(argp, verify->ve_bmval)))
		goto out;

	/* For convenience's sake, we compare raw xdr'd attributes in
	 * nfsd4_proc_verify */

	READ_BUF(4);
	verify->ve_attrlen = be32_to_cpup(p++);
	READ_BUF(verify->ve_attrlen);
	SAVEMEM(verify->ve_attrval, verify->ve_attrlen);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_write(struct nfsd4_compoundargs *argp, struct nfsd4_write *write)
{
	int avail;
	int len;
	DECODE_HEAD;

	status = nfsd4_decode_stateid(argp, &write->wr_stateid);
	if (status)
		return status;
	READ_BUF(16);
	p = xdr_decode_hyper(p, &write->wr_offset);
	write->wr_stable_how = be32_to_cpup(p++);
	if (write->wr_stable_how > NFS_FILE_SYNC)
		goto xdr_error;
	write->wr_buflen = be32_to_cpup(p++);

	/* Sorry .. no magic macros for this.. *
	 * READ_BUF(write->wr_buflen);
	 * SAVEMEM(write->wr_buf, write->wr_buflen);
	 */
	avail = (char*)argp->end - (char*)argp->p;
	if (avail + argp->pagelen < write->wr_buflen) {
		dprintk("NFSD: xdr error (%s:%d)\n",
				__FILE__, __LINE__);
		goto xdr_error;
	}
	write->wr_head.iov_base = p;
	write->wr_head.iov_len = avail;
	write->wr_pagelist = argp->pagelist;

	len = XDR_QUADLEN(write->wr_buflen) << 2;
	if (len >= avail) {
		int pages;

		len -= avail;

		pages = len >> PAGE_SHIFT;
		argp->pagelist += pages;
		argp->pagelen -= pages * PAGE_SIZE;
		len -= pages * PAGE_SIZE;

		next_decode_page(argp);
	}
	argp->p += XDR_QUADLEN(len);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_release_lockowner(struct nfsd4_compoundargs *argp, struct nfsd4_release_lockowner *rlockowner)
{
	DECODE_HEAD;

	if (argp->minorversion >= 1)
		return nfserr_notsupp;

	READ_BUF(12);
	COPYMEM(&rlockowner->rl_clientid, sizeof(clientid_t));
	rlockowner->rl_owner.len = be32_to_cpup(p++);
	READ_BUF(rlockowner->rl_owner.len);
	READMEM(rlockowner->rl_owner.data, rlockowner->rl_owner.len);

	if (argp->minorversion && !zero_clientid(&rlockowner->rl_clientid))
		return nfserr_inval;
	DECODE_TAIL;
}

static __be32
nfsd4_decode_exchange_id(struct nfsd4_compoundargs *argp,
			 struct nfsd4_exchange_id *exid)
{
	int dummy, tmp;
	DECODE_HEAD;

	READ_BUF(NFS4_VERIFIER_SIZE);
	COPYMEM(exid->verifier.data, NFS4_VERIFIER_SIZE);

	status = nfsd4_decode_opaque(argp, &exid->clname);
	if (status)
		return nfserr_bad_xdr;

	READ_BUF(4);
	exid->flags = be32_to_cpup(p++);

	/* Ignore state_protect4_a */
	READ_BUF(4);
	exid->spa_how = be32_to_cpup(p++);
	switch (exid->spa_how) {
	case SP4_NONE:
		break;
	case SP4_MACH_CRED:
		/* spo_must_enforce */
		status = nfsd4_decode_bitmap(argp,
					exid->spo_must_enforce);
		if (status)
			goto out;
		/* spo_must_allow */
		status = nfsd4_decode_bitmap(argp, exid->spo_must_allow);
		if (status)
			goto out;
		break;
	case SP4_SSV:
		/* ssp_ops */
		READ_BUF(4);
		dummy = be32_to_cpup(p++);
		READ_BUF(dummy * 4);
		p += dummy;

		READ_BUF(4);
		dummy = be32_to_cpup(p++);
		READ_BUF(dummy * 4);
		p += dummy;

		/* ssp_hash_algs<> */
		READ_BUF(4);
		tmp = be32_to_cpup(p++);
		while (tmp--) {
			READ_BUF(4);
			dummy = be32_to_cpup(p++);
			READ_BUF(dummy);
			p += XDR_QUADLEN(dummy);
		}

		/* ssp_encr_algs<> */
		READ_BUF(4);
		tmp = be32_to_cpup(p++);
		while (tmp--) {
			READ_BUF(4);
			dummy = be32_to_cpup(p++);
			READ_BUF(dummy);
			p += XDR_QUADLEN(dummy);
		}

		/* ssp_window and ssp_num_gss_handles */
		READ_BUF(8);
		dummy = be32_to_cpup(p++);
		dummy = be32_to_cpup(p++);
		break;
	default:
		goto xdr_error;
	}

	/* Ignore Implementation ID */
	READ_BUF(4);    /* nfs_impl_id4 array length */
	dummy = be32_to_cpup(p++);

	if (dummy > 1)
		goto xdr_error;

	if (dummy == 1) {
		/* nii_domain */
		READ_BUF(4);
		dummy = be32_to_cpup(p++);
		READ_BUF(dummy);
		p += XDR_QUADLEN(dummy);

		/* nii_name */
		READ_BUF(4);
		dummy = be32_to_cpup(p++);
		READ_BUF(dummy);
		p += XDR_QUADLEN(dummy);

		/* nii_date */
		READ_BUF(12);
		p += 3;
	}
	DECODE_TAIL;
}

static __be32
nfsd4_decode_create_session(struct nfsd4_compoundargs *argp,
			    struct nfsd4_create_session *sess)
{
	DECODE_HEAD;
	u32 dummy;

	READ_BUF(16);
	COPYMEM(&sess->clientid, 8);
	sess->seqid = be32_to_cpup(p++);
	sess->flags = be32_to_cpup(p++);

	/* Fore channel attrs */
	READ_BUF(28);
	dummy = be32_to_cpup(p++); /* headerpadsz is always 0 */
	sess->fore_channel.maxreq_sz = be32_to_cpup(p++);
	sess->fore_channel.maxresp_sz = be32_to_cpup(p++);
	sess->fore_channel.maxresp_cached = be32_to_cpup(p++);
	sess->fore_channel.maxops = be32_to_cpup(p++);
	sess->fore_channel.maxreqs = be32_to_cpup(p++);
	sess->fore_channel.nr_rdma_attrs = be32_to_cpup(p++);
	if (sess->fore_channel.nr_rdma_attrs == 1) {
		READ_BUF(4);
		sess->fore_channel.rdma_attrs = be32_to_cpup(p++);
	} else if (sess->fore_channel.nr_rdma_attrs > 1) {
		dprintk("Too many fore channel attr bitmaps!\n");
		goto xdr_error;
	}

	/* Back channel attrs */
	READ_BUF(28);
	dummy = be32_to_cpup(p++); /* headerpadsz is always 0 */
	sess->back_channel.maxreq_sz = be32_to_cpup(p++);
	sess->back_channel.maxresp_sz = be32_to_cpup(p++);
	sess->back_channel.maxresp_cached = be32_to_cpup(p++);
	sess->back_channel.maxops = be32_to_cpup(p++);
	sess->back_channel.maxreqs = be32_to_cpup(p++);
	sess->back_channel.nr_rdma_attrs = be32_to_cpup(p++);
	if (sess->back_channel.nr_rdma_attrs == 1) {
		READ_BUF(4);
		sess->back_channel.rdma_attrs = be32_to_cpup(p++);
	} else if (sess->back_channel.nr_rdma_attrs > 1) {
		dprintk("Too many back channel attr bitmaps!\n");
		goto xdr_error;
	}

	READ_BUF(4);
	sess->callback_prog = be32_to_cpup(p++);
	nfsd4_decode_cb_sec(argp, &sess->cb_sec);
	DECODE_TAIL;
}

static __be32
nfsd4_decode_destroy_session(struct nfsd4_compoundargs *argp,
			     struct nfsd4_destroy_session *destroy_session)
{
	DECODE_HEAD;
	READ_BUF(NFS4_MAX_SESSIONID_LEN);
	COPYMEM(destroy_session->sessionid.data, NFS4_MAX_SESSIONID_LEN);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_free_stateid(struct nfsd4_compoundargs *argp,
			  struct nfsd4_free_stateid *free_stateid)
{
	DECODE_HEAD;

	READ_BUF(sizeof(stateid_t));
	free_stateid->fr_stateid.si_generation = be32_to_cpup(p++);
	COPYMEM(&free_stateid->fr_stateid.si_opaque, sizeof(stateid_opaque_t));

	DECODE_TAIL;
}

static __be32
nfsd4_decode_sequence(struct nfsd4_compoundargs *argp,
		      struct nfsd4_sequence *seq)
{
	DECODE_HEAD;

	READ_BUF(NFS4_MAX_SESSIONID_LEN + 16);
	COPYMEM(seq->sessionid.data, NFS4_MAX_SESSIONID_LEN);
	seq->seqid = be32_to_cpup(p++);
	seq->slotid = be32_to_cpup(p++);
	seq->maxslots = be32_to_cpup(p++);
	seq->cachethis = be32_to_cpup(p++);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_test_stateid(struct nfsd4_compoundargs *argp, struct nfsd4_test_stateid *test_stateid)
{
	int i;
	__be32 *p, status;
	struct nfsd4_test_stateid_id *stateid;

	READ_BUF(4);
	test_stateid->ts_num_ids = ntohl(*p++);

	INIT_LIST_HEAD(&test_stateid->ts_stateid_list);

	for (i = 0; i < test_stateid->ts_num_ids; i++) {
		stateid = svcxdr_tmpalloc(argp, sizeof(*stateid));
		if (!stateid) {
			status = nfserrno(-ENOMEM);
			goto out;
		}

		INIT_LIST_HEAD(&stateid->ts_id_list);
		list_add_tail(&stateid->ts_id_list, &test_stateid->ts_stateid_list);

		status = nfsd4_decode_stateid(argp, &stateid->ts_id_stateid);
		if (status)
			goto out;
	}

	status = 0;
out:
	return status;
xdr_error:
	dprintk("NFSD: xdr error (%s:%d)\n", __FILE__, __LINE__);
	status = nfserr_bad_xdr;
	goto out;
}

static __be32 nfsd4_decode_destroy_clientid(struct nfsd4_compoundargs *argp, struct nfsd4_destroy_clientid *dc)
{
	DECODE_HEAD;

	READ_BUF(8);
	COPYMEM(&dc->clientid, 8);

	DECODE_TAIL;
}

static __be32 nfsd4_decode_reclaim_complete(struct nfsd4_compoundargs *argp, struct nfsd4_reclaim_complete *rc)
{
	DECODE_HEAD;

	READ_BUF(4);
	rc->rca_one_fs = be32_to_cpup(p++);

	DECODE_TAIL;
}

#ifdef CONFIG_NFSD_PNFS
static __be32
nfsd4_decode_getdeviceinfo(struct nfsd4_compoundargs *argp,
		struct nfsd4_getdeviceinfo *gdev)
{
	DECODE_HEAD;
	u32 num, i;

	READ_BUF(sizeof(struct nfsd4_deviceid) + 3 * 4);
	COPYMEM(&gdev->gd_devid, sizeof(struct nfsd4_deviceid));
	gdev->gd_layout_type = be32_to_cpup(p++);
	gdev->gd_maxcount = be32_to_cpup(p++);
	num = be32_to_cpup(p++);
	if (num) {
		READ_BUF(4 * num);
		gdev->gd_notify_types = be32_to_cpup(p++);
		for (i = 1; i < num; i++) {
			if (be32_to_cpup(p++)) {
				status = nfserr_inval;
				goto out;
			}
		}
	}
	DECODE_TAIL;
}

static __be32
nfsd4_decode_layoutget(struct nfsd4_compoundargs *argp,
		struct nfsd4_layoutget *lgp)
{
	DECODE_HEAD;

	READ_BUF(36);
	lgp->lg_signal = be32_to_cpup(p++);
	lgp->lg_layout_type = be32_to_cpup(p++);
	lgp->lg_seg.iomode = be32_to_cpup(p++);
	p = xdr_decode_hyper(p, &lgp->lg_seg.offset);
	p = xdr_decode_hyper(p, &lgp->lg_seg.length);
	p = xdr_decode_hyper(p, &lgp->lg_minlength);

	status = nfsd4_decode_stateid(argp, &lgp->lg_sid);
	if (status)
		return status;

	READ_BUF(4);
	lgp->lg_maxcount = be32_to_cpup(p++);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_layoutcommit(struct nfsd4_compoundargs *argp,
		struct nfsd4_layoutcommit *lcp)
{
	DECODE_HEAD;
	u32 timechange;

	READ_BUF(20);
	p = xdr_decode_hyper(p, &lcp->lc_seg.offset);
	p = xdr_decode_hyper(p, &lcp->lc_seg.length);
	lcp->lc_reclaim = be32_to_cpup(p++);

	status = nfsd4_decode_stateid(argp, &lcp->lc_sid);
	if (status)
		return status;

	READ_BUF(4);
	lcp->lc_newoffset = be32_to_cpup(p++);
	if (lcp->lc_newoffset) {
		READ_BUF(8);
		p = xdr_decode_hyper(p, &lcp->lc_last_wr);
	} else
		lcp->lc_last_wr = 0;
	READ_BUF(4);
	timechange = be32_to_cpup(p++);
	if (timechange) {
		status = nfsd4_decode_time(argp, &lcp->lc_mtime);
		if (status)
			return status;
	} else {
		lcp->lc_mtime.tv_nsec = UTIME_NOW;
	}
	READ_BUF(8);
	lcp->lc_layout_type = be32_to_cpup(p++);

	/*
	 * Save the layout update in XDR format and let the layout driver deal
	 * with it later.
	 */
	lcp->lc_up_len = be32_to_cpup(p++);
	if (lcp->lc_up_len > 0) {
		READ_BUF(lcp->lc_up_len);
		READMEM(lcp->lc_up_layout, lcp->lc_up_len);
	}

	DECODE_TAIL;
}

static __be32
nfsd4_decode_layoutreturn(struct nfsd4_compoundargs *argp,
		struct nfsd4_layoutreturn *lrp)
{
	DECODE_HEAD;

	READ_BUF(16);
	lrp->lr_reclaim = be32_to_cpup(p++);
	lrp->lr_layout_type = be32_to_cpup(p++);
	lrp->lr_seg.iomode = be32_to_cpup(p++);
	lrp->lr_return_type = be32_to_cpup(p++);
	if (lrp->lr_return_type == RETURN_FILE) {
		READ_BUF(16);
		p = xdr_decode_hyper(p, &lrp->lr_seg.offset);
		p = xdr_decode_hyper(p, &lrp->lr_seg.length);

		status = nfsd4_decode_stateid(argp, &lrp->lr_sid);
		if (status)
			return status;

		READ_BUF(4);
		lrp->lrf_body_len = be32_to_cpup(p++);
		if (lrp->lrf_body_len > 0) {
			READ_BUF(lrp->lrf_body_len);
			READMEM(lrp->lrf_body, lrp->lrf_body_len);
		}
	} else {
		lrp->lr_seg.offset = 0;
		lrp->lr_seg.length = NFS4_MAX_UINT64;
	}

	DECODE_TAIL;
}
#endif /* CONFIG_NFSD_PNFS */

static __be32
nfsd4_decode_fallocate(struct nfsd4_compoundargs *argp,
		       struct nfsd4_fallocate *fallocate)
{
	DECODE_HEAD;

	status = nfsd4_decode_stateid(argp, &fallocate->falloc_stateid);
	if (status)
		return status;

	READ_BUF(16);
	p = xdr_decode_hyper(p, &fallocate->falloc_offset);
	xdr_decode_hyper(p, &fallocate->falloc_length);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_clone(struct nfsd4_compoundargs *argp, struct nfsd4_clone *clone)
{
	DECODE_HEAD;

	status = nfsd4_decode_stateid(argp, &clone->cl_src_stateid);
	if (status)
		return status;
	status = nfsd4_decode_stateid(argp, &clone->cl_dst_stateid);
	if (status)
		return status;

	READ_BUF(8 + 8 + 8);
	p = xdr_decode_hyper(p, &clone->cl_src_pos);
	p = xdr_decode_hyper(p, &clone->cl_dst_pos);
	p = xdr_decode_hyper(p, &clone->cl_count);
	DECODE_TAIL;
}

static __be32
nfsd4_decode_copy(struct nfsd4_compoundargs *argp, struct nfsd4_copy *copy)
{
	DECODE_HEAD;
	unsigned int tmp;

	status = nfsd4_decode_stateid(argp, &copy->cp_src_stateid);
	if (status)
		return status;
	status = nfsd4_decode_stateid(argp, &copy->cp_dst_stateid);
	if (status)
		return status;

	READ_BUF(8 + 8 + 8 + 4 + 4 + 4);
	p = xdr_decode_hyper(p, &copy->cp_src_pos);
	p = xdr_decode_hyper(p, &copy->cp_dst_pos);
	p = xdr_decode_hyper(p, &copy->cp_count);
	p++; /* ca_consecutive: we always do consecutive copies */
	copy->cp_synchronous = be32_to_cpup(p++);
	tmp = be32_to_cpup(p); /* Source server list not supported */

	DECODE_TAIL;
}

static __be32
nfsd4_decode_seek(struct nfsd4_compoundargs *argp, struct nfsd4_seek *seek)
{
	DECODE_HEAD;

	status = nfsd4_decode_stateid(argp, &seek->seek_stateid);
	if (status)
		return status;

	READ_BUF(8 + 4);
	p = xdr_decode_hyper(p, &seek->seek_offset);
	seek->seek_whence = be32_to_cpup(p);

	DECODE_TAIL;
}

static __be32
nfsd4_decode_noop(struct nfsd4_compoundargs *argp, void *p)
{
	return nfs_ok;
}

static __be32
nfsd4_decode_notsupp(struct nfsd4_compoundargs *argp, void *p)
{
	return nfserr_notsupp;
}

typedef __be32(*nfsd4_dec)(struct nfsd4_compoundargs *argp, void *);

static const nfsd4_dec nfsd4_dec_ops[] = {
	[OP_ACCESS]		= (nfsd4_dec)nfsd4_decode_access,
	[OP_CLOSE]		= (nfsd4_dec)nfsd4_decode_close,
	[OP_COMMIT]		= (nfsd4_dec)nfsd4_decode_commit,
	[OP_CREATE]		= (nfsd4_dec)nfsd4_decode_create,
	[OP_DELEGPURGE]		= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_DELEGRETURN]	= (nfsd4_dec)nfsd4_decode_delegreturn,
	[OP_GETATTR]		= (nfsd4_dec)nfsd4_decode_getattr,
	[OP_GETFH]		= (nfsd4_dec)nfsd4_decode_noop,
	[OP_LINK]		= (nfsd4_dec)nfsd4_decode_link,
	[OP_LOCK]		= (nfsd4_dec)nfsd4_decode_lock,
	[OP_LOCKT]		= (nfsd4_dec)nfsd4_decode_lockt,
	[OP_LOCKU]		= (nfsd4_dec)nfsd4_decode_locku,
	[OP_LOOKUP]		= (nfsd4_dec)nfsd4_decode_lookup,
	[OP_LOOKUPP]		= (nfsd4_dec)nfsd4_decode_noop,
	[OP_NVERIFY]		= (nfsd4_dec)nfsd4_decode_verify,
	[OP_OPEN]		= (nfsd4_dec)nfsd4_decode_open,
	[OP_OPENATTR]		= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_OPEN_CONFIRM]	= (nfsd4_dec)nfsd4_decode_open_confirm,
	[OP_OPEN_DOWNGRADE]	= (nfsd4_dec)nfsd4_decode_open_downgrade,
	[OP_PUTFH]		= (nfsd4_dec)nfsd4_decode_putfh,
	[OP_PUTPUBFH]		= (nfsd4_dec)nfsd4_decode_putpubfh,
	[OP_PUTROOTFH]		= (nfsd4_dec)nfsd4_decode_noop,
	[OP_READ]		= (nfsd4_dec)nfsd4_decode_read,
	[OP_READDIR]		= (nfsd4_dec)nfsd4_decode_readdir,
	[OP_READLINK]		= (nfsd4_dec)nfsd4_decode_noop,
	[OP_REMOVE]		= (nfsd4_dec)nfsd4_decode_remove,
	[OP_RENAME]		= (nfsd4_dec)nfsd4_decode_rename,
	[OP_RENEW]		= (nfsd4_dec)nfsd4_decode_renew,
	[OP_RESTOREFH]		= (nfsd4_dec)nfsd4_decode_noop,
	[OP_SAVEFH]		= (nfsd4_dec)nfsd4_decode_noop,
	[OP_SECINFO]		= (nfsd4_dec)nfsd4_decode_secinfo,
	[OP_SETATTR]		= (nfsd4_dec)nfsd4_decode_setattr,
	[OP_SETCLIENTID]	= (nfsd4_dec)nfsd4_decode_setclientid,
	[OP_SETCLIENTID_CONFIRM] = (nfsd4_dec)nfsd4_decode_setclientid_confirm,
	[OP_VERIFY]		= (nfsd4_dec)nfsd4_decode_verify,
	[OP_WRITE]		= (nfsd4_dec)nfsd4_decode_write,
	[OP_RELEASE_LOCKOWNER]	= (nfsd4_dec)nfsd4_decode_release_lockowner,

	/* new operations for NFSv4.1 */
	[OP_BACKCHANNEL_CTL]	= (nfsd4_dec)nfsd4_decode_backchannel_ctl,
	[OP_BIND_CONN_TO_SESSION]= (nfsd4_dec)nfsd4_decode_bind_conn_to_session,
	[OP_EXCHANGE_ID]	= (nfsd4_dec)nfsd4_decode_exchange_id,
	[OP_CREATE_SESSION]	= (nfsd4_dec)nfsd4_decode_create_session,
	[OP_DESTROY_SESSION]	= (nfsd4_dec)nfsd4_decode_destroy_session,
	[OP_FREE_STATEID]	= (nfsd4_dec)nfsd4_decode_free_stateid,
	[OP_GET_DIR_DELEGATION]	= (nfsd4_dec)nfsd4_decode_notsupp,
#ifdef CONFIG_NFSD_PNFS
	[OP_GETDEVICEINFO]	= (nfsd4_dec)nfsd4_decode_getdeviceinfo,
	[OP_GETDEVICELIST]	= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_LAYOUTCOMMIT]	= (nfsd4_dec)nfsd4_decode_layoutcommit,
	[OP_LAYOUTGET]		= (nfsd4_dec)nfsd4_decode_layoutget,
	[OP_LAYOUTRETURN]	= (nfsd4_dec)nfsd4_decode_layoutreturn,
#else
	[OP_GETDEVICEINFO]	= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_GETDEVICELIST]	= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_LAYOUTCOMMIT]	= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_LAYOUTGET]		= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_LAYOUTRETURN]	= (nfsd4_dec)nfsd4_decode_notsupp,
#endif
	[OP_SECINFO_NO_NAME]	= (nfsd4_dec)nfsd4_decode_secinfo_no_name,
	[OP_SEQUENCE]		= (nfsd4_dec)nfsd4_decode_sequence,
	[OP_SET_SSV]		= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_TEST_STATEID]	= (nfsd4_dec)nfsd4_decode_test_stateid,
	[OP_WANT_DELEGATION]	= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_DESTROY_CLIENTID]	= (nfsd4_dec)nfsd4_decode_destroy_clientid,
	[OP_RECLAIM_COMPLETE]	= (nfsd4_dec)nfsd4_decode_reclaim_complete,

	/* new operations for NFSv4.2 */
	[OP_ALLOCATE]		= (nfsd4_dec)nfsd4_decode_fallocate,
	[OP_COPY]		= (nfsd4_dec)nfsd4_decode_copy,
	[OP_COPY_NOTIFY]	= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_DEALLOCATE]		= (nfsd4_dec)nfsd4_decode_fallocate,
	[OP_IO_ADVISE]		= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_LAYOUTERROR]	= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_LAYOUTSTATS]	= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_OFFLOAD_CANCEL]	= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_OFFLOAD_STATUS]	= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_READ_PLUS]		= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_SEEK]		= (nfsd4_dec)nfsd4_decode_seek,
	[OP_WRITE_SAME]		= (nfsd4_dec)nfsd4_decode_notsupp,
	[OP_CLONE]		= (nfsd4_dec)nfsd4_decode_clone,
};

static inline bool
nfsd4_opnum_in_range(struct nfsd4_compoundargs *argp, struct nfsd4_op *op)
{
	if (op->opnum < FIRST_NFS4_OP)
		return false;
	else if (argp->minorversion == 0 && op->opnum > LAST_NFS40_OP)
		return false;
	else if (argp->minorversion == 1 && op->opnum > LAST_NFS41_OP)
		return false;
	else if (argp->minorversion == 2 && op->opnum > LAST_NFS42_OP)
		return false;
	return true;
}

static __be32
nfsd4_decode_compound(struct nfsd4_compoundargs *argp)
{
	DECODE_HEAD;
	struct nfsd4_op *op;
	bool cachethis = false;
	int auth_slack= argp->rqstp->rq_auth_slack;
	int max_reply = auth_slack + 8; /* opcnt, status */
	int readcount = 0;
	int readbytes = 0;
	int i;

	READ_BUF(4);
	argp->taglen = be32_to_cpup(p++);
	READ_BUF(argp->taglen);
	SAVEMEM(argp->tag, argp->taglen);
	READ_BUF(8);
	argp->minorversion = be32_to_cpup(p++);
	argp->opcnt = be32_to_cpup(p++);
	max_reply += 4 + (XDR_QUADLEN(argp->taglen) << 2);

	if (argp->taglen > NFSD4_MAX_TAGLEN)
		goto xdr_error;
	/*
	 * NFS4ERR_RESOURCE is a more helpful error than GARBAGE_ARGS
	 * here, so we return success at the xdr level so that
	 * nfsd4_proc can handle this is an NFS-level error.
	 */
	if (argp->opcnt > NFSD_MAX_OPS_PER_COMPOUND)
		return 0;

	if (argp->opcnt > ARRAY_SIZE(argp->iops)) {
		argp->ops = kzalloc(argp->opcnt * sizeof(*argp->ops), GFP_KERNEL);
		if (!argp->ops) {
			argp->ops = argp->iops;
			dprintk("nfsd: couldn't allocate room for COMPOUND\n");
			goto xdr_error;
		}
	}

	if (argp->minorversion > NFSD_SUPPORTED_MINOR_VERSION)
		argp->opcnt = 0;

	for (i = 0; i < argp->opcnt; i++) {
		op = &argp->ops[i];
		op->replay = NULL;

		READ_BUF(4);
		op->opnum = be32_to_cpup(p++);

		if (nfsd4_opnum_in_range(argp, op))
			op->status = nfsd4_dec_ops[op->opnum](argp, &op->u);
		else {
			op->opnum = OP_ILLEGAL;
			op->status = nfserr_op_illegal;
		}
		op->opdesc = OPDESC(op);
		/*
		 * We'll try to cache the result in the DRC if any one
		 * op in the compound wants to be cached:
		 */
		cachethis |= nfsd4_cache_this_op(op);

		if (op->opnum == OP_READ) {
			readcount++;
			readbytes += nfsd4_max_reply(argp->rqstp, op);
		} else
			max_reply += nfsd4_max_reply(argp->rqstp, op);
		/*
		 * OP_LOCK and OP_LOCKT may return a conflicting lock.
		 * (Special case because it will just skip encoding this
		 * if it runs out of xdr buffer space, and it is the only
		 * operation that behaves this way.)
		 */
		if (op->opnum == OP_LOCK || op->opnum == OP_LOCKT)
			max_reply += NFS4_OPAQUE_LIMIT;

		if (op->status) {
			argp->opcnt = i+1;
			break;
		}
	}
	/* Sessions make the DRC unnecessary: */
	if (argp->minorversion)
		cachethis = false;
	svc_reserve(argp->rqstp, max_reply + readbytes);
	argp->rqstp->rq_cachetype = cachethis ? RC_REPLBUFF : RC_NOCACHE;

	if (readcount > 1 || max_reply > PAGE_SIZE - auth_slack)
		clear_bit(RQ_SPLICE_OK, &argp->rqstp->rq_flags);

	DECODE_TAIL;
}

static __be32 *encode_change(__be32 *p, struct kstat *stat, struct inode *inode,
			     struct svc_export *exp)
{
	if (exp->ex_flags & NFSEXP_V4ROOT) {
		*p++ = cpu_to_be32(convert_to_wallclock(exp->cd->flush_time));
		*p++ = 0;
	} else if (IS_I_VERSION(inode)) {
		p = xdr_encode_hyper(p, nfsd4_change_attribute(stat, inode));
	} else {
		*p++ = cpu_to_be32(stat->ctime.tv_sec);
		*p++ = cpu_to_be32(stat->ctime.tv_nsec);
	}
	return p;
}

static __be32 *encode_cinfo(__be32 *p, struct nfsd4_change_info *c)
{
	*p++ = cpu_to_be32(c->atomic);
	if (c->change_supported) {
		p = xdr_encode_hyper(p, c->before_change);
		p = xdr_encode_hyper(p, c->after_change);
	} else {
		*p++ = cpu_to_be32(c->before_ctime_sec);
		*p++ = cpu_to_be32(c->before_ctime_nsec);
		*p++ = cpu_to_be32(c->after_ctime_sec);
		*p++ = cpu_to_be32(c->after_ctime_nsec);
	}
	return p;
}

/* Encode as an array of strings the string given with components
 * separated @sep, escaped with esc_enter and esc_exit.
 */
static __be32 nfsd4_encode_components_esc(struct xdr_stream *xdr, char sep,
					  char *components, char esc_enter,
					  char esc_exit)
{
	__be32 *p;
	__be32 pathlen;
	int pathlen_offset;
	int strlen, count=0;
	char *str, *end, *next;

	dprintk("nfsd4_encode_components(%s)\n", components);

	pathlen_offset = xdr->buf->len;
	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return nfserr_resource;
	p++; /* We will fill this in with @count later */

	end = str = components;
	while (*end) {
		bool found_esc = false;

		/* try to parse as esc_start, ..., esc_end, sep */
		if (*str == esc_enter) {
			for (; *end && (*end != esc_exit); end++)
				/* find esc_exit or end of string */;
			next = end + 1;
			if (*end && (!*next || *next == sep)) {
				str++;
				found_esc = true;
			}
		}

		if (!found_esc)
			for (; *end && (*end != sep); end++)
				/* find sep or end of string */;

		strlen = end - str;
		if (strlen) {
			p = xdr_reserve_space(xdr, strlen + 4);
			if (!p)
				return nfserr_resource;
			p = xdr_encode_opaque(p, str, strlen);
			count++;
		}
		else
			end++;
		if (found_esc)
			end = next;

		str = end;
	}
	pathlen = htonl(count);
	write_bytes_to_xdr_buf(xdr->buf, pathlen_offset, &pathlen, 4);
	return 0;
}

/* Encode as an array of strings the string given with components
 * separated @sep.
 */
static __be32 nfsd4_encode_components(struct xdr_stream *xdr, char sep,
				      char *components)
{
	return nfsd4_encode_components_esc(xdr, sep, components, 0, 0);
}

/*
 * encode a location element of a fs_locations structure
 */
static __be32 nfsd4_encode_fs_location4(struct xdr_stream *xdr,
					struct nfsd4_fs_location *location)
{
	__be32 status;

	status = nfsd4_encode_components_esc(xdr, ':', location->hosts,
						'[', ']');
	if (status)
		return status;
	status = nfsd4_encode_components(xdr, '/', location->path);
	if (status)
		return status;
	return 0;
}

/*
 * Encode a path in RFC3530 'pathname4' format
 */
static __be32 nfsd4_encode_path(struct xdr_stream *xdr,
				const struct path *root,
				const struct path *path)
{
	struct path cur = *path;
	__be32 *p;
	struct dentry **components = NULL;
	unsigned int ncomponents = 0;
	__be32 err = nfserr_jukebox;

	dprintk("nfsd4_encode_components(");

	path_get(&cur);
	/* First walk the path up to the nfsd root, and store the
	 * dentries/path components in an array.
	 */
	for (;;) {
		if (path_equal(&cur, root))
			break;
		if (cur.dentry == cur.mnt->mnt_root) {
			if (follow_up(&cur))
				continue;
			goto out_free;
		}
		if ((ncomponents & 15) == 0) {
			struct dentry **new;
			new = krealloc(components,
					sizeof(*new) * (ncomponents + 16),
					GFP_KERNEL);
			if (!new)
				goto out_free;
			components = new;
		}
		components[ncomponents++] = cur.dentry;
		cur.dentry = dget_parent(cur.dentry);
	}
	err = nfserr_resource;
	p = xdr_reserve_space(xdr, 4);
	if (!p)
		goto out_free;
	*p++ = cpu_to_be32(ncomponents);

	while (ncomponents) {
		struct dentry *dentry = components[ncomponents - 1];
		unsigned int len;

		spin_lock(&dentry->d_lock);
		len = dentry->d_name.len;
		p = xdr_reserve_space(xdr, len + 4);
		if (!p) {
			spin_unlock(&dentry->d_lock);
			goto out_free;
		}
		p = xdr_encode_opaque(p, dentry->d_name.name, len);
		dprintk("/%pd", dentry);
		spin_unlock(&dentry->d_lock);
		dput(dentry);
		ncomponents--;
	}

	err = 0;
out_free:
	dprintk(")\n");
	while (ncomponents)
		dput(components[--ncomponents]);
	kfree(components);
	path_put(&cur);
	return err;
}

static __be32 nfsd4_encode_fsloc_fsroot(struct xdr_stream *xdr,
			struct svc_rqst *rqstp, const struct path *path)
{
	struct svc_export *exp_ps;
	__be32 res;

	exp_ps = rqst_find_fsidzero_export(rqstp);
	if (IS_ERR(exp_ps))
		return nfserrno(PTR_ERR(exp_ps));
	res = nfsd4_encode_path(xdr, &exp_ps->ex_path, path);
	exp_put(exp_ps);
	return res;
}

/*
 *  encode a fs_locations structure
 */
static __be32 nfsd4_encode_fs_locations(struct xdr_stream *xdr,
			struct svc_rqst *rqstp, struct svc_export *exp)
{
	__be32 status;
	int i;
	__be32 *p;
	struct nfsd4_fs_locations *fslocs = &exp->ex_fslocs;

	status = nfsd4_encode_fsloc_fsroot(xdr, rqstp, &exp->ex_path);
	if (status)
		return status;
	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return nfserr_resource;
	*p++ = cpu_to_be32(fslocs->locations_count);
	for (i=0; i<fslocs->locations_count; i++) {
		status = nfsd4_encode_fs_location4(xdr, &fslocs->locations[i]);
		if (status)
			return status;
	}
	return 0;
}

static u32 nfs4_file_type(umode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFIFO:	return NF4FIFO;
	case S_IFCHR:	return NF4CHR;
	case S_IFDIR:	return NF4DIR;
	case S_IFBLK:	return NF4BLK;
	case S_IFLNK:	return NF4LNK;
	case S_IFREG:	return NF4REG;
	case S_IFSOCK:	return NF4SOCK;
	default:	return NF4BAD;
	};
}

static inline __be32
nfsd4_encode_aclname(struct xdr_stream *xdr, struct svc_rqst *rqstp,
		     struct nfs4_ace *ace)
{
	if (ace->whotype != NFS4_ACL_WHO_NAMED)
		return nfs4_acl_write_who(xdr, ace->whotype);
	else if (ace->flag & NFS4_ACE_IDENTIFIER_GROUP)
		return nfsd4_encode_group(xdr, rqstp, ace->who_gid);
	else
		return nfsd4_encode_user(xdr, rqstp, ace->who_uid);
}

static inline __be32
nfsd4_encode_layout_types(struct xdr_stream *xdr, u32 layout_types)
{
	__be32		*p;
	unsigned long	i = hweight_long(layout_types);

	p = xdr_reserve_space(xdr, 4 + 4 * i);
	if (!p)
		return nfserr_resource;

	*p++ = cpu_to_be32(i);

	for (i = LAYOUT_NFSV4_1_FILES; i < LAYOUT_TYPE_MAX; ++i)
		if (layout_types & (1 << i))
			*p++ = cpu_to_be32(i);

	return 0;
}

#define WORD0_ABSENT_FS_ATTRS (FATTR4_WORD0_FS_LOCATIONS | FATTR4_WORD0_FSID | \
			      FATTR4_WORD0_RDATTR_ERROR)
#define WORD1_ABSENT_FS_ATTRS FATTR4_WORD1_MOUNTED_ON_FILEID
#define WORD2_ABSENT_FS_ATTRS 0

#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
static inline __be32
nfsd4_encode_security_label(struct xdr_stream *xdr, struct svc_rqst *rqstp,
			    void *context, int len)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, len + 4 + 4 + 4);
	if (!p)
		return nfserr_resource;

	/*
	 * For now we use a 0 here to indicate the null translation; in
	 * the future we may place a call to translation code here.
	 */
	*p++ = cpu_to_be32(0); /* lfs */
	*p++ = cpu_to_be32(0); /* pi */
	p = xdr_encode_opaque(p, context, len);
	return 0;
}
#else
static inline __be32
nfsd4_encode_security_label(struct xdr_stream *xdr, struct svc_rqst *rqstp,
			    void *context, int len)
{ return 0; }
#endif

static __be32 fattr_handle_absent_fs(u32 *bmval0, u32 *bmval1, u32 *bmval2, u32 *rdattr_err)
{
	/* As per referral draft:  */
	if (*bmval0 & ~WORD0_ABSENT_FS_ATTRS ||
	    *bmval1 & ~WORD1_ABSENT_FS_ATTRS) {
		if (*bmval0 & FATTR4_WORD0_RDATTR_ERROR ||
	            *bmval0 & FATTR4_WORD0_FS_LOCATIONS)
			*rdattr_err = NFSERR_MOVED;
		else
			return nfserr_moved;
	}
	*bmval0 &= WORD0_ABSENT_FS_ATTRS;
	*bmval1 &= WORD1_ABSENT_FS_ATTRS;
	*bmval2 &= WORD2_ABSENT_FS_ATTRS;
	return 0;
}


static int get_parent_attributes(struct svc_export *exp, struct kstat *stat)
{
	struct path path = exp->ex_path;
	int err;

	path_get(&path);
	while (follow_up(&path)) {
		if (path.dentry != path.mnt->mnt_root)
			break;
	}
	err = vfs_getattr(&path, stat, STATX_BASIC_STATS, AT_STATX_SYNC_AS_STAT);
	path_put(&path);
	return err;
}

static __be32
nfsd4_encode_bitmap(struct xdr_stream *xdr, u32 bmval0, u32 bmval1, u32 bmval2)
{
	__be32 *p;

	if (bmval2) {
		p = xdr_reserve_space(xdr, 16);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(3);
		*p++ = cpu_to_be32(bmval0);
		*p++ = cpu_to_be32(bmval1);
		*p++ = cpu_to_be32(bmval2);
	} else if (bmval1) {
		p = xdr_reserve_space(xdr, 12);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(2);
		*p++ = cpu_to_be32(bmval0);
		*p++ = cpu_to_be32(bmval1);
	} else {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(1);
		*p++ = cpu_to_be32(bmval0);
	}

	return 0;
out_resource:
	return nfserr_resource;
}

/*
 * Note: @fhp can be NULL; in this case, we might have to compose the filehandle
 * ourselves.
 */
static __be32
nfsd4_encode_fattr(struct xdr_stream *xdr, struct svc_fh *fhp,
		struct svc_export *exp,
		struct dentry *dentry, u32 *bmval,
		struct svc_rqst *rqstp, int ignore_crossmnt)
{
	u32 bmval0 = bmval[0];
	u32 bmval1 = bmval[1];
	u32 bmval2 = bmval[2];
	struct kstat stat;
	struct svc_fh *tempfh = NULL;
	struct kstatfs statfs;
	__be32 *p;
	int starting_len = xdr->buf->len;
	int attrlen_offset;
	__be32 attrlen;
	u32 dummy;
	u64 dummy64;
	u32 rdattr_err = 0;
	__be32 status;
	int err;
	struct nfs4_acl *acl = NULL;
	void *context = NULL;
	int contextlen;
	bool contextsupport = false;
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	u32 minorversion = resp->cstate.minorversion;
	struct path path = {
		.mnt	= exp->ex_path.mnt,
		.dentry	= dentry,
	};
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	BUG_ON(bmval1 & NFSD_WRITEONLY_ATTRS_WORD1);
	BUG_ON(!nfsd_attrs_supported(minorversion, bmval));

	if (exp->ex_fslocs.migrated) {
		status = fattr_handle_absent_fs(&bmval0, &bmval1, &bmval2, &rdattr_err);
		if (status)
			goto out;
	}

	err = vfs_getattr(&path, &stat, STATX_BASIC_STATS, AT_STATX_SYNC_AS_STAT);
	if (err)
		goto out_nfserr;
	if ((bmval0 & (FATTR4_WORD0_FILES_AVAIL | FATTR4_WORD0_FILES_FREE |
			FATTR4_WORD0_FILES_TOTAL | FATTR4_WORD0_MAXNAME)) ||
	    (bmval1 & (FATTR4_WORD1_SPACE_AVAIL | FATTR4_WORD1_SPACE_FREE |
		       FATTR4_WORD1_SPACE_TOTAL))) {
		err = vfs_statfs(&path, &statfs);
		if (err)
			goto out_nfserr;
	}
	if ((bmval0 & (FATTR4_WORD0_FILEHANDLE | FATTR4_WORD0_FSID)) && !fhp) {
		tempfh = kmalloc(sizeof(struct svc_fh), GFP_KERNEL);
		status = nfserr_jukebox;
		if (!tempfh)
			goto out;
		fh_init(tempfh, NFS4_FHSIZE);
		status = fh_compose(tempfh, exp, dentry, NULL);
		if (status)
			goto out;
		fhp = tempfh;
	}
	if (bmval0 & FATTR4_WORD0_ACL) {
		err = nfsd4_get_nfs4_acl(rqstp, dentry, &acl);
		if (err == -EOPNOTSUPP)
			bmval0 &= ~FATTR4_WORD0_ACL;
		else if (err == -EINVAL) {
			status = nfserr_attrnotsupp;
			goto out;
		} else if (err != 0)
			goto out_nfserr;
	}

#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
	if ((bmval2 & FATTR4_WORD2_SECURITY_LABEL) ||
	     bmval0 & FATTR4_WORD0_SUPPORTED_ATTRS) {
		if (exp->ex_flags & NFSEXP_SECURITY_LABEL)
			err = security_inode_getsecctx(d_inode(dentry),
						&context, &contextlen);
		else
			err = -EOPNOTSUPP;
		contextsupport = (err == 0);
		if (bmval2 & FATTR4_WORD2_SECURITY_LABEL) {
			if (err == -EOPNOTSUPP)
				bmval2 &= ~FATTR4_WORD2_SECURITY_LABEL;
			else if (err)
				goto out_nfserr;
		}
	}
#endif /* CONFIG_NFSD_V4_SECURITY_LABEL */

	status = nfsd4_encode_bitmap(xdr, bmval0, bmval1, bmval2);
	if (status)
		goto out;

	attrlen_offset = xdr->buf->len;
	p = xdr_reserve_space(xdr, 4);
	if (!p)
		goto out_resource;
	p++;                /* to be backfilled later */

	if (bmval0 & FATTR4_WORD0_SUPPORTED_ATTRS) {
		u32 supp[3];

		memcpy(supp, nfsd_suppattrs[minorversion], sizeof(supp));

		if (!IS_POSIXACL(dentry->d_inode))
			supp[0] &= ~FATTR4_WORD0_ACL;
		if (!contextsupport)
			supp[2] &= ~FATTR4_WORD2_SECURITY_LABEL;
		if (!supp[2]) {
			p = xdr_reserve_space(xdr, 12);
			if (!p)
				goto out_resource;
			*p++ = cpu_to_be32(2);
			*p++ = cpu_to_be32(supp[0]);
			*p++ = cpu_to_be32(supp[1]);
		} else {
			p = xdr_reserve_space(xdr, 16);
			if (!p)
				goto out_resource;
			*p++ = cpu_to_be32(3);
			*p++ = cpu_to_be32(supp[0]);
			*p++ = cpu_to_be32(supp[1]);
			*p++ = cpu_to_be32(supp[2]);
		}
	}
	if (bmval0 & FATTR4_WORD0_TYPE) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		dummy = nfs4_file_type(stat.mode);
		if (dummy == NF4BAD) {
			status = nfserr_serverfault;
			goto out;
		}
		*p++ = cpu_to_be32(dummy);
	}
	if (bmval0 & FATTR4_WORD0_FH_EXPIRE_TYPE) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		if (exp->ex_flags & NFSEXP_NOSUBTREECHECK)
			*p++ = cpu_to_be32(NFS4_FH_PERSISTENT);
		else
			*p++ = cpu_to_be32(NFS4_FH_PERSISTENT|
						NFS4_FH_VOL_RENAME);
	}
	if (bmval0 & FATTR4_WORD0_CHANGE) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		p = encode_change(p, &stat, d_inode(dentry), exp);
	}
	if (bmval0 & FATTR4_WORD0_SIZE) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		p = xdr_encode_hyper(p, stat.size);
	}
	if (bmval0 & FATTR4_WORD0_LINK_SUPPORT) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(1);
	}
	if (bmval0 & FATTR4_WORD0_SYMLINK_SUPPORT) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(1);
	}
	if (bmval0 & FATTR4_WORD0_NAMED_ATTR) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(0);
	}
	if (bmval0 & FATTR4_WORD0_FSID) {
		p = xdr_reserve_space(xdr, 16);
		if (!p)
			goto out_resource;
		if (exp->ex_fslocs.migrated) {
			p = xdr_encode_hyper(p, NFS4_REFERRAL_FSID_MAJOR);
			p = xdr_encode_hyper(p, NFS4_REFERRAL_FSID_MINOR);
		} else switch(fsid_source(fhp)) {
		case FSIDSOURCE_FSID:
			p = xdr_encode_hyper(p, (u64)exp->ex_fsid);
			p = xdr_encode_hyper(p, (u64)0);
			break;
		case FSIDSOURCE_DEV:
			*p++ = cpu_to_be32(0);
			*p++ = cpu_to_be32(MAJOR(stat.dev));
			*p++ = cpu_to_be32(0);
			*p++ = cpu_to_be32(MINOR(stat.dev));
			break;
		case FSIDSOURCE_UUID:
			p = xdr_encode_opaque_fixed(p, exp->ex_uuid,
								EX_UUID_LEN);
			break;
		}
	}
	if (bmval0 & FATTR4_WORD0_UNIQUE_HANDLES) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(0);
	}
	if (bmval0 & FATTR4_WORD0_LEASE_TIME) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(nn->nfsd4_lease);
	}
	if (bmval0 & FATTR4_WORD0_RDATTR_ERROR) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(rdattr_err);
	}
	if (bmval0 & FATTR4_WORD0_ACL) {
		struct nfs4_ace *ace;

		if (acl == NULL) {
			p = xdr_reserve_space(xdr, 4);
			if (!p)
				goto out_resource;

			*p++ = cpu_to_be32(0);
			goto out_acl;
		}
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(acl->naces);

		for (ace = acl->aces; ace < acl->aces + acl->naces; ace++) {
			p = xdr_reserve_space(xdr, 4*3);
			if (!p)
				goto out_resource;
			*p++ = cpu_to_be32(ace->type);
			*p++ = cpu_to_be32(ace->flag);
			*p++ = cpu_to_be32(ace->access_mask &
							NFS4_ACE_MASK_ALL);
			status = nfsd4_encode_aclname(xdr, rqstp, ace);
			if (status)
				goto out;
		}
	}
out_acl:
	if (bmval0 & FATTR4_WORD0_ACLSUPPORT) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(IS_POSIXACL(dentry->d_inode) ?
			ACL4_SUPPORT_ALLOW_ACL|ACL4_SUPPORT_DENY_ACL : 0);
	}
	if (bmval0 & FATTR4_WORD0_CANSETTIME) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(1);
	}
	if (bmval0 & FATTR4_WORD0_CASE_INSENSITIVE) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(0);
	}
	if (bmval0 & FATTR4_WORD0_CASE_PRESERVING) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(1);
	}
	if (bmval0 & FATTR4_WORD0_CHOWN_RESTRICTED) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(1);
	}
	if (bmval0 & FATTR4_WORD0_FILEHANDLE) {
		p = xdr_reserve_space(xdr, fhp->fh_handle.fh_size + 4);
		if (!p)
			goto out_resource;
		p = xdr_encode_opaque(p, &fhp->fh_handle.fh_base,
					fhp->fh_handle.fh_size);
	}
	if (bmval0 & FATTR4_WORD0_FILEID) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		p = xdr_encode_hyper(p, stat.ino);
	}
	if (bmval0 & FATTR4_WORD0_FILES_AVAIL) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		p = xdr_encode_hyper(p, (u64) statfs.f_ffree);
	}
	if (bmval0 & FATTR4_WORD0_FILES_FREE) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		p = xdr_encode_hyper(p, (u64) statfs.f_ffree);
	}
	if (bmval0 & FATTR4_WORD0_FILES_TOTAL) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		p = xdr_encode_hyper(p, (u64) statfs.f_files);
	}
	if (bmval0 & FATTR4_WORD0_FS_LOCATIONS) {
		status = nfsd4_encode_fs_locations(xdr, rqstp, exp);
		if (status)
			goto out;
	}
	if (bmval0 & FATTR4_WORD0_HOMOGENEOUS) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(1);
	}
	if (bmval0 & FATTR4_WORD0_MAXFILESIZE) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		p = xdr_encode_hyper(p, exp->ex_path.mnt->mnt_sb->s_maxbytes);
	}
	if (bmval0 & FATTR4_WORD0_MAXLINK) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(255);
	}
	if (bmval0 & FATTR4_WORD0_MAXNAME) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(statfs.f_namelen);
	}
	if (bmval0 & FATTR4_WORD0_MAXREAD) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		p = xdr_encode_hyper(p, (u64) svc_max_payload(rqstp));
	}
	if (bmval0 & FATTR4_WORD0_MAXWRITE) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		p = xdr_encode_hyper(p, (u64) svc_max_payload(rqstp));
	}
	if (bmval1 & FATTR4_WORD1_MODE) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(stat.mode & S_IALLUGO);
	}
	if (bmval1 & FATTR4_WORD1_NO_TRUNC) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(1);
	}
	if (bmval1 & FATTR4_WORD1_NUMLINKS) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(stat.nlink);
	}
	if (bmval1 & FATTR4_WORD1_OWNER) {
		status = nfsd4_encode_user(xdr, rqstp, stat.uid);
		if (status)
			goto out;
	}
	if (bmval1 & FATTR4_WORD1_OWNER_GROUP) {
		status = nfsd4_encode_group(xdr, rqstp, stat.gid);
		if (status)
			goto out;
	}
	if (bmval1 & FATTR4_WORD1_RAWDEV) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32((u32) MAJOR(stat.rdev));
		*p++ = cpu_to_be32((u32) MINOR(stat.rdev));
	}
	if (bmval1 & FATTR4_WORD1_SPACE_AVAIL) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		dummy64 = (u64)statfs.f_bavail * (u64)statfs.f_bsize;
		p = xdr_encode_hyper(p, dummy64);
	}
	if (bmval1 & FATTR4_WORD1_SPACE_FREE) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		dummy64 = (u64)statfs.f_bfree * (u64)statfs.f_bsize;
		p = xdr_encode_hyper(p, dummy64);
	}
	if (bmval1 & FATTR4_WORD1_SPACE_TOTAL) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		dummy64 = (u64)statfs.f_blocks * (u64)statfs.f_bsize;
		p = xdr_encode_hyper(p, dummy64);
	}
	if (bmval1 & FATTR4_WORD1_SPACE_USED) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			goto out_resource;
		dummy64 = (u64)stat.blocks << 9;
		p = xdr_encode_hyper(p, dummy64);
	}
	if (bmval1 & FATTR4_WORD1_TIME_ACCESS) {
		p = xdr_reserve_space(xdr, 12);
		if (!p)
			goto out_resource;
		p = xdr_encode_hyper(p, (s64)stat.atime.tv_sec);
		*p++ = cpu_to_be32(stat.atime.tv_nsec);
	}
	if (bmval1 & FATTR4_WORD1_TIME_DELTA) {
		p = xdr_reserve_space(xdr, 12);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(0);
		*p++ = cpu_to_be32(1);
		*p++ = cpu_to_be32(0);
	}
	if (bmval1 & FATTR4_WORD1_TIME_METADATA) {
		p = xdr_reserve_space(xdr, 12);
		if (!p)
			goto out_resource;
		p = xdr_encode_hyper(p, (s64)stat.ctime.tv_sec);
		*p++ = cpu_to_be32(stat.ctime.tv_nsec);
	}
	if (bmval1 & FATTR4_WORD1_TIME_MODIFY) {
		p = xdr_reserve_space(xdr, 12);
		if (!p)
			goto out_resource;
		p = xdr_encode_hyper(p, (s64)stat.mtime.tv_sec);
		*p++ = cpu_to_be32(stat.mtime.tv_nsec);
	}
	if (bmval1 & FATTR4_WORD1_MOUNTED_ON_FILEID) {
		struct kstat parent_stat;
		u64 ino = stat.ino;

		p = xdr_reserve_space(xdr, 8);
		if (!p)
                	goto out_resource;
		/*
		 * Get parent's attributes if not ignoring crossmount
		 * and this is the root of a cross-mounted filesystem.
		 */
		if (ignore_crossmnt == 0 &&
		    dentry == exp->ex_path.mnt->mnt_root) {
			err = get_parent_attributes(exp, &parent_stat);
			if (err)
				goto out_nfserr;
			ino = parent_stat.ino;
		}
		p = xdr_encode_hyper(p, ino);
	}
#ifdef CONFIG_NFSD_PNFS
	if (bmval1 & FATTR4_WORD1_FS_LAYOUT_TYPES) {
		status = nfsd4_encode_layout_types(xdr, exp->ex_layout_types);
		if (status)
			goto out;
	}

	if (bmval2 & FATTR4_WORD2_LAYOUT_TYPES) {
		status = nfsd4_encode_layout_types(xdr, exp->ex_layout_types);
		if (status)
			goto out;
	}

	if (bmval2 & FATTR4_WORD2_LAYOUT_BLKSIZE) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(stat.blksize);
	}
#endif /* CONFIG_NFSD_PNFS */
	if (bmval2 & FATTR4_WORD2_SUPPATTR_EXCLCREAT) {
		u32 supp[3];

		memcpy(supp, nfsd_suppattrs[minorversion], sizeof(supp));
		supp[0] &= NFSD_SUPPATTR_EXCLCREAT_WORD0;
		supp[1] &= NFSD_SUPPATTR_EXCLCREAT_WORD1;
		supp[2] &= NFSD_SUPPATTR_EXCLCREAT_WORD2;

		status = nfsd4_encode_bitmap(xdr, supp[0], supp[1], supp[2]);
		if (status)
			goto out;
	}

	if (bmval2 & FATTR4_WORD2_SECURITY_LABEL) {
		status = nfsd4_encode_security_label(xdr, rqstp, context,
								contextlen);
		if (status)
			goto out;
	}

	attrlen = htonl(xdr->buf->len - attrlen_offset - 4);
	write_bytes_to_xdr_buf(xdr->buf, attrlen_offset, &attrlen, 4);
	status = nfs_ok;

out:
#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
	if (context)
		security_release_secctx(context, contextlen);
#endif /* CONFIG_NFSD_V4_SECURITY_LABEL */
	kfree(acl);
	if (tempfh) {
		fh_put(tempfh);
		kfree(tempfh);
	}
	if (status)
		xdr_truncate_encode(xdr, starting_len);
	return status;
out_nfserr:
	status = nfserrno(err);
	goto out;
out_resource:
	status = nfserr_resource;
	goto out;
}

static void svcxdr_init_encode_from_buffer(struct xdr_stream *xdr,
				struct xdr_buf *buf, __be32 *p, int bytes)
{
	xdr->scratch.iov_len = 0;
	memset(buf, 0, sizeof(struct xdr_buf));
	buf->head[0].iov_base = p;
	buf->head[0].iov_len = 0;
	buf->len = 0;
	xdr->buf = buf;
	xdr->iov = buf->head;
	xdr->p = p;
	xdr->end = (void *)p + bytes;
	buf->buflen = bytes;
}

__be32 nfsd4_encode_fattr_to_buf(__be32 **p, int words,
			struct svc_fh *fhp, struct svc_export *exp,
			struct dentry *dentry, u32 *bmval,
			struct svc_rqst *rqstp, int ignore_crossmnt)
{
	struct xdr_buf dummy;
	struct xdr_stream xdr;
	__be32 ret;

	svcxdr_init_encode_from_buffer(&xdr, &dummy, *p, words << 2);
	ret = nfsd4_encode_fattr(&xdr, fhp, exp, dentry, bmval, rqstp,
							ignore_crossmnt);
	*p = xdr.p;
	return ret;
}

static inline int attributes_need_mount(u32 *bmval)
{
	if (bmval[0] & ~(FATTR4_WORD0_RDATTR_ERROR | FATTR4_WORD0_LEASE_TIME))
		return 1;
	if (bmval[1] & ~FATTR4_WORD1_MOUNTED_ON_FILEID)
		return 1;
	return 0;
}

static __be32
nfsd4_encode_dirent_fattr(struct xdr_stream *xdr, struct nfsd4_readdir *cd,
			const char *name, int namlen)
{
	struct svc_export *exp = cd->rd_fhp->fh_export;
	struct dentry *dentry;
	__be32 nfserr;
	int ignore_crossmnt = 0;

	dentry = lookup_one_len_unlocked(name, cd->rd_fhp->fh_dentry, namlen);
	if (IS_ERR(dentry))
		return nfserrno(PTR_ERR(dentry));
	if (d_really_is_negative(dentry)) {
		/*
		 * we're not holding the i_mutex here, so there's
		 * a window where this directory entry could have gone
		 * away.
		 */
		dput(dentry);
		return nfserr_noent;
	}

	exp_get(exp);
	/*
	 * In the case of a mountpoint, the client may be asking for
	 * attributes that are only properties of the underlying filesystem
	 * as opposed to the cross-mounted file system. In such a case,
	 * we will not follow the cross mount and will fill the attribtutes
	 * directly from the mountpoint dentry.
	 */
	if (nfsd_mountpoint(dentry, exp)) {
		int err;

		if (!(exp->ex_flags & NFSEXP_V4ROOT)
				&& !attributes_need_mount(cd->rd_bmval)) {
			ignore_crossmnt = 1;
			goto out_encode;
		}
		/*
		 * Why the heck aren't we just using nfsd_lookup??
		 * Different "."/".." handling?  Something else?
		 * At least, add a comment here to explain....
		 */
		err = nfsd_cross_mnt(cd->rd_rqstp, &dentry, &exp);
		if (err) {
			nfserr = nfserrno(err);
			goto out_put;
		}
		nfserr = check_nfsd_access(exp, cd->rd_rqstp);
		if (nfserr)
			goto out_put;

	}
out_encode:
	nfserr = nfsd4_encode_fattr(xdr, NULL, exp, dentry, cd->rd_bmval,
					cd->rd_rqstp, ignore_crossmnt);
out_put:
	dput(dentry);
	exp_put(exp);
	return nfserr;
}

static __be32 *
nfsd4_encode_rdattr_error(struct xdr_stream *xdr, __be32 nfserr)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 20);
	if (!p)
		return NULL;
	*p++ = htonl(2);
	*p++ = htonl(FATTR4_WORD0_RDATTR_ERROR); /* bmval0 */
	*p++ = htonl(0);			 /* bmval1 */

	*p++ = htonl(4);     /* attribute length */
	*p++ = nfserr;       /* no htonl */
	return p;
}

static int
nfsd4_encode_dirent(void *ccdv, const char *name, int namlen,
		    loff_t offset, u64 ino, unsigned int d_type)
{
	struct readdir_cd *ccd = ccdv;
	struct nfsd4_readdir *cd = container_of(ccd, struct nfsd4_readdir, common);
	struct xdr_stream *xdr = cd->xdr;
	int start_offset = xdr->buf->len;
	int cookie_offset;
	u32 name_and_cookie;
	int entry_bytes;
	__be32 nfserr = nfserr_toosmall;
	__be64 wire_offset;
	__be32 *p;

	/* In nfsv4, "." and ".." never make it onto the wire.. */
	if (name && isdotent(name, namlen)) {
		cd->common.err = nfs_ok;
		return 0;
	}

	if (cd->cookie_offset) {
		wire_offset = cpu_to_be64(offset);
		write_bytes_to_xdr_buf(xdr->buf, cd->cookie_offset,
							&wire_offset, 8);
	}

	p = xdr_reserve_space(xdr, 4);
	if (!p)
		goto fail;
	*p++ = xdr_one;                             /* mark entry present */
	cookie_offset = xdr->buf->len;
	p = xdr_reserve_space(xdr, 3*4 + namlen);
	if (!p)
		goto fail;
	p = xdr_encode_hyper(p, NFS_OFFSET_MAX);    /* offset of next entry */
	p = xdr_encode_array(p, name, namlen);      /* name length & name */

	nfserr = nfsd4_encode_dirent_fattr(xdr, cd, name, namlen);
	switch (nfserr) {
	case nfs_ok:
		break;
	case nfserr_resource:
		nfserr = nfserr_toosmall;
		goto fail;
	case nfserr_noent:
		xdr_truncate_encode(xdr, start_offset);
		goto skip_entry;
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
		p = nfsd4_encode_rdattr_error(xdr, nfserr);
		if (p == NULL) {
			nfserr = nfserr_toosmall;
			goto fail;
		}
	}
	nfserr = nfserr_toosmall;
	entry_bytes = xdr->buf->len - start_offset;
	if (entry_bytes > cd->rd_maxcount)
		goto fail;
	cd->rd_maxcount -= entry_bytes;
	/*
	 * RFC 3530 14.2.24 describes rd_dircount as only a "hint", so
	 * let's always let through the first entry, at least:
	 */
	if (!cd->rd_dircount)
		goto fail;
	name_and_cookie = 4 + 4 * XDR_QUADLEN(namlen) + 8;
	if (name_and_cookie > cd->rd_dircount && cd->cookie_offset)
		goto fail;
	cd->rd_dircount -= min(cd->rd_dircount, name_and_cookie);

	cd->cookie_offset = cookie_offset;
skip_entry:
	cd->common.err = nfs_ok;
	return 0;
fail:
	xdr_truncate_encode(xdr, start_offset);
	cd->common.err = nfserr;
	return -EINVAL;
}

static __be32
nfsd4_encode_stateid(struct xdr_stream *xdr, stateid_t *sid)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, sizeof(stateid_t));
	if (!p)
		return nfserr_resource;
	*p++ = cpu_to_be32(sid->si_generation);
	p = xdr_encode_opaque_fixed(p, &sid->si_opaque,
					sizeof(stateid_opaque_t));
	return 0;
}

static __be32
nfsd4_encode_access(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_access *access)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, 8);
	if (!p)
		return nfserr_resource;
	*p++ = cpu_to_be32(access->ac_supported);
	*p++ = cpu_to_be32(access->ac_resp_access);
	return 0;
}

static __be32 nfsd4_encode_bind_conn_to_session(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_bind_conn_to_session *bcts)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS4_MAX_SESSIONID_LEN + 8);
	if (!p)
		return nfserr_resource;
	p = xdr_encode_opaque_fixed(p, bcts->sessionid.data,
					NFS4_MAX_SESSIONID_LEN);
	*p++ = cpu_to_be32(bcts->dir);
	/* Upshifting from TCP to RDMA is not supported */
	*p++ = cpu_to_be32(0);
	return 0;
}

static __be32
nfsd4_encode_close(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_close *close)
{
	struct xdr_stream *xdr = &resp->xdr;

	return nfsd4_encode_stateid(xdr, &close->cl_stateid);
}


static __be32
nfsd4_encode_commit(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_commit *commit)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS4_VERIFIER_SIZE);
	if (!p)
		return nfserr_resource;
	p = xdr_encode_opaque_fixed(p, commit->co_verf.data,
						NFS4_VERIFIER_SIZE);
	return 0;
}

static __be32
nfsd4_encode_create(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_create *create)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, 20);
	if (!p)
		return nfserr_resource;
	encode_cinfo(p, &create->cr_cinfo);
	nfserr = nfsd4_encode_bitmap(xdr, create->cr_bmval[0],
			create->cr_bmval[1], create->cr_bmval[2]);
	return 0;
}

static __be32
nfsd4_encode_getattr(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_getattr *getattr)
{
	struct svc_fh *fhp = getattr->ga_fhp;
	struct xdr_stream *xdr = &resp->xdr;

	return nfsd4_encode_fattr(xdr, fhp, fhp->fh_export, fhp->fh_dentry,
				    getattr->ga_bmval, resp->rqstp, 0);
}

static __be32
nfsd4_encode_getfh(struct nfsd4_compoundres *resp, __be32 nfserr, struct svc_fh **fhpp)
{
	struct xdr_stream *xdr = &resp->xdr;
	struct svc_fh *fhp = *fhpp;
	unsigned int len;
	__be32 *p;

	len = fhp->fh_handle.fh_size;
	p = xdr_reserve_space(xdr, len + 4);
	if (!p)
		return nfserr_resource;
	p = xdr_encode_opaque(p, &fhp->fh_handle.fh_base, len);
	return 0;
}

/*
* Including all fields other than the name, a LOCK4denied structure requires
*   8(clientid) + 4(namelen) + 8(offset) + 8(length) + 4(type) = 32 bytes.
*/
static __be32
nfsd4_encode_lock_denied(struct xdr_stream *xdr, struct nfsd4_lock_denied *ld)
{
	struct xdr_netobj *conf = &ld->ld_owner;
	__be32 *p;

again:
	p = xdr_reserve_space(xdr, 32 + XDR_LEN(conf->len));
	if (!p) {
		/*
		 * Don't fail to return the result just because we can't
		 * return the conflicting open:
		 */
		if (conf->len) {
			kfree(conf->data);
			conf->len = 0;
			conf->data = NULL;
			goto again;
		}
		return nfserr_resource;
	}
	p = xdr_encode_hyper(p, ld->ld_start);
	p = xdr_encode_hyper(p, ld->ld_length);
	*p++ = cpu_to_be32(ld->ld_type);
	if (conf->len) {
		p = xdr_encode_opaque_fixed(p, &ld->ld_clientid, 8);
		p = xdr_encode_opaque(p, conf->data, conf->len);
		kfree(conf->data);
	}  else {  /* non - nfsv4 lock in conflict, no clientid nor owner */
		p = xdr_encode_hyper(p, (u64)0); /* clientid */
		*p++ = cpu_to_be32(0); /* length of owner name */
	}
	return nfserr_denied;
}

static __be32
nfsd4_encode_lock(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_lock *lock)
{
	struct xdr_stream *xdr = &resp->xdr;

	if (!nfserr)
		nfserr = nfsd4_encode_stateid(xdr, &lock->lk_resp_stateid);
	else if (nfserr == nfserr_denied)
		nfserr = nfsd4_encode_lock_denied(xdr, &lock->lk_denied);

	return nfserr;
}

static __be32
nfsd4_encode_lockt(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_lockt *lockt)
{
	struct xdr_stream *xdr = &resp->xdr;

	if (nfserr == nfserr_denied)
		nfsd4_encode_lock_denied(xdr, &lockt->lt_denied);
	return nfserr;
}

static __be32
nfsd4_encode_locku(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_locku *locku)
{
	struct xdr_stream *xdr = &resp->xdr;

	return nfsd4_encode_stateid(xdr, &locku->lu_stateid);
}


static __be32
nfsd4_encode_link(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_link *link)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, 20);
	if (!p)
		return nfserr_resource;
	p = encode_cinfo(p, &link->li_cinfo);
	return 0;
}


static __be32
nfsd4_encode_open(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_open *open)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	nfserr = nfsd4_encode_stateid(xdr, &open->op_stateid);
	if (nfserr)
		return nfserr;
	p = xdr_reserve_space(xdr, 24);
	if (!p)
		return nfserr_resource;
	p = encode_cinfo(p, &open->op_cinfo);
	*p++ = cpu_to_be32(open->op_rflags);

	nfserr = nfsd4_encode_bitmap(xdr, open->op_bmval[0], open->op_bmval[1],
					open->op_bmval[2]);
	if (nfserr)
		return nfserr;

	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return nfserr_resource;

	*p++ = cpu_to_be32(open->op_delegate_type);
	switch (open->op_delegate_type) {
	case NFS4_OPEN_DELEGATE_NONE:
		break;
	case NFS4_OPEN_DELEGATE_READ:
		nfserr = nfsd4_encode_stateid(xdr, &open->op_delegate_stateid);
		if (nfserr)
			return nfserr;
		p = xdr_reserve_space(xdr, 20);
		if (!p)
			return nfserr_resource;
		*p++ = cpu_to_be32(open->op_recall);

		/*
		 * TODO: ACE's in delegations
		 */
		*p++ = cpu_to_be32(NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE);
		*p++ = cpu_to_be32(0);
		*p++ = cpu_to_be32(0);
		*p++ = cpu_to_be32(0);   /* XXX: is NULL principal ok? */
		break;
	case NFS4_OPEN_DELEGATE_WRITE:
		nfserr = nfsd4_encode_stateid(xdr, &open->op_delegate_stateid);
		if (nfserr)
			return nfserr;
		p = xdr_reserve_space(xdr, 32);
		if (!p)
			return nfserr_resource;
		*p++ = cpu_to_be32(0);

		/*
		 * TODO: space_limit's in delegations
		 */
		*p++ = cpu_to_be32(NFS4_LIMIT_SIZE);
		*p++ = cpu_to_be32(~(u32)0);
		*p++ = cpu_to_be32(~(u32)0);

		/*
		 * TODO: ACE's in delegations
		 */
		*p++ = cpu_to_be32(NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE);
		*p++ = cpu_to_be32(0);
		*p++ = cpu_to_be32(0);
		*p++ = cpu_to_be32(0);   /* XXX: is NULL principal ok? */
		break;
	case NFS4_OPEN_DELEGATE_NONE_EXT: /* 4.1 */
		switch (open->op_why_no_deleg) {
		case WND4_CONTENTION:
		case WND4_RESOURCE:
			p = xdr_reserve_space(xdr, 8);
			if (!p)
				return nfserr_resource;
			*p++ = cpu_to_be32(open->op_why_no_deleg);
			/* deleg signaling not supported yet: */
			*p++ = cpu_to_be32(0);
			break;
		default:
			p = xdr_reserve_space(xdr, 4);
			if (!p)
				return nfserr_resource;
			*p++ = cpu_to_be32(open->op_why_no_deleg);
		}
		break;
	default:
		BUG();
	}
	/* XXX save filehandle here */
	return 0;
}

static __be32
nfsd4_encode_open_confirm(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_open_confirm *oc)
{
	struct xdr_stream *xdr = &resp->xdr;

	return nfsd4_encode_stateid(xdr, &oc->oc_resp_stateid);
}

static __be32
nfsd4_encode_open_downgrade(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_open_downgrade *od)
{
	struct xdr_stream *xdr = &resp->xdr;

	return nfsd4_encode_stateid(xdr, &od->od_stateid);
}

static __be32 nfsd4_encode_splice_read(
				struct nfsd4_compoundres *resp,
				struct nfsd4_read *read,
				struct file *file, unsigned long maxcount)
{
	struct xdr_stream *xdr = &resp->xdr;
	struct xdr_buf *buf = xdr->buf;
	u32 eof;
	long len;
	int space_left;
	__be32 nfserr;
	__be32 *p = xdr->p - 2;

	/* Make sure there will be room for padding if needed */
	if (xdr->end - xdr->p < 1)
		return nfserr_resource;

	len = maxcount;
	nfserr = nfsd_splice_read(read->rd_rqstp, read->rd_fhp,
				  file, read->rd_offset, &maxcount);
	read->rd_length = maxcount;
	if (nfserr) {
		/*
		 * nfsd_splice_actor may have already messed with the
		 * page length; reset it so as not to confuse
		 * xdr_truncate_encode:
		 */
		buf->page_len = 0;
		return nfserr;
	}

	eof = nfsd_eof_on_read(len, maxcount, read->rd_offset,
				d_inode(read->rd_fhp->fh_dentry)->i_size);

	*(p++) = htonl(eof);
	*(p++) = htonl(maxcount);

	buf->page_len = maxcount;
	buf->len += maxcount;
	xdr->page_ptr += (buf->page_base + maxcount + PAGE_SIZE - 1)
							/ PAGE_SIZE;

	/* Use rest of head for padding and remaining ops: */
	buf->tail[0].iov_base = xdr->p;
	buf->tail[0].iov_len = 0;
	xdr->iov = buf->tail;
	if (maxcount&3) {
		int pad = 4 - (maxcount&3);

		*(xdr->p++) = 0;

		buf->tail[0].iov_base += maxcount&3;
		buf->tail[0].iov_len = pad;
		buf->len += pad;
	}

	space_left = min_t(int, (void *)xdr->end - (void *)xdr->p,
				buf->buflen - buf->len);
	buf->buflen = buf->len + space_left;
	xdr->end = (__be32 *)((void *)xdr->end + space_left);

	return 0;
}

static __be32 nfsd4_encode_readv(struct nfsd4_compoundres *resp,
				 struct nfsd4_read *read,
				 struct file *file, unsigned long maxcount)
{
	struct xdr_stream *xdr = &resp->xdr;
	u32 eof;
	int v;
	int starting_len = xdr->buf->len - 8;
	long len;
	int thislen;
	__be32 nfserr;
	__be32 tmp;
	__be32 *p;
	u32 zzz = 0;
	int pad;

	len = maxcount;
	v = 0;

	thislen = min_t(long, len, ((void *)xdr->end - (void *)xdr->p));
	p = xdr_reserve_space(xdr, (thislen+3)&~3);
	WARN_ON_ONCE(!p);
	resp->rqstp->rq_vec[v].iov_base = p;
	resp->rqstp->rq_vec[v].iov_len = thislen;
	v++;
	len -= thislen;

	while (len) {
		thislen = min_t(long, len, PAGE_SIZE);
		p = xdr_reserve_space(xdr, (thislen+3)&~3);
		WARN_ON_ONCE(!p);
		resp->rqstp->rq_vec[v].iov_base = p;
		resp->rqstp->rq_vec[v].iov_len = thislen;
		v++;
		len -= thislen;
	}
	read->rd_vlen = v;

	len = maxcount;
	nfserr = nfsd_readv(resp->rqstp, read->rd_fhp, file, read->rd_offset,
			    resp->rqstp->rq_vec, read->rd_vlen, &maxcount);
	read->rd_length = maxcount;
	if (nfserr)
		return nfserr;
	xdr_truncate_encode(xdr, starting_len + 8 + ((maxcount+3)&~3));

	eof = nfsd_eof_on_read(len, maxcount, read->rd_offset,
				d_inode(read->rd_fhp->fh_dentry)->i_size);

	tmp = htonl(eof);
	write_bytes_to_xdr_buf(xdr->buf, starting_len    , &tmp, 4);
	tmp = htonl(maxcount);
	write_bytes_to_xdr_buf(xdr->buf, starting_len + 4, &tmp, 4);

	pad = (maxcount&3) ? 4 - (maxcount&3) : 0;
	write_bytes_to_xdr_buf(xdr->buf, starting_len + 8 + maxcount,
								&zzz, pad);
	return 0;

}

static __be32
nfsd4_encode_read(struct nfsd4_compoundres *resp, __be32 nfserr,
		  struct nfsd4_read *read)
{
	unsigned long maxcount;
	struct xdr_stream *xdr = &resp->xdr;
	struct file *file = read->rd_filp;
	int starting_len = xdr->buf->len;
	struct raparms *ra = NULL;
	__be32 *p;

	p = xdr_reserve_space(xdr, 8); /* eof flag and byte count */
	if (!p) {
		WARN_ON_ONCE(test_bit(RQ_SPLICE_OK, &resp->rqstp->rq_flags));
		return nfserr_resource;
	}
	if (resp->xdr.buf->page_len &&
	    test_bit(RQ_SPLICE_OK, &resp->rqstp->rq_flags)) {
		WARN_ON_ONCE(1);
		return nfserr_resource;
	}
	xdr_commit_encode(xdr);

	maxcount = svc_max_payload(resp->rqstp);
	maxcount = min_t(unsigned long, maxcount,
			 (xdr->buf->buflen - xdr->buf->len));
	maxcount = min_t(unsigned long, maxcount, read->rd_length);

	if (read->rd_tmp_file)
		ra = nfsd_init_raparms(file);

	if (file->f_op->splice_read &&
	    test_bit(RQ_SPLICE_OK, &resp->rqstp->rq_flags))
		nfserr = nfsd4_encode_splice_read(resp, read, file, maxcount);
	else
		nfserr = nfsd4_encode_readv(resp, read, file, maxcount);

	if (ra)
		nfsd_put_raparams(file, ra);

	if (nfserr)
		xdr_truncate_encode(xdr, starting_len);

	return nfserr;
}

static __be32
nfsd4_encode_readlink(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_readlink *readlink)
{
	int maxcount;
	__be32 wire_count;
	int zero = 0;
	struct xdr_stream *xdr = &resp->xdr;
	int length_offset = xdr->buf->len;
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return nfserr_resource;
	maxcount = PAGE_SIZE;

	p = xdr_reserve_space(xdr, maxcount);
	if (!p)
		return nfserr_resource;
	/*
	 * XXX: By default, vfs_readlink() will truncate symlinks if they
	 * would overflow the buffer.  Is this kosher in NFSv4?  If not, one
	 * easy fix is: if vfs_readlink() precisely fills the buffer, assume
	 * that truncation occurred, and return NFS4ERR_RESOURCE.
	 */
	nfserr = nfsd_readlink(readlink->rl_rqstp, readlink->rl_fhp,
						(char *)p, &maxcount);
	if (nfserr == nfserr_isdir)
		nfserr = nfserr_inval;
	if (nfserr) {
		xdr_truncate_encode(xdr, length_offset);
		return nfserr;
	}

	wire_count = htonl(maxcount);
	write_bytes_to_xdr_buf(xdr->buf, length_offset, &wire_count, 4);
	xdr_truncate_encode(xdr, length_offset + 4 + ALIGN(maxcount, 4));
	if (maxcount & 3)
		write_bytes_to_xdr_buf(xdr->buf, length_offset + 4 + maxcount,
						&zero, 4 - (maxcount&3));
	return 0;
}

static __be32
nfsd4_encode_readdir(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_readdir *readdir)
{
	int maxcount;
	int bytes_left;
	loff_t offset;
	__be64 wire_offset;
	struct xdr_stream *xdr = &resp->xdr;
	int starting_len = xdr->buf->len;
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS4_VERIFIER_SIZE);
	if (!p)
		return nfserr_resource;

	/* XXX: Following NFSv3, we ignore the READDIR verifier for now. */
	*p++ = cpu_to_be32(0);
	*p++ = cpu_to_be32(0);
	resp->xdr.buf->head[0].iov_len = ((char *)resp->xdr.p)
				- (char *)resp->xdr.buf->head[0].iov_base;

	/*
	 * Number of bytes left for directory entries allowing for the
	 * final 8 bytes of the readdir and a following failed op:
	 */
	bytes_left = xdr->buf->buflen - xdr->buf->len
			- COMPOUND_ERR_SLACK_SPACE - 8;
	if (bytes_left < 0) {
		nfserr = nfserr_resource;
		goto err_no_verf;
	}
	maxcount = min_t(u32, readdir->rd_maxcount, INT_MAX);
	/*
	 * Note the rfc defines rd_maxcount as the size of the
	 * READDIR4resok structure, which includes the verifier above
	 * and the 8 bytes encoded at the end of this function:
	 */
	if (maxcount < 16) {
		nfserr = nfserr_toosmall;
		goto err_no_verf;
	}
	maxcount = min_t(int, maxcount-16, bytes_left);

	/* RFC 3530 14.2.24 allows us to ignore dircount when it's 0: */
	if (!readdir->rd_dircount)
		readdir->rd_dircount = INT_MAX;

	readdir->xdr = xdr;
	readdir->rd_maxcount = maxcount;
	readdir->common.err = 0;
	readdir->cookie_offset = 0;

	offset = readdir->rd_cookie;
	nfserr = nfsd_readdir(readdir->rd_rqstp, readdir->rd_fhp,
			      &offset,
			      &readdir->common, nfsd4_encode_dirent);
	if (nfserr == nfs_ok &&
	    readdir->common.err == nfserr_toosmall &&
	    xdr->buf->len == starting_len + 8) {
		/* nothing encoded; which limit did we hit?: */
		if (maxcount - 16 < bytes_left)
			/* It was the fault of rd_maxcount: */
			nfserr = nfserr_toosmall;
		else
			/* We ran out of buffer space: */
			nfserr = nfserr_resource;
	}
	if (nfserr)
		goto err_no_verf;

	if (readdir->cookie_offset) {
		wire_offset = cpu_to_be64(offset);
		write_bytes_to_xdr_buf(xdr->buf, readdir->cookie_offset,
							&wire_offset, 8);
	}

	p = xdr_reserve_space(xdr, 8);
	if (!p) {
		WARN_ON_ONCE(1);
		goto err_no_verf;
	}
	*p++ = 0;	/* no more entries */
	*p++ = htonl(readdir->common.err == nfserr_eof);

	return 0;
err_no_verf:
	xdr_truncate_encode(xdr, starting_len);
	return nfserr;
}

static __be32
nfsd4_encode_remove(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_remove *remove)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, 20);
	if (!p)
		return nfserr_resource;
	p = encode_cinfo(p, &remove->rm_cinfo);
	return 0;
}

static __be32
nfsd4_encode_rename(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_rename *rename)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, 40);
	if (!p)
		return nfserr_resource;
	p = encode_cinfo(p, &rename->rn_sinfo);
	p = encode_cinfo(p, &rename->rn_tinfo);
	return 0;
}

static __be32
nfsd4_do_encode_secinfo(struct xdr_stream *xdr, struct svc_export *exp)
{
	u32 i, nflavs, supported;
	struct exp_flavor_info *flavs;
	struct exp_flavor_info def_flavs[2];
	__be32 *p, *flavorsp;
	static bool report = true;

	if (exp->ex_nflavors) {
		flavs = exp->ex_flavors;
		nflavs = exp->ex_nflavors;
	} else { /* Handling of some defaults in absence of real secinfo: */
		flavs = def_flavs;
		if (exp->ex_client->flavour->flavour == RPC_AUTH_UNIX) {
			nflavs = 2;
			flavs[0].pseudoflavor = RPC_AUTH_UNIX;
			flavs[1].pseudoflavor = RPC_AUTH_NULL;
		} else if (exp->ex_client->flavour->flavour == RPC_AUTH_GSS) {
			nflavs = 1;
			flavs[0].pseudoflavor
					= svcauth_gss_flavor(exp->ex_client);
		} else {
			nflavs = 1;
			flavs[0].pseudoflavor
					= exp->ex_client->flavour->flavour;
		}
	}

	supported = 0;
	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return nfserr_resource;
	flavorsp = p++;		/* to be backfilled later */

	for (i = 0; i < nflavs; i++) {
		rpc_authflavor_t pf = flavs[i].pseudoflavor;
		struct rpcsec_gss_info info;

		if (rpcauth_get_gssinfo(pf, &info) == 0) {
			supported++;
			p = xdr_reserve_space(xdr, 4 + 4 +
					      XDR_LEN(info.oid.len) + 4 + 4);
			if (!p)
				return nfserr_resource;
			*p++ = cpu_to_be32(RPC_AUTH_GSS);
			p = xdr_encode_opaque(p,  info.oid.data, info.oid.len);
			*p++ = cpu_to_be32(info.qop);
			*p++ = cpu_to_be32(info.service);
		} else if (pf < RPC_AUTH_MAXFLAVOR) {
			supported++;
			p = xdr_reserve_space(xdr, 4);
			if (!p)
				return nfserr_resource;
			*p++ = cpu_to_be32(pf);
		} else {
			if (report)
				pr_warn("NFS: SECINFO: security flavor %u "
					"is not supported\n", pf);
		}
	}

	if (nflavs != supported)
		report = false;
	*flavorsp = htonl(supported);
	return 0;
}

static __be32
nfsd4_encode_secinfo(struct nfsd4_compoundres *resp, __be32 nfserr,
		     struct nfsd4_secinfo *secinfo)
{
	struct xdr_stream *xdr = &resp->xdr;

	return nfsd4_do_encode_secinfo(xdr, secinfo->si_exp);
}

static __be32
nfsd4_encode_secinfo_no_name(struct nfsd4_compoundres *resp, __be32 nfserr,
		     struct nfsd4_secinfo_no_name *secinfo)
{
	struct xdr_stream *xdr = &resp->xdr;

	return nfsd4_do_encode_secinfo(xdr, secinfo->sin_exp);
}

/*
 * The SETATTR encode routine is special -- it always encodes a bitmap,
 * regardless of the error status.
 */
static __be32
nfsd4_encode_setattr(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_setattr *setattr)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, 16);
	if (!p)
		return nfserr_resource;
	if (nfserr) {
		*p++ = cpu_to_be32(3);
		*p++ = cpu_to_be32(0);
		*p++ = cpu_to_be32(0);
		*p++ = cpu_to_be32(0);
	}
	else {
		*p++ = cpu_to_be32(3);
		*p++ = cpu_to_be32(setattr->sa_bmval[0]);
		*p++ = cpu_to_be32(setattr->sa_bmval[1]);
		*p++ = cpu_to_be32(setattr->sa_bmval[2]);
	}
	return nfserr;
}

static __be32
nfsd4_encode_setclientid(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_setclientid *scd)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	if (!nfserr) {
		p = xdr_reserve_space(xdr, 8 + NFS4_VERIFIER_SIZE);
		if (!p)
			return nfserr_resource;
		p = xdr_encode_opaque_fixed(p, &scd->se_clientid, 8);
		p = xdr_encode_opaque_fixed(p, &scd->se_confirm,
						NFS4_VERIFIER_SIZE);
	}
	else if (nfserr == nfserr_clid_inuse) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			return nfserr_resource;
		*p++ = cpu_to_be32(0);
		*p++ = cpu_to_be32(0);
	}
	return nfserr;
}

static __be32
nfsd4_encode_write(struct nfsd4_compoundres *resp, __be32 nfserr, struct nfsd4_write *write)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, 16);
	if (!p)
		return nfserr_resource;
	*p++ = cpu_to_be32(write->wr_bytes_written);
	*p++ = cpu_to_be32(write->wr_how_written);
	p = xdr_encode_opaque_fixed(p, write->wr_verifier.data,
						NFS4_VERIFIER_SIZE);
	return 0;
}

static __be32
nfsd4_encode_exchange_id(struct nfsd4_compoundres *resp, __be32 nfserr,
			 struct nfsd4_exchange_id *exid)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;
	char *major_id;
	char *server_scope;
	int major_id_sz;
	int server_scope_sz;
	uint64_t minor_id = 0;

	major_id = utsname()->nodename;
	major_id_sz = strlen(major_id);
	server_scope = utsname()->nodename;
	server_scope_sz = strlen(server_scope);

	p = xdr_reserve_space(xdr,
		8 /* eir_clientid */ +
		4 /* eir_sequenceid */ +
		4 /* eir_flags */ +
		4 /* spr_how */);
	if (!p)
		return nfserr_resource;

	p = xdr_encode_opaque_fixed(p, &exid->clientid, 8);
	*p++ = cpu_to_be32(exid->seqid);
	*p++ = cpu_to_be32(exid->flags);

	*p++ = cpu_to_be32(exid->spa_how);

	switch (exid->spa_how) {
	case SP4_NONE:
		break;
	case SP4_MACH_CRED:
		/* spo_must_enforce bitmap: */
		nfserr = nfsd4_encode_bitmap(xdr,
					exid->spo_must_enforce[0],
					exid->spo_must_enforce[1],
					exid->spo_must_enforce[2]);
		if (nfserr)
			return nfserr;
		/* spo_must_allow bitmap: */
		nfserr = nfsd4_encode_bitmap(xdr,
					exid->spo_must_allow[0],
					exid->spo_must_allow[1],
					exid->spo_must_allow[2]);
		if (nfserr)
			return nfserr;
		break;
	default:
		WARN_ON_ONCE(1);
	}

	p = xdr_reserve_space(xdr,
		8 /* so_minor_id */ +
		4 /* so_major_id.len */ +
		(XDR_QUADLEN(major_id_sz) * 4) +
		4 /* eir_server_scope.len */ +
		(XDR_QUADLEN(server_scope_sz) * 4) +
		4 /* eir_server_impl_id.count (0) */);
	if (!p)
		return nfserr_resource;

	/* The server_owner struct */
	p = xdr_encode_hyper(p, minor_id);      /* Minor id */
	/* major id */
	p = xdr_encode_opaque(p, major_id, major_id_sz);

	/* Server scope */
	p = xdr_encode_opaque(p, server_scope, server_scope_sz);

	/* Implementation id */
	*p++ = cpu_to_be32(0);	/* zero length nfs_impl_id4 array */
	return 0;
}

static __be32
nfsd4_encode_create_session(struct nfsd4_compoundres *resp, __be32 nfserr,
			    struct nfsd4_create_session *sess)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, 24);
	if (!p)
		return nfserr_resource;
	p = xdr_encode_opaque_fixed(p, sess->sessionid.data,
					NFS4_MAX_SESSIONID_LEN);
	*p++ = cpu_to_be32(sess->seqid);
	*p++ = cpu_to_be32(sess->flags);

	p = xdr_reserve_space(xdr, 28);
	if (!p)
		return nfserr_resource;
	*p++ = cpu_to_be32(0); /* headerpadsz */
	*p++ = cpu_to_be32(sess->fore_channel.maxreq_sz);
	*p++ = cpu_to_be32(sess->fore_channel.maxresp_sz);
	*p++ = cpu_to_be32(sess->fore_channel.maxresp_cached);
	*p++ = cpu_to_be32(sess->fore_channel.maxops);
	*p++ = cpu_to_be32(sess->fore_channel.maxreqs);
	*p++ = cpu_to_be32(sess->fore_channel.nr_rdma_attrs);

	if (sess->fore_channel.nr_rdma_attrs) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			return nfserr_resource;
		*p++ = cpu_to_be32(sess->fore_channel.rdma_attrs);
	}

	p = xdr_reserve_space(xdr, 28);
	if (!p)
		return nfserr_resource;
	*p++ = cpu_to_be32(0); /* headerpadsz */
	*p++ = cpu_to_be32(sess->back_channel.maxreq_sz);
	*p++ = cpu_to_be32(sess->back_channel.maxresp_sz);
	*p++ = cpu_to_be32(sess->back_channel.maxresp_cached);
	*p++ = cpu_to_be32(sess->back_channel.maxops);
	*p++ = cpu_to_be32(sess->back_channel.maxreqs);
	*p++ = cpu_to_be32(sess->back_channel.nr_rdma_attrs);

	if (sess->back_channel.nr_rdma_attrs) {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			return nfserr_resource;
		*p++ = cpu_to_be32(sess->back_channel.rdma_attrs);
	}
	return 0;
}

static __be32
nfsd4_encode_sequence(struct nfsd4_compoundres *resp, __be32 nfserr,
		      struct nfsd4_sequence *seq)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS4_MAX_SESSIONID_LEN + 20);
	if (!p)
		return nfserr_resource;
	p = xdr_encode_opaque_fixed(p, seq->sessionid.data,
					NFS4_MAX_SESSIONID_LEN);
	*p++ = cpu_to_be32(seq->seqid);
	*p++ = cpu_to_be32(seq->slotid);
	/* Note slotid's are numbered from zero: */
	*p++ = cpu_to_be32(seq->maxslots - 1); /* sr_highest_slotid */
	*p++ = cpu_to_be32(seq->maxslots - 1); /* sr_target_highest_slotid */
	*p++ = cpu_to_be32(seq->status_flags);

	resp->cstate.data_offset = xdr->buf->len; /* DRC cache data pointer */
	return 0;
}

static __be32
nfsd4_encode_test_stateid(struct nfsd4_compoundres *resp, __be32 nfserr,
			  struct nfsd4_test_stateid *test_stateid)
{
	struct xdr_stream *xdr = &resp->xdr;
	struct nfsd4_test_stateid_id *stateid, *next;
	__be32 *p;

	p = xdr_reserve_space(xdr, 4 + (4 * test_stateid->ts_num_ids));
	if (!p)
		return nfserr_resource;
	*p++ = htonl(test_stateid->ts_num_ids);

	list_for_each_entry_safe(stateid, next, &test_stateid->ts_stateid_list, ts_id_list) {
		*p++ = stateid->ts_id_status;
	}

	return 0;
}

#ifdef CONFIG_NFSD_PNFS
static __be32
nfsd4_encode_getdeviceinfo(struct nfsd4_compoundres *resp, __be32 nfserr,
		struct nfsd4_getdeviceinfo *gdev)
{
	struct xdr_stream *xdr = &resp->xdr;
	const struct nfsd4_layout_ops *ops;
	u32 starting_len = xdr->buf->len, needed_len;
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return nfserr_resource;

	*p++ = cpu_to_be32(gdev->gd_layout_type);

	/* If maxcount is 0 then just update notifications */
	if (gdev->gd_maxcount != 0) {
		ops = nfsd4_layout_ops[gdev->gd_layout_type];
		nfserr = ops->encode_getdeviceinfo(xdr, gdev);
		if (nfserr) {
			/*
			 * We don't bother to burden the layout drivers with
			 * enforcing gd_maxcount, just tell the client to
			 * come back with a bigger buffer if it's not enough.
			 */
			if (xdr->buf->len + 4 > gdev->gd_maxcount)
				goto toosmall;
			return nfserr;
		}
	}

	if (gdev->gd_notify_types) {
		p = xdr_reserve_space(xdr, 4 + 4);
		if (!p)
			return nfserr_resource;
		*p++ = cpu_to_be32(1);			/* bitmap length */
		*p++ = cpu_to_be32(gdev->gd_notify_types);
	} else {
		p = xdr_reserve_space(xdr, 4);
		if (!p)
			return nfserr_resource;
		*p++ = 0;
	}

	return 0;
toosmall:
	dprintk("%s: maxcount too small\n", __func__);
	needed_len = xdr->buf->len + 4 /* notifications */;
	xdr_truncate_encode(xdr, starting_len);
	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return nfserr_resource;
	*p++ = cpu_to_be32(needed_len);
	return nfserr_toosmall;
}

static __be32
nfsd4_encode_layoutget(struct nfsd4_compoundres *resp, __be32 nfserr,
		struct nfsd4_layoutget *lgp)
{
	struct xdr_stream *xdr = &resp->xdr;
	const struct nfsd4_layout_ops *ops;
	__be32 *p;

	p = xdr_reserve_space(xdr, 36 + sizeof(stateid_opaque_t));
	if (!p)
		return nfserr_resource;

	*p++ = cpu_to_be32(1);	/* we always set return-on-close */
	*p++ = cpu_to_be32(lgp->lg_sid.si_generation);
	p = xdr_encode_opaque_fixed(p, &lgp->lg_sid.si_opaque,
				    sizeof(stateid_opaque_t));

	*p++ = cpu_to_be32(1);	/* we always return a single layout */
	p = xdr_encode_hyper(p, lgp->lg_seg.offset);
	p = xdr_encode_hyper(p, lgp->lg_seg.length);
	*p++ = cpu_to_be32(lgp->lg_seg.iomode);
	*p++ = cpu_to_be32(lgp->lg_layout_type);

	ops = nfsd4_layout_ops[lgp->lg_layout_type];
	return ops->encode_layoutget(xdr, lgp);
}

static __be32
nfsd4_encode_layoutcommit(struct nfsd4_compoundres *resp, __be32 nfserr,
			  struct nfsd4_layoutcommit *lcp)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return nfserr_resource;
	*p++ = cpu_to_be32(lcp->lc_size_chg);
	if (lcp->lc_size_chg) {
		p = xdr_reserve_space(xdr, 8);
		if (!p)
			return nfserr_resource;
		p = xdr_encode_hyper(p, lcp->lc_newsize);
	}

	return 0;
}

static __be32
nfsd4_encode_layoutreturn(struct nfsd4_compoundres *resp, __be32 nfserr,
		struct nfsd4_layoutreturn *lrp)
{
	struct xdr_stream *xdr = &resp->xdr;
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return nfserr_resource;
	*p++ = cpu_to_be32(lrp->lrs_present);
	if (lrp->lrs_present)
		return nfsd4_encode_stateid(xdr, &lrp->lr_sid);
	return 0;
}
#endif /* CONFIG_NFSD_PNFS */

static __be32
nfsd42_encode_write_res(struct nfsd4_compoundres *resp, struct nfsd42_write_res *write)
{
	__be32 *p;

	p = xdr_reserve_space(&resp->xdr, 4 + 8 + 4 + NFS4_VERIFIER_SIZE);
	if (!p)
		return nfserr_resource;

	*p++ = cpu_to_be32(0);
	p = xdr_encode_hyper(p, write->wr_bytes_written);
	*p++ = cpu_to_be32(write->wr_stable_how);
	p = xdr_encode_opaque_fixed(p, write->wr_verifier.data,
				    NFS4_VERIFIER_SIZE);
	return nfs_ok;
}

static __be32
nfsd4_encode_copy(struct nfsd4_compoundres *resp, __be32 nfserr,
		  struct nfsd4_copy *copy)
{
	__be32 *p;

	nfserr = nfsd42_encode_write_res(resp, &copy->cp_res);
	if (nfserr)
		return nfserr;

	p = xdr_reserve_space(&resp->xdr, 4 + 4);
	*p++ = xdr_one; /* cr_consecutive */
	*p++ = cpu_to_be32(copy->cp_synchronous);
	return 0;
}

static __be32
nfsd4_encode_seek(struct nfsd4_compoundres *resp, __be32 nfserr,
		  struct nfsd4_seek *seek)
{
	__be32 *p;

	p = xdr_reserve_space(&resp->xdr, 4 + 8);
	*p++ = cpu_to_be32(seek->seek_eof);
	p = xdr_encode_hyper(p, seek->seek_pos);

	return 0;
}

static __be32
nfsd4_encode_noop(struct nfsd4_compoundres *resp, __be32 nfserr, void *p)
{
	return nfserr;
}

typedef __be32(* nfsd4_enc)(struct nfsd4_compoundres *, __be32, void *);

/*
 * Note: nfsd4_enc_ops vector is shared for v4.0 and v4.1
 * since we don't need to filter out obsolete ops as this is
 * done in the decoding phase.
 */
static const nfsd4_enc nfsd4_enc_ops[] = {
	[OP_ACCESS]		= (nfsd4_enc)nfsd4_encode_access,
	[OP_CLOSE]		= (nfsd4_enc)nfsd4_encode_close,
	[OP_COMMIT]		= (nfsd4_enc)nfsd4_encode_commit,
	[OP_CREATE]		= (nfsd4_enc)nfsd4_encode_create,
	[OP_DELEGPURGE]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_DELEGRETURN]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_GETATTR]		= (nfsd4_enc)nfsd4_encode_getattr,
	[OP_GETFH]		= (nfsd4_enc)nfsd4_encode_getfh,
	[OP_LINK]		= (nfsd4_enc)nfsd4_encode_link,
	[OP_LOCK]		= (nfsd4_enc)nfsd4_encode_lock,
	[OP_LOCKT]		= (nfsd4_enc)nfsd4_encode_lockt,
	[OP_LOCKU]		= (nfsd4_enc)nfsd4_encode_locku,
	[OP_LOOKUP]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_LOOKUPP]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_NVERIFY]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_OPEN]		= (nfsd4_enc)nfsd4_encode_open,
	[OP_OPENATTR]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_OPEN_CONFIRM]	= (nfsd4_enc)nfsd4_encode_open_confirm,
	[OP_OPEN_DOWNGRADE]	= (nfsd4_enc)nfsd4_encode_open_downgrade,
	[OP_PUTFH]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_PUTPUBFH]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_PUTROOTFH]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_READ]		= (nfsd4_enc)nfsd4_encode_read,
	[OP_READDIR]		= (nfsd4_enc)nfsd4_encode_readdir,
	[OP_READLINK]		= (nfsd4_enc)nfsd4_encode_readlink,
	[OP_REMOVE]		= (nfsd4_enc)nfsd4_encode_remove,
	[OP_RENAME]		= (nfsd4_enc)nfsd4_encode_rename,
	[OP_RENEW]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_RESTOREFH]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_SAVEFH]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_SECINFO]		= (nfsd4_enc)nfsd4_encode_secinfo,
	[OP_SETATTR]		= (nfsd4_enc)nfsd4_encode_setattr,
	[OP_SETCLIENTID]	= (nfsd4_enc)nfsd4_encode_setclientid,
	[OP_SETCLIENTID_CONFIRM] = (nfsd4_enc)nfsd4_encode_noop,
	[OP_VERIFY]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_WRITE]		= (nfsd4_enc)nfsd4_encode_write,
	[OP_RELEASE_LOCKOWNER]	= (nfsd4_enc)nfsd4_encode_noop,

	/* NFSv4.1 operations */
	[OP_BACKCHANNEL_CTL]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_BIND_CONN_TO_SESSION] = (nfsd4_enc)nfsd4_encode_bind_conn_to_session,
	[OP_EXCHANGE_ID]	= (nfsd4_enc)nfsd4_encode_exchange_id,
	[OP_CREATE_SESSION]	= (nfsd4_enc)nfsd4_encode_create_session,
	[OP_DESTROY_SESSION]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_FREE_STATEID]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_GET_DIR_DELEGATION]	= (nfsd4_enc)nfsd4_encode_noop,
#ifdef CONFIG_NFSD_PNFS
	[OP_GETDEVICEINFO]	= (nfsd4_enc)nfsd4_encode_getdeviceinfo,
	[OP_GETDEVICELIST]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_LAYOUTCOMMIT]	= (nfsd4_enc)nfsd4_encode_layoutcommit,
	[OP_LAYOUTGET]		= (nfsd4_enc)nfsd4_encode_layoutget,
	[OP_LAYOUTRETURN]	= (nfsd4_enc)nfsd4_encode_layoutreturn,
#else
	[OP_GETDEVICEINFO]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_GETDEVICELIST]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_LAYOUTCOMMIT]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_LAYOUTGET]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_LAYOUTRETURN]	= (nfsd4_enc)nfsd4_encode_noop,
#endif
	[OP_SECINFO_NO_NAME]	= (nfsd4_enc)nfsd4_encode_secinfo_no_name,
	[OP_SEQUENCE]		= (nfsd4_enc)nfsd4_encode_sequence,
	[OP_SET_SSV]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_TEST_STATEID]	= (nfsd4_enc)nfsd4_encode_test_stateid,
	[OP_WANT_DELEGATION]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_DESTROY_CLIENTID]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_RECLAIM_COMPLETE]	= (nfsd4_enc)nfsd4_encode_noop,

	/* NFSv4.2 operations */
	[OP_ALLOCATE]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_COPY]		= (nfsd4_enc)nfsd4_encode_copy,
	[OP_COPY_NOTIFY]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_DEALLOCATE]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_IO_ADVISE]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_LAYOUTERROR]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_LAYOUTSTATS]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_OFFLOAD_CANCEL]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_OFFLOAD_STATUS]	= (nfsd4_enc)nfsd4_encode_noop,
	[OP_READ_PLUS]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_SEEK]		= (nfsd4_enc)nfsd4_encode_seek,
	[OP_WRITE_SAME]		= (nfsd4_enc)nfsd4_encode_noop,
	[OP_CLONE]		= (nfsd4_enc)nfsd4_encode_noop,
};

/*
 * Calculate whether we still have space to encode repsize bytes.
 * There are two considerations:
 *     - For NFS versions >=4.1, the size of the reply must stay within
 *       session limits
 *     - For all NFS versions, we must stay within limited preallocated
 *       buffer space.
 *
 * This is called before the operation is processed, so can only provide
 * an upper estimate.  For some nonidempotent operations (such as
 * getattr), it's not necessarily a problem if that estimate is wrong,
 * as we can fail it after processing without significant side effects.
 */
__be32 nfsd4_check_resp_size(struct nfsd4_compoundres *resp, u32 respsize)
{
	struct xdr_buf *buf = &resp->rqstp->rq_res;
	struct nfsd4_slot *slot = resp->cstate.slot;

	if (buf->len + respsize <= buf->buflen)
		return nfs_ok;
	if (!nfsd4_has_session(&resp->cstate))
		return nfserr_resource;
	if (slot->sl_flags & NFSD4_SLOT_CACHETHIS) {
		WARN_ON_ONCE(1);
		return nfserr_rep_too_big_to_cache;
	}
	return nfserr_rep_too_big;
}

void
nfsd4_encode_operation(struct nfsd4_compoundres *resp, struct nfsd4_op *op)
{
	struct xdr_stream *xdr = &resp->xdr;
	struct nfs4_stateowner *so = resp->cstate.replay_owner;
	struct svc_rqst *rqstp = resp->rqstp;
	const struct nfsd4_operation *opdesc = op->opdesc;
	int post_err_offset;
	nfsd4_enc encoder;
	__be32 *p;

	p = xdr_reserve_space(xdr, 8);
	if (!p) {
		WARN_ON_ONCE(1);
		return;
	}
	*p++ = cpu_to_be32(op->opnum);
	post_err_offset = xdr->buf->len;

	if (op->opnum == OP_ILLEGAL)
		goto status;
	if (op->status && opdesc &&
			!(opdesc->op_flags & OP_NONTRIVIAL_ERROR_ENCODE))
		goto status;
	BUG_ON(op->opnum < 0 || op->opnum >= ARRAY_SIZE(nfsd4_enc_ops) ||
	       !nfsd4_enc_ops[op->opnum]);
	encoder = nfsd4_enc_ops[op->opnum];
	op->status = encoder(resp, op->status, &op->u);
	if (opdesc && opdesc->op_release)
		opdesc->op_release(&op->u);
	xdr_commit_encode(xdr);

	/* nfsd4_check_resp_size guarantees enough room for error status */
	if (!op->status) {
		int space_needed = 0;
		if (!nfsd4_last_compound_op(rqstp))
			space_needed = COMPOUND_ERR_SLACK_SPACE;
		op->status = nfsd4_check_resp_size(resp, space_needed);
	}
	if (op->status == nfserr_resource && nfsd4_has_session(&resp->cstate)) {
		struct nfsd4_slot *slot = resp->cstate.slot;

		if (slot->sl_flags & NFSD4_SLOT_CACHETHIS)
			op->status = nfserr_rep_too_big_to_cache;
		else
			op->status = nfserr_rep_too_big;
	}
	if (op->status == nfserr_resource ||
	    op->status == nfserr_rep_too_big ||
	    op->status == nfserr_rep_too_big_to_cache) {
		/*
		 * The operation may have already been encoded or
		 * partially encoded.  No op returns anything additional
		 * in the case of one of these three errors, so we can
		 * just truncate back to after the status.  But it's a
		 * bug if we had to do this on a non-idempotent op:
		 */
		warn_on_nonidempotent_op(op);
		xdr_truncate_encode(xdr, post_err_offset);
	}
	if (so) {
		int len = xdr->buf->len - post_err_offset;

		so->so_replay.rp_status = op->status;
		so->so_replay.rp_buflen = len;
		read_bytes_from_xdr_buf(xdr->buf, post_err_offset,
						so->so_replay.rp_buf, len);
	}
status:
	/* Note that op->status is already in network byte order: */
	write_bytes_to_xdr_buf(xdr->buf, post_err_offset - 4, &op->status, 4);
}

/* 
 * Encode the reply stored in the stateowner reply cache 
 * 
 * XDR note: do not encode rp->rp_buflen: the buffer contains the
 * previously sent already encoded operation.
 */
void
nfsd4_encode_replay(struct xdr_stream *xdr, struct nfsd4_op *op)
{
	__be32 *p;
	struct nfs4_replay *rp = op->replay;

	BUG_ON(!rp);

	p = xdr_reserve_space(xdr, 8 + rp->rp_buflen);
	if (!p) {
		WARN_ON_ONCE(1);
		return;
	}
	*p++ = cpu_to_be32(op->opnum);
	*p++ = rp->rp_status;  /* already xdr'ed */

	p = xdr_encode_opaque_fixed(p, rp->rp_buf, rp->rp_buflen);
}

int
nfs4svc_encode_voidres(struct svc_rqst *rqstp, __be32 *p)
{
        return xdr_ressize_check(rqstp, p);
}

void nfsd4_release_compoundargs(struct svc_rqst *rqstp)
{
	struct nfsd4_compoundargs *args = rqstp->rq_argp;

	if (args->ops != args->iops) {
		kfree(args->ops);
		args->ops = args->iops;
	}
	kfree(args->tmpp);
	args->tmpp = NULL;
	while (args->to_free) {
		struct svcxdr_tmpbuf *tb = args->to_free;
		args->to_free = tb->next;
		kfree(tb);
	}
}

int
nfs4svc_decode_compoundargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct nfsd4_compoundargs *args = rqstp->rq_argp;

	if (rqstp->rq_arg.head[0].iov_len % 4) {
		/* client is nuts */
		dprintk("%s: compound not properly padded! (peeraddr=%pISc xid=0x%x)",
			__func__, svc_addr(rqstp), be32_to_cpu(rqstp->rq_xid));
		return 0;
	}
	args->p = p;
	args->end = rqstp->rq_arg.head[0].iov_base + rqstp->rq_arg.head[0].iov_len;
	args->pagelist = rqstp->rq_arg.pages;
	args->pagelen = rqstp->rq_arg.page_len;
	args->tail = false;
	args->tmpp = NULL;
	args->to_free = NULL;
	args->ops = args->iops;
	args->rqstp = rqstp;

	return !nfsd4_decode_compound(args);
}

int
nfs4svc_encode_compoundres(struct svc_rqst *rqstp, __be32 *p)
{
	/*
	 * All that remains is to write the tag and operation count...
	 */
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct xdr_buf *buf = resp->xdr.buf;

	WARN_ON_ONCE(buf->len != buf->head[0].iov_len + buf->page_len +
				 buf->tail[0].iov_len);

	rqstp->rq_next_page = resp->xdr.page_ptr + 1;

	p = resp->tagp;
	*p++ = htonl(resp->taglen);
	memcpy(p, resp->tag, resp->taglen);
	p += XDR_QUADLEN(resp->taglen);
	*p++ = htonl(resp->opcnt);

	nfsd4_sequence_done(resp);
	return 1;
}

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
