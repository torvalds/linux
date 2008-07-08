/*
 * msi.h:  Defines specific to the MBus - Sbus - Interface.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost   (ecd@skynet.be)
 */

#ifndef _SPARC_MSI_H
#define _SPARC_MSI_H

/*
 * Locations of MSI Registers.
 */
#define MSI_MBUS_ARBEN	0xe0001008	/* MBus Arbiter Enable register */

/*
 * Useful bits in the MSI Registers.
 */
#define MSI_ASYNC_MODE  0x80000000	/* Operate the MSI asynchronously */


static inline void msi_set_sync(void)
{
	__asm__ __volatile__ ("lda [%0] %1, %%g3\n\t"
			      "andn %%g3, %2, %%g3\n\t"
			      "sta %%g3, [%0] %1\n\t" : :
			      "r" (MSI_MBUS_ARBEN),
			      "i" (ASI_M_CTL), "r" (MSI_ASYNC_MODE) : "g3");
}

#endif /* !(_SPARC_MSI_H) */
