/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2005 Colin Percival
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions 
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct port;

typedef union {
	char * name;
	struct port * p;
} DEP;

typedef struct port {
	char * pkgname;
	char * portdir;
	char * prefix;
	char * comment;
	char * pkgdescr;
	char * maintainer;
	char * categories;
	size_t n_edep;
	DEP * edep;
	size_t n_pdep;
	DEP * pdep;
	size_t n_fdep;
	DEP * fdep;
	size_t n_bdep;
	DEP * bdep;
	size_t n_rdep;
	DEP * rdep;
	char * www;
	int recursed;
} PORT;

static void usage(void);
static char * strdup2(const char *str);
static DEP * makelist(char * str, size_t * n);
static PORT * portify(char * line);
static int portcompare(char * a, char * b);
static void heapifyports(PORT **pp, size_t size, size_t pos);
static PORT * findport(PORT ** pp, size_t st, size_t en, char * name, char * from);
static void translateport(PORT ** pp, size_t pplen, PORT * p);
static DEP * recurse_one(DEP * d, size_t * nd);
static void recurse(PORT * p);
static void heapifypkgs(DEP * d, size_t size, size_t pos);
static void sortpkgs(DEP * d, size_t nd);
static void printport(PORT * p);

static void
usage(void)
{

	fprintf(stderr, "usage: make_index file\n");
	exit(1);
	/* NOTREACHED */
}

static char *
strdup2(const char *str)
{
	char * r;

	r = strdup(str);
	if (r == NULL)
		err(1, "strdup");
	return r;
}

/* Take a space-separated list and return an array of (char *) */
static DEP *
makelist(char * str, size_t * n)
{
	DEP * d;
	size_t i;

	/* No depends at all? */
	if (str[0] == 0) {
		*n = 0;
		return NULL;
	}

	/* Count the number of fields */
	*n = 1;
	for (i = 0; str[i] != 0; i++)
		if (str[i] == ' ')
			(*n)++;

	/* Allocate and fill an array */
	d = malloc(*n * sizeof(DEP));
	if (d == NULL)
		err(1, "malloc(DEP)");
	for (i = 0; i < *n; i++) {
		d[i].name = strdup2(strsep(&str, " "));

		/* Strip trailing slashes */
		if (d[i].name[strlen(d[i].name) - 1] == '/')
			d[i].name[strlen(d[i].name) - 1] = 0;
	}

	return d;
}

/* Take a port's describe line and split it into fields */
static PORT *
portify(char * line)
{
	PORT * p;
	size_t i, n;

	/* Verify that line has the right number of fields */
	for (n = i = 0; line[i] != 0; i++)
		if (line[i] == '|')
			n++;
	if (n != 12)
		errx(1, "Port describe line is corrupt:\n%s\n", line);

	p = malloc(sizeof(PORT));
	if (p == NULL)
		err(1, "malloc(PORT)");

	p->pkgname = strdup2(strsep(&line, "|"));
	p->portdir = strdup2(strsep(&line, "|"));
	p->prefix = strdup2(strsep(&line, "|"));
	p->comment = strdup2(strsep(&line, "|"));
	p->pkgdescr = strdup2(strsep(&line, "|"));
	p->maintainer = strdup2(strsep(&line, "|"));
	p->categories = strdup2(strsep(&line, "|"));
	p->edep = makelist(strsep(&line, "|"), &p->n_edep);
	p->pdep = makelist(strsep(&line, "|"), &p->n_pdep);
	p->fdep = makelist(strsep(&line, "|"), &p->n_fdep);
	p->bdep = makelist(strsep(&line, "|"), &p->n_bdep);
	p->rdep = makelist(strsep(&line, "|"), &p->n_rdep);
	p->www = strdup2(strsep(&line, "|"));

	p->recursed = 0;

	/*
	 * line will now be equal to NULL -- we counted the field
	 * separators at the top of the function.
	 */

	return p;
}

/* Returns -1, 0, or 1 based on a comparison of the portdir strings */
static int
portcompare(char * a, char * b)
{
	size_t i;

	/* Find first non-matching position */
	for (i = 0; ; i++) {
		if (a[i] != b[i])
			break;
		if (a[i] == 0)			/* End of strings */
			return 0;
	}

	/* One string is a prefix of the other */
	if (a[i] == 0)
		return -1;
	if (b[i] == 0)
		return 1;

	/* One string has a category which is a prefix of the other */
	if (a[i] == '/')
		return -1;
	if (b[i] == '/')
		return 1;

	/* The two strings are simply different */
	if (a[i] < b[i])
		return -1;
	else
		return 1;
}

