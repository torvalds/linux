/***********************************************************************
**
** Implementation of the Skein block functions.
**
** Source code author: Doug Whiting, 2008.
**
** This algorithm and source code is released to the public domain.
**
** Compile-time switches:
**
**  SKEIN_USE_ASM  -- set bits (256/512/1024) to select which
**                    versions use ASM code for block processing
**                    [default: use C for all block sizes]
**
************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/types.h>

#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif

#include "skein.h"

#ifndef SKEIN_USE_ASM
#define SKEIN_USE_ASM   (0)                     /* default is all C code (no ASM) */
#endif

#ifndef SKEIN_LOOP
#define SKEIN_LOOP 001                          /* default: unroll 256 and 512, but not 1024 */
#endif

#define BLK_BITS        (WCNT*64)               /* some useful definitions for code here */
#define KW_TWK_BASE     (0)
#define KW_KEY_BASE     (3)
#define ks              (kw + KW_KEY_BASE)                
#define ts              (kw + KW_TWK_BASE)

#ifdef SKEIN_DEBUG
#define DebugSaveTweak(ctx) { ctx->h.T[0] = ts[0]; ctx->h.T[1] = ts[1]; }
#else
#define DebugSaveTweak(ctx)
#endif

/*****************************************************************/
/* functions to process blkCnt (nonzero) full block(s) of data. */
void    Skein_256_Process_Block(Skein_256_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t byteCntAdd);
void    Skein_512_Process_Block(Skein_512_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t byteCntAdd);
void    Skein1024_Process_Block(Skein1024_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t byteCntAdd);

/*****************************  Skein_256 ******************************/
#if !(SKEIN_USE_ASM & 256)
void Skein_256_Process_Block(Skein_256_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t byteCntAdd)
    { /* do it in C */
    enum
        {
        WCNT = SKEIN_256_STATE_WORDS
        };
#undef  RCNT
#define RCNT  (SKEIN_256_ROUNDS_TOTAL/8)

#ifdef  SKEIN_LOOP                              /* configure how much to unroll the loop */
#define SKEIN_UNROLL_256 (((SKEIN_LOOP)/100)%10)
#else
#define SKEIN_UNROLL_256 (0)
#endif

#if SKEIN_UNROLL_256
#if (RCNT % SKEIN_UNROLL_256)
#error "Invalid SKEIN_UNROLL_256"               /* sanity check on unroll count */
#endif
    size_t  r;
    u64b_t  kw[WCNT+4+RCNT*2];                  /* key schedule words : chaining vars + tweak + "rotation"*/
#else
    u64b_t  kw[WCNT+4];                         /* key schedule words : chaining vars + tweak */
#endif
    u64b_t  X0,X1,X2,X3;                        /* local copy of context vars, for speed */
    u64b_t  w [WCNT];                           /* local copy of input block */
#ifdef SKEIN_DEBUG
    const u64b_t *Xptr[4];                      /* use for debugging (help compiler put Xn in registers) */
    Xptr[0] = &X0;  Xptr[1] = &X1;  Xptr[2] = &X2;  Xptr[3] = &X3;
#endif
    Skein_assert(blkCnt != 0);                  /* never call with blkCnt == 0! */
    ts[0] = ctx->h.T[0];
    ts[1] = ctx->h.T[1];
    do  {
        /* this implementation only supports 2**64 input bytes (no carry out here) */
        ts[0] += byteCntAdd;                    /* update processed length */

        /* precompute the key schedule for this block */
        ks[0] = ctx->X[0];     
        ks[1] = ctx->X[1];
        ks[2] = ctx->X[2];
        ks[3] = ctx->X[3];
        ks[4] = ks[0] ^ ks[1] ^ ks[2] ^ ks[3] ^ SKEIN_KS_PARITY;

        ts[2] = ts[0] ^ ts[1];

        Skein_Get64_LSB_First(w,blkPtr,WCNT);   /* get input block in little-endian format */
        DebugSaveTweak(ctx);
        Skein_Show_Block(BLK_BITS,&ctx->h,ctx->X,blkPtr,w,ks,ts);

        X0 = w[0] + ks[0];                      /* do the first full key injection */
        X1 = w[1] + ks[1] + ts[0];
        X2 = w[2] + ks[2] + ts[1];
        X3 = w[3] + ks[3];

        Skein_Show_R_Ptr(BLK_BITS,&ctx->h,SKEIN_RND_KEY_INITIAL,Xptr);    /* show starting state values */

        blkPtr += SKEIN_256_BLOCK_BYTES;

        /* run the rounds */

#define Round256(p0,p1,p2,p3,ROT,rNum)                              \
    X##p0 += X##p1; X##p1 = RotL_64(X##p1,ROT##_0); X##p1 ^= X##p0; \
    X##p2 += X##p3; X##p3 = RotL_64(X##p3,ROT##_1); X##p3 ^= X##p2; \

#if SKEIN_UNROLL_256 == 0                       
#define R256(p0,p1,p2,p3,ROT,rNum)           /* fully unrolled */   \
    Round256(p0,p1,p2,p3,ROT,rNum)                                  \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,rNum,Xptr);

#define I256(R)                                                     \
    X0   += ks[((R)+1) % 5];    /* inject the key schedule value */ \
    X1   += ks[((R)+2) % 5] + ts[((R)+1) % 3];                      \
    X2   += ks[((R)+3) % 5] + ts[((R)+2) % 3];                      \
    X3   += ks[((R)+4) % 5] +     (R)+1;                            \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,SKEIN_RND_KEY_INJECT,Xptr);
