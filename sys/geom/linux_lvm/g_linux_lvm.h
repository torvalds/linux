/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Andrew Thompson <thompsa@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#define	G_LLVM_DEBUG(lvl, ...)	do {					\
	if (g_llvm_debug >= (lvl)) {					\
		printf("GEOM_LINUX_LVM");				\
		if (g_llvm_debug > 0)					\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

#define	G_LLVM_CLASS_NAME	"LINUX_LVM"
#define	G_LLVM_NAMELEN		128
#define	G_LLVM_UUIDLEN		40
#define	G_LLVM_MAGIC		"\040\114\126\115\062\040\170\133" \
				"\065\101\045\162\060\116\052\076"

struct g_llvm_label {
	uint64_t	ll_sector;
	uint32_t	ll_crc;
	uint32_t	ll_offset;
	char		ll_uuid[G_LLVM_UUIDLEN];
	uint64_t	ll_size;
	uint64_t	ll_pestart;
	uint64_t	ll_md_offset;
	uint64_t	ll_md_size;
};

struct g_llvm_metadata {
	uint32_t		md_csum;
	uint32_t		md_version;
	uint64_t		md_start;
	uint64_t		md_size;
	uint64_t		md_reloffset;
	uint64_t		md_relsize;
	struct g_llvm_vg	*md_vg;
};

struct g_llvm_lv {
	LIST_ENTRY(g_llvm_lv)	lv_next;
	struct g_llvm_vg	*lv_vg;
	char			lv_name[G_LLVM_NAMELEN];
	char			lv_uuid[G_LLVM_UUIDLEN];
	int			lv_sgcount;
	int			lv_sgactive;
	struct g_provider	*lv_gprov;
	int			lv_extentcount;
	LIST_HEAD(, g_llvm_segment) lv_segs;
	int			lv_numsegs;
	struct g_llvm_segment	*lv_firstsg;
};

struct g_llvm_pv {
	LIST_ENTRY(g_llvm_pv)	pv_next;
	struct g_llvm_vg	*pv_vg;
	char			pv_name[G_LLVM_NAMELEN];
	char			pv_uuid[G_LLVM_UUIDLEN];
	size_t			pv_size;
	off_t			pv_start;
	int			pv_count;
	struct g_provider	*pv_gprov;
	struct g_consumer	*pv_gcons;
};

struct g_llvm_segment {
	LIST_ENTRY(g_llvm_segment)	sg_next;
	int			sg_start;
	int			sg_end;
	int			sg_count;
	char			sg_pvname[G_LLVM_NAMELEN];
	struct g_llvm_pv	*sg_pv;
	int			sg_pvstart;
	off_t			sg_pvoffset;
};

struct g_llvm_vg {
	LIST_ENTRY(g_llvm_vg)	vg_next;
	char			vg_name[G_LLVM_NAMELEN];
	char			vg_uuid[G_LLVM_UUIDLEN];
	size_t			vg_extentsize;
	int			vg_sectorsize;
	struct g_geom		*vg_geom;
	LIST_HEAD(, g_llvm_pv)	vg_pvs;
	LIST_HEAD(, g_llvm_lv)	vg_lvs;
};
