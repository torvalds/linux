/* Copyright (C) 2000, 2001, 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* As a special exception, if you include this header file into source
   files compiled by GCC, this header file does not by itself cause
   the resulting executable to be covered by the GNU General Public
   License.  This exception does not however invalidate any other
   reasons why the executable file might be covered by the GNU General
   Public License.  */

/* ushmedia.h: Intrinsics corresponding to SHmedia instructions that
   may be executed in both user and privileged mode.  */

#ifndef _USHMEDIA_H
#define _USHMEDIA_H

#if __SHMEDIA__
#if ! __SH4_NO_FPU
typedef float __GCC_FV __attribute__ ((vector_size (4 * sizeof (float))));
typedef float __GCC_MTRX __attribute__ ((vector_size (16 * sizeof (float))));
#endif

static __inline unsigned long long
sh_media_MABS_L (unsigned long long mm)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_absv2si2 ((v2si) mm);
}

static __inline unsigned long long
sh_media_MABS_W (unsigned long long mm)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_absv4hi2 ((v4hi) mm);
}

static __inline unsigned long long
sh_media_MADD_L (unsigned long long mm, unsigned long long mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_addv2si3 ((v2si) mm, (v2si) mn);
}

static __inline unsigned long long
sh_media_MADD_W (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_addv4hi3 ((v4hi) mm, (v4hi) mn);
}

static __inline unsigned long long
sh_media_MADDS_L (unsigned long long mm, unsigned long long mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_ssaddv2si3 ((v2si) mm, (v2si) mn);
}

static __inline unsigned long long
sh_media_MADDS_UB (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_usaddv8qi3 ((v8qi) mm, (v8qi) mn);
}

static __inline unsigned long long
sh_media_MADDS_W (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_ssaddv4hi3 ((v4hi) mm, (v4hi) mn);
}

static __inline unsigned long long
sh_media_MCMPEQ_B (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_MCMPEQ_B ((v8qi) mm,
							   (v8qi) mn);
}

static __inline unsigned long long
sh_media_MCMPEQ_L (unsigned long long mm, unsigned long long mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_sh_media_MCMPEQ_L ((v2si) mm,
							   (v2si) mn);
}

static __inline unsigned long long
sh_media_MCMPEQ_W (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MCMPEQ_W ((v4hi) mm,
							   (v4hi) mn);
}

static __inline unsigned long long
sh_media_MCMPGT_UB (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_MCMPGT_UB ((v8qi) mm,
							   (v8qi) mn);
}

static __inline unsigned long long
sh_media_MCMPGT_L (unsigned long long mm, unsigned long long mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_sh_media_MCMPGT_L ((v2si) mm,
							   (v2si) mn);
}

static __inline unsigned long long
sh_media_MCMPGT_W (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MCMPGT_W ((v4hi) mm,
							   (v4hi) mn);
}

#define sh_media_MCMV __builtin_sh_media_MCMV

static __inline unsigned long long
sh_media_MCNVS_LW (unsigned long long mm, unsigned long long mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));
  typedef unsigned int uv2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_sh_media_MCNVS_LW ((v2si) mm,
							   (uv2si) mn);
}

static __inline unsigned long long
sh_media_MCNVS_WB (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MCNVS_WB ((v4hi) mm,
							   (v4hi) mn);
}

static __inline unsigned long long
sh_media_MCNVS_WUB (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MCNVS_WUB ((v4hi) mm,
							    (v4hi) mn);
}

static __inline unsigned long long
sh_media_MEXTR1 (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_MEXTR1 ((v8qi) mm,
							 (v8qi) mn);
}

static __inline unsigned long long
sh_media_MEXTR2 (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_MEXTR2 ((v8qi) mm,
							 (v8qi) mn);
}

static __inline unsigned long long
sh_media_MEXTR3 (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_MEXTR3 ((v8qi) mm,
							 (v8qi) mn);
}

static __inline unsigned long long
sh_media_MEXTR4 (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_MEXTR4 ((v8qi) mm,
							 (v8qi) mn);
}

