/* Target Definitions for MorphoRISC1
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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

extern struct rtx_def * mt_ucmpsi3_libcall;

enum processor_type
{
  PROCESSOR_MS1_64_001,
  PROCESSOR_MS1_16_002,
  PROCESSOR_MS1_16_003,
  PROCESSOR_MS2
};

enum epilogue_type
{
  EH_EPILOGUE,
  NORMAL_EPILOGUE
};

extern enum processor_type mt_cpu;


/* Support for a compile-time default CPU, et cetera.  The rules are:
   --with-arch is ignored if -march is specified.  */
#define OPTION_DEFAULT_SPECS \
  {"arch", "%{!march=*:-march=%(VALUE)}" }

/* A C string constant that tells the GCC driver program options to pass to
   the assembler.  */
#undef  ASM_SPEC
#define ASM_SPEC "%{march=*} %{!march=*: -march=ms1-16-002}"

/* A string to pass to at the end of the command given to the linker.  */
#undef  LIB_SPEC
#define LIB_SPEC "--start-group -lc -lsim --end-group \
%{msim: ; \
march=ms1-64-001:-T 64-001.ld%s; \
march=ms1-16-002:-T 16-002.ld%s; \
march=ms1-16-003:-T 16-003.ld%s; \
march=ms2:-T ms2.ld%s; \
	 :-T 16-002.ld}"

/* A string to pass at the very beginning of the command given to the
   linker.  */
#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "%{msim:crt0.o%s;\
march=ms1-64-001:%{!mno-crt0:crt0-64-001.o%s} startup-64-001.o%s; \
march=ms1-16-002:%{!mno-crt0:crt0-16-002.o%s} startup-16-002.o%s; \
march=ms1-16-003:%{!mno-crt0:crt0-16-003.o%s} startup-16-003.o%s; \
march=ms2:%{!mno-crt0:crt0-ms2.o%s} startup-ms2.o%s; \
	 :%{!mno-crt0:crt0-16-002.o%s} startup-16-002.o%s} \
crti.o%s crtbegin.o%s"

/* A string to pass at the end of the command given to the linker.  */
#undef  ENDFILE_SPEC
#define ENDFILE_SPEC "%{msim:exit.o%s; \
march=ms1-64-001:exit-64-001.o%s; \
march=ms1-16-002:exit-16-002.o%s; \
march=ms1-16-003:exit-16-003.o%s; \
march=ms2:exit-ms2.o%s; \
	 :exit-16-002.o%s} \
 crtend.o%s crtn.o%s"

/* Run-time target specifications.  */

#define TARGET_CPU_CPP_BUILTINS() 		\
  do						\
    {						\
      builtin_define_with_int_value ("__mt__", mt_cpu);	\
      builtin_assert ("machine=mt");		\
    }						\
  while (0)

#define TARGET_MS1_64_001 (mt_cpu == PROCESSOR_MS1_64_001)
#define TARGET_MS1_16_002 (mt_cpu == PROCESSOR_MS1_16_002)
#define TARGET_MS1_16_003 (mt_cpu == PROCESSOR_MS1_16_003)
#define TARGET_MS2 (mt_cpu == PROCESSOR_MS2)

#define TARGET_VERSION  fprintf (stderr, " (mt)");

#define OVERRIDE_OPTIONS mt_override_options ()

#define CAN_DEBUG_WITHOUT_FP 1


/* Storage Layout.  */

#define BITS_BIG_ENDIAN 0

#define BYTES_BIG_ENDIAN 1

#define WORDS_BIG_ENDIAN 1

#define UNITS_PER_WORD 4

/* A macro to update MODE and UNSIGNEDP when an object whose type is TYPE and
   which has the specified mode and signedness is to be stored in a register.
   This macro is only called when TYPE is a scalar type.

   On most RISC machines, which only have operations that operate on a full
   register, define this macro to set M to `word_mode' if M is an integer mode
   narrower than `BITS_PER_WORD'.  In most cases, only integer modes should be
   widened because wider-precision floating-point operations are usually more
   expensive than their narrower counterparts.

   For most machines, the macro definition does not change UNSIGNEDP.  However,
   some machines, have instructions that preferentially handle either signed or
   unsigned quantities of certain modes.  For example, on the DEC Alpha, 32-bit
   loads from memory and 32-bit add instructions sign-extend the result to 64
   bits.  On such machines, set UNSIGNEDP according to which kind of extension
   is more efficient.

   Do not define this macro if it would never modify MODE.  */
