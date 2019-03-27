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
 * $OpenBSD: patch.c,v 1.54 2014/12/13 10:31:07 tobias Exp $
 * $FreeBSD$
 *
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"
#include "util.h"
#include "pch.h"
#include "inp.h"
#include "backupfile.h"
#include "pathnames.h"

mode_t		filemode = 0644;

char		*buf;			/* general purpose buffer */
size_t		buf_size;		/* size of the general purpose buffer */

bool		using_plan_a = true;	/* try to keep everything in memory */
bool		out_of_mem = false;	/* ran out of memory in plan a */
bool		nonempty_patchf_seen = false;	/* seen nonempty patch file? */

#define MAXFILEC 2

char		*filearg[MAXFILEC];
bool		ok_to_create_file = false;
char		*outname = NULL;
char		*origprae = NULL;
char		*TMPOUTNAME;
char		*TMPINNAME;
char		*TMPREJNAME;
char		*TMPPATNAME;
bool		toutkeep = false;
bool		trejkeep = false;
bool		warn_on_invalid_line;
bool		last_line_missing_eol;

#ifdef DEBUGGING
int		debug = 0;
#endif

bool		force = false;
bool		batch = false;
bool		verbose = true;
bool		reverse = false;
bool		noreverse = false;
bool		skip_rest_of_patch = false;
int		strippath = 957;
bool		canonicalize = false;
bool		check_only = false;
int		diff_type = 0;
char		*revision = NULL;	/* prerequisite revision, if any */
LINENUM		input_lines = 0;	/* how long is input file in lines */
int		posix = 0;		/* strict POSIX mode? */

static void	reinitialize_almost_everything(void);
static void	get_some_switches(void);
static LINENUM	locate_hunk(LINENUM);
static void	abort_context_hunk(void);
static void	rej_line(int, LINENUM);
static void	abort_hunk(void);
static void	apply_hunk(LINENUM);
static void	init_output(const char *);
static void	init_reject(const char *);
static void	copy_till(LINENUM, bool);
static bool	spew_output(void);
static void	dump_line(LINENUM, bool);
static bool	patch_match(LINENUM, LINENUM, LINENUM);
static bool	similar(const char *, const char *, int);
static void	usage(void);

/* true if -E was specified on command line.  */
static bool	remove_empty_files = false;

/* true if -R was specified on command line.  */
static bool	reverse_flag_specified = false;

static bool	Vflag = false;

/* buffer holding the name of the rejected patch file. */
static char	rejname[PATH_MAX];

/* how many input lines have been irretractibly output */
static LINENUM	last_frozen_line = 0;

static int	Argc;		/* guess */
static char	**Argv;
static int	Argc_last;	/* for restarting plan_b */
static char	**Argv_last;

static FILE	*ofp = NULL;	/* output file pointer */
static FILE	*rejfp = NULL;	/* reject file pointer */

static int	filec = 0;	/* how many file arguments? */
static LINENUM	last_offset = 0;
static LINENUM	maxfuzz = 2;

/* patch using ifdef, ifndef, etc. */
static bool		do_defines = false;
/* #ifdef xyzzy */
static char		if_defined[128];
/* #ifndef xyzzy */
static char		not_defined[128];
/* #else */
static const char	else_defined[] = "#else\n";
/* #endif xyzzy */
static char		end_defined[128];


/* Apply a set of diffs as appropriate. */

