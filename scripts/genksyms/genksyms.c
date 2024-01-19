// SPDX-License-Identifier: GPL-2.0-or-later
/* Generate kernel symbol version hashes.
   Copyright 1996, 1997 Linux International.

   New implementation contributed by Richard Henderson <rth@tamu.edu>
   Based on original work by Bjorn Ekwall <bj0rn@blox.se>

   This file was part of the Linux modutils 2.4.22: moved back into the
   kernel sources by Rusty Russell/Kai Germaschewski.

 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdarg.h>
#include <getopt.h>

#include "genksyms.h"
/*----------------------------------------------------------------------*/

#define HASH_BUCKETS  4096

static struct symbol *symtab[HASH_BUCKETS];
static FILE *debugfile;

int cur_line = 1;
char *cur_filename;
int in_source_file;

static int flag_debug, flag_dump_defs, flag_reference, flag_dump_types,
	   flag_preserve, flag_warnings;

static int errors;
static int nsyms;

static struct symbol *expansion_trail;
static struct symbol *visited_symbols;

static const struct {
	int n;
	const char *name;
} symbol_types[] = {
	[SYM_NORMAL]     = { 0, NULL},
	[SYM_TYPEDEF]    = {'t', "typedef"},
	[SYM_ENUM]       = {'e', "enum"},
	[SYM_STRUCT]     = {'s', "struct"},
	[SYM_UNION]      = {'u', "union"},
	[SYM_ENUM_CONST] = {'E', "enum constant"},
};

static int equal_list(struct string_list *a, struct string_list *b);
static void print_list(FILE * f, struct string_list *list);
static struct string_list *concat_list(struct string_list *start, ...);
static struct string_list *mk_node(const char *string);
static void print_location(void);
static void print_type_name(enum symbol_type type, const char *name);

/*----------------------------------------------------------------------*/

