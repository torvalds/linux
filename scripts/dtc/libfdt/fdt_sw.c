// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */
#include "libfdt_env.h"

#include <fdt.h>
#include <libfdt.h>

#include "libfdt_internal.h"

static int fdt_sw_probe_(void *fdt)
{
	if (!can_assume(VALID_INPUT)) {
		if (fdt_magic(fdt) == FDT_MAGIC)
			return -FDT_ERR_BADSTATE;
		else if (fdt_magic(fdt) != FDT_SW_MAGIC)
			return -FDT_ERR_BADMAGIC;
	}

	return 0;
}

#define FDT_SW_PROBE(fdt) \
	{ \
		int err; \
		if ((err = fdt_sw_probe_(fdt)) != 0) \
			return err; \
	}

/* 'memrsv' state:	Initial state after fdt_create()
 *
 * Allowed functions:
 *	fdt_add_reservemap_entry()
 *	fdt_finish_reservemap()		[moves to 'struct' state]
 */
static int fdt_sw_probe_memrsv_(void *fdt)
{
	int err = fdt_sw_probe_(fdt);
	if (err)
		return err;

	if (!can_assume(VALID_INPUT) && fdt_off_dt_strings(fdt) != 0)
		return -FDT_ERR_BADSTATE;
	return 0;
}

#define FDT_SW_PROBE_MEMRSV(fdt) \
	{ \
		int err; \
		if ((err = fdt_sw_probe_memrsv_(fdt)) != 0) \
			return err; \
	}

/* 'struct' state:	Enter this state after fdt_finish_reservemap()
 *
 * Allowed functions:
 *	fdt_begin_node()
 *	fdt_end_node()
 *	fdt_property*()
 *	fdt_finish()			[moves to 'complete' state]
 */
static int fdt_sw_probe_struct_(void *fdt)
{
	int err = fdt_sw_probe_(fdt);
	if (err)
		return err;

	if (!can_assume(VALID_INPUT) &&
	    fdt_off_dt_strings(fdt) != fdt_totalsize(fdt))
		return -FDT_ERR_BADSTATE;
	return 0;
}

#define FDT_SW_PROBE_STRUCT(fdt) \
	{ \
		int err; \
		if ((err = fdt_sw_probe_struct_(fdt)) != 0) \
			return err; \
	}

static inline uint32_t sw_flags(void *fdt)
{
	/* assert: (fdt_magic(fdt) == FDT_SW_MAGIC) */
	return fdt_last_comp_version(fdt);
}

/* 'complete' state:	Enter this state after fdt_finish()
 *
 * Allowed functions: none
 */

static void *fdt_grab_space_(void *fdt, size_t len)
{
	unsigned int offset = fdt_size_dt_struct(fdt);
	unsigned int spaceleft;

	spaceleft = fdt_totalsize(fdt) - fdt_off_dt_struct(fdt)
		- fdt_size_dt_strings(fdt);

	if ((offset + len < offset) || (offset + len > spaceleft))
		return NULL;

	fdt_set_size_dt_struct(fdt, offset + len);
	return fdt_offset_ptr_w_(fdt, offset);
}

int fdt_create_with_flags(void *buf, int bufsize, uint32_t flags)
{
	const int hdrsize = FDT_ALIGN(sizeof(struct fdt_header),
				      sizeof(struct fdt_reserve_entry));
	void *fdt = buf;

	if (bufsize < hdrsize)
		return -FDT_ERR_NOSPACE;

	if (flags & ~FDT_CREATE_FLAGS_ALL)
		return -FDT_ERR_BADFLAGS;

	memset(buf, 0, bufsize);

	/*
	 * magic and last_comp_version keep intermediate state during the fdt
	 * creation process, which is replaced with the proper FDT format by
	 * fdt_finish().
	 *
	 * flags should be accessed with sw_flags().
	 */
	fdt_set_magic(fdt, FDT_SW_MAGIC);
	fdt_set_version(fdt, FDT_LAST_SUPPORTED_VERSION);
	fdt_set_last_comp_version(fdt, flags);

	fdt_set_totalsize(fdt,  bufsize);

	fdt_set_off_mem_rsvmap(fdt, hdrsize);
	fdt_set_off_dt_struct(fdt, fdt_off_mem_rsvmap(fdt));
	fdt_set_off_dt_strings(fdt, 0);

	return 0;
}

