/*
 * Copyright (c) 2006 Maxim Sobolev <sobomax@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/uio.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dev/powermac_nvram/powermac_nvramvar.h>

#define	DEVICE_NAME	(_PATH_DEV "powermac_nvram")

static void usage(void);
static int remove_var(uint8_t *, int, const char *);
static int append_var(uint8_t *, int, const char *, const char *);

struct deletelist {
	char *name;
	struct deletelist *next;
	struct deletelist *last;
};

static union {
	uint8_t buf[sizeof(struct chrp_header)];
	struct chrp_header header;
} conv;

int
main(int argc, char **argv)
{
	int opt, dump, fd, res, i, size;
	uint8_t buf[NVRAM_SIZE], *cp, *common;
	struct deletelist *dl;

	dump = 0;
	dl = NULL;

	while((opt = getopt(argc, argv, "d:p")) != -1) {
		switch(opt) {
		case 'p':
			dump = 1;
			break;

		case 'd':
			if (dl == NULL) {
				dl = malloc(sizeof(*dl));
				if (dl == NULL)
					err(1, "malloc");
				bzero(dl, sizeof(*dl));
				dl->last = dl;
			} else {
				dl->last->next = malloc(sizeof(*dl));
				if (dl->last->next == NULL)
					err(1, "malloc");
				dl->last = dl->last->next;
				bzero(dl->last, sizeof(*dl));
			}
			dl->last->name = optarg;
			break;

		default:
			usage();
			/* Not reached */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0 && dump == 0 && dl == NULL) {
		usage();
		/* Not reached */
	}

	fd = open(DEVICE_NAME, O_RDWR);
	if (fd == -1)
		err(1, DEVICE_NAME);
	for (i = 0; i < (int)sizeof(buf);) {
		res = read(fd, buf + i, sizeof(buf) - i);
		if (res == -1 && errno != EINTR)
			err(1, DEVICE_NAME);
		if (res == 0)
			break;
		if (res > 0)
			i += res;
	}
	if (i != sizeof(buf))
		errx(1, "%s: short read", DEVICE_NAME);

	/* Locate common block */
	size = 0;
	for (cp = buf; cp < buf + sizeof(buf); cp += size) {
		memcpy(conv.buf, cp, sizeof(struct chrp_header));
		size = conv.header.length * 0x10;
		if (strncmp(conv.header.name, "common", 7) == 0)
			break;
	}
	if (cp >= buf + sizeof(buf) || size <= (int)sizeof(struct chrp_header))
		errx(1, "%s: no common block", DEVICE_NAME);
	common = cp + sizeof(struct chrp_header);
	size -= sizeof(struct chrp_header);

	if (dump != 0) {
		while (size > 0) {
			i = strlen(common) + 1;
			if (i == 1)
				break;
			printf("%s\n", common);
			size -= i;
			common += i;
		}
		exit(0);
	}

	for (;dl != NULL; dl = dl->next) {
		if (remove_var(common, size, dl->name) == 0)
			warnx("%s: no such variable", dl->name);
	}

	for (; argc > 0; argc--, argv++) {
		cp = strchr(*argv, '=');
		if (cp == NULL)
			errx(1, "%s: invalid argument", *argv);
		cp[0] = '\0';
		cp++;
		remove_var(common, size, *argv);
		if (append_var(common, size, *argv, cp) == -1)
			errx(1, "%s: error setting variable", *argv);
	}

	for (i = 0; i < (int)sizeof(buf);) {
		res = write(fd, buf + i, sizeof(buf) - i);
		if (res == -1 && errno != EINTR)
			err(1, DEVICE_NAME);
		if (res == 0)
			break;
		if (res > 0)
			i += res;
	}
	if (i != sizeof(buf))
		errx(1, "%s: short write", DEVICE_NAME);
	if (close(fd) == -1)
		err(1, DEVICE_NAME);

	exit(0);
}

static void
usage(void)
{

	fprintf(stderr, "usage: nvram [-p] | [-d name ...] [name=value ...]\n");
	exit(1);
}

static int
remove_var(uint8_t *buf, int len, const char *var_name)
{
	int nremoved, i, name_len;

	nremoved = 0;
	name_len = strlen(var_name);
	while (len > 0) {
		i = strlen(buf) + 1;
		if (i == 1)
			break;
		if (strncmp(buf, var_name, name_len) == 0 && buf[name_len] == '=') {
			memmove(buf, buf + i, len - i);
			memset(buf + len - i, '\0', i);
			nremoved += 1;
			continue;
		}
		len -= i;
		buf += i;
	}
	return nremoved;
}

static int
append_var(uint8_t *buf, int len, const char *var_name, const char *var_value)
{
	int i, append_len;

	while (len > 0) {
		i = strlen(buf) + 1;
		if (i == 1)
			break;
		len -= i;
		buf += i;
	}
	append_len = strlen(var_name) + strlen(var_value) + 2;
	if (len < append_len)
		return -1;
	sprintf(buf, "%s=%s", var_name, var_value);
	return 0;
}