static const unsigned int crctab32[] = {
	0x00000000U, 0x77073096U, 0xee0e612cU, 0x990951baU, 0x076dc419U,
	0x706af48fU, 0xe963a535U, 0x9e6495a3U, 0x0edb8832U, 0x79dcb8a4U,
	0xe0d5e91eU, 0x97d2d988U, 0x09b64c2bU, 0x7eb17cbdU, 0xe7b82d07U,
	0x90bf1d91U, 0x1db71064U, 0x6ab020f2U, 0xf3b97148U, 0x84be41deU,
	0x1adad47dU, 0x6ddde4ebU, 0xf4d4b551U, 0x83d385c7U, 0x136c9856U,
	0x646ba8c0U, 0xfd62f97aU, 0x8a65c9ecU, 0x14015c4fU, 0x63066cd9U,
	0xfa0f3d63U, 0x8d080df5U, 0x3b6e20c8U, 0x4c69105eU, 0xd56041e4U,
	0xa2677172U, 0x3c03e4d1U, 0x4b04d447U, 0xd20d85fdU, 0xa50ab56bU,
	0x35b5a8faU, 0x42b2986cU, 0xdbbbc9d6U, 0xacbcf940U, 0x32d86ce3U,
	0x45df5c75U, 0xdcd60dcfU, 0xabd13d59U, 0x26d930acU, 0x51de003aU,
	0xc8d75180U, 0xbfd06116U, 0x21b4f4b5U, 0x56b3c423U, 0xcfba9599U,
	0xb8bda50fU, 0x2802b89eU, 0x5f058808U, 0xc60cd9b2U, 0xb10be924U,
	0x2f6f7c87U, 0x58684c11U, 0xc1611dabU, 0xb6662d3dU, 0x76dc4190U,
	0x01db7106U, 0x98d220bcU, 0xefd5102aU, 0x71b18589U, 0x06b6b51fU,
	0x9fbfe4a5U, 0xe8b8d433U, 0x7807c9a2U, 0x0f00f934U, 0x9609a88eU,
	0xe10e9818U, 0x7f6a0dbbU, 0x086d3d2dU, 0x91646c97U, 0xe6635c01U,
	0x6b6b51f4U, 0x1c6c6162U, 0x856530d8U, 0xf262004eU, 0x6c0695edU,
	0x1b01a57bU, 0x8208f4c1U, 0xf50fc457U, 0x65b0d9c6U, 0x12b7e950U,
	0x8bbeb8eaU, 0xfcb9887cU, 0x62dd1ddfU, 0x15da2d49U, 0x8cd37cf3U,
	0xfbd44c65U, 0x4db26158U, 0x3ab551ceU, 0xa3bc0074U, 0xd4bb30e2U,
	0x4adfa541U, 0x3dd895d7U, 0xa4d1c46dU, 0xd3d6f4fbU, 0x4369e96aU,
	0x346ed9fcU, 0xad678846U, 0xda60b8d0U, 0x44042d73U, 0x33031de5U,
	0xaa0a4c5fU, 0xdd0d7cc9U, 0x5005713cU, 0x270241aaU, 0xbe0b1010U,
	0xc90c2086U, 0x5768b525U, 0x206f85b3U, 0xb966d409U, 0xce61e49fU,
	0x5edef90eU, 0x29d9c998U, 0xb0d09822U, 0xc7d7a8b4U, 0x59b33d17U,
	0x2eb40d81U, 0xb7bd5c3bU, 0xc0ba6cadU, 0xedb88320U, 0x9abfb3b6U,
	0x03b6e20cU, 0x74b1d29aU, 0xead54739U, 0x9dd277afU, 0x04db2615U,
	0x73dc1683U, 0xe3630b12U, 0x94643b84U, 0x0d6d6a3eU, 0x7a6a5aa8U,
	0xe40ecf0bU, 0x9309ff9dU, 0x0a00ae27U, 0x7d079eb1U, 0xf00f9344U,
	0x8708a3d2U, 0x1e01f268U, 0x6906c2feU, 0xf762575dU, 0x806567cbU,
	0x196c3671U, 0x6e6b06e7U, 0xfed41b76U, 0x89d32be0U, 0x10da7a5aU,
	0x67dd4accU, 0xf9b9df6fU, 0x8ebeeff9U, 0x17b7be43U, 0x60b08ed5U,
	0xd6d6a3e8U, 0xa1d1937eU, 0x38d8c2c4U, 0x4fdff252U, 0xd1bb67f1U,
	0xa6bc5767U, 0x3fb506ddU, 0x48b2364bU, 0xd80d2bdaU, 0xaf0a1b4cU,
	0x36034af6U, 0x41047a60U, 0xdf60efc3U, 0xa867df55U, 0x316e8eefU,
	0x4669be79U, 0xcb61b38cU, 0xbc66831aU, 0x256fd2a0U, 0x5268e236U,
	0xcc0c7795U, 0xbb0b4703U, 0x220216b9U, 0x5505262fU, 0xc5ba3bbeU,
	0xb2bd0b28U, 0x2bb45a92U, 0x5cb36a04U, 0xc2d7ffa7U, 0xb5d0cf31U,
	0x2cd99e8bU, 0x5bdeae1dU, 0x9b64c2b0U, 0xec63f226U, 0x756aa39cU,
	0x026d930aU, 0x9c0906a9U, 0xeb0e363fU, 0x72076785U, 0x05005713U,
	0x95bf4a82U, 0xe2b87a14U, 0x7bb12baeU, 0x0cb61b38U, 0x92d28e9bU,
	0xe5d5be0dU, 0x7cdcefb7U, 0x0bdbdf21U, 0x86d3d2d4U, 0xf1d4e242U,
	0x68ddb3f8U, 0x1fda836eU, 0x81be16cdU, 0xf6b9265bU, 0x6fb077e1U,
	0x18b74777U, 0x88085ae6U, 0xff0f6a70U, 0x66063bcaU, 0x11010b5cU,
	0x8f659effU, 0xf862ae69U, 0x616bffd3U, 0x166ccf45U, 0xa00ae278U,
	0xd70dd2eeU, 0x4e048354U, 0x3903b3c2U, 0xa7672661U, 0xd06016f7U,
	0x4969474dU, 0x3e6e77dbU, 0xaed16a4aU, 0xd9d65adcU, 0x40df0b66U,
	0x37d83bf0U, 0xa9bcae53U, 0xdebb9ec5U, 0x47b2cf7fU, 0x30b5ffe9U,
	0xbdbdf21cU, 0xcabac28aU, 0x53b39330U, 0x24b4a3a6U, 0xbad03605U,
	0xcdd70693U, 0x54de5729U, 0x23d967bfU, 0xb3667a2eU, 0xc4614ab8U,
	0x5d681b02U, 0x2a6f2b94U, 0xb40bbe37U, 0xc30c8ea1U, 0x5a05df1bU,
	0x2d02ef8dU
};

