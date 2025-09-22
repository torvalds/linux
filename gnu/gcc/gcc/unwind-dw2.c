/* DWARF2 exception handling and frame unwind runtime interface routines.
   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combined
   executable.)

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "tconfig.h"
#include "tsystem.h"
#include "coretypes.h"
#include "tm.h"
#include "dwarf2.h"
#include "unwind.h"
#ifdef __USING_SJLJ_EXCEPTIONS__
# define NO_SIZE_OF_ENCODED_VALUE
#endif
#include "unwind-pe.h"
#include "unwind-dw2-fde.h"
#include "gthr.h"
#include "unwind-dw2.h"

#ifndef __USING_SJLJ_EXCEPTIONS__

#ifndef STACK_GROWS_DOWNWARD
#define STACK_GROWS_DOWNWARD 0
#else
#undef STACK_GROWS_DOWNWARD
#define STACK_GROWS_DOWNWARD 1
#endif

/* Dwarf frame registers used for pre gcc 3.0 compiled glibc.  */
#ifndef PRE_GCC3_DWARF_FRAME_REGISTERS
#define PRE_GCC3_DWARF_FRAME_REGISTERS DWARF_FRAME_REGISTERS
#endif

#ifndef DWARF_REG_TO_UNWIND_COLUMN
#define DWARF_REG_TO_UNWIND_COLUMN(REGNO) (REGNO)
#endif

/* This is the register and unwind state for a particular frame.  This
   provides the information necessary to unwind up past a frame and return
   to its caller.  */
struct _Unwind_Context
{
  void *reg[DWARF_FRAME_REGISTERS+1];
  void *cfa;
  void *ra;
  void *lsda;
  struct dwarf_eh_bases bases;
  /* Signal frame context.  */
#define SIGNAL_FRAME_BIT ((~(_Unwind_Word) 0 >> 1) + 1)
  /* Context which has version/args_size/by_value fields.  */
#define EXTENDED_CONTEXT_BIT ((~(_Unwind_Word) 0 >> 2) + 1)
  _Unwind_Word flags;
  /* 0 for now, can be increased when further fields are added to
     struct _Unwind_Context.  */
  _Unwind_Word version;
  _Unwind_Word args_size;
  char by_value[DWARF_FRAME_REGISTERS+1];
};

/* Byte size of every register managed by these routines.  */
static unsigned char dwarf_reg_size_table[DWARF_FRAME_REGISTERS+1];


/* Read unaligned data from the instruction buffer.  */

union unaligned
{
  void *p;
  unsigned u2 __attribute__ ((mode (HI)));
  unsigned u4 __attribute__ ((mode (SI)));
  unsigned u8 __attribute__ ((mode (DI)));
  signed s2 __attribute__ ((mode (HI)));
  signed s4 __attribute__ ((mode (SI)));
  signed s8 __attribute__ ((mode (DI)));
} __attribute__ ((packed));

static void uw_update_context (struct _Unwind_Context *, _Unwind_FrameState *);
static _Unwind_Reason_Code uw_frame_state_for (struct _Unwind_Context *,
					       _Unwind_FrameState *);

static inline void *
read_pointer (const void *p) { const union unaligned *up = p; return up->p; }

static inline int
read_1u (const void *p) { return *(const unsigned char *) p; }

static inline int
read_1s (const void *p) { return *(const signed char *) p; }

static inline int
read_2u (const void *p) { const union unaligned *up = p; return up->u2; }

static inline int
read_2s (const void *p) { const union unaligned *up = p; return up->s2; }

static inline unsigned int
read_4u (const void *p) { const union unaligned *up = p; return up->u4; }

static inline int
read_4s (const void *p) { const union unaligned *up = p; return up->s4; }

static inline unsigned long
read_8u (const void *p) { const union unaligned *up = p; return up->u8; }

static inline unsigned long
read_8s (const void *p) { const union unaligned *up = p; return up->s8; }

static inline _Unwind_Word
_Unwind_IsSignalFrame (struct _Unwind_Context *context)
{
  return (context->flags & SIGNAL_FRAME_BIT) ? 1 : 0;
}

static inline void
_Unwind_SetSignalFrame (struct _Unwind_Context *context, int val)
{
  if (val)
    context->flags |= SIGNAL_FRAME_BIT;
  else
    context->flags &= ~SIGNAL_FRAME_BIT;
}

static inline _Unwind_Word
_Unwind_IsExtendedContext (struct _Unwind_Context *context)
{
  return context->flags & EXTENDED_CONTEXT_BIT;
}

#ifdef __sparc64__

/* Figure out StackGhost cookie.  */
_Unwind_Word uw_get_wcookie(void);

asm(".text\n"
    "uw_get_wcookie:\n"
    "	add  %o7, %g0, %g4\n"
    "	save %sp, -176, %sp\n"
    "	save %sp, -176, %sp\n"
    "	flushw\n"
    "	restore\n"
    "	ldx [%sp + 2047 + 120], %g5\n"
    "	xor %g4, %g5, %i0\n"
    "	ret\n"
    "	 restore\n");
#endif


/* Get the value of register INDEX as saved in CONTEXT.  */

inline _Unwind_Word
_Unwind_GetGR (struct _Unwind_Context *context, int index)
{
  int size;
  void *ptr;

#ifdef DWARF_ZERO_REG
  if (index == DWARF_ZERO_REG)
    return 0;
#endif

  index = DWARF_REG_TO_UNWIND_COLUMN (index);
  gcc_assert (index < (int) sizeof(dwarf_reg_size_table));
  size = dwarf_reg_size_table[index];
  ptr = context->reg[index];

  if (_Unwind_IsExtendedContext (context) && context->by_value[index])
    return (_Unwind_Word) (_Unwind_Internal_Ptr) ptr;

#ifdef __sparc64__
  /* _Unwind_Word and _Unwind_Ptr are the same size on sparc64 */
  _Unwind_Word reg = * (_Unwind_Word *) ptr;
  if (index == 15 || index == 31)
    reg ^= uw_get_wcookie ();
  return reg;
#else
  /* This will segfault if the register hasn't been saved.  */
  if (size == sizeof(_Unwind_Ptr))
    return * (_Unwind_Ptr *) ptr;
  else
    {
      gcc_assert (size == sizeof(_Unwind_Word));
      return * (_Unwind_Word *) ptr;
    }
#endif
}

