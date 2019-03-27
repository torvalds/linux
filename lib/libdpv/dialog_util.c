/*-
 * Copyright (c) 2013-2018 Devin Teske <dteske@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "dialog_util.h"
#include "dpv.h"
#include "dpv_private.h"

extern char **environ;

#define TTY_DEFAULT_ROWS	24
#define TTY_DEFAULT_COLS	80

/* [X]dialog(1) characteristics */
uint8_t dialog_test	= 0;
uint8_t use_dialog	= 0;
uint8_t use_libdialog	= 1;
uint8_t use_xdialog	= 0;
uint8_t use_color	= 1;
char dialog[PATH_MAX]	= DIALOG;

/* [X]dialog(1) functionality */
char *title	= NULL;
char *backtitle	= NULL;
int dheight	= 0;
int dwidth	= 0;
static char *dargv[64] = { NULL };

/* TTY/Screen characteristics */
static struct winsize *maxsize = NULL;

/* Function prototypes */
static void tty_maxsize_update(void);
static void x11_maxsize_update(void);

/*
 * Update row/column fields of `maxsize' global (used by dialog_maxrows() and
 * dialog_maxcols()). If the `maxsize' pointer is NULL, it will be initialized.
 * The `ws_row' and `ws_col' fields of `maxsize' are updated to hold current
 * maximum height and width (respectively) for a dialog(1) widget based on the
 * active TTY size.
 *
 * This function is called automatically by dialog_maxrows/cols() to reflect
 * changes in terminal size in-between calls.
 */
static void
tty_maxsize_update(void)
{
	int fd = STDIN_FILENO;
	struct termios t;

	if (maxsize == NULL) {
		if ((maxsize = malloc(sizeof(struct winsize))) == NULL)
			errx(EXIT_FAILURE, "Out of memory?!");
		memset((void *)maxsize, '\0', sizeof(struct winsize));
	}

	if (!isatty(fd))
		fd = open("/dev/tty", O_RDONLY);
	if ((tcgetattr(fd, &t) < 0) || (ioctl(fd, TIOCGWINSZ, maxsize) < 0)) {
		maxsize->ws_row = TTY_DEFAULT_ROWS;
		maxsize->ws_col = TTY_DEFAULT_COLS;
	}
}

/*
 * Update row/column fields of `maxsize' global (used by dialog_maxrows() and
 * dialog_maxcols()). If the `maxsize' pointer is NULL, it will be initialized.
 * The `ws_row' and `ws_col' fields of `maxsize' are updated to hold current
 * maximum height and width (respectively) for an Xdialog(1) widget based on
 * the active video resolution of the X11 environment.
 *
 * This function is called automatically by dialog_maxrows/cols() to initialize
 * `maxsize'. Since video resolution changes are less common and more obtrusive
 * than changes to terminal size, the dialog_maxrows/cols() functions only call
 * this function when `maxsize' is set to NULL.
 */
static void
x11_maxsize_update(void)
{
	FILE *f = NULL;
	char *cols;
	char *cp;
	char *rows;
	char cmdbuf[LINE_MAX];
	char rbuf[LINE_MAX];

	if (maxsize == NULL) {
		if ((maxsize = malloc(sizeof(struct winsize))) == NULL)
			errx(EXIT_FAILURE, "Out of memory?!");
		memset((void *)maxsize, '\0', sizeof(struct winsize));
	}

	/* Assemble the command necessary to get X11 sizes */
	snprintf(cmdbuf, LINE_MAX, "%s --print-maxsize 2>&1", dialog);

	fflush(STDIN_FILENO); /* prevent popen(3) from seeking on stdin */

	if ((f = popen(cmdbuf, "r")) == NULL) {
		if (debug)
			warnx("WARNING! Command `%s' failed", cmdbuf);
		return;
	}

	/* Read in the line returned from Xdialog(1) */
	if ((fgets(rbuf, LINE_MAX, f) == NULL) || (pclose(f) < 0))
		return;

	/* Check for X11-related errors */
	if (strncmp(rbuf, "Xdialog: Error", 14) == 0)
		return;

	/* Parse expected output: MaxSize: YY, XXX */
	if ((rows = strchr(rbuf, ' ')) == NULL)
		return;
	if ((cols = strchr(rows, ',')) != NULL) {
		/* strtonum(3) doesn't like trailing junk */
		*(cols++) = '\0';
		if ((cp = strchr(cols, '\n')) != NULL)
			*cp = '\0';
	}

	/* Convert to unsigned short */
	maxsize->ws_row = (unsigned short)strtonum(
	    rows, 0, USHRT_MAX, (const char **)NULL);
	maxsize->ws_col = (unsigned short)strtonum(
	    cols, 0, USHRT_MAX, (const char **)NULL);
}