static unsigned long partial_crc32_one(unsigned char c, unsigned long crc)
{
	return crctab32[(crc ^ c) & 0xff] ^ (crc >> 8);
}

static unsigned long partial_crc32(const char *s, unsigned long crc)
{
	while (*s)
		crc = partial_crc32_one(*s++, crc);
	return crc;
}

static unsigned long crc32(const char *s)
{
	return partial_crc32(s, 0xffffffff) ^ 0xffffffff;
}

/*----------------------------------------------------------------------*/

static enum symbol_type map_to_ns(enum symbol_type t)
{
	switch (t) {
	case SYM_ENUM_CONST:
	case SYM_NORMAL:
	case SYM_TYPEDEF:
		return SYM_NORMAL;
	case SYM_ENUM:
	case SYM_STRUCT:
	case SYM_UNION:
		return SYM_STRUCT;
	}
	return t;
}

struct symbol *find_symbol(const char *name, enum symbol_type ns, int exact)
{
	unsigned long h = crc32(name) % HASH_BUCKETS;
	struct symbol *sym;

	for (sym = symtab[h]; sym; sym = sym->hash_next)
		if (map_to_ns(sym->type) == map_to_ns(ns) &&
		    strcmp(name, sym->name) == 0 &&
		    sym->is_declared)
			break;

	if (exact && sym && sym->type != ns)
		return NULL;
	return sym;
}

static int is_unknown_symbol(struct symbol *sym)
{
	struct string_list *defn;

	return ((sym->type == SYM_STRUCT ||
		 sym->type == SYM_UNION ||
		 sym->type == SYM_ENUM) &&
		(defn = sym->defn)  && defn->tag == SYM_NORMAL &&
			strcmp(defn->string, "}") == 0 &&
		(defn = defn->next) && defn->tag == SYM_NORMAL &&
			strcmp(defn->string, "UNKNOWN") == 0 &&
		(defn = defn->next) && defn->tag == SYM_NORMAL &&
			strcmp(defn->string, "{") == 0);
}

