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

extern const char *config_exclusive_filename;

typedef int (*config_fn_t)(const char *, const char *, void *);
int perf_default_config(const char *, const char *, void *);
int perf_config(config_fn_t fn, void *);
int perf_config_int(const char *, const char *);
u64 perf_config_u64(const char *, const char *);
int perf_config_bool(const char *, const char *);
int config_error_nonbool(const char *);
const char *perf_etc_perfconfig(void);

struct perf_config_set *perf_config_set__new(void);
void perf_config_set__delete(struct perf_config_set *set);

#endif /* __PERF_CONFIG_H */
