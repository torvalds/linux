/*	$OpenBSD: sdiff.c,v 1.36 2015/12/29 19:04:46 gsoares Exp $ */

/*
 * Written by Raymond Lai <ray@cyth.net>.
 * Public domain.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <paths.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

static char diff_path[] = "/usr/bin/diff";

#define WIDTH 126
/*
 * Each column must be at least one character wide, plus three
 * characters between the columns (space, [<|>], space).
 */
#define WIDTH_MIN 5

/* 3 kilobytes of chars */
#define MAX_CHECK 768

/* A single diff line. */
struct diffline {
	STAILQ_ENTRY(diffline) diffentries;
	char	*left;
	char	 div;
	char	*right;
};

static void astrcat(char **, const char *);
static void enqueue(char *, char, char *);
static char *mktmpcpy(const char *);
static int istextfile(FILE *);
static void binexec(char *, char *, char *) __dead2;
static void freediff(struct diffline *);
static void int_usage(void);
static int parsecmd(FILE *, FILE *, FILE *);
static void printa(FILE *, size_t);
static void printc(FILE *, size_t, FILE *, size_t);
static void printcol(const char *, size_t *, const size_t);
static void printd(FILE *, size_t);
static void println(const char *, const char, const char *);
static void processq(void);
static void prompt(const char *, const char *);
static void usage(void) __dead2;
static char *xfgets(FILE *);

static STAILQ_HEAD(, diffline) diffhead = STAILQ_HEAD_INITIALIZER(diffhead);
static size_t line_width;	/* width of a line (two columns and divider) */
static size_t width;		/* width of each column */
static size_t file1ln, file2ln;	/* line number of file1 and file2 */
static int Iflag = 0;	/* ignore sets matching regexp */
static int	lflag;		/* print only left column for identical lines */
static int	sflag;		/* skip identical lines */
FILE *outfp;		/* file to save changes to */
const char *tmpdir;	/* TMPDIR or /tmp */

enum {
	HELP_OPT = CHAR_MAX + 1,
	NORMAL_OPT,
	FCASE_SENSITIVE_OPT,
	FCASE_IGNORE_OPT,
	STRIPCR_OPT,
	TSIZE_OPT,
	DIFFPROG_OPT,
};

static struct option longopts[] = {
	/* options only processed in sdiff */
	{ "suppress-common-lines",	no_argument,		NULL,	's' },
	{ "width",			required_argument,	NULL,	'w' },

	{ "output",			required_argument,	NULL,	'o' },
	{ "diff-program",		required_argument,	NULL,	DIFFPROG_OPT },

	/* Options processed by diff. */
	{ "ignore-file-name-case",	no_argument,		NULL,	FCASE_IGNORE_OPT },
	{ "no-ignore-file-name-case",	no_argument,		NULL,	FCASE_SENSITIVE_OPT },
	{ "strip-trailing-cr",		no_argument,		NULL,	STRIPCR_OPT },
	{ "tabsize",			required_argument,	NULL,	TSIZE_OPT },
	{ "help",			no_argument,		NULL,	HELP_OPT },
	{ "text",			no_argument,		NULL,	'a' },
	{ "ignore-blank-lines",		no_argument,		NULL,	'B' },
	{ "ignore-space-change",	no_argument,		NULL,	'b' },
	{ "minimal",			no_argument,		NULL,	'd' },
	{ "ignore-tab-expansion",	no_argument,		NULL,	'E' },
	{ "ignore-matching-lines",	required_argument,	NULL,	'I' },
	{ "ignore-case",		no_argument,		NULL,	'i' },
	{ "left-column",		no_argument,		NULL,	'l' },
	{ "expand-tabs",		no_argument,		NULL,	't' },
	{ "speed-large-files",		no_argument,		NULL,	'H' },
	{ "ignore-all-space",		no_argument,		NULL,	'W' },

	{ NULL,				0,			NULL,	'\0'}
};

