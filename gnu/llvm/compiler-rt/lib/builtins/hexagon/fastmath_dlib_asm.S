//===----------------------Hexagon builtin routine ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/* ==================================================================== */
/*   FUNCTIONS Optimized double floating point operators                */
/* ==================================================================== */
/*      c = dadd_asm(a, b)                                              */
/* ====================================================================

QDOUBLE dadd(QDOUBLE a,QDOUBLE b) {
      QDOUBLE c;
      lint manta = a & MANTMASK;
      int  expa  = HEXAGON_R_sxth_R(a) ;
      lint mantb = b & MANTMASK;
      int  expb  = HEXAGON_R_sxth_R(b) ;
      int  exp, expdiff, j, k, hi, lo, cn;
      lint mant;

        expdiff = (int) HEXAGON_P_vabsdiffh_PP(a, b);
        expdiff = HEXAGON_R_sxth_R(expdiff) ;
        if (expdiff > 63) { expdiff = 62;}
        if (expa > expb) {
          exp = expa + 1;
          expa = 1;
          expb = expdiff + 1;
        } else {
          exp = expb + 1;
          expb = 1;
          expa = expdiff + 1;
        }
        mant = (manta>>expa) + (mantb>>expb);

        hi = (int) (mant>>32);
        lo = (int) (mant);

        k =  HEXAGON_R_normamt_R(hi);
        if(hi == 0 || hi == -1) k =  31+HEXAGON_R_normamt_R(lo);

        mant = (mant << k);
        cn  = (mant == 0x8000000000000000LL);
        exp = exp - k + cn;

        if (mant ==  0 || mant == -1)  exp = 0x8001;
        c = (mant & MANTMASK) | (((lint) exp) & EXP_MASK);
      return(c);
 }
 * ==================================================================== */
        .text
        .global dadd_asm
        .type dadd_asm, @function
dadd_asm:

