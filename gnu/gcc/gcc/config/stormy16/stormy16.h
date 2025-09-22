/* Xstormy16 cpu description.
   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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


/* Driver configuration */

/* Defined in svr4.h.  */
#undef ASM_SPEC
#define ASM_SPEC ""

/* For xstormy16:
   - If -msim is specified, everything is built and linked as for the sim.
   - If -T is specified, that linker script is used, and it should provide
     appropriate libraries.
   - If neither is specified, everything is built as for the sim, but no
     I/O support is assumed.

*/
#undef LIB_SPEC
#define LIB_SPEC "-( -lc %{msim:-lsim}%{!msim:%{!T*:-lnosys}} -)"

/* Defined in svr4.h.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "crt0.o%s crti.o%s crtbegin.o%s"

/* Defined in svr4.h.  */
#undef ENDFILE_SPEC
#define ENDFILE_SPEC "crtend.o%s crtn.o%s"

/* Defined in svr4.h for host compilers.  */
/* #define MD_EXEC_PREFIX "" */

/* Defined in svr4.h for host compilers.  */
/* #define MD_STARTFILE_PREFIX "" */


/* Run-time target specifications */

#define TARGET_CPU_CPP_BUILTINS() do {	\
  builtin_define_std ("xstormy16");	\
  builtin_assert ("machine=xstormy16");	\
  builtin_assert ("cpu=xstormy16");     \
} while (0)

#define TARGET_VERSION fprintf (stderr, " (xstormy16 cpu core)");

#define CAN_DEBUG_WITHOUT_FP


/* Storage Layout */

#define BITS_BIG_ENDIAN 1

#define BYTES_BIG_ENDIAN 0

#define WORDS_BIG_ENDIAN 0

#define UNITS_PER_WORD 2

#define PROMOTE_MODE(MODE,UNSIGNEDP,TYPE)				\
do {									\
  if (GET_MODE_CLASS (MODE) == MODE_INT					\
      && GET_MODE_SIZE (MODE) < 2)					\
    (MODE) = HImode;							\
} while (0)

#define PARM_BOUNDARY 16

#define STACK_BOUNDARY 16

#define FUNCTION_BOUNDARY 16

#define BIGGEST_ALIGNMENT 16

/* Defined in svr4.h.  */
/* #define MAX_OFILE_ALIGNMENT */

#define DATA_ALIGNMENT(TYPE, ALIGN)		\
  (TREE_CODE (TYPE) == ARRAY_TYPE		\
   && TYPE_MODE (TREE_TYPE (TYPE)) == QImode	\
   && (ALIGN) < BITS_PER_WORD ? BITS_PER_WORD : (ALIGN))

#define CONSTANT_ALIGNMENT(EXP, ALIGN)  \
  (TREE_CODE (EXP) == STRING_CST	\
   && (ALIGN) < BITS_PER_WORD ? BITS_PER_WORD : (ALIGN))

#define STRICT_ALIGNMENT 1

/* Defined in svr4.h.  */
#define PCC_BITFIELD_TYPE_MATTERS 1

/* Layout of Source Language Data Types */

#define INT_TYPE_SIZE 16

#define SHORT_TYPE_SIZE 16

#define LONG_TYPE_SIZE 32

#define LONG_LONG_TYPE_SIZE 64

#define FLOAT_TYPE_SIZE 32

#define DOUBLE_TYPE_SIZE 64

#define LONG_DOUBLE_TYPE_SIZE 64

#define DEFAULT_SIGNED_CHAR 0

/* Defined in svr4.h.  */
#define SIZE_TYPE "unsigned int"

/* Defined in svr4.h.  */
#define PTRDIFF_TYPE "int"

/* Defined in svr4.h, to "long int".  */
/* #define WCHAR_TYPE "long int" */

/* Defined in svr4.h.  */
#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

/* Define this macro if the type of Objective-C selectors should be `int'.

   If this macro is not defined, then selectors should have the type `struct
   objc_selector *'.  */
/* #define OBJC_INT_SELECTORS */


/* Register Basics */

#define FIRST_PSEUDO_REGISTER 19

#define FIXED_REGISTERS \
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1 }

#define CALL_USED_REGISTERS \
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1 }