static inline void *
_Unwind_GetPtr (struct _Unwind_Context *context, int index)
{
  return (void *)(_Unwind_Ptr) _Unwind_GetGR (context, index);
}

/* Get the value of the CFA as saved in CONTEXT.  */

_Unwind_Word
_Unwind_GetCFA (struct _Unwind_Context *context)
{
  return (_Unwind_Ptr) context->cfa;
}

/* Overwrite the saved value for register INDEX in CONTEXT with VAL.  */

inline void
_Unwind_SetGR (struct _Unwind_Context *context, int index, _Unwind_Word val)
{
  int size;
  void *ptr;

  index = DWARF_REG_TO_UNWIND_COLUMN (index);
  gcc_assert (index < (int) sizeof(dwarf_reg_size_table));
  size = dwarf_reg_size_table[index];

  if (_Unwind_IsExtendedContext (context) && context->by_value[index])
    {
      context->reg[index] = (void *) (_Unwind_Internal_Ptr) val;
      return;
    }

  ptr = context->reg[index];

  if (size == sizeof(_Unwind_Ptr))
    * (_Unwind_Ptr *) ptr = val;
  else
    {
      gcc_assert (size == sizeof(_Unwind_Word));
      * (_Unwind_Word *) ptr = val;
    }
}

/* Get the pointer to a register INDEX as saved in CONTEXT.  */

static inline void *
_Unwind_GetGRPtr (struct _Unwind_Context *context, int index)
{
  index = DWARF_REG_TO_UNWIND_COLUMN (index);
  if (_Unwind_IsExtendedContext (context) && context->by_value[index])
    return &context->reg[index];
  return context->reg[index];
}

/* Set the pointer to a register INDEX as saved in CONTEXT.  */

static inline void
_Unwind_SetGRPtr (struct _Unwind_Context *context, int index, void *p)
{
  index = DWARF_REG_TO_UNWIND_COLUMN (index);
  if (_Unwind_IsExtendedContext (context))
    context->by_value[index] = 0;
  context->reg[index] = p;
}

/* Overwrite the saved value for register INDEX in CONTEXT with VAL.  */

static inline void
_Unwind_SetGRValue (struct _Unwind_Context *context, int index,
		    _Unwind_Word val)
{
  index = DWARF_REG_TO_UNWIND_COLUMN (index);
  gcc_assert (index < (int) sizeof(dwarf_reg_size_table));
  gcc_assert (dwarf_reg_size_table[index] == sizeof (_Unwind_Ptr));

  context->by_value[index] = 1;
  context->reg[index] = (void *) (_Unwind_Internal_Ptr) val;
}

/* Return nonzero if register INDEX is stored by value rather than
   by reference.  */

static inline int
_Unwind_GRByValue (struct _Unwind_Context *context, int index)
{
  index = DWARF_REG_TO_UNWIND_COLUMN (index);
  return context->by_value[index];
}

/* Retrieve the return address for CONTEXT.  */

inline _Unwind_Ptr
_Unwind_GetIP (struct _Unwind_Context *context)
{
  return (_Unwind_Ptr) context->ra;
}

/* Retrieve the return address and flag whether that IP is before
   or after first not yet fully executed instruction.  */

inline _Unwind_Ptr
_Unwind_GetIPInfo (struct _Unwind_Context *context, int *ip_before_insn)
{
  *ip_before_insn = _Unwind_IsSignalFrame (context);
  return (_Unwind_Ptr) context->ra;
}

/* Overwrite the return address for CONTEXT with VAL.  */

inline void
_Unwind_SetIP (struct _Unwind_Context *context, _Unwind_Ptr val)
{
  context->ra = (void *) val;
}

void *
_Unwind_GetLanguageSpecificData (struct _Unwind_Context *context)
{
  return context->lsda;
}

_Unwind_Ptr
_Unwind_GetRegionStart (struct _Unwind_Context *context)
{
  return (_Unwind_Ptr) context->bases.func;
}

void *
_Unwind_FindEnclosingFunction (void *pc)
{
  struct dwarf_eh_bases bases;
  const struct dwarf_fde *fde = _Unwind_Find_FDE (pc-1, &bases);
  if (fde)
    return bases.func;
  else
    return NULL;
}

#ifndef __ia64__
_Unwind_Ptr
_Unwind_GetDataRelBase (struct _Unwind_Context *context)
{
  return (_Unwind_Ptr) context->bases.dbase;
}

_Unwind_Ptr
_Unwind_GetTextRelBase (struct _Unwind_Context *context)
{
  return (_Unwind_Ptr) context->bases.tbase;
}
#endif

#ifdef MD_UNWIND_SUPPORT
#include MD_UNWIND_SUPPORT
#endif

/* Extract any interesting information from the CIE for the translation
   unit F belongs to.  Return a pointer to the byte after the augmentation,
   or NULL if we encountered an undecipherable augmentation.  */

static const unsigned char *
extract_cie_info (const struct dwarf_cie *cie, struct _Unwind_Context *context,
		  _Unwind_FrameState *fs)
{
  const unsigned char *aug = cie->augmentation;
  const unsigned char *p = aug + strlen ((const char *)aug) + 1;
  const unsigned char *ret = NULL;
  _Unwind_Word utmp;

  /* g++ v2 "eh" has pointer immediately following augmentation string,
     so it must be handled first.  */
  if (aug[0] == 'e' && aug[1] == 'h')
    {
      fs->eh_ptr = read_pointer (p);
      p += sizeof (void *);
      aug += 2;
    }

  /* Immediately following the augmentation are the code and
     data alignment and return address column.  */
  p = read_uleb128 (p, &fs->code_align);
  p = read_sleb128 (p, &fs->data_align);
  if (cie->version == 1)
    fs->retaddr_column = *p++;
  else
    p = read_uleb128 (p, &fs->retaddr_column);
  fs->lsda_encoding = DW_EH_PE_omit;

  /* If the augmentation starts with 'z', then a uleb128 immediately
     follows containing the length of the augmentation field following
     the size.  */
  if (*aug == 'z')
    {
      p = read_uleb128 (p, &utmp);
      ret = p + utmp;

      fs->saw_z = 1;
      ++aug;
    }

  /* Iterate over recognized augmentation subsequences.  */
  while (*aug != '\0')
    {
      /* "L" indicates a byte showing how the LSDA pointer is encoded.  */
      if (aug[0] == 'L')
	{
	  fs->lsda_encoding = *p++;
	  aug += 1;
	}

      /* "R" indicates a byte indicating how FDE addresses are encoded.  */
      else if (aug[0] == 'R')
	{
	  fs->fde_encoding = *p++;
	  aug += 1;
	}

      /* "P" indicates a personality routine in the CIE augmentation.  */
      else if (aug[0] == 'P')
	{
	  _Unwind_Ptr personality;
	  
	  p = read_encoded_value (context, *p, p + 1, &personality);
	  fs->personality = (_Unwind_Personality_Fn) personality;
	  aug += 1;
	}

      /* "S" indicates a signal frame.  */
      else if (aug[0] == 'S')
	{
	  fs->signal_frame = 1;
	  aug += 1;
	}

      /* Otherwise we have an unknown augmentation string.
	 Bail unless we saw a 'z' prefix.  */
      else
	return ret;
    }

  return ret ? ret : p;
}


