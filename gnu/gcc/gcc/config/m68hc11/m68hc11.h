/* Definitions of target machine for GNU compiler.
   Motorola 68HC11 and 68HC12.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Stephane Carrez (stcarrez@nerim.fr)

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
Boston, MA 02110-1301, USA.

Note:
   A first 68HC11 port was made by Otto Lind (otto@coactive.com)
   on gcc 2.6.3.  I have used it as a starting point for this port.
   However, this new port is a complete re-write.  Its internal
   design is completely different.  The generated code is not
   compatible with the gcc 2.6.3 port.

   The gcc 2.6.3 port is available at:

   ftp.unina.it/pub/electronics/motorola/68hc11/gcc/gcc-6811-fsf.tar.gz

*/

/*****************************************************************************
**
** Controlling the Compilation Driver, `gcc'
**
*****************************************************************************/

#undef ENDFILE_SPEC

/* Compile and assemble for a 68hc11 unless there is a -m68hc12 option.  */
#ifndef ASM_SPEC
#define ASM_SPEC                                                \
"%{m68hc12:-m68hc12}"                                           \
"%{m68hcs12:-m68hcs12}"                                         \
"%{!m68hc12:%{!m68hcs12:-m68hc11}} "                            \
"%{mshort:-mshort}%{!mshort:-mlong} "                           \
"%{fshort-double:-mshort-double}%{!fshort-double:-mlong-double}"
#endif

/* We need to tell the linker the target elf format.  Just pass an
   emulation option.  This can be overridden by -Wl option of gcc.  */
#ifndef LINK_SPEC
#define LINK_SPEC                                               \
"%{m68hc12:-m m68hc12elf}"                                      \
"%{m68hcs12:-m m68hc12elf}"                                     \
"%{!m68hc12:%{!m68hcs12:-m m68hc11elf}} "                       \
"%{!mnorelax:%{!m68hc12:%{!m68hcs12:-relax}}}"
#endif

#ifndef LIB_SPEC
#define LIB_SPEC       ""
#endif

#ifndef CC1_SPEC
#define CC1_SPEC       ""
#endif

#ifndef CPP_SPEC
#define CPP_SPEC  \
"%{mshort:-D__HAVE_SHORT_INT__ -D__INT__=16}\
 %{!mshort:-D__INT__=32}\
 %{m68hc12:-Dmc6812 -DMC6812 -Dmc68hc12}\
 %{m68hcs12:-Dmc6812 -DMC6812 -Dmc68hcs12}\
 %{!m68hc12:%{!m68hcs12:-Dmc6811 -DMC6811 -Dmc68hc11}}\
 %{fshort-double:-D__HAVE_SHORT_DOUBLE__}\
 %{mlong-calls:-D__USE_RTC__}"
#endif

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "crt1%O%s"

/* Names to predefine in the preprocessor for this target machine.  */
#define TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define_std ("mc68hc1x");		\
    }						\
  while (0)

/* As an embedded target, we have no libc.  */
#ifndef inhibit_libc
#  define inhibit_libc
#endif

/* Forward type declaration for prototypes definitions.
   rtx_ptr is equivalent to rtx. Can't use the same name.  */
struct rtx_def;
typedef struct rtx_def *rtx_ptr;

union tree_node;
typedef union tree_node *tree_ptr;

/* We can't declare enum machine_mode forward nor include 'machmode.h' here.
   Prototypes defined here will use an int instead. It's better than no
   prototype at all.  */
typedef int enum_machine_mode;

/*****************************************************************************
**
** Run-time Target Specification
**
*****************************************************************************/

/* Run-time compilation parameters selecting different hardware subsets.  */

extern short *reg_renumber;	/* def in local_alloc.c */

#define TARGET_OP_TIME		(optimize && optimize_size == 0)
#define TARGET_RELAX            (TARGET_NO_DIRECT_MODE)

/* Default target_flags if no switches specified.  */
#ifndef TARGET_DEFAULT
# define TARGET_DEFAULT		0
#endif

/* Define this macro as a C expression for the initializer of an
   array of string to tell the driver program which options are
   defaults for this target and thus do not need to be handled
   specially when using `MULTILIB_OPTIONS'.  */
#ifndef MULTILIB_DEFAULTS
# if TARGET_DEFAULT & MASK_M6811
#  define MULTILIB_DEFAULTS { "m68hc11" }
# else
#  define MULTILIB_DEFAULTS { "m68hc12" }
# endif
#endif

/* Print subsidiary information on the compiler version in use.  */
#define TARGET_VERSION	fprintf (stderr, " (MC68HC11/MC68HC12/MC68HCS12)")

/* Sometimes certain combinations of command options do not make
   sense on a particular target machine.  You can define a macro
   `OVERRIDE_OPTIONS' to take account of this.  This macro, if
   defined, is executed once just after all the command options have
   been parsed.

   Don't use this macro to turn on various extra optimizations for
   `-O'.  That is what `OPTIMIZATION_OPTIONS' is for.  */

#define OVERRIDE_OPTIONS	m68hc11_override_options ();


/* Define cost parameters for a given processor variant.  */
struct processor_costs {
  const int add;		/* cost of an add instruction */
  const int logical;          /* cost of a logical instruction */
  const int shift_var;
  const int shiftQI_const[8];
  const int shiftHI_const[16];
  const int multQI;
  const int multHI;
  const int multSI;
  const int divQI;
  const int divHI;
  const int divSI;
};

/* Costs for the current processor.  */
extern const struct processor_costs *m68hc11_cost;


/* target machine storage layout */

/* Define this if most significant byte of a word is the lowest numbered.  */
#define BYTES_BIG_ENDIAN 	1

/* Define this if most significant bit is lowest numbered
   in instructions that operate on numbered bit-fields.  */
#define BITS_BIG_ENDIAN         0

/* Define this if most significant word of a multiword number is numbered.  */
#define WORDS_BIG_ENDIAN 	1

/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD		2

/* Definition of size_t.  This is really an unsigned short as the
   68hc11 only handles a 64K address space.  */
#define SIZE_TYPE               "short unsigned int"

/* A C expression for a string describing the name of the data type
   to use for the result of subtracting two pointers.  The typedef
   name `ptrdiff_t' is defined using the contents of the string.
   The 68hc11 only has a 64K address space.  */
#define PTRDIFF_TYPE            "short int"

/* Allocation boundary (bits) for storing pointers in memory.  */
#define POINTER_BOUNDARY	8

/* Normal alignment required for function parameters on the stack, in bits.
   This can't be less than BITS_PER_WORD */
#define PARM_BOUNDARY		(BITS_PER_WORD)

/* Boundary (bits) on which stack pointer should be aligned.  */
#define STACK_BOUNDARY		8

/* Allocation boundary (bits) for the code of a function.  */
#define FUNCTION_BOUNDARY	8

#define BIGGEST_ALIGNMENT	8

/* Alignment of field after `int : 0' in a structure.  */
#define EMPTY_FIELD_BOUNDARY	8

/* Every structure's size must be a multiple of this.  */
#define STRUCTURE_SIZE_BOUNDARY 8

/* Define this if instructions will fail to work if given data not
   on the nominal alignment.  If instructions will merely go slower
   in that case, do not define this macro.  */
#define STRICT_ALIGNMENT	0

/* An integer expression for the size in bits of the largest integer
   machine mode that should actually be used.  All integer machine modes of
   this size or smaller can be used for structures and unions with the
   appropriate sizes.  */
#define MAX_FIXED_MODE_SIZE	64

/* target machine storage layout */

/* Size (bits) of the type "int" on target machine
   (If undefined, default is BITS_PER_WORD).  */
#define INT_TYPE_SIZE           (TARGET_SHORT ? 16 : 32)

/* Size (bits) of the type "short" on target machine */
#define SHORT_TYPE_SIZE		16

/* Size (bits) of the type "long" on target machine */
#define LONG_TYPE_SIZE		32

/* Size (bits) of the type "long long" on target machine */
#define LONG_LONG_TYPE_SIZE     64

/* A C expression for the size in bits of the type `float' on the
   target machine. If you don't define this, the default is one word.
   Don't use default: a word is only 16.  */
#define FLOAT_TYPE_SIZE         32

/* A C expression for the size in bits of the type double on the target
   machine. If you don't define this, the default is two words.
   Be IEEE compliant.  */
#define DOUBLE_TYPE_SIZE        64

#define LONG_DOUBLE_TYPE_SIZE   64

/* Define this as 1 if `char' should by default be signed; else as 0.  */
#define DEFAULT_SIGNED_CHAR	0

/* Define these to avoid dependence on meaning of `int'.
   Note that WCHAR_TYPE_SIZE is used in cexp.y,
   where TARGET_SHORT is not available.  */
#define WCHAR_TYPE              "short int"
#define WCHAR_TYPE_SIZE         16


/* Standard register usage.  */

#define HARD_REG_SIZE           (UNITS_PER_WORD)

