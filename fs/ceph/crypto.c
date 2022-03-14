// SPDX-License-Identifier: GPL-2.0
/*
 * The base64 encode/decode code was copied from fscrypt:
 * Copyright (C) 2015, Google, Inc.
 * Copyright (C) 2015, Motorola Mobility
 * Written by Uday Savagaonkar, 2014.
 * Modified by Jaegeuk Kim, 2015.
 */
#include <linux/ceph/ceph_debug.h>
#include <linux/xattr.h>
#include <linux/fscrypt.h>

#include "super.h"
#include "mds_client.h"
#include "crypto.h"

/*
 * The base64url encoding used by fscrypt includes the '_' character, which may
 * cause problems in snapshot names (which can not start with '_').  Thus, we
 * used the base64 encoding defined for IMAP mailbox names (RFC 3501) instead,
 * which replaces '-' and '_' by '+' and ','.
 */
static const char base64_table[65] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";

int ceph_base64_encode(const u8 *src, int srclen, char *dst)
{
	u32 ac = 0;
	int bits = 0;
	int i;
	char *cp = dst;

	for (i = 0; i < srclen; i++) {
		ac = (ac << 8) | src[i];
		bits += 8;
		do {
			bits -= 6;
			*cp++ = base64_table[(ac >> bits) & 0x3f];
		} while (bits >= 6);
	}
	if (bits)
		*cp++ = base64_table[(ac << (6 - bits)) & 0x3f];
	return cp - dst;
}

int ceph_base64_decode(const char *src, int srclen, u8 *dst)
{
	u32 ac = 0;
	int bits = 0;
	int i;
	u8 *bp = dst;

	for (i = 0; i < srclen; i++) {
		const char *p = strchr(base64_table, src[i]);

		if (p == NULL || src[i] == 0)
			return -1;
		ac = (ac << 6) | (p - base64_table);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			*bp++ = (u8)(ac >> bits);
		}
	}
	if (ac & ((1 << bits) - 1))
		return -1;
	return bp - dst;
}

static int ceph_crypt_get_context(struct inode *inode, void *ctx, size_t len)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fscrypt_auth *cfa = (struct ceph_fscrypt_auth *)ci->fscrypt_auth;
	u32 ctxlen;

	/* Non existent or too short? */
	if (!cfa || (ci->fscrypt_auth_len < (offsetof(struct ceph_fscrypt_auth, cfa_blob) + 1)))
		return -ENOBUFS;

	/* Some format we don't recognize? */
	if (le32_to_cpu(cfa->cfa_version) != CEPH_FSCRYPT_AUTH_VERSION)
		return -ENOBUFS;

	ctxlen = le32_to_cpu(cfa->cfa_blob_len);
	if (len < ctxlen)
		return -ERANGE;

	memcpy(ctx, cfa->cfa_blob, ctxlen);
	return ctxlen;
}

static int ceph_crypt_set_context(struct inode *inode, const void *ctx,
				  size_t len, void *fs_data)
{
	int ret;
	struct iattr attr = { };
	struct ceph_iattr cia = { };
	struct ceph_fscrypt_auth *cfa;

	WARN_ON_ONCE(fs_data);

	if (len > FSCRYPT_SET_CONTEXT_MAX_SIZE)
		return -EINVAL;

	cfa = kzalloc(sizeof(*cfa), GFP_KERNEL);
	if (!cfa)
		return -ENOMEM;

	cfa->cfa_version = cpu_to_le32(CEPH_FSCRYPT_AUTH_VERSION);
	cfa->cfa_blob_len = cpu_to_le32(len);
	memcpy(cfa->cfa_blob, ctx, len);

	cia.fscrypt_auth = cfa;

	ret = __ceph_setattr(inode, &attr, &cia);
	if (ret == 0)
		inode_set_flags(inode, S_ENCRYPTED, S_ENCRYPTED);
	kfree(cia.fscrypt_auth);
	return ret;
}

static bool ceph_crypt_empty_dir(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	return ci->i_rsubdirs + ci->i_rfiles == 1;
}

static const union fscrypt_policy *ceph_get_dummy_policy(struct super_block *sb)
{
	return ceph_sb_to_client(sb)->fsc_dummy_enc_policy.policy;
}

static struct fscrypt_operations ceph_fscrypt_ops = {
	.get_context		= ceph_crypt_get_context,
	.set_context		= ceph_crypt_set_context,
	.get_dummy_policy	= ceph_get_dummy_policy,
	.empty_dir		= ceph_crypt_empty_dir,
};

void ceph_fscrypt_set_ops(struct super_block *sb)
{
	fscrypt_set_ops(sb, &ceph_fscrypt_ops);
}

void ceph_fscrypt_free_dummy_policy(struct ceph_fs_client *fsc)
{
	fscrypt_free_dummy_policy(&fsc->fsc_dummy_enc_policy);
}

int ceph_fscrypt_prepare_context(struct inode *dir, struct inode *inode,
				 struct ceph_acl_sec_ctx *as)
{
	int ret, ctxsize;
	bool encrypted = false;
	struct ceph_inode_info *ci = ceph_inode(inode);

	ret = fscrypt_prepare_new_inode(dir, inode, &encrypted);
	if (ret)
		return ret;
	if (!encrypted)
		return 0;

	as->fscrypt_auth = kzalloc(sizeof(*as->fscrypt_auth), GFP_KERNEL);
	if (!as->fscrypt_auth)
		return -ENOMEM;

	ctxsize = fscrypt_context_for_new_inode(as->fscrypt_auth->cfa_blob,
						inode);
	if (ctxsize < 0)
		return ctxsize;

