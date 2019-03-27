/*	$NetBSD: el.c,v 1.83 2016/02/24 17:13:22 christos Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"
#if !defined(lint) && !defined(SCCSID)
#if 0
static char sccsid[] = "@(#)el.c	8.2 (Berkeley) 1/3/94";
#else
__RCSID("$NetBSD: el.c,v 1.83 2016/02/24 17:13:22 christos Exp $");
#endif
#endif /* not lint && not SCCSID */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * el.c: EditLine interface functions
 */
#include <sys/types.h>
#include <sys/param.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIDECHAR
#include <locale.h>
#include <langinfo.h>
#endif

#include "el.h"
#include "parse.h"

/* el_init():
 *	Initialize editline and set default parameters.
 */
public EditLine *
el_init(const char *prog, FILE *fin, FILE *fout, FILE *ferr)
{
    return el_init_fd(prog, fin, fout, ferr, fileno(fin), fileno(fout),
	fileno(ferr));
}

public EditLine *
el_init_fd(const char *prog, FILE *fin, FILE *fout, FILE *ferr,
    int fdin, int fdout, int fderr)
{
	EditLine *el = el_malloc(sizeof(*el));

	if (el == NULL)
		return NULL;

	memset(el, 0, sizeof(EditLine));

	el->el_infile = fin;
	el->el_outfile = fout;
	el->el_errfile = ferr;

	el->el_infd = fdin;
	el->el_outfd = fdout;
	el->el_errfd = fderr;

	el->el_prog = Strdup(ct_decode_string(prog, &el->el_scratch));
	if (el->el_prog == NULL) {
		el_free(el);
		return NULL;
	}

	/*
         * Initialize all the modules. Order is important!!!
         */
	el->el_flags = 0;

	if (terminal_init(el) == -1) {
		el_free(el->el_prog);
		el_free(el);
		return NULL;
	}
	(void) keymacro_init(el);
	(void) map_init(el);
	if (tty_init(el) == -1)
		el->el_flags |= NO_TTY;
	(void) ch_init(el);
	(void) search_init(el);
	(void) hist_init(el);
	(void) prompt_init(el);
	(void) sig_init(el);
	(void) read_init(el);

	return el;
}


/* el_end():
 *	Clean up.
 */
public void
el_end(EditLine *el)
{

	if (el == NULL)
		return;

	el_reset(el);

	terminal_end(el);
	keymacro_end(el);
	map_end(el);
	if (!(el->el_flags & NO_TTY))
		tty_end(el);
	ch_end(el);
	search_end(el);
	hist_end(el);
	prompt_end(el);
	sig_end(el);

	el_free(el->el_prog);
#ifdef WIDECHAR
	el_free(el->el_scratch.cbuff);
	el_free(el->el_scratch.wbuff);
	el_free(el->el_lgcyconv.cbuff);
	el_free(el->el_lgcyconv.wbuff);
#endif
	el_free(el);
}


/* el_reset():
 *	Reset the tty and the parser
 */
public void
el_reset(EditLine *el)
{

	tty_cookedmode(el);
	ch_reset(el, 0);		/* XXX: Do we want that? */
}


/* el_set():
 *	set the editline parameters
 */
