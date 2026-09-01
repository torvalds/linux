// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017-2019 Linaro Ltd <ard.biesheuvel@linaro.org>
 * Copyright 2026 Google LLC
 */

#include <crypto/aes-cbc-macs.h>
#include <crypto/aes-cbc.h>
#include <crypto/aes-ccm.h>
#include <crypto/aes-ctr.h>
#include <crypto/aes-ecb.h>
#include <crypto/aes-gcm.h>
#include <crypto/aes-xts.h>
#include <crypto/aes.h>
#include <crypto/gf128mul.h>
#include <crypto/utils.h>
#include <linux/cache.h>
#include <linux/crypto.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/unaligned.h>
#include "fips-aes.h"

static const u8 ____cacheline_aligned aes_sbox[] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static const u8 ____cacheline_aligned aes_inv_sbox[] = {
	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
	0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
	0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
	0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
	0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
	0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
	0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
	0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
	0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
	0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
	0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda,
	0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
	0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
	0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
	0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
	0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea,
	0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
	0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
	0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
	0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
	0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
	0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20,
	0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
	0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
	0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
	0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
	0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
	0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0,
	0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
	0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26,
	0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d,
};

extern const u8 crypto_aes_sbox[256] __alias(aes_sbox);
extern const u8 crypto_aes_inv_sbox[256] __alias(aes_inv_sbox);

EXPORT_SYMBOL(crypto_aes_sbox);
EXPORT_SYMBOL(crypto_aes_inv_sbox);

/* aes_enc_tab[i] contains MixColumn([SubByte(i), 0, 0, 0]). */
const u32 ____cacheline_aligned aes_enc_tab[256] = {
	0xa56363c6, 0x847c7cf8, 0x997777ee, 0x8d7b7bf6, 0x0df2f2ff, 0xbd6b6bd6,
	0xb16f6fde, 0x54c5c591, 0x50303060, 0x03010102, 0xa96767ce, 0x7d2b2b56,
	0x19fefee7, 0x62d7d7b5, 0xe6abab4d, 0x9a7676ec, 0x45caca8f, 0x9d82821f,
	0x40c9c989, 0x877d7dfa, 0x15fafaef, 0xeb5959b2, 0xc947478e, 0x0bf0f0fb,
	0xecadad41, 0x67d4d4b3, 0xfda2a25f, 0xeaafaf45, 0xbf9c9c23, 0xf7a4a453,
	0x967272e4, 0x5bc0c09b, 0xc2b7b775, 0x1cfdfde1, 0xae93933d, 0x6a26264c,
	0x5a36366c, 0x413f3f7e, 0x02f7f7f5, 0x4fcccc83, 0x5c343468, 0xf4a5a551,
	0x34e5e5d1, 0x08f1f1f9, 0x937171e2, 0x73d8d8ab, 0x53313162, 0x3f15152a,
	0x0c040408, 0x52c7c795, 0x65232346, 0x5ec3c39d, 0x28181830, 0xa1969637,
	0x0f05050a, 0xb59a9a2f, 0x0907070e, 0x36121224, 0x9b80801b, 0x3de2e2df,
	0x26ebebcd, 0x6927274e, 0xcdb2b27f, 0x9f7575ea, 0x1b090912, 0x9e83831d,
	0x742c2c58, 0x2e1a1a34, 0x2d1b1b36, 0xb26e6edc, 0xee5a5ab4, 0xfba0a05b,
	0xf65252a4, 0x4d3b3b76, 0x61d6d6b7, 0xceb3b37d, 0x7b292952, 0x3ee3e3dd,
	0x712f2f5e, 0x97848413, 0xf55353a6, 0x68d1d1b9, 0x00000000, 0x2cededc1,
	0x60202040, 0x1ffcfce3, 0xc8b1b179, 0xed5b5bb6, 0xbe6a6ad4, 0x46cbcb8d,
	0xd9bebe67, 0x4b393972, 0xde4a4a94, 0xd44c4c98, 0xe85858b0, 0x4acfcf85,
	0x6bd0d0bb, 0x2aefefc5, 0xe5aaaa4f, 0x16fbfbed, 0xc5434386, 0xd74d4d9a,
	0x55333366, 0x94858511, 0xcf45458a, 0x10f9f9e9, 0x06020204, 0x817f7ffe,
	0xf05050a0, 0x443c3c78, 0xba9f9f25, 0xe3a8a84b, 0xf35151a2, 0xfea3a35d,
	0xc0404080, 0x8a8f8f05, 0xad92923f, 0xbc9d9d21, 0x48383870, 0x04f5f5f1,
	0xdfbcbc63, 0xc1b6b677, 0x75dadaaf, 0x63212142, 0x30101020, 0x1affffe5,
	0x0ef3f3fd, 0x6dd2d2bf, 0x4ccdcd81, 0x140c0c18, 0x35131326, 0x2fececc3,
	0xe15f5fbe, 0xa2979735, 0xcc444488, 0x3917172e, 0x57c4c493, 0xf2a7a755,
	0x827e7efc, 0x473d3d7a, 0xac6464c8, 0xe75d5dba, 0x2b191932, 0x957373e6,
	0xa06060c0, 0x98818119, 0xd14f4f9e, 0x7fdcdca3, 0x66222244, 0x7e2a2a54,
	0xab90903b, 0x8388880b, 0xca46468c, 0x29eeeec7, 0xd3b8b86b, 0x3c141428,
	0x79dedea7, 0xe25e5ebc, 0x1d0b0b16, 0x76dbdbad, 0x3be0e0db, 0x56323264,
	0x4e3a3a74, 0x1e0a0a14, 0xdb494992, 0x0a06060c, 0x6c242448, 0xe45c5cb8,
	0x5dc2c29f, 0x6ed3d3bd, 0xefacac43, 0xa66262c4, 0xa8919139, 0xa4959531,
	0x37e4e4d3, 0x8b7979f2, 0x32e7e7d5, 0x43c8c88b, 0x5937376e, 0xb76d6dda,
	0x8c8d8d01, 0x64d5d5b1, 0xd24e4e9c, 0xe0a9a949, 0xb46c6cd8, 0xfa5656ac,
	0x07f4f4f3, 0x25eaeacf, 0xaf6565ca, 0x8e7a7af4, 0xe9aeae47, 0x18080810,
	0xd5baba6f, 0x887878f0, 0x6f25254a, 0x722e2e5c, 0x241c1c38, 0xf1a6a657,
	0xc7b4b473, 0x51c6c697, 0x23e8e8cb, 0x7cdddda1, 0x9c7474e8, 0x211f1f3e,
	0xdd4b4b96, 0xdcbdbd61, 0x868b8b0d, 0x858a8a0f, 0x907070e0, 0x423e3e7c,
	0xc4b5b571, 0xaa6666cc, 0xd8484890, 0x05030306, 0x01f6f6f7, 0x120e0e1c,
	0xa36161c2, 0x5f35356a, 0xf95757ae, 0xd0b9b969, 0x91868617, 0x58c1c199,
	0x271d1d3a, 0xb99e9e27, 0x38e1e1d9, 0x13f8f8eb, 0xb398982b, 0x33111122,
	0xbb6969d2, 0x70d9d9a9, 0x898e8e07, 0xa7949433, 0xb69b9b2d, 0x221e1e3c,
	0x92878715, 0x20e9e9c9, 0x49cece87, 0xff5555aa, 0x78282850, 0x7adfdfa5,
	0x8f8c8c03, 0xf8a1a159, 0x80898909, 0x170d0d1a, 0xdabfbf65, 0x31e6e6d7,
	0xc6424284, 0xb86868d0, 0xc3414182, 0xb0999929, 0x772d2d5a, 0x110f0f1e,
	0xcbb0b07b, 0xfc5454a8, 0xd6bbbb6d, 0x3a16162c,
};
EXPORT_SYMBOL(aes_enc_tab);

/* aes_dec_tab[i] contains InvMixColumn([InvSubByte(i), 0, 0, 0]). */
const u32 ____cacheline_aligned aes_dec_tab[256] = {
	0x50a7f451, 0x5365417e, 0xc3a4171a, 0x965e273a, 0xcb6bab3b, 0xf1459d1f,
	0xab58faac, 0x9303e34b, 0x55fa3020, 0xf66d76ad, 0x9176cc88, 0x254c02f5,
	0xfcd7e54f, 0xd7cb2ac5, 0x80443526, 0x8fa362b5, 0x495ab1de, 0x671bba25,
	0x980eea45, 0xe1c0fe5d, 0x02752fc3, 0x12f04c81, 0xa397468d, 0xc6f9d36b,
	0xe75f8f03, 0x959c9215, 0xeb7a6dbf, 0xda595295, 0x2d83bed4, 0xd3217458,
	0x2969e049, 0x44c8c98e, 0x6a89c275, 0x78798ef4, 0x6b3e5899, 0xdd71b927,
	0xb64fe1be, 0x17ad88f0, 0x66ac20c9, 0xb43ace7d, 0x184adf63, 0x82311ae5,
	0x60335197, 0x457f5362, 0xe07764b1, 0x84ae6bbb, 0x1ca081fe, 0x942b08f9,
	0x58684870, 0x19fd458f, 0x876cde94, 0xb7f87b52, 0x23d373ab, 0xe2024b72,
	0x578f1fe3, 0x2aab5566, 0x0728ebb2, 0x03c2b52f, 0x9a7bc586, 0xa50837d3,
	0xf2872830, 0xb2a5bf23, 0xba6a0302, 0x5c8216ed, 0x2b1ccf8a, 0x92b479a7,
	0xf0f207f3, 0xa1e2694e, 0xcdf4da65, 0xd5be0506, 0x1f6234d1, 0x8afea6c4,
	0x9d532e34, 0xa055f3a2, 0x32e18a05, 0x75ebf6a4, 0x39ec830b, 0xaaef6040,
	0x069f715e, 0x51106ebd, 0xf98a213e, 0x3d06dd96, 0xae053edd, 0x46bde64d,
	0xb58d5491, 0x055dc471, 0x6fd40604, 0xff155060, 0x24fb9819, 0x97e9bdd6,
	0xcc434089, 0x779ed967, 0xbd42e8b0, 0x888b8907, 0x385b19e7, 0xdbeec879,
	0x470a7ca1, 0xe90f427c, 0xc91e84f8, 0x00000000, 0x83868009, 0x48ed2b32,
	0xac70111e, 0x4e725a6c, 0xfbff0efd, 0x5638850f, 0x1ed5ae3d, 0x27392d36,
	0x64d90f0a, 0x21a65c68, 0xd1545b9b, 0x3a2e3624, 0xb1670a0c, 0x0fe75793,
	0xd296eeb4, 0x9e919b1b, 0x4fc5c080, 0xa220dc61, 0x694b775a, 0x161a121c,
	0x0aba93e2, 0xe52aa0c0, 0x43e0223c, 0x1d171b12, 0x0b0d090e, 0xadc78bf2,
	0xb9a8b62d, 0xc8a91e14, 0x8519f157, 0x4c0775af, 0xbbdd99ee, 0xfd607fa3,
	0x9f2601f7, 0xbcf5725c, 0xc53b6644, 0x347efb5b, 0x7629438b, 0xdcc623cb,
	0x68fcedb6, 0x63f1e4b8, 0xcadc31d7, 0x10856342, 0x40229713, 0x2011c684,
	0x7d244a85, 0xf83dbbd2, 0x1132f9ae, 0x6da129c7, 0x4b2f9e1d, 0xf330b2dc,
	0xec52860d, 0xd0e3c177, 0x6c16b32b, 0x99b970a9, 0xfa489411, 0x2264e947,
	0xc48cfca8, 0x1a3ff0a0, 0xd82c7d56, 0xef903322, 0xc74e4987, 0xc1d138d9,
	0xfea2ca8c, 0x360bd498, 0xcf81f5a6, 0x28de7aa5, 0x268eb7da, 0xa4bfad3f,
	0xe49d3a2c, 0x0d927850, 0x9bcc5f6a, 0x62467e54, 0xc2138df6, 0xe8b8d890,
	0x5ef7392e, 0xf5afc382, 0xbe805d9f, 0x7c93d069, 0xa92dd56f, 0xb31225cf,
	0x3b99acc8, 0xa77d1810, 0x6e639ce8, 0x7bbb3bdb, 0x097826cd, 0xf418596e,
	0x01b79aec, 0xa89a4f83, 0x656e95e6, 0x7ee6ffaa, 0x08cfbc21, 0xe6e815ef,
	0xd99be7ba, 0xce366f4a, 0xd4099fea, 0xd67cb029, 0xafb2a431, 0x31233f2a,
	0x3094a5c6, 0xc066a235, 0x37bc4e74, 0xa6ca82fc, 0xb0d090e0, 0x15d8a733,
	0x4a9804f1, 0xf7daec41, 0x0e50cd7f, 0x2ff69117, 0x8dd64d76, 0x4db0ef43,
	0x544daacc, 0xdf0496e4, 0xe3b5d19e, 0x1b886a4c, 0xb81f2cc1, 0x7f516546,
	0x04ea5e9d, 0x5d358c01, 0x737487fa, 0x2e410bfb, 0x5a1d67b3, 0x52d2db92,
	0x335610e9, 0x1347d66d, 0x8c61d79a, 0x7a0ca137, 0x8e14f859, 0x893c13eb,
	0xee27a9ce, 0x35c961b7, 0xede51ce1, 0x3cb1477a, 0x59dfd29c, 0x3f73f255,
	0x79ce1418, 0xbf37c773, 0xeacdf753, 0x5baafd5f, 0x146f3ddf, 0x86db4478,
	0x81f3afca, 0x3ec468b9, 0x2c342438, 0x5f40a3c2, 0x72c31d16, 0x0c25e2bc,
	0x8b493c28, 0x41950dff, 0x7101a839, 0xdeb30c08, 0x9ce4b4d8, 0x90c15664,
	0x6184cb7b, 0x70b632d5, 0x745c6c48, 0x4257b8d0,
};
EXPORT_SYMBOL(aes_dec_tab);

