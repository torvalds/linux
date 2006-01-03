/*
 *  fs/nfs/nfs4xdr.c
 *
 *  Client-side XDR for NFSv4.
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

#include <linux/param.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/kdev_t.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_idmap.h>
#include "nfs4_fs.h"

#define NFSDBG_FACILITY		NFSDBG_XDR

/* Mapping from NFS error code to "errno" error code. */
#define errno_NFSERR_IO		EIO

static int nfs_stat_to_errno(int);

/* NFSv4 COMPOUND tags are only wanted for debugging purposes */
#ifdef DEBUG
#define NFS4_MAXTAGLEN		20
#else
#define NFS4_MAXTAGLEN		0
#endif

/* lock,open owner id: 
 * we currently use size 1 (u32) out of (NFS4_OPAQUE_LIMIT  >> 2)
 */
#define owner_id_maxsz          (1 + 1)
#define compound_encode_hdr_maxsz	(3 + (NFS4_MAXTAGLEN >> 2))
#define compound_decode_hdr_maxsz	(3 + (NFS4_MAXTAGLEN >> 2))
#define op_encode_hdr_maxsz	(1)
#define op_decode_hdr_maxsz	(2)
#define encode_putfh_maxsz	(op_encode_hdr_maxsz + 1 + \
				(NFS4_FHSIZE >> 2))
#define decode_putfh_maxsz	(op_decode_hdr_maxsz)
#define encode_putrootfh_maxsz	(op_encode_hdr_maxsz)
#define decode_putrootfh_maxsz	(op_decode_hdr_maxsz)
#define encode_getfh_maxsz      (op_encode_hdr_maxsz)
#define decode_getfh_maxsz      (op_decode_hdr_maxsz + 1 + \
				((3+NFS4_FHSIZE) >> 2))
#define nfs4_fattr_bitmap_maxsz 3
#define encode_getattr_maxsz    (op_encode_hdr_maxsz + nfs4_fattr_bitmap_maxsz)
#define nfs4_name_maxsz		(1 + ((3 + NFS4_MAXNAMLEN) >> 2))
#define nfs4_path_maxsz		(1 + ((3 + NFS4_MAXPATHLEN) >> 2))
/* This is based on getfattr, which uses the most attributes: */
#define nfs4_fattr_value_maxsz	(1 + (1 + 2 + 2 + 4 + 2 + 1 + 1 + 2 + 2 + \
				3 + 3 + 3 + 2 * nfs4_name_maxsz))
#define nfs4_fattr_maxsz	(nfs4_fattr_bitmap_maxsz + \
				nfs4_fattr_value_maxsz)
#define decode_getattr_maxsz    (op_decode_hdr_maxsz + nfs4_fattr_maxsz)
#define encode_savefh_maxsz     (op_encode_hdr_maxsz)
#define decode_savefh_maxsz     (op_decode_hdr_maxsz)
#define encode_restorefh_maxsz  (op_encode_hdr_maxsz)
#define decode_restorefh_maxsz  (op_decode_hdr_maxsz)
#define encode_fsinfo_maxsz	(op_encode_hdr_maxsz + 2)
#define decode_fsinfo_maxsz	(op_decode_hdr_maxsz + 11)
#define encode_renew_maxsz	(op_encode_hdr_maxsz + 3)
#define decode_renew_maxsz	(op_decode_hdr_maxsz)
#define encode_setclientid_maxsz \
				(op_encode_hdr_maxsz + \
				4 /*server->ip_addr*/ + \
				1 /*Netid*/ + \
				6 /*uaddr*/ + \
				6 + (NFS4_VERIFIER_SIZE >> 2))
#define decode_setclientid_maxsz \
				(op_decode_hdr_maxsz + \
				2 + \
				1024) /* large value for CLID_INUSE */
#define encode_setclientid_confirm_maxsz \
				(op_encode_hdr_maxsz + \
				3 + (NFS4_VERIFIER_SIZE >> 2))
#define decode_setclientid_confirm_maxsz \
				(op_decode_hdr_maxsz)
#define encode_lookup_maxsz	(op_encode_hdr_maxsz + \
				1 + ((3 + NFS4_FHSIZE) >> 2))
#define encode_remove_maxsz	(op_encode_hdr_maxsz + \
				nfs4_name_maxsz)
#define encode_rename_maxsz	(op_encode_hdr_maxsz + \
				2 * nfs4_name_maxsz)
#define decode_rename_maxsz	(op_decode_hdr_maxsz + 5 + 5)
#define encode_link_maxsz	(op_encode_hdr_maxsz + \
				nfs4_name_maxsz)
#define decode_link_maxsz	(op_decode_hdr_maxsz + 5)
#define encode_symlink_maxsz	(op_encode_hdr_maxsz + \
				1 + nfs4_name_maxsz + \
				nfs4_path_maxsz + \
				nfs4_fattr_maxsz)
#define decode_symlink_maxsz	(op_decode_hdr_maxsz + 8)
#define encode_create_maxsz	(op_encode_hdr_maxsz + \
				2 + nfs4_name_maxsz + \
				nfs4_fattr_maxsz)
#define decode_create_maxsz	(op_decode_hdr_maxsz + 8)
#define encode_delegreturn_maxsz (op_encode_hdr_maxsz + 4)
#define decode_delegreturn_maxsz (op_decode_hdr_maxsz)
#define NFS4_enc_compound_sz	(1024)  /* XXX: large enough? */
#define NFS4_dec_compound_sz	(1024)  /* XXX: large enough? */
#define NFS4_enc_read_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				op_encode_hdr_maxsz + 7)
#define NFS4_dec_read_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz + 2)
#define NFS4_enc_readlink_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				op_encode_hdr_maxsz)
#define NFS4_dec_readlink_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz)
#define NFS4_enc_readdir_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				op_encode_hdr_maxsz + 9)
#define NFS4_dec_readdir_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz + 2)
#define NFS4_enc_write_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				op_encode_hdr_maxsz + 8 + \
				encode_getattr_maxsz)
#define NFS4_dec_write_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz + 4 + \
				decode_getattr_maxsz)
#define NFS4_enc_commit_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				op_encode_hdr_maxsz + 3 + \
				encode_getattr_maxsz)
#define NFS4_dec_commit_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz + 2 + \
				decode_getattr_maxsz)
#define NFS4_enc_open_sz        (compound_encode_hdr_maxsz + \
                                encode_putfh_maxsz + \
                                op_encode_hdr_maxsz + \
                                13 + 3 + 2 + 64 + \
                                encode_getattr_maxsz + \
                                encode_getfh_maxsz)
#define NFS4_dec_open_sz        (compound_decode_hdr_maxsz + \
                                decode_putfh_maxsz + \
                                op_decode_hdr_maxsz + 4 + 5 + 2 + 3 + \
                                decode_getattr_maxsz + \
                                decode_getfh_maxsz)
#define NFS4_enc_open_confirm_sz      \
                                (compound_encode_hdr_maxsz + \
                                encode_putfh_maxsz + \
                                op_encode_hdr_maxsz + 5)
#define NFS4_dec_open_confirm_sz        (compound_decode_hdr_maxsz + \
                                        decode_putfh_maxsz + \
                                        op_decode_hdr_maxsz + 4)
#define NFS4_enc_open_noattr_sz	(compound_encode_hdr_maxsz + \
					encode_putfh_maxsz + \
					op_encode_hdr_maxsz + \
					11)
#define NFS4_dec_open_noattr_sz	(compound_decode_hdr_maxsz + \
					decode_putfh_maxsz + \
					op_decode_hdr_maxsz + \
					4 + 5 + 2 + 3)
#define NFS4_enc_open_downgrade_sz \
				(compound_encode_hdr_maxsz + \
                                encode_putfh_maxsz + \
                                op_encode_hdr_maxsz + 7 + \
				encode_getattr_maxsz)
#define NFS4_dec_open_downgrade_sz \
				(compound_decode_hdr_maxsz + \
                                decode_putfh_maxsz + \
                                op_decode_hdr_maxsz + 4 + \
				decode_getattr_maxsz)
#define NFS4_enc_close_sz       (compound_encode_hdr_maxsz + \
                                encode_putfh_maxsz + \
                                op_encode_hdr_maxsz + 5 + \
				encode_getattr_maxsz)
#define NFS4_dec_close_sz       (compound_decode_hdr_maxsz + \
                                decode_putfh_maxsz + \
                                op_decode_hdr_maxsz + 4 + \
				decode_getattr_maxsz)
#define NFS4_enc_setattr_sz     (compound_encode_hdr_maxsz + \
                                encode_putfh_maxsz + \
                                op_encode_hdr_maxsz + 4 + \
                                nfs4_fattr_maxsz + \
                                encode_getattr_maxsz)
#define NFS4_dec_setattr_sz     (compound_decode_hdr_maxsz + \
                                decode_putfh_maxsz + \
                                op_decode_hdr_maxsz + 3)
#define NFS4_enc_fsinfo_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_fsinfo_maxsz)
#define NFS4_dec_fsinfo_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_fsinfo_maxsz)
#define NFS4_enc_renew_sz	(compound_encode_hdr_maxsz + \
				encode_renew_maxsz)
#define NFS4_dec_renew_sz	(compound_decode_hdr_maxsz + \
				decode_renew_maxsz)
#define NFS4_enc_setclientid_sz	(compound_encode_hdr_maxsz + \
				encode_setclientid_maxsz)
#define NFS4_dec_setclientid_sz	(compound_decode_hdr_maxsz + \
				decode_setclientid_maxsz)
#define NFS4_enc_setclientid_confirm_sz \
				(compound_encode_hdr_maxsz + \
				encode_setclientid_confirm_maxsz + \
				encode_putrootfh_maxsz + \
				encode_fsinfo_maxsz)
#define NFS4_dec_setclientid_confirm_sz \
				(compound_decode_hdr_maxsz + \
				decode_setclientid_confirm_maxsz + \
				decode_putrootfh_maxsz + \
				decode_fsinfo_maxsz)
#define NFS4_enc_lock_sz        (compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz + \
				op_encode_hdr_maxsz + \
				1 + 1 + 2 + 2 + \
				1 + 4 + 1 + 2 + \
				owner_id_maxsz)
#define NFS4_dec_lock_sz        (compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz + \
				op_decode_hdr_maxsz + \
				2 + 2 + 1 + 2 + \
				owner_id_maxsz)
#define NFS4_enc_lockt_sz       (compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz + \
				op_encode_hdr_maxsz + \
				1 + 2 + 2 + 2 + \
				owner_id_maxsz)
#define NFS4_dec_lockt_sz       (NFS4_dec_lock_sz)
#define NFS4_enc_locku_sz       (compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz + \
				op_encode_hdr_maxsz + \
				1 + 1 + 4 + 2 + 2)
#define NFS4_dec_locku_sz       (compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz + \
				op_decode_hdr_maxsz + 4)
#define NFS4_enc_access_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				op_encode_hdr_maxsz + 1)
#define NFS4_dec_access_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz + 2)
#define NFS4_enc_getattr_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_getattr_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_lookup_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_lookup_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS4_dec_lookup_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS4_enc_lookup_root_sz (compound_encode_hdr_maxsz + \
				encode_putrootfh_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS4_dec_lookup_root_sz (compound_decode_hdr_maxsz + \
				decode_putrootfh_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS4_enc_remove_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_remove_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_remove_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz + 5 + \
				decode_getattr_maxsz)
#define NFS4_enc_rename_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_putfh_maxsz + \
				encode_rename_maxsz + \
				encode_getattr_maxsz + \
				encode_restorefh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_rename_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_putfh_maxsz + \
				decode_rename_maxsz + \
				decode_getattr_maxsz + \
				decode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_link_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_putfh_maxsz + \
				encode_link_maxsz + \
				decode_getattr_maxsz + \
				encode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_dec_link_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_putfh_maxsz + \
				decode_link_maxsz + \
				decode_getattr_maxsz + \
				decode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_symlink_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_symlink_maxsz + \
				encode_getattr_maxsz + \
				encode_getfh_maxsz)
#define NFS4_dec_symlink_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_symlink_maxsz + \
				decode_getattr_maxsz + \
				decode_getfh_maxsz)
#define NFS4_enc_create_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_savefh_maxsz + \
				encode_create_maxsz + \
				encode_getfh_maxsz + \
				encode_getattr_maxsz + \
				encode_restorefh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_create_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_savefh_maxsz + \
				decode_create_maxsz + \
				decode_getfh_maxsz + \
				decode_getattr_maxsz + \
				decode_restorefh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_pathconf_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_pathconf_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_statfs_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_statfs_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz + 12)
#define NFS4_enc_server_caps_sz (compound_encode_hdr_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_server_caps_sz (compound_decode_hdr_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_delegreturn_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_delegreturn_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_delegreturn_sz (compound_decode_hdr_maxsz + \
				decode_delegreturn_maxsz + \
				decode_getattr_maxsz)
#define NFS4_enc_getacl_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				encode_getattr_maxsz)
#define NFS4_dec_getacl_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz + \
				nfs4_fattr_bitmap_maxsz + 1)
#define NFS4_enc_setacl_sz	(compound_encode_hdr_maxsz + \
				encode_putfh_maxsz + \
				op_encode_hdr_maxsz + 4 + \
				nfs4_fattr_bitmap_maxsz + 1)
#define NFS4_dec_setacl_sz	(compound_decode_hdr_maxsz + \
				decode_putfh_maxsz + \
				op_decode_hdr_maxsz + nfs4_fattr_bitmap_maxsz)

static struct {
	unsigned int	mode;
	unsigned int	nfs2type;
} nfs_type2fmt[] = {
	{ 0,		NFNON	     },
	{ S_IFREG,	NFREG	     },
	{ S_IFDIR,	NFDIR	     },
	{ S_IFBLK,	NFBLK	     },
	{ S_IFCHR,	NFCHR	     },
	{ S_IFLNK,	NFLNK	     },
	{ S_IFSOCK,	NFSOCK	     },
	{ S_IFIFO,	NFFIFO	     },
	{ 0,		NFNON	     },
	{ 0,		NFNON	     },
};

