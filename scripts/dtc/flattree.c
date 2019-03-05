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
#include "srcpos.h"

#define FTF_FULLPATH	0x1
#define FTF_VARALIGN	0x2
#define FTF_NAMEPROPS	0x4
#define FTF_BOOTCPUID	0x8
#define FTF_STRTABSIZE	0x10
#define FTF_STRUCTSIZE	0x20
#define FTF_NOPS	0x40

static struct version_info {
	int version;
	int last_comp_version;
	int hdr_size;
	int flags;
} version_table[] = {
	{1, 1, FDT_V1_SIZE,
	 FTF_FULLPATH|FTF_VARALIGN|FTF_NAMEPROPS},
	{2, 1, FDT_V2_SIZE,
	 FTF_FULLPATH|FTF_VARALIGN|FTF_NAMEPROPS|FTF_BOOTCPUID},
	{3, 1, FDT_V3_SIZE,
	 FTF_FULLPATH|FTF_VARALIGN|FTF_NAMEPROPS|FTF_BOOTCPUID|FTF_STRTABSIZE},
	{16, 16, FDT_V3_SIZE,
	 FTF_BOOTCPUID|FTF_STRTABSIZE|FTF_NOPS},
	{17, 16, FDT_V17_SIZE,
	 FTF_BOOTCPUID|FTF_STRTABSIZE|FTF_STRUCTSIZE|FTF_NOPS},
};

struct emitter {
	void (*cell)(void *, cell_t);
	void (*string)(void *, const char *, int);
	void (*align)(void *, int);
	void (*data)(void *, struct data);
	void (*beginnode)(void *, struct label *labels);
	void (*endnode)(void *, struct label *labels);
	void (*property)(void *, struct label *labels);
};

static void bin_emit_cell(void *e, cell_t val)
{
	struct data *dtbuf = e;

	*dtbuf = data_append_cell(*dtbuf, val);
}

static void bin_emit_string(void *e, const char *str, int len)
{
	struct data *dtbuf = e;

	if (len == 0)
		len = strlen(str);

	*dtbuf = data_append_data(*dtbuf, str, len);
	*dtbuf = data_append_byte(*dtbuf, '\0');
}

static void bin_emit_align(void *e, int a)
{
	struct data *dtbuf = e;

	*dtbuf = data_append_align(*dtbuf, a);
}

static void bin_emit_data(void *e, struct data d)
{
	struct data *dtbuf = e;

	*dtbuf = data_append_data(*dtbuf, d.val, d.len);
}

static void bin_emit_beginnode(void *e, struct label *labels)
{
	bin_emit_cell(e, FDT_BEGIN_NODE);
}

static void bin_emit_endnode(void *e, struct label *labels)
{
	bin_emit_cell(e, FDT_END_NODE);
}

static void bin_emit_property(void *e, struct label *labels)
{
	bin_emit_cell(e, FDT_PROP);
}

static struct emitter bin_emitter = {
	.cell = bin_emit_cell,
	.string = bin_emit_string,
	.align = bin_emit_align,
	.data = bin_emit_data,
	.beginnode = bin_emit_beginnode,
	.endnode = bin_emit_endnode,
	.property = bin_emit_property,
};

static void emit_label(FILE *f, const char *prefix, const char *label)
{
	fprintf(f, "\t.globl\t%s_%s\n", prefix, label);
	fprintf(f, "%s_%s:\n", prefix, label);
	fprintf(f, "_%s_%s:\n", prefix, label);
}

static void emit_offset_label(FILE *f, const char *label, int offset)
{
	fprintf(f, "\t.globl\t%s\n", label);
	fprintf(f, "%s\t= . + %d\n", label, offset);
}

#define ASM_EMIT_BELONG(f, fmt, ...) \
	{ \
		fprintf((f), "\t.byte\t((" fmt ") >> 24) & 0xff\n", __VA_ARGS__); \
		fprintf((f), "\t.byte\t((" fmt ") >> 16) & 0xff\n", __VA_ARGS__); \
		fprintf((f), "\t.byte\t((" fmt ") >> 8) & 0xff\n", __VA_ARGS__); \
		fprintf((f), "\t.byte\t(" fmt ") & 0xff\n", __VA_ARGS__); \
	}

