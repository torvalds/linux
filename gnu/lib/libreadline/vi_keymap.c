/* vi_keymap.c -- the keymap for vi_mode in readline (). */

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

#if !defined (BUFSIZ)
#include <stdio.h>
#endif /* !BUFSIZ */

#include "readline.h"

#if 0
extern KEYMAP_ENTRY_ARRAY vi_escape_keymap;
#endif

/* The keymap arrays for handling vi mode. */
KEYMAP_ENTRY_ARRAY vi_movement_keymap = {
  /* The regular control keys come first. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-@ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-a */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-b */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-c */
  { ISFUNC, rl_vi_eof_maybe },			/* Control-d */
  { ISFUNC, rl_emacs_editing_mode },		/* Control-e */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-f */
  { ISFUNC, rl_abort },				/* Control-g */
  { ISFUNC, rl_backward_char },			/* Control-h */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-i */
  { ISFUNC, rl_newline },			/* Control-j */
  { ISFUNC, rl_kill_line },			/* Control-k */
  { ISFUNC, rl_clear_screen },			/* Control-l */
  { ISFUNC, rl_newline },			/* Control-m */
  { ISFUNC, rl_get_next_history },		/* Control-n */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-o */
  { ISFUNC, rl_get_previous_history },		/* Control-p */
  { ISFUNC, rl_quoted_insert },			/* Control-q */
  { ISFUNC, rl_reverse_search_history },	/* Control-r */
  { ISFUNC, rl_forward_search_history },	/* Control-s */
  { ISFUNC, rl_transpose_chars },		/* Control-t */
  { ISFUNC, rl_unix_line_discard },		/* Control-u */
  { ISFUNC, rl_quoted_insert },			/* Control-v */
  { ISFUNC, rl_unix_word_rubout },		/* Control-w */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-x */
  { ISFUNC, rl_yank },				/* Control-y */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-z */

  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-[ */	/* vi_escape_keymap */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-\ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-] */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-^ */
  { ISFUNC, rl_vi_undo },			/* Control-_ */

  /* The start of printing characters. */
  { ISFUNC, rl_forward_char },			/* SPACE */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ! */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* " */
  { ISFUNC, rl_insert_comment },		/* # */
  { ISFUNC, rl_end_of_line },			/* $ */
  { ISFUNC, rl_vi_match },			/* % */
  { ISFUNC, rl_vi_tilde_expand },		/* & */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ' */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ( */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ) */
  { ISFUNC, rl_vi_complete },			/* * */
  { ISFUNC, rl_get_next_history},		/* + */
  { ISFUNC, rl_vi_char_search },		/* , */
  { ISFUNC, rl_get_previous_history },		/* - */
  { ISFUNC, rl_vi_redo },			/* . */
  { ISFUNC, rl_vi_search },			/* / */

  /* Regular digits. */
  { ISFUNC, rl_beg_of_line },			/* 0 */
  { ISFUNC, rl_vi_arg_digit },			/* 1 */
  { ISFUNC, rl_vi_arg_digit },			/* 2 */
  { ISFUNC, rl_vi_arg_digit },			/* 3 */
  { ISFUNC, rl_vi_arg_digit },			/* 4 */
  { ISFUNC, rl_vi_arg_digit },			/* 5 */
  { ISFUNC, rl_vi_arg_digit },			/* 6 */
  { ISFUNC, rl_vi_arg_digit },			/* 7 */
  { ISFUNC, rl_vi_arg_digit },			/* 8 */
  { ISFUNC, rl_vi_arg_digit },			/* 9 */

  /* A little more punctuation. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* : */
  { ISFUNC, rl_vi_char_search },		/* ; */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* < */
  { ISFUNC, rl_vi_complete },			/* = */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* > */
  { ISFUNC, rl_vi_search },			/* ? */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* @ */

  /* Uppercase alphabet. */
  { ISFUNC, rl_vi_append_eol },			/* A */
  { ISFUNC, rl_vi_prev_word},			/* B */
  { ISFUNC, rl_vi_change_to },			/* C */
  { ISFUNC, rl_vi_delete_to },			/* D */
  { ISFUNC, rl_vi_end_word },			/* E */
  { ISFUNC, rl_vi_char_search },		/* F */
  { ISFUNC, rl_vi_fetch_history },		/* G */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* H */
  { ISFUNC, rl_vi_insert_beg },			/* I */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* J */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* K */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* L */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* M */
  { ISFUNC, rl_vi_search_again },		/* N */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* O */
  { ISFUNC, rl_vi_put },			/* P */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Q */
  { ISFUNC, rl_vi_replace },			/* R */
  { ISFUNC, rl_vi_subst },			/* S */
  { ISFUNC, rl_vi_char_search },		/* T */
  { ISFUNC, rl_revert_line },			/* U */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* V */
  { ISFUNC, rl_vi_next_word },			/* W */
  { ISFUNC, rl_rubout },			/* X */
  { ISFUNC, rl_vi_yank_to },			/* Y */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Z */

  /* Some more punctuation. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* [ */
  { ISFUNC, rl_vi_complete },			/* \ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ] */
  { ISFUNC, rl_vi_first_print },		/* ^ */
  { ISFUNC, rl_vi_yank_arg },			/* _ */
  { ISFUNC, rl_vi_goto_mark },			/* ` */

  /* Lowercase alphabet. */
  { ISFUNC, rl_vi_append_mode },		/* a */
  { ISFUNC, rl_vi_prev_word },			/* b */
  { ISFUNC, rl_vi_change_to },			/* c */
  { ISFUNC, rl_vi_delete_to },			/* d */
  { ISFUNC, rl_vi_end_word },			/* e */
  { ISFUNC, rl_vi_char_search },		/* f */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* g */
  { ISFUNC, rl_backward_char },			/* h */
  { ISFUNC, rl_vi_insertion_mode },		/* i */
  { ISFUNC, rl_get_next_history },		/* j */
  { ISFUNC, rl_get_previous_history },		/* k */
  { ISFUNC, rl_forward_char },			/* l */
  { ISFUNC, rl_vi_set_mark },			/* m */
  { ISFUNC, rl_vi_search_again },		/* n */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* o */
  { ISFUNC, rl_vi_put },			/* p */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* q */
  { ISFUNC, rl_vi_change_char },		/* r */
  { ISFUNC, rl_vi_subst },			/* s */
  { ISFUNC, rl_vi_char_search },		/* t */
  { ISFUNC, rl_vi_undo },			/* u */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* v */
  { ISFUNC, rl_vi_next_word },			/* w */
  { ISFUNC, rl_vi_delete },			/* x */
  { ISFUNC, rl_vi_yank_to },			/* y */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* z */

  /* Final punctuation. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* { */
  { ISFUNC, rl_vi_column },			/* | */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* } */
  { ISFUNC, rl_vi_change_case },		/* ~ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* RUBOUT */

