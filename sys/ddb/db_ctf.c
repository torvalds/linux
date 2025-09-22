/*	$OpenBSD: db_ctf.c,v 1.36 2025/07/24 01:12:55 jsg Exp $	*/

/*
 * Copyright (c) 2016-2017 Martin Pieuchot
 * Copyright (c) 2016 Jasper Lievisse Adriaanse <jasper@openbsd.org>
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

#include <sys/param.h>
#include <sys/stdint.h>
#include <sys/systm.h>
#include <sys/exec.h>

#include <machine/db_machdep.h>

#include <ddb/db_extern.h>
#include <ddb/db_command.h>
#include <ddb/db_elf.h>
#include <ddb/db_lex.h>
#include <ddb/db_output.h>
#include <ddb/db_sym.h>
#include <ddb/db_access.h>

#include <sys/exec_elf.h>
#include <sys/ctf.h>
#include <sys/malloc.h>
#include <lib/libz/zlib.h>

extern db_symtab_t		db_symtab;

struct ddb_ctf {
	struct ctf_header	*cth;
	const char		*rawctf;	/* raw .SUNW_ctf section */
	size_t			 rawctflen;	/* raw .SUNW_ctf section size */
	const char		*data;		/* decompressed CTF data */
	size_t			 dlen;		/* decompressed CTF data size */
	const char		*strtab;	/* ELF string table */
	uint32_t		 ctf_found;
};

struct ddb_ctf db_ctf;

static const char	*db_ctf_off2name(uint32_t);
static Elf_Sym		*db_ctf_idx2sym(size_t *, uint8_t);
static char		*db_ctf_decompress(const char *, size_t, size_t);

uint32_t		 db_ctf_type_len(const struct ctf_type *);
size_t			 db_ctf_type_size(const struct ctf_type *);
const struct ctf_type	*db_ctf_type_by_name(const char *, unsigned int);
const struct ctf_type	*db_ctf_type_by_symbol(Elf_Sym *);
const struct ctf_type	*db_ctf_type_by_index(uint16_t);
void			 db_ctf_pprint(const struct ctf_type *, vaddr_t);
void			 db_ctf_pprint_struct(const struct ctf_type *, vaddr_t);
void			 db_ctf_pprint_enum(const struct ctf_type *, vaddr_t);
void			 db_ctf_pprint_ptr(const struct ctf_type *, vaddr_t);

/*
 * Entrypoint to verify CTF presence, initialize the header, decompress
 * the data, etc.
 */
void
db_ctf_init(void)
{
	db_symtab_t *stab = &db_symtab;
	size_t rawctflen;

	/* Assume nothing was correct found until proven otherwise. */
	db_ctf.ctf_found = 0;

	if (stab->private == NULL)
		return;

	db_ctf.strtab = db_elf_find_strtab(stab);
	if (db_ctf.strtab == NULL)
		return;

	db_ctf.rawctf = db_elf_find_section(stab, &rawctflen, ELF_CTF);
	if (db_ctf.rawctf == NULL)
		return;

	db_ctf.rawctflen = rawctflen;
	db_ctf.cth = (struct ctf_header *)db_ctf.rawctf;
	db_ctf.dlen = db_ctf.cth->cth_stroff + db_ctf.cth->cth_strlen;

	if ((db_ctf.cth->cth_flags & CTF_F_COMPRESS) == 0) {
		db_printf("unsupported non-compressed CTF section\n");
		return;
	}

	/* Now decompress the section, take into account to skip the header */
	db_ctf.data = db_ctf_decompress(db_ctf.rawctf + sizeof(*db_ctf.cth),
	    db_ctf.rawctflen - sizeof(*db_ctf.cth), db_ctf.dlen);
	if (db_ctf.data == NULL)
		return;

	/* We made it this far, everything seems fine. */
	db_ctf.ctf_found = 1;
}

/*
 * Convert an index to a symbol name while ensuring the type is matched.
 * It must be noted this only works if the CTF table has the same order
 * as the symbol table.
 */