static void asm_emit_cell(void *e, cell_t val)
{
	FILE *f = e;

	fprintf(f, "\t.byte 0x%02x; .byte 0x%02x; .byte 0x%02x; .byte 0x%02x\n",
		(val >> 24) & 0xff, (val >> 16) & 0xff,
		(val >> 8) & 0xff, val & 0xff);
}

static void asm_emit_string(void *e, const char *str, int len)
{
	FILE *f = e;

	if (len != 0)
		fprintf(f, "\t.string\t\"%.*s\"\n", len, str);
	else
		fprintf(f, "\t.string\t\"%s\"\n", str);
}

static void asm_emit_align(void *e, int a)
{
	FILE *f = e;

	fprintf(f, "\t.balign\t%d, 0\n", a);
}

static void asm_emit_data(void *e, struct data d)
{
	FILE *f = e;
	int off = 0;
	struct marker *m = d.markers;

	for_each_marker_of_type(m, LABEL)
		emit_offset_label(f, m->ref, m->offset);

	while ((d.len - off) >= sizeof(uint32_t)) {
		asm_emit_cell(e, fdt32_to_cpu(*((fdt32_t *)(d.val+off))));
		off += sizeof(uint32_t);
	}

	while ((d.len - off) >= 1) {
		fprintf(f, "\t.byte\t0x%hhx\n", d.val[off]);
		off += 1;
	}

	assert(off == d.len);
}

static void asm_emit_beginnode(void *e, struct label *labels)
{
	FILE *f = e;
	struct label *l;

	for_each_label(labels, l) {
		fprintf(f, "\t.globl\t%s\n", l->label);
		fprintf(f, "%s:\n", l->label);
	}
	fprintf(f, "\t/* FDT_BEGIN_NODE */\n");
	asm_emit_cell(e, FDT_BEGIN_NODE);
}

static void asm_emit_endnode(void *e, struct label *labels)
{
	FILE *f = e;
	struct label *l;

	fprintf(f, "\t/* FDT_END_NODE */\n");
	asm_emit_cell(e, FDT_END_NODE);
	for_each_label(labels, l) {
		fprintf(f, "\t.globl\t%s_end\n", l->label);
		fprintf(f, "%s_end:\n", l->label);
	}
}

static void asm_emit_property(void *e, struct label *labels)
{
	FILE *f = e;
	struct label *l;

	for_each_label(labels, l) {
		fprintf(f, "\t.globl\t%s\n", l->label);
		fprintf(f, "%s:\n", l->label);
	}
	fprintf(f, "\t/* FDT_PROP */\n");
	asm_emit_cell(e, FDT_PROP);
}

static struct emitter asm_emitter = {
	.cell = asm_emit_cell,
	.string = asm_emit_string,
	.align = asm_emit_align,
	.data = asm_emit_data,
	.beginnode = asm_emit_beginnode,
	.endnode = asm_emit_endnode,
	.property = asm_emit_property,
};

static int stringtable_insert(struct data *d, const char *str)
{
	int i;

	/* FIXME: do this more efficiently? */

	for (i = 0; i < d->len; i++) {
		if (streq(str, d->val + i))
			return i;
	}

	*d = data_append_data(*d, str, strlen(str)+1);
	return i;
}

static void flatten_tree(struct node *tree, struct emitter *emit,
			 void *etarget, struct data *strbuf,
			 struct version_info *vi)
{
	struct property *prop;
	struct node *child;
	bool seen_name_prop = false;

	if (tree->deleted)
		return;

	emit->beginnode(etarget, tree->labels);

	if (vi->flags & FTF_FULLPATH)
		emit->string(etarget, tree->fullpath, 0);
	else
		emit->string(etarget, tree->name, 0);

	emit->align(etarget, sizeof(cell_t));

	for_each_property(tree, prop) {
		int nameoff;

		if (streq(prop->name, "name"))
			seen_name_prop = true;

		nameoff = stringtable_insert(strbuf, prop->name);

		emit->property(etarget, prop->labels);
		emit->cell(etarget, prop->val.len);
		emit->cell(etarget, nameoff);

		if ((vi->flags & FTF_VARALIGN) && (prop->val.len >= 8))
			emit->align(etarget, 8);

		emit->data(etarget, prop->val);
		emit->align(etarget, sizeof(cell_t));
	}

