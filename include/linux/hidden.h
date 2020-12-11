/* SPDX-License-Identifier: GPL-2.0 */
/*
 * When building position independent code with GCC using the -fPIC option,
 * (or even the -fPIE one on older versions), it will assume that we are
 * building a dynamic object (either a shared library or an executable) that
 * may have symbol references that can only be resolved at load time. For a
 * variety of reasons (ELF symbol preemption, the CoW footprint of the section
 * that is modified by the loader), this results in all references to symbols
 * with external linkage to go via entries in the Global Offset Table (GOT),
 * which carries absolute addresses which need to be fixed up when the
 * executable image is loaded at an offset which is different from its link
 * time offset.
 *
 * Fortunately, there is a way to inform the compiler that such symbol
 * references will be satisfied at link time rather than at load time, by
 * giving them 'hidden' visibility.
 */

#pragma GCC visibility push(hidden)
