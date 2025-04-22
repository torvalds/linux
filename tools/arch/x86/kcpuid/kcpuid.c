// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <cpuid.h>
#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))
#define min(a, b)	(((a) < (b)) ? (a) : (b))
#define __noreturn	__attribute__((__noreturn__))

typedef unsigned int u32;
typedef unsigned long long u64;

char *def_csv = "/usr/share/misc/cpuid.csv";
char *user_csv;


/* Cover both single-bit flag and multiple-bits fields */
struct bits_desc {
	/* start and end bits */
	int start, end;
	/* 0 or 1 for 1-bit flag */
	int value;
	char simp[32];
	char detail[256];
};

/* descriptor info for eax/ebx/ecx/edx */
struct reg_desc {
	/* number of valid entries */
	int nr;
	struct bits_desc descs[32];
};

enum cpuid_reg {
	R_EAX = 0,
	R_EBX,
	R_ECX,
	R_EDX,
	NR_REGS
};

static const char * const reg_names[] = {
	"EAX", "EBX", "ECX", "EDX",
};

struct subleaf {
	u32 index;
	u32 sub;
	u32 output[NR_REGS];
	struct reg_desc info[NR_REGS];
};

/* Represent one leaf (basic or extended) */
struct cpuid_func {
	/*
	 * Array of subleafs for this func, if there is no subleafs
	 * then the leafs[0] is the main leaf
	 */
	struct subleaf *leafs;
	int nr;
};

enum range_index {
	RANGE_STD = 0,			/* Standard */
	RANGE_EXT = 0x80000000,		/* Extended */
	RANGE_TSM = 0x80860000,		/* Transmeta */
	RANGE_CTR = 0xc0000000,		/* Centaur/Zhaoxin */
};

#define CPUID_INDEX_MASK		0xffff0000
#define CPUID_FUNCTION_MASK		(~CPUID_INDEX_MASK)

struct cpuid_range {
	/* array of main leafs */
	struct cpuid_func *funcs;
	/* number of valid leafs */
	int nr;
	enum range_index index;
};

static struct cpuid_range ranges[] = {
	{	.index		= RANGE_STD,	},
	{	.index		= RANGE_EXT,	},
	{	.index		= RANGE_TSM,	},
	{	.index		= RANGE_CTR,	},
};

static char *range_to_str(struct cpuid_range *range)
{
	switch (range->index) {
	case RANGE_STD:		return "Standard";
	case RANGE_EXT:		return "Extended";
	case RANGE_TSM:		return "Transmeta";
	case RANGE_CTR:		return "Centaur";
	default:		return NULL;
	}
}

#define __for_each_cpuid_range(range, __condition)				\
	for (unsigned int i = 0;						\
	     i < ARRAY_SIZE(ranges) && ((range) = &ranges[i]) && (__condition);	\
	     i++)

#define for_each_valid_cpuid_range(range)	__for_each_cpuid_range(range, (range)->nr != 0)
#define for_each_cpuid_range(range)		__for_each_cpuid_range(range, true)

struct cpuid_range *index_to_cpuid_range(u32 index)
{
	u32 func_idx = index & CPUID_FUNCTION_MASK;
	u32 range_idx = index & CPUID_INDEX_MASK;
	struct cpuid_range *range;

	for_each_valid_cpuid_range(range) {
		if (range->index == range_idx && (u32)range->nr > func_idx)
			return range;
	}

	return NULL;
}

static bool show_details;
static bool show_raw;
static bool show_flags_only = true;
static u32 user_index = 0xFFFFFFFF;
static u32 user_sub = 0xFFFFFFFF;
static int flines;

/*
 * Force using <cpuid.h> __cpuid_count() instead of __cpuid(). The
 * latter leaves ECX uninitialized, which can break CPUID queries.
 */

#define cpuid(leaf, a, b, c, d)				\
	__cpuid_count(leaf, 0, a, b, c, d)

#define cpuid_count(leaf, subleaf, a, b, c, d)		\
	__cpuid_count(leaf, subleaf, a, b, c, d)

