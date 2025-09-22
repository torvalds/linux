/*-
 * Copyright (c) 2010, 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David A. Holland.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Config for predefined macros. If this doesn't do what you want you
 * can set any or all of the CONFIG_ defines from the compiler command
 * line; or patch the list in main.c; or both.
 */

/*
 * Paths
 */

#ifndef CONFIG_LOCALINCLUDE
#define CONFIG_LOCALINCLUDE "/usr/local/include"
#endif

#ifndef CONFIG_SYSTEMINCLUDE
#define CONFIG_SYSTEMINCLUDE "/usr/include"
#endif

/*
 * Operating system
 */

#ifndef CONFIG_OS
#if defined(__NetBSD__)
#define CONFIG_OS "__NetBSD__"
#define CONFIG_OS_2 "__unix__"
#elif defined(__FreeBSD__)
#define CONFIG_OS "__FreeBSD__"
#define CONFIG_OS_2 "__unix__"
#elif defined(__OpenBSD__)
#define CONFIG_OS "__OpenBSD__"
#define CONFIG_OS_2 "__unix__"
#elif defined(__DragonFly__)
#define CONFIG_OS "__DragonFly__"
#define CONFIG_OS_2 "__unix__"
#elif defined(__bsdi__)
#define CONFIG_OS "__bsdi__"
#define CONFIG_OS_2 "__unix__"
#elif defined(__sun)
#define CONFIG_OS "__sun"
#define CONFIG_OS_2 "__unix__"
#elif defined(__sgi)
#define CONFIG_OS "__sgi"
#define CONFIG_OS_2 "__unix__"
#elif defined(__SVR4)
#define CONFIG_OS "__SVR4"
#define CONFIG_OS_2 "__unix__"
#elif defined(__APPLE__)
#define CONFIG_OS "__APPLE__"
#define CONFIG_OS_2 "__unix__"
#elif defined(__linux__)
#define CONFIG_OS "__linux__"
#elif defined(__CYGWIN__)
#define CONFIG_OS "__CYGWIN__"
#elif defined(__INTERIX)
#define CONFIG_OS "__INTERIX"
#elif defined(__MINGW32)
#define CONFIG_OS "__MINGW32"
#else
/* we could error... but let's instead assume generic unix */
#define CONFIG_OS "__unix__"
#endif
#endif

/*
 * CPU
 */

#ifndef CONFIG_CPU
#if defined(__x86_64__)
#define CONFIG_CPU "__x86_64__"
#define CONFIG_CPU_2 "__amd64__"
#elif defined(__i386__) || defined(__i386)
#define CONFIG_CPU "__i386__"
#define CONFIG_CPU_2 "__i386"
#elif defined(__sparc)
#define CONFIG_CPU "__sparc"
#elif defined(__mips)
#define CONFIG_CPU "__mips"
#elif defined(__mips__)
#define CONFIG_CPU "__mips__"
#elif defined(__mipsel__)
#define CONFIG_CPU "__mipsel__"
#elif defined(__POWERPC__)
#define CONFIG_CPU "__POWERPC__"
#elif defined(__POWERPC__)
#define CONFIG_CPU "__powerpc__"
#elif defined(__PPC__)
#define CONFIG_CPU "__PPC__"
#elif defined(__ppc__)
#define CONFIG_CPU "__ppc__"
#elif defined(__PPC64__)
#define CONFIG_CPU "__PPC64__"
#elif defined(__ppc64__)
#define CONFIG_CPU "__ppc64__"
#elif defined(__ARM__)
#define CONFIG_CPU "__ARM__"
#elif defined(__AARCH64__)
#define CONFIG_CPU "__AARCH64__"
#elif defined(__aarch64__)
#define CONFIG_CPU "__aarch64__"
#elif defined(__RISCV__)
#define CONFIG_CPU "__RISCV__"
#elif defined(__riscv__)
#define CONFIG_CPU "__riscv__"
#elif defined(__RISCV64__)
#define CONFIG_CPU "__RISCV64__"
#elif defined(__riscv64__)
#define CONFIG_CPU "__riscv64__"
#elif defined(__riscv64)
#define CONFIG_CPU "__riscv64"
#elif defined(__ia64__)
#define CONFIG_CPU "__ia64__"
#else
/* let it go */
#endif
#endif

/*
 * Other stuff
 */

#ifndef CONFIG_SIZE
#ifdef __LP64__
#define CONFIG_SIZE "__LP64__"
#else
#define CONFIG_SIZE "__ILP32__"
#endif
#endif

#ifndef CONFIG_BINFMT
#ifdef __ELF__
#define CONFIG_BINFMT "__ELF__"
#endif
#endif

/*
 * We are __TRADCPP__ by default, but if you want to masquerade as
 * some other compiler this is a convenient place to change it.
 */

#ifndef CONFIG_COMPILER
#define CONFIG_COMPILER "__TRADCPP__"
#define CONFIG_COMPILER_MINOR "__TRADCPP_MINOR__"
#endif
