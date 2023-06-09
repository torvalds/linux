// SPDX-License-Identifier: GPL-2.0
#include "demangle-cxx.h"
#include <stdlib.h>
#include <string.h>
#include <linux/compiler.h>

#ifdef HAVE_LIBBFD_SUPPORT
#define PACKAGE 'perf'
#include <bfd.h>
#endif

#ifdef HAVE_CXA_DEMANGLE_SUPPORT
#include <cxxabi.h>
#endif

#if defined(HAVE_LIBBFD_SUPPORT) || defined(HAVE_CPLUS_DEMANGLE_SUPPORT)
#ifndef DMGL_PARAMS
#define DMGL_PARAMS     (1 << 0)  /* Include function args */
#define DMGL_ANSI       (1 << 1)  /* Include const, volatile, etc */
#endif
#endif

/*
 * Demangle C++ function signature
 *
 * Note: caller is responsible for freeing demangled string
 */
extern "C"
char *cxx_demangle_sym(const char *str, bool params __maybe_unused,
                       bool modifiers __maybe_unused)
{
#ifdef HAVE_LIBBFD_SUPPORT
        int flags = (params ? DMGL_PARAMS : 0) | (modifiers ? DMGL_ANSI : 0);

        return bfd_demangle(NULL, str, flags);
#elif defined(HAVE_CPLUS_DEMANGLE_SUPPORT)
        int flags = (params ? DMGL_PARAMS : 0) | (modifiers ? DMGL_ANSI : 0);

        return cplus_demangle(str, flags);
#elif defined(HAVE_CXA_DEMANGLE_SUPPORT)
        char *output;
        int status;

        output = abi::__cxa_demangle(str, /*output_buffer=*/NULL, /*length=*/NULL, &status);
        return output;
#else
        return NULL;
#endif
}
