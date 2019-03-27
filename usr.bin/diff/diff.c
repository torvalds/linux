/*	$OpenBSD: diff.c,v 1.65 2015/12/29 19:04:46 gsoares Exp $	*/

/*
 * Copyright (c) 2003 Todd C. Miller <Todd.Miller@courtesan.com>
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
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "diff.h"
#include "xmalloc.h"

int	 lflag, Nflag, Pflag, rflag, sflag, Tflag, cflag;
int	 diff_format, diff_context, status, ignore_file_case;
int	 tabsize = 8;
char	*start, *ifdefname, *diffargs, *label[2], *ignore_pats;
char	*group_format = NULL;
struct stat stb1, stb2;
struct excludes *excludes_list;
regex_t	 ignore_re;

#define	OPTIONS	"0123456789aBbC:cdD:efHhI:iL:lnNPpqrS:sTtU:uwX:x:"
enum {
	OPT_TSIZE = CHAR_MAX + 1,
	OPT_STRIPCR,
	OPT_IGN_FN_CASE,
	OPT_NO_IGN_FN_CASE,
	OPT_NORMAL,
	OPT_HORIZON_LINES,
	OPT_CHANGED_GROUP_FORMAT,
};

static struct option longopts[] = {
	{ "text",			no_argument,		0,	'a' },
	{ "ignore-space-change",	no_argument,		0,	'b' },
	{ "context",			optional_argument,	0,	'C' },
	{ "ifdef",			required_argument,	0,	'D' },
	{ "minimal",			no_argument,		0,	'd' },
	{ "ed",				no_argument,		0,	'e' },
	{ "forward-ed",			no_argument,		0,	'f' },
	{ "speed-large-files",		no_argument,		NULL,	'H' },
	{ "ignore-blank-lines",		no_argument,		0,	'B' },
	{ "ignore-matching-lines",	required_argument,	0,	'I' },
	{ "ignore-case",		no_argument,		0,	'i' },
	{ "paginate",			no_argument,		NULL,	'l' },
	{ "label",			required_argument,	0,	'L' },
	{ "new-file",			no_argument,		0,	'N' },
	{ "rcs",			no_argument,		0,	'n' },
	{ "unidirectional-new-file",	no_argument,		0,	'P' },
	{ "show-c-function",		no_argument,		0,	'p' },
	{ "brief",			no_argument,		0,	'q' },
	{ "recursive",			no_argument,		0,	'r' },
	{ "report-identical-files",	no_argument,		0,	's' },
	{ "starting-file",		required_argument,	0,	'S' },
	{ "expand-tabs",		no_argument,		0,	't' },
	{ "initial-tab",		no_argument,		0,	'T' },
	{ "unified",			optional_argument,	0,	'U' },
	{ "ignore-all-space",		no_argument,		0,	'w' },
	{ "exclude",			required_argument,	0,	'x' },
	{ "exclude-from",		required_argument,	0,	'X' },
	{ "ignore-file-name-case",	no_argument,		NULL,	OPT_IGN_FN_CASE },
	{ "horizon-lines",		required_argument,	NULL,	OPT_HORIZON_LINES },
	{ "no-ignore-file-name-case",	no_argument,		NULL,	OPT_NO_IGN_FN_CASE },
	{ "normal",			no_argument,		NULL,	OPT_NORMAL },
	{ "strip-trailing-cr",		no_argument,		NULL,	OPT_STRIPCR },
	{ "tabsize",			optional_argument,	NULL,	OPT_TSIZE },
	{ "changed-group-format",	required_argument,	NULL,	OPT_CHANGED_GROUP_FORMAT},
	{ NULL,				0,			0,	'\0'}
};

void usage(void) __dead2;
void push_excludes(char *);
void push_ignore_pats(char *);
void read_excludes_file(char *file);
void set_argstr(char **, char **);

int
main(int argc, char **argv)
{
	const char *errstr = NULL;
	char *ep, **oargv;
	long  l;
	int   ch, dflags, lastch, gotstdin, prevoptind, newarg;

	oargv = argv;
	gotstdin = 0;
	dflags = 0;
	lastch = '\0';
	prevoptind = 1;
	newarg = 1;
	diff_context = 3;
	diff_format = 0;
	while ((ch = getopt_long(argc, argv, OPTIONS, longopts, NULL)) != -1) {
		switch (ch) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (newarg)
				usage();	/* disallow -[0-9]+ */
			else if (lastch == 'c' || lastch == 'u')
				diff_context = 0;
			else if (!isdigit(lastch) || diff_context > INT_MAX / 10)
				usage();
			diff_context = (diff_context * 10) + (ch - '0');
			break;
		case 'a':
			dflags |= D_FORCEASCII;
			break;
		case 'b':
			dflags |= D_FOLDBLANKS;
			break;
		case 'C':
		case 'c':
			cflag = 1;
			diff_format = D_CONTEXT;
			if (optarg != NULL) {
				l = strtol(optarg, &ep, 10);
				if (*ep != '\0' || l < 0 || l >= INT_MAX)
					usage();
				diff_context = (int)l;
			}
			break;
		case 'd':
			dflags |= D_MINIMAL;
			break;
		case 'D':
			diff_format = D_IFDEF;
			ifdefname = optarg;
			break;
		case 'e':
			diff_format = D_EDIT;
			break;
		case 'f':
			diff_format = D_REVERSE;
			break;
		case 'H':
			/* ignore but needed for compatibility with GNU diff */
			break;
		case 'h':
			/* silently ignore for backwards compatibility */
			break;
		case 'B':
			dflags |= D_SKIPBLANKLINES;
			break;
		case 'I':
			push_ignore_pats(optarg);
			break;
		case 'i':
			dflags |= D_IGNORECASE;
			break;
		case 'L':
			if (label[0] == NULL)
				label[0] = optarg;
			else if (label[1] == NULL)
				label[1] = optarg;
			else
				usage();
			break;
		case 'l':
			lflag = 1;
			break;
		case 'N':
			Nflag = 1;
			break;
		case 'n':
			diff_format = D_NREVERSE;
			break;
		case 'p':
			if (diff_format == 0)
				diff_format = D_CONTEXT;
			dflags |= D_PROTOTYPE;
			break;
		case 'P':
			Pflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 'q':
			diff_format = D_BRIEF;
			break;
		case 'S':
			start = optarg;
			break;
		case 's':
			sflag = 1;
			break;
		case 'T':
			Tflag = 1;
			break;
		case 't':
			dflags |= D_EXPANDTABS;
			break;
		case 'U':
		case 'u':
			diff_format = D_UNIFIED;
			if (optarg != NULL) {
				l = strtol(optarg, &ep, 10);
				if (*ep != '\0' || l < 0 || l >= INT_MAX)
					usage();
				diff_context = (int)l;
			}
			break;
		case 'w':
			dflags |= D_IGNOREBLANKS;
			break;
		case 'X':
			read_excludes_file(optarg);
			break;
		case 'x':
			push_excludes(optarg);
			break;
		case OPT_CHANGED_GROUP_FORMAT:
			diff_format = D_GFORMAT;
			group_format = optarg;
			break;
		case OPT_HORIZON_LINES:
			break; /* XXX TODO for compatibility with GNU diff3 */
		case OPT_IGN_FN_CASE:
			ignore_file_case = 1;
			break;
		case OPT_NO_IGN_FN_CASE:
			ignore_file_case = 0;
			break;
		case OPT_NORMAL:
			diff_format = D_NORMAL;
			break;
		case OPT_TSIZE:
			tabsize = (int) strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr) {
				warnx("Invalid argument for tabsize");
				usage();
			}
			break;
		case OPT_STRIPCR:
			dflags |= D_STRIPCR;
			break;
		default:
			usage();
			break;
		}
		lastch = ch;
		newarg = optind != prevoptind;
		prevoptind = optind;
	}
	argc -= optind;
	argv += optind;

