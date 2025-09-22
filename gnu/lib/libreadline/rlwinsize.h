/* rlwinsize.h -- an attempt to isolate some of the system-specific defines
   for `struct winsize' and TIOCGWINSZ. */

/* Copyright (C) 1997 Free Software Foundation, Inc.

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

#if !defined (_RLWINSIZE_H_)
#define _RLWINSIZE_H_

#if defined (HAVE_CONFIG_H)
#  include "config.h"
#endif

/* Try to find the definitions of `struct winsize' and TIOGCWINSZ */

#if defined (GWINSZ_IN_SYS_IOCTL) && !defined (TIOCGWINSZ)
#  include <sys/ioctl.h>
#endif /* GWINSZ_IN_SYS_IOCTL && !TIOCGWINSZ */

#if defined (STRUCT_WINSIZE_IN_TERMIOS) && !defined (STRUCT_WINSIZE_IN_SYS_IOCTL)
#  include <termios.h>
#endif /* STRUCT_WINSIZE_IN_TERMIOS && !STRUCT_WINSIZE_IN_SYS_IOCTL */

/* Not in either of the standard places, look around. */
#if !defined (STRUCT_WINSIZE_IN_TERMIOS) && !defined (STRUCT_WINSIZE_IN_SYS_IOCTL)
#  if defined (HAVE_SYS_STREAM_H)
#    include <sys/stream.h>
#  endif /* HAVE_SYS_STREAM_H */
#  if defined (HAVE_SYS_PTEM_H) /* SVR4.2, at least, has it here */
#    include <sys/ptem.h>
#    define _IO_PTEM_H          /* work around SVR4.2 1.1.4 bug */
#  endif /* HAVE_SYS_PTEM_H */
#  if defined (HAVE_SYS_PTE_H)  /* ??? */
#    include <sys/pte.h>
#  endif /* HAVE_SYS_PTE_H */
#endif /* !STRUCT_WINSIZE_IN_TERMIOS && !STRUCT_WINSIZE_IN_SYS_IOCTL */

#endif /* _RL_WINSIZE_H */