#define PROMOTE_MODE(MODE,UNSIGNEDP,TYPE)			\
  do								\
    {								\
      if (GET_MODE_CLASS (MODE) == MODE_INT			\
	  && GET_MODE_SIZE (MODE) < 4)				\
	(MODE) = SImode;					\
    }								\
  while (0)

/* Normal alignment required for function parameters on the stack, in bits.
   All stack parameters receive at least this much alignment regardless of data
   type.  On most machines, this is the same as the size of an integer.  */
#define PARM_BOUNDARY 32

/* Define this macro to the minimum alignment enforced by hardware for
   the stack pointer on this machine.  The definition is a C
   expression for the desired alignment (measured in bits).  This
   value is used as a default if PREFERRED_STACK_BOUNDARY is not
   defined.  On most machines, this should be the same as
   PARM_BOUNDARY.  */
#define STACK_BOUNDARY 32

/* Alignment required for a function entry point, in bits.  */
#define FUNCTION_BOUNDARY 32

/* Biggest alignment that any data type can require on this machine,
   in bits.  */
#define BIGGEST_ALIGNMENT 32

/* If defined, a C expression to compute the alignment for a variable
   in the static store.  TYPE is the data type, and ALIGN is the
   alignment that the object would ordinarily have.  The value of this
   macro is used instead of that alignment to align the object.

   If this macro is not defined, then ALIGN is used.  */
#define DATA_ALIGNMENT(TYPE, ALIGN)		\
  (TREE_CODE (TYPE) == ARRAY_TYPE		\
   && TYPE_MODE (TREE_TYPE (TYPE)) == QImode	\
   && (ALIGN) < BITS_PER_WORD ? BITS_PER_WORD : (ALIGN))

/* If defined, a C expression to compute the alignment given to a constant that
   is being placed in memory.  CONSTANT is the constant and ALIGN is the
   alignment that the object would ordinarily have.  The value of this macro is
   used instead of that alignment to align the object.

   If this macro is not defined, then ALIGN is used.

   The typical use of this macro is to increase alignment for string constants
   to be word aligned so that `strcpy' calls that copy constants can be done
   inline.  */
#define CONSTANT_ALIGNMENT(EXP, ALIGN)  \
  (TREE_CODE (EXP) == STRING_CST	\
   && (ALIGN) < BITS_PER_WORD ? BITS_PER_WORD : (ALIGN))

/* Number of bits which any structure or union's size must be a multiple of.
   Each structure or union's size is rounded up to a multiple of this.

   If you do not define this macro, the default is the same as `BITS_PER_UNIT'.  */
#define STRUCTURE_SIZE_BOUNDARY 32

/* Define this macro to be the value 1 if instructions will fail to work if
   given data not on the nominal alignment.  If instructions will merely go
   slower in that case, define this macro as 0.  */
#define STRICT_ALIGNMENT 1

/* Define this if you wish to imitate the way many other C compilers handle
   alignment of bitfields and the structures that contain them.  */
#define PCC_BITFIELD_TYPE_MATTERS 1

/* Layout of Source Language Data Types.  */

#define INT_TYPE_SIZE 32

#define SHORT_TYPE_SIZE 16

#define LONG_TYPE_SIZE 32

#define LONG_LONG_TYPE_SIZE 64

#define CHAR_TYPE_SIZE 8

#define FLOAT_TYPE_SIZE 32

#define DOUBLE_TYPE_SIZE 64

#define LONG_DOUBLE_TYPE_SIZE 64

#define DEFAULT_SIGNED_CHAR 1

/* Register Basics.  */

/* General purpose registers.  */
#define GPR_FIRST       0               /* First gpr */
#define GPR_LAST        15		/* Last possible gpr */

#define GPR_R0          0		/* Always 0 */
#define GPR_R7          7		/* Used as a scratch register */
#define GPR_R8          8		/* Used as a scratch register */
#define GPR_R9          9		/* Used as a scratch register */
#define GPR_R10         10		/* Used as a scratch register */
#define GPR_R11         11		/* Used as a scratch register */
#define GPR_FP          12		/* Frame pointer */
#define GPR_SP          13	        /* Stack pointer */
#define GPR_LINK	14		/* Saved return address as
					   seen  by the caller */
#define GPR_INTERRUPT_LINK 15		/* hold return addres for interrupts */

#define LOOP_FIRST         (GPR_LAST + 1)
#define LOOP_LAST	   (LOOP_FIRST + 3)

