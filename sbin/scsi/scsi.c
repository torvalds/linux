/*	$OpenBSD: scsi.c,v 1.32 2022/12/04 23:50:47 cheloha Exp $	*/
/*	$FreeBSD: scsi.c,v 1.11 1996/04/06 11:00:28 joerg Exp $	*/

/*
 * Written By Julian ELischer
 * Copyright julian Elischer 1993.
 * Permission is granted to use or redistribute this file in any way as long
 * as this notice remains. Julian Elischer does not guarantee that this file
 * is totally correct for any given task and users of this file must
 * accept responsibility for any damage that occurs from the application of this
 * file.
 *
 * (julian@tfs.com julian@dialix.oz.au)
 *
 * User SCSI hooks added by Peter Dufault:
 *
 * Copyright (c) 1994 HD Associates
 * (contact: dufault@hda.com)
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
 * 3. The name of HD Associates
 *    may not be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY HD ASSOCIATES ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL HD ASSOCIATES BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/scsiio.h>
#include <ctype.h>
#include <signal.h>
#include <err.h>
#include <paths.h>

#include "libscsi.h"

int	fd;
int	debuglevel;
int	debugflag;
int commandflag;
int verbose = 0;

int modeflag;
int editflag;
int modepage = 0; /* Read this mode page */
int pagectl = 0;  /* Mode sense page control */
int seconds = 2;

void	procargs(int *argc_p, char ***argv_p);
int	iget(void *hook, char *name);
char	*cget(void *hook, char *name);
void	arg_put(void *hook, int letter, void *arg, int count, char *name);
void	mode_sense(int fd, u_char *data, int len, int pc, int page);
void	mode_select(int fd, u_char *data, int len, int perm);
int	editit(const char *pathname);

static void
usage(void)
{
	fprintf(stderr,
"usage: scsi -f device -d debug_level\n"
"       scsi -f device -m page [-e] [-P pc]\n"
"       scsi -f device [-v] [-s seconds] -c cmd_fmt [arg ...]"
" -o count out_fmt\n"
"            [arg ...] -i count in_fmt [arg ...]\n");

	exit (1);
}

