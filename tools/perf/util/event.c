#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <perf/cpumap.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uapi/linux/mman.h> /* To get things like MAP_HUGETLB even on older libc headers */
#include <linux/perf_event.h>
#include <linux/zalloc.h>
#include "cpumap.h"
#include "dso.h"
#include "event.h"
#include "debug.h"
#include "hist.h"
#include "machine.h"
#include "sort.h"
#include "string2.h"
#include "strlist.h"
#include "thread.h"
#include "thread_map.h"
#include "time-utils.h"
#include <linux/ctype.h>
#include "map.h"
#include "util/namespaces.h"
#include "symbol.h"
#include "symbol/kallsyms.h"
#include "asm/bug.h"
#include "stat.h"
#include "session.h"
#include "bpf-event.h"
#include "print_binary.h"
#include "tool.h"
#include "util.h"

static const char *perf_event__names[] = {
	[0]					= "TOTAL",
	[PERF_RECORD_MMAP]			= "MMAP",
	[PERF_RECORD_MMAP2]			= "MMAP2",
	[PERF_RECORD_LOST]			= "LOST",
	[PERF_RECORD_COMM]			= "COMM",
	[PERF_RECORD_EXIT]			= "EXIT",
	[PERF_RECORD_THROTTLE]			= "THROTTLE",
	[PERF_RECORD_UNTHROTTLE]		= "UNTHROTTLE",
	[PERF_RECORD_FORK]			= "FORK",
	[PERF_RECORD_READ]			= "READ",
	[PERF_RECORD_SAMPLE]			= "SAMPLE",
	[PERF_RECORD_AUX]			= "AUX",
	[PERF_RECORD_ITRACE_START]		= "ITRACE_START",
	[PERF_RECORD_LOST_SAMPLES]		= "LOST_SAMPLES",
	[PERF_RECORD_SWITCH]			= "SWITCH",
	[PERF_RECORD_SWITCH_CPU_WIDE]		= "SWITCH_CPU_WIDE",
	[PERF_RECORD_NAMESPACES]		= "NAMESPACES",
	[PERF_RECORD_KSYMBOL]			= "KSYMBOL",
	[PERF_RECORD_BPF_EVENT]			= "BPF_EVENT",
	[PERF_RECORD_CGROUP]			= "CGROUP",
	[PERF_RECORD_TEXT_POKE]			= "TEXT_POKE",
	[PERF_RECORD_AUX_OUTPUT_HW_ID]		= "AUX_OUTPUT_HW_ID",
	[PERF_RECORD_HEADER_ATTR]		= "ATTR",
	[PERF_RECORD_HEADER_EVENT_TYPE]		= "EVENT_TYPE",
	[PERF_RECORD_HEADER_TRACING_DATA]	= "TRACING_DATA",
	[PERF_RECORD_HEADER_BUILD_ID]		= "BUILD_ID",
	[PERF_RECORD_FINISHED_ROUND]		= "FINISHED_ROUND",
	[PERF_RECORD_ID_INDEX]			= "ID_INDEX",
	[PERF_RECORD_AUXTRACE_INFO]		= "AUXTRACE_INFO",
	[PERF_RECORD_AUXTRACE]			= "AUXTRACE",
	[PERF_RECORD_AUXTRACE_ERROR]		= "AUXTRACE_ERROR",
	[PERF_RECORD_THREAD_MAP]		= "THREAD_MAP",
	[PERF_RECORD_CPU_MAP]			= "CPU_MAP",
	[PERF_RECORD_STAT_CONFIG]		= "STAT_CONFIG",
	[PERF_RECORD_STAT]			= "STAT",
	[PERF_RECORD_STAT_ROUND]		= "STAT_ROUND",
	[PERF_RECORD_EVENT_UPDATE]		= "EVENT_UPDATE",
	[PERF_RECORD_TIME_CONV]			= "TIME_CONV",
	[PERF_RECORD_HEADER_FEATURE]		= "FEATURE",
	[PERF_RECORD_COMPRESSED]		= "COMPRESSED",
	[PERF_RECORD_FINISHED_INIT]		= "FINISHED_INIT",
};

