/* Definitions of target machine for GNU compiler,
   for ATMEL AVR at90s8515, ATmega103/103L, ATmega603/603L microcontrollers.
   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Contributed by Denis Chertykov (denisc@overta.ru)

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

/* Names to predefine in the preprocessor for this target machine.  */

#define TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define_std ("AVR");		\
      if (avr_base_arch_macro)			\
	builtin_define (avr_base_arch_macro);	\
      if (avr_extra_arch_macro)			\
	builtin_define (avr_extra_arch_macro);	\
      if (avr_have_movw_lpmx_p)			\
	builtin_define ("__AVR_HAVE_MOVW__");	\
      if (avr_have_movw_lpmx_p)			\
	builtin_define ("__AVR_HAVE_LPMX__");	\
      if (avr_asm_only_p)			\
	builtin_define ("__AVR_ASM_ONLY__");	\
      if (avr_enhanced_p)			\
	builtin_define ("__AVR_ENHANCED__");	\
      if (avr_enhanced_p)			\
	builtin_define ("__AVR_HAVE_MUL__");	\
      if (avr_mega_p)				\
	builtin_define ("__AVR_MEGA__");	\
      if (TARGET_NO_INTERRUPTS)			\
	builtin_define ("__NO_INTERRUPTS__");	\
    }						\
  while (0)

extern const char *avr_base_arch_macro;
extern const char *avr_extra_arch_macro;
extern int avr_mega_p;
extern int avr_enhanced_p;
extern int avr_asm_only_p;
extern int avr_have_movw_lpmx_p;
#ifndef IN_LIBGCC2
extern GTY(()) section *progmem_section;
#endif

#define AVR_MEGA (avr_mega_p && !TARGET_SHORT_CALLS)
#define AVR_ENHANCED (avr_enhanced_p)
#define AVR_HAVE_MOVW (avr_have_movw_lpmx_p)

#define TARGET_VERSION fprintf (stderr, " (GNU assembler syntax)");

#define OVERRIDE_OPTIONS avr_override_options ()

#define CAN_DEBUG_WITHOUT_FP

#define BITS_BIG_ENDIAN 0
#define BYTES_BIG_ENDIAN 0
#define WORDS_BIG_ENDIAN 0

#ifdef IN_LIBGCC2
/* This is to get correct SI and DI modes in libgcc2.c (32 and 64 bits).  */
#define UNITS_PER_WORD 4
#else
/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD 1
#endif

#define POINTER_SIZE 16


/* Maximum sized of reasonable data type
   DImode or Dfmode ...  */
#define MAX_FIXED_MODE_SIZE 32

#define PARM_BOUNDARY 8

#define FUNCTION_BOUNDARY 8

#define EMPTY_FIELD_BOUNDARY 8

/* No data type wants to be aligned rounder than this.  */
#define BIGGEST_ALIGNMENT 8


#define STRICT_ALIGNMENT 0

#define INT_TYPE_SIZE (TARGET_INT8 ? 8 : 16)
#define SHORT_TYPE_SIZE (INT_TYPE_SIZE == 8 ? INT_TYPE_SIZE : 16)
#define LONG_TYPE_SIZE (INT_TYPE_SIZE == 8 ? 16 : 32)
#define LONG_LONG_TYPE_SIZE (INT_TYPE_SIZE == 8 ? 32 : 64)
#define FLOAT_TYPE_SIZE 32
#define DOUBLE_TYPE_SIZE 32
#define LONG_DOUBLE_TYPE_SIZE 32

#define DEFAULT_SIGNED_CHAR 1

#define SIZE_TYPE (INT_TYPE_SIZE == 8 ? "long unsigned int" : "unsigned int")
#define PTRDIFF_TYPE (INT_TYPE_SIZE == 8 ? "long int" :"int")

#define WCHAR_TYPE_SIZE 16

#define FIRST_PSEUDO_REGISTER 36

#define FIXED_REGISTERS {\
  1,1,/* r0 r1 */\
  0,0,/* r2 r3 */\
  0,0,/* r4 r5 */\
  0,0,/* r6 r7 */\
  0,0,/* r8 r9 */\
  0,0,/* r10 r11 */\
  0,0,/* r12 r13 */\
  0,0,/* r14 r15 */\
  0,0,/* r16 r17 */\
  0,0,/* r18 r19 */\
  0,0,/* r20 r21 */\
  0,0,/* r22 r23 */\
  0,0,/* r24 r25 */\
  0,0,/* r26 r27 */\
  0,0,/* r28 r29 */\
  0,0,/* r30 r31 */\
  1,1,/*  STACK */\
  1,1 /* arg pointer */  }

#define CALL_USED_REGISTERS {			\
  1,1,/* r0 r1 */				\
    0,0,/* r2 r3 */				\
    0,0,/* r4 r5 */				\
    0,0,/* r6 r7 */				\
    0,0,/* r8 r9 */				\
    0,0,/* r10 r11 */				\
    0,0,/* r12 r13 */				\
    0,0,/* r14 r15 */				\
    0,0,/* r16 r17 */				\
    1,1,/* r18 r19 */				\
    1,1,/* r20 r21 */				\
    1,1,/* r22 r23 */				\
    1,1,/* r24 r25 */				\
    1,1,/* r26 r27 */				\
    0,0,/* r28 r29 */				\
    1,1,/* r30 r31 */				\
    1,1,/*  STACK */				\
    1,1 /* arg pointer */  }

