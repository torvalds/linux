/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-3 optimized using the CP Assist for Cryptographic Functions (CPACF)
 *
 * Copyright 2025 Google LLC
 */
#include <asm/cpacf.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_sha3);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_sha3_init_optim);

static void sha3_absorb_blocks(struct sha3_state *state, const u8 *data,
			       size_t nblocks, size_t block_size)
{
	if (static_branch_likely(&have_sha3)) {
		/*
		 * Note that KIMD assumes little-endian order of the state
		 * words.  sha3_state already uses that order, though, so
		 * there's no need for a byteswap.
		 */
		switch (block_size) {
		case SHA3_224_BLOCK_SIZE:
			cpacf_kimd(CPACF_KIMD_SHA3_224, state,
				   data, nblocks * block_size);
			return;
		case SHA3_256_BLOCK_SIZE:
			/*
			 * This case handles both SHA3-256 and SHAKE256, since
			 * they have the same block size.
			 */
			cpacf_kimd(CPACF_KIMD_SHA3_256, state,
				   data, nblocks * block_size);
			return;
		case SHA3_384_BLOCK_SIZE:
			cpacf_kimd(CPACF_KIMD_SHA3_384, state,
				   data, nblocks * block_size);
			return;
		case SHA3_512_BLOCK_SIZE:
			cpacf_kimd(CPACF_KIMD_SHA3_512, state,
				   data, nblocks * block_size);
			return;
		}
	}
	sha3_absorb_blocks_generic(state, data, nblocks, block_size);
}

static void sha3_keccakf(struct sha3_state *state)
{
	if (static_branch_likely(&have_sha3)) {
		/*
		 * Passing zeroes into any of CPACF_KIMD_SHA3_* gives the plain
		 * Keccak-f permutation, which is what we want here.  Use
		 * SHA3-512 since it has the smallest block size.
		 */
		static const u8 zeroes[SHA3_512_BLOCK_SIZE];

		cpacf_kimd(CPACF_KIMD_SHA3_512, state, zeroes, sizeof(zeroes));
	} else {
		sha3_keccakf_generic(state);
	}
}

static inline bool s390_sha3(int func, const u8 *in, size_t in_len,
			     u8 *out, size_t out_len)
{
	struct sha3_state state;

	if (!static_branch_likely(&have_sha3))
		return false;

	if (static_branch_likely(&have_sha3_init_optim))
		func |= CPACF_KLMD_NIP | CPACF_KLMD_DUFOP;
	else
		memset(&state, 0, sizeof(state));

	cpacf_klmd(func, &state, in, in_len);

	if (static_branch_likely(&have_sha3_init_optim))
		kmsan_unpoison_memory(&state, out_len);

	memcpy(out, &state, out_len);
	memzero_explicit(&state, sizeof(state));
	return true;
}

#define sha3_224_arch sha3_224_arch
static bool sha3_224_arch(const u8 *in, size_t in_len,
			  u8 out[SHA3_224_DIGEST_SIZE])
{
	return s390_sha3(CPACF_KLMD_SHA3_224, in, in_len,
			 out, SHA3_224_DIGEST_SIZE);
}

#define sha3_256_arch sha3_256_arch
static bool sha3_256_arch(const u8 *in, size_t in_len,
			  u8 out[SHA3_256_DIGEST_SIZE])
{
	return s390_sha3(CPACF_KLMD_SHA3_256, in, in_len,
			 out, SHA3_256_DIGEST_SIZE);
}

#define sha3_384_arch sha3_384_arch
static bool sha3_384_arch(const u8 *in, size_t in_len,
			  u8 out[SHA3_384_DIGEST_SIZE])
{
	return s390_sha3(CPACF_KLMD_SHA3_384, in, in_len,
			 out, SHA3_384_DIGEST_SIZE);
}

#define sha3_512_arch sha3_512_arch
static bool sha3_512_arch(const u8 *in, size_t in_len,
			  u8 out[SHA3_512_DIGEST_SIZE])
{
	return s390_sha3(CPACF_KLMD_SHA3_512, in, in_len,
			 out, SHA3_512_DIGEST_SIZE);
}

#define sha3_mod_init_arch sha3_mod_init_arch
static void sha3_mod_init_arch(void)
{
	int num_present = 0;
	int num_possible = 0;

	if (!cpu_have_feature(S390_CPU_FEATURE_MSA))
		return;
	/*
	 * Since all the SHA-3 functions are in Message-Security-Assist
	 * Extension 6, just treat them as all or nothing.  This way we need
	 * only one static_key.
	 */
#define QUERY(opcode, func) \
	({ num_present += !!cpacf_query_func(opcode, func); num_possible++; })
	QUERY(CPACF_KIMD, CPACF_KIMD_SHA3_224);
	QUERY(CPACF_KIMD, CPACF_KIMD_SHA3_256);
	QUERY(CPACF_KIMD, CPACF_KIMD_SHA3_384);
	QUERY(CPACF_KIMD, CPACF_KIMD_SHA3_512);
	QUERY(CPACF_KLMD, CPACF_KLMD_SHA3_224);
	QUERY(CPACF_KLMD, CPACF_KLMD_SHA3_256);
	QUERY(CPACF_KLMD, CPACF_KLMD_SHA3_384);
	QUERY(CPACF_KLMD, CPACF_KLMD_SHA3_512);
#undef QUERY

	if (num_present == num_possible) {
		static_branch_enable(&have_sha3);
		if (test_facility(86))
			static_branch_enable(&have_sha3_init_optim);
	} else if (num_present != 0) {
		pr_warn("Unsupported combination of SHA-3 facilities\n");
	}
}
