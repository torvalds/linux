#ifndef __PROBE_FILE_H
#define __PROBE_FILE_H

#include "strlist.h"
#include "strfilter.h"
#include "probe-event.h"

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

#define PF_FL_UPROBE	1
#define PF_FL_RW	2

int probe_file__open(int flag);
int probe_file__open_both(int *kfd, int *ufd, int flag);
struct strlist *probe_file__get_namelist(int fd);
struct strlist *probe_file__get_rawlist(int fd);
int probe_file__add_event(int fd, struct probe_trace_event *tev);
int probe_file__del_events(int fd, struct strfilter *filter);
int probe_file__get_events(int fd, struct strfilter *filter,
				  struct strlist *plist);
int probe_file__del_strlist(int fd, struct strlist *namelist);

struct probe_cache *probe_cache__new(const char *target);
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
#endif
