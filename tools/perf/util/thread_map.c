#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "thread_map.h"

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
		free(namelist[i]);
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
			free(namelist[i]);
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
		free(namelist[i]);
	free(namelist);

out_free_closedir:
	free(threads);
	threads = NULL;
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
