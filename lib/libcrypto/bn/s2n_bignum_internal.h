// $OpenBSD: s2n_bignum_internal.h,v 1.5 2025/08/12 10:01:37 jsing Exp $
//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#ifdef __APPLE__
#   define S2N_BN_SYMBOL(NAME) _##NAME
#   if defined(__AARCH64EL__) || defined(__ARMEL__)
#     define __LF %%
#   else
#     define __LF ;
#   endif
#else
#   define S2N_BN_SYMBOL(name) name
#   define __LF ;
#endif

#define S2N_BN_SYM_VISIBILITY_DIRECTIVE(name) .globl S2N_BN_SYMBOL(name)
#ifdef S2N_BN_HIDE_SYMBOLS
#   ifdef __APPLE__
#      define S2N_BN_SYM_PRIVACY_DIRECTIVE(name) .private_extern S2N_BN_SYMBOL(name)
#   else
#      define S2N_BN_SYM_PRIVACY_DIRECTIVE(name) .hidden S2N_BN_SYMBOL(name)
#   endif
#else
#   define S2N_BN_SYM_PRIVACY_DIRECTIVE(name)  /* NO-OP: S2N_BN_SYM_PRIVACY_DIRECTIVE */
#endif

// Enable indirect branch tracking support unless explicitly disabled
// with -DNO_IBT. If the platform supports CET, simply inherit this from
// the usual header. Otherwise manually define _CET_ENDBR, used at each
// x86 entry point, to be the ENDBR64 instruction, with an explicit byte
// sequence for compilers/assemblers that don't know about it. Note that
// it is safe to use ENDBR64 on all platforms, since the encoding is by
// design interpreted as a NOP on all pre-CET x86_64 processors. The only
// downside is a small increase in code size and potentially a modest
// slowdown from executing one more instruction.

#if NO_IBT
#   if defined(_CET_ENDBR)
#     error "The s2n-bignum build option NO_IBT was configured, but _CET_ENDBR is defined in this compilation unit. That is weird, so failing the build."
#   endif
#   define _CET_ENDBR
#elif defined(__CET__)
#   include <cet.h>
#elif !defined(_CET_ENDBR)
#   define _CET_ENDBR .byte 0xf3,0x0f,0x1e,0xfa
#endif
