/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_ANNOTATE_H
#define __PERF_ANNOTATE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <asm/bug.h>
#include "symbol_conf.h"
#include "mutex.h"
#include "spark.h"
#include "hashmap.h"
#include "disasm.h"
#include "branch.h"

struct hist_browser_timer;
struct hist_entry;
struct map;
struct map_symbol;
struct addr_map_symbol;
struct option;
struct perf_sample;
struct evsel;
struct symbol;
struct annotated_data_type;

#define ANNOTATION__IPC_WIDTH 6
#define ANNOTATION__CYCLES_WIDTH 6
#define ANNOTATION__MINMAX_CYCLES_WIDTH 19
#define ANNOTATION__AVG_IPC_WIDTH 36
#define ANNOTATION__BR_CNTR_WIDTH 30
#define ANNOTATION_DUMMY_LEN	256

struct annotation_options {
	bool hide_src_code,
	     use_offset,
	     jump_arrows,
	     print_lines,
	     full_path,
	     show_linenr,
	     show_fileloc,
	     show_nr_jumps,
	     show_minmax_cycle,
	     show_asm_raw,
	     show_br_cntr,
	     annotate_src,
	     full_addr;
	u8   offset_level;
	int  min_pcnt;
	int  max_lines;
	int  context;
	char *objdump_path;
	char *disassembler_style;
	const char *prefix;
	const char *prefix_strip;
	unsigned int percent_type;
};

extern struct annotation_options annotate_opts;

enum {
	ANNOTATION__OFFSET_JUMP_TARGETS = 1,
	ANNOTATION__OFFSET_CALL,
	ANNOTATION__MAX_OFFSET_LEVEL,
};

#define ANNOTATION__MIN_OFFSET_LEVEL ANNOTATION__OFFSET_JUMP_TARGETS

struct annotation;

struct sym_hist_entry {
	u64		nr_samples;
	u64		period;
};

enum {
	PERCENT_HITS_LOCAL,
	PERCENT_HITS_GLOBAL,
	PERCENT_PERIOD_LOCAL,
	PERCENT_PERIOD_GLOBAL,
	PERCENT_MAX,
};

struct annotation_data {
	double			 percent[PERCENT_MAX];
	double			 percent_sum;
	struct sym_hist_entry	 he;
};

struct cycles_info {
	float			 ipc;
	u64			 avg;
	u64			 max;
	u64			 min;
};

struct annotation_line {
	struct list_head	 node;
	struct rb_node		 rb_node;
	s64			 offset;
	char			*line;
	int			 line_nr;
	char			*fileloc;
	char			*path;
	struct cycles_info	*cycles;
	int			 num_aggr;
	int			 br_cntr_nr;
	u64			*br_cntr;
	struct evsel		*evsel;
	int			 jump_sources;
	u32			 idx;
	int			 idx_asm;
	int			 data_nr;
	struct annotation_data	 data[];
};

struct disasm_line {
	struct ins		 ins;
	struct ins_operands	 ops;
	union {
		u8 bytes[4];
		u32 raw_insn;
	} raw;
	/* This needs to be at the end. */
	struct annotation_line	 al;
};

void annotation_line__add(struct annotation_line *al, struct list_head *head);

static inline double annotation_data__percent(struct annotation_data *data,
					      unsigned int which)
{
	return which < PERCENT_MAX ? data->percent[which] : -1;
}

static inline const char *percent_type_str(unsigned int type)
{
	static const char *str[PERCENT_MAX] = {
		"local hits",
		"global hits",
		"local period",
		"global period",
	};

	if (WARN_ON(type >= PERCENT_MAX))
		return "N/A";

	return str[type];
}

static inline struct disasm_line *disasm_line(struct annotation_line *al)
{
	return al ? container_of(al, struct disasm_line, al) : NULL;
}

/*
 * Is this offset in the same function as the line it is used?
 * asm functions jump to other functions, for instance.
 */
static inline bool disasm_line__has_local_offset(const struct disasm_line *dl)
{
	return dl->ops.target.offset_avail && !dl->ops.target.outside;
}

/*
 * Can we draw an arrow from the jump to its target, for instance? I.e.
 * is the jump and its target in the same function?
 */
bool disasm_line__is_valid_local_jump(struct disasm_line *dl, struct symbol *sym);

struct annotation_line *
annotation_line__next(struct annotation_line *pos, struct list_head *head);

struct annotation_write_ops {
	bool first_line, current_entry, change_color;
	int  width;
	void *obj;
	int  (*set_color)(void *obj, int color);
	void (*set_percent_color)(void *obj, double percent, bool current);
	int  (*set_jumps_percent_color)(void *obj, int nr, bool current);
	void (*printf)(void *obj, const char *fmt, ...);
	void (*write_graph)(void *obj, int graph);
};

