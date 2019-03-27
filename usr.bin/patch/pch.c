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
 * $OpenBSD: pch.c,v 1.43 2014/11/18 17:03:35 tobias Exp $
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "util.h"
#include "pch.h"
#include "pathnames.h"

/* Patch (diff listing) abstract type. */

static off_t	p_filesize;	/* size of the patch file */
static LINENUM	p_first;	/* 1st line number */
static LINENUM	p_newfirst;	/* 1st line number of replacement */
static LINENUM	p_ptrn_lines;	/* # lines in pattern */
static LINENUM	p_repl_lines;	/* # lines in replacement text */
static LINENUM	p_end = -1;	/* last line in hunk */
static LINENUM	p_max;		/* max allowed value of p_end */
static LINENUM	p_context = 3;	/* # of context lines */
static LINENUM	p_input_line = 0;	/* current line # from patch file */
static char	**p_line = NULL;/* the text of the hunk */
static unsigned short	*p_len = NULL; /* length of each line */
static char	*p_char = NULL;	/* +, -, and ! */
static int	hunkmax = INITHUNKMAX;	/* size of above arrays to begin with */
static int	p_indent;	/* indent to patch */
static off_t	p_base;		/* where to intuit this time */
static LINENUM	p_bline;	/* line # of p_base */
static off_t	p_start;	/* where intuit found a patch */
static LINENUM	p_sline;	/* and the line number for it */
static LINENUM	p_hunk_beg;	/* line number of current hunk */
static LINENUM	p_efake = -1;	/* end of faked up lines--don't free */
static LINENUM	p_bfake = -1;	/* beg of faked up lines */
static FILE	*pfp = NULL;	/* patch file pointer */
static char	*bestguess = NULL;	/* guess at correct filename */

static void	grow_hunkmax(void);
static int	intuit_diff_type(void);
static void	next_intuit_at(off_t, LINENUM);
static void	skip_to(off_t, LINENUM);
static size_t	pgets(bool _do_indent);
static char	*best_name(const struct file_name *, bool);
static char	*posix_name(const struct file_name *, bool);
static size_t	num_components(const char *);
static LINENUM	strtolinenum(char *, char **);

/*
 * Prepare to look for the next patch in the patch file.
 */
void
re_patch(void)
{
	p_first = 0;
	p_newfirst = 0;
	p_ptrn_lines = 0;
	p_repl_lines = 0;
	p_end = (LINENUM) - 1;
	p_max = 0;
	p_indent = 0;
}

/*
 * Open the patch file at the beginning of time.
 */
void
open_patch_file(const char *filename)
{
	struct stat filestat;
	int nr, nw;

	if (filename == NULL || *filename == '\0' || strEQ(filename, "-")) {
		pfp = fopen(TMPPATNAME, "w");
		if (pfp == NULL)
			pfatal("can't create %s", TMPPATNAME);
		while ((nr = fread(buf, 1, buf_size, stdin)) > 0) {
			nw = fwrite(buf, 1, nr, pfp);
			if (nr != nw)
				pfatal("write error to %s", TMPPATNAME);
		}
		if (ferror(pfp) || fclose(pfp))
			pfatal("can't write %s", TMPPATNAME);
		filename = TMPPATNAME;
	}
	pfp = fopen(filename, "r");
	if (pfp == NULL)
		pfatal("patch file %s not found", filename);
	if (fstat(fileno(pfp), &filestat))
		pfatal("can't stat %s", filename);
	p_filesize = filestat.st_size;
	next_intuit_at(0, 1L);	/* start at the beginning */
	set_hunkmax();
}

/*
 * Make sure our dynamically realloced tables are malloced to begin with.
 */
void
set_hunkmax(void)
{
	if (p_line == NULL)
		p_line = malloc(hunkmax * sizeof(char *));
	if (p_len == NULL)
		p_len = malloc(hunkmax * sizeof(unsigned short));
	if (p_char == NULL)
		p_char = malloc(hunkmax * sizeof(char));
}

/*
 * Enlarge the arrays containing the current hunk of patch.
 */
static void
grow_hunkmax(void)
{
	int new_hunkmax = hunkmax * 2;

	if (p_line == NULL || p_len == NULL || p_char == NULL)
		fatal("Internal memory allocation error\n");

	p_line = reallocf(p_line, new_hunkmax * sizeof(char *));
	p_len = reallocf(p_len, new_hunkmax * sizeof(unsigned short));
	p_char = reallocf(p_char, new_hunkmax * sizeof(char));

	if (p_line != NULL && p_len != NULL && p_char != NULL) {
		hunkmax = new_hunkmax;
		return;
	}

	if (!using_plan_a)
		fatal("out of memory\n");
	out_of_mem = true;	/* whatever is null will be allocated again */
				/* from within plan_a(), of all places */
}

/* True if the remainder of the patch file contains a diff of some sort. */

bool
there_is_another_patch(void)
{
	bool exists = false;

	if (p_base != 0 && p_base >= p_filesize) {
		if (verbose)
			say("done\n");
		return false;
	}
	if (p_filesize == 0)
		return false;
	nonempty_patchf_seen = true;
	if (verbose)
		say("Hmm...");
	diff_type = intuit_diff_type();
	if (!diff_type) {
		if (p_base != 0) {
			if (verbose)
				say("  Ignoring the trailing garbage.\ndone\n");
		} else
			say("  I can't seem to find a patch in there anywhere.\n");
		return false;
	}
	if (verbose)
		say("  %sooks like %s to me...\n",
		    (p_base == 0 ? "L" : "The next patch l"),
		    diff_type == UNI_DIFF ? "a unified diff" :
		    diff_type == CONTEXT_DIFF ? "a context diff" :
		diff_type == NEW_CONTEXT_DIFF ? "a new-style context diff" :
		    diff_type == NORMAL_DIFF ? "a normal diff" :
		    "an ed script");
	if (p_indent && verbose)
		say("(Patch is indented %d space%s.)\n", p_indent,
		    p_indent == 1 ? "" : "s");
	skip_to(p_start, p_sline);
	while (filearg[0] == NULL) {
		if (force || batch) {
			say("No file to patch.  Skipping...\n");
			filearg[0] = xstrdup(bestguess);
			skip_rest_of_patch = true;
			return true;
		}
		ask("File to patch: ");
		if (*buf != '\n') {
			free(bestguess);
			bestguess = xstrdup(buf);
			filearg[0] = fetchname(buf, &exists, 0);
		}
		if (!exists) {
			int def_skip = *bestguess == '\0';
			ask("No file found--skip this patch? [%c] ",
			    def_skip  ? 'y' : 'n');
			if (*buf == 'n' || (!def_skip && *buf != 'y'))
				continue;
			if (verbose)
				say("Skipping patch...\n");
			free(filearg[0]);
			filearg[0] = fetchname(bestguess, &exists, 0);
			skip_rest_of_patch = true;
			return true;
		}
	}
	return true;
}