Elf_Sym *
db_ctf_idx2sym(size_t *idx, uint8_t type)
{
	Elf_Sym *symp, *symtab_start, *symtab_end;
	size_t i = *idx + 1;

	symtab_start = STAB_TO_SYMSTART(&db_symtab);
	symtab_end = STAB_TO_SYMEND(&db_symtab);

	for (symp = &symtab_start[i]; symp < symtab_end; i++, symp++) {
		if (ELF_ST_TYPE(symp->st_info) != type)
			continue;

		*idx = i;
		return symp;
	}

	return NULL;
}

/*
 * For a given function name, return the number of arguments.
 */
int
db_ctf_func_numargs(Elf_Sym *st)
{
	Elf_Sym			*symp;
	uint16_t		*fstart, *fend;
	uint16_t		*fsp, kind, vlen;
	size_t			 i, idx = 0;

	if (!db_ctf.ctf_found || st == NULL)
		return -1;

	fstart = (uint16_t *)(db_ctf.data + db_ctf.cth->cth_funcoff);
	fend = (uint16_t *)(db_ctf.data + db_ctf.cth->cth_typeoff);

	fsp = fstart;
	while (fsp < fend) {
		symp = db_ctf_idx2sym(&idx, STT_FUNC);
		if (symp == NULL)
			break;

		kind = CTF_INFO_KIND(*fsp);
		vlen = CTF_INFO_VLEN(*fsp);
		fsp++;

		if (kind == CTF_K_UNKNOWN && vlen == 0)
			continue;

		/* Skip return type */
		fsp++;

		/* Skip argument types */
		for (i = 0; i < vlen; i++)
			fsp++;

		if (symp == st)
			return vlen;
	}

	return 0;
}

/*
 * Return the length of the type record in the CTF section.
 */
uint32_t
db_ctf_type_len(const struct ctf_type *ctt)
{
	uint16_t		 kind, vlen, i;
	uint32_t		 tlen;
	uint64_t		 size;

	kind = CTF_INFO_KIND(ctt->ctt_info);
	vlen = CTF_INFO_VLEN(ctt->ctt_info);

	if (ctt->ctt_size <= CTF_MAX_SIZE) {
		size = ctt->ctt_size;
		tlen = sizeof(struct ctf_stype);
	} else {
		size = CTF_TYPE_LSIZE(ctt);
		tlen = sizeof(struct ctf_type);
	}

	switch (kind) {
	case CTF_K_UNKNOWN:
	case CTF_K_FORWARD:
		break;
	case CTF_K_INTEGER:
		tlen += sizeof(uint32_t);
		break;
	case CTF_K_FLOAT:
		tlen += sizeof(uint32_t);
		break;
	case CTF_K_ARRAY:
		tlen += sizeof(struct ctf_array);
		break;
	case CTF_K_FUNCTION:
		tlen += (vlen + (vlen & 1)) * sizeof(uint16_t);
		break;
	case CTF_K_STRUCT:
	case CTF_K_UNION:
		if (size < CTF_LSTRUCT_THRESH) {
			for (i = 0; i < vlen; i++) {
				tlen += sizeof(struct ctf_member);
			}
		} else {
			for (i = 0; i < vlen; i++) {
				tlen += sizeof(struct ctf_lmember);
			}
		}
		break;
	case CTF_K_ENUM:
		for (i = 0; i < vlen; i++) {
			tlen += sizeof(struct ctf_enum);
		}
		break;
	case CTF_K_POINTER:
	case CTF_K_TYPEDEF:
	case CTF_K_VOLATILE:
	case CTF_K_CONST:
	case CTF_K_RESTRICT:
		break;
	default:
		return 0;
	}

	return tlen;
}

/*
 * Return the size of the type.
 */
