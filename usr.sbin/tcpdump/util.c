/*	$OpenBSD: util.c,v 1.31 2020/12/03 08:58:52 mvs Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/limits.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <pcap.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef TIME_WITH_SYS_TIME
#include <time.h>
#endif
#include <unistd.h>

#include "interface.h"
#include "privsep.h"
/*
 * Print out a filename (or other ascii string).
 * If ep is NULL, assume no truncation check is needed.
 * Return true if truncated.
 */
int
fn_print(const u_char *s, const u_char *ep)
{
	int ret;
	u_char c;

	ret = 1;			/* assume truncated */
	while (ep == NULL || s < ep) {
		c = *s++;
		if (c == '\0') {
			ret = 0;
			break;
		}
		if (!isascii(c)) {
			c = toascii(c);
			putchar('M');
			putchar('-');
		}
		if (!isprint(c)) {
			c ^= 0x40;	/* DEL to ?, others to alpha */
			putchar('^');
		}
		putchar(c);
	}
	return(ret);
}

/*
 * Print out a counted filename (or other ascii string).
 * If ep is NULL, assume no truncation check is needed.
 * Return true if truncated.
 */
int
fn_printn(const u_char *s, u_int n, const u_char *ep)
{
	int ret;
	u_char c;

	ret = 1;			/* assume truncated */
	while (ep == NULL || s < ep) {
		if (n-- <= 0) {
			ret = 0;
			break;
		}
		c = *s++;
		if (!isascii(c)) {
			c = toascii(c);
			putchar('M');
			putchar('-');
		}
		if (!isprint(c)) {
			c ^= 0x40;	/* DEL to ?, others to alpha */
			putchar('^');
		}
		putchar(c);
	}
	return(ret);
}

/*
 * Print the timestamp
 */
void
ts_print(const struct bpf_timeval *tvp)
{
	int s;
#define TSBUFLEN 32
	static char buf[TSBUFLEN];
	static struct timeval last;
	struct timeval diff, cur;
	time_t t;

	if (Iflag && device)
		printf("%s ", device);
	switch(tflag){
	case 0:
		break;
	case -1:
		/* Unix timeval style */
		printf("%u.%06u ", tvp->tv_sec, tvp->tv_usec);
		break;
	case -2:
		t = tvp->tv_sec;
		strftime(buf, TSBUFLEN, "%b %d %T", priv_localtime(&t));
		printf("%s.%06u ", buf, tvp->tv_usec);
		break;
	case -3: /* last frame time delta  */
	case -4: /* first frame time delta  */
		cur.tv_sec = tvp->tv_sec;
		cur.tv_usec = tvp->tv_usec;
		timersub(&cur, &last, &diff);
		printf("%lld.%06ld ", diff.tv_sec, diff.tv_usec);
		if (!timerisset(&last) || tflag == -3)
			last = cur;
		break;
	default:
		s = (tvp->tv_sec + thiszone) % 86400;
		printf("%02d:%02d:%02d.%06u ",
		    s / 3600, (s % 3600) / 60, s % 60, tvp->tv_usec);
		break;
	}
}

/*
 * Print a relative number of seconds (e.g. hold time, prune timer)
 * in the form 5m1s.  This does no truncation, so 32230861 seconds
 * is represented as 1y1w1d1h1m1s.
 */
void
relts_print(int secs)
{
	static char *lengths[] = {"y", "w", "d", "h", "m", "s"};
	static int seconds[] = {31536000, 604800, 86400, 3600, 60, 1};
	char **l = lengths;
	int *s = seconds;

	if (secs <= 0) {
		printf("0s");
		return;
	}
	while (secs > 0) {
		if (secs >= *s) {
			printf("%d%s", secs / *s, *l);
			secs -= (secs / *s) * *s;
		}
		s++;
		l++;
	}
}

/*
 * Convert a token value to a string; use "fmt" if not found.
 */
const char *
tok2str(const struct tok *lp, const char *fmt, int v)
{
	static char buf[128];

	while (lp->s != NULL) {
		if (lp->v == v)
			return (lp->s);
		++lp;
	}
	if (fmt == NULL)
		fmt = "#%d";
	(void)snprintf(buf, sizeof(buf), fmt, v);
	return (buf);
}


__dead void
error(const char *fmt, ...)
{
	va_list ap;

	(void)fprintf(stderr, "%s: ", program_name);
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fputc('\n', stderr);
	exit(1);
	/* NOTREACHED */
}

void
warning(const char *fmt, ...)
{
	va_list ap;

	(void)fprintf(stderr, "%s: WARNING: ", program_name);
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fputc('\n', stderr);
}


/*
 * Copy arg vector into a new buffer, concatenating arguments with spaces.
 */
char *
copy_argv(char * const *argv)
{
	size_t len = 0, n;
	char *buf;

	if (argv == NULL)
		return (NULL);

	for (n = 0; argv[n]; n++)
		len += strlen(argv[n])+1;
	if (len == 0)
		return (NULL);

	buf = malloc(len);
	if (buf == NULL)
		return (NULL);

	strlcpy(buf, argv[0], len);
	for (n = 1; argv[n]; n++) {
		strlcat(buf, " ", len);
		strlcat(buf, argv[n], len);
	}
	return (buf);
}

char *
read_infile(char *fname)
{
	struct stat	 buf;
	int		 fd;
	ssize_t		 cc;
	size_t		 bs;
	char		*cp;

	fd = open(fname, O_RDONLY);
	if (fd == -1)
		error("can't open %s: %s", fname, pcap_strerror(errno));

	if (fstat(fd, &buf) == -1)
		error("can't stat %s: %s", fname, pcap_strerror(errno));

	if (buf.st_size >= SSIZE_MAX)
		error("file too long");

	bs = buf.st_size;
	cp = malloc(bs + 1);
	if (cp == NULL)
		err(1, NULL);
	cc = read(fd, cp, bs);
	if (cc == -1)
		error("read %s: %s", fname, pcap_strerror(errno));
	if (cc != bs)
		error("short read %s (%ld != %lu)", fname, (long)cc,
		    (unsigned long)bs);
	cp[bs] = '\0';
	close(fd);

	return (cp);
}

void
safeputs(const char *s)
{
	while (*s) {
		safeputchar(*s);
		s++;
	}
}

void
safeputchar(int c)
{
	c &= 0xff;
	if (c < 0x80 && isprint(c))
		putchar(c);
	else
		printf("\\%03o", c);
}

/*
 * Print a value a la the %b format of the kernel's printf
 * (from sbin/ifconfig/ifconfig.c)
 */
void
printb(char *s, unsigned short v, char *bits)
{
	int i, any = 0;
	char c;

	if (bits && *bits == 8)
		printf("%s=%o", s, v);
	else
		printf("%s=%x", s, v);

	if (bits) {
		bits++;
		putchar('<');
		while ((i = *bits++)) {
			if (v & (1 << (i-1))) {
				if (any)
					putchar(',');
				any = 1;
				for (; (c = *bits) > 32; bits++)
					putchar(c);
			} else
				for (; *bits > 32; bits++)
					;
		}
		putchar('>');
	}
}
