/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012-2013 Intel Corporation
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
#include <sys/ioccom.h>
#include <sys/stat.h>

#include <ctype.h>
#include <dlfcn.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvmecontrol.h"

SET_CONCAT_DEF(top, struct nvme_function);

static void
print_usage(const struct nvme_function *f)
{
	const char *cp;
	char ch;
	bool need_prefix = true;

	cp = f->usage;
	while (*cp) {
		ch = *cp++;
		if (need_prefix) {
			if (ch != ' ')
				fputs("        nvmecontrol ", stderr);
			else
				fputs("                    ", stderr);
		}
		fputc(ch, stderr);
		need_prefix = (ch == '\n');
	}
	if (!need_prefix)
		fputc('\n', stderr);
}

static void
gen_usage_set(const struct nvme_function * const *f, const struct nvme_function * const *flimit)
{

	fprintf(stderr, "usage:\n");
	while (f < flimit) {
		print_usage(*f);
		f++;
	}
	exit(1);
}

void
usage(const struct nvme_function *f)
{

	fprintf(stderr, "usage:\n");
	print_usage(f);
	exit(1);
}

void
dispatch_set(int argc, char *argv[], const struct nvme_function * const *tbl,
    const struct nvme_function * const *tbl_limit)
{
	const struct nvme_function * const *f = tbl;

	if (argv[1] == NULL) {
		gen_usage_set(tbl, tbl_limit);
		return;
	}

	while (f < tbl_limit) {
		if (strcmp(argv[1], (*f)->name) == 0) {
			(*f)->fn(*f, argc-1, &argv[1]);
			return;
		}
		f++;
	}

	fprintf(stderr, "Unknown command: %s\n", argv[1]);
	gen_usage_set(tbl, tbl_limit);
}

void
set_concat_add(struct set_concat *m, void *b, void *e)
{
	void **bp, **ep;
	int add_n, cur_n;

	if (b == NULL)
		return;
	/*
	 * Args are really pointers to arrays of pointers, but C's
	 * casting rules kinda suck since you can't directly cast
	 * struct foo ** to a void **.
	 */
	bp = (void **)b;
	ep = (void **)e;
	add_n = ep - bp;
	cur_n = 0;
	if (m->begin != NULL)
		cur_n = m->limit - m->begin;
	m->begin = reallocarray(m->begin, cur_n + add_n, sizeof(void *));
	if (m->begin == NULL)
		err(1, "expanding concat set");
	memcpy(m->begin + cur_n, bp, add_n * sizeof(void *));
	m->limit = m->begin + cur_n + add_n;
}

static void
print_bytes(void *data, uint32_t length)
{
	uint32_t	i, j;
	uint8_t		*p, *end;

	end = (uint8_t *)data + length;

	for (i = 0; i < length; i++) {
		p = (uint8_t *)data + (i*16);
		printf("%03x: ", i*16);
		for (j = 0; j < 16 && p < end; j++)
			printf("%02x ", *p++);
		if (p >= end)
			break;
		printf("\n");
	}
	printf("\n");
}

static void
print_dwords(void *data, uint32_t length)
{
	uint32_t	*p;
	uint32_t	i, j;

	p = (uint32_t *)data;
	length /= sizeof(uint32_t);

	for (i = 0; i < length; i+=8) {
		printf("%03x: ", i*4);
		for (j = 0; j < 8; j++)
			printf("%08x ", p[i+j]);
		printf("\n");
	}

	printf("\n");
}

void
print_hex(void *data, uint32_t length)
{
	if (length >= sizeof(uint32_t) || length % sizeof(uint32_t) == 0)
		print_dwords(data, length);
	else
		print_bytes(data, length);
}

void
read_controller_data(int fd, struct nvme_controller_data *cdata)
{
	struct nvme_pt_command	pt;

	memset(&pt, 0, sizeof(pt));
	pt.cmd.opc = NVME_OPC_IDENTIFY;
	pt.cmd.cdw10 = htole32(1);
	pt.buf = cdata;
	pt.len = sizeof(*cdata);
	pt.is_read = 1;

	if (ioctl(fd, NVME_PASSTHROUGH_CMD, &pt) < 0)
		err(1, "identify request failed");

	/* Convert data to host endian */
	nvme_controller_data_swapbytes(cdata);

	if (nvme_completion_is_error(&pt.cpl))
		errx(1, "identify request returned error");
}

