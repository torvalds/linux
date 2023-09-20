// SPDX-License-Identifier: GPL-2.0-only
/*
 * genelf_debug.c
 * Copyright (C) 2015, Google, Inc
 *
 * Contributed by:
 * 	Stephane Eranian <eranian@google.com>
 *
 * based on GPLv2 source code from Oprofile
 * @remark Copyright 2007 OProfile authors
 * @author Philippe Elie
 */
#include <linux/compiler.h>
#include <linux/zalloc.h>
#include <sys/types.h>
#include <stdio.h>
#include <getopt.h>
#include <stddef.h>
#include <libelf.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
#include <fcntl.h>
#include <err.h>
#include <dwarf.h>

#include "genelf.h"
#include "../util/jitdump.h"

#define BUFFER_EXT_DFL_SIZE	(4 * 1024)

typedef uint32_t uword;
typedef uint16_t uhalf;
typedef int32_t  sword;
typedef int16_t  shalf;
typedef uint8_t  ubyte;
typedef int8_t   sbyte;

struct buffer_ext {
	size_t cur_pos;
	size_t max_sz;
	void *data;
};

static void
buffer_ext_dump(struct buffer_ext *be, const char *msg)
{
	size_t i;
	warnx("DUMP for %s", msg);
	for (i = 0 ; i < be->cur_pos; i++)
		warnx("%4zu 0x%02x", i, (((char *)be->data)[i]) & 0xff);
}

static inline int
buffer_ext_add(struct buffer_ext *be, void *addr, size_t sz)
{
	void *tmp;
	size_t be_sz = be->max_sz;

retry:
	if ((be->cur_pos + sz) < be_sz) {
		memcpy(be->data + be->cur_pos, addr, sz);
		be->cur_pos += sz;
		return 0;
	}

	if (!be_sz)
		be_sz = BUFFER_EXT_DFL_SIZE;
	else
		be_sz <<= 1;

	tmp = realloc(be->data, be_sz);
	if (!tmp)
		return -1;

	be->data   = tmp;
	be->max_sz = be_sz;

	goto retry;
}

static void
buffer_ext_init(struct buffer_ext *be)
{
	be->data = NULL;
	be->cur_pos = 0;
	be->max_sz = 0;
}

static void
buffer_ext_exit(struct buffer_ext *be)
{
	zfree(&be->data);
}

static inline size_t
buffer_ext_size(struct buffer_ext *be)
{
	return be->cur_pos;
}

static inline void *
buffer_ext_addr(struct buffer_ext *be)
{
	return be->data;
}

struct debug_line_header {
	// Not counting this field
	uword total_length;
	// version number (2 currently)
	uhalf version;
	// relative offset from next field to
	// program statement
	uword prolog_length;
	ubyte minimum_instruction_length;
	ubyte default_is_stmt;
	// line_base - see DWARF 2 specs
	sbyte line_base;
	// line_range - see DWARF 2 specs
	ubyte line_range;
	// number of opcode + 1
	ubyte opcode_base;
	/* follow the array of opcode args nr: ubytes [nr_opcode_base] */
	/* follow the search directories index, zero terminated string
	 * terminated by an empty string.
	 */
	/* follow an array of { filename, LEB128, LEB128, LEB128 }, first is
	 * the directory index entry, 0 means current directory, then mtime
	 * and filesize, last entry is followed by en empty string.
	 */
	/* follow the first program statement */
} __packed;

/* DWARF 2 spec talk only about one possible compilation unit header while
 * binutils can handle two flavours of dwarf 2, 32 and 64 bits, this is not
 * related to the used arch, an ELF 32 can hold more than 4 Go of debug
 * information. For now we handle only DWARF 2 32 bits comp unit. It'll only
 * become a problem if we generate more than 4GB of debug information.
 */
struct compilation_unit_header {
	uword total_length;
	uhalf version;
	uword debug_abbrev_offset;
	ubyte pointer_size;
} __packed;

#define DW_LNS_num_opcode (DW_LNS_set_isa + 1)

/* field filled at run time are marked with -1 */
static struct debug_line_header const default_debug_line_header = {
	.total_length = -1,
	.version = 2,
	.prolog_length = -1,
	.minimum_instruction_length = 1,	/* could be better when min instruction size != 1 */
	.default_is_stmt = 1,	/* we don't take care about basic block */
	.line_base = -5,	/* sensible value for line base ... */
	.line_range = -14,     /* ... and line range are guessed statically */
	.opcode_base = DW_LNS_num_opcode
};

