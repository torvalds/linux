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
#include <linux/sunrpc/addr.h>
#include <linux/xattr.h>
#include <linux/vmalloc.h>

#include <uapi/linux/xattr.h>

#include "idmap.h"
#include "acl.h"
#include "xdr4.h"
#include "vfs.h"
#include "state.h"
#include "cache.h"
#include "netns.h"
#include "pnfs.h"
#include "filecache.h"
#include "nfs4xdr_gen.h"

#include "trace.h"

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
	if (len > NFS4_MAXNAMLEN)
		return nfserr_nametoolong;
	if (isdotent(str, len))
		return nfserr_badname;
	for (i = 0; i < len; i++)
		if (str[i] == '/')
			return nfserr_badname;
	return 0;
}

static int zero_clientid(clientid_t *clid)
{
	return (clid->cl_boot == 0) && (clid->cl_id == 0);
}

/**
 * svcxdr_tmpalloc - allocate memory to be freed after compound processing
 * @argp: NFSv4 compound argument structure
 * @len: length of buffer to allocate
 *
 * Allocates a buffer of size @len to be freed when processing the compound
 * operation described in @argp finishes.
 */
static void *
svcxdr_tmpalloc(struct nfsd4_compoundargs *argp, size_t len)
{
	struct svcxdr_tmpbuf *tb;

	tb = kmalloc(struct_size(tb, buf, len), GFP_KERNEL);
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
svcxdr_dupstr(struct nfsd4_compoundargs *argp, void *buf, size_t len)
{
	char *p = svcxdr_tmpalloc(argp, size_add(len, 1));

	if (!p)
		return NULL;
	memcpy(p, buf, len);
	p[len] = '\0';
	return p;
}

static void *
svcxdr_savemem(struct nfsd4_compoundargs *argp, __be32 *p, size_t len)
{
	__be32 *tmp;

	/*
	 * The location of the decoded data item is stable,
	 * so @p is OK to use. This is the common case.
	 */
	if (p != argp->xdr->scratch.iov_base)
		return p;

	tmp = svcxdr_tmpalloc(argp, len);
	if (!tmp)
		return NULL;
	memcpy(tmp, p, len);
	return tmp;
}

/*
 * NFSv4 basic data type decoders
 */

/*
 * This helper handles variable-length opaques which belong to protocol
 * elements that this implementation does not support.
 */
static __be32
nfsd4_decode_ignored_string(struct nfsd4_compoundargs *argp, u32 maxlen)
{
	u32 len;

	if (xdr_stream_decode_u32(argp->xdr, &len) < 0)
		return nfserr_bad_xdr;
	if (maxlen && len > maxlen)
		return nfserr_bad_xdr;
	if (!xdr_inline_decode(argp->xdr, len))
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_opaque(struct nfsd4_compoundargs *argp, struct xdr_netobj *o)
{
	__be32 *p;
	u32 len;

	if (xdr_stream_decode_u32(argp->xdr, &len) < 0)
		return nfserr_bad_xdr;
	if (len == 0 || len > NFS4_OPAQUE_LIMIT)
		return nfserr_bad_xdr;
	p = xdr_inline_decode(argp->xdr, len);
	if (!p)
		return nfserr_bad_xdr;
	o->data = svcxdr_savemem(argp, p, len);
	if (!o->data)
		return nfserr_jukebox;
	o->len = len;

	return nfs_ok;
}

static __be32
nfsd4_decode_component4(struct nfsd4_compoundargs *argp, char **namp, u32 *lenp)
{
	__be32 *p, status;

	if (xdr_stream_decode_u32(argp->xdr, lenp) < 0)
		return nfserr_bad_xdr;
	p = xdr_inline_decode(argp->xdr, *lenp);
	if (!p)
		return nfserr_bad_xdr;
	status = check_filename((char *)p, *lenp);
	if (status)
		return status;
	*namp = svcxdr_savemem(argp, p, *lenp);
	if (!*namp)
		return nfserr_jukebox;

	return nfs_ok;
}

static __be32
nfsd4_decode_nfstime4(struct nfsd4_compoundargs *argp, struct timespec64 *tv)
{
	__be32 *p;

	p = xdr_inline_decode(argp->xdr, XDR_UNIT * 3);
	if (!p)
		return nfserr_bad_xdr;
	p = xdr_decode_hyper(p, &tv->tv_sec);
	tv->tv_nsec = be32_to_cpup(p++);
	if (tv->tv_nsec >= (u32)1000000000)
		return nfserr_inval;
	return nfs_ok;
}

static __be32
nfsd4_decode_verifier4(struct nfsd4_compoundargs *argp, nfs4_verifier *verf)
{
	__be32 *p;

	p = xdr_inline_decode(argp->xdr, NFS4_VERIFIER_SIZE);
	if (!p)
		return nfserr_bad_xdr;
	memcpy(verf->data, p, sizeof(verf->data));
	return nfs_ok;
}

/**
 * nfsd4_decode_bitmap4 - Decode an NFSv4 bitmap4
 * @argp: NFSv4 compound argument structure
 * @bmval: pointer to an array of u32's to decode into
 * @bmlen: size of the @bmval array
 *
 * The server needs to return nfs_ok rather than nfserr_bad_xdr when
 * encountering bitmaps containing bits it does not recognize. This
 * includes bits in bitmap words past WORDn, where WORDn is the last
 * bitmap WORD the implementation currently supports. Thus we are
 * careful here to simply ignore bits in bitmap words that this
 * implementation has yet to support explicitly.
 *
 * Return values:
 *   %nfs_ok: @bmval populated successfully
 *   %nfserr_bad_xdr: the encoded bitmap was invalid
 */
static __be32
nfsd4_decode_bitmap4(struct nfsd4_compoundargs *argp, u32 *bmval, u32 bmlen)
{
	ssize_t status;

	status = xdr_stream_decode_uint32_array(argp->xdr, bmval, bmlen);
	return status == -EBADMSG ? nfserr_bad_xdr : nfs_ok;
}

static __be32
nfsd4_decode_nfsace4(struct nfsd4_compoundargs *argp, struct nfs4_ace *ace)
{
	__be32 *p, status;
	u32 length;

	if (xdr_stream_decode_u32(argp->xdr, &ace->type) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &ace->flag) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &ace->access_mask) < 0)
		return nfserr_bad_xdr;

	if (xdr_stream_decode_u32(argp->xdr, &length) < 0)
		return nfserr_bad_xdr;
	p = xdr_inline_decode(argp->xdr, length);
	if (!p)
		return nfserr_bad_xdr;
	ace->whotype = nfs4_acl_get_whotype((char *)p, length);
	if (ace->whotype != NFS4_ACL_WHO_NAMED)
		status = nfs_ok;
	else if (ace->flag & NFS4_ACE_IDENTIFIER_GROUP)
		status = nfsd_map_name_to_gid(argp->rqstp,
				(char *)p, length, &ace->who_gid);
	else
		status = nfsd_map_name_to_uid(argp->rqstp,
				(char *)p, length, &ace->who_uid);

	return status;
}

/* A counted array of nfsace4's */
static noinline __be32
nfsd4_decode_acl(struct nfsd4_compoundargs *argp, struct nfs4_acl **acl)
{
	struct nfs4_ace *ace;
	__be32 status;
	u32 count;

	if (xdr_stream_decode_u32(argp->xdr, &count) < 0)
		return nfserr_bad_xdr;

	if (count > xdr_stream_remaining(argp->xdr) / 20)
		/*
		 * Even with 4-byte names there wouldn't be
		 * space for that many aces; something fishy is
		 * going on:
		 */
		return nfserr_fbig;

	*acl = svcxdr_tmpalloc(argp, nfs4_acl_bytes(count));
	if (*acl == NULL)
		return nfserr_jukebox;

	(*acl)->naces = count;
	for (ace = (*acl)->aces; ace < (*acl)->aces + count; ace++) {
		status = nfsd4_decode_nfsace4(argp, ace);
		if (status)
			return status;
	}

	return nfs_ok;
}

static noinline __be32
nfsd4_decode_security_label(struct nfsd4_compoundargs *argp,
			    struct xdr_netobj *label)
{
	u32 lfs, pi, length;
	__be32 *p;

	if (xdr_stream_decode_u32(argp->xdr, &lfs) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &pi) < 0)
		return nfserr_bad_xdr;

	if (xdr_stream_decode_u32(argp->xdr, &length) < 0)
		return nfserr_bad_xdr;
	if (length > NFS4_MAXLABELLEN)
		return nfserr_badlabel;
	p = xdr_inline_decode(argp->xdr, length);
	if (!p)
		return nfserr_bad_xdr;
	label->len = length;
	label->data = svcxdr_dupstr(argp, p, length);
	if (!label->data)
		return nfserr_jukebox;

	return nfs_ok;
}

static __be32
nfsd4_decode_fattr4(struct nfsd4_compoundargs *argp, u32 *bmval, u32 bmlen,
		    struct iattr *iattr, struct nfs4_acl **acl,
		    struct xdr_netobj *label, int *umask)
{
	unsigned int starting_pos;
	u32 attrlist4_count;
	__be32 *p, status;

	iattr->ia_valid = 0;
	status = nfsd4_decode_bitmap4(argp, bmval, bmlen);
	if (status)
		return nfserr_bad_xdr;

	if (bmval[0] & ~NFSD_WRITEABLE_ATTRS_WORD0
	    || bmval[1] & ~NFSD_WRITEABLE_ATTRS_WORD1
	    || bmval[2] & ~NFSD_WRITEABLE_ATTRS_WORD2) {
		if (nfsd_attrs_supported(argp->minorversion, bmval))
			return nfserr_inval;
		return nfserr_attrnotsupp;
	}

	if (xdr_stream_decode_u32(argp->xdr, &attrlist4_count) < 0)
		return nfserr_bad_xdr;
	starting_pos = xdr_stream_pos(argp->xdr);

	if (bmval[0] & FATTR4_WORD0_SIZE) {
		u64 size;

		if (xdr_stream_decode_u64(argp->xdr, &size) < 0)
			return nfserr_bad_xdr;
		iattr->ia_size = size;
		iattr->ia_valid |= ATTR_SIZE;
	}
	if (bmval[0] & FATTR4_WORD0_ACL) {
		status = nfsd4_decode_acl(argp, acl);
		if (status)
			return status;
	} else
		*acl = NULL;
	if (bmval[1] & FATTR4_WORD1_MODE) {
		u32 mode;

		if (xdr_stream_decode_u32(argp->xdr, &mode) < 0)
			return nfserr_bad_xdr;
		iattr->ia_mode = mode;
		iattr->ia_mode &= (S_IFMT | S_IALLUGO);
		iattr->ia_valid |= ATTR_MODE;
	}
	if (bmval[1] & FATTR4_WORD1_OWNER) {
		u32 length;

		if (xdr_stream_decode_u32(argp->xdr, &length) < 0)
			return nfserr_bad_xdr;
		p = xdr_inline_decode(argp->xdr, length);
		if (!p)
			return nfserr_bad_xdr;
		status = nfsd_map_name_to_uid(argp->rqstp, (char *)p, length,
					      &iattr->ia_uid);
		if (status)
			return status;
		iattr->ia_valid |= ATTR_UID;
	}
	if (bmval[1] & FATTR4_WORD1_OWNER_GROUP) {
		u32 length;

		if (xdr_stream_decode_u32(argp->xdr, &length) < 0)
			return nfserr_bad_xdr;
		p = xdr_inline_decode(argp->xdr, length);
		if (!p)
			return nfserr_bad_xdr;
		status = nfsd_map_name_to_gid(argp->rqstp, (char *)p, length,
					      &iattr->ia_gid);
		if (status)
			return status;
		iattr->ia_valid |= ATTR_GID;
	}
	if (bmval[1] & FATTR4_WORD1_TIME_ACCESS_SET) {
		u32 set_it;

		if (xdr_stream_decode_u32(argp->xdr, &set_it) < 0)
			return nfserr_bad_xdr;
		switch (set_it) {
		case NFS4_SET_TO_CLIENT_TIME:
			status = nfsd4_decode_nfstime4(argp, &iattr->ia_atime);
			if (status)
				return status;
			iattr->ia_valid |= (ATTR_ATIME | ATTR_ATIME_SET);
			break;
		case NFS4_SET_TO_SERVER_TIME:
			iattr->ia_valid |= ATTR_ATIME;
			break;
		default:
			return nfserr_bad_xdr;
		}
	}
	if (bmval[1] & FATTR4_WORD1_TIME_CREATE) {
		struct timespec64 ts;

		/* No Linux filesystem supports setting this attribute. */
		bmval[1] &= ~FATTR4_WORD1_TIME_CREATE;
		status = nfsd4_decode_nfstime4(argp, &ts);
		if (status)
			return status;
	}
	if (bmval[1] & FATTR4_WORD1_TIME_MODIFY_SET) {
		u32 set_it;

		if (xdr_stream_decode_u32(argp->xdr, &set_it) < 0)
			return nfserr_bad_xdr;
		switch (set_it) {
		case NFS4_SET_TO_CLIENT_TIME:
			status = nfsd4_decode_nfstime4(argp, &iattr->ia_mtime);
			if (status)
				return status;
			iattr->ia_valid |= (ATTR_MTIME | ATTR_MTIME_SET);
			break;
		case NFS4_SET_TO_SERVER_TIME:
			iattr->ia_valid |= ATTR_MTIME;
			break;
		default:
			return nfserr_bad_xdr;
		}
	}
	label->len = 0;
	if (IS_ENABLED(CONFIG_NFSD_V4_SECURITY_LABEL) &&
	    bmval[2] & FATTR4_WORD2_SECURITY_LABEL) {
		status = nfsd4_decode_security_label(argp, label);
		if (status)
			return status;
	}
	if (bmval[2] & FATTR4_WORD2_MODE_UMASK) {
		u32 mode, mask;

		if (!umask)
			return nfserr_bad_xdr;
		if (xdr_stream_decode_u32(argp->xdr, &mode) < 0)
			return nfserr_bad_xdr;
		iattr->ia_mode = mode & (S_IFMT | S_IALLUGO);
		if (xdr_stream_decode_u32(argp->xdr, &mask) < 0)
			return nfserr_bad_xdr;
		*umask = mask & S_IRWXUGO;
		iattr->ia_valid |= ATTR_MODE;
	}
	if (bmval[2] & FATTR4_WORD2_TIME_DELEG_ACCESS) {
		fattr4_time_deleg_access access;

		if (!xdrgen_decode_fattr4_time_deleg_access(argp->xdr, &access))
			return nfserr_bad_xdr;
		iattr->ia_atime.tv_sec = access.seconds;
		iattr->ia_atime.tv_nsec = access.nseconds;
		iattr->ia_valid |= ATTR_ATIME | ATTR_ATIME_SET | ATTR_DELEG;
	}
	if (bmval[2] & FATTR4_WORD2_TIME_DELEG_MODIFY) {
		fattr4_time_deleg_modify modify;

		if (!xdrgen_decode_fattr4_time_deleg_modify(argp->xdr, &modify))
			return nfserr_bad_xdr;
		iattr->ia_mtime.tv_sec = modify.seconds;
		iattr->ia_mtime.tv_nsec = modify.nseconds;
		iattr->ia_ctime.tv_sec = modify.seconds;
		iattr->ia_ctime.tv_nsec = modify.seconds;
		iattr->ia_valid |= ATTR_CTIME | ATTR_MTIME | ATTR_MTIME_SET | ATTR_DELEG;
	}

	/* request sanity: did attrlist4 contain the expected number of words? */
	if (attrlist4_count != xdr_stream_pos(argp->xdr) - starting_pos)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_stateid4(struct nfsd4_compoundargs *argp, stateid_t *sid)
{
	__be32 *p;

	p = xdr_inline_decode(argp->xdr, NFS4_STATEID_SIZE);
	if (!p)
		return nfserr_bad_xdr;
	sid->si_generation = be32_to_cpup(p++);
	memcpy(&sid->si_opaque, p, sizeof(sid->si_opaque));
	return nfs_ok;
}

static __be32
nfsd4_decode_clientid4(struct nfsd4_compoundargs *argp, clientid_t *clientid)
{
	__be32 *p;

	p = xdr_inline_decode(argp->xdr, sizeof(__be64));
	if (!p)
		return nfserr_bad_xdr;
	memcpy(clientid, p, sizeof(*clientid));
	return nfs_ok;
}

static __be32
nfsd4_decode_state_owner4(struct nfsd4_compoundargs *argp,
			  clientid_t *clientid, struct xdr_netobj *owner)
{
	__be32 status;

	status = nfsd4_decode_clientid4(argp, clientid);
	if (status)
		return status;
	return nfsd4_decode_opaque(argp, owner);
}

#ifdef CONFIG_NFSD_PNFS
static __be32
nfsd4_decode_deviceid4(struct nfsd4_compoundargs *argp,
		       struct nfsd4_deviceid *devid)
{
	__be32 *p;

	p = xdr_inline_decode(argp->xdr, NFS4_DEVICEID4_SIZE);
	if (!p)
		return nfserr_bad_xdr;
	memcpy(devid, p, sizeof(*devid));
	return nfs_ok;
}

static __be32
nfsd4_decode_layoutupdate4(struct nfsd4_compoundargs *argp,
			   struct nfsd4_layoutcommit *lcp)
{
	if (xdr_stream_decode_u32(argp->xdr, &lcp->lc_layout_type) < 0)
		return nfserr_bad_xdr;
	if (lcp->lc_layout_type < LAYOUT_NFSV4_1_FILES)
		return nfserr_bad_xdr;
	if (lcp->lc_layout_type >= LAYOUT_TYPE_MAX)
		return nfserr_bad_xdr;

	if (xdr_stream_decode_u32(argp->xdr, &lcp->lc_up_len) < 0)
		return nfserr_bad_xdr;
	if (lcp->lc_up_len > 0) {
		lcp->lc_up_layout = xdr_inline_decode(argp->xdr, lcp->lc_up_len);
		if (!lcp->lc_up_layout)
			return nfserr_bad_xdr;
	}

	return nfs_ok;
}

static __be32
nfsd4_decode_layoutreturn4(struct nfsd4_compoundargs *argp,
			   struct nfsd4_layoutreturn *lrp)
{
	__be32 status;

	if (xdr_stream_decode_u32(argp->xdr, &lrp->lr_return_type) < 0)
		return nfserr_bad_xdr;
	switch (lrp->lr_return_type) {
	case RETURN_FILE:
		if (xdr_stream_decode_u64(argp->xdr, &lrp->lr_seg.offset) < 0)
			return nfserr_bad_xdr;
		if (xdr_stream_decode_u64(argp->xdr, &lrp->lr_seg.length) < 0)
			return nfserr_bad_xdr;
		status = nfsd4_decode_stateid4(argp, &lrp->lr_sid);
		if (status)
			return status;
		if (xdr_stream_decode_u32(argp->xdr, &lrp->lrf_body_len) < 0)
			return nfserr_bad_xdr;
		if (lrp->lrf_body_len > 0) {
			lrp->lrf_body = xdr_inline_decode(argp->xdr, lrp->lrf_body_len);
			if (!lrp->lrf_body)
				return nfserr_bad_xdr;
		}
		break;
	case RETURN_FSID:
	case RETURN_ALL:
		lrp->lr_seg.offset = 0;
		lrp->lr_seg.length = NFS4_MAX_UINT64;
		break;
	default:
		return nfserr_bad_xdr;
	}

	return nfs_ok;
}

#endif /* CONFIG_NFSD_PNFS */

static __be32
nfsd4_decode_sessionid4(struct nfsd4_compoundargs *argp,
			struct nfs4_sessionid *sessionid)
{
	__be32 *p;

	p = xdr_inline_decode(argp->xdr, NFS4_MAX_SESSIONID_LEN);
	if (!p)
		return nfserr_bad_xdr;
	memcpy(sessionid->data, p, sizeof(sessionid->data));
	return nfs_ok;
}

/* Defined in Appendix A of RFC 5531 */
static __be32
nfsd4_decode_authsys_parms(struct nfsd4_compoundargs *argp,
			   struct nfsd4_cb_sec *cbs)
{
	u32 stamp, gidcount, uid, gid;
	__be32 *p, status;

	if (xdr_stream_decode_u32(argp->xdr, &stamp) < 0)
		return nfserr_bad_xdr;
	/* machine name */
	status = nfsd4_decode_ignored_string(argp, 255);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &uid) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &gid) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &gidcount) < 0)
		return nfserr_bad_xdr;
	if (gidcount > 16)
		return nfserr_bad_xdr;
	p = xdr_inline_decode(argp->xdr, gidcount << 2);
	if (!p)
		return nfserr_bad_xdr;
	if (cbs->flavor == (u32)(-1)) {
		struct user_namespace *userns = nfsd_user_namespace(argp->rqstp);

		kuid_t kuid = make_kuid(userns, uid);
		kgid_t kgid = make_kgid(userns, gid);
		if (uid_valid(kuid) && gid_valid(kgid)) {
			cbs->uid = kuid;
			cbs->gid = kgid;
			cbs->flavor = RPC_AUTH_UNIX;
		} else {
			dprintk("RPC_AUTH_UNIX with invalid uid or gid, ignoring!\n");
		}
	}

	return nfs_ok;
}

static __be32
nfsd4_decode_gss_cb_handles4(struct nfsd4_compoundargs *argp,
			     struct nfsd4_cb_sec *cbs)
{
	__be32 status;
	u32 service;

	dprintk("RPC_AUTH_GSS callback secflavor not supported!\n");

	if (xdr_stream_decode_u32(argp->xdr, &service) < 0)
		return nfserr_bad_xdr;
	if (service < RPC_GSS_SVC_NONE || service > RPC_GSS_SVC_PRIVACY)
		return nfserr_bad_xdr;
	/* gcbp_handle_from_server */
	status = nfsd4_decode_ignored_string(argp, 0);
	if (status)
		return status;
	/* gcbp_handle_from_client */
	status = nfsd4_decode_ignored_string(argp, 0);
	if (status)
		return status;

	return nfs_ok;
}

/* a counted array of callback_sec_parms4 items */
static __be32
nfsd4_decode_cb_sec(struct nfsd4_compoundargs *argp, struct nfsd4_cb_sec *cbs)
{
	u32 i, secflavor, nr_secflavs;
	__be32 status;

	/* callback_sec_params4 */
	if (xdr_stream_decode_u32(argp->xdr, &nr_secflavs) < 0)
		return nfserr_bad_xdr;
	if (nr_secflavs)
		cbs->flavor = (u32)(-1);
	else
		/* Is this legal? Be generous, take it to mean AUTH_NONE: */
		cbs->flavor = 0;

	for (i = 0; i < nr_secflavs; ++i) {
		if (xdr_stream_decode_u32(argp->xdr, &secflavor) < 0)
			return nfserr_bad_xdr;
		switch (secflavor) {
		case RPC_AUTH_NULL:
			/* void */
			if (cbs->flavor == (u32)(-1))
				cbs->flavor = RPC_AUTH_NULL;
			break;
		case RPC_AUTH_UNIX:
			status = nfsd4_decode_authsys_parms(argp, cbs);
			if (status)
				return status;
			break;
		case RPC_AUTH_GSS:
			status = nfsd4_decode_gss_cb_handles4(argp, cbs);
			if (status)
				return status;
			break;
		default:
			return nfserr_inval;
		}
	}

	return nfs_ok;
}


/*
 * NFSv4 operation argument decoders
 */

static __be32
nfsd4_decode_access(struct nfsd4_compoundargs *argp,
		    union nfsd4_op_u *u)
{
	struct nfsd4_access *access = &u->access;
	if (xdr_stream_decode_u32(argp->xdr, &access->ac_req_access) < 0)
		return nfserr_bad_xdr;
	return nfs_ok;
}

static __be32
nfsd4_decode_close(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_close *close = &u->close;
	if (xdr_stream_decode_u32(argp->xdr, &close->cl_seqid) < 0)
		return nfserr_bad_xdr;
	return nfsd4_decode_stateid4(argp, &close->cl_stateid);
}


static __be32
nfsd4_decode_commit(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_commit *commit = &u->commit;
	if (xdr_stream_decode_u64(argp->xdr, &commit->co_offset) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &commit->co_count) < 0)
		return nfserr_bad_xdr;
	memset(&commit->co_verf, 0, sizeof(commit->co_verf));
	return nfs_ok;
}

