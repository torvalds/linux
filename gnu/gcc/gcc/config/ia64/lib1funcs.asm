/* Copyright (C) 2000, 2001, 2003, 2005 Free Software Foundation, Inc.
   Contributed by James E. Wilson <wilson@cygnus.com>.

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

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

#ifdef L__divxf3
// Compute a 80-bit IEEE double-extended quotient.
//
// From the Intel IA-64 Optimization Guide, choose the minimum latency
// alternative.
//
// farg0 holds the dividend.  farg1 holds the divisor.
//
// __divtf3 is an alternate symbol name for backward compatibility.

	.text
	.align 16
	.global __divxf3
	.global __divtf3
	.proc __divxf3
__divxf3:
__divtf3:
	cmp.eq p7, p0 = r0, r0
	frcpa.s0 f10, p6 = farg0, farg1
	;;
(p6)	cmp.ne p7, p0 = r0, r0
	.pred.rel.mutex p6, p7
(p6)	fnma.s1 f11 = farg1, f10, f1
(p6)	fma.s1 f12 = farg0, f10, f0
	;;
(p6)	fma.s1 f13 = f11, f11, f0
(p6)	fma.s1 f14 = f11, f11, f11
	;;
(p6)	fma.s1 f11 = f13, f13, f11
(p6)	fma.s1 f13 = f14, f10, f10
	;;
(p6)	fma.s1 f10 = f13, f11, f10
(p6)	fnma.s1 f11 = farg1, f12, farg0
	;;
(p6)	fma.s1 f11 = f11, f10, f12
(p6)	fnma.s1 f12 = farg1, f10, f1
	;;
(p6)	fma.s1 f10 = f12, f10, f10
(p6)	fnma.s1 f12 = farg1, f11, farg0
	;;
(p6)	fma.s0 fret0 = f12, f10, f11
(p7)	mov fret0 = f10
	br.ret.sptk rp
	.endp __divxf3
#endif

#ifdef L__divdf3
// Compute a 64-bit IEEE double quotient.
//
// From the Intel IA-64 Optimization Guide, choose the minimum latency
// alternative.
//
// farg0 holds the dividend.  farg1 holds the divisor.

	.text
	.align 16
	.global __divdf3
	.proc __divdf3
__divdf3:
	cmp.eq p7, p0 = r0, r0
	frcpa.s0 f10, p6 = farg0, farg1
	;;
(p6)	cmp.ne p7, p0 = r0, r0
	.pred.rel.mutex p6, p7
(p6)	fmpy.s1 f11 = farg0, f10
(p6)	fnma.s1 f12 = farg1, f10, f1
	;;
(p6)	fma.s1 f11 = f12, f11, f11
(p6)	fmpy.s1 f13 = f12, f12
	;;
(p6)	fma.s1 f10 = f12, f10, f10
(p6)	fma.s1 f11 = f13, f11, f11
	;;
(p6)	fmpy.s1 f12 = f13, f13
(p6)	fma.s1 f10 = f13, f10, f10
	;;
(p6)	fma.d.s1 f11 = f12, f11, f11
(p6)	fma.s1 f10 = f12, f10, f10
	;;
(p6)	fnma.d.s1 f8 = farg1, f11, farg0
	;;
(p6)	fma.d fret0 = f8, f10, f11
(p7)	mov fret0 = f10
	br.ret.sptk rp
	;;
	.endp __divdf3
#endif

#ifdef L__divsf3
// Compute a 32-bit IEEE float quotient.
//
// From the Intel IA-64 Optimization Guide, choose the minimum latency
// alternative.
//
// farg0 holds the dividend.  farg1 holds the divisor.

	.text
	.align 16
	.global __divsf3
	.proc __divsf3
__divsf3:
	cmp.eq p7, p0 = r0, r0
	frcpa.s0 f10, p6 = farg0, farg1
	;;
(p6)	cmp.ne p7, p0 = r0, r0
	.pred.rel.mutex p6, p7
(p6)	fmpy.s1 f8 = farg0, f10
(p6)	fnma.s1 f9 = farg1, f10, f1
	;;
(p6)	fma.s1 f8 = f9, f8, f8
(p6)	fmpy.s1 f9 = f9, f9
	;;
(p6)	fma.s1 f8 = f9, f8, f8
(p6)	fmpy.s1 f9 = f9, f9
	;;
(p6)	fma.d.s1 f10 = f9, f8, f8
	;;
(p6)	fnorm.s.s0 fret0 = f10
(p7)	mov fret0 = f10
	br.ret.sptk rp
	;;
	.endp __divsf3
#endif

#ifdef L__divdi3
// Compute a 64-bit integer quotient.
//
// From the Intel IA-64 Optimization Guide, choose the minimum latency
// alternative.
//
// in0 holds the dividend.  in1 holds the divisor.

	.text
	.align 16
	.global __divdi3
	.proc __divdi3
__divdi3:
	.regstk 2,0,0,0
	// Transfer inputs to FP registers.
	setf.sig f8 = in0
	setf.sig f9 = in1
	// Check divide by zero.
	cmp.ne.unc p0,p7=0,in1
	;;
	// Convert the inputs to FP, so that they won't be treated as unsigned.
	fcvt.xf f8 = f8
	fcvt.xf f9 = f9
(p7)	break 1
	;;
	// Compute the reciprocal approximation.
	frcpa.s1 f10, p6 = f8, f9
	;;
	// 3 Newton-Raphson iterations.
(p6)	fnma.s1 f11 = f9, f10, f1
(p6)	fmpy.s1 f12 = f8, f10
	;;
(p6)	fmpy.s1 f13 = f11, f11
(p6)	fma.s1 f12 = f11, f12, f12
	;;
(p6)	fma.s1 f10 = f11, f10, f10
(p6)	fma.s1 f11 = f13, f12, f12
	;;
(p6)	fma.s1 f10 = f13, f10, f10
(p6)	fnma.s1 f12 = f9, f11, f8
	;;
(p6)	fma.s1 f10 = f12, f10, f11
	;;
	// Round quotient to an integer.
	fcvt.fx.trunc.s1 f10 = f10
	;;
	// Transfer result to GP registers.
	getf.sig ret0 = f10
	br.ret.sptk rp
	;;
	.endp __divdi3
#endif

#ifdef L__moddi3
// Compute a 64-bit integer modulus.
//
// From the Intel IA-64 Optimization Guide, choose the minimum latency
// alternative.
//
// in0 holds the dividend (a).  in1 holds the divisor (b).

	.text
	.align 16
	.global __moddi3
	.proc __moddi3
__moddi3:
	.regstk 2,0,0,0
	// Transfer inputs to FP registers.
	setf.sig f14 = in0
	setf.sig f9 = in1
	// Check divide by zero.
	cmp.ne.unc p0,p7=0,in1
	;;
	// Convert the inputs to FP, so that they won't be treated as unsigned.
	fcvt.xf f8 = f14
	fcvt.xf f9 = f9
(p7)	break 1
	;;
	// Compute the reciprocal approximation.
	frcpa.s1 f10, p6 = f8, f9
	;;
	// 3 Newton-Raphson iterations.
(p6)	fmpy.s1 f12 = f8, f10
(p6)	fnma.s1 f11 = f9, f10, f1
	;;
(p6)	fma.s1 f12 = f11, f12, f12
(p6)	fmpy.s1 f13 = f11, f11
	;;
(p6)	fma.s1 f10 = f11, f10, f10
(p6)	fma.s1 f11 = f13, f12, f12
	;;
	sub in1 = r0, in1
(p6)	fma.s1 f10 = f13, f10, f10
(p6)	fnma.s1 f12 = f9, f11, f8
	;;
	setf.sig f9 = in1
(p6)	fma.s1 f10 = f12, f10, f11
	;;
	fcvt.fx.trunc.s1 f10 = f10
	;;
	// r = q * (-b) + a
	xma.l f10 = f10, f9, f14
	;;
	// Transfer result to GP registers.
	getf.sig ret0 = f10
	br.ret.sptk rp
	;;
	.endp __moddi3
#endif

#ifdef L__udivdi3
// Compute a 64-bit unsigned integer quotient.
//
// From the Intel IA-64 Optimization Guide, choose the minimum latency
// alternative.
//
// in0 holds the dividend.  in1 holds the divisor.

	.text
	.align 16
	.global __udivdi3
	.proc __udivdi3
__udivdi3:
	.regstk 2,0,0,0
	// Transfer inputs to FP registers.
	setf.sig f8 = in0
	setf.sig f9 = in1
	// Check divide by zero.
	cmp.ne.unc p0,p7=0,in1
	;;
	// Convert the inputs to FP, to avoid FP software-assist faults.
	fcvt.xuf.s1 f8 = f8
	fcvt.xuf.s1 f9 = f9
(p7)	break 1
	;;
	// Compute the reciprocal approximation.
	frcpa.s1 f10, p6 = f8, f9
	;;
	// 3 Newton-Raphson iterations.
(p6)	fnma.s1 f11 = f9, f10, f1
(p6)	fmpy.s1 f12 = f8, f10
	;;
(p6)	fmpy.s1 f13 = f11, f11
(p6)	fma.s1 f12 = f11, f12, f12
	;;
(p6)	fma.s1 f10 = f11, f10, f10
(p6)	fma.s1 f11 = f13, f12, f12
	;;
(p6)	fma.s1 f10 = f13, f10, f10
(p6)	fnma.s1 f12 = f9, f11, f8
	;;
(p6)	fma.s1 f10 = f12, f10, f11
	;;
	// Round quotient to an unsigned integer.
	fcvt.fxu.trunc.s1 f10 = f10
	;;
	// Transfer result to GP registers.
	getf.sig ret0 = f10
	br.ret.sptk rp
	;;
	.endp __udivdi3
#endif

#ifdef L__umoddi3
// Compute a 64-bit unsigned integer modulus.
//
// From the Intel IA-64 Optimization Guide, choose the minimum latency
// alternative.
//
// in0 holds the dividend (a).  in1 holds the divisor (b).

	.text
	.align 16
	.global __umoddi3
	.proc __umoddi3
__umoddi3:
	.regstk 2,0,0,0
	// Transfer inputs to FP registers.
	setf.sig f14 = in0
	setf.sig f9 = in1
	// Check divide by zero.
	cmp.ne.unc p0,p7=0,in1
	;;
	// Convert the inputs to FP, to avoid FP software assist faults.
	fcvt.xuf.s1 f8 = f14
	fcvt.xuf.s1 f9 = f9
(p7)	break 1;
	;;
	// Compute the reciprocal approximation.
	frcpa.s1 f10, p6 = f8, f9
	;;
	// 3 Newton-Raphson iterations.
(p6)	fmpy.s1 f12 = f8, f10
(p6)	fnma.s1 f11 = f9, f10, f1
	;;
(p6)	fma.s1 f12 = f11, f12, f12
(p6)	fmpy.s1 f13 = f11, f11
	;;
(p6)	fma.s1 f10 = f11, f10, f10
(p6)	fma.s1 f11 = f13, f12, f12
	;;
	sub in1 = r0, in1
(p6)	fma.s1 f10 = f13, f10, f10
(p6)	fnma.s1 f12 = f9, f11, f8
	;;
	setf.sig f9 = in1
(p6)	fma.s1 f10 = f12, f10, f11
	;;
	// Round quotient to an unsigned integer.
	fcvt.fxu.trunc.s1 f10 = f10
	;;
	// r = q * (-b) + a
	xma.l f10 = f10, f9, f14
	;;
	// Transfer result to GP registers.
	getf.sig ret0 = f10
	br.ret.sptk rp
	;;
	.endp __umoddi3
#endif

#ifdef L__divsi3
// Compute a 32-bit integer quotient.
//
// From the Intel IA-64 Optimization Guide, choose the minimum latency
// alternative.
//
// in0 holds the dividend.  in1 holds the divisor.

	.text
	.align 16
	.global __divsi3
	.proc __divsi3
__divsi3:
	.regstk 2,0,0,0
	// Check divide by zero.
	cmp.ne.unc p0,p7=0,in1
	sxt4 in0 = in0
	sxt4 in1 = in1
	;;
	setf.sig f8 = in0
	setf.sig f9 = in1
(p7)	break 1
	;;
	mov r2 = 0x0ffdd
	fcvt.xf f8 = f8
	fcvt.xf f9 = f9
	;;
	setf.exp f11 = r2
	frcpa.s1 f10, p6 = f8, f9
	;;
(p6)	fmpy.s1 f8 = f8, f10
(p6)	fnma.s1 f9 = f9, f10, f1
	;;
(p6)	fma.s1 f8 = f9, f8, f8
(p6)	fma.s1 f9 = f9, f9, f11
	;;
(p6)	fma.s1 f10 = f9, f8, f8
	;;
	fcvt.fx.trunc.s1 f10 = f10
	;;
	getf.sig ret0 = f10
	br.ret.sptk rp
	;;
	.endp __divsi3
#endif

#ifdef L__modsi3
// Compute a 32-bit integer modulus.
//
// From the Intel IA-64 Optimization Guide, choose the minimum latency
// alternative.
//
// in0 holds the dividend.  in1 holds the divisor.

	.text
	.align 16
	.global __modsi3
	.proc __modsi3
__modsi3:
	.regstk 2,0,0,0
	mov r2 = 0x0ffdd
	sxt4 in0 = in0
	sxt4 in1 = in1
	;;
	setf.sig f13 = r32
	setf.sig f9 = r33
	// Check divide by zero.
	cmp.ne.unc p0,p7=0,in1
	;;
	sub in1 = r0, in1
	fcvt.xf f8 = f13
	fcvt.xf f9 = f9
	;;
	setf.exp f11 = r2
	frcpa.s1 f10, p6 = f8, f9
(p7)	break 1
	;;
(p6)	fmpy.s1 f12 = f8, f10
(p6)	fnma.s1 f10 = f9, f10, f1
	;;
	setf.sig f9 = in1
(p6)	fma.s1 f12 = f10, f12, f12
(p6)	fma.s1 f10 = f10, f10, f11	
	;;
(p6)	fma.s1 f10 = f10, f12, f12
	;;
	fcvt.fx.trunc.s1 f10 = f10
	;;
	xma.l f10 = f10, f9, f13
	;;
	getf.sig ret0 = f10
	br.ret.sptk rp
	;;
	.endp __modsi3
#endif

#ifdef L__udivsi3
// Compute a 32-bit unsigned integer quotient.
//
// From the Intel IA-64 Optimization Guide, choose the minimum latency
// alternative.
//
// in0 holds the dividend.  in1 holds the divisor.

	.text
	.align 16
	.global __udivsi3
	.proc __udivsi3
__udivsi3:
	.regstk 2,0,0,0
	mov r2 = 0x0ffdd
	zxt4 in0 = in0
	zxt4 in1 = in1
	;;
	setf.sig f8 = in0
	setf.sig f9 = in1
	// Check divide by zero.
	cmp.ne.unc p0,p7=0,in1
	;;
	fcvt.xf f8 = f8
	fcvt.xf f9 = f9
(p7)	break 1
	;;
	setf.exp f11 = r2
	frcpa.s1 f10, p6 = f8, f9
	;;
(p6)	fmpy.s1 f8 = f8, f10
(p6)	fnma.s1 f9 = f9, f10, f1
	;;
(p6)	fma.s1 f8 = f9, f8, f8
(p6)	fma.s1 f9 = f9, f9, f11
	;;
(p6)	fma.s1 f10 = f9, f8, f8
	;;
	fcvt.fxu.trunc.s1 f10 = f10
	;;
	getf.sig ret0 = f10
	br.ret.sptk rp
	;;
	.endp __udivsi3
#endif

#ifdef L__umodsi3
// Compute a 32-bit unsigned integer modulus.
//
// From the Intel IA-64 Optimization Guide, choose the minimum latency
// alternative.
//
// in0 holds the dividend.  in1 holds the divisor.

	.text
	.align 16
	.global __umodsi3
	.proc __umodsi3
__umodsi3:
	.regstk 2,0,0,0
	mov r2 = 0x0ffdd
	zxt4 in0 = in0
	zxt4 in1 = in1
	;;
	setf.sig f13 = in0
	setf.sig f9 = in1
	// Check divide by zero.
	cmp.ne.unc p0,p7=0,in1
	;;
	sub in1 = r0, in1
	fcvt.xf f8 = f13
	fcvt.xf f9 = f9
	;;
	setf.exp f11 = r2
	frcpa.s1 f10, p6 = f8, f9
(p7)	break 1;
	;;
(p6)	fmpy.s1 f12 = f8, f10
(p6)	fnma.s1 f10 = f9, f10, f1
	;;
	setf.sig f9 = in1
(p6)	fma.s1 f12 = f10, f12, f12
(p6)	fma.s1 f10 = f10, f10, f11
	;;
(p6)	fma.s1 f10 = f10, f12, f12
	;;
	fcvt.fxu.trunc.s1 f10 = f10
	;;
	xma.l f10 = f10, f9, f13
	;;
	getf.sig ret0 = f10
	br.ret.sptk rp
	;;
	.endp __umodsi3
#endif

#ifdef L__save_stack_nonlocal
// Notes on save/restore stack nonlocal: We read ar.bsp but write
// ar.bspstore.  This is because ar.bsp can be read at all times
// (independent of the RSE mode) but since it's read-only we need to
// restore the value via ar.bspstore.  This is OK because
// ar.bsp==ar.bspstore after executing "flushrs".

// void __ia64_save_stack_nonlocal(void *save_area, void *stack_pointer)

	.text
	.align 16
	.global __ia64_save_stack_nonlocal
	.proc __ia64_save_stack_nonlocal
__ia64_save_stack_nonlocal:
	{ .mmf
	  alloc r18 = ar.pfs, 2, 0, 0, 0
	  mov r19 = ar.rsc
	  ;;
	}
	{ .mmi
	  flushrs
	  st8 [in0] = in1, 24
	  and r19 = 0x1c, r19
	  ;;
	}
	{ .mmi
	  st8 [in0] = r18, -16
	  mov ar.rsc = r19
	  or r19 = 0x3, r19
	  ;;
	}
	{ .mmi
	  mov r16 = ar.bsp
	  mov r17 = ar.rnat
	  adds r2 = 8, in0
	  ;;
	}
	{ .mmi
	  st8 [in0] = r16
	  st8 [r2] = r17
	}
	{ .mib
	  mov ar.rsc = r19
	  br.ret.sptk.few rp
	  ;;
	}
	.endp __ia64_save_stack_nonlocal
#endif

#ifdef L__nonlocal_goto
// void __ia64_nonlocal_goto(void *target_label, void *save_area,
//			     void *static_chain);

	.text
	.align 16
	.global __ia64_nonlocal_goto
	.proc __ia64_nonlocal_goto
__ia64_nonlocal_goto:
	{ .mmi
	  alloc r20 = ar.pfs, 3, 0, 0, 0
	  ld8 r12 = [in1], 8
	  mov.ret.sptk rp = in0, .L0
	  ;;
	}
	{ .mmf
	  ld8 r16 = [in1], 8
	  mov r19 = ar.rsc
	  ;;
	}
	{ .mmi
	  flushrs
	  ld8 r17 = [in1], 8
	  and r19 = 0x1c, r19
	  ;;
	}
	{ .mmi
	  ld8 r18 = [in1]
	  mov ar.rsc = r19
	  or r19 = 0x3, r19
	  ;;
	}
	{ .mmi
	  mov ar.bspstore = r16
	  ;;
	  mov ar.rnat = r17
	  ;;
	}
	{ .mmi
	  loadrs
	  invala
	  mov r15 = in2
	  ;;
	}
.L0:	{ .mib
	  mov ar.rsc = r19
	  mov ar.pfs = r18
	  br.ret.sptk.few rp
	  ;;
	}
	.endp __ia64_nonlocal_goto
#endif

#ifdef L__restore_stack_nonlocal
// This is mostly the same as nonlocal_goto above.
// ??? This has not been tested yet.

// void __ia64_restore_stack_nonlocal(void *save_area)

	.text
	.align 16
	.global __ia64_restore_stack_nonlocal
	.proc __ia64_restore_stack_nonlocal
__ia64_restore_stack_nonlocal:
	{ .mmf
	  alloc r20 = ar.pfs, 4, 0, 0, 0
	  ld8 r12 = [in0], 8
	  ;;
	}
	{ .mmb
	  ld8 r16=[in0], 8
	  mov r19 = ar.rsc
	  ;;
	}
	{ .mmi
	  flushrs
	  ld8 r17 = [in0], 8
	  and r19 = 0x1c, r19
	  ;;
	}
	{ .mmf
	  ld8 r18 = [in0]
	  mov ar.rsc = r19
	  ;;
	}
	{ .mmi
	  mov ar.bspstore = r16
	  ;;
	  mov ar.rnat = r17
	  or r19 = 0x3, r19
	  ;;
	}
	{ .mmf
	  loadrs
	  invala
	  ;;
	}
.L0:	{ .mib
	  mov ar.rsc = r19
	  mov ar.pfs = r18
	  br.ret.sptk.few rp
	  ;;
	}
	.endp __ia64_restore_stack_nonlocal
#endif

#ifdef L__trampoline
// Implement the nested function trampoline.  This is out of line
// so that we don't have to bother with flushing the icache, as
// well as making the on-stack trampoline smaller.
//
// The trampoline has the following form:
//
//		+-------------------+ >
//	TRAMP:	| __ia64_trampoline | |
//		+-------------------+  > fake function descriptor
//		| TRAMP+16          | |
//		+-------------------+ >
//		| target descriptor |
//		+-------------------+
//		| static link	    |
//		+-------------------+

	.text
	.align 16
	.global __ia64_trampoline
	.proc __ia64_trampoline
__ia64_trampoline:
	{ .mmi
	  ld8 r2 = [r1], 8
	  ;;
	  ld8 r15 = [r1]
	}
	{ .mmi
	  ld8 r3 = [r2], 8
	  ;;
	  ld8 r1 = [r2]
	  mov b6 = r3
	}
	{ .bbb
	  br.sptk.many b6
	  ;;
	}
	.endp __ia64_trampoline
#endif

// Thunks for backward compatibility.
#ifdef L_fixtfdi
	.text
	.align 16
	.global __fixtfti
	.proc __fixtfti
__fixtfti:
	{ .bbb
	  br.sptk.many __fixxfti
	  ;;
	}
	.endp __fixtfti
#endif
#ifdef L_fixunstfdi
	.align 16
	.global __fixunstfti
	.proc __fixunstfti
__fixunstfti:
	{ .bbb
	  br.sptk.many __fixunsxfti
	  ;;
	}
	.endp __fixunstfti
#endif
#if L_floatditf
	.align 16
	.global __floattitf
	.proc __floattitf
__floattitf:
	{ .bbb
	  br.sptk.many __floattixf
	  ;;
	}
	.endp __floattitf
#endif
