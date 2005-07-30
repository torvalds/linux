#ifndef _ASM_CRIS_ARCH_CHECKSUM_H
#define _ASM_CRIS_ARCH_CHECKSUM_H

/*
 * Check values used in TCP/UDP headers.
 *
 * The gain of doing this in assembler instead of C, is that C doesn't
 * generate carry-additions for the 32-bit components of the
 * checksum. Which means it would be necessary to split all those into
 * 16-bit components and then add.
 */
extern inline unsigned int
csum_tcpudp_nofold(unsigned long saddr, unsigned long daddr,
		   unsigned short len, unsigned short proto, unsigned int sum)
{
	int res;

	__asm__ __volatile__ ("add.d %2, %0\n\t"
			      "addc %3, %0\n\t"
			      "addc %4, %0\n\t"
			      "addc 0, %0\n\t"
			      : "=r" (res)
			      : "0" (sum), "r" (daddr), "r" (saddr), \
			      "r" ((ntohs(len) << 16) + (proto << 8)));

	return res;
}

#endif /* _ASM_CRIS_ARCH_CHECKSUM_H */