#else                                       /* looping version */
#define R256(p0,p1,p2,p3,ROT,rNum)                                  \
    Round256(p0,p1,p2,p3,ROT,rNum)                                  \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,4*(r-1)+rNum,Xptr);

#define I256(R)                                                     \
    X0   += ks[r+(R)+0];        /* inject the key schedule value */ \
    X1   += ks[r+(R)+1] + ts[r+(R)+0];                              \
    X2   += ks[r+(R)+2] + ts[r+(R)+1];                              \
    X3   += ks[r+(R)+3] +    r+(R)   ;                              \
    ks[r + (R)+4    ]   = ks[r+(R)-1];     /* rotate key schedule */\
    ts[r + (R)+2    ]   = ts[r+(R)-1];                              \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,SKEIN_RND_KEY_INJECT,Xptr);

    for (r=1;r < 2*RCNT;r+=2*SKEIN_UNROLL_256)  /* loop thru it */
#endif  
        {    
#define R256_8_rounds(R)                  \
        R256(0,1,2,3,R_256_0,8*(R) + 1);  \
        R256(0,3,2,1,R_256_1,8*(R) + 2);  \
        R256(0,1,2,3,R_256_2,8*(R) + 3);  \
        R256(0,3,2,1,R_256_3,8*(R) + 4);  \
        I256(2*(R));                      \
        R256(0,1,2,3,R_256_4,8*(R) + 5);  \
        R256(0,3,2,1,R_256_5,8*(R) + 6);  \
        R256(0,1,2,3,R_256_6,8*(R) + 7);  \
        R256(0,3,2,1,R_256_7,8*(R) + 8);  \
        I256(2*(R)+1);

        R256_8_rounds( 0);

#define R256_Unroll_R(NN) ((SKEIN_UNROLL_256 == 0 && SKEIN_256_ROUNDS_TOTAL/8 > (NN)) || (SKEIN_UNROLL_256 > (NN)))

  #if   R256_Unroll_R( 1)
        R256_8_rounds( 1);
  #endif
  #if   R256_Unroll_R( 2)
        R256_8_rounds( 2);
  #endif
  #if   R256_Unroll_R( 3)
        R256_8_rounds( 3);
  #endif
  #if   R256_Unroll_R( 4)
        R256_8_rounds( 4);
  #endif
  #if   R256_Unroll_R( 5)
        R256_8_rounds( 5);
  #endif
  #if   R256_Unroll_R( 6)
        R256_8_rounds( 6);
  #endif
  #if   R256_Unroll_R( 7)
        R256_8_rounds( 7);
  #endif
  #if   R256_Unroll_R( 8)
        R256_8_rounds( 8);
  #endif
  #if   R256_Unroll_R( 9)
        R256_8_rounds( 9);
  #endif
  #if   R256_Unroll_R(10)
        R256_8_rounds(10);
  #endif
  #if   R256_Unroll_R(11)
        R256_8_rounds(11);
  #endif
  #if   R256_Unroll_R(12)
        R256_8_rounds(12);
  #endif
  #if   R256_Unroll_R(13)
        R256_8_rounds(13);
  #endif
  #if   R256_Unroll_R(14)
        R256_8_rounds(14);
  #endif
  #if  (SKEIN_UNROLL_256 > 14)
#error  "need more unrolling in Skein_256_Process_Block"
  #endif
        }
        /* do the final "feedforward" xor, update context chaining vars */
        ctx->X[0] = X0 ^ w[0];
        ctx->X[1] = X1 ^ w[1];
        ctx->X[2] = X2 ^ w[2];
        ctx->X[3] = X3 ^ w[3];

        Skein_Show_Round(BLK_BITS,&ctx->h,SKEIN_RND_FEED_FWD,ctx->X);

        ts[1] &= ~SKEIN_T1_FLAG_FIRST;
        }
    while (--blkCnt);
    ctx->h.T[0] = ts[0];
    ctx->h.T[1] = ts[1];
    }

