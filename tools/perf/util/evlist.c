#include <poll.h>
#include "evlist.h"
#include "evsel.h"
#include "util.h"

#include <linux/bitops.h>
#include <linux/hash.h>

struct perf_evlist *perf_evlist__new(void)
{
	struct perf_evlist *evlist = zalloc(sizeof(*evlist));

	if (evlist != NULL) {
		int i;

		for (i = 0; i < PERF_EVLIST__HLIST_SIZE; ++i)
			INIT_HLIST_HEAD(&evlist->heads[i]);
		INIT_LIST_HEAD(&evlist->entries);
	}

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

void perf_evlist__delete(struct perf_evlist *evlist)
{
	perf_evlist__purge(evlist);
	free(evlist->mmap);
	free(evlist->pollfd);
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