/* Argument register that is eliminated in favor of the frame and/or stack
   pointer.  Also add register to point to where the return address is
   stored.  */
#define SPECIAL_REG_FIRST		(LOOP_LAST + 1)
#define SPECIAL_REG_LAST		(SPECIAL_REG_FIRST)
#define ARG_POINTER_REGNUM		(SPECIAL_REG_FIRST + 0)
#define SPECIAL_REG_P(R)		((R) == SPECIAL_REG_FIRST)

/* The first/last register that can contain the arguments to a function.  */
#define FIRST_ARG_REGNUM	1
#define LAST_ARG_REGNUM		4

/* The register used to hold functions return value */
#define RETVAL_REGNUM		11

#define FIRST_PSEUDO_REGISTER (SPECIAL_REG_LAST + 1)

#define IS_PSEUDO_P(R)	(REGNO (R) >= FIRST_PSEUDO_REGISTER)

/* R0		always has the value 0
   R10          static link
   R12	FP	pointer to active frame
   R13	SP	pointer to top of stack
   R14	RA	return address
   R15	IRA	interrupt return address.  */
#define FIXED_REGISTERS { 1, 0, 0, 0, 0, 0, 0, 0, \
			  0, 0, 0, 0, 1, 1, 1, 1, \
			  1, 1, 1, 1, 1		  \
			 }

/* Like `FIXED_REGISTERS' but has 1 for each register that is clobbered (in
   general) by function calls as well as for fixed registers.  This macro
   therefore identifies the registers that are not available for general
   allocation of values that must live across function calls.  */
#define CALL_USED_REGISTERS	{ 1, 1, 1, 1, 1, 0, 0, 1, \
				  1, 1, 1, 1, 1, 1, 1, 1, \
				  1, 1, 1, 1, 1		  \
				}


/* How Values Fit in Registers.  */

#define HARD_REGNO_NREGS(REGNO, MODE) 				\
  ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

#define HARD_REGNO_MODE_OK(REGNO, MODE) 1

/* A C expression that is nonzero if a value of mode MODE1 is
   accessible in mode MODE2 without copying.  */
#define MODES_TIEABLE_P(MODE1, MODE2) 1

/* Register Classes.  */

enum reg_class
{
  NO_REGS,
  ALL_REGS,
  LIM_REG_CLASSES
};

#define   GENERAL_REGS	ALL_REGS

#define N_REG_CLASSES ((int) LIM_REG_CLASSES)

#define REG_CLASS_NAMES {"NO_REGS", "ALL_REGS" }

#define REG_CLASS_CONTENTS \
   {								\
     { 0x0 },							\
     { 0x000fffff },						\
   }

/* A C expression whose value is a register class containing hard register
   REGNO.  In general there is more than one such class; choose a class which
   is "minimal", meaning that no smaller class also contains the register.  */
#define REGNO_REG_CLASS(REGNO) GENERAL_REGS

#define BASE_REG_CLASS GENERAL_REGS

#define INDEX_REG_CLASS NO_REGS

#define REG_CLASS_FROM_LETTER(CHAR) NO_REGS

#define REGNO_OK_FOR_BASE_P(NUM) 1

#define REGNO_OK_FOR_INDEX_P(NUM) 1

/* A C expression that places additional restrictions on the register class to
   use when it is necessary to copy value X into a register in class CLASS.
   The value is a register class; perhaps CLASS, or perhaps another, smaller
   class.  On many machines, the following definition is safe:

        #define PREFERRED_RELOAD_CLASS(X,CLASS) CLASS
*/
#define PREFERRED_RELOAD_CLASS(X, CLASS) (CLASS)

#define SECONDARY_RELOAD_CLASS(CLASS,MODE,X) \
  mt_secondary_reload_class((CLASS), (MODE), (X))

/* A C expression for the maximum number of consecutive registers of
   class CLASS needed to hold a value of mode MODE.  */
#define CLASS_MAX_NREGS(CLASS, MODE) \
  ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* For MorphoRISC1:

   `I'	is used for the range of constants an arithmetic insn can
	actually contain (16 bits signed integers).

   `J'	is used for the range which is just zero (ie, $r0).

   `K'	is used for the range of constants a logical insn can actually
	contain (16 bit zero-extended integers).

   `L'	is used for the range of constants that be loaded with lui
	(ie, the bottom 16 bits are zero).

   `M'	is used for the range of constants that take two words to load
	(ie, not matched by `I', `K', and `L').

   `N'	is used for negative 16 bit constants other than -65536.

   `O'	is a 15 bit signed integer.

   `P'	is used for positive 16 bit constants.  */