int
main(int argc, char *argv[])
{
	int	error = 0, hunk, failed, i, fd;
	bool	patch_seen, reverse_seen;
	LINENUM	where = 0, newwhere, fuzz, mymaxfuzz;
	const	char *tmpdir;
	char	*v;

	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);
	for (i = 0; i < MAXFILEC; i++)
		filearg[i] = NULL;

	buf_size = INITLINELEN;
	buf = malloc((unsigned)(buf_size));
	if (buf == NULL)
		fatal("out of memory\n");

	/* Cons up the names of the temporary files.  */
	if ((tmpdir = getenv("TMPDIR")) == NULL || *tmpdir == '\0')
		tmpdir = _PATH_TMP;
	for (i = strlen(tmpdir) - 1; i > 0 && tmpdir[i] == '/'; i--)
		;
	i++;
	if (asprintf(&TMPOUTNAME, "%.*s/patchoXXXXXXXXXX", i, tmpdir) == -1)
		fatal("cannot allocate memory");
	if ((fd = mkstemp(TMPOUTNAME)) < 0)
		pfatal("can't create %s", TMPOUTNAME);
	close(fd);

	if (asprintf(&TMPINNAME, "%.*s/patchiXXXXXXXXXX", i, tmpdir) == -1)
		fatal("cannot allocate memory");
	if ((fd = mkstemp(TMPINNAME)) < 0)
		pfatal("can't create %s", TMPINNAME);
	close(fd);

	if (asprintf(&TMPREJNAME, "%.*s/patchrXXXXXXXXXX", i, tmpdir) == -1)
		fatal("cannot allocate memory");
	if ((fd = mkstemp(TMPREJNAME)) < 0)
		pfatal("can't create %s", TMPREJNAME);
	close(fd);

	if (asprintf(&TMPPATNAME, "%.*s/patchpXXXXXXXXXX", i, tmpdir) == -1)
		fatal("cannot allocate memory");
	if ((fd = mkstemp(TMPPATNAME)) < 0)
		pfatal("can't create %s", TMPPATNAME);
	close(fd);

	v = getenv("SIMPLE_BACKUP_SUFFIX");
	if (v)
		simple_backup_suffix = v;
	else
		simple_backup_suffix = ORIGEXT;

	/* parse switches */
	Argc = argc;
	Argv = argv;
	get_some_switches();

	if (!Vflag) {
		if ((v = getenv("PATCH_VERSION_CONTROL")) == NULL)
			v = getenv("VERSION_CONTROL");
		if (v != NULL || !posix)
			backup_type = get_version(v);	/* OK to pass NULL. */
	}

	/* make sure we clean up /tmp in case of disaster */
	set_signals(0);

	patch_seen = false;
	for (open_patch_file(filearg[1]); there_is_another_patch();
	    reinitialize_almost_everything()) {
		/* for each patch in patch file */

		patch_seen = true;

		warn_on_invalid_line = true;

		if (outname == NULL)
			outname = xstrdup(filearg[0]);

		/* for ed script just up and do it and exit */
		if (diff_type == ED_DIFF) {
			do_ed_script();
			continue;
		}
		/* initialize the patched file */
		if (!skip_rest_of_patch)
			init_output(TMPOUTNAME);

		/* initialize reject file */
		init_reject(TMPREJNAME);

		/* find out where all the lines are */
		if (!skip_rest_of_patch)
			scan_input(filearg[0]);

		/*
		 * from here on, open no standard i/o files, because
		 * malloc might misfire and we can't catch it easily
		 */

		/* apply each hunk of patch */
		hunk = 0;
		failed = 0;
		reverse_seen = false;
		out_of_mem = false;
		while (another_hunk()) {
			hunk++;
			fuzz = 0;
			mymaxfuzz = pch_context();
			if (maxfuzz < mymaxfuzz)
				mymaxfuzz = maxfuzz;
			if (!skip_rest_of_patch) {
				do {
					where = locate_hunk(fuzz);
					if (hunk == 1 && where == 0 && !force && !reverse_seen) {
						/* dwim for reversed patch? */
						if (!pch_swap()) {
							if (fuzz == 0)
								say("Not enough memory to try swapped hunk!  Assuming unswapped.\n");
							continue;
						}
						reverse = !reverse;
						/* try again */
						where = locate_hunk(fuzz);
						if (where == 0) {
							/* didn't find it swapped */
							if (!pch_swap())
								/* put it back to normal */
								fatal("lost hunk on alloc error!\n");
							reverse = !reverse;
						} else if (noreverse) {
							if (!pch_swap())
								/* put it back to normal */
								fatal("lost hunk on alloc error!\n");
							reverse = !reverse;
							say("Ignoring previously applied (or reversed) patch.\n");
							skip_rest_of_patch = true;
						} else if (batch) {
							if (verbose)
								say("%seversed (or previously applied) patch detected!  %s -R.",
								    reverse ? "R" : "Unr",
								    reverse ? "Assuming" : "Ignoring");
						} else {
							ask("%seversed (or previously applied) patch detected!  %s -R? [y] ",
							    reverse ? "R" : "Unr",
							    reverse ? "Assume" : "Ignore");
							if (*buf == 'n') {
								ask("Apply anyway? [n] ");
								if (*buf != 'y')
									skip_rest_of_patch = true;
								else
									reverse_seen = true;
								where = 0;
								reverse = !reverse;
								if (!pch_swap())
									/* put it back to normal */
									fatal("lost hunk on alloc error!\n");
							}
						}
					}
				} while (!skip_rest_of_patch && where == 0 &&
				    ++fuzz <= mymaxfuzz);

				if (skip_rest_of_patch) {	/* just got decided */
					if (ferror(ofp) || fclose(ofp)) {
						say("Error writing %s\n",
						    TMPOUTNAME);
						error = 1;
					}
					ofp = NULL;
				}
			}
			newwhere = pch_newfirst() + last_offset;
			if (skip_rest_of_patch) {
				abort_hunk();
				failed++;
				if (verbose)
					say("Hunk #%d ignored at %ld.\n",
					    hunk, newwhere);
			} else if (where == 0) {
				abort_hunk();
				failed++;
				if (verbose)
					say("Hunk #%d failed at %ld.\n",
					    hunk, newwhere);
			} else {
				apply_hunk(where);
				if (verbose) {
					say("Hunk #%d succeeded at %ld",
					    hunk, newwhere);
					if (fuzz != 0)
						say(" with fuzz %ld", fuzz);
					if (last_offset)
						say(" (offset %ld line%s)",
						    last_offset,
						    last_offset == 1L ? "" : "s");
					say(".\n");
				}
			}
		}

		if (out_of_mem && using_plan_a) {
			Argc = Argc_last;
			Argv = Argv_last;
			say("\n\nRan out of memory using Plan A--trying again...\n\n");
			if (ofp)
				fclose(ofp);
			ofp = NULL;
			if (rejfp)
				fclose(rejfp);
			rejfp = NULL;
			continue;
		}
		if (hunk == 0)
			fatal("Internal error: hunk should not be 0\n");

		/* finish spewing out the new file */
		if (!skip_rest_of_patch && !spew_output()) {
			say("Can't write %s\n", TMPOUTNAME);
			error = 1;
		}

		/* and put the output where desired */
		ignore_signals();
		if (!skip_rest_of_patch) {
			struct stat	statbuf;
			char	*realout = outname;

			if (!check_only) {
				if (move_file(TMPOUTNAME, outname) < 0) {
					toutkeep = true;
					realout = TMPOUTNAME;
					chmod(TMPOUTNAME, filemode);
				} else
					chmod(outname, filemode);

				if (remove_empty_files &&
				    stat(realout, &statbuf) == 0 &&
				    statbuf.st_size == 0) {
					if (verbose)
						say("Removing %s (empty after patching).\n",
						    realout);
					unlink(realout);
				}
			}
		}
		if (ferror(rejfp) || fclose(rejfp)) {
			say("Error writing %s\n", rejname);
			error = 1;
		}
		rejfp = NULL;
		if (failed) {
			error = 1;
			if (*rejname == '\0') {
				if (strlcpy(rejname, outname,
				    sizeof(rejname)) >= sizeof(rejname))
					fatal("filename %s is too long\n", outname);
				if (strlcat(rejname, REJEXT,
				    sizeof(rejname)) >= sizeof(rejname))
					fatal("filename %s is too long\n", outname);
			}
			if (!check_only)
				say("%d out of %d hunks %s--saving rejects to %s\n",
				    failed, hunk, skip_rest_of_patch ? "ignored" : "failed", rejname);
			else
				say("%d out of %d hunks %s while patching %s\n",
				    failed, hunk, skip_rest_of_patch ? "ignored" : "failed", filearg[0]);
			if (!check_only && move_file(TMPREJNAME, rejname) < 0)
				trejkeep = true;
		}
		set_signals(1);
	}

	if (!patch_seen && nonempty_patchf_seen)
		error = 2;

	my_exit(error);
	/* NOTREACHED */
}