const char *perf_event__name(unsigned int id)
{
	if (id >= ARRAY_SIZE(perf_event__names))
		return "INVALID";
	if (!perf_event__names[id])
		return "UNKNOWN";
	return perf_event__names[id];
}

struct process_symbol_args {
	const char *name;
	u64	   start;
};

static int find_func_symbol_cb(void *arg, const char *name, char type,
			       u64 start)
{
	struct process_symbol_args *args = arg;

	/*
	 * Must be a function or at least an alias, as in PARISC64, where "_text" is
	 * an 'A' to the same address as "_stext".
	 */
	if (!(kallsyms__is_function(type) ||
	      type == 'A') || strcmp(name, args->name))
		return 0;

	args->start = start;
	return 1;
}

static int find_any_symbol_cb(void *arg, const char *name,
			      char type __maybe_unused, u64 start)
{
	struct process_symbol_args *args = arg;

	if (strcmp(name, args->name))
		return 0;

	args->start = start;
	return 1;
}

int kallsyms__get_function_start(const char *kallsyms_filename,
				 const char *symbol_name, u64 *addr)
{
	struct process_symbol_args args = { .name = symbol_name, };

	if (kallsyms__parse(kallsyms_filename, &args, find_func_symbol_cb) <= 0)
		return -1;

	*addr = args.start;
	return 0;
}

int kallsyms__get_symbol_start(const char *kallsyms_filename,
			       const char *symbol_name, u64 *addr)
{
	struct process_symbol_args args = { .name = symbol_name, };

	if (kallsyms__parse(kallsyms_filename, &args, find_any_symbol_cb) <= 0)
		return -1;

	*addr = args.start;
	return 0;
}

void perf_event__read_stat_config(struct perf_stat_config *config,
				  struct perf_record_stat_config *event)
{
	unsigned i;

	for (i = 0; i < event->nr; i++) {

		switch (event->data[i].tag) {
#define CASE(__term, __val)					\
		case PERF_STAT_CONFIG_TERM__##__term:		\
			config->__val = event->data[i].val;	\
			break;

		CASE(AGGR_MODE,  aggr_mode)
		CASE(SCALE,      scale)
		CASE(INTERVAL,   interval)
		CASE(AGGR_LEVEL, aggr_level)
#undef CASE
		default:
			pr_warning("unknown stat config term %" PRI_lu64 "\n",
				   event->data[i].tag);
		}
	}
}

size_t perf_event__fprintf_comm(union perf_event *event, FILE *fp)
{
	const char *s;

	if (event->header.misc & PERF_RECORD_MISC_COMM_EXEC)
		s = " exec";
	else
		s = "";

	return fprintf(fp, "%s: %s:%d/%d\n", s, event->comm.comm, event->comm.pid, event->comm.tid);
}

size_t perf_event__fprintf_namespaces(union perf_event *event, FILE *fp)
{
	size_t ret = 0;
	struct perf_ns_link_info *ns_link_info;
	u32 nr_namespaces, idx;

	ns_link_info = event->namespaces.link_info;
	nr_namespaces = event->namespaces.nr_namespaces;

	ret += fprintf(fp, " %d/%d - nr_namespaces: %u\n\t\t[",
		       event->namespaces.pid,
		       event->namespaces.tid,
		       nr_namespaces);

	for (idx = 0; idx < nr_namespaces; idx++) {
		if (idx && (idx % 4 == 0))
			ret += fprintf(fp, "\n\t\t ");

		ret  += fprintf(fp, "%u/%s: %" PRIu64 "/%#" PRIx64 "%s", idx,
				perf_ns__name(idx), (u64)ns_link_info[idx].dev,
				(u64)ns_link_info[idx].ino,
				((idx + 1) != nr_namespaces) ? ", " : "]\n");
	}

	return ret;
}

size_t perf_event__fprintf_cgroup(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " cgroup: %" PRI_lu64 " %s\n",
		       event->cgroup.id, event->cgroup.path);
}

int perf_event__process_comm(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_comm_event(machine, event, sample);
}

