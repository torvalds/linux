/*
 * Internal definitions for Skein hashing.
 * Source code author: Doug Whiting, 2008.
 * This algorithm and source code is released to the public domain.
 *
 * The following compile-time switches may be defined to control some
 * tradeoffs between speed, code size, error checking, and security.
 *
 * The "default" note explains what happens when the switch is not defined.
 *
 *  SKEIN_DEBUG            -- make callouts from inside Skein code
 *                            to examine/display intermediate values.
 *                            [default: no callouts (no overhead)]
 *
 *  SKEIN_ERR_CHECK        -- how error checking is handled inside Skein
 *                            code. If not defined, most error checking
 *                            is disabled (for performance). Otherwise,
 *                            the switch value is interpreted as:
 *                                0: use assert()      to flag errors
 *                                1: return SKEIN_FAIL to flag errors
 */
/* Copyright 2013 Doug Whiting. This code is released to the public domain. */

#ifndef	_SKEIN_IMPL_H_
#define	_SKEIN_IMPL_H_

#include <sys/skein.h>
#include "skein_impl.h"
#include "skein_port.h"

/* determine where we can get bcopy/bzero declarations */
#ifdef	_KERNEL
#include <sys/systm.h>
#else
#include <strings.h>
#endif

/*
 * "Internal" Skein definitions
 *    -- not needed for sequential hashing API, but will be
 *           helpful for other uses of Skein (e.g., tree hash mode).
 *    -- included here so that they can be shared between
 *           reference and optimized code.
 */

/* tweak word T[1]: bit field starting positions */
/* offset 64 because it's the second word  */
#define	SKEIN_T1_BIT(BIT)	((BIT) - 64)

/* bits 112..118: level in hash tree */
#define	SKEIN_T1_POS_TREE_LVL	SKEIN_T1_BIT(112)
/* bit  119: partial final input byte */
#define	SKEIN_T1_POS_BIT_PAD	SKEIN_T1_BIT(119)
/* bits 120..125: type field */
#define	SKEIN_T1_POS_BLK_TYPE	SKEIN_T1_BIT(120)
/* bits 126: first block flag */
#define	SKEIN_T1_POS_FIRST	SKEIN_T1_BIT(126)
/* bit  127: final block flag */
#define	SKEIN_T1_POS_FINAL	SKEIN_T1_BIT(127)

/* tweak word T[1]: flag bit definition(s) */
#define	SKEIN_T1_FLAG_FIRST	(((uint64_t)1) << SKEIN_T1_POS_FIRST)
#define	SKEIN_T1_FLAG_FINAL	(((uint64_t)1) << SKEIN_T1_POS_FINAL)
#define	SKEIN_T1_FLAG_BIT_PAD	(((uint64_t)1) << SKEIN_T1_POS_BIT_PAD)

/* tweak word T[1]: tree level bit field mask */
#define	SKEIN_T1_TREE_LVL_MASK	(((uint64_t)0x7F) << SKEIN_T1_POS_TREE_LVL)
#define	SKEIN_T1_TREE_LEVEL(n)	(((uint64_t)(n)) << SKEIN_T1_POS_TREE_LVL)

/* tweak word T[1]: block type field */
#define	SKEIN_BLK_TYPE_KEY	(0)	/* key, for MAC and KDF */
#define	SKEIN_BLK_TYPE_CFG	(4)	/* configuration block */
#define	SKEIN_BLK_TYPE_PERS	(8)	/* personalization string */
#define	SKEIN_BLK_TYPE_PK	(12)	/* public key (for signature hashing) */
#define	SKEIN_BLK_TYPE_KDF	(16)	/* key identifier for KDF */
#define	SKEIN_BLK_TYPE_NONCE	(20)	/* nonce for PRNG */
#define	SKEIN_BLK_TYPE_MSG	(48)	/* message processing */
#define	SKEIN_BLK_TYPE_OUT	(63)	/* output stage */
#define	SKEIN_BLK_TYPE_MASK	(63)	/* bit field mask */

