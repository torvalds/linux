/* Specialized bits of code needed to support construction and
   destruction of file-scope objects in C++ code.
   Copyright (C) 1991, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Ron Guilmette (rfg@monkeys.com).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* This file is a bit like libgcc2.c in that it is compiled
   multiple times and yields multiple .o files.

   This file is useful on target machines where the object file format
   supports multiple "user-defined" sections (e.g. COFF, ELF, ROSE).  On
   such systems, this file allows us to avoid running collect (or any
   other such slow and painful kludge).  Additionally, if the target
   system supports a .init section, this file allows us to support the
   linking of C++ code with a non-C++ main program.

   Note that if INIT_SECTION_ASM_OP is defined in the tm.h file, then
   this file *will* make use of the .init section.  If that symbol is
   not defined however, then the .init section will not be used.

   Currently, only ELF and COFF are supported.  It is likely however that
   ROSE could also be supported, if someone was willing to do the work to
   make whatever (small?) adaptations are needed.  (Some work may be
   needed on the ROSE assembler and linker also.)

   This file must be compiled with gcc.  */

/* Target machine header files require this define. */
#define IN_LIBGCC2

/* FIXME: Including auto-host is incorrect, but until we have
   identified the set of defines that need to go into auto-target.h,
   this will have to do.  */
#include "auto-host.h"
#undef gid_t
#undef pid_t
#undef rlim_t
#undef ssize_t
#undef uid_t
#undef vfork
#include "tconfig.h"
#include "tsystem.h"
#include "coretypes.h"
#include "tm.h"
#include "unwind-dw2-fde.h"

#ifndef FORCE_CODE_SECTION_ALIGN
# define FORCE_CODE_SECTION_ALIGN
#endif

#ifndef CRT_CALL_STATIC_FUNCTION
# define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC)	\
static void __attribute__((__used__))			\
call_ ## FUNC (void)					\
{							\
  asm (SECTION_OP);					\
  FUNC ();						\
  FORCE_CODE_SECTION_ALIGN				\
  asm (TEXT_SECTION_ASM_OP);				\
}
#endif

#if defined(OBJECT_FORMAT_ELF) && defined(HAVE_LD_EH_FRAME_HDR) \
    && !defined(inhibit_libc) && !defined(CRTSTUFFT_O) \
    && defined(__GLIBC__) && __GLIBC__ >= 2
#include <link.h>
# if (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2) \
     || (__GLIBC__ == 2 && __GLIBC_MINOR__ == 2 && defined(DT_CONFIG)))
#  define USE_PT_GNU_EH_FRAME
# endif
#endif
#if defined(EH_FRAME_SECTION_NAME) && !defined(USE_PT_GNU_EH_FRAME)
# define USE_EH_FRAME_REGISTRY
#endif
#if defined(EH_FRAME_SECTION_NAME) && EH_TABLES_CAN_BE_READ_ONLY
# define EH_FRAME_SECTION_CONST const
#else
# define EH_FRAME_SECTION_CONST
#endif

/* We do not want to add the weak attribute to the declarations of these
   routines in unwind-dw2-fde.h because that will cause the definition of
   these symbols to be weak as well.

   This exposes a core issue, how to handle creating weak references vs
   how to create weak definitions.  Either we have to have the definition
   of TARGET_WEAK_ATTRIBUTE be conditional in the shared header files or
   have a second declaration if we want a function's references to be weak,
   but not its definition.

   Making TARGET_WEAK_ATTRIBUTE conditional seems like a good solution until
   one thinks about scaling to larger problems -- i.e., the condition under
   which TARGET_WEAK_ATTRIBUTE is active will eventually get far too
   complicated.

   So, we take an approach similar to #pragma weak -- we have a second
   declaration for functions that we want to have weak references.

   Neither way is particularly good.  */
   
/* References to __register_frame_info and __deregister_frame_info should
   be weak in this file if at all possible.  */
extern void __register_frame_info (const void *, struct object *)
				  TARGET_ATTRIBUTE_WEAK;
extern void __register_frame_info_bases (const void *, struct object *,
					 void *, void *)
				  TARGET_ATTRIBUTE_WEAK;
extern void *__deregister_frame_info (const void *)
				     TARGET_ATTRIBUTE_WEAK;
extern void *__deregister_frame_info_bases (const void *)
				     TARGET_ATTRIBUTE_WEAK;
extern void __do_global_ctors_1 (void);

/* Likewise for _Jv_RegisterClasses.  */
extern void _Jv_RegisterClasses (void *) TARGET_ATTRIBUTE_WEAK;