int perf_event__process_namespaces(struct perf_tool *tool __maybe_unused,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	return machine__process_namespaces_event(machine, event, sample);
}

int perf_event__process_cgroup(struct perf_tool *tool __maybe_unused,
			       union perf_event *event,
			       struct perf_sample *sample,
			       struct machine *machine)
{
	return machine__process_cgroup_event(machine, event, sample);
}

int perf_event__process_lost(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_lost_event(machine, event, sample);
}

int perf_event__process_aux(struct perf_tool *tool __maybe_unused,
			    union perf_event *event,
			    struct perf_sample *sample __maybe_unused,
			    struct machine *machine)
{
	return machine__process_aux_event(machine, event);
}

int perf_event__process_itrace_start(struct perf_tool *tool __maybe_unused,
				     union perf_event *event,
				     struct perf_sample *sample __maybe_unused,
				     struct machine *machine)
{
	return machine__process_itrace_start_event(machine, event);
}

int perf_event__process_aux_output_hw_id(struct perf_tool *tool __maybe_unused,
					 union perf_event *event,
					 struct perf_sample *sample __maybe_unused,
					 struct machine *machine)
{
	return machine__process_aux_output_hw_id_event(machine, event);
}

int perf_event__process_lost_samples(struct perf_tool *tool __maybe_unused,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct machine *machine)
{
	return machine__process_lost_samples_event(machine, event, sample);
}

int perf_event__process_switch(struct perf_tool *tool __maybe_unused,
			       union perf_event *event,
			       struct perf_sample *sample __maybe_unused,
			       struct machine *machine)
{
	return machine__process_switch_event(machine, event);
}

int perf_event__process_ksymbol(struct perf_tool *tool __maybe_unused,
				union perf_event *event,
				struct perf_sample *sample __maybe_unused,
				struct machine *machine)
{
	return machine__process_ksymbol(machine, event, sample);
}

int perf_event__process_bpf(struct perf_tool *tool __maybe_unused,
			    union perf_event *event,
			    struct perf_sample *sample,
			    struct machine *machine)
{
	return machine__process_bpf(machine, event, sample);
}

int perf_event__process_text_poke(struct perf_tool *tool __maybe_unused,
				  union perf_event *event,
				  struct perf_sample *sample,
				  struct machine *machine)
{
	return machine__process_text_poke(machine, event, sample);
}

size_t perf_event__fprintf_mmap(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " %d/%d: [%#" PRI_lx64 "(%#" PRI_lx64 ") @ %#" PRI_lx64 "]: %c %s\n",
		       event->mmap.pid, event->mmap.tid, event->mmap.start,
		       event->mmap.len, event->mmap.pgoff,
		       (event->header.misc & PERF_RECORD_MISC_MMAP_DATA) ? 'r' : 'x',
		       event->mmap.filename);
}

size_t perf_event__fprintf_mmap2(union perf_event *event, FILE *fp)
{
	if (event->header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID) {
		char sbuild_id[SBUILD_ID_SIZE];
		struct build_id bid;

		build_id__init(&bid, event->mmap2.build_id,
			       event->mmap2.build_id_size);
		build_id__sprintf(&bid, sbuild_id);

		return fprintf(fp, " %d/%d: [%#" PRI_lx64 "(%#" PRI_lx64 ") @ %#" PRI_lx64
				   " <%s>]: %c%c%c%c %s\n",
			       event->mmap2.pid, event->mmap2.tid, event->mmap2.start,
			       event->mmap2.len, event->mmap2.pgoff, sbuild_id,
			       (event->mmap2.prot & PROT_READ) ? 'r' : '-',
			       (event->mmap2.prot & PROT_WRITE) ? 'w' : '-',
			       (event->mmap2.prot & PROT_EXEC) ? 'x' : '-',
			       (event->mmap2.flags & MAP_SHARED) ? 's' : 'p',
			       event->mmap2.filename);
	} else {
		return fprintf(fp, " %d/%d: [%#" PRI_lx64 "(%#" PRI_lx64 ") @ %#" PRI_lx64
				   " %02x:%02x %"PRI_lu64" %"PRI_lu64"]: %c%c%c%c %s\n",
			       event->mmap2.pid, event->mmap2.tid, event->mmap2.start,
			       event->mmap2.len, event->mmap2.pgoff, event->mmap2.maj,
			       event->mmap2.min, event->mmap2.ino,
			       event->mmap2.ino_generation,
			       (event->mmap2.prot & PROT_READ) ? 'r' : '-',
			       (event->mmap2.prot & PROT_WRITE) ? 'w' : '-',
			       (event->mmap2.prot & PROT_EXEC) ? 'x' : '-',
			       (event->mmap2.flags & MAP_SHARED) ? 's' : 'p',
			       event->mmap2.filename);
	}
}