static inline bool has_subleafs(u32 f)
{
	u32 with_subleaves[] = {
		0x4,  0x7,  0xb,  0xd,  0xf,  0x10, 0x12,
		0x14, 0x17, 0x18, 0x1b, 0x1d, 0x1f, 0x23,
		0x8000001d, 0x80000020, 0x80000026,
	};

	for (unsigned i = 0; i < ARRAY_SIZE(with_subleaves); i++)
		if (f == with_subleaves[i])
			return true;

	return false;
}

static void leaf_print_raw(struct subleaf *leaf)
{
	if (has_subleafs(leaf->index)) {
		if (leaf->sub == 0)
			printf("0x%08x: subleafs:\n", leaf->index);

		printf(" %2d: EAX=0x%08x, EBX=0x%08x, ECX=0x%08x, EDX=0x%08x\n", leaf->sub,
		       leaf->output[0], leaf->output[1], leaf->output[2], leaf->output[3]);
	} else {
		printf("0x%08x: EAX=0x%08x, EBX=0x%08x, ECX=0x%08x, EDX=0x%08x\n", leaf->index,
		       leaf->output[0], leaf->output[1], leaf->output[2], leaf->output[3]);
	}
}

/* Return true is the input eax/ebx/ecx/edx are all zero */
static bool cpuid_store(struct cpuid_range *range, u32 f, int subleaf,
			u32 a, u32 b, u32 c, u32 d)
{
	struct cpuid_func *func;
	struct subleaf *leaf;
	int s = 0;

	if (a == 0 && b == 0 && c == 0 && d == 0)
		return true;

	/*
	 * Cut off vendor-prefix from CPUID function as we're using it as an
	 * index into ->funcs.
	 */
	func = &range->funcs[f & CPUID_FUNCTION_MASK];

	if (!func->leafs) {
		func->leafs = malloc(sizeof(struct subleaf));
		if (!func->leafs)
			err(EXIT_FAILURE, NULL);

		func->nr = 1;
	} else {
		s = func->nr;
		func->leafs = realloc(func->leafs, (s + 1) * sizeof(*leaf));
		if (!func->leafs)
			err(EXIT_FAILURE, NULL);

		func->nr++;
	}

	leaf = &func->leafs[s];

	leaf->index = f;
	leaf->sub = subleaf;
	leaf->output[R_EAX] = a;
	leaf->output[R_EBX] = b;
	leaf->output[R_ECX] = c;
	leaf->output[R_EDX] = d;

	return false;
}

static void raw_dump_range(struct cpuid_range *range)
{
	printf("%s Leafs :\n", range_to_str(range));
	printf("================\n");

	for (u32 f = 0; (int)f < range->nr; f++) {
		struct cpuid_func *func = &range->funcs[f];

		/* Skip leaf without valid items */
		if (!func->nr)
			continue;

		/* First item is the main leaf, followed by all subleafs */
		for (int i = 0; i < func->nr; i++)
			leaf_print_raw(&func->leafs[i]);
	}
}

