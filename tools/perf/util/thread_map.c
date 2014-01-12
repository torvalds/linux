#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "strlist.h"
#include <string.h>
#include "thread_map.h"
#include "util.h"

/* Skip "." and ".." directories */
static int filter(const struct dirent *dir)
{
	if (dir->d_name[0] == '.')
		return 0;
	else
		return 1;
}

struct thread_map *thread_map__new_by_pid(pid_t pid)
{
	struct thread_map *threads;
	char name[256];
	int items;
	struct dirent **namelist = NULL;
	int i;

	sprintf(name, "/proc/%d/task", pid);
	items = scandir(name, &namelist, filter, NULL);
	if (items <= 0)
		return NULL;

	threads = malloc(sizeof(*threads) + sizeof(pid_t) * items);
	if (threads != NULL) {
		for (i = 0; i < items; i++)
			threads->map[i] = atoi(namelist[i]->d_name);
		threads->nr = items;
	}

	for (i=0; i<items; i++)
		zfree(&namelist[i]);
	free(namelist);

	return threads;
}

struct thread_map *thread_map__new_by_tid(pid_t tid)
{
	struct thread_map *threads = malloc(sizeof(*threads) + sizeof(pid_t));

	if (threads != NULL) {
		threads->map[0] = tid;
		threads->nr	= 1;
	}

	return threads;
}

struct thread_map *thread_map__new_by_uid(uid_t uid)
{
	DIR *proc;
	int max_threads = 32, items, i;
	char path[256];
	struct dirent dirent, *next, **namelist = NULL;
	struct thread_map *threads = malloc(sizeof(*threads) +
					    max_threads * sizeof(pid_t));
	if (threads == NULL)
		goto out;

	proc = opendir("/proc");
	if (proc == NULL)
		goto out_free_threads;

	threads->nr = 0;

	while (!readdir_r(proc, &dirent, &next) && next) {
		char *end;
		bool grow = false;
		struct stat st;
		pid_t pid = strtol(dirent.d_name, &end, 10);

		if (*end) /* only interested in proper numerical dirents */
			continue;

		snprintf(path, sizeof(path), "/proc/%s", dirent.d_name);

		if (stat(path, &st) != 0)
			continue;

		if (st.st_uid != uid)
			continue;

		snprintf(path, sizeof(path), "/proc/%d/task", pid);
		items = scandir(path, &namelist, filter, NULL);
		if (items <= 0)
			goto out_free_closedir;

		while (threads->nr + items >= max_threads) {
			max_threads *= 2;
			grow = true;
		}

		if (grow) {
			struct thread_map *tmp;

			tmp = realloc(threads, (sizeof(*threads) +
						max_threads * sizeof(pid_t)));
			if (tmp == NULL)
				goto out_free_namelist;

			threads = tmp;
		}

		for (i = 0; i < items; i++)
			threads->map[threads->nr + i] = atoi(namelist[i]->d_name);

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

out_free_closedir:
	zfree(&threads);
	goto out_closedir;
}

struct thread_map *thread_map__new(pid_t pid, pid_t tid, uid_t uid)
{
	if (pid != -1)
		return thread_map__new_by_pid(pid);

	if (tid == -1 && uid != UINT_MAX)
		return thread_map__new_by_uid(uid);

	return thread_map__new_by_tid(tid);
}

static struct thread_map *thread_map__new_by_pid_str(const char *pid_str)
{
	struct thread_map *threads = NULL, *nt;
	char name[256];
	int items, total_tasks = 0;
	struct dirent **namelist = NULL;
	int i, j = 0;
	pid_t pid, prev_pid = INT_MAX;
	char *end_ptr;
	struct str_node *pos;
	struct strlist *slist = strlist__new(false, pid_str);

	if (!slist)
		return NULL;

	strlist__for_each(pos, slist) {
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
		nt = realloc(threads, (sizeof(*threads) +
				       sizeof(pid_t) * total_tasks));
		if (nt == NULL)
			goto out_free_namelist;

		threads = nt;

		for (i = 0; i < items; i++) {
			threads->map[j++] = atoi(namelist[i]->d_name);
			zfree(&namelist[i]);
		}
		threads->nr = total_tasks;
		free(namelist);
	}

out:
	strlist__delete(slist);
	return threads;

out_free_namelist:
	for (i = 0; i < items; i++)
		zfree(&namelist[i]);
	free(namelist);

out_free_threads:
	zfree(&threads);
	goto out;
}

static struct thread_map *thread_map__new_by_tid_str(const char *tid_str)
{
	struct thread_map *threads = NULL, *nt;
	int ntasks = 0;
	pid_t tid, prev_tid = INT_MAX;
	char *end_ptr;
	struct str_node *pos;
	struct strlist *slist;

	/* perf-stat expects threads to be generated even if tid not given */
	if (!tid_str) {
		threads = malloc(sizeof(*threads) + sizeof(pid_t));
		if (threads != NULL) {
			threads->map[0] = -1;
			threads->nr	= 1;
		}
		return threads;
	}

	slist = strlist__new(false, tid_str);
	if (!slist)
		return NULL;

	strlist__for_each(pos, slist) {
		tid = strtol(pos->s, &end_ptr, 10);

		if (tid == INT_MIN || tid == INT_MAX ||
		    (*end_ptr != '\0' && *end_ptr != ','))
			goto out_free_threads;

		if (tid == prev_tid)
			continue;

		ntasks++;
		nt = realloc(threads, sizeof(*threads) + sizeof(pid_t) * ntasks);

		if (nt == NULL)
			goto out_free_threads;

		threads = nt;
		threads->map[ntasks - 1] = tid;
		threads->nr		 = ntasks;
	}
out:
	return threads;

out_free_threads:
	zfree(&threads);
	goto out;
}

struct thread_map *thread_map__new_str(const char *pid, const char *tid,
				       uid_t uid)
{
	if (pid)
		return thread_map__new_by_pid_str(pid);

	if (!tid && uid != UINT_MAX)
		return thread_map__new_by_uid(uid);

	return thread_map__new_by_tid_str(tid);
}

void thread_map__delete(struct thread_map *threads)
{
	free(threads);
}

size_t thread_map__fprintf(struct thread_map *threads, FILE *fp)
{
	int i;
	size_t printed = fprintf(fp, "%d thread%s: ",
				 threads->nr, threads->nr > 1 ? "s" : "");
	for (i = 0; i < threads->nr; ++i)
		printed += fprintf(fp, "%s%d", i ? ", " : "", threads->map[i]);

	return printed + fprintf(fp, "\n");
}
