/* Subroutines for insn-output.c for ATMEL AVR micro controllers
   Copyright (C) 1998, 1999, 2000, 2001, 2002, 2004, 2005, 2006, 2007
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "insn-attr.h"
#include "flags.h"
#include "reload.h"
#include "tree.h"
#include "output.h"
#include "expr.h"
#include "toplev.h"
#include "obstack.h"
#include "function.h"
#include "recog.h"
#include "ggc.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"

/* Maximal allowed offset for an address in the LD command */
#define MAX_LD_OFFSET(MODE) (64 - (signed)GET_MODE_SIZE (MODE))

static int avr_naked_function_p (tree);
static int interrupt_function_p (tree);
static int signal_function_p (tree);
static int avr_regs_to_save (HARD_REG_SET *);
static int sequent_regs_live (void);
static const char *ptrreg_to_str (int);
static const char *cond_string (enum rtx_code);
static int avr_num_arg_regs (enum machine_mode, tree);
static int out_adj_frame_ptr (FILE *, int);
static int out_set_stack_ptr (FILE *, int, int);
static RTX_CODE compare_condition (rtx insn);
static int compare_sign_p (rtx insn);
static tree avr_handle_progmem_attribute (tree *, tree, tree, int, bool *);
static tree avr_handle_fndecl_attribute (tree *, tree, tree, int, bool *);
static tree avr_handle_fntype_attribute (tree *, tree, tree, int, bool *);
const struct attribute_spec avr_attribute_table[];
static bool avr_assemble_integer (rtx, unsigned int, int);
static void avr_file_start (void);
static void avr_file_end (void);
static void avr_output_function_prologue (FILE *, HOST_WIDE_INT);
static void avr_output_function_epilogue (FILE *, HOST_WIDE_INT);
static void avr_insert_attributes (tree, tree *);
static void avr_asm_init_sections (void);
static unsigned int avr_section_type_flags (tree, const char *, int);

static void avr_reorg (void);
static void avr_asm_out_ctor (rtx, int);
static void avr_asm_out_dtor (rtx, int);
static int avr_operand_rtx_cost (rtx, enum machine_mode, enum rtx_code);
static bool avr_rtx_costs (rtx, int, int, int *);
static int avr_address_cost (rtx);
static bool avr_return_in_memory (tree, tree);

/* Allocate registers from r25 to r8 for parameters for function calls.  */
#define FIRST_CUM_REG 26

/* Temporary register RTX (gen_rtx_REG (QImode, TMP_REGNO)) */
static GTY(()) rtx tmp_reg_rtx;

/* Zeroed register RTX (gen_rtx_REG (QImode, ZERO_REGNO)) */
static GTY(()) rtx zero_reg_rtx;

/* AVR register names {"r0", "r1", ..., "r31"} */
static const char *const avr_regnames[] = REGISTER_NAMES;

/* This holds the last insn address.  */
static int last_insn_address = 0;

/* Commands count in the compiled file */
static int commands_in_file;

/* Commands in the functions prologues in the compiled file */
static int commands_in_prologues;

/* Commands in the functions epilogues in the compiled file */
static int commands_in_epilogues;

/* Prologue/Epilogue size in words */
static int prologue_size;
static int epilogue_size;

/* Size of all jump tables in the current function, in words.  */
static int jump_tables_size;

/* Preprocessor macros to define depending on MCU type.  */
const char *avr_base_arch_macro;
const char *avr_extra_arch_macro;

section *progmem_section;

/* More than 8K of program memory: use "call" and "jmp".  */
int avr_mega_p = 0;

/* Enhanced core: use "movw", "mul", ...  */
int avr_enhanced_p = 0;

/* Assembler only.  */
int avr_asm_only_p = 0;

/* Core have 'MOVW' and 'LPM Rx,Z' instructions.  */
int avr_have_movw_lpmx_p = 0;

struct base_arch_s {
  int asm_only;
  int enhanced;
  int mega;
  int have_movw_lpmx;
  const char *const macro;
};

static const struct base_arch_s avr_arch_types[] = {
  { 1, 0, 0, 0,  NULL },  /* unknown device specified */
  { 1, 0, 0, 0, "__AVR_ARCH__=1" },
  { 0, 0, 0, 0, "__AVR_ARCH__=2" },
  { 0, 0, 0, 1, "__AVR_ARCH__=25"},
  { 0, 0, 1, 0, "__AVR_ARCH__=3" },
  { 0, 1, 0, 1, "__AVR_ARCH__=4" },
  { 0, 1, 1, 1, "__AVR_ARCH__=5" }
};

/* These names are used as the index into the avr_arch_types[] table 
   above.  */

enum avr_arch
{
  ARCH_UNKNOWN,
  ARCH_AVR1,
  ARCH_AVR2,
  ARCH_AVR25,
  ARCH_AVR3,
  ARCH_AVR4,
  ARCH_AVR5
};

struct mcu_type_s {
  const char *const name;
  int arch;  /* index in avr_arch_types[] */
  /* Must lie outside user's namespace.  NULL == no macro.  */
  const char *const macro;
};

/* List of all known AVR MCU types - if updated, it has to be kept
   in sync in several places (FIXME: is there a better way?):
    - here
    - avr.h (CPP_SPEC, LINK_SPEC, CRT_BINUTILS_SPECS)
    - t-avr (MULTILIB_MATCHES)
    - gas/config/tc-avr.c
    - avr-libc  */

static const struct mcu_type_s avr_mcu_types[] = {
    /* Classic, <= 8K.  */
  { "avr2",         ARCH_AVR2, NULL },
  { "at90s2313",    ARCH_AVR2, "__AVR_AT90S2313__" },
  { "at90s2323",    ARCH_AVR2, "__AVR_AT90S2323__" },
  { "at90s2333",    ARCH_AVR2, "__AVR_AT90S2333__" },
  { "at90s2343",    ARCH_AVR2, "__AVR_AT90S2343__" },
  { "attiny22",     ARCH_AVR2, "__AVR_ATtiny22__" },
  { "attiny26",     ARCH_AVR2, "__AVR_ATtiny26__" },
  { "at90s4414",    ARCH_AVR2, "__AVR_AT90S4414__" },
  { "at90s4433",    ARCH_AVR2, "__AVR_AT90S4433__" },
  { "at90s4434",    ARCH_AVR2, "__AVR_AT90S4434__" },
  { "at90s8515",    ARCH_AVR2, "__AVR_AT90S8515__" },
  { "at90c8534",    ARCH_AVR2, "__AVR_AT90C8534__" },
  { "at90s8535",    ARCH_AVR2, "__AVR_AT90S8535__" },
    /* Classic + MOVW, <= 8K.  */
  { "avr25",        ARCH_AVR25, NULL },
  { "attiny13",     ARCH_AVR25, "__AVR_ATtiny13__" },
  { "attiny2313",   ARCH_AVR25, "__AVR_ATtiny2313__" },
  { "attiny24",     ARCH_AVR25, "__AVR_ATtiny24__" },
  { "attiny44",     ARCH_AVR25, "__AVR_ATtiny44__" },
  { "attiny84",     ARCH_AVR25, "__AVR_ATtiny84__" },
  { "attiny25",     ARCH_AVR25, "__AVR_ATtiny25__" },
  { "attiny45",     ARCH_AVR25, "__AVR_ATtiny45__" },
  { "attiny85",     ARCH_AVR25, "__AVR_ATtiny85__" },
  { "attiny261",    ARCH_AVR25, "__AVR_ATtiny261__" },
  { "attiny461",    ARCH_AVR25, "__AVR_ATtiny461__" },
  { "attiny861",    ARCH_AVR25, "__AVR_ATtiny861__" },
  { "at86rf401",    ARCH_AVR25, "__AVR_AT86RF401__" },
    /* Classic, > 8K.  */
  { "avr3",         ARCH_AVR3, NULL },
  { "atmega103",    ARCH_AVR3, "__AVR_ATmega103__" },
  { "atmega603",    ARCH_AVR3, "__AVR_ATmega603__" },
  { "at43usb320",   ARCH_AVR3, "__AVR_AT43USB320__" },
  { "at43usb355",   ARCH_AVR3, "__AVR_AT43USB355__" },
  { "at76c711",     ARCH_AVR3, "__AVR_AT76C711__" },
    /* Enhanced, <= 8K.  */
  { "avr4",         ARCH_AVR4, NULL },
  { "atmega8",      ARCH_AVR4, "__AVR_ATmega8__" },
  { "atmega48",     ARCH_AVR4, "__AVR_ATmega48__" },
  { "atmega88",     ARCH_AVR4, "__AVR_ATmega88__" },
  { "atmega8515",   ARCH_AVR4, "__AVR_ATmega8515__" },
  { "atmega8535",   ARCH_AVR4, "__AVR_ATmega8535__" },
  { "atmega8hva",   ARCH_AVR4, "__AVR_ATmega8HVA__" },
  { "at90pwm1",     ARCH_AVR4, "__AVR_AT90PWM1__" },
  { "at90pwm2",     ARCH_AVR4, "__AVR_AT90PWM2__" },
  { "at90pwm3",     ARCH_AVR4, "__AVR_AT90PWM3__" },
    /* Enhanced, > 8K.  */
  { "avr5",         ARCH_AVR5, NULL },
  { "atmega16",     ARCH_AVR5, "__AVR_ATmega16__" },
  { "atmega161",    ARCH_AVR5, "__AVR_ATmega161__" },
  { "atmega162",    ARCH_AVR5, "__AVR_ATmega162__" },
  { "atmega163",    ARCH_AVR5, "__AVR_ATmega163__" },
  { "atmega164p",   ARCH_AVR5, "__AVR_ATmega164P__" },
  { "atmega165",    ARCH_AVR5, "__AVR_ATmega165__" },
  { "atmega165p",   ARCH_AVR5, "__AVR_ATmega165P__" },
  { "atmega168",    ARCH_AVR5, "__AVR_ATmega168__" },
  { "atmega169",    ARCH_AVR5, "__AVR_ATmega169__" },
  { "atmega169p",   ARCH_AVR5, "__AVR_ATmega169P__" },
  { "atmega32",     ARCH_AVR5, "__AVR_ATmega32__" },
  { "atmega323",    ARCH_AVR5, "__AVR_ATmega323__" },
  { "atmega324p",   ARCH_AVR5, "__AVR_ATmega324P__" },
  { "atmega325",    ARCH_AVR5, "__AVR_ATmega325__" },
  { "atmega325p",   ARCH_AVR5, "__AVR_ATmega325P__" },
  { "atmega3250",   ARCH_AVR5, "__AVR_ATmega3250__" },
  { "atmega3250p",  ARCH_AVR5, "__AVR_ATmega3250P__" },
  { "atmega329",    ARCH_AVR5, "__AVR_ATmega329__" },
  { "atmega329p",   ARCH_AVR5, "__AVR_ATmega329P__" },
  { "atmega3290",   ARCH_AVR5, "__AVR_ATmega3290__" },
  { "atmega3290p",  ARCH_AVR5, "__AVR_ATmega3290P__" },
  { "atmega406",    ARCH_AVR5, "__AVR_ATmega406__" },
  { "atmega64",     ARCH_AVR5, "__AVR_ATmega64__" },
  { "atmega640",    ARCH_AVR5, "__AVR_ATmega640__" },
  { "atmega644",    ARCH_AVR5, "__AVR_ATmega644__" },
  { "atmega644p",   ARCH_AVR5, "__AVR_ATmega644P__" },
  { "atmega645",    ARCH_AVR5, "__AVR_ATmega645__" },
  { "atmega6450",   ARCH_AVR5, "__AVR_ATmega6450__" },
  { "atmega649",    ARCH_AVR5, "__AVR_ATmega649__" },
  { "atmega6490",   ARCH_AVR5, "__AVR_ATmega6490__" },
  { "atmega128",    ARCH_AVR5, "__AVR_ATmega128__" },
  { "atmega1280",   ARCH_AVR5, "__AVR_ATmega1280__" },
  { "atmega1281",   ARCH_AVR5, "__AVR_ATmega1281__" },
  { "atmega16hva",  ARCH_AVR5, "__AVR_ATmega16HVA__" },
  { "at90can32",    ARCH_AVR5, "__AVR_AT90CAN32__" },
  { "at90can64",    ARCH_AVR5, "__AVR_AT90CAN64__" },
  { "at90can128",   ARCH_AVR5, "__AVR_AT90CAN128__" },
  { "at90usb82",    ARCH_AVR5, "__AVR_AT90USB82__" },
  { "at90usb162",   ARCH_AVR5, "__AVR_AT90USB162__" },
  { "at90usb646",   ARCH_AVR5, "__AVR_AT90USB646__" },
  { "at90usb647",   ARCH_AVR5, "__AVR_AT90USB647__" },
  { "at90usb1286",  ARCH_AVR5, "__AVR_AT90USB1286__" },
  { "at90usb1287",  ARCH_AVR5, "__AVR_AT90USB1287__" },
  { "at94k",        ARCH_AVR5, "__AVR_AT94K__" },
    /* Assembler only.  */
  { "avr1",         ARCH_AVR1, NULL },
  { "at90s1200",    ARCH_AVR1, "__AVR_AT90S1200__" },
  { "attiny11",     ARCH_AVR1, "__AVR_ATtiny11__" },
  { "attiny12",     ARCH_AVR1, "__AVR_ATtiny12__" },
  { "attiny15",     ARCH_AVR1, "__AVR_ATtiny15__" },
  { "attiny28",     ARCH_AVR1, "__AVR_ATtiny28__" },
  { NULL,           ARCH_UNKNOWN, NULL }
};

int avr_case_values_threshold = 30000;

/* Initialize the GCC target structure.  */
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.word\t"
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\t.long\t"
#undef TARGET_ASM_UNALIGNED_HI_OP
#define TARGET_ASM_UNALIGNED_HI_OP "\t.word\t"
#undef TARGET_ASM_UNALIGNED_SI_OP
#define TARGET_ASM_UNALIGNED_SI_OP "\t.long\t"
#undef TARGET_ASM_INTEGER
#define TARGET_ASM_INTEGER avr_assemble_integer
#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START avr_file_start
#undef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true
#undef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END avr_file_end

#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE avr_output_function_prologue
#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE avr_output_function_epilogue
#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE avr_attribute_table
#undef TARGET_ASM_FUNCTION_RODATA_SECTION
#define TARGET_ASM_FUNCTION_RODATA_SECTION default_no_function_rodata_section
#undef TARGET_INSERT_ATTRIBUTES
#define TARGET_INSERT_ATTRIBUTES avr_insert_attributes
#undef TARGET_SECTION_TYPE_FLAGS
#define TARGET_SECTION_TYPE_FLAGS avr_section_type_flags
#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS avr_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST avr_address_cost
#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG avr_reorg

#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY avr_return_in_memory

#undef TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING hook_bool_CUMULATIVE_ARGS_true

struct gcc_target targetm = TARGET_INITIALIZER;

void
avr_override_options (void)
{
  const struct mcu_type_s *t;
  const struct base_arch_s *base;

  flag_delete_null_pointer_checks = 0;

  for (t = avr_mcu_types; t->name; t++)
    if (strcmp (t->name, avr_mcu_name) == 0)
      break;

  if (!t->name)
    {
      fprintf (stderr, "unknown MCU '%s' specified\nKnown MCU names:\n",
	       avr_mcu_name);
      for (t = avr_mcu_types; t->name; t++)
	fprintf (stderr,"   %s\n", t->name);
    }

  base = &avr_arch_types[t->arch];
  avr_asm_only_p = base->asm_only;
  avr_enhanced_p = base->enhanced;
  avr_mega_p = base->mega;
  avr_have_movw_lpmx_p = base->have_movw_lpmx;
  avr_base_arch_macro = base->macro;
  avr_extra_arch_macro = t->macro;

  if (optimize && !TARGET_NO_TABLEJUMP)
    avr_case_values_threshold = (!AVR_MEGA || TARGET_CALL_PROLOGUES) ? 8 : 17;

  tmp_reg_rtx  = gen_rtx_REG (QImode, TMP_REGNO);
  zero_reg_rtx = gen_rtx_REG (QImode, ZERO_REGNO);
}

/*  return register class from register number.  */

static const int reg_class_tab[]={
  GENERAL_REGS,GENERAL_REGS,GENERAL_REGS,GENERAL_REGS,GENERAL_REGS,
  GENERAL_REGS,GENERAL_REGS,GENERAL_REGS,GENERAL_REGS,GENERAL_REGS,
  GENERAL_REGS,GENERAL_REGS,GENERAL_REGS,GENERAL_REGS,GENERAL_REGS,
  GENERAL_REGS, /* r0 - r15 */
  LD_REGS,LD_REGS,LD_REGS,LD_REGS,LD_REGS,LD_REGS,LD_REGS,
  LD_REGS,                      /* r16 - 23 */
  ADDW_REGS,ADDW_REGS,          /* r24,r25 */
  POINTER_X_REGS,POINTER_X_REGS, /* r26,27 */
  POINTER_Y_REGS,POINTER_Y_REGS, /* r28,r29 */
  POINTER_Z_REGS,POINTER_Z_REGS, /* r30,r31 */
  STACK_REG,STACK_REG           /* SPL,SPH */
};

/* Return register class for register R.  */

enum reg_class
avr_regno_reg_class (int r)
{
  if (r <= 33)
    return reg_class_tab[r];
  return ALL_REGS;
}

/* Return nonzero if FUNC is a naked function.  */

static int
avr_naked_function_p (tree func)
{
  tree a;

  gcc_assert (TREE_CODE (func) == FUNCTION_DECL);
  
  a = lookup_attribute ("naked", TYPE_ATTRIBUTES (TREE_TYPE (func)));
  return a != NULL_TREE;
}

/* Return nonzero if FUNC is an interrupt function as specified
   by the "interrupt" attribute.  */

static int
interrupt_function_p (tree func)
{
  tree a;

  if (TREE_CODE (func) != FUNCTION_DECL)
    return 0;

  a = lookup_attribute ("interrupt", DECL_ATTRIBUTES (func));
  return a != NULL_TREE;
}

/* Return nonzero if FUNC is a signal function as specified
   by the "signal" attribute.  */

static int
signal_function_p (tree func)
{
  tree a;

  if (TREE_CODE (func) != FUNCTION_DECL)
    return 0;

  a = lookup_attribute ("signal", DECL_ATTRIBUTES (func));
  return a != NULL_TREE;
}

/* Return the number of hard registers to push/pop in the prologue/epilogue
   of the current function, and optionally store these registers in SET.  */

static int
avr_regs_to_save (HARD_REG_SET *set)
{
  int reg, count;
  int int_or_sig_p = (interrupt_function_p (current_function_decl)
		      || signal_function_p (current_function_decl));
  int leaf_func_p = leaf_function_p ();

  if (set)
    CLEAR_HARD_REG_SET (*set);
  count = 0;

  /* No need to save any registers if the function never returns.  */
  if (TREE_THIS_VOLATILE (current_function_decl))
    return 0;

  for (reg = 0; reg < 32; reg++)
    {
      /* Do not push/pop __tmp_reg__, __zero_reg__, as well as
	 any global register variables.  */
      if (fixed_regs[reg])
	continue;

      if ((int_or_sig_p && !leaf_func_p && call_used_regs[reg])
	  || (regs_ever_live[reg]
	      && (int_or_sig_p || !call_used_regs[reg])
	      && !(frame_pointer_needed
		   && (reg == REG_Y || reg == (REG_Y+1)))))
	{
	  if (set)
	    SET_HARD_REG_BIT (*set, reg);
	  count++;
	}
    }
  return count;
}

/* Compute offset between arg_pointer and frame_pointer.  */

int
initial_elimination_offset (int from, int to)
{
  if (from == FRAME_POINTER_REGNUM && to == STACK_POINTER_REGNUM)
    return 0;
  else
    {
      int offset = frame_pointer_needed ? 2 : 0;

      offset += avr_regs_to_save (NULL);
      return get_frame_size () + 2 + 1 + offset;
    }
}

/* Return 1 if the function epilogue is just a single "ret".  */

int
avr_simple_epilogue (void)
{
  return (! frame_pointer_needed
	  && get_frame_size () == 0
	  && avr_regs_to_save (NULL) == 0
	  && ! interrupt_function_p (current_function_decl)
	  && ! signal_function_p (current_function_decl)
	  && ! avr_naked_function_p (current_function_decl)
	  && ! MAIN_NAME_P (DECL_NAME (current_function_decl))
	  && ! TREE_THIS_VOLATILE (current_function_decl));
}

/* This function checks sequence of live registers.  */

static int
sequent_regs_live (void)
{
  int reg;
  int live_seq=0;
  int cur_seq=0;

  for (reg = 0; reg < 18; ++reg)
    {
      if (!call_used_regs[reg])
	{
	  if (regs_ever_live[reg])
	    {
	      ++live_seq;
	      ++cur_seq;
	    }
	  else
	    cur_seq = 0;
	}
    }

  if (!frame_pointer_needed)
    {
      if (regs_ever_live[REG_Y])
	{
	  ++live_seq;
	  ++cur_seq;
	}
      else
	cur_seq = 0;

      if (regs_ever_live[REG_Y+1])
	{
	  ++live_seq;
	  ++cur_seq;
	}
      else
	cur_seq = 0;
    }
  else
    {
      cur_seq += 2;
      live_seq += 2;
    }
  return (cur_seq == live_seq) ? live_seq : 0;
}


/* Output to FILE the asm instructions to adjust the frame pointer by
   ADJ (r29:r28 -= ADJ;) which can be positive (prologue) or negative
   (epilogue).  Returns the number of instructions generated.  */

static int
out_adj_frame_ptr (FILE *file, int adj)
{
  int size = 0;

  if (adj)
    {
      if (TARGET_TINY_STACK)
	{
	  if (adj < -63 || adj > 63)
	    warning (0, "large frame pointer change (%d) with -mtiny-stack", adj);

	  /* The high byte (r29) doesn't change - prefer "subi" (1 cycle)
	     over "sbiw" (2 cycles, same size).  */

	  fprintf (file, (AS2 (subi, r28, %d) CR_TAB), adj);
	  size++;
	}
      else if (adj < -63 || adj > 63)
	{
	  fprintf (file, (AS2 (subi, r28, lo8(%d)) CR_TAB
			  AS2 (sbci, r29, hi8(%d)) CR_TAB),
		   adj, adj);
	  size += 2;
	}
      else if (adj < 0)
	{
	  fprintf (file, (AS2 (adiw, r28, %d) CR_TAB), -adj);
	  size++;
	}
      else
	{
	  fprintf (file, (AS2 (sbiw, r28, %d) CR_TAB), adj);
	  size++;
	}
    }
  return size;
}


/* Output to FILE the asm instructions to copy r29:r28 to SPH:SPL,
   handling various cases of interrupt enable flag state BEFORE and AFTER
   (0=disabled, 1=enabled, -1=unknown/unchanged) and target_flags.
   Returns the number of instructions generated.  */

static int
out_set_stack_ptr (FILE *file, int before, int after)
{
  int do_sph, do_cli, do_save, do_sei, lock_sph, size;

  /* The logic here is so that -mno-interrupts actually means
     "it is safe to write SPH in one instruction, then SPL in the
     next instruction, without disabling interrupts first".
     The after != -1 case (interrupt/signal) is not affected.  */

  do_sph = !TARGET_TINY_STACK;
  lock_sph = do_sph && !TARGET_NO_INTERRUPTS;
  do_cli = (before != 0 && (after == 0 || lock_sph));
  do_save = (do_cli && before == -1 && after == -1);
  do_sei = ((do_cli || before != 1) && after == 1);
  size = 1;

  if (do_save)
    {
      fprintf (file, AS2 (in, __tmp_reg__, __SREG__) CR_TAB);
      size++;
    }

  if (do_cli)
    {
      fprintf (file, "cli" CR_TAB);
      size++;
    }

  /* Do SPH first - maybe this will disable interrupts for one instruction
     someday (a suggestion has been sent to avr@atmel.com for consideration
     in future devices - that would make -mno-interrupts always safe).  */
  if (do_sph)
    {
      fprintf (file, AS2 (out, __SP_H__, r29) CR_TAB);
      size++;
    }

  /* Set/restore the I flag now - interrupts will be really enabled only
     after the next instruction.  This is not clearly documented, but
     believed to be true for all AVR devices.  */
  if (do_save)
    {
      fprintf (file, AS2 (out, __SREG__, __tmp_reg__) CR_TAB);
      size++;
    }
  else if (do_sei)
    {
      fprintf (file, "sei" CR_TAB);
      size++;
    }

  fprintf (file, AS2 (out, __SP_L__, r28) "\n");

  return size;
}