struct compound_hdr {
	int32_t		status;
	uint32_t	nops;
	uint32_t	taglen;
	char *		tag;
};

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
#define WRITE32(n)               *p++ = htonl(n)
#define WRITE64(n)               do {				\
	*p++ = htonl((uint32_t)((n) >> 32));				\
	*p++ = htonl((uint32_t)(n));					\
} while (0)
#define WRITEMEM(ptr,nbytes)     do {				\
	p = xdr_encode_opaque_fixed(p, ptr, nbytes);		\
} while (0)

#define RESERVE_SPACE(nbytes)	do {				\
	p = xdr_reserve_space(xdr, nbytes);			\
	if (!p) printk("RESERVE_SPACE(%d) failed in function %s\n", (int) (nbytes), __FUNCTION__); \
	BUG_ON(!p);						\
} while (0)

static void encode_string(struct xdr_stream *xdr, unsigned int len, const char *str)
{
	uint32_t *p;

	p = xdr_reserve_space(xdr, 4 + len);
	BUG_ON(p == NULL);
	xdr_encode_opaque(p, str, len);
}

static int encode_compound_hdr(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	uint32_t *p;

	dprintk("encode_compound: tag=%.*s\n", (int)hdr->taglen, hdr->tag);
	BUG_ON(hdr->taglen > NFS4_MAXTAGLEN);
	RESERVE_SPACE(12+(XDR_QUADLEN(hdr->taglen)<<2));
	WRITE32(hdr->taglen);
	WRITEMEM(hdr->tag, hdr->taglen);
	WRITE32(NFS4_MINOR_VERSION);
	WRITE32(hdr->nops);
	return 0;
}

static void encode_nfs4_verifier(struct xdr_stream *xdr, const nfs4_verifier *verf)
{
	uint32_t *p;

	p = xdr_reserve_space(xdr, NFS4_VERIFIER_SIZE);
	BUG_ON(p == NULL);
	xdr_encode_opaque_fixed(p, verf->data, NFS4_VERIFIER_SIZE);
}

static int encode_attrs(struct xdr_stream *xdr, const struct iattr *iap, const struct nfs_server *server)
{
	char owner_name[IDMAP_NAMESZ];
	char owner_group[IDMAP_NAMESZ];
	int owner_namelen = 0;
	int owner_grouplen = 0;
	uint32_t *p;
	uint32_t *q;
	int len;
	uint32_t bmval0 = 0;
	uint32_t bmval1 = 0;
	int status;

	/*
	 * We reserve enough space to write the entire attribute buffer at once.
	 * In the worst-case, this would be
	 *   12(bitmap) + 4(attrlen) + 8(size) + 4(mode) + 4(atime) + 4(mtime)
	 *          = 36 bytes, plus any contribution from variable-length fields
	 *            such as owner/group.
	 */
	len = 16;

	/* Sigh */
	if (iap->ia_valid & ATTR_SIZE)
		len += 8;
	if (iap->ia_valid & ATTR_MODE)
		len += 4;
	if (iap->ia_valid & ATTR_UID) {
		owner_namelen = nfs_map_uid_to_name(server->nfs4_state, iap->ia_uid, owner_name);
		if (owner_namelen < 0) {
			printk(KERN_WARNING "nfs: couldn't resolve uid %d to string\n",
			       iap->ia_uid);
			/* XXX */
			strcpy(owner_name, "nobody");
			owner_namelen = sizeof("nobody") - 1;
			/* goto out; */
		}
		len += 4 + (XDR_QUADLEN(owner_namelen) << 2);
	}
	if (iap->ia_valid & ATTR_GID) {
		owner_grouplen = nfs_map_gid_to_group(server->nfs4_state, iap->ia_gid, owner_group);
		if (owner_grouplen < 0) {
			printk(KERN_WARNING "nfs4: couldn't resolve gid %d to string\n",
			       iap->ia_gid);
			strcpy(owner_group, "nobody");
			owner_grouplen = sizeof("nobody") - 1;
			/* goto out; */
		}
		len += 4 + (XDR_QUADLEN(owner_grouplen) << 2);
	}
	if (iap->ia_valid & ATTR_ATIME_SET)
		len += 16;
	else if (iap->ia_valid & ATTR_ATIME)
		len += 4;
	if (iap->ia_valid & ATTR_MTIME_SET)
		len += 16;
	else if (iap->ia_valid & ATTR_MTIME)
		len += 4;
	RESERVE_SPACE(len);

	/*
	 * We write the bitmap length now, but leave the bitmap and the attribute
	 * buffer length to be backfilled at the end of this routine.
	 */
	WRITE32(2);
	q = p;
	p += 3;

	if (iap->ia_valid & ATTR_SIZE) {
		bmval0 |= FATTR4_WORD0_SIZE;
		WRITE64(iap->ia_size);
	}
	if (iap->ia_valid & ATTR_MODE) {
		bmval1 |= FATTR4_WORD1_MODE;
		WRITE32(iap->ia_mode & S_IALLUGO);
	}
	if (iap->ia_valid & ATTR_UID) {
		bmval1 |= FATTR4_WORD1_OWNER;
		WRITE32(owner_namelen);
		WRITEMEM(owner_name, owner_namelen);
	}
	if (iap->ia_valid & ATTR_GID) {
		bmval1 |= FATTR4_WORD1_OWNER_GROUP;
		WRITE32(owner_grouplen);
		WRITEMEM(owner_group, owner_grouplen);
	}
	if (iap->ia_valid & ATTR_ATIME_SET) {
		bmval1 |= FATTR4_WORD1_TIME_ACCESS_SET;
		WRITE32(NFS4_SET_TO_CLIENT_TIME);
		WRITE32(0);
		WRITE32(iap->ia_mtime.tv_sec);
		WRITE32(iap->ia_mtime.tv_nsec);
	}
	else if (iap->ia_valid & ATTR_ATIME) {
		bmval1 |= FATTR4_WORD1_TIME_ACCESS_SET;
		WRITE32(NFS4_SET_TO_SERVER_TIME);
	}
	if (iap->ia_valid & ATTR_MTIME_SET) {
		bmval1 |= FATTR4_WORD1_TIME_MODIFY_SET;
		WRITE32(NFS4_SET_TO_CLIENT_TIME);
		WRITE32(0);
		WRITE32(iap->ia_mtime.tv_sec);
		WRITE32(iap->ia_mtime.tv_nsec);
	}
	else if (iap->ia_valid & ATTR_MTIME) {
		bmval1 |= FATTR4_WORD1_TIME_MODIFY_SET;
		WRITE32(NFS4_SET_TO_SERVER_TIME);
	}
	
	/*
	 * Now we backfill the bitmap and the attribute buffer length.
	 */
	if (len != ((char *)p - (char *)q) + 4) {
		printk ("encode_attr: Attr length calculation error! %u != %Zu\n",
				len, ((char *)p - (char *)q) + 4);
		BUG();
	}
	len = (char *)p - (char *)q - 12;
	*q++ = htonl(bmval0);
	*q++ = htonl(bmval1);
	*q++ = htonl(len);

	status = 0;
/* out: */
	return status;
}

static int encode_access(struct xdr_stream *xdr, u32 access)
{
	uint32_t *p;

	RESERVE_SPACE(8);
	WRITE32(OP_ACCESS);
	WRITE32(access);
	
	return 0;
}

static int encode_close(struct xdr_stream *xdr, const struct nfs_closeargs *arg)
{
	uint32_t *p;

	RESERVE_SPACE(8+sizeof(arg->stateid->data));
	WRITE32(OP_CLOSE);
	WRITE32(arg->seqid->sequence->counter);
	WRITEMEM(arg->stateid->data, sizeof(arg->stateid->data));
	
	return 0;
}

static int encode_commit(struct xdr_stream *xdr, const struct nfs_writeargs *args)
{
	uint32_t *p;
        
        RESERVE_SPACE(16);
        WRITE32(OP_COMMIT);
        WRITE64(args->offset);
        WRITE32(args->count);

        return 0;
}

static int encode_create(struct xdr_stream *xdr, const struct nfs4_create_arg *create)
{
	uint32_t *p;
	
	RESERVE_SPACE(8);
	WRITE32(OP_CREATE);
	WRITE32(create->ftype);

	switch (create->ftype) {
	case NF4LNK:
		RESERVE_SPACE(4 + create->u.symlink->len);
		WRITE32(create->u.symlink->len);
		WRITEMEM(create->u.symlink->name, create->u.symlink->len);
		break;

	case NF4BLK: case NF4CHR:
		RESERVE_SPACE(8);
		WRITE32(create->u.device.specdata1);
		WRITE32(create->u.device.specdata2);
		break;

	default:
		break;
	}

	RESERVE_SPACE(4 + create->name->len);
	WRITE32(create->name->len);
	WRITEMEM(create->name->name, create->name->len);

	return encode_attrs(xdr, create->attrs, create->server);
}

static int encode_getattr_one(struct xdr_stream *xdr, uint32_t bitmap)
{
        uint32_t *p;

        RESERVE_SPACE(12);
        WRITE32(OP_GETATTR);
        WRITE32(1);
        WRITE32(bitmap);
        return 0;
}

static int encode_getattr_two(struct xdr_stream *xdr, uint32_t bm0, uint32_t bm1)
{
        uint32_t *p;

        RESERVE_SPACE(16);
        WRITE32(OP_GETATTR);
        WRITE32(2);
        WRITE32(bm0);
        WRITE32(bm1);
        return 0;
}

static int encode_getfattr(struct xdr_stream *xdr, const u32* bitmask)
{
	return encode_getattr_two(xdr,
			bitmask[0] & nfs4_fattr_bitmap[0],
			bitmask[1] & nfs4_fattr_bitmap[1]);
}

static int encode_fsinfo(struct xdr_stream *xdr, const u32* bitmask)
{
	return encode_getattr_two(xdr, bitmask[0] & nfs4_fsinfo_bitmap[0],
			bitmask[1] & nfs4_fsinfo_bitmap[1]);
}

static int encode_getfh(struct xdr_stream *xdr)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_GETFH);

	return 0;
}

static int encode_link(struct xdr_stream *xdr, const struct qstr *name)
{
	uint32_t *p;

	RESERVE_SPACE(8 + name->len);
	WRITE32(OP_LINK);
	WRITE32(name->len);
	WRITEMEM(name->name, name->len);
	
	return 0;
}

static inline int nfs4_lock_type(struct file_lock *fl, int block)
{
	if ((fl->fl_type & (F_RDLCK|F_WRLCK|F_UNLCK)) == F_RDLCK)
		return block ? NFS4_READW_LT : NFS4_READ_LT;
	return block ? NFS4_WRITEW_LT : NFS4_WRITE_LT;
}

static inline uint64_t nfs4_lock_length(struct file_lock *fl)
{
	if (fl->fl_end == OFFSET_MAX)
		return ~(uint64_t)0;
	return fl->fl_end - fl->fl_start + 1;
}

/*
 * opcode,type,reclaim,offset,length,new_lock_owner = 32
 * open_seqid,open_stateid,lock_seqid,lock_owner.clientid, lock_owner.id = 40
 */
static int encode_lock(struct xdr_stream *xdr, const struct nfs_lock_args *args)
{
	uint32_t *p;

	RESERVE_SPACE(32);
	WRITE32(OP_LOCK);
	WRITE32(nfs4_lock_type(args->fl, args->block));
	WRITE32(args->reclaim);
	WRITE64(args->fl->fl_start);
	WRITE64(nfs4_lock_length(args->fl));
	WRITE32(args->new_lock_owner);
	if (args->new_lock_owner){
		RESERVE_SPACE(40);
		WRITE32(args->open_seqid->sequence->counter);
		WRITEMEM(args->open_stateid->data, sizeof(args->open_stateid->data));
		WRITE32(args->lock_seqid->sequence->counter);
		WRITE64(args->lock_owner.clientid);
		WRITE32(4);
		WRITE32(args->lock_owner.id);
	}
	else {
		RESERVE_SPACE(20);
		WRITEMEM(args->lock_stateid->data, sizeof(args->lock_stateid->data));
		WRITE32(args->lock_seqid->sequence->counter);
	}

	return 0;
}

static int encode_lockt(struct xdr_stream *xdr, const struct nfs_lockt_args *args)
{
	uint32_t *p;

	RESERVE_SPACE(40);
	WRITE32(OP_LOCKT);
	WRITE32(nfs4_lock_type(args->fl, 0));
	WRITE64(args->fl->fl_start);
	WRITE64(nfs4_lock_length(args->fl));
	WRITE64(args->lock_owner.clientid);
	WRITE32(4);
	WRITE32(args->lock_owner.id);

	return 0;
}

static int encode_locku(struct xdr_stream *xdr, const struct nfs_locku_args *args)
{
	uint32_t *p;

	RESERVE_SPACE(44);
	WRITE32(OP_LOCKU);
	WRITE32(nfs4_lock_type(args->fl, 0));
	WRITE32(args->seqid->sequence->counter);
	WRITEMEM(args->stateid->data, sizeof(args->stateid->data));
	WRITE64(args->fl->fl_start);
	WRITE64(nfs4_lock_length(args->fl));

	return 0;
}

static int encode_lookup(struct xdr_stream *xdr, const struct qstr *name)
{
	int len = name->len;
	uint32_t *p;

	RESERVE_SPACE(8 + len);
	WRITE32(OP_LOOKUP);
	WRITE32(len);
	WRITEMEM(name->name, len);

	return 0;
}

static void encode_share_access(struct xdr_stream *xdr, int open_flags)
{
	uint32_t *p;

	RESERVE_SPACE(8);
	switch (open_flags & (FMODE_READ|FMODE_WRITE)) {
		case FMODE_READ:
			WRITE32(NFS4_SHARE_ACCESS_READ);
			break;
		case FMODE_WRITE:
			WRITE32(NFS4_SHARE_ACCESS_WRITE);
			break;
		case FMODE_READ|FMODE_WRITE:
			WRITE32(NFS4_SHARE_ACCESS_BOTH);
			break;
		default:
			BUG();
	}
	WRITE32(0);		/* for linux, share_deny = 0 always */
}