static __be32
nfsd4_decode_create(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_create *create = &u->create;
	__be32 *p, status;

	memset(create, 0, sizeof(*create));
	if (xdr_stream_decode_u32(argp->xdr, &create->cr_type) < 0)
		return nfserr_bad_xdr;
	switch (create->cr_type) {
	case NF4LNK:
		if (xdr_stream_decode_u32(argp->xdr, &create->cr_datalen) < 0)
			return nfserr_bad_xdr;
		p = xdr_inline_decode(argp->xdr, create->cr_datalen);
		if (!p)
			return nfserr_bad_xdr;
		create->cr_data = svcxdr_dupstr(argp, p, create->cr_datalen);
		if (!create->cr_data)
			return nfserr_jukebox;
		break;
	case NF4BLK:
	case NF4CHR:
		if (xdr_stream_decode_u32(argp->xdr, &create->cr_specdata1) < 0)
			return nfserr_bad_xdr;
		if (xdr_stream_decode_u32(argp->xdr, &create->cr_specdata2) < 0)
			return nfserr_bad_xdr;
		break;
	case NF4SOCK:
	case NF4FIFO:
	case NF4DIR:
	default:
		break;
	}
	status = nfsd4_decode_component4(argp, &create->cr_name,
					 &create->cr_namelen);
	if (status)
		return status;
	status = nfsd4_decode_fattr4(argp, create->cr_bmval,
				    ARRAY_SIZE(create->cr_bmval),
				    &create->cr_iattr, &create->cr_acl,
				    &create->cr_label, &create->cr_umask);
	if (status)
		return status;

	return nfs_ok;
}

static inline __be32
nfsd4_decode_delegreturn(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_delegreturn *dr = &u->delegreturn;
	return nfsd4_decode_stateid4(argp, &dr->dr_stateid);
}

static inline __be32
nfsd4_decode_getattr(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_getattr *getattr = &u->getattr;
	memset(getattr, 0, sizeof(*getattr));
	return nfsd4_decode_bitmap4(argp, getattr->ga_bmval,
				    ARRAY_SIZE(getattr->ga_bmval));
}

static __be32
nfsd4_decode_link(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_link *link = &u->link;
	memset(link, 0, sizeof(*link));
	return nfsd4_decode_component4(argp, &link->li_name, &link->li_namelen);
}

static __be32
nfsd4_decode_open_to_lock_owner4(struct nfsd4_compoundargs *argp,
				 struct nfsd4_lock *lock)
{
	__be32 status;

	if (xdr_stream_decode_u32(argp->xdr, &lock->lk_new_open_seqid) < 0)
		return nfserr_bad_xdr;
	status = nfsd4_decode_stateid4(argp, &lock->lk_new_open_stateid);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &lock->lk_new_lock_seqid) < 0)
		return nfserr_bad_xdr;
	return nfsd4_decode_state_owner4(argp, &lock->lk_new_clientid,
					 &lock->lk_new_owner);
}

static __be32
nfsd4_decode_exist_lock_owner4(struct nfsd4_compoundargs *argp,
			       struct nfsd4_lock *lock)
{
	__be32 status;

	status = nfsd4_decode_stateid4(argp, &lock->lk_old_lock_stateid);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &lock->lk_old_lock_seqid) < 0)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_locker4(struct nfsd4_compoundargs *argp, struct nfsd4_lock *lock)
{
	if (xdr_stream_decode_bool(argp->xdr, &lock->lk_is_new) < 0)
		return nfserr_bad_xdr;
	if (lock->lk_is_new)
		return nfsd4_decode_open_to_lock_owner4(argp, lock);
	return nfsd4_decode_exist_lock_owner4(argp, lock);
}

static __be32
nfsd4_decode_lock(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_lock *lock = &u->lock;
	memset(lock, 0, sizeof(*lock));
	if (xdr_stream_decode_u32(argp->xdr, &lock->lk_type) < 0)
		return nfserr_bad_xdr;
	if ((lock->lk_type < NFS4_READ_LT) || (lock->lk_type > NFS4_WRITEW_LT))
		return nfserr_bad_xdr;
	if (xdr_stream_decode_bool(argp->xdr, &lock->lk_reclaim) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &lock->lk_offset) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &lock->lk_length) < 0)
		return nfserr_bad_xdr;
	return nfsd4_decode_locker4(argp, lock);
}

static __be32
nfsd4_decode_lockt(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_lockt *lockt = &u->lockt;
	memset(lockt, 0, sizeof(*lockt));
	if (xdr_stream_decode_u32(argp->xdr, &lockt->lt_type) < 0)
		return nfserr_bad_xdr;
	if ((lockt->lt_type < NFS4_READ_LT) || (lockt->lt_type > NFS4_WRITEW_LT))
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &lockt->lt_offset) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &lockt->lt_length) < 0)
		return nfserr_bad_xdr;
	return nfsd4_decode_state_owner4(argp, &lockt->lt_clientid,
					 &lockt->lt_owner);
}

static __be32
nfsd4_decode_locku(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_locku *locku = &u->locku;
	__be32 status;

	if (xdr_stream_decode_u32(argp->xdr, &locku->lu_type) < 0)
		return nfserr_bad_xdr;
	if ((locku->lu_type < NFS4_READ_LT) || (locku->lu_type > NFS4_WRITEW_LT))
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &locku->lu_seqid) < 0)
		return nfserr_bad_xdr;
	status = nfsd4_decode_stateid4(argp, &locku->lu_stateid);
	if (status)
		return status;
	if (xdr_stream_decode_u64(argp->xdr, &locku->lu_offset) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &locku->lu_length) < 0)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_lookup(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_lookup *lookup = &u->lookup;
	return nfsd4_decode_component4(argp, &lookup->lo_name, &lookup->lo_len);
}

static __be32
nfsd4_decode_createhow4(struct nfsd4_compoundargs *argp, struct nfsd4_open *open)
{
	__be32 status;

	if (xdr_stream_decode_u32(argp->xdr, &open->op_createmode) < 0)
		return nfserr_bad_xdr;
	switch (open->op_createmode) {
	case NFS4_CREATE_UNCHECKED:
	case NFS4_CREATE_GUARDED:
		status = nfsd4_decode_fattr4(argp, open->op_bmval,
					     ARRAY_SIZE(open->op_bmval),
					     &open->op_iattr, &open->op_acl,
					     &open->op_label, &open->op_umask);
		if (status)
			return status;
		break;
	case NFS4_CREATE_EXCLUSIVE:
		status = nfsd4_decode_verifier4(argp, &open->op_verf);
		if (status)
			return status;
		break;
	case NFS4_CREATE_EXCLUSIVE4_1:
		if (argp->minorversion < 1)
			return nfserr_bad_xdr;
		status = nfsd4_decode_verifier4(argp, &open->op_verf);
		if (status)
			return status;
		status = nfsd4_decode_fattr4(argp, open->op_bmval,
					     ARRAY_SIZE(open->op_bmval),
					     &open->op_iattr, &open->op_acl,
					     &open->op_label, &open->op_umask);
		if (status)
			return status;
		break;
	default:
		return nfserr_bad_xdr;
	}

	return nfs_ok;
}

static __be32
nfsd4_decode_openflag4(struct nfsd4_compoundargs *argp, struct nfsd4_open *open)
{
	__be32 status;

	if (xdr_stream_decode_u32(argp->xdr, &open->op_create) < 0)
		return nfserr_bad_xdr;
	switch (open->op_create) {
	case NFS4_OPEN_NOCREATE:
		break;
	case NFS4_OPEN_CREATE:
		status = nfsd4_decode_createhow4(argp, open);
		if (status)
			return status;
		break;
	default:
		return nfserr_bad_xdr;
	}

	return nfs_ok;
}

static __be32 nfsd4_decode_share_access(struct nfsd4_compoundargs *argp, u32 *share_access, u32 *deleg_want, u32 *deleg_when)
{
	u32 w;

	if (xdr_stream_decode_u32(argp->xdr, &w) < 0)
		return nfserr_bad_xdr;
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
	switch (w & NFS4_SHARE_WANT_TYPE_MASK) {
	case OPEN4_SHARE_ACCESS_WANT_NO_PREFERENCE:
	case OPEN4_SHARE_ACCESS_WANT_READ_DELEG:
	case OPEN4_SHARE_ACCESS_WANT_WRITE_DELEG:
	case OPEN4_SHARE_ACCESS_WANT_ANY_DELEG:
	case OPEN4_SHARE_ACCESS_WANT_NO_DELEG:
	case OPEN4_SHARE_ACCESS_WANT_CANCEL:
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
	return nfserr_bad_xdr;
}

static __be32 nfsd4_decode_share_deny(struct nfsd4_compoundargs *argp, u32 *x)
{
	if (xdr_stream_decode_u32(argp->xdr, x) < 0)
		return nfserr_bad_xdr;
	/* Note: unlike access bits, deny bits may be zero. */
	if (*x & ~NFS4_SHARE_DENY_BOTH)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_open_claim4(struct nfsd4_compoundargs *argp,
			 struct nfsd4_open *open)
{
	__be32 status;

	if (xdr_stream_decode_u32(argp->xdr, &open->op_claim_type) < 0)
		return nfserr_bad_xdr;
	switch (open->op_claim_type) {
	case NFS4_OPEN_CLAIM_NULL:
	case NFS4_OPEN_CLAIM_DELEGATE_PREV:
		status = nfsd4_decode_component4(argp, &open->op_fname,
						 &open->op_fnamelen);
		if (status)
			return status;
		break;
	case NFS4_OPEN_CLAIM_PREVIOUS:
		if (xdr_stream_decode_u32(argp->xdr, &open->op_delegate_type) < 0)
			return nfserr_bad_xdr;
		break;
	case NFS4_OPEN_CLAIM_DELEGATE_CUR:
		status = nfsd4_decode_stateid4(argp, &open->op_delegate_stateid);
		if (status)
			return status;
		status = nfsd4_decode_component4(argp, &open->op_fname,
						 &open->op_fnamelen);
		if (status)
			return status;
		break;
	case NFS4_OPEN_CLAIM_FH:
	case NFS4_OPEN_CLAIM_DELEG_PREV_FH:
		if (argp->minorversion < 1)
			return nfserr_bad_xdr;
		/* void */
		break;
	case NFS4_OPEN_CLAIM_DELEG_CUR_FH:
		if (argp->minorversion < 1)
			return nfserr_bad_xdr;
		status = nfsd4_decode_stateid4(argp, &open->op_delegate_stateid);
		if (status)
			return status;
		break;
	default:
		return nfserr_bad_xdr;
	}

	return nfs_ok;
}

static __be32
nfsd4_decode_open(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_open *open = &u->open;
	__be32 status;
	u32 dummy;

	memset(open, 0, sizeof(*open));

	if (xdr_stream_decode_u32(argp->xdr, &open->op_seqid) < 0)
		return nfserr_bad_xdr;
	/* deleg_want is ignored */
	status = nfsd4_decode_share_access(argp, &open->op_share_access,
					   &open->op_deleg_want, &dummy);
	if (status)
		return status;
	status = nfsd4_decode_share_deny(argp, &open->op_share_deny);
	if (status)
		return status;
	status = nfsd4_decode_state_owner4(argp, &open->op_clientid,
					   &open->op_owner);
	if (status)
		return status;
	status = nfsd4_decode_openflag4(argp, open);
	if (status)
		return status;
	return nfsd4_decode_open_claim4(argp, open);
}

static __be32
nfsd4_decode_open_confirm(struct nfsd4_compoundargs *argp,
			  union nfsd4_op_u *u)
{
	struct nfsd4_open_confirm *open_conf = &u->open_confirm;
	__be32 status;

	if (argp->minorversion >= 1)
		return nfserr_notsupp;

	status = nfsd4_decode_stateid4(argp, &open_conf->oc_req_stateid);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &open_conf->oc_seqid) < 0)
		return nfserr_bad_xdr;

	memset(&open_conf->oc_resp_stateid, 0,
	       sizeof(open_conf->oc_resp_stateid));
	return nfs_ok;
}

static __be32
nfsd4_decode_open_downgrade(struct nfsd4_compoundargs *argp,
			    union nfsd4_op_u *u)
{
	struct nfsd4_open_downgrade *open_down = &u->open_downgrade;
	__be32 status;

	memset(open_down, 0, sizeof(*open_down));
	status = nfsd4_decode_stateid4(argp, &open_down->od_stateid);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &open_down->od_seqid) < 0)
		return nfserr_bad_xdr;
	/* deleg_want is ignored */
	status = nfsd4_decode_share_access(argp, &open_down->od_share_access,
					   &open_down->od_deleg_want, NULL);
	if (status)
		return status;
	return nfsd4_decode_share_deny(argp, &open_down->od_share_deny);
}

static __be32
nfsd4_decode_putfh(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_putfh *putfh = &u->putfh;
	__be32 *p;

	if (xdr_stream_decode_u32(argp->xdr, &putfh->pf_fhlen) < 0)
		return nfserr_bad_xdr;
	if (putfh->pf_fhlen > NFS4_FHSIZE)
		return nfserr_bad_xdr;
	p = xdr_inline_decode(argp->xdr, putfh->pf_fhlen);
	if (!p)
		return nfserr_bad_xdr;
	putfh->pf_fhval = svcxdr_savemem(argp, p, putfh->pf_fhlen);
	if (!putfh->pf_fhval)
		return nfserr_jukebox;

	putfh->no_verify = false;
	return nfs_ok;
}

static __be32
nfsd4_decode_read(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_read *read = &u->read;
	__be32 status;

	memset(read, 0, sizeof(*read));
	status = nfsd4_decode_stateid4(argp, &read->rd_stateid);
	if (status)
		return status;
	if (xdr_stream_decode_u64(argp->xdr, &read->rd_offset) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &read->rd_length) < 0)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_readdir(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_readdir *readdir = &u->readdir;
	__be32 status;

	memset(readdir, 0, sizeof(*readdir));
	if (xdr_stream_decode_u64(argp->xdr, &readdir->rd_cookie) < 0)
		return nfserr_bad_xdr;
	status = nfsd4_decode_verifier4(argp, &readdir->rd_verf);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &readdir->rd_dircount) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &readdir->rd_maxcount) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_uint32_array(argp->xdr, readdir->rd_bmval,
					   ARRAY_SIZE(readdir->rd_bmval)) < 0)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_remove(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_remove *remove = &u->remove;
	memset(&remove->rm_cinfo, 0, sizeof(remove->rm_cinfo));
	return nfsd4_decode_component4(argp, &remove->rm_name, &remove->rm_namelen);
}

static __be32
nfsd4_decode_rename(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_rename *rename = &u->rename;
	__be32 status;

	memset(rename, 0, sizeof(*rename));
	status = nfsd4_decode_component4(argp, &rename->rn_sname, &rename->rn_snamelen);
	if (status)
		return status;
	return nfsd4_decode_component4(argp, &rename->rn_tname, &rename->rn_tnamelen);
}

static __be32
nfsd4_decode_renew(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	clientid_t *clientid = &u->renew;
	return nfsd4_decode_clientid4(argp, clientid);
}

static __be32
nfsd4_decode_secinfo(struct nfsd4_compoundargs *argp,
		     union nfsd4_op_u *u)
{
	struct nfsd4_secinfo *secinfo = &u->secinfo;
	secinfo->si_exp = NULL;
	return nfsd4_decode_component4(argp, &secinfo->si_name, &secinfo->si_namelen);
}

static __be32
nfsd4_decode_setattr(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_setattr *setattr = &u->setattr;
	__be32 status;

	memset(setattr, 0, sizeof(*setattr));
	status = nfsd4_decode_stateid4(argp, &setattr->sa_stateid);
	if (status)
		return status;
	return nfsd4_decode_fattr4(argp, setattr->sa_bmval,
				   ARRAY_SIZE(setattr->sa_bmval),
				   &setattr->sa_iattr, &setattr->sa_acl,
				   &setattr->sa_label, NULL);
}

static __be32
nfsd4_decode_setclientid(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_setclientid *setclientid = &u->setclientid;
	__be32 *p, status;

	memset(setclientid, 0, sizeof(*setclientid));

	if (argp->minorversion >= 1)
		return nfserr_notsupp;

	status = nfsd4_decode_verifier4(argp, &setclientid->se_verf);
	if (status)
		return status;
	status = nfsd4_decode_opaque(argp, &setclientid->se_name);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &setclientid->se_callback_prog) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &setclientid->se_callback_netid_len) < 0)
		return nfserr_bad_xdr;
	p = xdr_inline_decode(argp->xdr, setclientid->se_callback_netid_len);
	if (!p)
		return nfserr_bad_xdr;
	setclientid->se_callback_netid_val = svcxdr_savemem(argp, p,
						setclientid->se_callback_netid_len);
	if (!setclientid->se_callback_netid_val)
		return nfserr_jukebox;

	if (xdr_stream_decode_u32(argp->xdr, &setclientid->se_callback_addr_len) < 0)
		return nfserr_bad_xdr;
	p = xdr_inline_decode(argp->xdr, setclientid->se_callback_addr_len);
	if (!p)
		return nfserr_bad_xdr;
	setclientid->se_callback_addr_val = svcxdr_savemem(argp, p,
						setclientid->se_callback_addr_len);
	if (!setclientid->se_callback_addr_val)
		return nfserr_jukebox;
	if (xdr_stream_decode_u32(argp->xdr, &setclientid->se_callback_ident) < 0)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_setclientid_confirm(struct nfsd4_compoundargs *argp,
				 union nfsd4_op_u *u)
{
	struct nfsd4_setclientid_confirm *scd_c = &u->setclientid_confirm;
	__be32 status;

	if (argp->minorversion >= 1)
		return nfserr_notsupp;

	status = nfsd4_decode_clientid4(argp, &scd_c->sc_clientid);
	if (status)
		return status;
	return nfsd4_decode_verifier4(argp, &scd_c->sc_confirm);
}

/* Also used for NVERIFY */
static __be32
nfsd4_decode_verify(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_verify *verify = &u->verify;
	__be32 *p, status;

	memset(verify, 0, sizeof(*verify));

	status = nfsd4_decode_bitmap4(argp, verify->ve_bmval,
				      ARRAY_SIZE(verify->ve_bmval));
	if (status)
		return status;

	/* For convenience's sake, we compare raw xdr'd attributes in
	 * nfsd4_proc_verify */

	if (xdr_stream_decode_u32(argp->xdr, &verify->ve_attrlen) < 0)
		return nfserr_bad_xdr;
	p = xdr_inline_decode(argp->xdr, verify->ve_attrlen);
	if (!p)
		return nfserr_bad_xdr;
	verify->ve_attrval = svcxdr_savemem(argp, p, verify->ve_attrlen);
	if (!verify->ve_attrval)
		return nfserr_jukebox;

	return nfs_ok;
}

static __be32
nfsd4_decode_write(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_write *write = &u->write;
	__be32 status;

	status = nfsd4_decode_stateid4(argp, &write->wr_stateid);
	if (status)
		return status;
	if (xdr_stream_decode_u64(argp->xdr, &write->wr_offset) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &write->wr_stable_how) < 0)
		return nfserr_bad_xdr;
	if (write->wr_stable_how > NFS_FILE_SYNC)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &write->wr_buflen) < 0)
		return nfserr_bad_xdr;
	if (!xdr_stream_subsegment(argp->xdr, &write->wr_payload, write->wr_buflen))
		return nfserr_bad_xdr;

	write->wr_bytes_written = 0;
	write->wr_how_written = 0;
	memset(&write->wr_verifier, 0, sizeof(write->wr_verifier));
	return nfs_ok;
}

static __be32
nfsd4_decode_release_lockowner(struct nfsd4_compoundargs *argp,
			       union nfsd4_op_u *u)
{
	struct nfsd4_release_lockowner *rlockowner = &u->release_lockowner;
	__be32 status;

	if (argp->minorversion >= 1)
		return nfserr_notsupp;

	status = nfsd4_decode_state_owner4(argp, &rlockowner->rl_clientid,
					   &rlockowner->rl_owner);
	if (status)
		return status;

	if (argp->minorversion && !zero_clientid(&rlockowner->rl_clientid))
		return nfserr_inval;

	return nfs_ok;
}

static __be32 nfsd4_decode_backchannel_ctl(struct nfsd4_compoundargs *argp,
					   union nfsd4_op_u *u)
{
	struct nfsd4_backchannel_ctl *bc = &u->backchannel_ctl;
	memset(bc, 0, sizeof(*bc));
	if (xdr_stream_decode_u32(argp->xdr, &bc->bc_cb_program) < 0)
		return nfserr_bad_xdr;
	return nfsd4_decode_cb_sec(argp, &bc->bc_cb_sec);
}

static __be32 nfsd4_decode_bind_conn_to_session(struct nfsd4_compoundargs *argp,
						union nfsd4_op_u *u)
{
	struct nfsd4_bind_conn_to_session *bcts = &u->bind_conn_to_session;
	u32 use_conn_in_rdma_mode;
	__be32 status;

	memset(bcts, 0, sizeof(*bcts));
	status = nfsd4_decode_sessionid4(argp, &bcts->sessionid);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &bcts->dir) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &use_conn_in_rdma_mode) < 0)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_state_protect_ops(struct nfsd4_compoundargs *argp,
			       struct nfsd4_exchange_id *exid)
{
	__be32 status;

	status = nfsd4_decode_bitmap4(argp, exid->spo_must_enforce,
				      ARRAY_SIZE(exid->spo_must_enforce));
	if (status)
		return nfserr_bad_xdr;
	status = nfsd4_decode_bitmap4(argp, exid->spo_must_allow,
				      ARRAY_SIZE(exid->spo_must_allow));
	if (status)
		return nfserr_bad_xdr;

	return nfs_ok;
}

/*
 * This implementation currently does not support SP4_SSV.
 * This decoder simply skips over these arguments.
 */
static noinline __be32
nfsd4_decode_ssv_sp_parms(struct nfsd4_compoundargs *argp,
			  struct nfsd4_exchange_id *exid)
{
	u32 count, window, num_gss_handles;
	__be32 status;

	/* ssp_ops */
	status = nfsd4_decode_state_protect_ops(argp, exid);
	if (status)
		return status;

	/* ssp_hash_algs<> */
	if (xdr_stream_decode_u32(argp->xdr, &count) < 0)
		return nfserr_bad_xdr;
	while (count--) {
		status = nfsd4_decode_ignored_string(argp, 0);
		if (status)
			return status;
	}

	/* ssp_encr_algs<> */
	if (xdr_stream_decode_u32(argp->xdr, &count) < 0)
		return nfserr_bad_xdr;
	while (count--) {
		status = nfsd4_decode_ignored_string(argp, 0);
		if (status)
			return status;
	}

	if (xdr_stream_decode_u32(argp->xdr, &window) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &num_gss_handles) < 0)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_state_protect4_a(struct nfsd4_compoundargs *argp,
			      struct nfsd4_exchange_id *exid)
{
	__be32 status;

	if (xdr_stream_decode_u32(argp->xdr, &exid->spa_how) < 0)
		return nfserr_bad_xdr;
	switch (exid->spa_how) {
	case SP4_NONE:
		break;
	case SP4_MACH_CRED:
		status = nfsd4_decode_state_protect_ops(argp, exid);
		if (status)
			return status;
		break;
	case SP4_SSV:
		status = nfsd4_decode_ssv_sp_parms(argp, exid);
		if (status)
			return status;
		break;
	default:
		return nfserr_bad_xdr;
	}

	return nfs_ok;
}

static __be32
nfsd4_decode_nfs_impl_id4(struct nfsd4_compoundargs *argp,
			  struct nfsd4_exchange_id *exid)
{
	__be32 status;
	u32 count;

	if (xdr_stream_decode_u32(argp->xdr, &count) < 0)
		return nfserr_bad_xdr;
	switch (count) {
	case 0:
		break;
	case 1:
		/* Note that RFC 8881 places no length limit on
		 * nii_domain, but this implementation permits no
		 * more than NFS4_OPAQUE_LIMIT bytes */
		status = nfsd4_decode_opaque(argp, &exid->nii_domain);
		if (status)
			return status;
		/* Note that RFC 8881 places no length limit on
		 * nii_name, but this implementation permits no
		 * more than NFS4_OPAQUE_LIMIT bytes */
		status = nfsd4_decode_opaque(argp, &exid->nii_name);
		if (status)
			return status;
		status = nfsd4_decode_nfstime4(argp, &exid->nii_time);
		if (status)
			return status;
		break;
	default:
		return nfserr_bad_xdr;
	}

	return nfs_ok;
}

static __be32
nfsd4_decode_exchange_id(struct nfsd4_compoundargs *argp,
			 union nfsd4_op_u *u)
{
	struct nfsd4_exchange_id *exid = &u->exchange_id;
	__be32 status;