#define SMALL_INT(X) ((unsigned HOST_WIDE_INT) (INTVAL (X) + 0x8000) < 0x10000)
#define SMALL_INT_UNSIGNED(X) ((unsigned HOST_WIDE_INT) (INTVAL (X)) < 0x10000)

/* A C expression that defines the machine-dependent operand
   constraint letters that specify particular ranges of integer
   values.  If C is one of those letters, the expression should check
   that VALUE, an integer, is in the appropriate range and return 1 if
   so, 0 otherwise.  If C is not one of those letters, the value
   should be 0 regardless of VALUE.  */
#define CONST_OK_FOR_LETTER_P(VALUE, C)					\
  ((C) == 'I' ? ((unsigned HOST_WIDE_INT) ((VALUE) + 0x8000) < 0x10000)	\
   : (C) == 'J' ? ((VALUE) == 0)					\
   : (C) == 'K' ? ((unsigned HOST_WIDE_INT) (VALUE) < 0x10000)		\
   : (C) == 'L' ? (((VALUE) & 0x0000ffff) == 0				\
		   && (((VALUE) & ~2147483647) == 0			\
		       || ((VALUE) & ~2147483647) == ~2147483647))	\
   : (C) == 'M' ? ((((VALUE) & ~0x0000ffff) != 0)			\
		   && (((VALUE) & ~0x0000ffff) != ~0x0000ffff)		\
		   && (((VALUE) & 0x0000ffff) != 0			\
		       || (((VALUE) & ~2147483647) != 0			\
			   && ((VALUE) & ~2147483647) != ~2147483647)))	\
   : (C) == 'N' ? ((unsigned HOST_WIDE_INT) ((VALUE) + 0xffff) < 0xffff) \
   : (C) == 'O' ? ((unsigned HOST_WIDE_INT) ((VALUE) + 0x4000) < 0x8000) \
   : (C) == 'P' ? ((VALUE) != 0 && (((VALUE) & ~0x0000ffff) == 0))	\
   : 0)

/* A C expression that defines the machine-dependent operand constraint letters
   (`G', `H') that specify particular ranges of `const_double' values.  */
#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C) 0

/* Most negative value represent on mt */
#define MT_MIN_INT 0x80000000

/* Basic Stack Layout.  */

enum save_direction
{
  FROM_PROCESSOR_TO_MEM,
  FROM_MEM_TO_PROCESSOR
};

/* Tell prologue and epilogue if register REGNO should be saved / restored.
   The return address and frame pointer are treated separately.
   Don't consider them here.  */
#define MUST_SAVE_REGISTER(regno)				\
  (   (regno) != GPR_LINK 					\
   && (regno) != GPR_FP		  				\
   && (regno) != GPR_SP		  				\
   && (regno) != GPR_R0		  				\
   &&   (( regs_ever_live [regno] && ! call_used_regs [regno] ) \
       /* Save ira register in an interrupt handler.  */	\
	|| (interrupt_handler && (regno) == GPR_INTERRUPT_LINK)	\
       /* Save any register used in an interrupt handler.  */	\
	|| (interrupt_handler && regs_ever_live [regno])	\
       /* Save call clobbered registers in non-leaf interrupt	\
	  handlers.  */						\
	|| (interrupt_handler && call_used_regs[regno] 		\
	   && !current_function_is_leaf)			\
	||(current_function_calls_eh_return			\
	   && (regno == GPR_R7 || regno == GPR_R8))		\
	)							\
  )

#define STACK_GROWS_DOWNWARD 1

/* Offset from the frame pointer to the first local variable slot to be
   allocated.

   If `FRAME_GROWS_DOWNWARD', find the next slot's offset by
   subtracting the first slot's length from `STARTING_FRAME_OFFSET'.
   Otherwise, it is found by adding the length of the first slot to
   the value `STARTING_FRAME_OFFSET'.  */
#define STARTING_FRAME_OFFSET current_function_outgoing_args_size

/* Offset from the argument pointer register to the first argument's address.
   On some machines it may depend on the data type of the function.

   If `ARGS_GROW_DOWNWARD', this is the offset to the location above the first
   argument's address.  */
#define FIRST_PARM_OFFSET(FUNDECL) 0

#define RETURN_ADDR_RTX(COUNT, FRAMEADDR) 				\
    mt_return_addr_rtx (COUNT)

