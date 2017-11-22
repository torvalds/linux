/*
 * Implementation of the Skein hash function.
 * Source code author: Doug Whiting, 2008.
 * This algorithm and source code is released to the public domain.
 */
/* Copyright 2013 Doug Whiting. This code is released to the public domain. */

#define	SKEIN_PORT_CODE		/* instantiate any code in skein_port.h */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/skein.h>		/* get the Skein API definitions   */
#include "skein_impl.h"		/* get internal definitions */

/* External function to process blkCnt (nonzero) full block(s) of data. */
void Skein_256_Process_Block(Skein_256_Ctxt_t *ctx, const uint8_t *blkPtr,
    size_t blkCnt, size_t byteCntAdd);
void Skein_512_Process_Block(Skein_512_Ctxt_t *ctx, const uint8_t *blkPtr,
    size_t blkCnt, size_t byteCntAdd);
void Skein1024_Process_Block(Skein1024_Ctxt_t *ctx, const uint8_t *blkPtr,
    size_t blkCnt, size_t byteCntAdd);

/* 256-bit Skein */
/* init the context for a straight hashing operation  */
int
Skein_256_Init(Skein_256_Ctxt_t *ctx, size_t hashBitLen)
{
	union {
		uint8_t b[SKEIN_256_STATE_BYTES];
		uint64_t w[SKEIN_256_STATE_WORDS];
	} cfg;			/* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hashBitLen = hashBitLen;	/* output hash bit count */

	switch (hashBitLen) {	/* use pre-computed values, where available */
#ifndef	SKEIN_NO_PRECOMP
	case 256:
		bcopy(SKEIN_256_IV_256, ctx->X, sizeof (ctx->X));
		break;
	case 224:
		bcopy(SKEIN_256_IV_224, ctx->X, sizeof (ctx->X));
		break;
	case 160:
		bcopy(SKEIN_256_IV_160, ctx->X, sizeof (ctx->X));
		break;
	case 128:
		bcopy(SKEIN_256_IV_128, ctx->X, sizeof (ctx->X));
		break;
#endif
	default:
		/* here if there is no precomputed IV value available */
		/*
		 * build/process the config block, type == CONFIG (could be
		 * precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		Skein_Start_New_Type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = Skein_Swap64(hashBitLen);
		cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		bzero(&cfg.w[3], sizeof (cfg) - 3 * sizeof (cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		bzero(ctx->X, sizeof (ctx->X));
		Skein_256_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}
	/*
	 * The chaining vars ctx->X are now initialized for the given
	 * hashBitLen.
	 * Set up to process the data message portion of the hash (default)
	 */
	Skein_Start_New_Type(ctx, MSG);	/* T0=0, T1= MSG type */

	return (SKEIN_SUCCESS);
}

/* init the context for a MAC and/or tree hash operation */
/*
 * [identical to Skein_256_Init() when keyBytes == 0 &&
 * treeInfo == SKEIN_CFG_TREE_INFO_SEQUENTIAL]
 */
int
Skein_256_InitExt(Skein_256_Ctxt_t *ctx, size_t hashBitLen, uint64_t treeInfo,
    const uint8_t *key, size_t keyBytes)
{
	union {
		uint8_t b[SKEIN_256_STATE_BYTES];
		uint64_t w[SKEIN_256_STATE_WORDS];
	} cfg;			/* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	Skein_Assert(keyBytes == 0 || key != NULL, SKEIN_FAIL);

	/* compute the initial chaining values ctx->X[], based on key */
	if (keyBytes == 0) {	/* is there a key? */
		/* no key: use all zeroes as key for config block */
		bzero(ctx->X, sizeof (ctx->X));
	} else {		/* here to pre-process a key */

		Skein_assert(sizeof (cfg.b) >= sizeof (ctx->X));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hashBitLen = 8 * sizeof (ctx->X);
		/* set tweaks: T0 = 0; T1 = KEY type */
		Skein_Start_New_Type(ctx, KEY);
		/* zero the initial chaining variables */
		bzero(ctx->X, sizeof (ctx->X));
		/* hash the key */
		(void) Skein_256_Update(ctx, key, keyBytes);
		/* put result into cfg.b[] */
		(void) Skein_256_Final_Pad(ctx, cfg.b);
		/* copy over into ctx->X[] */
		bcopy(cfg.b, ctx->X, sizeof (cfg.b));
#if	SKEIN_NEED_SWAP
		{
			uint_t i;
			/* convert key bytes to context words */
			for (i = 0; i < SKEIN_256_STATE_WORDS; i++)
				ctx->X[i] = Skein_Swap64(ctx->X[i]);
		}
#endif
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	ctx->h.hashBitLen = hashBitLen;	/* output hash bit count */
	Skein_Start_New_Type(ctx, CFG_FINAL);

	bzero(&cfg.w, sizeof (cfg.w));	/* pre-pad cfg.w[] with zeroes */
	cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
	cfg.w[1] = Skein_Swap64(hashBitLen);	/* hash result length in bits */
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = Skein_Swap64(treeInfo);

	Skein_Show_Key(256, &ctx->h, key, keyBytes);

	/* compute the initial chaining values from config block */
	Skein_256_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->X are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	ctx->h.bCnt = 0;	/* buffer b[] starts out empty */
	Skein_Start_New_Type(ctx, MSG);

	return (SKEIN_SUCCESS);
}

/* process the input bytes */
int
Skein_256_Update(Skein_256_Ctxt_t *ctx, const uint8_t *msg, size_t msgByteCnt)
{
	size_t n;

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msgByteCnt + ctx->h.bCnt > SKEIN_256_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.bCnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN_256_BLOCK_BYTES - ctx->h.bCnt;
			if (n) {
				/* check on our logic here */
				Skein_assert(n < msgByteCnt);
				bcopy(msg, &ctx->b[ctx->h.bCnt], n);
				msgByteCnt -= n;
				msg += n;
				ctx->h.bCnt += n;
			}
			Skein_assert(ctx->h.bCnt == SKEIN_256_BLOCK_BYTES);
			Skein_256_Process_Block(ctx, ctx->b, 1,
			    SKEIN_256_BLOCK_BYTES);
			ctx->h.bCnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msgByteCnt > SKEIN_256_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msgByteCnt - 1) / SKEIN_256_BLOCK_BYTES;
			Skein_256_Process_Block(ctx, msg, n,
			    SKEIN_256_BLOCK_BYTES);
			msgByteCnt -= n * SKEIN_256_BLOCK_BYTES;
			msg += n * SKEIN_256_BLOCK_BYTES;
		}
		Skein_assert(ctx->h.bCnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msgByteCnt) {
		Skein_assert(msgByteCnt + ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES);
		bcopy(msg, &ctx->b[ctx->h.bCnt], msgByteCnt);
		ctx->h.bCnt += msgByteCnt;
	}

	return (SKEIN_SUCCESS);
}

