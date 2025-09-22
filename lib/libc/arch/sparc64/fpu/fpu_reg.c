/*	$OpenBSD: fpu_reg.c,v 1.1 2003/07/21 18:41:30 jason Exp $	*/

/*
 * Copyright (c) 2003 Jason L. Wright (jason@thought.net)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/types.h>
#include "fpu_reg.h"

void
__fpu_setreg32(int r, u_int32_t v)
{
	switch (r) {
	case 0:
		asm("ld [%0], %%f0" : : "r" (&v));
		break;
	case 1:
		asm("ld [%0], %%f1" : : "r" (&v));
		break;
	case 2:
		asm("ld [%0], %%f2" : : "r" (&v));
		break;
	case 3:
		asm("ld [%0], %%f3" : : "r" (&v));
		break;
	case 4:
		asm("ld [%0], %%f4" : : "r" (&v));
		break;
	case 5:
		asm("ld [%0], %%f5" : : "r" (&v));
		break;
	case 6:
		asm("ld [%0], %%f6" : : "r" (&v));
		break;
	case 7:
		asm("ld [%0], %%f7" : : "r" (&v));
		break;
	case 8:
		asm("ld [%0], %%f8" : : "r" (&v));
		break;
	case 9:
		asm("ld [%0], %%f9" : : "r" (&v));
		break;
	case 10:
		asm("ld [%0], %%f10" : : "r" (&v));
		break;
	case 11:
		asm("ld [%0], %%f11" : : "r" (&v));
		break;
	case 12:
		asm("ld [%0], %%f12" : : "r" (&v));
		break;
	case 13:
		asm("ld [%0], %%f13" : : "r" (&v));
		break;
	case 14:
		asm("ld [%0], %%f14" : : "r" (&v));
		break;
	case 15:
		asm("ld [%0], %%f15" : : "r" (&v));
		break;
	case 16:
		asm("ld [%0], %%f16" : : "r" (&v));
		break;
	case 17:
		asm("ld [%0], %%f17" : : "r" (&v));
		break;
	case 18:
		asm("ld [%0], %%f18" : : "r" (&v));
		break;
	case 19:
		asm("ld [%0], %%f19" : : "r" (&v));
		break;
	case 20:
		asm("ld [%0], %%f20" : : "r" (&v));
		break;
	case 21:
		asm("ld [%0], %%f21" : : "r" (&v));
		break;
	case 22:
		asm("ld [%0], %%f22" : : "r" (&v));
		break;
	case 23:
		asm("ld [%0], %%f23" : : "r" (&v));
		break;
	case 24:
		asm("ld [%0], %%f24" : : "r" (&v));
		break;
	case 25:
		asm("ld [%0], %%f25" : : "r" (&v));
		break;
	case 26:
		asm("ld [%0], %%f26" : : "r" (&v));
		break;
	case 27:
		asm("ld [%0], %%f27" : : "r" (&v));
		break;
	case 28:
		asm("ld [%0], %%f28" : : "r" (&v));
		break;
	case 29:
		asm("ld [%0], %%f29" : : "r" (&v));
		break;
	case 30:
		asm("ld [%0], %%f30" : : "r" (&v));
		break;
	case 31:
		asm("ld [%0], %%f31" : : "r" (&v));
		break;
	case 32:
		asm("ld [%0], %%f32" : : "r" (&v));
		break;
	case 33:
		asm("ld [%0], %%f33" : : "r" (&v));
		break;
	case 34:
		asm("ld [%0], %%f34" : : "r" (&v));
		break;
	case 35:
		asm("ld [%0], %%f35" : : "r" (&v));
		break;
	case 36:
		asm("ld [%0], %%f36" : : "r" (&v));
		break;
	case 37:
		asm("ld [%0], %%f37" : : "r" (&v));
		break;
	case 38:
		asm("ld [%0], %%f38" : : "r" (&v));
		break;
	case 39:
		asm("ld [%0], %%f39" : : "r" (&v));
		break;
	case 40:
		asm("ld [%0], %%f40" : : "r" (&v));
		break;
	case 41:
		asm("ld [%0], %%f41" : : "r" (&v));
		break;
	case 42:
		asm("ld [%0], %%f42" : : "r" (&v));
		break;
	case 43:
		asm("ld [%0], %%f43" : : "r" (&v));
		break;
	case 44:
		asm("ld [%0], %%f44" : : "r" (&v));
		break;
	case 45:
		asm("ld [%0], %%f45" : : "r" (&v));
		break;
	case 46:
		asm("ld [%0], %%f46" : : "r" (&v));
		break;
	case 47:
		asm("ld [%0], %%f47" : : "r" (&v));
		break;
	case 48:
		asm("ld [%0], %%f48" : : "r" (&v));
		break;
	case 49:
		asm("ld [%0], %%f49" : : "r" (&v));
		break;
	case 50:
		asm("ld [%0], %%f50" : : "r" (&v));
		break;
	case 51:
		asm("ld [%0], %%f51" : : "r" (&v));
		break;
	case 52:
		asm("ld [%0], %%f52" : : "r" (&v));
		break;
	case 53:
		asm("ld [%0], %%f53" : : "r" (&v));
		break;
	case 54:
		asm("ld [%0], %%f54" : : "r" (&v));
		break;
	case 55:
		asm("ld [%0], %%f55" : : "r" (&v));
		break;
	case 56:
		asm("ld [%0], %%f56" : : "r" (&v));
		break;
	case 57:
		asm("ld [%0], %%f57" : : "r" (&v));
		break;
	case 58:
		asm("ld [%0], %%f58" : : "r" (&v));
		break;
	case 59:
		asm("ld [%0], %%f59" : : "r" (&v));
		break;
	case 60:
		asm("ld [%0], %%f60" : : "r" (&v));
		break;
	case 61:
		asm("ld [%0], %%f61" : : "r" (&v));
		break;
	case 62:
		asm("ld [%0], %%f62" : : "r" (&v));
		break;
	case 63:
		asm("ld [%0], %%f63" : : "r" (&v));
		break;
	}
}

void
__fpu_setreg64(int r, u_int64_t v)
{
	switch (r) {
	case 0:
		asm("ldd [%0], %%f0" : : "r" (&v));
		break;
	case 2:
		asm("ldd [%0], %%f2" : : "r" (&v));
		break;
	case 4:
		asm("ldd [%0], %%f4" : : "r" (&v));
		break;
	case 6:
		asm("ldd [%0], %%f6" : : "r" (&v));
		break;
	case 8:
		asm("ldd [%0], %%f8" : : "r" (&v));
		break;
	case 10:
		asm("ldd [%0], %%f10" : : "r" (&v));
		break;
	case 12:
		asm("ldd [%0], %%f12" : : "r" (&v));
		break;
	case 14:
		asm("ldd [%0], %%f14" : : "r" (&v));
		break;
	case 16:
		asm("ldd [%0], %%f16" : : "r" (&v));
		break;
	case 18:
		asm("ldd [%0], %%f18" : : "r" (&v));
		break;
	case 20:
		asm("ldd [%0], %%f20" : : "r" (&v));
		break;
	case 22:
		asm("ldd [%0], %%f22" : : "r" (&v));
		break;
	case 24:
		asm("ldd [%0], %%f24" : : "r" (&v));
		break;
	case 26:
		asm("ldd [%0], %%f26" : : "r" (&v));
		break;
	case 28:
		asm("ldd [%0], %%f28" : : "r" (&v));
		break;
	case 30:
		asm("ldd [%0], %%f30" : : "r" (&v));
		break;
	case 32:
		asm("ldd [%0], %%f32" : : "r" (&v));
		break;
	case 34:
		asm("ldd [%0], %%f34" : : "r" (&v));
		break;
	case 36:
		asm("ldd [%0], %%f36" : : "r" (&v));
		break;
	case 38:
		asm("ldd [%0], %%f38" : : "r" (&v));
		break;
	case 40:
		asm("ldd [%0], %%f40" : : "r" (&v));
		break;
	case 42:
		asm("ldd [%0], %%f42" : : "r" (&v));
		break;
	case 44:
		asm("ldd [%0], %%f44" : : "r" (&v));
		break;
	case 46:
		asm("ldd [%0], %%f46" : : "r" (&v));
		break;
	case 48:
		asm("ldd [%0], %%f48" : : "r" (&v));
		break;
	case 50:
		asm("ldd [%0], %%f50" : : "r" (&v));
		break;
	case 52:
		asm("ldd [%0], %%f52" : : "r" (&v));
		break;
	case 54:
		asm("ldd [%0], %%f54" : : "r" (&v));
		break;
	case 56:
		asm("ldd [%0], %%f56" : : "r" (&v));
		break;
	case 58:
		asm("ldd [%0], %%f58" : : "r" (&v));
		break;
	case 60:
		asm("ldd [%0], %%f60" : : "r" (&v));
		break;
	case 62:
		asm("ldd [%0], %%f62" : : "r" (&v));
		break;
	}
}

u_int32_t
__fpu_getreg32(int r)
{
	u_int32_t v;

	switch (r) {
	case 0:
		asm("st %%f0, [%0]" : : "r" (&v));
		break;
	case 1:
		asm("st %%f1, [%0]" : : "r" (&v));
		break;
	case 2:
		asm("st %%f2, [%0]" : : "r" (&v));
		break;
	case 3:
		asm("st %%f3, [%0]" : : "r" (&v));
		break;
	case 4:
		asm("st %%f4, [%0]" : : "r" (&v));
		break;
	case 5:
		asm("st %%f5, [%0]" : : "r" (&v));
		break;
	case 6:
		asm("st %%f6, [%0]" : : "r" (&v));
		break;
	case 7:
		asm("st %%f7, [%0]" : : "r" (&v));
		break;
	case 8:
		asm("st %%f8, [%0]" : : "r" (&v));
		break;
	case 9:
		asm("st %%f9, [%0]" : : "r" (&v));
		break;
	case 10:
		asm("st %%f10, [%0]" : : "r" (&v));
		break;
	case 11:
		asm("st %%f11, [%0]" : : "r" (&v));
		break;
	case 12:
		asm("st %%f12, [%0]" : : "r" (&v));
		break;
	case 13:
		asm("st %%f13, [%0]" : : "r" (&v));
		break;
	case 14:
		asm("st %%f14, [%0]" : : "r" (&v));
		break;
	case 15:
		asm("st %%f15, [%0]" : : "r" (&v));
		break;
	case 16:
		asm("st %%f16, [%0]" : : "r" (&v));
		break;
	case 17:
		asm("st %%f17, [%0]" : : "r" (&v));
		break;
	case 18:
		asm("st %%f18, [%0]" : : "r" (&v));
		break;
	case 19:
		asm("st %%f19, [%0]" : : "r" (&v));
		break;
	case 20:
		asm("st %%f20, [%0]" : : "r" (&v));
		break;
	case 21:
		asm("st %%f21, [%0]" : : "r" (&v));
		break;
	case 22:
		asm("st %%f22, [%0]" : : "r" (&v));
		break;
	case 23:
		asm("st %%f23, [%0]" : : "r" (&v));
		break;
	case 24:
		asm("st %%f24, [%0]" : : "r" (&v));
		break;
	case 25:
		asm("st %%f25, [%0]" : : "r" (&v));
		break;
	case 26:
		asm("st %%f26, [%0]" : : "r" (&v));
		break;
	case 27:
		asm("st %%f27, [%0]" : : "r" (&v));
		break;
	case 28:
		asm("st %%f28, [%0]" : : "r" (&v));
		break;
	case 29:
		asm("st %%f29, [%0]" : : "r" (&v));
		break;
	case 30:
		asm("st %%f30, [%0]" : : "r" (&v));
		break;
	case 31:
		asm("st %%f31, [%0]" : : "r" (&v));
		break;
	case 32:
		asm("st %%f32, [%0]" : : "r" (&v));
		break;
	case 33:
		asm("st %%f33, [%0]" : : "r" (&v));
		break;
	case 34:
		asm("st %%f34, [%0]" : : "r" (&v));
		break;
	case 35:
		asm("st %%f35, [%0]" : : "r" (&v));
		break;
	case 36:
		asm("st %%f36, [%0]" : : "r" (&v));
		break;
	case 37:
		asm("st %%f37, [%0]" : : "r" (&v));
		break;
	case 38:
		asm("st %%f38, [%0]" : : "r" (&v));
		break;
	case 39:
		asm("st %%f39, [%0]" : : "r" (&v));
		break;
	case 40:
		asm("st %%f40, [%0]" : : "r" (&v));
		break;
	case 41:
		asm("st %%f41, [%0]" : : "r" (&v));
		break;
	case 42:
		asm("st %%f42, [%0]" : : "r" (&v));
		break;
	case 43:
		asm("st %%f43, [%0]" : : "r" (&v));
		break;
	case 44:
		asm("st %%f44, [%0]" : : "r" (&v));
		break;
	case 45:
		asm("st %%f45, [%0]" : : "r" (&v));
		break;
	case 46:
		asm("st %%f46, [%0]" : : "r" (&v));
		break;
	case 47:
		asm("st %%f47, [%0]" : : "r" (&v));
		break;
	case 48:
		asm("st %%f48, [%0]" : : "r" (&v));
		break;
	case 49:
		asm("st %%f49, [%0]" : : "r" (&v));
		break;
	case 50:
		asm("st %%f50, [%0]" : : "r" (&v));
		break;
	case 51:
		asm("st %%f51, [%0]" : : "r" (&v));
		break;
	case 52:
		asm("st %%f52, [%0]" : : "r" (&v));
		break;
	case 53:
		asm("st %%f53, [%0]" : : "r" (&v));
		break;
	case 54:
		asm("st %%f54, [%0]" : : "r" (&v));
		break;
	case 55:
		asm("st %%f55, [%0]" : : "r" (&v));
		break;
	case 56:
		asm("st %%f56, [%0]" : : "r" (&v));
		break;
	case 57:
		asm("st %%f57, [%0]" : : "r" (&v));
		break;
	case 58:
		asm("st %%f58, [%0]" : : "r" (&v));
		break;
	case 59:
		asm("st %%f59, [%0]" : : "r" (&v));
		break;
	case 60:
		asm("st %%f60, [%0]" : : "r" (&v));
		break;
	case 61:
		asm("st %%f61, [%0]" : : "r" (&v));
		break;
	case 62:
		asm("st %%f62, [%0]" : : "r" (&v));
		break;
	case 63:
		asm("st %%f63, [%0]" : : "r" (&v));
		break;
	}
	return (v);
}

u_int64_t
__fpu_getreg64(int r)
{
	u_int64_t v;

	switch (r) {
	case 0:
		asm("std %%f0, [%0]" : : "r" (&v));
		break;
	case 2:
		asm("std %%f2, [%0]" : : "r" (&v));
		break;
	case 4:
		asm("std %%f4, [%0]" : : "r" (&v));
		break;
	case 6:
		asm("std %%f6, [%0]" : : "r" (&v));
		break;
	case 8:
		asm("std %%f8, [%0]" : : "r" (&v));
		break;
	case 10:
		asm("std %%f10, [%0]" : : "r" (&v));
		break;
	case 12:
		asm("std %%f12, [%0]" : : "r" (&v));
		break;
	case 14:
		asm("std %%f14, [%0]" : : "r" (&v));
		break;
	case 16:
		asm("std %%f16, [%0]" : : "r" (&v));
		break;
	case 18:
		asm("std %%f18, [%0]" : : "r" (&v));
		break;
	case 20:
		asm("std %%f20, [%0]" : : "r" (&v));
		break;
	case 22:
		asm("std %%f22, [%0]" : : "r" (&v));
		break;
	case 24:
		asm("std %%f24, [%0]" : : "r" (&v));
		break;
	case 26:
		asm("std %%f26, [%0]" : : "r" (&v));
		break;
	case 28:
		asm("std %%f28, [%0]" : : "r" (&v));
		break;
	case 30:
		asm("std %%f30, [%0]" : : "r" (&v));
		break;
	case 32:
		asm("std %%f32, [%0]" : : "r" (&v));
		break;
	case 34:
		asm("std %%f34, [%0]" : : "r" (&v));
		break;
	case 36:
		asm("std %%f36, [%0]" : : "r" (&v));
		break;
	case 38:
		asm("std %%f38, [%0]" : : "r" (&v));
		break;
	case 40:
		asm("std %%f40, [%0]" : : "r" (&v));
		break;
	case 42:
		asm("std %%f42, [%0]" : : "r" (&v));
		break;
	case 44:
		asm("std %%f44, [%0]" : : "r" (&v));
		break;
	case 46:
		asm("std %%f46, [%0]" : : "r" (&v));
		break;
	case 48:
		asm("std %%f48, [%0]" : : "r" (&v));
		break;
	case 50:
		asm("std %%f50, [%0]" : : "r" (&v));
		break;
	case 52:
		asm("std %%f52, [%0]" : : "r" (&v));
		break;
	case 54:
		asm("std %%f54, [%0]" : : "r" (&v));
		break;
	case 56:
		asm("std %%f56, [%0]" : : "r" (&v));
		break;
	case 58:
		asm("std %%f58, [%0]" : : "r" (&v));
		break;
	case 60:
		asm("std %%f60, [%0]" : : "r" (&v));
		break;
	case 62:
		asm("std %%f62, [%0]" : : "r" (&v));
		break;
	}
	return (v);
}