/* Prepare to find the next patch to do in the patch file. */

static void
reinitialize_almost_everything(void)
{
	re_patch();
	re_input();

	input_lines = 0;
	last_frozen_line = 0;

	filec = 0;
	if (!out_of_mem) {
		free(filearg[0]);
		filearg[0] = NULL;
	}

	free(outname);
	outname = NULL;

	last_offset = 0;
	diff_type = 0;

	free(revision);
	revision = NULL;

	reverse = reverse_flag_specified;
	skip_rest_of_patch = false;

	get_some_switches();
}

/* Process switches and filenames. */

static void
get_some_switches(void)
{
	const char *options = "b::B:cCd:D:eEfF:i:lnNo:p:r:RstuvV:x:z:";
	static struct option longopts[] = {
		{"backup",		no_argument,		0,	'b'},
		{"batch",		no_argument,		0,	't'},
		{"check",		no_argument,		0,	'C'},
		{"context",		no_argument,		0,	'c'},
		{"debug",		required_argument,	0,	'x'},
		{"directory",		required_argument,	0,	'd'},
		{"dry-run",		no_argument,		0,	'C'},
		{"ed",			no_argument,		0,	'e'},
		{"force",		no_argument,		0,	'f'},
		{"forward",		no_argument,		0,	'N'},
		{"fuzz",		required_argument,	0,	'F'},
		{"ifdef",		required_argument,	0,	'D'},
		{"input",		required_argument,	0,	'i'},
		{"ignore-whitespace",	no_argument,		0,	'l'},
		{"normal",		no_argument,		0,	'n'},
		{"output",		required_argument,	0,	'o'},
		{"prefix",		required_argument,	0,	'B'},
		{"quiet",		no_argument,		0,	's'},
		{"reject-file",		required_argument,	0,	'r'},
		{"remove-empty-files",	no_argument,		0,	'E'},
		{"reverse",		no_argument,		0,	'R'},
		{"silent",		no_argument,		0,	's'},
		{"strip",		required_argument,	0,	'p'},
		{"suffix",		required_argument,	0,	'z'},
		{"unified",		no_argument,		0,	'u'},
		{"version",		no_argument,		0,	'v'},
		{"version-control",	required_argument,	0,	'V'},
		{"posix",		no_argument,		&posix,	1},
		{NULL,			0,			0,	0}
	};
	int ch;

	rejname[0] = '\0';
	Argc_last = Argc;
	Argv_last = Argv;
	if (!Argc)
		return;
	optreset = optind = 1;
	while ((ch = getopt_long(Argc, Argv, options, longopts, NULL)) != -1) {
		switch (ch) {
		case 'b':
			if (backup_type == none)
				backup_type = numbered_existing;
			if (optarg == NULL)
				break;
			if (verbose)
				say("Warning, the ``-b suffix'' option has been"
				    " obsoleted by the -z option.\n");
			/* FALLTHROUGH */
		case 'z':
			/* must directly follow 'b' case for backwards compat */
			simple_backup_suffix = xstrdup(optarg);
			break;
		case 'B':
			origprae = xstrdup(optarg);
			break;
		case 'c':
			diff_type = CONTEXT_DIFF;
			break;
		case 'C':
			check_only = true;
			break;
		case 'd':
			if (chdir(optarg) < 0)
				pfatal("can't cd to %s", optarg);
			break;
		case 'D':
			do_defines = true;
			if (!isalpha((unsigned char)*optarg) && *optarg != '_')
				fatal("argument to -D is not an identifier\n");
			snprintf(if_defined, sizeof if_defined,
			    "#ifdef %s\n", optarg);
			snprintf(not_defined, sizeof not_defined,
			    "#ifndef %s\n", optarg);
			snprintf(end_defined, sizeof end_defined,
			    "#endif /* %s */\n", optarg);
			break;
		case 'e':
			diff_type = ED_DIFF;
			break;
		case 'E':
			remove_empty_files = true;
			break;
		case 'f':
			force = true;
			break;
		case 'F':
			maxfuzz = atoi(optarg);
			break;
		case 'i':
			if (++filec == MAXFILEC)
				fatal("too many file arguments\n");
			filearg[filec] = xstrdup(optarg);
			break;
		case 'l':
			canonicalize = true;
			break;
		case 'n':
			diff_type = NORMAL_DIFF;
			break;
		case 'N':
			noreverse = true;
			break;
		case 'o':
			outname = xstrdup(optarg);
			break;
		case 'p':
			strippath = atoi(optarg);
			break;
		case 'r':
			if (strlcpy(rejname, optarg,
			    sizeof(rejname)) >= sizeof(rejname))
				fatal("argument for -r is too long\n");
			break;
		case 'R':
			reverse = true;
			reverse_flag_specified = true;
			break;
		case 's':
			verbose = false;
			break;
		case 't':
			batch = true;
			break;
		case 'u':
			diff_type = UNI_DIFF;
			break;
		case 'v':
			version();
			break;
		case 'V':
			backup_type = get_version(optarg);
			Vflag = true;
			break;
#ifdef DEBUGGING
		case 'x':
			debug = atoi(optarg);
			break;
#endif
		default:
			if (ch != '\0')
				usage();
			break;
		}
	}
	Argc -= optind;
	Argv += optind;

	if (Argc > 0) {
		filearg[0] = xstrdup(*Argv++);
		Argc--;
		while (Argc > 0) {
			if (++filec == MAXFILEC)
				fatal("too many file arguments\n");
			filearg[filec] = xstrdup(*Argv++);
			Argc--;
		}
	}

	if (getenv("POSIXLY_CORRECT") != NULL)
		posix = 1;
}