/* Heapify (PORT *) number pos in a pseudo-heap pp[0]..pp[size - 1] */
static void
heapifyports(PORT **pp, size_t size, size_t pos)
{
	size_t i = pos;
	PORT * tmp;

top:
	/* Find the largest value out of {pos, 2*pos+1, 2*pos+2} */
	if ((2 * pos + 1 < size) &&
	    (portcompare(pp[i]->portdir, pp[2 * pos + 1]->portdir) < 0))
		i = 2 * pos + 1;
	if ((2 * pos + 2 < size) &&
	    (portcompare(pp[i]->portdir, pp[2 * pos + 2]->portdir) < 0))
		i = 2 * pos + 2;

	/* If necessary, swap elements and iterate down the tree. */
	if (i != pos) {
		tmp = pp[pos];
		pp[pos] = pp[i];
		pp[i] = tmp;
		pos = i;
		goto top;
	}
}

/* Translate a port directory name into a (PORT *), and free the name */
static PORT *
findport(PORT ** pp, size_t st, size_t en, char * name, char * from)
{
	size_t mid;
	int r;

	if (st == en)
		errx(1, "%s: no entry for %s", from, name);

	mid = (st + en) / 2;
	r = portcompare(pp[mid]->portdir, name);

	if (r == 0) {
		free(name);
		return pp[mid];
	} else if (r < 0)
		return findport(pp, mid + 1, en, name, from);
	else
		return findport(pp, st, mid, name, from);
}

/* Translate all depends from names into PORT *s */
static void
translateport(PORT ** pp, size_t pplen, PORT * p)
{
	size_t i;

	for (i = 0; i < p->n_edep; i++)
		p->edep[i].p = findport(pp, 0, pplen, p->edep[i].name, p->portdir);
	for (i = 0; i < p->n_pdep; i++)
		p->pdep[i].p = findport(pp, 0, pplen, p->pdep[i].name, p->portdir);
	for (i = 0; i < p->n_fdep; i++)
		p->fdep[i].p = findport(pp, 0, pplen, p->fdep[i].name, p->portdir);
	for (i = 0; i < p->n_bdep; i++)
		p->bdep[i].p = findport(pp, 0, pplen, p->bdep[i].name, p->portdir);
	for (i = 0; i < p->n_rdep; i++)
		p->rdep[i].p = findport(pp, 0, pplen, p->rdep[i].name, p->portdir);
}

/* Recurse on one specific depends list */
static DEP *
recurse_one(DEP * d, size_t * nd)
{
	size_t i, j, k, n, N;

	N = n = *nd;
	for (i = 0; i < n; i++) {
		recurse(d[i].p);
		for (j = 0; j < d[i].p->n_rdep; j++) {
			for (k = 0; k < N; k++) {
				if (d[i].p->rdep[j].p == d[k].p)
					break;
			}
			if (k == N) {
				N++;
				if (N >= *nd) {
					*nd += *nd;
					d = realloc(d, *nd * sizeof(DEP));
					if (d == NULL)
						err(1, "realloc(d)");
				}
				d[k].p = d[i].p->rdep[j].p;
			}
		}
	}
	*nd = N;

	return d;
}

/* Recurse on the depends lists */
static void
recurse(PORT * p)
{
	switch (p->recursed) {
	case 0:
		/* First time we've seen this port */
		p->recursed = 1;
		break;
	case 1:
		/* We're in the middle of recursing this port */
		errx(1, "Circular dependency loop found: %s"
		    " depends upon itself.\n", p->pkgname);
	case 2:
		/* This port has already been recursed */
		return;
	}

	p->edep = recurse_one(p->edep, &p->n_edep);
	p->pdep = recurse_one(p->pdep, &p->n_pdep);
	p->fdep = recurse_one(p->fdep, &p->n_fdep);
	p->bdep = recurse_one(p->bdep, &p->n_bdep);
	p->rdep = recurse_one(p->rdep, &p->n_rdep);

	/* Finished recursing on this port */
	p->recursed = 2;
}

/* Heapify an element in a package list */
static void
heapifypkgs(DEP * d, size_t size, size_t pos)
{
	size_t i = pos;
	PORT * tmp;

top:
	/* Find the largest value out of {pos, 2*pos+1, 2*pos+2} */
	if ((2 * pos + 1 < size) &&
	    (strcmp(d[i].p->pkgname, d[2 * pos + 1].p->pkgname) < 0))
		i = 2 * pos + 1;
	if ((2 * pos + 2 < size) &&
	    (strcmp(d[i].p->pkgname, d[2 * pos + 2].p->pkgname) < 0))
		i = 2 * pos + 2;

	/* If necessary, swap elements and iterate down the tree. */
	if (i != pos) {
		tmp = d[pos].p;
		d[pos].p = d[i].p;
		d[i].p = tmp;
		pos = i;
		goto top;
	}
}

