/*-
 * Copyright (c) 2015 Ian Lepore <ian@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_DEV_OFW_OFW_SUBR_H_
#define	_DEV_OFW_OFW_SUBR_H_

/*
 * Translate an address from the Nth tuple of a device node's reg properties to
 * a physical memory address, by applying the range mappings from all ancestors.
 * This assumes that all ancestor ranges are simple numerical offsets for which
 * addition and subtraction operations will perform the required mapping (the
 * bit-options in the high word of standard PCI properties are also handled).
 * After the call, *pci_hi (if non-NULL) contains the phys.hi cell of the
 * device's parent PCI bus, or OFW_PADDR_NOT_PCI if no PCI bus is involved.
 *
 * This is intended to be a helper function called by the platform-specific
 * implementation of OF_decode_addr(), and not for direct use by device drivers.
 */
#define	OFW_PADDR_NOT_PCI	(~0)

int ofw_reg_to_paddr(phandle_t _dev, int _regno, bus_addr_t *_paddr,
    bus_size_t *_size, pcell_t *_pci_hi);

int ofw_parse_bootargs(void);

#endif