/* Decode a DW_OP stack program.  Return the top of stack.  Push INITIAL
   onto the stack to start.  */

static _Unwind_Word
execute_stack_op (const unsigned char *op_ptr, const unsigned char *op_end,
		  struct _Unwind_Context *context, _Unwind_Word initial)
{
  _Unwind_Word stack[64];	/* ??? Assume this is enough.  */
  int stack_elt;

  stack[0] = initial;
  stack_elt = 1;

  while (op_ptr < op_end)
    {
      enum dwarf_location_atom op = *op_ptr++;
      _Unwind_Word result, reg, utmp;
      _Unwind_Sword offset, stmp;

      switch (op)
	{
	case DW_OP_lit0:
	case DW_OP_lit1:
	case DW_OP_lit2:
	case DW_OP_lit3:
	case DW_OP_lit4:
	case DW_OP_lit5:
	case DW_OP_lit6:
	case DW_OP_lit7:
	case DW_OP_lit8:
	case DW_OP_lit9:
	case DW_OP_lit10:
	case DW_OP_lit11:
	case DW_OP_lit12:
	case DW_OP_lit13:
	case DW_OP_lit14:
	case DW_OP_lit15:
	case DW_OP_lit16:
	case DW_OP_lit17:
	case DW_OP_lit18:
	case DW_OP_lit19:
	case DW_OP_lit20:
	case DW_OP_lit21:
	case DW_OP_lit22:
	case DW_OP_lit23:
	case DW_OP_lit24:
	case DW_OP_lit25:
	case DW_OP_lit26:
	case DW_OP_lit27:
	case DW_OP_lit28:
	case DW_OP_lit29:
	case DW_OP_lit30:
	case DW_OP_lit31:
	  result = op - DW_OP_lit0;
	  break;

	case DW_OP_addr:
	  result = (_Unwind_Word) (_Unwind_Ptr) read_pointer (op_ptr);
	  op_ptr += sizeof (void *);
	  break;

	case DW_OP_const1u:
	  result = read_1u (op_ptr);
	  op_ptr += 1;
	  break;
	case DW_OP_const1s:
	  result = read_1s (op_ptr);
	  op_ptr += 1;
	  break;
	case DW_OP_const2u:
	  result = read_2u (op_ptr);
	  op_ptr += 2;
	  break;
	case DW_OP_const2s:
	  result = read_2s (op_ptr);
	  op_ptr += 2;
	  break;
	case DW_OP_const4u:
	  result = read_4u (op_ptr);
	  op_ptr += 4;
	  break;
	case DW_OP_const4s:
	  result = read_4s (op_ptr);
	  op_ptr += 4;
	  break;
	case DW_OP_const8u:
	  result = read_8u (op_ptr);
	  op_ptr += 8;
	  break;
	case DW_OP_const8s:
	  result = read_8s (op_ptr);
	  op_ptr += 8;
	  break;
	case DW_OP_constu:
	  op_ptr = read_uleb128 (op_ptr, &result);
	  break;
	case DW_OP_consts:
	  op_ptr = read_sleb128 (op_ptr, &stmp);
	  result = stmp;
	  break;

	case DW_OP_reg0:
	case DW_OP_reg1:
	case DW_OP_reg2:
	case DW_OP_reg3:
	case DW_OP_reg4:
	case DW_OP_reg5:
	case DW_OP_reg6:
	case DW_OP_reg7:
	case DW_OP_reg8:
	case DW_OP_reg9:
	case DW_OP_reg10:
	case DW_OP_reg11:
	case DW_OP_reg12:
	case DW_OP_reg13:
	case DW_OP_reg14:
	case DW_OP_reg15:
	case DW_OP_reg16:
	case DW_OP_reg17:
	case DW_OP_reg18:
	case DW_OP_reg19:
	case DW_OP_reg20:
	case DW_OP_reg21:
	case DW_OP_reg22:
	case DW_OP_reg23:
	case DW_OP_reg24:
	case DW_OP_reg25:
	case DW_OP_reg26:
	case DW_OP_reg27:
	case DW_OP_reg28:
	case DW_OP_reg29:
	case DW_OP_reg30:
	case DW_OP_reg31:
	  result = _Unwind_GetGR (context, op - DW_OP_reg0);
	  break;
	case DW_OP_regx:
	  op_ptr = read_uleb128 (op_ptr, &reg);
	  result = _Unwind_GetGR (context, reg);
	  break;

	case DW_OP_breg0:
	case DW_OP_breg1:
	case DW_OP_breg2:
	case DW_OP_breg3:
	case DW_OP_breg4:
	case DW_OP_breg5:
	case DW_OP_breg6:
	case DW_OP_breg7:
	case DW_OP_breg8:
	case DW_OP_breg9:
	case DW_OP_breg10:
	case DW_OP_breg11:
	case DW_OP_breg12:
	case DW_OP_breg13:
	case DW_OP_breg14:
	case DW_OP_breg15:
	case DW_OP_breg16:
	case DW_OP_breg17:
	case DW_OP_breg18:
	case DW_OP_breg19:
	case DW_OP_breg20:
	case DW_OP_breg21:
	case DW_OP_breg22:
	case DW_OP_breg23:
	case DW_OP_breg24:
	case DW_OP_breg25:
	case DW_OP_breg26:
	case DW_OP_breg27:
	case DW_OP_breg28:
	case DW_OP_breg29:
	case DW_OP_breg30:
	case DW_OP_breg31:
	  op_ptr = read_sleb128 (op_ptr, &offset);
	  result = _Unwind_GetGR (context, op - DW_OP_breg0) + offset;
	  break;
	case DW_OP_bregx:
	  op_ptr = read_uleb128 (op_ptr, &reg);
	  op_ptr = read_sleb128 (op_ptr, &offset);
	  result = _Unwind_GetGR (context, reg) + offset;
	  break;

	case DW_OP_dup:
	  gcc_assert (stack_elt);
	  result = stack[stack_elt - 1];
	  break;

	case DW_OP_drop:
	  gcc_assert (stack_elt);
	  stack_elt -= 1;
	  goto no_push;

	case DW_OP_pick:
	  offset = *op_ptr++;
	  gcc_assert (offset < stack_elt - 1);
	  result = stack[stack_elt - 1 - offset];
	  break;

	case DW_OP_over:
	  gcc_assert (stack_elt >= 2);
	  result = stack[stack_elt - 2];
	  break;

	case DW_OP_swap:
	  {
	    _Unwind_Word t;
	    gcc_assert (stack_elt >= 2);
	    t = stack[stack_elt - 1];
	    stack[stack_elt - 1] = stack[stack_elt - 2];
	    stack[stack_elt - 2] = t;
	    goto no_push;
	  }

	case DW_OP_rot:
	  {
	    _Unwind_Word t1, t2, t3;

	    gcc_assert (stack_elt >= 3);
	    t1 = stack[stack_elt - 1];
	    t2 = stack[stack_elt - 2];
	    t3 = stack[stack_elt - 3];
	    stack[stack_elt - 1] = t2;
	    stack[stack_elt - 2] = t3;
	    stack[stack_elt - 3] = t1;
	    goto no_push;
	  }

	case DW_OP_deref:
	case DW_OP_deref_size:
	case DW_OP_abs:
	case DW_OP_neg:
	case DW_OP_not:
	case DW_OP_plus_uconst:
	  /* Unary operations.  */
	  gcc_assert (stack_elt);
	  stack_elt -= 1;
	  
	  result = stack[stack_elt];

	  switch (op)
	    {
	    case DW_OP_deref:
	      {
		void *ptr = (void *) (_Unwind_Ptr) result;
		result = (_Unwind_Ptr) read_pointer (ptr);
	      }
	      break;

	    case DW_OP_deref_size:
	      {
		void *ptr = (void *) (_Unwind_Ptr) result;
		switch (*op_ptr++)
		  {
		  case 1:
		    result = read_1u (ptr);
		    break;
		  case 2:
		    result = read_2u (ptr);
		    break;
		  case 4:
		    result = read_4u (ptr);
		    break;
		  case 8:
		    result = read_8u (ptr);
		    break;
		  default:
		    gcc_unreachable ();
		  }
	      }
	      break;

	    case DW_OP_abs:
	      if ((_Unwind_Sword) result < 0)
		result = -result;
	      break;
	    case DW_OP_neg:
	      result = -result;
	      break;
	    case DW_OP_not:
	      result = ~result;
	      break;
	    case DW_OP_plus_uconst:
	      op_ptr = read_uleb128 (op_ptr, &utmp);
	      result += utmp;
	      break;

	    default:
	      gcc_unreachable ();
	    }
	  break;

	case DW_OP_and:
	case DW_OP_div:
	case DW_OP_minus:
	case DW_OP_mod:
	case DW_OP_mul:
	case DW_OP_or:
	case DW_OP_plus:
	case DW_OP_shl:
	case DW_OP_shr:
	case DW_OP_shra:
	case DW_OP_xor:
	case DW_OP_le:
	case DW_OP_ge:
	case DW_OP_eq:
	case DW_OP_lt:
	case DW_OP_gt:
	case DW_OP_ne:
	  {
	    /* Binary operations.  */
	    _Unwind_Word first, second;
	    gcc_assert (stack_elt >= 2);
	    stack_elt -= 2;
	    
	    second = stack[stack_elt];
	    first = stack[stack_elt + 1];

	    switch (op)
	      {
	      case DW_OP_and:
		result = second & first;
		break;
	      case DW_OP_div:
		result = (_Unwind_Sword) second / (_Unwind_Sword) first;
		break;
	      case DW_OP_minus:
		result = second - first;
		break;
	      case DW_OP_mod:
		result = (_Unwind_Sword) second % (_Unwind_Sword) first;
		break;
	      case DW_OP_mul:
		result = second * first;
		break;
	      case DW_OP_or:
		result = second | first;
		break;
	      case DW_OP_plus:
		result = second + first;
		break;
	      case DW_OP_shl:
		result = second << first;
		break;
	      case DW_OP_shr:
		result = second >> first;
		break;
	      case DW_OP_shra:
		result = (_Unwind_Sword) second >> first;
		break;
	      case DW_OP_xor:
		result = second ^ first;
		break;
	      case DW_OP_le:
		result = (_Unwind_Sword) first <= (_Unwind_Sword) second;
		break;
	      case DW_OP_ge:
		result = (_Unwind_Sword) first >= (_Unwind_Sword) second;
		break;
	      case DW_OP_eq:
		result = (_Unwind_Sword) first == (_Unwind_Sword) second;
		break;
	      case DW_OP_lt:
		result = (_Unwind_Sword) first < (_Unwind_Sword) second;
		break;
	      case DW_OP_gt:
		result = (_Unwind_Sword) first > (_Unwind_Sword) second;
		break;
	      case DW_OP_ne:
		result = (_Unwind_Sword) first != (_Unwind_Sword) second;
		break;

	      default:
		gcc_unreachable ();
	      }
	  }
	  break;

	case DW_OP_skip:
	  offset = read_2s (op_ptr);
	  op_ptr += 2;
	  op_ptr += offset;
	  goto no_push;

	case DW_OP_bra:
	  gcc_assert (stack_elt);
	  stack_elt -= 1;
	  
	  offset = read_2s (op_ptr);
	  op_ptr += 2;
	  if (stack[stack_elt] != 0)
	    op_ptr += offset;
	  goto no_push;

	case DW_OP_nop:
	  goto no_push;

	default:
	  gcc_unreachable ();
	}

      /* Most things push a result value.  */
      gcc_assert ((size_t) stack_elt < sizeof(stack)/sizeof(*stack));
      stack[stack_elt++] = result;
    no_push:;
    }