static inline void encode_openhdr(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	uint32_t *p;
 /*
 * opcode 4, seqid 4, share_access 4, share_deny 4, clientid 8, ownerlen 4,
 * owner 4 = 32
 */
	RESERVE_SPACE(8);
	WRITE32(OP_OPEN);
	WRITE32(arg->seqid->sequence->counter);
	encode_share_access(xdr, arg->open_flags);
	RESERVE_SPACE(16);
	WRITE64(arg->clientid);
	WRITE32(4);
	WRITE32(arg->id);
}

static inline void encode_createmode(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	switch(arg->open_flags & O_EXCL) {
		case 0:
			WRITE32(NFS4_CREATE_UNCHECKED);
			encode_attrs(xdr, arg->u.attrs, arg->server);
			break;
		default:
			WRITE32(NFS4_CREATE_EXCLUSIVE);
			encode_nfs4_verifier(xdr, &arg->u.verifier);
	}
}

static void encode_opentype(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	switch (arg->open_flags & O_CREAT) {
		case 0:
			WRITE32(NFS4_OPEN_NOCREATE);
			break;
		default:
			BUG_ON(arg->claim != NFS4_OPEN_CLAIM_NULL);
			WRITE32(NFS4_OPEN_CREATE);
			encode_createmode(xdr, arg);
	}
}

static inline void encode_delegation_type(struct xdr_stream *xdr, int delegation_type)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	switch (delegation_type) {
		case 0:
			WRITE32(NFS4_OPEN_DELEGATE_NONE);
			break;
		case FMODE_READ:
			WRITE32(NFS4_OPEN_DELEGATE_READ);
			break;
		case FMODE_WRITE|FMODE_READ:
			WRITE32(NFS4_OPEN_DELEGATE_WRITE);
			break;
		default:
			BUG();
	}
}

static inline void encode_claim_null(struct xdr_stream *xdr, const struct qstr *name)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(NFS4_OPEN_CLAIM_NULL);
	encode_string(xdr, name->len, name->name);
}

static inline void encode_claim_previous(struct xdr_stream *xdr, int type)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(NFS4_OPEN_CLAIM_PREVIOUS);
	encode_delegation_type(xdr, type);
}

static inline void encode_claim_delegate_cur(struct xdr_stream *xdr, const struct qstr *name, const nfs4_stateid *stateid)
{
	uint32_t *p;

	RESERVE_SPACE(4+sizeof(stateid->data));
	WRITE32(NFS4_OPEN_CLAIM_DELEGATE_CUR);
	WRITEMEM(stateid->data, sizeof(stateid->data));
	encode_string(xdr, name->len, name->name);
}

static int encode_open(struct xdr_stream *xdr, const struct nfs_openargs *arg)
{
	encode_openhdr(xdr, arg);
	encode_opentype(xdr, arg);
	switch (arg->claim) {
		case NFS4_OPEN_CLAIM_NULL:
			encode_claim_null(xdr, arg->name);
			break;
		case NFS4_OPEN_CLAIM_PREVIOUS:
			encode_claim_previous(xdr, arg->u.delegation_type);
			break;
		case NFS4_OPEN_CLAIM_DELEGATE_CUR:
			encode_claim_delegate_cur(xdr, arg->name, &arg->u.delegation);
			break;
		default:
			BUG();
	}
	return 0;
}

static int encode_open_confirm(struct xdr_stream *xdr, const struct nfs_open_confirmargs *arg)
{
	uint32_t *p;

	RESERVE_SPACE(8+sizeof(arg->stateid->data));
	WRITE32(OP_OPEN_CONFIRM);
	WRITEMEM(arg->stateid->data, sizeof(arg->stateid->data));
	WRITE32(arg->seqid->sequence->counter);

	return 0;
}

static int encode_open_downgrade(struct xdr_stream *xdr, const struct nfs_closeargs *arg)
{
	uint32_t *p;

	RESERVE_SPACE(8+sizeof(arg->stateid->data));
	WRITE32(OP_OPEN_DOWNGRADE);
	WRITEMEM(arg->stateid->data, sizeof(arg->stateid->data));
	WRITE32(arg->seqid->sequence->counter);
	encode_share_access(xdr, arg->open_flags);
	return 0;
}

static int
encode_putfh(struct xdr_stream *xdr, const struct nfs_fh *fh)
{
	int len = fh->size;
	uint32_t *p;

	RESERVE_SPACE(8 + len);
	WRITE32(OP_PUTFH);
	WRITE32(len);
	WRITEMEM(fh->data, len);

	return 0;
}

static int encode_putrootfh(struct xdr_stream *xdr)
{
        uint32_t *p;
        
        RESERVE_SPACE(4);
        WRITE32(OP_PUTROOTFH);

        return 0;
}

static void encode_stateid(struct xdr_stream *xdr, const struct nfs_open_context *ctx)
{
	nfs4_stateid stateid;
	uint32_t *p;

	RESERVE_SPACE(16);
	if (ctx->state != NULL) {
		nfs4_copy_stateid(&stateid, ctx->state, ctx->lockowner);
		WRITEMEM(stateid.data, sizeof(stateid.data));
	} else
		WRITEMEM(zero_stateid.data, sizeof(zero_stateid.data));
}

static int encode_read(struct xdr_stream *xdr, const struct nfs_readargs *args)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_READ);

	encode_stateid(xdr, args->context);

	RESERVE_SPACE(12);
	WRITE64(args->offset);
	WRITE32(args->count);

	return 0;
}