#define REG_ALLOC_ORDER {			\
    24,25,					\
    18,19,					\
    20,21,					\
    22,23,					\
    30,31,					\
    26,27,					\
    28,29,					\
    17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,	\
    0,1,					\
    32,33,34,35					\
    }

#define ORDER_REGS_FOR_LOCAL_ALLOC order_regs_for_local_alloc ()


#define HARD_REGNO_NREGS(REGNO, MODE) ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

#define HARD_REGNO_MODE_OK(REGNO, MODE) avr_hard_regno_mode_ok(REGNO, MODE)

#define MODES_TIEABLE_P(MODE1, MODE2) 1

enum reg_class {
  NO_REGS,
  R0_REG,			/* r0 */
  POINTER_X_REGS,		/* r26 - r27 */
  POINTER_Y_REGS,		/* r28 - r29 */
  POINTER_Z_REGS,		/* r30 - r31 */
  STACK_REG,			/* STACK */
  BASE_POINTER_REGS,		/* r28 - r31 */
  POINTER_REGS,			/* r26 - r31 */
  ADDW_REGS,			/* r24 - r31 */
  SIMPLE_LD_REGS,		/* r16 - r23 */
  LD_REGS,			/* r16 - r31 */
  NO_LD_REGS,			/* r0 - r15 */
  GENERAL_REGS,			/* r0 - r31 */
  ALL_REGS, LIM_REG_CLASSES
};


#define N_REG_CLASSES (int)LIM_REG_CLASSES

#define REG_CLASS_NAMES {					\
		 "NO_REGS",					\
		   "R0_REG",	/* r0 */                        \
		   "POINTER_X_REGS", /* r26 - r27 */		\
		   "POINTER_Y_REGS", /* r28 - r29 */		\
		   "POINTER_Z_REGS", /* r30 - r31 */		\
		   "STACK_REG",	/* STACK */			\
		   "BASE_POINTER_REGS",	/* r28 - r31 */		\
		   "POINTER_REGS", /* r26 - r31 */		\
		   "ADDW_REGS",	/* r24 - r31 */			\
                   "SIMPLE_LD_REGS", /* r16 - r23 */            \
		   "LD_REGS",	/* r16 - r31 */			\
                   "NO_LD_REGS", /* r0 - r15 */                 \
		   "GENERAL_REGS", /* r0 - r31 */		\
		   "ALL_REGS" }

#define REG_CLASS_CONTENTS {						\
  {0x00000000,0x00000000},	/* NO_REGS */				\
  {0x00000001,0x00000000},	/* R0_REG */                            \
  {3 << REG_X,0x00000000},      /* POINTER_X_REGS, r26 - r27 */		\
  {3 << REG_Y,0x00000000},      /* POINTER_Y_REGS, r28 - r29 */		\
  {3 << REG_Z,0x00000000},      /* POINTER_Z_REGS, r30 - r31 */		\
  {0x00000000,0x00000003},	/* STACK_REG, STACK */			\
  {(3 << REG_Y) | (3 << REG_Z),						\
     0x00000000},		/* BASE_POINTER_REGS, r28 - r31 */	\
  {(3 << REG_X) | (3 << REG_Y) | (3 << REG_Z),				\
     0x00000000},		/* POINTER_REGS, r26 - r31 */		\
  {(3 << REG_X) | (3 << REG_Y) | (3 << REG_Z) | (3 << REG_W),		\
     0x00000000},		/* ADDW_REGS, r24 - r31 */		\
  {0x00ff0000,0x00000000},	/* SIMPLE_LD_REGS r16 - r23 */          \
  {(3 << REG_X)|(3 << REG_Y)|(3 << REG_Z)|(3 << REG_W)|(0xff << 16),	\
     0x00000000},	/* LD_REGS, r16 - r31 */			\
  {0x0000ffff,0x00000000},	/* NO_LD_REGS  r0 - r15 */              \
  {0xffffffff,0x00000000},	/* GENERAL_REGS, r0 - r31 */		\
  {0xffffffff,0x00000003}	/* ALL_REGS */				\
}

#define REGNO_REG_CLASS(R) avr_regno_reg_class(R)

#define BASE_REG_CLASS (reload_completed ? BASE_POINTER_REGS : POINTER_REGS)

#define INDEX_REG_CLASS NO_REGS

#define REGNO_OK_FOR_BASE_P(r) (((r) < FIRST_PSEUDO_REGISTER		\
				 && ((r) == REG_X			\
				     || (r) == REG_Y			\
				     || (r) == REG_Z			\
				     || (r) == ARG_POINTER_REGNUM))	\
				|| (reg_renumber			\
				    && (reg_renumber[r] == REG_X	\
					|| reg_renumber[r] == REG_Y	\
					|| reg_renumber[r] == REG_Z	\
					|| (reg_renumber[r]		\
					    == ARG_POINTER_REGNUM))))

