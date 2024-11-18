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
#include <crypto/sha2.h>
#include <linux/unaligned.h>

#include "protocol.h"

#define SHA256_DIGEST_WORDS (SHA256_DIGEST_SIZE / 4)

void mptcp_crypto_key_sha(u64 key, u32 *token, u64 *idsn)
{
	__be32 mptcp_hashed_key[SHA256_DIGEST_WORDS];
	__be64 input = cpu_to_be64(key);

	sha256((__force u8 *)&input, sizeof(input), (u8 *)mptcp_hashed_key);

	if (token)
		*token = be32_to_cpu(mptcp_hashed_key[0]);
	if (idsn)
		*idsn = be64_to_cpu(*((__be64 *)&mptcp_hashed_key[6]));
}

void mptcp_crypto_hmac_sha(u64 key1, u64 key2, u8 *msg, int len, void *hmac)
{
	u8 input[SHA256_BLOCK_SIZE + SHA256_DIGEST_SIZE];
	u8 key1be[8];
	u8 key2be[8];
	int i;

	if (WARN_ON_ONCE(len > SHA256_DIGEST_SIZE))
		len = SHA256_DIGEST_SIZE;

	put_unaligned_be64(key1, key1be);
	put_unaligned_be64(key2, key2be);

	/* Generate key xored with ipad */
	memset(input, 0x36, SHA256_BLOCK_SIZE);
	for (i = 0; i < 8; i++)
		input[i] ^= key1be[i];
	for (i = 0; i < 8; i++)
		input[i + 8] ^= key2be[i];

	memcpy(&input[SHA256_BLOCK_SIZE], msg, len);

	/* emit sha256(K1 || msg) on the second input block, so we can
	 * reuse 'input' for the last hashing
	 */
	sha256(input, SHA256_BLOCK_SIZE + len, &input[SHA256_BLOCK_SIZE]);

	/* Prepare second part of hmac */
	memset(input, 0x5C, SHA256_BLOCK_SIZE);
	for (i = 0; i < 8; i++)
		input[i] ^= key1be[i];
	for (i = 0; i < 8; i++)
		input[i + 8] ^= key2be[i];

	sha256(input, SHA256_BLOCK_SIZE + SHA256_DIGEST_SIZE, hmac);
}

#if IS_MODULE(CONFIG_MPTCP_KUNIT_TEST)
EXPORT_SYMBOL_GPL(mptcp_crypto_hmac_sha);
#endif
