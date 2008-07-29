#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <asm/processor.h>
#include <asm/i387.h>

#define XSTATE_FP	0x1
#define XSTATE_SSE	0x2

#define XSTATE_FPSSE	(XSTATE_FP | XSTATE_SSE)

#define FXSAVE_SIZE	512

/*
 * These are the features that the OS can handle currently.
 */
#define XCNTXT_LMASK	(XSTATE_FP | XSTATE_SSE)
#define XCNTXT_HMASK	0x0

extern unsigned int xstate_size, pcntxt_hmask, pcntxt_lmask;
extern struct xsave_struct *init_xstate_buf;

extern void xsave_cntxt_init(void);
extern void xsave_init(void);

#endif