static void
usage(void)
{
	fprintf(stderr,
"usage: patch [-bCcEeflNnRstuv] [-B backup-prefix] [-D symbol] [-d directory]\n"
"             [-F max-fuzz] [-i patchfile] [-o out-file] [-p strip-count]\n"
"             [-r rej-name] [-V t | nil | never | none] [-x number]\n"
"             [-z backup-ext] [--posix] [origfile [patchfile]]\n"
"       patch <patchfile\n");
	my_exit(EXIT_FAILURE);
}

/*
 * Attempt to find the right place to apply this hunk of patch.
 */
static LINENUM
locate_hunk(LINENUM fuzz)
{
	LINENUM	first_guess = pch_first() + last_offset;
	LINENUM	offset;
	LINENUM	pat_lines = pch_ptrn_lines();
	LINENUM	max_pos_offset = input_lines - first_guess - pat_lines + 1;
	LINENUM	max_neg_offset = first_guess - last_frozen_line - 1 + pch_context();

	if (pat_lines == 0) {		/* null range matches always */
		if (verbose && fuzz == 0 && (diff_type == CONTEXT_DIFF
		    || diff_type == NEW_CONTEXT_DIFF
		    || diff_type == UNI_DIFF)) {
			say("Empty context always matches.\n");
		}
		return (first_guess);
	}
	if (max_neg_offset >= first_guess)	/* do not try lines < 0 */
		max_neg_offset = first_guess - 1;
	if (first_guess <= input_lines && patch_match(first_guess, 0, fuzz))
		return first_guess;
	for (offset = 1; ; offset++) {
		bool	check_after = (offset <= max_pos_offset);
		bool	check_before = (offset <= max_neg_offset);

		if (check_after && patch_match(first_guess, offset, fuzz)) {
#ifdef DEBUGGING
			if (debug & 1)
				say("Offset changing from %ld to %ld\n",
				    last_offset, offset);
#endif
			last_offset = offset;
			return first_guess + offset;
		} else if (check_before && patch_match(first_guess, -offset, fuzz)) {
#ifdef DEBUGGING
			if (debug & 1)
				say("Offset changing from %ld to %ld\n",
				    last_offset, -offset);
#endif
			last_offset = -offset;
			return first_guess - offset;
		} else if (!check_before && !check_after)
			return 0;
	}
}