static ubyte standard_opcode_length[] =
{
	0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1
};
#if 0
{
	[DW_LNS_advance_pc]   = 1,
	[DW_LNS_advance_line] = 1,
	[DW_LNS_set_file] =  1,
	[DW_LNS_set_column] = 1,
	[DW_LNS_fixed_advance_pc] = 1,
	[DW_LNS_set_isa] = 1,
};
#endif

/* field filled at run time are marked with -1 */
static struct compilation_unit_header default_comp_unit_header = {
	.total_length = -1,
	.version = 2,
	.debug_abbrev_offset = 0,     /* we reuse the same abbrev entries for all comp unit */
	.pointer_size = sizeof(void *)
};

static void emit_uword(struct buffer_ext *be, uword data)
{
	buffer_ext_add(be, &data, sizeof(uword));
}

static void emit_string(struct buffer_ext *be, const char *s)
{
	buffer_ext_add(be, (void *)s, strlen(s) + 1);
}

static void emit_unsigned_LEB128(struct buffer_ext *be,
				 unsigned long data)
{
	do {
		ubyte cur = data & 0x7F;
		data >>= 7;
		if (data)
			cur |= 0x80;
		buffer_ext_add(be, &cur, 1);
	} while (data);
}

static void emit_signed_LEB128(struct buffer_ext *be, long data)
{
	int more = 1;
	int negative = data < 0;
	int size = sizeof(long) * CHAR_BIT;
	while (more) {
		ubyte cur = data & 0x7F;
		data >>= 7;
		if (negative)
			data |= - (1 << (size - 7));
		if ((data == 0 && !(cur & 0x40)) ||
		    (data == -1l && (cur & 0x40)))
			more = 0;
		else
			cur |= 0x80;
		buffer_ext_add(be, &cur, 1);
	}
}

static void emit_extended_opcode(struct buffer_ext *be, ubyte opcode,
				 void *data, size_t data_len)
{
	buffer_ext_add(be, (char *)"", 1);

	emit_unsigned_LEB128(be, data_len + 1);

	buffer_ext_add(be, &opcode, 1);
	buffer_ext_add(be, data, data_len);
}

static void emit_opcode(struct buffer_ext *be, ubyte opcode)
{
	buffer_ext_add(be, &opcode, 1);
}

static void emit_opcode_signed(struct buffer_ext  *be,
			       ubyte opcode, long data)
{
	buffer_ext_add(be, &opcode, 1);
	emit_signed_LEB128(be, data);
}

static void emit_opcode_unsigned(struct buffer_ext *be, ubyte opcode,
				 unsigned long data)
{
	buffer_ext_add(be, &opcode, 1);
	emit_unsigned_LEB128(be, data);
}

static void emit_advance_pc(struct buffer_ext *be, unsigned long delta_pc)
{
	emit_opcode_unsigned(be, DW_LNS_advance_pc, delta_pc);
}

static void emit_advance_lineno(struct buffer_ext  *be, long delta_lineno)
{
	emit_opcode_signed(be, DW_LNS_advance_line, delta_lineno);
}

static void emit_lne_end_of_sequence(struct buffer_ext *be)
{
	emit_extended_opcode(be, DW_LNE_end_sequence, NULL, 0);
}

static void emit_set_file(struct buffer_ext *be, unsigned long idx)
{
	emit_opcode_unsigned(be, DW_LNS_set_file, idx);
}

static void emit_lne_define_filename(struct buffer_ext *be,
				     const char *filename)
{
	buffer_ext_add(be, (void *)"", 1);

	/* LNE field, strlen(filename) + zero termination, 3 bytes for: the dir entry, timestamp, filesize */
	emit_unsigned_LEB128(be, strlen(filename) + 5);
	emit_opcode(be, DW_LNE_define_file);
	emit_string(be, filename);
	/* directory index 0=do not know */
        emit_unsigned_LEB128(be, 0);
	/* last modification date on file 0=do not know */
        emit_unsigned_LEB128(be, 0);
	/* filesize 0=do not know */
        emit_unsigned_LEB128(be, 0);
}

static void emit_lne_set_address(struct buffer_ext *be,
				 void *address)
{
	emit_extended_opcode(be, DW_LNE_set_address, &address, sizeof(unsigned long));
}

