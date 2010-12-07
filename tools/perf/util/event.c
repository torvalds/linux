#include <linux/types.h>
#include "event.h"
#include "debug.h"
#include "session.h"
#include "sort.h"
#include "string.h"
#include "strlist.h"
#include "thread.h"

static const char *event__name[] = {
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
	[PERF_RECORD_FINISHED_ROUND]	 = "FINISHED_ROUND",
};

const char *event__get_event_name(unsigned int id)
{
	if (id >= ARRAY_SIZE(event__name))
		return "INVALID";
	if (!event__name[id])
		return "UNKNOWN";
	return event__name[id];
}

static struct sample_data synth_sample = {
	.pid	   = -1,
	.tid	   = -1,
	.time	   = -1,
	.stream_id = -1,
	.cpu	   = -1,
	.period	   = 1,
};

static pid_t event__synthesize_comm(event_t *event, pid_t pid, int full,
				    event__handler_t process,
				    struct perf_session *session)
{
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

	memset(&event->comm, 0, sizeof(event->comm));

	while (!event->comm.comm[0] || !event->comm.pid) {
		if (fgets(bf, sizeof(bf), fp) == NULL) {
			pr_warning("couldn't get COMM and pgid, malformed %s\n", filename);
			goto out;
		}

		if (memcmp(bf, "Name:", 5) == 0) {
			char *name = bf + 5;
			while (*name && isspace(*name))
				++name;
			size = strlen(name) - 1;
			memcpy(event->comm.comm, name, size++);
		} else if (memcmp(bf, "Tgid:", 5) == 0) {
			char *tgids = bf + 5;
			while (*tgids && isspace(*tgids))
				++tgids;
			tgid = event->comm.pid = atoi(tgids);
		}
	}

	event->comm.header.type = PERF_RECORD_COMM;
	size = ALIGN(size, sizeof(u64));
	memset(event->comm.comm + size, 0, session->id_hdr_size);
	event->comm.header.size = (sizeof(event->comm) -
				(sizeof(event->comm.comm) - size) +
				session->id_hdr_size);
	if (!full) {
		event->comm.tid = pid;

		process(event, &synth_sample, session);
		goto out;
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

		event->comm.tid = pid;

		process(event, &synth_sample, session);
	}

	closedir(tasks);
out:
	fclose(fp);

	return tgid;
}

static int event__synthesize_mmap_events(event_t *event, pid_t pid, pid_t tgid,
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
			char *execname = strchr(bf, '/');

			/* Catch VDSO */
			if (execname == NULL)
				execname = strstr(bf, "[vdso]");

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
			memset(event->mmap.filename + size, 0, session->id_hdr_size);
			event->mmap.header.size += session->id_hdr_size;
			event->mmap.pid = tgid;
			event->mmap.tid = pid;

			process(event, &synth_sample, session);
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
	event_t *event = zalloc(sizeof(event->mmap) + session->id_hdr_size);

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
		memset(event->mmap.filename + size, 0, session->id_hdr_size);
		event->mmap.header.size += session->id_hdr_size;
		event->mmap.start = pos->start;
		event->mmap.len   = pos->end - pos->start;
		event->mmap.pid   = machine->pid;

		memcpy(event->mmap.filename, pos->dso->long_name,
		       pos->dso->long_name_len + 1);
		process(event, &synth_sample, session);
	}

	free(event);
	return 0;
}

static int __event__synthesize_thread(event_t *comm_event, event_t *mmap_event,
				      pid_t pid, event__handler_t process,
				      struct perf_session *session)
{
	pid_t tgid = event__synthesize_comm(comm_event, pid, 1, process,
					    session);
	if (tgid == -1)
		return -1;
	return event__synthesize_mmap_events(mmap_event, pid, tgid,
					     process, session);
}

int event__synthesize_thread(pid_t pid, event__handler_t process,
			     struct perf_session *session)
{
	event_t *comm_event, *mmap_event;
	int err = -1;

	comm_event = malloc(sizeof(comm_event->comm) + session->id_hdr_size);
	if (comm_event == NULL)
		goto out;