void
read_namespace_data(int fd, uint32_t nsid, struct nvme_namespace_data *nsdata)
{
	struct nvme_pt_command	pt;

	memset(&pt, 0, sizeof(pt));
	pt.cmd.opc = NVME_OPC_IDENTIFY;
	pt.cmd.nsid = htole32(nsid);
	pt.buf = nsdata;
	pt.len = sizeof(*nsdata);
	pt.is_read = 1;

	if (ioctl(fd, NVME_PASSTHROUGH_CMD, &pt) < 0)
		err(1, "identify request failed");

	/* Convert data to host endian */
	nvme_namespace_data_swapbytes(nsdata);

	if (nvme_completion_is_error(&pt.cpl))
		errx(1, "identify request returned error");
}

int
open_dev(const char *str, int *fd, int show_error, int exit_on_error)
{
	char		full_path[64];

	if (!strnstr(str, NVME_CTRLR_PREFIX, strlen(NVME_CTRLR_PREFIX))) {
		if (show_error)
			warnx("controller/namespace ids must begin with '%s'",
			    NVME_CTRLR_PREFIX);
		if (exit_on_error)
			exit(1);
		else
			return (EINVAL);
	}

	snprintf(full_path, sizeof(full_path), _PATH_DEV"%s", str);
	*fd = open(full_path, O_RDWR);
	if (*fd < 0) {
		if (show_error)
			warn("could not open %s", full_path);
		if (exit_on_error)
			exit(1);
		else
			return (errno);
	}

	return (0);
}

void
parse_ns_str(const char *ns_str, char *ctrlr_str, uint32_t *nsid)
{
	char	*nsloc;

	/*
	 * Pull the namespace id from the string. +2 skips past the "ns" part
	 *  of the string.  Don't search past 10 characters into the string,
	 *  otherwise we know it is malformed.
	 */
	nsloc = strnstr(ns_str, NVME_NS_PREFIX, 10);
	if (nsloc != NULL)
		*nsid = strtol(nsloc + 2, NULL, 10);
	if (nsloc == NULL || (*nsid == 0 && errno != 0))
		errx(1, "invalid namespace ID '%s'", ns_str);

	/*
	 * The controller string will include only the nvmX part of the
	 *  nvmeXnsY string.
	 */
	snprintf(ctrlr_str, nsloc - ns_str + 1, "%s", ns_str);
}

/*
 * Loads all the .so's from the specified directory.
 */
static void
load_dir(const char *dir)
{
	DIR *d;
	struct dirent *dent;
	char *path = NULL;
	void *h;

	d = opendir(dir);
	if (d == NULL)
		return;
	for (dent = readdir(d); dent != NULL; dent = readdir(d)) {
		if (strcmp(".so", dent->d_name + dent->d_namlen - 3) != 0)
			continue;
		asprintf(&path, "%s/%s", dir, dent->d_name);
		if (path == NULL)
			err(1, "Can't malloc for path, giving up.");
		if ((h = dlopen(path, RTLD_NOW | RTLD_GLOBAL)) == NULL)
			warnx("Can't load %s: %s", path, dlerror());
		else {
			/*
			 * Add in the top (for cli commands)linker sets. We have
			 * to do this by hand because linker sets aren't
			 * automatically merged.
			 */
			void *begin, *limit;

			begin = dlsym(h, "__start_set_top");
			limit = dlsym(h, "__stop_set_top");
			if (begin)
				add_to_top(begin, limit);
			/* log pages use constructors so are done on load */
		}
		free(path);
		path = NULL;
	}
	closedir(d);
}

int
main(int argc, char *argv[])
{

	add_to_top(NVME_CMD_BEGIN(top), NVME_CMD_LIMIT(top));

	load_dir("/lib/nvmecontrol");
	load_dir("/usr/local/lib/nvmecontrol");

	if (argc < 2)
		gen_usage_set(top_begin(), top_limit());

	dispatch_set(argc, argv, top_begin(), top_limit());

	return (0);
}