#ifdef __OpenBSD__
	if (pledge("stdio rpath tmppath", NULL) == -1)
		err(2, "pledge");
#endif

	/*
	 * Do sanity checks, fill in stb1 and stb2 and call the appropriate
	 * driver routine.  Both drivers use the contents of stb1 and stb2.
	 */
	if (argc != 2)
		usage();
	if (ignore_pats != NULL) {
		char buf[BUFSIZ];
		int error;

		if ((error = regcomp(&ignore_re, ignore_pats,
				     REG_NEWLINE | REG_EXTENDED)) != 0) {
			regerror(error, &ignore_re, buf, sizeof(buf));
			if (*ignore_pats != '\0')
				errx(2, "%s: %s", ignore_pats, buf);
			else
				errx(2, "%s", buf);
		}
	}
	if (strcmp(argv[0], "-") == 0) {
		fstat(STDIN_FILENO, &stb1);
		gotstdin = 1;
	} else if (stat(argv[0], &stb1) != 0)
		err(2, "%s", argv[0]);
	if (strcmp(argv[1], "-") == 0) {
		fstat(STDIN_FILENO, &stb2);
		gotstdin = 1;
	} else if (stat(argv[1], &stb2) != 0)
		err(2, "%s", argv[1]);
	if (gotstdin && (S_ISDIR(stb1.st_mode) || S_ISDIR(stb2.st_mode)))
		errx(2, "can't compare - to a directory");
	set_argstr(oargv, argv);
	if (S_ISDIR(stb1.st_mode) && S_ISDIR(stb2.st_mode)) {
		if (diff_format == D_IFDEF)
			errx(2, "-D option not supported with directories");
		diffdir(argv[0], argv[1], dflags);
	} else {
		if (S_ISDIR(stb1.st_mode)) {
			argv[0] = splice(argv[0], argv[1]);
			if (stat(argv[0], &stb1) < 0)
				err(2, "%s", argv[0]);
		}
		if (S_ISDIR(stb2.st_mode)) {
			argv[1] = splice(argv[1], argv[0]);
			if (stat(argv[1], &stb2) < 0)
				err(2, "%s", argv[1]);
		}
		print_status(diffreg(argv[0], argv[1], dflags, 1), argv[0],
		    argv[1], "");
	}
	exit(status);
}

