/* Definitions of target machine for GNU compiler, for HPs running
   HPUX using the 64bit runtime model.
   Copyright (C) 1999, 2000, 2001, 2002, 2004, 2005 Free Software Foundation,
   Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* We can debug dynamically linked executables on hpux11; we also
   want dereferencing of a NULL pointer to cause a SEGV.  Do not move
   the "+Accept TypeMismatch" switch.  We check for it in collect2
   to determine which init/fini is needed.  */
#undef LINK_SPEC
#if ((TARGET_DEFAULT | TARGET_CPU_DEFAULT) & MASK_GNU_LD)
#define LINK_SPEC \
  "%{!shared:%{p:-L/lib/pa20_64/libp -L/usr/lib/pa20_64/libp %{!static:\
     %nWarning: consider linking with `-static' as system libraries with\n\
     %n  profiling support are only provided in archive format}}}\
   %{!shared:%{pg:-L/lib/pa20_64/libp -L/usr/lib/pa20_64/libp %{!static:\
     %nWarning: consider linking with `-static' as system libraries with\n\
     %n  profiling support are only provided in archive format}}}\
   %{mhp-ld:+Accept TypeMismatch -z} -E %{mlinker-opt:-O}\
   %{!shared:-u main %{!nostdlib:%{!nodefaultlibs:-u __cxa_finalize}}}\
   %{static:-a archive} %{shared:%{mhp-ld:-b}%{!mhp-ld:-shared}}"
#else
#define LINK_SPEC \
  "%{!shared:%{p:-L/lib/pa20_64/libp -L/usr/lib/pa20_64/libp %{!static:\
     %nWarning: consider linking with `-static' as system libraries with\n\
     %n  profiling support are only provided in archive format}}}\
   %{!shared:%{pg:-L/lib/pa20_64/libp -L/usr/lib/pa20_64/libp %{!static:\
     %nWarning: consider linking with `-static' as system libraries with\n\
     %n  profiling support are only provided in archive format}}}\
   %{!mgnu-ld:+Accept TypeMismatch -z} -E %{mlinker-opt:-O}\
   %{!shared:-u main %{!nostdlib:%{!nodefaultlibs:-u __cxa_finalize}}}\
   %{static:-a archive} %{shared:%{mgnu-ld:-shared}%{!mgnu-ld:-b}}"
#endif

/* Profiling support is only provided in libc.a.  However, libprof and
   libgprof are only available in shared form on HP-UX 11.00.  We use
   the shared form if we are using the GNU linker or an archive form
   isn't available.  We also usually need to link with libdld and it's
   only available in shared form.  */
#undef LIB_SPEC
#if ((TARGET_DEFAULT | TARGET_CPU_DEFAULT) & MASK_GNU_LD)
#define LIB_SPEC \
  "%{!shared:\
     %{!p:%{!pg: %{static|mt|pthread:-lpthread} -lc\
	    %{static:%{!nolibdld:-a shared -ldld -a archive -lc}}}}\
     %{p:%{!pg:%{static:%{!mhp-ld:-a shared}%{mhp-ld:-a archive_shared}}\
	   -lprof %{static:-a archive} %{static|mt|pthread:-lpthread} -lc\
	   %{static:%{!nolibdld:-a shared -ldld -a archive -lc}}}}\
     %{pg:%{static:%{!mhp-ld:-a shared}%{mhp-ld:-a archive_shared}}\
       -lgprof %{static:-a archive} %{static|mt|pthread:-lpthread} -lc\
       %{static:%{!nolibdld:-a shared -ldld -a archive -lc}}}}"
#else
#define LIB_SPEC \
  "%{!shared:\
     %{!p:%{!pg: %{static|mt|pthread:-lpthread} -lc\
	    %{static:%{!nolibdld:-a shared -ldld -a archive -lc}}}}\
     %{p:%{!pg:%{static:%{mgnu-ld:-a shared}%{!mgnu-ld:-a archive_shared}}\
	   -lprof %{static:-a archive} %{static|mt|pthread:-lpthread} -lc\
	   %{static:%{!nolibdld:-a shared -ldld -a archive -lc}}}}\
     %{pg:%{static:%{mgnu-ld:-a shared}%{!mgnu-ld:-a archive_shared}}\
       -lgprof %{static:-a archive} %{static|mt|pthread:-lpthread} -lc\
       %{static:%{!nolibdld:-a shared -ldld -a archive -lc}}}}"
