#include <linux/types.h>
#include "event.h"
#include "debug.h"
#include "session.h"
#include "sort.h"
#include "string.h"
#include "strlist.h"
#include "thread.h"

const char *event__name[] = {
	[0]			 = "TOTAL",
	[PERF_RECORD_MMAP]	 = "MMAP",
	[PERF_RECORD_LOST]	 = "LOST",
	[PERF_RECORD_COMM]	 = "COMM",
	[PERF_RECORD_EXIT]	 = "EXIT",
	[PERF_RECORD_THROTTLE]	 = "THROTTLE",
	[PERF_RECORD_UNTHROTTLE] = "UNTHROTTLE",
	[PERF_RECORD_FORK]	 = "FORK",
	[PERF_RECORD_READ]	 = "READ",
	[PERF_RECORD_SAMPLE]	 = "SAMPLE",
	[PERF_RECORD_HEADER_ATTR]	 = "ATTR",
	[PERF_RECORD_HEADER_EVENT_TYPE]	 = "EVENT_TYPE",
	[PERF_RECORD_HEADER_TRACING_DATA]	 = "TRACING_DATA",
	[PERF_RECORD_HEADER_BUILD_ID]	 = "BUILD_ID",
};

static pid_t event__synthesize_comm(pid_t pid, int full,
				    event__handler_t process,
				    struct perf_session *session)
{
	event_t ev;
	char filename[PATH_MAX];
	char bf[BUFSIZ];
	FILE *fp;
	size_t size = 0;
	DIR *tasks;
	struct dirent dirent, *next;
	pid_t tgid = 0;

	snprintf(filename, sizeof(filename), "/proc/%d/status", pid);

	fp = fopen(filename, "r");
	if (fp == NULL) {
out_race:
		/*
		 * We raced with a task exiting - just return:
		 */
		pr_debug("couldn't open %s\n", filename);
		return 0;
	}

	memset(&ev.comm, 0, sizeof(ev.comm));
	while (!ev.comm.comm[0] || !ev.comm.pid) {
		if (fgets(bf, sizeof(bf), fp) == NULL)
			goto out_failure;

		if (memcmp(bf, "Name:", 5) == 0) {
			char *name = bf + 5;
			while (*name && isspace(*name))
				++name;
			size = strlen(name) - 1;
			memcpy(ev.comm.comm, name, size++);
		} else if (memcmp(bf, "Tgid:", 5) == 0) {
			char *tgids = bf + 5;
			while (*tgids && isspace(*tgids))
				++tgids;
			tgid = ev.comm.pid = atoi(tgids);
		}
	}

	ev.comm.header.type = PERF_RECORD_COMM;
	size = ALIGN(size, sizeof(u64));
	ev.comm.header.size = sizeof(ev.comm) - (sizeof(ev.comm.comm) - size);

	if (!full) {
		ev.comm.tid = pid;

		process(&ev, session);
		goto out_fclose;
	}

	snprintf(filename, sizeof(filename), "/proc/%d/task", pid);

	tasks = opendir(filename);
	if (tasks == NULL)
		goto out_race;

	while (!readdir_r(tasks, &dirent, &next) && next) {
		char *end;
		pid = strtol(dirent.d_name, &end, 10);
		if (*end)
			continue;

		ev.comm.tid = pid;

		process(&ev, session);
	}
	closedir(tasks);

out_fclose:
	fclose(fp);
	return tgid;

out_failure:
	pr_warning("couldn't get COMM and pgid, malformed %s\n", filename);
	return -1;
}

static int event__synthesize_mmap_events(pid_t pid, pid_t tgid,
					 event__handler_t process,
					 struct perf_session *session)
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

	while (1) {
		char bf[BUFSIZ], *pbf = bf;
		event_t ev = {
			.header = {
				.type = PERF_RECORD_MMAP,
				/*
				 * Just like the kernel, see __perf_event_mmap
				 * in kernel/perf_event.c
				 */
				.misc = PERF_RECORD_MISC_USER,
			 },
		};
		int n;
		size_t size;
		if (fgets(bf, sizeof(bf), fp) == NULL)
			break;

		/* 00400000-0040c000 r-xp 00000000 fd:01 41038  /bin/cat */
		n = hex2u64(pbf, &ev.mmap.start);
		if (n < 0)
			continue;
		pbf += n + 1;
		n = hex2u64(pbf, &ev.mmap.len);
		if (n < 0)
			continue;
		pbf += n + 3;
		if (*pbf == 'x') { /* vm_exec */
			char *execname = strchr(bf, '/');

			/* Catch VDSO */
			if (execname == NULL)
				execname = strstr(bf, "[vdso]");

			if (execname == NULL)
				continue;

			pbf += 3;
			n = hex2u64(pbf, &ev.mmap.pgoff);

			size = strlen(execname);
			execname[size - 1] = '\0'; /* Remove \n */
			memcpy(ev.mmap.filename, execname, size);
			size = ALIGN(size, sizeof(u64));
			ev.mmap.len -= ev.mmap.start;
			ev.mmap.header.size = (sizeof(ev.mmap) -
					       (sizeof(ev.mmap.filename) - size));
			ev.mmap.pid = tgid;
			ev.mmap.tid = pid;

			process(&ev, session);
		}
	}

	fclose(fp);
	return 0;
}