static __inline unsigned long long
sh_media_MEXTR5 (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_MEXTR5 ((v8qi) mm,
							 (v8qi) mn);
}

static __inline unsigned long long
sh_media_MEXTR6 (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_MEXTR6 ((v8qi) mm,
							 (v8qi) mn);
}

static __inline unsigned long long
sh_media_MEXTR7 (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_MEXTR7 ((v8qi) mm,
							 (v8qi) mn);
}

static __inline unsigned long long
sh_media_MMACFX_WL (unsigned long long mm, unsigned long long mn,
		    unsigned long long mw)
{
  typedef float v2hi __attribute__ ((mode(V2HI)));
  typedef float v2si __attribute__ ((mode(V2SI)));
  typedef unsigned int uv2si __attribute__ ((mode(V2SI)));

  long mm_l = (long) mm;
  long mn_l = (long) mn;

  return ((unsigned long long)
    __builtin_sh_media_MMACFX_WL ((v2hi) mm_l, (v2hi) mn_l,
				  (uv2si) mw));
}

static __inline unsigned long long
sh_media_MMACNFX_WL (unsigned long long mm, unsigned long long mn,
		     unsigned long long mw)
{
  typedef float v2hi __attribute__ ((mode(V2HI)));
  typedef float v2si __attribute__ ((mode(V2SI)));
  typedef unsigned int uv2si __attribute__ ((mode(V2SI)));

  long mm_l = (long) mm;
  long mn_l = (long) mn;

  return ((unsigned long long)
    __builtin_sh_media_MMACNFX_WL ((v2hi) mm_l, (v2hi) mn_l,
				   (uv2si) mw));
}

static __inline unsigned long long
sh_media_MMUL_L (unsigned long long mm, unsigned long long mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_mulv2si3 ((v2si) mm, (v2si) mn);
}

static __inline unsigned long long
sh_media_MMUL_W (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_mulv4hi3 ((v4hi) mm, (v4hi) mn);
}

static __inline unsigned long long
sh_media_MMULFX_L (unsigned long long mm, unsigned long long mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_sh_media_MMULFX_L ((v2si) mm,
							   (v2si) mn);
}

static __inline unsigned long long
sh_media_MMULFX_W (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MMULFX_W ((v4hi) mm,
							   (v4hi) mn);
}

static __inline unsigned long long
sh_media_MMULFXRP_W (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MMULFXRP_W ((v4hi) mm,
							     (v4hi) mn);
}

static __inline unsigned long long
sh_media_MMULHI_WL (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MMULHI_WL ((v4hi) mm,
							    (v4hi) mn);
}

static __inline unsigned long long
sh_media_MMULLO_WL (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MMULLO_WL ((v4hi) mm,
							    (v4hi) mn);
}

static __inline unsigned long long
sh_media_MMULSUM_WQ (unsigned long long mm, unsigned long long mn,
		     unsigned long long mw)
{
  typedef unsigned int uv4hi __attribute__ ((mode(V4HI)));

  return __builtin_sh_media_MMULSUM_WQ ((uv4hi) mm, (uv4hi) mn, mw);
}

static __inline unsigned long long
sh_media_MPERM_W (unsigned long long mm, unsigned int mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MPERM_W ((v4hi) mm, mn);
}

static __inline unsigned long long
sh_media_MSAD_UBQ (unsigned long long mm, unsigned long long mn,
		   unsigned long long mw)
{
  typedef unsigned int uv8qi __attribute__ ((mode(V8QI)));

  return __builtin_sh_media_MSAD_UBQ ((uv8qi) mm, (uv8qi) mn, mw);
}

static __inline unsigned long long
sh_media_MSHALDS_L (unsigned long long mm, unsigned int mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_sh_media_MSHALDS_L ((v2si) mm, mn);
}

static __inline unsigned long long
sh_media_MSHALDS_W (unsigned long long mm, unsigned int mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MSHALDS_W ((v4hi) mm, mn);
}