#endif

/* The libgcc_stub.a and milli.a libraries need to come last.  */
#undef LINK_GCC_C_SEQUENCE_SPEC
#define LINK_GCC_C_SEQUENCE_SPEC "\
  %G %L %G %{!nostdlib:%{!nodefaultlibs:%{!shared:-lgcc_stub}\
  /usr/lib/pa20_64/milli.a}}"

/* Under hpux11, the normal location of the `ld' and `as' programs is the
   /usr/ccs/bin directory.  */

#ifndef CROSS_COMPILE
#undef MD_EXEC_PREFIX
#define MD_EXEC_PREFIX "/usr/ccs/bin"
#endif

/* Default prefixes.  */

#undef STANDARD_STARTFILE_PREFIX_1
#define STANDARD_STARTFILE_PREFIX_1 "/lib/pa20_64/"

#undef STANDARD_STARTFILE_PREFIX_2
#define STANDARD_STARTFILE_PREFIX_2 "/usr/lib/pa20_64/"

/* Under hpux11 the normal location of the various pa20_64 *crt*.o files
   is the /usr/ccs/lib/pa20_64 directory.  Some files may also be in the
   /opt/langtools/lib/pa20_64 directory.  */

#ifndef CROSS_COMPILE
#undef MD_STARTFILE_PREFIX
#define MD_STARTFILE_PREFIX "/usr/ccs/lib/pa20_64/"
#endif

#ifndef CROSS_COMPILE
#undef MD_STARTFILE_PREFIX_1
#define MD_STARTFILE_PREFIX_1 "/opt/langtools/lib/pa20_64/"
#endif

/* This macro specifies the biggest alignment supported by the object
   file format of this machine.

   The .align directive in the HP assembler allows alignments up to
   4096 bytes.  However, the maximum alignment of a global common symbol
   is 16 bytes using HP ld.  Unfortunately, this macro doesn't provide
   a method to check for common symbols.  */
#undef MAX_OFILE_ALIGNMENT
#define MAX_OFILE_ALIGNMENT 32768

/* Due to limitations in the target structure, it isn't currently possible
   to dynamically switch between the GNU and HP assemblers.  */
#undef TARGET_GAS

/* Configure selects the standard ELFOS defines for use with GAS.  */
#ifdef USING_ELFOS_H

/* We are using GAS.  */
#define TARGET_GAS 1

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START pa_hpux64_gas_file_start

/* This is how we output a null terminated string.  */
#undef STRING_ASM_OP
#define STRING_ASM_OP	"\t.stringz\t"

#define TEXT_SECTION_ASM_OP	"\t.text"
#define DATA_SECTION_ASM_OP	"\t.data"
#define BSS_SECTION_ASM_OP	"\t.section\t.bss"

#define JCR_SECTION_NAME	".jcr"

#define HP_INIT_ARRAY_SECTION_ASM_OP	"\t.section\t.init"
#define GNU_INIT_ARRAY_SECTION_ASM_OP	"\t.section\t.init_array"
#define HP_FINI_ARRAY_SECTION_ASM_OP	"\t.section\t.fini"
#define GNU_FINI_ARRAY_SECTION_ASM_OP	"\t.section\t.fini_array"

/* We need to override the following two macros defined in elfos.h since
   the .comm directive has a different syntax and it can't be used for
   local common symbols.  */
#undef ASM_OUTPUT_ALIGNED_COMMON
#define ASM_OUTPUT_ALIGNED_COMMON(FILE, NAME, SIZE, ALIGN)		\
  pa_asm_output_aligned_common (FILE, NAME, SIZE, ALIGN)

#undef ASM_OUTPUT_ALIGNED_LOCAL
#define ASM_OUTPUT_ALIGNED_LOCAL(FILE, NAME, SIZE, ALIGN)		\
  pa_asm_output_aligned_local (FILE, NAME, SIZE, ALIGN)

/* The define in pa.h doesn't work with the alias attribute.  The
   default is ok with the following define for GLOBAL_ASM_OP.  */
