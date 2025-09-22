/*
 * rlcat - cat(1) using readline
 *
 * usage: rlcat
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

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <sys/types.h>
#include "posixstat.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#ifndef errno
extern int errno;
#endif

#if defined (READLINE_LIBRARY)
#  include "readline.h"
#  include "history.h"
#else
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

extern int optind;
extern char *optarg;

static int stdcat();

static char *progname;
static int vflag;

static void
usage()
{
  fprintf (stderr, "%s: usage: %s [-vEVN] [filename]\n", progname, progname);
}

int
main (argc, argv)
     int argc;
     char **argv;
{
  char *temp;
  int opt, Vflag, Nflag;

  progname = strrchr(argv[0], '/');
  if (progname == 0)
    progname = argv[0];
  else
    progname++;

  vflag = Vflag = Nflag = 0;
  while ((opt = getopt(argc, argv, "vEVN")) != EOF)
    {
      switch (opt)
	{
	case 'v':
	  vflag = 1;
	  break;
	case 'V':
	  Vflag = 1;
	  break;
	case 'E':
	  Vflag = 0;
	  break;
	case 'N':
	  Nflag = 1;
	  break;
	default:
	  usage ();
	  exit (2);
	}
    }

  argc -= optind;
  argv += optind;

  if (isatty(0) == 0 || argc || Nflag)
    return stdcat(argc, argv);

  rl_variable_bind ("editing-mode", Vflag ? "vi" : "emacs");
  while (temp = readline (""))
    {
      if (*temp)
        add_history (temp);
      printf ("%s\n", temp);
    }

  return (ferror (stdout));
}

static int
fcopy(fp)
     FILE *fp;
{
  int c;
  char *x;

  while ((c = getc(fp)) != EOF)
    {
      if (vflag && isascii ((unsigned char)c) && isprint((unsigned char)c) == 0)
	{
	  x = rl_untranslate_keyseq (c);
	  if (fputs (x, stdout) != 0)
	    return 1;
	}
      else if (putchar (c) == EOF)
        return 1;
    }
  return (ferror (stdout));
}

int
stdcat (argc, argv)
     int argc;
     char **argv;
{
  int  i, fd, r;
  char *s;
  FILE *fp;

  if (argc == 0)
    return (fcopy(stdin));

  for (i = 0, r = 1; i < argc; i++)
    {
      if (*argv[i] == '-' && argv[i][1] == 0)
	fp = stdin;
      else
	{
	  fp = fopen (argv[i], "r");
	  if (fp == 0)
	    {
	      fprintf (stderr, "%s: %s: cannot open: %s\n", progname, argv[i], strerror(errno));
	      continue;
	    }
        }
      r = fcopy (fp);
      if (fp != stdin)
	fclose(fp);
    }
  return r;
}
