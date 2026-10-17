/* Generate assembler source containing symbol information
 *
 * Copyright 2002       by Kai Germaschewski
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Usage: kallsyms [--all-symbols] [--pc-relative] [--sysmap=out.map] in out.bin > out.S
 *        kallsyms --sysmap=out.map in
 *
 *      in is vmlinux; an empty file stands for the first link, which has no
 *  symbols yet, and gives an empty table. --sysmap also writes the symbols
 *  in System.map format.
 *
 *      The byte tables go to out.bin and are pulled into out.S with .incbin;
 *  wider tables stay assembler source for endianness and relocations.
 *
 *      Table compression uses all the unused char codes on the symbols and
 *  maps these to the most used substrings (tokens). For instance, it might
 *  map char code 0xF7 to represent "write_" and then in every symbol where
 *  "write_" appears it can be replaced by 0xF7, saving 5 bytes.
 *      The used codes themselves are also placed in the table so that the
 *  decompresion can work without "special cases".
 *      Applied to kernel symbols, this usually produces a compression ratio
 *  of about 50%.
 *
 */

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>
#include <xalloc.h>

#include "kallsyms.h"

#define KSYM_NAME_LEN		512

struct sym_entry {
	unsigned long long addr;
	unsigned int len;
	unsigned int seq;
	unsigned char sym[];
};

struct addr_range {
	const char *start_sym, *end_sym;
	unsigned long long start, end;
};

static unsigned long long _text;
static struct addr_range text_ranges[] = {
	{ "_stext",     "_etext"     },
	{ "_sinittext", "_einittext" },
};
#define text_range_text     (&text_ranges[0])
#define text_range_inittext (&text_ranges[1])

static struct sym_entry **table;
static unsigned int table_size, table_cnt;
static int all_symbols;
static int pc_relative;

/* A dynamic array of symbols, encoded by symbol index. */
struct sym_arr {
	unsigned int *sym_indexes;
	unsigned int cnt, cap;
};

/* Every pair of bytes is a token. */
#define NR_TOKENS	0x10000

static int token_profit[NR_TOKENS];
static struct sym_arr token_syms[NR_TOKENS];

/* the table that holds the result of the compression */
static unsigned char best_table[256][2];
static unsigned char best_table_len[256];

static unsigned int sym_arr_last(const struct sym_arr *arr)
{
	return arr->cnt ? arr->sym_indexes[arr->cnt - 1] : UINT_MAX;
}

static void sym_arr_maybe_expand(struct sym_arr *arr)
{
	if (arr->cap > arr->cnt)
		return;

	arr->cap = arr->cap ? arr->cap * 2 : 16;
	arr->sym_indexes = xrealloc(arr->sym_indexes,
				    arr->cap * sizeof(*arr->sym_indexes));
}

static void sym_arr_add(struct sym_arr *arr, unsigned int sym_idx)
{
	sym_arr_maybe_expand(arr);
	arr->sym_indexes[arr->cnt++] = sym_idx;
}

static void sym_arr_free(struct sym_arr *arr)
{
	free(arr->sym_indexes);
	arr->sym_indexes = NULL;
	arr->cnt = 0;
	arr->cap = 0;
}

static void usage(void)
{
	fprintf(stderr, "Usage: kallsyms [--all-symbols] [--pc-relative] [--sysmap=out.map]\n"
			"                in out.bin > out.S\n"
			"       kallsyms --sysmap=out.map vmlinux\n");
	exit(1);
}

static char *sym_entry_name(const struct sym_entry *s)
{
	return (char *)s->sym + 1;
}

static bool is_ignored_symbol(const char *name, char type)
{
	if (type == 'u' || type == 'n')
		return true;

	if (toupper(type) == 'A') {
		/* Keep these useful absolute symbols */
		if (strcmp(name, "__kernel_syscall_via_break") &&
		    strcmp(name, "__kernel_syscall_via_epc") &&
		    strcmp(name, "__kernel_sigtramp") &&
		    strcmp(name, "__gp"))
			return true;
	}

	return false;
}

static void check_symbol_range(const char *sym, unsigned long long addr,
			       struct addr_range *ranges, int entries)
{
	size_t i;
	struct addr_range *ar;

