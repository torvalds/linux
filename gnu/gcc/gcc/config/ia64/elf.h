/* Definitions for embedded ia64-elf target.  */

/* This macro is a C statement to print on `stderr' a string describing the
   particular machine description choice.  */

#define TARGET_VERSION fprintf (stderr, " (IA-64) ELF");

/* A C string constant that tells the GCC driver program options to pass to
   the assembler.  It can also specify how to translate options you give to GNU
   CC into options for GCC to pass to the assembler.  */

#if ((TARGET_CPU_DEFAULT | TARGET_DEFAULT) & MASK_GNU_AS) != 0
/* GNU AS.  */
#undef  ASM_EXTRA_SPEC
#define ASM_EXTRA_SPEC \
  "%{mno-gnu-as:-N so} %{!mno-gnu-as:-x}"
#else
/* Intel ias.  */
#undef  ASM_SPEC
#define ASM_SPEC \
  "%{!mgnu-as:-N so} %{mgnu-as:-x} %{mconstant-gp:-M const_gp}\
   %{mauto-pic:-M no_plabel}"
#endif

/* A C string constant that tells the GCC driver program options to pass to
   the linker.  It can also specify how to translate options you give to GCC
   into options for GCC to pass to the linker.  */

/* The Intel linker does not support dynamic linking, so we need -dn.
   The Intel linker gives annoying messages unless -N so is used.  */
#if ((TARGET_CPU_DEFAULT | TARGET_DEFAULT) & MASK_GNU_LD) != 0
/* GNU LD.  */
#define LINK_SPEC "%{mno-gnu-ld:-dn -N so}"
#else
/* Intel ild.  */
#define LINK_SPEC "%{!mgnu-ld:-dn -N so}"
#endif

/* svr4.h links with crti.o/crtn.o, but elfos.h does not.  We override elfos.h
   so that we can use the standard ELF Unix method.  */
#undef  ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s crtn.o%s"

#undef	STARTFILE_SPEC
#define STARTFILE_SPEC "%{!shared: \
			 %{!symbolic: \
			  %{pg:gcrt0.o%s}%{!pg:%{p:mcrt0.o%s}%{!p:crt0.o%s}}}}\
			crti.o%s crtbegin.o%s"

/* End of elf.h */
