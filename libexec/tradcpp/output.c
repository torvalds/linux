/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David A. Holland.
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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "utils.h"
#include "mode.h"
#include "place.h"
#include "output.h"

static int outputfd = -1;
static bool incomment = false;
static char *linebuf;
static size_t linebufpos, linebufmax;
static struct place linebufplace;

static
void
output_open(void)
{
	if (mode.output_file == NULL) {
		outputfd = STDOUT_FILENO;
	} else {
		outputfd = open(mode.output_file, O_WRONLY|O_CREAT|O_TRUNC,
				0664);
		if (outputfd < 0) {
			complain(NULL, "%s: %s",
				 mode.output_file, strerror(errno));
			die();
		}
	}
}

static
void
dowrite(const char *buf, size_t len)
{
	size_t done;
	ssize_t result;
	static unsigned write_errors = 0;

	if (!mode.do_output) {
		return;
	}

	if (outputfd < 0) {
		output_open();
	}

	done = 0;
	while (done < len) {
		result = write(outputfd, buf+done, len-done);
		if (result == -1) {
			complain(NULL, "%s: write: %s",
				 mode.output_file, strerror(errno));
			complain_failed();
			write_errors++;
			if (write_errors > 5) {
				complain(NULL, "%s: giving up",
					 mode.output_file);
				die();
			}
			/* XXX is this really a good idea? */
			sleep(1);
		}
		done += (size_t)result;
	}
}


static
void
filter_output(const char *buf, size_t len)
{
	size_t pos, start;
	bool inesc = false;
	bool inquote = false;
	char quote = '\0';

	start = 0;
	for (pos = 0; pos < len - 1; pos++) {
		if (!inquote && buf[pos] == '/' && buf[pos+1] == '*') {
			if (!incomment) {
				if (pos > start) {
					dowrite(buf + start, pos - start);
				}
				start = pos;
				pos += 2;
				incomment = true;
				/* cancel out the loop's pos++ */
				pos--;
				continue;
			}
		} else if (buf[pos] == '*' && buf[pos+1] == '/') {
			if (incomment) {
				pos += 2;
				if (mode.output_retain_comments) {
					dowrite(buf + start, pos - start);
				}
				start = pos;
				incomment = false;
				/* cancel out the loop's pos++ */
				pos--;
				continue;
			}
		}

		if (incomment) {
			/* nothing */
		} else if (inesc) {
			inesc = false;
		} else if (buf[pos] == '\\') {
			inesc = true;
		} else if (!inquote && (buf[pos] == '"' || buf[pos] == '\'')) {
			inquote = true;
			quote = buf[pos];
		} else if (inquote && buf[pos] == quote) {
			inquote = false;
		}
	}
	pos++;

	if (pos > start) {
		if (!incomment || mode.output_retain_comments) {
			dowrite(buf + start, pos - start);
		}
	}
}

void
output(const struct place *p, const char *buf, size_t len)
{
	size_t oldmax;

	if (linebufpos + len > linebufmax) {
		oldmax = linebufmax;
		if (linebufmax == 0) {
			linebufmax = 64;
		}
		while (linebufpos + len > linebufmax) {
			linebufmax *= 2;
		}
		linebuf = dorealloc(linebuf, oldmax, linebufmax);
	}
	if (linebufpos == 0) {
		if (!place_samefile(&linebufplace, p)) {
			if (mode.output_cheaplinenumbers) {
				char str[256];

				snprintf(str, sizeof(str), "# %u \"%s\"\n",
					 p->line, place_getname(p));
				dowrite(str, strlen(str));
			}
		}
		linebufplace = *p;
	}
	memcpy(linebuf + linebufpos, buf, len);
	linebufpos += len;

	if (len == 1 && buf[0] == '\n') {
		filter_output(linebuf, linebufpos);
		linebufpos = 0;
	}
}

void
output_eof(void)
{
	if (mode.output_file != NULL && outputfd >= 0) {
		close(outputfd);
	}
	outputfd = -1;
}
