/*	$OpenBSD: rb-linux.c,v 1.2 2024/09/01 06:05:11 anton Exp $	*/
/*
 * Copyright 2002 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <linux/rbtree.h>
#include <linux/container_of.h>

struct keynode {
	struct rb_node node;
	int key;
};

struct rb_root root;

static struct keynode *
rb_find(struct rb_root *head, struct keynode *elm)
{
	struct rb_node *tmp = head->rb_node;

	while (tmp) {
		struct keynode *n = container_of(tmp, struct keynode, node);
		if (elm->key < n->key)
			tmp = tmp->rb_left;
		else if (elm->key > n->key)
			tmp = tmp->rb_right;
		else
			return n;
	}
	return NULL;
}

static struct keynode *
rb_insert(struct rb_root *head, struct keynode *elm)
{
	struct rb_node **tmp;
	struct rb_node *parent = NULL;
	tmp = &(head->rb_node);

	while (*tmp) {
		struct keynode *n = container_of(*tmp, struct keynode, node);
		parent = *tmp;
		if (elm->key < n->key)
			tmp = &((*tmp)->rb_left);
		else if (elm->key > n->key)
			tmp = &((*tmp)->rb_right);
		else
			return n;
	}

	rb_link_node(&elm->node, parent, tmp);
	rb_insert_color(&elm->node, head);

	return NULL;
}

#define TESTS 10
#define ITER 150
#define MIN 5
#define MAX 5000

int
main(int argc, char **argv)
{
	struct keynode *tmp, *ins;
	int i, t, max, min;
	struct rb_node *rb_node;

	root = RB_ROOT;

	for (t = 0; t < 10; t++) {
		for (i = 0; i < ITER; i++) {
			tmp = malloc(sizeof(struct keynode));
			if (tmp == NULL)
				err(1, "malloc");
			do {
				tmp->key = arc4random_uniform(MAX - MIN);
				tmp->key += MIN;
			} while (rb_find(&root, tmp) != NULL);
			if (i == 0)
				max = min = tmp->key;
			else {
				if (tmp->key > max)
					max = tmp->key;
				if (tmp->key < min)
					min = tmp->key;
			}
			if (rb_insert(&root, tmp) != NULL)
				errx(1, "rb_insert failed");
		}

		rb_node = rb_first(&root);
		ins = container_of(rb_node, struct keynode, node);
		if (ins->key != min)
			errx(1, "min does not match");
		tmp = ins;
		rb_node = rb_last(&root);
		ins = container_of(rb_node, struct keynode, node);
		if (ins->key != max)
			errx(1, "max does not match");

		rb_erase(&tmp->node, &root);

		for (i = 0; i < ITER - 1; i++) {
			rb_node = rb_first(&root);
			if (rb_node == NULL)
				errx(1, "rb_first error");
			tmp = container_of(rb_node, struct keynode, node);
			rb_erase(&tmp->node, &root);
			free(tmp);
		}
	}

	exit(0);
}

#undef RB_ROOT
#define RB_ROOT(head)	(head)->rbh_root

RB_GENERATE(linux_root, rb_node, __entry, panic_cmp);

int
panic_cmp(struct rb_node *a, struct rb_node *b)
{
	errx(1, "%s", __func__);
}