static const char *help_msg[] = {
	"usage: sdiff [-abdilstW] [-I regexp] [-o outfile] [-w width] file1 file2\n",
	"-l, --left-column: only print the left column for identical lines.",
	"-o OUTFILE, --output=OUTFILE: interactively merge file1 and file2 into outfile.",
	"-s, --suppress-common-lines: skip identical lines.",
	"-w WIDTH, --width=WIDTH: print a maximum of WIDTH characters on each line.",
	"",
	"Options passed to diff(1) are:",
	"\t-a, --text: treat file1 and file2 as text files.",
	"\t-b, --ignore-trailing-cr: ignore trailing blank spaces.",
	"\t-d, --minimal: minimize diff size.",
	"\t-I RE, --ignore-matching-lines=RE: ignore changes whose line matches RE.",
	"\t-i, --ignore-case: do a case-insensitive comparison.",
	"\t-t, --expand-tabs: sxpand tabs to spaces.",
	"\t-W, --ignore-all-spaces: ignore all spaces.",
	"\t--speed-large-files: assume large file with scattered changes.",
	"\t--strip-trailing-cr: strip trailing carriage return.",
	"\t--ignore-file-name-case: ignore case of file names.",
	"\t--no-ignore-file-name-case: do not ignore file name case",
	"\t--tabsize NUM: change size of tabs (default 8.)",

	NULL,
};

/*
 * Create temporary file if source_file is not a regular file.
 * Returns temporary file name if one was malloced, NULL if unnecessary.
 */
static char *
mktmpcpy(const char *source_file)
{
	struct stat sb;
	ssize_t rcount;
	int ifd, ofd;
	u_char buf[BUFSIZ];
	char *target_file;

	/* Open input and output. */
	ifd = open(source_file, O_RDONLY, 0);
	/* File was opened successfully. */
	if (ifd != -1) {
		if (fstat(ifd, &sb) == -1)
			err(2, "error getting file status from %s", source_file);

		/* Regular file. */
		if (S_ISREG(sb.st_mode)) {
			close(ifd);
			return (NULL);
		}
	} else {
		/* If ``-'' does not exist the user meant stdin. */
		if (errno == ENOENT && strcmp(source_file, "-") == 0)
			ifd = STDIN_FILENO;
		else
			err(2, "error opening %s", source_file);
	}

	/* Not a regular file, so copy input into temporary file. */
	if (asprintf(&target_file, "%s/sdiff.XXXXXXXXXX", tmpdir) == -1)
		err(2, "asprintf");
	if ((ofd = mkstemp(target_file)) == -1) {
		warn("error opening %s", target_file);
		goto FAIL;
	}
	while ((rcount = read(ifd, buf, sizeof(buf))) != -1 &&
	    rcount != 0) {
		ssize_t wcount;

		wcount = write(ofd, buf, (size_t)rcount);
		if (-1 == wcount || rcount != wcount) {
			warn("error writing to %s", target_file);
			goto FAIL;
		}
	}
	if (rcount == -1) {
		warn("error reading from %s", source_file);
		goto FAIL;
	}

	close(ifd);
	close(ofd);

	return (target_file);

FAIL:
	unlink(target_file);
	exit(2);
}

