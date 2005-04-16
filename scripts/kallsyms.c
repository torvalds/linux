/* Generate assembler source containing symbol information
 *
 * Copyright 2002       by Kai Germaschewski
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Usage: nm -n vmlinux | scripts/kallsyms [--all-symbols] > symbols.S
 *
 * ChangeLog:
 *
 * (25/Aug/2004) Paulo Marques <pmarques@grupopie.com>
 *      Changed the compression method from stem compression to "table lookup"
 *      compression
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* maximum token length used. It doesn't pay to increase it a lot, because
 * very long substrings probably don't repeat themselves too often. */
#define MAX_TOK_SIZE		11
#define KSYM_NAME_LEN		127

/* we use only a subset of the complete symbol table to gather the token count,
 * to speed up compression, at the expense of a little compression ratio */
#define WORKING_SET		1024

/* first find the best token only on the list of tokens that would profit more
 * than GOOD_BAD_THRESHOLD. Only if this list is empty go to the "bad" list.
 * Increasing this value will put less tokens on the "good" list, so the search
 * is faster. However, if the good list runs out of tokens, we must painfully
 * search the bad list. */
#define GOOD_BAD_THRESHOLD	10

/* token hash parameters */
#define HASH_BITS		18
#define HASH_TABLE_SIZE		(1 << HASH_BITS)
#define HASH_MASK		(HASH_TABLE_SIZE - 1)
#define HASH_BASE_OFFSET	2166136261U
#define HASH_FOLD(a)		((a)&(HASH_MASK))

/* flags to mark symbols */
#define SYM_FLAG_VALID		1
#define SYM_FLAG_SAMPLED	2

struct sym_entry {
	unsigned long long addr;
	char type;
	unsigned char flags;
	unsigned char len;
	unsigned char *sym;
};


static struct sym_entry *table;
static int size, cnt;
static unsigned long long _stext, _etext, _sinittext, _einittext;
static int all_symbols = 0;

struct token {
	unsigned char data[MAX_TOK_SIZE];
	unsigned char len;
	/* profit: the number of bytes that could be saved by inserting this
	 * token into the table */
	int profit;
	struct token *next;	/* next token on the hash list */
	struct token *right;	/* next token on the good/bad list */
	struct token *left;    /* previous token on the good/bad list */
	struct token *smaller; /* token that is less one letter than this one */
	};

struct token bad_head, good_head;
struct token *hash_table[HASH_TABLE_SIZE];

/* the table that holds the result of the compression */
unsigned char best_table[256][MAX_TOK_SIZE+1];
unsigned char best_table_len[256];


static void
usage(void)
{
	fprintf(stderr, "Usage: kallsyms [--all-symbols] < in.map > out.S\n");
	exit(1);
}

/*
 * This ignores the intensely annoying "mapping symbols" found
 * in ARM ELF files: $a, $t and $d.
 */
static inline int
is_arm_mapping_symbol(const char *str)
{
	return str[0] == '$' && strchr("atd", str[1])
	       && (str[2] == '\0' || str[2] == '.');
}

static int
read_symbol(FILE *in, struct sym_entry *s)
{
	char str[500];
	int rc;

	rc = fscanf(in, "%llx %c %499s\n", &s->addr, &s->type, str);
	if (rc != 3) {
		if (rc != EOF) {
			/* skip line */
			fgets(str, 500, in);
		}
		return -1;
	}

	/* Ignore most absolute/undefined (?) symbols. */
	if (strcmp(str, "_stext") == 0)
		_stext = s->addr;
	else if (strcmp(str, "_etext") == 0)
		_etext = s->addr;
	else if (strcmp(str, "_sinittext") == 0)
		_sinittext = s->addr;
	else if (strcmp(str, "_einittext") == 0)
		_einittext = s->addr;
	else if (toupper(s->type) == 'A')
	{
		/* Keep these useful absolute symbols */
		if (strcmp(str, "__kernel_syscall_via_break") &&
		    strcmp(str, "__kernel_syscall_via_epc") &&
		    strcmp(str, "__kernel_sigtramp") &&
		    strcmp(str, "__gp"))
			return -1;

	}
	else if (toupper(s->type) == 'U' ||
		 is_arm_mapping_symbol(str))
		return -1;

	/* include the type field in the symbol name, so that it gets
	 * compressed together */
	s->len = strlen(str) + 1;
	s->sym = (char *) malloc(s->len + 1);
	strcpy(s->sym + 1, str);
	s->sym[0] = s->type;

	return 0;
}

