// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>
#include <linux/xattr.h>
#include <linux/fscrypt.h>

#include "super.h"
#include "crypto.h"

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

static struct fscrypt_operations ceph_fscrypt_ops = {
	.get_context		= ceph_crypt_get_context,
	.set_context		= ceph_crypt_set_context,
	.empty_dir		= ceph_crypt_empty_dir,
};

void ceph_fscrypt_set_ops(struct super_block *sb)
{
	fscrypt_set_ops(sb, &ceph_fscrypt_ops);
}