int
main(int argc, char **argv)
{
	FILE *diffpipe=NULL, *file1, *file2;
	size_t diffargc = 0, wflag = WIDTH;
	int ch, fd[2] = {-1}, status;
	pid_t pid=0;
	const char *outfile = NULL;
	char **diffargv, *diffprog = diff_path, *filename1, *filename2,
	     *tmp1, *tmp2, *s1, *s2;
	int i;
	char I_arg[] = "-I";
	char speed_lf[] = "--speed-large-files";

	/*
	 * Process diff flags.
	 */
	/*
	 * Allocate memory for diff arguments and NULL.
	 * Each flag has at most one argument, so doubling argc gives an
	 * upper limit of how many diff args can be passed.  argv[0],
	 * file1, and file2 won't have arguments so doubling them will
	 * waste some memory; however we need an extra space for the
	 * NULL at the end, so it sort of works out.
	 */
	if (!(diffargv = calloc(argc, sizeof(char **) * 2)))
		err(2, "main");

	/* Add first argument, the program name. */
	diffargv[diffargc++] = diffprog;

	/* create a dynamic string for merging single-switch options */
	if ( asprintf(&diffargv[diffargc++], "-")  < 0 )
		err(2, "main");

	while ((ch = getopt_long(argc, argv, "aBbdEHI:ilo:stWw:",
	    longopts, NULL)) != -1) {
		const char *errstr;

		switch (ch) {
		/* only compatible --long-name-form with diff */
		case FCASE_IGNORE_OPT:
		case FCASE_SENSITIVE_OPT:
		case STRIPCR_OPT:
		case TSIZE_OPT:
		case 'S':
		break;
		/* combine no-arg single switches */
		case 'a':
		case 'B':
		case 'b':
		case 'd':
		case 'E':
		case 'i':
		case 't':
		case 'W':
			diffargv[1]  = realloc(diffargv[1], sizeof(char) * strlen(diffargv[1]) + 2);
			/*
			 * In diff, the 'W' option is 'w' and the 'w' is 'W'.
			 */
			if (ch == 'W')
				sprintf(diffargv[1], "%sw", diffargv[1]);
			else
				sprintf(diffargv[1], "%s%c", diffargv[1], ch);
			break;
		case 'H':
			diffargv[diffargc++] = speed_lf;
			break;
		case DIFFPROG_OPT:
			diffargv[0] = diffprog = optarg;
			break;
		case 'I':
			Iflag = 1;
			diffargv[diffargc++] = I_arg;
			diffargv[diffargc++] = optarg;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'o':
			outfile = optarg;
			break;
		case 's':
			sflag = 1;
			break;
		case 'w':
			wflag = strtonum(optarg, WIDTH_MIN,
			    INT_MAX, &errstr);
			if (errstr)
				errx(2, "width is %s: %s", errstr, optarg);
			break;
		case HELP_OPT:
			for (i = 0; help_msg[i] != NULL; i++)
				printf("%s\n", help_msg[i]);
			exit(0);
			break;
		default:
			usage();
			break;
		}
	}

	/* no single switches were used */
	if (strcmp(diffargv[1], "-") == 0 ) {
		for ( i = 1; i < argc-1; i++) {
			diffargv[i] = diffargv[i+1];
		}
		diffargv[diffargc-1] = NULL;
		diffargc--;
	}

	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	if (outfile && (outfp = fopen(outfile, "w")) == NULL)
		err(2, "could not open: %s", optarg);

	if ((tmpdir = getenv("TMPDIR")) == NULL || *tmpdir == '\0')
		tmpdir = _PATH_TMP;

	filename1 = argv[0];
	filename2 = argv[1];

	/*
	 * Create temporary files for diff and sdiff to share if file1
	 * or file2 are not regular files.  This allows sdiff and diff
	 * to read the same inputs if one or both inputs are stdin.
	 *
	 * If any temporary files were created, their names would be
	 * saved in tmp1 or tmp2.  tmp1 should never equal tmp2.
	 */
	tmp1 = tmp2 = NULL;
	/* file1 and file2 are the same, so copy to same temp file. */
	if (strcmp(filename1, filename2) == 0) {
		if ((tmp1 = mktmpcpy(filename1)))
			filename1 = filename2 = tmp1;
	/* Copy file1 and file2 into separate temp files. */
	} else {
		if ((tmp1 = mktmpcpy(filename1)))
			filename1 = tmp1;
		if ((tmp2 = mktmpcpy(filename2)))
			filename2 = tmp2;
	}

	diffargv[diffargc++] = filename1;
	diffargv[diffargc++] = filename2;
	/* Add NULL to end of array to indicate end of array. */
	diffargv[diffargc++] = NULL;

	/* Subtract column divider and divide by two. */
	width = (wflag - 3) / 2;
	/* Make sure line_width can fit in size_t. */
	if (width > (SIZE_MAX - 3) / 2)
		errx(2, "width is too large: %zu", width);
	line_width = width * 2 + 3;

	if (pipe(fd))
		err(2, "pipe");

	switch (pid = fork()) {
	case 0:
		/* child */
		/* We don't read from the pipe. */
		close(fd[0]);
		if (dup2(fd[1], STDOUT_FILENO) == -1)
			err(2, "child could not duplicate descriptor");
		/* Free unused descriptor. */
		close(fd[1]);
		execvp(diffprog, diffargv);
		err(2, "could not execute diff: %s", diffprog);
		break;
	case -1:
		err(2, "could not fork");
		break;
	}

	/* parent */
	/* We don't write to the pipe. */
	close(fd[1]);

	/* Open pipe to diff command. */
	if ((diffpipe = fdopen(fd[0], "r")) == NULL)
		err(2, "could not open diff pipe");

	if ((file1 = fopen(filename1, "r")) == NULL)
		err(2, "could not open %s", filename1);
	if ((file2 = fopen(filename2, "r")) == NULL)
		err(2, "could not open %s", filename2);
	if (!istextfile(file1) || !istextfile(file2)) {
		/* Close open files and pipe, delete temps */
		fclose(file1);
		fclose(file2);
		if (diffpipe != NULL)
			fclose(diffpipe);
		if (tmp1)
			if (unlink(tmp1))
				warn("Error deleting %s.", tmp1);
		if (tmp2)
			if (unlink(tmp2))
				warn("Error deleting %s.", tmp2);
		free(tmp1);
		free(tmp2);
		binexec(diffprog, filename1, filename2);
	}
	/* Line numbers start at one. */
	file1ln = file2ln = 1;

	/* Read and parse diff output. */
	while (parsecmd(diffpipe, file1, file2) != EOF)
		;
	fclose(diffpipe);

	/* Wait for diff to exit. */
	if (waitpid(pid, &status, 0) == -1 || !WIFEXITED(status) ||
	    WEXITSTATUS(status) >= 2)
		err(2, "diff exited abnormally.");

	/* Delete and free unneeded temporary files. */
	if (tmp1)
		if (unlink(tmp1))
			warn("Error deleting %s.", tmp1);
	if (tmp2)
		if (unlink(tmp2))
			warn("Error deleting %s.", tmp2);
	free(tmp1);
	free(tmp2);
	filename1 = filename2 = tmp1 = tmp2 = NULL;

	/* No more diffs, so print common lines. */
	if (lflag)
		while ((s1 = xfgets(file1)))
			enqueue(s1, ' ', NULL);
	else
		for (;;) {
			s1 = xfgets(file1);
			s2 = xfgets(file2);
			if (s1 || s2)
				enqueue(s1, ' ', s2);
			else
				break;
		}
	fclose(file1);
	fclose(file2);
	/* Process unmodified lines. */
	processq();

	/* Return diff exit status. */
	return (WEXITSTATUS(status));
}