/*
 * Return the current maximum height (rows) for an [X]dialog(1) widget.
 */
int
dialog_maxrows(void)
{

	if (use_xdialog && maxsize == NULL)
		x11_maxsize_update(); /* initialize maxsize for GUI */
	else if (!use_xdialog)
		tty_maxsize_update(); /* update maxsize for TTY */
	return (maxsize->ws_row);
}

/*
 * Return the current maximum width (cols) for an [X]dialog(1) widget.
 */
int
dialog_maxcols(void)
{

	if (use_xdialog && maxsize == NULL)
		x11_maxsize_update(); /* initialize maxsize for GUI */
	else if (!use_xdialog)
		tty_maxsize_update(); /* update maxsize for TTY */

	if (use_dialog || use_libdialog) {
		if (use_shadow)
			return (maxsize->ws_col - 2);
		else
			return (maxsize->ws_col);
	} else
		return (maxsize->ws_col);
}

/*
 * Return the current maximum width (cols) for the terminal.
 */
int
tty_maxcols(void)
{

	if (use_xdialog && maxsize == NULL)
		x11_maxsize_update(); /* initialize maxsize for GUI */
	else if (!use_xdialog)
		tty_maxsize_update(); /* update maxsize for TTY */

	return (maxsize->ws_col);
}

/*
 * Spawn an [X]dialog(1) `--gauge' box with a `--prompt' value of init_prompt.
 * Writes the resulting process ID to the pid_t pointed at by `pid'. Returns a
 * file descriptor (int) suitable for writing data to the [X]dialog(1) instance
 * (data written to the file descriptor is seen as standard-in by the spawned
 * [X]dialog(1) process).
 */