/* Order of allocation of registers */

#define REG_ALLOC_ORDER { 7, 6, 5, 4, 3, 2, 1, 0, 9, 8, 10, 11, 12, 13, 14, 15, 16 }


/* How Values Fit in Registers */

#define HARD_REGNO_NREGS(REGNO, MODE) 				\
  ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

#define HARD_REGNO_MODE_OK(REGNO, MODE) ((REGNO) != 16 || (MODE) == BImode)

/* A C expression that is nonzero if it is desirable to choose register
   allocation so as to avoid move instructions between a value of mode MODE1
   and a value of mode MODE2.

   If `HARD_REGNO_MODE_OK (R, MODE1)' and `HARD_REGNO_MODE_OK (R, MODE2)' are
   ever different for any R, then `MODES_TIEABLE_P (MODE1, MODE2)' must be
   zero.  */
#define MODES_TIEABLE_P(MODE1, MODE2) ((MODE1) != BImode && (MODE2) != BImode)


/* Register Classes */

enum reg_class
{
  NO_REGS,
  R0_REGS,
  R1_REGS,
  TWO_REGS,
  R2_REGS,
  EIGHT_REGS,
  R8_REGS,
  ICALL_REGS,
  GENERAL_REGS,
  CARRY_REGS,
  ALL_REGS,
  LIM_REG_CLASSES
};

#define N_REG_CLASSES ((int) LIM_REG_CLASSES)

#define REG_CLASS_NAMES				\
{						\
  "NO_REGS", 					\
  "R0_REGS", 					\
  "R1_REGS",					\
  "TWO_REGS",					\
  "R2_REGS",					\
  "EIGHT_REGS",					\
  "R8_REGS",					\
  "ICALL_REGS",					\
  "GENERAL_REGS",				\
  "CARRY_REGS",					\
  "ALL_REGS"					\
}

#define REG_CLASS_CONTENTS			\
{						\
  { 0x00000 },					\
  { 0x00001 },					\
  { 0x00002 },					\
  { 0x00003 },					\
  { 0x00004 },					\
  { 0x000FF },					\
  { 0x00100 },					\
  { 0x00300 },					\
  { 0x6FFFF },					\
  { 0x10000 },					\
  { (1 << FIRST_PSEUDO_REGISTER) - 1 }		\
}

#define REGNO_REG_CLASS(REGNO) 			\
  ((REGNO) == 0   ? R0_REGS			\
   : (REGNO) == 1 ? R1_REGS			\
   : (REGNO) == 2 ? R2_REGS			\
   : (REGNO) < 8  ? EIGHT_REGS			\
   : (REGNO) == 8 ? R8_REGS			\
   : (REGNO) == 16 ? CARRY_REGS			\
   : (REGNO) <= 18 ? GENERAL_REGS		\
   : ALL_REGS)

#define BASE_REG_CLASS GENERAL_REGS

#define INDEX_REG_CLASS GENERAL_REGS

/*   The following letters are unavailable, due to being used as
   constraints:
	'0'..'9'
	'<', '>'
	'E', 'F', 'G', 'H'
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P'
	'Q', 'R', 'S', 'T', 'U'
	'V', 'X'
	'g', 'i', 'm', 'n', 'o', 'p', 'r', 's' */

#define REG_CLASS_FROM_LETTER(CHAR)		\
 (  (CHAR) == 'a' ? R0_REGS			\
  : (CHAR) == 'b' ? R1_REGS			\
  : (CHAR) == 'c' ? R2_REGS			\
  : (CHAR) == 'd' ? R8_REGS			\
  : (CHAR) == 'e' ? EIGHT_REGS			\
  : (CHAR) == 't' ? TWO_REGS			\
  : (CHAR) == 'y' ? CARRY_REGS			\
  : (CHAR) == 'z' ? ICALL_REGS			\
  : NO_REGS)

#define REGNO_OK_FOR_BASE_P(NUM) 1

#define REGNO_OK_FOR_INDEX_P(NUM) REGNO_OK_FOR_BASE_P (NUM)

/* This declaration must be present.  */
#define PREFERRED_RELOAD_CLASS(X, CLASS) \
  xstormy16_preferred_reload_class (X, CLASS)

