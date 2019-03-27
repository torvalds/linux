/***********************************************************************
**
** Debug output functions for Skein hashing.
**
** Source code author: Doug Whiting, 2008.
**
** This algorithm and source code is released to the public domain.
**
************************************************************************/
#include <stdio.h>

#ifdef SKEIN_DEBUG  /* only instantiate this code if SKEIN_DEBUG is on */
#include "skein.h"

static const char INDENT[] =  "    ";  /* how much to indent on new line */

uint_t skein_DebugFlag = 0;  /* off by default. Must be set externally */

static void Show64_step(size_t cnt,const u64b_t *X,size_t step)
    {
    size_t i,j;
    for (i=j=0;i < cnt;i++,j+=step)
        {
        if (i % 4 ==  0) printf(INDENT);
        printf(" %08X.%08X ",(uint_32t)(X[j] >> 32),(uint_32t)X[j]);
        if (i % 4 ==  3 || i==cnt-1) printf("\n");
        fflush(stdout);
        }
    }

#define Show64(cnt,X) Show64_step(cnt,X,1)

static void Show64_flag(size_t cnt,const u64b_t *X)
    {
    size_t xptr = (size_t) X;
    size_t step = (xptr & 1) ? 2 : 1;
    if (step != 1)
        {
        X = (const u64b_t *) (xptr & ~1);
        }
    Show64_step(cnt,X,step);
    }

static void Show08(size_t cnt,const u08b_t *b)
    {
    size_t i;
    for (i=0;i < cnt;i++)
        {
        if (i %16 ==  0) printf(INDENT);
        else if (i % 4 == 0) printf(" ");
        printf(" %02X",b[i]);
        if (i %16 == 15 || i==cnt-1) printf("\n");
        fflush(stdout);
        }
    }

static const char *AlgoHeader(uint_t bits)
    {
    if (skein_DebugFlag & SKEIN_DEBUG_THREEFISH)
        switch (bits)
            {
            case  256:  return ":Threefish-256: ";
            case  512:  return ":Threefish-512: ";
            case 1024:  return ":Threefish-1024:";
            }
    else
        switch (bits)
            {
            case  256:  return ":Skein-256: ";
            case  512:  return ":Skein-512: ";
            case 1024:  return ":Skein-1024:";
            }
    return NULL;
    }

void Skein_Show_Final(uint_t bits,const Skein_Ctxt_Hdr_t *h,size_t cnt,const u08b_t *outPtr)
    {
    if (skein_DebugFlag & SKEIN_DEBUG_CONFIG || ((h->T[1] & SKEIN_T1_BLK_TYPE_MASK) != SKEIN_T1_BLK_TYPE_CFG))
    if (skein_DebugFlag & SKEIN_DEBUG_FINAL)
        {
        printf("\n%s Final output=\n",AlgoHeader(bits));
        Show08(cnt,outPtr);
        printf("    ++++++++++\n");
        fflush(stdout);
        }
    }

/* show state after a round (or "pseudo-round") */
void Skein_Show_Round(uint_t bits,const Skein_Ctxt_Hdr_t *h,size_t r,const u64b_t *X)
    {
    static uint_t injectNum=0;  /* not multi-thread safe! */

    if (skein_DebugFlag & SKEIN_DEBUG_CONFIG || ((h->T[1] & SKEIN_T1_BLK_TYPE_MASK) != SKEIN_T1_BLK_TYPE_CFG))
    if (skein_DebugFlag)
        {
        if (r >= SKEIN_RND_SPECIAL) 
            {       /* a key injection (or feedforward) point */
            injectNum = (r == SKEIN_RND_KEY_INITIAL) ? 0 : injectNum+1;
            if (  skein_DebugFlag & SKEIN_DEBUG_INJECT ||
                ((skein_DebugFlag & SKEIN_DEBUG_FINAL) && r == SKEIN_RND_FEED_FWD))
                {
                printf("\n%s",AlgoHeader(bits));
                switch (r)
                    {
                    case SKEIN_RND_KEY_INITIAL:
                        printf(" [state after initial key injection]");
                        break;
                    case SKEIN_RND_KEY_INJECT:
                        printf(" [state after key injection #%02d]",injectNum);
                        break;
                    case SKEIN_RND_FEED_FWD:
                        printf(" [state after plaintext feedforward]");
                        injectNum = 0;
                        break;
                    }
                printf("=\n");
                Show64(bits/64,X);
                if (r== SKEIN_RND_FEED_FWD)
                    printf("    ----------\n");
                }
            }
        else if (skein_DebugFlag & SKEIN_DEBUG_ROUNDS)
            {
            uint_t j;
            u64b_t p[SKEIN_MAX_STATE_WORDS];
            const u08b_t *perm;
            const static u08b_t PERM_256 [4][ 4] = { { 0,1,2,3 }, { 0,3,2,1 }, { 0,1,2,3 }, { 0,3,2,1 } };
            const static u08b_t PERM_512 [4][ 8] = { { 0,1,2,3,4,5,6,7 },
                                                     { 2,1,4,7,6,5,0,3 },
                                                     { 4,1,6,3,0,5,2,7 },
                                                     { 6,1,0,7,2,5,4,3 }
                                                   };
            const static u08b_t PERM_1024[4][16] = { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15 },
                                                     { 0, 9, 2,13, 6,11, 4,15,10, 7,12, 3,14, 5, 8, 1 },
                                                     { 0, 7, 2, 5, 4, 3, 6, 1,12,15,14,13, 8,11,10, 9 },
                                                     { 0,15, 2,11, 6,13, 4, 9,14, 1, 8, 5,10, 3,12, 7 }
                                                   };
                    
            if ((skein_DebugFlag & SKEIN_DEBUG_PERMUTE) && (r & 3))
                {
                printf("\n%s [state after round %2d (permuted)]=\n",AlgoHeader(bits),(int)r);
                switch (bits)
                    {
                    case  256: perm = PERM_256 [r&3];   break;
                    case  512: perm = PERM_512 [r&3];   break;
                    default:   perm = PERM_1024[r&3];   break;
                    }
                for (j=0;j<bits/64;j++)
                    p[j] = X[perm[j]];
                Show64(bits/64,p);
                }
            else
                {
                printf("\n%s [state after round %2d]=\n",AlgoHeader(bits),(int)r);
                Show64(bits/64,X);
                }
            }
        }
    }