int
dialog_spawn_gauge(char *init_prompt, pid_t *pid)
{
	char dummy_init[2] = "";
	char *cp;
	int height;
	int width;
	int error;
	posix_spawn_file_actions_t action;
#if DIALOG_SPAWN_DEBUG
	unsigned int i;
#endif
	unsigned int n = 0;
	int stdin_pipe[2] = { -1, -1 };

	/* Override `dialog' with a path from ENV_DIALOG if provided */
	if ((cp = getenv(ENV_DIALOG)) != NULL)
		snprintf(dialog, PATH_MAX, "%s", cp);

	/* For Xdialog(1), set ENV_XDIALOG_HIGH_DIALOG_COMPAT */
	setenv(ENV_XDIALOG_HIGH_DIALOG_COMPAT, "1", 1);

	/* Constrain the height/width */
	height = dialog_maxrows();
	if (backtitle != NULL)
		height -= use_shadow ? 5 : 4;
	if (dheight < height)
		height = dheight;
	width = dialog_maxcols();
	if (dwidth < width)
		width = dwidth;

	/* Populate argument array */
	dargv[n++] = dialog;
	if (title != NULL) {
		if ((dargv[n] = malloc(8)) == NULL)
			errx(EXIT_FAILURE, "Out of memory?!");
		sprintf(dargv[n++], "--title");
		dargv[n++] = title;
	} else {
		if ((dargv[n] = malloc(8)) == NULL)
			errx(EXIT_FAILURE, "Out of memory?!");
		sprintf(dargv[n++], "--title");
		if ((dargv[n] = malloc(1)) == NULL)
			errx(EXIT_FAILURE, "Out of memory?!");
		*dargv[n++] = '\0';
	}
	if (backtitle != NULL) {
		if ((dargv[n] = malloc(12)) == NULL)
			errx(EXIT_FAILURE, "Out of memory?!");
		sprintf(dargv[n++], "--backtitle");
		dargv[n++] = backtitle;
	}
	if (use_color) {
		if ((dargv[n] = malloc(11)) == NULL)
			errx(EXIT_FAILURE, "Out of memory?!");
		sprintf(dargv[n++], "--colors");
	}
	if (use_xdialog) {
		if ((dargv[n] = malloc(7)) == NULL)
			errx(EXIT_FAILURE, "Out of memory?!");
		sprintf(dargv[n++], "--left");

		/*
		 * NOTE: Xdialog(1)'s `--wrap' appears to be broken for the
		 * `--gauge' widget prompt-updates. Add it anyway (in-case it
		 * gets fixed in some later release).
		 */
		if ((dargv[n] = malloc(7)) == NULL)
			errx(EXIT_FAILURE, "Out of memory?!");
		sprintf(dargv[n++], "--wrap");
	}
	if ((dargv[n] = malloc(8)) == NULL)
		errx(EXIT_FAILURE, "Out of memory?!");
	sprintf(dargv[n++], "--gauge");
	dargv[n++] = use_xdialog ? dummy_init : init_prompt;
	if ((dargv[n] = malloc(40)) == NULL)
		errx(EXIT_FAILURE, "Out of memory?!");
	snprintf(dargv[n++], 40, "%u", height);
	if ((dargv[n] = malloc(40)) == NULL)
		errx(EXIT_FAILURE, "Out of memory?!");
	snprintf(dargv[n++], 40, "%u", width);
	dargv[n] = NULL;

	/* Open a pipe(2) to communicate with [X]dialog(1) */
	if (pipe(stdin_pipe) < 0)
		err(EXIT_FAILURE, "%s: pipe(2)", __func__);

	/* Fork [X]dialog(1) process */
#if DIALOG_SPAWN_DEBUG
	fprintf(stderr, "%s: spawning `", __func__);
	for (i = 0; i < n; i++) {
		if (i == 0)
			fprintf(stderr, "%s", dargv[i]);
		else if (*dargv[i] == '-' && *(dargv[i] + 1) == '-')
			fprintf(stderr, " %s", dargv[i]);
		else
			fprintf(stderr, " \"%s\"", dargv[i]);
	}
	fprintf(stderr, "'\n");
#endif
	posix_spawn_file_actions_init(&action);
	posix_spawn_file_actions_adddup2(&action, stdin_pipe[0], STDIN_FILENO);
	posix_spawn_file_actions_addclose(&action, stdin_pipe[1]);
	error = posix_spawnp(pid, dialog, &action,
	    (const posix_spawnattr_t *)NULL, dargv, environ);
	if (error != 0) err(EXIT_FAILURE, "%s", dialog);

	/* NB: Do not free(3) *dargv[], else SIGSEGV */

	return (stdin_pipe[1]);
}

/*
 * Returns the number of lines in buffer pointed to by `prompt'. Takes both
 * newlines and escaped-newlines into account.
 */
unsigned int
dialog_prompt_numlines(const char *prompt, uint8_t nlstate)
{
	uint8_t nls = nlstate; /* See dialog_prompt_nlstate() */
	const char *cp = prompt;
	unsigned int nlines = 1;

	if (prompt == NULL || *prompt == '\0')
		return (0);

	while (*cp != '\0') {
		if (use_dialog) {
			if (strncmp(cp, "\\n", 2) == 0) {
				cp++;
				nlines++;
				nls = TRUE; /* See declaration comment */
			} else if (*cp == '\n') {
				if (!nls)
					nlines++;
				nls = FALSE; /* See declaration comment */
			}
		} else if (use_libdialog) {
			if (*cp == '\n')
				nlines++;
		} else if (strncmp(cp, "\\n", 2) == 0) {
			cp++;
			nlines++;
		}
		cp++;
	}

	return (nlines);
}