	memset(exid, 0, sizeof(*exid));
	status = nfsd4_decode_verifier4(argp, &exid->verifier);
	if (status)
		return status;
	status = nfsd4_decode_opaque(argp, &exid->clname);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &exid->flags) < 0)
		return nfserr_bad_xdr;
	status = nfsd4_decode_state_protect4_a(argp, exid);
	if (status)
		return status;
	return nfsd4_decode_nfs_impl_id4(argp, exid);
}

static __be32
nfsd4_decode_channel_attrs4(struct nfsd4_compoundargs *argp,
			    struct nfsd4_channel_attrs *ca)
{
	__be32 *p;

	p = xdr_inline_decode(argp->xdr, XDR_UNIT * 7);
	if (!p)
		return nfserr_bad_xdr;

	/* headerpadsz is ignored */
	p++;
	ca->maxreq_sz = be32_to_cpup(p++);
	ca->maxresp_sz = be32_to_cpup(p++);
	ca->maxresp_cached = be32_to_cpup(p++);
	ca->maxops = be32_to_cpup(p++);
	ca->maxreqs = be32_to_cpup(p++);
	ca->nr_rdma_attrs = be32_to_cpup(p);
	switch (ca->nr_rdma_attrs) {
	case 0:
		break;
	case 1:
		if (xdr_stream_decode_u32(argp->xdr, &ca->rdma_attrs) < 0)
			return nfserr_bad_xdr;
		break;
	default:
		return nfserr_bad_xdr;
	}

	return nfs_ok;
}

static __be32
nfsd4_decode_create_session(struct nfsd4_compoundargs *argp,
			    union nfsd4_op_u *u)
{
	struct nfsd4_create_session *sess = &u->create_session;
	__be32 status;

	memset(sess, 0, sizeof(*sess));
	status = nfsd4_decode_clientid4(argp, &sess->clientid);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &sess->seqid) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &sess->flags) < 0)
		return nfserr_bad_xdr;
	status = nfsd4_decode_channel_attrs4(argp, &sess->fore_channel);
	if (status)
		return status;
	status = nfsd4_decode_channel_attrs4(argp, &sess->back_channel);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &sess->callback_prog) < 0)
		return nfserr_bad_xdr;
	return nfsd4_decode_cb_sec(argp, &sess->cb_sec);
}

static __be32
nfsd4_decode_destroy_session(struct nfsd4_compoundargs *argp,
			     union nfsd4_op_u *u)
{
	struct nfsd4_destroy_session *destroy_session = &u->destroy_session;
	return nfsd4_decode_sessionid4(argp, &destroy_session->sessionid);
}

static __be32
nfsd4_decode_free_stateid(struct nfsd4_compoundargs *argp,
			  union nfsd4_op_u *u)
{
	struct nfsd4_free_stateid *free_stateid = &u->free_stateid;
	return nfsd4_decode_stateid4(argp, &free_stateid->fr_stateid);
}

static __be32
nfsd4_decode_get_dir_delegation(struct nfsd4_compoundargs *argp,
		union nfsd4_op_u *u)
{
	struct nfsd4_get_dir_delegation *gdd = &u->get_dir_delegation;
	__be32 status;

	memset(gdd, 0, sizeof(*gdd));

	if (xdr_stream_decode_bool(argp->xdr, &gdd->gdda_signal_deleg_avail) < 0)
		return nfserr_bad_xdr;
	status = nfsd4_decode_bitmap4(argp, gdd->gdda_notification_types,
				      ARRAY_SIZE(gdd->gdda_notification_types));
	if (status)
		return status;
	status = nfsd4_decode_nfstime4(argp, &gdd->gdda_child_attr_delay);
	if (status)
		return status;
	status = nfsd4_decode_nfstime4(argp, &gdd->gdda_dir_attr_delay);
	if (status)
		return status;
	status = nfsd4_decode_bitmap4(argp, gdd->gdda_child_attributes,
					ARRAY_SIZE(gdd->gdda_child_attributes));
	if (status)
		return status;
	return nfsd4_decode_bitmap4(argp, gdd->gdda_dir_attributes,
					ARRAY_SIZE(gdd->gdda_dir_attributes));
}

#ifdef CONFIG_NFSD_PNFS
static __be32
nfsd4_decode_getdeviceinfo(struct nfsd4_compoundargs *argp,
		union nfsd4_op_u *u)
{
	struct nfsd4_getdeviceinfo *gdev = &u->getdeviceinfo;
	__be32 status;

	memset(gdev, 0, sizeof(*gdev));
	status = nfsd4_decode_deviceid4(argp, &gdev->gd_devid);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &gdev->gd_layout_type) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &gdev->gd_maxcount) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_uint32_array(argp->xdr,
					   &gdev->gd_notify_types, 1) < 0)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_layoutcommit(struct nfsd4_compoundargs *argp,
			  union nfsd4_op_u *u)
{
	struct nfsd4_layoutcommit *lcp = &u->layoutcommit;
	__be32 *p, status;

	memset(lcp, 0, sizeof(*lcp));
	if (xdr_stream_decode_u64(argp->xdr, &lcp->lc_seg.offset) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &lcp->lc_seg.length) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_bool(argp->xdr, &lcp->lc_reclaim) < 0)
		return nfserr_bad_xdr;
	status = nfsd4_decode_stateid4(argp, &lcp->lc_sid);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &lcp->lc_newoffset) < 0)
		return nfserr_bad_xdr;
	if (lcp->lc_newoffset) {
		if (xdr_stream_decode_u64(argp->xdr, &lcp->lc_last_wr) < 0)
			return nfserr_bad_xdr;
	} else
		lcp->lc_last_wr = 0;
	p = xdr_inline_decode(argp->xdr, XDR_UNIT);
	if (!p)
		return nfserr_bad_xdr;
	if (xdr_item_is_present(p)) {
		status = nfsd4_decode_nfstime4(argp, &lcp->lc_mtime);
		if (status)
			return status;
	} else {
		lcp->lc_mtime.tv_nsec = UTIME_NOW;
	}
	return nfsd4_decode_layoutupdate4(argp, lcp);
}

static __be32
nfsd4_decode_layoutget(struct nfsd4_compoundargs *argp,
		union nfsd4_op_u *u)
{
	struct nfsd4_layoutget *lgp = &u->layoutget;
	__be32 status;

	memset(lgp, 0, sizeof(*lgp));
	if (xdr_stream_decode_u32(argp->xdr, &lgp->lg_signal) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &lgp->lg_layout_type) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &lgp->lg_seg.iomode) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &lgp->lg_seg.offset) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &lgp->lg_seg.length) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &lgp->lg_minlength) < 0)
		return nfserr_bad_xdr;
	status = nfsd4_decode_stateid4(argp, &lgp->lg_sid);
	if (status)
		return status;
	if (xdr_stream_decode_u32(argp->xdr, &lgp->lg_maxcount) < 0)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32
nfsd4_decode_layoutreturn(struct nfsd4_compoundargs *argp,
		union nfsd4_op_u *u)
{
	struct nfsd4_layoutreturn *lrp = &u->layoutreturn;
	memset(lrp, 0, sizeof(*lrp));
	if (xdr_stream_decode_bool(argp->xdr, &lrp->lr_reclaim) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &lrp->lr_layout_type) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &lrp->lr_seg.iomode) < 0)
		return nfserr_bad_xdr;
	return nfsd4_decode_layoutreturn4(argp, lrp);
}
#endif /* CONFIG_NFSD_PNFS */

static __be32 nfsd4_decode_secinfo_no_name(struct nfsd4_compoundargs *argp,
					   union nfsd4_op_u *u)
{
	struct nfsd4_secinfo_no_name *sin = &u->secinfo_no_name;
	if (xdr_stream_decode_u32(argp->xdr, &sin->sin_style) < 0)
		return nfserr_bad_xdr;

	sin->sin_exp = NULL;
	return nfs_ok;
}

static __be32
nfsd4_decode_sequence(struct nfsd4_compoundargs *argp,
		      union nfsd4_op_u *u)
{
	struct nfsd4_sequence *seq = &u->sequence;
	__be32 *p, status;

	status = nfsd4_decode_sessionid4(argp, &seq->sessionid);
	if (status)
		return status;
	p = xdr_inline_decode(argp->xdr, XDR_UNIT * 4);
	if (!p)
		return nfserr_bad_xdr;
	seq->seqid = be32_to_cpup(p++);
	seq->slotid = be32_to_cpup(p++);
	/* sa_highest_slotid counts from 0 but maxslots  counts from 1 ... */
	seq->maxslots = be32_to_cpup(p++) + 1;
	seq->cachethis = be32_to_cpup(p);

	seq->status_flags = 0;
	return nfs_ok;
}

static __be32
nfsd4_decode_test_stateid(struct nfsd4_compoundargs *argp,
			  union nfsd4_op_u *u)
{
	struct nfsd4_test_stateid *test_stateid = &u->test_stateid;
	struct nfsd4_test_stateid_id *stateid;
	__be32 status;
	u32 i;

	memset(test_stateid, 0, sizeof(*test_stateid));
	if (xdr_stream_decode_u32(argp->xdr, &test_stateid->ts_num_ids) < 0)
		return nfserr_bad_xdr;

	INIT_LIST_HEAD(&test_stateid->ts_stateid_list);
	for (i = 0; i < test_stateid->ts_num_ids; i++) {
		stateid = svcxdr_tmpalloc(argp, sizeof(*stateid));
		if (!stateid)
			return nfserr_jukebox;
		INIT_LIST_HEAD(&stateid->ts_id_list);
		list_add_tail(&stateid->ts_id_list, &test_stateid->ts_stateid_list);
		status = nfsd4_decode_stateid4(argp, &stateid->ts_id_stateid);
		if (status)
			return status;
	}

	return nfs_ok;
}

static __be32 nfsd4_decode_destroy_clientid(struct nfsd4_compoundargs *argp,
					    union nfsd4_op_u *u)
{
	struct nfsd4_destroy_clientid *dc = &u->destroy_clientid;
	return nfsd4_decode_clientid4(argp, &dc->clientid);
}

static __be32 nfsd4_decode_reclaim_complete(struct nfsd4_compoundargs *argp,
					    union nfsd4_op_u *u)
{
	struct nfsd4_reclaim_complete *rc = &u->reclaim_complete;
	if (xdr_stream_decode_bool(argp->xdr, &rc->rca_one_fs) < 0)
		return nfserr_bad_xdr;
	return nfs_ok;
}

static __be32
nfsd4_decode_fallocate(struct nfsd4_compoundargs *argp,
		       union nfsd4_op_u *u)
{
	struct nfsd4_fallocate *fallocate = &u->allocate;
	__be32 status;

	status = nfsd4_decode_stateid4(argp, &fallocate->falloc_stateid);
	if (status)
		return status;
	if (xdr_stream_decode_u64(argp->xdr, &fallocate->falloc_offset) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &fallocate->falloc_length) < 0)
		return nfserr_bad_xdr;

	return nfs_ok;
}

static __be32 nfsd4_decode_nl4_server(struct nfsd4_compoundargs *argp,
				      struct nl4_server *ns)
{
	struct nfs42_netaddr *naddr;
	__be32 *p;

	if (xdr_stream_decode_u32(argp->xdr, &ns->nl4_type) < 0)
		return nfserr_bad_xdr;

	/* currently support for 1 inter-server source server */
	switch (ns->nl4_type) {
	case NL4_NETADDR:
		naddr = &ns->u.nl4_addr;

		if (xdr_stream_decode_u32(argp->xdr, &naddr->netid_len) < 0)
			return nfserr_bad_xdr;
		if (naddr->netid_len > RPCBIND_MAXNETIDLEN)
			return nfserr_bad_xdr;

		p = xdr_inline_decode(argp->xdr, naddr->netid_len);
		if (!p)
			return nfserr_bad_xdr;
		memcpy(naddr->netid, p, naddr->netid_len);

		if (xdr_stream_decode_u32(argp->xdr, &naddr->addr_len) < 0)
			return nfserr_bad_xdr;
		if (naddr->addr_len > RPCBIND_MAXUADDRLEN)
			return nfserr_bad_xdr;

		p = xdr_inline_decode(argp->xdr, naddr->addr_len);
		if (!p)
			return nfserr_bad_xdr;
		memcpy(naddr->addr, p, naddr->addr_len);
		break;
	default:
		return nfserr_bad_xdr;
	}

	return nfs_ok;
}

static __be32
nfsd4_decode_copy(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_copy *copy = &u->copy;
	u32 consecutive, i, count, sync;
	struct nl4_server *ns_dummy;
	__be32 status;

	memset(copy, 0, sizeof(*copy));
	status = nfsd4_decode_stateid4(argp, &copy->cp_src_stateid);
	if (status)
		return status;
	status = nfsd4_decode_stateid4(argp, &copy->cp_dst_stateid);
	if (status)
		return status;
	if (xdr_stream_decode_u64(argp->xdr, &copy->cp_src_pos) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &copy->cp_dst_pos) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &copy->cp_count) < 0)
		return nfserr_bad_xdr;
	/* ca_consecutive: we always do consecutive copies */
	if (xdr_stream_decode_u32(argp->xdr, &consecutive) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_bool(argp->xdr, &sync) < 0)
		return nfserr_bad_xdr;
	nfsd4_copy_set_sync(copy, sync);

	if (xdr_stream_decode_u32(argp->xdr, &count) < 0)
		return nfserr_bad_xdr;
	copy->cp_src = svcxdr_tmpalloc(argp, sizeof(*copy->cp_src));
	if (copy->cp_src == NULL)
		return nfserr_jukebox;
	if (count == 0) { /* intra-server copy */
		__set_bit(NFSD4_COPY_F_INTRA, &copy->cp_flags);
		return nfs_ok;
	}

	/* decode all the supplied server addresses but use only the first */
	status = nfsd4_decode_nl4_server(argp, copy->cp_src);
	if (status)
		return status;

	ns_dummy = kmalloc(sizeof(struct nl4_server), GFP_KERNEL);
	if (ns_dummy == NULL)
		return nfserr_jukebox;
	for (i = 0; i < count - 1; i++) {
		status = nfsd4_decode_nl4_server(argp, ns_dummy);
		if (status) {
			kfree(ns_dummy);
			return status;
		}
	}
	kfree(ns_dummy);

	return nfs_ok;
}

static __be32
nfsd4_decode_copy_notify(struct nfsd4_compoundargs *argp,
			 union nfsd4_op_u *u)
{
	struct nfsd4_copy_notify *cn = &u->copy_notify;
	__be32 status;

	memset(cn, 0, sizeof(*cn));
	cn->cpn_src = svcxdr_tmpalloc(argp, sizeof(*cn->cpn_src));
	if (cn->cpn_src == NULL)
		return nfserr_jukebox;
	cn->cpn_dst = svcxdr_tmpalloc(argp, sizeof(*cn->cpn_dst));
	if (cn->cpn_dst == NULL)
		return nfserr_jukebox;

	status = nfsd4_decode_stateid4(argp, &cn->cpn_src_stateid);
	if (status)
		return status;
	return nfsd4_decode_nl4_server(argp, cn->cpn_dst);
}

static __be32
nfsd4_decode_offload_status(struct nfsd4_compoundargs *argp,
			    union nfsd4_op_u *u)
{
	struct nfsd4_offload_status *os = &u->offload_status;
	os->count = 0;
	os->status = 0;
	return nfsd4_decode_stateid4(argp, &os->stateid);
}

static __be32
nfsd4_decode_seek(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_seek *seek = &u->seek;
	__be32 status;

	status = nfsd4_decode_stateid4(argp, &seek->seek_stateid);
	if (status)
		return status;
	if (xdr_stream_decode_u64(argp->xdr, &seek->seek_offset) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u32(argp->xdr, &seek->seek_whence) < 0)
		return nfserr_bad_xdr;

	seek->seek_eof = 0;
	seek->seek_pos = 0;
	return nfs_ok;
}

static __be32
nfsd4_decode_clone(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u)
{
	struct nfsd4_clone *clone = &u->clone;
	__be32 status;

	status = nfsd4_decode_stateid4(argp, &clone->cl_src_stateid);
	if (status)
		return status;
	status = nfsd4_decode_stateid4(argp, &clone->cl_dst_stateid);
	if (status)
		return status;
	if (xdr_stream_decode_u64(argp->xdr, &clone->cl_src_pos) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &clone->cl_dst_pos) < 0)
		return nfserr_bad_xdr;
	if (xdr_stream_decode_u64(argp->xdr, &clone->cl_count) < 0)
		return nfserr_bad_xdr;

	return nfs_ok;
}

/*
 * XDR data that is more than PAGE_SIZE in size is normally part of a
 * read or write. However, the size of extended attributes is limited
 * by the maximum request size, and then further limited by the underlying
 * filesystem limits. This can exceed PAGE_SIZE (currently, XATTR_SIZE_MAX
 * is 64k). Since there is no kvec- or page-based interface to xattrs,
 * and we're not dealing with contiguous pages, we need to do some copying.
 */

/*
 * Decode data into buffer.
 */
static __be32
nfsd4_vbuf_from_vector(struct nfsd4_compoundargs *argp, struct xdr_buf *xdr,
		       char **bufp, size_t buflen)
{
	struct page **pages = xdr->pages;
	struct kvec *head = xdr->head;
	char *tmp, *dp;
	u32 len;

	if (buflen <= head->iov_len) {
		/*
		 * We're in luck, the head has enough space. Just return
		 * the head, no need for copying.
		 */
		*bufp = head->iov_base;
		return 0;
	}

	tmp = svcxdr_tmpalloc(argp, buflen);
	if (tmp == NULL)
		return nfserr_jukebox;

	dp = tmp;
	memcpy(dp, head->iov_base, head->iov_len);
	buflen -= head->iov_len;
	dp += head->iov_len;

	while (buflen > 0) {
		len = min_t(u32, buflen, PAGE_SIZE);
		memcpy(dp, page_address(*pages), len);

		buflen -= len;
		dp += len;
		pages++;
	}

	*bufp = tmp;
	return 0;
}

/*
 * Get a user extended attribute name from the XDR buffer.
 * It will not have the "user." prefix, so prepend it.
 * Lastly, check for nul characters in the name.
 */
static __be32
nfsd4_decode_xattr_name(struct nfsd4_compoundargs *argp, char **namep)
{
	char *name, *sp, *dp;
	u32 namelen, cnt;
	__be32 *p;

	if (xdr_stream_decode_u32(argp->xdr, &namelen) < 0)
		return nfserr_bad_xdr;
	if (namelen > (XATTR_NAME_MAX - XATTR_USER_PREFIX_LEN))
		return nfserr_nametoolong;
	if (namelen == 0)
		return nfserr_bad_xdr;
	p = xdr_inline_decode(argp->xdr, namelen);
	if (!p)
		return nfserr_bad_xdr;
	name = svcxdr_tmpalloc(argp, namelen + XATTR_USER_PREFIX_LEN + 1);
	if (!name)
		return nfserr_jukebox;
	memcpy(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN);

	/*
	 * Copy the extended attribute name over while checking for 0
	 * characters.
	 */
	sp = (char *)p;
	dp = name + XATTR_USER_PREFIX_LEN;
	cnt = namelen;

	while (cnt-- > 0) {
		if (*sp == '\0')
			return nfserr_bad_xdr;
		*dp++ = *sp++;
	}
	*dp = '\0';

	*namep = name;

	return nfs_ok;
}

/*
 * A GETXATTR op request comes without a length specifier. We just set the
 * maximum length for the reply based on XATTR_SIZE_MAX and the maximum
 * channel reply size. nfsd_getxattr will probe the length of the xattr,
 * check it against getxa_len, and allocate + return the value.
 */
static __be32
nfsd4_decode_getxattr(struct nfsd4_compoundargs *argp,
		      union nfsd4_op_u *u)
{
	struct nfsd4_getxattr *getxattr = &u->getxattr;
	__be32 status;
	u32 maxcount;

	memset(getxattr, 0, sizeof(*getxattr));
	status = nfsd4_decode_xattr_name(argp, &getxattr->getxa_name);
	if (status)
		return status;

	maxcount = svc_max_payload(argp->rqstp);
	maxcount = min_t(u32, XATTR_SIZE_MAX, maxcount);

	getxattr->getxa_len = maxcount;
	return nfs_ok;
}

static __be32
nfsd4_decode_setxattr(struct nfsd4_compoundargs *argp,
		      union nfsd4_op_u *u)
{
	struct nfsd4_setxattr *setxattr = &u->setxattr;
	u32 flags, maxcount, size;
	__be32 status;

	memset(setxattr, 0, sizeof(*setxattr));

	if (xdr_stream_decode_u32(argp->xdr, &flags) < 0)
		return nfserr_bad_xdr;

	if (flags > SETXATTR4_REPLACE)
		return nfserr_inval;
	setxattr->setxa_flags = flags;

	status = nfsd4_decode_xattr_name(argp, &setxattr->setxa_name);
	if (status)
		return status;

	maxcount = svc_max_payload(argp->rqstp);
	maxcount = min_t(u32, XATTR_SIZE_MAX, maxcount);

	if (xdr_stream_decode_u32(argp->xdr, &size) < 0)
		return nfserr_bad_xdr;
	if (size > maxcount)
		return nfserr_xattr2big;

	setxattr->setxa_len = size;
	if (size > 0) {
		struct xdr_buf payload;

		if (!xdr_stream_subsegment(argp->xdr, &payload, size))
			return nfserr_bad_xdr;
		status = nfsd4_vbuf_from_vector(argp, &payload,
						&setxattr->setxa_buf, size);
	}

	return nfs_ok;
}

static __be32
nfsd4_decode_listxattrs(struct nfsd4_compoundargs *argp,
			union nfsd4_op_u *u)
{
	struct nfsd4_listxattrs *listxattrs = &u->listxattrs;
	u32 maxcount;

	memset(listxattrs, 0, sizeof(*listxattrs));

	if (xdr_stream_decode_u64(argp->xdr, &listxattrs->lsxa_cookie) < 0)
		return nfserr_bad_xdr;

	/*
	 * If the cookie  is too large to have even one user.x attribute
	 * plus trailing '\0' left in a maximum size buffer, it's invalid.
	 */
	if (listxattrs->lsxa_cookie >=
	    (XATTR_LIST_MAX / (XATTR_USER_PREFIX_LEN + 2)))
		return nfserr_badcookie;

	if (xdr_stream_decode_u32(argp->xdr, &maxcount) < 0)
		return nfserr_bad_xdr;
	if (maxcount < 8)
		/* Always need at least 2 words (length and one character) */
		return nfserr_inval;

	maxcount = min(maxcount, svc_max_payload(argp->rqstp));
	listxattrs->lsxa_maxcount = maxcount;

	return nfs_ok;
}

static __be32
nfsd4_decode_removexattr(struct nfsd4_compoundargs *argp,
			 union nfsd4_op_u *u)
{
	struct nfsd4_removexattr *removexattr = &u->removexattr;
	memset(removexattr, 0, sizeof(*removexattr));
	return nfsd4_decode_xattr_name(argp, &removexattr->rmxa_name);
}

static __be32
nfsd4_decode_noop(struct nfsd4_compoundargs *argp, union nfsd4_op_u *p)
{
	return nfs_ok;
}

static __be32
nfsd4_decode_notsupp(struct nfsd4_compoundargs *argp, union nfsd4_op_u *p)
{
	return nfserr_notsupp;
}

typedef __be32(*nfsd4_dec)(struct nfsd4_compoundargs *argp, union nfsd4_op_u *u);