#ifdef OBJECT_FORMAT_ELF

/*  Declare a pointer to void function type.  */
typedef void (*func_ptr) (void);
#define STATIC static

#else  /* OBJECT_FORMAT_ELF */

#include "gbl-ctors.h"

#define STATIC

#endif /* OBJECT_FORMAT_ELF */

#ifdef CRT_BEGIN

/* NOTE:  In order to be able to support SVR4 shared libraries, we arrange
   to have one set of symbols { __CTOR_LIST__, __DTOR_LIST__, __CTOR_END__,
   __DTOR_END__ } per root executable and also one set of these symbols
   per shared library.  So in any given whole process image, we may have
   multiple definitions of each of these symbols.  In order to prevent
   these definitions from conflicting with one another, and in order to
   ensure that the proper lists are used for the initialization/finalization
   of each individual shared library (respectively), we give these symbols
   only internal (i.e. `static') linkage, and we also make it a point to
   refer to only the __CTOR_END__ symbol in crtend.o and the __DTOR_LIST__
   symbol in crtbegin.o, where they are defined.  */

/* The -1 is a flag to __do_global_[cd]tors indicating that this table
   does not start with a count of elements.  */
#ifdef CTOR_LIST_BEGIN
CTOR_LIST_BEGIN;
#elif defined(CTORS_SECTION_ASM_OP)
/* Hack: force cc1 to switch to .data section early, so that assembling
   __CTOR_LIST__ does not undo our behind-the-back change to .ctors.  */
static func_ptr force_to_data[1] __attribute__ ((__unused__)) = { };
asm (CTORS_SECTION_ASM_OP);
STATIC func_ptr __CTOR_LIST__[1]
  __attribute__ ((__unused__, aligned(sizeof(func_ptr))))
  = { (func_ptr) (-1) };
#else
STATIC func_ptr __CTOR_LIST__[1]
  __attribute__ ((__unused__, section(".ctors"), aligned(sizeof(func_ptr))))
  = { (func_ptr) (-1) };
#endif /* __CTOR_LIST__ alternatives */

#ifdef DTOR_LIST_BEGIN
DTOR_LIST_BEGIN;
#elif defined(DTORS_SECTION_ASM_OP)
asm (DTORS_SECTION_ASM_OP);
STATIC func_ptr __DTOR_LIST__[1]
  __attribute__ ((aligned(sizeof(func_ptr))))
  = { (func_ptr) (-1) };
#else
STATIC func_ptr __DTOR_LIST__[1]
  __attribute__((section(".dtors"), aligned(sizeof(func_ptr))))
  = { (func_ptr) (-1) };
#endif /* __DTOR_LIST__ alternatives */

#ifdef USE_EH_FRAME_REGISTRY
/* Stick a label at the beginning of the frame unwind info so we can register
   and deregister it with the exception handling library code.  */
STATIC EH_FRAME_SECTION_CONST char __EH_FRAME_BEGIN__[]
     __attribute__((section(EH_FRAME_SECTION_NAME), aligned(4)))
     = { };
#endif /* USE_EH_FRAME_REGISTRY */

#ifdef JCR_SECTION_NAME
/* Stick a label at the beginning of the java class registration info
   so we can register them properly.  */
STATIC void *__JCR_LIST__[]
  __attribute__ ((unused, section(JCR_SECTION_NAME), aligned(sizeof(void*))))
  = { };
#endif /* JCR_SECTION_NAME */

#if defined(INIT_SECTION_ASM_OP) || defined(INIT_ARRAY_SECTION_ASM_OP)

#ifdef OBJECT_FORMAT_ELF

/* Declare the __dso_handle variable.  It should have a unique value
   in every shared-object; in a main program its value is zero.  The
   object should in any case be protected.  This means the instance
   in one DSO or the main program is not used in another object.  The
   dynamic linker takes care of this.  */

#ifdef TARGET_LIBGCC_SDATA_SECTION
extern void *__dso_handle __attribute__ ((__section__ (TARGET_LIBGCC_SDATA_SECTION)));
#endif
#ifdef HAVE_GAS_HIDDEN
extern void *__dso_handle __attribute__ ((__visibility__ ("hidden")));
#endif
#ifdef CRTSTUFFS_O
void *__dso_handle = &__dso_handle;
#else
void *__dso_handle = 0;
#endif

/* The __cxa_finalize function may not be available so we use only a
   weak declaration.  */