/* Output function prologue.  */

static void
avr_output_function_prologue (FILE *file, HOST_WIDE_INT size)
{
  int reg;
  int interrupt_func_p;
  int signal_func_p;
  int main_p;
  int live_seq;
  int minimize;

  last_insn_address = 0;
  jump_tables_size = 0;
  prologue_size = 0;
  fprintf (file, "/* prologue: frame size=" HOST_WIDE_INT_PRINT_DEC " */\n",
	   size);

  if (avr_naked_function_p (current_function_decl))
    {
      fputs ("/* prologue: naked */\n", file);
      goto out;
    }

  interrupt_func_p = interrupt_function_p (current_function_decl);
  signal_func_p = signal_function_p (current_function_decl);
  main_p = MAIN_NAME_P (DECL_NAME (current_function_decl));
  live_seq = sequent_regs_live ();
  minimize = (TARGET_CALL_PROLOGUES
	      && !interrupt_func_p && !signal_func_p && live_seq);

  if (interrupt_func_p)
    {
      fprintf (file,"\tsei\n");
      ++prologue_size;
    }
  if (interrupt_func_p || signal_func_p)
    {
      fprintf (file, "\t"
               AS1 (push,__zero_reg__)   CR_TAB
               AS1 (push,__tmp_reg__)    CR_TAB
	       AS2 (in,__tmp_reg__,__SREG__) CR_TAB
	       AS1 (push,__tmp_reg__)    CR_TAB
	       AS1 (clr,__zero_reg__)    "\n");
      prologue_size += 5;
    }
  if (main_p)
    {
      fprintf (file, ("\t" 
		      AS1 (ldi,r28) ",lo8(%s - " HOST_WIDE_INT_PRINT_DEC ")" CR_TAB
		      AS1 (ldi,r29) ",hi8(%s - " HOST_WIDE_INT_PRINT_DEC ")" CR_TAB
		      AS2 (out,__SP_H__,r29)     CR_TAB
		      AS2 (out,__SP_L__,r28) "\n"),
	       avr_init_stack, size, avr_init_stack, size);
      
      prologue_size += 4;
    }
  else if (minimize && (frame_pointer_needed || live_seq > 6)) 
    {
      fprintf (file, ("\t"
		      AS1 (ldi, r26) ",lo8(" HOST_WIDE_INT_PRINT_DEC ")" CR_TAB
		      AS1 (ldi, r27) ",hi8(" HOST_WIDE_INT_PRINT_DEC ")" CR_TAB), size, size);

      fputs ((AS2 (ldi,r30,pm_lo8(1f)) CR_TAB
	      AS2 (ldi,r31,pm_hi8(1f)) CR_TAB), file);
      
      prologue_size += 4;
      
      if (AVR_MEGA)
	{
	  fprintf (file, AS1 (jmp,__prologue_saves__+%d) "\n",
		   (18 - live_seq) * 2);
	  prologue_size += 2;
	}
      else
	{
	  fprintf (file, AS1 (rjmp,__prologue_saves__+%d) "\n",
		   (18 - live_seq) * 2);
	  ++prologue_size;
	}
      fputs ("1:\n", file);
    }
  else
    {
      HARD_REG_SET set;

      prologue_size += avr_regs_to_save (&set);
      for (reg = 0; reg < 32; ++reg)
	{
	  if (TEST_HARD_REG_BIT (set, reg))
	    {
	      fprintf (file, "\t" AS1 (push,%s) "\n", avr_regnames[reg]);
	    }
	}
      if (frame_pointer_needed)
	{
	  fprintf (file, "\t"
		   AS1 (push,r28) CR_TAB
		   AS1 (push,r29) CR_TAB
		   AS2 (in,r28,__SP_L__) CR_TAB
		   AS2 (in,r29,__SP_H__) "\n");
	  prologue_size += 4;
	  if (size)
	    {
	      fputs ("\t", file);
	      prologue_size += out_adj_frame_ptr (file, size);

	      if (interrupt_func_p)
		{
		  prologue_size += out_set_stack_ptr (file, 1, 1);
		}
	      else if (signal_func_p)
		{
		  prologue_size += out_set_stack_ptr (file, 0, 0);
		}
	      else
		{
		  prologue_size += out_set_stack_ptr (file, -1, -1);
		}
	    }
	}
    }

 out:
  fprintf (file, "/* prologue end (size=%d) */\n", prologue_size);
}

/* Output function epilogue.  */

static void
avr_output_function_epilogue (FILE *file, HOST_WIDE_INT size)
{
  int reg;
  int interrupt_func_p;
  int signal_func_p;
  int main_p;
  int function_size;
  int live_seq;
  int minimize;
  rtx last = get_last_nonnote_insn ();

  function_size = jump_tables_size;
  if (last)
    {
      rtx first = get_first_nonnote_insn ();
      function_size += (INSN_ADDRESSES (INSN_UID (last)) -
			INSN_ADDRESSES (INSN_UID (first)));
      function_size += get_attr_length (last);
    }

  fprintf (file, "/* epilogue: frame size=" HOST_WIDE_INT_PRINT_DEC " */\n", size);
  epilogue_size = 0;

  if (avr_naked_function_p (current_function_decl))
    {
      fputs ("/* epilogue: naked */\n", file);
      goto out;
    }

  if (last && GET_CODE (last) == BARRIER)
    {
      fputs ("/* epilogue: noreturn */\n", file);
      goto out;
    }

  interrupt_func_p = interrupt_function_p (current_function_decl);
  signal_func_p = signal_function_p (current_function_decl);
  main_p = MAIN_NAME_P (DECL_NAME (current_function_decl));
  live_seq = sequent_regs_live ();
  minimize = (TARGET_CALL_PROLOGUES
	      && !interrupt_func_p && !signal_func_p && live_seq);
  
  if (main_p)
    {
      /* Return value from main() is already in the correct registers
	 (r25:r24) as the exit() argument.  */
      if (AVR_MEGA)
	{
	  fputs ("\t" AS1 (jmp,exit) "\n", file);
	  epilogue_size += 2;
	}
      else
	{
	  fputs ("\t" AS1 (rjmp,exit) "\n", file);
	  ++epilogue_size;
	}
    }
  else if (minimize && (frame_pointer_needed || live_seq > 4))
    {
      fprintf (file, ("\t" AS2 (ldi, r30, %d) CR_TAB), live_seq);
      ++epilogue_size;
      if (frame_pointer_needed)
	{
	  epilogue_size += out_adj_frame_ptr (file, -size);
	}
      else
	{
	  fprintf (file, (AS2 (in , r28, __SP_L__) CR_TAB
			  AS2 (in , r29, __SP_H__) CR_TAB));
	  epilogue_size += 2;
	}
      
      if (AVR_MEGA)
	{
	  fprintf (file, AS1 (jmp,__epilogue_restores__+%d) "\n",
		   (18 - live_seq) * 2);
	  epilogue_size += 2;
	}
      else
	{
	  fprintf (file, AS1 (rjmp,__epilogue_restores__+%d) "\n",
		   (18 - live_seq) * 2);
	  ++epilogue_size;
	}
    }
  else
    {
      HARD_REG_SET set;

      if (frame_pointer_needed)
	{
	  if (size)
	    {
	      fputs ("\t", file);
	      epilogue_size += out_adj_frame_ptr (file, -size);

	      if (interrupt_func_p || signal_func_p)
		{
		  epilogue_size += out_set_stack_ptr (file, -1, 0);
		}
	      else
		{
		  epilogue_size += out_set_stack_ptr (file, -1, -1);
		}
	    }
	  fprintf (file, "\t"
		   AS1 (pop,r29) CR_TAB
		   AS1 (pop,r28) "\n");
	  epilogue_size += 2;
	}

      epilogue_size += avr_regs_to_save (&set);
      for (reg = 31; reg >= 0; --reg)
	{
	  if (TEST_HARD_REG_BIT (set, reg))
	    {
	      fprintf (file, "\t" AS1 (pop,%s) "\n", avr_regnames[reg]);
	    }
	}

      if (interrupt_func_p || signal_func_p)
	{
	  fprintf (file, "\t"
		   AS1 (pop,__tmp_reg__)      CR_TAB
		   AS2 (out,__SREG__,__tmp_reg__) CR_TAB
		   AS1 (pop,__tmp_reg__)      CR_TAB
		   AS1 (pop,__zero_reg__)     "\n");
	  epilogue_size += 4;
	  fprintf (file, "\treti\n");
	}
      else
	fprintf (file, "\tret\n");
      ++epilogue_size;
    }

 out:
  fprintf (file, "/* epilogue end (size=%d) */\n", epilogue_size);
  fprintf (file, "/* function %s size %d (%d) */\n", current_function_name (),
	   prologue_size + function_size + epilogue_size, function_size);
  commands_in_file += prologue_size + function_size + epilogue_size;
  commands_in_prologues += prologue_size;
  commands_in_epilogues += epilogue_size;
}


/* Return nonzero if X (an RTX) is a legitimate memory address on the target
   machine for a memory operand of mode MODE.  */

int
legitimate_address_p (enum machine_mode mode, rtx x, int strict)
{
  enum reg_class r = NO_REGS;
  
  if (TARGET_ALL_DEBUG)
    {
      fprintf (stderr, "mode: (%s) %s %s %s %s:",
	       GET_MODE_NAME(mode),
	       strict ? "(strict)": "",
	       reload_completed ? "(reload_completed)": "",
	       reload_in_progress ? "(reload_in_progress)": "",
	       reg_renumber ? "(reg_renumber)" : "");
      if (GET_CODE (x) == PLUS
	  && REG_P (XEXP (x, 0))
	  && GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) >= 0
	  && INTVAL (XEXP (x, 1)) <= MAX_LD_OFFSET (mode)
	  && reg_renumber
	  )
	fprintf (stderr, "(r%d ---> r%d)", REGNO (XEXP (x, 0)),
		 true_regnum (XEXP (x, 0)));
      debug_rtx (x);
    }
  if (REG_P (x) && (strict ? REG_OK_FOR_BASE_STRICT_P (x)
                    : REG_OK_FOR_BASE_NOSTRICT_P (x)))
    r = POINTER_REGS;
  else if (CONSTANT_ADDRESS_P (x))
    r = ALL_REGS;
  else if (GET_CODE (x) == PLUS
           && REG_P (XEXP (x, 0))
	   && GET_CODE (XEXP (x, 1)) == CONST_INT
	   && INTVAL (XEXP (x, 1)) >= 0)
    {
      int fit = INTVAL (XEXP (x, 1)) <= MAX_LD_OFFSET (mode);
      if (fit)
	{
	  if (! strict
	      || REGNO (XEXP (x,0)) == REG_Y
	      || REGNO (XEXP (x,0)) == REG_Z)
	    r = BASE_POINTER_REGS;
	  if (XEXP (x,0) == frame_pointer_rtx
	      || XEXP (x,0) == arg_pointer_rtx)
	    r = BASE_POINTER_REGS;
	}
      else if (frame_pointer_needed && XEXP (x,0) == frame_pointer_rtx)
	r = POINTER_Y_REGS;
    }
  else if ((GET_CODE (x) == PRE_DEC || GET_CODE (x) == POST_INC)
           && REG_P (XEXP (x, 0))
           && (strict ? REG_OK_FOR_BASE_STRICT_P (XEXP (x, 0))
               : REG_OK_FOR_BASE_NOSTRICT_P (XEXP (x, 0))))
    {
      r = POINTER_REGS;
    }
  if (TARGET_ALL_DEBUG)
    {
      fprintf (stderr, "   ret = %c\n", r + '0');
    }
  return r == NO_REGS ? 0 : (int)r;
}

/* Attempts to replace X with a valid
   memory address for an operand of mode MODE  */

rtx
legitimize_address (rtx x, rtx oldx, enum machine_mode mode)
{
  x = oldx;
  if (TARGET_ALL_DEBUG)
    {
      fprintf (stderr, "legitimize_address mode: %s", GET_MODE_NAME(mode));
      debug_rtx (oldx);
    }
  
  if (GET_CODE (oldx) == PLUS
      && REG_P (XEXP (oldx,0)))
    {
      if (REG_P (XEXP (oldx,1)))
	x = force_reg (GET_MODE (oldx), oldx);
      else if (GET_CODE (XEXP (oldx, 1)) == CONST_INT)
	{
	  int offs = INTVAL (XEXP (oldx,1));
	  if (frame_pointer_rtx != XEXP (oldx,0))
	    if (offs > MAX_LD_OFFSET (mode))
	      {
		if (TARGET_ALL_DEBUG)
		  fprintf (stderr, "force_reg (big offset)\n");
		x = force_reg (GET_MODE (oldx), oldx);
	      }
	}
    }
  return x;
}


/* Return a pointer register name as a string.  */

static const char *
ptrreg_to_str (int regno)
{
  switch (regno)
    {
    case REG_X: return "X";
    case REG_Y: return "Y";
    case REG_Z: return "Z";
    default:
      output_operand_lossage ("address operand requires constraint for X, Y, or Z register");
    }
  return NULL;
}

/* Return the condition name as a string.
   Used in conditional jump constructing  */

static const char *
cond_string (enum rtx_code code)
{
  switch (code)
    {
    case NE:
      return "ne";
    case EQ:
      return "eq";
    case GE:
      if (cc_prev_status.flags & CC_OVERFLOW_UNUSABLE)
	return "pl";
      else
	return "ge";
    case LT:
      if (cc_prev_status.flags & CC_OVERFLOW_UNUSABLE)
	return "mi";
      else
	return "lt";
    case GEU:
      return "sh";
    case LTU:
      return "lo";
    default:
      gcc_unreachable ();
    }
}

/* Output ADDR to FILE as address.  */

void
print_operand_address (FILE *file, rtx addr)
{
  switch (GET_CODE (addr))
    {
    case REG:
      fprintf (file, ptrreg_to_str (REGNO (addr)));
      break;

    case PRE_DEC:
      fprintf (file, "-%s", ptrreg_to_str (REGNO (XEXP (addr, 0))));
      break;

    case POST_INC:
      fprintf (file, "%s+", ptrreg_to_str (REGNO (XEXP (addr, 0))));
      break;

    default:
      if (CONSTANT_ADDRESS_P (addr)
	  && ((GET_CODE (addr) == SYMBOL_REF && SYMBOL_REF_FUNCTION_P (addr))
	      || GET_CODE (addr) == LABEL_REF))
	{
	  fprintf (file, "pm(");
	  output_addr_const (file,addr);
	  fprintf (file ,")");
	}
      else
	output_addr_const (file, addr);
    }
}


/* Output X as assembler operand to file FILE.  */
     
void
print_operand (FILE *file, rtx x, int code)
{
  int abcd = 0;

  if (code >= 'A' && code <= 'D')
    abcd = code - 'A';

  if (code == '~')
    {
      if (!AVR_MEGA)
	fputc ('r', file);
    }
  else if (REG_P (x))
    {
      if (x == zero_reg_rtx)
	fprintf (file, "__zero_reg__");
      else
	fprintf (file, reg_names[true_regnum (x) + abcd]);
    }
  else if (GET_CODE (x) == CONST_INT)
    fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x) + abcd);
  else if (GET_CODE (x) == MEM)
    {
      rtx addr = XEXP (x,0);

      if (CONSTANT_P (addr) && abcd)
	{
	  fputc ('(', file);
	  output_address (addr);
	  fprintf (file, ")+%d", abcd);
	}
      else if (code == 'o')
	{
	  if (GET_CODE (addr) != PLUS)
	    fatal_insn ("bad address, not (reg+disp):", addr);

	  print_operand (file, XEXP (addr, 1), 0);
	}
      else if (code == 'p' || code == 'r')
        {
          if (GET_CODE (addr) != POST_INC && GET_CODE (addr) != PRE_DEC)
            fatal_insn ("bad address, not post_inc or pre_dec:", addr);
          
          if (code == 'p')
            print_operand_address (file, XEXP (addr, 0));  /* X, Y, Z */
          else
            print_operand (file, XEXP (addr, 0), 0);  /* r26, r28, r30 */
        }
      else if (GET_CODE (addr) == PLUS)
	{
	  print_operand_address (file, XEXP (addr,0));
	  if (REGNO (XEXP (addr, 0)) == REG_X)
	    fatal_insn ("internal compiler error.  Bad address:"
			,addr);
	  fputc ('+', file);
	  print_operand (file, XEXP (addr,1), code);
	}
      else
	print_operand_address (file, addr);
    }
  else if (GET_CODE (x) == CONST_DOUBLE)
    {
      long val;
      REAL_VALUE_TYPE rv;
      if (GET_MODE (x) != SFmode)
	fatal_insn ("internal compiler error.  Unknown mode:", x);
      REAL_VALUE_FROM_CONST_DOUBLE (rv, x);
      REAL_VALUE_TO_TARGET_SINGLE (rv, val);
      fprintf (file, "0x%lx", val);
    }
  else if (code == 'j')
    fputs (cond_string (GET_CODE (x)), file);
  else if (code == 'k')
    fputs (cond_string (reverse_condition (GET_CODE (x))), file);
  else
    print_operand_address (file, x);
}

/* Recognize operand OP of mode MODE used in call instructions.  */

int
call_insn_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (GET_CODE (op) == MEM)
    {
      rtx inside = XEXP (op, 0);
      if (register_operand (inside, Pmode))
        return 1;
      if (CONSTANT_ADDRESS_P (inside))
        return 1;
    }
  return 0;
}

/* Update the condition code in the INSN.  */

void
notice_update_cc (rtx body ATTRIBUTE_UNUSED, rtx insn)
{
  rtx set;
  
  switch (get_attr_cc (insn))
    {
    case CC_NONE:
      /* Insn does not affect CC at all.  */
      break;

    case CC_SET_N:
      CC_STATUS_INIT;
      break;

    case CC_SET_ZN:
      set = single_set (insn);
      CC_STATUS_INIT;
      if (set)
	{
	  cc_status.flags |= CC_NO_OVERFLOW;
	  cc_status.value1 = SET_DEST (set);
	}
      break;

    case CC_SET_CZN:
      /* Insn sets the Z,N,C flags of CC to recog_operand[0].
         The V flag may or may not be known but that's ok because
         alter_cond will change tests to use EQ/NE.  */
      set = single_set (insn);
      CC_STATUS_INIT;
      if (set)
	{
	  cc_status.value1 = SET_DEST (set);
	  cc_status.flags |= CC_OVERFLOW_UNUSABLE;
	}
      break;

    case CC_COMPARE:
      set = single_set (insn);
      CC_STATUS_INIT;
      if (set)
	cc_status.value1 = SET_SRC (set);
      break;
      
    case CC_CLOBBER:
      /* Insn doesn't leave CC in a usable state.  */
      CC_STATUS_INIT;

      /* Correct CC for the ashrqi3 with the shift count as CONST_INT != 6 */
      set = single_set (insn);
      if (set)
	{
	  rtx src = SET_SRC (set);
	  
	  if (GET_CODE (src) == ASHIFTRT
	      && GET_MODE (src) == QImode)
	    {
	      rtx x = XEXP (src, 1);

	      if (GET_CODE (x) == CONST_INT
		  && INTVAL (x) > 0
		  && INTVAL (x) != 6)
		{
		  cc_status.value1 = SET_DEST (set);
		  cc_status.flags |= CC_OVERFLOW_UNUSABLE;
		}
	    }
	}
      break;
    }
}

/* Return maximum number of consecutive registers of
   class CLASS needed to hold a value of mode MODE.  */

int
class_max_nregs (enum reg_class class ATTRIBUTE_UNUSED,enum machine_mode mode)
{
  return ((GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD);
}

/* Choose mode for jump insn:
   1 - relative jump in range -63 <= x <= 62 ;
   2 - relative jump in range -2046 <= x <= 2045 ;
   3 - absolute jump (only for ATmega[16]03).  */

int
avr_jump_mode (rtx x, rtx insn)
{
  int dest_addr = INSN_ADDRESSES (INSN_UID (GET_MODE (x) == LABEL_REF
					    ? XEXP (x, 0) : x));
  int cur_addr = INSN_ADDRESSES (INSN_UID (insn));
  int jump_distance = cur_addr - dest_addr;
  
  if (-63 <= jump_distance && jump_distance <= 62)
    return 1;
  else if (-2046 <= jump_distance && jump_distance <= 2045)
    return 2;
  else if (AVR_MEGA)
    return 3;
  
  return 2;
}

/* return an AVR condition jump commands.
   X is a comparison RTX.
   LEN is a number returned by avr_jump_mode function.
   if REVERSE nonzero then condition code in X must be reversed.  */

const char *
ret_cond_branch (rtx x, int len, int reverse)
{
  RTX_CODE cond = reverse ? reverse_condition (GET_CODE (x)) : GET_CODE (x);
  
  switch (cond)
    {
    case GT:
      if (cc_prev_status.flags & CC_OVERFLOW_UNUSABLE)
	return (len == 1 ? (AS1 (breq,.+2) CR_TAB
			    AS1 (brpl,%0)) :
		len == 2 ? (AS1 (breq,.+4) CR_TAB
			    AS1 (brmi,.+2) CR_TAB
			    AS1 (rjmp,%0)) :
		(AS1 (breq,.+6) CR_TAB
		 AS1 (brmi,.+4) CR_TAB
		 AS1 (jmp,%0)));
	  
      else
	return (len == 1 ? (AS1 (breq,.+2) CR_TAB
			    AS1 (brge,%0)) :
		len == 2 ? (AS1 (breq,.+4) CR_TAB
			    AS1 (brlt,.+2) CR_TAB
			    AS1 (rjmp,%0)) :
		(AS1 (breq,.+6) CR_TAB
		 AS1 (brlt,.+4) CR_TAB
		 AS1 (jmp,%0)));
    case GTU:
      return (len == 1 ? (AS1 (breq,.+2) CR_TAB
                          AS1 (brsh,%0)) :
              len == 2 ? (AS1 (breq,.+4) CR_TAB
                          AS1 (brlo,.+2) CR_TAB
                          AS1 (rjmp,%0)) :
              (AS1 (breq,.+6) CR_TAB
               AS1 (brlo,.+4) CR_TAB
               AS1 (jmp,%0)));
    case LE:
      if (cc_prev_status.flags & CC_OVERFLOW_UNUSABLE)
	return (len == 1 ? (AS1 (breq,%0) CR_TAB
			    AS1 (brmi,%0)) :
		len == 2 ? (AS1 (breq,.+2) CR_TAB
			    AS1 (brpl,.+2) CR_TAB
			    AS1 (rjmp,%0)) :
		(AS1 (breq,.+2) CR_TAB
		 AS1 (brpl,.+4) CR_TAB
		 AS1 (jmp,%0)));
      else
	return (len == 1 ? (AS1 (breq,%0) CR_TAB
			    AS1 (brlt,%0)) :
		len == 2 ? (AS1 (breq,.+2) CR_TAB
			    AS1 (brge,.+2) CR_TAB
			    AS1 (rjmp,%0)) :
		(AS1 (breq,.+2) CR_TAB
		 AS1 (brge,.+4) CR_TAB
		 AS1 (jmp,%0)));
    case LEU:
      return (len == 1 ? (AS1 (breq,%0) CR_TAB
                          AS1 (brlo,%0)) :
              len == 2 ? (AS1 (breq,.+2) CR_TAB
                          AS1 (brsh,.+2) CR_TAB
			  AS1 (rjmp,%0)) :
              (AS1 (breq,.+2) CR_TAB
               AS1 (brsh,.+4) CR_TAB
	       AS1 (jmp,%0)));
    default:
      if (reverse)
	{
	  switch (len)
	    {
	    case 1:
	      return AS1 (br%k1,%0);
	    case 2:
	      return (AS1 (br%j1,.+2) CR_TAB
		      AS1 (rjmp,%0));
	    default:
	      return (AS1 (br%j1,.+4) CR_TAB
		      AS1 (jmp,%0));
	    }
	}
	else
	  {
	    switch (len)
	      {
	      case 1:
		return AS1 (br%j1,%0);
	      case 2:
		return (AS1 (br%k1,.+2) CR_TAB
			AS1 (rjmp,%0));
	      default:
		return (AS1 (br%k1,.+4) CR_TAB
			AS1 (jmp,%0));
	      }
	  }
    }
  return "";
}

/* Predicate function for immediate operand which fits to byte (8bit) */

int
byte_immediate_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return (GET_CODE (op) == CONST_INT
          && INTVAL (op) <= 0xff && INTVAL (op) >= 0);
}