#undef TARGET_ASM_GLOBALIZE_LABEL

/* This is how we globalize a label.  */
#define GLOBAL_ASM_OP	"\t.globl\t"

/* Hacked version from defaults.h that uses assemble_name_raw
   instead of assemble_name.  A symbol in a type directive that
   isn't otherwise referenced doesn't cause the symbol to be
   placed in the symbol table of the assembled object.  */
#undef ASM_OUTPUT_TYPE_DIRECTIVE
#define ASM_OUTPUT_TYPE_DIRECTIVE(STREAM, NAME, TYPE)		\
do {								\
  fputs (TYPE_ASM_OP, STREAM);					\
  assemble_name_raw (STREAM, NAME);				\
  fputs (", ", STREAM);						\
  fprintf (STREAM, TYPE_OPERAND_FMT, TYPE);			\
  putc ('\n', STREAM);						\
} while (0)

/* Hacked version from elfos.h that doesn't output a label.  */
#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(FILE, NAME, DECL)		\
do {								\
  ASM_OUTPUT_TYPE_DIRECTIVE (FILE, NAME, "function");		\
  ASM_DECLARE_RESULT (FILE, DECL_RESULT (DECL));		\
} while (0)

/* The type of external references must be set correctly for the
   dynamic loader to work correctly.  This is equivalent to the
   HP assembler's .IMPORT directive but relates more directly to
   ELF object file types.  */
#define ASM_OUTPUT_EXTERNAL(FILE, DECL, NAME)			\
  pa_hpux_asm_output_external ((FILE), (DECL), (NAME))
#define ASM_OUTPUT_EXTERNAL_REAL(FILE, DECL, NAME)		\
do {								\
  if (FUNCTION_NAME_P (NAME))					\
    ASM_OUTPUT_TYPE_DIRECTIVE (FILE, NAME, "function");		\
  else								\
    ASM_OUTPUT_TYPE_DIRECTIVE (FILE, NAME, "object");		\
} while (0)

/* We need set the type for external libcalls.  Also note that not all
   libcall names are passed to targetm.encode_section_info (e.g., __main).
   Thus, we also have to do the section encoding if it hasn't been done
   already.  */
#undef ASM_OUTPUT_EXTERNAL_LIBCALL
#define ASM_OUTPUT_EXTERNAL_LIBCALL(FILE, FUN)			\
do {								\
  if (!FUNCTION_NAME_P (XSTR (FUN, 0)))				\
    hppa_encode_label (FUN);					\
  ASM_OUTPUT_TYPE_DIRECTIVE (FILE, XSTR (FUN, 0), "function");	\
} while (0)

/* We need to use the HP style for internal labels.  */
#undef ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(LABEL, PREFIX, NUM)	\
  sprintf (LABEL, "*%c$%s%04ld", (PREFIX)[0], (PREFIX) + 1, (long)(NUM))

#else /* USING_ELFOS_H */

/* We are not using GAS.  */
#define TARGET_GAS 0

/* HPUX 11 has the "new" HP assembler.  It's still lousy, but it's a whole
   lot better than the assembler shipped with older versions of hpux.
   However, it doesn't support weak symbols and is a bad fit with ELF.  */
#undef NEW_HP_ASSEMBLER
#define NEW_HP_ASSEMBLER 1

/* It looks like DWARF2 will be the easiest debug format to handle on this
   platform.  */
#define DWARF2_DEBUGGING_INFO 1
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

/* This target uses the ELF object file format.  */
#define OBJECT_FORMAT_ELF

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START pa_hpux64_hpas_file_start

#undef TEXT_SECTION_ASM_OP
#define TEXT_SECTION_ASM_OP		"\t.SUBSPA $CODE$\n"
#undef READONLY_DATA_SECTION_ASM_OP
#define READONLY_DATA_SECTION_ASM_OP	"\t.SUBSPA $LIT$\n"
#undef DATA_SECTION_ASM_OP
#define DATA_SECTION_ASM_OP		"\t.SUBSPA $DATA$\n"
#undef BSS_SECTION_ASM_OP
#define BSS_SECTION_ASM_OP		"\t.SUBSPA $BSS$\n"

