/* $OpenBSD: vmparam.h,v 1.4 2010/12/31 21:38:08 miod Exp $ */
/* public domain */
/*
 * Physical memory is mapped 1:1 at the bottom of the supervisor address
 * space. Kernel virtual memory space starts from the end of physical memory,
 * up to the on-board devices appearing all over the last 3GB of address space.
 */
#define VM_MIN_KERNEL_ADDRESS	((vaddr_t)0x00000000)
#define VM_MAX_KERNEL_ADDRESS	((vaddr_t)0x40000000)

#include <m88k/vmparam.h>