/* Output all insn addresses and their sizes into the assembly language
   output file.  This is helpful for debugging whether the length attributes
   in the md file are correct.
   Output insn cost for next insn.  */

void
final_prescan_insn (rtx insn, rtx *operand ATTRIBUTE_UNUSED,
		    int num_operands ATTRIBUTE_UNUSED)
{
  int uid = INSN_UID (insn);

  if (TARGET_INSN_SIZE_DUMP || TARGET_ALL_DEBUG)
    {
      fprintf (asm_out_file, "/*DEBUG: 0x%x\t\t%d\t%d */\n",
	       INSN_ADDRESSES (uid),
               INSN_ADDRESSES (uid) - last_insn_address,
	       rtx_cost (PATTERN (insn), INSN));
    }
  last_insn_address = INSN_ADDRESSES (uid);
}

/* Return 0 if undefined, 1 if always true or always false.  */

int
avr_simplify_comparison_p (enum machine_mode mode, RTX_CODE operator, rtx x)
{
  unsigned int max = (mode == QImode ? 0xff :
                      mode == HImode ? 0xffff :
                      mode == SImode ? 0xffffffff : 0);
  if (max && operator && GET_CODE (x) == CONST_INT)
    {
      if (unsigned_condition (operator) != operator)
	max >>= 1;

      if (max != (INTVAL (x) & max)
	  && INTVAL (x) != 0xff)
	return 1;
    }
  return 0;
}


/* Returns nonzero if REGNO is the number of a hard
   register in which function arguments are sometimes passed.  */

int
function_arg_regno_p(int r)
{
  return (r >= 8 && r <= 25);
}

/* Initializing the variable cum for the state at the beginning
   of the argument list.  */

void
init_cumulative_args (CUMULATIVE_ARGS *cum, tree fntype, rtx libname,
		      tree fndecl ATTRIBUTE_UNUSED)
{
  cum->nregs = 18;
  cum->regno = FIRST_CUM_REG;
  if (!libname && fntype)
    {
      int stdarg = (TYPE_ARG_TYPES (fntype) != 0
                    && (TREE_VALUE (tree_last (TYPE_ARG_TYPES (fntype)))
                        != void_type_node));
      if (stdarg)
        cum->nregs = 0;
    }
}

/* Returns the number of registers to allocate for a function argument.  */

static int
avr_num_arg_regs (enum machine_mode mode, tree type)
{
  int size;

  if (mode == BLKmode)
    size = int_size_in_bytes (type);
  else
    size = GET_MODE_SIZE (mode);

  /* Align all function arguments to start in even-numbered registers.
     Odd-sized arguments leave holes above them.  */

  return (size + 1) & ~1;
}

/* Controls whether a function argument is passed
   in a register, and which register.  */

rtx
function_arg (CUMULATIVE_ARGS *cum, enum machine_mode mode, tree type,
	      int named ATTRIBUTE_UNUSED)
{
  int bytes = avr_num_arg_regs (mode, type);

  if (cum->nregs && bytes <= cum->nregs)
    return gen_rtx_REG (mode, cum->regno - bytes);

  return NULL_RTX;
}

/* Update the summarizer variable CUM to advance past an argument
   in the argument list.  */
   
void
function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode, tree type,
		      int named ATTRIBUTE_UNUSED)
{
  int bytes = avr_num_arg_regs (mode, type);

  cum->nregs -= bytes;
  cum->regno -= bytes;

  if (cum->nregs <= 0)
    {
      cum->nregs = 0;
      cum->regno = FIRST_CUM_REG;
    }
}

/***********************************************************************
  Functions for outputting various mov's for a various modes
************************************************************************/
const char *
output_movqi (rtx insn, rtx operands[], int *l)
{
  int dummy;
  rtx dest = operands[0];
  rtx src = operands[1];
  int *real_l = l;
  
  if (!l)
    l = &dummy;

  *l = 1;
  
  if (register_operand (dest, QImode))
    {
      if (register_operand (src, QImode)) /* mov r,r */
	{
	  if (test_hard_reg_class (STACK_REG, dest))
	    return AS2 (out,%0,%1);
	  else if (test_hard_reg_class (STACK_REG, src))
	    return AS2 (in,%0,%1);
	  
	  return AS2 (mov,%0,%1);
	}
      else if (CONSTANT_P (src))
	{
	  if (test_hard_reg_class (LD_REGS, dest)) /* ldi d,i */
	    return AS2 (ldi,%0,lo8(%1));
	  
	  if (GET_CODE (src) == CONST_INT)
	    {
	      if (src == const0_rtx) /* mov r,L */
		return AS1 (clr,%0);
	      else if (src == const1_rtx)
		{
		  *l = 2;
		  return (AS1 (clr,%0) CR_TAB
			  AS1 (inc,%0));
		}
	      else if (src == constm1_rtx)
		{
		  /* Immediate constants -1 to any register */
		  *l = 2;
		  return (AS1 (clr,%0) CR_TAB
			  AS1 (dec,%0));
		}
	      else
		{
		  int bit_nr = exact_log2 (INTVAL (src));

		  if (bit_nr >= 0)
		    {
		      *l = 3;
		      if (!real_l)
			output_asm_insn ((AS1 (clr,%0) CR_TAB
					  "set"), operands);
		      if (!real_l)
			avr_output_bld (operands, bit_nr);

		      return "";
		    }
		}
	    }
	  
	  /* Last resort, larger than loading from memory.  */
	  *l = 4;
	  return (AS2 (mov,__tmp_reg__,r31) CR_TAB
		  AS2 (ldi,r31,lo8(%1))     CR_TAB
		  AS2 (mov,%0,r31)          CR_TAB
		  AS2 (mov,r31,__tmp_reg__));
	}
      else if (GET_CODE (src) == MEM)
	return out_movqi_r_mr (insn, operands, real_l); /* mov r,m */
    }
  else if (GET_CODE (dest) == MEM)
    {
      const char *template;

      if (src == const0_rtx)
	operands[1] = zero_reg_rtx;

      template = out_movqi_mr_r (insn, operands, real_l);

      if (!real_l)
	output_asm_insn (template, operands);

      operands[1] = src;
    }
  return "";
}


const char *
output_movhi (rtx insn, rtx operands[], int *l)
{
  int dummy;
  rtx dest = operands[0];
  rtx src = operands[1];
  int *real_l = l;
  
  if (!l)
    l = &dummy;
  
  if (register_operand (dest, HImode))
    {
      if (register_operand (src, HImode)) /* mov r,r */
	{
	  if (test_hard_reg_class (STACK_REG, dest))
	    {
	      if (TARGET_TINY_STACK)
		{
		  *l = 1;
		  return AS2 (out,__SP_L__,%A1);
		}
	      else if (TARGET_NO_INTERRUPTS)
		{
		  *l = 2;
		  return (AS2 (out,__SP_H__,%B1) CR_TAB
			  AS2 (out,__SP_L__,%A1));
		}

	      *l = 5;
	      return (AS2 (in,__tmp_reg__,__SREG__)  CR_TAB
		      "cli"                          CR_TAB
		      AS2 (out,__SP_H__,%B1)         CR_TAB
		      AS2 (out,__SREG__,__tmp_reg__) CR_TAB
		      AS2 (out,__SP_L__,%A1));
	    }
	  else if (test_hard_reg_class (STACK_REG, src))
	    {
	      *l = 2;	
	      return (AS2 (in,%A0,__SP_L__) CR_TAB
		      AS2 (in,%B0,__SP_H__));
	    }

	  if (AVR_HAVE_MOVW)
	    {
	      *l = 1;
	      return (AS2 (movw,%0,%1));
	    }

	  if (true_regnum (dest) > true_regnum (src))
	    {
	      *l = 2;
	      return (AS2 (mov,%B0,%B1) CR_TAB
		      AS2 (mov,%A0,%A1));
	    }
	  else
	    {
	      *l = 2;
	      return (AS2 (mov,%A0,%A1) CR_TAB
		      AS2 (mov,%B0,%B1));
	    }
	}
      else if (CONSTANT_P (src))
	{
	  if (test_hard_reg_class (LD_REGS, dest)) /* ldi d,i */
	    {
	      *l = 2;
	      return (AS2 (ldi,%A0,lo8(%1)) CR_TAB
		      AS2 (ldi,%B0,hi8(%1)));
	    }
	  
	  if (GET_CODE (src) == CONST_INT)
	    {
	      if (src == const0_rtx) /* mov r,L */
		{
		  *l = 2;
		  return (AS1 (clr,%A0) CR_TAB
			  AS1 (clr,%B0));
		}
	      else if (src == const1_rtx)
		{
		  *l = 3;
		  return (AS1 (clr,%A0) CR_TAB
			  AS1 (clr,%B0) CR_TAB
			  AS1 (inc,%A0));
		}
	      else if (src == constm1_rtx)
		{
		  /* Immediate constants -1 to any register */
		  *l = 3;
		  return (AS1 (clr,%0)  CR_TAB
			  AS1 (dec,%A0) CR_TAB
			  AS2 (mov,%B0,%A0));
		}
	      else
		{
		  int bit_nr = exact_log2 (INTVAL (src));

		  if (bit_nr >= 0)
		    {
		      *l = 4;
		      if (!real_l)
			output_asm_insn ((AS1 (clr,%A0) CR_TAB
					  AS1 (clr,%B0) CR_TAB
					  "set"), operands);
		      if (!real_l)
			avr_output_bld (operands, bit_nr);

		      return "";
		    }
		}

	      if ((INTVAL (src) & 0xff) == 0)
		{
		  *l = 5;
		  return (AS2 (mov,__tmp_reg__,r31) CR_TAB
			  AS1 (clr,%A0)             CR_TAB
			  AS2 (ldi,r31,hi8(%1))     CR_TAB
			  AS2 (mov,%B0,r31)         CR_TAB
			  AS2 (mov,r31,__tmp_reg__));
		}
	      else if ((INTVAL (src) & 0xff00) == 0)
		{
		  *l = 5;
		  return (AS2 (mov,__tmp_reg__,r31) CR_TAB
			  AS2 (ldi,r31,lo8(%1))     CR_TAB
			  AS2 (mov,%A0,r31)         CR_TAB
			  AS1 (clr,%B0)             CR_TAB
			  AS2 (mov,r31,__tmp_reg__));
		}
	    }
	  
	  /* Last resort, equal to loading from memory.  */
	  *l = 6;
	  return (AS2 (mov,__tmp_reg__,r31) CR_TAB
		  AS2 (ldi,r31,lo8(%1))     CR_TAB
		  AS2 (mov,%A0,r31)         CR_TAB
		  AS2 (ldi,r31,hi8(%1))     CR_TAB
		  AS2 (mov,%B0,r31)         CR_TAB
		  AS2 (mov,r31,__tmp_reg__));
	}
      else if (GET_CODE (src) == MEM)
	return out_movhi_r_mr (insn, operands, real_l); /* mov r,m */
    }
  else if (GET_CODE (dest) == MEM)
    {
      const char *template;

      if (src == const0_rtx)
	operands[1] = zero_reg_rtx;

      template = out_movhi_mr_r (insn, operands, real_l);

      if (!real_l)
	output_asm_insn (template, operands);

      operands[1] = src;
      return "";
    }
  fatal_insn ("invalid insn:", insn);
  return "";
}

const char *
out_movqi_r_mr (rtx insn, rtx op[], int *l)
{
  rtx dest = op[0];
  rtx src = op[1];
  rtx x = XEXP (src, 0);
  int dummy;
  
  if (!l)
    l = &dummy;
  
  if (CONSTANT_ADDRESS_P (x))
    {
      if (avr_io_address_p (x, 1))
	{
	  *l = 1;
	  return AS2 (in,%0,%1-0x20);
	}
      *l = 2;
      return AS2 (lds,%0,%1);
    }
  /* memory access by reg+disp */
  else if (GET_CODE (x) == PLUS
      && REG_P (XEXP (x,0))
      && GET_CODE (XEXP (x,1)) == CONST_INT)
    {
      if ((INTVAL (XEXP (x,1)) - GET_MODE_SIZE (GET_MODE (src))) >= 63)
	{
	  int disp = INTVAL (XEXP (x,1));
	  if (REGNO (XEXP (x,0)) != REG_Y)
	    fatal_insn ("incorrect insn:",insn);

	  if (disp <= 63 + MAX_LD_OFFSET (GET_MODE (src)))
	    return *l = 3, (AS2 (adiw,r28,%o1-63) CR_TAB
			    AS2 (ldd,%0,Y+63)     CR_TAB
			    AS2 (sbiw,r28,%o1-63));

	  return *l = 5, (AS2 (subi,r28,lo8(-%o1)) CR_TAB
			  AS2 (sbci,r29,hi8(-%o1)) CR_TAB
			  AS2 (ld,%0,Y)            CR_TAB
			  AS2 (subi,r28,lo8(%o1))  CR_TAB
			  AS2 (sbci,r29,hi8(%o1)));
	}
      else if (REGNO (XEXP (x,0)) == REG_X)
	{
	  /* This is a paranoid case LEGITIMIZE_RELOAD_ADDRESS must exclude
	     it but I have this situation with extremal optimizing options.  */
	  if (reg_overlap_mentioned_p (dest, XEXP (x,0))
	      || reg_unused_after (insn, XEXP (x,0)))
	    return *l = 2, (AS2 (adiw,r26,%o1) CR_TAB
			    AS2 (ld,%0,X));

	  return *l = 3, (AS2 (adiw,r26,%o1) CR_TAB
			  AS2 (ld,%0,X)      CR_TAB
			  AS2 (sbiw,r26,%o1));
	}
      *l = 1;
      return AS2 (ldd,%0,%1);
    }
  *l = 1;
  return AS2 (ld,%0,%1);
}

const char *
out_movhi_r_mr (rtx insn, rtx op[], int *l)
{
  rtx dest = op[0];
  rtx src = op[1];
  rtx base = XEXP (src, 0);
  int reg_dest = true_regnum (dest);
  int reg_base = true_regnum (base);
  /* "volatile" forces reading low byte first, even if less efficient,
     for correct operation with 16-bit I/O registers.  */
  int mem_volatile_p = MEM_VOLATILE_P (src);
  int tmp;

  if (!l)
    l = &tmp;

  if (reg_base > 0)
    {
      if (reg_dest == reg_base)         /* R = (R) */
	{
	  *l = 3;
	  return (AS2 (ld,__tmp_reg__,%1+) CR_TAB
		  AS2 (ld,%B0,%1) CR_TAB
		  AS2 (mov,%A0,__tmp_reg__));
	}
      else if (reg_base == REG_X)        /* (R26) */
        {
          if (reg_unused_after (insn, base))
	    {
	      *l = 2;
	      return (AS2 (ld,%A0,X+) CR_TAB
		      AS2 (ld,%B0,X));
	    }
	  *l  = 3;
	  return (AS2 (ld,%A0,X+) CR_TAB
		  AS2 (ld,%B0,X) CR_TAB
		  AS2 (sbiw,r26,1));
        }
      else                      /* (R)  */
	{
	  *l = 2;
	  return (AS2 (ld,%A0,%1)    CR_TAB
		  AS2 (ldd,%B0,%1+1));
	}
    }
  else if (GET_CODE (base) == PLUS) /* (R + i) */
    {
      int disp = INTVAL (XEXP (base, 1));
      int reg_base = true_regnum (XEXP (base, 0));
      
      if (disp > MAX_LD_OFFSET (GET_MODE (src)))
	{
	  if (REGNO (XEXP (base, 0)) != REG_Y)
	    fatal_insn ("incorrect insn:",insn);
	  
	  if (disp <= 63 + MAX_LD_OFFSET (GET_MODE (src)))
	    return *l = 4, (AS2 (adiw,r28,%o1-62) CR_TAB
			    AS2 (ldd,%A0,Y+62)    CR_TAB
			    AS2 (ldd,%B0,Y+63)    CR_TAB
			    AS2 (sbiw,r28,%o1-62));

	  return *l = 6, (AS2 (subi,r28,lo8(-%o1)) CR_TAB
			  AS2 (sbci,r29,hi8(-%o1)) CR_TAB
			  AS2 (ld,%A0,Y)           CR_TAB
			  AS2 (ldd,%B0,Y+1)        CR_TAB
			  AS2 (subi,r28,lo8(%o1))  CR_TAB
			  AS2 (sbci,r29,hi8(%o1)));
	}
      if (reg_base == REG_X)
	{
	  /* This is a paranoid case. LEGITIMIZE_RELOAD_ADDRESS must exclude
	     it but I have this situation with extremal
	     optimization options.  */
	  
	  *l = 4;
	  if (reg_base == reg_dest)
	    return (AS2 (adiw,r26,%o1)      CR_TAB
		    AS2 (ld,__tmp_reg__,X+) CR_TAB
		    AS2 (ld,%B0,X)          CR_TAB
		    AS2 (mov,%A0,__tmp_reg__));

	  return (AS2 (adiw,r26,%o1) CR_TAB
		  AS2 (ld,%A0,X+)    CR_TAB
		  AS2 (ld,%B0,X)     CR_TAB
		  AS2 (sbiw,r26,%o1+1));
	}

      if (reg_base == reg_dest)
	{
	  *l = 3;
	  return (AS2 (ldd,__tmp_reg__,%A1) CR_TAB
		  AS2 (ldd,%B0,%B1)         CR_TAB
		  AS2 (mov,%A0,__tmp_reg__));
	}
      
      *l = 2;
      return (AS2 (ldd,%A0,%A1) CR_TAB
	      AS2 (ldd,%B0,%B1));
    }
  else if (GET_CODE (base) == PRE_DEC) /* (--R) */
    {
      if (reg_overlap_mentioned_p (dest, XEXP (base, 0)))
	fatal_insn ("incorrect insn:", insn);

      if (mem_volatile_p)
        {
          if (REGNO (XEXP (base, 0)) == REG_X)
            {
              *l = 4;
              return (AS2 (sbiw,r26,2)  CR_TAB
                      AS2 (ld,%A0,X+)   CR_TAB
                      AS2 (ld,%B0,X)    CR_TAB
                      AS2 (sbiw,r26,1));
            }
          else
            {
              *l = 3;
              return (AS2 (sbiw,%r1,2)   CR_TAB
                      AS2 (ld,%A0,%p1)  CR_TAB
                      AS2 (ldd,%B0,%p1+1));
            }
        }

      *l = 2;
      return (AS2 (ld,%B0,%1) CR_TAB
	      AS2 (ld,%A0,%1));
    }
  else if (GET_CODE (base) == POST_INC) /* (R++) */
    {
      if (reg_overlap_mentioned_p (dest, XEXP (base, 0)))
	fatal_insn ("incorrect insn:", insn);

      *l = 2;
      return (AS2 (ld,%A0,%1)  CR_TAB
	      AS2 (ld,%B0,%1));
    }
  else if (CONSTANT_ADDRESS_P (base))
    {
      if (avr_io_address_p (base, 2))
	{
	  *l = 2;
	  return (AS2 (in,%A0,%A1-0x20) CR_TAB
		  AS2 (in,%B0,%B1-0x20));
	}
      *l = 4;
      return (AS2 (lds,%A0,%A1) CR_TAB
	      AS2 (lds,%B0,%B1));
    }
  
  fatal_insn ("unknown move insn:",insn);
  return "";
}

const char *
out_movsi_r_mr (rtx insn, rtx op[], int *l)
{
  rtx dest = op[0];
  rtx src = op[1];
  rtx base = XEXP (src, 0);
  int reg_dest = true_regnum (dest);
  int reg_base = true_regnum (base);
  int tmp;

  if (!l)
    l = &tmp;
  
  if (reg_base > 0)
    {
      if (reg_base == REG_X)        /* (R26) */
        {
          if (reg_dest == REG_X)
	    /* "ld r26,-X" is undefined */
	    return *l=7, (AS2 (adiw,r26,3)        CR_TAB
			  AS2 (ld,r29,X)          CR_TAB
			  AS2 (ld,r28,-X)         CR_TAB
			  AS2 (ld,__tmp_reg__,-X) CR_TAB
			  AS2 (sbiw,r26,1)        CR_TAB
			  AS2 (ld,r26,X)          CR_TAB
			  AS2 (mov,r27,__tmp_reg__));
          else if (reg_dest == REG_X - 2)
            return *l=5, (AS2 (ld,%A0,X+)  CR_TAB
                          AS2 (ld,%B0,X+) CR_TAB
                          AS2 (ld,__tmp_reg__,X+)  CR_TAB
                          AS2 (ld,%D0,X)  CR_TAB
                          AS2 (mov,%C0,__tmp_reg__));
          else if (reg_unused_after (insn, base))
            return  *l=4, (AS2 (ld,%A0,X+)  CR_TAB
                           AS2 (ld,%B0,X+) CR_TAB
                           AS2 (ld,%C0,X+) CR_TAB
                           AS2 (ld,%D0,X));
          else
            return  *l=5, (AS2 (ld,%A0,X+)  CR_TAB
                           AS2 (ld,%B0,X+) CR_TAB
                           AS2 (ld,%C0,X+) CR_TAB
                           AS2 (ld,%D0,X)  CR_TAB
                           AS2 (sbiw,r26,3));
        }
      else
        {
          if (reg_dest == reg_base)
            return *l=5, (AS2 (ldd,%D0,%1+3) CR_TAB
                          AS2 (ldd,%C0,%1+2) CR_TAB
                          AS2 (ldd,__tmp_reg__,%1+1)  CR_TAB
                          AS2 (ld,%A0,%1)  CR_TAB
                          AS2 (mov,%B0,__tmp_reg__));
          else if (reg_base == reg_dest + 2)
            return *l=5, (AS2 (ld ,%A0,%1)    CR_TAB
                          AS2 (ldd,%B0,%1+1) CR_TAB
                          AS2 (ldd,__tmp_reg__,%1+2)  CR_TAB
                          AS2 (ldd,%D0,%1+3) CR_TAB
                          AS2 (mov,%C0,__tmp_reg__));
          else
            return *l=4, (AS2 (ld ,%A0,%1)   CR_TAB
                          AS2 (ldd,%B0,%1+1) CR_TAB
                          AS2 (ldd,%C0,%1+2) CR_TAB
                          AS2 (ldd,%D0,%1+3));
        }
    }
  else if (GET_CODE (base) == PLUS) /* (R + i) */
    {
      int disp = INTVAL (XEXP (base, 1));
      
      if (disp > MAX_LD_OFFSET (GET_MODE (src)))
	{
	  if (REGNO (XEXP (base, 0)) != REG_Y)
	    fatal_insn ("incorrect insn:",insn);

	  if (disp <= 63 + MAX_LD_OFFSET (GET_MODE (src)))
	    return *l = 6, (AS2 (adiw,r28,%o1-60) CR_TAB
			    AS2 (ldd,%A0,Y+60)    CR_TAB
			    AS2 (ldd,%B0,Y+61)    CR_TAB
			    AS2 (ldd,%C0,Y+62)    CR_TAB
			    AS2 (ldd,%D0,Y+63)    CR_TAB
			    AS2 (sbiw,r28,%o1-60));

	  return *l = 8, (AS2 (subi,r28,lo8(-%o1)) CR_TAB
			  AS2 (sbci,r29,hi8(-%o1)) CR_TAB
			  AS2 (ld,%A0,Y)           CR_TAB
			  AS2 (ldd,%B0,Y+1)        CR_TAB
			  AS2 (ldd,%C0,Y+2)        CR_TAB
			  AS2 (ldd,%D0,Y+3)        CR_TAB
			  AS2 (subi,r28,lo8(%o1))  CR_TAB
			  AS2 (sbci,r29,hi8(%o1)));
	}

      reg_base = true_regnum (XEXP (base, 0));
      if (reg_base == REG_X)
	{
	  /* R = (X + d) */
	  if (reg_dest == REG_X)
	    {
	      *l = 7;
	      /* "ld r26,-X" is undefined */
	      return (AS2 (adiw,r26,%o1+3)    CR_TAB
		      AS2 (ld,r29,X)          CR_TAB
		      AS2 (ld,r28,-X)         CR_TAB
		      AS2 (ld,__tmp_reg__,-X) CR_TAB
		      AS2 (sbiw,r26,1)        CR_TAB
		      AS2 (ld,r26,X)          CR_TAB
		      AS2 (mov,r27,__tmp_reg__));
	    }
	  *l = 6;
	  if (reg_dest == REG_X - 2)
	    return (AS2 (adiw,r26,%o1)      CR_TAB
		    AS2 (ld,r24,X+)         CR_TAB
		    AS2 (ld,r25,X+)         CR_TAB
		    AS2 (ld,__tmp_reg__,X+) CR_TAB
		    AS2 (ld,r27,X)          CR_TAB
		    AS2 (mov,r26,__tmp_reg__));

	  return (AS2 (adiw,r26,%o1) CR_TAB
		  AS2 (ld,%A0,X+)    CR_TAB
		  AS2 (ld,%B0,X+)    CR_TAB
		  AS2 (ld,%C0,X+)    CR_TAB
		  AS2 (ld,%D0,X)     CR_TAB
		  AS2 (sbiw,r26,%o1+3));
	}
      if (reg_dest == reg_base)
        return *l=5, (AS2 (ldd,%D0,%D1) CR_TAB
                      AS2 (ldd,%C0,%C1) CR_TAB
                      AS2 (ldd,__tmp_reg__,%B1)  CR_TAB
                      AS2 (ldd,%A0,%A1) CR_TAB
                      AS2 (mov,%B0,__tmp_reg__));
      else if (reg_dest == reg_base - 2)
        return *l=5, (AS2 (ldd,%A0,%A1) CR_TAB
                      AS2 (ldd,%B0,%B1) CR_TAB
                      AS2 (ldd,__tmp_reg__,%C1)  CR_TAB
                      AS2 (ldd,%D0,%D1) CR_TAB
                      AS2 (mov,%C0,__tmp_reg__));
      return *l=4, (AS2 (ldd,%A0,%A1) CR_TAB
                    AS2 (ldd,%B0,%B1) CR_TAB
                    AS2 (ldd,%C0,%C1) CR_TAB
                    AS2 (ldd,%D0,%D1));
    }
  else if (GET_CODE (base) == PRE_DEC) /* (--R) */
    return *l=4, (AS2 (ld,%D0,%1) CR_TAB
		  AS2 (ld,%C0,%1) CR_TAB
		  AS2 (ld,%B0,%1) CR_TAB
		  AS2 (ld,%A0,%1));
  else if (GET_CODE (base) == POST_INC) /* (R++) */
    return *l=4, (AS2 (ld,%A0,%1) CR_TAB
		  AS2 (ld,%B0,%1) CR_TAB
		  AS2 (ld,%C0,%1) CR_TAB
		  AS2 (ld,%D0,%1));
  else if (CONSTANT_ADDRESS_P (base))
      return *l=8, (AS2 (lds,%A0,%A1) CR_TAB
		    AS2 (lds,%B0,%B1) CR_TAB
		    AS2 (lds,%C0,%C1) CR_TAB
		    AS2 (lds,%D0,%D1));
    
  fatal_insn ("unknown move insn:",insn);
  return "";
}

