/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 *
 * libfdt is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 *
 *  a) This library is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of the
 *     License, or (at your option) any later version.
 *
 *     This library is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this library; if not, write to the Free
 *     Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 *     MA 02110-1301 USA
 *
 * Alternatively,
 *
 *  b) Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *     1. Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *     2. Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "libfdt_env.h"

#include <fdt.h>
#include <libfdt.h>

#include "libfdt_internal.h"

static int _fdt_blocks_misordered(const void *fdt,
			      int mem_rsv_size, int struct_size)
{
	return (fdt_off_mem_rsvmap(fdt) < FDT_ALIGN(sizeof(struct fdt_header), 8))
		|| (fdt_off_dt_struct(fdt) <
		    (fdt_off_mem_rsvmap(fdt) + mem_rsv_size))
		|| (fdt_off_dt_strings(fdt) <
		    (fdt_off_dt_struct(fdt) + struct_size))
		|| (fdt_totalsize(fdt) <
		    (fdt_off_dt_strings(fdt) + fdt_size_dt_strings(fdt)));
}

static int _fdt_rw_check_header(void *fdt)
{
	FDT_CHECK_HEADER(fdt);

	if (fdt_version(fdt) < 17)
		return -FDT_ERR_BADVERSION;
	if (_fdt_blocks_misordered(fdt, sizeof(struct fdt_reserve_entry),
				   fdt_size_dt_struct(fdt)))
		return -FDT_ERR_BADLAYOUT;
	if (fdt_version(fdt) > 17)
		fdt_set_version(fdt, 17);

	return 0;
}

#define FDT_RW_CHECK_HEADER(fdt) \
	{ \
		int err; \
		if ((err = _fdt_rw_check_header(fdt)) != 0) \
			return err; \
	}

static inline int _fdt_data_size(void *fdt)
{
	return fdt_off_dt_strings(fdt) + fdt_size_dt_strings(fdt);
}

static int _fdt_splice(void *fdt, void *splicepoint, int oldlen, int newlen)
{
	char *p = splicepoint;
	char *end = (char *)fdt + _fdt_data_size(fdt);

	if (((p + oldlen) < p) || ((p + oldlen) > end))
		return -FDT_ERR_BADOFFSET;
	if ((end - oldlen + newlen) > ((char *)fdt + fdt_totalsize(fdt)))
		return -FDT_ERR_NOSPACE;
	memmove(p + newlen, p + oldlen, end - p - oldlen);
	return 0;
}

static int _fdt_splice_mem_rsv(void *fdt, struct fdt_reserve_entry *p,
			       int oldn, int newn)
{
	int delta = (newn - oldn) * sizeof(*p);
	int err;
	err = _fdt_splice(fdt, p, oldn * sizeof(*p), newn * sizeof(*p));
	if (err)
		return err;
	fdt_set_off_dt_struct(fdt, fdt_off_dt_struct(fdt) + delta);
	fdt_set_off_dt_strings(fdt, fdt_off_dt_strings(fdt) + delta);
	return 0;
}

static int _fdt_splice_struct(void *fdt, void *p,
			      int oldlen, int newlen)
{
	int delta = newlen - oldlen;
	int err;

	if ((err = _fdt_splice(fdt, p, oldlen, newlen)))
		return err;

	fdt_set_size_dt_struct(fdt, fdt_size_dt_struct(fdt) + delta);
	fdt_set_off_dt_strings(fdt, fdt_off_dt_strings(fdt) + delta);
	return 0;
}

static int _fdt_splice_string(void *fdt, int newlen)
{
	void *p = (char *)fdt
		+ fdt_off_dt_strings(fdt) + fdt_size_dt_strings(fdt);
	int err;

	if ((err = _fdt_splice(fdt, p, 0, newlen)))
		return err;

	fdt_set_size_dt_strings(fdt, fdt_size_dt_strings(fdt) + newlen);
	return 0;
}

static int _fdt_find_add_string(void *fdt, const char *s)
{
	char *strtab = (char *)fdt + fdt_off_dt_strings(fdt);
	const char *p;
	char *new;
	int len = strlen(s) + 1;
	int err;

	p = _fdt_find_string(strtab, fdt_size_dt_strings(fdt), s);
	if (p)
		/* found it */
		return (p - strtab);

	new = strtab + fdt_size_dt_strings(fdt);
	err = _fdt_splice_string(fdt, len);
	if (err)
		return err;

	memcpy(new, s, len);
	return (new - strtab);
}

int fdt_add_mem_rsv(void *fdt, uint64_t address, uint64_t size)
{
	struct fdt_reserve_entry *re;
	int err;

	FDT_RW_CHECK_HEADER(fdt);

	re = _fdt_mem_rsv_w(fdt, fdt_num_mem_rsv(fdt));
	err = _fdt_splice_mem_rsv(fdt, re, 0, 1);
	if (err)
		return err;

	re->address = cpu_to_fdt64(address);
	re->size = cpu_to_fdt64(size);
	return 0;
}