/* Sort a list of dependent packages in alphabetical order */
static void
sortpkgs(DEP * d, size_t nd)
{
	size_t i;
	PORT * tmp;

	if (nd == 0)
		return;

	for (i = nd; i > 0; i--)
		heapifypkgs(d, nd, i - 1);	/* Build a heap */
	for (i = nd - 1; i > 0; i--) {
		tmp = d[0].p;			/* Extract elements */
		d[0].p = d[i].p;
		d[i].p = tmp;
		heapifypkgs(d, i, 0);		/* And re-heapify */
	}
}

/* Output an index line for the given port. */
static void
printport(PORT * p)
{
	size_t i;

	sortpkgs(p->edep, p->n_edep);
	sortpkgs(p->pdep, p->n_pdep);
	sortpkgs(p->fdep, p->n_fdep);
	sortpkgs(p->bdep, p->n_bdep);
	sortpkgs(p->rdep, p->n_rdep);

	printf("%s|%s|%s|%s|%s|%s|%s|",
	    p->pkgname, p->portdir, p->prefix, p->comment, p->pkgdescr, 
	    p->maintainer, p->categories);
	for (i = 0; i < p->n_bdep; i++)
		printf("%s%s", i ? " " : "", p->bdep[i].p->pkgname);
	printf("|");
	for (i = 0; i < p->n_rdep; i++)
		printf("%s%s", i ? " " : "", p->rdep[i].p->pkgname);
	printf("|");
	printf("%s|", p->www);
	for (i = 0; i < p->n_edep; i++)
		printf("%s%s", i ? " " : "", p->edep[i].p->pkgname);
	printf("|");
	for (i = 0; i < p->n_pdep; i++)
		printf("%s%s", i ? " " : "", p->pdep[i].p->pkgname);
	printf("|");
	for (i = 0; i < p->n_fdep; i++)
		printf("%s%s", i ? " " : "", p->fdep[i].p->pkgname);
	printf("\n");
}

/*
 * Algorithm:
 * 1. Suck in all the data, splitting into fields.
 * 1a. If there are no ports, there is no INDEX.
 * 2. Sort the ports according to port directory.
 * 3. Using a binary search, translate each dependency from a
 * port directory name into a pointer to a port.
 * 4. Recursively follow dependencies, expanding the lists of
 * pointers as needed (using realloc).
 * 5. Iterate through the ports, printing them out (remembering
 * to list the dependent ports in alphabetical order).
 */

int
main(int argc, char *argv[])
{
	FILE * f;
	char * line;
	size_t linelen;
	PORT ** pp;	/* Array of pointers to PORTs */
	PORT * tmp;
	size_t pplen;	/* Allocated size of array */
	size_t i;

	if (argc != 2)
		usage();
	if ((f = fopen(argv[1], "r")) == NULL)
		err(1, "fopen(%s)", argv[1]);

	pplen = 1024;
	if ((pp = malloc(pplen * sizeof(PORT *))) == NULL)
		err(1, "malloc(pp)");

	/*
	 * 1. Suck in all the data, splitting into fields.
	 */
	for(i = 0; (line = fgetln(f, &linelen)) != NULL; i++) {
		if (line[linelen - 1] != '\n')
			errx(1, "Unterminated line encountered");
		line[linelen - 1] = 0;

		/* Enlarge array if needed */
		if (i >= pplen) {
			pplen *= 2;
			if ((pp = realloc(pp, pplen * sizeof(PORT *))) == NULL)
				err(1, "realloc(pp)");
		}

		pp[i] = portify(line);
	}
	/* Reallocate to the correct size */
	pplen = i;
	if ((pp = realloc(pp, pplen * sizeof(PORT *))) == NULL)
		err(1, "realloc(pp)");

	/* Make sure we actually reached the EOF */
	if (!feof(f))
		err(1, "fgetln(%s)", argv[1]);
	/* Close the describes file */
	if (fclose(f) != 0)
		err(1, "fclose(%s)", argv[1]);

	/*
	 * 1a. If there are no ports, there is no INDEX.
	 */
	if (pplen == 0)
		return 0;

	/*
	 * 2. Sort the ports according to port directory.
	 */
	for (i = pplen; i > 0; i--)
		heapifyports(pp, pplen, i - 1);	/* Build a heap */
	for (i = pplen - 1; i > 0; i--) {
		tmp = pp[0];			/* Extract elements */
		pp[0] = pp[i];
		pp[i] = tmp;
		heapifyports(pp, i, 0);		/* And re-heapify */
	}

	/*
	 * 3. Using a binary search, translate each dependency from a
	 * port directory name into a pointer to a port.
	 */
	for (i = 0; i < pplen; i++)
		translateport(pp, pplen, pp[i]);

	/*
	 * 4. Recursively follow dependencies, expanding the lists of
	 * pointers as needed (using realloc).
	 */
	for (i = 0; i < pplen; i++)
		recurse(pp[i]);

	/*
	 * 5. Iterate through the ports, printing them out (remembering
	 * to list the dependent ports in alphabetical order).
	 */
	for (i = 0; i < pplen; i++)
		printport(pp[i]);

	return 0;
}