/*
 * Returns the length in bytes of the longest line in buffer pointed to by
 * `prompt'. Takes newlines and escaped newlines into account. Also discounts
 * dialog(1) color escape codes if enabled (via `use_color' global).
 */
unsigned int
dialog_prompt_longestline(const char *prompt, uint8_t nlstate)
{
	uint8_t backslash = 0;
	uint8_t nls = nlstate; /* See dialog_prompt_nlstate() */
	const char *p = prompt;
	int longest = 0;
	int n = 0;

	/* `prompt' parameter is required */
	if (prompt == NULL)
		return (0);
	if (*prompt == '\0')
		return (0); /* shortcut */

	/* Loop until the end of the string */
	while (*p != '\0') {
		/* dialog(1) and dialog(3) will render literal newlines */
		if (use_dialog || use_libdialog) {
			if (*p == '\n') {
				if (!use_libdialog && nls)
					n++;
				else {
					if (n > longest)
						longest = n;
					n = 0;
				}
				nls = FALSE; /* See declaration comment */
				p++;
				continue;
			}
		}

		/* Check for backslash character */
		if (*p == '\\') {
			/* If second backslash, count as a single-char */
			if ((backslash ^= 1) == 0)
				n++;
		} else if (backslash) {
			if (*p == 'n' && !use_libdialog) { /* new line */
				/* NB: dialog(3) ignores escaped newlines */
				nls = TRUE; /* See declaration comment */
				if (n > longest)
					longest = n;
				n = 0;
			} else if (use_color && *p == 'Z') {
				if (*++p != '\0')
					p++;
				backslash = 0;
				continue;
			} else /* [X]dialog(1)/dialog(3) only expand those */
				n += 2;

			backslash = 0;
		} else
			n++;
		p++;
	}
	if (n > longest)
		longest = n;

	return (longest);
}

/*
 * Returns a pointer to the last line in buffer pointed to by `prompt'. Takes
 * both newlines (if using dialog(1) versus Xdialog(1)) and escaped newlines
 * into account. If no newlines (escaped or otherwise) appear in the buffer,
 * `prompt' is returned. If passed a NULL pointer, returns NULL.
 */
char *
dialog_prompt_lastline(char *prompt, uint8_t nlstate)
{
	uint8_t nls = nlstate; /* See dialog_prompt_nlstate() */
	char *lastline;
	char *p;

	if (prompt == NULL)
		return (NULL);
	if (*prompt == '\0')
		return (prompt); /* shortcut */

	lastline = p = prompt;
	while (*p != '\0') {
		/* dialog(1) and dialog(3) will render literal newlines */
		if (use_dialog || use_libdialog) {
			if (*p == '\n') {
				if (use_libdialog || !nls)
					lastline = p + 1;
				nls = FALSE; /* See declaration comment */
			}
		}
		/* dialog(3) does not expand escaped newlines */
		if (use_libdialog) {
			p++;
			continue;
		}
		if (*p == '\\' && *(p + 1) != '\0' && *(++p) == 'n') {
			nls = TRUE; /* See declaration comment */
			lastline = p + 1;
		}
		p++;
	}

	return (lastline);
}

/*
 * Returns the number of extra lines generated by wrapping the text in buffer
 * pointed to by `prompt' within `ncols' columns (for prompts, this should be
 * dwidth - 4). Also discounts dialog(1) color escape codes if enabled (via
 * `use_color' global).
 */