static struct symbol *__add_symbol(const char *name, enum symbol_type type,
			    struct string_list *defn, int is_extern,
			    int is_reference)
{
	unsigned long h;
	struct symbol *sym;
	enum symbol_status status = STATUS_UNCHANGED;
	/* The parser adds symbols in the order their declaration completes,
	 * so it is safe to store the value of the previous enum constant in
	 * a static variable.
	 */
	static int enum_counter;
	static struct string_list *last_enum_expr;

	if (type == SYM_ENUM_CONST) {
		if (defn) {
			free_list(last_enum_expr, NULL);
			last_enum_expr = copy_list_range(defn, NULL);
			enum_counter = 1;
		} else {
			struct string_list *expr;
			char buf[20];

			snprintf(buf, sizeof(buf), "%d", enum_counter++);
			if (last_enum_expr) {
				expr = copy_list_range(last_enum_expr, NULL);
				defn = concat_list(mk_node("("),
						   expr,
						   mk_node(")"),
						   mk_node("+"),
						   mk_node(buf), NULL);
			} else {
				defn = mk_node(buf);
			}
		}
	} else if (type == SYM_ENUM) {
		free_list(last_enum_expr, NULL);
		last_enum_expr = NULL;
		enum_counter = 0;
		if (!name)
			/* Anonymous enum definition, nothing more to do */
			return NULL;
	}

	h = crc32(name) % HASH_BUCKETS;
	for (sym = symtab[h]; sym; sym = sym->hash_next) {
		if (map_to_ns(sym->type) == map_to_ns(type) &&
		    strcmp(name, sym->name) == 0) {
			if (is_reference)
				/* fall through */ ;
			else if (sym->type == type &&
				 equal_list(sym->defn, defn)) {
				if (!sym->is_declared && sym->is_override) {
					print_location();
					print_type_name(type, name);
					fprintf(stderr, " modversion is "
						"unchanged\n");
				}
				sym->is_declared = 1;
				return sym;
			} else if (!sym->is_declared) {
				if (sym->is_override && flag_preserve) {
					print_location();
					fprintf(stderr, "ignoring ");
					print_type_name(type, name);
					fprintf(stderr, " modversion change\n");
					sym->is_declared = 1;
					return sym;
				} else {
					status = is_unknown_symbol(sym) ?
						STATUS_DEFINED : STATUS_MODIFIED;
				}
			} else {
				error_with_pos("redefinition of %s", name);
				return sym;
			}
			break;
		}
	}

	if (sym) {
		struct symbol **psym;

		for (psym = &symtab[h]; *psym; psym = &(*psym)->hash_next) {
			if (*psym == sym) {
				*psym = sym->hash_next;
				break;
			}
		}
		--nsyms;
	}

	sym = xmalloc(sizeof(*sym));
	sym->name = name;
	sym->type = type;
	sym->defn = defn;
	sym->expansion_trail = NULL;
	sym->visited = NULL;
	sym->is_extern = is_extern;

	sym->hash_next = symtab[h];
	symtab[h] = sym;

	sym->is_declared = !is_reference;
	sym->status = status;
	sym->is_override = 0;

	if (flag_debug) {
		if (symbol_types[type].name)
			fprintf(debugfile, "Defn for %s %s == <",
				symbol_types[type].name, name);
		else
			fprintf(debugfile, "Defn for type%d %s == <",
				type, name);
		if (is_extern)
			fputs("extern ", debugfile);
		print_list(debugfile, defn);
		fputs(">\n", debugfile);
	}

	++nsyms;
	return sym;
}

struct symbol *add_symbol(const char *name, enum symbol_type type,
			  struct string_list *defn, int is_extern)
{
	return __add_symbol(name, type, defn, is_extern, 0);
}

static struct symbol *add_reference_symbol(const char *name, enum symbol_type type,
				    struct string_list *defn, int is_extern)
{
	return __add_symbol(name, type, defn, is_extern, 1);
}

/*----------------------------------------------------------------------*/

void free_node(struct string_list *node)
{
	free(node->string);
	free(node);
}

void free_list(struct string_list *s, struct string_list *e)
{
	while (s != e) {
		struct string_list *next = s->next;
		free_node(s);
		s = next;
	}
}

static struct string_list *mk_node(const char *string)
{
	struct string_list *newnode;

	newnode = xmalloc(sizeof(*newnode));
	newnode->string = xstrdup(string);
	newnode->tag = SYM_NORMAL;
	newnode->next = NULL;

	return newnode;
}

static struct string_list *concat_list(struct string_list *start, ...)
{
	va_list ap;
	struct string_list *n, *n2;

	if (!start)
		return NULL;
	for (va_start(ap, start); (n = va_arg(ap, struct string_list *));) {
		for (n2 = n; n2->next; n2 = n2->next)
			;
		n2->next = start;
		start = n;
	}
	va_end(ap);
	return start;
}

struct string_list *copy_node(struct string_list *node)
{
	struct string_list *newnode;

	newnode = xmalloc(sizeof(*newnode));
	newnode->string = xstrdup(node->string);
	newnode->tag = node->tag;

	return newnode;
}

struct string_list *copy_list_range(struct string_list *start,
				    struct string_list *end)
{
	struct string_list *res, *n;

