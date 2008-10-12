#ifndef ASM_X86__BIOS_EBDA_H
#define ASM_X86__BIOS_EBDA_H

#include <asm/io.h>

/*
 * there is a real-mode segmented pointer pointing to the
 * 4K EBDA area at 0x40E.
 */
static inline unsigned int get_bios_ebda(void)
{
	unsigned int address = *(unsigned short *)phys_to_virt(0x40E);
	address <<= 4;
	return address;	/* 0 means none */
}

void reserve_ebda_region(void);

#endif /* ASM_X86__BIOS_EBDA_H */
