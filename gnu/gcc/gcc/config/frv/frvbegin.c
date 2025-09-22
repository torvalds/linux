/* Frv initialization file linked before all user modules
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
   Boston, MA 02110-1301, USA.

   This file was originally taken from the file crtstuff.c in the
   main compiler directory, and simplified.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

#include "defaults.h"
#include <stddef.h>
#include "unwind-dw2-fde.h"
#include "gbl-ctors.h"

/*  Declare a pointer to void function type.  */
#define STATIC static

#ifdef __FRV_UNDERSCORE__
#define UNDERSCORE "_"
#else
#define UNDERSCORE ""
#endif

#define INIT_SECTION_NEG_ONE(SECTION, FLAGS, NAME)			\
__asm__ (".section " SECTION "," FLAGS "\n\t"				\
	 ".globl   " UNDERSCORE NAME "\n\t"				\
	 ".type    " UNDERSCORE NAME ",@object\n\t"			\
	 ".p2align  2\n"						\
	 UNDERSCORE NAME ":\n\t"					\
	 ".word     -1\n\t"						\
	 ".previous")

#define INIT_SECTION(SECTION, FLAGS, NAME)				\
__asm__ (".section " SECTION "," FLAGS "\n\t"				\
	 ".globl   " UNDERSCORE NAME "\n\t"				\
	 ".type    " UNDERSCORE NAME ",@object\n\t"			\
	 ".p2align  2\n"						\
	 UNDERSCORE NAME ":\n\t"					\
	 ".previous")

/* Beginning of .ctor/.dtor sections that provides a list of constructors and
   destructors to run.  */

INIT_SECTION_NEG_ONE (".ctors", "\"aw\"", "__CTOR_LIST__");
INIT_SECTION_NEG_ONE (".dtors", "\"aw\"", "__DTOR_LIST__");

/* Beginning of .eh_frame section that provides all of the exception handling
   tables.  */

INIT_SECTION (".eh_frame", "\"aw\"", "__EH_FRAME_BEGIN__");

#if ! __FRV_FDPIC__
/* In FDPIC, the linker itself generates this.  */
/* Beginning of .rofixup section that provides a list of pointers that we
   need to adjust.  */

INIT_SECTION (".rofixup", "\"a\"", "__ROFIXUP_LIST__");
#endif /* __FRV_FDPIC__ */

extern void __frv_register_eh(void) __attribute__((__constructor__));
extern void __frv_deregister_eh(void) __attribute__((__destructor__));

extern func_ptr __EH_FRAME_BEGIN__[];

/* Register the exception handling table as the first constructor.  */
void
__frv_register_eh (void)
{
  static struct object object;
  if (__register_frame_info)
    __register_frame_info (__EH_FRAME_BEGIN__, &object);
}

/* Note, do not declare __{,de}register_frame_info weak as it seems
   to interfere with the pic support.  */

/* Unregister the exception handling table as a deconstructor.  */
void
__frv_deregister_eh (void)
{
  static int completed = 0;

  if (completed)
    return;

  if (__deregister_frame_info)
    __deregister_frame_info (__EH_FRAME_BEGIN__);

  completed = 1;
}

/* Run the global destructors.  */
void
__do_global_dtors (void)
{
  static func_ptr *p = __DTOR_LIST__ + 1;
  while (*p)
    {
      p++;
      (*(p-1)) ();
    }
}

/* Run the global constructors.  */
void
__do_global_ctors (void)
{
  unsigned long nptrs = (unsigned long) __CTOR_LIST__[0];
  unsigned i;

  if (nptrs == (unsigned long)-1)
    for (nptrs = 0; __CTOR_LIST__[nptrs + 1] != 0; nptrs++);

  for (i = nptrs; i >= 1; i--)
    __CTOR_LIST__[i] ();

  atexit (__do_global_dtors);
}

/* Subroutine called automatically by `main'.
   Compiling a global function named `main'
   produces an automatic call to this function at the beginning.

   For many systems, this routine calls __do_global_ctors.
   For systems which support a .init section we use the .init section
   to run __do_global_ctors, so we need not do anything here.  */

void
__main (void)
{
  /* Support recursive calls to `main': run initializers just once.  */
  static int initialized;
  if (! initialized)
    {
      initialized = 1;
      __do_global_ctors ();
    }
}