#define REGNO_OK_FOR_INDEX_P(NUM) 0

#define PREFERRED_RELOAD_CLASS(X, CLASS) preferred_reload_class(X,CLASS)

#define SMALL_REGISTER_CLASSES 1

#define CLASS_LIKELY_SPILLED_P(c) class_likely_spilled_p(c)

#define CLASS_MAX_NREGS(CLASS, MODE)   class_max_nregs (CLASS, MODE)

#define STACK_PUSH_CODE POST_DEC

#define STACK_GROWS_DOWNWARD

#define STARTING_FRAME_OFFSET 1

#define STACK_POINTER_OFFSET 1

#define FIRST_PARM_OFFSET(FUNDECL) 0

#define STACK_BOUNDARY 8

#define STACK_POINTER_REGNUM 32

#define FRAME_POINTER_REGNUM REG_Y

#define ARG_POINTER_REGNUM 34

#define STATIC_CHAIN_REGNUM 2

#define FRAME_POINTER_REQUIRED frame_pointer_required_p()

/* Offset from the frame pointer register value to the top of the stack.  */
#define FRAME_POINTER_CFA_OFFSET(FNDECL) 0

#define ELIMINABLE_REGS {					\
      {ARG_POINTER_REGNUM, FRAME_POINTER_REGNUM},		\
	{FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM}		\
       ,{FRAME_POINTER_REGNUM+1,STACK_POINTER_REGNUM+1}}

#define CAN_ELIMINATE(FROM, TO) (((FROM) == ARG_POINTER_REGNUM		   \
				  && (TO) == FRAME_POINTER_REGNUM)	   \
				 || (((FROM) == FRAME_POINTER_REGNUM	   \
				      || (FROM) == FRAME_POINTER_REGNUM+1) \
				     && ! FRAME_POINTER_REQUIRED	   \
				     ))

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET)			\
     OFFSET = initial_elimination_offset (FROM, TO)

#define RETURN_ADDR_RTX(count, x) \
  gen_rtx_MEM (Pmode, memory_address (Pmode, plus_constant (tem, 1)))

#define PUSH_ROUNDING(NPUSHED) (NPUSHED)

#define RETURN_POPS_ARGS(FUNDECL, FUNTYPE, STACK_SIZE) 0

#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) (function_arg (&(CUM), MODE, TYPE, NAMED))

typedef struct avr_args {
  int nregs;			/* # registers available for passing */
  int regno;			/* next available register number */
} CUMULATIVE_ARGS;

#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, FNDECL, N_NAMED_ARGS) \
  init_cumulative_args (&(CUM), FNTYPE, LIBNAME, FNDECL)

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)	\
  (function_arg_advance (&CUM, MODE, TYPE, NAMED))

#define FUNCTION_ARG_REGNO_P(r) function_arg_regno_p(r)

extern int avr_reg_order[];

#define RET_REGISTER avr_ret_register ()

#define FUNCTION_VALUE(VALTYPE, FUNC) avr_function_value (VALTYPE, FUNC)

#define LIBCALL_VALUE(MODE)  avr_libcall_value (MODE)

#define FUNCTION_VALUE_REGNO_P(N) ((int) (N) == RET_REGISTER)

#define DEFAULT_PCC_STRUCT_RETURN 0

#define EPILOGUE_USES(REGNO) 0

#define HAVE_POST_INCREMENT 1
#define HAVE_PRE_DECREMENT 1

#define CONSTANT_ADDRESS_P(X) CONSTANT_P (X)

#define MAX_REGS_PER_ADDRESS 1

#ifdef REG_OK_STRICT
#  define GO_IF_LEGITIMATE_ADDRESS(mode, operand, ADDR)	\
{							\
  if (legitimate_address_p (mode, operand, 1))		\
    goto ADDR;						\
}
#  else
#  define GO_IF_LEGITIMATE_ADDRESS(mode, operand, ADDR)	\
{							\
  if (legitimate_address_p (mode, operand, 0))		\
    goto ADDR;						\
}
#endif

#define REG_OK_FOR_BASE_NOSTRICT_P(X) \
  (REGNO (X) >= FIRST_PSEUDO_REGISTER || REG_OK_FOR_BASE_STRICT_P(X))

#define REG_OK_FOR_BASE_STRICT_P(X) REGNO_OK_FOR_BASE_P (REGNO (X))

#ifdef REG_OK_STRICT
#  define REG_OK_FOR_BASE_P(X) REG_OK_FOR_BASE_STRICT_P (X)
#else
#  define REG_OK_FOR_BASE_P(X) REG_OK_FOR_BASE_NOSTRICT_P (X)
#endif

#define REG_OK_FOR_INDEX_P(X) 0

#define LEGITIMIZE_ADDRESS(X, OLDX, MODE, WIN)				\
{									\
  (X) = legitimize_address (X, OLDX, MODE);				\
  if (memory_address_p (MODE, X))					\
    goto WIN;								\
}

