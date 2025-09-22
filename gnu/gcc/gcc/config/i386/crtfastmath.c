/*
 * Copyright (C) 2005 Free Software Foundation, Inc.
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 * 
 * In addition to the permissions in the GNU General Public License, the
 * Free Software Foundation gives you unlimited permission to link the
 * compiled version of this file with other programs, and to distribute
 * those programs without any restriction coming from the use of this
 * file.  (The General Public License restrictions do apply in other
 * respects; for example, they cover modification of the file, and
 * distribution when not linked into another program.)
 * 
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 * 
 *    As a special exception, if you link this library with files
 *    compiled with GCC to produce an executable, this does not cause
 *    the resulting executable to be covered by the GNU General Public License.
 *    This exception does not however invalidate any other reasons why
 *    the executable file might be covered by the GNU General Public License.
 */

#define MXCSR_DAZ (1 << 6)	/* Enable denormals are zero mode */
#define MXCSR_FTZ (1 << 15)	/* Enable flush to zero mode */

#define FXSAVE	(1 << 24)
#define SSE	(1 << 25)

static void __attribute__((constructor))
#ifndef __x86_64__
/* The i386 ABI only requires 4-byte stack alignment, so this is necessary
   to make sure the fxsave struct gets correct alignment.
   See PR27537 and PR28621.  */
__attribute__ ((force_align_arg_pointer))
#endif
set_fast_math (void)
{
#ifndef __x86_64__
  /* All 64-bit targets have SSE and DAZ; only check them explicitly
     for 32-bit ones. */
  unsigned int eax, ebx, ecx, edx;

  /* See if we can use cpuid.  */
  asm volatile ("pushfl; pushfl; popl %0; movl %0,%1; xorl %2,%0;"
		"pushl %0; popfl; pushfl; popl %0; popfl"
		: "=&r" (eax), "=&r" (ebx)
		: "i" (0x00200000));

  if (((eax ^ ebx) & 0x00200000) == 0)
    return;

  /* Check the highest input value for eax.  */
  asm volatile ("xchgl %%ebx, %1; cpuid; xchgl %%ebx, %1"
		: "=a" (eax), "=r" (ebx), "=c" (ecx), "=d" (edx)
		: "0" (0));

  if (eax == 0)
    return;

  asm volatile ("xchgl %%ebx, %1; cpuid; xchgl %%ebx, %1"
		: "=a" (eax), "=r" (ebx), "=c" (ecx), "=d" (edx)
		: "0" (1));

  if (edx & SSE)
    {
      unsigned int mxcsr = __builtin_ia32_stmxcsr ();
  
      mxcsr |= MXCSR_FTZ;

      if (edx & FXSAVE)
	{
	  /* Check if DAZ is available.  */
	  struct
	    {
	      unsigned short int cwd;
	      unsigned short int swd;
	      unsigned short int twd;
	      unsigned short int fop;
	      long int fip;
	      long int fcs;
	      long int foo;
	      long int fos;
	      long int mxcsr;
	      long int mxcsr_mask;
	      long int st_space[32];
	      long int xmm_space[32];
	      long int padding[56];
	    } __attribute__ ((aligned (16))) fxsave;

	  __builtin_memset (&fxsave, 0, sizeof (fxsave));

	  asm volatile ("fxsave %0" : "=m" (fxsave) : "m" (fxsave));

	  if (fxsave.mxcsr_mask & MXCSR_DAZ)
	    mxcsr |= MXCSR_DAZ;
	}

      __builtin_ia32_ldmxcsr (mxcsr);
    }
#else
  unsigned int mxcsr = __builtin_ia32_stmxcsr ();
  mxcsr |= MXCSR_DAZ | MXCSR_FTZ;
  __builtin_ia32_ldmxcsr (mxcsr);
#endif
}
