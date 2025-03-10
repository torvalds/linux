// SPDX-License-Identifier: GPL-2.0
/*
 * interval_tree.c: Userspace Interval Tree test-suite
 * Copyright (c) 2025 Wei Yang <richard.weiyang@gmail.com>
 */
#include <linux/math64.h>
#include <linux/kern_levels.h>
#include "shared.h"

#include "../../../lib/interval_tree_test.c"

int usage(void)
{
	fprintf(stderr, "Userland interval tree test cases\n");
	fprintf(stderr, "  -n: Number of nodes in the interval tree\n");
	fprintf(stderr, "  -p: Number of iterations modifying the tree\n");
	fprintf(stderr, "  -q: Number of searches to the interval tree\n");
	fprintf(stderr, "  -s: Number of iterations searching the tree\n");
	fprintf(stderr, "  -a: Searches will iterate all nodes in the tree\n");
	fprintf(stderr, "  -m: Largest value for the interval's endpoint\n");
	fprintf(stderr, "  -r: Random seed\n");
	exit(-1);
}

void interval_tree_tests(void)
{
	interval_tree_test_init();
	interval_tree_test_exit();
}

int main(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "n:p:q:s:am:r:")) != -1) {
		if (opt == 'n')
			nnodes = strtoul(optarg, NULL, 0);
		else if (opt == 'p')
			perf_loops = strtoul(optarg, NULL, 0);
		else if (opt == 'q')
			nsearches = strtoul(optarg, NULL, 0);
		else if (opt == 's')
			search_loops = strtoul(optarg, NULL, 0);
		else if (opt == 'a')
			search_all = true;
		else if (opt == 'm')
			max_endpoint = strtoul(optarg, NULL, 0);
		else if (opt == 'r')
			seed = strtoul(optarg, NULL, 0);
		else
			usage();
	}

	interval_tree_tests();
	return 0;
}