int fdt_del_mem_rsv(void *fdt, int n)
{
	struct fdt_reserve_entry *re = _fdt_mem_rsv_w(fdt, n);
	int err;

	FDT_RW_CHECK_HEADER(fdt);

	if (n >= fdt_num_mem_rsv(fdt))
		return -FDT_ERR_NOTFOUND;

	err = _fdt_splice_mem_rsv(fdt, re, 1, 0);
	if (err)
		return err;
	return 0;
}

static int _fdt_resize_property(void *fdt, int nodeoffset, const char *name,
				int len, struct fdt_property **prop)
{
	int oldlen;
	int err;

	*prop = fdt_get_property_w(fdt, nodeoffset, name, &oldlen);
	if (! (*prop))
		return oldlen;

	if ((err = _fdt_splice_struct(fdt, (*prop)->data, FDT_TAGALIGN(oldlen),
				      FDT_TAGALIGN(len))))
		return err;

	(*prop)->len = cpu_to_fdt32(len);
	return 0;
}

static int _fdt_add_property(void *fdt, int nodeoffset, const char *name,
			     int len, struct fdt_property **prop)
{
	int proplen;
	int nextoffset;
	int namestroff;
	int err;

	if ((nextoffset = _fdt_check_node_offset(fdt, nodeoffset)) < 0)
		return nextoffset;

	namestroff = _fdt_find_add_string(fdt, name);
	if (namestroff < 0)
		return namestroff;

	*prop = _fdt_offset_ptr_w(fdt, nextoffset);
	proplen = sizeof(**prop) + FDT_TAGALIGN(len);

	err = _fdt_splice_struct(fdt, *prop, 0, proplen);
	if (err)
		return err;

	(*prop)->tag = cpu_to_fdt32(FDT_PROP);
	(*prop)->nameoff = cpu_to_fdt32(namestroff);
	(*prop)->len = cpu_to_fdt32(len);
	return 0;
}

int fdt_set_name(void *fdt, int nodeoffset, const char *name)
{
	char *namep;
	int oldlen, newlen;
	int err;

	FDT_RW_CHECK_HEADER(fdt);

	namep = (char *)(uintptr_t)fdt_get_name(fdt, nodeoffset, &oldlen);
	if (!namep)
		return oldlen;

	newlen = strlen(name);

	err = _fdt_splice_struct(fdt, namep, FDT_TAGALIGN(oldlen+1),
				 FDT_TAGALIGN(newlen+1));
	if (err)
		return err;

	memcpy(namep, name, newlen+1);
	return 0;
}

int fdt_setprop(void *fdt, int nodeoffset, const char *name,
		const void *val, int len)
{
	struct fdt_property *prop;
	int err;

	FDT_RW_CHECK_HEADER(fdt);

	err = _fdt_resize_property(fdt, nodeoffset, name, len, &prop);
	if (err == -FDT_ERR_NOTFOUND)
		err = _fdt_add_property(fdt, nodeoffset, name, len, &prop);
	if (err)
		return err;

	memcpy(prop->data, val, len);
	return 0;
}

int fdt_appendprop(void *fdt, int nodeoffset, const char *name,
		   const void *val, int len)
{
	struct fdt_property *prop;
	int err, oldlen, newlen;

	FDT_RW_CHECK_HEADER(fdt);

	prop = fdt_get_property_w(fdt, nodeoffset, name, &oldlen);
	if (prop) {
		newlen = len + oldlen;
		err = _fdt_splice_struct(fdt, prop->data,
					 FDT_TAGALIGN(oldlen),
					 FDT_TAGALIGN(newlen));
		if (err)
			return err;
		prop->len = cpu_to_fdt32(newlen);
		memcpy(prop->data + oldlen, val, len);
	} else {
		err = _fdt_add_property(fdt, nodeoffset, name, len, &prop);
		if (err)
			return err;
		memcpy(prop->data, val, len);
	}
	return 0;
}

int fdt_delprop(void *fdt, int nodeoffset, const char *name)
{
	struct fdt_property *prop;
	int len, proplen;

	FDT_RW_CHECK_HEADER(fdt);

	prop = fdt_get_property_w(fdt, nodeoffset, name, &len);
	if (! prop)
		return len;

	proplen = sizeof(*prop) + FDT_TAGALIGN(len);
	return _fdt_splice_struct(fdt, prop, proplen, 0);
}

int fdt_add_subnode_namelen(void *fdt, int parentoffset,
			    const char *name, int namelen)
{
	struct fdt_node_header *nh;
	int offset, nextoffset;
	int nodelen;
	int err;
	uint32_t tag;
	uint32_t *endtag;

	FDT_RW_CHECK_HEADER(fdt);

	offset = fdt_subnode_offset_namelen(fdt, parentoffset, name, namelen);
	if (offset >= 0)
		return -FDT_ERR_EXISTS;
	else if (offset != -FDT_ERR_NOTFOUND)
		return offset;

	/* Try to place the new node after the parent's properties */
	fdt_next_tag(fdt, parentoffset, &nextoffset); /* skip the BEGIN_NODE */
	do {
		offset = nextoffset;
		tag = fdt_next_tag(fdt, offset, &nextoffset);
	} while ((tag == FDT_PROP) || (tag == FDT_NOP));

	nh = _fdt_offset_ptr_w(fdt, offset);
	nodelen = sizeof(*nh) + FDT_TAGALIGN(namelen+1) + FDT_TAGSIZE;

	err = _fdt_splice_struct(fdt, nh, 0, nodelen);
	if (err)
		return err;

	nh->tag = cpu_to_fdt32(FDT_BEGIN_NODE);
	memset(nh->name, 0, FDT_TAGALIGN(namelen+1));
	memcpy(nh->name, name, namelen);
	endtag = (uint32_t *)((char *)nh + nodelen - FDT_TAGSIZE);
	*endtag = cpu_to_fdt32(FDT_END_NODE);

	return offset;
}

