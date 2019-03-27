/*-
 * Copyright (c) 2017 Netflix, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_EFI_OSDEP_H_
#define	_EFI_OSDEP_H_

/*
 * Defines to adjust the types that EDK2 uses for FreeBSD so we can
 * use the code and headers mostly unchanged. The headers are imported
 * all into one directory to avoid case issues with filenames and
 * included. The actual code is heavily modified since it has too many
 * annoying dependencies that are difficult to satisfy.
 */

#include <sys/cdefs.h>
#include <stdlib.h>
#include <stdint.h>
#include <uuid.h>

typedef int8_t INT8;
typedef int16_t INT16;
typedef int32_t INT32;
typedef int64_t INT64;
typedef intptr_t INTN;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uintptr_t UINTN;
//typedef uintptr_t EFI_PHYSICAL_ADDRESS;
//typedef uint32_t EFI_IPv4_ADDRESS;
//typedef uint8_t EFI_MAC_ADDRESS[6];
//typedef uint8_t EFI_IPv6_ADDRESS[16];
typedef uint8_t CHAR8;
typedef uint16_t CHAR16;
typedef UINT8 BOOLEAN;
typedef void VOID;
//typedef uuid_t GUID;
//typedef uuid_t EFI_GUID;

/* We can't actually call this stuff, so snip out API syntactic sugar */
#define INTERFACE_DECL(x)
#define EFIAPI
#define IN
#define OUT
#define CONST const
#define OPTIONAL
//#define TRUE 1
//#define FALSE 0

/*
 * EDK2 has fine definitions for these, so let it define them.
 */
#undef NULL
#undef EFI_PAGE_SIZE
#undef EFI_PAGE_MASK

/*
 * Note: the EDK2 code assumed #pragma packed works and PACKED is a
 * workaround for some old toolchain issues for EDK2 that aren't
 * relevent to FreeBSD.
 */
#define PACKED

/*
 * Since we're not compiling for the UEFI boot time (which use ms abi
 * conventions), tell EDK2 to define VA_START correctly. For the boot
 * loader, this likely needs to be different.
 */
#define NO_MSABI_VA_FUNCS 1

/*
 * Finally, we need to define the processor we are in EDK2 terms.
 */
#if defined(__i386__)
#define MDE_CPU_IA32
#elif defined(__amd64__)
#define MDE_CPU_X64
#elif defined(__arm__)
#define MDE_CPU_ARM
#elif defined(__aarch64__)
#define MDE_CPU_AARCH64
#endif
/* FreeBSD doesn't have/use MDE_CPU_EBC or MDE_CPU_IPF (ia64) */

#endif /* _EFI_OSDEP_H_ */
