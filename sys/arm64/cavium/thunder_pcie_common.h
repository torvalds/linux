/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _CAVIUM_THUNDER_PCIE_COMMON_H_
#define	_CAVIUM_THUNDER_PCIE_COMMON_H_

DECLARE_CLASS(thunder_pcie_driver);
DECLARE_CLASS(thunder_pem_driver);

MALLOC_DECLARE(M_THUNDER_PCIE);

uint32_t range_addr_is_pci(struct pcie_range *, uint64_t, uint64_t);
uint32_t range_addr_is_phys(struct pcie_range *, uint64_t, uint64_t);
uint64_t range_addr_phys_to_pci(struct pcie_range *, uint64_t);
uint64_t range_addr_pci_to_phys(struct pcie_range *, uint64_t);

int thunder_pcie_identify_ecam(device_t, int *);
#ifdef THUNDERX_PASS_1_1_ERRATA
struct resource *thunder_pcie_alloc_resource(device_t,
    device_t, int, int *, rman_res_t, rman_res_t, rman_res_t, u_int);
#endif

#endif /* _CAVIUM_THUNDER_PCIE_COMMON_H_ */
