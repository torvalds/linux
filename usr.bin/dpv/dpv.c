/*-
 * Copyright (c) 2013-2016 Devin Teske <dteske@FreeBSD.org>
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

#include <sys/stat.h>
#include <sys/types.h>

#define _BSD_SOURCE /* to get dprintf() prototype in stdio.h below */
#include <dialog.h>
#include <dpv.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string_m.h>
#include <unistd.h>

#include "dpv_util.h"

/* Debugging */
static uint8_t debug = FALSE;

/* Data to process */
static struct dpv_file_node *file_list = NULL;
static unsigned int nfiles = 0;

/* Data processing */
static uint8_t line_mode = FALSE;
static uint8_t no_overrun = FALSE;
static char *buf = NULL;
static int fd = -1;
static int output_type = DPV_OUTPUT_NONE;
static size_t bsize;
static char rpath[PATH_MAX];

/* Extra display information */
static uint8_t multiple = FALSE; /* `-m' */
static char *pgm; /* set to argv[0] by main() */

/* Function prototypes */
static void	sig_int(int sig);
static void	usage(void);
int		main(int argc, char *argv[]);
static int	operate_common(struct dpv_file_node *file, int out);
static int	operate_on_bytes(struct dpv_file_node *file, int out);
static int	operate_on_lines(struct dpv_file_node *file, int out);

static int
operate_common(struct dpv_file_node *file, int out)
{
	struct stat sb;

	/* Open the file if necessary */
	if (fd < 0) {
		if (multiple) {
			/* Resolve the file path and attempt to open it */
			if (realpath(file->path, rpath) == 0 ||
			    (fd = open(rpath, O_RDONLY)) < 0) {
				warn("%s", file->path);
				file->status = DPV_STATUS_FAILED;
				return (-1);
			}
		} else {
			/* Assume stdin, but if that's a TTY instead use the
			 * highest numbered file descriptor (obtained by
			 * generating new fd and then decrementing).
			 *
			 * NB: /dev/stdin should always be open(2)'able
			 */
			fd = STDIN_FILENO;
			if (isatty(fd)) {
				fd = open("/dev/stdin", O_RDONLY);
				close(fd--);
			}

			/* This answer might be wrong, if dpv(3) has (by
			 * request) opened an output file or pipe. If we
			 * told dpv(3) to open a file, subtract one from
			 * previous answer. If instead we told dpv(3) to
			 * prepare a pipe output, subtract two.
			 */
			switch(output_type) {
			case DPV_OUTPUT_FILE:
				fd -= 1;
				break;
			case DPV_OUTPUT_SHELL:
				fd -= 2;
				break;
			}
		}
	}

	/* Allocate buffer if necessary */
	if (buf == NULL) {
		/* Use output block size as buffer size if available */
		if (out >= 0) {
			if (fstat(out, &sb) != 0) {
				warn("%i", out);
				file->status = DPV_STATUS_FAILED;
				return (-1);
			}
			if (S_ISREG(sb.st_mode)) {
				if (sysconf(_SC_PHYS_PAGES) >
				    PHYSPAGES_THRESHOLD)
					bsize = MIN(BUFSIZE_MAX, MAXPHYS * 8);
				else
					bsize = BUFSIZE_SMALL;
			} else
				bsize = MAX(sb.st_blksize,
				    (blksize_t)sysconf(_SC_PAGESIZE));
		} else
			bsize = MIN(BUFSIZE_MAX, MAXPHYS * 8);

		/* Attempt to allocate */
		if ((buf = malloc(bsize+1)) == NULL) {
			end_dialog();
			err(EXIT_FAILURE, "Out of memory?!");
		}
	}

	return (0);
}

static int
operate_on_bytes(struct dpv_file_node *file, int out)
{
	int progress;
	ssize_t r, w;

	if (operate_common(file, out) < 0)
		return (-1);

	/* [Re-]Fill the buffer */
	if ((r = read(fd, buf, bsize)) <= 0) {
		if (fd != STDIN_FILENO)
			close(fd);
		fd = -1;
		file->status = DPV_STATUS_DONE;
		return (100);
	}

	/* [Re-]Dump the buffer */
	if (out >= 0) {
		if ((w = write(out, buf, r)) < 0) {
			end_dialog();
			err(EXIT_FAILURE, "output");
		}
		fsync(out);
	}

	dpv_overall_read += r;
	file->read += r;

	/* Calculate percentage of completion (if possible) */
	if (file->length >= 0) {
		progress = (file->read * 100 / (file->length > 0 ?
		    file->length : 1));

		/* If no_overrun, do not return 100% until read >= length */
		if (no_overrun && progress == 100 && file->read < file->length)
			progress--;
			
		return (progress);
	} else
		return (-1);
}