/* finalize the hash computation and output the result */
int
Skein_256_Final(Skein_256_Ctxt_t *ctx, uint8_t *hashVal)
{
	size_t i, n, byteCnt;
	uint64_t X[SKEIN_256_STATE_WORDS];

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;	/* tag as the final block */
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN_256_BLOCK_BYTES)
		bzero(&ctx->b[ctx->h.bCnt],
		    SKEIN_256_BLOCK_BYTES - ctx->h.bCnt);

	/* process the final block */
	Skein_256_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	bzero(ctx->b, sizeof (ctx->b));
	/* keep a local copy of counter mode "key" */
	bcopy(ctx->X, X, sizeof (X));
	for (i = 0; i * SKEIN_256_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		uint64_t tmp = Skein_Swap64((uint64_t)i);
		bcopy(&tmp, ctx->b, sizeof (tmp));
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein_256_Process_Block(ctx, ctx->b, 1, sizeof (uint64_t));
		/* number of output bytes left to go */
		n = byteCnt - i * SKEIN_256_BLOCK_BYTES;
		if (n >= SKEIN_256_BLOCK_BYTES)
			n = SKEIN_256_BLOCK_BYTES;
		Skein_Put64_LSB_First(hashVal + i * SKEIN_256_BLOCK_BYTES,
		    ctx->X, n);	/* "output" the ctr mode bytes */
		Skein_Show_Final(256, &ctx->h, n,
		    hashVal + i * SKEIN_256_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		bcopy(X, ctx->X, sizeof (X));
	}
	return (SKEIN_SUCCESS);
}

/* 512-bit Skein */