const char *
out_movsi_mr_r (rtx insn, rtx op[], int *l)
{
  rtx dest = op[0];
  rtx src = op[1];
  rtx base = XEXP (dest, 0);
  int reg_base = true_regnum (base);
  int reg_src = true_regnum (src);
  int tmp;
  
  if (!l)
    l = &tmp;
  
  if (CONSTANT_ADDRESS_P (base))
    return *l=8,(AS2 (sts,%A0,%A1) CR_TAB
		 AS2 (sts,%B0,%B1) CR_TAB
		 AS2 (sts,%C0,%C1) CR_TAB
		 AS2 (sts,%D0,%D1));
  if (reg_base > 0)                 /* (r) */
    {
      if (reg_base == REG_X)                /* (R26) */
        {
          if (reg_src == REG_X)
            {
	      /* "st X+,r26" is undefined */
              if (reg_unused_after (insn, base))
		return *l=6, (AS2 (mov,__tmp_reg__,r27) CR_TAB
			      AS2 (st,X,r26)            CR_TAB
			      AS2 (adiw,r26,1)          CR_TAB
			      AS2 (st,X+,__tmp_reg__)   CR_TAB
			      AS2 (st,X+,r28)           CR_TAB
			      AS2 (st,X,r29));
              else
                return *l=7, (AS2 (mov,__tmp_reg__,r27) CR_TAB
			      AS2 (st,X,r26)            CR_TAB
			      AS2 (adiw,r26,1)          CR_TAB
			      AS2 (st,X+,__tmp_reg__)   CR_TAB
			      AS2 (st,X+,r28)           CR_TAB
			      AS2 (st,X,r29)            CR_TAB
			      AS2 (sbiw,r26,3));
            }
          else if (reg_base == reg_src + 2)
            {
              if (reg_unused_after (insn, base))
                return *l=7, (AS2 (mov,__zero_reg__,%C1) CR_TAB
                              AS2 (mov,__tmp_reg__,%D1) CR_TAB
                              AS2 (st,%0+,%A1) CR_TAB
                              AS2 (st,%0+,%B1) CR_TAB
                              AS2 (st,%0+,__zero_reg__)  CR_TAB
                              AS2 (st,%0,__tmp_reg__)   CR_TAB
                              AS1 (clr,__zero_reg__));
              else
                return *l=8, (AS2 (mov,__zero_reg__,%C1) CR_TAB
                              AS2 (mov,__tmp_reg__,%D1) CR_TAB
                              AS2 (st,%0+,%A1) CR_TAB
                              AS2 (st,%0+,%B1) CR_TAB
                              AS2 (st,%0+,__zero_reg__)  CR_TAB
                              AS2 (st,%0,__tmp_reg__)   CR_TAB
                              AS1 (clr,__zero_reg__)     CR_TAB
                              AS2 (sbiw,r26,3));
            }
          return *l=5, (AS2 (st,%0+,%A1)  CR_TAB
                        AS2 (st,%0+,%B1) CR_TAB
                        AS2 (st,%0+,%C1) CR_TAB
                        AS2 (st,%0,%D1)  CR_TAB
                        AS2 (sbiw,r26,3));
        }
      else
        return *l=4, (AS2 (st,%0,%A1)    CR_TAB
		      AS2 (std,%0+1,%B1) CR_TAB
		      AS2 (std,%0+2,%C1) CR_TAB
		      AS2 (std,%0+3,%D1));
    }
  else if (GET_CODE (base) == PLUS) /* (R + i) */
    {
      int disp = INTVAL (XEXP (base, 1));
      reg_base = REGNO (XEXP (base, 0));
      if (disp > MAX_LD_OFFSET (GET_MODE (dest)))
	{
	  if (reg_base != REG_Y)
	    fatal_insn ("incorrect insn:",insn);

	  if (disp <= 63 + MAX_LD_OFFSET (GET_MODE (dest)))
	    return *l = 6, (AS2 (adiw,r28,%o0-60) CR_TAB
			    AS2 (std,Y+60,%A1)    CR_TAB
			    AS2 (std,Y+61,%B1)    CR_TAB
			    AS2 (std,Y+62,%C1)    CR_TAB
			    AS2 (std,Y+63,%D1)    CR_TAB
			    AS2 (sbiw,r28,%o0-60));

	  return *l = 8, (AS2 (subi,r28,lo8(-%o0)) CR_TAB
			  AS2 (sbci,r29,hi8(-%o0)) CR_TAB
			  AS2 (st,Y,%A1)           CR_TAB
			  AS2 (std,Y+1,%B1)        CR_TAB
			  AS2 (std,Y+2,%C1)        CR_TAB
			  AS2 (std,Y+3,%D1)        CR_TAB
			  AS2 (subi,r28,lo8(%o0))  CR_TAB
			  AS2 (sbci,r29,hi8(%o0)));
	}
      if (reg_base == REG_X)
	{
	  /* (X + d) = R */
	  if (reg_src == REG_X)
	    {
	      *l = 9;
	      return (AS2 (mov,__tmp_reg__,r26)  CR_TAB
		      AS2 (mov,__zero_reg__,r27) CR_TAB
		      AS2 (adiw,r26,%o0)         CR_TAB
		      AS2 (st,X+,__tmp_reg__)    CR_TAB
		      AS2 (st,X+,__zero_reg__)   CR_TAB
		      AS2 (st,X+,r28)            CR_TAB
		      AS2 (st,X,r29)             CR_TAB
		      AS1 (clr,__zero_reg__)     CR_TAB
		      AS2 (sbiw,r26,%o0+3));
	    }
	  else if (reg_src == REG_X - 2)
	    {
	      *l = 9;
	      return (AS2 (mov,__tmp_reg__,r26)  CR_TAB
		      AS2 (mov,__zero_reg__,r27) CR_TAB
		      AS2 (adiw,r26,%o0)         CR_TAB
		      AS2 (st,X+,r24)            CR_TAB
		      AS2 (st,X+,r25)            CR_TAB
		      AS2 (st,X+,__tmp_reg__)    CR_TAB
		      AS2 (st,X,__zero_reg__)    CR_TAB
		      AS1 (clr,__zero_reg__)     CR_TAB
		      AS2 (sbiw,r26,%o0+3));
	    }
	  *l = 6;
	  return (AS2 (adiw,r26,%o0) CR_TAB
		  AS2 (st,X+,%A1)    CR_TAB
		  AS2 (st,X+,%B1)    CR_TAB
		  AS2 (st,X+,%C1)    CR_TAB
		  AS2 (st,X,%D1)     CR_TAB
		  AS2 (sbiw,r26,%o0+3));
	}
      return *l=4, (AS2 (std,%A0,%A1)    CR_TAB
		    AS2 (std,%B0,%B1) CR_TAB
		    AS2 (std,%C0,%C1) CR_TAB
		    AS2 (std,%D0,%D1));
    }
  else if (GET_CODE (base) == PRE_DEC) /* (--R) */
    return *l=4, (AS2 (st,%0,%D1) CR_TAB
		  AS2 (st,%0,%C1) CR_TAB
		  AS2 (st,%0,%B1) CR_TAB
		  AS2 (st,%0,%A1));
  else if (GET_CODE (base) == POST_INC) /* (R++) */
    return *l=4, (AS2 (st,%0,%A1)  CR_TAB
		  AS2 (st,%0,%B1) CR_TAB
		  AS2 (st,%0,%C1) CR_TAB
		  AS2 (st,%0,%D1));
  fatal_insn ("unknown move insn:",insn);
  return "";
}

const char *
output_movsisf(rtx insn, rtx operands[], int *l)
{
  int dummy;
  rtx dest = operands[0];
  rtx src = operands[1];
  int *real_l = l;
  
  if (!l)
    l = &dummy;
  
  if (register_operand (dest, VOIDmode))
    {
      if (register_operand (src, VOIDmode)) /* mov r,r */
	{
	  if (true_regnum (dest) > true_regnum (src))
	    {
	      if (AVR_HAVE_MOVW)
		{
		  *l = 2;
		  return (AS2 (movw,%C0,%C1) CR_TAB
			  AS2 (movw,%A0,%A1));
		}
	      *l = 4;
	      return (AS2 (mov,%D0,%D1) CR_TAB
		      AS2 (mov,%C0,%C1) CR_TAB
		      AS2 (mov,%B0,%B1) CR_TAB
		      AS2 (mov,%A0,%A1));
	    }
	  else
	    {
	      if (AVR_HAVE_MOVW)
		{
		  *l = 2;
		  return (AS2 (movw,%A0,%A1) CR_TAB
			  AS2 (movw,%C0,%C1));
		}
	      *l = 4;
	      return (AS2 (mov,%A0,%A1) CR_TAB
		      AS2 (mov,%B0,%B1) CR_TAB
		      AS2 (mov,%C0,%C1) CR_TAB
		      AS2 (mov,%D0,%D1));
	    }
	}
      else if (CONSTANT_P (src))
	{
	  if (test_hard_reg_class (LD_REGS, dest)) /* ldi d,i */
	    {
	      *l = 4;
	      return (AS2 (ldi,%A0,lo8(%1))  CR_TAB
		      AS2 (ldi,%B0,hi8(%1))  CR_TAB
		      AS2 (ldi,%C0,hlo8(%1)) CR_TAB
		      AS2 (ldi,%D0,hhi8(%1)));
	    }
	  
	  if (GET_CODE (src) == CONST_INT)
	    {
	      const char *const clr_op0 =
		AVR_HAVE_MOVW ? (AS1 (clr,%A0) CR_TAB
				AS1 (clr,%B0) CR_TAB
				AS2 (movw,%C0,%A0))
			     : (AS1 (clr,%A0) CR_TAB
				AS1 (clr,%B0) CR_TAB
				AS1 (clr,%C0) CR_TAB
				AS1 (clr,%D0));

	      if (src == const0_rtx) /* mov r,L */
		{
		  *l = AVR_HAVE_MOVW ? 3 : 4;
		  return clr_op0;
		}
	      else if (src == const1_rtx)
		{
		  if (!real_l)
		    output_asm_insn (clr_op0, operands);
		  *l = AVR_HAVE_MOVW ? 4 : 5;
		  return AS1 (inc,%A0);
		}
	      else if (src == constm1_rtx)
		{
		  /* Immediate constants -1 to any register */
		  if (AVR_HAVE_MOVW)
		    {
		      *l = 4;
		      return (AS1 (clr,%A0)     CR_TAB
			      AS1 (dec,%A0)     CR_TAB
			      AS2 (mov,%B0,%A0) CR_TAB
			      AS2 (movw,%C0,%A0));
		    }
		  *l = 5;
		  return (AS1 (clr,%A0)     CR_TAB
			  AS1 (dec,%A0)     CR_TAB
			  AS2 (mov,%B0,%A0) CR_TAB
			  AS2 (mov,%C0,%A0) CR_TAB
			  AS2 (mov,%D0,%A0));
		}
	      else
		{
		  int bit_nr = exact_log2 (INTVAL (src));

		  if (bit_nr >= 0)
		    {
		      *l = AVR_HAVE_MOVW ? 5 : 6;
		      if (!real_l)
			{
			  output_asm_insn (clr_op0, operands);
			  output_asm_insn ("set", operands);
			}
		      if (!real_l)
			avr_output_bld (operands, bit_nr);

		      return "";
		    }
		}
	    }
	  
	  /* Last resort, better than loading from memory.  */
	  *l = 10;
	  return (AS2 (mov,__tmp_reg__,r31) CR_TAB
		  AS2 (ldi,r31,lo8(%1))     CR_TAB
		  AS2 (mov,%A0,r31)         CR_TAB
		  AS2 (ldi,r31,hi8(%1))     CR_TAB
		  AS2 (mov,%B0,r31)         CR_TAB
		  AS2 (ldi,r31,hlo8(%1))    CR_TAB
		  AS2 (mov,%C0,r31)         CR_TAB
		  AS2 (ldi,r31,hhi8(%1))    CR_TAB
		  AS2 (mov,%D0,r31)         CR_TAB
		  AS2 (mov,r31,__tmp_reg__));
	}
      else if (GET_CODE (src) == MEM)
	return out_movsi_r_mr (insn, operands, real_l); /* mov r,m */
    }
  else if (GET_CODE (dest) == MEM)
    {
      const char *template;

      if (src == const0_rtx)
	  operands[1] = zero_reg_rtx;

      template = out_movsi_mr_r (insn, operands, real_l);

      if (!real_l)
	output_asm_insn (template, operands);

      operands[1] = src;
      return "";
    }
  fatal_insn ("invalid insn:", insn);
  return "";
}

const char *
out_movqi_mr_r (rtx insn, rtx op[], int *l)
{
  rtx dest = op[0];
  rtx src = op[1];
  rtx x = XEXP (dest, 0);
  int dummy;

  if (!l)
    l = &dummy;
  
  if (CONSTANT_ADDRESS_P (x))
    {
      if (avr_io_address_p (x, 1))
	{
	  *l = 1;
	  return AS2 (out,%0-0x20,%1);
	}
      *l = 2;
      return AS2 (sts,%0,%1);
    }
  /* memory access by reg+disp */
  else if (GET_CODE (x) == PLUS	
      && REG_P (XEXP (x,0))
      && GET_CODE (XEXP (x,1)) == CONST_INT)
    {
      if ((INTVAL (XEXP (x,1)) - GET_MODE_SIZE (GET_MODE (dest))) >= 63)
	{
	  int disp = INTVAL (XEXP (x,1));
	  if (REGNO (XEXP (x,0)) != REG_Y)
	    fatal_insn ("incorrect insn:",insn);

	  if (disp <= 63 + MAX_LD_OFFSET (GET_MODE (dest)))
	    return *l = 3, (AS2 (adiw,r28,%o0-63) CR_TAB
			    AS2 (std,Y+63,%1)     CR_TAB
			    AS2 (sbiw,r28,%o0-63));

	  return *l = 5, (AS2 (subi,r28,lo8(-%o0)) CR_TAB
			  AS2 (sbci,r29,hi8(-%o0)) CR_TAB
			  AS2 (st,Y,%1)            CR_TAB
			  AS2 (subi,r28,lo8(%o0))  CR_TAB
			  AS2 (sbci,r29,hi8(%o0)));
	}
      else if (REGNO (XEXP (x,0)) == REG_X)
	{
	  if (reg_overlap_mentioned_p (src, XEXP (x, 0)))
	    {
	      if (reg_unused_after (insn, XEXP (x,0)))
		return *l = 3, (AS2 (mov,__tmp_reg__,%1) CR_TAB
				AS2 (adiw,r26,%o0)       CR_TAB
				AS2 (st,X,__tmp_reg__));

	      return *l = 4, (AS2 (mov,__tmp_reg__,%1) CR_TAB
			      AS2 (adiw,r26,%o0)       CR_TAB
			      AS2 (st,X,__tmp_reg__)   CR_TAB
			      AS2 (sbiw,r26,%o0));
	    }
	  else
	    {
	      if (reg_unused_after (insn, XEXP (x,0)))
		return *l = 2, (AS2 (adiw,r26,%o0) CR_TAB
				AS2 (st,X,%1));

	      return *l = 3, (AS2 (adiw,r26,%o0) CR_TAB
			      AS2 (st,X,%1)      CR_TAB
			      AS2 (sbiw,r26,%o0));
	    }
	}
      *l = 1;
      return AS2 (std,%0,%1);
    }
  *l = 1;
  return AS2 (st,%0,%1);
}

const char *
out_movhi_mr_r (rtx insn, rtx op[], int *l)
{
  rtx dest = op[0];
  rtx src = op[1];
  rtx base = XEXP (dest, 0);
  int reg_base = true_regnum (base);
  int reg_src = true_regnum (src);
  /* "volatile" forces writing high byte first, even if less efficient,
     for correct operation with 16-bit I/O registers.  */
  int mem_volatile_p = MEM_VOLATILE_P (dest);
  int tmp;

  if (!l)
    l = &tmp;
  if (CONSTANT_ADDRESS_P (base))
    {
      if (avr_io_address_p (base, 2))
	{
	  *l = 2;
	  return (AS2 (out,%B0-0x20,%B1) CR_TAB
		  AS2 (out,%A0-0x20,%A1));
	}
      return *l = 4, (AS2 (sts,%B0,%B1) CR_TAB
		      AS2 (sts,%A0,%A1));
    }
  if (reg_base > 0)
    {
      if (reg_base == REG_X)
        {
          if (reg_src == REG_X)
            {
              /* "st X+,r26" and "st -X,r26" are undefined.  */
              if (!mem_volatile_p && reg_unused_after (insn, src))
		return *l=4, (AS2 (mov,__tmp_reg__,r27) CR_TAB
			      AS2 (st,X,r26)            CR_TAB
			      AS2 (adiw,r26,1)          CR_TAB
			      AS2 (st,X,__tmp_reg__));
              else
		return *l=5, (AS2 (mov,__tmp_reg__,r27) CR_TAB
			      AS2 (adiw,r26,1)          CR_TAB
			      AS2 (st,X,__tmp_reg__)    CR_TAB
                              AS2 (sbiw,r26,1)          CR_TAB
                              AS2 (st,X,r26));
            }
          else
            {
              if (!mem_volatile_p && reg_unused_after (insn, base))
                return *l=2, (AS2 (st,X+,%A1) CR_TAB
                              AS2 (st,X,%B1));
              else
                return *l=3, (AS2 (adiw,r26,1) CR_TAB
                              AS2 (st,X,%B1)   CR_TAB
                              AS2 (st,-X,%A1));
            }
        }
      else
        return  *l=2, (AS2 (std,%0+1,%B1) CR_TAB
                       AS2 (st,%0,%A1));
    }
  else if (GET_CODE (base) == PLUS)
    {
      int disp = INTVAL (XEXP (base, 1));
      reg_base = REGNO (XEXP (base, 0));
      if (disp > MAX_LD_OFFSET (GET_MODE (dest)))
	{
	  if (reg_base != REG_Y)
	    fatal_insn ("incorrect insn:",insn);

	  if (disp <= 63 + MAX_LD_OFFSET (GET_MODE (dest)))
	    return *l = 4, (AS2 (adiw,r28,%o0-62) CR_TAB
			    AS2 (std,Y+63,%B1)    CR_TAB
			    AS2 (std,Y+62,%A1)    CR_TAB
			    AS2 (sbiw,r28,%o0-62));

	  return *l = 6, (AS2 (subi,r28,lo8(-%o0)) CR_TAB
			  AS2 (sbci,r29,hi8(-%o0)) CR_TAB
			  AS2 (std,Y+1,%B1)        CR_TAB
			  AS2 (st,Y,%A1)           CR_TAB
			  AS2 (subi,r28,lo8(%o0))  CR_TAB
			  AS2 (sbci,r29,hi8(%o0)));
	}
      if (reg_base == REG_X)
	{
	  /* (X + d) = R */
	  if (reg_src == REG_X)
            {
	      *l = 7;
	      return (AS2 (mov,__tmp_reg__,r26)  CR_TAB
		      AS2 (mov,__zero_reg__,r27) CR_TAB
                      AS2 (adiw,r26,%o0+1)       CR_TAB
		      AS2 (st,X,__zero_reg__)    CR_TAB
		      AS2 (st,-X,__tmp_reg__)    CR_TAB
		      AS1 (clr,__zero_reg__)     CR_TAB
                      AS2 (sbiw,r26,%o0));
	    }
	  *l = 4;
          return (AS2 (adiw,r26,%o0+1) CR_TAB
                  AS2 (st,X,%B1)       CR_TAB
                  AS2 (st,-X,%A1)      CR_TAB
                  AS2 (sbiw,r26,%o0));
	}
      return *l=2, (AS2 (std,%B0,%B1)    CR_TAB
                    AS2 (std,%A0,%A1));
    }
  else if (GET_CODE (base) == PRE_DEC) /* (--R) */
    return *l=2, (AS2 (st,%0,%B1) CR_TAB
		  AS2 (st,%0,%A1));
  else if (GET_CODE (base) == POST_INC) /* (R++) */
    {
      if (mem_volatile_p)
        {
          if (REGNO (XEXP (base, 0)) == REG_X)
            {
              *l = 4;
              return (AS2 (adiw,r26,1)  CR_TAB
                      AS2 (st,X,%B1)    CR_TAB
                      AS2 (st,-X,%A1)   CR_TAB
                      AS2 (adiw,r26,2));
            }
          else
            {
              *l = 3;
              return (AS2 (std,%p0+1,%B1) CR_TAB
                      AS2 (st,%p0,%A1)    CR_TAB
                      AS2 (adiw,%r0,2));
            }
        }

      *l = 2;
      return (AS2 (st,%0,%A1)  CR_TAB
            AS2 (st,%0,%B1));
    }
  fatal_insn ("unknown move insn:",insn);
  return "";
}

/* Return 1 if frame pointer for current function required.  */

int
frame_pointer_required_p (void)
{
  return (current_function_calls_alloca
	  || current_function_args_info.nregs == 0
  	  || get_frame_size () > 0);
}

/* Returns the condition of compare insn INSN, or UNKNOWN.  */

static RTX_CODE
compare_condition (rtx insn)
{
  rtx next = next_real_insn (insn);
  RTX_CODE cond = UNKNOWN;
  if (next && GET_CODE (next) == JUMP_INSN)
    {
      rtx pat = PATTERN (next);
      rtx src = SET_SRC (pat);
      rtx t = XEXP (src, 0);
      cond = GET_CODE (t);
    }
  return cond;
}

/* Returns nonzero if INSN is a tst insn that only tests the sign.  */

static int
compare_sign_p (rtx insn)
{
  RTX_CODE cond = compare_condition (insn);
  return (cond == GE || cond == LT);
}

/* Returns nonzero if the next insn is a JUMP_INSN with a condition
   that needs to be swapped (GT, GTU, LE, LEU).  */

int
compare_diff_p (rtx insn)
{
  RTX_CODE cond = compare_condition (insn);
  return (cond == GT || cond == GTU || cond == LE || cond == LEU) ? cond : 0;
}

/* Returns nonzero if INSN is a compare insn with the EQ or NE condition.  */

int
compare_eq_p (rtx insn)
{
  RTX_CODE cond = compare_condition (insn);
  return (cond == EQ || cond == NE);
}