void
procargs(int *argc_p, char ***argv_p)
{
	int argc = *argc_p;
	char **argv = *argv_p;
	int fflag, ch;

	fflag = 0;
	commandflag = 0;
	debugflag = 0;
	while ((ch = getopt(argc, argv, "cef:d:m:P:s:v")) != -1) {
		switch (ch) {
		case 'c':
			commandflag = 1;
			break;
		case 'e':
			editflag = 1;
			break;
		case 'f':
			if ((fd = scsi_open(optarg, O_RDWR)) < 0)
				err(1, "unable to open device %s", optarg);
			fflag = 1;
			break;
		case 'd':
			debuglevel = strtol(optarg, 0, 0);
			debugflag = 1;
			break;
		case 'm':
			modeflag = 1;
			modepage = strtol(optarg, 0, 0);
			break;
		case 'P':
			pagectl = strtol(optarg, 0, 0);
			break;
		case 's':
			seconds = strtol(optarg, 0, 0);
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}
	*argc_p = argc - optind;
	*argv_p = argv + optind;

	if (!fflag) usage();
}

/* get_hook: Structure for evaluating args in a callback.
 */
struct get_hook
{
	int argc;
	char **argv;
	int got;
};

/* iget: Integer argument callback
 */
int
iget(void *hook, char *name)
{
	struct get_hook *h = (struct get_hook *)hook;
	int arg;

	if (h->got >= h->argc)
	{
		fprintf(stderr, "Expecting an integer argument.\n");
		usage();
	}
	arg = strtol(h->argv[h->got], 0, 0);
	h->got++;

	if (verbose && name && *name)
		printf("%s: %d\n", name, arg);

	return arg;
}

/* cget: char * argument callback
 */
char *
cget(void *hook, char *name)
{
	struct get_hook *h = (struct get_hook *)hook;
	char *arg;

	if (h->got >= h->argc)
	{
		fprintf(stderr, "Expecting a character pointer argument.\n");
		usage();
	}
	arg = h->argv[h->got];
	h->got++;

	if (verbose && name)
		printf("cget: %s: %s", name, arg);

	return arg;
}

/* arg_put: "put argument" callback
 */
void arg_put(void *hook, int letter, void *arg, int count, char *name)
{
	if (verbose && name && *name)
		printf("%s:  ", name);

	switch(letter)
	{
		case 'i':
		case 'b':
		printf("%ld ", (long)arg);
		break;

		case 'c':
		case 'z':
		{
			char *p = malloc(count + 1);
			if (p == NULL)
				err(1, NULL);

			p[count] = 0;
			strncpy(p, (char *)arg, count);
			if (letter == 'z')
			{
				int i;
				for (i = count - 1; i >= 0; i--)
					if (p[i] == ' ')
						p[i] = 0;
					else
						break;
			}
			printf("%s ", p);
			free(p);
		}

		break;

		default:
		printf("Unknown format letter: '%c'\n", letter);
	}
	if (verbose)
		putchar('\n');
}

/* data_phase: SCSI bus data phase: DATA IN, DATA OUT, or no data transfer.
 */
enum data_phase {none = 0, in, out};

/* do_cmd: Send a command to a SCSI device
 */
static void
do_cmd(int fd, char *fmt, int argc, char **argv)
{
	struct get_hook h;
	scsireq_t *scsireq = scsireq_new();
	enum data_phase data_phase;
	int count, amount;
	char *data_fmt, *bp;

	h.argc = argc;
	h.argv = argv;
	h.got = 0;

	scsireq_reset(scsireq);

	scsireq_build_visit(scsireq, 0, 0, 0, fmt, iget, (void *)&h);

	/* Three choices here:
	 * 1. We've used up all the args and have no data phase.
	 * 2. We have input data ("-i")
	 * 3. We have output data ("-o")
	 */

	if (h.got >= h.argc)
	{
		data_phase = none;
		count = scsireq->datalen = 0;
	}
	else
	{
		char *flag = cget(&h, 0);

		if (strcmp(flag, "-o") == 0)
		{
			data_phase = out;
			scsireq->flags = SCCMD_WRITE;
		}
		else if (strcmp(flag, "-i") == 0)
		{
			data_phase = in;
			scsireq->flags = SCCMD_READ;
		}
		else
		{
			fprintf(stderr,
			"Need either \"-i\" or \"-o\" for data phase; not \"%s\".\n", flag);
			usage();
		}

		count = scsireq->datalen = iget(&h, 0);
		if (count) {
			data_fmt = cget(&h, 0);

			scsireq->databuf = malloc(count);
			if (scsireq->databuf == NULL)
				err(1, NULL);

			if (data_phase == out) {
				if (strcmp(data_fmt, "-") == 0)	{
					bp = (char *)scsireq->databuf;
					while (count > 0 &&
					    (amount = read(STDIN_FILENO,
					    bp, count)) > 0) {
						count -= amount;
						bp += amount;
					}
					if (amount == -1)
						err(1, "read");
					else if (amount == 0) {
						/* early EOF */
						fprintf(stderr,
							"Warning: only read %lu bytes out of %lu.\n",
							scsireq->datalen - (u_long)count,
							scsireq->datalen);
						scsireq->datalen -= (u_long)count;
					}
				}
				else
				{
					bzero(scsireq->databuf, count);
					scsireq_encode_visit(scsireq, data_fmt, iget, (void *)&h);
				}
			}
		}
	}


	scsireq->timeout = seconds * 1000;

	if (scsireq_enter(fd, scsireq) == -1)
	{
		scsi_debug(stderr, -1, scsireq);
		exit(1);
	}

	if (SCSIREQ_ERROR(scsireq))
		scsi_debug(stderr, 0, scsireq);

	if (count && data_phase == in)
	{
		if (strcmp(data_fmt, "-") == 0)	/* stdout */
		{
			bp = (char *)scsireq->databuf;
			while (count > 0 && (amount = write(STDOUT_FILENO, bp, count)) > 0)
			{
				count -= amount;
				bp += amount;
			}
			if (amount < 0)
				err(1, "write");
			else if (amount == 0)
				fprintf(stderr, "Warning: wrote only %lu bytes out of %lu.\n",
					scsireq->datalen - count,
					scsireq->datalen);

		}
		else
		{
			scsireq_decode_visit(scsireq, data_fmt, arg_put, 0);
			putchar('\n');
		}
	}
}

void mode_sense(int fd, u_char *data, int len, int pc, int page)
{
	scsireq_t *scsireq;

	bzero(data, len);

	scsireq = scsireq_new();

	if (scsireq_enter(fd, scsireq_build(scsireq,
	 len, data, SCCMD_READ,
	 "1A 0 v:2 {Page Control} v:6 {Page Code} 0 v:i1 {Allocation Length} 0",
	 pc, page, len)) == -1)	/* Mode sense */
	{
		scsi_debug(stderr, -1, scsireq);
		exit(1);
	}

	if (SCSIREQ_ERROR(scsireq))
	{
		scsi_debug(stderr, 0, scsireq);
		exit(1);
	}

	free(scsireq);
}

void mode_select(int fd, u_char *data, int len, int perm)
{
	scsireq_t *scsireq;

	scsireq = scsireq_new();

	if (scsireq_enter(fd, scsireq_build(scsireq,
	 len, data, SCCMD_WRITE,
	 "15 0:7 v:1 {SP} 0 0 v:i1 {Allocation Length} 0", perm, len)) == -1)	/* Mode select */
	{
		scsi_debug(stderr, -1, scsireq);
		exit(1);
	}

	if (SCSIREQ_ERROR(scsireq))
	{
		scsi_debug(stderr, 0, scsireq);
		exit(1);
	}

	free(scsireq);
}


#define START_ENTRY '{'
#define END_ENTRY '}'

static void
skipwhite(FILE *f)
{
	int c;

skip_again:

	while (isspace(c = getc(f)))
		continue;

	if (c == '#') {
		while ((c = getc(f)) != '\n' && c != EOF)
			continue;
		goto skip_again;
	}

	ungetc(c, f);
}

/* mode_lookup: Lookup a format description for a given page.
 */
char *mode_db = "/usr/share/misc/scsi_modes";
static char *mode_lookup(int page)
{
	char *new_db;
	FILE *modes;
	int match, next, found, c;
	static char fmt[1024];	/* XXX This should be with strealloc */
	int page_desc;
	new_db = getenv("SCSI_MODES");

	if (new_db)
		mode_db = new_db;

	modes = fopen(mode_db, "r");
	if (modes == NULL)
		return 0;

	next = 0;
	found = 0;

	while (!found) {

		skipwhite(modes);

		if (fscanf(modes, "%i", &page_desc) != 1)
			break;

		if (page_desc == page)
			found = 1;

		skipwhite(modes);
		if (getc(modes) != START_ENTRY) {
			errx(1, "Expected %c", START_ENTRY);
		}

		match = 1;
		while (match != 0) {
			c = getc(modes);
			if (c == EOF)
				fprintf(stderr, "Expected %c.\n", END_ENTRY);

			if (c == START_ENTRY) {
				match++;
			}
			if (c == END_ENTRY) {
				match--;
				if (match == 0)
					break;
			}
			if (found && c != '\n') {
				if (next >= sizeof(fmt)) {
					errx(1, "Stupid program: Buffer overflow.\n");
				}

				fmt[next++] = (u_char)c;
			}
		}
	}
	fclose(modes);
	fmt[next] = 0;

	return (found) ? fmt : 0;
}

/* -------- edit: Mode Select Editor ---------
 */
struct editinfo
{
	long can_edit;
	long default_value;
} editinfo[64];	/* XXX Bogus fixed size */

static int editind;
volatile int edit_opened;
static FILE *edit_file;
static char edit_name[L_tmpnam];

static void
edit_rewind(void)
{
	editind = 0;
}

static void
edit_done(void)
{
	int opened;

	sigset_t all, prev;
	sigfillset(&all);

	(void)sigprocmask(SIG_SETMASK, &all, &prev);

	opened = (int)edit_opened;
	edit_opened = 0;

	(void)sigprocmask(SIG_SETMASK, &prev, 0);

	if (opened)
	{
		if (fclose(edit_file))
			perror(edit_name);
		if (unlink(edit_name))
			perror(edit_name);
	}
}

static void
edit_init(void)
{
	int fd;

	edit_rewind();
	strlcpy(edit_name, "/var/tmp/scXXXXXXXX", sizeof edit_name);
	if ((fd = mkstemp(edit_name)) == -1)
		err(1, "mkstemp");
	if ( (edit_file = fdopen(fd, "w+")) == 0)
		err(1, "fdopen");
	edit_opened = 1;

	atexit(edit_done);
}

static void
edit_check(void *hook, int letter, void *arg, int count, char *name)
{
	if (letter != 'i' && letter != 'b') {
		errx(1, "Can't edit format %c.\n", letter);
	}

	if (editind >= sizeof(editinfo) / sizeof(editinfo[0])) {
		errx(1, "edit table overflow");
	}
	editinfo[editind].can_edit = ((long)arg != 0);
	editind++;
}

static void
edit_defaults(void *hook, int letter, void *arg, int count, char *name)
{
	if (letter != 'i' && letter != 'b') {
		errx(1, "Can't edit format %c.\n", letter);
	}

	editinfo[editind].default_value = ((long)arg);
	editind++;
}

static void
edit_report(void *hook, int letter, void *arg, int count, char *name)
{
	if (editinfo[editind].can_edit) {
		if (letter != 'i' && letter != 'b') {
			errx(1, "Can't report format %c.\n", letter);
		}

		fprintf(edit_file, "%s:  %ld\n", name, (long)arg);
	}

	editind++;
}

static int
edit_get(void *hook, char *name)
{
	int arg = editinfo[editind].default_value;

	if (editinfo[editind].can_edit) {
		char line[80];
		size_t len;
		if (fgets(line, sizeof(line), edit_file) == NULL)
			err(1, "fgets");

		len = strlen(line);
		if (len && line[len - 1] == '\n')
			line[len - 1] = '\0';

		if (strncmp(name, line, strlen(name)) != 0) {
			errx(1, "Expected \"%s\" and read \"%s\"\n",
			    name, line);
		}

		arg = strtoul(line + strlen(name) + 2, 0, 0);
	}

	editind++;
	return arg;
}

int
editit(const char *pathname)
{
	char *argp[] = {"sh", "-c", NULL, NULL}, *ed, *p;
	sig_t sighup, sigint, sigquit;
	pid_t pid;
	int st;

	ed = getenv("VISUAL");
	if (ed == NULL || ed[0] == '\0')
		ed = getenv("EDITOR");
	if (ed == NULL || ed[0] == '\0')
		ed = _PATH_VI;
	if (asprintf(&p, "%s %s", ed, pathname) == -1)
		return (-1);
	argp[2] = p;

 top:
	sighup = signal(SIGHUP, SIG_IGN);
	sigint = signal(SIGINT, SIG_IGN);
	sigquit = signal(SIGQUIT, SIG_IGN);
	if ((pid = fork()) == -1) {
		int saved_errno = errno;

		(void)signal(SIGHUP, sighup);
		(void)signal(SIGINT, sigint);
		(void)signal(SIGQUIT, sigquit);
		if (saved_errno == EAGAIN) {
			sleep(1);
			goto top;
		}
		free(p);
		errno = saved_errno;
		return (-1);
	}
	if (pid == 0) {
		execv(_PATH_BSHELL, argp);
		_exit(127);
	}
	free(p);
	for (;;) {
		if (waitpid(pid, &st, 0) == -1) {
			if (errno != EINTR)
				return (-1);
		} else
			break;
	}
	(void)signal(SIGHUP, sighup);
	(void)signal(SIGINT, sigint);
	(void)signal(SIGQUIT, sigquit);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
		errno = ECHILD;
		return (-1);
	}
	return (0);
}