#define XEXP_(X,Y) (X)
#define LEGITIMIZE_RELOAD_ADDRESS(X, MODE, OPNUM, TYPE, IND_LEVELS, WIN)    \
do {									    \
  if (1&&(GET_CODE (X) == POST_INC || GET_CODE (X) == PRE_DEC))	    \
    {									    \
      push_reload (XEXP (X,0), XEXP (X,0), &XEXP (X,0), &XEXP (X,0),	    \
	           POINTER_REGS, GET_MODE (X),GET_MODE (X) , 0, 0,	    \
		   OPNUM, RELOAD_OTHER);				    \
      goto WIN;								    \
    }									    \
  if (GET_CODE (X) == PLUS						    \
      && REG_P (XEXP (X, 0))						    \
      && GET_CODE (XEXP (X, 1)) == CONST_INT				    \
      && INTVAL (XEXP (X, 1)) >= 1)					    \
    {									    \
      int fit = INTVAL (XEXP (X, 1)) <= (64 - GET_MODE_SIZE (MODE));	    \
      if (fit)								    \
	{								    \
          if (reg_equiv_address[REGNO (XEXP (X, 0))] != 0)		    \
	    {								    \
	      int regno = REGNO (XEXP (X, 0));				    \
	      rtx mem = make_memloc (X, regno);				    \
	      push_reload (XEXP (mem,0), NULL, &XEXP (mem,0), NULL,         \
		           POINTER_REGS, Pmode, VOIDmode, 0, 0,		    \
		           1, ADDR_TYPE (TYPE));			    \
	      push_reload (mem, NULL_RTX, &XEXP (X, 0), NULL,		    \
		           BASE_POINTER_REGS, GET_MODE (X), VOIDmode, 0, 0, \
		           OPNUM, TYPE);				    \
	      goto WIN;							    \
	    }								    \
	  push_reload (XEXP (X, 0), NULL_RTX, &XEXP (X, 0), NULL,	    \
		       BASE_POINTER_REGS, GET_MODE (X), VOIDmode, 0, 0,	    \
		       OPNUM, TYPE);					    \
          goto WIN;							    \
	}								    \
      else if (! (frame_pointer_needed && XEXP (X,0) == frame_pointer_rtx)) \
	{								    \
	  push_reload (X, NULL_RTX, &X, NULL,				    \
		       POINTER_REGS, GET_MODE (X), VOIDmode, 0, 0,	    \
		       OPNUM, TYPE);					    \
          goto WIN;							    \
	}								    \
    }									    \
} while(0)

#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR,LABEL)			\
      if (GET_CODE (ADDR) == POST_INC || GET_CODE (ADDR) == PRE_DEC)	\
        goto LABEL

#define LEGITIMATE_CONSTANT_P(X) 1

#define REGISTER_MOVE_COST(MODE, FROM, TO) ((FROM) == STACK_REG ? 6 \
					    : (TO) == STACK_REG ? 12 \
					    : 2)

#define MEMORY_MOVE_COST(MODE,CLASS,IN) ((MODE)==QImode ? 2 :	\
					 (MODE)==HImode ? 4 :	\
					 (MODE)==SImode ? 8 :	\
					 (MODE)==SFmode ? 8 : 16)

#define BRANCH_COST 0

#define SLOW_BYTE_ACCESS 0

#define NO_FUNCTION_CSE

#define TEXT_SECTION_ASM_OP "\t.text"

#define DATA_SECTION_ASM_OP "\t.data"

#define BSS_SECTION_ASM_OP "\t.section .bss"

/* Define the pseudo-ops used to switch to the .ctors and .dtors sections.
   There are no shared libraries on this target, and these sections are
   placed in the read-only program memory, so they are not writable.  */

#undef CTORS_SECTION_ASM_OP
#define CTORS_SECTION_ASM_OP "\t.section .ctors,\"a\",@progbits"

#undef DTORS_SECTION_ASM_OP
#define DTORS_SECTION_ASM_OP "\t.section .dtors,\"a\",@progbits"

#define TARGET_ASM_CONSTRUCTOR avr_asm_out_ctor

#define TARGET_ASM_DESTRUCTOR avr_asm_out_dtor

#define JUMP_TABLES_IN_TEXT_SECTION 0

#define ASM_COMMENT_START " ; "

#define ASM_APP_ON "/* #APP */\n"

#define ASM_APP_OFF "/* #NOAPP */\n"

/* Switch into a generic section.  */
#define TARGET_ASM_NAMED_SECTION default_elf_asm_named_section
#define TARGET_ASM_INIT_SECTIONS avr_asm_init_sections

#define ASM_OUTPUT_ASCII(FILE, P, SIZE)	 gas_output_ascii (FILE,P,SIZE)

#define IS_ASM_LOGICAL_LINE_SEPARATOR(C) ((C) == '\n'			 \
					  || ((C) == '$'))

