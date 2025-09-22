/* DWARF2 EH unwinding support for PA HP-UX.
   Copyright (C) 2005 Free Software Foundation, Inc.

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
#include <unistd.h>

/* FIXME: We currently ignore the high halves of general, space and
   control registers on PA 2.0 machines for applications using the
   32-bit runtime.  We don't restore space registers or the floating
   point status registers.  */

#define MD_FALLBACK_FRAME_STATE_FOR pa_fallback_frame_state

/* HP-UX 10.X doesn't define GetSSReg.  */
#ifndef GetSSReg
#define GetSSReg(ssp, ss_reg) \
  ((UseWideRegs (ssp))							\
   ? (ssp)->ss_wide.ss_32.ss_reg ## _lo					\
   : (ssp)->ss_narrow.ss_reg)
#endif

#if TARGET_64BIT
#define GetSSRegAddr(ssp, ss_reg) ((long) &((ssp)->ss_wide.ss_64.ss_reg))
#else
#define GetSSRegAddr(ssp, ss_reg) \
  ((UseWideRegs (ssp))							\
   ? (long) &((ssp)->ss_wide.ss_32.ss_reg ## _lo)			\
   : (long) &((ssp)->ss_narrow.ss_reg))
#endif

#define UPDATE_FS_FOR_SAR(FS, N) \
  (FS)->regs.reg[N].how = REG_SAVED_OFFSET;				\
  (FS)->regs.reg[N].loc.offset = GetSSRegAddr (mc, ss_cr11) - new_cfa

#define UPDATE_FS_FOR_GR(FS, GRN, N) \
  (FS)->regs.reg[N].how = REG_SAVED_OFFSET;				\
  (FS)->regs.reg[N].loc.offset = GetSSRegAddr (mc, ss_gr##GRN) - new_cfa

#define UPDATE_FS_FOR_FR(FS, FRN, N) \
  (FS)->regs.reg[N].how = REG_SAVED_OFFSET;				\
  (FS)->regs.reg[N].loc.offset = (long) &(mc->ss_fr##FRN) - new_cfa;

#define UPDATE_FS_FOR_PC(FS, N) \
  (FS)->regs.reg[N].how = REG_SAVED_OFFSET;				\
  (FS)->regs.reg[N].loc.offset = GetSSRegAddr (mc, ss_pcoq_head) - new_cfa

/* Extract bit field from word using HP's numbering (MSB = 0).  */
#define GET_FIELD(X, FROM, TO) \
  ((X) >> (31 - (TO)) & ((1 << ((TO) - (FROM) + 1)) - 1))

static inline int
sign_extend (int x, int len)
{
  int signbit = (1 << (len - 1));
  int mask = (signbit << 1) - 1;
  return ((x & mask) ^ signbit) - signbit;
}

/* Extract a 17-bit signed constant from branch instructions.  */
static inline int
extract_17 (unsigned word)
{
  return sign_extend (GET_FIELD (word, 19, 28)
		      | GET_FIELD (word, 29, 29) << 10
		      | GET_FIELD (word, 11, 15) << 11
		      | (word & 0x1) << 16, 17);
}

/* Extract a 22-bit signed constant from branch instructions.  */
static inline int
extract_22 (unsigned word)
{
  return sign_extend (GET_FIELD (word, 19, 28)
		      | GET_FIELD (word, 29, 29) << 10
		      | GET_FIELD (word, 11, 15) << 11
		      | GET_FIELD (word, 6, 10) << 16
		      | (word & 0x1) << 21, 22);
}

static _Unwind_Reason_Code
pa_fallback_frame_state (struct _Unwind_Context *context,
			 _Unwind_FrameState *fs)
{
  static long cpu;
  unsigned int *pc = (unsigned int *) context->ra;

  if (pc == 0)
    return _URC_END_OF_STACK;

  /* Check for relocation of the return value.  */
  if (!TARGET_64BIT
      && *(pc + 0) == 0x2fd01224		/* fstd,ma fr4,8(sp) */
      && *(pc + 1) == 0x0fd9109d		/* ldw -4(sp),ret1 */
      && *(pc + 2) == 0x0fd130bc)		/* ldw,mb -8(sp),ret0 */
    pc += 3;
  else if (!TARGET_64BIT
	   && *(pc + 0) == 0x27d01224		/* fstw,ma fr4,8(sp) */
	   && *(pc + 1) == 0x0fd130bc)		/* ldw,mb -8(sp),ret0 */
    pc += 2;
  else if (!TARGET_64BIT
	   && *(pc + 0) == 0x0fdc12b0		/* stw,ma ret0,8(sp) */
	   && *(pc + 1) == 0x0fdd1299		/* stw ret1,-4(sp) */
	   && *(pc + 2) == 0x2fd13024)		/* fldd,mb -8(sp),fr4 */
    pc += 3;
  else if (!TARGET_64BIT
	   && *(pc + 0) == 0x0fdc12b0		/* stw,ma ret0,8(sp) */
	   && *(pc + 1) == 0x27d13024)		/* fldw,mb -8(sp),fr4 */
    pc += 2;

  /* Check if the return address points to an export stub (PA 1.1 or 2.0).  */
  if ((!TARGET_64BIT
       && *(pc + 0) == 0x4bc23fd1		/* ldw -18(sp),rp */
       && *(pc + 1) == 0x004010a1		/* ldsid (rp),r1 */
       && *(pc + 2) == 0x00011820		/* mtsp r1,sr0 */
       && *(pc + 3) == 0xe0400002)		/* be,n 0(sr0,rp) */
      ||
      (!TARGET_64BIT
       && *(pc + 0) == 0x4bc23fd1		/* ldw -18(sp),rp */
       && *(pc + 1) == 0xe840d002))		/* bve,n (rp) */
    {
      fs->cfa_how    = CFA_REG_OFFSET;
      fs->cfa_reg    = 30;
      fs->cfa_offset = 0;

      fs->retaddr_column = 0;
      fs->regs.reg[0].how = REG_SAVED_OFFSET;
      fs->regs.reg[0].loc.offset = -24;

      /* Update context to describe the stub frame.  */
      uw_update_context (context, fs);

      /* Set up fs to describe the FDE for the caller of this stub.  */
      return uw_frame_state_for (context, fs);
    }
  /* Check if the return address points to a relocation stub.  */
  else if (!TARGET_64BIT
	   && *(pc + 0) == 0x0fd11082		/* ldw -8(sp),rp */
	   && (*(pc + 1) == 0xe840c002		/* bv,n r0(rp) */
	       || *(pc + 1) == 0xe840d002))	/* bve,n (rp) */
    {
      fs->cfa_how    = CFA_REG_OFFSET;
      fs->cfa_reg    = 30;
      fs->cfa_offset = 0;

      fs->retaddr_column = 0;
      fs->regs.reg[0].how = REG_SAVED_OFFSET;
      fs->regs.reg[0].loc.offset = -8;

      /* Update context to describe the stub frame.  */
      uw_update_context (context, fs);

      /* Set up fs to describe the FDE for the caller of this stub.  */
      return uw_frame_state_for (context, fs);
    }

  /* Check if the return address is an export stub as signal handlers
     may return via an export stub.  */
  if (!TARGET_64BIT
      && (*pc & 0xffe0e002) == 0xe8400000	/* bl x,r2 */
      && *(pc + 1) == 0x08000240		/* nop */
      && *(pc + 2) == 0x4bc23fd1		/* ldw -18(sp),rp */
      && *(pc + 3) == 0x004010a1		/* ldsid (rp),r1 */
      && *(pc + 4) == 0x00011820		/* mtsp r1,sr0 */
      && *(pc + 5) == 0xe0400002)		/* be,n 0(sr0,rp) */
    /* Extract target address from PA 1.x 17-bit branch.  */
    pc += extract_17 (*pc) + 2;
  else if (!TARGET_64BIT
	   && (*pc & 0xfc00e002) == 0xe800a000	/* b,l x,r2 */
	   && *(pc + 1) == 0x08000240		/* nop */
	   && *(pc + 2) == 0x4bc23fd1		/* ldw -18(sp),rp */
	   && *(pc + 3) == 0xe840d002)		/* bve,n (rp) */
    /* Extract target address from PA 2.0 22-bit branch.  */
    pc += extract_22 (*pc) + 2;

  /* Now check if the return address is one of the signal handler
     returns, _sigreturn or _sigsetreturn.  */
  if ((TARGET_64BIT
       && *(pc + 0)  == 0x53db3f51		/* ldd -58(sp),dp */
       && *(pc + 8)  == 0x34160116		/* ldi 8b,r22 */
       && *(pc + 9)  == 0x08360ac1		/* shladd,l r22,3,r1,r1 */
       && *(pc + 10) == 0x0c2010c1		/* ldd 0(r1),r1 */
       && *(pc + 11) == 0xe4202000)		/* be,l 0(sr4,r1) */
      ||
      (TARGET_64BIT
       && *(pc + 0)  == 0x36dc0000		/* ldo 0(r22),ret0 */
       && *(pc + 6)  == 0x341601c0		/* ldi e0,r22 */
       && *(pc + 7)  == 0x08360ac1		/* shladd,l r22,3,r1,r1 */
       && *(pc + 8)  == 0x0c2010c1		/* ldd 0(r1),r1 */
       && *(pc + 9)  == 0xe4202000)		/* be,l 0(sr4,r1) */
      ||
      (!TARGET_64BIT
       && *(pc + 0)  == 0x379a0000		/* ldo 0(ret0),r26 */
       && *(pc + 1)  == 0x6bd33fc9		/* stw r19,-1c(sp) */
       && *(pc + 2)  == 0x20200801		/* ldil L%-40000000,r1 */
       && *(pc + 3)  == 0xe420e008		/* be,l 4(sr7,r1) */
       && *(pc + 4)  == 0x34160116)		/* ldi 8b,r22 */
      ||
      (!TARGET_64BIT
       && *(pc + 0)  == 0x6bd33fc9		/* stw r19,-1c(sp) */
       && *(pc + 1)  == 0x20200801		/* ldil L%-40000000,r1 */
       && *(pc + 2)  == 0xe420e008		/* be,l 4(sr7,r1) */
       && *(pc + 3)  == 0x341601c0))		/* ldi e0,r22 */
    {
      /* The previous stack pointer is saved at (long *)SP - 1.  The
	 ucontext structure is offset from the start of the previous
	 frame by the siglocal_misc structure.  */
      struct siglocalx *sl = (struct siglocalx *)
	(*((long *) context->cfa - 1));
      mcontext_t *mc = &(sl->sl_uc.uc_mcontext);

      long new_cfa = GetSSReg (mc, ss_sp);

      fs->cfa_how = CFA_REG_OFFSET;
      fs->cfa_reg = 30;
      fs->cfa_offset = new_cfa - (long) context->cfa;

      UPDATE_FS_FOR_GR (fs, 1, 1);
      UPDATE_FS_FOR_GR (fs, 2, 2);
      UPDATE_FS_FOR_GR (fs, 3, 3);
      UPDATE_FS_FOR_GR (fs, 4, 4);
      UPDATE_FS_FOR_GR (fs, 5, 5);
      UPDATE_FS_FOR_GR (fs, 6, 6);
      UPDATE_FS_FOR_GR (fs, 7, 7);
      UPDATE_FS_FOR_GR (fs, 8, 8);
      UPDATE_FS_FOR_GR (fs, 9, 9);
      UPDATE_FS_FOR_GR (fs, 10, 10);
      UPDATE_FS_FOR_GR (fs, 11, 11);
      UPDATE_FS_FOR_GR (fs, 12, 12);
      UPDATE_FS_FOR_GR (fs, 13, 13);
      UPDATE_FS_FOR_GR (fs, 14, 14);
      UPDATE_FS_FOR_GR (fs, 15, 15);
      UPDATE_FS_FOR_GR (fs, 16, 16);
      UPDATE_FS_FOR_GR (fs, 17, 17);
      UPDATE_FS_FOR_GR (fs, 18, 18);
      UPDATE_FS_FOR_GR (fs, 19, 19);
      UPDATE_FS_FOR_GR (fs, 20, 20);
      UPDATE_FS_FOR_GR (fs, 21, 21);
      UPDATE_FS_FOR_GR (fs, 22, 22);
      UPDATE_FS_FOR_GR (fs, 23, 23);
      UPDATE_FS_FOR_GR (fs, 24, 24);
      UPDATE_FS_FOR_GR (fs, 25, 25);
      UPDATE_FS_FOR_GR (fs, 26, 26);
      UPDATE_FS_FOR_GR (fs, 27, 27);
      UPDATE_FS_FOR_GR (fs, 28, 28);
      UPDATE_FS_FOR_GR (fs, 29, 29);
      UPDATE_FS_FOR_GR (fs, 30, 30);
      UPDATE_FS_FOR_GR (fs, 31, 31);

      if (TARGET_64BIT)
	{
	  UPDATE_FS_FOR_FR (fs, 4, 32);
	  UPDATE_FS_FOR_FR (fs, 5, 33);
	  UPDATE_FS_FOR_FR (fs, 6, 34);
	  UPDATE_FS_FOR_FR (fs, 7, 35);
	  UPDATE_FS_FOR_FR (fs, 8, 36);
	  UPDATE_FS_FOR_FR (fs, 9, 37);
	  UPDATE_FS_FOR_FR (fs, 10, 38);
	  UPDATE_FS_FOR_FR (fs, 11, 39);
	  UPDATE_FS_FOR_FR (fs, 12, 40);
	  UPDATE_FS_FOR_FR (fs, 13, 41);
	  UPDATE_FS_FOR_FR (fs, 14, 42);
	  UPDATE_FS_FOR_FR (fs, 15, 43);
	  UPDATE_FS_FOR_FR (fs, 16, 44);
	  UPDATE_FS_FOR_FR (fs, 17, 45);
	  UPDATE_FS_FOR_FR (fs, 18, 46);
	  UPDATE_FS_FOR_FR (fs, 19, 47);
	  UPDATE_FS_FOR_FR (fs, 20, 48);
	  UPDATE_FS_FOR_FR (fs, 21, 49);
	  UPDATE_FS_FOR_FR (fs, 22, 50);
	  UPDATE_FS_FOR_FR (fs, 23, 51);
	  UPDATE_FS_FOR_FR (fs, 24, 52);
	  UPDATE_FS_FOR_FR (fs, 25, 53);
	  UPDATE_FS_FOR_FR (fs, 26, 54);
	  UPDATE_FS_FOR_FR (fs, 27, 55);
	  UPDATE_FS_FOR_FR (fs, 28, 56);
	  UPDATE_FS_FOR_FR (fs, 29, 57);
	  UPDATE_FS_FOR_FR (fs, 30, 58);
	  UPDATE_FS_FOR_FR (fs, 31, 59);

	  UPDATE_FS_FOR_SAR (fs, 60);
	}
      else
	{
	  UPDATE_FS_FOR_FR (fs, 4, 32);
	  UPDATE_FS_FOR_FR (fs, 5, 34);
	  UPDATE_FS_FOR_FR (fs, 6, 36);
	  UPDATE_FS_FOR_FR (fs, 7, 38);
	  UPDATE_FS_FOR_FR (fs, 8, 40);
	  UPDATE_FS_FOR_FR (fs, 9, 44);
	  UPDATE_FS_FOR_FR (fs, 10, 44);
	  UPDATE_FS_FOR_FR (fs, 11, 46);
	  UPDATE_FS_FOR_FR (fs, 12, 48);
	  UPDATE_FS_FOR_FR (fs, 13, 50);
	  UPDATE_FS_FOR_FR (fs, 14, 52);
	  UPDATE_FS_FOR_FR (fs, 15, 54);

	  if (!cpu)
	    cpu = sysconf (_SC_CPU_VERSION);

	  /* PA-RISC 1.0 only has 16 floating point registers.  */
	  if (cpu != CPU_PA_RISC1_0)
	    {
	      UPDATE_FS_FOR_FR (fs, 16, 56);
	      UPDATE_FS_FOR_FR (fs, 17, 58);
	      UPDATE_FS_FOR_FR (fs, 18, 60);
	      UPDATE_FS_FOR_FR (fs, 19, 62);
	      UPDATE_FS_FOR_FR (fs, 20, 64);
	      UPDATE_FS_FOR_FR (fs, 21, 66);
	      UPDATE_FS_FOR_FR (fs, 22, 68);
	      UPDATE_FS_FOR_FR (fs, 23, 70);
	      UPDATE_FS_FOR_FR (fs, 24, 72);
	      UPDATE_FS_FOR_FR (fs, 25, 74);
	      UPDATE_FS_FOR_FR (fs, 26, 76);
	      UPDATE_FS_FOR_FR (fs, 27, 78);
	      UPDATE_FS_FOR_FR (fs, 28, 80);
	      UPDATE_FS_FOR_FR (fs, 29, 82);
	      UPDATE_FS_FOR_FR (fs, 30, 84);
	      UPDATE_FS_FOR_FR (fs, 31, 86);
	    }

	  UPDATE_FS_FOR_SAR (fs, 88);
	}

      fs->retaddr_column = DWARF_ALT_FRAME_RETURN_COLUMN;
      UPDATE_FS_FOR_PC (fs, DWARF_ALT_FRAME_RETURN_COLUMN);

      return _URC_NO_REASON;
    }

  return _URC_END_OF_STACK;
}