/* Prefetch data into L1 cache.  @mem should be cacheline-aligned. */
static __always_inline void aes_prefetch(const void *mem, size_t len)
{
	for (size_t i = 0; i < len; i += L1_CACHE_BYTES)
		*(volatile const u8 *)(mem + i);
	barrier();
}

static u32 mul_by_x(u32 w)
{
	u32 x = w & 0x7f7f7f7f;
	u32 y = w & 0x80808080;

	/* multiply by polynomial 'x' (0b10) in GF(2^8) */
	return (x << 1) ^ (y >> 7) * 0x1b;
}

static u32 mul_by_x2(u32 w)
{
	u32 x = w & 0x3f3f3f3f;
	u32 y = w & 0x80808080;
	u32 z = w & 0x40404040;

	/* multiply by polynomial 'x^2' (0b100) in GF(2^8) */
	return (x << 2) ^ (y >> 7) * 0x36 ^ (z >> 6) * 0x1b;
}

static u32 mix_columns(u32 x)
{
	/*
	 * Perform the following matrix multiplication in GF(2^8)
	 *
	 * | 0x2 0x3 0x1 0x1 |   | x[0] |
	 * | 0x1 0x2 0x3 0x1 |   | x[1] |
	 * | 0x1 0x1 0x2 0x3 | x | x[2] |
	 * | 0x3 0x1 0x1 0x2 |   | x[3] |
	 */
	u32 y = mul_by_x(x) ^ ror32(x, 16);

	return y ^ ror32(x ^ y, 8);
}

static u32 inv_mix_columns(u32 x)
{
	/*
	 * Perform the following matrix multiplication in GF(2^8)
	 *
	 * | 0xe 0xb 0xd 0x9 |   | x[0] |
	 * | 0x9 0xe 0xb 0xd |   | x[1] |
	 * | 0xd 0x9 0xe 0xb | x | x[2] |
	 * | 0xb 0xd 0x9 0xe |   | x[3] |
	 *
	 * which can conveniently be reduced to
	 *
	 * | 0x2 0x3 0x1 0x1 |   | 0x5 0x0 0x4 0x0 |   | x[0] |
	 * | 0x1 0x2 0x3 0x1 |   | 0x0 0x5 0x0 0x4 |   | x[1] |
	 * | 0x1 0x1 0x2 0x3 | x | 0x4 0x0 0x5 0x0 | x | x[2] |
	 * | 0x3 0x1 0x1 0x2 |   | 0x0 0x4 0x0 0x5 |   | x[3] |
	 */
	u32 y = mul_by_x2(x);

	return mix_columns(x ^ y ^ ror32(y, 16));
}

static u32 subw(u32 in)
{
	return (aes_sbox[in & 0xff]) ^
	       (aes_sbox[(in >>  8) & 0xff] <<  8) ^
	       (aes_sbox[(in >> 16) & 0xff] << 16) ^
	       (aes_sbox[(in >> 24) & 0xff] << 24);
}

static void aes_expandkey_generic(u32 rndkeys[], u32 *inv_rndkeys,
				  const u8 *in_key, int key_len)
{
	u32 kwords = key_len / sizeof(u32);
	u32 rc, i, j;

	for (i = 0; i < kwords; i++)
		rndkeys[i] = get_unaligned_le32(&in_key[i * sizeof(u32)]);

	for (i = 0, rc = 1; i < 10; i++, rc = mul_by_x(rc)) {
		u32 *rki = &rndkeys[i * kwords];
		u32 *rko = rki + kwords;

		rko[0] = ror32(subw(rki[kwords - 1]), 8) ^ rc ^ rki[0];
		rko[1] = rko[0] ^ rki[1];
		rko[2] = rko[1] ^ rki[2];
		rko[3] = rko[2] ^ rki[3];

		if (key_len == AES_KEYSIZE_192) {
			if (i >= 7)
				break;
			rko[4] = rko[3] ^ rki[4];
			rko[5] = rko[4] ^ rki[5];
		} else if (key_len == AES_KEYSIZE_256) {
			if (i >= 6)
				break;
			rko[4] = subw(rko[3]) ^ rki[4];
			rko[5] = rko[4] ^ rki[5];
			rko[6] = rko[5] ^ rki[6];
			rko[7] = rko[6] ^ rki[7];
		}
	}

	/*
	 * Generate the decryption keys for the Equivalent Inverse Cipher.
	 * This involves reversing the order of the round keys, and applying
	 * the Inverse Mix Columns transformation to all but the first and
	 * the last one.
	 */
	if (inv_rndkeys) {
		inv_rndkeys[0] = rndkeys[key_len + 24];
		inv_rndkeys[1] = rndkeys[key_len + 25];
		inv_rndkeys[2] = rndkeys[key_len + 26];
		inv_rndkeys[3] = rndkeys[key_len + 27];

		for (i = 4, j = key_len + 20; j > 0; i += 4, j -= 4) {
			inv_rndkeys[i]     = inv_mix_columns(rndkeys[j]);
			inv_rndkeys[i + 1] = inv_mix_columns(rndkeys[j + 1]);
			inv_rndkeys[i + 2] = inv_mix_columns(rndkeys[j + 2]);
			inv_rndkeys[i + 3] = inv_mix_columns(rndkeys[j + 3]);
		}

		inv_rndkeys[i]     = rndkeys[0];
		inv_rndkeys[i + 1] = rndkeys[1];
		inv_rndkeys[i + 2] = rndkeys[2];
		inv_rndkeys[i + 3] = rndkeys[3];
	}
}

int aes_expandkey(struct crypto_aes_ctx *ctx, const u8 *in_key,
		  unsigned int key_len)
{
	if (aes_check_keylen(key_len) != 0)
		return -EINVAL;
	ctx->key_length = key_len;
	aes_expandkey_generic(ctx->key_enc, ctx->key_dec, in_key, key_len);
	return 0;
}
EXPORT_SYMBOL(aes_expandkey);

static __always_inline u32 enc_quarterround(const u32 w[4], int i, u32 rk)
{
	return rk ^ aes_enc_tab[(u8)w[i]] ^
	       rol32(aes_enc_tab[(u8)(w[(i + 1) % 4] >> 8)], 8) ^
	       rol32(aes_enc_tab[(u8)(w[(i + 2) % 4] >> 16)], 16) ^
	       rol32(aes_enc_tab[(u8)(w[(i + 3) % 4] >> 24)], 24);
}

static __always_inline u32 enclast_quarterround(const u32 w[4], int i, u32 rk)
{
	return rk ^ ((aes_enc_tab[(u8)w[i]] & 0x0000ff00) >> 8) ^
	       (aes_enc_tab[(u8)(w[(i + 1) % 4] >> 8)] & 0x0000ff00) ^
	       ((aes_enc_tab[(u8)(w[(i + 2) % 4] >> 16)] & 0x0000ff00) << 8) ^
	       ((aes_enc_tab[(u8)(w[(i + 3) % 4] >> 24)] & 0x0000ff00) << 16);
}

static void __maybe_unused aes_encrypt_generic(const u32 rndkeys[], int nrounds,
					       u8 out[AES_BLOCK_SIZE],
					       const u8 in[AES_BLOCK_SIZE])
{
	const u32 *rkp = rndkeys;
	int n = nrounds - 1;
	u32 w[4];

	w[0] = get_unaligned_le32(&in[0]) ^ *rkp++;
	w[1] = get_unaligned_le32(&in[4]) ^ *rkp++;
	w[2] = get_unaligned_le32(&in[8]) ^ *rkp++;
	w[3] = get_unaligned_le32(&in[12]) ^ *rkp++;

	/*
	 * Prefetch the table before doing data and key-dependent loads from it.
	 *
	 * This is intended only as a basic constant-time hardening measure that
	 * avoids interfering with performance too much.  Its effectiveness is
	 * not guaranteed.  For proper constant-time AES, a CPU that supports
	 * AES instructions should be used instead.
	 */
	aes_prefetch(aes_enc_tab, sizeof(aes_enc_tab));

	do {
		u32 w0 = enc_quarterround(w, 0, *rkp++);
		u32 w1 = enc_quarterround(w, 1, *rkp++);
		u32 w2 = enc_quarterround(w, 2, *rkp++);
		u32 w3 = enc_quarterround(w, 3, *rkp++);

		w[0] = w0;
		w[1] = w1;
		w[2] = w2;
		w[3] = w3;
	} while (--n);

	put_unaligned_le32(enclast_quarterround(w, 0, *rkp++), &out[0]);
	put_unaligned_le32(enclast_quarterround(w, 1, *rkp++), &out[4]);
	put_unaligned_le32(enclast_quarterround(w, 2, *rkp++), &out[8]);
	put_unaligned_le32(enclast_quarterround(w, 3, *rkp++), &out[12]);
}

static __always_inline u32 dec_quarterround(const u32 w[4], int i, u32 rk)
{
	return rk ^ aes_dec_tab[(u8)w[i]] ^
	       rol32(aes_dec_tab[(u8)(w[(i + 3) % 4] >> 8)], 8) ^
	       rol32(aes_dec_tab[(u8)(w[(i + 2) % 4] >> 16)], 16) ^
	       rol32(aes_dec_tab[(u8)(w[(i + 1) % 4] >> 24)], 24);
}