#define ASM_OUTPUT_COMMON(STREAM, NAME, SIZE, ROUNDED)			   \
do {									   \
     fputs ("\t.comm ", (STREAM));					   \
     assemble_name ((STREAM), (NAME));					   \
     fprintf ((STREAM), ",%lu,1\n", (unsigned long)(SIZE));		   \
} while (0)

#define ASM_OUTPUT_BSS(FILE, DECL, NAME, SIZE, ROUNDED)			\
  asm_output_bss ((FILE), (DECL), (NAME), (SIZE), (ROUNDED))

#define ASM_OUTPUT_LOCAL(STREAM, NAME, SIZE, ROUNDED)			\
do {									\
     fputs ("\t.lcomm ", (STREAM));					\
     assemble_name ((STREAM), (NAME));					\
     fprintf ((STREAM), ",%d\n", (int)(SIZE));				\
} while (0)

#undef TYPE_ASM_OP
#undef SIZE_ASM_OP
#undef WEAK_ASM_OP
#define TYPE_ASM_OP	"\t.type\t"
#define SIZE_ASM_OP	"\t.size\t"
#define WEAK_ASM_OP	"\t.weak\t"
/* Define the strings used for the special svr4 .type and .size directives.
   These strings generally do not vary from one system running svr4 to
   another, but if a given system (e.g. m88k running svr) needs to use
   different pseudo-op names for these, they may be overridden in the
   file which includes this one.  */


#undef TYPE_OPERAND_FMT
#define TYPE_OPERAND_FMT	"@%s"
/* The following macro defines the format used to output the second
   operand of the .type assembler directive.  Different svr4 assemblers
   expect various different forms for this operand.  The one given here
   is just a default.  You may need to override it in your machine-
   specific tm.h file (depending upon the particulars of your assembler).  */

#define ASM_DECLARE_FUNCTION_NAME(FILE, NAME, DECL)		\
do {								\
     ASM_OUTPUT_TYPE_DIRECTIVE (FILE, NAME, "function");	\
     ASM_OUTPUT_LABEL (FILE, NAME);				\
} while (0)

#define ASM_DECLARE_FUNCTION_SIZE(FILE, FNAME, DECL)			\
  do {									\
    if (!flag_inhibit_size_directive)					\
      ASM_OUTPUT_MEASURED_SIZE (FILE, FNAME);				\
  } while (0)

#define ASM_DECLARE_OBJECT_NAME(FILE, NAME, DECL)			\
do {									\
  ASM_OUTPUT_TYPE_DIRECTIVE (FILE, NAME, "object");			\
  size_directive_output = 0;						\
  if (!flag_inhibit_size_directive && DECL_SIZE (DECL))			\
    {									\
      size_directive_output = 1;					\
      ASM_OUTPUT_SIZE_DIRECTIVE (FILE, NAME,				\
				 int_size_in_bytes (TREE_TYPE (DECL)));	\
    }									\
  ASM_OUTPUT_LABEL(FILE, NAME);						\
} while (0)

#undef ASM_FINISH_DECLARE_OBJECT
#define ASM_FINISH_DECLARE_OBJECT(FILE, DECL, TOP_LEVEL, AT_END)	 \
do {									 \
     const char *name = XSTR (XEXP (DECL_RTL (DECL), 0), 0);		 \
     HOST_WIDE_INT size;						 \
     if (!flag_inhibit_size_directive && DECL_SIZE (DECL)		 \
         && ! AT_END && TOP_LEVEL					 \
	 && DECL_INITIAL (DECL) == error_mark_node			 \
	 && !size_directive_output)					 \
       {								 \
	 size_directive_output = 1;					 \
	 size = int_size_in_bytes (TREE_TYPE (DECL));			 \
	 ASM_OUTPUT_SIZE_DIRECTIVE (FILE, name, size);			 \
       }								 \
   } while (0)


#define ESCAPES \
"\1\1\1\1\1\1\1\1btn\1fr\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\0\0\"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\
\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\\\0\0\0\
\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1"
/* A table of bytes codes used by the ASM_OUTPUT_ASCII and
   ASM_OUTPUT_LIMITED_STRING macros.  Each byte in the table
   corresponds to a particular byte value [0..255].  For any
   given byte value, if the value in the corresponding table
   position is zero, the given character can be output directly.
   If the table value is 1, the byte must be output as a \ooo
   octal escape.  If the tables value is anything else, then the
   byte value should be output as a \ followed by the value
   in the table.  Note that we can use standard UN*X escape
   sequences for many control characters, but we don't use
   \a to represent BEL because some svr4 assemblers (e.g. on
   the i386) don't know about that.  Also, we don't use \v
   since some versions of gas, such as 2.2 did not accept it.  */

#define STRING_LIMIT	((unsigned) 64)
#define STRING_ASM_OP	"\t.string\t"
/* Some svr4 assemblers have a limit on the number of characters which
   can appear in the operand of a .string directive.  If your assembler
   has such a limitation, you should define STRING_LIMIT to reflect that
   limit.  Note that at least some svr4 assemblers have a limit on the
   actual number of bytes in the double-quoted string, and that they
   count each character in an escape sequence as one byte.  Thus, an
   escape sequence like \377 would count as four bytes.

   If your target assembler doesn't support the .string directive, you
   should define this to zero.  */

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP ".global\t"

