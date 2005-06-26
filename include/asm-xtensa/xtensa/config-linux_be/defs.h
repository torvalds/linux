/* Definitions for Xtensa instructions, types, and protos. */

/*
 * Copyright (c) 2003 Tensilica, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2.1 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307,
 * USA.
 */

/* Do not modify. This is automatically generated.*/

#ifndef _XTENSA_BASE_HEADER
#define _XTENSA_BASE_HEADER

#ifdef __XTENSA__
#if defined(__GNUC__) && !defined(__XCC__)

#define L8UI_ASM(arr, ars, imm) { \
  __asm__ volatile("l8ui %0, %1, %2" : "=a" (arr) : "a" (ars) , "i" (imm)); \
}

#define XT_L8UI(ars, imm) \
({ \
  unsigned char _arr; \
  const unsigned char *_ars = ars; \
  L8UI_ASM(_arr, _ars, imm); \
  _arr; \
})

#define L16UI_ASM(arr, ars, imm) { \
  __asm__ volatile("l16ui %0, %1, %2" : "=a" (arr) : "a" (ars) , "i" (imm)); \
}

#define XT_L16UI(ars, imm) \
({ \
  unsigned short _arr; \
  const unsigned short *_ars = ars; \
  L16UI_ASM(_arr, _ars, imm); \
  _arr; \
})

#define L16SI_ASM(arr, ars, imm) {\
  __asm__ volatile("l16si %0, %1, %2" : "=a" (arr) : "a" (ars) , "i" (imm)); \
}

#define XT_L16SI(ars, imm) \
({ \
  signed short _arr; \
  const signed short *_ars = ars; \
  L16SI_ASM(_arr, _ars, imm); \
  _arr; \
})

#define L32I_ASM(arr, ars, imm) { \
  __asm__ volatile("l32i %0, %1, %2" : "=a" (arr) : "a" (ars) , "i" (imm)); \
}

#define XT_L32I(ars, imm) \
({ \
  unsigned _arr; \
  const unsigned *_ars = ars; \
  L32I_ASM(_arr, _ars, imm); \
  _arr; \
})

#define S8I_ASM(arr, ars, imm) {\
  __asm__ volatile("s8i %0, %1, %2" : : "a" (arr), "a" (ars) , "i" (imm) : "memory" ); \
}

#define XT_S8I(arr, ars, imm) \
({ \
  signed char _arr = arr; \
  const signed char *_ars = ars; \
  S8I_ASM(_arr, _ars, imm); \
})

#define S16I_ASM(arr, ars, imm) {\
  __asm__ volatile("s16i %0, %1, %2" : : "a" (arr), "a" (ars) , "i" (imm) : "memory" ); \
}

#define XT_S16I(arr, ars, imm) \
({ \
  signed short _arr = arr; \
  const signed short *_ars = ars; \
  S16I_ASM(_arr, _ars, imm); \
})

#define S32I_ASM(arr, ars, imm) { \
  __asm__ volatile("s32i %0, %1, %2" : : "a" (arr), "a" (ars) , "i" (imm) : "memory" ); \
}

#define XT_S32I(arr, ars, imm) \
({ \
  signed int _arr = arr; \
  const signed int *_ars = ars; \
  S32I_ASM(_arr, _ars, imm); \
})

#define ADDI_ASM(art, ars, imm) {\
   __asm__ ("addi %0, %1, %2" : "=a" (art) : "a" (ars), "i" (imm)); \
}

#define XT_ADDI(ars, imm) \
({ \
   unsigned _art; \
   unsigned _ars = ars; \
   ADDI_ASM(_art, _ars, imm); \
   _art; \
})

#define ABS_ASM(arr, art) {\
   __asm__ ("abs %0, %1" : "=a" (arr) : "a" (art)); \
}

#define XT_ABS(art) \
({ \
   unsigned _arr; \
   signed _art = art; \
   ABS_ASM(_arr, _art); \
   _arr; \
})

/* Note: In the following macros that reference SAR, the magic "state"
   register is used to capture the dependency on SAR.  This is because
   SAR is a 5-bit register and thus there are no C types that can be
   used to represent it.  It doesn't appear that the SAR register is
   even relevant to GCC, but it is marked as "clobbered" just in
   case.  */

