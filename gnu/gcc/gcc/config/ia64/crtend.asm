/* Copyright (C) 2000, 2001, 2003, 2005 Free Software Foundation, Inc.
   Contributed by Jes Sorensen, <Jes.Sorensen@cern.ch>

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

#include "auto-host.h"

.section .ctors,"aw","progbits"
	.align	8
__CTOR_END__:
	data8	0

.section .dtors,"aw","progbits"
	.align 8
__DTOR_END__:
	data8	0

.section .jcr,"aw","progbits"
	.align 8
__JCR_END__:
	data8	0

#ifdef HAVE_INITFINI_ARRAY
	.global __do_global_ctors_aux
	.hidden	__do_global_ctors_aux
#else /* !HAVE_INITFINI_ARRAY */
/*
 * Fragment of the ELF _init routine that invokes our dtor cleanup.
 *
 * We make the call by indirection, because in large programs the 
 * .fini and .init sections are not in range of the destination, and
 * we cannot allow the linker to insert a stub at the end of this
 * fragment of the _fini function.  Further, Itanium does not implement
 * the long branch instructions, and we do not wish every program to
 * trap to the kernel for emulation.
 *
 * Note that we require __do_global_ctors_aux to preserve the GP,
 * so that the next fragment in .fini gets the right value.
 */
.section .init,"ax","progbits"
	{ .mlx
	  movl r2 = @pcrel(__do_global_ctors_aux - 16)
	}
	{ .mii
	  mov r3 = ip
	  ;;
	  add r2 = r2, r3
	  ;;
	}
	{ .mib
	  mov b6 = r2
	  br.call.sptk.many b0 = b6
	  ;;
	}
#endif /* !HAVE_INITFINI_ARRAY */

.text
	.align 32
	.proc __do_global_ctors_aux
__do_global_ctors_aux:
	.prologue
	/*
		for (loc0 = __CTOR_END__-1; *p != -1; --p)
		  (*p) ();
	*/
	.save ar.pfs, r34
	alloc loc2 = ar.pfs, 0, 5, 0, 0
	movl loc0 = @gprel(__CTOR_END__ - 8)
	;;

	add loc0 = loc0, gp
	;;
	ld8 loc3 = [loc0], -8
	.save rp, loc1
	mov loc1 = rp
	.body
	;;

	cmp.eq p6, p0 = -1, loc3
	mov loc4 = gp
(p6)	br.cond.spnt.few .exit

.loop:	ld8 r15 = [loc3], 8
	;;
	ld8 gp = [loc3]
	mov b6 = r15

	ld8 loc3 = [loc0], -8
	nop 0
	br.call.sptk.many rp = b6
	;;

	cmp.ne p6, p0 = -1, loc3
	nop 0
(p6)	br.cond.sptk.few .loop

.exit:	mov gp = loc3
	mov rp = loc1
	mov ar.pfs = loc2

	br.ret.sptk.many rp
	.endp __do_global_ctors_aux
