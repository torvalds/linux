/* $Id: kgdb.h,v 1.8 1998/01/07 06:33:44 baccala Exp $
 * kgdb.h: Defines and declarations for serial line source level
 *         remote debugging of the Linux kernel using gdb.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_KGDB_H
#define _SPARC_KGDB_H

#ifndef __ASSEMBLY__
/* To init the kgdb engine. */
extern void set_debug_traps(void);

/* To enter the debugger explicitly. */
extern void breakpoint(void);

/* For convenience we define the format of a kgdb trap breakpoint
 * frame here also.
 */
struct kgdb_frame {
	unsigned long globals[8];
	unsigned long outs[8];
	unsigned long locals[8];
	unsigned long ins[8];
	unsigned long fpregs[32];
	unsigned long y;
	unsigned long psr;
	unsigned long wim;
	unsigned long tbr;
	unsigned long pc;
	unsigned long npc;
	unsigned long fpsr;
	unsigned long cpsr;
};
#endif /* !(__ASSEMBLY__) */

/* Macros for assembly usage of the kgdb breakpoint frame. */
#define KGDB_G0     0x000
#define KGDB_G1     0x004
#define KGDB_G2     0x008
#define KGDB_G4     0x010
#define KGDB_G6     0x018
#define KGDB_I0     0x020
#define KGDB_I2     0x028
#define KGDB_I4     0x030
#define KGDB_I6     0x038
#define KGDB_Y      0x100
#define KGDB_PSR    0x104
#define KGDB_WIM    0x108
#define KGDB_TBR    0x10c
#define KGDB_PC     0x110
#define KGDB_NPC    0x114

#define SAVE_KGDB_GLOBALS(reg) \
        std     %g0, [%reg + STACKFRAME_SZ + KGDB_G0]; \
        std     %g2, [%reg + STACKFRAME_SZ + KGDB_G2]; \
        std     %g4, [%reg + STACKFRAME_SZ + KGDB_G4]; \
        std     %g6, [%reg + STACKFRAME_SZ + KGDB_G6];

#define SAVE_KGDB_INS(reg) \
        std     %i0, [%reg + STACKFRAME_SZ + KGDB_I0]; \
        std     %i2, [%reg + STACKFRAME_SZ + KGDB_I2]; \
        std     %i4, [%reg + STACKFRAME_SZ + KGDB_I4]; \
        std     %i6, [%reg + STACKFRAME_SZ + KGDB_I6];

#define SAVE_KGDB_SREGS(reg, reg_y, reg_psr, reg_wim, reg_tbr, reg_pc, reg_npc) \
        st      %reg_y, [%reg + STACKFRAME_SZ + KGDB_Y]; \
        st      %reg_psr, [%reg + STACKFRAME_SZ + KGDB_PSR]; \
        st      %reg_wim, [%reg + STACKFRAME_SZ + KGDB_WIM]; \
        st      %reg_tbr, [%reg + STACKFRAME_SZ + KGDB_TBR]; \
        st      %reg_pc, [%reg + STACKFRAME_SZ + KGDB_PC]; \
        st      %reg_npc, [%reg + STACKFRAME_SZ + KGDB_NPC];

#define LOAD_KGDB_GLOBALS(reg) \
        ld      [%reg + STACKFRAME_SZ + KGDB_G1], %g1; \
        ldd     [%reg + STACKFRAME_SZ + KGDB_G2], %g2; \
        ldd     [%reg + STACKFRAME_SZ + KGDB_G4], %g4; \
        ldd     [%reg + STACKFRAME_SZ + KGDB_G6], %g6;

#define LOAD_KGDB_INS(reg) \
        ldd     [%reg + STACKFRAME_SZ + KGDB_I0], %i0; \
        ldd     [%reg + STACKFRAME_SZ + KGDB_I2], %i2; \
        ldd     [%reg + STACKFRAME_SZ + KGDB_I4], %i4; \
        ldd     [%reg + STACKFRAME_SZ + KGDB_I6], %i6;

#define LOAD_KGDB_SREGS(reg, reg_y, reg_psr, reg_wim, reg_tbr, reg_pc, reg_npc) \
	ld	[%reg + STACKFRAME_SZ + KGDB_Y], %reg_y; \
	ld	[%reg + STACKFRAME_SZ + KGDB_PSR], %reg_psr; \
	ld	[%reg + STACKFRAME_SZ + KGDB_WIM], %reg_wim; \
	ld	[%reg + STACKFRAME_SZ + KGDB_TBR], %reg_tbr; \
	ld	[%reg + STACKFRAME_SZ + KGDB_PC], %reg_pc; \
	ld	[%reg + STACKFRAME_SZ + KGDB_NPC], %reg_npc;

#endif /* !(_SPARC_KGDB_H) */
