// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_FENV_H
#define _LIBCPP_FENV_H

/*
    fenv.h synopsis

This entire header is C99 / C++0X

Macros:

    FE_DIVBYZERO
    FE_INEXACT
    FE_INVALID
    FE_OVERFLOW
    FE_UNDERFLOW
    FE_ALL_EXCEPT
    FE_DOWNWARD
    FE_TONEAREST
    FE_TOWARDZERO
    FE_UPWARD
    FE_DFL_ENV

Types:

    fenv_t
    fexcept_t

int feclearexcept(int excepts);
int fegetexceptflag(fexcept_t* flagp, int excepts);
int feraiseexcept(int excepts);
int fesetexceptflag(const fexcept_t* flagp, int excepts);
int fetestexcept(int excepts);
int fegetround();
int fesetround(int round);
int fegetenv(fenv_t* envp);
int feholdexcept(fenv_t* envp);
int fesetenv(const fenv_t* envp);
int feupdateenv(const fenv_t* envp);


*/

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if __has_include_next(<fenv.h>)
#  include_next <fenv.h>
#endif

#ifdef __cplusplus

extern "C++" {

#  ifdef feclearexcept
#    undef feclearexcept
#  endif

#  ifdef fegetexceptflag
#    undef fegetexceptflag
#  endif

#  ifdef feraiseexcept
#    undef feraiseexcept
#  endif

#  ifdef fesetexceptflag
#    undef fesetexceptflag
#  endif

#  ifdef fetestexcept
#    undef fetestexcept
#  endif

#  ifdef fegetround
#    undef fegetround
#  endif

#  ifdef fesetround
#    undef fesetround
#  endif

#  ifdef fegetenv
#    undef fegetenv
#  endif

#  ifdef feholdexcept
#    undef feholdexcept
#  endif

#  ifdef fesetenv
#    undef fesetenv
#  endif

#  ifdef feupdateenv
#    undef feupdateenv
#  endif

} // extern "C++"

#endif // defined(__cplusplus)

#endif // _LIBCPP_FENV_H
