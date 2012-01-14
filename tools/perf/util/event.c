#include <linux/types.h>
#include "event.h"
#include "debug.h"
#include "sort.h"
#include "string.h"
#include "strlist.h"
#include "thread.h"
#include "thread_map.h"

static const char *perf_event__names[] = {
	[0]					= "TOTAL",
	[PERF_RECORD_MMAP]			= "MMAP",
	[PERF_RECORD_LOST]			= "LOST",
	[PERF_RECORD_COMM]			= "COMM",
	[PERF_RECORD_EXIT]			= "EXIT",
	[PERF_RECORD_THROTTLE]			= "THROTTLE",
	[PERF_RECORD_UNTHROTTLE]		= "UNTHROTTLE",
	[PERF_RECORD_FORK]			= "FORK",
	[PERF_RECORD_READ]			= "READ",
	[PERF_RECORD_SAMPLE]			= "SAMPLE",
	[PERF_RECORD_HEADER_ATTR]		= "ATTR",
	[PERF_RECORD_HEADER_EVENT_TYPE]		= "EVENT_TYPE",
	[PERF_RECORD_HEADER_TRACING_DATA]	= "TRACING_DATA",
	[PERF_RECORD_HEADER_BUILD_ID]		= "BUILD_ID",
	[PERF_RECORD_FINISHED_ROUND]		= "FINISHED_ROUND",
};

const char *perf_event__name(unsigned int id)
{
	if (id >= ARRAY_SIZE(perf_event__names))
		return "INVALID";
	if (!perf_event__names[id])
		return "UNKNOWN";
	return perf_event__names[id];
}

static struct perf_sample synth_sample = {
	.pid	   = -1,
	.tid	   = -1,
	.time	   = -1,
	.stream_id = -1,
	.cpu	   = -1,
	.period	   = 1,
};

static pid_t perf_event__get_comm_tgid(pid_t pid, char *comm, size_t len)
{
	char filename[PATH_MAX];
	char bf[BUFSIZ];
	FILE *fp;
	size_t size = 0;
	pid_t tgid = -1;

	snprintf(filename, sizeof(filename), "/proc/%d/status", pid);

	fp = fopen(filename, "r");
	if (fp == NULL) {
		pr_debug("couldn't open %s\n", filename);
		return 0;
	}

	while (!comm[0] || (tgid < 0)) {
		if (fgets(bf, sizeof(bf), fp) == NULL) {
			pr_warning("couldn't get COMM and pgid, malformed %s\n",
				   filename);
			break;
		}

		if (memcmp(bf, "Name:", 5) == 0) {
			char *name = bf + 5;
			while (*name && isspace(*name))
				++name;
			size = strlen(name) - 1;
			if (size >= len)
				size = len - 1;
			memcpy(comm, name, size);

		} else if (memcmp(bf, "Tgid:", 5) == 0) {
			char *tgids = bf + 5;
			while (*tgids && isspace(*tgids))
				++tgids;
			tgid = atoi(tgids);
		}
	}

	fclose(fp);

	return tgid;
}

static pid_t perf_event__synthesize_comm(struct perf_tool *tool,
					 union perf_event *event, pid_t pid,
					 int full,
					 perf_event__handler_t process,
					 struct machine *machine)
{
	char filename[PATH_MAX];
	size_t size;
	DIR *tasks;
	struct dirent dirent, *next;
	pid_t tgid;

	memset(&event->comm, 0, sizeof(event->comm));

	tgid = perf_event__get_comm_tgid(pid, event->comm.comm,
					 sizeof(event->comm.comm));
	if (tgid < 0)
		goto out;

	event->comm.pid = tgid;
	event->comm.header.type = PERF_RECORD_COMM;

	size = strlen(event->comm.comm) + 1;
	size = ALIGN(size, sizeof(u64));
	memset(event->comm.comm + size, 0, machine->id_hdr_size);
	event->comm.header.size = (sizeof(event->comm) -
				(sizeof(event->comm.comm) - size) +
				machine->id_hdr_size);
	if (!full) {
		event->comm.tid = pid;

		process(tool, event, &synth_sample, machine);
		goto out;
	}

