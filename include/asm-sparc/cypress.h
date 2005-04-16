/* $Id: cypress.h,v 1.6 1996/08/29 09:48:09 davem Exp $
 * cypress.h: Cypress module specific definitions and defines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_CYPRESS_H
#define _SPARC_CYPRESS_H

/* Cypress chips have %psr 'impl' of '0001' and 'vers' of '0001'. */

/* The MMU control register fields on the Sparc Cypress 604/605 MMU's.
 *
 * ---------------------------------------------------------------
 * |implvers| MCA | MCM |MV| MID |BM| C|RSV|MR|CM|CL|CE|RSV|NF|ME|
 * ---------------------------------------------------------------
 *  31    24 23-22 21-20 19 18-15 14 13  12 11 10  9  8 7-2  1  0
 *
 * MCA: MultiChip Access -- Used for configuration of multiple
 *      CY7C604/605 cache units.
 * MCM: MultiChip Mask -- Again, for multiple cache unit config.
 * MV: MultiChip Valid -- Indicates MCM and MCA have valid settings.
 * MID: ModuleID -- Unique processor ID for MBus transactions. (605 only)
 * BM: Boot Mode -- 0 = not in boot mode, 1 = in boot mode
 * C: Cacheable -- Indicates whether accesses are cacheable while
 *    the MMU is off.  0=no 1=yes
 * MR: MemoryReflection -- Indicates whether the bus attached to the
 *     MBus supports memory reflection. 0=no 1=yes (605 only)
 * CM: CacheMode -- Indicates whether the cache is operating in write
 *     through or copy-back mode. 0=write-through 1=copy-back
 * CL: CacheLock -- Indicates if the entire cache is locked or not.
 *     0=not-locked 1=locked  (604 only)
 * CE: CacheEnable -- Is the virtual cache on? 0=no 1=yes
 * NF: NoFault -- Do faults generate traps? 0=yes 1=no
 * ME: MmuEnable -- Is the MMU doing translations? 0=no 1=yes
 */

#define CYPRESS_MCA       0x00c00000
#define CYPRESS_MCM       0x00300000
#define CYPRESS_MVALID    0x00080000
#define CYPRESS_MIDMASK   0x00078000   /* Only on 605 */
#define CYPRESS_BMODE     0x00004000
#define CYPRESS_ACENABLE  0x00002000
#define CYPRESS_MRFLCT    0x00000800   /* Only on 605 */
#define CYPRESS_CMODE     0x00000400
#define CYPRESS_CLOCK     0x00000200   /* Only on 604 */
#define CYPRESS_CENABLE   0x00000100
#define CYPRESS_NFAULT    0x00000002
#define CYPRESS_MENABLE   0x00000001

extern __inline__ void cypress_flush_page(unsigned long page)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
			     "r" (page), "i" (ASI_M_FLUSH_PAGE));
}

extern __inline__ void cypress_flush_segment(unsigned long addr)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
			     "r" (addr), "i" (ASI_M_FLUSH_SEG));
}

extern __inline__ void cypress_flush_region(unsigned long addr)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t" : :
			     "r" (addr), "i" (ASI_M_FLUSH_REGION));
}

extern __inline__ void cypress_flush_context(void)
{
	__asm__ __volatile__("sta %%g0, [%%g0] %0\n\t" : :
			     "i" (ASI_M_FLUSH_CTX));
}

/* XXX Displacement flushes for buggy chips and initial testing
 * XXX go here.
 */

#endif /* !(_SPARC_CYPRESS_H) */
