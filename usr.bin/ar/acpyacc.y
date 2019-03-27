%{
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Kai Wang
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

#include <sys/mman.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "ar.h"

#define TEMPLATE "arscp.XXXXXXXX"

struct list {
	char		*str;
	struct list	*next;
};


extern int	yylex(void);

static void	yyerror(const char *);
static void	arscp_addlib(char *archive, struct list *list);
static void	arscp_addmod(struct list *list);
static void	arscp_clear(void);
static int	arscp_copy(int ifd, int ofd);
static void	arscp_create(char *in, char *out);
static void	arscp_delete(struct list *list);
static void	arscp_dir(char *archive, struct list *list, char *rlt);
static void	arscp_end(int eval);
static void	arscp_extract(struct list *list);
static void	arscp_free_argv(void);
static void	arscp_free_mlist(struct list *list);
static void	arscp_list(void);
static struct list *arscp_mlist(struct list *list, char *str);
static void	arscp_mlist2argv(struct list *list);
static int	arscp_mlist_len(struct list *list);
static void	arscp_open(char *fname);
static void	arscp_prompt(void);
static void	arscp_replace(struct list *list);
static void	arscp_save(void);
static int	arscp_target_exist(void);

extern int		 lineno;

static struct bsdar	*bsdar;
static char		*target;
static char		*tmpac;
static int		 interactive;
static int		 verbose;

%}

%token ADDLIB
%token ADDMOD
%token CLEAR
%token CREATE
%token DELETE
%token DIRECTORY
%token END
%token EXTRACT
%token LIST
%token OPEN
%token REPLACE
%token VERBOSE
%token SAVE
%token LP
%token RP
%token COMMA
%token EOL
%token <str> FNAME
%type <list> mod_list

%union {
	char		*str;
	struct list	*list;
}

%%

begin
	: { arscp_prompt(); } ar_script
	;

ar_script
	: cmd_list
	|
	;

mod_list
	: FNAME { $$ = arscp_mlist(NULL, $1); }
	| mod_list separator FNAME { $$ = arscp_mlist($1, $3); }
	;

separator
	: COMMA
	|
	;

cmd_list
	: rawcmd
	| cmd_list rawcmd
	;

rawcmd
	: cmd EOL { arscp_prompt(); }
	;

cmd
	: addlib_cmd
	| addmod_cmd
	| clear_cmd
	| create_cmd
	| delete_cmd
	| directory_cmd
	| end_cmd
	| extract_cmd
	| list_cmd
	| open_cmd
	| replace_cmd
	| verbose_cmd
	| save_cmd
	| invalid_cmd
	| empty_cmd
	| error
	;

addlib_cmd
	: ADDLIB FNAME LP mod_list RP { arscp_addlib($2, $4); }
	| ADDLIB FNAME { arscp_addlib($2, NULL); }
	;

addmod_cmd
	: ADDMOD mod_list { arscp_addmod($2); }
	;

clear_cmd
	: CLEAR { arscp_clear(); }
	;

create_cmd
	: CREATE FNAME { arscp_create(NULL, $2); }
	;

delete_cmd
	: DELETE mod_list { arscp_delete($2); }
	;

directory_cmd
	: DIRECTORY FNAME { arscp_dir($2, NULL, NULL); }
	| DIRECTORY FNAME LP mod_list RP { arscp_dir($2, $4, NULL); }
	| DIRECTORY FNAME LP mod_list RP FNAME { arscp_dir($2, $4, $6); }
	;

end_cmd
	: END { arscp_end(EX_OK); }
	;

extract_cmd
	: EXTRACT mod_list { arscp_extract($2); }
	;

list_cmd
	: LIST { arscp_list(); }
	;

open_cmd
	: OPEN FNAME { arscp_open($2); }
	;

replace_cmd
	: REPLACE mod_list { arscp_replace($2); }
	;

save_cmd
	: SAVE { arscp_save(); }
	;

verbose_cmd
	: VERBOSE { verbose = !verbose; }
	;

empty_cmd
	:
	;

invalid_cmd
	: FNAME { yyerror(NULL); }
	;

%%

/* ARGSUSED */
static void
yyerror(const char *s)
{

	(void) s;
	printf("Syntax error in archive script, line %d\n", lineno);
}

/*
 * arscp_open first open an archive and check its validity. If the archive
 * format is valid, it calls arscp_create to create a temporary copy of
 * the archive.
 */
