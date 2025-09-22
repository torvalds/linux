/*	$OpenBSD: eln.c,v 1.18 2016/04/11 21:17:29 schwarze Exp $	*/
/*	$NetBSD: eln.c,v 1.9 2010/11/04 13:53:12 christos Exp $	*/

/*-
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "config.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "el.h"

int
el_getc(EditLine *el, char *cp)
{
	int num_read;
	wchar_t wc = 0;

	num_read = el_wgetc(el, &wc);
	*cp = '\0';
	if (num_read <= 0)
		return num_read;
	num_read = wctob(wc);
	if (num_read == EOF) {
		errno = ERANGE;
		return -1;
	} else {
		*cp = num_read;
		return 1;
	}
}


void
el_push(EditLine *el, const char *str)
{
	/* Using multibyte->wide string decoding works fine under single-byte
	 * character sets too, and Does The Right Thing. */
	el_wpush(el, ct_decode_string(str, &el->el_lgcyconv));
}


const char *
el_gets(EditLine *el, int *nread)
{
	const wchar_t *tmp;
	wchar_t *rd, *wr;

	if ((tmp = el_wgets(el, nread)) == NULL)
		return NULL;

	/*
	 * Temporary until the libedit audit is complete:
	 * Filter out all non-ASCII characters.
	 */
	wr = (wchar_t *)tmp;
	for (rd = wr; *rd != L'\0'; rd++) {
		if (wr < rd)
			*wr = *rd;
		if (*rd < 128)
			wr++;
	}
	*wr = L'\0';
	*nread = wr - tmp;

	return ct_encode_string(tmp, &el->el_lgcyconv);
}


int
el_parse(EditLine *el, int argc, const char *argv[])
{
	int ret;
	const wchar_t **wargv;

	wargv = (const wchar_t **)
	    ct_decode_argv(argc, argv, &el->el_lgcyconv);
	if (!wargv)
		return -1;
	ret = el_wparse(el, argc, wargv);
	free(wargv);

	return ret;
}


int
el_set(EditLine *el, int op, ...)
{
	va_list ap;
	int ret;

	if (!el)
		return -1;
	va_start(ap, op);

	switch (op) {
	case EL_PROMPT:         /* el_pfunc_t */
	case EL_RPROMPT: {
		el_pfunc_t p = va_arg(ap, el_pfunc_t);
		ret = prompt_set(el, p, 0, op, 0);
		break;
	}

	case EL_RESIZE: {
		el_zfunc_t p = va_arg(ap, el_zfunc_t);
		void *arg = va_arg(ap, void *);
		ret = ch_resizefun(el, p, arg);
		break;
	}

	case EL_TERMINAL:       /* const char * */
		ret = el_wset(el, op, va_arg(ap, char *));
		break;

	case EL_EDITOR:		/* const wchar_t * */
		ret = el_wset(el, op, ct_decode_string(va_arg(ap, char *),
		    &el->el_lgcyconv));
		break;

	case EL_SIGNAL:         /* int */
	case EL_EDITMODE:
	case EL_UNBUFFERED:
	case EL_PREP_TERM:
		ret = el_wset(el, op, va_arg(ap, int));
		break;

	case EL_BIND:   /* const char * list -> const wchar_t * list */
	case EL_TELLTC:
	case EL_SETTC:
	case EL_ECHOTC:
	case EL_SETTY: {
		const char *argv[21];
		int i;
		const wchar_t **wargv;
		for (i = 1; i < (int)__arraycount(argv) - 1; ++i)
			if ((argv[i] = va_arg(ap, char *)) == NULL)
			    break;
		argv[0] = argv[i] = NULL;
		wargv = (const wchar_t **)
		    ct_decode_argv(i + 1, argv, &el->el_lgcyconv);
		if (!wargv) {
		    ret = -1;
		    goto out;
		}
		/*
		 * AFAIK we can't portably pass through our new wargv to
		 * el_wset(), so we have to reimplement the body of
		 * el_wset() for these ops.
		 */
		switch (op) {
		case EL_BIND:
			wargv[0] = L"bind";
			ret = map_bind(el, i, wargv);
			break;
		case EL_TELLTC:
			wargv[0] = L"telltc";
			ret = terminal_telltc(el, i, wargv);
			break;
		case EL_SETTC:
			wargv[0] = L"settc";
			ret = terminal_settc(el, i, wargv);
			break;
		case EL_ECHOTC:
			wargv[0] = L"echotc";
			ret = terminal_echotc(el, i, wargv);
			break;
		case EL_SETTY:
			wargv[0] = L"setty";
			ret = tty_stty(el, i, wargv);
			break;
		default:
			ret = -1;
		}
		free(wargv);
		break;
	}

	/* XXX: do we need to change el_func_t too? */
	case EL_ADDFN: {          /* const char *, const char *, el_func_t */
		const char *args[2];
		el_func_t func;
		wchar_t **wargv;

		args[0] = va_arg(ap, const char *);
		args[1] = va_arg(ap, const char *);
		func = va_arg(ap, el_func_t);

		wargv = ct_decode_argv(2, args, &el->el_lgcyconv);
		if (!wargv) {
		    ret = -1;
		    goto out;
		}
		/* XXX: The two strdup's leak */
		ret = map_addfunc(el, wcsdup(wargv[0]), wcsdup(wargv[1]),
		    func);
		free(wargv);
		break;
	}
	case EL_HIST: {           /* hist_fun_t, const char * */
		hist_fun_t fun = va_arg(ap, hist_fun_t);
		void *ptr = va_arg(ap, void *);
		ret = hist_set(el, fun, ptr);
		el->el_flags |= NARROW_HISTORY;
		break;
	}
	case EL_GETCFN:         /* el_rfunc_t */
		ret = el_wset(el, op, va_arg(ap, el_rfunc_t));
		break;
	case EL_CLIENTDATA:     /* void * */
		ret = el_wset(el, op, va_arg(ap, void *));
		break;
	case EL_SETFP: {          /* int, FILE * */
		int what = va_arg(ap, int);
		FILE *fp = va_arg(ap, FILE *);
		ret = el_wset(el, op, what, fp);
		break;
	}
	case EL_PROMPT_ESC: /* el_pfunc_t, char */
	case EL_RPROMPT_ESC: {
		el_pfunc_t p = va_arg(ap, el_pfunc_t);
		char c = va_arg(ap, int);
		ret = prompt_set(el, p, c, op, 0);
		break;
	}
	default:
		ret = -1;
		break;
	}

out:
	va_end(ap);
	return ret;
}