  /* We were executing this program to get a value.  It should be
     at top of stack.  */
  gcc_assert (stack_elt);
  stack_elt -= 1;
  return stack[stack_elt];
}


/* Decode DWARF 2 call frame information. Takes pointers the
   instruction sequence to decode, current register information and
   CIE info, and the PC range to evaluate.  */

static void
execute_cfa_program (const unsigned char *insn_ptr,
		     const unsigned char *insn_end,
		     struct _Unwind_Context *context,
		     _Unwind_FrameState *fs)
{
  struct frame_state_reg_info *unused_rs = NULL;

  /* Don't allow remember/restore between CIE and FDE programs.  */
  fs->regs.prev = NULL;

  /* The comparison with the return address uses < rather than <= because
     we are only interested in the effects of code before the call; for a
     noreturn function, the return address may point to unrelated code with
     a different stack configuration that we are not interested in.  We
     assume that the call itself is unwind info-neutral; if not, or if
     there are delay instructions that adjust the stack, these must be
     reflected at the point immediately before the call insn.
     In signal frames, return address is after last completed instruction,
     so we add 1 to return address to make the comparison <=.  */
  while (insn_ptr < insn_end
	 && fs->pc < context->ra + _Unwind_IsSignalFrame (context))
    {
      unsigned char insn = *insn_ptr++;
      _Unwind_Word reg, utmp;
      _Unwind_Sword offset, stmp;

      if ((insn & 0xc0) == DW_CFA_advance_loc)
	fs->pc += (insn & 0x3f) * fs->code_align;
      else if ((insn & 0xc0) == DW_CFA_offset)
	{
	  reg = insn & 0x3f;
	  insn_ptr = read_uleb128 (insn_ptr, &utmp);
	  offset = (_Unwind_Sword) utmp * fs->data_align;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].how
	    = REG_SAVED_OFFSET;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].loc.offset = offset;
	}
      else if ((insn & 0xc0) == DW_CFA_restore)
	{
	  reg = insn & 0x3f;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].how = REG_UNSAVED;
	}
      else switch (insn)
	{
	case DW_CFA_set_loc:
	  {
	    _Unwind_Ptr pc;
	    
	    insn_ptr = read_encoded_value (context, fs->fde_encoding,
					   insn_ptr, &pc);
	    fs->pc = (void *) pc;
	  }
	  break;

	case DW_CFA_advance_loc1:
	  fs->pc += read_1u (insn_ptr) * fs->code_align;
	  insn_ptr += 1;
	  break;
	case DW_CFA_advance_loc2:
	  fs->pc += read_2u (insn_ptr) * fs->code_align;
	  insn_ptr += 2;
	  break;
	case DW_CFA_advance_loc4:
	  fs->pc += read_4u (insn_ptr) * fs->code_align;
	  insn_ptr += 4;
	  break;

	case DW_CFA_offset_extended:
	  insn_ptr = read_uleb128 (insn_ptr, &reg);
	  insn_ptr = read_uleb128 (insn_ptr, &utmp);
	  offset = (_Unwind_Sword) utmp * fs->data_align;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].how
	    = REG_SAVED_OFFSET;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].loc.offset = offset;
	  break;

	case DW_CFA_restore_extended:
	  insn_ptr = read_uleb128 (insn_ptr, &reg);
	  /* FIXME, this is wrong; the CIE might have said that the
	     register was saved somewhere.  */
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN(reg)].how = REG_UNSAVED;
	  break;

	case DW_CFA_undefined:
	case DW_CFA_same_value:
	  insn_ptr = read_uleb128 (insn_ptr, &reg);
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN(reg)].how = REG_UNSAVED;
	  break;

	case DW_CFA_nop:
	  break;

	case DW_CFA_register:
	  {
	    _Unwind_Word reg2;
	    insn_ptr = read_uleb128 (insn_ptr, &reg);
	    insn_ptr = read_uleb128 (insn_ptr, &reg2);
	    fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].how = REG_SAVED_REG;
	    fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].loc.reg = reg2;
	  }
	  break;

	case DW_CFA_remember_state:
	  {
	    struct frame_state_reg_info *new_rs;
	    if (unused_rs)
	      {
		new_rs = unused_rs;
		unused_rs = unused_rs->prev;
	      }
	    else
	      new_rs = alloca (sizeof (struct frame_state_reg_info));

	    *new_rs = fs->regs;
	    fs->regs.prev = new_rs;
	  }
	  break;

	case DW_CFA_restore_state:
	  {
	    struct frame_state_reg_info *old_rs = fs->regs.prev;
	    fs->regs = *old_rs;
	    old_rs->prev = unused_rs;
	    unused_rs = old_rs;
	  }
	  break;

	case DW_CFA_def_cfa:
	  insn_ptr = read_uleb128 (insn_ptr, &fs->cfa_reg);
	  insn_ptr = read_uleb128 (insn_ptr, &utmp);
	  fs->cfa_offset = utmp;
	  fs->cfa_how = CFA_REG_OFFSET;
	  break;

	case DW_CFA_def_cfa_register:
	  insn_ptr = read_uleb128 (insn_ptr, &fs->cfa_reg);
	  fs->cfa_how = CFA_REG_OFFSET;
	  break;

	case DW_CFA_def_cfa_offset:
	  insn_ptr = read_uleb128 (insn_ptr, &utmp);
	  fs->cfa_offset = utmp;
	  /* cfa_how deliberately not set.  */
	  break;

	case DW_CFA_def_cfa_expression:
	  fs->cfa_exp = insn_ptr;
	  fs->cfa_how = CFA_EXP;
	  insn_ptr = read_uleb128 (insn_ptr, &utmp);
	  insn_ptr += utmp;
	  break;

	case DW_CFA_expression:
	  insn_ptr = read_uleb128 (insn_ptr, &reg);
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].how = REG_SAVED_EXP;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].loc.exp = insn_ptr;
	  insn_ptr = read_uleb128 (insn_ptr, &utmp);
	  insn_ptr += utmp;
	  break;

	  /* Dwarf3.  */
	case DW_CFA_offset_extended_sf:
	  insn_ptr = read_uleb128 (insn_ptr, &reg);
	  insn_ptr = read_sleb128 (insn_ptr, &stmp);
	  offset = stmp * fs->data_align;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].how
	    = REG_SAVED_OFFSET;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].loc.offset = offset;
	  break;

	case DW_CFA_def_cfa_sf:
	  insn_ptr = read_uleb128 (insn_ptr, &fs->cfa_reg);
	  insn_ptr = read_sleb128 (insn_ptr, &fs->cfa_offset);
	  fs->cfa_how = CFA_REG_OFFSET;
	  fs->cfa_offset *= fs->data_align;
	  break;

	case DW_CFA_def_cfa_offset_sf:
	  insn_ptr = read_sleb128 (insn_ptr, &fs->cfa_offset);
	  fs->cfa_offset *= fs->data_align;
	  /* cfa_how deliberately not set.  */
	  break;

	case DW_CFA_val_offset:
	  insn_ptr = read_uleb128 (insn_ptr, &reg);
	  insn_ptr = read_uleb128 (insn_ptr, &utmp);
	  offset = (_Unwind_Sword) utmp * fs->data_align;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].how
	    = REG_SAVED_VAL_OFFSET;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].loc.offset = offset;
	  break;

	case DW_CFA_val_offset_sf:
	  insn_ptr = read_uleb128 (insn_ptr, &reg);
	  insn_ptr = read_sleb128 (insn_ptr, &stmp);
	  offset = stmp * fs->data_align;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].how
	    = REG_SAVED_VAL_OFFSET;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].loc.offset = offset;
	  break;

	case DW_CFA_val_expression:
	  insn_ptr = read_uleb128 (insn_ptr, &reg);
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].how
	    = REG_SAVED_VAL_EXP;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].loc.exp = insn_ptr;
	  insn_ptr = read_uleb128 (insn_ptr, &utmp);
	  insn_ptr += utmp;
	  break;

	case DW_CFA_GNU_window_save:
	  /* ??? Hardcoded for SPARC register window configuration.  */
	  for (reg = 16; reg < 32; ++reg)
	    {
	      fs->regs.reg[reg].how = REG_SAVED_OFFSET;
	      fs->regs.reg[reg].loc.offset = (reg - 16) * sizeof (void *);
	    }
	  break;

	case DW_CFA_GNU_args_size:
	  insn_ptr = read_uleb128 (insn_ptr, &context->args_size);
	  break;

	case DW_CFA_GNU_negative_offset_extended:
	  /* Obsoleted by DW_CFA_offset_extended_sf, but used by
	     older PowerPC code.  */
	  insn_ptr = read_uleb128 (insn_ptr, &reg);
	  insn_ptr = read_uleb128 (insn_ptr, &utmp);
	  offset = (_Unwind_Word) utmp * fs->data_align;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].how
	    = REG_SAVED_OFFSET;
	  fs->regs.reg[DWARF_REG_TO_UNWIND_COLUMN (reg)].loc.offset = -offset;
	  break;

	default:
	  gcc_unreachable ();
	}
    }
}

