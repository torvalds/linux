/*	$NetBSD: readline.h,v 1.39 2016/02/17 19:47:49 christos Exp $	*/

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
 *
 * $FreeBSD$
 */
#ifndef _READLINE_H_
#define _READLINE_H_

#include <sys/types.h>
#include <stdio.h>

/* list of readline stuff supported by editline library's readline wrapper */

/* typedefs */
typedef int	  Function(const char *, int);
typedef void	  VFunction(void);
typedef void	  rl_vcpfunc_t(char *);
typedef char	**rl_completion_func_t(const char *, int, int);
typedef char     *rl_compentry_func_t(const char *, int);
typedef int	  rl_command_func_t(int, int);

/* only supports length */
typedef struct {
	int length;
} HISTORY_STATE;

typedef void *histdata_t;

typedef struct _hist_entry {
	const char	*line;
	histdata_t	 data;
} HIST_ENTRY;

typedef struct _keymap_entry {
	char type;
#define ISFUNC	0
#define ISKMAP	1
#define ISMACR	2
	Function *function;
} KEYMAP_ENTRY;

#define KEYMAP_SIZE	256

typedef KEYMAP_ENTRY KEYMAP_ENTRY_ARRAY[KEYMAP_SIZE];
typedef KEYMAP_ENTRY *Keymap;

#define control_character_threshold	0x20
#define control_character_bit		0x40

#ifndef CTRL
#include <sys/ioctl.h>
#if !defined(__sun) && !defined(__hpux) && !defined(_AIX)
#include <sys/ttydefaults.h>
#endif
#ifndef CTRL
#define CTRL(c)		((c) & 037)
#endif
#endif
#ifndef UNCTRL
#define UNCTRL(c)	(((c) - 'a' + 'A')|control_character_bit)
#endif

#define RUBOUT		0x7f
#define ABORT_CHAR	CTRL('G')
#define RL_READLINE_VERSION	0x0402
#define RL_PROMPT_START_IGNORE	'\1'
#define RL_PROMPT_END_IGNORE	'\2'

/* global variables used by readline enabled applications */
#ifdef __cplusplus
extern "C" {
#endif
extern const char	*rl_library_version;
extern int		rl_readline_version;
extern char		*rl_readline_name;
extern FILE		*rl_instream;
extern FILE		*rl_outstream;
extern char		*rl_line_buffer;
extern int		 rl_point, rl_end;
extern int		 history_base, history_length;
extern int		 max_input_history;
extern char		*rl_basic_word_break_characters;
extern char		*rl_completer_word_break_characters;
extern char		*rl_completer_quote_characters;
extern rl_compentry_func_t *rl_completion_entry_function;
extern char		*(*rl_completion_word_break_hook)(void);
extern rl_completion_func_t *rl_attempted_completion_function;
extern int		 rl_attempted_completion_over;
extern int		rl_completion_type;
extern int		rl_completion_query_items;
extern char		*rl_special_prefixes;
extern int		rl_completion_append_character;
extern int		rl_inhibit_completion;
extern Function		*rl_pre_input_hook;
extern Function		*rl_startup_hook;
extern char		*rl_terminal_name;
extern int		rl_already_prompted;
extern char		*rl_prompt;
/*
 * The following is not implemented
 */
extern int		rl_catch_signals;
extern int		rl_catch_sigwinch;
extern KEYMAP_ENTRY_ARRAY emacs_standard_keymap,
			emacs_meta_keymap,
			emacs_ctlx_keymap;
extern int		rl_filename_completion_desired;
extern int		rl_ignore_completion_duplicates;
extern int		(*rl_getc_function)(FILE *);
extern VFunction	*rl_redisplay_function;
extern VFunction	*rl_completion_display_matches_hook;
extern VFunction	*rl_prep_term_function;
extern VFunction	*rl_deprep_term_function;
extern int		readline_echoing_p;
extern int		_rl_print_completions_horizontally;

/* supported functions */
char		*readline(const char *);
int		 rl_initialize(void);

void		 using_history(void);
int		 add_history(const char *);
void		 clear_history(void);
void		 stifle_history(int);
int		 unstifle_history(void);
int		 history_is_stifled(void);
int		 where_history(void);
HIST_ENTRY	*current_history(void);
HIST_ENTRY	*history_get(int);
HIST_ENTRY	*remove_history(int);
HIST_ENTRY	*replace_history_entry(int, const char *, histdata_t);
int		 history_total_bytes(void);
int		 history_set_pos(int);
HIST_ENTRY	*previous_history(void);
HIST_ENTRY	*next_history(void);
int		 history_search(const char *, int);
int		 history_search_prefix(const char *, int);
int		 history_search_pos(const char *, int, int);
int		 read_history(const char *);
int		 write_history(const char *);
int		 history_truncate_file (const char *, int);
int		 history_expand(char *, char **);
char	       **history_tokenize(const char *);
const char	*get_history_event(const char *, int *, int);
char		*history_arg_extract(int, int, const char *);

char		*tilde_expand(char *);
char		*filename_completion_function(const char *, int);
char		*username_completion_function(const char *, int);
int		 rl_complete(int, int);
int		 rl_read_key(void);
char	       **completion_matches(const char *, rl_compentry_func_t *);
void		 rl_display_match_list(char **, int, int);

int		 rl_insert(int, int);
int		 rl_insert_text(const char *);
void		 rl_reset_terminal(const char *);
int		 rl_bind_key(int, rl_command_func_t *);
int		 rl_newline(int, int);
void		 rl_callback_read_char(void);
void		 rl_callback_handler_install(const char *, rl_vcpfunc_t *);
void		 rl_callback_handler_remove(void);
void		 rl_redisplay(void);
int		 rl_get_previous_history(int, int);
void		 rl_prep_terminal(int);
void		 rl_deprep_terminal(void);
int		 rl_read_init_file(const char *);
int		 rl_parse_and_bind(const char *);
int		 rl_variable_bind(const char *, const char *);
void		 rl_stuff_char(int);
int		 rl_add_defun(const char *, rl_command_func_t *, int);
HISTORY_STATE	*history_get_history_state(void);
void		 rl_get_screen_size(int *, int *);
void		 rl_set_screen_size(int, int);
char		*rl_filename_completion_function (const char *, int);
int		 _rl_abort_internal(void);
int		 _rl_qsort_string_compare(char **, char **);
char	       **rl_completion_matches(const char *, rl_compentry_func_t *);
void		 rl_forced_update_display(void);
int		 rl_set_prompt(const char *);
int		 rl_on_new_line(void);

/*
 * The following are not implemented
 */
int		 rl_kill_text(int, int);
Keymap		 rl_get_keymap(void);
void		 rl_set_keymap(Keymap);
Keymap		 rl_make_bare_keymap(void);
int		 rl_generic_bind(int, const char *, const char *, Keymap);
int		 rl_bind_key_in_map(int, rl_command_func_t *, Keymap);
void		 rl_cleanup_after_signal(void);
void		 rl_free_line_state(void);
int		 rl_set_keyboard_input_timeout(int);

#ifdef __cplusplus
}
#endif

#endif /* _READLINE_H_ */
