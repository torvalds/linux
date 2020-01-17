#ifndef __MEM2NODE_H
#define __MEM2NODE_H

#include <linux/rbtree.h>
#include <linux/types.h>

struct perf_env;
struct phys_entry;

struct mem2yesde {
	struct rb_root		 root;
	struct phys_entry	*entries;
	int			 cnt;
};

int  mem2yesde__init(struct mem2yesde *map, struct perf_env *env);
void mem2yesde__exit(struct mem2yesde *map);
int  mem2yesde__yesde(struct mem2yesde *map, u64 addr);

#endif /* __MEM2NODE_H */