/* Given the _Unwind_Context CONTEXT for a stack frame, look up the FDE for
   its caller and decode it into FS.  This function also sets the
   args_size and lsda members of CONTEXT, as they are really information
   about the caller's frame.  */

static _Unwind_Reason_Code
uw_frame_state_for (struct _Unwind_Context *context, _Unwind_FrameState *fs)
{
  const struct dwarf_fde *fde;
  const struct dwarf_cie *cie;
  const unsigned char *aug, *insn, *end;

  memset (fs, 0, sizeof (*fs));
  context->args_size = 0;
  context->lsda = 0;

  if (context->ra == 0)
    return _URC_END_OF_STACK;

  fde = _Unwind_Find_FDE (context->ra + _Unwind_IsSignalFrame (context) - 1,
			  &context->bases);
  if (fde == NULL)
    {
#ifdef MD_FALLBACK_FRAME_STATE_FOR
      /* Couldn't find frame unwind info for this function.  Try a
	 target-specific fallback mechanism.  This will necessarily
	 not provide a personality routine or LSDA.  */
      return MD_FALLBACK_FRAME_STATE_FOR (context, fs);
#else
      return _URC_END_OF_STACK;
#endif
    }

  fs->pc = context->bases.func;

  cie = get_cie (fde);
  insn = extract_cie_info (cie, context, fs);
  if (insn == NULL)
    /* CIE contained unknown augmentation.  */
    return _URC_FATAL_PHASE1_ERROR;

  /* First decode all the insns in the CIE.  */
  end = (unsigned char *) next_fde ((struct dwarf_fde *) cie);
  execute_cfa_program (insn, end, context, fs);

  /* Locate augmentation for the fde.  */
  aug = (unsigned char *) fde + sizeof (*fde);
  aug += 2 * size_of_encoded_value (fs->fde_encoding);
  insn = NULL;
  if (fs->saw_z)
    {
      _Unwind_Word i;
      aug = read_uleb128 (aug, &i);
      insn = aug + i;
    }
  if (fs->lsda_encoding != DW_EH_PE_omit)
    {
      _Unwind_Ptr lsda;
      
      aug = read_encoded_value (context, fs->lsda_encoding, aug, &lsda);
      context->lsda = (void *) lsda;
    }

  /* Then the insns in the FDE up to our target PC.  */
  if (insn == NULL)
    insn = aug;
  end = (unsigned char *) next_fde (fde);
  execute_cfa_program (insn, end, context, fs);

  return _URC_NO_REASON;
}