	snprintf(filename, sizeof(filename), "/proc/%d/task", pid);

	tasks = opendir(filename);
	if (tasks == NULL) {
		pr_debug("couldn't open %s\n", filename);
		return 0;
	}

	while (!readdir_r(tasks, &dirent, &next) && next) {
		char *end;
		pid = strtol(dirent.d_name, &end, 10);
		if (*end)
			continue;

		/* already have tgid; jut want to update the comm */
		(void) perf_event__get_comm_tgid(pid, event->comm.comm,
					 sizeof(event->comm.comm));

		size = strlen(event->comm.comm) + 1;
		size = ALIGN(size, sizeof(u64));
		memset(event->comm.comm + size, 0, machine->id_hdr_size);
		event->comm.header.size = (sizeof(event->comm) -
					  (sizeof(event->comm.comm) - size) +
					  machine->id_hdr_size);

		event->comm.tid = pid;

		process(tool, event, &synth_sample, machine);
	}

	closedir(tasks);
out:
	return tgid;
}

static int perf_event__synthesize_mmap_events(struct perf_tool *tool,
					      union perf_event *event,
					      pid_t pid, pid_t tgid,
					      perf_event__handler_t process,
					      struct machine *machine)
{
	char filename[PATH_MAX];
	FILE *fp;

	snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);

	fp = fopen(filename, "r");
	if (fp == NULL) {
		/*
		 * We raced with a task exiting - just return:
		 */
		pr_debug("couldn't open %s\n", filename);
		return -1;
	}

	event->header.type = PERF_RECORD_MMAP;
	/*
	 * Just like the kernel, see __perf_event_mmap in kernel/perf_event.c
	 */
	event->header.misc = PERF_RECORD_MISC_USER;

	while (1) {
		char bf[BUFSIZ], *pbf = bf;
		int n;
		size_t size;
		if (fgets(bf, sizeof(bf), fp) == NULL)
			break;

		/* 00400000-0040c000 r-xp 00000000 fd:01 41038  /bin/cat */
		n = hex2u64(pbf, &event->mmap.start);
		if (n < 0)
			continue;
		pbf += n + 1;
		n = hex2u64(pbf, &event->mmap.len);
		if (n < 0)
			continue;
		pbf += n + 3;
		if (*pbf == 'x') { /* vm_exec */
			char anonstr[] = "//anon\n";
			char *execname = strchr(bf, '/');

			/* Catch VDSO */
			if (execname == NULL)
				execname = strstr(bf, "[vdso]");

			/* Catch anonymous mmaps */
			if ((execname == NULL) && !strstr(bf, "["))
				execname = anonstr;

			if (execname == NULL)
				continue;

			pbf += 3;
			n = hex2u64(pbf, &event->mmap.pgoff);

			size = strlen(execname);
			execname[size - 1] = '\0'; /* Remove \n */
			memcpy(event->mmap.filename, execname, size);
			size = ALIGN(size, sizeof(u64));
			event->mmap.len -= event->mmap.start;
			event->mmap.header.size = (sizeof(event->mmap) -
					        (sizeof(event->mmap.filename) - size));
			memset(event->mmap.filename + size, 0, machine->id_hdr_size);
			event->mmap.header.size += machine->id_hdr_size;
			event->mmap.pid = tgid;
			event->mmap.tid = pid;

			process(tool, event, &synth_sample, machine);
		}
	}

	fclose(fp);
	return 0;
}

int perf_event__synthesize_modules(struct perf_tool *tool,
				   perf_event__handler_t process,
				   struct machine *machine)
{
	struct rb_node *nd;
	struct map_groups *kmaps = &machine->kmaps;
	union perf_event *event = zalloc((sizeof(event->mmap) +
					  machine->id_hdr_size));
	if (event == NULL) {
		pr_debug("Not enough memory synthesizing mmap event "
			 "for kernel modules\n");
		return -1;
	}

	event->header.type = PERF_RECORD_MMAP;