/* We did not find the pattern, dump out the hunk so they can handle it. */

static void
abort_context_hunk(void)
{
	LINENUM	i;
	const LINENUM	pat_end = pch_end();
	/*
	 * add in last_offset to guess the same as the previous successful
	 * hunk
	 */
	const LINENUM	oldfirst = pch_first() + last_offset;
	const LINENUM	newfirst = pch_newfirst() + last_offset;
	const LINENUM	oldlast = oldfirst + pch_ptrn_lines() - 1;
	const LINENUM	newlast = newfirst + pch_repl_lines() - 1;
	const char	*stars = (diff_type >= NEW_CONTEXT_DIFF ? " ****" : "");
	const char	*minuses = (diff_type >= NEW_CONTEXT_DIFF ? " ----" : " -----");

	fprintf(rejfp, "***************\n");
	for (i = 0; i <= pat_end; i++) {
		switch (pch_char(i)) {
		case '*':
			if (oldlast < oldfirst)
				fprintf(rejfp, "*** 0%s\n", stars);
			else if (oldlast == oldfirst)
				fprintf(rejfp, "*** %ld%s\n", oldfirst, stars);
			else
				fprintf(rejfp, "*** %ld,%ld%s\n", oldfirst,
				    oldlast, stars);
			break;
		case '=':
			if (newlast < newfirst)
				fprintf(rejfp, "--- 0%s\n", minuses);
			else if (newlast == newfirst)
				fprintf(rejfp, "--- %ld%s\n", newfirst, minuses);
			else
				fprintf(rejfp, "--- %ld,%ld%s\n", newfirst,
				    newlast, minuses);
			break;
		case '\n':
			fprintf(rejfp, "%s", pfetch(i));
			break;
		case ' ':
		case '-':
		case '+':
		case '!':
			fprintf(rejfp, "%c %s", pch_char(i), pfetch(i));
			break;
		default:
			fatal("fatal internal error in abort_context_hunk\n");
		}
	}
}