static __inline unsigned long long
sh_media_MSHARD_L (unsigned long long mm, unsigned int mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_ashrv2si3 ((v2si) mm, mn);
}

static __inline unsigned long long
sh_media_MSHARD_W (unsigned long long mm, unsigned int mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_ashrv4hi3 ((v4hi) mm, mn);
}

#define sh_media_MSHARDS_Q __builtin_sh_media_MSHARDS_Q

static __inline unsigned long long
sh_media_MSHFHI_B (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_MSHFHI_B ((v8qi) mm,
							   (v8qi) mn);
}

static __inline unsigned long long
sh_media_MSHFHI_L (unsigned long long mm, unsigned long long mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_sh_media_MSHFHI_L ((v2si) mm,
							   (v2si) mn);
}

static __inline unsigned long long
sh_media_MSHFHI_W (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MSHFHI_W ((v4hi) mm,
							   (v4hi) mn);
}

static __inline unsigned long long
sh_media_MSHFLO_B (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_MSHFLO_B ((v8qi) mm,
							   (v8qi) mn);
}

static __inline unsigned long long
sh_media_MSHFLO_L (unsigned long long mm, unsigned long long mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_sh_media_MSHFLO_L ((v2si) mm,
							   (v2si) mn);
}

static __inline unsigned long long
sh_media_MSHFLO_W (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sh_media_MSHFLO_W ((v4hi) mm,
							   (v4hi) mn);
}

static __inline unsigned long long
sh_media_MSHLLD_L (unsigned long long mm, unsigned int mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_ashlv2si3 ((v2si) mm, mn);
}

static __inline unsigned long long
sh_media_MSHLLD_W (unsigned long long mm, unsigned int mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_ashlv4hi3 ((v4hi) mm, mn);
}

static __inline unsigned long long
sh_media_MSHLRD_L (unsigned long long mm, unsigned int mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_lshrv2si3 ((v2si) mm, mn);
}

static __inline unsigned long long
sh_media_MSHLRD_W (unsigned long long mm, unsigned int mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_lshrv4hi3 ((v4hi) mm, mn);
}

static __inline unsigned long long
sh_media_MSUB_L (unsigned long long mm, unsigned long long mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_subv2si3 ((v2si) mm, (v2si) mn);
}

static __inline unsigned long long
sh_media_MSUB_W (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_subv4hi3 ((v4hi) mm, (v4hi) mn);
}

static __inline unsigned long long
sh_media_MSUBS_L (unsigned long long mm, unsigned long long mn)
{
  typedef float v2si __attribute__ ((mode(V2SI)));

  return (unsigned long long) __builtin_sssubv2si3 ((v2si) mm, (v2si) mn);
}

static __inline unsigned long long
sh_media_MSUBS_UB (unsigned long long mm, unsigned long long mn)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_ussubv8qi3 ((v8qi) mm, (v8qi) mn);
}

static __inline unsigned long long
sh_media_MSUBS_W (unsigned long long mm, unsigned long long mn)
{
  typedef float v4hi __attribute__ ((mode(V4HI)));

  return (unsigned long long) __builtin_sssubv4hi3 ((v4hi) mm, (v4hi) mn);
}

#if ! __SH4_NOFPU__
/* Floating-point Intrinsics */

#define sh_media_FABS_D __builtin_fabs
#define sh_media_FABS_S __builtin_fabsf
#define sh_media_FCMPUN_D __builtin_isunordered
#define sh_media_FCMPUN_S __builtin_isunordered

static __inline float sh_media_FCOSA_S (float fg)
{
  union { int i; float f; } u;

  u.f = fg;
  return __builtin_sh_media_FCOSA_S (u.i);
}

static __inline float
sh_media_FGETSCR (void)
{ 
  float f;

  __asm volatile ("fgetscr %0" : "=f" (f));
  return f;
}

static __inline float
sh_media_FIPR_S (const void *fvg, const void *fvh)
{
  typedef float v4sf __attribute__ ((mode(V4SF)));
  v4sf vg = *(v4sf*) fvg;
  v4sf vh = *(v4sf*) fvh;

  return __builtin_sh_media_FIPR_S (vg, vh);
}

