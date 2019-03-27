/*	$NetBSD: readline.c,v 1.126 2016/02/24 17:13:22 christos Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jaromir Dolecek.
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
#if !defined(lint) && !defined(SCCSID)
__RCSID("$NetBSD: readline.c,v 1.126 2016/02/24 17:13:22 christos Exp $");
#endif /* not lint && not SCCSID */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>

#include "readline/readline.h"
#include "el.h"
#include "filecomplete.h"

void rl_prep_terminal(int);
void rl_deprep_terminal(void);

/* for rl_complete() */
#define TAB		'\r'

/* see comment at the #ifdef for sense of this */
/* #define GDB_411_HACK */

/* readline compatibility stuff - look at readline sources/documentation */
/* to see what these variables mean */
const char *rl_library_version = "EditLine wrapper";
int rl_readline_version = RL_READLINE_VERSION;
static char empty[] = { '\0' };
static char expand_chars[] = { ' ', '\t', '\n', '=', '(', '\0' };
static char break_chars[] = { ' ', '\t', '\n', '"', '\\', '\'', '`', '@', '$',
    '>', '<', '=', ';', '|', '&', '{', '(', '\0' };
char *rl_readline_name = empty;
FILE *rl_instream = NULL;
FILE *rl_outstream = NULL;
int rl_point = 0;
int rl_end = 0;
char *rl_line_buffer = NULL;
rl_vcpfunc_t *rl_linefunc = NULL;
int rl_done = 0;
VFunction *rl_event_hook = NULL;
KEYMAP_ENTRY_ARRAY emacs_standard_keymap,
    emacs_meta_keymap,
    emacs_ctlx_keymap;
/*
 * The following is not implemented; we always catch signals in the
 * libedit fashion: set handlers on entry to el_gets() and clear them
 * on the way out. This simplistic approach works for most cases; if
 * it does not work for your application, please let us know.
 */
int rl_catch_signals = 1;
int rl_catch_sigwinch = 1;

int history_base = 1;		/* probably never subject to change */
int history_length = 0;
int max_input_history = 0;
char history_expansion_char = '!';
char history_subst_char = '^';
char *history_no_expand_chars = expand_chars;
Function *history_inhibit_expansion_function = NULL;
char *history_arg_extract(int start, int end, const char *str);

int rl_inhibit_completion = 0;
int rl_attempted_completion_over = 0;
char *rl_basic_word_break_characters = break_chars;
char *rl_completer_word_break_characters = NULL;
char *rl_completer_quote_characters = NULL;
rl_compentry_func_t *rl_completion_entry_function = NULL;
char *(*rl_completion_word_break_hook)(void) = NULL;
rl_completion_func_t *rl_attempted_completion_function = NULL;
Function *rl_pre_input_hook = NULL;
Function *rl_startup1_hook = NULL;
int (*rl_getc_function)(FILE *) = NULL;
char *rl_terminal_name = NULL;
int rl_already_prompted = 0;
int rl_filename_completion_desired = 0;
int rl_ignore_completion_duplicates = 0;
int readline_echoing_p = 1;
int _rl_print_completions_horizontally = 0;
VFunction *rl_redisplay_function = NULL;
Function *rl_startup_hook = NULL;
VFunction *rl_completion_display_matches_hook = NULL;
VFunction *rl_prep_term_function = (VFunction *)rl_prep_terminal;
VFunction *rl_deprep_term_function = (VFunction *)rl_deprep_terminal;
KEYMAP_ENTRY_ARRAY emacs_meta_keymap;

/*
 * The current prompt string.
 */
char *rl_prompt = NULL;
/*
 * This is set to character indicating type of completion being done by
 * rl_complete_internal(); this is available for application completion
 * functions.
 */
int rl_completion_type = 0;

/*
 * If more than this number of items results from query for possible
 * completions, we ask user if they are sure to really display the list.
 */
int rl_completion_query_items = 100;

/*
 * List of characters which are word break characters, but should be left
 * in the parsed text when it is passed to the completion function.
 * Shell uses this to help determine what kind of completing to do.
 */
char *rl_special_prefixes = NULL;

/*
 * This is the character appended to the completed words if at the end of
 * the line. Default is ' ' (a space).
 */
int rl_completion_append_character = ' ';

/* stuff below is used internally by libedit for readline emulation */

static History *h = NULL;
static EditLine *e = NULL;
static rl_command_func_t *map[256];
static jmp_buf topbuf;

/* internal functions */
static unsigned char	 _el_rl_complete(EditLine *, int);
static unsigned char	 _el_rl_tstp(EditLine *, int);
static char		*_get_prompt(EditLine *);
static int		 _getc_function(EditLine *, wchar_t *);
static HIST_ENTRY	*_move_history(int);
static int		 _history_expand_command(const char *, size_t, size_t,
    char **);
static char		*_rl_compat_sub(const char *, const char *,
    const char *, int);
static int		 _rl_event_read_char(EditLine *, wchar_t *);
static void		 _rl_update_pos(void);


/* ARGSUSED */
static char *
_get_prompt(EditLine *el __attribute__((__unused__)))
{
	rl_already_prompted = 1;
	return rl_prompt;
}


/*
 * generic function for moving around history
 */
static HIST_ENTRY *
_move_history(int op)
{
	HistEvent ev;
	static HIST_ENTRY rl_he;

	if (history(h, &ev, op) != 0)
		return NULL;

	rl_he.line = ev.str;
	rl_he.data = NULL;

	return &rl_he;
}


/*
 * read one key from user defined input function
 */
static int
/*ARGSUSED*/
_getc_function(EditLine *el __attribute__((__unused__)), wchar_t *c)
{
	int i;

	i = (*rl_getc_function)(NULL);
	if (i == -1)
		return 0;
	*c = (wchar_t)i;
	return 1;
}

static void
_resize_fun(EditLine *el, void *a)
{
	const LineInfo *li;
	char **ap = a;

	li = el_line(el);
	/* a cheesy way to get rid of const cast. */
	*ap = memchr(li->buffer, *li->buffer, (size_t)1);
}

static const char *
_default_history_file(void)
{
	struct passwd *p;
	static char *path;
	size_t len;

	if (path)
		return path;

	if ((p = getpwuid(getuid())) == NULL)
		return NULL;

	len = strlen(p->pw_dir) + sizeof("/.history");
	if ((path = malloc(len)) == NULL)
		return NULL;

	(void)snprintf(path, len, "%s/.history", p->pw_dir);
	return path;
}

/*
 * READLINE compatibility stuff
 */

/*
 * Set the prompt
 */
int
rl_set_prompt(const char *prompt)
{
	char *p;

	if (!prompt)
		prompt = "";
	if (rl_prompt != NULL && strcmp(rl_prompt, prompt) == 0)
		return 0;
	if (rl_prompt)
		el_free(rl_prompt);
	rl_prompt = strdup(prompt);
	if (rl_prompt == NULL)
		return -1;

	while ((p = strchr(rl_prompt, RL_PROMPT_END_IGNORE)) != NULL)
		*p = RL_PROMPT_START_IGNORE;

	return 0;
}

/*
 * initialize rl compat stuff
 */