static void
p4_fetchname(struct file_name *name, char *str)
{
	char *t, *h;

	/* Skip leading whitespace. */
	while (isspace((unsigned char)*str))
		str++;

	/* Remove the file revision number. */
	for (t = str, h = NULL; *t != '\0' && !isspace((unsigned char)*t); t++)
		if (*t == '#')
			h = t;
	if (h != NULL)
		*h = '\0';

	name->path = fetchname(str, &name->exists, strippath);
}

/* Determine what kind of diff is in the remaining part of the patch file. */

static int
intuit_diff_type(void)
{
	off_t	this_line = 0, previous_line;
	off_t	first_command_line = -1;
	LINENUM	fcl_line = -1;
	bool	last_line_was_command = false, this_is_a_command = false;
	bool	stars_last_line = false, stars_this_line = false;
	char	*s, *t;
	int	indent, retval;
	struct file_name names[MAX_FILE];
	int	piece_of_git = 0;

	memset(names, 0, sizeof(names));
	ok_to_create_file = false;
	fseeko(pfp, p_base, SEEK_SET);
	p_input_line = p_bline - 1;
	for (;;) {
		previous_line = this_line;
		last_line_was_command = this_is_a_command;
		stars_last_line = stars_this_line;
		this_line = ftello(pfp);
		indent = 0;
		p_input_line++;
		if (pgets(false) == 0) {
			if (first_command_line >= 0) {
				/* nothing but deletes!? */
				p_start = first_command_line;
				p_sline = fcl_line;
				retval = ED_DIFF;
				goto scan_exit;
			} else {
				p_start = this_line;
				p_sline = p_input_line;
				retval = 0;
				goto scan_exit;
			}
		}
		for (s = buf; *s == ' ' || *s == '\t' || *s == 'X'; s++) {
			if (*s == '\t')
				indent += 8 - (indent % 8);
			else
				indent++;
		}
		for (t = s; isdigit((unsigned char)*t) || *t == ','; t++)
			;
		this_is_a_command = (isdigit((unsigned char)*s) &&
		    (*t == 'd' || *t == 'c' || *t == 'a'));
		if (first_command_line < 0 && this_is_a_command) {
			first_command_line = this_line;
			fcl_line = p_input_line;
			p_indent = indent;	/* assume this for now */
		}
		if (!stars_last_line && strnEQ(s, "*** ", 4))
			names[OLD_FILE].path = fetchname(s + 4,
			    &names[OLD_FILE].exists, strippath);
		else if (strnEQ(s, "--- ", 4)) {
			size_t off = 4;
			if (piece_of_git && strippath == 957 &&
			    strnEQ(s, "--- a/", 6))
				off = 6;
			names[NEW_FILE].path = fetchname(s + off,
			    &names[NEW_FILE].exists, strippath);
		} else if (strnEQ(s, "+++ ", 4)) {
			/* pretend it is the old name */
			size_t off = 4;
			if (piece_of_git && strippath == 957 &&
			    strnEQ(s, "+++ b/", 6))
				off = 6;
			names[OLD_FILE].path = fetchname(s + off,
			    &names[OLD_FILE].exists, strippath);
		} else if (strnEQ(s, "Index:", 6))
			names[INDEX_FILE].path = fetchname(s + 6,
			    &names[INDEX_FILE].exists, strippath);
		else if (strnEQ(s, "Prereq:", 7)) {
			for (t = s + 7; isspace((unsigned char)*t); t++)
				;
			revision = xstrdup(t);
			for (t = revision;
			     *t && !isspace((unsigned char)*t); t++)
				;
			*t = '\0';
			if (*revision == '\0') {
				free(revision);
				revision = NULL;
			}
		} else if (strnEQ(s, "diff --git a/", 13)) {
			/* Git-style diffs. */
			piece_of_git = 1;
		} else if (strnEQ(s, "==== ", 5)) {
			/* Perforce-style diffs. */
			if ((t = strstr(s + 5, " - ")) != NULL)
				p4_fetchname(&names[NEW_FILE], t + 3);
			p4_fetchname(&names[OLD_FILE], s + 5);
		}
		if ((!diff_type || diff_type == ED_DIFF) &&
		    first_command_line >= 0 &&
		    strEQ(s, ".\n")) {
			p_indent = indent;
			p_start = first_command_line;
			p_sline = fcl_line;
			retval = ED_DIFF;
			goto scan_exit;
		}
		if ((!diff_type || diff_type == UNI_DIFF) && strnEQ(s, "@@ -", 4)) {
			if (strnEQ(s + 4, "0,0", 3))
				ok_to_create_file = true;
			p_indent = indent;
			p_start = this_line;
			p_sline = p_input_line;
			retval = UNI_DIFF;
			goto scan_exit;
		}
		stars_this_line = strnEQ(s, "********", 8);
		if ((!diff_type || diff_type == CONTEXT_DIFF) && stars_last_line &&
		    strnEQ(s, "*** ", 4)) {
			if (strtolinenum(s + 4, &s) == 0)
				ok_to_create_file = true;
			/*
			 * If this is a new context diff the character just
			 * at the end of the line is a '*'.
			 */
			while (*s && *s != '\n')
				s++;
			p_indent = indent;
			p_start = previous_line;
			p_sline = p_input_line - 1;
			retval = (*(s - 1) == '*' ? NEW_CONTEXT_DIFF : CONTEXT_DIFF);
			goto scan_exit;
		}
		if ((!diff_type || diff_type == NORMAL_DIFF) &&
		    last_line_was_command &&
		    (strnEQ(s, "< ", 2) || strnEQ(s, "> ", 2))) {
			p_start = previous_line;
			p_sline = p_input_line - 1;
			p_indent = indent;
			retval = NORMAL_DIFF;
			goto scan_exit;
		}
	}
scan_exit:
	if (retval == UNI_DIFF) {
		/* unswap old and new */
		struct file_name tmp = names[OLD_FILE];
		names[OLD_FILE] = names[NEW_FILE];
		names[NEW_FILE] = tmp;
	}
	if (filearg[0] == NULL) {
		if (posix)
			filearg[0] = posix_name(names, ok_to_create_file);
		else {
			/* Ignore the Index: name for context diffs, like GNU */
			if (names[OLD_FILE].path != NULL ||
			    names[NEW_FILE].path != NULL) {
				free(names[INDEX_FILE].path);
				names[INDEX_FILE].path = NULL;
			}
			filearg[0] = best_name(names, ok_to_create_file);
		}
	}

	free(bestguess);
	bestguess = NULL;
	if (filearg[0] != NULL)
		bestguess = xstrdup(filearg[0]);
	else if (!ok_to_create_file) {
		/*
		 * We don't want to create a new file but we need a
		 * filename to set bestguess.  Avoid setting filearg[0]
		 * so the file is not created automatically.
		 */
		if (posix)
			bestguess = posix_name(names, true);
		else
			bestguess = best_name(names, true);
	}
	free(names[OLD_FILE].path);
	free(names[NEW_FILE].path);
	free(names[INDEX_FILE].path);
	return retval;
}