/* We provide explicit defines for CTORS_SECTION_ASM_OP and
   DTORS_SECTION_ASM_OP since we don't yet have support for
   named sections with the HP assembler.  */
#undef CTORS_SECTION_ASM_OP
#define CTORS_SECTION_ASM_OP "\t.SUBSPA \\.ctors,QUAD=1,ALIGN=8,ACCESS=31"
#undef DTORS_SECTION_ASM_OP
#define DTORS_SECTION_ASM_OP "\t.SUBSPA \\.dtors,QUAD=1,ALIGN=8,ACCESS=31"

#define HP_INIT_ARRAY_SECTION_ASM_OP \
  "\t.SUBSPA \\.init,QUAD=1,ALIGN=8,ACCESS=31"
#define GNU_INIT_ARRAY_SECTION_ASM_OP \
  "\t.SUBSPA \\.init_array,QUAD=1,ALIGN=8,ACCESS=31"
#define HP_FINI_ARRAY_SECTION_ASM_OP \
  "\t.SUBSPA \\.fini,QUAD=1,ALIGN=8,ACCESS=31"
#define GNU_FINI_ARRAY_SECTION_ASM_OP \
  "\t.SUBSPA \\.fini_array,QUAD=1,ALIGN=8,ACCESS=31"

#endif /* USING_ELFOS_H */

/* The following defines, used to run constructors and destructors with
   the SOM linker under HP-UX 11, are not needed.  */
#undef HAS_INIT_SECTION
#undef LD_INIT_SWITCH
#undef LD_FINI_SWITCH

/* The following STARTFILE_SPEC and ENDFILE_SPEC defines provide the
   magic needed to run initializers and finalizers.  */
#undef STARTFILE_SPEC
#if TARGET_HPUX_11_11
#define STARTFILE_SPEC \
  "%{!shared: %{!symbolic: crt0%O%s} %{munix=95:unix95.o%s} \
     %{!munix=93:%{!munix=95:unix98%O%s}}} %{static:crtbeginT%O%s} \
   %{!static:%{!shared:crtbegin%O%s} %{shared:crtbeginS%O%s}}"
#else
#define STARTFILE_SPEC \
  "%{!shared: %{!symbolic: crt0%O%s} %{munix=95:unix95%O%s}} \
   %{static:crtbeginT%O%s} %{!static:%{!shared:crtbegin%O%s} \
   %{shared:crtbeginS%O%s}}"
#endif
#undef ENDFILE_SPEC
#define ENDFILE_SPEC "%{!shared:crtend%O%s} %{shared:crtendS%O%s}"

/* Since HP uses the .init and .fini sections for array initializers
   and finalizers, we need different defines for INIT_SECTION_ASM_OP
   and FINI_SECTION_ASM_OP.  With the implementation adopted below,
   the sections are not actually used.  However, we still must provide
   defines to select the proper code path.  */
#undef INIT_SECTION_ASM_OP
#define INIT_SECTION_ASM_OP
#undef FINI_SECTION_ASM_OP
#define FINI_SECTION_ASM_OP

/* We are using array initializers and don't want calls in the INIT
   and FINI sections.  */
#undef CRT_CALL_STATIC_FUNCTION
#define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC)

/* The init_priority attribute is not supported with HP ld.  This could be
   supported if collect2 was used with LD_INIT_SWITCH.  Unfortunately, this
   approach doesn't work with GNU ld since HP-UX doesn't support DT_INIT,
   and therefore the -init and -fini GNU ld switches.  */
#undef SUPPORTS_INIT_PRIORITY
#define SUPPORTS_INIT_PRIORITY (TARGET_GNU_LD ? 1 : 0)

