/* Target Definitions for R8C/M16C/M32C
   Copyright (C) 2005
   Free Software Foundation, Inc.
   Contributed by Red Hat.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef GCC_M32C_H
#define GCC_M32C_H

/* Controlling the Compilation Driver, `gcc'.  */

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "crt0.o%s crtbegin.o%s"

/* There are four CPU series we support, but they basically break down
   into two families - the R8C/M16C families, with 16 bit address
   registers and one set of opcodes, and the M32CM/M32C group, with 24
   bit address registers and a different set of opcodes.  The
   assembler doesn't care except for which opcode set is needed; the
   big difference is in the memory maps, which we cover in
   LIB_SPEC.  */

#undef  ASM_SPEC
#define ASM_SPEC "\
%{mcpu=r8c:--m16c} \
%{mcpu=m16c:--m16c} \
%{mcpu=m32cm:--m32c} \
%{mcpu=m32c:--m32c} "

/* The default is R8C hardware.  We support a simulator, which has its
   own libgloss and link map, plus one default link map for each chip
   family.  Most of the logic here is making sure we do the right
   thing when no CPU is specified, which defaults to R8C.  */
#undef  LIB_SPEC
#define LIB_SPEC "-( -lc %{msim*:-lsim}%{!msim*:-lnosys} -) \
%{msim*:%{!T*: %{mcpu=m32cm:-Tsim24.ld}%{mcpu=m32c:-Tsim24.ld} \
	%{!mcpu=m32cm:%{!mcpu=m32c:-Tsim16.ld}}}} \
%{!T*:%{!msim*: %{mcpu=m16c:-Tm16c.ld} \
		%{mcpu=m32cm:-Tm32cm.ld} \
		%{mcpu=m32c:-Tm32c.ld} \
		%{!mcpu=m16c:%{!mcpu=m32cm:%{!mcpu=m32c:-Tr8c.ld}}}}} \
"

/* Run-time Target Specification */

/* Nothing unusual here.  */
#define TARGET_CPU_CPP_BUILTINS() \
  { \
    builtin_assert ("cpu=m32c"); \
    builtin_assert ("machine=m32c"); \
    builtin_define ("__m32c__=1"); \
    if (TARGET_R8C) \
      builtin_define ("__r8c_cpu__=1"); \
    if (TARGET_M16C) \
      builtin_define ("__m16c_cpu__=1"); \
    if (TARGET_M32CM) \
      builtin_define ("__m32cm_cpu__=1"); \
    if (TARGET_M32C) \
      builtin_define ("__m32c_cpu__=1"); \
  }

/* The pragma handlers need to know if we've started processing
   functions yet, as the memregs pragma should only be given at the
   beginning of the file.  This variable starts off TRUE and later
   becomes FALSE.  */
extern int ok_to_change_target_memregs;
extern int target_memregs;

/* TARGET_CPU is a multi-way option set in m32c.opt.  While we could
   use enums or defines for this, this and m32c.opt are the only
   places that know (or care) what values are being used.  */
#define TARGET_R8C	(target_cpu == 'r')
#define TARGET_M16C	(target_cpu == '6')
#define TARGET_M32CM	(target_cpu == 'm')
#define TARGET_M32C	(target_cpu == '3')

/* Address register sizes.  Warning: these are used all over the place
   to select between the two CPU families in general.  */
#define TARGET_A16	(TARGET_R8C || TARGET_M16C)
#define TARGET_A24	(TARGET_M32CM || TARGET_M32C)

#define TARGET_VERSION fprintf (stderr, " (m32c)");

#define OVERRIDE_OPTIONS m32c_override_options ();

/* Defining data structures for per-function information */