static __always_inline u32 declast_quarterround(const u32 w[4], int i, u32 rk)
{
	return rk ^ aes_inv_sbox[(u8)w[i]] ^
	       ((u32)aes_inv_sbox[(u8)(w[(i + 3) % 4] >> 8)] << 8) ^
	       ((u32)aes_inv_sbox[(u8)(w[(i + 2) % 4] >> 16)] << 16) ^
	       ((u32)aes_inv_sbox[(u8)(w[(i + 1) % 4] >> 24)] << 24);
}

static void __maybe_unused aes_decrypt_generic(const u32 inv_rndkeys[],
					       int nrounds,
					       u8 out[AES_BLOCK_SIZE],
					       const u8 in[AES_BLOCK_SIZE])
{
	const u32 *rkp = inv_rndkeys;
	int n = nrounds - 1;
	u32 w[4];

	w[0] = get_unaligned_le32(&in[0]) ^ *rkp++;
	w[1] = get_unaligned_le32(&in[4]) ^ *rkp++;
	w[2] = get_unaligned_le32(&in[8]) ^ *rkp++;
	w[3] = get_unaligned_le32(&in[12]) ^ *rkp++;

	aes_prefetch(aes_dec_tab, sizeof(aes_dec_tab));

	do {
		u32 w0 = dec_quarterround(w, 0, *rkp++);
		u32 w1 = dec_quarterround(w, 1, *rkp++);
		u32 w2 = dec_quarterround(w, 2, *rkp++);
		u32 w3 = dec_quarterround(w, 3, *rkp++);

		w[0] = w0;
		w[1] = w1;
		w[2] = w2;
		w[3] = w3;
	} while (--n);

	aes_prefetch(aes_inv_sbox, sizeof(aes_inv_sbox));
	put_unaligned_le32(declast_quarterround(w, 0, *rkp++), &out[0]);
	put_unaligned_le32(declast_quarterround(w, 1, *rkp++), &out[4]);
	put_unaligned_le32(declast_quarterround(w, 2, *rkp++), &out[8]);
	put_unaligned_le32(declast_quarterround(w, 3, *rkp++), &out[12]);
}

/*
 * Note: the aes_prepare*key_* names reflect the fact that the implementation
 * might not actually expand the key.  (The s390 code for example doesn't.)
 * Where the key is expanded we use the more specific names aes_expandkey_*.
 *
 * aes_preparekey_arch() is passed an optional pointer 'inv_k' which points to
 * the area to store the prepared decryption key.  It will be NULL if the user
 * is requesting encryption-only.  aes_preparekey_arch() is also passed a valid
 * 'key_len' and 'nrounds', corresponding to AES-128, AES-192, or AES-256.
 */
#ifdef CONFIG_CRYPTO_LIB_AES_ARCH
/* An arch-specific implementation of AES is available.  Include it. */
#include "aes.h" /* $(SRCARCH)/aes.h */
#else
/* No arch-specific implementation of AES is available.  Use generic code. */

static void aes_preparekey_arch(union aes_enckey_arch *k,
				union aes_invkey_arch *inv_k,
				const u8 *in_key, int key_len, int nrounds)
{
	aes_expandkey_generic(k->rndkeys, inv_k ? inv_k->inv_rndkeys : NULL,
			      in_key, key_len);
}

static void aes_encrypt_arch(const struct aes_enckey *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	aes_encrypt_generic(key->k.rndkeys, key->nrounds, out, in);
}

static void aes_decrypt_arch(const struct aes_key *key,
			     u8 out[AES_BLOCK_SIZE],
			     const u8 in[AES_BLOCK_SIZE])
{
	aes_decrypt_generic(key->inv_k.inv_rndkeys, key->nrounds, out, in);
}
#endif

static int __aes_preparekey(struct aes_enckey *enc_key,
			    union aes_invkey_arch *inv_k,
			    const u8 *in_key, size_t key_len)
{
	if (aes_check_keylen(key_len) != 0)
		return -EINVAL;
	enc_key->len = key_len;
	enc_key->nrounds = 6 + key_len / 4;
	aes_preparekey_arch(&enc_key->k, inv_k, in_key, key_len,
			    enc_key->nrounds);
	return 0;
}

int aes_preparekey(struct aes_key *key, const u8 *in_key, size_t key_len)
{
	return __aes_preparekey((struct aes_enckey *)key, &key->inv_k,
				in_key, key_len);
}
EXPORT_SYMBOL(aes_preparekey);

int aes_prepareenckey(struct aes_enckey *key, const u8 *in_key, size_t key_len)
{
	return __aes_preparekey(key, NULL, in_key, key_len);
}
EXPORT_SYMBOL(aes_prepareenckey);

void aes_encrypt(aes_encrypt_arg key, u8 out[AES_BLOCK_SIZE],
		 const u8 in[AES_BLOCK_SIZE])
{
	aes_encrypt_arch(key.enc_key, out, in);
}
EXPORT_SYMBOL(aes_encrypt);

void aes_decrypt(const struct aes_key *key, u8 out[AES_BLOCK_SIZE],
		 const u8 in[AES_BLOCK_SIZE])
{
	aes_decrypt_arch(key, out, in);
}
EXPORT_SYMBOL(aes_decrypt);

/* FIPS cryptographic algorithm self-test for "bare" AES */
static void __init aes_fips_test(void)
{
	struct aes_key key;
	u8 data[AES_BLOCK_SIZE];

	if (aes_preparekey(&key, fips_test_key, sizeof(fips_test_key)) != 0)
		panic("aes: FIPS self-test failed (preparekey)\n");

	aes_encrypt(&key, data, fips_test_data);
	if (memcmp(fips_test_aes_ecb_ctext, data, sizeof(data)) != 0)
		panic("aes: FIPS self-test failed (wrong ciphertext)\n");

	aes_decrypt(&key, data, data);
	if (memcmp(fips_test_data, data, sizeof(data)) != 0)
		panic("aes: FIPS self-test failed (wrong plaintext)\n");

	memzero_explicit(&key, sizeof(key));
}

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_CBC_MACS)

#ifndef aes_cbcmac_blocks_arch
static bool aes_cbcmac_blocks_arch(u8 h[AES_BLOCK_SIZE],
				   const struct aes_enckey *key, const u8 *data,
				   size_t nblocks, bool enc_before,
				   bool enc_after)
{
	return false;
}
#endif

/* This assumes nblocks >= 1. */
static void aes_cbcmac_blocks(u8 h[AES_BLOCK_SIZE],
			      const struct aes_enckey *key, const u8 *data,
			      size_t nblocks, bool enc_before, bool enc_after)
{
	if (aes_cbcmac_blocks_arch(h, key, data, nblocks, enc_before,
				   enc_after))
		return;

	if (enc_before)
		aes_encrypt(key, h, h);
	for (; nblocks > 1; nblocks--) {
		crypto_xor(h, data, AES_BLOCK_SIZE);
		data += AES_BLOCK_SIZE;
		aes_encrypt(key, h, h);
	}
	crypto_xor(h, data, AES_BLOCK_SIZE);
	if (enc_after)
		aes_encrypt(key, h, h);
}