static const nfsd4_dec nfsd4_dec_ops[] = {
	[OP_ACCESS]		= nfsd4_decode_access,
	[OP_CLOSE]		= nfsd4_decode_close,
	[OP_COMMIT]		= nfsd4_decode_commit,
	[OP_CREATE]		= nfsd4_decode_create,
	[OP_DELEGPURGE]		= nfsd4_decode_notsupp,
	[OP_DELEGRETURN]	= nfsd4_decode_delegreturn,
	[OP_GETATTR]		= nfsd4_decode_getattr,
	[OP_GETFH]		= nfsd4_decode_noop,
	[OP_LINK]		= nfsd4_decode_link,
	[OP_LOCK]		= nfsd4_decode_lock,
	[OP_LOCKT]		= nfsd4_decode_lockt,
	[OP_LOCKU]		= nfsd4_decode_locku,
	[OP_LOOKUP]		= nfsd4_decode_lookup,
	[OP_LOOKUPP]		= nfsd4_decode_noop,
	[OP_NVERIFY]		= nfsd4_decode_verify,
	[OP_OPEN]		= nfsd4_decode_open,
	[OP_OPENATTR]		= nfsd4_decode_notsupp,
	[OP_OPEN_CONFIRM]	= nfsd4_decode_open_confirm,
	[OP_OPEN_DOWNGRADE]	= nfsd4_decode_open_downgrade,
	[OP_PUTFH]		= nfsd4_decode_putfh,
	[OP_PUTPUBFH]		= nfsd4_decode_noop,
	[OP_PUTROOTFH]		= nfsd4_decode_noop,
	[OP_READ]		= nfsd4_decode_read,
	[OP_READDIR]		= nfsd4_decode_readdir,
	[OP_READLINK]		= nfsd4_decode_noop,
	[OP_REMOVE]		= nfsd4_decode_remove,
	[OP_RENAME]		= nfsd4_decode_rename,
	[OP_RENEW]		= nfsd4_decode_renew,
	[OP_RESTOREFH]		= nfsd4_decode_noop,
	[OP_SAVEFH]		= nfsd4_decode_noop,
	[OP_SECINFO]		= nfsd4_decode_secinfo,
	[OP_SETATTR]		= nfsd4_decode_setattr,
	[OP_SETCLIENTID]	= nfsd4_decode_setclientid,
	[OP_SETCLIENTID_CONFIRM] = nfsd4_decode_setclientid_confirm,
	[OP_VERIFY]		= nfsd4_decode_verify,
	[OP_WRITE]		= nfsd4_decode_write,
	[OP_RELEASE_LOCKOWNER]	= nfsd4_decode_release_lockowner,

	/* new operations for NFSv4.1 */
	[OP_BACKCHANNEL_CTL]	= nfsd4_decode_backchannel_ctl,
	[OP_BIND_CONN_TO_SESSION] = nfsd4_decode_bind_conn_to_session,
	[OP_EXCHANGE_ID]	= nfsd4_decode_exchange_id,
	[OP_CREATE_SESSION]	= nfsd4_decode_create_session,
	[OP_DESTROY_SESSION]	= nfsd4_decode_destroy_session,
	[OP_FREE_STATEID]	= nfsd4_decode_free_stateid,
	[OP_GET_DIR_DELEGATION]	= nfsd4_decode_get_dir_delegation,
#ifdef CONFIG_NFSD_PNFS
	[OP_GETDEVICEINFO]	= nfsd4_decode_getdeviceinfo,
	[OP_GETDEVICELIST]	= nfsd4_decode_notsupp,
	[OP_LAYOUTCOMMIT]	= nfsd4_decode_layoutcommit,
	[OP_LAYOUTGET]		= nfsd4_decode_layoutget,
	[OP_LAYOUTRETURN]	= nfsd4_decode_layoutreturn,
#else
	[OP_GETDEVICEINFO]	= nfsd4_decode_notsupp,
	[OP_GETDEVICELIST]	= nfsd4_decode_notsupp,
	[OP_LAYOUTCOMMIT]	= nfsd4_decode_notsupp,
	[OP_LAYOUTGET]		= nfsd4_decode_notsupp,
	[OP_LAYOUTRETURN]	= nfsd4_decode_notsupp,
#endif
	[OP_SECINFO_NO_NAME]	= nfsd4_decode_secinfo_no_name,
	[OP_SEQUENCE]		= nfsd4_decode_sequence,
	[OP_SET_SSV]		= nfsd4_decode_notsupp,
	[OP_TEST_STATEID]	= nfsd4_decode_test_stateid,
	[OP_WANT_DELEGATION]	= nfsd4_decode_notsupp,
	[OP_DESTROY_CLIENTID]	= nfsd4_decode_destroy_clientid,
	[OP_RECLAIM_COMPLETE]	= nfsd4_decode_reclaim_complete,

	/* new operations for NFSv4.2 */
	[OP_ALLOCATE]		= nfsd4_decode_fallocate,
	[OP_COPY]		= nfsd4_decode_copy,
	[OP_COPY_NOTIFY]	= nfsd4_decode_copy_notify,
	[OP_DEALLOCATE]		= nfsd4_decode_fallocate,
	[OP_IO_ADVISE]		= nfsd4_decode_notsupp,
	[OP_LAYOUTERROR]	= nfsd4_decode_notsupp,
	[OP_LAYOUTSTATS]	= nfsd4_decode_notsupp,
	[OP_OFFLOAD_CANCEL]	= nfsd4_decode_offload_status,
	[OP_OFFLOAD_STATUS]	= nfsd4_decode_offload_status,
	[OP_READ_PLUS]		= nfsd4_decode_read,
	[OP_SEEK]		= nfsd4_decode_seek,
	[OP_WRITE_SAME]		= nfsd4_decode_notsupp,
	[OP_CLONE]		= nfsd4_decode_clone,
	/* RFC 8276 extended atributes operations */
	[OP_GETXATTR]		= nfsd4_decode_getxattr,
	[OP_SETXATTR]		= nfsd4_decode_setxattr,
	[OP_LISTXATTRS]		= nfsd4_decode_listxattrs,
	[OP_REMOVEXATTR]	= nfsd4_decode_removexattr,
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

static bool
nfsd4_decode_compound(struct nfsd4_compoundargs *argp)
{
	struct nfsd4_op *op;
	bool cachethis = false;
	int auth_slack= argp->rqstp->rq_auth_slack;
	int max_reply = auth_slack + 8; /* opcnt, status */
	int readcount = 0;
	int readbytes = 0;
	__be32 *p;
	int i;

	if (xdr_stream_decode_u32(argp->xdr, &argp->taglen) < 0)
		return false;
	max_reply += XDR_UNIT;
	argp->tag = NULL;
	if (unlikely(argp->taglen)) {
		if (argp->taglen > NFSD4_MAX_TAGLEN)
			return false;
		p = xdr_inline_decode(argp->xdr, argp->taglen);
		if (!p)
			return false;
		argp->tag = svcxdr_savemem(argp, p, argp->taglen);
		if (!argp->tag)
			return false;
		max_reply += xdr_align_size(argp->taglen);
	}

	if (xdr_stream_decode_u32(argp->xdr, &argp->minorversion) < 0)
		return false;
	if (xdr_stream_decode_u32(argp->xdr, &argp->client_opcnt) < 0)
		return false;
	argp->opcnt = min_t(u32, argp->client_opcnt,
			    NFSD_MAX_OPS_PER_COMPOUND);

	if (argp->opcnt > ARRAY_SIZE(argp->iops)) {
		argp->ops = vcalloc(argp->opcnt, sizeof(*argp->ops));
		if (!argp->ops) {
			argp->ops = argp->iops;
			return false;
		}
	}

	if (argp->minorversion > NFSD_SUPPORTED_MINOR_VERSION)
		argp->opcnt = 0;

	for (i = 0; i < argp->opcnt; i++) {
		op = &argp->ops[i];
		op->replay = NULL;
		op->opdesc = NULL;

		if (xdr_stream_decode_u32(argp->xdr, &op->opnum) < 0)
			return false;
		if (nfsd4_opnum_in_range(argp, op)) {
			op->opdesc = OPDESC(op);
			op->status = nfsd4_dec_ops[op->opnum](argp, &op->u);
			if (op->status != nfs_ok)
				trace_nfsd_compound_decode_err(argp->rqstp,
							       argp->opcnt, i,
							       op->opnum,
							       op->status);
		} else {
			op->opnum = OP_ILLEGAL;
			op->status = nfserr_op_illegal;
		}

		/*
		 * We'll try to cache the result in the DRC if any one
		 * op in the compound wants to be cached:
		 */
		cachethis |= nfsd4_cache_this_op(op);

		if (op->opnum == OP_READ || op->opnum == OP_READ_PLUS) {
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

	argp->splice_ok = nfsd_read_splice_ok(argp->rqstp);
	if (readcount > 1 || max_reply > PAGE_SIZE - auth_slack)
		argp->splice_ok = false;

	return true;
}

static __be32 nfsd4_encode_nfs_fh4(struct xdr_stream *xdr,
				   struct knfsd_fh *fh_handle)
{
	return nfsd4_encode_opaque(xdr, fh_handle->fh_raw, fh_handle->fh_size);
}

/* This is a frequently-encoded type; open-coded for speed */
static __be32 nfsd4_encode_nfstime4(struct xdr_stream *xdr,
				    const struct timespec64 *tv)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, XDR_UNIT * 3);
	if (!p)
		return nfserr_resource;
	p = xdr_encode_hyper(p, tv->tv_sec);
	*p = cpu_to_be32(tv->tv_nsec);
	return nfs_ok;
}

static __be32 nfsd4_encode_specdata4(struct xdr_stream *xdr,
				     unsigned int major, unsigned int minor)
{
	__be32 status;

	status = nfsd4_encode_uint32_t(xdr, major);
	if (status != nfs_ok)
		return status;
	return nfsd4_encode_uint32_t(xdr, minor);
}

static __be32
nfsd4_encode_change_info4(struct xdr_stream *xdr, const struct nfsd4_change_info *c)
{
	__be32 status;

	status = nfsd4_encode_bool(xdr, c->atomic);
	if (status != nfs_ok)
		return status;
	status = nfsd4_encode_changeid4(xdr, c->before_change);
	if (status != nfs_ok)
		return status;
	return nfsd4_encode_changeid4(xdr, c->after_change);
}

static __be32 nfsd4_encode_netaddr4(struct xdr_stream *xdr,
				    const struct nfs42_netaddr *addr)
{
	__be32 status;

	/* na_r_netid */
	status = nfsd4_encode_opaque(xdr, addr->netid, addr->netid_len);
	if (status != nfs_ok)
		return status;
	/* na_r_addr */
	return nfsd4_encode_opaque(xdr, addr->addr, addr->addr_len);
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
			if (xdr_stream_encode_opaque(xdr, str, strlen) < 0)
				return nfserr_resource;
			count++;
		} else
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
	return nfs_ok;
}

static __be32 nfsd4_encode_pathname4(struct xdr_stream *xdr,
				     const struct path *root,
				     const struct path *path)
{
	struct path cur = *path;
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
	if (xdr_stream_encode_u32(xdr, ncomponents) != XDR_UNIT)
		goto out_free;
	while (ncomponents) {
		struct dentry *dentry = components[ncomponents - 1];

		spin_lock(&dentry->d_lock);
		if (xdr_stream_encode_opaque(xdr, dentry->d_name.name,
					     dentry->d_name.len) < 0) {
			spin_unlock(&dentry->d_lock);
			goto out_free;
		}
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

static __be32 nfsd4_encode_fs_locations4(struct xdr_stream *xdr,
					 struct svc_rqst *rqstp,
					 struct svc_export *exp)
{
	struct nfsd4_fs_locations *fslocs = &exp->ex_fslocs;
	struct svc_export *exp_ps;
	unsigned int i;
	__be32 status;

	/* fs_root */
	exp_ps = rqst_find_fsidzero_export(rqstp);
	if (IS_ERR(exp_ps))
		return nfserrno(PTR_ERR(exp_ps));
	status = nfsd4_encode_pathname4(xdr, &exp_ps->ex_path, &exp->ex_path);
	exp_put(exp_ps);
	if (status != nfs_ok)
		return status;

	/* locations<> */
	if (xdr_stream_encode_u32(xdr, fslocs->locations_count) != XDR_UNIT)
		return nfserr_resource;
	for (i = 0; i < fslocs->locations_count; i++) {
		status = nfsd4_encode_fs_location4(xdr, &fslocs->locations[i]);
		if (status != nfs_ok)
			return status;
	}

	return nfs_ok;
}

static __be32 nfsd4_encode_nfsace4(struct xdr_stream *xdr, struct svc_rqst *rqstp,
				   struct nfs4_ace *ace)
{
	__be32 status;

	/* type */
	status = nfsd4_encode_acetype4(xdr, ace->type);
	if (status != nfs_ok)
		return nfserr_resource;
	/* flag */
	status = nfsd4_encode_aceflag4(xdr, ace->flag);
	if (status != nfs_ok)
		return nfserr_resource;
	/* access mask */
	status = nfsd4_encode_acemask4(xdr, ace->access_mask & NFS4_ACE_MASK_ALL);
	if (status != nfs_ok)
		return nfserr_resource;
	/* who */
	if (ace->whotype != NFS4_ACL_WHO_NAMED)
		return nfs4_acl_write_who(xdr, ace->whotype);
	if (ace->flag & NFS4_ACE_IDENTIFIER_GROUP)
		return nfsd4_encode_group(xdr, rqstp, ace->who_gid);
	return nfsd4_encode_user(xdr, rqstp, ace->who_uid);
}

#define WORD0_ABSENT_FS_ATTRS (FATTR4_WORD0_FS_LOCATIONS | FATTR4_WORD0_FSID | \
			      FATTR4_WORD0_RDATTR_ERROR)
#define WORD1_ABSENT_FS_ATTRS FATTR4_WORD1_MOUNTED_ON_FILEID
#define WORD2_ABSENT_FS_ATTRS 0

#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
static inline __be32
nfsd4_encode_security_label(struct xdr_stream *xdr, struct svc_rqst *rqstp,
			    const struct lsm_context *context)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, context->len + 4 + 4 + 4);
	if (!p)
		return nfserr_resource;

	/*
	 * For now we use a 0 here to indicate the null translation; in
	 * the future we may place a call to translation code here.
	 */
	*p++ = cpu_to_be32(0); /* lfs */
	*p++ = cpu_to_be32(0); /* pi */
	p = xdr_encode_opaque(p, context->context, context->len);
	return 0;
}
#else
static inline __be32
nfsd4_encode_security_label(struct xdr_stream *xdr, struct svc_rqst *rqstp,
			    struct lsm_context *context)
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


static int nfsd4_get_mounted_on_ino(struct svc_export *exp, u64 *pino)
{
	struct path path = exp->ex_path;
	struct kstat stat;
	int err;

	path_get(&path);
	while (follow_up(&path)) {
		if (path.dentry != path.mnt->mnt_root)
			break;
	}
	err = vfs_getattr(&path, &stat, STATX_INO, AT_STATX_SYNC_AS_STAT);
	path_put(&path);
	if (!err)
		*pino = stat.ino;
	return err;
}

static __be32
nfsd4_encode_bitmap4(struct xdr_stream *xdr, u32 bmval0, u32 bmval1, u32 bmval2)
{
	__be32 *p;

	if (bmval2) {
		p = xdr_reserve_space(xdr, XDR_UNIT * 4);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(3);
		*p++ = cpu_to_be32(bmval0);
		*p++ = cpu_to_be32(bmval1);
		*p++ = cpu_to_be32(bmval2);
	} else if (bmval1) {
		p = xdr_reserve_space(xdr, XDR_UNIT * 3);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(2);
		*p++ = cpu_to_be32(bmval0);
		*p++ = cpu_to_be32(bmval1);
	} else {
		p = xdr_reserve_space(xdr, XDR_UNIT * 2);
		if (!p)
			goto out_resource;
		*p++ = cpu_to_be32(1);
		*p++ = cpu_to_be32(bmval0);
	}

	return nfs_ok;
out_resource:
	return nfserr_resource;
}

struct nfsd4_fattr_args {
	struct svc_rqst		*rqstp;
	struct svc_fh		*fhp;
	struct svc_export	*exp;
	struct dentry		*dentry;
	struct kstat		stat;
	struct kstatfs		statfs;
	struct nfs4_acl		*acl;
	u64			change_attr;
#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
	struct lsm_context	context;
#endif
	u32			rdattr_err;
	bool			contextsupport;
	bool			ignore_crossmnt;
};

typedef __be32(*nfsd4_enc_attr)(struct xdr_stream *xdr,
				const struct nfsd4_fattr_args *args);

static __be32 nfsd4_encode_fattr4__noop(struct xdr_stream *xdr,
					const struct nfsd4_fattr_args *args)
{
	return nfs_ok;
}

static __be32 nfsd4_encode_fattr4__true(struct xdr_stream *xdr,
					const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_bool(xdr, true);
}

static __be32 nfsd4_encode_fattr4__false(struct xdr_stream *xdr,
					 const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_bool(xdr, false);
}

static __be32 nfsd4_encode_fattr4_supported_attrs(struct xdr_stream *xdr,
						  const struct nfsd4_fattr_args *args)
{
	struct nfsd4_compoundres *resp = args->rqstp->rq_resp;
	u32 minorversion = resp->cstate.minorversion;
	u32 supp[3];

	memcpy(supp, nfsd_suppattrs[minorversion], sizeof(supp));
	if (!IS_POSIXACL(d_inode(args->dentry)))
		supp[0] &= ~FATTR4_WORD0_ACL;
	if (!args->contextsupport)
		supp[2] &= ~FATTR4_WORD2_SECURITY_LABEL;

	return nfsd4_encode_bitmap4(xdr, supp[0], supp[1], supp[2]);
}

static __be32 nfsd4_encode_fattr4_type(struct xdr_stream *xdr,
				       const struct nfsd4_fattr_args *args)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, XDR_UNIT);
	if (!p)
		return nfserr_resource;

	switch (args->stat.mode & S_IFMT) {
	case S_IFIFO:
		*p = cpu_to_be32(NF4FIFO);
		break;
	case S_IFCHR:
		*p = cpu_to_be32(NF4CHR);
		break;
	case S_IFDIR:
		*p = cpu_to_be32(NF4DIR);
		break;
	case S_IFBLK:
		*p = cpu_to_be32(NF4BLK);
		break;
	case S_IFLNK:
		*p = cpu_to_be32(NF4LNK);
		break;
	case S_IFREG:
		*p = cpu_to_be32(NF4REG);
		break;
	case S_IFSOCK:
		*p = cpu_to_be32(NF4SOCK);
		break;
	default:
		return nfserr_serverfault;
	}

	return nfs_ok;
}

static __be32 nfsd4_encode_fattr4_fh_expire_type(struct xdr_stream *xdr,
						 const struct nfsd4_fattr_args *args)
{
	u32 mask;

	mask = NFS4_FH_PERSISTENT;
	if (!(args->exp->ex_flags & NFSEXP_NOSUBTREECHECK))
		mask |= NFS4_FH_VOL_RENAME;
	return nfsd4_encode_uint32_t(xdr, mask);
}

static __be32 nfsd4_encode_fattr4_change(struct xdr_stream *xdr,
					 const struct nfsd4_fattr_args *args)
{
	const struct svc_export *exp = args->exp;

	if (unlikely(exp->ex_flags & NFSEXP_V4ROOT)) {
		u32 flush_time = convert_to_wallclock(exp->cd->flush_time);

		if (xdr_stream_encode_u32(xdr, flush_time) != XDR_UNIT)
			return nfserr_resource;
		if (xdr_stream_encode_u32(xdr, 0) != XDR_UNIT)
			return nfserr_resource;
		return nfs_ok;
	}
	return nfsd4_encode_changeid4(xdr, args->change_attr);
}

static __be32 nfsd4_encode_fattr4_size(struct xdr_stream *xdr,
				       const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint64_t(xdr, args->stat.size);
}

static __be32 nfsd4_encode_fattr4_fsid(struct xdr_stream *xdr,
				       const struct nfsd4_fattr_args *args)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, XDR_UNIT * 2 + XDR_UNIT * 2);
	if (!p)
		return nfserr_resource;

	if (unlikely(args->exp->ex_fslocs.migrated)) {
		p = xdr_encode_hyper(p, NFS4_REFERRAL_FSID_MAJOR);
		xdr_encode_hyper(p, NFS4_REFERRAL_FSID_MINOR);
		return nfs_ok;
	}
	switch (fsid_source(args->fhp)) {
	case FSIDSOURCE_FSID:
		p = xdr_encode_hyper(p, (u64)args->exp->ex_fsid);
		xdr_encode_hyper(p, (u64)0);
		break;
	case FSIDSOURCE_DEV:
		*p++ = xdr_zero;
		*p++ = cpu_to_be32(MAJOR(args->stat.dev));
		*p++ = xdr_zero;
		*p   = cpu_to_be32(MINOR(args->stat.dev));
		break;
	case FSIDSOURCE_UUID:
		xdr_encode_opaque_fixed(p, args->exp->ex_uuid, EX_UUID_LEN);
		break;
	}

	return nfs_ok;
}

static __be32 nfsd4_encode_fattr4_lease_time(struct xdr_stream *xdr,
					     const struct nfsd4_fattr_args *args)
{
	struct nfsd_net *nn = net_generic(SVC_NET(args->rqstp), nfsd_net_id);

	return nfsd4_encode_nfs_lease4(xdr, nn->nfsd4_lease);
}

static __be32 nfsd4_encode_fattr4_rdattr_error(struct xdr_stream *xdr,
					       const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint32_t(xdr, args->rdattr_err);
}

static __be32 nfsd4_encode_fattr4_aclsupport(struct xdr_stream *xdr,
					     const struct nfsd4_fattr_args *args)
{
	u32 mask;

	mask = 0;
	if (IS_POSIXACL(d_inode(args->dentry)))
		mask = ACL4_SUPPORT_ALLOW_ACL | ACL4_SUPPORT_DENY_ACL;
	return nfsd4_encode_uint32_t(xdr, mask);
}

static __be32 nfsd4_encode_fattr4_acl(struct xdr_stream *xdr,
				      const struct nfsd4_fattr_args *args)
{
	struct nfs4_acl *acl = args->acl;
	struct nfs4_ace *ace;
	__be32 status;

	/* nfsace4<> */
	if (!acl) {
		if (xdr_stream_encode_u32(xdr, 0) != XDR_UNIT)
			return nfserr_resource;
	} else {
		if (xdr_stream_encode_u32(xdr, acl->naces) != XDR_UNIT)
			return nfserr_resource;
		for (ace = acl->aces; ace < acl->aces + acl->naces; ace++) {
			status = nfsd4_encode_nfsace4(xdr, args->rqstp, ace);
			if (status != nfs_ok)
				return status;
		}
	}
	return nfs_ok;
}

static __be32 nfsd4_encode_fattr4_filehandle(struct xdr_stream *xdr,
					     const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_nfs_fh4(xdr, &args->fhp->fh_handle);
}

static __be32 nfsd4_encode_fattr4_fileid(struct xdr_stream *xdr,
					 const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint64_t(xdr, args->stat.ino);
}

static __be32 nfsd4_encode_fattr4_files_avail(struct xdr_stream *xdr,
					      const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint64_t(xdr, args->statfs.f_ffree);
}

static __be32 nfsd4_encode_fattr4_files_free(struct xdr_stream *xdr,
					     const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint64_t(xdr, args->statfs.f_ffree);
}

static __be32 nfsd4_encode_fattr4_files_total(struct xdr_stream *xdr,
					      const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint64_t(xdr, args->statfs.f_files);
}

static __be32 nfsd4_encode_fattr4_fs_locations(struct xdr_stream *xdr,
					       const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_fs_locations4(xdr, args->rqstp, args->exp);
}

static __be32 nfsd4_encode_fattr4_maxfilesize(struct xdr_stream *xdr,
					      const struct nfsd4_fattr_args *args)
{
	struct super_block *sb = args->exp->ex_path.mnt->mnt_sb;

	return nfsd4_encode_uint64_t(xdr, sb->s_maxbytes);
}

static __be32 nfsd4_encode_fattr4_maxlink(struct xdr_stream *xdr,
					  const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint32_t(xdr, 255);
}

static __be32 nfsd4_encode_fattr4_maxname(struct xdr_stream *xdr,
					  const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint32_t(xdr, args->statfs.f_namelen);
}

static __be32 nfsd4_encode_fattr4_maxread(struct xdr_stream *xdr,
					  const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint64_t(xdr, svc_max_payload(args->rqstp));
}

static __be32 nfsd4_encode_fattr4_maxwrite(struct xdr_stream *xdr,
					   const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint64_t(xdr, svc_max_payload(args->rqstp));
}

static __be32 nfsd4_encode_fattr4_mode(struct xdr_stream *xdr,
				       const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_mode4(xdr, args->stat.mode & S_IALLUGO);
}

static __be32 nfsd4_encode_fattr4_numlinks(struct xdr_stream *xdr,
					   const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint32_t(xdr, args->stat.nlink);
}

static __be32 nfsd4_encode_fattr4_owner(struct xdr_stream *xdr,
					const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_user(xdr, args->rqstp, args->stat.uid);
}

static __be32 nfsd4_encode_fattr4_owner_group(struct xdr_stream *xdr,
					      const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_group(xdr, args->rqstp, args->stat.gid);
}

static __be32 nfsd4_encode_fattr4_rawdev(struct xdr_stream *xdr,
					 const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_specdata4(xdr, MAJOR(args->stat.rdev),
				      MINOR(args->stat.rdev));
}

static __be32 nfsd4_encode_fattr4_space_avail(struct xdr_stream *xdr,
					      const struct nfsd4_fattr_args *args)
{
	u64 avail = (u64)args->statfs.f_bavail * (u64)args->statfs.f_bsize;

	return nfsd4_encode_uint64_t(xdr, avail);
}

