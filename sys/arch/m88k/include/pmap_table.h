/*	$OpenBSD: pmap_table.h,v 1.5 2013/11/16 18:45:20 miod Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1992 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#ifndef _M88K_PMAP_TABLE_H_
#define _M88K_PMAP_TABLE_H_

/*
 * Built-in mappings list.
 * An entry is considered invalid if size = 0, and
 * end of list is indicated by size 0xffffffff
 */
struct pmap_table {
	paddr_t		start;
	psize_t		size;
	vm_prot_t	prot;
	unsigned int	cacheability;
	boolean_t	may_use_batc;
};

const struct pmap_table *pmap_table_build(void);

#endif	/* _M88K_PMAP_TABLE_H_ */