int fdt_add_subnode(void *fdt, int parentoffset, const char *name)
{
	return fdt_add_subnode_namelen(fdt, parentoffset, name, strlen(name));
}

int fdt_del_node(void *fdt, int nodeoffset)
{
	int endoffset;

	FDT_RW_CHECK_HEADER(fdt);

	endoffset = _fdt_node_end_offset(fdt, nodeoffset);
	if (endoffset < 0)
		return endoffset;

	return _fdt_splice_struct(fdt, _fdt_offset_ptr_w(fdt, nodeoffset),
				  endoffset - nodeoffset, 0);
}

static void _fdt_packblocks(const char *old, char *new,
			    int mem_rsv_size, int struct_size)
{
	int mem_rsv_off, struct_off, strings_off;

	mem_rsv_off = FDT_ALIGN(sizeof(struct fdt_header), 8);
	struct_off = mem_rsv_off + mem_rsv_size;
	strings_off = struct_off + struct_size;

	memmove(new + mem_rsv_off, old + fdt_off_mem_rsvmap(old), mem_rsv_size);
	fdt_set_off_mem_rsvmap(new, mem_rsv_off);

	memmove(new + struct_off, old + fdt_off_dt_struct(old), struct_size);
	fdt_set_off_dt_struct(new, struct_off);
	fdt_set_size_dt_struct(new, struct_size);

	memmove(new + strings_off, old + fdt_off_dt_strings(old),
		fdt_size_dt_strings(old));
	fdt_set_off_dt_strings(new, strings_off);
	fdt_set_size_dt_strings(new, fdt_size_dt_strings(old));
}

int fdt_open_into(const void *fdt, void *buf, int bufsize)
{
	int err;
	int mem_rsv_size, struct_size;
	int newsize;
	const char *fdtstart = fdt;
	const char *fdtend = fdtstart + fdt_totalsize(fdt);
	char *tmp;

	FDT_CHECK_HEADER(fdt);

	mem_rsv_size = (fdt_num_mem_rsv(fdt)+1)
		* sizeof(struct fdt_reserve_entry);

	if (fdt_version(fdt) >= 17) {
		struct_size = fdt_size_dt_struct(fdt);
	} else {
		struct_size = 0;
		while (fdt_next_tag(fdt, struct_size, &struct_size) != FDT_END)
			;
		if (struct_size < 0)
			return struct_size;
	}

	if (!_fdt_blocks_misordered(fdt, mem_rsv_size, struct_size)) {
		/* no further work necessary */
		err = fdt_move(fdt, buf, bufsize);
		if (err)
			return err;
		fdt_set_version(buf, 17);
		fdt_set_size_dt_struct(buf, struct_size);
		fdt_set_totalsize(buf, bufsize);
		return 0;
	}

	/* Need to reorder */
	newsize = FDT_ALIGN(sizeof(struct fdt_header), 8) + mem_rsv_size
		+ struct_size + fdt_size_dt_strings(fdt);

	if (bufsize < newsize)
		return -FDT_ERR_NOSPACE;

	/* First attempt to build converted tree at beginning of buffer */
	tmp = buf;
	/* But if that overlaps with the old tree... */
	if (((tmp + newsize) > fdtstart) && (tmp < fdtend)) {
		/* Try right after the old tree instead */
		tmp = (char *)(uintptr_t)fdtend;
		if ((tmp + newsize) > ((char *)buf + bufsize))
			return -FDT_ERR_NOSPACE;
	}

	_fdt_packblocks(fdt, tmp, mem_rsv_size, struct_size);
	memmove(buf, tmp, newsize);

	fdt_set_magic(buf, FDT_MAGIC);
	fdt_set_totalsize(buf, bufsize);
	fdt_set_version(buf, 17);
	fdt_set_last_comp_version(buf, 16);
	fdt_set_boot_cpuid_phys(buf, fdt_boot_cpuid_phys(fdt));

	return 0;
}

int fdt_pack(void *fdt)
{
	int mem_rsv_size;

	FDT_RW_CHECK_HEADER(fdt);

	mem_rsv_size = (fdt_num_mem_rsv(fdt)+1)
		* sizeof(struct fdt_reserve_entry);
	_fdt_packblocks(fdt, fdt, mem_rsv_size, fdt_size_dt_struct(fdt));
	fdt_set_totalsize(fdt, _fdt_data_size(fdt));

	return 0;
}