int
rl_initialize(void)
{
	HistEvent ev;
	int editmode = 1;
	struct termios t;

	if (e != NULL)
		el_end(e);
	if (h != NULL)
		history_end(h);

	if (!rl_instream)
		rl_instream = stdin;
	if (!rl_outstream)
		rl_outstream = stdout;

	/*
	 * See if we don't really want to run the editor
	 */
	if (tcgetattr(fileno(rl_instream), &t) != -1 && (t.c_lflag & ECHO) == 0)
		editmode = 0;

	e = el_init(rl_readline_name, rl_instream, rl_outstream, stderr);

	if (!editmode)
		el_set(e, EL_EDITMODE, 0);

	h = history_init();
	if (!e || !h)
		return -1;

	history(h, &ev, H_SETSIZE, INT_MAX);	/* unlimited */
	history_length = 0;
	max_input_history = INT_MAX;
	el_set(e, EL_HIST, history, h);

	/* Setup resize function */
	el_set(e, EL_RESIZE, _resize_fun, &rl_line_buffer);

	/* setup getc function if valid */
	if (rl_getc_function)
		el_set(e, EL_GETCFN, _getc_function);

	/* for proper prompt printing in readline() */
	if (rl_set_prompt("") == -1) {
		history_end(h);
		el_end(e);
		return -1;
	}
	el_set(e, EL_PROMPT, _get_prompt, RL_PROMPT_START_IGNORE);
	el_set(e, EL_SIGNAL, rl_catch_signals);

	/* set default mode to "emacs"-style and read setting afterwards */
	/* so this can be overridden */
	el_set(e, EL_EDITOR, "emacs");
	if (rl_terminal_name != NULL)
		el_set(e, EL_TERMINAL, rl_terminal_name);
	else
		el_get(e, EL_TERMINAL, &rl_terminal_name);

	/*
	 * Word completion - this has to go AFTER rebinding keys
	 * to emacs-style.
	 */
	el_set(e, EL_ADDFN, "rl_complete",
	    "ReadLine compatible completion function",
	    _el_rl_complete);
	el_set(e, EL_BIND, "^I", "rl_complete", NULL);

	/*
	 * Send TSTP when ^Z is pressed.
	 */
	el_set(e, EL_ADDFN, "rl_tstp",
	    "ReadLine compatible suspend function",
	    _el_rl_tstp);
	el_set(e, EL_BIND, "^Z", "rl_tstp", NULL);

	/*
	 * Set some readline compatible key-bindings.
	 */
	el_set(e, EL_BIND, "^R", "em-inc-search-prev", NULL);

	/*
	 * Allow the use of Home/End keys.
	 */
	el_set(e, EL_BIND, "\\e[1~", "ed-move-to-beg", NULL);
	el_set(e, EL_BIND, "\\e[4~", "ed-move-to-end", NULL);
	el_set(e, EL_BIND, "\\e[7~", "ed-move-to-beg", NULL);
	el_set(e, EL_BIND, "\\e[8~", "ed-move-to-end", NULL);
	el_set(e, EL_BIND, "\\e[H", "ed-move-to-beg", NULL);
	el_set(e, EL_BIND, "\\e[F", "ed-move-to-end", NULL);

	/*
	 * Allow the use of the Delete/Insert keys.
	 */
	el_set(e, EL_BIND, "\\e[3~", "ed-delete-next-char", NULL);
	el_set(e, EL_BIND, "\\e[2~", "ed-quoted-insert", NULL);

	/*
	 * Ctrl-left-arrow and Ctrl-right-arrow for word moving.
	 */
	el_set(e, EL_BIND, "\\e[1;5C", "em-next-word", NULL);
	el_set(e, EL_BIND, "\\e[1;5D", "ed-prev-word", NULL);
	el_set(e, EL_BIND, "\\e[5C", "em-next-word", NULL);
	el_set(e, EL_BIND, "\\e[5D", "ed-prev-word", NULL);
	el_set(e, EL_BIND, "\\e\\e[C", "em-next-word", NULL);
	el_set(e, EL_BIND, "\\e\\e[D", "ed-prev-word", NULL);

	/* read settings from configuration file */
	el_source(e, NULL);

	/*
	 * Unfortunately, some applications really do use rl_point
	 * and rl_line_buffer directly.
	 */
	_resize_fun(e, &rl_line_buffer);
	_rl_update_pos();

	if (rl_startup_hook)
		(*rl_startup_hook)(NULL, 0);

	return 0;
}


/*
 * read one line from input stream and return it, chomping
 * trailing newline (if there is any)
 */
char *
readline(const char *p)
{
	HistEvent ev;
	const char * volatile prompt = p;
	int count;
	const char *ret;
	char *buf;
	static int used_event_hook;

	if (e == NULL || h == NULL)
		rl_initialize();

	rl_done = 0;

	(void)setjmp(topbuf);

	/* update prompt accordingly to what has been passed */
	if (rl_set_prompt(prompt) == -1)
		return NULL;

	if (rl_pre_input_hook)
		(*rl_pre_input_hook)(NULL, 0);

	if (rl_event_hook && !(e->el_flags&NO_TTY)) {
		el_set(e, EL_GETCFN, _rl_event_read_char);
		used_event_hook = 1;
	}

	if (!rl_event_hook && used_event_hook) {
		el_set(e, EL_GETCFN, EL_BUILTIN_GETCFN);
		used_event_hook = 0;
	}

	rl_already_prompted = 0;

	/* get one line from input stream */
	ret = el_gets(e, &count);

	if (ret && count > 0) {
		int lastidx;

		buf = strdup(ret);
		if (buf == NULL)
			return NULL;
		lastidx = count - 1;
		if (buf[lastidx] == '\n')
			buf[lastidx] = '\0';
	} else
		buf = NULL;

	history(h, &ev, H_GETSIZE);
	history_length = ev.num;

	return buf;
}

/*
 * history functions
 */

/*
 * is normally called before application starts to use
 * history expansion functions
 */
void
using_history(void)
{
	if (h == NULL || e == NULL)
		rl_initialize();
}


/*
 * substitute ``what'' with ``with'', returning resulting string; if
 * globally == 1, substitutes all occurrences of what, otherwise only the
 * first one
 */
static char *
_rl_compat_sub(const char *str, const char *what, const char *with,
    int globally)
{
	const	char	*s;
	char	*r, *result;
	size_t	len, with_len, what_len;

	len = strlen(str);
	with_len = strlen(with);
	what_len = strlen(what);

	/* calculate length we need for result */
	s = str;
	while (*s) {
		if (*s == *what && !strncmp(s, what, what_len)) {
			len += with_len - what_len;
			if (!globally)
				break;
			s += what_len;
		} else
			s++;
	}
	r = result = el_malloc((len + 1) * sizeof(*r));
	if (result == NULL)
		return NULL;
	s = str;
	while (*s) {
		if (*s == *what && !strncmp(s, what, what_len)) {
			(void)strncpy(r, with, with_len);
			r += with_len;
			s += what_len;
			if (!globally) {
				(void)strcpy(r, s);
				return result;
			}
		} else
			*r++ = *s++;
	}
	*r = '\0';
	return result;
}

static	char	*last_search_pat;	/* last !?pat[?] search pattern */
static	char	*last_search_match;	/* last !?pat[?] that matched */

