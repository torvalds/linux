// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2020 Facebook */

#include <errno.h>
#include <linux/err.h>
#include <linux/netfilter.h>
#include <linux/netfilter_arp.h>
#include <linux/perf_event.h>
#include <net/if.h>
#include <stdio.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/hashmap.h>

#include "json_writer.h"
#include "main.h"
#include "xlated_dumper.h"

#define PERF_HW_CACHE_LEN 128

static struct hashmap *link_table;
static struct dump_data dd;

static const char *perf_type_name[PERF_TYPE_MAX] = {
	[PERF_TYPE_HARDWARE]			= "hardware",
	[PERF_TYPE_SOFTWARE]			= "software",
	[PERF_TYPE_TRACEPOINT]			= "tracepoint",
	[PERF_TYPE_HW_CACHE]			= "hw-cache",
	[PERF_TYPE_RAW]				= "raw",
	[PERF_TYPE_BREAKPOINT]			= "breakpoint",
};

const char *event_symbols_hw[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= "cpu-cycles",
	[PERF_COUNT_HW_INSTRUCTIONS]		= "instructions",
	[PERF_COUNT_HW_CACHE_REFERENCES]	= "cache-references",
	[PERF_COUNT_HW_CACHE_MISSES]		= "cache-misses",
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= "branch-instructions",
	[PERF_COUNT_HW_BRANCH_MISSES]		= "branch-misses",
	[PERF_COUNT_HW_BUS_CYCLES]		= "bus-cycles",
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= "stalled-cycles-frontend",
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= "stalled-cycles-backend",
	[PERF_COUNT_HW_REF_CPU_CYCLES]		= "ref-cycles",
};

const char *event_symbols_sw[PERF_COUNT_SW_MAX] = {
	[PERF_COUNT_SW_CPU_CLOCK]		= "cpu-clock",
	[PERF_COUNT_SW_TASK_CLOCK]		= "task-clock",
	[PERF_COUNT_SW_PAGE_FAULTS]		= "page-faults",
	[PERF_COUNT_SW_CONTEXT_SWITCHES]	= "context-switches",
	[PERF_COUNT_SW_CPU_MIGRATIONS]		= "cpu-migrations",
	[PERF_COUNT_SW_PAGE_FAULTS_MIN]		= "minor-faults",
	[PERF_COUNT_SW_PAGE_FAULTS_MAJ]		= "major-faults",
	[PERF_COUNT_SW_ALIGNMENT_FAULTS]	= "alignment-faults",
	[PERF_COUNT_SW_EMULATION_FAULTS]	= "emulation-faults",
	[PERF_COUNT_SW_DUMMY]			= "dummy",
	[PERF_COUNT_SW_BPF_OUTPUT]		= "bpf-output",
	[PERF_COUNT_SW_CGROUP_SWITCHES]		= "cgroup-switches",
};

const char *evsel__hw_cache[PERF_COUNT_HW_CACHE_MAX] = {
	[PERF_COUNT_HW_CACHE_L1D]		= "L1-dcache",
	[PERF_COUNT_HW_CACHE_L1I]		= "L1-icache",
	[PERF_COUNT_HW_CACHE_LL]		= "LLC",
	[PERF_COUNT_HW_CACHE_DTLB]		= "dTLB",
	[PERF_COUNT_HW_CACHE_ITLB]		= "iTLB",
	[PERF_COUNT_HW_CACHE_BPU]		= "branch",
	[PERF_COUNT_HW_CACHE_NODE]		= "node",
};

const char *evsel__hw_cache_op[PERF_COUNT_HW_CACHE_OP_MAX] = {
	[PERF_COUNT_HW_CACHE_OP_READ]		= "load",
	[PERF_COUNT_HW_CACHE_OP_WRITE]		= "store",
	[PERF_COUNT_HW_CACHE_OP_PREFETCH]	= "prefetch",
};

const char *evsel__hw_cache_result[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[PERF_COUNT_HW_CACHE_RESULT_ACCESS]	= "refs",
	[PERF_COUNT_HW_CACHE_RESULT_MISS]	= "misses",
};

#define perf_event_name(array, id) ({			\
	const char *event_str = NULL;			\
							\
	if ((id) >= 0 && (id) < ARRAY_SIZE(array))	\
		event_str = array[id];			\
	event_str;					\
})

