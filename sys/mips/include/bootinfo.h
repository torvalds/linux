/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2013 Robert N. M. Watson
 * Copyright (C) 1994 by Rodney W. Grimes, Milwaukie, Oregon  97222
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Rodney W. Grimes.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RODNEY W. GRIMES ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL RODNEY W. GRIMES BE LIABLE
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

#ifndef	_MACHINE_BOOTINFO_H_
#define	_MACHINE_BOOTINFO_H_

/* Only change the version number if you break compatibility. */
#define	BOOTINFO_VERSION	2

#define	MIPS_BOOTINFO_MAGIC	0xCDEACDEA

#if defined(__mips_n32) || defined(__mips_n64)
typedef	uint64_t	bi_ptr_t;
#else
typedef	uint32_t	bi_ptr_t;
#endif

/*
 * A zero bootinfo field often means that there is no info available.
 * Flags are used to indicate the validity of fields where zero is a
 * normal value.
 */
struct bootinfo {
	/* bootinfo meta-data. */
	uint32_t	bi_version;
	uint32_t	bi_size;

	/* bootinfo contents. */
	uint64_t	bi_boot2opts;	/* boot2 flags to loader. */
	bi_ptr_t	bi_kernelname;	/* Pointer to name. */
	bi_ptr_t	bi_nfs_diskless;/* Pointer to NFS data. */
	bi_ptr_t	bi_dtb;		/* Pointer to dtb. */
	bi_ptr_t	bi_memsize;	/* Physical memory size in bytes. */
	bi_ptr_t	bi_modulep;	/* Preloaded modules. */
	bi_ptr_t	bi_boot_dev_type;	/* Boot-device type. */
	bi_ptr_t	bi_boot_dev_unitptr;	/* Boot-device unit/pointer. */
};

/*
 * Possible boot-device types passed from boot2 to loader, loader to kernel.
 * In most cases, the object pointed to will hold a filesystem; one exception
 * is BOOTINFO_DEV_TYPE_DRAM, which points to a pre-loaded object (e.g.,
 * loader, kernel).
 */
#define	BOOTINFO_DEV_TYPE_DRAM		0	/* DRAM loader/kernel (ptr). */
#define	BOOTINFO_DEV_TYPE_CFI		1	/* CFI flash (unit). */
#define	BOOTINFO_DEV_TYPE_SDCARD	2	/* SD card (unit). */

#ifdef _KERNEL
extern struct bootinfo	bootinfo;
#endif

#endif	/* !_MACHINE_BOOTINFO_H_ */
