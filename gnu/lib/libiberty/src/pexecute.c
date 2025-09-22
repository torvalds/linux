/* Utilities to execute a program in a subprocess (possibly linked by pipes
   with other subprocesses), and wait for it.
   Copyright (C) 2004 Free Software Foundation, Inc.

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If not,
write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* pexecute is an old routine.  This implementation uses the newer
   pex_init/pex_run/pex_get_status/pex_free routines.  Don't use
   pexecute in new code.  Use the newer routines instead.  */

#include "config.h"
#include "libiberty.h"
#include "pex-protos.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

/* We only permit a single pexecute chain to execute at a time.  This
   was always true anyhow, though it wasn't documented.  */

static struct pex_obj *pex;
static int idx;

int
pexecute (const char *program, char * const *argv, const char *pname,
	  const char *temp_base, char **errmsg_fmt, char **errmsg_arg,
	  int flags)
{
  const char *errmsg;
  int err;

  if ((flags & PEXECUTE_FIRST) != 0)
    {
      if (pex != NULL)
	{
	  *errmsg_fmt = (char *) "pexecute already in progress";
	  *errmsg_arg = NULL;
	  return -1;
	}
      pex = pex_init (PEX_USE_PIPES, pname, temp_base);
      idx = 0;
    }
  else
    {
      if (pex == NULL)
	{
	  *errmsg_fmt = (char *) "pexecute not in progress";
	  *errmsg_arg = NULL;
	  return -1;
	}
    }

  errmsg = pex_run (pex,
		    (((flags & PEXECUTE_LAST) != 0 ? PEX_LAST : 0)
		     | ((flags & PEXECUTE_SEARCH) != 0 ? PEX_SEARCH : 0)),
		    program, argv, NULL, NULL, &err);
  if (errmsg != NULL)
    {
      *errmsg_fmt = (char *) errmsg;
      *errmsg_arg = NULL;
      return -1;
    }

  /* Instead of a PID, we just return a one-based index into the
     status values.  We avoid zero just because the old pexecute would
     never return it.  */
  return ++idx;
}

int
pwait (int pid, int *status, int flags ATTRIBUTE_UNUSED)
{
  /* The PID returned by pexecute is one-based.  */
  --pid;

  if (pex == NULL || pid < 0 || pid >= idx)
    return -1;

  if (pid == 0 && idx == 1)
    {
      if (!pex_get_status (pex, 1, status))
	return -1;
    }
  else
    {
      int *vector;

      vector = XNEWVEC (int, idx);
      if (!pex_get_status (pex, idx, vector))
	{
	  free (vector);
	  return -1;
	}
      *status = vector[pid];
      free (vector);
    }

  /* Assume that we are done after the caller has retrieved the last
     exit status.  The original implementation did not require that
     the exit statuses be retrieved in order, but this implementation
     does.  */
  if (pid + 1 == idx)
    {
      pex_free (pex);
      pex = NULL;
      idx = 0;
    }

  return pid + 1;
}
