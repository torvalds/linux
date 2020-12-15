/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PURGATORY_H
#define _LINUX_PURGATORY_H

#include <linux/types.h>
#include <crypto/sha2.h>
#include <uapi/linux/kexec.h>

struct kexec_sha_region {
	unsigned long start;
	unsigned long len;
};

/*
 * These forward declarations serve two purposes:
 *
 * 1) Make sparse happy when checking arch/purgatory
 * 2) Document that these are required to be global so the symbol
 *    lookup in kexec works
 */
extern struct kexec_sha_region purgatory_sha_regions[KEXEC_SEGMENT_MAX];
extern u8 purgatory_sha256_digest[SHA256_DIGEST_SIZE];

#endif