/* We use DTOR_LIST_BEGIN to carry a bunch of hacks to allow us to use
   the init and fini array sections with both the HP and GNU linkers.
   The linkers setup the required dynamic entries in the dynamic segment
   and the dynamic linker does the calls.  This approach avoids using
   collect2.

   The first hack is to implement __do_global_ctors_aux in crtbegin as
   it needs to be the first entry in the init array so that it is called
   last.  HP got the order of the init array backwards.  The DT_INIT_ARRAY
   is supposed to be executed in the same order as the addresses appear in
   the array.  DT_FINI_ARRAY is supposed to be executed in the opposite
   order.

   The second hack is a set of plabels to implement the effect of
   CRT_CALL_STATIC_FUNCTION.  HP-UX 11 only supports DI_INIT_ARRAY and
   DT_FINI_ARRAY and they put the arrays in .init and .fini, rather than
   in .init_array and .fini_array.  The standard defines for .init and
   .fini have the execute flag set.  So, the assembler has to be hacked
   to munge the standard flags for these sections to make them agree
   with what the HP linker expects.  With the GNU linker, we need to
   used the .init_array and .fini_array sections.  So, we set up for
   both just in case.  Once we have built the table, the linker does
   the rest of the work.

   The order is significant.  Placing __do_global_ctors_aux first in
   the list, results in it being called last.  User specified initializers,
   either using the linker +init command or a plabel, run before the
   initializers specified here.  */

/* We need to add frame_dummy to the initializer list if EH_FRAME_SECTION_NAME
   or JCR_SECTION_NAME is defined.  */
#if defined(EH_FRAME_SECTION_NAME) || defined(JCR_SECTION_NAME)
#define PA_INIT_FRAME_DUMMY_ASM_OP ".dword P%frame_dummy"
#else
#define PA_INIT_FRAME_DUMMY_ASM_OP ""
#endif

/* The following hack sets up the .init, .init_array, .fini and
   .fini_array sections.  */
#define PA_CRTBEGIN_HACK \
asm (TEXT_SECTION_ASM_OP);						\
static void __attribute__((used))					\
__do_global_ctors_aux (void)						\
{									\
  func_ptr *p = __CTOR_LIST__;						\
  while (*(p + 1))							\
    p++;								\
  for (; *p != (func_ptr) -1; p--)					\
    (*p) ();								\
}									\
									\
asm (HP_INIT_ARRAY_SECTION_ASM_OP);					\
asm (".align 8");							\
asm (".dword P%__do_global_ctors_aux");					\
asm (PA_INIT_FRAME_DUMMY_ASM_OP);					\
asm (GNU_INIT_ARRAY_SECTION_ASM_OP);					\
asm (".align 8");							\
asm (".dword P%__do_global_ctors_aux");					\
asm (PA_INIT_FRAME_DUMMY_ASM_OP);					\
asm (HP_FINI_ARRAY_SECTION_ASM_OP);					\
asm (".align 8");							\
asm (".dword P%__do_global_dtors_aux");					\
asm (GNU_FINI_ARRAY_SECTION_ASM_OP);					\
asm (".align 8");							\
asm (".dword P%__do_global_dtors_aux")

/* The following two variants of DTOR_LIST_BEGIN are identical to those
   in crtstuff.c except for the addition of the above crtbegin hack.  */
#ifdef DTORS_SECTION_ASM_OP
#define DTOR_LIST_BEGIN \
asm (DTORS_SECTION_ASM_OP);						\
STATIC func_ptr __DTOR_LIST__[1]					\
  __attribute__ ((aligned(sizeof(func_ptr))))				\
  = { (func_ptr) (-1) };						\
PA_CRTBEGIN_HACK
#else
#define DTOR_LIST_BEGIN \
STATIC func_ptr __DTOR_LIST__[1]					\
  __attribute__ ((section(".dtors"), aligned(sizeof(func_ptr))))	\
  = { (func_ptr) (-1) };						\
PA_CRTBEGIN_HACK
#endif

/* If using HP ld do not call pxdb.  Use size as a program that does nothing
   and returns 0.  /bin/true cannot be used because it is a script without
   an interpreter.  */
#define INIT_ENVIRONMENT "LD_PXDB=/usr/ccs/bin/size"

/* The HPUX dynamic linker objects to undefined weak symbols, so do
   not use them in gthr-posix.h.  */
#define GTHREAD_USE_WEAK 0

/* We don't want undefined weak references to __register_frame_info,
   __deregister_frame_info, _Jv_RegisterClasses and __cxa_finalize
   introduced by crtbegin.o.  The GNU linker only resolves weak
   references if they appear in a shared library.  Thus, it would be
   impossible to create a static executable if the symbols were weak.
   So, the best solution seems to be to make the symbols strong and
   provide an archive library of empty stub functions.  */
#define TARGET_ATTRIBUTE_WEAK
