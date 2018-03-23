/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_ANNOTATE_H
#define __PERF_ANNOTATE_H

#include <stdbool.h>
#include <stdint.h>
#include <linux/types.h>
#include "symbol.h"
#include "hist.h"
#include "sort.h"
#include <linux/list.h>
#include <linux/rbtree.h>
#include <pthread.h>

struct ins_ops;

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
	} target;
	union {
		struct {
			char	*raw;
			char	*name;
			u64	addr;
		} source;
		struct {
			struct ins	    ins;
			struct ins_operands *ops;
		} locked;
	};
};

struct arch;

struct ins_ops {
	void (*free)(struct ins_operands *ops);
	int (*parse)(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms);
	int (*scnprintf)(struct ins *ins, char *bf, size_t size,
			 struct ins_operands *ops);
};

bool ins__is_jump(const struct ins *ins);
bool ins__is_call(const struct ins *ins);
bool ins__is_ret(const struct ins *ins);
bool ins__is_lock(const struct ins *ins);
int ins__scnprintf(struct ins *ins, char *bf, size_t size, struct ins_operands *ops);
bool ins__is_fused(struct arch *arch, const char *ins1, const char *ins2);

#define ANNOTATION__IPC_WIDTH 6
#define ANNOTATION__CYCLES_WIDTH 6

struct annotation_options {
	bool hide_src_code,
	     use_offset,
	     jump_arrows,
	     show_linenr,
	     show_nr_jumps,
	     show_nr_samples,
	     show_total_period;
};

extern struct annotation_options annotation__default_options;

struct annotation;

struct sym_hist_entry {
	u64		nr_samples;
	u64		period;
};

struct annotation_data {
	double			 percent;
	double			 percent_sum;
	struct sym_hist_entry	 he;
};

struct annotation_line {
	struct list_head	 node;
	struct rb_node		 rb_node;
	s64			 offset;
	char			*line;
	int			 line_nr;
	int			 jump_sources;
	float			 ipc;
	u64			 cycles;
	size_t			 privsize;
	char			*path;
	u32			 idx;
	int			 idx_asm;
	int			 samples_nr;
	struct annotation_data	 samples[0];
};

struct disasm_line {
	struct ins		 ins;
	struct ins_operands	 ops;

	/* This needs to be at the end. */
	struct annotation_line	 al;
};

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

void disasm_line__free(struct disasm_line *dl);
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

double annotation_line__max_percent(struct annotation_line *al, struct annotation *notes);
void annotation_line__write(struct annotation_line *al, struct annotation *notes,
			    struct annotation_write_ops *ops);

int disasm_line__scnprintf(struct disasm_line *dl, char *bf, size_t size, bool raw);
size_t disasm__fprintf(struct list_head *head, FILE *fp);
void symbol__calc_percent(struct symbol *sym, struct perf_evsel *evsel);

struct sym_hist {
	u64		      nr_samples;
	u64		      period;
	struct sym_hist_entry addr[0];
};

struct cyc_hist {
	u64	start;
	u64	cycles;
	u64	cycles_aggr;
	u32	num;
	u32	num_aggr;
	u8	have_start;
	/* 1 byte padding */
	u16	reset;
};

/** struct annotated_source - symbols with hits have this attached as in sannotation
 *
 * @histogram: Array of addr hit histograms per event being monitored
 * @lines: If 'print_lines' is specified, per source code line percentages
 * @source: source parsed from a disassembler like objdump -dS
 * @cyc_hist: Average cycles per basic block
 *
 * lines is allocated, percentages calculated and all sorted by percentage
 * when the annotation is about to be presented, so the percentages are for
 * one of the entries in the histogram array, i.e. for the event/counter being
 * presented. It is deallocated right after symbol__{tui,tty,etc}_annotate
 * returns.
 */
struct annotated_source {
	struct list_head   source;
	int    		   nr_histograms;
	size_t		   sizeof_sym_hist;
	struct cyc_hist	   *cycles_hist;
	struct sym_hist	   histograms[0];
};