/* init the context for a straight hashing operation  */
int
Skein_512_Init(Skein_512_Ctxt_t *ctx, size_t hashBitLen)
{
	union {
		uint8_t b[SKEIN_512_STATE_BYTES];
		uint64_t w[SKEIN_512_STATE_WORDS];
	} cfg;			/* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hashBitLen = hashBitLen;	/* output hash bit count */

	switch (hashBitLen) {	/* use pre-computed values, where available */
#ifndef	SKEIN_NO_PRECOMP
	case 512:
		bcopy(SKEIN_512_IV_512, ctx->X, sizeof (ctx->X));
		break;
	case 384:
		bcopy(SKEIN_512_IV_384, ctx->X, sizeof (ctx->X));
		break;
	case 256:
		bcopy(SKEIN_512_IV_256, ctx->X, sizeof (ctx->X));
		break;
	case 224:
		bcopy(SKEIN_512_IV_224, ctx->X, sizeof (ctx->X));
		break;
#endif
	default:
		/*
		 * here if there is no precomputed IV value available
		 * build/process the config block, type == CONFIG (could be
		 * precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		Skein_Start_New_Type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = Skein_Swap64(hashBitLen);
		cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		bzero(&cfg.w[3], sizeof (cfg) - 3 * sizeof (cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		bzero(ctx->X, sizeof (ctx->X));
		Skein_512_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}

	/*
	 * The chaining vars ctx->X are now initialized for the given
	 * hashBitLen. Set up to process the data message portion of the
	 * hash (default)
	 */
	Skein_Start_New_Type(ctx, MSG);	/* T0=0, T1= MSG type */

	return (SKEIN_SUCCESS);
}

/* init the context for a MAC and/or tree hash operation */
/*
 * [identical to Skein_512_Init() when keyBytes == 0 &&
 * treeInfo == SKEIN_CFG_TREE_INFO_SEQUENTIAL]
 */
int
Skein_512_InitExt(Skein_512_Ctxt_t *ctx, size_t hashBitLen, uint64_t treeInfo,
    const uint8_t *key, size_t keyBytes)
{
	union {
		uint8_t b[SKEIN_512_STATE_BYTES];
		uint64_t w[SKEIN_512_STATE_WORDS];
	} cfg;			/* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	Skein_Assert(keyBytes == 0 || key != NULL, SKEIN_FAIL);

	/* compute the initial chaining values ctx->X[], based on key */
	if (keyBytes == 0) {	/* is there a key? */
		/* no key: use all zeroes as key for config block */
		bzero(ctx->X, sizeof (ctx->X));
	} else {		/* here to pre-process a key */

		Skein_assert(sizeof (cfg.b) >= sizeof (ctx->X));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hashBitLen = 8 * sizeof (ctx->X);
		/* set tweaks: T0 = 0; T1 = KEY type */
		Skein_Start_New_Type(ctx, KEY);
		/* zero the initial chaining variables */
		bzero(ctx->X, sizeof (ctx->X));
		(void) Skein_512_Update(ctx, key, keyBytes); /* hash the key */
		/* put result into cfg.b[] */
		(void) Skein_512_Final_Pad(ctx, cfg.b);
		/* copy over into ctx->X[] */
		bcopy(cfg.b, ctx->X, sizeof (cfg.b));
#if	SKEIN_NEED_SWAP
		{
			uint_t i;
			/* convert key bytes to context words */
			for (i = 0; i < SKEIN_512_STATE_WORDS; i++)
				ctx->X[i] = Skein_Swap64(ctx->X[i]);
		}
#endif
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	ctx->h.hashBitLen = hashBitLen;	/* output hash bit count */
	Skein_Start_New_Type(ctx, CFG_FINAL);

	bzero(&cfg.w, sizeof (cfg.w));	/* pre-pad cfg.w[] with zeroes */
	cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
	cfg.w[1] = Skein_Swap64(hashBitLen);	/* hash result length in bits */
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = Skein_Swap64(treeInfo);

	Skein_Show_Key(512, &ctx->h, key, keyBytes);

	/* compute the initial chaining values from config block */
	Skein_512_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->X are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	ctx->h.bCnt = 0;	/* buffer b[] starts out empty */
	Skein_Start_New_Type(ctx, MSG);

	return (SKEIN_SUCCESS);
}

/* process the input bytes */
int
Skein_512_Update(Skein_512_Ctxt_t *ctx, const uint8_t *msg, size_t msgByteCnt)
{
	size_t n;

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msgByteCnt + ctx->h.bCnt > SKEIN_512_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.bCnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN_512_BLOCK_BYTES - ctx->h.bCnt;
			if (n) {
				/* check on our logic here */
				Skein_assert(n < msgByteCnt);
				bcopy(msg, &ctx->b[ctx->h.bCnt], n);
				msgByteCnt -= n;
				msg += n;
				ctx->h.bCnt += n;
			}
			Skein_assert(ctx->h.bCnt == SKEIN_512_BLOCK_BYTES);
			Skein_512_Process_Block(ctx, ctx->b, 1,
			    SKEIN_512_BLOCK_BYTES);
			ctx->h.bCnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msgByteCnt > SKEIN_512_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msgByteCnt - 1) / SKEIN_512_BLOCK_BYTES;
			Skein_512_Process_Block(ctx, msg, n,
			    SKEIN_512_BLOCK_BYTES);
			msgByteCnt -= n * SKEIN_512_BLOCK_BYTES;
			msg += n * SKEIN_512_BLOCK_BYTES;
		}
		Skein_assert(ctx->h.bCnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msgByteCnt) {
		Skein_assert(msgByteCnt + ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES);
		bcopy(msg, &ctx->b[ctx->h.bCnt], msgByteCnt);
		ctx->h.bCnt += msgByteCnt;
	}

	return (SKEIN_SUCCESS);
}

