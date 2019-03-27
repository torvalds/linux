/***********************license start***************
 * Copyright (c) 2003-2012  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/


/**
 * cvmx-uahcx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon uahcx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_UAHCX_DEFS_H__
#define __CVMX_UAHCX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_ASYNCLISTADDR(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_ASYNCLISTADDR(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000028ull);
}
#else
#define CVMX_UAHCX_EHCI_ASYNCLISTADDR(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_CONFIGFLAG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_CONFIGFLAG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000050ull);
}
#else
#define CVMX_UAHCX_EHCI_CONFIGFLAG(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000050ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_CTRLDSSEGMENT(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_CTRLDSSEGMENT(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000020ull);
}
#else
#define CVMX_UAHCX_EHCI_CTRLDSSEGMENT(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_FRINDEX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_FRINDEX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F000000001Cull);
}
#else
#define CVMX_UAHCX_EHCI_FRINDEX(block_id) (CVMX_ADD_IO_SEG(0x00016F000000001Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_HCCAPBASE(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_HCCAPBASE(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000000ull);
}
#else
#define CVMX_UAHCX_EHCI_HCCAPBASE(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_HCCPARAMS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_HCCPARAMS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000008ull);
}
#else
#define CVMX_UAHCX_EHCI_HCCPARAMS(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_HCSPARAMS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_HCSPARAMS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000004ull);
}
#else
#define CVMX_UAHCX_EHCI_HCSPARAMS(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000004ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_INSNREG00(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_INSNREG00(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000090ull);
}
#else
#define CVMX_UAHCX_EHCI_INSNREG00(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000090ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_INSNREG03(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_INSNREG03(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F000000009Cull);
}
#else
#define CVMX_UAHCX_EHCI_INSNREG03(block_id) (CVMX_ADD_IO_SEG(0x00016F000000009Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_INSNREG04(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_INSNREG04(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F00000000A0ull);
}
#else
#define CVMX_UAHCX_EHCI_INSNREG04(block_id) (CVMX_ADD_IO_SEG(0x00016F00000000A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_INSNREG06(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_INSNREG06(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F00000000E8ull);
}
#else
#define CVMX_UAHCX_EHCI_INSNREG06(block_id) (CVMX_ADD_IO_SEG(0x00016F00000000E8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_INSNREG07(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_INSNREG07(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F00000000ECull);
}
#else
#define CVMX_UAHCX_EHCI_INSNREG07(block_id) (CVMX_ADD_IO_SEG(0x00016F00000000ECull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_PERIODICLISTBASE(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_PERIODICLISTBASE(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000024ull);
}
#else
#define CVMX_UAHCX_EHCI_PERIODICLISTBASE(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000024ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_PORTSCX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((((offset >= 1) && (offset <= 2))) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((((offset >= 1) && (offset <= 2))) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((((offset >= 1) && (offset <= 2))) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((((offset >= 1) && (offset <= 2))) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((((offset >= 1) && (offset <= 2))) && ((block_id == 0))))))
		cvmx_warn("CVMX_UAHCX_EHCI_PORTSCX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000050ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 4;
}
#else
#define CVMX_UAHCX_EHCI_PORTSCX(offset, block_id) (CVMX_ADD_IO_SEG(0x00016F0000000050ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_USBCMD(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_USBCMD(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000010ull);
}
#else
#define CVMX_UAHCX_EHCI_USBCMD(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_USBINTR(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_USBINTR(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000018ull);
}
#else
#define CVMX_UAHCX_EHCI_USBINTR(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_EHCI_USBSTS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_EHCI_USBSTS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000014ull);
}
#else
#define CVMX_UAHCX_EHCI_USBSTS(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000014ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCBULKCURRENTED(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCBULKCURRENTED(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F000000042Cull);
}
#else
#define CVMX_UAHCX_OHCI0_HCBULKCURRENTED(block_id) (CVMX_ADD_IO_SEG(0x00016F000000042Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCBULKHEADED(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCBULKHEADED(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000428ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCBULKHEADED(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000428ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCCOMMANDSTATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCCOMMANDSTATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000408ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCCOMMANDSTATUS(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000408ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCCONTROL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCCONTROL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000404ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCCONTROL(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000404ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCCONTROLCURRENTED(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCCONTROLCURRENTED(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000424ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCCONTROLCURRENTED(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000424ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCCONTROLHEADED(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCCONTROLHEADED(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000420ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCCONTROLHEADED(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000420ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCDONEHEAD(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCDONEHEAD(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000430ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCDONEHEAD(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000430ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCFMINTERVAL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCFMINTERVAL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000434ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCFMINTERVAL(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000434ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCFMNUMBER(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCFMNUMBER(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F000000043Cull);
}
#else
#define CVMX_UAHCX_OHCI0_HCFMNUMBER(block_id) (CVMX_ADD_IO_SEG(0x00016F000000043Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCFMREMAINING(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCFMREMAINING(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000438ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCFMREMAINING(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000438ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCHCCA(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCHCCA(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000418ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCHCCA(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000418ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCINTERRUPTDISABLE(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCINTERRUPTDISABLE(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000414ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCINTERRUPTDISABLE(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000414ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCINTERRUPTENABLE(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCINTERRUPTENABLE(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000410ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCINTERRUPTENABLE(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000410ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCINTERRUPTSTATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCINTERRUPTSTATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F000000040Cull);
}
#else
#define CVMX_UAHCX_OHCI0_HCINTERRUPTSTATUS(block_id) (CVMX_ADD_IO_SEG(0x00016F000000040Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCLSTHRESHOLD(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCLSTHRESHOLD(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000444ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCLSTHRESHOLD(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000444ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCPERIODCURRENTED(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCPERIODCURRENTED(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F000000041Cull);
}
#else
#define CVMX_UAHCX_OHCI0_HCPERIODCURRENTED(block_id) (CVMX_ADD_IO_SEG(0x00016F000000041Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCPERIODICSTART(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCPERIODICSTART(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000440ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCPERIODICSTART(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000440ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCREVISION(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCREVISION(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000400ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCREVISION(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000400ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCRHDESCRIPTORA(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCRHDESCRIPTORA(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000448ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCRHDESCRIPTORA(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000448ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCRHDESCRIPTORB(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCRHDESCRIPTORB(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F000000044Cull);
}
#else
#define CVMX_UAHCX_OHCI0_HCRHDESCRIPTORB(block_id) (CVMX_ADD_IO_SEG(0x00016F000000044Cull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCRHPORTSTATUSX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((((offset >= 1) && (offset <= 2))) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((((offset >= 1) && (offset <= 2))) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((((offset >= 1) && (offset <= 2))) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((((offset >= 1) && (offset <= 2))) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((((offset >= 1) && (offset <= 2))) && ((block_id == 0))))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCRHPORTSTATUSX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000450ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 4;
}
#else
#define CVMX_UAHCX_OHCI0_HCRHPORTSTATUSX(offset, block_id) (CVMX_ADD_IO_SEG(0x00016F0000000450ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 4)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_HCRHSTATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_HCRHSTATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000450ull);
}
#else
#define CVMX_UAHCX_OHCI0_HCRHSTATUS(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000450ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_INSNREG06(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_INSNREG06(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F0000000498ull);
}
#else
#define CVMX_UAHCX_OHCI0_INSNREG06(block_id) (CVMX_ADD_IO_SEG(0x00016F0000000498ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_UAHCX_OHCI0_INSNREG07(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_UAHCX_OHCI0_INSNREG07(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00016F000000049Cull);
}
#else
#define CVMX_UAHCX_OHCI0_INSNREG07(block_id) (CVMX_ADD_IO_SEG(0x00016F000000049Cull))
#endif

/**
 * cvmx_uahc#_ehci_asynclistaddr
 *
 * ASYNCLISTADDR = Current Asynchronous List Address Register
 *
 * This 32-bit register contains the address of the next asynchronous queue head to be executed. If the host
 * controller is in 64-bit mode (as indicated by a one in 64-bit Addressing Capability field in the
 * HCCPARAMS register), then the most significant 32 bits of every control data structure address comes from
 * the CTRLDSSEGMENT register (See Section 2.3.5). Bits [4:0] of this register cannot be modified by system
 * software and will always return a zero when read. The memory structure referenced by this physical memory
 * pointer is assumed to be 32-byte (cache line) aligned.
 */
union cvmx_uahcx_ehci_asynclistaddr {
	uint32_t u32;
	struct cvmx_uahcx_ehci_asynclistaddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t lpl                          : 27; /**< Link Pointer Low (LPL). These bits correspond to memory address signals [31:5],
                                                         respectively. This field may only reference a Queue Head (QH). */
	uint32_t reserved_0_4                 : 5;
#else
	uint32_t reserved_0_4                 : 5;
	uint32_t lpl                          : 27;
#endif
	} s;
	struct cvmx_uahcx_ehci_asynclistaddr_s cn61xx;
	struct cvmx_uahcx_ehci_asynclistaddr_s cn63xx;
	struct cvmx_uahcx_ehci_asynclistaddr_s cn63xxp1;
	struct cvmx_uahcx_ehci_asynclistaddr_s cn66xx;
	struct cvmx_uahcx_ehci_asynclistaddr_s cn68xx;
	struct cvmx_uahcx_ehci_asynclistaddr_s cn68xxp1;
	struct cvmx_uahcx_ehci_asynclistaddr_s cnf71xx;
};
typedef union cvmx_uahcx_ehci_asynclistaddr cvmx_uahcx_ehci_asynclistaddr_t;

/**
 * cvmx_uahc#_ehci_configflag
 *
 * CONFIGFLAG = Configure Flag Register
 * This register is in the auxiliary power well. It is only reset by hardware when the auxiliary power is initially
 * applied or in response to a host controller reset.
 */
union cvmx_uahcx_ehci_configflag {
	uint32_t u32;
	struct cvmx_uahcx_ehci_configflag_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_1_31                : 31;
	uint32_t cf                           : 1;  /**< Configure Flag (CF) .Host software sets this bit as the last action in
                                                         its process of configuring the Host Controller (see Section 4.1). This bit controls the
                                                         default port-routing control logic. Bit values and side-effects are listed below.
                                                          0b: Port routing control logic default-routes each port to an implementation
                                                              dependent classic host controller.
                                                          1b: Port routing control logic default-routes all ports to this host controller. */
#else
	uint32_t cf                           : 1;
	uint32_t reserved_1_31                : 31;
#endif
	} s;
	struct cvmx_uahcx_ehci_configflag_s   cn61xx;
	struct cvmx_uahcx_ehci_configflag_s   cn63xx;
	struct cvmx_uahcx_ehci_configflag_s   cn63xxp1;
	struct cvmx_uahcx_ehci_configflag_s   cn66xx;
	struct cvmx_uahcx_ehci_configflag_s   cn68xx;
	struct cvmx_uahcx_ehci_configflag_s   cn68xxp1;
	struct cvmx_uahcx_ehci_configflag_s   cnf71xx;
};
typedef union cvmx_uahcx_ehci_configflag cvmx_uahcx_ehci_configflag_t;

/**
 * cvmx_uahc#_ehci_ctrldssegment
 *
 * CTRLDSSEGMENT = Control Data Structure Segment Register
 *
 * This 32-bit register corresponds to the most significant address bits [63:32] for all EHCI data structures. If
 * the 64-bit Addressing Capability field in HCCPARAMS is a zero, then this register is not used. Software
 * cannot write to it and a read from this register will return zeros.
 *
 * If the 64-bit Addressing Capability field in HCCPARAMS is a one, then this register is used with the link
 * pointers to construct 64-bit addresses to EHCI control data structures. This register is concatenated with the
 * link pointer from either the PERIODICLISTBASE, ASYNCLISTADDR, or any control data structure link
 * field to construct a 64-bit address.
 *
 * This register allows the host software to locate all control data structures within the same 4 Gigabyte
 * memory segment.
 */
union cvmx_uahcx_ehci_ctrldssegment {
	uint32_t u32;
	struct cvmx_uahcx_ehci_ctrldssegment_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ctrldsseg                    : 32; /**< Control Data Strucute Semgent Address Bit [63:32] */
#else
	uint32_t ctrldsseg                    : 32;
#endif
	} s;
	struct cvmx_uahcx_ehci_ctrldssegment_s cn61xx;
	struct cvmx_uahcx_ehci_ctrldssegment_s cn63xx;
	struct cvmx_uahcx_ehci_ctrldssegment_s cn63xxp1;
	struct cvmx_uahcx_ehci_ctrldssegment_s cn66xx;
	struct cvmx_uahcx_ehci_ctrldssegment_s cn68xx;
	struct cvmx_uahcx_ehci_ctrldssegment_s cn68xxp1;
	struct cvmx_uahcx_ehci_ctrldssegment_s cnf71xx;
};
typedef union cvmx_uahcx_ehci_ctrldssegment cvmx_uahcx_ehci_ctrldssegment_t;

/**
 * cvmx_uahc#_ehci_frindex
 *
 * FRINDEX = Frame Index Register
 * This register is used by the host controller to index into the periodic frame list. The register updates every
 * 125 microseconds (once each micro-frame). Bits [N:3] are used to select a particular entry in the Periodic
 * Frame List during periodic schedule execution. The number of bits used for the index depends on the size of
 * the frame list as set by system software in the Frame List Size field in the USBCMD register.
 * This register cannot be written unless the Host Controller is in the Halted state as indicated by the
 * HCHalted bit. A write to this register while the Run/Stop bit is set to a one (USBCMD register) produces
 * undefined results. Writes to this register also affect the SOF value.
 */
union cvmx_uahcx_ehci_frindex {
	uint32_t u32;
	struct cvmx_uahcx_ehci_frindex_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_14_31               : 18;
	uint32_t fi                           : 14; /**< Frame Index. The value in this register increments at the end of each time frame (e.g.
                                                         micro-frame). Bits [N:3] are used for the Frame List current index. This means that each
                                                         location of the frame list is accessed 8 times (frames or micro-frames) before moving to
                                                         the next index. The following illustrates values of N based on the value of the Frame List
                                                         Size field in the USBCMD register.
                                                         USBCMD[Frame List Size] Number Elements N
                                                            00b (1024) 12
                                                            01b (512) 11
                                                            10b (256) 10
                                                            11b Reserved */
#else
	uint32_t fi                           : 14;
	uint32_t reserved_14_31               : 18;
#endif
	} s;
	struct cvmx_uahcx_ehci_frindex_s      cn61xx;
	struct cvmx_uahcx_ehci_frindex_s      cn63xx;
	struct cvmx_uahcx_ehci_frindex_s      cn63xxp1;
	struct cvmx_uahcx_ehci_frindex_s      cn66xx;
	struct cvmx_uahcx_ehci_frindex_s      cn68xx;
	struct cvmx_uahcx_ehci_frindex_s      cn68xxp1;
	struct cvmx_uahcx_ehci_frindex_s      cnf71xx;
};
typedef union cvmx_uahcx_ehci_frindex cvmx_uahcx_ehci_frindex_t;

/**
 * cvmx_uahc#_ehci_hccapbase
 *
 * HCCAPBASE = Host Controller BASE Capability Register
 *
 */
union cvmx_uahcx_ehci_hccapbase {
	uint32_t u32;
	struct cvmx_uahcx_ehci_hccapbase_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t hciversion                   : 16; /**< Host Controller Interface Version Number */
	uint32_t reserved_8_15                : 8;
	uint32_t caplength                    : 8;  /**< Capabitlity Registers Length */
#else
	uint32_t caplength                    : 8;
	uint32_t reserved_8_15                : 8;
	uint32_t hciversion                   : 16;
#endif
	} s;
	struct cvmx_uahcx_ehci_hccapbase_s    cn61xx;
	struct cvmx_uahcx_ehci_hccapbase_s    cn63xx;
	struct cvmx_uahcx_ehci_hccapbase_s    cn63xxp1;
	struct cvmx_uahcx_ehci_hccapbase_s    cn66xx;
	struct cvmx_uahcx_ehci_hccapbase_s    cn68xx;
	struct cvmx_uahcx_ehci_hccapbase_s    cn68xxp1;
	struct cvmx_uahcx_ehci_hccapbase_s    cnf71xx;
};
typedef union cvmx_uahcx_ehci_hccapbase cvmx_uahcx_ehci_hccapbase_t;

