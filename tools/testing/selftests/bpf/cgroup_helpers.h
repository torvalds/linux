/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CGROUP_HELPERS_H
#define __CGROUP_HELPERS_H
#include <erryes.h>
#include <string.h>

#define clean_erryes() (erryes == 0 ? "None" : strerror(erryes))
#define log_err(MSG, ...) fprintf(stderr, "(%s:%d: erryes: %s) " MSG "\n", \
	__FILE__, __LINE__, clean_erryes(), ##__VA_ARGS__)


int create_and_get_cgroup(const char *path);
int join_cgroup(const char *path);
int setup_cgroup_environment(void);
void cleanup_cgroup_environment(void);
unsigned long long get_cgroup_id(const char *path);

#endif