void annotation_line__write(struct annotation_line *al, struct annotation *notes,
			    struct annotation_write_ops *ops);

int __annotation__scnprintf_samples_period(struct annotation *notes,
					   char *bf, size_t size,
					   struct evsel *evsel,
					   bool show_freq);

size_t disasm__fprintf(struct list_head *head, FILE *fp);
void symbol__calc_percent(struct symbol *sym, struct evsel *evsel);

/**
 * struct sym_hist - symbol histogram information for an event
 *
 * @nr_samples: Total number of samples.
 * @period: Sum of sample periods.
 */
struct sym_hist {
	u64		      nr_samples;
	u64		      period;
};

/**
 * struct cyc_hist - (CPU) cycle histogram for a basic block
 *
 * @start: Start address of current block (if known).
 * @cycles: Sum of cycles for the longest basic block.
 * @cycles_aggr: Total cycles for this address.
 * @cycles_max: Max cycles for this address.
 * @cycles_min: Min cycles for this address.
 * @cycles_spark: History of cycles for the longest basic block.
 * @num: Number of samples for the longest basic block.
 * @num_aggr: Total number of samples for this address.
 * @have_start: Whether the current branch info has a start address.
 * @reset: Number of resets due to a different start address.
 *
 * If sample has branch_stack and cycles info, it can construct basic blocks
 * between two adjacent branches.  It'd have start and end addresses but
 * sometimes the start address may not be available.  So the cycles are
 * accounted at the end address.  If multiple basic blocks end at the same
 * address, it will take the longest one.
 *
 * The @start, @cycles, @cycles_spark and @num fields are used for the longest
 * block only.  Other fields are used for all cases.
 *
 * See __symbol__account_cycles().
 */
struct cyc_hist {
	u64	start;
	u64	cycles;
	u64	cycles_aggr;
	u64	cycles_max;
	u64	cycles_min;
	s64	cycles_spark[NUM_SPARKS];
	u32	num;
	u32	num_aggr;
	u8	have_start;
	/* 1 byte padding */
	u16	reset;
};

/**
 * struct annotated_source - symbols with hits have this attached as in annotation
 *
 * @source: List head for annotated_line (embeded in disasm_line).
 * @histograms: Array of symbol histograms per event to maintain the total number
 * 		of samples and period.
 * @nr_histograms: This may not be the same as evsel->evlist->core.nr_entries if
 * 		  we have more than a group in a evlist, where we will want
 * 		  to see each group separately, that is why symbol__annotate2()
 * 		  sets src->nr_histograms to evsel->nr_members.
 * @samples: Hash map of sym_hist_entry.  Keyed by event index and offset in symbol.
 * @nr_events: Number of events in the current output.
 * @nr_entries: Number of annotated_line in the source list.
 * @nr_asm_entries: Number of annotated_line with actual asm instruction in the
 * 		    source list.
 * @max_jump_sources: Maximum number of jump instructions targeting to the same
 * 		      instruction.
 * @widths: Precalculated width of each column in the TUI output.
 *
 * disasm_lines are allocated, percentages calculated and all sorted by percentage
 * when the annotation is about to be presented, so the percentages are for
 * one of the entries in the histogram array, i.e. for the event/counter being
 * presented. It is deallocated right after symbol__{tui,tty,etc}_annotate
 * returns.
 */
struct annotated_source {
	struct list_head	source;
	struct sym_hist		*histograms;
	struct hashmap	   	*samples;
	int    			nr_histograms;
	int    			nr_events;
	int			nr_entries;
	int			nr_asm_entries;
	int			max_jump_sources;
	u64			start;
	struct {
		u8		addr;
		u8		jumps;
		u8		target;
		u8		min_addr;
		u8		max_addr;
		u8		max_ins_name;
		u16		max_line_len;
	} widths;
};

struct annotation_line *annotated_source__get_line(struct annotated_source *src,
						   s64 offset);

/* A branch counter once saturated */
#define ANNOTATION__BR_CNTR_SATURATED_FLAG	(1ULL << 63)

/**
 * struct annotated_branch - basic block and IPC information for a symbol.
 *
 * @hit_cycles: Total executed cycles.
 * @hit_insn: Total number of instructions executed.
 * @total_insn: Number of instructions in the function.
 * @cover_insn: Number of distinct, actually executed instructions.
 * @cycles_hist: Array of cyc_hist for each instruction.
 * @max_coverage: Maximum number of covered basic block (used for block-range).
 * @br_cntr: Array of the occurrences of events (branch counters) during a block.
 *
 * This struct is used by two different codes when the sample has branch stack
 * and cycles information.  annotation__compute_ipc() calculates average IPC
 * using @hit_insn / @hit_cycles.  The actual coverage can be calculated using
 * @cover_insn / @total_insn.  The @cycles_hist can give IPC for each (longest)
 * basic block ends at the given address.
 * process_basic_block() calculates coverage of instructions (or basic blocks)
 * in the function.
 */
