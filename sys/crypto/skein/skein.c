/***********************************************************************
**
** Implementation of the Skein hash function.
**
** Source code author: Doug Whiting, 2008.
**
** This algorithm and source code is released to the public domain.
** 
************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/types.h>

/* get the memcpy/memset functions */
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif

#define  SKEIN_PORT_CODE /* instantiate any code in skein_port.h */

#include "skein.h"       /* get the Skein API definitions   */
#include "skein_iv.h"    /* get precomputed IVs */

/*****************************************************************/
/* External function to process blkCnt (nonzero) full block(s) of data. */
void    Skein_256_Process_Block(Skein_256_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t byteCntAdd);
void    Skein_512_Process_Block(Skein_512_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t byteCntAdd);
void    Skein1024_Process_Block(Skein1024_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t byteCntAdd);

/*****************************************************************/
/*     256-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int Skein_256_Init(Skein_256_Ctxt_t *ctx, size_t hashBitLen)
    {
    union
        {
        u08b_t  b[SKEIN_256_STATE_BYTES];
        u64b_t  w[SKEIN_256_STATE_WORDS];
        } cfg;                              /* config block */
        
    Skein_Assert(hashBitLen > 0,SKEIN_BAD_HASHLEN);
    ctx->h.hashBitLen = hashBitLen;         /* output hash bit count */

    switch (hashBitLen)
        {             /* use pre-computed values, where available */
#ifndef SKEIN_NO_PRECOMP
        case  256: memcpy(ctx->X,SKEIN_256_IV_256,sizeof(ctx->X));  break;
        case  224: memcpy(ctx->X,SKEIN_256_IV_224,sizeof(ctx->X));  break;
        case  160: memcpy(ctx->X,SKEIN_256_IV_160,sizeof(ctx->X));  break;
        case  128: memcpy(ctx->X,SKEIN_256_IV_128,sizeof(ctx->X));  break;
#endif
        default:
            /* here if there is no precomputed IV value available */
            /* build/process the config block, type == CONFIG (could be precomputed) */
            Skein_Start_New_Type(ctx,CFG_FINAL);        /* set tweaks: T0=0; T1=CFG | FINAL */

            cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);  /* set the schema, version */
            cfg.w[1] = Skein_Swap64(hashBitLen);        /* hash result length in bits */
            cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
            memset(&cfg.w[3],0,sizeof(cfg) - 3*sizeof(cfg.w[0])); /* zero pad config block */

            /* compute the initial chaining values from config block */
            memset(ctx->X,0,sizeof(ctx->X));            /* zero the chaining variables */
            Skein_256_Process_Block(ctx,cfg.b,1,SKEIN_CFG_STR_LEN);
            break;
        }
    /* The chaining vars ctx->X are now initialized for the given hashBitLen. */
    /* Set up to process the data message portion of the hash (default) */
    Skein_Start_New_Type(ctx,MSG);              /* T0=0, T1= MSG type */

    return SKEIN_SUCCESS;
    }

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/* [identical to Skein_256_Init() when keyBytes == 0 && treeInfo == SKEIN_CFG_TREE_INFO_SEQUENTIAL] */
int Skein_256_InitExt(Skein_256_Ctxt_t *ctx,size_t hashBitLen,u64b_t treeInfo, const u08b_t *key, size_t keyBytes)
    {
    union
        {
        u08b_t  b[SKEIN_256_STATE_BYTES];
        u64b_t  w[SKEIN_256_STATE_WORDS];
        } cfg;                              /* config block */
        
    Skein_Assert(hashBitLen > 0,SKEIN_BAD_HASHLEN);
    Skein_Assert(keyBytes == 0 || key != NULL,SKEIN_FAIL);

    /* compute the initial chaining values ctx->X[], based on key */
    if (keyBytes == 0)                          /* is there a key? */
        {                                   
        memset(ctx->X,0,sizeof(ctx->X));        /* no key: use all zeroes as key for config block */
        }
    else                                        /* here to pre-process a key */
        {
        Skein_assert(sizeof(cfg.b) >= sizeof(ctx->X));
        /* do a mini-Init right here */
        ctx->h.hashBitLen=8*sizeof(ctx->X);     /* set output hash bit count = state size */
        Skein_Start_New_Type(ctx,KEY);          /* set tweaks: T0 = 0; T1 = KEY type */
        memset(ctx->X,0,sizeof(ctx->X));        /* zero the initial chaining variables */
        Skein_256_Update(ctx,key,keyBytes);     /* hash the key */
        Skein_256_Final_Pad(ctx,cfg.b);         /* put result into cfg.b[] */
        memcpy(ctx->X,cfg.b,sizeof(cfg.b));     /* copy over into ctx->X[] */
#if SKEIN_NEED_SWAP
        {
        uint_t i;
        for (i=0;i<SKEIN_256_STATE_WORDS;i++)   /* convert key bytes to context words */
            ctx->X[i] = Skein_Swap64(ctx->X[i]);
        }
#endif
        }
    /* build/process the config block, type == CONFIG (could be precomputed for each key) */
    ctx->h.hashBitLen = hashBitLen;             /* output hash bit count */
    Skein_Start_New_Type(ctx,CFG_FINAL);

    memset(&cfg.w,0,sizeof(cfg.w));             /* pre-pad cfg.w[] with zeroes */
    cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
    cfg.w[1] = Skein_Swap64(hashBitLen);        /* hash result length in bits */
    cfg.w[2] = Skein_Swap64(treeInfo);          /* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */

    Skein_Show_Key(256,&ctx->h,key,keyBytes);

    /* compute the initial chaining values from config block */
    Skein_256_Process_Block(ctx,cfg.b,1,SKEIN_CFG_STR_LEN);

    /* The chaining vars ctx->X are now initialized */
    /* Set up to process the data message portion of the hash (default) */
    ctx->h.bCnt = 0;                            /* buffer b[] starts out empty */
    Skein_Start_New_Type(ctx,MSG);
    
    return SKEIN_SUCCESS;
    }

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int Skein_256_Update(Skein_256_Ctxt_t *ctx, const u08b_t *msg, size_t msgByteCnt)
    {
    size_t n;

    Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    /* process full blocks, if any */
    if (msgByteCnt + ctx->h.bCnt > SKEIN_256_BLOCK_BYTES)
        {
        if (ctx->h.bCnt)                              /* finish up any buffered message data */
            {
            n = SKEIN_256_BLOCK_BYTES - ctx->h.bCnt;  /* # bytes free in buffer b[] */
            if (n)
                {
                Skein_assert(n < msgByteCnt);         /* check on our logic here */
                memcpy(&ctx->b[ctx->h.bCnt],msg,n);
                msgByteCnt  -= n;
                msg         += n;
                ctx->h.bCnt += n;
                }
            Skein_assert(ctx->h.bCnt == SKEIN_256_BLOCK_BYTES);
            Skein_256_Process_Block(ctx,ctx->b,1,SKEIN_256_BLOCK_BYTES);
            ctx->h.bCnt = 0;
            }
        /* now process any remaining full blocks, directly from input message data */
        if (msgByteCnt > SKEIN_256_BLOCK_BYTES)
            {
            n = (msgByteCnt-1) / SKEIN_256_BLOCK_BYTES;   /* number of full blocks to process */
            Skein_256_Process_Block(ctx,msg,n,SKEIN_256_BLOCK_BYTES);
            msgByteCnt -= n * SKEIN_256_BLOCK_BYTES;
            msg        += n * SKEIN_256_BLOCK_BYTES;
            }
        Skein_assert(ctx->h.bCnt == 0);
        }

    /* copy any remaining source message data bytes into b[] */
    if (msgByteCnt)
        {
        Skein_assert(msgByteCnt + ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES);
        memcpy(&ctx->b[ctx->h.bCnt],msg,msgByteCnt);
        ctx->h.bCnt += msgByteCnt;
        }

    return SKEIN_SUCCESS;
    }
   
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int Skein_256_Final(Skein_256_Ctxt_t *ctx, u08b_t *hashVal)
    {
    size_t i,n,byteCnt;
    u64b_t X[SKEIN_256_STATE_WORDS];
    Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;                 /* tag as the final block */
    if (ctx->h.bCnt < SKEIN_256_BLOCK_BYTES)            /* zero pad b[] if necessary */
        memset(&ctx->b[ctx->h.bCnt],0,SKEIN_256_BLOCK_BYTES - ctx->h.bCnt);

    Skein_256_Process_Block(ctx,ctx->b,1,ctx->h.bCnt);  /* process the final block */
    
    /* now output the result */
    byteCnt = (ctx->h.hashBitLen + 7) >> 3;             /* total number of output bytes */

    /* run Threefish in "counter mode" to generate output */
    memset(ctx->b,0,sizeof(ctx->b));  /* zero out b[], so it can hold the counter */
    memcpy(X,ctx->X,sizeof(X));       /* keep a local copy of counter mode "key" */
    for (i=0;i*SKEIN_256_BLOCK_BYTES < byteCnt;i++)
        {
        ((u64b_t *)ctx->b)[0]= Skein_Swap64((u64b_t) i); /* build the counter block */
        Skein_Start_New_Type(ctx,OUT_FINAL);
        Skein_256_Process_Block(ctx,ctx->b,1,sizeof(u64b_t)); /* run "counter mode" */
        n = byteCnt - i*SKEIN_256_BLOCK_BYTES;   /* number of output bytes left to go */
        if (n >= SKEIN_256_BLOCK_BYTES)
            n  = SKEIN_256_BLOCK_BYTES;
        Skein_Put64_LSB_First(hashVal+i*SKEIN_256_BLOCK_BYTES,ctx->X,n);   /* "output" the ctr mode bytes */
        Skein_Show_Final(256,&ctx->h,n,hashVal+i*SKEIN_256_BLOCK_BYTES);
        memcpy(ctx->X,X,sizeof(X));   /* restore the counter mode key for next time */
        }
    return SKEIN_SUCCESS;
    }

