/* DWARF2 EH unwinding support for PA Linux.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file with other programs, and to distribute
those programs without any restriction coming from the use of this
file.  (The General Public License restrictions do apply in other
respects; for example, they cover modification of the file, and
distribution when not linked into another program.)

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* Do code reading to identify a signal frame, and set the frame
   state data appropriately.  See unwind-dw2.c for the structs.  */

#include <signal.h>
#include <sys/ucontext.h>

/* Unfortunately, because of various bugs and changes to the kernel,
   we have several cases to deal with.

   In 2.4, the signal trampoline is 4 words, and (CONTEXT)->ra should
   point directly at the beginning of the trampoline and struct rt_sigframe.

   In <= 2.6.5-rc2-pa3, the signal trampoline is 9 words, and 
   (CONTEXT)->ra points at the 4th word in the trampoline structure.  This 
   is wrong, it should point at the 5th word.  This is fixed in 2.6.5-rc2-pa4.

   To detect these cases, we first take (CONTEXT)->ra, align it to 64-bytes
   to get the beginning of the signal frame, and then check offsets 0, 4
   and 5 to see if we found the beginning of the trampoline.  This will
   tell us how to locate the sigcontext structure.

   Note that with a 2.4 64-bit kernel, the signal context is not properly
   passed back to userspace so the unwind will not work correctly.  */

#define MD_FALLBACK_FRAME_STATE_FOR pa32_fallback_frame_state

static _Unwind_Reason_Code
pa32_fallback_frame_state (struct _Unwind_Context *context,
			   _Unwind_FrameState *fs)
{
  unsigned long sp = (unsigned long)context->ra & ~63;
  unsigned int *pc = (unsigned int *)sp;
  unsigned long off;
  _Unwind_Ptr new_cfa;
  int i;
  struct sigcontext *sc;
  struct rt_sigframe {
    struct siginfo info;
    struct ucontext uc;
  } *frame;

  /* rt_sigreturn trampoline:
     3419000x ldi 0, %r25 or ldi 1, %r25   (x = 0 or 2)
     3414015a ldi __NR_rt_sigreturn, %r20
     e4008200 be,l 0x100(%sr2, %r0), %sr0, %r31
     08000240 nop  */

  if (pc[0] == 0x34190000 || pc[0] == 0x34190002)
    off = 4*4;
  else if (pc[4] == 0x34190000 || pc[4] == 0x34190002)
    {
      pc += 4;
      off = 10 * 4;
    }
  else if (pc[5] == 0x34190000 || pc[5] == 0x34190002)
    {
      pc += 5;
      off = 10 * 4;
    }
  else
    {
      /* We may have to unwind through an alternate signal stack.
	 We assume that the alignment of the alternate signal stack
	 is BIGGEST_ALIGNMENT (i.e., that it has been allocated using
	 malloc).  As a result, we can't distinguish trampolines
	 used prior to 2.6.5-rc2-pa4.  However after 2.6.5-rc2-pa4,
	 the return address of a signal trampoline will be on an odd
	 word boundary and we can then determine the frame offset.  */
      sp = (unsigned long)context->ra;
      pc = (unsigned int *)sp;
      if ((pc[0] == 0x34190000 || pc[0] == 0x34190002) && (sp & 4))
	off = 5 * 4;
      else
	return _URC_END_OF_STACK;
    }

  if (pc[1] != 0x3414015a
      || pc[2] != 0xe4008200
      || pc[3] != 0x08000240)
    return _URC_END_OF_STACK;

  frame = (struct rt_sigframe *)(sp + off);
  sc = &frame->uc.uc_mcontext;

  new_cfa = sc->sc_gr[30];
  fs->cfa_how = CFA_REG_OFFSET;
  fs->cfa_reg = 30;
  fs->cfa_offset = new_cfa - (long) context->cfa;
  for (i = 1; i <= 31; i++)
    {
      fs->regs.reg[i].how = REG_SAVED_OFFSET;
      fs->regs.reg[i].loc.offset = (long)&sc->sc_gr[i] - new_cfa;
    }
  for (i = 4; i <= 31; i++)
    {
      /* FP regs have left and right halves */
      fs->regs.reg[2*i+24].how = REG_SAVED_OFFSET;
      fs->regs.reg[2*i+24].loc.offset
	= (long)&sc->sc_fr[i] - new_cfa;
      fs->regs.reg[2*i+24+1].how = REG_SAVED_OFFSET;
      fs->regs.reg[2*i+24+1].loc.offset
	= (long)&sc->sc_fr[i] + 4 - new_cfa;
    }
  fs->regs.reg[88].how = REG_SAVED_OFFSET;
  fs->regs.reg[88].loc.offset = (long) &sc->sc_sar - new_cfa;
  fs->regs.reg[DWARF_ALT_FRAME_RETURN_COLUMN].how = REG_SAVED_OFFSET;
  fs->regs.reg[DWARF_ALT_FRAME_RETURN_COLUMN].loc.offset
    = (long) &sc->sc_iaoq[0] - new_cfa;
  fs->retaddr_column = DWARF_ALT_FRAME_RETURN_COLUMN;
  return _URC_NO_REASON;
}
