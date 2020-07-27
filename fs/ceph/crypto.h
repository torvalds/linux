/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ceph fscrypt functionality
 */

#ifndef _CEPH_CRYPTO_H
#define _CEPH_CRYPTO_H

#include <linux/fscrypt.h>

struct ceph_fscrypt_auth {
	__le32	cfa_version;
	__le32	cfa_blob_len;
	u8	cfa_blob[FSCRYPT_SET_CONTEXT_MAX_SIZE];
} __packed;

#define CEPH_FSCRYPT_AUTH_VERSION	1
static inline u32 ceph_fscrypt_auth_len(struct ceph_fscrypt_auth *fa)
{
	u32 ctxsize = le32_to_cpu(fa->cfa_blob_len);

	return offsetof(struct ceph_fscrypt_auth, cfa_blob) + ctxsize;
}

#ifdef CONFIG_FS_ENCRYPTION
void ceph_fscrypt_set_ops(struct super_block *sb);

#else /* CONFIG_FS_ENCRYPTION */

static inline void ceph_fscrypt_set_ops(struct super_block *sb)
{
}

#endif /* CONFIG_FS_ENCRYPTION */

#endif