int fdt_create(void *buf, int bufsize)
{
	return fdt_create_with_flags(buf, bufsize, 0);
}

int fdt_resize(void *fdt, void *buf, int bufsize)
{
	size_t headsize, tailsize;
	char *oldtail, *newtail;

	FDT_SW_PROBE(fdt);

	if (bufsize < 0)
		return -FDT_ERR_NOSPACE;

	headsize = fdt_off_dt_struct(fdt) + fdt_size_dt_struct(fdt);
	tailsize = fdt_size_dt_strings(fdt);

	if (!can_assume(VALID_DTB) &&
	    headsize + tailsize > fdt_totalsize(fdt))
		return -FDT_ERR_INTERNAL;

	if ((headsize + tailsize) > (unsigned)bufsize)
		return -FDT_ERR_NOSPACE;

	oldtail = (char *)fdt + fdt_totalsize(fdt) - tailsize;
	newtail = (char *)buf + bufsize - tailsize;

	/* Two cases to avoid clobbering data if the old and new
	 * buffers partially overlap */
	if (buf <= fdt) {
		memmove(buf, fdt, headsize);
		memmove(newtail, oldtail, tailsize);
	} else {
		memmove(newtail, oldtail, tailsize);
		memmove(buf, fdt, headsize);
	}

	fdt_set_totalsize(buf, bufsize);
	if (fdt_off_dt_strings(buf))
		fdt_set_off_dt_strings(buf, bufsize);

	return 0;
}

int fdt_add_reservemap_entry(void *fdt, uint64_t addr, uint64_t size)
{
	struct fdt_reserve_entry *re;
	int offset;

	FDT_SW_PROBE_MEMRSV(fdt);

	offset = fdt_off_dt_struct(fdt);
	if ((offset + sizeof(*re)) > fdt_totalsize(fdt))
		return -FDT_ERR_NOSPACE;

	re = (struct fdt_reserve_entry *)((char *)fdt + offset);
	re->address = cpu_to_fdt64(addr);
	re->size = cpu_to_fdt64(size);

	fdt_set_off_dt_struct(fdt, offset + sizeof(*re));

	return 0;
}

int fdt_finish_reservemap(void *fdt)
{
	int err = fdt_add_reservemap_entry(fdt, 0, 0);

	if (err)
		return err;

	fdt_set_off_dt_strings(fdt, fdt_totalsize(fdt));
	return 0;
}

int fdt_begin_node(void *fdt, const char *name)
{
	struct fdt_node_header *nh;
	int namelen;

	FDT_SW_PROBE_STRUCT(fdt);

	namelen = strlen(name) + 1;
	nh = fdt_grab_space_(fdt, sizeof(*nh) + FDT_TAGALIGN(namelen));
	if (! nh)
		return -FDT_ERR_NOSPACE;

	nh->tag = cpu_to_fdt32(FDT_BEGIN_NODE);
	memcpy(nh->name, name, namelen);
	return 0;
}

int fdt_end_node(void *fdt)
{
	fdt32_t *en;

	FDT_SW_PROBE_STRUCT(fdt);

	en = fdt_grab_space_(fdt, FDT_TAGSIZE);
	if (! en)
		return -FDT_ERR_NOSPACE;

	*en = cpu_to_fdt32(FDT_END_NODE);
	return 0;
}

static int fdt_add_string_(void *fdt, const char *s)
{
	char *strtab = (char *)fdt + fdt_totalsize(fdt);
	unsigned int strtabsize = fdt_size_dt_strings(fdt);
	unsigned int len = strlen(s) + 1;
	unsigned int struct_top, offset;

	offset = strtabsize + len;
	struct_top = fdt_off_dt_struct(fdt) + fdt_size_dt_struct(fdt);
	if (fdt_totalsize(fdt) - offset < struct_top)
		return 0; /* no more room :( */

	memcpy(strtab - offset, s, len);
	fdt_set_size_dt_strings(fdt, strtabsize + len);
	return -offset;
}

