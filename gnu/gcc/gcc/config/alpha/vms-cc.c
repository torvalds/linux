/* VMS DEC C wrapper.
   Copyright (C) 2001, 2003 Free Software Foundation, Inc.
   Contributed by Douglas B. Rupp (rupp@gnat.com).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* This program is a wrapper around the VMS DEC C compiler.
   It translates Unix style command line options into corresponding
   VMS style qualifiers and then spawns the DEC C compiler.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"

#undef PATH_SEPARATOR
#undef PATH_SEPARATOR_STR
#define PATH_SEPARATOR ','
#define PATH_SEPARATOR_STR ","

/* These can be set by command line arguments */
static int verbose = 0;
static int save_temps = 0;

static int comp_arg_max = -1;
static const char **comp_args = 0;
static int comp_arg_index = -1;
static char *objfilename = 0;

static char *system_search_dirs = (char *) "";
static char *search_dirs;

static char *default_defines = (char *) "";
static char *defines;

/* Translate a Unix syntax directory specification into VMS syntax.
   If indicators of VMS syntax found, return input string.  */
static char *to_host_dir_spec (char *);

/* Translate a Unix syntax file specification into VMS syntax.
   If indicators of VMS syntax found, return input string.  */
static char *to_host_file_spec (char *);

/* Add a translated arg to the list to be passed to DEC CC.  */
static void addarg (const char *);

/* Preprocess the number of args in P_ARGC and contained in ARGV.
   Look for special flags, etc. that must be handled first.  */
static void preprocess_args (int *, char **);

/* Process the number of args in P_ARGC and contained in ARGV. Look
   for special flags, etc. that must be handled for the VMS compiler.  */
static void process_args (int *, char **);

/* Action routine called by decc$to_vms */
static int translate_unix (char *, int);

/* Add the argument contained in STR to the list of arguments to pass to the
   compiler.  */

static void
addarg (const char *str)
{
  int i;

  if (++comp_arg_index >= comp_arg_max)
    {
      const char **new_comp_args
	= (const char **) xcalloc (comp_arg_max + 1000, sizeof (char *));

      for (i = 0; i <= comp_arg_max; i++)
	new_comp_args [i] = comp_args [i];

      if (comp_args)
	free (comp_args);

      comp_arg_max += 1000;
      comp_args = new_comp_args;
    }

  comp_args [comp_arg_index] = str;
}

static void
preprocess_args (int *p_argc, char *argv[])
{
  int i;

  for (i = 1; i < *p_argc; i++)
    {
      if (strcmp (argv[i], "-o") == 0)
	{
	  char *buff, *ptr;

	  i++;
	  ptr = to_host_file_spec (argv[i]);
	  objfilename = xstrdup (ptr);
	  buff = concat ("/obj=", ptr, NULL);
	  addarg (buff);
	}
    }
}

static void
process_args (int *p_argc, char *argv[])
{
  int i;

  for (i = 1; i < *p_argc; i++)
    {
      if (strlen (argv[i]) < 2)
	continue;

      if (strncmp (argv[i], "-I", 2) == 0)
	{
	  char *ptr;
	  int new_len, search_dirs_len;

	  ptr = to_host_dir_spec (&argv[i][2]);
	  new_len = strlen (ptr);
	  search_dirs_len = strlen (search_dirs);

	  search_dirs = xrealloc (search_dirs, search_dirs_len + new_len + 2);
	  if (search_dirs_len > 0)
	    strcat (search_dirs, PATH_SEPARATOR_STR);
	  strcat (search_dirs, ptr);
	}
      else if (strncmp (argv[i], "-D", 2) == 0)
	{
	  char *ptr;
	  int new_len, defines_len;

	  ptr = &argv[i][2];
	  new_len = strlen (ptr);
	  defines_len = strlen (defines);

	  defines = xrealloc (defines, defines_len + new_len + 4);
	  if (defines_len > 0)
	    strcat (defines, ",");

	  strcat (defines, "\"");
	  strcat (defines, ptr);
	  strcat (defines, "\"");
	}
      else if (strcmp (argv[i], "-v") == 0)
	verbose = 1;
      else if (strcmp (argv[i], "-g0") == 0)
	addarg ("/nodebug");
      else if (strcmp (argv[i], "-O0") == 0)
	addarg ("/noopt");
      else if (strncmp (argv[i], "-g", 2) == 0)
	addarg ("/debug");
      else if (strcmp (argv[i], "-E") == 0)
	addarg ("/preprocess");
      else if (strcmp (argv[i], "-save-temps") == 0)
	save_temps = 1;
    }
}

/* The main program.  Spawn the VMS DEC C compiler after fixing up the
   Unix-like flags and args to be what VMS DEC C wants.  */

