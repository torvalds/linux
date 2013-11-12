#ifndef __PMU_H
#define __PMU_H

#include <linux/bitops.h>
#include <linux/perf_event.h>
#include <stdbool.h>

enum {
	PERF_PMU_FORMAT_VALUE_CONFIG,
	PERF_PMU_FORMAT_VALUE_CONFIG1,
	PERF_PMU_FORMAT_VALUE_CONFIG2,
};

#define PERF_PMU_FORMAT_BITS 64

struct perf_pmu {
	char *name;
	__u32 type;
	struct cpu_map *cpus;
	struct list_head format;
	struct list_head aliases;
	struct list_head list;
};

struct perf_pmu *perf_pmu__find(const char *name);
int perf_pmu__config(struct perf_pmu *pmu, struct perf_event_attr *attr,
		     struct list_head *head_terms);
int perf_pmu__config_terms(struct list_head *formats,
			   struct perf_event_attr *attr,
			   struct list_head *head_terms);
int perf_pmu__check_alias(struct perf_pmu *pmu, struct list_head *head_terms,
			  char **unit, double *scale);
struct list_head *perf_pmu__alias(struct perf_pmu *pmu,
				  struct list_head *head_terms);
int perf_pmu_wrap(void);
void perf_pmu_error(struct list_head *list, char *name, char const *msg);

int perf_pmu__new_format(struct list_head *list, char *name,
			 int config, unsigned long *bits);
void perf_pmu__set_format(unsigned long *bits, long from, long to);
int perf_pmu__format_parse(char *dir, struct list_head *head);

struct perf_pmu *perf_pmu__scan(struct perf_pmu *pmu);

void print_pmu_events(const char *event_glob, bool name_only);
bool pmu_have_event(const char *pname, const char *name);

int perf_pmu__test(void);
#endif /* __PMU_H */
