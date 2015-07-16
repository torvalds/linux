/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#ifndef __SELFTESTS_POWERPC_PMU_LIB_H
#define __SELFTESTS_POWERPC_PMU_LIB_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

union pipe {
	struct {
		int read_fd;
		int write_fd;
	};
	int fds[2];
};

extern int pick_online_cpu(void);
extern int bind_to_cpu(int cpu);
extern int kill_child_and_wait(pid_t child_pid);
extern int wait_for_child(pid_t child_pid);
extern int sync_with_child(union pipe read_pipe, union pipe write_pipe);
extern int wait_for_parent(union pipe read_pipe);
extern int notify_parent(union pipe write_pipe);
extern int notify_parent_of_error(union pipe write_pipe);
extern pid_t eat_cpu(int (test_function)(void));
extern bool require_paranoia_below(int level);

struct addr_range {
	uint64_t first, last;
};

extern struct addr_range libc, vdso;

int parse_proc_maps(void);

#endif /* __SELFTESTS_POWERPC_PMU_LIB_H */