/* A C expression whose value is RTL representing the location of the incoming
   return address at the beginning of any function, before the prologue.  This
   RTL is either a `REG', indicating that the return value is saved in `REG',
   or a `MEM' representing a location in the stack.

   You only need to define this macro if you want to support call frame
   debugging information like that provided by DWARF 2.  */
#define INCOMING_RETURN_ADDR_RTX gen_rtx_REG (SImode, GPR_LINK)

/* A C expression whose value is an integer giving the offset, in bytes, from
   the value of the stack pointer register to the top of the stack frame at the
   beginning of any function, before the prologue.  The top of the frame is
   defined to be the value of the stack pointer in the previous frame, just
   before the call instruction.

   You only need to define this macro if you want to support call frame
   debugging information like that provided by DWARF 2.  */
#define INCOMING_FRAME_SP_OFFSET 0

#define STACK_POINTER_REGNUM GPR_SP

#define FRAME_POINTER_REGNUM GPR_FP

/* The register number of the arg pointer register, which is used to
   access the function's argument list.  */
#define ARG_POINTER_REGNUM		(SPECIAL_REG_FIRST + 0)

/* Register numbers used for passing a function's static chain pointer.  */
#define STATIC_CHAIN_REGNUM 10

/* A C expression which is nonzero if a function must have and use a frame
   pointer.  */
#define FRAME_POINTER_REQUIRED 0

/* Structure to be filled in by compute_frame_size with register
   save masks, and offsets for the current function.  */

struct mt_frame_info
{
  unsigned int total_size;      /* # Bytes that the entire frame takes up.  */
  unsigned int pretend_size;    /* # Bytes we push and pretend caller did.  */
  unsigned int args_size;       /* # Bytes that outgoing arguments take up.  */
  unsigned int extra_size;
  unsigned int reg_size;        /* # Bytes needed to store regs.  */
  unsigned int var_size;        /* # Bytes that variables take up.  */
  unsigned int frame_size;      /* # Bytes in current frame.  */
  unsigned int reg_mask;        /* Mask of saved registers.  */
  unsigned int save_fp;         /* Nonzero if frame pointer must be saved.  */
  unsigned int save_lr;         /* Nonzero if return pointer must be saved.  */
  int          initialized;     /* Nonzero if frame size already calculated.  */
}; 

extern struct mt_frame_info current_frame_info;

/* If defined, this macro specifies a table of register pairs used to eliminate
   unneeded registers that point into the stack frame.  */
#define ELIMINABLE_REGS							\
{									\
  {ARG_POINTER_REGNUM,	 STACK_POINTER_REGNUM},				\
  {ARG_POINTER_REGNUM,	 FRAME_POINTER_REGNUM},				\
  {FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM}				\
}

/* A C expression that returns nonzero if the compiler is allowed to try to
   replace register number FROM with register number TO.  */
#define CAN_ELIMINATE(FROM, TO)						\
 ((FROM) == ARG_POINTER_REGNUM && (TO) == STACK_POINTER_REGNUM		\
  ? ! frame_pointer_needed						\
  : 1)

/* This macro is similar to `INITIAL_FRAME_POINTER_OFFSET'.  It
   specifies the initial difference between the specified pair of
   registers.  This macro must be defined if `ELIMINABLE_REGS' is
   defined.  */
#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET)			\
  (OFFSET) = mt_initial_elimination_offset (FROM, TO)

/* If defined, the maximum amount of space required for outgoing
   arguments will be computed and placed into the variable
   `current_function_outgoing_args_size'.  */
#define ACCUMULATE_OUTGOING_ARGS 1

/* Define this if it is the responsibility of the caller to
   allocate the area reserved for arguments passed in registers.  */
#define OUTGOING_REG_PARM_STACK_SPACE

/* The number of register assigned to holding function arguments.  */
#define MT_NUM_ARG_REGS        4

/* Define this if it is the responsibility of the caller to allocate
   the area reserved for arguments passed in registers.  */
#define REG_PARM_STACK_SPACE(FNDECL) (MT_NUM_ARG_REGS * UNITS_PER_WORD)

/* Define this macro if `REG_PARM_STACK_SPACE' is defined, but the stack
   parameters don't skip the area specified by it.  */
#define STACK_PARMS_IN_REG_PARM_AREA

/* A C expression that should indicate the number of bytes of its own
   arguments that a function pops on returning, or 0 if the function
   pops no arguments and the caller must therefore pop them all after
   the function returns.  */
