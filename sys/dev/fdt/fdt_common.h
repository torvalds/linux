/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under sponsorship from
 * the FreeBSD Foundation.
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

#ifndef _FDT_COMMON_H_
#define _FDT_COMMON_H_

#include <sys/sysctl.h>
#include <sys/slicer.h>
#include <contrib/libfdt/libfdt_env.h>
#include <dev/ofw/ofw_bus.h>

#define FDT_MEM_REGIONS	16

#define DI_MAX_INTR_NUM	32

struct fdt_sense_level {
	enum intr_trigger	trig;
	enum intr_polarity	pol;
};

#if defined(__arm__) && !defined(INTRNG)
typedef int (*fdt_pic_decode_t)(phandle_t, pcell_t *, int *, int *, int *);
extern fdt_pic_decode_t fdt_pic_table[];
#endif

#if defined(__arm__) || defined(__powerpc__)
typedef void (*fdt_fixup_t)(phandle_t);
struct fdt_fixup_entry {
	char		*model;
	fdt_fixup_t	handler;
};
extern struct fdt_fixup_entry fdt_fixup_table[];
#endif

extern SLIST_HEAD(fdt_ic_list, fdt_ic) fdt_ic_list_head;
struct fdt_ic {
	SLIST_ENTRY(fdt_ic)	fdt_ics;
	ihandle_t		iph;
	device_t		dev;
};

extern vm_paddr_t fdt_immr_pa;
extern vm_offset_t fdt_immr_va;
extern vm_offset_t fdt_immr_size;

#if defined(FDT_DTB_STATIC)
extern u_char fdt_static_dtb;
#endif

SYSCTL_DECL(_hw_fdt);

int fdt_addrsize_cells(phandle_t, int *, int *);
u_long fdt_data_get(void *, int);
int fdt_data_to_res(pcell_t *, int, int, u_long *, u_long *);
phandle_t fdt_find_compatible(phandle_t, const char *, int);
phandle_t fdt_depth_search_compatible(phandle_t, const char *, int);
int fdt_get_mem_regions(struct mem_region *, int *, uint64_t *);
int fdt_get_reserved_mem(struct mem_region *, int *);
int fdt_get_reserved_regions(struct mem_region *, int *);
int fdt_get_phyaddr(phandle_t, device_t, int *, void **);
int fdt_get_range(phandle_t, int, u_long *, u_long *);
int fdt_immr_addr(vm_offset_t);
int fdt_regsize(phandle_t, u_long *, u_long *);
int fdt_is_compatible_strict(phandle_t, const char *);
int fdt_parent_addr_cells(phandle_t);
int fdt_get_chosen_bootargs(char *bootargs, size_t max_size);

#endif /* _FDT_COMMON_H_ */
