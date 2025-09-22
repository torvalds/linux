/* emacs_keymap.c -- the keymap for emacs_mode in readline (). */

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

/* An array of function pointers, one for each possible key.
   If the type byte is ISKMAP, then the pointer is the address of
   a keymap. */

KEYMAP_ENTRY_ARRAY emacs_standard_keymap = {

  /* Control keys. */
  { ISFUNC, rl_set_mark },			/* Control-@ */
  { ISFUNC, rl_beg_of_line },			/* Control-a */
  { ISFUNC, rl_backward_char },			/* Control-b */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-c */
  { ISFUNC, rl_delete },			/* Control-d */
  { ISFUNC, rl_end_of_line },			/* Control-e */
  { ISFUNC, rl_forward_char },			/* Control-f */
  { ISFUNC, rl_abort },				/* Control-g */
  { ISFUNC, rl_rubout },			/* Control-h */
  { ISFUNC, rl_complete },			/* Control-i */
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
  { ISKMAP, (rl_command_func_t *)emacs_ctlx_keymap },	/* Control-x */
  { ISFUNC, rl_yank },				/* Control-y */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-z */
  { ISKMAP, (rl_command_func_t *)emacs_meta_keymap }, /* Control-[ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-\ */
  { ISFUNC, rl_char_search },			/* Control-] */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-^ */
  { ISFUNC, rl_undo_command },			/* Control-_ */

  /* The start of printing characters. */
  { ISFUNC, rl_insert },		/* SPACE */
  { ISFUNC, rl_insert },		/* ! */
  { ISFUNC, rl_insert },		/* " */
  { ISFUNC, rl_insert },		/* # */
  { ISFUNC, rl_insert },		/* $ */
  { ISFUNC, rl_insert },		/* % */
  { ISFUNC, rl_insert },		/* & */
  { ISFUNC, rl_insert },		/* ' */
  { ISFUNC, rl_insert },		/* ( */
  { ISFUNC, rl_insert },		/* ) */
  { ISFUNC, rl_insert },		/* * */
  { ISFUNC, rl_insert },		/* + */
  { ISFUNC, rl_insert },		/* , */
  { ISFUNC, rl_insert },		/* - */
  { ISFUNC, rl_insert },		/* . */
  { ISFUNC, rl_insert },		/* / */

	  /* Regular digits. */
  { ISFUNC, rl_insert },		/* 0 */
  { ISFUNC, rl_insert },		/* 1 */
  { ISFUNC, rl_insert },		/* 2 */
  { ISFUNC, rl_insert },		/* 3 */
  { ISFUNC, rl_insert },		/* 4 */
  { ISFUNC, rl_insert },		/* 5 */
  { ISFUNC, rl_insert },		/* 6 */
  { ISFUNC, rl_insert },		/* 7 */
  { ISFUNC, rl_insert },		/* 8 */
  { ISFUNC, rl_insert },		/* 9 */

  /* A little more punctuation. */
  { ISFUNC, rl_insert },		/* : */
  { ISFUNC, rl_insert },		/* ; */
  { ISFUNC, rl_insert },		/* < */
  { ISFUNC, rl_insert },		/* = */
  { ISFUNC, rl_insert },		/* > */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* @ */

  /* Uppercase alphabet. */
  { ISFUNC, rl_insert },		/* A */
  { ISFUNC, rl_insert },		/* B */
  { ISFUNC, rl_insert },		/* C */
  { ISFUNC, rl_insert },		/* D */
  { ISFUNC, rl_insert },		/* E */
  { ISFUNC, rl_insert },		/* F */
  { ISFUNC, rl_insert },		/* G */
  { ISFUNC, rl_insert },		/* H */
  { ISFUNC, rl_insert },		/* I */
  { ISFUNC, rl_insert },		/* J */
  { ISFUNC, rl_insert },		/* K */
  { ISFUNC, rl_insert },		/* L */
  { ISFUNC, rl_insert },		/* M */
  { ISFUNC, rl_insert },		/* N */
  { ISFUNC, rl_insert },		/* O */
  { ISFUNC, rl_insert },		/* P */
  { ISFUNC, rl_insert },		/* Q */
  { ISFUNC, rl_insert },		/* R */
  { ISFUNC, rl_insert },		/* S */
  { ISFUNC, rl_insert },		/* T */
  { ISFUNC, rl_insert },		/* U */
  { ISFUNC, rl_insert },		/* V */
  { ISFUNC, rl_insert },		/* W */
  { ISFUNC, rl_insert },		/* X */
  { ISFUNC, rl_insert },		/* Y */
  { ISFUNC, rl_insert },		/* Z */

  /* Some more punctuation. */
  { ISFUNC, rl_insert },		/* [ */
  { ISFUNC, rl_insert },		/* \ */
  { ISFUNC, rl_insert },		/* ] */
  { ISFUNC, rl_insert },		/* ^ */
  { ISFUNC, rl_insert },		/* _ */
  { ISFUNC, rl_insert },		/* ` */

  /* Lowercase alphabet. */
  { ISFUNC, rl_insert },		/* a */
  { ISFUNC, rl_insert },		/* b */
  { ISFUNC, rl_insert },		/* c */
  { ISFUNC, rl_insert },		/* d */
  { ISFUNC, rl_insert },		/* e */
  { ISFUNC, rl_insert },		/* f */
  { ISFUNC, rl_insert },		/* g */
  { ISFUNC, rl_insert },		/* h */
  { ISFUNC, rl_insert },		/* i */
  { ISFUNC, rl_insert },		/* j */
  { ISFUNC, rl_insert },		/* k */
  { ISFUNC, rl_insert },		/* l */
  { ISFUNC, rl_insert },		/* m */
  { ISFUNC, rl_insert },		/* n */
  { ISFUNC, rl_insert },		/* o */
  { ISFUNC, rl_insert },		/* p */
  { ISFUNC, rl_insert },		/* q */
  { ISFUNC, rl_insert },		/* r */
  { ISFUNC, rl_insert },		/* s */
  { ISFUNC, rl_insert },		/* t */
  { ISFUNC, rl_insert },		/* u */
  { ISFUNC, rl_insert },		/* v */
  { ISFUNC, rl_insert },		/* w */
  { ISFUNC, rl_insert },		/* x */
  { ISFUNC, rl_insert },		/* y */
  { ISFUNC, rl_insert },		/* z */

  /* Final punctuation. */
  { ISFUNC, rl_insert },		/* { */
  { ISFUNC, rl_insert },		/* | */
  { ISFUNC, rl_insert },		/* } */
  { ISFUNC, rl_insert },		/* ~ */
  { ISFUNC, rl_rubout },		/* RUBOUT */

#if KEYMAP_SIZE > 128
  /* Pure 8-bit characters (128 - 159).
     These might be used in some
     character sets. */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */
  { ISFUNC, rl_insert },		/* ? */

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

KEYMAP_ENTRY_ARRAY emacs_meta_keymap = {

  /* Meta keys.  Just like above, but the high bit is set. */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-@ */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-a */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-b */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-c */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-d */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-e */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-f */
  { ISFUNC, rl_abort },			/* Meta-Control-g */
  { ISFUNC, rl_backward_kill_word },	/* Meta-Control-h */
  { ISFUNC, rl_tab_insert },		/* Meta-Control-i */
  { ISFUNC, rl_vi_editing_mode },	/* Meta-Control-j */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-k */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-l */
  { ISFUNC, rl_vi_editing_mode },	/* Meta-Control-m */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-n */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-o */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-p */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-q */
  { ISFUNC, rl_revert_line },		/* Meta-Control-r */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-s */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-t */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-u */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-v */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-w */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-x */
  { ISFUNC, rl_yank_nth_arg },		/* Meta-Control-y */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-z */

  { ISFUNC, rl_complete },		/* Meta-Control-[ */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-\ */
  { ISFUNC, rl_backward_char_search },	/* Meta-Control-] */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-^ */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-Control-_ */

  /* The start of printing characters. */
  { ISFUNC, rl_set_mark },		/* Meta-SPACE */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-! */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-" */
  { ISFUNC, rl_insert_comment },	/* Meta-# */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-$ */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-% */
  { ISFUNC, rl_tilde_expand },		/* Meta-& */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-' */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-( */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-) */
  { ISFUNC, rl_insert_completions },	/* Meta-* */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-+ */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-, */
  { ISFUNC, rl_digit_argument },	/* Meta-- */
  { ISFUNC, rl_yank_last_arg},		/* Meta-. */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-/ */

  /* Regular digits. */
  { ISFUNC, rl_digit_argument },	/* Meta-0 */
  { ISFUNC, rl_digit_argument },	/* Meta-1 */
  { ISFUNC, rl_digit_argument },	/* Meta-2 */
  { ISFUNC, rl_digit_argument },	/* Meta-3 */
  { ISFUNC, rl_digit_argument },	/* Meta-4 */
  { ISFUNC, rl_digit_argument },	/* Meta-5 */
  { ISFUNC, rl_digit_argument },	/* Meta-6 */
  { ISFUNC, rl_digit_argument },	/* Meta-7 */
  { ISFUNC, rl_digit_argument },	/* Meta-8 */
  { ISFUNC, rl_digit_argument },	/* Meta-9 */

  /* A little more punctuation. */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-: */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-; */
  { ISFUNC, rl_beginning_of_history },	/* Meta-< */
  { ISFUNC, rl_possible_completions },	/* Meta-= */
  { ISFUNC, rl_end_of_history },	/* Meta-> */
  { ISFUNC, rl_possible_completions },	/* Meta-? */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-@ */

  /* Uppercase alphabet. */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-A */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-B */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-C */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-D */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-E */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-F */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-G */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-H */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-I */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-J */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-K */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-L */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-M */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-N */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-O */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-P */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-Q */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-R */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-S */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-T */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-U */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-V */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-W */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-X */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-Y */
  { ISFUNC, rl_do_lowercase_version },	/* Meta-Z */

  /* Some more punctuation. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Meta-[ */	/* was rl_arrow_keys */
  { ISFUNC, rl_delete_horizontal_space },	/* Meta-\ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Meta-] */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Meta-^ */
  { ISFUNC, rl_yank_last_arg },			/* Meta-_ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Meta-` */

  /* Lowercase alphabet. */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-a */
  { ISFUNC, rl_backward_word },		/* Meta-b */
  { ISFUNC, rl_capitalize_word },	/* Meta-c */
  { ISFUNC, rl_kill_word },		/* Meta-d */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-e */
  { ISFUNC, rl_forward_word },		/* Meta-f */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-g */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-h */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-i */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-j */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-k */
  { ISFUNC, rl_downcase_word },		/* Meta-l */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-m */
  { ISFUNC, rl_noninc_forward_search },	/* Meta-n */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-o */	/* was rl_arrow_keys */
  { ISFUNC, rl_noninc_reverse_search },	/* Meta-p */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-q */
  { ISFUNC, rl_revert_line },		/* Meta-r */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-s */
  { ISFUNC, rl_transpose_words },	/* Meta-t */
  { ISFUNC, rl_upcase_word },		/* Meta-u */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-v */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-w */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-x */
  { ISFUNC, rl_yank_pop },		/* Meta-y */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-z */

  /* Final punctuation. */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-{ */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-| */
  { ISFUNC, (rl_command_func_t *)0x0 },	/* Meta-} */
  { ISFUNC, rl_tilde_expand },		/* Meta-~ */
  { ISFUNC, rl_backward_kill_word },	/* Meta-rubout */

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

KEYMAP_ENTRY_ARRAY emacs_ctlx_keymap = {

  /* Control keys. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-@ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-a */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-b */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-c */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-d */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-e */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-f */
  { ISFUNC, rl_abort },				/* Control-g */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-h */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-i */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-j */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-k */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-l */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-m */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-n */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-o */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-p */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-q */
  { ISFUNC, rl_re_read_init_file },		/* Control-r */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-s */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-t */
  { ISFUNC, rl_undo_command },			/* Control-u */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-v */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-w */
  { ISFUNC, rl_exchange_point_and_mark },	/* Control-x */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-y */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-z */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-[ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-\ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-] */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-^ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* Control-_ */

  /* The start of printing characters. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* SPACE */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ! */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* " */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* # */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* $ */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* % */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* & */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* ' */
  { ISFUNC, rl_start_kbd_macro },		/* ( */
  { ISFUNC, rl_end_kbd_macro  },		/* ) */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* * */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* + */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* , */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* - */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* . */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* / */

  /* Regular digits. */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* 0 */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* 1 */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* 2 */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* 3 */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* 4 */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* 5 */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* 6 */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* 7 */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* 8 */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* 9 */

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
  { ISFUNC, (rl_command_func_t *)0x0 },		/* [ */
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
  { ISFUNC, rl_call_last_kbd_macro },		/* e */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* f */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* g */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* h */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* i */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* j */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* k */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* l */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* m */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* n */
  { ISFUNC, (rl_command_func_t *)0x0 },		/* o */
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
  { ISFUNC, rl_backward_kill_line },		/* RUBOUT */

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
