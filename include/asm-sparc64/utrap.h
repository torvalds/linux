/* $Id: utrap.h,v 1.1 1997/10/14 16:21:31 jj Exp $
 * include/asm-sparc64/utrap.h
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#ifndef __ASM_SPARC64_UTRAP_H
#define __ASM_SPARC64_UTRAP_H

#define UT_INSTRUCTION_EXCEPTION		1
#define UT_INSTRUCTION_ERROR			2
#define UT_INSTRUCTION_PROTECTION		3
#define UT_ILLTRAP_INSTRUCTION			4
#define UT_ILLEGAL_INSTRUCTION			5
#define UT_PRIVILEGED_OPCODE			6
#define UT_FP_DISABLED				7
#define UT_FP_EXCEPTION_IEEE_754		8
#define UT_FP_EXCEPTION_OTHER			9
#define UT_TAG_OVERVIEW				10
#define UT_DIVISION_BY_ZERO			11
#define UT_DATA_EXCEPTION			12
#define UT_DATA_ERROR				13
#define UT_DATA_PROTECTION			14
#define UT_MEM_ADDRESS_NOT_ALIGNED		15
#define UT_PRIVILEGED_ACTION			16
#define UT_ASYNC_DATA_ERROR			17
#define UT_TRAP_INSTRUCTION_16			18
#define UT_TRAP_INSTRUCTION_17			19
#define UT_TRAP_INSTRUCTION_18			20
#define UT_TRAP_INSTRUCTION_19			21
#define UT_TRAP_INSTRUCTION_20			22
#define UT_TRAP_INSTRUCTION_21			23
#define UT_TRAP_INSTRUCTION_22			24
#define UT_TRAP_INSTRUCTION_23			25
#define UT_TRAP_INSTRUCTION_24			26
#define UT_TRAP_INSTRUCTION_25			27
#define UT_TRAP_INSTRUCTION_26			28
#define UT_TRAP_INSTRUCTION_27			29
#define UT_TRAP_INSTRUCTION_28			30
#define UT_TRAP_INSTRUCTION_29			31
#define UT_TRAP_INSTRUCTION_30			32
#define UT_TRAP_INSTRUCTION_31			33

#define	UTH_NOCHANGE				(-1)

#ifndef __ASSEMBLY__ 
typedef int utrap_entry_t;
typedef void *utrap_handler_t;
#endif /* __ASSEMBLY__ */

#endif /* !(__ASM_SPARC64_PROCESSOR_H) */