const char *
get_history_event(const char *cmd, int *cindex, int qchar)
{
	int idx, sign, sub, num, begin, ret;
	size_t len;
	char	*pat;
	const char *rptr;
	HistEvent ev;

	idx = *cindex;
	if (cmd[idx++] != history_expansion_char)
		return NULL;

	/* find out which event to take */
	if (cmd[idx] == history_expansion_char || cmd[idx] == '\0') {
		if (history(h, &ev, H_FIRST) != 0)
			return NULL;
		*cindex = cmd[idx]? (idx + 1):idx;
		return ev.str;
	}
	sign = 0;
	if (cmd[idx] == '-') {
		sign = 1;
		idx++;
	}

	if ('0' <= cmd[idx] && cmd[idx] <= '9') {
		HIST_ENTRY *rl_he;

		num = 0;
		while (cmd[idx] && '0' <= cmd[idx] && cmd[idx] <= '9') {
			num = num * 10 + cmd[idx] - '0';
			idx++;
		}
		if (sign)
			num = history_length - num + 1;

		if (!(rl_he = history_get(num)))
			return NULL;

		*cindex = idx;
		return rl_he->line;
	}
	sub = 0;
	if (cmd[idx] == '?') {
		sub = 1;
		idx++;
	}
	begin = idx;
	while (cmd[idx]) {
		if (cmd[idx] == '\n')
			break;
		if (sub && cmd[idx] == '?')
			break;
		if (!sub && (cmd[idx] == ':' || cmd[idx] == ' '
				    || cmd[idx] == '\t' || cmd[idx] == qchar))
			break;
		idx++;
	}
	len = (size_t)idx - (size_t)begin;
	if (sub && cmd[idx] == '?')
		idx++;
	if (sub && len == 0 && last_search_pat && *last_search_pat)
		pat = last_search_pat;
	else if (len == 0)
		return NULL;
	else {
		if ((pat = el_malloc((len + 1) * sizeof(*pat))) == NULL)
			return NULL;
		(void)strncpy(pat, cmd + begin, len);
		pat[len] = '\0';
	}

	if (history(h, &ev, H_CURR) != 0) {
		if (pat != last_search_pat)
			el_free(pat);
		return NULL;
	}
	num = ev.num;

	if (sub) {
		if (pat != last_search_pat) {
			if (last_search_pat)
				el_free(last_search_pat);
			last_search_pat = pat;
		}
		ret = history_search(pat, -1);
	} else
		ret = history_search_prefix(pat, -1);

	if (ret == -1) {
		/* restore to end of list on failed search */
		history(h, &ev, H_FIRST);
		(void)fprintf(rl_outstream, "%s: Event not found\n", pat);
		if (pat != last_search_pat)
			el_free(pat);
		return NULL;
	}

	if (sub && len) {
		if (last_search_match && last_search_match != pat)
			el_free(last_search_match);
		last_search_match = pat;
	}

	if (pat != last_search_pat)
		el_free(pat);

	if (history(h, &ev, H_CURR) != 0)
		return NULL;
	*cindex = idx;
	rptr = ev.str;

	/* roll back to original position */
	(void)history(h, &ev, H_SET, num);

	return rptr;
}

/*
 * the real function doing history expansion - takes as argument command
 * to do and data upon which the command should be executed
 * does expansion the way I've understood readline documentation
 *
 * returns 0 if data was not modified, 1 if it was and 2 if the string
 * should be only printed and not executed; in case of error,
 * returns -1 and *result points to NULL
 * it's the caller's responsibility to free() the string returned in *result
 */
static int
_history_expand_command(const char *command, size_t offs, size_t cmdlen,
    char **result)
{
	char *tmp, *search = NULL, *aptr;
	const char *ptr, *cmd;
	static char *from = NULL, *to = NULL;
	int start, end, idx, has_mods = 0;
	int p_on = 0, g_on = 0;

	*result = NULL;
	aptr = NULL;
	ptr = NULL;

	/* First get event specifier */
	idx = 0;

	if (strchr(":^*$", command[offs + 1])) {
		char str[4];
		/*
		* "!:" is shorthand for "!!:".
		* "!^", "!*" and "!$" are shorthand for
		* "!!:^", "!!:*" and "!!:$" respectively.
		*/
		str[0] = str[1] = '!';
		str[2] = '0';
		ptr = get_history_event(str, &idx, 0);
		idx = (command[offs + 1] == ':')? 1:0;
		has_mods = 1;
	} else {
		if (command[offs + 1] == '#') {
			/* use command so far */
			if ((aptr = el_malloc((offs + 1) * sizeof(*aptr)))
			    == NULL)
				return -1;
			(void)strncpy(aptr, command, offs);
			aptr[offs] = '\0';
			idx = 1;
		} else {
			int	qchar;

			qchar = (offs > 0 && command[offs - 1] == '"')? '"':0;
			ptr = get_history_event(command + offs, &idx, qchar);
		}
		has_mods = command[offs + (size_t)idx] == ':';
	}

	if (ptr == NULL && aptr == NULL)
		return -1;

	if (!has_mods) {
		*result = strdup(aptr ? aptr : ptr);
		if (aptr)
			el_free(aptr);
		if (*result == NULL)
			return -1;
		return 1;
	}

	cmd = command + offs + idx + 1;

	/* Now parse any word designators */

	if (*cmd == '%')	/* last word matched by ?pat? */
		tmp = strdup(last_search_match? last_search_match:"");
	else if (strchr("^*$-0123456789", *cmd)) {
		start = end = -1;
		if (*cmd == '^')
			start = end = 1, cmd++;
		else if (*cmd == '$')
			start = -1, cmd++;
		else if (*cmd == '*')
			start = 1, cmd++;
	       else if (*cmd == '-' || isdigit((unsigned char) *cmd)) {
			start = 0;
			while (*cmd && '0' <= *cmd && *cmd <= '9')
				start = start * 10 + *cmd++ - '0';

			if (*cmd == '-') {
				if (isdigit((unsigned char) cmd[1])) {
					cmd++;
					end = 0;
					while (*cmd && '0' <= *cmd && *cmd <= '9')
						end = end * 10 + *cmd++ - '0';
				} else if (cmd[1] == '$') {
					cmd += 2;
					end = -1;
				} else {
					cmd++;
					end = -2;
				}
			} else if (*cmd == '*')
				end = -1, cmd++;
			else
				end = start;
		}
		tmp = history_arg_extract(start, end, aptr? aptr:ptr);
		if (tmp == NULL) {
			(void)fprintf(rl_outstream, "%s: Bad word specifier",
			    command + offs + idx);
			if (aptr)
				el_free(aptr);
			return -1;
		}
	} else
		tmp = strdup(aptr? aptr:ptr);

	if (aptr)
		el_free(aptr);

	if (*cmd == '\0' || ((size_t)(cmd - (command + offs)) >= cmdlen)) {
		*result = tmp;
		return 1;
	}

	for (; *cmd; cmd++) {
		if (*cmd == ':')
			continue;
		else if (*cmd == 'h') {		/* remove trailing path */
			if ((aptr = strrchr(tmp, '/')) != NULL)
				*aptr = '\0';
		} else if (*cmd == 't') {	/* remove leading path */
			if ((aptr = strrchr(tmp, '/')) != NULL) {
				aptr = strdup(aptr + 1);
				el_free(tmp);
				tmp = aptr;
			}
		} else if (*cmd == 'r') {	/* remove trailing suffix */
			if ((aptr = strrchr(tmp, '.')) != NULL)
				*aptr = '\0';
		} else if (*cmd == 'e') {	/* remove all but suffix */
			if ((aptr = strrchr(tmp, '.')) != NULL) {
				aptr = strdup(aptr);
				el_free(tmp);
				tmp = aptr;
			}
		} else if (*cmd == 'p')		/* print only */
			p_on = 1;
		else if (*cmd == 'g')
			g_on = 2;
		else if (*cmd == 's' || *cmd == '&') {
			char *what, *with, delim;
			size_t len, from_len;
			size_t size;

			if (*cmd == '&' && (from == NULL || to == NULL))
				continue;
			else if (*cmd == 's') {
				delim = *(++cmd), cmd++;
				size = 16;
				what = el_realloc(from, size * sizeof(*what));
				if (what == NULL) {
					el_free(from);
					el_free(tmp);
					return 0;
				}
				len = 0;
				for (; *cmd && *cmd != delim; cmd++) {
					if (*cmd == '\\' && cmd[1] == delim)
						cmd++;
					if (len >= size) {
						char *nwhat;
						nwhat = el_realloc(what,
						    (size <<= 1) *
						    sizeof(*nwhat));
						if (nwhat == NULL) {
							el_free(what);
							el_free(tmp);
							return 0;
						}
						what = nwhat;
					}
					what[len++] = *cmd;
				}
				what[len] = '\0';
				from = what;
				if (*what == '\0') {
					el_free(what);
					if (search) {
						from = strdup(search);
						if (from == NULL) {
							el_free(tmp);
							return 0;
						}
					} else {
						from = NULL;
						el_free(tmp);
						return -1;
					}
				}
				cmd++;	/* shift after delim */
				if (!*cmd)
					continue;

				size = 16;
				with = el_realloc(to, size * sizeof(*with));
				if (with == NULL) {
					el_free(to);
					el_free(tmp);
					return -1;
				}
				len = 0;
				from_len = strlen(from);
				for (; *cmd && *cmd != delim; cmd++) {
					if (len + from_len + 1 >= size) {
						char *nwith;
						size += from_len + 1;
						nwith = el_realloc(with,
						    size * sizeof(*nwith));
						if (nwith == NULL) {
							el_free(with);
							el_free(tmp);
							return -1;
						}
						with = nwith;
					}
					if (*cmd == '&') {
						/* safe */
						(void)strcpy(&with[len], from);
						len += from_len;
						continue;
					}
					if (*cmd == '\\'
					    && (*(cmd + 1) == delim
						|| *(cmd + 1) == '&'))
						cmd++;
					with[len++] = *cmd;
				}
				with[len] = '\0';
				to = with;
			}

			aptr = _rl_compat_sub(tmp, from, to, g_on);
			if (aptr) {
				el_free(tmp);
				tmp = aptr;
			}
			g_on = 0;
		}
	}
	*result = tmp;
	return p_on? 2:1;
}


