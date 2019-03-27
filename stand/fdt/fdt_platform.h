/*-
 * Copyright (c) 2014 Andrew Turner <andrew@FreeBSD.org>
 * All rights reserved.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef FDT_PLATFORM_H
#define FDT_PLATFORM_H

struct fdt_header;

struct fdt_mem_region {
	unsigned long	start;
	unsigned long	size;
};

#define	TMP_MAX_ETH	8

int fdt_copy(vm_offset_t);
void fdt_fixup_cpubusfreqs(unsigned long, unsigned long);
void fdt_fixup_ethernet(const char *, char *, int);
void fdt_fixup_memory(struct fdt_mem_region *, size_t);
void fdt_fixup_stdout(const char *);
void fdt_apply_overlays(void);
int fdt_load_dtb_addr(struct fdt_header *);
int fdt_load_dtb_file(const char *);
void fdt_load_dtb_overlays(const char *);
int fdt_setup_fdtp(void);

/* The platform library needs to implement these functions */
int fdt_platform_load_dtb(void);
void fdt_platform_fixups(void);

#endif /* FDT_PLATFORM_H */