size_t perf_event__fprintf_thread_map(union perf_event *event, FILE *fp)
{
	struct perf_thread_map *threads = thread_map__new_event(&event->thread_map);
	size_t ret;

	ret = fprintf(fp, " nr: ");

	if (threads)
		ret += thread_map__fprintf(threads, fp);
	else
		ret += fprintf(fp, "failed to get threads from event\n");

	perf_thread_map__put(threads);
	return ret;
}

size_t perf_event__fprintf_cpu_map(union perf_event *event, FILE *fp)
{
	struct perf_cpu_map *cpus = cpu_map__new_data(&event->cpu_map.data);
	size_t ret;

	ret = fprintf(fp, ": ");

	if (cpus)
		ret += cpu_map__fprintf(cpus, fp);
	else
		ret += fprintf(fp, "failed to get cpumap from event\n");

	perf_cpu_map__put(cpus);
	return ret;
}

int perf_event__process_mmap(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_mmap_event(machine, event, sample);
}

int perf_event__process_mmap2(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_mmap2_event(machine, event, sample);
}

size_t perf_event__fprintf_task(union perf_event *event, FILE *fp)
{
	return fprintf(fp, "(%d:%d):(%d:%d)\n",
		       event->fork.pid, event->fork.tid,
		       event->fork.ppid, event->fork.ptid);
}

int perf_event__process_fork(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_fork_event(machine, event, sample);
}

int perf_event__process_exit(struct perf_tool *tool __maybe_unused,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	return machine__process_exit_event(machine, event, sample);
}

size_t perf_event__fprintf_aux(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " offset: %#"PRI_lx64" size: %#"PRI_lx64" flags: %#"PRI_lx64" [%s%s%s]\n",
		       event->aux.aux_offset, event->aux.aux_size,
		       event->aux.flags,
		       event->aux.flags & PERF_AUX_FLAG_TRUNCATED ? "T" : "",
		       event->aux.flags & PERF_AUX_FLAG_OVERWRITE ? "O" : "",
		       event->aux.flags & PERF_AUX_FLAG_PARTIAL   ? "P" : "");
}

size_t perf_event__fprintf_itrace_start(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " pid: %u tid: %u\n",
		       event->itrace_start.pid, event->itrace_start.tid);
}

size_t perf_event__fprintf_aux_output_hw_id(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " hw_id: %#"PRI_lx64"\n",
		       event->aux_output_hw_id.hw_id);
}

size_t perf_event__fprintf_switch(union perf_event *event, FILE *fp)
{
	bool out = event->header.misc & PERF_RECORD_MISC_SWITCH_OUT;
	const char *in_out = !out ? "IN         " :
		!(event->header.misc & PERF_RECORD_MISC_SWITCH_OUT_PREEMPT) ?
				    "OUT        " : "OUT preempt";

	if (event->header.type == PERF_RECORD_SWITCH)
		return fprintf(fp, " %s\n", in_out);

	return fprintf(fp, " %s  %s pid/tid: %5d/%-5d\n",
		       in_out, out ? "next" : "prev",
		       event->context_switch.next_prev_pid,
		       event->context_switch.next_prev_tid);
}

static size_t perf_event__fprintf_lost(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " lost %" PRI_lu64 "\n", event->lost.lost);
}