/* finalize the hash computation and output the result */
int
Skein_512_Final(Skein_512_Ctxt_t *ctx, uint8_t *hashVal)
{
	size_t i, n, byteCnt;
	uint64_t X[SKEIN_512_STATE_WORDS];

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;	/* tag as the final block */
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN_512_BLOCK_BYTES)
		bzero(&ctx->b[ctx->h.bCnt],
		    SKEIN_512_BLOCK_BYTES - ctx->h.bCnt);

	/* process the final block */
	Skein_512_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	bzero(ctx->b, sizeof (ctx->b));
	/* keep a local copy of counter mode "key" */
	bcopy(ctx->X, X, sizeof (X));
	for (i = 0; i * SKEIN_512_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		uint64_t tmp = Skein_Swap64((uint64_t)i);
		bcopy(&tmp, ctx->b, sizeof (tmp));
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein_512_Process_Block(ctx, ctx->b, 1, sizeof (uint64_t));
		/* number of output bytes left to go */
		n = byteCnt - i * SKEIN_512_BLOCK_BYTES;
		if (n >= SKEIN_512_BLOCK_BYTES)
			n = SKEIN_512_BLOCK_BYTES;
		Skein_Put64_LSB_First(hashVal + i * SKEIN_512_BLOCK_BYTES,
		    ctx->X, n);	/* "output" the ctr mode bytes */
		Skein_Show_Final(512, &ctx->h, n,
		    hashVal + i * SKEIN_512_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		bcopy(X, ctx->X, sizeof (X));
	}
	return (SKEIN_SUCCESS);
}

/* 1024-bit Skein */

/* init the context for a straight hashing operation  */
int
Skein1024_Init(Skein1024_Ctxt_t *ctx, size_t hashBitLen)
{
	union {
		uint8_t b[SKEIN1024_STATE_BYTES];
		uint64_t w[SKEIN1024_STATE_WORDS];
	} cfg;			/* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	ctx->h.hashBitLen = hashBitLen;	/* output hash bit count */

	switch (hashBitLen) {	/* use pre-computed values, where available */
#ifndef	SKEIN_NO_PRECOMP
	case 512:
		bcopy(SKEIN1024_IV_512, ctx->X, sizeof (ctx->X));
		break;
	case 384:
		bcopy(SKEIN1024_IV_384, ctx->X, sizeof (ctx->X));
		break;
	case 1024:
		bcopy(SKEIN1024_IV_1024, ctx->X, sizeof (ctx->X));
		break;
#endif
	default:
		/* here if there is no precomputed IV value available */
		/*
		 * build/process the config block, type == CONFIG (could be
		 * precomputed)
		 */
		/* set tweaks: T0=0; T1=CFG | FINAL */
		Skein_Start_New_Type(ctx, CFG_FINAL);

		/* set the schema, version */
		cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
		/* hash result length in bits */
		cfg.w[1] = Skein_Swap64(hashBitLen);
		cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
		/* zero pad config block */
		bzero(&cfg.w[3], sizeof (cfg) - 3 * sizeof (cfg.w[0]));

		/* compute the initial chaining values from config block */
		/* zero the chaining variables */
		bzero(ctx->X, sizeof (ctx->X));
		Skein1024_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);
		break;
	}

	/*
	 * The chaining vars ctx->X are now initialized for the given
	 * hashBitLen. Set up to process the data message portion of the hash
	 * (default)
	 */
	Skein_Start_New_Type(ctx, MSG);	/* T0=0, T1= MSG type */

	return (SKEIN_SUCCESS);
}