typedef struct frame_state
{
  void *cfa;
  void *eh_ptr;
  long cfa_offset;
  long args_size;
  long reg_or_offset[PRE_GCC3_DWARF_FRAME_REGISTERS+1];
  unsigned short cfa_reg;
  unsigned short retaddr_column;
  char saved[PRE_GCC3_DWARF_FRAME_REGISTERS+1];
} frame_state;

struct frame_state * __frame_state_for (void *, struct frame_state *);

/* Called from pre-G++ 3.0 __throw to find the registers to restore for
   a given PC_TARGET.  The caller should allocate a local variable of
   `struct frame_state' and pass its address to STATE_IN.  */

struct frame_state *
__frame_state_for (void *pc_target, struct frame_state *state_in)
{
  struct _Unwind_Context context;
  _Unwind_FrameState fs;
  int reg;

  memset (&context, 0, sizeof (struct _Unwind_Context));
  context.flags = EXTENDED_CONTEXT_BIT;
  context.ra = pc_target + 1;

  if (uw_frame_state_for (&context, &fs) != _URC_NO_REASON)
    return 0;

  /* We have no way to pass a location expression for the CFA to our
     caller.  It wouldn't understand it anyway.  */
  if (fs.cfa_how == CFA_EXP)
    return 0;

  for (reg = 0; reg < PRE_GCC3_DWARF_FRAME_REGISTERS + 1; reg++)
    {
      state_in->saved[reg] = fs.regs.reg[reg].how;
      switch (state_in->saved[reg])
	{
	case REG_SAVED_REG:
	  state_in->reg_or_offset[reg] = fs.regs.reg[reg].loc.reg;
	  break;
	case REG_SAVED_OFFSET:
	  state_in->reg_or_offset[reg] = fs.regs.reg[reg].loc.offset;
	  break;
	default:
	  state_in->reg_or_offset[reg] = 0;
	  break;
	}
    }

  state_in->cfa_offset = fs.cfa_offset;
  state_in->cfa_reg = fs.cfa_reg;
  state_in->retaddr_column = fs.retaddr_column;
  state_in->args_size = context.args_size;
  state_in->eh_ptr = fs.eh_ptr;

  return state_in;
}