#define MAX_SUBLEAF_NUM		64
#define MAX_RANGE_INDEX_OFFSET	0xff
void setup_cpuid_range(struct cpuid_range *range)
{
	u32 max_func, range_funcs_sz;
	u32 eax, ebx, ecx, edx;

	cpuid(range->index, max_func, ebx, ecx, edx);

	/*
	 * If the CPUID range's maximum function value is garbage, then it
	 * is not recognized by this CPU.  Set the range's number of valid
	 * leaves to zero so that for_each_valid_cpu_range() can ignore it.
	 */
	if (max_func < range->index || max_func > (range->index + MAX_RANGE_INDEX_OFFSET)) {
		range->nr = 0;
		return;
	}

	range->nr = (max_func & CPUID_FUNCTION_MASK) + 1;
	range_funcs_sz = range->nr * sizeof(struct cpuid_func);

	range->funcs = malloc(range_funcs_sz);
	if (!range->funcs)
		err(EXIT_FAILURE, NULL);

	memset(range->funcs, 0, range_funcs_sz);

	for (u32 f = range->index; f <= max_func; f++) {
		u32 max_subleaf = MAX_SUBLEAF_NUM;
		bool allzero;

		cpuid(f, eax, ebx, ecx, edx);

		allzero = cpuid_store(range, f, 0, eax, ebx, ecx, edx);
		if (allzero)
			continue;

		if (!has_subleafs(f))
			continue;

		/*
		 * Some can provide the exact number of subleafs,
		 * others have to be tried (0xf)
		 */
		if (f == 0x7 || f == 0x14 || f == 0x17 || f == 0x18 || f == 0x1d)
			max_subleaf = min((eax & 0xff) + 1, max_subleaf);
		if (f == 0xb)
			max_subleaf = 2;
		if (f == 0x1f)
			max_subleaf = 6;
		if (f == 0x23)
			max_subleaf = 4;
		if (f == 0x80000020)
			max_subleaf = 4;
		if (f == 0x80000026)
			max_subleaf = 5;

		for (u32 subleaf = 1; subleaf < max_subleaf; subleaf++) {
			cpuid_count(f, subleaf, eax, ebx, ecx, edx);

			allzero = cpuid_store(range, f, subleaf, eax, ebx, ecx, edx);
			if (allzero)
				continue;
		}

	}
}

/*
 * The basic row format for cpuid.csv  is
 *	LEAF,SUBLEAF,register_name,bits,short name,long description
 *
 * like:
 *	0,    0,  EAX,   31:0, max_basic_leafs,  Max input value for supported subleafs
 *	1,    0,  ECX,      0, sse3,  Streaming SIMD Extensions 3(SSE3)
 */
static void parse_line(char *line)
{
	char *str;
	struct cpuid_range *range;
	struct cpuid_func *func;
	struct subleaf *leaf;
	u32 index;
	char buffer[512];
	char *buf;
	/*
	 * Tokens:
	 *  1. leaf
	 *  2. subleaf
	 *  3. register
	 *  4. bits
	 *  5. short name
	 *  6. long detail
	 */
	char *tokens[6];
	struct reg_desc *reg;
	struct bits_desc *bdesc;
	int reg_index;
	char *start, *end;
	u32 subleaf_start, subleaf_end;
	unsigned bit_start, bit_end;

	/* Skip comments and NULL line */
	if (line[0] == '#' || line[0] == '\n')
		return;

	strncpy(buffer, line, 511);
	buffer[511] = 0;
	str = buffer;
	for (int i = 0; i < 5; i++) {
		tokens[i] = strtok(str, ",");
		if (!tokens[i])
			goto err_exit;
		str = NULL;
	}
	tokens[5] = strtok(str, "\n");
	if (!tokens[5])
		goto err_exit;

	/* index/main-leaf */
	index = strtoull(tokens[0], NULL, 0);

	/*
	 * Skip line parsing if the index is not covered by known-valid
	 * CPUID ranges on this CPU.
	 */
	range = index_to_cpuid_range(index);
	if (!range)
		return;

	/* Skip line parsing if the index CPUID output is all zero */
	index &= CPUID_FUNCTION_MASK;
	func = &range->funcs[index];
	if (!func->nr)
		return;

	/* subleaf */
	buf = tokens[1];
	end = strtok(buf, ":");
	start = strtok(NULL, ":");
	subleaf_end = strtoul(end, NULL, 0);

	/* A subleaf range is given? */
	if (start) {
		subleaf_start = strtoul(start, NULL, 0);
		subleaf_end = min(subleaf_end, (u32)(func->nr - 1));
		if (subleaf_start > subleaf_end)
			return;
	} else {
		subleaf_start = subleaf_end;
		if (subleaf_start > (u32)(func->nr - 1))
			return;
	}

	/* register */
	buf = tokens[2];
	if (strcasestr(buf, "EAX"))
		reg_index = R_EAX;
	else if (strcasestr(buf, "EBX"))
		reg_index = R_EBX;
	else if (strcasestr(buf, "ECX"))
		reg_index = R_ECX;
	else if (strcasestr(buf, "EDX"))
		reg_index = R_EDX;
	else
		goto err_exit;

	/* bit flag or bits field */
	buf = tokens[3];
	end = strtok(buf, ":");
	start = strtok(NULL, ":");
	bit_end = strtoul(end, NULL, 0);
	bit_start = (start) ? strtoul(start, NULL, 0) : bit_end;

	for (u32 sub = subleaf_start; sub <= subleaf_end; sub++) {
		leaf = &func->leafs[sub];
		reg = &leaf->info[reg_index];
		bdesc = &reg->descs[reg->nr++];

		bdesc->end = bit_end;
		bdesc->start = bit_start;
		strcpy(bdesc->simp, strtok(tokens[4], " \t"));
		strcpy(bdesc->detail, tokens[5]);
	}
	return;

err_exit:
	warnx("Wrong line format:\n"
	      "\tline[%d]: %s", flines, line);
}