#if 0
/* This gives different results for -O0  */
static __inline float
sh_media_FMAC_S (float fg, float fh, float fq)
{
  return fg * fh + fq;
}
#else

#define sh_media_FMAC_S __builtin_sh_media_FMAC_S
#endif

static __inline long long
sh_media_FMOV_DQ (double dg)
{
  union { long long l; double d; } u;

  u.d = dg;
  return u.l;
}

static __inline float
sh_media_FMOV_LS (int mm)
{
  union { int i; float f; } u;

  u.i = mm;
  return u.f;
}

static __inline double
sh_media_FMOV_QD (long long mm)
{
  union { long long l; double d; } u;

  u.l = mm;
  return u.d;
}

static __inline int
sh_media_FMOV_SL (float fg)
{
  union { int i; float f; } u;

  u.f = fg;
  return u.i;
}

static __inline void
sh_media_FPUTSCR (float fg)
{ 
  __asm volatile ("fputscr %0" : : "f" (fg));
}

static __inline float sh_media_FSINA_S (float fg)
{
  union { int i; float f; } u;

  u.f = fg;
  return __builtin_sh_media_FSINA_S (u.i);
}

/* Can't use __builtin_sqrt / __builtin_sqrtf because they still implement
   error handling unless -ffast-math is used.  */
#define sh_media_FSQRT_D __builtin_sh_media_FSQRT_D
#define sh_media_FSQRT_S __builtin_sh_media_FSQRT_S
#define sh_media_FSRRA_S __builtin_sh_media_FSRRA_S

static __inline void
sh_media_FTRV_S (const void *mtrxg, const void *fvh, void *fvf)
{
  typedef float v16sf __attribute__ ((mode(V16SF)));
  typedef float v4sf __attribute__ ((mode(V4SF)));
  v16sf mtrx = *(v16sf*) mtrxg;
  v4sf vh = *(v4sf*) fvh;

  *(v4sf*) fvf = __builtin_sh_media_FTRV_S (mtrx, vh);
}
#endif /* ! __SH4_NOFPU__ */

/* Not implemented here: Control and Configuration intrinsics.  */
/* Misaligned Access Support intrinsics */

static __inline unsigned long long
sh_media_LDHI_L (void *p, int s)
{
  return __builtin_sh_media_LDHI_L ((char *)p + s);
}

static __inline unsigned long long
sh_media_LDHI_Q (void *p, int s)
{
  return __builtin_sh_media_LDHI_Q ((char *)p + s);
}

static __inline unsigned long long
sh_media_LDLO_L (void *p, int s)
{
  return __builtin_sh_media_LDLO_L ((char *)p + s);
}

static __inline unsigned long long
sh_media_LDLO_Q (void *p, int s)
{
  return __builtin_sh_media_LDLO_Q ((char *)p + s);
}

static __inline void
sh_media_STHI_L (void *p, int s, unsigned int mw)
{
  __builtin_sh_media_STHI_L ((char*)p + s, mw);
}

static __inline void
sh_media_STHI_Q (void *p, int s, unsigned long long mw)
{
  __builtin_sh_media_STHI_Q ((char*)p + s, mw);
}

static __inline void
sh_media_STLO_L (void *p, int s, unsigned int mw)
{
  __builtin_sh_media_STLO_L ((char*)p + s, mw);
}

static __inline void
sh_media_STLO_Q (void *p, int s, unsigned long long mw)
{
  __builtin_sh_media_STLO_Q ((char*)p + s, mw);
}

/* Miscellaneous intrinsics */

#define sh_media_NSB __builtin_sh_media_NSB

static __inline unsigned long long
sh_media_BYTEREV (unsigned long long mm)
{
  typedef float v8qi __attribute__ ((mode(V8QI)));

  return (unsigned long long) __builtin_sh_media_BYTEREV ((v8qi) mm);
}