static int
operate_on_lines(struct dpv_file_node *file, int out)
{
	char *p;
	int progress;
	ssize_t r, w;

	if (operate_common(file, out) < 0)
		return (-1);

	/* [Re-]Fill the buffer */
	if ((r = read(fd, buf, bsize)) <= 0) {
		if (fd != STDIN_FILENO)
			close(fd);
		fd = -1;
		file->status = DPV_STATUS_DONE;
		return (100);
	}
	buf[r] = '\0';

	/* [Re-]Dump the buffer */
	if (out >= 0) {
		if ((w = write(out, buf, r)) < 0) {
			end_dialog();
			err(EXIT_FAILURE, "output");
		}
		fsync(out);
	}

	/* Process the buffer for number of lines */
	for (p = buf; p != NULL && *p != '\0';)
		if ((p = strchr(p, '\n')) != NULL)
			dpv_overall_read++, p++, file->read++;

	/* Calculate percentage of completion (if possible) */
	if (file->length >= 0) {
		progress = (file->read * 100 / file->length);

		/* If no_overrun, do not return 100% until read >= length */
		if (no_overrun && progress == 100 && file->read < file->length)
			progress--;
			
		return (progress);
	} else
		return (-1);
}

/*
 * Takes a list of names that are to correspond to input streams coming from
 * stdin or fifos and produces necessary config to drive dpv(3) `--gauge'
 * widget. If the `-d' flag is used, output is instead send to terminal
 * standard output (and the output can then be saved to a file, piped into
 * custom [X]dialog(1) invocation, or whatever.
 */
