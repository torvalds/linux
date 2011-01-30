/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-{top,stat,record}.c, see those files for further
 * copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */
#include <poll.h>
#include "cpumap.h"
#include "thread_map.h"
#include "evlist.h"
#include "evsel.h"
#include "util.h"

#include <sys/mman.h>

#include <linux/bitops.h>
#include <linux/hash.h>

#define FD(e, x, y) (*(int *)xyarray__entry(e->fd, x, y))
#define SID(e, x, y) xyarray__entry(e->id, x, y)

void perf_evlist__init(struct perf_evlist *evlist)
{
	int i;

	for (i = 0; i < PERF_EVLIST__HLIST_SIZE; ++i)
		INIT_HLIST_HEAD(&evlist->heads[i]);
	INIT_LIST_HEAD(&evlist->entries);
}

struct perf_evlist *perf_evlist__new(void)
{
	struct perf_evlist *evlist = zalloc(sizeof(*evlist));

	if (evlist != NULL)
		perf_evlist__init(evlist);

	return evlist;
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

int perf_evlist__add_default(struct perf_evlist *evlist)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
	};
	struct perf_evsel *evsel = perf_evsel__new(&attr, 0);

	if (evsel == NULL)
		return -ENOMEM;

	perf_evlist__add(evlist, evsel);
	return 0;
}

int perf_evlist__alloc_pollfd(struct perf_evlist *evlist, int ncpus, int nthreads)
{
	int nfds = ncpus * nthreads * evlist->nr_entries;
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

static int perf_evlist__id_hash(struct perf_evlist *evlist, struct perf_evsel *evsel,
			       int cpu, int thread, int fd)
{
	struct perf_sample_id *sid;
	u64 read_data[4] = { 0, };
	int hash, id_idx = 1; /* The first entry is the counter value */

	if (!(evsel->attr.read_format & PERF_FORMAT_ID) ||
	    read(fd, &read_data, sizeof(read_data)) == -1)
		return -1;

	if (evsel->attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		++id_idx;
	if (evsel->attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		++id_idx;

	sid = SID(evsel, cpu, thread);
	sid->id = read_data[id_idx];
	sid->evsel = evsel;
	hash = hash_64(sid->id, PERF_EVLIST__HLIST_BITS);
	hlist_add_head(&sid->node, &evlist->heads[hash]);
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
	return NULL;
}

union perf_event *perf_evlist__read_on_cpu(struct perf_evlist *evlist, int cpu)
{
	/* XXX Move this to perf.c, making it generally available */
	unsigned int page_size = sysconf(_SC_PAGE_SIZE);
	struct perf_mmap *md = &evlist->mmap[cpu];
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

void perf_evlist__munmap(struct perf_evlist *evlist, int ncpus)
{
	int cpu;

	for (cpu = 0; cpu < ncpus; cpu++) {
		if (evlist->mmap[cpu].base != NULL) {
			munmap(evlist->mmap[cpu].base, evlist->mmap_len);
			evlist->mmap[cpu].base = NULL;
		}
	}
}

int perf_evlist__alloc_mmap(struct perf_evlist *evlist, int ncpus)
{
	evlist->mmap = zalloc(ncpus * sizeof(struct perf_mmap));
	return evlist->mmap != NULL ? 0 : -ENOMEM;
}

static int __perf_evlist__mmap(struct perf_evlist *evlist, int cpu, int prot,
			       int mask, int fd)
{
	evlist->mmap[cpu].prev = 0;
	evlist->mmap[cpu].mask = mask;
	evlist->mmap[cpu].base = mmap(NULL, evlist->mmap_len, prot,
				      MAP_SHARED, fd, 0);
	if (evlist->mmap[cpu].base == MAP_FAILED)
		return -1;

	perf_evlist__add_pollfd(evlist, fd);
	return 0;
}

/** perf_evlist__mmap - Create per cpu maps to receive events
 *
 * @evlist - list of events
 * @cpus - cpu map being monitored
 * @threads - threads map being monitored
 * @pages - map length in pages
 * @overwrite - overwrite older events?
 *
 * If overwrite is false the user needs to signal event consuption using:
 *
 *	struct perf_mmap *m = &evlist->mmap[cpu];
 *	unsigned int head = perf_mmap__read_head(m);
 *
 *	perf_mmap__write_tail(m, head)
 */
int perf_evlist__mmap(struct perf_evlist *evlist, struct cpu_map *cpus,
		      struct thread_map *threads, int pages, bool overwrite)
{
	unsigned int page_size = sysconf(_SC_PAGE_SIZE);
	int mask = pages * page_size - 1, cpu;
	struct perf_evsel *first_evsel, *evsel;
	int thread, prot = PROT_READ | (overwrite ? 0 : PROT_WRITE);

	if (evlist->mmap == NULL &&
	    perf_evlist__alloc_mmap(evlist, cpus->nr) < 0)
		return -ENOMEM;

	if (evlist->pollfd == NULL &&
	    perf_evlist__alloc_pollfd(evlist, cpus->nr, threads->nr) < 0)
		return -ENOMEM;

	evlist->overwrite = overwrite;
	evlist->mmap_len = (pages + 1) * page_size;
	first_evsel = list_entry(evlist->entries.next, struct perf_evsel, node);

	list_for_each_entry(evsel, &evlist->entries, node) {
		if ((evsel->attr.read_format & PERF_FORMAT_ID) &&
		    evsel->id == NULL &&
		    perf_evsel__alloc_id(evsel, cpus->nr, threads->nr) < 0)
			return -ENOMEM;

		for (cpu = 0; cpu < cpus->nr; cpu++) {
			for (thread = 0; thread < threads->nr; thread++) {
				int fd = FD(evsel, cpu, thread);

				if (evsel->idx || thread) {
					if (ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT,
						  FD(first_evsel, cpu, 0)) != 0)
						goto out_unmap;
				} else if (__perf_evlist__mmap(evlist, cpu, prot, mask, fd) < 0)
					goto out_unmap;

				if ((evsel->attr.read_format & PERF_FORMAT_ID) &&
				    perf_evlist__id_hash(evlist, evsel, cpu, thread, fd) < 0)
					goto out_unmap;
			}
		}
	}

	return 0;

out_unmap:
	for (cpu = 0; cpu < cpus->nr; cpu++) {
		if (evlist->mmap[cpu].base != NULL) {
			munmap(evlist->mmap[cpu].base, evlist->mmap_len);
			evlist->mmap[cpu].base = NULL;
		}
	}
	return -1;
}
