/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-{top,stat,record}.c, see those files for further
 * copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */
#include "util.h"
#include "debugfs.h"
#include <poll.h>
#include "cpumap.h"
#include "thread_map.h"
#include "evlist.h"
#include "evsel.h"
#include <unistd.h>

#include "parse-events.h"

#include <sys/mman.h>

#include <linux/bitops.h>
#include <linux/hash.h>

#define FD(e, x, y) (*(int *)xyarray__entry(e->fd, x, y))
#define SID(e, x, y) xyarray__entry(e->sample_id, x, y)

void perf_evlist__init(struct perf_evlist *evlist, struct cpu_map *cpus,
		       struct thread_map *threads)
{
	int i;

	for (i = 0; i < PERF_EVLIST__HLIST_SIZE; ++i)
		INIT_HLIST_HEAD(&evlist->heads[i]);
	INIT_LIST_HEAD(&evlist->entries);
	perf_evlist__set_maps(evlist, cpus, threads);
	evlist->workload.pid = -1;
}

struct perf_evlist *perf_evlist__new(struct cpu_map *cpus,
				     struct thread_map *threads)
{
	struct perf_evlist *evlist = zalloc(sizeof(*evlist));

	if (evlist != NULL)
		perf_evlist__init(evlist, cpus, threads);

	return evlist;
}

void perf_evlist__config_attrs(struct perf_evlist *evlist,
			       struct perf_record_opts *opts)
{
	struct perf_evsel *evsel;

	if (evlist->cpus->map[0] < 0)
		opts->no_inherit = true;

	list_for_each_entry(evsel, &evlist->entries, node) {
		perf_evsel__config(evsel, opts);

		if (evlist->nr_entries > 1)
			evsel->attr.sample_type |= PERF_SAMPLE_ID;
	}
}

static void perf_evlist__purge(struct perf_evlist *evlist)
{
	struct perf_evsel *pos, *n;

	list_for_each_entry_safe(pos, n, &evlist->entries, node) {
		list_del_init(&pos->node);
		perf_evsel__delete(pos);
	}

	evlist->nr_entries = 0;
}

void perf_evlist__exit(struct perf_evlist *evlist)
{
	free(evlist->mmap);
	free(evlist->pollfd);
	evlist->mmap = NULL;
	evlist->pollfd = NULL;
}

void perf_evlist__delete(struct perf_evlist *evlist)
{
	perf_evlist__purge(evlist);
	perf_evlist__exit(evlist);
	free(evlist);
}

void perf_evlist__add(struct perf_evlist *evlist, struct perf_evsel *entry)
{
	list_add_tail(&entry->node, &evlist->entries);
	++evlist->nr_entries;
}

static void perf_evlist__splice_list_tail(struct perf_evlist *evlist,
					  struct list_head *list,
					  int nr_entries)
{
	list_splice_tail(list, &evlist->entries);
	evlist->nr_entries += nr_entries;
}

int perf_evlist__add_default(struct perf_evlist *evlist)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
	};
	struct perf_evsel *evsel;

	event_attr_init(&attr);

	evsel = perf_evsel__new(&attr, 0);
	if (evsel == NULL)
		goto error;

	/* use strdup() because free(evsel) assumes name is allocated */
	evsel->name = strdup("cycles");
	if (!evsel->name)
		goto error_free;

	perf_evlist__add(evlist, evsel);
	return 0;
error_free:
	perf_evsel__delete(evsel);
error:
	return -ENOMEM;
}

int perf_evlist__add_attrs(struct perf_evlist *evlist,
			   struct perf_event_attr *attrs, size_t nr_attrs)
{
	struct perf_evsel *evsel, *n;
	LIST_HEAD(head);
	size_t i;

	for (i = 0; i < nr_attrs; i++) {
		evsel = perf_evsel__new(attrs + i, evlist->nr_entries + i);
		if (evsel == NULL)
			goto out_delete_partial_list;
		list_add_tail(&evsel->node, &head);
	}

	perf_evlist__splice_list_tail(evlist, &head, nr_attrs);

	return 0;

out_delete_partial_list:
	list_for_each_entry_safe(evsel, n, &head, node)
		perf_evsel__delete(evsel);
	return -1;
}