#if defined(SKEIN_CODE_SIZE) || defined(SKEIN_PERF)
size_t Skein_256_API_CodeSize(void)
    {
    return ((u08b_t *) Skein_256_API_CodeSize) -
           ((u08b_t *) Skein_256_Init);
    }
#endif

/*****************************************************************/
/*     512-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int Skein_512_Init(Skein_512_Ctxt_t *ctx, size_t hashBitLen)
    {
    union
        {
        u08b_t  b[SKEIN_512_STATE_BYTES];
        u64b_t  w[SKEIN_512_STATE_WORDS];
        } cfg;                              /* config block */
        
    Skein_Assert(hashBitLen > 0,SKEIN_BAD_HASHLEN);
    ctx->h.hashBitLen = hashBitLen;         /* output hash bit count */

    switch (hashBitLen)
        {             /* use pre-computed values, where available */
#ifndef SKEIN_NO_PRECOMP
        case  512: memcpy(ctx->X,SKEIN_512_IV_512,sizeof(ctx->X));  break;
        case  384: memcpy(ctx->X,SKEIN_512_IV_384,sizeof(ctx->X));  break;
        case  256: memcpy(ctx->X,SKEIN_512_IV_256,sizeof(ctx->X));  break;
        case  224: memcpy(ctx->X,SKEIN_512_IV_224,sizeof(ctx->X));  break;
#endif
        default:
            /* here if there is no precomputed IV value available */
            /* build/process the config block, type == CONFIG (could be precomputed) */
            Skein_Start_New_Type(ctx,CFG_FINAL);        /* set tweaks: T0=0; T1=CFG | FINAL */

            cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);  /* set the schema, version */
            cfg.w[1] = Skein_Swap64(hashBitLen);        /* hash result length in bits */
            cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
            memset(&cfg.w[3],0,sizeof(cfg) - 3*sizeof(cfg.w[0])); /* zero pad config block */

            /* compute the initial chaining values from config block */
            memset(ctx->X,0,sizeof(ctx->X));            /* zero the chaining variables */
            Skein_512_Process_Block(ctx,cfg.b,1,SKEIN_CFG_STR_LEN);
            break;
        }

    /* The chaining vars ctx->X are now initialized for the given hashBitLen. */
    /* Set up to process the data message portion of the hash (default) */
    Skein_Start_New_Type(ctx,MSG);              /* T0=0, T1= MSG type */

    return SKEIN_SUCCESS;
    }

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/* [identical to Skein_512_Init() when keyBytes == 0 && treeInfo == SKEIN_CFG_TREE_INFO_SEQUENTIAL] */
int Skein_512_InitExt(Skein_512_Ctxt_t *ctx,size_t hashBitLen,u64b_t treeInfo, const u08b_t *key, size_t keyBytes)
    {
    union
        {
        u08b_t  b[SKEIN_512_STATE_BYTES];
        u64b_t  w[SKEIN_512_STATE_WORDS];
        } cfg;                              /* config block */
        
    Skein_Assert(hashBitLen > 0,SKEIN_BAD_HASHLEN);
    Skein_Assert(keyBytes == 0 || key != NULL,SKEIN_FAIL);

    /* compute the initial chaining values ctx->X[], based on key */
    if (keyBytes == 0)                          /* is there a key? */
        {                                   
        memset(ctx->X,0,sizeof(ctx->X));        /* no key: use all zeroes as key for config block */
        }
    else                                        /* here to pre-process a key */
        {
        Skein_assert(sizeof(cfg.b) >= sizeof(ctx->X));
        /* do a mini-Init right here */
        ctx->h.hashBitLen=8*sizeof(ctx->X);     /* set output hash bit count = state size */
        Skein_Start_New_Type(ctx,KEY);          /* set tweaks: T0 = 0; T1 = KEY type */
        memset(ctx->X,0,sizeof(ctx->X));        /* zero the initial chaining variables */
        Skein_512_Update(ctx,key,keyBytes);     /* hash the key */
        Skein_512_Final_Pad(ctx,cfg.b);         /* put result into cfg.b[] */
        memcpy(ctx->X,cfg.b,sizeof(cfg.b));     /* copy over into ctx->X[] */
#if SKEIN_NEED_SWAP
        {
        uint_t i;
        for (i=0;i<SKEIN_512_STATE_WORDS;i++)   /* convert key bytes to context words */
            ctx->X[i] = Skein_Swap64(ctx->X[i]);
        }
#endif
        }
    /* build/process the config block, type == CONFIG (could be precomputed for each key) */
    ctx->h.hashBitLen = hashBitLen;             /* output hash bit count */
    Skein_Start_New_Type(ctx,CFG_FINAL);

    memset(&cfg.w,0,sizeof(cfg.w));             /* pre-pad cfg.w[] with zeroes */
    cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
    cfg.w[1] = Skein_Swap64(hashBitLen);        /* hash result length in bits */
    cfg.w[2] = Skein_Swap64(treeInfo);          /* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */

    Skein_Show_Key(512,&ctx->h,key,keyBytes);

    /* compute the initial chaining values from config block */
    Skein_512_Process_Block(ctx,cfg.b,1,SKEIN_CFG_STR_LEN);

    /* The chaining vars ctx->X are now initialized */
    /* Set up to process the data message portion of the hash (default) */
    ctx->h.bCnt = 0;                            /* buffer b[] starts out empty */
    Skein_Start_New_Type(ctx,MSG);
    
    return SKEIN_SUCCESS;
    }

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int Skein_512_Update(Skein_512_Ctxt_t *ctx, const u08b_t *msg, size_t msgByteCnt)
    {
    size_t n;

    Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    /* process full blocks, if any */
    if (msgByteCnt + ctx->h.bCnt > SKEIN_512_BLOCK_BYTES)
        {
        if (ctx->h.bCnt)                              /* finish up any buffered message data */
            {
            n = SKEIN_512_BLOCK_BYTES - ctx->h.bCnt;  /* # bytes free in buffer b[] */
            if (n)
                {
                Skein_assert(n < msgByteCnt);         /* check on our logic here */
                memcpy(&ctx->b[ctx->h.bCnt],msg,n);
                msgByteCnt  -= n;
                msg         += n;
                ctx->h.bCnt += n;
                }
            Skein_assert(ctx->h.bCnt == SKEIN_512_BLOCK_BYTES);
            Skein_512_Process_Block(ctx,ctx->b,1,SKEIN_512_BLOCK_BYTES);
            ctx->h.bCnt = 0;
            }
        /* now process any remaining full blocks, directly from input message data */
        if (msgByteCnt > SKEIN_512_BLOCK_BYTES)
            {
            n = (msgByteCnt-1) / SKEIN_512_BLOCK_BYTES;   /* number of full blocks to process */
            Skein_512_Process_Block(ctx,msg,n,SKEIN_512_BLOCK_BYTES);
            msgByteCnt -= n * SKEIN_512_BLOCK_BYTES;
            msg        += n * SKEIN_512_BLOCK_BYTES;
            }
        Skein_assert(ctx->h.bCnt == 0);
        }

    /* copy any remaining source message data bytes into b[] */
    if (msgByteCnt)
        {
        Skein_assert(msgByteCnt + ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES);
        memcpy(&ctx->b[ctx->h.bCnt],msg,msgByteCnt);
        ctx->h.bCnt += msgByteCnt;
        }

    return SKEIN_SUCCESS;
    }
   
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int Skein_512_Final(Skein_512_Ctxt_t *ctx, u08b_t *hashVal)
    {
    size_t i,n,byteCnt;
    u64b_t X[SKEIN_512_STATE_WORDS];
    Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;                 /* tag as the final block */
    if (ctx->h.bCnt < SKEIN_512_BLOCK_BYTES)            /* zero pad b[] if necessary */
        memset(&ctx->b[ctx->h.bCnt],0,SKEIN_512_BLOCK_BYTES - ctx->h.bCnt);

    Skein_512_Process_Block(ctx,ctx->b,1,ctx->h.bCnt);  /* process the final block */
    
    /* now output the result */
    byteCnt = (ctx->h.hashBitLen + 7) >> 3;             /* total number of output bytes */

    /* run Threefish in "counter mode" to generate output */
    memset(ctx->b,0,sizeof(ctx->b));  /* zero out b[], so it can hold the counter */
    memcpy(X,ctx->X,sizeof(X));       /* keep a local copy of counter mode "key" */
    for (i=0;i*SKEIN_512_BLOCK_BYTES < byteCnt;i++)
        {
        ((u64b_t *)ctx->b)[0]= Skein_Swap64((u64b_t) i); /* build the counter block */
        Skein_Start_New_Type(ctx,OUT_FINAL);
        Skein_512_Process_Block(ctx,ctx->b,1,sizeof(u64b_t)); /* run "counter mode" */
        n = byteCnt - i*SKEIN_512_BLOCK_BYTES;   /* number of output bytes left to go */
        if (n >= SKEIN_512_BLOCK_BYTES)
            n  = SKEIN_512_BLOCK_BYTES;
        Skein_Put64_LSB_First(hashVal+i*SKEIN_512_BLOCK_BYTES,ctx->X,n);   /* "output" the ctr mode bytes */
        Skein_Show_Final(512,&ctx->h,n,hashVal+i*SKEIN_512_BLOCK_BYTES);
        memcpy(ctx->X,X,sizeof(X));   /* restore the counter mode key for next time */
        }
    return SKEIN_SUCCESS;
    }