extern void __cxa_finalize (void *) TARGET_ATTRIBUTE_WEAK;

/* Run all the global destructors on exit from the program.  */
 
/* Some systems place the number of pointers in the first word of the
   table.  On SVR4 however, that word is -1.  In all cases, the table is
   null-terminated.  On SVR4, we start from the beginning of the list and
   invoke each per-compilation-unit destructor routine in order
   until we find that null.

   Note that this function MUST be static.  There will be one of these
   functions in each root executable and one in each shared library, but
   although they all have the same code, each one is unique in that it
   refers to one particular associated `__DTOR_LIST__' which belongs to the
   same particular root executable or shared library file.

   On some systems, this routine is run more than once from the .fini,
   when exit is called recursively, so we arrange to remember where in
   the list we left off processing, and we resume at that point,
   should we be re-invoked.  */

static void __attribute__((used))
__do_global_dtors_aux (void)
{
#ifndef FINI_ARRAY_SECTION_ASM_OP
  static func_ptr *p = __DTOR_LIST__ + 1;
  func_ptr f;
#endif /* !defined(FINI_ARRAY_SECTION_ASM_OP)  */
  static _Bool completed;

  if (__builtin_expect (completed, 0))
    return;

#ifdef CRTSTUFFS_O
  if (__cxa_finalize)
    __cxa_finalize (__dso_handle);
#endif

#ifdef FINI_ARRAY_SECTION_ASM_OP
  /* If we are using .fini_array then destructors will be run via that
     mechanism.  */
#else /* !defined (FINI_ARRAY_SECTION_ASM_OP) */
  while ((f = *p))
    {
      p++;
      f ();
    }
#endif /* !defined(FINI_ARRAY_SECTION_ASM_OP) */

#ifdef USE_EH_FRAME_REGISTRY
#ifdef CRT_GET_RFIB_DATA
  /* If we used the new __register_frame_info_bases interface,
     make sure that we deregister from the same place.  */
  if (__deregister_frame_info_bases)
    __deregister_frame_info_bases (__EH_FRAME_BEGIN__);
#else
  if (__deregister_frame_info)
    __deregister_frame_info (__EH_FRAME_BEGIN__);
#endif
#endif

  completed = 1;
}

/* Stick a call to __do_global_dtors_aux into the .fini section.  */
#ifdef FINI_SECTION_ASM_OP
CRT_CALL_STATIC_FUNCTION (FINI_SECTION_ASM_OP, __do_global_dtors_aux)
#else /* !defined(FINI_SECTION_ASM_OP) */
static func_ptr __do_global_dtors_aux_fini_array_entry[]
  __attribute__ ((__unused__, section(".fini_array")))
  = { __do_global_dtors_aux };
#endif /* !defined(FINI_SECTION_ASM_OP) */

#if defined(USE_EH_FRAME_REGISTRY) || defined(JCR_SECTION_NAME)
/* Stick a call to __register_frame_info into the .init section.  For some
   reason calls with no arguments work more reliably in .init, so stick the
   call in another function.  */

static void __attribute__((used))
frame_dummy (void)
{
#ifdef USE_EH_FRAME_REGISTRY
  static struct object object;
#ifdef CRT_GET_RFIB_DATA
  void *tbase, *dbase;
  tbase = 0;
  CRT_GET_RFIB_DATA (dbase);
  if (__register_frame_info_bases)
    __register_frame_info_bases (__EH_FRAME_BEGIN__, &object, tbase, dbase);
#else
  if (__register_frame_info)
    __register_frame_info (__EH_FRAME_BEGIN__, &object);
#endif /* CRT_GET_RFIB_DATA */
#endif /* USE_EH_FRAME_REGISTRY */
#ifdef JCR_SECTION_NAME
  if (__JCR_LIST__[0])
    {
      void (*register_classes) (void *) = _Jv_RegisterClasses;
      __asm ("" : "+r" (register_classes));
      if (register_classes)
	register_classes (__JCR_LIST__);
    }
#endif /* JCR_SECTION_NAME */
}

#ifdef INIT_SECTION_ASM_OP
CRT_CALL_STATIC_FUNCTION (INIT_SECTION_ASM_OP, frame_dummy)
#else /* defined(INIT_SECTION_ASM_OP) */
static func_ptr __frame_dummy_init_array_entry[]
  __attribute__ ((__unused__, section(".init_array")))
  = { frame_dummy };
#endif /* !defined(INIT_SECTION_ASM_OP) */
#endif /* USE_EH_FRAME_REGISTRY || JCR_SECTION_NAME */

