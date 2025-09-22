/*
 * rl - command-line interface to read a line from the standard input
 *      (or another fd) using readline.
 *
 * usage: rl [-p prompt] [-u unit] [-d default] [-n nchars]
 */

/* Copyright (C) 1987-2002 Free Software Foundation, Inc.

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

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include "posixstat.h"

#if defined (READLINE_LIBRARY)
#  include "readline.h"
#  include "history.h"
#else
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

extern int optind;
extern char *optarg;

#if !defined (strchr) && !defined (__STDC__)
extern char *strrchr();
#endif

static char *progname;
static char *deftext;

static int
set_deftext ()
{
  if (deftext)
    {
      rl_insert_text (deftext);
      deftext = (char *)NULL;
      rl_startup_hook = (rl_hook_func_t *)NULL;
    }
  return 0;
}

static void
usage()
{
  fprintf (stderr, "%s: usage: %s [-p prompt] [-u unit] [-d default] [-n nchars]\n",
		progname, progname);
}

int
main (argc, argv)
     int argc;
     char **argv;
{
  char *temp, *prompt;
  struct stat sb;
  int opt, fd, nch;
  FILE *ifp;

  progname = strrchr(argv[0], '/');
  if (progname == 0)
    progname = argv[0];
  else
    progname++;

  /* defaults */
  prompt = "readline$ ";
  fd = nch = 0;
  deftext = (char *)0;

  while ((opt = getopt(argc, argv, "p:u:d:n:")) != EOF)
    {
      switch (opt)
	{
	case 'p':
	  prompt = optarg;
	  break;
	case 'u':
	  fd = atoi(optarg);
	  if (fd < 0)
	    {
	      fprintf (stderr, "%s: bad file descriptor `%s'\n", progname, optarg);
	      exit (2);
	    }
	  break;
	case 'd':
	  deftext = optarg;
	  break;
	case 'n':
	  nch = atoi(optarg);
	  if (nch < 0)
	    {
	      fprintf (stderr, "%s: bad value for -n: `%s'\n", progname, optarg);
	      exit (2);
	    }
	  break;
	default:
	  usage ();
	  exit (2);
	}
    }

  if (fd != 0)
    {
      if (fstat (fd, &sb) < 0)
	{
	  fprintf (stderr, "%s: %d: bad file descriptor\n", progname, fd);
	  exit (1);
	}
      ifp = fdopen (fd, "r");
      rl_instream = ifp;
    }

  if (deftext && *deftext)
    rl_startup_hook = set_deftext;

  if (nch > 0)
    rl_num_chars_to_read = nch;

  temp = readline (prompt);

  /* Test for EOF. */
  if (temp == 0)
    exit (1);

  printf ("%s\n", temp);
  exit (0);
}