#if defined(SKEIN_CODE_SIZE) || defined(SKEIN_PERF)
size_t Skein_512_API_CodeSize(void)
    {
    return ((u08b_t *) Skein_512_API_CodeSize) -
           ((u08b_t *) Skein_512_Init);
    }
#endif

/*****************************************************************/
/*    1024-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int Skein1024_Init(Skein1024_Ctxt_t *ctx, size_t hashBitLen)
    {
    union
        {
        u08b_t  b[SKEIN1024_STATE_BYTES];
        u64b_t  w[SKEIN1024_STATE_WORDS];
        } cfg;                              /* config block */
        
    Skein_Assert(hashBitLen > 0,SKEIN_BAD_HASHLEN);
    ctx->h.hashBitLen = hashBitLen;         /* output hash bit count */

    switch (hashBitLen)
        {              /* use pre-computed values, where available */
#ifndef SKEIN_NO_PRECOMP
        case  512: memcpy(ctx->X,SKEIN1024_IV_512 ,sizeof(ctx->X)); break;
        case  384: memcpy(ctx->X,SKEIN1024_IV_384 ,sizeof(ctx->X)); break;
        case 1024: memcpy(ctx->X,SKEIN1024_IV_1024,sizeof(ctx->X)); break;
#endif
        default:
            /* here if there is no precomputed IV value available */
            /* build/process the config block, type == CONFIG (could be precomputed) */
            Skein_Start_New_Type(ctx,CFG_FINAL);        /* set tweaks: T0=0; T1=CFG | FINAL */

            cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);  /* set the schema, version */
            cfg.w[1] = Skein_Swap64(hashBitLen);        /* hash result length in bits */
            cfg.w[2] = Skein_Swap64(SKEIN_CFG_TREE_INFO_SEQUENTIAL);
            memset(&cfg.w[3],0,sizeof(cfg) - 3*sizeof(cfg.w[0])); /* zero pad config block */

            /* compute the initial chaining values from config block */
            memset(ctx->X,0,sizeof(ctx->X));            /* zero the chaining variables */
            Skein1024_Process_Block(ctx,cfg.b,1,SKEIN_CFG_STR_LEN);
            break;
        }

    /* The chaining vars ctx->X are now initialized for the given hashBitLen. */
    /* Set up to process the data message portion of the hash (default) */
    Skein_Start_New_Type(ctx,MSG);              /* T0=0, T1= MSG type */

    return SKEIN_SUCCESS;
    }

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a MAC and/or tree hash operation */
/* [identical to Skein1024_Init() when keyBytes == 0 && treeInfo == SKEIN_CFG_TREE_INFO_SEQUENTIAL] */
int Skein1024_InitExt(Skein1024_Ctxt_t *ctx,size_t hashBitLen,u64b_t treeInfo, const u08b_t *key, size_t keyBytes)
    {
    union
        {
        u08b_t  b[SKEIN1024_STATE_BYTES];
        u64b_t  w[SKEIN1024_STATE_WORDS];
        } cfg;                              /* config block */
        
    Skein_Assert(hashBitLen > 0,SKEIN_BAD_HASHLEN);
    Skein_Assert(keyBytes == 0 || key != NULL,SKEIN_FAIL);

    /* compute the initial chaining values ctx->X[], based on key */
    if (keyBytes == 0)                          /* is there a key? */
        {                                   
        memset(ctx->X,0,sizeof(ctx->X));        /* no key: use all zeroes as key for config block */
        }
    else                                        /* here to pre-process a key */
        {
        Skein_assert(sizeof(cfg.b) >= sizeof(ctx->X));
        /* do a mini-Init right here */
        ctx->h.hashBitLen=8*sizeof(ctx->X);     /* set output hash bit count = state size */
        Skein_Start_New_Type(ctx,KEY);          /* set tweaks: T0 = 0; T1 = KEY type */
        memset(ctx->X,0,sizeof(ctx->X));        /* zero the initial chaining variables */
        Skein1024_Update(ctx,key,keyBytes);     /* hash the key */
        Skein1024_Final_Pad(ctx,cfg.b);         /* put result into cfg.b[] */
        memcpy(ctx->X,cfg.b,sizeof(cfg.b));     /* copy over into ctx->X[] */
#if SKEIN_NEED_SWAP
        {
        uint_t i;
        for (i=0;i<SKEIN1024_STATE_WORDS;i++)   /* convert key bytes to context words */
            ctx->X[i] = Skein_Swap64(ctx->X[i]);
        }
#endif
        }
    /* build/process the config block, type == CONFIG (could be precomputed for each key) */
    ctx->h.hashBitLen = hashBitLen;             /* output hash bit count */
    Skein_Start_New_Type(ctx,CFG_FINAL);

    memset(&cfg.w,0,sizeof(cfg.w));             /* pre-pad cfg.w[] with zeroes */
    cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
    cfg.w[1] = Skein_Swap64(hashBitLen);        /* hash result length in bits */
    cfg.w[2] = Skein_Swap64(treeInfo);          /* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */

    Skein_Show_Key(1024,&ctx->h,key,keyBytes);

    /* compute the initial chaining values from config block */
    Skein1024_Process_Block(ctx,cfg.b,1,SKEIN_CFG_STR_LEN);

    /* The chaining vars ctx->X are now initialized */
    /* Set up to process the data message portion of the hash (default) */
    ctx->h.bCnt = 0;                            /* buffer b[] starts out empty */
    Skein_Start_New_Type(ctx,MSG);
    
    return SKEIN_SUCCESS;
    }

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int Skein1024_Update(Skein1024_Ctxt_t *ctx, const u08b_t *msg, size_t msgByteCnt)
    {
    size_t n;

    Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    /* process full blocks, if any */
    if (msgByteCnt + ctx->h.bCnt > SKEIN1024_BLOCK_BYTES)
        {
        if (ctx->h.bCnt)                              /* finish up any buffered message data */
            {
            n = SKEIN1024_BLOCK_BYTES - ctx->h.bCnt;  /* # bytes free in buffer b[] */
            if (n)
                {
                Skein_assert(n < msgByteCnt);         /* check on our logic here */
                memcpy(&ctx->b[ctx->h.bCnt],msg,n);
                msgByteCnt  -= n;
                msg         += n;
                ctx->h.bCnt += n;
                }
            Skein_assert(ctx->h.bCnt == SKEIN1024_BLOCK_BYTES);
            Skein1024_Process_Block(ctx,ctx->b,1,SKEIN1024_BLOCK_BYTES);
            ctx->h.bCnt = 0;
            }
        /* now process any remaining full blocks, directly from input message data */
        if (msgByteCnt > SKEIN1024_BLOCK_BYTES)
            {
            n = (msgByteCnt-1) / SKEIN1024_BLOCK_BYTES;   /* number of full blocks to process */
            Skein1024_Process_Block(ctx,msg,n,SKEIN1024_BLOCK_BYTES);
            msgByteCnt -= n * SKEIN1024_BLOCK_BYTES;
            msg        += n * SKEIN1024_BLOCK_BYTES;
            }
        Skein_assert(ctx->h.bCnt == 0);
        }

    /* copy any remaining source message data bytes into b[] */
    if (msgByteCnt)
        {
        Skein_assert(msgByteCnt + ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES);
        memcpy(&ctx->b[ctx->h.bCnt],msg,msgByteCnt);
        ctx->h.bCnt += msgByteCnt;
        }

    return SKEIN_SUCCESS;
    }
   
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int Skein1024_Final(Skein1024_Ctxt_t *ctx, u08b_t *hashVal)
    {
    size_t i,n,byteCnt;
    u64b_t X[SKEIN1024_STATE_WORDS];
    Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;                 /* tag as the final block */
    if (ctx->h.bCnt < SKEIN1024_BLOCK_BYTES)            /* zero pad b[] if necessary */
        memset(&ctx->b[ctx->h.bCnt],0,SKEIN1024_BLOCK_BYTES - ctx->h.bCnt);

    Skein1024_Process_Block(ctx,ctx->b,1,ctx->h.bCnt);  /* process the final block */
    
    /* now output the result */
    byteCnt = (ctx->h.hashBitLen + 7) >> 3;             /* total number of output bytes */

    /* run Threefish in "counter mode" to generate output */
    memset(ctx->b,0,sizeof(ctx->b));  /* zero out b[], so it can hold the counter */
    memcpy(X,ctx->X,sizeof(X));       /* keep a local copy of counter mode "key" */
    for (i=0;i*SKEIN1024_BLOCK_BYTES < byteCnt;i++)
        {
        ((u64b_t *)ctx->b)[0]= Skein_Swap64((u64b_t) i); /* build the counter block */
        Skein_Start_New_Type(ctx,OUT_FINAL);
        Skein1024_Process_Block(ctx,ctx->b,1,sizeof(u64b_t)); /* run "counter mode" */
        n = byteCnt - i*SKEIN1024_BLOCK_BYTES;   /* number of output bytes left to go */
        if (n >= SKEIN1024_BLOCK_BYTES)
            n  = SKEIN1024_BLOCK_BYTES;
        Skein_Put64_LSB_First(hashVal+i*SKEIN1024_BLOCK_BYTES,ctx->X,n);   /* "output" the ctr mode bytes */
        Skein_Show_Final(1024,&ctx->h,n,hashVal+i*SKEIN1024_BLOCK_BYTES);
        memcpy(ctx->X,X,sizeof(X));   /* restore the counter mode key for next time */
        }
    return SKEIN_SUCCESS;
    }

