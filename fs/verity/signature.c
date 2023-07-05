// SPDX-License-Identifier: GPL-2.0
/*
 * Verification of builtin signatures
 *
 * Copyright 2019 Google LLC
 */

/*
 * This file implements verification of fs-verity builtin signatures.  Please
 * take great care before using this feature.  It is not the only way to do
 * signatures with fs-verity, and the alternatives (such as userspace signature
 * verification, and IMA appraisal) can be much better.  For details about the
 * limitations of this feature, see Documentation/filesystems/fsverity.rst.
 */

#include "fsverity_private.h"

#include <linux/cred.h>
#include <linux/key.h>
#include <linux/slab.h>
#include <linux/verification.h>

/*
 * /proc/sys/fs/verity/require_signatures
 * If 1, all verity files must have a valid builtin signature.
 */
static int fsverity_require_signatures;

/*
 * Keyring that contains the trusted X.509 certificates.
 *
 * Only root (kuid=0) can modify this.  Also, root may use
 * keyctl_restrict_keyring() to prevent any more additions.
 */
static struct key *fsverity_keyring;

/**
 * fsverity_verify_signature() - check a verity file's signature
 * @vi: the file's fsverity_info
 * @signature: the file's built-in signature
 * @sig_size: size of signature in bytes, or 0 if no signature
 *
 * If the file includes a signature of its fs-verity file digest, verify it
 * against the certificates in the fs-verity keyring.
 *
 * Return: 0 on success (signature valid or not required); -errno on failure
 */
int fsverity_verify_signature(const struct fsverity_info *vi,
			      const u8 *signature, size_t sig_size)
{
	const struct inode *inode = vi->inode;
	const struct fsverity_hash_alg *hash_alg = vi->tree_params.hash_alg;
	struct fsverity_formatted_digest *d;
	int err;

	if (sig_size == 0) {
		if (fsverity_require_signatures) {
			fsverity_err(inode,
				     "require_signatures=1, rejecting unsigned file!");
			return -EPERM;
		}
		return 0;
	}

	d = kzalloc(sizeof(*d) + hash_alg->digest_size, GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	memcpy(d->magic, "FSVerity", 8);
	d->digest_algorithm = cpu_to_le16(hash_alg - fsverity_hash_algs);
	d->digest_size = cpu_to_le16(hash_alg->digest_size);
	memcpy(d->digest, vi->file_digest, hash_alg->digest_size);

	err = verify_pkcs7_signature(d, sizeof(*d) + hash_alg->digest_size,
				     signature, sig_size, fsverity_keyring,
				     VERIFYING_UNSPECIFIED_SIGNATURE,
				     NULL, NULL);
	kfree(d);

	if (err) {
		if (err == -ENOKEY)
			fsverity_err(inode,
				     "File's signing cert isn't in the fs-verity keyring");
		else if (err == -EKEYREJECTED)
			fsverity_err(inode, "Incorrect file signature");
		else if (err == -EBADMSG)
			fsverity_err(inode, "Malformed file signature");
		else
			fsverity_err(inode, "Error %d verifying file signature",
				     err);
		return err;
	}

	return 0;
}

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *fsverity_sysctl_header;

static struct ctl_table fsverity_sysctl_table[] = {
	{
		.procname       = "require_signatures",
		.data           = &fsverity_require_signatures,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = SYSCTL_ZERO,
		.extra2         = SYSCTL_ONE,
	},
	{ }
};

static void __init fsverity_sysctl_init(void)
{
	fsverity_sysctl_header = register_sysctl("fs/verity",
						 fsverity_sysctl_table);
	if (!fsverity_sysctl_header)
		panic("fsverity sysctl registration failed");
}
#else /* !CONFIG_SYSCTL */
static inline void fsverity_sysctl_init(void)
{
}
#endif /* !CONFIG_SYSCTL */

void __init fsverity_init_signature(void)
{
	fsverity_keyring =
		keyring_alloc(".fs-verity", KUIDT_INIT(0), KGIDT_INIT(0),
			      current_cred(), KEY_POS_SEARCH |
				KEY_USR_VIEW | KEY_USR_READ | KEY_USR_WRITE |
				KEY_USR_SEARCH | KEY_USR_SETATTR,
			      KEY_ALLOC_NOT_IN_QUOTA, NULL, NULL);
	if (IS_ERR(fsverity_keyring))
		panic("failed to allocate \".fs-verity\" keyring");

	fsverity_sysctl_init();
}