public int
FUN(el,set)(EditLine *el, int op, ...)
{
	va_list ap;
	int rv = 0;

	if (el == NULL)
		return -1;
	va_start(ap, op);

	switch (op) {
	case EL_PROMPT:
	case EL_RPROMPT: {
		el_pfunc_t p = va_arg(ap, el_pfunc_t);

		rv = prompt_set(el, p, 0, op, 1);
		break;
	}

	case EL_RESIZE: {
		el_zfunc_t p = va_arg(ap, el_zfunc_t);
		void *arg = va_arg(ap, void *);
		rv = ch_resizefun(el, p, arg);
		break;
	}

	case EL_ALIAS_TEXT: {
		el_afunc_t p = va_arg(ap, el_afunc_t);
		void *arg = va_arg(ap, void *);
		rv = ch_aliasfun(el, p, arg);
		break;
	}

	case EL_PROMPT_ESC:
	case EL_RPROMPT_ESC: {
		el_pfunc_t p = va_arg(ap, el_pfunc_t);
		int c = va_arg(ap, int);

		rv = prompt_set(el, p, (Char)c, op, 1);
		break;
	}

	case EL_TERMINAL:
		rv = terminal_set(el, va_arg(ap, char *));
		break;

	case EL_EDITOR:
		rv = map_set_editor(el, va_arg(ap, Char *));
		break;

	case EL_SIGNAL:
		if (va_arg(ap, int))
			el->el_flags |= HANDLE_SIGNALS;
		else
			el->el_flags &= ~HANDLE_SIGNALS;
		break;

	case EL_BIND:
	case EL_TELLTC:
	case EL_SETTC:
	case EL_ECHOTC:
	case EL_SETTY:
	{
		const Char *argv[20];
		int i;

		for (i = 1; i < (int)__arraycount(argv); i++)
			if ((argv[i] = va_arg(ap, Char *)) == NULL)
				break;

		switch (op) {
		case EL_BIND:
			argv[0] = STR("bind");
			rv = map_bind(el, i, argv);
			break;

		case EL_TELLTC:
			argv[0] = STR("telltc");
			rv = terminal_telltc(el, i, argv);
			break;

		case EL_SETTC:
			argv[0] = STR("settc");
			rv = terminal_settc(el, i, argv);
			break;

		case EL_ECHOTC:
			argv[0] = STR("echotc");
			rv = terminal_echotc(el, i, argv);
			break;

		case EL_SETTY:
			argv[0] = STR("setty");
			rv = tty_stty(el, i, argv);
			break;

		default:
			rv = -1;
			EL_ABORT((el->el_errfile, "Bad op %d\n", op));
			break;
		}
		break;
	}

	case EL_ADDFN:
	{
		Char *name = va_arg(ap, Char *);
		Char *help = va_arg(ap, Char *);
		el_func_t func = va_arg(ap, el_func_t);

		rv = map_addfunc(el, name, help, func);
		break;
	}

	case EL_HIST:
	{
		hist_fun_t func = va_arg(ap, hist_fun_t);
		void *ptr = va_arg(ap, void *);

		rv = hist_set(el, func, ptr);
		if (MB_CUR_MAX == 1)
			el->el_flags &= ~NARROW_HISTORY;
		break;
	}

	case EL_EDITMODE:
		if (va_arg(ap, int))
			el->el_flags &= ~EDIT_DISABLED;
		else
			el->el_flags |= EDIT_DISABLED;
		rv = 0;
		break;

	case EL_GETCFN:
	{
		el_rfunc_t rc = va_arg(ap, el_rfunc_t);
		rv = el_read_setfn(el, rc);
		break;
	}

	case EL_CLIENTDATA:
		el->el_data = va_arg(ap, void *);
		break;

	case EL_UNBUFFERED:
		rv = va_arg(ap, int);
		if (rv && !(el->el_flags & UNBUFFERED)) {
			el->el_flags |= UNBUFFERED;
			read_prepare(el);
		} else if (!rv && (el->el_flags & UNBUFFERED)) {
			el->el_flags &= ~UNBUFFERED;
			read_finish(el);
		}
		rv = 0;
		break;

	case EL_PREP_TERM:
		rv = va_arg(ap, int);
		if (rv)
			(void) tty_rawmode(el);
		else
			(void) tty_cookedmode(el);
		rv = 0;
		break;

	case EL_SETFP:
	{
		FILE *fp;
		int what;

		what = va_arg(ap, int);
		fp = va_arg(ap, FILE *);

		rv = 0;
		switch (what) {
		case 0:
			el->el_infile = fp;
			el->el_infd = fileno(fp);
			break;
		case 1:
			el->el_outfile = fp;
			el->el_outfd = fileno(fp);
			break;
		case 2:
			el->el_errfile = fp;
			el->el_errfd = fileno(fp);
			break;
		default:
			rv = -1;
			break;
		}
		break;
	}

	case EL_REFRESH:
		re_clear_display(el);
		re_refresh(el);
		terminal__flush(el);
		break;

	default:
		rv = -1;
		break;
	}

	va_end(ap);
	return rv;
}


/* el_get():
 *	retrieve the editline parameters
 */
