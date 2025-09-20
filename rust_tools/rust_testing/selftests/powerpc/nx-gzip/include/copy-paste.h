/* SPDX-License-Identifier: GPL-2.0-or-later */

/* From asm-compat.h */
#define __stringify_in_c(...)	#__VA_ARGS__
#define stringify_in_c(...)	__stringify_in_c(__VA_ARGS__) " "

/*
 * Macros taken from arch/powerpc/include/asm/ppc-opcode.h and other
 * header files.
 */
#define ___PPC_RA(a)    (((a) & 0x1f) << 16)
#define ___PPC_RB(b)    (((b) & 0x1f) << 11)

#define PPC_INST_COPY                   0x7c20060c
#define PPC_INST_PASTE                  0x7c20070d

#define PPC_COPY(a, b)          stringify_in_c(.long PPC_INST_COPY | \
						___PPC_RA(a) | ___PPC_RB(b))
#define PPC_PASTE(a, b)         stringify_in_c(.long PPC_INST_PASTE | \
						___PPC_RA(a) | ___PPC_RB(b))
#define CR0_SHIFT	28
#define CR0_MASK	0xF
/*
 * Copy/paste instructions:
 *
 *	copy RA,RB
 *		Copy contents of address (RA) + effective_address(RB)
 *		to internal copy-buffer.
 *
 *	paste RA,RB
 *		Paste contents of internal copy-buffer to the address
 *		(RA) + effective_address(RB)
 */
static inline int vas_copy(void *crb, int offset)
{
	asm volatile(PPC_COPY(%0, %1)";"
		:
		: "b" (offset), "b" (crb)
		: "memory");

	return 0;
}

static inline int vas_paste(void *paste_address, int offset)
{
	__u32 cr;

	cr = 0;
	asm volatile(PPC_PASTE(%1, %2)";"
		"mfocrf %0, 0x80;"
		: "=r" (cr)
		: "b" (offset), "b" (paste_address)
		: "memory", "cr0");

	return (cr >> CR0_SHIFT) & CR0_MASK;
}
