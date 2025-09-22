/* Implementation of the bindtextdomain(3) function
   Copyright (C) 1995-1998, 2000, 2001, 2002 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU Library General Public License as published
   by the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301,
   USA.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef _LIBC
# include <libintl.h>
#else
# include "libgnuintl.h"
#endif
#include "gettextP.h"

#ifdef _LIBC
/* We have to handle multi-threaded applications.  */
# include <bits/libc-lock.h>
#else
/* Provide dummy implementation if this is outside glibc.  */
# define __libc_rwlock_define(CLASS, NAME)
# define __libc_rwlock_wrlock(NAME)
# define __libc_rwlock_unlock(NAME)
#endif

/* The internal variables in the standalone libintl.a must have different
   names than the internal variables in GNU libc, otherwise programs
   using libintl.a cannot be linked statically.  */
#if !defined _LIBC
# define _nl_default_dirname libintl_nl_default_dirname
# define _nl_domain_bindings libintl_nl_domain_bindings
#endif

/* Some compilers, like SunOS4 cc, don't have offsetof in <stddef.h>.  */
#ifndef offsetof
# define offsetof(type,ident) ((size_t)&(((type*)0)->ident))
#endif

/* @@ end of prolog @@ */

/* Contains the default location of the message catalogs.  */
extern const char _nl_default_dirname[];
#ifdef _LIBC
extern const char _nl_default_dirname_internal[] attribute_hidden;
#else
# define INTUSE(name) name
#endif

/* List with bindings of specific domains.  */
extern struct binding *_nl_domain_bindings;

/* Lock variable to protect the global data in the gettext implementation.  */
__libc_rwlock_define (extern, _nl_state_lock attribute_hidden)


/* Names for the libintl functions are a problem.  They must not clash
   with existing names and they should follow ANSI C.  But this source
   code is also used in GNU C Library where the names have a __
   prefix.  So we have to make a difference here.  */
#ifdef _LIBC
# define BINDTEXTDOMAIN __bindtextdomain
# define BIND_TEXTDOMAIN_CODESET __bind_textdomain_codeset
# ifndef strdup
#  define strdup(str) __strdup (str)
# endif
#else
# define BINDTEXTDOMAIN libintl_bindtextdomain
# define BIND_TEXTDOMAIN_CODESET libintl_bind_textdomain_codeset
#endif

/* Prototypes for local functions.  */
static void set_binding_values PARAMS ((const char *domainname,
					const char **dirnamep,
					const char **codesetp));

/* Specifies the directory name *DIRNAMEP and the output codeset *CODESETP
   to be used for the DOMAINNAME message catalog.
   If *DIRNAMEP or *CODESETP is NULL, the corresponding attribute is not
   modified, only the current value is returned.
   If DIRNAMEP or CODESETP is NULL, the corresponding attribute is neither
   modified nor returned.  */
