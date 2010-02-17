#include <linux/types.h>
#include "event.h"
#include "debug.h"
#include "session.h"
#include "sort.h"
#include "string.h"
#include "strlist.h"
#include "thread.h"

static pid_t event__synthesize_comm(pid_t pid, int full,
				    int (*process)(event_t *event,
						   struct perf_session *session),
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
					 int (*process)(event_t *event,
							struct perf_session *session),
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
			.header = { .type = PERF_RECORD_MMAP },
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

int event__synthesize_thread(pid_t pid,
			     int (*process)(event_t *event,
					    struct perf_session *session),
			     struct perf_session *session)
{
	pid_t tgid = event__synthesize_comm(pid, 1, process, session);
	if (tgid == -1)
		return -1;
	return event__synthesize_mmap_events(pid, tgid, process, session);
}

void event__synthesize_threads(int (*process)(event_t *event,
					      struct perf_session *session),
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

static void thread__comm_adjust(struct thread *self)
{
	char *comm = self->comm;

	if (!symbol_conf.col_width_list_str && !symbol_conf.field_sep &&
	    (!symbol_conf.comm_list ||
	     strlist__has_entry(symbol_conf.comm_list, comm))) {
		unsigned int slen = strlen(comm);

		if (slen > comms__col_width) {
			comms__col_width = slen;
			threads__col_width = slen + 6;
		}
	}
}

static int thread__set_comm_adjust(struct thread *self, const char *comm)
{
	int ret = thread__set_comm(self, comm);

	if (ret)
		return ret;

	thread__comm_adjust(self);

	return 0;
}

int event__process_comm(event_t *self, struct perf_session *session)
{
	struct thread *thread = perf_session__findnew(session, self->comm.pid);

	dump_printf(": %s:%d\n", self->comm.comm, self->comm.pid);

	if (thread == NULL || thread__set_comm_adjust(thread, self->comm.comm)) {
		dump_printf("problem processing PERF_RECORD_COMM, skipping event.\n");
		return -1;
	}

	return 0;
}

int event__process_lost(event_t *self, struct perf_session *session)
{
	dump_printf(": id:%Ld: lost:%Ld\n", self->lost.id, self->lost.lost);
	session->events_stats.lost += self->lost.lost;
	return 0;
}

int event__process_mmap(event_t *self, struct perf_session *session)
{
	struct thread *thread = perf_session__findnew(session, self->mmap.pid);
	struct map *map = map__new(&self->mmap, MAP__FUNCTION,
				   session->cwd, session->cwdlen);

	dump_printf(" %d/%d: [%p(%p) @ %p]: %s\n",
		    self->mmap.pid, self->mmap.tid,
		    (void *)(long)self->mmap.start,
		    (void *)(long)self->mmap.len,
		    (void *)(long)self->mmap.pgoff,
		    self->mmap.filename);

	if (thread == NULL || map == NULL)
		dump_printf("problem processing PERF_RECORD_MMAP, skipping event.\n");
	else
		thread__insert_map(thread, map);

	return 0;
}

int event__process_task(event_t *self, struct perf_session *session)
{
	struct thread *thread = perf_session__findnew(session, self->fork.pid);
	struct thread *parent = perf_session__findnew(session, self->fork.ppid);

	dump_printf("(%d:%d):(%d:%d)\n", self->fork.pid, self->fork.tid,
		    self->fork.ppid, self->fork.ptid);
	/*
	 * A thread clone will have the same PID for both parent and child.
	 */
	if (thread == parent)
		return 0;

	if (self->header.type == PERF_RECORD_EXIT)
		return 0;

	if (thread == NULL || parent == NULL ||
	    thread__fork(thread, parent) < 0) {
		dump_printf("problem processing PERF_RECORD_FORK, skipping event.\n");
		return -1;
	}

	return 0;
}

void thread__find_addr_location(struct thread *self,
				struct perf_session *session, u8 cpumode,
				enum map_type type, u64 addr,
				struct addr_location *al,
				symbol_filter_t filter)
{
	struct map_groups *mg = &self->mg;

	al->thread = self;
	al->addr = addr;

	if (cpumode == PERF_RECORD_MISC_KERNEL) {
		al->level = 'k';
		mg = &session->kmaps;
	} else if (cpumode == PERF_RECORD_MISC_USER)
		al->level = '.';
	else {
		al->level = 'H';
		al->map = NULL;
		al->sym = NULL;
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
		if ((long long)al->addr < 0 && mg != &session->kmaps) {
			mg = &session->kmaps;
			goto try_again;
		}
		al->sym = NULL;
	} else {
		al->addr = al->map->map_ip(al->map, al->addr);
		al->sym = map__find_symbol(al->map, session, al->addr, filter);
	}
}

static void dso__calc_col_width(struct dso *self)
{
	if (!symbol_conf.col_width_list_str && !symbol_conf.field_sep &&
	    (!symbol_conf.dso_list ||
	     strlist__has_entry(symbol_conf.dso_list, self->name))) {
		unsigned int slen = strlen(self->name);
		if (slen > dsos__col_width)
			dsos__col_width = slen;
	}

	self->slen_calculated = 1;
}

int event__preprocess_sample(const event_t *self, struct perf_session *session,
			     struct addr_location *al, symbol_filter_t filter)
{
	u8 cpumode = self->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	struct thread *thread = perf_session__findnew(session, self->ip.pid);

	if (thread == NULL)
		return -1;

	if (symbol_conf.comm_list &&
	    !strlist__has_entry(symbol_conf.comm_list, thread->comm))
		goto out_filtered;

	dump_printf(" ... thread: %s:%d\n", thread->comm, thread->pid);

	thread__find_addr_location(thread, session, cpumode, MAP__FUNCTION,
				   self->ip.ip, al, filter);
	dump_printf(" ...... dso: %s\n",
		    al->map ? al->map->dso->long_name :
			al->level == 'H' ? "[hypervisor]" : "<not found>");
	/*
	 * We have to do this here as we may have a dso with no symbol hit that
	 * has a name longer than the ones with symbols sampled.
	 */
	if (al->map && !sort_dso.elide && !al->map->dso->slen_calculated)
		dso__calc_col_width(al->map->dso);

	if (symbol_conf.dso_list &&
	    (!al->map || !al->map->dso ||
	     !(strlist__has_entry(symbol_conf.dso_list, al->map->dso->short_name) ||
	       (al->map->dso->short_name != al->map->dso->long_name &&
		strlist__has_entry(symbol_conf.dso_list, al->map->dso->long_name)))))
		goto out_filtered;

	if (symbol_conf.sym_list && al->sym &&
	    !strlist__has_entry(symbol_conf.sym_list, al->sym->name))
		goto out_filtered;

	al->filtered = false;
	return 0;

out_filtered:
	al->filtered = true;
	return 0;
}

int event__parse_sample(event_t *event, u64 type, struct sample_data *data)
{
	u64 *array = event->sample.array;

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
	}

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