struct annotated_branch {
	u64			hit_cycles;
	u64			hit_insn;
	unsigned int		total_insn;
	unsigned int		cover_insn;
	struct cyc_hist		*cycles_hist;
	u64			max_coverage;
	u64			*br_cntr;
};

struct LOCKABLE annotation {
	struct annotated_source *src;
	struct annotated_branch *branch;
};

static inline void annotation__init(struct annotation *notes __maybe_unused)
{
}
void annotation__exit(struct annotation *notes);

void annotation__lock(struct annotation *notes) EXCLUSIVE_LOCK_FUNCTION(*notes);
void annotation__unlock(struct annotation *notes) UNLOCK_FUNCTION(*notes);
bool annotation__trylock(struct annotation *notes) EXCLUSIVE_TRYLOCK_FUNCTION(true, *notes);

static inline int annotation__cycles_width(struct annotation *notes)
{
	if (notes->branch && annotate_opts.show_minmax_cycle)
		return ANNOTATION__IPC_WIDTH + ANNOTATION__MINMAX_CYCLES_WIDTH;

	return notes->branch ? ANNOTATION__IPC_WIDTH + ANNOTATION__CYCLES_WIDTH : 0;
}

static inline int annotation__pcnt_width(struct annotation *notes)
{
	return (symbol_conf.show_total_period ? 12 : 8) * notes->src->nr_events;
}

static inline bool annotation_line__filter(struct annotation_line *al)
{
	return annotate_opts.hide_src_code && al->offset == -1;
}

static inline u8 annotation__br_cntr_width(void)
{
	return annotate_opts.show_br_cntr ? ANNOTATION__BR_CNTR_WIDTH : 0;
}

void annotation__update_column_widths(struct annotation *notes);
void annotation__toggle_full_addr(struct annotation *notes, struct map_symbol *ms);

static inline struct sym_hist *annotated_source__histogram(struct annotated_source *src, int idx)
{
	return &src->histograms[idx];
}

static inline struct sym_hist *annotation__histogram(struct annotation *notes, int idx)
{
	return annotated_source__histogram(notes->src, idx);
}

static inline struct sym_hist_entry *
annotated_source__hist_entry(struct annotated_source *src, int idx, u64 offset)
{
	struct sym_hist_entry *entry;
	long key = offset << 16 | idx;

	if (!hashmap__find(src->samples, key, &entry))
		return NULL;
	return entry;
}

static inline struct annotation *symbol__annotation(struct symbol *sym)
{
	return (void *)sym - symbol_conf.priv_size;
}

int addr_map_symbol__inc_samples(struct addr_map_symbol *ams, struct perf_sample *sample,
				 struct evsel *evsel);

struct annotated_branch *annotation__get_branch(struct annotation *notes);

int addr_map_symbol__account_cycles(struct addr_map_symbol *ams,
				    struct addr_map_symbol *start,
				    unsigned cycles,
				    struct evsel *evsel,
				    u64 br_cntr);

int hist_entry__inc_addr_samples(struct hist_entry *he, struct perf_sample *sample,
				 struct evsel *evsel, u64 addr);

struct annotated_source *symbol__hists(struct symbol *sym, int nr_hists);
void symbol__annotate_zero_histograms(struct symbol *sym);

int symbol__annotate(struct map_symbol *ms,
		     struct evsel *evsel,
		     struct arch **parch);
int symbol__annotate2(struct map_symbol *ms,
		      struct evsel *evsel,
		      struct arch **parch);

enum symbol_disassemble_errno {
	SYMBOL_ANNOTATE_ERRNO__SUCCESS		= 0,

	/*
	 * Choose an arbitrary negative big number not to clash with standard
	 * errno since SUS requires the errno has distinct positive values.
	 * See 'Issue 6' in the link below.
	 *
	 * http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/errno.h.html
	 */
	__SYMBOL_ANNOTATE_ERRNO__START		= -10000,