#define RETURN_POPS_ARGS(FUNDECL, FUNTYPE, STACK_SIZE) 0

#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
   mt_function_arg (& (CUM), (MODE), (TYPE), (NAMED), FALSE)

#define CUMULATIVE_ARGS int

#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, FNDECL, N_NAMED_ARGS) \
    mt_init_cumulative_args (& (CUM), FNTYPE, LIBNAME, FNDECL, FALSE)

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)			\
    mt_function_arg_advance (&CUM, MODE, TYPE, NAMED)

#define FUNCTION_ARG_BOUNDARY(MODE, TYPE)				\
    mt_function_arg_boundary (MODE, TYPE)

#define FUNCTION_ARG_REGNO_P(REGNO)					\
  ((REGNO) >= FIRST_ARG_REGNUM && ((REGNO) <= LAST_ARG_REGNUM))

#define RETURN_VALUE_REGNUM	RETVAL_REGNUM
     
#define FUNCTION_VALUE(VALTYPE, FUNC) \
   mt_function_value (VALTYPE, TYPE_MODE(VALTYPE), FUNC)

#define LIBCALL_VALUE(MODE) \
   mt_function_value (NULL_TREE, MODE, NULL_TREE)

#define FUNCTION_VALUE_REGNO_P(REGNO) ((REGNO) == RETURN_VALUE_REGNUM)

/* A C expression which can inhibit the returning of certain function
   values in registers, based on the type of value.  */
#define RETURN_IN_MEMORY(TYPE) (int_size_in_bytes (TYPE) > UNITS_PER_WORD)

/* Define this macro to be 1 if all structure and union return values must be
   in memory.  */
#define DEFAULT_PCC_STRUCT_RETURN 0

/* Define this macro as a C expression that is nonzero if the return
   instruction or the function epilogue ignores the value of the stack
   pointer; in other words, if it is safe to delete an instruction to
   adjust the stack pointer before a return from the function.  */
#define EXIT_IGNORE_STACK 1

#define EPILOGUE_USES(REGNO) mt_epilogue_uses(REGNO)

/* Define this macro if the function epilogue contains delay slots to which
   instructions from the rest of the function can be "moved".  */
#define DELAY_SLOTS_FOR_EPILOGUE 1

/* A C expression that returns 1 if INSN can be placed in delay slot number N
   of the epilogue.  */
#define ELIGIBLE_FOR_EPILOGUE_DELAY(INSN, N) 0

#define FUNCTION_PROFILER(FILE, LABELNO) gcc_unreachable ()

/* Trampolines are not implemented.  */
#define TRAMPOLINE_SIZE 0

#define INITIALIZE_TRAMPOLINE(ADDR, FNADDR, STATIC_CHAIN)

/* ?? What is this -- aldyh ?? */
#define UCMPSI3_LIBCALL		"__ucmpsi3"

/* Addressing Modes.  */

/* A C expression that is 1 if the RTX X is a constant which is a valid
   address.  */
#define CONSTANT_ADDRESS_P(X) CONSTANT_P (X)

/* A number, the maximum number of registers that can appear in a valid memory
   address.  Note that it is up to you to specify a value equal to the maximum
   number that `GO_IF_LEGITIMATE_ADDRESS' would ever accept.  */
#define MAX_REGS_PER_ADDRESS 1

#ifdef REG_OK_STRICT
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)	\
{						\
  if (mt_legitimate_address_p (MODE, X, 1))	\
    goto ADDR;					\
}
#else
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)	\
{						\
  if (mt_legitimate_address_p (MODE, X, 0))	\
    goto ADDR;					\
}
#endif

#ifdef REG_OK_STRICT
#define REG_OK_FOR_BASE_P(X) mt_reg_ok_for_base_p (X, 1)
#else
#define REG_OK_FOR_BASE_P(X) mt_reg_ok_for_base_p (X, 0)
#endif

#define REG_OK_FOR_INDEX_P(X) REG_OK_FOR_BASE_P (X)

#define LEGITIMIZE_ADDRESS(X, OLDX, MODE, WIN) {}

#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR, LABEL)

#define LEGITIMATE_CONSTANT_P(X) 1

/* A C expression for the cost of moving data of mode M between a register and
   memory.  A value of 2 is the default; this cost is relative to those in
   `REGISTER_MOVE_COST'.

   If moving between registers and memory is more expensive than between two
   registers, you should define this macro to express the relative cost.  */