/*
 * When sdiff detects a binary file as input, executes them with
 * diff to maintain the same behavior as GNU sdiff with binary input.
 */
static void
binexec(char *diffprog, char *f1, char *f2)
{

	char *args[] = {diffprog, f1, f2, (char *) 0};
	execv(diffprog, args);

	/* If execv() fails, sdiff's execution will continue below. */
	errx(1, "could not execute diff process");
}

/*
 * Checks whether a file appears to be a text file.
 */
static int
istextfile(FILE *f)
{
	int	ch, i;

	if (f == NULL)
		return (1);
	rewind(f);
	for (i = 0; i <= MAX_CHECK; i++) {
		ch = fgetc(f);
		if (ch == '\0') {
			rewind(f);
			return (0);
		}
		if (ch == EOF)
			break;
	}
	rewind(f);
	return (1);
}

/*
 * Prints an individual column (left or right), taking into account
 * that tabs are variable-width.  Takes a string, the current column
 * the cursor is on the screen, and the maximum value of the column.
 * The column value is updated as we go along.
 */
static void
printcol(const char *s, size_t *col, const size_t col_max)
{

	for (; *s && *col < col_max; ++s) {
		size_t new_col;

		switch (*s) {
		case '\t':
			/*
			 * If rounding to next multiple of eight causes
			 * an integer overflow, just return.
			 */
			if (*col > SIZE_MAX - 8)
				return;

			/* Round to next multiple of eight. */
			new_col = (*col / 8 + 1) * 8;

			/*
			 * If printing the tab goes past the column
			 * width, don't print it and just quit.
			 */
			if (new_col > col_max)
				return;
			*col = new_col;
			break;
		default:
			++(*col);
		}
		putchar(*s);
	}
}

