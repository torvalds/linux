/* Test for string function add boundaries of usable memory.
   Copyright (C) 1996,1997,1999,2000,2001,2002 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1996.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301 USA.  */

#define _GNU_SOURCE 1
#define __USE_GNU

/* Make sure we don't test the optimized inline functions if we want to
   test the real implementation.  */
#undef __USE_STRING_INLINES

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/param.h>

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

int
main (int argc, char *argv[])
{
  int size = sysconf (_SC_PAGESIZE);
  char *adr, *dest;
  int result = 0;

  adr = (char *) mmap (NULL, 3 * size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANON, -1, 0);
  dest = (char *) mmap (NULL, 3 * size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANON, -1, 0);
  if (adr == MAP_FAILED || dest == MAP_FAILED)
    {
      if (errno == ENOSYS)
        puts ("No test, mmap not available.");
      else
        {
          printf ("mmap failed: %m");
          result = 1;
        }
    }
  else
    {
      int inner, middle, outer;

      mprotect(adr, size, PROT_NONE);
      mprotect(adr + 2 * size, size, PROT_NONE);
      adr += size;

      mprotect(dest, size, PROT_NONE);
      mprotect(dest + 2 * size, size, PROT_NONE);
      dest += size;

      memset (adr, 'T', size);

      /* strlen test */
      for (outer = size - 1; outer >= MAX (0, size - 128); --outer)
        {
          for (inner = MAX (outer, size - 64); inner < size; ++inner)
	    {
	      adr[inner] = '\0';

	      if (strlen (&adr[outer]) != (size_t) (inner - outer))
		{
		  printf ("strlen flunked for outer = %d, inner = %d\n",
			  outer, inner);
		  result = 1;
		}

	      adr[inner] = 'T';
	    }
        }

      /* strchr test */
      for (outer = size - 1; outer >= MAX (0, size - 128); --outer)
        {
	  for (middle = MAX (outer, size - 64); middle < size; ++middle)
	    {
	      for (inner = middle; inner < size; ++inner)
		{
		  char *cp;
		  adr[middle] = 'V';
		  adr[inner] = '\0';

		  cp = strchr (&adr[outer], 'V');

		  if ((inner == middle && cp != NULL)
		      || (inner != middle
			  && (cp - &adr[outer]) != middle - outer))
		    {
		      printf ("strchr flunked for outer = %d, middle = %d, "
			      "inner = %d\n", outer, middle, inner);
		      result = 1;
		    }

		  adr[inner] = 'T';
		  adr[middle] = 'T';
		}
	    }
        }

      /* Special test.  */
      adr[size - 1] = '\0';
      if (strchr (&adr[size - 1], '\n') != NULL)
	{
	  puts ("strchr flunked for test of empty string at end of page");
	  result = 1;
	}

      /* strrchr test */
      for (outer = size - 1; outer >= MAX (0, size - 128); --outer)
        {
	  for (middle = MAX (outer, size - 64); middle < size; ++middle)
	    {
	      for (inner = middle; inner < size; ++inner)
		{
		  char *cp;
		  adr[middle] = 'V';
		  adr[inner] = '\0';

		  cp = strrchr (&adr[outer], 'V');

		  if ((inner == middle && cp != NULL)
		      || (inner != middle
			  && (cp - &adr[outer]) != middle - outer))
		    {
		      printf ("strrchr flunked for outer = %d, middle = %d, "
			      "inner = %d\n", outer, middle, inner);
		      result = 1;
		    }

		  adr[inner] = 'T';
		  adr[middle] = 'T';
		}
	    }
        }

#ifndef __FreeBSD__
      /* rawmemchr test */
      for (outer = size - 1; outer >= MAX (0, size - 128); --outer)
        {
	  for (middle = MAX (outer, size - 64); middle < size; ++middle)
	    {
	      char *cp;
	      adr[middle] = 'V';

	      cp = (char *) rawmemchr (&adr[outer], 'V');

	      if (cp - &adr[outer] != middle - outer)
		{
		  printf ("rawmemchr flunked for outer = %d, middle = %d\n",
			  outer, middle);
		  result = 1;
		}

	      adr[middle] = 'T';
	    }
        }
#endif

      /* strcpy test */
      for (outer = size - 1; outer >= MAX (0, size - 128); --outer)
        {
          for (inner = MAX (outer, size - 64); inner < size; ++inner)
	    {
	      adr[inner] = '\0';

	      if (strcpy (dest, &adr[outer]) != dest
		  || strlen (dest) != (size_t) (inner - outer))
		{
		  printf ("strcpy flunked for outer = %d, inner = %d\n",
			  outer, inner);
		  result = 1;
		}

	      adr[inner] = 'T';
	    }
        }

      /* strncpy tests */
      adr[size-1] = 'T';
      for (outer = size - 1; outer >= MAX (0, size - 128); --outer)
	{
	  size_t len;

	  for (len = 0; len < size - outer; ++len)
	    {
	      if (strncpy (dest, &adr[outer], len) != dest
		  || memcmp (dest, &adr[outer], len) != 0)
		{
		  printf ("outer strncpy flunked for outer = %d, len = %Zd\n",
			  outer, len);
		  result = 1;
		}
	    }
        }
      adr[size-1] = '\0';

      for (outer = size - 1; outer >= MAX (0, size - 128); --outer)
        {
          for (inner = MAX (outer, size - 64); inner < size; ++inner)
	    {
	      size_t len;

	      adr[inner] = '\0';

	      for (len = 0; len < size - outer + 64; ++len)
		{
		  if (strncpy (dest, &adr[outer], len) != dest
		      || memcmp (dest, &adr[outer],
				 MIN (inner - outer, len)) != 0
		      || (inner - outer < len
			  && strlen (dest) != (inner - outer)))
		    {
		      printf ("strncpy flunked for outer = %d, inner = %d, len = %Zd\n",
			      outer, inner, len);
		      result = 1;
		    }
		  if (strncpy (dest + 1, &adr[outer], len) != dest + 1
		      || memcmp (dest + 1, &adr[outer],
				 MIN (inner - outer, len)) != 0
		      || (inner - outer < len
			  && strlen (dest + 1) != (inner - outer)))
		    {
		      printf ("strncpy+1 flunked for outer = %d, inner = %d, len = %Zd\n",
			      outer, inner, len);
		      result = 1;
		    }
		}

	      adr[inner] = 'T';
	    }
        }

#ifndef __FreeBSD__
      /* stpcpy test */
      for (outer = size - 1; outer >= MAX (0, size - 128); --outer)
        {
          for (inner = MAX (outer, size - 64); inner < size; ++inner)
	    {
	      adr[inner] = '\0';

	      if ((stpcpy (dest, &adr[outer]) - dest) != inner - outer)
		{
		  printf ("stpcpy flunked for outer = %d, inner = %d\n",
			  outer, inner);
		  result = 1;
		}

	      adr[inner] = 'T';
	    }
        }

      /* stpncpy test */
      for (outer = size - 1; outer >= MAX (0, size - 128); --outer)
        {
          for (middle = MAX (outer, size - 64); middle < size; ++middle)
	    {
	      adr[middle] = '\0';

	      for (inner = 0; inner < size - outer; ++ inner)
		{
		  if ((stpncpy (dest, &adr[outer], inner) - dest)
		      != MIN (inner, middle - outer))
		    {
		      printf ("stpncpy flunked for outer = %d, middle = %d, "
			      "inner = %d\n", outer, middle, inner);
		      result = 1;
		    }
		}

	      adr[middle] = 'T';
	    }
        }
#endif

      /* memcpy test */
      for (outer = size - 1; outer >= MAX (0, size - 128); --outer)
	for (inner = 0; inner < size - outer; ++inner)
	  if (memcpy (dest, &adr[outer], inner) !=  dest)
	    {
	      printf ("memcpy flunked for outer = %d, inner = %d\n",
		      outer, inner);
	      result = 1;
	    }

#ifndef __FreeBSD__
      /* mempcpy test */
      for (outer = size - 1; outer >= MAX (0, size - 128); --outer)
	for (inner = 0; inner < size - outer; ++inner)
	  if (mempcpy (dest, &adr[outer], inner) !=  dest + inner)
	    {
	      printf ("mempcpy flunked for outer = %d, inner = %d\n",
		      outer, inner);
	      result = 1;
	    }
#endif
    }

  return result;
}
