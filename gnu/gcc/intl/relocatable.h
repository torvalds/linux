/* Provide relocatable packages.
   Copyright (C) 2003 Free Software Foundation, Inc.
   Written by Bruno Haible <bruno@clisp.org>, 2003.

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

#ifndef _RELOCATABLE_H
#define _RELOCATABLE_H

/* This can be enabled through the configure --enable-relocatable option.  */
#if ENABLE_RELOCATABLE

/* When building a DLL, we must export some functions.  Note that because
   this is a private .h file, we don't need to use __declspec(dllimport)
   in any case.  */
#if defined _MSC_VER && BUILDING_DLL
# define RELOCATABLE_DLL_EXPORTED __declspec(dllexport)
#else
# define RELOCATABLE_DLL_EXPORTED
#endif

/* Sets the original and the current installation prefix of the package.
   Relocation simply replaces a pathname starting with the original prefix
   by the corresponding pathname with the current prefix instead.  Both
   prefixes should be directory names without trailing slash (i.e. use ""
   instead of "/").  */
extern RELOCATABLE_DLL_EXPORTED void
       set_relocation_prefix (const char *orig_prefix,
			      const char *curr_prefix);

/* Returns the pathname, relocated according to the current installation
   directory.  */
extern const char * relocate (const char *pathname);

/* Memory management: relocate() leaks memory, because it has to construct
   a fresh pathname.  If this is a problem because your program calls
   relocate() frequently, think about caching the result.  */

/* Convenience function:
   Computes the current installation prefix, based on the original
   installation prefix, the original installation directory of a particular
   file, and the current pathname of this file.  Returns NULL upon failure.  */
extern const char * compute_curr_prefix (const char *orig_installprefix,
					 const char *orig_installdir,
					 const char *curr_pathname);

#else

/* By default, we use the hardwired pathnames.  */
#define relocate(pathname) (pathname)

#endif

#endif /* _RELOCATABLE_H */