/*
 * Remember where this patch ends so we know where to start up again.
 */
static void
next_intuit_at(off_t file_pos, LINENUM file_line)
{
	p_base = file_pos;
	p_bline = file_line;
}

/*
 * Basically a verbose fseeko() to the actual diff listing.
 */
static void
skip_to(off_t file_pos, LINENUM file_line)
{
	size_t	len;

	if (p_base > file_pos)
		fatal("Internal error: seek %lld>%lld\n",
		   (long long)p_base, (long long)file_pos);
	if (verbose && p_base < file_pos) {
		fseeko(pfp, p_base, SEEK_SET);
		say("The text leading up to this was:\n--------------------------\n");
		while (ftello(pfp) < file_pos) {
			len = pgets(false);
			if (len == 0)
				fatal("Unexpected end of file\n");
			say("|%s", buf);
		}
		say("--------------------------\n");
	} else
		fseeko(pfp, file_pos, SEEK_SET);
	p_input_line = file_line - 1;
}

/* Make this a function for better debugging.  */
static void
malformed(void)
{
	fatal("malformed patch at line %ld: %s", p_input_line, buf);
	/* about as informative as "Syntax error" in C */
}

/*
 * True if the line has been discarded (i.e. it is a line saying
 *  "\ No newline at end of file".)
 */
static bool
remove_special_line(void)
{
	int	c;

	c = fgetc(pfp);
	if (c == '\\') {
		do {
			c = fgetc(pfp);
		} while (c != EOF && c != '\n');

		return true;
	}
	if (c != EOF)
		fseeko(pfp, -1, SEEK_CUR);

	return false;
}

/*
 * True if there is more of the current diff listing to process.
 */