static void
rej_line(int ch, LINENUM i)
{
	size_t len;
	const char *line = pfetch(i);

	len = strlen(line);

	fprintf(rejfp, "%c%s", ch, line);
	if (len == 0 || line[len - 1] != '\n') {
		if (len >= USHRT_MAX)
			fprintf(rejfp, "\n\\ Line too long\n");
		else
			fprintf(rejfp, "\n\\ No newline at end of line\n");
	}
}

static void
abort_hunk(void)
{
	LINENUM		i, j, split;
	int		ch1, ch2;
	const LINENUM	pat_end = pch_end();
	const LINENUM	oldfirst = pch_first() + last_offset;
	const LINENUM	newfirst = pch_newfirst() + last_offset;

	if (diff_type != UNI_DIFF) {
		abort_context_hunk();
		return;
	}
	split = -1;
	for (i = 0; i <= pat_end; i++) {
		if (pch_char(i) == '=') {
			split = i;
			break;
		}
	}
	if (split == -1) {
		fprintf(rejfp, "malformed hunk: no split found\n");
		return;
	}
	i = 0;
	j = split + 1;
	fprintf(rejfp, "@@ -%ld,%ld +%ld,%ld @@\n",
	    pch_ptrn_lines() ? oldfirst : 0,
	    pch_ptrn_lines(), newfirst, pch_repl_lines());
	while (i < split || j <= pat_end) {
		ch1 = i < split ? pch_char(i) : -1;
		ch2 = j <= pat_end ? pch_char(j) : -1;
		if (ch1 == '-') {
			rej_line('-', i);
			i++;
		} else if (ch1 == ' ' && ch2 == ' ') {
			rej_line(' ', i);
			i++;
			j++;
		} else if (ch1 == '!' && ch2 == '!') {
			while (i < split && ch1 == '!') {
				rej_line('-', i);
				i++;
				ch1 = i < split ? pch_char(i) : -1;
			}
			while (j <= pat_end && ch2 == '!') {
				rej_line('+', j);
				j++;
				ch2 = j <= pat_end ? pch_char(j) : -1;
			}
		} else if (ch1 == '*') {
			i++;
		} else if (ch2 == '+' || ch2 == ' ') {
			rej_line(ch2, j);
			j++;
		} else {
			fprintf(rejfp, "internal error on (%ld %ld %ld)\n",
			    i, split, j);
			rej_line(ch1, i);
			rej_line(ch2, j);
			return;
		}
	}
}