size_t perf_event__fprintf_ksymbol(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " addr %" PRI_lx64 " len %u type %u flags 0x%x name %s\n",
		       event->ksymbol.addr, event->ksymbol.len,
		       event->ksymbol.ksym_type,
		       event->ksymbol.flags, event->ksymbol.name);
}

size_t perf_event__fprintf_bpf(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " type %u, flags %u, id %u\n",
		       event->bpf.type, event->bpf.flags, event->bpf.id);
}

static int text_poke_printer(enum binary_printer_ops op, unsigned int val,
			     void *extra, FILE *fp)
{
	bool old = *(bool *)extra;

	switch ((int)op) {
	case BINARY_PRINT_LINE_BEGIN:
		return fprintf(fp, "            %s bytes:", old ? "Old" : "New");
	case BINARY_PRINT_NUM_DATA:
		return fprintf(fp, " %02x", val);
	case BINARY_PRINT_LINE_END:
		return fprintf(fp, "\n");
	default:
		return 0;
	}
}

size_t perf_event__fprintf_text_poke(union perf_event *event, struct machine *machine, FILE *fp)
{
	struct perf_record_text_poke_event *tp = &event->text_poke;
	size_t ret;
	bool old;

	ret = fprintf(fp, " %" PRI_lx64 " ", tp->addr);
	if (machine) {
		struct addr_location al;

		addr_location__init(&al);
		al.map = map__get(maps__find(machine__kernel_maps(machine), tp->addr));
		if (al.map && map__load(al.map) >= 0) {
			al.addr = map__map_ip(al.map, tp->addr);
			al.sym = map__find_symbol(al.map, al.addr);
			if (al.sym)
				ret += symbol__fprintf_symname_offs(al.sym, &al, fp);
		}
		addr_location__exit(&al);
	}
	ret += fprintf(fp, " old len %u new len %u\n", tp->old_len, tp->new_len);
	old = true;
	ret += binary__fprintf(tp->bytes, tp->old_len, 16, text_poke_printer,
			       &old, fp);
	old = false;
	ret += binary__fprintf(tp->bytes + tp->old_len, tp->new_len, 16,
			       text_poke_printer, &old, fp);
	return ret;
}

size_t perf_event__fprintf(union perf_event *event, struct machine *machine, FILE *fp)
{
	size_t ret = fprintf(fp, "PERF_RECORD_%s",
			     perf_event__name(event->header.type));

	switch (event->header.type) {
	case PERF_RECORD_COMM:
		ret += perf_event__fprintf_comm(event, fp);
		break;
	case PERF_RECORD_FORK:
	case PERF_RECORD_EXIT:
		ret += perf_event__fprintf_task(event, fp);
		break;
	case PERF_RECORD_MMAP:
		ret += perf_event__fprintf_mmap(event, fp);
		break;
	case PERF_RECORD_NAMESPACES:
		ret += perf_event__fprintf_namespaces(event, fp);
		break;
	case PERF_RECORD_CGROUP:
		ret += perf_event__fprintf_cgroup(event, fp);
		break;
	case PERF_RECORD_MMAP2:
		ret += perf_event__fprintf_mmap2(event, fp);
		break;
	case PERF_RECORD_AUX:
		ret += perf_event__fprintf_aux(event, fp);
		break;
	case PERF_RECORD_ITRACE_START:
		ret += perf_event__fprintf_itrace_start(event, fp);
		break;
	case PERF_RECORD_SWITCH:
	case PERF_RECORD_SWITCH_CPU_WIDE:
		ret += perf_event__fprintf_switch(event, fp);
		break;
	case PERF_RECORD_LOST:
		ret += perf_event__fprintf_lost(event, fp);
		break;
	case PERF_RECORD_KSYMBOL:
		ret += perf_event__fprintf_ksymbol(event, fp);
		break;
	case PERF_RECORD_BPF_EVENT:
		ret += perf_event__fprintf_bpf(event, fp);
		break;
	case PERF_RECORD_TEXT_POKE:
		ret += perf_event__fprintf_text_poke(event, machine, fp);
		break;
	case PERF_RECORD_AUX_OUTPUT_HW_ID:
		ret += perf_event__fprintf_aux_output_hw_id(event, fp);
		break;
	default:
		ret += fprintf(fp, "\n");
	}

	return ret;
}