bool
another_hunk(void)
{
	off_t	line_beginning;			/* file pos of the current line */
	LINENUM	repl_beginning;			/* index of --- line */
	LINENUM	fillcnt;			/* #lines of missing ptrn or repl */
	LINENUM	fillsrc;			/* index of first line to copy */
	LINENUM	filldst;			/* index of first missing line */
	bool	ptrn_spaces_eaten;		/* ptrn was slightly malformed */
	bool	repl_could_be_missing;		/* no + or ! lines in this hunk */
	bool	repl_missing;			/* we are now backtracking */
	off_t	repl_backtrack_position;	/* file pos of first repl line */
	LINENUM	repl_patch_line;		/* input line number for same */
	LINENUM	ptrn_copiable;			/* # of copiable lines in ptrn */
	char	*s;
	size_t	len;
	int	context = 0;

	while (p_end >= 0) {
		if (p_end == p_efake)
			p_end = p_bfake;	/* don't free twice */
		else
			free(p_line[p_end]);
		p_end--;
	}
	p_efake = -1;

	p_max = hunkmax;	/* gets reduced when --- found */
	if (diff_type == CONTEXT_DIFF || diff_type == NEW_CONTEXT_DIFF) {
		line_beginning = ftello(pfp);
		repl_beginning = 0;
		fillcnt = 0;
		fillsrc = 0;
		filldst = 0;
		ptrn_spaces_eaten = false;
		repl_could_be_missing = true;
		repl_missing = false;
		repl_backtrack_position = 0;
		repl_patch_line = 0;
		ptrn_copiable = 0;

		len = pgets(true);
		p_input_line++;
		if (len == 0 || strnNE(buf, "********", 8)) {
			next_intuit_at(line_beginning, p_input_line);
			return false;
		}
		p_context = 100;
		p_hunk_beg = p_input_line + 1;
		while (p_end < p_max) {
			line_beginning = ftello(pfp);
			len = pgets(true);
			p_input_line++;
			if (len == 0) {
				if (p_max - p_end < 4) {
					/* assume blank lines got chopped */
					strlcpy(buf, "  \n", buf_size);
				} else {
					if (repl_beginning && repl_could_be_missing) {
						repl_missing = true;
						goto hunk_done;
					}
					fatal("unexpected end of file in patch\n");
				}
			}
			p_end++;
			if (p_end >= hunkmax)
				fatal("Internal error: hunk larger than hunk "
				    "buffer size");
			p_char[p_end] = *buf;
			p_line[p_end] = NULL;
			switch (*buf) {
			case '*':
				if (strnEQ(buf, "********", 8)) {
					if (repl_beginning && repl_could_be_missing) {
						repl_missing = true;
						goto hunk_done;
					} else
						fatal("unexpected end of hunk "
						    "at line %ld\n",
						    p_input_line);
				}
				if (p_end != 0) {
					if (repl_beginning && repl_could_be_missing) {
						repl_missing = true;
						goto hunk_done;
					}
					fatal("unexpected *** at line %ld: %s",
					    p_input_line, buf);
				}
				context = 0;
				p_line[p_end] = savestr(buf);
				if (out_of_mem) {
					p_end--;
					return false;
				}
				for (s = buf;
				     *s && !isdigit((unsigned char)*s); s++)
					;
				if (!*s)
					malformed();
				if (strnEQ(s, "0,0", 3))
					memmove(s, s + 2, strlen(s + 2) + 1);
				p_first = strtolinenum(s, &s);
				if (*s == ',') {
					for (;
					     *s && !isdigit((unsigned char)*s); s++)
						;
					if (!*s)
						malformed();
					p_ptrn_lines = strtolinenum(s, &s) - p_first + 1;
					if (p_ptrn_lines < 0)
						malformed();
				} else if (p_first)
					p_ptrn_lines = 1;
				else {
					p_ptrn_lines = 0;
					p_first = 1;
				}
				if (p_first >= LINENUM_MAX - p_ptrn_lines ||
				    p_ptrn_lines >= LINENUM_MAX - 6)
					malformed();

				/* we need this much at least */
				p_max = p_ptrn_lines + 6;
				while (p_max >= hunkmax)
					grow_hunkmax();
				p_max = hunkmax;
				break;
			case '-':
				if (buf[1] == '-') {
					if (repl_beginning ||
					    (p_end != p_ptrn_lines + 1 +
					    (p_char[p_end - 1] == '\n'))) {
						if (p_end == 1) {
							/*
							 * `old' lines were omitted;
							 * set up to fill them in
							 * from 'new' context lines.
							 */
							p_end = p_ptrn_lines + 1;
							fillsrc = p_end + 1;
							filldst = 1;
							fillcnt = p_ptrn_lines;
						} else {
							if (repl_beginning) {
								if (repl_could_be_missing) {
									repl_missing = true;
									goto hunk_done;
								}
								fatal("duplicate \"---\" at line %ld--check line numbers at line %ld\n",
								    p_input_line, p_hunk_beg + repl_beginning);
							} else {
								fatal("%s \"---\" at line %ld--check line numbers at line %ld\n",
								    (p_end <= p_ptrn_lines
								    ? "Premature"
								    : "Overdue"),
								    p_input_line, p_hunk_beg);
							}
						}
					}
					repl_beginning = p_end;
					repl_backtrack_position = ftello(pfp);
					repl_patch_line = p_input_line;
					p_line[p_end] = savestr(buf);
					if (out_of_mem) {
						p_end--;
						return false;
					}
					p_char[p_end] = '=';
					for (s = buf; *s && !isdigit((unsigned char)*s); s++)
						;
					if (!*s)
						malformed();
					p_newfirst = strtolinenum(s, &s);
					if (*s == ',') {
						for (; *s && !isdigit((unsigned char)*s); s++)
							;
						if (!*s)
							malformed();
						p_repl_lines = strtolinenum(s, &s) -
						    p_newfirst + 1;
						if (p_repl_lines < 0)
							malformed();
					} else if (p_newfirst)
						p_repl_lines = 1;
					else {
						p_repl_lines = 0;
						p_newfirst = 1;
					}
					if (p_newfirst >= LINENUM_MAX - p_repl_lines ||
					    p_repl_lines >= LINENUM_MAX - p_end)
						malformed();
					p_max = p_repl_lines + p_end;
					if (p_max > MAXHUNKSIZE)
						fatal("hunk too large (%ld lines) at line %ld: %s",
						    p_max, p_input_line, buf);
					while (p_max >= hunkmax)
						grow_hunkmax();
					if (p_repl_lines != ptrn_copiable &&
					    (p_context != 0 || p_repl_lines != 1))
						repl_could_be_missing = false;
					break;
				}
				goto change_line;
			case '+':
			case '!':
				repl_could_be_missing = false;
		change_line:
				if (buf[1] == '\n' && canonicalize)
					strlcpy(buf + 1, " \n", buf_size - 1);
				if (!isspace((unsigned char)buf[1]) &&
				    buf[1] != '>' && buf[1] != '<' &&
				    repl_beginning && repl_could_be_missing) {
					repl_missing = true;
					goto hunk_done;
				}
				if (context >= 0) {
					if (context < p_context)
						p_context = context;
					context = -1000;
				}
				p_line[p_end] = savestr(buf + 2);
				if (out_of_mem) {
					p_end--;
					return false;
				}
				if (p_end == p_ptrn_lines) {
					if (remove_special_line()) {
						int	l;

						l = strlen(p_line[p_end]) - 1;
						(p_line[p_end])[l] = 0;
					}
				}
				break;
			case '\t':
			case '\n':	/* assume the 2 spaces got eaten */
				if (repl_beginning && repl_could_be_missing &&
				    (!ptrn_spaces_eaten ||
				    diff_type == NEW_CONTEXT_DIFF)) {
					repl_missing = true;
					goto hunk_done;
				}
				p_line[p_end] = savestr(buf);
				if (out_of_mem) {
					p_end--;
					return false;
				}
				if (p_end != p_ptrn_lines + 1) {
					ptrn_spaces_eaten |= (repl_beginning != 0);
					context++;
					if (!repl_beginning)
						ptrn_copiable++;
					p_char[p_end] = ' ';
				}
				break;
			case ' ':
				if (!isspace((unsigned char)buf[1]) &&
				    repl_beginning && repl_could_be_missing) {
					repl_missing = true;
					goto hunk_done;
				}
				context++;
				if (!repl_beginning)
					ptrn_copiable++;
				p_line[p_end] = savestr(buf + 2);
				if (out_of_mem) {
					p_end--;
					return false;
				}
				break;
			default:
				if (repl_beginning && repl_could_be_missing) {
					repl_missing = true;
					goto hunk_done;
				}
				malformed();
			}
			/* set up p_len for strncmp() so we don't have to */
			/* assume null termination */
			if (p_line[p_end])
				p_len[p_end] = strlen(p_line[p_end]);
			else
				p_len[p_end] = 0;
		}

hunk_done:
		if (p_end >= 0 && !repl_beginning)
			fatal("no --- found in patch at line %ld\n", pch_hunk_beg());

		if (repl_missing) {

			/* reset state back to just after --- */
			p_input_line = repl_patch_line;
			for (p_end--; p_end > repl_beginning; p_end--)
				free(p_line[p_end]);
			fseeko(pfp, repl_backtrack_position, SEEK_SET);

			/* redundant 'new' context lines were omitted - set */
			/* up to fill them in from the old file context */
			if (!p_context && p_repl_lines == 1) {
				p_repl_lines = 0;
				p_max--;
			}
			fillsrc = 1;
			filldst = repl_beginning + 1;
			fillcnt = p_repl_lines;
			p_end = p_max;
		} else if (!p_context && fillcnt == 1) {
			/* the first hunk was a null hunk with no context */
			/* and we were expecting one line -- fix it up. */
			while (filldst < p_end) {
				p_line[filldst] = p_line[filldst + 1];
				p_char[filldst] = p_char[filldst + 1];
				p_len[filldst] = p_len[filldst + 1];
				filldst++;
			}
#if 0
			repl_beginning--;	/* this doesn't need to be fixed */
#endif
			p_end--;
			p_first++;	/* do append rather than insert */
			fillcnt = 0;
			p_ptrn_lines = 0;
		}
		if (diff_type == CONTEXT_DIFF &&
		    (fillcnt || (p_first > 1 && ptrn_copiable > 2 * p_context))) {
			if (verbose)
				say("%s\n%s\n%s\n",
				    "(Fascinating--this is really a new-style context diff but without",
				    "the telltale extra asterisks on the *** line that usually indicate",
				    "the new style...)");
			diff_type = NEW_CONTEXT_DIFF;
		}
		/* if there were omitted context lines, fill them in now */
		if (fillcnt) {
			p_bfake = filldst;	/* remember where not to free() */
			p_efake = filldst + fillcnt - 1;
			while (fillcnt-- > 0) {
				while (fillsrc <= p_end && p_char[fillsrc] != ' ')
					fillsrc++;
				if (fillsrc > p_end)
					fatal("replacement text or line numbers mangled in hunk at line %ld\n",
					    p_hunk_beg);
				p_line[filldst] = p_line[fillsrc];
				p_char[filldst] = p_char[fillsrc];
				p_len[filldst] = p_len[fillsrc];
				fillsrc++;
				filldst++;
			}
			while (fillsrc <= p_end && fillsrc != repl_beginning &&
			    p_char[fillsrc] != ' ')
				fillsrc++;
#ifdef DEBUGGING
			if (debug & 64)
				printf("fillsrc %ld, filldst %ld, rb %ld, e+1 %ld\n",
				fillsrc, filldst, repl_beginning, p_end + 1);
#endif
			if (fillsrc != p_end + 1 && fillsrc != repl_beginning)
				malformed();
			if (filldst != p_end + 1 && filldst != repl_beginning)
				malformed();
		}
		if (p_line[p_end] != NULL) {
			if (remove_special_line()) {
				p_len[p_end] -= 1;
				(p_line[p_end])[p_len[p_end]] = 0;
			}
		}
	} else if (diff_type == UNI_DIFF) {
		LINENUM	fillold;	/* index of old lines */
		LINENUM	fillnew;	/* index of new lines */
		char	ch;

		line_beginning = ftello(pfp); /* file pos of the current line */
		len = pgets(true);
		p_input_line++;
		if (len == 0 || strnNE(buf, "@@ -", 4)) {
			next_intuit_at(line_beginning, p_input_line);
			return false;
		}
		s = buf + 4;
		if (!*s)
			malformed();
		p_first = strtolinenum(s, &s);
		if (*s == ',') {
			p_ptrn_lines = strtolinenum(s + 1, &s);
		} else
			p_ptrn_lines = 1;
		if (*s == ' ')
			s++;
		if (*s != '+' || !*++s)
			malformed();
		p_newfirst = strtolinenum(s, &s);
		if (*s == ',') {
			p_repl_lines = strtolinenum(s + 1, &s);
		} else
			p_repl_lines = 1;
		if (*s == ' ')
			s++;
		if (*s != '@')
			malformed();
		if (p_first >= LINENUM_MAX - p_ptrn_lines ||
		    p_newfirst > LINENUM_MAX - p_repl_lines ||
		    p_ptrn_lines >= LINENUM_MAX - p_repl_lines - 1)
			malformed();
		if (!p_ptrn_lines)
			p_first++;	/* do append rather than insert */
		p_max = p_ptrn_lines + p_repl_lines + 1;
		while (p_max >= hunkmax)
			grow_hunkmax();
		fillold = 1;
		fillnew = fillold + p_ptrn_lines;
		p_end = fillnew + p_repl_lines;
		snprintf(buf, buf_size, "*** %ld,%ld ****\n", p_first,
		    p_first + p_ptrn_lines - 1);
		p_line[0] = savestr(buf);
		if (out_of_mem) {
			p_end = -1;
			return false;
		}
		p_char[0] = '*';
		snprintf(buf, buf_size, "--- %ld,%ld ----\n", p_newfirst,
		    p_newfirst + p_repl_lines - 1);
		p_line[fillnew] = savestr(buf);
		if (out_of_mem) {
			p_end = 0;
			return false;
		}
		p_char[fillnew++] = '=';
		p_context = 100;
		context = 0;
		p_hunk_beg = p_input_line + 1;
		while (fillold <= p_ptrn_lines || fillnew <= p_end) {
			line_beginning = ftello(pfp);
			len = pgets(true);
			p_input_line++;
			if (len == 0) {
				if (p_max - fillnew < 3) {
					/* assume blank lines got chopped */
					strlcpy(buf, " \n", buf_size);
				} else {
					fatal("unexpected end of file in patch\n");
				}
			}
			if (*buf == '\t' || *buf == '\n') {
				ch = ' ';	/* assume the space got eaten */
				s = savestr(buf);
			} else {
				ch = *buf;
				s = savestr(buf + 1);
			}
			if (out_of_mem) {
				while (--fillnew > p_ptrn_lines)
					free(p_line[fillnew]);
				p_end = fillold - 1;
				return false;
			}
			switch (ch) {
			case '-':
				if (fillold > p_ptrn_lines) {
					free(s);
					p_end = fillnew - 1;
					malformed();
				}
				p_char[fillold] = ch;
				p_line[fillold] = s;
				p_len[fillold++] = strlen(s);
				if (fillold > p_ptrn_lines) {
					if (remove_special_line()) {
						p_len[fillold - 1] -= 1;
						s[p_len[fillold - 1]] = 0;
					}
				}
				break;
			case '=':
				ch = ' ';
				/* FALL THROUGH */
			case ' ':
				if (fillold > p_ptrn_lines) {
					free(s);
					while (--fillnew > p_ptrn_lines)
						free(p_line[fillnew]);
					p_end = fillold - 1;
					malformed();
				}
				context++;
				p_char[fillold] = ch;
				p_line[fillold] = s;
				p_len[fillold++] = strlen(s);
				s = savestr(s);
				if (out_of_mem) {
					while (--fillnew > p_ptrn_lines)
						free(p_line[fillnew]);
					p_end = fillold - 1;
					return false;
				}
				if (fillold > p_ptrn_lines) {
					if (remove_special_line()) {
						p_len[fillold - 1] -= 1;
						s[p_len[fillold - 1]] = 0;
					}
				}
				/* FALL THROUGH */
			case '+':
				if (fillnew > p_end) {
					free(s);
					while (--fillnew > p_ptrn_lines)
						free(p_line[fillnew]);
					p_end = fillold - 1;
					malformed();
				}
				p_char[fillnew] = ch;
				p_line[fillnew] = s;
				p_len[fillnew++] = strlen(s);
				if (fillold > p_ptrn_lines) {
					if (remove_special_line()) {
						p_len[fillnew - 1] -= 1;
						s[p_len[fillnew - 1]] = 0;
					}
				}
				break;
			default:
				p_end = fillnew;
				malformed();
			}
			if (ch != ' ' && context > 0) {
				if (context < p_context)
					p_context = context;
				context = -1000;
			}
		}		/* while */
	} else {		/* normal diff--fake it up */
		char	hunk_type;
		int	i;
		LINENUM	min, max;

		line_beginning = ftello(pfp);
		p_context = 0;
		len = pgets(true);
		p_input_line++;
		if (len == 0 || !isdigit((unsigned char)*buf)) {
			next_intuit_at(line_beginning, p_input_line);
			return false;
		}
		p_first = strtolinenum(buf, &s);
		if (*s == ',') {
			p_ptrn_lines = strtolinenum(s + 1, &s) - p_first + 1;
			if (p_ptrn_lines < 0)
				malformed();
		} else
			p_ptrn_lines = (*s != 'a');
		hunk_type = *s;
		if (hunk_type == 'a')
			p_first++;	/* do append rather than insert */
		min = strtolinenum(s + 1, &s);
		if (*s == ',')
			max = strtolinenum(s + 1, &s);
		else
			max = min;
		if (min < 0 || min > max || max - min == LINENUM_MAX)
			malformed();
		if (hunk_type == 'd')
			min++;
		p_newfirst = min;
		p_repl_lines = max - min + 1;
		if (p_newfirst > LINENUM_MAX - p_repl_lines ||
		    p_ptrn_lines >= LINENUM_MAX - p_repl_lines - 1)
			malformed();
		p_end = p_ptrn_lines + p_repl_lines + 1;
		if (p_end > MAXHUNKSIZE)
			fatal("hunk too large (%ld lines) at line %ld: %s",
			    p_end, p_input_line, buf);
		while (p_end >= hunkmax)
			grow_hunkmax();
		snprintf(buf, buf_size, "*** %ld,%ld\n", p_first,
		    p_first + p_ptrn_lines - 1);
		p_line[0] = savestr(buf);
		if (out_of_mem) {
			p_end = -1;
			return false;
		}
		p_char[0] = '*';
		for (i = 1; i <= p_ptrn_lines; i++) {
			len = pgets(true);
			p_input_line++;
			if (len == 0)
				fatal("unexpected end of file in patch at line %ld\n",
				    p_input_line);
			if (*buf != '<')
				fatal("< expected at line %ld of patch\n",
				    p_input_line);
			p_line[i] = savestr(buf + 2);
			if (out_of_mem) {
				p_end = i - 1;
				return false;
			}
			p_len[i] = strlen(p_line[i]);
			p_char[i] = '-';
		}

		if (remove_special_line()) {
			p_len[i - 1] -= 1;
			(p_line[i - 1])[p_len[i - 1]] = 0;
		}
		if (hunk_type == 'c') {
			len = pgets(true);
			p_input_line++;
			if (len == 0)
				fatal("unexpected end of file in patch at line %ld\n",
				    p_input_line);
			if (*buf != '-')
				fatal("--- expected at line %ld of patch\n",
				    p_input_line);
		}
		snprintf(buf, buf_size, "--- %ld,%ld\n", min, max);
		p_line[i] = savestr(buf);
		if (out_of_mem) {
			p_end = i - 1;
			return false;
		}
		p_char[i] = '=';
		for (i++; i <= p_end; i++) {
			len = pgets(true);
			p_input_line++;
			if (len == 0)
				fatal("unexpected end of file in patch at line %ld\n",
				    p_input_line);
			if (*buf != '>')
				fatal("> expected at line %ld of patch\n",
				    p_input_line);
			/* Don't overrun if we don't have enough line */
			if (len > 2)
				p_line[i] = savestr(buf + 2);
			else
				p_line[i] = savestr("");

			if (out_of_mem) {
				p_end = i - 1;
				return false;
			}
			p_len[i] = strlen(p_line[i]);
			p_char[i] = '+';
		}

		if (remove_special_line()) {
			p_len[i - 1] -= 1;
			(p_line[i - 1])[p_len[i - 1]] = 0;
		}
	}
	if (reverse)		/* backwards patch? */
		if (!pch_swap())
			say("Not enough memory to swap next hunk!\n");
#ifdef DEBUGGING
	if (debug & 2) {
		LINENUM	i;
		char	special;

		for (i = 0; i <= p_end; i++) {
			if (i == p_ptrn_lines)
				special = '^';
			else
				special = ' ';
			fprintf(stderr, "%3ld %c %c %s", i, p_char[i],
			    special, p_line[i]);
			fflush(stderr);
		}
	}
#endif
	if (p_end + 1 < hunkmax)/* paranoia reigns supreme... */
		p_char[p_end + 1] = '^';	/* add a stopper for apply_hunk */
	return true;
}

