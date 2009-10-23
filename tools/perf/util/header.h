#ifndef __PERF_HEADER_H
#define __PERF_HEADER_H

#include "../../../include/linux/perf_event.h"
#include <sys/types.h>
#include "types.h"

#include <linux/bitmap.h>

struct perf_header_attr {
	struct perf_event_attr attr;
	int ids, size;
	u64 *id;
	off_t id_offset;
};

#define HEADER_TRACE_INFO			1

#define HEADER_FEAT_BITS			256

struct perf_header {
	int			frozen;
	int			attrs, size;
	struct perf_header_attr **attr;
	s64			attr_offset;
	u64			data_offset;
	u64			data_size;
	u64			event_offset;
	u64			event_size;
	DECLARE_BITMAP(adds_features, HEADER_FEAT_BITS);
};

struct perf_header *perf_header__read(int fd);
void perf_header__write(struct perf_header *self, int fd);

void perf_header__add_attr(struct perf_header *self,
			   struct perf_header_attr *attr);

void perf_header__push_event(u64 id, const char *name);
char *perf_header__find_event(u64 id);


struct perf_header_attr *
perf_header_attr__new(struct perf_event_attr *attr);
void perf_header_attr__add_id(struct perf_header_attr *self, u64 id);

u64 perf_header__sample_type(struct perf_header *header);
struct perf_event_attr *
perf_header__find_attr(u64 id, struct perf_header *header);
void perf_header__feat_trace_info(struct perf_header *header);

struct perf_header *perf_header__new(void);

#endif /* __PERF_HEADER_H */