int perf_event__process(struct perf_tool *tool __maybe_unused,
			union perf_event *event,
			struct perf_sample *sample,
			struct machine *machine)
{
	return machine__process_event(machine, event, sample);
}

struct map *thread__find_map(struct thread *thread, u8 cpumode, u64 addr,
			     struct addr_location *al)
{
	struct maps *maps = thread__maps(thread);
	struct machine *machine = maps__machine(maps);
	bool load_map = false;

	maps__zput(al->maps);
	map__zput(al->map);
	thread__zput(al->thread);
	al->thread = thread__get(thread);

	al->addr = addr;
	al->cpumode = cpumode;
	al->filtered = 0;

	if (machine == NULL)
		return NULL;

	if (cpumode == PERF_RECORD_MISC_KERNEL && perf_host) {
		al->level = 'k';
		maps = machine__kernel_maps(machine);
		load_map = true;
	} else if (cpumode == PERF_RECORD_MISC_USER && perf_host) {
		al->level = '.';
	} else if (cpumode == PERF_RECORD_MISC_GUEST_KERNEL && perf_guest) {
		al->level = 'g';
		maps = machine__kernel_maps(machine);
		load_map = true;
	} else if (cpumode == PERF_RECORD_MISC_GUEST_USER && perf_guest) {
		al->level = 'u';
	} else {
		al->level = 'H';

		if ((cpumode == PERF_RECORD_MISC_GUEST_USER ||
			cpumode == PERF_RECORD_MISC_GUEST_KERNEL) &&
			!perf_guest)
			al->filtered |= (1 << HIST_FILTER__GUEST);
		if ((cpumode == PERF_RECORD_MISC_USER ||
			cpumode == PERF_RECORD_MISC_KERNEL) &&
			!perf_host)
			al->filtered |= (1 << HIST_FILTER__HOST);

		return NULL;
	}
	al->maps = maps__get(maps);
	al->map = map__get(maps__find(maps, al->addr));
	if (al->map != NULL) {
		/*
		 * Kernel maps might be changed when loading symbols so loading
		 * must be done prior to using kernel maps.
		 */
		if (load_map)
			map__load(al->map);
		al->addr = map__map_ip(al->map, al->addr);
	}

	return al->map;
}

/*
 * For branch stacks or branch samples, the sample cpumode might not be correct
 * because it applies only to the sample 'ip' and not necessary to 'addr' or
 * branch stack addresses. If possible, use a fallback to deal with those cases.
 */
struct map *thread__find_map_fb(struct thread *thread, u8 cpumode, u64 addr,
				struct addr_location *al)
{
	struct map *map = thread__find_map(thread, cpumode, addr, al);
	struct machine *machine = maps__machine(thread__maps(thread));
	u8 addr_cpumode = machine__addr_cpumode(machine, cpumode, addr);

	if (map || addr_cpumode == cpumode)
		return map;

	return thread__find_map(thread, addr_cpumode, addr, al);
}

struct symbol *thread__find_symbol(struct thread *thread, u8 cpumode,
				   u64 addr, struct addr_location *al)
{
	al->sym = NULL;
	if (thread__find_map(thread, cpumode, addr, al))
		al->sym = map__find_symbol(al->map, al->addr);
	return al->sym;
}

struct symbol *thread__find_symbol_fb(struct thread *thread, u8 cpumode,
				      u64 addr, struct addr_location *al)
{
	al->sym = NULL;
	if (thread__find_map_fb(thread, cpumode, addr, al))
		al->sym = map__find_symbol(al->map, al->addr);
	return al->sym;
}

static bool check_address_range(struct intlist *addr_list, int addr_range,
				unsigned long addr)
{
	struct int_node *pos;

	intlist__for_each_entry(pos, addr_list) {
		if (addr >= pos->i && addr < pos->i + addr_range)
			return true;
	}

	return false;
}

