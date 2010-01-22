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

struct sample_event {
	struct perf_event_header        header;
	u64 array[];
};

struct sample_data {
	u64 ip;
	u32 pid, tid;
	u64 time;
	u64 addr;
	u64 id;
	u64 stream_id;
	u32 cpu;
	u64 period;
	struct ip_callchain *callchain;
	u32 raw_size;
	void *raw_data;
};

#define BUILD_ID_SIZE 20

struct build_id_event {
	struct perf_event_header header;
	u8			 build_id[ALIGN(BUILD_ID_SIZE, sizeof(u64))];
	char			 filename[];
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

struct events_stats {
	u64 total;
	u64 lost;
};

void event__print_totals(void);

enum map_type {
	MAP__FUNCTION = 0,
	MAP__VARIABLE,
};

#define MAP__NR_TYPES (MAP__VARIABLE + 1)

struct map {
	union {
		struct rb_node	rb_node;
		struct list_head node;
	};
	u64			start;
	u64			end;
	enum map_type		type;
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

void map__init(struct map *self, enum map_type type,
	       u64 start, u64 end, u64 pgoff, struct dso *dso);
struct map *map__new(struct mmap_event *event, enum map_type,
		     char *cwd, int cwdlen);
void map__delete(struct map *self);
struct map *map__clone(struct map *self);
int map__overlap(struct map *l, struct map *r);
size_t map__fprintf(struct map *self, FILE *fp);

struct perf_session;

int map__load(struct map *self, struct perf_session *session,
	      symbol_filter_t filter);
struct symbol *map__find_symbol(struct map *self, struct perf_session *session,
				u64 addr, symbol_filter_t filter);
struct symbol *map__find_symbol_by_name(struct map *self, const char *name,
					struct perf_session *session,
					symbol_filter_t filter);
void map__fixup_start(struct map *self);
void map__fixup_end(struct map *self);

int event__synthesize_thread(pid_t pid,
			     int (*process)(event_t *event,
					    struct perf_session *session),
			     struct perf_session *session);
void event__synthesize_threads(int (*process)(event_t *event,
					      struct perf_session *session),
			       struct perf_session *session);

int event__process_comm(event_t *self, struct perf_session *session);
int event__process_lost(event_t *self, struct perf_session *session);
int event__process_mmap(event_t *self, struct perf_session *session);
int event__process_task(event_t *self, struct perf_session *session);

struct addr_location;
int event__preprocess_sample(const event_t *self, struct perf_session *session,
			     struct addr_location *al, symbol_filter_t filter);
int event__parse_sample(event_t *event, u64 type, struct sample_data *data);

#endif /* __PERF_RECORD_H */
