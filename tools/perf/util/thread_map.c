// SPDX-License-Identifier: GPL-2.0
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "string2.h"
#include "strlist.h"
#include <string.h>
#include <api/fs/fs.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include "asm/bug.h"
#include "thread_map.h"
#include "debug.h"
#include "event.h"
#include <internal/threadmap.h>

/* Skip "." and ".." directories */
static int filter(const struct dirent *dir)
{
	if (dir->d_name[0] == '.')
		return 0;
	else
		return 1;
}

#define thread_map__alloc(__nr) perf_thread_map__realloc(NULL, __nr)

struct perf_thread_map *thread_map__new_by_pid(pid_t pid)
{
	struct perf_thread_map *threads;
	char name[256];
	int items;
	struct dirent **namelist = NULL;
	int i;

	sprintf(name, "/proc/%d/task", pid);
	items = scandir(name, &namelist, filter, NULL);
	if (items <= 0)
		return NULL;

	threads = thread_map__alloc(items);
	if (threads != NULL) {
		for (i = 0; i < items; i++)
			perf_thread_map__set_pid(threads, i, atoi(namelist[i]->d_name));
		threads->nr = items;
		refcount_set(&threads->refcnt, 1);
	}

	for (i=0; i<items; i++)
		zfree(&namelist[i]);
	free(namelist);

	return threads;
}

struct perf_thread_map *thread_map__new_by_tid(pid_t tid)
{
	struct perf_thread_map *threads = thread_map__alloc(1);

	if (threads != NULL) {
		perf_thread_map__set_pid(threads, 0, tid);
		threads->nr = 1;
		refcount_set(&threads->refcnt, 1);
	}

	return threads;
}

static struct perf_thread_map *thread_map__new_all_cpus(void)
{
	DIR *proc;
	int max_threads = 32, items, i;
	char path[NAME_MAX + 1 + 6];
	struct dirent *dirent, **namelist = NULL;
	struct perf_thread_map *threads = thread_map__alloc(max_threads);

	if (threads == NULL)
		goto out;

	proc = opendir("/proc");
	if (proc == NULL)
		goto out_free_threads;

	threads->nr = 0;
	refcount_set(&threads->refcnt, 1);

	while ((dirent = readdir(proc)) != NULL) {
		char *end;
		bool grow = false;
		pid_t pid = strtol(dirent->d_name, &end, 10);

		if (*end) /* only interested in proper numerical dirents */
			continue;

		snprintf(path, sizeof(path), "/proc/%d/task", pid);
		items = scandir(path, &namelist, filter, NULL);
		if (items <= 0) {
			pr_debug("scandir for %d returned empty, skipping\n", pid);
			continue;
		}
		while (threads->nr + items >= max_threads) {
			max_threads *= 2;
			grow = true;
		}

		if (grow) {
			struct perf_thread_map *tmp;

			tmp = perf_thread_map__realloc(threads, max_threads);
			if (tmp == NULL)
				goto out_free_namelist;

			threads = tmp;
		}

		for (i = 0; i < items; i++) {
			perf_thread_map__set_pid(threads, threads->nr + i,
						    atoi(namelist[i]->d_name));
		}

		for (i = 0; i < items; i++)
			zfree(&namelist[i]);
		free(namelist);

		threads->nr += items;
	}

out_closedir:
	closedir(proc);
out:
	return threads;

out_free_threads:
	free(threads);
	return NULL;

out_free_namelist:
	for (i = 0; i < items; i++)
		zfree(&namelist[i]);
	free(namelist);
	zfree(&threads);
	goto out_closedir;
}

struct perf_thread_map *thread_map__new(pid_t pid, pid_t tid)
{
	if (pid != -1)
		return thread_map__new_by_pid(pid);

	return thread_map__new_by_tid(tid);
}

static struct perf_thread_map *thread_map__new_by_pid_str(const char *pid_str)
{
	struct perf_thread_map *threads = NULL, *nt;
	char name[256];
	int items, total_tasks = 0;
	struct dirent **namelist = NULL;
	int i, j = 0;
	pid_t pid, prev_pid = INT_MAX;
	char *end_ptr;
	struct str_node *pos;
	struct strlist_config slist_config = { .dont_dupstr = true, };
	struct strlist *slist = strlist__new(pid_str, &slist_config);

	if (!slist)
		return NULL;

	strlist__for_each_entry(pos, slist) {
		pid = strtol(pos->s, &end_ptr, 10);

		if (pid == INT_MIN || pid == INT_MAX ||
		    (*end_ptr != '\0' && *end_ptr != ','))
			goto out_free_threads;

		if (pid == prev_pid)
			continue;

		sprintf(name, "/proc/%d/task", pid);
		items = scandir(name, &namelist, filter, NULL);
		if (items <= 0)
			goto out_free_threads;

		total_tasks += items;
		nt = perf_thread_map__realloc(threads, total_tasks);
		if (nt == NULL)
			goto out_free_namelist;

		threads = nt;

		for (i = 0; i < items; i++) {
			perf_thread_map__set_pid(threads, j++, atoi(namelist[i]->d_name));
			zfree(&namelist[i]);
		}
		threads->nr = total_tasks;
		free(namelist);
	}

out:
	strlist__delete(slist);
	if (threads)
		refcount_set(&threads->refcnt, 1);
	return threads;

out_free_namelist:
	for (i = 0; i < items; i++)
		zfree(&namelist[i]);
	free(namelist);

out_free_threads:
	zfree(&threads);
	goto out;
}

