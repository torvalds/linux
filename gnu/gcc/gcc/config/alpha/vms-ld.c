/* VMS linker wrapper.
   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003
   Free Software Foundation, Inc.
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

/* This program is a wrapper around the VMS linker.
   It translates Unix style command line options into corresponding
   VMS style qualifiers and then spawns the VMS linker.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"

typedef struct dsc {unsigned short len, mbz; char *adr; } Descr;

#undef PATH_SEPARATOR
#undef PATH_SEPARATOR_STR
#define PATH_SEPARATOR ','
#define PATH_SEPARATOR_STR ","

/* Local variable declarations.  */

/* File specification for vms-dwarf2.o.  */
static char *vmsdwarf2spec = 0;

/* File specification for vms-dwarf2eh.o.  */
static char *vmsdwarf2ehspec = 0;

/* verbose = 1 if -v passed.  */
static int verbose = 0;

/* save_temps = 1 if -save-temps passed.  */
static int save_temps = 0;

/* By default don't generate executable file if there are errors
   in the link. Override with --noinhibit-exec.  */
static int inhibit_exec = 1;

/* debug = 1 if -g passed.  */
static int debug = 0;

/* By default prefer to link with shareable image libraries.
   Override with -static.  */
static int staticp = 0;

/* By default generate an executable, not a shareable image library.
   Override with -shared.  */
static int share = 0;

/* Remember if IDENTIFICATION given on command line.  */
static int ident = 0;

/* Keep track of arg translations.  */
static int link_arg_max = -1;
static const char **link_args = 0;
static int link_arg_index = -1;

/* Keep track of filenames */
static char optfilefullname [267];
static char *sharefilename = 0;
static char *exefilename = 0;

/* System search dir list. Leave blank since link handles this
   internally.  */
static char *system_search_dirs = "";

/* Search dir list passed on command line (with -L).  */
static char *search_dirs;

/* Local function declarations.  */

/* Add STR to the list of arguments to pass to the linker. Expand the list as
   necessary to accommodate.  */
static void addarg (const char *);

/* Check to see if NAME is a regular file, i.e. not a directory */
static int is_regular_file (char *);

/* Translate a Unix syntax file specification FILESPEC into VMS syntax.
   If indicators of VMS syntax found, return input string.  */
static char *to_host_file_spec (char *);

/* Locate the library named LIB_NAME in the set of paths PATH_VAL.  */
static char *locate_lib (char *, char *);

/* Given a library name NAME, i.e. foo,  Look for libfoo.lib and then
   libfoo.a in the set of directories we are allowed to search in.  */
static const char *expand_lib (char *);

/* Preprocess the number of args P_ARGC in ARGV.
   Look for special flags, etc. that must be handled first.  */
static void preprocess_args (int *, char **);

/* Preprocess the number of args P_ARGC in ARGV.  Look for
   special flags, etc. that must be handled for the VMS linker.  */
static void process_args (int *, char **);

/* Action routine called by decc$to_vms. NAME is a file name or
   directory name. TYPE is unused.  */
static int translate_unix (char *, int);

int main (int, char **);

static void
addarg (const char *str)
{
  int i;

  if (++link_arg_index >= link_arg_max)
    {
      const char **new_link_args
	= (const char **) xcalloc (link_arg_max + 1000, sizeof (char *));

      for (i = 0; i <= link_arg_max; i++)
	new_link_args [i] = link_args [i];

      if (link_args)
	free (link_args);

      link_arg_max += 1000;
      link_args = new_link_args;
    }

  link_args [link_arg_index] = str;
}

static char *
locate_lib (char *lib_name, char *path_val)
{
  int lib_len = strlen (lib_name);
  char *eptr, *sptr;

  for (sptr = path_val; *sptr; sptr = eptr)
    {
      char *buf, *ptr;

      while (*sptr == PATH_SEPARATOR)
	sptr ++;

      eptr = strchr (sptr, PATH_SEPARATOR);
      if (eptr == 0)
	eptr = strchr (sptr, 0);

      buf = alloca ((eptr-sptr) + lib_len + 4 + 2);
      strncpy (buf, sptr, eptr-sptr);
      buf [eptr-sptr] = 0;
      strcat (buf, "/");
      strcat (buf, lib_name);
      ptr = strchr (buf, 0);

      if (debug || staticp)
	{
	  /* For debug or static links, look for shareable image libraries
	     last.  */
	  strcpy (ptr, ".a");
	  if (is_regular_file (buf))
	    return xstrdup (to_host_file_spec (buf));

	  strcpy (ptr, ".olb");
	  if (is_regular_file (buf))
	    return xstrdup (to_host_file_spec (buf));

	  strcpy (ptr, ".exe");
	  if (is_regular_file (buf))
	    return xstrdup (to_host_file_spec (buf));
	}
      else
	{
	  /* Otherwise look for shareable image libraries first.  */
	  strcpy (ptr, ".exe");
	  if (is_regular_file (buf))
	    return xstrdup (to_host_file_spec (buf));

	  strcpy (ptr, ".a");
	  if (is_regular_file (buf))
	    return xstrdup (to_host_file_spec (buf));

	  strcpy (ptr, ".olb");
	  if (is_regular_file (buf))
	    return xstrdup (to_host_file_spec (buf));
	}
    }

  return 0;
}

