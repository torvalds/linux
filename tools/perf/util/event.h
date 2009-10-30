#ifndef __PERF_RECORD_H
#define __PERF_RECORD_H

#include "../perf.h"
#include "util.h"
#include <linux/list.h>
#include <linux/rbtree.h>

/*
 * PERF_SAMPLE_IP | PERF_SAMPLE_TID | *
 */
struct ip_event {
	struct perf_event_header header;
	u64 ip;
	u32 pid, tid;
	unsigned char __more_data[];
};

struct mmap_event {
	struct perf_event_header header;
	u32 pid, tid;
	u64 start;
	u64 len;
	u64 pgoff;
	char filename[PATH_MAX];
};

struct comm_event {
	struct perf_event_header header;
	u32 pid, tid;
	char comm[16];
};

struct fork_event {
	struct perf_event_header header;
	u32 pid, ppid;
	u32 tid, ptid;
	u64 time;
};

struct lost_event {
	struct perf_event_header header;
	u64 id;
	u64 lost;
};

/*
 * PERF_FORMAT_ENABLED | PERF_FORMAT_RUNNING | PERF_FORMAT_ID
 */
struct read_event {
	struct perf_event_header header;
	u32 pid, tid;
	u64 value;
	u64 time_enabled;
	u64 time_running;
	u64 id;
};

struct sample_event{
	struct perf_event_header        header;
	u64 array[];
};


typedef union event_union {
	struct perf_event_header	header;
	struct ip_event			ip;
	struct mmap_event		mmap;
	struct comm_event		comm;
	struct fork_event		fork;
	struct lost_event		lost;
	struct read_event		read;
	struct sample_event		sample;
} event_t;

struct map {
	union {
		struct rb_node	rb_node;
		struct list_head node;
	};
	u64			start;
	u64			end;
	u64			pgoff;
	u64			(*map_ip)(struct map *, u64);
	u64			(*unmap_ip)(struct map *, u64);
	struct dso		*dso;
};

static inline u64 map__map_ip(struct map *map, u64 ip)
{
	return ip - map->start + map->pgoff;
}

static inline u64 map__unmap_ip(struct map *map, u64 ip)
{
	return ip + map->start - map->pgoff;
}

static inline u64 identity__map_ip(struct map *map __used, u64 ip)
{
	return ip;
}

struct symbol;

typedef int (*symbol_filter_t)(struct map *map, struct symbol *sym);

void map__init(struct map *self, u64 start, u64 end, u64 pgoff,
	       struct dso *dso);
struct map *map__new(struct mmap_event *event, char *cwd, int cwdlen,
		     unsigned int sym_priv_size);
struct map *map__clone(struct map *self);
int map__overlap(struct map *l, struct map *r);
size_t map__fprintf(struct map *self, FILE *fp);
struct symbol *map__find_symbol(struct map *self, u64 ip, symbol_filter_t filter);

int event__synthesize_thread(pid_t pid, int (*process)(event_t *event));
void event__synthesize_threads(int (*process)(event_t *event));

#endif /* __PERF_RECORD_H */
