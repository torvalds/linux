/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Stacey D. Son
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#ifndef	_IMGACT_BINMISC_H_
#define	_IMGACT_BINMISC_H_

/**
 * Miscellaneous binary interpreter image activator.
 */

#include <sys/param.h>	/* for MAXPATHLEN */

/*
 * Imgact bin misc parameters.
 */
#define	IBE_VERSION	1	/* struct ximgact_binmisc_entry version. */
#define	IBE_NAME_MAX	32	/* Max size for entry name. */
#define	IBE_MAGIC_MAX	256	/* Max size for header magic and mask. */
#define	IBE_ARG_LEN_MAX	256	/* Max space for optional interpreter command-
				   line arguments separated by white space */
#define	IBE_INTERP_LEN_MAX	(MAXPATHLEN + IBE_ARG_LEN_MAX)
#define	IBE_MAX_ENTRIES	64	/* Max number of interpreter entries. */

/*
 * Imgact bin misc interpreter entry flags.
 */
#define	IBF_ENABLED	0x0001	/* Entry is active. */
#define	IBF_USE_MASK	0x0002	/* Use mask on header magic field. */

/*
 * Used with sysctlbyname() to pass imgact bin misc entries in and out of the
 * kernel.
 */
typedef struct ximgact_binmisc_entry {
	uint32_t xbe_version;	/* Struct version(IBE_VERSION) */
	uint32_t xbe_flags;	/* Entry flags (IBF_*) */
	uint32_t xbe_moffset;	/* Magic offset in header */
	uint32_t xbe_msize;	/* Magic size */
	uint32_t spare[3];	/* Spare fields for future use */
	char xbe_name[IBE_NAME_MAX];	/* Unique interpreter name */
	char xbe_interpreter[IBE_INTERP_LEN_MAX]; /* Interpreter path + args */
	uint8_t xbe_magic[IBE_MAGIC_MAX]; /* Header Magic */
	uint8_t xbe_mask[IBE_MAGIC_MAX]; /* Magic Mask */
} ximgact_binmisc_entry_t;

/*
 * sysctl() command names.
 */
#define	IBE_SYSCTL_NAME		"kern.binmisc"

#define	IBE_SYSCTL_NAME_ADD	IBE_SYSCTL_NAME ".add"
#define	IBE_SYSCTL_NAME_REMOVE	IBE_SYSCTL_NAME ".remove"
#define	IBE_SYSCTL_NAME_DISABLE	IBE_SYSCTL_NAME ".disable"
#define	IBE_SYSCTL_NAME_ENABLE	IBE_SYSCTL_NAME ".enable"
#define	IBE_SYSCTL_NAME_LOOKUP	IBE_SYSCTL_NAME ".lookup"
#define	IBE_SYSCTL_NAME_LIST	IBE_SYSCTL_NAME ".list"

#define	KMOD_NAME	"imgact_binmisc"