static int encode_readdir(struct xdr_stream *xdr, const struct nfs4_readdir_arg *readdir, struct rpc_rqst *req)
{
	struct rpc_auth *auth = req->rq_task->tk_auth;
	uint32_t attrs[2] = {
		FATTR4_WORD0_RDATTR_ERROR|FATTR4_WORD0_FILEID,
		FATTR4_WORD1_MOUNTED_ON_FILEID,
	};
	int replen;
	uint32_t *p;

	RESERVE_SPACE(32+sizeof(nfs4_verifier));
	WRITE32(OP_READDIR);
	WRITE64(readdir->cookie);
	WRITEMEM(readdir->verifier.data, sizeof(readdir->verifier.data));
	WRITE32(readdir->count >> 1);  /* We're not doing readdirplus */
	WRITE32(readdir->count);
	WRITE32(2);
	/* Switch to mounted_on_fileid if the server supports it */
	if (readdir->bitmask[1] & FATTR4_WORD1_MOUNTED_ON_FILEID)
		attrs[0] &= ~FATTR4_WORD0_FILEID;
	else
		attrs[1] &= ~FATTR4_WORD1_MOUNTED_ON_FILEID;
	WRITE32(attrs[0] & readdir->bitmask[0]);
	WRITE32(attrs[1] & readdir->bitmask[1]);
	dprintk("%s: cookie = %Lu, verifier = 0x%x%x, bitmap = 0x%x%x\n",
			__FUNCTION__,
			(unsigned long long)readdir->cookie,
			((u32 *)readdir->verifier.data)[0],
			((u32 *)readdir->verifier.data)[1],
			attrs[0] & readdir->bitmask[0],
			attrs[1] & readdir->bitmask[1]);

	/* set up reply kvec
	 *    toplevel_status + taglen + rescount + OP_PUTFH + status
	 *      + OP_READDIR + status + verifer(2)  = 9
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + 9) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen, readdir->pages,
			 readdir->pgbase, readdir->count);
	dprintk("%s: inlined page args = (%u, %p, %u, %u)\n",
			__FUNCTION__, replen, readdir->pages,
			readdir->pgbase, readdir->count);

	return 0;
}

static int encode_readlink(struct xdr_stream *xdr, const struct nfs4_readlink *readlink, struct rpc_rqst *req)
{
	struct rpc_auth *auth = req->rq_task->tk_auth;
	unsigned int replen;
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_READLINK);

	/* set up reply kvec
	 *    toplevel_status + taglen + rescount + OP_PUTFH + status
	 *      + OP_READLINK + status + string length = 8
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + 8) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen, readlink->pages,
			readlink->pgbase, readlink->pglen);
	
	return 0;
}

static int encode_remove(struct xdr_stream *xdr, const struct qstr *name)
{
	uint32_t *p;

	RESERVE_SPACE(8 + name->len);
	WRITE32(OP_REMOVE);
	WRITE32(name->len);
	WRITEMEM(name->name, name->len);

	return 0;
}

static int encode_rename(struct xdr_stream *xdr, const struct qstr *oldname, const struct qstr *newname)
{
	uint32_t *p;

	RESERVE_SPACE(8 + oldname->len);
	WRITE32(OP_RENAME);
	WRITE32(oldname->len);
	WRITEMEM(oldname->name, oldname->len);
	
	RESERVE_SPACE(4 + newname->len);
	WRITE32(newname->len);
	WRITEMEM(newname->name, newname->len);

	return 0;
}

static int encode_renew(struct xdr_stream *xdr, const struct nfs4_client *client_stateid)
{
	uint32_t *p;

	RESERVE_SPACE(12);
	WRITE32(OP_RENEW);
	WRITE64(client_stateid->cl_clientid);

	return 0;
}

static int
encode_restorefh(struct xdr_stream *xdr)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_RESTOREFH);

	return 0;
}

static int
encode_setacl(struct xdr_stream *xdr, struct nfs_setaclargs *arg)
{
	uint32_t *p;

	RESERVE_SPACE(4+sizeof(zero_stateid.data));
	WRITE32(OP_SETATTR);
	WRITEMEM(zero_stateid.data, sizeof(zero_stateid.data));
	RESERVE_SPACE(2*4);
	WRITE32(1);
	WRITE32(FATTR4_WORD0_ACL);
	if (arg->acl_len % 4)
		return -EINVAL;
	RESERVE_SPACE(4);
	WRITE32(arg->acl_len);
	xdr_write_pages(xdr, arg->acl_pages, arg->acl_pgbase, arg->acl_len);
	return 0;
}

static int
encode_savefh(struct xdr_stream *xdr)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_SAVEFH);

	return 0;
}

static int encode_setattr(struct xdr_stream *xdr, const struct nfs_setattrargs *arg, const struct nfs_server *server)
{
	int status;
	uint32_t *p;
	
        RESERVE_SPACE(4+sizeof(arg->stateid.data));
        WRITE32(OP_SETATTR);
	WRITEMEM(arg->stateid.data, sizeof(arg->stateid.data));

        if ((status = encode_attrs(xdr, arg->iap, server)))
		return status;

        return 0;
}

static int encode_setclientid(struct xdr_stream *xdr, const struct nfs4_setclientid *setclientid)
{
	uint32_t *p;

	RESERVE_SPACE(4 + sizeof(setclientid->sc_verifier->data));
	WRITE32(OP_SETCLIENTID);
	WRITEMEM(setclientid->sc_verifier->data, sizeof(setclientid->sc_verifier->data));

	encode_string(xdr, setclientid->sc_name_len, setclientid->sc_name);
	RESERVE_SPACE(4);
	WRITE32(setclientid->sc_prog);
	encode_string(xdr, setclientid->sc_netid_len, setclientid->sc_netid);
	encode_string(xdr, setclientid->sc_uaddr_len, setclientid->sc_uaddr);
	RESERVE_SPACE(4);
	WRITE32(setclientid->sc_cb_ident);

	return 0;
}

static int encode_setclientid_confirm(struct xdr_stream *xdr, const struct nfs4_client *client_state)
{
        uint32_t *p;

        RESERVE_SPACE(12 + sizeof(client_state->cl_confirm.data));
        WRITE32(OP_SETCLIENTID_CONFIRM);
        WRITE64(client_state->cl_clientid);
        WRITEMEM(client_state->cl_confirm.data, sizeof(client_state->cl_confirm.data));

        return 0;
}

static int encode_write(struct xdr_stream *xdr, const struct nfs_writeargs *args)
{
	uint32_t *p;

	RESERVE_SPACE(4);
	WRITE32(OP_WRITE);

	encode_stateid(xdr, args->context);

	RESERVE_SPACE(16);
	WRITE64(args->offset);
	WRITE32(args->stable);
	WRITE32(args->count);

	xdr_write_pages(xdr, args->pages, args->pgbase, args->count);

	return 0;
}

static int encode_delegreturn(struct xdr_stream *xdr, const nfs4_stateid *stateid)
{
	uint32_t *p;

	RESERVE_SPACE(20);

	WRITE32(OP_DELEGRETURN);
	WRITEMEM(stateid->data, sizeof(stateid->data));
	return 0;

}
/*
 * END OF "GENERIC" ENCODE ROUTINES.
 */

/*
 * Encode an ACCESS request
 */
static int nfs4_xdr_enc_access(struct rpc_rqst *req, uint32_t *p, const struct nfs4_accessargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	if ((status = encode_putfh(&xdr, args->fh)) == 0)
		status = encode_access(&xdr, args->access);
	return status;
}

/*
 * Encode LOOKUP request
 */
static int nfs4_xdr_enc_lookup(struct rpc_rqst *req, uint32_t *p, const struct nfs4_lookup_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 4,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	if ((status = encode_putfh(&xdr, args->dir_fh)) != 0)
		goto out;
	if ((status = encode_lookup(&xdr, args->name)) != 0)
		goto out;
	if ((status = encode_getfh(&xdr)) != 0)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

/*
 * Encode LOOKUP_ROOT request
 */
static int nfs4_xdr_enc_lookup_root(struct rpc_rqst *req, uint32_t *p, const struct nfs4_lookup_root_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	if ((status = encode_putrootfh(&xdr)) != 0)
		goto out;
	if ((status = encode_getfh(&xdr)) == 0)
		status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

/*
 * Encode REMOVE request
 */
static int nfs4_xdr_enc_remove(struct rpc_rqst *req, uint32_t *p, const struct nfs4_remove_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	if ((status = encode_putfh(&xdr, args->fh)) != 0)
		goto out;
	if ((status = encode_remove(&xdr, args->name)) != 0)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

/*
 * Encode RENAME request
 */
static int nfs4_xdr_enc_rename(struct rpc_rqst *req, uint32_t *p, const struct nfs4_rename_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 7,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	if ((status = encode_putfh(&xdr, args->old_dir)) != 0)
		goto out;
	if ((status = encode_savefh(&xdr)) != 0)
		goto out;
	if ((status = encode_putfh(&xdr, args->new_dir)) != 0)
		goto out;
	if ((status = encode_rename(&xdr, args->old_name, args->new_name)) != 0)
		goto out;
	if ((status = encode_getfattr(&xdr, args->bitmask)) != 0)
		goto out;
	if ((status = encode_restorefh(&xdr)) != 0)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

/*
 * Encode LINK request
 */
static int nfs4_xdr_enc_link(struct rpc_rqst *req, uint32_t *p, const struct nfs4_link_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 7,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	if ((status = encode_putfh(&xdr, args->fh)) != 0)
		goto out;
	if ((status = encode_savefh(&xdr)) != 0)
		goto out;
	if ((status = encode_putfh(&xdr, args->dir_fh)) != 0)
		goto out;
	if ((status = encode_link(&xdr, args->name)) != 0)
		goto out;
	if ((status = encode_getfattr(&xdr, args->bitmask)) != 0)
		goto out;
	if ((status = encode_restorefh(&xdr)) != 0)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

/*
 * Encode CREATE request
 */
static int nfs4_xdr_enc_create(struct rpc_rqst *req, uint32_t *p, const struct nfs4_create_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 7,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	if ((status = encode_putfh(&xdr, args->dir_fh)) != 0)
		goto out;
	if ((status = encode_savefh(&xdr)) != 0)
		goto out;
	if ((status = encode_create(&xdr, args)) != 0)
		goto out;
	if ((status = encode_getfh(&xdr)) != 0)
		goto out;
	if ((status = encode_getfattr(&xdr, args->bitmask)) != 0)
		goto out;
	if ((status = encode_restorefh(&xdr)) != 0)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

/*
 * Encode SYMLINK request
 */
static int nfs4_xdr_enc_symlink(struct rpc_rqst *req, uint32_t *p, const struct nfs4_create_arg *args)
{
	return nfs4_xdr_enc_create(req, p, args);
}

/*
 * Encode GETATTR request
 */
static int nfs4_xdr_enc_getattr(struct rpc_rqst *req, uint32_t *p, const struct nfs4_getattr_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	if ((status = encode_putfh(&xdr, args->fh)) == 0)
		status = encode_getfattr(&xdr, args->bitmask);
	return status;
}

/*
 * Encode a CLOSE request
 */
static int nfs4_xdr_enc_close(struct rpc_rqst *req, uint32_t *p, struct nfs_closeargs *args)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr = {
                .nops   = 3,
        };
        int status;

        xdr_init_encode(&xdr, &req->rq_snd_buf, p);
        encode_compound_hdr(&xdr, &hdr);
        status = encode_putfh(&xdr, args->fh);
        if(status)
                goto out;
        status = encode_close(&xdr, args);
	if (status != 0)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
        return status;
}

/*
 * Encode an OPEN request
 */
static int nfs4_xdr_enc_open(struct rpc_rqst *req, uint32_t *p, struct nfs_openargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 7,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_savefh(&xdr);
	if (status)
		goto out;
	status = encode_open(&xdr, args);
	if (status)
		goto out;
	status = encode_getfh(&xdr);
	if (status)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
	if (status)
		goto out;
	status = encode_restorefh(&xdr);
	if (status)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

/*
 * Encode an OPEN_CONFIRM request
 */
static int nfs4_xdr_enc_open_confirm(struct rpc_rqst *req, uint32_t *p, struct nfs_open_confirmargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if(status)
		goto out;
	status = encode_open_confirm(&xdr, args);
out:
	return status;
}

/*
 * Encode an OPEN request with no attributes.
 */
static int nfs4_xdr_enc_open_noattr(struct rpc_rqst *req, uint32_t *p, struct nfs_openargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 3,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_open(&xdr, args);
	if (status)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

/*
 * Encode an OPEN_DOWNGRADE request
 */
static int nfs4_xdr_enc_open_downgrade(struct rpc_rqst *req, uint32_t *p, struct nfs_closeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 3,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_open_downgrade(&xdr, args);
	if (status != 0)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

/*
 * Encode a LOCK request
 */
static int nfs4_xdr_enc_lock(struct rpc_rqst *req, uint32_t *p, struct nfs_lock_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if(status)
		goto out;
	status = encode_lock(&xdr, args);
out:
	return status;
}

/*
 * Encode a LOCKT request
 */
static int nfs4_xdr_enc_lockt(struct rpc_rqst *req, uint32_t *p, struct nfs_lockt_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if(status)
		goto out;
	status = encode_lockt(&xdr, args);
out:
	return status;
}

/*
 * Encode a LOCKU request
 */
static int nfs4_xdr_enc_locku(struct rpc_rqst *req, uint32_t *p, struct nfs_locku_args *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops   = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if(status)
		goto out;
	status = encode_locku(&xdr, args);
out:
	return status;
}

/*
 * Encode a READLINK request
 */
static int nfs4_xdr_enc_readlink(struct rpc_rqst *req, uint32_t *p, const struct nfs4_readlink *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if(status)
		goto out;
	status = encode_readlink(&xdr, args, req);
out:
	return status;
}

/*
 * Encode a READDIR request
 */
static int nfs4_xdr_enc_readdir(struct rpc_rqst *req, uint32_t *p, const struct nfs4_readdir_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if(status)
		goto out;
	status = encode_readdir(&xdr, args, req);
out:
	return status;
}

/*
 * Encode a READ request
 */
static int nfs4_xdr_enc_read(struct rpc_rqst *req, uint32_t *p, struct nfs_readargs *args)
{
	struct rpc_auth	*auth = req->rq_task->tk_auth;
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};
	int replen, status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_read(&xdr, args);
	if (status)
		goto out;

	/* set up reply kvec
	 *    toplevel status + taglen=0 + rescount + OP_PUTFH + status
	 *       + OP_READ + status + eof + datalen = 9
	 */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + NFS4_dec_read_sz) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen,
			 args->pages, args->pgbase, args->count);
out:
	return status;
}

/*
 * Encode an SETATTR request
 */
static int nfs4_xdr_enc_setattr(struct rpc_rqst *req, uint32_t *p, struct nfs_setattrargs *args)

{
        struct xdr_stream xdr;
        struct compound_hdr hdr = {
                .nops   = 3,
        };
        int status;

        xdr_init_encode(&xdr, &req->rq_snd_buf, p);
        encode_compound_hdr(&xdr, &hdr);
        status = encode_putfh(&xdr, args->fh);
        if(status)
                goto out;
        status = encode_setattr(&xdr, args, args->server);
        if(status)
                goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
        return status;
}

/*
 * Encode a GETACL request
 */
static int
nfs4_xdr_enc_getacl(struct rpc_rqst *req, uint32_t *p,
		struct nfs_getaclargs *args)
{
	struct xdr_stream xdr;
	struct rpc_auth *auth = req->rq_task->tk_auth;
	struct compound_hdr hdr = {
		.nops   = 2,
	};
	int replen, status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_getattr_two(&xdr, FATTR4_WORD0_ACL, 0);
	/* set up reply buffer: */
	replen = (RPC_REPHDRSIZE + auth->au_rslack + NFS4_dec_getacl_sz) << 2;
	xdr_inline_pages(&req->rq_rcv_buf, replen,
		args->acl_pages, args->acl_pgbase, args->acl_len);
out:
	return status;
}

/*
 * Encode a WRITE request
 */
static int nfs4_xdr_enc_write(struct rpc_rqst *req, uint32_t *p, struct nfs_writeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_write(&xdr, args);
	if (status)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

/*
 *  a COMMIT request
 */
static int nfs4_xdr_enc_commit(struct rpc_rqst *req, uint32_t *p, struct nfs_writeargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status)
		goto out;
	status = encode_commit(&xdr, args);
	if (status)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
}

/*
 * FSINFO request
 */
static int nfs4_xdr_enc_fsinfo(struct rpc_rqst *req, uint32_t *p, struct nfs4_fsinfo_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (!status)
		status = encode_fsinfo(&xdr, args->bitmask);
	return status;
}

/*
 * a PATHCONF request
 */
static int nfs4_xdr_enc_pathconf(struct rpc_rqst *req, uint32_t *p, const struct nfs4_pathconf_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (!status)
		status = encode_getattr_one(&xdr,
				args->bitmask[0] & nfs4_pathconf_bitmap[0]);
	return status;
}

/*
 * a STATFS request
 */
static int nfs4_xdr_enc_statfs(struct rpc_rqst *req, uint32_t *p, const struct nfs4_statfs_arg *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fh);
	if (status == 0)
		status = encode_getattr_two(&xdr,
				args->bitmask[0] & nfs4_statfs_bitmap[0],
				args->bitmask[1] & nfs4_statfs_bitmap[1]);
	return status;
}

/*
 * GETATTR_BITMAP request
 */
static int nfs4_xdr_enc_server_caps(struct rpc_rqst *req, uint32_t *p, const struct nfs_fh *fhandle)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 2,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, fhandle);
	if (status == 0)
		status = encode_getattr_one(&xdr, FATTR4_WORD0_SUPPORTED_ATTRS|
				FATTR4_WORD0_LINK_SUPPORT|
				FATTR4_WORD0_SYMLINK_SUPPORT|
				FATTR4_WORD0_ACLSUPPORT);
	return status;
}

/*
 * a RENEW request
 */
static int nfs4_xdr_enc_renew(struct rpc_rqst *req, uint32_t *p, struct nfs4_client *clp)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 1,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	return encode_renew(&xdr, clp);
}

/*
 * a SETCLIENTID request
 */
static int nfs4_xdr_enc_setclientid(struct rpc_rqst *req, uint32_t *p, struct nfs4_setclientid *sc)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 1,
	};

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	return encode_setclientid(&xdr, sc);
}

/*
 * a SETCLIENTID_CONFIRM request
 */
static int nfs4_xdr_enc_setclientid_confirm(struct rpc_rqst *req, uint32_t *p, struct nfs4_client *clp)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops	= 3,
	};
	const u32 lease_bitmap[2] = { FATTR4_WORD0_LEASE_TIME, 0 };
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_setclientid_confirm(&xdr, clp);
	if (!status)
		status = encode_putrootfh(&xdr);
	if (!status)
		status = encode_fsinfo(&xdr, lease_bitmap);
	return status;
}

/*
 * DELEGRETURN request
 */
static int nfs4_xdr_enc_delegreturn(struct rpc_rqst *req, uint32_t *p, const struct nfs4_delegreturnargs *args)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr = {
		.nops = 3,
	};
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	encode_compound_hdr(&xdr, &hdr);
	status = encode_putfh(&xdr, args->fhandle);
	if (status != 0)
		goto out;
	status = encode_delegreturn(&xdr, args->stateid);
	if (status != 0)
		goto out;
	status = encode_getfattr(&xdr, args->bitmask);
out:
	return status;
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
#define READ32(x)         (x) = ntohl(*p++)
#define READ64(x)         do {			\
	(x) = (u64)ntohl(*p++) << 32;		\
	(x) |= ntohl(*p++);			\
} while (0)
#define READTIME(x)       do {			\
	p++;					\
	(x.tv_sec) = ntohl(*p++);		\
	(x.tv_nsec) = ntohl(*p++);		\
} while (0)
#define COPYMEM(x,nbytes) do {			\
	memcpy((x), p, nbytes);			\
	p += XDR_QUADLEN(nbytes);		\
} while (0)

#define READ_BUF(nbytes)  do { \
	p = xdr_inline_decode(xdr, nbytes); \
	if (!p) { \
		printk(KERN_WARNING "%s: reply buffer overflowed in line %d.", \
			       	__FUNCTION__, __LINE__); \
		return -EIO; \
	} \
} while (0)

static int decode_opaque_inline(struct xdr_stream *xdr, uint32_t *len, char **string)
{
	uint32_t *p;

	READ_BUF(4);
	READ32(*len);
	READ_BUF(*len);
	*string = (char *)p;
	return 0;
}

static int decode_compound_hdr(struct xdr_stream *xdr, struct compound_hdr *hdr)
{
	uint32_t *p;

	READ_BUF(8);
	READ32(hdr->status);
	READ32(hdr->taglen);
	
	READ_BUF(hdr->taglen + 4);
	hdr->tag = (char *)p;
	p += XDR_QUADLEN(hdr->taglen);
	READ32(hdr->nops);
	return 0;
}

static int decode_op_hdr(struct xdr_stream *xdr, enum nfs_opnum4 expected)
{
	uint32_t *p;
	uint32_t opnum;
	int32_t nfserr;

	READ_BUF(8);
	READ32(opnum);
	if (opnum != expected) {
		printk(KERN_NOTICE
				"nfs4_decode_op_hdr: Server returned operation"
			       	" %d but we issued a request for %d\n",
				opnum, expected);
		return -EIO;
	}
	READ32(nfserr);
	if (nfserr != NFS_OK)
		return -nfs_stat_to_errno(nfserr);
	return 0;
}

/* Dummy routine */
static int decode_ace(struct xdr_stream *xdr, void *ace, struct nfs4_client *clp)
{
	uint32_t *p;
	uint32_t strlen;
	char *str;

	READ_BUF(12);
	return decode_opaque_inline(xdr, &strlen, &str);
}

static int decode_attr_bitmap(struct xdr_stream *xdr, uint32_t *bitmap)
{
	uint32_t bmlen, *p;

	READ_BUF(4);
	READ32(bmlen);

	bitmap[0] = bitmap[1] = 0;
	READ_BUF((bmlen << 2));
	if (bmlen > 0) {
		READ32(bitmap[0]);
		if (bmlen > 1)
			READ32(bitmap[1]);
	}
	return 0;
}

static inline int decode_attr_length(struct xdr_stream *xdr, uint32_t *attrlen, uint32_t **savep)
{
	uint32_t *p;

	READ_BUF(4);
	READ32(*attrlen);
	*savep = xdr->p;
	return 0;
}

