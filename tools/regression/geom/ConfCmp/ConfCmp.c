/*-
 * Copyright (c) 2002 Poul-Henning Kamp
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <err.h>
#include <bsdxml.h>

FILE *fsubs;

struct node {
	LIST_HEAD(, node)	children;
	LIST_ENTRY(node)	siblings;
	struct node		*parent;
	const char		*name;
	struct sbuf		*cont;
	struct sbuf		*key;
	char			*id;
	char			*ref;
};

struct mytree {
	struct node		*top;
	struct node		*cur;
	int			indent;
	int			ignore;
};

struct ref {
	LIST_ENTRY(ref)		next;
	char 			*k1;
	char			*k2;
};

LIST_HEAD(, ref)		refs = LIST_HEAD_INITIALIZER(refs);

static struct node *
new_node(void)
{
	struct node *np;

	np = calloc(1, sizeof *np);
	np->cont = sbuf_new_auto();
	sbuf_clear(np->cont);
	np->key = sbuf_new_auto();
	sbuf_clear(np->key);
	LIST_INIT(&np->children);
	return (np);
}

static void
indent(int n)
{

	printf("%*.*s", n, n, "");
}

static void
StartElement(void *userData, const char *name, const char **attr)
{
	struct mytree *mt;
	struct node *np;
	int i;

	mt = userData;
	if (!strcmp(name, "FreeBSD")) {
		mt->ignore = 1;
		return;
	}
	mt->ignore = 0;
	mt->indent += 2;
	np = new_node();
	for (i = 0; attr[i]; i += 2) {
		if (!strcmp(attr[i], "id"))
			np->id = strdup(attr[i+1]);
		else if (!strcmp(attr[i], "ref"))
			np->ref = strdup(attr[i+1]);
	}
	np->name = strdup(name);
	sbuf_cat(np->key, name);
	sbuf_cat(np->key, "::");
	np->parent = mt->cur;
	LIST_INSERT_HEAD(&mt->cur->children, np, siblings);
	mt->cur = np;
}

static void
EndElement(void *userData, const char *name __unused)
{
	struct mytree *mt;
	struct node *np;

	mt = userData;
	if (mt->ignore)
		return;

	mt->indent -= 2;
	sbuf_finish(mt->cur->cont);
	LIST_FOREACH(np, &mt->cur->children, siblings) {
		if (strcmp(np->name, "name"))
			continue;
		sbuf_cat(mt->cur->key, sbuf_data(np->cont));
		break;
	}
	sbuf_finish(mt->cur->key);
	mt->cur = mt->cur->parent;
}

static void
CharData(void *userData , const XML_Char *s , int len)
{
	struct mytree *mt;
	const char *b, *e;

	mt = userData;
	if (mt->ignore)
		return;
	b = s;
	e = s + len - 1;
	while (isspace(*b) && b < e)
		b++;
	while (isspace(*e) && e > b)
		e--;
	if (e != b || *b)
		sbuf_bcat(mt->cur->cont, b, e - b + 1);
}

static struct mytree *
dofile(char *filename)
{
	XML_Parser parser;
	struct mytree *mt;
	struct stat st;
	int fd;
	char *p;
	int i;

	parser = XML_ParserCreate(NULL);
	mt = calloc(1, sizeof *mt);
	mt->top = new_node();
	mt->top->name = "(top)";
	mt->top->parent = mt->top;
	mt->cur = mt->top;
	sbuf_finish(mt->top->key);
	sbuf_finish(mt->top->cont);
	XML_SetUserData(parser, mt);
	XML_SetElementHandler(parser, StartElement, EndElement);
	XML_SetCharacterDataHandler(parser, CharData);
	fd = open(filename, O_RDONLY);
	if (fd < 0)
		err(1, filename);
	fstat(fd, &st);
	p = mmap(NULL, st.st_size, PROT_READ, MAP_NOCORE|MAP_PRIVATE, fd, 0);
	i = XML_Parse(parser, p, st.st_size, 1);
	if (i != 1)
		errx(1, "XML_Parse complained -> %d", i);
	munmap(p, st.st_size);
	close(fd);
	XML_ParserFree(parser);
	sbuf_finish(mt->top->cont);
	if (i)
		return (mt);
	else
		return (NULL);
}

static void
print_node(struct node *np)
{
	printf("\"%s\" -- \"%s\" -- \"%s\"", np->name, sbuf_data(np->cont), sbuf_data(np->key));
	if (np->id)
		printf(" id=\"%s\"", np->id);
	if (np->ref)
		printf(" ref=\"%s\"", np->ref);
	printf("\n");
}

static void
print_tree(struct node *np, int n)
{
	struct node *np1;

	indent(n); printf("%s id=%s ref=%s\n", np->name, np->id, np->ref);
	LIST_FOREACH(np1, &np->children, siblings)
		print_tree(np1, n + 2);
}

static void
sort_node(struct node *np)
{
	struct node *np1, *np2;
	int n;

	LIST_FOREACH(np1, &np->children, siblings)
		sort_node(np1);
	do {
		np1 = LIST_FIRST(&np->children);
		n = 0;
		for (;;) {
			if (np1 == NULL)
				return;
			np2 = LIST_NEXT(np1, siblings);
			if (np2 == NULL)
				return;
			if (strcmp(sbuf_data(np1->key), sbuf_data(np2->key)) > 0) {
				LIST_REMOVE(np2, siblings);
				LIST_INSERT_BEFORE(np1, np2, siblings);
				n++;
				break;
			}
			np1 = np2;
		}
	} while (n);
}

static int
refcmp(char *r1, char *r2)
{
	struct ref *r;

	LIST_FOREACH(r, &refs, next) {
		if (!strcmp(r1, r->k1))
			return (strcmp(r2, r->k2));
	}
	r = calloc(1, sizeof(*r));
	r->k1 = strdup(r1);
	r->k2 = strdup(r2);
	LIST_INSERT_HEAD(&refs, r, next);
	if (fsubs != NULL) {
		fprintf(fsubs, "s/%s/%s/g\n", r1, r2);
		fflush(fsubs);
	}
	return (0);
}

static int compare_node2(struct node *n1, struct node *n2, int in);

static int
compare_node(struct node *n1, struct node *n2, int in)
{
	int i;
	struct node *n1a, *n2a;

	i = strcmp(n1->name, n2->name);
	if (i)
		return (i);
	if (n1->id && n2->id)
		i = refcmp(n1->id, n2->id);
	else if (n1->id || n2->id)
		i = -1;
	if (i)
		return (i);
	if (n1->ref && n2->ref)
		i = refcmp(n1->ref, n2->ref);
	else if (n1->ref || n2->ref)
		i = -1;
	if (i)
		return (i);
	if (!strcmp(n1->name, "ref"))
		i = refcmp(sbuf_data(n1->cont), sbuf_data(n2->cont));
	else
		i = strcmp(sbuf_data(n1->cont), sbuf_data(n2->cont));
	if (i)
		return (1);
	n1a = LIST_FIRST(&n1->children);
	n2a = LIST_FIRST(&n2->children);
	for (;;) {
		if (n1a == NULL && n2a == NULL)
			return (0);
		if (n1a != NULL && n2a == NULL) {
			printf("1>");
			indent(in);
			print_node(n1a);
			printf("2>\n");
			return (1);
		}
		if (n1a == NULL && n2a != NULL) {
			printf("1>\n");
			printf("2>");
			indent(in);
			print_node(n2a);
			return (1);
		}
		i = compare_node2(n1a, n2a, in + 2);
		if (i)
			return (1);
		n1a = LIST_NEXT(n1a, siblings);
		n2a = LIST_NEXT(n2a, siblings);
	}
	return (0);
}

static int
compare_node2(struct node *n1, struct node *n2, int in)
{
	int i;

	i = compare_node(n1, n2, in);
	if (i) {
		printf("1>");
		indent(in);
		print_node(n1);
		printf("2>");
		indent(in);
		print_node(n2);
	}
	return (i);
}



int
main(int argc, char **argv)
{
	struct mytree *t1, *t2;
	int i;

	fsubs = fopen("_.subs", "w");
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	if (argc != 3)
		errx(1, "usage: %s file1 file2", argv[0]);

	t1 = dofile(argv[1]);
	if (t1 == NULL)
		errx(2, "XML parser error on file %s", argv[1]);
	sort_node(t1->top);
	t2 = dofile(argv[2]);
	if (t2 == NULL)
		errx(2, "XML parser error on file %s", argv[2]);
	sort_node(t2->top);
	i = compare_node(t1->top, t2->top, 0);
	return (i);
}

