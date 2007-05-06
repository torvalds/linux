/*
 *  linux/include/asm/traps.h
 *
 *  Copyright (C) 1993        Hamish Macdonald
 *
 *  Lineo, Inc    Jul 2001    Tony Kou
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef _BFIN_TRAPS_H
#define _BFIN_TRAPS_H

#define VEC_SYS		(0)
#define VEC_EXCPT01	(1)
#define VEC_EXCPT02	(2)
#define VEC_EXCPT03	(3)
#define VEC_EXCPT04	(4)
#define VEC_EXCPT05	(5)
#define VEC_EXCPT06	(6)
#define VEC_EXCPT07	(7)
#define VEC_EXCPT08	(8)
#define VEC_EXCPT09	(9)
#define VEC_EXCPT10	(10)
#define VEC_EXCPT11	(11)
#define VEC_EXCPT12	(12)
#define VEC_EXCPT13	(13)
#define VEC_EXCPT14	(14)
#define VEC_EXCPT15	(15)
#define VEC_STEP	(16)
#define VEC_OVFLOW	(17)
#define VEC_UNDEF_I	(33)
#define VEC_ILGAL_I	(34)
#define VEC_CPLB_VL	(35)
#define VEC_MISALI_D	(36)
#define VEC_UNCOV	(37)
#define VEC_CPLB_M	(38)
#define VEC_CPLB_MHIT	(39)
#define VEC_WATCH	(40)
#define VEC_ISTRU_VL	(41)	/*ADSP-BF535 only (MH) */
#define VEC_MISALI_I	(42)
#define VEC_CPLB_I_VL	(43)
#define VEC_CPLB_I_M	(44)
#define VEC_CPLB_I_MHIT	(45)
#define VEC_ILL_RES	(46)	/* including unvalid supervisor mode insn */

#ifndef __ASSEMBLY__

#define HWC_x2 "System MMR Error\nAn error occurred due to an invalid access to an System MMR location\nPossible reason: a 32-bit register is accessed with a 16-bit instruction,\nor a 16-bit register is accessed with a 32-bit instruction.\n"
#define HWC_x3 "External Memory Addressing Error\n"
#define HWC_x12 "Performance Monitor Overflow\n"
#define HWC_x18 "RAISE 5 instruction\n Software issued a RAISE 5 instruction to invoke the Hardware\n"
#define HWC_default "Reserved\n"

#define EXC_0x03 "Application stack overflow\n - Please increase the stack size of the application using elf2flt -s option,\n and/or reduce the stack use of the application.\n"
#define EXC_0x10 "Single step\n - When the processor is in single step mode, every instruction\n generates an exception. Primarily used for debugging.\n"
#define EXC_0x11 "Exception caused by a trace buffer full condition\n - The processor takes this exception when the trace\n buffer overflows (only when enabled by the Trace Unit Control register).\n"
#define EXC_0x21 "Undefined instruction\n - May be used to emulate instructions that are not defined for\n a particular processor implementation.\n"
#define EXC_0x22 "Illegal instruction combination\n - See section for multi-issue rules in the ADSP-BF53x Blackfin\n Processor Instruction Set Reference.\n"
#define EXC_0x23 "Data access CPLB protection violation\n - Attempted read or write to Supervisor resource,\n or illegal data memory access. \n"
#define EXC_0x24 "Data access misaligned address violation\n - Attempted misaligned data memory or data cache access.\n"
#define EXC_0x25 "Unrecoverable event\n - For example, an exception generated while processing a previous exception.\n"
#define EXC_0x26 "Data access CPLB miss\n - Used by the MMU to signal a CPLB miss on a data access.\n"
#define EXC_0x27 "Data access multiple CPLB hits\n - More than one CPLB entry matches data fetch address.\n"
#define EXC_0x28 "Program Sequencer Exception caused by an emulation watchpoint match\n - There is a watchpoint match, and one of the EMUSW\n bits in the Watchpoint Instruction Address Control register (WPIACTL) is set.\n"
#define EXC_0x2A "Instruction fetch misaligned address violation\n - Attempted misaligned instruction cache fetch. On a misaligned instruction fetch exception,\n the return address provided in RETX is the destination address which is misaligned, rather than the address of the offending instruction.\n"
#define EXC_0x2B "CPLB protection violation\n - Illegal instruction fetch access (memory protection violation).\n"
#define EXC_0x2C "Instruction fetch CPLB miss\n - CPLB miss on an instruction fetch.\n"
#define EXC_0x2D "Instruction fetch multiple CPLB hits\n - More than one CPLB entry matches instruction fetch address.\n"
#define EXC_0x2E "Illegal use of supervisor resource\n - Attempted to use a Supervisor register or instruction from User mode.\n Supervisor resources are registers and instructions that are reserved\n for Supervisor use: Supervisor only registers, all MMRs, and Supervisor\n only instructions.\n"

#endif				/* __ASSEMBLY__ */
#endif				/* _BFIN_TRAPS_H */
