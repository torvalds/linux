/* Utilities to execute a program in a subprocess (possibly linked by pipes
   with other subprocesses), and wait for it.  Generic Unix version
   (also used for UWIN and VMS).
   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2003, 2004, 2005
   Free Software Foundation, Inc.

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

#include "config.h"
#include "libiberty.h"
#include "pex-common.h"

#include <stdio.h>
#include <signal.h>
#include <errno.h>
#ifdef NEED_DECLARATION_ERRNO
extern int errno;
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <sys/types.h>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_GETRUSAGE
#include <sys/time.h>
#include <sys/resource.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif


#ifdef vfork /* Autoconf may define this to fork for us. */
# define VFORK_STRING "fork"
#else
# define VFORK_STRING "vfork"
#endif
#ifdef HAVE_VFORK_H
#include <vfork.h>
#endif
#ifdef VMS
#define vfork() (decc$$alloc_vfork_blocks() >= 0 ? \
               lib$get_current_invo_context(decc$$get_vfork_jmpbuf()) : -1)
#endif /* VMS */


/* File mode to use for private and world-readable files.  */

#if defined (S_IRUSR) && defined (S_IWUSR) && defined (S_IRGRP) && defined (S_IWGRP) && defined (S_IROTH) && defined (S_IWOTH)
#define PUBLIC_MODE  \
    (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)
#else
#define PUBLIC_MODE 0666
#endif

/* Get the exit status of a particular process, and optionally get the
   time that it took.  This is simple if we have wait4, slightly
   harder if we have waitpid, and is a pain if we only have wait.  */

static pid_t pex_wait (struct pex_obj *, pid_t, int *, struct pex_time *);

#ifdef HAVE_WAIT4

static pid_t
pex_wait (struct pex_obj *obj ATTRIBUTE_UNUSED, pid_t pid, int *status,
	  struct pex_time *time)
{
  pid_t ret;
  struct rusage r;

#ifdef HAVE_WAITPID
  if (time == NULL)
    return waitpid (pid, status, 0);
#endif

  ret = wait4 (pid, status, 0, &r);

  if (time != NULL)
    {
      time->user_seconds = r.ru_utime.tv_sec;
      time->user_microseconds= r.ru_utime.tv_usec;
      time->system_seconds = r.ru_stime.tv_sec;
      time->system_microseconds= r.ru_stime.tv_usec;
    }

  return ret;
}

#else /* ! defined (HAVE_WAIT4) */

#ifdef HAVE_WAITPID

#ifndef HAVE_GETRUSAGE

static pid_t
pex_wait (struct pex_obj *obj ATTRIBUTE_UNUSED, pid_t pid, int *status,
	  struct pex_time *time)
{
  if (time != NULL)
    memset (time, 0, sizeof (struct pex_time));
  return waitpid (pid, status, 0);
}

#else /* defined (HAVE_GETRUSAGE) */

static pid_t
pex_wait (struct pex_obj *obj ATTRIBUTE_UNUSED, pid_t pid, int *status,
	  struct pex_time *time)
{
  struct rusage r1, r2;
  pid_t ret;

  if (time == NULL)
    return waitpid (pid, status, 0);

  getrusage (RUSAGE_CHILDREN, &r1);

  ret = waitpid (pid, status, 0);
  if (ret < 0)
    return ret;

  getrusage (RUSAGE_CHILDREN, &r2);

  time->user_seconds = r2.ru_utime.tv_sec - r1.ru_utime.tv_sec;
  time->user_microseconds = r2.ru_utime.tv_usec - r1.ru_utime.tv_usec;
  if (r2.ru_utime.tv_usec < r1.ru_utime.tv_usec)
    {
      --time->user_seconds;
      time->user_microseconds += 1000000;
    }

  time->system_seconds = r2.ru_stime.tv_sec - r1.ru_stime.tv_sec;
  time->system_microseconds = r2.ru_stime.tv_usec - r1.ru_stime.tv_usec;
  if (r2.ru_stime.tv_usec < r1.ru_stime.tv_usec)
    {
      --time->system_seconds;
      time->system_microseconds += 1000000;
    }

  return ret;
}

#endif /* defined (HAVE_GETRUSAGE) */

#else /* ! defined (HAVE_WAITPID) */

struct status_list
{
  struct status_list *next;
  pid_t pid;
  int status;
  struct pex_time time;
};

