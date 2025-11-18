/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _NFT_SET_PIPAPO_AVX2_H

#if defined(CONFIG_X86_64) && !defined(CONFIG_UML)
#include <asm/fpu/xstate.h>
#define NFT_PIPAPO_ALIGN	(XSAVE_YMM_SIZE / BITS_PER_BYTE)

struct nft_pipapo_match;
bool nft_pipapo_avx2_estimate(const struct nft_set_desc *desc, u32 features,
			      struct nft_set_estimate *est);
struct nft_pipapo_elem *pipapo_get_avx2(const struct nft_pipapo_match *m,
					const u8 *data, u8 genmask,
					u64 tstamp);
#endif /* defined(CONFIG_X86_64) && !defined(CONFIG_UML) */

#endif /* _NFT_SET_PIPAPO_AVX2_H */