/*
 * csh-style history expansion
 */
int
history_expand(char *str, char **output)
{
	int ret = 0;
	size_t idx, i, size;
	char *tmp, *result;

	if (h == NULL || e == NULL)
		rl_initialize();

	if (history_expansion_char == 0) {
		*output = strdup(str);
		return 0;
	}

	*output = NULL;
	if (str[0] == history_subst_char) {
		/* ^foo^foo2^ is equivalent to !!:s^foo^foo2^ */
		*output = el_malloc((strlen(str) + 4 + 1) * sizeof(**output));
		if (*output == NULL)
			return 0;
		(*output)[0] = (*output)[1] = history_expansion_char;
		(*output)[2] = ':';
		(*output)[3] = 's';
		(void)strcpy((*output) + 4, str);
		str = *output;
	} else {
		*output = strdup(str);
		if (*output == NULL)
			return 0;
	}

#define ADD_STRING(what, len, fr)					\
	{								\
		if (idx + len + 1 > size) {				\
			char *nresult = el_realloc(result,		\
			    (size += len + 1) * sizeof(*nresult));	\
			if (nresult == NULL) {				\
				el_free(*output);			\
				if (/*CONSTCOND*/fr)			\
					el_free(tmp);			\
				return 0;				\
			}						\
			result = nresult;				\
		}							\
		(void)strncpy(&result[idx], what, len);			\
		idx += len;						\
		result[idx] = '\0';					\
	}

	result = NULL;
	size = idx = 0;
	tmp = NULL;
	for (i = 0; str[i];) {
		int qchar, loop_again;
		size_t len, start, j;

		qchar = 0;
		loop_again = 1;
		start = j = i;
loop:
		for (; str[j]; j++) {
			if (str[j] == '\\' &&
			    str[j + 1] == history_expansion_char) {
				len = strlen(&str[j + 1]) + 1;
				memmove(&str[j], &str[j + 1], len);
				continue;
			}
			if (!loop_again) {
				if (isspace((unsigned char) str[j])
				    || str[j] == qchar)
					break;
			}
			if (str[j] == history_expansion_char
			    && !strchr(history_no_expand_chars, str[j + 1])
			    && (!history_inhibit_expansion_function ||
			    (*history_inhibit_expansion_function)(str,
			    (int)j) == 0))
				break;
		}

		if (str[j] && loop_again) {
			i = j;
			qchar = (j > 0 && str[j - 1] == '"' )? '"':0;
			j++;
			if (str[j] == history_expansion_char)
				j++;
			loop_again = 0;
			goto loop;
		}
		len = i - start;
		ADD_STRING(&str[start], len, 0);

		if (str[i] == '\0' || str[i] != history_expansion_char) {
			len = j - i;
			ADD_STRING(&str[i], len, 0);
			if (start == 0)
				ret = 0;
			else
				ret = 1;
			break;
		}
		ret = _history_expand_command (str, i, (j - i), &tmp);
		if (ret > 0 && tmp) {
			len = strlen(tmp);
			ADD_STRING(tmp, len, 1);
		}
		if (tmp) {
			el_free(tmp);
			tmp = NULL;
		}
		i = j;
	}

	/* ret is 2 for "print only" option */
	if (ret == 2) {
		add_history(result);
#ifdef GDB_411_HACK
		/* gdb 4.11 has been shipped with readline, where */
		/* history_expand() returned -1 when the line	  */
		/* should not be executed; in readline 2.1+	  */
		/* it should return 2 in such a case		  */
		ret = -1;
#endif
	}
	el_free(*output);
	*output = result;

	return ret;
}