void
set_argstr(char **av, char **ave)
{
	size_t argsize;
	char **ap;

	argsize = 4 + *ave - *av + 1;
	diffargs = xmalloc(argsize);
	strlcpy(diffargs, "diff", argsize);
	for (ap = av + 1; ap < ave; ap++) {
		if (strcmp(*ap, "--") != 0) {
			strlcat(diffargs, " ", argsize);
			strlcat(diffargs, *ap, argsize);
		}
	}
}

/*
 * Read in an excludes file and push each line.
 */
void
read_excludes_file(char *file)
{
	FILE *fp;
	char *buf, *pattern;
	size_t len;

	if (strcmp(file, "-") == 0)
		fp = stdin;
	else if ((fp = fopen(file, "r")) == NULL)
		err(2, "%s", file);
	while ((buf = fgetln(fp, &len)) != NULL) {
		if (buf[len - 1] == '\n')
			len--;
		if ((pattern = strndup(buf, len)) == NULL)
			err(2, "xstrndup");
		push_excludes(pattern);
	}
	if (strcmp(file, "-") != 0)
		fclose(fp);
}

/*
 * Push a pattern onto the excludes list.
 */
void
push_excludes(char *pattern)
{
	struct excludes *entry;

	entry = xmalloc(sizeof(*entry));
	entry->pattern = pattern;
	entry->next = excludes_list;
	excludes_list = entry;
}

void
push_ignore_pats(char *pattern)
{
	size_t len;

	if (ignore_pats == NULL)
		ignore_pats = xstrdup(pattern);
	else {
		/* old + "|" + new + NUL */
		len = strlen(ignore_pats) + strlen(pattern) + 2;
		ignore_pats = xreallocarray(ignore_pats, 1, len);
		strlcat(ignore_pats, "|", len);
		strlcat(ignore_pats, pattern, len);
	}
}

void
print_only(const char *path, size_t dirlen, const char *entry)
{
	if (dirlen > 1)
		dirlen--;
	printf("Only in %.*s: %s\n", (int)dirlen, path, entry);
}

void
print_status(int val, char *path1, char *path2, const char *entry)
{
	switch (val) {
	case D_BINARY:
		printf("Binary files %s%s and %s%s differ\n",
		    path1, entry, path2, entry);
		break;
	case D_DIFFER:
		if (diff_format == D_BRIEF)
			printf("Files %s%s and %s%s differ\n",
			    path1, entry, path2, entry);
		break;
	case D_SAME:
		if (sflag)
			printf("Files %s%s and %s%s are identical\n",
			    path1, entry, path2, entry);
		break;
	case D_MISMATCH1:
		printf("File %s%s is a directory while file %s%s is a regular file\n",
		    path1, entry, path2, entry);
		break;
	case D_MISMATCH2:
		printf("File %s%s is a regular file while file %s%s is a directory\n",
		    path1, entry, path2, entry);
		break;
	case D_SKIPPED1:
		printf("File %s%s is not a regular file or directory and was skipped\n",
		    path1, entry);
		break;
	case D_SKIPPED2:
		printf("File %s%s is not a regular file or directory and was skipped\n",
		    path2, entry);
		break;
	}
}

void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: diff [-aBbdilpTtw] [-c | -e | -f | -n | -q | -u] [--ignore-case]\n"
	    "            [--no-ignore-case] [--normal] [--strip-trailing-cr] [--tabsize]\n"
	    "            [-I pattern] [-L label] file1 file2\n"
	    "       diff [-aBbdilpTtw] [-I pattern] [-L label] [--ignore-case]\n"
	    "            [--no-ignore-case] [--normal] [--strip-trailing-cr] [--tabsize]\n"
	    "            -C number file1 file2\n"
	    "       diff [-aBbdiltw] [-I pattern] [--ignore-case] [--no-ignore-case]\n"
	    "            [--normal] [--strip-trailing-cr] [--tabsize] -D string file1 file2\n"
	    "       diff [-aBbdilpTtw] [-I pattern] [-L label] [--ignore-case]\n"
	    "            [--no-ignore-case] [--normal] [--tabsize] [--strip-trailing-cr]\n"
	    "            -U number file1 file2\n"
	    "       diff [-aBbdilNPprsTtw] [-c | -e | -f | -n | -q | -u] [--ignore-case]\n"
	    "            [--no-ignore-case] [--normal] [--tabsize] [-I pattern] [-L label]\n"
	    "            [-S name] [-X file] [-x pattern] dir1 dir2\n");

	exit(2);
}