static int trace_event__id(const char *evname)
{
	char *filename, *colon;
	int err = -1, fd;

	if (asprintf(&filename, "%s/%s/id", tracing_events_path, evname) < 0)
		return -1;

	colon = strrchr(filename, ':');
	if (colon != NULL)
		*colon = '/';

	fd = open(filename, O_RDONLY);
	if (fd >= 0) {
		char id[16];
		if (read(fd, id, sizeof(id)) > 0)
			err = atoi(id);
		close(fd);
	}

	free(filename);
	return err;
}

int perf_evlist__add_tracepoints(struct perf_evlist *evlist,
				 const char *tracepoints[],
				 size_t nr_tracepoints)
{
	int err;
	size_t i;
	struct perf_event_attr *attrs = zalloc(nr_tracepoints * sizeof(*attrs));

	if (attrs == NULL)
		return -1;

	for (i = 0; i < nr_tracepoints; i++) {
		err = trace_event__id(tracepoints[i]);

		if (err < 0)
			goto out_free_attrs;

		attrs[i].type	       = PERF_TYPE_TRACEPOINT;
		attrs[i].config	       = err;
	        attrs[i].sample_type   = (PERF_SAMPLE_RAW | PERF_SAMPLE_TIME |
					  PERF_SAMPLE_CPU);
		attrs[i].sample_period = 1;
	}

	err = perf_evlist__add_attrs(evlist, attrs, nr_tracepoints);
out_free_attrs:
	free(attrs);
	return err;
}

static struct perf_evsel *
	perf_evlist__find_tracepoint_by_id(struct perf_evlist *evlist, int id)
{
	struct perf_evsel *evsel;

	list_for_each_entry(evsel, &evlist->entries, node) {
		if (evsel->attr.type   == PERF_TYPE_TRACEPOINT &&
		    (int)evsel->attr.config == id)
			return evsel;
	}

	return NULL;
}

int perf_evlist__set_tracepoints_handlers(struct perf_evlist *evlist,
					  const struct perf_evsel_str_handler *assocs,
					  size_t nr_assocs)
{
	struct perf_evsel *evsel;
	int err;
	size_t i;

	for (i = 0; i < nr_assocs; i++) {
		err = trace_event__id(assocs[i].name);
		if (err < 0)
			goto out;

		evsel = perf_evlist__find_tracepoint_by_id(evlist, err);
		if (evsel == NULL)
			continue;

		err = -EEXIST;
		if (evsel->handler.func != NULL)
			goto out;
		evsel->handler.func = assocs[i].handler;
	}

	err = 0;
out:
	return err;
}

void perf_evlist__disable(struct perf_evlist *evlist)
{
	int cpu, thread;
	struct perf_evsel *pos;

	for (cpu = 0; cpu < evlist->cpus->nr; cpu++) {
		list_for_each_entry(pos, &evlist->entries, node) {
			for (thread = 0; thread < evlist->threads->nr; thread++)
				ioctl(FD(pos, cpu, thread), PERF_EVENT_IOC_DISABLE);
		}
	}
}

void perf_evlist__enable(struct perf_evlist *evlist)
{
	int cpu, thread;
	struct perf_evsel *pos;

	for (cpu = 0; cpu < evlist->cpus->nr; cpu++) {
		list_for_each_entry(pos, &evlist->entries, node) {
			for (thread = 0; thread < evlist->threads->nr; thread++)
				ioctl(FD(pos, cpu, thread), PERF_EVENT_IOC_ENABLE);
		}
	}
}

static int perf_evlist__alloc_pollfd(struct perf_evlist *evlist)
{
	int nfds = evlist->cpus->nr * evlist->threads->nr * evlist->nr_entries;
	evlist->pollfd = malloc(sizeof(struct pollfd) * nfds);
	return evlist->pollfd != NULL ? 0 : -ENOMEM;
}

void perf_evlist__add_pollfd(struct perf_evlist *evlist, int fd)
{
	fcntl(fd, F_SETFL, O_NONBLOCK);
	evlist->pollfd[evlist->nr_fds].fd = fd;
	evlist->pollfd[evlist->nr_fds].events = POLLIN;
	evlist->nr_fds++;
}