	for (i = 0; i < entries; ++i) {
		ar = &ranges[i];

		if (strcmp(sym, ar->start_sym) == 0) {
			ar->start = addr;
			return;
		} else if (strcmp(sym, ar->end_sym) == 0) {
			ar->end = addr;
			return;
		}
	}
}

static struct sym_entry *add_symbol(unsigned long long addr, char type,
				    const char *name)
{
	size_t len = strlen(name);
	struct sym_entry *sym;

	if (len >= KSYM_NAME_LEN) {
		fprintf(stderr, "Symbol %s too long for kallsyms (%zu >= %d).\n"
				"Please increase KSYM_NAME_LEN both in kernel and kallsyms.c\n",
			name, len, KSYM_NAME_LEN);
		return NULL;
	}

	if (strcmp(name, "_text") == 0)
		_text = addr;

	/* Ignore most absolute/undefined (?) symbols. */
	if (is_ignored_symbol(name, type))
		return NULL;

	check_symbol_range(name, addr, text_ranges, ARRAY_SIZE(text_ranges));

	/* include the type field in the symbol name, so that it gets
	 * compressed together */
	len++;

	sym = xmalloc(sizeof(*sym) + len + 1);
	sym->addr = addr;
	sym->len = len;
	sym->sym[0] = type;
	strcpy(sym_entry_name(sym), name);

	return sym;
}

static int symbol_in_range(const struct sym_entry *s,
			   const struct addr_range *ranges, int entries)
{
	size_t i;
	const struct addr_range *ar;

	for (i = 0; i < entries; ++i) {
		ar = &ranges[i];

		if (s->addr >= ar->start && s->addr <= ar->end)
			return 1;
	}

	return 0;
}

static int symbol_valid(const struct sym_entry *s)
{
	const char *name = sym_entry_name(s);

	/* if --all-symbols is not specified, then symbols outside the text
	 * and inittext sections are discarded */
	if (!all_symbols) {
		/*
		 * Symbols starting with __start and __stop are used to denote
		 * section boundaries, and should always be included:
		 */
		if (string_starts_with(name, "__start_") ||
		    string_starts_with(name, "__stop_"))
			return 1;

		if (symbol_in_range(s, text_ranges,
				    ARRAY_SIZE(text_ranges)) == 0)
			return 0;
		/* Corner case.  Discard any symbols with the same value as
		 * _etext _einittext; they can move between pass 1 and 2 when
		 * the kallsyms data are added.  If these symbols move then
		 * they may get dropped in pass 2, which breaks the kallsyms
		 * rules.
		 */
		if ((s->addr == text_range_text->end &&
		     strcmp(name, text_range_text->end_sym)) ||
		    (s->addr == text_range_inittext->end &&
		     strcmp(name, text_range_inittext->end_sym)))
			return 0;
	}

	return 1;
}

/* remove all the invalid symbols from the table */
static void shrink_table(void)
{
	unsigned int i, pos;

	pos = 0;
	for (i = 0; i < table_cnt; i++) {
		if (symbol_valid(table[i])) {
			if (pos != i)
				table[pos] = table[i];
			pos++;
		} else {
			free(table[i]);
		}
	}
	table_cnt = pos;
}

static void add_table_entry(struct sym_entry *sym)
{
	sym->seq = table_cnt;

	if (table_cnt >= table_size) {
		table_size = table_size ? table_size * 2 : 10000;
		table = xrealloc(table, sizeof(*table) * table_size);
	}

	table[table_cnt++] = sym;
}

static bool file_is_empty(const char *path)
{
	struct stat st;

	if (stat(path, &st)) {
		perror(path);
		exit(EXIT_FAILURE);
	}

	return st.st_size == 0;
}

/*
 * Read the symbols from vmlinux, writing System.map if asked to. The first
 * link has no symbols yet: an empty file gives an empty table.
 */
static void read_elf(const char *path, FILE *sysmap_out)
{
	struct sysmap *map;
	size_t i;

	if (file_is_empty(path))
		return;

	map = sysmap_read(path);
	if (sysmap_out)
		sysmap_write(map, sysmap_out);

	for (i = 0; i < map->nr_syms; i++) {
		const struct sysmap_symbol *s = &map->syms[i];
		struct sym_entry *sym = add_symbol(s->addr, s->type, s->name);

		if (sym)
			add_table_entry(sym);
	}

	sysmap_free(map);
}