static int link_parse_fd(int *argc, char ***argv)
{
	int fd;

	if (is_prefix(**argv, "id")) {
		unsigned int id;
		char *endptr;

		NEXT_ARGP();

		id = strtoul(**argv, &endptr, 0);
		if (*endptr) {
			p_err("can't parse %s as ID", **argv);
			return -1;
		}
		NEXT_ARGP();

		fd = bpf_link_get_fd_by_id(id);
		if (fd < 0)
			p_err("failed to get link with ID %d: %s", id, strerror(errno));
		return fd;
	} else if (is_prefix(**argv, "pinned")) {
		char *path;

		NEXT_ARGP();

		path = **argv;
		NEXT_ARGP();

		return open_obj_pinned_any(path, BPF_OBJ_LINK);
	}

	p_err("expected 'id' or 'pinned', got: '%s'?", **argv);
	return -1;
}

static void
show_link_header_json(struct bpf_link_info *info, json_writer_t *wtr)
{
	const char *link_type_str;

	jsonw_uint_field(wtr, "id", info->id);
	link_type_str = libbpf_bpf_link_type_str(info->type);
	if (link_type_str)
		jsonw_string_field(wtr, "type", link_type_str);
	else
		jsonw_uint_field(wtr, "type", info->type);

	jsonw_uint_field(json_wtr, "prog_id", info->prog_id);
}

static void show_link_attach_type_json(__u32 attach_type, json_writer_t *wtr)
{
	const char *attach_type_str;

	attach_type_str = libbpf_bpf_attach_type_str(attach_type);
	if (attach_type_str)
		jsonw_string_field(wtr, "attach_type", attach_type_str);
	else
		jsonw_uint_field(wtr, "attach_type", attach_type);
}

static bool is_iter_map_target(const char *target_name)
{
	return strcmp(target_name, "bpf_map_elem") == 0 ||
	       strcmp(target_name, "bpf_sk_storage_map") == 0;
}

static bool is_iter_cgroup_target(const char *target_name)
{
	return strcmp(target_name, "cgroup") == 0;
}

static const char *cgroup_order_string(__u32 order)
{
	switch (order) {
	case BPF_CGROUP_ITER_ORDER_UNSPEC:
		return "order_unspec";
	case BPF_CGROUP_ITER_SELF_ONLY:
		return "self_only";
	case BPF_CGROUP_ITER_DESCENDANTS_PRE:
		return "descendants_pre";
	case BPF_CGROUP_ITER_DESCENDANTS_POST:
		return "descendants_post";
	case BPF_CGROUP_ITER_ANCESTORS_UP:
		return "ancestors_up";
	default: /* won't happen */
		return "unknown";
	}
}

static bool is_iter_task_target(const char *target_name)
{
	return strcmp(target_name, "task") == 0 ||
		strcmp(target_name, "task_file") == 0 ||
		strcmp(target_name, "task_vma") == 0;
}

static void show_iter_json(struct bpf_link_info *info, json_writer_t *wtr)
{
	const char *target_name = u64_to_ptr(info->iter.target_name);

	jsonw_string_field(wtr, "target_name", target_name);

	if (is_iter_map_target(target_name))
		jsonw_uint_field(wtr, "map_id", info->iter.map.map_id);
	else if (is_iter_task_target(target_name)) {
		if (info->iter.task.tid)
			jsonw_uint_field(wtr, "tid", info->iter.task.tid);
		else if (info->iter.task.pid)
			jsonw_uint_field(wtr, "pid", info->iter.task.pid);
	}

	if (is_iter_cgroup_target(target_name)) {
		jsonw_lluint_field(wtr, "cgroup_id", info->iter.cgroup.cgroup_id);
		jsonw_string_field(wtr, "order",
				   cgroup_order_string(info->iter.cgroup.order));
	}
}

void netfilter_dump_json(const struct bpf_link_info *info, json_writer_t *wtr)
{
	jsonw_uint_field(json_wtr, "pf",
			 info->netfilter.pf);
	jsonw_uint_field(json_wtr, "hook",
			 info->netfilter.hooknum);
	jsonw_int_field(json_wtr, "prio",
			 info->netfilter.priority);
	jsonw_uint_field(json_wtr, "flags",
			 info->netfilter.flags);
}