static const char *
expand_lib (char *name)
{
  char *lib, *lib_path;

  if (strcmp (name, "c") == 0)
    /* IEEE VAX C compatible library for non-prefixed (e.g. no DECC$)
       C RTL functions.  */
    return "sys$library:vaxcrtltx.olb";

  else if (strcmp (name, "m") == 0)
    /* No separate library for math functions */
    return "";

  else
    {
      lib = xmalloc (strlen (name) + 14);

      strcpy (lib, "lib");
      strcat (lib, name);
      lib_path = locate_lib (lib, search_dirs);

      if (lib_path)
	return lib_path;
    }

  fprintf (stderr,
	   "Couldn't locate library: lib%s.exe, lib%s.a or lib%s.olb\n",
	   name, name, name);

  exit (1);
}

static int
is_regular_file (char *name)
{
  int ret;
  struct stat statbuf;

  ret = stat (name, &statbuf);
  return !ret && S_ISREG (statbuf.st_mode);
}

static void
preprocess_args (int *p_argc, char **argv)
{
  int i;

  for (i = 1; i < *p_argc; i++)
    if (strlen (argv[i]) >= 6 && strncmp (argv[i], "-shared", 7) == 0)
      share = 1;

  for (i = 1; i < *p_argc; i++)
    if (strcmp (argv[i], "-o") == 0)
      {
	char *buff, *ptr;
	int out_len;
	int len;

	i++;
	ptr = to_host_file_spec (argv[i]);
	exefilename = xstrdup (ptr);
	out_len = strlen (ptr);
	buff = xmalloc (out_len + 18);

	if (share)
	  strcpy (buff, "/share=");
	else
	  strcpy (buff, "/exe=");

	strcat (buff, ptr);
	addarg (buff);

	if (share)
	  {
	    sharefilename = xmalloc (out_len+5);
	    if (ptr == strchr (argv[i], ']'))
	      strcpy (sharefilename, ++ptr);
	    else if (ptr == strchr (argv[i], ':'))
	      strcpy (sharefilename, ++ptr);
	    else if (ptr == strrchr (argv[i], '/'))
	      strcpy (sharefilename, ++ptr);
	    else
	      strcpy (sharefilename, argv[i]);

	    len = strlen (sharefilename);
	    if (strncasecmp (&sharefilename[len-4], ".exe", 4) == 0)
	      sharefilename[len-4] = 0;

	    for (ptr = sharefilename; *ptr; ptr++)
	      *ptr = TOUPPER (*ptr);
	  }
      }
}

static void
process_args (int *p_argc, char **argv)
{
  int i;

  for (i = 1; i < *p_argc; i++)
    {
      if (strlen (argv[i]) < 2)
	continue;

      if (strncmp (argv[i], "-L", 2) == 0)
	{
	  char *nbuff, *ptr;
	  int new_len, search_dirs_len;

	  ptr = &argv[i][2];
	  new_len = strlen (ptr);
	  search_dirs_len = strlen (search_dirs);

	  nbuff = xmalloc (new_len + 1);
	  strcpy (nbuff, ptr);

	  /* Remove trailing slashes.  */
	  while (new_len > 1 && nbuff [new_len - 1] == '/')
	    {
	      nbuff [new_len - 1] = 0;
	      new_len--;
	    }

	  search_dirs = xrealloc (search_dirs, search_dirs_len + new_len + 2);
	  if (search_dirs_len > 0)
	    strcat (search_dirs, PATH_SEPARATOR_STR);

	  strcat (search_dirs, nbuff);
	  free (nbuff);
	}

      /* -v turns on verbose option here and is passed on to gcc.  */
      else if (strcmp (argv[i], "-v") == 0)
	verbose = 1;
      else if (strcmp (argv[i], "-g0") == 0)
	addarg ("/notraceback");
      else if (strncmp (argv[i], "-g", 2) == 0)
	{
	  addarg ("/debug");
	  debug = 1;
	}
      else if (strcmp (argv[i], "-static") == 0)
	staticp = 1;
      else if (strcmp (argv[i], "-map") == 0)
	{
	  char *buff, *ptr;

	  buff = xmalloc (strlen (exefilename) + 5);
	  strcpy (buff, exefilename);
	  ptr = strchr (buff, '.');
	  if (ptr)
	    *ptr = 0;

	  strcat (buff, ".map");
	  addarg ("/map=");
	  addarg (buff);
	  addarg ("/full");
	}
      else if (strcmp (argv[i], "-save-temps") == 0)
	save_temps = 1;
      else if (strcmp (argv[i], "--noinhibit-exec") == 0)
	inhibit_exec = 0;
    }
}