#define SET_ASM_OP	"\t.set\t"

#define ASM_WEAKEN_LABEL(FILE, NAME)	\
  do					\
    {					\
      fputs ("\t.weak\t", (FILE));	\
      assemble_name ((FILE), (NAME));	\
      fputc ('\n', (FILE));		\
    }					\
  while (0)

#define SUPPORTS_WEAK 1

#define ASM_GENERATE_INTERNAL_LABEL(STRING, PREFIX, NUM)	\
sprintf (STRING, "*.%s%lu", PREFIX, (unsigned long)(NUM))

#define HAS_INIT_SECTION 1

#define REGISTER_NAMES {				\
  "r0","r1","r2","r3","r4","r5","r6","r7",		\
    "r8","r9","r10","r11","r12","r13","r14","r15",	\
    "r16","r17","r18","r19","r20","r21","r22","r23",	\
    "r24","r25","r26","r27","r28","r29","r30","r31",	\
    "__SPL__","__SPH__","argL","argH"}

#define FINAL_PRESCAN_INSN(insn, operand, nop) final_prescan_insn (insn, operand,nop)

#define PRINT_OPERAND(STREAM, X, CODE) print_operand (STREAM, X, CODE)

#define PRINT_OPERAND_PUNCT_VALID_P(CODE) ((CODE) == '~')

#define PRINT_OPERAND_ADDRESS(STREAM, X) print_operand_address(STREAM, X)

#define USER_LABEL_PREFIX ""

#define ASSEMBLER_DIALECT AVR_HAVE_MOVW

#define ASM_OUTPUT_REG_PUSH(STREAM, REGNO)	\
{						\
  gcc_assert (REGNO < 32);			\
  fprintf (STREAM, "\tpush\tr%d", REGNO);	\
}

#define ASM_OUTPUT_REG_POP(STREAM, REGNO)	\
{						\
  gcc_assert (REGNO < 32);			\
  fprintf (STREAM, "\tpop\tr%d", REGNO);	\
}

#define ASM_OUTPUT_ADDR_VEC_ELT(STREAM, VALUE)		\
  avr_output_addr_vec_elt(STREAM, VALUE)

#define ASM_OUTPUT_CASE_LABEL(STREAM, PREFIX, NUM, TABLE) \
  (switch_to_section (progmem_section), \
   (*targetm.asm_out.internal_label) (STREAM, PREFIX, NUM))

#define ASM_OUTPUT_SKIP(STREAM, N)		\
fprintf (STREAM, "\t.skip %lu,0\n", (unsigned long)(N))

#define ASM_OUTPUT_ALIGN(STREAM, POWER)			\
  do {							\
      if ((POWER) > 1)					\
          fprintf (STREAM, "\t.p2align\t%d\n", POWER);	\
  } while (0)

#define CASE_VECTOR_MODE HImode

extern int avr_case_values_threshold;

#define CASE_VALUES_THRESHOLD avr_case_values_threshold

#undef WORD_REGISTER_OPERATIONS

#define MOVE_MAX 4

#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

#define Pmode HImode

#define FUNCTION_MODE HImode

#define DOLLARS_IN_IDENTIFIERS 0

#define NO_DOLLAR_IN_LABEL 1

#define TRAMPOLINE_TEMPLATE(FILE) \
  internal_error ("trampolines not supported")

#define TRAMPOLINE_SIZE 4

#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT)			      \
{									      \
  emit_move_insn (gen_rtx_MEM (HImode, plus_constant ((TRAMP), 2)), CXT);    \
  emit_move_insn (gen_rtx_MEM (HImode, plus_constant ((TRAMP), 6)), FNADDR); \
}
/* Store in cc_status the expressions
   that the condition codes will describe
   after execution of an instruction whose pattern is EXP.
   Do not alter them if the instruction would not alter the cc's.  */

#define NOTICE_UPDATE_CC(EXP, INSN) notice_update_cc(EXP, INSN)

/* The add insns don't set overflow in a usable way.  */
#define CC_OVERFLOW_UNUSABLE 01000
/* The mov,and,or,xor insns don't set carry.  That's ok though as the
   Z bit is all we need when doing unsigned comparisons on the result of
   these insns (since they're always with 0).  However, conditions.h has
   CC_NO_OVERFLOW defined for this purpose.  Rename it to something more
   understandable.  */
#define CC_NO_CARRY CC_NO_OVERFLOW


/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */

#define FUNCTION_PROFILER(FILE, LABELNO)  \
  fprintf (FILE, "/* profiler %d */", (LABELNO))

#define ADJUST_INSN_LENGTH(INSN, LENGTH) (LENGTH =\
					  adjust_insn_length (INSN, LENGTH))

#define CPP_SPEC "%{posix:-D_POSIX_SOURCE}"

#define CC1_SPEC "%{profile:-p}"

#define CC1PLUS_SPEC "%{!frtti:-fno-rtti} \
    %{!fenforce-eh-specs:-fno-enforce-eh-specs} \
    %{!fexceptions:-fno-exceptions}"