#define PREFERRED_OUTPUT_RELOAD_CLASS(X, CLASS) \
  xstormy16_preferred_reload_class (X, CLASS)

/* This chip has the interesting property that only the first eight
   registers can be moved to/from memory.  */
#define SECONDARY_RELOAD_CLASS(CLASS, MODE, X)			\
  xstormy16_secondary_reload_class (CLASS, MODE, X)

/* Normally the compiler avoids choosing registers that have been explicitly
   mentioned in the rtl as spill registers (these registers are normally those
   used to pass parameters and return values).  However, some machines have so
   few registers of certain classes that there would not be enough registers to
   use as spill registers if this were done.

   Define `SMALL_REGISTER_CLASSES' to be an expression with a nonzero value on
   these machines.  When this macro has a nonzero value, the compiler allows
   registers explicitly used in the rtl to be used as spill registers but
   avoids extending the lifetime of these registers.

   It is always safe to define this macro with a nonzero value, but if you
   unnecessarily define it, you will reduce the amount of optimizations that
   can be performed in some cases.  If you do not define this macro with a
   nonzero value when it is required, the compiler will run out of spill
   registers and print a fatal error message.  For most machines, you should
   not define this macro at all.  */
/* #define SMALL_REGISTER_CLASSES */

/* This declaration is required.  */
#define CLASS_MAX_NREGS(CLASS, MODE) \
  ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* If defined, a C expression for a class that contains registers which the
   compiler must always access in a mode that is the same size as the mode in
   which it loaded the register.

   For the example, loading 32-bit integer or floating-point objects into
   floating-point registers on the Alpha extends them to 64-bits.  Therefore
   loading a 64-bit object and then storing it as a 32-bit object does not
   store the low-order 32-bits, as would be the case for a normal register.
   Therefore, `alpha.h' defines this macro as `FLOAT_REGS'.  */
/* #define CLASS_CANNOT_CHANGE_SIZE */

#define CONST_OK_FOR_LETTER_P(VALUE, C)			\
  (  (C) == 'I' ? (VALUE) >= 0 && (VALUE) <= 3		\
   : (C) == 'J' ? exact_log2 (VALUE) != -1		\
   : (C) == 'K' ? exact_log2 (~(VALUE)) != -1		\
   : (C) == 'L' ? (VALUE) >= 0 && (VALUE) <= 255	\
   : (C) == 'M' ? (VALUE) >= -255 && (VALUE) <= 0	\
   : (C) == 'N' ? (VALUE) >= -3 && (VALUE) <= 0		\
   : (C) == 'O' ? (VALUE) >= 1 && (VALUE) <= 4		\
   : (C) == 'P' ? (VALUE) >= -4 && (VALUE) <= -1	\
   : 0 )

#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C) 0

#define EXTRA_CONSTRAINT(VALUE, C) \
  xstormy16_extra_constraint_p (VALUE, C)


/* Basic Stack Layout */

/* We want to use post-increment instructions to push things on the stack,
   because we don't have any pre-increment ones.  */
#define STACK_PUSH_CODE POST_INC

#define FRAME_GROWS_DOWNWARD 0

#define ARGS_GROW_DOWNWARD 1

#define STARTING_FRAME_OFFSET 0

#define FIRST_PARM_OFFSET(FUNDECL) 0

#define RETURN_ADDR_RTX(COUNT, FRAMEADDR)	\
  ((COUNT) == 0					\
   ? gen_rtx_MEM (Pmode, arg_pointer_rtx)	\
   : NULL_RTX)

#define INCOMING_RETURN_ADDR_RTX  \
   gen_rtx_MEM (SImode, gen_rtx_PLUS (Pmode, stack_pointer_rtx, GEN_INT (-4)))

#define INCOMING_FRAME_SP_OFFSET (xstormy16_interrupt_function_p () ? 6 : 4)


/* Register That Address the Stack Frame.  */

#define STACK_POINTER_REGNUM 15

#define FRAME_POINTER_REGNUM 17

#define HARD_FRAME_POINTER_REGNUM 13

#define ARG_POINTER_REGNUM 18

#define STATIC_CHAIN_REGNUM 1


/* Eliminating the Frame Pointer and the Arg Pointer */

#define FRAME_POINTER_REQUIRED 0