static int get_prog_info(int prog_id, struct bpf_prog_info *info)
{
	__u32 len = sizeof(*info);
	int err, prog_fd;

	prog_fd = bpf_prog_get_fd_by_id(prog_id);
	if (prog_fd < 0)
		return prog_fd;

	memset(info, 0, sizeof(*info));
	err = bpf_prog_get_info_by_fd(prog_fd, info, &len);
	if (err)
		p_err("can't get prog info: %s", strerror(errno));
	close(prog_fd);
	return err;
}

static int cmp_u64(const void *A, const void *B)
{
	const __u64 *a = A, *b = B;

	return *a - *b;
}

static void
show_kprobe_multi_json(struct bpf_link_info *info, json_writer_t *wtr)
{
	__u32 i, j = 0;
	__u64 *addrs;

	jsonw_bool_field(json_wtr, "retprobe",
			 info->kprobe_multi.flags & BPF_F_KPROBE_MULTI_RETURN);
	jsonw_uint_field(json_wtr, "func_cnt", info->kprobe_multi.count);
	jsonw_name(json_wtr, "funcs");
	jsonw_start_array(json_wtr);
	addrs = u64_to_ptr(info->kprobe_multi.addrs);
	qsort(addrs, info->kprobe_multi.count, sizeof(addrs[0]), cmp_u64);

	/* Load it once for all. */
	if (!dd.sym_count)
		kernel_syms_load(&dd);
	for (i = 0; i < dd.sym_count; i++) {
		if (dd.sym_mapping[i].address != addrs[j])
			continue;
		jsonw_start_object(json_wtr);
		jsonw_uint_field(json_wtr, "addr", dd.sym_mapping[i].address);
		jsonw_string_field(json_wtr, "func", dd.sym_mapping[i].name);
		/* Print null if it is vmlinux */
		if (dd.sym_mapping[i].module[0] == '\0') {
			jsonw_name(json_wtr, "module");
			jsonw_null(json_wtr);
		} else {
			jsonw_string_field(json_wtr, "module", dd.sym_mapping[i].module);
		}
		jsonw_end_object(json_wtr);
		if (j++ == info->kprobe_multi.count)
			break;
	}
	jsonw_end_array(json_wtr);
}

static void
show_perf_event_kprobe_json(struct bpf_link_info *info, json_writer_t *wtr)
{
	jsonw_bool_field(wtr, "retprobe", info->perf_event.type == BPF_PERF_EVENT_KRETPROBE);
	jsonw_uint_field(wtr, "addr", info->perf_event.kprobe.addr);
	jsonw_string_field(wtr, "func",
			   u64_to_ptr(info->perf_event.kprobe.func_name));
	jsonw_uint_field(wtr, "offset", info->perf_event.kprobe.offset);
}

static void
show_perf_event_uprobe_json(struct bpf_link_info *info, json_writer_t *wtr)
{
	jsonw_bool_field(wtr, "retprobe", info->perf_event.type == BPF_PERF_EVENT_URETPROBE);
	jsonw_string_field(wtr, "file",
			   u64_to_ptr(info->perf_event.uprobe.file_name));
	jsonw_uint_field(wtr, "offset", info->perf_event.uprobe.offset);
}

static void
show_perf_event_tracepoint_json(struct bpf_link_info *info, json_writer_t *wtr)
{
	jsonw_string_field(wtr, "tracepoint",
			   u64_to_ptr(info->perf_event.tracepoint.tp_name));
}

static char *perf_config_hw_cache_str(__u64 config)
{
	const char *hw_cache, *result, *op;
	char *str = malloc(PERF_HW_CACHE_LEN);

	if (!str) {
		p_err("mem alloc failed");
		return NULL;
	}

	hw_cache = perf_event_name(evsel__hw_cache, config & 0xff);
	if (hw_cache)
		snprintf(str, PERF_HW_CACHE_LEN, "%s-", hw_cache);
	else
		snprintf(str, PERF_HW_CACHE_LEN, "%lld-", config & 0xff);

	op = perf_event_name(evsel__hw_cache_op, (config >> 8) & 0xff);
	if (op)
		snprintf(str + strlen(str), PERF_HW_CACHE_LEN - strlen(str),
			 "%s-", op);
	else
		snprintf(str + strlen(str), PERF_HW_CACHE_LEN - strlen(str),
			 "%lld-", (config >> 8) & 0xff);

	result = perf_event_name(evsel__hw_cache_result, config >> 16);
	if (result)
		snprintf(str + strlen(str), PERF_HW_CACHE_LEN - strlen(str),
			 "%s", result);
	else
		snprintf(str + strlen(str), PERF_HW_CACHE_LEN - strlen(str),
			 "%lld", config >> 16);
	return str;
}