static void output_label(const char *label)
{
	printf(".globl %s\n", label);
	printf("\t.balign 4\n");
	printf("%s:\n", label);
}

static long bin_pos(FILE *file)
{
	const long pos = ftell(file);

	if (pos < 0) {
		perror("kallsyms: ftell");
		exit(EXIT_FAILURE);
	}

	return pos;
}

static void write_incbin(const char *filename, long start, long end)
{
	if (start >= end)
		return;

	printf("\t.incbin \"%s\", %ld, %ld\n", filename, start, end - start);
}

/* uncompress a compressed symbol. When this function is called, the best table
 * might still be compressed itself, so the function needs to be recursive */
static int expand_symbol(const unsigned char *data, int len, char *result)
{
	int c, rlen, total=0;

	while (len) {
		c = *data;
		/* if the table holds a single char that is the same as the one
		 * we are looking for, then end the search */
		if (best_table[c][0]==c && best_table_len[c]==1) {
			*result++ = c;
			total++;
		} else {
			/* if not, recurse and expand */
			rlen = expand_symbol(best_table[c], best_table_len[c], result);
			total += rlen;
			result += rlen;
		}
		data++;
		len--;
	}
	*result=0;

	return total;
}

static int compare_names(const void *a, const void *b)
{
	int ret;
	const struct sym_entry *sa = *(const struct sym_entry **)a;
	const struct sym_entry *sb = *(const struct sym_entry **)b;

	ret = strcmp(sym_entry_name(sa), sym_entry_name(sb));
	if (!ret) {
		if (sa->addr > sb->addr)
			return 1;
		else if (sa->addr < sb->addr)
			return -1;

		/* keep old order */
		return (int)(sa->seq - sb->seq);
	}

	return ret;
}

static void sort_symbols_by_name(void)
{
	qsort(table, table_cnt, sizeof(table[0]), compare_names);
}

