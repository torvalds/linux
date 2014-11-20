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
	bool exec;
};

void comm__free(struct comm *comm);
struct comm *comm__new(const char *str, u64 timestamp, bool exec);
const char *comm__str(const struct comm *comm);
int comm__override(struct comm *comm, const char *str, u64 timestamp,
		   bool exec);

#endif  /* __PERF_COMM_H */
