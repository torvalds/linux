/* Standard register usage.  */

/* Number of actual hardware registers.
   The hardware registers are assigned numbers for the compiler
   from 0 to just below FIRST_PSEUDO_REGISTER.
   All registers that the compiler knows about must be given numbers,
   even those that are not normally considered general registers.

   HP-PA 1.0 has 32 fullword registers and 16 floating point
   registers. The floating point registers hold either word or double
   word values.

   16 additional registers are reserved.

   HP-PA 1.1 has 32 fullword registers and 32 floating point
   registers. However, the floating point registers behave
   differently: the left and right halves of registers are addressable
   as 32 bit registers. So, we will set things up like the 68k which
   has different fp units: define separate register sets for the 1.0
   and 1.1 fp units.  */

#define FIRST_PSEUDO_REGISTER 89  /* 32 general regs + 56 fp regs +
				     + 1 shift reg */

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.

   On the HP-PA, these are:
   Reg 0	= 0 (hardware). However, 0 is used for condition code,
                  so is not fixed.
   Reg 1	= ADDIL target/Temporary (hardware).
   Reg 2	= Return Pointer
   Reg 3	= Frame Pointer
   Reg 4	= Frame Pointer (>8k varying frame with HP compilers only)
   Reg 4-18	= Preserved Registers
   Reg 19	= Linkage Table Register in HPUX 8.0 shared library scheme.
   Reg 20-22	= Temporary Registers
   Reg 23-26	= Temporary/Parameter Registers
   Reg 27	= Global Data Pointer (hp)
   Reg 28	= Temporary/Return Value register
   Reg 29	= Temporary/Static Chain/Return Value register #2
   Reg 30	= stack pointer
   Reg 31	= Temporary/Millicode Return Pointer (hp)

   Freg 0-3	= Status Registers	 -- Not known to the compiler.
   Freg 4-7	= Arguments/Return Value
   Freg 8-11	= Temporary Registers
   Freg 12-15	= Preserved Registers

   Freg 16-31	= Reserved

   On the Snake, fp regs are

   Freg 0-3	= Status Registers	-- Not known to the compiler.
   Freg 4L-7R	= Arguments/Return Value
   Freg 8L-11R	= Temporary Registers
   Freg 12L-21R	= Preserved Registers
   Freg 22L-31R = Temporary Registers

*/

#define FIXED_REGISTERS  \
 {0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 1, 0, 0, 1, 0, \
  /* fp registers */	  \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0}

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  */
#define CALL_USED_REGISTERS  \
 {1, 1, 1, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 1, 1, 1, 1, 1, \
  1, 1, 1, 1, 1, 1, 1, 1, \
  /* fp registers */	  \
  1, 1, 1, 1, 1, 1, 1, 1, \
  1, 1, 1, 1, 1, 1, 1, 1, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 0, 0, 0, 0, \
  0, 0, 0, 0, 1, 1, 1, 1, \
  1, 1, 1, 1, 1, 1, 1, 1, \
  1, 1, 1, 1, 1, 1, 1, 1, \
  1}

#define CONDITIONAL_REGISTER_USAGE \
{						\
  int i;					\
  if (!TARGET_PA_11)				\
    {						\
      for (i = 56; i < 88; i++) 		\
	fixed_regs[i] = call_used_regs[i] = 1; 	\
      for (i = 33; i < 88; i += 2) 		\
	fixed_regs[i] = call_used_regs[i] = 1; 	\
    }						\
  if (TARGET_DISABLE_FPREGS || TARGET_SOFT_FLOAT)\
    {						\
      for (i = 32; i < 88; i++) 		\
	fixed_regs[i] = call_used_regs[i] = 1; 	\
    }						\
  if (flag_pic)					\
    fixed_regs[PIC_OFFSET_TABLE_REGNUM] = 1;	\
}

/* Allocate the call used registers first.  This should minimize
   the number of registers that need to be saved (as call used
   registers will generally not be allocated across a call).

   Experimentation has shown slightly better results by allocating
   FP registers first.  We allocate the caller-saved registers more
   or less in reverse order to their allocation as arguments.

   FP registers are ordered so that all L registers are selected before
   R registers.  This works around a false dependency interlock on the
   PA8000 when accessing the high and low parts of an FP register
   independently.  */