static int decode_attr_supported(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *bitmask)
{
	if (likely(bitmap[0] & FATTR4_WORD0_SUPPORTED_ATTRS)) {
		decode_attr_bitmap(xdr, bitmask);
		bitmap[0] &= ~FATTR4_WORD0_SUPPORTED_ATTRS;
	} else
		bitmask[0] = bitmask[1] = 0;
	dprintk("%s: bitmask=0x%x%x\n", __FUNCTION__, bitmask[0], bitmask[1]);
	return 0;
}

static int decode_attr_type(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *type)
{
	uint32_t *p;

	*type = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_TYPE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_TYPE)) {
		READ_BUF(4);
		READ32(*type);
		if (*type < NF4REG || *type > NF4NAMEDATTR) {
			dprintk("%s: bad type %d\n", __FUNCTION__, *type);
			return -EIO;
		}
		bitmap[0] &= ~FATTR4_WORD0_TYPE;
	}
	dprintk("%s: type=0%o\n", __FUNCTION__, nfs_type2fmt[*type].nfs2type);
	return 0;
}

static int decode_attr_change(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *change)
{
	uint32_t *p;

	*change = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_CHANGE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_CHANGE)) {
		READ_BUF(8);
		READ64(*change);
		bitmap[0] &= ~FATTR4_WORD0_CHANGE;
	}
	dprintk("%s: change attribute=%Lu\n", __FUNCTION__,
			(unsigned long long)*change);
	return 0;
}

static int decode_attr_size(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *size)
{
	uint32_t *p;

	*size = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_SIZE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_SIZE)) {
		READ_BUF(8);
		READ64(*size);
		bitmap[0] &= ~FATTR4_WORD0_SIZE;
	}
	dprintk("%s: file size=%Lu\n", __FUNCTION__, (unsigned long long)*size);
	return 0;
}

static int decode_attr_link_support(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	uint32_t *p;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_LINK_SUPPORT - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_LINK_SUPPORT)) {
		READ_BUF(4);
		READ32(*res);
		bitmap[0] &= ~FATTR4_WORD0_LINK_SUPPORT;
	}
	dprintk("%s: link support=%s\n", __FUNCTION__, *res == 0 ? "false" : "true");
	return 0;
}

static int decode_attr_symlink_support(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	uint32_t *p;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_SYMLINK_SUPPORT - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_SYMLINK_SUPPORT)) {
		READ_BUF(4);
		READ32(*res);
		bitmap[0] &= ~FATTR4_WORD0_SYMLINK_SUPPORT;
	}
	dprintk("%s: symlink support=%s\n", __FUNCTION__, *res == 0 ? "false" : "true");
	return 0;
}

static int decode_attr_fsid(struct xdr_stream *xdr, uint32_t *bitmap, struct nfs4_fsid *fsid)
{
	uint32_t *p;

	fsid->major = 0;
	fsid->minor = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FSID - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FSID)) {
		READ_BUF(16);
		READ64(fsid->major);
		READ64(fsid->minor);
		bitmap[0] &= ~FATTR4_WORD0_FSID;
	}
	dprintk("%s: fsid=(0x%Lx/0x%Lx)\n", __FUNCTION__,
			(unsigned long long)fsid->major,
			(unsigned long long)fsid->minor);
	return 0;
}

static int decode_attr_lease_time(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	uint32_t *p;

	*res = 60;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_LEASE_TIME - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_LEASE_TIME)) {
		READ_BUF(4);
		READ32(*res);
		bitmap[0] &= ~FATTR4_WORD0_LEASE_TIME;
	}
	dprintk("%s: file size=%u\n", __FUNCTION__, (unsigned int)*res);
	return 0;
}

static int decode_attr_aclsupport(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	uint32_t *p;

	*res = ACL4_SUPPORT_ALLOW_ACL|ACL4_SUPPORT_DENY_ACL;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_ACLSUPPORT - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_ACLSUPPORT)) {
		READ_BUF(4);
		READ32(*res);
		bitmap[0] &= ~FATTR4_WORD0_ACLSUPPORT;
	}
	dprintk("%s: ACLs supported=%u\n", __FUNCTION__, (unsigned int)*res);
	return 0;
}

static int decode_attr_fileid(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *fileid)
{
	uint32_t *p;

	*fileid = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILEID - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILEID)) {
		READ_BUF(8);
		READ64(*fileid);
		bitmap[0] &= ~FATTR4_WORD0_FILEID;
	}
	dprintk("%s: fileid=%Lu\n", __FUNCTION__, (unsigned long long)*fileid);
	return 0;
}

static int decode_attr_files_avail(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	uint32_t *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILES_AVAIL - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILES_AVAIL)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_AVAIL;
	}
	dprintk("%s: files avail=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_files_free(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	uint32_t *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILES_FREE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILES_FREE)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_FREE;
	}
	dprintk("%s: files free=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_files_total(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	uint32_t *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_FILES_TOTAL - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_FILES_TOTAL)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[0] &= ~FATTR4_WORD0_FILES_TOTAL;
	}
	dprintk("%s: files total=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_maxfilesize(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	uint32_t *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXFILESIZE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXFILESIZE)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[0] &= ~FATTR4_WORD0_MAXFILESIZE;
	}
	dprintk("%s: maxfilesize=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_maxlink(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *maxlink)
{
	uint32_t *p;
	int status = 0;

	*maxlink = 1;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXLINK - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXLINK)) {
		READ_BUF(4);
		READ32(*maxlink);
		bitmap[0] &= ~FATTR4_WORD0_MAXLINK;
	}
	dprintk("%s: maxlink=%u\n", __FUNCTION__, *maxlink);
	return status;
}

static int decode_attr_maxname(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *maxname)
{
	uint32_t *p;
	int status = 0;

	*maxname = 1024;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXNAME - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXNAME)) {
		READ_BUF(4);
		READ32(*maxname);
		bitmap[0] &= ~FATTR4_WORD0_MAXNAME;
	}
	dprintk("%s: maxname=%u\n", __FUNCTION__, *maxname);
	return status;
}

static int decode_attr_maxread(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	uint32_t *p;
	int status = 0;

	*res = 1024;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXREAD - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXREAD)) {
		uint64_t maxread;
		READ_BUF(8);
		READ64(maxread);
		if (maxread > 0x7FFFFFFF)
			maxread = 0x7FFFFFFF;
		*res = (uint32_t)maxread;
		bitmap[0] &= ~FATTR4_WORD0_MAXREAD;
	}
	dprintk("%s: maxread=%lu\n", __FUNCTION__, (unsigned long)*res);
	return status;
}

static int decode_attr_maxwrite(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *res)
{
	uint32_t *p;
	int status = 0;

	*res = 1024;
	if (unlikely(bitmap[0] & (FATTR4_WORD0_MAXWRITE - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_MAXWRITE)) {
		uint64_t maxwrite;
		READ_BUF(8);
		READ64(maxwrite);
		if (maxwrite > 0x7FFFFFFF)
			maxwrite = 0x7FFFFFFF;
		*res = (uint32_t)maxwrite;
		bitmap[0] &= ~FATTR4_WORD0_MAXWRITE;
	}
	dprintk("%s: maxwrite=%lu\n", __FUNCTION__, (unsigned long)*res);
	return status;
}

static int decode_attr_mode(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *mode)
{
	uint32_t *p;

	*mode = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_MODE - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_MODE)) {
		READ_BUF(4);
		READ32(*mode);
		*mode &= ~S_IFMT;
		bitmap[1] &= ~FATTR4_WORD1_MODE;
	}
	dprintk("%s: file mode=0%o\n", __FUNCTION__, (unsigned int)*mode);
	return 0;
}

static int decode_attr_nlink(struct xdr_stream *xdr, uint32_t *bitmap, uint32_t *nlink)
{
	uint32_t *p;

	*nlink = 1;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_NUMLINKS - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_NUMLINKS)) {
		READ_BUF(4);
		READ32(*nlink);
		bitmap[1] &= ~FATTR4_WORD1_NUMLINKS;
	}
	dprintk("%s: nlink=%u\n", __FUNCTION__, (unsigned int)*nlink);
	return 0;
}

static int decode_attr_owner(struct xdr_stream *xdr, uint32_t *bitmap, struct nfs4_client *clp, int32_t *uid)
{
	uint32_t len, *p;

	*uid = -2;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_OWNER - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_OWNER)) {
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);
		if (len < XDR_MAX_NETOBJ) {
			if (nfs_map_name_to_uid(clp, (char *)p, len, uid) != 0)
				dprintk("%s: nfs_map_name_to_uid failed!\n",
						__FUNCTION__);
		} else
			printk(KERN_WARNING "%s: name too long (%u)!\n",
					__FUNCTION__, len);
		bitmap[1] &= ~FATTR4_WORD1_OWNER;
	}
	dprintk("%s: uid=%d\n", __FUNCTION__, (int)*uid);
	return 0;
}

static int decode_attr_group(struct xdr_stream *xdr, uint32_t *bitmap, struct nfs4_client *clp, int32_t *gid)
{
	uint32_t len, *p;

	*gid = -2;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_OWNER_GROUP - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_OWNER_GROUP)) {
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);
		if (len < XDR_MAX_NETOBJ) {
			if (nfs_map_group_to_gid(clp, (char *)p, len, gid) != 0)
				dprintk("%s: nfs_map_group_to_gid failed!\n",
						__FUNCTION__);
		} else
			printk(KERN_WARNING "%s: name too long (%u)!\n",
					__FUNCTION__, len);
		bitmap[1] &= ~FATTR4_WORD1_OWNER_GROUP;
	}
	dprintk("%s: gid=%d\n", __FUNCTION__, (int)*gid);
	return 0;
}

static int decode_attr_rdev(struct xdr_stream *xdr, uint32_t *bitmap, dev_t *rdev)
{
	uint32_t major = 0, minor = 0, *p;

	*rdev = MKDEV(0,0);
	if (unlikely(bitmap[1] & (FATTR4_WORD1_RAWDEV - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_RAWDEV)) {
		dev_t tmp;

		READ_BUF(8);
		READ32(major);
		READ32(minor);
		tmp = MKDEV(major, minor);
		if (MAJOR(tmp) == major && MINOR(tmp) == minor)
			*rdev = tmp;
		bitmap[1] &= ~ FATTR4_WORD1_RAWDEV;
	}
	dprintk("%s: rdev=(0x%x:0x%x)\n", __FUNCTION__, major, minor);
	return 0;
}

static int decode_attr_space_avail(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	uint32_t *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_AVAIL - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_AVAIL)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_AVAIL;
	}
	dprintk("%s: space avail=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_space_free(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	uint32_t *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_FREE - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_FREE)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_FREE;
	}
	dprintk("%s: space free=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_space_total(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *res)
{
	uint32_t *p;
	int status = 0;

	*res = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_TOTAL - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_TOTAL)) {
		READ_BUF(8);
		READ64(*res);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_TOTAL;
	}
	dprintk("%s: space total=%Lu\n", __FUNCTION__, (unsigned long long)*res);
	return status;
}

static int decode_attr_space_used(struct xdr_stream *xdr, uint32_t *bitmap, uint64_t *used)
{
	uint32_t *p;

	*used = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_SPACE_USED - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_SPACE_USED)) {
		READ_BUF(8);
		READ64(*used);
		bitmap[1] &= ~FATTR4_WORD1_SPACE_USED;
	}
	dprintk("%s: space used=%Lu\n", __FUNCTION__,
			(unsigned long long)*used);
	return 0;
}

static int decode_attr_time(struct xdr_stream *xdr, struct timespec *time)
{
	uint32_t *p;
	uint64_t sec;
	uint32_t nsec;

	READ_BUF(12);
	READ64(sec);
	READ32(nsec);
	time->tv_sec = (time_t)sec;
	time->tv_nsec = (long)nsec;
	return 0;
}

static int decode_attr_time_access(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec *time)
{
	int status = 0;

	time->tv_sec = 0;
	time->tv_nsec = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_TIME_ACCESS - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_TIME_ACCESS)) {
		status = decode_attr_time(xdr, time);
		bitmap[1] &= ~FATTR4_WORD1_TIME_ACCESS;
	}
	dprintk("%s: atime=%ld\n", __FUNCTION__, (long)time->tv_sec);
	return status;
}

static int decode_attr_time_metadata(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec *time)
{
	int status = 0;

	time->tv_sec = 0;
	time->tv_nsec = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_TIME_METADATA - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_TIME_METADATA)) {
		status = decode_attr_time(xdr, time);
		bitmap[1] &= ~FATTR4_WORD1_TIME_METADATA;
	}
	dprintk("%s: ctime=%ld\n", __FUNCTION__, (long)time->tv_sec);
	return status;
}

static int decode_attr_time_modify(struct xdr_stream *xdr, uint32_t *bitmap, struct timespec *time)
{
	int status = 0;

	time->tv_sec = 0;
	time->tv_nsec = 0;
	if (unlikely(bitmap[1] & (FATTR4_WORD1_TIME_MODIFY - 1U)))
		return -EIO;
	if (likely(bitmap[1] & FATTR4_WORD1_TIME_MODIFY)) {
		status = decode_attr_time(xdr, time);
		bitmap[1] &= ~FATTR4_WORD1_TIME_MODIFY;
	}
	dprintk("%s: mtime=%ld\n", __FUNCTION__, (long)time->tv_sec);
	return status;
}

static int verify_attr_len(struct xdr_stream *xdr, uint32_t *savep, uint32_t attrlen)
{
	unsigned int attrwords = XDR_QUADLEN(attrlen);
	unsigned int nwords = xdr->p - savep;

	if (unlikely(attrwords != nwords)) {
		printk(KERN_WARNING "%s: server returned incorrect attribute length: %u %c %u\n",
				__FUNCTION__,
				attrwords << 2,
				(attrwords < nwords) ? '<' : '>',
				nwords << 2);
		return -EIO;
	}
	return 0;
}

static int decode_change_info(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	uint32_t *p;

	READ_BUF(20);
	READ32(cinfo->atomic);
	READ64(cinfo->before);
	READ64(cinfo->after);
	return 0;
}

static int decode_access(struct xdr_stream *xdr, struct nfs4_accessres *access)
{
	uint32_t *p;
	uint32_t supp, acc;
	int status;

	status = decode_op_hdr(xdr, OP_ACCESS);
	if (status)
		return status;
	READ_BUF(8);
	READ32(supp);
	READ32(acc);
	access->supported = supp;
	access->access = acc;
	return 0;
}

