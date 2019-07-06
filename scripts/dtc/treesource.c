// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 */

#include "dtc.h"
#include "srcpos.h"

extern FILE *yyin;
extern int yyparse(void);
extern YYLTYPE yylloc;

struct dt_info *parser_output;
bool treesource_error;

struct dt_info *dt_from_source(const char *fname)
{
	parser_output = NULL;
	treesource_error = false;

	srcfile_push(fname);
	yyin = current_srcfile->f;
	yylloc.file = current_srcfile;

	if (yyparse() != 0)
		die("Unable to parse input tree\n");

	if (treesource_error)
		die("Syntax error parsing input tree\n");

	return parser_output;
}

static void write_prefix(FILE *f, int level)
{
	int i;

	for (i = 0; i < level; i++)
		fputc('\t', f);
}

static bool isstring(char c)
{
	return (isprint((unsigned char)c)
		|| (c == '\0')
		|| strchr("\a\b\t\n\v\f\r", c));
}

static void write_propval_string(FILE *f, const char *s, size_t len)
{
	const char *end = s + len - 1;

	if (!len)
		return;

	assert(*end == '\0');

	fprintf(f, "\"");
	while (s < end) {
		char c = *s++;
		switch (c) {
		case '\a':
			fprintf(f, "\\a");
			break;
		case '\b':
			fprintf(f, "\\b");
			break;
		case '\t':
			fprintf(f, "\\t");
			break;
		case '\n':
			fprintf(f, "\\n");
			break;
		case '\v':
			fprintf(f, "\\v");
			break;
		case '\f':
			fprintf(f, "\\f");
			break;
		case '\r':
			fprintf(f, "\\r");
			break;
		case '\\':
			fprintf(f, "\\\\");
			break;
		case '\"':
			fprintf(f, "\\\"");
			break;
		case '\0':
			fprintf(f, "\\0");
			break;
		default:
			if (isprint((unsigned char)c))
				fprintf(f, "%c", c);
			else
				fprintf(f, "\\x%02"PRIx8, c);
		}
	}
	fprintf(f, "\"");
}

static void write_propval_int(FILE *f, const char *p, size_t len, size_t width)
{
	const char *end = p + len;
	assert(len % width == 0);

	for (; p < end; p += width) {
		switch (width) {
		case 1:
			fprintf(f, "%02"PRIx8, *(const uint8_t*)p);
			break;
		case 2:
			fprintf(f, "0x%02"PRIx16, fdt16_to_cpu(*(const fdt16_t*)p));
			break;
		case 4:
			fprintf(f, "0x%02"PRIx32, fdt32_to_cpu(*(const fdt32_t*)p));
			break;
		case 8:
			fprintf(f, "0x%02"PRIx64, fdt64_to_cpu(*(const fdt64_t*)p));
			break;
		}
		if (p + width < end)
			fputc(' ', f);
	}
}

static bool has_data_type_information(struct marker *m)
{
	return m->type >= TYPE_UINT8;
}

static struct marker *next_type_marker(struct marker *m)
{
	while (m && !has_data_type_information(m))
		m = m->next;
	return m;
}

size_t type_marker_length(struct marker *m)
{
	struct marker *next = next_type_marker(m->next);

	if (next)
		return next->offset - m->offset;
	return 0;
}

static const char *delim_start[] = {
	[TYPE_UINT8] = "[",
	[TYPE_UINT16] = "/bits/ 16 <",
	[TYPE_UINT32] = "<",
	[TYPE_UINT64] = "/bits/ 64 <",
	[TYPE_STRING] = "",
};
static const char *delim_end[] = {
	[TYPE_UINT8] = "]",
	[TYPE_UINT16] = ">",
	[TYPE_UINT32] = ">",
	[TYPE_UINT64] = ">",
	[TYPE_STRING] = "",
};

static enum markertype guess_value_type(struct property *prop)
{
	int len = prop->val.len;
	const char *p = prop->val.val;
	struct marker *m = prop->val.markers;
	int nnotstring = 0, nnul = 0;
	int nnotstringlbl = 0, nnotcelllbl = 0;
	int i;

	for (i = 0; i < len; i++) {
		if (! isstring(p[i]))
			nnotstring++;
		if (p[i] == '\0')
			nnul++;
	}

	for_each_marker_of_type(m, LABEL) {
		if ((m->offset > 0) && (prop->val.val[m->offset - 1] != '\0'))
			nnotstringlbl++;
		if ((m->offset % sizeof(cell_t)) != 0)
			nnotcelllbl++;
	}