static const char *perf_config_str(__u32 type, __u64 config)
{
	const char *perf_config;

	switch (type) {
	case PERF_TYPE_HARDWARE:
		perf_config = perf_event_name(event_symbols_hw, config);
		break;
	case PERF_TYPE_SOFTWARE:
		perf_config = perf_event_name(event_symbols_sw, config);
		break;
	case PERF_TYPE_HW_CACHE:
		perf_config = perf_config_hw_cache_str(config);
		break;
	default:
		perf_config = NULL;
		break;
	}
	return perf_config;
}

static void
show_perf_event_event_json(struct bpf_link_info *info, json_writer_t *wtr)
{
	__u64 config = info->perf_event.event.config;
	__u32 type = info->perf_event.event.type;
	const char *perf_type, *perf_config;

	perf_type = perf_event_name(perf_type_name, type);
	if (perf_type)
		jsonw_string_field(wtr, "event_type", perf_type);
	else
		jsonw_uint_field(wtr, "event_type", type);

	perf_config = perf_config_str(type, config);
	if (perf_config)
		jsonw_string_field(wtr, "event_config", perf_config);
	else
		jsonw_uint_field(wtr, "event_config", config);

	if (type == PERF_TYPE_HW_CACHE && perf_config)
		free((void *)perf_config);
}

