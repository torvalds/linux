/*
 * Modified to interface to the Linux kernel
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */

#ifndef __CRYPTO_VMAC_H
#define __CRYPTO_VMAC_H

/* --------------------------------------------------------------------------
 * VMAC and VHASH Implementation by Ted Krovetz (tdk@acm.org) and Wei Dai.
 * This implementation is herby placed in the public domain.
 * The authors offers no warranty. Use at your own risk.
 * Please send bug reports to the authors.
 * Last modified: 17 APR 08, 1700 PDT
 * ----------------------------------------------------------------------- */

/*
 * User definable settings.
 */
#define VMAC_TAG_LEN	64
#define VMAC_KEY_SIZE	128/* Must be 128, 192 or 256			*/
#define VMAC_KEY_LEN	(VMAC_KEY_SIZE/8)
#define VMAC_NHBYTES	128/* Must 2^i for any 3 < i < 13 Standard = 128*/

/*
 * This implementation uses u32 and u64 as names for unsigned 32-
 * and 64-bit integer types. These are defined in C99 stdint.h. The
 * following may need adaptation if you are not running a C99 or
 * Microsoft C environment.
 */
struct vmac_ctx {
	u64 nhkey[(VMAC_NHBYTES/8)+2*(VMAC_TAG_LEN/64-1)];
	u64 polykey[2*VMAC_TAG_LEN/64];
	u64 l3key[2*VMAC_TAG_LEN/64];
	u64 polytmp[2*VMAC_TAG_LEN/64];
	u64 cached_nonce[2];
	u64 cached_aes[2];
	int first_block_processed;
};

typedef u64 vmac_t;

struct vmac_ctx_t {
	struct crypto_cipher *child;
	struct vmac_ctx __vmac_ctx;
	u8 partial[VMAC_NHBYTES];	/* partial block */
	int partial_size;		/* size of the partial block */
};

#endif /* __CRYPTO_VMAC_H */
