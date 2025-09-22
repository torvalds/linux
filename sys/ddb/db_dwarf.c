/*	$OpenBSD: db_dwarf.c,v 1.7 2017/10/27 08:40:15 mpi Exp $	 */
/*
 * Copyright (c) 2014 Matthew Dempsky <matthew@dempsky.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <machine/db_machdep.h>
#include <ddb/db_sym.h>
#ifdef DIAGNOSTIC
#define DWARN(fmt, ...) printf("ddb: " fmt "\n", __VA_ARGS__)
#else
#define DWARN(fmt, ...) ((void)0)
#endif
#else /* _KERNEL */
#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define DWARN warnx
#endif /* _KERNEL */

enum {
	DW_LNS_copy			= 1,
	DW_LNS_advance_pc		= 2,
	DW_LNS_advance_line		= 3,
	DW_LNS_set_file			= 4,
	DW_LNS_set_column		= 5,
	DW_LNS_negate_stmt		= 6,
	DW_LNS_set_basic_block		= 7,
	DW_LNS_const_add_pc		= 8,
	DW_LNS_fixed_advance_pc		= 9,
	DW_LNS_set_prologue_end		= 10,
	DW_LNS_set_epilogue_begin	= 11,
};

enum {
	DW_LNE_end_sequence		= 1,
	DW_LNE_set_address		= 2,
	DW_LNE_define_file		= 3,
};

struct dwbuf {
	const char *buf;
	size_t len;
};

static inline bool
read_bytes(struct dwbuf *d, void *v, size_t n)
{
	if (d->len < n)
		return (false);
	memcpy(v, d->buf, n);
	d->buf += n;
	d->len -= n;
	return (true);
}

static bool
read_s8(struct dwbuf *d, int8_t *v)
{
	return (read_bytes(d, v, sizeof(*v)));
}

static bool
read_u8(struct dwbuf *d, uint8_t *v)
{
	return (read_bytes(d, v, sizeof(*v)));
}

static bool
read_u16(struct dwbuf *d, uint16_t *v)
{
	return (read_bytes(d, v, sizeof(*v)));
}

static bool
read_u32(struct dwbuf *d, uint32_t *v)
{
	return (read_bytes(d, v, sizeof(*v)));
}

static bool
read_u64(struct dwbuf *d, uint64_t *v)
{
	return (read_bytes(d, v, sizeof(*v)));
}

/* Read a DWARF LEB128 (little-endian base-128) value. */
static bool
read_leb128(struct dwbuf *d, uint64_t *v, bool signextend)
{
	unsigned int shift = 0;
	uint64_t res = 0;
	uint8_t x;
	while (shift < 64 && read_u8(d, &x)) {
		res |= (uint64_t)(x & 0x7f) << shift;
		shift += 7;
		if ((x & 0x80) == 0) {
			if (signextend && shift < 64 && (x & 0x40) != 0)
				res |= ~(uint64_t)0 << shift;
			*v = res;
			return (true);
		}
	}
	return (false);
}

static bool
read_sleb128(struct dwbuf *d, int64_t *v)
{
	return (read_leb128(d, (uint64_t *)v, true));
}

static bool
read_uleb128(struct dwbuf *d, uint64_t *v)
{
	return (read_leb128(d, v, false));
}

/* Read a NUL terminated string. */
static bool
read_string(struct dwbuf *d, const char **s)
{
	const char *end = memchr(d->buf, '\0', d->len);
	if (end == NULL)
		return (false);
	size_t n = end - d->buf + 1;
	*s = d->buf;
	d->buf += n;
	d->len -= n;
	return (true);
}

static bool
read_buf(struct dwbuf *d, struct dwbuf *v, size_t n)
{
	if (d->len < n)
		return (false);
	v->buf = d->buf;
	v->len = n;
	d->buf += n;
	d->len -= n;
	return (true);
}

static bool
skip_bytes(struct dwbuf *d, size_t n)
{
	if (d->len < n)
		return (false);
	d->buf += n;
	d->len -= n;
	return (true);
}

static bool
read_filename(struct dwbuf *names, const char **outdirname,
    const char **outbasename, uint8_t opcode_base, uint64_t file)
{
	if (file == 0)
		return (false);

	/* Skip over opcode table. */
	size_t i;
	for (i = 1; i < opcode_base; i++) {
		uint64_t dummy;
		if (!read_uleb128(names, &dummy))
			return (false);
	}

	/* Skip over directory name table for now. */
	struct dwbuf dirnames = *names;
	for (;;) {
		const char *name;
		if (!read_string(names, &name))
			return (false);
		if (*name == '\0')
			break;
	}

	/* Locate file entry. */
	const char *basename = NULL;
	uint64_t dir = 0;
	for (i = 0; i < file; i++) {
		uint64_t mtime, size;
		if (!read_string(names, &basename) || *basename == '\0' ||
		    !read_uleb128(names, &dir) ||
		    !read_uleb128(names, &mtime) ||
		    !read_uleb128(names, &size))
			return (false);
	}

	const char *dirname = NULL;
	for (i = 0; i < dir; i++) {
		if (!read_string(&dirnames, &dirname) || *dirname == '\0')
			return (false);
	}

	*outdirname = dirname;
	*outbasename = basename;
	return (true);
}