#define ELIMINABLE_REGS					\
{							\
  {FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM},		\
  {FRAME_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM},	\
  {ARG_POINTER_REGNUM,	 STACK_POINTER_REGNUM},		\
  {ARG_POINTER_REGNUM,	 HARD_FRAME_POINTER_REGNUM},	\
}

#define CAN_ELIMINATE(FROM, TO)						\
 ((FROM) == ARG_POINTER_REGNUM && (TO) == STACK_POINTER_REGNUM		\
  ? ! frame_pointer_needed						\
  : 1)

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
  (OFFSET) = xstormy16_initial_elimination_offset (FROM, TO)


/* Passing Function Arguments on the Stack */

#define PUSH_ROUNDING(BYTES) (((BYTES) + 1) & ~1)

#define RETURN_POPS_ARGS(FUNDECL, FUNTYPE, STACK_SIZE) 0


/* Function Arguments in Registers */

#define NUM_ARGUMENT_REGISTERS 6
#define FIRST_ARGUMENT_REGISTER 2

#define XSTORMY16_WORD_SIZE(TYPE, MODE)				\
  ((((TYPE) ? int_size_in_bytes (TYPE) : GET_MODE_SIZE (MODE))	\
    + 1) 							\
   / 2)

#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
	xstormy16_function_arg (CUM, MODE, TYPE, NAMED)

/* For this platform, the value of CUMULATIVE_ARGS is the number of words
   of arguments that have been passed in registers so far.  */
#define CUMULATIVE_ARGS int

#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
  (CUM) = 0

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)			\
  ((CUM) = xstormy16_function_arg_advance (CUM, MODE, TYPE, NAMED))

#define FUNCTION_ARG_REGNO_P(REGNO)					\
  ((REGNO) >= FIRST_ARGUMENT_REGISTER 					\
   && (REGNO) < FIRST_ARGUMENT_REGISTER + NUM_ARGUMENT_REGISTERS)


/* How Scalar Function Values are Returned */

/* The number of the hard register that is used to return a scalar value from a
   function call.  */
#define RETURN_VALUE_REGNUM	FIRST_ARGUMENT_REGISTER
     
#define FUNCTION_VALUE(VALTYPE, FUNC) \
  xstormy16_function_value (VALTYPE, FUNC)

#define LIBCALL_VALUE(MODE) gen_rtx_REG (MODE, RETURN_VALUE_REGNUM)

#define FUNCTION_VALUE_REGNO_P(REGNO) ((REGNO) == RETURN_VALUE_REGNUM)


/* Function Entry and Exit */

#define EPILOGUE_USES(REGNO) \
  xstormy16_epilogue_uses (REGNO)


/* Generating Code for Profiling.  */

/* This declaration must be present, but it can be an abort if profiling is
   not implemented.  */
     
#define FUNCTION_PROFILER(FILE, LABELNO) xstormy16_function_profiler ()


/* If the target has particular reasons why a function cannot be inlined,
   it may define the TARGET_CANNOT_INLINE_P.  This macro takes one argument,
   the DECL describing the function.  The function should NULL if the function
   *can* be inlined.  Otherwise it should return a pointer to a string containing
   a message describing why the function could not be inlined.  The message will
   displayed if the '-Winline' command line switch has been given.  If the message
   contains a '%s' sequence, this will be replaced by the name of the function.  */
/* #define TARGET_CANNOT_INLINE_P(FN_DECL) xstormy16_cannot_inline_p (FN_DECL) */

/* Implementing the Varargs Macros.  */

/* Implement the stdarg/varargs va_start macro.  STDARG_P is nonzero if this
   is stdarg.h instead of varargs.h.  VALIST is the tree of the va_list
   variable to initialize.  NEXTARG is the machine independent notion of the
   'next' argument after the variable arguments.  If not defined, a standard
   implementation will be defined that works for arguments passed on the stack.  */
#define EXPAND_BUILTIN_VA_START(VALIST, NEXTARG) \
  xstormy16_expand_builtin_va_start (VALIST, NEXTARG)

/* Trampolines for Nested Functions.  */

#define TRAMPOLINE_SIZE 8

#define TRAMPOLINE_ALIGNMENT 16