#define MEMORY_MOVE_COST(M,C,I) 10

/* Define this macro as a C expression which is nonzero if accessing less than
   a word of memory (i.e. a `char' or a `short') is no faster than accessing a
   word of memory.  */
#define SLOW_BYTE_ACCESS 1

#define SLOW_UNALIGNED_ACCESS(MODE, ALIGN) 1 

#define TEXT_SECTION_ASM_OP ".text"

#define DATA_SECTION_ASM_OP ".data"

#define BSS_SECTION_ASM_OP "\t.section\t.bss"

/* A C string constant for text to be output before each `asm' statement or
   group of consecutive ones.  Normally this is `"#APP"', which is a comment
   that has no effect on most assemblers but tells the GNU assembler that it
   must check the lines that follow for all valid assembler constructs.  */
#define ASM_APP_ON "#APP\n"

/* A C string constant for text to be output after each `asm' statement or
   group of consecutive ones.  Normally this is `"#NO_APP"', which tells the
   GNU assembler to resume making the time-saving assumptions that are valid
   for ordinary compiler output.  */
#define ASM_APP_OFF "#NO_APP\n"

/* This is how to output an assembler line defining a `char' constant.  */
#define ASM_OUTPUT_CHAR(FILE, VALUE)			\
  do							\
    {							\
      fprintf (FILE, "\t.byte\t");			\
      output_addr_const (FILE, (VALUE));		\
      fprintf (FILE, "\n");				\
    }							\
  while (0)

/* This is how to output an assembler line defining a `short' constant.  */
#define ASM_OUTPUT_SHORT(FILE, VALUE)			\
  do							\
    {							\
      fprintf (FILE, "\t.hword\t");			\
      output_addr_const (FILE, (VALUE));		\
      fprintf (FILE, "\n");				\
    }							\
  while (0)

/* This is how to output an assembler line defining an `int' constant.
   We also handle symbol output here.  */
#define ASM_OUTPUT_INT(FILE, VALUE)			\
  do							\
    {							\
      fprintf (FILE, "\t.word\t");			\
      output_addr_const (FILE, (VALUE));		\
      fprintf (FILE, "\n");				\
    }							\
  while (0)

/* A C statement to output to the stdio stream STREAM an assembler instruction
   to assemble a single byte containing the number VALUE.

   This declaration must be present.  */
#define ASM_OUTPUT_BYTE(STREAM, VALUE) \
  fprintf (STREAM, "\t%s\t0x%x\n", ASM_BYTE_OP, (VALUE))

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.globl "

#define REGISTER_NAMES							\
{ "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",			\
  "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",			\
  "LOOP1", "LOOP2", "LOOP3", "LOOP4", "ap" }

/* If defined, a C initializer for an array of structures containing a name and
   a register number.  This macro defines additional names for hard registers,
   thus allowing the `asm' option in declarations to refer to registers using
   alternate names.  */
#define ADDITIONAL_REGISTER_NAMES \
{ { "FP", 12}, {"SP", 13}, {"RA", 14}, {"IRA", 15} }

/* Define this macro if you are using an unusual assembler that requires
   different names for the machine instructions.

   The definition is a C statement or statements which output an assembler
   instruction opcode to the stdio stream STREAM.  The macro-operand PTR is a
   variable of type `char *' which points to the opcode name in its "internal"
   form--the form that is written in the machine description.  The definition
   should output the opcode name to STREAM, performing any translation you
   desire, and increment the variable PTR to point at the end of the opcode so
   that it will not be output twice.  */
#define ASM_OUTPUT_OPCODE(STREAM, PTR) \
   (PTR) = mt_asm_output_opcode (STREAM, PTR)

#define FINAL_PRESCAN_INSN(INSN, OPVEC, NOPERANDS) \
  mt_final_prescan_insn (INSN, OPVEC, NOPERANDS)

#define PRINT_OPERAND(STREAM, X, CODE) mt_print_operand (STREAM, X, CODE)

/* A C expression which evaluates to true if CODE is a valid punctuation
   character for use in the `PRINT_OPERAND' macro.  */
/* #:  Print nop for delay slot.  */
#define PRINT_OPERAND_PUNCT_VALID_P(CODE) ((CODE) == '#')

#define PRINT_OPERAND_ADDRESS(STREAM, X) mt_print_operand_address (STREAM, X)

