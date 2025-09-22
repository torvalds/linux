/*	$OpenBSD: unalign.c,v 1.2 2008/07/26 10:25:04 miod Exp $	*/
/* Written by Miod Vallat, 2004 AD -- this file is in the public domain */

/*
 * This test checks for the ability, for 32 bit systems, to correctly
 * access a long long (64 bit) value aligned on a 32 bit boundary, but not
 * on a 64 bit boundary.
 *
 * All architectures should pass this test; on m88k this requires assistance
 * from the kernel to recover from the misaligned operand exception: see
 * double_reg_fixup() in arch/m88k/m88k/trap.c for details.
 */

#include <stdio.h>
#include <sys/types.h>

uint32_t array[5];

int
unalign_read(uint64_t *addr)
{
	uint64_t t;

	t = *addr;
#if BYTE_ORDER == BIG_ENDIAN
	if (t != 0x13579aceffffabcdULL)
#else
	if (t != 0xffffabcd13579aceULL)
#endif
		return (1);

	return (0);
}

void
unalign_write(uint64_t *addr)
{
	uint64_t t;

	t = 0xdeadbeaffadebabeULL;
	*addr = t;
}

int
main(int argc, char *argv[])
{
#if !defined(__LP64__)
	uint32_t *addr = array;

	/* align on a 64 bit boundary */
	if (((uint32_t)addr & 7) != 0)
		addr++;

	addr[0] = 0x12345678;
	addr[1] = 0x13579ace;
	addr[2] = 0xffffabcd;
	addr[3] = 0x2468fedc;

	if (unalign_read((uint64_t *)(addr + 1)))
		return (1);

	unalign_write((uint64_t *)(addr + 1));

#if BYTE_ORDER == BIG_ENDIAN
	if (addr[1] != 0xdeadbeaf || addr[2] != 0xfadebabe)
#else
	if (addr[1] != 0xfadebabe || addr[2] != 0xdeadbeaf)
#endif
		return (1);
#endif	/* __LP64__ */

	return (0);
}
