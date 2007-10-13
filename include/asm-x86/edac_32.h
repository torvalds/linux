#ifndef ASM_EDAC_H
#define ASM_EDAC_H

/* ECC atomic, DMA, SMP and interrupt safe scrub function */

static __inline__ void atomic_scrub(void *va, u32 size)
{
	unsigned long *virt_addr = va;
	u32 i;

	for (i = 0; i < size / 4; i++, virt_addr++)
		/* Very carefully read and write to memory atomically
		 * so we are interrupt, DMA and SMP safe.
		 */
		__asm__ __volatile__("lock; addl $0, %0"::"m"(*virt_addr));
}

#endif
