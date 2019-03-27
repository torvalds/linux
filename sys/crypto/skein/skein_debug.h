#ifndef _SKEIN_DEBUG_H_
#define _SKEIN_DEBUG_H_
/***********************************************************************
**
** Interface definitions for Skein hashing debug output.
**
** Source code author: Doug Whiting, 2008.
**
** This algorithm and source code is released to the public domain.
**
************************************************************************/

#ifdef  SKEIN_DEBUG
/* callout functions used inside Skein code */
void    Skein_Show_Block(uint_t bits,const Skein_Ctxt_Hdr_t *h,const u64b_t *X,const u08b_t *blkPtr,
                         const u64b_t *wPtr,const u64b_t *ksPtr,const u64b_t *tsPtr);
void    Skein_Show_Round(uint_t bits,const Skein_Ctxt_Hdr_t *h,size_t r,const u64b_t *X);
void    Skein_Show_R_Ptr(uint_t bits,const Skein_Ctxt_Hdr_t *h,size_t r,const u64b_t *X_ptr[]);
void    Skein_Show_Final(uint_t bits,const Skein_Ctxt_Hdr_t *h,size_t cnt,const u08b_t *outPtr);
void    Skein_Show_Key  (uint_t bits,const Skein_Ctxt_Hdr_t *h,const u08b_t *key,size_t keyBytes);

extern  uint_t skein_DebugFlag;            /* flags to control debug output (0 --> none) */

#define SKEIN_RND_SPECIAL       (1000u)
#define SKEIN_RND_KEY_INITIAL   (SKEIN_RND_SPECIAL+0u)
#define SKEIN_RND_KEY_INJECT    (SKEIN_RND_SPECIAL+1u)
#define SKEIN_RND_FEED_FWD      (SKEIN_RND_SPECIAL+2u)

/* flag bits:  skein_DebugFlag */
#define SKEIN_DEBUG_KEY         (1u << 1)  /* show MAC key */
#define SKEIN_DEBUG_CONFIG      (1u << 2)  /* show config block processing */
#define SKEIN_DEBUG_STATE       (1u << 3)  /* show input state during Show_Block() */
#define SKEIN_DEBUG_TWEAK       (1u << 4)  /* show input state during Show_Block() */
#define SKEIN_DEBUG_KEYSCHED    (1u << 5)  /* show expanded key schedule */
#define SKEIN_DEBUG_INPUT_64    (1u << 6)  /* show input block as 64-bit words */
#define SKEIN_DEBUG_INPUT_08    (1u << 7)  /* show input block as  8-bit bytes */
#define SKEIN_DEBUG_INJECT      (1u << 8)  /* show state after key injection & feedforward points */
#define SKEIN_DEBUG_ROUNDS      (1u << 9)  /* show state after all rounds */
#define SKEIN_DEBUG_FINAL       (1u <<10)  /* show final output of Skein */
#define SKEIN_DEBUG_HDR         (1u <<11)  /* show block header */
#define SKEIN_DEBUG_THREEFISH   (1u <<12)  /* use Threefish name instead of Skein */
#define SKEIN_DEBUG_PERMUTE     (1u <<13)  /* use word permutations */
#define SKEIN_DEBUG_ALL         ((~0u) & ~(SKEIN_DEBUG_THREEFISH | SKEIN_DEBUG_PERMUTE))
#define THREEFISH_DEBUG_ALL     (SKEIN_DEBUG_ALL | SKEIN_DEBUG_THREEFISH)

#endif /*  SKEIN_DEBUG    */

#endif /* _SKEIN_DEBUG_H_ */
