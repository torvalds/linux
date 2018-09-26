/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>

#define PAGE_SIZE 4096

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define MB(x) (x << 20)

/*
 * Checks if two given values differ by less than err% of their sum.
 */
static inline int values_close(long a, long b, int err)
{
	return abs(a - b) <= (a + b) / 100 * err;
}

extern int cg_find_unified_root(char *root, size_t len);
extern char *cg_name(const char *root, const char *name);
extern char *cg_name_indexed(const char *root, const char *name, int index);
extern int cg_create(const char *cgroup);
extern int cg_destroy(const char *cgroup);
extern int cg_read(const char *cgroup, const char *control,
		   char *buf, size_t len);
extern int cg_read_strcmp(const char *cgroup, const char *control,
			  const char *expected);
extern int cg_read_strstr(const char *cgroup, const char *control,
			  const char *needle);
extern long cg_read_long(const char *cgroup, const char *control);
long cg_read_key_long(const char *cgroup, const char *control, const char *key);
extern int cg_write(const char *cgroup, const char *control, char *buf);
extern int cg_run(const char *cgroup,
		  int (*fn)(const char *cgroup, void *arg),
		  void *arg);
extern int cg_enter_current(const char *cgroup);
extern int cg_run_nowait(const char *cgroup,
			 int (*fn)(const char *cgroup, void *arg),
			 void *arg);
extern int get_temp_fd(void);
extern int alloc_pagecache(int fd, size_t size);
extern int alloc_anon(const char *cgroup, void *arg);
extern int is_swap_enabled(void);
extern int set_oom_adj_score(int pid, int score);