/*
* Return a string consisting of arguments of "str" from "start" to "end".
*/
char *
history_arg_extract(int start, int end, const char *str)
{
	size_t  i, len, max;
	char	**arr, *result = NULL;

	arr = history_tokenize(str);
	if (!arr)
		return NULL;
	if (arr && *arr == NULL)
		goto out;

	for (max = 0; arr[max]; max++)
		continue;
	max--;

	if (start == '$')
		start = (int)max;
	if (end == '$')
		end = (int)max;
	if (end < 0)
		end = (int)max + end + 1;
	if (start < 0)
		start = end;

	if (start < 0 || end < 0 || (size_t)start > max ||
	    (size_t)end > max || start > end)
		goto out;

	for (i = (size_t)start, len = 0; i <= (size_t)end; i++)
		len += strlen(arr[i]) + 1;
	len++;
	result = el_malloc(len * sizeof(*result));
	if (result == NULL)
		goto out;

	for (i = (size_t)start, len = 0; i <= (size_t)end; i++) {
		(void)strcpy(result + len, arr[i]);
		len += strlen(arr[i]);
		if (i < (size_t)end)
			result[len++] = ' ';
	}
	result[len] = '\0';

out:
	for (i = 0; arr[i]; i++)
		el_free(arr[i]);
	el_free(arr);

	return result;
}

/*
 * Parse the string into individual tokens,
 * similar to how shell would do it.
 */
char **
history_tokenize(const char *str)
{
	int size = 1, idx = 0, i, start;
	size_t len;
	char **result = NULL, *temp, delim = '\0';

	for (i = 0; str[i];) {
		while (isspace((unsigned char) str[i]))
			i++;
		start = i;
		for (; str[i];) {
			if (str[i] == '\\') {
				if (str[i+1] != '\0')
					i++;
			} else if (str[i] == delim)
				delim = '\0';
			else if (!delim &&
				    (isspace((unsigned char) str[i]) ||
				strchr("()<>;&|$", str[i])))
				break;
			else if (!delim && strchr("'`\"", str[i]))
				delim = str[i];
			if (str[i])
				i++;
		}

		if (idx + 2 >= size) {
			char **nresult;
			size <<= 1;
			nresult = el_realloc(result, (size_t)size * sizeof(*nresult));
			if (nresult == NULL) {
				el_free(result);
				return NULL;
			}
			result = nresult;
		}
		len = (size_t)i - (size_t)start;
		temp = el_malloc((size_t)(len + 1) * sizeof(*temp));
		if (temp == NULL) {
			for (i = 0; i < idx; i++)
				el_free(result[i]);
			el_free(result);
			return NULL;
		}
		(void)strncpy(temp, &str[start], len);
		temp[len] = '\0';
		result[idx++] = temp;
		result[idx] = NULL;
		if (str[i])
			i++;
	}
	return result;
}


/*
 * limit size of history record to ``max'' events
 */
void
stifle_history(int max)
{
	HistEvent ev;

	if (h == NULL || e == NULL)
		rl_initialize();

	if (history(h, &ev, H_SETSIZE, max) == 0)
		max_input_history = max;
}


/*
 * "unlimit" size of history - set the limit to maximum allowed int value
 */
int
unstifle_history(void)
{
	HistEvent ev;
	int omax;

	history(h, &ev, H_SETSIZE, INT_MAX);
	omax = max_input_history;
	max_input_history = INT_MAX;
	return omax;		/* some value _must_ be returned */
}


int
history_is_stifled(void)
{

	/* cannot return true answer */
	return max_input_history != INT_MAX;
}

static const char _history_tmp_template[] = "/tmp/.historyXXXXXX";

int
history_truncate_file (const char *filename, int nlines)
{
	int ret = 0;
	FILE *fp, *tp;
	char template[sizeof(_history_tmp_template)];
	char buf[4096];
	int fd;
	char *cp;
	off_t off;
	int count = 0;
	ssize_t left = 0;

	if (filename == NULL && (filename = _default_history_file()) == NULL)
		return errno;
	if ((fp = fopen(filename, "r+")) == NULL)
		return errno;
	strcpy(template, _history_tmp_template);
	if ((fd = mkstemp(template)) == -1) {
		ret = errno;
		goto out1;
	}

	if ((tp = fdopen(fd, "r+")) == NULL) {
		close(fd);
		ret = errno;
		goto out2;
	}

	for(;;) {
		if (fread(buf, sizeof(buf), (size_t)1, fp) != 1) {
			if (ferror(fp)) {
				ret = errno;
				break;
			}
			if (fseeko(fp, (off_t)sizeof(buf) * count, SEEK_SET) ==
			    (off_t)-1) {
				ret = errno;
				break;
			}
			left = (ssize_t)fread(buf, (size_t)1, sizeof(buf), fp);
			if (ferror(fp)) {
				ret = errno;
				break;
			}
			if (left == 0) {
				count--;
				left = sizeof(buf);
			} else if (fwrite(buf, (size_t)left, (size_t)1, tp)
			    != 1) {
				ret = errno;
				break;
			}
			fflush(tp);
			break;
		}
		if (fwrite(buf, sizeof(buf), (size_t)1, tp) != 1) {
			ret = errno;
			break;
		}
		count++;
	}
	if (ret)
		goto out3;
	cp = buf + left - 1;
	if(*cp != '\n')
		cp++;
	for(;;) {
		while (--cp >= buf) {
			if (*cp == '\n') {
				if (--nlines == 0) {
					if (++cp >= buf + sizeof(buf)) {
						count++;
						cp = buf;
					}
					break;
				}
			}
		}
		if (nlines <= 0 || count == 0)
			break;
		count--;
		if (fseeko(tp, (off_t)sizeof(buf) * count, SEEK_SET) < 0) {
			ret = errno;
			break;
		}
		if (fread(buf, sizeof(buf), (size_t)1, tp) != 1) {
			if (ferror(tp)) {
				ret = errno;
				break;
			}
			ret = EAGAIN;
			break;
		}
		cp = buf + sizeof(buf);
	}

	if (ret || nlines > 0)
		goto out3;

	if (fseeko(fp, (off_t)0, SEEK_SET) == (off_t)-1) {
		ret = errno;
		goto out3;
	}

	if (fseeko(tp, (off_t)sizeof(buf) * count + (cp - buf), SEEK_SET) ==
	    (off_t)-1) {
		ret = errno;
		goto out3;
	}

	for(;;) {
		if ((left = (ssize_t)fread(buf, (size_t)1, sizeof(buf), tp)) == 0) {
			if (ferror(fp))
				ret = errno;
			break;
		}
		if (fwrite(buf, (size_t)left, (size_t)1, fp) != 1) {
			ret = errno;
			break;
		}
	}
	fflush(fp);
	if((off = ftello(fp)) > 0)
		(void)ftruncate(fileno(fp), off);
out3:
	fclose(tp);
out2:
	unlink(template);
out1:
	fclose(fp);

	return ret;
}


/*
 * read history from a file given
 */
int
read_history(const char *filename)
{
	HistEvent ev;

	if (h == NULL || e == NULL)
		rl_initialize();
	if (filename == NULL && (filename = _default_history_file()) == NULL)
		return errno;
	return history(h, &ev, H_LOAD, filename) == -1 ?
	    (errno ? errno : EINVAL) : 0;
}


/*
 * write history to a file given
 */
int
write_history(const char *filename)
{
	HistEvent ev;

	if (h == NULL || e == NULL)
		rl_initialize();
	if (filename == NULL && (filename = _default_history_file()) == NULL)
		return errno;
	return history(h, &ev, H_SAVE, filename) == -1 ?
	    (errno ? errno : EINVAL) : 0;
}