#if defined(SKEIN_CODE_SIZE) || defined(SKEIN_PERF)
size_t Skein1024_API_CodeSize(void)
    {
    return ((u08b_t *) Skein1024_API_CodeSize) -
           ((u08b_t *) Skein1024_Init);
    }
#endif

/**************** Functions to support MAC/tree hashing ***************/
/*   (this code is identical for Optimized and Reference versions)    */

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int Skein_256_Final_Pad(Skein_256_Ctxt_t *ctx, u08b_t *hashVal)
    {
    Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;        /* tag as the final block */
    if (ctx->h.bCnt < SKEIN_256_BLOCK_BYTES)   /* zero pad b[] if necessary */
        memset(&ctx->b[ctx->h.bCnt],0,SKEIN_256_BLOCK_BYTES - ctx->h.bCnt);
    Skein_256_Process_Block(ctx,ctx->b,1,ctx->h.bCnt);    /* process the final block */
    
    Skein_Put64_LSB_First(hashVal,ctx->X,SKEIN_256_BLOCK_BYTES);   /* "output" the state bytes */
    
    return SKEIN_SUCCESS;
    }

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int Skein_512_Final_Pad(Skein_512_Ctxt_t *ctx, u08b_t *hashVal)
    {
    Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;        /* tag as the final block */
    if (ctx->h.bCnt < SKEIN_512_BLOCK_BYTES)   /* zero pad b[] if necessary */
        memset(&ctx->b[ctx->h.bCnt],0,SKEIN_512_BLOCK_BYTES - ctx->h.bCnt);
    Skein_512_Process_Block(ctx,ctx->b,1,ctx->h.bCnt);    /* process the final block */
    
    Skein_Put64_LSB_First(hashVal,ctx->X,SKEIN_512_BLOCK_BYTES);   /* "output" the state bytes */
    
    return SKEIN_SUCCESS;
    }

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the block, no OUTPUT stage */
int Skein1024_Final_Pad(Skein1024_Ctxt_t *ctx, u08b_t *hashVal)
    {
    Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;        /* tag as the final block */
    if (ctx->h.bCnt < SKEIN1024_BLOCK_BYTES)   /* zero pad b[] if necessary */
        memset(&ctx->b[ctx->h.bCnt],0,SKEIN1024_BLOCK_BYTES - ctx->h.bCnt);
    Skein1024_Process_Block(ctx,ctx->b,1,ctx->h.bCnt);    /* process the final block */
    
    Skein_Put64_LSB_First(hashVal,ctx->X,SKEIN1024_BLOCK_BYTES);   /* "output" the state bytes */
    
    return SKEIN_SUCCESS;
    }