	/*
	 * kernel uses 0 for user space maps, see kernel/perf_event.c
	 * __perf_event_mmap
	 */
	if (machine__is_host(machine))
		event->header.misc = PERF_RECORD_MISC_KERNEL;
	else
		event->header.misc = PERF_RECORD_MISC_GUEST_KERNEL;

	for (nd = rb_first(&kmaps->maps[MAP__FUNCTION]);
	     nd; nd = rb_next(nd)) {
		size_t size;
		struct map *pos = rb_entry(nd, struct map, rb_node);

		if (pos->dso->kernel)
			continue;

		size = ALIGN(pos->dso->long_name_len + 1, sizeof(u64));
		event->mmap.header.type = PERF_RECORD_MMAP;
		event->mmap.header.size = (sizeof(event->mmap) -
				        (sizeof(event->mmap.filename) - size));
		memset(event->mmap.filename + size, 0, machine->id_hdr_size);
		event->mmap.header.size += machine->id_hdr_size;
		event->mmap.start = pos->start;
		event->mmap.len   = pos->end - pos->start;
		event->mmap.pid   = machine->pid;

		memcpy(event->mmap.filename, pos->dso->long_name,
		       pos->dso->long_name_len + 1);
		process(tool, event, &synth_sample, machine);
	}

	free(event);
	return 0;
}

static int __event__synthesize_thread(union perf_event *comm_event,
				      union perf_event *mmap_event,
				      pid_t pid, int full,
					  perf_event__handler_t process,
				      struct perf_tool *tool,
				      struct machine *machine)
{
	pid_t tgid = perf_event__synthesize_comm(tool, comm_event, pid, full,
						 process, machine);
	if (tgid == -1)
		return -1;
	return perf_event__synthesize_mmap_events(tool, mmap_event, pid, tgid,
						  process, machine);
}

int perf_event__synthesize_thread_map(struct perf_tool *tool,
				      struct thread_map *threads,
				      perf_event__handler_t process,
				      struct machine *machine)
{
	union perf_event *comm_event, *mmap_event;
	int err = -1, thread, j;

	comm_event = malloc(sizeof(comm_event->comm) + machine->id_hdr_size);
	if (comm_event == NULL)
		goto out;

	mmap_event = malloc(sizeof(mmap_event->mmap) + machine->id_hdr_size);
	if (mmap_event == NULL)
		goto out_free_comm;

	err = 0;
	for (thread = 0; thread < threads->nr; ++thread) {
		if (__event__synthesize_thread(comm_event, mmap_event,
					       threads->map[thread], 0,
					       process, tool, machine)) {
			err = -1;
			break;
		}

		/*
		 * comm.pid is set to thread group id by
		 * perf_event__synthesize_comm
		 */
		if ((int) comm_event->comm.pid != threads->map[thread]) {
			bool need_leader = true;

			/* is thread group leader in thread_map? */
			for (j = 0; j < threads->nr; ++j) {
				if ((int) comm_event->comm.pid == threads->map[j]) {
					need_leader = false;
					break;
				}
			}

			/* if not, generate events for it */
			if (need_leader &&
			    __event__synthesize_thread(comm_event,
						      mmap_event,
						      comm_event->comm.pid, 0,
						      process, tool, machine)) {
				err = -1;
				break;
			}
		}
	}
	free(mmap_event);
out_free_comm:
	free(comm_event);
out:
	return err;
}

int perf_event__synthesize_threads(struct perf_tool *tool,
				   perf_event__handler_t process,
				   struct machine *machine)
{
	DIR *proc;
	struct dirent dirent, *next;
	union perf_event *comm_event, *mmap_event;
	int err = -1;

	comm_event = malloc(sizeof(comm_event->comm) + machine->id_hdr_size);
	if (comm_event == NULL)
		goto out;

	mmap_event = malloc(sizeof(mmap_event->mmap) + machine->id_hdr_size);
	if (mmap_event == NULL)
		goto out_free_comm;

	proc = opendir("/proc");
	if (proc == NULL)
		goto out_free_mmap;

