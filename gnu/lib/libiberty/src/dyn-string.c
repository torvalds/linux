/* An abstract string datatype.
   Copyright (C) 1998, 1999, 2000, 2002, 2004 Free Software Foundation, Inc.
   Contributed by Mark Mitchell (mark@markmitchell.com).

This file is part of GNU CC.
   
GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combined
executable.)

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "libiberty.h"
#include "dyn-string.h"

/* If this file is being compiled for inclusion in the C++ runtime
   library, as part of the demangler implementation, we don't want to
   abort if an allocation fails.  Instead, percolate an error code up
   through the call chain.  */

#if defined(IN_LIBGCC2) || defined(IN_GLIBCPP_V3)
#define RETURN_ON_ALLOCATION_FAILURE
#endif

/* Performs in-place initialization of a dyn_string struct.  This
   function can be used with a dyn_string struct on the stack or
   embedded in another object.  The contents of of the string itself
   are still dynamically allocated.  The string initially is capable
   of holding at least SPACE characeters, including the terminating
   NUL.  If SPACE is 0, it will silently be increated to 1.  

   If RETURN_ON_ALLOCATION_FAILURE is defined and memory allocation
   fails, returns 0.  Otherwise returns 1.  */

int
dyn_string_init (struct dyn_string *ds_struct_ptr, int space)
{
  /* We need at least one byte in which to store the terminating NUL.  */
  if (space == 0)
    space = 1;

#ifdef RETURN_ON_ALLOCATION_FAILURE
  ds_struct_ptr->s = (char *) malloc (space);
  if (ds_struct_ptr->s == NULL)
    return 0;
#else
  ds_struct_ptr->s = XNEWVEC (char, space);
#endif
  ds_struct_ptr->allocated = space;
  ds_struct_ptr->length = 0;
  ds_struct_ptr->s[0] = '\0';

  return 1;
}

/* Create a new dynamic string capable of holding at least SPACE
   characters, including the terminating NUL.  If SPACE is 0, it will
   be silently increased to 1.  If RETURN_ON_ALLOCATION_FAILURE is
   defined and memory allocation fails, returns NULL.  Otherwise
   returns the newly allocated string.  */

dyn_string_t 
dyn_string_new (int space)
{
  dyn_string_t result;
#ifdef RETURN_ON_ALLOCATION_FAILURE
  result = (dyn_string_t) malloc (sizeof (struct dyn_string));
  if (result == NULL)
    return NULL;
  if (!dyn_string_init (result, space))
    {
      free (result);
      return NULL;
    }
#else
  result = XNEW (struct dyn_string);
  dyn_string_init (result, space);
#endif
  return result;
}

/* Free the memory used by DS.  */

void 
dyn_string_delete (dyn_string_t ds)
{
  free (ds->s);
  free (ds);
}

/* Returns the contents of DS in a buffer allocated with malloc.  It
   is the caller's responsibility to deallocate the buffer using free.
   DS is then set to the empty string.  Deletes DS itself.  */

char*
dyn_string_release (dyn_string_t ds)
{
  /* Store the old buffer.  */
  char* result = ds->s;
  /* The buffer is no longer owned by DS.  */
  ds->s = NULL;
  /* Delete DS.  */
  free (ds);
  /* Return the old buffer.  */
  return result;
}

/* Increase the capacity of DS so it can hold at least SPACE
   characters, plus the terminating NUL.  This function will not (at
   present) reduce the capacity of DS.  Returns DS on success. 

   If RETURN_ON_ALLOCATION_FAILURE is defined and a memory allocation
   operation fails, deletes DS and returns NULL.  */

dyn_string_t 
dyn_string_resize (dyn_string_t ds, int space)
{
  int new_allocated = ds->allocated;

  /* Increase SPACE to hold the NUL termination.  */
  ++space;

  /* Increase allocation by factors of two.  */
  while (space > new_allocated)
    new_allocated *= 2;
    
  if (new_allocated != ds->allocated)
    {
      ds->allocated = new_allocated;
      /* We actually need more space.  */
#ifdef RETURN_ON_ALLOCATION_FAILURE
      ds->s = (char *) realloc (ds->s, ds->allocated);
      if (ds->s == NULL)
	{
	  free (ds);
	  return NULL;
	}
#else
      ds->s = XRESIZEVEC (char, ds->s, ds->allocated);
#endif
    }

  return ds;
}