typedef struct machine_function GTY (())
{
  /* How much we adjust the stack when returning from an exception
     handler.  */
  rtx eh_stack_adjust;

  /* TRUE if the current function is an interrupt handler.  */
  int is_interrupt;

  /* TRUE if the current function is a leaf function.  Currently, this
     only affects saving $a0 in interrupt functions.  */
  int is_leaf;

  /* Bitmask that keeps track of which registers are used in an
     interrupt function, so we know which ones need to be saved and
     restored.  */
  int intr_pushm;
  /* Likewise, one element for each memreg that needs to be saved.  */
  char intr_pushmem[16];

  /* TRUE if the current function can use a simple RTS to return, instead
     of the longer ENTER/EXIT pair.  */
  int use_rts;
}
machine_function;

#define INIT_EXPANDERS m32c_init_expanders ()

/* Storage Layout */

#define BITS_BIG_ENDIAN 0
#define BYTES_BIG_ENDIAN 0
#define WORDS_BIG_ENDIAN 0

/* We can do QI, HI, and SI operations pretty much equally well, but
   GCC expects us to have a "native" format, so we pick the one that
   matches "int".  Pointers are 16 bits for R8C/M16C (when TARGET_A16
   is true) and 24 bits for M32CM/M32C (when TARGET_A24 is true), but
   24 bit pointers are stored in 32 bit words.  */
#define BITS_PER_UNIT 8
#define UNITS_PER_WORD 2
#define POINTER_SIZE (TARGET_A16 ? 16 : 32)
#define POINTERS_EXTEND_UNSIGNED 1

/* These match the alignment enforced by the two types of stack operations.  */
#define PARM_BOUNDARY (TARGET_A16 ? 8 : 16)
#define STACK_BOUNDARY (TARGET_A16 ? 8 : 16)

/* We do this because we care more about space than about speed.  For
   the chips with 16 bit busses, we could set these to 16 if
   desired.  */
#define FUNCTION_BOUNDARY 8
#define BIGGEST_ALIGNMENT 8

#define STRICT_ALIGNMENT 0
#define SLOW_BYTE_ACCESS 1

/* Layout of Source Language Data Types */

#define INT_TYPE_SIZE 16
#define SHORT_TYPE_SIZE 16
#define LONG_TYPE_SIZE 32
#define LONG_LONG_TYPE_SIZE 64

#define FLOAT_TYPE_SIZE 32
#define DOUBLE_TYPE_SIZE 64
#define LONG_DOUBLE_TYPE_SIZE 64

#define DEFAULT_SIGNED_CHAR 1

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE (TARGET_A16 ? "int" : "long int")

/* REGISTER USAGE */

/* Register Basics */

/* Register layout:

        [r0h][r0l]  $r0  (16 bits, or two 8 bit halves)
        [--------]  $r2  (16 bits)
        [r1h][r1l]  $r1  (16 bits, or two 8 bit halves)
        [--------]  $r3  (16 bits)
   [---][--------]  $a0  (might be 24 bits)
   [---][--------]  $a1  (might be 24 bits)
   [---][--------]  $sb  (might be 24 bits)
   [---][--------]  $fb  (might be 24 bits)
   [---][--------]  $sp  (might be 24 bits)
   [-------------]  $pc  (20 or 24 bits)
             [---]  $flg (CPU flags)
   [---][--------]  $argp (virtual)
        [--------]  $mem0 (all 16 bits)
          . . .
        [--------]  $mem14
*/

#define FIRST_PSEUDO_REGISTER   20

/* Note that these two tables are modified based on which CPU family
   you select; see m32c_conditional_register_usage for details.  */

/* r0 r2 r1 r3 - a0 a1 sb fb - sp pc flg argp - mem0..mem14 */
#define FIXED_REGISTERS     { 0, 0, 0, 0, \
			      0, 0, 1, 0, \
			      1, 1, 0, 1, \
			      0, 0, 0, 0, 0, 0, 0, 0 }
#define CALL_USED_REGISTERS { 1, 1, 1, 1, \
			      1, 1, 1, 0, \
			      1, 1, 1, 1, \
			      1, 1, 1, 1, 1, 1, 1, 1 }

