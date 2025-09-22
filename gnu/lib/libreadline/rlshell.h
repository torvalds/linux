/* rlshell.h -- utility functions normally provided by bash. */

/* Copyright (C) 1999 Free Software Foundation, Inc.

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

#if !defined (_RL_SHELL_H_)
#define _RL_SHELL_H_

#include "rlstdc.h"

extern char *sh_single_quote PARAMS((char *));
extern void sh_set_lines_and_columns PARAMS((int, int));
extern char *sh_get_env_value PARAMS((const char *));
extern char *sh_get_home_dir PARAMS((void));
extern int sh_unset_nodelay_mode PARAMS((int));

#endif /* _RL_SHELL_H_ */