__inline__ static unsigned long long
sh_media_CMVEQ (unsigned long long mm, unsigned long long mn, unsigned long long mw) __attribute__ ((always_inline));

__inline__ static unsigned long long
sh_media_CMVEQ (unsigned long long mm, unsigned long long mn, unsigned long long mw)
{
  return mm == 0 ? mn : mw;
}

__inline__ static unsigned long long
sh_media_CMVNE (unsigned long long mm, unsigned long long mn, unsigned long long mw) __attribute__ ((always_inline));

__inline__ static unsigned long long
sh_media_CMVNE (unsigned long long mm, unsigned long long mn, unsigned long long mw)
{
  return mm != 0 ? mn : mw;
}

static __inline long long
sh_media_ADDZ_L (unsigned int mm, unsigned int mn)
{
  return mm + mn;
}

/* NOP and Synchronization intrinsics not implemented here.  */

static __inline__ void sh_media_PREFO(void *mm, int s)
{
  __builtin_sh_media_PREFO (mm + s, 0, 0);
}

/* Event Handling intrinsics not implemented here.  */

/* Old asm stuff */

static __inline__
void
sh_media_NOP (void)
{
  __asm__ ("nop" : :);
}

__inline__ static
unsigned long long
sh_media_SWAP_Q (void *mm, long long mn, unsigned long long mw)
{
  unsigned long long res;
  unsigned long long *addr = (unsigned long long *)((char *)mm + mn);
  __asm__ ("swap.q	%m1, %0" : "=r" (res), "+o" (*addr) : "0" (mw));
  return res;
}

__inline__ static
void     
sh_media_SYNCI (void)
{
  __asm__ __volatile__ ("synci");
}

__inline__ static
void     
sh_media_SYNCO (void)
{
  __asm__ __volatile__ ("synco");
}

__inline__ static
void
sh_media_ALLOCO (void *mm, int s)
{
  __builtin_sh_media_ALLOCO (mm + s);
}

__inline__ static
void
sh_media_ICBI (void *mm, int s)
{
  __asm__ __volatile__ ("icbi	%m0" : : "o" (((char*)mm)[s]));
}

__inline__ static
void
sh_media_OCBI (void *mm, int s)
{
  __asm__ __volatile__ ("ocbi	%m0" : : "o" (((char*)mm)[s]));
}

__inline__ static
void
sh_media_OCBP (void *mm, int s)
{
  __asm__ __volatile__ ("ocbp	%m0" : : "o" (((char*)mm)[s]));
}

__inline__ static
void
sh_media_OCBWB (void *mm, int s)
{
  __asm__ __volatile__ ("ocbwb	%m0" : : "o" (((char*)mm)[s]));
}

__inline__ static
void
sh_media_PREFI (void *mm, int s)
{
  __asm__ __volatile__ ("prefi	%m0" : : "o" (((char*)mm)[s]));
}

__inline__ static
void
sh_media_BRK (void)
{
  __asm__ __volatile__ ("brk");
}

__inline__ static
void
sh_media_TRAPA (unsigned long long mm)
{
  __asm__ __volatile__ ("trapa	%%0" : : "r" (mm));
}

__inline__ static
short         
sh_media_unaligned_LD_W (void *p)
{
#if __LITTLE_ENDIAN__
  return (((unsigned char *)p)[0]
	  | (((short)((__signed__ char *)p)[1]) << 8));
#else
  return ((((short)((__signed__ char *)p)[0]) << 8)
	  | ((unsigned char *)p)[1]);
#endif
}

__inline__ static
unsigned short
sh_media_unaligned_LD_UW (void *p)
{
  unsigned char *addr = p;
#if __LITTLE_ENDIAN__
  return sh_media_MSHFLO_B (addr[0], addr[1]);
#else
  return sh_media_MSHFLO_B (addr[1], addr[0]);
#endif
}

/* We don't use the sh_media_LD* functions here because that turned out
   to impede constant propagation of the offsets into the ldhi / ldlo
   instructions.  */
