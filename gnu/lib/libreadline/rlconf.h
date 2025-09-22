/* rlconf.h -- readline configuration definitions */

/* Copyright (C) 1994 Free Software Foundation, Inc.

   This file contains the Readline Library (the Library), a set of
   routines for providing Emacs style line input to programs that ask
   for it.

   The Library is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The Library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   The GNU General Public License is often shipped with GNU software, and
   is generally kept in a file called COPYING or LICENSE.  If you do not
   have a copy of the license, write to the Free Software Foundation,
   59 Temple Place, Suite 330, Boston, MA 02111 USA. */

#if !defined (_RLCONF_H_)
#define _RLCONF_H_

/* Define this if you want the vi-mode editing available. */
#define VI_MODE

/* Define this to get an indication of file type when listing completions. */
#define VISIBLE_STATS

/* This definition is needed by readline.c, rltty.c, and signals.c. */
/* If on, then readline handles signals in a way that doesn't screw. */
#define HANDLE_SIGNALS

/* Ugly but working hack for binding prefix meta. */
#define PREFIX_META_HACK

/* The final, last-ditch effort file name for an init file. */
#define DEFAULT_INPUTRC "~/.inputrc"

/* If defined, expand tabs to spaces. */
#define DISPLAY_TABS

/* If defined, use the terminal escape sequence to move the cursor forward
   over a character when updating the line rather than rewriting it. */
/* #define HACK_TERMCAP_MOTION */

/* The string inserted by the `insert comment' command. */
#define RL_COMMENT_BEGIN_DEFAULT "#"

/* Define this if you want code that allows readline to be used in an
   X `callback' style. */
#define READLINE_CALLBACKS

/* Define this if you want the cursor to indicate insert or overwrite mode. */
/* #define CURSOR_MODE */

#endif /* _RLCONF_H_ */