/* Must only be used to roll back in case of error */
static void fdt_del_last_string_(void *fdt, const char *s)
{
	int strtabsize = fdt_size_dt_strings(fdt);
	int len = strlen(s) + 1;

	fdt_set_size_dt_strings(fdt, strtabsize - len);
}

static int fdt_find_add_string_(void *fdt, const char *s, int *allocated)
{
	char *strtab = (char *)fdt + fdt_totalsize(fdt);
	int strtabsize = fdt_size_dt_strings(fdt);
	const char *p;

	*allocated = 0;

	p = fdt_find_string_(strtab - strtabsize, strtabsize, s);
	if (p)
		return p - strtab;

	*allocated = 1;

	return fdt_add_string_(fdt, s);
}

int fdt_property_placeholder(void *fdt, const char *name, int len, void **valp)
{
	struct fdt_property *prop;
	int nameoff;
	int allocated;

	FDT_SW_PROBE_STRUCT(fdt);

	/* String de-duplication can be slow, _NO_NAME_DEDUP skips it */
	if (sw_flags(fdt) & FDT_CREATE_FLAG_NO_NAME_DEDUP) {
		allocated = 1;
		nameoff = fdt_add_string_(fdt, name);
	} else {
		nameoff = fdt_find_add_string_(fdt, name, &allocated);
	}
	if (nameoff == 0)
		return -FDT_ERR_NOSPACE;

	prop = fdt_grab_space_(fdt, sizeof(*prop) + FDT_TAGALIGN(len));
	if (! prop) {
		if (allocated)
			fdt_del_last_string_(fdt, name);
		return -FDT_ERR_NOSPACE;
	}

	prop->tag = cpu_to_fdt32(FDT_PROP);
	prop->nameoff = cpu_to_fdt32(nameoff);
	prop->len = cpu_to_fdt32(len);
	*valp = prop->data;
	return 0;
}

int fdt_property(void *fdt, const char *name, const void *val, int len)
{
	void *ptr;
	int ret;

	ret = fdt_property_placeholder(fdt, name, len, &ptr);
	if (ret)
		return ret;
	memcpy(ptr, val, len);
	return 0;
}

int fdt_finish(void *fdt)
{
	char *p = (char *)fdt;
	fdt32_t *end;
	int oldstroffset, newstroffset;
	uint32_t tag;
	int offset, nextoffset;

	FDT_SW_PROBE_STRUCT(fdt);

	/* Add terminator */
	end = fdt_grab_space_(fdt, sizeof(*end));
	if (! end)
		return -FDT_ERR_NOSPACE;
	*end = cpu_to_fdt32(FDT_END);

	/* Relocate the string table */
	oldstroffset = fdt_totalsize(fdt) - fdt_size_dt_strings(fdt);
	newstroffset = fdt_off_dt_struct(fdt) + fdt_size_dt_struct(fdt);
	memmove(p + newstroffset, p + oldstroffset, fdt_size_dt_strings(fdt));
	fdt_set_off_dt_strings(fdt, newstroffset);

	/* Walk the structure, correcting string offsets */
	offset = 0;
	while ((tag = fdt_next_tag(fdt, offset, &nextoffset)) != FDT_END) {
		if (tag == FDT_PROP) {
			struct fdt_property *prop =
				fdt_offset_ptr_w_(fdt, offset);
			int nameoff;

			nameoff = fdt32_to_cpu(prop->nameoff);
			nameoff += fdt_size_dt_strings(fdt);
			prop->nameoff = cpu_to_fdt32(nameoff);
		}
		offset = nextoffset;
	}
	if (nextoffset < 0)
		return nextoffset;

	/* Finally, adjust the header */
	fdt_set_totalsize(fdt, newstroffset + fdt_size_dt_strings(fdt));

	/* And fix up fields that were keeping intermediate state. */
	fdt_set_last_comp_version(fdt, FDT_LAST_COMPATIBLE_VERSION);
	fdt_set_magic(fdt, FDT_MAGIC);

	return 0;
}