static void write_src(FILE *out_bin_file, const char *out_bin_name)
{
	unsigned int i, off;
	unsigned int best_idx[256];
	unsigned int *markers, markers_cnt;
	long bin_start;
	char buf[KSYM_NAME_LEN];

	printf("\t.section .rodata, \"a\"\n");

	output_label("kallsyms_num_syms");
	printf("\t.long\t%u\n", table_cnt);
	printf("\n");

	/* table of offset markers, that give the offset in the compressed stream
	 * every 256 symbols */
	markers_cnt = (table_cnt + 255) / 256;
	markers = xmalloc(sizeof(*markers) * markers_cnt);

	output_label("kallsyms_names");
	bin_start = bin_pos(out_bin_file);
	off = 0;
	for (i = 0; i < table_cnt; i++) {
		if ((i & 0xFF) == 0)
			markers[i >> 8] = off;
		table[i]->seq = i;

		/* There cannot be any symbol of length zero. */
		if (table[i]->len == 0) {
			fprintf(stderr, "kallsyms failure: "
				"unexpected zero symbol length\n");
			exit(EXIT_FAILURE);
		}

		/* Only lengths that fit in up-to-two-byte ULEB128 are supported. */
		if (table[i]->len > 0x3FFF) {
			fprintf(stderr, "kallsyms failure: "
				"unexpected huge symbol length\n");
			exit(EXIT_FAILURE);
		}

		/* Encode length with ULEB128. */
		if (table[i]->len <= 0x7F) {
			/* Most symbols use a single byte for the length. */
			fputc(table[i]->len, out_bin_file);
			off += table[i]->len + 1;
		} else {
			/* "Big" symbols use two bytes. */
			fputc((table[i]->len & 0x7F) | 0x80, out_bin_file);
			fputc((table[i]->len >> 7) & 0x7F, out_bin_file);
			off += table[i]->len + 2;
		}
		fwrite(table[i]->sym, 1, table[i]->len, out_bin_file);

		/*
		 * Now that we wrote out the compressed symbol name, restore the
		 * original name for the comments below.
		 */
		expand_symbol(table[i]->sym, table[i]->len, buf);
		strcpy((char *)table[i]->sym, buf);
	}
	write_incbin(out_bin_name, bin_start, bin_pos(out_bin_file));
	printf(".size kallsyms_names, . - kallsyms_names\n");
	printf("\n");

	output_label("kallsyms_markers");
	for (i = 0; i < markers_cnt; i++)
		printf("\t.long\t%u\n", markers[i]);
	printf(".size kallsyms_markers, . - kallsyms_markers\n");
	printf("\n");

	free(markers);

	output_label("kallsyms_token_table");
	bin_start = bin_pos(out_bin_file);
	off = 0;
	for (i = 0; i < 256; i++) {
		best_idx[i] = off;
		expand_symbol(best_table[i], best_table_len[i], buf);
		fwrite(buf, 1, strlen(buf) + 1, out_bin_file);
		off += strlen(buf) + 1;
	}
	write_incbin(out_bin_name, bin_start, bin_pos(out_bin_file));
	printf(".size kallsyms_token_table, . - kallsyms_token_table\n");
	printf("\n");

	output_label("kallsyms_token_index");
	for (i = 0; i < 256; i++)
		printf("\t.short\t%d\n", best_idx[i]);
	printf("\n");

	output_label("kallsyms_offsets");

	for (i = 0; i < table_cnt; i++) {
		if (pc_relative) {
			long long offset = table[i]->addr - _text;

			if (offset < INT_MIN || offset > INT_MAX) {
				fprintf(stderr, "kallsyms failure: "
					"relative symbol value %#llx out of range\n",
					table[i]->addr);
				exit(EXIT_FAILURE);
			}
			printf("\t.long\t_text - . + (%d)\t/* %s */\n",
			       (int)offset, table[i]->sym);
		} else {
			printf("\t.long\t%#x\t/* %s */\n",
			       (unsigned int)table[i]->addr, table[i]->sym);
		}
	}
	printf(".size kallsyms_offsets, . - kallsyms_offsets\n");
	printf("\n");

	sort_symbols_by_name();
	output_label("kallsyms_seqs_of_names");
	bin_start = bin_pos(out_bin_file);
	for (i = 0; i < table_cnt; i++) {
		fputc(table[i]->seq >> 16, out_bin_file);
		fputc(table[i]->seq >> 8, out_bin_file);
		fputc(table[i]->seq >> 0, out_bin_file);
	}
	write_incbin(out_bin_name, bin_start, bin_pos(out_bin_file));
	printf("\n");
}

static unsigned int token_index(unsigned char first, unsigned char second)
{
	return first + (second << 8);
}

static unsigned int sym_token_index(const unsigned char *symbol, int first_idx)
{
	return token_index(symbol[first_idx], symbol[first_idx + 1]);
}

/* table lookup compression functions */

/* count all the possible tokens in a symbol */
static void learn_symbol(const unsigned char *symbol, int len)
{
	int i;

	for (i = 0; i < len - 1; i++)
		token_profit[sym_token_index(symbol, i)]++;
}

/* decrease the count for all the possible tokens in a symbol */
static void forget_symbol(const unsigned char *symbol, int len)
{
	int i;

	for (i = 0; i < len - 1; i++)
		token_profit[sym_token_index(symbol, i)]--;
}

static void token_add_symbol(unsigned int token_idx, unsigned int sym_idx)
{
	struct sym_arr *arr = &token_syms[token_idx];

	/* Symbol indexes kept in sorted order, check for duplicate. */
	if (sym_arr_last(arr) == sym_idx)
		return;

	sym_arr_add(arr, sym_idx);
}

static void symbol_index_all_tokens(const unsigned char *symbol, int len,
				    unsigned int sym_idx)
{
	int i;

	for (i = 0; i < len - 1; i++) {
		const unsigned int token_idx = sym_token_index(symbol, i);

		token_add_symbol(token_idx, sym_idx);
	}
}

/*
 * The symbol just got compressed. The only parts of the symbol that changed
 * meaningfully are those containing the newly assigned compressed char, so
 * index those.
 */
static void symbol_index_new_tokens(const unsigned char *symbol, int len,
				    unsigned int sym_idx, int compressed_chr)
{
	int i;

	for (i = 0; i < len - 1; i++) {
		const unsigned int token_idx = sym_token_index(symbol, i);

		if (symbol[i] == compressed_chr ||
		    symbol[i + 1] == compressed_chr)
			token_add_symbol(token_idx, sym_idx);
	}
}