bool
db_dwarf_line_at_pc(const char *linetab, size_t linetabsize, uintptr_t pc,
    const char **outdirname, const char **outbasename, int *outline)
{
	struct dwbuf table = { .buf = linetab, .len = linetabsize };

	/*
	 * For simplicity, we simply brute force search through the entire
	 * line table each time.
	 */
	uint32_t unitsize;
	struct dwbuf unit;
next:
	/* Line tables are a sequence of compilation unit entries. */
	if (!read_u32(&table, &unitsize) || unitsize >= 0xfffffff0 ||
	    !read_buf(&table, &unit, unitsize))
		return (false);

	uint16_t version;
	uint32_t header_size;
	if (!read_u16(&unit, &version) || version > 2 ||
	    !read_u32(&unit, &header_size))
		goto next;

	struct dwbuf headerstart = unit;
	uint8_t min_insn_length, default_is_stmt, line_range, opcode_base;
	int8_t line_base;
	if (!read_u8(&unit, &min_insn_length) ||
	    !read_u8(&unit, &default_is_stmt) ||
	    !read_s8(&unit, &line_base) ||
	    !read_u8(&unit, &line_range) ||
	    !read_u8(&unit, &opcode_base))
		goto next;

	/*
	 * Directory and file names are next in the header, but for now we
	 * skip directly to the line number program.
	 */
	struct dwbuf names = unit;
	unit = headerstart;
	if (!skip_bytes(&unit, header_size))
		return (false);

	/* VM registers. */
	uint64_t address = 0, file = 1, line = 1, column = 0;
	uint8_t is_stmt = default_is_stmt;
	bool basic_block = false, end_sequence = false;
	bool prologue_end = false, epilogue_begin = false;

	/* Last line table entry emitted, if any. */
	bool have_last = false;
	uint64_t last_line = 0, last_file = 0;

	/* Time to run the line program. */
	uint8_t opcode;
	while (read_u8(&unit, &opcode)) {
		bool emit = false, reset_basic_block = false;

		if (opcode >= opcode_base) {
			/* "Special" opcodes. */
			uint8_t diff = opcode - opcode_base;
			address += diff / line_range;
			line += line_base + diff % line_range;
			emit = true;
		} else if (opcode == 0) {
			/* "Extended" opcodes. */
			uint64_t extsize;
			struct dwbuf extra;
			if (!read_uleb128(&unit, &extsize) ||
			    !read_buf(&unit, &extra, extsize) ||
			    !read_u8(&extra, &opcode))
				goto next;
			switch (opcode) {
			case DW_LNE_end_sequence:
				emit = true;
				end_sequence = true;
				break;
			case DW_LNE_set_address:
				switch (extra.len) {
				case 4: {
					uint32_t address32;
					if (!read_u32(&extra, &address32))
						goto next;
					address = address32;
					break;
				}
				case 8:
					if (!read_u64(&extra, &address))
						goto next;
					break;
				default:
					DWARN("unexpected address length: %zu",
					    extra.len);
					goto next;
				}
				break;
			case DW_LNE_define_file:
				/* XXX: hope this isn't needed */
			default:
				DWARN("unknown extended opcode: %d", opcode);
				goto next;
			}
		} else {
			/* "Standard" opcodes. */
			switch (opcode) {
			case DW_LNS_copy:
				emit = true;
				reset_basic_block = true;
				break;
			case DW_LNS_advance_pc: {
				uint64_t delta;
				if (!read_uleb128(&unit, &delta))
					goto next;
				address += delta * min_insn_length;
				break;
			}
			case DW_LNS_advance_line: {
				int64_t delta;
				if (!read_sleb128(&unit, &delta))
					goto next;
				line += delta;
				break;
			}
			case DW_LNS_set_file:
				if (!read_uleb128(&unit, &file))
					goto next;
				break;
			case DW_LNS_set_column:
				if (!read_uleb128(&unit, &column))
					goto next;
				break;
			case DW_LNS_negate_stmt:
				is_stmt = !is_stmt;
				break;
			case DW_LNS_set_basic_block:
				basic_block = true;
				break;
			case DW_LNS_const_add_pc:
				address += (255 - opcode_base) / line_range;
				break;
			case DW_LNS_set_prologue_end:
				prologue_end = true;
				break;
			case DW_LNS_set_epilogue_begin:
				epilogue_begin = true;
				break;
			default:
				DWARN("unknown standard opcode: %d", opcode);
				goto next;
			}
		}

		if (emit) {
			if (address > pc) {
				/* Found an entry after our target PC. */
				if (!have_last) {
					/* Give up on this program. */
					break;
				}
				/* Return the last entry. */
				*outline = last_line;
				return (read_filename(&names, outdirname,
				    outbasename, opcode_base, last_file));
			}

			last_file = file;
			last_line = line;
			have_last = true;
		}

		if (reset_basic_block)
			basic_block = false;
	}

	goto next;
}