__inline__ static
int           
sh_media_unaligned_LD_L (void *p)
{
#if __LITTLE_ENDIAN__
  return (__builtin_sh_media_LDHI_L ((char *)p + 3)
	  | __builtin_sh_media_LDLO_L (p));
#else
  return (__builtin_sh_media_LDLO_L ((char *)p + 3)
	  | __builtin_sh_media_LDHI_L (p));
#endif
}

__inline__ static
long long     
sh_media_unaligned_LD_Q (void *p)
{
#if __LITTLE_ENDIAN__
  return (__builtin_sh_media_LDHI_Q ((char *)p + 7)
	  | __builtin_sh_media_LDLO_Q (p));
#else
  return (__builtin_sh_media_LDLO_Q ((char *)p + 7)
	  | __builtin_sh_media_LDHI_Q (p));
#endif
}

__inline__ static
void
sh_media_unaligned_ST_W (void *p, unsigned int k)
{
  char *addr = p;
#if __LITTLE_ENDIAN__
  addr[0] = k;
  addr[1] = k >> 8;
#else
  addr[1] = k;
  addr[0] = k >> 8;
#endif
}

/* We don't use the sh_media_ST* functions here because that turned out
   to impede constant propagation of the offsets into the ldhi / ldlo
   instructions.  */
__inline__ static
void
sh_media_unaligned_ST_L (void *p, unsigned int k)
{
#if __LITTLE_ENDIAN__
  __builtin_sh_media_STHI_L (p + 3, k);
  __builtin_sh_media_STLO_L (p, k);
#else
  __builtin_sh_media_STLO_L (p + 3, k);
  __builtin_sh_media_STHI_L (p, k);
#endif
}

__inline__ static
void
sh_media_unaligned_ST_Q (void *p, unsigned long long k)
{
#if __LITTLE_ENDIAN__
  __builtin_sh_media_STHI_Q (p + 7, k);
  __builtin_sh_media_STLO_Q (p, k);
#else
  __builtin_sh_media_STLO_Q (p + 7, k);
  __builtin_sh_media_STHI_Q (p, k);
#endif
}

#if ! __SH4_NOFPU__
__inline__ static
void
sh_media_FVCOPY_S (const void *fvg, void *fvf)
{
  const __GCC_FV *g = fvg;
  __GCC_FV *f = fvf;
  *f = *g;
}

__inline__ static
void
sh_media_FVADD_S (const void *fvg, const void *fvh, void *fvf)
{
  const float *g = fvg, *h = fvh;
  float *f = fvf;
#if 1
  int i;

  for (i = 0; i < 4; i++)
    f[i] = g[i] + h[i];
#else
  f[0] = g[0] + h[0];
  f[1] = g[1] + h[1];
  f[2] = g[2] + h[2];
  f[3] = g[3] + h[3];
#endif
}

__inline__ static
void
sh_media_FVSUB_S (const void *fvg, const void *fvh, void *fvf)
{
  const float *g = fvg, *h = fvh;
  float *f = fvf;
#if 1
  int i;

  for (i = 0; i < 4; i++)
    f[i] = g[i] - h[i];
#else
  f[0] = g[0] - h[0];
  f[1] = g[1] - h[1];
  f[2] = g[2] - h[2];
  f[3] = g[3] - h[3];
#endif
}

__inline__ static
void
sh_media_FMTRXCOPY_S (const void *mtrxg, void *mtrxf)
{
  const __GCC_MTRX *g = mtrxg;
  __GCC_MTRX *f = mtrxf;
  *f = *g;
}

__inline__ static
void
sh_media_FMTRXADD_S (const void *mtrxg, const void *mtrxh, void *mtrxf)
{
  const __GCC_FV *g = mtrxg, *h = mtrxh;
  __GCC_FV *f = mtrxf;
#if 1
  int i;

  for (i = 0; i < 4; i++)
    sh_media_FVADD_S (&g[i], &h[i], &f[i]);
#else
  sh_media_FVADD_S (&g[0], &h[0], &f[0]);
  sh_media_FVADD_S (&g[1], &h[1], &f[1]);
  sh_media_FVADD_S (&g[2], &h[2], &f[2]);
  sh_media_FVADD_S (&g[3], &h[3], &f[3]);
#endif
}