static pid_t
pex_wait (struct pex_obj *obj, pid_t pid, int *status, struct pex_time *time)
{
  struct status_list **pp;

  for (pp = (struct status_list **) &obj->sysdep;
       *pp != NULL;
       pp = &(*pp)->next)
    {
      if ((*pp)->pid == pid)
	{
	  struct status_list *p;

	  p = *pp;
	  *status = p->status;
	  if (time != NULL)
	    *time = p->time;
	  *pp = p->next;
	  free (p);
	  return pid;
	}
    }

  while (1)
    {
      pid_t cpid;
      struct status_list *psl;
      struct pex_time pt;
#ifdef HAVE_GETRUSAGE
      struct rusage r1, r2;
#endif

      if (time != NULL)
	{
#ifdef HAVE_GETRUSAGE
	  getrusage (RUSAGE_CHILDREN, &r1);
#else
	  memset (&pt, 0, sizeof (struct pex_time));
#endif
	}

      cpid = wait (status);

#ifdef HAVE_GETRUSAGE
      if (time != NULL && cpid >= 0)
	{
	  getrusage (RUSAGE_CHILDREN, &r2);

	  pt.user_seconds = r2.ru_utime.tv_sec - r1.ru_utime.tv_sec;
	  pt.user_microseconds = r2.ru_utime.tv_usec - r1.ru_utime.tv_usec;
	  if (pt.user_microseconds < 0)
	    {
	      --pt.user_seconds;
	      pt.user_microseconds += 1000000;
	    }

	  pt.system_seconds = r2.ru_stime.tv_sec - r1.ru_stime.tv_sec;
	  pt.system_microseconds = r2.ru_stime.tv_usec - r1.ru_stime.tv_usec;
	  if (pt.system_microseconds < 0)
	    {
	      --pt.system_seconds;
	      pt.system_microseconds += 1000000;
	    }
	}
#endif

      if (cpid < 0 || cpid == pid)
	{
	  if (time != NULL)
	    *time = pt;
	  return cpid;
	}

      psl = XNEW (struct status_list);
      psl->pid = cpid;
      psl->status = *status;
      if (time != NULL)
	psl->time = pt;
      psl->next = (struct status_list *) obj->sysdep;
      obj->sysdep = (void *) psl;
    }
}

#endif /* ! defined (HAVE_WAITPID) */
#endif /* ! defined (HAVE_WAIT4) */

static void pex_child_error (struct pex_obj *, const char *, const char *, int)
     ATTRIBUTE_NORETURN;
static int pex_unix_open_read (struct pex_obj *, const char *, int);
static int pex_unix_open_write (struct pex_obj *, const char *, int);
static long pex_unix_exec_child (struct pex_obj *, int, const char *,
				 char * const *, char * const *,
				 int, int, int, int,
				 const char **, int *);
static int pex_unix_close (struct pex_obj *, int);
static int pex_unix_wait (struct pex_obj *, long, int *, struct pex_time *,
			  int, const char **, int *);
static int pex_unix_pipe (struct pex_obj *, int *, int);
static FILE *pex_unix_fdopenr (struct pex_obj *, int, int);
static FILE *pex_unix_fdopenw (struct pex_obj *, int, int);
static void pex_unix_cleanup (struct pex_obj *);

/* The list of functions we pass to the common routines.  */

const struct pex_funcs funcs =
{
  pex_unix_open_read,
  pex_unix_open_write,
  pex_unix_exec_child,
  pex_unix_close,
  pex_unix_wait,
  pex_unix_pipe,
  pex_unix_fdopenr,
  pex_unix_fdopenw,
  pex_unix_cleanup
};

/* Return a newly initialized pex_obj structure.  */

struct pex_obj *
pex_init (int flags, const char *pname, const char *tempbase)
{
  return pex_init_common (flags, pname, tempbase, &funcs);
}

/* Open a file for reading.  */

static int
pex_unix_open_read (struct pex_obj *obj ATTRIBUTE_UNUSED, const char *name,
		    int binary ATTRIBUTE_UNUSED)
{
  return open (name, O_RDONLY);
}

/* Open a file for writing.  */

static int
pex_unix_open_write (struct pex_obj *obj ATTRIBUTE_UNUSED, const char *name,
		     int binary ATTRIBUTE_UNUSED)
{
  /* Note that we can't use O_EXCL here because gcc may have already
     created the temporary file via make_temp_file.  */
  return open (name, O_WRONLY | O_CREAT | O_TRUNC, PUBLIC_MODE);
}

/* Close a file.  */

static int
pex_unix_close (struct pex_obj *obj ATTRIBUTE_UNUSED, int fd)
{
  return close (fd);
}

/* Report an error from a child process.  We don't use stdio routines,
   because we might be here due to a vfork call.  */

static void
pex_child_error (struct pex_obj *obj, const char *executable,
		 const char *errmsg, int err)
{
#define writeerr(s) write (STDERR_FILE_NO, s, strlen (s))
  writeerr (obj->pname);
  writeerr (": error trying to exec '");
  writeerr (executable);
  writeerr ("': ");
  writeerr (errmsg);
  writeerr (": ");
  writeerr (xstrerror (err));
  writeerr ("\n");
  _exit (-1);
}

/* Execute a child.  */

extern char **environ;

