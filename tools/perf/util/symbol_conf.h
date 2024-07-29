/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_SYMBOL_CONF
#define __PERF_SYMBOL_CONF 1

#include <stdbool.h>

struct strlist;
struct intlist;

struct symbol_conf {
	bool		nanosecs;
	unsigned short	priv_size;
	bool		try_vmlinux_path,
			init_annotation,
			force,
			ignore_vmlinux,
			ignore_vmlinux_buildid,
			show_kernel_path,
			use_modules,
			allow_aliases,
			show_nr_samples,
			show_total_period,
			use_callchain,
			cumulate_callchain,
			show_branchflag_count,
			exclude_other,
			show_cpu_utilization,
			initialized,
			kptr_restrict,
			event_group,
			demangle,
			demangle_kernel,
			filter_relative,
			show_hist_headers,
			has_filter,
			show_ref_callgraph,
			hide_unresolved,
			raw_trace,
			report_hierarchy,
			report_block,
			report_individual_block,
			inline_name,
			disable_add2line_warn,
			buildid_mmap2,
			guest_code,
			lazy_load_kernel_maps,
			keep_exited_threads,
			annotate_data_member,
			annotate_data_sample,
			skip_empty;
	const char	*vmlinux_name,
			*kallsyms_name,
			*source_prefix,
			*field_sep,
			*graph_function;
	const char	*default_guest_vmlinux_name,
			*default_guest_kallsyms,
			*default_guest_modules;
	const char	*guestmount;
	const char	*dso_list_str,
			*comm_list_str,
			*pid_list_str,
			*tid_list_str,
			*sym_list_str,
			*col_width_list_str,
			*bt_stop_list_str;
	char		*addr2line_path;
	unsigned long	time_quantum;
       struct strlist	*dso_list,
			*comm_list,
			*sym_list,
			*dso_from_list,
			*dso_to_list,
			*sym_from_list,
			*sym_to_list,
			*bt_stop_list;
	struct intlist	*pid_list,
			*tid_list,
			*addr_list;
	const char	*symfs;
	int		res_sample;
	int		pad_output_len_dso;
	int		group_sort_idx;
	int		addr_range;
};

extern struct symbol_conf symbol_conf;

#endif // __PERF_SYMBOL_CONF