#define INITIALIZE_TRAMPOLINE(ADDR, FNADDR, STATIC_CHAIN) \
  xstormy16_initialize_trampoline (ADDR, FNADDR, STATIC_CHAIN)


/* Define this macro to override the type used by the library routines to pick
   up arguments of type `float'.  (By default, they use a union of `float' and
   `int'.)

   The obvious choice would be `float'--but that won't work with traditional C
   compilers that expect all arguments declared as `float' to arrive as
   `double'.  To avoid this conversion, the library routines ask for the value
   as some other type and then treat it as a `float'.  */
/* #define FLOAT_ARG_TYPE */

/* Define this macro to override the way library routines redesignate a `float'
   argument as a `float' instead of the type it was passed as.  The default is
   an expression which takes the `float' field of the union.  */
/* #define FLOATIFY(PASSED_VALUE) */

/* Define this macro to override the type used by the library routines to
   return values that ought to have type `float'.  (By default, they use
   `int'.)

   The obvious choice would be `float'--but that won't work with traditional C
   compilers gratuitously convert values declared as `float' into `double'.  */
/* #define FLOAT_VALUE_TYPE */

/* Define this macro to override the way the value of a `float'-returning
   library routine should be packaged in order to return it.  These functions
   are actually declared to return type `FLOAT_VALUE_TYPE' (normally `int').

   These values can't be returned as type `float' because traditional C
   compilers would gratuitously convert the value to a `double'.

   A local variable named `intify' is always available when the macro `INTIFY'
   is used.  It is a union of a `float' field named `f' and a field named `i'
   whose type is `FLOAT_VALUE_TYPE' or `int'.

   If you don't define this macro, the default definition works by copying the
   value through that union.  */
/* #define INTIFY(FLOAT_VALUE) */

/* Define this macro as the name of the data type corresponding to `SImode' in
   the system's own C compiler.

   You need not define this macro if that type is `long int', as it usually is.  */
/* #define nongcc_SI_type */

/* Define this macro as the name of the data type corresponding to the
   word_mode in the system's own C compiler.

   You need not define this macro if that type is `long int', as it usually is.  */
/* #define nongcc_word_type */

/* Define these macros to supply explicit C statements to carry out various
   arithmetic operations on types `float' and `double' in the library routines
   in `libgcc1.c'.  See that file for a full list of these macros and their
   arguments.

   On most machines, you don't need to define any of these macros, because the
   C compiler that comes with the system takes care of doing them.  */
/* #define perform_...  */


/* Addressing Modes */

#define HAVE_POST_INCREMENT 1

#define HAVE_PRE_DECREMENT 1

#define CONSTANT_ADDRESS_P(X) CONSTANT_P (X)

#define MAX_REGS_PER_ADDRESS 1

#ifdef REG_OK_STRICT
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, LABEL)	\
do {							\
  if (xstormy16_legitimate_address_p (MODE, X, 1))	\
    goto LABEL;						\
} while (0)
#else
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, LABEL)	\
do {							\
  if (xstormy16_legitimate_address_p (MODE, X, 0))	\
    goto LABEL;						\
} while (0)
#endif

#ifdef REG_OK_STRICT
#define REG_OK_FOR_BASE_P(X) 						   \
  (REGNO_OK_FOR_BASE_P (REGNO (X)) && (REGNO (X) < FIRST_PSEUDO_REGISTER))
#else
#define REG_OK_FOR_BASE_P(X) REGNO_OK_FOR_BASE_P (REGNO (X))
#endif

#define REG_OK_FOR_INDEX_P(X) REG_OK_FOR_BASE_P (X)

/* On this chip, this is true if the address is valid with an offset
   of 0 but not of 6, because in that case it cannot be used as an
   address for DImode or DFmode, or if the address is a post-increment
   or pre-decrement address.
*/
#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR,LABEL)			\
  if (xstormy16_mode_dependent_address_p (ADDR))				\
    goto LABEL

#define LEGITIMATE_CONSTANT_P(X) 1


/* Describing Relative Costs of Operations */

#define REGISTER_MOVE_COST(MODE, FROM, TO) 2

#define MEMORY_MOVE_COST(M,C,I) (5 + memory_move_secondary_cost (M, C, I))