	as->fscrypt_auth->cfa_version = cpu_to_le32(CEPH_FSCRYPT_AUTH_VERSION);
	as->fscrypt_auth->cfa_blob_len = cpu_to_le32(ctxsize);

	WARN_ON_ONCE(ci->fscrypt_auth);
	kfree(ci->fscrypt_auth);
	ci->fscrypt_auth_len = ceph_fscrypt_auth_len(as->fscrypt_auth);
	ci->fscrypt_auth = kmemdup(as->fscrypt_auth, ci->fscrypt_auth_len,
				   GFP_KERNEL);
	if (!ci->fscrypt_auth)
		return -ENOMEM;

	inode->i_flags |= S_ENCRYPTED;

	return 0;
}

void ceph_fscrypt_as_ctx_to_req(struct ceph_mds_request *req,
				struct ceph_acl_sec_ctx *as)
{
	swap(req->r_fscrypt_auth, as->fscrypt_auth);
}

int ceph_encode_encrypted_dname(const struct inode *parent,
				struct qstr *d_name, char *buf)
{
	u32 len;
	int elen;
	int ret;
	u8 *cryptbuf;

	if (!fscrypt_has_encryption_key(parent)) {
		memcpy(buf, d_name->name, d_name->len);
		return d_name->len;
	}

	/*
	 * Convert cleartext d_name to ciphertext. If result is longer than
	 * CEPH_NOHASH_NAME_MAX, sha256 the remaining bytes
	 *
	 * See: fscrypt_setup_filename
	 */
	if (!fscrypt_fname_encrypted_size(parent, d_name->len, NAME_MAX, &len))
		return -ENAMETOOLONG;

	/* Allocate a buffer appropriate to hold the result */
	cryptbuf = kmalloc(len > CEPH_NOHASH_NAME_MAX ? NAME_MAX : len,
			   GFP_KERNEL);
	if (!cryptbuf)
		return -ENOMEM;

	ret = fscrypt_fname_encrypt(parent, d_name, cryptbuf, len);
	if (ret) {
		kfree(cryptbuf);
		return ret;
	}

	/* hash the end if the name is long enough */
	if (len > CEPH_NOHASH_NAME_MAX) {
		u8 hash[SHA256_DIGEST_SIZE];
		u8 *extra = cryptbuf + CEPH_NOHASH_NAME_MAX;

		/*
		 * hash the extra bytes and overwrite crypttext beyond that
		 * point with it
		 */
		sha256(extra, len - CEPH_NOHASH_NAME_MAX, hash);
		memcpy(extra, hash, SHA256_DIGEST_SIZE);
		len = CEPH_NOHASH_NAME_MAX + SHA256_DIGEST_SIZE;
	}

	/* base64 encode the encrypted name */
	elen = ceph_base64_encode(cryptbuf, len, buf);
	kfree(cryptbuf);
	dout("base64-encoded ciphertext name = %.*s\n", elen, buf);
	return elen;
}

int ceph_encode_encrypted_fname(const struct inode *parent,
				struct dentry *dentry, char *buf)
{
	WARN_ON_ONCE(!fscrypt_has_encryption_key(parent));

	return ceph_encode_encrypted_dname(parent, &dentry->d_name, buf);
}

/**
 * ceph_fname_to_usr - convert a filename for userland presentation
 * @fname: ceph_fname to be converted
 * @tname: temporary name buffer to use for conversion (may be NULL)
 * @oname: where converted name should be placed
 * @is_nokey: set to true if key wasn't available during conversion (may be NULL)
 *
 * Given a filename (usually from the MDS), format it for presentation to
 * userland. If @parent is not encrypted, just pass it back as-is.
 *
 * Otherwise, base64 decode the string, and then ask fscrypt to format it
 * for userland presentation.
 *
 * Returns 0 on success or negative error code on error.
 */
int ceph_fname_to_usr(const struct ceph_fname *fname, struct fscrypt_str *tname,
		      struct fscrypt_str *oname, bool *is_nokey)
{
	int ret;
	struct fscrypt_str _tname = FSTR_INIT(NULL, 0);
	struct fscrypt_str iname;

	if (!IS_ENCRYPTED(fname->dir)) {
		oname->name = fname->name;
		oname->len = fname->name_len;
		return 0;
	}

	/* Sanity check that the resulting name will fit in the buffer */
	if (fname->name_len > NAME_MAX || fname->ctext_len > NAME_MAX)
		return -EIO;

	ret = __fscrypt_prepare_readdir(fname->dir);
	if (ret)
		return ret;

	/*
	 * Use the raw dentry name as sent by the MDS instead of
	 * generating a nokey name via fscrypt.
	 */
	if (!fscrypt_has_encryption_key(fname->dir)) {
		if (fname->no_copy)
			oname->name = fname->name;
		else
			memcpy(oname->name, fname->name, fname->name_len);
		oname->len = fname->name_len;
		if (is_nokey)
			*is_nokey = true;
		return 0;
	}

	if (fname->ctext_len == 0) {
		int declen;

		if (!tname) {
			ret = fscrypt_fname_alloc_buffer(NAME_MAX, &_tname);
			if (ret)
				return ret;
			tname = &_tname;
		}

		declen = ceph_base64_decode(fname->name, fname->name_len,
					    tname->name);
		if (declen <= 0) {
			ret = -EIO;
			goto out;
		}
		iname.name = tname->name;
		iname.len = declen;
	} else {
		iname.name = fname->ctext;
		iname.len = fname->ctext_len;
	}

	ret = fscrypt_fname_disk_to_usr(fname->dir, 0, 0, &iname, oname);
out:
	fscrypt_fname_free_buffer(&_tname);
	return ret;
}
