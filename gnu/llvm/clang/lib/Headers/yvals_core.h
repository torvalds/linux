//===----- yvals_core.h - Internal MSVC STL core header -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Only include this if we are aiming for MSVC compatibility.
#ifndef _MSC_VER
#include_next <yvals_core.h>
#else

#ifndef __clang_yvals_core_h
#define __clang_yvals_core_h

#include_next <yvals_core.h>

#ifdef _STL_INTRIN_HEADER
#undef _STL_INTRIN_HEADER
#define _STL_INTRIN_HEADER <intrin0.h>
#endif

#endif
#endif