int aes_cmac_preparekey(struct aes_cmac_key *key, const u8 *in_key,
			size_t key_len)
{
	u64 hi, lo, mask;
	int err;

	/* Prepare the AES key. */
	err = aes_prepareenckey(&key->aes, in_key, key_len);
	if (err)
		return err;

	/*
	 * Prepare the subkeys K1 and K2 by encrypting the all-zeroes block,
	 * then multiplying by 'x' and 'x^2' (respectively) in GF(2^128).
	 * Reference: NIST SP 800-38B, Section 6.1 "Subkey Generation".
	 */
	memset(key->k_final[0].b, 0, AES_BLOCK_SIZE);
	aes_encrypt(&key->aes, key->k_final[0].b, key->k_final[0].b);
	hi = be64_to_cpu(key->k_final[0].w[0]);
	lo = be64_to_cpu(key->k_final[0].w[1]);
	for (int i = 0; i < 2; i++) {
		mask = ((s64)hi >> 63) & 0x87;
		hi = (hi << 1) ^ (lo >> 63);
		lo = (lo << 1) ^ mask;
		key->k_final[i].w[0] = cpu_to_be64(hi);
		key->k_final[i].w[1] = cpu_to_be64(lo);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(aes_cmac_preparekey);

void aes_xcbcmac_preparekey(struct aes_cmac_key *key,
			    const u8 in_key[AES_KEYSIZE_128])
{
	static const u8 constants[3][AES_BLOCK_SIZE] = {
		{ [0 ... AES_BLOCK_SIZE - 1] = 0x1 },
		{ [0 ... AES_BLOCK_SIZE - 1] = 0x2 },
		{ [0 ... AES_BLOCK_SIZE - 1] = 0x3 },
	};
	u8 new_aes_key[AES_BLOCK_SIZE];

	static_assert(AES_BLOCK_SIZE == AES_KEYSIZE_128);
	aes_prepareenckey(&key->aes, in_key, AES_BLOCK_SIZE);
	aes_encrypt(&key->aes, new_aes_key, constants[0]);
	aes_encrypt(&key->aes, key->k_final[0].b, constants[1]);
	aes_encrypt(&key->aes, key->k_final[1].b, constants[2]);
	aes_prepareenckey(&key->aes, new_aes_key, AES_BLOCK_SIZE);
	memzero_explicit(new_aes_key, AES_BLOCK_SIZE);
}
EXPORT_SYMBOL_GPL(aes_xcbcmac_preparekey);

void aes_cmac_update(struct aes_cmac_ctx *ctx, const u8 *data, size_t data_len)
{
	bool enc_before = false;
	size_t nblocks;

	if (ctx->partial_len) {
		/* XOR data into a pending block. */
		size_t l = min(data_len, AES_BLOCK_SIZE - ctx->partial_len);

		crypto_xor(&ctx->h[ctx->partial_len], data, l);
		data += l;
		data_len -= l;
		ctx->partial_len += l;
		if (data_len == 0) {
			/*
			 * Either the pending block hasn't been filled yet, or
			 * no more data was given so it's not yet known whether
			 * the block is the final block.
			 */
			return;
		}
		/* Pending block has been filled and isn't the final block. */
		enc_before = true;
	}

	nblocks = data_len / AES_BLOCK_SIZE;
	data_len %= AES_BLOCK_SIZE;
	if (nblocks == 0) {
		/* 0 additional full blocks, then optionally a partial block */
		if (enc_before)
			aes_encrypt(&ctx->key->aes, ctx->h, ctx->h);
		crypto_xor(ctx->h, data, data_len);
		ctx->partial_len = data_len;
	} else if (data_len != 0) {
		/* 1 or more additional full blocks, then a partial block */
		aes_cbcmac_blocks(ctx->h, &ctx->key->aes, data, nblocks,
				  enc_before, /* enc_after= */ true);
		data += nblocks * AES_BLOCK_SIZE;
		crypto_xor(ctx->h, data, data_len);
		ctx->partial_len = data_len;
	} else {
		/*
		 * 1 or more additional full blocks only.  Encryption of the
		 * last block is delayed until it's known whether it's the final
		 * block in the message or not.
		 */
		aes_cbcmac_blocks(ctx->h, &ctx->key->aes, data, nblocks,
				  enc_before, /* enc_after= */ false);
		ctx->partial_len = AES_BLOCK_SIZE;
	}
}
EXPORT_SYMBOL_GPL(aes_cmac_update);

void aes_cmac_final(struct aes_cmac_ctx *ctx, u8 out[AES_BLOCK_SIZE])
{
	if (ctx->partial_len == AES_BLOCK_SIZE) {
		/* Final block is a full block.  Use k_final[0]. */
		crypto_xor(ctx->h, ctx->key->k_final[0].b, AES_BLOCK_SIZE);
	} else {
		/* Final block is a partial block.  Pad, and use k_final[1]. */
		ctx->h[ctx->partial_len] ^= 0x80;
		crypto_xor(ctx->h, ctx->key->k_final[1].b, AES_BLOCK_SIZE);
	}
	aes_encrypt(&ctx->key->aes, out, ctx->h);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_GPL(aes_cmac_final);

void aes_cbcmac_update(struct aes_cbcmac_ctx *ctx, const u8 *data,
		       size_t data_len)
{
	bool enc_before = false;
	size_t nblocks;

	if (ctx->partial_len) {
		size_t l = min(data_len, AES_BLOCK_SIZE - ctx->partial_len);

		crypto_xor(&ctx->h[ctx->partial_len], data, l);
		data += l;
		data_len -= l;
		ctx->partial_len += l;
		if (ctx->partial_len < AES_BLOCK_SIZE)
			return;
		enc_before = true;
	}

	nblocks = data_len / AES_BLOCK_SIZE;
	data_len %= AES_BLOCK_SIZE;
	if (nblocks == 0) {
		if (enc_before)
			aes_encrypt(ctx->key, ctx->h, ctx->h);
	} else {
		aes_cbcmac_blocks(ctx->h, ctx->key, data, nblocks, enc_before,
				  /* enc_after= */ true);
		data += nblocks * AES_BLOCK_SIZE;
	}
	crypto_xor(ctx->h, data, data_len);
	ctx->partial_len = data_len;
}
EXPORT_SYMBOL_NS_GPL(aes_cbcmac_update, "CRYPTO_INTERNAL");

void aes_cbcmac_final(struct aes_cbcmac_ctx *ctx, u8 out[AES_BLOCK_SIZE])
{
	if (ctx->partial_len)
		aes_encrypt(ctx->key, out, ctx->h);
	else
		memcpy(out, ctx->h, AES_BLOCK_SIZE);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_NS_GPL(aes_cbcmac_final, "CRYPTO_INTERNAL");

/* FIPS cryptographic algorithm self-test for AES-CMAC */
static void __init aes_cmac_fips_test(void)
{
	struct aes_cmac_key key __cleanup(aes_cmac_zeroize_key);
	u8 mac[AES_BLOCK_SIZE];

	if (aes_cmac_preparekey(&key, fips_test_key, sizeof(fips_test_key)) !=
	    0)
		panic("aes: CMAC FIPS self-test failed (preparekey)\n");
	aes_cmac(&key, fips_test_data, sizeof(fips_test_data), mac);
	if (memcmp(fips_test_aes_cmac_value, mac, sizeof(mac)) != 0)
		panic("aes: CMAC FIPS self-test failed (wrong MAC)\n");
}
#else /* CONFIG_CRYPTO_LIB_AES_CBC_MACS */
static inline void aes_cmac_fips_test(void)
{
}
#endif /* !CONFIG_CRYPTO_LIB_AES_CBC_MACS */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_ECB)
/*
 * Hooks for optimized AES-ECB implementations, overridable by the architecture.
 * They are called with len > 0 && len % AES_BLOCK_SIZE == 0.  Returning false
 * causes the fallback implementation to be used instead.
 */
#ifndef aes_ecb_encrypt_arch
static bool aes_ecb_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				 const struct aes_enckey *key)
{
	return false;
}
#endif
#ifndef aes_ecb_decrypt_arch
static bool aes_ecb_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				 const struct aes_key *key)
{
	return false;
}
#endif

void aes_ecb_encrypt(u8 *dst, const u8 *src, size_t len, aes_encrypt_arg key)
{
	if (WARN_ON_ONCE(len % AES_BLOCK_SIZE))
		len = round_down(len, AES_BLOCK_SIZE);

	if (unlikely(len == 0))
		return;

	if (likely(aes_ecb_encrypt_arch(dst, src, len, key.enc_key)))
		return;

	for (size_t i = 0; i < len; i += AES_BLOCK_SIZE)
		aes_encrypt(key, &dst[i], &src[i]);
}
EXPORT_SYMBOL_GPL(aes_ecb_encrypt);

void aes_ecb_decrypt(u8 *dst, const u8 *src, size_t len,
		     const struct aes_key *key)
{
	if (WARN_ON_ONCE(len % AES_BLOCK_SIZE))
		len = round_down(len, AES_BLOCK_SIZE);

	if (unlikely(len == 0))
		return;

	if (likely(aes_ecb_decrypt_arch(dst, src, len, key)))
		return;

	for (size_t i = 0; i < len; i += AES_BLOCK_SIZE)
		aes_decrypt(key, &dst[i], &src[i]);
}
EXPORT_SYMBOL_GPL(aes_ecb_decrypt);

/* FIPS cryptographic algorithm self-test for AES-ECB */
static void __init aes_ecb_fips_test(void)
{
	struct aes_key key;
	u8 data[sizeof(fips_test_data)];

	if (aes_preparekey(&key, fips_test_key, sizeof(fips_test_key)) != 0)
		panic("aes: ECB FIPS self-test failed (preparekey)\n");

	aes_ecb_encrypt(data, fips_test_data, sizeof(data), &key);
	if (memcmp(fips_test_aes_ecb_ctext, data, sizeof(data)) != 0)
		panic("aes: ECB FIPS self-test failed (wrong ciphertext)\n");

	aes_ecb_decrypt(data, data, sizeof(data), &key);
	if (memcmp(fips_test_data, data, sizeof(data)) != 0)
		panic("aes: ECB FIPS self-test failed (wrong plaintext)\n");

	memzero_explicit(&key, sizeof(key));
}
#else /* CONFIG_CRYPTO_LIB_AES_ECB */
static inline void aes_ecb_fips_test(void)
{
}
#endif /* !CONFIG_CRYPTO_LIB_AES_ECB */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_CBC)
/*
 * Hooks for optimized AES-CBC implementations, overridable by the architecture.
 * They are called with len > 0 && len % AES_BLOCK_SIZE == 0.  Returning false
 * causes the fallback implementation to be used instead.
 */
#ifndef aes_cbc_encrypt_arch
static bool aes_cbc_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 iv[AES_BLOCK_SIZE],
				 const struct aes_enckey *key)
{
	return false;
}
#endif
#ifndef aes_cbc_decrypt_arch
static bool aes_cbc_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 iv[AES_BLOCK_SIZE],
				 const struct aes_key *key)
{
	return false;
}
#endif

void aes_cbc_encrypt(u8 *dst, const u8 *src, size_t len, u8 iv[AES_BLOCK_SIZE],
		     aes_encrypt_arg key)
{
	const u8 *prev = iv;

	if (WARN_ON_ONCE(len % AES_BLOCK_SIZE))
		len = round_down(len, AES_BLOCK_SIZE);

	if (unlikely(len == 0))
		return;

	if (likely(aes_cbc_encrypt_arch(dst, src, len, iv, key.enc_key)))
		return;

	do {
		crypto_xor_cpy(dst, src, prev, AES_BLOCK_SIZE);
		aes_encrypt(key, dst, dst);
		prev = dst;
		dst += AES_BLOCK_SIZE;
		src += AES_BLOCK_SIZE;
		len -= AES_BLOCK_SIZE;
	} while (len);
	memcpy(iv, prev, AES_BLOCK_SIZE);
}
EXPORT_SYMBOL_GPL(aes_cbc_encrypt);

void aes_cbc_decrypt(u8 *dst, const u8 *src, size_t len, u8 iv[AES_BLOCK_SIZE],
		     const struct aes_key *key)
{
	u8 next_iv[AES_BLOCK_SIZE];

	if (WARN_ON_ONCE(len % AES_BLOCK_SIZE))
		len = round_down(len, AES_BLOCK_SIZE);

	if (unlikely(len == 0))
		return;

	if (likely(aes_cbc_decrypt_arch(dst, src, len, iv, key)))
		return;

	len -= AES_BLOCK_SIZE;
	dst += len;
	src += len;
	memcpy(next_iv, src, AES_BLOCK_SIZE);
	for (;;) {
		aes_decrypt(key, dst, src);
		if (len == 0)
			break;
		src -= AES_BLOCK_SIZE;
		crypto_xor(dst, src, AES_BLOCK_SIZE);
		dst -= AES_BLOCK_SIZE;
		len -= AES_BLOCK_SIZE;
	}
	crypto_xor(dst, iv, AES_BLOCK_SIZE);
	memcpy(iv, next_iv, AES_BLOCK_SIZE);
}
EXPORT_SYMBOL_GPL(aes_cbc_decrypt);

/*
 * Hooks for optimized AES-CBC-CTS implementations, overridable by the
 * architecture.  They are called with len > AES_BLOCK_SIZE.  Returning false
 * causes the fallback implementation to be used instead.  The fallback
 * implementation still uses the arch-optimized AES-CBC code if available, but
 * direct implementation of AES-CBC-CTS is helpful on short messages.
 */
#ifndef aes_cbc_cts_encrypt_arch
static bool aes_cbc_cts_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				     u8 iv[AES_BLOCK_SIZE],
				     const struct aes_enckey *key)
{
	return false;
}
#endif
#ifndef aes_cbc_cts_decrypt_arch
static bool aes_cbc_cts_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				     u8 iv[AES_BLOCK_SIZE],
				     const struct aes_key *key)
{
	return false;
}
#endif

void aes_cbc_cts_encrypt(u8 *dst, const u8 *src, size_t len,
			 u8 iv[AES_BLOCK_SIZE], aes_encrypt_arg key)
{
	/* Offset to P[n] and C[n] (last plaintext and ciphertext block) */
	size_t pn_offset = round_down(len - 1, AES_BLOCK_SIZE);
	/* Length of P[n] and C[n], 1 <= pn_len <= AES_BLOCK_SIZE */
	size_t pn_len = len - pn_offset;
	u8 tmp[AES_BLOCK_SIZE] __aligned(__alignof__(long));
	u8 *pad;

	if (WARN_ON_ONCE(len < AES_BLOCK_SIZE))
		return;

	if (len == AES_BLOCK_SIZE) {
		aes_cbc_encrypt(dst, src, len, iv, key);
		return;
	}
	if (likely(aes_cbc_cts_encrypt_arch(dst, src, len, iv, key.enc_key)))
		return;

	/* CBC-encrypt all blocks except the last. */
	aes_cbc_encrypt(dst, src, pn_offset, iv, key);

	/*
	 * Compute C[n] and C[n - 1].
	 *
	 * Careful: src may equal dst (i.e., the encryption can be in-place), so
	 * src[pn_offset..] can't be read after dst[pn_offset..] is written.
	 */
	pad = &dst[pn_offset - AES_BLOCK_SIZE];
	memcpy(tmp, pad, AES_BLOCK_SIZE);
	crypto_xor(tmp, &src[pn_offset], pn_len);
	memcpy(&dst[pn_offset], pad, pn_len); /* C[n] */
	aes_encrypt(key, pad, tmp); /* C[n - 1] */

	memzero_explicit(tmp, sizeof(tmp));
}
EXPORT_SYMBOL_GPL(aes_cbc_cts_encrypt);

