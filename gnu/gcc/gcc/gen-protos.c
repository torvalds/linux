/* gen-protos.c - massages a list of prototypes, for use by fixproto.
   Copyright (C) 1993, 1994, 1995, 1996, 1998,
   1999, 2003, 2004, 2005 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "scan.h"
#include "errors.h"

int verbose = 0;

static void add_hash (const char *);
static int parse_fn_proto (char *, char *, struct fn_decl *);

#define HASH_SIZE 2503 /* a prime */
int hash_tab[HASH_SIZE];
int next_index;
int collisions;

static void
add_hash (const char *fname)
{
  int i, i0;

  /* NOTE:  If you edit this, also edit lookup_std_proto in fix-header.c !! */
  i = hashstr (fname, strlen (fname)) % HASH_SIZE;
  i0 = i;
  if (hash_tab[i] != 0)
    {
      collisions++;
      for (;;)
	{
	  i = (i+1) % HASH_SIZE;
	  gcc_assert (i != i0);
	  if (hash_tab[i] == 0)
	    break;
	}
    }
  hash_tab[i] = next_index;

  next_index++;
}

/* Given a function prototype, fill in the fields of FN.
   The result is a boolean indicating if a function prototype was found.

   The input string is modified (trailing NULs are inserted).
   The fields of FN point to the input string.  */

static int
parse_fn_proto (char *start, char *end, struct fn_decl *fn)
{
  char *ptr;
  int param_nesting = 1;
  char *param_start, *param_end, *decl_start, *name_start, *name_end;

  ptr = end - 1;
  while (*ptr == ' ' || *ptr == '\t') ptr--;
  if (*ptr-- != ';')
    {
      fprintf (stderr, "Funny input line: %s\n", start);
      return 0;
    }
  while (*ptr == ' ' || *ptr == '\t') ptr--;
  if (*ptr != ')')
    {
      fprintf (stderr, "Funny input line: %s\n", start);
      return 0;
    }
  param_end = ptr;
  for (;;)
    {
      int c = *--ptr;
      if (c == '(' && --param_nesting == 0)
	break;
      else if (c == ')')
	param_nesting++;
    }
  param_start = ptr+1;

  ptr--;
  while (*ptr == ' ' || *ptr == '\t') ptr--;

  if (!ISALNUM ((unsigned char)*ptr))
    {
      if (verbose)
	fprintf (stderr, "%s: Can't handle this complex prototype: %s\n",
		 progname, start);
      return 0;
    }
  name_end = ptr+1;

  while (ISIDNUM (*ptr))
    --ptr;
  name_start = ptr+1;
  while (*ptr == ' ' || *ptr == '\t') ptr--;
  ptr[1] = 0;
  *param_end = 0;
  *name_end = 0;

  decl_start = start;
  if (strncmp (decl_start, "typedef ", 8) == 0)
    return 0;
  if (strncmp (decl_start, "extern ", 7) == 0)
    decl_start += 7;

  fn->fname = name_start;
  fn->rtype = decl_start;
  fn->params = param_start;
  return 1;
}

int
main (int argc ATTRIBUTE_UNUSED, char **argv)
{
  FILE *inf = stdin;
  FILE *outf = stdout;
  int i;
  sstring linebuf;
  struct fn_decl fn_decl;

  i = strlen (argv[0]);
  while (i > 0 && argv[0][i-1] != '/') --i;
  progname = &argv[0][i];

  /* Unlock the stdio streams.  */
  unlock_std_streams ();

  INIT_SSTRING (&linebuf);

  fprintf (outf, "struct fn_decl std_protos[] = {\n");

  /* A hash table entry of 0 means "unused" so reserve it.  */
  fprintf (outf, "  {\"\", \"\", \"\", 0},\n");
  next_index = 1;

  for (;;)
    {
      int c = skip_spaces (inf, ' ');

      if (c == EOF)
	break;
      linebuf.ptr = linebuf.base;
      ungetc (c, inf);
      c = read_upto (inf, &linebuf, '\n');
      if (linebuf.base[0] == '#') /* skip cpp command */
	continue;
      if (linebuf.base[0] == '\0') /* skip empty line */
	continue;

      if (! parse_fn_proto (linebuf.base, linebuf.ptr, &fn_decl))
	continue;

      add_hash (fn_decl.fname);

      fprintf (outf, "  {\"%s\", \"%s\", \"%s\", 0},\n",
	       fn_decl.fname, fn_decl.rtype, fn_decl.params);

      if (c == EOF)
	break;
    }
  fprintf (outf, "  {0, 0, 0, 0}\n};\n");


  fprintf (outf, "#define HASH_SIZE %d\n", HASH_SIZE);
  fprintf (outf, "short hash_tab[HASH_SIZE] = {\n");
  for (i = 0; i < HASH_SIZE; i++)
    fprintf (outf, "  %d,\n", hash_tab[i]);
  fprintf (outf, "};\n");

  fprintf (stderr, "gen-protos: %d entries %d collisions\n",
	   next_index, collisions);

  return 0;
}
