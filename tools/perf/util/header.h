#ifndef _PERF_HEADER_H
#define _PERF_HEADER_H

#include "../../../include/linux/perf_counter.h"
#include <sys/types.h>
#include "types.h"

struct perf_header_attr {
	struct perf_counter_attr attr;
	int ids, size;
	u64 *id;
	off_t id_offset;
};

struct perf_header {
	int frozen;
	int attrs, size;
	struct perf_header_attr **attr;
	s64 attr_offset;
	u64 data_offset;
	u64 data_size;
};

struct perf_header *perf_header__read(int fd);
void perf_header__write(struct perf_header *self, int fd);

void perf_header__add_attr(struct perf_header *self,
			   struct perf_header_attr *attr);

struct perf_header_attr *
perf_header_attr__new(struct perf_counter_attr *attr);
void perf_header_attr__add_id(struct perf_header_attr *self, u64 id);


struct perf_header *perf_header__new(void);

#endif /* _PERF_HEADER_H */