void aes_cbc_cts_decrypt(u8 *dst, const u8 *src, size_t len,
			 u8 iv[AES_BLOCK_SIZE], const struct aes_key *key)
{
	/* Offset to P[n] and C[n] (last plaintext and ciphertext block) */
	size_t pn_offset = round_down(len - 1, AES_BLOCK_SIZE);
	/* Length of P[n] and C[n], 1 <= pn_len <= AES_BLOCK_SIZE */
	size_t pn_len = len - pn_offset;
	u8 *pad;

	if (WARN_ON_ONCE(len < AES_BLOCK_SIZE))
		return;

	if (len == AES_BLOCK_SIZE) {
		aes_cbc_decrypt(dst, src, len, iv, key);
		return;
	}
	if (likely(aes_cbc_cts_decrypt_arch(dst, src, len, iv, key)))
		return;

	/* Compute P[0]..P[n - 2]. */
	aes_cbc_decrypt(dst, src, pn_offset - AES_BLOCK_SIZE, iv, key);

	/*
	 * Compute P[n] and P[n - 1].
	 *
	 * Careful: src may equal dst (i.e., the decryption can be in-place), so
	 * src[pn_offset..] can't be read after dst[pn_offset..] is written.
	 *
	 * To avoid needing a temporary buffer, do a "redundant" XOR to recover
	 * src[pn_offset..] from dst[pn_offset..] after the latter is written.
	 */
	pad = &dst[pn_offset - AES_BLOCK_SIZE];
	aes_decrypt(key, pad, &src[pn_offset - AES_BLOCK_SIZE]);
	crypto_xor_cpy(&dst[pn_offset], &src[pn_offset], pad,
		       pn_len); /* P[n] */
	crypto_xor(pad, &dst[pn_offset], pn_len);
	aes_decrypt(key, pad, pad);
	crypto_xor(pad, iv, AES_BLOCK_SIZE); /* P[n - 1] */
}
EXPORT_SYMBOL_GPL(aes_cbc_cts_decrypt);

/* FIPS cryptographic algorithm self-test for AES-CBC */
static void __init aes_cbc_fips_test(void)
{
	struct aes_key key;
	u8 iv[AES_BLOCK_SIZE];
	u8 data[sizeof(fips_test_data)];

	if (aes_preparekey(&key, fips_test_key, sizeof(fips_test_key)) != 0)
		panic("aes: CBC FIPS self-test failed (preparekey)\n");

	memcpy(iv, fips_test_iv, sizeof(iv));
	aes_cbc_encrypt(data, fips_test_data, sizeof(data), iv, &key);
	if (memcmp(fips_test_aes_cbc_ctext, data, sizeof(data)) != 0)
		panic("aes: CBC FIPS self-test failed (wrong ciphertext)\n");

	memcpy(iv, fips_test_iv, sizeof(iv));
	aes_cbc_decrypt(data, data, sizeof(data), iv, &key);
	if (memcmp(fips_test_data, data, sizeof(data)) != 0)
		panic("aes: CBC FIPS self-test failed (wrong plaintext)\n");

	memzero_explicit(&key, sizeof(key));
}

/* FIPS cryptographic algorithm self-test for AES-CBC-CTS */
static void __init aes_cbc_cts_fips_test(void)
{
	struct aes_key key;
	u8 iv[AES_BLOCK_SIZE];
	const size_t data_len = 2 * AES_BLOCK_SIZE;
	u8 ptext[2 * AES_BLOCK_SIZE];
	u8 data[2 * AES_BLOCK_SIZE];

	/* ptext = fips_test_data || fips_test_data */
	memcpy(ptext, fips_test_data, AES_BLOCK_SIZE);
	memcpy(&ptext[AES_BLOCK_SIZE], ptext, AES_BLOCK_SIZE);

	if (aes_preparekey(&key, fips_test_key, sizeof(fips_test_key)) != 0)
		panic("aes: CBC-CTS FIPS self-test failed (preparekey)\n");

	memcpy(iv, fips_test_iv, sizeof(iv));
	aes_cbc_cts_encrypt(data, ptext, data_len, iv, &key);
	if (memcmp(fips_test_aes_cbc_cts_ctext, data, data_len) != 0)
		panic("aes: CBC-CTS FIPS self-test failed (wrong ciphertext)\n");

	memcpy(iv, fips_test_iv, sizeof(iv));
	aes_cbc_cts_decrypt(data, data, data_len, iv, &key);
	if (memcmp(ptext, data, data_len) != 0)
		panic("aes: CBC-CTS FIPS self-test failed (wrong plaintext)\n");

	memzero_explicit(&key, sizeof(key));
}
#else /* CONFIG_CRYPTO_LIB_AES_CBC */
static inline void aes_cbc_fips_test(void)
{
}
static inline void aes_cbc_cts_fips_test(void)
{
}
#endif /* !CONFIG_CRYPTO_LIB_AES_CBC */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_CTR)
/*
 * Hooks for optimized AES-CTR and AES-XCTR implementations, overridable by the
 * architecture.  They are called with any len >= 0.  Returning false causes the
 * fallback implementation to be used instead.
 */
#ifndef aes_ctr_arch
static bool aes_ctr_arch(u8 *dst, const u8 *src, size_t len,
			 u8 ctr[AES_BLOCK_SIZE], const struct aes_enckey *key)
{
	return false;
}
#endif
#ifndef aes_xctr_arch
static bool aes_xctr_arch(u8 *dst, const u8 *src, size_t len, u64 *ctr,
			  const u8 iv[AES_BLOCK_SIZE],
			  const struct aes_enckey *key)
{
	return false;
}
#endif

static __always_inline void inc_be128_ctr(u8 ctr[AES_BLOCK_SIZE])
{
	/*
	 * 255 times out of 256 the first iteration is enough, so unroll the
	 * first iteration as a micro-optimization.
	 */
	if ((++ctr[AES_BLOCK_SIZE - 1]) != 0)
		return;
	for (int i = AES_BLOCK_SIZE - 2; i >= 0; i--) {
		if (++ctr[i] != 0)
			break;
	}
}

void aes_ctr(u8 *dst, const u8 *src, size_t len, u8 ctr[AES_BLOCK_SIZE],
	     aes_encrypt_arg key)
{
	u8 keystream[AES_BLOCK_SIZE] __aligned(__alignof__(long));

	if (likely(aes_ctr_arch(dst, src, len, ctr, key.enc_key)))
		return;

	/* Handle the full blocks. */
	for (; len >= AES_BLOCK_SIZE; len -= AES_BLOCK_SIZE) {
		aes_encrypt(key, keystream, ctr);
		crypto_xor_cpy(dst, src, keystream, AES_BLOCK_SIZE);
		inc_be128_ctr(ctr);
		dst += AES_BLOCK_SIZE;
		src += AES_BLOCK_SIZE;
	}
	/* Handle any partial block at the end. */
	if (len) {
		aes_encrypt(key, keystream, ctr);
		crypto_xor_cpy(dst, src, keystream, len);
		/* Counter is incremented even with just a partial block. */
		inc_be128_ctr(ctr);
	}
	memzero_explicit(keystream, sizeof(keystream));
}
EXPORT_SYMBOL_GPL(aes_ctr);

void aes_xctr(u8 *dst, const u8 *src, size_t len, u64 *ctr,
	      const u8 iv[AES_BLOCK_SIZE], aes_encrypt_arg key)
{
	const __le64 iv0 = get_unaligned((const __le64 *)&iv[0]);
	__le64 aes_input[2];
	u8 keystream[AES_BLOCK_SIZE] __aligned(__alignof__(long));

	if (likely(aes_xctr_arch(dst, src, len, ctr, iv, key.enc_key)))
		return;

	aes_input[1] = get_unaligned((const __le64 *)&iv[8]);
	/* Handle the full blocks. */
	for (; len >= AES_BLOCK_SIZE; len -= AES_BLOCK_SIZE) {
		aes_input[0] = iv0 ^ cpu_to_le64((*ctr)++);
		aes_encrypt(key, keystream, (const u8 *)aes_input);
		crypto_xor_cpy(dst, src, keystream, AES_BLOCK_SIZE);
		dst += AES_BLOCK_SIZE;
		src += AES_BLOCK_SIZE;
	}
	/* Handle any partial block at the end. */
	if (len) {
		/* Counter is incremented even with just a partial block. */
		aes_input[0] = iv0 ^ cpu_to_le64((*ctr)++);
		aes_encrypt(key, keystream, (const u8 *)aes_input);
		crypto_xor_cpy(dst, src, keystream, len);
	}
	memzero_explicit(keystream, sizeof(keystream));
	memzero_explicit(aes_input, sizeof(aes_input));
}
EXPORT_SYMBOL_GPL(aes_xctr);

/* FIPS cryptographic algorithm self-test for AES-CTR */
static void __init aes_ctr_fips_test(void)
{
	struct aes_enckey key;
	u8 ctr[AES_BLOCK_SIZE];
	u8 data[sizeof(fips_test_data)];

	if (aes_prepareenckey(&key, fips_test_key, sizeof(fips_test_key)) != 0)
		panic("aes: CTR FIPS self-test failed (preparekey)\n");

	memcpy(ctr, fips_test_iv, sizeof(ctr));
	aes_ctr(data, fips_test_data, sizeof(data), ctr, &key);
	if (memcmp(fips_test_aes_ctr_ctext, data, sizeof(data)) != 0)
		panic("aes: CTR FIPS self-test failed (wrong ciphertext)\n");

	memcpy(ctr, fips_test_iv, sizeof(ctr));
	aes_ctr(data, data, sizeof(data), ctr, &key);
	if (memcmp(fips_test_data, data, sizeof(data)) != 0)
		panic("aes: CTR FIPS self-test failed (wrong plaintext)\n");

	memzero_explicit(&key, sizeof(key));
}
#else /* CONFIG_CRYPTO_LIB_AES_CTR */
static inline void aes_ctr_fips_test(void)
{
}
#endif /* !CONFIG_CRYPTO_LIB_AES_CTR */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_XTS)
int aes_xts_preparekey(struct aes_xts_key *key, const u8 *in_key,
		       size_t key_len, int flags)
{
	int err;

	err = __xts_verify_key(in_key, key_len, flags);
	if (unlikely(err))
		goto out_zeroize;
	/* First half of XTS key is the main key */
	err = aes_preparekey(&key->main_key, in_key, key_len / 2);
	if (unlikely(err))
		goto out_zeroize;
	/* Second half of XTS key is the tweak key */
	err = aes_prepareenckey(&key->tweak_key, &in_key[key_len / 2],
				key_len / 2);
	if (unlikely(err))
		goto out_zeroize;
	return 0;

out_zeroize:
	memzero_explicit(key, sizeof(*key));
	return err;
}
EXPORT_SYMBOL_GPL(aes_xts_preparekey);

/*
 * Hooks for optimized AES-XTS implementations, overridable by the architecture.
 * They are called with len > 0 && len % AES_BLOCK_SIZE == 0.  In other words,
 * they aren't expected to handle ciphertext stealing or empty inputs.
 * Returning false causes the fallback implementation to be used instead.
 *
 * (Currently, all users of AES-XTS in the kernel seem to en/decrypt whole
 * numbers of blocks anyway, with len >= 512.  So there's no need to heavily
 * optimize ciphertext stealing for short messages.)
 */
#ifndef aes_xts_encrypt_arch
static bool aes_xts_encrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 tweak[AES_BLOCK_SIZE],
				 const struct aes_xts_key *key, bool cont)
{
	return false;
}
#endif
#ifndef aes_xts_decrypt_arch
static bool aes_xts_decrypt_arch(u8 *dst, const u8 *src, size_t len,
				 u8 tweak[AES_BLOCK_SIZE],
				 const struct aes_xts_key *key, bool cont)
{
	return false;
}
#endif

static noinline void aes_xts_crypt_nocts_blockbyblock(
	u8 *dst, const u8 *src, size_t len, u8 tweak[AES_BLOCK_SIZE],
	const struct aes_xts_key *key, bool cont, bool enc)
{
	le128 t;

	if (cont)
		memcpy(&t, tweak, sizeof(t));
	else
		aes_encrypt(&key->tweak_key, (u8 *)&t, tweak);
	do {
		crypto_xor_cpy(dst, src, (const u8 *)&t, AES_BLOCK_SIZE);
		if (enc)
			aes_encrypt(&key->main_key, dst, dst);
		else
			aes_decrypt(&key->main_key, dst, dst);
		crypto_xor(dst, (const u8 *)&t, AES_BLOCK_SIZE);
		gf128mul_x_ble(&t, &t);
		dst += AES_BLOCK_SIZE;
		src += AES_BLOCK_SIZE;
		len -= AES_BLOCK_SIZE;
	} while (len);
	memcpy(tweak, &t, sizeof(t));
	memzero_explicit(&t, sizeof(t));
}