static ubyte get_special_opcode(struct debug_entry *ent,
				unsigned int last_line,
				unsigned long last_vma)
{
	unsigned int temp;
	unsigned long delta_addr;

	/*
	 * delta from line_base
	 */
	temp = (ent->lineno - last_line) - default_debug_line_header.line_base;

	if (temp >= default_debug_line_header.line_range)
		return 0;

	/*
	 * delta of addresses
	 */
	delta_addr = (ent->addr - last_vma) / default_debug_line_header.minimum_instruction_length;

	/* This is not sufficient to ensure opcode will be in [0-256] but
	 * sufficient to ensure when summing with the delta lineno we will
	 * not overflow the unsigned long opcode */

	if (delta_addr <= 256 / default_debug_line_header.line_range) {
		unsigned long opcode = temp +
			(delta_addr * default_debug_line_header.line_range) +
			default_debug_line_header.opcode_base;

		return opcode <= 255 ? opcode : 0;
	}
	return 0;
}

static void emit_lineno_info(struct buffer_ext *be,
			     struct debug_entry *ent, size_t nr_entry,
			     unsigned long code_addr)
{
	size_t i;

	/* as described in the jitdump format */
	const char repeated_name_marker[] = {'\xff', '\0'};

	/*
	 * Machine state at start of a statement program
	 * address = 0
	 * file    = 1
	 * line    = 1
	 * column  = 0
	 * is_stmt = default_is_stmt as given in the debug_line_header
	 * basic block = 0
	 * end sequence = 0
	 */

	/* start state of the state machine we take care of */
	unsigned long last_vma = 0;
	char const  *cur_filename = NULL;
	unsigned long cur_file_idx = 0;
	int last_line = 1;

	emit_lne_set_address(be, (void *)code_addr);

	for (i = 0; i < nr_entry; i++, ent = debug_entry_next(ent)) {
		int need_copy = 0;
		ubyte special_opcode;

		/*
		 * check if filename changed, if so add it
		 */
		if ((!cur_filename || strcmp(cur_filename, ent->name)) &&
			strcmp(repeated_name_marker, ent->name)) {
			emit_lne_define_filename(be, ent->name);
			cur_filename = ent->name;
			emit_set_file(be, ++cur_file_idx);
			need_copy = 1;
		}

		special_opcode = get_special_opcode(ent, last_line, last_vma);
		if (special_opcode != 0) {
			last_line = ent->lineno;
			last_vma  = ent->addr;
			emit_opcode(be, special_opcode);
		} else {
			/*
			 * lines differ, emit line delta
			 */
			if (last_line != ent->lineno) {
				emit_advance_lineno(be, ent->lineno - last_line);
				last_line = ent->lineno;
				need_copy = 1;
			}
			/*
			 * addresses differ, emit address delta
			 */
			if (last_vma != ent->addr) {
				emit_advance_pc(be, ent->addr - last_vma);
				last_vma = ent->addr;
				need_copy = 1;
			}
			/*
			 * add new row to matrix
			 */
			if (need_copy)
				emit_opcode(be, DW_LNS_copy);
		}
	}
}

static void add_debug_line(struct buffer_ext *be,
	struct debug_entry *ent, size_t nr_entry,
	unsigned long code_addr)
{
	struct debug_line_header * dbg_header;
	size_t old_size;

	old_size = buffer_ext_size(be);

	buffer_ext_add(be, (void *)&default_debug_line_header,
		 sizeof(default_debug_line_header));

	buffer_ext_add(be, &standard_opcode_length,  sizeof(standard_opcode_length));

	// empty directory entry
	buffer_ext_add(be, (void *)"", 1);

	// empty filename directory
	buffer_ext_add(be, (void *)"", 1);

	dbg_header = buffer_ext_addr(be) + old_size;
	dbg_header->prolog_length = (buffer_ext_size(be) - old_size) -
		offsetof(struct debug_line_header, minimum_instruction_length);

	emit_lineno_info(be, ent, nr_entry, code_addr);

	emit_lne_end_of_sequence(be);

	dbg_header = buffer_ext_addr(be) + old_size;
	dbg_header->total_length = (buffer_ext_size(be) - old_size) -
		offsetof(struct debug_line_header, version);
}

static void
add_debug_abbrev(struct buffer_ext *be)
{
        emit_unsigned_LEB128(be, 1);
        emit_unsigned_LEB128(be, DW_TAG_compile_unit);
        emit_unsigned_LEB128(be, DW_CHILDREN_yes);
        emit_unsigned_LEB128(be, DW_AT_stmt_list);
        emit_unsigned_LEB128(be, DW_FORM_data4);
        emit_unsigned_LEB128(be, 0);
        emit_unsigned_LEB128(be, 0);
        emit_unsigned_LEB128(be, 0);
}