typedef union { _Unwind_Ptr ptr; _Unwind_Word word; } _Unwind_SpTmp;

static inline void
_Unwind_SetSpColumn (struct _Unwind_Context *context, void *cfa,
		     _Unwind_SpTmp *tmp_sp)
{
  int size = dwarf_reg_size_table[__builtin_dwarf_sp_column ()];
  
  if (size == sizeof(_Unwind_Ptr))
    tmp_sp->ptr = (_Unwind_Ptr) cfa;
  else
    {
      gcc_assert (size == sizeof(_Unwind_Word));
      tmp_sp->word = (_Unwind_Ptr) cfa;
    }
  _Unwind_SetGRPtr (context, __builtin_dwarf_sp_column (), tmp_sp);
}

static void
uw_update_context_1 (struct _Unwind_Context *context, _Unwind_FrameState *fs)
{
  struct _Unwind_Context orig_context = *context;
  void *cfa;
  long i;

#ifdef EH_RETURN_STACKADJ_RTX
  /* Special handling here: Many machines do not use a frame pointer,
     and track the CFA only through offsets from the stack pointer from
     one frame to the next.  In this case, the stack pointer is never
     stored, so it has no saved address in the context.  What we do
     have is the CFA from the previous stack frame.

     In very special situations (such as unwind info for signal return),
     there may be location expressions that use the stack pointer as well.

     Do this conditionally for one frame.  This allows the unwind info
     for one frame to save a copy of the stack pointer from the previous
     frame, and be able to use much easier CFA mechanisms to do it.
     Always zap the saved stack pointer value for the next frame; carrying
     the value over from one frame to another doesn't make sense.  */

  _Unwind_SpTmp tmp_sp;

  if (!_Unwind_GetGRPtr (&orig_context, __builtin_dwarf_sp_column ()))
    _Unwind_SetSpColumn (&orig_context, context->cfa, &tmp_sp);
  _Unwind_SetGRPtr (context, __builtin_dwarf_sp_column (), NULL);
#endif

  /* Compute this frame's CFA.  */
  switch (fs->cfa_how)
    {
    case CFA_REG_OFFSET:
      cfa = _Unwind_GetPtr (&orig_context, fs->cfa_reg);
      cfa += fs->cfa_offset;
      break;

    case CFA_EXP:
      {
	const unsigned char *exp = fs->cfa_exp;
	_Unwind_Word len;

	exp = read_uleb128 (exp, &len);
	cfa = (void *) (_Unwind_Ptr)
	  execute_stack_op (exp, exp + len, &orig_context, 0);
	break;
      }

    default:
      gcc_unreachable ();
    }
  context->cfa = cfa;

  /* Compute the addresses of all registers saved in this frame.  */
  for (i = 0; i < DWARF_FRAME_REGISTERS + 1; ++i)
    switch (fs->regs.reg[i].how)
      {
      case REG_UNSAVED:
	break;

      case REG_SAVED_OFFSET:
	_Unwind_SetGRPtr (context, i,
			  (void *) (cfa + fs->regs.reg[i].loc.offset));
	break;

      case REG_SAVED_REG:
	if (_Unwind_GRByValue (&orig_context, fs->regs.reg[i].loc.reg))
	  _Unwind_SetGRValue (context, i,
			      _Unwind_GetGR (&orig_context,
					     fs->regs.reg[i].loc.reg));
	else
	  _Unwind_SetGRPtr (context, i,
			    _Unwind_GetGRPtr (&orig_context,
					      fs->regs.reg[i].loc.reg));
	break;

      case REG_SAVED_EXP:
	{
	  const unsigned char *exp = fs->regs.reg[i].loc.exp;
	  _Unwind_Word len;
	  _Unwind_Ptr val;

	  exp = read_uleb128 (exp, &len);
	  val = execute_stack_op (exp, exp + len, &orig_context,
				  (_Unwind_Ptr) cfa);
	  _Unwind_SetGRPtr (context, i, (void *) val);
	}
	break;

      case REG_SAVED_VAL_OFFSET:
	_Unwind_SetGRValue (context, i,
			    (_Unwind_Internal_Ptr)
			    (cfa + fs->regs.reg[i].loc.offset));
	break;

      case REG_SAVED_VAL_EXP:
	{
	  const unsigned char *exp = fs->regs.reg[i].loc.exp;
	  _Unwind_Word len;
	  _Unwind_Ptr val;

	  exp = read_uleb128 (exp, &len);
	  val = execute_stack_op (exp, exp + len, &orig_context,
				  (_Unwind_Ptr) cfa);
	  _Unwind_SetGRValue (context, i, val);
	}
	break;
      }

  _Unwind_SetSignalFrame (context, fs->signal_frame);

#ifdef MD_FROB_UPDATE_CONTEXT
  MD_FROB_UPDATE_CONTEXT (context, fs);
#endif
}

/* CONTEXT describes the unwind state for a frame, and FS describes the FDE
   of its caller.  Update CONTEXT to refer to the caller as well.  Note
   that the args_size and lsda members are not updated here, but later in
   uw_frame_state_for.  */