	if (start == end)
		return NULL;
	n = res = copy_node(start);
	for (start = start->next; start != end; start = start->next) {
		n->next = copy_node(start);
		n = n->next;
	}
	n->next = NULL;
	return res;
}

static int equal_list(struct string_list *a, struct string_list *b)
{
	while (a && b) {
		if (a->tag != b->tag || strcmp(a->string, b->string))
			return 0;
		a = a->next;
		b = b->next;
	}

	return !a && !b;
}

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static struct string_list *read_node(FILE *f)
{
	char buffer[256];
	struct string_list node = {
		.string = buffer,
		.tag = SYM_NORMAL };
	int c, in_string = 0;

	while ((c = fgetc(f)) != EOF) {
		if (!in_string && c == ' ') {
			if (node.string == buffer)
				continue;
			break;
		} else if (c == '"') {
			in_string = !in_string;
		} else if (c == '\n') {
			if (node.string == buffer)
				return NULL;
			ungetc(c, f);
			break;
		}
		if (node.string >= buffer + sizeof(buffer) - 1) {
			fprintf(stderr, "Token too long\n");
			exit(1);
		}
		*node.string++ = c;
	}
	if (node.string == buffer)
		return NULL;
	*node.string = 0;
	node.string = buffer;

	if (node.string[1] == '#') {
		size_t n;

		for (n = 0; n < ARRAY_SIZE(symbol_types); n++) {
			if (node.string[0] == symbol_types[n].n) {
				node.tag = n;
				node.string += 2;
				return copy_node(&node);
			}
		}
		fprintf(stderr, "Unknown type %c\n", node.string[0]);
		exit(1);
	}
	return copy_node(&node);
}

static void read_reference(FILE *f)
{
	while (!feof(f)) {
		struct string_list *defn = NULL;
		struct string_list *sym, *def;
		int is_extern = 0, is_override = 0;
		struct symbol *subsym;

		sym = read_node(f);
		if (sym && sym->tag == SYM_NORMAL &&
		    !strcmp(sym->string, "override")) {
			is_override = 1;
			free_node(sym);
			sym = read_node(f);
		}
		if (!sym)
			continue;
		def = read_node(f);
		if (def && def->tag == SYM_NORMAL &&
		    !strcmp(def->string, "extern")) {
			is_extern = 1;
			free_node(def);
			def = read_node(f);
		}
		while (def) {
			def->next = defn;
			defn = def;
			def = read_node(f);
		}
		subsym = add_reference_symbol(xstrdup(sym->string), sym->tag,
					      defn, is_extern);
		subsym->is_override = is_override;
		free_node(sym);
	}
}

static void print_node(FILE * f, struct string_list *list)
{
	if (symbol_types[list->tag].n) {
		putc(symbol_types[list->tag].n, f);
		putc('#', f);
	}
	fputs(list->string, f);
}

static void print_list(FILE * f, struct string_list *list)
{
	struct string_list **e, **b;
	struct string_list *tmp, **tmp2;
	int elem = 1;

	if (list == NULL) {
		fputs("(nil)", f);
		return;
	}

	tmp = list;
	while ((tmp = tmp->next) != NULL)
		elem++;

	b = alloca(elem * sizeof(*e));
	e = b + elem;
	tmp2 = e - 1;

	(*tmp2--) = list;
	while ((list = list->next) != NULL)
		*(tmp2--) = list;

	while (b != e) {
		print_node(f, *b++);
		putc(' ', f);
	}
}