/* Assign names to real MC68HC11 registers.
   A and B registers are not really used (A+B = D)
   X register is first so that GCC allocates X+D for 32-bit integers and
   the lowpart of that integer will be D.  Having the lower part in D is
   better for 32<->16bit conversions and for many arithmetic operations.  */
#define HARD_X_REGNUM		0
#define HARD_D_REGNUM		1
#define HARD_Y_REGNUM		2
#define HARD_SP_REGNUM		3
#define HARD_PC_REGNUM		4
#define HARD_A_REGNUM		5
#define HARD_B_REGNUM		6
#define HARD_CCR_REGNUM		7

/* The Z register does not really exist in the 68HC11.  This a fake register
   for GCC.  It is treated exactly as an index register (X or Y).  It is only
   in the A_REGS class, which is the BASE_REG_CLASS for GCC.  Defining this
   register helps the reload pass of GCC.  Otherwise, the reload often dies
   with register spill failures.

   The Z register is replaced by either X or Y during the machine specific
   reorg (m68hc11_reorg).  It is saved in the SOFT_Z_REGNUM soft-register
   when this is necessary.

   It's possible to tell GCC not to use this register with -ffixed-z.  */
#define HARD_Z_REGNUM           8

/* The frame pointer is a soft-register.  It's treated as such by GCC:
   it is not and must not be part of the BASE_REG_CLASS.  */
#define DEFAULT_HARD_FP_REGNUM  (9)
#define HARD_FP_REGNUM		(9)
#define HARD_AP_REGNUM		(HARD_FP_REGNUM)

/* Temporary soft-register used in some cases when an operand came
   up into a bad register class (D, X, Y, SP) and gcc failed to
   recognize this. This register is never allocated by GCC.  */
#define SOFT_TMP_REGNUM          10

/* The soft-register which is used to save the Z register
   (see Z register replacement notes in m68hc11.c).  */
#define SOFT_Z_REGNUM            11

/* The soft-register which is used to save either X or Y.  */
#define SOFT_SAVED_XY_REGNUM     12

/* A fake clobber register for 68HC12 patterns.  */
#define FAKE_CLOBBER_REGNUM     (13)

/* Define 32 soft-registers of 16-bit each.  By default,
   only 12 of them are enabled and can be used by GCC.  The
   -msoft-reg-count=<n> option allows to control the number of valid
   soft-registers. GCC can put 32-bit values in them
   by allocating consecutive registers.  The first 3 soft-registers
   are never allocated by GCC.  They are used in case the insn template needs
   a temporary register, or for the Z register replacement.  */

#define MAX_SOFT_REG_COUNT      (32)
#define SOFT_REG_FIXED          0, 0, 0, 0, 0, 0, 0, 0, \
				0, 0, 0, 0, 1, 1, 1, 1, \
				1, 1, 1, 1, 1, 1, 1, 1, \
				1, 1, 1, 1, 1, 1, 1, 1
#define SOFT_REG_USED           0, 0, 0, 0, 0, 0, 0, 0, \
				0, 0, 0, 0, 1, 1, 1, 1, \
				1, 1, 1, 1, 1, 1, 1, 1, \
				1, 1, 1, 1, 1, 1, 1, 1
#define SOFT_REG_ORDER		\
SOFT_REG_FIRST, SOFT_REG_FIRST+1,SOFT_REG_FIRST+2,SOFT_REG_FIRST+3,\
SOFT_REG_FIRST+4, SOFT_REG_FIRST+5,SOFT_REG_FIRST+6,SOFT_REG_FIRST+7,\
SOFT_REG_FIRST+8, SOFT_REG_FIRST+9,SOFT_REG_FIRST+10,SOFT_REG_FIRST+11,\
SOFT_REG_FIRST+12, SOFT_REG_FIRST+13,SOFT_REG_FIRST+14,SOFT_REG_FIRST+15,\
SOFT_REG_FIRST+16, SOFT_REG_FIRST+17,SOFT_REG_FIRST+18,SOFT_REG_FIRST+19,\
SOFT_REG_FIRST+20, SOFT_REG_FIRST+21,SOFT_REG_FIRST+22,SOFT_REG_FIRST+23,\
SOFT_REG_FIRST+24, SOFT_REG_FIRST+25,SOFT_REG_FIRST+26,SOFT_REG_FIRST+27,\
SOFT_REG_FIRST+28, SOFT_REG_FIRST+29,SOFT_REG_FIRST+30,SOFT_REG_FIRST+31

#define SOFT_REG_NAMES							\
"*_.d1",  "*_.d2",  "*_.d3",  "*_.d4", \
"*_.d5",  "*_.d6",  "*_.d7",  "*_.d8",	\
"*_.d9",  "*_.d10", "*_.d11", "*_.d12", \
"*_.d13", "*_.d14", "*_.d15", "*_.d16",	\
"*_.d17", "*_.d18", "*_.d19", "*_.d20", \
"*_.d21", "*_.d22", "*_.d23", "*_.d24", \
"*_.d25", "*_.d26", "*_.d27", "*_.d28", \
"*_.d29", "*_.d30", "*_.d31", "*_.d32"

/* First available soft-register for GCC.  */
#define SOFT_REG_FIRST          (SOFT_SAVED_XY_REGNUM+2)

/* Last available soft-register for GCC.  */
#define SOFT_REG_LAST           (SOFT_REG_FIRST+MAX_SOFT_REG_COUNT)
#define SOFT_FP_REGNUM		(SOFT_REG_LAST)
#define SOFT_AP_REGNUM		(SOFT_FP_REGNUM+1)

/* Number of actual hardware registers. The hardware registers are assigned
   numbers for the compiler from 0 to just below FIRST_PSEUDO_REGISTER. 
   All registers that the compiler knows about must be given numbers, even
   those that are not normally considered general registers.  */
#define FIRST_PSEUDO_REGISTER	(SOFT_REG_LAST+2)

/* 1 for registers that have pervasive standard uses and are not available
   for the register allocator.  */
#define FIXED_REGISTERS \
  {0, 0, 0, 1, 1, 1, 1, 1,   0, 1,  1,   1,1, 1, SOFT_REG_FIXED, 1, 1}
/* X, D, Y, SP,PC,A, B, CCR, Z, FP,ZTMP,ZR,XYR, FK, D1 - D32, SOFT-FP, AP */

/* 1 for registers not available across function calls. For our pseudo
   registers, all are available.  */
#define CALL_USED_REGISTERS \
  {1, 1, 1, 1, 1, 1, 1, 1,   1, 1,  1,   1,1, 1, SOFT_REG_USED, 1, 1}
/* X, D, Y, SP,PC,A, B, CCR, Z, FP, ZTMP,ZR,XYR, D1 - 32,     SOFT-FP, AP */


/* Define this macro to change register usage conditional on target flags.

   The soft-registers are disabled or enabled according to the
  -msoft-reg-count=<n> option.  */


#define CONDITIONAL_REGISTER_USAGE (m68hc11_conditional_register_usage ())

/* List the order in which to allocate registers.  Each register must be
   listed once, even those in FIXED_REGISTERS.  */
#define REG_ALLOC_ORDER							\
{ HARD_D_REGNUM, HARD_X_REGNUM, HARD_Y_REGNUM,				\
  SOFT_REG_ORDER, HARD_Z_REGNUM, HARD_PC_REGNUM, HARD_A_REGNUM,		\
  HARD_B_REGNUM, HARD_CCR_REGNUM, HARD_FP_REGNUM, SOFT_FP_REGNUM,	\
  HARD_SP_REGNUM, SOFT_TMP_REGNUM, SOFT_Z_REGNUM, SOFT_SAVED_XY_REGNUM, \
  SOFT_AP_REGNUM, FAKE_CLOBBER_REGNUM  }

/* A C expression for the number of consecutive hard registers,
   starting at register number REGNO, required to hold a value of
   mode MODE.  */
#define HARD_REGNO_NREGS(REGNO, MODE) \
((Q_REGNO_P (REGNO)) ? (GET_MODE_SIZE (MODE)) : \
   ((GET_MODE_SIZE (MODE) + HARD_REG_SIZE - 1) / HARD_REG_SIZE))

/* Value is 1 if hard register REGNO can hold a value of machine-mode MODE.
    - 8 bit values are stored anywhere (except the SP register).
    - 16 bit values can be stored in any register whose mode is 16
    - 32 bit values can be stored in D, X registers or in a soft register
      (except the last one because we need 2 soft registers)
    - Values whose size is > 32 bit are not stored in real hard
      registers.  They may be stored in soft registers if there are
      enough of them.  */
#define HARD_REGNO_MODE_OK(REGNO, MODE) \
     hard_regno_mode_ok (REGNO,MODE)