/*
 * Prompts user to either choose between two strings or edit one, both,
 * or neither.
 */
static void
prompt(const char *s1, const char *s2)
{
	char *cmd;

	/* Print command prompt. */
	putchar('%');

	/* Get user input. */
	for (; (cmd = xfgets(stdin)); free(cmd)) {
		const char *p;

		/* Skip leading whitespace. */
		for (p = cmd; isspace(*p); ++p)
			;
		switch (*p) {
		case 'e':
			/* Skip `e'. */
			++p;
			if (eparse(p, s1, s2) == -1)
				goto USAGE;
			break;
		case 'l':
		case '1':
			/* Choose left column as-is. */
			if (s1 != NULL)
				fprintf(outfp, "%s\n", s1);
			/* End of command parsing. */
			break;
		case 'q':
			goto QUIT;
		case 'r':
		case '2':
			/* Choose right column as-is. */
			if (s2 != NULL)
				fprintf(outfp, "%s\n", s2);
			/* End of command parsing. */
			break;
		case 's':
			sflag = 1;
			goto PROMPT;
		case 'v':
			sflag = 0;
			/* FALLTHROUGH */
		default:
			/* Interactive usage help. */
USAGE:
			int_usage();
PROMPT:
			putchar('%');

			/* Prompt user again. */
			continue;
		}
		free(cmd);
		return;
	}

	/*
	 * If there was no error, we received an EOF from stdin, so we
	 * should quit.
	 */
QUIT:
	fclose(outfp);
	exit(0);
}

/*
 * Takes two strings, separated by a column divider.  NULL strings are
 * treated as empty columns.  If the divider is the ` ' character, the
 * second column is not printed (-l flag).  In this case, the second
 * string must be NULL.  When the second column is NULL, the divider
 * does not print the trailing space following the divider character.
 *
 * Takes into account that tabs can take multiple columns.
 */
static void
println(const char *s1, const char divider, const char *s2)
{
	size_t col;

	/* Print first column.  Skips if s1 == NULL. */
	col = 0;
	if (s1) {
		/* Skip angle bracket and space. */
		printcol(s1, &col, width);

	}

	/* Otherwise, we pad this column up to width. */
	for (; col < width; ++col)
		putchar(' ');

	/* Only print left column. */
	if (divider == ' ' && !s2) {
		printf(" (\n");
		return;
	}

	/*
	 * Print column divider.  If there is no second column, we don't
	 * need to add the space for padding.
	 */
	if (!s2) {
		printf(" %c\n", divider);
		return;
	}
	printf(" %c ", divider);
	col += 3;

	/* Skip angle bracket and space. */
	printcol(s2, &col, line_width);

	putchar('\n');
}

/*
 * Reads a line from file and returns as a string.  If EOF is reached,
 * NULL is returned.  The returned string must be freed afterwards.
 */
static char *
xfgets(FILE *file)
{
	size_t linecap;
	ssize_t l;
	char *s;

	clearerr(file);
	linecap = 0;
	s = NULL;

	if ((l = getline(&s, &linecap, file)) == -1) {
		if (ferror(file))
			err(2, "error reading file");
		return (NULL);
	}

	if (s[l-1] == '\n')
		s[l-1] = '\0';

	return (s);
}

/*
 * Parse ed commands from diffpipe and print lines from file1 (lines
 * to change or delete) or file2 (lines to add or change).
 * Returns EOF or 0.
 */