static unsigned long expand_and_crc_sym(struct symbol *sym, unsigned long crc)
{
	struct string_list *list = sym->defn;
	struct string_list **e, **b;
	struct string_list *tmp, **tmp2;
	int elem = 1;

	if (!list)
		return crc;

	tmp = list;
	while ((tmp = tmp->next) != NULL)
		elem++;

	b = alloca(elem * sizeof(*e));
	e = b + elem;
	tmp2 = e - 1;

	*(tmp2--) = list;
	while ((list = list->next) != NULL)
		*(tmp2--) = list;

	while (b != e) {
		struct string_list *cur;
		struct symbol *subsym;

		cur = *(b++);
		switch (cur->tag) {
		case SYM_NORMAL:
			if (flag_dump_defs)
				fprintf(debugfile, "%s ", cur->string);
			crc = partial_crc32(cur->string, crc);
			crc = partial_crc32_one(' ', crc);
			break;

		case SYM_ENUM_CONST:
		case SYM_TYPEDEF:
			subsym = find_symbol(cur->string, cur->tag, 0);
			/* FIXME: Bad reference files can segfault here. */
			if (subsym->expansion_trail) {
				if (flag_dump_defs)
					fprintf(debugfile, "%s ", cur->string);
				crc = partial_crc32(cur->string, crc);
				crc = partial_crc32_one(' ', crc);
			} else {
				subsym->expansion_trail = expansion_trail;
				expansion_trail = subsym;
				crc = expand_and_crc_sym(subsym, crc);
			}
			break;

		case SYM_STRUCT:
		case SYM_UNION:
		case SYM_ENUM:
			subsym = find_symbol(cur->string, cur->tag, 0);
			if (!subsym) {
				struct string_list *n;

				error_with_pos("expand undefined %s %s",
					       symbol_types[cur->tag].name,
					       cur->string);
				n = concat_list(mk_node
						(symbol_types[cur->tag].name),
						mk_node(cur->string),
						mk_node("{"),
						mk_node("UNKNOWN"),
						mk_node("}"), NULL);
				subsym =
				    add_symbol(cur->string, cur->tag, n, 0);
			}
			if (subsym->expansion_trail) {
				if (flag_dump_defs) {
					fprintf(debugfile, "%s %s ",
						symbol_types[cur->tag].name,
						cur->string);
				}

				crc = partial_crc32(symbol_types[cur->tag].name,
						    crc);
				crc = partial_crc32_one(' ', crc);
				crc = partial_crc32(cur->string, crc);
				crc = partial_crc32_one(' ', crc);
			} else {
				subsym->expansion_trail = expansion_trail;
				expansion_trail = subsym;
				crc = expand_and_crc_sym(subsym, crc);
			}
			break;
		}
	}

	{
		static struct symbol **end = &visited_symbols;

		if (!sym->visited) {
			*end = sym;
			end = &sym->visited;
			sym->visited = (struct symbol *)-1L;
		}
	}

	return crc;
}

void export_symbol(const char *name)
{
	struct symbol *sym;

	sym = find_symbol(name, SYM_NORMAL, 0);
	if (!sym)
		error_with_pos("export undefined symbol %s", name);
	else {
		unsigned long crc;
		int has_changed = 0;

		if (flag_dump_defs)
			fprintf(debugfile, "Export %s == <", name);

		expansion_trail = (struct symbol *)-1L;

		sym->expansion_trail = expansion_trail;
		expansion_trail = sym;
		crc = expand_and_crc_sym(sym, 0xffffffff) ^ 0xffffffff;

		sym = expansion_trail;
		while (sym != (struct symbol *)-1L) {
			struct symbol *n = sym->expansion_trail;

			if (sym->status != STATUS_UNCHANGED) {
				if (!has_changed) {
					print_location();
					fprintf(stderr, "%s: %s: modversion "
						"changed because of changes "
						"in ", flag_preserve ? "error" :
						       "warning", name);
				} else
					fprintf(stderr, ", ");
				print_type_name(sym->type, sym->name);
				if (sym->status == STATUS_DEFINED)
					fprintf(stderr, " (became defined)");
				has_changed = 1;
				if (flag_preserve)
					errors++;
			}
			sym->expansion_trail = 0;
			sym = n;
		}
		if (has_changed)
			fprintf(stderr, "\n");

		if (flag_dump_defs)
			fputs(">\n", debugfile);

		printf("#SYMVER %s 0x%08lx\n", name, crc);
	}
}

/*----------------------------------------------------------------------*/

static void print_location(void)
{
	fprintf(stderr, "%s:%d: ", cur_filename ? : "<stdin>", cur_line);
}

