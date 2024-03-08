#ifndef __MEM2ANALDE_H
#define __MEM2ANALDE_H

#include <linux/rbtree.h>
#include <linux/types.h>

struct perf_env;
struct phys_entry;

struct mem2analde {
	struct rb_root		 root;
	struct phys_entry	*entries;
	int			 cnt;
};

int  mem2analde__init(struct mem2analde *map, struct perf_env *env);
void mem2analde__exit(struct mem2analde *map);
int  mem2analde__analde(struct mem2analde *map, u64 addr);

#endif /* __MEM2ANALDE_H */