	while (!readdir_r(proc, &dirent, &next) && next) {
		char *end;
		pid_t pid = strtol(dirent.d_name, &end, 10);

		if (*end) /* only interested in proper numerical dirents */
			continue;

		__event__synthesize_thread(comm_event, mmap_event, pid, 1,
					   process, tool, machine);
	}

	closedir(proc);
	err = 0;
out_free_mmap:
	free(mmap_event);
out_free_comm:
	free(comm_event);
out:
	return err;
}

struct process_symbol_args {
	const char *name;
	u64	   start;
};

static int find_symbol_cb(void *arg, const char *name, char type,
			  u64 start, u64 end __used)
{
	struct process_symbol_args *args = arg;

	/*
	 * Must be a function or at least an alias, as in PARISC64, where "_text" is
	 * an 'A' to the same address as "_stext".
	 */
	if (!(symbol_type__is_a(type, MAP__FUNCTION) ||
	      type == 'A') || strcmp(name, args->name))
		return 0;

	args->start = start;
	return 1;
}

int perf_event__synthesize_kernel_mmap(struct perf_tool *tool,
				       perf_event__handler_t process,
				       struct machine *machine,
				       const char *symbol_name)
{
	size_t size;
	const char *filename, *mmap_name;
	char path[PATH_MAX];
	char name_buff[PATH_MAX];
	struct map *map;
	int err;
	/*
	 * We should get this from /sys/kernel/sections/.text, but till that is
	 * available use this, and after it is use this as a fallback for older
	 * kernels.
	 */
	struct process_symbol_args args = { .name = symbol_name, };
	union perf_event *event = zalloc((sizeof(event->mmap) +
					  machine->id_hdr_size));
	if (event == NULL) {
		pr_debug("Not enough memory synthesizing mmap event "
			 "for kernel modules\n");
		return -1;
	}

	mmap_name = machine__mmap_name(machine, name_buff, sizeof(name_buff));
	if (machine__is_host(machine)) {
		/*
		 * kernel uses PERF_RECORD_MISC_USER for user space maps,
		 * see kernel/perf_event.c __perf_event_mmap
		 */
		event->header.misc = PERF_RECORD_MISC_KERNEL;
		filename = "/proc/kallsyms";
	} else {
		event->header.misc = PERF_RECORD_MISC_GUEST_KERNEL;
		if (machine__is_default_guest(machine))
			filename = (char *) symbol_conf.default_guest_kallsyms;
		else {
			sprintf(path, "%s/proc/kallsyms", machine->root_dir);
			filename = path;
		}
	}

	if (kallsyms__parse(filename, &args, find_symbol_cb) <= 0)
		return -ENOENT;

	map = machine->vmlinux_maps[MAP__FUNCTION];
	size = snprintf(event->mmap.filename, sizeof(event->mmap.filename),
			"%s%s", mmap_name, symbol_name) + 1;
	size = ALIGN(size, sizeof(u64));
	event->mmap.header.type = PERF_RECORD_MMAP;
	event->mmap.header.size = (sizeof(event->mmap) -
			(sizeof(event->mmap.filename) - size) + machine->id_hdr_size);
	event->mmap.pgoff = args.start;
	event->mmap.start = map->start;
	event->mmap.len   = map->end - event->mmap.start;
	event->mmap.pid   = machine->pid;

	err = process(tool, event, &synth_sample, machine);
	free(event);

	return err;
}

size_t perf_event__fprintf_comm(union perf_event *event, FILE *fp)
{
	return fprintf(fp, ": %s:%d\n", event->comm.comm, event->comm.tid);
}

int perf_event__process_comm(struct perf_tool *tool __used,
			     union perf_event *event,
			     struct perf_sample *sample __used,
			     struct machine *machine)
{
	struct thread *thread = machine__findnew_thread(machine, event->comm.tid);

	if (dump_trace)
		perf_event__fprintf_comm(event, stdout);

	if (thread == NULL || thread__set_comm(thread, event->comm.comm)) {
		dump_printf("problem processing PERF_RECORD_COMM, skipping event.\n");
		return -1;
	}

