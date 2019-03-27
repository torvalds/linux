/*-
 * Copyright (c) 2015 John H. Baldwin <jhb@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Map system call codes to names for the supported ABIs on each
 * platform.  Rather than regnerating system call name tables locally
 * during the build, use the generated tables in the kernel source
 * tree.
 */

#include <sys/param.h>
#include <sys/acl.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <stdio.h>
#include <sysdecode.h>

static
#include <kern/syscalls.c>

#if defined(__amd64__) || defined(__powerpc64__)
static
#include <compat/freebsd32/freebsd32_syscalls.c>
#endif

#if defined(__aarch64__) || defined(__amd64__) || defined(__i386__)
static
#ifdef __aarch64__
#include <arm64/linux/linux_syscalls.c>
#elif __amd64__
#include <amd64/linux/linux_syscalls.c>
#else
#include <i386/linux/linux_syscalls.c>
#endif
#endif

#ifdef __amd64__
static
#include <amd64/linux32/linux32_syscalls.c>
#endif

static
#include <compat/cloudabi32/cloudabi32_syscalls.c>
static
#include <compat/cloudabi64/cloudabi64_syscalls.c>

const char *
sysdecode_syscallname(enum sysdecode_abi abi, unsigned int code)
{

	switch (abi) {
	case SYSDECODE_ABI_FREEBSD:
		if (code < nitems(syscallnames))
			return (syscallnames[code]);
		break;
#if defined(__amd64__) || defined(__powerpc64__)
	case SYSDECODE_ABI_FREEBSD32:
		if (code < nitems(freebsd32_syscallnames))
			return (freebsd32_syscallnames[code]);
		break;
#endif
#if defined(__aarch64__) || defined(__amd64__) || defined(__i386__)
	case SYSDECODE_ABI_LINUX:
		if (code < nitems(linux_syscallnames))
			return (linux_syscallnames[code]);
		break;
#endif
#ifdef __amd64__
	case SYSDECODE_ABI_LINUX32:
		if (code < nitems(linux32_syscallnames))
			return (linux32_syscallnames[code]);
		break;
#endif
	case SYSDECODE_ABI_CLOUDABI32:
		if (code < nitems(cloudabi32_syscallnames))
			return (cloudabi32_syscallnames[code]);
		break;
	case SYSDECODE_ABI_CLOUDABI64:
		if (code < nitems(cloudabi64_syscallnames))
			return (cloudabi64_syscallnames[code]);
		break;
	default:
		break;
	}
	return (NULL);
}