/* init the context for a MAC and/or tree hash operation */
/*
 * [identical to Skein1024_Init() when keyBytes == 0 &&
 * treeInfo == SKEIN_CFG_TREE_INFO_SEQUENTIAL]
 */
int
Skein1024_InitExt(Skein1024_Ctxt_t *ctx, size_t hashBitLen, uint64_t treeInfo,
    const uint8_t *key, size_t keyBytes)
{
	union {
		uint8_t b[SKEIN1024_STATE_BYTES];
		uint64_t w[SKEIN1024_STATE_WORDS];
	} cfg;			/* config block */

	Skein_Assert(hashBitLen > 0, SKEIN_BAD_HASHLEN);
	Skein_Assert(keyBytes == 0 || key != NULL, SKEIN_FAIL);

	/* compute the initial chaining values ctx->X[], based on key */
	if (keyBytes == 0) {	/* is there a key? */
		/* no key: use all zeroes as key for config block */
		bzero(ctx->X, sizeof (ctx->X));
	} else {		/* here to pre-process a key */
		Skein_assert(sizeof (cfg.b) >= sizeof (ctx->X));
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hashBitLen = 8 * sizeof (ctx->X);
		/* set tweaks: T0 = 0; T1 = KEY type */
		Skein_Start_New_Type(ctx, KEY);
		/* zero the initial chaining variables */
		bzero(ctx->X, sizeof (ctx->X));
		(void) Skein1024_Update(ctx, key, keyBytes); /* hash the key */
		/* put result into cfg.b[] */
		(void) Skein1024_Final_Pad(ctx, cfg.b);
		/* copy over into ctx->X[] */
		bcopy(cfg.b, ctx->X, sizeof (cfg.b));
#if	SKEIN_NEED_SWAP
		{
			uint_t i;
			/* convert key bytes to context words */
			for (i = 0; i < SKEIN1024_STATE_WORDS; i++)
				ctx->X[i] = Skein_Swap64(ctx->X[i]);
		}
#endif
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	ctx->h.hashBitLen = hashBitLen;	/* output hash bit count */
	Skein_Start_New_Type(ctx, CFG_FINAL);

	bzero(&cfg.w, sizeof (cfg.w));	/* pre-pad cfg.w[] with zeroes */
	cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
	/* hash result length in bits */
	cfg.w[1] = Skein_Swap64(hashBitLen);
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = Skein_Swap64(treeInfo);

	Skein_Show_Key(1024, &ctx->h, key, keyBytes);

	/* compute the initial chaining values from config block */
	Skein1024_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->X are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	ctx->h.bCnt = 0;	/* buffer b[] starts out empty */
	Skein_Start_New_Type(ctx, MSG);

	return (SKEIN_SUCCESS);
}

/* process the input bytes */
int
Skein1024_Update(Skein1024_Ctxt_t *ctx, const uint8_t *msg, size_t msgByteCnt)
{
	size_t n;

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	/* process full blocks, if any */
	if (msgByteCnt + ctx->h.bCnt > SKEIN1024_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.bCnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN1024_BLOCK_BYTES - ctx->h.bCnt;
			if (n) {
				/* check on our logic here */
				Skein_assert(n < msgByteCnt);
				bcopy(msg, &ctx->b[ctx->h.bCnt], n);
				msgByteCnt -= n;
				msg += n;
				ctx->h.bCnt += n;
			}
			Skein_assert(ctx->h.bCnt == SKEIN1024_BLOCK_BYTES);
			Skein1024_Process_Block(ctx, ctx->b, 1,
			    SKEIN1024_BLOCK_BYTES);
			ctx->h.bCnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from
		 * input message data
		 */
		if (msgByteCnt > SKEIN1024_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msgByteCnt - 1) / SKEIN1024_BLOCK_BYTES;
			Skein1024_Process_Block(ctx, msg, n,
			    SKEIN1024_BLOCK_BYTES);
			msgByteCnt -= n * SKEIN1024_BLOCK_BYTES;
			msg += n * SKEIN1024_BLOCK_BYTES;
		}
		Skein_assert(ctx->h.bCnt == 0);
	}

	/* copy any remaining source message data bytes into b[] */
	if (msgByteCnt) {
		Skein_assert(msgByteCnt + ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES);
		bcopy(msg, &ctx->b[ctx->h.bCnt], msgByteCnt);
		ctx->h.bCnt += msgByteCnt;
	}

	return (SKEIN_SUCCESS);
}