int event__synthesize_modules(event__handler_t process,
			      struct perf_session *session,
			      struct machine *machine)
{
	struct rb_node *nd;
	struct map_groups *kmaps = &machine->kmaps;
	u16 misc;

	/*
	 * kernel uses 0 for user space maps, see kernel/perf_event.c
	 * __perf_event_mmap
	 */
	if (machine__is_host(machine))
		misc = PERF_RECORD_MISC_KERNEL;
	else
		misc = PERF_RECORD_MISC_GUEST_KERNEL;

	for (nd = rb_first(&kmaps->maps[MAP__FUNCTION]);
	     nd; nd = rb_next(nd)) {
		event_t ev;
		size_t size;
		struct map *pos = rb_entry(nd, struct map, rb_node);

		if (pos->dso->kernel)
			continue;

		size = ALIGN(pos->dso->long_name_len + 1, sizeof(u64));
		memset(&ev, 0, sizeof(ev));
		ev.mmap.header.misc = misc;
		ev.mmap.header.type = PERF_RECORD_MMAP;
		ev.mmap.header.size = (sizeof(ev.mmap) -
				        (sizeof(ev.mmap.filename) - size));
		ev.mmap.start = pos->start;
		ev.mmap.len   = pos->end - pos->start;
		ev.mmap.pid   = machine->pid;

		memcpy(ev.mmap.filename, pos->dso->long_name,
		       pos->dso->long_name_len + 1);
		process(&ev, session);
	}

	return 0;
}

int event__synthesize_thread(pid_t pid, event__handler_t process,
			     struct perf_session *session)
{
	pid_t tgid = event__synthesize_comm(pid, 1, process, session);
	if (tgid == -1)
		return -1;
	return event__synthesize_mmap_events(pid, tgid, process, session);
}

void event__synthesize_threads(event__handler_t process,
			       struct perf_session *session)
{
	DIR *proc;
	struct dirent dirent, *next;

	proc = opendir("/proc");

	while (!readdir_r(proc, &dirent, &next) && next) {
		char *end;
		pid_t pid = strtol(dirent.d_name, &end, 10);

		if (*end) /* only interested in proper numerical dirents */
			continue;

		event__synthesize_thread(pid, process, session);
	}

	closedir(proc);
}

struct process_symbol_args {
	const char *name;
	u64	   start;
};

static int find_symbol_cb(void *arg, const char *name, char type, u64 start)
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