/**
 * cvmx_uahc#_ehci_hccparams
 *
 * HCCPARAMS = Host Controller Capability Parameters
 * Multiple Mode control (time-base bit functionality), addressing capability
 */
union cvmx_uahcx_ehci_hccparams {
	uint32_t u32;
	struct cvmx_uahcx_ehci_hccparams_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t eecp                         : 8;  /**< EHCI Extended Capabilities Pointer. Default = Implementation Dependent.
                                                         This optional field indicates the existence of a capabilities list. A value of 00h indicates
                                                         no extended capabilities are implemented. A non-zero value in this register indicates the
                                                         offset in PCI configuration space of the first EHCI extended capability. The pointer value
                                                         must be 40h or greater if implemented to maintain the consistency of the PCI header
                                                         defined for this class of device. */
	uint32_t ist                          : 4;  /**< Isochronous Scheduling Threshold. Default = implementation dependent. This field
                                                         indicates, relative to the current position of the executing host controller, where software
                                                         can reliably update the isochronous schedule. When bit [7] is zero, the value of the least
                                                         significant 3 bits indicates the number of micro-frames a host controller can hold a set of
                                                         isochronous data structures (one or more) before flushing the state. When bit [7] is a
                                                         one, then host software assumes the host controller may cache an isochronous data
                                                         structure for an entire frame. Refer to Section 4.7.2.1 for details on how software uses
                                                         this information for scheduling isochronous transfers. */
	uint32_t reserved_3_3                 : 1;
	uint32_t aspc                         : 1;  /**< Asynchronous Schedule Park Capability. Default = Implementation dependent. If this
                                                         bit is set to a one, then the host controller supports the park feature for high-speed
                                                         queue heads in the Asynchronous Schedule. The feature can be disabled or enabled
                                                         and set to a specific level by using the Asynchronous Schedule Park Mode Enable and
                                                         Asynchronous Schedule Park Mode Count fields in the USBCMD register. */
	uint32_t pflf                         : 1;  /**< Programmable Frame List Flag. Default = Implementation dependent. If this bit is set
                                                         to a zero, then system software must use a frame list length of 1024 elements with this
                                                         host controller. The USBCMD register Frame List Size field is a read-only register and
                                                         should be set to zero.
                                                         If set to a one, then system software can specify and use a smaller frame list and
                                                         configure the host controller via the USBCMD register Frame List Size field. The frame
                                                         list must always be aligned on a 4K page boundary. This requirement ensures that the
                                                         frame list is always physically contiguous. */
	uint32_t ac64                         : 1;  /**< 64-bit Addressing Capability1 . This field documents the addressing range capability of
                                                          this implementation. The value of this field determines whether software should use the
                                                          data structures defined in Section 3 (32-bit) or those defined in Appendix B (64-bit).
                                                          Values for this field have the following interpretation:
                                                         - 0: data structures using 32-bit address memory pointers
                                                         - 1: data structures using 64-bit address memory pointers */
#else
	uint32_t ac64                         : 1;
	uint32_t pflf                         : 1;
	uint32_t aspc                         : 1;
	uint32_t reserved_3_3                 : 1;
	uint32_t ist                          : 4;
	uint32_t eecp                         : 8;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_uahcx_ehci_hccparams_s    cn61xx;
	struct cvmx_uahcx_ehci_hccparams_s    cn63xx;
	struct cvmx_uahcx_ehci_hccparams_s    cn63xxp1;
	struct cvmx_uahcx_ehci_hccparams_s    cn66xx;
	struct cvmx_uahcx_ehci_hccparams_s    cn68xx;
	struct cvmx_uahcx_ehci_hccparams_s    cn68xxp1;
	struct cvmx_uahcx_ehci_hccparams_s    cnf71xx;
};
typedef union cvmx_uahcx_ehci_hccparams cvmx_uahcx_ehci_hccparams_t;

/**
 * cvmx_uahc#_ehci_hcsparams
 *
 * HCSPARAMS = Host Controller Structural Parameters
 * This is a set of fields that are structural parameters: Number of downstream ports, etc.
 */
union cvmx_uahcx_ehci_hcsparams {
	uint32_t u32;
	struct cvmx_uahcx_ehci_hcsparams_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t dpn                          : 4;  /**< Debug Port Number. Optional. This register identifies which of the host controller ports
                                                         is the debug port. The value is the port number (one-based) of the debug port. A nonzero
                                                         value in this field indicates the presence of a debug port. The value in this register
                                                         must not be greater than N_PORTS (see below). */
	uint32_t reserved_17_19               : 3;
	uint32_t p_indicator                  : 1;  /**< Port Indicator. This bit indicates whether the ports support port
                                                         indicator control. When this bit is a one, the port status and control
                                                         registers include a read/writeable field for controlling the state of
                                                         the port indicator. */
	uint32_t n_cc                         : 4;  /**< Number of Companion Controller. This field indicates the number of
                                                         companion controllers associated with this USB 2.0 host controller.
                                                         A zero in this field indicates there are no companion host controllers.
                                                         Port-ownership hand-off is not supported. Only high-speed devices are
                                                         supported on the host controller root ports.
                                                         A value larger than zero in this field indicates there are companion USB 1.1 host
                                                         controller(s). Port-ownership hand-offs are supported. High, Full-and Low-speed
                                                         devices are supported on the host controller root ports. */
	uint32_t n_pcc                        : 4;  /**< Number of Ports per Companion Controller (N_PCC). This field indicates
                                                         the number of ports supported per companion host controller. It is used to
                                                         indicate the port routing  configuration to system software. */
	uint32_t prr                          : 1;  /**< Port Routing Rules. This field indicates the method used by this implementation for
                                                         how all ports are mapped to companion controllers. The value of this field has
                                                         the following interpretation:
                                                         0 The first N_PCC ports are routed to the lowest numbered function
                                                           companion host controller, the next N_PCC port are routed to the next
                                                           lowest function companion controller, and so on.
                                                         1 The port routing is explicitly enumerated by the first N_PORTS elements
                                                           of the HCSP-PORTROUTE array. */
	uint32_t reserved_5_6                 : 2;
	uint32_t ppc                          : 1;  /**< Port Power Control. This field indicates whether the host controller
                                                         implementation includes port power control. A one in this bit indicates the ports have
                                                         port power switches. A zero in this bit indicates the port do not have port power
                                                         switches. The value of this field affects the functionality of the Port Power field
                                                         in each port status and control register (see Section 2.3.8). */
	uint32_t n_ports                      : 4;  /**< This field specifies the number of physical downstream ports implemented
                                                         on this host controller. The value of this field determines how many port registers are
                                                         addressable in the Operational Register Space (see Table 2-8). Valid values are in the
                                                         range of 1H to FH. A zero in this field is undefined. */
#else
	uint32_t n_ports                      : 4;
	uint32_t ppc                          : 1;
	uint32_t reserved_5_6                 : 2;
	uint32_t prr                          : 1;
	uint32_t n_pcc                        : 4;
	uint32_t n_cc                         : 4;
	uint32_t p_indicator                  : 1;
	uint32_t reserved_17_19               : 3;
	uint32_t dpn                          : 4;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_uahcx_ehci_hcsparams_s    cn61xx;
	struct cvmx_uahcx_ehci_hcsparams_s    cn63xx;
	struct cvmx_uahcx_ehci_hcsparams_s    cn63xxp1;
	struct cvmx_uahcx_ehci_hcsparams_s    cn66xx;
	struct cvmx_uahcx_ehci_hcsparams_s    cn68xx;
	struct cvmx_uahcx_ehci_hcsparams_s    cn68xxp1;
	struct cvmx_uahcx_ehci_hcsparams_s    cnf71xx;
};
typedef union cvmx_uahcx_ehci_hcsparams cvmx_uahcx_ehci_hcsparams_t;

/**
 * cvmx_uahc#_ehci_insnreg00
 *
 * EHCI_INSNREG00 = EHCI Programmable Microframe Base Value Register (Synopsys Speicific)
 * This register allows you to change the microframe length value (default is microframe SOF = 125 s) to reduce the simulation time.
 */
union cvmx_uahcx_ehci_insnreg00 {
	uint32_t u32;
	struct cvmx_uahcx_ehci_insnreg00_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_14_31               : 18;
	uint32_t mfmc                         : 13; /**< For byte interface (8-bits), <13:1> is used as the 1-microframe counter.
                                                         For word interface (16_bits> <12:1> is used as the 1-microframe counter with word
                                                           interface (16-bits). */
	uint32_t en                           : 1;  /**< Writing 1b1 enables this register.
                                                         Note: Do not enable this register for the gate-level netlist */
#else
	uint32_t en                           : 1;
	uint32_t mfmc                         : 13;
	uint32_t reserved_14_31               : 18;
#endif
	} s;
	struct cvmx_uahcx_ehci_insnreg00_s    cn61xx;
	struct cvmx_uahcx_ehci_insnreg00_s    cn63xx;
	struct cvmx_uahcx_ehci_insnreg00_s    cn63xxp1;
	struct cvmx_uahcx_ehci_insnreg00_s    cn66xx;
	struct cvmx_uahcx_ehci_insnreg00_s    cn68xx;
	struct cvmx_uahcx_ehci_insnreg00_s    cn68xxp1;
	struct cvmx_uahcx_ehci_insnreg00_s    cnf71xx;
};
typedef union cvmx_uahcx_ehci_insnreg00 cvmx_uahcx_ehci_insnreg00_t;

/**
 * cvmx_uahc#_ehci_insnreg03
 *
 * EHCI_INSNREG03 = EHCI Timing Adjust Register (Synopsys Speicific)
 * This register allows you to change the timing of Phy Tx turnaround delay etc.
 */
union cvmx_uahcx_ehci_insnreg03 {
	uint32_t u32;
	struct cvmx_uahcx_ehci_insnreg03_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_13_31               : 19;
	uint32_t txtx_tadao                   : 3;  /**< Tx-Tx turnaround Delay Add on. This field specifies the extra delays in phy_clks to
                                                         be added to the "Transmit to Transmit turnaround delay" value maintained in the core.
                                                         The default value of this register field is 0. This default value of 0 is sufficient
                                                         for most PHYs. But for some PHYs which puts wait states during the token packet, it
                                                         may be required to program a value greater than 0 to meet the transmit to transmit
                                                         minimum turnaround time. The recommendation to use the default value of 0 and change
                                                         it only if there is an issue with minimum transmit-to- transmit turnaround time. This
                                                         value should be programmed during core initialization and should not be changed afterwards. */
	uint32_t reserved_9_9                 : 1;
	uint32_t ta_off                       : 8;  /**< Time-Available Offset. This value indicates the additional number of bytes to be
                                                         accommodated for the time-available calculation. The USB traffic on the bus can be started
                                                         only when sufficient time is available to complete the packet within the EOF1 point. Refer
                                                         to the USB 2.0 specification for details of the EOF1 point. This time-available
                                                         calculation is done in the hardware, and can be further offset by programming a value in
                                                         this location.
                                                         Note: Time-available calculation is added for future flexibility. The application is not
                                                         required to program this field by default. */
	uint32_t reserved_0_0                 : 1;
#else
	uint32_t reserved_0_0                 : 1;
	uint32_t ta_off                       : 8;
	uint32_t reserved_9_9                 : 1;
	uint32_t txtx_tadao                   : 3;
	uint32_t reserved_13_31               : 19;
#endif
	} s;
	struct cvmx_uahcx_ehci_insnreg03_s    cn61xx;
	struct cvmx_uahcx_ehci_insnreg03_s    cn63xx;
	struct cvmx_uahcx_ehci_insnreg03_s    cn63xxp1;
	struct cvmx_uahcx_ehci_insnreg03_s    cn66xx;
	struct cvmx_uahcx_ehci_insnreg03_s    cn68xx;
	struct cvmx_uahcx_ehci_insnreg03_s    cn68xxp1;
	struct cvmx_uahcx_ehci_insnreg03_s    cnf71xx;
};
typedef union cvmx_uahcx_ehci_insnreg03 cvmx_uahcx_ehci_insnreg03_t;

/**
 * cvmx_uahc#_ehci_insnreg04
 *
 * EHCI_INSNREG04 = EHCI Debug Register (Synopsys Speicific)
 * This register is used only for debug purposes.
 */
union cvmx_uahcx_ehci_insnreg04 {
	uint32_t u32;
	struct cvmx_uahcx_ehci_insnreg04_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t auto_dis                     : 1;  /**< Automatic feature disable.
                                                          1'b0: 0 by default, the automatic feature is enabled. The Suspend signal is deasserted
                                                                (logic level 1'b1) when run/stop is reset by software, but the hchalted bit is not
                                                                yet set.
                                                          1'b1: Disables the automatic feature, which takes all ports out of suspend when software
                                                                clears the run/stop bit. This is for backward compatibility.
                                                         This bit has an added functionality in release 2.80a and later. For systems where the host
                                                         is halted without waking up all ports out of suspend, the port can become stuck because
                                                         the PHYCLK is not running when the halt is programmed. To avoid this, the DWC H20AHB host
                                                         core automatically pulls ports out of suspend when the host is halted by software. This bit
                                                         is used to disable this automatic function. */
	uint32_t nakrf_dis                    : 1;  /**< NAK Reload Fix Disable.
                                                         1b0: NAK reload fix enabled.
                                                         1b1: NAK reload fix disabled. (Incorrect NAK reload transition at the end of a microframe
                                                              for backward compatibility with Release 2.40c. For more information see the USB 2.0
                                                              Host-AHB Release Notes. */
	uint32_t reserved_3_3                 : 1;
	uint32_t pesd                         : 1;  /**< Scales down port enumeration time.
                                                          1'b1: scale down enabled
                                                          1'b0:  scale downd disabled
                                                         This is for simulation only. */
	uint32_t hcp_fw                       : 1;  /**< HCCPARAMS Field Writeable.
                                                         1'b1: The HCCPARAMS register's bits 17, 15:4, and 2:0 become writable.
                                                         1'b0: The HCCPARAMS register's bits 17, 15:4, and 2:0 are not writable. */
	uint32_t hcp_rw                       : 1;  /**< HCCPARAMS Reigster Writeable.
                                                         1'b1: The HCCPARAMS register becomes writable.
                                                         1'b0: The HCCPARAMS register is not writable. */
#else
	uint32_t hcp_rw                       : 1;
	uint32_t hcp_fw                       : 1;
	uint32_t pesd                         : 1;
	uint32_t reserved_3_3                 : 1;
	uint32_t nakrf_dis                    : 1;
	uint32_t auto_dis                     : 1;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_uahcx_ehci_insnreg04_s    cn61xx;
	struct cvmx_uahcx_ehci_insnreg04_s    cn63xx;
	struct cvmx_uahcx_ehci_insnreg04_s    cn63xxp1;
	struct cvmx_uahcx_ehci_insnreg04_s    cn66xx;
	struct cvmx_uahcx_ehci_insnreg04_s    cn68xx;
	struct cvmx_uahcx_ehci_insnreg04_s    cn68xxp1;
	struct cvmx_uahcx_ehci_insnreg04_s    cnf71xx;
};
typedef union cvmx_uahcx_ehci_insnreg04 cvmx_uahcx_ehci_insnreg04_t;