static long
pex_unix_exec_child (struct pex_obj *obj, int flags, const char *executable,
		     char * const * argv, char * const * env,
                     int in, int out, int errdes,
		     int toclose, const char **errmsg, int *err)
{
  pid_t pid;

  /* We declare these to be volatile to avoid warnings from gcc about
     them being clobbered by vfork.  */
  volatile int sleep_interval;
  volatile int retries;

  sleep_interval = 1;
  pid = -1;
  for (retries = 0; retries < 4; ++retries)
    {
      pid = vfork ();
      if (pid >= 0)
	break;
      sleep (sleep_interval);
      sleep_interval *= 2;
    }

  switch (pid)
    {
    case -1:
      *err = errno;
      *errmsg = VFORK_STRING;
      return -1;

    case 0:
      /* Child process.  */
      if (in != STDIN_FILE_NO)
	{
	  if (dup2 (in, STDIN_FILE_NO) < 0)
	    pex_child_error (obj, executable, "dup2", errno);
	  if (close (in) < 0)
	    pex_child_error (obj, executable, "close", errno);
	}
      if (out != STDOUT_FILE_NO)
	{
	  if (dup2 (out, STDOUT_FILE_NO) < 0)
	    pex_child_error (obj, executable, "dup2", errno);
	  if (close (out) < 0)
	    pex_child_error (obj, executable, "close", errno);
	}
      if (errdes != STDERR_FILE_NO)
	{
	  if (dup2 (errdes, STDERR_FILE_NO) < 0)
	    pex_child_error (obj, executable, "dup2", errno);
	  if (close (errdes) < 0)
	    pex_child_error (obj, executable, "close", errno);
	}
      if (toclose >= 0)
	{
	  if (close (toclose) < 0)
	    pex_child_error (obj, executable, "close", errno);
	}
      if ((flags & PEX_STDERR_TO_STDOUT) != 0)
	{
	  if (dup2 (STDOUT_FILE_NO, STDERR_FILE_NO) < 0)
	    pex_child_error (obj, executable, "dup2", errno);
	}

      if (env)
        environ = (char**) env;

      if ((flags & PEX_SEARCH) != 0)
	{
	  execvp (executable, argv);
	  pex_child_error (obj, executable, "execvp", errno);
	}
      else
	{
	  execv (executable, argv);
	  pex_child_error (obj, executable, "execv", errno);
	}

      /* NOTREACHED */
      return -1;

    default:
      /* Parent process.  */
      if (in != STDIN_FILE_NO)
	{
	  if (close (in) < 0)
	    {
	      *err = errno;
	      *errmsg = "close";
	      return -1;
	    }
	}
      if (out != STDOUT_FILE_NO)
	{
	  if (close (out) < 0)
	    {
	      *err = errno;
	      *errmsg = "close";
	      return -1;
	    }
	}
      if (errdes != STDERR_FILE_NO)
	{
	  if (close (errdes) < 0)
	    {
	      *err = errno;
	      *errmsg = "close";
	      return -1;
	    }
	}

      return (long) pid;
    }
}

/* Wait for a child process to complete.  */

static int
pex_unix_wait (struct pex_obj *obj, long pid, int *status,
	       struct pex_time *time, int done, const char **errmsg,
	       int *err)
{
  /* If we are cleaning up when the caller didn't retrieve process
     status for some reason, encourage the process to go away.  */
  if (done)
    kill (pid, SIGTERM);

  if (pex_wait (obj, pid, status, time) < 0)
    {
      *err = errno;
      *errmsg = "wait";
      return -1;
    }

  return 0;
}

/* Create a pipe.  */

static int
pex_unix_pipe (struct pex_obj *obj ATTRIBUTE_UNUSED, int *p,
	       int binary ATTRIBUTE_UNUSED)
{
  return pipe (p);
}

/* Get a FILE pointer to read from a file descriptor.  */

static FILE *
pex_unix_fdopenr (struct pex_obj *obj ATTRIBUTE_UNUSED, int fd,
		  int binary ATTRIBUTE_UNUSED)
{
  return fdopen (fd, "r");
}

static FILE *
pex_unix_fdopenw (struct pex_obj *obj ATTRIBUTE_UNUSED, int fd,
		  int binary ATTRIBUTE_UNUSED)
{
  if (fcntl (fd, F_SETFD, FD_CLOEXEC) < 0)
    return NULL;
  return fdopen (fd, "w");
}

static void
pex_unix_cleanup (struct pex_obj *obj ATTRIBUTE_UNUSED)
{
#if !defined (HAVE_WAIT4) && !defined (HAVE_WAITPID)
  while (obj->sysdep != NULL)
    {
      struct status_list *this;
      struct status_list *next;

      this = (struct status_list *) obj->sysdep;
      next = this->next;
      free (this);
      obj->sysdep = (void *) next;
    }
#endif
}
