/*	$OpenBSD: util.c,v 1.29 2025/08/20 10:40:21 henning Exp $ */

/*
 * Copyright (c) 2004 Alexander Guy <alexander.guy@andern.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "ntpd.h"

double
gettime_corrected(void)
{
	return (gettime() + getoffset());
}

double
getoffset(void)
{
	struct timeval	tv;
	if (adjtime(NULL, &tv) == -1)
		return (0.0);
	return (tv.tv_sec + 1.0e-6 * tv.tv_usec);
}

double
gettime(void)
{
	struct timeval	tv;

	if (gettimeofday(&tv, NULL) == -1)
		fatal("gettimeofday");

	return (gettime_from_timeval(&tv));
}

double
gettime_from_timeval(struct timeval *tv)
{
	/*
	 * Account for overflow on OSes that have a 32-bit time_t.
	 */
	return ((uint64_t)tv->tv_sec + JAN_1970 + 1.0e-6 * tv->tv_usec);
}

time_t
getmonotime(void)
{
	struct timespec	ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		fatal("clock_gettime");

	return (ts.tv_sec);
}


int
d_to_tv(double d, struct timeval *tv)
{
	/* this assumes a 64 bit time_t */
	if (!isfinite(d) || d > (double)LLONG_MAX || d < (double)LLONG_MIN)
		return (-1);

	tv->tv_sec = d;
	tv->tv_usec = (d - tv->tv_sec) * 1000000;
	while (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec -= 1;
	}

	return (0);
}

double
lfp_to_d(struct l_fixedpt lfp)
{
	double	base, ret;

	lfp.int_partl = ntohl(lfp.int_partl);
	lfp.fractionl = ntohl(lfp.fractionl);

	/* see comment in ntp.h */
	base = NTP_ERA;
	if (lfp.int_partl <= INT32_MAX)
		base++; 
	ret = base * SECS_IN_ERA;
	ret += (double)(lfp.int_partl) + ((double)lfp.fractionl / L_DENOMINATOR);

	return (ret);
}

struct l_fixedpt
d_to_lfp(double d)
{
	struct l_fixedpt	lfp;

	while (d > SECS_IN_ERA)
		d -= SECS_IN_ERA;
	lfp.int_partl = htonl((u_int32_t)d);
	lfp.fractionl = htonl((u_int32_t)((d - (u_int32_t)d) * L_DENOMINATOR));

	return (lfp);
}

double
sfp_to_d(struct s_fixedpt sfp)
{
	double	ret;

	sfp.int_parts = ntohs(sfp.int_parts);
	sfp.fractions = ntohs(sfp.fractions);

	ret = (double)(sfp.int_parts) + ((double)sfp.fractions / S_DENOMINATOR);

	return (ret);
}

struct s_fixedpt
d_to_sfp(double d)
{
	struct s_fixedpt	sfp;

	sfp.int_parts = htons((u_int16_t)d);
	sfp.fractions = htons((u_int16_t)((d - (u_int16_t)d) * S_DENOMINATOR));

	return (sfp);
}

char *
print_rtable(int r)
{
	static char b[11];

	b[0] = 0;
	if (r > 0)
		snprintf(b, sizeof(b), "rtable %d", r);

	return (b);
}

const char *
log_sockaddr(struct sockaddr *sa)
{
	static char	buf[NI_MAXHOST];

	if (getnameinfo(sa, SA_LEN(sa), buf, sizeof(buf), NULL, 0,
	    NI_NUMERICHOST))
		return ("(unknown)");
	else
		return (buf);
}

const char *
log_ntp_addr(struct ntp_addr *addr)
{
	if (addr == NULL)
		return ("(unknown)");
	return log_sockaddr((struct sockaddr *)&addr->ss);
}

pid_t
start_child(char *pname, int cfd, int argc, char **argv)
{
	char		**nargv;
	int		  nargc, i;
	pid_t		  pid;

	/* Prepare the child process new argv. */
	nargv = calloc(argc + 3, sizeof(char *));
	if (nargv == NULL)
		fatal("%s: calloc", __func__);

	/* Copy the program name first. */
	nargc = 0;
	nargv[nargc++] = argv[0];

	/* Set the process name and copy the original args. */
	nargv[nargc++] = "-P";
	nargv[nargc++] = pname;
	for (i = 1; i < argc; i++)
		nargv[nargc++] = argv[i];

	nargv[nargc] = NULL;

	switch (pid = fork()) {
	case -1:
		fatal("%s: fork", __func__);
		break;
	case 0:
		/* Prepare the parent socket and execute. */
		if (cfd != PARENT_SOCK_FILENO) {
			if (dup2(cfd, PARENT_SOCK_FILENO) == -1)
				fatal("dup2");
		} else if (fcntl(cfd, F_SETFD, 0) == -1)
			fatal("fcntl");

		execvp(argv[0], nargv);
		fatal("%s: execvp", __func__);
		break;

	default:
		/* Close child's socket end. */
		close(cfd);
		break;
	}

	free(nargv);
	return (pid);
}

int
sanitize_argv(int *argc, char ***argv)
{
	char		**nargv;
	int		  nargc;
	int		  i;

	/*
	 * We need at least three arguments:
	 * Example: '/usr/sbin/ntpd' '-P' 'foobar'.
	 */
	if (*argc < 3)
		return (-1);

	*argc -= 2;

	/* Allocate new arguments vector and copy pointers. */
	nargv = calloc((*argc) + 1, sizeof(char *));
	if (nargv == NULL)
		return (-1);

	nargc = 0;
	nargv[nargc++] = (*argv)[0];
	for (i = 1; i < *argc; i++)
		nargv[nargc++] = (*argv)[i + 2];

	nargv[nargc] = NULL;
	*argv = nargv;
	return (0);
}
