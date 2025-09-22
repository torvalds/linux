/* Override definitions in elfos.h/svr4.h to be correct for IA64.  */

#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS ia64_sysv4_init_libfuncs

/* We want DWARF2 as specified by the IA64 ABI.  */
#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

/* Stabs does not work properly for 64-bit targets.  */
#undef DBX_DEBUGGING_INFO

/* Various pseudo-ops for which the Intel assembler uses non-standard
   definitions.  */

#undef STRING_ASM_OP
#define STRING_ASM_OP "\tstringz\t"

#undef SKIP_ASM_OP
#define SKIP_ASM_OP "\t.skip\t"

#undef COMMON_ASM_OP
#define COMMON_ASM_OP "\t.common\t"

#undef ASCII_DATA_ASM_OP
#define ASCII_DATA_ASM_OP "\tstring\t"

/* ia64-specific options for gas
   ??? ia64 gas doesn't accept standard svr4 assembler options?  */
#undef ASM_SPEC
#define ASM_SPEC "-x %{mconstant-gp} %{mauto-pic} %(asm_extra)"

/* ??? Unfortunately, .lcomm doesn't work, because it puts things in either
   .bss or .sbss, and we can't control the decision of which is used.  When
   I use .lcomm, I get a cryptic "Section group has no member" error from
   the Intel simulator.  So we must explicitly put variables in .bss
   instead.  This matters only if we care about the Intel assembler.  */

/* This is asm_output_aligned_bss from varasm.c without the
   (*targetm.asm_out.globalize_label) call at the beginning.  */

/* This is for final.c, because it is used by ASM_DECLARE_OBJECT_NAME.  */
extern int size_directive_output;

#undef ASM_OUTPUT_ALIGNED_LOCAL
#define ASM_OUTPUT_ALIGNED_DECL_LOCAL(FILE, DECL, NAME, SIZE, ALIGN) \
do {									\
  if ((DECL) && sdata_symbolic_operand (XEXP (DECL_RTL (DECL), 0), Pmode)) \
    switch_to_section (sbss_section);					\
  else									\
    switch_to_section (bss_section);					\
  ASM_OUTPUT_ALIGN (FILE, floor_log2 ((ALIGN) / BITS_PER_UNIT));	\
  ASM_DECLARE_OBJECT_NAME (FILE, NAME, DECL);				\
  ASM_OUTPUT_SKIP (FILE, SIZE ? SIZE : 1);				\
} while (0)

/* The # tells the Intel assembler that this is not a register name.
   However, we can't emit the # in a label definition, so we set a variable
   in ASM_OUTPUT_LABEL to control whether we want the postfix here or not.
   We append the # to the label name, but since NAME can be an expression
   we have to scan it for a non-label character and insert the # there.  */

#undef ASM_OUTPUT_LABELREF
#define ASM_OUTPUT_LABELREF(STREAM, NAME)	\
do {						\
  const char *name_ = NAME;			\
  if (*name_ == '*')				\
    name_++;					\
  else						\
    fputs (user_label_prefix, STREAM);		\
  fputs (name_, STREAM);			\
  if (!ia64_asm_output_label)			\
    fputc ('#', STREAM);			\
} while (0)

/* Intel assembler requires both flags and type if declaring a non-predefined
   section.  */
#undef INIT_SECTION_ASM_OP
#define INIT_SECTION_ASM_OP	"\t.section\t.init,\"ax\",\"progbits\""
#undef FINI_SECTION_ASM_OP
#define FINI_SECTION_ASM_OP	"\t.section\t.fini,\"ax\",\"progbits\""

/* svr4.h undefines this, so we need to define it here.  */
#define DBX_REGISTER_NUMBER(REGNO) \
  ia64_dbx_register_number(REGNO)

/* Things that svr4.h defines to the wrong type, because it assumes 32 bit
   ints and 32 bit longs.  */

#undef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

/* We redefine this to use the ia64 .proc pseudo-op.  */

#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(FILE, NAME, DECL) \
do {									\
  fputs ("\t.proc ", FILE);						\
  assemble_name (FILE, NAME);						\
  fputc ('\n', FILE);							\
  ASM_OUTPUT_LABEL (FILE, NAME);					\
} while (0)

/* We redefine this to use the ia64 .endp pseudo-op.  */

#undef ASM_DECLARE_FUNCTION_SIZE
#define ASM_DECLARE_FUNCTION_SIZE(FILE, NAME, DECL) \
do {									\
  fputs ("\t.endp ", FILE);						\
  assemble_name (FILE, NAME);						\
  fputc ('\n', FILE);							\
} while (0)

/* Override default elf definition.  */
#undef  TARGET_ASM_RELOC_RW_MASK
#define TARGET_ASM_RELOC_RW_MASK  ia64_reloc_rw_mask
#undef	TARGET_ASM_SELECT_RTX_SECTION
#define TARGET_ASM_SELECT_RTX_SECTION  ia64_select_rtx_section

#define SDATA_SECTION_ASM_OP "\t.sdata"
#define SBSS_SECTION_ASM_OP "\t.sbss"
