/* Determine a canonical name for the current locale's character encoding.
   Copyright (C) 2000-2003 Free Software Foundation, Inc.
   This file is part of the GNU CHARSET Library.

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

#ifndef _LOCALCHARSET_H
#define _LOCALCHARSET_H


#ifdef __cplusplus
extern "C" {
#endif


/* Determine the current locale's character encoding, and canonicalize it
   into one of the canonical names listed in config.charset.
   The result must not be freed; it is statically allocated.
   If the canonical name cannot be determined, the result is a non-canonical
   name.  */
extern const char * locale_charset (void);


#ifdef __cplusplus
}
#endif


#endif /* _LOCALCHARSET_H */