/* Output test instruction for HImode.  */

const char *
out_tsthi (rtx insn, int *l)
{
  if (compare_sign_p (insn))
    {
      if (l) *l = 1;
      return AS1 (tst,%B0);
    }
  if (reg_unused_after (insn, SET_SRC (PATTERN (insn)))
      && compare_eq_p (insn))
    {
      /* Faster than sbiw if we can clobber the operand.  */
      if (l) *l = 1;
      return AS2 (or,%A0,%B0);
    }
  if (test_hard_reg_class (ADDW_REGS, SET_SRC (PATTERN (insn))))
    {
      if (l) *l = 1;
      return AS2 (sbiw,%0,0);
    }
  if (l) *l = 2;
  return (AS2 (cp,%A0,__zero_reg__) CR_TAB
          AS2 (cpc,%B0,__zero_reg__));
}


/* Output test instruction for SImode.  */

const char *
out_tstsi (rtx insn, int *l)
{
  if (compare_sign_p (insn))
    {
      if (l) *l = 1;
      return AS1 (tst,%D0);
    }
  if (test_hard_reg_class (ADDW_REGS, SET_SRC (PATTERN (insn))))
    {
      if (l) *l = 3;
      return (AS2 (sbiw,%A0,0) CR_TAB
              AS2 (cpc,%C0,__zero_reg__) CR_TAB
              AS2 (cpc,%D0,__zero_reg__));
    }
  if (l) *l = 4;
  return (AS2 (cp,%A0,__zero_reg__) CR_TAB
          AS2 (cpc,%B0,__zero_reg__) CR_TAB
          AS2 (cpc,%C0,__zero_reg__) CR_TAB
          AS2 (cpc,%D0,__zero_reg__));
}


/* Generate asm equivalent for various shifts.
   Shift count is a CONST_INT, MEM or REG.
   This only handles cases that are not already
   carefully hand-optimized in ?sh??i3_out.  */

void
out_shift_with_cnt (const char *template, rtx insn, rtx operands[],
		    int *len, int t_len)
{
  rtx op[10];
  char str[500];
  int second_label = 1;
  int saved_in_tmp = 0;
  int use_zero_reg = 0;

  op[0] = operands[0];
  op[1] = operands[1];
  op[2] = operands[2];
  op[3] = operands[3];
  str[0] = 0;

  if (len)
    *len = 1;

  if (GET_CODE (operands[2]) == CONST_INT)
    {
      int scratch = (GET_CODE (PATTERN (insn)) == PARALLEL);
      int count = INTVAL (operands[2]);
      int max_len = 10;  /* If larger than this, always use a loop.  */

      if (count <= 0)
	{
	  if (len)
	    *len = 0;
	  return;
	}

      if (count < 8 && !scratch)
	use_zero_reg = 1;

      if (optimize_size)
	max_len = t_len + (scratch ? 3 : (use_zero_reg ? 4 : 5));

      if (t_len * count <= max_len)
	{
	  /* Output shifts inline with no loop - faster.  */
	  if (len)
	    *len = t_len * count;
	  else
	    {
	      while (count-- > 0)
		output_asm_insn (template, op);
	    }

	  return;
	}

      if (scratch)
	{
	  if (!len)
	    strcat (str, AS2 (ldi,%3,%2));
	}
      else if (use_zero_reg)
	{
	  /* Hack to save one word: use __zero_reg__ as loop counter.
	     Set one bit, then shift in a loop until it is 0 again.  */

	  op[3] = zero_reg_rtx;
	  if (len)
	    *len = 2;
	  else
	    strcat (str, ("set" CR_TAB
			  AS2 (bld,%3,%2-1)));
	}
      else
	{
	  /* No scratch register available, use one from LD_REGS (saved in
	     __tmp_reg__) that doesn't overlap with registers to shift.  */

	  op[3] = gen_rtx_REG (QImode,
			   ((true_regnum (operands[0]) - 1) & 15) + 16);
	  op[4] = tmp_reg_rtx;
	  saved_in_tmp = 1;

	  if (len)
	    *len = 3;  /* Includes "mov %3,%4" after the loop.  */
	  else
	    strcat (str, (AS2 (mov,%4,%3) CR_TAB
			  AS2 (ldi,%3,%2)));
	}

      second_label = 0;
    }
  else if (GET_CODE (operands[2]) == MEM)
    {
      rtx op_mov[10];
      
      op[3] = op_mov[0] = tmp_reg_rtx;
      op_mov[1] = op[2];

      if (len)
	out_movqi_r_mr (insn, op_mov, len);
      else
	output_asm_insn (out_movqi_r_mr (insn, op_mov, NULL), op_mov);
    }
  else if (register_operand (operands[2], QImode))
    {
      if (reg_unused_after (insn, operands[2]))
	op[3] = op[2];
      else
	{
	  op[3] = tmp_reg_rtx;
	  if (!len)
	    strcat (str, (AS2 (mov,%3,%2) CR_TAB));
	}
    }
  else
    fatal_insn ("bad shift insn:", insn);

  if (second_label)
    {
      if (len)
	++*len;
      else
	strcat (str, AS1 (rjmp,2f));
    }

  if (len)
    *len += t_len + 2;  /* template + dec + brXX */
  else
    {
      strcat (str, "\n1:\t");
      strcat (str, template);
      strcat (str, second_label ? "\n2:\t" : "\n\t");
      strcat (str, use_zero_reg ? AS1 (lsr,%3) : AS1 (dec,%3));
      strcat (str, CR_TAB);
      strcat (str, second_label ? AS1 (brpl,1b) : AS1 (brne,1b));
      if (saved_in_tmp)
	strcat (str, (CR_TAB AS2 (mov,%3,%4)));
      output_asm_insn (str, op);
    }
}


/* 8bit shift left ((char)x << i)   */

const char *
ashlqi3_out (rtx insn, rtx operands[], int *len)
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      int k;

      if (!len)
	len = &k;

      switch (INTVAL (operands[2]))
	{
	default:
	  if (INTVAL (operands[2]) < 8)
	    break;

	  *len = 1;
	  return AS1 (clr,%0);
	  
	case 1:
	  *len = 1;
	  return AS1 (lsl,%0);
	  
	case 2:
	  *len = 2;
	  return (AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0));

	case 3:
	  *len = 3;
	  return (AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0));

	case 4:
	  if (test_hard_reg_class (LD_REGS, operands[0]))
	    {
	      *len = 2;
	      return (AS1 (swap,%0) CR_TAB
		      AS2 (andi,%0,0xf0));
	    }
	  *len = 4;
	  return (AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0));

	case 5:
	  if (test_hard_reg_class (LD_REGS, operands[0]))
	    {
	      *len = 3;
	      return (AS1 (swap,%0) CR_TAB
		      AS1 (lsl,%0)  CR_TAB
		      AS2 (andi,%0,0xe0));
	    }
	  *len = 5;
	  return (AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0));

	case 6:
	  if (test_hard_reg_class (LD_REGS, operands[0]))
	    {
	      *len = 4;
	      return (AS1 (swap,%0) CR_TAB
		      AS1 (lsl,%0)  CR_TAB
		      AS1 (lsl,%0)  CR_TAB
		      AS2 (andi,%0,0xc0));
	    }
	  *len = 6;
	  return (AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0) CR_TAB
		  AS1 (lsl,%0));

	case 7:
	  *len = 3;
	  return (AS1 (ror,%0) CR_TAB
		  AS1 (clr,%0) CR_TAB
		  AS1 (ror,%0));
	}
    }
  else if (CONSTANT_P (operands[2]))
    fatal_insn ("internal compiler error.  Incorrect shift:", insn);

  out_shift_with_cnt (AS1 (lsl,%0),
		      insn, operands, len, 1);
  return "";
}


/* 16bit shift left ((short)x << i)   */

const char *
ashlhi3_out (rtx insn, rtx operands[], int *len)
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      int scratch = (GET_CODE (PATTERN (insn)) == PARALLEL);
      int ldi_ok = test_hard_reg_class (LD_REGS, operands[0]);
      int k;
      int *t = len;

      if (!len)
	len = &k;
      
      switch (INTVAL (operands[2]))
	{
	default:
	  if (INTVAL (operands[2]) < 16)
	    break;

	  *len = 2;
	  return (AS1 (clr,%B0) CR_TAB
		  AS1 (clr,%A0));

	case 4:
	  if (optimize_size && scratch)
	    break;  /* 5 */
	  if (ldi_ok)
	    {
	      *len = 6;
	      return (AS1 (swap,%A0)      CR_TAB
		      AS1 (swap,%B0)      CR_TAB
		      AS2 (andi,%B0,0xf0) CR_TAB
		      AS2 (eor,%B0,%A0)   CR_TAB
		      AS2 (andi,%A0,0xf0) CR_TAB
		      AS2 (eor,%B0,%A0));
	    }
	  if (scratch)
	    {
	      *len = 7;
	      return (AS1 (swap,%A0)    CR_TAB
		      AS1 (swap,%B0)    CR_TAB
		      AS2 (ldi,%3,0xf0) CR_TAB
		      AS2 (and,%B0,%3)  CR_TAB
		      AS2 (eor,%B0,%A0) CR_TAB
		      AS2 (and,%A0,%3)  CR_TAB
		      AS2 (eor,%B0,%A0));
	    }
	  break;  /* optimize_size ? 6 : 8 */

	case 5:
	  if (optimize_size)
	    break;  /* scratch ? 5 : 6 */
	  if (ldi_ok)
	    {
	      *len = 8;
	      return (AS1 (lsl,%A0)       CR_TAB
		      AS1 (rol,%B0)       CR_TAB
		      AS1 (swap,%A0)      CR_TAB
		      AS1 (swap,%B0)      CR_TAB
		      AS2 (andi,%B0,0xf0) CR_TAB
		      AS2 (eor,%B0,%A0)   CR_TAB
		      AS2 (andi,%A0,0xf0) CR_TAB
		      AS2 (eor,%B0,%A0));
	    }
	  if (scratch)
	    {
	      *len = 9;
	      return (AS1 (lsl,%A0)     CR_TAB
		      AS1 (rol,%B0)     CR_TAB
		      AS1 (swap,%A0)    CR_TAB
		      AS1 (swap,%B0)    CR_TAB
		      AS2 (ldi,%3,0xf0) CR_TAB
		      AS2 (and,%B0,%3)  CR_TAB
		      AS2 (eor,%B0,%A0) CR_TAB
		      AS2 (and,%A0,%3)  CR_TAB
		      AS2 (eor,%B0,%A0));
	    }
	  break;  /* 10 */

	case 6:
	  if (optimize_size)
	    break;  /* scratch ? 5 : 6 */
	  *len = 9;
	  return (AS1 (clr,__tmp_reg__) CR_TAB
		  AS1 (lsr,%B0)         CR_TAB
		  AS1 (ror,%A0)         CR_TAB
		  AS1 (ror,__tmp_reg__) CR_TAB
		  AS1 (lsr,%B0)         CR_TAB
		  AS1 (ror,%A0)         CR_TAB
		  AS1 (ror,__tmp_reg__) CR_TAB
		  AS2 (mov,%B0,%A0)     CR_TAB
		  AS2 (mov,%A0,__tmp_reg__));

	case 7:
	  *len = 5;
	  return (AS1 (lsr,%B0)     CR_TAB
		  AS2 (mov,%B0,%A0) CR_TAB
		  AS1 (clr,%A0)     CR_TAB
		  AS1 (ror,%B0)     CR_TAB
		  AS1 (ror,%A0));

	case 8:
	  if (true_regnum (operands[0]) + 1 == true_regnum (operands[1]))
	    return *len = 1, AS1 (clr,%A0);
	  else
	    return *len = 2, (AS2 (mov,%B0,%A1) CR_TAB
			      AS1 (clr,%A0));

	case 9:
	  *len = 3;
	  return (AS2 (mov,%B0,%A0) CR_TAB
		  AS1 (clr,%A0)     CR_TAB
		  AS1 (lsl,%B0));

	case 10:
	  *len = 4;
	  return (AS2 (mov,%B0,%A0) CR_TAB
		  AS1 (clr,%A0)     CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS1 (lsl,%B0));

	case 11:
	  *len = 5;
	  return (AS2 (mov,%B0,%A0) CR_TAB
		  AS1 (clr,%A0)     CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS1 (lsl,%B0));

	case 12:
	  if (ldi_ok)
	    {
	      *len = 4;
	      return (AS2 (mov,%B0,%A0) CR_TAB
		      AS1 (clr,%A0)     CR_TAB
		      AS1 (swap,%B0)    CR_TAB
		      AS2 (andi,%B0,0xf0));
	    }
	  if (scratch)
	    {
	      *len = 5;
	      return (AS2 (mov,%B0,%A0) CR_TAB
		      AS1 (clr,%A0)     CR_TAB
		      AS1 (swap,%B0)    CR_TAB
		      AS2 (ldi,%3,0xf0) CR_TAB
		      AS2 (and,%B0,%3));
	    }
	  *len = 6;
	  return (AS2 (mov,%B0,%A0) CR_TAB
		  AS1 (clr,%A0)     CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS1 (lsl,%B0));

	case 13:
	  if (ldi_ok)
	    {
	      *len = 5;
	      return (AS2 (mov,%B0,%A0) CR_TAB
		      AS1 (clr,%A0)     CR_TAB
		      AS1 (swap,%B0)    CR_TAB
		      AS1 (lsl,%B0)     CR_TAB
		      AS2 (andi,%B0,0xe0));
	    }
	  if (AVR_ENHANCED && scratch)
	    {
	      *len = 5;
	      return (AS2 (ldi,%3,0x20) CR_TAB
		      AS2 (mul,%A0,%3)  CR_TAB
		      AS2 (mov,%B0,r0)  CR_TAB
		      AS1 (clr,%A0)     CR_TAB
		      AS1 (clr,__zero_reg__));
	    }
	  if (optimize_size && scratch)
	    break;  /* 5 */
	  if (scratch)
	    {
	      *len = 6;
	      return (AS2 (mov,%B0,%A0) CR_TAB
		      AS1 (clr,%A0)     CR_TAB
		      AS1 (swap,%B0)    CR_TAB
		      AS1 (lsl,%B0)     CR_TAB
		      AS2 (ldi,%3,0xe0) CR_TAB
		      AS2 (and,%B0,%3));
	    }
	  if (AVR_ENHANCED)
	    {
	      *len = 6;
	      return ("set"            CR_TAB
		      AS2 (bld,r1,5)   CR_TAB
		      AS2 (mul,%A0,r1) CR_TAB
		      AS2 (mov,%B0,r0) CR_TAB
		      AS1 (clr,%A0)    CR_TAB
		      AS1 (clr,__zero_reg__));
	    }
	  *len = 7;
	  return (AS2 (mov,%B0,%A0) CR_TAB
		  AS1 (clr,%A0)     CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS1 (lsl,%B0));

	case 14:
	  if (AVR_ENHANCED && ldi_ok)
	    {
	      *len = 5;
	      return (AS2 (ldi,%B0,0x40) CR_TAB
		      AS2 (mul,%A0,%B0)  CR_TAB
		      AS2 (mov,%B0,r0)   CR_TAB
		      AS1 (clr,%A0)      CR_TAB
		      AS1 (clr,__zero_reg__));
	    }
	  if (AVR_ENHANCED && scratch)
	    {
	      *len = 5;
	      return (AS2 (ldi,%3,0x40) CR_TAB
		      AS2 (mul,%A0,%3)  CR_TAB
		      AS2 (mov,%B0,r0)  CR_TAB
		      AS1 (clr,%A0)     CR_TAB
		      AS1 (clr,__zero_reg__));
	    }
	  if (optimize_size && ldi_ok)
	    {
	      *len = 5;
	      return (AS2 (mov,%B0,%A0) CR_TAB
		      AS2 (ldi,%A0,6) "\n1:\t"
		      AS1 (lsl,%B0)     CR_TAB
		      AS1 (dec,%A0)     CR_TAB
		      AS1 (brne,1b));
	    }
	  if (optimize_size && scratch)
	    break;  /* 5 */
	  *len = 6;
	  return (AS1 (clr,%B0) CR_TAB
		  AS1 (lsr,%A0) CR_TAB
		  AS1 (ror,%B0) CR_TAB
		  AS1 (lsr,%A0) CR_TAB
		  AS1 (ror,%B0) CR_TAB
		  AS1 (clr,%A0));

	case 15:
	  *len = 4;
	  return (AS1 (clr,%B0) CR_TAB
		  AS1 (lsr,%A0) CR_TAB
		  AS1 (ror,%B0) CR_TAB
		  AS1 (clr,%A0));
	}
      len = t;
    }
  out_shift_with_cnt ((AS1 (lsl,%A0) CR_TAB
		       AS1 (rol,%B0)),
		       insn, operands, len, 2);
  return "";
}


/* 32bit shift left ((long)x << i)   */

const char *
ashlsi3_out (rtx insn, rtx operands[], int *len)
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      int k;
      int *t = len;
      
      if (!len)
	len = &k;
      
      switch (INTVAL (operands[2]))
	{
	default:
	  if (INTVAL (operands[2]) < 32)
	    break;

	  if (AVR_HAVE_MOVW)
	    return *len = 3, (AS1 (clr,%D0) CR_TAB
			      AS1 (clr,%C0) CR_TAB
			      AS2 (movw,%A0,%C0));
	  *len = 4;
	  return (AS1 (clr,%D0) CR_TAB
		  AS1 (clr,%C0) CR_TAB
		  AS1 (clr,%B0) CR_TAB
		  AS1 (clr,%A0));

	case 8:
	  {
	    int reg0 = true_regnum (operands[0]);
	    int reg1 = true_regnum (operands[1]);
	    *len = 4;
	    if (reg0 >= reg1)
	      return (AS2 (mov,%D0,%C1)  CR_TAB
		      AS2 (mov,%C0,%B1)  CR_TAB
		      AS2 (mov,%B0,%A1)  CR_TAB
		      AS1 (clr,%A0));
	    else if (reg0 + 1 == reg1)
	      {
		*len = 1;
		return AS1 (clr,%A0);
	      }
	    else
	      return (AS1 (clr,%A0)      CR_TAB
		      AS2 (mov,%B0,%A1)  CR_TAB
		      AS2 (mov,%C0,%B1)  CR_TAB
		      AS2 (mov,%D0,%C1));
	  }

	case 16:
	  {
	    int reg0 = true_regnum (operands[0]);
	    int reg1 = true_regnum (operands[1]);
	    *len = 4;
	    if (AVR_HAVE_MOVW && (reg0 + 2 != reg1))
	      {
		*len = 3;
		return (AS2 (movw,%C0,%A1) CR_TAB
			AS1 (clr,%B0)      CR_TAB
			AS1 (clr,%A0));
	      }
	    if (reg0 + 1 >= reg1)
	      return (AS2 (mov,%D0,%B1)  CR_TAB
		      AS2 (mov,%C0,%A1)  CR_TAB
		      AS1 (clr,%B0)      CR_TAB
		      AS1 (clr,%A0));
	    if (reg0 + 2 == reg1)
	      {
		*len = 2;
		return (AS1 (clr,%B0)      CR_TAB
			AS1 (clr,%A0));
	      }
	    else
	      return (AS2 (mov,%C0,%A1)  CR_TAB
		      AS2 (mov,%D0,%B1)  CR_TAB
		      AS1 (clr,%B0)      CR_TAB
		      AS1 (clr,%A0));
	  }

	case 24:
	  *len = 4;
	  if (true_regnum (operands[0]) + 3 != true_regnum (operands[1]))
	    return (AS2 (mov,%D0,%A1)  CR_TAB
		    AS1 (clr,%C0)      CR_TAB
		    AS1 (clr,%B0)      CR_TAB
		    AS1 (clr,%A0));
	  else
	    {
	      *len = 3;
	      return (AS1 (clr,%C0)      CR_TAB
		      AS1 (clr,%B0)      CR_TAB
		      AS1 (clr,%A0));
	    }

	case 31:
	  *len = 6;
	  return (AS1 (clr,%D0) CR_TAB
		  AS1 (lsr,%A0) CR_TAB
		  AS1 (ror,%D0) CR_TAB
		  AS1 (clr,%C0) CR_TAB
		  AS1 (clr,%B0) CR_TAB
		  AS1 (clr,%A0));
	}
      len = t;
    }
  out_shift_with_cnt ((AS1 (lsl,%A0) CR_TAB
		       AS1 (rol,%B0) CR_TAB
		       AS1 (rol,%C0) CR_TAB
		       AS1 (rol,%D0)),
		       insn, operands, len, 4);
  return "";
}

/* 8bit arithmetic shift right  ((signed char)x >> i) */

const char *
ashrqi3_out (rtx insn, rtx operands[], int *len)
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      int k;

      if (!len)
	len = &k;

      switch (INTVAL (operands[2]))
	{
	case 1:
	  *len = 1;
	  return AS1 (asr,%0);

	case 2:
	  *len = 2;
	  return (AS1 (asr,%0) CR_TAB
		  AS1 (asr,%0));

	case 3:
	  *len = 3;
	  return (AS1 (asr,%0) CR_TAB
		  AS1 (asr,%0) CR_TAB
		  AS1 (asr,%0));

	case 4:
	  *len = 4;
	  return (AS1 (asr,%0) CR_TAB
		  AS1 (asr,%0) CR_TAB
		  AS1 (asr,%0) CR_TAB
		  AS1 (asr,%0));

	case 5:
	  *len = 5;
	  return (AS1 (asr,%0) CR_TAB
		  AS1 (asr,%0) CR_TAB
		  AS1 (asr,%0) CR_TAB
		  AS1 (asr,%0) CR_TAB
		  AS1 (asr,%0));

	case 6:
	  *len = 4;
	  return (AS2 (bst,%0,6)  CR_TAB
		  AS1 (lsl,%0)    CR_TAB
		  AS2 (sbc,%0,%0) CR_TAB
		  AS2 (bld,%0,0));

	default:
	  if (INTVAL (operands[2]) < 8)
	    break;

	  /* fall through */

	case 7:
	  *len = 2;
	  return (AS1 (lsl,%0) CR_TAB
		  AS2 (sbc,%0,%0));
	}
    }
  else if (CONSTANT_P (operands[2]))
    fatal_insn ("internal compiler error.  Incorrect shift:", insn);

  out_shift_with_cnt (AS1 (asr,%0),
		      insn, operands, len, 1);
  return "";
}


/* 16bit arithmetic shift right  ((signed short)x >> i) */