#define BRANCH_COST 5

#define SLOW_BYTE_ACCESS 0

#define NO_FUNCTION_CSE


/* Dividing the output into sections.  */

#define TEXT_SECTION_ASM_OP ".text"

#define DATA_SECTION_ASM_OP ".data"

#define BSS_SECTION_ASM_OP "\t.section\t.bss"

/* Define the pseudo-ops used to switch to the .ctors and .dtors sections.
   There are no shared libraries on this target so these sections need
   not be writable.

   Defined in elfos.h.  */

#undef CTORS_SECTION_ASM_OP
#undef DTORS_SECTION_ASM_OP
#define CTORS_SECTION_ASM_OP	"\t.section\t.ctors,\"a\""
#define DTORS_SECTION_ASM_OP	"\t.section\t.dtors,\"a\""

#define TARGET_ASM_INIT_SECTIONS xstormy16_asm_init_sections

#define JUMP_TABLES_IN_TEXT_SECTION 1

/* The Overall Framework of an Assembler File.  */

#define ASM_COMMENT_START ";"

#define ASM_APP_ON "#APP\n"

#define ASM_APP_OFF "#NO_APP\n"

/* Output of Data.  */

#define IS_ASM_LOGICAL_LINE_SEPARATOR(C) ((C) == '|')

#define ASM_OUTPUT_ALIGNED_DECL_COMMON(STREAM, DECL, NAME, SIZE, ALIGNMENT) \
  xstormy16_asm_output_aligned_common (STREAM, DECL, NAME, SIZE, ALIGNMENT, 1)
#define ASM_OUTPUT_ALIGNED_DECL_LOCAL(STREAM, DECL, NAME, SIZE, ALIGNMENT) \
  xstormy16_asm_output_aligned_common (STREAM, DECL, NAME, SIZE, ALIGNMENT, 0)


/* Output and Generation of Labels.  */
#define SYMBOL_FLAG_XSTORMY16_BELOW100	(SYMBOL_FLAG_MACH_DEP << 0)

#define ASM_OUTPUT_SYMBOL_REF(STREAM, SYMBOL)				\
  do {									\
    const char *rn = XSTR (SYMBOL, 0);					\
    if (SYMBOL_REF_FUNCTION_P (SYMBOL))					\
      ASM_OUTPUT_LABEL_REF ((STREAM), rn);				\
    else								\
      assemble_name (STREAM, rn);					\
  } while (0)

#define ASM_OUTPUT_LABEL_REF(STREAM, NAME)	\
do  {						\
  fputs ("@fptr(", STREAM);			\
  assemble_name (STREAM, NAME);			\
  fputc (')', STREAM);				\
} while (0)

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.globl "


/* Macros Controlling Initialization Routines.  */

/* When you are using special sections for
   initialization and termination functions, this macro also controls how
   `crtstuff.c' and `libgcc2.c' arrange to run the initialization functions.

   Defined in svr4.h.  */
/* #define INIT_SECTION_ASM_OP */

/* Define this macro as a C statement to output on the stream STREAM the
   assembler code to arrange to call the function named NAME at initialization
   time.

   Assume that NAME is the name of a C function generated automatically by the
   compiler.  This function takes no arguments.  Use the function
   `assemble_name' to output the name NAME; this performs any system-specific
   syntactic transformations such as adding an underscore.

   If you don't define this macro, nothing special is output to arrange to call
   the function.  This is correct when the function will be called in some
   other manner--for example, by means of the `collect2' program, which looks
   through the symbol table to find these functions by their names.

   Defined in svr4.h.  */
/* #define ASM_OUTPUT_CONSTRUCTOR(STREAM, NAME) */

/* This is like `ASM_OUTPUT_CONSTRUCTOR' but used for termination functions
   rather than initialization functions.

   Defined in svr4.h.  */
/* #define ASM_OUTPUT_DESTRUCTOR(STREAM, NAME) */

/* Define this macro if the system uses ELF format object files.

   Defined in svr4.h.  */
/* #define OBJECT_FORMAT_ELF */


/* Output of Assembler Instructions.  */

#define REGISTER_NAMES							\
{ "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",	\
  "r11", "r12", "r13", "psw", "sp", "carry", "fp", "ap" }

