// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC
 */

/*
 * Implementation of the NH almost-universal hash function, specifically the
 * variant of NH used in Adiantum.  This is *not* a cryptographic hash function.
 *
 * Reference: section 6.3 of "Adiantum: length-preserving encryption for
 * entry-level processors" (https://eprint.iacr.org/2018/720.pdf).
 */

#include <crypto/nh.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/unaligned.h>

#ifdef CONFIG_CRYPTO_LIB_NH_ARCH
#include "nh.h" /* $(SRCARCH)/nh.h */
#else
static bool nh_arch(const u32 *key, const u8 *message, size_t message_len,
		    __le64 hash[NH_NUM_PASSES])
{
	return false;
}
#endif

void nh(const u32 *key, const u8 *message, size_t message_len,
	__le64 hash[NH_NUM_PASSES])
{
	u64 sums[4] = { 0, 0, 0, 0 };

	if (nh_arch(key, message, message_len, hash))
		return;

	static_assert(NH_PAIR_STRIDE == 2);
	static_assert(NH_NUM_PASSES == 4);

	while (message_len) {
		u32 m0 = get_unaligned_le32(message + 0);
		u32 m1 = get_unaligned_le32(message + 4);
		u32 m2 = get_unaligned_le32(message + 8);
		u32 m3 = get_unaligned_le32(message + 12);

		sums[0] += (u64)(u32)(m0 + key[0]) * (u32)(m2 + key[2]);
		sums[1] += (u64)(u32)(m0 + key[4]) * (u32)(m2 + key[6]);
		sums[2] += (u64)(u32)(m0 + key[8]) * (u32)(m2 + key[10]);
		sums[3] += (u64)(u32)(m0 + key[12]) * (u32)(m2 + key[14]);
		sums[0] += (u64)(u32)(m1 + key[1]) * (u32)(m3 + key[3]);
		sums[1] += (u64)(u32)(m1 + key[5]) * (u32)(m3 + key[7]);
		sums[2] += (u64)(u32)(m1 + key[9]) * (u32)(m3 + key[11]);
		sums[3] += (u64)(u32)(m1 + key[13]) * (u32)(m3 + key[15]);
		key += NH_MESSAGE_UNIT / sizeof(key[0]);
		message += NH_MESSAGE_UNIT;
		message_len -= NH_MESSAGE_UNIT;
	}

	hash[0] = cpu_to_le64(sums[0]);
	hash[1] = cpu_to_le64(sums[1]);
	hash[2] = cpu_to_le64(sums[2]);
	hash[3] = cpu_to_le64(sums[3]);
}
EXPORT_SYMBOL_GPL(nh);

#ifdef nh_mod_init_arch
static int __init nh_mod_init(void)
{
	nh_mod_init_arch();
	return 0;
}
subsys_initcall(nh_mod_init);

static void __exit nh_mod_exit(void)
{
}
module_exit(nh_mod_exit);
#endif

MODULE_DESCRIPTION("NH almost-universal hash function");
MODULE_LICENSE("GPL");