/* show state after a round (or "pseudo-round"), given a list of pointers */
void Skein_Show_R_Ptr(uint_t bits,const Skein_Ctxt_Hdr_t *h,size_t r,const u64b_t *X_ptr[])
    {
    uint_t i;
    u64b_t X[SKEIN_MAX_STATE_WORDS];

    for (i=0;i<bits/64;i++)     /* copy over the words */ 
        X[i] = X_ptr[i][0];
    Skein_Show_Round(bits,h,r,X);
    }


/* show the state at the start of a block */
void Skein_Show_Block(uint_t bits,const Skein_Ctxt_Hdr_t *h,const u64b_t *X,const u08b_t *blkPtr,
                      const u64b_t *wPtr, const u64b_t *ksPtr, const u64b_t *tsPtr)
    {
    uint_t n;
    if (skein_DebugFlag & SKEIN_DEBUG_CONFIG || ((h->T[1] & SKEIN_T1_BLK_TYPE_MASK) != SKEIN_T1_BLK_TYPE_CFG))
    if (skein_DebugFlag)
        {
        if (skein_DebugFlag & SKEIN_DEBUG_HDR)
            {
            printf("\n%s Block: outBits=%4d. T0=%06X.",AlgoHeader(bits),(uint_t) h->hashBitLen,(uint_t)h->T[0]);
            printf(" Type=");
            n = (uint_t) ((h->T[1] & SKEIN_T1_BLK_TYPE_MASK) >> SKEIN_T1_POS_BLK_TYPE);
            switch (n)
                {
                case SKEIN_BLK_TYPE_KEY:  printf("KEY. ");  break;
                case SKEIN_BLK_TYPE_CFG:  printf("CFG. ");  break;
                case SKEIN_BLK_TYPE_PERS: printf("PERS.");  break;
                case SKEIN_BLK_TYPE_PK :  printf("PK.  ");  break;
                case SKEIN_BLK_TYPE_KDF:  printf("KDF. ");  break;
                case SKEIN_BLK_TYPE_MSG:  printf("MSG. ");  break;
                case SKEIN_BLK_TYPE_OUT:  printf("OUT. ");  break;
                default:    printf("0x%02X.",n); break;
                }
            printf(" Flags=");
            printf((h->T[1] & SKEIN_T1_FLAG_FIRST)   ? " First":"      ");
            printf((h->T[1] & SKEIN_T1_FLAG_FINAL)   ? " Final":"      ");
            printf((h->T[1] & SKEIN_T1_FLAG_BIT_PAD) ? " Pad"  :"    ");
            n = (uint_t) ((h->T[1] & SKEIN_T1_TREE_LVL_MASK) >> SKEIN_T1_POS_TREE_LVL);
            if (n)
                printf("  TreeLevel = %02X",n);
            printf("\n");
            fflush(stdout);
            }
        if (skein_DebugFlag & SKEIN_DEBUG_TWEAK)
            {
            printf("  Tweak:\n");
            Show64(2,h->T);
            }
        if (skein_DebugFlag & SKEIN_DEBUG_STATE)
            {
            printf("  %s words:\n",(skein_DebugFlag & SKEIN_DEBUG_THREEFISH)?"Key":"State");
            Show64(bits/64,X);
            }
        if (skein_DebugFlag & SKEIN_DEBUG_KEYSCHED)
            {
            printf("  Tweak schedule:\n");
            Show64_flag(3,tsPtr);
            printf("  Key   schedule:\n");
            Show64_flag((bits/64)+1,ksPtr);
            }
        if (skein_DebugFlag & SKEIN_DEBUG_INPUT_64)
            {
            printf("  Input block (words):\n");
            Show64(bits/64,wPtr);
            }
        if (skein_DebugFlag & SKEIN_DEBUG_INPUT_08)
            {
            printf("  Input block (bytes):\n");
            Show08(bits/8,blkPtr);
            }
        }
    }

void Skein_Show_Key(uint_t bits,const Skein_Ctxt_Hdr_t *h,const u08b_t *key,size_t keyBytes)
    {
    if (keyBytes)
    if (skein_DebugFlag & SKEIN_DEBUG_CONFIG || ((h->T[1] & SKEIN_T1_BLK_TYPE_MASK) != SKEIN_T1_BLK_TYPE_CFG))
    if (skein_DebugFlag & SKEIN_DEBUG_KEY)
        {
        printf("\n%s MAC key = %4u bytes\n",AlgoHeader(bits),(unsigned) keyBytes);
        Show08(keyBytes,key);
        }
    }
#endif