static __be32 nfsd4_encode_fattr4_space_free(struct xdr_stream *xdr,
					     const struct nfsd4_fattr_args *args)
{
	u64 free = (u64)args->statfs.f_bfree * (u64)args->statfs.f_bsize;

	return nfsd4_encode_uint64_t(xdr, free);
}

static __be32 nfsd4_encode_fattr4_space_total(struct xdr_stream *xdr,
					      const struct nfsd4_fattr_args *args)
{
	u64 total = (u64)args->statfs.f_blocks * (u64)args->statfs.f_bsize;

	return nfsd4_encode_uint64_t(xdr, total);
}

static __be32 nfsd4_encode_fattr4_space_used(struct xdr_stream *xdr,
					     const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint64_t(xdr, (u64)args->stat.blocks << 9);
}

static __be32 nfsd4_encode_fattr4_time_access(struct xdr_stream *xdr,
					      const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_nfstime4(xdr, &args->stat.atime);
}

static __be32 nfsd4_encode_fattr4_time_create(struct xdr_stream *xdr,
					      const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_nfstime4(xdr, &args->stat.btime);
}

/*
 * ctime (in NFSv4, time_metadata) is not writeable, and the client
 * doesn't really care what resolution could theoretically be stored by
 * the filesystem.
 *
 * The client cares how close together changes can be while still
 * guaranteeing ctime changes.  For most filesystems (which have
 * timestamps with nanosecond fields) that is limited by the resolution
 * of the time returned from current_time() (which I'm assuming to be
 * 1/HZ).
 */
static __be32 nfsd4_encode_fattr4_time_delta(struct xdr_stream *xdr,
					     const struct nfsd4_fattr_args *args)
{
	const struct inode *inode = d_inode(args->dentry);
	u32 ns = max_t(u32, NSEC_PER_SEC/HZ, inode->i_sb->s_time_gran);
	struct timespec64 ts = ns_to_timespec64(ns);

	return nfsd4_encode_nfstime4(xdr, &ts);
}

static __be32 nfsd4_encode_fattr4_time_metadata(struct xdr_stream *xdr,
						const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_nfstime4(xdr, &args->stat.ctime);
}

static __be32 nfsd4_encode_fattr4_time_modify(struct xdr_stream *xdr,
					      const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_nfstime4(xdr, &args->stat.mtime);
}

static __be32 nfsd4_encode_fattr4_mounted_on_fileid(struct xdr_stream *xdr,
						    const struct nfsd4_fattr_args *args)
{
	u64 ino;
	int err;

	if (!args->ignore_crossmnt &&
	    args->dentry == args->exp->ex_path.mnt->mnt_root) {
		err = nfsd4_get_mounted_on_ino(args->exp, &ino);
		if (err)
			return nfserrno(err);
	} else
		ino = args->stat.ino;

	return nfsd4_encode_uint64_t(xdr, ino);
}

#ifdef CONFIG_NFSD_PNFS

static __be32 nfsd4_encode_fattr4_fs_layout_types(struct xdr_stream *xdr,
						  const struct nfsd4_fattr_args *args)
{
	unsigned long mask = args->exp->ex_layout_types;
	int i;

	/* Hamming weight of @mask is the number of layout types to return */
	if (xdr_stream_encode_u32(xdr, hweight_long(mask)) != XDR_UNIT)
		return nfserr_resource;
	for (i = LAYOUT_NFSV4_1_FILES; i < LAYOUT_TYPE_MAX; ++i)
		if (mask & BIT(i)) {
			/* layouttype4 */
			if (xdr_stream_encode_u32(xdr, i) != XDR_UNIT)
				return nfserr_resource;
		}
	return nfs_ok;
}

static __be32 nfsd4_encode_fattr4_layout_types(struct xdr_stream *xdr,
					       const struct nfsd4_fattr_args *args)
{
	unsigned long mask = args->exp->ex_layout_types;
	int i;

	/* Hamming weight of @mask is the number of layout types to return */
	if (xdr_stream_encode_u32(xdr, hweight_long(mask)) != XDR_UNIT)
		return nfserr_resource;
	for (i = LAYOUT_NFSV4_1_FILES; i < LAYOUT_TYPE_MAX; ++i)
		if (mask & BIT(i)) {
			/* layouttype4 */
			if (xdr_stream_encode_u32(xdr, i) != XDR_UNIT)
				return nfserr_resource;
		}
	return nfs_ok;
}

static __be32 nfsd4_encode_fattr4_layout_blksize(struct xdr_stream *xdr,
						 const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_uint32_t(xdr, args->stat.blksize);
}

#endif

static __be32 nfsd4_encode_fattr4_suppattr_exclcreat(struct xdr_stream *xdr,
						     const struct nfsd4_fattr_args *args)
{
	struct nfsd4_compoundres *resp = args->rqstp->rq_resp;
	u32 supp[3];

	memcpy(supp, nfsd_suppattrs[resp->cstate.minorversion], sizeof(supp));
	supp[0] &= NFSD_SUPPATTR_EXCLCREAT_WORD0;
	supp[1] &= NFSD_SUPPATTR_EXCLCREAT_WORD1;
	supp[2] &= NFSD_SUPPATTR_EXCLCREAT_WORD2;

	return nfsd4_encode_bitmap4(xdr, supp[0], supp[1], supp[2]);
}

#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
static __be32 nfsd4_encode_fattr4_sec_label(struct xdr_stream *xdr,
					    const struct nfsd4_fattr_args *args)
{
	return nfsd4_encode_security_label(xdr, args->rqstp, &args->context);
}
#endif

static __be32 nfsd4_encode_fattr4_xattr_support(struct xdr_stream *xdr,
						const struct nfsd4_fattr_args *args)
{
	int err = xattr_supports_user_prefix(d_inode(args->dentry));

	return nfsd4_encode_bool(xdr, err == 0);
}

#define NFSD_OA_SHARE_ACCESS	(BIT(OPEN_ARGS_SHARE_ACCESS_READ)	| \
				 BIT(OPEN_ARGS_SHARE_ACCESS_WRITE)	| \
				 BIT(OPEN_ARGS_SHARE_ACCESS_BOTH))

#define NFSD_OA_SHARE_DENY	(BIT(OPEN_ARGS_SHARE_DENY_NONE)		| \
				 BIT(OPEN_ARGS_SHARE_DENY_READ)		| \
				 BIT(OPEN_ARGS_SHARE_DENY_WRITE)	| \
				 BIT(OPEN_ARGS_SHARE_DENY_BOTH))

#define NFSD_OA_SHARE_ACCESS_WANT	(BIT(OPEN_ARGS_SHARE_ACCESS_WANT_ANY_DELEG)		| \
					 BIT(OPEN_ARGS_SHARE_ACCESS_WANT_NO_DELEG)		| \
					 BIT(OPEN_ARGS_SHARE_ACCESS_WANT_CANCEL)		| \
					 BIT(OPEN_ARGS_SHARE_ACCESS_WANT_DELEG_TIMESTAMPS)	| \
					 BIT(OPEN_ARGS_SHARE_ACCESS_WANT_OPEN_XOR_DELEGATION))

#define NFSD_OA_OPEN_CLAIM	(BIT(OPEN_ARGS_OPEN_CLAIM_NULL)		| \
				 BIT(OPEN_ARGS_OPEN_CLAIM_PREVIOUS)	| \
				 BIT(OPEN_ARGS_OPEN_CLAIM_DELEGATE_CUR)	| \
				 BIT(OPEN_ARGS_OPEN_CLAIM_DELEGATE_PREV)| \
				 BIT(OPEN_ARGS_OPEN_CLAIM_FH)		| \
				 BIT(OPEN_ARGS_OPEN_CLAIM_DELEG_CUR_FH)	| \
				 BIT(OPEN_ARGS_OPEN_CLAIM_DELEG_PREV_FH))

#define NFSD_OA_CREATE_MODE	(BIT(OPEN_ARGS_CREATEMODE_UNCHECKED4)	| \
				 BIT(OPEN_ARGS_CREATE_MODE_GUARDED)	| \
				 BIT(OPEN_ARGS_CREATEMODE_EXCLUSIVE4)	| \
				 BIT(OPEN_ARGS_CREATE_MODE_EXCLUSIVE4_1))

static uint32_t oa_share_access = NFSD_OA_SHARE_ACCESS;
static uint32_t oa_share_deny = NFSD_OA_SHARE_DENY;
static uint32_t oa_share_access_want = NFSD_OA_SHARE_ACCESS_WANT;
static uint32_t oa_open_claim = NFSD_OA_OPEN_CLAIM;
static uint32_t oa_create_mode = NFSD_OA_CREATE_MODE;

static const struct open_arguments4 nfsd_open_arguments = {
	.oa_share_access = { .count = 1, .element = &oa_share_access },
	.oa_share_deny = { .count = 1, .element = &oa_share_deny },
	.oa_share_access_want = { .count = 1, .element = &oa_share_access_want },
	.oa_open_claim = { .count = 1, .element = &oa_open_claim },
	.oa_create_mode = { .count = 1, .element = &oa_create_mode },
};

static __be32 nfsd4_encode_fattr4_open_arguments(struct xdr_stream *xdr,
						 const struct nfsd4_fattr_args *args)
{
	if (!xdrgen_encode_fattr4_open_arguments(xdr, &nfsd_open_arguments))
		return nfserr_resource;
	return nfs_ok;
}

static const nfsd4_enc_attr nfsd4_enc_fattr4_encode_ops[] = {
	[FATTR4_SUPPORTED_ATTRS]	= nfsd4_encode_fattr4_supported_attrs,
	[FATTR4_TYPE]			= nfsd4_encode_fattr4_type,
	[FATTR4_FH_EXPIRE_TYPE]		= nfsd4_encode_fattr4_fh_expire_type,
	[FATTR4_CHANGE]			= nfsd4_encode_fattr4_change,
	[FATTR4_SIZE]			= nfsd4_encode_fattr4_size,
	[FATTR4_LINK_SUPPORT]		= nfsd4_encode_fattr4__true,
	[FATTR4_SYMLINK_SUPPORT]	= nfsd4_encode_fattr4__true,
	[FATTR4_NAMED_ATTR]		= nfsd4_encode_fattr4__false,
	[FATTR4_FSID]			= nfsd4_encode_fattr4_fsid,
	[FATTR4_UNIQUE_HANDLES]		= nfsd4_encode_fattr4__true,
	[FATTR4_LEASE_TIME]		= nfsd4_encode_fattr4_lease_time,
	[FATTR4_RDATTR_ERROR]		= nfsd4_encode_fattr4_rdattr_error,
	[FATTR4_ACL]			= nfsd4_encode_fattr4_acl,
	[FATTR4_ACLSUPPORT]		= nfsd4_encode_fattr4_aclsupport,
	[FATTR4_ARCHIVE]		= nfsd4_encode_fattr4__noop,
	[FATTR4_CANSETTIME]		= nfsd4_encode_fattr4__true,
	[FATTR4_CASE_INSENSITIVE]	= nfsd4_encode_fattr4__false,
	[FATTR4_CASE_PRESERVING]	= nfsd4_encode_fattr4__true,
	[FATTR4_CHOWN_RESTRICTED]	= nfsd4_encode_fattr4__true,
	[FATTR4_FILEHANDLE]		= nfsd4_encode_fattr4_filehandle,
	[FATTR4_FILEID]			= nfsd4_encode_fattr4_fileid,
	[FATTR4_FILES_AVAIL]		= nfsd4_encode_fattr4_files_avail,
	[FATTR4_FILES_FREE]		= nfsd4_encode_fattr4_files_free,
	[FATTR4_FILES_TOTAL]		= nfsd4_encode_fattr4_files_total,
	[FATTR4_FS_LOCATIONS]		= nfsd4_encode_fattr4_fs_locations,
	[FATTR4_HIDDEN]			= nfsd4_encode_fattr4__noop,
	[FATTR4_HOMOGENEOUS]		= nfsd4_encode_fattr4__true,
	[FATTR4_MAXFILESIZE]		= nfsd4_encode_fattr4_maxfilesize,
	[FATTR4_MAXLINK]		= nfsd4_encode_fattr4_maxlink,
	[FATTR4_MAXNAME]		= nfsd4_encode_fattr4_maxname,
	[FATTR4_MAXREAD]		= nfsd4_encode_fattr4_maxread,
	[FATTR4_MAXWRITE]		= nfsd4_encode_fattr4_maxwrite,
	[FATTR4_MIMETYPE]		= nfsd4_encode_fattr4__noop,
	[FATTR4_MODE]			= nfsd4_encode_fattr4_mode,
	[FATTR4_NO_TRUNC]		= nfsd4_encode_fattr4__true,
	[FATTR4_NUMLINKS]		= nfsd4_encode_fattr4_numlinks,
	[FATTR4_OWNER]			= nfsd4_encode_fattr4_owner,
	[FATTR4_OWNER_GROUP]		= nfsd4_encode_fattr4_owner_group,
	[FATTR4_QUOTA_AVAIL_HARD]	= nfsd4_encode_fattr4__noop,
	[FATTR4_QUOTA_AVAIL_SOFT]	= nfsd4_encode_fattr4__noop,
	[FATTR4_QUOTA_USED]		= nfsd4_encode_fattr4__noop,
	[FATTR4_RAWDEV]			= nfsd4_encode_fattr4_rawdev,
	[FATTR4_SPACE_AVAIL]		= nfsd4_encode_fattr4_space_avail,
	[FATTR4_SPACE_FREE]		= nfsd4_encode_fattr4_space_free,
	[FATTR4_SPACE_TOTAL]		= nfsd4_encode_fattr4_space_total,
	[FATTR4_SPACE_USED]		= nfsd4_encode_fattr4_space_used,
	[FATTR4_SYSTEM]			= nfsd4_encode_fattr4__noop,
	[FATTR4_TIME_ACCESS]		= nfsd4_encode_fattr4_time_access,
	[FATTR4_TIME_ACCESS_SET]	= nfsd4_encode_fattr4__noop,
	[FATTR4_TIME_BACKUP]		= nfsd4_encode_fattr4__noop,
	[FATTR4_TIME_CREATE]		= nfsd4_encode_fattr4_time_create,
	[FATTR4_TIME_DELTA]		= nfsd4_encode_fattr4_time_delta,
	[FATTR4_TIME_METADATA]		= nfsd4_encode_fattr4_time_metadata,
	[FATTR4_TIME_MODIFY]		= nfsd4_encode_fattr4_time_modify,
	[FATTR4_TIME_MODIFY_SET]	= nfsd4_encode_fattr4__noop,
	[FATTR4_MOUNTED_ON_FILEID]	= nfsd4_encode_fattr4_mounted_on_fileid,
	[FATTR4_DIR_NOTIF_DELAY]	= nfsd4_encode_fattr4__noop,
	[FATTR4_DIRENT_NOTIF_DELAY]	= nfsd4_encode_fattr4__noop,
	[FATTR4_DACL]			= nfsd4_encode_fattr4__noop,
	[FATTR4_SACL]			= nfsd4_encode_fattr4__noop,
	[FATTR4_CHANGE_POLICY]		= nfsd4_encode_fattr4__noop,
	[FATTR4_FS_STATUS]		= nfsd4_encode_fattr4__noop,

#ifdef CONFIG_NFSD_PNFS
	[FATTR4_FS_LAYOUT_TYPES]	= nfsd4_encode_fattr4_fs_layout_types,
	[FATTR4_LAYOUT_HINT]		= nfsd4_encode_fattr4__noop,
	[FATTR4_LAYOUT_TYPES]		= nfsd4_encode_fattr4_layout_types,
	[FATTR4_LAYOUT_BLKSIZE]		= nfsd4_encode_fattr4_layout_blksize,
	[FATTR4_LAYOUT_ALIGNMENT]	= nfsd4_encode_fattr4__noop,
#else
	[FATTR4_FS_LAYOUT_TYPES]	= nfsd4_encode_fattr4__noop,
	[FATTR4_LAYOUT_HINT]		= nfsd4_encode_fattr4__noop,
	[FATTR4_LAYOUT_TYPES]		= nfsd4_encode_fattr4__noop,
	[FATTR4_LAYOUT_BLKSIZE]		= nfsd4_encode_fattr4__noop,
	[FATTR4_LAYOUT_ALIGNMENT]	= nfsd4_encode_fattr4__noop,
#endif

	[FATTR4_FS_LOCATIONS_INFO]	= nfsd4_encode_fattr4__noop,
	[FATTR4_MDSTHRESHOLD]		= nfsd4_encode_fattr4__noop,
	[FATTR4_RETENTION_GET]		= nfsd4_encode_fattr4__noop,
	[FATTR4_RETENTION_SET]		= nfsd4_encode_fattr4__noop,
	[FATTR4_RETENTEVT_GET]		= nfsd4_encode_fattr4__noop,
	[FATTR4_RETENTEVT_SET]		= nfsd4_encode_fattr4__noop,
	[FATTR4_RETENTION_HOLD]		= nfsd4_encode_fattr4__noop,
	[FATTR4_MODE_SET_MASKED]	= nfsd4_encode_fattr4__noop,
	[FATTR4_SUPPATTR_EXCLCREAT]	= nfsd4_encode_fattr4_suppattr_exclcreat,
	[FATTR4_FS_CHARSET_CAP]		= nfsd4_encode_fattr4__noop,
	[FATTR4_CLONE_BLKSIZE]		= nfsd4_encode_fattr4__noop,
	[FATTR4_SPACE_FREED]		= nfsd4_encode_fattr4__noop,
	[FATTR4_CHANGE_ATTR_TYPE]	= nfsd4_encode_fattr4__noop,

#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
	[FATTR4_SEC_LABEL]		= nfsd4_encode_fattr4_sec_label,
#else
	[FATTR4_SEC_LABEL]		= nfsd4_encode_fattr4__noop,
#endif

	[FATTR4_MODE_UMASK]		= nfsd4_encode_fattr4__noop,
	[FATTR4_XATTR_SUPPORT]		= nfsd4_encode_fattr4_xattr_support,
	[FATTR4_OPEN_ARGUMENTS]		= nfsd4_encode_fattr4_open_arguments,
};

/*
 * Note: @fhp can be NULL; in this case, we might have to compose the filehandle
 * ourselves.
 */
static __be32
nfsd4_encode_fattr4(struct svc_rqst *rqstp, struct xdr_stream *xdr,
		    struct svc_fh *fhp, struct svc_export *exp,
		    struct dentry *dentry, const u32 *bmval,
		    int ignore_crossmnt)
{
	DECLARE_BITMAP(attr_bitmap, ARRAY_SIZE(nfsd4_enc_fattr4_encode_ops));
	struct nfs4_delegation *dp = NULL;
	struct nfsd4_fattr_args args;
	struct svc_fh *tempfh = NULL;
	int starting_len = xdr->buf->len;
	unsigned int attrlen_offset;
	__be32 attrlen, status;
	u32 attrmask[3];
	int err;
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	u32 minorversion = resp->cstate.minorversion;
	struct path path = {
		.mnt	= exp->ex_path.mnt,
		.dentry	= dentry,
	};
	unsigned long bit;

	WARN_ON_ONCE(bmval[1] & NFSD_WRITEONLY_ATTRS_WORD1);
	WARN_ON_ONCE(!nfsd_attrs_supported(minorversion, bmval));

	args.rqstp = rqstp;
	args.exp = exp;
	args.dentry = dentry;
	args.ignore_crossmnt = (ignore_crossmnt != 0);
	args.acl = NULL;
#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
	args.context.context = NULL;
#endif

	/*
	 * Make a local copy of the attribute bitmap that can be modified.
	 */
	attrmask[0] = bmval[0];
	attrmask[1] = bmval[1];
	attrmask[2] = bmval[2];

	args.rdattr_err = 0;
	if (exp->ex_fslocs.migrated) {
		status = fattr_handle_absent_fs(&attrmask[0], &attrmask[1],
						&attrmask[2], &args.rdattr_err);
		if (status)
			goto out;
	}
	if ((attrmask[0] & (FATTR4_WORD0_CHANGE |
			    FATTR4_WORD0_SIZE)) ||
	    (attrmask[1] & (FATTR4_WORD1_TIME_ACCESS |
			    FATTR4_WORD1_TIME_MODIFY |
			    FATTR4_WORD1_TIME_METADATA))) {
		status = nfsd4_deleg_getattr_conflict(rqstp, dentry, &dp);
		if (status)
			goto out;
	}

	err = vfs_getattr(&path, &args.stat,
			  STATX_BASIC_STATS | STATX_BTIME | STATX_CHANGE_COOKIE,
			  AT_STATX_SYNC_AS_STAT);
	if (dp) {
		struct nfs4_cb_fattr *ncf = &dp->dl_cb_fattr;

		if (ncf->ncf_file_modified) {
			++ncf->ncf_initial_cinfo;
			args.stat.size = ncf->ncf_cur_fsize;
			if (!timespec64_is_epoch(&ncf->ncf_cb_mtime))
				args.stat.mtime = ncf->ncf_cb_mtime;
		}
		args.change_attr = ncf->ncf_initial_cinfo;

		if (!timespec64_is_epoch(&ncf->ncf_cb_atime))
			args.stat.atime = ncf->ncf_cb_atime;

		nfs4_put_stid(&dp->dl_stid);
	} else {
		args.change_attr = nfsd4_change_attribute(&args.stat);
	}

	if (err)
		goto out_nfserr;

	if (!(args.stat.result_mask & STATX_BTIME))
		/* underlying FS does not offer btime so we can't share it */
		attrmask[1] &= ~FATTR4_WORD1_TIME_CREATE;
	if ((attrmask[0] & (FATTR4_WORD0_FILES_AVAIL | FATTR4_WORD0_FILES_FREE |
			FATTR4_WORD0_FILES_TOTAL | FATTR4_WORD0_MAXNAME)) ||
	    (attrmask[1] & (FATTR4_WORD1_SPACE_AVAIL | FATTR4_WORD1_SPACE_FREE |
		       FATTR4_WORD1_SPACE_TOTAL))) {
		err = vfs_statfs(&path, &args.statfs);
		if (err)
			goto out_nfserr;
	}
	if ((attrmask[0] & (FATTR4_WORD0_FILEHANDLE | FATTR4_WORD0_FSID)) &&
	    !fhp) {
		tempfh = kmalloc(sizeof(struct svc_fh), GFP_KERNEL);
		status = nfserr_jukebox;
		if (!tempfh)
			goto out;
		fh_init(tempfh, NFS4_FHSIZE);
		status = fh_compose(tempfh, exp, dentry, NULL);
		if (status)
			goto out;
		args.fhp = tempfh;
	} else
		args.fhp = fhp;

	if (attrmask[0] & FATTR4_WORD0_ACL) {
		err = nfsd4_get_nfs4_acl(rqstp, dentry, &args.acl);
		if (err == -EOPNOTSUPP)
			attrmask[0] &= ~FATTR4_WORD0_ACL;
		else if (err == -EINVAL) {
			status = nfserr_attrnotsupp;
			goto out;
		} else if (err != 0)
			goto out_nfserr;
	}

	args.contextsupport = false;

#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
	if ((attrmask[2] & FATTR4_WORD2_SECURITY_LABEL) ||
	     attrmask[0] & FATTR4_WORD0_SUPPORTED_ATTRS) {
		if (exp->ex_flags & NFSEXP_SECURITY_LABEL)
			err = security_inode_getsecctx(d_inode(dentry),
						&args.context);
		else
			err = -EOPNOTSUPP;
		args.contextsupport = (err == 0);
		if (attrmask[2] & FATTR4_WORD2_SECURITY_LABEL) {
			if (err == -EOPNOTSUPP)
				attrmask[2] &= ~FATTR4_WORD2_SECURITY_LABEL;
			else if (err)
				goto out_nfserr;
		}
	}
#endif /* CONFIG_NFSD_V4_SECURITY_LABEL */

	/* attrmask */
	status = nfsd4_encode_bitmap4(xdr, attrmask[0], attrmask[1],
				      attrmask[2]);
	if (status)
		goto out;

	/* attr_vals */
	attrlen_offset = xdr->buf->len;
	if (unlikely(!xdr_reserve_space(xdr, XDR_UNIT)))
		goto out_resource;
	bitmap_from_arr32(attr_bitmap, attrmask,
			  ARRAY_SIZE(nfsd4_enc_fattr4_encode_ops));
	for_each_set_bit(bit, attr_bitmap,
			 ARRAY_SIZE(nfsd4_enc_fattr4_encode_ops)) {
		status = nfsd4_enc_fattr4_encode_ops[bit](xdr, &args);
		if (status != nfs_ok)
			goto out;
	}
	attrlen = cpu_to_be32(xdr->buf->len - attrlen_offset - XDR_UNIT);
	write_bytes_to_xdr_buf(xdr->buf, attrlen_offset, &attrlen, XDR_UNIT);
	status = nfs_ok;

out:
#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
	if (args.context.context)
		security_release_secctx(&args.context);
#endif /* CONFIG_NFSD_V4_SECURITY_LABEL */
	kfree(args.acl);
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
	ret = nfsd4_encode_fattr4(rqstp, &xdr, fhp, exp, dentry, bmval,
				  ignore_crossmnt);
	*p = xdr.p;
	return ret;
}