#define REG_ALLOC_ORDER \
 {					\
  /* caller-saved fp regs.  */		\
  68, 70, 72, 74, 76, 78, 80, 82,	\
  84, 86, 40, 42, 44, 46, 38, 36,	\
  34, 32,				\
  69, 71, 73, 75, 77, 79, 81, 83,	\
  85, 87, 41, 43, 45, 47, 39, 37,	\
  35, 33,				\
  /* caller-saved general regs.  */	\
  28, 19, 20, 21, 22, 31, 27, 29,	\
  23, 24, 25, 26,  2,			\
  /* callee-saved fp regs.  */		\
  48, 50, 52, 54, 56, 58, 60, 62,	\
  64, 66,				\
  49, 51, 53, 55, 57, 59, 61, 63,	\
  65, 67,				\
  /* callee-saved general regs.  */	\
   3,  4,  5,  6,  7,  8,  9, 10, 	\
  11, 12, 13, 14, 15, 16, 17, 18,	\
  /* special registers.  */		\
   1, 30,  0, 88}


/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.
   This is ordinarily the length in words of a value of mode MODE
   but can be less for certain modes in special long registers.

   On the HP-PA, general registers are 32 bits wide.  The floating
   point registers are 64 bits wide.  Snake fp regs are treated as
   32 bits wide since the left and right parts are independently
   accessible.  */
