/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_DEMANGLE_JAVA
#define __PERF_DEMANGLE_JAVA 1
/*
 * demangle function flags
 */
#define JAVA_DEMANGLE_NORET	0x1 /* do not process return type */

char * java_demangle_sym(const char *str, int flags);

#endif /* __PERF_DEMANGLE_JAVA */
