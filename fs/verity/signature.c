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
 * @desc: the file's fsverity_descriptor
 * @desc_size: size of @desc
 *
 * If the file's fs-verity descriptor includes a signature of the file digest,
 * verify it against the certificates in the fs-verity keyring.
 *
 * Return: 0 on success (signature valid or not required); -errno on failure
 */
int fsverity_verify_signature(const struct fsverity_info *vi,
			      const struct fsverity_descriptor *desc,
			      size_t desc_size)
{
	const struct inode *inode = vi->inode;
	const struct fsverity_hash_alg *hash_alg = vi->tree_params.hash_alg;
	const u32 sig_size = le32_to_cpu(desc->sig_size);
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

	if (sig_size > desc_size - sizeof(*desc)) {
		fsverity_err(inode, "Signature overflows verity descriptor");
		return -EBADMSG;
	}

	d = kzalloc(sizeof(*d) + hash_alg->digest_size, GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	memcpy(d->magic, "FSVerity", 8);
	d->digest_algorithm = cpu_to_le16(hash_alg - fsverity_hash_algs);
	d->digest_size = cpu_to_le16(hash_alg->digest_size);
	memcpy(d->digest, vi->file_digest, hash_alg->digest_size);

	err = verify_pkcs7_signature(d, sizeof(*d) + hash_alg->digest_size,
				     desc->signature, sig_size,
				     fsverity_keyring,
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
		 hash_alg->name, hash_alg->digest_size, vi->file_digest);
	return 0;
}

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