static void
uw_update_context (struct _Unwind_Context *context, _Unwind_FrameState *fs)
{
  uw_update_context_1 (context, fs);

  /* Compute the return address now, since the return address column
     can change from frame to frame.  */
  context->ra = __builtin_extract_return_addr
    (_Unwind_GetPtr (context, fs->retaddr_column));
}

static void
uw_advance_context (struct _Unwind_Context *context, _Unwind_FrameState *fs)
{
  uw_update_context (context, fs);
}

/* Fill in CONTEXT for top-of-stack.  The only valid registers at this
   level will be the return address and the CFA.  */

#define uw_init_context(CONTEXT)					   \
  do									   \
    {									   \
      /* Do any necessary initialization to access arbitrary stack frames. \
	 On the SPARC, this means flushing the register windows.  */	   \
      __builtin_unwind_init ();						   \
      uw_init_context_1 (CONTEXT, __builtin_dwarf_cfa (),		   \
			 __builtin_return_address (0));			   \
    }									   \
  while (0)

static inline void
init_dwarf_reg_size_table (void)
{
  __builtin_init_dwarf_reg_size_table (dwarf_reg_size_table);
}

static void
uw_init_context_1 (struct _Unwind_Context *context,
		   void *outer_cfa, void *outer_ra)
{
  void *ra = __builtin_extract_return_addr (__builtin_return_address (0));
  _Unwind_FrameState fs;
  _Unwind_SpTmp sp_slot;
  _Unwind_Reason_Code code;

  memset (context, 0, sizeof (struct _Unwind_Context));
  context->ra = ra;
  context->flags = EXTENDED_CONTEXT_BIT;

  code = uw_frame_state_for (context, &fs);
  gcc_assert (code == _URC_NO_REASON);

#if __GTHREADS
  {
    static __gthread_once_t once_regsizes = __GTHREAD_ONCE_INIT;
    if (__gthread_once (&once_regsizes, init_dwarf_reg_size_table) != 0
	|| dwarf_reg_size_table[0] == 0)
      init_dwarf_reg_size_table ();
  }
#else
  if (dwarf_reg_size_table[0] == 0)
    init_dwarf_reg_size_table ();
#endif

  /* Force the frame state to use the known cfa value.  */
  _Unwind_SetSpColumn (context, outer_cfa, &sp_slot);
  fs.cfa_how = CFA_REG_OFFSET;
  fs.cfa_reg = __builtin_dwarf_sp_column ();
  fs.cfa_offset = 0;

  uw_update_context_1 (context, &fs);

  /* If the return address column was saved in a register in the
     initialization context, then we can't see it in the given
     call frame data.  So have the initialization context tell us.  */
  context->ra = __builtin_extract_return_addr (outer_ra);
}


/* Install TARGET into CURRENT so that we can return to it.  This is a
   macro because __builtin_eh_return must be invoked in the context of
   our caller.  */

#define uw_install_context(CURRENT, TARGET)				 \
  do									 \
    {									 \
      long offset = uw_install_context_1 ((CURRENT), (TARGET));		 \
      void *handler = __builtin_frob_return_addr ((TARGET)->ra);	 \
      __builtin_eh_return (offset, handler);				 \
    }									 \
  while (0)

static long
uw_install_context_1 (struct _Unwind_Context *current,
		      struct _Unwind_Context *target)
{
  long i;
  _Unwind_SpTmp sp_slot;

  /* If the target frame does not have a saved stack pointer,
     then set up the target's CFA.  */
  if (!_Unwind_GetGRPtr (target, __builtin_dwarf_sp_column ()))
    _Unwind_SetSpColumn (target, target->cfa, &sp_slot);

  for (i = 0; i < DWARF_FRAME_REGISTERS; ++i)
    {
      void *c = current->reg[i];
      void *t = target->reg[i];

      gcc_assert (current->by_value[i] == 0);
      if (target->by_value[i] && c)
	{
	  _Unwind_Word w;
	  _Unwind_Ptr p;
	  if (dwarf_reg_size_table[i] == sizeof (_Unwind_Word))
	    {
	      w = (_Unwind_Internal_Ptr) t;
	      memcpy (c, &w, sizeof (_Unwind_Word));
	    }
	  else
	    {
	      gcc_assert (dwarf_reg_size_table[i] == sizeof (_Unwind_Ptr));
	      p = (_Unwind_Internal_Ptr) t;
	      memcpy (c, &p, sizeof (_Unwind_Ptr));
	    }
	}
      else if (t && c && t != c)
	memcpy (c, t, dwarf_reg_size_table[i]);
    }

  /* If the current frame doesn't have a saved stack pointer, then we
     need to rely on EH_RETURN_STACKADJ_RTX to get our target stack
     pointer value reloaded.  */
  if (!_Unwind_GetGRPtr (current, __builtin_dwarf_sp_column ()))
    {
      void *target_cfa;

      target_cfa = _Unwind_GetPtr (target, __builtin_dwarf_sp_column ());

      /* We adjust SP by the difference between CURRENT and TARGET's CFA.  */
      if (STACK_GROWS_DOWNWARD)
	return target_cfa - current->cfa + target->args_size;
      else
	return current->cfa - target_cfa - target->args_size;
    }
  return 0;
}

static inline _Unwind_Ptr
uw_identify_context (struct _Unwind_Context *context)
{
  return _Unwind_GetIP (context);
}


#include "unwind.inc"

#if defined (USE_GAS_SYMVER) && defined (SHARED) && defined (USE_LIBUNWIND_EXCEPTIONS)
alias (_Unwind_Backtrace);
alias (_Unwind_DeleteException);
alias (_Unwind_FindEnclosingFunction);
alias (_Unwind_ForcedUnwind);
alias (_Unwind_GetDataRelBase);
alias (_Unwind_GetTextRelBase);
alias (_Unwind_GetCFA);
alias (_Unwind_GetGR);
alias (_Unwind_GetIP);
alias (_Unwind_GetLanguageSpecificData);
alias (_Unwind_GetRegionStart);
alias (_Unwind_RaiseException);
alias (_Unwind_Resume);
alias (_Unwind_Resume_or_Rethrow);
alias (_Unwind_SetGR);
alias (_Unwind_SetIP);
#endif

#endif /* !USING_SJLJ_EXCEPTIONS */