#define CONDITIONAL_REGISTER_USAGE m32c_conditional_register_usage ();

/* The *_REGNO theme matches m32c.md and most register number
   arguments; the PC_REGNUM is the odd one out.  */
#ifndef PC_REGNO
#define PC_REGNO 9
#endif
#define PC_REGNUM PC_REGNO

/* Order of Allocation of Registers */

#define REG_ALLOC_ORDER { \
	0, 1, 2, 3, 4, 5, /* r0..r3, a0, a1 */ \
	12, 13, 14, 15, 16, 17, 18, /* mem0..mem7 */  \
	6, 7, 8, 9, 10, 11 /* sb, fb, sp, pc, flg, ap */ }

/* How Values Fit in Registers */

#define HARD_REGNO_NREGS(R,M) m32c_hard_regno_nregs (R, M)
#define HARD_REGNO_MODE_OK(R,M) m32c_hard_regno_ok (R, M)
#define MODES_TIEABLE_P(M1,M2) m32c_modes_tieable_p (M1, M2)
#define AVOID_CCMODE_COPIES

/* Register Classes */

/* Most registers are special purpose in some form or another, so this
   table is pretty big.  Class names are used for constraints also;
   for example the HL_REGS class (HL below) is "Rhl" in the md files.
   See m32c_reg_class_from_constraint for the mapping.  There's some
   duplication so that we can better isolate the reason for using
   constraints in the md files from the actual registers used; for
   example we may want to exclude a1a0 from SI_REGS in the future,
   without precluding their use as HImode registers.  */

/* m7654 - m3210 - argp flg pc sp - fb sb a1 a0 - r3 r1 r2 r0 */
/*       mmPAR */
#define REG_CLASS_CONTENTS \
{ { 0x00000000 }, /* NO */\
  { 0x00000100 }, /* SP  - sp */\
  { 0x00000080 }, /* FB  - fb */\
  { 0x00000040 }, /* SB  - sb */\
  { 0x000001c0 }, /* CR  - sb fb sp */\
  { 0x00000001 }, /* R0  - r0 */\
  { 0x00000004 }, /* R1  - r1 */\
  { 0x00000002 }, /* R2  - r2 */\
  { 0x00000008 }, /* R3  - r3 */\
  { 0x00000003 }, /* R02 - r0r2 */\
  { 0x00000005 }, /* HL  - r0 r1 */\
  { 0x00000005 }, /* QI  - r0 r1 */\
  { 0x0000000a }, /* R23 - r2 r3 */\
  { 0x0000000f }, /* R03 - r0r2 r1r3 */\
  { 0x0000000f }, /* DI  - r0r2r1r3 + mems */\
  { 0x00000010 }, /* A0  - a0 */\
  { 0x00000020 }, /* A1  - a1 */\
  { 0x00000030 }, /* A   - a0 a1 */\
  { 0x000000f0 }, /* AD  - a0 a1 sb fp */\
  { 0x000001f0 }, /* PS  - a0 a1 sb fp sp */\
  { 0x0000000f }, /* SI  - r0r2 r1r3 a0a1 */\
  { 0x0000003f }, /* HI  - r0 r1 r2 r3 a0 a1 */\
  { 0x0000003f }, /* RA  - r0..r3 a0 a1 */\
  { 0x0000007f }, /* GENERAL */\
  { 0x00000400 }, /* FLG */\
  { 0x000001ff }, /* HC  - r0l r1 r2 r3 a0 a1 sb fb sp */\
  { 0x000ff000 }, /* MEM */\
  { 0x000ff003 }, /* R02_A_MEM */\
  { 0x000ff005 }, /* A_HL_MEM */\
  { 0x000ff00c }, /* R1_R3_A_MEM */\
  { 0x000ff00f }, /* R03_MEM */\
  { 0x000ff03f }, /* A_HI_MEM */\
  { 0x000ff0ff }, /* A_AD_CR_MEM_SI */\
  { 0x000ff1ff }, /* ALL */\
}

