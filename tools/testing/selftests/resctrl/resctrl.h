/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RESCTRL_H
#define RESCTRL_H
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <dirent.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/eventfd.h>
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include "../kselftest.h"

#define MB			(1024 * 1024)
#define RESCTRL_PATH		"/sys/fs/resctrl"
#define PHYS_ID_PATH		"/sys/devices/system/cpu/cpu"
#define INFO_PATH		"/sys/fs/resctrl/info"

/*
 * CPU vendor IDs
 *
 * Define as bits because they're used for vendor_specific bitmask in
 * the struct resctrl_test.
 */
#define ARCH_INTEL     1
#define ARCH_AMD       2

#define END_OF_TESTS	1

#define BENCHMARK_ARGS		64

#define DEFAULT_SPAN		(250 * MB)

/*
 * user_params:		User supplied parameters
 * @cpu:		CPU number to which the benchmark will be bound to
 * @bits:		Number of bits used for cache allocation size
 * @benchmark_cmd:	Benchmark command to run during (some of the) tests
 */
struct user_params {
	int cpu;
	int bits;
	const char *benchmark_cmd[BENCHMARK_ARGS];
};

/*
 * resctrl_test:	resctrl test definition
 * @name:		Test name
 * @group:		Test group - a common name for tests that share some characteristic
 *			(e.g., L3 CAT test belongs to the CAT group). Can be NULL
 * @resource:		Resource to test (e.g., MB, L3, L2, etc.)
 * @vendor_specific:	Bitmask for vendor-specific tests (can be 0 for universal tests)
 * @disabled:		Test is disabled
 * @feature_check:	Callback to check required resctrl features
 * @run_test:		Callback to run the test
 * @cleanup:		Callback to cleanup after the test
 */
struct resctrl_test {
	const char	*name;
	const char	*group;
	const char	*resource;
	unsigned int	vendor_specific;
	bool		disabled;
	bool		(*feature_check)(const struct resctrl_test *test);
	int		(*run_test)(const struct resctrl_test *test,
				    const struct user_params *uparams);
	void		(*cleanup)(void);
};

/*
 * resctrl_val_param:	resctrl test parameters
 * @ctrlgrp:		Name of the control monitor group (con_mon grp)
 * @mongrp:		Name of the monitor group (mon grp)
 * @filename:		Name of file to which the o/p should be written
 * @init:		Callback function to initialize test environment
 * @setup:		Callback function to setup per test run environment
 * @measure:		Callback that performs the measurement (a single test)
 */
struct resctrl_val_param {
	const char	*ctrlgrp;
	const char	*mongrp;
	char		filename[64];
	unsigned long	mask;
	int		num_of_runs;
	int		(*init)(const struct resctrl_val_param *param,
				int domain_id);
	int		(*setup)(const struct resctrl_test *test,
				 const struct user_params *uparams,
				 struct resctrl_val_param *param);
	int		(*measure)(const struct user_params *uparams,
				   struct resctrl_val_param *param,
				   pid_t bm_pid);
};

struct perf_event_read {
	__u64 nr;			/* The number of events */
	struct {
		__u64 value;		/* The value of the event */
	} values[2];
};

/*
 * Memory location that consumes values compiler must not optimize away.
 * Volatile ensures writes to this location cannot be optimized away by
 * compiler.
 */
extern volatile int *value_sink;

extern char llc_occup_path[1024];

int get_vendor(void);
bool check_resctrlfs_support(void);
int filter_dmesg(void);
int get_domain_id(const char *resource, int cpu_no, int *domain_id);
int mount_resctrlfs(void);
int umount_resctrlfs(void);
const char *get_bw_report_type(const char *bw_report);
bool resctrl_resource_exists(const char *resource);
bool resctrl_mon_feature_exists(const char *resource, const char *feature);
bool resource_info_file_exists(const char *resource, const char *file);
bool test_resource_feature_check(const struct resctrl_test *test);
char *fgrep(FILE *inf, const char *str);
int taskset_benchmark(pid_t bm_pid, int cpu_no, cpu_set_t *old_affinity);
int taskset_restore(pid_t bm_pid, cpu_set_t *old_affinity);
int write_schemata(const char *ctrlgrp, char *schemata, int cpu_no,
		   const char *resource);
int write_bm_pid_to_resctrl(pid_t bm_pid, const char *ctrlgrp, const char *mongrp);
int perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu,
		    int group_fd, unsigned long flags);
unsigned char *alloc_buffer(size_t buf_size, int memflush);
void mem_flush(unsigned char *buf, size_t buf_size);
void fill_cache_read(unsigned char *buf, size_t buf_size, bool once);
int run_fill_buf(size_t buf_size, int memflush, int op, bool once);
int initialize_mem_bw_imc(void);
int measure_mem_bw(const struct user_params *uparams,
		   struct resctrl_val_param *param, pid_t bm_pid,
		   const char *bw_report);
void initialize_mem_bw_resctrl(const struct resctrl_val_param *param,
			       int domain_id);
int resctrl_val(const struct resctrl_test *test,
		const struct user_params *uparams,
		const char * const *benchmark_cmd,
		struct resctrl_val_param *param);
unsigned long create_bit_mask(unsigned int start, unsigned int len);
unsigned int count_contiguous_bits(unsigned long val, unsigned int *start);
int get_full_cbm(const char *cache_type, unsigned long *mask);
int get_mask_no_shareable(const char *cache_type, unsigned long *mask);
int get_cache_size(int cpu_no, const char *cache_type, unsigned long *cache_size);
int resource_info_unsigned_get(const char *resource, const char *filename, unsigned int *val);
void ctrlc_handler(int signum, siginfo_t *info, void *ptr);
int signal_handler_register(const struct resctrl_test *test);
void signal_handler_unregister(void);
unsigned int count_bits(unsigned long n);

void perf_event_attr_initialize(struct perf_event_attr *pea, __u64 config);
void perf_event_initialize_read_format(struct perf_event_read *pe_read);
int perf_open(struct perf_event_attr *pea, pid_t pid, int cpu_no);
int perf_event_reset_enable(int pe_fd);
int perf_event_measure(int pe_fd, struct perf_event_read *pe_read,
		       const char *filename, pid_t bm_pid);
int measure_llc_resctrl(const char *filename, pid_t bm_pid);
void show_cache_info(int no_of_bits, __u64 avg_llc_val, size_t cache_span, bool lines);

/*
 * cache_portion_size - Calculate the size of a cache portion
 * @cache_size:		Total cache size in bytes
 * @portion_mask:	Cache portion mask
 * @full_cache_mask:	Full Cache Bit Mask (CBM) for the cache
 *
 * Return: The size of the cache portion in bytes.
 */
static inline unsigned long cache_portion_size(unsigned long cache_size,
					       unsigned long portion_mask,
					       unsigned long full_cache_mask)
{
	unsigned int bits = count_bits(full_cache_mask);

	/*
	 * With no bits the full CBM, assume cache cannot be split into
	 * smaller portions. To avoid divide by zero, return cache_size.
	 */
	if (!bits)
		return cache_size;

	return cache_size * count_bits(portion_mask) / bits;
}

extern struct resctrl_test mbm_test;
extern struct resctrl_test mba_test;
extern struct resctrl_test cmt_test;
extern struct resctrl_test l3_cat_test;
extern struct resctrl_test l3_noncont_cat_test;
extern struct resctrl_test l2_noncont_cat_test;

#endif /* RESCTRL_H */