static int
symbol_valid(struct sym_entry *s)
{
	/* Symbols which vary between passes.  Passes 1 and 2 must have
	 * identical symbol lists.  The kallsyms_* symbols below are only added
	 * after pass 1, they would be included in pass 2 when --all-symbols is
	 * specified so exclude them to get a stable symbol list.
	 */
	static char *special_symbols[] = {
		"kallsyms_addresses",
		"kallsyms_num_syms",
		"kallsyms_names",
		"kallsyms_markers",
		"kallsyms_token_table",
		"kallsyms_token_index",

	/* Exclude linker generated symbols which vary between passes */
		"_SDA_BASE_",		/* ppc */
		"_SDA2_BASE_",		/* ppc */
		NULL };
	int i;

	/* if --all-symbols is not specified, then symbols outside the text
	 * and inittext sections are discarded */
	if (!all_symbols) {
		if ((s->addr < _stext || s->addr > _etext)
		    && (s->addr < _sinittext || s->addr > _einittext))
			return 0;
		/* Corner case.  Discard any symbols with the same value as
		 * _etext or _einittext, they can move between pass 1 and 2
		 * when the kallsyms data is added.  If these symbols move then
		 * they may get dropped in pass 2, which breaks the kallsyms
		 * rules.
		 */
		if ((s->addr == _etext && strcmp(s->sym + 1, "_etext")) ||
		    (s->addr == _einittext && strcmp(s->sym + 1, "_einittext")))
			return 0;
	}

	/* Exclude symbols which vary between passes. */
	if (strstr(s->sym + 1, "_compiled."))
		return 0;

	for (i = 0; special_symbols[i]; i++)
		if( strcmp(s->sym + 1, special_symbols[i]) == 0 )
			return 0;

	return 1;
}

static void
read_map(FILE *in)
{
	while (!feof(in)) {
		if (cnt >= size) {
			size += 10000;
			table = realloc(table, sizeof(*table) * size);
			if (!table) {
				fprintf(stderr, "out of memory\n");
				exit (1);
			}
		}
		if (read_symbol(in, &table[cnt]) == 0)
			cnt++;
	}
}

static void output_label(char *label)
{
	printf(".globl %s\n",label);
	printf("\tALGN\n");
	printf("%s:\n",label);
}

/* uncompress a compressed symbol. When this function is called, the best table
 * might still be compressed itself, so the function needs to be recursive */
static int expand_symbol(unsigned char *data, int len, char *result)
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

static void
write_src(void)
{
	int i, k, off, valid;
	unsigned int best_idx[256];
	unsigned int *markers;
	char buf[KSYM_NAME_LEN+1];

	printf("#include <asm/types.h>\n");
	printf("#if BITS_PER_LONG == 64\n");
	printf("#define PTR .quad\n");
	printf("#define ALGN .align 8\n");
	printf("#else\n");
	printf("#define PTR .long\n");
	printf("#define ALGN .align 4\n");
	printf("#endif\n");

	printf(".data\n");

	output_label("kallsyms_addresses");
	valid = 0;
	for (i = 0; i < cnt; i++) {
		if (table[i].flags & SYM_FLAG_VALID) {
			printf("\tPTR\t%#llx\n", table[i].addr);
			valid++;
		}
	}
	printf("\n");

	output_label("kallsyms_num_syms");
	printf("\tPTR\t%d\n", valid);
	printf("\n");

	/* table of offset markers, that give the offset in the compressed stream
	 * every 256 symbols */
	markers = (unsigned int *) malloc(sizeof(unsigned int)*((valid + 255) / 256));

	output_label("kallsyms_names");
	valid = 0;
	off = 0;
	for (i = 0; i < cnt; i++) {

		if (!table[i].flags & SYM_FLAG_VALID)
			continue;

		if ((valid & 0xFF) == 0)
			markers[valid >> 8] = off;

		printf("\t.byte 0x%02x", table[i].len);
		for (k = 0; k < table[i].len; k++)
			printf(", 0x%02x", table[i].sym[k]);
		printf("\n");

		off += table[i].len + 1;
		valid++;
	}
	printf("\n");

	output_label("kallsyms_markers");
	for (i = 0; i < ((valid + 255) >> 8); i++)
		printf("\tPTR\t%d\n", markers[i]);
	printf("\n");

	free(markers);

	output_label("kallsyms_token_table");
	off = 0;
	for (i = 0; i < 256; i++) {
		best_idx[i] = off;
		expand_symbol(best_table[i],best_table_len[i],buf);
		printf("\t.asciz\t\"%s\"\n", buf);
		off += strlen(buf) + 1;
	}
	printf("\n");

	output_label("kallsyms_token_index");
	for (i = 0; i < 256; i++)
		printf("\t.short\t%d\n", best_idx[i]);
	printf("\n");
}


