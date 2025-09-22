/* funmap.c -- attach names to functions. */

/* Copyright (C) 1987, 1989, 1992 Free Software Foundation, Inc.

   This file is part of the GNU Readline Library, a library for
   reading lines of text with interactive input and history editing.

   The GNU Readline Library is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2, or
   (at your option) any later version.

   The GNU Readline Library is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   The GNU General Public License is often shipped with GNU software, and
   is generally kept in a file called COPYING or LICENSE.  If you do not
   have a copy of the license, write to the Free Software Foundation,
   59 Temple Place, Suite 330, Boston, MA 02111 USA. */
#define READLINE_LIBRARY

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#if !defined (BUFSIZ)
#include <stdio.h>
#endif /* BUFSIZ */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#include "rlconf.h"
#include "readline.h"

#include "xmalloc.h"

#ifdef __STDC__
typedef int QSFUNC (const void *, const void *);
#else
typedef int QSFUNC ();
#endif

extern int _rl_qsort_string_compare PARAMS((char **, char **));

FUNMAP **funmap;
static int funmap_size;
static int funmap_entry;

/* After initializing the function map, this is the index of the first
   program specific function. */
int funmap_program_specific_entry_start;

static FUNMAP default_funmap[] = {
  { "abort", rl_abort },
  { "accept-line", rl_newline },
  { "arrow-key-prefix", rl_arrow_keys },
  { "backward-byte", rl_backward_byte },
  { "backward-char", rl_backward_char },
  { "backward-delete-char", rl_rubout },
  { "backward-kill-line", rl_backward_kill_line },
  { "backward-kill-word", rl_backward_kill_word },
  { "backward-word", rl_backward_word },
  { "beginning-of-history", rl_beginning_of_history },
  { "beginning-of-line", rl_beg_of_line },
  { "call-last-kbd-macro", rl_call_last_kbd_macro },
  { "capitalize-word", rl_capitalize_word },
  { "character-search", rl_char_search },
  { "character-search-backward", rl_backward_char_search },
  { "clear-screen", rl_clear_screen },
  { "complete", rl_complete },
  { "copy-backward-word", rl_copy_backward_word },
  { "copy-forward-word", rl_copy_forward_word },
  { "copy-region-as-kill", rl_copy_region_to_kill },
  { "delete-char", rl_delete },
  { "delete-char-or-list", rl_delete_or_show_completions },
  { "delete-horizontal-space", rl_delete_horizontal_space },
  { "digit-argument", rl_digit_argument },
  { "do-lowercase-version", rl_do_lowercase_version },
  { "downcase-word", rl_downcase_word },
  { "dump-functions", rl_dump_functions },
  { "dump-macros", rl_dump_macros },
  { "dump-variables", rl_dump_variables },
  { "emacs-editing-mode", rl_emacs_editing_mode },
  { "end-kbd-macro", rl_end_kbd_macro },
  { "end-of-history", rl_end_of_history },
  { "end-of-line", rl_end_of_line },
  { "exchange-point-and-mark", rl_exchange_point_and_mark },
  { "forward-backward-delete-char", rl_rubout_or_delete },
  { "forward-byte", rl_forward_byte },
  { "forward-char", rl_forward_char },
  { "forward-search-history", rl_forward_search_history },
  { "forward-word", rl_forward_word },
  { "history-search-backward", rl_history_search_backward },
  { "history-search-forward", rl_history_search_forward },
  { "insert-comment", rl_insert_comment },
  { "insert-completions", rl_insert_completions },
  { "kill-whole-line", rl_kill_full_line },
  { "kill-line", rl_kill_line },
  { "kill-region", rl_kill_region },
  { "kill-word", rl_kill_word },
  { "menu-complete", rl_menu_complete },
  { "next-history", rl_get_next_history },
  { "non-incremental-forward-search-history", rl_noninc_forward_search },
  { "non-incremental-reverse-search-history", rl_noninc_reverse_search },
  { "non-incremental-forward-search-history-again", rl_noninc_forward_search_again },
  { "non-incremental-reverse-search-history-again", rl_noninc_reverse_search_again },
  { "overwrite-mode", rl_overwrite_mode },
#ifdef __CYGWIN__
  { "paste-from-clipboard", rl_paste_from_clipboard },
#endif
  { "possible-completions", rl_possible_completions },
  { "previous-history", rl_get_previous_history },
  { "quoted-insert", rl_quoted_insert },
  { "re-read-init-file", rl_re_read_init_file },
  { "redraw-current-line", rl_refresh_line},
  { "reverse-search-history", rl_reverse_search_history },
  { "revert-line", rl_revert_line },
  { "self-insert", rl_insert },
  { "set-mark", rl_set_mark },
  { "start-kbd-macro", rl_start_kbd_macro },
  { "tab-insert", rl_tab_insert },
  { "tilde-expand", rl_tilde_expand },
  { "transpose-chars", rl_transpose_chars },
  { "transpose-words", rl_transpose_words },
  { "tty-status", rl_tty_status },
  { "undo", rl_undo_command },
  { "universal-argument", rl_universal_argument },
  { "unix-line-discard", rl_unix_line_discard },
  { "unix-word-rubout", rl_unix_word_rubout },
  { "upcase-word", rl_upcase_word },
  { "yank", rl_yank },
  { "yank-last-arg", rl_yank_last_arg },
  { "yank-nth-arg", rl_yank_nth_arg },
  { "yank-pop", rl_yank_pop },

#if defined (VI_MODE)
  { "vi-append-eol", rl_vi_append_eol },
  { "vi-append-mode", rl_vi_append_mode },
  { "vi-arg-digit", rl_vi_arg_digit },
  { "vi-back-to-indent", rl_vi_back_to_indent },
  { "vi-bWord", rl_vi_bWord },
  { "vi-bword", rl_vi_bword },
  { "vi-change-case", rl_vi_change_case },
  { "vi-change-char", rl_vi_change_char },
  { "vi-change-to", rl_vi_change_to },
  { "vi-char-search", rl_vi_char_search },
  { "vi-column", rl_vi_column },
  { "vi-complete", rl_vi_complete },
  { "vi-delete", rl_vi_delete },
  { "vi-delete-to", rl_vi_delete_to },
  { "vi-eWord", rl_vi_eWord },
  { "vi-editing-mode", rl_vi_editing_mode },
  { "vi-end-word", rl_vi_end_word },
  { "vi-eof-maybe", rl_vi_eof_maybe },
  { "vi-eword", rl_vi_eword },
  { "vi-fWord", rl_vi_fWord },
  { "vi-fetch-history", rl_vi_fetch_history },
  { "vi-first-print", rl_vi_first_print },
  { "vi-fword", rl_vi_fword },
  { "vi-goto-mark", rl_vi_goto_mark },
  { "vi-insert-beg", rl_vi_insert_beg },
  { "vi-insertion-mode", rl_vi_insertion_mode },
  { "vi-match", rl_vi_match },
  { "vi-movement-mode", rl_vi_movement_mode },
  { "vi-next-word", rl_vi_next_word },
  { "vi-overstrike", rl_vi_overstrike },
  { "vi-overstrike-delete", rl_vi_overstrike_delete },
  { "vi-prev-word", rl_vi_prev_word },
  { "vi-put", rl_vi_put },
  { "vi-redo", rl_vi_redo },
  { "vi-replace", rl_vi_replace },
  { "vi-search", rl_vi_search },
  { "vi-search-again", rl_vi_search_again },
  { "vi-set-mark", rl_vi_set_mark },
  { "vi-subst", rl_vi_subst },
  { "vi-tilde-expand", rl_vi_tilde_expand },
  { "vi-yank-arg", rl_vi_yank_arg },
  { "vi-yank-to", rl_vi_yank_to },
#endif /* VI_MODE */

 {(char *)NULL, (rl_command_func_t *)NULL }
};