	return 0;
}

int perf_event__process_lost(struct perf_tool *tool __used,
			     union perf_event *event,
			     struct perf_sample *sample __used,
			     struct machine *machine __used)
{
	dump_printf(": id:%" PRIu64 ": lost:%" PRIu64 "\n",
		    event->lost.id, event->lost.lost);
	return 0;
}

static void perf_event__set_kernel_mmap_len(union perf_event *event,
					    struct map **maps)
{
	maps[MAP__FUNCTION]->start = event->mmap.start;
	maps[MAP__FUNCTION]->end   = event->mmap.start + event->mmap.len;
	/*
	 * Be a bit paranoid here, some perf.data file came with
	 * a zero sized synthesized MMAP event for the kernel.
	 */
	if (maps[MAP__FUNCTION]->end == 0)
		maps[MAP__FUNCTION]->end = ~0ULL;
}

static int perf_event__process_kernel_mmap(struct perf_tool *tool __used,
					   union perf_event *event,
					   struct machine *machine)
{
	struct map *map;
	char kmmap_prefix[PATH_MAX];
	enum dso_kernel_type kernel_type;
	bool is_kernel_mmap;

	machine__mmap_name(machine, kmmap_prefix, sizeof(kmmap_prefix));
	if (machine__is_host(machine))
		kernel_type = DSO_TYPE_KERNEL;
	else
		kernel_type = DSO_TYPE_GUEST_KERNEL;

	is_kernel_mmap = memcmp(event->mmap.filename,
				kmmap_prefix,
				strlen(kmmap_prefix)) == 0;
	if (event->mmap.filename[0] == '/' ||
	    (!is_kernel_mmap && event->mmap.filename[0] == '[')) {

		char short_module_name[1024];
		char *name, *dot;

		if (event->mmap.filename[0] == '/') {
			name = strrchr(event->mmap.filename, '/');
			if (name == NULL)
				goto out_problem;

			++name; /* skip / */
			dot = strrchr(name, '.');
			if (dot == NULL)
				goto out_problem;
			snprintf(short_module_name, sizeof(short_module_name),
					"[%.*s]", (int)(dot - name), name);
			strxfrchar(short_module_name, '-', '_');
		} else
			strcpy(short_module_name, event->mmap.filename);

		map = machine__new_module(machine, event->mmap.start,
					  event->mmap.filename);
		if (map == NULL)
			goto out_problem;

		name = strdup(short_module_name);
		if (name == NULL)
			goto out_problem;

		map->dso->short_name = name;
		map->dso->sname_alloc = 1;
		map->end = map->start + event->mmap.len;
	} else if (is_kernel_mmap) {
		const char *symbol_name = (event->mmap.filename +
				strlen(kmmap_prefix));
		/*
		 * Should be there already, from the build-id table in
		 * the header.
		 */
		struct dso *kernel = __dsos__findnew(&machine->kernel_dsos,
						     kmmap_prefix);
		if (kernel == NULL)
			goto out_problem;

		kernel->kernel = kernel_type;
		if (__machine__create_kernel_maps(machine, kernel) < 0)
			goto out_problem;

		perf_event__set_kernel_mmap_len(event, machine->vmlinux_maps);

		/*
		 * Avoid using a zero address (kptr_restrict) for the ref reloc
		 * symbol. Effectively having zero here means that at record
		 * time /proc/sys/kernel/kptr_restrict was non zero.
		 */
		if (event->mmap.pgoff != 0) {
			maps__set_kallsyms_ref_reloc_sym(machine->vmlinux_maps,
							 symbol_name,
							 event->mmap.pgoff);
		}

		if (machine__is_default_guest(machine)) {
			/*
			 * preload dso of guest kernel and modules
			 */
			dso__load(kernel, machine->vmlinux_maps[MAP__FUNCTION],
				  NULL);
		}
	}
	return 0;
out_problem:
	return -1;
}

