/* SPDX-License-Identifier: GPL-2.0 */
#include <stdbool.h>
#include <stdlib.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define MB(x) (x << 20)

#define USEC_PER_SEC	1000000L
#define NSEC_PER_SEC	1000000000L

#define TEST_UID	65534 /* usually nobody, any !root is fine */

#define CG_THREADS_FILE (!cg_test_v1_named ? "cgroup.threads" : "tasks")
#define CG_NAMED_NAME "selftest"
#define CG_PATH_FORMAT (!cg_test_v1_named ? "0::%s" : (":name=" CG_NAMED_NAME ":%s"))

/*
 * Checks if two given values differ by less than err% of their sum.
 */
static inline int values_close(long a, long b, int err)
{
	return labs(a - b) <= (a + b) / 100 * err;
}

/*
 * Checks if two given values differ by less than err% of their sum and assert
 * with detailed debug info if not.
 */
static inline int values_close_report(long a, long b, int err)
{
	long diff  = labs(a - b);
	long limit = (a + b) / 100 * err;
	double actual_err = (a + b) ? (100.0 * diff / (a + b)) : 0.0;
	int close = diff <= limit;

	if (!close)
		fprintf(stderr,
			"[FAIL] actual=%ld expected=%ld | diff=%ld | limit=%ld | "
			"tolerance=%d%% | actual_error=%.2f%%\n",
			a, b, diff, limit, err, actual_err);

	return close;
}

extern ssize_t read_text(const char *path, char *buf, size_t max_len);
extern ssize_t write_text(const char *path, char *buf, ssize_t len);

extern int cg_find_controller_root(char *root, size_t len, const char *controller);
extern int cg_find_unified_root(char *root, size_t len, bool *nsdelegate);
extern char *cg_name(const char *root, const char *name);
extern char *cg_name_indexed(const char *root, const char *name, int index);
extern char *cg_control(const char *cgroup, const char *control);
extern int cg_create(const char *cgroup);
extern int cg_destroy(const char *cgroup);
extern int cg_read(const char *cgroup, const char *control,
		   char *buf, size_t len);
extern int cg_read_strcmp(const char *cgroup, const char *control,
			  const char *expected);
extern int cg_read_strstr(const char *cgroup, const char *control,
			  const char *needle);
extern long cg_read_long(const char *cgroup, const char *control);
extern long cg_read_long_fd(int fd);
long cg_read_key_long(const char *cgroup, const char *control, const char *key);
extern long cg_read_lc(const char *cgroup, const char *control);
extern int cg_write(const char *cgroup, const char *control, char *buf);
extern int cg_open(const char *cgroup, const char *control, int flags);
int cg_write_numeric(const char *cgroup, const char *control, long value);
extern int cg_run(const char *cgroup,
		  int (*fn)(const char *cgroup, void *arg),
		  void *arg);
extern int cg_enter(const char *cgroup, int pid);
extern int cg_enter_current(const char *cgroup);
extern int cg_enter_current_thread(const char *cgroup);
extern int cg_run_nowait(const char *cgroup,
			 int (*fn)(const char *cgroup, void *arg),
			 void *arg);
extern int cg_wait_for_proc_count(const char *cgroup, int count);
extern int cg_killall(const char *cgroup);
int proc_mount_contains(const char *option);
int cgroup_feature(const char *feature);
extern ssize_t proc_read_text(int pid, bool thread, const char *item, char *buf, size_t size);
extern int proc_read_strstr(int pid, bool thread, const char *item, const char *needle);
extern pid_t clone_into_cgroup(int cgroup_fd);
extern int clone_reap(pid_t pid, int options);
extern int clone_into_cgroup_run_wait(const char *cgroup);
extern int dirfd_open_opath(const char *dir);
extern int cg_prepare_for_wait(const char *cgroup);
extern int memcg_prepare_for_wait(const char *cgroup);
extern int cg_wait_for(int fd);
extern bool cg_test_v1_named;
