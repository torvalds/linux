/*-
 * Copyright 1986, Larry Wall
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following condition is met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this condition and the following disclaimer.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * patch - a program to apply diffs to original files
 *
 * -C option added in 1998, original code by Marc Espie, based on FreeBSD
 * behaviour
 *
 * $OpenBSD: inp.c,v 1.44 2015/07/26 14:32:19 millert Exp $
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <paths.h>
#include <spawn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "util.h"
#include "pch.h"
#include "inp.h"


/* Input-file-with-indexable-lines abstract type */

static size_t	i_size;		/* size of the input file */
static char	*i_womp;	/* plan a buffer for entire file */
static char	**i_ptr;	/* pointers to lines in i_womp */
static char	empty_line[] = { '\0' };

static int	tifd = -1;	/* plan b virtual string array */
static char	*tibuf[2];	/* plan b buffers */
static LINENUM	tiline[2] = {-1, -1};	/* 1st line in each buffer */
static size_t	lines_per_buf;	/* how many lines per buffer */
static size_t	tibuflen;	/* plan b buffer length */
static size_t	tireclen;	/* length of records in tmp file */

static bool	rev_in_string(const char *);
static bool	reallocate_lines(size_t *);

/* returns false if insufficient memory */
static bool	plan_a(const char *);

static void	plan_b(const char *);

/* New patch--prepare to edit another file. */

void
re_input(void)
{
	if (using_plan_a) {
		free(i_ptr);
		i_ptr = NULL;
		if (i_womp != NULL) {
			munmap(i_womp, i_size);
			i_womp = NULL;
		}
		i_size = 0;
	} else {
		using_plan_a = true;	/* maybe the next one is smaller */
		close(tifd);
		tifd = -1;
		free(tibuf[0]);
		free(tibuf[1]);
		tibuf[0] = tibuf[1] = NULL;
		tiline[0] = tiline[1] = -1;
		tireclen = 0;
	}
}

/* Construct the line index, somehow or other. */

void
scan_input(const char *filename)
{
	if (!plan_a(filename))
		plan_b(filename);
	if (verbose) {
		say("Patching file %s using Plan %s...\n", filename,
		    (using_plan_a ? "A" : "B"));
	}
}

static bool
reallocate_lines(size_t *lines_allocated)
{
	char	**p;
	size_t	new_size;

	new_size = *lines_allocated * 3 / 2;
	p = reallocarray(i_ptr, new_size + 2, sizeof(char *));
	if (p == NULL) {	/* shucks, it was a near thing */
		munmap(i_womp, i_size);
		i_womp = NULL;
		free(i_ptr);
		i_ptr = NULL;
		*lines_allocated = 0;
		return false;
	}
	*lines_allocated = new_size;
	i_ptr = p;
	return true;
}

/* Try keeping everything in memory. */