#define manta      R0
#define mantexpa   R1:0
#define lmanta     R1:0
#define mantb      R2
#define mantexpb   R3:2
#define lmantb     R3:2
#define expa       R4
#define expb       R5
#define mantexpd   R7:6
#define expd       R6
#define exp        R8
#define c63        R9
#define lmant      R1:0
#define manth      R1
#define mantl      R0
#define zero       R7:6
#define zerol      R6
#define minus      R3:2
#define minusl     R2
#define maxneg     R9
#define minmin     R11:10  // exactly 0x800000000000000000LL
#define minminh    R11
#define k          R4
#define kl         R5
#define ce         P0
        .falign
      {
        mantexpd = VABSDIFFH(mantexpa, mantexpb) //represented as 0x08001LL
        c63 = #62
        expa = SXTH(manta)
        expb = SXTH(mantb)
      } {
        expd = SXTH(expd)
        ce = CMP.GT(expa, expb);
        if ( ce.new) exp = add(expa, #1)
        if (!ce.new) exp = add(expb, #1)
      } {
        if ( ce) expa = #1
        if (!ce) expb = #1
        manta.L = #0
        expd = MIN(expd, c63)
      } {
        if (!ce) expa = add(expd, #1)
        if ( ce) expb = add(expd, #1)
        mantb.L = #0
        zero = #0
      } {
        lmanta = ASR(lmanta, expa)
        lmantb = ASR(lmantb, expb)
        minmin = #0
      } {
        lmant = add(lmanta, lmantb)
        minus = #-1
        minminh.H = #0x8000
      } {
        k  = NORMAMT(manth)
        kl = NORMAMT(mantl)
        p0 = cmp.eq(manth, zerol)
        p1 = cmp.eq(manth, minusl)
      } {
        p0 = OR(p0, p1)
        if(p0.new) k = add(kl, #31)
        maxneg.H = #0
      } {
        mantexpa = ASL(lmant, k)
        exp = SUB(exp, k)
        maxneg.L = #0x8001
      } {
        p0 = cmp.eq(mantexpa, zero)
        p1 = cmp.eq(mantexpa, minus)
        manta.L = #0
        exp = ZXTH(exp)
      } {
        p2 = cmp.eq(mantexpa, minmin)    //is result 0x80....0
        if(p2.new) exp = add(exp, #1)
      }
#if (__HEXAGON_ARCH__ == 60)
      {
        p0 = OR(p0, p1)
        if( p0.new) manta = OR(manta,maxneg)
        if(!p0.new) manta = OR(manta,exp)
      }
        jumpr  r31
#else
      {
        p0 = OR(p0, p1)
        if( p0.new) manta = OR(manta,maxneg)
        if(!p0.new) manta = OR(manta,exp)
        jumpr  r31
      }
#endif
/* =================================================================== *
 QDOUBLE dsub(QDOUBLE a,QDOUBLE b) {
      QDOUBLE c;
      lint manta = a & MANTMASK;
      int  expa  = HEXAGON_R_sxth_R(a) ;
      lint mantb = b & MANTMASK;
      int  expb  = HEXAGON_R_sxth_R(b) ;
      int  exp, expdiff, j, k, hi, lo, cn;
      lint mant;

        expdiff = (int) HEXAGON_P_vabsdiffh_PP(a, b);
        expdiff = HEXAGON_R_sxth_R(expdiff) ;
        if (expdiff > 63) { expdiff = 62;}
        if (expa > expb) {
          exp = expa + 1;
          expa = 1;
          expb = expdiff + 1;
        } else {
          exp = expb + 1;
          expb = 1;
          expa = expdiff + 1;
        }
        mant = (manta>>expa) - (mantb>>expb);

        hi = (int) (mant>>32);
        lo = (int) (mant);

        k =  HEXAGON_R_normamt_R(hi);
        if(hi == 0 || hi == -1) k =  31+HEXAGON_R_normamt_R(lo);

        mant = (mant << k);
        cn  = (mant == 0x8000000000000000LL);
        exp = exp - k + cn;

        if (mant ==  0 || mant == -1)  exp = 0x8001;
        c = (mant & MANTMASK) | (((lint) exp) & EXP_MASK);
      return(c);
 }
 * ==================================================================== */
        .text
        .global dsub_asm
        .type dsub_asm, @function
dsub_asm:

#define manta      R0
#define mantexpa   R1:0
#define lmanta     R1:0
#define mantb      R2
#define mantexpb   R3:2
#define lmantb     R3:2
#define expa       R4
#define expb       R5
#define mantexpd   R7:6
#define expd       R6
#define exp        R8
#define c63        R9
#define lmant      R1:0
#define manth      R1
#define mantl      R0
#define zero       R7:6
#define zerol      R6
#define minus      R3:2
#define minusl     R2
#define maxneg     R9
#define minmin     R11:10  // exactly 0x800000000000000000LL
#define minminh    R11
#define k          R4
#define kl         R5
#define ce         P0
        .falign
      {
        mantexpd = VABSDIFFH(mantexpa, mantexpb) //represented as 0x08001LL
        c63 = #62
        expa = SXTH(manta)
        expb = SXTH(mantb)
      } {
        expd = SXTH(expd)
        ce = CMP.GT(expa, expb);
        if ( ce.new) exp = add(expa, #1)
        if (!ce.new) exp = add(expb, #1)
      } {
        if ( ce) expa = #1
        if (!ce) expb = #1
        manta.L = #0
        expd = MIN(expd, c63)
      } {
        if (!ce) expa = add(expd, #1)
        if ( ce) expb = add(expd, #1)
        mantb.L = #0
        zero = #0
      } {
        lmanta = ASR(lmanta, expa)
        lmantb = ASR(lmantb, expb)
        minmin = #0
      } {
        lmant = sub(lmanta, lmantb)
        minus = #-1
        minminh.H = #0x8000
      } {
        k  = NORMAMT(manth)
        kl = NORMAMT(mantl)
        p0 = cmp.eq(manth, zerol)
        p1 = cmp.eq(manth, minusl)
      } {
        p0 = OR(p0, p1)
        if(p0.new) k = add(kl, #31)
        maxneg.H = #0
      } {
        mantexpa = ASL(lmant, k)
        exp = SUB(exp, k)
        maxneg.L = #0x8001
      } {
        p0 = cmp.eq(mantexpa, zero)
        p1 = cmp.eq(mantexpa, minus)
        manta.L = #0
        exp = ZXTH(exp)
      } {
        p2 = cmp.eq(mantexpa, minmin)    //is result 0x80....0
        if(p2.new) exp = add(exp, #1)
      }
#if (__HEXAGON_ARCH__ == 60)
      {
        p0 = OR(p0, p1)
        if( p0.new) manta = OR(manta,maxneg)
        if(!p0.new) manta = OR(manta,exp)
      }
        jumpr  r31
#else
      {
        p0 = OR(p0, p1)
        if( p0.new) manta = OR(manta,maxneg)
        if(!p0.new) manta = OR(manta,exp)
        jumpr  r31
      }
#endif
/* ==================================================================== *
 QDOUBLE dmpy(QDOUBLE a,QDOUBLE b) {
        QDOUBLE c;
        lint manta = a & MANTMASK;
        int  expa  = HEXAGON_R_sxth_R(a) ;
        lint mantb = b & MANTMASK;
        int  expb  = HEXAGON_R_sxth_R(b) ;
        int exp, k;
        lint mant;
        int          hia, hib, hi, lo;
        unsigned int loa, lob;

        hia = (int)(a >> 32);
        loa = HEXAGON_R_extractu_RII((int)manta, 31, 1);
        hib = (int)(b >> 32);
        lob = HEXAGON_R_extractu_RII((int)mantb, 31, 1);

        mant = HEXAGON_P_mpy_RR(hia, lob);
        mant = HEXAGON_P_mpyacc_RR(mant,hib, loa);
        mant = (mant >> 30) + (HEXAGON_P_mpy_RR(hia, hib)<<1);

        hi = (int) (mant>>32);
        lo = (int) (mant);

        k =  HEXAGON_R_normamt_R(hi);
        if(hi == 0 || hi == -1) k =  31+HEXAGON_R_normamt_R(lo);
        mant = mant << k;
        exp = expa + expb - k;
        if (mant ==  0 || mant == -1)  exp = 0x8001;
        c = (mant & MANTMASK) | (((lint) exp) & EXP_MASK);
        return(c);
 }
 * ==================================================================== */
        .text
        .global dmpy_asm
        .type dmpy_asm, @function
dmpy_asm:

#define mantal     R0
#define mantah     R1
#define mantexpa   R1:0
#define mantbl     R2
#define mantbh     R3
#define mantexpb   R3:2
#define expa       R4
#define expb       R5
#define mantexpd   R7:6
#define exp        R8
#define lmantc     R11:10
#define mantch     R11
#define mantcl     R10
#define zero0      R7:6
#define zero0l     R6
#define minus1     R3:2
#define minus1l    R2
#define maxneg     R9
#define k          R4
#define kl         R5

        .falign
      {
        mantbl = lsr(mantbl, #16)
        mantal = lsr(mantal, #16)
        expa = sxth(mantal)
        expb = sxth(mantbl)
      }
      {
        lmantc = mpy(mantah, mantbh)
        mantexpd = mpy(mantah, mantbl)
      }
      {
        lmantc = add(lmantc, lmantc) //<<1
        mantexpd+= mpy(mantbh, mantal)
      }
      {
        lmantc += asr(mantexpd, #15)
        exp = add(expa, expb)
        zero0 = #0
        minus1 = #-1
      }
      {
        k  = normamt(mantch)
        kl = normamt(mantcl)
        p0 = cmp.eq(mantch, zero0l)
        p1 = cmp.eq(mantch, minus1l)
      }
      {
        p0 = or(p0, p1)
        if(p0.new) k = add(kl, #31)
        maxneg.H = #0
      }
      {
        mantexpa = asl(lmantc, k)
        exp = sub(exp, k)
        maxneg.L = #0x8001
      }
      {
        p0 = cmp.eq(mantexpa, zero0)
        p1 = cmp.eq(mantexpa, minus1)
        mantal.L = #0
        exp = zxth(exp)
      }
#if (__HEXAGON_ARCH__ == 60)
      {
        p0 = or(p0, p1)
        if( p0.new) mantal = or(mantal,maxneg)
        if(!p0.new) mantal = or(mantal,exp)
      }
        jumpr  r31
#else
      {
        p0 = or(p0, p1)
        if( p0.new) mantal = or(mantal,maxneg)
        if(!p0.new) mantal = or(mantal,exp)
        jumpr  r31
      }
#endif