static void perf_evlist__id_hash(struct perf_evlist *evlist,
				 struct perf_evsel *evsel,
				 int cpu, int thread, u64 id)
{
	int hash;
	struct perf_sample_id *sid = SID(evsel, cpu, thread);

	sid->id = id;
	sid->evsel = evsel;
	hash = hash_64(sid->id, PERF_EVLIST__HLIST_BITS);
	hlist_add_head(&sid->node, &evlist->heads[hash]);
}

void perf_evlist__id_add(struct perf_evlist *evlist, struct perf_evsel *evsel,
			 int cpu, int thread, u64 id)
{
	perf_evlist__id_hash(evlist, evsel, cpu, thread, id);
	evsel->id[evsel->ids++] = id;
}

static int perf_evlist__id_add_fd(struct perf_evlist *evlist,
				  struct perf_evsel *evsel,
				  int cpu, int thread, int fd)
{
	u64 read_data[4] = { 0, };
	int id_idx = 1; /* The first entry is the counter value */

	if (!(evsel->attr.read_format & PERF_FORMAT_ID) ||
	    read(fd, &read_data, sizeof(read_data)) == -1)
		return -1;

	if (evsel->attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		++id_idx;
	if (evsel->attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		++id_idx;

	perf_evlist__id_add(evlist, evsel, cpu, thread, read_data[id_idx]);
	return 0;
}

struct perf_evsel *perf_evlist__id2evsel(struct perf_evlist *evlist, u64 id)
{
	struct hlist_head *head;
	struct hlist_node *pos;
	struct perf_sample_id *sid;
	int hash;

	if (evlist->nr_entries == 1)
		return list_entry(evlist->entries.next, struct perf_evsel, node);

	hash = hash_64(id, PERF_EVLIST__HLIST_BITS);
	head = &evlist->heads[hash];

	hlist_for_each_entry(sid, pos, head, node)
		if (sid->id == id)
			return sid->evsel;

	if (!perf_evlist__sample_id_all(evlist))
		return list_entry(evlist->entries.next, struct perf_evsel, node);

	return NULL;
}

union perf_event *perf_evlist__mmap_read(struct perf_evlist *evlist, int idx)
{
	/* XXX Move this to perf.c, making it generally available */
	unsigned int page_size = sysconf(_SC_PAGE_SIZE);
	struct perf_mmap *md = &evlist->mmap[idx];
	unsigned int head = perf_mmap__read_head(md);
	unsigned int old = md->prev;
	unsigned char *data = md->base + page_size;
	union perf_event *event = NULL;

	if (evlist->overwrite) {
		/*
		 * If we're further behind than half the buffer, there's a chance
		 * the writer will bite our tail and mess up the samples under us.
		 *
		 * If we somehow ended up ahead of the head, we got messed up.
		 *
		 * In either case, truncate and restart at head.
		 */
		int diff = head - old;
		if (diff > md->mask / 2 || diff < 0) {
			fprintf(stderr, "WARNING: failed to keep up with mmap data.\n");

			/*
			 * head points to a known good entry, start there.
			 */
			old = head;
		}
	}

	if (old != head) {
		size_t size;

		event = (union perf_event *)&data[old & md->mask];
		size = event->header.size;

		/*
		 * Event straddles the mmap boundary -- header should always
		 * be inside due to u64 alignment of output.
		 */
		if ((old & md->mask) + size != ((old + size) & md->mask)) {
			unsigned int offset = old;
			unsigned int len = min(sizeof(*event), size), cpy;
			void *dst = &evlist->event_copy;

			do {
				cpy = min(md->mask + 1 - (offset & md->mask), len);
				memcpy(dst, &data[offset & md->mask], cpy);
				offset += cpy;
				dst += cpy;
				len -= cpy;
			} while (len);

			event = &evlist->event_copy;
		}

		old += size;
	}

	md->prev = old;

	if (!evlist->overwrite)
		perf_mmap__write_tail(md, old);

	return event;
}

void perf_evlist__munmap(struct perf_evlist *evlist)
{
	int i;

	for (i = 0; i < evlist->nr_mmaps; i++) {
		if (evlist->mmap[i].base != NULL) {
			munmap(evlist->mmap[i].base, evlist->mmap_len);
			evlist->mmap[i].base = NULL;
		}
	}

	free(evlist->mmap);
	evlist->mmap = NULL;
}

static int perf_evlist__alloc_mmap(struct perf_evlist *evlist)
{
	evlist->nr_mmaps = evlist->cpus->nr;
	if (evlist->cpus->map[0] == -1)
		evlist->nr_mmaps = evlist->threads->nr;
	evlist->mmap = zalloc(evlist->nr_mmaps * sizeof(struct perf_mmap));
	return evlist->mmap != NULL ? 0 : -ENOMEM;
}

static int __perf_evlist__mmap(struct perf_evlist *evlist,
			       int idx, int prot, int mask, int fd)
{
	evlist->mmap[idx].prev = 0;
	evlist->mmap[idx].mask = mask;
	evlist->mmap[idx].base = mmap(NULL, evlist->mmap_len, prot,
				      MAP_SHARED, fd, 0);
	if (evlist->mmap[idx].base == MAP_FAILED) {
		evlist->mmap[idx].base = NULL;
		return -1;
	}

	perf_evlist__add_pollfd(evlist, fd);
	return 0;
}

static int perf_evlist__mmap_per_cpu(struct perf_evlist *evlist, int prot, int mask)
{
	struct perf_evsel *evsel;
	int cpu, thread;

	for (cpu = 0; cpu < evlist->cpus->nr; cpu++) {
		int output = -1;

		for (thread = 0; thread < evlist->threads->nr; thread++) {
			list_for_each_entry(evsel, &evlist->entries, node) {
				int fd = FD(evsel, cpu, thread);

				if (output == -1) {
					output = fd;
					if (__perf_evlist__mmap(evlist, cpu,
								prot, mask, output) < 0)
						goto out_unmap;
				} else {
					if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, output) != 0)
						goto out_unmap;
				}

				if ((evsel->attr.read_format & PERF_FORMAT_ID) &&
				    perf_evlist__id_add_fd(evlist, evsel, cpu, thread, fd) < 0)
					goto out_unmap;
			}
		}
	}

	return 0;

out_unmap:
	for (cpu = 0; cpu < evlist->cpus->nr; cpu++) {
		if (evlist->mmap[cpu].base != NULL) {
			munmap(evlist->mmap[cpu].base, evlist->mmap_len);
			evlist->mmap[cpu].base = NULL;
		}
	}
	return -1;
}