/*
 * The buffer space for this field was reserved during a previous
 * call to nfsd4_encode_entry4().
 */
static void nfsd4_encode_entry4_nfs_cookie4(const struct nfsd4_readdir *readdir,
					    u64 offset)
{
	__be64 cookie = cpu_to_be64(offset);
	struct xdr_stream *xdr = readdir->xdr;

	if (!readdir->cookie_offset)
		return;
	write_bytes_to_xdr_buf(xdr->buf, readdir->cookie_offset, &cookie,
			       sizeof(cookie));
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
nfsd4_encode_entry4_fattr(struct nfsd4_readdir *cd, const char *name,
			  int namlen)
{
	struct svc_export *exp = cd->rd_fhp->fh_export;
	struct dentry *dentry;
	__be32 nfserr;
	int ignore_crossmnt = 0;

	dentry = lookup_positive_unlocked(name, cd->rd_fhp->fh_dentry, namlen);
	if (IS_ERR(dentry))
		return nfserrno(PTR_ERR(dentry));

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
		nfserr = check_nfsd_access(exp, cd->rd_rqstp, false);
		if (nfserr)
			goto out_put;

	}
out_encode:
	nfserr = nfsd4_encode_fattr4(cd->rd_rqstp, cd->xdr, NULL, exp, dentry,
				     cd->rd_bmval, ignore_crossmnt);
out_put:
	dput(dentry);
	exp_put(exp);
	return nfserr;
}

static __be32
nfsd4_encode_entry4_rdattr_error(struct xdr_stream *xdr, __be32 nfserr)
{
	__be32 status;

	/* attrmask */
	status = nfsd4_encode_bitmap4(xdr, FATTR4_WORD0_RDATTR_ERROR, 0, 0);
	if (status != nfs_ok)
		return status;
	/* attr_vals */
	if (xdr_stream_encode_u32(xdr, XDR_UNIT) != XDR_UNIT)
		return nfserr_resource;
	/* rdattr_error */
	if (xdr_stream_encode_be32(xdr, nfserr) != XDR_UNIT)
		return nfserr_resource;
	return nfs_ok;
}

static int
nfsd4_encode_entry4(void *ccdv, const char *name, int namlen,
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

	/* In nfsv4, "." and ".." never make it onto the wire.. */
	if (name && isdotent(name, namlen)) {
		cd->common.err = nfs_ok;
		return 0;
	}

	/* Encode the previous entry's cookie value */
	nfsd4_encode_entry4_nfs_cookie4(cd, offset);

	if (xdr_stream_encode_item_present(xdr) != XDR_UNIT)
		goto fail;

	/* Reserve send buffer space for this entry's cookie value. */
	cookie_offset = xdr->buf->len;
	if (nfsd4_encode_nfs_cookie4(xdr, OFFSET_MAX) != nfs_ok)
		goto fail;
	if (nfsd4_encode_component4(xdr, name, namlen) != nfs_ok)
		goto fail;
	nfserr = nfsd4_encode_entry4_fattr(cd, name, namlen);
	switch (nfserr) {
	case nfs_ok:
		break;
	case nfserr_resource:
		nfserr = nfserr_toosmall;
		goto fail;
	case nfserr_noent:
		xdr_truncate_encode(xdr, start_offset);
		goto skip_entry;
	case nfserr_jukebox:
		/*
		 * The pseudoroot should only display dentries that lead to
		 * exports. If we get EJUKEBOX here, then we can't tell whether
		 * this entry should be included. Just fail the whole READDIR
		 * with NFS4ERR_DELAY in that case, and hope that the situation
		 * will resolve itself by the client's next attempt.
		 */
		if (cd->rd_fhp->fh_export->ex_flags & NFSEXP_V4ROOT)
			goto fail;
		fallthrough;
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
		if (nfsd4_encode_entry4_rdattr_error(xdr, nfserr)) {
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
	 * RFC 3530 14.2.24 describes rd_dircount as only a "hint", and
	 * notes that it could be zero. If it is zero, then the server
	 * should enforce only the rd_maxcount value.
	 */
	if (cd->rd_dircount) {
		name_and_cookie = 4 + 4 * XDR_QUADLEN(namlen) + 8;
		if (name_and_cookie > cd->rd_dircount && cd->cookie_offset)
			goto fail;
		cd->rd_dircount -= min(cd->rd_dircount, name_and_cookie);
		if (!cd->rd_dircount)
			cd->rd_maxcount = 0;
	}

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
nfsd4_encode_verifier4(struct xdr_stream *xdr, const nfs4_verifier *verf)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS4_VERIFIER_SIZE);
	if (!p)
		return nfserr_resource;
	memcpy(p, verf->data, sizeof(verf->data));
	return nfs_ok;
}

static __be32
nfsd4_encode_clientid4(struct xdr_stream *xdr, const clientid_t *clientid)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, sizeof(__be64));
	if (!p)
		return nfserr_resource;
	memcpy(p, clientid, sizeof(*clientid));
	return nfs_ok;
}

/* This is a frequently-encoded item; open-coded for speed */
static __be32
nfsd4_encode_stateid4(struct xdr_stream *xdr, const stateid_t *sid)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, NFS4_STATEID_SIZE);
	if (!p)
		return nfserr_resource;
	*p++ = cpu_to_be32(sid->si_generation);
	memcpy(p, &sid->si_opaque, sizeof(sid->si_opaque));
	return nfs_ok;
}

static __be32
nfsd4_encode_sessionid4(struct xdr_stream *xdr,
			const struct nfs4_sessionid *sessionid)
{
	return nfsd4_encode_opaque_fixed(xdr, sessionid->data,
					 NFS4_MAX_SESSIONID_LEN);
}

static __be32
nfsd4_encode_access(struct nfsd4_compoundres *resp, __be32 nfserr,
		    union nfsd4_op_u *u)
{
	struct nfsd4_access *access = &u->access;
	struct xdr_stream *xdr = resp->xdr;
	__be32 status;

	/* supported */
	status = nfsd4_encode_uint32_t(xdr, access->ac_supported);
	if (status != nfs_ok)
		return status;
	/* access */
	return nfsd4_encode_uint32_t(xdr, access->ac_resp_access);
}

static __be32 nfsd4_encode_bind_conn_to_session(struct nfsd4_compoundres *resp, __be32 nfserr,
						union nfsd4_op_u *u)
{
	struct nfsd4_bind_conn_to_session *bcts = &u->bind_conn_to_session;
	struct xdr_stream *xdr = resp->xdr;

	/* bctsr_sessid */
	nfserr = nfsd4_encode_sessionid4(xdr, &bcts->sessionid);
	if (nfserr != nfs_ok)
		return nfserr;
	/* bctsr_dir */
	if (xdr_stream_encode_u32(xdr, bcts->dir) != XDR_UNIT)
		return nfserr_resource;
	/* bctsr_use_conn_in_rdma_mode */
	return nfsd4_encode_bool(xdr, false);
}

static __be32
nfsd4_encode_close(struct nfsd4_compoundres *resp, __be32 nfserr,
		   union nfsd4_op_u *u)
{
	struct nfsd4_close *close = &u->close;
	struct xdr_stream *xdr = resp->xdr;

	/* open_stateid */
	return nfsd4_encode_stateid4(xdr, &close->cl_stateid);
}


static __be32
nfsd4_encode_commit(struct nfsd4_compoundres *resp, __be32 nfserr,
		    union nfsd4_op_u *u)
{
	struct nfsd4_commit *commit = &u->commit;

	return nfsd4_encode_verifier4(resp->xdr, &commit->co_verf);
}

static __be32
nfsd4_encode_create(struct nfsd4_compoundres *resp, __be32 nfserr,
		    union nfsd4_op_u *u)
{
	struct nfsd4_create *create = &u->create;
	struct xdr_stream *xdr = resp->xdr;

	/* cinfo */
	nfserr = nfsd4_encode_change_info4(xdr, &create->cr_cinfo);
	if (nfserr)
		return nfserr;
	/* attrset */
	return nfsd4_encode_bitmap4(xdr, create->cr_bmval[0],
				    create->cr_bmval[1], create->cr_bmval[2]);
}

static __be32
nfsd4_encode_getattr(struct nfsd4_compoundres *resp, __be32 nfserr,
		     union nfsd4_op_u *u)
{
	struct nfsd4_getattr *getattr = &u->getattr;
	struct svc_fh *fhp = getattr->ga_fhp;
	struct xdr_stream *xdr = resp->xdr;

	/* obj_attributes */
	return nfsd4_encode_fattr4(resp->rqstp, xdr, fhp, fhp->fh_export,
				   fhp->fh_dentry, getattr->ga_bmval, 0);
}

static __be32
nfsd4_encode_getfh(struct nfsd4_compoundres *resp, __be32 nfserr,
		   union nfsd4_op_u *u)
{
	struct xdr_stream *xdr = resp->xdr;
	struct svc_fh *fhp = u->getfh;

	/* object */
	return nfsd4_encode_nfs_fh4(xdr, &fhp->fh_handle);
}

static __be32
nfsd4_encode_lock_owner4(struct xdr_stream *xdr, const clientid_t *clientid,
			 const struct xdr_netobj *owner)
{
	__be32 status;

	/* clientid */
	status = nfsd4_encode_clientid4(xdr, clientid);
	if (status != nfs_ok)
		return status;
	/* owner */
	return nfsd4_encode_opaque(xdr, owner->data, owner->len);
}

static __be32
nfsd4_encode_lock4denied(struct xdr_stream *xdr,
			 const struct nfsd4_lock_denied *ld)
{
	__be32 status;

	/* offset */
	status = nfsd4_encode_offset4(xdr, ld->ld_start);
	if (status != nfs_ok)
		return status;
	/* length */
	status = nfsd4_encode_length4(xdr, ld->ld_length);
	if (status != nfs_ok)
		return status;
	/* locktype */
	if (xdr_stream_encode_u32(xdr, ld->ld_type) != XDR_UNIT)
		return nfserr_resource;
	/* owner */
	return nfsd4_encode_lock_owner4(xdr, &ld->ld_clientid,
					&ld->ld_owner);
}

static __be32
nfsd4_encode_lock(struct nfsd4_compoundres *resp, __be32 nfserr,
		  union nfsd4_op_u *u)
{
	struct nfsd4_lock *lock = &u->lock;
	struct xdr_stream *xdr = resp->xdr;
	__be32 status;

	switch (nfserr) {
	case nfs_ok:
		/* resok4 */
		status = nfsd4_encode_stateid4(xdr, &lock->lk_resp_stateid);
		break;
	case nfserr_denied:
		/* denied */
		status = nfsd4_encode_lock4denied(xdr, &lock->lk_denied);
		break;
	default:
		return nfserr;
	}
	return status != nfs_ok ? status : nfserr;
}

static __be32
nfsd4_encode_lockt(struct nfsd4_compoundres *resp, __be32 nfserr,
		   union nfsd4_op_u *u)
{
	struct nfsd4_lockt *lockt = &u->lockt;
	struct xdr_stream *xdr = resp->xdr;
	__be32 status;

	if (nfserr == nfserr_denied) {
		/* denied */
		status = nfsd4_encode_lock4denied(xdr, &lockt->lt_denied);
		if (status != nfs_ok)
			return status;
	}
	return nfserr;
}

static __be32
nfsd4_encode_locku(struct nfsd4_compoundres *resp, __be32 nfserr,
		   union nfsd4_op_u *u)
{
	struct nfsd4_locku *locku = &u->locku;
	struct xdr_stream *xdr = resp->xdr;

	/* lock_stateid */
	return nfsd4_encode_stateid4(xdr, &locku->lu_stateid);
}


static __be32
nfsd4_encode_link(struct nfsd4_compoundres *resp, __be32 nfserr,
		  union nfsd4_op_u *u)
{
	struct nfsd4_link *link = &u->link;
	struct xdr_stream *xdr = resp->xdr;

	return nfsd4_encode_change_info4(xdr, &link->li_cinfo);
}

/*
 * This implementation does not yet support returning an ACE in an
 * OPEN that offers a delegation.
 */
static __be32
nfsd4_encode_open_nfsace4(struct xdr_stream *xdr)
{
	__be32 status;

	/* type */
	status = nfsd4_encode_acetype4(xdr, NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE);
	if (status != nfs_ok)
		return nfserr_resource;
	/* flag */
	status = nfsd4_encode_aceflag4(xdr, 0);
	if (status != nfs_ok)
		return nfserr_resource;
	/* access mask */
	status = nfsd4_encode_acemask4(xdr, 0);
	if (status != nfs_ok)
		return nfserr_resource;
	/* who - empty for now */
	if (xdr_stream_encode_u32(xdr, 0) != XDR_UNIT)
		return nfserr_resource;
	return nfs_ok;
}

static __be32
nfsd4_encode_open_read_delegation4(struct xdr_stream *xdr, struct nfsd4_open *open)
{
	__be32 status;

	/* stateid */
	status = nfsd4_encode_stateid4(xdr, &open->op_delegate_stateid);
	if (status != nfs_ok)
		return status;
	/* recall */
	status = nfsd4_encode_bool(xdr, open->op_recall);
	if (status != nfs_ok)
		return status;
	/* permissions */
	return nfsd4_encode_open_nfsace4(xdr);
}

static __be32
nfsd4_encode_nfs_space_limit4(struct xdr_stream *xdr, u64 filesize)
{
	/* limitby */
	if (xdr_stream_encode_u32(xdr, NFS4_LIMIT_SIZE) != XDR_UNIT)
		return nfserr_resource;
	/* filesize */
	return nfsd4_encode_uint64_t(xdr, filesize);
}

static __be32
nfsd4_encode_open_write_delegation4(struct xdr_stream *xdr,
				    struct nfsd4_open *open)
{
	__be32 status;

	/* stateid */
	status = nfsd4_encode_stateid4(xdr, &open->op_delegate_stateid);
	if (status != nfs_ok)
		return status;
	/* recall */
	status = nfsd4_encode_bool(xdr, open->op_recall);
	if (status != nfs_ok)
		return status;
	/* space_limit */
	status = nfsd4_encode_nfs_space_limit4(xdr, 0);
	if (status != nfs_ok)
		return status;
	return nfsd4_encode_open_nfsace4(xdr);
}

static __be32
nfsd4_encode_open_none_delegation4(struct xdr_stream *xdr,
				   struct nfsd4_open *open)
{
	__be32 status = nfs_ok;

	/* ond_why */
	if (xdr_stream_encode_u32(xdr, open->op_why_no_deleg) != XDR_UNIT)
		return nfserr_resource;
	switch (open->op_why_no_deleg) {
	case WND4_CONTENTION:
		/* ond_server_will_push_deleg */
		status = nfsd4_encode_bool(xdr, false);
		break;
	case WND4_RESOURCE:
		/* ond_server_will_signal_avail */
		status = nfsd4_encode_bool(xdr, false);
	}
	return status;
}

static __be32
nfsd4_encode_open_delegation4(struct xdr_stream *xdr, struct nfsd4_open *open)
{
	__be32 status;

	/* delegation_type */
	if (xdr_stream_encode_u32(xdr, open->op_delegate_type) != XDR_UNIT)
		return nfserr_resource;
	switch (open->op_delegate_type) {
	case OPEN_DELEGATE_NONE:
		status = nfs_ok;
		break;
	case OPEN_DELEGATE_READ:
	case OPEN_DELEGATE_READ_ATTRS_DELEG:
		/* read */
		status = nfsd4_encode_open_read_delegation4(xdr, open);
		break;
	case OPEN_DELEGATE_WRITE:
	case OPEN_DELEGATE_WRITE_ATTRS_DELEG:
		/* write */
		status = nfsd4_encode_open_write_delegation4(xdr, open);
		break;
	case OPEN_DELEGATE_NONE_EXT:
		/* od_whynone */
		status = nfsd4_encode_open_none_delegation4(xdr, open);
		break;
	default:
		status = nfserr_serverfault;
	}

	return status;
}

static __be32
nfsd4_encode_open(struct nfsd4_compoundres *resp, __be32 nfserr,
		  union nfsd4_op_u *u)
{
	struct nfsd4_open *open = &u->open;
	struct xdr_stream *xdr = resp->xdr;

	/* stateid */
	nfserr = nfsd4_encode_stateid4(xdr, &open->op_stateid);
	if (nfserr != nfs_ok)
		return nfserr;
	/* cinfo */
	nfserr = nfsd4_encode_change_info4(xdr, &open->op_cinfo);
	if (nfserr != nfs_ok)
		return nfserr;
	/* rflags */
	nfserr = nfsd4_encode_uint32_t(xdr, open->op_rflags);
	if (nfserr != nfs_ok)
		return nfserr;
	/* attrset */
	nfserr = nfsd4_encode_bitmap4(xdr, open->op_bmval[0],
				      open->op_bmval[1], open->op_bmval[2]);
	if (nfserr != nfs_ok)
		return nfserr;
	/* delegation */
	return nfsd4_encode_open_delegation4(xdr, open);
}

static __be32
nfsd4_encode_open_confirm(struct nfsd4_compoundres *resp, __be32 nfserr,
			  union nfsd4_op_u *u)
{
	struct nfsd4_open_confirm *oc = &u->open_confirm;
	struct xdr_stream *xdr = resp->xdr;

	/* open_stateid */
	return nfsd4_encode_stateid4(xdr, &oc->oc_resp_stateid);
}

static __be32
nfsd4_encode_open_downgrade(struct nfsd4_compoundres *resp, __be32 nfserr,
			    union nfsd4_op_u *u)
{
	struct nfsd4_open_downgrade *od = &u->open_downgrade;
	struct xdr_stream *xdr = resp->xdr;

	/* open_stateid */
	return nfsd4_encode_stateid4(xdr, &od->od_stateid);
}

/*
 * The operation of this function assumes that this is the only
 * READ operation in the COMPOUND. If there are multiple READs,
 * we use nfsd4_encode_readv().
 */
static __be32 nfsd4_encode_splice_read(
				struct nfsd4_compoundres *resp,
				struct nfsd4_read *read,
				struct file *file, unsigned long maxcount)
{
	struct xdr_stream *xdr = resp->xdr;
	struct xdr_buf *buf = xdr->buf;
	int status, space_left;
	__be32 nfserr;

	/*
	 * Splice read doesn't work if encoding has already wandered
	 * into the XDR buf's page array.
	 */
	if (unlikely(xdr->buf->page_len)) {
		WARN_ON_ONCE(1);
		return nfserr_serverfault;
	}

	/*
	 * Make sure there is room at the end of buf->head for
	 * svcxdr_encode_opaque_pages() to create a tail buffer
	 * to XDR-pad the payload.
	 */
	if (xdr->iov != xdr->buf->head || xdr->end - xdr->p < 1)
		return nfserr_resource;

	nfserr = nfsd_splice_read(read->rd_rqstp, read->rd_fhp,
				  file, read->rd_offset, &maxcount,
				  &read->rd_eof);
	read->rd_length = maxcount;
	if (nfserr)
		goto out_err;
	svcxdr_encode_opaque_pages(read->rd_rqstp, xdr, buf->pages,
				   buf->page_base, maxcount);
	status = svc_encode_result_payload(read->rd_rqstp,
					   buf->head[0].iov_len, maxcount);
	if (status) {
		nfserr = nfserrno(status);
		goto out_err;
	}

	/*
	 * Prepare to encode subsequent operations.
	 *
	 * xdr_truncate_encode() is not safe to use after a successful
	 * splice read has been done, so the following stream
	 * manipulations are open-coded.
	 */
	space_left = min_t(int, (void *)xdr->end - (void *)xdr->p,
				buf->buflen - buf->len);
	buf->buflen = buf->len + space_left;
	xdr->end = (__be32 *)((void *)xdr->end + space_left);

	return nfs_ok;

out_err:
	/*
	 * nfsd_splice_actor may have already messed with the
	 * page length; reset it so as not to confuse
	 * xdr_truncate_encode in our caller.
	 */
	buf->page_len = 0;
	return nfserr;
}

static __be32 nfsd4_encode_readv(struct nfsd4_compoundres *resp,
				 struct nfsd4_read *read,
				 struct file *file, unsigned long maxcount)
{
	struct xdr_stream *xdr = resp->xdr;
	unsigned int base = xdr->buf->page_len & ~PAGE_MASK;
	unsigned int starting_len = xdr->buf->len;
	__be32 zero = xdr_zero;
	__be32 nfserr;

	if (xdr_reserve_space_vec(xdr, maxcount) < 0)
		return nfserr_resource;

	nfserr = nfsd_iter_read(resp->rqstp, read->rd_fhp, file,
				read->rd_offset, &maxcount, base,
				&read->rd_eof);
	read->rd_length = maxcount;
	if (nfserr)
		return nfserr;
	if (svc_encode_result_payload(resp->rqstp, starting_len, maxcount))
		return nfserr_io;
	xdr_truncate_encode(xdr, starting_len + xdr_align_size(maxcount));

	write_bytes_to_xdr_buf(xdr->buf, starting_len + maxcount, &zero,
			       xdr_pad_size(maxcount));
	return nfs_ok;
}

static __be32
nfsd4_encode_read(struct nfsd4_compoundres *resp, __be32 nfserr,
		  union nfsd4_op_u *u)
{
	struct nfsd4_compoundargs *argp = resp->rqstp->rq_argp;
	struct nfsd4_read *read = &u->read;
	struct xdr_stream *xdr = resp->xdr;
	bool splice_ok = argp->splice_ok;
	unsigned int eof_offset;
	unsigned long maxcount;
	__be32 wire_data[2];
	struct file *file;

	if (nfserr)
		return nfserr;

	eof_offset = xdr->buf->len;
	file = read->rd_nf->nf_file;

	/* Reserve space for the eof flag and byte count */
	if (unlikely(!xdr_reserve_space(xdr, XDR_UNIT * 2))) {
		WARN_ON_ONCE(splice_ok);
		return nfserr_resource;
	}
	xdr_commit_encode(xdr);

	maxcount = min_t(unsigned long, read->rd_length,
			 (xdr->buf->buflen - xdr->buf->len));

	if (file->f_op->splice_read && splice_ok)
		nfserr = nfsd4_encode_splice_read(resp, read, file, maxcount);
	else
		nfserr = nfsd4_encode_readv(resp, read, file, maxcount);
	if (nfserr) {
		xdr_truncate_encode(xdr, eof_offset);
		return nfserr;
	}

	wire_data[0] = read->rd_eof ? xdr_one : xdr_zero;
	wire_data[1] = cpu_to_be32(read->rd_length);
	write_bytes_to_xdr_buf(xdr->buf, eof_offset, &wire_data, XDR_UNIT * 2);
	return nfs_ok;
}

static __be32
nfsd4_encode_readlink(struct nfsd4_compoundres *resp, __be32 nfserr,
		      union nfsd4_op_u *u)
{
	struct nfsd4_readlink *readlink = &u->readlink;
	__be32 *p, wire_count, zero = xdr_zero;
	struct xdr_stream *xdr = resp->xdr;
	unsigned int length_offset;
	int maxcount, status;

	/* linktext4.count */
	length_offset = xdr->buf->len;
	if (unlikely(!xdr_reserve_space(xdr, XDR_UNIT)))
		return nfserr_resource;

	/* linktext4.data */
	maxcount = PAGE_SIZE;
	p = xdr_reserve_space(xdr, maxcount);
	if (!p)
		return nfserr_resource;
	nfserr = nfsd_readlink(readlink->rl_rqstp, readlink->rl_fhp,
						(char *)p, &maxcount);
	if (nfserr == nfserr_isdir)
		nfserr = nfserr_inval;
	if (nfserr)
		goto out_err;
	status = svc_encode_result_payload(readlink->rl_rqstp, length_offset,
					   maxcount);
	if (status) {
		nfserr = nfserrno(status);
		goto out_err;
	}

	wire_count = cpu_to_be32(maxcount);
	write_bytes_to_xdr_buf(xdr->buf, length_offset, &wire_count, XDR_UNIT);
	xdr_truncate_encode(xdr, length_offset + 4 + xdr_align_size(maxcount));
	write_bytes_to_xdr_buf(xdr->buf, length_offset + 4 + maxcount, &zero,
			       xdr_pad_size(maxcount));
	return nfs_ok;

out_err:
	xdr_truncate_encode(xdr, length_offset);
	return nfserr;
}