/* table lookup compression functions */

static inline unsigned int rehash_token(unsigned int hash, unsigned char data)
{
	return ((hash * 16777619) ^ data);
}

static unsigned int hash_token(unsigned char *data, int len)
{
	unsigned int hash=HASH_BASE_OFFSET;
	int i;

	for (i = 0; i < len; i++)
		hash = rehash_token(hash, data[i]);

	return HASH_FOLD(hash);
}

/* find a token given its data and hash value */
static struct token *find_token_hash(unsigned char *data, int len, unsigned int hash)
{
	struct token *ptr;

	ptr = hash_table[hash];

	while (ptr) {
		if ((ptr->len == len) && (memcmp(ptr->data, data, len) == 0))
			return ptr;
		ptr=ptr->next;
	}

	return NULL;
}

static inline void insert_token_in_group(struct token *head, struct token *ptr)
{
	ptr->right = head->right;
	ptr->right->left = ptr;
	head->right = ptr;
	ptr->left = head;
}

static inline void remove_token_from_group(struct token *ptr)
{
	ptr->left->right = ptr->right;
	ptr->right->left = ptr->left;
}


/* build the counts for all the tokens that start with "data", and have lenghts
 * from 2 to "len" */
static void learn_token(unsigned char *data, int len)
{
	struct token *ptr,*last_ptr;
	int i, newprofit;
	unsigned int hash = HASH_BASE_OFFSET;
	unsigned int hashes[MAX_TOK_SIZE + 1];

	if (len > MAX_TOK_SIZE)
		len = MAX_TOK_SIZE;

	/* calculate and store the hash values for all the sub-tokens */
	hash = rehash_token(hash, data[0]);
	for (i = 2; i <= len; i++) {
		hash = rehash_token(hash, data[i-1]);
		hashes[i] = HASH_FOLD(hash);
	}

	last_ptr = NULL;
	ptr = NULL;

	for (i = len; i >= 2; i--) {
		hash = hashes[i];

		if (!ptr) ptr = find_token_hash(data, i, hash);

		if (!ptr) {
			/* create a new token entry */
			ptr = (struct token *) malloc(sizeof(*ptr));

			memcpy(ptr->data, data, i);
			ptr->len = i;

			/* when we create an entry, it's profit is 0 because
			 * we also take into account the size of the token on
			 * the compressed table. We then subtract GOOD_BAD_THRESHOLD
			 * so that the test to see if this token belongs to
			 * the good or bad list, is a comparison to zero */
			ptr->profit = -GOOD_BAD_THRESHOLD;

			ptr->next = hash_table[hash];
			hash_table[hash] = ptr;

			insert_token_in_group(&bad_head, ptr);

			ptr->smaller = NULL;
		} else {
			newprofit = ptr->profit + (ptr->len - 1);
			/* check to see if this token needs to be moved to a
			 * different list */
			if((ptr->profit < 0) && (newprofit >= 0)) {
				remove_token_from_group(ptr);
				insert_token_in_group(&good_head,ptr);
			}
			ptr->profit = newprofit;
		}

		if (last_ptr) last_ptr->smaller = ptr;
		last_ptr = ptr;

		ptr = ptr->smaller;
	}
}

/* decrease the counts for all the tokens that start with "data", and have lenghts
 * from 2 to "len". This function is much simpler than learn_token because we have
 * more guarantees (tho tokens exist, the ->smaller pointer is set, etc.)
 * The two separate functions exist only because of compression performance */
static void forget_token(unsigned char *data, int len)
{
	struct token *ptr;
	int i, newprofit;
	unsigned int hash=0;

	if (len > MAX_TOK_SIZE) len = MAX_TOK_SIZE;

	hash = hash_token(data, len);
	ptr = find_token_hash(data, len, hash);

	for (i = len; i >= 2; i--) {

		newprofit = ptr->profit - (ptr->len - 1);
		if ((ptr->profit >= 0) && (newprofit < 0)) {
			remove_token_from_group(ptr);
			insert_token_in_group(&bad_head, ptr);
		}
		ptr->profit=newprofit;

		ptr=ptr->smaller;
	}
}

/* count all the possible tokens in a symbol */
static void learn_symbol(unsigned char *symbol, int len)
{
	int i;

	for (i = 0; i < len - 1; i++)
		learn_token(symbol + i, len - i);
}

/* decrease the count for all the possible tokens in a symbol */
static void forget_symbol(unsigned char *symbol, int len)
{
	int i;

	for (i = 0; i < len - 1; i++)
		forget_token(symbol + i, len - i);
}