/**
 * cvmx_uahc#_ehci_insnreg06
 *
 * EHCI_INSNREG06 = EHCI  AHB Error Status Register (Synopsys Speicific)
 * This register contains AHB Error Status.
 */
union cvmx_uahcx_ehci_insnreg06 {
	uint32_t u32;
	struct cvmx_uahcx_ehci_insnreg06_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t vld                          : 1;  /**< AHB Error Captured. Indicator that an AHB error was encountered and values were captured.
                                                         To clear this field the application must write a 0 to it. */
	uint32_t reserved_0_30                : 31;
#else
	uint32_t reserved_0_30                : 31;
	uint32_t vld                          : 1;
#endif
	} s;
	struct cvmx_uahcx_ehci_insnreg06_s    cn61xx;
	struct cvmx_uahcx_ehci_insnreg06_s    cn63xx;
	struct cvmx_uahcx_ehci_insnreg06_s    cn63xxp1;
	struct cvmx_uahcx_ehci_insnreg06_s    cn66xx;
	struct cvmx_uahcx_ehci_insnreg06_s    cn68xx;
	struct cvmx_uahcx_ehci_insnreg06_s    cn68xxp1;
	struct cvmx_uahcx_ehci_insnreg06_s    cnf71xx;
};
typedef union cvmx_uahcx_ehci_insnreg06 cvmx_uahcx_ehci_insnreg06_t;

/**
 * cvmx_uahc#_ehci_insnreg07
 *
 * EHCI_INSNREG07 = EHCI  AHB Error Address Register (Synopsys Speicific)
 * This register contains AHB Error Status.
 */
union cvmx_uahcx_ehci_insnreg07 {
	uint32_t u32;
	struct cvmx_uahcx_ehci_insnreg07_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t err_addr                     : 32; /**< AHB Master Error Address. AHB address of the control phase at which the AHB error occurred */
#else
	uint32_t err_addr                     : 32;
#endif
	} s;
	struct cvmx_uahcx_ehci_insnreg07_s    cn61xx;
	struct cvmx_uahcx_ehci_insnreg07_s    cn63xx;
	struct cvmx_uahcx_ehci_insnreg07_s    cn63xxp1;
	struct cvmx_uahcx_ehci_insnreg07_s    cn66xx;
	struct cvmx_uahcx_ehci_insnreg07_s    cn68xx;
	struct cvmx_uahcx_ehci_insnreg07_s    cn68xxp1;
	struct cvmx_uahcx_ehci_insnreg07_s    cnf71xx;
};
typedef union cvmx_uahcx_ehci_insnreg07 cvmx_uahcx_ehci_insnreg07_t;

/**
 * cvmx_uahc#_ehci_periodiclistbase
 *
 * PERIODICLISTBASE = Periodic Frame List Base Address Register
 *
 * This 32-bit register contains the beginning address of the Periodic Frame List in the system memory. If the
 * host controller is in 64-bit mode (as indicated by a one in the 64-bit Addressing Capability field in the
 * HCCSPARAMS register), then the most significant 32 bits of every control data structure address comes
 * from the CTRLDSSEGMENT register (see Section 2.3.5). System software loads this register prior to
 * starting the schedule execution by the Host Controller (see 4.1). The memory structure referenced by this
 * physical memory pointer is assumed to be 4-Kbyte aligned. The contents of this register are combined with
 * the Frame Index Register (FRINDEX) to enable the Host Controller to step through the Periodic Frame List
 * in sequence.
 */
union cvmx_uahcx_ehci_periodiclistbase {
	uint32_t u32;
	struct cvmx_uahcx_ehci_periodiclistbase_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t baddr                        : 20; /**< Base Address (Low). These bits correspond to memory address signals [31:12],respectively. */
	uint32_t reserved_0_11                : 12;
#else
	uint32_t reserved_0_11                : 12;
	uint32_t baddr                        : 20;
#endif
	} s;
	struct cvmx_uahcx_ehci_periodiclistbase_s cn61xx;
	struct cvmx_uahcx_ehci_periodiclistbase_s cn63xx;
	struct cvmx_uahcx_ehci_periodiclistbase_s cn63xxp1;
	struct cvmx_uahcx_ehci_periodiclistbase_s cn66xx;
	struct cvmx_uahcx_ehci_periodiclistbase_s cn68xx;
	struct cvmx_uahcx_ehci_periodiclistbase_s cn68xxp1;
	struct cvmx_uahcx_ehci_periodiclistbase_s cnf71xx;
};
typedef union cvmx_uahcx_ehci_periodiclistbase cvmx_uahcx_ehci_periodiclistbase_t;

/**
 * cvmx_uahc#_ehci_portsc#
 *
 * PORTSCX = Port X Status and Control Register
 * Default: 00002000h (w/PPC set to one); 00003000h (w/PPC set to a zero)
 */
union cvmx_uahcx_ehci_portscx {
	uint32_t u32;
	struct cvmx_uahcx_ehci_portscx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_23_31               : 9;
	uint32_t wkoc_e                       : 1;  /**< Wake on Over-current Enable.Writing this bit to a
                                                         one enables the port to be sensitive to over-current conditions as wake-up events.
                                                         This field is zero if Port Power is zero. */
	uint32_t wkdscnnt_e                   : 1;  /**< Wake on Disconnect Enable. Writing this bit to a one enables the port to be
                                                         sensitive to device disconnects as wake-up events.
                                                         This field is zero if Port Power is zero. */
	uint32_t wkcnnt_e                     : 1;  /**< Wake on Connect Enable. Writing this bit to a one enables the port to be
                                                         sensitive to device connects as wake-up events.
                                                         This field is zero if Port Power is zero. */
	uint32_t ptc                          : 4;  /**< Port Test Control. When this field is zero, the port is NOT
                                                         operating in a test mode. A non-zero value indicates that it is operating
                                                         in test mode and the specific test mode is indicated by the specific value.
                                                         The encoding of the test mode bits are (0110b - 1111b are reserved):
                                                         Bits Test Mode
                                                          0000b Test mode not enabled
                                                          0001b Test J_STATE
                                                          0010b Test K_STATE
                                                          0011b Test SE0_NAK
                                                          0100b Test Packet
                                                          0101b Test FORCE_ENABLE */
	uint32_t pic                          : 2;  /**< Port Indicator Control. Writing to these bits has no effect if the
                                                         P_INDICATOR bit in the HCSPARAMS register is a zero. If P_INDICATOR bit is a one,
                                                         then the bit encodings are:
                                                         Bit Value Meaning
                                                          00b Port indicators are off
                                                          01b Amber
                                                          10b Green
                                                          11b Undefined
                                                         This field is zero if Port Power is zero. */
	uint32_t po                           : 1;  /**< Port Owner.This bit unconditionally goes to a 0b when the
                                                         Configured bit in the CONFIGFLAG register makes a 0b to 1b transition. This bit
                                                         unconditionally goes to 1b whenever the Configured bit is zero.
                                                         System software uses this field to release ownership of the port to a selected host
                                                         controller (in the event that the attached device is not a high-speed device). Software
                                                         writes a one to this bit when the attached device is not a high-speed device. A one in
                                                         this bit means that a companion host controller owns and controls the port. */
	uint32_t pp                           : 1;  /**< Port Power. The function of this bit depends on the value of the Port
                                                         Power Control (PPC) field in the HCSPARAMS register. The behavior is as follows:
                                                         PPC PP    Operation
                                                          0b 1b    RO  - Host controller does not have port power control switches.
                                                                         Each port is hard-wired to power.
                                                          1b 1b/0b R/W - Host controller has port power control switches. This bit
                                                                         represents the current setting of the switch (0 = off, 1 = on). When
                                                                         power is not available on a port (i.e. PP equals a 0), the port is
                                                                         nonfunctional  and will not report attaches, detaches, etc.
                                                         When an over-current condition is detected on a powered port and PPC is a one, the PP
                                                         bit in each affected port may be transitioned by the host controller from a 1 to 0
                                                         (removing power from the port). */
	uint32_t lsts                         : 2;  /**< Line Status.These bits reflect the current logical levels of the D+ (bit 11) and D(bit 10)
                                                          signal lines. These bits are used for detection of low-speed USB devices prior to
                                                          the port reset and enable sequence. This field is valid only when the port enable bit is
                                                          zero and the current connect status bit is set to a one.
                                                          The encoding of the bits are:
                                                           Bits[11:10] USB State   Interpretation
                                                           00b         SE0         Not Low-speed device, perform EHCI reset
                                                           10b         J-state     Not Low-speed device, perform EHCI reset
                                                           01b         K-state     Low-speed device, release ownership of port
                                                           11b         Undefined   Not Low-speed device, perform EHCI reset.
                                                         This value of this field is undefined if Port Power is zero. */
	uint32_t reserved_9_9                 : 1;
	uint32_t prst                         : 1;  /**< Port Reset.1=Port is in Reset. 0=Port is not in Reset. Default = 0. When
                                                         software writes a one to this bit (from a zero), the bus reset sequence as defined in the
                                                         USB Specification Revision 2.0 is started. Software writes a zero to this bit to terminate
                                                         the bus reset sequence. Software must keep this bit at a one long enough to ensure the
                                                         reset sequence, as specified in the USB Specification Revision 2.0, completes. Note:
                                                         when software writes this bit to a one, it must also write a zero to the Port Enable bit.
                                                         Note that when software writes a zero to this bit there may be a delay before the bit
                                                         status changes to a zero. The bit status will not read as a zero until after the reset has
                                                         completed. If the port is in high-speed mode after reset is complete, the host controller
                                                         will automatically enable this port (e.g. set the Port Enable bit to a one). A host controller
                                                         must terminate the reset and stabilize the state of the port within 2 milliseconds of
                                                         software transitioning this bit from a one to a zero. For example: if the port detects that
                                                         the attached device is high-speed during reset, then the host controller must have the
                                                         port in the enabled state within 2ms of software writing this bit to a zero.
                                                         The HCHalted bit in the USBSTS register should be a zero before software attempts to
                                                         use this bit. The host controller may hold Port Reset asserted to a one when the
                                                         HCHalted bit is a one.
                                                         This field is zero if Port Power is zero. */
	uint32_t spd                          : 1;  /**< Suspend. 1=Port in suspend state. 0=Port not in suspend state. Default = 0. Port
                                                         Enabled Bit and Suspend bit of this register define the port states as follows:
                                                         Bits [Port Enabled, Suspend]     Port State
                                                                      0X                  Disable
                                                                      10                  Enable
                                                                      11                  Suspend
                                                         When in suspend state, downstream propagation of data is blocked on this port, except
                                                         for port reset. The blocking occurs at the end of the current transaction, if a transaction
                                                         was in progress when this bit was written to 1. In the suspend state, the port is sensitive
                                                         to resume detection. Note that the bit status does not change until the port is
                                                         suspended and that there may be a delay in suspending a port if there is a transaction
                                                         currently in progress on the USB.
                                                         A write of zero to this bit is ignored by the host controller. The host controller will
                                                         unconditionally set this bit to a zero when:
                                                         . Software sets the Force Port Resume bit to a zero (from a one).
                                                         . Software sets the Port Reset bit to a one (from a zero).
                                                         If host software sets this bit to a one when the port is not enabled (i.e. Port enabled bit is
                                                         a zero) the results are undefined.
                                                         This field is zero if Port Power is zero. */
	uint32_t fpr                          : 1;  /**< Force Port Resume.
                                                         1= Resume detected/driven on port. 0=No resume (Kstate)
                                                         detected/driven on port. Default = 0. This functionality defined for manipulating
                                                         this bit depends on the value of the Suspend bit. For example, if the port is not
                                                         suspended (Suspend and Enabled bits are a one) and software transitions this bit to a
                                                         one, then the effects on the bus are undefined.
                                                         Software sets this bit to a 1 to drive resume signaling. The Host Controller sets this bit to
                                                         a 1 if a J-to-K transition is detected while the port is in the Suspend state. When this bit
                                                         transitions to a one because a J-to-K transition is detected, the Port Change Detect bit in
                                                         the USBSTS register is also set to a one. If software sets this bit to a one, the host
                                                         controller must not set the Port Change Detect bit.
                                                         Note that when the EHCI controller owns the port, the resume sequence follows the
                                                         defined sequence documented in the USB Specification Revision 2.0. The resume
                                                         signaling (Full-speed 'K') is driven on the port as long as this bit remains a one. Software
                                                         must appropriately time the Resume and set this bit to a zero when the appropriate
                                                         amount of time has elapsed. Writing a zero (from one) causes the port to return to high-
                                                         speed mode (forcing the bus below the port into a high-speed idle). This bit will remain a
                                                         one until the port has switched to the high-speed idle. The host controller must complete
                                                         this transition within 2 milliseconds of software setting this bit to a zero.
                                                         This field is zero if Port Power is zero. */
	uint32_t occ                          : 1;  /**< Over-current Change. 1=This bit gets set to a one when there is a change to Over-current Active.
                                                         Software clears this bit by writing a one to this bit position. */
	uint32_t oca                          : 1;  /**< Over-current Active. 1=This port currently has an over-current condition. 0=This port does not
                                                         have an over-current condition. This bit will automatically transition from a one to a zero when
                                                         the over current condition is removed. */
	uint32_t pedc                         : 1;  /**< Port Enable/Disable Change. 1=Port enabled/disabled status has changed.
                                                         0=No change. Default = 0. For the root hub, this bit gets set to a one only when a port is
                                                               disabled due to the appropriate conditions existing at the EOF2 point (See Chapter 11 of
                                                         the USB Specification for the definition of a Port Error). Software clears this bit by writing
                                                         a 1 to it.
                                                         This field is zero if Port Power is zero. */
	uint32_t ped                          : 1;  /**< Port Enabled/Disabled. 1=Enable. 0=Disable. Ports can only be
                                                         enabled by the host controller as a part of the reset and enable. Software cannot enable
                                                         a port by writing a one to this field. The host controller will only set this bit to a one when
                                                         the reset sequence determines that the attached device is a high-speed device.
                                                         Ports can be disabled by either a fault condition (disconnect event or other fault
                                                         condition) or by host software. Note that the bit status does not change until the port
                                                         state actually changes. There may be a delay in disabling or enabling a port due to other
                                                         host controller and bus events. See Section 4.2 for full details on port reset and enable.
                                                         When the port is disabled (0b) downstream propagation of data is blocked on this port,
                                                         except for reset.
                                                         This field is zero if Port Power is zero. */
	uint32_t csc                          : 1;  /**< Connect Status Change. 1=Change in Current Connect Status. 0=No change. Indicates a change
                                                         has occurred in the port's Current Connect Status. The host controller sets this bit for all
                                                         changes to the port device connect status, even if system software has not cleared an existing
                                                         connect status change. For example, the insertion status changes twice before system software
                                                         has cleared the changed condition, hub hardware will be setting an already-set bit
                                                         (i.e., the bit will remain set). Software sets this bit to 0 by writing a 1 to it.
                                                         This field is zero if Port Power is zero. */
	uint32_t ccs                          : 1;  /**< Current Connect Status. 1=Device is present on port. 0=No device is present.
                                                         This value reflects the current state of the port, and may not correspond
                                                         directly to the event that caused the Connect Status Change bit (Bit 1) to be set.
                                                         This field is zero if Port Power is zero. */
#else
	uint32_t ccs                          : 1;
	uint32_t csc                          : 1;
	uint32_t ped                          : 1;
	uint32_t pedc                         : 1;
	uint32_t oca                          : 1;
	uint32_t occ                          : 1;
	uint32_t fpr                          : 1;
	uint32_t spd                          : 1;
	uint32_t prst                         : 1;
	uint32_t reserved_9_9                 : 1;
	uint32_t lsts                         : 2;
	uint32_t pp                           : 1;
	uint32_t po                           : 1;
	uint32_t pic                          : 2;
	uint32_t ptc                          : 4;
	uint32_t wkcnnt_e                     : 1;
	uint32_t wkdscnnt_e                   : 1;
	uint32_t wkoc_e                       : 1;
	uint32_t reserved_23_31               : 9;
#endif
	} s;
	struct cvmx_uahcx_ehci_portscx_s      cn61xx;
	struct cvmx_uahcx_ehci_portscx_s      cn63xx;
	struct cvmx_uahcx_ehci_portscx_s      cn63xxp1;
	struct cvmx_uahcx_ehci_portscx_s      cn66xx;
	struct cvmx_uahcx_ehci_portscx_s      cn68xx;
	struct cvmx_uahcx_ehci_portscx_s      cn68xxp1;
	struct cvmx_uahcx_ehci_portscx_s      cnf71xx;
};
typedef union cvmx_uahcx_ehci_portscx cvmx_uahcx_ehci_portscx_t;

