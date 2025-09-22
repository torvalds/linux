/* keymaps.h -- Manipulation of readline keymaps. */

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

#ifndef _KEYMAPS_H_
#define _KEYMAPS_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined (READLINE_LIBRARY)
#  include "rlstdc.h"
#  include "chardefs.h"
#  include "rltypedefs.h"
#else
#  include <readline/rlstdc.h>
#  include <readline/chardefs.h>
#  include <readline/rltypedefs.h>
#endif

/* A keymap contains one entry for each key in the ASCII set.
   Each entry consists of a type and a pointer.
   FUNCTION is the address of a function to run, or the
   address of a keymap to indirect through.
   TYPE says which kind of thing FUNCTION is. */
typedef struct _keymap_entry {
  char type;
  rl_command_func_t *function;
} KEYMAP_ENTRY;

/* This must be large enough to hold bindings for all of the characters
   in a desired character set (e.g, 128 for ASCII, 256 for ISO Latin-x,
   and so on) plus one for subsequence matching. */
#define KEYMAP_SIZE 257
#define ANYOTHERKEY KEYMAP_SIZE-1

/* I wanted to make the above structure contain a union of:
   union { rl_command_func_t *function; struct _keymap_entry *keymap; } value;
   but this made it impossible for me to create a static array.
   Maybe I need C lessons. */

typedef KEYMAP_ENTRY KEYMAP_ENTRY_ARRAY[KEYMAP_SIZE];
typedef KEYMAP_ENTRY *Keymap;

/* The values that TYPE can have in a keymap entry. */
#define ISFUNC 0
#define ISKMAP 1
#define ISMACR 2

extern KEYMAP_ENTRY_ARRAY emacs_standard_keymap, emacs_meta_keymap, emacs_ctlx_keymap;
extern KEYMAP_ENTRY_ARRAY vi_insertion_keymap, vi_movement_keymap;

/* Return a new, empty keymap.
   Free it with free() when you are done. */
extern Keymap rl_make_bare_keymap PARAMS((void));

/* Return a new keymap which is a copy of MAP. */
extern Keymap rl_copy_keymap PARAMS((Keymap));

/* Return a new keymap with the printing characters bound to rl_insert,
   the lowercase Meta characters bound to run their equivalents, and
   the Meta digits bound to produce numeric arguments. */
extern Keymap rl_make_keymap PARAMS((void));

/* Free the storage associated with a keymap. */
extern void rl_discard_keymap PARAMS((Keymap));

/* These functions actually appear in bind.c */

/* Return the keymap corresponding to a given name.  Names look like
   `emacs' or `emacs-meta' or `vi-insert'.  */
extern Keymap rl_get_keymap_by_name PARAMS((const char *));

/* Return the current keymap. */
extern Keymap rl_get_keymap PARAMS((void));

/* Set the current keymap to MAP. */
extern void rl_set_keymap PARAMS((Keymap));

#ifdef __cplusplus
}
#endif

#endif /* _KEYMAPS_H_ */
