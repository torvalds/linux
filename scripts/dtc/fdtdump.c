// SPDX-License-Identifier: GPL-2.0
/*
 * fdtdump.c - Contributed by Pantelis Antoniou <pantelis.antoniou AT gmail.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <fdt.h>
#include <libfdt_env.h>

#include "util.h"

#define ALIGN(x, a)	(((x) + ((a) - 1)) & ~((a) - 1))
#define PALIGN(p, a)	((void *)(ALIGN((unsigned long)(p), (a))))
#define GET_CELL(p)	(p += 4, *((const uint32_t *)(p-4)))

static void print_data(const char *data, int len)
{
	int i;
	const char *p = data;

	/* no data, don't print */
	if (len == 0)
		return;

	if (util_is_printable_string(data, len)) {
		printf(" = \"%s\"", (const char *)data);
	} else if ((len % 4) == 0) {
		printf(" = <");
		for (i = 0; i < len; i += 4)
			printf("0x%08x%s", fdt32_to_cpu(GET_CELL(p)),
			       i < (len - 4) ? " " : "");
		printf(">");
	} else {
		printf(" = [");
		for (i = 0; i < len; i++)
			printf("%02x%s", *p++, i < len - 1 ? " " : "");
		printf("]");
	}
}

static void dump_blob(void *blob)
{
	struct fdt_header *bph = blob;
	uint32_t off_mem_rsvmap = fdt32_to_cpu(bph->off_mem_rsvmap);
	uint32_t off_dt = fdt32_to_cpu(bph->off_dt_struct);
	uint32_t off_str = fdt32_to_cpu(bph->off_dt_strings);
	struct fdt_reserve_entry *p_rsvmap =
		(struct fdt_reserve_entry *)((char *)blob + off_mem_rsvmap);
	const char *p_struct = (const char *)blob + off_dt;
	const char *p_strings = (const char *)blob + off_str;
	uint32_t version = fdt32_to_cpu(bph->version);
	uint32_t totalsize = fdt32_to_cpu(bph->totalsize);
	uint32_t tag;
	const char *p, *s, *t;
	int depth, sz, shift;
	int i;
	uint64_t addr, size;

	depth = 0;
	shift = 4;

	printf("/dts-v1/;\n");
	printf("// magic:\t\t0x%x\n", fdt32_to_cpu(bph->magic));
	printf("// totalsize:\t\t0x%x (%d)\n", totalsize, totalsize);
	printf("// off_dt_struct:\t0x%x\n", off_dt);
	printf("// off_dt_strings:\t0x%x\n", off_str);
	printf("// off_mem_rsvmap:\t0x%x\n", off_mem_rsvmap);
	printf("// version:\t\t%d\n", version);
	printf("// last_comp_version:\t%d\n",
	       fdt32_to_cpu(bph->last_comp_version));
	if (version >= 2)
		printf("// boot_cpuid_phys:\t0x%x\n",
		       fdt32_to_cpu(bph->boot_cpuid_phys));

	if (version >= 3)
		printf("// size_dt_strings:\t0x%x\n",
		       fdt32_to_cpu(bph->size_dt_strings));
	if (version >= 17)
		printf("// size_dt_struct:\t0x%x\n",
		       fdt32_to_cpu(bph->size_dt_struct));
	printf("\n");

	for (i = 0; ; i++) {
		addr = fdt64_to_cpu(p_rsvmap[i].address);
		size = fdt64_to_cpu(p_rsvmap[i].size);
		if (addr == 0 && size == 0)
			break;

		printf("/memreserve/ %llx %llx;\n",
		       (unsigned long long)addr, (unsigned long long)size);
	}

	p = p_struct;
	while ((tag = fdt32_to_cpu(GET_CELL(p))) != FDT_END) {

		/* printf("tag: 0x%08x (%d)\n", tag, p - p_struct); */

		if (tag == FDT_BEGIN_NODE) {
			s = p;
			p = PALIGN(p + strlen(s) + 1, 4);

			if (*s == '\0')
				s = "/";

			printf("%*s%s {\n", depth * shift, "", s);

			depth++;
			continue;
		}

		if (tag == FDT_END_NODE) {
			depth--;

			printf("%*s};\n", depth * shift, "");
			continue;
		}

		if (tag == FDT_NOP) {
			printf("%*s// [NOP]\n", depth * shift, "");
			continue;
		}

		if (tag != FDT_PROP) {
			fprintf(stderr, "%*s ** Unknown tag 0x%08x\n", depth * shift, "", tag);
			break;
		}
		sz = fdt32_to_cpu(GET_CELL(p));
		s = p_strings + fdt32_to_cpu(GET_CELL(p));
		if (version < 16 && sz >= 8)
			p = PALIGN(p, 8);
		t = p;

		p = PALIGN(p + sz, 4);

		printf("%*s%s", depth * shift, "", s);
		print_data(t, sz);
		printf(";\n");
	}
}


int main(int argc, char *argv[])
{
	char *buf;

	if (argc < 2) {
		fprintf(stderr, "supply input filename\n");
		return 5;
	}

	buf = utilfdt_read(argv[1]);
	if (buf)
		dump_blob(buf);
	else
		return 10;

	return 0;
}