/* Value is 1 if it is a good idea to tie two pseudo registers when one has
   mode MODE1 and one has mode MODE2.  If HARD_REGNO_MODE_OK could produce
   different values for MODE1 and MODE2, for any hard reg, then this must be
   0 for correct output.

   All modes are tieable except QImode.  */
#define MODES_TIEABLE_P(MODE1, MODE2)                   \
     (((MODE1) == (MODE2))                              \
      || ((MODE1) != QImode && (MODE2) != QImode))


/* Define the classes of registers for register constraints in the
   machine description.  Also define ranges of constants.

   One of the classes must always be named ALL_REGS and include all hard regs.
   If there is more than one class, another class must be named NO_REGS
   and contain no registers.

   The name GENERAL_REGS must be the name of a class (or an alias for
   another name such as ALL_REGS).  This is the class of registers
   that is allowed by "g" or "r" in a register constraint.
   Also, registers outside this class are allocated only when
   instructions express preferences for them.

   The classes must be numbered in nondecreasing order; that is,
   a larger-numbered class must never be contained completely
   in a smaller-numbered class.

   For any two classes, it is very desirable that there be another
   class that represents their union.  */

/* The M68hc11 has so few registers that it's not possible for GCC to
   do any register allocation without breaking. We extend the processor
   registers by having soft registers. These registers are treated as
   hard registers by GCC but they are located in memory and accessed by page0
   accesses (IND mode).  */
enum reg_class
{
  NO_REGS,
  D_REGS,			/* 16-bit data register */
  X_REGS,			/* 16-bit X register */
  Y_REGS,			/* 16-bit Y register */
  SP_REGS,			/* 16 bit stack pointer */
  DA_REGS,			/* 8-bit A reg.  */
  DB_REGS,			/* 8-bit B reg.  */
  Z_REGS,			/* 16-bit fake Z register */
  D8_REGS,			/* 8-bit A or B reg.  */
  Q_REGS,			/* 8-bit (byte (QI)) data (A, B or D) */
  D_OR_X_REGS,			/* D or X register */
  D_OR_Y_REGS,			/* D or Y register */
  D_OR_SP_REGS,			/* D or SP register */
  X_OR_Y_REGS,			/* IX or Y register */
  A_REGS,			/* 16-bit address register (X, Y, Z) */
  X_OR_SP_REGS,			/* X or SP register */
  Y_OR_SP_REGS,			/* Y or SP register */
  X_OR_Y_OR_D_REGS,		/* X, Y or D */
  A_OR_D_REGS,			/* X, Y, Z or D */
  A_OR_SP_REGS,			/* X, Y, Z or SP */
  H_REGS,			/* 16-bit hard register (D, X, Y, Z, SP) */
  S_REGS,			/* 16-bit soft register */
  D_OR_S_REGS,			/* 16-bit soft register or D register */
  X_OR_S_REGS,			/* 16-bit soft register or X register */
  Y_OR_S_REGS,			/* 16-bit soft register or Y register */
  Z_OR_S_REGS,			/* 16-bit soft register or Z register */
  SP_OR_S_REGS,			/* 16-bit soft register or SP register */
  D_OR_X_OR_S_REGS,		/* 16-bit soft register or D or X register */
  D_OR_Y_OR_S_REGS,		/* 16-bit soft register or D or Y register */
  D_OR_SP_OR_S_REGS,		/* 16-bit soft register or D or SP register */
  A_OR_S_REGS,			/* 16-bit soft register or X, Y registers */
  D_OR_A_OR_S_REGS,		/* 16-bit soft register or D, X, Y registers */
  TMP_REGS,			/* 16 bit fake scratch register */
  D_OR_A_OR_TMP_REGS,		/* General scratch register */
  G_REGS,			/* 16-bit general register
                                   (H_REGS + soft registers) */
  ALL_REGS,
  LIM_REG_CLASSES
};

/* alias GENERAL_REGS to G_REGS.  */
#define GENERAL_REGS	G_REGS

#define N_REG_CLASSES	(int) LIM_REG_CLASSES

/* Give names of register classes as strings for dump file.  */
#define REG_CLASS_NAMES \
{ "NO_REGS",                                    \
      "D_REGS",                                 \
      "X_REGS",                                 \
      "Y_REGS",                                 \
      "SP_REGS",                                \
      "DA_REGS",                                \
      "DB_REGS",                                \
      "D8_REGS",                                \
      "Z_REGS",                                 \
      "Q_REGS",                                 \
      "D_OR_X_REGS",                            \
      "D_OR_Y_REGS",                            \
      "D_OR_SP_REGS",                           \
      "X_OR_Y_REGS",                            \
      "A_REGS",                                 \
      "X_OR_SP_REGS",                           \
      "Y_OR_SP_REGS",                           \
      "X_OR_Y_OR_D_REGS",                       \
      "A_OR_D_REGS",                            \
      "A_OR_SP_REGS",                           \
      "H_REGS",                                 \
      "S_REGS",                                 \
      "D_OR_S_REGS",                            \
      "X_OR_S_REGS",                            \
      "Y_OR_S_REGS",                            \
      "Z_OR_S_REGS",                            \
      "SP_OR_S_REGS",                           \
      "D_OR_X_OR_S_REGS",                       \
      "D_OR_Y_OR_S_REGS",                       \
      "D_OR_SP_OR_S_REGS",                      \
      "A_OR_S_REGS",                            \
      "D_OR_A_OR_S_REGS",                       \
      "TMP_REGS",				\
      "D_OR_A_OR_TMP_REGS",			\
      "G_REGS",                                 \
      "ALL_REGS" }

/* An initializer containing the contents of the register classes,
   as integers which are bit masks.  The Nth integer specifies the
   contents of class N.  The way the integer MASK is interpreted is
   that register R is in the class if `MASK & (1 << R)' is 1.  */

/*--------------------------------------------------------------
   X		0x00000001
   D		0x00000002
   Y		0x00000004
   SP		0x00000008
   PC		0x00000010
   A		0x00000020
   B		0x00000040
   CCR		0x00000080
   Z		0x00000100
   FRAME        0x00000200
   ZTMP		0x00000400
   ZREG		0x00000800
   XYREG	0x00001000
   FAKE         0x00002000
   Di		0xFFFFc000, 0x03FFF
   SFRAME       0x00000000, 0x04000
   AP           0x00000000, 0x08000

   D_OR_X_REGS represents D+X. It is used for 32-bits numbers.
   A_REGS      represents a valid base register for indexing. It represents
	       X,Y and the Z register.
   S_REGS      represents the soft-registers. This includes the hard frame
	       and soft frame registers.
--------------------------------------------------------------*/

#define REG_CLASS_CONTENTS \
/* NO_REGS */		{{ 0x00000000, 0x00000000 },			\
/* D_REGS  */		 { 0x00000002, 0x00000000 }, /* D */            \
/* X_REGS  */		 { 0x00000001, 0x00000000 }, /* X */            \
/* Y_REGS  */		 { 0x00000004, 0x00000000 }, /* Y */            \
/* SP_REGS */		 { 0x00000008, 0x00000000 }, /* SP */           \
/* DA_REGS */		 { 0x00000020, 0x00000000 }, /* A */            \
/* DB_REGS */		 { 0x00000040, 0x00000000 }, /* B */            \
/* Z_REGS  */		 { 0x00000100, 0x00000000 }, /* Z */            \
/* D8_REGS */		 { 0x00000060, 0x00000000 }, /* A B */          \
/* Q_REGS  */		 { 0x00000062, 0x00000000 }, /* A B D */        \
/* D_OR_X_REGS */        { 0x00000003, 0x00000000 }, /* D X */          \
/* D_OR_Y_REGS */        { 0x00000006, 0x00000000 }, /* D Y */          \
/* D_OR_SP_REGS */       { 0x0000000A, 0x00000000 }, /* D SP */         \
/* X_OR_Y_REGS  */	 { 0x00000005, 0x00000000 }, /* X Y */          \
/* A_REGS  */		 { 0x00000105, 0x00000000 }, /* X Y Z */        \
/* X_OR_SP_REGS */       { 0x00000009, 0x00000000 }, /* X SP */         \
/* Y_OR_SP_REGS */       { 0x0000000C, 0x00000000 }, /* Y SP */         \
/* X_OR_Y_OR_D_REGS */   { 0x00000007, 0x00000000 }, /* D X Y */        \
/* A_OR_D_REGS  */       { 0x00000107, 0x00000000 }, /* D X Y Z */      \
/* A_OR_SP_REGS */       { 0x0000010D, 0x00000000 }, /* X Y SP */       \
/* H_REGS  */		 { 0x0000010F, 0x00000000 }, /* D X Y SP */     \
/* S_REGS  */		 { 0xFFFFDE00, 0x00007FFF }, /* _.D,..,FP,Z*  */  \
/* D_OR_S_REGS */	 { 0xFFFFDE02, 0x00007FFF }, /* D _.D */        \
/* X_OR_S_REGS */	 { 0xFFFFDE01, 0x00007FFF }, /* X _.D */        \
/* Y_OR_S_REGS */	 { 0xFFFFDE04, 0x00007FFF }, /* Y _.D */        \
/* Z_OR_S_REGS */	 { 0xFFFFDF00, 0x00007FFF }, /* Z _.D */        \
/* SP_OR_S_REGS */	 { 0xFFFFDE08, 0x00007FFF }, /* SP _.D */	\
/* D_OR_X_OR_S_REGS */	 { 0xFFFFDE03, 0x00007FFF }, /* D X _.D */      \
/* D_OR_Y_OR_S_REGS */	 { 0xFFFFDE06, 0x00007FFF }, /* D Y _.D */      \
/* D_OR_SP_OR_S_REGS */	 { 0xFFFFDE0A, 0x00007FFF }, /* D SP _.D */     \
/* A_OR_S_REGS */	 { 0xFFFFDF05, 0x00007FFF }, /* X Y _.D */      \
/* D_OR_A_OR_S_REGS */	 { 0xFFFFDF07, 0x00007FFF }, /* D X Y _.D */    \
/* TMP_REGS  */	         { 0x00002000, 0x00000000 }, /* FAKE */		\
/* D_OR_A_OR_TMP_REGS*/  { 0x00002107, 0x00000000 }, /* D X Y Z Fake */  \
/* G_REGS  */		 { 0xFFFFFF1F, 0x00007FFF }, /* ? _.D D X Y */   \
/* ALL_REGS*/		 { 0xFFFFFFFF, 0x00007FFF }}


