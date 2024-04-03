// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_kfuncs.h"

char _license[] SEC("license") = "GPL";

#ifndef SHA256_DIGEST_SIZE
#define SHA256_DIGEST_SIZE      32
#endif

#define MAX_SIG_SIZE 1024

/* By default, "fsverity sign" signs a file with fsverity_formatted_digest
 * of the file. fsverity_formatted_digest on the kernel side is only used
 * with CONFIG_FS_VERITY_BUILTIN_SIGNATURES. However, BPF LSM doesn't not
 * require CONFIG_FS_VERITY_BUILTIN_SIGNATURES, so vmlinux.h may not have
 * fsverity_formatted_digest. In this test, we intentionally avoid using
 * fsverity_formatted_digest.
 *
 * Luckily, fsverity_formatted_digest is simply 8-byte magic followed by
 * fsverity_digest. We use a char array of size fsverity_formatted_digest
 * plus SHA256_DIGEST_SIZE. The magic part of it is filled by user space,
 * and the rest of it is filled by bpf_get_fsverity_digest.
 *
 * Note that, generating signatures based on fsverity_formatted_digest is
 * the design choice of this selftest (and "fsverity sign"). With BPF
 * LSM, we have the flexibility to generate signature based on other data
 * sets, for example, fsverity_digest or only the digest[] part of it.
 */
#define MAGIC_SIZE 8
#define SIZEOF_STRUCT_FSVERITY_DIGEST 4  /* sizeof(struct fsverity_digest) */
char digest[MAGIC_SIZE + SIZEOF_STRUCT_FSVERITY_DIGEST + SHA256_DIGEST_SIZE];

__u32 monitored_pid;
char sig[MAX_SIG_SIZE];
__u32 sig_size;
__u32 user_keyring_serial;

SEC("lsm.s/file_open")
int BPF_PROG(test_file_open, struct file *f)
{
	struct bpf_dynptr digest_ptr, sig_ptr;
	struct bpf_key *trusted_keyring;
	__u32 pid;
	int ret;

	pid = bpf_get_current_pid_tgid() >> 32;
	if (pid != monitored_pid)
		return 0;

	/* digest_ptr points to fsverity_digest */
	bpf_dynptr_from_mem(digest + MAGIC_SIZE, sizeof(digest) - MAGIC_SIZE, 0, &digest_ptr);

	ret = bpf_get_fsverity_digest(f, &digest_ptr);
	/* No verity, allow access */
	if (ret < 0)
		return 0;

	/* Move digest_ptr to fsverity_formatted_digest */
	bpf_dynptr_from_mem(digest, sizeof(digest), 0, &digest_ptr);

	/* Read signature from xattr */
	bpf_dynptr_from_mem(sig, sizeof(sig), 0, &sig_ptr);
	ret = bpf_get_file_xattr(f, "user.sig", &sig_ptr);
	/* No signature, reject access */
	if (ret < 0)
		return -EPERM;

	trusted_keyring = bpf_lookup_user_key(user_keyring_serial, 0);
	if (!trusted_keyring)
		return -ENOENT;

	/* Verify signature */
	ret = bpf_verify_pkcs7_signature(&digest_ptr, &sig_ptr, trusted_keyring);

	bpf_key_put(trusted_keyring);
	return ret;
}