#if KEYMAP_SIZE > 128
  /* Undefined keys. */
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 }
#endif /* KEYMAP_SIZE > 128 */
};


KEYMAP_ENTRY_ARRAY vi_insertion_keymap = {
  /* The regular control keys come first. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-@ */
  { ISFUNC, rl_insert },			/* Control-a */
  { ISFUNC, rl_insert },			/* Control-b */
  { ISFUNC, rl_insert },			/* Control-c */
  { ISFUNC, rl_vi_eof_maybe },			/* Control-d */
  { ISFUNC, rl_insert },			/* Control-e */
  { ISFUNC, rl_insert },			/* Control-f */
  { ISFUNC, rl_insert },			/* Control-g */
  { ISFUNC, rl_rubout },			/* Control-h */
  { ISFUNC, rl_complete },			/* Control-i */
  { ISFUNC, rl_newline },			/* Control-j */
  { ISFUNC, rl_insert },			/* Control-k */
  { ISFUNC, rl_insert },			/* Control-l */
  { ISFUNC, rl_newline },			/* Control-m */
  { ISFUNC, rl_insert },			/* Control-n */
  { ISFUNC, rl_insert },			/* Control-o */
  { ISFUNC, rl_insert },			/* Control-p */
  { ISFUNC, rl_insert },			/* Control-q */
  { ISFUNC, rl_reverse_search_history },	/* Control-r */
  { ISFUNC, rl_forward_search_history },	/* Control-s */
  { ISFUNC, rl_transpose_chars },		/* Control-t */
  { ISFUNC, rl_unix_line_discard },		/* Control-u */
  { ISFUNC, rl_quoted_insert },			/* Control-v */
  { ISFUNC, rl_unix_word_rubout },		/* Control-w */
  { ISFUNC, rl_insert },			/* Control-x */
  { ISFUNC, rl_yank },				/* Control-y */
  { ISFUNC, rl_insert },			/* Control-z */

  { ISFUNC, rl_vi_movement_mode },		/* Control-[ */
  { ISFUNC, rl_insert },			/* Control-\ */
  { ISFUNC, rl_insert },			/* Control-] */
  { ISFUNC, rl_insert },			/* Control-^ */
  { ISFUNC, rl_vi_undo },			/* Control-_ */

  /* The start of printing characters. */
  { ISFUNC, rl_insert },			/* SPACE */
  { ISFUNC, rl_insert },			/* ! */
  { ISFUNC, rl_insert },			/* " */
  { ISFUNC, rl_insert },			/* # */
  { ISFUNC, rl_insert },			/* $ */
  { ISFUNC, rl_insert },			/* % */
  { ISFUNC, rl_insert },			/* & */
  { ISFUNC, rl_insert },			/* ' */
  { ISFUNC, rl_insert },			/* ( */
  { ISFUNC, rl_insert },			/* ) */
  { ISFUNC, rl_insert },			/* * */
  { ISFUNC, rl_insert },			/* + */
  { ISFUNC, rl_insert },			/* , */
  { ISFUNC, rl_insert },			/* - */
  { ISFUNC, rl_insert },			/* . */
  { ISFUNC, rl_insert },			/* / */

  /* Regular digits. */
  { ISFUNC, rl_insert },			/* 0 */
  { ISFUNC, rl_insert },			/* 1 */
  { ISFUNC, rl_insert },			/* 2 */
  { ISFUNC, rl_insert },			/* 3 */
  { ISFUNC, rl_insert },			/* 4 */
  { ISFUNC, rl_insert },			/* 5 */
  { ISFUNC, rl_insert },			/* 6 */
  { ISFUNC, rl_insert },			/* 7 */
  { ISFUNC, rl_insert },			/* 8 */
  { ISFUNC, rl_insert },			/* 9 */

  /* A little more punctuation. */
  { ISFUNC, rl_insert },			/* : */
  { ISFUNC, rl_insert },			/* ; */
  { ISFUNC, rl_insert },			/* < */
  { ISFUNC, rl_insert },			/* = */
  { ISFUNC, rl_insert },			/* > */
  { ISFUNC, rl_insert },			/* ? */
  { ISFUNC, rl_insert },			/* @ */

  /* Uppercase alphabet. */
  { ISFUNC, rl_insert },			/* A */
  { ISFUNC, rl_insert },			/* B */
  { ISFUNC, rl_insert },			/* C */
  { ISFUNC, rl_insert },			/* D */
  { ISFUNC, rl_insert },			/* E */
  { ISFUNC, rl_insert },			/* F */
  { ISFUNC, rl_insert },			/* G */
  { ISFUNC, rl_insert },			/* H */
  { ISFUNC, rl_insert },			/* I */
  { ISFUNC, rl_insert },			/* J */
  { ISFUNC, rl_insert },			/* K */
  { ISFUNC, rl_insert },			/* L */
  { ISFUNC, rl_insert },			/* M */
  { ISFUNC, rl_insert },			/* N */
  { ISFUNC, rl_insert },			/* O */
  { ISFUNC, rl_insert },			/* P */
  { ISFUNC, rl_insert },			/* Q */
  { ISFUNC, rl_insert },			/* R */
  { ISFUNC, rl_insert },			/* S */
  { ISFUNC, rl_insert },			/* T */
  { ISFUNC, rl_insert },			/* U */
  { ISFUNC, rl_insert },			/* V */
  { ISFUNC, rl_insert },			/* W */
  { ISFUNC, rl_insert },			/* X */
  { ISFUNC, rl_insert },			/* Y */
  { ISFUNC, rl_insert },			/* Z */

  /* Some more punctuation. */
  { ISFUNC, rl_insert },			/* [ */
  { ISFUNC, rl_insert },			/* \ */
  { ISFUNC, rl_insert },			/* ] */
  { ISFUNC, rl_insert },			/* ^ */
  { ISFUNC, rl_insert },			/* _ */
  { ISFUNC, rl_insert },			/* ` */

  /* Lowercase alphabet. */
  { ISFUNC, rl_insert },			/* a */
  { ISFUNC, rl_insert },			/* b */
  { ISFUNC, rl_insert },			/* c */
  { ISFUNC, rl_insert },			/* d */
  { ISFUNC, rl_insert },			/* e */
  { ISFUNC, rl_insert },			/* f */
  { ISFUNC, rl_insert },			/* g */
  { ISFUNC, rl_insert },			/* h */
  { ISFUNC, rl_insert },			/* i */
  { ISFUNC, rl_insert },			/* j */
  { ISFUNC, rl_insert },			/* k */
  { ISFUNC, rl_insert },			/* l */
  { ISFUNC, rl_insert },			/* m */
  { ISFUNC, rl_insert },			/* n */
  { ISFUNC, rl_insert },			/* o */
  { ISFUNC, rl_insert },			/* p */
  { ISFUNC, rl_insert },			/* q */
  { ISFUNC, rl_insert },			/* r */
  { ISFUNC, rl_insert },			/* s */
  { ISFUNC, rl_insert },			/* t */
  { ISFUNC, rl_insert },			/* u */
  { ISFUNC, rl_insert },			/* v */
  { ISFUNC, rl_insert },			/* w */
  { ISFUNC, rl_insert },			/* x */
  { ISFUNC, rl_insert },			/* y */
  { ISFUNC, rl_insert },			/* z */

  /* Final punctuation. */
  { ISFUNC, rl_insert },			/* { */
  { ISFUNC, rl_insert },			/* | */
  { ISFUNC, rl_insert },			/* } */
  { ISFUNC, rl_insert },			/* ~ */
  { ISFUNC, rl_rubout },			/* RUBOUT */

#if KEYMAP_SIZE > 128
  /* Pure 8-bit characters (128 - 159).
     These might be used in some
     character sets. */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */
  { ISFUNC, rl_insert },	/* ? */

  /* ISO Latin-1 characters (160 - 255) */
  { ISFUNC, rl_insert },	/* No-break space */
  { ISFUNC, rl_insert },	/* Inverted exclamation mark */
  { ISFUNC, rl_insert },	/* Cent sign */
  { ISFUNC, rl_insert },	/* Pound sign */
  { ISFUNC, rl_insert },	/* Currency sign */
  { ISFUNC, rl_insert },	/* Yen sign */
  { ISFUNC, rl_insert },	/* Broken bar */
  { ISFUNC, rl_insert },	/* Section sign */
  { ISFUNC, rl_insert },	/* Diaeresis */
  { ISFUNC, rl_insert },	/* Copyright sign */
  { ISFUNC, rl_insert },	/* Feminine ordinal indicator */
  { ISFUNC, rl_insert },	/* Left pointing double angle quotation mark */
  { ISFUNC, rl_insert },	/* Not sign */
  { ISFUNC, rl_insert },	/* Soft hyphen */
  { ISFUNC, rl_insert },	/* Registered sign */
  { ISFUNC, rl_insert },	/* Macron */
  { ISFUNC, rl_insert },	/* Degree sign */
  { ISFUNC, rl_insert },	/* Plus-minus sign */
  { ISFUNC, rl_insert },	/* Superscript two */
  { ISFUNC, rl_insert },	/* Superscript three */
  { ISFUNC, rl_insert },	/* Acute accent */
  { ISFUNC, rl_insert },	/* Micro sign */
  { ISFUNC, rl_insert },	/* Pilcrow sign */
  { ISFUNC, rl_insert },	/* Middle dot */
  { ISFUNC, rl_insert },	/* Cedilla */
  { ISFUNC, rl_insert },	/* Superscript one */
  { ISFUNC, rl_insert },	/* Masculine ordinal indicator */
  { ISFUNC, rl_insert },	/* Right pointing double angle quotation mark */
  { ISFUNC, rl_insert },	/* Vulgar fraction one quarter */
  { ISFUNC, rl_insert },	/* Vulgar fraction one half */
  { ISFUNC, rl_insert },	/* Vulgar fraction three quarters */
  { ISFUNC, rl_insert },	/* Inverted questionk mark */
  { ISFUNC, rl_insert },	/* Latin capital letter a with grave */
  { ISFUNC, rl_insert },	/* Latin capital letter a with acute */
  { ISFUNC, rl_insert },	/* Latin capital letter a with circumflex */
  { ISFUNC, rl_insert },	/* Latin capital letter a with tilde */
  { ISFUNC, rl_insert },	/* Latin capital letter a with diaeresis */
  { ISFUNC, rl_insert },	/* Latin capital letter a with ring above */
  { ISFUNC, rl_insert },	/* Latin capital letter ae */
  { ISFUNC, rl_insert },	/* Latin capital letter c with cedilla */
  { ISFUNC, rl_insert },	/* Latin capital letter e with grave */
  { ISFUNC, rl_insert },	/* Latin capital letter e with acute */
  { ISFUNC, rl_insert },	/* Latin capital letter e with circumflex */
  { ISFUNC, rl_insert },	/* Latin capital letter e with diaeresis */
  { ISFUNC, rl_insert },	/* Latin capital letter i with grave */
  { ISFUNC, rl_insert },	/* Latin capital letter i with acute */
  { ISFUNC, rl_insert },	/* Latin capital letter i with circumflex */
  { ISFUNC, rl_insert },	/* Latin capital letter i with diaeresis */
  { ISFUNC, rl_insert },	/* Latin capital letter eth (Icelandic) */
  { ISFUNC, rl_insert },	/* Latin capital letter n with tilde */
  { ISFUNC, rl_insert },	/* Latin capital letter o with grave */
  { ISFUNC, rl_insert },	/* Latin capital letter o with acute */
  { ISFUNC, rl_insert },	/* Latin capital letter o with circumflex */
  { ISFUNC, rl_insert },	/* Latin capital letter o with tilde */
  { ISFUNC, rl_insert },	/* Latin capital letter o with diaeresis */
  { ISFUNC, rl_insert },	/* Multiplication sign */
  { ISFUNC, rl_insert },	/* Latin capital letter o with stroke */
  { ISFUNC, rl_insert },	/* Latin capital letter u with grave */
  { ISFUNC, rl_insert },	/* Latin capital letter u with acute */
  { ISFUNC, rl_insert },	/* Latin capital letter u with circumflex */
  { ISFUNC, rl_insert },	/* Latin capital letter u with diaeresis */
  { ISFUNC, rl_insert },	/* Latin capital letter Y with acute */
  { ISFUNC, rl_insert },	/* Latin capital letter thorn (Icelandic) */
  { ISFUNC, rl_insert },	/* Latin small letter sharp s (German) */
  { ISFUNC, rl_insert },	/* Latin small letter a with grave */
  { ISFUNC, rl_insert },	/* Latin small letter a with acute */
  { ISFUNC, rl_insert },	/* Latin small letter a with circumflex */
  { ISFUNC, rl_insert },	/* Latin small letter a with tilde */
  { ISFUNC, rl_insert },	/* Latin small letter a with diaeresis */
  { ISFUNC, rl_insert },	/* Latin small letter a with ring above */
  { ISFUNC, rl_insert },	/* Latin small letter ae */
  { ISFUNC, rl_insert },	/* Latin small letter c with cedilla */
  { ISFUNC, rl_insert },	/* Latin small letter e with grave */
  { ISFUNC, rl_insert },	/* Latin small letter e with acute */
  { ISFUNC, rl_insert },	/* Latin small letter e with circumflex */
  { ISFUNC, rl_insert },	/* Latin small letter e with diaeresis */
  { ISFUNC, rl_insert },	/* Latin small letter i with grave */
  { ISFUNC, rl_insert },	/* Latin small letter i with acute */
  { ISFUNC, rl_insert },	/* Latin small letter i with circumflex */
  { ISFUNC, rl_insert },	/* Latin small letter i with diaeresis */
  { ISFUNC, rl_insert },	/* Latin small letter eth (Icelandic) */
  { ISFUNC, rl_insert },	/* Latin small letter n with tilde */
  { ISFUNC, rl_insert },	/* Latin small letter o with grave */
  { ISFUNC, rl_insert },	/* Latin small letter o with acute */
  { ISFUNC, rl_insert },	/* Latin small letter o with circumflex */
  { ISFUNC, rl_insert },	/* Latin small letter o with tilde */
  { ISFUNC, rl_insert },	/* Latin small letter o with diaeresis */
  { ISFUNC, rl_insert },	/* Division sign */
  { ISFUNC, rl_insert },	/* Latin small letter o with stroke */
  { ISFUNC, rl_insert },	/* Latin small letter u with grave */
  { ISFUNC, rl_insert },	/* Latin small letter u with acute */
  { ISFUNC, rl_insert },	/* Latin small letter u with circumflex */
  { ISFUNC, rl_insert },	/* Latin small letter u with diaeresis */
  { ISFUNC, rl_insert },	/* Latin small letter y with acute */
  { ISFUNC, rl_insert },	/* Latin small letter thorn (Icelandic) */
  { ISFUNC, rl_insert }		/* Latin small letter y with diaeresis */
#endif /* KEYMAP_SIZE > 128 */
};