	if ((vi->flags & FTF_NAMEPROPS) && !seen_name_prop) {
		emit->property(etarget, NULL);
		emit->cell(etarget, tree->basenamelen+1);
		emit->cell(etarget, stringtable_insert(strbuf, "name"));

		if ((vi->flags & FTF_VARALIGN) && ((tree->basenamelen+1) >= 8))
			emit->align(etarget, 8);

		emit->string(etarget, tree->name, tree->basenamelen);
		emit->align(etarget, sizeof(cell_t));
	}

	for_each_child(tree, child) {
		flatten_tree(child, emit, etarget, strbuf, vi);
	}

	emit->endnode(etarget, tree->labels);
}

static struct data flatten_reserve_list(struct reserve_info *reservelist,
				 struct version_info *vi)
{
	struct reserve_info *re;
	struct data d = empty_data;
	int    j;

	for (re = reservelist; re; re = re->next) {
		d = data_append_re(d, re->address, re->size);
	}
	/*
	 * Add additional reserved slots if the user asked for them.
	 */
	for (j = 0; j < reservenum; j++) {
		d = data_append_re(d, 0, 0);
	}

	return d;
}

static void make_fdt_header(struct fdt_header *fdt,
			    struct version_info *vi,
			    int reservesize, int dtsize, int strsize,
			    int boot_cpuid_phys)
{
	int reserve_off;

	reservesize += sizeof(struct fdt_reserve_entry);

	memset(fdt, 0xff, sizeof(*fdt));

	fdt->magic = cpu_to_fdt32(FDT_MAGIC);
	fdt->version = cpu_to_fdt32(vi->version);
	fdt->last_comp_version = cpu_to_fdt32(vi->last_comp_version);

	/* Reserve map should be doubleword aligned */
	reserve_off = ALIGN(vi->hdr_size, 8);

	fdt->off_mem_rsvmap = cpu_to_fdt32(reserve_off);
	fdt->off_dt_struct = cpu_to_fdt32(reserve_off + reservesize);
	fdt->off_dt_strings = cpu_to_fdt32(reserve_off + reservesize
					  + dtsize);
	fdt->totalsize = cpu_to_fdt32(reserve_off + reservesize + dtsize + strsize);

	if (vi->flags & FTF_BOOTCPUID)
		fdt->boot_cpuid_phys = cpu_to_fdt32(boot_cpuid_phys);
	if (vi->flags & FTF_STRTABSIZE)
		fdt->size_dt_strings = cpu_to_fdt32(strsize);
	if (vi->flags & FTF_STRUCTSIZE)
		fdt->size_dt_struct = cpu_to_fdt32(dtsize);
}

