/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PROBE_FILE_H
#define __PROBE_FILE_H

#include "probe-event.h"

struct strlist;
struct strfilter;

/* Cache of probe definitions */
struct probe_cache_entry {
	struct list_head	node;
	bool			sdt;
	struct perf_probe_event pev;
	char			*spev;
	struct strlist		*tevlist;
};

struct probe_cache {
	int	fd;
	struct list_head entries;
};

enum probe_type {
	PROBE_TYPE_U = 0,
	PROBE_TYPE_S,
	PROBE_TYPE_X,
	PROBE_TYPE_STRING,
	PROBE_TYPE_BITFIELD,
	PROBE_TYPE_END,
};

#define PF_FL_UPROBE	1
#define PF_FL_RW	2
#define for_each_probe_cache_entry(entry, pcache) \
	list_for_each_entry(entry, &pcache->entries, node)

/* probe-file.c depends on libelf */
#ifdef HAVE_LIBELF_SUPPORT
int open_trace_file(const char *trace_file, bool readwrite);
int probe_file__open(int flag);
int probe_file__open_both(int *kfd, int *ufd, int flag);
struct strlist *probe_file__get_namelist(int fd);
struct strlist *probe_file__get_rawlist(int fd);
int probe_file__add_event(int fd, struct probe_trace_event *tev);

int probe_file__get_events(int fd, struct strfilter *filter,
				  struct strlist *plist);
int probe_file__del_strlist(int fd, struct strlist *namelist);

int probe_cache_entry__get_event(struct probe_cache_entry *entry,
				 struct probe_trace_event **tevs);

struct probe_cache *probe_cache__new(const char *target, struct nsinfo *nsi);
int probe_cache__add_entry(struct probe_cache *pcache,
			   struct perf_probe_event *pev,
			   struct probe_trace_event *tevs, int ntevs);
int probe_cache__scan_sdt(struct probe_cache *pcache, const char *pathname);
int probe_cache__commit(struct probe_cache *pcache);
void probe_cache__purge(struct probe_cache *pcache);
void probe_cache__delete(struct probe_cache *pcache);
int probe_cache__filter_purge(struct probe_cache *pcache,
			      struct strfilter *filter);
struct probe_cache_entry *probe_cache__find(struct probe_cache *pcache,
					    struct perf_probe_event *pev);
struct probe_cache_entry *probe_cache__find_by_name(struct probe_cache *pcache,
					const char *group, const char *event);
int probe_cache__show_all_caches(struct strfilter *filter);
bool probe_type_is_available(enum probe_type type);
bool kretprobe_offset_is_supported(void);
bool uprobe_ref_ctr_is_supported(void);
bool user_access_is_supported(void);
bool multiprobe_event_is_supported(void);
bool immediate_value_is_supported(void);
#else	/* ! HAVE_LIBELF_SUPPORT */
static inline struct probe_cache *probe_cache__new(const char *tgt __maybe_unused, struct nsinfo *nsi __maybe_unused)
{
	return NULL;
}
#define probe_cache__delete(pcache) do {} while (0)
#endif
#endif
