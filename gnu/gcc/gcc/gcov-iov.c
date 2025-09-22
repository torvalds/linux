/* Generate gcov version string from version.c. See gcov-io.h for
   description of how the version string is generated.
   Copyright (C) 2002, 2003, 2005 Free Software Foundation, Inc.
   Contributed by Nathan Sidwell <nathan@codesourcery.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include <stdio.h>
#include <stdlib.h>

/* Command line arguments are the base GCC version and the development
   phase (the latter may be an empty string).  */

int
main (int argc, char **argv)
{
  unsigned int version = 0;
  unsigned char v[4];
  unsigned int ix;
  unsigned long major;
  unsigned long minor = 0;
  char phase = 0;
  char *ptr;

  if (argc != 3)
    {
      fprintf (stderr, "usage: %s 'version' 'phase'\n", argv[0]);
      return 1;
    }

  ptr = argv[1];
  major = strtoul (ptr, &ptr, 10);

  if (*ptr == '.')
    minor = strtoul (ptr + 1, 0, 10);

  phase = argv[2][0];
  if (phase == '\0')
    phase = '*';

  v[0] = (major < 10 ? '0' : 'A' - 10) + major;
  v[1] = (minor / 10) + '0';
  v[2] = (minor % 10) + '0';
  v[3] = phase;

  for (ix = 0; ix != 4; ix++)
    version = (version << 8) | v[ix];

  printf ("/* Generated automatically by the program `%s'\n", argv[0]);
  printf ("   from `%s (%lu %lu) and %s (%c)'.  */\n",
	  argv[1], major, minor, argv[2], phase);
  printf ("\n");
  printf ("#define GCOV_VERSION ((gcov_unsigned_t)%#08x)  /* %.4s */\n",
	  version, v);

  return 0;
}
