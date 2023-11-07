// SPDX-License-Identifier: GPL-2.0
/*
 * Verification of builtin signatures
 *
 * Copyright 2019 Google LLC
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
	unsigned int digest_algorithm =
		vi->tree_params.hash_alg - fsverity_hash_algs;

	return __fsverity_verify_signature(vi->inode, signature, sig_size,
					   vi->file_digest, digest_algorithm);
}

/**
 * __fsverity_verify_signature() - check a verity file's signature
 * @inode: the file's inode
 * @signature: the file's signature
 * @sig_size: size of @signature. Can be 0 if there is no signature
 * @file_digest: the file's digest
 * @digest_algorithm: the digest algorithm used
 *
 * Takes the file's digest and optional signature and verifies the signature
 * against the digest and the fs-verity keyring if appropriate
 *
 * Return: 0 on success (signature valid or not required); -errno on failure
 */
int __fsverity_verify_signature(const struct inode *inode, const u8 *signature,
				size_t sig_size, const u8 *file_digest,
				unsigned int digest_algorithm)
{
	struct fsverity_formatted_digest *d;
	struct fsverity_hash_alg *hash_alg = fsverity_get_hash_alg(inode,
							digest_algorithm);
	int err;

	if (IS_ERR(hash_alg))
		return PTR_ERR(hash_alg);

	if (sig_size == 0) {
		if (fsverity_require_signatures) {
			fsverity_err(inode,
				     "require_signatures=1, rejecting unsigned file!");
			return -EPERM;
		}
		return 0;
	}

	if (fsverity_keyring->keys.nr_leaves_on_tree == 0) {
		/*
		 * The ".fs-verity" keyring is empty, due to builtin signatures
		 * being supported by the kernel but not actually being used.
		 * In this case, verify_pkcs7_signature() would always return an
		 * error, usually ENOKEY.  It could also be EBADMSG if the
		 * PKCS#7 is malformed, but that isn't very important to
		 * distinguish.  So, just skip to ENOKEY to avoid the attack
		 * surface of the PKCS#7 parser, which would otherwise be
		 * reachable by any task able to execute FS_IOC_ENABLE_VERITY.
		 */
		fsverity_err(inode,
			     "fs-verity keyring is empty, rejecting signed file!");
		return -ENOKEY;
	}

	d = kzalloc(sizeof(*d) + hash_alg->digest_size, GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	memcpy(d->magic, "FSVerity", 8);
	d->digest_algorithm = cpu_to_le16(hash_alg - fsverity_hash_algs);
	d->digest_size = cpu_to_le16(hash_alg->digest_size);
	memcpy(d->digest, file_digest, hash_alg->digest_size);

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

	pr_debug("Valid signature for file digest %s:%*phN\n",
		 hash_alg->name, hash_alg->digest_size, file_digest);
	return 0;
}
EXPORT_SYMBOL_GPL(__fsverity_verify_signature);

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *fsverity_sysctl_header;

static const struct ctl_path fsverity_sysctl_path[] = {
	{ .procname = "fs", },
	{ .procname = "verity", },
	{ }
};

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

static int __init fsverity_sysctl_init(void)
{
	fsverity_sysctl_header = register_sysctl_paths(fsverity_sysctl_path,
						       fsverity_sysctl_table);
	if (!fsverity_sysctl_header) {
		pr_err("sysctl registration failed!\n");
		return -ENOMEM;
	}
	return 0;
}
#else /* !CONFIG_SYSCTL */
static inline int __init fsverity_sysctl_init(void)
{
	return 0;
}
#endif /* !CONFIG_SYSCTL */

int __init fsverity_init_signature(void)
{
	struct key *ring;
	int err;

	ring = keyring_alloc(".fs-verity", KUIDT_INIT(0), KGIDT_INIT(0),
			     current_cred(), KEY_POS_SEARCH |
				KEY_USR_VIEW | KEY_USR_READ | KEY_USR_WRITE |
				KEY_USR_SEARCH | KEY_USR_SETATTR,
			     KEY_ALLOC_NOT_IN_QUOTA, NULL, NULL);
	if (IS_ERR(ring))
		return PTR_ERR(ring);

	err = fsverity_sysctl_init();
	if (err)
		goto err_put_ring;

	fsverity_keyring = ring;
	return 0;

err_put_ring:
	key_put(ring);
	return err;
}
