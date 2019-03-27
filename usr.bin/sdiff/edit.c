/*	$OpenBSD: edit.c,v 1.19 2009/06/07 13:29:50 ray Exp $ */

/*
 * Written by Raymond Lai <ray@cyth.net>.
 * Public domain.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

static void
cleanup(const char *filename)
{

	if (unlink(filename))
		err(2, "could not delete: %s", filename);
	exit(2);
}

/*
 * Execute an editor on the specified pathname, which is interpreted
 * from the shell.  This means flags may be included.
 *
 * Returns -1 on error, or the exit value on success.
 */
static int
editit(const char *pathname)
{
	sig_t sighup, sigint, sigquit, sigchld;
	pid_t pid;
	int saved_errno, st, ret = -1;
	const char *ed;

	ed = getenv("VISUAL");
	if (ed == NULL)
		ed = getenv("EDITOR");
	if (ed == NULL)
		ed = _PATH_VI;

	sighup = signal(SIGHUP, SIG_IGN);
	sigint = signal(SIGINT, SIG_IGN);
	sigquit = signal(SIGQUIT, SIG_IGN);
	sigchld = signal(SIGCHLD, SIG_DFL);
	if ((pid = fork()) == -1)
		goto fail;
	if (pid == 0) {
		execlp(ed, ed, pathname, (char *)NULL);
		_exit(127);
	}
	while (waitpid(pid, &st, 0) == -1)
		if (errno != EINTR)
			goto fail;
	if (!WIFEXITED(st))
		errno = EINTR;
	else
		ret = WEXITSTATUS(st);

 fail:
	saved_errno = errno;
	(void)signal(SIGHUP, sighup);
	(void)signal(SIGINT, sigint);
	(void)signal(SIGQUIT, sigquit);
	(void)signal(SIGCHLD, sigchld);
	errno = saved_errno;
	return (ret);
}

/*
 * Parse edit command.  Returns 0 on success, -1 on error.
 */
int
eparse(const char *cmd, const char *left, const char *right)
{
	FILE *file;
	size_t nread;
	int fd;
	char *filename;
	char buf[BUFSIZ], *text;

	/* Skip whitespace. */
	while (isspace(*cmd))
		++cmd;

	text = NULL;
	switch (*cmd) {
	case '\0':
		/* Edit empty file. */
		break;

	case 'b':
		/* Both strings. */
		if (left == NULL)
			goto RIGHT;
		if (right == NULL)
			goto LEFT;

		/* Neither column is blank, so print both. */
		if (asprintf(&text, "%s\n%s\n", left, right) == -1)
			err(2, "could not allocate memory");
		break;

	case 'l':
LEFT:
		/* Skip if there is no left column. */
		if (left == NULL)
			break;

		if (asprintf(&text, "%s\n", left) == -1)
			err(2, "could not allocate memory");

		break;

	case 'r':
RIGHT:
		/* Skip if there is no right column. */
		if (right == NULL)
			break;

		if (asprintf(&text, "%s\n", right) == -1)
			err(2, "could not allocate memory");

		break;

	default:
		return (-1);
	}

	/* Create temp file. */
	if (asprintf(&filename, "%s/sdiff.XXXXXXXXXX", tmpdir) == -1)
		err(2, "asprintf");
	if ((fd = mkstemp(filename)) == -1)
		err(2, "mkstemp");
	if (text != NULL) {
		size_t len;
		ssize_t nwritten;

		len = strlen(text);
		if ((nwritten = write(fd, text, len)) == -1 ||
		    (size_t)nwritten != len) {
			warn("error writing to temp file");
			cleanup(filename);
		}
	}
	close(fd);

	/* text is no longer used. */
	free(text);

	/* Edit temp file. */
	if (editit(filename) == -1) {
		warn("error editing %s", filename);
		cleanup(filename);
	}

	/* Open temporary file. */
	if (!(file = fopen(filename, "r"))) {
		warn("could not open edited file: %s", filename);
		cleanup(filename);
	}

	/* Copy temporary file contents to output file. */
	for (nread = sizeof(buf); nread == sizeof(buf);) {
		size_t nwritten;

		nread = fread(buf, sizeof(*buf), sizeof(buf), file);
		/* Test for error or end of file. */
		if (nread != sizeof(buf) &&
		    (ferror(file) || !feof(file))) {
			warnx("error reading edited file: %s", filename);
			cleanup(filename);
		}

		/*
		 * If we have nothing to read, break out of loop
		 * instead of writing nothing.
		 */
		if (!nread)
			break;

		/* Write data we just read. */
		nwritten = fwrite(buf, sizeof(*buf), nread, outfp);
		if (nwritten != nread) {
			warnx("error writing to output file");
			cleanup(filename);
		}
	}

	/* We've reached the end of the temporary file, so remove it. */
	if (unlink(filename))
		warn("could not delete: %s", filename);
	fclose(file);

	free(filename);

	return (0);
}