int
dialog_prompt_wrappedlines(char *prompt, int ncols, uint8_t nlstate)
{
	uint8_t backslash = 0;
	uint8_t nls = nlstate; /* See dialog_prompt_nlstate() */
	char *cp;
	char *p = prompt;
	int n = 0;
	int wlines = 0;

	/* `prompt' parameter is required */
	if (p == NULL)
		return (0);
	if (*p == '\0')
		return (0); /* shortcut */

	/* Loop until the end of the string */
	while (*p != '\0') {
		/* dialog(1) and dialog(3) will render literal newlines */
		if (use_dialog || use_libdialog) {
			if (*p == '\n') {
				if (use_dialog || !nls)
					n = 0;
				nls = FALSE; /* See declaration comment */
			}
		}

		/* Check for backslash character */
		if (*p == '\\') {
			/* If second backslash, count as a single-char */
			if ((backslash ^= 1) == 0)
				n++;
		} else if (backslash) {
			if (*p == 'n' && !use_libdialog) { /* new line */
				/* NB: dialog(3) ignores escaped newlines */
				nls = TRUE; /* See declaration comment */
				n = 0;
			} else if (use_color && *p == 'Z') {
				if (*++p != '\0')
					p++;
				backslash = 0;
				continue;
			} else /* [X]dialog(1)/dialog(3) only expand those */
				n += 2;

			backslash = 0;
		} else
			n++;

		/* Did we pass the width barrier? */
		if (n > ncols) {
			/*
			 * Work backward to find the first whitespace on-which
			 * dialog(1) will wrap the line (but don't go before
			 * the start of this line).
			 */
			cp = p;
			while (n > 1 && !isspace(*cp)) {
				cp--;
				n--;
			}
			if (n > 0 && isspace(*cp))
				p = cp;
			wlines++;
			n = 1;
		}

		p++;
	}

	return (wlines);
}

/*
 * Returns zero if the buffer pointed to by `prompt' contains an escaped
 * newline but only if appearing after any/all literal newlines. This is
 * specific to dialog(1) and does not apply to Xdialog(1).
 *
 * As an attempt to make shell scripts easier to read, dialog(1) will "eat"
 * the first literal newline after an escaped newline. This however has a bug
 * in its implementation in that rather than allowing `\\n\n' to be treated
 * similar to `\\n' or `\n', dialog(1) expands the `\\n' and then translates
 * the following literal newline (with or without characters between [!]) into
 * a single space.
 *
 * If you want to be compatible with Xdialog(1), it is suggested that you not
 * use literal newlines (they aren't supported); but if you have to use them,
 * go right ahead. But be forewarned... if you set $DIALOG in your environment
 * to something other than `cdialog' (our current dialog(1)), then it should
 * do the same thing w/respect to how to handle a literal newline after an
 * escaped newline (you could do no wrong by translating every literal newline
 * into a space but only when you've previously encountered an escaped one;
 * this is what dialog(1) is doing).
 *
 * The ``newline state'' (or nlstate for short; as I'm calling it) is helpful
 * if you plan to combine multiple strings into a single prompt text. In lead-
 * up to this procedure, a common task is to calculate and utilize the widths
 * and heights of each piece of prompt text to later be combined. However, if
 * (for example) the first string ends in a positive newline state (has an
 * escaped newline without trailing literal), the first literal newline in the
 * second string will be mangled.
 *
 * The return value of this function should be used as the `nlstate' argument
 * to dialog_*() functions that require it to allow accurate calculations in
 * the event such information is needed.
 */
uint8_t
dialog_prompt_nlstate(const char *prompt)
{
	const char *cp;

	if (prompt == NULL)
		return 0;

	/*
	 * Work our way backward from the end of the string for efficiency.
	 */
	cp = prompt + strlen(prompt);
	while (--cp >= prompt) {
		/*
		 * If we get to a literal newline first, this prompt ends in a
		 * clean state for rendering with dialog(1). Otherwise, if we
		 * get to an escaped newline first, this prompt ends in an un-
		 * clean state (following literal will be mangled; see above).
		 */
		if (*cp == '\n')
			return (0);
		else if (*cp == 'n' && --cp > prompt && *cp == '\\')
			return (1);
	}

	return (0); /* no newlines (escaped or otherwise) */
}

/*
 * Free allocated items initialized by tty_maxsize_update() and
 * x11_maxsize_update()
 */
void
dialog_maxsize_free(void)
{
	if (maxsize != NULL) {
		free(maxsize);
		maxsize = NULL;
	}
}