/* set up a C expression whose value is a register class containing hard
   register REGNO */
#define Q_REGNO_P(REGNO)	((REGNO) == HARD_A_REGNUM \
				 || (REGNO) == HARD_B_REGNUM)
#define Q_REG_P(X)              (REG_P (X) && Q_REGNO_P (REGNO (X)))

#define D_REGNO_P(REGNO)        ((REGNO) == HARD_D_REGNUM)
#define D_REG_P(X)              (REG_P (X) && D_REGNO_P (REGNO (X)))

#define DB_REGNO_P(REGNO)       ((REGNO) == HARD_B_REGNUM)
#define DB_REG_P(X)             (REG_P (X) && DB_REGNO_P (REGNO (X)))
#define DA_REGNO_P(REGNO)       ((REGNO) == HARD_A_REGNUM)
#define DA_REG_P(X)             (REG_P (X) && DA_REGNO_P (REGNO (X)))

#define X_REGNO_P(REGNO)        ((REGNO) == HARD_X_REGNUM)
#define X_REG_P(X)              (REG_P (X) && X_REGNO_P (REGNO (X)))

#define Y_REGNO_P(REGNO)        ((REGNO) == HARD_Y_REGNUM)
#define Y_REG_P(X)              (REG_P (X) && Y_REGNO_P (REGNO (X)))

#define Z_REGNO_P(REGNO)        ((REGNO) == HARD_Z_REGNUM)
#define Z_REG_P(X)              (REG_P (X) && Z_REGNO_P (REGNO (X)))

#define SP_REGNO_P(REGNO)       ((REGNO) == HARD_SP_REGNUM)
#define SP_REG_P(X)             (REG_P (X) && SP_REGNO_P (REGNO (X)))

/* Address register.  */
#define A_REGNO_P(REGNO)        ((REGNO) == HARD_X_REGNUM \
                                 || (REGNO) == HARD_Y_REGNUM \
                                 || (REGNO) == HARD_Z_REGNUM)
#define A_REG_P(X)              (REG_P (X) && A_REGNO_P (REGNO (X)))

/* M68hc11 hard registers.  */
#define H_REGNO_P(REGNO)        (D_REGNO_P (REGNO) || A_REGNO_P (REGNO) \
				 || SP_REGNO_P (REGNO) || Q_REGNO_P (REGNO))
#define H_REG_P(X)              (REG_P (X) && H_REGNO_P (REGNO (X)))

#define FAKE_REGNO_P(REGNO)     ((REGNO) == FAKE_CLOBBER_REGNUM)
#define FAKE_REG_P(X)           (REG_P (X) && FAKE_REGNO_P (REGNO (X)))

/* Soft registers (or register emulation for gcc).  The temporary register
   used by insn template must be part of the S_REGS class so that it
   matches the 'u' constraint.  */
#define S_REGNO_P(REGNO)        ((REGNO) >= SOFT_TMP_REGNUM \
                                 && (REGNO) <= SOFT_REG_LAST \
                                 && (REGNO) != FAKE_CLOBBER_REGNUM)
#define S_REG_P(X)              (REG_P (X) && S_REGNO_P (REGNO (X)))

#define Z_REGNO_P(REGNO)        ((REGNO) == HARD_Z_REGNUM)
#define Z_REG_P(X)              (REG_P (X) && Z_REGNO_P (REGNO (X)))

/* General register.  */
#define G_REGNO_P(REGNO)        (H_REGNO_P (REGNO) || S_REGNO_P (REGNO) \
                                 || ((REGNO) == HARD_PC_REGNUM) \
				 || ((REGNO) == HARD_FP_REGNUM) \
				 || ((REGNO) == SOFT_FP_REGNUM) \
				 || ((REGNO) == FAKE_CLOBBER_REGNUM) \
				 || ((REGNO) == SOFT_AP_REGNUM))

#define G_REG_P(X)              (REG_P (X) && G_REGNO_P (REGNO (X)))

#define REGNO_REG_CLASS(REGNO) \
  (D_REGNO_P (REGNO) ? D_REGS : \
   (X_REGNO_P (REGNO) ? X_REGS : \
    (Y_REGNO_P (REGNO) ? Y_REGS : \
     (SP_REGNO_P (REGNO) ? SP_REGS : \
      (Z_REGNO_P (REGNO) ? Z_REGS : \
       (H_REGNO_P (REGNO) ? H_REGS : \
        (FAKE_REGNO_P (REGNO) ? TMP_REGS : \
	 (S_REGNO_P (REGNO) ? S_REGS : \
	  (DA_REGNO_P (REGNO) ? DA_REGS: \
	   (DB_REGNO_P (REGNO) ? DB_REGS: \
            (G_REGNO_P (REGNO) ? G_REGS : ALL_REGS)))))))))))


/* Get reg_class from a letter in the machine description.  */

extern enum reg_class m68hc11_tmp_regs_class;
#define REG_CLASS_FROM_LETTER(C) \
   ((C) == 'a' ? DA_REGS : \
    (C) == 'A' ? A_REGS : \
    (C) == 'b' ? DB_REGS : \
    (C) == 'B' ? X_OR_Y_REGS : \
    (C) == 'd' ? D_REGS : \
    (C) == 'D' ? D_OR_X_REGS : \
    (C) == 'q' ? Q_REGS : \
    (C) == 'h' ? H_REGS : \
    (C) == 't' ? TMP_REGS : \
    (C) == 'u' ? S_REGS : \
    (C) == 'v' ? m68hc11_tmp_regs_class : \
    (C) == 'w' ? SP_REGS : \
    (C) == 'x' ? X_REGS : \
    (C) == 'y' ? Y_REGS : \
    (C) == 'z' ? Z_REGS : NO_REGS)

#define PREFERRED_RELOAD_CLASS(X,CLASS)	preferred_reload_class(X,CLASS)

#define SMALL_REGISTER_CLASSES 1

/* A C expression that is nonzero if hard register number REGNO2 can be
   considered for use as a rename register for REGNO1 */

#define HARD_REGNO_RENAME_OK(REGNO1,REGNO2) \
  m68hc11_hard_regno_rename_ok ((REGNO1), (REGNO2))

/* A C expression whose value is nonzero if pseudos that have been
   assigned to registers of class CLASS would likely be spilled
   because registers of CLASS are needed for spill registers.

   The default value of this macro returns 1 if CLASS has exactly one
   register and zero otherwise.  On most machines, this default
   should be used.  Only define this macro to some other expression
   if pseudo allocated by `local-alloc.c' end up in memory because
   their hard registers were needed for spill registers.  If this
   macro returns nonzero for those classes, those pseudos will only
   be allocated by `global.c', which knows how to reallocate the
   pseudo to another register.  If there would not be another
   register available for reallocation, you should not change the
   definition of this macro since the only effect of such a
   definition would be to slow down register allocation.  */

#define CLASS_LIKELY_SPILLED_P(CLASS)					\
  (((CLASS) == D_REGS)							\
   || ((CLASS) == X_REGS)                                               \
   || ((CLASS) == Y_REGS)                                               \
   || ((CLASS) == A_REGS)                                               \
   || ((CLASS) == SP_REGS)                                              \
   || ((CLASS) == D_OR_X_REGS)                                          \
   || ((CLASS) == D_OR_Y_REGS)                                          \
   || ((CLASS) == X_OR_SP_REGS)                                         \
   || ((CLASS) == Y_OR_SP_REGS)                                         \
   || ((CLASS) == D_OR_SP_REGS))