#ifndef _KERNEL
#include <sys/endian.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef ELFDATA
#if BYTE_ORDER == LITTLE_ENDIAN
#define ELFDATA ELFDATA2LSB
#elif BYTE_ORDER == BIG_ENDIAN
#define ELFDATA ELFDATA2MSB
#else
#error Unsupported byte order
#endif
#endif /* !ELFDATA */

static void
usage(void)
{
	extern const char *__progname;
	errx(1, "usage: %s [-s] [-e filename] [addr addr ...]", __progname);
}

/*
 * Basic addr2line clone for stand-alone testing.
 */
int
main(int argc, char *argv[])
{
	const char *filename = "a.out";

	int ch;
	bool showdir = true;
	while ((ch = getopt(argc, argv, "e:s")) != EOF) {
		switch (ch) {
		case 'e':
			filename = optarg;
			break;
		case 's':
			showdir = false;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	/* Start by mapping the full file into memory. */
	int fd = open(filename, O_RDONLY);
	if (fd == -1)
		err(1, "open");

	struct stat st;
	if (fstat(fd, &st) == -1)
		err(1, "fstat");
	if (st.st_size < (off_t)sizeof(Elf_Ehdr))
		errx(1, "file too small to be ELF");
	if ((uintmax_t)st.st_size > SIZE_MAX)
		errx(1, "file too big to fit memory");
	size_t filesize = st.st_size;

	const char *p = mmap(NULL, filesize, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		err(1, "mmap");

	close(fd);

	/* Read and validate ELF header. */
	Elf_Ehdr ehdr;
	memcpy(&ehdr, p, sizeof(ehdr));
	if (!IS_ELF(ehdr))
		errx(1, "file is not ELF");
	if (ehdr.e_ident[EI_CLASS] != ELFCLASS)
		errx(1, "unexpected word size");
	if (ehdr.e_ident[EI_DATA] != ELFDATA)
		errx(1, "unexpected data format");
	if (ehdr.e_shoff > filesize)
		errx(1, "bogus section table offset");
	if (ehdr.e_shentsize < sizeof(Elf_Shdr))
		errx(1, "unexpected section header size");
	if (ehdr.e_shnum > (filesize - ehdr.e_shoff) / ehdr.e_shentsize)
		errx(1, "bogus section header count");
	if (ehdr.e_shstrndx >= ehdr.e_shnum)
		errx(1, "bogus string table index");

	/* Find section header string table location and size. */
	Elf_Shdr shdr;
	memcpy(&shdr, p + ehdr.e_shoff + ehdr.e_shstrndx * ehdr.e_shentsize,
	    sizeof(shdr));
	if (shdr.sh_type != SHT_STRTAB)
		errx(1, "unexpected string table type");
	if (shdr.sh_offset > filesize)
		errx(1, "bogus string table offset");
	if (shdr.sh_size > filesize - shdr.sh_offset)
		errx(1, "bogus string table size");
	const char *shstrtab = p + shdr.sh_offset;
	size_t shstrtabsize = shdr.sh_size;

	/* Search through section table for .debug_line section. */
	size_t i;
	for (i = 0; i < ehdr.e_shnum; i++) {
		memcpy(&shdr, p + ehdr.e_shoff + i * ehdr.e_shentsize,
		    sizeof(shdr));
		if (0 == strncmp(".debug_line", shstrtab + shdr.sh_name,
		    shstrtabsize - shdr.sh_name))
			break;
	}
	if (i == ehdr.e_shnum)
		errx(1, "no DWARF line number table found");
	if (shdr.sh_offset > filesize)
		errx(1, "bogus line table offset");
	if (shdr.sh_size > filesize - shdr.sh_offset)
		errx(1, "bogus line table size");
	const char *linetab = p + shdr.sh_offset;
	size_t linetabsize = shdr.sh_size;

	const char *addrstr;
	while ((addrstr = *argv++) != NULL) {
		unsigned long addr = strtoul(addrstr, NULL, 16);

		const char *dir, *file;
		int line;
		if (!db_dwarf_line_at_pc(linetab, linetabsize, addr,
		    &dir, &file, &line)) {
			dir = NULL;
			file = "??";
			line = 0;
		}
		if (showdir && dir != NULL)
			printf("%s/", dir);
		printf("%s:%d\n", file, line);
	}

	return (0);
}
#endif /* !_KERNEL */