static void print_type_name(enum symbol_type type, const char *name)
{
	if (symbol_types[type].name)
		fprintf(stderr, "%s %s", symbol_types[type].name, name);
	else
		fprintf(stderr, "%s", name);
}

void error_with_pos(const char *fmt, ...)
{
	va_list args;

	if (flag_warnings) {
		print_location();

		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		putc('\n', stderr);

		errors++;
	}
}

static void genksyms_usage(void)
{
	fputs("Usage:\n" "genksyms [-adDTwqhVR] > /path/to/.tmp_obj.ver\n" "\n"
	      "  -d, --debug           Increment the debug level (repeatable)\n"
	      "  -D, --dump            Dump expanded symbol defs (for debugging only)\n"
	      "  -r, --reference file  Read reference symbols from a file\n"
	      "  -T, --dump-types file Dump expanded types into file\n"
	      "  -p, --preserve        Preserve reference modversions or fail\n"
	      "  -w, --warnings        Enable warnings\n"
	      "  -q, --quiet           Disable warnings (default)\n"
	      "  -h, --help            Print this message\n"
	      "  -V, --version         Print the release version\n"
	      , stderr);
}

int main(int argc, char **argv)
{
	FILE *dumpfile = NULL, *ref_file = NULL;
	int o;

	struct option long_opts[] = {
		{"debug", 0, 0, 'd'},
		{"warnings", 0, 0, 'w'},
		{"quiet", 0, 0, 'q'},
		{"dump", 0, 0, 'D'},
		{"reference", 1, 0, 'r'},
		{"dump-types", 1, 0, 'T'},
		{"preserve", 0, 0, 'p'},
		{"version", 0, 0, 'V'},
		{"help", 0, 0, 'h'},
		{0, 0, 0, 0}
	};

	while ((o = getopt_long(argc, argv, "dwqVDr:T:ph",
				&long_opts[0], NULL)) != EOF)
		switch (o) {
		case 'd':
			flag_debug++;
			break;
		case 'w':
			flag_warnings = 1;
			break;
		case 'q':
			flag_warnings = 0;
			break;
		case 'V':
			fputs("genksyms version 2.5.60\n", stderr);
			break;
		case 'D':
			flag_dump_defs = 1;
			break;
		case 'r':
			flag_reference = 1;
			ref_file = fopen(optarg, "r");
			if (!ref_file) {
				perror(optarg);
				return 1;
			}
			break;
		case 'T':
			flag_dump_types = 1;
			dumpfile = fopen(optarg, "w");
			if (!dumpfile) {
				perror(optarg);
				return 1;
			}
			break;
		case 'p':
			flag_preserve = 1;
			break;
		case 'h':
			genksyms_usage();
			return 0;
		default:
			genksyms_usage();
			return 1;
		}
	{
		extern int yydebug;
		extern int yy_flex_debug;

		yydebug = (flag_debug > 1);
		yy_flex_debug = (flag_debug > 2);

		debugfile = stderr;
		/* setlinebuf(debugfile); */
	}

	if (flag_reference) {
		read_reference(ref_file);
		fclose(ref_file);
	}

	yyparse();

	if (flag_dump_types && visited_symbols) {
		while (visited_symbols != (struct symbol *)-1L) {
			struct symbol *sym = visited_symbols;

			if (sym->is_override)
				fputs("override ", dumpfile);
			if (symbol_types[sym->type].n) {
				putc(symbol_types[sym->type].n, dumpfile);
				putc('#', dumpfile);
			}
			fputs(sym->name, dumpfile);
			putc(' ', dumpfile);
			if (sym->is_extern)
				fputs("extern ", dumpfile);
			print_list(dumpfile, sym->defn);
			putc('\n', dumpfile);

			visited_symbols = sym->visited;
			sym->visited = NULL;
		}
	}

	if (flag_debug) {
		fprintf(debugfile, "Hash table occupancy %d/%d = %g\n",
			nsyms, HASH_BUCKETS,
			(double)nsyms / (double)HASH_BUCKETS);
	}

	if (dumpfile)
		fclose(dumpfile);

	return errors != 0;
}