int
el_get(EditLine *el, int op, ...)
{
	va_list ap;
	int ret;

	if (!el)
		return -1;

	va_start(ap, op);

	switch (op) {
	case EL_PROMPT:         /* el_pfunc_t * */
	case EL_RPROMPT: {
		el_pfunc_t *p = va_arg(ap, el_pfunc_t *);
		ret = prompt_get(el, p, 0, op);
		break;
	}

	case EL_PROMPT_ESC: /* el_pfunc_t *, char **/
	case EL_RPROMPT_ESC: {
		el_pfunc_t *p = va_arg(ap, el_pfunc_t *);
		char *c = va_arg(ap, char *);
		wchar_t wc = 0;
		ret = prompt_get(el, p, &wc, op);
		*c = (unsigned char)wc;
		break;
	}

	case EL_EDITOR: {
		const char **p = va_arg(ap, const char **);
		const wchar_t *pw;
		ret = el_wget(el, op, &pw);
		*p = ct_encode_string(pw, &el->el_lgcyconv);
		if (!el->el_lgcyconv.csize)
			ret = -1;
		break;
	}

	case EL_TERMINAL:       /* const char ** */
		ret = el_wget(el, op, va_arg(ap, const char **));
		break;

	case EL_SIGNAL:         /* int * */
	case EL_EDITMODE:
	case EL_UNBUFFERED:
	case EL_PREP_TERM:
		ret = el_wget(el, op, va_arg(ap, int *));
		break;

	case EL_GETTC: {
		char *argv[20];
		static char gettc[] = "gettc";
		int i;
		for (i = 1; i < (int)__arraycount(argv); ++i)
			if ((argv[i] = va_arg(ap, char *)) == NULL)
				break;
		argv[0] = gettc;
		ret = terminal_gettc(el, i, argv);
		break;
	}

	case EL_GETCFN:         /* el_rfunc_t */
		ret = el_wget(el, op, va_arg(ap, el_rfunc_t *));
		break;

	case EL_CLIENTDATA:     /* void ** */
		ret = el_wget(el, op, va_arg(ap, void **));
		break;

	case EL_GETFP: {          /* int, FILE ** */
		int what = va_arg(ap, int);
		FILE **fpp = va_arg(ap, FILE **);
		ret = el_wget(el, op, what, fpp);
		break;
	}

	default:
		ret = -1;
		break;
	}

	va_end(ap);
	return ret;
}


const LineInfo *
el_line(EditLine *el)
{
	const LineInfoW *winfo = el_wline(el);
	LineInfo *info = &el->el_lgcylinfo;
	size_t offset;
	const wchar_t *p;

	info->buffer   = ct_encode_string(winfo->buffer, &el->el_lgcyconv);

	offset = 0;
	for (p = winfo->buffer; p < winfo->cursor; p++)
		offset += ct_enc_width(*p);
	info->cursor = info->buffer + offset;

	offset = 0;
	for (p = winfo->buffer; p < winfo->lastchar; p++)
		offset += ct_enc_width(*p);
	info->lastchar = info->buffer + offset;

	return info;
}


int
el_insertstr(EditLine *el, const char *str)
{
	return el_winsertstr(el, ct_decode_string(str, &el->el_lgcyconv));
}