static void
set_binding_values (domainname, dirnamep, codesetp)
     const char *domainname;
     const char **dirnamep;
     const char **codesetp;
{
  struct binding *binding;
  int modified;

  /* Some sanity checks.  */
  if (domainname == NULL || domainname[0] == '\0')
    {
      if (dirnamep)
	*dirnamep = NULL;
      if (codesetp)
	*codesetp = NULL;
      return;
    }

  __libc_rwlock_wrlock (_nl_state_lock);

  modified = 0;

  for (binding = _nl_domain_bindings; binding != NULL; binding = binding->next)
    {
      int compare = strcmp (domainname, binding->domainname);
      if (compare == 0)
	/* We found it!  */
	break;
      if (compare < 0)
	{
	  /* It is not in the list.  */
	  binding = NULL;
	  break;
	}
    }

  if (binding != NULL)
    {
      if (dirnamep)
	{
	  const char *dirname = *dirnamep;

	  if (dirname == NULL)
	    /* The current binding has be to returned.  */
	    *dirnamep = binding->dirname;
	  else
	    {
	      /* The domain is already bound.  If the new value and the old
		 one are equal we simply do nothing.  Otherwise replace the
		 old binding.  */
	      char *result = binding->dirname;
	      if (strcmp (dirname, result) != 0)
		{
		  if (strcmp (dirname, INTUSE(_nl_default_dirname)) == 0)
		    result = (char *) INTUSE(_nl_default_dirname);
		  else
		    {
#if defined _LIBC || defined HAVE_STRDUP
		      result = strdup (dirname);
#else
		      size_t len = strlen (dirname) + 1;
		      result = (char *) malloc (len);
		      if (__builtin_expect (result != NULL, 1))
			memcpy (result, dirname, len);
#endif
		    }

		  if (__builtin_expect (result != NULL, 1))
		    {
		      if (binding->dirname != INTUSE(_nl_default_dirname))
			free (binding->dirname);

		      binding->dirname = result;
		      modified = 1;
		    }
		}
	      *dirnamep = result;
	    }
	}

      if (codesetp)
	{
	  const char *codeset = *codesetp;

	  if (codeset == NULL)
	    /* The current binding has be to returned.  */
	    *codesetp = binding->codeset;
	  else
	    {
	      /* The domain is already bound.  If the new value and the old
		 one are equal we simply do nothing.  Otherwise replace the
		 old binding.  */
	      char *result = binding->codeset;
	      if (result == NULL || strcmp (codeset, result) != 0)
		{
#if defined _LIBC || defined HAVE_STRDUP
		  result = strdup (codeset);
#else
		  size_t len = strlen (codeset) + 1;
		  result = (char *) malloc (len);
		  if (__builtin_expect (result != NULL, 1))
		    memcpy (result, codeset, len);
#endif

		  if (__builtin_expect (result != NULL, 1))
		    {
		      if (binding->codeset != NULL)
			free (binding->codeset);

		      binding->codeset = result;
		      binding->codeset_cntr++;
		      modified = 1;
		    }
		}
	      *codesetp = result;
	    }
	}
    }
  else if ((dirnamep == NULL || *dirnamep == NULL)
	   && (codesetp == NULL || *codesetp == NULL))
    {
      /* Simply return the default values.  */
      if (dirnamep)
	*dirnamep = INTUSE(_nl_default_dirname);
      if (codesetp)
	*codesetp = NULL;
    }
  else
    {
      /* We have to create a new binding.  */
      size_t len = strlen (domainname) + 1;
      struct binding *new_binding =
	(struct binding *) malloc (offsetof (struct binding, domainname) + len);

      if (__builtin_expect (new_binding == NULL, 0))
	goto failed;

      memcpy (new_binding->domainname, domainname, len);

      if (dirnamep)
	{
	  const char *dirname = *dirnamep;

	  if (dirname == NULL)
	    /* The default value.  */
	    dirname = INTUSE(_nl_default_dirname);
	  else
	    {
	      if (strcmp (dirname, INTUSE(_nl_default_dirname)) == 0)
		dirname = INTUSE(_nl_default_dirname);
	      else
		{
		  char *result;
#if defined _LIBC || defined HAVE_STRDUP
		  result = strdup (dirname);
		  if (__builtin_expect (result == NULL, 0))
		    goto failed_dirname;
#else
		  size_t len = strlen (dirname) + 1;
		  result = (char *) malloc (len);
		  if (__builtin_expect (result == NULL, 0))
		    goto failed_dirname;
		  memcpy (result, dirname, len);
#endif
		  dirname = result;
		}
	    }
	  *dirnamep = dirname;
	  new_binding->dirname = (char *) dirname;
	}
      else
	/* The default value.  */
	new_binding->dirname = (char *) INTUSE(_nl_default_dirname);

      new_binding->codeset_cntr = 0;

      if (codesetp)
	{
	  const char *codeset = *codesetp;

	  if (codeset != NULL)
	    {
	      char *result;

#if defined _LIBC || defined HAVE_STRDUP
	      result = strdup (codeset);
	      if (__builtin_expect (result == NULL, 0))
		goto failed_codeset;
#else
	      size_t len = strlen (codeset) + 1;
	      result = (char *) malloc (len);
	      if (__builtin_expect (result == NULL, 0))
		goto failed_codeset;
	      memcpy (result, codeset, len);
#endif
	      codeset = result;
	      new_binding->codeset_cntr++;
	    }
	  *codesetp = codeset;
	  new_binding->codeset = (char *) codeset;
	}
      else
	new_binding->codeset = NULL;

      /* Now enqueue it.  */
      if (_nl_domain_bindings == NULL
	  || strcmp (domainname, _nl_domain_bindings->domainname) < 0)
	{
	  new_binding->next = _nl_domain_bindings;
	  _nl_domain_bindings = new_binding;
	}
      else
	{
	  binding = _nl_domain_bindings;
	  while (binding->next != NULL
		 && strcmp (domainname, binding->next->domainname) > 0)
	    binding = binding->next;

	  new_binding->next = binding->next;
	  binding->next = new_binding;
	}

      modified = 1;

      /* Here we deal with memory allocation failures.  */
      if (0)
	{
	failed_codeset:
	  if (new_binding->dirname != INTUSE(_nl_default_dirname))
	    free (new_binding->dirname);
	failed_dirname:
	  free (new_binding);
	failed:
	  if (dirnamep)
	    *dirnamep = NULL;
	  if (codesetp)
	    *codesetp = NULL;
	}
    }

  /* If we modified any binding, we flush the caches.  */
  if (modified)
    ++_nl_msg_cat_cntr;

  __libc_rwlock_unlock (_nl_state_lock);
}

/* Specify that the DOMAINNAME message catalog will be found
   in DIRNAME rather than in the system locale data base.  */
char *
BINDTEXTDOMAIN (domainname, dirname)
     const char *domainname;
     const char *dirname;
{
  set_binding_values (domainname, &dirname, NULL);
  return (char *) dirname;
}

/* Specify the character encoding in which the messages from the
   DOMAINNAME message catalog will be returned.  */
char *
BIND_TEXTDOMAIN_CODESET (domainname, codeset)
     const char *domainname;
     const char *codeset;
{
  set_binding_values (domainname, NULL, &codeset);
  return (char *) codeset;
}

#ifdef _LIBC
/* Aliases for function names in GNU C Library.  */
weak_alias (__bindtextdomain, bindtextdomain);
weak_alias (__bind_textdomain_codeset, bind_textdomain_codeset);
#endif
