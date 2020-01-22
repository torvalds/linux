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
#include <linux/cryptohash.h>
#include <asm/unaligned.h>

#include "protocol.h"

struct sha1_state {
	u32 workspace[SHA_WORKSPACE_WORDS];
	u32 digest[SHA_DIGEST_WORDS];
	unsigned int count;
};

static void sha1_init(struct sha1_state *state)
{
	sha_init(state->digest);
	state->count = 0;
}

static void sha1_update(struct sha1_state *state, u8 *input)
{
	sha_transform(state->digest, input, state->workspace);
	state->count += SHA_MESSAGE_BYTES;
}

static void sha1_pad_final(struct sha1_state *state, u8 *input,
			   unsigned int length, __be32 *mptcp_hashed_key)
{
	int i;

	input[length] = 0x80;
	memset(&input[length + 1], 0, SHA_MESSAGE_BYTES - length - 9);
	put_unaligned_be64((length + state->count) << 3,
			   &input[SHA_MESSAGE_BYTES - 8]);

	sha_transform(state->digest, input, state->workspace);
	for (i = 0; i < SHA_DIGEST_WORDS; ++i)
		put_unaligned_be32(state->digest[i], &mptcp_hashed_key[i]);

	memzero_explicit(state->workspace, SHA_WORKSPACE_WORDS << 2);
}

void mptcp_crypto_key_sha(u64 key, u32 *token, u64 *idsn)
{
	__be32 mptcp_hashed_key[SHA_DIGEST_WORDS];
	u8 input[SHA_MESSAGE_BYTES];
	struct sha1_state state;

	sha1_init(&state);
	put_unaligned_be64(key, input);
	sha1_pad_final(&state, input, 8, mptcp_hashed_key);

	if (token)
		*token = be32_to_cpu(mptcp_hashed_key[0]);
	if (idsn)
		*idsn = be64_to_cpu(*((__be64 *)&mptcp_hashed_key[3]));
}

void mptcp_crypto_hmac_sha(u64 key1, u64 key2, u32 nonce1, u32 nonce2,
			   u32 *hash_out)
{
	u8 input[SHA_MESSAGE_BYTES * 2];
	struct sha1_state state;
	u8 key1be[8];
	u8 key2be[8];
	int i;

	put_unaligned_be64(key1, key1be);
	put_unaligned_be64(key2, key2be);

	/* Generate key xored with ipad */
	memset(input, 0x36, SHA_MESSAGE_BYTES);
	for (i = 0; i < 8; i++)
		input[i] ^= key1be[i];
	for (i = 0; i < 8; i++)
		input[i + 8] ^= key2be[i];

	put_unaligned_be32(nonce1, &input[SHA_MESSAGE_BYTES]);
	put_unaligned_be32(nonce2, &input[SHA_MESSAGE_BYTES + 4]);

	sha1_init(&state);
	sha1_update(&state, input);

	/* emit sha256(K1 || msg) on the second input block, so we can
	 * reuse 'input' for the last hashing
	 */
	sha1_pad_final(&state, &input[SHA_MESSAGE_BYTES], 8,
		       (__force __be32 *)&input[SHA_MESSAGE_BYTES]);

	/* Prepare second part of hmac */
	memset(input, 0x5C, SHA_MESSAGE_BYTES);
	for (i = 0; i < 8; i++)
		input[i] ^= key1be[i];
	for (i = 0; i < 8; i++)
		input[i + 8] ^= key2be[i];

	sha1_init(&state);
	sha1_update(&state, input);
	sha1_pad_final(&state, &input[SHA_MESSAGE_BYTES], SHA_DIGEST_WORDS << 2,
		       (__be32 *)hash_out);
}