/**
 * cvmx_uahc#_ehci_usbcmd
 *
 * USBCMD = USB Command Register
 * The Command Register indicates the command to be executed by the serial bus host controller. Writing to the register causes a command to be executed.
 */
union cvmx_uahcx_ehci_usbcmd {
	uint32_t u32;
	struct cvmx_uahcx_ehci_usbcmd_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_24_31               : 8;
	uint32_t itc                          : 8;  /**< Interrupt Threshold Control. This field is used by system software
                                                         to select the maximum rate at which the host controller will issue interrupts. The only
                                                         valid values are defined below. If software writes an invalid value to this register, the
                                                         results are undefined. Value Maximum Interrupt Interval
                                                           00h Reserved
                                                           01h 1 micro-frame
                                                           02h 2 micro-frames
                                                           04h 4 micro-frames
                                                           08h 8 micro-frames (default, equates to 1 ms)
                                                           10h 16 micro-frames (2 ms)
                                                           20h 32 micro-frames (4 ms)
                                                           40h 64 micro-frames (8 ms) */
	uint32_t reserved_12_15               : 4;
	uint32_t aspm_en                      : 1;  /**< Asynchronous Schedule Park Mode Enable. */
	uint32_t reserved_10_10               : 1;
	uint32_t aspmc                        : 2;  /**< Asynchronous Schedule Park Mode Count. */
	uint32_t lhcr                         : 1;  /**< Light Host Controller Reset */
	uint32_t iaa_db                       : 1;  /**< Interrupt on Async Advance Doorbell.This bit is used as a doorbell by
                                                         software to tell the host controller to issue an interrupt the next time it advances
                                                         asynchronous schedule. Software must write a 1 to this bit to ring the doorbell.
                                                         When the host controller has evicted all appropriate cached schedule state, it sets the
                                                         Interrupt on Async Advance status bit in the USBSTS register. If the Interrupt on Async
                                                         Advance Enable bit in the USBINTR register is a one then the host controller will assert
                                                         an interrupt at the next interrupt threshold. */
	uint32_t as_en                        : 1;  /**< Asynchronous Schedule Enable .This bit controls whether the host
                                                         controller skips processing the Asynchronous Schedule. Values mean:
                                                          - 0: Do not process the Asynchronous Schedule
                                                          - 1: Use the ASYNCLISTADDR register to access the Asynchronous Schedule. */
	uint32_t ps_en                        : 1;  /**< Periodic Schedule Enable. This bit controls whether the host
                                                         controller skips processing the Periodic Schedule. Values mean:
                                                            - 0: Do not process the Periodic Schedule
                                                            - 1: Use the PERIODICLISTBASE register to access the Periodic Schedule. */
	uint32_t fls                          : 2;  /**< Frame List Size. This field is R/W only if Programmable
                                                         Frame List Flag in the HCCPARAMS registers is set to a one. This field specifies the
                                                         size of the frame list. The size the frame list controls which bits in the Frame Index
                                                         Register should be used for the Frame List Current index. Values mean:
                                                              00b: 1024 elements (4096 bytes) Default value
                                                              01b: 512 elements  (2048 bytes)
                                                              10b: 256 elements  (1024 bytes) - for resource-constrained environments
                                                              11b: Reserved */
	uint32_t hcreset                      : 1;  /**< Host Controller Reset (HCRESET). This control bit is used by software to reset
                                                         the host controller. The effects of this on Root Hub registers are similar to a Chip
                                                         Hardware Reset. When software writes a one to this bit, the Host Controller resets
                                                         its internal pipelines, timers, counters, state machines, etc. to their initial
                                                         value. Any transaction currently in progress on USB is immediately terminated.
                                                         A USB reset is not driven on downstream ports.
                                                         This bit is set to zero by the Host Controller when the reset process is complete. Software can not
                                                         terminate the reset process early by writing zero to this register.
                                                         Software should not set this bit to a one when the HCHalted bit in the USBSTS register is a zero.
                                                         Attempting to reset an activtely running host controller will result in undefined behavior. */
	uint32_t rs                           : 1;  /**< Run/Stop (RS).
                                                           1=Run. 0=Stop.
                                                         When set to a 1, the Host Controller proceeds with execution of the schedule.
                                                         The Host Controller continues execution as long as this bit is set to a 1.
                                                         When this bit is set to 0, the Host Controller completes the current and any
                                                         actively pipelined transactions on the USB and then halts. The Host
                                                         Controller must halt within 16 micro-frames after software clears the Run bit. The HC
                                                         Halted bit in the status register indicates when the Host Controller has finished its
                                                         pending pipelined transactions and has entered the stopped state. Software must not
                                                         write a one to this field unless the host controller is in the Halted state (i.e. HCHalted in
                                                         the USBSTS register is a one). Doing so will yield undefined results. */
#else
	uint32_t rs                           : 1;
	uint32_t hcreset                      : 1;
	uint32_t fls                          : 2;
	uint32_t ps_en                        : 1;
	uint32_t as_en                        : 1;
	uint32_t iaa_db                       : 1;
	uint32_t lhcr                         : 1;
	uint32_t aspmc                        : 2;
	uint32_t reserved_10_10               : 1;
	uint32_t aspm_en                      : 1;
	uint32_t reserved_12_15               : 4;
	uint32_t itc                          : 8;
	uint32_t reserved_24_31               : 8;
#endif
	} s;
	struct cvmx_uahcx_ehci_usbcmd_s       cn61xx;
	struct cvmx_uahcx_ehci_usbcmd_s       cn63xx;
	struct cvmx_uahcx_ehci_usbcmd_s       cn63xxp1;
	struct cvmx_uahcx_ehci_usbcmd_s       cn66xx;
	struct cvmx_uahcx_ehci_usbcmd_s       cn68xx;
	struct cvmx_uahcx_ehci_usbcmd_s       cn68xxp1;
	struct cvmx_uahcx_ehci_usbcmd_s       cnf71xx;
};
typedef union cvmx_uahcx_ehci_usbcmd cvmx_uahcx_ehci_usbcmd_t;

/**
 * cvmx_uahc#_ehci_usbintr
 *
 * USBINTR = USB Interrupt Enable Register
 * This register enables and disables reporting of the corresponding interrupt to the software. When a bit is set
 * and the corresponding interrupt is active, an interrupt is generated to the host. Interrupt sources that are
 * disabled in this register still appear in the USBSTS to allow the software to poll for events.
 * Each interrupt enable bit description indicates whether it is dependent on the interrupt threshold mechanism.
 * Note: for all enable register bits, 1= Enabled, 0= Disabled
 */
union cvmx_uahcx_ehci_usbintr {
	uint32_t u32;
	struct cvmx_uahcx_ehci_usbintr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_6_31                : 26;
	uint32_t ioaa_en                      : 1;  /**< Interrupt on Async Advance Enable When this bit is a one, and the Interrupt on
                                                         Async Advance bit in the USBSTS register is a one, the host controller will issue an
                                                         interrupt at the next interrupt threshold. The interrupt is acknowledged by software
                                                         clearing the Interrupt on Async Advance bit. */
	uint32_t hserr_en                     : 1;  /**< Host System Error Enable When this bit is a one, and the Host System
                                                         Error Status bit in the USBSTS register is a one, the host controller will issue an
                                                         interrupt. The interrupt is acknowledged by software clearing the Host System Error bit. */
	uint32_t flro_en                      : 1;  /**< Frame List Rollover Enable. When this bit is a one, and the Frame List
                                                         Rollover bit in the USBSTS register is a one, the host controller will issue an
                                                         interrupt. The interrupt is acknowledged by software clearing the Frame List Rollover bit. */
	uint32_t pci_en                       : 1;  /**< Port Change Interrupt Enable. When this bit is a one, and the Port Change Detect bit in
                                                         the USBSTS register is a one, the host controller will issue an interrupt.
                                                         The interrupt is acknowledged by software clearing the Port Change Detect bit. */
	uint32_t usberrint_en                 : 1;  /**< USB Error Interrupt Enable. When this bit is a one, and the USBERRINT
                                                         bit in the USBSTS register is a one, the host controller will issue an interrupt at the next
                                                         interrupt threshold. The interrupt is acknowledged by software clearing the USBERRINT bit. */
	uint32_t usbint_en                    : 1;  /**< USB Interrupt Enable. When this bit is a one, and the USBINT bit in the USBSTS register
                                                         is a one, the host controller will issue an interrupt at the next interrupt threshold.
                                                         The interrupt is acknowledged by software clearing the USBINT bit. */
#else
	uint32_t usbint_en                    : 1;
	uint32_t usberrint_en                 : 1;
	uint32_t pci_en                       : 1;
	uint32_t flro_en                      : 1;
	uint32_t hserr_en                     : 1;
	uint32_t ioaa_en                      : 1;
	uint32_t reserved_6_31                : 26;
#endif
	} s;
	struct cvmx_uahcx_ehci_usbintr_s      cn61xx;
	struct cvmx_uahcx_ehci_usbintr_s      cn63xx;
	struct cvmx_uahcx_ehci_usbintr_s      cn63xxp1;
	struct cvmx_uahcx_ehci_usbintr_s      cn66xx;
	struct cvmx_uahcx_ehci_usbintr_s      cn68xx;
	struct cvmx_uahcx_ehci_usbintr_s      cn68xxp1;
	struct cvmx_uahcx_ehci_usbintr_s      cnf71xx;
};
typedef union cvmx_uahcx_ehci_usbintr cvmx_uahcx_ehci_usbintr_t;

/**
 * cvmx_uahc#_ehci_usbsts
 *
 * USBSTS = USB Status Register
 * This register indicates pending interrupts and various states of the Host Controller. The status resulting from
 * a transaction on the serial bus is not indicated in this register. Software sets a bit to 0 in this register by
 * writing a 1 to it.
 */
union cvmx_uahcx_ehci_usbsts {
	uint32_t u32;
	struct cvmx_uahcx_ehci_usbsts_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t ass                          : 1;  /**< Asynchronous Schedule Status. The bit reports the current real
                                                         status of the Asynchronous Schedule. If this bit is a zero then the status of the
                                                         Asynchronous Schedule is disabled. If this bit is a one then the status of the
                                                         Asynchronous Schedule is enabled. The Host Controller is not required to immediately
                                                         disable or enable the Asynchronous Schedule when software transitions the
                                                         Asynchronous Schedule Enable bit in the USBCMD register. When this bit and the
                                                         Asynchronous Schedule Enable bit are the same value, the Asynchronous Schedule is
                                                         either enabled (1) or disabled (0). */
	uint32_t pss                          : 1;  /**< Periodic Schedule Status. The bit reports the current real status of
                                                         the Periodic Schedule. If this bit is a zero then the status of the Periodic
                                                         Schedule is disabled. If this bit is a one then the status of the Periodic Schedule
                                                         is enabled. The Host Controller is not required to immediately disable or enable the
                                                         Periodic Schedule when software transitions the Periodic Schedule Enable bit in
                                                         the USBCMD register. When this bit and the Periodic Schedule Enable bit are the
                                                         same value, the Periodic Schedule is either enabled (1) or disabled (0). */
	uint32_t reclm                        : 1;  /**< Reclamation.This is a read-only status bit, which is used to detect an
                                                         empty asynchronous schedule. */
	uint32_t hchtd                        : 1;  /**< HCHalted. This bit is a zero whenever the Run/Stop bit is a one. The
                                                         Host Controller sets this bit to one after it has stopped executing as a result of the
                                                         Run/Stop bit being set to 0, either by software or by the Host Controller hardware (e.g.
                                                         internal error). */
	uint32_t reserved_6_11                : 6;
	uint32_t ioaa                         : 1;  /**< Interrupt on Async Advance. System software can force the host
                                                         controller to issue an interrupt the next time the host controller advances the
                                                         asynchronous schedule by writing a one to the Interrupt on Async Advance Doorbell bit
                                                         in the USBCMD register. This status bit indicates the assertion of that interrupt source. */
	uint32_t hsyserr                      : 1;  /**< Host System Error. The Host Controller sets this bit to 1 when a serious error
                                                         occurs during a host system access involving the Host Controller module. */
	uint32_t flro                         : 1;  /**< Frame List Rollover. The Host Controller sets this bit to a one when the
                                                         Frame List Index rolls over from its maximum value to zero. The exact value at
                                                         which the rollover occurs depends on the frame list size. For example, if
                                                         the frame list size (as programmed in the Frame List Size field of the USBCMD register)
                                                         is 1024, the Frame Index Register rolls over every time FRINDEX[13] toggles. Similarly,
                                                         if the size is 512, the Host Controller sets this bit to a one every time FRINDEX[12]
                                                         toggles. */
	uint32_t pcd                          : 1;  /**< Port Change Detect. The Host Controller sets this bit to a one when any port
                                                         for which the Port Owner bit is set to zero (see Section 2.3.9) has a change bit transition
                                                         from a zero to a one or a Force Port Resume bit transition from a zero to a one as a
                                                         result of a J-K transition detected on a suspended port. This bit will also be set as a
                                                         result of the Connect Status Change being set to a one after system software has
                                                         relinquished ownership of a connected port by writing a one to a port's Port Owner bit. */
	uint32_t usberrint                    : 1;  /**< USB Error Interrupt. The Host Controller sets this bit to 1 when completion of a USB
                                                         transaction results in an error condition (e.g., error counter underflow). If the TD on
                                                         which the error interrupt occurred also had its IOC bit set, both this bit and USBINT
                                                         bit are set. */
	uint32_t usbint                       : 1;  /**< USB Interrupt. The Host Controller sets this bit to 1 on the completion of a USB
                                                         transaction, which results in the retirement of a Transfer Descriptor that had its
                                                         IOC bit set. The Host Controller also sets this bit to 1 when a short packet is
                                                         detected (actual number of bytes received was less than the expected number of bytes). */
#else
	uint32_t usbint                       : 1;
	uint32_t usberrint                    : 1;
	uint32_t pcd                          : 1;
	uint32_t flro                         : 1;
	uint32_t hsyserr                      : 1;
	uint32_t ioaa                         : 1;
	uint32_t reserved_6_11                : 6;
	uint32_t hchtd                        : 1;
	uint32_t reclm                        : 1;
	uint32_t pss                          : 1;
	uint32_t ass                          : 1;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_uahcx_ehci_usbsts_s       cn61xx;
	struct cvmx_uahcx_ehci_usbsts_s       cn63xx;
	struct cvmx_uahcx_ehci_usbsts_s       cn63xxp1;
	struct cvmx_uahcx_ehci_usbsts_s       cn66xx;
	struct cvmx_uahcx_ehci_usbsts_s       cn68xx;
	struct cvmx_uahcx_ehci_usbsts_s       cn68xxp1;
	struct cvmx_uahcx_ehci_usbsts_s       cnf71xx;
};
typedef union cvmx_uahcx_ehci_usbsts cvmx_uahcx_ehci_usbsts_t;