/* Parse csv file, and construct the array of all leafs and subleafs */
static void parse_text(void)
{
	FILE *file;
	char *filename, *line = NULL;
	size_t len = 0;
	int ret;

	if (show_raw)
		return;

	filename = user_csv ? user_csv : def_csv;
	file = fopen(filename, "r");
	if (!file) {
		/* Fallback to a csv in the same dir */
		file = fopen("./cpuid.csv", "r");
	}

	if (!file)
		err(EXIT_FAILURE, "%s", filename);

	while (1) {
		ret = getline(&line, &len, file);
		flines++;
		if (ret > 0)
			parse_line(line);

		if (feof(file))
			break;
	}

	fclose(file);
}

static void show_reg(const struct reg_desc *rdesc, u32 value)
{
	const struct bits_desc *bdesc;
	int start, end;
	u32 mask;

	for (int i = 0; i < rdesc->nr; i++) {
		bdesc = &rdesc->descs[i];

		start = bdesc->start;
		end = bdesc->end;
		if (start == end) {
			/* single bit flag */
			if (value & (1 << start))
				printf("\t%-20s %s%s%s\n",
					bdesc->simp,
				        show_flags_only ? "" : "\t\t\t",
					show_details ? "-" : "",
					show_details ? bdesc->detail : ""
					);
		} else {
			/* bit fields */
			if (show_flags_only)
				continue;

			mask = ((u64)1 << (end - start + 1)) - 1;
			printf("\t%-20s\t: 0x%-8x\t%s%s\n",
					bdesc->simp,
					(value >> start) & mask,
					show_details ? "-" : "",
					show_details ? bdesc->detail : ""
					);
		}
	}
}

static void show_reg_header(bool has_entries, u32 leaf, u32 subleaf, const char *reg_name)
{
	if (show_details && has_entries)
		printf("CPUID_0x%x_%s[0x%x]:\n", leaf, reg_name, subleaf);
}

static void show_leaf(struct subleaf *leaf)
{
	if (show_raw)
		leaf_print_raw(leaf);

	for (int i = R_EAX; i < NR_REGS; i++) {
		show_reg_header((leaf->info[i].nr > 0), leaf->index, leaf->sub, reg_names[i]);
		show_reg(&leaf->info[i], leaf->output[i]);
	}

	if (!show_raw && show_details)
		printf("\n");
}

static void show_func(struct cpuid_func *func)
{
	for (int i = 0; i < func->nr; i++)
		show_leaf(&func->leafs[i]);
}

static void show_range(struct cpuid_range *range)
{
	for (int i = 0; i < range->nr; i++)
		show_func(&range->funcs[i]);
}

static inline struct cpuid_func *index_to_func(u32 index)
{
	u32 func_idx = index & CPUID_FUNCTION_MASK;
	struct cpuid_range *range;

	range = index_to_cpuid_range(index);
	if (!range)
		return NULL;

	return &range->funcs[func_idx];
}