#define ADDITIONAL_REGISTER_NAMES		\
  { { "r14", 14 },				\
    { "r15", 15 } }

#define PRINT_OPERAND(STREAM, X, CODE) xstormy16_print_operand (STREAM, X, CODE)

#define PRINT_OPERAND_ADDRESS(STREAM, X) xstormy16_print_operand_address (STREAM, X)

/* USER_LABEL_PREFIX is defined in svr4.h.  */
#define REGISTER_PREFIX ""
#define LOCAL_LABEL_PREFIX "."
#define USER_LABEL_PREFIX ""
#define IMMEDIATE_PREFIX "#"

#define ASM_OUTPUT_REG_PUSH(STREAM, REGNO) \
  fprintf (STREAM, "\tpush %d\n", REGNO)

#define ASM_OUTPUT_REG_POP(STREAM, REGNO) \
  fprintf (STREAM, "\tpop %d\n", REGNO)


/* Output of dispatch tables.  */

/* This port does not use the ASM_OUTPUT_ADDR_VEC_ELT macro, because
   this could cause label alignment to appear between the 'br' and the table,
   which would be bad.  Instead, it controls the output of the table
   itself.  */
#define ASM_OUTPUT_ADDR_VEC(LABEL, BODY) \
  xstormy16_output_addr_vec (file, LABEL, BODY)

/* Alignment for ADDR_VECs is the same as for code.  */
#define ADDR_VEC_ALIGN(ADDR_VEC) 1


/* Assembler Commands for Exception Regions.  */

#define DWARF2_UNWIND_INFO 0

/* Don't use __builtin_setjmp for unwinding, since it's tricky to get
   at the high 16 bits of an address.  */
#define DONT_USE_BUILTIN_SETJMP
#define JMP_BUF_SIZE  8

/* Assembler Commands for Alignment.  */

#define ASM_OUTPUT_ALIGN(STREAM, POWER) \
  fprintf ((STREAM), "\t.p2align %d\n", (POWER))


/* Macros Affecting all Debug Formats.  */

/* Defined in svr4.h.  */
#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG


/* Macros for SDB and Dwarf Output.  */

/* Define this macro if addresses in Dwarf 2 debugging info should not
   be the same size as pointers on the target architecture.  The
   macro's value should be the size, in bytes, to use for addresses in
   the debugging info.

   Some architectures use word addresses to refer to code locations,
   but Dwarf 2 info always uses byte addresses.  On such machines,
   Dwarf 2 addresses need to be larger than the architecture's
   pointers.  */
#define DWARF2_ADDR_SIZE 4


/* Miscellaneous Parameters.  */

#define CASE_VECTOR_MODE SImode

#define WORD_REGISTER_OPERATIONS

#define LOAD_EXTEND_OP(MODE) ZERO_EXTEND

#define MOVE_MAX 2

#define SHIFT_COUNT_TRUNCATED 1

#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

#define Pmode HImode

#define FUNCTION_MODE HImode

#define NO_IMPLICIT_EXTERN_C

/* Defined in svr4.h.  */
#define HANDLE_SYSV_PRAGMA 1

/* Define this if the target system supports the function `atexit' from the
   ANSI C standard.  If this is not defined, and `INIT_SECTION_ASM_OP' is not
   defined, a default `exit' function will be provided to support C++.

   Defined by svr4.h */
/* #define HAVE_ATEXIT */

/* A C statement which is executed by the Haifa scheduler after it has scheduled
   an insn from the ready list.  FILE is either a null pointer, or a stdio stream
   to write any debug output to.  VERBOSE is the verbose level provided by
   -fsched-verbose-<n>.  INSN is the instruction that was scheduled.  MORE is the
   number of instructions that can be issued in the current cycle.  This macro
   is responsible for updating the value of MORE (typically by (MORE)--).  */
/* #define MD_SCHED_VARIABLE_ISSUE (FILE, VERBOSE, INSN, MORE) */


/* Define the information needed to generate branch and scc insns.  This is
   stored from the compare operation.  Note that we can't use "rtx" here
   since it hasn't been defined!  */

extern struct rtx_def *xstormy16_compare_op0, *xstormy16_compare_op1;

/* End of xstormy16.h */