const char *
ashrhi3_out (rtx insn, rtx operands[], int *len)
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      int scratch = (GET_CODE (PATTERN (insn)) == PARALLEL);
      int ldi_ok = test_hard_reg_class (LD_REGS, operands[0]);
      int k;
      int *t = len;
      
      if (!len)
	len = &k;

      switch (INTVAL (operands[2]))
	{
	case 4:
	case 5:
	  /* XXX try to optimize this too? */
	  break;

	case 6:
	  if (optimize_size)
	    break;  /* scratch ? 5 : 6 */
	  *len = 8;
	  return (AS2 (mov,__tmp_reg__,%A0) CR_TAB
		  AS2 (mov,%A0,%B0)         CR_TAB
		  AS1 (lsl,__tmp_reg__)     CR_TAB
		  AS1 (rol,%A0)             CR_TAB
		  AS2 (sbc,%B0,%B0)         CR_TAB
		  AS1 (lsl,__tmp_reg__)     CR_TAB
		  AS1 (rol,%A0)             CR_TAB
		  AS1 (rol,%B0));

	case 7:
	  *len = 4;
	  return (AS1 (lsl,%A0)     CR_TAB
		  AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (rol,%A0)     CR_TAB
		  AS2 (sbc,%B0,%B0));

	case 8:
	  {
	    int reg0 = true_regnum (operands[0]);
	    int reg1 = true_regnum (operands[1]);

	    if (reg0 == reg1)
	      return *len = 3, (AS2 (mov,%A0,%B0) CR_TAB
				AS1 (lsl,%B0)     CR_TAB
				AS2 (sbc,%B0,%B0));
	    else if (reg0 == reg1 + 1)
	      return *len = 3, (AS1 (clr,%B0)    CR_TAB
				AS2 (sbrc,%A0,7) CR_TAB
				AS1 (dec,%B0));

	    return *len = 4, (AS2 (mov,%A0,%B1) CR_TAB
			      AS1 (clr,%B0)     CR_TAB
			      AS2 (sbrc,%A0,7)  CR_TAB
			      AS1 (dec,%B0));
	  }

	case 9:
	  *len = 4;
	  return (AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (lsl,%B0)      CR_TAB
		  AS2 (sbc,%B0,%B0) CR_TAB
		  AS1 (asr,%A0));

	case 10:
	  *len = 5;
	  return (AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS2 (sbc,%B0,%B0) CR_TAB
		  AS1 (asr,%A0)     CR_TAB
		  AS1 (asr,%A0));

	case 11:
	  if (AVR_ENHANCED && ldi_ok)
	    {
	      *len = 5;
	      return (AS2 (ldi,%A0,0x20) CR_TAB
		      AS2 (muls,%B0,%A0) CR_TAB
		      AS2 (mov,%A0,r1)   CR_TAB
		      AS2 (sbc,%B0,%B0)  CR_TAB
		      AS1 (clr,__zero_reg__));
	    }
	  if (optimize_size && scratch)
	    break;  /* 5 */
	  *len = 6;
	  return (AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS2 (sbc,%B0,%B0) CR_TAB
		  AS1 (asr,%A0)     CR_TAB
		  AS1 (asr,%A0)     CR_TAB
		  AS1 (asr,%A0));

	case 12:
	  if (AVR_ENHANCED && ldi_ok)
	    {
	      *len = 5;
	      return (AS2 (ldi,%A0,0x10) CR_TAB
		      AS2 (muls,%B0,%A0) CR_TAB
		      AS2 (mov,%A0,r1)   CR_TAB
		      AS2 (sbc,%B0,%B0)  CR_TAB
		      AS1 (clr,__zero_reg__));
	    }
	  if (optimize_size && scratch)
	    break;  /* 5 */
	  *len = 7;
	  return (AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS2 (sbc,%B0,%B0) CR_TAB
		  AS1 (asr,%A0)     CR_TAB
		  AS1 (asr,%A0)     CR_TAB
		  AS1 (asr,%A0)     CR_TAB
		  AS1 (asr,%A0));

	case 13:
	  if (AVR_ENHANCED && ldi_ok)
	    {
	      *len = 5;
	      return (AS2 (ldi,%A0,0x08) CR_TAB
		      AS2 (muls,%B0,%A0) CR_TAB
		      AS2 (mov,%A0,r1)   CR_TAB
		      AS2 (sbc,%B0,%B0)  CR_TAB
		      AS1 (clr,__zero_reg__));
	    }
	  if (optimize_size)
	    break;  /* scratch ? 5 : 7 */
	  *len = 8;
	  return (AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS2 (sbc,%B0,%B0) CR_TAB
		  AS1 (asr,%A0)     CR_TAB
		  AS1 (asr,%A0)     CR_TAB
		  AS1 (asr,%A0)     CR_TAB
		  AS1 (asr,%A0)     CR_TAB
		  AS1 (asr,%A0));

	case 14:
	  *len = 5;
	  return (AS1 (lsl,%B0)     CR_TAB
		  AS2 (sbc,%A0,%A0) CR_TAB
		  AS1 (lsl,%B0)     CR_TAB
		  AS2 (mov,%B0,%A0) CR_TAB
		  AS1 (rol,%A0));

	default:
	  if (INTVAL (operands[2]) < 16)
	    break;

	  /* fall through */

	case 15:
	  return *len = 3, (AS1 (lsl,%B0)     CR_TAB
			    AS2 (sbc,%A0,%A0) CR_TAB
			    AS2 (mov,%B0,%A0));
	}
      len = t;
    }
  out_shift_with_cnt ((AS1 (asr,%B0) CR_TAB
		       AS1 (ror,%A0)),
		       insn, operands, len, 2);
  return "";
}


/* 32bit arithmetic shift right  ((signed long)x >> i) */

const char *
ashrsi3_out (rtx insn, rtx operands[], int *len)
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      int k;
      int *t = len;
      
      if (!len)
	len = &k;
      
      switch (INTVAL (operands[2]))
	{
	case 8:
	  {
	    int reg0 = true_regnum (operands[0]);
	    int reg1 = true_regnum (operands[1]);
	    *len=6;
	    if (reg0 <= reg1)
	      return (AS2 (mov,%A0,%B1) CR_TAB
		      AS2 (mov,%B0,%C1) CR_TAB
		      AS2 (mov,%C0,%D1) CR_TAB
		      AS1 (clr,%D0)     CR_TAB
		      AS2 (sbrc,%C0,7)  CR_TAB
		      AS1 (dec,%D0));
	    else if (reg0 == reg1 + 1)
	      {
		*len = 3;
		return (AS1 (clr,%D0)     CR_TAB
			AS2 (sbrc,%C0,7)  CR_TAB
			AS1 (dec,%D0));
	      }
	    else
	      return (AS1 (clr,%D0)     CR_TAB
		      AS2 (sbrc,%D1,7)  CR_TAB
		      AS1 (dec,%D0)     CR_TAB
		      AS2 (mov,%C0,%D1) CR_TAB
		      AS2 (mov,%B0,%C1) CR_TAB
		      AS2 (mov,%A0,%B1));
	  }
	  
	case 16:
	  {
	    int reg0 = true_regnum (operands[0]);
	    int reg1 = true_regnum (operands[1]);
	    *len=6;
	    if (AVR_HAVE_MOVW && (reg0 != reg1 + 2))
	      {
		*len = 5;
		return (AS2 (movw,%A0,%C1) CR_TAB
			AS1 (clr,%D0)      CR_TAB
			AS2 (sbrc,%B0,7)   CR_TAB
			AS1 (com,%D0)      CR_TAB
			AS2 (mov,%C0,%D0));
	      }
	    if (reg0 <= reg1 + 1)
	      return (AS2 (mov,%A0,%C1) CR_TAB
		      AS2 (mov,%B0,%D1) CR_TAB
		      AS1 (clr,%D0)     CR_TAB
		      AS2 (sbrc,%B0,7)  CR_TAB
		      AS1 (com,%D0)     CR_TAB
		      AS2 (mov,%C0,%D0));
	    else if (reg0 == reg1 + 2)
	      return *len = 4, (AS1 (clr,%D0)     CR_TAB
				AS2 (sbrc,%B0,7)  CR_TAB
				AS1 (com,%D0)     CR_TAB
				AS2 (mov,%C0,%D0));
	    else
	      return (AS2 (mov,%B0,%D1) CR_TAB
		      AS2 (mov,%A0,%C1) CR_TAB
		      AS1 (clr,%D0)     CR_TAB
		      AS2 (sbrc,%B0,7)  CR_TAB
		      AS1 (com,%D0)     CR_TAB
		      AS2 (mov,%C0,%D0));
	  }

	case 24:
	  if (true_regnum (operands[0]) != true_regnum (operands[1]) + 3)
	    return *len = 6, (AS2 (mov,%A0,%D1) CR_TAB
			      AS1 (clr,%D0)     CR_TAB
			      AS2 (sbrc,%A0,7)  CR_TAB
			      AS1 (com,%D0)     CR_TAB
			      AS2 (mov,%B0,%D0) CR_TAB
			      AS2 (mov,%C0,%D0));
	  else
	    return *len = 5, (AS1 (clr,%D0)     CR_TAB
			      AS2 (sbrc,%A0,7)  CR_TAB
			      AS1 (com,%D0)     CR_TAB
			      AS2 (mov,%B0,%D0) CR_TAB
			      AS2 (mov,%C0,%D0));

	default:
	  if (INTVAL (operands[2]) < 32)
	    break;

	  /* fall through */

	case 31:
	  if (AVR_HAVE_MOVW)
	    return *len = 4, (AS1 (lsl,%D0)     CR_TAB
			      AS2 (sbc,%A0,%A0) CR_TAB
			      AS2 (mov,%B0,%A0) CR_TAB
			      AS2 (movw,%C0,%A0));
	  else
	    return *len = 5, (AS1 (lsl,%D0)     CR_TAB
			      AS2 (sbc,%A0,%A0) CR_TAB
			      AS2 (mov,%B0,%A0) CR_TAB
			      AS2 (mov,%C0,%A0) CR_TAB
			      AS2 (mov,%D0,%A0));
	}
      len = t;
    }
  out_shift_with_cnt ((AS1 (asr,%D0) CR_TAB
		       AS1 (ror,%C0) CR_TAB
		       AS1 (ror,%B0) CR_TAB
		       AS1 (ror,%A0)),
		       insn, operands, len, 4);
  return "";
}

/* 8bit logic shift right ((unsigned char)x >> i) */

const char *
lshrqi3_out (rtx insn, rtx operands[], int *len)
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      int k;

      if (!len)
	len = &k;
      
      switch (INTVAL (operands[2]))
	{
	default:
	  if (INTVAL (operands[2]) < 8)
	    break;

	  *len = 1;
	  return AS1 (clr,%0);

	case 1:
	  *len = 1;
	  return AS1 (lsr,%0);

	case 2:
	  *len = 2;
	  return (AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0));
	case 3:
	  *len = 3;
	  return (AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0));
	  
	case 4:
	  if (test_hard_reg_class (LD_REGS, operands[0]))
	    {
	      *len=2;
	      return (AS1 (swap,%0) CR_TAB
		      AS2 (andi,%0,0x0f));
	    }
	  *len = 4;
	  return (AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0));
	  
	case 5:
	  if (test_hard_reg_class (LD_REGS, operands[0]))
	    {
	      *len = 3;
	      return (AS1 (swap,%0) CR_TAB
		      AS1 (lsr,%0)  CR_TAB
		      AS2 (andi,%0,0x7));
	    }
	  *len = 5;
	  return (AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0));
	  
	case 6:
	  if (test_hard_reg_class (LD_REGS, operands[0]))
	    {
	      *len = 4;
	      return (AS1 (swap,%0) CR_TAB
		      AS1 (lsr,%0)  CR_TAB
		      AS1 (lsr,%0)  CR_TAB
		      AS2 (andi,%0,0x3));
	    }
	  *len = 6;
	  return (AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0) CR_TAB
		  AS1 (lsr,%0));
	  
	case 7:
	  *len = 3;
	  return (AS1 (rol,%0) CR_TAB
		  AS1 (clr,%0) CR_TAB
		  AS1 (rol,%0));
	}
    }
  else if (CONSTANT_P (operands[2]))
    fatal_insn ("internal compiler error.  Incorrect shift:", insn);
  
  out_shift_with_cnt (AS1 (lsr,%0),
		      insn, operands, len, 1);
  return "";
}

/* 16bit logic shift right ((unsigned short)x >> i) */

const char *
lshrhi3_out (rtx insn, rtx operands[], int *len)
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      int scratch = (GET_CODE (PATTERN (insn)) == PARALLEL);
      int ldi_ok = test_hard_reg_class (LD_REGS, operands[0]);
      int k;
      int *t = len;

      if (!len)
	len = &k;
      
      switch (INTVAL (operands[2]))
	{
	default:
	  if (INTVAL (operands[2]) < 16)
	    break;

	  *len = 2;
	  return (AS1 (clr,%B0) CR_TAB
		  AS1 (clr,%A0));

	case 4:
	  if (optimize_size && scratch)
	    break;  /* 5 */
	  if (ldi_ok)
	    {
	      *len = 6;
	      return (AS1 (swap,%B0)      CR_TAB
		      AS1 (swap,%A0)      CR_TAB
		      AS2 (andi,%A0,0x0f) CR_TAB
		      AS2 (eor,%A0,%B0)   CR_TAB
		      AS2 (andi,%B0,0x0f) CR_TAB
		      AS2 (eor,%A0,%B0));
	    }
	  if (scratch)
	    {
	      *len = 7;
	      return (AS1 (swap,%B0)    CR_TAB
		      AS1 (swap,%A0)    CR_TAB
		      AS2 (ldi,%3,0x0f) CR_TAB
		      AS2 (and,%A0,%3)  CR_TAB
		      AS2 (eor,%A0,%B0) CR_TAB
		      AS2 (and,%B0,%3)  CR_TAB
		      AS2 (eor,%A0,%B0));
	    }
	  break;  /* optimize_size ? 6 : 8 */

	case 5:
	  if (optimize_size)
	    break;  /* scratch ? 5 : 6 */
	  if (ldi_ok)
	    {
	      *len = 8;
	      return (AS1 (lsr,%B0)       CR_TAB
		      AS1 (ror,%A0)       CR_TAB
		      AS1 (swap,%B0)      CR_TAB
		      AS1 (swap,%A0)      CR_TAB
		      AS2 (andi,%A0,0x0f) CR_TAB
		      AS2 (eor,%A0,%B0)   CR_TAB
		      AS2 (andi,%B0,0x0f) CR_TAB
		      AS2 (eor,%A0,%B0));
	    }
	  if (scratch)
	    {
	      *len = 9;
	      return (AS1 (lsr,%B0)     CR_TAB
		      AS1 (ror,%A0)     CR_TAB
		      AS1 (swap,%B0)    CR_TAB
		      AS1 (swap,%A0)    CR_TAB
		      AS2 (ldi,%3,0x0f) CR_TAB
		      AS2 (and,%A0,%3)  CR_TAB
		      AS2 (eor,%A0,%B0) CR_TAB
		      AS2 (and,%B0,%3)  CR_TAB
		      AS2 (eor,%A0,%B0));
	    }
	  break;  /* 10 */

	case 6:
	  if (optimize_size)
	    break;  /* scratch ? 5 : 6 */
	  *len = 9;
	  return (AS1 (clr,__tmp_reg__) CR_TAB
		  AS1 (lsl,%A0)         CR_TAB
		  AS1 (rol,%B0)         CR_TAB
		  AS1 (rol,__tmp_reg__) CR_TAB
		  AS1 (lsl,%A0)         CR_TAB
		  AS1 (rol,%B0)         CR_TAB
		  AS1 (rol,__tmp_reg__) CR_TAB
		  AS2 (mov,%A0,%B0)     CR_TAB
		  AS2 (mov,%B0,__tmp_reg__));

	case 7:
	  *len = 5;
	  return (AS1 (lsl,%A0)     CR_TAB
		  AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (rol,%A0)     CR_TAB
		  AS2 (sbc,%B0,%B0) CR_TAB
		  AS1 (neg,%B0));

	case 8:
	  if (true_regnum (operands[0]) != true_regnum (operands[1]) + 1)
	    return *len = 2, (AS2 (mov,%A0,%B1) CR_TAB
			      AS1 (clr,%B0));
	  else
	    return *len = 1, AS1 (clr,%B0);

	case 9:
	  *len = 3;
	  return (AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (clr,%B0)     CR_TAB
		  AS1 (lsr,%A0));

	case 10:
	  *len = 4;
	  return (AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (clr,%B0)     CR_TAB
		  AS1 (lsr,%A0)     CR_TAB
		  AS1 (lsr,%A0));

	case 11:
	  *len = 5;
	  return (AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (clr,%B0)     CR_TAB
		  AS1 (lsr,%A0)     CR_TAB
		  AS1 (lsr,%A0)     CR_TAB
		  AS1 (lsr,%A0));

	case 12:
	  if (ldi_ok)
	    {
	      *len = 4;
	      return (AS2 (mov,%A0,%B0) CR_TAB
		      AS1 (clr,%B0)     CR_TAB
		      AS1 (swap,%A0)    CR_TAB
		      AS2 (andi,%A0,0x0f));
	    }
	  if (scratch)
	    {
	      *len = 5;
	      return (AS2 (mov,%A0,%B0) CR_TAB
		      AS1 (clr,%B0)     CR_TAB
		      AS1 (swap,%A0)    CR_TAB
		      AS2 (ldi,%3,0x0f) CR_TAB
		      AS2 (and,%A0,%3));
	    }
	  *len = 6;
	  return (AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (clr,%B0)     CR_TAB
		  AS1 (lsr,%A0)     CR_TAB
		  AS1 (lsr,%A0)     CR_TAB
		  AS1 (lsr,%A0)     CR_TAB
		  AS1 (lsr,%A0));

	case 13:
	  if (ldi_ok)
	    {
	      *len = 5;
	      return (AS2 (mov,%A0,%B0) CR_TAB
		      AS1 (clr,%B0)     CR_TAB
		      AS1 (swap,%A0)    CR_TAB
		      AS1 (lsr,%A0)     CR_TAB
		      AS2 (andi,%A0,0x07));
	    }
	  if (AVR_ENHANCED && scratch)
	    {
	      *len = 5;
	      return (AS2 (ldi,%3,0x08) CR_TAB
		      AS2 (mul,%B0,%3)  CR_TAB
		      AS2 (mov,%A0,r1)  CR_TAB
		      AS1 (clr,%B0)     CR_TAB
		      AS1 (clr,__zero_reg__));
	    }
	  if (optimize_size && scratch)
	    break;  /* 5 */
	  if (scratch)
	    {
	      *len = 6;
	      return (AS2 (mov,%A0,%B0) CR_TAB
		      AS1 (clr,%B0)     CR_TAB
		      AS1 (swap,%A0)    CR_TAB
		      AS1 (lsr,%A0)     CR_TAB
		      AS2 (ldi,%3,0x07) CR_TAB
		      AS2 (and,%A0,%3));
	    }
	  if (AVR_ENHANCED)
	    {
	      *len = 6;
	      return ("set"            CR_TAB
		      AS2 (bld,r1,3)   CR_TAB
		      AS2 (mul,%B0,r1) CR_TAB
		      AS2 (mov,%A0,r1) CR_TAB
		      AS1 (clr,%B0)    CR_TAB
		      AS1 (clr,__zero_reg__));
	    }
	  *len = 7;
	  return (AS2 (mov,%A0,%B0) CR_TAB
		  AS1 (clr,%B0)     CR_TAB
		  AS1 (lsr,%A0)     CR_TAB
		  AS1 (lsr,%A0)     CR_TAB
		  AS1 (lsr,%A0)     CR_TAB
		  AS1 (lsr,%A0)     CR_TAB
		  AS1 (lsr,%A0));

	case 14:
	  if (AVR_ENHANCED && ldi_ok)
	    {
	      *len = 5;
	      return (AS2 (ldi,%A0,0x04) CR_TAB
		      AS2 (mul,%B0,%A0)  CR_TAB
		      AS2 (mov,%A0,r1)   CR_TAB
		      AS1 (clr,%B0)      CR_TAB
		      AS1 (clr,__zero_reg__));
	    }
	  if (AVR_ENHANCED && scratch)
	    {
	      *len = 5;
	      return (AS2 (ldi,%3,0x04) CR_TAB
		      AS2 (mul,%B0,%3)  CR_TAB
		      AS2 (mov,%A0,r1)  CR_TAB
		      AS1 (clr,%B0)     CR_TAB
		      AS1 (clr,__zero_reg__));
	    }
	  if (optimize_size && ldi_ok)
	    {
	      *len = 5;
	      return (AS2 (mov,%A0,%B0) CR_TAB
		      AS2 (ldi,%B0,6) "\n1:\t"
		      AS1 (lsr,%A0)     CR_TAB
		      AS1 (dec,%B0)     CR_TAB
		      AS1 (brne,1b));
	    }
	  if (optimize_size && scratch)
	    break;  /* 5 */
	  *len = 6;
	  return (AS1 (clr,%A0) CR_TAB
		  AS1 (lsl,%B0) CR_TAB
		  AS1 (rol,%A0) CR_TAB
		  AS1 (lsl,%B0) CR_TAB
		  AS1 (rol,%A0) CR_TAB
		  AS1 (clr,%B0));

	case 15:
	  *len = 4;
	  return (AS1 (clr,%A0) CR_TAB
		  AS1 (lsl,%B0) CR_TAB
		  AS1 (rol,%A0) CR_TAB
		  AS1 (clr,%B0));
	}
      len = t;
    }
  out_shift_with_cnt ((AS1 (lsr,%B0) CR_TAB
		       AS1 (ror,%A0)),
		       insn, operands, len, 2);
  return "";
}

/* 32bit logic shift right ((unsigned int)x >> i) */

const char *
lshrsi3_out (rtx insn, rtx operands[], int *len)
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      int k;
      int *t = len;
      
      if (!len)
	len = &k;
      
      switch (INTVAL (operands[2]))
	{
	default:
	  if (INTVAL (operands[2]) < 32)
	    break;

	  if (AVR_HAVE_MOVW)
	    return *len = 3, (AS1 (clr,%D0) CR_TAB
			      AS1 (clr,%C0) CR_TAB
			      AS2 (movw,%A0,%C0));
	  *len = 4;
	  return (AS1 (clr,%D0) CR_TAB
		  AS1 (clr,%C0) CR_TAB
		  AS1 (clr,%B0) CR_TAB
		  AS1 (clr,%A0));

	case 8:
	  {
	    int reg0 = true_regnum (operands[0]);
	    int reg1 = true_regnum (operands[1]);
	    *len = 4;
	    if (reg0 <= reg1)
	      return (AS2 (mov,%A0,%B1) CR_TAB
		      AS2 (mov,%B0,%C1) CR_TAB
		      AS2 (mov,%C0,%D1) CR_TAB
		      AS1 (clr,%D0));
	    else if (reg0 == reg1 + 1)
	      return *len = 1, AS1 (clr,%D0);
	    else
	      return (AS1 (clr,%D0)     CR_TAB
		      AS2 (mov,%C0,%D1) CR_TAB
		      AS2 (mov,%B0,%C1) CR_TAB
		      AS2 (mov,%A0,%B1)); 
	  }
	  
	case 16:
	  {
	    int reg0 = true_regnum (operands[0]);
	    int reg1 = true_regnum (operands[1]);
	    *len = 4;
	    if (AVR_HAVE_MOVW && (reg0 != reg1 + 2))
	      {
		*len = 3;
		return (AS2 (movw,%A0,%C1) CR_TAB
			AS1 (clr,%C0)      CR_TAB
			AS1 (clr,%D0));
	      }
	    if (reg0 <= reg1 + 1)
	      return (AS2 (mov,%A0,%C1) CR_TAB
		      AS2 (mov,%B0,%D1) CR_TAB
		      AS1 (clr,%C0)     CR_TAB
		      AS1 (clr,%D0));
	    else if (reg0 == reg1 + 2)
	      return *len = 2, (AS1 (clr,%C0)     CR_TAB
				AS1 (clr,%D0));
	    else
	      return (AS2 (mov,%B0,%D1) CR_TAB
		      AS2 (mov,%A0,%C1) CR_TAB
		      AS1 (clr,%C0)     CR_TAB
		      AS1 (clr,%D0));
	  }
	  
	case 24:
	  if (true_regnum (operands[0]) != true_regnum (operands[1]) + 3)
	    return *len = 4, (AS2 (mov,%A0,%D1) CR_TAB
			      AS1 (clr,%B0)     CR_TAB
			      AS1 (clr,%C0)     CR_TAB
			      AS1 (clr,%D0));
	  else
	    return *len = 3, (AS1 (clr,%B0)     CR_TAB
			      AS1 (clr,%C0)     CR_TAB
			      AS1 (clr,%D0));

	case 31:
	  *len = 6;
	  return (AS1 (clr,%A0)    CR_TAB
		  AS2 (sbrc,%D0,7) CR_TAB
		  AS1 (inc,%A0)    CR_TAB
		  AS1 (clr,%B0)    CR_TAB
		  AS1 (clr,%C0)    CR_TAB
		  AS1 (clr,%D0));
	}
      len = t;
    }
  out_shift_with_cnt ((AS1 (lsr,%D0) CR_TAB
		       AS1 (ror,%C0) CR_TAB
		       AS1 (ror,%B0) CR_TAB
		       AS1 (ror,%A0)),
		      insn, operands, len, 4);
  return "";
}

/* Modifies the length assigned to instruction INSN
 LEN is the initially computed length of the insn.  */

