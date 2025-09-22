/* ARM EABI compliant unwinding routines
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Contributed by Paul Brook
 
   This file is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combine
   executable.)

   This file is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */
#include "unwind.h"

/* We add a prototype for abort here to avoid creating a dependency on
   target headers.  */
extern void abort (void);

typedef struct _ZSt9type_info type_info; /* This names C++ type_info type */

/* Misc constants.  */
#define R_IP    12
#define R_SP    13
#define R_LR    14
#define R_PC    15

#define uint32_highbit (((_uw) 1) << 31)

void __attribute__((weak)) __cxa_call_unexpected(_Unwind_Control_Block *ucbp);

/* Unwind descriptors.  */

typedef struct
{
  _uw16 length;
  _uw16 offset;
} EHT16;

typedef struct
{
  _uw length;
  _uw offset;
} EHT32;

/* Calculate the address encoded by a 31-bit self-relative offset at address
   P.  Copy of routine in unwind-arm.c.  */

static inline _uw
selfrel_offset31 (const _uw *p)
{
  _uw offset;

  offset = *p;
  /* Sign extend to 32 bits.  */
  if (offset & (1 << 30))
    offset |= 1u << 31;

  return offset + (_uw) p;
}


/* Personality routine helper functions.  */

#define CODE_FINISH (0xb0)

/* Return the next byte of unwinding information, or CODE_FINISH if there is
   no data remaining.  */
static inline _uw8
next_unwind_byte (__gnu_unwind_state * uws)
{
  _uw8 b;

  if (uws->bytes_left == 0)
    {
      /* Load another word */
      if (uws->words_left == 0)
	return CODE_FINISH; /* Nothing left.  */
      uws->words_left--;
      uws->data = *(uws->next++);
      uws->bytes_left = 3;
    }
  else
    uws->bytes_left--;

  /* Extract the most significant byte.  */
  b = (uws->data >> 24) & 0xff;
  uws->data <<= 8;
  return b;
}