#if defined(SKEIN_CODE_SIZE) || defined(SKEIN_PERF)
size_t Skein_256_Process_Block_CodeSize(void)
    {
    return ((u08b_t *) Skein_256_Process_Block_CodeSize) -
           ((u08b_t *) Skein_256_Process_Block);
    }
uint_t Skein_256_Unroll_Cnt(void)
    {
    return SKEIN_UNROLL_256;
    }
#endif
#endif

/*****************************  Skein_512 ******************************/
#if !(SKEIN_USE_ASM & 512)
void Skein_512_Process_Block(Skein_512_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t byteCntAdd)
    { /* do it in C */
    enum
        {
        WCNT = SKEIN_512_STATE_WORDS
        };
#undef  RCNT
#define RCNT  (SKEIN_512_ROUNDS_TOTAL/8)

#ifdef  SKEIN_LOOP                              /* configure how much to unroll the loop */
#define SKEIN_UNROLL_512 (((SKEIN_LOOP)/10)%10)
#else
#define SKEIN_UNROLL_512 (0)
#endif

#if SKEIN_UNROLL_512
#if (RCNT % SKEIN_UNROLL_512)
#error "Invalid SKEIN_UNROLL_512"               /* sanity check on unroll count */
#endif
    size_t  r;
    u64b_t  kw[WCNT+4+RCNT*2];                  /* key schedule words : chaining vars + tweak + "rotation"*/
#else
    u64b_t  kw[WCNT+4];                         /* key schedule words : chaining vars + tweak */
#endif
    u64b_t  X0,X1,X2,X3,X4,X5,X6,X7;            /* local copy of vars, for speed */
    u64b_t  w [WCNT];                           /* local copy of input block */
#ifdef SKEIN_DEBUG
    const u64b_t *Xptr[8];                      /* use for debugging (help compiler put Xn in registers) */
    Xptr[0] = &X0;  Xptr[1] = &X1;  Xptr[2] = &X2;  Xptr[3] = &X3;
    Xptr[4] = &X4;  Xptr[5] = &X5;  Xptr[6] = &X6;  Xptr[7] = &X7;
#endif

    Skein_assert(blkCnt != 0);                  /* never call with blkCnt == 0! */
    ts[0] = ctx->h.T[0];
    ts[1] = ctx->h.T[1];
    do  {
        /* this implementation only supports 2**64 input bytes (no carry out here) */
        ts[0] += byteCntAdd;                    /* update processed length */

        /* precompute the key schedule for this block */
        ks[0] = ctx->X[0];
        ks[1] = ctx->X[1];
        ks[2] = ctx->X[2];
        ks[3] = ctx->X[3];
        ks[4] = ctx->X[4];
        ks[5] = ctx->X[5];
        ks[6] = ctx->X[6];
        ks[7] = ctx->X[7];
        ks[8] = ks[0] ^ ks[1] ^ ks[2] ^ ks[3] ^ 
                ks[4] ^ ks[5] ^ ks[6] ^ ks[7] ^ SKEIN_KS_PARITY;

        ts[2] = ts[0] ^ ts[1];

        Skein_Get64_LSB_First(w,blkPtr,WCNT); /* get input block in little-endian format */
        DebugSaveTweak(ctx);
        Skein_Show_Block(BLK_BITS,&ctx->h,ctx->X,blkPtr,w,ks,ts);

        X0   = w[0] + ks[0];                    /* do the first full key injection */
        X1   = w[1] + ks[1];
        X2   = w[2] + ks[2];
        X3   = w[3] + ks[3];
        X4   = w[4] + ks[4];
        X5   = w[5] + ks[5] + ts[0];
        X6   = w[6] + ks[6] + ts[1];
        X7   = w[7] + ks[7];

        blkPtr += SKEIN_512_BLOCK_BYTES;

        Skein_Show_R_Ptr(BLK_BITS,&ctx->h,SKEIN_RND_KEY_INITIAL,Xptr);
        /* run the rounds */
#define Round512(p0,p1,p2,p3,p4,p5,p6,p7,ROT,rNum)                  \
    X##p0 += X##p1; X##p1 = RotL_64(X##p1,ROT##_0); X##p1 ^= X##p0; \
    X##p2 += X##p3; X##p3 = RotL_64(X##p3,ROT##_1); X##p3 ^= X##p2; \
    X##p4 += X##p5; X##p5 = RotL_64(X##p5,ROT##_2); X##p5 ^= X##p4; \
    X##p6 += X##p7; X##p7 = RotL_64(X##p7,ROT##_3); X##p7 ^= X##p6; \

#if SKEIN_UNROLL_512 == 0                       
#define R512(p0,p1,p2,p3,p4,p5,p6,p7,ROT,rNum)      /* unrolled */  \
    Round512(p0,p1,p2,p3,p4,p5,p6,p7,ROT,rNum)                      \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,rNum,Xptr);

#define I512(R)                                                     \
    X0   += ks[((R)+1) % 9];   /* inject the key schedule value */  \
    X1   += ks[((R)+2) % 9];                                        \
    X2   += ks[((R)+3) % 9];                                        \
    X3   += ks[((R)+4) % 9];                                        \
    X4   += ks[((R)+5) % 9];                                        \
    X5   += ks[((R)+6) % 9] + ts[((R)+1) % 3];                      \
    X6   += ks[((R)+7) % 9] + ts[((R)+2) % 3];                      \
    X7   += ks[((R)+8) % 9] +     (R)+1;                            \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,SKEIN_RND_KEY_INJECT,Xptr);