static void
arscp_open(char *fname)
{
	struct archive		*a;
	struct archive_entry	*entry;
	int			 r;

	if ((a = archive_read_new()) == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, 0, "archive_read_new failed");
	archive_read_support_format_ar(a);
	AC(archive_read_open_filename(a, fname, DEF_BLKSZ));
	if ((r = archive_read_next_header(a, &entry)))
		bsdar_warnc(bsdar, archive_errno(a), "%s",
		    archive_error_string(a));
	AC(archive_read_close(a));
	AC(archive_read_free(a));
	if (r != ARCHIVE_OK)
		return;
	arscp_create(fname, fname);
}

/*
 * Create archive. in != NULL indicate it's a OPEN cmd, and resulting
 * archive is based on modification of an existing one. If in == NULL,
 * we are in CREATE cmd and a new empty archive will be created.
 */
static void
arscp_create(char *in, char *out)
{
	struct archive		*a;
	int			 ifd, ofd;

	/* Delete previously created temporary archive, if any. */
	if (tmpac) {
		if (unlink(tmpac) < 0)
			bsdar_errc(bsdar, EX_IOERR, errno, "unlink failed");
		free(tmpac);
	}

	tmpac = strdup(TEMPLATE);
	if (tmpac == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "strdup failed");
	if ((ofd = mkstemp(tmpac)) < 0)
		bsdar_errc(bsdar, EX_IOERR, errno, "mkstemp failed");

	if (in) {
		/*
		 * Command OPEN creates a temporary copy of the
		 * input archive.
		 */
		if ((ifd = open(in, O_RDONLY)) < 0) {
			bsdar_warnc(bsdar, errno, "open failed");
			return;
		}
		if (arscp_copy(ifd, ofd)) {
			bsdar_warnc(bsdar, 0, "arscp_copy failed");
			return;
		}
		close(ifd);
		close(ofd);
	} else {
		/*
		 * Command CREATE creates an "empty" archive.
		 * (archive with only global header)
		 */
		if ((a = archive_write_new()) == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, 0,
			    "archive_write_new failed");
		archive_write_set_format_ar_svr4(a);
		AC(archive_write_open_fd(a, ofd));
		AC(archive_write_close(a));
		AC(archive_write_free(a));
	}

	/* Override previous target, if any. */
	if (target)
		free(target);

	target = out;
	bsdar->filename = tmpac;
}

/* A file copying implementation using mmap. */
static int
arscp_copy(int ifd, int ofd)
{
	struct stat		 sb;
	char			*buf, *p;
	ssize_t			 w;
	size_t			 bytes;

	if (fstat(ifd, &sb) < 0) {
		bsdar_warnc(bsdar, errno, "fstate failed");
		return (1);
	}
	if ((p = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, ifd,
	    (off_t)0)) == MAP_FAILED) {
		bsdar_warnc(bsdar, errno, "mmap failed");
		return (1);
	}
	for (buf = p, bytes = sb.st_size; bytes > 0; bytes -= w) {
		w = write(ofd, buf, bytes);
		if (w <= 0) {
			bsdar_warnc(bsdar, errno, "write failed");
			break;
		}
	}
	if (munmap(p, sb.st_size) < 0)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "munmap failed");
	if (bytes > 0)
		return (1);

	return (0);
}

/*
 * Add all modules of archive to current archive, if list != NULL,
 * only those modules specified in 'list' will be added.
 */
static void
arscp_addlib(char *archive, struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	bsdar->addlib = archive;
	ar_mode_A(bsdar);
	arscp_free_argv();
	arscp_free_mlist(list);
}

/* Add modules into current archive. */
static void
arscp_addmod(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_mode_q(bsdar);
	arscp_free_argv();
	arscp_free_mlist(list);
}

/* Delete modules from current archive. */
static void
arscp_delete(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_mode_d(bsdar);
	arscp_free_argv();
	arscp_free_mlist(list);
}

/* Extract modules from current archive. */
static void
arscp_extract(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_mode_x(bsdar);
	arscp_free_argv();
	arscp_free_mlist(list);
}

/* List modules of archive. (Simple Mode) */
static void
arscp_list(void)
{

	if (!arscp_target_exist())
		return;
	bsdar->argc = 0;
	bsdar->argv = NULL;
	/* Always verbose. */
	bsdar->options |= AR_V;
	ar_mode_t(bsdar);
	bsdar->options &= ~AR_V;
}