/* Requires len > 0 && len % AES_BLOCK_SIZE == 0 */
static __always_inline void aes_xts_encrypt_nocts(u8 *dst, const u8 *src,
						  size_t len,
						  u8 tweak[AES_BLOCK_SIZE],
						  const struct aes_xts_key *key,
						  bool cont)
{
	if (likely(aes_xts_encrypt_arch(dst, src, len, tweak, key, cont)))
		return;

	/*
	 * For the fallback, just go block-by-block.  It could be implemented on
	 * top of AES-ECB, which could be significantly faster than this if the
	 * arch has optimized AES-ECB code but not AES-XTS.  However, AES-XTS
	 * performance is important enough that it needs to be (and has been)
	 * implemented directly by every non-obsolete arch anyway.
	 */
	aes_xts_crypt_nocts_blockbyblock(dst, src, len, tweak, key, cont,
					 /* enc= */ true);
}

/* Requires len > 0 && len % AES_BLOCK_SIZE == 0 */
static __always_inline void aes_xts_decrypt_nocts(u8 *dst, const u8 *src,
						  size_t len,
						  u8 tweak[AES_BLOCK_SIZE],
						  const struct aes_xts_key *key,
						  bool cont)
{
	if (likely(aes_xts_decrypt_arch(dst, src, len, tweak, key, cont)))
		return;

	/* Just go block-by-block.  See comment in aes_xts_encrypt_nocts(). */
	aes_xts_crypt_nocts_blockbyblock(dst, src, len, tweak, key, cont,
					 /* enc= */ false);
}

static noinline void aes_xts_encrypt_cts(u8 *dst, const u8 *src, size_t len,
					 u8 tweak[AES_BLOCK_SIZE],
					 const struct aes_xts_key *key,
					 bool cont)
{
	size_t partial_len = len % AES_BLOCK_SIZE; /* Length of partial block */
	size_t nocts_len = round_down(len, AES_BLOCK_SIZE);
	u8 tmp_block[AES_BLOCK_SIZE] __aligned(__alignof__(long));

	/* Encrypt all full blocks. */
	aes_xts_encrypt_nocts(dst, src, nocts_len, tweak, key, cont);
	dst += nocts_len - AES_BLOCK_SIZE;
	src += nocts_len - AES_BLOCK_SIZE;

	/*
	 * Swap the partial block with the first 'partial_len' bytes of the
	 * encrypted last full block.  Note that a temporary buffer is needed to
	 * support in-place encryption.
	 */
	memcpy(tmp_block, src + AES_BLOCK_SIZE, partial_len);
	memcpy(dst + AES_BLOCK_SIZE, dst, partial_len);
	memcpy(dst, tmp_block, partial_len);

	/* Encrypt the last full block again. */
	crypto_xor(dst, tweak, AES_BLOCK_SIZE);
	aes_encrypt(&key->main_key, dst, dst);
	crypto_xor(dst, tweak, AES_BLOCK_SIZE);
	memzero_explicit(tmp_block, sizeof(tmp_block));
}

static noinline void aes_xts_decrypt_cts(u8 *dst, const u8 *src, size_t len,
					 u8 tweak[AES_BLOCK_SIZE],
					 const struct aes_xts_key *key,
					 bool cont)
{
	size_t partial_len = len % AES_BLOCK_SIZE; /* Length of partial block */
	size_t nocts_len = round_down(len, AES_BLOCK_SIZE) - AES_BLOCK_SIZE;
	union {
		u8 block[AES_BLOCK_SIZE];
		le128 tweak;
	} tmp __aligned(__alignof__(long));

	/*
	 * Decrypt all blocks except the last full block and the partial block.
	 * The last full block has to be handled specially because decryption
	 * ciphertext stealing uses the last two tweaks in reverse order.
	 *
	 * nocts_len == 0 is possible here, which aes_xts_decrypt_nocts()
	 * doesn't handle (so that the length doesn't get checked redundantly in
	 * the fast path).  So handle that case specially as well.
	 */
	if (nocts_len)
		aes_xts_decrypt_nocts(dst, src, nocts_len, tweak, key, cont);
	else if (!cont)
		aes_encrypt(&key->tweak_key, tweak, tweak);
	dst += nocts_len;
	src += nocts_len;

	/* Copy the tweak, advance it again, then decrypt last full block. */
	memcpy(&tmp.tweak, tweak, AES_BLOCK_SIZE);
	gf128mul_x_ble(&tmp.tweak, &tmp.tweak);
	crypto_xor_cpy(dst, src, tmp.block, AES_BLOCK_SIZE);
	aes_decrypt(&key->main_key, dst, dst);
	crypto_xor(dst, tmp.block, AES_BLOCK_SIZE);

	/*
	 * Swap the partial block with the first 'partial_len' bytes of the
	 * decrypted last full block.  Note that a temporary buffer is needed to
	 * support in-place decryption.
	 */
	memcpy(tmp.block, src + AES_BLOCK_SIZE, partial_len);
	memcpy(dst + AES_BLOCK_SIZE, dst, partial_len);
	memcpy(dst, tmp.block, partial_len);

	/* Decrypt the last full block again. */
	crypto_xor(dst, tweak, AES_BLOCK_SIZE);
	aes_decrypt(&key->main_key, dst, dst);
	crypto_xor(dst, tweak, AES_BLOCK_SIZE);
	memzero_explicit(&tmp, sizeof(tmp));
}

void aes_xts_encrypt(u8 *dst, const u8 *src, size_t len,
		     u8 tweak[AES_BLOCK_SIZE], const struct aes_xts_key *key,
		     bool cont)
{
	if (WARN_ON_ONCE(len < AES_BLOCK_SIZE))
		return;

	if (unlikely(len % AES_BLOCK_SIZE)) {
		aes_xts_encrypt_cts(dst, src, len, tweak, key, cont);
		return;
	}

	aes_xts_encrypt_nocts(dst, src, len, tweak, key, cont);
}
EXPORT_SYMBOL_GPL(aes_xts_encrypt);

void aes_xts_decrypt(u8 *dst, const u8 *src, size_t len,
		     u8 tweak[AES_BLOCK_SIZE], const struct aes_xts_key *key,
		     bool cont)
{
	if (WARN_ON_ONCE(len < AES_BLOCK_SIZE))
		return;

	if (unlikely(len % AES_BLOCK_SIZE)) {
		aes_xts_decrypt_cts(dst, src, len, tweak, key, cont);
		return;
	}

	aes_xts_decrypt_nocts(dst, src, len, tweak, key, cont);
}
EXPORT_SYMBOL_GPL(aes_xts_decrypt);

/* FIPS cryptographic algorithm self-test for AES-XTS */
static void __init aes_xts_fips_test(void)
{
	struct aes_xts_key *key __free(kfree_sensitive) = kmalloc_obj(*key);
	u8 tweak[AES_BLOCK_SIZE];
	u8 data[sizeof(fips_test_data)];

	if (key == NULL)
		panic("aes: XTS FIPS self-test failed (kmalloc)\n");

	if (aes_xts_preparekey(key, fips_test_xts_key,
			       sizeof(fips_test_xts_key), 0) != 0)
		panic("aes: XTS FIPS self-test failed (preparekey)\n");

	memcpy(tweak, fips_test_iv, sizeof(tweak));
	aes_xts_encrypt(data, fips_test_data, sizeof(data), tweak, key, false);
	if (memcmp(fips_test_aes_xts_ctext, data, sizeof(data)) != 0)
		panic("aes: XTS FIPS self-test failed (wrong ciphertext)\n");

	memcpy(tweak, fips_test_iv, sizeof(tweak));
	aes_xts_decrypt(data, data, sizeof(data), tweak, key, false);
	if (memcmp(fips_test_data, data, sizeof(data)) != 0)
		panic("aes: XTS FIPS self-test failed (wrong plaintext)\n");
}
#else /* CONFIG_CRYPTO_LIB_AES_XTS */
static inline void aes_xts_fips_test(void)
{
}
#endif /* !CONFIG_CRYPTO_LIB_AES_XTS */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_GCM)
/*
 * Hooks for optimized AES-GCM implementations, overridable by the architecture.
 * They are called with len > 0 && len % AES_BLOCK_SIZE == 0.  I.e. they aren't
 * expected to handle empty inputs or partial blocks, as those cases are handled
 * by non-arch-specific code instead.
 *
 * The GHASH accumulator is provided in POLYVAL format.  The counter is provided
 * in big endian format, and it's read-only, as the caller handles updating it.
 *
 * Returning false causes the fallback implementation to be used instead.
 *
 * These hooks are used only for en/decrypted data.  For the associated data the
 * GHASH functions are called instead, so those should be implemented too.
 */
#ifndef aes_gcm_encrypt_update_arch
static bool aes_gcm_encrypt_update_arch(u8 *dst, const u8 *src, size_t len,
					struct polyval_elem *ghash_acc,
					const __be32 ctr32[4],
					const struct aes_enckey *aes_key,
					const struct ghash_key *ghash_key)
{
	return false;
}
#endif
#ifndef aes_gcm_decrypt_update_arch
static bool aes_gcm_decrypt_update_arch(u8 *dst, const u8 *src, size_t len,
					struct polyval_elem *ghash_acc,
					const __be32 ctr32[4],
					const struct aes_enckey *aes_key,
					const struct ghash_key *ghash_key)
{
	return false;
}
#endif

int aes_gcm_preparekey(struct aes_gcm_key *key, const u8 *in_key,
		       size_t key_len, size_t authtag_len)
{
	u8 h[AES_BLOCK_SIZE] = { 0 };
	int err;

	err = crypto_gcm_check_authsize(authtag_len);
	if (unlikely(err))
		return err;

	err = aes_prepareenckey(&key->aes, in_key, key_len);
	if (unlikely(err))
		return err;

	aes_encrypt(&key->aes, h, h);
	ghash_preparekey(&key->ghash, h);

	key->authtag_len = authtag_len;

	memzero_explicit(h, sizeof(h));
	return 0;
}
EXPORT_SYMBOL_GPL(aes_gcm_preparekey);

void aes_gcm_init(struct aes_gcm_ctx *ctx, const u8 nonce[12],
		  const struct aes_gcm_key *key)
{
	ctx->key = key;
	ctx->ad_len = 0;
	ctx->data_len = 0;
	ghash_init(&ctx->ghash, &key->ghash);
	memset(ctx->keystream, 0, sizeof(ctx->keystream));

	memcpy(ctx->ctr32, nonce, 12);
	ctx->ctr32[3] = cpu_to_be32(1);

	aes_encrypt(&key->aes, ctx->j0_enc, ctx->ctr);
	ctx->ctr32[3] = cpu_to_be32(2);
}
EXPORT_SYMBOL_GPL(aes_gcm_init);

void aes_gcm_auth_update(struct aes_gcm_ctx *ctx, const u8 *ad, size_t len)
{
	WARN_ON_ONCE(ctx->data_len != 0);
	if (len) {
		ghash_update(&ctx->ghash, ad, len);
		ctx->ad_len += len;
	}
}
EXPORT_SYMBOL_GPL(aes_gcm_auth_update);

static const u8 gcm_zeroes[AES_BLOCK_SIZE];