enum reg_class
{
  NO_REGS,
  SP_REGS,
  FB_REGS,
  SB_REGS,
  CR_REGS,
  R0_REGS,
  R1_REGS,
  R2_REGS,
  R3_REGS,
  R02_REGS,
  HL_REGS,
  QI_REGS,
  R23_REGS,
  R03_REGS,
  DI_REGS,
  A0_REGS,
  A1_REGS,
  A_REGS,
  AD_REGS,
  PS_REGS,
  SI_REGS,
  HI_REGS,
  RA_REGS,
  GENERAL_REGS,
  FLG_REGS,
  HC_REGS,
  MEM_REGS,
  R02_A_MEM_REGS,
  A_HL_MEM_REGS,
  R1_R3_A_MEM_REGS,
  R03_MEM_REGS,
  A_HI_MEM_REGS,
  A_AD_CR_MEM_SI_REGS,
  ALL_REGS,
  LIM_REG_CLASSES
};

#define N_REG_CLASSES LIM_REG_CLASSES

#define REG_CLASS_NAMES {\
"NO_REGS", \
"SP_REGS", \
"FB_REGS", \
"SB_REGS", \
"CR_REGS", \
"R0_REGS", \
"R1_REGS", \
"R2_REGS", \
"R3_REGS", \
"R02_REGS", \
"HL_REGS", \
"QI_REGS", \
"R23_REGS", \
"R03_REGS", \
"DI_REGS", \
"A0_REGS", \
"A1_REGS", \
"A_REGS", \
"AD_REGS", \
"PS_REGS", \
"SI_REGS", \
"HI_REGS", \
"RA_REGS", \
"GENERAL_REGS", \
"FLG_REGS", \
"HC_REGS", \
"MEM_REGS", \
"R02_A_MEM_REGS", \
"A_HL_MEM_REGS", \
"R1_R3_A_MEM_REGS", \
"R03_MEM_REGS", \
"A_HI_MEM_REGS", \
"A_AD_CR_MEM_SI_REGS", \
"ALL_REGS", \
}

#define REGNO_REG_CLASS(R) m32c_regno_reg_class (R)

/* We support simple displacements off address registers, nothing else.  */
#define BASE_REG_CLASS A_REGS
#define INDEX_REG_CLASS NO_REGS

/* We primarily use the new "long" constraint names, with the initial
   letter classifying the constraint type and following letters
   specifying which.  The types are:

   I - integer values
   R - register classes
   S - memory references (M was used)
   A - addresses (currently unused)
*/

#define CONSTRAINT_LEN(CHAR,STR) \
	((CHAR) == 'I' ? 3 \
	 : (CHAR) == 'R' ? 3 \
	 : (CHAR) == 'S' ? 2 \
	 : (CHAR) == 'A' ? 2 \
	 : DEFAULT_CONSTRAINT_LEN(CHAR,STR))
#define REG_CLASS_FROM_CONSTRAINT(CHAR,STR) \
	m32c_reg_class_from_constraint (CHAR, STR)

#define REGNO_OK_FOR_BASE_P(NUM) m32c_regno_ok_for_base_p (NUM)
#define REGNO_OK_FOR_INDEX_P(NUM) 0

#define PREFERRED_RELOAD_CLASS(X,CLASS) m32c_preferred_reload_class (X, CLASS)
#define PREFERRED_OUTPUT_RELOAD_CLASS(X,CLASS) m32c_preferred_output_reload_class (X, CLASS)
#define LIMIT_RELOAD_CLASS(MODE,CLASS) m32c_limit_reload_class (MODE, CLASS)

#define SECONDARY_RELOAD_CLASS(CLASS,MODE,X) m32c_secondary_reload_class (CLASS, MODE, X)

#define SMALL_REGISTER_CLASSES 1