static int
parsecmd(FILE *diffpipe, FILE *file1, FILE *file2)
{
	size_t file1start, file1end, file2start, file2end, n;
	/* ed command line and pointer to characters in line */
	char *line, *p, *q;
	const char *errstr;
	char c, cmd;

	/* Read ed command. */
	if (!(line = xfgets(diffpipe)))
		return (EOF);

	p = line;
	/* Go to character after line number. */
	while (isdigit(*p))
		++p;
	c = *p;
	*p++ = 0;
	file1start = strtonum(line, 0, INT_MAX, &errstr);
	if (errstr)
		errx(2, "file1 start is %s: %s", errstr, line);

	/* A range is specified for file1. */
	if (c == ',') {
		q = p;
		/* Go to character after file2end. */
		while (isdigit(*p))
			++p;
		c = *p;
		*p++ = 0;
		file1end = strtonum(q, 0, INT_MAX, &errstr);
		if (errstr)
			errx(2, "file1 end is %s: %s", errstr, line);
		if (file1start > file1end)
			errx(2, "invalid line range in file1: %s", line);
	} else
		file1end = file1start;

	cmd = c;
	/* Check that cmd is valid. */
	if (!(cmd == 'a' || cmd == 'c' || cmd == 'd'))
		errx(2, "ed command not recognized: %c: %s", cmd, line);

	q = p;
	/* Go to character after line number. */
	while (isdigit(*p))
		++p;
	c = *p;
	*p++ = 0;
	file2start = strtonum(q, 0, INT_MAX, &errstr);
	if (errstr)
		errx(2, "file2 start is %s: %s", errstr, line);

	/*
	 * There should either be a comma signifying a second line
	 * number or the line should just end here.
	 */
	if (c != ',' && c != '\0')
		errx(2, "invalid line range in file2: %c: %s", c, line);

	if (c == ',') {

		file2end = strtonum(p, 0, INT_MAX, &errstr);
		if (errstr)
			errx(2, "file2 end is %s: %s", errstr, line);
		if (file2start >= file2end)
			errx(2, "invalid line range in file2: %s", line);
	} else
		file2end = file2start;

	/* Appends happen _after_ stated line. */
	if (cmd == 'a') {
		if (file1start != file1end)
			errx(2, "append cannot have a file1 range: %s",
			    line);
		if (file1start == SIZE_MAX)
			errx(2, "file1 line range too high: %s", line);
		file1start = ++file1end;
	}
	/*
	 * I'm not sure what the deal is with the line numbers for
	 * deletes, though.
	 */
	else if (cmd == 'd') {
		if (file2start != file2end)
			errx(2, "delete cannot have a file2 range: %s",
			    line);
		if (file2start == SIZE_MAX)
			errx(2, "file2 line range too high: %s", line);
		file2start = ++file2end;
	}

	/*
	 * Continue reading file1 and file2 until we reach line numbers
	 * specified by diff.  Should only happen with -I flag.
	 */
	for (; file1ln < file1start && file2ln < file2start;
	    ++file1ln, ++file2ln) {
		char *s1, *s2;

		if (!(s1 = xfgets(file1)))
			errx(2, "file1 shorter than expected");
		if (!(s2 = xfgets(file2)))
			errx(2, "file2 shorter than expected");

		/* If the -l flag was specified, print only left column. */
		if (lflag) {
			free(s2);
			/*
			 * XXX - If -l and -I are both specified, all
			 * unchanged or ignored lines are shown with a
			 * `(' divider.  This matches GNU sdiff, but I
			 * believe it is a bug.  Just check out:
			 * gsdiff -l -I '^$' samefile samefile.
			 */
			if (Iflag)
				enqueue(s1, '(', NULL);
			else
				enqueue(s1, ' ', NULL);
		} else
			enqueue(s1, ' ', s2);
	}
	/* Ignore deleted lines. */
	for (; file1ln < file1start; ++file1ln) {
		char *s;

		if (!(s = xfgets(file1)))
			errx(2, "file1 shorter than expected");

		enqueue(s, '(', NULL);
	}
	/* Ignore added lines. */
	for (; file2ln < file2start; ++file2ln) {
		char *s;

		if (!(s = xfgets(file2)))
			errx(2, "file2 shorter than expected");

		/* If -l flag was given, don't print right column. */
		if (lflag)
			free(s);
		else
			enqueue(NULL, ')', s);
	}

	/* Process unmodified or skipped lines. */
	processq();

	switch (cmd) {
	case 'a':
		printa(file2, file2end);
		n = file2end - file2start + 1;
		break;
	case 'c':
		printc(file1, file1end, file2, file2end);
		n = file1end - file1start + 1 + 1 + file2end - file2start + 1;
		break;
	case 'd':
		printd(file1, file1end);
		n = file1end - file1start + 1;
		break;
	default:
		errx(2, "invalid diff command: %c: %s", cmd, line);
	}
	free(line);

	/* Skip to next ed line. */
	while (n--) {
		if (!(line = xfgets(diffpipe)))
			errx(2, "diff ended early");
		free(line);
	}

	return (0);
}