#define SRC_ASM(arr, ars, art) {\
   register int _xt_sar __asm__ ("state"); \
   __asm__ ("src %0, %1, %2" \
	    : "=a" (arr) : "a" (ars), "a" (art), "t" (_xt_sar)); \
}

#define XT_SRC(ars, art) \
({ \
   unsigned _arr; \
   unsigned _ars = ars; \
   unsigned _art = art; \
   SRC_ASM(_arr, _ars, _art); \
   _arr; \
})

#define SSR_ASM(ars) {\
   register int _xt_sar __asm__ ("state"); \
   __asm__ ("ssr %1" : "=t" (_xt_sar) : "a" (ars) : "sar"); \
}

#define XT_SSR(ars) \
({ \
   unsigned _ars = ars; \
   SSR_ASM(_ars); \
})

#define SSL_ASM(ars) {\
   register int _xt_sar __asm__ ("state"); \
   __asm__ ("ssl %1" : "=t" (_xt_sar) : "a" (ars) : "sar"); \
}

#define XT_SSL(ars) \
({ \
   unsigned _ars = ars; \
   SSL_ASM(_ars); \
})

#define SSA8B_ASM(ars) {\
   register int _xt_sar __asm__ ("state"); \
   __asm__ ("ssa8b %1" : "=t" (_xt_sar) : "a" (ars) : "sar"); \
}

#define XT_SSA8B(ars) \
({ \
   unsigned _ars = ars; \
   SSA8B_ASM(_ars); \
})

#define SSA8L_ASM(ars) {\
   register int _xt_sar __asm__ ("state"); \
   __asm__ ("ssa8l %1" : "=t" (_xt_sar) : "a" (ars) : "sar"); \
}

#define XT_SSA8L(ars) \
({ \
   unsigned _ars = ars; \
   SSA8L_ASM(_ars); \
})

#define SSAI_ASM(imm) {\
   register int _xt_sar __asm__ ("state"); \
   __asm__ ("ssai %1" : "=t" (_xt_sar) : "i" (imm) : "sar"); \
}

#define XT_SSAI(imm) \
({ \
   SSAI_ASM(imm); \
})








#endif /* __GNUC__ && !__XCC__ */

#ifdef __XCC__

/* Core load/store instructions */
extern unsigned char _TIE_L8UI(const unsigned char * ars, immediate imm);
extern unsigned short _TIE_L16UI(const unsigned short * ars, immediate imm);
extern signed short _TIE_L16SI(const signed short * ars, immediate imm);
extern unsigned _TIE_L32I(const unsigned * ars, immediate imm);
extern void _TIE_S8I(unsigned char arr, unsigned char * ars, immediate imm);
extern void _TIE_S16I(unsigned short arr, unsigned short * ars, immediate imm);
extern void _TIE_S32I(unsigned arr, unsigned * ars, immediate imm);

#define XT_L8UI  _TIE_L8UI
#define XT_L16UI _TIE_L16UI
#define XT_L16SI _TIE_L16SI
#define XT_L32I  _TIE_L32I
#define XT_S8I   _TIE_S8I
#define XT_S16I  _TIE_S16I
#define XT_S32I  _TIE_S32I

/* Add-immediate instruction */
extern unsigned _TIE_ADDI(unsigned ars, immediate imm);
#define XT_ADDI  _TIE_ADDI

/* Absolute value instruction */
extern unsigned _TIE_ABS(int art);
#define XT_ABS _TIE_ABS

/* funnel shift instructions */
extern unsigned _TIE_SRC(unsigned ars, unsigned art);
#define XT_SRC _TIE_SRC
extern void _TIE_SSR(unsigned ars);
#define XT_SSR _TIE_SSR
extern void _TIE_SSL(unsigned ars);
#define XT_SSL _TIE_SSL
extern void _TIE_SSA8B(unsigned ars);
#define XT_SSA8B _TIE_SSA8B
extern void _TIE_SSA8L(unsigned ars);
#define XT_SSA8L _TIE_SSA8L
extern void _TIE_SSAI(immediate imm);
#define XT_SSAI _TIE_SSAI


#endif /* __XCC__ */

#endif /* __XTENSA__ */
#endif /* !_XTENSA_BASE_HEADER */