#define CLASS_LIKELY_SPILLED_P(C) m32c_class_likely_spilled_p (C)

#define CLASS_MAX_NREGS(C,M) m32c_class_max_nregs (C, M)

#define CANNOT_CHANGE_MODE_CLASS(F,T,C) m32c_cannot_change_mode_class(F,T,C)

#define CONST_OK_FOR_CONSTRAINT_P(VALUE,C,STR) \
	m32c_const_ok_for_constraint_p (VALUE, C, STR)
#define CONST_DOUBLE_OK_FOR_CONSTRAINT_P(VALUE,C,STR) 0
#define EXTRA_CONSTRAINT_STR(VALUE,C,STR) \
	m32c_extra_constraint_p (VALUE, C, STR)
#define EXTRA_MEMORY_CONSTRAINT(C,STR) \
	m32c_extra_memory_constraint (C, STR)
#define EXTRA_ADDRESS_CONSTRAINT(C,STR) \
	m32c_extra_address_constraint (C, STR)

/* STACK AND CALLING */

/* Frame Layout */

/* Standard push/pop stack, no surprises here.  */

#define STACK_GROWS_DOWNWARD 1
#define STACK_PUSH_CODE PRE_DEC
#define FRAME_GROWS_DOWNWARD 1

#define STARTING_FRAME_OFFSET 0
#define FIRST_PARM_OFFSET(F) 0

#define RETURN_ADDR_RTX(COUNT,FA) m32c_return_addr_rtx (COUNT)

#define INCOMING_RETURN_ADDR_RTX m32c_incoming_return_addr_rtx()
#define INCOMING_FRAME_SP_OFFSET (TARGET_A24 ? 4 : 3)

/* Exception Handling Support */

#define EH_RETURN_DATA_REGNO(N) m32c_eh_return_data_regno (N)
#define EH_RETURN_STACKADJ_RTX m32c_eh_return_stackadj_rtx ()

/* Registers That Address the Stack Frame */

#ifndef FP_REGNO
#define FP_REGNO 7
#endif
#ifndef SP_REGNO
#define SP_REGNO 8
#endif
#define AP_REGNO 11

#define STACK_POINTER_REGNUM	SP_REGNO
#define FRAME_POINTER_REGNUM	FP_REGNO
#define ARG_POINTER_REGNUM	AP_REGNO

/* The static chain must be pointer-capable.  */
#define STATIC_CHAIN_REGNUM A0_REGNO

#define DWARF_FRAME_REGISTERS 20
#define DWARF_FRAME_REGNUM(N) m32c_dwarf_frame_regnum (N)
#define DBX_REGISTER_NUMBER(N) m32c_dwarf_frame_regnum (N)

/* Eliminating Frame Pointer and Arg Pointer */

/* If the frame pointer isn't used, we detect it manually.  But the
   stack pointer doesn't have as flexible addressing as the frame
   pointer, so we always assume we have it.  */
#define FRAME_POINTER_REQUIRED 1

#define ELIMINABLE_REGS \
  {{AP_REGNO, SP_REGNO}, \
   {AP_REGNO, FB_REGNO}, \
   {FB_REGNO, SP_REGNO}}

#define CAN_ELIMINATE(FROM,TO) 1
#define INITIAL_ELIMINATION_OFFSET(FROM,TO,VAR) \
	(VAR) = m32c_initial_elimination_offset(FROM,TO)

/* Passing Function Arguments on the Stack */

#define PUSH_ARGS 1
#define PUSH_ROUNDING(N) m32c_push_rounding (N)
#define RETURN_POPS_ARGS(D,T,S) 0
#define CALL_POPS_ARGS(C) 0

/* Passing Arguments in Registers */

#define FUNCTION_ARG(CA,MODE,TYPE,NAMED) \
	m32c_function_arg (&(CA),MODE,TYPE,NAMED)