/* The main program.  Spawn the VMS linker after fixing up the Unix-like flags
   and args to be what the VMS linker wants.  */

int
main (int argc, char **argv)
{
  int i;
  char cwdev [128], *devptr;
  int devlen;
  int optfd;
  FILE *optfile;
  char *cwd = getcwd (0, 1024);
  char *optfilename;

  devptr = strchr (cwd, ':');
  devlen = (devptr - cwd) + 1;
  strncpy (cwdev, cwd, devlen);
  cwdev [devlen] = '\0';

  search_dirs = xstrdup (system_search_dirs);

  addarg ("link");

  /* Pass to find args that have to be append first.  */
  preprocess_args (&argc , argv);

  /* Pass to find the rest of the args.  */
  process_args (&argc , argv);

  /* Create a temp file to hold args, otherwise we can easily exceed the VMS
     command line length limits.  */
  optfilename = alloca (strlen ("LDXXXXXX") + 1);
  strcpy (optfilename, "LDXXXXXX");
  optfd = mkstemp (optfilename);
  getcwd (optfilefullname, 256, 1); /* VMS style cwd.  */
  strcat (optfilefullname, optfilename);
  strcat (optfilefullname, ".");
  optfile = fdopen (optfd, "w");

  /* Write out the IDENTIFICATION argument first so that it can be overridden
     by an options file.  */
  for (i = 1; i < argc; i++)
    {
      int arg_len = strlen (argv[i]);

      if (arg_len > 6 && strncasecmp (argv[i], "IDENT=", 6) == 0)
	{
	  /* Comes from command line. If present will always appear before
	     IDENTIFICATION=... and will override.  */

	  if (!ident)
	    ident = 1;
	}
      else if (arg_len > 15
	       && strncasecmp (argv[i], "IDENTIFICATION=", 15) == 0)
	{
	  /* Comes from pragma Ident ().  */

	  if (!ident)
	    {
	      fprintf (optfile, "case_sensitive=yes\n");
	      fprintf (optfile, "IDENTIFICATION=\"%15.15s\"\n", &argv[i][15]);
	      fprintf (optfile, "case_sensitive=NO\n");
	      ident = 1;
	    }
	}
    }

  for (i = 1; i < argc; i++)
    {
      int arg_len = strlen (argv[i]);

      if (strcmp (argv[i], "-o") == 0)
	i++;
      else if (arg_len > 2 && strncmp (argv[i], "-l", 2) == 0)
	{
	  const char *libname = expand_lib (&argv[i][2]);
	  const char *ext;
	  int len;

	  if ((len = strlen (libname)) > 0)
	    {
	      char buff [256];

	      if (len > 4 && strcasecmp (&libname [len-4], ".exe") == 0)
		ext = "/shareable";
	      else
		ext = "/library";

	      if (libname[0] == '[')
		sprintf (buff, "%s%s", cwdev, libname);
	      else
		sprintf (buff, "%s", libname);

	      fprintf (optfile, "%s%s\n", buff, ext);
	    }
	}

      else if (strcmp (argv[i], "-v" ) == 0
	       || strncmp (argv[i], "-g", 2 ) == 0
	       || strcmp (argv[i], "-static" ) == 0
	       || strcmp (argv[i], "-map" ) == 0
	       || strcmp (argv[i], "-save-temps") == 0
	       || strcmp (argv[i], "--noinhibit-exec") == 0
	       || (arg_len > 2 && strncmp (argv[i], "-L", 2) == 0)
	       || (arg_len >= 6 && strncmp (argv[i], "-share", 6) == 0))
	;
      else if (arg_len > 1 && argv[i][0] == '@')
	{
	  FILE *atfile;
	  char *ptr, *ptr1;
	  struct stat statbuf;
	  char *buff;
	  int len;

	  if (stat (&argv[i][1], &statbuf))
	    {
	      fprintf (stderr, "Couldn't open linker response file: %s\n",
		       &argv[i][1]);
	      exit (1);
	    }

	  buff = xmalloc (statbuf.st_size + 1);
	  atfile = fopen (&argv[i][1], "r");
	  fgets (buff, statbuf.st_size + 1, atfile);
	  fclose (atfile);

	  len = strlen (buff);
	  if (buff [len - 1] == '\n')
	    {
	      buff [len - 1] = 0;
	      len--;
	    }

	  ptr = buff;

	  do
	  {
	     ptr1 = strchr (ptr, ' ');
	     if (ptr1)
	       *ptr1 = 0;
	     ptr = to_host_file_spec (ptr);
	     if (ptr[0] == '[')
	       fprintf (optfile, "%s%s\n", cwdev, ptr);
	     else
	       fprintf (optfile, "%s\n", ptr);
	     ptr = ptr1 + 1;
	  } while (ptr1);
	}

      /* Unix style file specs and VMS style switches look alike, so assume an
	 arg consisting of one and only one slash, and that being first, is
	 really a switch.  */
      else if ((argv[i][0] == '/') && (strchr (&argv[i][1], '/') == 0))
	addarg (argv[i]);
      else if (arg_len > 4
	       && strncasecmp (&argv[i][arg_len-4], ".OPT", 4) == 0)
	{
	  FILE *optfile1;
	  char buff [256];

	  optfile1 = fopen (argv[i], "r");
	  while (fgets (buff, 256, optfile1))
	    fputs (buff, optfile);

	  fclose (optfile1);
	}
      else if (arg_len > 7 && strncasecmp (argv[i], "GSMATCH", 7) == 0)
	fprintf (optfile, "%s\n", argv[i]);
      else if (arg_len > 6 && strncasecmp (argv[i], "IDENT=", 6) == 0)
	{
	  /* Comes from command line and will override pragma.  */
	  fprintf (optfile, "case_sensitive=yes\n");
	  fprintf (optfile, "IDENT=\"%15.15s\"\n", &argv[i][6]);
	  fprintf (optfile, "case_sensitive=NO\n");
	  ident = 1;
	}
      else if (arg_len > 15
	       && strncasecmp (argv[i], "IDENTIFICATION=", 15) == 0)
	;
      else
	{
	  /* Assume filename arg.  */
	  const char *addswitch = "";
	  char buff [256];
	  int buff_len;
	  int is_cld = 0;

	  argv[i] = to_host_file_spec (argv[i]);
	  arg_len = strlen (argv[i]);

	  if (arg_len > 4 && strcasecmp (&argv[i][arg_len-4], ".exe") == 0)
	    addswitch = "/shareable";

	  if (arg_len > 4 && strcasecmp (&argv[i][arg_len-4], ".cld") == 0)
	    {
	      addswitch = "/shareable";
	      is_cld = 1;
	    }

	  if (arg_len > 2 && strcasecmp (&argv[i][arg_len-2], ".a") == 0)
	    addswitch = "/lib";

	  if (arg_len > 4 && strcasecmp (&argv[i][arg_len-4], ".olb") == 0)
	    addswitch = "/lib";

	  if (argv[i][0] == '[')
	    sprintf (buff, "%s%s%s\n", cwdev, argv[i], addswitch);
	  else if (strchr (argv[i], ':'))
	    sprintf (buff, "%s%s\n", argv[i], addswitch);
	  else
	    sprintf (buff, "%s%s%s\n", cwd, argv[i], addswitch);

	  buff_len = strlen (buff);

	  if (buff_len >= 15
	      && strcasecmp (&buff[buff_len - 15], "vms-dwarf2eh.o\n") == 0)
	    vmsdwarf2ehspec = xstrdup (buff);
	  else if (buff_len >= 13
	      && strcasecmp (&buff[buff_len - 13],"vms-dwarf2.o\n") == 0)
	    vmsdwarf2spec = xstrdup (buff);
	  else if (is_cld)
	    {
	      addarg (buff);
	      addarg (",");
	    }
	  else
	    fprintf (optfile, buff);
	}
    }

#if 0
  if (share)
    fprintf (optfile, "symbol_vector=(main=procedure)\n");
#endif

  if (vmsdwarf2ehspec)
    {
      fprintf (optfile, "case_sensitive=yes\n");
      fprintf (optfile, "cluster=DWARF2eh,,,%s", vmsdwarf2ehspec);
      fprintf (optfile, "collect=DWARF2eh,eh_frame\n");
      fprintf (optfile, "case_sensitive=NO\n");
    }

  if (debug && vmsdwarf2spec)
    {
      fprintf (optfile, "case_sensitive=yes\n");
      fprintf (optfile, "cluster=DWARF2debug,,,%s", vmsdwarf2spec);
      fprintf (optfile, "collect=DWARF2debug,debug_abbrev,debug_aranges,-\n");
      fprintf (optfile, " debug_frame,debug_info,debug_line,debug_loc,-\n");
      fprintf (optfile, " debug_macinfo,debug_pubnames,debug_str,-\n");
      fprintf (optfile, " debug_zzzzzz\n");
      fprintf (optfile, "case_sensitive=NO\n");
    }

  if (debug && share)
    {
      fprintf (optfile, "case_sensitive=yes\n");
      fprintf (optfile, "symbol_vector=(-\n");
      fprintf (optfile,
	       "%s$DWARF2.DEBUG_ABBREV/$dwarf2.debug_abbrev=DATA,-\n",
	       sharefilename);
      fprintf (optfile,
	       "%s$DWARF2.DEBUG_ARANGES/$dwarf2.debug_aranges=DATA,-\n",
	       sharefilename);
      fprintf (optfile, "%s$DWARF2.DEBUG_FRAME/$dwarf2.debug_frame=DATA,-\n",
	       sharefilename);
      fprintf (optfile, "%s$DWARF2.DEBUG_INFO/$dwarf2.debug_info=DATA,-\n",
	       sharefilename);
      fprintf (optfile, "%s$DWARF2.DEBUG_LINE/$dwarf2.debug_line=DATA,-\n",
	       sharefilename);
      fprintf (optfile, "%s$DWARF2.DEBUG_LOC/$dwarf2.debug_loc=DATA,-\n",
	       sharefilename);
      fprintf (optfile,
	       "%s$DWARF2.DEBUG_MACINFO/$dwarf2.debug_macinfo=DATA,-\n",
	       sharefilename);
      fprintf (optfile,
	       "%s$DWARF2.DEBUG_PUBNAMES/$dwarf2.debug_pubnames=DATA,-\n",
	       sharefilename);
      fprintf (optfile, "%s$DWARF2.DEBUG_STR/$dwarf2.debug_str=DATA,-\n",
	       sharefilename);
      fprintf (optfile, "%s$DWARF2.DEBUG_ZZZZZZ/$dwarf2.debug_zzzzzz=DATA)\n",
	       sharefilename);
      fprintf (optfile, "case_sensitive=NO\n");
    }

  fclose (optfile);
  addarg (optfilefullname);
  addarg ("/opt");

  addarg (NULL);

  if (verbose)
    {
      int i;

      for (i = 0; i < link_arg_index; i++)
	printf ("%s ", link_args [i]);
      putchar ('\n');
    }

  {
    int i;
    int len = 0;

    for (i = 0; link_args[i]; i++)
      len = len + strlen (link_args[i]) + 1;

    {
      char *allargs = (char *) alloca (len + 1);
      Descr cmd;
      int status;
      int status1 = 1;

      for (i = 0; i < len + 1; i++)
	allargs [i] = 0;

      for (i = 0; link_args [i]; i++)
	{
	  strcat (allargs, link_args [i]);
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

      if (debug && !share)
	{
	  strcpy (allargs, "@gnu:[bin]set_exe ");
	  strcat (allargs, exefilename);
	  strcat (allargs, " /nodebug /silent");
	  len = strlen (allargs);
	  cmd.adr = allargs;
	  cmd.len = len;
	  cmd.mbz = 0;

	  if (verbose)
	    printf (allargs);

	  i = LIB$SPAWN (&cmd, 0, 0, 0, 0, 0, &status1);

	  if ((i & 1) != 1)
	    {
	      LIB$SIGNAL (i);
	      exit (1);
	    }
	}

      if (!save_temps)
	remove (optfilefullname);

      if ((status & 1) == 1 && (status1 & 1) == 1)
	exit (0);

      if (exefilename && inhibit_exec == 1)
	remove (exefilename);

      exit (1);
    }
  }
}

static char new_host_filespec [255];
static char filename_buff [256];

static int
translate_unix (char *name, int type ATTRIBUTE_UNUSED)
{
  strcpy (filename_buff, name);
  return 0;
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
