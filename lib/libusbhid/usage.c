/*	$OpenBSD: usage.c,v 1.17 2018/07/09 08:57:04 mpi Exp $	*/
/*	$NetBSD: usage.c,v 1.1 2001/12/28 17:45:27 augustss Exp $	*/

/*
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

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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
	if (hid_start(hidname) == -1)
		errx(1, "hid_init: failed");
}

int
hid_start(const char *hidname)
{
	char line[100], name[100], *p, *n;
	struct usage_page *curpage = NULL;
	int lineno, no;
	FILE *f;

	if (hidname == NULL)
		hidname = _PATH_HIDTABLE;

	f = fopen(hidname, "r");
	if (f == NULL)
		return -1;
	for (lineno = 1; ; lineno++) {
		if (fgets(line, sizeof line, f) == NULL)
			break;
		if (line[0] == '#')
			continue;
		for (p = line; isspace((unsigned char)*p); p++)
			;
		if (!*p)
			continue;
		if (sscanf(line, " * %99[^\n]", name) == 1)
			no = -1;
		else if (sscanf(line, " 0x%x %99[^\n]", &no, name) != 2 &&
		    sscanf(line, " %d %99[^\n]", &no, name) != 2) {
			warnx("file %s, line %d, syntax error",
			    hidname, lineno);
			errno = EINVAL;
			goto fail;
		}
		for (p = name; *p; p++)
			if (isspace((unsigned char)*p) || *p == '.')
				*p = '_';
		n = strdup(name);
		if (!n)
			goto fail;

		if (isspace((unsigned char)line[0])) {
			if (!curpage) {
				warnx("file %s, line %d, syntax error",
				    hidname, lineno);
				free(n);
				errno = EINVAL;
				goto fail;
			}
			if (curpage->pagesize >= curpage->pagesizemax) {
				void *new;
				int len;

				len = curpage->pagesizemax + 10;
				new = reallocarray(curpage->page_contents,
				    len, sizeof(struct usage_in_page));
				if (!new) {
					free(curpage->page_contents);
					curpage->page_contents = NULL;
					free(n);
					goto fail;
				}
				curpage->pagesizemax = len;
				curpage->page_contents = new;
			}
			curpage->page_contents[curpage->pagesize].name = n;
			curpage->page_contents[curpage->pagesize].usage = no;
			curpage->pagesize++;
		} else {
			if (npages >= npagesmax) {
				int len;
				void *new;

				if (pages == NULL) {
					len = 5;
					pages = calloc(len,
					    sizeof (struct usage_page));
				} else {
					len = npagesmax * 5;
					new = reallocarray(pages,
					    len, sizeof(struct usage_page));
					if (!new) {
						free(n);
						goto fail;
					}
					pages = new;
					bzero(pages + npagesmax,
					    (len - npagesmax) *
					    sizeof(struct usage_page));
				}
				if (!pages) {
					free(n);
					goto fail;
				}
				npagesmax = len;
			}
			curpage = &pages[npages++];
			curpage->name = n;
			curpage->usage = no;
			curpage->pagesize = 0;
			curpage->pagesizemax = 10;
			curpage->page_contents = calloc(curpage->pagesizemax,
			    sizeof (struct usage_in_page));
			if (!curpage->page_contents)
				goto fail;
		}
	}
	fclose(f);
#ifdef DEBUG
	dump_hid_table();
#endif
	return 0;

fail:
	if (f)
		fclose(f);
	if (pages) {
		for (no = 0; no < npages; no++) {
			if (pages[no].name)
				free((char *)pages[no].name);
			if (pages[no].page_contents)
				free((char *)pages[no].page_contents);
		}
		free(pages);
		pages = NULL;
	}
	npages = 0;
	npagesmax = 0;
	return -1;
}

const char *
hid_usage_page(int i)
{
	static char b[10];
	int k;

	if (!pages)
		return NULL;

	for (k = 0; k < npages; k++)
		if (pages[k].usage == i)
			return pages[k].name;
	snprintf(b, sizeof b, "0x%04x", i);
	return b;
}

const char *
hid_usage_in_page(unsigned int u)
{
	int i = HID_USAGE(u), j, k, us;
	int page = HID_PAGE(u);
	static char b[100];

	for (k = 0; k < npages; k++)
		if (pages[k].usage == page)
			break;
	if (k >= npages)
		goto bad;
	for (j = 0; j < pages[k].pagesize; j++) {
		us = pages[k].page_contents[j].usage;
		if (us == -1) {
			snprintf(b, sizeof b,
			    pages[k].page_contents[j].name, i);
			return b;
		}
		if (us == i)
			return pages[k].page_contents[j].name;
	}
 bad:
	snprintf(b, sizeof b, "0x%04x", i);
	return b;
}

int
hid_parse_usage_page(const char *name)
{
	int k;

	if (!pages)
		return 0;

	for (k = 0; k < npages; k++)
		if (strcmp(pages[k].name, name) == 0)
			return pages[k].usage;
	return -1;
}

/* XXX handle hex */
int
hid_parse_usage_in_page(const char *name)
{
	const char *sep, *fmtsep, *errstr, *fmtname;
	unsigned int l;
	int k, j, us, pu, len;

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
	for (j = 0; j < pages[k].pagesize; j++) {
		us = pages[k].page_contents[j].usage;
		if (us == -1) {
			fmtname = pages[k].page_contents[j].name;
			fmtsep = strchr(fmtname, '%');
			len = fmtsep - fmtname;
			if (fmtsep != NULL && strncmp(sep, fmtname, len) == 0) {
				pu = strtonum(sep + len, 0x1, 0xFFFF, &errstr);
				if (errstr == NULL)
					return (pages[k].usage << 16) | pu;
			}
		}
		if (strcmp(pages[k].page_contents[j].name, sep) == 0)
			return (pages[k].usage << 16) |
			    pages[k].page_contents[j].usage;
	}
	return -1;
}
