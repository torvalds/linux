/*
 * Interface declarations for Skein hashing.
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
#ifndef	_SYS_SKEIN_H_
#define	_SYS_SKEIN_H_

#ifdef  _KERNEL
#include <sys/types.h>		/* get size_t definition */
#else
#include <stdint.h>
#include <stdlib.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

enum {
	SKEIN_SUCCESS = 0,	/* return codes from Skein calls */
	SKEIN_FAIL = 1,
	SKEIN_BAD_HASHLEN = 2
};

#define	SKEIN_MODIFIER_WORDS	(2)	/* number of modifier (tweak) words */

#define	SKEIN_256_STATE_WORDS	(4)
#define	SKEIN_512_STATE_WORDS	(8)
#define	SKEIN1024_STATE_WORDS	(16)
#define	SKEIN_MAX_STATE_WORDS	(16)

#define	SKEIN_256_STATE_BYTES	(8 * SKEIN_256_STATE_WORDS)
#define	SKEIN_512_STATE_BYTES	(8 * SKEIN_512_STATE_WORDS)
#define	SKEIN1024_STATE_BYTES	(8 * SKEIN1024_STATE_WORDS)

#define	SKEIN_256_STATE_BITS	(64 * SKEIN_256_STATE_WORDS)
#define	SKEIN_512_STATE_BITS	(64 * SKEIN_512_STATE_WORDS)
#define	SKEIN1024_STATE_BITS	(64 * SKEIN1024_STATE_WORDS)

#define	SKEIN_256_BLOCK_BYTES	(8 * SKEIN_256_STATE_WORDS)
#define	SKEIN_512_BLOCK_BYTES	(8 * SKEIN_512_STATE_WORDS)
#define	SKEIN1024_BLOCK_BYTES	(8 * SKEIN1024_STATE_WORDS)

typedef struct {
	size_t hashBitLen;	/* size of hash result, in bits */
	size_t bCnt;		/* current byte count in buffer b[] */
	/* tweak words: T[0]=byte cnt, T[1]=flags */
	uint64_t T[SKEIN_MODIFIER_WORDS];
} Skein_Ctxt_Hdr_t;

typedef struct {		/*  256-bit Skein hash context structure */
	Skein_Ctxt_Hdr_t h;	/* common header context variables */
	uint64_t X[SKEIN_256_STATE_WORDS];	/* chaining variables */
	/* partial block buffer (8-byte aligned) */
	uint8_t b[SKEIN_256_BLOCK_BYTES];
} Skein_256_Ctxt_t;

typedef struct {		/*  512-bit Skein hash context structure */
	Skein_Ctxt_Hdr_t h;	/* common header context variables */
	uint64_t X[SKEIN_512_STATE_WORDS];	/* chaining variables */
	/* partial block buffer (8-byte aligned) */
	uint8_t b[SKEIN_512_BLOCK_BYTES];
} Skein_512_Ctxt_t;

typedef struct {		/* 1024-bit Skein hash context structure */
	Skein_Ctxt_Hdr_t h;	/* common header context variables */
	uint64_t X[SKEIN1024_STATE_WORDS];	/* chaining variables */
	/* partial block buffer (8-byte aligned) */
	uint8_t b[SKEIN1024_BLOCK_BYTES];
} Skein1024_Ctxt_t;

/*   Skein APIs for (incremental) "straight hashing" */
int Skein_256_Init(Skein_256_Ctxt_t *ctx, size_t hashBitLen);
int Skein_512_Init(Skein_512_Ctxt_t *ctx, size_t hashBitLen);
int Skein1024_Init(Skein1024_Ctxt_t *ctx, size_t hashBitLen);

int Skein_256_Update(Skein_256_Ctxt_t *ctx, const uint8_t *msg,
    size_t msgByteCnt);
int Skein_512_Update(Skein_512_Ctxt_t *ctx, const uint8_t *msg,
    size_t msgByteCnt);
int Skein1024_Update(Skein1024_Ctxt_t *ctx, const uint8_t *msg,
    size_t msgByteCnt);

int Skein_256_Final(Skein_256_Ctxt_t *ctx, uint8_t *hashVal);
int Skein_512_Final(Skein_512_Ctxt_t *ctx, uint8_t *hashVal);
int Skein1024_Final(Skein1024_Ctxt_t *ctx, uint8_t *hashVal);