typedef struct dsc {unsigned short len, mbz; char *adr; } Descr;

int
main (int argc, char **argv)
{
  int i;
  char cwdev [128], *devptr;
  int devlen;
  char *cwd = getcwd (0, 1024);

  devptr = strchr (cwd, ':');
  devlen = (devptr - cwd) + 1;
  strncpy (cwdev, cwd, devlen);
  cwdev [devlen] = '\0';

  search_dirs = xstrdup (system_search_dirs);
  defines = xstrdup (default_defines);

  addarg ("cc");
  preprocess_args (&argc , argv);
  process_args (&argc , argv);

  if (strlen (search_dirs) > 0)
    {
      addarg ("/include=(");
      addarg (search_dirs);
      addarg (")");
    }

  if (strlen (defines) > 0)
    {
      addarg ("/define=(");
      addarg (defines);
      addarg (")");
    }

  for (i = 1; i < argc; i++)
    {
      int arg_len = strlen (argv[i]);

      if (strcmp (argv[i], "-o") == 0)
	i++;
      else if (strcmp (argv[i], "-v" ) == 0
	       || strcmp (argv[i], "-E") == 0
	       || strcmp (argv[i], "-c") == 0
	       || strncmp (argv[i], "-g", 2 ) == 0
	       || strncmp (argv[i], "-O", 2 ) == 0
	       || strcmp (argv[i], "-save-temps") == 0
	       || (arg_len > 2 && strncmp (argv[i], "-I", 2) == 0)
	       || (arg_len > 2 && strncmp (argv[i], "-D", 2) == 0))
	;

      /* Unix style file specs and VMS style switches look alike, so assume
	 an arg consisting of one and only one slash, and that being first, is
	 really a switch.  */
      else if ((argv[i][0] == '/') && (strchr (&argv[i][1], '/') == 0))
	addarg (argv[i]);
      else
	{
	  /* Assume filename arg */
	  char buff [256], *ptr;

	  ptr = to_host_file_spec (argv[i]);
	  arg_len = strlen (ptr);

	  if (ptr[0] == '[')
	    sprintf (buff, "%s%s", cwdev, ptr);
	  else if (strchr (ptr, ':'))
	    sprintf (buff, "%s", ptr);
	  else
	    sprintf (buff, "%s%s", cwd, ptr);

	  ptr = xstrdup (buff);
	  addarg (ptr);
	}
    }

  addarg (NULL);

  if (verbose)
    {
      int i;

      for (i = 0; i < comp_arg_index; i++)
	printf ("%s ", comp_args [i]);

      putchar ('\n');
    }

  {
    int i;
    int len = 0;

    for (i = 0; comp_args[i]; i++)
      len = len + strlen (comp_args[i]) + 1;

    {
      char *allargs = (char *) alloca (len + 1);
      Descr cmd;
      int status;
      int status1 = 1;

      for (i = 0; i < len + 1; i++)
	allargs [i] = 0;

      for (i = 0; comp_args [i]; i++)
	{
	  strcat (allargs, comp_args [i]);
	  strcat (allargs, " ");
	}

      cmd.adr = allargs;
      cmd.len = len;
      cmd.mbz = 0;

      i = LIB$SPAWN (&cmd, 0, 0, 0, 0, 0, &status);

      if ((i & 1) != 1)
	{
	  LIB$SIGNAL (i);
	  exit (1);
	}

      if ((status & 1) == 1 && (status1 & 1) == 1)
	exit (0);

      exit (1);
    }
  }
}

static char new_host_filespec [255];
static char new_host_dirspec [255];
static char filename_buff [256];

static int
translate_unix (char *name, int type ATTRIBUTE_UNUSED)
{
  strcpy (filename_buff, name);
  return 0;
}

static char *
to_host_dir_spec (char *dirspec)
{
  int len = strlen (dirspec);

  strcpy (new_host_dirspec, dirspec);

  if (strchr (new_host_dirspec, ']') || strchr (new_host_dirspec, ':'))
    return new_host_dirspec;

  while (len > 1 && new_host_dirspec [len-1] == '/')
    {
      new_host_dirspec [len-1] = 0;
      len--;
    }

  decc$to_vms (new_host_dirspec, translate_unix, 1, 2);
  strcpy (new_host_dirspec, filename_buff);

  return new_host_dirspec;

}

static char *
to_host_file_spec (char *filespec)
{
  strcpy (new_host_filespec, "");
  if (strchr (filespec, ']') || strchr (filespec, ':'))
    strcpy (new_host_filespec, filespec);
  else
    {
      decc$to_vms (filespec, translate_unix, 1, 1);
      strcpy (new_host_filespec, filename_buff);
    }

  return new_host_filespec;
}
