/*-
 * Copyright (c) 2010, 2013 The NetBSD Foundation, Inc.
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "bool.h"
#include "array.h"
#include "mode.h"
#include "place.h"
#include "files.h"
#include "directive.h"

struct incdir {
	const char *name;
	bool issystem;
};

DECLARRAY(incdir, static UNUSED);
DEFARRAY(incdir, static);

static struct incdirarray quotepath, bracketpath;

////////////////////////////////////////////////////////////
// management

static
struct incdir *
incdir_create(const char *name, bool issystem)
{
	struct incdir *id;

	id = domalloc(sizeof(*id));
	id->name = name;
	id->issystem = issystem;
	return id;
}

static
void
incdir_destroy(struct incdir *id)
{
	dofree(id, sizeof(*id));
}

void
files_init(void)
{
	incdirarray_init(&quotepath);
	incdirarray_init(&bracketpath);
}

DESTROYALL_ARRAY(incdir, );

void
files_cleanup(void)
{
	incdirarray_destroyall(&quotepath);
	incdirarray_cleanup(&quotepath);
	incdirarray_destroyall(&bracketpath);
	incdirarray_cleanup(&bracketpath);
}

////////////////////////////////////////////////////////////
// path setup

void
files_addquotepath(const char *dir, bool issystem)
{
	struct incdir *id;

	id = incdir_create(dir, issystem);
	incdirarray_add(&quotepath, id, NULL);
}

void
files_addbracketpath(const char *dir, bool issystem)
{
	struct incdir *id;

	id = incdir_create(dir, issystem);
	incdirarray_add(&bracketpath, id, NULL);
}

////////////////////////////////////////////////////////////
// parsing

/*
 * Find the end of the logical line. End of line characters that are
 * commented out do not count.
 */
static
size_t
findeol(const char *buf, size_t start, size_t limit)
{
	size_t i;
	int incomment = 0;
	bool inquote = false;
	char quote = '\0';

	for (i=start; i<limit; i++) {
		if (incomment) {
			if (i+1 < limit && buf[i] == '*' && buf[i+1] == '/') {
				i++;
				incomment = 0;
			}
		} else if (!inquote && i+1 < limit &&
			   buf[i] == '/' && buf[i+1] == '*') {
			i++;
			incomment = 1;
		} else if (i+1 < limit &&
			   buf[i] == '\\' && buf[i+1] != '\n') {
			i++;
		} else if (!inquote && (buf[i] == '"' || buf[i] == '\'')) {
			inquote = true;
			quote = buf[i];
		} else if (inquote && buf[i] == quote) {
			inquote = false;
		} else if (buf[i] == '\n') {
			return i;
		}
	}
	return limit;
}

static
unsigned
countnls(const char *buf, size_t start, size_t limit)
{
	size_t i;
	unsigned count = 0;

	for (i=start; i<limit; i++) {
		if (buf[i] == '\n') {
			count++;
			if (count == 0) {
				/* just return the max and error downstream */
				return count - 1;
			}
		}
	}
	return count;
}