int event__synthesize_kernel_mmap(event__handler_t process,
				  struct perf_session *session,
				  struct machine *machine,
				  const char *symbol_name)
{
	size_t size;
	const char *filename, *mmap_name;
	char path[PATH_MAX];
	char name_buff[PATH_MAX];
	struct map *map;

	event_t ev = {
		.header = {
			.type = PERF_RECORD_MMAP,
		},
	};
	/*
	 * We should get this from /sys/kernel/sections/.text, but till that is
	 * available use this, and after it is use this as a fallback for older
	 * kernels.
	 */
	struct process_symbol_args args = { .name = symbol_name, };

	mmap_name = machine__mmap_name(machine, name_buff, sizeof(name_buff));
	if (machine__is_host(machine)) {
		/*
		 * kernel uses PERF_RECORD_MISC_USER for user space maps,
		 * see kernel/perf_event.c __perf_event_mmap
		 */
		ev.header.misc = PERF_RECORD_MISC_KERNEL;
		filename = "/proc/kallsyms";
	} else {
		ev.header.misc = PERF_RECORD_MISC_GUEST_KERNEL;
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
	size = snprintf(ev.mmap.filename, sizeof(ev.mmap.filename),
			"%s%s", mmap_name, symbol_name) + 1;
	size = ALIGN(size, sizeof(u64));
	ev.mmap.header.size = (sizeof(ev.mmap) -
			(sizeof(ev.mmap.filename) - size));
	ev.mmap.pgoff = args.start;
	ev.mmap.start = map->start;
	ev.mmap.len   = map->end - ev.mmap.start;
	ev.mmap.pid   = machine->pid;

	return process(&ev, session);
}

static void thread__comm_adjust(struct thread *self, struct hists *hists)
{
	char *comm = self->comm;

	if (!symbol_conf.col_width_list_str && !symbol_conf.field_sep &&
	    (!symbol_conf.comm_list ||
	     strlist__has_entry(symbol_conf.comm_list, comm))) {
		u16 slen = strlen(comm);

		if (hists__new_col_len(hists, HISTC_COMM, slen))
			hists__set_col_len(hists, HISTC_THREAD, slen + 6);
	}
}

static int thread__set_comm_adjust(struct thread *self, const char *comm,
				   struct hists *hists)
{
	int ret = thread__set_comm(self, comm);

	if (ret)
		return ret;

	thread__comm_adjust(self, hists);

	return 0;
}

int event__process_comm(event_t *self, struct perf_session *session)
{
	struct thread *thread = perf_session__findnew(session, self->comm.tid);

	dump_printf(": %s:%d\n", self->comm.comm, self->comm.tid);

	if (thread == NULL || thread__set_comm_adjust(thread, self->comm.comm,
						      &session->hists)) {
		dump_printf("problem processing PERF_RECORD_COMM, skipping event.\n");
		return -1;
	}

	return 0;
}

int event__process_lost(event_t *self, struct perf_session *session)
{
	dump_printf(": id:%Ld: lost:%Ld\n", self->lost.id, self->lost.lost);
	session->hists.stats.total_lost += self->lost.lost;
	return 0;
}

static void event_set_kernel_mmap_len(struct map **maps, event_t *self)
{
	maps[MAP__FUNCTION]->start = self->mmap.start;
	maps[MAP__FUNCTION]->end   = self->mmap.start + self->mmap.len;
	/*
	 * Be a bit paranoid here, some perf.data file came with
	 * a zero sized synthesized MMAP event for the kernel.
	 */
	if (maps[MAP__FUNCTION]->end == 0)
		maps[MAP__FUNCTION]->end = ~0UL;
}

static int event__process_kernel_mmap(event_t *self,
			struct perf_session *session)
{
	struct map *map;
	char kmmap_prefix[PATH_MAX];
	struct machine *machine;
	enum dso_kernel_type kernel_type;
	bool is_kernel_mmap;

	machine = perf_session__findnew_machine(session, self->mmap.pid);
	if (!machine) {
		pr_err("Can't find id %d's machine\n", self->mmap.pid);
		goto out_problem;
	}

	machine__mmap_name(machine, kmmap_prefix, sizeof(kmmap_prefix));
	if (machine__is_host(machine))
		kernel_type = DSO_TYPE_KERNEL;
	else
		kernel_type = DSO_TYPE_GUEST_KERNEL;

	is_kernel_mmap = memcmp(self->mmap.filename,
				kmmap_prefix,
				strlen(kmmap_prefix)) == 0;
	if (self->mmap.filename[0] == '/' ||
	    (!is_kernel_mmap && self->mmap.filename[0] == '[')) {

		char short_module_name[1024];
		char *name, *dot;

		if (self->mmap.filename[0] == '/') {
			name = strrchr(self->mmap.filename, '/');
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
			strcpy(short_module_name, self->mmap.filename);

		map = machine__new_module(machine, self->mmap.start,
					  self->mmap.filename);
		if (map == NULL)
			goto out_problem;

		name = strdup(short_module_name);
		if (name == NULL)
			goto out_problem;

		map->dso->short_name = name;
		map->dso->sname_alloc = 1;
		map->end = map->start + self->mmap.len;
	} else if (is_kernel_mmap) {
		const char *symbol_name = (self->mmap.filename +
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

		event_set_kernel_mmap_len(machine->vmlinux_maps, self);
		perf_session__set_kallsyms_ref_reloc_sym(machine->vmlinux_maps,
							 symbol_name,
							 self->mmap.pgoff);
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

int event__process_mmap(event_t *self, struct perf_session *session)
{
	struct machine *machine;
	struct thread *thread;
	struct map *map;
	u8 cpumode = self->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	int ret = 0;

	dump_printf(" %d/%d: [%#Lx(%#Lx) @ %#Lx]: %s\n",
			self->mmap.pid, self->mmap.tid, self->mmap.start,
			self->mmap.len, self->mmap.pgoff, self->mmap.filename);

	if (cpumode == PERF_RECORD_MISC_GUEST_KERNEL ||
	    cpumode == PERF_RECORD_MISC_KERNEL) {
		ret = event__process_kernel_mmap(self, session);
		if (ret < 0)
			goto out_problem;
		return 0;
	}

	machine = perf_session__find_host_machine(session);
	if (machine == NULL)
		goto out_problem;
	thread = perf_session__findnew(session, self->mmap.pid);
	if (thread == NULL)
		goto out_problem;
	map = map__new(&machine->user_dsos, self->mmap.start,
			self->mmap.len, self->mmap.pgoff,
			self->mmap.pid, self->mmap.filename,
			MAP__FUNCTION);
	if (map == NULL)
		goto out_problem;

	thread__insert_map(thread, map);
	return 0;

out_problem:
	dump_printf("problem processing PERF_RECORD_MMAP, skipping event.\n");
	return 0;
}

int event__process_task(event_t *self, struct perf_session *session)
{
	struct thread *thread = perf_session__findnew(session, self->fork.tid);
	struct thread *parent = perf_session__findnew(session, self->fork.ptid);

	dump_printf("(%d:%d):(%d:%d)\n", self->fork.pid, self->fork.tid,
		    self->fork.ppid, self->fork.ptid);

	if (self->header.type == PERF_RECORD_EXIT) {
		perf_session__remove_thread(session, thread);
		return 0;
	}

	if (thread == NULL || parent == NULL ||
	    thread__fork(thread, parent) < 0) {
		dump_printf("problem processing PERF_RECORD_FORK, skipping event.\n");
		return -1;
	}

	return 0;
}

int event__process(event_t *event, struct perf_session *session)
{
	switch (event->header.type) {
	case PERF_RECORD_COMM:
		event__process_comm(event, session);
		break;
	case PERF_RECORD_MMAP:
		event__process_mmap(event, session);
		break;
	case PERF_RECORD_FORK:
	case PERF_RECORD_EXIT:
		event__process_task(event, session);
		break;
	default:
		break;
	}

	return 0;
}

void thread__find_addr_map(struct thread *self,
			   struct perf_session *session, u8 cpumode,
			   enum map_type type, pid_t pid, u64 addr,
			   struct addr_location *al)
{
	struct map_groups *mg = &self->mg;
	struct machine *machine = NULL;

	al->thread = self;
	al->addr = addr;
	al->cpumode = cpumode;
	al->filtered = false;

	if (cpumode == PERF_RECORD_MISC_KERNEL && perf_host) {
		al->level = 'k';
		machine = perf_session__find_host_machine(session);
		if (machine == NULL) {
			al->map = NULL;
			return;
		}
		mg = &machine->kmaps;
	} else if (cpumode == PERF_RECORD_MISC_USER && perf_host) {
		al->level = '.';
		machine = perf_session__find_host_machine(session);
	} else if (cpumode == PERF_RECORD_MISC_GUEST_KERNEL && perf_guest) {
		al->level = 'g';
		machine = perf_session__find_machine(session, pid);
		if (machine == NULL) {
			al->map = NULL;
			return;
		}
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
		    cpumode == PERF_RECORD_MISC_KERNEL &&
		    machine && mg != &machine->kmaps) {
			mg = &machine->kmaps;
			goto try_again;
		}
	} else
		al->addr = al->map->map_ip(al->map, al->addr);
}

void thread__find_addr_location(struct thread *self,
				struct perf_session *session, u8 cpumode,
				enum map_type type, pid_t pid, u64 addr,
				struct addr_location *al,
				symbol_filter_t filter)
{
	thread__find_addr_map(self, session, cpumode, type, pid, addr, al);
	if (al->map != NULL)
		al->sym = map__find_symbol(al->map, al->addr, filter);
	else
		al->sym = NULL;
}

static void dso__calc_col_width(struct dso *self, struct hists *hists)
{
	if (!symbol_conf.col_width_list_str && !symbol_conf.field_sep &&
	    (!symbol_conf.dso_list ||
	     strlist__has_entry(symbol_conf.dso_list, self->name))) {
		u16 slen = dso__name_len(self);
		hists__new_col_len(hists, HISTC_DSO, slen);
	}

	self->slen_calculated = 1;
}

int event__preprocess_sample(const event_t *self, struct perf_session *session,
			     struct addr_location *al, struct sample_data *data,
			     symbol_filter_t filter)
{
	u8 cpumode = self->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	struct thread *thread;

	event__parse_sample(self, session->sample_type, data);

	dump_printf("(IP, %d): %d/%d: %#Lx period: %Ld cpu:%d\n",
		    self->header.misc, data->pid, data->tid, data->ip,
		    data->period, data->cpu);

	if (session->sample_type & PERF_SAMPLE_CALLCHAIN) {
		unsigned int i;

		dump_printf("... chain: nr:%Lu\n", data->callchain->nr);

		if (!ip_callchain__valid(data->callchain, self)) {
			pr_debug("call-chain problem with event, "
				 "skipping it.\n");
			goto out_filtered;
		}

		if (dump_trace) {
			for (i = 0; i < data->callchain->nr; i++)
				dump_printf("..... %2d: %016Lx\n",
					    i, data->callchain->ips[i]);
		}
	}
	thread = perf_session__findnew(session, self->ip.pid);
	if (thread == NULL)
		return -1;

	if (symbol_conf.comm_list &&
	    !strlist__has_entry(symbol_conf.comm_list, thread->comm))
		goto out_filtered;

	dump_printf(" ... thread: %s:%d\n", thread->comm, thread->pid);
	/*
	 * Have we already created the kernel maps for the host machine?
	 *
	 * This should have happened earlier, when we processed the kernel MMAP
	 * events, but for older perf.data files there was no such thing, so do
	 * it now.
	 */
	if (cpumode == PERF_RECORD_MISC_KERNEL &&
	    session->host_machine.vmlinux_maps[MAP__FUNCTION] == NULL)
		machine__create_kernel_maps(&session->host_machine);

	thread__find_addr_map(thread, session, cpumode, MAP__FUNCTION,
			      self->ip.pid, self->ip.ip, al);
	dump_printf(" ...... dso: %s\n",
		    al->map ? al->map->dso->long_name :
			al->level == 'H' ? "[hypervisor]" : "<not found>");
	al->sym = NULL;
	al->cpu = data->cpu;

	if (al->map) {
		if (symbol_conf.dso_list &&
		    (!al->map || !al->map->dso ||
		     !(strlist__has_entry(symbol_conf.dso_list,
					  al->map->dso->short_name) ||
		       (al->map->dso->short_name != al->map->dso->long_name &&
			strlist__has_entry(symbol_conf.dso_list,
					   al->map->dso->long_name)))))
			goto out_filtered;
		/*
		 * We have to do this here as we may have a dso with no symbol
		 * hit that has a name longer than the ones with symbols
		 * sampled.
		 */
		if (!sort_dso.elide && !al->map->dso->slen_calculated)
			dso__calc_col_width(al->map->dso, &session->hists);

		al->sym = map__find_symbol(al->map, al->addr, filter);
	} else {
		const unsigned int unresolved_col_width = BITS_PER_LONG / 4;

		if (hists__col_len(&session->hists, HISTC_DSO) < unresolved_col_width &&
		    !symbol_conf.col_width_list_str && !symbol_conf.field_sep &&
		    !symbol_conf.dso_list)
			hists__set_col_len(&session->hists, HISTC_DSO,
					   unresolved_col_width);
	}

	if (symbol_conf.sym_list && al->sym &&
	    !strlist__has_entry(symbol_conf.sym_list, al->sym->name))
		goto out_filtered;

	return 0;

out_filtered:
	al->filtered = true;
	return 0;
}

int event__parse_sample(const event_t *event, u64 type, struct sample_data *data)
{
	const u64 *array = event->sample.array;

	if (type & PERF_SAMPLE_IP) {
		data->ip = event->ip.ip;
		array++;
	}

	if (type & PERF_SAMPLE_TID) {
		u32 *p = (u32 *)array;
		data->pid = p[0];
		data->tid = p[1];
		array++;
	}

	if (type & PERF_SAMPLE_TIME) {
		data->time = *array;
		array++;
	}

	if (type & PERF_SAMPLE_ADDR) {
		data->addr = *array;
		array++;
	}

	data->id = -1ULL;
	if (type & PERF_SAMPLE_ID) {
		data->id = *array;
		array++;
	}

	if (type & PERF_SAMPLE_STREAM_ID) {
		data->stream_id = *array;
		array++;
	}

	if (type & PERF_SAMPLE_CPU) {
		u32 *p = (u32 *)array;
		data->cpu = *p;
		array++;
	} else
		data->cpu = -1;

	if (type & PERF_SAMPLE_PERIOD) {
		data->period = *array;
		array++;
	}

	if (type & PERF_SAMPLE_READ) {
		pr_debug("PERF_SAMPLE_READ is unsuported for now\n");
		return -1;
	}

	if (type & PERF_SAMPLE_CALLCHAIN) {
		data->callchain = (struct ip_callchain *)array;
		array += 1 + data->callchain->nr;
	}

	if (type & PERF_SAMPLE_RAW) {
		u32 *p = (u32 *)array;
		data->raw_size = *p;
		p++;
		data->raw_data = p;
	}

	return 0;
}