/* Return the maximum number of consecutive registers needed to represent
   mode MODE in a register of class CLASS.  */
#define CLASS_MAX_NREGS(CLASS, MODE)		\
(((CLASS) == DA_REGS || (CLASS) == DB_REGS \
   || (CLASS) == D8_REGS || (CLASS) == Q_REGS) ? GET_MODE_SIZE (MODE) \
 : ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

/* The letters I, J, K, L and M in a register constraint string
   can be used to stand for particular ranges of immediate operands.
   This macro defines what the ranges are.
   C is the letter, and VALUE is a constant value.
   Return 1 if VALUE is in the range specified by C.

   `K' is for 0.
   `L' is for range -65536 to 65536
   `M' is for values whose 16-bit low part is 0
   'N' is for +1 or -1.
   'O' is for 16 (for rotate using swap).
   'P' is for range -8 to 2 (used by addhi_sp)

   'I', 'J' are not used.  */

#define CONST_OK_FOR_LETTER_P(VALUE, C) \
  ((C) == 'K' ? (VALUE) == 0 : \
   (C) == 'L' ? ((VALUE) >= -65536 && (VALUE) <= 65535) : \
   (C) == 'M' ? ((VALUE) & 0x0ffffL) == 0 : \
   (C) == 'N' ? ((VALUE) == 1 || (VALUE) == -1) : \
   (C) == 'I' ? ((VALUE) >= -2 && (VALUE) <= 2) : \
   (C) == 'O' ? (VALUE) == 16 : \
   (C) == 'P' ? ((VALUE) <= 2 && (VALUE) >= -8) : 0)

/* Similar, but for floating constants, and defining letters G and H.

   `G' is for 0.0.  */
#define CONST_DOUBLE_OK_FOR_LETTER_P(VALUE, C) \
  ((C) == 'G' ? (GET_MODE_CLASS (GET_MODE (VALUE)) == MODE_FLOAT \
		 && VALUE == CONST0_RTX (GET_MODE (VALUE))) : 0) 

/* 'U' represents certain kind of memory indexed operand for 68HC12.
   and any memory operand for 68HC11.
   'R' represents indexed addressing mode or access to page0 for 68HC11.
   For 68HC12, it represents any memory operand.  */
#define EXTRA_CONSTRAINT(OP, C)                         \
((C) == 'U' ? m68hc11_small_indexed_indirect_p (OP, GET_MODE (OP)) \
 : (C) == 'Q' ? m68hc11_symbolic_p (OP, GET_MODE (OP)) \
 : (C) == 'R' ? m68hc11_indirect_p (OP, GET_MODE (OP)) \
 : (C) == 'S' ? (memory_operand (OP, GET_MODE (OP)) \
		 && non_push_operand (OP, GET_MODE (OP))) : 0)


/* Stack layout; function entry, exit and calling.  */

/* Define this if pushing a word on the stack
   makes the stack pointer a smaller address.  */
#define STACK_GROWS_DOWNWARD

/* Define this to nonzero if the nominal address of the stack frame
   is at the high-address end of the local variables;
   that is, each additional local variable allocated
   goes at a more negative offset in the frame.

   Define to 0 for 68HC11, the frame pointer is the bottom
   of local variables.  */
#define FRAME_GROWS_DOWNWARD		0

/* Define this if successive arguments to a function occupy decreasing 
   addresses in the stack.  */
/* #define ARGS_GROW_DOWNWARD */

/* Offset within stack frame to start allocating local variables at.
   If FRAME_GROWS_DOWNWARD, this is the offset to the END of the
   first local allocated.  Otherwise, it is the offset to the BEGINNING
   of the first local allocated.  */
#define STARTING_FRAME_OFFSET		0

/* Offset of first parameter from the argument pointer register value.  */

#define FIRST_PARM_OFFSET(FNDECL)	2

/* After the prologue, RA is at 0(AP) in the current frame.  */
#define RETURN_ADDR_RTX(COUNT, FRAME)					\
  ((COUNT) == 0								\
   ? gen_rtx_MEM (Pmode, arg_pointer_rtx)                               \
   : 0)

/* Before the prologue, the top of the frame is at 2(sp).  */
#define INCOMING_FRAME_SP_OFFSET        2

/* Define this if functions should assume that stack space has been
   allocated for arguments even when their values are passed in
   registers.
  
   The value of this macro is the size, in bytes, of the area reserved for
   arguments passed in registers.
  
   This space can either be allocated by the caller or be a part of the
   machine-dependent stack frame: `OUTGOING_REG_PARM_STACK_SPACE'
   says which.  */
/* #define REG_PARM_STACK_SPACE(FNDECL)	2 */

/* Define this macro if REG_PARM_STACK_SPACE is defined but stack
   parameters don't skip the area specified by REG_PARM_STACK_SPACE.
   Normally, when a parameter is not passed in registers, it is placed on
   the stack beyond the REG_PARM_STACK_SPACE area.  Defining this macro  
   suppresses this behavior and causes the parameter to be passed on the
   stack in its natural location.  */
/* #define STACK_PARMS_IN_REG_PARM_AREA */

/* Register to use for pushing function arguments.  */
#define STACK_POINTER_REGNUM		HARD_SP_REGNUM

/* Base register for access to local variables of the function.  */
#define FRAME_POINTER_REGNUM		SOFT_FP_REGNUM

#define HARD_FRAME_POINTER_REGNUM	HARD_FP_REGNUM

/* Base register for access to arguments of the function.  */
#define ARG_POINTER_REGNUM		SOFT_AP_REGNUM

/* Register in which static-chain is passed to a function.  */
#define STATIC_CHAIN_REGNUM	        SOFT_Z_REGNUM


/* Definitions for register eliminations.

   This is an array of structures.  Each structure initializes one pair
   of eliminable registers.  The "from" register number is given first,
   followed by "to".  Eliminations of the same "from" register are listed
   in order of preference.

   We have two registers that are eliminated on the 6811. The pseudo arg
   pointer and pseudo frame pointer registers can always be eliminated;
   they are replaced with either the stack or the real frame pointer.  */

#define ELIMINABLE_REGS					\
{{ARG_POINTER_REGNUM,   STACK_POINTER_REGNUM},		\
 {ARG_POINTER_REGNUM,   HARD_FRAME_POINTER_REGNUM},	\
 {FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM},		\
 {FRAME_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM}}

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms may be
   accessed via the stack pointer) in functions that seem suitable.
   This is computed in `reload', in reload1.c.  */
#define FRAME_POINTER_REQUIRED	0

/* Given FROM and TO register numbers, say whether this elimination is allowed.
   Frame pointer elimination is automatically handled.

   All other eliminations are valid.  */

#define CAN_ELIMINATE(FROM, TO)					\
 ((FROM) == ARG_POINTER_REGNUM && (TO) == STACK_POINTER_REGNUM	\
  ? ! frame_pointer_needed					\
  : 1)


/* Define the offset between two registers, one to be eliminated, and the other
   its replacement, at the start of a routine.  */

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET)			\
    { OFFSET = m68hc11_initial_elimination_offset (FROM, TO); }


/* Passing Function Arguments on the Stack.  */

/* If we generate an insn to push BYTES bytes, this says how many the
   stack pointer really advances by. No rounding or alignment needed
   for MC6811.  */
#define PUSH_ROUNDING(BYTES)	(BYTES)

/* Value is 1 if returning from a function call automatically pops the
   arguments described by the number-of-args field in the call. FUNTYPE is
   the data type of the function (as a tree), or for a library call it is
   an identifier node for the subroutine name.
  
   The standard MC6811 call, with arg count word, includes popping the
   args as part of the call template.  */
#define RETURN_POPS_ARGS(FUNDECL,FUNTYPE,SIZE)	0

/* Passing Arguments in Registers.  */

/* Define a data type for recording info about an argument list
   during the scan of that argument list.  This data type should
   hold all necessary information about the function itself
   and about the args processed so far, enough to enable macros
   such as FUNCTION_ARG to determine where the next arg should go.  */

typedef struct m68hc11_args
{
  int words;
  int nregs;
} CUMULATIVE_ARGS;

/* If defined, a C expression which determines whether, and in which direction,
   to pad out an argument with extra space.  The value should be of type
   `enum direction': either `upward' to pad above the argument,
   `downward' to pad below, or `none' to inhibit padding.

   Structures are stored left shifted in their argument slot.  */
#define FUNCTION_ARG_PADDING(MODE, TYPE) \
  m68hc11_function_arg_padding ((MODE), (TYPE))

#undef PAD_VARARGS_DOWN
#define PAD_VARARGS_DOWN \
  (m68hc11_function_arg_padding (TYPE_MODE (type), type) == downward)

/* Initialize a variable CUM of type CUMULATIVE_ARGS for a call to a
   function whose data type is FNTYPE. For a library call, FNTYPE is 0.  */
