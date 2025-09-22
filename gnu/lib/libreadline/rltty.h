/* rltty.h - tty driver-related definitions used by some library files. */

/* Copyright (C) 1995 Free Software Foundation, Inc.

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

#if !defined (_RLTTY_H_)
#define _RLTTY_H_

/* Posix systems use termios and the Posix signal functions. */
#if defined (TERMIOS_TTY_DRIVER)
#  include <termios.h>
#endif /* TERMIOS_TTY_DRIVER */

/* System V machines use termio. */
#if defined (TERMIO_TTY_DRIVER)
#  include <termio.h>
#  if !defined (TCOON)
#    define TCOON 1
#  endif
#endif /* TERMIO_TTY_DRIVER */

/* Other (BSD) machines use sgtty. */
#if defined (NEW_TTY_DRIVER)
#  include <sgtty.h>
#endif

#include "rlwinsize.h"

/* Define _POSIX_VDISABLE if we are not using the `new' tty driver and
   it is not already defined.  It is used both to determine if a
   special character is disabled and to disable certain special
   characters.  Posix systems should set to 0, USG systems to -1. */
#if !defined (NEW_TTY_DRIVER) && !defined (_POSIX_VDISABLE)
#  if defined (_SVR4_VDISABLE)
#    define _POSIX_VDISABLE _SVR4_VDISABLE
#  else
#    if defined (_POSIX_VERSION)
#      define _POSIX_VDISABLE 0
#    else /* !_POSIX_VERSION */
#      define _POSIX_VDISABLE -1
#    endif /* !_POSIX_VERSION */
#  endif /* !_SVR4_DISABLE */
#endif /* !NEW_TTY_DRIVER && !_POSIX_VDISABLE */

typedef struct _rl_tty_chars {
  char t_eof;
  char t_eol;
  char t_eol2;
  char t_erase;
  char t_werase;
  char t_kill;
  char t_reprint;
  char t_intr;
  char t_quit;
  char t_susp;
  char t_dsusp;
  char t_start;
  char t_stop;
  char t_lnext;
  char t_flush;
  char t_status;
} _RL_TTY_CHARS;

#endif /* _RLTTY_H_ */
