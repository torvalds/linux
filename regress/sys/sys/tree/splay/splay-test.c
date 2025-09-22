/*	$OpenBSD: splay-test.c,v 1.4 2008/04/13 00:22:17 djm Exp $	*/
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
#include <sys/tree.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <stdlib.h>

struct node {
	SPLAY_ENTRY(node) node;
	int key;
};

SPLAY_HEAD(tree, node) root;

static int
compare(struct node *a, struct node *b)
{
	if (a->key < b->key) return (-1);
	else if (a->key > b->key) return (1);
	return (0);
}

SPLAY_PROTOTYPE(tree, node, node, compare);

SPLAY_GENERATE(tree, node, node, compare);

#define ITER 150
#define MIN 5
#define MAX 5000

int
main(int argc, char **argv)
{
	struct node *tmp, *ins;
	int i, max, min;

	SPLAY_INIT(&root);

	for (i = 0; i < ITER; i++) {
		tmp = malloc(sizeof(struct node));
		if (tmp == NULL) err(1, "malloc");
		do {
			tmp->key = arc4random_uniform(MAX-MIN);
			tmp->key += MIN;
		} while (SPLAY_FIND(tree, &root, tmp) != NULL);
		if (i == 0)
			max = min = tmp->key;
		else {
			if (tmp->key > max)
				max = tmp->key;
			if (tmp->key < min)
				min = tmp->key;
		}
		if (SPLAY_INSERT(tree, &root, tmp) != NULL)
			errx(1, "SPLAY_INSERT failed");
	}

	ins = SPLAY_MIN(tree, &root);
	if (ins->key != min)
		errx(1, "min does not match");
	tmp = ins;
	ins = SPLAY_MAX(tree, &root);
	if (ins->key != max)
		errx(1, "max does not match");

	if (SPLAY_REMOVE(tree, &root, tmp) != tmp)
		errx(1, "SPLAY_REMOVE failed");

	for (i = 0; i < ITER - 1; i++) {
		tmp = SPLAY_ROOT(&root);
		if (tmp == NULL)
			errx(1, "SPLAY_ROOT error");
		if (SPLAY_REMOVE(tree, &root, tmp) != tmp)
			errx(1, "SPLAY_REMOVE error");
		free(tmp);
	}

	exit(0);
}