static __be32 nfsd4_encode_dirlist4(struct xdr_stream *xdr,
				    struct nfsd4_readdir *readdir,
				    u32 max_payload)
{
	int bytes_left, maxcount, starting_len = xdr->buf->len;
	loff_t offset;
	__be32 status;

	/*
	 * Number of bytes left for directory entries allowing for the
	 * final 8 bytes of the readdir and a following failed op.
	 */
	bytes_left = xdr->buf->buflen - xdr->buf->len -
		COMPOUND_ERR_SLACK_SPACE - XDR_UNIT * 2;
	if (bytes_left < 0)
		return nfserr_resource;
	maxcount = min_t(u32, readdir->rd_maxcount, max_payload);

	/*
	 * The RFC defines rd_maxcount as the size of the
	 * READDIR4resok structure, which includes the verifier
	 * and the 8 bytes encoded at the end of this function.
	 */
	if (maxcount < XDR_UNIT * 4)
		return nfserr_toosmall;
	maxcount = min_t(int, maxcount - XDR_UNIT * 4, bytes_left);

	/* RFC 3530 14.2.24 allows us to ignore dircount when it's 0 */
	if (!readdir->rd_dircount)
		readdir->rd_dircount = max_payload;

	/* *entries */
	readdir->xdr = xdr;
	readdir->rd_maxcount = maxcount;
	readdir->common.err = 0;
	readdir->cookie_offset = 0;
	offset = readdir->rd_cookie;
	status = nfsd_readdir(readdir->rd_rqstp, readdir->rd_fhp, &offset,
			      &readdir->common, nfsd4_encode_entry4);
	if (status)
		return status;
	if (readdir->common.err == nfserr_toosmall &&
	    xdr->buf->len == starting_len) {
		/* No entries were encoded. Which limit did we hit? */
		if (maxcount - XDR_UNIT * 4 < bytes_left)
			/* It was the fault of rd_maxcount */
			return nfserr_toosmall;
		/* We ran out of buffer space */
		return nfserr_resource;
	}
	/* Encode the final entry's cookie value */
	nfsd4_encode_entry4_nfs_cookie4(readdir, offset);
	/* No entries follow */
	if (xdr_stream_encode_item_absent(xdr) != XDR_UNIT)
		return nfserr_resource;

	/* eof */
	return nfsd4_encode_bool(xdr, readdir->common.err == nfserr_eof);
}

static __be32
nfsd4_encode_readdir(struct nfsd4_compoundres *resp, __be32 nfserr,
		     union nfsd4_op_u *u)
{
	struct nfsd4_readdir *readdir = &u->readdir;
	struct xdr_stream *xdr = resp->xdr;
	int starting_len = xdr->buf->len;

	/* cookieverf */
	nfserr = nfsd4_encode_verifier4(xdr, &readdir->rd_verf);
	if (nfserr != nfs_ok)
		return nfserr;

	/* reply */
	nfserr = nfsd4_encode_dirlist4(xdr, readdir, svc_max_payload(resp->rqstp));
	if (nfserr != nfs_ok)
		xdr_truncate_encode(xdr, starting_len);
	return nfserr;
}

static __be32
nfsd4_encode_remove(struct nfsd4_compoundres *resp, __be32 nfserr,
		    union nfsd4_op_u *u)
{
	struct nfsd4_remove *remove = &u->remove;
	struct xdr_stream *xdr = resp->xdr;

	return nfsd4_encode_change_info4(xdr, &remove->rm_cinfo);
}

static __be32
nfsd4_encode_rename(struct nfsd4_compoundres *resp, __be32 nfserr,
		    union nfsd4_op_u *u)
{
	struct nfsd4_rename *rename = &u->rename;
	struct xdr_stream *xdr = resp->xdr;

	nfserr = nfsd4_encode_change_info4(xdr, &rename->rn_sinfo);
	if (nfserr)
		return nfserr;
	return nfsd4_encode_change_info4(xdr, &rename->rn_tinfo);
}

static __be32
nfsd4_encode_rpcsec_gss_info(struct xdr_stream *xdr,
			     struct rpcsec_gss_info *info)
{
	__be32 status;

	/* oid */
	if (xdr_stream_encode_opaque(xdr, info->oid.data, info->oid.len) < 0)
		return nfserr_resource;
	/* qop */
	status = nfsd4_encode_qop4(xdr, info->qop);
	if (status != nfs_ok)
		return status;
	/* service */
	if (xdr_stream_encode_u32(xdr, info->service) != XDR_UNIT)
		return nfserr_resource;

	return nfs_ok;
}

static __be32
nfsd4_encode_secinfo4(struct xdr_stream *xdr, rpc_authflavor_t pf,
		      u32 *supported)
{
	struct rpcsec_gss_info info;
	__be32 status;

	if (rpcauth_get_gssinfo(pf, &info) == 0) {
		(*supported)++;

		/* flavor */
		status = nfsd4_encode_uint32_t(xdr, RPC_AUTH_GSS);
		if (status != nfs_ok)
			return status;
		/* flavor_info */
		status = nfsd4_encode_rpcsec_gss_info(xdr, &info);
		if (status != nfs_ok)
			return status;
	} else if (pf < RPC_AUTH_MAXFLAVOR) {
		(*supported)++;

		/* flavor */
		status = nfsd4_encode_uint32_t(xdr, pf);
		if (status != nfs_ok)
			return status;
	}
	return nfs_ok;
}

static __be32
nfsd4_encode_SECINFO4resok(struct xdr_stream *xdr, struct svc_export *exp)
{
	u32 i, nflavs, supported;
	struct exp_flavor_info *flavs;
	struct exp_flavor_info def_flavs[2];
	unsigned int count_offset;
	__be32 status, wire_count;

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

	count_offset = xdr->buf->len;
	if (unlikely(!xdr_reserve_space(xdr, XDR_UNIT)))
		return nfserr_resource;

	for (i = 0, supported = 0; i < nflavs; i++) {
		status = nfsd4_encode_secinfo4(xdr, flavs[i].pseudoflavor,
					       &supported);
		if (status != nfs_ok)
			return status;
	}

	wire_count = cpu_to_be32(supported);
	write_bytes_to_xdr_buf(xdr->buf, count_offset, &wire_count,
			       XDR_UNIT);
	return 0;
}

static __be32
nfsd4_encode_secinfo(struct nfsd4_compoundres *resp, __be32 nfserr,
		     union nfsd4_op_u *u)
{
	struct nfsd4_secinfo *secinfo = &u->secinfo;
	struct xdr_stream *xdr = resp->xdr;

	return nfsd4_encode_SECINFO4resok(xdr, secinfo->si_exp);
}

static __be32
nfsd4_encode_secinfo_no_name(struct nfsd4_compoundres *resp, __be32 nfserr,
		     union nfsd4_op_u *u)
{
	struct nfsd4_secinfo_no_name *secinfo = &u->secinfo_no_name;
	struct xdr_stream *xdr = resp->xdr;

	return nfsd4_encode_SECINFO4resok(xdr, secinfo->sin_exp);
}

static __be32
nfsd4_encode_setattr(struct nfsd4_compoundres *resp, __be32 nfserr,
		     union nfsd4_op_u *u)
{
	struct nfsd4_setattr *setattr = &u->setattr;
	__be32 status;

	switch (nfserr) {
	case nfs_ok:
		/* attrsset */
		status = nfsd4_encode_bitmap4(resp->xdr, setattr->sa_bmval[0],
					      setattr->sa_bmval[1],
					      setattr->sa_bmval[2]);
		break;
	default:
		/* attrsset */
		status = nfsd4_encode_bitmap4(resp->xdr, 0, 0, 0);
	}
	return status != nfs_ok ? status : nfserr;
}

static __be32
nfsd4_encode_setclientid(struct nfsd4_compoundres *resp, __be32 nfserr,
			 union nfsd4_op_u *u)
{
	struct nfsd4_setclientid *scd = &u->setclientid;
	struct xdr_stream *xdr = resp->xdr;

	if (!nfserr) {
		nfserr = nfsd4_encode_clientid4(xdr, &scd->se_clientid);
		if (nfserr != nfs_ok)
			goto out;
		nfserr = nfsd4_encode_verifier4(xdr, &scd->se_confirm);
	} else if (nfserr == nfserr_clid_inuse) {
		/* empty network id */
		if (xdr_stream_encode_u32(xdr, 0) < 0) {
			nfserr = nfserr_resource;
			goto out;
		}
		/* empty universal address */
		if (xdr_stream_encode_u32(xdr, 0) < 0) {
			nfserr = nfserr_resource;
			goto out;
		}
	}
out:
	return nfserr;
}

static __be32
nfsd4_encode_write(struct nfsd4_compoundres *resp, __be32 nfserr,
		   union nfsd4_op_u *u)
{
	struct nfsd4_write *write = &u->write;
	struct xdr_stream *xdr = resp->xdr;

	/* count */
	nfserr = nfsd4_encode_count4(xdr, write->wr_bytes_written);
	if (nfserr)
		return nfserr;
	/* committed */
	if (xdr_stream_encode_u32(xdr, write->wr_how_written) != XDR_UNIT)
		return nfserr_resource;
	/* writeverf */
	return nfsd4_encode_verifier4(xdr, &write->wr_verifier);
}

static __be32
nfsd4_encode_state_protect_ops4(struct xdr_stream *xdr,
				struct nfsd4_exchange_id *exid)
{
	__be32 status;

	/* spo_must_enforce */
	status = nfsd4_encode_bitmap4(xdr, exid->spo_must_enforce[0],
				      exid->spo_must_enforce[1],
				      exid->spo_must_enforce[2]);
	if (status != nfs_ok)
		return status;
	/* spo_must_allow */
	return nfsd4_encode_bitmap4(xdr, exid->spo_must_allow[0],
				    exid->spo_must_allow[1],
				    exid->spo_must_allow[2]);
}

static __be32
nfsd4_encode_state_protect4_r(struct xdr_stream *xdr, struct nfsd4_exchange_id *exid)
{
	__be32 status;

	if (xdr_stream_encode_u32(xdr, exid->spa_how) != XDR_UNIT)
		return nfserr_resource;
	switch (exid->spa_how) {
	case SP4_NONE:
		status = nfs_ok;
		break;
	case SP4_MACH_CRED:
		/* spr_mach_ops */
		status = nfsd4_encode_state_protect_ops4(xdr, exid);
		break;
	default:
		status = nfserr_serverfault;
	}
	return status;
}

static __be32
nfsd4_encode_server_owner4(struct xdr_stream *xdr, struct svc_rqst *rqstp)
{
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	__be32 status;

	/* so_minor_id */
	status = nfsd4_encode_uint64_t(xdr, 0);
	if (status != nfs_ok)
		return status;
	/* so_major_id */
	return nfsd4_encode_opaque(xdr, nn->nfsd_name, strlen(nn->nfsd_name));
}

static __be32
nfsd4_encode_nfs_impl_id4(struct xdr_stream *xdr, struct nfsd4_exchange_id *exid)
{
	__be32 status;

	/* nii_domain */
	status = nfsd4_encode_opaque(xdr, exid->nii_domain.data,
				     exid->nii_domain.len);
	if (status != nfs_ok)
		return status;
	/* nii_name */
	status = nfsd4_encode_opaque(xdr, exid->nii_name.data,
				     exid->nii_name.len);
	if (status != nfs_ok)
		return status;
	/* nii_time */
	return nfsd4_encode_nfstime4(xdr, &exid->nii_time);
}

static __be32
nfsd4_encode_exchange_id(struct nfsd4_compoundres *resp, __be32 nfserr,
			 union nfsd4_op_u *u)
{
	struct nfsd_net *nn = net_generic(SVC_NET(resp->rqstp), nfsd_net_id);
	struct nfsd4_exchange_id *exid = &u->exchange_id;
	struct xdr_stream *xdr = resp->xdr;

	/* eir_clientid */
	nfserr = nfsd4_encode_clientid4(xdr, &exid->clientid);
	if (nfserr != nfs_ok)
		return nfserr;
	/* eir_sequenceid */
	nfserr = nfsd4_encode_sequenceid4(xdr, exid->seqid);
	if (nfserr != nfs_ok)
		return nfserr;
	/* eir_flags */
	nfserr = nfsd4_encode_uint32_t(xdr, exid->flags);
	if (nfserr != nfs_ok)
		return nfserr;
	/* eir_state_protect */
	nfserr = nfsd4_encode_state_protect4_r(xdr, exid);
	if (nfserr != nfs_ok)
		return nfserr;
	/* eir_server_owner */
	nfserr = nfsd4_encode_server_owner4(xdr, resp->rqstp);
	if (nfserr != nfs_ok)
		return nfserr;
	/* eir_server_scope */
	nfserr = nfsd4_encode_opaque(xdr, nn->nfsd_name,
				     strlen(nn->nfsd_name));
	if (nfserr != nfs_ok)
		return nfserr;
	/* eir_server_impl_id<1> */
	if (xdr_stream_encode_u32(xdr, 1) != XDR_UNIT)
		return nfserr_resource;
	nfserr = nfsd4_encode_nfs_impl_id4(xdr, exid);
	if (nfserr != nfs_ok)
		return nfserr;

	return nfs_ok;
}

static __be32
nfsd4_encode_channel_attrs4(struct xdr_stream *xdr,
			    const struct nfsd4_channel_attrs *attrs)
{
	__be32 status;

	/* ca_headerpadsize */
	status = nfsd4_encode_count4(xdr, 0);
	if (status != nfs_ok)
		return status;
	/* ca_maxrequestsize */
	status = nfsd4_encode_count4(xdr, attrs->maxreq_sz);
	if (status != nfs_ok)
		return status;
	/* ca_maxresponsesize */
	status = nfsd4_encode_count4(xdr, attrs->maxresp_sz);
	if (status != nfs_ok)
		return status;
	/* ca_maxresponsesize_cached */
	status = nfsd4_encode_count4(xdr, attrs->maxresp_cached);
	if (status != nfs_ok)
		return status;
	/* ca_maxoperations */
	status = nfsd4_encode_count4(xdr, attrs->maxops);
	if (status != nfs_ok)
		return status;
	/* ca_maxrequests */
	status = nfsd4_encode_count4(xdr, attrs->maxreqs);
	if (status != nfs_ok)
		return status;
	/* ca_rdma_ird<1> */
	if (xdr_stream_encode_u32(xdr, attrs->nr_rdma_attrs) != XDR_UNIT)
		return nfserr_resource;
	if (attrs->nr_rdma_attrs)
		return nfsd4_encode_uint32_t(xdr, attrs->rdma_attrs);
	return nfs_ok;
}

static __be32
nfsd4_encode_create_session(struct nfsd4_compoundres *resp, __be32 nfserr,
			    union nfsd4_op_u *u)
{
	struct nfsd4_create_session *sess = &u->create_session;
	struct xdr_stream *xdr = resp->xdr;

	/* csr_sessionid */
	nfserr = nfsd4_encode_sessionid4(xdr, &sess->sessionid);
	if (nfserr != nfs_ok)
		return nfserr;
	/* csr_sequence */
	nfserr = nfsd4_encode_sequenceid4(xdr, sess->seqid);
	if (nfserr != nfs_ok)
		return nfserr;
	/* csr_flags */
	nfserr = nfsd4_encode_uint32_t(xdr, sess->flags);
	if (nfserr != nfs_ok)
		return nfserr;
	/* csr_fore_chan_attrs */
	nfserr = nfsd4_encode_channel_attrs4(xdr, &sess->fore_channel);
	if (nfserr != nfs_ok)
		return nfserr;
	/* csr_back_chan_attrs */
	return nfsd4_encode_channel_attrs4(xdr, &sess->back_channel);
}

static __be32
nfsd4_encode_sequence(struct nfsd4_compoundres *resp, __be32 nfserr,
		      union nfsd4_op_u *u)
{
	struct nfsd4_sequence *seq = &u->sequence;
	struct xdr_stream *xdr = resp->xdr;

	/* sr_sessionid */
	nfserr = nfsd4_encode_sessionid4(xdr, &seq->sessionid);
	if (nfserr != nfs_ok)
		return nfserr;
	/* sr_sequenceid */
	nfserr = nfsd4_encode_sequenceid4(xdr, seq->seqid);
	if (nfserr != nfs_ok)
		return nfserr;
	/* sr_slotid */
	nfserr = nfsd4_encode_slotid4(xdr, seq->slotid);
	if (nfserr != nfs_ok)
		return nfserr;
	/* Note slotid's are numbered from zero: */
	/* sr_highest_slotid */
	nfserr = nfsd4_encode_slotid4(xdr, seq->maxslots - 1);
	if (nfserr != nfs_ok)
		return nfserr;
	/* sr_target_highest_slotid */
	nfserr = nfsd4_encode_slotid4(xdr, seq->target_maxslots - 1);
	if (nfserr != nfs_ok)
		return nfserr;
	/* sr_status_flags */
	nfserr = nfsd4_encode_uint32_t(xdr, seq->status_flags);
	if (nfserr != nfs_ok)
		return nfserr;

	resp->cstate.data_offset = xdr->buf->len; /* DRC cache data pointer */
	return nfs_ok;
}

static __be32
nfsd4_encode_test_stateid(struct nfsd4_compoundres *resp, __be32 nfserr,
			  union nfsd4_op_u *u)
{
	struct nfsd4_test_stateid *test_stateid = &u->test_stateid;
	struct nfsd4_test_stateid_id *stateid, *next;
	struct xdr_stream *xdr = resp->xdr;

	/* tsr_status_codes<> */
	if (xdr_stream_encode_u32(xdr, test_stateid->ts_num_ids) != XDR_UNIT)
		return nfserr_resource;
	list_for_each_entry_safe(stateid, next,
				 &test_stateid->ts_stateid_list, ts_id_list) {
		if (xdr_stream_encode_be32(xdr, stateid->ts_id_status) != XDR_UNIT)
			return nfserr_resource;
	}
	return nfs_ok;
}

static __be32
nfsd4_encode_get_dir_delegation(struct nfsd4_compoundres *resp, __be32 nfserr,
				union nfsd4_op_u *u)
{
	struct nfsd4_get_dir_delegation *gdd = &u->get_dir_delegation;
	struct xdr_stream *xdr = resp->xdr;
	__be32 status = nfserr_resource;

	switch(gdd->gddrnf_status) {
	case GDD4_OK:
		if (xdr_stream_encode_u32(xdr, GDD4_OK) != XDR_UNIT)
			break;
		status = nfsd4_encode_verifier4(xdr, &gdd->gddr_cookieverf);
		if (status)
			break;
		status = nfsd4_encode_stateid4(xdr, &gdd->gddr_stateid);
		if (status)
			break;
		status = nfsd4_encode_bitmap4(xdr, gdd->gddr_notification[0], 0, 0);
		if (status)
			break;
		status = nfsd4_encode_bitmap4(xdr, gdd->gddr_child_attributes[0],
						   gdd->gddr_child_attributes[1],
						   gdd->gddr_child_attributes[2]);
		if (status)
			break;
		status = nfsd4_encode_bitmap4(xdr, gdd->gddr_dir_attributes[0],
						   gdd->gddr_dir_attributes[1],
						   gdd->gddr_dir_attributes[2]);
		break;
	default:
		pr_warn("nfsd: bad gddrnf_status (%u)\n", gdd->gddrnf_status);
		gdd->gddrnf_will_signal_deleg_avail = 0;
		fallthrough;
	case GDD4_UNAVAIL:
		if (xdr_stream_encode_u32(xdr, GDD4_UNAVAIL) != XDR_UNIT)
			break;
		status = nfsd4_encode_bool(xdr, gdd->gddrnf_will_signal_deleg_avail);
		break;
	}
	return status;
}

#ifdef CONFIG_NFSD_PNFS
static __be32
nfsd4_encode_device_addr4(struct xdr_stream *xdr,
			  const struct nfsd4_getdeviceinfo *gdev)
{
	u32 needed_len, starting_len = xdr->buf->len;
	const struct nfsd4_layout_ops *ops;
	__be32 status;

	/* da_layout_type */
	if (xdr_stream_encode_u32(xdr, gdev->gd_layout_type) != XDR_UNIT)
		return nfserr_resource;
	/* da_addr_body */
	ops = nfsd4_layout_ops[gdev->gd_layout_type];
	status = ops->encode_getdeviceinfo(xdr, gdev);
	if (status != nfs_ok) {
		/*
		 * Don't burden the layout drivers with enforcing
		 * gd_maxcount. Just tell the client to come back
		 * with a bigger buffer if it's not enough.
		 */
		if (xdr->buf->len + XDR_UNIT > gdev->gd_maxcount)
			goto toosmall;
		return status;
	}

	return nfs_ok;

toosmall:
	needed_len = xdr->buf->len + XDR_UNIT;	/* notifications */
	xdr_truncate_encode(xdr, starting_len);

	status = nfsd4_encode_count4(xdr, needed_len);
	if (status != nfs_ok)
		return status;
	return nfserr_toosmall;
}

static __be32
nfsd4_encode_getdeviceinfo(struct nfsd4_compoundres *resp, __be32 nfserr,
		union nfsd4_op_u *u)
{
	struct nfsd4_getdeviceinfo *gdev = &u->getdeviceinfo;
	struct xdr_stream *xdr = resp->xdr;

	/* gdir_device_addr */
	nfserr = nfsd4_encode_device_addr4(xdr, gdev);
	if (nfserr)
		return nfserr;
	/* gdir_notification */
	return nfsd4_encode_bitmap4(xdr, gdev->gd_notify_types, 0, 0);
}

static __be32
nfsd4_encode_layout4(struct xdr_stream *xdr, const struct nfsd4_layoutget *lgp)
{
	const struct nfsd4_layout_ops *ops = nfsd4_layout_ops[lgp->lg_layout_type];
	__be32 status;

	/* lo_offset */
	status = nfsd4_encode_offset4(xdr, lgp->lg_seg.offset);
	if (status != nfs_ok)
		return status;
	/* lo_length */
	status = nfsd4_encode_length4(xdr, lgp->lg_seg.length);
	if (status != nfs_ok)
		return status;
	/* lo_iomode */
	if (xdr_stream_encode_u32(xdr, lgp->lg_seg.iomode) != XDR_UNIT)
		return nfserr_resource;
	/* lo_content */
	if (xdr_stream_encode_u32(xdr, lgp->lg_layout_type) != XDR_UNIT)
		return nfserr_resource;
	return ops->encode_layoutget(xdr, lgp);
}

static __be32
nfsd4_encode_layoutget(struct nfsd4_compoundres *resp, __be32 nfserr,
		union nfsd4_op_u *u)
{
	struct nfsd4_layoutget *lgp = &u->layoutget;
	struct xdr_stream *xdr = resp->xdr;

	/* logr_return_on_close */
	nfserr = nfsd4_encode_bool(xdr, true);
	if (nfserr != nfs_ok)
		return nfserr;
	/* logr_stateid */
	nfserr = nfsd4_encode_stateid4(xdr, &lgp->lg_sid);
	if (nfserr != nfs_ok)
		return nfserr;
	/* logr_layout<> */
	if (xdr_stream_encode_u32(xdr, 1) != XDR_UNIT)
		return nfserr_resource;
	return nfsd4_encode_layout4(xdr, lgp);
}

static __be32
nfsd4_encode_layoutcommit(struct nfsd4_compoundres *resp, __be32 nfserr,
			  union nfsd4_op_u *u)
{
	struct nfsd4_layoutcommit *lcp = &u->layoutcommit;
	struct xdr_stream *xdr = resp->xdr;

	/* ns_sizechanged */
	nfserr = nfsd4_encode_bool(xdr, lcp->lc_size_chg);
	if (nfserr != nfs_ok)
		return nfserr;
	if (lcp->lc_size_chg)
		/* ns_size */
		return nfsd4_encode_length4(xdr, lcp->lc_newsize);
	return nfs_ok;
}

static __be32
nfsd4_encode_layoutreturn(struct nfsd4_compoundres *resp, __be32 nfserr,
		union nfsd4_op_u *u)
{
	struct nfsd4_layoutreturn *lrp = &u->layoutreturn;
	struct xdr_stream *xdr = resp->xdr;

	/* lrs_present */
	nfserr = nfsd4_encode_bool(xdr, lrp->lrs_present);
	if (nfserr != nfs_ok)
		return nfserr;
	if (lrp->lrs_present)
		/* lrs_stateid */
		return nfsd4_encode_stateid4(xdr, &lrp->lr_sid);
	return nfs_ok;
}
#endif /* CONFIG_NFSD_PNFS */