#else                                       /* looping version */
#define R512(p0,p1,p2,p3,p4,p5,p6,p7,ROT,rNum)                      \
    Round512(p0,p1,p2,p3,p4,p5,p6,p7,ROT,rNum)                      \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,4*(r-1)+rNum,Xptr);

#define I512(R)                                                     \
    X0   += ks[r+(R)+0];        /* inject the key schedule value */ \
    X1   += ks[r+(R)+1];                                            \
    X2   += ks[r+(R)+2];                                            \
    X3   += ks[r+(R)+3];                                            \
    X4   += ks[r+(R)+4];                                            \
    X5   += ks[r+(R)+5] + ts[r+(R)+0];                              \
    X6   += ks[r+(R)+6] + ts[r+(R)+1];                              \
    X7   += ks[r+(R)+7] +    r+(R)   ;                              \
    ks[r +       (R)+8] = ks[r+(R)-1];  /* rotate key schedule */   \
    ts[r +       (R)+2] = ts[r+(R)-1];                              \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,SKEIN_RND_KEY_INJECT,Xptr);

    for (r=1;r < 2*RCNT;r+=2*SKEIN_UNROLL_512)   /* loop thru it */
#endif                         /* end of looped code definitions */
        {
#define R512_8_rounds(R)  /* do 8 full rounds */  \
        R512(0,1,2,3,4,5,6,7,R_512_0,8*(R)+ 1);   \
        R512(2,1,4,7,6,5,0,3,R_512_1,8*(R)+ 2);   \
        R512(4,1,6,3,0,5,2,7,R_512_2,8*(R)+ 3);   \
        R512(6,1,0,7,2,5,4,3,R_512_3,8*(R)+ 4);   \
        I512(2*(R));                              \
        R512(0,1,2,3,4,5,6,7,R_512_4,8*(R)+ 5);   \
        R512(2,1,4,7,6,5,0,3,R_512_5,8*(R)+ 6);   \
        R512(4,1,6,3,0,5,2,7,R_512_6,8*(R)+ 7);   \
        R512(6,1,0,7,2,5,4,3,R_512_7,8*(R)+ 8);   \
        I512(2*(R)+1);        /* and key injection */

        R512_8_rounds( 0);

#define R512_Unroll_R(NN) ((SKEIN_UNROLL_512 == 0 && SKEIN_512_ROUNDS_TOTAL/8 > (NN)) || (SKEIN_UNROLL_512 > (NN)))

  #if   R512_Unroll_R( 1)
        R512_8_rounds( 1);
  #endif
  #if   R512_Unroll_R( 2)
        R512_8_rounds( 2);
  #endif
  #if   R512_Unroll_R( 3)
        R512_8_rounds( 3);
  #endif
  #if   R512_Unroll_R( 4)
        R512_8_rounds( 4);
  #endif
  #if   R512_Unroll_R( 5)
        R512_8_rounds( 5);
  #endif
  #if   R512_Unroll_R( 6)
        R512_8_rounds( 6);
  #endif
  #if   R512_Unroll_R( 7)
        R512_8_rounds( 7);
  #endif
  #if   R512_Unroll_R( 8)
        R512_8_rounds( 8);
  #endif
  #if   R512_Unroll_R( 9)
        R512_8_rounds( 9);
  #endif
  #if   R512_Unroll_R(10)
        R512_8_rounds(10);
  #endif
  #if   R512_Unroll_R(11)
        R512_8_rounds(11);
  #endif
  #if   R512_Unroll_R(12)
        R512_8_rounds(12);
  #endif
  #if   R512_Unroll_R(13)
        R512_8_rounds(13);
  #endif
  #if   R512_Unroll_R(14)
        R512_8_rounds(14);
  #endif
  #if  (SKEIN_UNROLL_512 > 14)
#error  "need more unrolling in Skein_512_Process_Block"
  #endif
        }

        /* do the final "feedforward" xor, update context chaining vars */
        ctx->X[0] = X0 ^ w[0];
        ctx->X[1] = X1 ^ w[1];
        ctx->X[2] = X2 ^ w[2];
        ctx->X[3] = X3 ^ w[3];
        ctx->X[4] = X4 ^ w[4];
        ctx->X[5] = X5 ^ w[5];
        ctx->X[6] = X6 ^ w[6];
        ctx->X[7] = X7 ^ w[7];
        Skein_Show_Round(BLK_BITS,&ctx->h,SKEIN_RND_FEED_FWD,ctx->X);

        ts[1] &= ~SKEIN_T1_FLAG_FIRST;
        }
    while (--blkCnt);
    ctx->h.T[0] = ts[0];
    ctx->h.T[1] = ts[1];
    }

