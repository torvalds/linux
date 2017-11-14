/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CGROUP_HELPERS_H
#define __CGROUP_HELPERS_H
#include <errno.h>
#include <string.h>

#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_err(MSG, ...) fprintf(stderr, "(%s:%d: errno: %s) " MSG "\n", \
	__FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)


int create_and_get_cgroup(char *path);
int join_cgroup(char *path);
int setup_cgroup_environment(void);
void cleanup_cgroup_environment(void);

#endif