static bool
plan_a(const char *filename)
{
	int		ifd, statfailed;
	char		*p, *s;
	struct stat	filestat;
	ptrdiff_t	sz;
	size_t		i;
	size_t		iline, lines_allocated;

#ifdef DEBUGGING
	if (debug & 8)
		return false;
#endif

	if (filename == NULL || *filename == '\0')
		return false;

	statfailed = stat(filename, &filestat);
	if (statfailed && ok_to_create_file) {
		if (verbose)
			say("(Creating file %s...)\n", filename);

		/*
		 * in check_patch case, we still display `Creating file' even
		 * though we're not. The rule is that -C should be as similar
		 * to normal patch behavior as possible
		 */
		if (check_only)
			return true;
		makedirs(filename, true);
		close(creat(filename, 0666));
		statfailed = stat(filename, &filestat);
	}
	if (statfailed)
		fatal("can't find %s\n", filename);
	filemode = filestat.st_mode;
	if (!S_ISREG(filemode))
		fatal("%s is not a normal file--can't patch\n", filename);
	if ((uint64_t)filestat.st_size > SIZE_MAX) {
		say("block too large to mmap\n");
		return false;
	}
	i_size = (size_t)filestat.st_size;
	if (out_of_mem) {
		set_hunkmax();	/* make sure dynamic arrays are allocated */
		out_of_mem = false;
		return false;	/* force plan b because plan a bombed */
	}
	if ((ifd = open(filename, O_RDONLY)) < 0)
		pfatal("can't open file %s", filename);

	if (i_size) {
		i_womp = mmap(NULL, i_size, PROT_READ, MAP_PRIVATE, ifd, 0);
		if (i_womp == MAP_FAILED) {
			perror("mmap failed");
			i_womp = NULL;
			close(ifd);
			return false;
		}
	} else {
		i_womp = NULL;
	}

	close(ifd);
	if (i_size)
		madvise(i_womp, i_size, MADV_SEQUENTIAL);

	/* estimate the number of lines */
	lines_allocated = i_size / 25;
	if (lines_allocated < 100)
		lines_allocated = 100;

	if (!reallocate_lines(&lines_allocated))
		return false;

	/* now scan the buffer and build pointer array */
	iline = 1;
	i_ptr[iline] = i_womp;
	/*
	 * Testing for NUL here actively breaks files that innocently use NUL
	 * for other reasons. mmap(2) succeeded, just scan the whole buffer.
	 */
	for (s = i_womp, i = 0; i < i_size; s++, i++) {
		if (*s == '\n') {
			if (iline == lines_allocated) {
				if (!reallocate_lines(&lines_allocated))
					return false;
			}
			/* these are NOT NUL terminated */
			i_ptr[++iline] = s + 1;
		}
	}
	/* if the last line contains no EOL, append one */
	if (i_size > 0 && i_womp[i_size - 1] != '\n') {
		last_line_missing_eol = true;
		/* fix last line */
		sz = s - i_ptr[iline];
		p = malloc(sz + 1);
		if (p == NULL) {
			free(i_ptr);
			i_ptr = NULL;
			munmap(i_womp, i_size);
			i_womp = NULL;
			return false;
		}

		memcpy(p, i_ptr[iline], sz);
		p[sz] = '\n';
		i_ptr[iline] = p;
		/* count the extra line and make it point to some valid mem */
		i_ptr[++iline] = empty_line;
	} else
		last_line_missing_eol = false;

	input_lines = iline - 1;

	/* now check for revision, if any */

	if (revision != NULL) {
		if (i_womp == NULL || !rev_in_string(i_womp)) {
			if (force) {
				if (verbose)
					say("Warning: this file doesn't appear "
					    "to be the %s version--patching anyway.\n",
					    revision);
			} else if (batch) {
				fatal("this file doesn't appear to be the "
				    "%s version--aborting.\n",
				    revision);
			} else {
				ask("This file doesn't appear to be the "
				    "%s version--patch anyway? [n] ",
				    revision);
				if (*buf != 'y')
					fatal("aborted\n");
			}
		} else if (verbose)
			say("Good.  This file appears to be the %s version.\n",
			    revision);
	}
	return true;		/* plan a will work */
}

/* Keep (virtually) nothing in memory. */