/*
 * returns history ``num''th event
 *
 * returned pointer points to static variable
 */
HIST_ENTRY *
history_get(int num)
{
	static HIST_ENTRY she;
	HistEvent ev;
	int curr_num;

	if (h == NULL || e == NULL)
		rl_initialize();

	/* save current position */
	if (history(h, &ev, H_CURR) != 0)
		return NULL;
	curr_num = ev.num;

	/* start from the oldest */
	if (history(h, &ev, H_LAST) != 0)
		return NULL;	/* error */

	/* look forwards for event matching specified offset */
	if (history(h, &ev, H_NEXT_EVDATA, num, &she.data))
		return NULL;

	she.line = ev.str;

	/* restore pointer to where it was */
	(void)history(h, &ev, H_SET, curr_num);

	return &she;
}


/*
 * add the line to history table
 */
int
add_history(const char *line)
{
	HistEvent ev;

	if (line == NULL)
		return 0;

	if (h == NULL || e == NULL)
		rl_initialize();

	(void)history(h, &ev, H_ENTER, line);
	if (history(h, &ev, H_GETSIZE) == 0)
		history_length = ev.num;

	return !(history_length > 0); /* return 0 if all is okay */
}


/*
 * remove the specified entry from the history list and return it.
 */
HIST_ENTRY *
remove_history(int num)
{
	HIST_ENTRY *he;
	HistEvent ev;

	if (h == NULL || e == NULL)
		rl_initialize();

	if ((he = el_malloc(sizeof(*he))) == NULL)
		return NULL;

	if (history(h, &ev, H_DELDATA, num, &he->data) != 0) {
		el_free(he);
		return NULL;
	}

	he->line = ev.str;
	if (history(h, &ev, H_GETSIZE) == 0)
		history_length = ev.num;

	return he;
}


/*
 * replace the line and data of the num-th entry
 */
HIST_ENTRY *
replace_history_entry(int num, const char *line, histdata_t data)
{
	HIST_ENTRY *he;
	HistEvent ev;
	int curr_num;

	if (h == NULL || e == NULL)
		rl_initialize();

	/* save current position */
	if (history(h, &ev, H_CURR) != 0)
		return NULL;
	curr_num = ev.num;

	/* start from the oldest */
	if (history(h, &ev, H_LAST) != 0)
		return NULL;	/* error */

	if ((he = el_malloc(sizeof(*he))) == NULL)
		return NULL;

	/* look forwards for event matching specified offset */
	if (history(h, &ev, H_NEXT_EVDATA, num, &he->data))
		goto out;

	he->line = strdup(ev.str);
	if (he->line == NULL)
		goto out;

	if (history(h, &ev, H_REPLACE, line, data))
		goto out;

	/* restore pointer to where it was */
	if (history(h, &ev, H_SET, curr_num))
		goto out;

	return he;
out:
	el_free(he);
	return NULL;
}

/*
 * clear the history list - delete all entries
 */
void
clear_history(void)
{
	HistEvent ev;

	if (h == NULL || e == NULL)
		rl_initialize();

	(void)history(h, &ev, H_CLEAR);
	history_length = 0;
}


/*
 * returns offset of the current history event
 */
int
where_history(void)
{
	HistEvent ev;
	int curr_num, off;

	if (history(h, &ev, H_CURR) != 0)
		return 0;
	curr_num = ev.num;

	(void)history(h, &ev, H_FIRST);
	off = 1;
	while (ev.num != curr_num && history(h, &ev, H_NEXT) == 0)
		off++;

	return off;
}


/*
 * returns current history event or NULL if there is no such event
 */
HIST_ENTRY *
current_history(void)
{

	return _move_history(H_CURR);
}


/*
 * returns total number of bytes history events' data are using
 */
int
history_total_bytes(void)
{
	HistEvent ev;
	int curr_num;
	size_t size;

	if (history(h, &ev, H_CURR) != 0)
		return -1;
	curr_num = ev.num;

	(void)history(h, &ev, H_FIRST);
	size = 0;
	do
		size += strlen(ev.str) * sizeof(*ev.str);
	while (history(h, &ev, H_NEXT) == 0);

	/* get to the same position as before */
	history(h, &ev, H_PREV_EVENT, curr_num);

	return (int)size;
}


/*
 * sets the position in the history list to ``pos''
 */
int
history_set_pos(int pos)
{
	HistEvent ev;
	int curr_num;

	if (pos >= history_length || pos < 0)
		return -1;

	(void)history(h, &ev, H_CURR);
	curr_num = ev.num;

	/*
	 * use H_DELDATA to set to nth history (without delete) by passing
	 * (void **)-1
	 */
	if (history(h, &ev, H_DELDATA, pos, (void **)-1)) {
		(void)history(h, &ev, H_SET, curr_num);
		return -1;
	}
	return 0;
}


/*
 * returns previous event in history and shifts pointer accordingly
 */
HIST_ENTRY *
previous_history(void)
{

	return _move_history(H_PREV);
}


/*
 * returns next event in history and shifts pointer accordingly
 */
HIST_ENTRY *
next_history(void)
{

	return _move_history(H_NEXT);
}


/*
 * searches for first history event containing the str
 */
int
history_search(const char *str, int direction)
{
	HistEvent ev;
	const char *strp;
	int curr_num;

	if (history(h, &ev, H_CURR) != 0)
		return -1;
	curr_num = ev.num;

	for (;;) {
		if ((strp = strstr(ev.str, str)) != NULL)
			return (int)(strp - ev.str);
		if (history(h, &ev, direction < 0 ? H_NEXT:H_PREV) != 0)
			break;
	}
	(void)history(h, &ev, H_SET, curr_num);
	return -1;
}


/*
 * searches for first history event beginning with str
 */
int
history_search_prefix(const char *str, int direction)
{
	HistEvent ev;

	return (history(h, &ev, direction < 0 ?
	    H_PREV_STR : H_NEXT_STR, str));
}


/*
 * search for event in history containing str, starting at offset
 * abs(pos); continue backward, if pos<0, forward otherwise
 */
/* ARGSUSED */
int
history_search_pos(const char *str,
		   int direction __attribute__((__unused__)), int pos)
{
	HistEvent ev;
	int curr_num, off;

	off = (pos > 0) ? pos : -pos;
	pos = (pos > 0) ? 1 : -1;

	if (history(h, &ev, H_CURR) != 0)
		return -1;
	curr_num = ev.num;

	if (history_set_pos(off) != 0 || history(h, &ev, H_CURR) != 0)
		return -1;

	for (;;) {
		if (strstr(ev.str, str))
			return off;
		if (history(h, &ev, (pos < 0) ? H_PREV : H_NEXT) != 0)
			break;
	}

	/* set "current" pointer back to previous state */
	(void)history(h, &ev,
	    pos < 0 ? H_NEXT_EVENT : H_PREV_EVENT, curr_num);

	return -1;
}


/********************************/
/* completion functions */

char *
tilde_expand(char *name)
{
	return fn_tilde_expand(name);
}

char *
filename_completion_function(const char *name, int state)
{
	return fn_filename_completion_function(name, state);
}

/*
 * a completion generator for usernames; returns _first_ username
 * which starts with supplied text
 * text contains a partial username preceded by random character
 * (usually '~'); state resets search from start (??? should we do that anyway)
 * it's the caller's responsibility to free the returned value
 */
