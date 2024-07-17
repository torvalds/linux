/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_SORT_H
#define __PERF_SORT_H
#include <regex.h>
#include <stdbool.h>
#include "hist.h"

struct option;

extern regex_t parent_regex;
extern const char *sort_order;
extern const char *field_order;
extern const char default_parent_pattern[];
extern const char *parent_pattern;
extern const char *default_sort_order;
extern regex_t ignore_callees_regex;
extern int have_ignore_callees;
extern enum sort_mode sort__mode;
extern struct sort_entry sort_comm;
extern struct sort_entry sort_dso;
extern struct sort_entry sort_sym;
extern struct sort_entry sort_parent;
extern struct sort_entry sort_dso_from;
extern struct sort_entry sort_dso_to;
extern struct sort_entry sort_sym_from;
extern struct sort_entry sort_sym_to;
extern struct sort_entry sort_srcline;
extern struct sort_entry sort_type;
extern const char default_mem_sort_order[];
extern bool chk_double_cl;

enum sort_mode {
	SORT_MODE__NORMAL,
	SORT_MODE__BRANCH,
	SORT_MODE__MEMORY,
	SORT_MODE__TOP,
	SORT_MODE__DIFF,
	SORT_MODE__TRACEPOINT,
};

enum sort_type {
	/* common sort keys */
	SORT_PID,
	SORT_COMM,
	SORT_DSO,
	SORT_SYM,
	SORT_PARENT,
	SORT_CPU,
	SORT_SOCKET,
	SORT_SRCLINE,
	SORT_SRCFILE,
	SORT_LOCAL_WEIGHT,
	SORT_GLOBAL_WEIGHT,
	SORT_TRANSACTION,
	SORT_TRACE,
	SORT_SYM_SIZE,
	SORT_DSO_SIZE,
	SORT_CGROUP,
	SORT_CGROUP_ID,
	SORT_SYM_IPC_NULL,
	SORT_TIME,
	SORT_CODE_PAGE_SIZE,
	SORT_LOCAL_INS_LAT,
	SORT_GLOBAL_INS_LAT,
	SORT_LOCAL_PIPELINE_STAGE_CYC,
	SORT_GLOBAL_PIPELINE_STAGE_CYC,
	SORT_ADDR,
	SORT_LOCAL_RETIRE_LAT,
	SORT_GLOBAL_RETIRE_LAT,
	SORT_SIMD,
	SORT_ANNOTATE_DATA_TYPE,
	SORT_ANNOTATE_DATA_TYPE_OFFSET,
	SORT_SYM_OFFSET,

	/* branch stack specific sort keys */
	__SORT_BRANCH_STACK,
	SORT_DSO_FROM = __SORT_BRANCH_STACK,
	SORT_DSO_TO,
	SORT_SYM_FROM,
	SORT_SYM_TO,
	SORT_MISPREDICT,
	SORT_ABORT,
	SORT_IN_TX,
	SORT_CYCLES,
	SORT_SRCLINE_FROM,
	SORT_SRCLINE_TO,
	SORT_SYM_IPC,
	SORT_ADDR_FROM,
	SORT_ADDR_TO,

	/* memory mode specific sort keys */
	__SORT_MEMORY_MODE,
	SORT_MEM_DADDR_SYMBOL = __SORT_MEMORY_MODE,
	SORT_MEM_DADDR_DSO,
	SORT_MEM_LOCKED,
	SORT_MEM_TLB,
	SORT_MEM_LVL,
	SORT_MEM_SNOOP,
	SORT_MEM_DCACHELINE,
	SORT_MEM_IADDR_SYMBOL,
	SORT_MEM_PHYS_DADDR,
	SORT_MEM_DATA_PAGE_SIZE,
	SORT_MEM_BLOCKED,
};

/*
 * configurable sorting bits
 */

struct sort_entry {
	const char *se_header;

	int64_t (*se_cmp)(struct hist_entry *, struct hist_entry *);
	int64_t (*se_collapse)(struct hist_entry *, struct hist_entry *);
	int64_t	(*se_sort)(struct hist_entry *, struct hist_entry *);
	int	(*se_snprintf)(struct hist_entry *he, char *bf, size_t size,
			       unsigned int width);
	int	(*se_filter)(struct hist_entry *he, int type, const void *arg);
	void	(*se_init)(struct hist_entry *he);
	u8	se_width_idx;
};

extern struct sort_entry sort_thread;

struct evlist;
struct tep_handle;
int setup_sorting(struct evlist *evlist);
int setup_output_field(void);
void reset_output_field(void);
void sort__setup_elide(FILE *fp);
void perf_hpp__set_elide(int idx, bool elide);

char *sort_help(const char *prefix);

int report_parse_ignore_callees_opt(const struct option *opt, const char *arg, int unset);

bool is_strict_order(const char *order);

int hpp_dimension__add_output(unsigned col);
void reset_dimensions(void);
int sort_dimension__add(struct perf_hpp_list *list, const char *tok,
			struct evlist *evlist,
			int level);
int output_field_add(struct perf_hpp_list *list, const char *tok);
int64_t
sort__iaddr_cmp(struct hist_entry *left, struct hist_entry *right);
int64_t
sort__daddr_cmp(struct hist_entry *left, struct hist_entry *right);
int64_t
sort__dcacheline_cmp(struct hist_entry *left, struct hist_entry *right);
int64_t
_sort__sym_cmp(struct symbol *sym_l, struct symbol *sym_r);
char *hist_entry__srcline(struct hist_entry *he);
#endif	/* __PERF_SORT_H */