static void show_info(void)
{
	struct cpuid_range *range;
	struct cpuid_func *func;

	if (show_raw) {
		/* Show all of the raw output of 'cpuid' instr */
		for_each_valid_cpuid_range(range)
			raw_dump_range(range);
		return;
	}

	if (user_index != 0xFFFFFFFF) {
		/* Only show specific leaf/subleaf info */
		func = index_to_func(user_index);
		if (!func)
			errx(EXIT_FAILURE, "Invalid input leaf (0x%x)", user_index);

		/* Dump the raw data also */
		show_raw = true;

		if (user_sub != 0xFFFFFFFF) {
			if (user_sub + 1 > (u32)func->nr) {
				errx(EXIT_FAILURE, "Leaf 0x%x has no valid subleaf = 0x%x",
				     user_index, user_sub);
			}

			show_leaf(&func->leafs[user_sub]);
			return;
		}

		show_func(func);
		return;
	}

	printf("CPU features:\n=============\n\n");
	for_each_valid_cpuid_range(range)
		show_range(range);
}

static void __noreturn usage(int exit_code)
{
	errx(exit_code, "kcpuid [-abdfhr] [-l leaf] [-s subleaf]\n"
	     "\t-a|--all             Show both bit flags and complex bit fields info\n"
	     "\t-b|--bitflags        Show boolean flags only\n"
	     "\t-d|--detail          Show details of the flag/fields (default)\n"
	     "\t-f|--flags           Specify the CPUID CSV file\n"
	     "\t-h|--help            Show usage info\n"
	     "\t-l|--leaf=index      Specify the leaf you want to check\n"
	     "\t-r|--raw             Show raw CPUID data\n"
	     "\t-s|--subleaf=sub     Specify the subleaf you want to check"
	);
}

static struct option opts[] = {
	{ "all", no_argument, NULL, 'a' },		/* show both bit flags and fields */
	{ "bitflags", no_argument, NULL, 'b' },		/* only show bit flags, default on */
	{ "detail", no_argument, NULL, 'd' },		/* show detail descriptions */
	{ "file", required_argument, NULL, 'f' },	/* use user's cpuid file */
	{ "help", no_argument, NULL, 'h'},		/* show usage */
	{ "leaf", required_argument, NULL, 'l'},	/* only check a specific leaf */
	{ "raw", no_argument, NULL, 'r'},		/* show raw CPUID leaf data */
	{ "subleaf", required_argument, NULL, 's'},	/* check a specific subleaf */
	{ NULL, 0, NULL, 0 }
};

static void parse_options(int argc, char *argv[])
{
	int c;

	while ((c = getopt_long(argc, argv, "abdf:hl:rs:",
					opts, NULL)) != -1)
		switch (c) {
		case 'a':
			show_flags_only = false;
			break;
		case 'b':
			show_flags_only = true;
			break;
		case 'd':
			show_details = true;
			break;
		case 'f':
			user_csv = optarg;
			break;
		case 'h':
			usage(EXIT_SUCCESS);
		case 'l':
			/* main leaf */
			user_index = strtoul(optarg, NULL, 0);
			break;
		case 'r':
			show_raw = true;
			break;
		case 's':
			/* subleaf */
			user_sub = strtoul(optarg, NULL, 0);
			break;
		default:
			usage(EXIT_FAILURE);
		}
}

/*
 * Do 4 things in turn:
 * 1. Parse user options
 * 2. Parse and store all the CPUID leaf data supported on this platform
 * 2. Parse the csv file, while skipping leafs which are not available
 *    on this platform
 * 3. Print leafs info based on user options
 */
int main(int argc, char *argv[])
{
	struct cpuid_range *range;

	parse_options(argc, argv);

	/* Setup the cpuid leafs of current platform */
	for_each_cpuid_range(range)
		setup_cpuid_range(range);

	/* Read and parse the 'cpuid.csv' */
	parse_text();

	show_info();
	return 0;
}