/* A C string constant that tells the GCC drvier program options to
   pass to `cc1plus'.  */

#define ASM_SPEC "%{mmcu=avr25:-mmcu=avr2;\
mmcu=*:-mmcu=%*}"

#define LINK_SPEC " %{!mmcu*:-m avr2}\
%{mmcu=at90s1200|\
  mmcu=attiny11|\
  mmcu=attiny12|\
  mmcu=attiny15|\
  mmcu=attiny28:-m avr1}\
%{mmcu=attiny22|\
  mmcu=attiny26|\
  mmcu=at90s2*|\
  mmcu=at90s4*|\
  mmcu=at90s8*|\
  mmcu=at90c8*|\
  mmcu=at86rf401|\
  mmcu=attiny13|\
  mmcu=attiny2313|\
  mmcu=attiny24|\
  mmcu=attiny25|\
  mmcu=attiny261|\
  mmcu=attiny4*|\
  mmcu=attiny8*:-m avr2}\
%{mmcu=atmega103|\
  mmcu=atmega603|\
  mmcu=at43*|\
  mmcu=at76*:-m avr3}\
%{mmcu=atmega8*|\
  mmcu=atmega48|\
  mmcu=at90pwm*:-m avr4}\
%{mmcu=atmega16*|\
  mmcu=atmega32*|\
  mmcu=atmega406|\
  mmcu=atmega64*|\
  mmcu=atmega128*|\
  mmcu=at90can*|\
  mmcu=at90usb*|\
  mmcu=at94k:-m avr5}\
%{mmcu=atmega324*|\
  mmcu=atmega325*|\
  mmcu=atmega329*|\
  mmcu=atmega406|\
  mmcu=atmega48|\
  mmcu=atmega88|\
  mmcu=atmega64|\
  mmcu=atmega644*|\
  mmcu=atmega645*|\
  mmcu=atmega649*|\
  mmcu=atmega128|\
  mmcu=atmega162|\
  mmcu=atmega164*|\
  mmcu=atmega165*|\
  mmcu=atmega168|\
  mmcu=atmega169*|\
  mmcu=atmega8hva|\
  mmcu=atmega16hva|\
  mmcu=at90can*|\
  mmcu=at90pwm*|\
  mmcu=at90usb*: -Tdata 0x800100}\
%{mmcu=atmega640|\
  mmcu=atmega1280|\
  mmcu=atmega1281: -Tdata 0x800200} "

#define LIB_SPEC \
  "%{!mmcu=at90s1*:%{!mmcu=attiny11:%{!mmcu=attiny12:%{!mmcu=attiny15:%{!mmcu=attiny28: -lc }}}}}"

#define LIBSTDCXX "-lgcc"
/* No libstdc++ for now.  Empty string doesn't work.  */

#define LIBGCC_SPEC \
  "%{!mmcu=at90s1*:%{!mmcu=attiny11:%{!mmcu=attiny12:%{!mmcu=attiny15:%{!mmcu=attiny28: -lgcc }}}}}"

#define STARTFILE_SPEC "%(crt_binutils)"

#define ENDFILE_SPEC ""