static int decode_close(struct xdr_stream *xdr, struct nfs_closeres *res)
{
	uint32_t *p;
	int status;

	status = decode_op_hdr(xdr, OP_CLOSE);
	if (status)
		return status;
	READ_BUF(sizeof(res->stateid.data));
	COPYMEM(res->stateid.data, sizeof(res->stateid.data));
	return 0;
}

static int decode_commit(struct xdr_stream *xdr, struct nfs_writeres *res)
{
	uint32_t *p;
	int status;

	status = decode_op_hdr(xdr, OP_COMMIT);
	if (status)
		return status;
	READ_BUF(8);
	COPYMEM(res->verf->verifier, 8);
	return 0;
}

static int decode_create(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	uint32_t *p;
	uint32_t bmlen;
	int status;

	status = decode_op_hdr(xdr, OP_CREATE);
	if (status)
		return status;
	if ((status = decode_change_info(xdr, cinfo)))
		return status;
	READ_BUF(4);
	READ32(bmlen);
	READ_BUF(bmlen << 2);
	return 0;
}

static int decode_server_caps(struct xdr_stream *xdr, struct nfs4_server_caps_res *res)
{
	uint32_t *savep;
	uint32_t attrlen, 
		 bitmap[2] = {0};
	int status;

	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;
	if ((status = decode_attr_supported(xdr, bitmap, res->attr_bitmask)) != 0)
		goto xdr_error;
	if ((status = decode_attr_link_support(xdr, bitmap, &res->has_links)) != 0)
		goto xdr_error;
	if ((status = decode_attr_symlink_support(xdr, bitmap, &res->has_symlinks)) != 0)
		goto xdr_error;
	if ((status = decode_attr_aclsupport(xdr, bitmap, &res->acl_bitmask)) != 0)
		goto xdr_error;
	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __FUNCTION__, -status);
	return status;
}
	
static int decode_statfs(struct xdr_stream *xdr, struct nfs_fsstat *fsstat)
{
	uint32_t *savep;
	uint32_t attrlen, 
		 bitmap[2] = {0};
	int status;
	
	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;

	if ((status = decode_attr_files_avail(xdr, bitmap, &fsstat->afiles)) != 0)
		goto xdr_error;
	if ((status = decode_attr_files_free(xdr, bitmap, &fsstat->ffiles)) != 0)
		goto xdr_error;
	if ((status = decode_attr_files_total(xdr, bitmap, &fsstat->tfiles)) != 0)
		goto xdr_error;
	if ((status = decode_attr_space_avail(xdr, bitmap, &fsstat->abytes)) != 0)
		goto xdr_error;
	if ((status = decode_attr_space_free(xdr, bitmap, &fsstat->fbytes)) != 0)
		goto xdr_error;
	if ((status = decode_attr_space_total(xdr, bitmap, &fsstat->tbytes)) != 0)
		goto xdr_error;

	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __FUNCTION__, -status);
	return status;
}

static int decode_pathconf(struct xdr_stream *xdr, struct nfs_pathconf *pathconf)
{
	uint32_t *savep;
	uint32_t attrlen, 
		 bitmap[2] = {0};
	int status;
	
	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;

	if ((status = decode_attr_maxlink(xdr, bitmap, &pathconf->max_link)) != 0)
		goto xdr_error;
	if ((status = decode_attr_maxname(xdr, bitmap, &pathconf->max_namelen)) != 0)
		goto xdr_error;

	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __FUNCTION__, -status);
	return status;
}

static int decode_getfattr(struct xdr_stream *xdr, struct nfs_fattr *fattr, const struct nfs_server *server)
{
	uint32_t *savep;
	uint32_t attrlen,
		 bitmap[2] = {0},
		 type;
	int status, fmode = 0;

	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;

	fattr->bitmap[0] = bitmap[0];
	fattr->bitmap[1] = bitmap[1];

	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;


	if ((status = decode_attr_type(xdr, bitmap, &type)) != 0)
		goto xdr_error;
	fattr->type = nfs_type2fmt[type].nfs2type;
	fmode = nfs_type2fmt[type].mode;

	if ((status = decode_attr_change(xdr, bitmap, &fattr->change_attr)) != 0)
		goto xdr_error;
	if ((status = decode_attr_size(xdr, bitmap, &fattr->size)) != 0)
		goto xdr_error;
	if ((status = decode_attr_fsid(xdr, bitmap, &fattr->fsid_u.nfs4)) != 0)
		goto xdr_error;
	if ((status = decode_attr_fileid(xdr, bitmap, &fattr->fileid)) != 0)
		goto xdr_error;
	if ((status = decode_attr_mode(xdr, bitmap, &fattr->mode)) != 0)
		goto xdr_error;
	fattr->mode |= fmode;
	if ((status = decode_attr_nlink(xdr, bitmap, &fattr->nlink)) != 0)
		goto xdr_error;
	if ((status = decode_attr_owner(xdr, bitmap, server->nfs4_state, &fattr->uid)) != 0)
		goto xdr_error;
	if ((status = decode_attr_group(xdr, bitmap, server->nfs4_state, &fattr->gid)) != 0)
		goto xdr_error;
	if ((status = decode_attr_rdev(xdr, bitmap, &fattr->rdev)) != 0)
		goto xdr_error;
	if ((status = decode_attr_space_used(xdr, bitmap, &fattr->du.nfs3.used)) != 0)
		goto xdr_error;
	if ((status = decode_attr_time_access(xdr, bitmap, &fattr->atime)) != 0)
		goto xdr_error;
	if ((status = decode_attr_time_metadata(xdr, bitmap, &fattr->ctime)) != 0)
		goto xdr_error;
	if ((status = decode_attr_time_modify(xdr, bitmap, &fattr->mtime)) != 0)
		goto xdr_error;
	if ((status = verify_attr_len(xdr, savep, attrlen)) == 0)
		fattr->valid = NFS_ATTR_FATTR | NFS_ATTR_FATTR_V3 | NFS_ATTR_FATTR_V4;
xdr_error:
	dprintk("%s: xdr returned %d\n", __FUNCTION__, -status);
	return status;
}


static int decode_fsinfo(struct xdr_stream *xdr, struct nfs_fsinfo *fsinfo)
{
	uint32_t *savep;
	uint32_t attrlen, bitmap[2];
	int status;

	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto xdr_error;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto xdr_error;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto xdr_error;

	fsinfo->rtmult = fsinfo->wtmult = 512;	/* ??? */

	if ((status = decode_attr_lease_time(xdr, bitmap, &fsinfo->lease_time)) != 0)
		goto xdr_error;
	if ((status = decode_attr_maxfilesize(xdr, bitmap, &fsinfo->maxfilesize)) != 0)
		goto xdr_error;
	if ((status = decode_attr_maxread(xdr, bitmap, &fsinfo->rtmax)) != 0)
		goto xdr_error;
	fsinfo->rtpref = fsinfo->dtpref = fsinfo->rtmax;
	if ((status = decode_attr_maxwrite(xdr, bitmap, &fsinfo->wtmax)) != 0)
		goto xdr_error;
	fsinfo->wtpref = fsinfo->wtmax;

	status = verify_attr_len(xdr, savep, attrlen);
xdr_error:
	dprintk("%s: xdr returned %d!\n", __FUNCTION__, -status);
	return status;
}

static int decode_getfh(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	uint32_t *p;
	uint32_t len;
	int status;

	status = decode_op_hdr(xdr, OP_GETFH);
	if (status)
		return status;
	/* Zero handle first to allow comparisons */
	memset(fh, 0, sizeof(*fh));

	READ_BUF(4);
	READ32(len);
	if (len > NFS4_FHSIZE)
		return -EIO;
	fh->size = len;
	READ_BUF(len);
	COPYMEM(fh->data, len);
	return 0;
}

static int decode_link(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	int status;
	
	status = decode_op_hdr(xdr, OP_LINK);
	if (status)
		return status;
	return decode_change_info(xdr, cinfo);
}

/*
 * We create the owner, so we know a proper owner.id length is 4.
 */
static int decode_lock_denied (struct xdr_stream *xdr, struct file_lock *fl)
{
	uint64_t offset, length, clientid;
	uint32_t *p;
	uint32_t namelen, type;

	READ_BUF(32);
	READ64(offset);
	READ64(length);
	READ32(type);
	if (fl != NULL) {
		fl->fl_start = (loff_t)offset;
		fl->fl_end = fl->fl_start + (loff_t)length - 1;
		if (length == ~(uint64_t)0)
			fl->fl_end = OFFSET_MAX;
		fl->fl_type = F_WRLCK;
		if (type & 1)
			fl->fl_type = F_RDLCK;
		fl->fl_pid = 0;
	}
	READ64(clientid);
	READ32(namelen);
	READ_BUF(namelen);
	return -NFS4ERR_DENIED;
}

static int decode_lock(struct xdr_stream *xdr, struct nfs_lock_res *res)
{
	uint32_t *p;
	int status;

	status = decode_op_hdr(xdr, OP_LOCK);
	if (status == 0) {
		READ_BUF(sizeof(res->stateid.data));
		COPYMEM(res->stateid.data, sizeof(res->stateid.data));
	} else if (status == -NFS4ERR_DENIED)
		return decode_lock_denied(xdr, NULL);
	return status;
}

static int decode_lockt(struct xdr_stream *xdr, struct nfs_lockt_res *res)
{
	int status;
	status = decode_op_hdr(xdr, OP_LOCKT);
	if (status == -NFS4ERR_DENIED)
		return decode_lock_denied(xdr, res->denied);
	return status;
}

static int decode_locku(struct xdr_stream *xdr, struct nfs_locku_res *res)
{
	uint32_t *p;
	int status;

	status = decode_op_hdr(xdr, OP_LOCKU);
	if (status == 0) {
		READ_BUF(sizeof(res->stateid.data));
		COPYMEM(res->stateid.data, sizeof(res->stateid.data));
	}
	return status;
}

static int decode_lookup(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_LOOKUP);
}

/* This is too sick! */
static int decode_space_limit(struct xdr_stream *xdr, u64 *maxsize)
{
        uint32_t *p;
	uint32_t limit_type, nblocks, blocksize;

	READ_BUF(12);
	READ32(limit_type);
	switch (limit_type) {
		case 1:
			READ64(*maxsize);
			break;
		case 2:
			READ32(nblocks);
			READ32(blocksize);
			*maxsize = (uint64_t)nblocks * (uint64_t)blocksize;
	}
	return 0;
}

static int decode_delegation(struct xdr_stream *xdr, struct nfs_openres *res)
{
        uint32_t *p;
        uint32_t delegation_type;

	READ_BUF(4);
	READ32(delegation_type);
	if (delegation_type == NFS4_OPEN_DELEGATE_NONE) {
		res->delegation_type = 0;
		return 0;
	}
	READ_BUF(20);
	COPYMEM(res->delegation.data, sizeof(res->delegation.data));
	READ32(res->do_recall);
	switch (delegation_type) {
		case NFS4_OPEN_DELEGATE_READ:
			res->delegation_type = FMODE_READ;
			break;
		case NFS4_OPEN_DELEGATE_WRITE:
			res->delegation_type = FMODE_WRITE|FMODE_READ;
			if (decode_space_limit(xdr, &res->maxsize) < 0)
				return -EIO;
	}
	return decode_ace(xdr, NULL, res->server->nfs4_state);
}

static int decode_open(struct xdr_stream *xdr, struct nfs_openres *res)
{
        uint32_t *p;
        uint32_t bmlen;
        int status;

        status = decode_op_hdr(xdr, OP_OPEN);
        if (status)
                return status;
        READ_BUF(sizeof(res->stateid.data));
        COPYMEM(res->stateid.data, sizeof(res->stateid.data));

        decode_change_info(xdr, &res->cinfo);

        READ_BUF(8);
        READ32(res->rflags);
        READ32(bmlen);
        if (bmlen > 10)
                goto xdr_error;

        READ_BUF(bmlen << 2);
        p += bmlen;
	return decode_delegation(xdr, res);
xdr_error:
	dprintk("%s: Bitmap too large! Length = %u\n", __FUNCTION__, bmlen);
	return -EIO;
}

static int decode_open_confirm(struct xdr_stream *xdr, struct nfs_open_confirmres *res)
{
        uint32_t *p;
	int status;

        status = decode_op_hdr(xdr, OP_OPEN_CONFIRM);
        if (status)
                return status;
        READ_BUF(sizeof(res->stateid.data));
        COPYMEM(res->stateid.data, sizeof(res->stateid.data));
        return 0;
}

static int decode_open_downgrade(struct xdr_stream *xdr, struct nfs_closeres *res)
{
	uint32_t *p;
	int status;

	status = decode_op_hdr(xdr, OP_OPEN_DOWNGRADE);
	if (status)
		return status;
	READ_BUF(sizeof(res->stateid.data));
	COPYMEM(res->stateid.data, sizeof(res->stateid.data));
	return 0;
}

static int decode_putfh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_PUTFH);
}

static int decode_putrootfh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_PUTROOTFH);
}

static int decode_read(struct xdr_stream *xdr, struct rpc_rqst *req, struct nfs_readres *res)
{
	struct kvec *iov = req->rq_rcv_buf.head;
	uint32_t *p;
	uint32_t count, eof, recvd, hdrlen;
	int status;

	status = decode_op_hdr(xdr, OP_READ);
	if (status)
		return status;
	READ_BUF(8);
	READ32(eof);
	READ32(count);
	hdrlen = (u8 *) p - (u8 *) iov->iov_base;
	recvd = req->rq_rcv_buf.len - hdrlen;
	if (count > recvd) {
		printk(KERN_WARNING "NFS: server cheating in read reply: "
				"count %u > recvd %u\n", count, recvd);
		count = recvd;
		eof = 0;
	}
	xdr_read_pages(xdr, count);
	res->eof = eof;
	res->count = count;
	return 0;
}