static void
plan_b(const char *filename)
{
	FILE	*ifp;
	size_t	i = 0, j, len, maxlen = 1;
	char	*lbuf = NULL, *p;
	bool	found_revision = (revision == NULL);

	using_plan_a = false;
	if ((ifp = fopen(filename, "r")) == NULL)
		pfatal("can't open file %s", filename);
	unlink(TMPINNAME);
	if ((tifd = open(TMPINNAME, O_EXCL | O_CREAT | O_WRONLY, 0666)) < 0)
		pfatal("can't open file %s", TMPINNAME);
	while ((p = fgetln(ifp, &len)) != NULL) {
		if (p[len - 1] == '\n')
			p[len - 1] = '\0';
		else {
			/* EOF without EOL, copy and add the NUL */
			if ((lbuf = malloc(len + 1)) == NULL)
				fatal("out of memory\n");
			memcpy(lbuf, p, len);
			lbuf[len] = '\0';
			p = lbuf;

			last_line_missing_eol = true;
			len++;
		}
		if (revision != NULL && !found_revision && rev_in_string(p))
			found_revision = true;
		if (len > maxlen)
			maxlen = len;   /* find longest line */
	}
	free(lbuf);
	if (ferror(ifp))
		pfatal("can't read file %s", filename);

	if (revision != NULL) {
		if (!found_revision) {
			if (force) {
				if (verbose)
					say("Warning: this file doesn't appear "
					    "to be the %s version--patching anyway.\n",
					    revision);
			} else if (batch) {
				fatal("this file doesn't appear to be the "
				    "%s version--aborting.\n",
				    revision);
			} else {
				ask("This file doesn't appear to be the %s "
				    "version--patch anyway? [n] ",
				    revision);
				if (*buf != 'y')
					fatal("aborted\n");
			}
		} else if (verbose)
			say("Good.  This file appears to be the %s version.\n",
			    revision);
	}
	fseek(ifp, 0L, SEEK_SET);	/* rewind file */
	tireclen = maxlen;
	tibuflen = maxlen > BUFFERSIZE ? maxlen : BUFFERSIZE;
	lines_per_buf = tibuflen / maxlen;
	tibuf[0] = malloc(tibuflen + 1);
	if (tibuf[0] == NULL)
		fatal("out of memory\n");
	tibuf[1] = malloc(tibuflen + 1);
	if (tibuf[1] == NULL)
		fatal("out of memory\n");
	for (i = 1;; i++) {
		p = tibuf[0] + maxlen * (i % lines_per_buf);
		if (i % lines_per_buf == 0)	/* new block */
			if (write(tifd, tibuf[0], tibuflen) !=
			    (ssize_t) tibuflen)
				pfatal("can't write temp file");
		if (fgets(p, maxlen + 1, ifp) == NULL) {
			input_lines = i - 1;
			if (i % lines_per_buf != 0)
				if (write(tifd, tibuf[0], tibuflen) !=
				    (ssize_t) tibuflen)
					pfatal("can't write temp file");
			break;
		}
		j = strlen(p);
		/* These are '\n' terminated strings, so no need to add a NUL */
		if (j == 0 || p[j - 1] != '\n')
			p[j] = '\n';
	}
	fclose(ifp);
	close(tifd);
	if ((tifd = open(TMPINNAME, O_RDONLY)) < 0)
		pfatal("can't reopen file %s", TMPINNAME);
}

/*
 * Fetch a line from the input file, \n terminated, not necessarily \0.
 */
char *
ifetch(LINENUM line, int whichbuf)
{
	if (line < 1 || line > input_lines) {
		if (warn_on_invalid_line) {
			say("No such line %ld in input file, ignoring\n", line);
			warn_on_invalid_line = false;
		}
		return NULL;
	}
	if (using_plan_a)
		return i_ptr[line];
	else {
		LINENUM	offline = line % lines_per_buf;
		LINENUM	baseline = line - offline;

		if (tiline[0] == baseline)
			whichbuf = 0;
		else if (tiline[1] == baseline)
			whichbuf = 1;
		else {
			tiline[whichbuf] = baseline;

			if (lseek(tifd, (off_t) (baseline / lines_per_buf *
			    tibuflen), SEEK_SET) < 0)
				pfatal("cannot seek in the temporary input file");

			if (read(tifd, tibuf[whichbuf], tibuflen) !=
			    (ssize_t) tibuflen)
				pfatal("error reading tmp file %s", TMPINNAME);
		}
		return tibuf[whichbuf] + (tireclen * offline);
	}
}

/*
 * True if the string argument contains the revision number we want.
 */
static bool
rev_in_string(const char *string)
{
	const char	*s;
	size_t		patlen;

	if (revision == NULL)
		return true;
	patlen = strlen(revision);
	if (strnEQ(string, revision, patlen) && isspace((unsigned char)string[patlen]))
		return true;
	for (s = string; *s; s++) {
		if (isspace((unsigned char)*s) && strnEQ(s + 1, revision, patlen) &&
		    isspace((unsigned char)s[patlen + 1])) {
			return true;
		}
	}
	return false;
}