/* We found where to apply it (we hope), so do it. */

static void
apply_hunk(LINENUM where)
{
	LINENUM		old = 1;
	const LINENUM	lastline = pch_ptrn_lines();
	LINENUM		new = lastline + 1;
#define OUTSIDE 0
#define IN_IFNDEF 1
#define IN_IFDEF 2
#define IN_ELSE 3
	int		def_state = OUTSIDE;
	const LINENUM	pat_end = pch_end();

	where--;
	while (pch_char(new) == '=' || pch_char(new) == '\n')
		new++;

	while (old <= lastline) {
		if (pch_char(old) == '-') {
			copy_till(where + old - 1, false);
			if (do_defines) {
				if (def_state == OUTSIDE) {
					fputs(not_defined, ofp);
					def_state = IN_IFNDEF;
				} else if (def_state == IN_IFDEF) {
					fputs(else_defined, ofp);
					def_state = IN_ELSE;
				}
				fputs(pfetch(old), ofp);
			}
			last_frozen_line++;
			old++;
		} else if (new > pat_end) {
			break;
		} else if (pch_char(new) == '+') {
			copy_till(where + old - 1, false);
			if (do_defines) {
				if (def_state == IN_IFNDEF) {
					fputs(else_defined, ofp);
					def_state = IN_ELSE;
				} else if (def_state == OUTSIDE) {
					fputs(if_defined, ofp);
					def_state = IN_IFDEF;
				}
			}
			fputs(pfetch(new), ofp);
			new++;
		} else if (pch_char(new) != pch_char(old)) {
			say("Out-of-sync patch, lines %ld,%ld--mangled text or line numbers, maybe?\n",
			    pch_hunk_beg() + old,
			    pch_hunk_beg() + new);
#ifdef DEBUGGING
			say("oldchar = '%c', newchar = '%c'\n",
			    pch_char(old), pch_char(new));
#endif
			my_exit(2);
		} else if (pch_char(new) == '!') {
			copy_till(where + old - 1, false);
			if (do_defines) {
				fputs(not_defined, ofp);
				def_state = IN_IFNDEF;
			}
			while (pch_char(old) == '!') {
				if (do_defines) {
					fputs(pfetch(old), ofp);
				}
				last_frozen_line++;
				old++;
			}
			if (do_defines) {
				fputs(else_defined, ofp);
				def_state = IN_ELSE;
			}
			while (pch_char(new) == '!') {
				fputs(pfetch(new), ofp);
				new++;
			}
		} else {
			if (pch_char(new) != ' ')
				fatal("Internal error: expected ' '\n");
			old++;
			new++;
			if (do_defines && def_state != OUTSIDE) {
				fputs(end_defined, ofp);
				def_state = OUTSIDE;
			}
		}
	}
	if (new <= pat_end && pch_char(new) == '+') {
		copy_till(where + old - 1, false);
		if (do_defines) {
			if (def_state == OUTSIDE) {
				fputs(if_defined, ofp);
				def_state = IN_IFDEF;
			} else if (def_state == IN_IFNDEF) {
				fputs(else_defined, ofp);
				def_state = IN_ELSE;
			}
		}
		while (new <= pat_end && pch_char(new) == '+') {
			fputs(pfetch(new), ofp);
			new++;
		}
	}
	if (do_defines && def_state != OUTSIDE) {
		fputs(end_defined, ofp);
	}
}

