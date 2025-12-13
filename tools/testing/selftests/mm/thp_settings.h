/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __THP_SETTINGS_H__
#define __THP_SETTINGS_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum thp_enabled {
	THP_NEVER,
	THP_ALWAYS,
	THP_INHERIT,
	THP_MADVISE,
};

enum thp_defrag {
	THP_DEFRAG_ALWAYS,
	THP_DEFRAG_DEFER,
	THP_DEFRAG_DEFER_MADVISE,
	THP_DEFRAG_MADVISE,
	THP_DEFRAG_NEVER,
};

enum shmem_enabled {
	SHMEM_NEVER,
	SHMEM_ALWAYS,
	SHMEM_WITHIN_SIZE,
	SHMEM_ADVISE,
	SHMEM_INHERIT,
	SHMEM_DENY,
	SHMEM_FORCE,
};

#define NR_ORDERS 20

struct hugepages_settings {
	enum thp_enabled enabled;
};

struct khugepaged_settings {
	bool defrag;
	unsigned int alloc_sleep_millisecs;
	unsigned int scan_sleep_millisecs;
	unsigned int max_ptes_none;
	unsigned int max_ptes_swap;
	unsigned int max_ptes_shared;
	unsigned long pages_to_scan;
};

struct shmem_hugepages_settings {
	enum shmem_enabled enabled;
};

struct thp_settings {
	enum thp_enabled thp_enabled;
	enum thp_defrag thp_defrag;
	enum shmem_enabled shmem_enabled;
	bool use_zero_page;
	struct khugepaged_settings khugepaged;
	unsigned long read_ahead_kb;
	struct hugepages_settings hugepages[NR_ORDERS];
	struct shmem_hugepages_settings shmem_hugepages[NR_ORDERS];
};

int read_file(const char *path, char *buf, size_t buflen);
int write_file(const char *path, const char *buf, size_t buflen);
unsigned long read_num(const char *path);
void write_num(const char *path, unsigned long num);

int thp_read_string(const char *name, const char * const strings[]);
void thp_write_string(const char *name, const char *val);
unsigned long thp_read_num(const char *name);
void thp_write_num(const char *name, unsigned long num);

void thp_write_settings(struct thp_settings *settings);
void thp_read_settings(struct thp_settings *settings);
struct thp_settings *thp_current_settings(void);
void thp_push_settings(struct thp_settings *settings);
void thp_pop_settings(void);
void thp_restore_settings(void);
void thp_save_settings(void);

void thp_set_read_ahead_path(char *path);
unsigned long thp_supported_orders(void);
unsigned long thp_shmem_supported_orders(void);

bool thp_available(void);
bool thp_is_enabled(void);

#endif /* __THP_SETTINGS_H__ */