#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
    (m68hc11_init_cumulative_args (&CUM, FNTYPE, LIBNAME))

/* Update the data in CUM to advance over an argument of mode MODE and data
   type TYPE. (TYPE is null for libcalls where that information may not be
   available.) */
#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED) \
    (m68hc11_function_arg_advance (&CUM, MODE, TYPE, NAMED))

/* Define where to put the arguments to a function.
   Value is zero to push the argument on the stack,
   or a hard register in which to store the argument.

   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).  */
#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
  (m68hc11_function_arg (&CUM, MODE, TYPE, NAMED))

/* Define the profitability of saving registers around calls.

   Disable this because the saving instructions generated by
   caller-save need a reload and the way it is implemented,
   it forbids all spill registers at that point.  Enabling
   caller saving results in spill failure.  */
#define CALLER_SAVE_PROFITABLE(REFS,CALLS) 0

/* 1 if N is a possible register number for function argument passing.
   D is for 16-bit values, X is for 32-bit (X+D).  */
#define FUNCTION_ARG_REGNO_P(N)	\
     (((N) == HARD_D_REGNUM) || ((N) == HARD_X_REGNUM))

/* All return values are in the D or X+D registers:
    - 8 and 16-bit values are returned in D.
      BLKmode are passed in D as pointer.
    - 32-bit values are returned in X + D.
      The high part is passed in X and the low part in D.
      For GCC, the register number must be HARD_X_REGNUM.  */
#define FUNCTION_VALUE(VALTYPE, FUNC)					\
     gen_rtx_REG (TYPE_MODE (VALTYPE),					\
              ((TYPE_MODE (VALTYPE) == BLKmode				\
	        || GET_MODE_SIZE (TYPE_MODE (VALTYPE)) <= 2)		\
		   ? HARD_D_REGNUM : HARD_X_REGNUM))

#define LIBCALL_VALUE(MODE)						\
     gen_rtx_REG (MODE,						\
              (((MODE) == BLKmode || GET_MODE_SIZE (MODE) <= 2)		\
                   ? HARD_D_REGNUM : HARD_X_REGNUM))

/* 1 if N is a possible register number for a function value.  */
#define FUNCTION_VALUE_REGNO_P(N) \
     ((N) == HARD_D_REGNUM || (N) == HARD_X_REGNUM)

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in functions
   that have frame pointers. No definition is equivalent to always zero.  */
#define EXIT_IGNORE_STACK	0


/* Generating Code for Profiling.  */

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */
#define FUNCTION_PROFILER(FILE, LABELNO)		\
    fprintf (FILE, "\tldy\t.LP%d\n\tjsr mcount\n", (LABELNO))
/* Length in units of the trampoline for entering a nested function.  */
#define TRAMPOLINE_SIZE		(TARGET_M6811 ? 11 : 9)

/* A C statement to initialize the variable parts of a trampoline.
   ADDR is an RTX for the address of the trampoline; FNADDR is an
   RTX for the address of the nested function; STATIC_CHAIN is an
   RTX for the static chain value that should be passed to the
   function when it is called.  */
#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT) \
  m68hc11_initialize_trampoline ((TRAMP), (FNADDR), (CXT))


/* Addressing modes, and classification of registers for them.  */

#define ADDR_STRICT       0x01  /* Accept only registers in class A_REGS  */
#define ADDR_INCDEC       0x02  /* Post/Pre inc/dec */
#define ADDR_INDEXED      0x04  /* D-reg index */
#define ADDR_OFFSET       0x08
#define ADDR_INDIRECT     0x10  /* Accept (mem (mem ...)) for [n,X] */
#define ADDR_CONST        0x20  /* Accept const and symbol_ref  */

/* The 68HC12 has all the post/pre increment/decrement modes.  */
#define HAVE_POST_INCREMENT (TARGET_M6812 && TARGET_AUTO_INC_DEC)
#define HAVE_PRE_INCREMENT  (TARGET_M6812 && TARGET_AUTO_INC_DEC)
#define HAVE_POST_DECREMENT (TARGET_M6812 && TARGET_AUTO_INC_DEC)
#define HAVE_PRE_DECREMENT  (TARGET_M6812 && TARGET_AUTO_INC_DEC)

/* The class value for base registers.  This depends on the target:
   A_REGS for 68HC11 and A_OR_SP_REGS for 68HC12.  The class value
   is stored at init time.  */
extern enum reg_class m68hc11_base_reg_class;
#define BASE_REG_CLASS		m68hc11_base_reg_class

/* The class value for index registers.  This is NO_REGS for 68HC11.  */

extern enum reg_class m68hc11_index_reg_class;
#define INDEX_REG_CLASS	        m68hc11_index_reg_class

/* These assume that REGNO is a hard or pseudo reg number. They give nonzero
   only if REGNO is a hard reg of the suitable class or a pseudo reg currently
   allocated to a suitable hard reg.  Since they use reg_renumber, they are
   safe only once reg_renumber has been allocated, which happens in
   local-alloc.c.  */


/* Internal macro, return 1 if REGNO is a valid base register.  */
#define REG_VALID_P(REGNO) ((REGNO) >= 0)

extern unsigned char m68hc11_reg_valid_for_base[FIRST_PSEUDO_REGISTER];
#define REG_VALID_FOR_BASE_P(REGNO) \
    (REG_VALID_P (REGNO) && (REGNO) < FIRST_PSEUDO_REGISTER \
     && m68hc11_reg_valid_for_base[REGNO])

/* Internal macro, return 1 if REGNO is a valid index register.  */
extern unsigned char m68hc11_reg_valid_for_index[FIRST_PSEUDO_REGISTER];
#define REG_VALID_FOR_INDEX_P(REGNO) \
    (REG_VALID_P (REGNO) >= 0 && (REGNO) < FIRST_PSEUDO_REGISTER \
     && m68hc11_reg_valid_for_index[REGNO])

/* Internal macro, the nonstrict definition for REGNO_OK_FOR_BASE_P.  */
#define REGNO_OK_FOR_BASE_NONSTRICT_P(REGNO) \
    ((REGNO) >= FIRST_PSEUDO_REGISTER \
     || REG_VALID_FOR_BASE_P (REGNO) \
     || (REGNO) == FRAME_POINTER_REGNUM \
     || (REGNO) == HARD_FRAME_POINTER_REGNUM \
     || (REGNO) == ARG_POINTER_REGNUM \
     || (reg_renumber && REG_VALID_FOR_BASE_P (reg_renumber[REGNO])))

/* Internal macro, the nonstrict definition for REGNO_OK_FOR_INDEX_P.  */
#define REGNO_OK_FOR_INDEX_NONSTRICT_P(REGNO) \
    (TARGET_M6812 \
     && ((REGNO) >= FIRST_PSEUDO_REGISTER \
         || REG_VALID_FOR_INDEX_P (REGNO) \
         || (reg_renumber && REG_VALID_FOR_INDEX_P (reg_renumber[REGNO]))))

/* Internal macro, the strict definition for REGNO_OK_FOR_BASE_P.  */
#define REGNO_OK_FOR_BASE_STRICT_P(REGNO) \
    ((REGNO) < FIRST_PSEUDO_REGISTER ? REG_VALID_FOR_BASE_P (REGNO) \
     : (reg_renumber && REG_VALID_FOR_BASE_P (reg_renumber[REGNO])))

/* Internal macro, the strict definition for REGNO_OK_FOR_INDEX_P.  */
#define REGNO_OK_FOR_INDEX_STRICT_P(REGNO) \
    (TARGET_M6812 \
     && ((REGNO) < FIRST_PSEUDO_REGISTER ? REG_VALID_FOR_INDEX_P (REGNO) \
         : (reg_renumber && REG_VALID_FOR_INDEX_P (reg_renumber[REGNO]))))

#define REGNO_OK_FOR_BASE_P2(REGNO,STRICT) \
    ((STRICT) ? (REGNO_OK_FOR_BASE_STRICT_P (REGNO)) \
              : (REGNO_OK_FOR_BASE_NONSTRICT_P (REGNO)))

#define REGNO_OK_FOR_INDEX_P2(REGNO,STRICT) \
    ((STRICT) ? (REGNO_OK_FOR_INDEX_STRICT_P (REGNO)) \
              : (REGNO_OK_FOR_INDEX_NONSTRICT_P (REGNO)))

#define REGNO_OK_FOR_BASE_P(REGNO) REGNO_OK_FOR_BASE_STRICT_P (REGNO)
#define REGNO_OK_FOR_INDEX_P(REGNO) REGNO_OK_FOR_INDEX_STRICT_P (REGNO)