static int decode_readdir(struct xdr_stream *xdr, struct rpc_rqst *req, struct nfs4_readdir_res *readdir)
{
	struct xdr_buf	*rcvbuf = &req->rq_rcv_buf;
	struct page	*page = *rcvbuf->pages;
	struct kvec	*iov = rcvbuf->head;
	unsigned int	nr, pglen = rcvbuf->page_len;
	uint32_t	*end, *entry, *p, *kaddr;
	uint32_t	len, attrlen;
	int 		hdrlen, recvd, status;

	status = decode_op_hdr(xdr, OP_READDIR);
	if (status)
		return status;
	READ_BUF(8);
	COPYMEM(readdir->verifier.data, 8);
	dprintk("%s: verifier = 0x%x%x\n",
			__FUNCTION__,
			((u32 *)readdir->verifier.data)[0],
			((u32 *)readdir->verifier.data)[1]);


	hdrlen = (char *) p - (char *) iov->iov_base;
	recvd = rcvbuf->len - hdrlen;
	if (pglen > recvd)
		pglen = recvd;
	xdr_read_pages(xdr, pglen);

	BUG_ON(pglen + readdir->pgbase > PAGE_CACHE_SIZE);
	kaddr = p = (uint32_t *) kmap_atomic(page, KM_USER0);
	end = (uint32_t *) ((char *)p + pglen + readdir->pgbase);
	entry = p;
	for (nr = 0; *p++; nr++) {
		if (p + 3 > end)
			goto short_pkt;
		dprintk("cookie = %Lu, ", *((unsigned long long *)p));
		p += 2;			/* cookie */
		len = ntohl(*p++);	/* filename length */
		if (len > NFS4_MAXNAMLEN) {
			printk(KERN_WARNING "NFS: giant filename in readdir (len 0x%x)\n", len);
			goto err_unmap;
		}
		dprintk("filename = %*s\n", len, (char *)p);
		p += XDR_QUADLEN(len);
		if (p + 1 > end)
			goto short_pkt;
		len = ntohl(*p++);	/* bitmap length */
		p += len;
		if (p + 1 > end)
			goto short_pkt;
		attrlen = XDR_QUADLEN(ntohl(*p++));
		p += attrlen;		/* attributes */
		if (p + 2 > end)
			goto short_pkt;
		entry = p;
	}
	if (!nr && (entry[0] != 0 || entry[1] == 0))
		goto short_pkt;
out:	
	kunmap_atomic(kaddr, KM_USER0);
	return 0;
short_pkt:
	dprintk("%s: short packet at entry %d\n", __FUNCTION__, nr);
	entry[0] = entry[1] = 0;
	/* truncate listing ? */
	if (!nr) {
		printk(KERN_NOTICE "NFS: readdir reply truncated!\n");
		entry[1] = 1;
	}
	goto out;
err_unmap:
	kunmap_atomic(kaddr, KM_USER0);
	return -errno_NFSERR_IO;
}

static int decode_readlink(struct xdr_stream *xdr, struct rpc_rqst *req)
{
	struct xdr_buf *rcvbuf = &req->rq_rcv_buf;
	struct kvec *iov = rcvbuf->head;
	int hdrlen, len, recvd;
	uint32_t *p;
	char *kaddr;
	int status;

	status = decode_op_hdr(xdr, OP_READLINK);
	if (status)
		return status;

	/* Convert length of symlink */
	READ_BUF(4);
	READ32(len);
	if (len >= rcvbuf->page_len || len <= 0) {
		dprintk(KERN_WARNING "nfs: server returned giant symlink!\n");
		return -ENAMETOOLONG;
	}
	hdrlen = (char *) xdr->p - (char *) iov->iov_base;
	recvd = req->rq_rcv_buf.len - hdrlen;
	if (recvd < len) {
		printk(KERN_WARNING "NFS: server cheating in readlink reply: "
				"count %u > recvd %u\n", len, recvd);
		return -EIO;
	}
	xdr_read_pages(xdr, len);
	/*
	 * The XDR encode routine has set things up so that
	 * the link text will be copied directly into the
	 * buffer.  We just have to do overflow-checking,
	 * and and null-terminate the text (the VFS expects
	 * null-termination).
	 */
	kaddr = (char *)kmap_atomic(rcvbuf->pages[0], KM_USER0);
	kaddr[len+rcvbuf->page_base] = '\0';
	kunmap_atomic(kaddr, KM_USER0);
	return 0;
}

static int decode_remove(struct xdr_stream *xdr, struct nfs4_change_info *cinfo)
{
	int status;

	status = decode_op_hdr(xdr, OP_REMOVE);
	if (status)
		goto out;
	status = decode_change_info(xdr, cinfo);
out:
	return status;
}

static int decode_rename(struct xdr_stream *xdr, struct nfs4_change_info *old_cinfo,
	      struct nfs4_change_info *new_cinfo)
{
	int status;

	status = decode_op_hdr(xdr, OP_RENAME);
	if (status)
		goto out;
	if ((status = decode_change_info(xdr, old_cinfo)))
		goto out;
	status = decode_change_info(xdr, new_cinfo);
out:
	return status;
}

static int decode_renew(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_RENEW);
}

static int
decode_restorefh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_RESTOREFH);
}

static int decode_getacl(struct xdr_stream *xdr, struct rpc_rqst *req,
		size_t *acl_len)
{
	uint32_t *savep;
	uint32_t attrlen,
		 bitmap[2] = {0};
	struct kvec *iov = req->rq_rcv_buf.head;
	int status;

	*acl_len = 0;
	if ((status = decode_op_hdr(xdr, OP_GETATTR)) != 0)
		goto out;
	if ((status = decode_attr_bitmap(xdr, bitmap)) != 0)
		goto out;
	if ((status = decode_attr_length(xdr, &attrlen, &savep)) != 0)
		goto out;

	if (unlikely(bitmap[0] & (FATTR4_WORD0_ACL - 1U)))
		return -EIO;
	if (likely(bitmap[0] & FATTR4_WORD0_ACL)) {
		int hdrlen, recvd;

		/* We ignore &savep and don't do consistency checks on
		 * the attr length.  Let userspace figure it out.... */
		hdrlen = (u8 *)xdr->p - (u8 *)iov->iov_base;
		recvd = req->rq_rcv_buf.len - hdrlen;
		if (attrlen > recvd) {
			printk(KERN_WARNING "NFS: server cheating in getattr"
					" acl reply: attrlen %u > recvd %u\n",
					attrlen, recvd);
			return -EINVAL;
		}
		if (attrlen <= *acl_len)
			xdr_read_pages(xdr, attrlen);
		*acl_len = attrlen;
	} else
		status = -EOPNOTSUPP;

out:
	return status;
}

static int
decode_savefh(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_SAVEFH);
}

static int decode_setattr(struct xdr_stream *xdr, struct nfs_setattrres *res)
{
	uint32_t *p;
	uint32_t bmlen;
	int status;

        
	status = decode_op_hdr(xdr, OP_SETATTR);
	if (status)
		return status;
	READ_BUF(4);
	READ32(bmlen);
	READ_BUF(bmlen << 2);
	return 0;
}

static int decode_setclientid(struct xdr_stream *xdr, struct nfs4_client *clp)
{
	uint32_t *p;
	uint32_t opnum;
	int32_t nfserr;

	READ_BUF(8);
	READ32(opnum);
	if (opnum != OP_SETCLIENTID) {
		printk(KERN_NOTICE
				"nfs4_decode_setclientid: Server returned operation"
			       	" %d\n", opnum);
		return -EIO;
	}
	READ32(nfserr);
	if (nfserr == NFS_OK) {
		READ_BUF(8 + sizeof(clp->cl_confirm.data));
		READ64(clp->cl_clientid);
		COPYMEM(clp->cl_confirm.data, sizeof(clp->cl_confirm.data));
	} else if (nfserr == NFSERR_CLID_INUSE) {
		uint32_t len;

		/* skip netid string */
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);

		/* skip uaddr string */
		READ_BUF(4);
		READ32(len);
		READ_BUF(len);
		return -NFSERR_CLID_INUSE;
	} else
		return -nfs_stat_to_errno(nfserr);

	return 0;
}

static int decode_setclientid_confirm(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_SETCLIENTID_CONFIRM);
}

static int decode_write(struct xdr_stream *xdr, struct nfs_writeres *res)
{
	uint32_t *p;
	int status;

	status = decode_op_hdr(xdr, OP_WRITE);
	if (status)
		return status;

	READ_BUF(16);
	READ32(res->count);
	READ32(res->verf->committed);
	COPYMEM(res->verf->verifier, 8);
	return 0;
}

static int decode_delegreturn(struct xdr_stream *xdr)
{
	return decode_op_hdr(xdr, OP_DELEGRETURN);
}

/*
 * Decode OPEN_DOWNGRADE response
 */
static int nfs4_xdr_dec_open_downgrade(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_closeres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;
        status = decode_putfh(&xdr);
        if (status)
                goto out;
        status = decode_open_downgrade(&xdr, res);
	if (status != 0)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server);
out:
        return status;
}

/*
 * END OF "GENERIC" DECODE ROUTINES.
 */

/*
 * Decode ACCESS response
 */
static int nfs4_xdr_dec_access(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_accessres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;
	
	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;
	if ((status = decode_putfh(&xdr)) == 0)
		status = decode_access(&xdr, res);
out:
	return status;
}

/*
 * Decode LOOKUP response
 */
static int nfs4_xdr_dec_lookup(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_lookup_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;
	
	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_lookup(&xdr)) != 0)
		goto out;
	if ((status = decode_getfh(&xdr, res->fh)) != 0)
		goto out;
	status = decode_getfattr(&xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Decode LOOKUP_ROOT response
 */
static int nfs4_xdr_dec_lookup_root(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_lookup_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;
	
	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;
	if ((status = decode_putrootfh(&xdr)) != 0)
		goto out;
	if ((status = decode_getfh(&xdr, res->fh)) == 0)
		status = decode_getfattr(&xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Decode REMOVE response
 */
static int nfs4_xdr_dec_remove(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_remove_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;
	
	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_remove(&xdr, &res->cinfo)) != 0)
		goto out;
	decode_getfattr(&xdr, res->dir_attr, res->server);
out:
	return status;
}

/*
 * Decode RENAME response
 */
static int nfs4_xdr_dec_rename(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_rename_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;
	
	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_savefh(&xdr)) != 0)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_rename(&xdr, &res->old_cinfo, &res->new_cinfo)) != 0)
		goto out;
	/* Current FH is target directory */
	if (decode_getfattr(&xdr, res->new_fattr, res->server) != 0)
		goto out;
	if ((status = decode_restorefh(&xdr)) != 0)
		goto out;
	decode_getfattr(&xdr, res->old_fattr, res->server);
out:
	return status;
}

/*
 * Decode LINK response
 */
static int nfs4_xdr_dec_link(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_link_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;
	
	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_savefh(&xdr)) != 0)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_link(&xdr, &res->cinfo)) != 0)
		goto out;
	/*
	 * Note order: OP_LINK leaves the directory as the current
	 *             filehandle.
	 */
	if (decode_getfattr(&xdr, res->dir_attr, res->server) != 0)
		goto out;
	if ((status = decode_restorefh(&xdr)) != 0)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * Decode CREATE response
 */
static int nfs4_xdr_dec_create(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_create_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;
	
	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	if ((status = decode_savefh(&xdr)) != 0)
		goto out;
	if ((status = decode_create(&xdr,&res->dir_cinfo)) != 0)
		goto out;
	if ((status = decode_getfh(&xdr, res->fh)) != 0)
		goto out;
	if (decode_getfattr(&xdr, res->fattr, res->server) != 0)
		goto out;
	if ((status = decode_restorefh(&xdr)) != 0)
		goto out;
	decode_getfattr(&xdr, res->dir_fattr, res->server);
out:
	return status;
}

/*
 * Decode SYMLINK response
 */
static int nfs4_xdr_dec_symlink(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_create_res *res)
{
	return nfs4_xdr_dec_create(rqstp, p, res);
}

/*
 * Decode GETATTR response
 */
static int nfs4_xdr_dec_getattr(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_getattr_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;
	
	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_getfattr(&xdr, res->fattr, res->server);
out:
	return status;

}

/*
 * Encode an SETACL request
 */
static int
nfs4_xdr_enc_setacl(struct rpc_rqst *req, uint32_t *p, struct nfs_setaclargs *args)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr = {
                .nops   = 2,
        };
        int status;

        xdr_init_encode(&xdr, &req->rq_snd_buf, p);
        encode_compound_hdr(&xdr, &hdr);
        status = encode_putfh(&xdr, args->fh);
        if (status)
                goto out;
        status = encode_setacl(&xdr, args);
out:
        return status;
}
/*
 * Decode SETACL response
 */
static int
nfs4_xdr_dec_setacl(struct rpc_rqst *rqstp, uint32_t *p, void *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_setattr(&xdr, res);
out:
	return status;
}

/*
 * Decode GETACL response
 */
static int
nfs4_xdr_dec_getacl(struct rpc_rqst *rqstp, uint32_t *p, size_t *acl_len)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_getacl(&xdr, rqstp, acl_len);

out:
	return status;
}

/*
 * Decode CLOSE response
 */
static int nfs4_xdr_dec_close(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_closeres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;
        status = decode_putfh(&xdr);
        if (status)
                goto out;
        status = decode_close(&xdr, res);
	if (status != 0)
		goto out;
	/*
	 * Note: Server may do delete on close for this file
	 * 	in which case the getattr call will fail with
	 * 	an ESTALE error. Shouldn't be a problem,
	 * 	though, since fattr->valid will remain unset.
	 */
	decode_getfattr(&xdr, res->fattr, res->server);
out:
        return status;
}

/*
 * Decode OPEN response
 */
static int nfs4_xdr_dec_open(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_openres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;
        status = decode_putfh(&xdr);
        if (status)
                goto out;
        status = decode_savefh(&xdr);
	if (status)
		goto out;
        status = decode_open(&xdr, res);
        if (status)
                goto out;
	status = decode_getfh(&xdr, &res->fh);
        if (status)
		goto out;
	if (decode_getfattr(&xdr, res->f_attr, res->server) != 0)
		goto out;
	if ((status = decode_restorefh(&xdr)) != 0)
		goto out;
	decode_getfattr(&xdr, res->dir_attr, res->server);
out:
        return status;
}

