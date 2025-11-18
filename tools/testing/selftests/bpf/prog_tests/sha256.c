// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2025 Google LLC */

#include <test_progs.h>
#include "bpf/libbpf_internal.h"

#define MAX_LEN 4096

/* Test libbpf_sha256() for all lengths from 0 to MAX_LEN inclusively. */
void test_sha256(void)
{
	/*
	 * The correctness of this value was verified by running this test with
	 * libbpf_sha256() replaced by OpenSSL's SHA256().
	 */
	static const __u8 expected_digest_of_digests[SHA256_DIGEST_LENGTH] = {
		0x62, 0x30, 0x0e, 0x1d, 0xea, 0x7f, 0xc4, 0x74,
		0xfd, 0x8e, 0x64, 0x0b, 0xd8, 0x5f, 0xea, 0x04,
		0xf3, 0xef, 0x77, 0x42, 0xc2, 0x01, 0xb8, 0x90,
		0x6e, 0x19, 0x91, 0x1b, 0xca, 0xb3, 0x28, 0x42,
	};
	__u64 seed = 0;
	__u8 *data = NULL, *digests = NULL;
	__u8 digest_of_digests[SHA256_DIGEST_LENGTH];
	size_t i;

	data = malloc(MAX_LEN);
	if (!ASSERT_OK_PTR(data, "malloc"))
		goto out;
	digests = malloc((MAX_LEN + 1) * SHA256_DIGEST_LENGTH);
	if (!ASSERT_OK_PTR(digests, "malloc"))
		goto out;

	/* Generate MAX_LEN bytes of "random" data deterministically. */
	for (i = 0; i < MAX_LEN; i++) {
		seed = (seed * 25214903917 + 11) & ((1ULL << 48) - 1);
		data[i] = (__u8)(seed >> 16);
	}

	/* Calculate a digest for each length 0 through MAX_LEN inclusively. */
	for (i = 0; i <= MAX_LEN; i++)
		libbpf_sha256(data, i, &digests[i * SHA256_DIGEST_LENGTH]);

	/* Calculate and verify the digest of all the digests. */
	libbpf_sha256(digests, (MAX_LEN + 1) * SHA256_DIGEST_LENGTH,
		      digest_of_digests);
	ASSERT_MEMEQ(digest_of_digests, expected_digest_of_digests,
		     SHA256_DIGEST_LENGTH, "digest_of_digests");
out:
	free(data);
	free(digests);
}