/* Execute the unwinding instructions described by UWS.  */
_Unwind_Reason_Code
__gnu_unwind_execute (_Unwind_Context * context, __gnu_unwind_state * uws)
{
  _uw op;
  int set_pc;
  _uw reg;

  set_pc = 0;
  for (;;)
    {
      op = next_unwind_byte (uws);
      if (op == CODE_FINISH)
	{
	  /* If we haven't already set pc then copy it from lr.  */
	  if (!set_pc)
	    {
	      _Unwind_VRS_Get (context, _UVRSC_CORE, R_LR, _UVRSD_UINT32,
			       &reg);
	      _Unwind_VRS_Set (context, _UVRSC_CORE, R_PC, _UVRSD_UINT32,
			       &reg);
	      set_pc = 1;
	    }
	  /* Drop out of the loop.  */
	  break;
	}
      if ((op & 0x80) == 0)
	{
	  /* vsp = vsp +- (imm6 << 2 + 4).  */
	  _uw offset;

	  offset = ((op & 0x3f) << 2) + 4;
	  _Unwind_VRS_Get (context, _UVRSC_CORE, R_SP, _UVRSD_UINT32, &reg);
	  if (op & 0x40)
	    reg -= offset;
	  else
	    reg += offset;
	  _Unwind_VRS_Set (context, _UVRSC_CORE, R_SP, _UVRSD_UINT32, &reg);
	  continue;
	}
      
      if ((op & 0xf0) == 0x80)
	{
	  op = (op << 8) | next_unwind_byte (uws);
	  if (op == 0x8000)
	    {
	      /* Refuse to unwind.  */
	      return _URC_FAILURE;
	    }
	  /* Pop r4-r15 under mask.  */
	  op = (op << 4) & 0xfff0;
	  if (_Unwind_VRS_Pop (context, _UVRSC_CORE, op, _UVRSD_UINT32)
	      != _UVRSR_OK)
	    return _URC_FAILURE;
	  if (op & (1 << R_PC))
	    set_pc = 1;
	  continue;
	}
      if ((op & 0xf0) == 0x90)
	{
	  op &= 0xf;
	  if (op == 13 || op == 15)
	    /* Reserved.  */
	    return _URC_FAILURE;
	  /* vsp = r[nnnn].  */
	  _Unwind_VRS_Get (context, _UVRSC_CORE, op, _UVRSD_UINT32, &reg);
	  _Unwind_VRS_Set (context, _UVRSC_CORE, R_SP, _UVRSD_UINT32, &reg);
	  continue;
	}
      if ((op & 0xf0) == 0xa0)
	{
	  /* Pop r4-r[4+nnn], [lr].  */
	  _uw mask;
	  
	  mask = (0xff0 >> (7 - (op & 7))) & 0xff0;
	  if (op & 8)
	    mask |= (1 << R_LR);
	  if (_Unwind_VRS_Pop (context, _UVRSC_CORE, mask, _UVRSD_UINT32)
	      != _UVRSR_OK)
	    return _URC_FAILURE;
	  continue;
	}
      if ((op & 0xf0) == 0xb0)
	{
	  /* op == 0xb0 already handled.  */
	  if (op == 0xb1)
	    {
	      op = next_unwind_byte (uws);
	      if (op == 0 || ((op & 0xf0) != 0))
		/* Spare.  */
		return _URC_FAILURE;
	      /* Pop r0-r4 under mask.  */
	      if (_Unwind_VRS_Pop (context, _UVRSC_CORE, op, _UVRSD_UINT32)
		  != _UVRSR_OK)
		return _URC_FAILURE;
	      continue;
	    }
	  if (op == 0xb2)
	    {
	      /* vsp = vsp + 0x204 + (uleb128 << 2).  */
	      int shift;

	      _Unwind_VRS_Get (context, _UVRSC_CORE, R_SP, _UVRSD_UINT32,
			       &reg);
	      op = next_unwind_byte (uws);
	      shift = 2;
	      while (op & 0x80)
		{
		  reg += ((op & 0x7f) << shift);
		  shift += 7;
		  op = next_unwind_byte (uws);
		}
	      reg += ((op & 0x7f) << shift) + 0x204;
	      _Unwind_VRS_Set (context, _UVRSC_CORE, R_SP, _UVRSD_UINT32,
			       &reg);
	      continue;
	    }
	  if (op == 0xb3)
	    {
	      /* Pop VFP registers with fldmx.  */
	      op = next_unwind_byte (uws);
	      op = ((op & 0xf0) << 12) | ((op & 0xf) + 1);
	      if (_Unwind_VRS_Pop (context, _UVRSC_VFP, op, _UVRSD_VFPX)
		  != _UVRSR_OK)
		return _URC_FAILURE;
	      continue;
	    }
	  if ((op & 0xfc) == 0xb4)
	    {
	      /* Pop FPA E[4]-E[4+nn].  */
	      op = 0x40000 | ((op & 3) + 1);
	      if (_Unwind_VRS_Pop (context, _UVRSC_FPA, op, _UVRSD_FPAX)
		  != _UVRSR_OK)
		return _URC_FAILURE;
	      continue;
	    }
	  /* op & 0xf8 == 0xb8.  */
	  /* Pop VFP D[8]-D[8+nnn] with fldmx.  */
	  op = 0x80000 | ((op & 7) + 1);
	  if (_Unwind_VRS_Pop (context, _UVRSC_VFP, op, _UVRSD_VFPX)
	      != _UVRSR_OK)
	    return _URC_FAILURE;
	  continue;
	}
      if ((op & 0xf0) == 0xc0)
	{
	  if (op == 0xc6)
	    {
	      /* Pop iWMMXt D registers.  */
	      op = next_unwind_byte (uws);
	      op = ((op & 0xf0) << 12) | ((op & 0xf) + 1);
	      if (_Unwind_VRS_Pop (context, _UVRSC_WMMXD, op, _UVRSD_UINT64)
		  != _UVRSR_OK)
		return _URC_FAILURE;
	      continue;
	    }
	  if (op == 0xc7)
	    {
	      op = next_unwind_byte (uws);
	      if (op == 0 || (op & 0xf0) != 0)
		/* Spare.  */
		return _URC_FAILURE;
	      /* Pop iWMMXt wCGR{3,2,1,0} under mask.  */
	      if (_Unwind_VRS_Pop (context, _UVRSC_WMMXC, op, _UVRSD_UINT32)
		  != _UVRSR_OK)
		return _URC_FAILURE;
	      continue;
	    }
	  if ((op & 0xf8) == 0xc0)
	    {
	      /* Pop iWMMXt wR[10]-wR[10+nnn].  */
	      op = 0xa0000 | ((op & 0xf) + 1);
	      if (_Unwind_VRS_Pop (context, _UVRSC_WMMXD, op, _UVRSD_UINT64)
		  != _UVRSR_OK)
		return _URC_FAILURE;
	      continue;
	    }
	  if (op == 0xc8)
	    {
	      /* Pop FPA registers.  */
	      op = next_unwind_byte (uws);
	      op = ((op & 0xf0) << 12) | ((op & 0xf) + 1);
	      if (_Unwind_VRS_Pop (context, _UVRSC_FPA, op, _UVRSD_FPAX)
		  != _UVRSR_OK)
		return _URC_FAILURE;
	      continue;
	    }
	  if (op == 0xc9)
	    {
	      /* Pop VFP registers with fldmd.  */
	      op = next_unwind_byte (uws);
	      op = ((op & 0xf0) << 12) | ((op & 0xf) + 1);
	      if (_Unwind_VRS_Pop (context, _UVRSC_VFP, op, _UVRSD_DOUBLE)
		  != _UVRSR_OK)
		return _URC_FAILURE;
	      continue;
	    }
	  /* Spare.  */
	  return _URC_FAILURE;
	}
      if ((op & 0xf8) == 0xd0)
	{
	  /* Pop VFP D[8]-D[8+nnn] with fldmd.  */
	  op = 0x80000 | ((op & 7) + 1);
	  if (_Unwind_VRS_Pop (context, _UVRSC_VFP, op, _UVRSD_DOUBLE)
	      != _UVRSR_OK)
	    return _URC_FAILURE;
	  continue;
	}
      /* Spare.  */
      return _URC_FAILURE;
    }
  return _URC_OK;
}