/* Sets the contents of DS to the empty string.  */

void
dyn_string_clear (dyn_string_t ds)
{
  /* A dyn_string always has room for at least the NUL terminator.  */
  ds->s[0] = '\0';
  ds->length = 0;
}

/* Makes the contents of DEST the same as the contents of SRC.  DEST
   and SRC must be distinct.  Returns 1 on success.  On failure, if
   RETURN_ON_ALLOCATION_FAILURE, deletes DEST and returns 0.  */

int
dyn_string_copy (dyn_string_t dest, dyn_string_t src)
{
  if (dest == src)
    abort ();

  /* Make room in DEST.  */
  if (dyn_string_resize (dest, src->length) == NULL)
    return 0;
  /* Copy DEST into SRC.  */
  strlcpy (dest->s, src->s, dest->allocated);
  /* Update the size of DEST.  */
  dest->length = src->length;
  return 1;
}

/* Copies SRC, a NUL-terminated string, into DEST.  Returns 1 on
   success.  On failure, if RETURN_ON_ALLOCATION_FAILURE, deletes DEST
   and returns 0.  */

int
dyn_string_copy_cstr (dyn_string_t dest, const char *src)
{
  int length = strlen (src);
  /* Make room in DEST.  */
  if (dyn_string_resize (dest, length) == NULL)
    return 0;
  /* Copy DEST into SRC.  */
  strlcpy (dest->s, src, dest->allocated);
  /* Update the size of DEST.  */
  dest->length = length;
  return 1;
}

/* Inserts SRC at the beginning of DEST.  DEST is expanded as
   necessary.  SRC and DEST must be distinct.  Returns 1 on success.
   On failure, if RETURN_ON_ALLOCATION_FAILURE, deletes DEST and
   returns 0.  */

int
dyn_string_prepend (dyn_string_t dest, dyn_string_t src)
{
  return dyn_string_insert (dest, 0, src);
}

/* Inserts SRC, a NUL-terminated string, at the beginning of DEST.
   DEST is expanded as necessary.  Returns 1 on success.  On failure,
   if RETURN_ON_ALLOCATION_FAILURE, deletes DEST and returns 0. */

int
dyn_string_prepend_cstr (dyn_string_t dest, const char *src)
{
  return dyn_string_insert_cstr (dest, 0, src);
}

/* Inserts SRC into DEST starting at position POS.  DEST is expanded
   as necessary.  SRC and DEST must be distinct.  Returns 1 on
   success.  On failure, if RETURN_ON_ALLOCATION_FAILURE, deletes DEST
   and returns 0.  */

int
dyn_string_insert (dyn_string_t dest, int pos, dyn_string_t src)
{
  int i;

  if (src == dest)
    abort ();

  if (dyn_string_resize (dest, dest->length + src->length) == NULL)
    return 0;
  /* Make room for the insertion.  Be sure to copy the NUL.  */
  for (i = dest->length; i >= pos; --i)
    dest->s[i + src->length] = dest->s[i];
  /* Splice in the new stuff.  */
  strncpy (dest->s + pos, src->s, src->length);
  /* Compute the new length.  */
  dest->length += src->length;
  return 1;
}

/* Inserts SRC, a NUL-terminated string, into DEST starting at
   position POS.  DEST is expanded as necessary.  Returns 1 on
   success.  On failure, RETURN_ON_ALLOCATION_FAILURE, deletes DEST
   and returns 0.  */