#define REG_OK_FOR_BASE_STRICT_P(X)     REGNO_OK_FOR_BASE_STRICT_P (REGNO (X))
#define REG_OK_FOR_BASE_NONSTRICT_P(X)  REGNO_OK_FOR_BASE_NONSTRICT_P (REGNO (X))
#define REG_OK_FOR_INDEX_STRICT_P(X)    REGNO_OK_FOR_INDEX_STRICT_P (REGNO (X))
#define REG_OK_FOR_INDEX_NONSTRICT_P(X) REGNO_OK_FOR_INDEX_NONSTRICT_P (REGNO (X))

/* see PUSH_POP_ADDRESS_P() below for an explanation of this.  */
#define IS_STACK_PUSH(operand) \
    ((GET_CODE (operand) == MEM) \
     && (GET_CODE (XEXP (operand, 0)) == PRE_DEC) \
     && (SP_REG_P (XEXP (XEXP (operand, 0), 0))))

#define IS_STACK_POP(operand) \
    ((GET_CODE (operand) == MEM) \
     && (GET_CODE (XEXP (operand, 0)) == POST_INC) \
     && (SP_REG_P (XEXP (XEXP (operand, 0), 0))))

/* 1 if X is an rtx for a constant that is a valid address.  */
#define CONSTANT_ADDRESS_P(X)	(CONSTANT_P (X))

/* Maximum number of registers that can appear in a valid memory address */
#define MAX_REGS_PER_ADDRESS	2

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression that is a
   valid memory address for an instruction. The MODE argument is the
   machine mode for the MEM expression that wants to use this address.  */

/*--------------------------------------------------------------
   Valid addresses are either direct or indirect (MEM) versions
   of the following forms:
	constant		N
	register		,X
	indexed			N,X
--------------------------------------------------------------*/

/* The range of index that is allowed by indirect addressing.  */

#define VALID_MIN_OFFSET m68hc11_min_offset
#define VALID_MAX_OFFSET m68hc11_max_offset

/* The offset values which are allowed by the n,x and n,y addressing modes.
   Take into account the size of the mode because we may have to add
   a mode offset to access the lowest part of the data.
   (For example, for an SImode, the last valid offset is 252.) */
#define VALID_CONSTANT_OFFSET_P(X,MODE)		\
(((GET_CODE (X) == CONST_INT) &&			\
  ((INTVAL (X) >= VALID_MIN_OFFSET)		\
     && ((INTVAL (X) <= VALID_MAX_OFFSET		\
		- (HOST_WIDE_INT) (GET_MODE_SIZE (MODE) + 1))))) \
|| (TARGET_M6812 \
    && ((GET_CODE (X) == SYMBOL_REF) \
        || GET_CODE (X) == LABEL_REF \
        || GET_CODE (X) == CONST)))

/* This is included to allow stack push/pop operations. Special hacks in the
   md and m6811.c files exist to support this.  */
#define PUSH_POP_ADDRESS_P(X) \
  (((GET_CODE (X) == PRE_DEC) || (GET_CODE (X) == POST_INC)) \
	&& SP_REG_P (XEXP (X, 0)))

/* Go to ADDR if X is a valid address.  */
#ifndef REG_OK_STRICT
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR) \
{ \
  if (m68hc11_go_if_legitimate_address ((X), (MODE), 0)) goto ADDR; \
}
#else
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)		 \
{							 \
  if (m68hc11_go_if_legitimate_address ((X), (MODE), 1)) goto ADDR; \
}
#endif

/* The macros REG_OK_FOR..._P assume that the arg is a REG rtx and check its
   validity for a certain class.  We have two alternate definitions for each
   of them.  The usual definition accepts all pseudo regs; the other rejects
   them unless they have been allocated suitable hard regs.  The symbol
   REG_OK_STRICT causes the latter definition to be used.
  
   Most source files want to accept pseudo regs in the hope that they will
   get allocated to the class that the insn wants them to be in. Source files
   for reload pass need to be strict. After reload, it makes no difference,
   since pseudo regs have been eliminated by then.  */

#ifndef REG_OK_STRICT
/* Nonzero if X is a hard reg that can be used as a base reg.  */
#define REG_OK_FOR_BASE_P(X)   REG_OK_FOR_BASE_NONSTRICT_P(X)

/* Nonzero if X is a hard reg that can be used as an index.  */
#define REG_OK_FOR_INDEX_P(X)  REG_OK_FOR_INDEX_NONSTRICT_P(X)
#else
#define REG_OK_FOR_BASE_P(X)   REG_OK_FOR_BASE_STRICT_P(X)
#define REG_OK_FOR_INDEX_P(X)  REG_OK_FOR_INDEX_STRICT_P(X)
#endif


/* Try machine-dependent ways of modifying an illegitimate address
   to be legitimate.  If we find one, return the new, valid address.
   This macro is used in only one place: `memory_address' in explow.c.
  
   OLDX is the address as it was before break_out_memory_refs was called.
   In some cases it is useful to look at this to decide what needs to be done.
  
   MODE and WIN are passed so that this macro can use
   GO_IF_LEGITIMATE_ADDRESS.
  
   It is always safe for this macro to do nothing.
   It exists to recognize opportunities to optimize the output.  */

#define LEGITIMIZE_ADDRESS(X,OLDX,MODE,WIN)                     \
{ rtx operand = (X);                                            \
  if (m68hc11_legitimize_address (&operand, (OLDX), (MODE)))	\
    {                                                           \
      (X) = operand;                                            \
      GO_IF_LEGITIMATE_ADDRESS (MODE,X,WIN);                    \
    }                                                           \
}

/* Go to LABEL if ADDR (a legitimate address expression)
   has an effect that depends on the machine mode it is used for.  */
#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR,LABEL)  \
{									\
  if (GET_CODE (ADDR) == PRE_DEC || GET_CODE (ADDR) == POST_DEC		\
      || GET_CODE (ADDR) == PRE_INC || GET_CODE (ADDR) == POST_INC)	\
    goto LABEL;								\
}

/* Nonzero if the constant value X is a legitimate general operand.
   It is given that X satisfies CONSTANT_P or is a CONST_DOUBLE.  */

#define LEGITIMATE_CONSTANT_P(X)	1


/* Tell final.c how to eliminate redundant test instructions.  */

#define NOTICE_UPDATE_CC(EXP, INSN) \
	m68hc11_notice_update_cc ((EXP), (INSN))

/* Move costs between classes of registers */
#define REGISTER_MOVE_COST(MODE, CLASS1, CLASS2)	\
    (m68hc11_register_move_cost (MODE, CLASS1, CLASS2))

/* Move cost between register and memory.
    - Move to a 16-bit register is reasonable,
    - Move to a soft register can be expensive.  */
#define MEMORY_MOVE_COST(MODE,CLASS,IN)		\
    m68hc11_memory_move_cost ((MODE),(CLASS),(IN))

/* A C expression for the cost of a branch instruction.  A value of 1
   is the default; other values are interpreted relative to that.

   Pretend branches are cheap because GCC generates sub-optimal code
   for the default value.  */
#define BRANCH_COST 0

/* Nonzero if access to memory by bytes is slow and undesirable.  */
#define SLOW_BYTE_ACCESS	0

/* It is as good to call a constant function address as to call an address
   kept in a register.  */
#define NO_FUNCTION_CSE

/* Try a machine-dependent way of reloading an illegitimate address
   operand.  If we find one, push the reload and jump to WIN.  This
   macro is used in only one place: `find_reloads_address' in reload.c.

   For M68HC11, we handle large displacements of a base register
   by splitting the addend across an addhi3 insn.

   For M68HC12, the 64K offset range is available.
   */

#define LEGITIMIZE_RELOAD_ADDRESS(X,MODE,OPNUM,TYPE,IND_LEVELS,WIN)     \
do {                                                                    \
  /* We must recognize output that we have already generated ourselves.  */ \
  if (GET_CODE (X) == PLUS						\
      && GET_CODE (XEXP (X, 0)) == PLUS					\
      && GET_CODE (XEXP (XEXP (X, 0), 0)) == REG			\
      && GET_CODE (XEXP (XEXP (X, 0), 1)) == CONST_INT			\
      && GET_CODE (XEXP (X, 1)) == CONST_INT)				\
    {									\
      push_reload (XEXP (X, 0), NULL_RTX, &XEXP (X, 0), NULL,           \
                   BASE_REG_CLASS, GET_MODE (X), VOIDmode, 0, 0,        \
                   OPNUM, TYPE);                                        \
      goto WIN;                                                         \
    }									\
  if (GET_CODE (X) == PLUS                                              \
      && GET_CODE (XEXP (X, 0)) == REG                                  \
      && GET_CODE (XEXP (X, 1)) == CONST_INT				\
      && !VALID_CONSTANT_OFFSET_P (XEXP (X, 1), MODE))                  \
    {                                                                   \
      HOST_WIDE_INT val = INTVAL (XEXP (X, 1));                         \
      HOST_WIDE_INT low, high;                                          \
      high = val & (~0x0FF);                                            \
      low  = val & 0x00FF;                                              \
      if (low >= 256-15) { high += 16; low -= 16; }                     \
      /* Reload the high part into a base reg; leave the low part       \
         in the mem directly.  */                                       \
                                                                        \
      X = gen_rtx_PLUS (Pmode,						\
                        gen_rtx_PLUS (Pmode, XEXP (X, 0),		\
                                      GEN_INT (high)),                  \
                        GEN_INT (low));                                 \
                                                                        \
      push_reload (XEXP (X, 0), NULL_RTX, &XEXP (X, 0), NULL,           \
                   BASE_REG_CLASS, GET_MODE (X), VOIDmode, 0, 0,        \
                   OPNUM, TYPE);                                        \
      goto WIN;                                                         \
    }                                                                   \
} while (0)