size_t perf_event__fprintf_mmap(union perf_event *event, FILE *fp)
{
	return fprintf(fp, " %d/%d: [%#" PRIx64 "(%#" PRIx64 ") @ %#" PRIx64 "]: %s\n",
		       event->mmap.pid, event->mmap.tid, event->mmap.start,
		       event->mmap.len, event->mmap.pgoff, event->mmap.filename);
}

int perf_event__process_mmap(struct perf_tool *tool,
			     union perf_event *event,
			     struct perf_sample *sample __used,
			     struct machine *machine)
{
	struct thread *thread;
	struct map *map;
	u8 cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	int ret = 0;

	if (dump_trace)
		perf_event__fprintf_mmap(event, stdout);

	if (cpumode == PERF_RECORD_MISC_GUEST_KERNEL ||
	    cpumode == PERF_RECORD_MISC_KERNEL) {
		ret = perf_event__process_kernel_mmap(tool, event, machine);
		if (ret < 0)
			goto out_problem;
		return 0;
	}

	thread = machine__findnew_thread(machine, event->mmap.pid);
	if (thread == NULL)
		goto out_problem;
	map = map__new(&machine->user_dsos, event->mmap.start,
			event->mmap.len, event->mmap.pgoff,
			event->mmap.pid, event->mmap.filename,
			MAP__FUNCTION);
	if (map == NULL)
		goto out_problem;

	thread__insert_map(thread, map);
	return 0;

out_problem:
	dump_printf("problem processing PERF_RECORD_MMAP, skipping event.\n");
	return 0;
}

size_t perf_event__fprintf_task(union perf_event *event, FILE *fp)
{
	return fprintf(fp, "(%d:%d):(%d:%d)\n",
		       event->fork.pid, event->fork.tid,
		       event->fork.ppid, event->fork.ptid);
}

int perf_event__process_task(struct perf_tool *tool __used,
			     union perf_event *event,
			     struct perf_sample *sample __used,
			      struct machine *machine)
{
	struct thread *thread = machine__findnew_thread(machine, event->fork.tid);
	struct thread *parent = machine__findnew_thread(machine, event->fork.ptid);

	if (dump_trace)
		perf_event__fprintf_task(event, stdout);

	if (event->header.type == PERF_RECORD_EXIT) {
		machine__remove_thread(machine, thread);
		return 0;
	}

	if (thread == NULL || parent == NULL ||
	    thread__fork(thread, parent) < 0) {
		dump_printf("problem processing PERF_RECORD_FORK, skipping event.\n");
		return -1;
	}

	return 0;
}

size_t perf_event__fprintf(union perf_event *event, FILE *fp)
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
	default:
		ret += fprintf(fp, "\n");
	}

	return ret;
}

int perf_event__process(struct perf_tool *tool, union perf_event *event,
			struct perf_sample *sample, struct machine *machine)
{
	switch (event->header.type) {
	case PERF_RECORD_COMM:
		perf_event__process_comm(tool, event, sample, machine);
		break;
	case PERF_RECORD_MMAP:
		perf_event__process_mmap(tool, event, sample, machine);
		break;
	case PERF_RECORD_FORK:
	case PERF_RECORD_EXIT:
		perf_event__process_task(tool, event, sample, machine);
		break;
	case PERF_RECORD_LOST:
		perf_event__process_lost(tool, event, sample, machine);
	default:
		break;
	}

	return 0;
}

void thread__find_addr_map(struct thread *self,
			   struct machine *machine, u8 cpumode,
			   enum map_type type, u64 addr,
			   struct addr_location *al)
{
	struct map_groups *mg = &self->mg;

	al->thread = self;
	al->addr = addr;
	al->cpumode = cpumode;
	al->filtered = false;

	if (machine == NULL) {
		al->map = NULL;
		return;
	}

