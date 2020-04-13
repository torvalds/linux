// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP cryptographic functions
 * Copyright (c) 2017 - 2019, Intel Corporation.
 *
 * Note: This code is based on mptcp_ctrl.c, mptcp_ipv4.c, and
 *       mptcp_ipv6 from multipath-tcp.org, authored by:
 *
 *       Sébastien Barré <sebastien.barre@uclouvain.be>
 *       Christoph Paasch <christoph.paasch@uclouvain.be>
 *       Jaakko Korkeaniemi <jaakko.korkeaniemi@aalto.fi>
 *       Gregory Detal <gregory.detal@uclouvain.be>
 *       Fabien Duchêne <fabien.duchene@uclouvain.be>
 *       Andreas Seelinger <Andreas.Seelinger@rwth-aachen.de>
 *       Lavkesh Lahngir <lavkesh51@gmail.com>
 *       Andreas Ripke <ripke@neclab.eu>
 *       Vlad Dogaru <vlad.dogaru@intel.com>
 *       Octavian Purdila <octavian.purdila@intel.com>
 *       John Ronan <jronan@tssg.org>
 *       Catalin Nicutar <catalin.nicutar@gmail.com>
 *       Brandon Heller <brandonh@stanford.edu>
 */

#include <linux/kernel.h>
#include <crypto/sha.h>
#include <asm/unaligned.h>

#include "protocol.h"

#define SHA256_DIGEST_WORDS (SHA256_DIGEST_SIZE / 4)

void mptcp_crypto_key_sha(u64 key, u32 *token, u64 *idsn)
{
	__be32 mptcp_hashed_key[SHA256_DIGEST_WORDS];
	__be64 input = cpu_to_be64(key);
	struct sha256_state state;

	sha256_init(&state);
	sha256_update(&state, (__force u8 *)&input, sizeof(input));
	sha256_final(&state, (u8 *)mptcp_hashed_key);

	if (token)
		*token = be32_to_cpu(mptcp_hashed_key[0]);
	if (idsn)
		*idsn = be64_to_cpu(*((__be64 *)&mptcp_hashed_key[6]));
}

void mptcp_crypto_hmac_sha(u64 key1, u64 key2, u8 *msg, int len, void *hmac)
{
	u8 input[SHA256_BLOCK_SIZE + SHA256_DIGEST_SIZE];
	__be32 mptcp_hashed_key[SHA256_DIGEST_WORDS];
	__be32 *hash_out = (__force __be32 *)hmac;
	struct sha256_state state;
	u8 key1be[8];
	u8 key2be[8];
	int i;

	if (WARN_ON_ONCE(len > SHA256_DIGEST_SIZE))
		len = SHA256_DIGEST_SIZE;

	put_unaligned_be64(key1, key1be);
	put_unaligned_be64(key2, key2be);

	/* Generate key xored with ipad */
	memset(input, 0x36, SHA_MESSAGE_BYTES);
	for (i = 0; i < 8; i++)
		input[i] ^= key1be[i];
	for (i = 0; i < 8; i++)
		input[i + 8] ^= key2be[i];

	memcpy(&input[SHA256_BLOCK_SIZE], msg, len);

	sha256_init(&state);
	sha256_update(&state, input, SHA256_BLOCK_SIZE + len);

	/* emit sha256(K1 || msg) on the second input block, so we can
	 * reuse 'input' for the last hashing
	 */
	sha256_final(&state, &input[SHA256_BLOCK_SIZE]);

	/* Prepare second part of hmac */
	memset(input, 0x5C, SHA_MESSAGE_BYTES);
	for (i = 0; i < 8; i++)
		input[i] ^= key1be[i];
	for (i = 0; i < 8; i++)
		input[i + 8] ^= key2be[i];

	sha256_init(&state);
	sha256_update(&state, input, SHA256_BLOCK_SIZE + SHA256_DIGEST_SIZE);
	sha256_final(&state, (u8 *)mptcp_hashed_key);

	/* takes only first 160 bits */
	for (i = 0; i < 5; i++)
		hash_out[i] = mptcp_hashed_key[i];
}

#ifdef CONFIG_MPTCP_HMAC_TEST
struct test_cast {
	char *key;
	char *msg;
	char *result;
};

/* we can't reuse RFC 4231 test vectors, as we have constraint on the
 * input and key size, and we truncate the output.
 */
static struct test_cast tests[] = {
	{
		.key = "0b0b0b0b0b0b0b0b",
		.msg = "48692054",
		.result = "8385e24fb4235ac37556b6b886db106284a1da67",
	},
	{
		.key = "aaaaaaaaaaaaaaaa",
		.msg = "dddddddd",
		.result = "2c5e219164ff1dca1c4a92318d847bb6b9d44492",
	},
	{
		.key = "0102030405060708",
		.msg = "cdcdcdcd",
		.result = "e73b9ba9969969cefb04aa0d6df18ec2fcc075b6",
	},
};

static int __init test_mptcp_crypto(void)
{
	char hmac[20], hmac_hex[41];
	u32 nonce1, nonce2;
	u64 key1, key2;
	u8 msg[8];
	int i, j;

	for (i = 0; i < ARRAY_SIZE(tests); ++i) {
		/* mptcp hmap will convert to be before computing the hmac */
		key1 = be64_to_cpu(*((__be64 *)&tests[i].key[0]));
		key2 = be64_to_cpu(*((__be64 *)&tests[i].key[8]));
		nonce1 = be32_to_cpu(*((__be32 *)&tests[i].msg[0]));
		nonce2 = be32_to_cpu(*((__be32 *)&tests[i].msg[4]));

		put_unaligned_be32(nonce1, &msg[0]);
		put_unaligned_be32(nonce2, &msg[4]);

		mptcp_crypto_hmac_sha(key1, key2, msg, 8, hmac);
		for (j = 0; j < 20; ++j)
			sprintf(&hmac_hex[j << 1], "%02x", hmac[j] & 0xff);
		hmac_hex[40] = 0;

		if (memcmp(hmac_hex, tests[i].result, 40))
			pr_err("test %d failed, got %s expected %s", i,
			       hmac_hex, tests[i].result);
		else
			pr_info("test %d [ ok ]", i);
	}
	return 0;
}

late_initcall(test_mptcp_crypto);
#endif
