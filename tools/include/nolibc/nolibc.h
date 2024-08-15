/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/* nolibc.h
 * Copyright (C) 2017-2018 Willy Tarreau <w@1wt.eu>
 */

/*
 * This file is designed to be used as a libc alternative for minimal programs
 * with very limited requirements. It consists of a small number of syscall and
 * type definitions, and the minimal startup code needed to call main().
 * All syscalls are declared as static functions so that they can be optimized
 * away by the compiler when not used.
 *
 * Syscalls are split into 3 levels:
 *   - The lower level is the arch-specific syscall() definition, consisting in
 *     assembly code in compound expressions. These are called my_syscall0() to
 *     my_syscall6() depending on the number of arguments. The MIPS
 *     implementation is limited to 5 arguments. All input arguments are cast
 *     to a long stored in a register. These expressions always return the
 *     syscall's return value as a signed long value which is often either a
 *     pointer or the negated errno value.
 *
 *   - The second level is mostly architecture-independent. It is made of
 *     static functions called sys_<name>() which rely on my_syscallN()
 *     depending on the syscall definition. These functions are responsible
 *     for exposing the appropriate types for the syscall arguments (int,
 *     pointers, etc) and for setting the appropriate return type (often int).
 *     A few of them are architecture-specific because the syscalls are not all
 *     mapped exactly the same among architectures. For example, some archs do
 *     not implement select() and need pselect6() instead, so the sys_select()
 *     function will have to abstract this.
 *
 *   - The third level is the libc call definition. It exposes the lower raw
 *     sys_<name>() calls in a way that looks like what a libc usually does,
 *     takes care of specific input values, and of setting errno upon error.
 *     There can be minor variations compared to standard libc calls. For
 *     example the open() call always takes 3 args here.
 *
 * The errno variable is declared static and unused. This way it can be
 * optimized away if not used. However this means that a program made of
 * multiple C files may observe different errno values (one per C file). For
 * the type of programs this project targets it usually is not a problem. The
 * resulting program may even be reduced by defining the NOLIBC_IGNORE_ERRNO
 * macro, in which case the errno value will never be assigned.
 *
 * Some stdint-like integer types are defined. These are valid on all currently
 * supported architectures, because signs are enforced, ints are assumed to be
 * 32 bits, longs the size of a pointer and long long 64 bits. If more
 * architectures have to be supported, this may need to be adapted.
 *
 * Some macro definitions like the O_* values passed to open(), and some
 * structures like the sys_stat struct depend on the architecture.
 *
 * The definitions start with the architecture-specific parts, which are picked
 * based on what the compiler knows about the target architecture, and are
 * completed with the generic code. Since it is the compiler which sets the
 * target architecture, cross-compiling normally works out of the box without
 * having to specify anything.
 *
 * Finally some very common libc-level functions are provided. It is the case
 * for a few functions usually found in string.h, ctype.h, or stdlib.h.
 *
 * The nolibc.h file is only a convenient entry point which includes all other
 * files. It also defines the NOLIBC macro, so that it is possible for a
 * program to check this macro to know if it is being built against and decide
 * to disable some features or simply not to include some standard libc files.
 *
 * A simple static executable may be built this way :
 *      $ gcc -fno-asynchronous-unwind-tables -fno-ident -s -Os -nostdlib \
 *            -static -include nolibc.h -o hello hello.c -lgcc
 *
 * Simple programs meant to be reasonably portable to various libc and using
 * only a few common includes, may also be built by simply making the include
 * path point to the nolibc directory:
 *      $ gcc -fno-asynchronous-unwind-tables -fno-ident -s -Os -nostdlib \
 *            -I../nolibc -o hello hello.c -lgcc
 *
 * The available standard (but limited) include files are:
 *   ctype.h, errno.h, signal.h, stdio.h, stdlib.h, string.h, time.h
 *
 * In addition, the following ones are expected to be provided by the compiler:
 *   float.h, stdarg.h, stddef.h
 *
 * The following ones which are part to the C standard are not provided:
 *   assert.h, locale.h, math.h, setjmp.h, limits.h
 *
 * A very useful calling convention table may be found here :
 *      http://man7.org/linux/man-pages/man2/syscall.2.html
 *
 * This doc is quite convenient though not necessarily up to date :
 *      https://w3challs.com/syscalls/
 *
 */
#ifndef _NOLIBC_H
#define _NOLIBC_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"
#include "ctype.h"
#include "signal.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "time.h"
#include "unistd.h"

/* Used by programs to avoid std includes */
#define NOLIBC

#endif /* _NOLIBC_H */