#if defined(SKEIN_CODE_SIZE) || defined(SKEIN_PERF)
size_t Skein_512_Process_Block_CodeSize(void)
    {
    return ((u08b_t *) Skein_512_Process_Block_CodeSize) -
           ((u08b_t *) Skein_512_Process_Block);
    }
uint_t Skein_512_Unroll_Cnt(void)
    {
    return SKEIN_UNROLL_512;
    }
#endif
#endif

/*****************************  Skein1024 ******************************/
#if !(SKEIN_USE_ASM & 1024)
void Skein1024_Process_Block(Skein1024_Ctxt_t *ctx,const u08b_t *blkPtr,size_t blkCnt,size_t byteCntAdd)
    { /* do it in C, always looping (unrolled is bigger AND slower!) */
    enum
        {
        WCNT = SKEIN1024_STATE_WORDS
        };
#undef  RCNT
#define RCNT  (SKEIN1024_ROUNDS_TOTAL/8)

#ifdef  SKEIN_LOOP                              /* configure how much to unroll the loop */
#define SKEIN_UNROLL_1024 ((SKEIN_LOOP)%10)
#else
#define SKEIN_UNROLL_1024 (0)
#endif

#if (SKEIN_UNROLL_1024 != 0)
#if (RCNT % SKEIN_UNROLL_1024)
#error "Invalid SKEIN_UNROLL_1024"              /* sanity check on unroll count */
#endif
    size_t  r;
    u64b_t  kw[WCNT+4+RCNT*2];                  /* key schedule words : chaining vars + tweak + "rotation"*/
#else
    u64b_t  kw[WCNT+4];                         /* key schedule words : chaining vars + tweak */
#endif

    u64b_t  X00,X01,X02,X03,X04,X05,X06,X07,    /* local copy of vars, for speed */
            X08,X09,X10,X11,X12,X13,X14,X15;
    u64b_t  w [WCNT];                           /* local copy of input block */
#ifdef SKEIN_DEBUG
    const u64b_t *Xptr[16];                     /* use for debugging (help compiler put Xn in registers) */
    Xptr[ 0] = &X00;  Xptr[ 1] = &X01;  Xptr[ 2] = &X02;  Xptr[ 3] = &X03;
    Xptr[ 4] = &X04;  Xptr[ 5] = &X05;  Xptr[ 6] = &X06;  Xptr[ 7] = &X07;
    Xptr[ 8] = &X08;  Xptr[ 9] = &X09;  Xptr[10] = &X10;  Xptr[11] = &X11;
    Xptr[12] = &X12;  Xptr[13] = &X13;  Xptr[14] = &X14;  Xptr[15] = &X15;
#endif

    Skein_assert(blkCnt != 0);                  /* never call with blkCnt == 0! */
    ts[0] = ctx->h.T[0];
    ts[1] = ctx->h.T[1];
    do  {
        /* this implementation only supports 2**64 input bytes (no carry out here) */
        ts[0] += byteCntAdd;                    /* update processed length */

        /* precompute the key schedule for this block */
        ks[ 0] = ctx->X[ 0];
        ks[ 1] = ctx->X[ 1];
        ks[ 2] = ctx->X[ 2];
        ks[ 3] = ctx->X[ 3];
        ks[ 4] = ctx->X[ 4];
        ks[ 5] = ctx->X[ 5];
        ks[ 6] = ctx->X[ 6];
        ks[ 7] = ctx->X[ 7];
        ks[ 8] = ctx->X[ 8];
        ks[ 9] = ctx->X[ 9];
        ks[10] = ctx->X[10];
        ks[11] = ctx->X[11];
        ks[12] = ctx->X[12];
        ks[13] = ctx->X[13];
        ks[14] = ctx->X[14];
        ks[15] = ctx->X[15];
        ks[16] = ks[ 0] ^ ks[ 1] ^ ks[ 2] ^ ks[ 3] ^
                 ks[ 4] ^ ks[ 5] ^ ks[ 6] ^ ks[ 7] ^
                 ks[ 8] ^ ks[ 9] ^ ks[10] ^ ks[11] ^
                 ks[12] ^ ks[13] ^ ks[14] ^ ks[15] ^ SKEIN_KS_PARITY;

        ts[2]  = ts[0] ^ ts[1];

        Skein_Get64_LSB_First(w,blkPtr,WCNT); /* get input block in little-endian format */
        DebugSaveTweak(ctx);
        Skein_Show_Block(BLK_BITS,&ctx->h,ctx->X,blkPtr,w,ks,ts);

        X00    = w[ 0] + ks[ 0];                 /* do the first full key injection */
        X01    = w[ 1] + ks[ 1];
        X02    = w[ 2] + ks[ 2];
        X03    = w[ 3] + ks[ 3];
        X04    = w[ 4] + ks[ 4];
        X05    = w[ 5] + ks[ 5];
        X06    = w[ 6] + ks[ 6];
        X07    = w[ 7] + ks[ 7];
        X08    = w[ 8] + ks[ 8];
        X09    = w[ 9] + ks[ 9];
        X10    = w[10] + ks[10];
        X11    = w[11] + ks[11];
        X12    = w[12] + ks[12];
        X13    = w[13] + ks[13] + ts[0];
        X14    = w[14] + ks[14] + ts[1];
        X15    = w[15] + ks[15];

        Skein_Show_R_Ptr(BLK_BITS,&ctx->h,SKEIN_RND_KEY_INITIAL,Xptr);

#define Round1024(p0,p1,p2,p3,p4,p5,p6,p7,p8,p9,pA,pB,pC,pD,pE,pF,ROT,rNum) \
    X##p0 += X##p1; X##p1 = RotL_64(X##p1,ROT##_0); X##p1 ^= X##p0;   \
    X##p2 += X##p3; X##p3 = RotL_64(X##p3,ROT##_1); X##p3 ^= X##p2;   \
    X##p4 += X##p5; X##p5 = RotL_64(X##p5,ROT##_2); X##p5 ^= X##p4;   \
    X##p6 += X##p7; X##p7 = RotL_64(X##p7,ROT##_3); X##p7 ^= X##p6;   \
    X##p8 += X##p9; X##p9 = RotL_64(X##p9,ROT##_4); X##p9 ^= X##p8;   \
    X##pA += X##pB; X##pB = RotL_64(X##pB,ROT##_5); X##pB ^= X##pA;   \
    X##pC += X##pD; X##pD = RotL_64(X##pD,ROT##_6); X##pD ^= X##pC;   \
    X##pE += X##pF; X##pF = RotL_64(X##pF,ROT##_7); X##pF ^= X##pE;   \

#if SKEIN_UNROLL_1024 == 0                      
#define R1024(p0,p1,p2,p3,p4,p5,p6,p7,p8,p9,pA,pB,pC,pD,pE,pF,ROT,rn) \
    Round1024(p0,p1,p2,p3,p4,p5,p6,p7,p8,p9,pA,pB,pC,pD,pE,pF,ROT,rn) \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,rn,Xptr);

#define I1024(R)                                                      \
    X00   += ks[((R)+ 1) % 17]; /* inject the key schedule value */   \
    X01   += ks[((R)+ 2) % 17];                                       \
    X02   += ks[((R)+ 3) % 17];                                       \
    X03   += ks[((R)+ 4) % 17];                                       \
    X04   += ks[((R)+ 5) % 17];                                       \
    X05   += ks[((R)+ 6) % 17];                                       \
    X06   += ks[((R)+ 7) % 17];                                       \
    X07   += ks[((R)+ 8) % 17];                                       \
    X08   += ks[((R)+ 9) % 17];                                       \
    X09   += ks[((R)+10) % 17];                                       \
    X10   += ks[((R)+11) % 17];                                       \
    X11   += ks[((R)+12) % 17];                                       \
    X12   += ks[((R)+13) % 17];                                       \
    X13   += ks[((R)+14) % 17] + ts[((R)+1) % 3];                     \
    X14   += ks[((R)+15) % 17] + ts[((R)+2) % 3];                     \
    X15   += ks[((R)+16) % 17] +     (R)+1;                           \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,SKEIN_RND_KEY_INJECT,Xptr); 