static void
add_compilation_unit(struct buffer_ext *be,
		     size_t offset_debug_line)
{
	struct compilation_unit_header *comp_unit_header;
	size_t old_size = buffer_ext_size(be);

	buffer_ext_add(be, &default_comp_unit_header,
		       sizeof(default_comp_unit_header));

	emit_unsigned_LEB128(be, 1);
	emit_uword(be, offset_debug_line);

	comp_unit_header = buffer_ext_addr(be) + old_size;
	comp_unit_header->total_length = (buffer_ext_size(be) - old_size) -
		offsetof(struct compilation_unit_header, version);
}

static int
jit_process_debug_info(uint64_t code_addr,
		       void *debug, int nr_debug_entries,
		       struct buffer_ext *dl,
		       struct buffer_ext *da,
		       struct buffer_ext *di)
{
	struct debug_entry *ent = debug;
	int i;

	for (i = 0; i < nr_debug_entries; i++) {
		ent->addr = ent->addr - code_addr;
		ent = debug_entry_next(ent);
	}
	add_compilation_unit(di, buffer_ext_size(dl));
	add_debug_line(dl, debug, nr_debug_entries, GEN_ELF_TEXT_OFFSET);
	add_debug_abbrev(da);
	if (0) buffer_ext_dump(da, "abbrev");

	return 0;
}

int
jit_add_debug_info(Elf *e, uint64_t code_addr, void *debug, int nr_debug_entries)
{
	Elf_Data *d;
	Elf_Scn *scn;
	Elf_Shdr *shdr;
	struct buffer_ext dl, di, da;
	int ret = -1;

	buffer_ext_init(&dl);
	buffer_ext_init(&di);
	buffer_ext_init(&da);

	if (jit_process_debug_info(code_addr, debug, nr_debug_entries, &dl, &da, &di))
		goto out;

	/*
	 * setup .debug_line section
	 */
	scn = elf_newscn(e);
	if (!scn) {
		warnx("cannot create section");
		goto out;
	}

	d = elf_newdata(scn);
	if (!d) {
		warnx("cannot get new data");
		goto out;
	}

	d->d_align = 1;
	d->d_off = 0LL;
	d->d_buf = buffer_ext_addr(&dl);
	d->d_type = ELF_T_BYTE;
	d->d_size = buffer_ext_size(&dl);
	d->d_version = EV_CURRENT;

	shdr = elf_getshdr(scn);
	if (!shdr) {
		warnx("cannot get section header");
		goto out;
	}

	shdr->sh_name = 52; /* .debug_line */
	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_addr = 0; /* must be zero or == sh_offset -> dynamic object */
	shdr->sh_flags = 0;
	shdr->sh_entsize = 0;

	/*
	 * setup .debug_info section
	 */
	scn = elf_newscn(e);
	if (!scn) {
		warnx("cannot create section");
		goto out;
	}

	d = elf_newdata(scn);
	if (!d) {
		warnx("cannot get new data");
		goto out;
	}

	d->d_align = 1;
	d->d_off = 0LL;
	d->d_buf = buffer_ext_addr(&di);
	d->d_type = ELF_T_BYTE;
	d->d_size = buffer_ext_size(&di);
	d->d_version = EV_CURRENT;

	shdr = elf_getshdr(scn);
	if (!shdr) {
		warnx("cannot get section header");
		goto out;
	}

	shdr->sh_name = 64; /* .debug_info */
	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_addr = 0; /* must be zero or == sh_offset -> dynamic object */
	shdr->sh_flags = 0;
	shdr->sh_entsize = 0;

	/*
	 * setup .debug_abbrev section
	 */
	scn = elf_newscn(e);
	if (!scn) {
		warnx("cannot create section");
		goto out;
	}

	d = elf_newdata(scn);
	if (!d) {
		warnx("cannot get new data");
		goto out;
	}

	d->d_align = 1;
	d->d_off = 0LL;
	d->d_buf = buffer_ext_addr(&da);
	d->d_type = ELF_T_BYTE;
	d->d_size = buffer_ext_size(&da);
	d->d_version = EV_CURRENT;

	shdr = elf_getshdr(scn);
	if (!shdr) {
		warnx("cannot get section header");
		goto out;
	}

	shdr->sh_name = 76; /* .debug_info */
	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_addr = 0; /* must be zero or == sh_offset -> dynamic object */
	shdr->sh_flags = 0;
	shdr->sh_entsize = 0;

	/*
	 * now we update the ELF image with all the sections
	 */
	if (elf_update(e, ELF_C_WRITE) < 0)
		warnx("elf_update debug failed");
	else
		ret = 0;

out:
	buffer_ext_exit(&dl);
	buffer_ext_exit(&di);
	buffer_ext_exit(&da);
	return ret;
}