#define CRT_BINUTILS_SPECS "\
%{mmcu=at90s1200|mmcu=avr1:crts1200.o%s} \
%{mmcu=attiny11:crttn11.o%s} \
%{mmcu=attiny12:crttn12.o%s} \
%{mmcu=attiny15:crttn15.o%s} \
%{mmcu=attiny28:crttn28.o%s} \
%{!mmcu*|mmcu=at90s8515|mmcu=avr2:crts8515.o%s} \
%{mmcu=at90s2313:crts2313.o%s} \
%{mmcu=at90s2323:crts2323.o%s} \
%{mmcu=at90s2333:crts2333.o%s} \
%{mmcu=at90s2343:crts2343.o%s} \
%{mmcu=attiny22:crttn22.o%s} \
%{mmcu=attiny26:crttn26.o%s} \
%{mmcu=at90s4433:crts4433.o%s} \
%{mmcu=at90s4414:crts4414.o%s} \
%{mmcu=at90s4434:crts4434.o%s} \
%{mmcu=at90c8534:crtc8534.o%s} \
%{mmcu=at90s8535:crts8535.o%s} \
%{mmcu=at86rf401:crt86401.o%s} \
%{mmcu=attiny13:crttn13.o%s} \
%{mmcu=attiny2313|mmcu=avr25:crttn2313.o%s} \
%{mmcu=attiny24:crttn24.o%s} \
%{mmcu=attiny44:crttn44.o%s} \
%{mmcu=attiny84:crttn84.o%s} \
%{mmcu=attiny25:crttn25.o%s} \
%{mmcu=attiny45:crttn45.o%s} \
%{mmcu=attiny85:crttn85.o%s} \
%{mmcu=attiny261:crttn261.o%s} \
%{mmcu=attiny461:crttn461.o%s} \
%{mmcu=attiny861:crttn861.o%s} \
%{mmcu=atmega103|mmcu=avr3:crtm103.o%s} \
%{mmcu=atmega603:crtm603.o%s} \
%{mmcu=at43usb320:crt43320.o%s} \
%{mmcu=at43usb355:crt43355.o%s} \
%{mmcu=at76c711:crt76711.o%s} \
%{mmcu=atmega8|mmcu=avr4:crtm8.o%s} \
%{mmcu=atmega48:crtm48.o%s} \
%{mmcu=atmega88:crtm88.o%s} \
%{mmcu=atmega8515:crtm8515.o%s} \
%{mmcu=atmega8535:crtm8535.o%s} \
%{mmcu=at90pwm1:crt90pwm1.o%s} \
%{mmcu=at90pwm2:crt90pwm2.o%s} \
%{mmcu=at90pwm3:crt90pwm3.o%s} \
%{mmcu=atmega16:crtm16.o%s} \
%{mmcu=atmega161|mmcu=avr5:crtm161.o%s} \
%{mmcu=atmega162:crtm162.o%s} \
%{mmcu=atmega163:crtm163.o%s} \
%{mmcu=atmega164p:crtm164p.o%s} \
%{mmcu=atmega165:crtm165.o%s} \
%{mmcu=atmega165p:crtm165p.o%s} \
%{mmcu=atmega168:crtm168.o%s} \
%{mmcu=atmega169:crtm169.o%s} \
%{mmcu=atmega169p:crtm169p.o%s} \
%{mmcu=atmega32:crtm32.o%s} \
%{mmcu=atmega323:crtm323.o%s} \
%{mmcu=atmega324p:crtm324p.o%s} \
%{mmcu=atmega325:crtm325.o%s} \
%{mmcu=atmega325p:crtm325p.o%s} \
%{mmcu=atmega3250:crtm3250.o%s} \
%{mmcu=atmega3250p:crtm3250p.o%s} \
%{mmcu=atmega329:crtm329.o%s} \
%{mmcu=atmega329p:crtm329p.o%s} \
%{mmcu=atmega3290:crtm3290.o%s} \
%{mmcu=atmega3290p:crtm3290p.o%s} \
%{mmcu=atmega406:crtm406.o%s} \
%{mmcu=atmega64:crtm64.o%s} \
%{mmcu=atmega640:crtm640.o%s} \
%{mmcu=atmega644:crtm644.o%s} \
%{mmcu=atmega644p:crtm644p.o%s} \
%{mmcu=atmega645:crtm645.o%s} \
%{mmcu=atmega6450:crtm6450.o%s} \
%{mmcu=atmega649:crtm649.o%s} \
%{mmcu=atmega6490:crtm6490.o%s} \
%{mmcu=atmega128:crtm128.o%s} \
%{mmcu=atmega1280:crtm1280.o%s} \
%{mmcu=atmega1281:crtm1281.o%s} \
%{mmcu=atmega8hva:crtm8hva.o%s} \
%{mmcu=atmega16hva:crtm16hva.o%s} \
%{mmcu=at90can32:crtcan32.o%s} \
%{mmcu=at90can64:crtcan64.o%s} \
%{mmcu=at90can128:crtcan128.o%s} \
%{mmcu=at90usb82:crtusb82.o%s} \
%{mmcu=at90usb162:crtusb162.o%s} \
%{mmcu=at90usb646:crtusb646.o%s} \
%{mmcu=at90usb647:crtusb647.o%s} \
%{mmcu=at90usb1286:crtusb1286.o%s} \
%{mmcu=at90usb1287:crtusb1287.o%s} \
%{mmcu=at94k:crtat94k.o%s}"

#define EXTRA_SPECS {"crt_binutils", CRT_BINUTILS_SPECS},

/* This is the default without any -mmcu=* option (AT90S*).  */
#define MULTILIB_DEFAULTS { "mmcu=avr2" }

/* This is undefined macro for collect2 disabling */
#define LINKER_NAME "ld"

#define TEST_HARD_REG_CLASS(CLASS, REGNO) \
  TEST_HARD_REG_BIT (reg_class_contents[ (int) (CLASS)], REGNO)

/* Note that the other files fail to use these
   in some of the places where they should.  */

#if defined(__STDC__) || defined(ALMOST_STDC)
#define AS2(a,b,c) #a " " #b "," #c
#define AS2C(b,c) " " #b "," #c
#define AS3(a,b,c,d) #a " " #b "," #c "," #d
#define AS1(a,b) #a " " #b
#else
#define AS1(a,b) "a	b"
#define AS2(a,b,c) "a	b,c"
#define AS2C(b,c) " b,c"
#define AS3(a,b,c,d) "a	b,c,d"
#endif
#define OUT_AS1(a,b) output_asm_insn (AS1(a,b), operands)
#define OUT_AS2(a,b,c) output_asm_insn (AS2(a,b,c), operands)
#define CR_TAB "\n\t"

#define PREFERRED_DEBUGGING_TYPE DBX_DEBUG

#define DWARF2_DEBUGGING_INFO 1

#define DWARF2_ADDR_SIZE 4

#define OBJECT_FORMAT_ELF