/*
 * Examples of manipulating the interpreter table using sysctlbyname(3):
 *
 * #include <sys/imgact_binmisc.h>
 *
 * #define LLVM_MAGIC  "BC\xc0\xde"
 *
 * #define MIPS64_ELF_MAGIC	"\x7f\x45\x4c\x46\x02\x02\x01\x00\x00\x00" \
 *				"\x00\x00\x00\x00\x00\x00\x00\x02\x00\x08"
 * #define MIPS64_ELF_MASK	"\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff" \
 *				"\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff"
 *
 * ximgact_binmisc_entry_t xbe, *xbep, out_xbe;
 * size_t size = 0, osize;
 * int error, i;
 *
 * // Add image activator for LLVM byte code
 * bzero(&xbe, sizeof(xbe));
 * xbe.xbe_version = IBE_VERSION;
 * xbe.xbe_flags = IBF_ENABLED;
 * strlcpy(xbe.xbe_name, "llvm_bc", IBE_NAME_MAX);
 * strlcpy(xbe.xbe_interpreter, "/usr/bin/lli --fake-arg0=#a",
 *     IBE_INTERP_LEN_MAX);
 * xbe.xbe_moffset = 0;
 * xbe.xbe_msize = 4;
 * memcpy(xbe.xbe_magic, LLVM_MAGIC, xbe.xbe_msize);
 * error = sysctlbyname(IBE_SYSCTL_NAME_ADD, NULL, NULL, &xbe, sizeof(xbe));
 *
 * // Add image activator for mips64 ELF binaries to use qemu user mode
 * bzero(&xbe, sizeof(xbe));
 * xbe.xbe_version = IBE_VERSION;
 * xbe.xbe_flags = IBF_ENABLED | IBF_USE_MASK;
 * strlcpy(xbe.xbe_name, "mips64elf", IBE_NAME_MAX);
 * strlcpy(xbe.xbe_interpreter, "/usr/local/bin/qemu-mips64",
 *	IBE_INTERP_LEN_MAX);
 * xbe.xbe_moffset = 0;
 * xbe.xbe_msize = 20;
 * memcpy(xbe.xbe_magic, MIPS64_ELF_MAGIC, xbe.xbe_msize);
 * memcpy(xbe.xbe_mask, MIPS64_ELF_MASK, xbe.xbe_msize);
 * sysctlbyname(IBE_SYSCTL_NAME_ADD, NULL, NULL, &xbe, sizeof(xbe));
 *
 * // Disable (OR Enable OR Remove) image activator for LLVM byte code
 * bzero(&xbe, sizeof(xbe));
 * xbe.xbe_version = IBE_VERSION;
 * strlcpy(xbe.xbe_name, "llvm_bc", IBE_NAME_MAX);
 * error = sysctlbyname(IBE_SYSCTL_NAME_DISABLE, NULL, NULL, &xbe, sizeof(xbe));
 * // OR sysctlbyname(IBE_SYSCTL_NAME_ENABLE, NULL, NULL, &xbe, sizeof(xbe));
 * // OR sysctlbyname(IBE_SYSCTL_NAME_REMOVE, NULL, NULL, &xbe, sizeof(xbe));
 *
 * // Lookup image activator  "llvm_bc"
 * bzero(&xbe, sizeof(xbe));
 * xbe.xbe_version = IBE_VERSION;
 * strlcpy(xbe.xbe_name, "llvm_bc", IBE_NAME_MAX);
 * size = sizeof(out_xbe);
 * error = sysctlbyname(IBE_SYSCTL_NAME_LOOKUP, &out_xbe, &size, &xbe,
 *	sizeof(xbe));
 *
 * // Get all the currently configured image activators and report
 * error = sysctlbyname(IBE_SYSCTL_NAME_LIST, NULL, &size, NULL, 0);
 * if (0 == error && size > 0) {
 *	xbep = malloc(size);
 *	while(1) {
 *	    osize = size;
 *	    error = sysctlbyname("kern.binmisc.list", xbep, &size, NULL, 0);
 *	    if (-1 == error && ENOMEM == errno && size == osize) {
 *		// The buffer too small and needs to grow
 *		size += sizeof(xbe);
 *		xbep = realloc(xbep, size);
 *	    } else
 *		break;
 *	}
 * }
 * for(i = 0; i < (size / sizeof(xbe)); i++, xbep++)
 *	printf("name: %s interpreter: %s flags: %s %s\n", xbep->xbe_name,
 *	    xbep->xbe_interpreter, (xbep->xbe_flags & IBF_ENABLED) ?
 *	    "ENABLED" : "", (xbep->xbe_flags & IBF_ENABLED) ? "USE_MASK" : "");
 *
 * The sysctlbyname() calls above may return the following errors in addition
 * to the standard ones:
 *
 * [EINVAL]  Invalid argument in the input ximgact_binmisc_entry_t structure.
 * [EEXIST]  Interpreter entry for given name already exist in kernel list.
 * [ENOMEM]  Allocating memory in the kernel failed or, in the case of
 *           kern.binmisc.list, the user buffer is too small.
 * [ENOENT]  Interpreter entry for given name is not found.
 * [ENOSPC]  Attempted to exceed maximum number of entries (IBE_MAX_ENTRIES).
 */

#endif /* !_IMGACT_BINMISC_H_ */