__inline__ static
void
sh_media_FMTRXSUB_S (const void *mtrxg, const void *mtrxh, void *mtrxf)
{
  const __GCC_FV *g = mtrxg, *h = mtrxh;
  __GCC_FV *f = mtrxf;
#if 1
  int i;

  for (i = 0; i < 4; i++)
    sh_media_FVSUB_S (&g[i], &h[i], &f[i]);
#else
  sh_media_FVSUB_S (&g[0], &h[0], &f[0]);
  sh_media_FVSUB_S (&g[1], &h[1], &f[1]);
  sh_media_FVSUB_S (&g[2], &h[2], &f[2]);
  sh_media_FVSUB_S (&g[3], &h[3], &f[3]);
#endif
}

__inline__ static
void
sh_media_FTRVADD_S (const void *mtrxg, const void *fvh, const void *fvi, void *fvf)
{
  sh_media_FTRV_S (mtrxg, fvh, fvf);
  sh_media_FVADD_S (fvf, fvi, fvf);
}

__inline__ static
void
sh_media_FTRVSUB_S (const void *mtrxg, const void *fvh, const void *fvi, void *fvf)
{
  sh_media_FTRV_S (mtrxg, fvh, fvf);
  sh_media_FVSUB_S (fvf, fvi, fvf);
}

__inline__ static
void
sh_media_FMTRXMUL_S (const void *mtrxg, const void *mtrxh, void *mtrxf)
{
  const __GCC_FV *g = mtrxg;
  __GCC_FV *f = mtrxf;
#if 1
  int j;

  for (j = 0; j < 4; j++)
    sh_media_FTRV_S (mtrxh, &g[j], &f[j]);
#else
  sh_media_FTRV_S (mtrxh, &g[0], &f[0]);
  sh_media_FTRV_S (mtrxh, &g[1], &f[1]);
  sh_media_FTRV_S (mtrxh, &g[2], &f[2]);
  sh_media_FTRV_S (mtrxh, &g[3], &f[3]);
#endif
}

__inline__ static
void
sh_media_FMTRXMULADD_S (const void *mtrxg, const void *mtrxh, const void *mtrxi, void *mtrxf)
{
  const __GCC_FV *g = mtrxg, *i = mtrxi;
  __GCC_FV *f = mtrxf;
#if 1
  int j;

  for (j = 0; j < 4; j++)
    sh_media_FTRVADD_S (mtrxh, &g[j], &i[j], &f[j]);
#else
  sh_media_FTRVADD_S (mtrxh, &g[0], &i[0], &f[0]);
  sh_media_FTRVADD_S (mtrxh, &g[1], &i[1], &f[1]);
  sh_media_FTRVADD_S (mtrxh, &g[2], &i[2], &f[2]);
  sh_media_FTRVADD_S (mtrxh, &g[3], &i[3], &f[3]);
#endif
}

__inline__ static
void
sh_media_FMTRXMULSUB_S (const void *mtrxg, const void *mtrxh, const void *mtrxi, void *mtrxf)
{
  const __GCC_FV *g = mtrxg, *i = mtrxi;
  __GCC_FV *f = mtrxf;
#if 1
  int j;

  for (j = 0; j < 4; j++)
    sh_media_FTRVSUB_S (mtrxh, &g[j], &i[j], &f[j]);
#else
  sh_media_FTRVSUB_S (mtrxh, &g[0], &i[0], &f[0]);
  sh_media_FTRVSUB_S (mtrxh, &g[1], &i[1], &f[1]);
  sh_media_FTRVSUB_S (mtrxh, &g[2], &i[2], &f[2]);
  sh_media_FTRVSUB_S (mtrxh, &g[3], &i[3], &f[3]);
#endif
}
#endif /* ! __SH4_NOFPU__ */

#endif /* __SHMEDIA__ */

#endif /* _USHMEDIA_H */