#if SKEIN_TREE_HASH
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int Skein_256_Output(Skein_256_Ctxt_t *ctx, u08b_t *hashVal)
    {
    size_t i,n,byteCnt;
    u64b_t X[SKEIN_256_STATE_WORDS];
    Skein_Assert(ctx->h.bCnt <= SKEIN_256_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    /* now output the result */
    byteCnt = (ctx->h.hashBitLen + 7) >> 3;    /* total number of output bytes */

    /* run Threefish in "counter mode" to generate output */
    memset(ctx->b,0,sizeof(ctx->b));  /* zero out b[], so it can hold the counter */
    memcpy(X,ctx->X,sizeof(X));       /* keep a local copy of counter mode "key" */
    for (i=0;i*SKEIN_256_BLOCK_BYTES < byteCnt;i++)
        {
        ((u64b_t *)ctx->b)[0]= Skein_Swap64((u64b_t) i); /* build the counter block */
        Skein_Start_New_Type(ctx,OUT_FINAL);
        Skein_256_Process_Block(ctx,ctx->b,1,sizeof(u64b_t)); /* run "counter mode" */
        n = byteCnt - i*SKEIN_256_BLOCK_BYTES;   /* number of output bytes left to go */
        if (n >= SKEIN_256_BLOCK_BYTES)
            n  = SKEIN_256_BLOCK_BYTES;
        Skein_Put64_LSB_First(hashVal+i*SKEIN_256_BLOCK_BYTES,ctx->X,n);   /* "output" the ctr mode bytes */
        Skein_Show_Final(256,&ctx->h,n,hashVal+i*SKEIN_256_BLOCK_BYTES);
        memcpy(ctx->X,X,sizeof(X));   /* restore the counter mode key for next time */
        }
    return SKEIN_SUCCESS;
    }

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int Skein_512_Output(Skein_512_Ctxt_t *ctx, u08b_t *hashVal)
    {
    size_t i,n,byteCnt;
    u64b_t X[SKEIN_512_STATE_WORDS];
    Skein_Assert(ctx->h.bCnt <= SKEIN_512_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    /* now output the result */
    byteCnt = (ctx->h.hashBitLen + 7) >> 3;    /* total number of output bytes */

    /* run Threefish in "counter mode" to generate output */
    memset(ctx->b,0,sizeof(ctx->b));  /* zero out b[], so it can hold the counter */
    memcpy(X,ctx->X,sizeof(X));       /* keep a local copy of counter mode "key" */
    for (i=0;i*SKEIN_512_BLOCK_BYTES < byteCnt;i++)
        {
        ((u64b_t *)ctx->b)[0]= Skein_Swap64((u64b_t) i); /* build the counter block */
        Skein_Start_New_Type(ctx,OUT_FINAL);
        Skein_512_Process_Block(ctx,ctx->b,1,sizeof(u64b_t)); /* run "counter mode" */
        n = byteCnt - i*SKEIN_512_BLOCK_BYTES;   /* number of output bytes left to go */
        if (n >= SKEIN_512_BLOCK_BYTES)
            n  = SKEIN_512_BLOCK_BYTES;
        Skein_Put64_LSB_First(hashVal+i*SKEIN_512_BLOCK_BYTES,ctx->X,n);   /* "output" the ctr mode bytes */
        Skein_Show_Final(256,&ctx->h,n,hashVal+i*SKEIN_512_BLOCK_BYTES);
        memcpy(ctx->X,X,sizeof(X));   /* restore the counter mode key for next time */
        }
    return SKEIN_SUCCESS;
    }

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* just do the OUTPUT stage                                       */
int Skein1024_Output(Skein1024_Ctxt_t *ctx, u08b_t *hashVal)
    {
    size_t i,n,byteCnt;
    u64b_t X[SKEIN1024_STATE_WORDS];
    Skein_Assert(ctx->h.bCnt <= SKEIN1024_BLOCK_BYTES,SKEIN_FAIL);    /* catch uninitialized context */

    /* now output the result */
    byteCnt = (ctx->h.hashBitLen + 7) >> 3;    /* total number of output bytes */

    /* run Threefish in "counter mode" to generate output */
    memset(ctx->b,0,sizeof(ctx->b));  /* zero out b[], so it can hold the counter */
    memcpy(X,ctx->X,sizeof(X));       /* keep a local copy of counter mode "key" */
    for (i=0;i*SKEIN1024_BLOCK_BYTES < byteCnt;i++)
        {
        ((u64b_t *)ctx->b)[0]= Skein_Swap64((u64b_t) i); /* build the counter block */
        Skein_Start_New_Type(ctx,OUT_FINAL);
        Skein1024_Process_Block(ctx,ctx->b,1,sizeof(u64b_t)); /* run "counter mode" */
        n = byteCnt - i*SKEIN1024_BLOCK_BYTES;   /* number of output bytes left to go */
        if (n >= SKEIN1024_BLOCK_BYTES)
            n  = SKEIN1024_BLOCK_BYTES;
        Skein_Put64_LSB_First(hashVal+i*SKEIN1024_BLOCK_BYTES,ctx->X,n);   /* "output" the ctr mode bytes */
        Skein_Show_Final(256,&ctx->h,n,hashVal+i*SKEIN1024_BLOCK_BYTES);
        memcpy(ctx->X,X,sizeof(X));   /* restore the counter mode key for next time */
        }
    return SKEIN_SUCCESS;
    }


/* Adapt the functions to match the prototype expected by libmd */
void
SKEIN256_Init(SKEIN256_CTX * ctx)
{

	Skein_256_Init(ctx, 256);
}

void
SKEIN512_Init(SKEIN512_CTX * ctx)
{

	Skein_512_Init(ctx, 512);
}

void
SKEIN1024_Init(SKEIN1024_CTX * ctx)
{

	Skein1024_Init(ctx, 1024);
}

void
SKEIN256_Update(SKEIN256_CTX * ctx, const void *in, size_t len)
{

	Skein_256_Update(ctx, in, len);
}

void
SKEIN512_Update(SKEIN512_CTX * ctx, const void *in, size_t len)
{

	Skein_512_Update(ctx, in, len);
}

void
SKEIN1024_Update(SKEIN1024_CTX * ctx, const void *in, size_t len)
{

	Skein1024_Update(ctx, in, len);
}

void
SKEIN256_Final(unsigned char digest[static SKEIN_256_BLOCK_BYTES], SKEIN256_CTX *ctx)
{

	Skein_256_Final(ctx, digest);
	explicit_bzero(ctx, sizeof(*ctx));
}

void
SKEIN512_Final(unsigned char digest[static SKEIN_512_BLOCK_BYTES], SKEIN512_CTX *ctx)
{

	Skein_512_Final(ctx, digest);
	explicit_bzero(ctx, sizeof(*ctx));
}

void
SKEIN1024_Final(unsigned char digest[static SKEIN1024_BLOCK_BYTES], SKEIN1024_CTX *ctx)
{

	Skein1024_Final(ctx, digest);
	explicit_bzero(ctx, sizeof(*ctx));
}

#ifdef WEAK_REFS
/* When building libmd, provide weak references. Note: this is not
   activated in the context of compiling these sources for internal
   use in libcrypt.
 */
#undef SKEIN256_Init
__weak_reference(_libmd_SKEIN256_Init, SKEIN256_Init);
#undef SKEIN256_Update
__weak_reference(_libmd_SKEIN256_Update, SKEIN256_Update);
#undef SKEIN256_Final
__weak_reference(_libmd_SKEIN256_Final, SKEIN256_Final);

#undef SKEIN512_Init
__weak_reference(_libmd_SKEIN512_Init, SKEIN512_Init);
#undef SKEIN512_Update
__weak_reference(_libmd_SKEIN512_Update, SKEIN512_Update);
#undef SKEIN512_Final
__weak_reference(_libmd_SKEIN512_Final, SKEIN512_Final);

#undef SKEIN1024_Init
__weak_reference(_libmd_SKEIN1024_Init, SKEIN1024_Init);
#undef SKEIN1024_Update
__weak_reference(_libmd_SKEIN1024_Update, SKEIN1024_Update);
#undef SKEIN1024_Final
__weak_reference(_libmd_SKEIN1024_Final, SKEIN1024_Final);
#endif

#endif
