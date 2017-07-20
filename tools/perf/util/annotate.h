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
		u64	addr;
		s64	offset;
		bool	offset_avail;
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
	int (*parse)(struct arch *arch, struct ins_operands *ops, struct map *map);
	int (*scnprintf)(struct ins *ins, char *bf, size_t size,
			 struct ins_operands *ops);
};

bool ins__is_jump(const struct ins *ins);
bool ins__is_call(const struct ins *ins);
bool ins__is_ret(const struct ins *ins);
bool ins__is_lock(const struct ins *ins);
int ins__scnprintf(struct ins *ins, char *bf, size_t size, struct ins_operands *ops);
bool ins__is_fused(struct arch *arch, const char *ins1, const char *ins2);

struct annotation;

struct disasm_line {
	struct list_head    node;
	s64		    offset;
	char		    *line;
	struct ins	    ins;
	int		    line_nr;
	float		    ipc;
	u64		    cycles;
	struct ins_operands ops;
};

static inline bool disasm_line__has_offset(const struct disasm_line *dl)
{
	return dl->ops.target.offset_avail;
}

struct sym_hist_entry {
	u64		nr_samples;
	u64		period;
};

void disasm_line__free(struct disasm_line *dl);
struct disasm_line *disasm__get_next_ip_line(struct list_head *head, struct disasm_line *pos);
int disasm_line__scnprintf(struct disasm_line *dl, char *bf, size_t size, bool raw);
size_t disasm__fprintf(struct list_head *head, FILE *fp);
double disasm__calc_percent(struct annotation *notes, int evidx, s64 offset,
			    s64 end, const char **path, struct sym_hist_entry *sample);

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

struct source_line_samples {
	double		percent;
	double		percent_sum;
	u64		nr;
};

struct source_line {
	struct rb_node	node;
	char		*path;
	int		nr_pcnt;
	struct source_line_samples samples[1];
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
	struct source_line *lines;
	int    		   nr_histograms;
	size_t		   sizeof_sym_hist;
	struct cyc_hist	   *cycles_hist;
	struct sym_hist	   histograms[0];
};

struct annotation {
	pthread_mutex_t		lock;
	u64			max_coverage;
	struct annotated_source *src;
};

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

int symbol__disassemble(struct symbol *sym, struct map *map,
			const char *arch_name, size_t privsize,
			struct arch **parch, char *cpuid);

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
void symbol__annotate_zero_histogram(struct symbol *sym, int evidx);
void symbol__annotate_decay_histogram(struct symbol *sym, int evidx);
void disasm__purge(struct list_head *head);

bool ui__has_annotation(void);

int symbol__tty_annotate(struct symbol *sym, struct map *map,
			 struct perf_evsel *evsel, bool print_lines,
			 bool full_paths, int min_pcnt, int max_lines);

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

#endif	/* __PERF_ANNOTATE_H */