char *
username_completion_function(const char *text, int state)
{
#if defined(HAVE_GETPW_R_POSIX) || defined(HAVE_GETPW_R_DRAFT)
	struct passwd pwres;
	char pwbuf[1024];
#endif
	struct passwd *pass = NULL;

	if (text[0] == '\0')
		return NULL;

	if (*text == '~')
		text++;

	if (state == 0)
		setpwent();

	while (
#if defined(HAVE_GETPW_R_POSIX) || defined(HAVE_GETPW_R_DRAFT)
	    getpwent_r(&pwres, pwbuf, sizeof(pwbuf), &pass) == 0 && pass != NULL
#else
	    (pass = getpwent()) != NULL
#endif
	    && text[0] == pass->pw_name[0]
	    && strcmp(text, pass->pw_name) == 0)
		continue;

	if (pass == NULL) {
		endpwent();
		return NULL;
	}
	return strdup(pass->pw_name);
}


/*
 * el-compatible wrapper to send TSTP on ^Z
 */
/* ARGSUSED */
static unsigned char
_el_rl_tstp(EditLine *el __attribute__((__unused__)), int ch __attribute__((__unused__)))
{
	(void)kill(0, SIGTSTP);
	return CC_NORM;
}

/*
 * Display list of strings in columnar format on readline's output stream.
 * 'matches' is list of strings, 'len' is number of strings in 'matches',
 * 'max' is maximum length of string in 'matches'.
 */
void
rl_display_match_list(char **matches, int len, int max)
{

	fn_display_match_list(e, matches, (size_t)len, (size_t)max);
}

static const char *
/*ARGSUSED*/
_rl_completion_append_character_function(const char *dummy
    __attribute__((__unused__)))
{
	static char buf[2];
	buf[0] = (char)rl_completion_append_character;
	buf[1] = '\0';
	return buf;
}


/*
 * complete word at current point
 */
/* ARGSUSED */
int
rl_complete(int ignore __attribute__((__unused__)), int invoking_key)
{
#ifdef WIDECHAR
	static ct_buffer_t wbreak_conv, sprefix_conv;
#endif
	char *breakchars;

	if (h == NULL || e == NULL)
		rl_initialize();

	if (rl_inhibit_completion) {
		char arr[2];
		arr[0] = (char)invoking_key;
		arr[1] = '\0';
		el_insertstr(e, arr);
		return CC_REFRESH;
	}

	if (rl_completion_word_break_hook != NULL)
		breakchars = (*rl_completion_word_break_hook)();
	else
		breakchars = rl_basic_word_break_characters;

	_rl_update_pos();

	/* Just look at how many global variables modify this operation! */
	return fn_complete(e,
	    (rl_compentry_func_t *)rl_completion_entry_function,
	    rl_attempted_completion_function,
	    ct_decode_string(rl_basic_word_break_characters, &wbreak_conv),
	    ct_decode_string(breakchars, &sprefix_conv),
	    _rl_completion_append_character_function,
	    (size_t)rl_completion_query_items,
	    &rl_completion_type, &rl_attempted_completion_over,
	    &rl_point, &rl_end, NULL, NULL, NULL);


}


/* ARGSUSED */
static unsigned char
_el_rl_complete(EditLine *el __attribute__((__unused__)), int ch)
{
	return (unsigned char)rl_complete(0, ch);
}

/*
 * misc other functions
 */

/*
 * bind key c to readline-type function func
 */
int
rl_bind_key(int c, rl_command_func_t *func)
{
	int retval = -1;

	if (h == NULL || e == NULL)
		rl_initialize();

	if (func == rl_insert) {
		/* XXX notice there is no range checking of ``c'' */
		e->el_map.key[c] = ED_INSERT;
		retval = 0;
	}
	return retval;
}


/*
 * read one key from input - handles chars pushed back
 * to input stream also
 */
int
rl_read_key(void)
{
	char fooarr[2 * sizeof(int)];

	if (e == NULL || h == NULL)
		rl_initialize();

	return el_getc(e, fooarr);
}


/*
 * reset the terminal
 */
/* ARGSUSED */
void
rl_reset_terminal(const char *p __attribute__((__unused__)))
{

	if (h == NULL || e == NULL)
		rl_initialize();
	el_reset(e);
}


/*
 * insert character ``c'' back into input stream, ``count'' times
 */
int
rl_insert(int count, int c)
{
	char arr[2];

	if (h == NULL || e == NULL)
		rl_initialize();

	/* XXX - int -> char conversion can lose on multichars */
	arr[0] = (char)c;
	arr[1] = '\0';

	for (; count > 0; count--)
		el_push(e, arr);

	return 0;
}

int
rl_insert_text(const char *text)
{
	if (!text || *text == 0)
		return 0;

	if (h == NULL || e == NULL)
		rl_initialize();

	if (el_insertstr(e, text) < 0)
		return 0;
	return (int)strlen(text);
}

/*ARGSUSED*/
int
rl_newline(int count __attribute__((__unused__)),
    int c __attribute__((__unused__)))
{
	/*
	 * Readline-4.0 appears to ignore the args.
	 */
	return rl_insert(1, '\n');
}

/*ARGSUSED*/
static unsigned char
rl_bind_wrapper(EditLine *el __attribute__((__unused__)), unsigned char c)
{
	if (map[c] == NULL)
	    return CC_ERROR;

	_rl_update_pos();

	(*map[c])(1, c);

	/* If rl_done was set by the above call, deal with it here */
	if (rl_done)
		return CC_EOF;

	return CC_NORM;
}

int
rl_add_defun(const char *name, rl_command_func_t *fun, int c)
{
	char dest[8];
	if ((size_t)c >= sizeof(map) / sizeof(map[0]) || c < 0)
		return -1;
	map[(unsigned char)c] = fun;
	el_set(e, EL_ADDFN, name, name, rl_bind_wrapper);
	vis(dest, c, VIS_WHITE|VIS_NOSLASH, 0);
	el_set(e, EL_BIND, dest, name, NULL);
	return 0;
}

void
rl_callback_read_char(void)
{
	int count = 0, done = 0;
	const char *buf = el_gets(e, &count);
	char *wbuf;

	if (buf == NULL || count-- <= 0)
		return;
	if (count == 0 && buf[0] == e->el_tty.t_c[TS_IO][C_EOF])
		done = 1;
	if (buf[count] == '\n' || buf[count] == '\r')
		done = 2;

	if (done && rl_linefunc != NULL) {
		el_set(e, EL_UNBUFFERED, 0);
		if (done == 2) {
		    if ((wbuf = strdup(buf)) != NULL)
			wbuf[count] = '\0';
		} else
			wbuf = NULL;
		(*(void (*)(const char *))rl_linefunc)(wbuf);
		el_set(e, EL_UNBUFFERED, 1);
	}
}

void
rl_callback_handler_install(const char *prompt, rl_vcpfunc_t *linefunc)
{
	if (e == NULL) {
		rl_initialize();
	}
	(void)rl_set_prompt(prompt);
	rl_linefunc = linefunc;
	el_set(e, EL_UNBUFFERED, 1);
}