size_t
db_ctf_type_size(const struct ctf_type *ctt)
{
	vaddr_t			 taddr = (vaddr_t)ctt;
	const struct ctf_type	*ref;
	const struct ctf_array	*arr;
	size_t			 tlen = 0;
	uint16_t		 kind;
	uint32_t		 toff;
	uint64_t		 size;

	kind = CTF_INFO_KIND(ctt->ctt_info);

	if (ctt->ctt_size <= CTF_MAX_SIZE) {
		size = ctt->ctt_size;
		toff = sizeof(struct ctf_stype);
	} else {
		size = CTF_TYPE_LSIZE(ctt);
		toff = sizeof(struct ctf_type);
	}

	switch (kind) {
	case CTF_K_UNKNOWN:
	case CTF_K_FORWARD:
		break;
	case CTF_K_INTEGER:
	case CTF_K_FLOAT:
		tlen = size;
		break;
	case CTF_K_ARRAY:
		arr = (struct ctf_array *)(taddr + toff);
		ref = db_ctf_type_by_index(arr->cta_contents);
		tlen = arr->cta_nelems * db_ctf_type_size(ref);
		break;
	case CTF_K_FUNCTION:
		tlen = 0;
		break;
	case CTF_K_STRUCT:
	case CTF_K_UNION:
	case CTF_K_ENUM:
		tlen = size;
		break;
	case CTF_K_POINTER:
		tlen = sizeof(void *);
		break;
	case CTF_K_TYPEDEF:
	case CTF_K_VOLATILE:
	case CTF_K_CONST:
	case CTF_K_RESTRICT:
		ref = db_ctf_type_by_index(ctt->ctt_type);
		tlen = db_ctf_type_size(ref);
		break;
	default:
		return 0;
	}

	return tlen;
}

/*
 * Return the CTF type associated to an ELF symbol.
 */
const struct ctf_type *
db_ctf_type_by_symbol(Elf_Sym *st)
{
	Elf_Sym			*symp;
	uint32_t		 objtoff;
	uint16_t		*dsp;
	size_t			 idx = 0;

	if (!db_ctf.ctf_found || st == NULL)
		return NULL;

	objtoff = db_ctf.cth->cth_objtoff;

	while (objtoff < db_ctf.cth->cth_funcoff) {
		dsp = (uint16_t *)(db_ctf.data + objtoff);

		symp = db_ctf_idx2sym(&idx, STT_OBJECT);
		if (symp == NULL)
			break;
		if (symp == st)
			return db_ctf_type_by_index(*dsp);

		objtoff += sizeof(*dsp);
	}

	return NULL;
}

const struct ctf_type *
db_ctf_type_by_name(const char *name, unsigned int kind)
{
	struct ctf_header	*cth;
	const struct ctf_type   *ctt;
	const char		*tname;
	uint32_t		 off, toff;

	if (!db_ctf.ctf_found)
		return (NULL);

	cth = db_ctf.cth;

	for (off = cth->cth_typeoff; off < cth->cth_stroff; off += toff) {
		ctt = (struct ctf_type *)(db_ctf.data + off);
		toff = db_ctf_type_len(ctt);
		if (toff == 0) {
			db_printf("incorrect type at offset %u", off);
			break;
		}

		if (CTF_INFO_KIND(ctt->ctt_info) != kind)
			continue;

		tname = db_ctf_off2name(ctt->ctt_name);
		if (tname == NULL)
			continue;

		if (strcmp(name, tname) == 0)
			return (ctt);
	}

	return (NULL);
}

/*
 * Return the CTF type corresponding to a given index in the type section.
 */
const struct ctf_type *
db_ctf_type_by_index(uint16_t index)
{
	uint32_t		 offset = db_ctf.cth->cth_typeoff;
	uint16_t		 idx = 1;

	if (!db_ctf.ctf_found)
		return NULL;

	while (offset < db_ctf.cth->cth_stroff) {
		const struct ctf_type   *ctt;
		uint32_t		 toff;

		ctt = (struct ctf_type *)(db_ctf.data + offset);
		if (idx == index)
			return ctt;

		toff = db_ctf_type_len(ctt);
		if (toff == 0) {
			db_printf("incorrect type at offset %u", offset);
			break;
		}
		offset += toff;
		idx++;
	}

	return NULL;
}