/*
 * Decode OPEN_CONFIRM response
 */
static int nfs4_xdr_dec_open_confirm(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_open_confirmres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;
        status = decode_putfh(&xdr);
        if (status)
                goto out;
        status = decode_open_confirm(&xdr, res);
out:
        return status;
}

/*
 * Decode OPEN response
 */
static int nfs4_xdr_dec_open_noattr(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_openres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;
        status = decode_putfh(&xdr);
        if (status)
                goto out;
        status = decode_open(&xdr, res);
        if (status)
                goto out;
	decode_getfattr(&xdr, res->f_attr, res->server);
out:
        return status;
}

/*
 * Decode SETATTR response
 */
static int nfs4_xdr_dec_setattr(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_setattrres *res)
{
        struct xdr_stream xdr;
        struct compound_hdr hdr;
        int status;

        xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
        status = decode_compound_hdr(&xdr, &hdr);
        if (status)
                goto out;
        status = decode_putfh(&xdr);
        if (status)
                goto out;
        status = decode_setattr(&xdr, res);
        if (status)
                goto out;
	status = decode_getfattr(&xdr, res->fattr, res->server);
	if (status == NFS4ERR_DELAY)
		status = 0;
out:
        return status;
}

/*
 * Decode LOCK response
 */
static int nfs4_xdr_dec_lock(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_lock_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_lock(&xdr, res);
out:
	return status;
}

/*
 * Decode LOCKT response
 */
static int nfs4_xdr_dec_lockt(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_lockt_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_lockt(&xdr, res);
out:
	return status;
}

/*
 * Decode LOCKU response
 */
static int nfs4_xdr_dec_locku(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_locku_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_locku(&xdr, res);
out:
	return status;
}

/*
 * Decode READLINK response
 */
static int nfs4_xdr_dec_readlink(struct rpc_rqst *rqstp, uint32_t *p, void *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_readlink(&xdr, rqstp);
out:
	return status;
}

/*
 * Decode READDIR response
 */
static int nfs4_xdr_dec_readdir(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_readdir_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_readdir(&xdr, rqstp, res);
out:
	return status;
}

/*
 * Decode Read response
 */
static int nfs4_xdr_dec_read(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_readres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_read(&xdr, rqstp, res);
	if (!status)
		status = res->count;
out:
	return status;
}

/*
 * Decode WRITE response
 */
static int nfs4_xdr_dec_write(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_writeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_write(&xdr, res);
	if (status)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server);
	if (!status)
		status = res->count;
out:
	return status;
}

/*
 * Decode COMMIT response
 */
static int nfs4_xdr_dec_commit(struct rpc_rqst *rqstp, uint32_t *p, struct nfs_writeres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status)
		goto out;
	status = decode_putfh(&xdr);
	if (status)
		goto out;
	status = decode_commit(&xdr, res);
	if (status)
		goto out;
	decode_getfattr(&xdr, res->fattr, res->server);
out:
	return status;
}

/*
 * FSINFO request
 */
static int nfs4_xdr_dec_fsinfo(struct rpc_rqst *req, uint32_t *p, struct nfs_fsinfo *fsinfo)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_putfh(&xdr);
	if (!status)
		status = decode_fsinfo(&xdr, fsinfo);
	if (!status)
		status = -nfs_stat_to_errno(hdr.status);
	return status;
}

/*
 * PATHCONF request
 */
static int nfs4_xdr_dec_pathconf(struct rpc_rqst *req, uint32_t *p, struct nfs_pathconf *pathconf)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_putfh(&xdr);
	if (!status)
		status = decode_pathconf(&xdr, pathconf);
	return status;
}

/*
 * STATFS request
 */
static int nfs4_xdr_dec_statfs(struct rpc_rqst *req, uint32_t *p, struct nfs_fsstat *fsstat)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_putfh(&xdr);
	if (!status)
		status = decode_statfs(&xdr, fsstat);
	return status;
}

/*
 * GETATTR_BITMAP request
 */
static int nfs4_xdr_dec_server_caps(struct rpc_rqst *req, uint32_t *p, struct nfs4_server_caps_res *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	if ((status = decode_compound_hdr(&xdr, &hdr)) != 0)
		goto out;
	if ((status = decode_putfh(&xdr)) != 0)
		goto out;
	status = decode_server_caps(&xdr, res);
out:
	return status;
}

/*
 * Decode RENEW response
 */
static int nfs4_xdr_dec_renew(struct rpc_rqst *rqstp, uint32_t *p, void *dummy)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_renew(&xdr);
	return status;
}

/*
 * a SETCLIENTID request
 */
static int nfs4_xdr_dec_setclientid(struct rpc_rqst *req, uint32_t *p,
		struct nfs4_client *clp)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_setclientid(&xdr, clp);
	if (!status)
		status = -nfs_stat_to_errno(hdr.status);
	return status;
}

/*
 * a SETCLIENTID_CONFIRM request
 */
static int nfs4_xdr_dec_setclientid_confirm(struct rpc_rqst *req, uint32_t *p, struct nfs_fsinfo *fsinfo)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (!status)
		status = decode_setclientid_confirm(&xdr);
	if (!status)
		status = decode_putrootfh(&xdr);
	if (!status)
		status = decode_fsinfo(&xdr, fsinfo);
	if (!status)
		status = -nfs_stat_to_errno(hdr.status);
	return status;
}

/*
 * DELEGRETURN request
 */
static int nfs4_xdr_dec_delegreturn(struct rpc_rqst *rqstp, uint32_t *p, struct nfs4_delegreturnres *res)
{
	struct xdr_stream xdr;
	struct compound_hdr hdr;
	int status;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	status = decode_compound_hdr(&xdr, &hdr);
	if (status != 0)
		goto out;
	status = decode_putfh(&xdr);
	if (status != 0)
		goto out;
	status = decode_delegreturn(&xdr);
	decode_getfattr(&xdr, res->fattr, res->server);
out:
	return status;
}

uint32_t *nfs4_decode_dirent(uint32_t *p, struct nfs_entry *entry, int plus)
{
	uint32_t bitmap[2] = {0};
	uint32_t len;

	if (!*p++) {
		if (!*p)
			return ERR_PTR(-EAGAIN);
		entry->eof = 1;
		return ERR_PTR(-EBADCOOKIE);
	}

	entry->prev_cookie = entry->cookie;
	p = xdr_decode_hyper(p, &entry->cookie);
	entry->len = ntohl(*p++);
	entry->name = (const char *) p;
	p += XDR_QUADLEN(entry->len);

	/*
	 * In case the server doesn't return an inode number,
	 * we fake one here.  (We don't use inode number 0,
	 * since glibc seems to choke on it...)
	 */
	entry->ino = 1;

	len = ntohl(*p++);		/* bitmap length */
	if (len-- > 0) {
		bitmap[0] = ntohl(*p++);
		if (len-- > 0) {
			bitmap[1] = ntohl(*p++);
			p += len;
		}
	}
	len = XDR_QUADLEN(ntohl(*p++));	/* attribute buffer length */
	if (len > 0) {
		if (bitmap[0] & FATTR4_WORD0_RDATTR_ERROR) {
			bitmap[0] &= ~FATTR4_WORD0_RDATTR_ERROR;
			/* Ignore the return value of rdattr_error for now */
			p++;
			len--;
		}
		if (bitmap[0] == 0 && bitmap[1] == FATTR4_WORD1_MOUNTED_ON_FILEID)
			xdr_decode_hyper(p, &entry->ino);
		else if (bitmap[0] == FATTR4_WORD0_FILEID)
			xdr_decode_hyper(p, &entry->ino);
		p += len;
	}

	entry->eof = !p[0] && p[1];
	return p;
}

/*
 * We need to translate between nfs status return values and
 * the local errno values which may not be the same.
 */
static struct {
	int stat;
	int errno;
} nfs_errtbl[] = {
	{ NFS4_OK,		0		},
	{ NFS4ERR_PERM,		EPERM		},
	{ NFS4ERR_NOENT,	ENOENT		},
	{ NFS4ERR_IO,		errno_NFSERR_IO	},
	{ NFS4ERR_NXIO,		ENXIO		},
	{ NFS4ERR_ACCESS,	EACCES		},
	{ NFS4ERR_EXIST,	EEXIST		},
	{ NFS4ERR_XDEV,		EXDEV		},
	{ NFS4ERR_NOTDIR,	ENOTDIR		},
	{ NFS4ERR_ISDIR,	EISDIR		},
	{ NFS4ERR_INVAL,	EINVAL		},
	{ NFS4ERR_FBIG,		EFBIG		},
	{ NFS4ERR_NOSPC,	ENOSPC		},
	{ NFS4ERR_ROFS,		EROFS		},
	{ NFS4ERR_MLINK,	EMLINK		},
	{ NFS4ERR_NAMETOOLONG,	ENAMETOOLONG	},
	{ NFS4ERR_NOTEMPTY,	ENOTEMPTY	},
	{ NFS4ERR_DQUOT,	EDQUOT		},
	{ NFS4ERR_STALE,	ESTALE		},
	{ NFS4ERR_BADHANDLE,	EBADHANDLE	},
	{ NFS4ERR_BADOWNER,	EINVAL		},
	{ NFS4ERR_BADNAME,	EINVAL		},
	{ NFS4ERR_BAD_COOKIE,	EBADCOOKIE	},
	{ NFS4ERR_NOTSUPP,	ENOTSUPP	},
	{ NFS4ERR_TOOSMALL,	ETOOSMALL	},
	{ NFS4ERR_SERVERFAULT,	ESERVERFAULT	},
	{ NFS4ERR_BADTYPE,	EBADTYPE	},
	{ NFS4ERR_LOCKED,	EAGAIN		},
	{ NFS4ERR_RESOURCE,	EREMOTEIO	},
	{ NFS4ERR_SYMLINK,	ELOOP		},
	{ NFS4ERR_OP_ILLEGAL,	EOPNOTSUPP	},
	{ NFS4ERR_DEADLOCK,	EDEADLK		},
	{ NFS4ERR_WRONGSEC,	EPERM		}, /* FIXME: this needs
						    * to be handled by a
						    * middle-layer.
						    */
	{ -1,			EIO		}
};

/*
 * Convert an NFS error code to a local one.
 * This one is used jointly by NFSv2 and NFSv3.
 */
static int
nfs_stat_to_errno(int stat)
{
	int i;
	for (i = 0; nfs_errtbl[i].stat != -1; i++) {
		if (nfs_errtbl[i].stat == stat)
			return nfs_errtbl[i].errno;
	}
	if (stat <= 10000 || stat > 10100) {
		/* The server is looney tunes. */
		return ESERVERFAULT;
	}
	/* If we cannot translate the error, the recovery routines should
	 * handle it.
	 * Note: remaining NFSv4 error codes have values > 10000, so should
	 * not conflict with native Linux error codes.
	 */
	return stat;
}

#ifndef MAX
# define MAX(a, b)	(((a) > (b))? (a) : (b))
#endif

#define PROC(proc, argtype, restype)				\
[NFSPROC4_CLNT_##proc] = {					\
	.p_proc   = NFSPROC4_COMPOUND,				\
	.p_encode = (kxdrproc_t) nfs4_xdr_##argtype,		\
	.p_decode = (kxdrproc_t) nfs4_xdr_##restype,		\
	.p_bufsiz = MAX(NFS4_##argtype##_sz,NFS4_##restype##_sz) << 2,	\
    }

struct rpc_procinfo	nfs4_procedures[] = {
  PROC(READ,		enc_read,	dec_read),
  PROC(WRITE,		enc_write,	dec_write),
  PROC(COMMIT,		enc_commit,	dec_commit),
  PROC(OPEN,		enc_open,	dec_open),
  PROC(OPEN_CONFIRM,	enc_open_confirm,	dec_open_confirm),
  PROC(OPEN_NOATTR,	enc_open_noattr,	dec_open_noattr),
  PROC(OPEN_DOWNGRADE,	enc_open_downgrade,	dec_open_downgrade),
  PROC(CLOSE,		enc_close,	dec_close),
  PROC(SETATTR,		enc_setattr,	dec_setattr),
  PROC(FSINFO,		enc_fsinfo,	dec_fsinfo),
  PROC(RENEW,		enc_renew,	dec_renew),
  PROC(SETCLIENTID,	enc_setclientid,	dec_setclientid),
  PROC(SETCLIENTID_CONFIRM,	enc_setclientid_confirm,	dec_setclientid_confirm),
  PROC(LOCK,            enc_lock,       dec_lock),
  PROC(LOCKT,           enc_lockt,      dec_lockt),
  PROC(LOCKU,           enc_locku,      dec_locku),
  PROC(ACCESS,		enc_access,	dec_access),
  PROC(GETATTR,		enc_getattr,	dec_getattr),
  PROC(LOOKUP,		enc_lookup,	dec_lookup),
  PROC(LOOKUP_ROOT,	enc_lookup_root,	dec_lookup_root),
  PROC(REMOVE,		enc_remove,	dec_remove),
  PROC(RENAME,		enc_rename,	dec_rename),
  PROC(LINK,		enc_link,	dec_link),
  PROC(SYMLINK,		enc_symlink,	dec_symlink),
  PROC(CREATE,		enc_create,	dec_create),
  PROC(PATHCONF,	enc_pathconf,	dec_pathconf),
  PROC(STATFS,		enc_statfs,	dec_statfs),
  PROC(READLINK,	enc_readlink,	dec_readlink),
  PROC(READDIR,		enc_readdir,	dec_readdir),
  PROC(SERVER_CAPS,	enc_server_caps, dec_server_caps),
  PROC(DELEGRETURN,	enc_delegreturn, dec_delegreturn),
  PROC(GETACL,		enc_getacl,	dec_getacl),
  PROC(SETACL,		enc_setacl,	dec_setacl),
};

struct rpc_version		nfs_version4 = {
	.number			= 4,
	.nrprocs		= sizeof(nfs4_procedures)/sizeof(nfs4_procedures[0]),
	.procs			= nfs4_procedures
};

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
