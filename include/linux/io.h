/*
 * Copyright 2006 PathScale, Inc.  All Rights Reserved.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef _LINUX_IO_H
#define _LINUX_IO_H

#include <linux/types.h>
#include <asm/io.h>
#include <asm/page.h>

void __iowrite32_copy(void __iomem *to, const void *from, size_t count);
void __iowrite64_copy(void __iomem *to, const void *from, size_t count);

int ioremap_page_range(unsigned long addr, unsigned long end,
		       unsigned long phys_addr, pgprot_t prot);

/**
 *	check_signature		-	find BIOS signatures
 *	@io_addr: mmio address to check
 *	@signature:  signature block
 *	@length: length of signature
 *
 *	Perform a signature comparison with the mmio address io_addr. This
 *	address should have been obtained by ioremap.
 *	Returns 1 on a match.
 */

static inline int check_signature(const volatile void __iomem *io_addr,
	const unsigned char *signature, int length)
{
	int retval = 0;
	do {
		if (readb(io_addr) != *signature)
			goto out;
		io_addr++;
		signature++;
		length--;
	} while (length);
	retval = 1;
out:
	return retval;
}

#endif /* _LINUX_IO_H */