void dt_to_blob(FILE *f, struct dt_info *dti, int version)
{
	struct version_info *vi = NULL;
	int i;
	struct data blob       = empty_data;
	struct data reservebuf = empty_data;
	struct data dtbuf      = empty_data;
	struct data strbuf     = empty_data;
	struct fdt_header fdt;
	int padlen = 0;

	for (i = 0; i < ARRAY_SIZE(version_table); i++) {
		if (version_table[i].version == version)
			vi = &version_table[i];
	}
	if (!vi)
		die("Unknown device tree blob version %d\n", version);

	flatten_tree(dti->dt, &bin_emitter, &dtbuf, &strbuf, vi);
	bin_emit_cell(&dtbuf, FDT_END);

	reservebuf = flatten_reserve_list(dti->reservelist, vi);

	/* Make header */
	make_fdt_header(&fdt, vi, reservebuf.len, dtbuf.len, strbuf.len,
			dti->boot_cpuid_phys);

	/*
	 * If the user asked for more space than is used, adjust the totalsize.
	 */
	if (minsize > 0) {
		padlen = minsize - fdt32_to_cpu(fdt.totalsize);
		if (padlen < 0) {
			padlen = 0;
			if (quiet < 1)
				fprintf(stderr,
					"Warning: blob size %"PRIu32" >= minimum size %d\n",
					fdt32_to_cpu(fdt.totalsize), minsize);
		}
	}

	if (padsize > 0)
		padlen = padsize;

	if (alignsize > 0)
		padlen = ALIGN(fdt32_to_cpu(fdt.totalsize) + padlen, alignsize)
			- fdt32_to_cpu(fdt.totalsize);

	if (padlen > 0) {
		int tsize = fdt32_to_cpu(fdt.totalsize);
		tsize += padlen;
		fdt.totalsize = cpu_to_fdt32(tsize);
	}

	/*
	 * Assemble the blob: start with the header, add with alignment
	 * the reserve buffer, add the reserve map terminating zeroes,
	 * the device tree itself, and finally the strings.
	 */
	blob = data_append_data(blob, &fdt, vi->hdr_size);
	blob = data_append_align(blob, 8);
	blob = data_merge(blob, reservebuf);
	blob = data_append_zeroes(blob, sizeof(struct fdt_reserve_entry));
	blob = data_merge(blob, dtbuf);
	blob = data_merge(blob, strbuf);

	/*
	 * If the user asked for more space than is used, pad out the blob.
	 */
	if (padlen > 0)
		blob = data_append_zeroes(blob, padlen);

	if (fwrite(blob.val, blob.len, 1, f) != 1) {
		if (ferror(f))
			die("Error writing device tree blob: %s\n",
			    strerror(errno));
		else
			die("Short write on device tree blob\n");
	}

	/*
	 * data_merge() frees the right-hand element so only the blob
	 * remains to be freed.
	 */
	data_free(blob);
}

static void dump_stringtable_asm(FILE *f, struct data strbuf)
{
	const char *p;
	int len;

	p = strbuf.val;

	while (p < (strbuf.val + strbuf.len)) {
		len = strlen(p);
		fprintf(f, "\t.string \"%s\"\n", p);
		p += len+1;
	}
}