/*
 * Pretty print `addr'.
 */
void
db_ctf_pprint(const struct ctf_type *ctt, vaddr_t addr)
{
	vaddr_t			 taddr = (vaddr_t)ctt;
	const struct ctf_type	*ref;
	const struct ctf_array	*arr;
	uint16_t		 kind;
	uint32_t		 eob, toff, i;
	db_expr_t		 val;
	size_t			 elm_size;

	if (ctt == NULL)
		return;

	kind = CTF_INFO_KIND(ctt->ctt_info);
	if (ctt->ctt_size <= CTF_MAX_SIZE)
		toff = sizeof(struct ctf_stype);
	else
		toff = sizeof(struct ctf_type);

	switch (kind) {
	case CTF_K_ARRAY:
		arr = (struct ctf_array *)(taddr + toff);
		ref = db_ctf_type_by_index(arr->cta_contents);
		elm_size = db_ctf_type_size(ref);
		db_printf("[");
		for (i = 0; i < arr->cta_nelems; i++) {
			db_ctf_pprint(ref, addr + i * elm_size);
			if (i + 1 < arr->cta_nelems)
				db_printf(",");
		}
		db_printf("]");
		break;
	case CTF_K_ENUM:
		db_ctf_pprint_enum(ctt, addr);
		break;
	case CTF_K_FLOAT:
	case CTF_K_FUNCTION:
		val = db_get_value(addr, sizeof(val), 0);
		db_printf("%lx", (unsigned long)val);
		break;
	case CTF_K_INTEGER:
		eob = db_get_value((taddr + toff), sizeof(eob), 0);
		switch (CTF_INT_BITS(eob)) {
#ifndef __LP64__
		case 64: {
			uint64_t val64;
#if BYTE_ORDER == LITTLE_ENDIAN
			val64 = db_get_value(addr + 4, CTF_INT_BITS(eob) / 8,
			   CTF_INT_ENCODING(eob) & CTF_INT_SIGNED); 
			val64 <<= 32;
			val64 |= db_get_value(addr, CTF_INT_BITS(eob) / 8, 0);
#else
			val64 = db_get_value(addr, CTF_INT_BITS(eob) / 8,
			   CTF_INT_ENCODING(eob) & CTF_INT_SIGNED); 
			val64 <<= 32;
			val64 |= db_get_value(addr + 4, CTF_INT_BITS(eob) / 8,
			    0);
#endif
			if (CTF_INT_ENCODING(eob) & CTF_INT_SIGNED)
				db_printf("%lld", val64);
			else
				db_printf("%llu", val64);
			break;
		}
#endif
		default:
			val = db_get_value(addr, CTF_INT_BITS(eob) / 8,
			   CTF_INT_ENCODING(eob) & CTF_INT_SIGNED); 
			if (CTF_INT_ENCODING(eob) & CTF_INT_SIGNED)
				db_printf("%ld", val);
			else
				db_printf("%lu", val);
			break;
		}
		break;
	case CTF_K_STRUCT:
	case CTF_K_UNION:
		db_ctf_pprint_struct(ctt, addr);
		break;
	case CTF_K_POINTER:
		db_ctf_pprint_ptr(ctt, addr);
		break;
	case CTF_K_TYPEDEF:
	case CTF_K_VOLATILE:
	case CTF_K_CONST:
	case CTF_K_RESTRICT:
		ref = db_ctf_type_by_index(ctt->ctt_type);
		db_ctf_pprint(ref, addr);
		break;
	case CTF_K_UNKNOWN:
	case CTF_K_FORWARD:
	default:
		break;
	}
}

