/*	$OpenBSD: DEFS.h,v 1.2 2023/12/13 09:01:25 miod Exp $	*/

/*
 * Copyright (c) 1998-2002 Michael Shalayeff
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF MIND
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>

#define	END(x)	EXIT(x)

/*
 * We define a hidden alias with the prefix "_libc_" for each global symbol
 * that may be used internally.  By referencing _libc_x instead of x, other
 * parts of libc prevent overriding by the application and avoid unnecessary
 * relocations.
 */
#define _HIDDEN(x)		_libc_##x
#define _HIDDEN_ALIAS(x,y)			\
	STRONG_ALIAS(_HIDDEN(x),y)		!\
	.hidden _HIDDEN(x)
#define _HIDDEN_FALIAS(x,y)			\
	_HIDDEN_ALIAS(x,y)			!\
	.type _HIDDEN(x),@function

/*
 * For functions implemented in ASM that aren't syscalls.
 *   END_STRONG(x)	Like DEF_STRONG() in C; for standard/reserved C names
 *   END_WEAK(x)	Like DEF_WEAK() in C; for non-ISO C names
 *   ALTEND_STRONG(x) and ALTEND_WEAK()
 *			Matching macros for ALTENTRY functions
 *   END_BUILTIN(x)	If compiling with clang, then just END() and
 *			mark it .protected, else be like END_STRONG();
 *			for clang builtins like memcpy
 *
 * If a 'BUILTIN' function needs be referenced by other ASM code, then use
 *   _BUILTIN(x)	If compiled with clang, then just x, otherwise
 *			_HIDDEN(x)
 *
 *   _END(x)		Set a size on a symbol, like END(), but even for
 *			symbols with no matching ENTRY().  (On alpha and
 *			mips64, END() generates .end which requires a
 *			matching .ent from ENTRY())
 */
#define	END_STRONG(x)	END(x) ! _HIDDEN_FALIAS(x,x) ! _END(_HIDDEN(x))
#define	END_WEAK(x)	END_STRONG(x) ! .weak x
#define	ALTEND_STRONG(x) _HIDDEN_FALIAS(x,x) ! _END(_HIDDEN(x))
#define	ALTEND_WEAK(x)	ALTEND_STRONG(x) ! .weak x

#ifdef __clang__
#define	END_BUILTIN(x)	END(x) ! .protected x
#define	_BUILTIN(x)	x
#else
#define	END_BUILTIN(x)	END_STRONG(x)
#define	_BUILTIN(x)	_HIDDEN(x)
#endif

#define _END(x)		.size x, . - x

#define PINSYSCALL(sysno, label)			\
	.pushsection .openbsd.syscalls,"",@progbits	!\
	.p2align 2					!\
	.long label					!\
	.long sysno					!\
	.popsection