int
adjust_insn_length (rtx insn, int len)
{
  rtx patt = PATTERN (insn);
  rtx set;

  if (GET_CODE (patt) == SET)
    {
      rtx op[10];
      op[1] = SET_SRC (patt);
      op[0] = SET_DEST (patt);
      if (general_operand (op[1], VOIDmode)
	  && general_operand (op[0], VOIDmode))
	{
	  switch (GET_MODE (op[0]))
	    {
	    case QImode:
	      output_movqi (insn, op, &len);
	      break;
	    case HImode:
	      output_movhi (insn, op, &len);
	      break;
	    case SImode:
	    case SFmode:
	      output_movsisf (insn, op, &len);
	      break;
	    default:
	      break;
	    }
	}
      else if (op[0] == cc0_rtx && REG_P (op[1]))
	{
	  switch (GET_MODE (op[1]))
	    {
	    case HImode: out_tsthi (insn,&len); break;
	    case SImode: out_tstsi (insn,&len); break;
	    default: break;
	    }
	}
      else if (GET_CODE (op[1]) == AND)
	{
	  if (GET_CODE (XEXP (op[1],1)) == CONST_INT)
	    {
	      HOST_WIDE_INT mask = INTVAL (XEXP (op[1],1));
	      if (GET_MODE (op[1]) == SImode)
		len = (((mask & 0xff) != 0xff)
		       + ((mask & 0xff00) != 0xff00)
		       + ((mask & 0xff0000L) != 0xff0000L)
		       + ((mask & 0xff000000L) != 0xff000000L));
	      else if (GET_MODE (op[1]) == HImode)
		len = (((mask & 0xff) != 0xff)
		       + ((mask & 0xff00) != 0xff00));
	    }
	}
      else if (GET_CODE (op[1]) == IOR)
	{
	  if (GET_CODE (XEXP (op[1],1)) == CONST_INT)
	    {
	      HOST_WIDE_INT mask = INTVAL (XEXP (op[1],1));
	      if (GET_MODE (op[1]) == SImode)
		len = (((mask & 0xff) != 0)
		       + ((mask & 0xff00) != 0)
		       + ((mask & 0xff0000L) != 0)
		       + ((mask & 0xff000000L) != 0));
	      else if (GET_MODE (op[1]) == HImode)
		len = (((mask & 0xff) != 0)
		       + ((mask & 0xff00) != 0));
	    }
	}
    }
  set = single_set (insn);
  if (set)
    {
      rtx op[10];

      op[1] = SET_SRC (set);
      op[0] = SET_DEST (set);

      if (GET_CODE (patt) == PARALLEL
	  && general_operand (op[1], VOIDmode)
	  && general_operand (op[0], VOIDmode))
	{
	  if (XVECLEN (patt, 0) == 2)
	    op[2] = XVECEXP (patt, 0, 1);

	  switch (GET_MODE (op[0]))
	    {
	    case QImode:
	      len = 2;
	      break;
	    case HImode:
	      output_reload_inhi (insn, op, &len);
	      break;
	    case SImode:
	    case SFmode:
	      output_reload_insisf (insn, op, &len);
	      break;
	    default:
	      break;
	    }
	}
      else if (GET_CODE (op[1]) == ASHIFT
	  || GET_CODE (op[1]) == ASHIFTRT
	  || GET_CODE (op[1]) == LSHIFTRT)
	{
	  rtx ops[10];
	  ops[0] = op[0];
	  ops[1] = XEXP (op[1],0);
	  ops[2] = XEXP (op[1],1);
	  switch (GET_CODE (op[1]))
	    {
	    case ASHIFT:
	      switch (GET_MODE (op[0]))
		{
		case QImode: ashlqi3_out (insn,ops,&len); break;
		case HImode: ashlhi3_out (insn,ops,&len); break;
		case SImode: ashlsi3_out (insn,ops,&len); break;
		default: break;
		}
	      break;
	    case ASHIFTRT:
	      switch (GET_MODE (op[0]))
		{
		case QImode: ashrqi3_out (insn,ops,&len); break;
		case HImode: ashrhi3_out (insn,ops,&len); break;
		case SImode: ashrsi3_out (insn,ops,&len); break;
		default: break;
		}
	      break;
	    case LSHIFTRT:
	      switch (GET_MODE (op[0]))
		{
		case QImode: lshrqi3_out (insn,ops,&len); break;
		case HImode: lshrhi3_out (insn,ops,&len); break;
		case SImode: lshrsi3_out (insn,ops,&len); break;
		default: break;
		}
	      break;
	    default:
	      break;
	    }
	}
    }
  return len;
}

/* Return nonzero if register REG dead after INSN.  */

int
reg_unused_after (rtx insn, rtx reg)
{
  return (dead_or_set_p (insn, reg)
	  || (REG_P(reg) && _reg_unused_after (insn, reg)));
}

/* Return nonzero if REG is not used after INSN.
   We assume REG is a reload reg, and therefore does
   not live past labels.  It may live past calls or jumps though.  */

int
_reg_unused_after (rtx insn, rtx reg)
{
  enum rtx_code code;
  rtx set;

  /* If the reg is set by this instruction, then it is safe for our
     case.  Disregard the case where this is a store to memory, since
     we are checking a register used in the store address.  */
  set = single_set (insn);
  if (set && GET_CODE (SET_DEST (set)) != MEM
      && reg_overlap_mentioned_p (reg, SET_DEST (set)))
    return 1;

  while ((insn = NEXT_INSN (insn)))
    {
      rtx set;
      code = GET_CODE (insn);

#if 0
      /* If this is a label that existed before reload, then the register
	 if dead here.  However, if this is a label added by reorg, then
	 the register may still be live here.  We can't tell the difference,
	 so we just ignore labels completely.  */
      if (code == CODE_LABEL)
	return 1;
      /* else */
#endif

      if (!INSN_P (insn))
	continue;

      if (code == JUMP_INSN)
	return 0;

      /* If this is a sequence, we must handle them all at once.
	 We could have for instance a call that sets the target register,
	 and an insn in a delay slot that uses the register.  In this case,
	 we must return 0.  */
      else if (code == INSN && GET_CODE (PATTERN (insn)) == SEQUENCE)
	{
	  int i;
	  int retval = 0;

	  for (i = 0; i < XVECLEN (PATTERN (insn), 0); i++)
	    {
	      rtx this_insn = XVECEXP (PATTERN (insn), 0, i);
	      rtx set = single_set (this_insn);

	      if (GET_CODE (this_insn) == CALL_INSN)
		code = CALL_INSN;
	      else if (GET_CODE (this_insn) == JUMP_INSN)
		{
		  if (INSN_ANNULLED_BRANCH_P (this_insn))
		    return 0;
		  code = JUMP_INSN;
		}

	      if (set && reg_overlap_mentioned_p (reg, SET_SRC (set)))
		return 0;
	      if (set && reg_overlap_mentioned_p (reg, SET_DEST (set)))
		{
		  if (GET_CODE (SET_DEST (set)) != MEM)
		    retval = 1;
		  else
		    return 0;
		}
	      if (set == 0
		  && reg_overlap_mentioned_p (reg, PATTERN (this_insn)))
		return 0;
	    }
	  if (retval == 1)
	    return 1;
	  else if (code == JUMP_INSN)
	    return 0;
	}

      if (code == CALL_INSN)
	{
	  rtx tem;
	  for (tem = CALL_INSN_FUNCTION_USAGE (insn); tem; tem = XEXP (tem, 1))
	    if (GET_CODE (XEXP (tem, 0)) == USE
		&& REG_P (XEXP (XEXP (tem, 0), 0))
		&& reg_overlap_mentioned_p (reg, XEXP (XEXP (tem, 0), 0)))
	      return 0;
	  if (call_used_regs[REGNO (reg)]) 
	    return 1;
	}

      set = single_set (insn);

      if (set && reg_overlap_mentioned_p (reg, SET_SRC (set)))
	return 0;
      if (set && reg_overlap_mentioned_p (reg, SET_DEST (set)))
	return GET_CODE (SET_DEST (set)) != MEM;
      if (set == 0 && reg_overlap_mentioned_p (reg, PATTERN (insn)))
	return 0;
    }
  return 1;
}

/* Target hook for assembling integer objects.  The AVR version needs
   special handling for references to certain labels.  */

static bool
avr_assemble_integer (rtx x, unsigned int size, int aligned_p)
{
  if (size == POINTER_SIZE / BITS_PER_UNIT && aligned_p
      && ((GET_CODE (x) == SYMBOL_REF && SYMBOL_REF_FUNCTION_P (x))
	  || GET_CODE (x) == LABEL_REF))
    {
      fputs ("\t.word\tpm(", asm_out_file);
      output_addr_const (asm_out_file, x);
      fputs (")\n", asm_out_file);
      return true;
    }
  return default_assemble_integer (x, size, aligned_p);
}

/* The routine used to output NUL terminated strings.  We use a special
   version of this for most svr4 targets because doing so makes the
   generated assembly code more compact (and thus faster to assemble)
   as well as more readable, especially for targets like the i386
   (where the only alternative is to output character sequences as
   comma separated lists of numbers).  */

void
gas_output_limited_string(FILE *file, const char *str)
{
  const unsigned char *_limited_str = (unsigned char *) str;
  unsigned ch;
  fprintf (file, "%s\"", STRING_ASM_OP);
  for (; (ch = *_limited_str); _limited_str++)
    {
      int escape;
      switch (escape = ESCAPES[ch])
	{
	case 0:
	  putc (ch, file);
	  break;
	case 1:
	  fprintf (file, "\\%03o", ch);
	  break;
	default:
	  putc ('\\', file);
	  putc (escape, file);
	  break;
	}
    }
  fprintf (file, "\"\n");
}

/* The routine used to output sequences of byte values.  We use a special
   version of this for most svr4 targets because doing so makes the
   generated assembly code more compact (and thus faster to assemble)
   as well as more readable.  Note that if we find subparts of the
   character sequence which end with NUL (and which are shorter than
   STRING_LIMIT) we output those using ASM_OUTPUT_LIMITED_STRING.  */

void
gas_output_ascii(FILE *file, const char *str, size_t length)
{
  const unsigned char *_ascii_bytes = (const unsigned char *) str;
  const unsigned char *limit = _ascii_bytes + length;
  unsigned bytes_in_chunk = 0;
  for (; _ascii_bytes < limit; _ascii_bytes++)
    {
      const unsigned char *p;
      if (bytes_in_chunk >= 60)
	{
	  fprintf (file, "\"\n");
	  bytes_in_chunk = 0;
	}
      for (p = _ascii_bytes; p < limit && *p != '\0'; p++)
	continue;
      if (p < limit && (p - _ascii_bytes) <= (signed)STRING_LIMIT)
	{
	  if (bytes_in_chunk > 0)
	    {
	      fprintf (file, "\"\n");
	      bytes_in_chunk = 0;
	    }
	  gas_output_limited_string (file, (char*)_ascii_bytes);
	  _ascii_bytes = p;
	}
      else
	{
	  int escape;
	  unsigned ch;
	  if (bytes_in_chunk == 0)
	    fprintf (file, "\t.ascii\t\"");
	  switch (escape = ESCAPES[ch = *_ascii_bytes])
	    {
	    case 0:
	      putc (ch, file);
	      bytes_in_chunk++;
	      break;
	    case 1:
	      fprintf (file, "\\%03o", ch);
	      bytes_in_chunk += 4;
	      break;
	    default:
	      putc ('\\', file);
	      putc (escape, file);
	      bytes_in_chunk += 2;
	      break;
	    }
	}
    }
  if (bytes_in_chunk > 0)
    fprintf (file, "\"\n");
}

/* Return value is nonzero if pseudos that have been
   assigned to registers of class CLASS would likely be spilled
   because registers of CLASS are needed for spill registers.  */

enum reg_class
class_likely_spilled_p (int c)
{
  return (c != ALL_REGS && c != ADDW_REGS);
}

/* Valid attributes:
   progmem - put data to program memory;
   signal - make a function to be hardware interrupt. After function
   prologue interrupts are disabled;
   interrupt - make a function to be hardware interrupt. After function
   prologue interrupts are enabled;
   naked     - don't generate function prologue/epilogue and `ret' command.

   Only `progmem' attribute valid for type.  */

const struct attribute_spec avr_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "progmem",   0, 0, false, false, false,  avr_handle_progmem_attribute },
  { "signal",    0, 0, true,  false, false,  avr_handle_fndecl_attribute },
  { "interrupt", 0, 0, true,  false, false,  avr_handle_fndecl_attribute },
  { "naked",     0, 0, false, true,  true,   avr_handle_fntype_attribute },
  { NULL,        0, 0, false, false, false, NULL }
};

/* Handle a "progmem" attribute; arguments as in
   struct attribute_spec.handler.  */
static tree
avr_handle_progmem_attribute (tree *node, tree name,
			      tree args ATTRIBUTE_UNUSED,
			      int flags ATTRIBUTE_UNUSED,
			      bool *no_add_attrs)
{
  if (DECL_P (*node))
    {
      if (TREE_CODE (*node) == TYPE_DECL)
	{
	  /* This is really a decl attribute, not a type attribute,
	     but try to handle it for GCC 3.0 backwards compatibility.  */

	  tree type = TREE_TYPE (*node);
	  tree attr = tree_cons (name, args, TYPE_ATTRIBUTES (type));
	  tree newtype = build_type_attribute_variant (type, attr);

	  TYPE_MAIN_VARIANT (newtype) = TYPE_MAIN_VARIANT (type);
	  TREE_TYPE (*node) = newtype;
	  *no_add_attrs = true;
	}
      else if (TREE_STATIC (*node) || DECL_EXTERNAL (*node))
	{
	  if (DECL_INITIAL (*node) == NULL_TREE && !DECL_EXTERNAL (*node))
	    {
	      warning (0, "only initialized variables can be placed into "
		       "program memory area");
	      *no_add_attrs = true;
	    }
	}
      else
	{
	  warning (OPT_Wattributes, "%qs attribute ignored",
		   IDENTIFIER_POINTER (name));
	  *no_add_attrs = true;
	}
    }

  return NULL_TREE;
}

/* Handle an attribute requiring a FUNCTION_DECL; arguments as in
   struct attribute_spec.handler.  */

static tree
avr_handle_fndecl_attribute (tree *node, tree name,
			     tree args ATTRIBUTE_UNUSED,
			     int flags ATTRIBUTE_UNUSED,
			     bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_DECL)
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }
  else
    {
      const char *func_name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (*node));
      const char *attr = IDENTIFIER_POINTER (name);

      /* If the function has the 'signal' or 'interrupt' attribute, test to
         make sure that the name of the function is "__vector_NN" so as to
         catch when the user misspells the interrupt vector name.  */

      if (strncmp (attr, "interrupt", strlen ("interrupt")) == 0)
        {
          if (strncmp (func_name, "__vector", strlen ("__vector")) != 0)
            {
              warning (0, "%qs appears to be a misspelled interrupt handler",
                       func_name);
            }
        }
      else if (strncmp (attr, "signal", strlen ("signal")) == 0)
        {
          if (strncmp (func_name, "__vector", strlen ("__vector")) != 0)
            {
              warning (0, "%qs appears to be a misspelled signal handler",
                       func_name);
            }
        }
    }

  return NULL_TREE;
}

static tree
avr_handle_fntype_attribute (tree *node, tree name,
                             tree args ATTRIBUTE_UNUSED,
                             int flags ATTRIBUTE_UNUSED,
                             bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_TYPE)
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Look for attribute `progmem' in DECL
   if found return 1, otherwise 0.  */

int
avr_progmem_p (tree decl, tree attributes)
{
  tree a;

  if (TREE_CODE (decl) != VAR_DECL)
    return 0;

  if (NULL_TREE
      != lookup_attribute ("progmem", attributes))
    return 1;

  a=decl;
  do
    a = TREE_TYPE(a);
  while (TREE_CODE (a) == ARRAY_TYPE);

  if (a == error_mark_node)
    return 0;

  if (NULL_TREE != lookup_attribute ("progmem", TYPE_ATTRIBUTES (a)))
    return 1;
  
  return 0;
}

/* Add the section attribute if the variable is in progmem.  */

static void
avr_insert_attributes (tree node, tree *attributes)
{
  if (TREE_CODE (node) == VAR_DECL
      && (TREE_STATIC (node) || DECL_EXTERNAL (node))
      && avr_progmem_p (node, *attributes))
    {
      static const char dsec[] = ".progmem.data";
      *attributes = tree_cons (get_identifier ("section"),
		build_tree_list (NULL, build_string (strlen (dsec), dsec)),
		*attributes);

      /* ??? This seems sketchy.  Why can't the user declare the
	 thing const in the first place?  */
      TREE_READONLY (node) = 1;
    }
}

/* A get_unnamed_section callback for switching to progmem_section.  */

static void
avr_output_progmem_section_asm_op (const void *arg ATTRIBUTE_UNUSED)
{
  fprintf (asm_out_file,
	   "\t.section .progmem.gcc_sw_table, \"%s\", @progbits\n",
	   AVR_MEGA ? "a" : "ax");
  /* Should already be aligned, this is just to be safe if it isn't.  */
  fprintf (asm_out_file, "\t.p2align 1\n");
}

/* Implement TARGET_ASM_INIT_SECTIONS.  */

static void
avr_asm_init_sections (void)
{
  progmem_section = get_unnamed_section (AVR_MEGA ? 0 : SECTION_CODE,
					 avr_output_progmem_section_asm_op,
					 NULL);
  readonly_data_section = data_section;
}

static unsigned int
avr_section_type_flags (tree decl, const char *name, int reloc)
{
  unsigned int flags = default_section_type_flags (decl, name, reloc);

  if (strncmp (name, ".noinit", 7) == 0)
    {
      if (decl && TREE_CODE (decl) == VAR_DECL
	  && DECL_INITIAL (decl) == NULL_TREE)
	flags |= SECTION_BSS;  /* @nobits */
      else
	warning (0, "only uninitialized variables can be placed in the "
		 ".noinit section");
    }

  return flags;
}

/* Outputs some appropriate text to go at the start of an assembler
   file.  */

static void
avr_file_start (void)
{
  if (avr_asm_only_p)
    error ("MCU %qs supported for assembler only", avr_mcu_name);

  default_file_start ();

/*  fprintf (asm_out_file, "\t.arch %s\n", avr_mcu_name);*/
  fputs ("__SREG__ = 0x3f\n"
	 "__SP_H__ = 0x3e\n"
	 "__SP_L__ = 0x3d\n", asm_out_file);
  
  fputs ("__tmp_reg__ = 0\n" 
         "__zero_reg__ = 1\n", asm_out_file);

  /* FIXME: output these only if there is anything in the .data / .bss
     sections - some code size could be saved by not linking in the
     initialization code from libgcc if one or both sections are empty.  */
  fputs ("\t.global __do_copy_data\n", asm_out_file);
  fputs ("\t.global __do_clear_bss\n", asm_out_file);

  commands_in_file = 0;
  commands_in_prologues = 0;
  commands_in_epilogues = 0;
}

/* Outputs to the stdio stream FILE some
   appropriate text to go at the end of an assembler file.  */

static void
avr_file_end (void)
{
  fputs ("/* File ", asm_out_file);
  output_quoted_string (asm_out_file, main_input_filename);
  fprintf (asm_out_file,
	   ": code %4d = 0x%04x (%4d), prologues %3d, epilogues %3d */\n",
	   commands_in_file,
	   commands_in_file,
	   commands_in_file - commands_in_prologues - commands_in_epilogues,
	   commands_in_prologues, commands_in_epilogues);
}

/* Choose the order in which to allocate hard registers for
   pseudo-registers local to a basic block.

   Store the desired register order in the array `reg_alloc_order'.
   Element 0 should be the register to allocate first; element 1, the
   next register; and so on.  */

void
order_regs_for_local_alloc (void)
{
  unsigned int i;
  static const int order_0[] = {
    24,25,
    18,19,
    20,21,
    22,23,
    30,31,
    26,27,
    28,29,
    17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,
    0,1,
    32,33,34,35
  };
  static const int order_1[] = {
    18,19,
    20,21,
    22,23,
    24,25,
    30,31,
    26,27,
    28,29,
    17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,
    0,1,
    32,33,34,35
  };
  static const int order_2[] = {
    25,24,
    23,22,
    21,20,
    19,18,
    30,31,
    26,27,
    28,29,
    17,16,
    15,14,13,12,11,10,9,8,7,6,5,4,3,2,
    1,0,
    32,33,34,35
  };
  
  const int *order = (TARGET_ORDER_1 ? order_1 :
		      TARGET_ORDER_2 ? order_2 :
		      order_0);
  for (i=0; i < ARRAY_SIZE (order_0); ++i)
      reg_alloc_order[i] = order[i];
}


/* Mutually recursive subroutine of avr_rtx_cost for calculating the
   cost of an RTX operand given its context.  X is the rtx of the
   operand, MODE is its mode, and OUTER is the rtx_code of this
   operand's parent operator.  */

static int
avr_operand_rtx_cost (rtx x, enum machine_mode mode, enum rtx_code outer)
{
  enum rtx_code code = GET_CODE (x);
  int total;

  switch (code)
    {
    case REG:
    case SUBREG:
      return 0;

    case CONST_INT:
    case CONST_DOUBLE:
      return COSTS_N_INSNS (GET_MODE_SIZE (mode));

    default:
      break;
    }

  total = 0;
  avr_rtx_costs (x, code, outer, &total);
  return total;
}

/* The AVR backend's rtx_cost function.  X is rtx expression whose cost
   is to be calculated.  Return true if the complete cost has been
   computed, and false if subexpressions should be scanned.  In either
   case, *TOTAL contains the cost result.  */

