/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *                                                                   USA
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

struct data data_grow_for(struct data d, int xlen)
{
	struct data nd;
	int newsize;

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

	d = data_grow_for(empty_data, strlen(s)+1);

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

	while (!feof(f) && (d.len < maxlen)) {
		size_t chunksize, ret;

		if (maxlen == -1)
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
	uint16_t value_16;
	uint32_t value_32;
	uint64_t value_64;

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

struct data data_append_re(struct data d, const struct fdt_reserve_entry *re)
{
	struct fdt_reserve_entry bere;

	bere.address = cpu_to_fdt64(re->address);
	bere.size = cpu_to_fdt64(re->size);

	return data_append_data(d, &bere, sizeof(bere));
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

	m = xmalloc(sizeof(*m));
	m->offset = d.len;
	m->type = type;
	m->ref = ref;
	m->next = NULL;

	return data_append_markers(d, m);
}

int data_is_one_string(struct data d)
{
	int i;
	int len = d.len;

	if (len == 0)
		return 0;

	for (i = 0; i < len-1; i++)
		if (d.val[i] == '\0')
			return 0;

	if (d.val[len-1] != '\0')
		return 0;

	return 1;
}