	mmap_event = malloc(sizeof(mmap_event->mmap) + session->id_hdr_size);
	if (mmap_event == NULL)
		goto out_free_comm;

	err = __event__synthesize_thread(comm_event, mmap_event, pid,
					 process, session);
	free(mmap_event);
out_free_comm:
	free(comm_event);
out:
	return err;
}

int event__synthesize_threads(event__handler_t process,
			      struct perf_session *session)
{
	DIR *proc;
	struct dirent dirent, *next;
	event_t *comm_event, *mmap_event;
	int err = -1;

	comm_event = malloc(sizeof(comm_event->comm) + session->id_hdr_size);
	if (comm_event == NULL)
		goto out;

	mmap_event = malloc(sizeof(mmap_event->mmap) + session->id_hdr_size);
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

		__event__synthesize_thread(comm_event, mmap_event, pid,
					   process, session);
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
	int err;
	/*
	 * We should get this from /sys/kernel/sections/.text, but till that is
	 * available use this, and after it is use this as a fallback for older
	 * kernels.
	 */
	struct process_symbol_args args = { .name = symbol_name, };
	event_t *event = zalloc(sizeof(event->mmap) + session->id_hdr_size);

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
			(sizeof(event->mmap.filename) - size) + session->id_hdr_size);
	event->mmap.pgoff = args.start;
	event->mmap.start = map->start;
	event->mmap.len   = map->end - event->mmap.start;
	event->mmap.pid   = machine->pid;

	err = process(event, &synth_sample, session);
	free(event);

	return err;
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

int event__process_comm(event_t *self, struct sample_data *sample __used,
			struct perf_session *session)
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

int event__process_lost(event_t *self, struct sample_data *sample __used,
			struct perf_session *session)
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
		maps[MAP__FUNCTION]->end = ~0ULL;
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

int event__process_mmap(event_t *self, struct sample_data *sample __used,
			struct perf_session *session)
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

int event__process_task(event_t *self, struct sample_data *sample __used,
			struct perf_session *session)
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

int event__process(event_t *event, struct sample_data *sample,
		   struct perf_session *session)
{
	switch (event->header.type) {
	case PERF_RECORD_COMM:
		event__process_comm(event, sample, session);
		break;
	case PERF_RECORD_MMAP:
		event__process_mmap(event, sample, session);
		break;
	case PERF_RECORD_FORK:
	case PERF_RECORD_EXIT:
		event__process_task(event, sample, session);
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
	struct thread *thread = perf_session__findnew(session, self->ip.pid);

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

static int event__parse_id_sample(const event_t *event,
				  struct perf_session *session,
				  struct sample_data *sample)
{
	const u64 *array;
	u64 type;

	sample->cpu = sample->pid = sample->tid = -1;
	sample->stream_id = sample->id = sample->time = -1ULL;

	if (!session->sample_id_all)
		return 0;

	array = event->sample.array;
	array += ((event->header.size -
		   sizeof(event->header)) / sizeof(u64)) - 1;
	type = session->sample_type;

	if (type & PERF_SAMPLE_CPU) {
		u32 *p = (u32 *)array;
		sample->cpu = *p;
		array--;
	}

	if (type & PERF_SAMPLE_STREAM_ID) {
		sample->stream_id = *array;
		array--;
	}

	if (type & PERF_SAMPLE_ID) {
		sample->id = *array;
		array--;
	}

	if (type & PERF_SAMPLE_TIME) {
		sample->time = *array;
		array--;
	}

	if (type & PERF_SAMPLE_TID) {
		u32 *p = (u32 *)array;
		sample->pid = p[0];
		sample->tid = p[1];
	}

	return 0;
}

int event__parse_sample(const event_t *event, struct perf_session *session,
			struct sample_data *data)
{
	const u64 *array;
	u64 type;

	if (event->header.type != PERF_RECORD_SAMPLE)
		return event__parse_id_sample(event, session, data);

	array = event->sample.array;
	type = session->sample_type;

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
