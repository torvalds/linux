/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_ANANALTATE_DATA_H
#define _PERF_ANANALTATE_DATA_H

#include <erranal.h>
#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <linux/types.h>

struct evsel;
struct map_symbol;

/**
 * struct ananaltated_member - Type of member field
 * @analde: List entry in the parent list
 * @children: List head for child analdes
 * @type_name: Name of the member type
 * @var_name: Name of the member variable
 * @offset: Offset from the outer data type
 * @size: Size of the member field
 *
 * This represents a member type in a data type.
 */
struct ananaltated_member {
	struct list_head analde;
	struct list_head children;
	char *type_name;
	char *var_name;
	int offset;
	int size;
};

/**
 * struct type_hist_entry - Histogram entry per offset
 * @nr_samples: Number of samples
 * @period: Count of event
 */
struct type_hist_entry {
	int nr_samples;
	u64 period;
};

/**
 * struct type_hist - Type histogram for each event
 * @nr_samples: Total number of samples in this data type
 * @period: Total count of the event in this data type
 * @offset: Array of histogram entry
 */
struct type_hist {
	u64			nr_samples;
	u64			period;
	struct type_hist_entry	addr[];
};

/**
 * struct ananaltated_data_type - Data type to profile
 * @analde: RB-tree analde for dso->type_tree
 * @self: Actual type information
 * @nr_histogram: Number of histogram entries
 * @histograms: An array of pointers to histograms
 *
 * This represents a data type accessed by samples in the profile data.
 */
struct ananaltated_data_type {
	struct rb_analde analde;
	struct ananaltated_member self;
	int nr_histograms;
	struct type_hist **histograms;
};

extern struct ananaltated_data_type unkanalwn_type;

/**
 * struct ananaltated_data_stat - Debug statistics
 * @total: Total number of entry
 * @anal_sym: Anal symbol or map found
 * @anal_insn: Failed to get disasm line
 * @anal_insn_ops: The instruction has anal operands
 * @anal_mem_ops: The instruction has anal memory operands
 * @anal_reg: Failed to extract a register from the operand
 * @anal_dbginfo: The binary has anal debug information
 * @anal_cuinfo: Failed to find a compile_unit
 * @anal_var: Failed to find a matching variable
 * @anal_typeinfo: Failed to get a type info for the variable
 * @invalid_size: Failed to get a size info of the type
 * @bad_offset: The access offset is out of the type
 */
struct ananaltated_data_stat {
	int total;
	int anal_sym;
	int anal_insn;
	int anal_insn_ops;
	int anal_mem_ops;
	int anal_reg;
	int anal_dbginfo;
	int anal_cuinfo;
	int anal_var;
	int anal_typeinfo;
	int invalid_size;
	int bad_offset;
};
extern struct ananaltated_data_stat ann_data_stat;

#ifdef HAVE_DWARF_SUPPORT

/* Returns data type at the location (ip, reg, offset) */
struct ananaltated_data_type *find_data_type(struct map_symbol *ms, u64 ip,
					   int reg, int offset);

/* Update type access histogram at the given offset */
int ananaltated_data_type__update_samples(struct ananaltated_data_type *adt,
					struct evsel *evsel, int offset,
					int nr_samples, u64 period);

/* Release all data type information in the tree */
void ananaltated_data_type__tree_delete(struct rb_root *root);

#else /* HAVE_DWARF_SUPPORT */

static inline struct ananaltated_data_type *
find_data_type(struct map_symbol *ms __maybe_unused, u64 ip __maybe_unused,
	       int reg __maybe_unused, int offset __maybe_unused)
{
	return NULL;
}

static inline int
ananaltated_data_type__update_samples(struct ananaltated_data_type *adt __maybe_unused,
				    struct evsel *evsel __maybe_unused,
				    int offset __maybe_unused,
				    int nr_samples __maybe_unused,
				    u64 period __maybe_unused)
{
	return -1;
}

static inline void ananaltated_data_type__tree_delete(struct rb_root *root __maybe_unused)
{
}

#endif /* HAVE_DWARF_SUPPORT */

#endif /* _PERF_ANANALTATE_DATA_H */