struct perf_thread_map *thread_map__new_by_tid_str(const char *tid_str)
{
	struct perf_thread_map *threads = NULL, *nt;
	int ntasks = 0;
	pid_t tid, prev_tid = INT_MAX;
	char *end_ptr;
	struct str_node *pos;
	struct strlist_config slist_config = { .dont_dupstr = true, };
	struct strlist *slist;

	/* perf-stat expects threads to be generated even if tid not given */
	if (!tid_str)
		return perf_thread_map__new_dummy();

	slist = strlist__new(tid_str, &slist_config);
	if (!slist)
		return NULL;

	strlist__for_each_entry(pos, slist) {
		tid = strtol(pos->s, &end_ptr, 10);

		if (tid == INT_MIN || tid == INT_MAX ||
		    (*end_ptr != '\0' && *end_ptr != ','))
			goto out_free_threads;

		if (tid == prev_tid)
			continue;

		ntasks++;
		nt = perf_thread_map__realloc(threads, ntasks);

		if (nt == NULL)
			goto out_free_threads;

		threads = nt;
		perf_thread_map__set_pid(threads, ntasks - 1, tid);
		threads->nr = ntasks;
	}
out:
	strlist__delete(slist);
	if (threads)
		refcount_set(&threads->refcnt, 1);
	return threads;

out_free_threads:
	zfree(&threads);
	goto out;
}

struct perf_thread_map *thread_map__new_str(const char *pid, const char *tid, bool all_threads)
{
	if (pid)
		return thread_map__new_by_pid_str(pid);

	if (all_threads)
		return thread_map__new_all_cpus();

	return thread_map__new_by_tid_str(tid);
}

size_t thread_map__fprintf(struct perf_thread_map *threads, FILE *fp)
{
	int i;
	size_t printed = fprintf(fp, "%d thread%s: ",
				 threads->nr, threads->nr > 1 ? "s" : "");
	for (i = 0; i < threads->nr; ++i)
		printed += fprintf(fp, "%s%d", i ? ", " : "", perf_thread_map__pid(threads, i));

	return printed + fprintf(fp, "\n");
}

static int get_comm(char **comm, pid_t pid)
{
	char *path;
	size_t size;
	int err;

	if (asprintf(&path, "%s/%d/comm", procfs__mountpoint(), pid) == -1)
		return -ENOMEM;

	err = filename__read_str(path, comm, &size);
	if (!err) {
		/*
		 * We're reading 16 bytes, while filename__read_str
		 * allocates data per BUFSIZ bytes, so we can safely
		 * mark the end of the string.
		 */
		(*comm)[size] = 0;
		strim(*comm);
	}

	free(path);
	return err;
}

static void comm_init(struct perf_thread_map *map, int i)
{
	pid_t pid = perf_thread_map__pid(map, i);
	char *comm = NULL;

	/* dummy pid comm initialization */
	if (pid == -1) {
		map->map[i].comm = strdup("dummy");
		return;
	}

	/*
	 * The comm name is like extra bonus ;-),
	 * so just warn if we fail for any reason.
	 */
	if (get_comm(&comm, pid))
		pr_warning("Couldn't resolve comm name for pid %d\n", pid);

	map->map[i].comm = comm;
}

void thread_map__read_comms(struct perf_thread_map *threads)
{
	int i;

	for (i = 0; i < threads->nr; ++i)
		comm_init(threads, i);
}

static void thread_map__copy_event(struct perf_thread_map *threads,
				   struct perf_record_thread_map *event)
{
	unsigned i;

	threads->nr = (int) event->nr;

	for (i = 0; i < event->nr; i++) {
		perf_thread_map__set_pid(threads, i, (pid_t) event->entries[i].pid);
		threads->map[i].comm = strndup(event->entries[i].comm, 16);
	}

	refcount_set(&threads->refcnt, 1);
}

struct perf_thread_map *thread_map__new_event(struct perf_record_thread_map *event)
{
	struct perf_thread_map *threads;

	threads = thread_map__alloc(event->nr);
	if (threads)
		thread_map__copy_event(threads, event);

	return threads;
}

bool thread_map__has(struct perf_thread_map *threads, pid_t pid)
{
	int i;

	for (i = 0; i < threads->nr; ++i) {
		if (threads->map[i].pid == pid)
			return true;
	}

	return false;
}

int thread_map__remove(struct perf_thread_map *threads, int idx)
{
	int i;

	if (threads->nr < 1)
		return -EINVAL;

	if (idx >= threads->nr)
		return -EINVAL;

	/*
	 * Free the 'idx' item and shift the rest up.
	 */
	zfree(&threads->map[idx].comm);

	for (i = idx; i < threads->nr - 1; i++)
		threads->map[i] = threads->map[i + 1];

	threads->nr--;
	return 0;
}
