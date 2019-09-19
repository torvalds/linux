// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * BTF-to-C dumper test for topological sorting of dependent structs.
 *
 * Copyright (c) 2019 Facebook
 */
/* ----- START-EXPECTED-OUTPUT ----- */
struct s1 {};

struct s3;

struct s4;

struct s2 {
	struct s2 *s2;
	struct s3 *s3;
	struct s4 *s4;
};

struct s3 {
	struct s1 s1;
	struct s2 s2;
};

struct s4 {
	struct s1 s1;
	struct s3 s3;
};

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

struct hlist_node {
	struct hlist_node *next;
	struct hlist_node **pprev;
};

struct hlist_head {
	struct hlist_node *first;
};

struct callback_head {
	struct callback_head *next;
	void (*func)(struct callback_head *);
};

struct root_struct {
	struct s4 s4;
	struct list_head l;
	struct hlist_node n;
	struct hlist_head h;
	struct callback_head cb;
};

/*------ END-EXPECTED-OUTPUT ------ */

int f(struct root_struct *root)
{
	return 0;
}