/*
 * Callers need to drop the reference to al->thread, obtained in
 * machine__findnew_thread()
 */
int machine__resolve(struct machine *machine, struct addr_location *al,
		     struct perf_sample *sample)
{
	struct thread *thread;
	struct dso *dso;

	if (symbol_conf.guest_code && !machine__is_host(machine))
		thread = machine__findnew_guest_code(machine, sample->pid);
	else
		thread = machine__findnew_thread(machine, sample->pid, sample->tid);
	if (thread == NULL)
		return -1;

	dump_printf(" ... thread: %s:%d\n", thread__comm_str(thread), thread__tid(thread));
	thread__find_map(thread, sample->cpumode, sample->ip, al);
	dso = al->map ? map__dso(al->map) : NULL;
	dump_printf(" ...... dso: %s\n",
		dso
		? dso->long_name
		: (al->level == 'H' ? "[hypervisor]" : "<not found>"));

	if (thread__is_filtered(thread))
		al->filtered |= (1 << HIST_FILTER__THREAD);

	thread__put(thread);
	thread = NULL;

	al->sym = NULL;
	al->cpu = sample->cpu;
	al->socket = -1;
	al->srcline = NULL;

	if (al->cpu >= 0) {
		struct perf_env *env = machine->env;

		if (env && env->cpu)
			al->socket = env->cpu[al->cpu].socket_id;
	}

	if (al->map) {
		if (symbol_conf.dso_list &&
		    (!dso || !(strlist__has_entry(symbol_conf.dso_list,
						  dso->short_name) ||
			       (dso->short_name != dso->long_name &&
				strlist__has_entry(symbol_conf.dso_list,
						   dso->long_name))))) {
			al->filtered |= (1 << HIST_FILTER__DSO);
		}

		al->sym = map__find_symbol(al->map, al->addr);
	} else if (symbol_conf.dso_list) {
		al->filtered |= (1 << HIST_FILTER__DSO);
	}

	if (symbol_conf.sym_list) {
		int ret = 0;
		char al_addr_str[32];
		size_t sz = sizeof(al_addr_str);

		if (al->sym) {
			ret = strlist__has_entry(symbol_conf.sym_list,
						al->sym->name);
		}
		if (!ret && al->sym) {
			snprintf(al_addr_str, sz, "0x%"PRIx64,
				 map__unmap_ip(al->map, al->sym->start));
			ret = strlist__has_entry(symbol_conf.sym_list,
						al_addr_str);
		}
		if (!ret && symbol_conf.addr_list && al->map) {
			unsigned long addr = map__unmap_ip(al->map, al->addr);

			ret = intlist__has_entry(symbol_conf.addr_list, addr);
			if (!ret && symbol_conf.addr_range) {
				ret = check_address_range(symbol_conf.addr_list,
							  symbol_conf.addr_range,
							  addr);
			}
		}

		if (!ret)
			al->filtered |= (1 << HIST_FILTER__SYMBOL);
	}

	return 0;
}

bool is_bts_event(struct perf_event_attr *attr)
{
	return attr->type == PERF_TYPE_HARDWARE &&
	       (attr->config & PERF_COUNT_HW_BRANCH_INSTRUCTIONS) &&
	       attr->sample_period == 1;
}

bool sample_addr_correlates_sym(struct perf_event_attr *attr)
{
	if (attr->type == PERF_TYPE_SOFTWARE &&
	    (attr->config == PERF_COUNT_SW_PAGE_FAULTS ||
	     attr->config == PERF_COUNT_SW_PAGE_FAULTS_MIN ||
	     attr->config == PERF_COUNT_SW_PAGE_FAULTS_MAJ))
		return true;

	if (is_bts_event(attr))
		return true;

	return false;
}

void thread__resolve(struct thread *thread, struct addr_location *al,
		     struct perf_sample *sample)
{
	thread__find_map_fb(thread, sample->cpumode, sample->addr, al);

	al->cpu = sample->cpu;
	al->sym = NULL;

	if (al->map)
		al->sym = map__find_symbol(al->map, al->addr);
}