/* List modules of archive. (Advance Mode) */
static void
arscp_dir(char *archive, struct list *list, char *rlt)
{
	FILE	*out;

	/* If rlt != NULL, redirect output to it */
	out = NULL;
	if (rlt) {
		out = stdout;
		if ((stdout = fopen(rlt, "w")) == NULL)
			bsdar_errc(bsdar, EX_IOERR, errno,
			    "fopen %s failed", rlt);
	}

	bsdar->filename = archive;
	if (list)
		arscp_mlist2argv(list);
	else {
		bsdar->argc = 0;
		bsdar->argv = NULL;
	}
	if (verbose)
		bsdar->options |= AR_V;
	ar_mode_t(bsdar);
	bsdar->options &= ~AR_V;

	if (rlt) {
		if (fclose(stdout) == EOF)
			bsdar_errc(bsdar, EX_IOERR, errno,
			    "fclose %s failed", rlt);
		stdout = out;
		free(rlt);
	}
	free(archive);
	bsdar->filename = tmpac;
	arscp_free_argv();
	arscp_free_mlist(list);
}


/* Replace modules of current archive. */
static void
arscp_replace(struct list *list)
{

	if (!arscp_target_exist())
		return;
	arscp_mlist2argv(list);
	ar_mode_r(bsdar);
	arscp_free_argv();
	arscp_free_mlist(list);
}

/* Rename the temporary archive to the target archive. */
static void
arscp_save(void)
{
	mode_t mask;

	if (target) {
		if (rename(tmpac, target) < 0)
			bsdar_errc(bsdar, EX_IOERR, errno, "rename failed");
		/*
		 * mkstemp creates temp files with mode 0600, here we
		 * set target archive mode per process umask.
		 */
		mask = umask(0);
		umask(mask);
		if (chmod(target, 0666 & ~mask) < 0)
			bsdar_errc(bsdar, EX_IOERR, errno, "chmod failed");
		free(tmpac);
		free(target);
		tmpac = NULL;
		target= NULL;
		bsdar->filename = NULL;
	} else
		bsdar_warnc(bsdar, 0, "no open output archive");
}

/*
 * Discard all the contents of current archive. This is achieved by
 * invoking CREATE cmd on current archive.
 */
static void
arscp_clear(void)
{
	char		*new_target;

	if (target) {
		new_target = strdup(target);
		if (new_target == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "strdup failed");
		arscp_create(NULL, new_target);
	}
}

/*
 * Quit ar(1). Note that END cmd will not SAVE current archive
 * before exit.
 */
static void
arscp_end(int eval)
{

	if (target)
		free(target);
	if (tmpac) {
		if (unlink(tmpac) == -1)
			bsdar_errc(bsdar, EX_IOERR, errno, "unlink %s failed",
			    tmpac);
		free(tmpac);
	}

	exit(eval);
}

/*
 * Check if target specified, i.e, whether OPEN or CREATE has been
 * issued by user.
 */
static int
arscp_target_exist(void)
{

	if (target)
		return (1);

	bsdar_warnc(bsdar, 0, "no open output archive");
	return (0);
}

/* Construct module list. */
static struct list *
arscp_mlist(struct list *list, char *str)
{
	struct list *l;

	l = malloc(sizeof(*l));
	if (l == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");
	l->str = str;
	l->next = list;

	return (l);
}

/* Calculate the length of a mlist. */
static int
arscp_mlist_len(struct list *list)
{
	int len;

	for(len = 0; list; list = list->next)
		len++;

	return (len);
}

/* Free the space allocated for mod_list. */
static void
arscp_free_mlist(struct list *list)
{
	struct list *l;

	/* Note that list->str was freed in arscp_free_argv. */
	for(; list; list = l) {
		l = list->next;
		free(list);
	}
}

/* Convert mlist to argv array. */
static void
arscp_mlist2argv(struct list *list)
{
	char	**argv;
	int	  i, n;

	n = arscp_mlist_len(list);
	argv = malloc(n * sizeof(*argv));
	if (argv == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");

	/* Note that module names are stored in reverse order in mlist. */
	for(i = n - 1; i >= 0; i--, list = list->next) {
		if (list == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "invalid mlist");
		argv[i] = list->str;
	}

	bsdar->argc = n;
	bsdar->argv = argv;
}

/* Free space allocated for argv array and its elements. */
static void
arscp_free_argv(void)
{
	int i;

	for(i = 0; i < bsdar->argc; i++)
		free(bsdar->argv[i]);

	free(bsdar->argv);
}

/* Show a prompt if we are in interactive mode */
static void
arscp_prompt(void)
{

	if (interactive) {
		printf("AR >");
		fflush(stdout);
	}
}

/* Main function for ar script mode. */
void
ar_mode_script(struct bsdar *ar)
{

	bsdar = ar;
	interactive = isatty(fileno(stdin));
	while(yyparse()) {
		if (!interactive)
			arscp_end(1);
	}

	/* Script ends without END */
	arscp_end(EX_OK);
}