/* finalize the hash computation and output the result */
int
Skein1024_Final(Skein1024_Ctxt_t *ctx, uint8_t *hashVal)
{
	size_t i, n, byteCnt;
	uint64_t X[SKEIN1024_STATE_WORDS];

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;	/* tag as the final block */
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN1024_BLOCK_BYTES)
		bzero(&ctx->b[ctx->h.bCnt],
		    SKEIN1024_BLOCK_BYTES - ctx->h.bCnt);

	/* process the final block */
	Skein1024_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	bzero(ctx->b, sizeof (ctx->b));
	/* keep a local copy of counter mode "key" */
	bcopy(ctx->X, X, sizeof (X));
	for (i = 0; i * SKEIN1024_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		uint64_t tmp = Skein_Swap64((uint64_t)i);
		bcopy(&tmp, ctx->b, sizeof (tmp));
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein1024_Process_Block(ctx, ctx->b, 1, sizeof (uint64_t));
		/* number of output bytes left to go */
		n = byteCnt - i * SKEIN1024_BLOCK_BYTES;
		if (n >= SKEIN1024_BLOCK_BYTES)
			n = SKEIN1024_BLOCK_BYTES;
		Skein_Put64_LSB_First(hashVal + i * SKEIN1024_BLOCK_BYTES,
		    ctx->X, n);	/* "output" the ctr mode bytes */
		Skein_Show_Final(1024, &ctx->h, n,
		    hashVal + i * SKEIN1024_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		bcopy(X, ctx->X, sizeof (X));
	}
	return (SKEIN_SUCCESS);
}

/* Functions to support MAC/tree hashing */
/* (this code is identical for Optimized and Reference versions) */

/* finalize the hash computation and output the block, no OUTPUT stage */
int
Skein_256_Final_Pad(Skein_256_Ctxt_t *ctx, uint8_t *hashVal)
{
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;	/* tag as the final block */
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN_256_BLOCK_BYTES)
		bzero(&ctx->b[ctx->h.bCnt],
		    SKEIN_256_BLOCK_BYTES - ctx->h.bCnt);
	/* process the final block */
	Skein_256_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* "output" the state bytes */
	Skein_Put64_LSB_First(hashVal, ctx->X, SKEIN_256_BLOCK_BYTES);

	return (SKEIN_SUCCESS);
}

/* finalize the hash computation and output the block, no OUTPUT stage */
int
Skein_512_Final_Pad(Skein_512_Ctxt_t *ctx, uint8_t *hashVal)
{
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;	/* tag as the final block */
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN_512_BLOCK_BYTES)
		bzero(&ctx->b[ctx->h.bCnt],
		    SKEIN_512_BLOCK_BYTES - ctx->h.bCnt);
	/* process the final block */
	Skein_512_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* "output" the state bytes */
	Skein_Put64_LSB_First(hashVal, ctx->X, SKEIN_512_BLOCK_BYTES);

	return (SKEIN_SUCCESS);
}

/* finalize the hash computation and output the block, no OUTPUT stage */
int
Skein1024_Final_Pad(Skein1024_Ctxt_t *ctx, uint8_t *hashVal)
{
	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	/* tag as the final block */
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;
	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN1024_BLOCK_BYTES)
		bzero(&ctx->b[ctx->h.bCnt],
		    SKEIN1024_BLOCK_BYTES - ctx->h.bCnt);
	/* process the final block */
	Skein1024_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* "output" the state bytes */
	Skein_Put64_LSB_First(hashVal, ctx->X, SKEIN1024_BLOCK_BYTES);

	return (SKEIN_SUCCESS);
}