/*
 * Skein APIs for "extended" initialization: MAC keys, tree hashing.
 * After an InitExt() call, just use Update/Final calls as with Init().
 *
 * Notes: Same parameters as _Init() calls, plus treeInfo/key/keyBytes.
 *          When keyBytes == 0 and treeInfo == SKEIN_SEQUENTIAL,
 *              the results of InitExt() are identical to calling Init().
 *          The function Init() may be called once to "precompute" the IV for
 *              a given hashBitLen value, then by saving a copy of the context
 *              the IV computation may be avoided in later calls.
 *          Similarly, the function InitExt() may be called once per MAC key
 *              to precompute the MAC IV, then a copy of the context saved and
 *              reused for each new MAC computation.
 */
int Skein_256_InitExt(Skein_256_Ctxt_t *ctx, size_t hashBitLen,
    uint64_t treeInfo, const uint8_t *key, size_t keyBytes);
int Skein_512_InitExt(Skein_512_Ctxt_t *ctx, size_t hashBitLen,
    uint64_t treeInfo, const uint8_t *key, size_t keyBytes);
int Skein1024_InitExt(Skein1024_Ctxt_t *ctx, size_t hashBitLen,
    uint64_t treeInfo, const uint8_t *key, size_t keyBytes);

/*
 * Skein APIs for MAC and tree hash:
 *	Final_Pad: pad, do final block, but no OUTPUT type
 *	Output:    do just the output stage
 */
int Skein_256_Final_Pad(Skein_256_Ctxt_t *ctx, uint8_t *hashVal);
int Skein_512_Final_Pad(Skein_512_Ctxt_t *ctx, uint8_t *hashVal);
int Skein1024_Final_Pad(Skein1024_Ctxt_t *ctx, uint8_t *hashVal);

#ifndef	SKEIN_TREE_HASH
#define	SKEIN_TREE_HASH (1)
#endif
#if	SKEIN_TREE_HASH
int Skein_256_Output(Skein_256_Ctxt_t *ctx, uint8_t *hashVal);
int Skein_512_Output(Skein_512_Ctxt_t *ctx, uint8_t *hashVal);
int Skein1024_Output(Skein1024_Ctxt_t *ctx, uint8_t *hashVal);
#endif

/*
 * When you initialize a Skein KCF hashing method you can pass this param
 * structure in cm_param to fine-tune the algorithm's defaults.
 */
typedef struct skein_param {
	size_t	sp_digest_bitlen;		/* length of digest in bits */
} skein_param_t;

/* Module definitions */
#ifdef	SKEIN_MODULE_IMPL
#define	CKM_SKEIN_256				"CKM_SKEIN_256"
#define	CKM_SKEIN_512				"CKM_SKEIN_512"
#define	CKM_SKEIN1024				"CKM_SKEIN1024"
#define	CKM_SKEIN_256_MAC			"CKM_SKEIN_256_MAC"
#define	CKM_SKEIN_512_MAC			"CKM_SKEIN_512_MAC"
#define	CKM_SKEIN1024_MAC			"CKM_SKEIN1024_MAC"

typedef enum skein_mech_type {
	SKEIN_256_MECH_INFO_TYPE,
	SKEIN_512_MECH_INFO_TYPE,
	SKEIN1024_MECH_INFO_TYPE,
	SKEIN_256_MAC_MECH_INFO_TYPE,
	SKEIN_512_MAC_MECH_INFO_TYPE,
	SKEIN1024_MAC_MECH_INFO_TYPE
} skein_mech_type_t;

#define	VALID_SKEIN_DIGEST_MECH(__mech)				\
	((int)(__mech) >= SKEIN_256_MECH_INFO_TYPE &&		\
	(__mech) <= SKEIN1024_MECH_INFO_TYPE)
#define	VALID_SKEIN_MAC_MECH(__mech)				\
	((int)(__mech) >= SKEIN_256_MAC_MECH_INFO_TYPE &&	\
	(__mech) <= SKEIN1024_MAC_MECH_INFO_TYPE)
#endif	/* SKEIN_MODULE_IMPL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SKEIN_H_ */
