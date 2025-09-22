/*	$OpenBSD: main.c,v 1.2 2008/06/26 05:42:05 ray Exp $	*/
/*	$NetBSD: main.c,v 1.4 2002/02/21 07:38:18 itojun Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int	main(int, char *[]);
void	die(void);

void
die(void)
{

	if (ferror(stdin))
		err(1, "fgetln");
	else
		errx(1, "input is truncated");
}

int
main(int argc, char *argv[])
{
	char *p, *title, *buf, *format;
	size_t len;
	struct tm tm;

	for (;;) {
		p = fgetln(stdin, &len);
		if (p == 0)
			die();
		title = malloc(len + 1);
		memcpy(title, p, len);
		title[len] = '\0';

		if (!strcmp(title, "EOF\n"))
			return(0);
		if (title[0] == '#' || title[0] == '\n') {
			free(title);
			continue;
		}

		p = fgetln(stdin, &len);
		if (p == 0)
			die();
		buf = malloc(len + 1);
		memcpy(buf, p, len);
		buf[len] = '\0';

		p = fgetln(stdin, &len);
		if (p == 0)
			die();
		format = malloc(len + 1);
		memcpy(format, p, len);
		format[len] = '\0';

		tm.tm_sec = -1;
		tm.tm_min = -1;
		tm.tm_hour = -1;
		tm.tm_mday = -1;
		tm.tm_mon = -1;
		tm.tm_year = -1;
		tm.tm_wday = -1;
		tm.tm_yday = -1;

		p = strptime(buf, format, &tm);

		printf("%s", title);
		if (p) {
			printf("succeeded\n");
			printf("%d %d %d %d %d %d %d %d\n",
			    tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday,
			    tm.tm_mon, tm.tm_year, tm.tm_wday, tm.tm_yday);
			printf("%s\n", p);
		} else {
			printf("failed\n");
		}

		free(title);
		free(buf);
		free(format);
	}
}