static int show_link_close_json(int fd, struct bpf_link_info *info)
{
	struct bpf_prog_info prog_info;
	const char *prog_type_str;
	int err;

	jsonw_start_object(json_wtr);

	show_link_header_json(info, json_wtr);

	switch (info->type) {
	case BPF_LINK_TYPE_RAW_TRACEPOINT:
		jsonw_string_field(json_wtr, "tp_name",
				   u64_to_ptr(info->raw_tracepoint.tp_name));
		break;
	case BPF_LINK_TYPE_TRACING:
		err = get_prog_info(info->prog_id, &prog_info);
		if (err)
			return err;

		prog_type_str = libbpf_bpf_prog_type_str(prog_info.type);
		/* libbpf will return NULL for variants unknown to it. */
		if (prog_type_str)
			jsonw_string_field(json_wtr, "prog_type", prog_type_str);
		else
			jsonw_uint_field(json_wtr, "prog_type", prog_info.type);

		show_link_attach_type_json(info->tracing.attach_type,
					   json_wtr);
		jsonw_uint_field(json_wtr, "target_obj_id", info->tracing.target_obj_id);
		jsonw_uint_field(json_wtr, "target_btf_id", info->tracing.target_btf_id);
		break;
	case BPF_LINK_TYPE_CGROUP:
		jsonw_lluint_field(json_wtr, "cgroup_id",
				   info->cgroup.cgroup_id);
		show_link_attach_type_json(info->cgroup.attach_type, json_wtr);
		break;
	case BPF_LINK_TYPE_ITER:
		show_iter_json(info, json_wtr);
		break;
	case BPF_LINK_TYPE_NETNS:
		jsonw_uint_field(json_wtr, "netns_ino",
				 info->netns.netns_ino);
		show_link_attach_type_json(info->netns.attach_type, json_wtr);
		break;
	case BPF_LINK_TYPE_NETFILTER:
		netfilter_dump_json(info, json_wtr);
		break;
	case BPF_LINK_TYPE_STRUCT_OPS:
		jsonw_uint_field(json_wtr, "map_id",
				 info->struct_ops.map_id);
		break;
	case BPF_LINK_TYPE_KPROBE_MULTI:
		show_kprobe_multi_json(info, json_wtr);
		break;
	case BPF_LINK_TYPE_PERF_EVENT:
		switch (info->perf_event.type) {
		case BPF_PERF_EVENT_EVENT:
			show_perf_event_event_json(info, json_wtr);
			break;
		case BPF_PERF_EVENT_TRACEPOINT:
			show_perf_event_tracepoint_json(info, json_wtr);
			break;
		case BPF_PERF_EVENT_KPROBE:
		case BPF_PERF_EVENT_KRETPROBE:
			show_perf_event_kprobe_json(info, json_wtr);
			break;
		case BPF_PERF_EVENT_UPROBE:
		case BPF_PERF_EVENT_URETPROBE:
			show_perf_event_uprobe_json(info, json_wtr);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	if (!hashmap__empty(link_table)) {
		struct hashmap_entry *entry;

		jsonw_name(json_wtr, "pinned");
		jsonw_start_array(json_wtr);
		hashmap__for_each_key_entry(link_table, entry, info->id)
			jsonw_string(json_wtr, entry->pvalue);
		jsonw_end_array(json_wtr);
	}

	emit_obj_refs_json(refs_table, info->id, json_wtr);

	jsonw_end_object(json_wtr);

	return 0;
}

static void show_link_header_plain(struct bpf_link_info *info)
{
	const char *link_type_str;

	printf("%u: ", info->id);
	link_type_str = libbpf_bpf_link_type_str(info->type);
	if (link_type_str)
		printf("%s  ", link_type_str);
	else
		printf("type %u  ", info->type);

	if (info->type == BPF_LINK_TYPE_STRUCT_OPS)
		printf("map %u  ", info->struct_ops.map_id);
	else
		printf("prog %u  ", info->prog_id);
}

static void show_link_attach_type_plain(__u32 attach_type)
{
	const char *attach_type_str;

	attach_type_str = libbpf_bpf_attach_type_str(attach_type);
	if (attach_type_str)
		printf("attach_type %s  ", attach_type_str);
	else
		printf("attach_type %u  ", attach_type);
}

static void show_iter_plain(struct bpf_link_info *info)
{
	const char *target_name = u64_to_ptr(info->iter.target_name);

	printf("target_name %s  ", target_name);

	if (is_iter_map_target(target_name))
		printf("map_id %u  ", info->iter.map.map_id);
	else if (is_iter_task_target(target_name)) {
		if (info->iter.task.tid)
			printf("tid %u ", info->iter.task.tid);
		else if (info->iter.task.pid)
			printf("pid %u ", info->iter.task.pid);
	}

	if (is_iter_cgroup_target(target_name)) {
		printf("cgroup_id %llu  ", info->iter.cgroup.cgroup_id);
		printf("order %s  ",
		       cgroup_order_string(info->iter.cgroup.order));
	}
}

static const char * const pf2name[] = {
	[NFPROTO_INET] = "inet",
	[NFPROTO_IPV4] = "ip",
	[NFPROTO_ARP] = "arp",
	[NFPROTO_NETDEV] = "netdev",
	[NFPROTO_BRIDGE] = "bridge",
	[NFPROTO_IPV6] = "ip6",
};

static const char * const inethook2name[] = {
	[NF_INET_PRE_ROUTING] = "prerouting",
	[NF_INET_LOCAL_IN] = "input",
	[NF_INET_FORWARD] = "forward",
	[NF_INET_LOCAL_OUT] = "output",
	[NF_INET_POST_ROUTING] = "postrouting",
};

static const char * const arphook2name[] = {
	[NF_ARP_IN] = "input",
	[NF_ARP_OUT] = "output",
};

void netfilter_dump_plain(const struct bpf_link_info *info)
{
	const char *hookname = NULL, *pfname = NULL;
	unsigned int hook = info->netfilter.hooknum;
	unsigned int pf = info->netfilter.pf;

	if (pf < ARRAY_SIZE(pf2name))
		pfname = pf2name[pf];

	switch (pf) {
	case NFPROTO_BRIDGE: /* bridge shares numbers with enum nf_inet_hooks */
	case NFPROTO_IPV4:
	case NFPROTO_IPV6:
	case NFPROTO_INET:
		if (hook < ARRAY_SIZE(inethook2name))
			hookname = inethook2name[hook];
		break;
	case NFPROTO_ARP:
		if (hook < ARRAY_SIZE(arphook2name))
			hookname = arphook2name[hook];
	default:
		break;
	}

	if (pfname)
		printf("\n\t%s", pfname);
	else
		printf("\n\tpf: %d", pf);

	if (hookname)
		printf(" %s", hookname);
	else
		printf(", hook %u,", hook);

	printf(" prio %d", info->netfilter.priority);

	if (info->netfilter.flags)
		printf(" flags 0x%x", info->netfilter.flags);
}

static void show_kprobe_multi_plain(struct bpf_link_info *info)
{
	__u32 i, j = 0;
	__u64 *addrs;

	if (!info->kprobe_multi.count)
		return;

	if (info->kprobe_multi.flags & BPF_F_KPROBE_MULTI_RETURN)
		printf("\n\tkretprobe.multi  ");
	else
		printf("\n\tkprobe.multi  ");
	printf("func_cnt %u  ", info->kprobe_multi.count);
	addrs = (__u64 *)u64_to_ptr(info->kprobe_multi.addrs);
	qsort(addrs, info->kprobe_multi.count, sizeof(__u64), cmp_u64);

	/* Load it once for all. */
	if (!dd.sym_count)
		kernel_syms_load(&dd);
	if (!dd.sym_count)
		return;

	printf("\n\t%-16s %s", "addr", "func [module]");
	for (i = 0; i < dd.sym_count; i++) {
		if (dd.sym_mapping[i].address != addrs[j])
			continue;
		printf("\n\t%016lx %s",
		       dd.sym_mapping[i].address, dd.sym_mapping[i].name);
		if (dd.sym_mapping[i].module[0] != '\0')
			printf(" [%s]  ", dd.sym_mapping[i].module);
		else
			printf("  ");

		if (j++ == info->kprobe_multi.count)
			break;
	}
}

static void show_perf_event_kprobe_plain(struct bpf_link_info *info)
{
	const char *buf;

	buf = u64_to_ptr(info->perf_event.kprobe.func_name);
	if (buf[0] == '\0' && !info->perf_event.kprobe.addr)
		return;

	if (info->perf_event.type == BPF_PERF_EVENT_KRETPROBE)
		printf("\n\tkretprobe ");
	else
		printf("\n\tkprobe ");
	if (info->perf_event.kprobe.addr)
		printf("%llx ", info->perf_event.kprobe.addr);
	printf("%s", buf);
	if (info->perf_event.kprobe.offset)
		printf("+%#x", info->perf_event.kprobe.offset);
	printf("  ");
}

static void show_perf_event_uprobe_plain(struct bpf_link_info *info)
{
	const char *buf;

	buf = u64_to_ptr(info->perf_event.uprobe.file_name);
	if (buf[0] == '\0')
		return;

	if (info->perf_event.type == BPF_PERF_EVENT_URETPROBE)
		printf("\n\turetprobe ");
	else
		printf("\n\tuprobe ");
	printf("%s+%#x  ", buf, info->perf_event.uprobe.offset);
}

static void show_perf_event_tracepoint_plain(struct bpf_link_info *info)
{
	const char *buf;

	buf = u64_to_ptr(info->perf_event.tracepoint.tp_name);
	if (buf[0] == '\0')
		return;

	printf("\n\ttracepoint %s  ", buf);
}

static void show_perf_event_event_plain(struct bpf_link_info *info)
{
	__u64 config = info->perf_event.event.config;
	__u32 type = info->perf_event.event.type;
	const char *perf_type, *perf_config;

	printf("\n\tevent ");
	perf_type = perf_event_name(perf_type_name, type);
	if (perf_type)
		printf("%s:", perf_type);
	else
		printf("%u :", type);

	perf_config = perf_config_str(type, config);
	if (perf_config)
		printf("%s  ", perf_config);
	else
		printf("%llu  ", config);

	if (type == PERF_TYPE_HW_CACHE && perf_config)
		free((void *)perf_config);
}

static int show_link_close_plain(int fd, struct bpf_link_info *info)
{
	struct bpf_prog_info prog_info;
	const char *prog_type_str;
	int err;

	show_link_header_plain(info);

	switch (info->type) {
	case BPF_LINK_TYPE_RAW_TRACEPOINT:
		printf("\n\ttp '%s'  ",
		       (const char *)u64_to_ptr(info->raw_tracepoint.tp_name));
		break;
	case BPF_LINK_TYPE_TRACING:
		err = get_prog_info(info->prog_id, &prog_info);
		if (err)
			return err;

		prog_type_str = libbpf_bpf_prog_type_str(prog_info.type);
		/* libbpf will return NULL for variants unknown to it. */
		if (prog_type_str)
			printf("\n\tprog_type %s  ", prog_type_str);
		else
			printf("\n\tprog_type %u  ", prog_info.type);

		show_link_attach_type_plain(info->tracing.attach_type);
		if (info->tracing.target_obj_id || info->tracing.target_btf_id)
			printf("\n\ttarget_obj_id %u  target_btf_id %u  ",
			       info->tracing.target_obj_id,
			       info->tracing.target_btf_id);
		break;
	case BPF_LINK_TYPE_CGROUP:
		printf("\n\tcgroup_id %zu  ", (size_t)info->cgroup.cgroup_id);
		show_link_attach_type_plain(info->cgroup.attach_type);
		break;
	case BPF_LINK_TYPE_ITER:
		show_iter_plain(info);
		break;
	case BPF_LINK_TYPE_NETNS:
		printf("\n\tnetns_ino %u  ", info->netns.netns_ino);
		show_link_attach_type_plain(info->netns.attach_type);
		break;
	case BPF_LINK_TYPE_NETFILTER:
		netfilter_dump_plain(info);
		break;
	case BPF_LINK_TYPE_KPROBE_MULTI:
		show_kprobe_multi_plain(info);
		break;
	case BPF_LINK_TYPE_PERF_EVENT:
		switch (info->perf_event.type) {
		case BPF_PERF_EVENT_EVENT:
			show_perf_event_event_plain(info);
			break;
		case BPF_PERF_EVENT_TRACEPOINT:
			show_perf_event_tracepoint_plain(info);
			break;
		case BPF_PERF_EVENT_KPROBE:
		case BPF_PERF_EVENT_KRETPROBE:
			show_perf_event_kprobe_plain(info);
			break;
		case BPF_PERF_EVENT_UPROBE:
		case BPF_PERF_EVENT_URETPROBE:
			show_perf_event_uprobe_plain(info);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	if (!hashmap__empty(link_table)) {
		struct hashmap_entry *entry;

		hashmap__for_each_key_entry(link_table, entry, info->id)
			printf("\n\tpinned %s", (char *)entry->pvalue);
	}
	emit_obj_refs_plain(refs_table, info->id, "\n\tpids ");

	printf("\n");

	return 0;
}

static int do_show_link(int fd)
{
	struct bpf_link_info info;
	__u32 len = sizeof(info);
	__u64 *addrs = NULL;
	char buf[PATH_MAX];
	int count;
	int err;

	memset(&info, 0, sizeof(info));
	buf[0] = '\0';
again:
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	if (err) {
		p_err("can't get link info: %s",
		      strerror(errno));
		close(fd);
		return err;
	}
	if (info.type == BPF_LINK_TYPE_RAW_TRACEPOINT &&
	    !info.raw_tracepoint.tp_name) {
		info.raw_tracepoint.tp_name = ptr_to_u64(&buf);
		info.raw_tracepoint.tp_name_len = sizeof(buf);
		goto again;
	}
	if (info.type == BPF_LINK_TYPE_ITER &&
	    !info.iter.target_name) {
		info.iter.target_name = ptr_to_u64(&buf);
		info.iter.target_name_len = sizeof(buf);
		goto again;
	}
	if (info.type == BPF_LINK_TYPE_KPROBE_MULTI &&
	    !info.kprobe_multi.addrs) {
		count = info.kprobe_multi.count;
		if (count) {
			addrs = calloc(count, sizeof(__u64));
			if (!addrs) {
				p_err("mem alloc failed");
				close(fd);
				return -ENOMEM;
			}
			info.kprobe_multi.addrs = ptr_to_u64(addrs);
			goto again;
		}
	}
	if (info.type == BPF_LINK_TYPE_PERF_EVENT) {
		switch (info.perf_event.type) {
		case BPF_PERF_EVENT_TRACEPOINT:
			if (!info.perf_event.tracepoint.tp_name) {
				info.perf_event.tracepoint.tp_name = ptr_to_u64(&buf);
				info.perf_event.tracepoint.name_len = sizeof(buf);
				goto again;
			}
			break;
		case BPF_PERF_EVENT_KPROBE:
		case BPF_PERF_EVENT_KRETPROBE:
			if (!info.perf_event.kprobe.func_name) {
				info.perf_event.kprobe.func_name = ptr_to_u64(&buf);
				info.perf_event.kprobe.name_len = sizeof(buf);
				goto again;
			}
			break;
		case BPF_PERF_EVENT_UPROBE:
		case BPF_PERF_EVENT_URETPROBE:
			if (!info.perf_event.uprobe.file_name) {
				info.perf_event.uprobe.file_name = ptr_to_u64(&buf);
				info.perf_event.uprobe.name_len = sizeof(buf);
				goto again;
			}
			break;
		default:
			break;
		}
	}

	if (json_output)
		show_link_close_json(fd, &info);
	else
		show_link_close_plain(fd, &info);

	if (addrs)
		free(addrs);
	close(fd);
	return 0;
}

static int do_show(int argc, char **argv)
{
	__u32 id = 0;
	int err, fd;

	if (show_pinned) {
		link_table = hashmap__new(hash_fn_for_key_as_id,
					  equal_fn_for_key_as_id, NULL);
		if (IS_ERR(link_table)) {
			p_err("failed to create hashmap for pinned paths");
			return -1;
		}
		build_pinned_obj_table(link_table, BPF_OBJ_LINK);
	}
	build_obj_refs_table(&refs_table, BPF_OBJ_LINK);

	if (argc == 2) {
		fd = link_parse_fd(&argc, &argv);
		if (fd < 0)
			return fd;
		do_show_link(fd);
		goto out;
	}

	if (argc)
		return BAD_ARG();

	if (json_output)
		jsonw_start_array(json_wtr);
	while (true) {
		err = bpf_link_get_next_id(id, &id);
		if (err) {
			if (errno == ENOENT)
				break;
			p_err("can't get next link: %s%s", strerror(errno),
			      errno == EINVAL ? " -- kernel too old?" : "");
			break;
		}

		fd = bpf_link_get_fd_by_id(id);
		if (fd < 0) {
			if (errno == ENOENT)
				continue;
			p_err("can't get link by id (%u): %s",
			      id, strerror(errno));
			break;
		}

		err = do_show_link(fd);
		if (err)
			break;
	}
	if (json_output)
		jsonw_end_array(json_wtr);

	delete_obj_refs_table(refs_table);

	if (show_pinned)
		delete_pinned_obj_table(link_table);

out:
	if (dd.sym_count)
		kernel_syms_destroy(&dd);
	return errno == ENOENT ? 0 : -1;
}

static int do_pin(int argc, char **argv)
{
	int err;

	err = do_pin_any(argc, argv, link_parse_fd);
	if (!err && json_output)
		jsonw_null(json_wtr);
	return err;
}

static int do_detach(int argc, char **argv)
{
	int err, fd;

	if (argc != 2) {
		p_err("link specifier is invalid or missing\n");
		return 1;
	}

	fd = link_parse_fd(&argc, &argv);
	if (fd < 0)
		return 1;

	err = bpf_link_detach(fd);
	if (err)
		err = -errno;
	close(fd);
	if (err) {
		p_err("failed link detach: %s", strerror(-err));
		return 1;
	}

	if (json_output)
		jsonw_null(json_wtr);

	return 0;
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %1$s %2$s { show | list }   [LINK]\n"
		"       %1$s %2$s pin        LINK  FILE\n"
		"       %1$s %2$s detach     LINK\n"
		"       %1$s %2$s help\n"
		"\n"
		"       " HELP_SPEC_LINK "\n"
		"       " HELP_SPEC_OPTIONS " |\n"
		"                    {-f|--bpffs} | {-n|--nomount} }\n"
		"",
		bin_name, argv[-2]);

	return 0;
}

static const struct cmd cmds[] = {
	{ "show",	do_show },
	{ "list",	do_show },
	{ "help",	do_help },
	{ "pin",	do_pin },
	{ "detach",	do_detach },
	{ 0 }
};

int do_link(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}
