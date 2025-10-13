// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 */

#include "dtc.h"

void data_free(struct data d)
{
	struct marker *m, *nm;

	m = d.markers;
	while (m) {
		nm = m->next;
		free(m->ref);
		free(m);
		m = nm;
	}

	if (d.val)
		free(d.val);
}

struct data data_grow_for(struct data d, unsigned int xlen)
{
	struct data nd;
	unsigned int newsize;

	if (xlen == 0)
		return d;

	nd = d;

	newsize = xlen;

	while ((d.len + xlen) > newsize)
		newsize *= 2;

	nd.val = xrealloc(d.val, newsize);

	return nd;
}

struct data data_copy_mem(const char *mem, int len)
{
	struct data d;

	d = data_grow_for(empty_data, len);

	d.len = len;
	memcpy(d.val, mem, len);

	return d;
}

struct data data_copy_escape_string(const char *s, int len)
{
	int i = 0;
	struct data d;
	char *q;

	d = data_add_marker(empty_data, TYPE_STRING, NULL);
	d = data_grow_for(d, len + 1);

	q = d.val;
	while (i < len) {
		char c = s[i++];

		if (c == '\\')
			c = get_escape_char(s, &i);

		q[d.len++] = c;
	}

	q[d.len++] = '\0';
	return d;
}

struct data data_copy_file(FILE *f, size_t maxlen)
{
	struct data d = empty_data;

	d = data_add_marker(d, TYPE_NONE, NULL);
	while (!feof(f) && (d.len < maxlen)) {
		size_t chunksize, ret;

		if (maxlen == (size_t)-1)
			chunksize = 4096;
		else
			chunksize = maxlen - d.len;

		d = data_grow_for(d, chunksize);
		ret = fread(d.val + d.len, 1, chunksize, f);

		if (ferror(f))
			die("Error reading file into data: %s", strerror(errno));

		if (d.len + ret < d.len)
			die("Overflow reading file into data\n");

		d.len += ret;
	}

	return d;
}

struct data data_append_data(struct data d, const void *p, int len)
{
	d = data_grow_for(d, len);
	memcpy(d.val + d.len, p, len);
	d.len += len;
	return d;
}

struct data data_insert_at_marker(struct data d, struct marker *m,
				  const void *p, int len)
{
	d = data_grow_for(d, len);
	memmove(d.val + m->offset + len, d.val + m->offset, d.len - m->offset);
	memcpy(d.val + m->offset, p, len);
	d.len += len;

	/* Adjust all markers after the one we're inserting at */
	m = m->next;
	for_each_marker(m)
		m->offset += len;
	return d;
}

static struct data data_append_markers(struct data d, struct marker *m)
{
	struct marker **mp = &d.markers;

	/* Find the end of the markerlist */
	while (*mp)
		mp = &((*mp)->next);
	*mp = m;
	return d;
}

struct data data_merge(struct data d1, struct data d2)
{
	struct data d;
	struct marker *m2 = d2.markers;

	d = data_append_markers(data_append_data(d1, d2.val, d2.len), m2);

	/* Adjust for the length of d1 */
	for_each_marker(m2)
		m2->offset += d1.len;

	d2.markers = NULL; /* So data_free() doesn't clobber them */
	data_free(d2);

	return d;
}

struct data data_append_integer(struct data d, uint64_t value, int bits)
{
	uint8_t value_8;
	fdt16_t value_16;
	fdt32_t value_32;
	fdt64_t value_64;

	switch (bits) {
	case 8:
		value_8 = value;
		return data_append_data(d, &value_8, 1);

	case 16:
		value_16 = cpu_to_fdt16(value);
		return data_append_data(d, &value_16, 2);

	case 32:
		value_32 = cpu_to_fdt32(value);
		return data_append_data(d, &value_32, 4);

	case 64:
		value_64 = cpu_to_fdt64(value);
		return data_append_data(d, &value_64, 8);

	default:
		die("Invalid literal size (%d)\n", bits);
	}
}

struct data data_append_re(struct data d, uint64_t address, uint64_t size)
{
	struct fdt_reserve_entry re;

	re.address = cpu_to_fdt64(address);
	re.size = cpu_to_fdt64(size);

	return data_append_data(d, &re, sizeof(re));
}

struct data data_append_cell(struct data d, cell_t word)
{
	return data_append_integer(d, word, sizeof(word) * 8);
}

struct data data_append_addr(struct data d, uint64_t addr)
{
	return data_append_integer(d, addr, sizeof(addr) * 8);
}

struct data data_append_byte(struct data d, uint8_t byte)
{
	return data_append_data(d, &byte, 1);
}

struct data data_append_zeroes(struct data d, int len)
{
	d = data_grow_for(d, len);

	memset(d.val + d.len, 0, len);
	d.len += len;
	return d;
}

struct data data_append_align(struct data d, int align)
{
	int newlen = ALIGN(d.len, align);
	return data_append_zeroes(d, newlen - d.len);
}

struct data data_add_marker(struct data d, enum markertype type, char *ref)
{
	struct marker *m;

	m = alloc_marker(d.len, type, ref);

	return data_append_markers(d, m);
}

bool data_is_one_string(struct data d)
{
	int i;
	int len = d.len;

	if (len == 0)
		return false;

	for (i = 0; i < len-1; i++)
		if (d.val[i] == '\0')
			return false;

	if (d.val[len-1] != '\0')
		return false;

	return true;
}

struct data data_insert_data(struct data d, struct marker *m, struct data old)
{
	unsigned int offset = m->offset;
	struct marker *next = m->next;
	struct marker *marker;
	struct data new_data;
	char *ref;

	new_data = data_insert_at_marker(d, m, old.val, old.len);

	/* Copy all markers from old value */
	marker = old.markers;
	for_each_marker(marker) {
		ref = NULL;

		if (marker->ref)
			ref = xstrdup(marker->ref);

		m->next = alloc_marker(marker->offset + offset, marker->type,
				       ref);
		m = m->next;
	}
	m->next = next;

	return new_data;
}

struct marker *alloc_marker(unsigned int offset, enum markertype type,
			    char *ref)
{
	struct marker *m;

	m = xmalloc(sizeof(*m));
	m->offset = offset;
	m->type = type;
	m->ref = ref;
	m->next = NULL;

	return m;
}