void dt_to_asm(FILE *f, struct dt_info *dti, int version)
{
	struct version_info *vi = NULL;
	int i;
	struct data strbuf = empty_data;
	struct reserve_info *re;
	const char *symprefix = "dt";

	for (i = 0; i < ARRAY_SIZE(version_table); i++) {
		if (version_table[i].version == version)
			vi = &version_table[i];
	}
	if (!vi)
		die("Unknown device tree blob version %d\n", version);

	fprintf(f, "/* autogenerated by dtc, do not edit */\n\n");

	emit_label(f, symprefix, "blob_start");
	emit_label(f, symprefix, "header");
	fprintf(f, "\t/* magic */\n");
	asm_emit_cell(f, FDT_MAGIC);
	fprintf(f, "\t/* totalsize */\n");
	ASM_EMIT_BELONG(f, "_%s_blob_abs_end - _%s_blob_start",
			symprefix, symprefix);
	fprintf(f, "\t/* off_dt_struct */\n");
	ASM_EMIT_BELONG(f, "_%s_struct_start - _%s_blob_start",
		symprefix, symprefix);
	fprintf(f, "\t/* off_dt_strings */\n");
	ASM_EMIT_BELONG(f, "_%s_strings_start - _%s_blob_start",
		symprefix, symprefix);
	fprintf(f, "\t/* off_mem_rsvmap */\n");
	ASM_EMIT_BELONG(f, "_%s_reserve_map - _%s_blob_start",
		symprefix, symprefix);
	fprintf(f, "\t/* version */\n");
	asm_emit_cell(f, vi->version);
	fprintf(f, "\t/* last_comp_version */\n");
	asm_emit_cell(f, vi->last_comp_version);

	if (vi->flags & FTF_BOOTCPUID) {
		fprintf(f, "\t/* boot_cpuid_phys */\n");
		asm_emit_cell(f, dti->boot_cpuid_phys);
	}

	if (vi->flags & FTF_STRTABSIZE) {
		fprintf(f, "\t/* size_dt_strings */\n");
		ASM_EMIT_BELONG(f, "_%s_strings_end - _%s_strings_start",
				symprefix, symprefix);
	}

	if (vi->flags & FTF_STRUCTSIZE) {
		fprintf(f, "\t/* size_dt_struct */\n");
		ASM_EMIT_BELONG(f, "_%s_struct_end - _%s_struct_start",
			symprefix, symprefix);
	}

	/*
	 * Reserve map entries.
	 * Align the reserve map to a doubleword boundary.
	 * Each entry is an (address, size) pair of u64 values.
	 * Always supply a zero-sized temination entry.
	 */
	asm_emit_align(f, 8);
	emit_label(f, symprefix, "reserve_map");

	fprintf(f, "/* Memory reserve map from source file */\n");

	/*
	 * Use .long on high and low halfs of u64s to avoid .quad
	 * as it appears .quad isn't available in some assemblers.
	 */
	for (re = dti->reservelist; re; re = re->next) {
		struct label *l;

		for_each_label(re->labels, l) {
			fprintf(f, "\t.globl\t%s\n", l->label);
			fprintf(f, "%s:\n", l->label);
		}
		ASM_EMIT_BELONG(f, "0x%08x", (unsigned int)(re->address >> 32));
		ASM_EMIT_BELONG(f, "0x%08x",
				(unsigned int)(re->address & 0xffffffff));
		ASM_EMIT_BELONG(f, "0x%08x", (unsigned int)(re->size >> 32));
		ASM_EMIT_BELONG(f, "0x%08x", (unsigned int)(re->size & 0xffffffff));
	}
	for (i = 0; i < reservenum; i++) {
		fprintf(f, "\t.long\t0, 0\n\t.long\t0, 0\n");
	}

	fprintf(f, "\t.long\t0, 0\n\t.long\t0, 0\n");

	emit_label(f, symprefix, "struct_start");
	flatten_tree(dti->dt, &asm_emitter, f, &strbuf, vi);

	fprintf(f, "\t/* FDT_END */\n");
	asm_emit_cell(f, FDT_END);
	emit_label(f, symprefix, "struct_end");

	emit_label(f, symprefix, "strings_start");
	dump_stringtable_asm(f, strbuf);
	emit_label(f, symprefix, "strings_end");

	emit_label(f, symprefix, "blob_end");

	/*
	 * If the user asked for more space than is used, pad it out.
	 */
	if (minsize > 0) {
		fprintf(f, "\t.space\t%d - (_%s_blob_end - _%s_blob_start), 0\n",
			minsize, symprefix, symprefix);
	}
	if (padsize > 0) {
		fprintf(f, "\t.space\t%d, 0\n", padsize);
	}
	if (alignsize > 0)
		asm_emit_align(f, alignsize);
	emit_label(f, symprefix, "blob_abs_end");

	data_free(strbuf);
}

struct inbuf {
	char *base, *limit, *ptr;
};

static void inbuf_init(struct inbuf *inb, void *base, void *limit)
{
	inb->base = base;
	inb->limit = limit;
	inb->ptr = inb->base;
}

static void flat_read_chunk(struct inbuf *inb, void *p, int len)
{
	if ((inb->ptr + len) > inb->limit)
		die("Premature end of data parsing flat device tree\n");

	memcpy(p, inb->ptr, len);

	inb->ptr += len;
}

static uint32_t flat_read_word(struct inbuf *inb)
{
	fdt32_t val;

	assert(((inb->ptr - inb->base) % sizeof(val)) == 0);

	flat_read_chunk(inb, &val, sizeof(val));

	return fdt32_to_cpu(val);
}

static void flat_realign(struct inbuf *inb, int align)
{
	int off = inb->ptr - inb->base;

	inb->ptr = inb->base + ALIGN(off, align);
	if (inb->ptr > inb->limit)
		die("Premature end of data parsing flat device tree\n");
}

static char *flat_read_string(struct inbuf *inb)
{
	int len = 0;
	const char *p = inb->ptr;
	char *str;

	do {
		if (p >= inb->limit)
			die("Premature end of data parsing flat device tree\n");
		len++;
	} while ((*p++) != '\0');

	str = xstrdup(inb->ptr);

	inb->ptr += len;

	flat_realign(inb, sizeof(uint32_t));

	return str;
}

static struct data flat_read_data(struct inbuf *inb, int len)
{
	struct data d = empty_data;

	if (len == 0)
		return empty_data;

	d = data_grow_for(d, len);
	d.len = len;

