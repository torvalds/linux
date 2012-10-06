#ifndef __PERF_MACHINE_H
#define __PERF_MACHINE_H

#include <sys/types.h>

struct thread;
struct machine;

struct thread *machine__find_thread(struct machine *machine, pid_t pid);

#endif /* __PERF_MACHINE_H */
