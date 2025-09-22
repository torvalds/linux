/* DWARF2 EH unwinding support for Alpha VMS.
   Copyright (C) 2004 Free Software Foundation, Inc.

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

#include <pdscdef.h>

#define MD_FALLBACK_FRAME_STATE_FOR alpha_fallback_frame_state

static _Unwind_Reason_Code
alpha_fallback_frame_state (struct _Unwind_Context *context,
			    _Unwind_FrameState *fs)
{
  PDSCDEF *pv = *((PDSCDEF **) context->reg [29]);

  if (pv && ((long) pv & 0x7) == 0) /* low bits 0 means address */
    pv = *(PDSCDEF **) pv;

  if (pv && ((pv->pdsc$w_flags & 0xf) == PDSC$K_KIND_FP_STACK))
    {
      int i, j;

      fs->cfa_offset = pv->pdsc$l_size;
      fs->cfa_reg = pv->pdsc$w_flags & PDSC$M_BASE_REG_IS_FP ? 29 : 30;
      fs->retaddr_column = 26;
      fs->cfa_how = CFA_REG_OFFSET;
      fs->regs.reg[27].loc.offset = -pv->pdsc$l_size;
      fs->regs.reg[27].how = REG_SAVED_OFFSET;
      fs->regs.reg[26].loc.offset
	= -(pv->pdsc$l_size - pv->pdsc$w_rsa_offset);
      fs->regs.reg[26].how = REG_SAVED_OFFSET;

      for (i = 0, j = 0; i < 32; i++)
	if (1<<i & pv->pdsc$l_ireg_mask)
	  {
	    fs->regs.reg[i].loc.offset
	      = -(pv->pdsc$l_size - pv->pdsc$w_rsa_offset - 8 * ++j);
	    fs->regs.reg[i].how = REG_SAVED_OFFSET;
	  }

      return _URC_NO_REASON;
    }
  else if (pv && ((pv->pdsc$w_flags & 0xf) == PDSC$K_KIND_FP_REGISTER))
    {
      fs->cfa_offset = pv->pdsc$l_size;
      fs->cfa_reg = pv->pdsc$w_flags & PDSC$M_BASE_REG_IS_FP ? 29 : 30;
      fs->retaddr_column = 26;
      fs->cfa_how = CFA_REG_OFFSET;
      fs->regs.reg[26].loc.reg = pv->pdsc$b_save_ra;
      fs->regs.reg[26].how = REG_SAVED_REG;
      fs->regs.reg[29].loc.reg = pv->pdsc$b_save_fp;
      fs->regs.reg[29].how = REG_SAVED_REG;

      return _URC_NO_REASON;
    }
  return _URC_END_OF_STACK;
}
