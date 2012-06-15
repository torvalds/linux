#ifndef __PERF_THREAD_MAP_H
#define __PERF_THREAD_MAP_H

#include <sys/types.h>
#include <stdio.h>

struct thread_map {
	int nr;
	pid_t map[];
};

struct thread_map *thread_map__new_by_pid(pid_t pid);
struct thread_map *thread_map__new_by_tid(pid_t tid);
struct thread_map *thread_map__new_by_uid(uid_t uid);
struct thread_map *thread_map__new(pid_t pid, pid_t tid, uid_t uid);

struct thread_map *thread_map__new_str(const char *pid,
		const char *tid, uid_t uid);

void thread_map__delete(struct thread_map *threads);

size_t thread_map__fprintf(struct thread_map *threads, FILE *fp);

#endif	/* __PERF_THREAD_MAP_H */