static int perf_evlist__mmap_per_thread(struct perf_evlist *evlist, int prot, int mask)
{
	struct perf_evsel *evsel;
	int thread;

	for (thread = 0; thread < evlist->threads->nr; thread++) {
		int output = -1;

		list_for_each_entry(evsel, &evlist->entries, node) {
			int fd = FD(evsel, 0, thread);

			if (output == -1) {
				output = fd;
				if (__perf_evlist__mmap(evlist, thread,
							prot, mask, output) < 0)
					goto out_unmap;
			} else {
				if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, output) != 0)
					goto out_unmap;
			}

			if ((evsel->attr.read_format & PERF_FORMAT_ID) &&
			    perf_evlist__id_add_fd(evlist, evsel, 0, thread, fd) < 0)
				goto out_unmap;
		}
	}

	return 0;

out_unmap:
	for (thread = 0; thread < evlist->threads->nr; thread++) {
		if (evlist->mmap[thread].base != NULL) {
			munmap(evlist->mmap[thread].base, evlist->mmap_len);
			evlist->mmap[thread].base = NULL;
		}
	}
	return -1;
}

/** perf_evlist__mmap - Create per cpu maps to receive events
 *
 * @evlist - list of events
 * @pages - map length in pages
 * @overwrite - overwrite older events?
 *
 * If overwrite is false the user needs to signal event consuption using:
 *
 *	struct perf_mmap *m = &evlist->mmap[cpu];
 *	unsigned int head = perf_mmap__read_head(m);
 *
 *	perf_mmap__write_tail(m, head)
 *
 * Using perf_evlist__read_on_cpu does this automatically.
 */
