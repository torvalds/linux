/*	$OpenBSD: DEFS.h,v 1.5 2023/12/11 22:24:15 kettenis Exp $ */
/*
 * Copyright (c) 2017 Philip Guenther <guenther@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <machine/asm.h>

/*
 * We define a hidden alias with the prefix "_libc_" for each global symbol
 * that may be used internally.  By referencing _libc_x instead of x, other
 * parts of libc prevent overriding by the application and avoid unnecessary
 * relocations.
 */
#define _HIDDEN(x)		_libc_##x
#define _HIDDEN_ALIAS(x,y)			\
	STRONG_ALIAS(_HIDDEN(x),y);		\
	.hidden _HIDDEN(x)
#define _HIDDEN_FALIAS(x,y)			\
	_HIDDEN_ALIAS(x,y);			\
	.type _HIDDEN(x),@function

/*
 * For functions implemented in ASM that aren't syscalls.
 *   END_STRONG(x)	Like DEF_STRONG() in C; for standard/reserved C names
 *   END_WEAK(x)	Like DEF_WEAK() in C; for non-ISO C names
 *   END_BUILTIN(x)	If compiling with clang, then just END() and
 *			mark it .protected, else be like END_STRONG();
 *			for clang builtins like memcpy
 */
#define	END_STRONG(x)	END(x); _HIDDEN_FALIAS(x,x); END(_HIDDEN(x))
#define	END_WEAK(x)	END_STRONG(x); .weak x

#ifdef __clang__
#define	END_BUILTIN(x)	END(x); .protected x
#else
#define	END_BUILTIN(x)	END_STRONG(x)
#endif

#define PINSYSCALL(sysno, label)					\
	.pushsection .openbsd.syscalls,"",@progbits;			\
	.p2align 2;							\
	.long label;							\
	.long sysno;							\
	.popsection;
