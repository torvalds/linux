/*-
 * Copyright (c) 2016 Netflix, Inc.
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

#include <ctype.h>
#include <efivar.h>
#include <efivar-dp.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXSIZE 65536	/* Everyting will be smaller than this, most 1000x smaller */

/* options descriptor */
static struct option longopts[] = {
	{ "to-unix",		no_argument,		NULL,	'u' },
	{ "to-efi",		no_argument,		NULL,	'e' },
	{ "format",		no_argument,		NULL,	'f' },
	{ "parse",		no_argument,		NULL,	'p' },
	{ NULL,			0,			NULL,	0 }
};


static int flag_format, flag_parse, flag_unix, flag_efi;

static void
usage(void)
{

	errx(1, "efidp [-efpu]");
}

static ssize_t
read_file(int fd, void **rv) 
{
	uint8_t *retval;
	size_t len;
	off_t off;
	ssize_t red;

	len = MAXSIZE;
	off = 0;
	retval = malloc(len);
	do {
		red = read(fd, retval + off, len - off);
		if (red == 0)
			break;
		off += red;
		if (off == (off_t)len)
			break;
	} while (1);
	*rv = retval;

	return off;
}

static void
parse_args(int argc, char **argv)
{
	int ch;

	while ((ch = getopt_long(argc, argv, "efpu",
		    longopts, NULL)) != -1) {
		switch (ch) {
		case 'e':
			flag_efi++;
			break;
		case 'f':
			flag_format++;
			break;
		case 'p':
			flag_parse++;
			break;
		case 'u':
			flag_unix++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc >= 1)
		usage();
	
	if (flag_parse + flag_format + flag_efi + flag_unix != 1) {
		warnx("Can only use one of -p (--parse), "
		    "and -f (--format)");
		usage();
	}
}

static char *
trim(char *s)
{
	char *t;

	while (isspace(*s))
		s++;
	t = s + strlen(s) - 1;
	while (t > s && isspace(*t))
		*t-- = '\0';
	return s;
}

static void
unix_to_efi(void)
{
	char buffer[MAXSIZE];
	char efi[MAXSIZE];
	efidp dp;
	char *walker;
	int rv;

	dp = NULL;
	while (fgets(buffer, sizeof(buffer), stdin)) {
		walker= trim(buffer);
		free(dp);
		dp = NULL;
		rv = efivar_unix_path_to_device_path(walker, &dp);
		if (rv != 0 || dp == NULL) {
			errno = rv;
			warn("Can't convert '%s' to efi", walker);
			continue;
		}
		if (efidp_format_device_path(efi, sizeof(efi),
		    dp, efidp_size(dp)) < 0) {
			warnx("Can't format dp for '%s'", walker);
			continue;
		}
		printf("%s\n", efi);
	}
	free(dp);
}

static void
efi_to_unix(void)
{
	char buffer[MAXSIZE];
	char dpbuf[MAXSIZE];
	efidp dp;
	size_t dplen;
	char *walker, *dev, *relpath, *abspath;
	int rv;

	dp = (efidp)dpbuf;
	while (fgets(buffer, sizeof(buffer), stdin)) {
		walker= trim(buffer);
		dplen = efidp_parse_device_path(walker, dp, sizeof(dpbuf));
		rv = efivar_device_path_to_unix_path(dp, &dev, &relpath, &abspath);
		if (rv == 0)
			printf("%s:%s %s\n", dev, relpath, abspath);
		else {
			errno = rv;
			warn("Can't convert '%s' to unix", walker);
		}
	}
}

static void
format(void)
{
	char buffer[MAXSIZE];
	ssize_t fmtlen;
	ssize_t len;
	void *data;
	size_t dplen;
	const_efidp dp;

	len = read_file(STDIN_FILENO, &data);
	if (len == -1)
		err(1, "read");
	dp = (const_efidp)data;
	while (len > 0) {
		dplen = efidp_size(dp);
		fmtlen = efidp_format_device_path(buffer, sizeof(buffer),
		    dp, dplen);
		if (fmtlen > 0)
			printf("%s\n", buffer);
		len -= dplen;
		dp = (const_efidp)((const char *)dp + dplen);
	}
	free(data);
}

static void
parse(void)
{
	char buffer[MAXSIZE];
	efidp dp;
	ssize_t dplen;
	char *walker;

	dplen = MAXSIZE;
	dp = malloc(dplen);
	if (dp == NULL)
		errx(1, "Can't allocate memory.");
	while (fgets(buffer, sizeof(buffer), stdin)) {
		walker= trim(buffer);
		dplen = efidp_parse_device_path(walker, dp, dplen);
		if (dplen == -1)
			errx(1, "Can't parse %s", walker);
		write(STDOUT_FILENO, dp, dplen);
	}
	free(dp);
}

int
main(int argc, char **argv)
{

	parse_args(argc, argv);
	if (flag_unix)
		efi_to_unix();
	else if (flag_efi)
		unix_to_efi();
	else if (flag_format)
		format();
	else if (flag_parse)
		parse();
}
