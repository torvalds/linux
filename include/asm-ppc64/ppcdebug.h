#ifndef __PPCDEBUG_H
#define __PPCDEBUG_H
/********************************************************************
 * Author: Adam Litke, IBM Corp
 * (c) 2001
 *
 * This file contains definitions and macros for a runtime debugging
 * system for ppc64 (This should also work on 32 bit with a few    
 * adjustments.                                                   
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 ********************************************************************/

#include <linux/config.h>
#include <linux/types.h>
#include <asm/udbg.h>
#include <stdarg.h>

#define PPCDBG_BITVAL(X)     ((1UL)<<((unsigned long)(X)))

/* Defined below are the bit positions of various debug flags in the
 * ppc64_debug_switch variable.
 * -- When adding new values, please enter them into trace names below -- 
 *
 * Values 62 & 63 can be used to stress the hardware page table management
 * code.  They must be set statically, any attempt to change them dynamically
 * would be a very bad idea.
 */
#define PPCDBG_MMINIT        PPCDBG_BITVAL(0)
#define PPCDBG_MM            PPCDBG_BITVAL(1)
#define PPCDBG_SYS32         PPCDBG_BITVAL(2)
#define PPCDBG_SYS32NI       PPCDBG_BITVAL(3)
#define PPCDBG_SYS32X	     PPCDBG_BITVAL(4)
#define PPCDBG_SYS32M	     PPCDBG_BITVAL(5)
#define PPCDBG_SYS64         PPCDBG_BITVAL(6)
#define PPCDBG_SYS64NI       PPCDBG_BITVAL(7)
#define PPCDBG_SYS64X	     PPCDBG_BITVAL(8)
#define PPCDBG_SIGNAL        PPCDBG_BITVAL(9)
#define PPCDBG_SIGNALXMON    PPCDBG_BITVAL(10)
#define PPCDBG_BINFMT32      PPCDBG_BITVAL(11)
#define PPCDBG_BINFMT64      PPCDBG_BITVAL(12)
#define PPCDBG_BINFMTXMON    PPCDBG_BITVAL(13)
#define PPCDBG_BINFMT_32ADDR PPCDBG_BITVAL(14)
#define PPCDBG_ALIGNFIXUP    PPCDBG_BITVAL(15)
#define PPCDBG_TCEINIT       PPCDBG_BITVAL(16)
#define PPCDBG_TCE           PPCDBG_BITVAL(17)
#define PPCDBG_PHBINIT       PPCDBG_BITVAL(18)
#define PPCDBG_SMP           PPCDBG_BITVAL(19)
#define PPCDBG_BOOT          PPCDBG_BITVAL(20)
#define PPCDBG_BUSWALK       PPCDBG_BITVAL(21)
#define PPCDBG_PROM	     PPCDBG_BITVAL(22)
#define PPCDBG_RTAS	     PPCDBG_BITVAL(23)
#define PPCDBG_HTABSTRESS    PPCDBG_BITVAL(62)
#define PPCDBG_HTABSIZE      PPCDBG_BITVAL(63)
#define PPCDBG_NONE          (0UL)
#define PPCDBG_ALL           (0xffffffffUL)

/* The default initial value for the debug switch */
#define PPC_DEBUG_DEFAULT    0 
/* #define PPC_DEBUG_DEFAULT    PPCDBG_ALL        */

#define PPCDBG_NUM_FLAGS     64

extern u64 ppc64_debug_switch;

#ifdef WANT_PPCDBG_TAB
/* A table of debug switch names to allow name lookup in xmon 
 * (and whoever else wants it.
 */
char *trace_names[PPCDBG_NUM_FLAGS] = {
	/* Known debug names */
	"mminit", 	"mm",
	"syscall32", 	"syscall32_ni", "syscall32x",	"syscall32m",
	"syscall64", 	"syscall64_ni", "syscall64x",
	"signal",	"signal_xmon",
	"binfmt32",	"binfmt64",	"binfmt_xmon",	"binfmt_32addr",
	"alignfixup",   "tceinit",      "tce",          "phb_init",     
	"smp",          "boot",         "buswalk",	"prom",
	"rtas"
};
#else
extern char *trace_names[64];
#endif /* WANT_PPCDBG_TAB */

#ifdef CONFIG_PPCDBG
/* Macro to conditionally print debug based on debug_switch */
#define PPCDBG(...) udbg_ppcdbg(__VA_ARGS__)

/* Macro to conditionally call a debug routine based on debug_switch */
#define PPCDBGCALL(FLAGS,FUNCTION) ifppcdebug(FLAGS) FUNCTION

/* Macros to test for debug states */
#define ifppcdebug(FLAGS) if (udbg_ifdebug(FLAGS))
#define ppcdebugset(FLAGS) (udbg_ifdebug(FLAGS))
#define PPCDBG_BINFMT (test_thread_flag(TIF_32BIT) ? PPCDBG_BINFMT32 : PPCDBG_BINFMT64)

#else
#define PPCDBG(...) do {;} while (0)
#define PPCDBGCALL(FLAGS,FUNCTION) do {;} while (0)
#define ifppcdebug(...) if (0)
#define ppcdebugset(FLAGS) (0)
#endif /* CONFIG_PPCDBG */

#endif /*__PPCDEBUG_H */