#else                                       /* looping version */
#define R1024(p0,p1,p2,p3,p4,p5,p6,p7,p8,p9,pA,pB,pC,pD,pE,pF,ROT,rn) \
    Round1024(p0,p1,p2,p3,p4,p5,p6,p7,p8,p9,pA,pB,pC,pD,pE,pF,ROT,rn) \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,4*(r-1)+rn,Xptr);

#define I1024(R)                                                      \
    X00   += ks[r+(R)+ 0];    /* inject the key schedule value */     \
    X01   += ks[r+(R)+ 1];                                            \
    X02   += ks[r+(R)+ 2];                                            \
    X03   += ks[r+(R)+ 3];                                            \
    X04   += ks[r+(R)+ 4];                                            \
    X05   += ks[r+(R)+ 5];                                            \
    X06   += ks[r+(R)+ 6];                                            \
    X07   += ks[r+(R)+ 7];                                            \
    X08   += ks[r+(R)+ 8];                                            \
    X09   += ks[r+(R)+ 9];                                            \
    X10   += ks[r+(R)+10];                                            \
    X11   += ks[r+(R)+11];                                            \
    X12   += ks[r+(R)+12];                                            \
    X13   += ks[r+(R)+13] + ts[r+(R)+0];                              \
    X14   += ks[r+(R)+14] + ts[r+(R)+1];                              \
    X15   += ks[r+(R)+15] +    r+(R)   ;                              \
    ks[r  +       (R)+16] = ks[r+(R)-1];  /* rotate key schedule */   \
    ts[r  +       (R)+ 2] = ts[r+(R)-1];                              \
    Skein_Show_R_Ptr(BLK_BITS,&ctx->h,SKEIN_RND_KEY_INJECT,Xptr);

    for (r=1;r <= 2*RCNT;r+=2*SKEIN_UNROLL_1024)    /* loop thru it */