int
main(int argc, char *argv[])
{
	char dummy;
	int ch;
	int n = 0;
	size_t config_size = sizeof(struct dpv_config);
	size_t file_node_size = sizeof(struct dpv_file_node);
	struct dpv_config *config;
	struct dpv_file_node *curfile;
	struct sigaction act;

	pgm = argv[0]; /* store a copy of invocation name */

	/* Allocate config structure */
	if ((config = malloc(config_size)) == NULL)
		errx(EXIT_FAILURE, "Out of memory?!");
	memset((void *)(config), '\0', config_size);

	/*
	 * Process command-line options
	 */
	while ((ch = getopt(argc, argv,
	    "a:b:dDhi:I:klL:mn:No:p:P:t:TU:wx:X")) != -1) {
		switch(ch) {
		case 'a': /* additional message text to append */
			if (config->aprompt == NULL) {
				config->aprompt = malloc(DPV_APROMPT_MAX);
				if (config->aprompt == NULL)
					errx(EXIT_FAILURE, "Out of memory?!");
			}
			snprintf(config->aprompt, DPV_APROMPT_MAX, "%s",
			    optarg);
			break;
		case 'b': /* [X]dialog(1) backtitle */
			if (config->backtitle != NULL)
				free((char *)config->backtitle);
			config->backtitle = malloc(strlen(optarg) + 1);
			if (config->backtitle == NULL)
				errx(EXIT_FAILURE, "Out of memory?!");
			*(config->backtitle) = '\0';
			strcat(config->backtitle, optarg);
			break;
		case 'd': /* debugging */
			debug = TRUE;
			config->debug = debug;
			break;
		case 'D': /* use dialog(1) instead of libdialog */
			config->display_type = DPV_DISPLAY_DIALOG;
			break;
		case 'h': /* help/usage */
			usage();
			break; /* NOTREACHED */
		case 'i': /* status line format string for single-file */
			config->status_solo = optarg;
			break;
		case 'I': /* status line format string for many-files */
			config->status_many = optarg;
			break;
		case 'k': /* keep tite */
			config->keep_tite = TRUE;
			break;
		case 'l': /* Line mode */
			line_mode = TRUE;
			break;
		case 'L': /* custom label size */
			config->label_size =
			    (int)strtol(optarg, (char **)NULL, 10);
			if (config->label_size == 0 && errno == EINVAL)
				errx(EXIT_FAILURE,
				    "`-L' argument must be numeric");
			else if (config->label_size < -1)
				config->label_size = -1;
			break;
		case 'm': /* enable multiple file arguments */
			multiple = TRUE;
			break;
		case 'o': /* `-o path' for sending data-read to file */
			output_type = DPV_OUTPUT_FILE;
			config->output_type = DPV_OUTPUT_FILE;
			config->output = optarg;
			break;
		case 'n': /* custom number of files per `page' */
			config->display_limit =
				(int)strtol(optarg, (char **)NULL, 10);
			if (config->display_limit == 0 && errno == EINVAL)
				errx(EXIT_FAILURE,
				    "`-n' argument must be numeric");
			else if (config->display_limit < 0)
				config->display_limit = -1;
			break;
		case 'N': /* No overrun (truncate reads of known-length) */
			no_overrun = TRUE;
			config->options |= DPV_NO_OVERRUN;
			break;
		case 'p': /* additional message text to use as prefix */
			if (config->pprompt == NULL) {
				config->pprompt = malloc(DPV_PPROMPT_MAX + 2);
				if (config->pprompt == NULL)
					errx(EXIT_FAILURE, "Out of memory?!");
				/* +2 is for implicit "\n" appended later */
			}
			snprintf(config->pprompt, DPV_PPROMPT_MAX, "%s",
			    optarg);
			break;
		case 'P': /* custom size for mini-progressbar */
			config->pbar_size =
			    (int)strtol(optarg, (char **)NULL, 10);
			if (config->pbar_size == 0 && errno == EINVAL)
				errx(EXIT_FAILURE,
				    "`-P' argument must be numeric");
			else if (config->pbar_size < -1)
				config->pbar_size = -1;
			break;
		case 't': /* [X]dialog(1) title */
			if (config->title != NULL)
				free(config->title);
			config->title = malloc(strlen(optarg) + 1);
			if (config->title == NULL)
				errx(EXIT_FAILURE, "Out of memory?!");
			*(config->title) = '\0';
			strcat(config->title, optarg);
			break;
		case 'T': /* test mode (don't read data, fake it) */
			config->options |= DPV_TEST_MODE;
			break;
		case 'U': /* updates per second */
			config->status_updates_per_second =
			    (int)strtol(optarg, (char **)NULL, 10);
			if (config->status_updates_per_second == 0 &&
			    errno == EINVAL)
				errx(EXIT_FAILURE,
				    "`-U' argument must be numeric");
			break;
		case 'w': /* `-p' and `-a' widths bump [X]dialog(1) width */
			config->options |= DPV_WIDE_MODE;
			break;
		case 'x': /* `-x cmd' for sending data-read to sh(1) code */
			output_type = DPV_OUTPUT_SHELL;
			config->output_type = DPV_OUTPUT_SHELL;
			config->output = optarg;
			break;
		case 'X': /* X11 support through x11/xdialog */
			config->display_type = DPV_DISPLAY_XDIALOG;
			break;
		case '?': /* unknown argument (based on optstring) */
			/* FALLTHROUGH */
		default: /* unhandled argument (based on switch) */
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	/* Process remaining arguments as list of names to display */
	for (curfile = file_list; n < argc; n++) {
		nfiles++;

		/* Allocate a new struct for the file argument */
		if (curfile == NULL) {
			if ((curfile = malloc(file_node_size)) == NULL)
				errx(EXIT_FAILURE, "Out of memory?!");
			memset((void *)(curfile), '\0', file_node_size);
			file_list = curfile;
		} else {
			if ((curfile->next = malloc(file_node_size)) == NULL)
				errx(EXIT_FAILURE, "Out of memory?!");
			memset((void *)(curfile->next), '\0', file_node_size);
			curfile = curfile->next;
		}
		curfile->name = argv[n];

		/* Read possible `lines:' prefix from label syntax */
		if (sscanf(curfile->name, "%lli:%c", &(curfile->length),
		    &dummy) == 2)
			curfile->name = strchr(curfile->name, ':') + 1;
		else
			curfile->length = -1;

		/* Read path argument if enabled */
		if (multiple) {
			if (++n >= argc)
				errx(EXIT_FAILURE, "Missing path argument "
				    "for label number %i", nfiles);
			curfile->path = argv[n];
		} else
			break;
	}

	/* Display usage and exit if not given at least one name */
	if (nfiles == 0) {
		warnx("no labels provided");
		usage();
		/* NOTREACHED */
	}

	/*
	 * Set cleanup routine for Ctrl-C action
	 */
	if (config->display_type == DPV_DISPLAY_LIBDIALOG) {
		act.sa_handler = sig_int;
		sigaction(SIGINT, &act, 0);
	}

	/* Set status formats and action */
	if (line_mode) {
		config->status_solo = LINE_STATUS_SOLO;
		config->status_many = LINE_STATUS_SOLO;
		config->action = operate_on_lines;
	} else {
		config->status_solo = BYTE_STATUS_SOLO;
		config->status_many = BYTE_STATUS_SOLO;
		config->action = operate_on_bytes;
	}

	/*
	 * Hand off to dpv(3)...
	 */
	if (dpv(config, file_list) != 0 && debug)
		warnx("dpv(3) returned error!?");

	if (!config->keep_tite)
		end_dialog();
	dpv_free();

	exit(EXIT_SUCCESS);
}

/*
 * Interrupt handler to indicate we received a Ctrl-C interrupt.
 */
static void
sig_int(int sig __unused)
{
	dpv_interrupt = TRUE;
}

/*
 * Print short usage statement to stderr and exit with error status.
 */
static void
usage(void)
{

	if (debug) /* No need for usage */
		exit(EXIT_FAILURE);

	fprintf(stderr, "Usage: %s [options] bytes:label\n", pgm);
	fprintf(stderr, "       %s [options] -m bytes1:label1 path1 "
	    "[bytes2:label2 path2 ...]\n", pgm);
	fprintf(stderr, "OPTIONS:\n");
#define OPTFMT "\t%-14s %s\n"
	fprintf(stderr, OPTFMT, "-a text",
	    "Append text. Displayed below file progress indicators.");
	fprintf(stderr, OPTFMT, "-b backtitle",
	    "String to be displayed on the backdrop, at top-left.");
	fprintf(stderr, OPTFMT, "-d",
	    "Debug. Write to standard output instead of dialog.");
	fprintf(stderr, OPTFMT, "-D",
	    "Use dialog(1) instead of dialog(3) [default].");
	fprintf(stderr, OPTFMT, "-h",
	    "Produce this output on standard error and exit.");
	fprintf(stderr, OPTFMT, "-i format",
	    "Customize status line format. See fdpv(1) for details.");
	fprintf(stderr, OPTFMT, "-I format",
	    "Customize status line format. See fdpv(1) for details.");
	fprintf(stderr, OPTFMT, "-L size",
	    "Label size. Must be a number greater than 0, or -1.");
	fprintf(stderr, OPTFMT, "-m",
	    "Enable processing of multiple file argiments.");
	fprintf(stderr, OPTFMT, "-n num",
	    "Display at-most num files per screen. Default is -1.");
	fprintf(stderr, OPTFMT, "-N",
	    "No overrun. Stop reading input at stated length, if any.");
	fprintf(stderr, OPTFMT, "-o file",
	    "Output data to file. First %s replaced with label text.");
	fprintf(stderr, OPTFMT, "-p text",
	    "Prefix text. Displayed above file progress indicators.");
	fprintf(stderr, OPTFMT, "-P size",
	    "Mini-progressbar size. Must be a number greater than 3.");
	fprintf(stderr, OPTFMT, "-t title",
	    "Title string to be displayed at top of dialog(1) box.");
	fprintf(stderr, OPTFMT, "-T",
	    "Test mode. Don't actually read any data, but fake it.");
	fprintf(stderr, OPTFMT, "-U num",
	    "Update status line num times per-second. Default is 2.");
	fprintf(stderr, OPTFMT, "-w",
	    "Wide. Width of `-p' and `-a' text bump dialog(1) width.");
	fprintf(stderr, OPTFMT, "-x cmd",
	    "Send data to executed cmd. First %s replaced with label.");
	fprintf(stderr, OPTFMT, "-X",
	    "X11. Use Xdialog(1) instead of dialog(1).");
	exit(EXIT_FAILURE);
}
