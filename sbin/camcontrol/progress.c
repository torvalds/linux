/*	$NetBSD: progressbar.c,v 1.21 2009/04/12 10:18:52 lukem Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1997-2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "progress.h"

static const char * const suffixes[] = {
	"",	/* 2^0  (byte) */
	"KiB",	/* 2^10 Kibibyte */
	"MiB",	/* 2^20 Mebibyte */
	"GiB",	/* 2^30 Gibibyte */
	"TiB",	/* 2^40 Tebibyte */
	"PiB",	/* 2^50 Pebibyte */
	"EiB",	/* 2^60 Exbibyte */
};

#define NSUFFIXES	nitems(suffixes)
#define SECSPERHOUR	(60 * 60)
#define DEFAULT_TTYWIDTH	80

/* initialise progress meter structure */
int
progress_init(progress_t *prog, const char *prefix, uint64_t total)
{
        struct winsize	winsize;
        int		oerrno = errno;

	(void) memset(prog, 0x0, sizeof(*prog));
	prog->size = total;
	prog->prefix = strdup(prefix);
	prog->start = time(NULL);
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsize) != -1 &&
            winsize.ws_col != 0) {
                prog->ttywidth = winsize.ws_col;
        } else {
                prog->ttywidth = DEFAULT_TTYWIDTH;
	}
        errno = oerrno;
	return 1;
}

/* update the values in the progress meter */
int
progress_update(progress_t *prog, uint64_t done)
{
	prog->done = done;
	prog->percent = (prog->done * 100) / prog->size;
	prog->now = time(NULL);
	prog->elapsed = prog->now - prog->start;
	if (done == 0 || prog->elapsed == 0 || prog->done / prog->elapsed == 0) {
		prog->eta = 0;
	} else {
		prog->eta = prog->size / (prog->done / prog->elapsed) - prog->elapsed;
	}
	return 1;
}

/* update the values in the progress meter */
int
progress_reset_size(progress_t *prog, uint64_t size)
{
	prog->size = size;
	return 1;
}

/* make it look pretty at the end - display done bytes (usually total) */
int
progress_complete(progress_t *prog, uint64_t done)
{
	progress_update(prog, done);
	progress_draw(prog);
	printf("\n");
	return 1;
}

/* draw the progress meter */
int
progress_draw(progress_t *prog)
{
#define	BAROVERHEAD	45		/* non `*' portion of progress bar */
					/*
					 * stars should contain at least
					 * sizeof(buf) - BAROVERHEAD entries
					 */
#define	MIN_BAR_LEN	10
	static const char	stars[] =
"*****************************************************************************"
"*****************************************************************************"
"*****************************************************************************";
	unsigned		bytesabbrev;
	unsigned		bpsabbrev;
	int64_t			secs;
	uint64_t		bytespersec;
	uint64_t		abbrevsize;
	int64_t			secsleft;
	ssize_t			barlength;
	size_t			starc;
	char			hours[12];
	char			buf[256];
	int			len;
	int			prefix_len;

	prefix_len = strlen(prog->prefix);
	barlength = MIN(sizeof(buf) - 1, (unsigned)prog->ttywidth) -
		BAROVERHEAD - prefix_len;
	if (barlength < MIN_BAR_LEN) {
		int tmp_prefix_len;
		/*
		 * In this case the TTY width is too small or the prefix is
		 * too large for a progress bar.  We'll try decreasing the
		 * prefix length.
		 */
		barlength = MIN_BAR_LEN;
		tmp_prefix_len = MIN(sizeof(buf) - 1,(unsigned)prog->ttywidth) -
		    BAROVERHEAD - MIN_BAR_LEN;
		if (tmp_prefix_len > 0)
			prefix_len = tmp_prefix_len;
		else
			prefix_len = 0;
	}
	starc = (barlength * prog->percent) / 100;
	abbrevsize = prog->done;
	for (bytesabbrev = 0; abbrevsize >= 100000 && bytesabbrev < NSUFFIXES; bytesabbrev++) {
		abbrevsize >>= 10;
	}
	if (bytesabbrev == NSUFFIXES) {
		bytesabbrev--;
	}
	bytespersec = 0;
	if (prog->done > 0) {
		bytespersec = prog->done;
		if (prog->elapsed > 0) {
			bytespersec /= prog->elapsed;
		}
	}
	for (bpsabbrev = 1; bytespersec >= 1024000 && bpsabbrev < NSUFFIXES; bpsabbrev++) {
		bytespersec >>= 10;
	}
	if (prog->done == 0 || prog->elapsed <= 0 || prog->done > prog->size) {
		secsleft = 0;
	} else {
		secsleft = prog->eta;
	}
	if ((secs = secsleft / SECSPERHOUR) > 0) {
		(void) snprintf(hours, sizeof(hours), "%2lld:", (long long)secs);
	} else {
		(void) snprintf(hours, sizeof(hours), "   ");
	}
	secs = secsleft % SECSPERHOUR;
	len = snprintf(buf, sizeof(buf),
		"\r%.*s %3lld%% |%.*s%*s| %5lld %-3s %3lld.%02d %.2sB/s %s%02d:%02d ETA",
		prefix_len, (prog->prefix) ? prog->prefix : "",
		(long long)prog->percent,
		(int)starc, stars, (int)(barlength - starc), "",
		(long long)abbrevsize,
		suffixes[bytesabbrev],
		(long long)(bytespersec / 1024),
		(int)((bytespersec % 1024) * 100 / 1024),
		suffixes[bpsabbrev],
		hours,
		(int)secs / 60, (int)secs % 60);
	return (int)write(STDOUT_FILENO, buf, len);
}