public int
FUN(el,get)(EditLine *el, int op, ...)
{
	va_list ap;
	int rv;

	if (el == NULL)
		return -1;

	va_start(ap, op);

	switch (op) {
	case EL_PROMPT:
	case EL_RPROMPT: {
		el_pfunc_t *p = va_arg(ap, el_pfunc_t *);
		rv = prompt_get(el, p, 0, op);
		break;
	}
	case EL_PROMPT_ESC:
	case EL_RPROMPT_ESC: {
		el_pfunc_t *p = va_arg(ap, el_pfunc_t *);
		Char *c = va_arg(ap, Char *);

		rv = prompt_get(el, p, c, op);
		break;
	}

	case EL_EDITOR:
		rv = map_get_editor(el, va_arg(ap, const Char **));
		break;

	case EL_SIGNAL:
		*va_arg(ap, int *) = (el->el_flags & HANDLE_SIGNALS);
		rv = 0;
		break;

	case EL_EDITMODE:
		*va_arg(ap, int *) = !(el->el_flags & EDIT_DISABLED);
		rv = 0;
		break;

	case EL_TERMINAL:
		terminal_get(el, va_arg(ap, const char **));
		rv = 0;
		break;

	case EL_GETTC:
	{
		static char name[] = "gettc";
		char *argv[20];
		int i;

		for (i = 1; i < (int)__arraycount(argv); i++)
			if ((argv[i] = va_arg(ap, char *)) == NULL)
				break;

		argv[0] = name;
		rv = terminal_gettc(el, i, argv);
		break;
	}

	case EL_GETCFN:
		*va_arg(ap, el_rfunc_t *) = el_read_getfn(el);
		rv = 0;
		break;

	case EL_CLIENTDATA:
		*va_arg(ap, void **) = el->el_data;
		rv = 0;
		break;

	case EL_UNBUFFERED:
		*va_arg(ap, int *) = (el->el_flags & UNBUFFERED) != 0;
		rv = 0;
		break;

	case EL_GETFP:
	{
		int what;
		FILE **fpp;

		what = va_arg(ap, int);
		fpp = va_arg(ap, FILE **);
		rv = 0;
		switch (what) {
		case 0:
			*fpp = el->el_infile;
			break;
		case 1:
			*fpp = el->el_outfile;
			break;
		case 2:
			*fpp = el->el_errfile;
			break;
		default:
			rv = -1;
			break;
		}
		break;
	}
	default:
		rv = -1;
		break;
	}
	va_end(ap);

	return rv;
}


/* el_line():
 *	Return editing info
 */
public const TYPE(LineInfo) *
FUN(el,line)(EditLine *el)
{

	return (const TYPE(LineInfo) *)(void *)&el->el_line;
}


/* el_source():
 *	Source a file
 */
public int
el_source(EditLine *el, const char *fname)
{
	FILE *fp;
	size_t len;
	ssize_t slen;
	char *ptr;
	char *path = NULL;
	const Char *dptr;
	int error = 0;

	fp = NULL;
	if (fname == NULL) {
#ifdef HAVE_ISSETUGID
		static const char elpath[] = "/.editrc";
		size_t plen = sizeof(elpath);

		if (issetugid())
			return -1;
		if ((ptr = getenv("HOME")) == NULL)
			return -1;
		plen += strlen(ptr);
		if ((path = el_malloc(plen * sizeof(*path))) == NULL)
			return -1;
		(void)snprintf(path, plen, "%s%s", ptr, elpath);
		fname = path;
#else
		/*
		 * If issetugid() is missing, always return an error, in order
		 * to keep from inadvertently opening up the user to a security
		 * hole.
		 */
		return -1;
#endif
	}
	if (fp == NULL)
		fp = fopen(fname, "r");
	if (fp == NULL) {
		el_free(path);
		return -1;
	}

	ptr = NULL;
	len = 0;
	while ((slen = getline(&ptr, &len, fp)) != -1) {
		if (*ptr == '\n')
			continue;	/* Empty line. */
		if (slen > 0 && ptr[--slen] == '\n')
			ptr[slen] = '\0';

		dptr = ct_decode_string(ptr, &el->el_scratch);
		if (!dptr)
			continue;
		/* loop until first non-space char or EOL */
		while (*dptr != '\0' && Isspace(*dptr))
			dptr++;
		if (*dptr == '#')
			continue;   /* ignore, this is a comment line */
		if ((error = parse_line(el, dptr)) == -1)
			break;
	}
	free(ptr);

	el_free(path);
	(void) fclose(fp);
	return error;
}


/* el_resize():
 *	Called from program when terminal is resized
 */
public void
el_resize(EditLine *el)
{
	int lins, cols;
	sigset_t oset, nset;

	(void) sigemptyset(&nset);
	(void) sigaddset(&nset, SIGWINCH);
	(void) sigprocmask(SIG_BLOCK, &nset, &oset);

	/* get the correct window size */
	if (terminal_get_size(el, &lins, &cols))
		terminal_change_size(el, lins, cols);

	(void) sigprocmask(SIG_SETMASK, &oset, NULL);
}


/* el_beep():
 *	Called from the program to beep
 */
public void
el_beep(EditLine *el)
{

	terminal_beep(el);
}


/* el_editmode()
 *	Set the state of EDIT_DISABLED from the `edit' command.
 */
protected int
/*ARGSUSED*/
el_editmode(EditLine *el, int argc, const Char **argv)
{
	const Char *how;

	if (argv == NULL || argc != 2 || argv[1] == NULL)
		return -1;

	how = argv[1];
	if (Strcmp(how, STR("on")) == 0) {
		el->el_flags &= ~EDIT_DISABLED;
		tty_rawmode(el);
	} else if (Strcmp(how, STR("off")) == 0) {
		tty_cookedmode(el);
		el->el_flags |= EDIT_DISABLED;
	}
	else {
		(void) fprintf(el->el_errfile, "edit: Bad value `" FSTR "'.\n",
		    how);
		return -1;
	}
	return 0;
}