static void build_initial_token_table(void)
{
	unsigned int i;

	for (i = 0; i < table_cnt; i++)
		learn_symbol(table[i]->sym, table[i]->len);

	/*
	 * The initial occurrence counts tell us exactly how much memory should
	 * be reserved for each token's symbol array.
	 */
	for (i = 0; i < ARRAY_SIZE(token_syms); i++) {
		const int nr_syms = token_profit[i];

		if (!nr_syms)
			continue;

		token_syms[i].cap = nr_syms;
		token_syms[i].sym_indexes =
			xmalloc(nr_syms * sizeof(unsigned int));
	}

	/* For every symbol, index every token -> symbol it is present in. */
	for (i = 0; i < table_cnt; i++)
		symbol_index_all_tokens(table[i]->sym, table[i]->len, i);
}

static unsigned char *find_token(unsigned char *str, int len,
				 const unsigned char *token)
{
	int i;

	for (i = 0; i < len - 1; i++) {
		if (str[i] == token[0] && str[i+1] == token[1])
			return &str[i];
	}
	return NULL;
}

/* replace a given token in all the valid symbols. Use the sampled symbols
 * to update the counts */
static void compress_symbols(const unsigned char *str, int compressed_chr)
{
	const unsigned int token_idx = sym_token_index(str, 0);
	struct sym_arr *arr = &token_syms[token_idx];
	unsigned int sym_idx, j, len, size;
	unsigned char *p1, *p2;

	/* Iterate through all symbols this token is found in and compress. */
	for (j = 0; j < arr->cnt; j++) {
		sym_idx = arr->sym_indexes[j];

		len = table[sym_idx]->len;
		p1 = table[sym_idx]->sym;

		/* find the token on the symbol */
		p2 = find_token(p1, len, str);
		if (!p2) continue;

		/* decrease the counts for this symbol's tokens */
		forget_symbol(table[sym_idx]->sym, len);

		size = len;

		do {
			*p2 = compressed_chr;
			p2++;
			size -= (p2 - p1);
			memmove(p2, p2 + 1, size);
			p1 = p2;
			len--;

			if (size < 2) break;

			/* find the token on the symbol */
			p2 = find_token(p1, size, str);

		} while (p2);

		table[sym_idx]->len = len;

		/* increase the counts for this symbol's new tokens */
		learn_symbol(table[sym_idx]->sym, len);
		symbol_index_new_tokens(table[sym_idx]->sym, len, sym_idx,
					compressed_chr);
	}

	sym_arr_free(arr); /* No symbol contains this token any more. */
}

/* search the token with the maximum profit */
static int find_best_token(void)
{
	int i, best, bestprofit;

	bestprofit=-10000;
	best = 0;

	for (i = 0; i < ARRAY_SIZE(token_profit); i++) {
		if (token_profit[i] > bestprofit) {
			best = i;
			bestprofit = token_profit[i];
		}
	}
	return best;
}

/* this is the core of the algorithm: calculate the "best" table */
static void optimize_result(void)
{
	int i, best;

	/* using the '\0' symbol last allows compress_symbols to use standard
	 * fast string functions */
	for (i = 255; i >= 0; i--) {

		/* if this table slot is empty (it is not used by an actual
		 * original char code */
		if (!best_table_len[i]) {

			/* find the token with the best profit value */
			best = find_best_token();
			if (token_profit[best] == 0)
				break;

			/* place it in the "best" table */
			best_table_len[i] = 2;
			best_table[i][0] = best & 0xFF;
			best_table[i][1] = (best >> 8) & 0xFF;

			/* replace this token in all the valid symbols */
			compress_symbols(best_table[i], i);
		}
	}
}

/* start by placing the symbols that are actually used on the table */
static void insert_real_symbols_in_table(void)
{
	unsigned int i, j, c;

	for (i = 0; i < table_cnt; i++) {
		for (j = 0; j < table[i]->len; j++) {
			c = table[i]->sym[j];
			best_table[c][0]=c;
			best_table_len[c]=1;
		}
	}
}