static __always_inline void ghash_pad(struct ghash_ctx *ghash, u64 len)
{
	if (len % AES_BLOCK_SIZE)
		ghash_update(ghash, gcm_zeroes, -len % AES_BLOCK_SIZE);
}

static __always_inline void aes_gcm_crypt_update(struct aes_gcm_ctx *ctx,
						 u8 *dst, const u8 *src,
						 size_t len, bool enc)
{
	size_t partial_len, n;

	if (unlikely(len == 0))
		return;

	partial_len = ctx->data_len % AES_BLOCK_SIZE;
	if (ctx->data_len == 0)
		ghash_pad(&ctx->ghash, ctx->ad_len);
	ctx->data_len += len;

	if (unlikely(partial_len != 0)) {
		/*
		 * The previous call ended on a non-block-aligned data_len, so
		 * continue using a previously-generated keystream block.
		 */
		n = min(len, AES_BLOCK_SIZE - partial_len);
		if (enc) {
			crypto_xor_cpy(dst, src, &ctx->keystream[partial_len],
				       n);
			ghash_update(&ctx->ghash, dst, n);
		} else {
			ghash_update(&ctx->ghash, src, n);
			crypto_xor_cpy(dst, src, &ctx->keystream[partial_len],
				       n);
		}
		dst += n;
		src += n;
		len -= n;
	}

	if (len >= AES_BLOCK_SIZE) {
		n = round_down(len, AES_BLOCK_SIZE);
		if (enc) {
			if (likely(aes_gcm_encrypt_update_arch(
				    dst, src, n, &ctx->ghash.acc, ctx->ctr32,
				    &ctx->key->aes, &ctx->key->ghash))) {
				be32_add_cpu(&ctx->ctr32[3],
					     n / AES_BLOCK_SIZE);
			} else {
				aes_ctr(dst, src, n, ctx->ctr, &ctx->key->aes);
				ghash_update(&ctx->ghash, dst, n);
			}
		} else {
			if (likely(aes_gcm_decrypt_update_arch(
				    dst, src, n, &ctx->ghash.acc, ctx->ctr32,
				    &ctx->key->aes, &ctx->key->ghash))) {
				be32_add_cpu(&ctx->ctr32[3],
					     n / AES_BLOCK_SIZE);
			} else {
				ghash_update(&ctx->ghash, src, n);
				aes_ctr(dst, src, n, ctx->ctr, &ctx->key->aes);
			}
		}
		dst += n;
		src += n;
		len -= n;
	}

	if (len != 0) {
		/*
		 * Ending on a non-block aligned data_len.  Generate the next
		 * keystream block, use the needed portion of it, and leave it
		 * cached in ctx->keystream in case this isn't the final call.
		 */
		aes_encrypt(&ctx->key->aes, ctx->keystream, ctx->ctr);
		be32_add_cpu(&ctx->ctr32[3], 1);
		if (enc) {
			crypto_xor_cpy(dst, src, ctx->keystream, len);
			ghash_update(&ctx->ghash, dst, len);
		} else {
			ghash_update(&ctx->ghash, src, len);
			crypto_xor_cpy(dst, src, ctx->keystream, len);
		}
	}
}

void aes_gcm_encrypt_update(struct aes_gcm_ctx *ctx, u8 *dst, const u8 *src,
			    size_t len)
{
	aes_gcm_crypt_update(ctx, dst, src, len, /* enc= */ true);
}
EXPORT_SYMBOL_GPL(aes_gcm_encrypt_update);

void aes_gcm_decrypt_update(struct aes_gcm_ctx *ctx, u8 *dst, const u8 *src,
			    size_t len)
{
	aes_gcm_crypt_update(ctx, dst, src, len, /* enc= */ false);
}
EXPORT_SYMBOL_GPL(aes_gcm_decrypt_update);

/* Maximum AES-GCM associated data length in bytes */
#define AES_GCM_MAX_AD_LEN ((1ULL << 61) - 1)
/* Maximum AES-GCM en/decrypted data length in bytes */
#define AES_GCM_MAX_DATA_LEN ((1ULL << 36) - 32)