	flat_read_chunk(inb, d.val, len);

	flat_realign(inb, sizeof(uint32_t));

	return d;
}

static char *flat_read_stringtable(struct inbuf *inb, int offset)
{
	const char *p;

	p = inb->base + offset;
	while (1) {
		if (p >= inb->limit || p < inb->base)
			die("String offset %d overruns string table\n",
			    offset);

		if (*p == '\0')
			break;

		p++;
	}

	return xstrdup(inb->base + offset);
}

static struct property *flat_read_property(struct inbuf *dtbuf,
					   struct inbuf *strbuf, int flags)
{
	uint32_t proplen, stroff;
	char *name;
	struct data val;

	proplen = flat_read_word(dtbuf);
	stroff = flat_read_word(dtbuf);

	name = flat_read_stringtable(strbuf, stroff);

	if ((flags & FTF_VARALIGN) && (proplen >= 8))
		flat_realign(dtbuf, 8);

	val = flat_read_data(dtbuf, proplen);

	return build_property(name, val, NULL);
}


static struct reserve_info *flat_read_mem_reserve(struct inbuf *inb)
{
	struct reserve_info *reservelist = NULL;
	struct reserve_info *new;
	struct fdt_reserve_entry re;

	/*
	 * Each entry is a pair of u64 (addr, size) values for 4 cell_t's.
	 * List terminates at an entry with size equal to zero.
	 *
	 * First pass, count entries.
	 */
	while (1) {
		uint64_t address, size;

		flat_read_chunk(inb, &re, sizeof(re));
		address  = fdt64_to_cpu(re.address);
		size = fdt64_to_cpu(re.size);
		if (size == 0)
			break;

		new = build_reserve_entry(address, size);
		reservelist = add_reserve_entry(reservelist, new);
	}

	return reservelist;
}


static char *nodename_from_path(const char *ppath, const char *cpath)
{
	int plen;

	plen = strlen(ppath);

	if (!strstarts(cpath, ppath))
		die("Path \"%s\" is not valid as a child of \"%s\"\n",
		    cpath, ppath);

	/* root node is a special case */
	if (!streq(ppath, "/"))
		plen++;

	return xstrdup(cpath + plen);
}

static struct node *unflatten_tree(struct inbuf *dtbuf,
				   struct inbuf *strbuf,
				   const char *parent_flatname, int flags)
{
	struct node *node;
	char *flatname;
	uint32_t val;

	node = build_node(NULL, NULL, NULL);

	flatname = flat_read_string(dtbuf);

	if (flags & FTF_FULLPATH)
		node->name = nodename_from_path(parent_flatname, flatname);
	else
		node->name = flatname;

	do {
		struct property *prop;
		struct node *child;

		val = flat_read_word(dtbuf);
		switch (val) {
		case FDT_PROP:
			if (node->children)
				fprintf(stderr, "Warning: Flat tree input has "
					"subnodes preceding a property.\n");
			prop = flat_read_property(dtbuf, strbuf, flags);
			add_property(node, prop);
			break;

		case FDT_BEGIN_NODE:
			child = unflatten_tree(dtbuf,strbuf, flatname, flags);
			add_child(node, child);
			break;

		case FDT_END_NODE:
			break;

		case FDT_END:
			die("Premature FDT_END in device tree blob\n");
			break;

		case FDT_NOP:
			if (!(flags & FTF_NOPS))
				fprintf(stderr, "Warning: NOP tag found in flat tree"
					" version <16\n");

			/* Ignore */
			break;

		default:
			die("Invalid opcode word %08x in device tree blob\n",
			    val);
		}
	} while (val != FDT_END_NODE);

	if (node->name != flatname) {
		free(flatname);
	}

	return node;
}


struct dt_info *dt_from_blob(const char *fname)
{
	FILE *f;
	fdt32_t magic_buf, totalsize_buf;
	uint32_t magic, totalsize, version, size_dt, boot_cpuid_phys;
	uint32_t off_dt, off_str, off_mem_rsvmap;
	int rc;
	char *blob;
	struct fdt_header *fdt;
	char *p;
	struct inbuf dtbuf, strbuf;
	struct inbuf memresvbuf;
	int sizeleft;
	struct reserve_info *reservelist;
	struct node *tree;
	uint32_t val;
	int flags = 0;

