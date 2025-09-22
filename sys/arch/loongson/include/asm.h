/* $OpenBSD: asm.h,v 1.2 2016/11/18 15:38:14 visa Exp $ */
/* public domain */

#ifdef MULTIPROCESSOR
#define HW_GET_CPU_INFO(ci, tmp)	\
	dmfc0	ci, COP_0_ERROR_PC
#endif

#include <mips64/asm.h>
