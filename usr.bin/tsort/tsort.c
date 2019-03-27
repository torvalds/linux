/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Michael Rendell of Memorial University of Newfoundland.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1989, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static const char sccsid[] = "@(#)tsort.c	8.3 (Berkeley) 5/4/95";
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <ctype.h>
#include <db.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 *  Topological sort.  Input is a list of pairs of strings separated by
 *  white space (spaces, tabs, and/or newlines); strings are written to
 *  standard output in sorted order, one per line.
 *
 *  usage:
 *     tsort [-dlq] [inputfile]
 *  If no input file is specified, standard input is read.
 *
 *  Should be compatible with AT&T tsort HOWEVER the output is not identical
 *  (i.e. for most graphs there is more than one sorted order, and this tsort
 *  usually generates a different one then the AT&T tsort).  Also, cycle
 *  reporting seems to be more accurate in this version (the AT&T tsort
 *  sometimes says a node is in a cycle when it isn't).
 *
 *  Michael Rendell, michael@stretch.cs.mun.ca - Feb 26, '90
 */

#define	NF_MARK		0x1		/* marker for cycle detection */
#define	NF_ACYCLIC	0x2		/* this node is cycle free */
#define	NF_NODEST	0x4		/* Unreachable */


typedef struct node_str NODE;

struct node_str {
	NODE **n_prevp;			/* pointer to previous node's n_next */
	NODE *n_next;			/* next node in graph */
	NODE **n_arcs;			/* array of arcs to other nodes */
	int n_narcs;			/* number of arcs in n_arcs[] */
	int n_arcsize;			/* size of n_arcs[] array */
	int n_refcnt;			/* # of arcs pointing to this node */
	int n_flags;			/* NF_* */
	char n_name[1];			/* name of this node */
};

typedef struct _buf {
	char *b_buf;
	int b_bsize;
} BUF;

static DB *db;
static NODE *graph, **cycle_buf, **longest_cycle;
static int debug, longest, quiet;

static void	 add_arc(char *, char *);
static int	 find_cycle(NODE *, NODE *, int, int);
static NODE	*get_node(char *);
static void	*grow_buf(void *, size_t);
static void	 remove_node(NODE *);
static void	 clear_cycle(void);
static void	 tsort(void);
static void	 usage(void);

int
main(int argc, char *argv[])
{
	BUF *b;
	int c, n;
	FILE *fp;
	int bsize, ch, nused;
	BUF bufs[2];

	fp = NULL;
	while ((ch = getopt(argc, argv, "dlq")) != -1)
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'l':
			longest = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	switch (argc) {
	case 0:
		fp = stdin;
		break;
	case 1:
		if ((fp = fopen(*argv, "r")) == NULL)
			err(1, "%s", *argv);
		break;
	default:
		usage();
	}

	for (b = bufs, n = 2; --n >= 0; b++)
		b->b_buf = grow_buf(NULL, b->b_bsize = 1024);

	/* parse input and build the graph */
	for (n = 0, c = getc(fp);;) {
		while (c != EOF && isspace(c))
			c = getc(fp);
		if (c == EOF)
			break;

		nused = 0;
		b = &bufs[n];
		bsize = b->b_bsize;
		do {
			b->b_buf[nused++] = c;
			if (nused == bsize)
				b->b_buf = grow_buf(b->b_buf, bsize *= 2);
			c = getc(fp);
		} while (c != EOF && !isspace(c));

		b->b_buf[nused] = '\0';
		b->b_bsize = bsize;
		if (n)
			add_arc(bufs[0].b_buf, bufs[1].b_buf);
		n = !n;
	}
	(void)fclose(fp);
	if (n)
		errx(1, "odd data count");

	/* do the sort */
	tsort();
	exit(0);
}

/* double the size of oldbuf and return a pointer to the new buffer. */
static void *
grow_buf(void *bp, size_t size)
{
	if ((bp = realloc(bp, size)) == NULL)
		err(1, NULL);
	return (bp);
}

/*
 * add an arc from node s1 to node s2 in the graph.  If s1 or s2 are not in
 * the graph, then add them.
 */
static void
add_arc(char *s1, char *s2)
{
	NODE *n1;
	NODE *n2;
	int bsize, i;

	n1 = get_node(s1);

	if (!strcmp(s1, s2))
		return;

	n2 = get_node(s2);

	/*
	 * Check if this arc is already here.
	 */
	for (i = 0; i < n1->n_narcs; i++)
		if (n1->n_arcs[i] == n2)
			return;
	/*
	 * Add it.
	 */
	if (n1->n_narcs == n1->n_arcsize) {
		if (!n1->n_arcsize)
			n1->n_arcsize = 10;
		bsize = n1->n_arcsize * sizeof(*n1->n_arcs) * 2;
		n1->n_arcs = grow_buf(n1->n_arcs, bsize);
		n1->n_arcsize = bsize / sizeof(*n1->n_arcs);
	}
	n1->n_arcs[n1->n_narcs++] = n2;
	++n2->n_refcnt;
}

/* Find a node in the graph (insert if not found) and return a pointer to it. */
static NODE *
get_node(char *name)
{
	DBT data, key;
	NODE *n;

	if (db == NULL &&
	    (db = dbopen(NULL, O_RDWR, 0, DB_HASH, NULL)) == NULL)
		err(1, "db: %s", name);

	key.data = name;
	key.size = strlen(name) + 1;

	switch ((*db->get)(db, &key, &data, 0)) {
	case 0:
		bcopy(data.data, &n, sizeof(n));
		return (n);
	case 1:
		break;
	default:
	case -1:
		err(1, "db: %s", name);
	}

	if ((n = malloc(sizeof(NODE) + key.size)) == NULL)
		err(1, NULL);

	n->n_narcs = 0;
	n->n_arcsize = 0;
	n->n_arcs = NULL;
	n->n_refcnt = 0;
	n->n_flags = 0;
	bcopy(name, n->n_name, key.size);

	/* Add to linked list. */
	if ((n->n_next = graph) != NULL)
		graph->n_prevp = &n->n_next;
	n->n_prevp = &graph;
	graph = n;

	/* Add to hash table. */
	data.data = &n;
	data.size = sizeof(n);
	if ((*db->put)(db, &key, &data, 0))
		err(1, "db: %s", name);
	return (n);
}


/*
 * Clear the NODEST flag from all nodes.
 */
static void
clear_cycle(void)
{
	NODE *n;

	for (n = graph; n != NULL; n = n->n_next)
		n->n_flags &= ~NF_NODEST;
}

/* do topological sort on graph */
static void
tsort(void)
{
	NODE *n, *next;
	int cnt, i;

	while (graph != NULL) {
		/*
		 * Keep getting rid of simple cases until there are none left,
		 * if there are any nodes still in the graph, then there is
		 * a cycle in it.
		 */
		do {
			for (cnt = 0, n = graph; n != NULL; n = next) {
				next = n->n_next;
				if (n->n_refcnt == 0) {
					remove_node(n);
					++cnt;
				}
			}
		} while (graph != NULL && cnt);

		if (graph == NULL)
			break;

		if (!cycle_buf) {
			/*
			 * Allocate space for two cycle logs - one to be used
			 * as scratch space, the other to save the longest
			 * cycle.
			 */
			for (cnt = 0, n = graph; n != NULL; n = n->n_next)
				++cnt;
			cycle_buf = malloc(sizeof(NODE *) * cnt);
			longest_cycle = malloc(sizeof(NODE *) * cnt);
			if (cycle_buf == NULL || longest_cycle == NULL)
				err(1, NULL);
		}
		for (n = graph; n != NULL; n = n->n_next)
			if (!(n->n_flags & NF_ACYCLIC)) {
				if ((cnt = find_cycle(n, n, 0, 0))) {
					if (!quiet) {
						warnx("cycle in data");
						for (i = 0; i < cnt; i++)
							warnx("%s",
							    longest_cycle[i]->n_name);
					}
					remove_node(n);
					clear_cycle();
					break;
				} else {
					/* to avoid further checks */
					n->n_flags  |= NF_ACYCLIC;
					clear_cycle();
				}
			}

		if (n == NULL)
			errx(1, "internal error -- could not find cycle");
	}
}

/* print node and remove from graph (does not actually free node) */
static void
remove_node(NODE *n)
{
	NODE **np;
	int i;

	(void)printf("%s\n", n->n_name);
	for (np = n->n_arcs, i = n->n_narcs; --i >= 0; np++)
		--(*np)->n_refcnt;
	n->n_narcs = 0;
	*n->n_prevp = n->n_next;
	if (n->n_next)
		n->n_next->n_prevp = n->n_prevp;
}


/* look for the longest? cycle from node from to node to. */
static int
find_cycle(NODE *from, NODE *to, int longest_len, int depth)
{
	NODE **np;
	int i, len;

	/*
	 * avoid infinite loops and ignore portions of the graph known
	 * to be acyclic
	 */
	if (from->n_flags & (NF_NODEST|NF_MARK|NF_ACYCLIC))
		return (0);
	from->n_flags |= NF_MARK;

	for (np = from->n_arcs, i = from->n_narcs; --i >= 0; np++) {
		cycle_buf[depth] = *np;
		if (*np == to) {
			if (depth + 1 > longest_len) {
				longest_len = depth + 1;
				(void)memcpy((char *)longest_cycle,
				    (char *)cycle_buf,
				    longest_len * sizeof(NODE *));
			}
		} else {
			if ((*np)->n_flags & (NF_MARK|NF_ACYCLIC|NF_NODEST))
				continue;
			len = find_cycle(*np, to, longest_len, depth + 1);

			if (debug)
				(void)printf("%*s %s->%s %d\n", depth, "",
				    from->n_name, to->n_name, len);

			if (len == 0)
				(*np)->n_flags |= NF_NODEST;

			if (len > longest_len)
				longest_len = len;

			if (len > 0 && !longest)
				break;
		}
	}
	from->n_flags &= ~NF_MARK;
	return (longest_len);
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: tsort [-dlq] [file]\n");
	exit(1);
}