	f = srcfile_relative_open(fname, NULL);

	rc = fread(&magic_buf, sizeof(magic_buf), 1, f);
	if (ferror(f))
		die("Error reading DT blob magic number: %s\n",
		    strerror(errno));
	if (rc < 1) {
		if (feof(f))
			die("EOF reading DT blob magic number\n");
		else
			die("Mysterious short read reading magic number\n");
	}

	magic = fdt32_to_cpu(magic_buf);
	if (magic != FDT_MAGIC)
		die("Blob has incorrect magic number\n");

	rc = fread(&totalsize_buf, sizeof(totalsize_buf), 1, f);
	if (ferror(f))
		die("Error reading DT blob size: %s\n", strerror(errno));
	if (rc < 1) {
		if (feof(f))
			die("EOF reading DT blob size\n");
		else
			die("Mysterious short read reading blob size\n");
	}

	totalsize = fdt32_to_cpu(totalsize_buf);
	if (totalsize < FDT_V1_SIZE)
		die("DT blob size (%d) is too small\n", totalsize);

	blob = xmalloc(totalsize);

	fdt = (struct fdt_header *)blob;
	fdt->magic = cpu_to_fdt32(magic);
	fdt->totalsize = cpu_to_fdt32(totalsize);

	sizeleft = totalsize - sizeof(magic) - sizeof(totalsize);
	p = blob + sizeof(magic)  + sizeof(totalsize);

	while (sizeleft) {
		if (feof(f))
			die("EOF before reading %d bytes of DT blob\n",
			    totalsize);

		rc = fread(p, 1, sizeleft, f);
		if (ferror(f))
			die("Error reading DT blob: %s\n",
			    strerror(errno));

		sizeleft -= rc;
		p += rc;
	}

	off_dt = fdt32_to_cpu(fdt->off_dt_struct);
	off_str = fdt32_to_cpu(fdt->off_dt_strings);
	off_mem_rsvmap = fdt32_to_cpu(fdt->off_mem_rsvmap);
	version = fdt32_to_cpu(fdt->version);
	boot_cpuid_phys = fdt32_to_cpu(fdt->boot_cpuid_phys);

	if (off_mem_rsvmap >= totalsize)
		die("Mem Reserve structure offset exceeds total size\n");

	if (off_dt >= totalsize)
		die("DT structure offset exceeds total size\n");

	if (off_str > totalsize)
		die("String table offset exceeds total size\n");

	if (version >= 3) {
		uint32_t size_str = fdt32_to_cpu(fdt->size_dt_strings);
		if ((off_str+size_str < off_str) || (off_str+size_str > totalsize))
			die("String table extends past total size\n");
		inbuf_init(&strbuf, blob + off_str, blob + off_str + size_str);
	} else {
		inbuf_init(&strbuf, blob + off_str, blob + totalsize);
	}

	if (version >= 17) {
		size_dt = fdt32_to_cpu(fdt->size_dt_struct);
		if ((off_dt+size_dt < off_dt) || (off_dt+size_dt > totalsize))
			die("Structure block extends past total size\n");
	}

	if (version < 16) {
		flags |= FTF_FULLPATH | FTF_NAMEPROPS | FTF_VARALIGN;
	} else {
		flags |= FTF_NOPS;
	}

	inbuf_init(&memresvbuf,
		   blob + off_mem_rsvmap, blob + totalsize);
	inbuf_init(&dtbuf, blob + off_dt, blob + totalsize);

	reservelist = flat_read_mem_reserve(&memresvbuf);

	val = flat_read_word(&dtbuf);

	if (val != FDT_BEGIN_NODE)
		die("Device tree blob doesn't begin with FDT_BEGIN_NODE (begins with 0x%08x)\n", val);

	tree = unflatten_tree(&dtbuf, &strbuf, "", flags);

	val = flat_read_word(&dtbuf);
	if (val != FDT_END)
		die("Device tree blob doesn't end with FDT_END\n");

	free(blob);

	fclose(f);

	return build_dt_info(DTSF_V1, reservelist, tree, boot_cpuid_phys);
}