#if	SKEIN_TREE_HASH
/* just do the OUTPUT stage */
int
Skein_256_Output(Skein_256_Ctxt_t *ctx, uint8_t *hashVal)
{
	size_t i, n, byteCnt;
	uint64_t X[SKEIN_256_STATE_WORDS];

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	bzero(ctx->b, sizeof (ctx->b));
	/* keep a local copy of counter mode "key" */
	bcopy(ctx->X, X, sizeof (X));
	for (i = 0; i * SKEIN_256_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		uint64_t tmp = Skein_Swap64((uint64_t)i);
		bcopy(&tmp, ctx->b, sizeof (tmp));
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein_256_Process_Block(ctx, ctx->b, 1, sizeof (uint64_t));
		/* number of output bytes left to go */
		n = byteCnt - i * SKEIN_256_BLOCK_BYTES;
		if (n >= SKEIN_256_BLOCK_BYTES)
			n = SKEIN_256_BLOCK_BYTES;
		Skein_Put64_LSB_First(hashVal + i * SKEIN_256_BLOCK_BYTES,
		    ctx->X, n);	/* "output" the ctr mode bytes */
		Skein_Show_Final(256, &ctx->h, n,
		    hashVal + i * SKEIN_256_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		bcopy(X, ctx->X, sizeof (X));
	}
	return (SKEIN_SUCCESS);
}

/* just do the OUTPUT stage */
int
Skein_512_Output(Skein_512_Ctxt_t *ctx, uint8_t *hashVal)
{
	size_t i, n, byteCnt;
	uint64_t X[SKEIN_512_STATE_WORDS];

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	bzero(ctx->b, sizeof (ctx->b));
	/* keep a local copy of counter mode "key" */
	bcopy(ctx->X, X, sizeof (X));
	for (i = 0; i * SKEIN_512_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		uint64_t tmp = Skein_Swap64((uint64_t)i);
		bcopy(&tmp, ctx->b, sizeof (tmp));
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein_512_Process_Block(ctx, ctx->b, 1, sizeof (uint64_t));
		/* number of output bytes left to go */
		n = byteCnt - i * SKEIN_512_BLOCK_BYTES;
		if (n >= SKEIN_512_BLOCK_BYTES)
			n = SKEIN_512_BLOCK_BYTES;
		Skein_Put64_LSB_First(hashVal + i * SKEIN_512_BLOCK_BYTES,
		    ctx->X, n);	/* "output" the ctr mode bytes */
		Skein_Show_Final(256, &ctx->h, n,
		    hashVal + i * SKEIN_512_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		bcopy(X, ctx->X, sizeof (X));
	}
	return (SKEIN_SUCCESS);
}

/* just do the OUTPUT stage */
int
Skein1024_Output(Skein1024_Ctxt_t *ctx, uint8_t *hashVal)
{
	size_t i, n, byteCnt;
	uint64_t X[SKEIN1024_STATE_WORDS];

	/* catch uninitialized context */
	Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES, SKEIN_FAIL);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	bzero(ctx->b, sizeof (ctx->b));
	/* keep a local copy of counter mode "key" */
	bcopy(ctx->X, X, sizeof (X));
	for (i = 0; i * SKEIN1024_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		uint64_t tmp = Skein_Swap64((uint64_t)i);
		bcopy(&tmp, ctx->b, sizeof (tmp));
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein1024_Process_Block(ctx, ctx->b, 1, sizeof (uint64_t));
		/* number of output bytes left to go */
		n = byteCnt - i * SKEIN1024_BLOCK_BYTES;
		if (n >= SKEIN1024_BLOCK_BYTES)
			n = SKEIN1024_BLOCK_BYTES;
		Skein_Put64_LSB_First(hashVal + i * SKEIN1024_BLOCK_BYTES,
		    ctx->X, n);	/* "output" the ctr mode bytes */
		Skein_Show_Final(256, &ctx->h, n,
		    hashVal + i * SKEIN1024_BLOCK_BYTES);
		/* restore the counter mode key for next time */
		bcopy(X, ctx->X, sizeof (X));
	}
	return (SKEIN_SUCCESS);
}
#endif

#ifdef _KERNEL
EXPORT_SYMBOL(Skein_512_Init);
EXPORT_SYMBOL(Skein_512_InitExt);
EXPORT_SYMBOL(Skein_512_Update);
EXPORT_SYMBOL(Skein_512_Final);
#endif