/* Execute the unwinding instructions associated with a frame.  UCBP and
   CONTEXT are the current exception object and virtual CPU state
   respectively.  */

_Unwind_Reason_Code
__gnu_unwind_frame (_Unwind_Control_Block * ucbp, _Unwind_Context * context)
{
  _uw *ptr;
  __gnu_unwind_state uws;

  ptr = (_uw *) ucbp->pr_cache.ehtp;
  /* Skip over the personality routine address.  */
  ptr++;
  /* Setup the unwinder state.  */
  uws.data = (*ptr) << 8;
  uws.next = ptr + 1;
  uws.bytes_left = 3;
  uws.words_left = ((*ptr) >> 24) & 0xff;

  return __gnu_unwind_execute (context, &uws);
}

/* Get the _Unwind_Control_Block from an _Unwind_Context.  */

static inline _Unwind_Control_Block *
unwind_UCB_from_context (_Unwind_Context * context)
{
  return (_Unwind_Control_Block *) _Unwind_GetGR (context, R_IP);
}

/* Get the start address of the function being unwound.  */

_Unwind_Ptr
_Unwind_GetRegionStart (_Unwind_Context * context)
{
  _Unwind_Control_Block *ucbp;

  ucbp = unwind_UCB_from_context (context);
  return (_Unwind_Ptr) ucbp->pr_cache.fnstart;
}

/* Find the Language specific exception data.  */

void *
_Unwind_GetLanguageSpecificData (_Unwind_Context * context)
{
  _Unwind_Control_Block *ucbp;
  _uw *ptr;

  /* Get a pointer to the exception table entry.  */
  ucbp = unwind_UCB_from_context (context);
  ptr = (_uw *) ucbp->pr_cache.ehtp;
  /* Skip the personality routine address.  */
  ptr++;
  /* Skip the unwind opcodes.  */
  ptr += (((*ptr) >> 24) & 0xff) + 1;

  return ptr;
}