int
dyn_string_insert_cstr (dyn_string_t dest, int pos, const char *src)
{
  int i;
  int length = strlen (src);

  if (dyn_string_resize (dest, dest->length + length) == NULL)
    return 0;
  /* Make room for the insertion.  Be sure to copy the NUL.  */
  for (i = dest->length; i >= pos; --i)
    dest->s[i + length] = dest->s[i];
  /* Splice in the new stuff.  */
  strncpy (dest->s + pos, src, length);
  /* Compute the new length.  */
  dest->length += length;
  return 1;
}

/* Inserts character C into DEST starting at position POS.  DEST is
   expanded as necessary.  Returns 1 on success.  On failure,
   RETURN_ON_ALLOCATION_FAILURE, deletes DEST and returns 0.  */

int
dyn_string_insert_char (dyn_string_t dest, int pos, int c)
{
  int i;

  if (dyn_string_resize (dest, dest->length + 1) == NULL)
    return 0;
  /* Make room for the insertion.  Be sure to copy the NUL.  */
  for (i = dest->length; i >= pos; --i)
    dest->s[i + 1] = dest->s[i];
  /* Add the new character.  */
  dest->s[pos] = c;
  /* Compute the new length.  */
  ++dest->length;
  return 1;
}
     
/* Append S to DS, resizing DS if necessary.  Returns 1 on success.
   On failure, if RETURN_ON_ALLOCATION_FAILURE, deletes DEST and
   returns 0.  */

int
dyn_string_append (dyn_string_t dest, dyn_string_t s)
{
  if (dyn_string_resize (dest, dest->length + s->length) == 0)
    return 0;
  strlcpy (dest->s + dest->length, s->s, dest->allocated - dest->length);
  dest->length += s->length;
  return 1;
}

/* Append the NUL-terminated string S to DS, resizing DS if necessary.
   Returns 1 on success.  On failure, if RETURN_ON_ALLOCATION_FAILURE,
   deletes DEST and returns 0.  */

int
dyn_string_append_cstr (dyn_string_t dest, const char *s)
{
  int len = strlen (s);

  /* The new length is the old length plus the size of our string, plus
     one for the null at the end.  */
  if (dyn_string_resize (dest, dest->length + len) == NULL)
    return 0;
  strlcpy (dest->s + dest->length, s, dest->allocated - dest->length);
  dest->length += len;
  return 1;
}

/* Appends C to the end of DEST.  Returns 1 on success.  On failiure,
   if RETURN_ON_ALLOCATION_FAILURE, deletes DEST and returns 0.  */

int
dyn_string_append_char (dyn_string_t dest, int c)
{
  /* Make room for the extra character.  */
  if (dyn_string_resize (dest, dest->length + 1) == NULL)
    return 0;
  /* Append the character; it will overwrite the old NUL.  */
  dest->s[dest->length] = c;
  /* Add a new NUL at the end.  */
  dest->s[dest->length + 1] = '\0';
  /* Update the length.  */
  ++(dest->length);
  return 1;
}

/* Sets the contents of DEST to the substring of SRC starting at START
   and ending before END.  START must be less than or equal to END,
   and both must be between zero and the length of SRC, inclusive.
   Returns 1 on success.  On failure, if RETURN_ON_ALLOCATION_FAILURE,
   deletes DEST and returns 0.  */

int
dyn_string_substring (dyn_string_t dest, dyn_string_t src,
                      int start, int end)
{
  int i;
  int length = end - start;

  if (start > end || start > src->length || end > src->length)
    abort ();

  /* Make room for the substring.  */
  if (dyn_string_resize (dest, length) == NULL)
    return 0;
  /* Copy the characters in the substring,  */
  for (i = length; --i >= 0; )
    dest->s[i] = src->s[start + i];
  /* NUL-terimate the result.  */
  dest->s[length] = '\0';
  /* Record the length of the substring.  */
  dest->length = length;

  return 1;
}

/* Returns non-zero if DS1 and DS2 have the same contents.  */

int
dyn_string_eq (dyn_string_t ds1, dyn_string_t ds2)
{
  /* If DS1 and DS2 have different lengths, they must not be the same.  */
  if (ds1->length != ds2->length)
    return 0;
  else
    return !strcmp (ds1->s, ds2->s);
}