/* set all the symbol flags and do the initial token count */
static void build_initial_tok_table(void)
{
	int i, use_it, valid;

	valid = 0;
	for (i = 0; i < cnt; i++) {
		table[i].flags = 0;
		if ( symbol_valid(&table[i]) ) {
			table[i].flags |= SYM_FLAG_VALID;
			valid++;
		}
	}

	use_it = 0;
	for (i = 0; i < cnt; i++) {

		/* subsample the available symbols. This method is almost like
		 * a Bresenham's algorithm to get uniformly distributed samples
		 * across the symbol table */
		if (table[i].flags & SYM_FLAG_VALID) {

			use_it += WORKING_SET;

			if (use_it >= valid) {
				table[i].flags |= SYM_FLAG_SAMPLED;
				use_it -= valid;
			}
		}
		if (table[i].flags & SYM_FLAG_SAMPLED)
			learn_symbol(table[i].sym, table[i].len);
	}
}

/* replace a given token in all the valid symbols. Use the sampled symbols
 * to update the counts */
static void compress_symbols(unsigned char *str, int tlen, int idx)
{
	int i, len, learn, size;
	unsigned char *p;

	for (i = 0; i < cnt; i++) {

		if (!(table[i].flags & SYM_FLAG_VALID)) continue;

		len = table[i].len;
		learn = 0;
		p = table[i].sym;

		do {
			/* find the token on the symbol */
			p = (unsigned char *) strstr((char *) p, (char *) str);
			if (!p) break;

			if (!learn) {
				/* if this symbol was used to count, decrease it */
				if (table[i].flags & SYM_FLAG_SAMPLED)
					forget_symbol(table[i].sym, len);
				learn = 1;
			}

			*p = idx;
			size = (len - (p - table[i].sym)) - tlen + 1;
			memmove(p + 1, p + tlen, size);
			p++;
			len -= tlen - 1;

		} while (size >= tlen);

		if(learn) {
			table[i].len = len;
			/* if this symbol was used to count, learn it again */
			if(table[i].flags & SYM_FLAG_SAMPLED)
				learn_symbol(table[i].sym, len);
		}
	}
}

/* search the token with the maximum profit */
static struct token *find_best_token(void)
{
	struct token *ptr,*best,*head;
	int bestprofit;

	bestprofit=-10000;

	/* failsafe: if the "good" list is empty search from the "bad" list */
	if(good_head.right == &good_head) head = &bad_head;
	else head = &good_head;

	ptr = head->right;
	best = NULL;
	while (ptr != head) {
		if (ptr->profit > bestprofit) {
			bestprofit = ptr->profit;
			best = ptr;
		}
		ptr = ptr->right;
	}

	return best;
}

/* this is the core of the algorithm: calculate the "best" table */
static void optimize_result(void)
{
	struct token *best;
	int i;

	/* using the '\0' symbol last allows compress_symbols to use standard
	 * fast string functions */
	for (i = 255; i >= 0; i--) {

		/* if this table slot is empty (it is not used by an actual
		 * original char code */
		if (!best_table_len[i]) {

			/* find the token with the breates profit value */
			best = find_best_token();

			/* place it in the "best" table */
			best_table_len[i] = best->len;
			memcpy(best_table[i], best->data, best_table_len[i]);
			/* zero terminate the token so that we can use strstr
			   in compress_symbols */
			best_table[i][best_table_len[i]]='\0';

			/* replace this token in all the valid symbols */
			compress_symbols(best_table[i], best_table_len[i], i);
		}
	}
}

/* start by placing the symbols that are actually used on the table */
static void insert_real_symbols_in_table(void)
{
	int i, j, c;

	memset(best_table, 0, sizeof(best_table));
	memset(best_table_len, 0, sizeof(best_table_len));

	for (i = 0; i < cnt; i++) {
		if (table[i].flags & SYM_FLAG_VALID) {
			for (j = 0; j < table[i].len; j++) {
				c = table[i].sym[j];
				best_table[c][0]=c;
				best_table_len[c]=1;
			}
		}
	}
}

static void optimize_token_table(void)
{
	memset(hash_table, 0, sizeof(hash_table));

	good_head.left = &good_head;
	good_head.right = &good_head;

	bad_head.left = &bad_head;
	bad_head.right = &bad_head;

	build_initial_tok_table();

	insert_real_symbols_in_table();

	optimize_result();
}


int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--all-symbols") == 0)
		all_symbols = 1;
	else if (argc != 1)
		usage();

	read_map(stdin);
	optimize_token_table();
	write_src();

	return 0;
}