/*
 * Open the new file.
 */
static void
init_output(const char *name)
{
	ofp = fopen(name, "w");
	if (ofp == NULL)
		pfatal("can't create %s", name);
}

/*
 * Open a file to put hunks we can't locate.
 */
static void
init_reject(const char *name)
{
	rejfp = fopen(name, "w");
	if (rejfp == NULL)
		pfatal("can't create %s", name);
}

/*
 * Copy input file to output, up to wherever hunk is to be applied.
 * If endoffile is true, treat the last line specially since it may
 * lack a newline.
 */
static void
copy_till(LINENUM lastline, bool endoffile)
{
	if (last_frozen_line > lastline)
		fatal("misordered hunks! output would be garbled\n");
	while (last_frozen_line < lastline) {
		if (++last_frozen_line == lastline && endoffile)
			dump_line(last_frozen_line, !last_line_missing_eol);
		else
			dump_line(last_frozen_line, true);
	}
}

/*
 * Finish copying the input file to the output file.
 */
static bool
spew_output(void)
{
	int rv;

#ifdef DEBUGGING
	if (debug & 256)
		say("il=%ld lfl=%ld\n", input_lines, last_frozen_line);
#endif
	if (input_lines)
		copy_till(input_lines, true);	/* dump remainder of file */
	rv = ferror(ofp) == 0 && fclose(ofp) == 0;
	ofp = NULL;
	return rv;
}

/*
 * Copy one line from input to output.
 */
static void
dump_line(LINENUM line, bool write_newline)
{
	char	*s;

	s = ifetch(line, 0);
	if (s == NULL)
		return;
	/* Note: string is not NUL terminated. */
	for (; *s != '\n'; s++)
		putc(*s, ofp);
	if (write_newline)
		putc('\n', ofp);
}

/*
 * Does the patch pattern match at line base+offset?
 */
static bool
patch_match(LINENUM base, LINENUM offset, LINENUM fuzz)
{
	LINENUM		pline = 1 + fuzz;
	LINENUM		iline;
	LINENUM		pat_lines = pch_ptrn_lines() - fuzz;
	const char	*ilineptr;
	const char	*plineptr;
	unsigned short	plinelen;

	/* Patch does not match if we don't have any more context to use */
	if (pline > pat_lines)
		return false;
	for (iline = base + offset + fuzz; pline <= pat_lines; pline++, iline++) {
		ilineptr = ifetch(iline, offset >= 0);
		if (ilineptr == NULL)
			return false;
		plineptr = pfetch(pline);
		plinelen = pch_line_len(pline);
		if (canonicalize) {
			if (!similar(ilineptr, plineptr, plinelen))
				return false;
		} else if (strnNE(ilineptr, plineptr, plinelen))
			return false;
		if (iline == input_lines) {
			/*
			 * We are looking at the last line of the file.
			 * If the file has no eol, the patch line should
			 * not have one either and vice-versa. Note that
			 * plinelen > 0.
			 */
			if (last_line_missing_eol) {
				if (plineptr[plinelen - 1] == '\n')
					return false;
			} else {
				if (plineptr[plinelen - 1] != '\n')
					return false;
			}
		}
	}
	return true;
}

/*
 * Do two lines match with canonicalized white space?
 */
static bool
similar(const char *a, const char *b, int len)
{
	while (len) {
		if (isspace((unsigned char)*b)) {	/* whitespace (or \n) to match? */
			if (!isspace((unsigned char)*a))	/* no corresponding whitespace? */
				return false;
			while (len && isspace((unsigned char)*b) && *b != '\n')
				b++, len--;	/* skip pattern whitespace */
			while (isspace((unsigned char)*a) && *a != '\n')
				a++;	/* skip target whitespace */
			if (*a == '\n' || *b == '\n')
				return (*a == *b);	/* should end in sync */
		} else if (*a++ != *b++)	/* match non-whitespace chars */
			return false;
		else
			len--;	/* probably not necessary */
	}
	return true;		/* actually, this is not reached */
	/* since there is always a \n */
}