static
void
file_read(const struct placefile *pf, int fd, const char *name, bool toplevel)
{
	struct lineplace places;
	struct place ptmp;
	size_t bufend, bufmax, linestart, lineend, nextlinestart, tmp;
	ssize_t result;
	bool ateof = false;
	char *buf;

	place_setfilestart(&places.current, pf);
	places.nextline = places.current;

	if (name) {
		debuglog(&places.current, "Reading file %s", name);
	} else {
		debuglog(&places.current, "Reading standard input");
	}

	bufmax = 128;
	bufend = 0;
	linestart = 0;
	lineend = 0;
	buf = domalloc(bufmax);

	while (1) {
		if (lineend >= bufend) {
			/* do not have a whole line in the buffer; read more */
			assert(bufend >= linestart);
			if (linestart > 0 && bufend > linestart) {
				/* slide to beginning of buffer */
				memmove(buf, buf+linestart, bufend-linestart);
				bufend -= linestart;
				lineend -= linestart;
				linestart = 0;
			}
			if (bufend >= bufmax) {
				/* need bigger buffer */
				buf = dorealloc(buf, bufmax, bufmax*2);
				bufmax = bufmax*2;
				/* just in case someone's screwing around */
				if (bufmax > 0xffffffff) {
					complain(&places.current,
						 "Input line too long");
					die();
				}
			}

			if (ateof) {
				/* don't read again, in case it's a socket */
				result = 0;
			} else {
				result = read(fd, buf+bufend, bufmax - bufend);
			}

			if (result == -1) {
				/* read error */
				complain(NULL, "%s: %s",
					 name, strerror(errno));
				complain_fail();
			} else if (result == 0 && bufend == linestart) {
				/* eof */
				ateof = true;
				break;
			} else if (result == 0) {
				/* eof in middle of line */
				ateof = true;
				ptmp = places.current;
				place_addcolumns(&ptmp, bufend - linestart);
				if (buf[bufend - 1] == '\n') {
					complain(&ptmp, "Unclosed comment");
					complain_fail();
				} else {
					complain(&ptmp,
						 "No newline at end of file");
				}
				if (mode.werror) {
					complain_fail();
				}
				assert(bufend < bufmax);
				lineend = bufend++;
				buf[lineend] = '\n';
			} else {
				bufend += (size_t)result;
				lineend = findeol(buf, linestart, bufend);
			}
			/* loop in case we still don't have a whole line */
			continue;
		}

		/* have a line */
		assert(buf[lineend] == '\n');
		buf[lineend] = '\0';
		nextlinestart = lineend+1;
		place_addlines(&places.nextline, 1);

		/* check for CR/NL */
		if (lineend > 0 && buf[lineend-1] == '\r') {
			buf[lineend-1] = '\0';
			lineend--;
		}

		/* check for continuation line */
		if (lineend > 0 && buf[lineend-1]=='\\') {
			lineend--;
			tmp = nextlinestart - lineend;
			if (bufend > nextlinestart) {
				memmove(buf+lineend, buf+nextlinestart,
					bufend - nextlinestart);
			}
			bufend -= tmp;
			nextlinestart -= tmp;
			lineend = findeol(buf, linestart, bufend);
			/* might not have a whole line, so loop */
			continue;
		}

		/* line now goes from linestart to lineend */
		assert(buf[lineend] == '\0');

		/* count how many commented-out newlines we swallowed */
		place_addlines(&places.nextline,
			       countnls(buf, linestart, lineend));

		/* process the line (even if it's empty) */
		directive_gotline(&places, buf+linestart, lineend-linestart);

		linestart = nextlinestart;
		lineend = findeol(buf, linestart, bufend);
		places.current = places.nextline;
	}

	if (toplevel) {
		directive_goteof(&places.current);
	}
	dofree(buf, bufmax);
}

////////////////////////////////////////////////////////////
// path search

static
char *
mkfilename(struct place *place, const char *dir, const char *file)
{
	size_t dlen, flen, rlen;
	char *ret;
	bool needslash = false;

	if (dir == NULL) {
		dir = place_getparsedir(place);
	}

	dlen = strlen(dir);
	flen = strlen(file);
	if (dlen > 0 && dir[dlen-1] != '/') {
		needslash = true;
	}

	rlen = dlen + (needslash ? 1 : 0) + flen;
	ret = domalloc(rlen + 1);
	snprintf(ret, rlen+1, "%s%s%s", dir, needslash ? "/" : "", file);
	return ret;
}

static
int
file_tryopen(const char *file)
{
	int fd;

	/* XXX check for non-regular files */

	fd = open(file, O_RDONLY);
	if (fd == -1) {
		if (errno != ENOENT && errno != ENOTDIR) {
			complain(NULL, "%s: %s", file, strerror(errno));
		}
		return -1;
	}

	return fd;
}

static
void
file_search(struct place *place, struct incdirarray *path, const char *name)
{
	unsigned i, num;
	struct incdir *id;
	const struct placefile *pf;
	char *file;
	int fd;

	assert(place != NULL);

	if (name[0] == '/') {
		fd = file_tryopen(name);
		if (fd >= 0) {
			pf = place_addfile(place, name, true);
			file_read(pf, fd, name, false);
			close(fd);
			return;
		}
	} else {
		num = incdirarray_num(path);
		for (i=0; i<num; i++) {
			id = incdirarray_get(path, i);
			file = mkfilename(place, id->name, name);
			fd = file_tryopen(file);
			if (fd >= 0) {
				pf = place_addfile(place, file, id->issystem);
				file_read(pf, fd, file, false);
				dostrfree(file);
				close(fd);
				return;
			}
			dostrfree(file);
		}
	}
	complain(place, "Include file %s not found", name);
	complain_fail();
}

void
file_readquote(struct place *place, const char *name)
{
	file_search(place, &quotepath, name);
}

void
file_readbracket(struct place *place, const char *name)
{
	file_search(place, &bracketpath, name);
}

void
file_readabsolute(struct place *place, const char *name)
{
	const struct placefile *pf;
	int fd;

	assert(place != NULL);

	if (name == NULL) {
		fd = STDIN_FILENO;
		pf = place_addfile(place, "<standard-input>", false);
	} else {
		fd = file_tryopen(name);
		if (fd < 0) {
			complain(NULL, "%s: %s", name, strerror(errno));
			die();
		}
		pf = place_addfile(place, name, false);
	}

	file_read(pf, fd, name, true);

	if (name != NULL) {
		close(fd);
	}
}