void
rl_callback_handler_remove(void)
{
	el_set(e, EL_UNBUFFERED, 0);
	rl_linefunc = NULL;
}

void
rl_redisplay(void)
{
	char a[2];
	a[0] = (char)e->el_tty.t_c[TS_IO][C_REPRINT];
	a[1] = '\0';
	el_push(e, a);
}

int
rl_get_previous_history(int count, int key)
{
	char a[2];
	a[0] = (char)key;
	a[1] = '\0';
	while (count--)
		el_push(e, a);
	return 0;
}

void
/*ARGSUSED*/
rl_prep_terminal(int meta_flag __attribute__((__unused__)))
{
	el_set(e, EL_PREP_TERM, 1);
}

void
rl_deprep_terminal(void)
{
	el_set(e, EL_PREP_TERM, 0);
}

int
rl_read_init_file(const char *s)
{
	return el_source(e, s);
}

int
rl_parse_and_bind(const char *line)
{
	const char **argv;
	int argc;
	Tokenizer *tok;

	tok = tok_init(NULL);
	tok_str(tok, line, &argc, &argv);
	argc = el_parse(e, argc, argv);
	tok_end(tok);
	return argc ? 1 : 0;
}

int
rl_variable_bind(const char *var, const char *value)
{
	/*
	 * The proper return value is undocument, but this is what the
	 * readline source seems to do.
	 */
	return el_set(e, EL_BIND, "", var, value, NULL) == -1 ? 1 : 0;
}

void
rl_stuff_char(int c)
{
	char buf[2];

	buf[0] = (char)c;
	buf[1] = '\0';
	el_insertstr(e, buf);
}

static int
_rl_event_read_char(EditLine *el, wchar_t *wc)
{
	char	ch;
	int	n;
	ssize_t num_read = 0;

	ch = '\0';
	*wc = L'\0';
	while (rl_event_hook) {

		(*rl_event_hook)();

#if defined(FIONREAD)
		if (ioctl(el->el_infd, FIONREAD, &n) < 0)
			return -1;
		if (n)
			num_read = read(el->el_infd, &ch, (size_t)1);
		else
			num_read = 0;
#elif defined(F_SETFL) && defined(O_NDELAY)
		if ((n = fcntl(el->el_infd, F_GETFL, 0)) < 0)
			return -1;
		if (fcntl(el->el_infd, F_SETFL, n|O_NDELAY) < 0)
			return -1;
		num_read = read(el->el_infd, &ch, 1);
		if (fcntl(el->el_infd, F_SETFL, n))
			return -1;
#else
		/* not non-blocking, but what you gonna do? */
		num_read = read(el->el_infd, &ch, 1);
		return -1;
#endif

		if (num_read < 0 && errno == EAGAIN)
			continue;
		if (num_read == 0)
			continue;
		break;
	}
	if (!rl_event_hook)
		el_set(el, EL_GETCFN, EL_BUILTIN_GETCFN);
	*wc = (wchar_t)ch;
	return (int)num_read;
}

static void
_rl_update_pos(void)
{
	const LineInfo *li = el_line(e);

	rl_point = (int)(li->cursor - li->buffer);
	rl_end = (int)(li->lastchar - li->buffer);
}

void
rl_get_screen_size(int *rows, int *cols)
{
	if (rows)
		el_get(e, EL_GETTC, "li", rows, (void *)0);
	if (cols)
		el_get(e, EL_GETTC, "co", cols, (void *)0);
}

void
rl_set_screen_size(int rows, int cols)
{
	char buf[64];
	(void)snprintf(buf, sizeof(buf), "%d", rows);
	el_set(e, EL_SETTC, "li", buf, NULL);
	(void)snprintf(buf, sizeof(buf), "%d", cols);
	el_set(e, EL_SETTC, "co", buf, NULL);
}

char **
rl_completion_matches(const char *str, rl_compentry_func_t *fun)
{
	size_t len, max, i, j, min;
	char **list, *match, *a, *b;

	len = 1;
	max = 10;
	if ((list = el_malloc(max * sizeof(*list))) == NULL)
		return NULL;

	while ((match = (*fun)(str, (int)(len - 1))) != NULL) {
		list[len++] = match;
		if (len == max) {
			char **nl;
			max += 10;
			if ((nl = el_realloc(list, max * sizeof(*nl))) == NULL)
				goto out;
			list = nl;
		}
	}
	if (len == 1)
		goto out;
	list[len] = NULL;
	if (len == 2) {
		if ((list[0] = strdup(list[1])) == NULL)
			goto out;
		return list;
	}
	qsort(&list[1], len - 1, sizeof(*list),
	    (int (*)(const void *, const void *)) strcmp);
	min = SIZE_MAX;
	for (i = 1, a = list[i]; i < len - 1; i++, a = b) {
		b = list[i + 1];
		for (j = 0; a[j] && a[j] == b[j]; j++)
			continue;
		if (min > j)
			min = j;
	}
	if (min == 0 && *str) {
		if ((list[0] = strdup(str)) == NULL)
			goto out;
	} else {
		if ((list[0] = el_malloc((min + 1) * sizeof(*list[0]))) == NULL)
			goto out;
		(void)memcpy(list[0], list[1], min);
		list[0][min] = '\0';
	}
	return list;

out:
	el_free(list);
	return NULL;
}

char *
rl_filename_completion_function (const char *text, int state)
{
	return fn_filename_completion_function(text, state);
}

void
rl_forced_update_display(void)
{
	el_set(e, EL_REFRESH);
}

int
_rl_abort_internal(void)
{
	el_beep(e);
	longjmp(topbuf, 1);
	/*NOTREACHED*/
}

int
_rl_qsort_string_compare(char **s1, char **s2)
{
	return strcoll(*s1, *s2);
}

HISTORY_STATE *
history_get_history_state(void)
{
	HISTORY_STATE *hs;

	if ((hs = el_malloc(sizeof(*hs))) == NULL)
		return NULL;
	hs->length = history_length;
	return hs;
}

int
/*ARGSUSED*/
rl_kill_text(int from __attribute__((__unused__)),
    int to __attribute__((__unused__)))
{
	return 0;
}

Keymap
rl_make_bare_keymap(void)
{
	return NULL;
}

Keymap
rl_get_keymap(void)
{
	return NULL;
}

void
/*ARGSUSED*/
rl_set_keymap(Keymap k __attribute__((__unused__)))
{
}

int
/*ARGSUSED*/
rl_generic_bind(int type __attribute__((__unused__)),
    const char * keyseq __attribute__((__unused__)),
    const char * data __attribute__((__unused__)),
    Keymap k __attribute__((__unused__)))
{
	return 0;
}

int
/*ARGSUSED*/
rl_bind_key_in_map(int key __attribute__((__unused__)),
    rl_command_func_t *fun __attribute__((__unused__)),
    Keymap k __attribute__((__unused__)))
{
	return 0;
}

/* unsupported, but needed by python */
void
rl_cleanup_after_signal(void)
{
}

int
rl_on_new_line(void)
{
	return 0;
}

void
rl_free_line_state(void)
{
}

int
/*ARGSUSED*/
rl_set_keyboard_input_timeout(int u __attribute__((__unused__)))
{
	return 0;
}