/*
 * Input a line from the patch file.
 * Worry about indentation if do_indent is true.
 * The line is read directly into the buf global variable which
 * is resized if necessary in order to hold the complete line.
 * Returns the number of characters read including the terminating
 * '\n', if any.
 */
size_t
pgets(bool do_indent)
{
	char *line;
	size_t len;
	int indent = 0, skipped = 0;

	line = fgetln(pfp, &len);
	if (line != NULL) {
		if (len + 1 > buf_size) {
			while (len + 1 > buf_size)
				buf_size *= 2;
			free(buf);
			buf = malloc(buf_size);
			if (buf == NULL)
				fatal("out of memory\n");
		}
		if (do_indent == 1 && p_indent) {
			for (;
			    indent < p_indent && (*line == ' ' || *line == '\t' || *line == 'X');
			    line++, skipped++) {
				if (*line == '\t')
					indent += 8 - (indent %7);
				else
					indent++;
			}
		}
		memcpy(buf, line, len - skipped);
		buf[len - skipped] = '\0';
	}
	return len;
}


/*
 * Reverse the old and new portions of the current hunk.
 */
bool
pch_swap(void)
{
	char	**tp_line;	/* the text of the hunk */
	unsigned short	*tp_len;/* length of each line */
	char	*tp_char;	/* +, -, and ! */
	LINENUM	i;
	LINENUM	n;
	bool	blankline = false;
	char	*s;

	i = p_first;
	p_first = p_newfirst;
	p_newfirst = i;

	/* make a scratch copy */

	tp_line = p_line;
	tp_len = p_len;
	tp_char = p_char;
	p_line = NULL;	/* force set_hunkmax to allocate again */
	p_len = NULL;
	p_char = NULL;
	set_hunkmax();
	if (p_line == NULL || p_len == NULL || p_char == NULL) {

		free(p_line);
		p_line = tp_line;
		free(p_len);
		p_len = tp_len;
		free(p_char);
		p_char = tp_char;
		return false;	/* not enough memory to swap hunk! */
	}
	/* now turn the new into the old */

	i = p_ptrn_lines + 1;
	if (tp_char[i] == '\n') {	/* account for possible blank line */
		blankline = true;
		i++;
	}
	if (p_efake >= 0) {	/* fix non-freeable ptr range */
		if (p_efake <= i)
			n = p_end - i + 1;
		else
			n = -i;
		p_efake += n;
		p_bfake += n;
	}
	for (n = 0; i <= p_end; i++, n++) {
		p_line[n] = tp_line[i];
		p_char[n] = tp_char[i];
		if (p_char[n] == '+')
			p_char[n] = '-';
		p_len[n] = tp_len[i];
	}
	if (blankline) {
		i = p_ptrn_lines + 1;
		p_line[n] = tp_line[i];
		p_char[n] = tp_char[i];
		p_len[n] = tp_len[i];
		n++;
	}
	if (p_char[0] != '=')
		fatal("Malformed patch at line %ld: expected '=' found '%c'\n",
		    p_input_line, p_char[0]);
	p_char[0] = '*';
	for (s = p_line[0]; *s; s++)
		if (*s == '-')
			*s = '*';

	/* now turn the old into the new */

	if (p_char[0] != '*')
		fatal("Malformed patch at line %ld: expected '*' found '%c'\n",
		    p_input_line, p_char[0]);
	tp_char[0] = '=';
	for (s = tp_line[0]; *s; s++)
		if (*s == '*')
			*s = '-';
	for (i = 0; n <= p_end; i++, n++) {
		p_line[n] = tp_line[i];
		p_char[n] = tp_char[i];
		if (p_char[n] == '-')
			p_char[n] = '+';
		p_len[n] = tp_len[i];
	}

	if (i != p_ptrn_lines + 1)
		fatal("Malformed patch at line %ld: expected %ld lines, "
		    "got %ld\n",
		    p_input_line, p_ptrn_lines + 1, i);

	i = p_ptrn_lines;
	p_ptrn_lines = p_repl_lines;
	p_repl_lines = i;

	free(tp_line);
	free(tp_len);
	free(tp_char);

	return true;
}

