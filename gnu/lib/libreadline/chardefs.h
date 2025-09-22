/* chardefs.h -- Character definitions for readline. */

/* Copyright (C) 1994 Free Software Foundation, Inc.

   This file is part of the GNU Readline Library, a library for
   reading lines of text with interactive input and history editing.

   The GNU Readline Library is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2, or
   (at your option) any later version.

   The GNU Readline Library is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   The GNU General Public License is often shipped with GNU software, and
   is generally kept in a file called COPYING or LICENSE.  If you do not
   have a copy of the license, write to the Free Software Foundation,
   59 Temple Place, Suite 330, Boston, MA 02111 USA. */

#ifndef _CHARDEFS_H_
#define _CHARDEFS_H_

#include <ctype.h>

#if defined (HAVE_CONFIG_H)
#  if defined (HAVE_STRING_H)
#    if ! defined (STDC_HEADERS) && defined (HAVE_MEMORY_H)
#      include <memory.h>
#    endif
#    include <string.h>
#  endif /* HAVE_STRING_H */
#  if defined (HAVE_STRINGS_H)
#    include <strings.h>
#  endif /* HAVE_STRINGS_H */
#else
#  include <string.h>
#endif /* !HAVE_CONFIG_H */

#ifndef whitespace
#define whitespace(c) (((c) == ' ') || ((c) == '\t'))
#endif

#ifdef CTRL
#  undef CTRL
#endif
#ifdef UNCTRL
#  undef UNCTRL
#endif

/* Some character stuff. */
#define control_character_threshold 0x020   /* Smaller than this is control. */
#define control_character_mask 0x1f	    /* 0x20 - 1 */
#define meta_character_threshold 0x07f	    /* Larger than this is Meta. */
#define control_character_bit 0x40	    /* 0x000000, must be off. */
#define meta_character_bit 0x080	    /* x0000000, must be on. */
#define largest_char 255		    /* Largest character value. */

#define CTRL_CHAR(c) ((c) < control_character_threshold && (((c) & 0x80) == 0))
#define META_CHAR(c) ((c) > meta_character_threshold && (c) <= largest_char)

#define CTRL(c) ((c) & control_character_mask)
#define META(c) ((c) | meta_character_bit)

#define UNMETA(c) ((c) & (~meta_character_bit))
#define UNCTRL(c) _rl_to_upper(((c)|control_character_bit))

#if defined STDC_HEADERS || (!defined (isascii) && !defined (HAVE_ISASCII))
#  define IN_CTYPE_DOMAIN(c) 1
#else
#  define IN_CTYPE_DOMAIN(c) isascii(c)
#endif

#if !defined (isxdigit) && !defined (HAVE_ISXDIGIT)
#  define isxdigit(c)   (isdigit((c)) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))
#endif

#define NON_NEGATIVE(c)	((unsigned char)(c) == (c))

/* Some systems define these; we want our definitions. */
#undef ISPRINT

#define ISALNUM(c)	(IN_CTYPE_DOMAIN (c) && isalnum (c))
#define ISALPHA(c)	(IN_CTYPE_DOMAIN (c) && isalpha (c))
#define ISDIGIT(c)	(IN_CTYPE_DOMAIN (c) && isdigit (c))
#define ISLOWER(c)	(IN_CTYPE_DOMAIN (c) && islower (c))
#define ISPRINT(c)	(IN_CTYPE_DOMAIN (c) && isprint (c))
#define ISUPPER(c)	(IN_CTYPE_DOMAIN (c) && isupper (c))
#define ISXDIGIT(c)	(IN_CTYPE_DOMAIN (c) && isxdigit (c))

#define _rl_lowercase_p(c)	(NON_NEGATIVE(c) && ISLOWER(c))
#define _rl_uppercase_p(c)	(NON_NEGATIVE(c) && ISUPPER(c))
#define _rl_digit_p(c)		((c) >= '0' && (c) <= '9')

#define _rl_pure_alphabetic(c)	(NON_NEGATIVE(c) && ISALPHA(c))
#define ALPHABETIC(c)		(NON_NEGATIVE(c) && ISALNUM(c))

#ifndef _rl_to_upper
#  define _rl_to_upper(c) (_rl_lowercase_p(c) ? toupper((unsigned char)c) : (c))
#  define _rl_to_lower(c) (_rl_uppercase_p(c) ? tolower((unsigned char)c) : (c))
#endif

#ifndef _rl_digit_value
#  define _rl_digit_value(x) ((x) - '0')
#endif

#ifndef _rl_isident
#  define _rl_isident(c) (ISALNUM(c) || (c) == '_')
#endif

#ifndef ISOCTAL
#  define ISOCTAL(c)	((c) >= '0' && (c) <= '7')
#endif
#define OCTVALUE(c)	((c) - '0')

#define HEXVALUE(c) \
  (((c) >= 'a' && (c) <= 'f') \
	? (c)-'a'+10 \
	: (c) >= 'A' && (c) <= 'F' ? (c)-'A'+10 : (c)-'0')

#ifndef NEWLINE
#define NEWLINE '\n'
#endif

#ifndef RETURN
#define RETURN CTRL('M')
#endif

#ifndef RUBOUT
#define RUBOUT 0x7f
#endif

#ifndef TAB
#define TAB '\t'
#endif

#ifdef ABORT_CHAR
#undef ABORT_CHAR
#endif
#define ABORT_CHAR CTRL('G')

#ifdef PAGE
#undef PAGE
#endif
#define PAGE CTRL('L')

#ifdef SPACE
#undef SPACE
#endif
#define SPACE ' '	/* XXX - was 0x20 */

#ifdef ESC
#undef ESC
#endif
#define ESC CTRL('[')

#endif  /* _CHARDEFS_H_ */