/*
 * Queues up a diff line.
 */
static void
enqueue(char *left, char divider, char *right)
{
	struct diffline *diffp;

	if (!(diffp = malloc(sizeof(struct diffline))))
		err(2, "enqueue");
	diffp->left = left;
	diffp->div = divider;
	diffp->right = right;
	STAILQ_INSERT_TAIL(&diffhead, diffp, diffentries);
}

/*
 * Free a diffline structure and its elements.
 */
static void
freediff(struct diffline *diffp)
{

	free(diffp->left);
	free(diffp->right);
	free(diffp);
}

/*
 * Append second string into first.  Repeated appends to the same string
 * are cached, making this an O(n) function, where n = strlen(append).
 */
static void
astrcat(char **s, const char *append)
{
	/* Length of string in previous run. */
	static size_t offset = 0;
	size_t newsiz;
	/*
	 * String from previous run.  Compared to *s to see if we are
	 * dealing with the same string.  If so, we can use offset.
	 */
	static const char *oldstr = NULL;
	char *newstr;

	/*
	 * First string is NULL, so just copy append.
	 */
	if (!*s) {
		if (!(*s = strdup(append)))
			err(2, "astrcat");

		/* Keep track of string. */
		offset = strlen(*s);
		oldstr = *s;

		return;
	}

	/*
	 * *s is a string so concatenate.
	 */

	/* Did we process the same string in the last run? */
	/*
	 * If this is a different string from the one we just processed
	 * cache new string.
	 */
	if (oldstr != *s) {
		offset = strlen(*s);
		oldstr = *s;
	}

	/* Size = strlen(*s) + \n + strlen(append) + '\0'. */
	newsiz = offset + 1 + strlen(append) + 1;

	/* Resize *s to fit new string. */
	newstr = realloc(*s, newsiz);
	if (newstr == NULL)
		err(2, "astrcat");
	*s = newstr;

	/* *s + offset should be end of string. */
	/* Concatenate. */
	strlcpy(*s + offset, "\n", newsiz - offset);
	strlcat(*s + offset, append, newsiz - offset);

	/* New string length should be exactly newsiz - 1 characters. */
	/* Store generated string's values. */
	offset = newsiz - 1;
	oldstr = *s;
}

/*
 * Process diff set queue, printing, prompting, and saving each diff
 * line stored in queue.
 */
static void
processq(void)
{
	struct diffline *diffp;
	char divc, *left, *right;

	/* Don't process empty queue. */
	if (STAILQ_EMPTY(&diffhead))
		return;

	/* Remember the divider. */
	divc = STAILQ_FIRST(&diffhead)->div;

	left = NULL;
	right = NULL;
	/*
	 * Go through set of diffs, concatenating each line in left or
	 * right column into two long strings, `left' and `right'.
	 */
	STAILQ_FOREACH(diffp, &diffhead, diffentries) {
		/*
		 * Print changed lines if -s was given,
		 * print all lines if -s was not given.
		 */
		if (!sflag || diffp->div == '|' || diffp->div == '<' ||
		    diffp->div == '>')
			println(diffp->left, diffp->div, diffp->right);

		/* Append new lines to diff set. */
		if (diffp->left)
			astrcat(&left, diffp->left);
		if (diffp->right)
			astrcat(&right, diffp->right);
	}

	/* Empty queue and free each diff line and its elements. */
	while (!STAILQ_EMPTY(&diffhead)) {
		diffp = STAILQ_FIRST(&diffhead);
		STAILQ_REMOVE_HEAD(&diffhead, diffentries);
		freediff(diffp);
	}

	/* Write to outfp, prompting user if lines are different. */
	if (outfp)
		switch (divc) {
		case ' ': case '(': case ')':
			fprintf(outfp, "%s\n", left);
			break;
		case '|': case '<': case '>':
			prompt(left, right);
			break;
		default:
			errx(2, "invalid divider: %c", divc);
		}

	/* Free left and right. */
	free(left);
	free(right);
}

