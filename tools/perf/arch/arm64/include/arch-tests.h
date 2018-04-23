/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_TESTS_H
#define ARCH_TESTS_H

#ifdef HAVE_DWARF_UNWIND_SUPPORT
struct thread;
struct perf_sample;
#endif

extern struct test arch_tests[];

#endif