int perf_evlist__mmap(struct perf_evlist *evlist, unsigned int pages,
		      bool overwrite)
{
	unsigned int page_size = sysconf(_SC_PAGE_SIZE);
	struct perf_evsel *evsel;
	const struct cpu_map *cpus = evlist->cpus;
	const struct thread_map *threads = evlist->threads;
	int prot = PROT_READ | (overwrite ? 0 : PROT_WRITE), mask;

        /* 512 kiB: default amount of unprivileged mlocked memory */
        if (pages == UINT_MAX)
                pages = (512 * 1024) / page_size;
	else if (!is_power_of_2(pages))
		return -EINVAL;

	mask = pages * page_size - 1;

	if (evlist->mmap == NULL && perf_evlist__alloc_mmap(evlist) < 0)
		return -ENOMEM;

	if (evlist->pollfd == NULL && perf_evlist__alloc_pollfd(evlist) < 0)
		return -ENOMEM;

	evlist->overwrite = overwrite;
	evlist->mmap_len = (pages + 1) * page_size;

	list_for_each_entry(evsel, &evlist->entries, node) {
		if ((evsel->attr.read_format & PERF_FORMAT_ID) &&
		    evsel->sample_id == NULL &&
		    perf_evsel__alloc_id(evsel, cpus->nr, threads->nr) < 0)
			return -ENOMEM;
	}

	if (evlist->cpus->map[0] == -1)
		return perf_evlist__mmap_per_thread(evlist, prot, mask);

	return perf_evlist__mmap_per_cpu(evlist, prot, mask);
}

int perf_evlist__create_maps(struct perf_evlist *evlist, pid_t target_pid,
			     pid_t target_tid, const char *cpu_list)
{
	evlist->threads = thread_map__new(target_pid, target_tid);

	if (evlist->threads == NULL)
		return -1;

	if (cpu_list == NULL && target_tid != -1)
		evlist->cpus = cpu_map__dummy_new();
	else
		evlist->cpus = cpu_map__new(cpu_list);

	if (evlist->cpus == NULL)
		goto out_delete_threads;

	return 0;

out_delete_threads:
	thread_map__delete(evlist->threads);
	return -1;
}

void perf_evlist__delete_maps(struct perf_evlist *evlist)
{
	cpu_map__delete(evlist->cpus);
	thread_map__delete(evlist->threads);
	evlist->cpus	= NULL;
	evlist->threads = NULL;
}

int perf_evlist__set_filters(struct perf_evlist *evlist)
{
	const struct thread_map *threads = evlist->threads;
	const struct cpu_map *cpus = evlist->cpus;
	struct perf_evsel *evsel;
	char *filter;
	int thread;
	int cpu;
	int err;
	int fd;

	list_for_each_entry(evsel, &evlist->entries, node) {
		filter = evsel->filter;
		if (!filter)
			continue;
		for (cpu = 0; cpu < cpus->nr; cpu++) {
			for (thread = 0; thread < threads->nr; thread++) {
				fd = FD(evsel, cpu, thread);
				err = ioctl(fd, PERF_EVENT_IOC_SET_FILTER, filter);
				if (err)
					return err;
			}
		}
	}

	return 0;
}

bool perf_evlist__valid_sample_type(const struct perf_evlist *evlist)
{
	struct perf_evsel *pos, *first;

	pos = first = list_entry(evlist->entries.next, struct perf_evsel, node);

	list_for_each_entry_continue(pos, &evlist->entries, node) {
		if (first->attr.sample_type != pos->attr.sample_type)
			return false;
	}

	return true;
}

u64 perf_evlist__sample_type(const struct perf_evlist *evlist)
{
	struct perf_evsel *first;

	first = list_entry(evlist->entries.next, struct perf_evsel, node);
	return first->attr.sample_type;
}

u16 perf_evlist__id_hdr_size(const struct perf_evlist *evlist)
{
	struct perf_evsel *first;
	struct perf_sample *data;
	u64 sample_type;
	u16 size = 0;

	first = list_entry(evlist->entries.next, struct perf_evsel, node);

	if (!first->attr.sample_id_all)
		goto out;

	sample_type = first->attr.sample_type;

	if (sample_type & PERF_SAMPLE_TID)
		size += sizeof(data->tid) * 2;

       if (sample_type & PERF_SAMPLE_TIME)
		size += sizeof(data->time);

	if (sample_type & PERF_SAMPLE_ID)
		size += sizeof(data->id);

	if (sample_type & PERF_SAMPLE_STREAM_ID)
		size += sizeof(data->stream_id);

	if (sample_type & PERF_SAMPLE_CPU)
		size += sizeof(data->cpu) * 2;
out:
	return size;
}