static bool
avr_rtx_costs (rtx x, int code, int outer_code ATTRIBUTE_UNUSED, int *total)
{
  enum machine_mode mode = GET_MODE (x);
  HOST_WIDE_INT val;

  switch (code)
    {
    case CONST_INT:
    case CONST_DOUBLE:
      /* Immediate constants are as cheap as registers.  */
      *total = 0;
      return true;

    case MEM:
    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      *total = COSTS_N_INSNS (GET_MODE_SIZE (mode));
      return true;

    case NEG:
      switch (mode)
	{
	case QImode:
	case SFmode:
	  *total = COSTS_N_INSNS (1);
	  break;

	case HImode:
	  *total = COSTS_N_INSNS (3);
	  break;

	case SImode:
	  *total = COSTS_N_INSNS (7);
	  break;

	default:
	  return false;
	}
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      return true;

    case ABS:
      switch (mode)
	{
	case QImode:
	case SFmode:
	  *total = COSTS_N_INSNS (1);
	  break;

	default:
	  return false;
	}
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      return true;

    case NOT:
      *total = COSTS_N_INSNS (GET_MODE_SIZE (mode));
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      return true;

    case ZERO_EXTEND:
      *total = COSTS_N_INSNS (GET_MODE_SIZE (mode)
			      - GET_MODE_SIZE (GET_MODE (XEXP (x, 0))));
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      return true;

    case SIGN_EXTEND:
      *total = COSTS_N_INSNS (GET_MODE_SIZE (mode) + 2
			      - GET_MODE_SIZE (GET_MODE (XEXP (x, 0))));
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      return true;

    case PLUS:
      switch (mode)
	{
	case QImode:
	  *total = COSTS_N_INSNS (1);
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	  break;

	case HImode:
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    {
	      *total = COSTS_N_INSNS (2);
	      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	    }
	  else if (INTVAL (XEXP (x, 1)) >= -63 && INTVAL (XEXP (x, 1)) <= 63)
	    *total = COSTS_N_INSNS (1);
	  else
	    *total = COSTS_N_INSNS (2);
	  break;

	case SImode:
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    {
	      *total = COSTS_N_INSNS (4);
	      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	    }
	  else if (INTVAL (XEXP (x, 1)) >= -63 && INTVAL (XEXP (x, 1)) <= 63)
	    *total = COSTS_N_INSNS (1);
	  else
	    *total = COSTS_N_INSNS (4);
	  break;

	default:
	  return false;
	}
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      return true;

    case MINUS:
    case AND:
    case IOR:
      *total = COSTS_N_INSNS (GET_MODE_SIZE (mode));
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      if (GET_CODE (XEXP (x, 1)) != CONST_INT)
          *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
      return true;

    case XOR:
      *total = COSTS_N_INSNS (GET_MODE_SIZE (mode));
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
      return true;

    case MULT:
      switch (mode)
	{
	case QImode:
	  if (AVR_ENHANCED)
	    *total = COSTS_N_INSNS (optimize_size ? 3 : 4);
	  else if (optimize_size)
	    *total = COSTS_N_INSNS (AVR_MEGA ? 2 : 1);
	  else
	    return false;
	  break;

	case HImode:
	  if (AVR_ENHANCED)
	    *total = COSTS_N_INSNS (optimize_size ? 7 : 10);
	  else if (optimize_size)
	    *total = COSTS_N_INSNS (AVR_MEGA ? 2 : 1);
	  else
	    return false;
	  break;

	default:
	  return false;
	}
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
      return true;

    case DIV:
    case MOD:
    case UDIV:
    case UMOD:
      if (optimize_size)
	*total = COSTS_N_INSNS (AVR_MEGA ? 2 : 1);
      else
	return false;
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
      return true;

    case ASHIFT:
      switch (mode)
	{
	case QImode:
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    {
	      *total = COSTS_N_INSNS (optimize_size ? 4 : 17);
	      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	    }
	  else
	    {
	      val = INTVAL (XEXP (x, 1));
	      if (val == 7)
		*total = COSTS_N_INSNS (3);
	      else if (val >= 0 && val <= 7)
		*total = COSTS_N_INSNS (val);
	      else
		*total = COSTS_N_INSNS (1);
	    }
	  break;

	case HImode:
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    {
	      *total = COSTS_N_INSNS (optimize_size ? 5 : 41);
	      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	    }
	  else
	    switch (INTVAL (XEXP (x, 1)))
	      {
	      case 0:
		*total = 0;
		break;
	      case 1:
	      case 8:
		*total = COSTS_N_INSNS (2);
		break;
	      case 9:
		*total = COSTS_N_INSNS (3);
		break;
	      case 2:
	      case 3:
	      case 10:
	      case 15:
		*total = COSTS_N_INSNS (4);
		break;
	      case 7:
	      case 11:
	      case 12:
		*total = COSTS_N_INSNS (5);
		break;
	      case 4:
		*total = COSTS_N_INSNS (optimize_size ? 5 : 8);
		break;
	      case 6:
		*total = COSTS_N_INSNS (optimize_size ? 5 : 9);
		break;
	      case 5:
		*total = COSTS_N_INSNS (optimize_size ? 5 : 10);
		break;
	      default:
	        *total = COSTS_N_INSNS (optimize_size ? 5 : 41);
	        *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	      }
	  break;

	case SImode:
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    {
	      *total = COSTS_N_INSNS (optimize_size ? 7 : 113);
	      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	    }
	  else
	    switch (INTVAL (XEXP (x, 1)))
	      {
	      case 0:
		*total = 0;
		break;
	      case 24:
		*total = COSTS_N_INSNS (3);
		break;
	      case 1:
	      case 8:
	      case 16:
		*total = COSTS_N_INSNS (4);
		break;
	      case 31:
		*total = COSTS_N_INSNS (6);
		break;
	      case 2:
		*total = COSTS_N_INSNS (optimize_size ? 7 : 8);
		break;
	      default:
		*total = COSTS_N_INSNS (optimize_size ? 7 : 113);
		*total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	      }
	  break;

	default:
	  return false;
	}
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      return true;

    case ASHIFTRT:
      switch (mode)
	{
	case QImode:
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    {
	      *total = COSTS_N_INSNS (optimize_size ? 4 : 17);
	      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	    }
	  else
	    {
	      val = INTVAL (XEXP (x, 1));
	      if (val == 6)
		*total = COSTS_N_INSNS (4);
	      else if (val == 7)
		*total = COSTS_N_INSNS (2);
	      else if (val >= 0 && val <= 7)
		*total = COSTS_N_INSNS (val);
	      else
		*total = COSTS_N_INSNS (1);
	    }
	  break;

	case HImode:
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    {
	      *total = COSTS_N_INSNS (optimize_size ? 5 : 41);
	      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	    }
	  else
	    switch (INTVAL (XEXP (x, 1)))
	      {
	      case 0:
		*total = 0;
		break;
	      case 1:
		*total = COSTS_N_INSNS (2);
		break;
	      case 15:
		*total = COSTS_N_INSNS (3);
		break;
	      case 2:
	      case 7:
              case 8:
              case 9:
		*total = COSTS_N_INSNS (4);
		break;
              case 10:
	      case 14:
		*total = COSTS_N_INSNS (5);
		break;
              case 11:
                *total = COSTS_N_INSNS (optimize_size ? 5 : 6);
		break;
              case 12:
                *total = COSTS_N_INSNS (optimize_size ? 5 : 7);
		break;
              case 6:
	      case 13:
                *total = COSTS_N_INSNS (optimize_size ? 5 : 8);
		break;
	      default:
	        *total = COSTS_N_INSNS (optimize_size ? 5 : 41);
	        *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	      }
	  break;

	case SImode:
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    {
	      *total = COSTS_N_INSNS (optimize_size ? 7 : 113);
	      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	    }
	  else
	    switch (INTVAL (XEXP (x, 1)))
	      {
	      case 0:
		*total = 0;
		break;
	      case 1:
		*total = COSTS_N_INSNS (4);
		break;
	      case 8:
	      case 16:
	      case 24:
		*total = COSTS_N_INSNS (6);
		break;
	      case 2:
		*total = COSTS_N_INSNS (optimize_size ? 7 : 8);
		break;
	      case 31:
		*total = COSTS_N_INSNS (AVR_HAVE_MOVW ? 4 : 5);
		break;
	      default:
		*total = COSTS_N_INSNS (optimize_size ? 7 : 113);
		*total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	      }
	  break;

	default:
	  return false;
	}
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      return true;

    case LSHIFTRT:
      switch (mode)
	{
	case QImode:
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    {
	      *total = COSTS_N_INSNS (optimize_size ? 4 : 17);
	      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	    }
	  else
	    {
	      val = INTVAL (XEXP (x, 1));
	      if (val == 7)
		*total = COSTS_N_INSNS (3);
	      else if (val >= 0 && val <= 7)
		*total = COSTS_N_INSNS (val);
	      else
		*total = COSTS_N_INSNS (1);
	    }
	  break;

	case HImode:
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    {
	      *total = COSTS_N_INSNS (optimize_size ? 5 : 41);
	      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	    }
	  else
	    switch (INTVAL (XEXP (x, 1)))
	      {
	      case 0:
		*total = 0;
		break;
	      case 1:
	      case 8:
		*total = COSTS_N_INSNS (2);
		break;
	      case 9:
		*total = COSTS_N_INSNS (3);
		break;
	      case 2:
	      case 10:
	      case 15:
		*total = COSTS_N_INSNS (4);
		break;
	      case 7:
              case 11:
		*total = COSTS_N_INSNS (5);
		break;
	      case 3:
	      case 12:
	      case 13:
	      case 14:
		*total = COSTS_N_INSNS (optimize_size ? 5 : 6);
		break;
	      case 4:
		*total = COSTS_N_INSNS (optimize_size ? 5 : 7);
		break;
	      case 5:
	      case 6:
		*total = COSTS_N_INSNS (optimize_size ? 5 : 9);
		break;
	      default:
	        *total = COSTS_N_INSNS (optimize_size ? 5 : 41);
	        *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	      }
	  break;

	case SImode:
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    {
	      *total = COSTS_N_INSNS (optimize_size ? 7 : 113);
	      *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	    }
	  else
	    switch (INTVAL (XEXP (x, 1)))
	      {
	      case 0:
		*total = 0;
		break;
	      case 1:
		*total = COSTS_N_INSNS (4);
		break;
	      case 2:
		*total = COSTS_N_INSNS (optimize_size ? 7 : 8);
		break;
	      case 8:
	      case 16:
	      case 24:
		*total = COSTS_N_INSNS (4);
		break;
	      case 31:
		*total = COSTS_N_INSNS (6);
		break;
	      default:
		*total = COSTS_N_INSNS (optimize_size ? 7 : 113);
		*total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	      }
	  break;

	default:
	  return false;
	}
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      return true;

    case COMPARE:
      switch (GET_MODE (XEXP (x, 0)))
	{
	case QImode:
	  *total = COSTS_N_INSNS (1);
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	  break;

        case HImode:
	  *total = COSTS_N_INSNS (2);
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
            *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	  else if (INTVAL (XEXP (x, 1)) != 0)
	    *total += COSTS_N_INSNS (1);
          break;

        case SImode:
          *total = COSTS_N_INSNS (4);
          if (GET_CODE (XEXP (x, 1)) != CONST_INT)
            *total += avr_operand_rtx_cost (XEXP (x, 1), mode, code);
	  else if (INTVAL (XEXP (x, 1)) != 0)
	    *total += COSTS_N_INSNS (3);
          break;

	default:
	  return false;
	}
      *total += avr_operand_rtx_cost (XEXP (x, 0), mode, code);
      return true;

    default:
      break;
    }
  return false;
}

/* Calculate the cost of a memory address.  */

static int
avr_address_cost (rtx x)
{
  if (GET_CODE (x) == PLUS
      && GET_CODE (XEXP (x,1)) == CONST_INT
      && (REG_P (XEXP (x,0)) || GET_CODE (XEXP (x,0)) == SUBREG)
      && INTVAL (XEXP (x,1)) >= 61)
    return 18;
  if (CONSTANT_ADDRESS_P (x))
    {
      if (avr_io_address_p (x, 1))
	return 2;
      return 4;
    }
  return 4;
}

/* Test for extra memory constraint 'Q'.
   It's a memory address based on Y or Z pointer with valid displacement.  */

int
extra_constraint_Q (rtx x)
{
  if (GET_CODE (XEXP (x,0)) == PLUS
      && REG_P (XEXP (XEXP (x,0), 0))
      && GET_CODE (XEXP (XEXP (x,0), 1)) == CONST_INT
      && (INTVAL (XEXP (XEXP (x,0), 1))
	  <= MAX_LD_OFFSET (GET_MODE (x))))
    {
      rtx xx = XEXP (XEXP (x,0), 0);
      int regno = REGNO (xx);
      if (TARGET_ALL_DEBUG)
	{
	  fprintf (stderr, ("extra_constraint:\n"
			    "reload_completed: %d\n"
			    "reload_in_progress: %d\n"),
		   reload_completed, reload_in_progress);
	  debug_rtx (x);
	}
      if (regno >= FIRST_PSEUDO_REGISTER)
	return 1;		/* allocate pseudos */
      else if (regno == REG_Z || regno == REG_Y)
	return 1;		/* strictly check */
      else if (xx == frame_pointer_rtx
	       || xx == arg_pointer_rtx)
	return 1;		/* XXX frame & arg pointer checks */
    }
  return 0;
}

/* Convert condition code CONDITION to the valid AVR condition code.  */

RTX_CODE
avr_normalize_condition (RTX_CODE condition)
{
  switch (condition)
    {
    case GT:
      return GE;
    case GTU:
      return GEU;
    case LE:
      return LT;
    case LEU:
      return LTU;
    default:
      gcc_unreachable ();
    }
}

/* This function optimizes conditional jumps.  */

static void
avr_reorg (void)
{
  rtx insn, pattern;
  
  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      if (! (GET_CODE (insn) == INSN
	     || GET_CODE (insn) == CALL_INSN
	     || GET_CODE (insn) == JUMP_INSN)
	  || !single_set (insn))
	continue;

      pattern = PATTERN (insn);

      if (GET_CODE (pattern) == PARALLEL)
	pattern = XVECEXP (pattern, 0, 0);
      if (GET_CODE (pattern) == SET
	  && SET_DEST (pattern) == cc0_rtx
	  && compare_diff_p (insn))
	{
	  if (GET_CODE (SET_SRC (pattern)) == COMPARE)
	    {
	      /* Now we work under compare insn.  */
	      
	      pattern = SET_SRC (pattern);
	      if (true_regnum (XEXP (pattern,0)) >= 0
		  && true_regnum (XEXP (pattern,1)) >= 0 )
		{
		  rtx x = XEXP (pattern,0);
		  rtx next = next_real_insn (insn);
		  rtx pat = PATTERN (next);
		  rtx src = SET_SRC (pat);
		  rtx t = XEXP (src,0);
		  PUT_CODE (t, swap_condition (GET_CODE (t)));
		  XEXP (pattern,0) = XEXP (pattern,1);
		  XEXP (pattern,1) = x;
		  INSN_CODE (next) = -1;
		}
	      else if (true_regnum (XEXP (pattern,0)) >= 0
		       && GET_CODE (XEXP (pattern,1)) == CONST_INT)
		{
		  rtx x = XEXP (pattern,1);
		  rtx next = next_real_insn (insn);
		  rtx pat = PATTERN (next);
		  rtx src = SET_SRC (pat);
		  rtx t = XEXP (src,0);
		  enum machine_mode mode = GET_MODE (XEXP (pattern, 0));

		  if (avr_simplify_comparison_p (mode, GET_CODE (t), x))
		    {
		      XEXP (pattern, 1) = gen_int_mode (INTVAL (x) + 1, mode);
		      PUT_CODE (t, avr_normalize_condition (GET_CODE (t)));
		      INSN_CODE (next) = -1;
		      INSN_CODE (insn) = -1;
		    }
		}
	    }
	  else if (true_regnum (SET_SRC (pattern)) >= 0)
	    {
	      /* This is a tst insn */
	      rtx next = next_real_insn (insn);
	      rtx pat = PATTERN (next);
	      rtx src = SET_SRC (pat);
	      rtx t = XEXP (src,0);

	      PUT_CODE (t, swap_condition (GET_CODE (t)));
	      SET_SRC (pattern) = gen_rtx_NEG (GET_MODE (SET_SRC (pattern)),
					       SET_SRC (pattern));
	      INSN_CODE (next) = -1;
	      INSN_CODE (insn) = -1;
	    }
	}
    }
}

/* Returns register number for function return value.*/

int
avr_ret_register (void)
{
  return 24;
}

/* Ceate an RTX representing the place where a
   library function returns a value of mode MODE.  */

rtx
avr_libcall_value (enum machine_mode mode)
{
  int offs = GET_MODE_SIZE (mode);
  if (offs < 2)
    offs = 2;
  return gen_rtx_REG (mode, RET_REGISTER + 2 - offs);
}

/* Create an RTX representing the place where a
   function returns a value of data type VALTYPE.  */

rtx
avr_function_value (tree type, tree func ATTRIBUTE_UNUSED)
{
  unsigned int offs;
  
  if (TYPE_MODE (type) != BLKmode)
    return avr_libcall_value (TYPE_MODE (type));
  
  offs = int_size_in_bytes (type);
  if (offs < 2)
    offs = 2;
  if (offs > 2 && offs < GET_MODE_SIZE (SImode))
    offs = GET_MODE_SIZE (SImode);
  else if (offs > GET_MODE_SIZE (SImode) && offs < GET_MODE_SIZE (DImode))
    offs = GET_MODE_SIZE (DImode);
  
  return gen_rtx_REG (BLKmode, RET_REGISTER + 2 - offs);
}

/* Returns nonzero if the number MASK has only one bit set.  */

int
mask_one_bit_p (HOST_WIDE_INT mask)
{
  int i;
  unsigned HOST_WIDE_INT n=mask;
  for (i = 0; i < 32; ++i)
    {
      if (n & 0x80000000L)
	{
	  if (n & 0x7fffffffL)
	    return 0;
	  else
	    return 32-i;
	}
      n<<=1;
    }
  return 0; 
}


/* Places additional restrictions on the register class to
   use when it is necessary to copy value X into a register
   in class CLASS.  */

enum reg_class
preferred_reload_class (rtx x ATTRIBUTE_UNUSED, enum reg_class class)
{
  return class;
}

int
test_hard_reg_class (enum reg_class class, rtx x)
{
  int regno = true_regnum (x);
  if (regno < 0)
    return 0;

  if (TEST_HARD_REG_CLASS (class, regno))
    return 1;

  return 0;
}


int
jump_over_one_insn_p (rtx insn, rtx dest)
{
  int uid = INSN_UID (GET_CODE (dest) == LABEL_REF
		      ? XEXP (dest, 0)
		      : dest);
  int jump_addr = INSN_ADDRESSES (INSN_UID (insn));
  int dest_addr = INSN_ADDRESSES (uid);
  return dest_addr - jump_addr == get_attr_length (insn) + 1;
}

/* Returns 1 if a value of mode MODE can be stored starting with hard
   register number REGNO.  On the enhanced core, anything larger than
   1 byte must start in even numbered register for "movw" to work
   (this way we don't have to check for odd registers everywhere).  */

int
avr_hard_regno_mode_ok (int regno, enum machine_mode mode)
{
  /* The only thing that can go into registers r28:r29 is a Pmode.  */
  if (regno == REG_Y && mode == Pmode)
    return 1;

  /* Otherwise disallow all regno/mode combinations that span r28:r29.  */
  if (regno <= (REG_Y + 1) && (regno + GET_MODE_SIZE (mode)) >= (REG_Y + 1))
    return 0;

  if (mode == QImode)
    return 1;

  /* Modes larger than QImode occupy consecutive registers.  */
  if (regno + GET_MODE_SIZE (mode) > FIRST_PSEUDO_REGISTER)
    return 0;

  /* All modes larger than QImode should start in an even register.  */
  return !(regno & 1);
}

/* Returns 1 if X is a valid address for an I/O register of size SIZE
   (1 or 2).  Used for lds/sts -> in/out optimization.  Add 0x20 to SIZE
   to check for the lower half of I/O space (for cbi/sbi/sbic/sbis).  */

int
avr_io_address_p (rtx x, int size)
{
  return (optimize > 0 && GET_CODE (x) == CONST_INT
	  && INTVAL (x) >= 0x20 && INTVAL (x) <= 0x60 - size);
}

/* Returns nonzero (bit number + 1) if X, or -X, is a constant power of 2.  */

int
const_int_pow2_p (rtx x)
{
  if (GET_CODE (x) == CONST_INT)
    {
      HOST_WIDE_INT d = INTVAL (x);
      HOST_WIDE_INT abs_d = (d >= 0) ? d : -d;
      return exact_log2 (abs_d) + 1;
    }
  return 0;
}

const char *
output_reload_inhi (rtx insn ATTRIBUTE_UNUSED, rtx *operands, int *len)
{
  int tmp;
  if (!len)
    len = &tmp;
      
  if (GET_CODE (operands[1]) == CONST_INT)
    {
      int val = INTVAL (operands[1]);
      if ((val & 0xff) == 0)
	{
	  *len = 3;
	  return (AS2 (mov,%A0,__zero_reg__) CR_TAB
		  AS2 (ldi,%2,hi8(%1))       CR_TAB
		  AS2 (mov,%B0,%2));
	}
      else if ((val & 0xff00) == 0)
	{
	  *len = 3;
	  return (AS2 (ldi,%2,lo8(%1)) CR_TAB
		  AS2 (mov,%A0,%2)     CR_TAB
		  AS2 (mov,%B0,__zero_reg__));
	}
      else if ((val & 0xff) == ((val & 0xff00) >> 8))
	{
	  *len = 3;
	  return (AS2 (ldi,%2,lo8(%1)) CR_TAB
		  AS2 (mov,%A0,%2)     CR_TAB
		  AS2 (mov,%B0,%2));
	}
    }
  *len = 4;
  return (AS2 (ldi,%2,lo8(%1)) CR_TAB
	  AS2 (mov,%A0,%2)     CR_TAB
	  AS2 (ldi,%2,hi8(%1)) CR_TAB
	  AS2 (mov,%B0,%2));
}


const char *
output_reload_insisf (rtx insn ATTRIBUTE_UNUSED, rtx *operands, int *len)
{
  rtx src = operands[1];
  int cnst = (GET_CODE (src) == CONST_INT);

  if (len)
    {
      if (cnst)
	*len = 4 + ((INTVAL (src) & 0xff) != 0)
		+ ((INTVAL (src) & 0xff00) != 0)
		+ ((INTVAL (src) & 0xff0000) != 0)
		+ ((INTVAL (src) & 0xff000000) != 0);
      else
	*len = 8;

      return "";
    }

  if (cnst && ((INTVAL (src) & 0xff) == 0))
    output_asm_insn (AS2 (mov, %A0, __zero_reg__), operands);
  else
    {
      output_asm_insn (AS2 (ldi, %2, lo8(%1)), operands);
      output_asm_insn (AS2 (mov, %A0, %2), operands);
    }
  if (cnst && ((INTVAL (src) & 0xff00) == 0))
    output_asm_insn (AS2 (mov, %B0, __zero_reg__), operands);
  else
    {
      output_asm_insn (AS2 (ldi, %2, hi8(%1)), operands);
      output_asm_insn (AS2 (mov, %B0, %2), operands);
    }
  if (cnst && ((INTVAL (src) & 0xff0000) == 0))
    output_asm_insn (AS2 (mov, %C0, __zero_reg__), operands);
  else
    {
      output_asm_insn (AS2 (ldi, %2, hlo8(%1)), operands);
      output_asm_insn (AS2 (mov, %C0, %2), operands);
    }
  if (cnst && ((INTVAL (src) & 0xff000000) == 0))
    output_asm_insn (AS2 (mov, %D0, __zero_reg__), operands);
  else
    {
      output_asm_insn (AS2 (ldi, %2, hhi8(%1)), operands);
      output_asm_insn (AS2 (mov, %D0, %2), operands);
    }
  return "";
}

void
avr_output_bld (rtx operands[], int bit_nr)
{
  static char s[] = "bld %A0,0";

  s[5] = 'A' + (bit_nr >> 3);
  s[8] = '0' + (bit_nr & 7);
  output_asm_insn (s, operands);
}

void
avr_output_addr_vec_elt (FILE *stream, int value)
{
  switch_to_section (progmem_section);
  if (AVR_MEGA)
    fprintf (stream, "\t.word pm(.L%d)\n", value);
  else
    fprintf (stream, "\trjmp .L%d\n", value);

  jump_tables_size++;
}

/* Returns 1 if SCRATCH are safe to be allocated as a scratch
   registers (for a define_peephole2) in the current function.  */

int
avr_peep2_scratch_safe (rtx scratch)
{
  if ((interrupt_function_p (current_function_decl)
       || signal_function_p (current_function_decl))
      && leaf_function_p ())
    {
      int first_reg = true_regnum (scratch);
      int last_reg = first_reg + GET_MODE_SIZE (GET_MODE (scratch)) - 1;
      int reg;

      for (reg = first_reg; reg <= last_reg; reg++)
	{
	  if (!regs_ever_live[reg])
	    return 0;
	}
    }
  return 1;
}

/* Output a branch that tests a single bit of a register (QI, HI or SImode)
   or memory location in the I/O space (QImode only).

   Operand 0: comparison operator (must be EQ or NE, compare bit to zero).
   Operand 1: register operand to test, or CONST_INT memory address.
   Operand 2: bit number (for QImode operand) or mask (HImode, SImode).
   Operand 3: label to jump to if the test is true.  */

const char *
avr_out_sbxx_branch (rtx insn, rtx operands[])
{
  enum rtx_code comp = GET_CODE (operands[0]);
  int long_jump = (get_attr_length (insn) >= 4);
  int reverse = long_jump || jump_over_one_insn_p (insn, operands[3]);

  if (comp == GE)
    comp = EQ;
  else if (comp == LT)
    comp = NE;

  if (reverse)
    comp = reverse_condition (comp);

  if (GET_CODE (operands[1]) == CONST_INT)
    {
      if (INTVAL (operands[1]) < 0x40)
	{
	  if (comp == EQ)
	    output_asm_insn (AS2 (sbis,%1-0x20,%2), operands);
	  else
	    output_asm_insn (AS2 (sbic,%1-0x20,%2), operands);
	}
      else
	{
	  output_asm_insn (AS2 (in,__tmp_reg__,%1-0x20), operands);
	  if (comp == EQ)
	    output_asm_insn (AS2 (sbrs,__tmp_reg__,%2), operands);
	  else
	    output_asm_insn (AS2 (sbrc,__tmp_reg__,%2), operands);
	}
    }
  else  /* GET_CODE (operands[1]) == REG */
    {
      if (GET_MODE (operands[1]) == QImode)
	{
	  if (comp == EQ)
	    output_asm_insn (AS2 (sbrs,%1,%2), operands);
	  else
	    output_asm_insn (AS2 (sbrc,%1,%2), operands);
	}
      else  /* HImode or SImode */
	{
	  static char buf[] = "sbrc %A1,0";
	  int bit_nr = exact_log2 (INTVAL (operands[2])
				   & GET_MODE_MASK (GET_MODE (operands[1])));

	  buf[3] = (comp == EQ) ? 's' : 'c';
	  buf[6] = 'A' + (bit_nr >> 3);
	  buf[9] = '0' + (bit_nr & 7);
	  output_asm_insn (buf, operands);
	}
    }

  if (long_jump)
    return (AS1 (rjmp,.+4) CR_TAB
	    AS1 (jmp,%3));
  if (!reverse)
    return AS1 (rjmp,%3);
  return "";
}

/* Worker function for TARGET_ASM_CONSTRUCTOR.  */

static void
avr_asm_out_ctor (rtx symbol, int priority)
{
  fputs ("\t.global __do_global_ctors\n", asm_out_file);
  default_ctor_section_asm_out_constructor (symbol, priority);
}

/* Worker function for TARGET_ASM_DESTRUCTOR.  */

static void
avr_asm_out_dtor (rtx symbol, int priority)
{
  fputs ("\t.global __do_global_dtors\n", asm_out_file);
  default_dtor_section_asm_out_destructor (symbol, priority);
}

/* Worker function for TARGET_RETURN_IN_MEMORY.  */

static bool
avr_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED)
{
  if (TYPE_MODE (type) == BLKmode)
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      return (size == -1 || size > 8);
    }
  else
    return false;
}

#include "gt-avr.h"
