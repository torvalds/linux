/* $OpenBSD: asm.h,v 1.1 2010/09/20 06:32:30 syuu Exp $ */
/* public domain */

#ifdef MULTIPROCESSOR
#define HW_GET_CPU_INFO(ci, tmp)	\
	dmfc0	ci, COP_0_ERROR_PC
#endif

#include <mips64/asm.h>
