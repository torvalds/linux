#ifndef __PERF_MACHINE_H
#define __PERF_MACHINE_H

#include <sys/types.h>

struct thread;
struct machine;
union perf_event;

struct thread *machine__find_thread(struct machine *machine, pid_t pid);

int machine__process_comm_event(struct machine *machine, union perf_event *event);
int machine__process_exit_event(struct machine *machine, union perf_event *event);
int machine__process_fork_event(struct machine *machine, union perf_event *event);
int machine__process_lost_event(struct machine *machine, union perf_event *event);
int machine__process_mmap_event(struct machine *machine, union perf_event *event);
int machine__process_event(struct machine *machine, union perf_event *event);

#endif /* __PERF_MACHINE_H */