#else  /* OBJECT_FORMAT_ELF */

/* The function __do_global_ctors_aux is compiled twice (once in crtbegin.o
   and once in crtend.o).  It must be declared static to avoid a link
   error.  Here, we define __do_global_ctors as an externally callable
   function.  It is externally callable so that __main can invoke it when
   INVOKE__main is defined.  This has the additional effect of forcing cc1
   to switch to the .text section.  */

static void __do_global_ctors_aux (void);
void
__do_global_ctors (void)
{
#ifdef INVOKE__main
  /* If __main won't actually call __do_global_ctors then it doesn't matter
     what's inside the function.  The inside of __do_global_ctors_aux is
     called automatically in that case.  And the Alliant fx2800 linker
     crashes on this reference.  So prevent the crash.  */
  __do_global_ctors_aux ();
#endif
}

asm (INIT_SECTION_ASM_OP);	/* cc1 doesn't know that we are switching! */

/* A routine to invoke all of the global constructors upon entry to the
   program.  We put this into the .init section (for systems that have
   such a thing) so that we can properly perform the construction of
   file-scope static-storage C++ objects within shared libraries.  */

static void __attribute__((used))
__do_global_ctors_aux (void)	/* prologue goes in .init section */
{
  FORCE_CODE_SECTION_ALIGN	/* explicit align before switch to .text */
  asm (TEXT_SECTION_ASM_OP);	/* don't put epilogue and body in .init */
  DO_GLOBAL_CTORS_BODY;
  atexit (__do_global_dtors);
}

#endif /* OBJECT_FORMAT_ELF */

#elif defined(HAS_INIT_SECTION) /* ! INIT_SECTION_ASM_OP */

extern void __do_global_dtors (void);

/* This case is used by the Irix 6 port, which supports named sections but
   not an SVR4-style .fini section.  __do_global_dtors can be non-static
   in this case because we protect it with -hidden_symbol.  */

void
__do_global_dtors (void)
{
  func_ptr *p, f;
  for (p = __DTOR_LIST__ + 1; (f = *p); p++)
    f ();

#ifdef USE_EH_FRAME_REGISTRY
  if (__deregister_frame_info)
    __deregister_frame_info (__EH_FRAME_BEGIN__);
#endif
}

#if defined(USE_EH_FRAME_REGISTRY) || defined(JCR_SECTION_NAME)
/* A helper function for __do_global_ctors, which is in crtend.o.  Here
   in crtbegin.o, we can reference a couple of symbols not visible there.
   Plus, since we're before libgcc.a, we have no problems referencing
   functions from there.  */
void
__do_global_ctors_1(void)
{
#ifdef USE_EH_FRAME_REGISTRY
  static struct object object;
  if (__register_frame_info)
    __register_frame_info (__EH_FRAME_BEGIN__, &object);
#endif
#ifdef JCR_SECTION_NAME
  if (__JCR_LIST__[0])
    {
      void (*register_classes) (void *) = _Jv_RegisterClasses;
      __asm ("" : "+r" (register_classes));
      if (register_classes)
	register_classes (__JCR_LIST__);
    }
#endif
}
#endif /* USE_EH_FRAME_REGISTRY || JCR_SECTION_NAME */

#else /* ! INIT_SECTION_ASM_OP && ! HAS_INIT_SECTION */
#error "What are you doing with crtstuff.c, then?"
#endif

#elif defined(CRT_END) /* ! CRT_BEGIN */

/* Put a word containing zero at the end of each of our two lists of function
   addresses.  Note that the words defined here go into the .ctors and .dtors
   sections of the crtend.o file, and since that file is always linked in
   last, these words naturally end up at the very ends of the two lists
   contained in these two sections.  */

#ifdef CTOR_LIST_END
CTOR_LIST_END;
#elif defined(CTORS_SECTION_ASM_OP)
/* Hack: force cc1 to switch to .data section early, so that assembling
   __CTOR_LIST__ does not undo our behind-the-back change to .ctors.  */
static func_ptr force_to_data[1] __attribute__ ((__unused__)) = { };
asm (CTORS_SECTION_ASM_OP);
STATIC func_ptr __CTOR_END__[1]
  __attribute__((aligned(sizeof(func_ptr))))
  = { (func_ptr) 0 };
#else
STATIC func_ptr __CTOR_END__[1]
  __attribute__((section(".ctors"), aligned(sizeof(func_ptr))))
  = { (func_ptr) 0 };
#endif

