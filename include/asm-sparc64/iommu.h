/* $Id: iommu.h,v 1.10 2001/03/08 09:55:56 davem Exp $
 * iommu.h: Definitions for the sun5 IOMMU.
 *
 * Copyright (C) 1996, 1999 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC64_IOMMU_H
#define _SPARC64_IOMMU_H

/* The format of an iopte in the page tables. */
#define IOPTE_VALID   0x8000000000000000UL /* IOPTE is valid                  */
#define IOPTE_64K     0x2000000000000000UL /* IOPTE is for 64k page           */
#define IOPTE_STBUF   0x1000000000000000UL /* DVMA can use streaming buffer   */
#define IOPTE_INTRA   0x0800000000000000UL /* SBUS slot-->slot direct transfer*/
#define IOPTE_CONTEXT 0x07ff800000000000UL /* Context number		      */
#define IOPTE_PAGE    0x00007fffffffe000UL /* Physical page number (PA[42:13])*/
#define IOPTE_CACHE   0x0000000000000010UL /* Cached (in UPA E-cache)         */
#define IOPTE_WRITE   0x0000000000000002UL /* Writeable                       */

#endif /* !(_SPARC_IOMMU_H) */