typedef struct m32c_cumulative_args
{
  /* For address of return value buffer (structures are returned by
     passing the address of a buffer as an invisible first argument.
     This identifies it).  If set, the current parameter will be put
     on the stack, regardless of type.  */
  int force_mem;
  /* First parm is 1, parm 0 is hidden pointer for returning
     aggregates.  */
  int parm_num;
} m32c_cumulative_args;

#define CUMULATIVE_ARGS m32c_cumulative_args
#define INIT_CUMULATIVE_ARGS(CA,FNTYPE,LIBNAME,FNDECL,N_NAMED_ARGS) \
	m32c_init_cumulative_args (&(CA),FNTYPE,LIBNAME,FNDECL,N_NAMED_ARGS)
#define FUNCTION_ARG_ADVANCE(CA,MODE,TYPE,NAMED) \
	m32c_function_arg_advance (&(CA),MODE,TYPE,NAMED)
#define FUNCTION_ARG_BOUNDARY(MODE,TYPE) (TARGET_A16 ? 8 : 16)
#define FUNCTION_ARG_REGNO_P(r) m32c_function_arg_regno_p (r)

/* How Scalar Function Values Are Returned */

#define FUNCTION_VALUE(VT,F) m32c_function_value (VT, F)
#define LIBCALL_VALUE(MODE) m32c_libcall_value (MODE)

#define FUNCTION_VALUE_REGNO_P(r) ((r) == R0_REGNO || (r) == MEM0_REGNO)

/* How Large Values Are Returned */

#define DEFAULT_PCC_STRUCT_RETURN 1

/* Function Entry and Exit */

#define EXIT_IGNORE_STACK 0
#define EPILOGUE_USES(REGNO) m32c_epilogue_uses(REGNO)
#define EH_USES(REGNO) 0	/* FIXME */

/* Generating Code for Profiling */

#define FUNCTION_PROFILER(FILE,LABELNO)

/* Implementing the Varargs Macros */

/* Trampolines for Nested Functions */

#define TRAMPOLINE_SIZE m32c_trampoline_size ()
#define TRAMPOLINE_ALIGNMENT m32c_trampoline_alignment ()
#define INITIALIZE_TRAMPOLINE(a,fn,sc) m32c_initialize_trampoline (a, fn, sc)

/* Addressing Modes */

#define HAVE_PRE_DECREMENT 1
#define HAVE_POST_INCREMENT 1
#define CONSTANT_ADDRESS_P(X) CONSTANT_P(X)
#define MAX_REGS_PER_ADDRESS 1

/* This is passed to the macros below, so that they can be implemented
   in m32c.c.  */
#ifdef REG_OK_STRICT
#define REG_OK_STRICT_V 1
#else
#define REG_OK_STRICT_V 0
#endif

#define GO_IF_LEGITIMATE_ADDRESS(MODE,X,LABEL) \
	if (m32c_legitimate_address_p (MODE, X, REG_OK_STRICT_V)) \
	  goto LABEL;

#define REG_OK_FOR_BASE_P(X) m32c_reg_ok_for_base_p (X, REG_OK_STRICT_V)
#define REG_OK_FOR_INDEX_P(X) 0

/* #define FIND_BASE_TERM(X) when we do unspecs for symrefs */

#define LEGITIMIZE_ADDRESS(X,OLDX,MODE,WIN) \
	if (m32c_legitimize_address(&(X),OLDX,MODE)) \
	  goto win;

#define LEGITIMIZE_RELOAD_ADDRESS(X,MODE,OPNUM,TYPE,IND_LEVELS,WIN) \
	if (m32c_legitimize_reload_address(&(X),MODE,OPNUM,TYPE,IND_LEVELS)) \
	  goto win;

#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR,LABEL) \
	if (m32c_mode_dependent_address (ADDR)) \
	  goto LABEL;

#define LEGITIMATE_CONSTANT_P(X) m32c_legitimate_constant_p (X)

