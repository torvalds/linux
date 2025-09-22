/* Frv initialization file linked after all user modules
   Copyright (C) 1999, 2000, 2003, 2004 Free Software Foundation, Inc.
    Contributed by Red Hat, Inc.
  
   This file is part of GCC.
  
   GCC is free software ; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation * either version 2, or (at your option)
   any later version.
  
   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY ; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
  
   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

#include "defaults.h"
#include <stddef.h>
#include "unwind-dw2-fde.h"

#ifdef __FRV_UNDERSCORE__
#define UNDERSCORE "_"
#else
#define UNDERSCORE ""
#endif

#define FINI_SECTION_ZERO(SECTION, FLAGS, NAME)				\
__asm__ (".section " SECTION "," FLAGS "\n\t"				\
	 ".globl   " UNDERSCORE NAME "\n\t"				\
	 ".type    " UNDERSCORE NAME ",@object\n\t"			\
	 ".p2align  2\n"						\
	 UNDERSCORE NAME ":\n\t"					\
	 ".word     0\n\t"						\
	 ".previous")

#define FINI_SECTION(SECTION, FLAGS, NAME)				\
__asm__ (".section " SECTION "," FLAGS "\n\t"				\
	 ".globl   " UNDERSCORE NAME "\n\t"				\
	 ".type    " UNDERSCORE NAME ",@object\n\t"			\
	 ".p2align  2\n"						\
	 UNDERSCORE NAME ":\n\t"					\
	 ".previous")

/* End of .ctor/.dtor sections that provides a list of constructors and
   destructors to run.  */

FINI_SECTION_ZERO (".ctors", "\"aw\"", "__CTOR_END__");
FINI_SECTION_ZERO (".dtors", "\"aw\"", "__DTOR_END__");

/* End of .eh_frame section that provides all of the exception handling
   tables.  */

FINI_SECTION_ZERO (".eh_frame", "\"aw\"", "__FRAME_END__");

#if ! __FRV_FDPIC__
/* In FDPIC, the linker itself generates this.  */
/* End of .rofixup section that provides a list of pointers that we
   need to adjust.  */

FINI_SECTION (".rofixup", "\"a\"", "__ROFIXUP_END__");
#endif /* __FRV_FDPIC__ */
