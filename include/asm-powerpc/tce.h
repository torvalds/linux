/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * Rewrite, cleanup:
 * Copyright (C) 2004 Olof Johansson <olof@austin.ibm.com>, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _ASM_POWERPC_TCE_H
#define _ASM_POWERPC_TCE_H

/*
 * Tces come in two formats, one for the virtual bus and a different
 * format for PCI
 */
#define TCE_VB  0
#define TCE_PCI 1

/* TCE page size is 4096 bytes (1 << 12) */

#define TCE_SHIFT	12
#define TCE_PAGE_SIZE	(1 << TCE_SHIFT)
#define TCE_PAGE_FACTOR	(PAGE_SHIFT - TCE_SHIFT)


/* tce_entry
 * Used by pSeries (SMP) and iSeries/pSeries LPAR, but there it's
 * abstracted so layout is irrelevant.
 */
union tce_entry {
   	unsigned long te_word;
	struct {
		unsigned int  tb_cacheBits :6;	/* Cache hash bits - not used */
		unsigned int  tb_rsvd      :6;
		unsigned long tb_rpn       :40;	/* Real page number */
		unsigned int  tb_valid     :1;	/* Tce is valid (vb only) */
		unsigned int  tb_allio     :1;	/* Tce is valid for all lps (vb only) */
		unsigned int  tb_lpindex   :8;	/* LpIndex for user of TCE (vb only) */
		unsigned int  tb_pciwr     :1;	/* Write allowed (pci only) */
		unsigned int  tb_rdwr      :1;	/* Read allowed  (pci), Write allowed (vb) */
	} te_bits;
#define te_cacheBits te_bits.tb_cacheBits
#define te_rpn       te_bits.tb_rpn
#define te_valid     te_bits.tb_valid
#define te_allio     te_bits.tb_allio
#define te_lpindex   te_bits.tb_lpindex
#define te_pciwr     te_bits.tb_pciwr
#define te_rdwr      te_bits.tb_rdwr
};


#endif /* _ASM_POWERPC_TCE_H */