static void optimize_token_table(void)
{
	build_initial_token_table();

	insert_real_symbols_in_table();

	optimize_result();
}

/* guess for "linker script provide" symbol */
static int may_be_linker_script_provide_symbol(const struct sym_entry *se)
{
	const char *symbol = sym_entry_name(se);
	int len = se->len - 1;

	if (len < 8)
		return 0;

	if (symbol[0] != '_' || symbol[1] != '_')
		return 0;

	/* __start_XXXXX */
	if (!memcmp(symbol + 2, "start_", 6))
		return 1;

	/* __stop_XXXXX */
	if (!memcmp(symbol + 2, "stop_", 5))
		return 1;

	/* __end_XXXXX */
	if (!memcmp(symbol + 2, "end_", 4))
		return 1;

	/* __XXXXX_start */
	if (!memcmp(symbol + len - 6, "_start", 6))
		return 1;

	/* __XXXXX_end */
	if (!memcmp(symbol + len - 4, "_end", 4))
		return 1;

	return 0;
}

static int compare_symbols(const void *a, const void *b)
{
	const struct sym_entry *sa = *(const struct sym_entry **)a;
	const struct sym_entry *sb = *(const struct sym_entry **)b;
	int wa, wb;

	/* sort by address first */
	if (sa->addr > sb->addr)
		return 1;
	if (sa->addr < sb->addr)
		return -1;

	/* sort by "weakness" type */
	wa = (sa->sym[0] == 'w') || (sa->sym[0] == 'W');
	wb = (sb->sym[0] == 'w') || (sb->sym[0] == 'W');
	if (wa != wb)
		return wa - wb;

	/* sort by "linker script provide" type */
	wa = may_be_linker_script_provide_symbol(sa);
	wb = may_be_linker_script_provide_symbol(sb);
	if (wa != wb)
		return wa - wb;

	/* sort by the number of prefix underscores */
	wa = strspn(sym_entry_name(sa), "_");
	wb = strspn(sym_entry_name(sb), "_");
	if (wa != wb)
		return wa - wb;

	/* sort by initial order, so that other symbols are left undisturbed */
	return sa->seq - sb->seq;
}

static void sort_symbols(void)
{
	qsort(table, table_cnt, sizeof(table[0]), compare_symbols);
}

int main(int argc, char **argv)
{
	const char *in, *sysmap = NULL, *out_bin_name;
	FILE *sysmap_out = NULL, *out_bin_file;

	while (1) {
		static const struct option long_options[] = {
			{"all-symbols",     no_argument, &all_symbols,     1},
			{"pc-relative",     no_argument, &pc_relative,     1},
			{"sysmap",    required_argument, NULL,           's'},
			{},
		};

		int c = getopt_long(argc, argv, "", long_options, NULL);

		if (c == -1)
			break;
		if (c == 's')
			sysmap = optarg;
		else if (c != 0)
			usage();
	}

	if (optind + 2 != argc && !(sysmap && optind + 1 == argc))
		usage();

	in = argv[optind];
	if (sysmap) {
		sysmap_out = fopen(sysmap, "w");
		if (!sysmap_out) {
			perror(sysmap);
			exit(EXIT_FAILURE);
		}
	}

	if (optind + 1 == argc) {
		read_elf(in, sysmap_out);
		if (ferror(sysmap_out) || fclose(sysmap_out)) {
			perror(sysmap);
			exit(EXIT_FAILURE);
		}
		return 0;
	}

	out_bin_name = argv[optind + 1];
	out_bin_file = fopen(out_bin_name, "w");
	if (!out_bin_file) {
		perror(out_bin_name);
		exit(EXIT_FAILURE);
	}

	read_elf(in, sysmap_out);
	if (sysmap_out && (ferror(sysmap_out) || fclose(sysmap_out))) {
		perror(sysmap);
		exit(EXIT_FAILURE);
	}
	shrink_table();
	sort_symbols();
	optimize_token_table();
	write_src(out_bin_file, out_bin_name);

	/* Write errors are latched in the stream, so one check at the end covers them all. */
	if (ferror(out_bin_file) || fclose(out_bin_file)) {
		perror(out_bin_name);
		exit(EXIT_FAILURE);
	}

	return 0;
}