	if (cpumode == PERF_RECORD_MISC_KERNEL && perf_host) {
		al->level = 'k';
		mg = &machine->kmaps;
	} else if (cpumode == PERF_RECORD_MISC_USER && perf_host) {
		al->level = '.';
	} else if (cpumode == PERF_RECORD_MISC_GUEST_KERNEL && perf_guest) {
		al->level = 'g';
		mg = &machine->kmaps;
	} else {
		/*
		 * 'u' means guest os user space.
		 * TODO: We don't support guest user space. Might support late.
		 */
		if (cpumode == PERF_RECORD_MISC_GUEST_USER && perf_guest)
			al->level = 'u';
		else
			al->level = 'H';
		al->map = NULL;

		if ((cpumode == PERF_RECORD_MISC_GUEST_USER ||
			cpumode == PERF_RECORD_MISC_GUEST_KERNEL) &&
			!perf_guest)
			al->filtered = true;
		if ((cpumode == PERF_RECORD_MISC_USER ||
			cpumode == PERF_RECORD_MISC_KERNEL) &&
			!perf_host)
			al->filtered = true;

		return;
	}
try_again:
	al->map = map_groups__find(mg, type, al->addr);
	if (al->map == NULL) {
		/*
		 * If this is outside of all known maps, and is a negative
		 * address, try to look it up in the kernel dso, as it might be
		 * a vsyscall or vdso (which executes in user-mode).
		 *
		 * XXX This is nasty, we should have a symbol list in the
		 * "[vdso]" dso, but for now lets use the old trick of looking
		 * in the whole kernel symbol list.
		 */
		if ((long long)al->addr < 0 &&
		    cpumode == PERF_RECORD_MISC_USER &&
		    machine && mg != &machine->kmaps) {
			mg = &machine->kmaps;
			goto try_again;
		}
	} else
		al->addr = al->map->map_ip(al->map, al->addr);
}

void thread__find_addr_location(struct thread *thread, struct machine *machine,
				u8 cpumode, enum map_type type, u64 addr,
				struct addr_location *al,
				symbol_filter_t filter)
{
	thread__find_addr_map(thread, machine, cpumode, type, addr, al);
	if (al->map != NULL)
		al->sym = map__find_symbol(al->map, al->addr, filter);
	else
		al->sym = NULL;
}

int perf_event__preprocess_sample(const union perf_event *event,
				  struct machine *machine,
				  struct addr_location *al,
				  struct perf_sample *sample,
				  symbol_filter_t filter)
{
	u8 cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	struct thread *thread = machine__findnew_thread(machine, event->ip.pid);

	if (thread == NULL)
		return -1;

	if (symbol_conf.comm_list &&
	    !strlist__has_entry(symbol_conf.comm_list, thread->comm))
		goto out_filtered;

	dump_printf(" ... thread: %s:%d\n", thread->comm, thread->pid);
	/*
	 * Have we already created the kernel maps for this machine?
	 *
	 * This should have happened earlier, when we processed the kernel MMAP
	 * events, but for older perf.data files there was no such thing, so do
	 * it now.
	 */
	if (cpumode == PERF_RECORD_MISC_KERNEL &&
	    machine->vmlinux_maps[MAP__FUNCTION] == NULL)
		machine__create_kernel_maps(machine);

	thread__find_addr_map(thread, machine, cpumode, MAP__FUNCTION,
			      event->ip.ip, al);
	dump_printf(" ...... dso: %s\n",
		    al->map ? al->map->dso->long_name :
			al->level == 'H' ? "[hypervisor]" : "<not found>");
	al->sym = NULL;
	al->cpu = sample->cpu;

	if (al->map) {
		struct dso *dso = al->map->dso;

		if (symbol_conf.dso_list &&
		    (!dso || !(strlist__has_entry(symbol_conf.dso_list,
						  dso->short_name) ||
			       (dso->short_name != dso->long_name &&
				strlist__has_entry(symbol_conf.dso_list,
						   dso->long_name)))))
			goto out_filtered;

		al->sym = map__find_symbol(al->map, al->addr, filter);
	}

	if (symbol_conf.sym_list && al->sym &&
	    !strlist__has_entry(symbol_conf.sym_list, al->sym->name))
		goto out_filtered;

	return 0;

out_filtered:
	al->filtered = true;
	return 0;
}