#define HARD_REGNO_NREGS(REGNO, MODE)					\
  (FP_REGNO_P (REGNO)							\
   ? (!TARGET_PA_11							\
      ? COMPLEX_MODE_P (MODE) ? 2 : 1					\
      : (GET_MODE_SIZE (MODE) + 4 - 1) / 4) 	                        \
   : (GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* There are no instructions that use DImode in PA 1.0, so we only
   allow it in PA 1.1 and later.  */
#define VALID_FP_MODE_P(MODE)						\
  ((MODE) == SFmode || (MODE) == DFmode					\
   || (MODE) == SCmode || (MODE) == DCmode				\
   || (MODE) == QImode || (MODE) == HImode || (MODE) == SImode		\
   || (TARGET_PA_11 && (MODE) == DImode))

/* Value is 1 if hard register REGNO can hold a value of machine-mode MODE.

   On the HP-PA, the cpu registers can hold any mode that fits in 32 bits.
   For the 64-bit modes, we choose a set of non-overlapping general registers
   that includes the incoming arguments and the return value.  We specify a
   set with no overlaps so that we don't have to specify that the destination
   register is an early clobber in patterns using this mode.  Except for the
   return value, the starting registers are odd.  For 128 and 256 bit modes,
   we similarly specify non-overlapping sets of cpu registers.  However,
   there aren't any patterns defined for modes larger than 64 bits at the
   moment.

   We limit the modes allowed in the floating point registers to the
   set of modes used in the machine definition.  In addition, we allow
   the complex modes SCmode and DCmode.  The real and imaginary parts
   of complex modes are allocated to separate registers.  This might
   allow patterns to be defined in the future to operate on these values.

   The PA 2.0 architecture specifies that quad-precision floating-point
   values should start on an even floating point register.  Thus, we
   choose non-overlapping sets of registers starting on even register
   boundaries for large modes.  However, there is currently no support
   in the machine definition for modes larger than 64 bits.  TFmode is
   supported under HP-UX using libcalls.  Since TFmode values are passed
   by reference, they never need to be loaded into the floating-point
   registers.  */
#define HARD_REGNO_MODE_OK(REGNO, MODE) \
  ((REGNO) == 0 ? (MODE) == CCmode || (MODE) == CCFPmode		\
   : !TARGET_PA_11 && FP_REGNO_P (REGNO)				\
     ? (VALID_FP_MODE_P (MODE)						\
	&& (GET_MODE_SIZE (MODE) <= 8					\
	    || (GET_MODE_SIZE (MODE) == 16 && ((REGNO) & 3) == 0)))	\
   : FP_REGNO_P (REGNO)							\
     ? (VALID_FP_MODE_P (MODE)						\
	&& (GET_MODE_SIZE (MODE) <= 4					\
	    || (GET_MODE_SIZE (MODE) == 8 && ((REGNO) & 1) == 0)	\
	    || (GET_MODE_SIZE (MODE) == 16 && ((REGNO) & 3) == 0)	\
	    || (GET_MODE_SIZE (MODE) == 32 && ((REGNO) & 7) == 0)))	\
   : (GET_MODE_SIZE (MODE) <= UNITS_PER_WORD				\
      || (GET_MODE_SIZE (MODE) == 2 * UNITS_PER_WORD			\
	  && ((((REGNO) & 1) == 1 && (REGNO) <= 25) || (REGNO) == 28))	\
      || (GET_MODE_SIZE (MODE) == 4 * UNITS_PER_WORD			\
	  && ((REGNO) & 3) == 3 && (REGNO) <= 23)			\
      || (GET_MODE_SIZE (MODE) == 8 * UNITS_PER_WORD			\
	  && ((REGNO) & 7) == 3 && (REGNO) <= 19)))

/* How to renumber registers for dbx and gdb.

   Registers 0  - 31 remain unchanged.

   Registers 32 - 87 are mapped to 72 - 127

   Register 88 is mapped to 32.  */

#define DBX_REGISTER_NUMBER(REGNO) \
  ((REGNO) <= 31 ? (REGNO) :						\
   ((REGNO) <= 87 ? (REGNO) + 40 : 32))

/* We must not use the DBX register numbers for the DWARF 2 CFA column
   numbers because that maps to numbers beyond FIRST_PSEUDO_REGISTER.
   Instead use the identity mapping.  */
#define DWARF_FRAME_REGNUM(REG) REG

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

  /* The HP-PA has four kinds of registers: general regs, 1.0 fp regs,
     1.1 fp regs, and the high 1.1 fp regs, to which the operands of
     fmpyadd and fmpysub are restricted.  */

enum reg_class { NO_REGS, R1_REGS, GENERAL_REGS, FPUPPER_REGS, FP_REGS,
		 GENERAL_OR_FP_REGS, SHIFT_REGS, ALL_REGS, LIM_REG_CLASSES};

#define N_REG_CLASSES (int) LIM_REG_CLASSES

/* Give names of register classes as strings for dump file.  */

#define REG_CLASS_NAMES \
  {"NO_REGS", "R1_REGS", "GENERAL_REGS", "FPUPPER_REGS", "FP_REGS", \
   "GENERAL_OR_FP_REGS", "SHIFT_REGS", "ALL_REGS"}

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES. Register 0, the "condition code" register,
   is in no class.  */

#define REG_CLASS_CONTENTS	\
 {{0x00000000, 0x00000000, 0x00000000},	/* NO_REGS */			\
  {0x00000002, 0x00000000, 0x00000000},	/* R1_REGS */			\
  {0xfffffffe, 0x00000000, 0x00000000},	/* GENERAL_REGS */		\
  {0x00000000, 0xff000000, 0x00ffffff},	/* FPUPPER_REGS */		\
  {0x00000000, 0xffffffff, 0x00ffffff},	/* FP_REGS */			\
  {0xfffffffe, 0xffffffff, 0x00ffffff},	/* GENERAL_OR_FP_REGS */	\
  {0x00000000, 0x00000000, 0x01000000},	/* SHIFT_REGS */		\
  {0xfffffffe, 0xffffffff, 0x01ffffff}}	/* ALL_REGS */

/* Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */

#define REGNO_REG_CLASS(REGNO)						\
  ((REGNO) == 0 ? NO_REGS 						\
   : (REGNO) == 1 ? R1_REGS						\
   : (REGNO) < 32 ? GENERAL_REGS					\
   : (REGNO) < 56 ? FP_REGS						\
   : (REGNO) < 88 ? FPUPPER_REGS					\
   : SHIFT_REGS)

/* Get reg_class from a letter such as appears in the machine description.  */
/* Keep 'x' for backward compatibility with user asm.  */
#define REG_CLASS_FROM_LETTER(C) \
  ((C) == 'f' ? FP_REGS :					\
   (C) == 'y' ? FPUPPER_REGS :					\
   (C) == 'x' ? FP_REGS :					\
   (C) == 'q' ? SHIFT_REGS :					\
   (C) == 'a' ? R1_REGS :					\
   (C) == 'Z' ? ALL_REGS : NO_REGS)

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.  */
#define CLASS_MAX_NREGS(CLASS, MODE)					\
  ((CLASS) == FP_REGS || (CLASS) == FPUPPER_REGS			\
   ? (!TARGET_PA_11							\
      ? COMPLEX_MODE_P (MODE) ? 2 : 1					\
      : (GET_MODE_SIZE (MODE) + 4 - 1) / 4)				\
   : ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

/* 1 if N is a possible register number for function argument passing.  */

#define FUNCTION_ARG_REGNO_P(N) \
  (((N) >= 23 && (N) <= 26) || (! TARGET_SOFT_FLOAT && (N) >= 32 && (N) <= 39)) 

/* How to refer to registers in assembler output.
   This sequence is indexed by compiler's hard-register-number (see above).  */

#define REGISTER_NAMES \
{"%r0",   "%r1",    "%r2",   "%r3",    "%r4",   "%r5",    "%r6",   "%r7",    \
 "%r8",   "%r9",    "%r10",  "%r11",   "%r12",  "%r13",   "%r14",  "%r15",   \
 "%r16",  "%r17",   "%r18",  "%r19",   "%r20",  "%r21",   "%r22",  "%r23",   \
 "%r24",  "%r25",   "%r26",  "%r27",   "%r28",  "%r29",   "%r30",  "%r31",   \
 "%fr4",  "%fr4R",  "%fr5",  "%fr5R",  "%fr6",  "%fr6R",  "%fr7",  "%fr7R",  \
 "%fr8",  "%fr8R",  "%fr9",  "%fr9R",  "%fr10", "%fr10R", "%fr11", "%fr11R", \
 "%fr12", "%fr12R", "%fr13", "%fr13R", "%fr14", "%fr14R", "%fr15", "%fr15R", \
 "%fr16", "%fr16R", "%fr17", "%fr17R", "%fr18", "%fr18R", "%fr19", "%fr19R", \
 "%fr20", "%fr20R", "%fr21", "%fr21R", "%fr22", "%fr22R", "%fr23", "%fr23R", \
 "%fr24", "%fr24R", "%fr25", "%fr25R", "%fr26", "%fr26R", "%fr27", "%fr27R", \
 "%fr28", "%fr28R", "%fr29", "%fr29R", "%fr30", "%fr30R", "%fr31", "%fr31R", \
 "SAR"}

#define ADDITIONAL_REGISTER_NAMES \
{{"%fr4L",32}, {"%fr5L",34}, {"%fr6L",36}, {"%fr7L",38},		\
 {"%fr8L",40}, {"%fr9L",42}, {"%fr10L",44}, {"%fr11L",46},		\
 {"%fr12L",48}, {"%fr13L",50}, {"%fr14L",52}, {"%fr15L",54},		\
 {"%fr16L",56}, {"%fr17L",58}, {"%fr18L",60}, {"%fr19L",62},		\
 {"%fr20L",64}, {"%fr21L",66}, {"%fr22L",68}, {"%fr23L",70},		\
 {"%fr24L",72}, {"%fr25L",74}, {"%fr26L",76}, {"%fr27L",78},		\
 {"%fr28L",80}, {"%fr29L",82}, {"%fr30L",84}, {"%fr31R",86},		\
 {"%cr11",88}}

#define FP_SAVED_REG_LAST 66
#define FP_SAVED_REG_FIRST 48
#define FP_REG_STEP 2
#define FP_REG_FIRST 32
#define FP_REG_LAST 87