void
db_ctf_pprint_struct(const struct ctf_type *ctt, vaddr_t addr)
{
	const char		*name, *p = (const char *)ctt;
	const struct ctf_type	*ref;
	uint32_t		 toff;
	uint64_t		 size;
	uint16_t		 i, vlen;

	vlen = CTF_INFO_VLEN(ctt->ctt_info);

	if (ctt->ctt_size <= CTF_MAX_SIZE) {
		size = ctt->ctt_size;
		toff = sizeof(struct ctf_stype);
	} else {
		size = CTF_TYPE_LSIZE(ctt);
		toff = sizeof(struct ctf_type);
	}

	db_printf("{");
	if (size < CTF_LSTRUCT_THRESH) {
		for (i = 0; i < vlen; i++) {
			struct ctf_member	*ctm;

			ctm = (struct ctf_member *)(p + toff);
			toff += sizeof(struct ctf_member);

			name = db_ctf_off2name(ctm->ctm_name);
			if (name != NULL)
				db_printf("%s = ", name);
			ref = db_ctf_type_by_index(ctm->ctm_type);
			db_ctf_pprint(ref, addr + ctm->ctm_offset / 8);
			if (i < vlen - 1)
				db_printf(", ");
		}
	} else {
		for (i = 0; i < vlen; i++) {
			struct ctf_lmember	*ctlm;

			ctlm = (struct ctf_lmember *)(p + toff);
			toff += sizeof(struct ctf_lmember);

			name = db_ctf_off2name(ctlm->ctlm_name);
			if (name != NULL)
				db_printf("%s = ", name);
			ref = db_ctf_type_by_index(ctlm->ctlm_type);
			db_ctf_pprint(ref, addr +
			    CTF_LMEM_OFFSET(ctlm) / 8);
			if (i < vlen - 1)
				db_printf(", ");
		}
	}
	db_printf("}");
}

void
db_ctf_pprint_enum(const struct ctf_type *ctt, vaddr_t addr)
{
	const char		*name = NULL, *p = (const char *)ctt;
	struct ctf_enum		*cte;
	uint32_t		 toff;
	int32_t			 val;
	uint16_t		 i, vlen;

	vlen = CTF_INFO_VLEN(ctt->ctt_info);
	toff = sizeof(struct ctf_stype);

	val = (int32_t)db_get_value(addr, sizeof(val), 1);
	for (i = 0; i < vlen; i++) {
		cte = (struct ctf_enum *)(p + toff);
		toff += sizeof(*cte);

		if (val == cte->cte_value) {
			name = db_ctf_off2name(cte->cte_name);
			break;
		}
	}

	if (name != NULL)
		db_printf("%s", name);
	else
		db_printf("#%d", val);
}

void
db_ctf_pprint_ptr(const struct ctf_type *ctt, vaddr_t addr)
{
	const char		*name, *modif = "";
	const struct ctf_type	*ref;
	uint16_t		 kind;
	unsigned long		 ptr;

	ref = db_ctf_type_by_index(ctt->ctt_type);
	kind = CTF_INFO_KIND(ref->ctt_info);

	switch (kind) {
	case CTF_K_VOLATILE:
		modif = "volatile ";
		ref = db_ctf_type_by_index(ref->ctt_type);
		break;
	case CTF_K_CONST:
		modif = "const ";
		ref = db_ctf_type_by_index(ref->ctt_type);
		break;
	case CTF_K_STRUCT:
		modif = "struct ";
		break;
	case CTF_K_UNION:
		modif = "union ";
		break;
	default:
		break;
	}

	name = db_ctf_off2name(ref->ctt_name);
	if (name != NULL)
		db_printf("(%s%s *)", modif, name);

	ptr = (unsigned long)db_get_value(addr, sizeof(ptr), 0);

	db_printf("0x%lx", ptr);
}

static const char *
db_ctf_off2name(uint32_t offset)
{
	const char		*name;

	if (!db_ctf.ctf_found)
		return NULL;

	if (CTF_NAME_STID(offset) != CTF_STRTAB_0)
		return "external";

	if (CTF_NAME_OFFSET(offset) >= db_ctf.cth->cth_strlen)
		return "exceeds strlab";

	if (db_ctf.cth->cth_stroff + CTF_NAME_OFFSET(offset) >= db_ctf.dlen)
		return "invalid";

	name = db_ctf.data + db_ctf.cth->cth_stroff + CTF_NAME_OFFSET(offset);
	if (*name == '\0')
		return NULL;

	return name;
}