/*
 * Print lines following an (a)ppend command.
 */
static void
printa(FILE *file, size_t line2)
{
	char *line;

	for (; file2ln <= line2; ++file2ln) {
		if (!(line = xfgets(file)))
			errx(2, "append ended early");
		enqueue(NULL, '>', line);
	}
	processq();
}

/*
 * Print lines following a (c)hange command, from file1ln to file1end
 * and from file2ln to file2end.
 */
static void
printc(FILE *file1, size_t file1end, FILE *file2, size_t file2end)
{
	struct fileline {
		STAILQ_ENTRY(fileline)	 fileentries;
		char			*line;
	};
	STAILQ_HEAD(, fileline) delqhead = STAILQ_HEAD_INITIALIZER(delqhead);

	/* Read lines to be deleted. */
	for (; file1ln <= file1end; ++file1ln) {
		struct fileline *linep;
		char *line1;

		/* Read lines from both. */
		if (!(line1 = xfgets(file1)))
			errx(2, "error reading file1 in delete in change");

		/* Add to delete queue. */
		if (!(linep = malloc(sizeof(struct fileline))))
			err(2, "printc");
		linep->line = line1;
		STAILQ_INSERT_TAIL(&delqhead, linep, fileentries);
	}

	/* Process changed lines.. */
	for (; !STAILQ_EMPTY(&delqhead) && file2ln <= file2end;
	    ++file2ln) {
		struct fileline *del;
		char *add;

		/* Get add line. */
		if (!(add = xfgets(file2)))
			errx(2, "error reading add in change");

		del = STAILQ_FIRST(&delqhead);
		enqueue(del->line, '|', add);
		STAILQ_REMOVE_HEAD(&delqhead, fileentries);
		/*
		 * Free fileline structure but not its elements since
		 * they are queued up.
		 */
		free(del);
	}
	processq();

	/* Process remaining lines to add. */
	for (; file2ln <= file2end; ++file2ln) {
		char *add;

		/* Get add line. */
		if (!(add = xfgets(file2)))
			errx(2, "error reading add in change");

		enqueue(NULL, '>', add);
	}
	processq();

	/* Process remaining lines to delete. */
	while (!STAILQ_EMPTY(&delqhead)) {
		struct fileline *filep;

		filep = STAILQ_FIRST(&delqhead);
		enqueue(filep->line, '<', NULL);
		STAILQ_REMOVE_HEAD(&delqhead, fileentries);
		free(filep);
	}
	processq();
}

/*
 * Print deleted lines from file, from file1ln to file1end.
 */
static void
printd(FILE *file1, size_t file1end)
{
	char *line1;

	/* Print out lines file1ln to line2. */
	for (; file1ln <= file1end; ++file1ln) {
		if (!(line1 = xfgets(file1)))
			errx(2, "file1 ended early in delete");
		enqueue(line1, '<', NULL);
	}
	processq();
}

/*
 * Interactive mode usage.
 */
static void
int_usage(void)
{

	puts("e:\tedit blank diff\n"
	    "eb:\tedit both diffs concatenated\n"
	    "el:\tedit left diff\n"
	    "er:\tedit right diff\n"
	    "l | 1:\tchoose left diff\n"
	    "r | 2:\tchoose right diff\n"
	    "s:\tsilent mode--don't print identical lines\n"
	    "v:\tverbose mode--print identical lines\n"
	    "q:\tquit");
}

static void
usage(void)
{

	fprintf(stderr,
	    "usage: sdiff [-abdilstHW] [-I regexp] [-o outfile] [-w width] file1"
	    " file2\n");
	exit(2);
}