/*
 * Return the specified line position in the old file of the old context.
 */
LINENUM
pch_first(void)
{
	return p_first;
}

/*
 * Return the number of lines of old context.
 */
LINENUM
pch_ptrn_lines(void)
{
	return p_ptrn_lines;
}

/*
 * Return the probable line position in the new file of the first line.
 */
LINENUM
pch_newfirst(void)
{
	return p_newfirst;
}

/*
 * Return the number of lines in the replacement text including context.
 */
LINENUM
pch_repl_lines(void)
{
	return p_repl_lines;
}

/*
 * Return the number of lines in the whole hunk.
 */
LINENUM
pch_end(void)
{
	return p_end;
}

/*
 * Return the number of context lines before the first changed line.
 */
LINENUM
pch_context(void)
{
	return p_context;
}

/*
 * Return the length of a particular patch line.
 */
unsigned short
pch_line_len(LINENUM line)
{
	return p_len[line];
}

/*
 * Return the control character (+, -, *, !, etc) for a patch line.
 */
char
pch_char(LINENUM line)
{
	return p_char[line];
}

/*
 * Return a pointer to a particular patch line.
 */
char *
pfetch(LINENUM line)
{
	return p_line[line];
}

/*
 * Return where in the patch file this hunk began, for error messages.
 */
