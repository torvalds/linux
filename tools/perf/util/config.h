#ifndef __PERF_CONFIG_H
#define __PERF_CONFIG_H

#include <stdbool.h>
#include <linux/list.h>

struct perf_config_item {
	char *name;
	char *value;
	struct list_head node;
};

struct perf_config_section {
	char *name;
	struct list_head items;
	struct list_head node;
};

struct perf_config_set {
	struct list_head sections;
};

struct perf_config_set *perf_config_set__new(void);
void perf_config_set__delete(struct perf_config_set *set);

#endif /* __PERF_CONFIG_H */
