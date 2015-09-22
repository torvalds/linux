/*
 * Copied from the kernel sources:
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _TOOLS_LINUX_ASM_POWERPC_BARRIER_H
#define _TOOLS_LINUX_ASM_POWERPC_BARRIER_H

/*
 * Memory barrier.
 * The sync instruction guarantees that all memory accesses initiated
 * by this processor have been performed (with respect to all other
 * mechanisms that access memory).  The eieio instruction is a barrier
 * providing an ordering (separately) for (a) cacheable stores and (b)
 * loads and stores to non-cacheable memory (e.g. I/O devices).
 *
 * mb() prevents loads and stores being reordered across this point.
 * rmb() prevents loads being reordered across this point.
 * wmb() prevents stores being reordered across this point.
 *
 * *mb() variants without smp_ prefix must order all types of memory
 * operations with one another. sync is the only instruction sufficient
 * to do this.
 */
#define mb()   __asm__ __volatile__ ("sync" : : : "memory")
#define rmb()  __asm__ __volatile__ ("sync" : : : "memory")
#define wmb()  __asm__ __volatile__ ("sync" : : : "memory")

#endif /* _TOOLS_LINUX_ASM_POWERPC_BARRIER_H */