LINENUM
pch_hunk_beg(void)
{
	return p_hunk_beg;
}

/*
 * Apply an ed script by feeding ed itself.
 */
void
do_ed_script(void)
{
	char	*t;
	off_t	beginning_of_this_line;
	FILE	*pipefp = NULL;
	int	continuation;

	if (!skip_rest_of_patch) {
		if (copy_file(filearg[0], TMPOUTNAME) < 0) {
			unlink(TMPOUTNAME);
			fatal("can't create temp file %s", TMPOUTNAME);
		}
		snprintf(buf, buf_size, "%s%s%s", _PATH_RED,
		    verbose ? " " : " -s ", TMPOUTNAME);
		pipefp = popen(buf, "w");
	}
	for (;;) {
		beginning_of_this_line = ftello(pfp);
		if (pgets(true) == 0) {
			next_intuit_at(beginning_of_this_line, p_input_line);
			break;
		}
		p_input_line++;
		for (t = buf; isdigit((unsigned char)*t) || *t == ','; t++)
			;
		/* POSIX defines allowed commands as {a,c,d,i,s} */
		if (isdigit((unsigned char)*buf) &&
		    (*t == 'a' || *t == 'c' || *t == 'd' || *t == 'i' || *t == 's')) {
			if (pipefp != NULL)
				fputs(buf, pipefp);
			if (*t == 's') {
				for (;;) {
					continuation = 0;
					t = strchr(buf, '\0') - 1;
					while (--t >= buf && *t == '\\')
						continuation = !continuation;
					if (!continuation ||
					    pgets(true) == 0)
						break;
					if (pipefp != NULL)
						fputs(buf, pipefp);
				}
			} else if (*t != 'd') {
				while (pgets(true)) {
					p_input_line++;
					if (pipefp != NULL)
						fputs(buf, pipefp);
					if (strEQ(buf, ".\n"))
						break;
				}
			}
		} else {
			next_intuit_at(beginning_of_this_line, p_input_line);
			break;
		}
	}
	if (pipefp == NULL)
		return;
	fprintf(pipefp, "w\n");
	fprintf(pipefp, "q\n");
	fflush(pipefp);
	pclose(pipefp);
	ignore_signals();
	if (!check_only) {
		if (move_file(TMPOUTNAME, outname) < 0) {
			toutkeep = true;
			chmod(TMPOUTNAME, filemode);
		} else
			chmod(outname, filemode);
	}
	set_signals(1);
}