#ifdef DTOR_LIST_END
DTOR_LIST_END;
#elif defined(DTORS_SECTION_ASM_OP)
asm (DTORS_SECTION_ASM_OP);
STATIC func_ptr __DTOR_END__[1]
  __attribute__ ((unused, aligned(sizeof(func_ptr))))
  = { (func_ptr) 0 };
#else
STATIC func_ptr __DTOR_END__[1]
  __attribute__((unused, section(".dtors"), aligned(sizeof(func_ptr))))
  = { (func_ptr) 0 };
#endif

#ifdef EH_FRAME_SECTION_NAME
/* Terminate the frame unwind info section with a 4byte 0 as a sentinel;
   this would be the 'length' field in a real FDE.  */
# if __INT_MAX__ == 2147483647
typedef int int32;
# elif __LONG_MAX__ == 2147483647
typedef long int32;
# elif __SHRT_MAX__ == 2147483647
typedef short int32;
# else
#  error "Missing a 4 byte integer"
# endif
STATIC EH_FRAME_SECTION_CONST int32 __FRAME_END__[]
     __attribute__ ((unused, section(EH_FRAME_SECTION_NAME),
		     aligned(sizeof(int32))))
     = { 0 };
#endif /* EH_FRAME_SECTION_NAME */

#ifdef JCR_SECTION_NAME
/* Null terminate the .jcr section array.  */
STATIC void *__JCR_END__[1] 
   __attribute__ ((unused, section(JCR_SECTION_NAME),
		   aligned(sizeof(void *))))
   = { 0 };
#endif /* JCR_SECTION_NAME */

#ifdef INIT_ARRAY_SECTION_ASM_OP

/* If we are using .init_array, there is nothing to do.  */

#elif defined(INIT_SECTION_ASM_OP)

#ifdef OBJECT_FORMAT_ELF
static void __attribute__((used))
__do_global_ctors_aux (void)
{
  func_ptr *p;
  for (p = __CTOR_END__ - 1; *p != (func_ptr) -1; p--)
    (*p) ();
}

/* Stick a call to __do_global_ctors_aux into the .init section.  */
CRT_CALL_STATIC_FUNCTION (INIT_SECTION_ASM_OP, __do_global_ctors_aux)
#else  /* OBJECT_FORMAT_ELF */

/* Stick the real initialization code, followed by a normal sort of
   function epilogue at the very end of the .init section for this
   entire root executable file or for this entire shared library file.

   Note that we use some tricks here to get *just* the body and just
   a function epilogue (but no function prologue) into the .init
   section of the crtend.o file.  Specifically, we switch to the .text
   section, start to define a function, and then we switch to the .init
   section just before the body code.

   Earlier on, we put the corresponding function prologue into the .init
   section of the crtbegin.o file (which will be linked in first).

   Note that we want to invoke all constructors for C++ file-scope static-
   storage objects AFTER any other possible initialization actions which
   may be performed by the code in the .init section contributions made by
   other libraries, etc.  That's because those other initializations may
   include setup operations for very primitive things (e.g. initializing
   the state of the floating-point coprocessor, etc.) which should be done
   before we start to execute any of the user's code.  */

static void
__do_global_ctors_aux (void)	/* prologue goes in .text section */
{
  asm (INIT_SECTION_ASM_OP);
  DO_GLOBAL_CTORS_BODY;
  atexit (__do_global_dtors);
}				/* epilogue and body go in .init section */

FORCE_CODE_SECTION_ALIGN
asm (TEXT_SECTION_ASM_OP);

#endif /* OBJECT_FORMAT_ELF */

#elif defined(HAS_INIT_SECTION) /* ! INIT_SECTION_ASM_OP */

extern void __do_global_ctors (void);

/* This case is used by the Irix 6 port, which supports named sections but
   not an SVR4-style .init section.  __do_global_ctors can be non-static
   in this case because we protect it with -hidden_symbol.  */
void
__do_global_ctors (void)
{
  func_ptr *p;
#if defined(USE_EH_FRAME_REGISTRY) || defined(JCR_SECTION_NAME)
  __do_global_ctors_1();
#endif
  for (p = __CTOR_END__ - 1; *p != (func_ptr) -1; p--)
    (*p) ();
}

#else /* ! INIT_SECTION_ASM_OP && ! HAS_INIT_SECTION */
#error "What are you doing with crtstuff.c, then?"
#endif

#else /* ! CRT_BEGIN && ! CRT_END */
#error "One of CRT_BEGIN or CRT_END must be defined."
#endif
