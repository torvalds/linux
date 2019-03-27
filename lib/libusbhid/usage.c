/*	$NetBSD: usage.c,v 1.8 2000/10/10 19:23:58 is Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1999 Lennart Augustsson <augustss@netbsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usbhid.h"

#define _PATH_HIDTABLE "/usr/share/misc/usb_hid_usages"

struct usage_in_page {
	const char *name;
	int usage;
};

static struct usage_page {
	const char *name;
	int usage;
	struct usage_in_page *page_contents;
	int pagesize, pagesizemax;
} *pages;
static int npages, npagesmax;

#ifdef DEBUG
void
dump_hid_table(void)
{
	int i, j;

	for (i = 0; i < npages; i++) {
		printf("%d\t%s\n", pages[i].usage, pages[i].name);
		for (j = 0; j < pages[i].pagesize; j++) {
			printf("\t%d\t%s\n", pages[i].page_contents[j].usage,
			       pages[i].page_contents[j].name);
		}
	}
}
#endif

void
hid_init(const char *hidname)
{
	FILE *f;
	char line[100], name[100], *p, *n;
	int no;
	int lineno;
	struct usage_page *curpage = NULL;

	if (hidname == NULL)
		hidname = _PATH_HIDTABLE;

	f = fopen(hidname, "r");
	if (f == NULL)
		err(1, "%s", hidname);
	for (lineno = 1; ; lineno++) {
		if (fgets(line, sizeof line, f) == NULL)
			break;
		if (line[0] == '#')
			continue;
		for (p = line; *p && isspace(*p); p++)
			;
		if (!*p)
			continue;
		if (sscanf(line, " * %[^\n]", name) == 1)
			no = -1;
		else if (sscanf(line, " 0x%x %[^\n]", &no, name) != 2 &&
			 sscanf(line, " %d %[^\n]", &no, name) != 2)
			errx(1, "file %s, line %d, syntax error",
			     hidname, lineno);
		for (p = name; *p; p++)
			if (isspace(*p) || *p == '.')
				*p = '_';
		n = strdup(name);
		if (!n)
			err(1, "strdup");
		if (isspace(line[0])) {
			if (!curpage)
				errx(1, "file %s, line %d, syntax error",
				     hidname, lineno);
			if (curpage->pagesize >= curpage->pagesizemax) {
				curpage->pagesizemax += 10;
				curpage->page_contents =
					realloc(curpage->page_contents,
						curpage->pagesizemax *
						sizeof (struct usage_in_page));
				if (!curpage->page_contents)
					err(1, "realloc");
			}
			curpage->page_contents[curpage->pagesize].name = n;
			curpage->page_contents[curpage->pagesize].usage = no;
			curpage->pagesize++;
		} else {
			if (npages >= npagesmax) {
				if (pages == NULL) {
					npagesmax = 5;
					pages = malloc(npagesmax *
						  sizeof (struct usage_page));
				} else {
					npagesmax += 5;
					pages = realloc(pages,
						   npagesmax *
						   sizeof (struct usage_page));
				}
				if (!pages)
					err(1, "alloc");
			}
			curpage = &pages[npages++];
			curpage->name = n;
			curpage->usage = no;
			curpage->pagesize = 0;
			curpage->pagesizemax = 10;
			curpage->page_contents =
				malloc(curpage->pagesizemax *
				       sizeof (struct usage_in_page));
			if (!curpage->page_contents)
				err(1, "malloc");
		}
	}
	fclose(f);
#ifdef DEBUG
	dump_hid_table();
#endif
}

const char *
hid_usage_page(int i)
{
	static char b[10];
	int k;

	if (!pages)
		errx(1, "no hid table");

	for (k = 0; k < npages; k++)
		if (pages[k].usage == i)
			return pages[k].name;
	sprintf(b, "0x%04x", i);
	return b;
}

const char *
hid_usage_in_page(unsigned int u)
{
	int page = HID_PAGE(u);
	int i = HID_USAGE(u);
	static char b[100];
	int j, k, us;

	for (k = 0; k < npages; k++)
		if (pages[k].usage == page)
			break;
	if (k >= npages)
		goto bad;
	for (j = 0; j < pages[k].pagesize; j++) {
		us = pages[k].page_contents[j].usage;
		if (us == -1) {
			sprintf(b,
			    fmtcheck(pages[k].page_contents[j].name, "%d"),
			    i);
			return b;
		}
		if (us == i)
			return pages[k].page_contents[j].name;
	}
 bad:
	sprintf(b, "0x%04x", i);
	return b;
}

int
hid_parse_usage_page(const char *name)
{
	int k;

	if (!pages)
		errx(1, "no hid table");

	for (k = 0; k < npages; k++)
		if (strcmp(pages[k].name, name) == 0)
			return pages[k].usage;
	return -1;
}

/* XXX handle hex */
int
hid_parse_usage_in_page(const char *name)
{
	const char *sep;
	int k, j;
	unsigned int l;

	sep = strchr(name, ':');
	if (sep == NULL)
		return -1;
	l = sep - name;
	for (k = 0; k < npages; k++)
		if (strncmp(pages[k].name, name, l) == 0)
			goto found;
	return -1;
 found:
	sep++;
	for (j = 0; j < pages[k].pagesize; j++)
		if (strcmp(pages[k].page_contents[j].name, sep) == 0)
			return (pages[k].usage << 16) | pages[k].page_contents[j].usage;
	return (-1);
}