static __be32
nfsd4_encode_write_response4(struct xdr_stream *xdr,
			     const struct nfsd4_copy *copy)
{
	const struct nfsd42_write_res *write = &copy->cp_res;
	u32 count = nfsd4_copy_is_sync(copy) ? 0 : 1;
	__be32 status;

	/* wr_callback_id<1> */
	if (xdr_stream_encode_u32(xdr, count) != XDR_UNIT)
		return nfserr_resource;
	if (count) {
		status = nfsd4_encode_stateid4(xdr, &write->cb_stateid);
		if (status != nfs_ok)
			return status;
	}

	/* wr_count */
	status = nfsd4_encode_length4(xdr, write->wr_bytes_written);
	if (status != nfs_ok)
		return status;
	/* wr_committed */
	if (xdr_stream_encode_u32(xdr, write->wr_stable_how) != XDR_UNIT)
		return nfserr_resource;
	/* wr_writeverf */
	return nfsd4_encode_verifier4(xdr, &write->wr_verifier);
}

static __be32 nfsd4_encode_copy_requirements4(struct xdr_stream *xdr,
					      const struct nfsd4_copy *copy)
{
	__be32 status;

	/* cr_consecutive */
	status = nfsd4_encode_bool(xdr, true);
	if (status != nfs_ok)
		return status;
	/* cr_synchronous */
	return nfsd4_encode_bool(xdr, nfsd4_copy_is_sync(copy));
}

static __be32
nfsd4_encode_copy(struct nfsd4_compoundres *resp, __be32 nfserr,
		  union nfsd4_op_u *u)
{
	struct nfsd4_copy *copy = &u->copy;

	nfserr = nfsd4_encode_write_response4(resp->xdr, copy);
	if (nfserr != nfs_ok)
		return nfserr;
	return nfsd4_encode_copy_requirements4(resp->xdr, copy);
}

static __be32
nfsd4_encode_netloc4(struct xdr_stream *xdr, const struct nl4_server *ns)
{
	__be32 status;

	if (xdr_stream_encode_u32(xdr, ns->nl4_type) != XDR_UNIT)
		return nfserr_resource;
	switch (ns->nl4_type) {
	case NL4_NETADDR:
		/* nl_addr */
		status = nfsd4_encode_netaddr4(xdr, &ns->u.nl4_addr);
		break;
	default:
		status = nfserr_serverfault;
	}
	return status;
}

static __be32
nfsd4_encode_copy_notify(struct nfsd4_compoundres *resp, __be32 nfserr,
			 union nfsd4_op_u *u)
{
	struct nfsd4_copy_notify *cn = &u->copy_notify;
	struct xdr_stream *xdr = resp->xdr;

	/* cnr_lease_time */
	nfserr = nfsd4_encode_nfstime4(xdr, &cn->cpn_lease_time);
	if (nfserr)
		return nfserr;
	/* cnr_stateid */
	nfserr = nfsd4_encode_stateid4(xdr, &cn->cpn_cnr_stateid);
	if (nfserr)
		return nfserr;
	/* cnr_source_server<> */
	if (xdr_stream_encode_u32(xdr, 1) != XDR_UNIT)
		return nfserr_resource;
	return nfsd4_encode_netloc4(xdr, cn->cpn_src);
}

static __be32
nfsd4_encode_offload_status(struct nfsd4_compoundres *resp, __be32 nfserr,
			    union nfsd4_op_u *u)
{
	struct nfsd4_offload_status *os = &u->offload_status;
	struct xdr_stream *xdr = resp->xdr;

	/* osr_count */
	nfserr = nfsd4_encode_length4(xdr, os->count);
	if (nfserr != nfs_ok)
		return nfserr;
	/* osr_complete<1> */
	if (os->completed) {
		if (xdr_stream_encode_u32(xdr, 1) != XDR_UNIT)
			return nfserr_resource;
		if (xdr_stream_encode_be32(xdr, os->status) != XDR_UNIT)
			return nfserr_resource;
	} else if (xdr_stream_encode_u32(xdr, 0) != XDR_UNIT)
		return nfserr_resource;
	return nfs_ok;
}

static __be32
nfsd4_encode_read_plus_data(struct nfsd4_compoundres *resp,
			    struct nfsd4_read *read)
{
	struct nfsd4_compoundargs *argp = resp->rqstp->rq_argp;
	struct file *file = read->rd_nf->nf_file;
	struct xdr_stream *xdr = resp->xdr;
	bool splice_ok = argp->splice_ok;
	unsigned int offset_offset;
	__be32 nfserr, wire_count;
	unsigned long maxcount;
	__be64 wire_offset;

	if (xdr_stream_encode_u32(xdr, NFS4_CONTENT_DATA) != XDR_UNIT)
		return nfserr_io;

	offset_offset = xdr->buf->len;

	/* Reserve space for the byte offset and count */
	if (unlikely(!xdr_reserve_space(xdr, XDR_UNIT * 3)))
		return nfserr_io;
	xdr_commit_encode(xdr);

	maxcount = min_t(unsigned long, read->rd_length,
			 (xdr->buf->buflen - xdr->buf->len));

	if (file->f_op->splice_read && splice_ok)
		nfserr = nfsd4_encode_splice_read(resp, read, file, maxcount);
	else
		nfserr = nfsd4_encode_readv(resp, read, file, maxcount);
	if (nfserr)
		return nfserr;

	wire_offset = cpu_to_be64(read->rd_offset);
	write_bytes_to_xdr_buf(xdr->buf, offset_offset, &wire_offset,
			       XDR_UNIT * 2);
	wire_count = cpu_to_be32(read->rd_length);
	write_bytes_to_xdr_buf(xdr->buf, offset_offset + XDR_UNIT * 2,
			       &wire_count, XDR_UNIT);
	return nfs_ok;
}

static __be32
nfsd4_encode_read_plus(struct nfsd4_compoundres *resp, __be32 nfserr,
		       union nfsd4_op_u *u)
{
	struct nfsd4_read *read = &u->read;
	struct file *file = read->rd_nf->nf_file;
	struct xdr_stream *xdr = resp->xdr;
	unsigned int eof_offset;
	__be32 wire_data[2];
	u32 segments = 0;

	if (nfserr)
		return nfserr;

	eof_offset = xdr->buf->len;

	/* Reserve space for the eof flag and segment count */
	if (unlikely(!xdr_reserve_space(xdr, XDR_UNIT * 2)))
		return nfserr_io;
	xdr_commit_encode(xdr);

	read->rd_eof = read->rd_offset >= i_size_read(file_inode(file));
	if (read->rd_eof)
		goto out;

	nfserr = nfsd4_encode_read_plus_data(resp, read);
	if (nfserr) {
		xdr_truncate_encode(xdr, eof_offset);
		return nfserr;
	}

	segments++;

out:
	wire_data[0] = read->rd_eof ? xdr_one : xdr_zero;
	wire_data[1] = cpu_to_be32(segments);
	write_bytes_to_xdr_buf(xdr->buf, eof_offset, &wire_data, XDR_UNIT * 2);
	return nfserr;
}

static __be32
nfsd4_encode_seek(struct nfsd4_compoundres *resp, __be32 nfserr,
		  union nfsd4_op_u *u)
{
	struct nfsd4_seek *seek = &u->seek;
	struct xdr_stream *xdr = resp->xdr;

	/* sr_eof */
	nfserr = nfsd4_encode_bool(xdr, seek->seek_eof);
	if (nfserr != nfs_ok)
		return nfserr;
	/* sr_offset */
	return nfsd4_encode_offset4(xdr, seek->seek_pos);
}

static __be32
nfsd4_encode_noop(struct nfsd4_compoundres *resp, __be32 nfserr,
		  union nfsd4_op_u *p)
{
	return nfserr;
}

/*
 * Encode kmalloc-ed buffer in to XDR stream.
 */
static __be32
nfsd4_vbuf_to_stream(struct xdr_stream *xdr, char *buf, u32 buflen)
{
	u32 cplen;
	__be32 *p;

	cplen = min_t(unsigned long, buflen,
		      ((void *)xdr->end - (void *)xdr->p));
	p = xdr_reserve_space(xdr, cplen);
	if (!p)
		return nfserr_resource;

	memcpy(p, buf, cplen);
	buf += cplen;
	buflen -= cplen;

	while (buflen) {
		cplen = min_t(u32, buflen, PAGE_SIZE);
		p = xdr_reserve_space(xdr, cplen);
		if (!p)
			return nfserr_resource;

		memcpy(p, buf, cplen);

		if (cplen < PAGE_SIZE) {
			/*
			 * We're done, with a length that wasn't page
			 * aligned, so possibly not word aligned. Pad
			 * any trailing bytes with 0.
			 */
			xdr_encode_opaque_fixed(p, NULL, cplen);
			break;
		}

		buflen -= PAGE_SIZE;
		buf += PAGE_SIZE;
	}

	return 0;
}

static __be32
nfsd4_encode_getxattr(struct nfsd4_compoundres *resp, __be32 nfserr,
		      union nfsd4_op_u *u)
{
	struct nfsd4_getxattr *getxattr = &u->getxattr;
	struct xdr_stream *xdr = resp->xdr;
	__be32 *p, err;

	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return nfserr_resource;

	*p = cpu_to_be32(getxattr->getxa_len);

	if (getxattr->getxa_len == 0)
		return 0;

	err = nfsd4_vbuf_to_stream(xdr, getxattr->getxa_buf,
				    getxattr->getxa_len);

	kvfree(getxattr->getxa_buf);

	return err;
}

static __be32
nfsd4_encode_setxattr(struct nfsd4_compoundres *resp, __be32 nfserr,
		      union nfsd4_op_u *u)
{
	struct nfsd4_setxattr *setxattr = &u->setxattr;
	struct xdr_stream *xdr = resp->xdr;

	return nfsd4_encode_change_info4(xdr, &setxattr->setxa_cinfo);
}

/*
 * See if there are cookie values that can be rejected outright.
 */
static __be32
nfsd4_listxattr_validate_cookie(struct nfsd4_listxattrs *listxattrs,
				u32 *offsetp)
{
	u64 cookie = listxattrs->lsxa_cookie;

	/*
	 * If the cookie is larger than the maximum number we can fit
	 * in the buffer we just got back from vfs_listxattr, it's invalid.
	 */
	if (cookie > (listxattrs->lsxa_len) / (XATTR_USER_PREFIX_LEN + 2))
		return nfserr_badcookie;

	*offsetp = (u32)cookie;
	return 0;
}

static __be32
nfsd4_encode_listxattrs(struct nfsd4_compoundres *resp, __be32 nfserr,
			union nfsd4_op_u *u)
{
	struct nfsd4_listxattrs *listxattrs = &u->listxattrs;
	struct xdr_stream *xdr = resp->xdr;
	u32 cookie_offset, count_offset, eof;
	u32 left, xdrleft, slen, count;
	u32 xdrlen, offset;
	u64 cookie;
	char *sp;
	__be32 status, tmp;
	__be64 wire_cookie;
	__be32 *p;
	u32 nuser;

	eof = 1;

	status = nfsd4_listxattr_validate_cookie(listxattrs, &offset);
	if (status)
		goto out;

	/*
	 * Reserve space for the cookie and the name array count. Record
	 * the offsets to save them later.
	 */
	cookie_offset = xdr->buf->len;
	count_offset = cookie_offset + 8;
	p = xdr_reserve_space(xdr, XDR_UNIT * 3);
	if (!p) {
		status = nfserr_resource;
		goto out;
	}

	count = 0;
	left = listxattrs->lsxa_len;
	sp = listxattrs->lsxa_buf;
	nuser = 0;

	/* Bytes left is maxcount - 8 (cookie) - 4 (array count) */
	xdrleft = listxattrs->lsxa_maxcount - XDR_UNIT * 3;

	while (left > 0 && xdrleft > 0) {
		slen = strlen(sp);

		/*
		 * Check if this is a "user." attribute, skip it if not.
		 */
		if (strncmp(sp, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN))
			goto contloop;

		slen -= XATTR_USER_PREFIX_LEN;
		xdrlen = 4 + ((slen + 3) & ~3);
		/* Check if both entry and eof can fit in the XDR buffer */
		if (xdrlen + XDR_UNIT > xdrleft) {
			if (count == 0) {
				/*
				 * Can't even fit the first attribute name.
				 */
				status = nfserr_toosmall;
				goto out;
			}
			eof = 0;
			goto wreof;
		}

		left -= XATTR_USER_PREFIX_LEN;
		sp += XATTR_USER_PREFIX_LEN;
		if (nuser++ < offset)
			goto contloop;


		p = xdr_reserve_space(xdr, xdrlen);
		if (!p) {
			status = nfserr_resource;
			goto out;
		}

		xdr_encode_opaque(p, sp, slen);

		xdrleft -= xdrlen;
		count++;
contloop:
		sp += slen + 1;
		left -= slen + 1;
	}

	/*
	 * If there were user attributes to copy, but we didn't copy
	 * any, the offset was too large (e.g. the cookie was invalid).
	 */
	if (nuser > 0 && count == 0) {
		status = nfserr_badcookie;
		goto out;
	}

wreof:
	p = xdr_reserve_space(xdr, 4);
	if (!p) {
		status = nfserr_resource;
		goto out;
	}
	*p = cpu_to_be32(eof);

	cookie = offset + count;

	wire_cookie = cpu_to_be64(cookie);
	write_bytes_to_xdr_buf(xdr->buf, cookie_offset, &wire_cookie, 8);
	tmp = cpu_to_be32(count);
	write_bytes_to_xdr_buf(xdr->buf, count_offset, &tmp, 4);
out:
	if (listxattrs->lsxa_len)
		kvfree(listxattrs->lsxa_buf);
	return status;
}

static __be32
nfsd4_encode_removexattr(struct nfsd4_compoundres *resp, __be32 nfserr,
			 union nfsd4_op_u *u)
{
	struct nfsd4_removexattr *removexattr = &u->removexattr;
	struct xdr_stream *xdr = resp->xdr;

	return nfsd4_encode_change_info4(xdr, &removexattr->rmxa_cinfo);
}

typedef __be32(*nfsd4_enc)(struct nfsd4_compoundres *, __be32, union nfsd4_op_u *u);

/*
 * Note: nfsd4_enc_ops vector is shared for v4.0 and v4.1
 * since we don't need to filter out obsolete ops as this is
 * done in the decoding phase.
 */
static const nfsd4_enc nfsd4_enc_ops[] = {
	[OP_ACCESS]		= nfsd4_encode_access,
	[OP_CLOSE]		= nfsd4_encode_close,
	[OP_COMMIT]		= nfsd4_encode_commit,
	[OP_CREATE]		= nfsd4_encode_create,
	[OP_DELEGPURGE]		= nfsd4_encode_noop,
	[OP_DELEGRETURN]	= nfsd4_encode_noop,
	[OP_GETATTR]		= nfsd4_encode_getattr,
	[OP_GETFH]		= nfsd4_encode_getfh,
	[OP_LINK]		= nfsd4_encode_link,
	[OP_LOCK]		= nfsd4_encode_lock,
	[OP_LOCKT]		= nfsd4_encode_lockt,
	[OP_LOCKU]		= nfsd4_encode_locku,
	[OP_LOOKUP]		= nfsd4_encode_noop,
	[OP_LOOKUPP]		= nfsd4_encode_noop,
	[OP_NVERIFY]		= nfsd4_encode_noop,
	[OP_OPEN]		= nfsd4_encode_open,
	[OP_OPENATTR]		= nfsd4_encode_noop,
	[OP_OPEN_CONFIRM]	= nfsd4_encode_open_confirm,
	[OP_OPEN_DOWNGRADE]	= nfsd4_encode_open_downgrade,
	[OP_PUTFH]		= nfsd4_encode_noop,
	[OP_PUTPUBFH]		= nfsd4_encode_noop,
	[OP_PUTROOTFH]		= nfsd4_encode_noop,
	[OP_READ]		= nfsd4_encode_read,
	[OP_READDIR]		= nfsd4_encode_readdir,
	[OP_READLINK]		= nfsd4_encode_readlink,
	[OP_REMOVE]		= nfsd4_encode_remove,
	[OP_RENAME]		= nfsd4_encode_rename,
	[OP_RENEW]		= nfsd4_encode_noop,
	[OP_RESTOREFH]		= nfsd4_encode_noop,
	[OP_SAVEFH]		= nfsd4_encode_noop,
	[OP_SECINFO]		= nfsd4_encode_secinfo,
	[OP_SETATTR]		= nfsd4_encode_setattr,
	[OP_SETCLIENTID]	= nfsd4_encode_setclientid,
	[OP_SETCLIENTID_CONFIRM] = nfsd4_encode_noop,
	[OP_VERIFY]		= nfsd4_encode_noop,
	[OP_WRITE]		= nfsd4_encode_write,
	[OP_RELEASE_LOCKOWNER]	= nfsd4_encode_noop,

	/* NFSv4.1 operations */
	[OP_BACKCHANNEL_CTL]	= nfsd4_encode_noop,
	[OP_BIND_CONN_TO_SESSION] = nfsd4_encode_bind_conn_to_session,
	[OP_EXCHANGE_ID]	= nfsd4_encode_exchange_id,
	[OP_CREATE_SESSION]	= nfsd4_encode_create_session,
	[OP_DESTROY_SESSION]	= nfsd4_encode_noop,
	[OP_FREE_STATEID]	= nfsd4_encode_noop,
	[OP_GET_DIR_DELEGATION]	= nfsd4_encode_get_dir_delegation,
#ifdef CONFIG_NFSD_PNFS
	[OP_GETDEVICEINFO]	= nfsd4_encode_getdeviceinfo,
	[OP_GETDEVICELIST]	= nfsd4_encode_noop,
	[OP_LAYOUTCOMMIT]	= nfsd4_encode_layoutcommit,
	[OP_LAYOUTGET]		= nfsd4_encode_layoutget,
	[OP_LAYOUTRETURN]	= nfsd4_encode_layoutreturn,
#else
	[OP_GETDEVICEINFO]	= nfsd4_encode_noop,
	[OP_GETDEVICELIST]	= nfsd4_encode_noop,
	[OP_LAYOUTCOMMIT]	= nfsd4_encode_noop,
	[OP_LAYOUTGET]		= nfsd4_encode_noop,
	[OP_LAYOUTRETURN]	= nfsd4_encode_noop,
#endif
	[OP_SECINFO_NO_NAME]	= nfsd4_encode_secinfo_no_name,
	[OP_SEQUENCE]		= nfsd4_encode_sequence,
	[OP_SET_SSV]		= nfsd4_encode_noop,
	[OP_TEST_STATEID]	= nfsd4_encode_test_stateid,
	[OP_WANT_DELEGATION]	= nfsd4_encode_noop,
	[OP_DESTROY_CLIENTID]	= nfsd4_encode_noop,
	[OP_RECLAIM_COMPLETE]	= nfsd4_encode_noop,

	/* NFSv4.2 operations */
	[OP_ALLOCATE]		= nfsd4_encode_noop,
	[OP_COPY]		= nfsd4_encode_copy,
	[OP_COPY_NOTIFY]	= nfsd4_encode_copy_notify,
	[OP_DEALLOCATE]		= nfsd4_encode_noop,
	[OP_IO_ADVISE]		= nfsd4_encode_noop,
	[OP_LAYOUTERROR]	= nfsd4_encode_noop,
	[OP_LAYOUTSTATS]	= nfsd4_encode_noop,
	[OP_OFFLOAD_CANCEL]	= nfsd4_encode_noop,
	[OP_OFFLOAD_STATUS]	= nfsd4_encode_offload_status,
	[OP_READ_PLUS]		= nfsd4_encode_read_plus,
	[OP_SEEK]		= nfsd4_encode_seek,
	[OP_WRITE_SAME]		= nfsd4_encode_noop,
	[OP_CLONE]		= nfsd4_encode_noop,

	/* RFC 8276 extended atributes operations */
	[OP_GETXATTR]		= nfsd4_encode_getxattr,
	[OP_SETXATTR]		= nfsd4_encode_setxattr,
	[OP_LISTXATTRS]		= nfsd4_encode_listxattrs,
	[OP_REMOVEXATTR]	= nfsd4_encode_removexattr,
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

static __be32 nfsd4_map_status(__be32 status, u32 minor)
{
	switch (status) {
	case nfs_ok:
		break;
	case nfserr_wrong_type:
		/* RFC 8881 - 15.1.2.9 */
		if (minor == 0)
			status = nfserr_inval;
		break;
	case nfserr_symlink_not_dir:
		status = nfserr_symlink;
		break;
	}
	return status;
}

void
nfsd4_encode_operation(struct nfsd4_compoundres *resp, struct nfsd4_op *op)
{
	struct xdr_stream *xdr = resp->xdr;
	struct nfs4_stateowner *so = resp->cstate.replay_owner;
	struct svc_rqst *rqstp = resp->rqstp;
	const struct nfsd4_operation *opdesc = op->opdesc;
	unsigned int op_status_offset;
	nfsd4_enc encoder;

	if (xdr_stream_encode_u32(xdr, op->opnum) != XDR_UNIT)
		goto release;
	op_status_offset = xdr->buf->len;
	if (!xdr_reserve_space(xdr, XDR_UNIT))
		goto release;

	if (op->opnum == OP_ILLEGAL)
		goto status;
	if (op->status && opdesc &&
			!(opdesc->op_flags & OP_NONTRIVIAL_ERROR_ENCODE))
		goto status;
	BUG_ON(op->opnum >= ARRAY_SIZE(nfsd4_enc_ops) ||
	       !nfsd4_enc_ops[op->opnum]);
	encoder = nfsd4_enc_ops[op->opnum];
	op->status = encoder(resp, op->status, &op->u);
	if (op->status)
		trace_nfsd_compound_encode_err(rqstp, op->opnum, op->status);
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
		xdr_truncate_encode(xdr, op_status_offset + XDR_UNIT);
	}
	if (so) {
		int len = xdr->buf->len - (op_status_offset + XDR_UNIT);

		so->so_replay.rp_status = op->status;
		so->so_replay.rp_buflen = len;
		read_bytes_from_xdr_buf(xdr->buf, op_status_offset + XDR_UNIT,
						so->so_replay.rp_buf, len);
	}
status:
	op->status = nfsd4_map_status(op->status,
				      resp->cstate.minorversion);
	write_bytes_to_xdr_buf(xdr->buf, op_status_offset,
			       &op->status, XDR_UNIT);
release:
	if (opdesc && opdesc->op_release)
		opdesc->op_release(&op->u);

	/*
	 * Account for pages consumed while encoding this operation.
	 * The xdr_stream primitives don't manage rq_next_page.
	 */
	rqstp->rq_next_page = xdr->page_ptr + 1;
}

/**
 * nfsd4_encode_replay - encode a result stored in the stateowner reply cache
 * @xdr: send buffer's XDR stream
 * @op: operation being replayed
 *
 * @op->replay->rp_buf contains the previously-sent already-encoded result.
 */
void nfsd4_encode_replay(struct xdr_stream *xdr, struct nfsd4_op *op)
{
	struct nfs4_replay *rp = op->replay;

	trace_nfsd_stateowner_replay(op->opnum, rp);

	if (xdr_stream_encode_u32(xdr, op->opnum) != XDR_UNIT)
		return;
	if (xdr_stream_encode_be32(xdr, rp->rp_status) != XDR_UNIT)
		return;
	xdr_stream_encode_opaque_fixed(xdr, rp->rp_buf, rp->rp_buflen);
}

void nfsd4_release_compoundargs(struct svc_rqst *rqstp)
{
	struct nfsd4_compoundargs *args = rqstp->rq_argp;

	if (args->ops != args->iops) {
		vfree(args->ops);
		args->ops = args->iops;
	}
	while (args->to_free) {
		struct svcxdr_tmpbuf *tb = args->to_free;
		args->to_free = tb->next;
		kfree(tb);
	}
}

bool
nfs4svc_decode_compoundargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd4_compoundargs *args = rqstp->rq_argp;

	/* svcxdr_tmp_alloc */
	args->to_free = NULL;

	args->xdr = xdr;
	args->ops = args->iops;
	args->rqstp = rqstp;

	return nfsd4_decode_compound(args);
}

bool
nfs4svc_encode_compoundres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	__be32 *p;

	/*
	 * Send buffer space for the following items is reserved
	 * at the top of nfsd4_proc_compound().
	 */
	p = resp->statusp;

	*p++ = resp->cstate.status;
	*p++ = htonl(resp->taglen);
	memcpy(p, resp->tag, resp->taglen);
	p += XDR_QUADLEN(resp->taglen);
	*p++ = htonl(resp->opcnt);

	nfsd4_sequence_done(resp);
	return true;
}