int
rl_add_funmap_entry (name, function)
     const char *name;
     rl_command_func_t *function;
{
  if (funmap_entry + 2 >= funmap_size)
    {
      funmap_size += 64;
      funmap = (FUNMAP **)xrealloc (funmap, funmap_size * sizeof (FUNMAP *));
    }

  funmap[funmap_entry] = (FUNMAP *)xmalloc (sizeof (FUNMAP));
  funmap[funmap_entry]->name = name;
  funmap[funmap_entry]->function = function;

  funmap[++funmap_entry] = (FUNMAP *)NULL;
  return funmap_entry;
}

static int funmap_initialized;

/* Make the funmap contain all of the default entries. */
void
rl_initialize_funmap ()
{
  register int i;

  if (funmap_initialized)
    return;

  for (i = 0; default_funmap[i].name; i++)
    rl_add_funmap_entry (default_funmap[i].name, default_funmap[i].function);

  funmap_initialized = 1;
  funmap_program_specific_entry_start = i;
}

/* Produce a NULL terminated array of known function names.  The array
   is sorted.  The array itself is allocated, but not the strings inside.
   You should free () the array when you done, but not the pointrs. */
const char **
rl_funmap_names ()
{
  const char **result;
  int result_size, result_index;

  /* Make sure that the function map has been initialized. */
  rl_initialize_funmap ();

  for (result_index = result_size = 0, result = (const char **)NULL; funmap[result_index]; result_index++)
    {
      if (result_index + 2 > result_size)
	{
	  result_size += 20;
	  result = (const char **)xrealloc (result, result_size * sizeof (char *));
	}

      result[result_index] = funmap[result_index]->name;
      result[result_index + 1] = (char *)NULL;
    }

  qsort (result, result_index, sizeof (char *), (QSFUNC *)_rl_qsort_string_compare);
  return (result);
}