static void
mode_edit(int fd, int page, int edit, int argc, char *argv[])
{
	int i;
	u_char data[255];
	u_char *mode_pars;
	struct mode_header
	{
		u_char mdl;	/* Mode data length */
		u_char medium_type;
		u_char dev_spec_par;
		u_char bdl;	/* Block descriptor length */
	};

	struct mode_page_header
	{
		u_char page_code;
		u_char page_length;
	};

	struct mode_header *mh;
	struct mode_page_header *mph;

	char *fmt = mode_lookup(page);
	if (!fmt && verbose) {
		fprintf(stderr,
		"No mode data base entry in \"%s\" for page %d;  binary %s only.\n",
		mode_db, page, (edit ? "edit" : "display"));
	}

	if (edit) {
		if (!fmt) {
			errx(1, "Sorry: can't edit without a format.\n");
		}

		if (pagectl != 0 && pagectl != 3) {
			errx(1,
"It only makes sense to edit page 0 (current) or page 3 (saved values)\n");
		}

		verbose = 1;

		mode_sense(fd, data, sizeof(data), 1, page);

		mh = (struct mode_header *)data;
		mph = (struct mode_page_header *)
		(((char *)mh) + sizeof(*mh) + mh->bdl);

		mode_pars = (char *)mph + sizeof(*mph);

		edit_init();
		scsireq_buff_decode_visit(mode_pars, mh->mdl,
		fmt, edit_check, 0);

		mode_sense(fd, data, sizeof(data), 0, page);

		edit_rewind();
		scsireq_buff_decode_visit(mode_pars, mh->mdl,
		fmt, edit_defaults, 0);

		edit_rewind();
		scsireq_buff_decode_visit(mode_pars, mh->mdl,
		fmt, edit_report, 0);

		fclose(edit_file);
		if (editit(edit_name) == -1 && errno != ECHILD)
			err(1, "edit %s", edit_name);
		if ((edit_file = fopen(edit_name, "r")) == NULL)
			err(1, "open %s", edit_name);

		edit_rewind();
		scsireq_buff_encode_visit(mode_pars, mh->mdl,
		fmt, edit_get, 0);

		/* Eliminate block descriptors:
		 */
		bcopy((char *)mph, ((char *)mh) + sizeof(*mh),
		sizeof(*mph) + mph->page_length);

		mh->bdl = 0;
		mph = (struct mode_page_header *) (((char *)mh) + sizeof(*mh));
		mode_pars = ((char *)mph) + 2;

#if 0
		/* Turn this on to see what you're sending to the
		 * device:
		 */
		edit_rewind();
		scsireq_buff_decode_visit(mode_pars,
		mh->mdl, fmt, arg_put, 0);
#endif

		edit_done();

		/* Make it permanent if pageselect is three.
		 */

		mph->page_code &= ~0xC0;	/* Clear PS and RESERVED */
		mh->mdl = 0;				/* Reserved for mode select */

		mode_select(fd, (char *)mh,
		sizeof(*mh) + mh->bdl + sizeof(*mph) + mph->page_length,
		(pagectl == 3));

		exit(0);
	}

	mode_sense(fd, data, sizeof(data), pagectl, page);

	/* Skip over the block descriptors.
	 */
	mh = (struct mode_header *)data;
	mph = (struct mode_page_header *)(((char *)mh) + sizeof(*mh) + mh->bdl);
	mode_pars = (char *)mph + sizeof(*mph);

	if (!fmt) {
		for (i = 0; i < mh->mdl; i++) {
			printf("%02x%c",mode_pars[i],
			(((i + 1) % 8) == 0) ? '\n' : ' ');
		}
		putc('\n', stdout);
	} else {
			verbose = 1;
			scsireq_buff_decode_visit(mode_pars,
			mh->mdl, fmt, arg_put, 0);
	}
}

int
main(int argc, char **argv)
{
	procargs(&argc,&argv);

	/* XXX This has grown to the point that it should be cleaned up.
	 */
	if (debugflag) {
		if (ioctl(fd,SCIOCDEBUG,&debuglevel) == -1)
			err(1, "SCIOCDEBUG");
	} else if (commandflag) {
		char *fmt;

		if (argc < 1) {
			fprintf(stderr, "Need the command format string.\n");
			usage();
		}


		fmt = argv[0];

		argc -= 1;
		argv += 1;

		do_cmd(fd, fmt, argc, argv);
	} else if (modeflag)
		mode_edit(fd, modepage, editflag, argc, argv);

	exit(0);
}
