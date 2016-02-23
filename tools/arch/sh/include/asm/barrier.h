/*
 * Copied from the kernel sources:
 *
 * Copyright (C) 1999, 2000  Niibe Yutaka  &  Kaz Kojima
 * Copyright (C) 2002 Paul Mundt
 */
#ifndef __TOOLS_LINUX_ASM_SH_BARRIER_H
#define __TOOLS_LINUX_ASM_SH_BARRIER_H

/*
 * A brief note on ctrl_barrier(), the control register write barrier.
 *
 * Legacy SH cores typically require a sequence of 8 nops after
 * modification of a control register in order for the changes to take
 * effect. On newer cores (like the sh4a and sh5) this is accomplished
 * with icbi.
 *
 * Also note that on sh4a in the icbi case we can forego a synco for the
 * write barrier, as it's not necessary for control registers.
 *
 * Historically we have only done this type of barrier for the MMUCR, but
 * it's also necessary for the CCR, so we make it generic here instead.
 */
#if defined(__SH4A__) || defined(__SH5__)
#define mb()		__asm__ __volatile__ ("synco": : :"memory")
#define rmb()		mb()
#define wmb()		mb()
#endif

#include <asm-generic/barrier.h>

#endif /* __TOOLS_LINUX_ASM_SH_BARRIER_H */
