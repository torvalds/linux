/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CONFIG_H
#define __PERF_CONFIG_H

#include <stdbool.h>
#include <linux/list.h>

struct perf_config_item {
	char *name;
	char *value;
	bool from_system_config;
	struct list_head node;
};

struct perf_config_section {
	char *name;
	struct list_head items;
	bool from_system_config;
	struct list_head node;
};

struct perf_config_set {
	struct list_head sections;
};

extern const char *config_exclusive_filename;

typedef int (*config_fn_t)(const char *, const char *, void *);

int perf_default_config(const char *, const char *, void *);
int perf_config(config_fn_t fn, void *);
int perf_config_scan(const char *name, const char *fmt, ...) __scanf(2, 3);
const char *perf_config_get(const char *name);
int perf_config_set(struct perf_config_set *set,
		    config_fn_t fn, void *data);
int perf_config_int(int *dest, const char *, const char *);
int perf_config_u8(u8 *dest, const char *name, const char *value);
int perf_config_u64(u64 *dest, const char *, const char *);
int perf_config_bool(const char *, const char *);
int config_error_nonbool(const char *);
const char *perf_etc_perfconfig(void);
const char *perf_home_perfconfig(void);
int perf_config_system(void);
int perf_config_global(void);

struct perf_config_set *perf_config_set__new(void);
struct perf_config_set *perf_config_set__load_file(const char *file);
void perf_config_set__delete(struct perf_config_set *set);
int perf_config_set__collect(struct perf_config_set *set, const char *file_name,
			     const char *var, const char *value);
void perf_config__exit(void);
void perf_config__refresh(void);
int perf_config__set_variable(const char *var, const char *value);

/**
 * perf_config_sections__for_each - iterate thru all the sections
 * @list: list_head instance to iterate
 * @section: struct perf_config_section iterator
 */
#define perf_config_sections__for_each_entry(list, section)	\
        list_for_each_entry(section, list, node)

/**
 * perf_config_items__for_each - iterate thru all the items
 * @list: list_head instance to iterate
 * @item: struct perf_config_item iterator
 */
#define perf_config_items__for_each_entry(list, item)	\
        list_for_each_entry(item, list, node)

/**
 * perf_config_set__for_each - iterate thru all the config section-item pairs
 * @set: evlist instance to iterate
 * @section: struct perf_config_section iterator
 * @item: struct perf_config_item iterator
 */
#define perf_config_set__for_each_entry(set, section, item)			\
	perf_config_sections__for_each_entry(&set->sections, section)		\
	perf_config_items__for_each_entry(&section->items, item)

#endif /* __PERF_CONFIG_H */
