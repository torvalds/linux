// SPDX-License-Identifier: GPL-2.0

#include <stdint.h>
#include <time.h>
#include <sched.h>

/*
 * '18446744073709551615\0'
 */
#define BUFF_U64_STR_SIZE	24
#define MAX_PATH		1024

#define container_of(ptr, type, member)({			\
	const typeof(((type *)0)->member) *__mptr = (ptr);	\
	(type *)((char *)__mptr - offsetof(type, member)) ; })

extern int config_debug;
void debug_msg(const char *fmt, ...);
void err_msg(const char *fmt, ...);

long parse_seconds_duration(char *val);
void get_duration(time_t start_time, char *output, int output_size);

int parse_cpu_list(char *cpu_list, char **monitored_cpus);
long long get_llong_from_str(char *start);

static inline void
update_min(unsigned long long *a, unsigned long long *b)
{
	if (*a > *b)
		*a = *b;
}

static inline void
update_max(unsigned long long *a, unsigned long long *b)
{
	if (*a < *b)
		*a = *b;
}

static inline void
update_sum(unsigned long long *a, unsigned long long *b)
{
	*a += *b;
}

struct sched_attr {
	uint32_t size;
	uint32_t sched_policy;
	uint64_t sched_flags;
	int32_t sched_nice;
	uint32_t sched_priority;
	uint64_t sched_runtime;
	uint64_t sched_deadline;
	uint64_t sched_period;
};

int parse_prio(char *arg, struct sched_attr *sched_param);
int parse_cpu_set(char *cpu_list, cpu_set_t *set);
int __set_sched_attr(int pid, struct sched_attr *attr);
int set_comm_sched_attr(const char *comm_prefix, struct sched_attr *attr);
int set_comm_cgroup(const char *comm_prefix, const char *cgroup);
int set_pid_cgroup(pid_t pid, const char *cgroup);
int set_cpu_dma_latency(int32_t latency);
int auto_house_keeping(cpu_set_t *monitored_cpus);

#define ns_to_usf(x) (((double)x/1000))
#define ns_to_per(total, part) ((part * 100) / (double)total)
