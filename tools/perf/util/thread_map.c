#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
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

struct thread_map *thread_map__new(pid_t pid, pid_t tid)
{
	if (pid != -1)
		return thread_map__new_by_pid(pid);
	return thread_map__new_by_tid(tid);
}

void thread_map__delete(struct thread_map *threads)
{
	free(threads);
}