void aes_gcm_encrypt_final(struct aes_gcm_ctx *ctx, u8 *authtag)
{
	__be64 tail[2];

	WARN_ON_ONCE(ctx->ad_len > AES_GCM_MAX_AD_LEN);
	WARN_ON_ONCE(ctx->data_len > AES_GCM_MAX_DATA_LEN);

	ghash_pad(&ctx->ghash,
		  ctx->data_len == 0 ? ctx->ad_len : ctx->data_len);

	tail[0] = cpu_to_be64(ctx->ad_len * 8);
	tail[1] = cpu_to_be64(ctx->data_len * 8);
	ghash_update(&ctx->ghash, (const u8 *)tail, 16);
	ghash_final(&ctx->ghash, ctx->ctr); /* Use ctr as temp buffer */

	crypto_xor_cpy(authtag, ctx->ctr, ctx->j0_enc, ctx->key->authtag_len);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_GPL(aes_gcm_encrypt_final);

int aes_gcm_decrypt_final(struct aes_gcm_ctx *ctx, const u8 *authtag)
{
	__be64 tail[2];
	int err;

	if (WARN_ON_ONCE(ctx->ad_len > AES_GCM_MAX_AD_LEN) ||
	    WARN_ON_ONCE(ctx->data_len > AES_GCM_MAX_DATA_LEN)) {
		err = -EBADMSG;
		goto out;
	}

	ghash_pad(&ctx->ghash,
		  ctx->data_len == 0 ? ctx->ad_len : ctx->data_len);

	tail[0] = cpu_to_be64(ctx->ad_len * 8);
	tail[1] = cpu_to_be64(ctx->data_len * 8);
	ghash_update(&ctx->ghash, (const u8 *)tail, 16);
	ghash_final(&ctx->ghash, ctx->ctr); /* Use ctr as temp buffer */
	crypto_xor(ctx->ctr, ctx->j0_enc, ctx->key->authtag_len);
	err = crypto_memneq(ctx->ctr, authtag, ctx->key->authtag_len) ?
		      -EBADMSG :
		      0;
out:
	memzero_explicit(ctx, sizeof(*ctx));
	return err;
}
EXPORT_SYMBOL_GPL(aes_gcm_decrypt_final);

void aes_gcm_encrypt(u8 *dst, const u8 *src, size_t data_len, u8 *authtag,
		     const u8 *ad, size_t ad_len, const u8 nonce[12],
		     const struct aes_gcm_key *key)
{
	struct aes_gcm_ctx ctx;

	aes_gcm_init(&ctx, nonce, key);
	aes_gcm_auth_update(&ctx, ad, ad_len);
	aes_gcm_encrypt_update(&ctx, dst, src, data_len);
	aes_gcm_encrypt_final(&ctx, authtag);
}
EXPORT_SYMBOL_GPL(aes_gcm_encrypt);

int aes_gcm_decrypt(u8 *dst, const u8 *src, size_t data_len, const u8 *authtag,
		    const u8 *ad, size_t ad_len, const u8 nonce[12],
		    const struct aes_gcm_key *key)
{
	struct aes_gcm_ctx ctx;
	int err;

	aes_gcm_init(&ctx, nonce, key);
	aes_gcm_auth_update(&ctx, ad, ad_len);
	aes_gcm_decrypt_update(&ctx, dst, src, data_len);
	err = aes_gcm_decrypt_final(&ctx, authtag);
	if (unlikely(err) && data_len) {
		/*
		 * Clear the inauthentic decrypted data so that callers won't
		 * receive it even if they fail to correctly handle errors.
		 */
		memset(dst, 0, data_len);
	}
	return err;
}
EXPORT_SYMBOL_GPL(aes_gcm_decrypt);

/* FIPS cryptographic algorithm self-test for AES-GCM */
static void __init aes_gcm_fips_test(void)
{
	const size_t data_len = sizeof(fips_test_data);
	u8 buf[sizeof(fips_test_data) + AES_BLOCK_SIZE];
	struct aes_gcm_key key;
	int err;

	if (aes_gcm_preparekey(&key, fips_test_key, sizeof(fips_test_key),
			       AES_BLOCK_SIZE) != 0)
		panic("aes: GCM FIPS self-test failed (preparekey)\n");

	aes_gcm_encrypt(buf, fips_test_data, data_len, &buf[data_len],
			fips_test_ad, sizeof(fips_test_ad), fips_test_iv, &key);
	if (memcmp(fips_test_aes_gcm_ctext_and_tag, buf, sizeof(buf)) != 0)
		panic("aes: GCM FIPS self-test failed (wrong ciphertext and/or tag)\n");

	err = aes_gcm_decrypt(buf, buf, data_len, &buf[data_len], fips_test_ad,
			      sizeof(fips_test_ad), fips_test_iv, &key);
	if (err != 0)
		panic("aes: GCM FIPS self-test failed (decryption failed)\n");
	if (memcmp(fips_test_data, buf, data_len) != 0)
		panic("aes: GCM FIPS self-test failed (wrong plaintext)\n");

	memzero_explicit(&key, sizeof(key));
}
#else /* CONFIG_CRYPTO_LIB_AES_GCM */
static inline void aes_gcm_fips_test(void)
{
}
#endif /* !CONFIG_CRYPTO_LIB_AES_GCM */

#if IS_ENABLED(CONFIG_CRYPTO_LIB_AES_CCM)
int aes_ccm_preparekey(struct aes_ccm_key *key, const u8 *in_key,
		       size_t key_len, size_t authtag_len)
{
	int err;

	if (unlikely(authtag_len < 4 || authtag_len > 16 || authtag_len % 2))
		return -EINVAL;

	err = aes_prepareenckey(&key->aes, in_key, key_len);
	if (unlikely(err))
		return err;

	key->authtag_len = authtag_len;
	return 0;
}
EXPORT_SYMBOL_GPL(aes_ccm_preparekey);

int aes_ccm_init(struct aes_ccm_ctx *ctx, u64 data_len, u64 ad_len,
		 const u8 *nonce, size_t nonce_len,
		 const struct aes_ccm_key *key)
{
	/*
	 * This is the value L defined in the CCM specification.  It determines
	 * the maximum allowed message length, and it is itself determined by
	 * the nonce length.  They are inversely related, i.e. the longer the
	 * nonce the smaller the maximum message length is.
	 */
	unsigned int l = 15 - nonce_len;

	if (unlikely(nonce_len < 7 || nonce_len > 13))
		return -EINVAL;
	/* Thus 2 <= l <= 8. */

	/* Check whether data_len can be represented in 'l' bytes. */
	if (unlikely(data_len > U64_MAX >> (64 - 8 * l)))
		return -EOVERFLOW;

	ctx->key = key;
	ctx->ad_remaining = ad_len;
	ctx->data_remaining = data_len;
	ctx->ad_padded = false;

	/*
	 * Initialize the zero-th counter block to:
	 *
	 *	L - 1 || nonce || 0
	 *
	 * ... and the zero-th CBC-MAC block to:
	 *
	 *	Flags || nonce || data_len
	 */
	*(__be64 *)&ctx->ctr[8] = 0;
	*(__be64 *)&ctx->mac[8] = cpu_to_be64(data_len);
	ctx->ctr[0] = l - 1;
	ctx->mac[0] = (ad_len ? 0x40 : 0) |
		      (((key->authtag_len - 2) / 2) << 3) | (l - 1);
	memcpy(&ctx->ctr[1], nonce, nonce_len); /* Overlapping store */
	memcpy(&ctx->mac[1], nonce, nonce_len); /* Overlapping store */

	/*
	 * Generate S_0 by encrypting the counter (this is used to encrypt the
	 * auth tag later), and encrypt the zero-th CBC-MAC block.
	 */
	aes_encrypt(&key->aes, ctx->s0, ctx->ctr);
	aes_encrypt(&key->aes, ctx->mac, ctx->mac);

	/* Increment the counter from 0 to 1. */
	ctx->ctr[15] = 1;

	if (ad_len) {
		/*
		 * Update CBC-MAC with the associated data length, represented
		 * using either 2, 6, or 10 bytes depending on the length.
		 */
		if (likely(ad_len < 0xff00)) {
			*(__be16 *)&ctx->mac[0] ^= cpu_to_be16(ad_len);
			ctx->partial_len = 2;
		} else if (ad_len <= U32_MAX) {
			__be32 *p = (__be32 *)&ctx->mac[2];

			*(__be16 *)&ctx->mac[0] ^= cpu_to_be16(0xfffe);
			put_unaligned(get_unaligned(p) ^ cpu_to_be32(ad_len),
				      p);
			ctx->partial_len = 6;
		} else {
			__be64 *p = (__be64 *)&ctx->mac[2];

			*(__be16 *)&ctx->mac[0] ^= cpu_to_be16(0xffff);
			put_unaligned(get_unaligned(p) ^ cpu_to_be64(ad_len),
				      p);
			ctx->partial_len = 10;
		}
	} else {
		ctx->partial_len = 0;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(aes_ccm_init);

void aes_ccm_auth_update(struct aes_ccm_ctx *ctx, const u8 *ad, size_t len)
{
	size_t partial_len = ctx->partial_len;
	bool enc_before = false;
	size_t nblocks;

	WARN_ON_ONCE(ctx->ad_padded);

	/*
	 * We could warn on len > ad_remaining here, but underflow will be
	 * caught by the != 0 check at the end anyway.  (It's a u64, so it isn't
	 * going to underflow all the way back to 0.)
	 */
	ctx->ad_remaining -= len;

	if (partial_len) {
		size_t n = min(len, AES_BLOCK_SIZE - partial_len);

		crypto_xor(&ctx->mac[partial_len], ad, n);
		ad += n;
		len -= n;
		partial_len += n;
		if (partial_len < AES_BLOCK_SIZE) {
			ctx->partial_len = partial_len;
			return;
		}
		enc_before = true;
	}

	nblocks = len / AES_BLOCK_SIZE;
	len %= AES_BLOCK_SIZE;
	if (nblocks == 0) {
		if (enc_before)
			aes_encrypt(&ctx->key->aes, ctx->mac, ctx->mac);
	} else {
		aes_cbcmac_blocks(ctx->mac, &ctx->key->aes, ad, nblocks,
				  enc_before, /* enc_after= */ true);
		ad += nblocks * AES_BLOCK_SIZE;
	}
	crypto_xor(ctx->mac, ad, len);
	ctx->partial_len = len;
}
EXPORT_SYMBOL_GPL(aes_ccm_auth_update);

static __always_inline void aes_ccm_crypt_update(struct aes_ccm_ctx *ctx,
						 u8 *dst, const u8 *src,
						 size_t len, bool enc)
{
	size_t partial_len = ctx->partial_len;
	size_t n, nblocks;

	if (unlikely(len == 0))
		return;

	WARN_ON_ONCE(ctx->ad_remaining != 0);

	/*
	 * We could warn on len > data_remaining here, but underflow will be
	 * caught by the != 0 check at the end anyway.  (It's a u64, so it isn't
	 * going to underflow all the way back to 0.)
	 */
	ctx->data_remaining -= len;

	if (!ctx->ad_padded) {
		ctx->ad_padded = true;
		if (partial_len)
			aes_encrypt(&ctx->key->aes, ctx->mac, ctx->mac);
	} else if (partial_len) {
		/*
		 * The previous call ended on a non-block-aligned data_len, so
		 * continue using a previously-generated keystream block.
		 */
		n = min(len, AES_BLOCK_SIZE - partial_len);
		if (enc)
			crypto_xor(&ctx->mac[partial_len], src, n);
		crypto_xor_cpy(dst, src, &ctx->keystream[partial_len], n);
		if (!enc)
			crypto_xor(&ctx->mac[partial_len], dst, n);
		dst += n;
		src += n;
		len -= n;
		partial_len += n;
		if (partial_len < AES_BLOCK_SIZE) {
			ctx->partial_len = partial_len;
			return;
		}
		aes_encrypt(&ctx->key->aes, ctx->mac, ctx->mac);
	}

	if (len >= AES_BLOCK_SIZE) {
		n = round_down(len, AES_BLOCK_SIZE);
		nblocks = len / AES_BLOCK_SIZE;
		if (enc)
			aes_cbcmac_blocks(ctx->mac, &ctx->key->aes, src,
					  nblocks, /* enc_before= */ false,
					  /* enc_after= */ true);
		aes_ctr(dst, src, n, ctx->ctr, &ctx->key->aes);
		if (!enc)
			aes_cbcmac_blocks(ctx->mac, &ctx->key->aes, dst,
					  nblocks, /* enc_before= */ false,
					  /* enc_after= */ true);
		dst += n;
		src += n;
		len -= n;
	}

	if (len) {
		/*
		 * Ending on a non-block aligned data_len.  Generate the next
		 * keystream block, use the needed portion of it, and leave it
		 * cached in ctx->keystream in case this isn't the final call.
		 */
		aes_encrypt(&ctx->key->aes, ctx->keystream, ctx->ctr);
		inc_be128_ctr(ctx->ctr);
		if (enc)
			crypto_xor(ctx->mac, src, len);
		crypto_xor_cpy(dst, src, ctx->keystream, len);
		if (!enc)
			crypto_xor(ctx->mac, dst, len);
	}
	ctx->partial_len = len;
}

void aes_ccm_encrypt_update(struct aes_ccm_ctx *ctx, u8 *dst, const u8 *src,
			    size_t len)
{
	aes_ccm_crypt_update(ctx, dst, src, len, /* enc= */ true);
}
EXPORT_SYMBOL_GPL(aes_ccm_encrypt_update);

void aes_ccm_decrypt_update(struct aes_ccm_ctx *ctx, u8 *dst, const u8 *src,
			    size_t len)
{
	aes_ccm_crypt_update(ctx, dst, src, len, /* enc= */ false);
}
EXPORT_SYMBOL_GPL(aes_ccm_decrypt_update);

void aes_ccm_encrypt_final(struct aes_ccm_ctx *ctx, u8 *authtag)
{
	WARN_ON_ONCE(ctx->ad_remaining != 0);
	WARN_ON_ONCE(ctx->data_remaining != 0);
	if (ctx->partial_len)
		aes_encrypt(&ctx->key->aes, ctx->mac, ctx->mac);
	crypto_xor_cpy(authtag, ctx->mac, ctx->s0, ctx->key->authtag_len);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_GPL(aes_ccm_encrypt_final);

int aes_ccm_decrypt_final(struct aes_ccm_ctx *ctx, const u8 *authtag)
{
	int err;

	if (WARN_ON_ONCE(ctx->ad_remaining != 0) ||
	    WARN_ON_ONCE(ctx->data_remaining != 0)) {
		err = -EBADMSG;
		goto out;
	}

	if (ctx->partial_len)
		aes_encrypt(&ctx->key->aes, ctx->mac, ctx->mac);
	crypto_xor(ctx->mac, ctx->s0, ctx->key->authtag_len);
	err = crypto_memneq(ctx->mac, authtag, ctx->key->authtag_len) ?
		      -EBADMSG :
		      0;
out:
	memzero_explicit(ctx, sizeof(*ctx));
	return err;
}
EXPORT_SYMBOL_GPL(aes_ccm_decrypt_final);

int aes_ccm_encrypt(u8 *dst, const u8 *src, size_t data_len, u8 *authtag,
		    const u8 *ad, size_t ad_len, const u8 *nonce,
		    size_t nonce_len, const struct aes_ccm_key *key)
{
	struct aes_ccm_ctx ctx;
	int err;

	err = aes_ccm_init(&ctx, data_len, ad_len, nonce, nonce_len, key);
	if (unlikely(err))
		return err;
	aes_ccm_auth_update(&ctx, ad, ad_len);
	aes_ccm_encrypt_update(&ctx, dst, src, data_len);
	aes_ccm_encrypt_final(&ctx, authtag);
	return 0;
}
EXPORT_SYMBOL_GPL(aes_ccm_encrypt);

int aes_ccm_decrypt(u8 *dst, const u8 *src, size_t data_len, const u8 *authtag,
		    const u8 *ad, size_t ad_len, const u8 *nonce,
		    size_t nonce_len, const struct aes_ccm_key *key)
{
	struct aes_ccm_ctx ctx;
	int err;

	err = aes_ccm_init(&ctx, data_len, ad_len, nonce, nonce_len, key);
	if (unlikely(err))
		return err;
	aes_ccm_auth_update(&ctx, ad, ad_len);
	aes_ccm_decrypt_update(&ctx, dst, src, data_len);
	err = aes_ccm_decrypt_final(&ctx, authtag);
	if (unlikely(err) && data_len) {
		/*
		 * Clear the inauthentic decrypted data so that callers won't
		 * receive it even if they fail to correctly handle errors.
		 */
		memset(dst, 0, data_len);
	}
	return err;
}
EXPORT_SYMBOL_GPL(aes_ccm_decrypt);

/* FIPS cryptographic algorithm self-test for AES-CCM */
static void __init aes_ccm_fips_test(void)
{
	const size_t data_len = sizeof(fips_test_data);
	const size_t nonce_len = 13;
	u8 buf[sizeof(fips_test_data) + AES_BLOCK_SIZE];
	struct aes_ccm_key key;
	int err;

	if (aes_ccm_preparekey(&key, fips_test_key, sizeof(fips_test_key),
			       AES_BLOCK_SIZE) != 0)
		panic("aes: CCM FIPS self-test failed (preparekey)\n");

	err = aes_ccm_encrypt(buf, fips_test_data, data_len, &buf[data_len],
			      fips_test_ad, sizeof(fips_test_ad), fips_test_iv,
			      nonce_len, &key);
	if (err != 0)
		panic("aes: CCM FIPS self-test failed (encryption failed)\n");
	if (memcmp(fips_test_aes_ccm_ctext_and_tag, buf, sizeof(buf)) != 0)
		panic("aes: CCM FIPS self-test failed (wrong ciphertext and/or tag)\n");

	err = aes_ccm_decrypt(buf, buf, data_len, &buf[data_len], fips_test_ad,
			      sizeof(fips_test_ad), fips_test_iv, nonce_len,
			      &key);
	if (err != 0)
		panic("aes: CCM FIPS self-test failed (decryption failed)\n");
	if (memcmp(fips_test_data, buf, data_len) != 0)
		panic("aes: CCM FIPS self-test failed (wrong plaintext)\n");

	memzero_explicit(&key, sizeof(key));
}
#else /* CONFIG_CRYPTO_LIB_AES_CCM */
static inline void aes_ccm_fips_test(void)
{
}
#endif /* !CONFIG_CRYPTO_LIB_AES_CCM */

static int __init aes_mod_init(void)
{
#ifdef aes_mod_init_arch
	aes_mod_init_arch();
#endif
	if (fips_enabled) {
		aes_fips_test();
		aes_cmac_fips_test();
		aes_ecb_fips_test();
		aes_cbc_fips_test();
		aes_cbc_cts_fips_test();
		aes_ctr_fips_test();
		aes_xts_fips_test();
		aes_gcm_fips_test();
		aes_ccm_fips_test();
	}
	return 0;
}
subsys_initcall(aes_mod_init);

static void __exit aes_mod_exit(void)
{
}
module_exit(aes_mod_exit);

MODULE_DESCRIPTION("AES block cipher");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_AUTHOR("Eric Biggers <ebiggers@kernel.org>");
MODULE_LICENSE("GPL v2");