	SYMBOL_ANNOTATE_ERRNO__NO_VMLINUX	= __SYMBOL_ANNOTATE_ERRNO__START,
	SYMBOL_ANNOTATE_ERRNO__NO_LIBOPCODES_FOR_BPF,
	SYMBOL_ANNOTATE_ERRNO__ARCH_INIT_CPUID_PARSING,
	SYMBOL_ANNOTATE_ERRNO__ARCH_INIT_REGEXP,
	SYMBOL_ANNOTATE_ERRNO__BPF_INVALID_FILE,
	SYMBOL_ANNOTATE_ERRNO__BPF_MISSING_BTF,

	__SYMBOL_ANNOTATE_ERRNO__END,
};

int symbol__strerror_disassemble(struct map_symbol *ms, int errnum, char *buf, size_t buflen);

int symbol__annotate_printf(struct map_symbol *ms, struct evsel *evsel);
void symbol__annotate_zero_histogram(struct symbol *sym, int evidx);
void symbol__annotate_decay_histogram(struct symbol *sym, int evidx);
void annotated_source__purge(struct annotated_source *as);

int map_symbol__annotation_dump(struct map_symbol *ms, struct evsel *evsel);

bool ui__has_annotation(void);

int symbol__tty_annotate(struct map_symbol *ms, struct evsel *evsel);

int symbol__tty_annotate2(struct map_symbol *ms, struct evsel *evsel);

#ifdef HAVE_SLANG_SUPPORT
int symbol__tui_annotate(struct map_symbol *ms, struct evsel *evsel,
			 struct hist_browser_timer *hbt);
#else
static inline int symbol__tui_annotate(struct map_symbol *ms __maybe_unused,
				struct evsel *evsel  __maybe_unused,
				struct hist_browser_timer *hbt __maybe_unused)
{
	return 0;
}
#endif

void annotation_options__init(void);
void annotation_options__exit(void);

void annotation_config__init(void);

int annotate_parse_percent_type(const struct option *opt, const char *_str,
				int unset);

int annotate_check_args(void);

/**
 * struct annotated_op_loc - Location info of instruction operand
 * @reg1: First register in the operand
 * @reg2: Second register in the operand
 * @offset: Memory access offset in the operand
 * @segment: Segment selector register
 * @mem_ref: Whether the operand accesses memory
 * @multi_regs: Whether the second register is used
 * @imm: Whether the operand is an immediate value (in offset)
 */
struct annotated_op_loc {
	int reg1;
	int reg2;
	int offset;
	u8 segment;
	bool mem_ref;
	bool multi_regs;
	bool imm;
};

enum annotated_insn_ops {
	INSN_OP_SOURCE = 0,
	INSN_OP_TARGET = 1,

	INSN_OP_MAX,
};

enum annotated_x86_segment {
	INSN_SEG_NONE = 0,

	INSN_SEG_X86_CS,
	INSN_SEG_X86_DS,
	INSN_SEG_X86_ES,
	INSN_SEG_X86_FS,
	INSN_SEG_X86_GS,
	INSN_SEG_X86_SS,
};

/**
 * struct annotated_insn_loc - Location info of instruction
 * @ops: Array of location info for source and target operands
 */
struct annotated_insn_loc {
	struct annotated_op_loc ops[INSN_OP_MAX];
};

#define for_each_insn_op_loc(insn_loc, i, op_loc)			\
	for (i = INSN_OP_SOURCE, op_loc = &(insn_loc)->ops[i];		\
	     i < INSN_OP_MAX;						\
	     i++, op_loc++)

/* Get detailed location info in the instruction */
int annotate_get_insn_location(struct arch *arch, struct disasm_line *dl,
			       struct annotated_insn_loc *loc);

/* Returns a data type from the sample instruction (if any) */
struct annotated_data_type *hist_entry__get_data_type(struct hist_entry *he);

struct annotated_item_stat {
	struct list_head list;
	char *name;
	int good;
	int bad;
};
extern struct list_head ann_insn_stat;

/* Calculate PC-relative address */
u64 annotate_calc_pcrel(struct map_symbol *ms, u64 ip, int offset,
			struct disasm_line *dl);

/**
 * struct annotated_basic_block - Basic block of instructions
 * @list: List node
 * @begin: start instruction in the block
 * @end: end instruction in the block
 */
struct annotated_basic_block {
	struct list_head list;
	struct disasm_line *begin;
	struct disasm_line *end;
};

/* Get a list of basic blocks from src to dst addresses */
int annotate_get_basic_blocks(struct symbol *sym, s64 src, s64 dst,
			      struct list_head *head);

void debuginfo_cache__delete(void);

int annotation_br_cntr_entry(char **str, int br_cntr_nr, u64 *br_cntr,
			     int num_aggr, struct evsel *evsel);
int annotation_br_cntr_abbr_list(char **str, struct evsel *evsel, bool header);
#endif	/* __PERF_ANNOTATE_H */