/**
 * cvmx_uahc#_ohci0_hcbulkcurrented
 *
 * HCBULKCURRENTED = Host Controller Bulk Current ED Register
 *
 * The HcBulkCurrentED register contains the physical address of the current endpoint of the Bulk list. As the Bulk list will be served in a round-robin
 * fashion, the endpoints will be ordered according to their insertion to the list.
 */
union cvmx_uahcx_ohci0_hcbulkcurrented {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcbulkcurrented_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t bced                         : 28; /**< BulkCurrentED. This is advanced to the next ED after the HC has served the
                                                         present one. HC continues processing the list from where it left off in the
                                                         last Frame. When it reaches the end of the Bulk list, HC checks the
                                                         ControlListFilled of HcControl. If set, it copies the content of HcBulkHeadED
                                                         to HcBulkCurrentED and clears the bit. If it is not set, it does nothing.
                                                         HCD is only allowed to modify this register when the BulkListEnable of
                                                         HcControl is cleared. When set, the HCD only reads the instantaneous value of
                                                         this register. This is initially set to zero to indicate the end of the Bulk
                                                         list. */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t bced                         : 28;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcbulkcurrented_s cn61xx;
	struct cvmx_uahcx_ohci0_hcbulkcurrented_s cn63xx;
	struct cvmx_uahcx_ohci0_hcbulkcurrented_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcbulkcurrented_s cn66xx;
	struct cvmx_uahcx_ohci0_hcbulkcurrented_s cn68xx;
	struct cvmx_uahcx_ohci0_hcbulkcurrented_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcbulkcurrented_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcbulkcurrented cvmx_uahcx_ohci0_hcbulkcurrented_t;

/**
 * cvmx_uahc#_ohci0_hcbulkheaded
 *
 * HCBULKHEADED = Host Controller Bulk Head ED Register
 *
 * The HcBulkHeadED register contains the physical address of the first Endpoint Descriptor of the Bulk list.
 */
union cvmx_uahcx_ohci0_hcbulkheaded {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcbulkheaded_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t bhed                         : 28; /**< BulkHeadED. HC traverses the Bulk list starting with the HcBulkHeadED
                                                         pointer. The content is loaded from HCCA during the initialization of HC. */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t bhed                         : 28;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcbulkheaded_s cn61xx;
	struct cvmx_uahcx_ohci0_hcbulkheaded_s cn63xx;
	struct cvmx_uahcx_ohci0_hcbulkheaded_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcbulkheaded_s cn66xx;
	struct cvmx_uahcx_ohci0_hcbulkheaded_s cn68xx;
	struct cvmx_uahcx_ohci0_hcbulkheaded_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcbulkheaded_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcbulkheaded cvmx_uahcx_ohci0_hcbulkheaded_t;

/**
 * cvmx_uahc#_ohci0_hccommandstatus
 *
 * HCCOMMANDSTATUS = Host Controller Command Status Register
 *
 * The HcCommandStatus register is used by the Host Controller to receive commands issued by the Host Controller Driver, as well as reflecting the
 * current status of the Host Controller. To the Host Controller Driver, it appears to be a "write to set" register. The Host Controller must ensure
 * that bits written as '1' become set in the register while bits written as '0' remain unchanged in the register. The Host Controller Driver
 * may issue multiple distinct commands to the Host Controller without concern for corrupting previously issued commands. The Host Controller Driver
 * has normal read access to all bits.
 * The SchedulingOverrunCount field indicates the number of frames with which the Host Controller has detected the scheduling overrun error. This
 * occurs when the Periodic list does not complete before EOF. When a scheduling overrun error is detected, the Host Controller increments the counter
 * and sets the SchedulingOverrun field in the HcInterruptStatus register.
 */
union cvmx_uahcx_ohci0_hccommandstatus {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hccommandstatus_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_18_31               : 14;
	uint32_t soc                          : 2;  /**< SchedulingOverrunCount. These bits are incremented on each scheduling overrun
                                                         error. It is initialized to 00b and wraps around at 11b. This will be
                                                         incremented when a scheduling overrun is detected even if SchedulingOverrun
                                                         in HcInterruptStatus has already been set. This is used by HCD to monitor
                                                         any persistent scheduling problems. */
	uint32_t reserved_4_15                : 12;
	uint32_t ocr                          : 1;  /**< OwnershipChangeRequest. This bit is set by an OS HCD to request a change of
                                                         control of the HC. When set HC will set the OwnershipChange field in
                                                         HcInterruptStatus. After the changeover, this bit is cleared and remains so
                                                         until the next request from OS HCD. */
	uint32_t blf                          : 1;  /**< BulkListFilled This bit is used to indicate whether there are any TDs on the
                                                         Bulk list. It is set by HCD whenever it adds a TD to an ED in the Bulk list.
                                                         When HC begins to process the head of the Bulk list, it checks BF. As long
                                                         as BulkListFilled is 0, HC will not start processing the Bulk list. If
                                                         BulkListFilled is 1, HC will start processing the Bulk list and will set BF
                                                         to 0. If HC finds a TD on the list, then HC will set BulkListFilled to 1
                                                         causing the Bulk list processing to continue. If no TD is found on the Bulk
                                                         list,and if HCD does not set BulkListFilled, then BulkListFilled will still
                                                         be 0 when HC completes processing the Bulk list and Bulk list processing will
                                                         stop. */
	uint32_t clf                          : 1;  /**< ControlListFilled. This bit is used to indicate whether there are any TDs
                                                         on the Control list. It is set by HCD whenever it adds a TD to an ED in the
                                                         Control list. When HC begins to process the head of the Control list, it
                                                         checks CLF. As long as ControlListFilled is 0, HC will not start processing
                                                         the Control list. If CF is 1, HC will start processing the Control list and
                                                         will set ControlListFilled to 0. If HC finds a TD on the list, then HC will
                                                         set ControlListFilled to 1 causing the Control list processing to continue.
                                                         If no TD is found on the Control list, and if the HCD does not set
                                                         ControlListFilled, then ControlListFilled will still be 0 when HC completes
                                                         processing the Control list and Control list processing will stop. */
	uint32_t hcr                          : 1;  /**< HostControllerReset. This bit is set by HCD to initiate a software reset of
                                                         HC. Regardless of the functional state of HC, it moves to the USBSUSPEND
                                                         state in which most of the operational registers are reset except those
                                                         stated otherwise; e.g., the InterruptRouting field of HcControl, and no
                                                         Host bus accesses are allowed. This bit is cleared by HC upon the
                                                         completion of the reset operation. The reset operation must be completed
                                                         within 10 ms. This bit, when set, should not cause a reset to the Root Hub
                                                         and no subsequent reset signaling should be asserted to its downstream ports. */
#else
	uint32_t hcr                          : 1;
	uint32_t clf                          : 1;
	uint32_t blf                          : 1;
	uint32_t ocr                          : 1;
	uint32_t reserved_4_15                : 12;
	uint32_t soc                          : 2;
	uint32_t reserved_18_31               : 14;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hccommandstatus_s cn61xx;
	struct cvmx_uahcx_ohci0_hccommandstatus_s cn63xx;
	struct cvmx_uahcx_ohci0_hccommandstatus_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hccommandstatus_s cn66xx;
	struct cvmx_uahcx_ohci0_hccommandstatus_s cn68xx;
	struct cvmx_uahcx_ohci0_hccommandstatus_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hccommandstatus_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hccommandstatus cvmx_uahcx_ohci0_hccommandstatus_t;

/**
 * cvmx_uahc#_ohci0_hccontrol
 *
 * HCCONTROL = Host Controller Control Register
 *
 * The HcControl register defines the operating modes for the Host Controller. Most of the fields in this register are modified only by the Host Controller
 * Driver, except HostControllerFunctionalState and RemoteWakeupConnected.
 */
union cvmx_uahcx_ohci0_hccontrol {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hccontrol_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_11_31               : 21;
	uint32_t rwe                          : 1;  /**< RemoteWakeupEnable. This bit is used by HCD to enable or disable the remote wakeup
                                                         feature upon the detection of upstream resume signaling. When this bit is set and
                                                         the ResumeDetected bit in HcInterruptStatus is set, a remote wakeup is signaled
                                                         to the host system. Setting this bit has no impact on the generation of hardware
                                                         interrupt. */
	uint32_t rwc                          : 1;  /**< RemoteWakeupConnected.This bit indicates whether HC supports remote wakeup signaling.
                                                         If remote wakeup is supported and used by the system it is the responsibility of
                                                         system firmware to set this bit during POST. HC clears the bit upon a hardware reset
                                                         but does not alter it upon a software reset. Remote wakeup signaling of the host
                                                         system is host-bus-specific and is not described in this specification. */
	uint32_t ir                           : 1;  /**< InterruptRouting
                                                         This bit determines the routing of interrupts generated by events registered in
                                                         HcInterruptStatus. If clear, all interrupts are routed to the normal host bus
                                                         interrupt mechanism. If set, interrupts are routed to the System Management
                                                         Interrupt. HCD clears this bit upon a hardware reset, but it does not alter
                                                         this bit upon a software reset. HCD uses this bit as a tag to indicate the
                                                         ownership of HC. */
	uint32_t hcfs                         : 2;  /**< HostControllerFunctionalState for USB
                                                          00b: USBRESET
                                                          01b: USBRESUME
                                                          10b: USBOPERATIONAL
                                                          11b: USBSUSPEND
                                                         A transition to USBOPERATIONAL from another state causes SOF generation to begin
                                                         1 ms later. HCD may determine whether HC has begun sending SOFs by reading the
                                                         StartofFrame field of HcInterruptStatus.
                                                         This field may be changed by HC only when in the USBSUSPEND state. HC may move from
                                                         the USBSUSPEND state to the USBRESUME state after detecting the resume signaling
                                                         from a downstream port.
                                                         HC enters USBSUSPEND after a software reset, whereas it enters USBRESET after a
                                                         hardware reset. The latter also resets the Root Hub and asserts subsequent reset
                                                         signaling to downstream ports. */
	uint32_t ble                          : 1;  /**< BulkListEnable. This bit is set to enable the processing of the Bulk list in the
                                                         next Frame. If cleared by HCD, processing of the Bulk list does not occur after
                                                         the next SOF. HC checks this bit whenever it determines to process the list. When
                                                         disabled, HCD may modify the list. If HcBulkCurrentED is pointing to an ED to be
                                                         removed, HCD must advance the pointer by updating HcBulkCurrentED before re-enabling
                                                         processing of the list. */
	uint32_t cle                          : 1;  /**< ControlListEnable. This bit is set to enable the processing of the Control list in
                                                         the next Frame. If cleared by HCD, processing of the Control list does not occur
                                                         after the next SOF. HC must check this bit whenever it determines to process the
                                                         list. When disabled, HCD may modify the list. If HcControlCurrentED is pointing to
                                                         an ED to be removed, HCD must advance the pointer by updating HcControlCurrentED
                                                         before re-enabling processing of the list. */
	uint32_t ie                           : 1;  /**< IsochronousEnable This bit is used by HCD to enable/disable processing of
                                                         isochronous EDs. While processing the periodic list in a Frame, HC checks the
                                                         status of this bit when it finds an Isochronous ED (F=1). If set (enabled), HC
                                                         continues processing the EDs. If cleared (disabled), HC halts processing of the
                                                         periodic list (which now contains only isochronous EDs) and begins processing the
                                                         Bulk/Control lists. Setting this bit is guaranteed to take effect in the next
                                                         Frame (not the current Frame). */
	uint32_t ple                          : 1;  /**< PeriodicListEnable. This bit is set to enable the processing of the periodic list
                                                         in the next Frame. If cleared by HCD, processing of the periodic list does not
                                                         occur after the next SOF. HC must check this bit before it starts processing
                                                         the list. */
	uint32_t cbsr                         : 2;  /**< ControlBulkServiceRatio. This specifies the service ratio between Control and
                                                         Bulk EDs. Before processing any of the nonperiodic lists, HC must compare the
                                                         ratio specified with its internal count on how many nonempty Control EDs have
                                                         been processed, in determining whether to continue serving another Control ED
                                                         or switching to Bulk EDs. The internal count will be retained when crossing
                                                         the frame boundary. In case of reset, HCD is responsible for restoring this
                                                         value.

                                                           CBSR   No. of Control EDs Over Bulk EDs Served
                                                            0             1:1
                                                            1             2:1
                                                            2             3:1
                                                            3             4:1 */
#else
	uint32_t cbsr                         : 2;
	uint32_t ple                          : 1;
	uint32_t ie                           : 1;
	uint32_t cle                          : 1;
	uint32_t ble                          : 1;
	uint32_t hcfs                         : 2;
	uint32_t ir                           : 1;
	uint32_t rwc                          : 1;
	uint32_t rwe                          : 1;
	uint32_t reserved_11_31               : 21;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hccontrol_s   cn61xx;
	struct cvmx_uahcx_ohci0_hccontrol_s   cn63xx;
	struct cvmx_uahcx_ohci0_hccontrol_s   cn63xxp1;
	struct cvmx_uahcx_ohci0_hccontrol_s   cn66xx;
	struct cvmx_uahcx_ohci0_hccontrol_s   cn68xx;
	struct cvmx_uahcx_ohci0_hccontrol_s   cn68xxp1;
	struct cvmx_uahcx_ohci0_hccontrol_s   cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hccontrol cvmx_uahcx_ohci0_hccontrol_t;

/**
 * cvmx_uahc#_ohci0_hccontrolcurrented
 *
 * HCCONTROLCURRENTED = Host Controller Control Current ED Register
 *
 * The HcControlCurrentED register contains the physical address of the current Endpoint Descriptor of the Control list.
 */
union cvmx_uahcx_ohci0_hccontrolcurrented {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hccontrolcurrented_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t cced                         : 28; /**< ControlCurrentED. This pointer is advanced to the next ED after serving the
                                                         present one. HC will continue processing the list from where it left off in
                                                         the last Frame. When it reaches the end of the Control list, HC checks the
                                                         ControlListFilled of in HcCommandStatus. If set, it copies the content of
                                                         HcControlHeadED to HcControlCurrentED and clears the bit. If not set, it
                                                         does nothing. HCD is allowed to modify this register only when the
                                                         ControlListEnable of HcControl is cleared. When set, HCD only reads the
                                                         instantaneous value of this register. Initially, this is set to zero to
                                                         indicate the end of the Control list. */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t cced                         : 28;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hccontrolcurrented_s cn61xx;
	struct cvmx_uahcx_ohci0_hccontrolcurrented_s cn63xx;
	struct cvmx_uahcx_ohci0_hccontrolcurrented_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hccontrolcurrented_s cn66xx;
	struct cvmx_uahcx_ohci0_hccontrolcurrented_s cn68xx;
	struct cvmx_uahcx_ohci0_hccontrolcurrented_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hccontrolcurrented_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hccontrolcurrented cvmx_uahcx_ohci0_hccontrolcurrented_t;

/**
 * cvmx_uahc#_ohci0_hccontrolheaded
 *
 * HCCONTROLHEADED = Host Controller Control Head ED Register
 *
 * The HcControlHeadED register contains the physical address of the first Endpoint Descriptor of the Control list.
 */
union cvmx_uahcx_ohci0_hccontrolheaded {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hccontrolheaded_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ched                         : 28; /**< ControlHeadED. HC traverses the Control list starting with the HcControlHeadED
                                                         pointer. The content is loaded from HCCA during the initialization of HC. */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t ched                         : 28;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hccontrolheaded_s cn61xx;
	struct cvmx_uahcx_ohci0_hccontrolheaded_s cn63xx;
	struct cvmx_uahcx_ohci0_hccontrolheaded_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hccontrolheaded_s cn66xx;
	struct cvmx_uahcx_ohci0_hccontrolheaded_s cn68xx;
	struct cvmx_uahcx_ohci0_hccontrolheaded_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hccontrolheaded_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hccontrolheaded cvmx_uahcx_ohci0_hccontrolheaded_t;

/**
 * cvmx_uahc#_ohci0_hcdonehead
 *
 * HCDONEHEAD = Host Controller Done Head Register
 *
 * The HcDoneHead register contains the physical address of the last completed Transfer Descriptor that was added to the Done queue. In normal operation,
 * the Host Controller Driver should not need to read this register as its content is periodically written to the HCCA.
 */
union cvmx_uahcx_ohci0_hcdonehead {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcdonehead_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t dh                           : 28; /**< DoneHead. When a TD is completed, HC writes the content of HcDoneHead to the
                                                         NextTD field of the TD. HC then overwrites the content of HcDoneHead with the
                                                         address of this TD. This is set to zero whenever HC writes the content of
                                                         this register to HCCA. It also sets the WritebackDoneHead of HcInterruptStatus. */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t dh                           : 28;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcdonehead_s  cn61xx;
	struct cvmx_uahcx_ohci0_hcdonehead_s  cn63xx;
	struct cvmx_uahcx_ohci0_hcdonehead_s  cn63xxp1;
	struct cvmx_uahcx_ohci0_hcdonehead_s  cn66xx;
	struct cvmx_uahcx_ohci0_hcdonehead_s  cn68xx;
	struct cvmx_uahcx_ohci0_hcdonehead_s  cn68xxp1;
	struct cvmx_uahcx_ohci0_hcdonehead_s  cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcdonehead cvmx_uahcx_ohci0_hcdonehead_t;

/**
 * cvmx_uahc#_ohci0_hcfminterval
 *
 * HCFMINTERVAL = Host Controller Frame Interval Register
 *
 * The HcFmInterval register contains a 14-bit value which indicates the bit time interval in a Frame, (i.e., between two consecutive SOFs), and a 15-bit value
 * indicating the Full Speed maximum packet size that the Host Controller may transmit or receive without causing scheduling overrun. The Host Controller Driver
 * may carry out minor adjustment on the FrameInterval by writing a new value over the present one at each SOF. This provides the programmability necessary for
 * the Host Controller to synchronize with an external clocking resource and to adjust any unknown local clock offset.
 */
union cvmx_uahcx_ohci0_hcfminterval {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcfminterval_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t fit                          : 1;  /**< FrameIntervalToggle. HCD toggles this bit whenever it loads a new value to
                                                         FrameInterval. */
	uint32_t fsmps                        : 15; /**< FSLargestDataPacket. This field specifies a value which is loaded into the
                                                         Largest Data Packet Counter at the beginning of each frame. The counter value
                                                         represents the largest amount of data in bits which can be sent or received by
                                                         the HC in a single transaction at any given time without causing scheduling
                                                         overrun. The field value is calculated by the HCD. */
	uint32_t reserved_14_15               : 2;
	uint32_t fi                           : 14; /**< FrameInterval. This specifies the interval between two consecutive SOFs in bit
                                                         times. The nominal value is set to be 11,999. HCD should store the current
                                                         value of this field before resetting HC. By setting the HostControllerReset
                                                         field of HcCommandStatus as this will cause the HC to reset this field to its
                                                         nominal value. HCD may choose to restore the stored value upon the completion
                                                         of the Reset sequence. */
#else
	uint32_t fi                           : 14;
	uint32_t reserved_14_15               : 2;
	uint32_t fsmps                        : 15;
	uint32_t fit                          : 1;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcfminterval_s cn61xx;
	struct cvmx_uahcx_ohci0_hcfminterval_s cn63xx;
	struct cvmx_uahcx_ohci0_hcfminterval_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcfminterval_s cn66xx;
	struct cvmx_uahcx_ohci0_hcfminterval_s cn68xx;
	struct cvmx_uahcx_ohci0_hcfminterval_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcfminterval_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcfminterval cvmx_uahcx_ohci0_hcfminterval_t;

/**
 * cvmx_uahc#_ohci0_hcfmnumber
 *
 * HCFMNUMBER = Host Cotroller Frame Number Register
 *
 * The HcFmNumber register is a 16-bit counter. It provides a timing reference among events happening in the Host Controller and the Host Controller Driver.
 * The Host Controller Driver may use the 16-bit value specified in this register and generate a 32-bit frame number without requiring frequent access to
 * the register.
 */
union cvmx_uahcx_ohci0_hcfmnumber {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcfmnumber_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t fn                           : 16; /**< FrameNumber. This is incremented when HcFmRemaining is re-loaded. It will be
                                                         rolled over to 0h after ffffh. When entering the USBOPERATIONAL state,
                                                         this will be incremented automatically. The content will be written to HCCA
                                                         after HC has incremented the FrameNumber at each frame boundary and sent a
                                                         SOF but before HC reads the first ED in that Frame. After writing to HCCA,
                                                         HC will set the StartofFrame in HcInterruptStatus. */
#else
	uint32_t fn                           : 16;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcfmnumber_s  cn61xx;
	struct cvmx_uahcx_ohci0_hcfmnumber_s  cn63xx;
	struct cvmx_uahcx_ohci0_hcfmnumber_s  cn63xxp1;
	struct cvmx_uahcx_ohci0_hcfmnumber_s  cn66xx;
	struct cvmx_uahcx_ohci0_hcfmnumber_s  cn68xx;
	struct cvmx_uahcx_ohci0_hcfmnumber_s  cn68xxp1;
	struct cvmx_uahcx_ohci0_hcfmnumber_s  cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcfmnumber cvmx_uahcx_ohci0_hcfmnumber_t;

/**
 * cvmx_uahc#_ohci0_hcfmremaining
 *
 * HCFMREMAINING = Host Controller Frame Remaining Register
 * The HcFmRemaining register is a 14-bit down counter showing the bit time remaining in the current Frame.
 */
union cvmx_uahcx_ohci0_hcfmremaining {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcfmremaining_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t frt                          : 1;  /**< FrameRemainingToggle. This bit is loaded from the FrameIntervalToggle field
                                                         of HcFmInterval whenever FrameRemaining reaches 0. This bit is used by HCD
                                                         for the synchronization between FrameInterval and FrameRemaining. */
	uint32_t reserved_14_30               : 17;
	uint32_t fr                           : 14; /**< FrameRemaining. This counter is decremented at each bit time. When it
                                                         reaches zero, it is reset by loading the FrameInterval value specified in
                                                         HcFmInterval at the next bit time boundary. When entering the USBOPERATIONAL
                                                         state, HC re-loads the content with the FrameInterval of HcFmInterval and uses
                                                         the updated value from the next SOF. */
#else
	uint32_t fr                           : 14;
	uint32_t reserved_14_30               : 17;
	uint32_t frt                          : 1;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcfmremaining_s cn61xx;
	struct cvmx_uahcx_ohci0_hcfmremaining_s cn63xx;
	struct cvmx_uahcx_ohci0_hcfmremaining_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcfmremaining_s cn66xx;
	struct cvmx_uahcx_ohci0_hcfmremaining_s cn68xx;
	struct cvmx_uahcx_ohci0_hcfmremaining_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcfmremaining_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcfmremaining cvmx_uahcx_ohci0_hcfmremaining_t;

/**
 * cvmx_uahc#_ohci0_hchcca
 *
 * HCHCCA =  Host Controller Host Controller Communication Area Register
 *
 * The HcHCCA register contains the physical address of the Host Controller Communication Area. The Host Controller Driver determines the alignment restrictions
 * by writing all 1s to HcHCCA and reading the content of HcHCCA. The alignment is evaluated by examining the number of zeroes in the lower order bits. The
 * minimum alignment is 256 bytes; therefore, bits 0 through 7 must always return '0' when read. Detailed description can be found in Chapter 4. This area
 * is used to hold the control structures and the Interrupt table that are accessed by both the Host Controller and the Host Controller Driver.
 */
union cvmx_uahcx_ohci0_hchcca {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hchcca_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t hcca                         : 24; /**< This is the base address (bits [31:8]) of the Host Controller Communication Area. */
	uint32_t reserved_0_7                 : 8;
#else
	uint32_t reserved_0_7                 : 8;
	uint32_t hcca                         : 24;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hchcca_s      cn61xx;
	struct cvmx_uahcx_ohci0_hchcca_s      cn63xx;
	struct cvmx_uahcx_ohci0_hchcca_s      cn63xxp1;
	struct cvmx_uahcx_ohci0_hchcca_s      cn66xx;
	struct cvmx_uahcx_ohci0_hchcca_s      cn68xx;
	struct cvmx_uahcx_ohci0_hchcca_s      cn68xxp1;
	struct cvmx_uahcx_ohci0_hchcca_s      cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hchcca cvmx_uahcx_ohci0_hchcca_t;

/**
 * cvmx_uahc#_ohci0_hcinterruptdisable
 *
 * HCINTERRUPTDISABLE = Host Controller InterruptDisable Register
 *
 * Each disable bit in the HcInterruptDisable register corresponds to an associated interrupt bit in the HcInterruptStatus register. The HcInterruptDisable
 * register is coupled with the HcInterruptEnable register. Thus, writing a '1' to a bit in this register clears the corresponding bit in the HcInterruptEnable
 * register, whereas writing a '0' to a bit in this register leaves the corresponding bit in the HcInterruptEnable register unchanged. On read, the current
 * value of the HcInterruptEnable register is returned.
 */
union cvmx_uahcx_ohci0_hcinterruptdisable {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcinterruptdisable_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t mie                          : 1;  /**< A '0' written to this field is ignored by HC.
                                                         A '1' written to this field disables interrupt generation due to events
                                                         specified in the other bits of this register. This field is set after a
                                                         hardware or software reset. */
	uint32_t oc                           : 1;  /**< 0 - Ignore; 1 - Disable interrupt generation due to Ownership Change. */
	uint32_t reserved_7_29                : 23;
	uint32_t rhsc                         : 1;  /**< 0 - Ignore; 1 - Disable interrupt generation due to Root Hub Status Change. */
	uint32_t fno                          : 1;  /**< 0 - Ignore; 1 - Disable interrupt generation due to Frame Number Overflow. */
	uint32_t ue                           : 1;  /**< 0 - Ignore; 1 - Disable interrupt generation due to Unrecoverable Error. */
	uint32_t rd                           : 1;  /**< 0 - Ignore; 1 - Disable interrupt generation due to Resume Detect. */
	uint32_t sf                           : 1;  /**< 0 - Ignore; 1 - Disable interrupt generation due to Start of Frame. */
	uint32_t wdh                          : 1;  /**< 0 - Ignore; 1 - Disable interrupt generation due to HcDoneHead Writeback. */
	uint32_t so                           : 1;  /**< 0 - Ignore; 1 - Disable interrupt generation due to Scheduling Overrun. */
#else
	uint32_t so                           : 1;
	uint32_t wdh                          : 1;
	uint32_t sf                           : 1;
	uint32_t rd                           : 1;
	uint32_t ue                           : 1;
	uint32_t fno                          : 1;
	uint32_t rhsc                         : 1;
	uint32_t reserved_7_29                : 23;
	uint32_t oc                           : 1;
	uint32_t mie                          : 1;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcinterruptdisable_s cn61xx;
	struct cvmx_uahcx_ohci0_hcinterruptdisable_s cn63xx;
	struct cvmx_uahcx_ohci0_hcinterruptdisable_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcinterruptdisable_s cn66xx;
	struct cvmx_uahcx_ohci0_hcinterruptdisable_s cn68xx;
	struct cvmx_uahcx_ohci0_hcinterruptdisable_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcinterruptdisable_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcinterruptdisable cvmx_uahcx_ohci0_hcinterruptdisable_t;

/**
 * cvmx_uahc#_ohci0_hcinterruptenable
 *
 * HCINTERRUPTENABLE = Host Controller InterruptEnable Register
 *
 * Each enable bit in the HcInterruptEnable register corresponds to an associated interrupt bit in the HcInterruptStatus register. The HcInterruptEnable
 * register is used to control which events generate a hardware interrupt. When a bit is set in the HcInterruptStatus register AND the corresponding bit
 * in the HcInterruptEnable register is set AND the MasterInterruptEnable bit is set, then a hardware interrupt is requested on the host bus.
 * Writing a '1' to a bit in this register sets the corresponding bit, whereas writing a '0' to a bit in this register leaves the corresponding bit
 * unchanged. On read, the current value of this register is returned.
 */
union cvmx_uahcx_ohci0_hcinterruptenable {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcinterruptenable_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t mie                          : 1;  /**< A '0' written to this field is ignored by HC.
                                                         A '1' written to this field enables interrupt generation due to events
                                                         specified in the other bits of this register. This is used by HCD as a Master
                                                         Interrupt Enable. */
	uint32_t oc                           : 1;  /**< 0 - Ignore; 1 - Enable interrupt generation due to Ownership Change. */
	uint32_t reserved_7_29                : 23;
	uint32_t rhsc                         : 1;  /**< 0 - Ignore; 1 - Enable interrupt generation due to Root Hub Status Change. */
	uint32_t fno                          : 1;  /**< 0 - Ignore; 1 - Enable interrupt generation due to Frame Number Overflow. */
	uint32_t ue                           : 1;  /**< 0 - Ignore; 1 - Enable interrupt generation due to Unrecoverable Error. */
	uint32_t rd                           : 1;  /**< 0 - Ignore; 1 - Enable interrupt generation due to Resume Detect. */
	uint32_t sf                           : 1;  /**< 0 - Ignore; 1 - Enable interrupt generation due to Start of Frame. */
	uint32_t wdh                          : 1;  /**< 0 - Ignore; 1 - Enable interrupt generation due to HcDoneHead Writeback. */
	uint32_t so                           : 1;  /**< 0 - Ignore; 1 - Enable interrupt generation due to Scheduling Overrun. */
#else
	uint32_t so                           : 1;
	uint32_t wdh                          : 1;
	uint32_t sf                           : 1;
	uint32_t rd                           : 1;
	uint32_t ue                           : 1;
	uint32_t fno                          : 1;
	uint32_t rhsc                         : 1;
	uint32_t reserved_7_29                : 23;
	uint32_t oc                           : 1;
	uint32_t mie                          : 1;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcinterruptenable_s cn61xx;
	struct cvmx_uahcx_ohci0_hcinterruptenable_s cn63xx;
	struct cvmx_uahcx_ohci0_hcinterruptenable_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcinterruptenable_s cn66xx;
	struct cvmx_uahcx_ohci0_hcinterruptenable_s cn68xx;
	struct cvmx_uahcx_ohci0_hcinterruptenable_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcinterruptenable_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcinterruptenable cvmx_uahcx_ohci0_hcinterruptenable_t;

/**
 * cvmx_uahc#_ohci0_hcinterruptstatus
 *
 * HCINTERRUPTSTATUS = Host Controller InterruptStatus Register
 *
 * This register provides status on various events that cause hardware interrupts. When an event occurs, Host Controller sets the corresponding bit
 * in this register. When a bit becomes set, a hardware interrupt is generated if the interrupt is enabled in the HcInterruptEnable register
 * and the MasterInterruptEnable bit is set. The Host Controller Driver may clear specific bits in this register by writing '1' to bit positions
 * to be cleared. The Host Controller Driver may not set any of these bits. The Host Controller will never clear the bit.
 */
union cvmx_uahcx_ohci0_hcinterruptstatus {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcinterruptstatus_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_31_31               : 1;
	uint32_t oc                           : 1;  /**< OwnershipChange. This bit is set by HC when HCD sets the OwnershipChangeRequest
                                                         field in HcCommandStatus. This event, when unmasked, will always generate an
                                                         System Management Interrupt (SMI) immediately. This bit is tied to 0b when the
                                                         SMI pin is not implemented. */
	uint32_t reserved_7_29                : 23;
	uint32_t rhsc                         : 1;  /**< RootHubStatusChange. This bit is set when the content of HcRhStatus or the
                                                         content of any of HcRhPortStatus[NumberofDownstreamPort] has changed. */
	uint32_t fno                          : 1;  /**< FrameNumberOverflow. This bit is set when the MSb of HcFmNumber (bit 15)
                                                         changes value, from 0 to 1 or from 1 to 0, and after HccaFrameNumber has been
                                                         updated. */
	uint32_t ue                           : 1;  /**< UnrecoverableError. This bit is set when HC detects a system error not related
                                                         to USB. HC should not proceed with any processing nor signaling before the
                                                         system error has been corrected. HCD clears this bit after HC has been reset. */
	uint32_t rd                           : 1;  /**< ResumeDetected. This bit is set when HC detects that a device on the USB is
                                                          asserting resume signaling. It is the transition from no resume signaling to
                                                         resume signaling causing this bit to be set. This bit is not set when HCD
                                                         sets the USBRESUME state. */
	uint32_t sf                           : 1;  /**< StartofFrame. This bit is set by HC at each start of a frame and after the
                                                         update of HccaFrameNumber. HC also generates a SOF token at the same time. */
	uint32_t wdh                          : 1;  /**< WritebackDoneHead. This bit is set immediately after HC has written
                                                         HcDoneHead to HccaDoneHead. Further updates of the HccaDoneHead will not
                                                         occur until this bit has been cleared. HCD should only clear this bit after
                                                         it has saved the content of HccaDoneHead. */
	uint32_t so                           : 1;  /**< SchedulingOverrun. This bit is set when the USB schedule for the current
                                                         Frame overruns and after the update of HccaFrameNumber. A scheduling overrun
                                                         will also cause the SchedulingOverrunCount of HcCommandStatus to be
                                                         incremented. */
#else
	uint32_t so                           : 1;
	uint32_t wdh                          : 1;
	uint32_t sf                           : 1;
	uint32_t rd                           : 1;
	uint32_t ue                           : 1;
	uint32_t fno                          : 1;
	uint32_t rhsc                         : 1;
	uint32_t reserved_7_29                : 23;
	uint32_t oc                           : 1;
	uint32_t reserved_31_31               : 1;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcinterruptstatus_s cn61xx;
	struct cvmx_uahcx_ohci0_hcinterruptstatus_s cn63xx;
	struct cvmx_uahcx_ohci0_hcinterruptstatus_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcinterruptstatus_s cn66xx;
	struct cvmx_uahcx_ohci0_hcinterruptstatus_s cn68xx;
	struct cvmx_uahcx_ohci0_hcinterruptstatus_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcinterruptstatus_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcinterruptstatus cvmx_uahcx_ohci0_hcinterruptstatus_t;

/**
 * cvmx_uahc#_ohci0_hclsthreshold
 *
 * HCLSTHRESHOLD = Host Controller LS Threshold Register
 *
 * The HcLSThreshold register contains an 11-bit value used by the Host Controller to determine whether to commit to the transfer of a maximum of 8-byte
 * LS packet before EOF. Neither the Host Controller nor the Host Controller Driver are allowed to change this value.
 */
union cvmx_uahcx_ohci0_hclsthreshold {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hclsthreshold_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_12_31               : 20;
	uint32_t lst                          : 12; /**< LSThreshold
                                                         This field contains a value which is compared to the FrameRemaining field
                                                         prior to initiating a Low Speed transaction. The transaction is started only
                                                         if FrameRemaining >= this field. The value is calculated by HCD
                                                         with the consideration of transmission and setup overhead. */
#else
	uint32_t lst                          : 12;
	uint32_t reserved_12_31               : 20;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hclsthreshold_s cn61xx;
	struct cvmx_uahcx_ohci0_hclsthreshold_s cn63xx;
	struct cvmx_uahcx_ohci0_hclsthreshold_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hclsthreshold_s cn66xx;
	struct cvmx_uahcx_ohci0_hclsthreshold_s cn68xx;
	struct cvmx_uahcx_ohci0_hclsthreshold_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hclsthreshold_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hclsthreshold cvmx_uahcx_ohci0_hclsthreshold_t;

/**
 * cvmx_uahc#_ohci0_hcperiodcurrented
 *
 * HCPERIODCURRENTED = Host Controller Period Current ED Register
 *
 * The HcPeriodCurrentED register contains the physical address of the current Isochronous or Interrupt Endpoint Descriptor.
 */
union cvmx_uahcx_ohci0_hcperiodcurrented {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcperiodcurrented_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t pced                         : 28; /**< PeriodCurrentED. This is used by HC to point to the head of one of the
                                                         Periodic lists which will be processed in the current Frame. The content of
                                                         this register is updated by HC after a periodic ED has been processed. HCD
                                                         may read the content in determining which ED is currently being processed
                                                         at the time of reading. */
	uint32_t reserved_0_3                 : 4;
#else
	uint32_t reserved_0_3                 : 4;
	uint32_t pced                         : 28;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcperiodcurrented_s cn61xx;
	struct cvmx_uahcx_ohci0_hcperiodcurrented_s cn63xx;
	struct cvmx_uahcx_ohci0_hcperiodcurrented_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcperiodcurrented_s cn66xx;
	struct cvmx_uahcx_ohci0_hcperiodcurrented_s cn68xx;
	struct cvmx_uahcx_ohci0_hcperiodcurrented_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcperiodcurrented_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcperiodcurrented cvmx_uahcx_ohci0_hcperiodcurrented_t;

/**
 * cvmx_uahc#_ohci0_hcperiodicstart
 *
 * HCPERIODICSTART = Host Controller Periodic Start Register
 *
 * The HcPeriodicStart register has a 14-bit programmable value which determines when is the earliest time HC should start processing the periodic list.
 */
union cvmx_uahcx_ohci0_hcperiodicstart {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcperiodicstart_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_14_31               : 18;
	uint32_t ps                           : 14; /**< PeriodicStart After a hardware reset, this field is cleared. This is then set
                                                         by HCD during the HC initialization. The value is calculated roughly as 10%
                                                         off from HcFmInterval.. A typical value will be 3E67h. When HcFmRemaining
                                                         reaches the value specified, processing of the periodic lists will have
                                                         priority over Control/Bulk processing. HC will therefore start processing
                                                         the Interrupt list after completing the current Control or Bulk transaction
                                                         that is in progress. */
#else
	uint32_t ps                           : 14;
	uint32_t reserved_14_31               : 18;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcperiodicstart_s cn61xx;
	struct cvmx_uahcx_ohci0_hcperiodicstart_s cn63xx;
	struct cvmx_uahcx_ohci0_hcperiodicstart_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcperiodicstart_s cn66xx;
	struct cvmx_uahcx_ohci0_hcperiodicstart_s cn68xx;
	struct cvmx_uahcx_ohci0_hcperiodicstart_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcperiodicstart_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcperiodicstart cvmx_uahcx_ohci0_hcperiodicstart_t;

/**
 * cvmx_uahc#_ohci0_hcrevision
 *
 * HCREVISION = Host Controller Revision Register
 *
 */
union cvmx_uahcx_ohci0_hcrevision {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcrevision_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_8_31                : 24;
	uint32_t rev                          : 8;  /**< Revision This read-only field contains the BCD representation of the version
                                                         of the HCI specification that is implemented by this HC. For example, a value
                                                         of 11h corresponds to version 1.1. All of the HC implementations that are
                                                         compliant with this specification will have a value of 10h. */
#else
	uint32_t rev                          : 8;
	uint32_t reserved_8_31                : 24;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcrevision_s  cn61xx;
	struct cvmx_uahcx_ohci0_hcrevision_s  cn63xx;
	struct cvmx_uahcx_ohci0_hcrevision_s  cn63xxp1;
	struct cvmx_uahcx_ohci0_hcrevision_s  cn66xx;
	struct cvmx_uahcx_ohci0_hcrevision_s  cn68xx;
	struct cvmx_uahcx_ohci0_hcrevision_s  cn68xxp1;
	struct cvmx_uahcx_ohci0_hcrevision_s  cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcrevision cvmx_uahcx_ohci0_hcrevision_t;

/**
 * cvmx_uahc#_ohci0_hcrhdescriptora
 *
 * HCRHDESCRIPTORA = Host Controller Root Hub DescriptorA Register
 *
 * The HcRhDescriptorA register is the first register of two describing the characteristics of the Root Hub. Reset values are implementation-specific.
 * The descriptor length (11), descriptor type (0x29), and hub controller current (0) fields of the hub Class Descriptor are emulated by the HCD. All
 * other fields are located in the HcRhDescriptorA and HcRhDescriptorB registers.
 */
union cvmx_uahcx_ohci0_hcrhdescriptora {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcrhdescriptora_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t potpgt                       : 8;  /**< PowerOnToPowerGoodTime. This byte specifies the duration HCD has to wait before
                                                         accessing a powered-on port of the Root Hub. It is implementation-specific. The
                                                         unit of time is 2 ms. The duration is calculated as POTPGT * 2 ms. */
	uint32_t reserved_13_23               : 11;
	uint32_t nocp                         : 1;  /**< NoOverCurrentProtection. This bit describes how the overcurrent status for the
                                                         Root Hub ports are reported. When this bit is cleared, the
                                                         OverCurrentProtectionMode field specifies global or per-port reporting.
                                                         - 0: Over-current status is reported  collectively for all downstream ports
                                                         - 1: No overcurrent protection supported */
	uint32_t ocpm                         : 1;  /**< OverCurrentProtectionMode. This bit describes how the overcurrent status for
                                                         the Root Hub ports are reported. At reset, this fields should reflect the same
                                                         mode as PowerSwitchingMode. This field is valid only if the
                                                         NoOverCurrentProtection field is cleared. 0: over-current status is reported
                                                         collectively for all downstream ports 1: over-current status is reported on a
                                                         per-port basis */
	uint32_t dt                           : 1;  /**< DeviceType. This bit specifies that the Root Hub is not a compound device. The
                                                         Root Hub is not permitted to be a compound device. This field should always
                                                         read/write 0. */
	uint32_t psm                          : 1;  /**< PowerSwitchingMode. This bit is used to specify how the power switching of
                                                         the Root Hub ports is controlled. It is implementation-specific. This field
                                                         is only valid if the NoPowerSwitching field is cleared. 0: all ports are
                                                         powered at the same time. 1: each port is powered individually.  This mode
                                                         allows port power to be controlled by either the global switch or per-port
                                                         switching. If the PortPowerControlMask bit is set, the port responds only
                                                         to port power commands (Set/ClearPortPower). If the port mask is cleared,
                                                         then the port is controlled only by the global power switch
                                                         (Set/ClearGlobalPower). */
	uint32_t nps                          : 1;  /**< NoPowerSwitching These bits are used to specify whether power switching is
                                                         supported or port are always powered. It is implementation-specific. When
                                                         this bit is cleared, the PowerSwitchingMode specifies global or per-port
                                                         switching.
                                                          - 0: Ports are power switched
                                                          - 1: Ports are always powered on when the HC is powered on */
	uint32_t ndp                          : 8;  /**< NumberDownstreamPorts. These bits specify the number of downstream ports
                                                         supported by the Root Hub. It is implementation-specific. The minimum number
                                                         of ports is 1. The maximum number of ports supported by OpenHCI is 15. */
#else
	uint32_t ndp                          : 8;
	uint32_t nps                          : 1;
	uint32_t psm                          : 1;
	uint32_t dt                           : 1;
	uint32_t ocpm                         : 1;
	uint32_t nocp                         : 1;
	uint32_t reserved_13_23               : 11;
	uint32_t potpgt                       : 8;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcrhdescriptora_s cn61xx;
	struct cvmx_uahcx_ohci0_hcrhdescriptora_s cn63xx;
	struct cvmx_uahcx_ohci0_hcrhdescriptora_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcrhdescriptora_s cn66xx;
	struct cvmx_uahcx_ohci0_hcrhdescriptora_s cn68xx;
	struct cvmx_uahcx_ohci0_hcrhdescriptora_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcrhdescriptora_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcrhdescriptora cvmx_uahcx_ohci0_hcrhdescriptora_t;

/**
 * cvmx_uahc#_ohci0_hcrhdescriptorb
 *
 * HCRHDESCRIPTORB = Host Controller Root Hub DescriptorB Register
 *
 * The HcRhDescriptorB register is the second register of two describing the characteristics of the Root Hub. These fields are written during
 * initialization to correspond with the system implementation. Reset values are implementation-specific.
 */
union cvmx_uahcx_ohci0_hcrhdescriptorb {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcrhdescriptorb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t ppcm                         : 16; /**< PortPowerControlMask.
                                                         Each bit indicates if a port is affected by a global power control command
                                                         when PowerSwitchingMode is set. When set, the port's power state is only
                                                         affected by per-port power control (Set/ClearPortPower). When cleared, the
                                                         port is controlled by the global power switch (Set/ClearGlobalPower). If
                                                         the device is configured to global switching mode (PowerSwitchingMode=0),
                                                         this field is not valid.
                                                            bit 0: Reserved
                                                            bit 1: Ganged-power mask on Port \#1
                                                            bit 2: Ganged-power mask on Port \#2
                                                            - ...
                                                            bit15: Ganged-power mask on Port \#15 */
	uint32_t dr                           : 16; /**< DeviceRemovable.
                                                         Each bit is dedicated to a port of the Root Hub. When cleared,the attached
                                                         device is removable. When set, the attached device is not removable.
                                                             bit 0: Reserved
                                                             bit 1: Device attached to Port \#1
                                                             bit 2: Device attached to Port \#2
                                                             - ...
                                                             bit15: Device attached to Port \#15 */
#else
	uint32_t dr                           : 16;
	uint32_t ppcm                         : 16;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcrhdescriptorb_s cn61xx;
	struct cvmx_uahcx_ohci0_hcrhdescriptorb_s cn63xx;
	struct cvmx_uahcx_ohci0_hcrhdescriptorb_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcrhdescriptorb_s cn66xx;
	struct cvmx_uahcx_ohci0_hcrhdescriptorb_s cn68xx;
	struct cvmx_uahcx_ohci0_hcrhdescriptorb_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcrhdescriptorb_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcrhdescriptorb cvmx_uahcx_ohci0_hcrhdescriptorb_t;

/**
 * cvmx_uahc#_ohci0_hcrhportstatus#
 *
 * HCRHPORTSTATUSX = Host Controller Root Hub Port X Status Registers
 *
 * The HcRhPortStatus[1:NDP] register is used to control and report port events on a per-port basis. NumberDownstreamPorts represents the number
 * of HcRhPortStatus registers that are implemented in hardware. The lower word is used to reflect the port status, whereas the upper word reflects
 * the status change bits. Some status bits are implemented with special write behavior (see below). If a transaction (token through handshake) is
 * in progress when a write to change port status occurs, the resulting port status change must be postponed until the transaction completes.
 * Reserved bits should always be written '0'.
 */
union cvmx_uahcx_ohci0_hcrhportstatusx {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcrhportstatusx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_21_31               : 11;
	uint32_t prsc                         : 1;  /**< PortResetStatusChange. This bit is set at the end of the 10-ms port reset
                                                         signal. The HCD writes a '1' to clear this bit. Writing a '0' has no effect.
                                                            0 = port reset is not complete
                                                            1 = port reset is complete */
	uint32_t ocic                         : 1;  /**< PortOverCurrentIndicatorChange. This bit is valid only if overcurrent
                                                         conditions are reported on a per-port basis. This bit is set when Root Hub
                                                         changes the PortOverCurrentIndicator bit. The HCD writes a '1' to clear this
                                                         bit. Writing a  '0'  has no effect.
                                                            0 = no change in PortOverCurrentIndicator
                                                            1 = PortOverCurrentIndicator has changed */
	uint32_t pssc                         : 1;  /**< PortSuspendStatusChange. This bit is set when the full resume sequence has
                                                         been completed. This sequence includes the 20-s resume pulse, LS EOP, and
                                                         3-ms resychronization delay.
                                                         The HCD writes a '1' to clear this bit. Writing a '0' has no effect. This
                                                         bit is also cleared when ResetStatusChange is set.
                                                            0 = resume is not completed
                                                            1 = resume completed */
	uint32_t pesc                         : 1;  /**< PortEnableStatusChange. This bit is set when hardware events cause the
                                                         PortEnableStatus bit to be cleared. Changes from HCD writes do not set this
                                                         bit. The HCD writes a '1' to clear this bit. Writing a '0' has no effect.
                                                           0 = no change in PortEnableStatus
                                                           1 = change in PortEnableStatus */
	uint32_t csc                          : 1;  /**< ConnectStatusChange. This bit is set whenever a connect or disconnect event
                                                         occurs. The HCD writes a '1' to clear this bit. Writing a '0' has no
                                                         effect. If CurrentConnectStatus is cleared when a SetPortReset,SetPortEnable,
                                                         or SetPortSuspend write occurs, this bit is set to force the driver to
                                                         re-evaluate the connection status since these writes should not occur if the
                                                         port is disconnected.
                                                           0 = no change in CurrentConnectStatus
                                                           1 = change in CurrentConnectStatus
                                                         Note: If the DeviceRemovable[NDP] bit is set, this bit is set only after a
                                                          Root Hub reset to inform the system that the device is attached. Description */
	uint32_t reserved_10_15               : 6;
	uint32_t lsda                         : 1;  /**< (read) LowSpeedDeviceAttached. This bit indicates the speed of the device
                                                              attached to this port. When set, a Low Speed device is attached to this
                                                              port. When clear, a Full Speed device is attached to this port. This
                                                              field is valid only when the CurrentConnectStatus is set.
                                                                 0 = full speed device attached
                                                                 1 = low speed device attached
                                                         (write) ClearPortPower. The HCD clears the PortPowerStatus bit by writing a
                                                              '1' to this bit. Writing a '0' has no effect. */
	uint32_t pps                          : 1;  /**< (read) PortPowerStatus. This bit reflects the port's power status, regardless
                                                             of the type of power switching implemented. This bit is cleared if an
                                                             overcurrent condition is detected. HCD sets this bit by writing
                                                             SetPortPower or SetGlobalPower. HCD clears this bit by writing
                                                             ClearPortPower or ClearGlobalPower. Which power control switches are
                                                             enabled is determined by PowerSwitchingMode and PortPortControlMask[NDP].
                                                             In global switching mode (PowerSwitchingMode=0), only Set/ClearGlobalPower
                                                               controls this bit. In per-port power switching (PowerSwitchingMode=1),
                                                               if the PortPowerControlMask[NDP] bit for the port is set, only
                                                               Set/ClearPortPower commands are enabled. If the mask is not set, only
                                                               Set/ClearGlobalPower commands are enabled. When port power is disabled,
                                                               CurrentConnectStatus, PortEnableStatus, PortSuspendStatus, and
                                                               PortResetStatus should be reset.
                                                                  0 = port power is off
                                                                  1 = port power is on
                                                         (write) SetPortPower. The HCD writes a '1' to set the PortPowerStatus bit.
                                                               Writing a '0' has no effect. Note: This bit is always reads '1'
                                                               if power switching is not supported. */
	uint32_t reserved_5_7                 : 3;
	uint32_t prs                          : 1;  /**< (read) PortResetStatus. When this bit is set by a write to SetPortReset, port
                                                               reset signaling is asserted. When reset is completed, this bit is
                                                               cleared when PortResetStatusChange is set. This bit cannot be set if
                                                               CurrentConnectStatus is cleared.
                                                                  0 = port reset signal is not active
                                                               1 = port reset signal is active
                                                         (write) SetPortReset. The HCD sets the port reset signaling by writing a '1'
                                                               to this bit. Writing a '0'has no effect. If CurrentConnectStatus is
                                                               cleared, this write does not set PortResetStatus, but instead sets
                                                               ConnectStatusChange. This informs the driver that it attempted to reset
                                                               a disconnected port. Description */
	uint32_t poci                         : 1;  /**< (read) PortOverCurrentIndicator. This bit is only valid when the Root Hub is
                                                                configured in such a way that overcurrent conditions are reported on a
                                                                per-port basis. If per-port overcurrent reporting is not supported, this
                                                                bit is set to 0. If cleared, all power operations are normal for this
                                                                port. If set, an overcurrent condition exists on this port. This bit
                                                                always reflects the overcurrent input signal
                                                                  0 = no overcurrent condition.
                                                                  1 = overcurrent condition detected.
                                                         (write) ClearSuspendStatus. The HCD writes a '1' to initiate a resume.
                                                                 Writing  a '0' has no effect. A resume is initiated only if
                                                                 PortSuspendStatus is set. */
	uint32_t pss                          : 1;  /**< (read) PortSuspendStatus. This bit indicates the port is suspended or in the
                                                              resume sequence. It is set by a SetSuspendState write and cleared when
                                                              PortSuspendStatusChange is set at the end of the resume interval. This
                                                              bit cannot be set if CurrentConnectStatus is cleared. This bit is also
                                                              cleared when PortResetStatusChange is set at the end of the port reset
                                                              or when the HC is placed in the USBRESUME state. If an upstream resume is
                                                              in progress, it should propagate to the HC.
                                                                 0 = port is not suspended
                                                                 1 = port is suspended
                                                         (write) SetPortSuspend. The HCD sets the PortSuspendStatus bit by writing a
                                                              '1' to this bit. Writing a '0' has no effect. If CurrentConnectStatus
                                                                is cleared, this write does not set PortSuspendStatus; instead it sets
                                                                ConnectStatusChange.This informs the driver that it attempted to suspend
                                                                a disconnected port. */
	uint32_t pes                          : 1;  /**< (read) PortEnableStatus. This bit indicates whether the port is enabled or
                                                              disabled. The Root Hub may clear this bit when an overcurrent condition,
                                                              disconnect event, switched-off power, or operational bus error such
                                                              as babble is detected. This change also causes PortEnabledStatusChange
                                                              to be set. HCD sets this bit by writing SetPortEnable and clears it by
                                                              writing ClearPortEnable. This bit cannot be set when CurrentConnectStatus
                                                              is cleared. This bit is also set, if not already, at the completion of a
                                                              port reset when ResetStatusChange is set or port suspend when
                                                              SuspendStatusChange is set.
                                                                0 = port is disabled
                                                                1 = port is enabled
                                                         (write) SetPortEnable. The HCD sets PortEnableStatus by writing a '1'.
                                                              Writing a '0' has no effect. If CurrentConnectStatus is cleared, this
                                                              write does not set PortEnableStatus, but instead sets ConnectStatusChange.
                                                              This informs the driver that it attempted to enable a disconnected port. */
	uint32_t ccs                          : 1;  /**< (read) CurrentConnectStatus. This bit reflects the current state of the
                                                               downstream port.
                                                                 0 = no device connected
                                                                 1 = device connected
                                                         (write) ClearPortEnable.
                                                                The HCD writes a '1' to this bit to clear the PortEnableStatus bit.
                                                                Writing a '0' has no effect. The CurrentConnectStatus is not
                                                                affected by any write.
                                                          Note: This bit is always read '1b' when the attached device is
                                                                nonremovable (DeviceRemoveable[NDP]). */
#else
	uint32_t ccs                          : 1;
	uint32_t pes                          : 1;
	uint32_t pss                          : 1;
	uint32_t poci                         : 1;
	uint32_t prs                          : 1;
	uint32_t reserved_5_7                 : 3;
	uint32_t pps                          : 1;
	uint32_t lsda                         : 1;
	uint32_t reserved_10_15               : 6;
	uint32_t csc                          : 1;
	uint32_t pesc                         : 1;
	uint32_t pssc                         : 1;
	uint32_t ocic                         : 1;
	uint32_t prsc                         : 1;
	uint32_t reserved_21_31               : 11;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcrhportstatusx_s cn61xx;
	struct cvmx_uahcx_ohci0_hcrhportstatusx_s cn63xx;
	struct cvmx_uahcx_ohci0_hcrhportstatusx_s cn63xxp1;
	struct cvmx_uahcx_ohci0_hcrhportstatusx_s cn66xx;
	struct cvmx_uahcx_ohci0_hcrhportstatusx_s cn68xx;
	struct cvmx_uahcx_ohci0_hcrhportstatusx_s cn68xxp1;
	struct cvmx_uahcx_ohci0_hcrhportstatusx_s cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcrhportstatusx cvmx_uahcx_ohci0_hcrhportstatusx_t;

/**
 * cvmx_uahc#_ohci0_hcrhstatus
 *
 * HCRHSTATUS = Host Controller Root Hub Status Register
 *
 * The HcRhStatus register is divided into two parts. The lower word of a Dword represents the Hub Status field and the upper word represents the Hub
 * Status Change field. Reserved bits should always be written '0'.
 */
union cvmx_uahcx_ohci0_hcrhstatus {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_hcrhstatus_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t crwe                         : 1;  /**< (write) ClearRemoteWakeupEnable Writing a '1' clears DeviceRemoveWakeupEnable.
                                                         Writing a '0' has no effect. */
	uint32_t reserved_18_30               : 13;
	uint32_t ccic                         : 1;  /**< OverCurrentIndicatorChange. This bit is set by hardware when a change has
                                                         occurred to the OCI field of this register. The HCD clears this bit by
                                                         writing a '1'. Writing a '0' has no effect. */
	uint32_t lpsc                         : 1;  /**< (read) LocalPowerStatusChange. The Root Hub does not support the local power
                                                                status feature; thus, this bit is always read as '0'.
                                                         (write) SetGlobalPower In global power mode (PowerSwitchingMode=0), This bit
                                                                 is written to '1' to turn on power to all ports (clear PortPowerStatus).
                                                                 In per-port power mode, it sets PortPowerStatus only on ports whose
                                                                 PortPowerControlMask bit is not set. Writing a '0' has no effect. */
	uint32_t drwe                         : 1;  /**< (read) DeviceRemoteWakeupEnable. This bit enables a ConnectStatusChange bit as
                                                                a resume event, causing a USBSUSPEND to USBRESUME state transition and
                                                                setting the ResumeDetected interrupt. 0 = ConnectStatusChange is not a
                                                                remote wakeup event. 1 = ConnectStatusChange is a remote wakeup event.
                                                         (write) SetRemoteWakeupEnable Writing a '1' sets DeviceRemoveWakeupEnable.
                                                                 Writing a '0' has no effect. */
	uint32_t reserved_2_14                : 13;
	uint32_t oci                          : 1;  /**< OverCurrentIndicator. This bit reports overcurrent conditions when the global
                                                         reporting is implemented. When set, an overcurrent condition exists. When
                                                         cleared, all power operations are normal. If per-port overcurrent protection
                                                         is implemented this bit is always '0' */
	uint32_t lps                          : 1;  /**< (read)  LocalPowerStatus. The Root Hub does not support the local power status
                                                                 feature; thus, this bit is always read as '0.
                                                         (write) ClearGlobalPower. In global power mode (PowerSwitchingMode=0), This
                                                                 bit is written to '1' to turn off power to all ports
                                                                 (clear PortPowerStatus). In per-port power mode, it clears
                                                                 PortPowerStatus only on ports whose PortPowerControlMask bit is not
                                                                 set. Writing a '0' has no effect. Description */
#else
	uint32_t lps                          : 1;
	uint32_t oci                          : 1;
	uint32_t reserved_2_14                : 13;
	uint32_t drwe                         : 1;
	uint32_t lpsc                         : 1;
	uint32_t ccic                         : 1;
	uint32_t reserved_18_30               : 13;
	uint32_t crwe                         : 1;
#endif
	} s;
	struct cvmx_uahcx_ohci0_hcrhstatus_s  cn61xx;
	struct cvmx_uahcx_ohci0_hcrhstatus_s  cn63xx;
	struct cvmx_uahcx_ohci0_hcrhstatus_s  cn63xxp1;
	struct cvmx_uahcx_ohci0_hcrhstatus_s  cn66xx;
	struct cvmx_uahcx_ohci0_hcrhstatus_s  cn68xx;
	struct cvmx_uahcx_ohci0_hcrhstatus_s  cn68xxp1;
	struct cvmx_uahcx_ohci0_hcrhstatus_s  cnf71xx;
};
typedef union cvmx_uahcx_ohci0_hcrhstatus cvmx_uahcx_ohci0_hcrhstatus_t;

/**
 * cvmx_uahc#_ohci0_insnreg06
 *
 * OHCI0_INSNREG06 = OHCI  AHB Error Status Register (Synopsys Speicific)
 *
 * This register contains AHB Error Status.
 */
union cvmx_uahcx_ohci0_insnreg06 {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_insnreg06_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t vld                          : 1;  /**< AHB Error Captured. Indicator that an AHB error was encountered and values were captured.
                                                         To clear this field the application must write a 0 to it. */
	uint32_t reserved_0_30                : 31;
#else
	uint32_t reserved_0_30                : 31;
	uint32_t vld                          : 1;
#endif
	} s;
	struct cvmx_uahcx_ohci0_insnreg06_s   cn61xx;
	struct cvmx_uahcx_ohci0_insnreg06_s   cn63xx;
	struct cvmx_uahcx_ohci0_insnreg06_s   cn63xxp1;
	struct cvmx_uahcx_ohci0_insnreg06_s   cn66xx;
	struct cvmx_uahcx_ohci0_insnreg06_s   cn68xx;
	struct cvmx_uahcx_ohci0_insnreg06_s   cn68xxp1;
	struct cvmx_uahcx_ohci0_insnreg06_s   cnf71xx;
};
typedef union cvmx_uahcx_ohci0_insnreg06 cvmx_uahcx_ohci0_insnreg06_t;

/**
 * cvmx_uahc#_ohci0_insnreg07
 *
 * OHCI0_INSNREG07 = OHCI  AHB Error Address Register (Synopsys Speicific)
 *
 * This register contains AHB Error Status.
 */
union cvmx_uahcx_ohci0_insnreg07 {
	uint32_t u32;
	struct cvmx_uahcx_ohci0_insnreg07_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t err_addr                     : 32; /**< AHB Master Error Address. AHB address of the control phase at which the AHB error occurred */
#else
	uint32_t err_addr                     : 32;
#endif
	} s;
	struct cvmx_uahcx_ohci0_insnreg07_s   cn61xx;
	struct cvmx_uahcx_ohci0_insnreg07_s   cn63xx;
	struct cvmx_uahcx_ohci0_insnreg07_s   cn63xxp1;
	struct cvmx_uahcx_ohci0_insnreg07_s   cn66xx;
	struct cvmx_uahcx_ohci0_insnreg07_s   cn68xx;
	struct cvmx_uahcx_ohci0_insnreg07_s   cn68xxp1;
	struct cvmx_uahcx_ohci0_insnreg07_s   cnf71xx;
};
typedef union cvmx_uahcx_ohci0_insnreg07 cvmx_uahcx_ohci0_insnreg07_t;

#endif