#endif  
        {
#define R1024_8_rounds(R)    /* do 8 full rounds */                               \
        R1024(00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,R1024_0,8*(R) + 1); \
        R1024(00,09,02,13,06,11,04,15,10,07,12,03,14,05,08,01,R1024_1,8*(R) + 2); \
        R1024(00,07,02,05,04,03,06,01,12,15,14,13,08,11,10,09,R1024_2,8*(R) + 3); \
        R1024(00,15,02,11,06,13,04,09,14,01,08,05,10,03,12,07,R1024_3,8*(R) + 4); \
        I1024(2*(R));                                                             \
        R1024(00,01,02,03,04,05,06,07,08,09,10,11,12,13,14,15,R1024_4,8*(R) + 5); \
        R1024(00,09,02,13,06,11,04,15,10,07,12,03,14,05,08,01,R1024_5,8*(R) + 6); \
        R1024(00,07,02,05,04,03,06,01,12,15,14,13,08,11,10,09,R1024_6,8*(R) + 7); \
        R1024(00,15,02,11,06,13,04,09,14,01,08,05,10,03,12,07,R1024_7,8*(R) + 8); \
        I1024(2*(R)+1);

        R1024_8_rounds( 0);

#define R1024_Unroll_R(NN) ((SKEIN_UNROLL_1024 == 0 && SKEIN1024_ROUNDS_TOTAL/8 > (NN)) || (SKEIN_UNROLL_1024 > (NN)))

  #if   R1024_Unroll_R( 1)
        R1024_8_rounds( 1);
  #endif
  #if   R1024_Unroll_R( 2)
        R1024_8_rounds( 2);
  #endif
  #if   R1024_Unroll_R( 3)
        R1024_8_rounds( 3);
  #endif
  #if   R1024_Unroll_R( 4)
        R1024_8_rounds( 4);
  #endif
  #if   R1024_Unroll_R( 5)
        R1024_8_rounds( 5);
  #endif
  #if   R1024_Unroll_R( 6)
        R1024_8_rounds( 6);
  #endif
  #if   R1024_Unroll_R( 7)
        R1024_8_rounds( 7);
  #endif
  #if   R1024_Unroll_R( 8)
        R1024_8_rounds( 8);
  #endif
  #if   R1024_Unroll_R( 9)
        R1024_8_rounds( 9);
  #endif
  #if   R1024_Unroll_R(10)
        R1024_8_rounds(10);
  #endif
  #if   R1024_Unroll_R(11)
        R1024_8_rounds(11);
  #endif
  #if   R1024_Unroll_R(12)
        R1024_8_rounds(12);
  #endif
  #if   R1024_Unroll_R(13)
        R1024_8_rounds(13);
  #endif
  #if   R1024_Unroll_R(14)
        R1024_8_rounds(14);
  #endif
  #if  (SKEIN_UNROLL_1024 > 14)
#error  "need more unrolling in Skein_1024_Process_Block"
  #endif
        }
        /* do the final "feedforward" xor, update context chaining vars */

        ctx->X[ 0] = X00 ^ w[ 0];
        ctx->X[ 1] = X01 ^ w[ 1];
        ctx->X[ 2] = X02 ^ w[ 2];
        ctx->X[ 3] = X03 ^ w[ 3];
        ctx->X[ 4] = X04 ^ w[ 4];
        ctx->X[ 5] = X05 ^ w[ 5];
        ctx->X[ 6] = X06 ^ w[ 6];
        ctx->X[ 7] = X07 ^ w[ 7];
        ctx->X[ 8] = X08 ^ w[ 8];
        ctx->X[ 9] = X09 ^ w[ 9];
        ctx->X[10] = X10 ^ w[10];
        ctx->X[11] = X11 ^ w[11];
        ctx->X[12] = X12 ^ w[12];
        ctx->X[13] = X13 ^ w[13];
        ctx->X[14] = X14 ^ w[14];
        ctx->X[15] = X15 ^ w[15];

        Skein_Show_Round(BLK_BITS,&ctx->h,SKEIN_RND_FEED_FWD,ctx->X);
        
        ts[1] &= ~SKEIN_T1_FLAG_FIRST;
        blkPtr += SKEIN1024_BLOCK_BYTES;
        }
    while (--blkCnt);
    ctx->h.T[0] = ts[0];
    ctx->h.T[1] = ts[1];
    }

#if defined(SKEIN_CODE_SIZE) || defined(SKEIN_PERF)
size_t Skein1024_Process_Block_CodeSize(void)
    {
    return ((u08b_t *) Skein1024_Process_Block_CodeSize) -
           ((u08b_t *) Skein1024_Process_Block);
    }
uint_t Skein1024_Unroll_Cnt(void)
    {
    return SKEIN_UNROLL_1024;
    }
#endif
#endif