/* Unused for the time being. */
#if 0
KEYMAP_ENTRY_ARRAY vi_escape_keymap = {
  /* The regular control keys come first. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-@ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-a */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-b */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-c */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-d */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-e */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-f */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-g */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-h */
  { ISFUNC, rl_tab_insert},			/* Control-i */
  { ISFUNC, rl_emacs_editing_mode},		/* Control-j */
  { ISFUNC, rl_kill_line },			/* Control-k */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-l */
  { ISFUNC, rl_emacs_editing_mode},		/* Control-m */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-n */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-o */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-p */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-q */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-r */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-s */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-t */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-u */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-v */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-w */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-x */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-y */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-z */

  { ISFUNC, rl_vi_movement_mode },		/* Control-[ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-\ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-] */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-^ */
  { ISFUNC, rl_vi_undo },			/* Control-_ */

  /* The start of printing characters. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* SPACE */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ! */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* " */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* # */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* $ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* % */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* & */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ' */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ( */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ) */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* * */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* + */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* , */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* - */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* . */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* / */

  /* Regular digits. */
  { ISFUNC, rl_vi_arg_digit },			/* 0 */
  { ISFUNC, rl_vi_arg_digit },			/* 1 */
  { ISFUNC, rl_vi_arg_digit },			/* 2 */
  { ISFUNC, rl_vi_arg_digit },			/* 3 */
  { ISFUNC, rl_vi_arg_digit },			/* 4 */
  { ISFUNC, rl_vi_arg_digit },			/* 5 */
  { ISFUNC, rl_vi_arg_digit },			/* 6 */
  { ISFUNC, rl_vi_arg_digit },			/* 7 */
  { ISFUNC, rl_vi_arg_digit },			/* 8 */
  { ISFUNC, rl_vi_arg_digit },			/* 9 */

  /* A little more punctuation. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* : */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ; */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* < */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* = */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* > */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ? */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* @ */

  /* Uppercase alphabet. */
  { ISFUNC, rl_do_lowercase_version },		/* A */
  { ISFUNC, rl_do_lowercase_version },		/* B */
  { ISFUNC, rl_do_lowercase_version },		/* C */
  { ISFUNC, rl_do_lowercase_version },		/* D */
  { ISFUNC, rl_do_lowercase_version },		/* E */
  { ISFUNC, rl_do_lowercase_version },		/* F */
  { ISFUNC, rl_do_lowercase_version },		/* G */
  { ISFUNC, rl_do_lowercase_version },		/* H */
  { ISFUNC, rl_do_lowercase_version },		/* I */
  { ISFUNC, rl_do_lowercase_version },		/* J */
  { ISFUNC, rl_do_lowercase_version },		/* K */
  { ISFUNC, rl_do_lowercase_version },		/* L */
  { ISFUNC, rl_do_lowercase_version },		/* M */
  { ISFUNC, rl_do_lowercase_version },		/* N */
  { ISFUNC, rl_do_lowercase_version },		/* O */
  { ISFUNC, rl_do_lowercase_version },		/* P */
  { ISFUNC, rl_do_lowercase_version },		/* Q */
  { ISFUNC, rl_do_lowercase_version },		/* R */
  { ISFUNC, rl_do_lowercase_version },		/* S */
  { ISFUNC, rl_do_lowercase_version },		/* T */
  { ISFUNC, rl_do_lowercase_version },		/* U */
  { ISFUNC, rl_do_lowercase_version },		/* V */
  { ISFUNC, rl_do_lowercase_version },		/* W */
  { ISFUNC, rl_do_lowercase_version },		/* X */
  { ISFUNC, rl_do_lowercase_version },		/* Y */
  { ISFUNC, rl_do_lowercase_version },		/* Z */

  /* Some more punctuation. */
  { ISFUNC, rl_arrow_keys },			/* [ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* \ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ] */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ^ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* _ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ` */

  /* Lowercase alphabet. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* a */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* b */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* c */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* d */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* e */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* f */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* g */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* h */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* i */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* j */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* k */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* l */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* m */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* n */
  { ISFUNC, rl_arrow_keys },			/* o */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* p */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* q */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* r */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* s */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* t */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* u */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* v */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* w */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* x */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* y */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* z */

  /* Final punctuation. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* { */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* | */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* } */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ~ */
  { ISFUNC, rl_backward_kill_word },		/* RUBOUT */

#if KEYMAP_SIZE > 128
  /* Undefined keys. */
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 },
  { ISFUNC, (rl_command_func_t *)0x0 }
#endif /* KEYMAP_SIZE > 128 */
};
#endif
