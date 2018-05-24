#ifndef __MEM2NODE_H
#define __MEM2NODE_H

#include <linux/rbtree.h>
#include "env.h"

struct phys_entry;

struct mem2node {
	struct rb_root		 root;
	struct phys_entry	*entries;
	int			 cnt;
};

int  mem2node__init(struct mem2node *map, struct perf_env *env);
void mem2node__exit(struct mem2node *map);
int  mem2node__node(struct mem2node *map, u64 addr);

#endif /* __MEM2NODE_H */
