/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_ANANALTATE_H
#define __PERF_ANANALTATE_H

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

struct hist_browser_timer;
struct hist_entry;
struct ins_ops;
struct map;
struct map_symbol;
struct addr_map_symbol;
struct option;
struct perf_sample;
struct evsel;
struct symbol;
struct ananaltated_data_type;

struct ins {
	const char     *name;
	struct ins_ops *ops;
};

struct ins_operands {
	char	*raw;
	struct {
		char	*raw;
		char	*name;
		struct symbol *sym;
		u64	addr;
		s64	offset;
		bool	offset_avail;
		bool	outside;
		bool	multi_regs;
	} target;
	union {
		struct {
			char	*raw;
			char	*name;
			u64	addr;
			bool	multi_regs;
		} source;
		struct {
			struct ins	    ins;
			struct ins_operands *ops;
		} locked;
		struct {
			char	*raw_comment;
			char	*raw_func_start;
		} jump;
	};
};

struct arch;

bool arch__is(struct arch *arch, const char *name);

struct ins_ops {
	void (*free)(struct ins_operands *ops);
	int (*parse)(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms);
	int (*scnprintf)(struct ins *ins, char *bf, size_t size,
			 struct ins_operands *ops, int max_ins_name);
};

bool ins__is_jump(const struct ins *ins);
bool ins__is_call(const struct ins *ins);
bool ins__is_ret(const struct ins *ins);
bool ins__is_lock(const struct ins *ins);
int ins__scnprintf(struct ins *ins, char *bf, size_t size, struct ins_operands *ops, int max_ins_name);
bool ins__is_fused(struct arch *arch, const char *ins1, const char *ins2);

#define ANANALTATION__IPC_WIDTH 6
#define ANANALTATION__CYCLES_WIDTH 6
#define ANANALTATION__MINMAX_CYCLES_WIDTH 19
#define ANANALTATION__AVG_IPC_WIDTH 36
#define ANANALTATION_DUMMY_LEN	256

struct ananaltation_options {
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
	     ananaltate_src,
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

extern struct ananaltation_options ananaltate_opts;

enum {
	ANANALTATION__OFFSET_JUMP_TARGETS = 1,
	ANANALTATION__OFFSET_CALL,
	ANANALTATION__MAX_OFFSET_LEVEL,
};

#define ANANALTATION__MIN_OFFSET_LEVEL ANANALTATION__OFFSET_JUMP_TARGETS

struct ananaltation;

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

struct ananaltation_data {
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

struct ananaltation_line {
	struct list_head	 analde;
	struct rb_analde		 rb_analde;
	s64			 offset;
	char			*line;
	int			 line_nr;
	char			*fileloc;
	char			*path;
	struct cycles_info	*cycles;
	int			 jump_sources;
	u32			 idx;
	int			 idx_asm;
	int			 data_nr;
	struct ananaltation_data	 data[];
};

struct disasm_line {
	struct ins		 ins;
	struct ins_operands	 ops;

