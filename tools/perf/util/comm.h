#ifndef __PERF_COMM_H
#define __PERF_COMM_H

#include "../perf.h"
#include <linux/rbtree.h>
#include <linux/list.h>

struct comm_str;

struct comm {
	struct comm_str *comm_str;
	u64 start;
	struct list_head list;
};

void comm__free(struct comm *comm);
struct comm *comm__new(const char *str, u64 timestamp);
const char *comm__str(const struct comm *comm);

#endif  /* __PERF_COMM_H */
