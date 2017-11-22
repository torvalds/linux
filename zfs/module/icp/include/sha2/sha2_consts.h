/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_SHA2_CONSTS_H
#define	_SYS_SHA2_CONSTS_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Loading 32-bit constants on a sparc is expensive since it involves both
 * a `sethi' and an `or'.  thus, we instead use `ld' to load the constants
 * from an array called `sha2_consts'.  however, on intel (and perhaps other
 * processors), it is cheaper to load the constant directly.  thus, the c
 * code in SHA transform functions uses the macro SHA2_CONST() which either
 * expands to a constant or an array reference, depending on
 * the architecture the code is being compiled for.
 *
 * SHA512 constants are used for SHA384
 */

#include <sys/types.h>		/* uint32_t */

extern	const uint32_t	sha256_consts[];
extern	const uint64_t	sha512_consts[];

#if	defined(__sparc)
#define	SHA256_CONST(x)		(sha256_consts[x])
#define	SHA512_CONST(x)		(sha512_consts[x])
#else
#define	SHA256_CONST(x)		(SHA256_CONST_ ## x)
#define	SHA512_CONST(x)		(SHA512_CONST_ ## x)
#endif

/* constants, as provided in FIPS 180-2 */

#define	SHA256_CONST_0		0x428a2f98U
#define	SHA256_CONST_1		0x71374491U
#define	SHA256_CONST_2		0xb5c0fbcfU
#define	SHA256_CONST_3		0xe9b5dba5U
#define	SHA256_CONST_4		0x3956c25bU
#define	SHA256_CONST_5		0x59f111f1U
#define	SHA256_CONST_6		0x923f82a4U
#define	SHA256_CONST_7		0xab1c5ed5U

#define	SHA256_CONST_8		0xd807aa98U
#define	SHA256_CONST_9		0x12835b01U
#define	SHA256_CONST_10		0x243185beU
#define	SHA256_CONST_11		0x550c7dc3U
#define	SHA256_CONST_12		0x72be5d74U
#define	SHA256_CONST_13		0x80deb1feU
#define	SHA256_CONST_14		0x9bdc06a7U
#define	SHA256_CONST_15		0xc19bf174U

#define	SHA256_CONST_16		0xe49b69c1U
#define	SHA256_CONST_17		0xefbe4786U
#define	SHA256_CONST_18		0x0fc19dc6U
#define	SHA256_CONST_19		0x240ca1ccU
#define	SHA256_CONST_20		0x2de92c6fU
#define	SHA256_CONST_21		0x4a7484aaU
#define	SHA256_CONST_22		0x5cb0a9dcU
#define	SHA256_CONST_23		0x76f988daU

#define	SHA256_CONST_24		0x983e5152U
#define	SHA256_CONST_25		0xa831c66dU
#define	SHA256_CONST_26		0xb00327c8U
#define	SHA256_CONST_27		0xbf597fc7U
#define	SHA256_CONST_28		0xc6e00bf3U
#define	SHA256_CONST_29		0xd5a79147U
#define	SHA256_CONST_30		0x06ca6351U
#define	SHA256_CONST_31		0x14292967U

#define	SHA256_CONST_32		0x27b70a85U
#define	SHA256_CONST_33		0x2e1b2138U
#define	SHA256_CONST_34		0x4d2c6dfcU
#define	SHA256_CONST_35		0x53380d13U
#define	SHA256_CONST_36		0x650a7354U
#define	SHA256_CONST_37		0x766a0abbU
#define	SHA256_CONST_38		0x81c2c92eU
#define	SHA256_CONST_39		0x92722c85U

#define	SHA256_CONST_40		0xa2bfe8a1U
#define	SHA256_CONST_41		0xa81a664bU
#define	SHA256_CONST_42		0xc24b8b70U
#define	SHA256_CONST_43		0xc76c51a3U
#define	SHA256_CONST_44		0xd192e819U
#define	SHA256_CONST_45		0xd6990624U
#define	SHA256_CONST_46		0xf40e3585U
#define	SHA256_CONST_47		0x106aa070U

#define	SHA256_CONST_48		0x19a4c116U
#define	SHA256_CONST_49		0x1e376c08U
#define	SHA256_CONST_50		0x2748774cU
#define	SHA256_CONST_51		0x34b0bcb5U
#define	SHA256_CONST_52		0x391c0cb3U
#define	SHA256_CONST_53		0x4ed8aa4aU
#define	SHA256_CONST_54		0x5b9cca4fU
#define	SHA256_CONST_55		0x682e6ff3U

#define	SHA256_CONST_56		0x748f82eeU
#define	SHA256_CONST_57		0x78a5636fU
#define	SHA256_CONST_58		0x84c87814U
#define	SHA256_CONST_59		0x8cc70208U
#define	SHA256_CONST_60		0x90befffaU
#define	SHA256_CONST_61		0xa4506cebU
#define	SHA256_CONST_62		0xbef9a3f7U
#define	SHA256_CONST_63		0xc67178f2U

#define	SHA512_CONST_0		0x428a2f98d728ae22ULL
#define	SHA512_CONST_1		0x7137449123ef65cdULL
#define	SHA512_CONST_2		0xb5c0fbcfec4d3b2fULL
#define	SHA512_CONST_3		0xe9b5dba58189dbbcULL
#define	SHA512_CONST_4		0x3956c25bf348b538ULL
#define	SHA512_CONST_5		0x59f111f1b605d019ULL
#define	SHA512_CONST_6		0x923f82a4af194f9bULL
#define	SHA512_CONST_7		0xab1c5ed5da6d8118ULL
#define	SHA512_CONST_8		0xd807aa98a3030242ULL
#define	SHA512_CONST_9		0x12835b0145706fbeULL
#define	SHA512_CONST_10		0x243185be4ee4b28cULL
#define	SHA512_CONST_11		0x550c7dc3d5ffb4e2ULL
#define	SHA512_CONST_12		0x72be5d74f27b896fULL
#define	SHA512_CONST_13		0x80deb1fe3b1696b1ULL
#define	SHA512_CONST_14		0x9bdc06a725c71235ULL
#define	SHA512_CONST_15		0xc19bf174cf692694ULL
#define	SHA512_CONST_16		0xe49b69c19ef14ad2ULL
#define	SHA512_CONST_17		0xefbe4786384f25e3ULL
#define	SHA512_CONST_18		0x0fc19dc68b8cd5b5ULL
#define	SHA512_CONST_19		0x240ca1cc77ac9c65ULL
#define	SHA512_CONST_20		0x2de92c6f592b0275ULL
#define	SHA512_CONST_21		0x4a7484aa6ea6e483ULL
#define	SHA512_CONST_22		0x5cb0a9dcbd41fbd4ULL
#define	SHA512_CONST_23		0x76f988da831153b5ULL
#define	SHA512_CONST_24		0x983e5152ee66dfabULL
#define	SHA512_CONST_25		0xa831c66d2db43210ULL
#define	SHA512_CONST_26		0xb00327c898fb213fULL
#define	SHA512_CONST_27		0xbf597fc7beef0ee4ULL
#define	SHA512_CONST_28		0xc6e00bf33da88fc2ULL
#define	SHA512_CONST_29		0xd5a79147930aa725ULL
#define	SHA512_CONST_30		0x06ca6351e003826fULL
#define	SHA512_CONST_31		0x142929670a0e6e70ULL
#define	SHA512_CONST_32		0x27b70a8546d22ffcULL
#define	SHA512_CONST_33		0x2e1b21385c26c926ULL
#define	SHA512_CONST_34		0x4d2c6dfc5ac42aedULL
#define	SHA512_CONST_35		0x53380d139d95b3dfULL
#define	SHA512_CONST_36		0x650a73548baf63deULL
#define	SHA512_CONST_37		0x766a0abb3c77b2a8ULL
#define	SHA512_CONST_38		0x81c2c92e47edaee6ULL
#define	SHA512_CONST_39		0x92722c851482353bULL
#define	SHA512_CONST_40		0xa2bfe8a14cf10364ULL
#define	SHA512_CONST_41		0xa81a664bbc423001ULL
#define	SHA512_CONST_42		0xc24b8b70d0f89791ULL
#define	SHA512_CONST_43		0xc76c51a30654be30ULL
#define	SHA512_CONST_44		0xd192e819d6ef5218ULL
#define	SHA512_CONST_45		0xd69906245565a910ULL
#define	SHA512_CONST_46		0xf40e35855771202aULL
#define	SHA512_CONST_47		0x106aa07032bbd1b8ULL
#define	SHA512_CONST_48		0x19a4c116b8d2d0c8ULL
#define	SHA512_CONST_49		0x1e376c085141ab53ULL
#define	SHA512_CONST_50		0x2748774cdf8eeb99ULL
#define	SHA512_CONST_51		0x34b0bcb5e19b48a8ULL
#define	SHA512_CONST_52		0x391c0cb3c5c95a63ULL
#define	SHA512_CONST_53		0x4ed8aa4ae3418acbULL
#define	SHA512_CONST_54		0x5b9cca4f7763e373ULL
#define	SHA512_CONST_55		0x682e6ff3d6b2b8a3ULL
#define	SHA512_CONST_56		0x748f82ee5defb2fcULL
#define	SHA512_CONST_57		0x78a5636f43172f60ULL
#define	SHA512_CONST_58		0x84c87814a1f0ab72ULL
#define	SHA512_CONST_59		0x8cc702081a6439ecULL
#define	SHA512_CONST_60		0x90befffa23631e28ULL
#define	SHA512_CONST_61		0xa4506cebde82bde9ULL
#define	SHA512_CONST_62		0xbef9a3f7b2c67915ULL
#define	SHA512_CONST_63		0xc67178f2e372532bULL
#define	SHA512_CONST_64		0xca273eceea26619cULL
#define	SHA512_CONST_65		0xd186b8c721c0c207ULL
#define	SHA512_CONST_66		0xeada7dd6cde0eb1eULL
#define	SHA512_CONST_67		0xf57d4f7fee6ed178ULL
#define	SHA512_CONST_68		0x06f067aa72176fbaULL
#define	SHA512_CONST_69		0x0a637dc5a2c898a6ULL
#define	SHA512_CONST_70		0x113f9804bef90daeULL
#define	SHA512_CONST_71		0x1b710b35131c471bULL
#define	SHA512_CONST_72		0x28db77f523047d84ULL
#define	SHA512_CONST_73		0x32caab7b40c72493ULL
#define	SHA512_CONST_74		0x3c9ebe0a15c9bebcULL
#define	SHA512_CONST_75		0x431d67c49c100d4cULL
#define	SHA512_CONST_76		0x4cc5d4becb3e42b6ULL
#define	SHA512_CONST_77		0x597f299cfc657e2aULL
#define	SHA512_CONST_78		0x5fcb6fab3ad6faecULL
#define	SHA512_CONST_79		0x6c44198c4a475817ULL


#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SHA2_CONSTS_H */