#define	SKEIN_T1_BLK_TYPE(T)	\
	(((uint64_t)(SKEIN_BLK_TYPE_##T)) << SKEIN_T1_POS_BLK_TYPE)
/* key, for MAC and KDF */
#define	SKEIN_T1_BLK_TYPE_KEY	SKEIN_T1_BLK_TYPE(KEY)
/* configuration block */
#define	SKEIN_T1_BLK_TYPE_CFG	SKEIN_T1_BLK_TYPE(CFG)
/* personalization string */
#define	SKEIN_T1_BLK_TYPE_PERS	SKEIN_T1_BLK_TYPE(PERS)
/* public key (for digital signature hashing) */
#define	SKEIN_T1_BLK_TYPE_PK	SKEIN_T1_BLK_TYPE(PK)
/* key identifier for KDF */
#define	SKEIN_T1_BLK_TYPE_KDF	SKEIN_T1_BLK_TYPE(KDF)
/* nonce for PRNG */
#define	SKEIN_T1_BLK_TYPE_NONCE	SKEIN_T1_BLK_TYPE(NONCE)
/* message processing */
#define	SKEIN_T1_BLK_TYPE_MSG	SKEIN_T1_BLK_TYPE(MSG)
/* output stage */
#define	SKEIN_T1_BLK_TYPE_OUT	SKEIN_T1_BLK_TYPE(OUT)
/* field bit mask */
#define	SKEIN_T1_BLK_TYPE_MASK	SKEIN_T1_BLK_TYPE(MASK)

#define	SKEIN_T1_BLK_TYPE_CFG_FINAL	\
	(SKEIN_T1_BLK_TYPE_CFG | SKEIN_T1_FLAG_FINAL)
#define	SKEIN_T1_BLK_TYPE_OUT_FINAL	\
	(SKEIN_T1_BLK_TYPE_OUT | SKEIN_T1_FLAG_FINAL)

#define	SKEIN_VERSION		(1)

#ifndef	SKEIN_ID_STRING_LE	/* allow compile-time personalization */
#define	SKEIN_ID_STRING_LE	(0x33414853)	/* "SHA3" (little-endian) */
#endif

#define	SKEIN_MK_64(hi32, lo32)	((lo32) + (((uint64_t)(hi32)) << 32))
#define	SKEIN_SCHEMA_VER	SKEIN_MK_64(SKEIN_VERSION, SKEIN_ID_STRING_LE)
#define	SKEIN_KS_PARITY		SKEIN_MK_64(0x1BD11BDA, 0xA9FC1A22)

#define	SKEIN_CFG_STR_LEN	(4*8)

/* bit field definitions in config block treeInfo word */
#define	SKEIN_CFG_TREE_LEAF_SIZE_POS	(0)
#define	SKEIN_CFG_TREE_NODE_SIZE_POS	(8)
#define	SKEIN_CFG_TREE_MAX_LEVEL_POS	(16)

#define	SKEIN_CFG_TREE_LEAF_SIZE_MSK	\
	(((uint64_t)0xFF) << SKEIN_CFG_TREE_LEAF_SIZE_POS)
#define	SKEIN_CFG_TREE_NODE_SIZE_MSK	\
	(((uint64_t)0xFF) << SKEIN_CFG_TREE_NODE_SIZE_POS)
#define	SKEIN_CFG_TREE_MAX_LEVEL_MSK	\
	(((uint64_t)0xFF) << SKEIN_CFG_TREE_MAX_LEVEL_POS)

#define	SKEIN_CFG_TREE_INFO(leaf, node, maxLvl)			\
	((((uint64_t)(leaf)) << SKEIN_CFG_TREE_LEAF_SIZE_POS) |	\
	(((uint64_t)(node)) << SKEIN_CFG_TREE_NODE_SIZE_POS) |	\
	(((uint64_t)(maxLvl)) << SKEIN_CFG_TREE_MAX_LEVEL_POS))

/* use as treeInfo in InitExt() call for sequential processing */
#define	SKEIN_CFG_TREE_INFO_SEQUENTIAL	SKEIN_CFG_TREE_INFO(0, 0, 0)

/*
 * Skein macros for getting/setting tweak words, etc.
 * These are useful for partial input bytes, hash tree init/update, etc.
 */
#define	Skein_Get_Tweak(ctxPtr, TWK_NUM)	((ctxPtr)->h.T[TWK_NUM])
#define	Skein_Set_Tweak(ctxPtr, TWK_NUM, tVal)		\
	do {						\
		(ctxPtr)->h.T[TWK_NUM] = (tVal);	\
		_NOTE(CONSTCOND)			\
	} while (0)

#define	Skein_Get_T0(ctxPtr)		Skein_Get_Tweak(ctxPtr, 0)
#define	Skein_Get_T1(ctxPtr)		Skein_Get_Tweak(ctxPtr, 1)
#define	Skein_Set_T0(ctxPtr, T0)	Skein_Set_Tweak(ctxPtr, 0, T0)
#define	Skein_Set_T1(ctxPtr, T1)	Skein_Set_Tweak(ctxPtr, 1, T1)

/* set both tweak words at once */
#define	Skein_Set_T0_T1(ctxPtr, T0, T1)		\
	do {					\
		Skein_Set_T0(ctxPtr, (T0));	\
		Skein_Set_T1(ctxPtr, (T1));	\
		_NOTE(CONSTCOND)		\
	} while (0)

#define	Skein_Set_Type(ctxPtr, BLK_TYPE)	\
	Skein_Set_T1(ctxPtr, SKEIN_T1_BLK_TYPE_##BLK_TYPE)

/*
 * set up for starting with a new type: h.T[0]=0; h.T[1] = NEW_TYPE; h.bCnt=0;
 */
#define	Skein_Start_New_Type(ctxPtr, BLK_TYPE)				\
	do {								\
		Skein_Set_T0_T1(ctxPtr, 0, SKEIN_T1_FLAG_FIRST |	\
		    SKEIN_T1_BLK_TYPE_ ## BLK_TYPE);			\
		(ctxPtr)->h.bCnt = 0;	\
		_NOTE(CONSTCOND)					\
	} while (0)

#define	Skein_Clear_First_Flag(hdr)					\
	do {								\
		(hdr).T[1] &= ~SKEIN_T1_FLAG_FIRST;			\
		_NOTE(CONSTCOND)					\
	} while (0)
#define	Skein_Set_Bit_Pad_Flag(hdr)					\
	do {								\
		(hdr).T[1] |=  SKEIN_T1_FLAG_BIT_PAD;			\
		_NOTE(CONSTCOND)					\
	} while (0)

#define	Skein_Set_Tree_Level(hdr, height)				\
	do {								\
		(hdr).T[1] |= SKEIN_T1_TREE_LEVEL(height);		\
		_NOTE(CONSTCOND)					\
	} while (0)

/*
 * "Internal" Skein definitions for debugging and error checking
 * Note: in Illumos we always disable debugging features.
 */
#define	Skein_Show_Block(bits, ctx, X, blkPtr, wPtr, ksEvenPtr, ksOddPtr)
#define	Skein_Show_Round(bits, ctx, r, X)
#define	Skein_Show_R_Ptr(bits, ctx, r, X_ptr)
#define	Skein_Show_Final(bits, ctx, cnt, outPtr)
#define	Skein_Show_Key(bits, ctx, key, keyBytes)

/* run-time checks (e.g., bad params, uninitialized context)? */
#ifndef	SKEIN_ERR_CHECK
/* default: ignore all Asserts, for performance */
#define	Skein_Assert(x, retCode)
#define	Skein_assert(x)
#elif	defined(SKEIN_ASSERT)
#include <sys/debug.h>
#define	Skein_Assert(x, retCode)	ASSERT(x)
#define	Skein_assert(x)			ASSERT(x)
#else
#include <sys/debug.h>
/*  caller error */
#define	Skein_Assert(x, retCode)		\
	do {					\
		if (!(x))			\
			return (retCode);	\
		_NOTE(CONSTCOND)		\
	} while (0)
/* internal error */
#define	Skein_assert(x)	ASSERT(x)
#endif

/*
 * Skein block function constants (shared across Ref and Opt code)
 */
enum {
	/* Skein_256 round rotation constants */
	R_256_0_0 = 14, R_256_0_1 = 16,
	R_256_1_0 = 52, R_256_1_1 = 57,
	R_256_2_0 = 23, R_256_2_1 = 40,
	R_256_3_0 = 5, R_256_3_1 = 37,
	R_256_4_0 = 25, R_256_4_1 = 33,
	R_256_5_0 = 46, R_256_5_1 = 12,
	R_256_6_0 = 58, R_256_6_1 = 22,
	R_256_7_0 = 32, R_256_7_1 = 32,

	/* Skein_512 round rotation constants */
	R_512_0_0 = 46, R_512_0_1 = 36, R_512_0_2 = 19, R_512_0_3 = 37,
	R_512_1_0 = 33, R_512_1_1 = 27, R_512_1_2 = 14, R_512_1_3 = 42,
	R_512_2_0 = 17, R_512_2_1 = 49, R_512_2_2 = 36, R_512_2_3 = 39,
	R_512_3_0 = 44, R_512_3_1 = 9, R_512_3_2 = 54, R_512_3_3 = 56,
	R_512_4_0 = 39, R_512_4_1 = 30, R_512_4_2 = 34, R_512_4_3 = 24,
	R_512_5_0 = 13, R_512_5_1 = 50, R_512_5_2 = 10, R_512_5_3 = 17,
	R_512_6_0 = 25, R_512_6_1 = 29, R_512_6_2 = 39, R_512_6_3 = 43,
	R_512_7_0 = 8, R_512_7_1 = 35, R_512_7_2 = 56, R_512_7_3 = 22,

	/* Skein1024 round rotation constants */
	R1024_0_0 = 24, R1024_0_1 = 13, R1024_0_2 = 8, R1024_0_3 =
	    47, R1024_0_4 = 8, R1024_0_5 = 17, R1024_0_6 = 22, R1024_0_7 = 37,
	R1024_1_0 = 38, R1024_1_1 = 19, R1024_1_2 = 10, R1024_1_3 =
	    55, R1024_1_4 = 49, R1024_1_5 = 18, R1024_1_6 = 23, R1024_1_7 = 52,
	R1024_2_0 = 33, R1024_2_1 = 4, R1024_2_2 = 51, R1024_2_3 =
	    13, R1024_2_4 = 34, R1024_2_5 = 41, R1024_2_6 = 59, R1024_2_7 = 17,
	R1024_3_0 = 5, R1024_3_1 = 20, R1024_3_2 = 48, R1024_3_3 =
	    41, R1024_3_4 = 47, R1024_3_5 = 28, R1024_3_6 = 16, R1024_3_7 = 25,
	R1024_4_0 = 41, R1024_4_1 = 9, R1024_4_2 = 37, R1024_4_3 =
	    31, R1024_4_4 = 12, R1024_4_5 = 47, R1024_4_6 = 44, R1024_4_7 = 30,
	R1024_5_0 = 16, R1024_5_1 = 34, R1024_5_2 = 56, R1024_5_3 =
	    51, R1024_5_4 = 4, R1024_5_5 = 53, R1024_5_6 = 42, R1024_5_7 = 41,
	R1024_6_0 = 31, R1024_6_1 = 44, R1024_6_2 = 47, R1024_6_3 =
	    46, R1024_6_4 = 19, R1024_6_5 = 42, R1024_6_6 = 44, R1024_6_7 = 25,
	R1024_7_0 = 9, R1024_7_1 = 48, R1024_7_2 = 35, R1024_7_3 =
	    52, R1024_7_4 = 23, R1024_7_5 = 31, R1024_7_6 = 37, R1024_7_7 = 20
};

/* number of rounds for the different block sizes */
#define	SKEIN_256_ROUNDS_TOTAL	(72)
#define	SKEIN_512_ROUNDS_TOTAL	(72)
#define	SKEIN1024_ROUNDS_TOTAL	(80)


extern const uint64_t SKEIN_256_IV_128[];
extern const uint64_t SKEIN_256_IV_160[];
extern const uint64_t SKEIN_256_IV_224[];
extern const uint64_t SKEIN_256_IV_256[];
extern const uint64_t SKEIN_512_IV_128[];
extern const uint64_t SKEIN_512_IV_160[];
extern const uint64_t SKEIN_512_IV_224[];
extern const uint64_t SKEIN_512_IV_256[];
extern const uint64_t SKEIN_512_IV_384[];
extern const uint64_t SKEIN_512_IV_512[];
extern const uint64_t SKEIN1024_IV_384[];
extern const uint64_t SKEIN1024_IV_512[];
extern const uint64_t SKEIN1024_IV_1024[];

#endif	/* _SKEIN_IMPL_H_ */