/*
 * Choose the name of the file to be patched based on POSIX rules.
 * NOTE: the POSIX rules are amazingly stupid and we only follow them
 *       if the user specified --posix or set POSIXLY_CORRECT.
 */
static char *
posix_name(const struct file_name *names, bool assume_exists)
{
	char *path = NULL;
	int i;

	/*
	 * POSIX states that the filename will be chosen from one
	 * of the old, new and index names (in that order) if
	 * the file exists relative to CWD after -p stripping.
	 */
	for (i = 0; i < MAX_FILE; i++) {
		if (names[i].path != NULL && names[i].exists) {
			path = names[i].path;
			break;
		}
	}
	if (path == NULL && !assume_exists) {
		/*
		 * No files found, check to see if the diff could be
		 * creating a new file.
		 */
		if (path == NULL && ok_to_create_file &&
		    names[NEW_FILE].path != NULL)
			path = names[NEW_FILE].path;
	}

	return path ? xstrdup(path) : NULL;
}

static char *
compare_names(const struct file_name *names, bool assume_exists)
{
	size_t min_components, min_baselen, min_len, tmp;
	char *best = NULL;
	char *path;
	int i;

	/*
	 * The "best" name is the one with the fewest number of path
	 * components, the shortest basename length, and the shortest
	 * overall length (in that order).  We only use the Index: file
	 * if neither of the old or new files could be intuited from
	 * the diff header.
	 */
	min_components = min_baselen = min_len = SIZE_MAX;
	for (i = INDEX_FILE; i >= OLD_FILE; i--) {
		path = names[i].path;
		if (path == NULL || (!names[i].exists && !assume_exists))
			continue;
		if ((tmp = num_components(path)) > min_components)
			continue;
		if (tmp < min_components) {
			min_components = tmp;
			best = path;
		}
		if ((tmp = strlen(basename(path))) > min_baselen)
			continue;
		if (tmp < min_baselen) {
			min_baselen = tmp;
			best = path;
		}
		if ((tmp = strlen(path)) > min_len)
			continue;
		min_len = tmp;
		best = path;
	}
	return best;
}

/*
 * Choose the name of the file to be patched based the "best" one
 * available.
 */
static char *
best_name(const struct file_name *names, bool assume_exists)
{
	char *best;

	best = compare_names(names, assume_exists);

	/* No match?  Check to see if the diff could be creating a new file. */
	if (best == NULL && ok_to_create_file)
		best = names[NEW_FILE].path;

	return best ? xstrdup(best) : NULL;
}

static size_t
num_components(const char *path)
{
	size_t n;
	const char *cp;

	for (n = 0, cp = path; (cp = strchr(cp, '/')) != NULL; n++, cp++) {
		while (*cp == '/')
			cp++;		/* skip consecutive slashes */
	}
	return n;
}

/*
 * Convert number at NPTR into LINENUM and save address of first
 * character that is not a digit in ENDPTR.  If conversion is not
 * possible, call fatal.
 */
static LINENUM
strtolinenum(char *nptr, char **endptr)
{
	LINENUM rv;
	char c;
	char *p;
	const char *errstr;

	for (p = nptr; isdigit((unsigned char)*p); p++)
		;

	if (p == nptr)
		malformed();

	c = *p;
	*p = '\0';

	rv = strtonum(nptr, 0, LINENUM_MAX, &errstr);
	if (errstr != NULL)
		fatal("invalid line number at line %ld: `%s' is %s\n",
		    p_input_line, nptr, errstr);

	*p = c;
	*endptr = p;

	return rv;
}