/* Condition Code Status */

#define REVERSIBLE_CC_MODE(MODE) 1

/* Describing Relative Costs of Operations */

#define REGISTER_MOVE_COST(MODE,FROM,TO) \
	m32c_register_move_cost (MODE, FROM, TO)
#define MEMORY_MOVE_COST(MODE,CLASS,IN) \
	m32c_memory_move_cost (MODE, CLASS, IN)

/* Dividing the Output into Sections (Texts, Data, ...) */

#define TEXT_SECTION_ASM_OP ".text"
#define DATA_SECTION_ASM_OP ".data"
#define BSS_SECTION_ASM_OP ".bss"

#define CTOR_LIST_BEGIN
#define CTOR_LIST_END
#define DTOR_LIST_BEGIN
#define DTOR_LIST_END
#define CTORS_SECTION_ASM_OP "\t.section\t.init_array,\"aw\",%init_array"
#define DTORS_SECTION_ASM_OP "\t.section\t.fini_array,\"aw\",%fini_array"
#define INIT_ARRAY_SECTION_ASM_OP "\t.section\t.init_array,\"aw\",%init_array"
#define FINI_ARRAY_SECTION_ASM_OP "\t.section\t.fini_array,\"aw\",%fini_array"

/* The Overall Framework of an Assembler File */

#define ASM_COMMENT_START ";"
#define ASM_APP_ON ""
#define ASM_APP_OFF ""

/* Output and Generation of Labels */

#define GLOBAL_ASM_OP "\t.global\t"

/* Output of Assembler Instructions */

#define REGISTER_NAMES {	\
  "r0", "r2", "r1", "r3", \
  "a0", "a1", "sb", "fb", "sp", \
  "pc", "flg", "argp", \
  "mem0",  "mem2",  "mem4",  "mem6",  "mem8",  "mem10",  "mem12",  "mem14", \
}

#define ADDITIONAL_REGISTER_NAMES { \
  {"r0l", 0}, \
  {"r1l", 2}, \
  {"r0r2", 0}, \
  {"r1r3", 2}, \
  {"a0a1", 4}, \
  {"r0r2r1r3", 0} }

#define PRINT_OPERAND(S,X,C) m32c_print_operand (S, X, C)
#define PRINT_OPERAND_PUNCT_VALID_P(C) m32c_print_operand_punct_valid_p (C)
#define PRINT_OPERAND_ADDRESS(S,X) m32c_print_operand_address (S, X)

#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX "_"

#define ASM_OUTPUT_REG_PUSH(S,R) m32c_output_reg_push (S, R)
#define ASM_OUTPUT_REG_POP(S,R) m32c_output_reg_pop (S, R)

/* Output of Dispatch Tables */

#define ASM_OUTPUT_ADDR_VEC_ELT(S,V) \
	fprintf (S, "\t.word L%d\n", V)

/* Assembler Commands for Exception Regions */

#define DWARF_CIE_DATA_ALIGNMENT -1

/* Assembler Commands for Alignment */

#define ASM_OUTPUT_ALIGN(STREAM,POWER) \
	fprintf (STREAM, "\t.p2align\t%d\n", POWER);

/* Controlling Debugging Information Format */

#define DWARF2_ADDR_SIZE	4

/* Miscellaneous Parameters */

#define HAS_LONG_COND_BRANCH false
#define HAS_LONG_UNCOND_BRANCH true
#define CASE_VECTOR_MODE SImode
#define LOAD_EXTEND_OP(MEM) ZERO_EXTEND

#define MOVE_MAX 4
#define TRULY_NOOP_TRUNCATION(op,ip) 1

#define STORE_FLAG_VALUE 1

/* 16 or 24 bit pointers */
#define Pmode (TARGET_A16 ? HImode : PSImode)
#define FUNCTION_MODE QImode

#define REGISTER_TARGET_PRAGMAS() m32c_register_pragmas()

#endif