static char *
db_ctf_decompress(const char *buf, size_t size, size_t len)
{
	z_stream		 stream;
	char			*data;
	int			 error;

	data = malloc(len, M_TEMP, M_WAITOK|M_ZERO|M_CANFAIL);
	if (data == NULL)
		return NULL;

	memset(&stream, 0, sizeof(stream));
	stream.next_in = (void *)buf;
	stream.avail_in = size;
	stream.next_out = data;
	stream.avail_out = len;

	if ((error = inflateInit(&stream)) != Z_OK) {
		db_printf("zlib inflateInit failed: %s", zError(error));
		goto exit;
	}

	if ((error = inflate(&stream, Z_FINISH)) != Z_STREAM_END) {
		db_printf("zlib inflate failed: %s", zError(error));
		inflateEnd(&stream);
		goto exit;
	}

	if ((error = inflateEnd(&stream)) != Z_OK) {
		db_printf("zlib inflateEnd failed: %s", zError(error));
		goto exit;
	}

	if (stream.total_out != len) {
		db_printf("decompression failed: %lu != %zu",
		    stream.total_out, len);
		goto exit;
	}

	return data;

exit:
	free(data, M_TEMP, len);
	return NULL;
}

/*
 * pprint <symbol name>
 */
void
db_ctf_pprint_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	Elf_Sym *st;
	const struct ctf_type *ctt;
	int t;

	if (!db_ctf.ctf_found) {
		db_printf("No CTF data found\n");
		db_flush_lex();
		return;
	}

	/*
	 * Read the struct name from the debugger input.
	 */
	t = db_read_token();
	if (t != tIDENT) {
		db_printf("Bad symbol name\n");
		db_flush_lex();
		return;
	}

	if ((st = db_symbol_by_name(db_tok_string, &addr)) == NULL) {
		db_printf("Symbol not found %s\n", db_tok_string);
		db_flush_lex();
		return;
	}

	if ((ctt = db_ctf_type_by_symbol(st)) == NULL) {
		modif[0] = '\0';
		db_print_cmd(addr, 0, 0, modif);
		db_flush_lex();
		return;
	}

	db_printf("%s:\t", db_tok_string);
	db_ctf_pprint(ctt, addr);
	db_printf("\n");
}

/*
 * show struct <struct name> [addr]: displays the data starting at addr
 * (`dot' if unspecified) as a struct of the given type.
 */
void
db_ctf_show_struct(db_expr_t addr, int have_addr, db_expr_t count,
    char *modifiers)
{
	const struct ctf_type *ctt;
	const char *name;
	uint64_t sz;
	int t;

	/*
	 * Read the struct name from the debugger input.
	 */
	t = db_read_token();
	if (t != tIDENT) {
		db_printf("Bad struct name\n");
		db_flush_lex();
		return;
	}
	name = db_tok_string;

	ctt = db_ctf_type_by_name(name, CTF_K_STRUCT);
	if (ctt == NULL) {
		db_printf("unknown struct %s\n", name);
		db_flush_lex();
		return;
	}

	/*
	 * Read the address, if any, from the debugger input.
	 * In that case, update `dot' value.
	 */
	if (db_expression(&addr)) {
		db_dot = (vaddr_t)addr;
		db_last_addr = db_dot;
	} else
		addr = (db_expr_t)db_dot;

	db_skip_to_eol();

	/*
	 * Display the structure contents.
	 */
	sz = (ctt->ctt_size <= CTF_MAX_SIZE) ?
	    ctt->ctt_size : CTF_TYPE_LSIZE(ctt);
	db_printf("struct %s at %p (%llu bytes) ", name, (void *)addr, sz);
	db_ctf_pprint_struct(ctt, addr);
}