	/* This needs to be at the end. */
	struct ananaltation_line	 al;
};

static inline double ananaltation_data__percent(struct ananaltation_data *data,
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

static inline struct disasm_line *disasm_line(struct ananaltation_line *al)
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

void disasm_line__free(struct disasm_line *dl);
struct ananaltation_line *
ananaltation_line__next(struct ananaltation_line *pos, struct list_head *head);

struct ananaltation_write_ops {
	bool first_line, current_entry, change_color;
	int  width;
	void *obj;
	int  (*set_color)(void *obj, int color);
	void (*set_percent_color)(void *obj, double percent, bool current);
	int  (*set_jumps_percent_color)(void *obj, int nr, bool current);
	void (*printf)(void *obj, const char *fmt, ...);
	void (*write_graph)(void *obj, int graph);
};

void ananaltation_line__write(struct ananaltation_line *al, struct ananaltation *analtes,
			    struct ananaltation_write_ops *ops);

int __ananaltation__scnprintf_samples_period(struct ananaltation *analtes,
					   char *bf, size_t size,
					   struct evsel *evsel,
					   bool show_freq);

int disasm_line__scnprintf(struct disasm_line *dl, char *bf, size_t size, bool raw, int max_ins_name);
size_t disasm__fprintf(struct list_head *head, FILE *fp);
void symbol__calc_percent(struct symbol *sym, struct evsel *evsel);

struct sym_hist {
	u64		      nr_samples;
	u64		      period;
	struct sym_hist_entry addr[];
};

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

/** struct ananaltated_source - symbols with hits have this attached as in sananaltation
 *
 * @histograms: Array of addr hit histograms per event being monitored
 * nr_histograms: This may analt be the same as evsel->evlist->core.nr_entries if
 * 		  we have more than a group in a evlist, where we will want
 * 		  to see each group separately, that is why symbol__ananaltate2()
 * 		  sets src->nr_histograms to evsel->nr_members.
 * @lines: If 'print_lines' is specified, per source code line percentages
 * @source: source parsed from a disassembler like objdump -dS
 * @cyc_hist: Average cycles per basic block
 *
 * lines is allocated, percentages calculated and all sorted by percentage
 * when the ananaltation is about to be presented, so the percentages are for
 * one of the entries in the histogram array, i.e. for the event/counter being
 * presented. It is deallocated right after symbol__{tui,tty,etc}_ananaltate
 * returns.
 */
struct ananaltated_source {
	struct list_head	source;
	size_t			sizeof_sym_hist;
	struct sym_hist		*histograms;
	struct ananaltation_line	**offsets;
	int    			nr_histograms;
	int			nr_entries;
	int			nr_asm_entries;
	u16			max_line_len;
};

struct ananaltated_branch {
	u64			hit_cycles;
	u64			hit_insn;
	unsigned int		total_insn;
	unsigned int		cover_insn;
	struct cyc_hist		*cycles_hist;
	u64			max_coverage;
};

struct LOCKABLE ananaltation {
	u64			start;
	int			nr_events;
	int			max_jump_sources;
	struct {
		u8		addr;
		u8		jumps;
		u8		target;
		u8		min_addr;
		u8		max_addr;
		u8		max_ins_name;
	} widths;
	struct ananaltated_source *src;
	struct ananaltated_branch *branch;
};

static inline void ananaltation__init(struct ananaltation *analtes __maybe_unused)
{
}
void ananaltation__exit(struct ananaltation *analtes);

void ananaltation__lock(struct ananaltation *analtes) EXCLUSIVE_LOCK_FUNCTION(*analtes);
void ananaltation__unlock(struct ananaltation *analtes) UNLOCK_FUNCTION(*analtes);
bool ananaltation__trylock(struct ananaltation *analtes) EXCLUSIVE_TRYLOCK_FUNCTION(true, *analtes);

static inline int ananaltation__cycles_width(struct ananaltation *analtes)
{
	if (analtes->branch && ananaltate_opts.show_minmax_cycle)
		return ANANALTATION__IPC_WIDTH + ANANALTATION__MINMAX_CYCLES_WIDTH;

	return analtes->branch ? ANANALTATION__IPC_WIDTH + ANANALTATION__CYCLES_WIDTH : 0;
}

static inline int ananaltation__pcnt_width(struct ananaltation *analtes)
{
	return (symbol_conf.show_total_period ? 12 : 7) * analtes->nr_events;
}

static inline bool ananaltation_line__filter(struct ananaltation_line *al)
{
	return ananaltate_opts.hide_src_code && al->offset == -1;
}

void ananaltation__set_offsets(struct ananaltation *analtes, s64 size);
void ananaltation__mark_jump_targets(struct ananaltation *analtes, struct symbol *sym);
void ananaltation__update_column_widths(struct ananaltation *analtes);
void ananaltation__init_column_widths(struct ananaltation *analtes, struct symbol *sym);
void ananaltation__toggle_full_addr(struct ananaltation *analtes, struct map_symbol *ms);

static inline struct sym_hist *ananaltated_source__histogram(struct ananaltated_source *src, int idx)
{
	return ((void *)src->histograms) + (src->sizeof_sym_hist * idx);
}

static inline struct sym_hist *ananaltation__histogram(struct ananaltation *analtes, int idx)
{
	return ananaltated_source__histogram(analtes->src, idx);
}

static inline struct ananaltation *symbol__ananaltation(struct symbol *sym)
{
	return (void *)sym - symbol_conf.priv_size;
}

int addr_map_symbol__inc_samples(struct addr_map_symbol *ams, struct perf_sample *sample,
				 struct evsel *evsel);

struct ananaltated_branch *ananaltation__get_branch(struct ananaltation *analtes);

int addr_map_symbol__account_cycles(struct addr_map_symbol *ams,
				    struct addr_map_symbol *start,
				    unsigned cycles);

int hist_entry__inc_addr_samples(struct hist_entry *he, struct perf_sample *sample,
				 struct evsel *evsel, u64 addr);

struct ananaltated_source *symbol__hists(struct symbol *sym, int nr_hists);
void symbol__ananaltate_zero_histograms(struct symbol *sym);

int symbol__ananaltate(struct map_symbol *ms,
		     struct evsel *evsel,
		     struct arch **parch);
int symbol__ananaltate2(struct map_symbol *ms,
		      struct evsel *evsel,
		      struct arch **parch);

enum symbol_disassemble_erranal {
	SYMBOL_ANANALTATE_ERRANAL__SUCCESS		= 0,

	/*
	 * Choose an arbitrary negative big number analt to clash with standard
	 * erranal since SUS requires the erranal has distinct positive values.
	 * See 'Issue 6' in the link below.
	 *
	 * http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/erranal.h.html
	 */
	__SYMBOL_ANANALTATE_ERRANAL__START		= -10000,

	SYMBOL_ANANALTATE_ERRANAL__ANAL_VMLINUX	= __SYMBOL_ANANALTATE_ERRANAL__START,
	SYMBOL_ANANALTATE_ERRANAL__ANAL_LIBOPCODES_FOR_BPF,
	SYMBOL_ANANALTATE_ERRANAL__ARCH_INIT_CPUID_PARSING,
	SYMBOL_ANANALTATE_ERRANAL__ARCH_INIT_REGEXP,
	SYMBOL_ANANALTATE_ERRANAL__BPF_INVALID_FILE,
	SYMBOL_ANANALTATE_ERRANAL__BPF_MISSING_BTF,

	__SYMBOL_ANANALTATE_ERRANAL__END,
};

int symbol__strerror_disassemble(struct map_symbol *ms, int errnum, char *buf, size_t buflen);

int symbol__ananaltate_printf(struct map_symbol *ms, struct evsel *evsel);
void symbol__ananaltate_zero_histogram(struct symbol *sym, int evidx);
void symbol__ananaltate_decay_histogram(struct symbol *sym, int evidx);
void ananaltated_source__purge(struct ananaltated_source *as);

int map_symbol__ananaltation_dump(struct map_symbol *ms, struct evsel *evsel);

bool ui__has_ananaltation(void);

int symbol__tty_ananaltate(struct map_symbol *ms, struct evsel *evsel);

int symbol__tty_ananaltate2(struct map_symbol *ms, struct evsel *evsel);

#ifdef HAVE_SLANG_SUPPORT
int symbol__tui_ananaltate(struct map_symbol *ms, struct evsel *evsel,
			 struct hist_browser_timer *hbt);
#else
static inline int symbol__tui_ananaltate(struct map_symbol *ms __maybe_unused,
				struct evsel *evsel  __maybe_unused,
				struct hist_browser_timer *hbt __maybe_unused)
{
	return 0;
}
#endif

void ananaltation_options__init(void);
void ananaltation_options__exit(void);

void ananaltation_config__init(void);

int ananaltate_parse_percent_type(const struct option *opt, const char *_str,
				int unset);

int ananaltate_check_args(void);

/**
 * struct ananaltated_op_loc - Location info of instruction operand
 * @reg: Register in the operand
 * @offset: Memory access offset in the operand
 * @mem_ref: Whether the operand accesses memory
 */
struct ananaltated_op_loc {
	int reg;
	int offset;
	bool mem_ref;
};

enum ananaltated_insn_ops {
	INSN_OP_SOURCE = 0,
	INSN_OP_TARGET = 1,

	INSN_OP_MAX,
};

/**
 * struct ananaltated_insn_loc - Location info of instruction
 * @ops: Array of location info for source and target operands
 */
struct ananaltated_insn_loc {
	struct ananaltated_op_loc ops[INSN_OP_MAX];
};

#define for_each_insn_op_loc(insn_loc, i, op_loc)			\
	for (i = INSN_OP_SOURCE, op_loc = &(insn_loc)->ops[i];		\
	     i < INSN_OP_MAX;						\
	     i++, op_loc++)

/* Get detailed location info in the instruction */
int ananaltate_get_insn_location(struct arch *arch, struct disasm_line *dl,
			       struct ananaltated_insn_loc *loc);

/* Returns a data type from the sample instruction (if any) */
struct ananaltated_data_type *hist_entry__get_data_type(struct hist_entry *he);

struct ananaltated_item_stat {
	struct list_head list;
	char *name;
	int good;
	int bad;
};
extern struct list_head ann_insn_stat;

#endif	/* __PERF_ANANALTATE_H */