	if ((p[len-1] == '\0') && (nnotstring == 0) && (nnul < (len-nnul))
	    && (nnotstringlbl == 0)) {
		return TYPE_STRING;
	} else if (((len % sizeof(cell_t)) == 0) && (nnotcelllbl == 0)) {
		return TYPE_UINT32;
	}

	return TYPE_UINT8;
}

static void write_propval(FILE *f, struct property *prop)
{
	size_t len = prop->val.len;
	struct marker *m = prop->val.markers;
	struct marker dummy_marker;
	enum markertype emit_type = TYPE_NONE;
	char *srcstr;

	if (len == 0) {
		fprintf(f, ";");
		if (annotate) {
			srcstr = srcpos_string_first(prop->srcpos, annotate);
			if (srcstr) {
				fprintf(f, " /* %s */", srcstr);
				free(srcstr);
			}
		}
		fprintf(f, "\n");
		return;
	}

	fprintf(f, " =");

	if (!next_type_marker(m)) {
		/* data type information missing, need to guess */
		dummy_marker.type = guess_value_type(prop);
		dummy_marker.next = prop->val.markers;
		dummy_marker.offset = 0;
		dummy_marker.ref = NULL;
		m = &dummy_marker;
	}

	for_each_marker(m) {
		size_t chunk_len = (m->next ? m->next->offset : len) - m->offset;
		size_t data_len = type_marker_length(m) ? : len - m->offset;
		const char *p = &prop->val.val[m->offset];

		if (has_data_type_information(m)) {
			emit_type = m->type;
			fprintf(f, " %s", delim_start[emit_type]);
		} else if (m->type == LABEL)
			fprintf(f, " %s:", m->ref);
		else if (m->offset)
			fputc(' ', f);

		if (emit_type == TYPE_NONE) {
			assert(chunk_len == 0);
			continue;
		}

		switch(emit_type) {
		case TYPE_UINT16:
			write_propval_int(f, p, chunk_len, 2);
			break;
		case TYPE_UINT32:
			write_propval_int(f, p, chunk_len, 4);
			break;
		case TYPE_UINT64:
			write_propval_int(f, p, chunk_len, 8);
			break;
		case TYPE_STRING:
			write_propval_string(f, p, chunk_len);
			break;
		default:
			write_propval_int(f, p, chunk_len, 1);
		}

		if (chunk_len == data_len) {
			size_t pos = m->offset + chunk_len;
			fprintf(f, pos == len ? "%s" : "%s,",
			        delim_end[emit_type] ? : "");
			emit_type = TYPE_NONE;
		}
	}
	fprintf(f, ";");
	if (annotate) {
		srcstr = srcpos_string_first(prop->srcpos, annotate);
		if (srcstr) {
			fprintf(f, " /* %s */", srcstr);
			free(srcstr);
		}
	}
	fprintf(f, "\n");
}

static void write_tree_source_node(FILE *f, struct node *tree, int level)
{
	struct property *prop;
	struct node *child;
	struct label *l;
	char *srcstr;

	write_prefix(f, level);
	for_each_label(tree->labels, l)
		fprintf(f, "%s: ", l->label);
	if (tree->name && (*tree->name))
		fprintf(f, "%s {", tree->name);
	else
		fprintf(f, "/ {");

	if (annotate) {
		srcstr = srcpos_string_first(tree->srcpos, annotate);
		if (srcstr) {
			fprintf(f, " /* %s */", srcstr);
			free(srcstr);
		}
	}
	fprintf(f, "\n");

	for_each_property(tree, prop) {
		write_prefix(f, level+1);
		for_each_label(prop->labels, l)
			fprintf(f, "%s: ", l->label);
		fprintf(f, "%s", prop->name);
		write_propval(f, prop);
	}
	for_each_child(tree, child) {
		fprintf(f, "\n");
		write_tree_source_node(f, child, level+1);
	}
	write_prefix(f, level);
	fprintf(f, "};");
	if (annotate) {
		srcstr = srcpos_string_last(tree->srcpos, annotate);
		if (srcstr) {
			fprintf(f, " /* %s */", srcstr);
			free(srcstr);
		}
	}
	fprintf(f, "\n");
}

void dt_to_source(FILE *f, struct dt_info *dti)
{
	struct reserve_info *re;

	fprintf(f, "/dts-v1/;\n\n");

	for (re = dti->reservelist; re; re = re->next) {
		struct label *l;

		for_each_label(re->labels, l)
			fprintf(f, "%s: ", l->label);
		fprintf(f, "/memreserve/\t0x%016llx 0x%016llx;\n",
			(unsigned long long)re->address,
			(unsigned long long)re->size);
	}

	write_tree_source_node(f, dti->dt, 0);
}
