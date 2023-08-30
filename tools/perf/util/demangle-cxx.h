/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_DEMANGLE_CXX
#define __PERF_DEMANGLE_CXX 1

#ifdef __cplusplus
extern "C" {
#endif

char *cxx_demangle_sym(const char *str, bool params, bool modifiers);

#ifdef __cplusplus
}
#endif


#endif /* __PERF_DEMANGLE_CXX */