bool perf_evlist__valid_sample_id_all(const struct perf_evlist *evlist)
{
	struct perf_evsel *pos, *first;

	pos = first = list_entry(evlist->entries.next, struct perf_evsel, node);

	list_for_each_entry_continue(pos, &evlist->entries, node) {
		if (first->attr.sample_id_all != pos->attr.sample_id_all)
			return false;
	}

	return true;
}

bool perf_evlist__sample_id_all(const struct perf_evlist *evlist)
{
	struct perf_evsel *first;

	first = list_entry(evlist->entries.next, struct perf_evsel, node);
	return first->attr.sample_id_all;
}

void perf_evlist__set_selected(struct perf_evlist *evlist,
			       struct perf_evsel *evsel)
{
	evlist->selected = evsel;
}

int perf_evlist__open(struct perf_evlist *evlist, bool group)
{
	struct perf_evsel *evsel, *first;
	int err, ncpus, nthreads;

	first = list_entry(evlist->entries.next, struct perf_evsel, node);

	list_for_each_entry(evsel, &evlist->entries, node) {
		struct xyarray *group_fd = NULL;

		if (group && evsel != first)
			group_fd = first->fd;

		err = perf_evsel__open(evsel, evlist->cpus, evlist->threads,
				       group, group_fd);
		if (err < 0)
			goto out_err;
	}

	return 0;
out_err:
	ncpus = evlist->cpus ? evlist->cpus->nr : 1;
	nthreads = evlist->threads ? evlist->threads->nr : 1;

	list_for_each_entry_reverse(evsel, &evlist->entries, node)
		perf_evsel__close(evsel, ncpus, nthreads);

	return err;
}

int perf_evlist__prepare_workload(struct perf_evlist *evlist,
				  struct perf_record_opts *opts,
				  const char *argv[])
{
	int child_ready_pipe[2], go_pipe[2];
	char bf;

	if (pipe(child_ready_pipe) < 0) {
		perror("failed to create 'ready' pipe");
		return -1;
	}

	if (pipe(go_pipe) < 0) {
		perror("failed to create 'go' pipe");
		goto out_close_ready_pipe;
	}

	evlist->workload.pid = fork();
	if (evlist->workload.pid < 0) {
		perror("failed to fork");
		goto out_close_pipes;
	}

	if (!evlist->workload.pid) {
		if (opts->pipe_output)
			dup2(2, 1);

		close(child_ready_pipe[0]);
		close(go_pipe[1]);
		fcntl(go_pipe[0], F_SETFD, FD_CLOEXEC);

		/*
		 * Do a dummy execvp to get the PLT entry resolved,
		 * so we avoid the resolver overhead on the real
		 * execvp call.
		 */
		execvp("", (char **)argv);

		/*
		 * Tell the parent we're ready to go
		 */
		close(child_ready_pipe[1]);

		/*
		 * Wait until the parent tells us to go.
		 */
		if (read(go_pipe[0], &bf, 1) == -1)
			perror("unable to read pipe");

		execvp(argv[0], (char **)argv);

		perror(argv[0]);
		kill(getppid(), SIGUSR1);
		exit(-1);
	}

	if (!opts->system_wide && opts->target_tid == -1 && opts->target_pid == -1)
		evlist->threads->map[0] = evlist->workload.pid;

	close(child_ready_pipe[1]);
	close(go_pipe[0]);
	/*
	 * wait for child to settle
	 */
	if (read(child_ready_pipe[0], &bf, 1) == -1) {
		perror("unable to read pipe");
		goto out_close_pipes;
	}

	evlist->workload.cork_fd = go_pipe[1];
	close(child_ready_pipe[0]);
	return 0;

out_close_pipes:
	close(go_pipe[0]);
	close(go_pipe[1]);
out_close_ready_pipe:
	close(child_ready_pipe[0]);
	close(child_ready_pipe[1]);
	return -1;
}

int perf_evlist__start_workload(struct perf_evlist *evlist)
{
	if (evlist->workload.cork_fd > 0) {
		/*
		 * Remove the cork, let it rip!
		 */
		return close(evlist->workload.cork_fd);
	}

	return 0;
}
