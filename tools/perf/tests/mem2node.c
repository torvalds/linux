// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/bitmap.h>
#include "cpumap.h"
#include "mem2node.h"
#include "tests.h"

static struct node {
	int		 node;
	const char 	*map;
} test_nodes[] = {
	{ .node = 0, .map = "0"     },
	{ .node = 1, .map = "1-2"   },
	{ .node = 3, .map = "5-7,9" },
};

#define T TEST_ASSERT_VAL

static unsigned long *get_bitmap(const char *str, int nbits)
{
	struct cpu_map *map = cpu_map__new(str);
	unsigned long *bm = NULL;
	int i;

	bm = bitmap_alloc(nbits);

	if (map && bm) {
		for (i = 0; i < map->nr; i++) {
			set_bit(map->map[i], bm);
		}
	}

	if (map)
		cpu_map__put(map);
	else
		free(bm);

	return bm && map ? bm : NULL;
}

int test__mem2node(struct test *t __maybe_unused, int subtest __maybe_unused)
{
	struct mem2node map;
	struct memory_node nodes[3];
	struct perf_env env = {
		.memory_nodes    = (struct memory_node *) &nodes[0],
		.nr_memory_nodes = ARRAY_SIZE(nodes),
		.memory_bsize    = 0x100,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(nodes); i++) {
		nodes[i].node = test_nodes[i].node;
		nodes[i].size = 10;

		T("failed: alloc bitmap",
		  (nodes[i].set = get_bitmap(test_nodes[i].map, 10)));
	}

	T("failed: mem2node__init", !mem2node__init(&map, &env));
	T("failed: mem2node__node",  0 == mem2node__node(&map,   0x50));
	T("failed: mem2node__node",  1 == mem2node__node(&map,  0x100));
	T("failed: mem2node__node",  1 == mem2node__node(&map,  0x250));
	T("failed: mem2node__node",  3 == mem2node__node(&map,  0x500));
	T("failed: mem2node__node",  3 == mem2node__node(&map,  0x650));
	T("failed: mem2node__node", -1 == mem2node__node(&map,  0x450));
	T("failed: mem2node__node", -1 == mem2node__node(&map, 0x1050));

	for (i = 0; i < ARRAY_SIZE(nodes); i++)
		free(nodes[i].set);

	mem2node__exit(&map);
	return 0;
}