/* Defining the Output Assembler Language.  */

/* A default list of other sections which we might be "in" at any given
   time.  For targets that use additional sections (e.g. .tdesc) you
   should override this definition in the target-specific file which
   includes this file.  */

/* Output before read-only data.  */
#define TEXT_SECTION_ASM_OP	("\t.sect\t.text")

/* Output before writable data.  */
#define DATA_SECTION_ASM_OP	("\t.sect\t.data")

/* Output before uninitialized data.  */
#define BSS_SECTION_ASM_OP 	("\t.sect\t.bss")

/* Define the pseudo-ops used to switch to the .ctors and .dtors sections.

   Same as config/elfos.h but don't mark these section SHF_WRITE since
   there is no shared library problem.  */
#undef CTORS_SECTION_ASM_OP
#define CTORS_SECTION_ASM_OP	"\t.section\t.ctors,\"a\""

#undef DTORS_SECTION_ASM_OP
#define DTORS_SECTION_ASM_OP	"\t.section\t.dtors,\"a\""

#define TARGET_ASM_CONSTRUCTOR  m68hc11_asm_out_constructor
#define TARGET_ASM_DESTRUCTOR   m68hc11_asm_out_destructor

/* Comment character */
#define ASM_COMMENT_START	";"

/* Output to assembler file text saying following lines
   may contain character constants, extra white space, comments, etc.  */
#define ASM_APP_ON 		"; Begin inline assembler code\n#APP\n"

/* Output to assembler file text saying following lines
   no longer contain unusual constructs.  */
#define ASM_APP_OFF 		"; End of inline assembler code\n#NO_APP\n"

/* Write the extra assembler code needed to declare a function properly.
   Some svr4 assemblers need to also have something extra said about the
   function's return value.  We allow for that here.

   For 68HC12 we mark functions that return with 'rtc'.  The linker
   will ensure that a 'call' is really made (instead of 'jsr').
   The debugger needs this information to correctly compute the stack frame.

   For 68HC11/68HC12 we also mark interrupt handlers for gdb to
   compute the correct stack frame.  */

#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(FILE, NAME, DECL)	\
  do							\
    {							\
      fprintf (FILE, "%s", TYPE_ASM_OP);		\
      assemble_name (FILE, NAME);			\
      putc (',', FILE);					\
      fprintf (FILE, TYPE_OPERAND_FMT, "function");	\
      putc ('\n', FILE);				\
      							\
      if (current_function_far)                         \
        {						\
          fprintf (FILE, "\t.far\t");			\
	  assemble_name (FILE, NAME);			\
	  putc ('\n', FILE);				\
	}						\
      else if (current_function_interrupt		\
	       || current_function_trap)		\
        {						\
	  fprintf (FILE, "\t.interrupt\t");		\
	  assemble_name (FILE, NAME);			\
	  putc ('\n', FILE);				\
	}						\
      ASM_DECLARE_RESULT (FILE, DECL_RESULT (DECL));	\
      ASM_OUTPUT_LABEL(FILE, NAME);			\
    }							\
  while (0)

/* Output #ident as a .ident.  */

/* output external reference */
#define ASM_OUTPUT_EXTERNAL(FILE,DECL,NAME) \
  {fputs ("\t; extern\t", FILE); \
  assemble_name (FILE, NAME); \
  fputs ("\n", FILE);}

/* How to refer to registers in assembler output.  This sequence is indexed
   by compiler's hard-register-number (see above).  */
#define REGISTER_NAMES						\
{ "x", "d", "y", "sp", "pc", "a", "b", "ccr", "z",		\
  "*_.frame", "*_.tmp", "*_.z", "*_.xy", "*fake clobber",	\
  SOFT_REG_NAMES, "*sframe", "*ap"}

/* Print an instruction operand X on file FILE. CODE is the code from the
   %-spec for printing this operand. If `%z3' was used to print operand
   3, then CODE is 'z'.  */

#define PRINT_OPERAND(FILE, X, CODE) \
  print_operand (FILE, X, CODE)

/* Print a memory operand whose address is X, on file FILE.  */
#define PRINT_OPERAND_ADDRESS(FILE, ADDR) \
  print_operand_address (FILE, ADDR)

/* This is how to output an insn to push/pop a register on the stack.
   It need not be very fast code.  

   Don't define because we don't know how to handle that with
   the STATIC_CHAIN_REGNUM (soft register).  Saving the static
   chain must be made inside FUNCTION_PROFILER.  */

#undef ASM_OUTPUT_REG_PUSH
#undef ASM_OUTPUT_REG_POP

/* This is how to output an element of a case-vector that is relative.  */

#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL) \
  fprintf (FILE, "\t%s\tL%d-L%d\n", integer_asm_op (2, TRUE), VALUE, REL)

/* This is how to output an element of a case-vector that is absolute.  */
#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE) \
  fprintf (FILE, "\t%s\t.L%d\n", integer_asm_op (2, TRUE), VALUE)

/* This is how to output an assembler line that says to advance the
   location counter to a multiple of 2**LOG bytes.  */
#define ASM_OUTPUT_ALIGN(FILE,LOG)			\
  do {                                                  \
      if ((LOG) > 1)                                    \
          fprintf ((FILE), "%s\n", ALIGN_ASM_OP); \
  } while (0)


/* Assembler Commands for Exception Regions.  */

/* Default values provided by GCC should be ok. Assuming that DWARF-2
   frame unwind info is ok for this platform.  */

#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

/* For the support of memory banks we need addresses that indicate
   the page number.  */
#define DWARF2_ADDR_SIZE 4

/* SCz 2003-07-08: Don't use as dwarf2 .file/.loc directives because
   the linker is doing relaxation and it does not adjust the debug_line
   sections when it shrinks the code.  This results in invalid addresses
   when debugging.  This does not bless too much the HC11/HC12 as most
   applications are embedded and small, hence a reasonable debug info.
   This problem is known for binutils 2.13, 2.14 and mainline.  */
#undef HAVE_AS_DWARF2_DEBUG_LINE

/* The prefix for local labels.  You should be able to define this as
   an empty string, or any arbitrary string (such as ".", ".L%", etc)
   without having to make any other changes to account for the specific
   definition.  Note it is a string literal, not interpreted by printf
   and friends.  */
#define LOCAL_LABEL_PREFIX "."

/* The prefix for immediate operands.  */
#define IMMEDIATE_PREFIX "#"
#define GLOBAL_ASM_OP   "\t.globl\t"


/* Miscellaneous Parameters.  */

/* Specify the machine mode that this machine uses
   for the index in the tablejump instruction.  */
#define CASE_VECTOR_MODE	Pmode

/* This flag, if defined, says the same insns that convert to a signed fixnum
   also convert validly to an unsigned one.  */
#define FIXUNS_TRUNC_LIKE_FIX_TRUNC

/* Max number of bytes we can move from memory to memory in one
   reasonably fast instruction.  */
#define MOVE_MAX 		2

/* MOVE_RATIO is the number of move instructions that is better than a
   block move.  Make this small on 6811, since the code size grows very
   large with each move.  */
#define MOVE_RATIO		3

/* Define if shifts truncate the shift count which implies one can omit
   a sign-extension or zero-extension of a shift count.  */
#define SHIFT_COUNT_TRUNCATED	1

/* Value is 1 if truncating an integer of INPREC bits to OUTPREC bits
   is done just by pretending it is already truncated.  */
#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC)	1

/* Specify the machine mode that pointers have. After generation of rtl, the
   compiler makes no further distinction between pointers and any other
   objects of this machine mode.  */
#define Pmode			HImode

/* A function address in a call instruction is a byte address (for indexing
   purposes) so give the MEM rtx a byte's mode.  */
#define FUNCTION_MODE		QImode

extern int debug_m6811;
extern int z_replacement_completed;
extern int current_function_interrupt;
extern int current_function_trap;
extern int current_function_far;

extern GTY(()) rtx m68hc11_compare_op0;
extern GTY(()) rtx m68hc11_compare_op1;
extern GTY(()) rtx m68hc11_soft_tmp_reg;
extern GTY(()) rtx ix_reg;
extern GTY(()) rtx iy_reg;
extern GTY(()) rtx d_reg;