struct annotation {
	pthread_mutex_t		lock;
	u64			max_coverage;
	u64			start;
	struct annotation_options *options;
	struct annotation_line	**offsets;
	int			nr_events;
	int			nr_jumps;
	int			max_jump_sources;
	int			nr_entries;
	int			nr_asm_entries;
	u16			max_line_len;
	struct {
		u8		addr;
		u8		jumps;
		u8		target;
		u8		min_addr;
		u8		max_addr;
	} widths;
	bool			have_cycles;
	struct annotated_source *src;
};

static inline int annotation__cycles_width(struct annotation *notes)
{
	return notes->have_cycles ? ANNOTATION__IPC_WIDTH + ANNOTATION__CYCLES_WIDTH : 0;
}

static inline int annotation__pcnt_width(struct annotation *notes)
{
	return (notes->options->show_total_period ? 12 : 7) * notes->nr_events;
}

static inline bool annotation_line__filter(struct annotation_line *al, struct annotation *notes)
{
	return notes->options->hide_src_code && al->offset == -1;
}

void annotation__set_offsets(struct annotation *notes, s64 size);
void annotation__compute_ipc(struct annotation *notes, size_t size);
void annotation__mark_jump_targets(struct annotation *notes, struct symbol *sym);
void annotation__update_column_widths(struct annotation *notes);
void annotation__init_column_widths(struct annotation *notes, struct symbol *sym);

static inline struct sym_hist *annotation__histogram(struct annotation *notes, int idx)
{
	return (((void *)&notes->src->histograms) +
	 	(notes->src->sizeof_sym_hist * idx));
}

static inline struct annotation *symbol__annotation(struct symbol *sym)
{
	return (void *)sym - symbol_conf.priv_size;
}

int addr_map_symbol__inc_samples(struct addr_map_symbol *ams, struct perf_sample *sample,
				 int evidx);

int addr_map_symbol__account_cycles(struct addr_map_symbol *ams,
				    struct addr_map_symbol *start,
				    unsigned cycles);

int hist_entry__inc_addr_samples(struct hist_entry *he, struct perf_sample *sample,
				 int evidx, u64 addr);

int symbol__alloc_hist(struct symbol *sym);
void symbol__annotate_zero_histograms(struct symbol *sym);

int symbol__annotate(struct symbol *sym, struct map *map,
		     struct perf_evsel *evsel, size_t privsize,
		     struct arch **parch);
int symbol__annotate2(struct symbol *sym, struct map *map,
		      struct perf_evsel *evsel,
		      struct annotation_options *options,
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

	__SYMBOL_ANNOTATE_ERRNO__END,
};

int symbol__strerror_disassemble(struct symbol *sym, struct map *map,
				 int errnum, char *buf, size_t buflen);

int symbol__annotate_printf(struct symbol *sym, struct map *map,
			    struct perf_evsel *evsel, bool full_paths,
			    int min_pcnt, int max_lines, int context);
int symbol__annotate_fprintf2(struct symbol *sym, FILE *fp);
void symbol__annotate_zero_histogram(struct symbol *sym, int evidx);
void symbol__annotate_decay_histogram(struct symbol *sym, int evidx);
void annotated_source__purge(struct annotated_source *as);

int map_symbol__annotation_dump(struct map_symbol *ms, struct perf_evsel *evsel);

bool ui__has_annotation(void);

int symbol__tty_annotate(struct symbol *sym, struct map *map,
			 struct perf_evsel *evsel, bool print_lines,
			 bool full_paths, int min_pcnt, int max_lines);

int symbol__tty_annotate2(struct symbol *sym, struct map *map,
			  struct perf_evsel *evsel, bool print_lines,
			  bool full_paths);

#ifdef HAVE_SLANG_SUPPORT
int symbol__tui_annotate(struct symbol *sym, struct map *map,
			 struct perf_evsel *evsel,
			 struct hist_browser_timer *hbt);
#else
static inline int symbol__tui_annotate(struct symbol *sym __maybe_unused,
				struct map *map __maybe_unused,
				struct perf_evsel *evsel  __maybe_unused,
				struct hist_browser_timer *hbt
				__maybe_unused)
{
	return 0;
}
#endif

extern const char	*disassembler_style;

void annotation_config__init(void);

#endif	/* __PERF_ANNOTATE_H */