/* If defined, C string expressions to be used for the `%R', `%L', `%U', and
   `%I' options of `asm_fprintf' (see `final.c').  These are useful when a
   single `md' file must support multiple assembler formats.  In that case, the
   various `tm.h' files can define these macros differently.

   USER_LABEL_PREFIX is defined in svr4.h.  */
#define REGISTER_PREFIX     "%"
#define LOCAL_LABEL_PREFIX  "."
#define USER_LABEL_PREFIX   ""
#define IMMEDIATE_PREFIX    ""

/* This macro should be provided on machines where the addresses in a dispatch
   table are relative to the table's own address.

   The definition should be a C statement to output to the stdio stream STREAM
   an assembler pseudo-instruction to generate a difference between two labels.
   VALUE and REL are the numbers of two internal labels.  The definitions of
   these labels are output using `targetm.asm_out.internal_label', and they
   must be printed in the same way here.  */
#define ASM_OUTPUT_ADDR_DIFF_ELT(STREAM, BODY, VALUE, REL) \
fprintf (STREAM, "\t.word .L%d-.L%d\n", VALUE, REL)

/* This macro should be provided on machines where the addresses in a dispatch
   table are absolute.

   The definition should be a C statement to output to the stdio stream STREAM
   an assembler pseudo-instruction to generate a reference to a label.  VALUE
   is the number of an internal label whose definition is output using
   `targetm.asm_out.internal_label'.  */
#define ASM_OUTPUT_ADDR_VEC_ELT(STREAM, VALUE) \
fprintf (STREAM, "\t.word .L%d\n", VALUE)

#define DWARF_FRAME_RETURN_COLUMN DWARF_FRAME_REGNUM (GPR_LINK)

#define EH_RETURN_DATA_REGNO(N) \
  ((N) == 0 ? GPR_R7 : (N) == 1 ? GPR_R8 : INVALID_REGNUM)

#define EH_RETURN_STACKADJ_REGNO	GPR_R11
#define EH_RETURN_STACKADJ_RTX		\
	gen_rtx_REG (SImode, EH_RETURN_STACKADJ_REGNO)
#define EH_RETURN_HANDLER_REGNO		GPR_R10
#define EH_RETURN_HANDLER_RTX		\
	gen_rtx_REG (SImode, EH_RETURN_HANDLER_REGNO)

#define ASM_OUTPUT_ALIGN(STREAM, POWER) \
  fprintf ((STREAM), "\t.p2align %d\n", (POWER))

#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

#ifndef DWARF2_DEBUGGING_INFO
#define DWARF2_DEBUGGING_INFO
#endif

/* Define this macro if GCC should produce dwarf version 2-style
   line numbers.  This usually requires extending the assembler to
   support them, and #defining DWARF2_LINE_MIN_INSN_LENGTH in the
   assembler configuration header files.  */
#define DWARF2_ASM_LINE_DEBUG_INFO 1

/* An alias for a machine mode name.  This is the machine mode that
   elements of a jump-table should have.  */
#define CASE_VECTOR_MODE SImode

/* Define this macro if operations between registers with integral
   mode smaller than a word are always performed on the entire
   register.  Most RISC machines have this property and most CISC
   machines do not.  */
#define WORD_REGISTER_OPERATIONS

/* The maximum number of bytes that a single instruction can move quickly from
   memory to memory.  */
#define MOVE_MAX 4

/* A C expression which is nonzero if on this machine it is safe to "convert"
   an integer of INPREC bits to one of OUTPREC bits (where OUTPREC is smaller
   than INPREC) by merely operating on it as if it had only OUTPREC bits.

   On many machines, this expression can be 1.

   When `TRULY_NOOP_TRUNCATION' returns 1 for a pair of sizes for modes for
   which `MODES_TIEABLE_P' is 0, suboptimal code can result.  If this is the
   case, making `TRULY_NOOP_TRUNCATION' return 0 in such cases may improve
   things.  */
#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

#define Pmode SImode

/* An alias for the machine mode used for memory references to functions being
   called, in `call' RTL expressions.  On most machines this should be
   `QImode'.  */
#define FUNCTION_MODE QImode

#define HANDLE_SYSV_PRAGMA 1

/* Indicate how many instructions can be issued at the same time.  */
#define ISSUE_RATE 1

/* Define the information needed to generate branch and scc insns.  This is
   stored from the compare operation.  Note that we can't use "rtx" here
   since it hasn't been defined!  */

extern struct rtx_def * mt_compare_op0;
extern struct rtx_def * mt_compare_op1;

